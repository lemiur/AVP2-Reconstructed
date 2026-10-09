// Jupiter runtime/server/src/servermgr.cpp
// Talon's server manager functions mostly take the CServerMgr explicitly. The unit also holds
// two STLport containers (static initializers at 0x00484720 and 0x00484830) we can't build.
// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC /IE:/AVP2Source/build/proj/LT2/lithshared/wonapi
#include <winsock2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <map>
#include <set>
#include <string>
#include <windows.h>
#undef PlaySound
#include "bdefs.h"
#include "ltengineobjects.h"
#include "servermgr.h"
#include "s_object.h"
#include "smoveabstract.h"
#include "soundtrack.h"
#include "serverevent.h"
#include "interlink.h"
#include "dhashtable.h"
#include "de_memory.h"
#include "model.h"
#include "objectmgr.h"
#include "stringmgr.h"
#include "sloaderthread.h"
#include "sserializehelper.h"
#include "packet.h"
#include "s_client.h"
#include "server_extradata.h"
#include "fullintersectline.h"
#include "WONAuth/AuthContext.h"
#include "WONAuth/PeerAuthServer.h"
#include "engine_vars.h"
#include "server_vars.h"
#include "moveobject.h"
#include "serverde_impl.h"

#define SMSG_PEERAUTH		27



LTRESULT sm_CreateNewID(CServerMgr *pServerMgr, LTLink **ppID);
LTRESULT sm_RemoveObjectFromWorld(CServerMgr *pServerMgr, LPBASECLASS pObject);
LTRESULT sm_AddObjectToWorld(CServerMgr *pServerMgr, LPBASECLASS pObject, ClassDef *pClass,
	ObjectCreateStruct *pStruct, uint16 objectID, uint32 initialUpdateCode, LTObject **ppOut);
void sm_FreeObjectOfClass(CServerMgr *pServerMgr, LTObject *pObj);
LTRESULT sm_SetupError(CServerMgr *pServerMgr, LTRESULT theError, ...);
void sm_UpdateClientState(CServerMgr *pServerMgr, Client *pClient);
void sm_UpdateClientInWorld(CServerMgr *pServerMgr, Client *pClient);
void sm_RemoveAllSoundInstances(CServerMgr *pServerMgr);
LTRESULT dsi_SetupMessage(char *pMsg, int maxMsgLen, LTRESULT dResult, va_list marker);
LTRESULT LoadServerBinaries(CClassMgr *pClassMgr);
void sm_InitConsoleCommands(CServerMgr *pServerMgr, ConsoleState *pState);
void sm_TermConsoleCommands(ConsoleState *pState);
ILTServer* CreateLTServer(CServerMgr *pServerMgr);		// 0x004798b0
void InitServerNetHandlers();							// 0x00476800
void sm_RemoveClient(CServerMgr *pServerMgr, Client *pClient);
void sm_RemoveAllSounds(CServerMgr *pServerMgr);
void sm_FreeAllModels(CServerMgr *pServerMgr);			// s_object, 0x00476ff0
void sm_RemoveAllObjectsFromWorld(CServerMgr *pServerMgr, LTBOOL bRemoveStaticObjects);
static void _ServerStringWhine(const char *pString, void *pUser);
// An empty function (folded with every other empty void function at 0x00473ac0).
void sm_TermDebug();

// Loader thread message types (LThreadMessage::m_Data[0]).
#define LOADERMSG_FILELOADED	0

#define UPDATEINFO_NUMSTART		200
#define SERV_RUNNINGWORLD		0
#define SERV_NOSTATE			1
#define CC_NOCOMMANDS			(1<<1)

// GLOBAL: LITHTECH 0x004e5d9c
uint32 g_ObjectMemory;

// GLOBAL: LITHTECH 0x004e5da0
uint32 g_SphereFindTicks;
// GLOBAL: LITHTECH 0x004e5db0
uint32 g_SphereFindCount;
// GLOBAL: LITHTECH 0x004e5db4
uint32 g_PolyFindCount;
// GLOBAL: LITHTECH 0x004e5dc4
uint32 g_PolyFindTicks;



#define MIN_FRAMETIME		0.01f
#define MAX_FRAMETIME		0.2f

// Update flags.
#define UPDATEFLAG_NONACTIVE	(1<<0)

// m_ServerFlags.
#define SFLAG_PAUSED			(1<<0)

// m_InternalFlags debug frame stepping.
#define SIFLAG_FRAMESTEP		(1<<2)
#define SIFLAG_FRAMESTEPRUN		(1<<3)


void sm_RemoveAllUnusedSoundData(CServerMgr *pServerMgr);
LTRESULT sm_CacheFile(CServerMgr *pServerMgr, uint32 fileType, char *pFilename);
void sm_SendFileIOMessage(CServerMgr *pServerMgr, uint8 fileType, uint8 msgID, uint16 fileID, LTBOOL bTellLocal);
void sm_ResetDeactivateTimer(LTObject *pObj);
void ic_FreeFileList(FileEntry *pList);
LTRESULT sm_InitExtraData(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct);
LTRESULT sm_TermExtraData(CServerMgr *pServerMgr, LTObject *pObject);
void InitialWorldModelRotate(WorldModelInstance *pInstance);		// 0x0045d570
void DetachObjectStanding(LTObject *pObj);						// 0x0045d110
void DetachObjectsStandingOn(LTObject *pObj);					// 0x0045d150
void w_RemoveObjectFromLeaf(LTObject *pObj);					// 0x00430680

#define CF_KILLSOUNDLOOP		(1<<5)
#define CFLAG_SENDGLOBALLIGHT	(1<<10)

#define IFLAG_HASCLIENTREF	(1<<7)

#define MAX_OBJECTIDS				0xfff
#define CLIENT_WAITINGTOENTERWORLD	1
void s_DisassociateClientsFromObjects(CServerMgr *pServerMgr);
void sm_RemoveAllUnusedSoundTracks(CServerMgr *pServerMgr);
void sm_UncacheModels(CServerMgr *pServerMgr);
void sm_UpdateObject(CServerMgr *pServerMgr, LTObject *pObj);		// s_object, 0x00477120


#define IFLAG_INACTIVE_MASK		0x38
#define IFLAG_DEACTIVATENOW		(1<<9)
float time_GetTime();									// 0x0049c130
void model_SetWorldName(char *pWorldName);				// 0x004166f0
void sm_LoadChildModelLinks(ILTServer *pServer, char *pWorldName);	// 0x004848f0 (STLport)
LTRESULT sm_StartCachingFiles(CServerMgr *pServerMgr, LTBOOL bBuildCacheList);
LTRESULT LoadObjects(CServerMgr *pServerMgr, ILTStream *pStream, char *pWorldName, LTBOOL bAllObjects);	// s_object

#define LOADWORLD_LOADWORLDOBJECTS	(1<<0)
#define LOADWORLD_RUNWORLD			(1<<1)
#define LOADWORLD_KEEPGEOMETRY		(1<<2)
#define SMSG_CONSOLETEXT	0xd
#define PACKETFLAG_MESSAGE	(1<<0)	// CPacket::m_ErrorFlags: allocated for a game message
void model_FreeUnusedChildModels();					// 0x00416820
void* dsi_GetLoadUser();							// 0x00416740
void dsi_LoadProgress(uint32 percent);				// 0x00416800
void sm_CacheSingleFile(CServerMgr *pServerMgr, uint16 fileType, uint16 fileID);


#define FT_SOUND			3
#define SMSG_PORTALFLAGS	0x13
#define IFLAG_INSKY			(1<<8)

#define UPDATEINFO_CACHESIZE	40
#define SFLAG_BUILDINGCACHELIST	(1<<4)
#define MODELFLAG_CACHED		(1<<0)

// File types (de_codes.h).
#define FT_MODEL		0
#define FT_SPRITE		1
#define FT_TEXTURE		2

#define FILEIO_UNLOAD	0x18

// Inactive object flags (LTObject::m_InternalFlags).
#define IFLAG_INACTIVE_TICK_MASK	0x18
#define IFLAG_AUTODEACTIVATED		(1<<5)	// serverde_impl.h; bit 3 is IFLAG_INACTIVE



// ----------------------------------------------------------------------- //
// The server's WONAPI auth context (released by Init and Term). The WONAPI headers (via <string>)
// use up 28 static initializer numbers first.
// ----------------------------------------------------------------------- //

using namespace WONAPI;

// FUNCTION: LITHTECH 0x004821c0 _$E32
// FUNCTION: LITHTECH 0x004821d0 _$E29
// FUNCTION: LITHTECH 0x004821e0 _$E31
// FUNCTION: LITHTECH 0x004821f0 _$E30
// GLOBAL: LITHTECH 0x004e5d98
// Extern, not static: CServerMgr::Init's release of it only matches that way (the name is unknown; the
// client's g_pAuthContext is a different global).
SmartPtr<AuthContext> g_pServerAuthContext;


// ----------------------------------------------------------------------- //
// IDs.
// ----------------------------------------------------------------------- //

// Allocates a new ID for the object.
// FUNCTION: LITHTECH 0x00482220
LTRESULT sm_AllocateID(CServerMgr *pServerMgr, LTLink **ppIDLink, uint16 objectID)
{
	LTLink *pListHead, *pCur;
	LTRESULT dResult;
	ObjInfo *pInfo;

	if (objectID == INVALID_OBJECTID)
	{
		// Look for an unblocked ID.
		pListHead = &pServerMgr->m_FreeIDs;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			*ppIDLink = pCur;
			if (!(GetLinkID(*ppIDLink) & IDFLAG_BLOCKEDID))
				goto FoundOne;
		}

		// If if didn't find one, create a new object
		if ((dResult = sm_CreateNewID(pServerMgr, ppIDLink)) != LT_OK)
			return dResult;
	}
	else
	{
		// If objects up to this ID haven't been created yet, generate them.
		while (objectID >= pServerMgr->m_nAllocatedIDs)
		{
			if ((dResult = sm_CreateNewID(pServerMgr, ppIDLink)) != LT_OK)
				return dResult;
		}

		*ppIDLink = sm_FindInFreeList(pServerMgr, objectID);
		if (!*ppIDLink)
		{
			RETURN_ERROR(1, sm_AllocateID, LT_ERROR);
		}
	}

FoundOne:;
	// Unblock the ID.
	SetLinkID(*ppIDLink, GetLinkID(*ppIDLink) & ~IDFLAG_BLOCKEDID);

	// Take it out of the free list and put it in the allocated list.
	dl_Remove(*ppIDLink);
	dl_Insert(&pServerMgr->m_IDs, *ppIDLink);

	// Resize the update infos if necessary.
	if (pServerMgr->m_nAllocatedIDs > pServerMgr->m_nObjInfos)
		pServerMgr->ResizeUpdateInfos(pServerMgr->m_nAllocatedIDs);

	// Clear its object info flags.
	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pInfo = &((Client*)pCur->m_pData)->m_ObjInfos[GetLinkID(*ppIDLink)];
		pInfo->m_ChangeFlags = 0;
		pInfo->m_nSoundFlags = 0;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00482380
void sm_FreeID(CServerMgr *pServerMgr, LTLink *pIDLink)
{
	// Remove from its current list.
	dl_Remove(pIDLink);

	// Put it in the free list.
	dl_Insert(&pServerMgr->m_FreeIDs, pIDLink);
}


// ----------------------------------------------------------------------- //
// Init/term.
// ----------------------------------------------------------------------- //

// The auth context global is extern (g_pServerAuthContext): as a file static the release at the end computed
// &m_RefCount from eax instead of the esi copy.
// FUNCTION: LITHTECH 0x004823b0
LTBOOL CServerMgr::Init()
{
	SetupGlobals();

	m_pSerializeHelper = new CServerSerializeHelper(this);
	m_pDefaultModel = new Model(&g_DefAlloc, &g_DefAlloc);
	m_MoveAbstract = new SMoveAbstract(this);

	if ((om_Init(&m_ObjectMgr, LTFALSE) != LT_OK) ||
		!m_pSerializeHelper || !m_pDefaultModel || !m_MoveAbstract)
	{
		return LTFALSE;
	}

	// Start the loader thread.
	if (!m_LoaderThread.Init(this))
		return LTFALSE;

	if (m_LoaderThread.Start(3) != LT_OK)
		return LTFALSE;

	m_World.m_WorldTree.InitWorldTree(&m_ObjectMgr);

	m_NetMgr.Init("SERVER_PLAYER");
	m_NetMgr.m_pHandler = this;

	dl_TieOff(&m_RemovedObjectHead);
	m_pServerAppHandler = LTNULL;
	m_pCurProps = LTNULL;
	m_ErrorString[0] = 0;
	m_ObjectMap.SetCacheSize(100);
	m_CacheList = LTNULL;
	m_CacheListSize = m_CacheListAllocedSize = 0;

	memset(m_SkyObjects, 0xFF, sizeof(m_SkyObjects));

	m_pGlobalLightObject = LTNULL;

	m_nObjInfos = UPDATEINFO_NUMSTART;
	m_nAllocatedIDs = 0;

	m_pGameInfo = LTNULL;
	m_GameInfoLen = 0;

	m_State = SERV_NOSTATE;
	m_ServerFlags = 0;
	m_InternalFlags = 0;

	g_ObjectMemory = 0;

	m_UpdatesSize = m_nUpdatesSent = 0;

	// Init lists and structure banks and stuff.
	sb_Init2(&m_ObjectListBank, 8, 16, 4);
	sb_Init2(&m_ObjectLinkBank, 8, 128, 64);
	sb_Init2(&m_InterLinkBank, sizeof(InterLink), 32, 32);
	sb_Init2(&m_ServerEventBank, 0x164, 48, 32);
	sb_Init2(&m_ClientStructNodeBank, 0x18, 32, 16);
	sb_Init2(&m_SoundDataBank, sizeof(CSoundData), 16, 16);
	sb_Init2(&m_SoundTrackBank, sizeof(CSoundTrack), 16, 16);
	sb_Init2(&m_FileIDInfoBank, 0xc0, 16, 16);
	sb_Init2(&m_BankCAC, 0xa, 32, 32);

	m_SObjBank.SetCacheSize(64);

	dl_TieOff(&m_FreeIDs);
	dl_TieOff(&m_IDs);
	dl_InitList(&m_Objects);
	dl_InitList(&m_Clients);
	dl_InitList(&m_ClientReferences);
	dl_InitList(&m_SoundDataList);
	dl_InitList(&m_SoundTrackList);

	m_SkyDef.m_Min.Init(-10.0f, -10.0f, -10.0f);
	m_SkyDef.m_Max.Init(10.0f, 10.0f, 10.0f);
	m_SkyDef.m_ViewMin.Init();
	m_SkyDef.m_ViewMax.Init();

	m_bTrackChanges = LTFALSE;
	m_pWorldFile = LTNULL;
	m_CRCString[0] = 0;
	m_StringCRC = 0;
	m_WorldCRC = 0;
	m_pTracePacketFile = LTNULL;

	m_FrameTime = 0.0f;
	m_LastServerFPS = 0.0f;
	m_TargetTimeBase = 0.0f;
	m_TargetTime = 0.0f;
	m_nTargetTimeSteps = 0;
	m_LastTargetTimeBase = 0.0f;
	m_GameTime = 0.0f;
	m_FrameCode = 0;
	m_TrueFrameTime = 0.0f;
	m_LastTime = 0.0f;
	m_TimeOffset = 0.0f;

	m_hNameTable = hs_CreateHashTable(500, HASH_STRING_NOCASE);
	m_hModelTable = hs_CreateHashTable(100, HASH_STRING_NOCASE);

	sm_InitConsoleCommands(this, &m_ConsoleState);
	cc_RunConfigFile(&m_ConsoleState, "s_autoexec.cfg", CC_NOCOMMANDS, 0);

	m_pServerInterface = CreateLTServer(this);

	sf_Init(&m_FileMgr, this);

	m_ClassMgr.Init(this);
	m_StringHolder.SetAllocSize(300);

	m_pCollisionInfo = LTNULL;

	InitServerNetHandlers();

	g_pServerAuthContext = (AuthContext*)LTNULL;
	return LTTRUE;
}

