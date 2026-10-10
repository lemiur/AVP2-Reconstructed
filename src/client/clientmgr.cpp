// Jupiter runtime/client/src/clientmgr.cpp
// Talon passes the manager explicitly to the C-style helpers (cm_*), keeps the client shell in
// m_pClientShell (no holders) and authenticates peers through WONAPI (PeerAuthClient).
// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC /IE:/AVP2Source/build/proj/LT2/lithshared/wonapi /IE:/AVP2Source/jupiter/dx9inc /IE:/AVP2Source/build/proj/LT2/lithshared/lith /IE:/AVP2Source/build/proj/LT2/lithshared/controlfilemgr
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "bdefs.h"
#include "de_memory.h"

#include "clientmgr.h"
#include "clientshell.h"
#include "iclientshell.h"
#include "dsys_interface.h"
#include "render.h"
#include "soundmgr.h"
#include "musicdriver.h"
#include "console.h"
#include "concommand.h"
#include "consolecommands.h"
#include "client_filemgr.h"
#include "sprite.h"
#include "input.h"
#include "cmoveabstract.h"
#include "videomgr.h"
#include "cloaderthread.h"
#include "counter.h"
#include "setupobject.h"
#include "effects.h"
#include "servermgr.h"
#include "model.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "WONAuth/AuthContext.h"
#include "WONAuth/PeerAuthClient.h"

#define CMD_USERCOMMAND		(1<<0)
#define CMSG_HELLO			5
#define CMSG_PEERTOPEERAUTH	0xd

#define MUSIC_IMMEDIATE		0


using namespace WONAPI;


// ------------------------------------------------------------------ //
// Externs.
// ------------------------------------------------------------------ //


#define MAX_RESTREES	20
#define MAX_CLIENT_COMMANDS	255
#define RECORDTYPE_LTOBJECT	1
#define PACKETFLAG_MESSAGE	(1<<0)	// CPacket::m_ErrorFlags: allocated for a game message
#define TYPECODE_SOUND		4


void cm_FreeUnusedModelTextures(CClientMgr *pClientMgr, LTObject *pObject);		// 0x004263f0
void cm_MoveAndRotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTRotation *pNewRot);	// 0x00426a40
void cm_ScaleObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewScale);		// 0x00426640
void cm_RotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTRotation *pNewRot);		// 0x00426940
LTRESULT so_ExtraTerm(CClientMgr *pClientMgr, LTObject *pObject);						// 0x0048a620
LTRESULT om_CreateObject(ObjectMgr *pMgr, ObjectCreateStruct *pStruct, LTObject **ppObject);	// 0x00468790
LTRESULT om_DestroyObject(ObjectMgr *pMgr, LTObject *pObject);							// 0x00468880
void InitialWorldModelRotate(WorldModelInstance *pInstance);							// 0x0045d570
void DetachObjectStanding(LTObject *pObj);												// 0x0045d110
void DetachObjectsStandingOn(LTObject *pObj);											// 0x0045d150
void w_RemoveObjectFromLeaf(LTObject *pObj);
void ic_FreeFileList(FileEntry *pList);	// 0x0043eae0											// 0x00430680

#define MODELFLAG_CACHED	(1<<0)

// Empty memory failure callback (folded with the other empty functions at 0x004359b0).
void cm_OnMemoryFailure(void *pUser);
LTRESULT om_Init(ObjectMgr *pMgr, LTBOOL bClient);	// 0x004685d0
LTRESULT om_Term(ObjectMgr *pMgr);					// 0x00468710

void cm_FreeSurfaceSprites(CClientMgr *pClientMgr);
void cm_RemoveObjectsInList(CClientMgr *pClientMgr, LTList *pList, LTBOOL bServerOnly);
void cm_SaveAutoConfig(CClientMgr *pClientMgr, const char *pFilename);
static void ClientStringWhine(const char *pString, void *pUser);
static void FreeModelList(LTLink *pListHead);
static void FreeSpriteList(LTList *pList);

// The client manager's CSoundMgr (CClientMgr::m_SoundMgr).
#define SOUNDMGR(pMgr)	(&(pMgr)->m_SoundMgr)


// Empty in this build (folded into 0x00473ac0); called after binding a texture (cutil.cpp).
void cm_OnTextureBound(CClientMgr *pClientMgr);

// shellbind.cpp
typedef void* (*CreateShellFn)(void *pInterface);
typedef void (*DeleteShellFn)(void *pShell);
void sb_UnloadShellModule(ShellBindModule *pModule);										// 0x0048a770
void sb_GetShellFunctions(ShellBindModule *pModule, CreateShellFn *pCreate, DeleteShellFn *pDelete);	// 0x0048a790

void bm_UnbindModule(CBindModuleType *hModule);	// 0x004017a0


// ------------------------------------------------------------------ //
// Globals.
// ------------------------------------------------------------------ //

// GLOBAL: LITHTECH 0x004def68
SmartPtr<AuthContext> g_pAuthContext;

// GLOBAL: LITHTECH 0x004def6c
SmartPtr<AuthCertificateBase> g_pPeerCertificate;

ObjectBank<LTLink> g_DLinkBank(64, 1024);

// GLOBAL: LITHTECH 0x004def94
PeerAuthClient *g_pPeerAuthClient;

// Timing information
// GLOBAL: LITHTECH 0x004def98
uint32 g_Ticks_Music;
// GLOBAL: LITHTECH 0x004def9c
uint32 g_Ticks_Sound;
// GLOBAL: LITHTECH 0x004defa0
uint32 g_Ticks_Input;
// GLOBAL: LITHTECH 0x004defa4
uint32 g_Ticks_ClientShell;
// GLOBAL: LITHTECH 0x004defa8
uint32 g_Ticks_Render;

// Only the constructor is used here, but the header's chunk allocators make this object the first to
// emit CSegment's and CStyle's vector deleting destructors (0x00414750, 0x004147e0). Its six static
// CLithChunkAllocator members each use up a static-initializer number, so the original included it
// after the globals above (their _$E29.._$E42 numbers leave no room before them).
#include "ltdirectmusic_impl.h"
#include "engine_vars.h"

// The WONAPI headers (via <string>) use up 28 static initializer numbers first.
// FUNCTION: LITHTECH 0x0040fa60 _$E32
// FUNCTION: LITHTECH 0x0040fa70 _$E29
// FUNCTION: LITHTECH 0x0040fa80 _$E31
// FUNCTION: LITHTECH 0x0040fa90 _$E30
// FUNCTION: LITHTECH 0x0040fac0 _$E37
// FUNCTION: LITHTECH 0x0040fad0 _$E34
// FUNCTION: LITHTECH 0x0040fae0 _$E36
// FUNCTION: LITHTECH 0x0040faf0 _$E35
// FUNCTION: LITHTECH 0x0040fb20 _$E42
// FUNCTION: LITHTECH 0x0040fb30 _$E39
// FUNCTION: LITHTECH 0x0040fb60 _$E41
// FUNCTION: LITHTECH 0x0040fb70 _$E40


// FUNCTION: LITHTECH 0x0040fba0
CSoundMgr* GetClientILTSoundMgrImpl()
{
	return &g_pClientMgr->m_SoundMgr;
}

// FUNCTION: LITHTECH 0x0040fbb0
SMusicMgr* GetMusicMgr()
{
	return &g_pClientMgr->m_MusicMgr;
}


// ----------------------------------------------------------------------- //
// CClientMgr functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0040fbc0
void _GetRModeFromConsoleVariables(RMode *pMode)
{
	LTCommandVar *pVar;

	// Init the renderer.
	memset(pMode, 0, sizeof(*pMode));
	pMode->m_Width = g_ScreenWidth;
	pMode->m_Height = g_ScreenHeight;
	pMode->m_BitDepth = g_CV_BitDepth;

	// Use a specific render DLL if they want.
	pVar = cc_FindConsoleVar(&g_ClientConsoleState, "renderdll");
	if(pVar)
	{
		strncpy(pMode->m_RenderDLL, pVar->pStringVal, sizeof(pMode->m_RenderDLL)-1);
	}
	else
	{
		strcpy(pMode->m_RenderDLL, "d3d.ren");
	}

	// Use a specific card description if they want.
	pVar = cc_FindConsoleVar(&g_ClientConsoleState, "carddesc");
	if(pVar && pVar->pStringVal)
	{
		strncpy(pMode->m_InternalName, pVar->pStringVal, sizeof(pMode->m_InternalName)-1);
	}
}


// cutil.cpp
void cm_FreeSharedTextures(CClientMgr *pClientMgr);					// 0x004260f0

// The console commands keep their own client manager pointer.
void cm_RunAutoConfig(CClientMgr *pClientMgr, const char *pFilename, uint32 flags);
void cm_RunCommandLine(CClientMgr *pClientMgr, uint32 flags, CmdLineArgs *pArgs);

