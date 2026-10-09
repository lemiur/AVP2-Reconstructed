// Talon client manager (Jupiter runtime/client/src/clientmgr.h). Only recovered members.
// One definition for the engine: add members here at their exact offsets.
// Talon passes the manager explicitly (cm_ functions take it as the first argument), while the
// ci_ interface functions reach it through g_pClientMgr.
#ifndef __CLIENTMGR_H__
#define __CLIENTMGR_H__

#include <stddef.h>
// First: lthread.h (through cloaderthread.h) brings in <windows.h>, which has to come before the StdLith
// headers.
#include "cloaderthread.h"
#include "ltbasedefs.h"
#include "motion.h"
#include "netmgr.h"
#include "concommand.h"
#include "world_tree.h"
#include "objectmgr.h"
#include "ratetracker.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "pixelformat.h"
#include "version_info.h"
#include "musicmgr.h"
#include "demomgr.h"
#include "soundmgr.h"
#include "debuggraphmgr.h"

// Talon client file manager handle (client_filemgr).
struct ClientFileMgr;
class FileRef;
class CClientShell;
class VideoMgr;
class MoveAbstract;
class ILTCursor;
class ILTDirectMusicMgr;
class LThreadMessage;
class CLoaderThread;
class ModelInstance;
struct LightAnim;
struct WorldData;

// 0x00404820 (client_filemgr.h; Ghidra: CPacket_Data::Free). Jupiter: IClientFileMgr::OpenFile.
ILTStream* cf_OpenFile(ClientFileMgr *hFileMgr, FileRef *pRef);


// The client manager starts with its world (MainWorld, de_mainworld.h): cm_Init (0x004112c0)
// constructs a MainWorld at offset 0 and the ObjectMgr at 0x1f0, just as CServerMgr does at 0x1a4/0x394.
class CClientMgr
{
public:
	// 0x00425d30. Variadic: the arguments format the error's message.
	LTRESULT		SetupError(LTRESULT theError, ...);
	void			ForwardMessagesToScript();							// 0x00425e00
	void			ForwardCommandChanges(int32 *pChanges, int32 nChanges);	// 0x00425ea0
	void			UpdateFrameRate();									// 0x00425f00

	// clientmgr.cpp, used by render.cpp.
	LTBOOL			BindClientShellWorlds();							// 0x00410c00
	void			UnbindClientShellWorlds();							// 0x00410c20
	void			BindSharedTextures();								// 0x00410c30
	void			UnbindSharedTextures();								// 0x00410c80
	void			InitConsole();										// 0x00410cb0
	void			TermConsole();										// 0x00410d00
	uint16			IncCurTextureFrameCode();							// 0x00411cc0

	// 0x00411c50: a fresh packet for a message.
	class CPacket*	AllocPacket();
	// Gives a received message the client's LMessageHelper (LMessageImpl::m_Unknown04).
	void			SetupPacketMessage(class CPacket *pPacket);			// 0x00411cb0

	// cobject.cpp: updates animations, particle systems, poly grids, line systems and models.
	// cnet.cpp: sends our command changes (and queued sound updates) to the server.
	void			SendUpdate(CNetMgr *pNetMgr, CBaseConn *pConnID, int32 *pCommands, int nCommands);	// 0x00417a50
	void			UpdateObjects();									// 0x00417de0

	// 0x00411d10.
	LTRESULT		FreeUnusedModels();

	// clientmgr.cpp: startup, the main loop and shutdown (kernel/sys/win/client.cpp).
	// Talon passes the command line (CmdLineArgs) instead of using a command line holder.
	LTRESULT		Init(const char **resTrees, uint32 nResTrees, const char *pConfigFile,
						struct CmdLineArgs *pArgs);					// 0x0040fc70
	LTRESULT		Update();										// 0x00410db0
	void			InitGlobals();									// 0x00410d10 sets g_pClientMgr (Ghidra: WorldTree::InitWorldTree)
	void			StartPeerAuth();								// 0x00410050 (Ghidra: CNetMgr::SendPacket)
	void			OnPeerAuthPacket(class CPacket *pPacket);		// 0x004101c0
	void			TermClientShellDE();							// 0x004103d0
	void			OnEnterWorld(CClientShell *pShell);				// 0x004104c0
	void			OnExitWorld(CClientShell *pShell);				// 0x004104e0
	// Checksums the world info string the server sent (CClientShell::DoLoadWorld).
	LTRESULT		GetWorldInfoCRC(char *pInfoString, uint32 &crc);	// 0x00411de0
	void			EndShell();										// 0x00410a40
	void			AppTermMusic();									// 0x00410bf0
	void			ProcessLoaderMessage(LThreadMessage &msg);		// 0x00410d30
	void			ProcessLoaderMessages();						// 0x00410d60
	void			ProcessAllInput(LTBOOL bForceClear);			// 0x004110b0
	LTRESULT		PlaySound(PlaySoundInfo *pPlaySoundInfo, FileRef *pFile, float fOffsetTime);	// 0x004111b0
	void			UpdateAllSounds(float fFrameTime);				// 0x00411260
					~CClientMgr();									// 0x00411720 (Ghidra: CClientMgr::Term)

