// Jupiter runtime/client/src/clientshell.cpp
// Talon: the shell keeps its CClientMgr (m_pClientMgr) and its local CServerMgr (m_pServerMgr)
// instead of holders, the world is the client manager's MainWorld, world textures can be sprites
// (SurfaceSprite) and surface effects animate texture coordinates every frame.
#include <windows.h>
#undef PlaySound
#include <string.h>
#include "bdefs.h"
#include "clientshell.h"
#include "clientmgr.h"
#include "servermgr.h"
#include "iclientshell.h"
#include "counter.h"
#include "console.h"
#include "consolecommands.h"
#include "concommand.h"
#include "dhashtable.h"
#include "client_filemgr.h"
#include "setupobject.h"
#include "render.h"
#include "renderstruct.h"
#include "moveobject.h"
#include "packet.h"
#include "soundmgr.h"
#include "dsys_interface.h"
#include "nexus.h"
#include "model.h"
#include "de_memory.h"
#include "iltclient.h"
#include "predict.h"
#include "engine_vars.h"
#include "s_client.h"

#define CMSG_COMMANDSTRING	10
#define OBJID_CLIENTCREATED	0xFFFF
#define UPDATEFLAG_NONACTIVE	(1<<0)
#define MESSAGE_GUARANTEED	(1<<7)

#define TYPECODE_WORLD		0
#define TYPECODE_MODEL		1


// GLOBAL: LITHTECH 0x004deff8
CClientShell *g_pClientShell;
// The local server's world (s_client.h).
// GLOBAL: LITHTECH 0x004deff4
MainWorld *g_pServerWorld;

// Profiling (named after the PlayDemo profile, demomgr.cpp).
// GLOBAL: LITHTECH 0x004df000
uint32 g_Ticks_NetUpdate = 0;
// GLOBAL: LITHTECH 0x004df004
uint32 g_Ticks_ServerUpdate = 0;
// GLOBAL: LITHTECH 0x004df008
uint32 g_Ticks_ProcessPackets = 0;
// GLOBAL: LITHTECH 0x004df00c
uint32 g_Ticks_GameClientShell = 0;

// clientmgr.cpp
void cm_OnExitServer(CClientMgr *pClientMgr, CClientShell *pShell);						// 0x004120d0
void cm_RemoveObjectsInList(CClientMgr *pClientMgr, LTList *pList, LTBOOL bServerOnly);	// 0x004128b0
void cm_FreeSurfaceSprites(CClientMgr *pClientMgr);									// 0x00412900
LTRESULT cm_AddObjectToClientWorld(CClientMgr *pClientMgr, uint16 objectID,
	InternalObjectSetup *pSetup, LTObject **ppObject, LTBOOL bMove, LTBOOL bRotate);	// 0x004126a0
// The load progress hook (identical-code folded with con_ExhaustMemory, 0x00473ac0).
void cm_ShowLoadProgress(CClientMgr *pClientMgr);
// cutil.cpp
SharedTexture* cm_AddSharedTexture(CClientMgr *pClientMgr, FileRef *pRef);				// 0x00426050
void cm_TagAndFreeTextures(CClientMgr *pClientMgr);										// 0x00426380
void cm_BindUnboundTextures(CClientMgr *pClientMgr);									// 0x004263b0
// sprite.cpp
LTRESULT LoadSprite(CClientMgr *pClientMgr, FileRef *pRef, Sprite **ppSprite);			// 0x00489710
// nexus.cpp
Leech* nexus_CreateLeech(LeechDef *pDef, void *pUserData);								// 0x004448f0
LTRESULT nexus_AddLeech(Nexus *pNexus, Leech *pLeech);									// 0x00444970

void DetachObjectStanding(LTObject *pObj);			// 0x0045d110
void DetachObjectsStandingOn(LTObject *pObj);		// 0x0045d150

class FileIDInfo;

// What the world loader gets (16 bytes; servermgr.cpp has the same).
struct WorldLoadInfo
{
				WorldLoadInfo()	{ Clear(); }

	void		Clear()
	{
		m_pStream = LTNULL;
		m_pUser = LTNULL;
		m_ProgressFn = LTNULL;
		m_pUser2 = LTNULL;
	}

	ILTStream	*m_pStream;					// 0x00
	void		*m_pUser;					// 0x04
	void		(*m_ProgressFn)(void *pUser);	// 0x08
	void		*m_pUser2;					// 0x0c
};


// ------------------------------------------------------------------ //
// Static functions..
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004148f0 _$E1
// FUNCTION: LITHTECH 0x004148e0 _$E2
static inline CONCOLOR ShellColor(uint32 r, uint32 g, uint32 b)
{
	return CONRGB(r, g, b);
}

// GLOBAL: LITHTECH 0x004deffc
CONCOLOR g_ShellMsgColor = ShellColor(0, 255, 0);


static void AddAllObjectsToBSP(CClientShell *pShell, LTList *pList);


