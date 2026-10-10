// Jupiter runtime/server/src/s_concommand.cpp
// FLAGS: /O2 /GX-
// Talon's console command set is older than Jupiter's (Portal, simpler objectinfo), and the
// server console state carries its CServerMgr in ConsoleState::m_Unknown38.
#include <stdlib.h>
#include <string.h>
#include "servermgr.h"
#include "dhashtable.h"
#include "packet.h"

extern LTEngineVar* GetEngineVars();
extern int GetNumEngineVars();

float time_GetTime();
void BPrint(const char *pMsg, ...);
LTRESULT sm_SetPortalFlags(CServerMgr *pServerMgr, const char *pPortalName, uint32 flags);
void sm_ClearAutoDeactivate(CServerMgr *pServerMgr);
void sm_SendToAllClients(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket, uint32 packetFlags);

#define SMSG_CONSOLEVAR					0x10
#define LOADWORLD_LOADWORLDOBJECTS		(1<<0)
#define LOADWORLD_RUNWORLD				(1<<1)
#define IFLAG_INACTIVE_MASK				0x38

// Each server console command can return a status by setting this.
// GLOBAL: LITHTECH 0x004e49f0
static LTRESULT g_CommandStatus;


// ------------------------------------------------------------------ //
// Command functions.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00473760
static void con_Portal(int argc, char *argv[])
{
	if (argc >= 2)
	{
		sm_SetPortalFlags(g_pServerMgr, argv[0], atoi(argv[1]));
	}
}

// FUNCTION: LITHTECH 0x00473790
static void con_ShowUsedFiles(int argc, char *argv[])
{
	HHashIterator *hIterator;
	HHashElement *hElement;

	if (g_pServerMgr)
	{
		hIterator = hs_GetFirstElement(g_pServerMgr->m_FileMgr.m_hFileTable);
		while (hIterator)
		{
			hElement = hs_GetNextElement(hIterator);
			BPrint("Used file: %s", (char*)hs_GetElementKey(hElement, LTNULL));
		}
	}
}

// FUNCTION: LITHTECH 0x004737e0
static void con_ShowGameVars(int argc, char *argv[])
{
	LTCommandVar *pCurVar;
	HHashIterator *hIterator;
	HHashElement *hElement;
	ConsoleState *pState;

	if (g_pServerMgr)
	{
		pState = &g_pServerMgr->m_ConsoleState;

		dsi_ConsolePrint("Game vars --------------------");
		hIterator = hs_GetFirstElement(pState->m_VarHash);
		while (hIterator)
		{
			hElement = hs_GetNextElement(hIterator);
			if (!hElement)
				continue;

			pCurVar = (LTCommandVar*)hs_GetElementUserData(hElement);
			cc_PrintVarDescription(pState, pCurVar);
		}
	}
}

// FUNCTION: LITHTECH 0x00473840
static void con_ServerWorld(int argc, char *argv[])
{
	uint32 flags;

	if (argc >= 1)
	{
		flags = LOADWORLD_LOADWORLDOBJECTS | LOADWORLD_RUNWORLD;
		if (argc >= 2 && !atoi(argv[1]))
			flags &= ~LOADWORLD_LOADWORLDOBJECTS;

		g_CommandStatus = g_pServerMgr->DoStartWorld(argv[0], flags, time_GetTime());
	}
}

static LTBOOL _FindClassInList(char *pClassName, char classNames[500][50], int nClassNames, int *pIndex);

// FUNCTION: LITHTECH 0x00473890
static void con_ObjectInfo(int argc, char *argv[])
{
	LTLink *pCur, *pListHead;
	LTObject *pObj;
	char classNames[500][50];
	int classCounts[500], nActiveClassCounts[500], totalClassBytes[500];
	int totalBytes, nClassNames, index, i;
	uint32 dwNumActiveObjects;

	// Count them up.
	nClassNames = 0;
	memset(classCounts, 0, sizeof(classCounts));
	memset(nActiveClassCounts, 0, sizeof(nActiveClassCounts));
	memset(totalClassBytes, 0, sizeof(totalClassBytes));
	dwNumActiveObjects = 0;

	pListHead = &g_pServerMgr->m_Objects.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObj = (LTObject*)pCur->m_pData;

		if (!_FindClassInList(pObj->sd->m_pClass->m_ClassName, classNames, nClassNames, &index))
		{
			if (nClassNames >= 500)
				continue;

			index = nClassNames;
			strncpy(classNames[nClassNames], pObj->sd->m_pClass->m_ClassName, 49);
			++nClassNames;
		}

		++classCounts[index];
		totalClassBytes[index] += pObj->sd->m_pClass->m_ClassObjectSize;

		if (!(pObj->m_InternalFlags & IFLAG_INACTIVE_MASK))
		{
			nActiveClassCounts[index]++;
			dwNumActiveObjects++;
		}
	}

	// Show the list.
	totalBytes = 0;
	for (i=0; i < nClassNames; i++)
	{
		totalBytes += totalClassBytes[i];
		dsi_ConsolePrint("%d (%d) instance(s) of '%s' (%d bytes)", classCounts[i],
			nActiveClassCounts[i], classNames[i], totalClassBytes[i]);
	}

	dsi_ConsolePrint("--- Total objects: %d (Active: %d) (Bytes: %d) ---",
		g_pServerMgr->m_Objects.m_nElements, dwNumActiveObjects, totalBytes);
}