	// clientmgr.cpp, used by the ci_ interface functions.
	LTRESULT		StartShell(StartGameRequest *pRequest);			// 0x00410500
	LTRESULT		AppInitMusic(char *pMusicDLL);					// 0x00410a70
	LTRESULT		ClearInput();									// 0x00411150
	void			ShowDrawSurface(uint32 flags);					// 0x00411180

	MainWorld		m_World;			// 0x0000
	ObjectMgr		m_ObjectMgr;		// 0x01f0 (its m_ObjectLists are at 0x410)
	class CClientSerializeHelper	*m_pSerializeHelper;	// 0x04c0 ILTMessage helper (cloaderthread.h)
	class ILTClient	*m_pClientDE;		// 0x04c4 handed to the client shell's create function
	MotionState		m_MotionState;		// 0x04c8
	CollisionInfo	*m_pCollisionInfo;	// 0x0508 only valid during touch notifies (CMoveAbstract)
	MoveAbstract	*m_MoveAbstract;	// 0x050c
	CNetMgr			m_NetMgr;			// 0x0510 (0x104 bytes)
	class CBindModuleType	*m_hClientResourceModule;			// 0x0614 cres.dll
	class CBindModuleType	*m_hLocalizedClientResourceModule;	// 0x0618 cresl.dll
	struct ShellBindModule	*m_hShellModule;					// 0x061c cshell.dll
	class IClientShell		*m_pClientShell;					// 0x0620
	char			m_ErrorString[301];	// 0x0624 (SetupError)
	uint8			m_Pad0751[0x754 - 0x751];
	LTVector		m_vUnknown754;		// 0x0754 handed to the renderer (SceneDesc+0x44)
	LTVector		m_vUnknown760;		// 0x0760 handed to the renderer (SceneDesc+0x38)
	LTVector		m_GlobalLightScale;	// 0x076c (values 0-2)
	LTVector		m_GlobalVertexTint;	// 0x0778 (values 0-1)
	CSoundMgr		m_SoundMgr;			// 0x0784 (soundmgr.h; 0x910 bytes), the client ILTSoundMgr
	SMusicMgr		m_MusicMgr;			// 0x1094
	char			m_MusicDLLName[256];	// 0x1104
	LTLink			m_TextureUsers;		// 0x1204 the client models (Model::m_Link; cm_TagUsedTextures)
	LTList			m_Sprites;			// 0x1210
	LTList			m_SharedTextures;	// 0x1220
	StructBank		m_FileIDInfoBank;	// 0x1230 FileIDInfos (CClientShell::GetClientFileIDInfo)
	ObjectBank<SharedTexture>	m_SharedTextureBank;	// 0x124c
	uint16			m_SkyObjects[30];	// 0x1270 MAX_SKYOBJECTS object IDs (0xFFFF = none)
	uint16			m_nSkyObjects;		// 0x12ac
	uint8			m_Pad12ae[0x12b0 - 0x12ae];
	SkyDef			m_SkyDef;			// 0x12b0
	LTVersionInfo	m_VersionInfo;		// 0x12e0
	uint32			m_Unknown12e8;		// 0x12e8
	struct ObjectMapEntry	*m_ObjectMap;	// 0x12ec (indexed by object ID; servermgr.h)
	uint32			m_ObjectMapSize;	// 0x12f0
	RateTracker		m_FramerateTracker;	// 0x12f4
	CDebugGraphMgr	m_DebugGraphMgr;	// 0x1300 (debuggraphmgr.h; 0x64 bytes)
	float			m_LastTime;			// 0x1364 m_CurTime of the previous frame (demomgr)
	float			m_FrameTime;		// 0x1368
	float			m_CurTime;			// 0x136c (pd_InitialServerUpdate)
	uint32			m_Unknown1370;		// 0x1370
	struct SurfaceSprite	*m_SurfaceSprites;	// 0x1374 animated world textures (clientshell.h)
	ConsoleState	m_ServerConsoleMirror;	// 0x1378
	uint8			m_Commands[2][255];	// 0x13bc command states per input slot
	uint8			m_Pad15ba[0x15bc - 0x15ba];
	int				m_iCurInputSlot;	// 0x15bc
	uint8			m_LastCommands[64];	// 0x15c0 command changes sent in the last update (cnet.cpp)
	uint32			m_nLastCommands;	// 0x1600
	float			m_TimeSinceUpdate;	// 0x1604 time since the last update was sent
	uint8			m_LastUpdateRate;	// 0x1608
	uint8			m_Pad1609[0x160c - 0x1609];
	float			m_AxisOffsets[3];	// 0x160c
	class InputMgr	*m_InputMgr;		// 0x1618 (input.h)
	CClientShell	*m_pCurShell;		// 0x161c
	uint8			m_Pad1620[0x1624 - 0x1620];
	uint32			m_Unknown1624;		// 0x1624
	LTBOOL			m_bCanSaveConfigFile;	// 0x1628
	LTBOOL			m_bInputState;		// 0x162c FALSE tells the server to ignore our input.
	LTBOOL			m_bTrackingInputDevices;	// 0x1630
	ModelHookFn		m_ModelHookFn;		// 0x1634
	void			*m_ModelHookUser;	// 0x1638
	LTBOOL			m_bRendering;		// 0x163c TRUE inside cm_Render
	LTBOOL			m_bNotifyRemoves;	// 0x1640 cleared first thing in ~CClientMgr
	ClientFileMgr	*m_hFileMgr;		// 0x1644
	const char		*m_ResTrees[20];	// 0x1648 (Init)
	uint32			m_nResTrees;		// 0x1698
	CDemoMgr		m_DemoMgr;			// 0x169c (demomgr.h)
	// (cloaderthread.h, 0x64 bytes). LOADERTHREAD(pMgr) is &pMgr->m_LoaderThread.
	CLoaderThread	m_LoaderThread;		// 0x16b8
	class Model		*m_pDefaultModel;	// 0x171c ref-counted Model made by cm_Init (name unknown)
	VideoMgr		*m_pVideoMgr;		// 0x1720
	ILTCursor		*m_pCursorMgr;		// 0x1724
	FormatMgr		m_FormatMgr;		// 0x1728 (GetFormatMgr 0x0040c510)
	uint16			m_CurTextureFrameCode;	// 0x22bc (IncCurTextureFrameCode)
	uint8			m_Pad22be[0x22c0 - 0x22be];
	ILTDirectMusicMgr	*m_pDirectMusicMgr;	// 0x22c0
	uint32			m_Unknown22c4;		// 0x22c4
};

