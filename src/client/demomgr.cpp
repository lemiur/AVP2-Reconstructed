// Talon client demo recording and playback (no Jupiter equivalent).
// A demo file holds a version dword, 16 reserved dwords, the world name and the start time,
// then per frame a DEMOSYNC_TIME block (the frame's time) and a DEMOSYNC_INPUT block (axis
// offsets and command changes), and finally DEMOSYNC_END. Playing a demo also prints the
// PlayDemo profile counters and, with PlayDemoReps, the spread over all runs.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "iclientshell.h"
#include "dsys_interface.h"
#include "streamsim.h"
#include "counter.h"
#include "input.h"
#include "demomgr.h"
#include "render.h"		// g_Render (the renderer profile counters)
#include "clientshell.h"
#include "de_file.h"
#include "de_memory.h"
#include "engine_vars.h"

// 0x0049c130 (timemgr.cpp)
float time_GetTime();
// 0x00430710 (de_objects.cpp)
void DebugOut(const char *pMsg, ...);


// The renderer profile counters (0x004e48e0-0x004e48f0) are RenderStruct members: g_Render.m_Ticks_TagVisibleLeaves..m_Ticks_Translucent.

// Name the emitted literal at its first byte so its relocation is symbol+0, not DAT_004d1afb+1.
// GLOBAL: LITHTECH 0x004d1afc ??_C@_0BE@ECIK@?5?5?5?5?5?5?5?5Translucent?$AA@

// A profile counter and its range over the PlayDemoReps runs.
struct PDCounter
{
	PDCounter(uint32 *pValue, const char *pName)
	{
		m_pValue = pValue;
		m_pName = pName;
		m_Min = 0xFFFFFFFF;
		m_Max = 0;
	}

	uint32		*m_pValue;
	const char	*m_pName;
	uint32		m_Min;
	uint32		m_Max;
};

// GLOBAL: LITHTECH 0x004e3528
PDCounter g_PDCounters[] =
{
	PDCounter(&g_PD_FOpen, "FOpen"),
	PDCounter(&g_PD_Malloc, "Malloc"),
	PDCounter(&g_PD_Free, "Free"),
	PDCounter(&g_Ticks_Music, "Music"),
	PDCounter(&g_Ticks_Sound, "Sound"),
	PDCounter(&g_Ticks_Input, "Input"),
	PDCounter(&g_Ticks_ClientShell, "ClientShell"),
	PDCounter(&g_Ticks_NetUpdate, "  Net"),
	PDCounter(&g_Ticks_ServerUpdate, "  Server"),
	PDCounter(&g_Ticks_ProcessPackets, "  ProcessPackets"),
	PDCounter(&g_Ticks_GameClientShell, "  GameClientShell"),
	PDCounter(&g_Ticks_Render, "    Render"),
	PDCounter(&g_Render.m_Ticks_TagVisibleLeaves, "      TagVisibleLeaves"),
	PDCounter(&g_Render.m_Ticks_FlushObjectQueues, "      FlushObjectQueues"),
	PDCounter(&g_Render.m_Ticks_Models, "        Models"),
	PDCounter(&g_Render.m_Ticks_WorldModels, "        WorldModels"),
	PDCounter(&g_Render.m_Ticks_Translucent, "        Translucent"),
};

#define NUM_PDCOUNTERS	(sizeof(g_PDCounters) / sizeof(g_PDCounters[0]))

// FUNCTION: LITHTECH 0x004328c0 _$E2
// FUNCTION: LITHTECH 0x004328d0 _$E1

// Range of the PlayDemoReps runs.
// GLOBAL: LITHTECH 0x004d1af4
float g_MinDemoTime = 100000.0;
// GLOBAL: LITHTECH 0x004d1af8
float g_MinDemoFPS = 100000.0;
// GLOBAL: LITHTECH 0x004e3638
float g_MaxDemoTime = 0.0f;
// GLOBAL: LITHTECH 0x004e363c
float g_MaxDemoFPS = 0.0f;


static void DemoPrint(const char *pMsg, ...);


// FUNCTION: LITHTECH 0x00432af0
CDemoMgr::CDemoMgr()
{
	m_pClientMgr = LTNULL;
	m_pStream = LTNULL;
	m_State = DEMO_NONE;
	m_TimeOffset = 0.0f;
	m_pSaveStream = LTNULL;
}

