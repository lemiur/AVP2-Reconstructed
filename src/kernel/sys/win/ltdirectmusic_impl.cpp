// Jupiter runtime/kernel/src/sys/win/ltdirectmusic_impl.cpp
// FLAGS: /O2 /IE:/AVP2Source/jupiter/dx9inc /IE:/AVP2Source/build/proj/LT2/lithshared/lith /IE:/AVP2Source/build/proj/LT2/lithshared/controlfilemgr
// Talon (DirectX 7) version: one performance with a port instead of audiopaths, reverb through the
// port's IKsControl, a notification thread started with _beginthread that reaches the manager
// through a static pointer, and no pause state. CStyle::SetStyleName and CStyleList::Find were
// identical-code folded with CSegment::SetSegmentName and CSegmentList::Find.
#include <windows.h>
#include <process.h>
#include <string.h>
#include "bdefs.h"
#include "console.h"
#include "ltdirectmusic_impl.h"
#include "ltdirectmusiccontrolfile.h"
#include "dmksctrl.h"
#include "engine_vars.h"




// the manager the notification thread works on
// GLOBAL: LITHTECH 0x004e446c
static CLTDirectMusicMgr* g_pLTDMMgr = LTNULL;


// output error to console
// FUNCTION: LITHTECH 0x00447050
void LTDMConOutError(char *pMsg, ...)
{
	if (g_CV_LTDMConsoleOutput >= 1)
	{
		char msg[500] = "";
		va_list marker;

		va_start(marker, pMsg);
		_vsnprintf(msg, sizeof(msg)-1, pMsg, marker);
		msg[sizeof(msg)-1] = '\0';
		va_end(marker);

		if (msg[strlen(msg)-1] == '\n') msg[strlen(msg)-1] = '\0';
		con_PrintString(CONRGB(128, 0, 255), 0, msg);
	}
}

// output warning to console
// FUNCTION: LITHTECH 0x004470f0
void LTDMConOutWarning(char *pMsg, ...)
{
	if (g_CV_LTDMConsoleOutput >= 2)
	{
		char msg[500] = "";
		va_list marker;

		va_start(marker, pMsg);
		_vsnprintf(msg, sizeof(msg)-1, pMsg, marker);
		msg[sizeof(msg)-1] = '\0';
		va_end(marker);

		if (msg[strlen(msg)-1] == '\n') msg[strlen(msg)-1] = '\0';
		con_PrintString(CONRGB(128, 255, 0), 0, msg);
	}
}

// output message to console
// FUNCTION: LITHTECH 0x00447190
void LTDMConOutMsg(int nLevel, char *pMsg, ...)
{
	if (g_CV_LTDMConsoleOutput >= nLevel)
	{
		char msg[500] = "";
		va_list marker;

		va_start(marker, pMsg);
		_vsnprintf(msg, sizeof(msg)-1, pMsg, marker);
		msg[sizeof(msg)-1] = '\0';
		va_end(marker);

		if (msg[strlen(msg)-1] == '\n') msg[strlen(msg)-1] = '\0';
		con_PrintString(CONRGB(128, 255, 128), 0, msg);
	}
}

// if the mgr is not initialized we fail
// FUNCTION: LITHTECH 0x00447230
void LTDMConOutMgrNotInitialized(const char* sFuncName)
{
	LTDMConOutError("ERROR! LTDirectMusic not Initialized (%s)\n", sFuncName);
}

// we will fail if level has not been initialized (folded with LTDMConOutMgrNotInitialized)
void LTDMConOutLevelNotInitialized(const char* sFuncName)
{
	LTDMConOutError("ERROR! LTDirectMusic not Initialized (%s)\n", sFuncName);
}


///////////////////////////////////////////////////////////////////////////////////////////
// macro for converting from normal to wide strings
///////////////////////////////////////////////////////////////////////////////////////////
#define MULTI_TO_WIDE( x,y )	MultiByteToWideChar( CP_ACP, MB_PRECOMPOSED, y, -1, x, _MAX_PATH );


///////////////////////////////////////////////////////////////////////////////////////////
// define the static chunk allocators
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00447250 _$E11
// FUNCTION: LITHTECH 0x00447260 _$E7
// FUNCTION: LITHTECH 0x00447280 _$E10
// FUNCTION: LITHTECH 0x00447290 _$E8
// GLOBAL: LITHTECH 0x004e4454 ?m_ChunkAllocator@CCommandItem@CLTDirectMusicMgr@@2V?$CLithChunkAllocator@VCCommandItem@CLTDirectMusicMgr@@@@A
CLithChunkAllocator<CLTDirectMusicMgr::CCommandItem> CLTDirectMusicMgr::CCommandItem::m_ChunkAllocator;
// FUNCTION: LITHTECH 0x00447320 _$E16
// FUNCTION: LITHTECH 0x00447330 _$E13
// FUNCTION: LITHTECH 0x00447350 _$E15
// FUNCTION: LITHTECH 0x00447360 _$E14
// GLOBAL: LITHTECH 0x004e43f0 ?m_ChunkAllocator@CSegment@CLTDirectMusicMgr@@2V?$CLithChunkAllocator@VCSegment@CLTDirectMusicMgr@@@@A
CLithChunkAllocator<CLTDirectMusicMgr::CSegment> CLTDirectMusicMgr::CSegment::m_ChunkAllocator;
// FUNCTION: LITHTECH 0x00447430 _$E21
// FUNCTION: LITHTECH 0x00447440 _$E18
// FUNCTION: LITHTECH 0x00447460 _$E20
// FUNCTION: LITHTECH 0x00447470 _$E19
// GLOBAL: LITHTECH 0x004e442c ?m_ChunkAllocator@CSegmentState@CLTDirectMusicMgr@@2V?$CLithChunkAllocator@VCSegmentState@CLTDirectMusicMgr@@@@A
CLithChunkAllocator<CLTDirectMusicMgr::CSegmentState> CLTDirectMusicMgr::CSegmentState::m_ChunkAllocator;
// FUNCTION: LITHTECH 0x00447510 _$E26
// FUNCTION: LITHTECH 0x00447520 _$E23
// FUNCTION: LITHTECH 0x00447540 _$E25
// FUNCTION: LITHTECH 0x00447550 _$E24
// GLOBAL: LITHTECH 0x004e4440 ?m_ChunkAllocator@CBand@CLTDirectMusicMgr@@2V?$CLithChunkAllocator@VCBand@CLTDirectMusicMgr@@@@A
CLithChunkAllocator<CLTDirectMusicMgr::CBand> CLTDirectMusicMgr::CBand::m_ChunkAllocator;
// FUNCTION: LITHTECH 0x004475e0 _$E31
// FUNCTION: LITHTECH 0x004475f0 _$E28
// FUNCTION: LITHTECH 0x00447610 _$E30
// FUNCTION: LITHTECH 0x00447620 _$E29
// GLOBAL: LITHTECH 0x004e4404 ?m_ChunkAllocator@CStyle@CLTDirectMusicMgr@@2V?$CLithChunkAllocator@VCStyle@CLTDirectMusicMgr@@@@A
CLithChunkAllocator<CLTDirectMusicMgr::CStyle> CLTDirectMusicMgr::CStyle::m_ChunkAllocator;
// FUNCTION: LITHTECH 0x004476f0 _$E36
// FUNCTION: LITHTECH 0x00447700 _$E33
// FUNCTION: LITHTECH 0x00447720 _$E35
// FUNCTION: LITHTECH 0x00447730 _$E34
// GLOBAL: LITHTECH 0x004e4418 ?m_ChunkAllocator@CDLSBank@CLTDirectMusicMgr@@2V?$CLithChunkAllocator@VCDLSBank@CLTDirectMusicMgr@@@@A
CLithChunkAllocator<CLTDirectMusicMgr::CDLSBank> CLTDirectMusicMgr::CDLSBank::m_ChunkAllocator;

// The out-of-line inline destructors: ~CSegment (after the CSegment group) was folded into this one.
// FUNCTION: LITHTECH 0x004476e0 ??1CStyle@CLTDirectMusicMgr@@QAE@XZ