ILTClient* ci_CreateClientInterface(CClientMgr *pClientMgr);	// 0x00404ac0
void ci_Term();													// 0x0040c3e0
void c_InitConsoleCommands();									// 0x00423600
void c_TermConsoleCommands();									// 0x00423690
void mw_ResetWatches();											// 0x0044db60
void InitErrorLog();											// 0x004361f0
void TermErrorLog();											// 0x00436240
ILTCursor* CreateCursorMgr(CClientMgr *pClientMgr);				// 0x00446e50
VideoMgr* CreateVideoMgr(CClientMgr *pClientMgr, const char *pszName);	// 0x0049d210
typedef void (*StringShowFn)(const char *pString, void *pUser);
void str_ShowAllStringsAllocated(StringShowFn fn, void *pUser);	// 0x00497440

#define DEBUGGRAPHMGR(pMgr)	(&(pMgr)->m_DebugGraphMgr)


// FUNCTION: LITHTECH 0x0040fc70
LTRESULT CClientMgr::Init(const char **resTrees, uint32 nResTrees, const char *pConfigFile, CmdLineArgs *pArgs)
{
	char cmd[64];
	TreeType treeTypes[MAX_RESTREES];
	int nTreesLoaded;
	LTRESULT dResult;
	RMode defaultMode;
	char versionStr[32];
	LTRect rGraphMgr;

	r_InitRenderStruct(LTTRUE);

	mw_ResetWatches();

	InitGlobals();

	m_LastUpdateRate = 10;
	m_bInputState = LTTRUE;
	m_bTrackingInputDevices = LTFALSE;
	m_CurTextureFrameCode = 0;

	m_World.m_WorldTree.InitWorldTree(&m_ObjectMgr);

	// Init our ILTClient interface..
	m_pClientDE = ci_CreateClientInterface(this);

	// Setup some default data.
	m_AxisOffsets[0] = m_AxisOffsets[1] = m_AxisOffsets[2] = 0.0f;
	memset(m_Commands, 0, sizeof(m_Commands));
	m_iCurInputSlot = 0;

	// Add files to the file manager.
	if(nResTrees > MAX_RESTREES)
		nResTrees = MAX_RESTREES;

	if(nResTrees == 0)
	{
		SetupError(LT_NOGAMERESOURCES);
		cm_ProcessError(this, LT_NOGAMERESOURCES | ERROR_SHUTDOWN);
		RETURN_ERROR(1, CClientMgr::Init, LT_NOGAMERESOURCES);
	}

	memcpy(m_ResTrees, resTrees, sizeof(m_ResTrees));
	m_nResTrees = nResTrees;

	cf_AddResourceTrees(m_hFileMgr, resTrees, nResTrees, treeTypes, &nTreesLoaded);
	if(nTreesLoaded == 0)
	{
		SetupError(LT_CANTLOADGAMERESOURCES, resTrees[0]);
		cm_ProcessError(this, LT_CANTLOADGAMERESOURCES);
		RETURN_ERROR(1, CClientMgr::Init, LT_CANTLOADGAMERESOURCES);
	}

	// Setup the ClientShellDE.
	dResult = dsi_InitClientShellDE(this);
	if(dResult != LT_OK)
	{
		cm_ProcessError(this, dResult | ERROR_SHUTDOWN);
		return dResult;
	}

	c_InitConsoleCommands();
	con_InitBare();

	// Add the version console variable..
	dsi_GetVersionInfo(m_VersionInfo);
	m_VersionInfo.GetString(versionStr, sizeof(versionStr));
	sprintf(cmd, "version %s", versionStr);
	cc_HandleCommand(&g_ClientConsoleState, cmd);

	// Initialize input.
	if(dsi_IsInputEnabled())
	{
		if(!m_InputMgr->Init(m_InputMgr, &g_ClientConsoleState))
		{
			cm_ProcessError(this, LT_CANTINITIALIZEINPUT | ERROR_SHUTDOWN);
			RETURN_ERROR(1, CClientMgr::Init, LT_CANTINITIALIZEINPUT);
		}
	}

	g_pCommandClientMgr = this;
	cm_RunAutoConfig(this, pConfigFile, CC_NOCOMMANDS);
	cm_RunCommandLine(this, CC_NOCOMMANDS, pArgs);

	// Open up the error log.
	InitErrorLog();

	// Init some music stuff..
	m_MusicMgr.m_hWnd = dsi_GetMainWindow();
	m_MusicMgr.m_hInstance = dsi_GetInstanceHandle();

	if(g_bNullRender)
	{
		dsi_ConsolePrint("Warning: NullRender is ON.");
	}

	// Rerun the config stuff with commands only.
	cm_RunAutoConfig(this, pConfigFile, CC_NOVARS);
	cm_RunCommandLine(this, CC_NOVARS, pArgs);
	m_bCanSaveConfigFile = LTTRUE;

	m_pCollisionInfo = LTNULL;

	m_pCursorMgr = CreateCursorMgr(this);

	// Tell the client shell that we are ready to rock!
	_GetRModeFromConsoleVariables(&defaultMode);
	dResult = m_pClientShell->OnEngineInitialized(&defaultMode, &m_NetMgr.m_guidApp);

	// VideoMgr MUST be initialized AFTER the ClientShell, so that the SoundMgr is
	// properly initialized.
	m_pVideoMgr = CreateVideoMgr(this, "BINK");

	// Tell the video stuff.
	if(m_pVideoMgr)
	{
		m_pVideoMgr->OnRenderInit();
	}

	// Initialize the debug graph manager
	rGraphMgr.left	 = 0;
	rGraphMgr.top	 = 0;
	rGraphMgr.right	 = defaultMode.m_Width;
	rGraphMgr.bottom = defaultMode.m_Height;
	DEBUGGRAPHMGR(this)->Init(m_pClientDE, &rGraphMgr);

	g_pAuthContext = LTNULL;

	return dResult;
}


// Starts the peer to peer authentication: sends the shell's auth context to the server.
// FUNCTION: LITHTECH 0x00410050
void CClientMgr::StartPeerAuth()
{
	g_pAuthContext = (AuthContext*)m_pClientShell->GetAuthContext();
	if(g_pAuthContext.get())
	{
		g_pPeerAuthClient = new PeerAuthClient;

		ByteBufferPtr outMsg;

		g_pPeerAuthClient->SetUseAuth2(true);

		outMsg = g_pPeerAuthClient->Start(g_pAuthContext->GetPeerData(), AUTH_TYPE_PERSISTENT, 4);

		CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		cPacket->m_Data[0] = CMSG_PEERTOPEERAUTH;
		cPacket->WriteRaw((void*)outMsg->data(), (uint16)outMsg->length());
		m_NetMgr.SendPacket(cPacket, m_pCurShell->m_HostID, MESSAGE_GUARANTEED);
	}
}