// SMoveAbstract::GetPhysics, emitted with the SMoveAbstract vtable here.
// FUNCTION: LITHTECH 0x004827d0 ?GetPhysics@SMoveAbstract@@UAEPAVILTPhysics@@XZ

// FUNCTION: LITHTECH 0x004827e0
void CServerMgr::Term()
{
	LTLink *pListHead, *pCur, *pNext;

	g_pServerAuthContext = (AuthContext*)LTNULL;
	SetupGlobals();

	// Stop loading.
	m_LoaderThread.Terminate(LTTRUE);
	ProcessLoaderMessages();

	// Shutdown the world if it's running.
	DoEndWorld(LTFALSE);

	// Free game info.
	dfree(m_pGameInfo);
	m_pGameInfo = LTNULL;
	m_GameInfoLen = 0;

	// Remove all the clients.
	pListHead = &m_Clients.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		sm_RemoveClient(this, (Client*)pCur->m_pData);
		pCur = pNext;
	}
	dl_InitList(&m_Clients);

	m_NetMgr.Term();

	// All the objects better be cleared out.
	sm_RemoveAllObjectsFromWorld(this, LTTRUE);

	// Delete the sound data and instances
	sm_RemoveAllSounds(this);

	// Free all the IDs.
	for (pCur=m_FreeIDs.m_pNext; pCur != &m_FreeIDs; pCur=pNext)
	{
		pNext = pCur->m_pNext;
		((StructLink*)pCur)->m_pSLNext = g_DLinkBank.m_Bank.m_FreeListHead;
		g_DLinkBank.m_Bank.m_FreeListHead = (StructLink*)pCur;
	}
	dl_TieOff(&m_FreeIDs);

	m_nAllocatedIDs = 0;
	m_ObjectMap.Term();

	// Destroy the world.
	m_World.Term();
	sm_FreeAllModels(this);
	hs_DestroyHashTable(m_hModelTable);
	m_hModelTable = LTNULL;

	m_ClassMgr.Term();
	sm_TermDebug();
	sm_TermConsoleCommands(&m_ConsoleState);

	// Whine about all the strings they didn't free.
	str_ShowAllStringsAllocated(_ServerStringWhine, LTNULL);

	// Get rid of the file trees.
	sf_Term(&m_FileMgr);

	// Get rid of the struct banks.
	sb_Term(&m_ObjectListBank);
	sb_Term(&m_ObjectLinkBank);
	sb_Term(&m_InterLinkBank);
	sb_Term(&m_ServerEventBank);
	sb_Term(&m_ClientStructNodeBank);
	sb_Term(&m_SoundDataBank);
	sb_Term(&m_SoundTrackBank);
	sb_Term(&m_FileIDInfoBank);
	sb_Term(&m_BankCAC);

	m_SObjBank.Term();

	hs_DestroyHashTable(m_hNameTable);
	m_hNameTable = LTNULL;

	// Free the sprite list.
	if (m_CacheList)
	{
		dfree(m_CacheList);
		m_CacheList = LTNULL;
	}
	m_CacheListSize = m_CacheListAllocedSize = 0;

	// Shut down the object manager.
	om_Term(&m_ObjectMgr);

	if (m_MoveAbstract)
	{
		delete m_MoveAbstract;
		m_MoveAbstract = LTNULL;
	}

	if (m_pServerInterface)
	{
		delete m_pServerInterface;
		m_pServerInterface = LTNULL;
	}

	if (m_pTracePacketFile)
	{
		m_pTracePacketFile->Release();
		m_pTracePacketFile = LTNULL;
	}

	if (m_pSerializeHelper)
	{
		delete m_pSerializeHelper;
		m_pSerializeHelper = LTNULL;
	}

	if (m_pDefaultModel)
	{
		delete m_pDefaultModel;
		m_pDefaultModel = LTNULL;
	}

	m_pServerAppHandler = LTNULL;
	g_pServerMgr = LTNULL;
	g_pClassMgr = LTNULL;
}

// FUNCTION: LITHTECH 0x00482ab0
void sm_RemoveAllObjectsFromWorld(CServerMgr *pServerMgr, LTBOOL bRemoveStaticObjects)
{
	LTObject *pObj;

	pServerMgr->m_InternalFlags |= SIFLAG_REMOVINGALLOBJECTS;

	// Get rid of the ones that were already going away.
	sm_RemoveObjectsThatNeedToGetRemoved(pServerMgr);

	while (pServerMgr->m_Objects.m_Head.m_pNext != &pServerMgr->m_Objects.m_Head)
	{
		pObj = (LTObject*)pServerMgr->m_Objects.m_Head.m_pNext->m_pData;

		if (bRemoveStaticObjects)
		{
			sm_RemoveObjectFromWorld(pServerMgr, pObj->sd->m_pObject);
		}
		else if (!(pObj->sd->m_pClass->m_ClassFlags & CF_STATIC))
		{
			sm_RemoveObjectFromWorld(pServerMgr, pObj->sd->m_pObject);
		}
	}

	pServerMgr->m_InternalFlags &= ~SIFLAG_REMOVINGALLOBJECTS;
}

// FUNCTION: LITHTECH 0x00482b20
static void _ServerStringWhine(const char *pString, void *pUser)
{
	DEBUG_PRINT(1, ("Unfreed (server) string: %s", pString));
}

// FUNCTION: LITHTECH 0x00482b40
LTBOOL CServerMgr::Listen(char *pDriverInfo, char *pListenInfo)
{
	SetupGlobals();

	if (!m_NetMgr.AddDriver(pDriverInfo))
	{
		sm_SetupError(this, LT_ERRORINITTINGNETDRIVER, pDriverInfo);
		return LTFALSE;
	}

	dsi_ConsolePrint("Listening on driver: %s", pDriverInfo);
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00482b90
LTBOOL CServerMgr::TransferNetDriver(CBaseDriver *pDriver)
{
	uint32 index;

	index = pDriver->m_pNetMgr->m_Drivers.FindElement(pDriver);
	if (index != BAD_INDEX)
		pDriver->m_pNetMgr->m_Drivers.Remove(index);

	m_NetMgr.m_Drivers.Append(pDriver);
	m_NetMgr.SetMainDriver(pDriver);
	pDriver->m_pNetMgr = &m_NetMgr;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00482cf0
LTBOOL CServerMgr::AddResources(char **pResources, uint32 nResources)
{
	TreeType treeTypes[50];
	int nTreesLoaded;

	SetupGlobals();

	if (nResources == 0)
	{
		sm_SetupError(this, LT_NOGAMERESOURCES);
		return LTFALSE;
	}

	sf_AddResources(&m_FileMgr, (const char**)pResources, nResources, treeTypes, &nTreesLoaded);
	if (nTreesLoaded == 0)
	{
		sm_SetupError(this, LT_CANTLOADGAMERESOURCES, pResources[0]);
		return LTFALSE;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00482d80
LTBOOL CServerMgr::LoadBinaries()
{
	SetupGlobals();
	return LoadServerBinaries(&m_ClassMgr) == LT_OK;
}

// FUNCTION: LITHTECH 0x00482da0
void CServerMgr::SetGameInfo(void *pData, uint32 dataLen)
{
	dfree(m_pGameInfo);

	m_pGameInfo = dalloc(dataLen);
	if (m_pGameInfo)
	{
		memcpy(m_pGameInfo, pData, dataLen);
		m_GameInfoLen = dataLen;
	}
}


// ----------------------------------------------------------------------- //
// Updating.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00482df0
void sm_ClearChangedObjectList(CServerMgr *pServerMgr)
{
	LTObject *pObj;

	for (pObj=pServerMgr->m_pChangeListHead; pObj; pObj=pObj->sd->m_pChangeNext)
	{
		pObj->sd->m_ChangeFlags = 0;
	}

	pServerMgr->m_pChangeListHead = LTNULL;
}

// FUNCTION: LITHTECH 0x00482e20
void sm_ClearChangedSoundTrackList(CServerMgr *pServerMgr)
{
	CSoundTrack *pSoundTrack;

	for (pSoundTrack=pServerMgr->m_ChangedSoundTrackHead; pSoundTrack; pSoundTrack=pSoundTrack->m_pChangedNext)
	{
		pSoundTrack->m_wChangeFlags = 0;
	}

	pServerMgr->m_ChangedSoundTrackHead = LTNULL;
}

// Updates the active objects (they're at the front of m_Objects) and the class tick counts.
// FUNCTION: LITHTECH 0x00482e50
void CServerMgr::UpdateObjects()
{
	LTLink *pCur, *pListHead;
	LTObject *pObj;
	CClassData *pClassData, *pCurClass;
	int i, j;
	uint32 nTotalObjects, nTotalUpdated, nAve1, nAve2, len;
	char className[32];

	// Reset the per-frame class counts.
	for (i=0; i < m_ClassMgr.m_nClassDatas; i++)
	{
		m_ClassMgr.m_ClassDatas[i].m_nTicksThisUpdate = 0;
		m_ClassMgr.m_ClassDatas[i].m_nUpdated = 0;
		m_ClassMgr.m_ClassDatas[i].m_nTotal = 0;
		m_ClassMgr.m_ClassDatas[i].m_bDisplayedTicks = LTFALSE;

		if (m_ClassMgr.m_ClassDatas[i].m_nTickAveCnt >= 600)
		{
			m_ClassMgr.m_ClassDatas[i].m_nTickAveCnt = 0;
			m_ClassMgr.m_ClassDatas[i].m_nTotalTicks = 0;
			m_ClassMgr.m_ClassDatas[i].m_nMaxTicks = 0;
		}
	}

	// Update the objects (the inactive ones are at the end of the list).
	pListHead = &m_Objects.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pObj = (LTObject*)pCur->m_pData;
		pCur = pCur->m_pNext;

		if (pObj->m_InternalFlags & IFLAG_INACTIVE_MASK)
			break;

		if (!(pObj->m_InternalFlags & IFLAG_INWORLD))
			continue;

		// Count down to auto-deactivation.
		if (g_bAutoDeactivate && pObj->sd->m_fDeactivateTimer > 0.0f)
		{
			pObj->sd->m_fDeactivateTimer -= m_FrameTime;
			if (pObj->sd->m_fDeactivateTimer <= 0.0f)
				pObj->m_InternalFlags |= IFLAG_DEACTIVATENOW;
		}

		pClassData = (CClassData*)pObj->sd->m_pClass->m_pInternal[m_ClassMgr.m_ClassIndex];
		sm_UpdateObject(this, pObj);
		pClassData->m_nUpdated++;
	}

	if (g_CV_ShowClassTicks)
	{
		nTotalObjects = 0;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pObj = (LTObject*)pCur->m_pData;
			pClassData = (CClassData*)pObj->sd->m_pClass->m_pInternal[m_ClassMgr.m_ClassIndex];
			pClassData->m_nTotal++;
			nTotalObjects++;
		}

		for (i=0; i < m_ClassMgr.m_nClassDatas; i++)
		{
			pClassData = &m_ClassMgr.m_ClassDatas[i];
			pClassData->m_nTickAveCnt++;
			pClassData->m_nTotalTicks += pClassData->m_nTicksThisUpdate;
			if (pClassData->m_nMaxTicks < pClassData->m_nTicksThisUpdate)
				pClassData->m_nMaxTicks = pClassData->m_nTicksThisUpdate;
		}

		nTotalUpdated = 0;
		g_Ticks_ClassUpdate = 0;

		dsi_ConsolePrint("------------------------- Class Tick Counts -------------------------------------");
		dsi_ConsolePrint("Class                (OBJECTS) Total Updated   (TICKS) Average    Max     Frame");
		dsi_ConsolePrint("---------------------------------------------------------------------------------");

		// Display the classes, lowest average ticks first.
		for (j=0; j < m_ClassMgr.m_nClassDatas; j++)
		{
			pCurClass = LTNULL;

			for (i=0; i < m_ClassMgr.m_nClassDatas; i++)
			{
				pClassData = &m_ClassMgr.m_ClassDatas[i];

				if (!pClassData->m_bDisplayedTicks)
				{
					if (pClassData->m_nTotalTicks)
					{
						if (!pCurClass)
							pCurClass = pClassData;

						nAve1 = pCurClass->m_nTotalTicks / pCurClass->m_nTickAveCnt;
						nAve2 = pClassData->m_nTotalTicks / pClassData->m_nTickAveCnt;
						if (nAve2 <= nAve1)
							pCurClass = pClassData;
					}
					else
					{
						pClassData->m_bDisplayedTicks = LTTRUE;
					}
				}
			}

			if (pCurClass)
			{
				pCurClass->m_bDisplayedTicks = LTTRUE;

				strncpy(className, pCurClass->m_pClass->m_ClassName, sizeof(className));
				className[sizeof(className)-1] = 0;

				for (len=strlen(className); len < 30; len++)
					className[len] = ' ';

				dsi_ConsolePrint("%s| %3d |  %3d  |           %6d | %6d | %6d", className,
					pCurClass->m_nTotal, pCurClass->m_nUpdated,
					pCurClass->m_nTotalTicks / pCurClass->m_nTickAveCnt,
					pCurClass->m_nMaxTicks, pCurClass->m_nTicksThisUpdate);

				g_Ticks_ClassUpdate += pCurClass->m_nTicksThisUpdate;
				nTotalUpdated += pCurClass->m_nUpdated;
			}
		}

		dsi_ConsolePrint(" ");
		dsi_ConsolePrint("FRAME TOTALS:");
		dsi_ConsolePrint("  (OBJECTS) Total: %d, Updated: %d,  (TICKS) %d", nTotalObjects, nTotalUpdated, g_Ticks_ClassUpdate);
		dsi_ConsolePrint("---------------------------------------------------------------------------------");
		dsi_ConsolePrint(" ");
	}
	else
	{
		g_Ticks_ClassUpdate = 0;
		for (i=0; i < m_ClassMgr.m_nClassDatas; i++)
		{
			g_Ticks_ClassUpdate += m_ClassMgr.m_ClassDatas[i].m_nTicksThisUpdate;
			m_ClassMgr.m_ClassDatas[i].m_nTickAveCnt = 0;
			m_ClassMgr.m_ClassDatas[i].m_nTotalTicks = 0;
		}
	}
}

void sm_UpdateClientStates(CServerMgr *pServerMgr);
void sm_UpdateClientsInWorld(CServerMgr *pServerMgr);
void sm_MergeClientChangeLists(CServerMgr *pServerMgr);

// FUNCTION: LITHTECH 0x00483200
void sm_FinishUpdateFrame(CServerMgr *pServerMgr)
{
	sm_UpdateClientStates(pServerMgr);

	// Merge the change lists into the client lists.
	sm_MergeClientChangeLists(pServerMgr);

	// Remove the objects that were waiting to go away.
	while (pServerMgr->m_RemovedObjectHead.m_pNext != &pServerMgr->m_RemovedObjectHead)
	{
		sm_RemoveObjectsThatNeedToGetRemoved(pServerMgr);
	}

	pServerMgr->m_pChangeListHead = LTNULL;
	pServerMgr->m_ChangedSoundTrackHead = LTNULL;

	sm_UpdateClientsInWorld(pServerMgr);
}

// FUNCTION: LITHTECH 0x00483260
void sm_UpdateClientStates(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pListHead;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		sm_UpdateClientState(pServerMgr, (Client*)pCur->m_pData);
	}
}

// FUNCTION: LITHTECH 0x00483290
void sm_UpdateClientsInWorld(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pListHead;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		sm_UpdateClientInWorld(pServerMgr, (Client*)pCur->m_pData);
	}

	pServerMgr->m_nSendPackets = 0;
	pServerMgr->m_nDroppedSendPackets = 0;
}