// FUNCTION: LITHTECH 0x00416270
static void RemoveAllObjectsFromWorldTree(CClientShell *pShell, LTList *pList)
{
	LTLink *pCur, *pListHead;
	LTObject *pObj;

	pListHead = &pList->m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObj = (LTObject*)pCur->m_pData;
		pObj->RemoveFromWorldTree();
	}
}


////////////////////////////////////////////////////////
// ClientShell functions
////////////////////////////////////////////////////////

// FUNCTION: LITHTECH 0x00414900
CClientShell::CClientShell()
{
	uint32 i;

	m_pLastWorld = LTNULL;
	m_bWorldOpened = LTFALSE;
	m_GameTime = m_LastGameTime = m_GameFrameTime = 0.0f;
	m_Unknown74 = 0;

	m_HostID = LTNULL;

	m_pDriver = LTNULL;

	m_KillTag = 0;
	m_pServerMgr = LTNULL;
	m_ClientID = (uint16)-1;

	m_ClientObjectID = (uint16)-1;
	dl_TieOff(&m_MovingObjects);
	dl_TieOff(&m_RotatingObjects);
	m_pFrameClientObject = LTNULL;

	for (i=0; i < 16; i++)
	{
		m_ColorSignExtend[i] = (uint8)(((float)i * 255.0f) / 15.0f);
	}
}

// FUNCTION: LITHTECH 0x00414990 ??_GCClientShell@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x004149b0
CClientShell::~CClientShell()
{
	Term();
}

// FUNCTION: LITHTECH 0x004149d0 ??_GCNetHandler@@UAEPAXI@Z


