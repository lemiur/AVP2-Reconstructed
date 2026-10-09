// Talon server interface implementation (Jupiter runtime/server/src/serverde_impl.cpp).
// In Talon, ILTServer mixes virtual methods (implemented by CLTServer) with C function
// pointers (the si_ functions, installed by si_SetupFunctionPointers).
#ifndef __SERVERDE_IMPL_H__
#define __SERVERDE_IMPL_H__

#include "servermgr.h"
#include "iltserver.h"
#include "iltphysics.h"
#include "iltmodel.h"
#include "ilttransform.h"
#include "iltlightanim.h"
#include "shared_iltcommon.h"
#include "server_filemgr.h"
#include "s_object.h"
#include "smoveabstract.h"
#include "interlink.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "s_client.h"
#include "server_extradata.h"

class CServerMgr;

// The child model links ILTServer::LinkModelToExtraChildModel builds: child model filename -> set of
// extra child model filenames. An STLport map, member of CLTServer: every includer builds with the STLport
// FLAGS (see serverde_impl.cpp).
#include <map>
#include <set>
#include <string>
typedef std::set<std::string> ExtraChildSet;
typedef std::map<std::string, ExtraChildSet> ExtraChildMap;

// Installs the si_ function pointers.
void si_SetupFunctionPointers(ILTServer *pServer);

// The interface classes CLTServer embeds (their methods live in serverde_impl.cpp).
// vtable 0x004c8318.
class SPhysicsLT : public ILTPhysics
{
public:
	SPhysicsLT(CServerMgr *pServerMgr) : m_fStairHeight(-1.0f)
	{
		m_pServerMgr = pServerMgr;
		m_ClientServerType = ServerType;
	}

	virtual LTRESULT	SetVelocity(HOBJECT hObj, LTVector *pVel);
	virtual LTRESULT	SetAcceleration(HOBJECT hObj, LTVector *pAccel);
	virtual LTRESULT	SetObjectDims(HOBJECT hObj, LTVector *pNewDims, uint32 flags);
	virtual LTRESULT	MoveObject(HOBJECT hObj, LTVector *pPos, uint32 flags);
	virtual LTRESULT	GetGlobalForce(LTVector &vec);
	virtual LTRESULT	SetGlobalForce(LTVector &vec);
	virtual LTRESULT	GetStairHeight(float &fHeight);
	virtual LTRESULT	SetStairHeight(float fHeight);

	float		m_fStairHeight;		// 0x08
	CServerMgr	*m_pServerMgr;		// 0x0c
};

// vtable 0x004c8368.
class ServerCommonLT : public CommonLT
{
public:
	virtual LTRESULT	SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct *pStruct);
	virtual LTRESULT	SetObjectFlags(HOBJECT hObj, const ObjFlagType flagType, uint32 dwFlags);
	virtual LTRESULT	GetAttachmentObjects(HATTACHMENT hAttachment, HOBJECT &hParent, HOBJECT &hChild);
	virtual LTRESULT	CreateMessage(ILTMessage* &pMsg);
	virtual LTRESULT	GetPointStatus(LTVector *pPoint);
	virtual LTRESULT	GetPointShade(LTVector *pPoint, LTVector *pColor);
	virtual LTRESULT	GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags);
	virtual LTRESULT	GetPolyInfo(HPOLY hPoly, LTPlane **ppPlane, LTVector *pVertexList,
		uint32 nVertexListMaxSize, uint32 *pnNumVertices);
	virtual LTRESULT	GetPolySurfaceFlags(HPOLY hPoly, uint32 &dwSurfFlags);

	CServerMgr	*m_pServerMgr;		// 0x10
};

// vtable 0x004c83c0.
class ServerModelLT : public ILTModel
{
public:
	ServerModelLT(CServerMgr *pServerMgr)
	{
		m_pServerMgr = pServerMgr;
	}