///////////////////////////////////////////////////////////////////////////////////////////
// thread that handles the DirectMusic notifications
///////////////////////////////////////////////////////////////////////////////////////////
// Extern: as a static, VC6 emits it after Init, which takes its address.
// FUNCTION: LITHTECH 0x004477c0
void LTDMNotificationThread(void* pData)
{
	DMUS_NOTIFICATION_PMSG* pPmsg;
	IDirectMusicSegmentState* pDMSegState = LTNULL;
	IDirectMusicSegment* pDMSegment = LTNULL;

	EnterCriticalSection(&g_pLTDMMgr->m_CommandQueueCriticalSection);

	// loop until our exit flag is set
	while (!g_pLTDMMgr->m_bExitNotificationThread)
	{
		// wait for a notification
		LeaveCriticalSection(&g_pLTDMMgr->m_CommandQueueCriticalSection);
		WaitForSingleObject(g_pLTDMMgr->m_hNotify, 100);
		EnterCriticalSection(&g_pLTDMMgr->m_CommandQueueCriticalSection);

		// loop through all directmusic notification messages
		while (g_pLTDMMgr->m_pPerformance->GetNotificationPMsg(&pPmsg) == S_OK)
		{
			// if this is a segment notification
			if ((!g_pLTDMMgr->m_bExitNotificationThread) && (pPmsg->guidNotificationType == GUID_NOTIFICATION_SEGMENT))
			{
				// make sure the punkUser field is not null
				if (pPmsg->punkUser != LTNULL)
				{
					// set up the segment state
					pDMSegState = (IDirectMusicSegmentState*)pPmsg->punkUser;

					// set up the segment
					if (FAILED(pDMSegState->GetSegment(&pDMSegment))) pDMSegment = LTNULL;
				}

				// if this is a notification of a segment about to finish
				if (pPmsg->dwNotificationOption == DMUS_NOTIFICATION_SEGALMOSTEND)
				{
					// if this came from a secondary segment we should not process this message
					if (g_pLTDMMgr->m_lstSecondarySegmentsPlaying.Find(pDMSegState) != LTNULL) continue;

					// if this came from a motif we should not process this message
					if (g_pLTDMMgr->m_lstMotifsPlaying.Find(pDMSegState) != LTNULL) continue;

					// make sure we are on the last played primairy segment state
					{
						CLTDirectMusicMgr::CSegmentState* pFirstSegState = g_pLTDMMgr->m_lstPrimairySegmentsPlaying.GetFirst();
						if (pFirstSegState != LTNULL)
						{
							if (pFirstSegState->GetDMSegmentState() != pDMSegState) continue;
						}
					}

					// holds the current command we are working on
					CLTDirectMusicMgr::CCommandItem* pCurCommand;

					// check if there was a previous command
					if (g_pLTDMMgr->m_pLastCommand != LTNULL) pCurCommand = g_pLTDMMgr->m_pLastCommand->Next();
					else pCurCommand = g_pLTDMMgr->m_lstCommands.GetFirst();

					// loop to execute commands
					while (pCurCommand != LTNULL)
					{
						LTDMCommandTypes nCommandType = pCurCommand->GetCommandType();

						// these commands have not been implemented so just skip them
						if ((nCommandType == LTDMCommandNull) || (nCommandType == LTDMCommandStopPlaying) ||
							(nCommandType == LTDMCommandPauseQueue) || (nCommandType == LTDMCommandAdjustVolume) ||
							(nCommandType == LTDMCommandPlaySecondarySegment) || (nCommandType == LTDMCommandPlayMotif) ||
							(nCommandType == LTDMCommandClearOldCommands))
						{
							// go to the next command
							pCurCommand = pCurCommand->Next();
						}

						else if (nCommandType == LTDMCommandLoopToStart)
						{
							CLTDirectMusicMgr::CCommandItemLoopToStart* pCmdLoop = (CLTDirectMusicMgr::CCommandItemLoopToStart*)pCurCommand;

							// check if we were finished looping previously
							if (pCmdLoop->GetNumLoops() == 0)
							{
								pCurCommand = pCurCommand->Next();
							}
							else
							{
								// check if we need to decrement the loop counter
								if (pCmdLoop->GetNumLoops() > 0)
								{
									pCmdLoop->SetNumLoops(pCmdLoop->GetNumLoops()-1);
								}

								// check if we are finished looping
								if (pCmdLoop->GetNumLoops() == 0)
								{
									pCurCommand = pCurCommand->Next();
								}

								// if this was not the last loop go to start of command queue
								else
								{
									pCurCommand = g_pLTDMMgr->m_lstCommands.GetFirst();
								}
							}
						}

						else if (nCommandType == LTDMCommandChangeIntensity)
						{
							CLTDirectMusicMgr::CCommandChangeIntensity* pCmdChangeIntensity = (CLTDirectMusicMgr::CCommandChangeIntensity*)pCurCommand;

							// change the intensity (this clears the command queue)
							g_pLTDMMgr->ChangeIntensity(pCmdChangeIntensity->GetNewIntensity());

							// start at the beginning of the new command queue
							pCurCommand = g_pLTDMMgr->m_lstCommands.GetFirst();
						}

						else if (nCommandType == LTDMCommandPlaySegment)
						{
							CLTDirectMusicMgr::CCommandItemPlaySegment* pCmdSeg = (CLTDirectMusicMgr::CCommandItemPlaySegment*)pCurCommand;

							// new directmusic state to get when playing the segment
							IDirectMusicSegmentState* pDMSegmentState = LTNULL;

							// play this new segment queued
							g_pLTDMMgr->m_pPerformance->PlaySegment(pCmdSeg->GetSegment()->GetDMSegment(), DMUS_SEGF_MEASURE, 0, &pDMSegmentState);

							// create new segment state in the primairy segment state list
							g_pLTDMMgr->m_lstPrimairySegmentsPlaying.CreateSegmentState(pDMSegmentState, pCmdSeg->GetSegment());

							// we are done processing commands for now
							break;
						}

						else if (nCommandType == LTDMCommandPlayTransition)
						{
							CLTDirectMusicMgr::CCommandItemPlayTransition* pCmdSeg = (CLTDirectMusicMgr::CCommandItemPlayTransition*)pCurCommand;

							// new directmusic state to get when playing the segment
							IDirectMusicSegmentState* pDMSegmentState = LTNULL;

							// play this new segment queued
							g_pLTDMMgr->m_pPerformance->PlaySegment(pCmdSeg->GetTransition()->GetDMSegment(), DMUS_SEGF_MEASURE, 0, &pDMSegmentState);

							// create new segment state in the primairy segment state list
							g_pLTDMMgr->m_lstPrimairySegmentsPlaying.CreateSegmentState(pDMSegmentState, LTNULL);

							// delete this command from the command queue
							g_pLTDMMgr->m_lstCommands.Delete(pCurCommand);

							// set the last command to start of the command queue
							pCurCommand = LTNULL;

							// we are done processing commands for now
							break;
						}

						else if (nCommandType == LTDMCommandStopSegment)
						{
							CLTDirectMusicMgr::CCommandItemStopSegment* pCmdSeg = (CLTDirectMusicMgr::CCommandItemStopSegment*)pCurCommand;

							// stop this segment
							g_pLTDMMgr->m_pPerformance->Stop(pCmdSeg->GetSegment()->GetDMSegment(), LTNULL, 0, DMUS_SEGF_MEASURE);

							// go to the next command
							pCurCommand = pCurCommand->Next();
						}
					}

					// update the last command pointer
					g_pLTDMMgr->m_pLastCommand = pCurCommand;

					// go through secondary command queue
					pCurCommand = g_pLTDMMgr->m_lstCommands2.GetFirst();
					while (pCurCommand != LTNULL)
					{
						if (pCurCommand->GetCommandType() == LTDMCommandPlaySecondarySegment)
						{
							CLTDirectMusicMgr::CCommandItemPlaySecondarySegment* pCmdSeg = (CLTDirectMusicMgr::CCommandItemPlaySecondarySegment*)pCurCommand;
							IDirectMusicSegmentState* pDMSegmentState = LTNULL;
							g_pLTDMMgr->m_pPerformance->PlaySegment(pCmdSeg->GetSegment()->GetDMSegment(), DMUS_SEGF_SECONDARY | DMUS_SEGF_MEASURE | DMUS_SEGF_CONTROL, 0, &pDMSegmentState);
							g_pLTDMMgr->m_lstSecondarySegmentsPlaying.CreateSegmentState(pDMSegmentState, pCmdSeg->GetSegment());
						}

						if (pCurCommand->GetCommandType() == LTDMCommandPlayMotif)
						{
							CLTDirectMusicMgr::CCommandItemPlayMotif* pCmdSeg = (CLTDirectMusicMgr::CCommandItemPlayMotif*)pCurCommand;
							IDirectMusicSegmentState* pDMSegmentState = LTNULL;
							g_pLTDMMgr->m_pPerformance->PlaySegment(pCmdSeg->GetSegment()->GetDMSegment(), DMUS_SEGF_SECONDARY | DMUS_SEGF_MEASURE, 0, &pDMSegmentState);
							g_pLTDMMgr->m_lstMotifsPlaying.CreateSegmentState(pDMSegmentState, pCmdSeg->GetSegment());
						}

						// go to the next command
						CLTDirectMusicMgr::CCommandItem* pNextCommand = pCurCommand->Next();

						// delete this command from the command queue
						g_pLTDMMgr->m_lstCommands2.Delete(pCurCommand);

						// set the next command
						pCurCommand = pNextCommand;
					}
				}

				// if this is a segment finished notification
				if ((pPmsg->dwNotificationOption == DMUS_NOTIFICATION_SEGEND) && (pDMSegState != LTNULL))
				{
					CLTDirectMusicMgr::CSegmentState* pFindSegState;

					// search for a primairy segment that matches this segment state pointer
					if ((pFindSegState = g_pLTDMMgr->m_lstPrimairySegmentsPlaying.Find(pDMSegState)) != LTNULL)
					{
						g_pLTDMMgr->m_lstPrimairySegmentsPlaying.CleanupSegmentState(g_pLTDMMgr, pFindSegState, LTDMEnactInvalid, FALSE);
					}

					// search for a secondary segment that matches this segment state pointer
					else if ((pFindSegState = g_pLTDMMgr->m_lstSecondarySegmentsPlaying.Find(pDMSegState)) != LTNULL)
					{
						g_pLTDMMgr->m_lstSecondarySegmentsPlaying.CleanupSegmentState(g_pLTDMMgr, pFindSegState, LTDMEnactImmediately, FALSE);
					}

					// search for a motif segment that matches this segment state pointer
					else if ((pFindSegState = g_pLTDMMgr->m_lstMotifsPlaying.Find(pDMSegState)) != LTNULL)
					{
						g_pLTDMMgr->m_lstMotifsPlaying.CleanupSegmentState(g_pLTDMMgr, pFindSegState, LTDMEnactImmediately, FALSE);
					}
				}
			}

			// free the notification message we are finished with it
			g_pLTDMMgr->m_pPerformance->FreePMsg((DMUS_PMSG*)pPmsg);
		}
	}

	LeaveCriticalSection(&g_pLTDMMgr->m_CommandQueueCriticalSection);

	// stop directmusic notifications
	g_pLTDMMgr->m_pPerformance->SetNotificationHandle(0, 0);
	CloseHandle(g_pLTDMMgr->m_hNotify);

	_endthread();
}