// FUNCTION: LITHTECH 0x004149f0
LTBOOL CClientShell::Init(CClientMgr *pClientMgr)
{
	m_pClientMgr = pClientMgr;

	// Setup net stuff.
	InitHandlers();
	m_pClientMgr->m_NetMgr.m_pHandler = this;
	g_pClientShell = this;

	// Initialize the fileid info list.  Some information sent to the client doesn't change based on fileid.
	// This is used to reduce the amount of info sent to the client, by just comparing the new info to what
	// was sent last.  As an example, sound radii is typically the same for one particular file, so the server
	// only needs to send this info once.
	m_hFileIDTable = hs_CreateHashTable(100, 0);

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00414a30
void CClientShell::Term()
{
	HHashIterator *hIterator;
	HHashElement *hElement;

	// Let the client manager do its thing..
	cm_OnExitServer(m_pClientMgr, this);

	if (m_pClientMgr->m_NetMgr.m_Connections.GetSize() > 0)
		SendGoodbye();

	m_pClientMgr->m_NetMgr.Disconnect(m_HostID, 0);
	m_HostID = LTNULL;

	m_pClientMgr->m_NetMgr.m_pHandler = LTNULL;

	// Uninit the server mugger if there is one (CServerMgr's destructor is virtual through CNetHandler).
	if (m_pServerMgr)
	{
		delete (CNetHandler*)m_pServerMgr;
		m_pServerMgr = LTNULL;
	}

	// Uninit object stuff.
	RemoveAllObjects();

	// Get rid of the world references because the (local) server will
	// delete the world.
	NotifyWorldClosing();
	CloseWorlds();

	m_pDriver = LTNULL;
	m_ClientObjectID = (uint16)-1;

	m_pFrameClientObject = LTNULL;

	// Free the fileid info structures...
	hIterator = hs_GetFirstElement(m_hFileIDTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		if (hElement)
			sb_Free(&m_pClientMgr->m_FileIDInfoBank, hs_GetElementUserData(hElement));
	}
	hs_DestroyHashTable(m_hFileIDTable);

	g_pClientShell = LTNULL;
}


// FUNCTION: LITHTECH 0x00414b20
LTRESULT CClientShell::StartupClient(CBaseDriver *pDriver)
{
	m_pDriver = pDriver;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00414b30
LTRESULT CClientShell::StartupLocal(StartGameRequest *pRequest, LTBOOL bHost, CBaseDriver *pServerDriver)
{
	LTRESULT dResult;
	CBaseDriver *pServerLocalDriver;
	char errorString[256];

	// Setup a server mugger.
	if ((dResult = CreateServerMgr()) != LT_OK)
		return dResult;

	// Give it the game info data.
	if (pRequest->m_pGameInfo && pRequest->m_GameInfoLen > 0)
	{
		m_pServerMgr->SetGameInfo(pRequest->m_pGameInfo, pRequest->m_GameInfoLen);
	}

	// Add the resources to it and listen locally by default.
	if (!m_pServerMgr->AddResources((char**)m_pClientMgr->m_ResTrees, m_pClientMgr->m_nResTrees))
	{
		m_pServerMgr->GetErrorString(errorString, 256);
		RETURN_ERROR_PARAM(1, CClientShell::StartupLocal, LT_SERVERERROR, errorString);
	}

	// Load the object.lto file.
	if (!m_pServerMgr->LoadBinaries())
	{
		m_pServerMgr->GetErrorString(errorString, 256);
		RETURN_ERROR_PARAM(1, CClientShell::StartupLocal, LT_SERVERERROR, errorString);
	}

	if (!m_pServerMgr->Listen("local", "Metaltek Session"))
	{
		m_pServerMgr->GetErrorString(errorString, 256);
		RETURN_ERROR_PARAM(1, CClientShell::StartupLocal, LT_SERVERERROR, errorString);
	}

	m_bLocal = LTTRUE;

	// Give the server the CBaseDriver we have setup.
	if (pServerDriver)
	{
		m_pServerMgr->TransferNetDriver(pServerDriver);
	}

	// Setup a local driver.
	con_PrintString(g_ShellMsgColor, 1, "Connecting locally");
	m_pDriver = m_pClientMgr->m_NetMgr.AddDriver("local");

	// Connect locally.
	pServerLocalDriver = m_pServerMgr->GetLocalDriver();
	m_pDriver->LocalConnect(pServerLocalDriver);

	// Sort of a hack.. new connection notifications don't come in  (and thus
	// m_HostID doesn't get set) until you do an update.
	m_pClientMgr->m_NetMgr.Update("Client: ", m_pClientMgr->m_CurTime, LTTRUE);

	return LT_OK;
}


// The original inlines the whole CServerMgr constructor (inline in servermgr.h). MotionInfo's empty constructor
// needs __forceinline (motion.h). The member constructors after CClassMgr (LTList, CMoArray, SkyDef, ObjectBank)
// stay out of line only because of the GetAppGuid() accessor at the end: one pending inline site after the
// constructor halves the budget share of the constructor's own call sites.
// FUNCTION: LITHTECH 0x00414cd0
LTRESULT CClientShell::CreateServerMgr()
{
	m_pServerMgr = new CServerMgr;
	if (!m_pServerMgr || !m_pServerMgr->Init())
	{
		if (m_pServerMgr)
			delete (CNetHandler*)m_pServerMgr;
		m_pServerMgr = LTNULL;

		RETURN_ERROR(1, CClientShell::CreateServerMgr, LT_CANTCREATESERVER);
	}

	if (m_pClientMgr)
		m_pServerMgr->m_NetMgr.SetAppGuid(m_pClientMgr->m_NetMgr.GetAppGuid());

	return LT_OK;
}

// 0x00414e80 is CServerMgr's scalar deleting destructor (the destructor is inline in servermgr.h).


static void UpdateSurfaceEffects(CClientShell *pShell);

// FUNCTION: LITHTECH 0x004150e0
LTRESULT CClientShell::Update()
{
	long updateFlags;
	char errorString[256];
	LTCommandVar *pVar;
	LTObject *pClientObject;
	LTRESULT dResult;
	IClientShell *pClientShell;

	pClientShell = m_pClientMgr->m_pClientShell;

	{
		CountAdder cntAdd(&g_Ticks_NetUpdate);
		m_pClientMgr->m_NetMgr.Update("Client: ", m_pClientMgr->m_CurTime, LTTRUE);
	}

	{
		CountAdder cntAdd(&g_Ticks_FrameServer);
		CountAdder cntAdd2(&g_Ticks_ServerUpdate);

		// Update our local server manager if it's there.
		// (If the update fails an error will be returned.)
		if (m_pServerMgr && g_bUpdateServer)
		{
			updateFlags = 0;
			if (!dsi_IsClientActive())
			{
				// Console variable 'alwaysfocused' overrides this.
				pVar = cc_FindConsoleVar(&g_ClientConsoleState, "alwaysfocused");
				if (!pVar || (pVar->floatVal != 1.0f))
				{
					updateFlags |= UPDATEFLAG_NONACTIVE;
				}
			}

			if (!m_pServerMgr->Update(updateFlags, m_pClientMgr->m_CurTime))
			{
				m_pServerMgr->GetErrorString(errorString, 256);
				m_pClientMgr->SetupError(LT_SERVERERROR, errorString);
				RETURN_ERROR_PARAM(1, CClientShell::Update, LT_SERVERERROR|ERROR_DISCONNECT, errorString);
			}
		}
	}

	m_GameFrameTime = 0.0f;

	{
		// Process all packets.
		CountAdder cntAdd(&g_Ticks_FrameNet);
		CountAdder cntAdd2(&g_Ticks_ProcessPackets);

		dResult = ProcessPackets();
		if (dResult != LT_OK)
			return dResult | ERROR_DISCONNECT;
	}

	// Update the game frame time.
	m_GameFrameTime = m_GameTime - m_LastGameTime;
	m_LastGameTime = m_GameTime;

	// Call PreUpdate and Update functions.
	if (dsi_IsClientActive())
	{
		// Give the predictive views some time!
		pd_Update(this);
	}

	{
		CountAdder cntAdd(&g_Ticks_FrameClientShell);
		CountAdder cntAdd2(&g_Ticks_GameClientShell);

		pClientShell->PreUpdate();
		pClientShell->Update();

		// The shell may have been shut down during its update.
		if (this != g_pClientMgr->m_pCurShell && (void*)this != (void*)g_pClientMgr->m_pClientShell)
			return LT_OK;
	}

	if (dsi_IsClientActive())
	{
		// Update all the client object structures from their script counterparts.
		m_pClientMgr->UpdateObjects();
		UpdateSurfaceEffects(this);
	}

	{
		CountAdder cntAdd(&g_Ticks_FrameClientShell);
		CountAdder cntAdd2(&g_Ticks_GameClientShell);

		pClientShell->PostUpdate();
	}

	// Update client obect pointer...
	pClientObject = GetClientObject();
	if (pClientObject)
	{
		if (m_pFrameClientObject != pClientObject)
		{
			m_pFrameClientObject = pClientObject;
		}
	}

	return LT_OK;
}


// Runs the surface effects (ILTClient::AddSurfaceEffect) and recalculates the texture
// coordinates of their polies. pSurface and pBsp only cover the effect call: afterwards the original rereads
// pInst->m_pSurface and pInst->m_pBsp.
// FUNCTION: LITHTECH 0x004154f0
static void UpdateSurfaceEffects(CClientShell *pShell)
{
	CClientMgr *pClientMgr;
	SurfaceEffectInst *pInst;
	Surface *pSurface;
	WorldBsp *pBsp;
	WorldPoly *pPoly;
	SPolyVertex *pVert, *pEnd;
	SurfaceData data;
	float fPOffset, fQOffset;
	uint16 iPoly;

	pClientMgr = pShell->m_pClientMgr;
	for (pInst=pClientMgr->m_World.m_pSurfaceEffects; pInst; pInst=pInst->m_pNext)
	{
		pSurface = pInst->m_pSurface;
		pBsp = pInst->m_pBsp;
		data.O = pSurface->O;
		data.P = pSurface->P;
		data.Q = pSurface->Q;
		data.m_pInternalWorld = &pClientMgr->m_World;
		data.m_pInternalWorldBsp = pBsp;
		data.m_pInternalSurface = pSurface;
		pInst->m_pEffect->UpdateEffect(&data, pInst->m_pData);

		pSurface = pInst->m_pSurface;
		pSurface->O = data.O;
		pSurface->P = data.P;
		pSurface->Q = data.Q;

		fPOffset = data.O.Dot(data.P);
		fQOffset = data.O.Dot(data.Q);

		iPoly = pSurface->m_iFirstPoly;
		while (iPoly != 0xFFFF)
		{
			pPoly = pInst->m_pBsp->m_Polies[iPoly];

			pVert = (SPolyVertex*)(pPoly + 1);
			pEnd = &pVert[pPoly->GetNumVertices()];
			for (; pVert != pEnd; pVert++)
			{
				pVert->m_U = pVert->m_Vec->Dot(pSurface->P) - fPOffset;
				pVert->m_V = pVert->m_Vec->Dot(pSurface->Q) - fQOffset;
			}

			iPoly = pPoly->m_iNextSurfacePoly;
		}
	}
}


// FUNCTION: LITHTECH 0x004156f0
void CClientShell::SendCommandToServer(char *pCommand)
{
	CPacketRef cPacket;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	cPacket->m_Data[0] = CMSG_COMMANDSTRING;
	cPacket->WriteString(pCommand);

	SendPacketToServer(cPacket);
}


// FUNCTION: LITHTECH 0x00415750
void CClientShell::SendPacketToServer(CPacket *pPacket)
{
	m_pClientMgr->m_NetMgr.SendPacket(pPacket, m_HostID, MESSAGE_GUARANTEED);
}



////////////////////////////////////////////////////////
// Main high-level functions
////////////////////////////////////////////////////////

// FUNCTION: LITHTECH 0x00415770
void CClientShell::RemoveAllObjects()
{
	int i;

	// Remove all the server objects.
	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		cm_RemoveObjectsInList(m_pClientMgr, &m_pClientMgr->m_ObjectMgr.m_ObjectLists[i], LTTRUE);
	}
}


// Extern: a static callback would be emitted after DoLoadWorld, which takes its address.
// FUNCTION: LITHTECH 0x004157a0
void cs_WorldLoadProgress(void *pUser)
{
	cm_ShowLoadProgress((CClientMgr*)pUser);
}


void cs_UnloadWorld(CClientShell *pShell);
static void SetWorldPolyTexturePointers(CClientMgr *pClientMgr, MainWorld *pWorld);

// The packet carries the info string's CRC before the world file's. WorldLoadInfo is cleared again (memset, as in
// CServerMgr::LoadWorld) right before the load, and the load failure is the else branch (the original puts it at the
// end of the function).
// FUNCTION: LITHTECH 0x004157b0
LTRESULT CClientShell::DoLoadWorld(CPacket *pPacket, LTBOOL bLocal)
{
	int i;
	ILTStream *pStream;
	FileRef ref;
	FileIdentifier *pIdent;
	const char *pWorldName;
	char *pInfoString;
	uint32 worldVersion, fileCRC, infoCRC;
	LTBOOL bFlushUnusedTextures;
	MainWorld *pServerWorld;
	MainWorld *pWorld;
	WorldLoadInfo loadInfo;

	// Cleanup...
	cs_UnloadWorld(this);

	// Get the game time.
	m_GameTime = m_LastGameTime = pPacket->ReadType((float*)0);
	m_GameFrameTime = 0.0f;

	m_ServerPeriodTrack = m_pClientMgr->m_CurTime;
	m_ServerPeriod = 1.0f / 30.0f;

	// Get the world file ID and world pointer.
	ref.m_FileType = FILE_SERVERFILE;
	ref.m_FileID = pPacket->ReadType((uint16*)0);
	pWorldName = cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref);
	pIdent = cf_GetFileIdentifier(m_pClientMgr->m_hFileMgr, &ref, TYPECODE_WORLD);

	worldVersion = pPacket->ReadType((uint32*)0);
	pInfoString = pPacket->ReadString();
	infoCRC = pPacket->ReadType((uint32*)0);
	fileCRC = pPacket->ReadType((uint32*)0);

	if (worldVersion != 0)
	{
		if (worldVersion == 1)
		{
			if (m_pClientMgr->m_pClientShell)
				m_pClientMgr->m_pClientShell->SetDisconnectCode(8, cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref));
		}
		else if (worldVersion == 0x51)
		{
			if (m_pClientMgr->m_pClientShell)
				m_pClientMgr->m_pClientShell->SetDisconnectCode(0x10000, cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref));
		}

		RETURN_ERROR(1, CClientShell::DoLoadWorld, LT_ERROR);
	}

	// If we're local and it's the same world, don't reload all the textures.
	bFlushUnusedTextures = LTTRUE;
	if (pIdent == m_pLastWorld)
		bFlushUnusedTextures = LTFALSE;

	pServerWorld = g_pServerWorld;

	//Init the prediction stuff with the server's time.
	pd_InitialServerUpdate(this, m_GameTime);

	pWorld = &m_pClientMgr->m_World;

	//check if we have a local server.
	if (m_bLocal)
	{
		if (!pServerWorld)
			RETURN_ERROR(1, ClientShell::DoLoadWorld, LT_ERROR);

		//inherit our client world from the server world.
		if (!pWorld->InheritFrom(pServerWorld))
			RETURN_ERROR_PARAM(1, ClientShell::DoLoadWorld, LT_ERROR, "Inherit failed");
	}
	else
	{
		uint32 checkVal;

		// Notify the client shell so they can put up a bitmap.
		if (m_pClientMgr->m_pClientShell)
			m_pClientMgr->m_pClientShell->PreLoadWorld((char*)pWorldName);

		pStream = cf_OpenFile(m_pClientMgr->m_hFileMgr, &ref);
		if (!pStream)
		{
			if (m_pClientMgr->m_pClientShell)
				m_pClientMgr->m_pClientShell->SetDisconnectCode(1, cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref));

			RETURN_ERROR(1, CClientShell::DoLoadWorld, LT_MISSINGWORLDFILE);
		}

		m_pClientMgr->m_pClientDE->Common()->GetCRC(pStream, checkVal);
		if (checkVal != fileCRC)
		{
			if (m_pClientMgr->m_pClientShell)
				m_pClientMgr->m_pClientShell->SetDisconnectCode(6, cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref));

			RETURN_ERROR(1, CClientShell::DoLoadWorld, LT_CRCWORLDCHECKFAILED);
		}

		m_pClientMgr->GetWorldInfoCRC(pInfoString, checkVal);
		if (checkVal != infoCRC)
		{
			if (m_pClientMgr->m_pClientShell)
				m_pClientMgr->m_pClientShell->SetDisconnectCode(7, cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref));

			RETURN_ERROR(1, CClientShell::DoLoadWorld, LT_CRCMISCCHECKFAILED);
		}

		con_Printf(CONRGB(250,100,100), 1, "Entering world %s", cf_GetFilename(m_pClientMgr->m_hFileMgr, &ref));

		pStream->SeekTo(0);

		memset(&loadInfo, 0, sizeof(loadInfo));
		loadInfo.m_pStream = pStream;
		loadInfo.m_pUser = (void*)m_pClientMgr->m_Unknown12e8;
		loadInfo.m_ProgressFn = cs_WorldLoadProgress;
		loadInfo.m_pUser2 = m_pClientMgr;
		if (pWorld->Load(&loadInfo) == LT_OK)
		{
			pWorld->m_pWorldStream = pStream;
			pWorld->LoadObjects(pStream);
		}
		else
		{
			m_pClientMgr->SetupError(LT_INVALIDWORLDFILE, pWorldName);
			RETURN_ERROR_PARAM(1, CClientShell::DoLoadWorld, LT_INVALIDWORLDFILE, pWorldName);
		}
	}

	// Try to bind to the worlds.
	if (!BindWorlds())
	{
		RETURN_ERROR(1, CClientShell::DoLoadWorld, LT_ERRORBINDINGWORLD);
	}

	// Get rid of unused shared textures.
	if (bFlushUnusedTextures)
	{
		cm_TagAndFreeTextures(m_pClientMgr);
	}

	// Set poly texture pointers.
	SetWorldPolyTexturePointers(m_pClientMgr, pWorld);

	// Bind textures so we're ready to go.
	cm_BindUnboundTextures(m_pClientMgr);

	// Create objects for all the vis containers.
	if (!CreateVisContainerObjects())
	{
		CloseWorlds();
		RETURN_ERROR_PARAM(1, CClientShell::DoLoadWorld, LT_ERROR, "CreateVisContainerObjects failed");
	}

	// Add all objects back into the BSP.
	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		AddAllObjectsToBSP(this, &m_pClientMgr->m_ObjectMgr.m_ObjectLists[i]);
	}

	m_pLastWorld = pIdent;

	// Call the OnEnterWorld function.
	m_pClientMgr->OnEnterWorld(this);
	m_bWorldOpened = LTTRUE;

	return LT_OK;
}