// FUNCTION: LITHTECH 0x004832d0
void sm_MergeClientChangeLists(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pListHead;
	Client *pClient;
	LTObject *pObj;
	CSoundTrack *pSoundTrack;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (pClient->m_State == CLIENT_INWORLD)
		{
			for (pObj=pServerMgr->m_pChangeListHead; pObj; pObj=pObj->sd->m_pChangeNext)
			{
				pClient->m_ObjInfos[pObj->m_ObjectID].m_ChangeFlags |= pObj->sd->m_ChangeFlags;
			}

			for (pSoundTrack=pServerMgr->m_ChangedSoundTrackHead; pSoundTrack; pSoundTrack=pSoundTrack->m_pChangedNext)
			{
				pClient->m_ObjInfos[(uint16)GetLinkID(pSoundTrack->m_pIDLink)].m_ChangeFlags |= pSoundTrack->m_wChangeFlags;
			}
		}
	}

	sm_ClearChangedObjectList(pServerMgr);
	sm_ClearChangedSoundTrackList(pServerMgr);
}

// FUNCTION: LITHTECH 0x00483370
void CServerMgr::UpdateSounds(float fDeltaTime)
{
	LTLink *pCur, *pNext;
	CSoundTrack *pSoundTrack;

	pCur = m_SoundTrackList.m_Head.m_pNext;
	while (pCur != &m_SoundTrackList.m_Head)
	{
		pSoundTrack = (CSoundTrack*)pCur->m_pData;
		if (!pSoundTrack)
			break;

		pNext = pCur->m_pNext;

		if (!pSoundTrack->GetRemove() || pSoundTrack->m_nClientRefs)
			pSoundTrack->Update(fDeltaTime);

		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x004833b0
void CServerMgr::RemoveSounds()
{
	LTLink *pCur, *pNext;
	CSoundTrack *pSoundTrack;
	LTObject *pObj;

	pCur = m_SoundTrackList.m_Head.m_pNext;
	while (pCur != &m_SoundTrackList.m_Head)
	{
		pSoundTrack = (CSoundTrack*)pCur->m_pData;
		if (!pSoundTrack)
			break;

		pNext = pCur->m_pNext;

		if (pSoundTrack->GetRemove() && pSoundTrack->m_wChangeFlags == 0 && pSoundTrack->m_nClientRefs == 0)
		{
			// Break the link to its object.
			if (pSoundTrack->m_dwFlags & PLAYSOUND_ATTACHED)
			{
				if (pSoundTrack->m_pInterLink)
				{
					pObj = pSoundTrack->m_pInterLink->m_pOwner;
					if (pObj)
						DisconnectLinks(this, pObj, pSoundTrack, LTTRUE);
				}
			}

			dl_RemoveAt(&m_SoundTrackList, &pSoundTrack->m_Link);

			if (pSoundTrack->m_pIDLink)
				sm_FreeID(g_pServerMgr, pSoundTrack->m_pIDLink);

			pSoundTrack->m_pSoundData = LTNULL;
			pSoundTrack->m_pIDLink = LTNULL;
			sb_Free(&m_SoundTrackBank, pSoundTrack);
		}

		pCur = pNext;
	}
}

// A file the loader thread finished with.
// FUNCTION: LITHTECH 0x00483460
void CServerMgr::OnLoaderMessage(LThreadMessage &msg)
{
	IServerShell *pShell;
	UsedFile *pFile;
	Model *pModel;

	pShell = m_ClassMgr.m_pServerShell;
	pFile = (UsedFile*)msg.m_Data[1].m_pData;

	if (msg.m_Data[0].m_dwData == LOADERMSG_FILELOADED)
	{
		pModel = (Model*)msg.m_Data[2].m_pData;

		if (msg.m_ID == LT_OK)
		{
			se_LoadChildModels(this, pModel, pFile, 1);
			if (pShell)
				pShell->FileLoadNotify(pModel->GetFilename(), LT_OK);
		}
		else
		{
			if (pShell)
				pShell->FileLoadNotify((char*)hs_GetElementKey(pFile->m_hElement, LTNULL), LT_ERROR);
		}
	}
}

// FUNCTION: LITHTECH 0x004834d0
void CServerMgr::ProcessLoaderMessages()
{
	LThreadMessage msg;

	while (m_LoaderThread.m_Outgoing.GetMessage(msg, LTFALSE) == LT_OK)
	{
		OnLoaderMessage(msg);
	}
}

// Matched by tools/permute.py: the paused/non-active branch comes first, the single-step loop is written as
// i = 0; if (i < 1) do { ... i++; } while (i < 1), fFrameTime lives in the running branch and curTime is read
// through a local copy (that gives the exe's early ebx/ebp pushes before the RUNNINGWORLD test).
// FUNCTION: LITHTECH 0x00483520
LTBOOL CServerMgr::Update(int32 updateFlags, float curTime)
{
	float fFrameTime;
	int nSteps, i;

	SetupGlobals();
	ProcessLoaderMessages();

	curTime += m_TimeOffset;
	m_TrueFrameTime = curTime - m_LastTime;
	m_LastTime = curTime;

	float fCurTime = curTime;
	m_NetMgr.Update("Server: ", fCurTime, LTTRUE);

	// Clear the profiling counters.
	g_Ticks_MoveObject = 0;
	g_nMoveObjectCalls = 0;
	g_IntersectTicks = g_nIntersectCalls = 0;
	g_IntersectLineLen = 0.0f;
	g_SphereFindTicks = 0;
	g_SphereFindCount = 0;
	g_PolyFindTicks = 0;
	g_PolyFindCount = 0;

	if (ProcessIncomingPackets(this))
		return LTFALSE;

	if (m_State == SERV_RUNNINGWORLD)
	{
		if ((updateFlags & UPDATEFLAG_NONACTIVE) || (m_ServerFlags & SFLAG_PAUSED))
		{
			m_LastTime = fCurTime;
			sm_UpdateClientStates(this);
		}
		else
		{
			float fFrameTime;
			// Restart the target time if the update rate changed.
			if (g_ServerFPS != m_LastServerFPS)
			{
				m_LastTargetTimeBase = m_GameTime;
				m_TargetTimeBase = m_TargetTime;
				m_nTargetTimeSteps = 0;
				m_FrameCode = 0;
			}

			m_LastServerFPS = g_ServerFPS;
			fFrameTime = 1.0f / g_ServerFPS;
			m_FrameTime = g_CV_TimeScale * fFrameTime;
			m_TargetTime = (float)m_nTargetTimeSteps * fFrameTime + m_TargetTimeBase;

			nSteps = (int)((curTime - m_TargetTime) / fFrameTime);
			if (nSteps < 0)
				nSteps = 0;

			m_FrameTime = m_TrueFrameTime;
			m_FrameTime = LTCLAMP(m_FrameTime, MIN_FRAMETIME, MAX_FRAMETIME);

			m_GameTime += m_FrameTime;

			i=0;
			if (i < 1)
			{
				do
				{
					UpdateSounds(m_FrameTime);

					// Debug frame stepping.
					if ((m_InternalFlags & SIFLAG_FRAMESTEP) && !(m_InternalFlags & SIFLAG_FRAMESTEPRUN))
					{
						if (m_nFramesToSkip == 0)
						{
							for (;;)
							{
							}
						}
						--m_nFramesToSkip;
					}

					if (m_ClassMgr.m_pServerShell)
						m_ClassMgr.m_pServerShell->Update(m_FrameTime);

					UpdateObjects();

					m_TrueFrameTime = 0.0f;
					++m_FrameCode;
					i++;
				} while (i < 1);
			}

			m_nTargetTimeSteps += nSteps;
			m_TargetTime = (float)m_nTargetTimeSteps * fFrameTime + m_TargetTimeBase;
		}

		sm_FinishUpdateFrame(this);
		RemoveSounds();
	}

	if (g_CV_ShowGameTime)
	{
		dsi_ConsolePrint("Game time: %.2f", m_GameTime);
	}

	if (g_CV_ShowSphereFindTicks)
	{
		dsi_ConsolePrint("ILTServer::FindObjectsTouchingSphere ticks: %d", g_SphereFindTicks);
		dsi_ConsolePrint("ILTServer::FindObjectsTouchingSphere count: %d", g_SphereFindCount);
	}

	if (g_CV_ShowPolyFindTicks)
	{
		dsi_ConsolePrint("ILTServer::FindPoliesTouchingBox ticks: %d", g_PolyFindTicks);
		dsi_ConsolePrint("ILTServer::FindPoliesTouchingBox count: %d", g_PolyFindCount);
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00483810
void CServerMgr::SetupGlobals()
{
	g_pServerMgr = this;
	g_pClassMgr = &m_ClassMgr;
}

// FUNCTION: LITHTECH 0x00483830
void CServerMgr::GetErrorString(char *pStr, int maxLen)
{
	strncpy(pStr, m_ErrorString, maxLen);
}

// Destroys and frees a game object.
inline void sm_DeleteObjectOfClass(CServerMgr *pServerMgr, ClassDef *pClass, LPBASECLASS pObject)
{
	CClassData *pClassData;

	pClassData = (CClassData*)pClass->m_pInternal[pServerMgr->m_ClassMgr.m_ClassIndex];
	pClass->m_DestructFn(pObject);
	sb_Free(&pClassData->m_ObjectBank, pObject);
}

// FUNCTION: LITHTECH 0x00483850
LPBASECLASS CServerMgr::EZCreateObject(CClassData *pClassData, ObjectCreateStruct *pStruct)
{
	ClassDef *pClass;
	LPBASECLASS pObject;
	LTObject *pObj;
	StructBank *pBank;

	pClass = pClassData->m_pClass;

	pBank = &((CClassData*)pClass->m_pInternal[m_ClassMgr.m_ClassIndex])->m_ObjectBank;
	pObject = (LPBASECLASS)sb_Allocate(pBank);
	pObject->m_hObject = LTNULL;
	pObject->m_pFirstAggregate = LTNULL;
	pClass->m_ConstructFn(pObject);

	pObject->EngineMessageFn(MID_PRECREATE, pStruct, 0.0f);

	if (sm_AddObjectToWorld(this, pObject, pClassData->m_pClass, pStruct, INVALID_OBJECTID, 0, &pObj) == LT_OK)
		return pObject;

	sm_DeleteObjectOfClass(this, pClassData->m_pClass, pObject);
	return LTNULL;
}


// FUNCTION: LITHTECH 0x00483d00
void CServerMgr::ResizeUpdateInfos(uint32 nAllocatedIDs)
{
	LTLink *pCur;
	Client *pClient;
	ObjInfo *pNewInfos;

	for (pCur=m_Clients.m_Head.m_pNext; pCur != &m_Clients.m_Head; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		pNewInfos = (ObjInfo*)dalloc(sizeof(ObjInfo) * (nAllocatedIDs + UPDATEINFO_CACHESIZE));
		memcpy(pNewInfos, pClient->m_ObjInfos, sizeof(ObjInfo) * m_nObjInfos);
		memset(&pNewInfos[m_nObjInfos], 0, sizeof(ObjInfo) * UPDATEINFO_CACHESIZE);

		dfree(pClient->m_ObjInfos);
		pClient->m_ObjInfos = pNewInfos;
	}

	m_nObjInfos = nAllocatedIDs + UPDATEINFO_CACHESIZE;
}


// FUNCTION: LITHTECH 0x00484030
void sm_FreeObjectOfClass(CServerMgr *pServerMgr, LTObject *pObj)
{
	if (pObj->sd && pObj->sd->m_pObject)
	{
		sm_DeleteObjectOfClass(pServerMgr, pObj->sd->m_pClass, pObj->sd->m_pObject);
		pObj->sd->m_pObject = LTNULL;
	}
}


// ----------------------------------------------------------------------- //
// Caching.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00484550
LTRESULT sm_StartCachingFiles(CServerMgr *pServerMgr, LTBOOL bBuildCacheList)
{
	HHashIterator *hIterator;
	Model *pModel;

	// Clear our 'wanted sprite' list.
	pServerMgr->m_CacheListSize = 0;

	// Clear all sounds and models as not needed.
	pServerMgr->UntouchAllSoundData();

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		pModel->m_Flags &= ~MODELFLAG_CACHED;
	}

	// Ask the shell what it wants to have loaded.
	if (bBuildCacheList)
		pServerMgr->m_InternalFlags |= SFLAG_BUILDINGCACHELIST;
	else
		pServerMgr->m_InternalFlags &= ~SFLAG_BUILDINGCACHELIST;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004845d0
LTRESULT sm_EndCachingFiles(CServerMgr *pServerMgr)
{
	pServerMgr->m_ClassMgr.m_pServerShell->CacheFiles();
	pServerMgr->m_InternalFlags &= ~SFLAG_BUILDINGCACHELIST;

	pServerMgr->FreeUnusedModels();
	sm_RemoveAllUnusedSoundData(pServerMgr);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00484610
LTRESULT sm_CacheEasyStuff(CServerMgr *pServerMgr)
{
	LTLink *pListHead, *pCur;
	LTObject *pObject;
	int i;

	// Cache models.
	pListHead = &pServerMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObject = (LTObject*)pCur->m_pData;

		if (((ModelInstance*)pObject)->GetModelDB() != pServerMgr->m_pDefaultModel)
		{
			if (pObject->sd->m_pFile)
				sm_CacheFile(pServerMgr, FT_MODEL, sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObject->sd->m_pFile));

			for (i=0; i < MAX_MODEL_TEXTURES; i++)
			{
				if (pObject->sd->m_pSkins[i])
					sm_CacheFile(pServerMgr, FT_TEXTURE, sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObject->sd->m_pSkins[i]));
			}

			if (pObject->sd->m_pSkins[0])
				sm_CacheFile(pServerMgr, FT_TEXTURE, sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObject->sd->m_pSkins[0]));
		}
	}

	// Cache sprites.
	pListHead = &pServerMgr->m_ObjectMgr.m_ObjectLists[OT_SPRITE].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObject = (LTObject*)pCur->m_pData;

		if (pObject->sd->m_pFile)
			sm_CacheFile(pServerMgr, FT_SPRITE, sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObject->sd->m_pFile));
	}

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// Child model links (childmodel.map). STLport containers.
// ----------------------------------------------------------------------- //

