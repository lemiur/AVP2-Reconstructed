// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
// Jupiter runtime/client/src/clientde_impl.cpp. Talon keeps the client's interface
// implementations together in this file: ILTClient (CLTClient + the ci_ function pointers),
// ILTCommon (client_iltcommon.cpp), ILTPhysics (client_iltphysics.cpp), ILTModel
// (client_iltmodel.cpp), ILTVideoMgr (client_iltvideomgr.cpp), ILTLightAnim and ILTCursor.
// The member interfaces reach the manager through a stored CClientMgr*; the ci_ functions
// use g_pClientMgr.
#include <string.h>
#include <stddef.h>
#include "console.h"		// (windows.h first: StdLith #defines DWORD)
#undef CopyFile
#include "bdefs.h"
#include "clientmgr.h"
#include "de_objects.h"
#include "moveobject.h"
#include "counter.h"
#include "packet.h"
#include "iltvideomgr.h"
#include "videomgr.h"
#include "shared_iltcommon.h"
#include "iltclient.h"
#include "iltphysics.h"
#include "iltmodel.h"
#include "iltlightanim.h"
#include "ilttransform.h"
#include "ilttexmod.h"
#include "iltcursor.h"
#include "iltdirectmusic.h"
#include "iltsoundmgr.h"
#include "setupobject.h"
#include "impl_common.h"
#include "client_filemgr.h"
#include "soundmgr.h"
#include "musicmgr.h"
#include "render.h"
#include "de_world.h"
#include "clientshell.h"
#include "stringmgr.h"
#include "concommand.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "dsys_interface.h"
#include "consolecommands.h"
#include "serverde_impl.h"
#include "servermgr.h"
#include "sprite.h"
#include "input.h"
#include "engine_vars.h"

// Used by the ci_ functions (other units).
class CameraInstance;
class LTParticleSystem;
class LTPolyGrid;

void		cis_Init(ILTClient *pClientDE);		// 0x0040cbd0 (winclientde.cpp)
void		cis_Term();							// 0x0040f0d0
SMusicMgr*	GetMusicMgr();						// 0x0040fbb0
LTRESULT	w_GetWorldInfoString(ILTStream *pStream, char *pInfoString, uint32 maxLen, uint32 *pActualLen);	// 0x00427db0

#define DRAWMODE_NORMAL		1
#define DRAWMODE_OBJECTLIST	2
LTBOOL		cm_Render(CClientMgr *pClientMgr, CameraInstance *pCamera, int drawMode,
				LTObject **pObjects, int nObjects);		// 0x00412260
LTRESULT	cm_AddObjectToClientWorld(CClientMgr *pClientMgr, uint16 objectID, InternalObjectSetup *pSetup,
				LTObject **ppObject, LTBOOL bMove, LTBOOL bRotate);	// 0x004126a0
LTRESULT	cm_AddSurfaceEffect(CClientMgr *pClientMgr, SurfaceEffectDesc *pDesc);	// 0x00425cd0
void		cm_ScaleObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewScale);		// 0x00426640
void		cm_RotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTRotation *pNewRot);	// 0x00426940
void		cm_MoveAndRotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTRotation *pNewRot);	// 0x00426a40

LTRESULT	ps_SetTexture(LTParticleSystem *pSystem, CClientMgr *pClientMgr, const char *pName);	// 0x00469710
void		ps_AddParticles(LTParticleSystem *pSystem, uint32 nParticles,
				LTVector *pMinOffset, LTVector *pMaxOffset, LTVector *pMinVelocity, LTVector *pMaxVelocity,
				LTVector *pMinColor, LTVector *pMaxColor, float minLifetime, float maxLifetime);	// 0x00469810
void		ps_OptimizeParticles(LTParticleSystem *pSystem);	// 0x0046a060
LTBOOL		pg_Init(LTPolyGrid *pGrid, uint32 width, uint32 height, LTBOOL bHalfTriangles);	// 0x0046dc60
LTRESULT	LoadSprite(CClientMgr *pClientMgr, FileRef *pFilename, Sprite **ppSprite);	// 0x00489710
LTBOOL		cp_Parse(char *pCommand, const char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs);	// 0x004202e0
void		quat_GetVectors(const float *pQuat, float *pRight, float *pUp, float *pForward);	// 0x0044cf30
void		quat_ConvertFromMatrix(float *pQuat, const float mat[4][4]);	// 0x0044ce20

HLTLINE		linesystem_GetNextLine(HLOCALOBJ hObj, HLTLINE hPrev);		// 0x004450c0
void		linesystem_GetLineInfo(HLTLINE hLine, LTLine *pLine);		// 0x00445110
void		linesystem_SetLineInfo(HLTLINE hLine, LTLine *pLine);		// 0x00445170
HLTLINE		linesystem_AddLine(HLOCALOBJ hObj, LTLine *pLine);			// 0x00445490
void		linesystem_RemoveLine(HLOCALOBJ hObj, HLTLINE hLine);		// 0x004457f0


// The global pan textures (sky shadow, fog) and the global light are RenderStruct members (g_Render.m_GlobalPans at
// 0x004e4998, m_GlobalLightDir 0x004e49c0, m_GlobalLightColor 0x004e49cc, m_AmbientLight 0x004e49d8): the renderer
// reads them there.


// ------------------------------------------------------------------------ //
// Externals.
// ------------------------------------------------------------------------ //



// world/fullintersectline (0x00437c3d).
LTBOOL i_IntersectSegment(ClientIntersectQuery *pQuery, ClientIntersectInfo *pInfo,
	WorldTree *pWorldTree, LTBOOL bUnknown);

// The first function in the object (0x004049c0, before g_TotalGlobalTimeCounter's initialisers).
// FUNCTION: LITHTECH 0x004049c0
LTBOOL ci_IntersectSegment(ClientIntersectQuery *pQuery, ClientIntersectInfo *pInfo)
{
	if(g_pClientMgr && g_pClientMgr->m_pCurShell)
		return i_IntersectSegment(pQuery, pInfo, &g_pClientMgr->m_World.m_WorldTree, LTFALSE);

	return LTFALSE;
}


// cutil.cpp.
LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 objectID);			// 0x004265e0
void cm_FreeUnusedModelTextures(CClientMgr *pClientMgr, LTObject *pObject);	// 0x004263f0

// The light table's color sample.
struct LTRGBColor
{
	uint8	b, g, r, a;
};


// clientmgr.cpp / cutil.cpp.
LTRESULT cm_RemoveObjectFromClientWorld(CClientMgr *pClientMgr, LTObject *pObject);	// 0x00412820
void cm_RelocateObject(CClientMgr *pClientMgr, LTObject *pObject);				// 0x00426730



#define CMSG_MESSAGE	11

// Used by GetPointContainers.
class TempObjArray
{
public:
	LTVector	m_Point;
	LTObject	**m_pObjects;
	uint32		m_nMaxObjects;
	uint32		*m_pNumObjects;
	uint32		*m_pTotalNumFound;
};



// Talon client-side sound manager/texmod implementations (other units).
class CLTTexMod : public ILTTexMod
{
public:
	CLTTexMod(CClientMgr *pClientMgr);		// 0x0049ada0

	virtual LTRESULT GetTextureHandle(char *pFilename, HTEXTURE &hTexture, const uint32 flags);
	virtual LTRESULT ReleaseTextureHandle(const HTEXTURE hTexture);
	virtual LTRESULT GetTextureInfo(const HTEXTURE hTexture, TextureInfo &info);
	virtual LTRESULT WasTextureDrawnLastFrame(const HTEXTURE hTexture);
	virtual LTRESULT LockTexture(const HTEXTURE hTexture, const LTRect *pRect,
		const uint32 lockType, uint8* &pData, long &lPitch);
	virtual LTRESULT UnlockTexture(const HTEXTURE hTexture);

	CClientMgr	*m_pClientMgr;
};


// ------------------------------------------------------------------------ //
// The interface implementations.
// ------------------------------------------------------------------------ //
// The member functions below are defined `inline` (the vtables reference them, so each is
// still emitted once): their FN_NAME statics are then COMDATs that sit next to their name
// strings in .data, as in lithtech.exe (a plain function's statics share one .data section at
// the front of the object). ClientCommonLT's and CLTClient's constructors (and CLTClient's
// destructor) are defined after the class's members, because VC6 emits a class's ??_G where
// its constructor is defined (0x00406020 and 0x00407d30 follow the last member).

void ci_Init(ILTClient *pClientDE);


// CPhysicsLT (vtable 0x004c67f0).
class CPhysicsLT : public ILTClientPhysics
{
public:
	CPhysicsLT(CClientMgr *pClientMgr) : m_fStairHeight(-1.0f)
	{
		m_pClientMgr = pClientMgr;
		m_ClientServerType = ClientType;
	}

	virtual LTRESULT SetVelocity(HOBJECT hObj, LTVector *pVel);
	virtual LTRESULT SetAcceleration(HOBJECT hObj, LTVector *pAccel);
	virtual LTRESULT MoveObject(HOBJECT hObj, LTVector *pPos, uint32 flags);
	virtual LTRESULT UpdateMovement(MoveInfo *pMoveInfo);
	virtual LTRESULT SetObjectDims(HOBJECT hObj, LTVector *pNewDims, uint32 flags);
	virtual LTRESULT GetGlobalForce(LTVector &vec);
	virtual LTRESULT SetGlobalForce(LTVector &vec);
	virtual LTRESULT MovePushObjects(HOBJECT hToMove, LTVector &newPos,
		HOBJECT *hPushObjects, uint32 nPushObjects);
	virtual LTRESULT RotatePushObjects(HOBJECT hToMove, LTRotation &newRot,
		HOBJECT *hPushObjects, uint32 nPushObjects);
	virtual LTRESULT GetStairHeight(float &fHeight);
	virtual LTRESULT SetStairHeight(float fHeight);

	float		m_fStairHeight;		// 0x08
	CClientMgr	*m_pClientMgr;		// 0x0c
};


// ClientCommonLT (vtable 0x004c6848).
class ClientCommonLT : public CommonLT
{
public:
	ClientCommonLT(CClientMgr *pClientMgr);

	virtual LTRESULT SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct *pStruct);
	virtual LTRESULT CreateMessage(ILTMessage* &pMsg);
	virtual LTRESULT GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags);
	virtual LTRESULT GetPolyInfo(HPOLY hPoly, LTPlane **ppPlane, LTVector *pVertexList,
		uint32 nVertexListMaxSize, uint32 *pnNumVertices);
	virtual LTRESULT GetPolySurfaceFlags(HPOLY hPoly, uint32 &dwSurfFlags);
	virtual LTRESULT GetPointStatus(LTVector *pPoint);
	virtual LTRESULT GetPointShade(LTVector *pPoint, LTVector *pColor);
	virtual LTRESULT GetAttachmentObjects(HATTACHMENT hAttachment, HOBJECT &hParent, HOBJECT &hChild);

	CClientMgr	*m_pClientMgr;		// 0x10
};


// The client's ILTCursor forwards to the manager's cursor (vtable 0x004c66e8).
class CLTCursorClient : public ILTCursor
{
public:
	CLTCursorClient(CClientMgr *pClientMgr)
	{
		m_pClientMgr = pClientMgr;
	}

	virtual LTRESULT SetCursorMode(CursorMode cMode);
	virtual LTRESULT GetCursorMode(CursorMode &cMode);
	virtual LTRESULT IsCursorModeAvailable(CursorMode cMode);
	virtual LTRESULT LoadCursorBitmapResource(const char *pName, HLTCURSOR &hCursor);
	virtual LTRESULT FreeCursor(const HLTCURSOR hCursor);
	virtual LTRESULT SetCursor(HLTCURSOR hCursor);
	virtual LTRESULT IsValidCursor(HLTCURSOR hCursor);
	virtual LTRESULT RefreshCursor();

	CClientMgr	*m_pClientMgr;		// 0x04
};


// LVideoMgr (vtable 0x004c67d4).
class LVideoMgr : public ILTVideoMgr
{
public:
	LVideoMgr(CClientMgr *pClientMgr)
	{
		m_pClientMgr = pClientMgr;
	}

	virtual LTRESULT StartOnScreenVideo(char *pFilename, uint32 flags, HVIDEO &hVideo);
	virtual LTRESULT StartTextureVideo(char *pFilename, uint32 flags, HVIDEO &hVideo);
	virtual LTRESULT UpdateVideo(HVIDEO hVideo);
	virtual LTRESULT GetVideoStatus(HVIDEO hVideo);
	virtual LTRESULT StopVideo(HVIDEO hVideo);
	virtual LTRESULT BindTextureVideoToPoly(HVIDEO hVideo, HPOLY hPoly);

	CClientMgr	*m_pClientMgr;		// 0x04
};


// ClientModelLT (vtable 0x004c6740).
class ClientModelLT : public ILTModel
{
public:
	ClientModelLT(CClientMgr *pClientMgr)
	{
		m_pClientMgr = pClientMgr;
	}

	virtual LTRESULT SetCurAnim(LTAnimTracker *pTracker, HMODELANIM hAnim);

	CClientMgr	*m_pClientMgr;		// 0x04
};


// ClientLightAnimLT (vtable 0x004c6708).
class ClientLightAnimLT : public ILTLightAnim
{
public:
	ClientLightAnimLT(CClientMgr *pClientMgr)
	{
		m_pClientMgr = pClientMgr;
	}

	virtual LTRESULT FindLightAnim(const char *pName, HLIGHTANIM &hLightAnim);
	virtual LTRESULT GetNumFrames(HLIGHTANIM hLightAnim, uint32 &nFrames);
	virtual LTRESULT GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info);
	virtual LTRESULT SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info);

	CClientMgr	*m_pClientMgr;		// 0x04
};


// CLTClient (vtable 0x004c6530, 0x3d8 bytes).
class CLTClient : public ILTClient
{
public:
	CLTClient(CClientMgr *pClientMgr);

	virtual ~CLTClient();

// ILTCSBase.
	virtual HMESSAGEWRITE	StartHMessageWrite();
	virtual HMODELANIM	GetAnimIndex(HOBJECT hObj, char *pAnimName);
	virtual void		SetModelAnimation(HOBJECT hObj, HMODELANIM hAnim);
	virtual HMODELANIM	GetModelAnimation(HOBJECT hObj);
	virtual void		SetModelLooping(HOBJECT hObj, LTBOOL bLoop);
	virtual LTBOOL		GetModelLooping(HOBJECT hObj);
	virtual LTRESULT	ResetModelAnimation(HOBJECT hObj);
	virtual uint32		GetModelPlaybackState(HOBJECT hObj);
	virtual LTRESULT	FreeUnusedModels();
	virtual void		CPrint(char *pMsg, ...);
	virtual uint32		GetPointContainers(LTVector *pPoint, HOBJECT *pList, uint32 maxListSize);
	virtual LTBOOL		GetContainerCode(HOBJECT hObj, uint16 *pCode);
	virtual LTRESULT	OpenFile(char *pFilename, ILTStream **pStream);
	virtual LTRESULT	CopyFile(const char *pszSourceFile, const char *pszDestFile);
	virtual HSTRING		FormatString(int messageCode, ...);
	virtual HSTRING		CopyString(HSTRING hString);
	virtual HSTRING		CreateString(char *pString);
	virtual void		FreeString(HSTRING hString);
	virtual LTBOOL		CompareStrings(HSTRING hString1, HSTRING hString2);
	virtual LTBOOL		CompareStringsUpper(HSTRING hString1, HSTRING hString2);
	virtual char*		GetStringData(HSTRING hString);
	virtual float		GetVarValueFloat(HCONSOLEVAR hVar);
	virtual char*		GetVarValueString(HCONSOLEVAR hVar);
	virtual LTFLOAT		GetTime();
	virtual LTFLOAT		GetFrameTime();
	virtual LTRESULT	RemoveObject(HOBJECT hObj);

// ILTClient.
	virtual LTRESULT	GetPointStatus(LTVector *pPoint);
	virtual LTRESULT	GetPointShade(LTVector *pPoint, LTVector *pColor);
	virtual LTRESULT	GetSConValueFloat(char *pName, float &val);
	virtual LTRESULT	GetSConValueString(char *pName, char *valBuf, uint32 bufLen);
	virtual float		GetServerConVarValueFloat(char *pName);
	virtual char*		GetServerConVarValueString(char *pName);
	virtual LTRESULT	GetGlobalLightDir(LTVector &dir);
	virtual LTRESULT	SetGlobalLightDir(LTVector dir);
	virtual LTRESULT	GetGlobalLightColor(LTVector &color);
	virtual LTRESULT	SetGlobalLightColor(LTVector color);
	virtual LTRESULT	GetAmbientLight(float &light);
	virtual LTRESULT	SetAmbientLight(float light);
	virtual LTRESULT	StartVideo(char *pFilename, uint32 flags);
	virtual LTRESULT	StopVideo();
	virtual LTRESULT	UpdateVideo();
	virtual LTRESULT	IsVideoPlaying();
	virtual HMESSAGEWRITE	StartMessage(uint8 messageID);
	virtual LTRESULT	EndMessage(HMESSAGEWRITE hMessage);
	virtual LTRESULT	EndMessage2(HMESSAGEWRITE hMessage, uint32 flags);
	virtual LTRESULT	SendToServer(ILTMessage &msg, uint8 msgID, uint32 flags);
	virtual LTRESULT	GetAttachments(HLOCALOBJ hObj, HLOCALOBJ *inList, uint32 inListSize,
		uint32 *outListSize, uint32 *outNumAttachments);
	virtual LTRESULT	ProcessAttachments(HOBJECT hObj);
	virtual LTRESULT	SetObjectPos(HLOCALOBJ hObj, LTVector *pPos, LTBOOL bForce);
	virtual uint32		GetObjectFlags(HOBJECT hObj);
	virtual void		SetObjectFlags(HOBJECT hObj, uint32 flags);
	virtual LTParticle*	AddParticle(HLOCALOBJ hObj, LTVector *pPos, LTVector *pVelocity, LTVector *pColor, float lifeTime);
	virtual LTRESULT	GetSpriteControl(HLOCALOBJ hObj, ILTSpriteControl* &pControl);
	virtual LTRESULT	GetCanvasFn(HOBJECT hCanvas, CanvasDrawFn &fn, void* &pUserData);
	virtual LTRESULT	SetCanvasFn(HOBJECT hCanvas, CanvasDrawFn fn, void* pUserData);
	virtual LTRESULT	GetCanvasRadius(HOBJECT hCanvas, float &radius);
	virtual LTRESULT	SetCanvasRadius(HOBJECT hCanvas, float radius);
	virtual LTRESULT	ModelNodeControl(HOBJECT hObj, NodeControlFn fn, void *pUserData);
	virtual LTBOOL		GetModelPlaying(HLOCALOBJ hObj);
	virtual void		SetModelPlaying(HLOCALOBJ hObj, LTBOOL bPlaying);
	virtual LTRESULT	StartQuery(char *pInfo);
	virtual LTRESULT	UpdateQuery();
	virtual LTRESULT	GetQueryResults(NetSession* &pListHead);
	virtual LTRESULT	EndQuery();

public:
	ClientCommonLT		m_CommonLT;			// 0x380
	CPhysicsLT			m_PhysicsLT;		// 0x394
	LVideoMgr			m_VideoMgr;			// 0x3a4
	ClientModelLT		m_ModelLT;			// 0x3ac
	ILTTransform		m_TransformLT;		// 0x3b4
	ClientLightAnimLT	m_LightAnimLT;		// 0x3b8
	CLTTexMod			m_TexMod;			// 0x3c0
	CLTCursorClient		m_CursorLT;			// 0x3c8
	CClientMgr			*m_pClientMgr;		// 0x3d0
	HVIDEO				m_hVideo;			// 0x3d4 the StartVideo video
};


// Global profile counter (Jupiter client_ticks.cpp's g_TotalGlobalTimeCounter); only its constructor
// is referenced in lithtech.exe.
// FUNCTION: LITHTECH 0x004049f0 _$E20
// FUNCTION: LITHTECH 0x00404a00 _$E19
// GLOBAL: LITHTECH 0x004decb4
CountPercent g_TotalGlobalTimeCounter;

// The frame profile counters around it (Jupiter client_ticks.cpp keeps them together; several names are
// invented, clientmgr.h / clientshell.h).
// GLOBAL: LITHTECH 0x004debf4
uint32 g_Ticks_FrameNet;
// GLOBAL: LITHTECH 0x004debf8
uint32 g_Ticks_Render_WorldModels;
// GLOBAL: LITHTECH 0x004dec3c
uint32 g_Ticks_Render_Models;
// GLOBAL: LITHTECH 0x004deca0
uint32 g_Ticks_SoundUpdate;
// GLOBAL: LITHTECH 0x004decb0
uint32 g_Ticks_Render_PolyGrids;
// GLOBAL: LITHTECH 0x004decf4
uint32 g_Ticks_FrameServer;
// GLOBAL: LITHTECH 0x004decf8
uint32 g_Ticks_Render_ParticleSystems;
// GLOBAL: LITHTECH 0x004ded00
uint32 g_Ticks_Render_Sprites;
// GLOBAL: LITHTECH 0x004ded10
uint32 g_Ticks_RenderScene;
// GLOBAL: LITHTECH 0x004ded50
uint32 g_Ticks_FrameClientShell;
// GLOBAL: LITHTECH 0x004ded54
uint32 g_Ticks_Render_Objects;