static void SetPolyTexturePointers(CClientMgr *pClientMgr, WorldBsp *pBsp);

// FUNCTION: LITHTECH 0x00415fb0
static void SetWorldPolyTexturePointers(CClientMgr *pClientMgr, MainWorld *pWorld)
{
	uint32 i;

	for (i=0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		SetPolyTexturePointers(pClientMgr, pWorld->m_WorldModels[i]->m_pOriginalBsp);
	}
}


// FUNCTION: LITHTECH 0x00415ff0
static void SetPolyTexturePointers(CClientMgr *pClientMgr, WorldBsp *pBsp)
{
	uint32 i;
	Surface *pSurface;
	FileRef ref;
	Sprite *pSprite;
	SurfaceSprite *pSurfaceSprite;

	for (i=0; i < pBsp->m_nSurfaces; i++)
	{
		pSurface = &pBsp->m_Surfaces[i];

		ref.m_FileType = FILE_ANYFILE;
		ref.m_pFilename = pBsp->m_TextureNames[pSurface->m_Unknown36];

		// Animated textures are sprites.
		if (pSurface->m_Flags & SURF_SPRITEANIMATE)
		{
			if (LoadSprite(pClientMgr, &ref, &pSprite) == LT_OK)
			{
				pSurfaceSprite = (SurfaceSprite*)dalloc_z(sizeof(SurfaceSprite));
				pSurfaceSprite->m_pNext = pClientMgr->m_SurfaceSprites;
				pClientMgr->m_SurfaceSprites = pSurfaceSprite;
				pSurfaceSprite->m_pSurface = pSurface;
				pSurfaceSprite->m_pSprite = pSprite;
				spr_InitTracker(&pSurfaceSprite->m_SpriteTracker, pSprite);
				if (pSurfaceSprite->m_SpriteTracker.m_pCurFrame)
					pSurface->m_pTexture = pSurfaceSprite->m_SpriteTracker.m_pCurFrame->m_pTex;
			}

			continue;
		}

		pSurface->m_pTexture = cm_AddSharedTexture(pClientMgr, &ref);

		if (!pSurface->m_pTexture && (g_DebugLevel >= 1))
		{
			dsi_ConsolePrint("Unable to find world texture %s", ref.m_pFilename);
		}

		// Give it a default texture if it's missing its texture.
		if (!pSurface->m_pTexture)
		{
			ref.m_FileType = FILE_CLIENTFILE;
			ref.m_pFilename = "textures\\default_texture.dtx";
			pSurface->m_pTexture = cm_AddSharedTexture(pClientMgr, &ref);
		}
	}
}