// world name -> (child model key -> associated child model).
typedef std::map<std::string, std::string> ChildModelLinks;
typedef std::map<std::string, ChildModelLinks> WorldChildModelLinks;

// FUNCTION: LITHTECH 0x00484720 _$E37
// FUNCTION: LITHTECH 0x00484730 _$E34
// FUNCTION: LITHTECH 0x00484780 _$E36
// FUNCTION: LITHTECH 0x00484790 _$E35
// GLOBAL: LITHTECH 0x004e5da4
WorldChildModelLinks g_ChildModelLinks;
// FUNCTION: LITHTECH 0x00484830 _$E42
// FUNCTION: LITHTECH 0x00484840 _$E39
// FUNCTION: LITHTECH 0x00484880 _$E41
// The string is file-static: its atexit destructor (_$E40) allocates registers differently for an extern one.
// FUNCTION: LITHTECH 0x00484890 _$E40
// GLOBAL: LITHTECH 0x004e5db8
static std::string g_ChildModelNone;

// Hands the links childmodel.map has for a world to the server's model loader.
// FUNCTION: LITHTECH 0x004848f0
void sm_LoadChildModelLinks(ILTServer *pServer, char *pWorldName)
{
	char fileName[256];

	pServer->ResetModelToChildModelLink();

	strcpy(fileName, pWorldName);
	_strlwr(fileName);

	std::string worldKey(fileName);
	ChildModelLinks &links = g_ChildModelLinks[worldKey];
	if (!links.empty())
	{
		for (ChildModelLinks::iterator it=links.begin(); it != links.end(); ++it)
		{
			char *pChild = (char*)it->second.c_str();
			pServer->LinkModelToExtraChildModel((char*)it->first.c_str(), &pChild, 1);
		}
	}
}


// Reads childmodel.map (lines of "world childmodelkey associatedchildmodel") the first time a world starts.
// The two keys are temporaries of the one statement (their destructors and the inner operator[] temporary's
// are what keep the pair<string,map> destructor out of line, as in the original).
// FUNCTION: LITHTECH 0x00484d70
void CServerMgr::LoadChildModelMap()
{
	char line[256];
	char *pWorld, *pKey, *pAssociated;
	FILE *fp;

	if (g_ChildModelLinks.empty())
	{
		g_ChildModelNone = "none";

		fp = fopen("childmodel.map", "r");
		if (fp)
		{
			while (fgets(line, sizeof(line), fp))
			{
				pWorld = strtok(line, "; \t\n\r");
				pKey = strtok(NULL, "; \t\n\r");
				pAssociated = strtok(NULL, "; \t\n\r");
				_strlwr(pWorld);
				_strlwr(pKey);
				_strlwr(pAssociated);

				if (pWorld && pKey && pAssociated)
				{
					g_ChildModelLinks[std::string(pWorld)][std::string(pKey)] = pAssociated;
				}
			}
		}
	}
}


// ----------------------------------------------------------------------- //
// Worlds.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00485020
LTRESULT CServerMgr::DoRunWorld()
{
	if (!m_World.m_bLoaded)
		RETURN_ERROR(1, CServerMgr::DoRunWorld, LT_NOTINWORLD);

	sm_CacheEasyStuff(this);
	sm_EndCachingFiles(this);

	m_State = SERV_RUNNINGWORLD;
	m_LastServerFPS = g_ServerFPS;

	m_ClassMgr.m_pServerShell->PostStartWorld();

	sm_FinishUpdateFrame(this);
	dsi_ConsolePrint("Running world");
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00485190
void s_DisassociateClientsFromObjects(CServerMgr *pServerMgr)
{
	LTLink *pListHead, *pCur;
	Client *pClient;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (pClient->m_pObject)
		{
			pClient->m_pObject->sd->m_pClient = LTNULL;
			pClient->m_pObject = LTNULL;
		}
	}
}

LTRESULT sm_UnloadModelFile(CServerMgr *pServerMgr, char *pFilename);

// Unloads the models that had extra child models linked in (the links are per world).
// The postfix it++ keeps _Rb_global::_M_increment (0x00457ed0) out of line (++it inlines it), and declaring
// the iterator in the for statement drops the default constructor's dead store.
// FUNCTION: LITHTECH 0x004851d0
void sm_UncacheModels(CServerMgr *pServerMgr)
{
	std::set<Model*> models;
	HHashIterator *hIterator;
	Model *pModel;

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		if (pModel->m_Unknown190)
			models.insert(pModel);
	}

	for (std::set<Model*>::iterator it=models.begin(); it != models.end(); it++)
		sm_UnloadModelFile(pServerMgr, (*it)->GetFilename());
}

// Unloads a model file the game is done with and tells the clients.
// FUNCTION: LITHTECH 0x00485300
LTRESULT sm_UnloadModelFile(CServerMgr *pServerMgr, char *pFilename)
{
	UsedFile *pFile;
	LTRESULT dResult;

	if (!sf_AddUsedFile(&pServerMgr->m_FileMgr, pFilename, 0, &pFile))
	{
		RETURN_ERROR_PARAM(1, ThreadLoadTexture, LT_MISSINGFILE, pFilename);
	}

	dResult = se_UncacheModel(pServerMgr, pFilename, pFile);
	if (dResult == LT_OK)
	{
		sm_SendFileIOMessage(pServerMgr, FT_MODEL, FILEIO_UNLOAD, (uint16)pFile->m_FileID, 0);
		return LT_OK;
	}

	return dResult;
}

// FUNCTION: LITHTECH 0x00485990
void CServerMgr::ProcessClientCommands(Client *pClient, uint8 *pCommands, int nCommands)
{
	int i;

	for (i=0; i < nCommands; i++)
	{
		if (pClient->m_Commands[pClient->m_iCurCommands][pCommands[i]])
			m_ClassMgr.m_pServerShell->OnCommandOn((HCLIENT)pClient, pCommands[i]);
		else
			m_ClassMgr.m_pServerShell->OnCommandOff((HCLIENT)pClient, pCommands[i]);
	}
}

// FUNCTION: LITHTECH 0x004859f0
CBaseDriver* CServerMgr::GetLocalDriver()
{
	uint32 i;

	for (i=0; i < m_NetMgr.m_Drivers.GetSize(); i++)
	{
		if (strcmp(m_NetMgr.m_Drivers[i]->m_Name, "local") == 0)
			return m_NetMgr.m_Drivers[i];
	}

	return LTNULL;
}