// ------------------------------------------------------------------------ //
// Light anims.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00404a20
LTRESULT cm_SetLightAnimInfo(CClientMgr *pClientMgr, HLIGHTANIM hLightAnim, LAInfo &info, LTBOOL bForce)
{
	FN_NAME(ClientLightAnimLT::SetLightAnimInfo);
	LightAnim *pAnim;
	LTBOOL bChanged;

	CHECK_PARAMS2(hLightAnim < pClientMgr->m_World.m_LightAnims.GetSize());

	pAnim = &pClientMgr->m_World.m_LightAnims[hLightAnim];
	if(bForce)
		bChanged = LTTRUE;
	else
		bChanged = la_InfoChanged(pAnim, &info, (uint32*)&hLightAnim);

	la_SetInfo(pAnim, &info);
	if(bChanged)
		pClientMgr->m_World.UpdateLightAnimPolies(pAnim);

	return LT_OK;
}


// ------------------------------------------------------------------------ //
// Interface creation.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00404ac0
ILTClient* ci_CreateClientInterface(CClientMgr *pClientMgr)
{
	return new CLTClient(pClientMgr);
}

// FUNCTION: LITHTECH 0x00404c20 ??_GILTClient@@MAEPAXI@Z


// ------------------------------------------------------------------------ //
// CPhysicsLT.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00404c40
inline LTRESULT CPhysicsLT::SetVelocity(HOBJECT hObj, LTVector *pVel)
{
	CHECK_PARAMS(hObj, CPhysicsLT::SetVelocity);

	hObj->m_Velocity = *pVel;
	hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00404cc0
inline LTRESULT CPhysicsLT::SetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	CHECK_PARAMS(hObj, CPhysicsLT::SetAcceleration);

	hObj->m_Acceleration = *pAccel;
	hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00404d40
inline LTRESULT CPhysicsLT::MoveObject(HOBJECT hObj, LTVector *pPos, uint32 flags)
{
	MoveState moveState;
	uint32 moFlags;
	WorldTree *pWorldTree;

	++g_nMoveObjectCalls;
	CountAdder cntAdd(&g_Ticks_MoveObject);

	CHECK_PARAMS(hObj, CPhysicsLT::MoveObject);

	pWorldTree = &m_pClientMgr->m_World.m_WorldTree;
	if(!pWorldTree)
	{
		RETURN_ERROR(3, CPhysicsLT::MoveObject, LT_NOTINITIALIZED);
	}

	moveState.Setup(pWorldTree, m_pClientMgr->m_MoveAbstract, hObj, hObj->m_BPriority);

	moFlags = MO_DETACHSTANDING;
	if(flags & MOVEOBJECT_TELEPORT)
		moFlags |= MO_TELEPORT;

	if(flags & MOVEOBJECT_NCTELEPORT)
		moFlags |= MO_NOSLIDING;

	::MoveObject(&moveState, *pPos, moFlags);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00404ee0
inline LTRESULT CPhysicsLT::UpdateMovement(MoveInfo *pMoveInfo)
{
	MotionState *pState;

	CHECK_PARAMS(pMoveInfo && pMoveInfo->m_hObject, CPhysicsLT::UpdateMovement);

	pState = &m_pClientMgr->m_MotionState;
	pState->m_pObj = pMoveInfo->m_hObject;
	pState->m_dt = pMoveInfo->m_dt;
	pState->m_pVelocity = &pState->m_pObj->m_Velocity;
	pState->m_pAcceleration = &pState->m_pObj->m_Acceleration;
	pState->m_Flags = pState->m_pObj->m_Flags;

	if(!(pState->m_pObj->m_InternalFlags & IFLAG_APPLYPHYSICS))
	{
		pState->m_Offset.Init();
		return LT_OK;
	}

	CalcMotion(pState);
	pMoveInfo->m_Offset = pState->m_Offset;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00404fb0
inline LTRESULT CPhysicsLT::SetObjectDims(HOBJECT hObj, LTVector *pNewDims, uint32 flags)
{
	LTVector newDims;
	MoveState moveState;
	WorldTree *pWorldTree;

	CHECK_PARAMS(hObj && pNewDims, SPhysicsLT::SetObjectDims);

	pWorldTree = &m_pClientMgr->m_World.m_WorldTree;
	if(!pWorldTree)
	{
		RETURN_ERROR(1, CPhysicsLT::SetObjectDims, LT_INVALIDPARAMS);
	}

	newDims = *pNewDims;

	// Not allowed to change a WorldModel's dimensions!
	if(hObj->m_ObjectType != OT_CONTAINER && hObj->m_ObjectType != OT_WORLDMODEL)
	{
		moveState.Setup(pWorldTree, m_pClientMgr->m_MoveAbstract, hObj, hObj->m_BPriority);
		if(ChangeObjectDimensions(&moveState, &newDims, flags & SETDIMS_PUSHOBJECTS, LTTRUE))
		{
			return LT_OK;
		}
		else
		{
			*pNewDims = newDims;
			return LT_ERROR;
		}
	}
	else
	{
		return LT_INVALIDPARAMS;
	}
}

// FUNCTION: LITHTECH 0x00405110
inline LTRESULT CPhysicsLT::GetGlobalForce(LTVector &vec)
{
	vec = m_pClientMgr->m_MotionState.m_Info.m_Force;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00405140
inline LTRESULT CPhysicsLT::SetGlobalForce(LTVector &vec)
{
	MotionState *pState;

	pState = &m_pClientMgr->m_MotionState;
	pState->m_Info.m_Force = vec;
	pState->m_Info.m_ForceMag = pState->m_Info.m_Force.Mag();
	if(pState->m_Info.m_ForceMag > 0.00001f)
	{
		pState->m_Info.m_UnitForce = pState->m_Info.m_Force;
		pState->m_Info.m_UnitForce /= pState->m_Info.m_ForceMag;
	}
	else
	{
		pState->m_Info.m_UnitForce.Init();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004051e0
inline LTRESULT CPhysicsLT::MovePushObjects(HOBJECT hToMove, LTVector &newPos,
	HOBJECT *hPushObjects, uint32 nPushObjects)
{
	FN_NAME(CPhysicsLT::MovePushObjects);
	MoveState moveState;
	WorldTree *pWorldTree;

	CHECK_PARAMS2(hToMove && hPushObjects);

	pWorldTree = &m_pClientMgr->m_World.m_WorldTree;
	if(!pWorldTree)
	{
		ERR(3, LT_NOTINITIALIZED);
	}

	moveState.Setup(pWorldTree, m_pClientMgr->m_MoveAbstract, hToMove, hToMove->m_BPriority);
	moveState.m_CustomTestObjects = hPushObjects;
	moveState.m_nCustomTestObjects = nPushObjects;
	::MoveObject(&moveState, newPos, MO_DETACHSTANDING|MO_MOVESTANDINGONS|MO_GOTHRUWORLD);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00405310
inline LTRESULT CPhysicsLT::RotatePushObjects(HOBJECT hToMove, LTRotation &newRot,
	HOBJECT *hPushObjects, uint32 nPushObjects)
{
	FN_NAME(CPhysicsLT::RotatePushObjects);
	MoveState moveState;
	WorldTree *pWorldTree;

	CHECK_PARAMS2(hToMove && hPushObjects);

	pWorldTree = &m_pClientMgr->m_World.m_WorldTree;
	if(!pWorldTree)
	{
		ERR(3, LT_NOTINITIALIZED);
	}

	if(hToMove->m_ObjectType != OT_WORLDMODEL && hToMove->m_ObjectType != OT_CONTAINER)
	{
		RETURN_ERROR(3, CPhysicsLT::RotatePushObjects, LT_INVALIDPARAMS);
	}

	moveState.Setup(pWorldTree, m_pClientMgr->m_MoveAbstract, hToMove, hToMove->m_BPriority);
	moveState.m_CustomTestObjects = hPushObjects;
	moveState.m_nCustomTestObjects = nPushObjects;
	RotateWorldModel(&moveState, &newRot, LTTRUE);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00405460
inline LTRESULT CPhysicsLT::GetStairHeight(float &fHeight)
{
	fHeight = m_fStairHeight;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00405470
inline LTRESULT CPhysicsLT::SetStairHeight(float fHeight)
{
	m_fStairHeight = fHeight;
	return LT_OK;
}


// ------------------------------------------------------------------------ //
// ClientCommonLT (Jupiter client_iltcommon.cpp).
// ------------------------------------------------------------------------ //

// Talon world lookups through the client manager's world models.
inline WorldPoly* cm_GetPolyFromHPoly(CClientMgr *pClientMgr, HPOLY hPoly)
{
	uint32 iModel;
	WorldData *pWorldData;

	iModel = hPoly >> 16;
	if(iModel >= pClientMgr->m_World.m_WorldModels.GetSize())
		return LTNULL;

	pWorldData = pClientMgr->m_World.m_WorldModels[iModel];
	if(!pWorldData)
		return LTNULL;

	return pWorldData->m_pOriginalBsp->GetPolyFromHPoly(hPoly);
}

// FUNCTION: LITHTECH 0x00405480
inline LTRESULT ClientCommonLT::SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct *pStruct)
{
	FN_NAME(ClientCommonLT::SetObjectFilenames);
	InternalObjectSetup objectSetup;
	uint32 i;

	CHECK_PARAMS2(pStruct && pObj &&
		(pObj->m_ObjectType == OT_MODEL || pObj->m_ObjectType == OT_SPRITE));

	// Unload un-used model textures..
	if(pObj->m_ObjectType == OT_MODEL)
	{
		cm_FreeUnusedModelTextures(m_pClientMgr, pObj);
	}

	// Setup the InternalObjectSetup.
	objectSetup.m_pSetup = pStruct;
	objectSetup.m_Filename.m_pFilename = pStruct->m_Filename;
	objectSetup.m_Filename.m_FileType = FILE_ANYFILE;
	for(i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		objectSetup.m_SkinNames[i].m_pFilename = pStruct->m_SkinNames[i];
		objectSetup.m_SkinNames[i].m_FileType = FILE_ANYFILE;
	}

	objectSetup.m_bResetAnimations = pStruct->m_bResetAnimations;

	// Init
	return so_ExtraInit(m_pClientMgr, pObj, &objectSetup, LTFALSE);
}

// FUNCTION: LITHTECH 0x00405590
inline LTRESULT ClientCommonLT::CreateMessage(ILTMessage* &pMsg)
{
	pMsg = &m_pClientMgr->AllocPacket()->m_Message;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004055b0
inline LTRESULT ClientCommonLT::GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags)
{
	WorldPoly *pPoly;

	if(!m_pClientMgr->m_pCurShell)
	{
		RETURN_ERROR(2, CommonLT::GetPolyTextureFlags, LT_NOTINITIALIZED);
	}

	*pFlags = 0;

	pPoly = cm_GetPolyFromHPoly(m_pClientMgr, hPoly);
	if(pPoly)
	{
		*pFlags = ((Surface*)pPoly->m_pSurface)->m_TextureFlags;
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(2, CommonLT::GetPolyTextureFlags, LT_ERROR);
	}
}

// The output pointer is advanced per vertex (`*pVertexList = ...; pVertexList++`): indexing it gave the reverse
// register assignment for the vertex count and the output pointer.
// FUNCTION: LITHTECH 0x00405680
inline LTRESULT ClientCommonLT::GetPolyInfo(HPOLY hPoly, LTPlane **ppPlane, LTVector *pVertexList,
	uint32 nVertexListMaxSize, uint32 *pnNumVertices)
{
	WorldPoly *pPoly;
	uint32 nVertices, i;

	if(!m_pClientMgr->m_pCurShell)
	{
		RETURN_ERROR(2, CommonLT::GetPolyInfo, LT_NOTINITIALIZED);
	}

	pPoly = cm_GetPolyFromHPoly(m_pClientMgr, hPoly);
	if(pPoly)
	{
		if(ppPlane)
			*ppPlane = pPoly->m_pPlane;

		nVertices = pPoly->GetNumVertices();
		if(pVertexList)
		{
			for(i=0; i < nVertices && i < nVertexListMaxSize; i++)
			{
				*pVertexList = *((SPolyVertex*)(pPoly + 1))[i].m_Vec;
				pVertexList++;
			}
		}

		if(pnNumVertices)
			*pnNumVertices = nVertices;

		return LT_OK;
	}
	else
	{
		RETURN_ERROR(2, CommonLT::GetPolyInfo, LT_ERROR);
	}
}

// FUNCTION: LITHTECH 0x004057b0
inline LTRESULT ClientCommonLT::GetPolySurfaceFlags(HPOLY hPoly, uint32 &dwSurfFlags)
{
	WorldPoly *pPoly;

	if(!m_pClientMgr->m_pCurShell)
	{
		RETURN_ERROR(2, CommonLT::GetPolySurfaceFlags, LT_NOTINITIALIZED);
	}

	pPoly = cm_GetPolyFromHPoly(m_pClientMgr, hPoly);
	if(pPoly && pPoly->m_pSurface)
	{
		dwSurfFlags = ((Surface*)pPoly->m_pSurface)->m_Flags;
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(2, CommonLT::GetPolySurfaceFlags, LT_ERROR);
	}
}

// FUNCTION: LITHTECH 0x00405880
inline LTRESULT ClientCommonLT::GetPointStatus(LTVector *pPoint)
{
	WorldTree *pWorldTree = &m_pClientMgr->m_World.m_WorldTree;

	if(!pWorldTree)
		return LT_NOTINWORLD;

	return ic_IsPointInWorld(pWorldTree, pPoint) ? LT_INSIDE : LT_OUTSIDE;
}

void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGBColor *pRGB);	// 0x00405980 (also called by server_interface.cpp)

// FUNCTION: LITHTECH 0x004058b0
inline LTRESULT ClientCommonLT::GetPointShade(LTVector *pPoint, LTVector *pColor)
{
	LTRGBColor rgb;

	if(!pPoint || !pColor)
		RETURN_ERROR(1, CLTClient::GetPointShade, LT_INVALIDPARAMS);

	if(!m_pClientMgr->m_pCurShell)
		return LT_NOTINWORLD;

	w_GetLightVal(&m_pClientMgr->m_World.m_LightTable, pPoint, &rgb);

	pColor->x = rgb.r;
	pColor->y = rgb.g;
	pColor->z = rgb.b;
	return LT_OK;
}

// The older Jupiter light grid lookup (no light groups, no filtering): Ghidra calls it CLightTable::GetLightVal, but it
// is a free cdecl function (config/renames.csv).
// FUNCTION: LITHTECH 0x00405980
void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGBColor *pRGB)
{
	LTVector fSamplePt;
	LTVector samples[8];
	LTVector ySamples[2];
	LTVector xySamples[2];
	LTVector vInv;
	LTVector finalColor;
	struct { int x, y, z; } gridCoords;
	LTRGB *pBase;

	fSamplePt = *pPos - pTable->m_LookupStart;
	fSamplePt.x *= pTable->m_InvBlockSize.x;
	fSamplePt.y *= pTable->m_InvBlockSize.y;
	fSamplePt.z *= pTable->m_InvBlockSize.z;

	gridCoords.x = (int)fSamplePt.x;
	gridCoords.y = (int)fSamplePt.y;
	gridCoords.z = (int)fSamplePt.z;
	gridCoords.x = LTCLAMP(gridCoords.x, 0, (int)pTable->m_DimsMinus1[0]);
	gridCoords.y = LTCLAMP(gridCoords.y, 0, (int)pTable->m_DimsMinus1[1]);
	gridCoords.z = LTCLAMP(gridCoords.z, 0, (int)pTable->m_DimsMinus1[2]);

	if(gridCoords.x == (int)pTable->m_DimsMinus1[0])
	{
		fSamplePt.x = 0.0f;
		vInv.x = 1.0f;
		gridCoords.x--;
	}
	else
	{
		fSamplePt.x = fSamplePt.x - (float)floor(fSamplePt.x);
		vInv.x = 1.0f - fSamplePt.x;
	}

	if(gridCoords.y == (int)pTable->m_DimsMinus1[1])
	{
		fSamplePt.y = 0.0f;
		vInv.y = 1.0f;
		gridCoords.y--;
	}
	else
	{
		fSamplePt.y = fSamplePt.y - (float)floor(fSamplePt.y);
		vInv.y = 1.0f - fSamplePt.y;
	}

	if(gridCoords.z == (int)pTable->m_DimsMinus1[2])
	{
		fSamplePt.z = 0.0f;
		vInv.z = 1.0f;
		gridCoords.z--;
	}
	else
	{
		fSamplePt.z = fSamplePt.z - (float)floor(fSamplePt.z);
		vInv.z = 1.0f - fSamplePt.z;
	}

	pBase = &pTable->m_pData[gridCoords.x + gridCoords.y*pTable->m_Dims[0] + gridCoords.z*pTable->m_XSizeTimesYSize];
	samples[0].x = pBase[pTable->m_Dims[0]].r;
	samples[0].y = pBase[pTable->m_Dims[0]].g;
	samples[0].z = pBase[pTable->m_Dims[0]].b;
	samples[1].x = pBase[pTable->m_Dims[0] + 1].r;
	samples[1].y = pBase[pTable->m_Dims[0] + 1].g;
	samples[1].z = pBase[pTable->m_Dims[0] + 1].b;
	samples[2].x = pBase[0].r;
	samples[2].y = pBase[0].g;
	samples[2].z = pBase[0].b;
	samples[3].x = pBase[1].r;
	samples[3].y = pBase[1].g;
	samples[3].z = pBase[1].b;

	pBase += pTable->m_XSizeTimesYSize;
	samples[4].x = pBase[pTable->m_Dims[0]].r;
	samples[4].y = pBase[pTable->m_Dims[0]].g;
	samples[4].z = pBase[pTable->m_Dims[0]].b;
	samples[5].x = pBase[pTable->m_Dims[0] + 1].r;
	samples[5].y = pBase[pTable->m_Dims[0] + 1].g;
	samples[5].z = pBase[pTable->m_Dims[0] + 1].b;
	samples[6].x = pBase[0].r;
	samples[6].y = pBase[0].g;
	samples[6].z = pBase[0].b;
	samples[7].x = pBase[1].r;
	samples[7].y = pBase[1].g;
	samples[7].z = pBase[1].b;

	ySamples[0].x = samples[2].x * vInv.y;
	ySamples[0].y = samples[2].y * vInv.y;
	ySamples[0].z = samples[2].z * vInv.y;
	ySamples[0].x += samples[0].x * fSamplePt.y;
	ySamples[0].y += samples[0].y * fSamplePt.y;
	ySamples[0].z += samples[0].z * fSamplePt.y;
	ySamples[1].x = samples[3].x * vInv.y;
	ySamples[1].y = samples[3].y * vInv.y;
	ySamples[1].z = samples[3].z * vInv.y;
	ySamples[1].x += samples[1].x * fSamplePt.y;
	ySamples[1].y += samples[1].y * fSamplePt.y;
	ySamples[1].z += samples[1].z * fSamplePt.y;
	xySamples[0].x = ySamples[0].x * vInv.x;
	xySamples[0].y = ySamples[0].y * vInv.x;
	xySamples[0].z = ySamples[0].z * vInv.x;
	xySamples[0].x += ySamples[1].x * fSamplePt.x;
	xySamples[0].y += ySamples[1].y * fSamplePt.x;
	xySamples[0].z += ySamples[1].z * fSamplePt.x;
	ySamples[0].x = samples[6].x * vInv.y;
	ySamples[0].y = samples[6].y * vInv.y;
	ySamples[0].z = samples[6].z * vInv.y;
	ySamples[0].x += samples[4].x * fSamplePt.y;
	ySamples[0].y += samples[4].y * fSamplePt.y;
	ySamples[0].z += samples[4].z * fSamplePt.y;
	ySamples[1].x = samples[7].x * vInv.y;
	ySamples[1].y = samples[7].y * vInv.y;
	ySamples[1].z = samples[7].z * vInv.y;
	ySamples[1].x += samples[5].x * fSamplePt.y;
	ySamples[1].y += samples[5].y * fSamplePt.y;
	ySamples[1].z += samples[5].z * fSamplePt.y;
	xySamples[1].x = ySamples[0].x * vInv.x;
	xySamples[1].y = ySamples[0].y * vInv.x;
	xySamples[1].z = ySamples[0].z * vInv.x;
	xySamples[1].x += ySamples[1].x * fSamplePt.x;
	xySamples[1].y += ySamples[1].y * fSamplePt.x;
	xySamples[1].z += ySamples[1].z * fSamplePt.x;
	finalColor.x = xySamples[0].x * vInv.z;
	finalColor.y = xySamples[0].y * vInv.z;
	finalColor.z = xySamples[0].z * vInv.z;
	finalColor.x += xySamples[1].x * fSamplePt.z;
	finalColor.y += xySamples[1].y * fSamplePt.z;
	finalColor.z += xySamples[1].z * fSamplePt.z;
	pRGB->r = (uint8)(int)finalColor.x;
	pRGB->g = (uint8)(int)finalColor.y;
	pRGB->b = (uint8)(int)finalColor.z;
}

// FUNCTION: LITHTECH 0x00405f50
inline LTRESULT ClientCommonLT::GetAttachmentObjects(HATTACHMENT hAttachment, HOBJECT &hParent, HOBJECT &hChild)
{
	FN_NAME(ClientCommonLT::GetAttachmentObjects);
	Attachment *pAttachment;

	pAttachment = (Attachment*)hAttachment;
	if(!pAttachment)
	{
		ERR(1, LT_INVALIDPARAMS);
	}

	hParent = cm_FindObject(m_pClientMgr, pAttachment->m_nParentID);
	hChild = cm_FindObject(m_pClientMgr, pAttachment->m_nChildID);

	if(!hParent || !hChild)
	{
		ERR(1, LT_NOTINITIALIZED);
	}

	return LT_OK;
}

inline ClientCommonLT::ClientCommonLT(CClientMgr *pClientMgr)
{
	m_pClientMgr = pClientMgr;
}

// FUNCTION: LITHTECH 0x00406020 ??_GClientCommonLT@@UAEPAXI@Z


// ------------------------------------------------------------------------ //
// CLTCursorClient: forwards to the manager's cursor.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00406040
inline LTRESULT CLTCursorClient::SetCursorMode(CursorMode cMode)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->SetCursorMode(cMode);
}

// FUNCTION: LITHTECH 0x00406060
inline LTRESULT CLTCursorClient::GetCursorMode(CursorMode &cMode)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->GetCursorMode(cMode);
}

// FUNCTION: LITHTECH 0x00406080
inline LTRESULT CLTCursorClient::IsCursorModeAvailable(CursorMode cMode)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->IsCursorModeAvailable(cMode);
}

// FUNCTION: LITHTECH 0x004060a0
inline LTRESULT CLTCursorClient::LoadCursorBitmapResource(const char *pName, HLTCURSOR &hCursor)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->LoadCursorBitmapResource(pName, hCursor);
}

// FUNCTION: LITHTECH 0x004060c0
inline LTRESULT CLTCursorClient::FreeCursor(const HLTCURSOR hCursor)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->FreeCursor(hCursor);
}

// FUNCTION: LITHTECH 0x004060e0
inline LTRESULT CLTCursorClient::SetCursor(HLTCURSOR hCursor)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->SetCursor(hCursor);
}