// FUNCTION: LITHTECH 0x00432b10
CDemoMgr::~CDemoMgr()
{
	StopDemo();
}

// FUNCTION: LITHTECH 0x00432b20
LTBOOL CDemoMgr::IsConsoleUp()
{
	if(m_State == DEMO_RECORDING || m_State == DEMO_PLAYING)
		return LTFALSE;

	return dsi_IsConsoleUp();
}

// FUNCTION: LITHTECH 0x00432b40
void CDemoMgr::SRand()
{
	srand(123);

	if(m_pClientMgr && m_pClientMgr->m_pClientShell)
		m_pClientMgr->m_pClientShell->SRand();
}

// FUNCTION: LITHTECH 0x00432b70
void CDemoMgr::DemoSerialize(ILTStream *pStream, LTBOOL bLoad)
{
	if(m_pClientMgr && m_pClientMgr->m_pClientShell)
		m_pClientMgr->m_pClientShell->DemoSerialize(pStream, bLoad);
}

// FUNCTION: LITHTECH 0x00432b90
LTRESULT CDemoMgr::RecordDemo(char *pWorldName, const char *pFilename)
{
	int i;

	StopDemo();

	m_pStream = streamsim_Open(pFilename, "wb");
	if(!m_pStream)
		return LT_ERROR;

	m_pStream->WriteVal((uint32)DEMO_VERSION);
	for(i=0; i < 16; i++)
		m_pStream->WriteVal((uint32)0);

	m_pStream->WriteString(pWorldName);

	m_pClientMgr->m_LastTime = m_pClientMgr->m_CurTime;
	m_pStream->Write(&m_pClientMgr->m_CurTime, 4);
	m_pStream->Write(&m_pClientMgr->m_LastTime, 4);
	m_pClientMgr->m_FrameTime = 0.0f;
	m_pClientMgr->m_TimeSinceUpdate = 0.0f;

	m_State = DEMO_RECORDING;
	DemoSerialize(m_pStream, LTFALSE);
	SRand();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00432c70
LTRESULT CDemoMgr::PlayDemo(const char *pFilename, char *pWorldName, uint32 maxWorldNameLen)
{
	uint32 version, dummy;
	int i;

	StopDemo();

	m_pStream = streamsim_MemStreamFromFile((char*)pFilename);
	if(!m_pStream)
		return LT_ERROR;

	m_pStream->Read(&version, 4);
	if(version != DEMO_VERSION)
	{
		dsi_ConsolePrint("Invalid version (%d, wanted %d)", version, DEMO_VERSION);
		return LT_INVALIDVERSION;
	}

	for(i=0; i < 16; i++)
		m_pStream->Read(&dummy, 4);

	if(m_pStream->ReadString(pWorldName, maxWorldNameLen) != LT_OK)
	{
		m_pStream->Release();
		return LT_ERROR;
	}

	m_pStream->Read(&m_pClientMgr->m_CurTime, 4);
	m_pStream->Read(&m_pClientMgr->m_LastTime, 4);
	m_pClientMgr->m_FrameTime = 0.0f;
	m_pClientMgr->m_iCurInputSlot = 0;
	memset(m_pClientMgr->m_Commands[0], 0, sizeof(m_pClientMgr->m_Commands[0]));

	m_State = DEMO_PLAYING;
	m_StartTime = -1.0f;
	m_nFramesDrawn = 0;
	m_pClientMgr->m_TimeSinceUpdate = 0.0f;

	// Save the game state so StopDemo can restore it.
	m_pSaveStream = streamsim_OpenMemStream(256);
	if(!m_pSaveStream)
	{
		StopDemo();
		return LT_ERROR;
	}

	DemoSerialize(m_pSaveStream, LTFALSE);
	DemoSerialize(m_pStream, LTTRUE);
	SRand();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00432de0
void CDemoMgr::StopDemo()
{
	if(m_pStream)
	{
		if(m_State == DEMO_RECORDING)
		{
			m_pStream->WriteVal((uint8)DEMOSYNC_END);
		}
		else if(m_State == DEMO_PLAYING)
		{
			if(m_pSaveStream)
			{
				m_pSaveStream->SeekTo(0);
				DemoSerialize(m_pSaveStream, LTTRUE);
			}
		}

		m_pStream->Release();
		m_pStream = LTNULL;
	}

	if(m_pSaveStream)
	{
		m_pSaveStream->Release();
		m_pSaveStream = LTNULL;
	}

	m_State = DEMO_NONE;
}

// FUNCTION: LITHTECH 0x00432e50
LTRESULT CDemoMgr::ProcessInput(int32 *pChanges, int32 *pnChanges, int32 *pOn, int32 *pnOn,
	LTBOOL bClearInput)
{
	LTRESULT dResult;
	uint8 nChanges, change;
	uint8 *pOldCommands, *pNewCommands;
	int i;

	*pnChanges = 0;
	*pnOn = 0;

	if(m_State == DEMO_PLAYING)
	{
		dResult = ReadSyncByte(DEMOSYNC_INPUT);
		if(dResult != 2)
		{
			if(dResult != LT_OK)
				return dResult;

			for(i=0; i < 3; i++)
				m_pStream->Read(&m_pClientMgr->m_AxisOffsets[i], 4);

			m_pStream->Read(&nChanges, 1);
			if(nChanges >= 255)
			{
				dsi_ConsolePrint("DemoMgr: Invalid number of changes");
				return LT_ERROR;
			}

			for(i=0; i < nChanges; i++)
			{
				m_pStream->Read(&change, 1);
				if(change >= 255)
				{
					dsi_ConsolePrint("DemoMgr: Invalid change");
					return LT_ERROR;
				}

				pChanges[i] = change;
			}

			*pnChanges = nChanges;
			for(i=0; i < *pnChanges; i++)
			{
				m_pClientMgr->m_Commands[0][pChanges[i]] = !m_pClientMgr->m_Commands[0][pChanges[i]];
			}

			for(i=0; i < 255; i++)
			{
				if(m_pClientMgr->m_Commands[0][i])
				{
					pOn[*pnOn] = i;
					(*pnOn)++;
				}
			}

			return LT_OK;
		}
	}

	pOldCommands = m_pClientMgr->m_Commands[m_pClientMgr->m_iCurInputSlot];
	pNewCommands = m_pClientMgr->m_Commands[!m_pClientMgr->m_iCurInputSlot];
	memset(pNewCommands, 0, 255);

	if(!m_pClientMgr->m_bTrackingInputDevices)
	{
		m_pClientMgr->m_InputMgr->ReadInput(m_pClientMgr->m_InputMgr, pNewCommands,
			m_pClientMgr->m_AxisOffsets);
	}

	if(!m_pClientMgr->m_bInputState || bClearInput)
	{
		memset(pNewCommands, 0, 255);
		memset(m_pClientMgr->m_AxisOffsets, 0, sizeof(m_pClientMgr->m_AxisOffsets));
	}

	for(i=0; i < 255; i++)
	{
		if(pNewCommands[i])
		{
			pOn[*pnOn] = i;
			(*pnOn)++;
		}

		if(pNewCommands[i] != pOldCommands[i])
		{
			pChanges[*pnChanges] = i;
			(*pnChanges)++;
		}
	}

	m_pClientMgr->m_iCurInputSlot = !m_pClientMgr->m_iCurInputSlot;

	if(m_State == DEMO_RECORDING)
	{
		m_pStream->WriteVal((uint8)DEMOSYNC_INPUT);

		for(i=0; i < 3; i++)
			m_pStream->Write(&m_pClientMgr->m_AxisOffsets[i], 4);

		m_pStream->WriteVal((uint8)*pnChanges);
		for(i=0; i < *pnChanges; i++)
			m_pStream->WriteVal((uint8)pChanges[i]);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00433120
LTRESULT CDemoMgr::UpdateTime()
{
	LTRESULT dResult;
	uint32 i;

	if(m_State == DEMO_PLAYING)
	{
		// Start timing once the world is loaded.
		if(m_StartTime == -1.0f && m_pClientMgr->m_World.m_bLoaded)
		{
			m_StartTime = time_GetTime();
			for(i=0; i < NUM_PDCOUNTERS; i++)
				*g_PDCounters[i].m_pValue = 0;
		}

		dResult = ReadSyncByte(DEMOSYNC_TIME);
		if(dResult != LT_OK)
			return dResult;

		m_pStream->Read(&m_pClientMgr->m_CurTime, 4);
	}
	else
	{
		m_pClientMgr->m_CurTime = time_GetTime() + m_TimeOffset;
		if(m_State == DEMO_RECORDING)
		{
			m_pStream->WriteVal((uint8)DEMOSYNC_TIME);
			m_pStream->Write(&m_pClientMgr->m_CurTime, 4);
		}
	}

	m_pClientMgr->m_FrameTime = m_pClientMgr->m_CurTime - m_pClientMgr->m_LastTime;
	m_pClientMgr->m_LastTime = m_pClientMgr->m_CurTime;
	m_pClientMgr->UpdateFrameRate();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00433200
LTRESULT CDemoMgr::ReadSyncByte(int expected)
{
	uint8 syncByte;
	float timeTaken, fps;
	double variance;
	PDCounter *pCounter;
	int i;

	if(m_pStream && m_State == DEMO_PLAYING)
	{
		m_pStream->Read(&syncByte, 1);
		if(syncByte == DEMOSYNC_END)
		{
			timeTaken = time_GetTime() - m_StartTime;
			fps = (float)m_nFramesDrawn / timeTaken;
			DemoPrint("Demo played in %.3f seconds", timeTaken);
			DemoPrint("%d frames drawn, average %.3f fps", m_nFramesDrawn, fps);

			g_MinDemoTime = LTMIN(g_MinDemoTime, timeTaken);
			g_MaxDemoTime = LTMAX(g_MaxDemoTime, timeTaken);
			g_MinDemoFPS = LTMIN(g_MinDemoFPS, fps);
			g_MaxDemoFPS = LTMAX(g_MaxDemoFPS, fps);

			for(i=0; i < NUM_PDCOUNTERS; i++)
			{
				pCounter = &g_PDCounters[i];

				if(g_CV_PlayDemoProfile)
				{
					DemoPrint("%s: %.3f", pCounter->m_pName,
						(double)*pCounter->m_pValue / (double)cnt_NumTicksPerSecond());
				}

				pCounter->m_Min = LTMIN(pCounter->m_Min, *pCounter->m_pValue);
				pCounter->m_Max = LTMAX(pCounter->m_Max, *pCounter->m_pValue);
			}

			if(g_CV_PlayDemoReps > 1)
			{
				if(g_CurRunIteration == (uint32)g_CV_PlayDemoReps)
				{
					DemoPrint("PlayDemoReps info -------------");
					DemoPrint("Min time: %.3f, Max time: %.3f, diff: %.3f (%.3f%% error)",
						g_MinDemoTime, g_MaxDemoTime, g_MaxDemoTime - g_MinDemoTime,
						((g_MaxDemoTime * 100.0f) / g_MinDemoTime) - 100.0f);
					DemoPrint("Min FPS: %.3f, Max FPS: %.3f, diff: %.3f",
						g_MinDemoFPS, g_MaxDemoFPS, g_MaxDemoFPS - g_MinDemoFPS);

					if(g_CV_PlayDemoProfile)
					{
						DemoPrint("value: (min time / max time) variance");
						for(i=0; i < NUM_PDCOUNTERS; i++)
						{
							pCounter = &g_PDCounters[i];

							variance = (double)(pCounter->m_Max - pCounter->m_Min) / (double)cnt_NumTicksPerSecond();
							DemoPrint("%s: (%.3f / %.3f), variance: %.3f (%.1f%% total)", pCounter->m_pName,
								(double)pCounter->m_Min / (double)cnt_NumTicksPerSecond(),
								(double)pCounter->m_Max / (double)cnt_NumTicksPerSecond(),
								variance,
								(variance / (g_MaxDemoTime - g_MinDemoTime)) * 100.0);
						}
					}
				}
				else
				{
					dsi_OnClientShutdown(LTNULL);
				}
			}

			dsi_SetConsoleUp(LTTRUE);
			StopDemo();
			m_TimeOffset = m_pClientMgr->m_CurTime - time_GetTime();
			return 2;
		}
		else if(syncByte != expected)
		{
			dsi_ConsolePrint("DemoMgr: Sync byte wrong (wanted %d, got %d)", expected, syncByte);
			StopDemo();
			return 2;
		}
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00433590
static void DemoPrint(const char *pMsg, ...)
{
	char str[1024];
	va_list marker;

	va_start(marker, pMsg);
	_vsnprintf(str, 1023, pMsg, marker);
	va_end(marker);

	dsi_ConsolePrint("%s", str);
	DebugOut("%s\n", str);
}