// FUNCTION: LITHTECH 0x00416120
static void AddAllObjectsToBSP(CClientShell *pShell, LTList *pList)
{
	LTLink *pCur, *pListHead;
	LTObject *pObj;
	WorldTree *pWorldTree;

	pWorldTree = &pShell->m_pClientMgr->m_World.m_WorldTree;

	pListHead = &pList->m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObj = (LTObject*)pCur->m_pData;

		pWorldTree->InsertObject(pObj, 0);
	}
}


// FUNCTION: LITHTECH 0x00416160
void cs_UnloadWorld(CClientShell *pShell)
{
	pShell->NotifyWorldClosing();
	pShell->RemoveAllObjects();
	pShell->CloseWorlds();
}


// FUNCTION: LITHTECH 0x00416180
void CClientShell::NotifyWorldClosing()
{
	if (m_bWorldOpened)
	{
		m_pClientMgr->OnExitWorld(this);
	}

	m_bWorldOpened = LTFALSE;
}


// FUNCTION: LITHTECH 0x004161a0
void CClientShell::CloseWorlds()
{
	int i;
	LTLink *pListHead, *pCur;
	LTObject *pObject;

	// Remove all objects from the BSP.
	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		RemoveAllObjectsFromWorldTree(this, &m_pClientMgr->m_ObjectMgr.m_ObjectLists[i]);
	}

	// Clear their standing-on status.
	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		pListHead = &m_pClientMgr->m_ObjectMgr.m_ObjectLists[i].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pObject = (LTObject*)pCur->m_pData;

			// Detach it from whatever it's standing on.
			DetachObjectStanding(pObject);
			DetachObjectsStandingOn(pObject);
		}
	}

	// Remove all world model objects.
	cm_RemoveObjectsInList(m_pClientMgr, &m_pClientMgr->m_ObjectMgr.m_ObjectLists[OT_WORLDMODEL], LTFALSE);

	cm_RemoveObjectsInList(m_pClientMgr, &m_pClientMgr->m_ObjectMgr.m_ObjectLists[OT_CONTAINER], LTFALSE);

	// Unbind all the worlds.
	UnbindWorlds();

	// Free the animated world textures.
	cm_FreeSurfaceSprites(m_pClientMgr);

	// Shut down the sounds.
	m_pClientMgr->m_SoundMgr.StopAllSounds();

	// Close any open world files.
	m_pClientMgr->m_World.Term();
}