	virtual LTRESULT	SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL bHidden);
	virtual LTRESULT	AddTracker(HOBJECT hObj, LTAnimTracker *pTracker);
	virtual LTRESULT	RemoveTracker(HOBJECT hObj, LTAnimTracker *pTracker);
	virtual LTRESULT	SetCurAnim(LTAnimTracker *pTracker, HMODELANIM hAnim);
	virtual LTRESULT	ResetAnim(LTAnimTracker *pTracker);
	virtual LTRESULT	SetLooping(LTAnimTracker *pTracker, LTBOOL bLooping);
	virtual LTRESULT	SetPlaying(LTAnimTracker *pTracker, LTBOOL bPlaying);
	virtual LTRESULT	SetWeightSet(LTAnimTracker *pTracker, HMODELWEIGHTSET hSet);
	virtual LTRESULT	SetAllowTransition(LTAnimTracker *pTracker, LTBOOL bAllowTransition);
	virtual LTRESULT	SetTimeScale(LTAnimTracker *pTracker, LTFLOAT fTimeScale);
	virtual LTRESULT	SetCurAnimTime(LTAnimTracker *pTracker, uint32 curTime);

	CServerMgr	*m_pServerMgr;		// 0x04
};

// vtable 0x004c8304.
class ServerLightAnimLT : public ILTLightAnim
{
public:
	ServerLightAnimLT(CServerMgr *pServerMgr)
	{
		m_pServerMgr = pServerMgr;
	}

	virtual LTRESULT	FindLightAnim(const char *pName, HLIGHTANIM &hLightAnim);
	virtual LTRESULT	GetNumFrames(HLIGHTANIM hLightAnim, uint32 &nFrames);
	virtual LTRESULT	GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info);
	virtual LTRESULT	SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info);

	CServerMgr	*m_pServerMgr;		// 0x04
};



// Talon CLTServer (0x28c+ bytes).
class CLTServer : public ILTServer
{
public:
	CLTServer(CServerMgr *pServerMgr);

	virtual			~CLTServer();

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