// Layout checks: a member that moves breaks the build here instead of silently shifting offsets.
#define CM_CHECKOFFSET(member, ofs) \
	typedef char CM_Check##member[(offsetof(CClientMgr, member) == (ofs)) ? 1 : -1];
CM_CHECKOFFSET(m_World, 0x0)
CM_CHECKOFFSET(m_MotionState, 0x4c8)
CM_CHECKOFFSET(m_NetMgr, 0x510)
CM_CHECKOFFSET(m_hClientResourceModule, 0x614)
CM_CHECKOFFSET(m_SoundMgr, 0x784)
CM_CHECKOFFSET(m_ServerConsoleMirror, 0x1378)
CM_CHECKOFFSET(m_ObjectMgr, 0x1f0)
CM_CHECKOFFSET(m_ErrorString, 0x624)
CM_CHECKOFFSET(m_SharedTextureBank, 0x124c)
CM_CHECKOFFSET(m_FramerateTracker, 0x12f4)
CM_CHECKOFFSET(m_iCurInputSlot, 0x15bc)
CM_CHECKOFFSET(m_pCurShell, 0x161c)
CM_CHECKOFFSET(m_hFileMgr, 0x1644)
CM_CHECKOFFSET(m_pVideoMgr, 0x1720)
CM_CHECKOFFSET(m_pDirectMusicMgr, 0x22c0)
CM_CHECKOFFSET(m_GlobalLightScale, 0x76c)
CM_CHECKOFFSET(m_SkyDef, 0x12b0)
CM_CHECKOFFSET(m_AxisOffsets, 0x160c)
CM_CHECKOFFSET(m_bInputState, 0x162c)
CM_CHECKOFFSET(m_ModelHookFn, 0x1634)
CM_CHECKOFFSET(m_MusicMgr, 0x1094)
CM_CHECKOFFSET(m_TextureUsers, 0x1204)
CM_CHECKOFFSET(m_VersionInfo, 0x12e0)
CM_CHECKOFFSET(m_nResTrees, 0x1698)
CM_CHECKOFFSET(m_DemoMgr, 0x169c)
CM_CHECKOFFSET(m_LoaderThread, 0x16b8)
CM_CHECKOFFSET(m_pDefaultModel, 0x171c)
CM_CHECKOFFSET(m_FormatMgr, 0x1728)
CM_CHECKOFFSET(m_pDirectMusicMgr, 0x22c0)