// Handles the server's peer to peer authentication packet and answers it.
// FUNCTION: LITHTECH 0x004101c0
void CClientMgr::OnPeerAuthPacket(CPacket *pPacket)
{
	char buf[512];
	uint32 len;

	memset(buf, 0, sizeof(buf));

	len = pPacket->m_DataLen - pPacket->m_Pos;
	pPacket->ReadRaw(buf, (uint16)len);

	ByteBufferPtr outMsg;

	g_pPeerAuthClient->HandleRecvMsg(buf + 4, len - 4, outMsg);

	delete g_pPeerAuthClient;

	if(outMsg.get() && outMsg->length())
	{
		CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		cPacket->m_Data[0] = CMSG_PEERTOPEERAUTH;
		cPacket->WriteRaw((void*)outMsg->data(), (uint16)outMsg->length());
		m_NetMgr.SendPacket(cPacket, m_pCurShell->m_HostID, MESSAGE_GUARANTEED);
	}
	else
	{
		g_pAuthContext = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x004103d0
void CClientMgr::TermClientShellDE()
{
	LTExtraCommandStruct *pCommand;
	LTLink *pCur, *pNext;
	IClientShell *pShell;
	CreateShellFn pCreate;
	DeleteShellFn pDelete;

	g_pAuthContext = LTNULL;

	// Clear rendering hooks.
	m_ModelHookFn = LTNULL;

	// Remove their console programs.
	pCur = g_ClientConsoleState.m_ExtraCommands.m_pNext;
	while(pCur && (pCur != &g_ClientConsoleState.m_ExtraCommands))
	{
		pNext = pCur->m_pNext;

		pCommand = (LTExtraCommandStruct*)pCur->m_pData;
		if(pCommand->flags & CMD_USERCOMMAND)
		{
			cc_RemoveCommand(&g_ClientConsoleState, pCommand);
		}

		pCur = pNext;
	}

	// Delete the client shell.
	if(m_pClientShell)
	{
		pShell = m_pClientShell;
		m_pClientShell = LTNULL;

		sb_GetShellFunctions(m_hShellModule, &pCreate, &pDelete);
		pDelete(pShell);
	}

	if(m_hShellModule)
	{
		sb_UnloadShellModule(m_hShellModule);
		m_hShellModule = LTNULL;
	}

	if(m_hClientResourceModule)
	{
		bm_UnbindModule(m_hClientResourceModule);
		m_hClientResourceModule = LTNULL;
	}

	if(m_hLocalizedClientResourceModule)
	{
		bm_UnbindModule(m_hLocalizedClientResourceModule);
		m_hLocalizedClientResourceModule = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x004104c0
void CClientMgr::OnEnterWorld(CClientShell *pShell)
{
	if(m_pClientShell)
	{
		m_pClientShell->OnEnterWorld();
	}
}


// FUNCTION: LITHTECH 0x004104e0
void CClientMgr::OnExitWorld(CClientShell *pShell)
{
	if(m_pClientShell)
	{
		m_pClientShell->OnExitWorld();
	}
}


// Starts a shell: hosts or joins a game, or runs one locally, and sends the hello message.
// As in Jupiter, the world is started through a pWorldName local and a dResult that the start overwrites. Talon's
// default is an error, so a local game without a world name (a recorded or played demo) fails here; the error
// block's code doesn't show the value (LT_ERROR is a guess).
// FUNCTION: LITHTECH 0x00410500
LTRESULT CClientMgr::StartShell(StartGameRequest *pRequest)
{
	CClientShell *pShell;
	LTRESULT dResult;
	char errorString[256];
	CBaseDriver *pDriver;
	char playbackWorldName[256];
	CPacketRef cHello;
	char *pWorldName;

	// Kill the old server connection if there is one.
	EndShell();

	pShell = new CClientShell;

	// Start a shell.
	pShell->m_ShellMode = pRequest->m_Type;
	pShell->Init(this);

	dsi_SetConsoleUp(LTFALSE);

	switch(pRequest->m_Type)
	{
		case STARTGAME_CLIENT:
		{
			// Use the first net driver..
			pDriver = m_NetMgr.m_pMainDriver;
			if(!pDriver)
			{
				RETURN_ERROR(1, StartGame, LT_NOTINITIALIZED);
			}

			// Try to actually connect...
			if(pRequest->m_flags & SG_LOBBY)
			{
				if(!pDriver->JoinLobbyLaunchSession())
				{
					RETURN_ERROR(1, StartGame, LT_ERROR);
				}
			}
			else
			{
				if(!pRequest->m_pNetSession)
				{
					RETURN_ERROR_PARAM(1, StartGame, LT_INVALIDPARAMS, "Missing m_pNetSession");
				}

				dResult = pDriver->JoinSession(pRequest->m_pNetSession);
				if(dResult != LT_OK)
				{
					return dResult;
				}
			}

			// Setup the shell.
			dResult = pShell->StartupClient(pDriver);
			if(dResult != LT_OK)
				return dResult;
		}
		break;

		case STARTGAME_CLIENTTCP:
		{
			// Use the first net driver..
			pDriver = m_NetMgr.GetDriver("internet");
			if(!pDriver)
			{
				RETURN_ERROR(1, StartGame, LT_NOTINITIALIZED);
			}

			// Connect to the given address.
			dResult = pDriver->ConnectTCP(pRequest->m_TCPAddress);
			if(dResult != LT_OK)
				return dResult;

			// Setup the shell.
			dResult = pShell->StartupClient(pDriver);
			if(dResult != LT_OK)
				return dResult;
		}
		break;

		default:
		{
			pDriver = LTNULL;
			if(pRequest->m_Type == STARTGAME_HOST)
			{
				pDriver = m_NetMgr.m_pMainDriver;
				if(!pDriver)
				{
					RETURN_ERROR(1, StartGame, LT_NOTINITIALIZED);
				}

				if(pRequest->m_flags & SG_LOBBY)
				{
					if(pDriver->HostLobbyLaunchSession(&pRequest->m_HostInfo) != LT_OK)
					{
						RETURN_ERROR(1, StartGame, LT_ERROR);
					}
				}
				else
				{
					dResult = pDriver->HostSession(&pRequest->m_HostInfo);
					if(dResult != LT_OK)
					{
						return dResult;
					}
				}
			}

			// Record or play back a demo.
			pWorldName = pRequest->m_WorldName;
			if(pRequest->m_RecordFilename[0])
			{
				dResult = m_DemoMgr.RecordDemo(pWorldName, pRequest->m_RecordFilename);
				if(dResult != LT_OK)
				{
					delete pShell;
					return dResult;
				}

				pRequest->m_WorldName[0] = 0;
			}
			else if(pRequest->m_PlaybackFilename[0])
			{
				dResult = m_DemoMgr.PlayDemo(pRequest->m_PlaybackFilename, playbackWorldName, sizeof(playbackWorldName));
				if(dResult != LT_OK)
				{
					delete pShell;
					return dResult;
				}

				pRequest->m_WorldName[0] = 0;
			}

			// Setup the shell.
			dResult = pShell->StartupLocal(pRequest, pRequest->m_Type == STARTGAME_HOST, pDriver);
			if(dResult != LT_OK)
			{
				delete pShell;
				return dResult;
			}

			dResult = LT_ERROR;
			if(pWorldName[0] != 0)
			{
				dResult = pShell->m_pServerMgr->DoStartWorld(pWorldName, LOADWORLD_LOADWORLDOBJECTS|LOADWORLD_RUNWORLD, m_CurTime);
			}

			if(dResult != LT_OK)
			{
				pShell->m_pServerMgr->GetErrorString(errorString, 256);
				SetupError(LT_SERVERERROR, errorString);
				delete pShell;
				RETURN_ERROR_PARAM(1, CClientMgr::StartShell, LT_SERVERERROR, "error loading world");
			}

			// If they asked for a playdemo, fill in the world name.
			if(pRequest->m_PlaybackFilename[0] != 0)
			{
				SAFE_STRCPY(pRequest->m_WorldName, playbackWorldName);
			}
		}
		break;
	}

	// 'Enter' the server.
	m_pCurShell = pShell;
	cm_OnEnterServer(this);

	// Send the hello message.
	if(m_pCurShell)
	{
		cHello = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

		cHello->WriteType((uint16)pRequest->m_ClientDataLen);
		cHello->WriteRaw(pRequest->m_pClientData, (uint16)pRequest->m_ClientDataLen);
		cHello->m_Data[0] = CMSG_HELLO;
		m_NetMgr.SendPacket(cHello, m_pCurShell->m_HostID, MESSAGE_GUARANTEED);
	}

	if(pRequest->m_Type != STARTGAME_HOST && pRequest->m_Type != STARTGAME_HOSTTCP && pRequest->m_Type != STARTGAME_NORMAL)
	{
		StartPeerAuth();
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00410a40
void CClientMgr::EndShell()
{
	m_DemoMgr.StopDemo();

	if(m_pCurShell)
	{
		delete m_pCurShell;
		m_pCurShell = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x00410a70
LTRESULT CClientMgr::AppInitMusic(char *pMusicDLL)
{
	if(!g_bMusicEnable || g_CV_ForceSoundDisable)
		RETURN_ERROR(1, CClientMgr::AppInitMusic, LT_ERROR);

	if(pMusicDLL)
		strncpy(m_MusicDLLName, pMusicDLL, sizeof(m_MusicDLLName)-1);
	else
		strncpy(m_MusicDLLName, "cdaudio.dll", sizeof(m_MusicDLLName)-1);
	m_MusicDLLName[sizeof(m_MusicDLLName)-1] = 0;

	// Initialize music...
	switch(music_InitDriver(m_MusicDLLName, &m_MusicMgr))
	{
		case MUSICDRIVER_CANTLOADLIBRARY:
			SetupError(LT_MISSINGMUSICDLL, m_MusicDLLName);
			RETURN_ERROR(1, CClientMgr::AppInitMusic, LT_MISSINGMUSICDLL);
			break;
		case MUSICDRIVER_INVALIDDLL:
			SetupError(LT_INVALIDMUSICDLL, m_MusicDLLName);
			RETURN_ERROR(1, CClientMgr::AppInitMusic, LT_INVALIDMUSICDLL);
			break;
		case MUSICDRIVER_INVALIDOPTIONS:
			SetupError(LT_UNABLETOINITMUSICDLL, m_MusicDLLName);
			RETURN_ERROR(1, CClientMgr::AppInitMusic, LT_UNABLETOINITMUSICDLL);
			break;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00410bf0
void CClientMgr::AppTermMusic()
{
	music_TermDriver();
}


// FUNCTION: LITHTECH 0x00410c00
LTBOOL CClientMgr::BindClientShellWorlds()
{
	if(m_pCurShell)
	{
		m_pCurShell->BindWorlds();
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00410c20
void CClientMgr::UnbindClientShellWorlds()
{
	if(m_pCurShell)
		m_pCurShell->UnbindWorlds();
}


// FUNCTION: LITHTECH 0x00410c30
void CClientMgr::BindSharedTextures()
{
	LTLink *pCur, *pListHead;

	// Read in the latest console variables..
	if(g_Render.m_bInitted && g_Render.ReadConsoleVariables)
		g_Render.ReadConsoleVariables();

	pListHead = &m_SharedTextures.m_Head;
	for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		r_BindTexture((SharedTexture*)pCur->m_pData, LTFALSE);
		cm_OnTextureBound(this);
	}
}


// FUNCTION: LITHTECH 0x00410c80
void CClientMgr::UnbindSharedTextures()
{
	LTLink *pCur, *pListHead;

	pListHead = &m_SharedTextures.m_Head;
	for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		r_UnbindTexture((SharedTexture*)pCur->m_pData);
	}
}


// FUNCTION: LITHTECH 0x00410cb0
void CClientMgr::InitConsole()
{
	LTRect rect(0, 0, g_Render.m_Width, g_Render.m_Height/2);

	// Init the console.
	con_Term(LTFALSE);
	con_Init(&rect, c_CommandHandler, &g_Render);
}


// FUNCTION: LITHTECH 0x00410d00
void CClientMgr::TermConsole()
{
	con_Term(LTFALSE);
}


// FUNCTION: LITHTECH 0x00410d10
void CClientMgr::InitGlobals()
{
	m_pVideoMgr = LTNULL;
	g_pClientMgr = this;
	g_pCommandClientMgr = this;
}


// Something the loader thread finished.
// FUNCTION: LITHTECH 0x00410d30
void CClientMgr::ProcessLoaderMessage(LThreadMessage &msg)
{
	if(msg.m_ID == CLT_LOADEDFILE && msg.m_Data[0].m_dwData == 0)
	{
		cm_BindModel(this, (Model*)msg.m_Data[2].m_pData, (FileIdentifier*)msg.m_Data[1].m_pData, LTTRUE);
	}
}


// FUNCTION: LITHTECH 0x00410d60
void CClientMgr::ProcessLoaderMessages()
{
	LThreadMessage msg;

	while(LOADERTHREAD(this)->m_Outgoing.GetMessage(msg) == LT_OK)
	{
		ProcessLoaderMessage(msg);
	}
}


// FUNCTION: LITHTECH 0x00410db0
LTRESULT CClientMgr::Update()
{
	uint16 i;
	LTRESULT dResult;

	ProcessLoaderMessages();

	// Update videos.
	if(m_pVideoMgr)
	{
		m_pVideoMgr->UpdateVideos();
	}

	// Hack while there's a bug in Shogo..
	if(g_CV_FullLightScale)
	{
		VEC_SET(m_GlobalLightScale, 1, 1, 1);
	}

	// Update framerate (don't want to update it if the client's not active).
	if(dsi_IsClientActive())
	{
		m_DemoMgr.UpdateTime();
	}

	// Sleep in between frames.. helpful for debugging so it doesn't hog
	// all the processor time.
	dsi_ClientSleep(g_ClientSleepMS);

	// Pause music if disabled...
	if(m_MusicMgr.m_bValid)
	{
		CountAdder cntAdd(&g_Ticks_Music);

		if(g_bMusicEnable != m_MusicMgr.m_bEnabled)
		{
			m_MusicMgr.m_bEnabled = g_bMusicEnable;
			if(!g_bMusicEnable)
			{
				m_MusicMgr.Pause(MUSIC_IMMEDIATE);
			}
			else
			{
				m_MusicMgr.Resume();
			}
		}
	}

	// Pause sound if disabled...
	if(SOUNDMGR(this)->m_bValid)
	{
		CountAdder cntAdd(&g_Ticks_Sound);

		if(g_bSoundEnable != SOUNDMGR(this)->m_bEnabled)
		{
			SOUNDMGR(this)->m_bEnabled = g_bSoundEnable;
			if(!g_bSoundEnable)
			{
				SOUNDMGR(this)->PauseSounds();
			}
			else
			{
				SOUNDMGR(this)->ResumeSounds();
			}
		}
	}

	// Forward windows messages to the scripts.
	if(!m_DemoMgr.IsConsoleUp())
	{
		ForwardMessagesToScript();
	}

	// Gather and send current input.
	if(dsi_IsClientActive())
	{
		CountAdder cntAdd(&g_Ticks_Input);
		ProcessAllInput(LTFALSE);
	}

	// Update client shells.
	if(m_pCurShell)
	{
		CountAdder cntAdd(&g_Ticks_ClientShell);

		dResult = m_pCurShell->Update();
		if(dResult != LT_OK)
		{
			return cm_ProcessError(this, dResult);
		}
	}
	else if(m_pClientShell)
	{
		m_pClientShell->PreUpdate();
		m_pClientShell->Update();
		UpdateObjects();
		m_pClientShell->PostUpdate();
	}

	// Update sounds.
	{
		CountAdder cntAdd(&g_Ticks_Sound);
		CountAdder cntAdd2(&g_Ticks_SoundUpdate);

		UpdateAllSounds(m_FrameTime);
	}

	// Feed the keys to the console while it's up.
	if(m_DemoMgr.IsConsoleUp())
	{
		for(i=0; i < dsi_NumKeyDowns(); i++)
		{
			con_OnKeyPress(dsi_GetKeyDown(i));
		}

		dsi_ClearKeyDowns();
		dsi_ClearKeyUps();
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004110b0
void CClientMgr::ProcessAllInput(LTBOOL bForceClear)
{
	int32 nOn, nChanges;
	int32 changes[MAX_CLIENT_COMMANDS], on[MAX_CLIENT_COMMANDS];

	m_DemoMgr.ProcessInput(changes, &nChanges, on, &nOn, bForceClear || m_DemoMgr.IsConsoleUp());

	ForwardCommandChanges(changes, nChanges);

	// Send the commands that are on to the server.
	if(m_pCurShell && m_pCurShell->m_bWorldOpened)
	{
		SendUpdate(&m_NetMgr, m_pCurShell->m_HostID, on, nOn);
	}
}


// FUNCTION: LITHTECH 0x00411150
LTRESULT CClientMgr::ClearInput()
{
	if(m_DemoMgr.m_State == DEMO_NONE)
	{
		ProcessAllInput(LTTRUE);
		dsi_ClearKeyDowns();
		dsi_ClearKeyUps();
		dsi_ClearKeyMessages();
		m_InputMgr->ClearInput();
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00411180
void CClientMgr::ShowDrawSurface(uint32 flags)
{
	DEBUGGRAPHMGR(this)->Draw();

	if(g_Render.m_bInitted)
	{
		g_Render.SwapBuffers(flags);
	}
}


// FUNCTION: LITHTECH 0x004111b0 ?PlaySoundA@CClientMgr@@QAEKPAUPlaySoundInfo@@PAVFileRef@@M@Z
LTRESULT CClientMgr::PlaySound(PlaySoundInfo *pPlaySoundInfo, FileRef *pFile, float fOffsetTime)
{
	FileIdentifier *pIdent;
	const char *pFilename;
	LTRESULT dResult;

	if(!pPlaySoundInfo || !SOUNDMGR(this)->IsValid() || !SOUNDMGR(this)->IsEnabled())
		return LT_ERROR;

	pIdent = cf_GetFileIdentifier(m_hFileMgr, pFile, TYPECODE_SOUND);
	if(!pIdent)
	{
		pFilename = cf_GetFilename(m_hFileMgr, pFile);
		DEBUG_PRINT(2, ("Missing sound file %s", pFilename));
		return LT_ERROR;
	}

	dResult = SOUNDMGR(this)->PlaySound(*pPlaySoundInfo, *pIdent, (uint32)(fOffsetTime * 1000.0f + 0.5f));
	if(dResult != LT_OK)
		return dResult;

	m_pClientShell->OnPlaySound(pPlaySoundInfo);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00411260
void CClientMgr::UpdateAllSounds(float fFrameTime)
{
	if(!SOUNDMGR(this)->IsValid() || !SOUNDMGR(this)->IsEnabled())
		return;

	SOUNDMGR(this)->Update();

	if(g_bSoundShowCounts)
	{
		con_Printf(CONRGB(0,0,255), 0, "Sounds: playing:%d, heard:%d", SOUNDMGR(this)->GetNumSoundsPlaying(),
			SOUNDMGR(this)->GetNumSoundsHeard());
	}
}


// ------------------------------------------------------------------ //
// C routines.
// ------------------------------------------------------------------ //

// The inline CClientMgr constructor (the member constructors in member order, with MotionState's gravity
// through SetForce, 0x00411670) is built into this function: the CSoundMgr,
// CDebugGraphMgr and CLoaderThread members are real objects (clientmgr.h), and MotionInfo's empty constructor
// is __forceinline in motion.h so that it vanishes as in the original.
// FUNCTION: LITHTECH 0x004112c0
CClientMgr* cm_Init()
{
	CClientMgr *pClientMgr;

	pClientMgr = new CClientMgr;
	if(!pClientMgr)
		return LTNULL;

	pClientMgr->InitGlobals();
	pClientMgr->m_DemoMgr.m_pClientMgr = pClientMgr;

	pClientMgr->m_ObjectMap = LTNULL;
	pClientMgr->m_ObjectMapSize = 0;

	pClientMgr->m_pDirectMusicMgr = (ILTDirectMusicMgr*)new CLTDirectMusicMgr;

	// No errors so far...
	pClientMgr->m_ErrorString[0] = '\0';
	dm_AddMemoryFailureCallback(cm_OnMemoryFailure, pClientMgr);

	pClientMgr->m_bNotifyRemoves = LTTRUE;
	pClientMgr->m_pCurShell = LTNULL;
	pClientMgr->m_pClientShell = LTNULL;
	pClientMgr->m_hShellModule = LTNULL;
	pClientMgr->m_hClientResourceModule = LTNULL;
	pClientMgr->m_hLocalizedClientResourceModule = LTNULL;
	strcpy(pClientMgr->m_MusicDLLName, "cdaudio.dll");
	pClientMgr->m_MusicMgr.m_bValid = 0;

	pClientMgr->m_TimeSinceUpdate = 0;
	pClientMgr->m_bCanSaveConfigFile = LTFALSE;

	dl_InitList(&pClientMgr->m_Sprites);
	dl_TieOff(&pClientMgr->m_TextureUsers);
	pClientMgr->m_SurfaceSprites = LTNULL;

	VEC_SET(pClientMgr->m_GlobalLightScale, 1.0f, 1.0f, 1.0f);
	VEC_SET(pClientMgr->m_GlobalVertexTint, 1.0f, 1.0f, 1.0f);
	pClientMgr->m_vUnknown754.Init();
	pClientMgr->m_vUnknown760.Init();

	// Initialize the client file mgr.
	pClientMgr->m_hFileMgr = cf_Init(pClientMgr);

	// Initialize all the object banks.
	om_Init(&pClientMgr->m_ObjectMgr, LTTRUE);
	sb_Init2(&pClientMgr->m_FileIDInfoBank, 10, 32, 32);
	pClientMgr->m_SharedTextureBank.Init(64, 64);

	// Initialize all the lists.
	dl_InitList(&pClientMgr->m_SharedTextures);

	input_GetManager(&pClientMgr->m_InputMgr);

	// Init the net manager.
	pClientMgr->m_NetMgr.Init("Player");

	pClientMgr->m_bRendering = LTFALSE;
	pClientMgr->m_nSkyObjects = 0;
	pClientMgr->m_Unknown12e8 = 0;
	pClientMgr->m_Unknown1370 = 0;
	pClientMgr->m_LastTime = 0.0f;
	pClientMgr->m_CurTime = 0.0f;
	pClientMgr->m_iCurInputSlot = 0;
	pClientMgr->m_nLastCommands = 0;
	pClientMgr->m_Unknown1624 = 0;
	pClientMgr->m_bInputState = LTFALSE;
	pClientMgr->m_bTrackingInputDevices = LTFALSE;
	pClientMgr->m_ModelHookFn = LTNULL;
	pClientMgr->m_ModelHookUser = LTNULL;
	pClientMgr->m_nResTrees = 0;

	pClientMgr->m_pDefaultModel = new Model(&g_DefAlloc, &g_DefAlloc);
	pClientMgr->m_pDefaultModel->AddRef();

	pClientMgr->m_MoveAbstract = new CMoveAbstract(pClientMgr);
	pClientMgr->m_pSerializeHelper = new CClientSerializeHelper(pClientMgr);

	// Check everything..
	if(pClientMgr->m_pSerializeHelper && pClientMgr->m_pDefaultModel && pClientMgr->m_MoveAbstract &&
		LOADERTHREAD(pClientMgr)->Init(pClientMgr) && LOADERTHREAD(pClientMgr)->Start(LTPRI_NORMAL) == LT_OK)
	{
		return pClientMgr;
	}

	delete pClientMgr;
	return LTNULL;
}


// Talon does Jupiter's Term here; the members' destructors follow inline.
// FUNCTION: LITHTECH 0x00411720
CClientMgr::~CClientMgr()
{
	int i;

	LOADERTHREAD(this)->Terminate(LTTRUE);
	ProcessLoaderMessages();

	m_bNotifyRemoves = LTFALSE;

	cm_FreeSurfaceSprites(this);

	// Tell the client shell we're going away.
	if(m_pClientShell)
	{
		m_pClientShell->OnEngineTerm();
	}

	TermClientShellDE();

	// Get rid of ALL objects.
	for(i=0; i < NUM_OBJECTTYPES; i++)
	{
		cm_RemoveObjectsInList(this, &m_ObjectMgr.m_ObjectLists[i], LTFALSE);
	}

	// Kill all the shells.
	EndShell();

	cm_SaveAutoConfig(this, "autoexec.cfg");

	// Stop getting input.
	m_InputMgr->Term(m_InputMgr);

	se_RemoveSurfaceEffects(this);

	memset(m_SkyObjects, 0xFF, sizeof(m_SkyObjects));

	SOUNDMGR(this)->Term(LTTRUE);

	// Kill the music driver...
	AppTermMusic();

	// Kill the graph manager
	DEBUGGRAPHMGR(this)->Term();

	FreeModelList(&m_TextureUsers);
	FreeSpriteList(&m_Sprites);

	con_Term(LTTRUE);

	m_NetMgr.Term();

	r_TermRender(this, 2);

	cm_FreeSharedTextures(this);

	c_TermConsoleCommands();

	om_Term(&m_ObjectMgr);

	// Cleanup the object map.
	if(m_ObjectMap)
	{
		dfree(m_ObjectMap);
		m_ObjectMap = LTNULL;
	}

	m_ObjectMapSize = 0;

	sb_Term(&m_FileIDInfoBank);

	// Kill the ClientDE interface..
	ci_Term();
	str_ShowAllStringsAllocated(ClientStringWhine, LTNULL);

	// Kill the file manager.
	if(m_hFileMgr)
	{
		cf_Term(m_hFileMgr);
	}

	TermErrorLog();

	if(m_pClientDE)
	{
		delete m_pClientDE;
		m_pClientDE = LTNULL;
	}

	if(m_MoveAbstract)
	{
		delete m_MoveAbstract;
		m_MoveAbstract = LTNULL;
	}

	if(m_pSerializeHelper)
	{
		delete m_pSerializeHelper;
		m_pSerializeHelper = LTNULL;
	}

	if(m_pDefaultModel)
	{
		m_pDefaultModel->Release();
		delete m_pDefaultModel;
		m_pDefaultModel = LTNULL;
	}
}


// ------------------------------------------------------------------ //
// Internal helpers.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00411b80
static void ClientStringWhine(const char *pString, void *pUser)
{
	con_WhitePrintf("Unfreed string: %s", pString);
}


// Frees every model in the list that nothing else references.
// FUNCTION: LITHTECH 0x00411ba0
static void FreeModelList(LTLink *pListHead)
{
	LTLink *pCur, *pNext;
	Model *pModel;

	for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pNext)
	{
		pNext = pCur->m_pNext;
		((Model*)pCur->m_pData)->AddRef();
	}

	for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pNext)
	{
		pModel = (Model*)pCur->m_pData;
		pNext = pCur->m_pNext;

		pModel->Release();
		if(pModel->m_RefCount == 0)
		{
			delete pModel;
		}
	}

	dl_TieOff(pListHead);
}


// FUNCTION: LITHTECH 0x00411c10
static void FreeSpriteList(LTList *pList)
{
	LTLink *pCur, *pNext, *pListHead;

	pListHead = &pList->m_Head;
	pCur = pListHead->m_pNext;
	while(pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		spr_Destroy((Sprite*)pCur->m_pData);
		pCur = pNext;
	}

	dl_InitList(pList);
}


// Gets a packet for a game message (its ILTMessage reads and writes object references
// through m_pSerializeHelper).
// Only the Release call is scheduled differently (the original sets ecx before the stores).
// FUNCTION: LITHTECH 0x00411c50
CPacket* CClientMgr::AllocPacket()
{
	CPacketRef cPacket;
	CPacket *pPacket;

	// (The reference the caller gets is the one packet_Get's smart pointer drops at the end.)
	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	pPacket = cPacket.m_pPacket;
	pPacket->AddRef();
	pPacket->m_Message.m_Unknown04 = (uint32)m_pSerializeHelper;
	pPacket->m_ErrorFlags |= PACKETFLAG_MESSAGE;
	return pPacket;
}


// FUNCTION: LITHTECH 0x00411cb0
void CClientMgr::SetupPacketMessage(CPacket *pPacket)
{
	pPacket->m_Message.m_Unknown04 = (uint32)m_pSerializeHelper;
}


// FUNCTION: LITHTECH 0x00411cc0
uint16 CClientMgr::IncCurTextureFrameCode()
{
	LTLink *pCur, *pListHead;

	if(m_CurTextureFrameCode == 0xFFFF)
	{
		// Wrapped around: reset all the textures' frame codes.
		m_CurTextureFrameCode = 1;

		pListHead = &m_SharedTextures.m_Head;
		for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			((SharedTexture*)pCur->m_pData)->m_Unknown30 = 0;
		}
	}
	else
	{
		++m_CurTextureFrameCode;
	}

	return m_CurTextureFrameCode;
}


// FUNCTION: LITHTECH 0x00411d10
LTRESULT CClientMgr::FreeUnusedModels()
{
	LTLink *pCur, *pNext, *pListHead;
	Model *pModel;
	LTObject *pObject;

	// Untag all the models.
	pListHead = &m_TextureUsers;
	for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pModel = (Model*)pCur->m_pData;
		pModel->AddRef();
		pModel->m_Flags &= ~MODELFLAG_CACHED;
	}

	// Tag the ones the client's models use.
	for(pCur=m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head.m_pNext; pCur != &m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head; pCur=pCur->m_pNext)
	{
		pObject = (LTObject*)pCur->m_pData;
		if(pObject->m_ObjectID == (uint16)INVALID_OBJECTID)
		{
			((ModelInstance*)pObject)->GetModelDB()->m_Flags |= MODELFLAG_CACHED;
		}
	}

	// Free the untagged ones.
	for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pNext)
	{
		pModel = (Model*)pCur->m_pData;
		pNext = pCur->m_pNext;

		pModel->Release();
		if(pModel->m_RefCount == 0 && !(pModel->m_Flags & MODELFLAG_CACHED))
		{
			dl_Remove(&pModel->m_Link);
			delete pModel;
		}
	}

	return LT_OK;
}


// Adds up the CRCs of the files the world info string names (wildcards allowed: "dir/pre*suf").
// Never returns 0 so the server can tell "checked" from "not set".
// FUNCTION: LITHTECH 0x00411de0
LTRESULT CClientMgr::GetWorldInfoCRC(char *pInfoString, uint32 &crc)
{
	char infoString[1024];
	char prefix[128], dirName[128], suffix[128];
	char *pTok;
	uint32 total;
	uint32 fileCRC;
	int len;
	int prefixLen, suffixLen;
	FileEntry *pList, *pCur;
	char *pWild;
	ILTStream *pStream;

	if(!pInfoString || !pInfoString[0])
		return LT_OK;

	strcpy(infoString, pInfoString);

	total = 0;
	pTok = strtok(infoString, "; \n\r\t");
	while(pTok)
	{
		pWild = strstr(pTok, "*");
		if(pWild)
		{
			// Split it into the part before and after the wildcard.
			strncpy(prefix, pTok, pWild - pTok);
			strcpy(suffix, pWild+1);
			prefix[pWild - pTok] = 0;

			// Get the directory.
			strcpy(dirName, prefix);
			for(len=strlen(dirName); len > 0; len--)
			{
				if(dirName[len] == '\\' || dirName[len] == '/')
					break;

				dirName[len] = 0;
			}

			pList = cf_GetFileList(m_hFileMgr, dirName);
			prefixLen = strlen(prefix);
			suffixLen = strlen(suffix);

			for(pCur=pList; pCur; pCur=pCur->m_pNext)
			{
				if(pCur->m_Type == TYPE_FILE &&
					strnicmp(pCur->m_pFullFilename, prefix, prefixLen) == 0 &&
					strnicmp(&pCur->m_pFullFilename[strlen(pCur->m_pFullFilename) - suffixLen], suffix, suffixLen) == 0)
				{
					FileRef ref;
					ref.m_FileType = FILE_CLIENTFILE;
					ref.m_pFilename = pCur->m_pFullFilename;
					pStream = cf_OpenFile(m_hFileMgr, &ref);
					if(pStream)
					{
						m_pClientDE->Common()->GetCRC(pStream, fileCRC);
						total += fileCRC;
						pStream->Release();
					}
				}
			}

			ic_FreeFileList(pList);
		}
		else
		{
			FileRef ref;
			ref.m_FileType = FILE_CLIENTFILE;
			ref.m_pFilename = pTok;
			pStream = cf_OpenFile(m_hFileMgr, &ref);
			if(pStream)
			{
				m_pClientDE->Common()->GetCRC(pStream, fileCRC);
				total += fileCRC;
				pStream->Release();
			}
		}

		pTok = strtok(LTNULL, "; \n\r\t");
	}

	if(!total)
		total = 1;

	crc = total;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00412070
void cm_OnEnterServer(CClientMgr *pClientMgr)
{
	cf_OnConnect(pClientMgr->m_hFileMgr, pClientMgr->m_pCurShell->m_HostID);

	// Mirror the server's console state.
	memset(&pClientMgr->m_ServerConsoleMirror, 0, sizeof(pClientMgr->m_ServerConsoleMirror));
	pClientMgr->m_ServerConsoleMirror.Alloc = dalloc;
	pClientMgr->m_ServerConsoleMirror.Free = dfree;
	cc_InitState(&pClientMgr->m_ServerConsoleMirror);

	pClientMgr->m_nLastCommands = 0;
}


// FUNCTION: LITHTECH 0x004120d0
void cm_OnExitServer(CClientMgr *pClientMgr, CClientShell *pShell)
{
	if(pShell == pClientMgr->m_pCurShell)
	{
		memset(pClientMgr->m_SkyObjects, 0xFF, sizeof(pClientMgr->m_SkyObjects));
		cf_OnDisconnect(pClientMgr->m_hFileMgr);
		cc_TermState(&pClientMgr->m_ServerConsoleMirror);
	}
}


// Runs the +command arguments.
// FUNCTION: LITHTECH 0x00412110
void cm_RunCommandLine(CClientMgr *pClientMgr, uint32 flags, CmdLineArgs *pArgs)
{
	int i;
	char str[512];

	if(pArgs)
	{
		for(i=0; i < pArgs->m_Argc; i++)
		{
			if(pArgs->m_Argv[i][0] == '+')
			{
				sprintf(str, "(%s) (%s)", &pArgs->m_Argv[i][1], pArgs->m_Argv[i+1]);
				cc_HandleCommand2(&g_ClientConsoleState, str, flags);
			}
		}
	}
	else
	{
		for(i=0; i < __argc-1; i++)
		{
			if(__argv[i][0] == '+')
			{
				sprintf(str, "(%s) (%s)", &__argv[i][1], __argv[i+1]);
				cc_HandleCommand2(&g_ClientConsoleState, str, flags);
			}
		}
	}
}


// FUNCTION: LITHTECH 0x004121e0
void cm_RunAutoConfig(CClientMgr *pClientMgr, const char *pFilename, uint32 flags)
{
	g_pCommandClientMgr = pClientMgr;
	cc_RunConfigFile(&g_ClientConsoleState, pFilename, flags, VARFLAG_SAVE);
}


// FUNCTION: LITHTECH 0x00412210
void cm_SaveAutoConfig(CClientMgr *pClientMgr, const char *pFilename)
{
	if(pClientMgr->m_bCanSaveConfigFile)
	{
		cc_SaveConfigFile(&g_ClientConsoleState, pFilename);
	}
}


// FUNCTION: LITHTECH 0x00412230
LTRESULT cm_StartRenderFromGlobals(CClientMgr *pClientMgr)
{
	RMode mode;

	_GetRModeFromConsoleVariables(&mode);
	return r_InitRender(pClientMgr, &mode);
}


// What cm_Render hands the renderer (RenderStruct::RenderScene). Talon layout, names from
// Jupiter's SceneDesc where the use matches.
// The scene's screen rectangle. The original's LTRect constructor sits out of line at 0x00412660
// (it is called with four zeros for sceneDesc.m_Rect), so it can't be the SDK's inline LTRect.
struct SceneRect
{
	SceneRect(int l=0, int t=0, int r=0, int b=0);

	int left, top, right, bottom;
};

struct SceneDesc
{
	int			m_DrawMode;						// 0x00 DRAWMODE_
	uint32		*m_pTicks_Render_Objects;		// 0x04
	uint32		*m_pTicks_Render_Models;		// 0x08
	uint32		*m_pTicks_Render_Sprites;		// 0x0c
	uint32		*m_pTicks_Render_WorldModels;	// 0x10
	uint32		*m_pTicks_Render_ParticleSystems;	// 0x14
	uint32		*m_pTicks_Render_PolyGrids;		// 0x18 (name unknown)
	LTVector	m_GlobalModelLightAdd;			// 0x1c console "modeladd"
	LTVector	m_GlobalModelDirAdd;			// 0x28 (name unknown)
	uint32		m_hRenderContext;				// 0x34 the world's render data (MainWorld+0x1c8)
	LTVector	m_Unknown38;					// 0x38 CClientMgr::m_vUnknown760
	LTVector	m_Unknown44;					// 0x44 CClientMgr::m_vUnknown754
	LTVector	m_GlobalLightScale;				// 0x50
	LTVector	m_GlobalVertexTint;				// 0x5c
	LTVector	m_GlobalModelDirAdd2;			// 0x68 (name unknown)
	LTVector	m_GlobalLightAdd;				// 0x74 the camera's light add
	float		m_FrameTime;					// 0x80
	SkyDef		m_SkyDef;						// 0x84
	LTObject	**m_SkyObjects;					// 0xb4
	int			m_nSkyObjects;					// 0xb8
	SceneRect	m_Rect;							// 0xbc
	float		m_xFov, m_yFov;					// 0xcc
	float		m_FarZ;							// 0xd4
	LTVector	m_Pos;							// 0xd8
	LTRotation	m_Rotation;						// 0xe4
	LTObject	**m_pObjectList;				// 0xf4
	int			m_ObjectListSize;				// 0xf8
	ModelHookFn	m_ModelHookFn;					// 0xfc
	void		*m_ModelHookUser;				// 0x100
};

#define MAX_SKYOBJECTS		30


LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 id);	// 0x004265e0 (cutil.cpp)


// FUNCTION: LITHTECH 0x00412660 ??0SceneRect@@QAE@HHHH@Z
SceneRect::SceneRect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b)
{
}

// FUNCTION: LITHTECH 0x00412260
LTBOOL cm_Render(CClientMgr *pClientMgr, CameraInstance *pCamera, int drawMode,
	LTObject **pObjects, int nObjects)
{
	uint32 width, height, i;
	SceneDesc *pDesc;
	Counter renderTicks;
	SceneDesc sceneDesc;
	LTObject *skyObjects[MAX_SKYOBJECTS];

	if(!r_IsRenderInitted() || pClientMgr->m_bRendering)
		return LTFALSE;

	pDesc = &sceneDesc;

	pDesc->m_DrawMode = drawMode;

	width = g_Render.m_Width;
	height = g_Render.m_Height;

	// Get stuff from the camera object.
	if(pClientMgr->m_pCurShell && pClientMgr->m_pCurShell->GetWorld())
		pDesc->m_hRenderContext = pClientMgr->m_pCurShell->GetWorld()->m_Unknown1C8;
	else
		pDesc->m_hRenderContext = 0;

	pDesc->m_Pos = pCamera->m_Pos;
	pDesc->m_Rotation = pCamera->m_Rotation;
	pDesc->m_xFov = pCamera->m_xFov;
	pDesc->m_yFov = pCamera->m_yFov;

	if(pCamera->m_bFullScreen)
	{
		pDesc->m_Rect.left = pDesc->m_Rect.top = 0;
		pDesc->m_Rect.right = width;
		pDesc->m_Rect.bottom = height;
	}
	else
	{
		pDesc->m_Rect.left = pCamera->m_Left;
		pDesc->m_Rect.top = pCamera->m_Top;
		pDesc->m_Rect.right = pCamera->m_Right;
		pDesc->m_Rect.bottom = pCamera->m_Bottom;
	}

	pDesc->m_FarZ = g_CV_FarZ;

	pDesc->m_Unknown38 = pClientMgr->m_vUnknown760;
	pDesc->m_Unknown44 = pClientMgr->m_vUnknown754;
	pDesc->m_GlobalLightScale = pClientMgr->m_GlobalLightScale;
	pDesc->m_GlobalVertexTint = pClientMgr->m_GlobalVertexTint;
	pDesc->m_GlobalLightAdd = pCamera->m_LightAdd;

	pDesc->m_GlobalModelLightAdd = g_ConsoleModelAdd;
	pDesc->m_GlobalModelDirAdd = g_ConsoleModelDirAdd;
	pDesc->m_GlobalModelDirAdd2 = g_ConsoleModelDirAdd2;

	pDesc->m_pTicks_Render_Objects = &g_Ticks_Render_Objects;
	pDesc->m_pTicks_Render_Models = &g_Ticks_Render_Models;
	pDesc->m_pTicks_Render_Sprites = &g_Ticks_Render_Sprites;
	pDesc->m_pTicks_Render_WorldModels = &g_Ticks_Render_WorldModels;
	pDesc->m_pTicks_Render_ParticleSystems = &g_Ticks_Render_ParticleSystems;
	pDesc->m_pTicks_Render_PolyGrids = &g_Ticks_Render_PolyGrids;
	pDesc->m_FrameTime = pClientMgr->m_FrameTime;

	pDesc->m_pObjectList = pObjects;
	pDesc->m_ObjectListSize = nObjects;

	// Sky info.
	memcpy(&pDesc->m_SkyDef, &pClientMgr->m_SkyDef, sizeof(pDesc->m_SkyDef));

	pDesc->m_nSkyObjects = 0;
	pDesc->m_SkyObjects = skyObjects;
	for(i=0; i < MAX_SKYOBJECTS; i++)
	{
		skyObjects[pDesc->m_nSkyObjects] = cm_FindObject(pClientMgr, pClientMgr->m_SkyObjects[i]);
		if(skyObjects[pDesc->m_nSkyObjects])
			++pDesc->m_nSkyObjects;
	}

	// Model hook stuff.
	pDesc->m_ModelHookFn = pClientMgr->m_ModelHookFn;
	pDesc->m_ModelHookUser = pClientMgr->m_ModelHookUser;

	pClientMgr->m_bRendering = LTTRUE;

	{
		CountAdder cntAdd(&g_Ticks_Render);
		CountAdder cntAdd2(&g_Ticks_RenderScene);

		if(g_CV_RenderEnable)
		{
			g_Render.RenderScene(&sceneDesc);
		}
	}

	pClientMgr->m_bRendering = LTFALSE;
	{
		CDemoMgr *pDemoMgr = &pClientMgr->m_DemoMgr;
		pDemoMgr->m_nFramesDrawn++;
	}
	g_Render.m_Unknown154 = 0;

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00412680
void cm_RebindTextures(CClientMgr *pClientMgr)
{
	pClientMgr->UnbindSharedTextures();
	pClientMgr->BindSharedTextures();
}


// FUNCTION: LITHTECH 0x004126a0
LTRESULT cm_AddObjectToClientWorld(CClientMgr *pClientMgr, uint16 objectID, InternalObjectSetup *pSetup,
	LTObject **ppObject, LTBOOL bMove, LTBOOL bRotate)
{
	LTRESULT dResult;
	LTObject *pObject;

	*ppObject = LTNULL;

	dResult = om_CreateObject(&pClientMgr->m_ObjectMgr, pSetup->m_pSetup, &pObject);
	if(dResult != LT_OK)
		return dResult;

	dResult = so_ExtraInit(pClientMgr, pObject, pSetup, LTFALSE);
	if(dResult != LT_OK)
	{
		om_DestroyObject(&pClientMgr->m_ObjectMgr, pObject);
		return dResult;
	}

	// Add it to the object map.
	pObject->m_ObjectID = objectID;
	if(pObject->m_ObjectID != (uint16)INVALID_OBJECTID)
	{
		cm_AddToObjectMap(pClientMgr, pObject->m_ObjectID);
		pClientMgr->m_ObjectMap[pObject->m_ObjectID].m_nRecordType = RECORDTYPE_LTOBJECT;
		pClientMgr->m_ObjectMap[pObject->m_ObjectID].m_pRecordData = pObject;
	}

	if(bMove)
	{
		if(bRotate)
		{
			cm_MoveAndRotateObject(pClientMgr, pObject, &pSetup->m_pSetup->m_Pos, &pSetup->m_pSetup->m_Rotation);
		}
		else
		{
			cm_MoveObject(pClientMgr, pObject, &pSetup->m_pSetup->m_Pos, LTTRUE);
		}
	}
	else if(bRotate)
	{
		cm_RotateObject(pClientMgr, pObject, &pSetup->m_pSetup->m_Rotation);
	}

	if(pSetup->m_pSetup->m_Scale.x != 1.0f || pSetup->m_pSetup->m_Scale.y != 1.0f || pSetup->m_pSetup->m_Scale.z != 1.0f)
	{
		cm_ScaleObject(pClientMgr, pObject, &pSetup->m_pSetup->m_Scale);
	}

	if(pObject->m_ObjectType == OT_WORLDMODEL || pObject->m_ObjectType == OT_CONTAINER)
	{
		InitialWorldModelRotate((WorldModelInstance*)pObject);
	}

	*ppObject = pObject;
	g_Render.m_Unknown154 |= 1;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00412820
LTRESULT cm_RemoveObjectFromClientWorld(CClientMgr *pClientMgr, LTObject *pObject)
{
	DetachObjectStanding(pObject);
	DetachObjectsStandingOn(pObject);
	w_RemoveObjectFromLeaf(pObject);

	// Tell the client shell.
	if(pObject->m_Unknown188 & CF_NOTIFYREMOVE)
	{
		if(pClientMgr->m_pClientShell)
		{
			pClientMgr->m_pClientShell->OnObjectRemove((HLOCALOBJ)pObject);
		}
	}

	so_ExtraTerm(pClientMgr, pObject);

	if(pObject->m_ObjectID != (uint16)INVALID_OBJECTID)
	{
		cm_ClearObjectMapEntry(pClientMgr, pObject->m_ObjectID);
	}

	if(pObject->m_ObjectType == OT_MODEL)
	{
		cm_FreeUnusedModelTextures(pClientMgr, pObject);
	}

	om_DestroyObject(&pClientMgr->m_ObjectMgr, pObject);
	g_Render.m_Unknown154 |= 2;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x004128b0
void cm_RemoveObjectsInList(CClientMgr *pClientMgr, LTList *pList, LTBOOL bServerOnly)
{
	LTLink *pCur, *pNext, *pListHead;
	LTObject *pObject;

	pListHead = &pList->m_Head;
	pCur = pListHead->m_pNext;
	while(pCur != pListHead)
	{
		pNext = pCur->m_pNext;

		pObject = (LTObject*)pCur->m_pData;
		if(!bServerOnly || pObject->m_ObjectID != (uint16)INVALID_OBJECTID)
		{
			cm_RemoveObjectFromClientWorld(pClientMgr, pObject);
		}

		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00412900
void cm_FreeSurfaceSprites(CClientMgr *pClientMgr)
{
	SurfaceSprite *pCur, *pNext;

	pCur = pClientMgr->m_SurfaceSprites;
	while(pCur)
	{
		pNext = pCur->m_pNext;
		dfree(pCur);
		pCur = pNext;
	}

	pClientMgr->m_SurfaceSprites = LTNULL;
}


// A model user hook entry (the Leech cm_BindModel puts on the model's nexus): m_pRef is the
// leech's user data, the model's FileIdentifier, whose m_pData is the model.
struct ClientModelUser
{
	void			*m_pOwner;		// 0x00
	FileIdentifier	*m_pRef;		// 0x04
};

// Called (through the hook at 0x004d03f8) when a model reference goes away.
// FUNCTION: LITHTECH 0x00412930
LTRESULT cm_OnModelRefRemoved(void *pUser, ClientModelUser *pUser2, LTBOOL bServer)
{
	if(!bServer && pUser2->m_pRef && pUser2->m_pRef->m_pData)
	{
		cm_RemoveModelObjects(g_pClientMgr, (Model*)pUser2->m_pRef->m_pData, pUser2->m_pRef);
	}

	return LT_OK;
}


// ------------------------------------------------------------------ //
// Template and inline code emitted with this file.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00411650 ??0LTList@@QAE@XZ
// FUNCTION: LITHTECH 0x00411710 ?GetPhysics@CMoveAbstract@@UAEPAVILTPhysics@@XZ
// LTVector(x, y, z), called out of line by cm_Init (the SetForce gravity vector).
// FUNCTION: LITHTECH 0x00412960 ??0?$_CVector@M@@QAE@MMM@Z
// FUNCTION: LITHTECH 0x00412980 ?GenGetNext@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBEPAUWorldPoly@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004129a0 ?GenAppendList@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEHABV?$GenList@PAUWorldPoly@@@@@Z
// FUNCTION: LITHTECH 0x00412a70 ?AllocVoid@?$ObjectBank@VModelInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412ab0 ?AllocVoid@?$ObjectBank@VWorldModelInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412af0 ?AllocVoid@?$ObjectBank@VSpriteInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412b30 ?AllocVoid@?$ObjectBank@VDynamicLight@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412b70 ?AllocVoid@?$ObjectBank@VCameraInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412bb0 ?AllocVoid@?$ObjectBank@VLTParticleSystem@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412bf0 ?FreeVoid@?$ObjectBank@VLTObject@@VNullCS@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00412c20 ?AllocVoid@?$ObjectBank@VLTPolyGrid@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412c60 ?AllocVoid@?$ObjectBank@VLineSystem@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412ca0 ?AllocVoid@?$ObjectBank@VContainerInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412ce0 ?AllocVoid@?$ObjectBank@VCanvas@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00412d20 ?GenGetNext@?$CMoArray@ULightAnim@@VDefaultCache@@@@UBE?AULightAnim@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00412d50 ?GenGetAt@?$CMoArray@ULightAnim@@VDefaultCache@@@@UBE?AULightAnim@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00412d80 ?GenAppend@?$CMoArray@ULightAnim@@VDefaultCache@@@@UAEHAAULightAnim@@@Z
// FUNCTION: LITHTECH 0x00412ec0 ?GenRemoveAt@?$CMoArray@ULightAnim@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00412fe0 ?GenCopyList@?$CMoArray@ULightAnim@@VDefaultCache@@@@UAEHABV?$GenList@ULightAnim@@@@@Z
// FUNCTION: LITHTECH 0x00413120 ?GenAppendList@?$CMoArray@ULightAnim@@VDefaultCache@@@@UAEHABV?$GenList@ULightAnim@@@@@Z
// FUNCTION: LITHTECH 0x00413230 ?GenFindElement@?$CMoArray@ULightAnim@@VDefaultCache@@@@UBEHABULightAnim@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413260 ?GenRemoveAt@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413350 ?GenAppendList@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@UAEHABV?$GenList@ULAPolyRef@@@@@Z
// FUNCTION: LITHTECH 0x00413430 ?GenIsValid@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBEHABVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413450 ?GenGetNext@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@UBE?AULAPolyRef@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413470 ?GenGetAt@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@UBE?AULAPolyRef@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413490 ?GenCopyList@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@UAEHABV?$GenList@ULAPolyRef@@@@@Z
// FUNCTION: LITHTECH 0x004135b0 ?GenGetNext@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@UBE?AULAPolyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004135e0 ?GenGetAt@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@UBE?AULAPolyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413610 ?GenAppend@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@UAEHAAULAPolyFrame@@@Z
// FUNCTION: LITHTECH 0x00413750 ?GenRemoveAt@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00413880 ?GenCopyList@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@UAEHABV?$GenList@ULAPolyFrame@@@@@Z
// FUNCTION: LITHTECH 0x004139c0 ?GenAppendList@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@UAEHABV?$GenList@ULAPolyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00413ad0 ?AllocVoid@?$ObjectBank@USharedTexture@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00413b10 ?FreeVoid@?$ObjectBank@USharedTexture@@VNullCS@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00413ce0 ?SetSize2@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00413d50 ?InternalNiceSetSize@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00413e30 ??1?$ObjectBank@VLTObject@@VNullCS@@@@UAE@XZ
// FUNCTION: LITHTECH 0x00413e50 ?AllocVoid@?$ObjectBank@VLTObject@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00413e90 ?Term@?$ObjectBank@VLTObject@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413eb0 ?Term@?$ObjectBank@VModelInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413ed0 ?Term@?$ObjectBank@VWorldModelInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413ef0 ?Term@?$ObjectBank@VSpriteInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413f10 ?Term@?$ObjectBank@VDynamicLight@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413f30 ?Term@?$ObjectBank@VCameraInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413f50 ?Term@?$ObjectBank@VLTParticleSystem@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413f70 ?Term@?$ObjectBank@VLTPolyGrid@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413f90 ?Term@?$ObjectBank@VLineSystem@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413fb0 ?Term@?$ObjectBank@VContainerInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413fd0 ?Term@?$ObjectBank@VCanvas@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00413ff0 ?InternalNiceSetSize@?$CMoArray@ULightAnim@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00414110 ?SetSize2@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00414180 ?InternalNiceSetSize@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00414290 ??0?$ObjectBank@USharedTexture@@VNullCS@@@@QAE@XZ
// FUNCTION: LITHTECH 0x004142b0 ?Term@?$ObjectBank@USharedTexture@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x004142d0 ?Term@?$ObjectBank@VLTLink@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x004142f0 ??_G?$ObjectBank@VLTObject@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00414330 ??_G?$ObjectBank@VModelInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00414370 ??_G?$ObjectBank@VWorldModelInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004143b0 ??_G?$ObjectBank@VSpriteInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004143f0 ??_G?$ObjectBank@VDynamicLight@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00414430 ??_G?$ObjectBank@VCameraInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00414470 ??_G?$ObjectBank@VLTParticleSystem@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004144b0 ??_G?$ObjectBank@VLTPolyGrid@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004144f0 ??_G?$ObjectBank@VLineSystem@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00414530 ??_G?$ObjectBank@VContainerInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00414570 ??_G?$ObjectBank@VCanvas@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004145b0 ??_G?$ObjectBank@USharedTexture@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004145f0 ??_G?$ObjectBank@VLTLink@@VNullCS@@@@UAEPAXI@Z
// Template code the WONAPI smart pointers and std::string leave behind.
// FUNCTION: LITHTECH 0x00410310 ??1PeerAuthClient@WONAPI@@QAE@XZ
// (folded with the std::string<char> one)
// FUNCTION: LITHTECH 0x00413b40 ??1?$_String_base@EV?$allocator@E@_STL@@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00413b90 ??4?$ConstSmartPtr@VByteBuffer@WONAPI@@@WONAPI@@QAEPBVByteBuffer@1@PBV21@@Z
// FUNCTION: LITHTECH 0x00413bf0 ?_M_do_lock@?$_STL_mutex_spin@$0A@@_STL@@SAXPCK@Z
// FUNCTION: LITHTECH 0x00414630 ?Insert2@?$CMoArray@EVDefaultCache@@@@QAEHKABEPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00414720 ?_DeleteAndDestroyArray@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@AAEXPAVLAlloc@@K@Z
// CLithChunkAllocator<T>::Term's delete[] (called from CLTDirectMusicMgr::Term); the copies come from here
// because this object links before ltdirectmusic_impl.

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004359b0 (the empty callbacks).
void cm_OnMemoryFailure(void *pUser)
{
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x00473ac0 (the empty void functions).
void cm_OnTextureBound(CClientMgr *pClientMgr)
{
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x00473ac0 (the empty void functions).
void cm_ShowLoadProgress(CClientMgr *pClientMgr)
{
}

// FUNCTION: LITHTECH 0x00414750 ??_ECSegment@CLTDirectMusicMgr@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x004147e0 ??_ECStyle@CLTDirectMusicMgr@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x00414860 ?BaseNew@@YAPAPAUWorldPoly@@PAVLAlloc@@PAPAU1@K@Z
// FUNCTION: LITHTECH 0x00414880 ?BaseNew@@YAPAULightAnim@@PAVLAlloc@@PAU1@K@Z
// FUNCTION: LITHTECH 0x004148c0 ?BaseNew@@YAPAULAPolyFrame@@PAVLAlloc@@PAU1@K@Z