	// ILTServer.
	virtual LTRESULT	GetNumClassProps(const HCLASS hClass, uint32 &count);
	virtual LTRESULT	GetClassProp(const HCLASS hClass, const uint32 iProp, ClassPropInfo &info);
	virtual LTRESULT	GetClassName(const HCLASS hClass, char *pName, uint32 maxNameBytes);
	virtual LTRESULT	GetGlobalForce(LTVector *pVec);
	virtual LTRESULT	SetGlobalForce(LTVector *pVec);
	virtual LTRESULT	ThreadLoadFile(char *pFilename, uint32 type);
	virtual LTRESULT	UnloadFile(char *pFilename, uint32 type);
	virtual LTRESULT	GetHPolyObject(const HPOLY hPoly, HOBJECT &hObject);
	virtual LTRESULT	FindNamedObjects(char *pName, BaseObjArray<HOBJECT> &objArray, uint32 *nTotalFound);
	virtual LTRESULT	FindWorldModelObjectIntersections(HOBJECT hWorldModel,
		LTVector vNewPos, LTRotation rNewRot, BaseObjArray<HOBJECT> &objArray);
	virtual LTRESULT	GetWorldBox(LTVector &min, LTVector &max);
	virtual HMESSAGEWRITE	StartSpecialEffectMessage(LPBASECLASS pObject);
	virtual HMESSAGEWRITE	StartInstantSpecialEffectMessage(LTVector *pPos);
	virtual HMESSAGEWRITE	StartMessageToObject(LPBASECLASS pSender, HOBJECT hSendTo, uint32 messageID);
	virtual LTRESULT	StartMessageToServer(LPBASECLASS pSender, uint32 messageID, HMESSAGEWRITE *hWrite);
	virtual HMESSAGEWRITE	StartMessage(HCLIENT hSendTo, uint8 messageID);
	virtual LTRESULT	EndMessage2(HMESSAGEWRITE hMessage, uint32 flags);
	virtual LTRESULT	EndMessage(HMESSAGEWRITE hMessage);
	virtual LTRESULT	SetObjectSFXMessage(HOBJECT hObject, ILTMessage &msg);
	virtual LTRESULT	SendToObject(ILTMessage &msg, uint32 msgID, HOBJECT hSender, HOBJECT hSendTo, uint32 flags);
	virtual LTRESULT	SendToServer(ILTMessage &msg, uint32 msgID, HOBJECT hSender, uint32 flags);
	virtual LTRESULT	SendToClient(ILTMessage &msg, uint8 msgID, HCLIENT hSendTo, uint32 flags);
	virtual LTRESULT	SendSFXMessage(ILTMessage &msg, LTVector &pos, uint32 flags);
	virtual LTRESULT	SendTo(const void *pData, uint32 len, const char *sAddr, uint32 port);
	virtual LTRESULT	GetClientPing(HCLIENT hClient, float &ping);
	virtual void		LinkModelToExtraChildModel(char *child_model_key, char **associated_chmdl, int size_chmld);
	virtual void		ResetModelToChildModelLink();
	virtual void*		GetChildModelLinkMap();
	virtual float		GetObjectMass(HOBJECT hObj);
	virtual void		SetObjectMass(HOBJECT hObj, float mass);
	virtual float		GetForceIgnoreLimit(HOBJECT hObj, float &limit);
	virtual void		SetForceIgnoreLimit(HOBJECT hObj, float limit);
	virtual char*		GetObjectName(HOBJECT hObject);
	virtual LTRESULT	GetObjectName(HOBJECT hObject, char *pName, uint32 nameBufSize);
	virtual LTRESULT	SetFrictionCoefficient(HOBJECT hObj, float coeff);
	virtual LTRESULT	MoveObject(HOBJECT hObj, LTVector *pNewPos);
	virtual LTRESULT	GetStandingOn(HOBJECT hObj, CollisionInfo *pInfo);
	virtual uint32		GetObjectFlags(HOBJECT hObj);
	virtual void		SetObjectFlags(HOBJECT hObj, uint32 flags);
	virtual LTRESULT	GetNetFlags(HOBJECT hObj, uint32 &flags);
	virtual LTRESULT	SetNetFlags(HOBJECT hObj, uint32 flags);
	virtual void		GetObjectDims(HOBJECT hObj, LTVector *pNewDims);
	virtual LTRESULT	SetObjectDims(HOBJECT hObj, LTVector *pNewDims);
	virtual LTRESULT	SetObjectDims2(HOBJECT hObj, LTVector *pNewDims);
	virtual LTRESULT	GetVelocity(HOBJECT hObj, LTVector *pVel);
	virtual LTRESULT	SetVelocity(HOBJECT hObj, LTVector *pVel);
	virtual LTRESULT	GetAcceleration(HOBJECT hObj, LTVector *pAccel);
	virtual LTRESULT	SetAcceleration(HOBJECT hObj, LTVector *pAccel);
	virtual LTRESULT	SetModelFilenames(HOBJECT hObj, char *pFilename, char *pSkinName);
	virtual LTRESULT	SetObjectFilenames(HOBJECT hObj, char *pFilename, char *pSkinName);
	virtual LTRESULT	GetModelAnimUserDims(HOBJECT hObj, LTVector *pDims, HMODELANIM hAnim);
	virtual LTRESULT	GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags);
	virtual LTRESULT	GetClientData(HCLIENT hClient, void *&pData, uint32 &nLength);

protected:
	void		SendFileIOMessage(uint8 fileType, uint8 msgID, uint16 fileID, LTBOOL bTellLocal);
	LTRESULT	ThreadLoadTexture(char *pFilename);
	LTRESULT	UnloadTexture(char *pFilename);

public:
	CServerMgr		*m_pServerMgr;		// 0x244
	ILTTransform	m_TransformLT;		// 0x248
	ServerModelLT	m_ModelLT;			// 0x24c
	ServerCommonLT	m_CommonLT;			// 0x254
	SPhysicsLT		m_PhysicsLT;		// 0x268
	ServerLightAnimLT	m_LightAnimLT;	// 0x278
	ExtraChildMap	m_ChildModelLinks;	// 0x280 STLport map (child model links)
};



// ------------------------------------------------------------------------ //
// Helpers that live in other units.
// ------------------------------------------------------------------------ //

// Change flags (ServerData::m_ChangeFlags).
#define CF_ROTATION		(1<<2)
#define CF_FLAGS		(1<<3)
#define CF_SCALE		(1<<4)
#define CF_MODELINFO	(1<<5)
#define CF_RENDERINFO	(1<<6)
#define CF_ATTACHMENTS	(1<<8)
#define CF_SNAPROTATION	(1<<10)
#define CF_RESETANIM	(1<<13)