///////////////////////////////////////////////////////////////////////////////////////////
// default constructor
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00447be0
CLTDirectMusicMgr::CLTDirectMusicMgr()
{
	// mgr is initially not initialized
	m_bInitialized = FALSE;
	m_bLevelInitialized = FALSE;

	// set pointers to initial LTNULL value
	m_pDirectMusic = LTNULL;
	m_pLoader = LTNULL;
	m_pPerformance = LTNULL;
	m_pPort = LTNULL;
	m_aryTransitions = LTNULL;
	m_aryIntensities = LTNULL;
	m_sWorkingDirectoryAny = LTNULL;
	m_sWorkingDirectoryControlFile = LTNULL;
	m_nNumPChannels = 64;
	m_nNumVoices = 64;
	m_nSynthSampleRate = 44100;
	m_pLastCommand = LTNULL;

	// set the global mgr pointer for the notification thread
	g_pLTDMMgr = this;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Initialize the Mgr getting initial parameters from the described control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00447c90
LTRESULT CLTDirectMusicMgr::Init()
{
	LTDMConOutMsg(3, "CLTDirectMusicMgr::Init\n");

	// init will fail if the Mgr is already initialized
	if (m_bInitialized)
	{
		LTDMConOutError("ERROR! CLTDirectMusicMgr alread initialized. (CLTDirectMusicMgr::Init)\n");
		return LT_ERROR;
	}

	// initialize com, if not we fail
	if (FAILED(CoInitialize(LTNULL)))
	{
		LTDMConOutError("ERROR! Unable to initialize com. (CLTDirectMusicMgr::Init)\n");
		return LT_ERROR;
	}

	if (!InitDirectMusic())
	{
		LTDMConOutError("ERROR! Unable to initialize DirectMusic. (CLTDirectMusicMgr::Init)\n");

	    // Release COM.
	    CoUninitialize();

		return LT_ERROR;
	}

	// critical section for the command queue
	InitializeCriticalSection(&m_CommandQueueCriticalSection);

	// set up the notification GUID
	m_guid = GUID_NOTIFICATION_SEGMENT;

	// add the notification to directmusic
	m_pPerformance->AddNotificationType(m_guid);

	// create the notification event and give it to directmusic
	m_hNotify = CreateEvent(LTNULL, FALSE, FALSE, LTNULL);
	m_pPerformance->SetNotificationHandle(m_hNotify, 0);

	// this should be false until we are read to exit the thread
	m_bExitNotificationThread = FALSE;

	// set up chunk allocators
	CCommandItem::m_ChunkAllocator.Init(100,1);
	CSegment::m_ChunkAllocator.Init(20,1);
	CSegmentState::m_ChunkAllocator.Init(10,1);
	CBand::m_ChunkAllocator.Init(5,0);
	CStyle::m_ChunkAllocator.Init(5,0);
	CDLSBank::m_ChunkAllocator.Init(5,0);

	// Begin the notification thread
	m_hThread = _beginthread(LTDMNotificationThread, 0, LTNULL);

	// set Mgr to initialized we have succeeded
	m_bInitialized = TRUE;

	// return LT_OK we have succeeded
	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Terminate the mgr
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00447e30
LTRESULT CLTDirectMusicMgr::Term()
{
	LTDMConOutMsg(3, "CLTDirectMusicMgr::Term\n");

	// if we are not initialized then just exit
	if (!m_bInitialized)
	{
		LTDMConOutWarning("WARNING! LTDirectMusicMgr already terminated or never initialized. (CLTDirectMusicMgr::Term)\n");
		return LT_OK;
	}

	// check if the level has been terminated, if not we must terminate it
	if (m_bLevelInitialized)
	{
		TermLevel();
	}

	// signal the notification thread that it is time to exit
	EnterCriticalSection(&m_CommandQueueCriticalSection);
	m_bExitNotificationThread = TRUE;
	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	// wait for the thread to exit (wait up to 1 second)
	WaitForSingleObject((HANDLE)m_hThread, 1000);

	// Terminate directmusic
	TermDirectMusic();

	// Release COM.
	CoUninitialize();

	// terminate chunk allocators
	CCommandItem::m_ChunkAllocator.Term();
	CSegment::m_ChunkAllocator.Term();
	CBand::m_ChunkAllocator.Term();
	CStyle::m_ChunkAllocator.Term();
	CDLSBank::m_ChunkAllocator.Term();

	DeleteCriticalSection(&m_CommandQueueCriticalSection);

	// set initialized to false we are finished
	m_bInitialized = FALSE;

	// return LT_OK we have succeeded
	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Initialize a game level using the parameters in the given control file
///////////////////////////////////////////////////////////////////////////////////////////
// Jupiter's nLastPos local indexed three times (not one char local) gives the original's `add ecx,-2; [ecx+esi]`.
// FUNCTION: LITHTECH 0x00448040
LTRESULT CLTDirectMusicMgr::InitLevel(const char* sWorkingDirectory, const char* sControlFileName, const char* sDefine1,
		  						     const char* sDefine2, const char* sDefine3)
{
	LTDMConOutMsg(3, "CLTDirectMusicMgr::InitLevel sWorkingDirectory=%s sControlFileName=%s sDefine1=%s sDefine2=%s sDeine3=%s\n",
					  sWorkingDirectory, sControlFileName, sDefine1, sDefine2, sDefine3);

	// control file mgr object used for reading the control file
	CControlFileMgrDStream controlFile;

	// if the mgr is not initialized we fail
	if (!m_bInitialized)
	{
		LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::InitLevel");
		return LT_ERROR;
	}

	// we will fail if InitLevel has already been called and has not been terminated
	if (m_bLevelInitialized) { LTDMConOutError("ERROR! InitLevel has already been called and not terminated. (CLTDirectMusicMgr::InitLevel)\n"); return LT_ERROR; }

	// make sure control file name is valid
	if (sControlFileName == LTNULL) { LTDMConOutError("ERROR! Control file name not valid. (CLTDirectMusicMgr::InitLevel)\n"); return LT_ERROR; }

	// make sure working directory is valid
	if (sWorkingDirectory == LTNULL) { LTDMConOutError("ERROR! Working directory not valid. (CLTDirectMusicMgr::InitLevel)\n"); return LT_ERROR; }

	// set the working directory
	SetWorkingDirectory(sWorkingDirectory);

	// add the defines to the control file
	if (sDefine1 != LTNULL) controlFile.AddDefine(sDefine1);
	if (sDefine2 != LTNULL) controlFile.AddDefine(sDefine2);
	if (sDefine3 != LTNULL) controlFile.AddDefine(sDefine3);

	// if the control file name has a path in it use it as is
	if ((strchr(sControlFileName, '\\') != LTNULL) || (strchr(sControlFileName, '/') != LTNULL) || (strchr(sControlFileName, ':') != LTNULL))
	{
		// read in the control file
		if (!controlFile.Init(sControlFileName))
		{
			LTDMConOutError("ERROR! Unable to read control file. (CLTDirectMusicMgr::InitLevel)\n");
			return LT_ERROR;
		}
	}
	// otherwise add the working directory to it
	else
	{
		char* sFileName = new char[strlen(sWorkingDirectory)+strlen(sControlFileName)+2];
		if (sFileName != LTNULL)
		{
			strcpy(sFileName, sWorkingDirectory);
			if (strlen(sWorkingDirectory) > 0)
			{
				// see if we need to append a backslash to directory
				int nLastPos = strlen(sWorkingDirectory)-1;
				if ((sWorkingDirectory[nLastPos] != '\\') && (sWorkingDirectory[nLastPos] != '/') && (sWorkingDirectory[nLastPos] != ':')) strcat(sFileName, "\\");
			}
			strcat(sFileName, sControlFileName);

			// read in the control file
			if (controlFile.Init(sFileName))
			{
				delete [] sFileName;
			}
			else
			{
				LTDMConOutError("ERROR! Unable to read control file. (CLTDirectMusicMgr::InitLevel)\n");
				return LT_ERROR;
			}
		}
		else return LT_ERROR;
	}

	// set defaults for values in control file
	m_nNumIntensities = 0;
	m_nInitialIntensity = 0;
	m_nInitialVolume = 0;
	m_nVolumeOffset = 0;
	m_nNumPChannels = 64;
	m_nNumVoices = 64;
	m_nSynthSampleRate = 44100;

	// read in values from control file
	controlFile.GetKeyVal(LTNULL, "NUMINTENSITIES", m_nNumIntensities);
	controlFile.GetKeyVal(LTNULL, "INITIALINTENSITY", m_nInitialIntensity);
	controlFile.GetKeyVal(LTNULL, "INITIALVOLUME", m_nInitialVolume);
	controlFile.GetKeyVal(LTNULL, "VOLUMEOFFSET", m_nVolumeOffset);
	controlFile.GetKeyVal(LTNULL, "PCHANNELS", m_nNumPChannels);
	controlFile.GetKeyVal(LTNULL, "VOICES", m_nNumVoices);
	controlFile.GetKeyVal(LTNULL, "SYNTHSAMPLERATE", m_nSynthSampleRate);

	// re-initialize the performance if the synthesizer settings changed
	if (((int)m_portParams.dwVoices != m_nNumVoices) || ((int)m_portParams.dwAudioChannels != m_nNumPChannels) ||
		((int)m_portParams.dwSampleRate != m_nSynthSampleRate))
	{
		InitPerformance();
	}

	// set the DLS bank search directory
	{
		CControlFileKey* pKey = controlFile.GetKey(LTNULL, "DLSBANKDIRECTORY");
		if (pKey != LTNULL)
		{
			CControlFileWord* pWord = pKey->GetFirstWord();
			if (pWord != LTNULL)
			{
				m_pLoader->SetSearchDirectory(CLSID_DirectMusicCollection, pWord->GetVal(), FALSE);
			}
		}
	}

	// set up reverb
	InitReverb(controlFile);

	// make sure the number of intensities is valid
	if (m_nNumIntensities < 1)
	{
		LTDMConOutError("ERROR! Number of intensities is not valid. NumIntensities = %i (CLTDirectMusicMgr::InitLevel)\n", m_nNumIntensities);
		controlFile.Term();
		return LT_ERROR;
	}

	// allocate the intensity array
	m_aryIntensities = new CIntensity[m_nNumIntensities+1];
	for (int i = 0; i <= m_nNumIntensities; i++)
	{
		m_aryIntensities[i].SetNumLoops(0);
		m_aryIntensities[i].SetIntensityToSetAtFinish(0);
	}

	// allocate the transition array
	m_nNumTransitions = (m_nNumIntensities+1)*(m_nNumIntensities+1);
	m_aryTransitions = new CTransition[m_nNumTransitions];
	for (int j = 0; j < m_nNumTransitions; j++)
	{
		m_aryTransitions[j].SetEnactTime(LTDMEnactNextMeasure);
		m_aryTransitions[j].SetManual(TRUE);
		m_aryTransitions[j].SetDMSegment(LTNULL);
	}

	// make sure the allocations succeeded
	if ((m_aryIntensities != LTNULL) && (m_aryTransitions != LTNULL))
	{
		// read in all of the music data
		ReadDLSBanks(controlFile);
		ReadStylesAndBands(controlFile);
		ReadIntensities(controlFile);
		ReadSecondarySegments(controlFile);
		ReadMotifs(controlFile);
		ReadTransitions(controlFile);

		// level is now initialized
		m_bLevelInitialized = TRUE;

		// we are done with the control file
		controlFile.Term();

		// set the initial volume
		SetVolume(m_nInitialVolume);

		// we have not played any commands yet
		m_pLastCommand = LTNULL;
		m_nCurIntensity = 0;

		return LT_OK;
	}

	LTDMConOutError("ERROR! Unable to allocate memory for Intensity and Transition arrays. (CLTDirectMusicMgr::InitLevel)\n", m_nNumIntensities);
	controlFile.Term();

	// clean up the arrays
	if (m_aryIntensities != LTNULL)
	{
		delete [] m_aryIntensities;
		m_aryIntensities = LTNULL;
	}
	if (m_aryTransitions != LTNULL)
	{
		delete [] m_aryTransitions;
		m_aryTransitions = LTNULL;
	}

	return LT_ERROR;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Terminate the current game level
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448590
LTRESULT CLTDirectMusicMgr::TermLevel()
{
	LTDMConOutMsg(3, "CLTDirectMusicMgr::TermLevel\n");

	// if the mgr is not initialized we fail
	if (!m_bInitialized)
	{
		LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::TermLevel");
		return LT_ERROR;
	}

	// if the level is not initialized just exit
	if (!m_bLevelInitialized)
	{
		LTDMConOutWarning("WARNING! Level already terminated or never initialized. (CLTDirectMusicMgr::TermLevel)\n");
		return LT_OK;
	}

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	// clear the command queue
	ClearCommands();

	// stop the music
	Stop();

	// turn off reverb
	TermReverb();

	// clean up all the segments and segment states
	m_lstSegments.CleanupSegments(this, FALSE);
	m_lstMotifs.CleanupSegments(this, FALSE);
	m_lstPrimairySegmentsPlaying.CleanupSegmentStates(this, FALSE);
	m_lstSecondarySegmentsPlaying.CleanupSegmentStates(this, FALSE);
	m_lstMotifsPlaying.CleanupSegmentStates(this, FALSE);

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	// clean up all the bands
	{
		CBand* pBand;
		while ((pBand = m_lstBands.GetFirst()) != LTNULL)
		{
			pBand->GetDMSegment()->Release();
			pBand->GetBand()->Unload(m_pPerformance);
			m_lstBands.Delete(pBand);
			delete pBand;
		}
	}

	// clean up all the styles
	{
		CStyle* pStyle;
		while ((pStyle = m_lstStyles.GetFirst()) != LTNULL)
		{
			pStyle->GetDMStyle()->Release();
			m_lstStyles.Delete(pStyle);
			delete pStyle;
		}
	}

	// clean up all the DLS banks
	{
		CDLSBank* pDLSBank;
		while ((pDLSBank = m_lstDLSBanks.GetFirst()) != LTNULL)
		{
			pDLSBank->GetDLSBank()->Release();
			m_lstDLSBanks.Delete(pDLSBank);
			delete pDLSBank;
		}
	}

	// clean up the working directories
	if (m_sWorkingDirectoryAny != LTNULL)
	{
		delete [] m_sWorkingDirectoryAny;
		m_sWorkingDirectoryAny = LTNULL;
	}
	if (m_sWorkingDirectoryControlFile != LTNULL)
	{
		delete [] m_sWorkingDirectoryControlFile;
		m_sWorkingDirectoryControlFile = LTNULL;
	}

	// clean up the arrays
	if (m_aryIntensities != LTNULL)
	{
		delete [] m_aryIntensities;
		m_aryIntensities = LTNULL;
	}
	if (m_aryTransitions != LTNULL)
	{
		delete [] m_aryTransitions;
		m_aryTransitions = LTNULL;
	}

	// level is no longer initialized
	m_bLevelInitialized = FALSE;

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Begin playing music
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448780
LTRESULT CLTDirectMusicMgr::Play()
{
	LTDMConOutMsg(4, "CLTDirectMusicMgr::Play\n");

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::Play"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::Play"); return LT_ERROR; }

	// start playing at the initial intensity
	return ChangeIntensity(m_nInitialIntensity, LTDMEnactImmediately);
};


///////////////////////////////////////////////////////////////////////////////////////////
// Stop playing music
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x004487e0
LTRESULT CLTDirectMusicMgr::Stop(const LTDMEnactTypes nStart)
{
	LTDMConOutMsg(4, "CLTDirectMusicMgr::Stop nStart=%i\n", nStart);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::Stop"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::Stop"); return LT_ERROR; }

	// stop the performance
	if ((m_pPerformance != LTNULL) && (nStart != LTDMEnactNextSegment))
	{
		m_pPerformance->Stop(LTNULL, LTNULL, 0, EnactTypeToFlags(nStart));
	}

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	// clear the command queues
	{
		CCommandItem* pCommand;
		while ((pCommand = m_lstCommands.GetFirst()) != LTNULL)
		{
			m_lstCommands.Delete(pCommand);
			delete pCommand;
		}
		while ((pCommand = m_lstCommands2.GetFirst()) != LTNULL)
		{
			m_lstCommands2.Delete(pCommand);
			delete pCommand;
		}
	}

	// clean up all the segment states
	m_lstPrimairySegmentsPlaying.CleanupSegmentStates(this, FALSE);
	m_lstSecondarySegmentsPlaying.CleanupSegmentStates(this, FALSE);
	m_lstMotifsPlaying.CleanupSegmentStates(this, FALSE);

	// no last command
	m_pLastCommand = LTNULL;

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Pause music playing
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448900
LTRESULT CLTDirectMusicMgr::Pause(const LTDMEnactTypes nStart)
{
	LTDMConOutMsg(4, "CLTDirectMusicMgr::Pause nStart=%i\n", nStart);
	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// UnPause music playing
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448920
LTRESULT CLTDirectMusicMgr::UnPause()
{
	LTDMConOutMsg(4, "CLTDirectMusicMgr::UnPause\n");
	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Set current volume
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448940
LTRESULT CLTDirectMusicMgr::SetVolume(const long nVolume)
{
	long nNewVolume = nVolume + m_nVolumeOffset;

	LTDMConOutMsg(4, "CLTDirectMusicMgr::SetVolume nVolume=%i\n", nVolume);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::SetVolume"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::SetVolume"); return LT_ERROR; }

	// set the master volume
	return FAILED(m_pPerformance->SetGlobalParam(GUID_PerfMasterVolume, (void*)&nNewVolume, sizeof(long)));
};


///////////////////////////////////////////////////////////////////////////////////////////
// Change the intensity level
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x004489d0
LTRESULT CLTDirectMusicMgr::ChangeIntensity(const int nNewIntensity, const LTDMEnactTypes nStart)
{
	IDirectMusicSegmentState* pDMSegmentState = LTNULL;
	CTransition* pTransition;
	CSegment* pSegment;
	uint32 nFlags;

	LTDMConOutMsg(4, "CLTDirectMusicMgr::ChangeIntensity nNewIntensity=%i nStart=%i\n", nNewIntensity, nStart);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::ChangeIntensity"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::ChangeIntensity"); return LT_ERROR; }

	// make sure we have a performance
	if (m_pPerformance == LTNULL) return LT_ERROR;

	// intensity 0 means stop
	if (nNewIntensity == 0)
	{
		Stop(nStart);
		return LT_ERROR;
	}

	// make sure the new intensity is valid
	if ((nNewIntensity <= 0) || (nNewIntensity > m_nNumIntensities)) return LT_ERROR;

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	// check if we are already at this intensity
	if (m_nCurIntensity == nNewIntensity)
	{
		LeaveCriticalSection(&m_CommandQueueCriticalSection);
		LTDMConOutMsg(4, "CLTDirectMusicMgr::ChangeIntensity new intensity same as old nothing to do, exiting.\n");
		return LT_OK;
	}

	// get the transition
	pTransition = GetTransition(m_nCurIntensity, nNewIntensity);
	if (pTransition == LTNULL)
	{
		LeaveCriticalSection(&m_CommandQueueCriticalSection);
		LTDMConOutError("ERROR! Invalid transition found, exiting. (CLTDirectMusicMgr::ChangeIntensity)\n");
		return LT_ERROR;
	}

	// clear the command queue
	ClearCommands();

	// figure out when to start
	LTDMEnactTypes nEnactValue;
	if (nStart == LTDMEnactInvalid) nEnactValue = pTransition->GetEnactTime();
	else nEnactValue = nStart;
	nFlags = EnactTypeToFlags(nEnactValue);

	// play the transition segment if there is one
	if (pTransition->GetDMSegment() != LTNULL)
	{
		if (nEnactValue == LTDMEnactNextSegment)
		{
			CCommandItemPlayTransition* pCommand = new CCommandItemPlayTransition;
			pCommand->SetTransition(pTransition);
			m_lstCommands.InsertLast(pCommand);
		}
		else
		{
			m_pPerformance->PlaySegment(pTransition->GetDMSegment(), nFlags, 0, &pDMSegmentState);
			m_lstPrimairySegmentsPlaying.CreateSegmentState(pDMSegmentState, LTNULL);
		}
	}
	// otherwise play the first segment in the new intensity
	else
	{
		if (nEnactValue != LTDMEnactNextSegment)
		{
			pSegment = m_aryIntensities[nNewIntensity].GetSegmentList().GetFirst();
			if ((pSegment != LTNULL) && (pSegment->GetDMSegment() != LTNULL))
			{
				m_pPerformance->PlaySegment(pSegment->GetDMSegment(), nFlags, 0, &pDMSegmentState);
				m_lstPrimairySegmentsPlaying.CreateSegmentState(pDMSegmentState, pSegment);
			}
		}
	}

	// queue up all the segments in the new intensity
	pSegment = m_aryIntensities[nNewIntensity].GetSegmentList().GetFirst();
	while (pSegment != LTNULL)
	{
		if (pSegment->GetDMSegment() != LTNULL)
		{
			CCommandItemPlaySegment* pCommand = new CCommandItemPlaySegment;
			pCommand->SetSegment(pSegment);
			m_lstCommands.InsertLast(pCommand);
		}
		pSegment = pSegment->Next();
	}

	// add the loop command
	{
		CCommandItemLoopToStart* pCommand = new CCommandItemLoopToStart;
		pCommand->SetNumLoops(m_aryIntensities[nNewIntensity].GetNumLoops());
		m_lstCommands.InsertLast(pCommand);
	}

	// add the change intensity command
	if (m_aryIntensities[nNewIntensity].GetIntensityToSetAtFinish() > 0)
	{
		CCommandChangeIntensity* pCommand = new CCommandChangeIntensity;
		pCommand->SetNewIntensity(m_aryIntensities[nNewIntensity].GetIntensityToSetAtFinish());
		m_lstCommands.InsertLast(pCommand);
	}

	// set the last command
	if ((pTransition->GetDMSegment() == LTNULL) && (nEnactValue != LTDMEnactNextSegment)) m_pLastCommand = m_lstCommands.GetFirst();
	else m_pLastCommand = LTNULL;

	// set the new intensity
	m_nCurIntensity = nNewIntensity;

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Play a secondary segment
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448d20
LTRESULT CLTDirectMusicMgr::PlaySecondary(const char* sSecondarySegment, const LTDMEnactTypes nStart)
{
	IDirectMusicSegmentState* pDMSegmentState = LTNULL;
	LTDMEnactTypes nEnact = nStart;

	LTDMConOutMsg(4, "CLTDirectMusicMgr::PlaySecondary sSecondarySegment=%s nStart=%i\n", sSecondarySegment, nStart);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::PlaySecondary"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::PlaySecondary"); return LT_ERROR; }

	// find the segment
	CSegment* pSegment = m_lstSegments.Find(sSecondarySegment);
	if (pSegment == LTNULL) return LT_ERROR;
	if (pSegment->GetDMSegment() == LTNULL) return LT_ERROR;

	// use the segment default enact time
	if (nEnact == LTDMEnactDefault) nEnact = pSegment->GetDefaultEnact();

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	if (nEnact != LTDMEnactNextSegment)
	{
		m_pPerformance->PlaySegment(pSegment->GetDMSegment(), EnactTypeToFlags(nEnact) | DMUS_SEGF_SECONDARY | DMUS_SEGF_CONTROL, 0, &pDMSegmentState);
		m_lstSecondarySegmentsPlaying.CreateSegmentState(pDMSegmentState, pSegment);
	}
	else
	{
		CCommandItemPlaySecondarySegment* pCommand = new CCommandItemPlaySecondarySegment;
		if (pCommand != LTNULL)
		{
			pCommand->SetSegment(pSegment);
			m_lstCommands2.Insert(pCommand);
		}
	}

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Stop all secondary segments with the specified name (if LTNULL it stops them all)
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448e90
LTRESULT CLTDirectMusicMgr::StopSecondary(const char* sSecondarySegment, const LTDMEnactTypes nStart)
{
	LTDMEnactTypes nEnact = nStart;

	LTDMConOutMsg(4, "CLTDirectMusicMgr::StopSecondary sSecondarySegment=%s nStart=%i\n", sSecondarySegment, nStart);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::StopSecondary"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::StopSecondary"); return LT_ERROR; }

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	// stop all secondary segments
	if (sSecondarySegment == LTNULL)
	{
		CSegmentState* pSegState = m_lstSecondarySegmentsPlaying.GetFirst();
		while (pSegState != LTNULL)
		{
			CSegmentState* pNextSegState = pSegState->Next();
			if (pSegState->GetDMSegmentState() != LTNULL)
			{
				if (nStart == LTDMEnactDefault) nEnact = pSegState->GetSegment()->GetDefaultEnact();
				if (nEnact != LTDMEnactNextSegment) m_lstSecondarySegmentsPlaying.CleanupSegmentState(this, pSegState, nEnact, FALSE);
				pSegState = pNextSegState;
			}
		}
	}
	// stop the secondary segments with this name
	else
	{
		CSegmentState* pSegState = m_lstSecondarySegmentsPlaying.Find(sSecondarySegment);
		while (pSegState != LTNULL)
		{
			if (nStart == LTDMEnactDefault) nEnact = pSegState->GetSegment()->GetDefaultEnact();
			if ((pSegState->GetSegment()->GetDMSegment() != LTNULL) && (nEnact != LTDMEnactNextSegment))
				m_lstSecondarySegmentsPlaying.CleanupSegmentState(this, pSegState, nEnact, FALSE);
			pSegState = m_lstSecondarySegmentsPlaying.Find(sSecondarySegment);
		}
	}

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Play a motif
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00448fc0 ?PlayMotif@CLTDirectMusicMgr@@UAEKPBDW4LTDMEnactTypes@@@Z
LTRESULT CLTDirectMusicMgr::PlayMotif(const char* sMotifName, const LTDMEnactTypes nStart)
{
	IDirectMusicSegmentState* pDMSegmentState = LTNULL;
	LTDMEnactTypes nEnact = nStart;

	LTDMConOutMsg(4, "CLTDirectMusicMgr::PlayMotif sMotifName=%s nStart=%i\n", sMotifName, nStart);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::PlayMotif"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::PlayMotif"); return LT_ERROR; }

	// find the motif
	CSegment* pSegment = m_lstMotifs.Find(sMotifName);
	if (pSegment == LTNULL) return LT_ERROR;
	if (pSegment->GetDMSegment() == LTNULL) return LT_ERROR;

	// use the motif default enact time
	if (nEnact == LTDMEnactDefault) nEnact = pSegment->GetDefaultEnact();

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	if (nEnact != LTDMEnactNextSegment)
	{
		m_pPerformance->PlaySegment(pSegment->GetDMSegment(), EnactTypeToFlags(nEnact) | DMUS_SEGF_SECONDARY, 0, &pDMSegmentState);
		m_lstMotifsPlaying.CreateSegmentState(pDMSegmentState, pSegment);
	}
	else
	{
		CCommandItemPlayMotif* pCommand = new CCommandItemPlayMotif;
		if (pCommand != LTNULL)
		{
			pCommand->SetSegment(pSegment);
			m_lstCommands2.Insert(pCommand);
		}
	}

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Play a motif
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449120 ?PlayMotif@CLTDirectMusicMgr@@UAEKPBD0W4LTDMEnactTypes@@@Z
LTRESULT CLTDirectMusicMgr::PlayMotif(const char* sStyleName, const char* sMotifName, const LTDMEnactTypes nStart)
{
	return PlayMotif(sMotifName, nStart);
};