// FUNCTION: LITHTECH 0x00473a10
static LTBOOL _FindClassInList(char *pClassName, char classNames[500][50], int nClassNames, int *pIndex)
{
	int i;

	for (i=0; i < nClassNames; i++)
	{
		if (strcmp(pClassName, classNames[i]) == 0)
		{
			*pIndex = i;
			return LTTRUE;
		}
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00473a80
static void con_DisableWMPhysics(int argc, char **argv)
{
	LTLink *pCur, *pListHead;
	LTObject *pObj;

	pListHead = &g_pServerMgr->m_Objects.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObj = (LTObject*)pCur->m_pData;
		pObj->m_Flags |= FLAG_BOXPHYSICS;
	}
}

// FUNCTION: LITHTECH 0x00473ac0
static void con_ExhaustMemory(int argc, char **argv)
{
}

// FUNCTION: LITHTECH 0x00473ad0
static void con_SpawnObject(int argc, char **argv)
{
	HCLASS hClass;
	LPBASECLASS pRet;
	ObjectCreateStruct theStruct;
	Client *pClient;
	char *pSpawnArgs;

	if (argc == 0 || !g_pServerMgr || !g_pServerMgr->m_pServerInterface ||
		g_pServerMgr->m_Clients.m_nElements == 0)
	{
		dsi_ConsolePrint("SpawnObject <class name> \"spawn args\"");
		return;
	}

	if (argc >= 2)
		pSpawnArgs = argv[1];
	else
		pSpawnArgs = "";

	hClass = g_pServerMgr->m_pServerInterface->GetClass(argv[0]);
	if (!hClass)
	{
		dsi_ConsolePrint("Can't find class %s", argv[0]);
		return;
	}

	// Put it near the first client.
	theStruct.Clear();

	pClient = (Client*)g_pServerMgr->m_Clients.m_Head.m_pNext->m_pData;
	theStruct.m_Pos = pClient->m_ViewPos;
	theStruct.m_Pos.z += 200.0f;

	pRet = g_pServerMgr->m_pServerInterface->CreateObjectProps(hClass, &theStruct, pSpawnArgs);
	if (pRet)
	{
		dsi_ConsolePrint("%s spawned successfully", argv[0]);
	}
	else
	{
		dsi_ConsolePrint("Error in CreateObjectProps");
	}
}


// ------------------------------------------------------------------ //
// Tables.
// ------------------------------------------------------------------ //

// GLOBAL: LITHTECH 0x004d57a8
static LTCommandStruct g_ServerCommandStructs[] =
{
	{ "Portal", con_Portal, 0 },
	{ "ShowGameVars", con_ShowGameVars, 0 },
	{ "ShowUsedFiles", con_ShowUsedFiles, 0 },
	{ "world", con_ServerWorld, 0 },
	{ "objectinfo", con_ObjectInfo, 0 },
	{ "DisableWMPhysics", con_DisableWMPhysics, 0 },
	{ "ExhaustMemory", con_ExhaustMemory, 0 },
	{ "SpawnObject", con_SpawnObject, 0 },
};

#define NUM_SERVERCOMMANDSTRUCTS	(sizeof(g_ServerCommandStructs) / sizeof(LTCommandStruct))


// Takes a reference of our own on a packet.
inline CPacket* sm_AddRef(const CPacketRef &cPacketRef)
{
	CPacket *pPacket = cPacketRef.m_pPacket;

	if (pPacket)
		pPacket->AddRef();

	return pPacket;
}


// Console callbacks.
// FUNCTION: LITHTECH 0x00473d40
void sm_NewVar(ConsoleState *pState, LTCommandVar *pVar)
{
	CPacket *pPacket;

	if (!pState->m_Unknown38)
		return;

	pPacket = sm_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	if (stricmp(pVar->pVarName, "AutoDeactivate") == 0)
	{
		sm_ClearAutoDeactivate(g_pServerMgr);
	}

	pPacket->m_Pos = 1;
	pPacket->m_DataLen = 1;
	pPacket->WriteString(pVar->pVarName);
	pPacket->WriteString(pVar->pStringVal);
	sm_SendToAllClients((CServerMgr*)pState->m_Unknown38, SMSG_CONSOLEVAR, pPacket, MESSAGE_GUARANTEED);
	pPacket->Release();
}

// FUNCTION: LITHTECH 0x00473df0
void sm_VarChange(ConsoleState *pState, LTCommandVar *pVar)
{
	sm_NewVar(pState, pVar);
}


// ------------------------------------------------------------------ //
// Interface functions.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00473e00
void sm_InitConsoleCommands(CServerMgr *pServerMgr, ConsoleState *pState)
{
	memset(pState, 0, sizeof(ConsoleState));

	pState->m_SaveFns = LTNULL;
	pState->m_nSaveFns = 0;

	pState->m_pEngineVars = GetEngineVars();
	pState->m_nEngineVars = GetNumEngineVars();

	pState->m_pCommandStructs = g_ServerCommandStructs;
	pState->m_nCommandStructs = NUM_SERVERCOMMANDSTRUCTS;

	pState->ConsolePrint = dsi_ConsolePrint;

	pState->Alloc = operator new;
	pState->Free = operator delete;

	pState->NewVar = sm_NewVar;
	pState->VarChange = sm_VarChange;
	pState->m_Unknown38 = (uint32)pServerMgr;

	cc_InitState(pState);
}

// FUNCTION: LITHTECH 0x00473e70
void sm_TermConsoleCommands(ConsoleState *pState)
{
	cc_TermState(pState);
}

// FUNCTION: LITHTECH 0x00473e80
LTRESULT sm_HandleCommand(ConsoleState *pState, char *pCommand)
{
	g_CommandStatus = LT_OK;
	cc_HandleCommand(pState, pCommand);
	return g_CommandStatus;
}