// ----------------------------------------------------------------------- //
// Sounds.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004860a0
CSoundData* CServerMgr::FindSoundData(UsedFile *pFile)
{
	LTLink *pCur;
	CSoundData *pSoundData;

	for (pCur=m_SoundDataList.m_Head.m_pNext; pCur != &m_SoundDataList.m_Head; pCur=pCur->m_pNext)
	{
		pSoundData = (CSoundData*)pCur->m_pData;
		if (pSoundData->m_pFile == pFile)
			return pSoundData;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x004860d0
CSoundData* CServerMgr::GetSoundData(UsedFile *pFile)
{
	CSoundData *pSoundData;
	ILTStream *pStream;
	char *pFilename;
	LTBOOL bResult;

	// Already loaded?
	pSoundData = FindSoundData(pFile);
	if (!pSoundData)
	{
		pFilename = sf_GetUsedFilename(&m_FileMgr, pFile);
		if (pFile)
		{
			pStream = sf_OpenFile(&m_FileMgr, pFilename);
			if (pStream)
			{
				pSoundData = (CSoundData*)sb_Allocate(&m_SoundDataBank);
				bResult = pSoundData->Init(pFile, pStream, pFile->m_FileSize);
				pStream->Release();

				if (!bResult)
				{
					sb_Free(&m_SoundDataBank, pSoundData);
					return LTNULL;
				}

				dl_AddHead(&m_SoundDataList, &pSoundData->m_Link, pSoundData);
			}
		}
	}

	return pSoundData;
}

// FUNCTION: LITHTECH 0x004864d0 ?sm_SetupError@@YAKPAVCServerMgr@@KZZ
LTRESULT sm_SetupError(CServerMgr *pServerMgr, LTRESULT theError, ...)
{
	va_list marker;
	LTRESULT dResult;

	va_start(marker, theError);
	dResult = dsi_SetupMessage(pServerMgr->m_ErrorString, MAX_ERRORSTRING_LEN, theError, marker);
	va_end(marker);

	pServerMgr->m_LastErrorCode = theError;
	return dResult;
}

// FUNCTION: LITHTECH 0x00486500
LTRESULT sm_CheckObjectIntegrity(CServerMgr *pServerMgr)
{
	LTLink *pListHead, *pCur;
	LTObject *pObj;

	pListHead = &pServerMgr->m_Objects.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObj = (LTObject*)pCur->m_pData;

		if (pServerMgr->m_ObjectMap[pObj->m_ObjectID].m_nRecordType != RECORDTYPE_LTOBJECT ||
			!pServerMgr->m_ObjectMap[pObj->m_ObjectID].m_pRecordData)
		{
			RETURN_ERROR_PARAM(1, sm_CheckObjectIntegrity, LT_ERROR, pObj->sd->m_pClass->m_ClassName);
		}
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00486590
void CServerMgr::UntouchAllSoundData()
{
	LTLink *pCur, *pNext;
	CSoundData *pSoundData;

	pCur = m_SoundDataList.m_Head.m_pNext;
	while (pCur != &m_SoundDataList.m_Head)
	{
		pNext = pCur->m_pNext;
		pSoundData = (CSoundData*)pCur->m_pData;
		pSoundData->m_bTouched = LTFALSE;
		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x004865b0
void sm_RemoveAllSounds(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext;
	CSoundData *pSoundData;

	// Remove sound instances.
	sm_RemoveAllSoundInstances(pServerMgr);

	// Uninit sound stuff.
	pCur = pServerMgr->m_SoundDataList.m_Head.m_pNext;
	while (pCur != &pServerMgr->m_SoundDataList.m_Head)
	{
		pNext = pCur->m_pNext;
		pSoundData = (CSoundData*)pCur->m_pData;
		sb_Free(&pServerMgr->m_SoundDataBank, pSoundData);
		pCur = pNext;
	}
	dl_InitList(&pServerMgr->m_SoundDataList);
}

// FUNCTION: LITHTECH 0x00486600
void sm_RemoveAllUnusedSoundData(CServerMgr *pServerMgr)
{
	LTLink *pCur;
	CSoundData *pSoundData;

	// Remove untouched sounddata
	pCur = pServerMgr->m_SoundDataList.m_Head.m_pNext;
	while (pCur != &pServerMgr->m_SoundDataList.m_Head)
	{
		pSoundData = (CSoundData*)pCur->m_pData;
		pCur = pCur->m_pNext;
		if (pSoundData->m_bTouched)
			continue;

		dl_RemoveAt(&pServerMgr->m_SoundDataList, &pSoundData->m_Link);
		pSoundData->Term();
		sb_Free(&pServerMgr->m_SoundDataBank, pSoundData);
	}
}

// FUNCTION: LITHTECH 0x00486660
void sm_RemoveAllUnusedSoundTracks(CServerMgr *pServerMgr)
{
	LTLink *pCurInstance;
	CSoundTrack *pSoundInstance;

	// Remove any sound instances that aren't being held onto by handles
	pCurInstance = pServerMgr->m_SoundTrackList.m_Head.m_pNext;
	while (pCurInstance != &pServerMgr->m_SoundTrackList.m_Head)
	{
		pSoundInstance = (CSoundTrack*)pCurInstance->m_pData;
		pCurInstance = pCurInstance->m_pNext;

		// Report instances that still have handles on them.
		if (pSoundInstance->m_dwFlags & PLAYSOUND_GETHANDLE)
		{
			if (g_DebugLevel >= 3)
			{
				if (pSoundInstance->m_pFile)
				{
					dsi_ConsolePrint("Unfreed sound file (server) %s",
						sf_GetUsedFilename(&pServerMgr->m_FileMgr, pSoundInstance->m_pFile));
				}
			}
		}

		// Remove the instance...
		dl_RemoveAt(&pServerMgr->m_SoundTrackList, &pSoundInstance->m_Link);
		if (pSoundInstance->m_pIDLink)
			sm_FreeID(g_pServerMgr, pSoundInstance->m_pIDLink);
		pSoundInstance->m_pSoundData = LTNULL;
		pSoundInstance->m_pIDLink = LTNULL;
		sb_Free(&pServerMgr->m_SoundTrackBank, pSoundInstance);
	}
}

// FUNCTION: LITHTECH 0x00486710
void sm_RemoveAllSoundInstances(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext;
	CSoundTrack *pSoundTrack;

	pCur = pServerMgr->m_SoundTrackList.m_Head.m_pNext;
	while (pCur != &pServerMgr->m_SoundTrackList.m_Head)
	{
		pNext = pCur->m_pNext;
		pSoundTrack = (CSoundTrack*)pCur->m_pData;

		if (pSoundTrack->m_pIDLink)
			sm_FreeID(g_pServerMgr, pSoundTrack->m_pIDLink);
		pSoundTrack->m_pSoundData = LTNULL;
		pSoundTrack->m_pIDLink = LTNULL;
		sb_Free(&pServerMgr->m_SoundTrackBank, pSoundTrack);

		pCur = pNext;
	}
	dl_InitList(&pServerMgr->m_SoundTrackList);
}


// ----------------------------------------------------------------------- //
// Auto-deactivation.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00486780 ?sm_ClearAutoDeactivate@@YAXPAVCServerMgr@@@Z
void sm_ClearAutoDeactivate(CServerMgr *pServerMgr)
{
	LTLink *pListHead, *pCur;
	LTObject *pObj;

	pListHead = &pServerMgr->m_Objects.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pObj = (LTObject*)pCur->m_pData;
		pCur = pCur->m_pNext;

		if (pObj->sd)
		{
			pObj->sd->m_fDeactivateTimer = pObj->sd->m_fDeactivationTime;

			if (pObj->m_InternalFlags & IFLAG_AUTODEACTIVATED)
				sm_SetObjectStateFlags(pServerMgr, pObj, pObj->m_InternalFlags & IFLAG_INACTIVE_TICK_MASK);

			if (g_bAutoDeactivate && pObj->m_Link60.m_pNext == &pObj->m_Link60)
				pServerMgr->m_World.m_WorldTree.InsertObject(pObj, 0);
		}
	}
}

// FUNCTION: LITHTECH 0x004867f0
void sm_ResetDeactivateTimer(LTObject *pObj)
{
	if (pObj->sd->m_fDeactivationTime > 0.0f)
		pObj->sd->m_fDeactivateTimer = g_pServerMgr->m_FrameTime + pObj->sd->m_fDeactivationTime;
	else
		pObj->sd->m_fDeactivateTimer = pObj->sd->m_fDeactivationTime;

	if (pObj->m_InternalFlags & IFLAG_AUTODEACTIVATED)
		sm_SetObjectStateFlags(g_pServerMgr, pObj, pObj->m_InternalFlags & IFLAG_INACTIVE_TICK_MASK);
}

void sm_ActivateObjectCB(WorldTreeObj *pObj, void *pUser);	// 0x00486900
void sm_SendToVisibleClientsCB(WorldTreeObj *pObj, void *pUser);	// 0x00486c10
void sm_GetClientObjects(LTLink *pListHead, LTObject ***ppObjects, uint32 *pnObjects);	// 0x00486bc0

// Activates the objects around pObj.
// FUNCTION: LITHTECH 0x00486850
void sm_ActivateObjectsNear(CServerMgr *pServerMgr, LTObject *pObj)
{
	VisQueryRequest request;

	if (g_bAutoDeactivate && pServerMgr->m_World.m_bLoaded)
	{
		request.m_ViewRadius = 10000.0f;
		request.m_AddObject = sm_ActivateObjectCB;
		request.m_Viewpoint = pObj->m_Pos;
		request.m_pUserData = LTNULL;
		pServerMgr->m_World.m_WorldTree.DoVisQuery(&request);
	}
}

// What sm_SendToVisibleClients hands its world tree callback.
struct SendToVisibleInfo
{
	CServerMgr	*m_pServerMgr;		// 0x00
	CPacket		*m_pPacket;			// 0x04
	LTObject	*m_pObj;			// 0x08
	uint8		m_MsgID;			// 0x0c
	uint32		m_Flags;			// 0x10
};

// Sends a special effect message to the clients that can see pPos.
// FUNCTION: LITHTECH 0x00486ab0
LTRESULT sm_SendSFXMessage(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket, LTObject *pObj, LTVector *pPos, uint32 flags)
{
	VisQueryRequest request;
	SendToVisibleInfo info;
	Client *pClient;

	if (pServerMgr->m_Clients.m_nElements)
	{
		// Don't bother with a vis query if the only client is local.
		if (pServerMgr->m_Clients.m_nElements == 1)
		{
			pClient = (Client*)pServerMgr->m_Clients.m_Head.m_pNext->m_pData;
			if (pClient->m_ClientFlags & CFLAG_LOCAL)
			{
				sm_SendToClient(pServerMgr, pClient, msgID, pPacket, flags);
				return LT_OK;
			}
		}

		info.m_pServerMgr = pServerMgr;
		info.m_pPacket = pPacket;
		info.m_pObj = pObj;
		info.m_MsgID = msgID;
		info.m_Flags = flags;

		request.m_ViewRadius = 10000.0f;
		request.m_Viewpoint = *pPos;
		request.m_AddObject = sm_SendToVisibleClientsCB;
		request.m_Unknown18 = (void*)sm_GetClientObjects;
		request.m_pUserData = &info;
		pServerMgr->m_World.m_WorldTree.DoVisQuery(&request);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00486900
void sm_ActivateObjectCB(WorldTreeObj *pObj, void *pUser)
{
	if (pObj->GetObjType() == WTObj_DObject)
		sm_ResetDeactivateTimer((LTObject*)pObj);
}


// ----------------------------------------------------------------------- //
// CRCs.
// ----------------------------------------------------------------------- //

// CRCs the files listed in m_CRCString (a ';' separated list, '*' wildcards allowed).
// FUNCTION: LITHTECH 0x00483a00
LTRESULT CServerMgr::CreateStringCRC()
{
	char crcString[1024];
	char prefix[128], dirName[128], suffix[128];
	char *pToken, *pStar;
	FileEntry *pList, *pCur;
	ILTStream *pStream;
	uint32 crc, totalCRC, prefixLen, suffixLen;
	int i;

	if (m_CRCString[0])
	{
		strcpy(crcString, m_CRCString);

		totalCRC = 0;
		pToken = strtok(crcString, "; \n\r\t");
		while (pToken)
		{
			pStar = strstr(pToken, "*");
			if (pStar)
			{
				strncpy(prefix, pToken, pStar - pToken);
				strcpy(suffix, pStar + 1);
				prefix[pStar - pToken] = 0;

				// Find the directory part.
				strcpy(dirName, prefix);
				for (i=strlen(dirName); i > 0 && dirName[i] != '\\'; i--)
				{
					dirName[i] = 0;
				}

				pList = sf_GetFileList(&m_FileMgr, dirName);

				prefixLen = strlen(prefix);
				suffixLen = strlen(suffix);
				for (pCur=pList; pCur; pCur=pCur->m_pNext)
				{
					if (pCur->m_Type == FILE_TYPE &&
						_strnicmp(pCur->m_pFullFilename, prefix, prefixLen) == 0 &&
						_strnicmp(&pCur->m_pFullFilename[strlen(pCur->m_pFullFilename) - suffixLen], suffix, suffixLen) == 0)
					{
						pStream = sf_OpenFile(&m_FileMgr, pCur->m_pFullFilename);
						if (pStream)
						{
							m_pServerInterface->Common()->GetCRC(pStream, crc);
							totalCRC += crc;
							pStream->Release();
						}
					}
				}

				ic_FreeFileList(pList);
			}
			else
			{
				pStream = sf_OpenFile(&m_FileMgr, pToken);
				if (pStream)
				{
					m_pServerInterface->Common()->GetCRC(pStream, crc);
					totalCRC += crc;
					pStream->Release();
				}
			}

			pToken = strtok(LTNULL, "; \n\r\t");
		}

		if (!totalCRC)
			totalCRC = 1;

		m_StringCRC = totalCRC;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00483c60
LTRESULT CServerMgr::CreateWorldCRC()
{
	char filename[256];
	ILTStream *pStream;

	if (m_bTrackChanges && m_pWorldFile && m_pServerInterface)
	{
		strncpy(filename, sf_GetUsedFilename(&m_FileMgr, m_pWorldFile), 255);
		_strupr(filename);

		pStream = sf_OpenFile(&m_FileMgr, filename);
		if (pStream)
		{
			m_pServerInterface->Common()->GetCRC(pStream, m_WorldCRC);
			pStream->Release();
			return LT_OK;
		}
	}

	return LT_ERROR;
}


// ----------------------------------------------------------------------- //
// Objects.
// ----------------------------------------------------------------------- //

// One instruction off: the original zero-extends the create flag byte (xor eax,eax first).
// Wave 6 tried: (uint8)(x & 1), x & 1, OCS_AUTOLOAD ternary, `&= 1` after the byte store, |=, a 1-bit bitfield
// view of m_bCreateFlag1 (uint32 and uint8): no change (7 aligned).
// Wave 7 phase 2: audit: behaviour matches (7 aligned).
// PARKED: one zero-extension (xor eax,eax first) of the create flag byte; behaviour identical
// STUB: LITHTECH 0x00483dd0
LTRESULT sm_CreateServerData(CServerMgr *pServerMgr, ObjectCreateStruct *pStruct, ClassDef *pClass,
	LTObject *pObject, LPBASECLASS pBaseClass, ServerData **ppData)
{
	ServerData *pRet;

	*ppData = LTNULL;

	pRet = pServerMgr->m_SObjBank.Allocate();
	if (!pRet)
	{
		RETURN_ERROR(1, sm_CreateServerData, LT_TRIEDTOREMOVECLIENTOBJECT);
	}

	// Init all the data.
	pRet->m_pObject = LTNULL;
	pRet->m_pSkins[0] = LTNULL;
	pRet->m_pFile = LTNULL;
	pRet->m_pClass = pClass;
	pRet->m_pClient = LTNULL;
	pRet->m_NextUpdate = pStruct->m_NextUpdate;
	pRet->m_fDeactivationTime = pStruct->m_fDeactivationTime;
	pRet->m_fDeactivateTimer = pStruct->m_fDeactivationTime;
	pRet->m_pSFXMsg = LTNULL;
	pRet->m_pIDLink = LTNULL;
	pRet->m_bCreateFlag1 = 0;
	pRet->m_ChangeFlags = 0;
	pRet->m_NetFlags = 0;
	pRet->m_bCreateFlag1 = (uint8)pStruct->m_CreateFlags & 1;

	// Add its name to the hash table.
	if (pStruct->m_Name[0] == 0)
	{
		// Don't waste memory for most of the objects..
		pRet->m_hName = LTNULL;
	}
	else
	{
		pRet->m_hName = LTNULL;
		pRet->m_hName = hs_AddElement(pServerMgr->m_hNameTable, pStruct->m_Name, strlen(pStruct->m_Name)+1);
		hs_SetElementUserData(pRet->m_hName, pObject);
	}

	dl_TieOff(&pRet->m_Links);

	dl_AddHead(&pServerMgr->m_Objects, &pRet->m_ListNode, pObject);

	*ppData = pRet;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00483f30
LTRESULT sm_DestroyServerData(CServerMgr *pServerMgr, LTObject *pObject)
{
	ClientRef *pClientRef;

	// Free its game object.
	sm_FreeObjectOfClass(pServerMgr, pObject);

	// Get rid of its alpha list stuff.
	if (pObject->sd->m_hName)
	{
		hs_RemoveElement(pServerMgr->m_hNameTable, pObject->sd->m_hName);
	}

	// If it has a client ref, get rid of it.
	if (pObject->m_InternalFlags & IFLAG_HASCLIENTREF)
	{
		pClientRef = sm_FindClientRefFromObject(pServerMgr, pObject);
		if (pClientRef)
		{
			dl_RemoveAt(&pServerMgr->m_ClientReferences, &pClientRef->m_Link);
			dfree(pClientRef);
		}
	}

	// Break all links.
	BreakInterLinks(pServerMgr, pObject, LINKTYPE_INTERLINK, LTTRUE);
	BreakInterLinks(pServerMgr, pObject, LINKTYPE_CONTAINER, LTFALSE);
	BreakInterLinks(pServerMgr, pObject, LINKTYPE_SOUND, LTFALSE);
	BreakInterLinks(pServerMgr, pObject, LINKTYPE_OBJREF, LTFALSE);

	dl_RemoveAt(&pServerMgr->m_Objects, &pObject->sd->m_ListNode);
	pServerMgr->m_SObjBank.Free(pObject->sd);
	return LT_OK;
}

// initialUpdateReason is the float passed with MID_OBJECTCREATED (callers declare it uint32).
// FUNCTION: LITHTECH 0x00484090 ?sm_AddObjectToWorld@@YAKPAVCServerMgr@@PAVBaseClass@@PAUClassDef@@PAUObjectCreateStruct@@GKPAPAVLTObject@@@Z
LTRESULT sm_AddObjectToWorld(CServerMgr *pServerMgr, LPBASECLASS pBaseClass, ClassDef *pClass,
	ObjectCreateStruct *pStruct, uint16 objectID, uint32 initialUpdateReason, LTObject **ppOut)
{
	ObjectMgr *pObjectMgr;
	LTObject *pObject;
	LTLink *pIDLink;
	LTRESULT dResult;

	*ppOut = LTNULL;

	if (!pServerMgr->m_bTrackChanges)
	{
		RETURN_ERROR(1, sm_AddObjectToWorld, LT_NOTINWORLD);
	}

	// Create the object.
	pObjectMgr = &pServerMgr->m_ObjectMgr;
	dResult = om_CreateObject(pObjectMgr, pStruct, &pObject);
	if (dResult != LT_OK)
		return dResult;

	pBaseClass->m_hObject = (HOBJECT)pObject;

	dResult = sm_CreateServerData(pServerMgr, pStruct, pClass, pObject, pBaseClass, &pObject->sd);
	if (dResult != LT_OK)
	{
		om_DestroyObject(pObjectMgr, pObject);
		return dResult;
	}

	// Tell this object that it is active.
	pBaseClass->EngineMessageFn(MID_ACTIVATING, LTNULL, 0.0f);

	// Init extra data (like model, worldmodel, sprite, etc.)
	dResult = sm_InitExtraData(pServerMgr, pObject, pStruct);
	if (dResult != LT_OK)
	{
		sm_DestroyServerData(pServerMgr, pObject);
		om_DestroyObject(pObjectMgr, pObject);
		return dResult;
	}

	// Get an id...
	dResult = sm_AllocateID(pServerMgr, &pIDLink, objectID);
	if (dResult != LT_OK)
	{
		sm_DestroyServerData(pServerMgr, pObject);
		om_DestroyObject(pObjectMgr, pObject);
		return dResult;
	}

	pObject->m_InternalFlags = IFLAG_INWORLD | IFLAG_APPLYPHYSICS;

	// Assign the id...
	pObject->sd->m_pIDLink = pIDLink;
	pObject->m_ObjectID = (uint16)GetLinkID(pIDLink);
	pServerMgr->m_ObjectMap[pObject->m_ObjectID].m_nRecordType = RECORDTYPE_LTOBJECT;
	pServerMgr->m_ObjectMap[pObject->m_ObjectID].m_pRecordData = pObject;

	// This should be last so if an error returns, the caller needs to FreeObjectOfClass.
	pObject->sd->m_pObject = pBaseClass;

	// Transform it if it's a Worldmodel.
	if (pObject->HasWorldModel())
	{
		InitialWorldModelRotate((WorldModelInstance*)pObject);
	}

	// Add it to the bsp...
	sm_UpdateInBspStatus(pServerMgr, pObject);

	// Call its initial update!
	pBaseClass->EngineMessageFn(MID_INITIALUPDATE, pStruct, *(float*)&initialUpdateReason);

	// Set its initial change flags..
	SetObjectChangeFlags(pServerMgr, pObject, sm_GetNewObjectChangeFlags(pServerMgr, pObject));

	g_ObjectMemory += sizeof(LTObject) + sizeof(ServerData);
	*ppOut = pObject;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00484390
LTRESULT sm_RemoveObjectFromWorld(CServerMgr *pServerMgr, LPBASECLASS pBaseClass)
{
	LTObject *pObject, *pChild;
	Attachment *pAttachment;

	pObject = (LTObject*)pBaseClass->m_hObject;
	if (!pObject)
	{
		RETURN_ERROR(1, sm_RemoveObjectFromWorld, LT_ERROR);
	}

	// Make sure it's not a client object.
	if (pObject->sd)
	{
		pObject->m_InternalFlags |= IFLAG_OBJECTGOINGAWAY;

		// Send a detach message to the child attachments.
		if (~pServerMgr->m_InternalFlags & SIFLAG_REMOVINGALLOBJECTS)
		{
			for (pAttachment=pObject->m_Attachments; pAttachment; pAttachment=pAttachment->m_pNext)
			{
				pChild = sm_FindObject(pServerMgr, pAttachment->m_nChildID);
				if (pChild)
				{
					pChild->sd->m_pObject->EngineMessageFn(MID_PARENTATTACHMENTREMOVED, LTNULL, 0.0f);
				}
			}
		}

		// Tell clients to get it out of the sky if it's in there.
		if (pObject->m_InternalFlags & IFLAG_INSKY)
		{
			sm_SetSendSkyDef(pServerMgr);
			sm_RemoveObjectFromSky(pServerMgr, pObject);
			pObject->m_InternalFlags &= ~IFLAG_INSKY;
		}

		if (pObject->sd->m_pClient)
		{
			RETURN_ERROR(1, sm_RemoveObjectFromWorld, LT_TRIEDTOREMOVECLIENTOBJECT);
		}

		BreakInterLinks(pServerMgr, pObject, LINKTYPE_INTERLINK, LTTRUE);

		if (pObject->sd->m_pIDLink)
		{
			sm_FreeID(pServerMgr, pObject->sd->m_pIDLink);

			// however, still set m_pRecordData to NULL for any lookups that might happen
			pServerMgr->m_ObjectMap[pObject->m_ObjectID].m_nRecordType = 0;
			pServerMgr->m_ObjectMap[pObject->m_ObjectID].m_pRecordData = LTNULL;
		}

		// Detach it from whatever it's standing on.
		DetachObjectStanding(pObject);
		DetachObjectsStandingOn(pObject);
		w_RemoveObjectFromLeaf(pObject);

		sm_DestroyServerData(pServerMgr, pObject);
		pObject->sd = LTNULL;
	}

	sm_TermExtraData(pServerMgr, pObject);

	// Free the LTObject.
	g_ObjectMemory -= sizeof(LTObject) + sizeof(ServerData);
	return om_DestroyObject(&pServerMgr->m_ObjectMgr, pObject);
}


// ----------------------------------------------------------------------- //
// CServerMgr's ILTSoundMgr implementation (its second base class).
// ----------------------------------------------------------------------- //


// FUNCTION: LITHTECH 0x00485a40
LTRESULT CServerMgr::PlaySound(PlaySoundInfo *pPlaySoundInfo, HLTSOUND &hResult)
{
	CServerEvent *pEvent;
	CSoundData *pSoundData;
	CSoundTrack *pSoundTrack;
	UsedFile *pFile;

	pSoundData = LTNULL;
	pSoundTrack = LTNULL;

	if (!pPlaySoundInfo)
		return LT_INVALIDPARAMS;

	// Need a world to compress positions...
	if (!m_World.m_bLoaded)
		return LT_ERROR;

	// Check if sound attached to object and someone forgot the handle to the object...
	if (pPlaySoundInfo->m_dwFlags & PLAYSOUND_ATTACHED && !pPlaySoundInfo->m_hObject)
		return LT_ERROR;

	// Get pointer to file...
	if (sf_AddUsedFile(&m_FileMgr, pPlaySoundInfo->m_szSoundName, 0, &pFile) == 0)
	{
		DEBUG_PRINT(2, ("Missing sound file %s", pPlaySoundInfo->m_szSoundName));
		return LT_MISSINGFILE;
	}

	// Limit the radii...
	if (pPlaySoundInfo->m_dwFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
	{
		pPlaySoundInfo->m_fOuterRadius = LTCLAMP(pPlaySoundInfo->m_fOuterRadius, MIN_SOUND_RADIUS, MAX_SOUND_RADIUS);
		pPlaySoundInfo->m_fInnerRadius = LTCLAMP(pPlaySoundInfo->m_fInnerRadius, MIN_SOUND_RADIUS, MAX_SOUND_RADIUS);
	}

	// Remove flags that don't change anything.
	if ((pPlaySoundInfo->m_dwFlags & PLAYSOUND_CTRL_VOL) && pPlaySoundInfo->m_nVolume == 100)
		pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_CTRL_VOL;
	if ((pPlaySoundInfo->m_dwFlags & PLAYSOUND_CTRL_PITCH) && pPlaySoundInfo->m_fPitchShift == 1.0f)
		pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_CTRL_PITCH;
	if ((pPlaySoundInfo->m_dwFlags & PLAYSOUND_CTRL_TYPE) && pPlaySoundInfo->m_nUserSoundType == 0)
		pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_CTRL_TYPE;
	if ((pPlaySoundInfo->m_dwFlags & PLAYSOUND_USER_DATA) && pPlaySoundInfo->m_UserData == 0)
		pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_USER_DATA;

	// Check if server needs to keep a reference to the sound...
	if (pPlaySoundInfo->m_dwFlags & (PLAYSOUND_GETHANDLE | PLAYSOUND_LOOP | PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED))
	{
		// Removed an invalid combination...
		if (pPlaySoundInfo->m_dwFlags & PLAYSOUND_ATTACHED)
			pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_CLIENTLOCAL;

		// Check if server needs to track the time...
		if (pPlaySoundInfo->m_dwFlags & (PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED) &&
			!(pPlaySoundInfo->m_dwFlags & PLAYSOUND_LOOP))
		{
			// Check if file data already read...
			pSoundData = GetSoundData(pFile);
			if (!pSoundData)
				return LT_ERROR;
		}

		// Create the instance of the sound on the server...
		pSoundTrack = (CSoundTrack*)sb_Allocate(&m_SoundTrackBank);
		if (!pSoundTrack)
			return LT_ERROR;

		// Set handle for caller...
		if (pPlaySoundInfo->m_dwFlags & PLAYSOUND_GETHANDLE)
			pPlaySoundInfo->m_hSound = (HLTSOUND)pSoundTrack;
		else
			pPlaySoundInfo->m_hSound = LTNULL;

		// Initialize the soundtrack...
		if (!pSoundTrack->Init(pPlaySoundInfo, m_GameTime, pFile, pSoundData))
		{
			// Undo all of it...
			pPlaySoundInfo->m_hSound = LTNULL;
			sb_Free(&m_SoundTrackBank, pSoundTrack);
			return LT_ERROR;
		}

		// Put in sound list...
		dl_AddHead(&m_SoundTrackList, &pSoundTrack->m_Link, pSoundTrack);
	}
	// Handle sound that the server doesn't need to care about...
	else
	{
		// Setup a sound event.
		pEvent = CreateServerEvent(this, EVENT_PLAYSOUND);
		PLAYSOUNDINFO_COPY(pEvent->m_PlaySoundInfo, *pPlaySoundInfo);
		pEvent->m_pUsedFile = pFile;
	}

	hResult = (HLTSOUND)pSoundTrack;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00485cf0
LTRESULT CServerMgr::GetSoundDuration(HLTSOUND hSound, LTFLOAT &fDuration)
{
	CSoundTrack *pSoundTrack;

	pSoundTrack = (CSoundTrack*)hSound;
	if (!pSoundTrack || !pSoundTrack->m_pSoundData)
	{
		RETURN_ERROR(1, CServerMgr::GetSoundDuration, LT_INVALIDPARAMS);
	}

	fDuration = pSoundTrack->GetDuration();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00485d50
LTRESULT CServerMgr::IsSoundDone(HLTSOUND hSound, LTBOOL &bDone)
{
	CSoundTrack *pSoundTrack;

	pSoundTrack = (CSoundTrack*)hSound;
	if (!pSoundTrack)
	{
		RETURN_ERROR(1, CServerMgr::IsSoundDone, LT_INVALIDPARAMS);
	}

	bDone = pSoundTrack->IsDone();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00485dd0
LTRESULT CServerMgr::KillSound(HLTSOUND hSound)
{
	CSoundTrack *pSoundTrack;

	pSoundTrack = (CSoundTrack*)hSound;
	if (!pSoundTrack)
	{
		RETURN_ERROR(1, CServerMgr::KillSound, LT_INVALIDPARAMS);
	}

	// Only sounds with handles can be killed by the game.
	if (!(pSoundTrack->m_dwFlags & PLAYSOUND_GETHANDLE))
	{
		RETURN_ERROR(1, CServerMgr::KillSound, LT_ERROR);
	}

	pSoundTrack->m_fTimeLeft = 0.0f;
	pSoundTrack->SetRemove(LTTRUE);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00485e70
LTRESULT CServerMgr::KillSoundLoop(HLTSOUND hSound)
{
	CSoundTrack *pSoundTrack;

	pSoundTrack = (CSoundTrack*)hSound;
	if (!pSoundTrack)
	{
		RETURN_ERROR(1, CServerMgr::KillSoundLoop, LT_INVALIDPARAMS);
	}

	if ((pSoundTrack->m_dwFlags & (PLAYSOUND_GETHANDLE | PLAYSOUND_LOOP)) != (PLAYSOUND_GETHANDLE | PLAYSOUND_LOOP))
	{
		RETURN_ERROR(1, CServerMgr::KillSoundLoop, LT_ERROR);
	}

	// Tell the clients to stop looping it.
	SetSoundTrackChangeFlags(this, pSoundTrack, CF_KILLSOUNDLOOP);

	pSoundTrack->m_fTimeLeft = 0.0f;
	pSoundTrack->SetRemove(LTTRUE);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00485f90
LTRESULT CServerMgr::KillSoundFade(HLTSOUND hSound, LTFLOAT fFadeOutTime)
{
	CSoundTrack *pSoundTrack;

	pSoundTrack = (CSoundTrack*)hSound;
	if (!pSoundTrack)
	{
		RETURN_ERROR(1, CServerMgr::KillSoundFade, LT_INVALIDPARAMS);
	}

	if (!(pSoundTrack->m_dwFlags & PLAYSOUND_GETHANDLE))
	{
		RETURN_ERROR(1, CServerMgr::KillSoundFade, LT_ERROR);
	}

	SetSoundTrackChangeFlags(this, pSoundTrack, CF_KILLSOUNDLOOP);

	pSoundTrack->m_fTimeLeft = 0.0f;
	pSoundTrack->SetRemove(LTTRUE);
	pSoundTrack->m_Unknown5C = fFadeOutTime;
	return LT_OK;
}


// ----------------------------------------------------------------------- //
// Misc.
// ----------------------------------------------------------------------- //

// Collects the client objects in a list.
// FUNCTION: LITHTECH 0x00486bc0
void sm_GetClientObjects(LTLink *pListHead, LTObject ***ppObjects, uint32 *pnObjects)
{
	LTLink *pCur;
	LTObject *pObj;

	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObj = (LTObject*)pCur->m_pData;
		if (!pObj->sd)
			break;

		if (pObj->sd->m_pClient)
		{
			(*ppObjects)[*pnObjects] = pObj;
			(*pnObjects)++;
		}
	}
}

// FUNCTION: LITHTECH 0x00486fd0
void CServerMgr::SetGlobalLightObject(HOBJECT hObject)
{
	LTLink *pCur;

	m_pGlobalLightObject = (LTObject*)hObject;

	// Tell the clients about it.
	for (pCur=m_Clients.m_Head.m_pNext; pCur != &m_Clients.m_Head; pCur=pCur->m_pNext)
	{
		((Client*)pCur->m_pData)->m_ClientFlags |= CFLAG_SENDGLOBALLIGHT;
	}
}


// FUNCTION: LITHTECH 0x004850b0
void CServerMgr::DoEndWorld(LTBOOL bKeepGeometryAround)
{
	LTLink *pCur, *pHead;
	Client *pClient;
	CPacket *pPacket;
	int i, j;

	// No longer consider world loaded.
	m_bTrackChanges = LTFALSE;

	// Reset the clients' outgoing packets.
	for (pCur = m_Clients.m_Head.m_pNext; pCur != &m_Clients.m_Head; pCur = pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;
		for (i=0; i < 2; i++)
		{
			for (j=0; j < 2; j++)
			{
				pPacket = pClient->m_PacketBufs[i][j].m_pPacket;
				pPacket->m_Pos = 1;
				pPacket->m_DataLen = 1;
			}
		}
	}

	// Get rid of any objects with client references laying around.  These are
	// the objects that got restored from a savegame but never had a client
	// take over them.
	sm_RemoveOldClientRefObjects(this);

	// Clear the client reference list (client references are only
	// valid while in the world/save file they came from).
	sm_ClearClientReferenceList(this);

	// All clients go into WAITINGTOENTERWORLD state.
	pHead = &m_Clients.m_Head;
	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pCur->m_pNext)
	{
		sm_SetClientState(this, (Client*)pCur->m_pData, CLIENT_WAITINGTOENTERWORLD);
	}

	// Disassociate clients from their objects.
	s_DisassociateClientsFromObjects(this);

	// Remove all objects from the world.
	sm_RemoveAllObjectsFromWorld(this, LTFALSE);

	// Delete the sound data and instances
	// This has to come after the removal of objects, cuz the objects have pointers to some sounds...
	sm_RemoveAllUnusedSoundTracks(this);

	m_World.ClearWorldData();
	if (!bKeepGeometryAround)
	{
		m_World.Term();
	}

	m_State = SERV_NOSTATE;
	sm_UncacheModels(this);

	dsi_ConsolePrint("World ended");
}

// FUNCTION: LITHTECH 0x004862e0
LTRESULT sm_CreateNewID(CServerMgr *pServerMgr, LTLink **pID)
{
	LTLink *pRet;
	ObjectMapEntry dRecord;

	if (pServerMgr->m_nAllocatedIDs + 1 >= MAX_OBJECTIDS)
		RETURN_ERROR(1, sm_CreateNewID, LT_ERROR);

	*pID = LTNULL;
	pRet = g_DLinkBank.Allocate();

	pRet->m_pData = (void*)pServerMgr->m_nAllocatedIDs;
	++pServerMgr->m_nAllocatedIDs;

	dl_Insert(&pServerMgr->m_FreeIDs, pRet);

	// Make sure the object map has space.
	dRecord.m_nRecordType = 0;
	dRecord.m_pRecordData = LTNULL;
	pServerMgr->m_ObjectMap.Append(dRecord);

	*pID = pRet;
	return LT_OK;
}


// Frees the cached models no object uses.
// FUNCTION: LITHTECH 0x004855b0
LTRESULT CServerMgr::FreeUnusedModels()
{
	LTLink *pCur;
	Model *pModel;
	HHashIterator *hIterator;
	HHashElement *hElement;

	// Mark the models the objects use.
	for (pCur=m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head.m_pNext; pCur != &m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head; pCur=pCur->m_pNext)
	{
		pModel = ((ModelInstance*)pCur->m_pData)->GetModelDB();
		if (pModel)
			pModel->m_Flags |= MODELFLAG_CACHED;
	}

	model_FreeUnusedChildModels();

	hIterator = hs_GetFirstElement(m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		pModel->m_RefCount++;
	}

	hIterator = hs_GetFirstElement(m_hModelTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		pModel = (Model*)hs_GetElementUserData(hElement);

		if (pModel->m_RefCount > 0)
			pModel->m_RefCount--;

		if (pModel->m_RefCount == 0 && !(pModel->m_Flags & MODELFLAG_CACHED))
		{
			DEBUG_PRINT(2, ("Debug: Server caching out model %s", pModel->GetFilename()));
			DEBUG_PRINT(3, ("Removing model from resource list: %s\n", pModel->GetFilename()));

			hs_RemoveElement(m_hModelTable, hElement);
			pModel->Delete();
		}
	}

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// World loading.
// ----------------------------------------------------------------------- //

// What the world loader gets (16 bytes).
struct WorldLoadInfo
{
				WorldLoadInfo()	{ Clear(); }

	void		Clear()
	{
		m_pStream = LTNULL;
		m_pUser = LTNULL;
		m_ProgressFn = LTNULL;
		m_UnknownC = 0;
	}

	ILTStream	*m_pStream;					// 0x00
	void		*m_pUser;					// 0x04
	void		(*m_ProgressFn)(uint32 percent);	// 0x08
	uint32		m_UnknownC;					// 0x0c
};

// Reports world loading progress (without a user pointer).
// FUNCTION: LITHTECH 0x00483910
static void sm_WorldLoadProgressCB(uint32 percent)
{
	dsi_LoadProgress(0);
}

// FUNCTION: LITHTECH 0x00483920
LTRESULT CServerMgr::LoadWorld(ILTStream *pStream, char *pWorldName)
{
	WorldLoadInfo loadInfo;

	m_World.Term();

	pStream->SeekTo(0);

	memset(&loadInfo, 0, sizeof(loadInfo));
	loadInfo.m_pStream = pStream;
	loadInfo.m_pUser = dsi_GetLoadUser();
	loadInfo.m_ProgressFn = sm_WorldLoadProgressCB;

	if (m_World.Load(&loadInfo) == LT_OK && m_World.LoadObjects(pStream) == LT_OK)
	{
		m_bTrackChanges = LTTRUE;
		return LT_OK;
	}

	m_World.Term();
	sm_SetupError(this, LT_INVALIDWORLDFILE, pWorldName);
	RETURN_ERROR_PARAM(1, CServerMgr::LoadWorld, LT_INVALIDWORLDFILE, pWorldName);
}


// ----------------------------------------------------------------------- //
// Portals and file caching.
// ----------------------------------------------------------------------- //

// The CPacketRef operators are inline calls still pending after each WriteType, so the two
// uint16 bodies stay out of line (0x004370c0) and only the last (uint8) one is inlined.
// FUNCTION: LITHTECH 0x00486920
LTRESULT sm_SetPortalFlags(CServerMgr *pServerMgr, const char *pPortalName, uint32 flags)
{
	uint32 iWorld, iPortal;
	BspPortal *pPortal;
	CPacketRef cPacket;

	pPortal = w_FindPortal(&pServerMgr->m_World, pPortalName, &iWorld, &iPortal);
	if (!pPortal)
	{
		RETURN_ERROR(1, ILTServer::SetPortalFlags, LT_NOTFOUND);
	}

	flags &= PORTAL_OPEN;
	if ((uint16)flags != pPortal->m_Flags)
	{
		// Tell the clients.
		cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		cPacket->WriteType((uint16)iWorld);
		cPacket->WriteType((uint16)iPortal);
		cPacket->WriteType((uint8)flags);
		sm_SendToAllClientsInWorld(g_pServerMgr, SMSG_PORTALFLAGS, cPacket);
	}

	pPortal->m_Flags = (uint16)flags;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00486c50
LTRESULT sm_CacheFile(CServerMgr *pServerMgr, uint32 fileType, char *pFilename)
{
	UsedFile *pFile;
	Model *pModel;
	CSoundData *pSoundData;

	if (!g_CV_CacheFiles)
		return LT_OK;

	if (!pFilename)
	{
		RETURN_ERROR(1, ILTServer::CacheFile, LT_ERROR);
	}

	switch (fileType)
	{
		case FT_MODEL:
		{
			if (se_GetModel(pServerMgr, pFilename, &pModel, &pFile, LTTRUE, LTTRUE) == LT_OK)
			{
				if (pServerMgr->m_InternalFlags & SFLAG_BUILDINGCACHELIST)
				{
					pModel->m_Flags |= MODELFLAG_CACHED;
					dsi_LoadProgress(1);
					return LT_OK;
				}

				if (pFile)
					sm_CacheSingleFile(pServerMgr, FT_MODEL, (uint16)pFile->m_FileID);
			}
			else
			{
				RETURN_ERROR_PARAM(1, ILTServer::CacheFile, LT_NOTFOUND, pFilename);
			}
		}
		break;

		case FT_SPRITE:
		case FT_TEXTURE:
		{
			if (sf_AddUsedFile(&pServerMgr->m_FileMgr, pFilename, 0, &pFile) == 0)
			{
				RETURN_ERROR_PARAM(1, ILTServer::CacheFile, LT_NOTFOUND, pFilename);
			}
			else if (pFile)
			{
				sm_CacheSingleFile(pServerMgr, (uint16)fileType, (uint16)pFile->m_FileID);
			}
		}
		break;

		case FT_SOUND:
		{
			if (sf_AddUsedFile(&pServerMgr->m_FileMgr, pFilename, 0, &pFile) == 0)
			{
				RETURN_ERROR_PARAM(1, ILTServer::CacheFile, LT_NOTFOUND, pFilename);
			}

			// Keep a copy of the data on the server side...
			if (pServerMgr->m_InternalFlags & SFLAG_BUILDINGCACHELIST)
			{
				pSoundData = pServerMgr->GetSoundData(pFile);
				if (pSoundData)
					pSoundData->m_bTouched = LTTRUE;
			}
			else if (pFile)
			{
				sm_CacheSingleFile(pServerMgr, FT_SOUND, (uint16)pFile->m_FileID);
			}
		}
		break;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00486e50
void sm_CacheSingleFile(CServerMgr *pServerMgr, uint16 fileType, uint16 fileID)
{
	uint32 i, newSize;
	OtherFile *pNew;
	LTLink *pCur, *pListHead;

	// Don't send duplicates..
	for (i=0; i < pServerMgr->m_CacheListSize; i++)
	{
		if (pServerMgr->m_CacheList[i].m_FileID == fileID)
			return;
	}

	// Expand the list if necessary
	if (pServerMgr->m_CacheListSize == pServerMgr->m_CacheListAllocedSize)
	{
		newSize = pServerMgr->m_CacheListAllocedSize + 20;
		pNew = (OtherFile*)dalloc(sizeof(OtherFile) * newSize);
		memcpy(pNew, pServerMgr->m_CacheList, sizeof(OtherFile) * pServerMgr->m_CacheListAllocedSize);
		dfree(pServerMgr->m_CacheList);
		pServerMgr->m_CacheList = pNew;
		pServerMgr->m_CacheListAllocedSize = newSize;
	}

	// Update the list
	pServerMgr->m_CacheList[pServerMgr->m_CacheListSize].m_FileID = fileID;
	pServerMgr->m_CacheList[pServerMgr->m_CacheListSize].m_FileType = fileType;
	pServerMgr->m_CacheListSize++;

	// Send a single update packet if we're not building a cache list
	if ((pServerMgr->m_InternalFlags & SFLAG_BUILDINGCACHELIST) == 0)
	{
		pListHead = &pServerMgr->m_Clients.m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			sm_SendCacheListToClient(pServerMgr, (Client*)pCur->m_pData, pServerMgr->m_CacheListSize - 1);
		}
	}
}


// Prints a message on all the clients' consoles.
// VC6 inlines CMoArray<uint8>::Insert2 (via WriteType/Append) here; the original calls it.
// Counterpart of packet_AddRef (an inline wrapper; the extra inline call site matters to the budget).
inline void packet_Release(CPacket *pPacket)
{
	pPacket->Release();
}

// FUNCTION: LITHTECH 0x004861a0
void BPrint(const char *pMsg, ...)
{
	char msg[500];
	va_list marker;

	if (!g_pServerMgr)
		return;

	va_start(marker, pMsg);
	_vsnprintf(msg, 499, pMsg, marker);
	va_end(marker);

	CPacket *pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));
	pPacket->WriteType((uint8)0);
	pPacket->WriteString(msg);
	sm_SendToAllClients(g_pServerMgr, SMSG_CONSOLETEXT, pPacket, MESSAGE_GUARANTEED);
	packet_Release(pPacket);
}


// Gets a packet for a game message (its ILTMessage reads and writes object references
// through m_pSerializeHelper).
// FUNCTION: LITHTECH 0x00486f60
CPacket* CServerMgr::AllocPacket()
{
	CPacket *pPacket;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));
	pPacket->m_Message.m_Unknown04 = (uint32)m_pSerializeHelper;
	pPacket->AddRef();
	pPacket->m_ErrorFlags |= PACKETFLAG_MESSAGE;
	pPacket->Release();
	return pPacket;
}

// FUNCTION: LITHTECH 0x00486fc0
void CServerMgr::SetupPacketMessage(CPacket *pPacket)
{
	pPacket->m_Message.m_Unknown04 = (uint32)m_pSerializeHelper;
}


// FUNCTION: LITHTECH 0x00486c10
void sm_SendToVisibleClientsCB(WorldTreeObj *pObj, void *pUser)
{
	SendToVisibleInfo *pInfo;
	Client *pClient;

	pInfo = (SendToVisibleInfo*)pUser;
	if (pObj->GetObjType() == WTObj_DObject)
	{
		pClient = ((LTObject*)pObj)->sd->m_pClient;
		if (pClient)
		{
			sm_SendToClient(pInfo->m_pServerMgr, pClient, pInfo->m_MsgID, pInfo->m_pPacket, pInfo->m_Flags);
		}
	}
}


// FUNCTION: LITHTECH 0x00485390
void sm_SendFileIOMessage(CServerMgr *pServerMgr, uint8 fileType, uint8 msgID, uint16 fileID, LTBOOL bTellLocal)
{
	CPacketRef cPacket;
	LTLink *pCur, *pListHead;
	Client *pClient;

	// Tell the clients to do the same.
	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		// (local clients will just inherit the model)
		if (bTellLocal || !(pClient->m_ClientFlags & CFLAG_LOCAL))
		{
			cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			cPacket->WriteType(fileType);
			cPacket->WriteType(fileID);
			SendToClient(pServerMgr, pClient, msgID, cPacket, LTFALSE, MESSAGE_GUARANTEED);
		}
	}
}


// FUNCTION: LITHTECH 0x00484aa0 ?DoStartWorld@CServerMgr@@QAEKPADKM@Z
LTRESULT CServerMgr::DoStartWorld(char *pWorldName, uint32 flags, float curTime)
{
	char fileName[256];
	char *pExt;
	LTBOOL bKeepGeometry, bSwitchingWorlds;
	LTRESULT dResult;
	ILTStream *pStream;
	UsedFile *pWorldFile;
	uint32 seed;

	LoadChildModelMap();

	// Seed the random number generators.
	seed = (uint32)time_GetTime();
	srand(seed);
	m_ClassMgr.m_pServerShell->SRand(seed);

	// Is it the same world (so we can keep the geometry)?
	bKeepGeometry = LTFALSE;
	if ((flags & LOADWORLD_KEEPGEOMETRY) && m_pWorldFile)
	{
		strncpy(fileName, sf_GetUsedFilename(&m_FileMgr, m_pWorldFile), 255);
		_strupr(fileName);

		pExt = strstr(fileName, ".DAT");
		if (pExt)
			*pExt = 0;

		if (stricmp(pWorldName, fileName) == 0)
			bKeepGeometry = LTTRUE;
	}

	bSwitchingWorlds = (m_State == SERV_RUNNINGWORLD);
	m_ClassMgr.m_pServerShell->PreStartWorld(bSwitchingWorlds);
	if (bSwitchingWorlds)
		DoEndWorld(bKeepGeometry);

	memset(m_SkyObjects, 0xFF, sizeof(m_SkyObjects));
	m_GameTime = 0.0f;
	m_pChangeListHead = LTNULL;
	m_ChangedSoundTrackHead = LTNULL;
	dl_TieOff(&m_RemovedObjectHead);

	// Open the world file.
	sprintf(fileName, "%s.dat", pWorldName);
	pStream = sf_OpenFile(&m_FileMgr, fileName);
	if (!pStream)
	{
		sm_SetupError(this, LT_MISSINGWORLDFILE, pWorldName);
		RETURN_ERROR_PARAM(1, CServerMgr::DoStartWorld, LT_MISSINGWORLDFILE, pWorldName);
	}

	if (!sf_AddUsedFile(&m_FileMgr, fileName, 1, &pWorldFile))
	{
		sm_SetupError(this, LT_MISSINGWORLDFILE, pWorldName);
		RETURN_ERROR_PARAM(1, CServerMgr::DoStartWorld, LT_MISSINGWORLDFILE, pWorldName);
	}

	model_SetWorldName(pWorldName);
	sm_LoadChildModelLinks(m_pServerInterface, pWorldName);

	if (bKeepGeometry && m_pWorldFile)
	{
		m_bTrackChanges = LTTRUE;
	}
	else
	{
		dResult = LoadWorld(pStream, pWorldName);
		if (dResult != LT_OK)
		{
			pStream->Release();
			return dResult;
		}
	}

	m_pWorldFile = pWorldFile;

	sm_StartCachingFiles(this, LTTRUE);
	InitWorldObjects();

	dResult = LoadObjects(this, pStream, pWorldName, flags & LOADWORLD_LOADWORLDOBJECTS);
	if (dResult != LT_OK)
	{
		dsi_ConsolePrint("FAILED to load world: %s", pWorldName);
		return dResult;
	}

	m_World.m_WorldFlags |= 1;
	m_World.m_pWorldStream = pStream;
	dsi_ConsolePrint("Loaded world: %s", pWorldName);

	// Reset the timing.
	m_nTargetTimeSteps = 0;
	m_TargetTimeBase = curTime;
	m_LastTime = curTime;
	m_TargetTime = 0.0f;
	m_FrameCode = 0;
	m_LastTargetTimeBase = 0.0f;
	m_GameTime = 0.0f;
	m_TimeOffset = 0.0f;
	m_FrameTime = 0.0f;

	if (flags & LOADWORLD_RUNWORLD)
		DoRunWorld();

	return LT_OK;
}


// WorldBsp members de_world.h doesn't have yet.
#define WORLDBSP_INFOFLAGS(pBsp)	(*(uint32*)((uint8*)(pBsp) + 0x48))		// WIF_
#define WORLDBSP_NUMSECTIONS(pBsp)	(*(uint32*)((uint8*)(pBsp) + 0x140))	// terrain sections

#define WIF_TERRAIN			(1<<3)
#define WIF_PHYSICSBSP		(1<<4)
#define WIF_VISBSP			(1<<5)

// Makes a VisContainer object for each physics/vis BSP (and terrain section).
// FUNCTION: LITHTECH 0x004856e0
LTBOOL CServerMgr::InitWorldObjects()
{
	ObjectCreateStruct ocs;
	CClassData *pClass;
	WorldData *pWorldData;
	uint32 i, j;

	// Hmmm... kinda messy..
	pClass = m_ClassMgr.FindClassData("VisContainer");
	if (!pClass)
		return LTFALSE;

	for (i=0; i < m_World.m_WorldModels.GetSize(); i++)
	{
		pWorldData = m_World.m_WorldModels[i];

		if (WORLDBSP_INFOFLAGS(pWorldData->m_pOriginalBsp) & WIF_TERRAIN)
		{
			for (j=0; j < WORLDBSP_NUMSECTIONS(pWorldData->m_pOriginalBsp); j++)
			{
				ocs.Clear();
				ocs.m_ObjectType = OT_WORLDMODEL;
				ocs.m_Flags = FLAG_SOLID | FLAG_RAYHIT;
				if (!w_MakeSpecialName(pWorldData->m_pOriginalBsp->m_WorldName, j, ocs.m_Filename, sizeof(ocs.m_Filename)))
					return LTFALSE;

				if (!EZCreateObject(pClass, &ocs))
					return LTFALSE;
			}
		}
		else if (WORLDBSP_INFOFLAGS(pWorldData->m_pOriginalBsp) & (WIF_PHYSICSBSP | WIF_VISBSP))
		{
			// Ok, make an object for it.
			ocs.Clear();
			ocs.m_ObjectType = OT_WORLDMODEL;
			ocs.m_Flags = FLAG_SOLID | FLAG_RAYHIT;
			SAFE_STRCPY(ocs.m_Filename, pWorldData->m_pValidBsp->m_WorldName);

			if (!EZCreateObject(pClass, &ocs))
				return LTFALSE;
		}
	}

	return LTTRUE;
}


// Handles a client's peer to peer authentication packet (the server side of the exchange).
// FUNCTION: LITHTECH 0x00487010
void CServerMgr::OnPeerToPeerAuthPacket(Client *pClient, CPacket *pPacket)
{
	PeerAuthServer authServer;
	char buf[512];
	uint32 len;

	memset(buf, 0, sizeof(buf));

	if (!g_pServerAuthContext.get())
	{
		g_pServerAuthContext = (AuthContext*)g_pClassMgr->m_pServerShell->GetAuthContext();
		if (!g_pServerAuthContext.get())
		{
			// No auth context: let the client carry on without authentication.
			CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			cPacket->m_Data[0] = SMSG_PEERAUTH;
			m_NetMgr.SendPacket(cPacket, pClient->m_ConnectionID, MESSAGE_GUARANTEED);
			return;
		}
	}

	if (authServer.GetState() == PeerAuthServer::STATE_NOT_STARTED)
	{
		authServer.SetUseAuth2(true);
		authServer.Start(g_pServerAuthContext->GetPeerData(), 4);
	}

	len = pPacket->m_DataLen - pPacket->m_Pos;
	pPacket->ReadRaw(buf, (uint16)len);

	ByteBufferPtr outMsg;

	if (authServer.HandleRecvMsg(buf + 4, len - 4, outMsg) == WS_Success)
	{
		if (pClient->m_Unknown3E0 == 1)
			pClient->m_Unknown3E4 = 1;
		else
			pClient->m_Unknown3E0 = 1;
	}

	if (outMsg.get())
	{
		CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		cPacket->WriteRaw((void*)outMsg->data(), (uint16)outMsg->length());
		cPacket->m_Data[0] = SMSG_PEERAUTH;
		m_NetMgr.SendPacket(cPacket, pClient->m_ConnectionID, MESSAGE_GUARANTEED);
	}
}

// Template code the peer auth leaves behind.
// FUNCTION: LITHTECH 0x004872f0 ?Release@RefCount@WONAPI@@QAEXXZ
// FUNCTION: LITHTECH 0x00487310 ??1Blowfish@WONAPI@@QAE@XZ

// Template code this object instantiated first (STLport string/map/set nodes of the child model
// link map and of sm_UncacheModels).
// FUNCTION: LITHTECH 0x004873c0 ??0?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00487400 ??1?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x004874c0 ??1?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00487560 ?insert@?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@_STL@@QAE?AU?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@2@U32@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@2@@Z
// FUNCTION: LITHTECH 0x00487730 ??0?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x004877a0 ?insert@?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@QAE?AU?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@U32@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@@Z
// FUNCTION: LITHTECH 0x00487970 ??1?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00487a00 ??0?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@QAE@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@1@0@Z
// FUNCTION: LITHTECH 0x00487a90 ??0?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@QAE@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@1@ABV?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@1@@Z
// FUNCTION: LITHTECH 0x00487b90 ??0?$_Rb_tree_base@PAVModel@@V?$allocator@PAVModel@@@_STL@@@_STL@@QAE@ABV?$allocator@PAVModel@@@1@@Z
// FUNCTION: LITHTECH 0x00487d10 ?_M_empty_initialize@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@AAEXXZ
// FUNCTION: LITHTECH 0x00487d30 ?_M_erase@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@_STL@@AAEXPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x00487dd0 ??1?$_Rb_tree_base@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00487e00 ?lower_bound@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@_STL@@QAE?AU?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@2@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@2@@Z
// FUNCTION: LITHTECH 0x00487e90 ?_M_copy@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@AAEPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@PAU32@0@Z
// FUNCTION: LITHTECH 0x00487f70 ?_M_erase@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@AAEXPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x00487fe0 ?_M_erase@?$_Rb_tree@PAVModel@@PAV1@U?$_Identity@PAVModel@@@_STL@@U?$less@PAVModel@@@3@V?$allocator@PAVModel@@@3@@_STL@@AAEXPAU?$_Rb_tree_node@PAVModel@@@2@@Z
// FUNCTION: LITHTECH 0x00488140 ?insert_unique@?$_Rb_tree@PAVModel@@PAV1@U?$_Identity@PAVModel@@@_STL@@U?$less@PAVModel@@@3@V?$allocator@PAVModel@@@3@@_STL@@QAE?AU?$pair@U?$_Rb_tree_iterator@PAVModel@@U?$_Nonconst_traits@PAVModel@@@_STL@@@_STL@@_N@2@ABQAVModel@@@Z
// FUNCTION: LITHTECH 0x004882d0 ?destroy_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@IAEXPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x00488330 ?destroy_node@?$_Rb_tree@PAVModel@@PAV1@U?$_Identity@PAVModel@@@_STL@@U?$less@PAVModel@@@3@V?$allocator@PAVModel@@@3@@_STL@@IAEXPAU?$_Rb_tree_node@PAVModel@@@2@@Z
// FUNCTION: LITHTECH 0x00488460 ?_M_create_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@IAEPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@@Z
// FUNCTION: LITHTECH 0x00488560 ?_M_insert@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?d08100ff
// FUNCTION: LITHTECH 0x00488740 ?insert_unique@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$les4e13f51b
// FUNCTION: LITHTECH 0x00488900 ?_M_insert@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@AAE?AU?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@PAU_Rb_tree_node_base@2@0ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@@Z
// FUNCTION: LITHTECH 0x00488ae0 ?insert_unique@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@QAE?AU?$pair@U?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@_N@2@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@2@@Z
// FUNCTION: LITHTECH 0x00488ca0 ?_M_assign_dispatch@?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@AAEAAV12@PBD0U__false_type@@@Z
// FUNCTION: LITHTECH 0x00488e00 ?_Construct@_STL@@YAXPAU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@1@ABU21@@Z
// FUNCTION: LITHTECH 0x00488eb0 ??_G?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x00488f70 ?_M_acquire_lock@_STL_mutex_base@_STL@@QAEXXZ
// FUNCTION: LITHTECH 0x00489060 ?_M_release_lock@_STL_mutex_base@_STL@@QAEXXZ
// FUNCTION: LITHTECH 0x00489070 ??_G?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x004890f0 ?_M_create_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@@_STL@@IAEPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@@2@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@2@@Z
// FUNCTION: LITHTECH 0x00489240 ??0?$_Rb_tree_base@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@_STL@@QAE@ABV?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@1@@Z
// FUNCTION: LITHTECH 0x004893c0 ??0?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@QAE@ABU01@@Z
// FUNCTION: LITHTECH 0x00489460 ?_M_create_node@?$_Rb_tree@PAVModel@@PAV1@U?$_Identity@PAVModel@@@_STL@@U?$less@PAVModel@@@3@V?$allocator@PAVModel@@@3@@_STL@@IAEPAU?$_Rb_tree_node@PAVModel@@@2@ABQAVModel@@@Z
// FUNCTION: LITHTECH 0x00489500 ?_Construct@_STL@@YAXPAU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@1@ABU21@@Z
// FUNCTION: LITHTECH 0x00489610 ??0?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@@_STL@@@2@@2@@_STL@@QAE@ABU01@@Z