// Internal object flags (LTObject::m_InternalFlags) not in s_object.h.
#define IFLAG_INACTIVE			(1<<3)
#define IFLAG_INACTIVE_TOUCH	(1<<4)
#define IFLAG_AUTODEACTIVATED	(1<<5)
#define IFLAG_INACTIVE_MASK		(IFLAG_INACTIVE | IFLAG_INACTIVE_TOUCH | IFLAG_AUTODEACTIVATED)
#define IFLAG_INSKY				(1<<8)
#define IFLAG_ATTACHED			(1<<10)	// The object is attached to another one.

// Server internal flags (CServerMgr::m_InternalFlags).
#define SFLAG_DEMOPLAYBACK			(1<<2)
#define SFLAG_BUILDINGCACHELIST		(1<<4)

#define DISCONNECTREASON_KICKED		6

// GLOBAL: LITHTECH 0x004e3794
extern LTBOOL g_bAutoDeactivate;

void		BPrint(const char *pMsg, ...);								// 0x004861a0
LTRESULT	sm_CacheFile(CServerMgr *pServerMgr, uint32 fileType, char *pFilename);	// 0x00486c50
void		sm_SetSendSkyDef(CServerMgr *pServerMgr);					// 0x00473010
LTRESULT	sm_RemoveObjectFromSky(CServerMgr *pServerMgr, LTObject *pObj);	// 0x004733f0
LTRESULT	sm_AttachClient(CServerMgr *pServerMgr, Client *pParent, Client *pChild);	// 0x00470c10
LTRESULT	sm_DetachClient(CServerMgr *pServerMgr, Client *pClient);	// 0x00470db0
void		sm_ActivateObjectsNear(CServerMgr *pServerMgr, LTObject *pObj);	// 0x00486850
ObjRefEntry*	AddObjRef(LTObject *pObj);								// 0x00443da0
void		ReleaseObjRef(ObjRefEntry *pRef);							// 0x00443e60 (interlink)

void		GetAttachmentTransform(LTObject *pParent, Attachment *pAttachment, LTVector &vPos, LTRotation &rRot);	// 0x00462510

// LMessageImpl::m_MsgType: where CLTServer::EndMessage2 sends a message.
#define MSGTYPE_OBJECT		0
#define MSGTYPE_CLIENT		1
#define MSGTYPE_SFX			2
#define MSGTYPE_INSTANTSFX	3
#define MSGTYPE_SERVER		4

// Server -> client packet IDs.
#define SMSG_MESSAGE		13
#define SMSG_SFXMESSAGE		18

class CPacket;
LTRESULT	sm_SendSFXMessage(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket, LTObject *pObj, LTVector *pPos, uint32 flags);	// 0x00486ab0
void		sm_SetObjectSpecialEffectMessage(CServerMgr *pServerMgr, LTObject *pObj, class CPacket *pPacket);	// 0x00477e80 (s_object)

void		RetransformWorldModel(WorldModelInstance *pWorldModel);	// 0x0045d1e0 (moveobject)
LTBOOL		DoesBoxIntersectBSP(Node *pRoot, LTVector &vMin, LTVector &vMax);	// 0x0041aed0

LTRESULT	sm_AddObjectToWorld(CServerMgr *pServerMgr, LPBASECLASS pObject, ClassDef *pClass,
	ObjectCreateStruct *pStruct, uint16 objectID, uint32 initialUpdateCode, LTObject **ppOut);	// 0x00484090

#define SMSG_THREADLOAD		23
#define SMSG_UNLOAD			24


// Function pointer targets that live in other units.
void		ic_StartCounter(LTCounter *pCounter);	// 0x00473ac0 (folded with every empty void function)
int			ic_Parse(char *pCommand, char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs);	// 0x0040c3d0
LTBOOL		si_GetPointShade(LTVector *pPoint, LTVector *pColor);	// 0x004795f0
void		DebugOut(const char *pMsg, ...);			// 0x00430710

// GLOBAL: LITHTECH 0x004e4df8
extern Node *g_NodeStack[];

#endif  // __SERVERDE_IMPL_H__