///////////////////////////////////////////////////////////////////////////////////////////
// Stop all motifs with the specified name (if LTNULL it stops them all)
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449140 ?StopMotif@CLTDirectMusicMgr@@UAEKPBDW4LTDMEnactTypes@@@Z
LTRESULT CLTDirectMusicMgr::StopMotif(const char* sMotifName, const LTDMEnactTypes nStart)
{
	LTDMEnactTypes nEnact = nStart;

	LTDMConOutMsg(4, "CLTDirectMusicMgr::StopMotif sMotifName=%s nStart=%i\n", sMotifName, nStart);

	// if the mgr is not initialized we fail
	if (!m_bInitialized) { LTDMConOutMgrNotInitialized("CLTDirectMusicMgr::StopMotif"); return LT_ERROR; }

	// if the level is not initialized we fail
	if (!m_bLevelInitialized) { LTDMConOutLevelNotInitialized("CLTDirectMusicMgr::StopMotif"); return LT_ERROR; }

	EnterCriticalSection(&m_CommandQueueCriticalSection);

	// stop all motifs
	if (sMotifName == LTNULL)
	{
		CSegmentState* pSegState = m_lstMotifsPlaying.GetFirst();
		while (pSegState != LTNULL)
		{
			CSegmentState* pNextSegState = pSegState->Next();
			if (pSegState->GetDMSegmentState() != LTNULL)
			{
				if (nStart == LTDMEnactDefault) nEnact = pSegState->GetSegment()->GetDefaultEnact();
				if (nEnact != LTDMEnactNextSegment) m_lstMotifsPlaying.CleanupSegmentState(this, pSegState, nEnact, FALSE);
				pSegState = pNextSegState;
			}
		}
	}
	// stop the motifs with this name
	else
	{
		CSegmentState* pSegState = m_lstMotifsPlaying.Find(sMotifName);
		while (pSegState != LTNULL)
		{
			if (nStart == LTDMEnactDefault) nEnact = pSegState->GetSegment()->GetDefaultEnact();
			if ((pSegState->GetSegment()->GetDMSegment() != LTNULL) && (nEnact != LTDMEnactNextSegment))
				m_lstMotifsPlaying.CleanupSegmentState(this, pSegState, nEnact, FALSE);
			pSegState = m_lstMotifsPlaying.Find(sMotifName);
		}
	}

	LeaveCriticalSection(&m_CommandQueueCriticalSection);

	return LT_OK;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Stop all motifs with the specified name (if LTNULL it stops them all)
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449270 ?StopMotif@CLTDirectMusicMgr@@UAEKPBD0W4LTDMEnactTypes@@@Z
LTRESULT CLTDirectMusicMgr::StopMotif(const char* sStyleName, const char* sMotifName, const LTDMEnactTypes nStart)
{
	return StopMotif(sMotifName, nStart);
};


///////////////////////////////////////////////////////////////////////////////////////////
// Initialize DirectMusic
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449290
LTBOOL CLTDirectMusicMgr::InitDirectMusic()
{
	// create the directmusic performance
	if (FAILED(CoCreateInstance(CLSID_DirectMusicPerformance, LTNULL, CLSCTX_INPROC, IID_IDirectMusicPerformance8, (void**)&m_pPerformance)))
    {
		// we failed set performance to LTNULL
		m_pPerformance = LTNULL;

		// we have failed exit function
		return FALSE;
    }

	// create the directmusic loader
	m_pLoader = new CLTDMLoader;
	if (m_pLoader == LTNULL)
    {
		// we failed set loader to LTNULL
        m_pLoader = LTNULL;

		// close down and release the performance
		m_pPerformance->CloseDown();
		m_pPerformance->Release();
		m_pPerformance = LTNULL;

		// we have failed exit function
		return FALSE;
    }
	m_pLoader->Init();

    // Initialize the performance
	m_pDirectMusic = LTNULL;
	if (FAILED(m_pPerformance->Init(&m_pDirectMusic, LTNULL, LTNULL)))
	{
		// Release the loader object.
	    m_pLoader->Release();
		m_pLoader = LTNULL;

		// close down and release the performance
		m_pPerformance->CloseDown();
		m_pPerformance->Release();
		m_pPerformance = LTNULL;

		// we have failed exit function
		return FALSE;
	}

	// Initialize the synthesizer
	if (!InitPerformance())
	{
		// make sure direct sound object is removed and clear directmusic pointer
		if (m_pDirectMusic != LTNULL)
		{
			m_pDirectMusic->SetDirectSound(LTNULL, LTNULL);
			m_pDirectMusic = LTNULL;
		}

		// Release the loader object.
		if (m_pLoader != LTNULL)
		{
			m_pLoader->Release();
			m_pLoader = LTNULL;
		}

		// close down and release the performance
		if (m_pPerformance != LTNULL)
		{
			m_pPerformance->CloseDown();
			m_pPerformance->Release();
			m_pPerformance = LTNULL;
		}

		// we have failed exit function
		return FALSE;
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Initialize the synthesizer
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449380
LTBOOL CLTDirectMusicMgr::InitPerformance()
{
	HRESULT hr;

	// get rid of the old port
	if (m_pPort != LTNULL) TermPort();

	// setup synthesizer parameters
	memset(&m_portParams, 0, sizeof(m_portParams));
	m_portParams.dwVoices = m_nNumVoices;
	m_portParams.dwChannelGroups = ((m_nNumPChannels-1)/16)+1;
	m_portParams.dwAudioChannels = m_nNumPChannels;
	m_portParams.dwSize = sizeof(DMUS_PORTPARAMS);
	m_portParams.dwValidParams = DMUS_PORTPARAMS_VOICES | DMUS_PORTPARAMS_CHANNELGROUPS | DMUS_PORTPARAMS_AUDIOCHANNELS | DMUS_PORTPARAMS_SAMPLERATE;
	m_portParams.dwSampleRate = m_nSynthSampleRate;

	// create the port
	hr = m_pDirectMusic->CreatePort(GUID_NULL, &m_portParams, &m_pPort, LTNULL);
	if (FAILED(hr)) goto Done;

	// activate the port
	hr = m_pPort->Activate(TRUE);
	if (FAILED(hr)) goto Done;

	// add the port to the performance
	hr = m_pPerformance->AddPort(m_pPort);
	if (FAILED(hr)) goto Done;

	// assign the pchannel blocks
	{
		for (DWORD i = 0; i < m_portParams.dwChannelGroups; i++)
		{
			if (FAILED(m_pPerformance->AssignPChannelBlock(i, m_pPort, i+1))) break;
		}
	}

Done:
	return SUCCEEDED(hr);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Terminate the synthesizer port
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449460
LTBOOL CLTDirectMusicMgr::TermPort()
{
	if (m_pPort != LTNULL)
	{
		m_pPort->Activate(FALSE);
		if (m_pPerformance != LTNULL) m_pPerformance->RemovePort(m_pPort);
		m_pPort->Release();
		m_pPort = LTNULL;
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Terminate DirectMusic
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x004494a0
void CLTDirectMusicMgr::TermDirectMusic()
{
	// stop the performance from playing
	if (m_pPerformance != LTNULL)
	{
		m_pPerformance->Stop( LTNULL, LTNULL, 0, 0 );
	}

	// get rid of the port
	TermPort();

	// make sure direct sound object is removed and clear directmusic pointer
	if (m_pDirectMusic != LTNULL)
	{
		m_pDirectMusic->SetDirectSound(LTNULL, LTNULL);
		m_pDirectMusic = LTNULL;
	}

	// close down and release the performance
	if (m_pPerformance != LTNULL)
	{
		m_pPerformance->CloseDown();
		m_pPerformance->Release();
		m_pPerformance = LTNULL;
	}

    // Release the loader object.
	if (m_pLoader != LTNULL)
	{
		delete m_pLoader;
		m_pLoader = LTNULL;
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Set the DirectMusic working directory
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449520
void CLTDirectMusicMgr::SetWorkingDirectory(const char* sWorkingDirectory, LTDMFileTypes nFileType)
{
	// store the any type locally
	if (nFileType == LTDMFileTypeAny)
	{
		// remove the old value
		if (m_sWorkingDirectoryAny != LTNULL)
		{
			delete [] m_sWorkingDirectoryAny;
			m_sWorkingDirectoryAny = LTNULL;
		}

		// store the new value if it is not null
		if (sWorkingDirectory != LTNULL)
		{
			m_sWorkingDirectoryAny = new char[strlen(sWorkingDirectory)+1];
			strcpy(m_sWorkingDirectoryAny, sWorkingDirectory);
		}
	}

	// store the control file type locally
	else if (nFileType == LTDMFileTypeControlFile)
	{
		// remove the old value
		if (m_sWorkingDirectoryControlFile != LTNULL)
		{
			delete [] m_sWorkingDirectoryControlFile;
			m_sWorkingDirectoryControlFile = LTNULL;
		}

		// store the new value if it is not null
		if (sWorkingDirectory != LTNULL)
		{
			m_sWorkingDirectoryControlFile = new char[strlen(sWorkingDirectory)+1];
			strcpy(m_sWorkingDirectoryControlFile, sWorkingDirectory);
		}
	}

	// convert file type to GUID type
	CLSID m_clsID = GUID_NULL;
	switch (nFileType)
	{
		case LTDMFileTypeAny : { m_clsID = GUID_DirectMusicAllTypes; break; }
		case LTDMFileTypeDLS : { m_clsID = CLSID_DirectMusicCollection; break; }
		case LTDMFileTypeStyle : { m_clsID = CLSID_DirectMusicStyle; break; }
		case LTDMFileTypeSegment : { m_clsID = CLSID_DirectMusicSegment; break; }
		case LTDMFileTypeChordMap : { m_clsID = CLSID_DirectMusicChordMap; break; }
	}

	// set the search directory for directmusic if GUID is valid
    if (m_clsID != GUID_NULL)
	{
		m_pLoader->SetSearchDirectory( m_clsID, sWorkingDirectory, FALSE );
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Load Segment
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449710
LTBOOL CLTDirectMusicMgr::LoadSegment(const char* sSegmentName )
{
    HRESULT         hr;
    DMUS_OBJECTDESC ObjDesc;
	IDirectMusicSegment* pDMSegment;
	CSegment*		pSegment;

	// first check and see if this segment has already been loaded
	if (m_lstSegments.Find(sSegmentName) != LTNULL)
	{
		// it has already been loaded to we don't need to load it again just exit happily
		return TRUE;
	}

	// make a new segment item
	pSegment = new CSegment;
	if (pSegment == LTNULL) return FALSE;

	// set up object descriptor
    ObjDesc.guidClass = CLSID_DirectMusicSegment;
    ObjDesc.dwSize = sizeof(DMUS_OBJECTDESC);
	MULTI_TO_WIDE( ObjDesc.wszFileName, sSegmentName);
    ObjDesc.dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_FILENAME;

	// load the object
    hr = m_pLoader->GetObject( &ObjDesc, IID_IDirectMusicSegment, (void**)&pDMSegment );

	// check if the load succeeded
    if (SUCCEEDED(hr))
	{
		// load all dls banks this segment refrences
		pDMSegment->SetParam(GUID_Download, 0xFFFFFFFF, 0, 0, LTNULL);

		// set the new segment up
		pSegment->SetDMSegment(pDMSegment);
		pSegment->SetSegmentName(sSegmentName);
		m_lstSegments.Insert(pSegment);

		// output debut info
		LTDMConOutMsg(3, "LTDirectMusic loaded segment %s\n", sSegmentName);

	    return TRUE;
	}
	// if the load failed
	else
	{
		LTDMConOutWarning("WARNING! LTDirectMusic failed to load segment %s\n", sSegmentName);

		delete pSegment;

		return FALSE;
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Load DLS Bank
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449890
LTBOOL CLTDirectMusicMgr::LoadDLSBank(const char* sFileName)
{
	IDirectMusicCollection* pDMCollection = LTNULL;
	CDLSBank* pDLSBank;

	// convert the specified file name to wide characters
    WCHAR wszFileName[_MAX_PATH];
    MULTI_TO_WIDE( wszFileName, sFileName );

	// check if the filename is empty then don't load anything
	if ( wcscmp(wszFileName, L"") == 0 )
		return TRUE;

	// create DLSBank
	pDLSBank = new CDLSBank();
	if (pDLSBank == LTNULL) return FALSE;

	// add to DLSBank list
	pDLSBank->SetDLSBank(pDMCollection);
	m_lstDLSBanks.Insert(pDLSBank);

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Load Style and associated bands
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449980
LTBOOL CLTDirectMusicMgr::LoadStyleAndBands(char* sStyleFileName, CControlFileMgr& controlFile)
{
	// convert the specified file name to wide characters
    WCHAR wszFileName[_MAX_PATH];
    MULTI_TO_WIDE( wszFileName, sStyleFileName );
	IDirectMusicStyle* pDMStyle;
	CStyle* pStyle;

	// check if the filename is empty then don't load anything
	if ( wcscmp(wszFileName, L"") == 0 )
		return TRUE;

	// allocate the style class
	pStyle = new CStyle;
	if (pStyle == LTNULL) return FALSE;

	// set up the object description
	DMUS_OBJECTDESC ObjectDescript;
	ObjectDescript.guidClass = CLSID_DirectMusicStyle;
	wcscpy(ObjectDescript.wszFileName, wszFileName);
	ObjectDescript.dwValidData = DMUS_OBJ_CLASS | DMUS_OBJ_FILENAME ;
	ObjectDescript.dwSize = sizeof(DMUS_OBJECTDESC);

	// load the style object
	if (FAILED(m_pLoader->GetObject(&ObjectDescript, IID_IDirectMusicStyle, (void**)&pDMStyle)))
	{
		// load failed
		LTDMConOutWarning("WARNING! LTDirectMusic failed to style %s\n", sStyleFileName);
		delete pStyle;
		return FALSE;
	}
	else
	{
		// output debug message that we loaded the style
		LTDMConOutMsg(3, "LTDirectMusic loaded style %s\n", sStyleFileName);

		// load all the band commands for this level and load any of them that are
		// specified for this style file
		CControlFileKey* pKey = controlFile.GetKey(LTNULL, "BAND");
		CControlFileWord* pWord;
		CControlFileWord* pWord2;
		while (pKey != LTNULL)
		{
			// get the name of the style file this band is for
			pWord = pKey->GetFirstWord();
			if (pWord != LTNULL)
			{
				// check if this matches the style we are loading
				if (stricmp(pWord->GetVal(), sStyleFileName) == 0)
				{
					// get name of the band to load
					pWord2 = pWord->Next();
					if (pWord2 != LTNULL)
					{
						// load the band
						LoadBand(pDMStyle, pWord2->GetVal());
					}
				}
			}
			pKey = pKey->NextWithSameName();
		}

		// add style to list of styles
		pStyle->SetDMStyle(pDMStyle);
		pStyle->SetStyleName(sStyleFileName);
		m_lstStyles.Insert(pStyle);

		return TRUE;
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Load Band
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449b50
LTBOOL CLTDirectMusicMgr::LoadBand(IDirectMusicStyle* pStyle, const char* sBandName)
{
	IDirectMusicBand* pDMBand;
	CBand* pBand;

	// check if style is null
	if (pStyle == LTNULL) return FALSE;

	// create new band object
	pBand = new CBand;
	if (pBand == LTNULL) return FALSE;

	// convert band name to wide characters
	WCHAR wszBandName[_MAX_PATH];
    MULTI_TO_WIDE( wszBandName, sBandName );

	// check if name is empty
	if ( wcscmp(wszBandName, L"") != 0 )
	{
		// load band
		pStyle->GetBand(wszBandName, &pDMBand);

		// if we failed then exit
		if (pDMBand == LTNULL)
		{
			delete pBand;
			return FALSE;
		}

		// download all of the instruments in the band
		HRESULT hr = pDMBand->Download(m_pPerformance);

		// check if download worked
		if (FAILED(hr))
		{
			LTDMConOutWarning("WARNING! LTDirectMusic failed to load band %s\n", sBandName);
		}

		// add band to global list of bands
		pBand->SetBand(pDMBand);
		m_lstBands.Insert(pBand);

		// create a band segment
		IDirectMusicSegment* pNewSeg;
		hr = pDMBand->CreateSegment(&pNewSeg);

		// check if band segment was created OK
		if (FAILED(hr))
		{
			LTDMConOutWarning("WARNING! LTDirectMusic failed to create band segment for band %s\n", sBandName);
		}
		else
		{
			// play the band segment
			m_pPerformance->PlaySegment(pNewSeg, DMUS_SEGF_SECONDARY, 0, LTNULL);

			// set the band segment in the band structure
			pBand->SetDMSegment(pNewSeg);
		}

		// output debug message that we loaded the style
		LTDMConOutMsg(3, "LTDirectMusic loaded band %s\n", sBandName);
	}

	// we succeeded
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Read in and set up DLS Banks from control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449cc0
LTBOOL CLTDirectMusicMgr::ReadDLSBanks(CControlFileMgr& controlFile)
{
	CControlFileKey* pKey = controlFile.GetKey(LTNULL, "DLSBANK");
	CControlFileWord* pWord;

	// loop through all DLSBANK keys
	while (pKey != LTNULL)
	{
		// get the first value
		pWord = pKey->GetFirstWord();
		if (pWord != LTNULL)
		{
			// load the DLS Bank
			LoadDLSBank(pWord->GetVal());
		}

		// get the next key
		pKey = pKey->NextWithSameName();
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Read in and load styles and bands from control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449d10
LTBOOL CLTDirectMusicMgr::ReadStylesAndBands(CControlFileMgr& controlFile)
{
	CControlFileKey* pKey = controlFile.GetKey(LTNULL, "STYLE");
	CControlFileWord* pWord;

	// loop through all STYLE keys
	while (pKey != LTNULL)
	{
		// get the first value
		pWord = pKey->GetFirstWord();
		if (pWord != LTNULL)
		{
			// load the style and any associated bands
			LoadStyleAndBands(pWord->GetVal(), controlFile);
		}

		// get the next key
		pKey = pKey->NextWithSameName();
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Read in intensity descriptions from control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449d60
LTBOOL CLTDirectMusicMgr::ReadIntensities(CControlFileMgr& controlFile)
{
	CControlFileKey* pKey = controlFile.GetKey(LTNULL, "INTENSITY");
	CControlFileWord* pWord;
	int nIntensity;
	int nLoop;
	int nNextIntensity;

	// loop through all INTENSITY keys
	while (pKey != LTNULL)
	{
		// get the first word which is the intensity number
		pWord = pKey->GetFirstWord();

		// if this is LTNULL then we have an invalid Intensity definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the value for the intensity
		nIntensity = 0;
		pWord->GetVal(nIntensity);

		// make sure the intensity number is valid
		if ((nIntensity <= 0) || (nIntensity > m_nNumIntensities))
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the next word which is the loop value
		pWord = pWord->Next();

		// if this is LTNULL then we have an invalid Intensity definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the loop value
		nLoop = -1;
		pWord->GetVal(nLoop);

		// get the next word with is the next intensity to switch to when this one finishes
		pWord = pWord->Next();

		// if this is LTNULL then we have an invalid Intensity definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the next intensity to switch to value
		nNextIntensity = 0;
		pWord->GetVal(nNextIntensity);

		// get the next word which is the first segment name for this intensity
		pWord = pWord->Next();

		// if this is LTNULL then we have an invalid Intensity definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// loop through all segment names and add them to the intensity and load them in
		while (pWord != LTNULL)
		{
			// load in the segment
			LoadSegment(pWord->GetVal());

			// find the loaded segment in the master segment list
			CSegment* pMasterSegment = m_lstSegments.Find(pWord->GetVal());
			if (pMasterSegment == LTNULL) break;
			if (pMasterSegment->GetDMSegment() == LTNULL) break;

			// add the segment to this intensity
			CSegment* pNewSeg = new CSegment;
			if (pNewSeg == LTNULL) break;
			pNewSeg->SetDMSegment(pMasterSegment->GetDMSegment());
			m_aryIntensities[nIntensity].GetSegmentList().InsertLast(pNewSeg);

			// get the next word which is the next segment
			pWord = pWord->Next();
		}

		// set the other values in our intensity
		m_aryIntensities[nIntensity].SetNumLoops(nLoop);
		m_aryIntensities[nIntensity].SetIntensityToSetAtFinish(nNextIntensity);

		// get the next key
		pKey = pKey->NextWithSameName();
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Read in secondary segments from control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449ef0
LTBOOL CLTDirectMusicMgr::ReadSecondarySegments(CControlFileMgr& controlFile)
{
	CControlFileKey* pKey = controlFile.GetKey(LTNULL, "SECONDARYSEGMENT");
	CControlFileWord* pWord;

	// loop through all SECONDARYSEGMENT keys
	while (pKey != LTNULL)
	{
		// get the first value
		pWord = pKey->GetFirstWord();
		if (pWord != LTNULL)
		{
			// load the segment
			LoadSegment(pWord->GetVal());

			// find the loaded segment in the master segment list
			CSegment* pMasterSegment = m_lstSegments.Find(pWord->GetVal());
			if (pMasterSegment != LTNULL)
			{
				// get the next word which is the first
				pWord = pWord->Next();
				if (pWord != LTNULL)
				{
					// set the enact time in the segment
					pMasterSegment->SetDefaultEnact(StringToEnactType(pWord->GetVal()));
				}
			}
		}

		// get the next key
		pKey = pKey->NextWithSameName();
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Read in motifs from control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x00449f70
LTBOOL CLTDirectMusicMgr::ReadMotifs(CControlFileMgr& controlFile)
{
	CControlFileKey* pKey = controlFile.GetKey(LTNULL, "MOTIF");
	CControlFileWord* pWord;

	// loop through all MOTIF keys
	while (pKey != LTNULL)
	{
		// get the first word
		pWord = pKey->GetFirstWord();
		if (pWord != LTNULL)
		{
			// find the name of the style
			char* sStyleName = pWord->GetVal();

			// find the style
			CStyle* pStyle = m_lstStyles.Find(sStyleName);

			// get the second word
			pWord = pWord->Next();
			if (pWord != LTNULL)
			{
				// find the name of the motif
				char* sMotifName = pWord->GetVal();

				// make sure we have a valid style and motif name
				if ((pStyle != LTNULL) && (sMotifName != LTNULL))
				{
					// load in the motif returning a motif segment
					IDirectMusicSegment* pDMSeg;
					WCHAR wszMotif[_MAX_PATH];
				  	MULTI_TO_WIDE( wszMotif, sMotifName );

					// make sure we got a segment
					if ((pStyle->GetDMStyle()->GetMotif( wszMotif, &pDMSeg ) == S_OK) && (pDMSeg != LTNULL))
					{
						// create a new motif segment
						CSegment* pSegment = new CSegment;
						if (pSegment != LTNULL)
						{
							// set the members of the new segment
							pSegment->SetDMSegment(pDMSeg);
							pSegment->SetSegmentName(sMotifName);

							// see if there is an enact time present and set it
							pWord = pWord->Next();
							if (pWord != LTNULL)
							{
								// set the enact time in the segment
								pSegment->SetDefaultEnact(StringToEnactType(pWord->GetVal()));
							}

							// add new segment to the master motif segment list
							m_lstMotifs.Insert(pSegment);
						}
					}
				}
			}
		}

		// get the next key
		pKey = pKey->NextWithSameName();
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Read in transition matrix from control file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a0b0
LTBOOL CLTDirectMusicMgr::ReadTransitions(CControlFileMgr& controlFile)
{
	CControlFileKey* pKey = controlFile.GetKey(LTNULL, "TRANSITION");
	CControlFileWord* pWord;
	int nTransitionFrom;
	int nTransitionTo;
	LTDMEnactTypes m_nEnactTime;
	LTBOOL m_bManual;

	// loop through all TRANSITION keys
	while (pKey != LTNULL)
	{
		// get the first word which is the from intensity transition value
		pWord = pKey->GetFirstWord();

		// if this is LTNULL then we have an invalid tranisition definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the value for the intensity
		nTransitionFrom = 0;
		pWord->GetVal(nTransitionFrom);

		// make sure the intensity number is valid
		if ((nTransitionFrom <= 0) || (nTransitionFrom > m_nNumIntensities))
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the next word which is the intensity transition to value
		pWord = pWord->Next();

		// if this is LTNULL then we have an invalid tranisition definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the value for the intensity
		nTransitionTo = 0;
		pWord->GetVal(nTransitionTo);

		// make sure the intensity number is valid
		if ((nTransitionTo <= 0) || (nTransitionTo > m_nNumIntensities))
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the next word which is the when to enact the transition value
		pWord = pWord->Next();

		// if this is LTNULL then we have an invalid tranisition definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// figure out which enact value user specified
		m_nEnactTime = StringToEnactType(pWord->GetVal());

		// make sure we got a valid enact time
		if (m_nEnactTime == LTDMEnactInvalid)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// get the next word which defines if this is an automatic or manual transition
		pWord = pWord->Next();

		// if this is LTNULL then we have an invalid tranisition definition go on to next one
		if (pWord == LTNULL)
		{
			pKey = pKey->NextWithSameName();
			continue;
		}

		// figure out which enact value user specified
		if (stricmp(pWord->GetVal(), "MANUAL") == 0) m_bManual = TRUE;
		else
		{
			if (stricmp(pWord->GetVal(), "AUTOMATIC") == 0) m_bManual = FALSE;
			else
			{
				pKey = pKey->NextWithSameName();
				continue;
			}
		}

		// get the next word which is the when to enact the transition value
		pWord = pWord->Next();

		// check if there is a segment name for this transition
		if (pWord != LTNULL)
		{
			// load in the segment
			LoadSegment(pWord->GetVal());

			// find the loaded segment in the master segment list
			CSegment* pMasterSegment = m_lstSegments.Find(pWord->GetVal());
			if (pMasterSegment != LTNULL)
			{
				GetTransition(nTransitionFrom, nTransitionTo)->SetDMSegment(pMasterSegment->GetDMSegment());
			}
			else
			{
				GetTransition(nTransitionFrom, nTransitionTo)->SetDMSegment(LTNULL);
			}
		}

		// set the other values in our transition
		GetTransition(nTransitionFrom, nTransitionTo)->SetEnactTime(m_nEnactTime);
		GetTransition(nTransitionFrom, nTransitionTo)->SetManual(m_bManual);

		// get the next key
		pKey = pKey->NextWithSameName();
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Set the name for the segment
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a250
LTBOOL CLTDirectMusicMgr::CSegment::SetSegmentName(const char* sSegmentName)
{
	// if there was an old long segment name delete it
	if (m_sSegmentNameLong != LTNULL)
	{
		delete [] m_sSegmentNameLong;
		m_sSegmentNameLong = LTNULL;
	}

	// if new name is null
	if (sSegmentName == LTNULL)
	{
		// just set the segment name to nothing
		m_sSegmentName[0] = '\0';
	}

	// if new name is valid then proceed
	else
	{
		// find out the length of the string
		int nLen = strlen(sSegmentName);

		// check if we need to use the long storage method
		if (nLen > 63)
		{
			// allocate the new string
			m_sSegmentNameLong = new char[nLen+1];

			// if allocation failed then exit
			if (m_sSegmentNameLong == LTNULL) return FALSE;

			// copy over the string contents
			strcpy(m_sSegmentNameLong, sSegmentName);

			// set the short name to nothing
			m_sSegmentName[0] = '\0';
		}

		// it is a short name so just copy it over
		else
		{
			strcpy(m_sSegmentName, sSegmentName);
		}
	}

	// exit successfully
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Get the name for the segment
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a2e0
const char* CLTDirectMusicMgr::CSegment::GetSegmentName()
{
	if (m_sSegmentNameLong == LTNULL) return m_sSegmentName;
	else return m_sSegmentNameLong;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Find the segment state with the specified segment name return null if not found
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a2f0 ?Find@CSegmentStateList@CLTDirectMusicMgr@@QAEPAVCSegmentState@2@PBD@Z
CLTDirectMusicMgr::CSegmentState* CLTDirectMusicMgr::CSegmentStateList::Find(const char* sName)
{
	// make sure name is valid
	if (sName == LTNULL) return LTNULL;

	// loop through all segment states
	CSegmentState* pSegState = GetFirst();
	while (pSegState != LTNULL)
	{
		if (pSegState->GetSegment() != LTNULL)
		{
			if (pSegState->GetSegment()->GetSegmentName() != LTNULL)
			{
				if (stricmp(pSegState->GetSegment()->GetSegmentName(), sName) == 0) return pSegState;
			}
		}
		pSegState = pSegState->Next();
	}

	return LTNULL;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Find the segment state with the specified directmusic segment state
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a340 ?Find@CSegmentStateList@CLTDirectMusicMgr@@QAEPAVCSegmentState@2@PBUIDirectMusicSegmentState@@@Z
CLTDirectMusicMgr::CSegmentState* CLTDirectMusicMgr::CSegmentStateList::Find(const IDirectMusicSegmentState* pDMSegmentState)
{
	// loop through all segment states
	CSegmentState* pSegState = GetFirst();
	while (pSegState != LTNULL)
	{
		if (pDMSegmentState == pSegState->GetDMSegmentState()) return pSegState;
		pSegState = pSegState->Next();
	}

	return LTNULL;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Find the segment object with the specified name return null if not found
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a360
CLTDirectMusicMgr::CSegment* CLTDirectMusicMgr::CSegmentList::Find(const char* sName)
{
	// make sure name is valid
	if (sName == LTNULL) return LTNULL;

	// loop through all segments
	CSegment* pSegment = GetFirst();
	while (pSegment != LTNULL)
	{
		if (pSegment->GetSegmentName() != LTNULL)
		{
			if (stricmp(pSegment->GetSegmentName(), sName) == 0) return pSegment;
		}
		pSegment = pSegment->Next();
	}

	return LTNULL;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Set the name for the style (identical code folded with CSegment::SetSegmentName)
///////////////////////////////////////////////////////////////////////////////////////////
LTBOOL CLTDirectMusicMgr::CStyle::SetStyleName(const char* sStyleName)
{
	// if there was an old long style name delete it
	if (m_sStyleNameLong != LTNULL)
	{
		delete [] m_sStyleNameLong;
		m_sStyleNameLong = LTNULL;
	}

	// if new name is null
	if (sStyleName == LTNULL)
	{
		// just set the style name to nothing
		m_sStyleName[0] = '\0';
	}

	// if new name is valid then proceed
	else
	{
		// find out the length of the string
		int nLen = strlen(sStyleName);

		// check if we need to use the long storage method
		if (nLen > 63)
		{
			// allocate the new string
			m_sStyleNameLong = new char[nLen+1];

			// if allocation failed then exit
			if (m_sStyleNameLong == LTNULL) return FALSE;

			// copy over the string contents
			strcpy(m_sStyleNameLong, sStyleName);

			// set the short name to nothing
			m_sStyleName[0] = '\0';
		}

		// it is a short name so just copy it over
		else
		{
			strcpy(m_sStyleName, sStyleName);
		}
	}

	// exit successfully
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Get the name for the style (identical code folded with CSegment::GetSegmentName)
///////////////////////////////////////////////////////////////////////////////////////////
const char* CLTDirectMusicMgr::CStyle::GetStyleName()
{
	if (m_sStyleNameLong == LTNULL) return m_sStyleName;
	else return m_sStyleNameLong;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Find the style object with the specified name (identical code folded with CSegmentList::Find)
///////////////////////////////////////////////////////////////////////////////////////////
CLTDirectMusicMgr::CStyle* CLTDirectMusicMgr::CStyleList::Find(const char* sName)
{
	// make sure name is valid
	if (sName == LTNULL) return LTNULL;

	// loop through all styles
	CStyle* pStyle = GetFirst();
	while (pStyle != LTNULL)
	{
		if (pStyle->GetStyleName() != LTNULL)
		{
			if (stricmp(pStyle->GetStyleName(), sName) == 0) return pStyle;
		}
		pStyle = pStyle->Next();
	}

	return LTNULL;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Destructor for intensity class
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a3b0
CLTDirectMusicMgr::CIntensity::~CIntensity()
{
	// free all of the segment classes that were specified
	CSegment* pSegment;
	while ((pSegment = m_lstSegments.GetFirst()) != LTNULL)
	{
		// make sure the segment name is de-allocated
		pSegment->SetSegmentName(LTNULL);

		// remove our Segment object from the Segment list
		m_lstSegments.Delete(pSegment);

		// delete our Segment object
		delete pSegment;
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Clear the command queue
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a400
void CLTDirectMusicMgr::ClearCommands()
{
	CCommandItem* pCommand;

	// loop through all the commands
	while ((pCommand = m_lstCommands.GetFirst()) != LTNULL)
	{
		m_lstCommands.Delete(pCommand);
		delete pCommand;
	}

	// loop through all the commands in 2nd command queue
	while ((pCommand = m_lstCommands2.GetFirst()) != LTNULL)
	{
		m_lstCommands2.Delete(pCommand);
		delete pCommand;
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Get the transition from one intensity to another
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a460
CLTDirectMusicMgr::CTransition* CLTDirectMusicMgr::GetTransition(int nFrom, int nTo)
{
	if ((nFrom > m_nNumTransitions) || (nFrom <= 0) || (nTo > m_nNumTransitions) || (nTo <= 0))
	{
		return &m_aryTransitions[0];
	}
	else
	{
		return &m_aryTransitions[nFrom+1+(nTo*(m_nNumIntensities+1))];
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// Cleanup segments in this segment list
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a4b0
void CLTDirectMusicMgr::CSegmentList::CleanupSegments(CLTDirectMusicMgr* pLTDMMgr, LTBOOL bOnlyIfNotPlaying)
{
	CSegment* pSegment = GetFirst();
	CSegment* pNextSegment;

	while (pSegment != LTNULL)
	{
		pNextSegment = pSegment->Next();
		CleanupSegment(pLTDMMgr, pSegment, LTDMEnactDefault, bOnlyIfNotPlaying);
		pSegment = pNextSegment;
	}
};


///////////////////////////////////////////////////////////////////////////////////////////
// cleanup a segment (stops, deletes, and removes from list)
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a4e0
void CLTDirectMusicMgr::CSegmentList::CleanupSegment(CLTDirectMusicMgr* pLTDMMgr, CSegment* pSegment, LTDMEnactTypes nStart, LTBOOL bOnlyIfNotPlaying)
{
	// make sure parameters passed in are OK
	if ((pLTDMMgr != LTNULL) && (pSegment != LTNULL))
	{
		// make sure directmusic segment is not LTNULL
		if (pSegment->GetDMSegment() != LTNULL)
		{
			// check if it is playing
			LTBOOL bPlaying = pLTDMMgr->GetDMPerformance()->IsPlaying(pSegment->GetDMSegment(), LTNULL) != S_OK;

			// check if this segment is not playing or if we don't have to check
			if ((!bPlaying) || (!bOnlyIfNotPlaying))
			{
				// stop the segment
				pLTDMMgr->GetDMPerformance()->Stop( pSegment->GetDMSegment(), LTNULL, 0, 0 );

				// release the directmusic Segment
				if (pSegment->GetDMSegment() != LTNULL) pSegment->GetDMSegment()->Release();

				// make sure the segment name is de-allocated
				pSegment->SetSegmentName(LTNULL);

				// remove our Segment object from the Segment list
				Delete(pSegment);

				// delete our Segment object
				delete pSegment;
			}
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// add a new segment state (creates, and add to the list)
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a570
void CLTDirectMusicMgr::CSegmentStateList::CreateSegmentState(IDirectMusicSegmentState* pDMSegmentState, CSegment* pSegment)
{
	CSegmentState* pNewSegmentState;

	// make new direct music segment state is not NULL
	if (pDMSegmentState != LTNULL)
	{
		// create a new secondary segment state item
		pNewSegmentState = new CSegmentState;

		// make sure allocation worked
		if (pNewSegmentState != LTNULL)
		{
			pNewSegmentState->SetSegment(pSegment);
			pNewSegmentState->SetDMSegmentState(pDMSegmentState);
			Insert(pNewSegmentState);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// cleanup segment states
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a5e0
void CLTDirectMusicMgr::CSegmentStateList::CleanupSegmentStates(CLTDirectMusicMgr* pLTDMMgr, LTBOOL bOnlyIfNotPlaying)
{
	CSegmentState* pSegmentState = GetFirst();
	CSegmentState* pNextSegmentState;

	while (pSegmentState != LTNULL)
	{
		pNextSegmentState = pSegmentState->Next();
		CleanupSegmentState(pLTDMMgr, pSegmentState, LTDMEnactDefault, bOnlyIfNotPlaying);
		pSegmentState = pNextSegmentState;
	}
};


///////////////////////////////////////////////////////////////////////////////////////////
// cleanup segment state
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a610
void CLTDirectMusicMgr::CSegmentStateList::CleanupSegmentState(CLTDirectMusicMgr* pLTDMMgr, CSegmentState* pSegmentState, LTDMEnactTypes nStart, LTBOOL bOnlyIfNotPlaying)
{
	// make sure parameters passed in are OK
	if ((pLTDMMgr != LTNULL) && (pSegmentState != LTNULL))
	{
		// make sure directmusic segment is not LTNULL
		if (pSegmentState->GetDMSegmentState() != LTNULL)
		{
			// check if it is playing
			LTBOOL bPlaying = pLTDMMgr->GetDMPerformance()->IsPlaying(LTNULL, pSegmentState->GetDMSegmentState()) != S_OK;

			// check if this segment is not playing or if we don't have to check
			if ((!bPlaying) || (!bOnlyIfNotPlaying))
			{
				// if the enact type is invalid then do not call stop on this segment state
				if (nStart != LTDMEnactInvalid)
				{
					pLTDMMgr->GetDMPerformance()->Stop( LTNULL, pSegmentState->GetDMSegmentState(), 0, 0 );
				}

				// release the directmusic Segment
				if (pSegmentState->GetDMSegmentState() != LTNULL)
					pSegmentState->GetDMSegmentState()->Release();

				// remove our Segment object from the Segment list
				Delete(pSegmentState);

				// delete our Segment object
				delete pSegmentState;
			}
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// return the directmusic flags value that corresponds to the specified enact value
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a690
uint32 CLTDirectMusicMgr::EnactTypeToFlags(LTDMEnactTypes nEnactVal)
{
	uint32 nFlags = 0;

	switch(nEnactVal)
	{
		case LTDMEnactDefault :
		{
			nFlags = DMUS_SEGF_DEFAULT;
			break;
		}
		case LTDMEnactImmediately :
		{
			nFlags = 0;
			break;
		}
		case LTDMEnactNextBeat :
		{
			nFlags = DMUS_SEGF_BEAT;
			break;
		}
		case LTDMEnactNextMeasure :
		{
			nFlags = DMUS_SEGF_MEASURE;
			break;
		}
		case LTDMEnactNextGrid :
		{
			nFlags = DMUS_SEGF_GRID;
			break;
		}
		case LTDMEnactNextSegment :
		{
			nFlags = DMUS_SEGF_MEASURE;
			break;
		}
		default :
		{
			nFlags = 0;
		}
	}

	return nFlags;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Set reverb parameters
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a6e0
LTBOOL CLTDirectMusicMgr::SetReverbParameters(DMUS_WAVES_REVERB_PARAMS params)
{
	// make sure we have a port
	if (m_pPort != LTNULL)
	{
		IKsControl* pControl;

		// get the property set interface for the port
		if (SUCCEEDED(m_pPort->QueryInterface(IID_IKsControl, (void**)&pControl)))
		{
			KSPROPERTY ksp;
			ULONG cb;

			// set the reverb parameters
			ZeroMemory(&ksp, sizeof(ksp));
			ksp.Set = GUID_DMUS_PROP_WavesReverb;
			ksp.Id = 0;
			ksp.Flags = KSPROPERTY_TYPE_SET;
			pControl->KsProperty(&ksp, sizeof(ksp), (LPVOID)&params, sizeof(params), &cb);

			pControl->Release();

			return TRUE;
		}
	}

	return FALSE;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Enable reverb
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a780
LTBOOL CLTDirectMusicMgr::EnableReverb()
{
	// make sure we have a port
	if (m_pPort != LTNULL)
	{
		IKsControl* pControl;
		DWORD dwEffects = 0;

		// get the property set interface for the port
		if (SUCCEEDED(m_pPort->QueryInterface(IID_IKsControl, (void**)&pControl)))
		{
			KSPROPERTY ksp;
			ULONG cb;

			// turn on reverb
			dwEffects = DMUS_EFFECT_REVERB;
			ZeroMemory(&ksp, sizeof(ksp));
			ksp.Set = GUID_DMUS_PROP_Effects;
			ksp.Id = 0;
			ksp.Flags = KSPROPERTY_TYPE_SET;
			pControl->KsProperty(&ksp, sizeof(ksp), (LPVOID)&dwEffects, sizeof(dwEffects), &cb);

			pControl->Release();

			return TRUE;
		}
	}

	return FALSE;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Disable reverb
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a830
LTBOOL CLTDirectMusicMgr::DisableReverb()
{
	// make sure we have a port
	if (m_pPort != LTNULL)
	{
		IKsControl* pControl;
		DWORD dwEffects = 0;

		// get the property set interface for the port
		if (SUCCEEDED(m_pPort->QueryInterface(IID_IKsControl, (void**)&pControl)))
		{
			KSPROPERTY ksp;
			ULONG cb;

			// get the current effects
			ZeroMemory(&ksp, sizeof(ksp));
			ksp.Set = GUID_DMUS_PROP_Effects;
			ksp.Id = 0;
			ksp.Flags = KSPROPERTY_TYPE_GET;
			pControl->KsProperty(&ksp, sizeof(ksp), (LPVOID)&dwEffects, sizeof(dwEffects), &cb);

			// turn off reverb
			ZeroMemory(&ksp, sizeof(ksp));
			dwEffects = dwEffects & ~DMUS_EFFECT_REVERB;
			ksp.Set = GUID_DMUS_PROP_Effects;
			ksp.Id = 0;
			ksp.Flags = KSPROPERTY_TYPE_SET;
			pControl->KsProperty(&ksp, sizeof(ksp), (LPVOID)&dwEffects, sizeof(dwEffects), &cb);

			pControl->Release();

			return TRUE;
		}
	}

	return FALSE;
};


///////////////////////////////////////////////////////////////////////////////////////////
// Initialize reverb parameters from control file and set up reverb in DirectMusic
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044a950
LTBOOL CLTDirectMusicMgr::InitReverb(CControlFileMgr& controlFile)
{
	// set reverb defaults
	m_bUseReverb = FALSE;
	m_ReverbParameters.fInGain = 0.0;
	m_ReverbParameters.fHighFreqRTRatio = -10.0;
	m_ReverbParameters.fReverbMix = 1000.0;
	m_ReverbParameters.fReverbTime = 0.001f;

	// check if reverb is on or off in control file
	{
		char sVal[5];
		controlFile.GetKeyVal(LTNULL, "REVERB", sVal, 4);
		if (stricmp(sVal,"ON") == 0) m_bUseReverb = TRUE;
	}

	// read in reverb parameters
	controlFile.GetKeyVal(LTNULL, "REVERBINGAIN", m_ReverbParameters.fInGain);
	controlFile.GetKeyVal(LTNULL, "REVERBHIGHFREQRTRATIO", m_ReverbParameters.fHighFreqRTRatio);
	controlFile.GetKeyVal(LTNULL, "REVERBMIX", m_ReverbParameters.fReverbMix);
	controlFile.GetKeyVal(LTNULL, "REVERBTIME", m_ReverbParameters.fReverbTime);

	// set up the reverb parameters
	SetReverbParameters(m_ReverbParameters);
	if (m_bUseReverb) EnableReverb();

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Terminate reverb if it was enabled.
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044aa60
LTBOOL CLTDirectMusicMgr::TermReverb()
{
	// check if reverb was on
	if (m_bUseReverb)
	{
		// Disable it if it was on
		return DisableReverb();
	}
	// we don't need to do anything if it wasn't on
	else return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// convert an enact type to a string for display (sName must be large enough no checking is done!)
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044aa80
void CLTDirectMusicMgr::EnactTypeToString(LTDMEnactTypes nType, char* sName)
{
	switch (nType)
	{
		case LTDMEnactInvalid :
		{
			strcpy(sName, "Invalid");
			break;
		}
		case LTDMEnactDefault :
		{
			strcpy(sName, "Default");
			break;
		}
		case LTDMEnactImmediately :
		{
			strcpy(sName, "Immediate");
			break;
		}
		case LTDMEnactNextBeat :
		{
			strcpy(sName, "Beat");
			break;
		}
		case LTDMEnactNextMeasure :
		{
			strcpy(sName, "Measure");
			break;
		}
		case LTDMEnactNextGrid :
		{
			strcpy(sName, "Grid");
			break;
		}
		case LTDMEnactNextSegment :
		{
			strcpy(sName, "Segment");
			break;
		}
		default :
		{
			strcpy(sName, "");
			break;
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////
// convert a string to an enact type
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044ab70
LTDMEnactTypes CLTDirectMusicMgr::StringToEnactType(const char* sName)
{
	if (sName == LTNULL) return LTDMEnactInvalid;
	if (stricmp(sName, "Invalid") == 0) return LTDMEnactInvalid;
	if (stricmp(sName, "Default") == 0) return LTDMEnactDefault;
	if (stricmp(sName, "Immediatly") == 0) return LTDMEnactImmediately;
	if (stricmp(sName, "Immediately") == 0) return LTDMEnactImmediately;
	if (stricmp(sName, "Immediate") == 0) return LTDMEnactImmediately;
	if (stricmp(sName, "NextBeat") == 0) return LTDMEnactNextBeat;
	if (stricmp(sName, "NextMeasure") == 0) return LTDMEnactNextMeasure;
	if (stricmp(sName, "NextGrid") == 0) return LTDMEnactNextGrid;
	if (stricmp(sName, "NextSegment") == 0) return LTDMEnactNextSegment;
	if (stricmp(sName, "Beat") == 0) return LTDMEnactNextBeat;
	if (stricmp(sName, "Measure") == 0) return LTDMEnactNextMeasure;
	if (stricmp(sName, "Grid") == 0) return LTDMEnactNextGrid;
	if (stricmp(sName, "Segment") == 0) return LTDMEnactNextSegment;
	return LTDMEnactInvalid;
}


// return the current intenisty
// FUNCTION: LITHTECH 0x0044ace0
int CLTDirectMusicMgr::GetCurIntensity()
{
	return m_nCurIntensity;
};


// return the number of intensities currently in this level (undefined if not in a level)
// FUNCTION: LITHTECH 0x0044acf0
int CLTDirectMusicMgr::GetNumIntensities()
{
	return m_nNumIntensities;
};


// return the initial intensity value for this level
// FUNCTION: LITHTECH 0x0044ad00
int CLTDirectMusicMgr::GetInitialIntensity()
{
	return m_nInitialIntensity;
};


// return the initial intensity value for this level
// FUNCTION: LITHTECH 0x0044ad10
int CLTDirectMusicMgr::GetInitialVolume()
{
	return m_nInitialVolume;
};


// return the volume offset.  This offset is applied to whatever volume is set.
// FUNCTION: LITHTECH 0x0044ad20
int CLTDirectMusicMgr::GetVolumeOffset()
{
	return m_nVolumeOffset;
};


///////////////////////////////////////////////////////////////////////////////////////////
// template and compiler-generated functions
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044ad30 ?GetFromFreeList@?$CLithChunkAllocator@VCCommandItem@CLTDirectMusicMgr@@@@AAEPAVCCommandItem@CLTDirectMusicMgr@@XZ
// FUNCTION: LITHTECH 0x0044ad40 ?AllocChunk@?$CLithChunkAllocator@VCCommandItem@CLTDirectMusicMgr@@@@AAEHXZ
// FUNCTION: LITHTECH 0x0044ade0 ?AllocChunk@?$CLithChunkAllocator@VCSegment@CLTDirectMusicMgr@@@@AAEHXZ
// FUNCTION: LITHTECH 0x0044aeb0 ?AllocChunk@?$CLithChunkAllocator@VCSegmentState@CLTDirectMusicMgr@@@@AAEHXZ
// FUNCTION: LITHTECH 0x0044af60 ?AllocChunk@?$CLithChunkAllocator@VCBand@CLTDirectMusicMgr@@@@AAEHXZ
// FUNCTION: LITHTECH 0x0044afe0 ?AllocChunk@?$CLithChunkAllocator@VCStyle@CLTDirectMusicMgr@@@@AAEHXZ
// FUNCTION: LITHTECH 0x0044b0a0 ?InsertFirst@?$CLithBaseList@VCChunk@?$CLithChunkAllocator@VCDLSBank@CLTDirectMusicMgr@@@@@@QAEXPAVCChunk@?$CLithChunkAllocator@VCDLSBank@CLTDirectMusicMgr@@@@@Z
// This identical Delete copy follows InsertFirst in the original COMDAT order.
// FUNCTION: LITHTECH 0x0044b0d0 ?Delete@?$CLithBaseList@VCChunk@?$CLithChunkAllocator@VCDLSBank@CLTDirectMusicMgr@@@@@@QAEXPAVCChunk@?$CLithChunkAllocator@VCDLSBank@CLTDirectMusicMgr@@@@@Z