// FUNCTION: LITHTECH 0x00406100
inline LTRESULT CLTCursorClient::IsValidCursor(HLTCURSOR hCursor)
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->IsValidCursor(hCursor);
}

// FUNCTION: LITHTECH 0x00406120
inline LTRESULT CLTCursorClient::RefreshCursor()
{
	if(!m_pClientMgr)
		return LT_NOTINITIALIZED;

	return m_pClientMgr->m_pCursorMgr->RefreshCursor();
}


// ------------------------------------------------------------------------ //
// LVideoMgr (Jupiter client_iltvideomgr.cpp).
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00406140
inline LTRESULT LVideoMgr::StartOnScreenVideo(char *pFilename, uint32 flags, HVIDEO &hVideo)
{
	VideoMgr *pMgr = m_pClientMgr->m_pVideoMgr;

	if(!pMgr)
		RETURN_ERROR(2, LVideoMgr::StartOnScreenVideo, LT_NOTINITIALIZED);

	if(!pFilename)
		RETURN_ERROR(2, LVideoMgr::StartOnScreenVideo, LT_INVALIDPARAMS);

	return pMgr->CreateScreenVideo(pFilename, flags, (VideoInst*&)hVideo);
}

// FUNCTION: LITHTECH 0x004061e0
inline LTRESULT LVideoMgr::StartTextureVideo(char *pFilename, uint32 flags, HVIDEO &hVideo)
{
	VideoMgr *pMgr = m_pClientMgr->m_pVideoMgr;

	if(!pMgr)
		RETURN_ERROR(2, LVideoMgr::StartTextureVideo, LT_NOTINITIALIZED);

	if(!pFilename)
		RETURN_ERROR(2, LVideoMgr::StartTextureVideo, LT_INVALIDPARAMS);

	return pMgr->CreateTextureVideo(pFilename, flags, (VideoInst*&)hVideo);
}

// FUNCTION: LITHTECH 0x00406280
inline LTRESULT LVideoMgr::UpdateVideo(HVIDEO hVideo)
{
	VideoMgr *pMgr = m_pClientMgr->m_pVideoMgr;
	VideoInst *pVideo = (VideoInst*)hVideo;

	if(!pMgr)
		RETURN_ERROR(2, LVideoMgr::UpdateVideo, LT_NOTINITIALIZED);

	if(!pVideo || pMgr->m_Videos.FindElement(pVideo) == BAD_INDEX)
		RETURN_ERROR(2, LVideoMgr::UpdateVideo, LT_INVALIDPARAMS);

	return pVideo->DrawVideo();
}

// FUNCTION: LITHTECH 0x00406340
inline LTRESULT LVideoMgr::GetVideoStatus(HVIDEO hVideo)
{
	VideoMgr *pMgr = m_pClientMgr->m_pVideoMgr;
	VideoInst *pVideo = (VideoInst*)hVideo;

	if(!pMgr)
		RETURN_ERROR(2, LVideoMgr::UpdateVideo, LT_NOTINITIALIZED);

	if(!pVideo || pMgr->m_Videos.FindElement(pVideo) == BAD_INDEX)
		RETURN_ERROR(2, LVideoMgr::UpdateVideo, LT_INVALIDPARAMS);

	return pVideo->GetVideoStatus();
}