// FUNCTION: LITHTECH 0x004162a0
LTBOOL CClientShell::CreateVisContainerObjects()
{
	uint32 i, iSection;
	WorldData *pWorldData;
	ObjectCreateStruct ocs;
	InternalObjectSetup internalSetup;
	LTObject *pObject;
	MainWorld *pWorld;

	internalSetup.m_pSetup = &ocs;

	pWorld = GetWorld();
	for (i=0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		pWorldData = pWorld->m_WorldModels[i];

		if (pWorldData->m_pOriginalBsp->m_WorldInfoFlags & WIF_TERRAIN)
		{
			// One object per terrain section.
			for (iSection=0; iSection < pWorldData->m_pOriginalBsp->m_TerrainSections.GetSize(); iSection++)
			{
				ocs.Clear();
				ocs.m_ObjectType = OT_WORLDMODEL;
				ocs.m_Flags = FLAG_SOLID | FLAG_RAYHIT;
				if (!w_MakeSpecialName(pWorldData->m_pOriginalBsp->m_WorldName, iSection, ocs.m_Filename, sizeof(ocs.m_Filename)))
					return LTFALSE;

				if (cm_AddObjectToClientWorld(m_pClientMgr, OBJID_CLIENTCREATED, &internalSetup, &pObject, LTFALSE, LTFALSE) != LT_OK)
					return LTFALSE;
			}
		}
		else if (pWorldData->m_pOriginalBsp->m_WorldInfoFlags & (WIF_PHYSICSBSP | WIF_VISBSP))
		{
			// Ok, make an object for it.
			ocs.Clear();
			ocs.m_ObjectType = OT_WORLDMODEL;
			ocs.m_Flags = FLAG_SOLID | FLAG_RAYHIT | FLAG_VISIBLE;
			SAFE_STRCPY(ocs.m_Filename, pWorldData->m_pValidBsp->m_WorldName);
			ocs.m_Pos = pWorldData->m_pOriginalBsp->m_WorldTranslation;

			if (cm_AddObjectToClientWorld(m_pClientMgr, OBJID_CLIENTCREATED, &internalSetup, &pObject, LTTRUE, LTFALSE) != LT_OK)
				return LTFALSE;
		}
	}

	return LTTRUE;
}