CM_CHECKOFFSET(m_Unknown22c4, 0x22c4)
typedef char CM_CheckSize[(sizeof(CClientMgr) == 0x22c8) ? 1 : -1];

#define LOADERTHREAD(pMgr)	(&(pMgr)->m_LoaderThread)

// GLOBAL: LITHTECH 0x004defac
extern CClientMgr *g_pClientMgr;

// The engine's command line (client.cpp builds it from the real one, launch.dll or -cmdfile).
struct CmdLineArgs
{
	char	**m_Argv;		// 0x00
	int		m_Argc;			// 0x04
	char	*m_pArgBuffer;	// 0x08 holds the strings m_Argv points to
};

// Flags or'd into an error code handed to cm_ProcessError.
#define ERROR_DISCONNECT	(1<<25)
#define ERROR_SHUTDOWN		(1<<26)

// The console commands and cm_Init keep their own client manager pointer.
// GLOBAL: LITHTECH 0x004e33d4
extern CClientMgr *g_pCommandClientMgr;

// 0x00412230: starts the renderer from the console variables.
LTRESULT cm_StartRenderFromGlobals(CClientMgr *pClientMgr);
// 0x00412070: cm_Init's hello after a shell started
void cm_OnEnterServer(CClientMgr *pClientMgr);
// 0x00412680
void cm_RebindTextures(CClientMgr *pClientMgr);
// 0x00425cd0 (ILTClient::AddSurfaceEffect)
LTRESULT cm_AddSurfaceEffect(CClientMgr *pClientMgr, SurfaceEffectDesc *pDesc);
// 0x00425d60
LTRESULT cm_ProcessError(CClientMgr *pClientMgr, LTRESULT theError);

// 0x004112c0: allocates and sets up the client manager (NULL on failure).
CClientMgr* cm_Init();

// ------------------------------------------------------------------ //
// cutil.cpp (cm_ functions take the manager).
// ------------------------------------------------------------------ //

// 0x00426750
void cm_UpdateModelDims(CClientMgr *pClientMgr, ModelInstance *pInstance);
// 0x00426860
void cm_MoveObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTBOOL bForce);
// 0x00426520 (Ghidra: CClientMgr::AddToObjectMap). Grows m_ObjectMap to hold id.
void cm_AddToObjectMap(CClientMgr *pClientMgr, uint16 id);
// 0x004265b0 (Ghidra: so_ExtraTerm). Clears an m_ObjectMap entry.
void cm_ClearObjectMapEntry(CClientMgr *pClientMgr, uint16 id);

// GLOBAL: LITHTECH 0x004deca0
extern uint32 g_Ticks_SoundUpdate;
// GLOBAL: LITHTECH 0x004ded54
extern uint32 g_Ticks_Render_Objects;
// GLOBAL: LITHTECH 0x004dec3c
extern uint32 g_Ticks_Render_Models;
// GLOBAL: LITHTECH 0x004ded00
extern uint32 g_Ticks_Render_Sprites;
// GLOBAL: LITHTECH 0x004debf8
extern uint32 g_Ticks_Render_WorldModels;
// GLOBAL: LITHTECH 0x004decf8
extern uint32 g_Ticks_Render_ParticleSystems;
// GLOBAL: LITHTECH 0x004decb0
extern uint32 g_Ticks_Render_PolyGrids;
// GLOBAL: LITHTECH 0x004ded10
extern uint32 g_Ticks_RenderScene;
// The client's leech on server models (clientmgr.cpp).
// GLOBAL: LITHTECH 0x004d03f8
extern LeechDef g_ClientModelLeechDef;
extern uint32 g_CurRunIteration;
extern uint32 g_Ticks_Music;
extern uint32 g_Ticks_Sound;
extern uint32 g_Ticks_Input;
extern uint32 g_Ticks_ClientShell;
extern uint32 g_Ticks_Render;

#endif  // __CLIENTMGR_H__