// FUNCTION: LITHTECH 0x00406400
inline LTRESULT LVideoMgr::StopVideo(HVIDEO hVideo)
{
	VideoMgr *pMgr = m_pClientMgr->m_pVideoMgr;
	VideoInst *pVideo = (VideoInst*)hVideo;

	if(!pMgr)
		RETURN_ERROR(2, LVideoMgr::UpdateVideo, LT_NOTINITIALIZED);

	if(!pVideo || pMgr->m_Videos.FindElement(pVideo) == BAD_INDEX)
		RETURN_ERROR(2, LVideoMgr::UpdateVideo, LT_INVALIDPARAMS);

	pVideo->Release();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004064c0
inline LTRESULT LVideoMgr::BindTextureVideoToPoly(HVIDEO hVideo, HPOLY hPoly)
{
	CClientMgr *pClientMgr = m_pClientMgr;
	VideoMgr *pMgr = pClientMgr->m_pVideoMgr;
	VideoInst *pVideo = (VideoInst*)hVideo;
	WorldPoly *pPoly;

	if(!pMgr)
		RETURN_ERROR(2, LVideoMgr::BindTextureVideoToPoly, LT_NOTINITIALIZED);

	if(!pVideo || pMgr->m_Videos.FindElement(pVideo) == BAD_INDEX || !hPoly)
		RETURN_ERROR(2, LVideoMgr::BindTextureVideoToPoly, LT_INVALIDPARAMS);

	if(pClientMgr->m_pCurShell)
	{
		pPoly = cm_GetPolyFromHPoly(pClientMgr, hPoly);
		if(pPoly && pPoly->m_pSurface)
		{
			return pVideo->BindToSurface((Surface*)pPoly->m_pSurface);
		}
	}

	RETURN_ERROR(1, LVideoMgr::BindTextureVideoToPoly, LT_NOTINITIALIZED);
}


// ------------------------------------------------------------------------ //
// ClientModelLT (Jupiter client_iltmodel.cpp).
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004065e0
inline LTRESULT ClientModelLT::SetCurAnim(LTAnimTracker *pTracker, HMODELANIM hAnim)
{
	FN_NAME(ClientModelLT::SetCurAnim);
	ModelInstance *pInst;
	LTRESULT dResult;

	pInst = pTracker->GetModelInstance();
	if(pInst)
	{
		dResult = ILTModel::SetCurAnim(pTracker, hAnim);
		if(dResult == LT_OK)
		{
			cm_UpdateModelDims(m_pClientMgr, pInst);
		}

		return dResult;
	}
	else
	{
		RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
	}
}


// ------------------------------------------------------------------------ //
// ClientLightAnimLT.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00406660
inline LTRESULT ClientLightAnimLT::FindLightAnim(const char *pName, HLIGHTANIM &hLightAnim)
{
	if(m_pClientMgr->m_World.FindLightAnim(pName, &hLightAnim))
	{
		return LT_OK;
	}
	else
	{
		hLightAnim = INVALID_LIGHT_ANIM;
		return LT_NOTFOUND;
	}
}

// FUNCTION: LITHTECH 0x00406690
inline LTRESULT ClientLightAnimLT::GetNumFrames(HLIGHTANIM hLightAnim, uint32 &nFrames)
{
	FN_NAME(ClientLightAnimLT::GetNumFrames);

	nFrames = 0;
	CHECK_PARAMS2(hLightAnim < m_pClientMgr->m_World.m_LightAnims.GetSize());

	nFrames = m_pClientMgr->m_World.m_LightAnims[hLightAnim].m_nFrames;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00406700
inline LTRESULT ClientLightAnimLT::GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info)
{
	FN_NAME(ClientLightAnimLT::GetLightAnimInfo);

	CHECK_PARAMS2(hLightAnim < m_pClientMgr->m_World.m_LightAnims.GetSize());

	la_GetInfo(&m_pClientMgr->m_World.m_LightAnims[hLightAnim], &info);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00406770
inline LTRESULT ClientLightAnimLT::SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info)
{
	return cm_SetLightAnimInfo(m_pClientMgr, hLightAnim, info, LTFALSE);
}


// ------------------------------------------------------------------------ //
// CLTClient.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00406790
inline LTRESULT CLTClient::OpenFile(char *pFilename, ILTStream **pStream)
{
	FileRef ref;

	if(!pFilename || !pStream)
	{
		RETURN_ERROR(1, CLTClient::OpenFile, LT_INVALIDPARAMS);
	}

	ref.m_pFilename = pFilename;
	ref.m_FileType = FILE_ANYFILE;
	*pStream = cf_OpenFile(m_pClientMgr->m_hFileMgr, &ref);
	if(*pStream)
	{
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(3, CLTClient::OpenFile, LT_NOTFOUND);
	}
}

// FUNCTION: LITHTECH 0x00406870
inline LTRESULT CLTClient::CopyFile(const char *pszSourceFile, const char *pszDestFile)
{
	return cf_CopyFile(m_pClientMgr->m_hFileMgr, pszSourceFile, pszDestFile);
}

// FUNCTION: LITHTECH 0x004068a0
inline LTRESULT CLTClient::SetObjectPos(HLOCALOBJ hObj, LTVector *pPos, LTBOOL bForce)
{
	FN_NAME(CLTClient::SetObjectPos);

	CHECK_PARAMS2(hObj && pPos);

	cm_MoveObject(m_pClientMgr, hObj, pPos, bForce);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00406910
inline void CLTClient::SetObjectFlags(HOBJECT hObj, uint32 flags)
{
	m_pCommonLT->SetObjectFlags(hObj, OFT_Flags, flags);
}


// Add a particle (Jupiter particlesystem.h).
inline PSParticle* ps_AddParticle(LTParticleSystem *pSystem, LTVector *pPos, LTVector *pColor, LTVector *pVel, float lifeTime)
{
	PSParticle *pParticle;

	pParticle = (PSParticle*)sb_Allocate(pSystem->m_pParticleBank);
	if(!pParticle)
		return LTNULL;

	// Update the extents.
	if(pPos->x < pSystem->m_MinPos.x)	pSystem->m_MinPos.x = pPos->x;
	if(pPos->y < pSystem->m_MinPos.y)	pSystem->m_MinPos.y = pPos->y;
	if(pPos->z < pSystem->m_MinPos.z)	pSystem->m_MinPos.z = pPos->z;
	if(pPos->x > pSystem->m_MaxPos.x)	pSystem->m_MaxPos.x = pPos->x;
	if(pPos->y > pSystem->m_MaxPos.y)	pSystem->m_MaxPos.y = pPos->y;
	if(pPos->z > pSystem->m_MaxPos.z)	pSystem->m_MaxPos.z = pPos->z;

	pParticle->m_Pos = *pPos;
	pParticle->m_Color = *pColor;
	pParticle->m_Velocity = *pVel;
	pParticle->m_Alpha = 1.0f;
	pParticle->m_Lifetime = lifeTime;
	pParticle->m_TotalLifetime = lifeTime;
	pParticle->m_Size = pSystem->m_ParticleRadius;

	// Add it to the list.
	pParticle->m_pPrev = pSystem->m_ParticleHead.m_pPrev;
	pParticle->m_pNext = &pSystem->m_ParticleHead;
	pParticle->m_pNext->m_pPrev = pParticle;
	pParticle->m_pPrev->m_pNext = pParticle;

	++pSystem->m_nParticles;
	++pSystem->m_nChangedParticles;
	return pParticle;
}

// FUNCTION: LITHTECH 0x00406930
inline LTParticle* CLTClient::AddParticle(HLOCALOBJ hObj, LTVector *pPos, LTVector *pVelocity, LTVector *pColor, float lifeTime)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hObj;

	if(pSystem && pSystem->m_ObjectType == OT_PARTICLESYSTEM)
	{
		return (LTParticle*)ps_AddParticle(pSystem, pPos, pColor, pVelocity, lifeTime);
	}
	else
	{
		return LTNULL;
	}
}


// FUNCTION: LITHTECH 0x00406ac0
inline LTRESULT CLTClient::StartQuery(char *pInfo)
{
	CBaseDriver *pDriver = m_pClientMgr->m_NetMgr.m_pMainDriver;

	if(pDriver)
		return pDriver->StartQuery(pInfo);
	else
		RETURN_ERROR(1, StartQuery, LT_NOTINITIALIZED);
}

// FUNCTION: LITHTECH 0x00406b10
inline LTRESULT CLTClient::UpdateQuery()
{
	CBaseDriver *pDriver = m_pClientMgr->m_NetMgr.m_pMainDriver;

	if(pDriver)
		return pDriver->UpdateQuery();
	else
		RETURN_ERROR(1, UpdateQuery, LT_NOTINITIALIZED);
}

// FUNCTION: LITHTECH 0x00406b60
inline LTRESULT CLTClient::GetQueryResults(NetSession* &pListHead)
{
	CBaseDriver *pDriver = m_pClientMgr->m_NetMgr.m_pMainDriver;

	pListHead = LTNULL;
	if(pDriver)
		return pDriver->GetQueryResults(pListHead);
	else
		RETURN_ERROR(1, GetQueryResults, LT_NOTINITIALIZED);
}

// FUNCTION: LITHTECH 0x00406bc0
inline LTRESULT CLTClient::EndQuery()
{
	CBaseDriver *pDriver = m_pClientMgr->m_NetMgr.m_pMainDriver;

	if(pDriver)
		return pDriver->EndQuery();
	else
		RETURN_ERROR(1, EndQuery, LT_NOTINITIALIZED);
}

// FUNCTION: LITHTECH 0x00406c10
inline LTRESULT CLTClient::GetSConValueFloat(char *pName, float &val)
{
	LTCommandVar *pVar;

	if(!pName)
		RETURN_ERROR(2, CLTClient::GetSConValueFloat, LT_INVALIDPARAMS);

	val = 0.0f;
	pVar = cc_FindConsoleVar(&m_pClientMgr->m_ServerConsoleMirror, pName);
	if(pVar)
	{
		val = pVar->floatVal;
		return LT_OK;
	}
	else
	{
		RETURN_ERROR_PARAM(2, CLTClient::GetSConValueFloat, LT_NOTFOUND, pName);
	}
}

// FUNCTION: LITHTECH 0x00406cc0
inline LTRESULT CLTClient::GetSConValueString(char *pName, char *valBuf, uint32 bufLen)
{
	LTCommandVar *pVar;

	if(!pName || bufLen <= 0)
		RETURN_ERROR(2, CLTClient::GetSConValueString, LT_INVALIDPARAMS);

	valBuf[0] = 0;
	pVar = cc_FindConsoleVar(&m_pClientMgr->m_ServerConsoleMirror, pName);
	if(pVar)
	{
		strncpy(valBuf, pVar->pStringVal, bufLen-1);
		valBuf[bufLen-1] = 0;
		return LT_OK;
	}
	else
	{
		RETURN_ERROR_PARAM(2, CLTClient::GetSConValueString, LT_NOTFOUND, pName);
	}
}

// FUNCTION: LITHTECH 0x00406d90
inline float CLTClient::GetServerConVarValueFloat(char *pName)
{
	LTCommandVar *pVar;

	if(pName)
	{
		pVar = cc_FindConsoleVar(&m_pClientMgr->m_ServerConsoleMirror, pName);
		if(pVar)
			return pVar->floatVal;
	}

	return 0.0f;
}

// FUNCTION: LITHTECH 0x00406dc0
inline char* CLTClient::GetServerConVarValueString(char *pName)
{
	LTCommandVar *pVar;

	if(pName)
	{
		pVar = cc_FindConsoleVar(&m_pClientMgr->m_ServerConsoleMirror, pName);
		if(pVar)
			return pVar->pStringVal;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00406df0
inline LTRESULT CLTClient::SendToServer(ILTMessage &msg, uint8 msgID, uint32 flags)
{
	FN_NAME(CLTClient::SendToServer);
	LMessageImpl *pMsg = (LMessageImpl*)&msg;

	CHECK_PARAMS2(pMsg && !pMsg->IsInvalid());

	pMsg->m_pPacket->m_Data[0] = CMSG_MESSAGE;
	pMsg->m_pPacket->WriteType(msgID);

	if(m_pClientMgr->m_pCurShell)
	{
		m_pClientMgr->m_NetMgr.SendPacket(pMsg->m_pPacket, m_pClientMgr->m_pCurShell->m_HostID, flags);
		return LT_OK;
	}

	RETURN_ERROR(2, CLTClient::SendToServer, LT_NOTINITIALIZED);

}

// FUNCTION: LITHTECH 0x00407030
inline LTRESULT CLTClient::ProcessAttachments(HOBJECT hObj)
{
	Attachment *pCur;

	if(!hObj)
		RETURN_ERROR(2, CLTClient::ProcessAttachments, LT_INVALIDPARAMS);

	pCur = hObj->m_Attachments;
	while(pCur)
	{
		r_ProcessAttachment(hObj, pCur);
		pCur = pCur->m_pNext;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004070a0
inline LTRESULT CLTClient::GetSpriteControl(HLOCALOBJ hObj, ILTSpriteControl* &pControl)
{
	if(hObj && hObj->m_ObjectType == OT_SPRITE)
	{
		pControl = (ILTSpriteControl*)((SpriteInstance*)hObj)->m_SCImpl;
		return LT_OK;
	}

	RETURN_ERROR(2, CLTClient::GetSpriteControl, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00407100
inline LTRESULT CLTClient::ModelNodeControl(HOBJECT hObj, NodeControlFn fn, void *pUserData)
{
	FN_NAME(CLTClient::ModelNodeControl);

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	((ModelInstance*)hObj)->m_NodeControlFn = fn;
	((ModelInstance*)hObj)->m_pNodeControlUserData = pUserData;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407170
inline LTRESULT CLTClient::GetCanvasFn(HOBJECT hCanvas, CanvasDrawFn &fn, void* &pUserData)
{
	FN_NAME(CLTClient::GetCanvasFn);

	CHECK_PARAMS2(hCanvas && hCanvas->m_ObjectType == OT_CANVAS);

	fn = ((Canvas*)hCanvas)->m_Fn;
	pUserData = ((Canvas*)hCanvas)->m_pFnUserData;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004071e0
inline LTRESULT CLTClient::SetCanvasFn(HOBJECT hCanvas, CanvasDrawFn fn, void* pUserData)
{
	FN_NAME(CLTClient::SetCanvasFn);

	CHECK_PARAMS2(hCanvas && hCanvas->m_ObjectType == OT_CANVAS);

	((Canvas*)hCanvas)->m_Fn = fn;
	((Canvas*)hCanvas)->m_pFnUserData = pUserData;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407250
inline LTRESULT CLTClient::GetCanvasRadius(HOBJECT hCanvas, float &radius)
{
	FN_NAME(CLTClient::GetCanvasRadius);

	CHECK_PARAMS2(hCanvas && hCanvas->m_ObjectType == OT_CANVAS);

	radius = ((Canvas*)hCanvas)->m_CanvasRadius;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004072b0
inline LTRESULT CLTClient::SetCanvasRadius(HOBJECT hCanvas, float radius)
{
	// (Talon reuses GetCanvasRadius's name here.)
	FN_NAME(CLTClient::GetCanvasRadius);

	CHECK_PARAMS2(hCanvas && hCanvas->m_ObjectType == OT_CANVAS);

	((Canvas*)hCanvas)->m_CanvasRadius = radius;
	cm_RelocateObject(m_pClientMgr, hCanvas);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407320
inline LTRESULT CLTClient::GetGlobalLightDir(LTVector &dir)
{
	dir = g_Render.m_GlobalLightDir;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407350
inline LTRESULT CLTClient::SetGlobalLightDir(LTVector dir)
{
	dir.Norm();
	if(dir.MagSqr() < 0.001f)
		dir.Init(1.0f, 0.0f, 0.0f);

	g_Render.m_GlobalLightDir = dir;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407410
inline LTRESULT CLTClient::GetGlobalLightColor(LTVector &color)
{
	color = g_Render.m_GlobalLightColor;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407440
inline LTRESULT CLTClient::SetGlobalLightColor(LTVector color)
{
	g_Render.m_GlobalLightColor = color;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00407470
inline LTRESULT CLTClient::GetAmbientLight(float &light)
{
	light = (float)g_Render.m_AmbientLight / 255.0f;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004074a0
inline LTRESULT CLTClient::SetAmbientLight(float light)
{
	if(light < 0.0f)
		light = 0.0f;
	else if(light > 1.0f)
		light = 1.0f;

	g_Render.m_AmbientLight = (uint32)(light * 255.0f);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004074f0
inline LTRESULT CLTClient::GetAttachments(HLOCALOBJ hObj, HLOCALOBJ *inList, uint32 inListSize,
	uint32 *outListSize, uint32 *outNumAttachments)
{
	FN_NAME(CLTClient::GetAttachments);

	CHECK_PARAMS2(inList && outListSize && outNumAttachments);

	return m_pCommonLT->GetAttachments(hObj, inList, inListSize, *outListSize, *outNumAttachments);
}

// FUNCTION: LITHTECH 0x00407560
inline LTRESULT CLTClient::StartVideo(char *pFilename, uint32 flags)
{
	if(!pFilename)
		RETURN_ERROR(2, CLTClient::StartVideo, LT_INVALIDPARAMS);

	if(m_hVideo)
	{
		m_pVideoMgr->StopVideo(m_hVideo);
		m_hVideo = LTNULL;
	}

	return m_pVideoMgr->StartOnScreenVideo(pFilename, flags, m_hVideo);
}

// FUNCTION: LITHTECH 0x004075f0
inline LTRESULT CLTClient::StopVideo()
{
	LTRESULT dResult;

	dResult = m_pVideoMgr->StopVideo(m_hVideo);
	m_hVideo = LTNULL;
	return dResult;
}

// FUNCTION: LITHTECH 0x00407620
inline LTRESULT CLTClient::UpdateVideo()
{
	return m_pVideoMgr->UpdateVideo(m_hVideo);
}

// FUNCTION: LITHTECH 0x00407640
inline LTRESULT CLTClient::IsVideoPlaying()
{
	if(m_pVideoMgr->GetVideoStatus(m_hVideo) == LT_OK)
		return VIDEO_PLAYING;

	StopVideo();
	return VIDEO_NOTPLAYING;
}

// FUNCTION: LITHTECH 0x00407680
inline LTRESULT CLTClient::GetPointStatus(LTVector *pPoint)
{
	return m_pCommonLT->GetPointStatus(pPoint);
}

// FUNCTION: LITHTECH 0x00407690
inline LTRESULT CLTClient::GetPointShade(LTVector *pPoint, LTVector *pColor)
{
	return m_pCommonLT->GetPointShade(pPoint, pColor);
}

// FUNCTION: LITHTECH 0x004076a0
inline HMESSAGEWRITE CLTClient::StartMessage(uint8 messageID)
{
	CPacket *pPacket;

	pPacket = m_pClientMgr->AllocPacket();
	if(pPacket)
	{
		LMessageImpl *pMsg = &pPacket->m_Message;
		pMsg->m_MsgID = messageID;
		return pMsg;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x004076d0
inline LTRESULT CLTClient::EndMessage2(HMESSAGEWRITE hMessage, uint32 flags)
{
	LTRESULT dResult;

	if(!hMessage || ((LMessageImpl*)hMessage)->IsInvalid())
		RETURN_ERROR(2, CLTClient::EndMessage2, LT_INVALIDPARAMS);

	dResult = SendToServer(*hMessage, (uint8)((LMessageImpl*)hMessage)->m_MsgID, flags);
	hMessage->Release();
	return dResult;
}

// FUNCTION: LITHTECH 0x00407750
inline LTRESULT CLTClient::EndMessage(HMESSAGEWRITE hMessage)
{
	return EndMessage2(hMessage, MESSAGE_GUARANTEED);
}

// FUNCTION: LITHTECH 0x00407770
inline HMESSAGEWRITE CLTClient::StartHMessageWrite()
{
	CPacket *pPacket;

	pPacket = m_pClientMgr->AllocPacket();
	pPacket->Init(8192, MAX_PACKET_LEN);
	return pPacket->GetMessageImpl();
}

// FUNCTION: LITHTECH 0x00407800
inline HMODELANIM CLTClient::GetModelAnimation(HOBJECT hObj)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;
	HMODELANIM hAnim;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		hAnim = 0;
		pModelLT->GetCurAnim(pTracker, hAnim);
		return hAnim;
	}

	return (HMODELANIM)-1;
}

// FUNCTION: LITHTECH 0x00407840
inline void CLTClient::SetModelAnimation(HOBJECT hObj, HMODELANIM hAnim)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		pModelLT->SetCurAnim(pTracker, hAnim);
	}
}

// FUNCTION: LITHTECH 0x00407870
inline LTRESULT CLTClient::ResetModelAnimation(HOBJECT hObj)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;

	LTRESULT dResult;

	dResult = pModelLT->GetMainTracker(hObj, pTracker);
	if(dResult == LT_OK)
	{
		return pModelLT->ResetAnim(pTracker);
	}

	return dResult;
}

// FUNCTION: LITHTECH 0x004078a0
inline uint32 CLTClient::GetModelPlaybackState(HOBJECT hObj)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;
	uint32 flags;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		flags = 0;
		pModelLT->GetPlaybackState(pTracker, flags);
		return flags;
	}

	return 0;
}

// FUNCTION: LITHTECH 0x004078e0
inline LTBOOL CLTClient::GetModelLooping(HOBJECT hObj)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		return pModelLT->GetLooping(pTracker) == LT_YES;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00407920
inline void CLTClient::SetModelLooping(HOBJECT hObj, LTBOOL bLoop)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		pModelLT->SetLooping(pTracker, bLoop);
	}
}

// FUNCTION: LITHTECH 0x00407950
inline LTBOOL CLTClient::GetModelPlaying(HLOCALOBJ hObj)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		return pModelLT->GetPlaying(pTracker) == LT_YES;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00407990
inline void CLTClient::SetModelPlaying(HLOCALOBJ hObj, LTBOOL bPlaying)
{
	ILTModel *pModelLT = GetModelLT();
	LTAnimTracker *pTracker;

	if(pModelLT->GetMainTracker(hObj, pTracker) == LT_OK)
	{
		pModelLT->SetPlaying(pTracker, bPlaying);
	}
}

// FUNCTION: LITHTECH 0x004079c0
inline LTRESULT CLTClient::FreeUnusedModels()
{
	return m_pClientMgr->FreeUnusedModels();
}

// FUNCTION: LITHTECH 0x004079d0
inline void CLTClient::CPrint(char *pMsg, ...)
{
	va_list marker;
	char str[500];

	va_start(marker, pMsg);
	_vsnprintf(str, 499, pMsg, marker);
	va_end(marker);

	con_PrintString(CONRGB(255,255,255), 0, str);
}

static void ci_GetPointContainersCB(WorldTreeObj *pObj, void *pUser);

// FUNCTION: LITHTECH 0x00407a10
inline uint32 CLTClient::GetPointContainers(LTVector *pPoint, HOBJECT *pList, uint32 maxListSize)
{
	WorldTree *pWorldTree = &g_pClientMgr->m_World.m_WorldTree;
	TempObjArray theArray;
	uint32 nObjects;

	if(!pWorldTree || !pPoint || !pList || maxListSize == 0)
		return 0;

	theArray.m_Point = *pPoint;
	theArray.m_pObjects = pList;
	theArray.m_nMaxObjects = maxListSize;
	theArray.m_pNumObjects = &nObjects;
	nObjects = 0;

	pWorldTree->FindObjectsOnPoint(pPoint, ci_GetPointContainersCB, &theArray, NOA_Objects);
	return nObjects;
}

// FUNCTION: LITHTECH 0x00407a90
inline LTBOOL CLTClient::GetContainerCode(HOBJECT hObj, uint16 *pCode)
{
	ContainerInstance *pContainer = (ContainerInstance*)hObj;

	if(!pContainer || pContainer->m_ObjectType != OT_CONTAINER || !pCode)
		return LTFALSE;

	*pCode = pContainer->m_ContainerCode;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00407ac0
inline HSTRING CLTClient::FormatString(int messageCode, ...)
{
	uint8 *pBuffer;
	int bufferLen;
	va_list marker;
	HSTRING ret;

	// Check our localized module first.
	if(g_pClientMgr->m_hLocalizedClientResourceModule)
	{
		va_start(marker, messageCode);
		pBuffer = str_FormatString(g_pClientMgr->m_hLocalizedClientResourceModule,
			messageCode, &marker, &bufferLen);
		va_end(marker);

		if(pBuffer)
		{
			ret = str_CreateString(pBuffer);
			str_FreeStringBuffer(pBuffer);
			return ret;
		}
	}

	if(g_pClientMgr->m_hClientResourceModule)
	{
		va_start(marker, messageCode);
		pBuffer = str_FormatString(g_pClientMgr->m_hClientResourceModule,
			messageCode, &marker, &bufferLen);
		va_end(marker);

		if(pBuffer)
		{
			ret = str_CreateString(pBuffer);
			str_FreeStringBuffer(pBuffer);
			return ret;
		}
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00407b70
inline HSTRING CLTClient::CopyString(HSTRING hString)
{
	if(hString)
		return str_CopyString(hString);
	else
		return LTNULL;
}

// FUNCTION: LITHTECH 0x00407b90
inline HSTRING CLTClient::CreateString(char *pString)
{
	if(pString)
		return str_CreateStringAnsi(pString);
	else
		return LTNULL;
}

inline void CLTClient::FreeString(HSTRING hString)
{
	ic_FreeString(hString);
}

// FUNCTION: LITHTECH 0x00407bb0
inline LTBOOL CLTClient::CompareStrings(HSTRING hString1, HSTRING hString2)
{
	if(hString1 && hString2)
		return str_CompareStrings(hString1, hString2);
	else if(hString1)
		return LTFALSE;
	else
		return LTTRUE;
}

// FUNCTION: LITHTECH 0x00407be0
inline LTBOOL CLTClient::CompareStringsUpper(HSTRING hString1, HSTRING hString2)
{
	if(hString1 && hString2)
		return str_CompareStringsUpper(hString1, hString2);
	else if(hString1)
		return LTFALSE;
	else
		return LTTRUE;
}

// FUNCTION: LITHTECH 0x00407c10
inline char* CLTClient::GetStringData(HSTRING hString)
{
	if(hString)
		return str_GetStringData(hString);
	else
		return "";
}

// FUNCTION: LITHTECH 0x00407c30
inline float CLTClient::GetVarValueFloat(HCONSOLEVAR hVar)
{
	if(hVar)
		return ((LTCommandVar*)hVar)->floatVal;
	else
		return 0.0f;
}

// FUNCTION: LITHTECH 0x00407c50
inline char* CLTClient::GetVarValueString(HCONSOLEVAR hVar)
{
	if(hVar)
		return ((LTCommandVar*)hVar)->pStringVal;
	else
		return LTNULL;
}

// FUNCTION: LITHTECH 0x00407c70
inline LTFLOAT CLTClient::GetTime()
{
	return g_pClientMgr->m_CurTime;
}

// FUNCTION: LITHTECH 0x00407c80
inline LTFLOAT CLTClient::GetFrameTime()
{
	return g_pClientMgr->m_FrameTime;
}

// FUNCTION: LITHTECH 0x00407c90
inline LTRESULT CLTClient::RemoveObject(HOBJECT hObj)
{
	if(!hObj)
	{
		RETURN_ERROR(1, CLTClient::RemoveObject, LT_INVALIDPARAMS);
	}

	if(hObj->m_ObjectID != (uint16)-1)
	{
		RETURN_ERROR(1, CLTClient::RemoveObject, LT_CANTREMOVESERVEROBJECT);
	}

	return cm_RemoveObjectFromClientWorld(g_pClientMgr, hObj);
}

// The Talon server shares these (/OPT:ICF folds them with the server's copies).
inline HMODELANIM CLTClient::GetAnimIndex(HOBJECT hObj, char *pAnimName)
{
	return ic_GetAnimIndex(hObj, pAnimName);
}

inline uint32 CLTClient::GetObjectFlags(HOBJECT hObj)
{
	uint32 flags = 0;	// as the server's copy: /OPT:ICF folded the two (0x0047d140), so the original's client copy zeroes it too

	m_pCommonLT->GetObjectFlags(hObj, OFT_Flags, flags);
	return flags;
}

// Defined after the members: with an earlier destructor (or one in the class) VC6 emits ??_G
// right after ci_CreateClientInterface; with both defined here, at the constructor.
inline CLTClient::~CLTClient()
{
	if(m_pDirectMusicMgr)
	{
		delete m_pDirectMusicMgr;
		m_pDirectMusicMgr = LTNULL;
	}
}

inline CLTClient::CLTClient(CClientMgr *pClientMgr)
	: m_CommonLT(pClientMgr), m_PhysicsLT(pClientMgr), m_VideoMgr(pClientMgr),
	m_ModelLT(pClientMgr), m_LightAnimLT(pClientMgr), m_TexMod(pClientMgr),
	m_CursorLT(pClientMgr)
{
	m_CommonLT.m_pTransformLT = &m_TransformLT;
	m_CommonLT.m_pModelLT = &m_ModelLT;
	m_CommonLT.SetMathLT(&m_MathLT);

	m_pCommonLT = &m_CommonLT;
	m_pPhysicsLT = &m_PhysicsLT;
	m_pVideoMgr = &m_VideoMgr;
	m_pDirectMusicMgr = pClientMgr->m_pDirectMusicMgr;
	m_pModelLT = &m_ModelLT;
	m_pTransformLT = &m_TransformLT;
	m_pLightAnimLT = &m_LightAnimLT;
	m_pTexMod = &m_TexMod;
	m_pSoundMgr = &pClientMgr->m_SoundMgr;
	m_pCursorLT = &m_CursorLT;
	m_hVideo = LTNULL;

	ci_Init(this);
	m_pClientMgr = pClientMgr;
}

// FUNCTION: LITHTECH 0x00407d30 ??_GCLTClient@@UAEPAXI@Z


// ------------------------------------------------------------------------ //
// Container queries.
// ------------------------------------------------------------------------ //

static LTBOOL _IsPointInContainer(LTVector *pPoint, ContainerInstance *pContainer);

// FUNCTION: LITHTECH 0x00407d80
static void ci_GetPointContainersCB(WorldTreeObj *pObj, void *pUser)
{
	TempObjArray *pArray;
	LTObject *pObject;

	if(pObj->GetObjType() != WTObj_DObject)
		return;

	pObject = (LTObject*)pObj;
	if(pObject->m_ObjectType != OT_CONTAINER)
		return;

	pArray = (TempObjArray*)pUser;
	if(*pArray->m_pNumObjects >= pArray->m_nMaxObjects)
		return;

	if(_IsPointInContainer(&pArray->m_Point, (ContainerInstance*)pObject))
	{
		pArray->m_pObjects[*pArray->m_pNumObjects] = pObject;
		++(*pArray->m_pNumObjects);
	}
}

// FUNCTION: LITHTECH 0x00407dd0
// The named copy of the position decides the term order of MatVMul_H's sums below (without it VC6 loads the y
// products first instead of z).
static LTBOOL _IsPointInContainer(LTVector *pPoint, ContainerInstance *pContainer)
{
	WorldBsp *pWorldBsp;
	LTVector transformedPoint;
	float dist;

	pWorldBsp = pContainer->m_pOriginalBsp;
	LTVector pos = pContainer->GetPos();
	dist = (*pPoint - pos).MagSqr();
	if(dist < (pWorldBsp->m_MaxBox - pWorldBsp->m_MinBox).MagSqr())
	{
		// Transform the point..
		MatVMul_H(&transformedPoint, &pContainer->m_BackTransform, pPoint);

		if(!ci_IsPointInsideBSP(pWorldBsp->m_RootNode, transformedPoint))
		{
			return LTTRUE;
		}
	}

	return LTFALSE;
}


// ------------------------------------------------------------------------ //
// ci_ functions (ILTClient function pointers).
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00407f60
LTBOOL ci_GetSurfaceBounds(SurfaceData *pSurface, LTVector *pMin, LTVector *pMax)
{
	WorldBsp *pBsp;
	Surface *pInternalSurface;
	WorldPoly *pPoly;
	LTVector *pVec;
	uint16 iPoly, i;

	if(!g_pClientMgr->m_pCurShell || !g_pClientMgr->m_World.m_bLoaded || !pSurface || !pMin || !pMax)
		return LTFALSE;

	pInternalSurface = (Surface*)pSurface->m_pInternalSurface;
	pBsp = (WorldBsp*)pSurface->m_pInternalWorldBsp;

	pMin->Init(100000.0f, 100000.0f, 100000.0f);
	pMax->Init(-100000.0f, -100000.0f, -100000.0f);

	iPoly = pInternalSurface->m_iFirstPoly;
	while(iPoly != 0xFFFF)
	{
		pPoly = pBsp->m_Polies[iPoly];
		for(i=0; i < pPoly->m_nVertices; i++)
		{
			pVec = ((SPolyVertex*)(pPoly + 1))[i].m_Vec;
			VEC_MIN(*pMin, *pMin, *pVec);
			VEC_MAX(*pMax, *pMax, *pVec);
		}

		iPoly = pPoly->m_iNextSurfacePoly;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x004080c0
LTRESULT ci_SetPolyHideStatus(HPOLY hPoly, LTBOOL bHide)
{
	WorldPoly *pPoly;

	if(!hPoly)
		RETURN_ERROR(1, CLTClient::SetPolyHideStatus, LT_INVALIDPARAMS);

	if(g_pClientMgr->m_pCurShell)
	{
		pPoly = cm_GetPolyFromHPoly(g_pClientMgr, hPoly);
		if(pPoly)
		{
			if(bHide)
				((Surface*)pPoly->m_pSurface)->m_Flags |= SURF_INVISIBLE;
			else
				((Surface*)pPoly->m_pSurface)->m_Flags &= ~SURF_INVISIBLE;

			return LT_OK;
		}
	}

	RETURN_ERROR(1, CLTClient::SetPolyHideStatus, LT_ERROR);
}

// FUNCTION: LITHTECH 0x004081a0
LTRESULT ci_GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags)
{
	WorldPoly *pPoly;

	if(!hPoly || !pFlags)
		RETURN_ERROR(1, CLTClient::GetPolyTextureFlags, LT_INVALIDPARAMS);

	*pFlags = 0;
	if(g_pClientMgr->m_pCurShell)
	{
		pPoly = cm_GetPolyFromHPoly(g_pClientMgr, hPoly);
		if(pPoly)
		{
			*pFlags = ((Surface*)pPoly->m_pSurface)->m_TextureFlags;
			return LT_OK;
		}
	}

	RETURN_ERROR(2, CLTClient::GetPolyTextureFlags, LT_ERROR);
}

// FUNCTION: LITHTECH 0x00408280
LTRESULT ci_SetModelHook(ModelHookFn fn, void *pUser)
{
	g_pClientMgr->m_ModelHookFn = fn;
	g_pClientMgr->m_ModelHookUser = pUser;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004082b0
LTRESULT ci_InitNetworking(char *pDriver, uint32 dwFlags)
{
	// Shut down the current shell.
	if(g_pClientMgr->m_pCurShell)
	{
		delete g_pClientMgr->m_pCurShell;
		g_pClientMgr->m_pCurShell = LTNULL;
	}

	g_pClientMgr->m_NetMgr.InitDrivers();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004082f0
LTRESULT ci_GetServiceList(NetService* &pListHead)
{
	return g_pClientMgr->m_NetMgr.GetServiceList(pListHead);
}

// FUNCTION: LITHTECH 0x00408310
LTRESULT ci_FreeServiceList(NetService *pListHead)
{
	if(!pListHead)
		RETURN_ERROR(2, CLTClient::GetServiceList, LT_INVALIDPARAMS);

	return g_pClientMgr->m_NetMgr.FreeServiceList(pListHead);
}

// FUNCTION: LITHTECH 0x00408370
LTRESULT ci_SelectService(HNETSERVICE hNetService)
{
	if(!hNetService)
		RETURN_ERROR(2, CLTClient::SelectService, LT_INVALIDPARAMS);

	return g_pClientMgr->m_NetMgr.SelectService(hNetService);
}

// FUNCTION: LITHTECH 0x004083d0
LTRESULT ci_GetSessionList(NetSession* &pListHead, char *pInfo)
{
	return g_pClientMgr->m_NetMgr.GetSessionList(pListHead, pInfo);
}

// FUNCTION: LITHTECH 0x004083f0
LTRESULT ci_FreeSessionList(NetSession *pListHead)
{
	return g_pClientMgr->m_NetMgr.FreeSessionList(pListHead);
}

// FUNCTION: LITHTECH 0x00408410
LTRESULT ci_AddInternetDriver()
{
	if(g_pClientMgr->m_NetMgr.GetDriver("internet"))
		return LT_ALREADYEXISTS;

	return g_pClientMgr->m_NetMgr.AddDriver("internet") ? LT_OK : LT_ERROR;
}

// FUNCTION: LITHTECH 0x00408450
LTRESULT ci_RemoveInternetDriver()
{
	CBaseDriver *pDriver;

	pDriver = g_pClientMgr->m_NetMgr.GetDriver("internet");
	if(pDriver)
	{
		g_pClientMgr->m_NetMgr.RemoveDriver(pDriver);
		return LT_OK;
	}

	return LT_ERROR;
}

// FUNCTION: LITHTECH 0x00408490
LTRESULT ci_GetTcpIpAddress(char* sAddress, uint32 dwBufferSize)
{
	uint16 hostPort;

	if(!sAddress)
		RETURN_ERROR(1, CLTClient::GetTcpIpAddress, LT_INVALIDPARAMS);

	return g_pClientMgr->m_NetMgr.GetLocalIpAddress(sAddress, dwBufferSize, hostPort);
}

// FUNCTION: LITHTECH 0x004084f0
LTRESULT ci_IsLobbyLaunched(char* sDriver)
{
	CBaseDriver *pDriver;

	if(!sDriver)
		sDriver = "dplay2";

	pDriver = g_pClientMgr->m_NetMgr.GetDriver(sDriver);
	if(!pDriver)
	{
		pDriver = g_pClientMgr->m_NetMgr.AddDriver(sDriver);
		if(!pDriver)
			RETURN_ERROR(1, CLTClient::IsLobbyLaunched, LT_ERROR);
	}

	return pDriver->IsLobbyLaunched() ? LT_OK : LT_ERROR;
}

// FUNCTION: LITHTECH 0x00408570
LTRESULT ci_GetLobbyLaunchInfo(char* sDriver, void** ppLobbyLaunchData)
{
	CBaseDriver *pDriver;

	if(!ppLobbyLaunchData)
		RETURN_ERROR(1, CLTClient::GetLobbyLaunchInfo, LT_INVALIDPARAMS);

	if(!sDriver)
		sDriver = "dplay2";

	pDriver = g_pClientMgr->m_NetMgr.GetDriver(sDriver);
	if(!pDriver)
		pDriver = g_pClientMgr->m_NetMgr.AddDriver(sDriver);

	if(!pDriver)
		RETURN_ERROR(1, CLTClient::GetLobbyLaunchInfo, LT_ERROR);

	if(!pDriver->GetLobbyLaunchInfo(ppLobbyLaunchData))
		RETURN_ERROR(1, CLTClient::GetLobbyLaunchInfo, LT_ERROR);

	return LT_OK;
}


// ------------------------------------------------------------------------ //
// ci_Init: installs the ci_ function pointers.
// ------------------------------------------------------------------------ //

LTRESULT	ci_StartGame(StartGameRequest *pRequest);
LTRESULT	ci_SetGameInfo(void *pData, uint32 dataLen);
LTRESULT	ci_GetGameMode(int *mode);
LTRESULT	ci_GetLocalClientID(uint32 *pID);
LTBOOL		ci_IsConnected();
void		ci_Disconnect();
void		ci_Shutdown();
void		ci_ShutdownWithMessage(char *pMsg, ...);
LTRESULT	ci_FlipScreen(uint32 flags);
LTRESULT	ci_ClearScreen(LTRect *pClearRect, uint32 flags, LTVector *vColor);
LTRESULT	ci_Start3D();
LTRESULT	ci_RenderCamera(HLOCALOBJ hCamera);
LTRESULT	ci_RenderObjects(HLOCALOBJ hCamera, HLOCALOBJ *pObjects, int nObjects);
LTRESULT	ci_StartOptimized2D();
LTRESULT	ci_EndOptimized2D();
LTRESULT	ci_SetOptimized2DBlend(LTSurfaceBlend blend);
LTRESULT	ci_SetOptimized2DColor(HLTCOLOR hColor);
LTRESULT	ci_End3D();
FileEntry*	ci_GetFileList(char *pDirName);
LTRESULT	ci_GetWorldInfoString(char *pFilename, char *pInfoString, uint32 maxLen, uint32 *pActualLen);
LTRESULT	ci_ReadConfigFile(char *pFilename);
LTRESULT	ci_WriteConfigFile(char *pFilename);
void		ci_GetAxisOffsets(LTFLOAT *offsets);
void		ci_PlayJoystickEffect(char *pEffectName, float x, float y);
LTBOOL		ci_InitMusic(char *szMusicDLL);
LTBOOL		ci_SetMusicDirectory(char *szMusicDirectory);
LTBOOL		ci_InitInstruments(char *szDLSFile, char *szStyleFile);
LTBOOL		ci_LoadSong(char *szSong);
void		ci_DestroyAllSongs();
LTBOOL		ci_PlayBreak(char *szSong, uint32 dwBoundaryFlags);
void		ci_StopMusic(uint32 dwBoundaryFlags);
LTBOOL		ci_PauseMusic();
LTBOOL		ci_ResumeMusic();
LTBOOL		ci_AddSongToPlayList(char *szPlayList, char *szSong);
void		ci_DeletePlayList(char *szPlayList);
LTBOOL		ci_PlayList(char *szPlayList, char *szTransition, LTBOOL bLoop, uint32 dwBoundaryFlags);
LTBOOL		ci_PlayMotif(char *szMotif, LTBOOL bLoop);
void		ci_StopMotif(char *szMotif);
short		ci_GetMusicVolume();
void		ci_SetMusicVolume(short wVolume);
LTRESULT	ci_InitSound(InitSoundInfo *pSoundInfo);
LTRESULT	ci_GetSound3DProviderLists(Sound3DProvider *&pSound3DProviderList, LTBOOL bVerify);
void		ci_ReleaseSound3DProviderList(Sound3DProvider *pSound3DProviderList);
unsigned short	ci_GetSoundVolume();
void		ci_SetSoundVolume(unsigned short nVolume);
LTRESULT	ci_SetReverbProperties(ReverbProperties *pReverbProperties);
LTRESULT	ci_GetReverbProperties(ReverbProperties *pReverbProperties);
LTRESULT	ci_PlaySound(PlaySoundInfo *pPlaySoundInfo);
LTRESULT	ci_SetSoundPosition(HLTSOUND hSound, LTVector *pPos);
LTRESULT	ci_GetSoundPosition(HLTSOUND hSound, LTVector *pPos);
void		ci_PauseSounds();
void		ci_ResumeSounds();
LTRESULT	ci_GetSoundDuration(HLTSOUND hSound, LTFLOAT *fDuration);
LTRESULT	ci_GetSoundTimer(HLTSOUND hSound, LTFLOAT *fTimer);
LTRESULT	ci_GetSoundData(HLTSOUND hSound, int16 *&pSixteenBitData, int8 *&pEightBitData,
				uint32 *dwSamplesPerSecond, uint32 *dwChannels);
LTRESULT	ci_GetSoundOffset(HLTSOUND hSound, uint32 *dwOffset, uint32 *dwSize);
LTBOOL		ci_IsDone(HLTSOUND hSound);
void		ci_KillSound(HLTSOUND hSound);
void		ci_KillSoundLoop(HLTSOUND hSound);
void		ci_SetListener(LTBOOL bListenerInClient, LTVector *pPos, LTRotation *pRot);
void		ci_GetListener(LTBOOL *bListenerInClient, LTVector *pPos, LTRotation *pRot);
LTBOOL		ci_IntersectSegment(ClientIntersectQuery *pQuery, ClientIntersectInfo *pInfo);
LTBOOL		ci_CastRay(ClientIntersectQuery *pQuery, ClientIntersectInfo *pInfo);
LTRESULT	ci_FindObjectsInSphere(LTVector *pCenter, float radius,
				HLOCALOBJ *inObjects, uint32 nInObjects, uint32 *nOutObjects, uint32 *nFound);
LTRESULT	ci_RegisterConsoleProgram(char *pName, ConsoleProgramFn fn);
LTRESULT	ci_UnregisterConsoleProgram(char *pName);
HCONSOLEVAR	ci_GetConsoleVar(char *pName);
LTRESULT	ci_SetGlobalPanTexture(uint32 index, char *pFilename);
LTRESULT	ci_SetGlobalPanInfo(uint32 index, float xOffset, float zOffset, float xScale, float zScale);
LTRESULT	ci_AddSurfaceEffect(SurfaceEffectDesc *pDesc);
void		ci_SetInputState(LTBOOL bOn);
LTRESULT	ci_ClearInput();
DeviceBinding*	ci_GetDeviceBindings(uint32 nDevice);
void		ci_FreeDeviceBindings(DeviceBinding *pBindings);
LTRESULT	ci_StartDeviceTrack(uint32 nDevices, uint32 nBufferSize);
LTRESULT	ci_TrackDevice(DeviceInput *pInputArray, uint32 *pnInOut);
LTRESULT	ci_EndDeviceTrack();
DeviceObject*	ci_GetDeviceObjects(uint32 nDeviceFlags);
void		ci_FreeDeviceObjects(DeviceObject *pList);
LTRESULT	ci_GetDeviceName(uint32 nDeviceType, char *pStrBuffer, uint32 nBufferSize);
LTRESULT	ci_IsDeviceEnabled(char *strDeviceName, LTBOOL *pIsEnabled);
LTRESULT	ci_EnableDevice(char *strDeviceName);
float		ci_GetGameTime();
float		ci_GetGameFrameTime();
LTRESULT	ci_GetSkyDef(SkyDef *pDef);
LTBOOL		ci_IsCommandOn(int commandNum);
void		ci_RunConsoleString(char *pString);
HLOCALOBJ	ci_GetClientObject();
void		ci_GetGlobalLightScale(LTVector *pScale);
void		ci_SetGlobalLightScale(LTVector *pScale);
void		ci_OffsetGlobalLightScale(LTVector *pOffset);
void		ci_GetGlobalVertexTint(LTVector *pScale);
void		ci_SetGlobalVertexTint(LTVector *pScale);
void		ci_OffsetGlobalVertexTint(LTVector *pOffset);
HLOCALOBJ	ci_CreateObject(ObjectCreateStruct *pStruct);
LTRESULT	ci_GetObjectScale(HLOCALOBJ hObj, LTVector *pScale);
LTRESULT	ci_SetObjectScale(HLOCALOBJ hObj, LTVector *pScale);
void		ci_GetObjectPos(HLOCALOBJ hObj, LTVector *pPos);
void		ci_GetObjectRotation(HLOCALOBJ hObj, LTRotation *pRotation);
void		ci_SetObjectRotation(HLOCALOBJ hObj, LTRotation *pRotation);
void		ci_SetObjectPosAndRotation(HLOCALOBJ hObj, LTVector *pPos, LTRotation *pRotation);
void		ci_GetObjectColor(HLOCALOBJ hObject, float *r, float *g, float *b, float *a);
void		ci_SetObjectColor(HLOCALOBJ hObject, float r, float g, float b, float a);
LTRESULT	ci_GetObjectUserFlags(HLOCALOBJ hObj, uint32 *pFlags);
LTRESULT	ci_SetObjectUserFlags(HLOCALOBJ hObj, uint32 flags);
uint32		ci_GetObjectClientFlags(HLOCALOBJ hObj);
void		ci_SetObjectClientFlags(HLOCALOBJ hObj, uint32 flags);
void*		ci_GetObjectUserData(HLOCALOBJ hObj);
void		ci_SetObjectUserData(HLOCALOBJ hObj, void *pData);
LTRESULT	ci_Get3DCameraPt(HLOCALOBJ hCamera, int sx, int sy, LTVector *pOut);
void		ci_GetCameraFOV(HLOCALOBJ hObj, float *pX, float *pY);
void		ci_SetCameraFOV(HLOCALOBJ hObj, float fovX, float fovY);
void		ci_GetCameraRect(HLOCALOBJ hObj, LTBOOL *bFullscreen, int *left, int *top, int *right, int *bottom);
void		ci_SetCameraRect(HLOCALOBJ hObj, LTBOOL bFullscreen, int left, int top, int right, int bottom);
LTBOOL		ci_GetCameraLightAdd(HLOCALOBJ hCamera, LTVector *pAdd);
LTBOOL		ci_SetCameraLightAdd(HLOCALOBJ hCamera, LTVector *pAdd);
LTRESULT	ci_SetupParticleSystem(HLOCALOBJ hObj, char *pTextureName, float gravityAccel, uint32 flags, float particleRadius);
LTRESULT	ci_SetSoftwarePSColor(HLOCALOBJ hObj, float r, float g, float b);
void		ci_AddParticles(HLOCALOBJ hObj, uint32 nParticles,
				LTVector *pMinOffset, LTVector *pMaxOffset, LTVector *pMinVelocity, LTVector *pMaxVelocity,
				LTVector *pMinColor, LTVector *pMaxColor, float minLifetime, float maxLifetime);
LTBOOL		ci_GetParticles(HLOCALOBJ hObj, LTParticle **pHead, LTParticle **pTail);
LTRESULT	ci_GetParticleLifetime(HLOCALOBJ hSystem, LTParticle *pParticle, LTFLOAT &fLifetime);
LTRESULT	ci_GetParticleTotalLifetime(HLOCALOBJ hSystem, LTParticle *pParticle, LTFLOAT &fTotalLifetime);
void		ci_GetParticlePos(HLOCALOBJ hSystem, LTParticle *pParticle, LTVector *pPos);
void		ci_SetParticlePos(HLOCALOBJ hSystem, LTParticle *pParticle, LTVector *pPos);
void		ci_RemoveParticle(HLOCALOBJ hSystem, LTParticle *pParticle);
LTRESULT	ci_OptimizeParticles(HLOCALOBJ hSystem);
LTBOOL		ci_SetupPolyGrid(HLOCALOBJ hObj, uint32 width, uint32 height, LTBOOL bHalfTriangles);
LTRESULT	ci_SetPolyGridEnvMap(HLOCALOBJ hObj, char *pFilename);
LTRESULT	ci_SetPolyGridTexture(HLOCALOBJ hObj, char *pFilename);
LTRESULT	ci_GetPolyGridTextureInfo(HLOCALOBJ hObj, float *xPan, float *yPan, float *xScale, float *yScale);
LTRESULT	ci_SetPolyGridTextureInfo(HLOCALOBJ hObj, float xPan, float yPan, float xScale, float yScale);
LTRESULT	ci_GetPolyGridInfo(HLOCALOBJ hObj, char **pBytes, uint32 *pWidth, uint32 *pHeight, PGColor **pColorTable);
LTRESULT	ci_FitPolyGrid(HLOCALOBJ hObj, LTVector *pMin, LTVector *pMax, LTVector *pPos, LTVector *pScale);
void		ci_GetLightColor(HLOCALOBJ hObj, float *r, float *g, float *b);
void		ci_SetLightColor(HLOCALOBJ hObj, float r, float g, float b);
float		ci_GetLightRadius(HLOCALOBJ hObj);
void		ci_SetLightRadius(HLOCALOBJ hObj, float radius);
LTRESULT	ci_ClipSprite(HLOCALOBJ hObj, HPOLY hPoly);
int			ci_Parse(char *pCommand, char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs);

// FUNCTION: LITHTECH 0x00408640
void ci_Init(ILTClient *pClientDE)
{
	pClientDE->StartGame = ci_StartGame;
	pClientDE->SetGameInfo = ci_SetGameInfo;
	pClientDE->GetGameMode = ci_GetGameMode;
	pClientDE->GetLocalClientID = ci_GetLocalClientID;
	pClientDE->IsConnected = ci_IsConnected;
	pClientDE->Disconnect = ci_Disconnect;
	pClientDE->Shutdown = ci_Shutdown;
	pClientDE->ShutdownWithMessage = ci_ShutdownWithMessage;

	pClientDE->FlipScreen = ci_FlipScreen;
	pClientDE->ClearScreen = ci_ClearScreen;
	pClientDE->Start3D = ci_Start3D;
	pClientDE->RenderCamera = ci_RenderCamera;
	pClientDE->RenderObjects = ci_RenderObjects;
	pClientDE->StartOptimized2D = ci_StartOptimized2D;
	pClientDE->EndOptimized2D = ci_EndOptimized2D;
	pClientDE->SetOptimized2DBlend = ci_SetOptimized2DBlend;
	pClientDE->SetOptimized2DColor = ci_SetOptimized2DColor;
	pClientDE->End3D = ci_End3D;
	pClientDE->GetRenderModes = dsi_GetRenderModes;
	pClientDE->RelinquishRenderModes = dsi_RelinquishRenderModes;
	pClientDE->SetRenderMode = dsi_SetRenderMode;
	pClientDE->GetRenderMode = dsi_GetRenderMode;
	pClientDE->ShutdownRender = dsi_ShutdownRender;

	pClientDE->GetFileList = ci_GetFileList;
	pClientDE->FreeFileList = ic_FreeFileList;
	pClientDE->GetWorldInfoString = ci_GetWorldInfoString;
	pClientDE->ReadConfigFile = ci_ReadConfigFile;
	pClientDE->WriteConfigFile = ci_WriteConfigFile;

	pClientDE->GetAxisOffsets = ci_GetAxisOffsets;
	pClientDE->PlayJoystickEffect = ci_PlayJoystickEffect;

	pClientDE->InitMusic = ci_InitMusic;
	pClientDE->SetMusicDirectory = ci_SetMusicDirectory;
	pClientDE->InitInstruments = ci_InitInstruments;
	pClientDE->LoadSong = ci_LoadSong;
	pClientDE->DestroyAllSongs = ci_DestroyAllSongs;
	pClientDE->PlayBreak = ci_PlayBreak;
	pClientDE->StopMusic = ci_StopMusic;
	pClientDE->PauseMusic = ci_PauseMusic;
	pClientDE->ResumeMusic = ci_ResumeMusic;
	pClientDE->AddSongToPlayList = ci_AddSongToPlayList;
	pClientDE->DeletePlayList = ci_DeletePlayList;
	pClientDE->PlayList = ci_PlayList;
	pClientDE->PlayMotif = ci_PlayMotif;
	pClientDE->StopMotif = ci_StopMotif;
	pClientDE->GetMusicVolume = ci_GetMusicVolume;
	pClientDE->SetMusicVolume = ci_SetMusicVolume;

	pClientDE->InitSound = ci_InitSound;
	pClientDE->GetSound3DProviderLists = ci_GetSound3DProviderLists;
	pClientDE->ReleaseSound3DProviderList = ci_ReleaseSound3DProviderList;
	pClientDE->GetSoundVolume = ci_GetSoundVolume;
	pClientDE->SetSoundVolume = ci_SetSoundVolume;
	pClientDE->SetReverbProperties = ci_SetReverbProperties;
	pClientDE->GetReverbProperties = ci_GetReverbProperties;
	pClientDE->PlaySound = ci_PlaySound;
	pClientDE->SetSoundPosition = ci_SetSoundPosition;
	pClientDE->GetSoundPosition = ci_GetSoundPosition;
	pClientDE->PauseSounds = ci_PauseSounds;
	pClientDE->ResumeSounds = ci_ResumeSounds;
	pClientDE->GetSoundDuration = ci_GetSoundDuration;
	pClientDE->GetSoundTimer = ci_GetSoundTimer;
	pClientDE->GetSoundData = ci_GetSoundData;
	pClientDE->GetSoundOffset = ci_GetSoundOffset;
	pClientDE->IsDone = ci_IsDone;
	pClientDE->KillSound = ci_KillSound;
	pClientDE->KillSoundLoop = ci_KillSoundLoop;
	pClientDE->SetListener = ci_SetListener;
	pClientDE->GetListener = ci_GetListener;

	pClientDE->IntersectSegment = ci_IntersectSegment;
	pClientDE->CastRay = ci_CastRay;
	pClientDE->FindObjectsInSphere = ci_FindObjectsInSphere;

	pClientDE->RegisterConsoleProgram = ci_RegisterConsoleProgram;
	pClientDE->UnregisterConsoleProgram = ci_UnregisterConsoleProgram;
	pClientDE->GetConsoleVar = ci_GetConsoleVar;

	pClientDE->StartCounter = ic_StartCounter;
	pClientDE->EndCounter = ic_EndCounter;

	pClientDE->SetGlobalPanTexture = ci_SetGlobalPanTexture;
	pClientDE->SetGlobalPanInfo = ci_SetGlobalPanInfo;
	pClientDE->AddSurfaceEffect = ci_AddSurfaceEffect;

	pClientDE->SetInputState = ci_SetInputState;
	pClientDE->ClearInput = ci_ClearInput;
	pClientDE->GetDeviceBindings = ci_GetDeviceBindings;
	pClientDE->FreeDeviceBindings = ci_FreeDeviceBindings;
	pClientDE->StartDeviceTrack = ci_StartDeviceTrack;
	pClientDE->TrackDevice = ci_TrackDevice;
	pClientDE->EndDeviceTrack = ci_EndDeviceTrack;
	pClientDE->GetDeviceObjects = ci_GetDeviceObjects;
	pClientDE->FreeDeviceObjects = ci_FreeDeviceObjects;
	pClientDE->GetDeviceName = ci_GetDeviceName;
	pClientDE->IsDeviceEnabled = ci_IsDeviceEnabled;
	pClientDE->EnableDevice = ci_EnableDevice;

	pClientDE->GetGameTime = ci_GetGameTime;
	pClientDE->GetGameFrameTime = ci_GetGameFrameTime;
	pClientDE->DebugOut = (void (*)(char*, ...))DebugOut;
	pClientDE->GetSkyDef = ci_GetSkyDef;
	pClientDE->IsCommandOn = ci_IsCommandOn;
	pClientDE->RunConsoleString = ci_RunConsoleString;
	pClientDE->GetClientObject = ci_GetClientObject;

	pClientDE->GetGlobalLightScale = ci_GetGlobalLightScale;
	pClientDE->SetGlobalLightScale = ci_SetGlobalLightScale;
	pClientDE->OffsetGlobalLightScale = ci_OffsetGlobalLightScale;
	pClientDE->GetGlobalVertexTint = ci_GetGlobalVertexTint;
	pClientDE->SetGlobalVertexTint = ci_SetGlobalVertexTint;
	pClientDE->OffsetGlobalVertexTint = ci_OffsetGlobalVertexTint;

	pClientDE->CreateObject = ci_CreateObject;
	pClientDE->GetObjectScale = ci_GetObjectScale;
	pClientDE->SetObjectScale = ci_SetObjectScale;
	pClientDE->GetObjectPos = ci_GetObjectPos;
	pClientDE->GetObjectRotation = ci_GetObjectRotation;
	pClientDE->SetObjectRotation = ci_SetObjectRotation;
	pClientDE->SetObjectPosAndRotation = ci_SetObjectPosAndRotation;
	pClientDE->GetObjectColor = ci_GetObjectColor;
	pClientDE->SetObjectColor = ci_SetObjectColor;
	pClientDE->GetObjectUserFlags = ci_GetObjectUserFlags;
	pClientDE->SetObjectUserFlags = ci_SetObjectUserFlags;
	pClientDE->GetObjectClientFlags = ci_GetObjectClientFlags;
	pClientDE->SetObjectClientFlags = ci_SetObjectClientFlags;
	pClientDE->GetObjectUserData = ci_GetObjectUserData;
	pClientDE->SetObjectUserData = ci_SetObjectUserData;

	pClientDE->Get3DCameraPt = ci_Get3DCameraPt;
	pClientDE->GetCameraFOV = ci_GetCameraFOV;
	pClientDE->SetCameraFOV = ci_SetCameraFOV;
	pClientDE->GetCameraRect = ci_GetCameraRect;
	pClientDE->SetCameraRect = ci_SetCameraRect;
	pClientDE->GetCameraLightAdd = ci_GetCameraLightAdd;
	pClientDE->SetCameraLightAdd = ci_SetCameraLightAdd;

	pClientDE->SetupParticleSystem = ci_SetupParticleSystem;
	pClientDE->SetSoftwarePSColor = ci_SetSoftwarePSColor;
	pClientDE->AddParticles = ci_AddParticles;
	pClientDE->GetParticles = ci_GetParticles;
	pClientDE->GetParticleLifetime = ci_GetParticleLifetime;
	pClientDE->GetParticleTotalLifetime = ci_GetParticleTotalLifetime;
	pClientDE->GetParticlePos = ci_GetParticlePos;
	pClientDE->SetParticlePos = ci_SetParticlePos;
	pClientDE->RemoveParticle = ci_RemoveParticle;
	pClientDE->OptimizeParticles = ci_OptimizeParticles;

	pClientDE->GetNextLine = linesystem_GetNextLine;
	pClientDE->GetLineInfo = linesystem_GetLineInfo;
	pClientDE->SetLineInfo = linesystem_SetLineInfo;
	pClientDE->AddLine = linesystem_AddLine;
	pClientDE->RemoveLine = linesystem_RemoveLine;

	pClientDE->SetupPolyGrid = ci_SetupPolyGrid;
	pClientDE->SetPolyGridEnvMap = ci_SetPolyGridEnvMap;
	pClientDE->SetPolyGridTexture = ci_SetPolyGridTexture;
	pClientDE->GetPolyGridTextureInfo = ci_GetPolyGridTextureInfo;
	pClientDE->SetPolyGridTextureInfo = ci_SetPolyGridTextureInfo;
	pClientDE->GetPolyGridInfo = ci_GetPolyGridInfo;
	pClientDE->FitPolyGrid = ci_FitPolyGrid;

	pClientDE->GetLightColor = ci_GetLightColor;
	pClientDE->SetLightColor = ci_SetLightColor;
	pClientDE->GetLightRadius = ci_GetLightRadius;
	pClientDE->SetLightRadius = ci_SetLightRadius;

	pClientDE->ClipSprite = ci_ClipSprite;

	pClientDE->GetNextModelNode = ic_GetNextModelNode;
	pClientDE->GetModelNodeName = ic_GetModelNodeName;

	pClientDE->Parse = ci_Parse;

	pClientDE->GetSurfaceBounds = ci_GetSurfaceBounds;
	pClientDE->SetPolyHideStatus = ci_SetPolyHideStatus;
	pClientDE->GetPolyTextureFlags = ci_GetPolyTextureFlags;
	pClientDE->SetModelHook = ci_SetModelHook;

	pClientDE->InitNetworking = ci_InitNetworking;
	pClientDE->GetServiceList = ci_GetServiceList;
	pClientDE->FreeServiceList = ci_FreeServiceList;
	pClientDE->SelectService = ci_SelectService;
	pClientDE->GetSessionList = ci_GetSessionList;
	pClientDE->FreeSessionList = ci_FreeSessionList;
	pClientDE->AddInternetDriver = ci_AddInternetDriver;
	pClientDE->RemoveInternetDriver = ci_RemoveInternetDriver;
	pClientDE->GetLobbyLaunchInfo = ci_GetLobbyLaunchInfo;
	pClientDE->IsLobbyLaunched = ci_IsLobbyLaunched;
	pClientDE->GetTcpIpAddress = ci_GetTcpIpAddress;

	// The system-dependent functions.
	cis_Init(pClientDE);
}


// FUNCTION: LITHTECH 0x00408cb0
LTRESULT ci_StartGame(StartGameRequest *pRequest)
{
	return g_pClientMgr->StartShell(pRequest);
}

// FUNCTION: LITHTECH 0x00408cd0
LTRESULT ci_SetGameInfo(void *pData, uint32 dataLen)
{
	if(!g_pClientMgr->m_pCurShell)
		RETURN_ERROR(1, SetGameInfo, LT_NOTCONNECTED);

	if(!g_pClientMgr->m_pCurShell->m_pServerMgr)
		RETURN_ERROR(1, SetGameInfo, LT_CANTCREATESERVER);

	g_pClientMgr->m_pCurShell->m_pServerMgr->SetGameInfo(pData, dataLen);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00408d70
LTRESULT ci_GetGameMode(int *mode)
{
	if(g_pClientMgr->m_pCurShell)
		*mode = g_pClientMgr->m_pCurShell->m_ShellMode;
	else
		*mode = GAMEMODE_NONE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00408da0
LTRESULT ci_GetLocalClientID(uint32 *pID)
{
	*pID = 0;

	if(!g_pClientMgr->m_pCurShell || g_pClientMgr->m_pCurShell->m_ClientID == (uint16)-1)
	{
		return LT_NOTCONNECTED;
	}

	*pID = g_pClientMgr->m_pCurShell->m_ClientID;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00408de0
LTBOOL ci_IsConnected()
{
	return !!g_pClientMgr->m_pCurShell;
}

// FUNCTION: LITHTECH 0x00408e00
void ci_Disconnect()
{
	if(g_pClientMgr->m_pCurShell)
	{
		delete g_pClientMgr->m_pCurShell;
		g_pClientMgr->m_pCurShell = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x00408e30
void ci_ShutdownWithMessage(char *pMsg, ...)
{
	va_list marker;
	char str[500];

	va_start(marker, pMsg);
	_vsnprintf(str, 499, pMsg, marker);
	va_end(marker);

	dsi_OnClientShutdown(str);
}

// FUNCTION: LITHTECH 0x00408e70
void ci_Shutdown()
{
	ci_ShutdownWithMessage("");
}

// FUNCTION: LITHTECH 0x00408e80
LTRESULT ci_FlipScreen(uint32 flags)
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, FlipScreen, LT_NOTINITIALIZED);

	g_pClientMgr->ShowDrawSurface(flags);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00408ee0
LTRESULT ci_ClearScreen(LTRect *pClearRect, uint32 flags, LTVector *vColor)
{
	LTRect rect;

	if(!r_IsRenderInitted())
		RETURN_ERROR(1, ClearScreen, LT_NOTINITIALIZED);

	if(pClearRect)
	{
		rect = *pClearRect;
		rect.left = LTCLAMP(rect.left, 0, (int)g_Render.m_Width);
		rect.right = LTCLAMP(rect.right, 0, (int)g_Render.m_Width);
		rect.top = LTCLAMP(rect.top, 0, (int)g_Render.m_Height);
		rect.bottom = LTCLAMP(rect.bottom, 0, (int)g_Render.m_Height);
	}
	else
	{
		rect.left = rect.top = 0;
		rect.right = g_Render.m_Width;
		rect.bottom = g_Render.m_Height;
	}

	if(g_CV_ForceClear)
		flags |= CLEARSCREEN_SCREEN;

	g_Render.Clear(&rect, flags, vColor);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00409010
LTRESULT ci_Start3D()
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, Start3D, LT_NOTINITIALIZED);

	if(g_Render.IsIn3D())
		RETURN_ERROR(1, Start3D, LT_ALREADYIN3D);

	g_Render.Start3D();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004090a0
LTRESULT ci_RenderCamera(HLOCALOBJ hCamera)
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(15, RenderCamera, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(15, RenderCamera, LT_NOTIN3D);

	if(!hCamera || hCamera->m_ObjectType != OT_CAMERA)
		RETURN_ERROR(15, RenderCamera, LT_INVALIDPARAMS);

	cm_Render(g_pClientMgr, (CameraInstance*)hCamera, DRAWMODE_NORMAL, LTNULL, 0);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00409190
LTRESULT ci_RenderObjects(HLOCALOBJ hCamera, HLOCALOBJ *pObjects, int nObjects)
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, RenderCamera, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(1, RenderCamera, LT_NOTIN3D);

	if(!hCamera || hCamera->m_ObjectType != OT_CAMERA)
		RETURN_ERROR(1, RenderCamera, LT_INVALIDPARAMS);

	if(!pObjects || nObjects <= 0)
		return LT_OK;

	if(cm_Render(g_pClientMgr, (CameraInstance*)hCamera, DRAWMODE_OBJECTLIST, pObjects, nObjects))
		return LT_OK;

	RETURN_ERROR(1, RenderObjects, LT_ERROR);
}

// FUNCTION: LITHTECH 0x004092c0
LTRESULT ci_StartOptimized2D()
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, StartOptimized2D, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(1, StartOptimized2D, LT_NOTIN3D);

	g_Render.StartOptimized2D();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00409350
LTRESULT ci_EndOptimized2D()
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, EndOptimized2D, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(1, EndOptimized2D, LT_NOTIN3D);

	g_Render.EndOptimized2D();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004093e0
LTRESULT ci_SetOptimized2DBlend(LTSurfaceBlend blend)
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, SetOptimized2DBlend, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(1, SetOptimized2DBlend, LT_NOTIN3D);

	return g_Render.SetOptimized2DBlend(blend) ? LT_OK : LT_ERROR;
}

// FUNCTION: LITHTECH 0x00409480
LTRESULT ci_SetOptimized2DColor(HLTCOLOR hColor)
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, SetOptimized2DColor, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(1, SetOptimized2DColor, LT_NOTIN3D);

	return g_Render.SetOptimized2DColor(hColor) ? LT_OK : LT_ERROR;
}

// FUNCTION: LITHTECH 0x00409520
LTRESULT ci_End3D()
{
	if(!r_IsRenderInitted())
		RETURN_ERROR(1, End3D, LT_NOTINITIALIZED);

	if(!g_Render.IsIn3D())
		RETURN_ERROR(1, End3D, LT_NOTIN3D);

	g_Render.End3D();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004095b0
FileEntry* ci_GetFileList(char *pDirName)
{
	return cf_GetFileList(g_pClientMgr->m_hFileMgr, pDirName);
}

// FUNCTION: LITHTECH 0x004095d0
LTRESULT ci_GetWorldInfoString(char *pFilename, char *pInfoString, uint32 maxLen, uint32 *pActualLen)
{
	FileRef ref;
	ILTStream *pStream;
	LTRESULT dResult;

	ref.m_pFilename = pFilename;
	ref.m_FileType = FILE_ANYFILE;
	pStream = cf_OpenFile(g_pClientMgr->m_hFileMgr, &ref);
	if(!pStream)
		RETURN_ERROR(1, GetWorldInfoString, LT_NOTFOUND);

	dResult = w_GetWorldInfoString(pStream, pInfoString, maxLen, pActualLen);
	pStream->Release();

	if(dResult == LT_OK)
		return LT_OK;

	RETURN_ERROR(1, GetWorldInfoString, LT_INVALIDWORLDFILE);
}

// FUNCTION: LITHTECH 0x004096b0
LTRESULT ci_ReadConfigFile(char *pFilename)
{
	if(cc_RunConfigFile(&g_ClientConsoleState, pFilename, 0, VARFLAG_SAVE))
		return LT_OK;
	else
		return LT_ERROR;
}

// FUNCTION: LITHTECH 0x004096d0
LTRESULT ci_WriteConfigFile(char *pFilename)
{
	if(cc_SaveConfigFile(&g_ClientConsoleState, pFilename))
		return LT_OK;
	else
		return LT_ERROR;
}

// FUNCTION: LITHTECH 0x004096f0
void ci_GetAxisOffsets(LTFLOAT *offsets)
{
	offsets[0] = g_pClientMgr->m_AxisOffsets[0];
	offsets[1] = g_pClientMgr->m_AxisOffsets[1];
	offsets[2] = g_pClientMgr->m_AxisOffsets[2];
}

// FUNCTION: LITHTECH 0x00409720
void ci_PlayJoystickEffect(char *pEffectName, float x, float y)
{
	g_pClientMgr->m_InputMgr->PlayJoystickEffect(g_pClientMgr->m_InputMgr, pEffectName, x, y);
}

// FUNCTION: LITHTECH 0x00409750
LTBOOL ci_InitMusic(char *szMusicDLL)
{
	return g_pClientMgr->AppInitMusic(szMusicDLL) == LT_OK;
}

// FUNCTION: LITHTECH 0x00409770
LTBOOL ci_SetMusicDirectory(char *szMusicDirectory)
{
	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	GetMusicMgr()->Stop(MUSIC_IMMEDIATE);
	return GetMusicMgr()->SetDataDirectory(szMusicDirectory);
}

// FUNCTION: LITHTECH 0x004097a0
LTBOOL ci_InitInstruments(char *szDLSFile, char *szStyleFile)
{
	if(GetMusicMgr()->m_bValid)
	{
		GetMusicMgr()->Stop(MUSIC_IMMEDIATE);
		if(GetMusicMgr()->m_ulCaps & MUSIC_INSTRUMENTSET)
			return GetMusicMgr()->InitInstruments(szDLSFile, szStyleFile);
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x004097d0
LTBOOL ci_LoadSong(char *szSong)
{
	void *pSong;

	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	pSong = GetMusicMgr()->GetSong(szSong);
	if(!pSong)
	{
		pSong = GetMusicMgr()->CreateSong(szSong);
		if(!pSong)
			return LTFALSE;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00409810
void ci_DestroyAllSongs()
{
	if(!GetMusicMgr()->m_bValid)
		return;

	GetMusicMgr()->DestroyAllSongs();
}

// FUNCTION: LITHTECH 0x00409830
LTBOOL ci_PlayBreak(char *szSong, uint32 dwBoundaryFlags)
{
	void *pSong;

	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	pSong = GetMusicMgr()->GetSong(szSong);
	if(!pSong)
	{
		pSong = GetMusicMgr()->CreateSong(szSong);
		if(!pSong)
			return LTFALSE;
	}

	return GetMusicMgr()->PlayBreak(pSong, dwBoundaryFlags);
}

// FUNCTION: LITHTECH 0x00409890
void ci_StopMusic(uint32 dwBoundaryFlags)
{
	if(!GetMusicMgr()->m_bValid)
		return;

	GetMusicMgr()->Stop(dwBoundaryFlags);
}

// FUNCTION: LITHTECH 0x004098b0
LTBOOL ci_PauseMusic()
{
	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	return GetMusicMgr()->Pause(MUSIC_IMMEDIATE);
}

// FUNCTION: LITHTECH 0x004098d0
LTBOOL ci_ResumeMusic()
{
	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	return GetMusicMgr()->Resume();
}

// FUNCTION: LITHTECH 0x004098f0
LTBOOL ci_AddSongToPlayList(char *szPlayList, char *szSong)
{
	void *pPlayList;
	void *pSong;

	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	if(!szPlayList || !szSong)
		return LTFALSE;

	pPlayList = GetMusicMgr()->GetPlayList(szPlayList);
	if(!pPlayList)
	{
		pPlayList = GetMusicMgr()->CreatePlayList(szPlayList);
		if(!pPlayList)
			return LTFALSE;
	}

	pSong = GetMusicMgr()->GetSong(szSong);
	if(!pSong)
	{
		pSong = GetMusicMgr()->CreateSong(szSong);
		if(!pSong)
			return LTFALSE;
	}

	return GetMusicMgr()->AddSongToList(pPlayList, pSong);
}

// FUNCTION: LITHTECH 0x00409970
void ci_DeletePlayList(char *szPlayList)
{
	void *pPlayList;

	if(!GetMusicMgr()->m_bValid)
		return;

	if(!szPlayList)
		return;

	pPlayList = GetMusicMgr()->GetPlayList(szPlayList);
	if(!pPlayList)
		return;

	GetMusicMgr()->RemoveList(pPlayList);
}

// FUNCTION: LITHTECH 0x004099b0
LTBOOL ci_PlayList(char *szPlayList, char *szTransition, LTBOOL bLoop, uint32 dwBoundaryFlags)
{
	void *pPlayList;
	void *pTransition;

	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	if(!szPlayList)
		return LTFALSE;

	pPlayList = GetMusicMgr()->GetPlayList(szPlayList);
	if(!pPlayList)
		return LTFALSE;

	pTransition = LTNULL;
	if(szTransition)
	{
		pTransition = GetMusicMgr()->GetSong(szTransition);
		if(!pTransition)
		{
			pTransition = GetMusicMgr()->CreateSong(szTransition);
			if(!pTransition)
				return LTFALSE;
		}
	}

	return GetMusicMgr()->PlayList(pPlayList, pTransition, bLoop, dwBoundaryFlags);
}

// FUNCTION: LITHTECH 0x00409a40
LTBOOL ci_PlayMotif(char *szMotif, LTBOOL bLoop)
{
	if(!GetMusicMgr()->m_bValid)
		return LTFALSE;

	if(!szMotif)
		return LTFALSE;

	if(GetMusicMgr()->m_ulCaps & MUSIC_MOTIFSAVAIL)
		return GetMusicMgr()->PlayMotif(szMotif, bLoop);

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00409a80
void ci_StopMotif(char *szMotif)
{
	if(!GetMusicMgr()->m_bValid)
		return;

	if(GetMusicMgr()->m_ulCaps & MUSIC_MOTIFSAVAIL)
		GetMusicMgr()->StopMotif(szMotif);
}

// FUNCTION: LITHTECH 0x00409aa0
short ci_GetMusicVolume()
{
	if(!GetMusicMgr()->m_bValid)
		return 0;

	if(GetMusicMgr()->m_ulCaps & MUSIC_VOLUMECONTROL)
		return (short)GetMusicMgr()->GetVolume();

	return 0;
}

// FUNCTION: LITHTECH 0x00409ad0
void ci_SetMusicVolume(short wVolume)
{
	if(!GetMusicMgr()->m_bValid)
		return;

	if(GetMusicMgr()->m_ulCaps & MUSIC_VOLUMECONTROL)
		GetMusicMgr()->SetVolume(wVolume);
}

// FUNCTION: LITHTECH 0x00409b00
LTRESULT ci_GetSound3DProviderLists(Sound3DProvider *&pSound3DProviderList, LTBOOL bVerify)
{
	return GetClientILTSoundMgrImpl()->GetSound3DProviderLists(pSound3DProviderList, bVerify);
}

// FUNCTION: LITHTECH 0x00409b20
void ci_ReleaseSound3DProviderList(Sound3DProvider *pSound3DProviderList)
{
	GetClientILTSoundMgrImpl()->ReleaseSound3DProviderList(pSound3DProviderList);
}

// FUNCTION: LITHTECH 0x00409b40
LTRESULT ci_InitSound(InitSoundInfo *pSoundInfo)
{
	return GetClientILTSoundMgrImpl()->InitSound(pSoundInfo);
}

// FUNCTION: LITHTECH 0x00409b60
unsigned short ci_GetSoundVolume()
{
	uint16 nVolume;

	GetClientILTSoundMgrImpl()->GetVolume(nVolume);
	return nVolume;
}

// FUNCTION: LITHTECH 0x00409b80
void ci_SetSoundVolume(unsigned short nVolume)
{
	GetClientILTSoundMgrImpl()->SetVolume(nVolume);
}

// FUNCTION: LITHTECH 0x00409ba0
LTRESULT ci_SetReverbProperties(ReverbProperties *pReverbProperties)
{
	return GetClientILTSoundMgrImpl()->SetReverbProperties(pReverbProperties);
}

// FUNCTION: LITHTECH 0x00409bc0
LTRESULT ci_GetReverbProperties(ReverbProperties *pReverbProperties)
{
	return GetClientILTSoundMgrImpl()->GetReverbProperties(pReverbProperties);
}

// FUNCTION: LITHTECH 0x00409be0
LTRESULT ci_PlaySound(PlaySoundInfo *pPlaySoundInfo)
{
	HLTSOUND hSound;
	LTRESULT dResult;

	dResult = GetClientILTSoundMgrImpl()->PlaySound(pPlaySoundInfo, hSound);
	if(dResult == LT_OK)
		g_pClientMgr->m_pClientShell->OnPlaySound(pPlaySoundInfo);

	return dResult;
}

// FUNCTION: LITHTECH 0x00409c20
LTRESULT ci_SetSoundPosition(HLTSOUND hSound, LTVector *pPos)
{
	return GetClientILTSoundMgrImpl()->SetSoundPosition(hSound, pPos);
}

// FUNCTION: LITHTECH 0x00409c40
LTRESULT ci_GetSoundPosition(HLTSOUND hSound, LTVector *pPos)
{
	return GetClientILTSoundMgrImpl()->GetSoundPosition(hSound, pPos);
}

// FUNCTION: LITHTECH 0x00409c60
void ci_PauseSounds()
{
	GetClientILTSoundMgrImpl()->PauseSounds();
}

// FUNCTION: LITHTECH 0x00409c70
void ci_ResumeSounds()
{
	GetClientILTSoundMgrImpl()->ResumeSounds();
}

// FUNCTION: LITHTECH 0x00409c80
LTRESULT ci_GetSoundDuration(HLTSOUND hSound, LTFLOAT *fDuration)
{
	return GetClientILTSoundMgrImpl()->GetSoundDuration(hSound, *fDuration);
}

// FUNCTION: LITHTECH 0x00409ca0
LTRESULT ci_GetSoundTimer(HLTSOUND hSound, LTFLOAT *fTimer)
{
	CSoundInstance *pSoundInstance;

	if(!g_pClientMgr)
		return LT_OK;

	if(!GetClientILTSoundMgrImpl()->IsValid())
		return LT_OK;

	if(!hSound || !fTimer)
		RETURN_ERROR(1, GetSoundDuration, LT_INVALIDPARAMS);

	pSoundInstance = GetClientILTSoundMgrImpl()->FindSoundInstance(hSound, LTTRUE);
	if(pSoundInstance)
	{
		if(pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_PLAYING)
		{
			*fTimer = (pSoundInstance->GetDuration() - pSoundInstance->GetTimer()) * 0.001f;
			return LT_OK;
		}

		return LT_FINISHED;
	}

	RETURN_ERROR(1, GetSoundTimer, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00409db0
LTRESULT ci_GetSoundData(HLTSOUND hSound, int16 *&pSixteenBitData, int8 *&pEightBitData,
	uint32 *dwSamplesPerSecond, uint32 *dwChannels)
{
	CSoundInstance *pSoundInstance;
	CSoundBuffer *pSoundBuffer;
	const AILSOUNDINFO *pSoundInfo;

	if(!g_pClientMgr)
		return LT_OK;

	if(!GetClientILTSoundMgrImpl()->IsValid())
		return LT_OK;

	if(!hSound)
		RETURN_ERROR(1, GetSoundData, LT_INVALIDPARAMS);

	pSoundInstance = GetClientILTSoundMgrImpl()->FindSoundInstance(hSound, LTTRUE);
	if(pSoundInstance)
	{
		pSoundBuffer = pSoundInstance->GetSoundBuffer();
		if(pSoundBuffer)
		{
			// Decompress data.
			if(pSoundBuffer->IsCompressed() && !pSoundBuffer->GetDecompressedSoundBuffer())
				pSoundBuffer->DecompressData();

			pSoundInfo = pSoundBuffer->GetSampleInfo();
			if(pSoundInfo)
			{
				if(dwSamplesPerSecond)
					*dwSamplesPerSecond = pSoundInfo->rate;
				if(dwChannels)
					*dwChannels = pSoundInfo->channels;

				if(pSoundInfo->bits == 8)
				{
					pEightBitData = (int8*)pSoundBuffer->GetSoundData();
					pSixteenBitData = LTNULL;
					return LT_OK;
				}
				else if(pSoundInfo->bits == 16)
				{
					pEightBitData = LTNULL;
					pSixteenBitData = (int16*)pSoundBuffer->GetSoundData();
					return LT_OK;
				}
				else
				{
					pEightBitData = LTNULL;
					pSixteenBitData = LTNULL;
					return LT_ERROR;
				}
			}
		}
	}

	RETURN_ERROR(1, GetSoundData, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00409f40
LTRESULT ci_GetSoundOffset(HLTSOUND hSound, uint32 *dwOffset, uint32 *dwSize)
{
	CSoundInstance *pSoundInstance;
	CSoundBuffer *pSoundBuffer;
	const AILSOUNDINFO *pSoundInfo;

	if(!g_pClientMgr)
		return LT_OK;

	if(!GetClientILTSoundMgrImpl()->IsValid())
		return LT_OK;

	if(!hSound)
		RETURN_ERROR(1, GetSoundOffset, LT_INVALIDPARAMS);

	pSoundInstance = GetClientILTSoundMgrImpl()->FindSoundInstance(hSound, LTTRUE);
	if(pSoundInstance)
	{
		pSoundBuffer = pSoundInstance->GetSoundBuffer();
		if(pSoundBuffer)
		{
			// Decompress data.
			if(pSoundBuffer->IsCompressed() && !pSoundBuffer->GetDecompressedSoundBuffer())
				pSoundBuffer->DecompressData();

			if(pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_PLAYING)
			{
				pSoundInfo = pSoundBuffer->GetSampleInfo();
				if(pSoundInfo)
				{
					if(dwOffset)
					{
						*dwOffset = (pSoundInstance->GetDuration() - pSoundInstance->GetTimer())
							* pSoundInfo->rate * pSoundInfo->channels / 1000;
					}

					if(dwSize)
					{
						if(pSoundInfo->bits)
							*dwSize = pSoundBuffer->GetSoundDataLen() / pSoundInfo->bits * 8;
						else
							return LT_ERROR;
					}

					return LT_OK;
				}
			}

			return LT_FINISHED;
		}
	}

	RETURN_ERROR(1, GetSoundData, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0040a0a0
LTBOOL ci_IsDone(HLTSOUND hSound)
{
	LTBOOL bResult;

	GetClientILTSoundMgrImpl()->IsSoundDone(hSound, bResult);
	return bResult;
}

// FUNCTION: LITHTECH 0x0040a0c0
void ci_KillSound(HLTSOUND hSound)
{
	GetClientILTSoundMgrImpl()->KillSound(hSound);
}

// FUNCTION: LITHTECH 0x0040a0e0
void ci_KillSoundLoop(HLTSOUND hSound)
{
	GetClientILTSoundMgrImpl()->KillSoundLoop(hSound);
}

// FUNCTION: LITHTECH 0x0040a100
void ci_SetListener(LTBOOL bListenerInClient, LTVector *pPos, LTRotation *pRot)
{
	LTBOOL bTeleport;

	bTeleport = LTFALSE;
	if(pPos)
	{
		// Teleport if it moved far.
		if(VEC_DISTSQR(*pPos, GetClientILTSoundMgrImpl()->GetLastListenerPosition()) > 1048576.0f)
			bTeleport = LTTRUE;
	}

	GetClientILTSoundMgrImpl()->SetListener(bListenerInClient, pPos, pRot, bTeleport);
}

// FUNCTION: LITHTECH 0x0040a1d0
void ci_GetListener(LTBOOL *bListenerInClient, LTVector *pPos, LTRotation *pRot)
{
	// Get the "in client" state
	if(bListenerInClient)
		*bListenerInClient = GetClientILTSoundMgrImpl()->IsListenerInClient();

	// Get the position
	if(pPos)
		*pPos = GetClientILTSoundMgrImpl()->GetListenerPosition();

	// Calculate an LTRotation..
	if(pRot)
	{
		LTMatrix mListener;
		LTVector vForward, vRight, vUp;

		vForward = GetClientILTSoundMgrImpl()->GetListenerFront();
		vRight = GetClientILTSoundMgrImpl()->GetListenerRight();
		vUp = vRight.Cross(vForward);
		mListener.SetBasisVectors(&vRight, &vUp, &vForward);
		quat_ConvertFromMatrix((float*)pRot, mListener.m);
	}
}

// FUNCTION: LITHTECH 0x0040a320
LTBOOL ci_CastRay(ClientIntersectQuery *pQuery, ClientIntersectInfo *pInfo)
{
	float mag, scale;

	mag = pQuery->m_Direction.Mag();
	if(mag < 0.00001f)
		return LTFALSE;

	// Scale it to 10000.
	scale = 10000.0f / mag;
	pQuery->m_To = pQuery->m_Direction * scale;
	pQuery->m_To = pQuery->m_To + pQuery->m_From;

	return ci_IntersectSegment(pQuery, pInfo);
}

static void ci_FindObjectsInSphereCB(WorldTreeObj *pObj, void *pUser);

// FUNCTION: LITHTECH 0x0040a3f0
LTRESULT ci_FindObjectsInSphere(LTVector *pCenter, float radius,
	HLOCALOBJ *inObjects, uint32 nInObjects, uint32 *nOutObjects, uint32 *nFound)
{
	WorldTree *pWorldTree;
	LTVector boxMin, boxMax;
	TempObjArray theArray;
	float radiusSqr;
	uint32 i;
	LTLink *pListHead, *pCur;
	LTObject *pObject;

	radiusSqr = radius * radius;
	*nOutObjects = 0;
	*nFound = 0;

	pWorldTree = &g_pClientMgr->m_World.m_WorldTree;
	if(pWorldTree)
	{
		boxMin = *pCenter - LTVector(radius, radius, radius);
		boxMax = *pCenter + LTVector(radius, radius, radius);

		theArray.m_pObjects = inObjects;
		theArray.m_nMaxObjects = nInObjects;
		theArray.m_pNumObjects = nOutObjects;
		theArray.m_pTotalNumFound = nFound;

		pWorldTree->FindObjectsInBox(&boxMin, &boxMax, ci_FindObjectsInSphereCB, &theArray, NOA_Objects);
		return LT_OK;
	}

	// Just search them all.
	for(i=0; i < NUM_OBJECTTYPES; i++)
	{
		pListHead = &g_pClientMgr->m_ObjectMgr.m_ObjectLists[i].m_Head;
		for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pObject = (LTObject*)pCur->m_pData;

			if((pObject->GetPos() - *pCenter).MagSqr() < radiusSqr)
			{
				if(*nOutObjects < nInObjects)
				{
					inObjects[*nOutObjects] = pObject;
					++(*nOutObjects);
				}

				++(*nFound);
			}
		}
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040a590
static void ci_FindObjectsInSphereCB(WorldTreeObj *pObj, void *pUser)
{
	TempObjArray *pStruct;

	if(pObj->GetObjType() != WTObj_DObject)
		return;

	pStruct = (TempObjArray*)pUser;
	if(*pStruct->m_pNumObjects < pStruct->m_nMaxObjects)
	{
		pStruct->m_pObjects[*pStruct->m_pNumObjects] = (LTObject*)pObj;
		++(*pStruct->m_pNumObjects);
	}

	++(*pStruct->m_pTotalNumFound);
}

// FUNCTION: LITHTECH 0x0040a5c0
LTRESULT ci_RegisterConsoleProgram(char *pName, ConsoleProgramFn fn)
{
	if(cc_FindCommand(&g_ClientConsoleState, pName))
		RETURN_ERROR(1, CLTClient::RegisterConsoleProgram, LT_ALREADYEXISTS);

	cc_AddCommand(&g_ClientConsoleState, pName, fn, CMD_USERCOMMAND);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040a630
LTRESULT ci_UnregisterConsoleProgram(char *pName)
{
	uint32 nRemoved;
	LTExtraCommandStruct *pCommand;

	nRemoved = 0;
	while((pCommand = cc_FindCommand(&g_ClientConsoleState, pName)) != LTNULL)
	{
		++nRemoved;
		cc_RemoveCommand(&g_ClientConsoleState, pCommand);
	}

	if(nRemoved == 0)
	{
		RETURN_ERROR(1, CLTClient::UnregisterConsoleProgram, LT_NOTFOUND);
	}

	return LT_OK;
}

// (Folded with r_GetParameter at 0x0046f0c0.)
HCONSOLEVAR ci_GetConsoleVar(char *pName)
{
	return (HCONSOLEVAR)cc_FindConsoleVar(&g_ClientConsoleState, pName);
}

// FUNCTION: LITHTECH 0x0040a6b0
LTRESULT ci_SetGlobalPanTexture(uint32 index, char *pFilename)
{
	FileRef ref;
	GlobalPanInfo *pInfo;

	if(index >= NUM_GLOBALPAN_TYPES)
		RETURN_ERROR(1, SetGlobalPanTexture, LT_INVALIDPARAMS);

	pInfo = &g_Render.m_GlobalPans[index];
	if(pFilename)
	{
		ref.m_FileType = FILE_CLIENTFILE;
		ref.m_pFilename = pFilename;
		pInfo->m_pTexture = cm_AddSharedTexture(g_pClientMgr, &ref);
		if(!pInfo->m_pTexture)
			RETURN_ERROR_PARAM(1, SetGlobalPanTexture, LT_NOTFOUND, pFilename);
	}
	else
	{
		pInfo->m_pTexture = LTNULL;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040a790
LTRESULT ci_SetGlobalPanInfo(uint32 index, float xOffset, float zOffset, float xScale, float zScale)
{
	GlobalPanInfo *pInfo;

	if(index >= NUM_GLOBALPAN_TYPES)
		RETURN_ERROR(1, SetGlobalPanInfo, LT_INVALIDPARAMS);

	pInfo = &g_Render.m_GlobalPans[index];
	pInfo->m_xOffset = xOffset;
	pInfo->m_zOffset = zOffset;
	pInfo->m_xScale = xScale;
	pInfo->m_zScale = zScale;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040a800
LTRESULT ci_AddSurfaceEffect(SurfaceEffectDesc *pDesc)
{
	return cm_AddSurfaceEffect(g_pClientMgr, pDesc);
}

// FUNCTION: LITHTECH 0x0040a820
void ci_SetInputState(LTBOOL bOn)
{
	g_pClientMgr->m_bInputState = bOn;
}

// FUNCTION: LITHTECH 0x0040a840
LTRESULT ci_ClearInput()
{
	return g_pClientMgr->ClearInput();
}

// FUNCTION: LITHTECH 0x0040a850
DeviceBinding* ci_GetDeviceBindings(uint32 nDevice)
{
	if(!g_pClientMgr->m_InputMgr)
		return LTNULL;

	return g_pClientMgr->m_InputMgr->GetDeviceBindings(nDevice);
}

// FUNCTION: LITHTECH 0x0040a870
void ci_FreeDeviceBindings(DeviceBinding *pBindings)
{
	if(!g_pClientMgr->m_InputMgr)
		return;

	g_pClientMgr->m_InputMgr->FreeDeviceBindings(pBindings);
}

// FUNCTION: LITHTECH 0x0040a890
LTRESULT ci_StartDeviceTrack(uint32 nDevices, uint32 nBufferSize)
{
	if(!g_pClientMgr->m_InputMgr)
		return LT_ERROR;

	g_pClientMgr->m_bTrackingInputDevices = LTTRUE;
	if(g_pClientMgr->m_InputMgr->StartDeviceTrack(g_pClientMgr->m_InputMgr, nDevices, nBufferSize))
		return LT_OK;
	else
		return LT_ERROR;
}

// FUNCTION: LITHTECH 0x0040a8e0
LTRESULT ci_TrackDevice(DeviceInput *pInputArray, uint32 *pnInOut)
{
	if(!g_pClientMgr->m_InputMgr)
		return LT_ERROR;

	if(!g_pClientMgr->m_bTrackingInputDevices)
		return LT_ERROR;

	if(g_pClientMgr->m_InputMgr->TrackDevice(pInputArray, pnInOut))
		return LT_OK;
	else
		return LT_ERROR;
}

// FUNCTION: LITHTECH 0x0040a920
LTRESULT ci_EndDeviceTrack()
{
	if(!g_pClientMgr->m_InputMgr)
		return LT_ERROR;

	g_pClientMgr->m_bTrackingInputDevices = LTFALSE;
	if(g_pClientMgr->m_InputMgr->EndDeviceTrack())
		return LT_OK;
	else
		return LT_ERROR;
}

// FUNCTION: LITHTECH 0x0040a960
DeviceObject* ci_GetDeviceObjects(uint32 nDeviceFlags)
{
	if(!g_pClientMgr->m_InputMgr)
		return LTNULL;

	return g_pClientMgr->m_InputMgr->GetDeviceObjects(nDeviceFlags);
}

// FUNCTION: LITHTECH 0x0040a980
void ci_FreeDeviceObjects(DeviceObject *pList)
{
	if(!g_pClientMgr->m_InputMgr)
		return;

	g_pClientMgr->m_InputMgr->FreeDeviceObjects(pList);
}

// FUNCTION: LITHTECH 0x0040a9a0
LTRESULT ci_GetDeviceName(uint32 nDeviceType, char *pStrBuffer, uint32 nBufferSize)
{
	if(!g_pClientMgr->m_InputMgr)
		return LT_ERROR;

	if(g_pClientMgr->m_InputMgr->GetDeviceName(nDeviceType, pStrBuffer, nBufferSize))
		return LT_OK;
	else
		return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0040a9e0
LTRESULT ci_IsDeviceEnabled(char *strDeviceName, LTBOOL *pIsEnabled)
{
	if(!g_pClientMgr->m_InputMgr)
		return LT_ERROR;

	*pIsEnabled = g_pClientMgr->m_InputMgr->IsDeviceEnabled(strDeviceName);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040aa10
LTRESULT ci_EnableDevice(char *strDeviceName)
{
	if(!g_pClientMgr->m_InputMgr)
		return LT_ERROR;

	if(g_pClientMgr->m_InputMgr->EnableDevice(g_pClientMgr->m_InputMgr, strDeviceName))
		return LT_OK;
	else
		return LT_ERROR;
}

// FUNCTION: LITHTECH 0x0040aa40
float ci_GetGameTime()
{
	if(g_pClientMgr->m_pCurShell)
		return g_pClientMgr->m_pCurShell->m_GameTime;
	else
		return 0.0f;
}

// FUNCTION: LITHTECH 0x0040aa60
float ci_GetGameFrameTime()
{
	if(g_pClientMgr->m_pCurShell)
		return g_pClientMgr->m_pCurShell->m_GameFrameTime;
	else
		return 0.0f;
}

// FUNCTION: LITHTECH 0x0040aa80
LTRESULT ci_GetSkyDef(SkyDef *pDef)
{
	*pDef = g_pClientMgr->m_SkyDef;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040aaa0
LTBOOL ci_IsCommandOn(int commandNum)
{
	if(commandNum >= MAX_CLIENT_COMMANDS)
		return LTFALSE;

	return g_pClientMgr->m_Commands[g_pClientMgr->m_iCurInputSlot][commandNum] != 0;
}

// FUNCTION: LITHTECH 0x0040aae0
void ci_RunConsoleString(char *pString)
{
	c_CommandHandler(pString);
}

// FUNCTION: LITHTECH 0x0040aaf0
HLOCALOBJ ci_GetClientObject()
{
	if(g_pClientMgr->m_pCurShell)
		return g_pClientMgr->m_pCurShell->GetClientObject();
	else
		return LTNULL;
}

// FUNCTION: LITHTECH 0x0040ab10
void ci_GetGlobalVertexTint(LTVector *pScale)
{
	*pScale = g_pClientMgr->m_GlobalVertexTint;
}

// FUNCTION: LITHTECH 0x0040ab30
void ci_SetGlobalVertexTint(LTVector *pScale)
{
	g_pClientMgr->m_GlobalVertexTint = *pScale;
	g_pClientMgr->m_GlobalVertexTint.x = LTCLAMP(g_pClientMgr->m_GlobalVertexTint.x, 0.0f, 1.0f);
	g_pClientMgr->m_GlobalVertexTint.y = LTCLAMP(g_pClientMgr->m_GlobalVertexTint.y, 0.0f, 1.0f);
	g_pClientMgr->m_GlobalVertexTint.z = LTCLAMP(g_pClientMgr->m_GlobalVertexTint.z, 0.0f, 1.0f);
}

// FUNCTION: LITHTECH 0x0040ac40
void ci_OffsetGlobalVertexTint(LTVector *pOffset)
{
	g_pClientMgr->m_GlobalVertexTint = g_pClientMgr->m_GlobalVertexTint + *pOffset;
	g_pClientMgr->m_GlobalVertexTint.x = LTCLAMP(g_pClientMgr->m_GlobalVertexTint.x, 0.0f, 1.0f);
	g_pClientMgr->m_GlobalVertexTint.y = LTCLAMP(g_pClientMgr->m_GlobalVertexTint.y, 0.0f, 1.0f);
	g_pClientMgr->m_GlobalVertexTint.z = LTCLAMP(g_pClientMgr->m_GlobalVertexTint.z, 0.0f, 1.0f);
}

// FUNCTION: LITHTECH 0x0040ad90
void ci_GetGlobalLightScale(LTVector *pScale)
{
	*pScale = g_pClientMgr->m_GlobalLightScale;
}

// FUNCTION: LITHTECH 0x0040adb0
void ci_SetGlobalLightScale(LTVector *pScale)
{
	g_pClientMgr->m_GlobalLightScale = *pScale;
	g_pClientMgr->m_GlobalLightScale.x = LTCLAMP(g_pClientMgr->m_GlobalLightScale.x, 0.0f, 2.0f);
	g_pClientMgr->m_GlobalLightScale.y = LTCLAMP(g_pClientMgr->m_GlobalLightScale.y, 0.0f, 2.0f);
	g_pClientMgr->m_GlobalLightScale.z = LTCLAMP(g_pClientMgr->m_GlobalLightScale.z, 0.0f, 2.0f);
}

// FUNCTION: LITHTECH 0x0040aec0
void ci_OffsetGlobalLightScale(LTVector *pOffset)
{
	g_pClientMgr->m_GlobalLightScale = g_pClientMgr->m_GlobalLightScale + *pOffset;
	g_pClientMgr->m_GlobalLightScale.x = LTCLAMP(g_pClientMgr->m_GlobalLightScale.x, 0.0f, 2.0f);
	g_pClientMgr->m_GlobalLightScale.y = LTCLAMP(g_pClientMgr->m_GlobalLightScale.y, 0.0f, 2.0f);
	g_pClientMgr->m_GlobalLightScale.z = LTCLAMP(g_pClientMgr->m_GlobalLightScale.z, 0.0f, 2.0f);
}

// FUNCTION: LITHTECH 0x0040b010
HLOCALOBJ ci_CreateObject(ObjectCreateStruct *pStruct)
{
	InternalObjectSetup setup;
	LTObject *pObject;
	uint32 i;

	setup.m_pSetup = pStruct;
	setup.m_Filename.m_FileType = FILE_CLIENTFILE;
	setup.m_Filename.m_pFilename = pStruct->m_Filename;
	for(i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		setup.m_SkinNames[i].m_pFilename = pStruct->m_SkinNames[i];
		setup.m_SkinNames[i].m_FileType = FILE_CLIENTFILE;
	}

	if(cm_AddObjectToClientWorld(g_pClientMgr, 0xFFFF, &setup, &pObject, LTTRUE, LTTRUE) != LT_OK)
		return LTNULL;

	return pObject;
}

// FUNCTION: LITHTECH 0x0040b0b0
void ci_SetObjectPosAndRotation(HLOCALOBJ hObj, LTVector *pPos, LTRotation *pRotation)
{
	if(hObj && pPos && pRotation)
	{
		cm_MoveAndRotateObject(g_pClientMgr, hObj, pPos, pRotation);
	}
}

// FUNCTION: LITHTECH 0x0040b0e0
LTRESULT ci_GetObjectScale(HLOCALOBJ hObj, LTVector *pScale)
{
	if(!hObj)
		RETURN_ERROR(1, CLTClient::GetObjectScale, LT_INVALIDPARAMS);

	VEC_COPY(*pScale, hObj->m_Scale);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040b140
LTRESULT ci_SetObjectScale(HLOCALOBJ hObj, LTVector *pScale)
{
	if(!hObj)
		RETURN_ERROR(1, CLTClient::SetObjectScale, LT_INVALIDPARAMS);

	cm_ScaleObject(g_pClientMgr, hObj, pScale);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040b1a0
void ci_GetObjectColor(HLOCALOBJ hObject, float *r, float *g, float *b, float *a)
{
	if(!hObject)
		return;

	if(r)
		*r = (float)hObject->m_ColorR / 255.0f;
	if(g)
		*g = (float)hObject->m_ColorG / 255.0f;
	if(b)
		*b = (float)hObject->m_ColorB / 255.0f;
	if(a)
		*a = (float)hObject->m_ColorA / 255.0f;
}

// FUNCTION: LITHTECH 0x0040b230
void ci_SetObjectColor(HLOCALOBJ hObject, float r, float g, float b, float a)
{
	if(!hObject)
		return;

	hObject->m_ColorR = (unsigned char)(r * 255.0f);
	hObject->m_ColorG = (unsigned char)(g * 255.0f);
	hObject->m_ColorB = (unsigned char)(b * 255.0f);
	hObject->m_ColorA = (unsigned char)(a * 255.0f);
}

// FUNCTION: LITHTECH 0x0040b290
LTRESULT ci_GetObjectUserFlags(HLOCALOBJ hObj, uint32 *pFlags)
{
	if(hObj && pFlags)
	{
		*pFlags = hObj->m_UserFlags;
		return LT_OK;
	}

	RETURN_ERROR(1, CLTClient::GetObjectUserFlags, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0040b2f0
LTRESULT ci_SetObjectUserFlags(HLOCALOBJ hObj, uint32 flags)
{
	if(!hObj)
		RETURN_ERROR(1, CLTClient::SetObjectUserFlags, LT_INVALIDPARAMS);

	// Can only set them on client-side objects.
	if(hObj->m_ObjectID == (uint16)-1)
	{
		hObj->m_UserFlags = flags;
		return LT_OK;
	}

	RETURN_ERROR(1, CLTClient::SetObjectUserFlags, LT_ERROR);
}

// FUNCTION: LITHTECH 0x0040b380
uint32 ci_GetObjectClientFlags(HLOCALOBJ hObj)
{
	if(hObj)
		return hObj->m_Unknown188;
	else
		return 0;
}

// FUNCTION: LITHTECH 0x0040b3a0
void ci_SetObjectClientFlags(HLOCALOBJ hObj, uint32 flags)
{
	if(hObj)
		hObj->m_Unknown188 = (uint16)flags;
}

// FUNCTION: LITHTECH 0x0040b3c0
void* ci_GetObjectUserData(HLOCALOBJ hObj)
{
	if(hObj)
		return hObj->m_pUserData;
	else
		return LTNULL;
}

// FUNCTION: LITHTECH 0x0040b3e0
void ci_SetObjectUserData(HLOCALOBJ hObj, void *pData)
{
	if(hObj)
		hObj->m_pUserData = pData;
}

// FUNCTION: LITHTECH 0x0040b400
void ci_GetObjectPos(HLOCALOBJ hObj, LTVector *pPos)
{
	if(hObj && pPos)
		*pPos = hObj->m_Pos;
}

// FUNCTION: LITHTECH 0x0040b430
void ci_GetObjectRotation(HLOCALOBJ hObj, LTRotation *pRotation)
{
	if(hObj && pRotation)
		*pRotation = hObj->m_Rotation;
}

// FUNCTION: LITHTECH 0x0040b470
void ci_SetObjectRotation(HLOCALOBJ hObj, LTRotation *pRotation)
{
	if(hObj && pRotation)
		cm_RotateObject(g_pClientMgr, hObj, pRotation);
}

// Matched in wave 7 phase 2 with Jupiter's named intermediates (xAngle, xCoord, xScale, centerX, ...); the inline
// expressions scheduled the *pOut = vRight * wx copy differently (5 aligned). Talon has no ltsinf/ltcosf locals.
// FUNCTION: LITHTECH 0x0040b4a0
LTRESULT ci_Get3DCameraPt(HLOCALOBJ hCamera, int sx, int sy, LTVector *pOut)
{
	CameraInstance *pCamera = (CameraInstance*)hCamera;
	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA || !pOut)
		RETURN_ERROR(1, Get3DCameraPt, LT_INVALIDPARAMS);

	int left, top, right, bottom;
	if(pCamera->m_bFullScreen)
	{
		left = top = 0;
		right = g_Render.m_Width;
		bottom = g_Render.m_Height;
	}
	else
	{
		left = pCamera->m_Left;
		top = pCamera->m_Top;
		right = pCamera->m_Right;
		bottom = pCamera->m_Bottom;
	}

	if(sx < left || sx >= right || sy < top || sy >= bottom)
		RETURN_ERROR(1, Get3DCameraPt, LT_OUTSIDE);

	// Figure out the scaling value to scale the FOV to 90 degrees.
	float xAngle = pCamera->m_xFov * 0.5f;
	float yAngle = pCamera->m_yFov * 0.5f;

	// Find what x coordinate each angle intercepts the y=1 plane at.
	// A 45 degree angle intercepts at x=1.
	float xCoord = 1.0f / (float)tan(MATH_HALFPI - xAngle);
	float yCoord = 1.0f / (float)tan(MATH_HALFPI - yAngle);
	float xScale = 1.0f / xCoord; // Scale to 45 degree angle to make it x=z.
	float yScale = 1.0f / yCoord; // Scale to 45 degree angle to make it y=z.

	float halfWidth = (float)((right - left) >> 1);
	float halfHeight = (float)((bottom - top) >> 1);
	float centerX = (float)left + halfWidth;
	float centerY = (float)top + halfHeight;

	float wx = (((float)sx - centerX) / halfWidth) / xScale;
	float wy = -((((float)sy - centerY) / halfHeight) / yScale);

	LTVector vRight, vUp, vForward;
	quat_GetVectors((float*)&pCamera->m_Rotation, (float*)&vRight, (float*)&vUp, (float*)&vForward);
	*pOut = vRight * wx;
	*pOut += vUp * wy;
	*pOut += vForward;
	*pOut += pCamera->GetPos();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040b720
void ci_GetCameraFOV(HLOCALOBJ hObj, float *pX, float *pY)
{
	CameraInstance *pCamera = (CameraInstance*)hObj;

	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA || !pX || !pY)
		return;

	*pX = pCamera->m_xFov;
	*pY = pCamera->m_yFov;
}

// FUNCTION: LITHTECH 0x0040b760
void ci_SetCameraFOV(HLOCALOBJ hObj, float fovX, float fovY)
{
	CameraInstance *pCamera = (CameraInstance*)hObj;

	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA)
		return;

	fovX = LTCLAMP(fovX, (MATH_PI / 100.0f), (199.0f * (MATH_PI / 100.0f)));
	fovY = LTCLAMP(fovY, (MATH_PI / 100.0f), (199.0f * (MATH_PI / 100.0f)));

	pCamera->m_xFov = fovX;
	pCamera->m_yFov = fovY;
}

// FUNCTION: LITHTECH 0x0040b810
void ci_GetCameraRect(HLOCALOBJ hObj, LTBOOL *bFullscreen, int *left, int *top, int *right, int *bottom)
{
	CameraInstance *pCamera = (CameraInstance*)hObj;

	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA || !bFullscreen || !left || !top || !right || !bottom)
		return;

	*bFullscreen = pCamera->m_bFullScreen;
	*left = pCamera->m_Left;
	*top = pCamera->m_Top;
	*right = pCamera->m_Right;
	*bottom = pCamera->m_Bottom;
}

// FUNCTION: LITHTECH 0x0040b880
void ci_SetCameraRect(HLOCALOBJ hObj, LTBOOL bFullscreen, int left, int top, int right, int bottom)
{
	CameraInstance *pCamera = (CameraInstance*)hObj;

	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA)
		return;

	pCamera->m_bFullScreen = bFullscreen;
	pCamera->m_Left = left;
	pCamera->m_Top = top;
	pCamera->m_Right = right;
	pCamera->m_Bottom = bottom;
}

// FUNCTION: LITHTECH 0x0040b8d0
LTBOOL ci_GetCameraLightAdd(HLOCALOBJ hCamera, LTVector *pAdd)
{
	CameraInstance *pCamera = (CameraInstance*)hCamera;

	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA)
		return LTFALSE;

	*pAdd = pCamera->m_LightAdd;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0040b910
LTBOOL ci_SetCameraLightAdd(HLOCALOBJ hCamera, LTVector *pAdd)
{
	CameraInstance *pCamera = (CameraInstance*)hCamera;

	if(!pCamera || pCamera->m_ObjectType != OT_CAMERA)
		return LTFALSE;

	pCamera->m_LightAdd = *pAdd;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0040b950
LTRESULT ci_SetupParticleSystem(HLOCALOBJ hObj, char *pTextureName, float gravityAccel, uint32 flags, float particleRadius)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hObj;

	if(!pSystem || pSystem->m_ObjectType != OT_PARTICLESYSTEM || !pTextureName)
		RETURN_ERROR(1, SetupParticleSystem, LT_INVALIDPARAMS);

	pSystem->m_GravityAccel = gravityAccel;
	pSystem->m_Unknown260 = flags;
	pSystem->m_ParticleRadius = particleRadius;

	if(ps_SetTexture(pSystem, g_pClientMgr, pTextureName) == LT_OK)
		return LT_OK;

	RETURN_ERROR(1, SetupParticleSystem, LT_NOTFOUND);
}

// FUNCTION: LITHTECH 0x0040ba10
LTRESULT ci_SetSoftwarePSColor(HLOCALOBJ hObj, float r, float g, float b)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hObj;

	if(pSystem && pSystem->m_ObjectType == OT_PARTICLESYSTEM)
	{
		pSystem->m_SoftwareR = (uint8)(r * 255.0f);
		pSystem->m_SoftwareG = (uint8)(g * 255.0f);
		pSystem->m_SoftwareB = (uint8)(b * 255.0f);
		return LT_OK;
	}

	RETURN_ERROR(1, CLTClient::SetSoftwarePSColor, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0040baa0
void ci_AddParticles(HLOCALOBJ hObj, uint32 nParticles,
	LTVector *pMinOffset, LTVector *pMaxOffset, LTVector *pMinVelocity, LTVector *pMaxVelocity,
	LTVector *pMinColor, LTVector *pMaxColor, float minLifetime, float maxLifetime)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hObj;

	if(pSystem && pSystem->m_ObjectType == OT_PARTICLESYSTEM)
	{
		ps_AddParticles(pSystem, nParticles, pMinOffset, pMaxOffset, pMinVelocity, pMaxVelocity,
			pMinColor, pMaxColor, minLifetime, maxLifetime);
	}
}

// FUNCTION: LITHTECH 0x0040bad0
LTBOOL ci_GetParticles(HLOCALOBJ hObj, LTParticle **pHead, LTParticle **pTail)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hObj;

	if(pSystem && pSystem->m_ObjectType == OT_PARTICLESYSTEM)
	{
		*pHead = (LTParticle*)pSystem->m_ParticleHead.m_pNext;
		*pTail = (LTParticle*)&pSystem->m_ParticleHead;
		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0040bb10
void ci_GetParticlePos(HLOCALOBJ hSystem, LTParticle *pParticle, LTVector *pPos)
{
	if(pParticle && pPos)
		*pPos = ((PSParticle*)pParticle)->m_Pos;
}

// FUNCTION: LITHTECH 0x0040bb40
LTRESULT ci_GetParticleLifetime(HLOCALOBJ hSystem, LTParticle *pParticle, LTFLOAT &fLifetime)
{
	if(!pParticle)
		RETURN_ERROR(1, CLTClient::GetParticleLifetime, LT_INVALIDPARAMS);

	fLifetime = ((PSParticle*)pParticle)->m_Lifetime;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040bb90
LTRESULT ci_GetParticleTotalLifetime(HLOCALOBJ hSystem, LTParticle *pParticle, LTFLOAT &fTotalLifetime)
{
	if(!pParticle)
		RETURN_ERROR(1, CLTClient::GetParticleLifetime, LT_INVALIDPARAMS);

	fTotalLifetime = ((PSParticle*)pParticle)->m_TotalLifetime;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040bbe0
void ci_SetParticlePos(HLOCALOBJ hSystem, LTParticle *pParticle, LTVector *pPos)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hSystem;

	if(!pSystem || !pParticle || !pPos)
		return;

	((PSParticle*)pParticle)->m_Pos = *pPos;

	// Update the extents.
	if(pPos->x < pSystem->m_MinPos.x)	pSystem->m_MinPos.x = pPos->x;
	if(pPos->y < pSystem->m_MinPos.y)	pSystem->m_MinPos.y = pPos->y;
	if(pPos->z < pSystem->m_MinPos.z)	pSystem->m_MinPos.z = pPos->z;
	if(pPos->x > pSystem->m_MaxPos.x)	pSystem->m_MaxPos.x = pPos->x;
	if(pPos->y > pSystem->m_MaxPos.y)	pSystem->m_MaxPos.y = pPos->y;
	if(pPos->z > pSystem->m_MaxPos.z)	pSystem->m_MaxPos.z = pPos->z;

	++pSystem->m_nChangedParticles;
}

// FUNCTION: LITHTECH 0x0040bcc0
void ci_RemoveParticle(HLOCALOBJ hSystem, LTParticle *pParticle)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hSystem;
	PSParticle *pPSParticle = (PSParticle*)pParticle;

	if(!pSystem || !pParticle)
		return;

	if(pSystem->m_ObjectType != OT_PARTICLESYSTEM)
		return;

	pPSParticle->m_pPrev->m_pNext = pPSParticle->m_pNext;
	pPSParticle->m_pNext->m_pPrev = pPSParticle->m_pPrev;
	sb_Free(pSystem->m_pParticleBank, pPSParticle);
	pSystem->m_nParticles--;
}

// FUNCTION: LITHTECH 0x0040bd10
LTRESULT ci_OptimizeParticles(HLOCALOBJ hSystem)
{
	LTParticleSystem *pSystem = (LTParticleSystem*)hSystem;

	if(pSystem && pSystem->m_ObjectType == OT_PARTICLESYSTEM)
	{
		ps_OptimizeParticles(pSystem);
		cm_MoveObject(g_pClientMgr, pSystem, &pSystem->m_Pos, LTTRUE);
		return LT_OK;
	}

	RETURN_ERROR(1, CLTClient::OptimizeParticles, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0040bd80
LTBOOL ci_SetupPolyGrid(HLOCALOBJ hObj, uint32 width, uint32 height, LTBOOL bHalfTriangles)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;

	if(!pGrid || pGrid->m_ObjectType != OT_POLYGRID)
		return LTFALSE;

	return pg_Init(pGrid, width, height, bHalfTriangles);
}

// FUNCTION: LITHTECH 0x0040bda0
LTRESULT ci_SetPolyGridTexture(HLOCALOBJ hObj, char *pFilename)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;
	FileRef ref;
	LTRESULT dResult;

	if(!pGrid || pGrid->m_ObjectType != OT_POLYGRID)
	{
		RETURN_ERROR_PARAM(1, CLTClient::SetPolyGridTexture, LT_ERROR, "invalid PolyGrid");
	}

	ref.m_FileType = FILE_CLIENTFILE;
	ref.m_pFilename = pFilename;
	dResult = LoadSprite(g_pClientMgr, &ref, &pGrid->m_pSprite);
	if(dResult != LT_OK)
		return dResult;

	spr_InitTracker((SpriteTracker*)pGrid->m_SpriteTracker, pGrid->m_pSprite);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040be60
LTRESULT ci_SetPolyGridEnvMap(HLOCALOBJ hObj, char *pFilename)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;
	FileRef ref;

	if(!pGrid || pGrid->m_ObjectType != OT_POLYGRID)
		RETURN_ERROR(0, SetPolyGridEnvMap, LT_INVALIDPARAMS);

	if(pFilename)
	{
		ref.m_FileType = FILE_CLIENTFILE;
		ref.m_pFilename = pFilename;
		pGrid->m_pEnvMap = cm_AddSharedTexture(g_pClientMgr, &ref);
		if(pGrid->m_pEnvMap)
		{
			return LT_OK;
		}
		else
		{
			RETURN_ERROR(1, SetPolyGridEnvMap, LT_NOTFOUND);
		}
	}
	else
	{
		pGrid->m_pEnvMap = LTNULL;
		return LT_OK;
	}
}

// FUNCTION: LITHTECH 0x0040bf50
LTRESULT ci_GetPolyGridTextureInfo(HLOCALOBJ hObj, float *xPan, float *yPan, float *xScale, float *yScale)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;

	if(pGrid && pGrid->m_ObjectType == OT_POLYGRID)
	{
		if(!xPan || !yPan || !xScale || !yScale)
			RETURN_ERROR(0, GetPolyGridTextureInfo, LT_INVALIDPARAMS);

		*xPan = pGrid->m_xPan;
		*yPan = pGrid->m_yPan;
		*xScale = pGrid->m_xScale;
		*yScale = pGrid->m_yScale;
		return LT_OK;
	}

	RETURN_ERROR_PARAM(1, CLTClient::GetPolyGridTextureInfo, LT_ERROR, "invalid PolyGrid");
}

// FUNCTION: LITHTECH 0x0040c030
LTRESULT ci_SetPolyGridTextureInfo(HLOCALOBJ hObj, float xPan, float yPan, float xScale, float yScale)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;

	if(pGrid && pGrid->m_ObjectType == OT_POLYGRID)
	{
		pGrid->m_xPan = xPan;
		pGrid->m_yPan = yPan;
		pGrid->m_xScale = xScale;
		pGrid->m_yScale = yScale;
		return LT_OK;
	}

	RETURN_ERROR_PARAM(1, CLTClient::SetPolyGridTextureInfo, LT_ERROR, "invalid PolyGrid");
}

// FUNCTION: LITHTECH 0x0040c0b0
LTRESULT ci_GetPolyGridInfo(HLOCALOBJ hObj, char **pBytes, uint32 *pWidth, uint32 *pHeight, PGColor **pColorTable)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;

	if(!pGrid || pGrid->m_ObjectType != OT_POLYGRID || !pBytes || !pWidth || !pHeight || !pColorTable)
		RETURN_ERROR(0, GetPolyGridInfo, LT_INVALIDPARAMS);

	*pBytes = pGrid->m_Data;
	*pWidth = pGrid->m_Width;
	*pHeight = pGrid->m_Height;
	*pColorTable = (PGColor*)pGrid->m_ColorTable;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040c150
LTRESULT ci_FitPolyGrid(HLOCALOBJ hObj, LTVector *pMin, LTVector *pMax, LTVector *pPos, LTVector *pScale)
{
	LTPolyGrid *pGrid = (LTPolyGrid*)hObj;
	LTVector vCenter;

	if(!pGrid || pGrid->m_ObjectType != OT_POLYGRID || !pMin || !pMax)
		RETURN_ERROR(1, CLTClient::FitPolyGrid, LT_INVALIDPARAMS);

	vCenter = (*pMax - *pMin);
	vCenter *= 0.5f;
	vCenter += *pMin;

	if(pPos)
		*pPos = vCenter;

	if(pScale)
	{
		float fH = (float)pGrid->m_Height * 0.5f;
		float dz = pMax->z - vCenter.z;
		pScale->x = (pMax->x - vCenter.x) / ((float)pGrid->m_Width * 0.5f);
		pScale->z = dz / fH;
		pScale->y = (pMax->y - vCenter.y) / 127.0f;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040c300
void ci_GetLightColor(HLOCALOBJ hObj, float *r, float *g, float *b)
{
	ci_GetObjectColor(hObj, r, g, b, LTNULL);
}

// FUNCTION: LITHTECH 0x0040c320
void ci_SetLightColor(HLOCALOBJ hObject, float r, float g, float b)
{
	if(!hObject)
		return;

	ci_SetObjectColor(hObject, r, g, b, (float)hObject->m_ColorA / 255.0f);
}

// (Folded with si_GetLightRadius at 0x00481610.)
float ci_GetLightRadius(HLOCALOBJ hObj)
{
	DynamicLight *pLight = (DynamicLight*)hObj;

	if(pLight->m_ObjectType == OT_LIGHT)
		return pLight->m_LightRadius;
	else
		return 1.0f;
}

// FUNCTION: LITHTECH 0x0040c360
void ci_SetLightRadius(HLOCALOBJ hObj, float radius)
{
	DynamicLight *pLight = (DynamicLight*)hObj;

	if(pLight->m_ObjectType != OT_LIGHT)
		return;

	if(radius > pLight->m_LightRadius)
	{
		pLight->m_LightRadius = radius;
		cm_RelocateObject(g_pClientMgr, pLight);
	}
	else
	{
		pLight->m_LightRadius = radius;
	}
}

// FUNCTION: LITHTECH 0x0040c3a0
LTRESULT ci_ClipSprite(HLOCALOBJ hObj, HPOLY hPoly)
{
	if(!hObj)
		return LT_ERROR;

	if(hObj->m_ObjectType != OT_SPRITE)
		return LT_ERROR;

	((SpriteInstance*)hObj)->m_ClipperPoly = hPoly;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040c3d0
int ci_Parse(char *pCommand, char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs)
{
	return cp_Parse(pCommand, (const char**)pNewCommandPos, argBuffer, argPointers, nArgs);
}

// FUNCTION: LITHTECH 0x0040c3e0
void ci_Term()
{
	cis_Term();
}

// FUNCTION: LITHTECH 0x0040c3f0 ?SetSize2@?$CMoArray@EVDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0040c460 ?BaseNew@@YAPAEPAVLAlloc@@PAEK@Z