// Out-of-line copies of inlines.
// FUNCTION: LITHTECH 0x004165f0 ?Clear@ObjectCreateStruct@@QAEXXZ
// FUNCTION: LITHTECH 0x00416670 ??0FileRef@@QAE@XZ


// FUNCTION: LITHTECH 0x00416680
LTBOOL CClientShell::BindWorlds()
{
	MainWorld *pWorld;
	RenderContextInit contextInit;

	if (r_IsRenderInitted())
	{
		pWorld = &m_pClientMgr->m_World;
		if (!pWorld->m_Unknown1C8)
		{
			contextInit.m_pWorld = pWorld;
			pWorld->m_Unknown1C8 = (uint32)g_Render.CreateContext(&contextInit);
			if (!pWorld->m_Unknown1C8)
				return LTFALSE;
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x004166c0
void CClientShell::UnbindWorlds()
{
	MainWorld *pWorld;

	pWorld = &m_pClientMgr->m_World;
	if (pWorld->m_Unknown1C8)
	{
		g_Render.DeleteContext((HRENDERCONTEXT)pWorld->m_Unknown1C8);
		pWorld->m_Unknown1C8 = 0;
	}
}


// Tells the client shell which world the local server is loading.
// FUNCTION: LITHTECH 0x004166f0
void model_SetWorldName(char *pWorldName)
{
	CClientMgr *pClientMgr;

	if (g_pClientShell)
	{
		pClientMgr = g_pClientShell->m_pClientMgr;
		if (pClientMgr->m_pClientShell)
			pClientMgr->m_pClientShell->PreLoadWorld(pWorldName);
	}
}


// FUNCTION: LITHTECH 0x00416720
void clienthack_UnloadWorld()
{
	if (!g_pClientShell)
		return;

	g_pClientShell->m_ClientObjectID = (uint16)-1;
	cs_UnloadWorld(g_pClientShell);
}


// FUNCTION: LITHTECH 0x00416740
void* dsi_GetLoadUser()
{
	return (void*)g_pClientMgr->m_Unknown12e8;
}


// The server loaded a model: the client shares it instead of loading it again.
// FUNCTION: LITHTECH 0x00416750
void clienthack_ModelLoaded(Model *pModel)
{
	FileRef ref;
	FileIdentifier *pIdent;

	if (g_bForceRemote)
		return;

	ref.m_FileType = FILE_SERVERFILE;
	ref.m_FileID = (uint16)pModel->m_FileID;
	pIdent = cf_GetFileIdentifier(g_pClientMgr->m_hFileMgr, &ref, TYPECODE_MODEL);
	if (pIdent && !pIdent->m_pData)
	{
		pIdent->m_pData = pModel;
		nexus_AddLeech(&pModel->m_Nexus, nexus_CreateLeech(&g_ClientModelLeechDef, pIdent));

		if (g_DebugLevel >= 1)
		{
			dsi_ConsolePrint("Client inherited model %s from server!", cf_GetFilename(g_pClientMgr->m_hFileMgr, &ref));
		}
	}
}


// FUNCTION: LITHTECH 0x00416800
void dsi_LoadProgress(uint32 percent)
{
	if (g_pClientShell)
		cm_ShowLoadProgress(g_pClientShell->m_pClientMgr);
}


// Marks the models client objects use, so the server doesn't free them.
// FUNCTION: LITHTECH 0x00416820
void model_FreeUnusedChildModels()
{
	LTLink *pListHead, *pCur;
	ModelInstance *pInst;
	Model *pModel;

	if (!g_pClientMgr)
		return;

	pListHead = &g_pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pInst = (ModelInstance*)pCur->m_pData;
		pModel = pInst->GetModelDB();
		if (!pInst->sd && pModel)
			pModel->m_Flags |= 1;
	}
}


// Get the FileIDInfo for this file id.
// FUNCTION: LITHTECH 0x00416870
FileIDInfo* CClientShell::GetClientFileIDInfo(uint16 wFileID)
{
	HHashElement *hElement;
	FileIDInfo *pFileIDInfo;

	// See if it's in the hash table.
	hElement = hs_FindElement(m_hFileIDTable, &wFileID, 2);
	if (hElement)
	{
		pFileIDInfo = (FileIDInfo *)hs_GetElementUserData(hElement);
		if (pFileIDInfo)
			return pFileIDInfo;
	}

	// Create a new fileidinfo...
	pFileIDInfo = (FileIDInfo *)sb_Allocate_z(&g_pClientMgr->m_FileIDInfoBank);
	if (!pFileIDInfo)
		return LTNULL;

	// Make a new one...
	hElement = hs_AddElement(m_hFileIDTable, &wFileID, 2);
	if (!hElement)
	{
		dfree(pFileIDInfo);
		return LTNULL;
	}

	// Have the hash table own the pointer...
	hs_SetElementUserData(hElement, (void *)pFileIDInfo);

	return pFileIDInfo;
}

// FUNCTION: LITHTECH 0x00416920
MainWorld* CClientShell::GetWorld()
{
	return &m_pClientMgr->m_World;
}


// Template and inline code the CServerMgr constructor and destructor use.
// FUNCTION: LITHTECH 0x00414e80 ??_GCServerMgr@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004150c0 ??0ServerFileMgr@@QAE@XZ
// FUNCTION: LITHTECH 0x00416930 ??0?$CMoArray@UObjectMapEntry@@VDefaultCache@@@@QAE@XZ
// FUNCTION: LITHTECH 0x00416950 ?GenGetNext@?$CMoArray@UObjectMapEntry@@VDefaultCache@@@@UBE?AUObjectMapEntry@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00416970 ?GenGetAt@?$CMoArray@UObjectMapEntry@@VDefaultCache@@@@UBE?AUObjectMapEntry@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00416990 ?GenRemoveAt@?$CMoArray@UObjectMapEntry@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00416ab0 ?AllocVoid@?$ObjectBank@UServerData@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00416af0 ?FreeVoid@?$ObjectBank@UServerData@@VNullCS@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00416b20 ??1?$ObjectBank@VModelInstance@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416b40 ??1?$ObjectBank@VWorldModelInstance@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416b60 ??1?$ObjectBank@VSpriteInstance@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416b80 ??1?$ObjectBank@VDynamicLight@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416ba0 ??1?$ObjectBank@VCameraInstance@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416bc0 ??1?$ObjectBank@VLTParticleSystem@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416be0 ??1?$ObjectBank@VLTPolyGrid@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00416c00 ?SetSize2@?$CMoArray@ULightAnim@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00416c90 ?Term@?$ObjectBank@UUsedFile@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00416cb0 ?SetSize2@?$CMoArray@UObjectMapEntry@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00416d20 ?InternalNiceSetSize@?$CMoArray@UObjectMapEntry@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00416e10 ??0?$ObjectBank@UServerData@@VNullCS@@@@QAE@XZ
// FUNCTION: LITHTECH 0x00416e30 ?Term@?$ObjectBank@UServerData@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00416e50 ??_G?$ObjectBank@UUsedFile@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00416e90 ??_G?$ObjectBank@UServerData@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00416ef0 ?BaseNew@@YAPAUObjectMapEntry@@PAVLAlloc@@PAU1@K@Z
// FUNCTION: LITHTECH 0x00416ed0 ?BaseDelete@@YAXPAVLAlloc@@PAUObjectMapEntry@@K@Z

