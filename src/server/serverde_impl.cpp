// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
// Jupiter runtime/server/src/serverde_impl.cpp
// Talon: ILTServer mixes virtual methods (CLTServer) with C function pointers (si_ functions,
// installed by si_SetupFunctionPointers). CLTServer reaches the server manager through its
// m_pServerMgr member; the si_ functions use g_pServerMgr.
// servermgr.h pulls in windows.h (lthread.h), which has to come before stdlith's type macros;
// keep the ILT method names windows.h would rename.
#include <windows.h>
#undef GetClassName
#undef CopyFile
#undef PlaySound
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <set>
#include <map>
#include <string>
#include "serverde_impl.h"
#include "stringmgr.h"
#include "dhashtable.h"
#include "concommand.h"
#include "animtracker.h"
#include "geomroutines.h"
#include "genericprop_setup.h"
#include "impl_common.h"
#include "packet.h"
#include "game_serialize.h"
#include "s_object.h"

class ServerAppHandler
{
public:
	virtual LTRESULT	ShellMessageFn(char *pMsg, uint32 nLen);
};

ILTStream*	streamsim_Open(const char *pFilename, const char *pAccess);
LTRESULT	sm_SetPortalFlags(CServerMgr *pServerMgr, const char *pPortalName, uint32 flags);
LTBOOL		ServerIntersectSegment(IntersectQuery *pQuery, IntersectInfo *pInfo);


LTBOOL PolyTouchesBox(WorldPoly *pPoly, void *pUnknown1, void *pUnknown2);	// 0x00416f10

#ifndef NETDRIVER_TCPIP
#define NETDRIVER_TCPIP	(1<<0)
#endif



ObjectList*	si_FindObjectsTouchingSphere(LTVector *pPosition, float radius);
void		si_RelinquishList(ObjectList *pList);



// Talon's server-side ILT interface implementations (the classes CLTServer embeds: SPhysicsLT,
// ServerCommonLT, ServerModelLT and ServerLightAnimLT; Jupiter split them into
// server_iltphysics.cpp, server_iltcommon.cpp and server_iltmodel.cpp), plus si_GetPointShade and
// CreateLTServer.
#include <string.h>
#include "bdefs.h"
#include "server_vars.h"
#include "servermgr.h"
#include "boxfind.h"
#include "s_object.h"
#include "serverde_impl.h"
#include "shared_iltcommon.h"
#include "iltmodel.h"
#include "iltphysics.h"
#include "iltlightanim.h"
#include "smoveabstract.h"
#include "moveobject.h"
#include "motion.h"
#include "impl_common.h"
#include "animtracker.h"
#include "packet.h"
#include "s_client.h"
#include "server_extradata.h"
#include "model.h"


void sm_SetLightAnimChanged(CServerMgr *pServerMgr, uint32 iLightAnim, uint32 flags);	// s_client, 0x00473550


// A world light (the MainWorld::m_StaticLights list). Talon layout, only what's used here.
struct StaticLight
{
	uint8		m_Pad00[0x68];		// WorldTreeObj, list link
	LTVector	m_Pos;				// 0x68
	float		m_Radius;			// 0x74
	LTVector	m_Color;			// 0x78 0-255
	LTVector	m_Dir;				// 0x84 Normalized direction vector for directional lights
	float		m_FOV;				// 0x90 cos(fov/2), -1 for omnidirectional lights
	LTVector	m_InnerColor;		// 0x94 color at the center of a directional light's cone
};

// Light table lookup result (clientde_impl.cpp has the same).
struct LTRGBColor
{
	uint8	b, g, r, a;
};

void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGBColor *pRGB);	// 0x00405980


// FUNCTION: LITHTECH 0x004795f0
LTBOOL si_GetPointShade(LTVector *pPoint, LTVector *pColor)
{
	LTRGBColor rgb;
	LTLink *pCur, *pListHead;
	StaticLight *pLight;
	LTVector vDelta;
	float fDist, fDot, t;

	if (!pColor || !pPoint)
		return LTFALSE;

	w_GetLightVal(&g_pServerMgr->m_World.m_LightTable, pPoint, &rgb);
	pColor->x = rgb.r;
	pColor->y = rgb.g;
	pColor->z = rgb.b;

	// Add the static lights.
	pListHead = &g_pServerMgr->m_World.m_StaticLights;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pLight = (StaticLight*)pCur->m_pData;

		vDelta = *pPoint - pLight->m_Pos;
		fDist = vDelta.Mag();
		if (fDist < pLight->m_Radius)
		{
			if (pLight->m_FOV > -1.0f && pLight->m_FOV < 1.0f && fDist != 0.0f)
			{
				// Directional light: fade from the inner color to the color at the edge of the cone.
				fDot = VEC_DOT(vDelta, pLight->m_Dir) * (1.0f / fDist);
				if (fDot >= pLight->m_FOV)
				{
					t = (1.0f - (fDot + 1.0f) * 0.5f) / (1.0f - (pLight->m_FOV + 1.0f) * 0.5f);
					*pColor += pLight->m_InnerColor * t + pLight->m_Color * (1.0f - t);
				}
			}
			else
			{
				*pColor += pLight->m_Color;
			}
		}
	}

	if (pColor->x > 255.0f)
		pColor->x = 255.0f;

	if (pColor->y > 255.0f)
		pColor->y = 255.0f;

	if (pColor->z > 255.0f)
		pColor->z = 255.0f;

	return LTTRUE;
}


// ----------------------------------------------------------------------- //
// The interface classes.
// ----------------------------------------------------------------------- //

// The explicit map comparator construction keeps _M_empty_initialize out of line, matching the original call.
// FUNCTION: LITHTECH 0x004798b0
ILTServer* CreateLTServer(CServerMgr *pServerMgr)
{
	CLTServer *pServer = new CLTServer(pServerMgr);
	if (pServer)
		si_SetupFunctionPointers(pServer);
	return pServer;
}

// FUNCTION: LITHTECH 0x004799f0 ??_GILTServer@@MAEPAXI@Z

// ----------------------------------------------------------------------- //
// SPhysicsLT
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00479a10
LTRESULT SPhysicsLT::SetVelocity(HOBJECT hObj, LTVector *pVel)
{
	if (!hObj || !pVel)
	{
		RETURN_ERROR(1, ILTPhysics::SetVelocity, LT_INVALIDPARAMS);
	}

	if (hObj->m_Velocity.DistSqr(*pVel) < 0.001f)
		return LT_OK;

	hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	hObj->m_Velocity = *pVel;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479b00
LTRESULT SPhysicsLT::SetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::SetAcceleration, LT_INVALIDPARAMS);
	}

	if (hObj->m_Acceleration.DistSqr(*pAccel) < 0.001f)
		return LT_OK;

	hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	hObj->m_Acceleration = *pAccel;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479bd0
LTRESULT SPhysicsLT::MoveObject(HOBJECT hObj, LTVector *pPos, uint32 flags)
{
	uint32 moveFlags;

	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::MoveObject, LT_INVALIDPARAMS);
	}

	moveFlags = MO_DETACHSTANDING | MO_SETCHANGEFLAG | MO_MOVESTANDINGONS;
	if (flags & MOVEOBJECT_TELEPORT)
		moveFlags |= MO_TELEPORT;

	if (flags & MOVEOBJECT_NCTELEPORT)
		moveFlags |= MO_NOSLIDING;

	FullMoveObject(m_pServerMgr, hObj, pPos, moveFlags);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479c50
LTRESULT SPhysicsLT::SetObjectDims(HOBJECT hObj, LTVector *pNewDims, uint32 flags)
{
	LTVector newDims;
	MoveState moveState;

	newDims = *pNewDims;

	if (hObj)
	{
		if (pNewDims->x > g_CV_DebugMaxDims || pNewDims->y > g_CV_DebugMaxDims || pNewDims->z > g_CV_DebugMaxDims)
		{
			dsi_ConsolePrint("ILTPhysics::SetObjectDims: dims larger than 'DebugMaxDims'");
			dsi_ConsolePrint("Object class: %s, dims (%.2f, %.2f, %.2f)", hObj->sd->m_pClass->m_ClassName,
				pNewDims->x, pNewDims->y, pNewDims->z);
		}

		// Not allowed to change a WorldModel's dimensions!
		if (hObj->m_ObjectType != OT_CONTAINER && hObj->m_ObjectType != OT_WORLDMODEL)
		{
			moveState.Setup(&m_pServerMgr->m_World.m_WorldTree, m_pServerMgr->m_MoveAbstract, hObj, hObj->m_BPriority);
			if (ChangeObjectDimensions(&moveState, &newDims, flags & SETDIMS_PUSHOBJECTS, LTTRUE))
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

	RETURN_ERROR(2, SPhysicsLT::SetObjectDims, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x00479df0
LTRESULT SPhysicsLT::GetGlobalForce(LTVector &vec)
{
	vec = m_pServerMgr->GetMotionState()->m_Info.m_Force;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479e20
LTRESULT SPhysicsLT::SetGlobalForce(LTVector &vec)
{
	MotionState *pState;

	pState = m_pServerMgr->GetMotionState();
	pState->m_Info.m_Force = vec;
	pState->m_Info.m_ForceMag = pState->m_Info.m_Force.Mag();
	if (pState->m_Info.m_ForceMag > 0.00001f)
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


// ----------------------------------------------------------------------- //
// ServerCommonLT
// ----------------------------------------------------------------------- //

// The poly an HPOLY refers to (world model index in the high word).
static inline WorldPoly* w_GetPolyFromHPoly(MainWorld *pWorld, HPOLY hPoly)
{
	uint32 iModel;
	WorldData *pWorldData;

	iModel = hPoly >> 16;
	if (iModel >= pWorld->m_WorldModels.GetSize())
		return LTNULL;

	pWorldData = pWorld->m_WorldModels[iModel];
	if (!pWorldData)
		return LTNULL;

	return pWorldData->m_pOriginalBsp->GetPolyFromHPoly(hPoly);
}


// These virtual member definitions stay emitted by their vtables. Marking them inline gives each
// FN_NAME static a COMDAT beside its string literal, matching the original data contribution order.
// FUNCTION: LITHTECH 0x00479ec0
inline LTRESULT ServerCommonLT::SetObjectFlags(HOBJECT hObj, const ObjFlagType flagType, uint32 dwFlags)
{
	FN_NAME(ServerCommonLT::SetObjectFlags);
	uint32 oldFlags;

	CHECK_PARAMS2(hObj);

	if (flagType == OFT_Flags)
	{
		if (hObj->m_Flags != dwFlags)
		{
			// They changed a FLAGS_.
			hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;

			// If we're going to nonsolid, get rid of anything standing on us.
			if ((hObj->m_Flags & FLAG_SOLID) && !(dwFlags & FLAG_SOLID))
			{
				DetachObjectStanding(hObj);
			}

			// Only tell clients if it changes a flag relevant to them.
			if ((hObj->m_Flags ^ dwFlags) & CLIENT_FLAGMASK)
			{
				SetObjectChangeFlags(m_pServerMgr, hObj, CF_FLAGS);
			}

			oldFlags = hObj->m_Flags;
			hObj->m_Flags = dwFlags;

			// If they turned on real world model physics, retransform the world model.
			if (hObj->HasWorldModel() && (oldFlags & FLAG_BOXPHYSICS) && !(dwFlags & FLAG_BOXPHYSICS))
			{
				RetransformWorldModel((WorldModelInstance*)hObj);
			}

			sm_UpdateInBspStatus(m_pServerMgr, hObj);
		}
	}
	else
	{
		if (hObj->m_Flags2 != dwFlags)
		{
			hObj->m_Flags2 = dwFlags;
			SetObjectChangeFlags(m_pServerMgr, hObj, CF_FLAGS);
		}
	}

	return LT_OK;
}


// Changes a model's or sprite's files and tells the clients that know about the object.
// FUNCTION: LITHTECH 0x0047a190
inline LTRESULT ServerCommonLT::SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct *pStruct)
{
	FN_NAME(ServerCommonLT::SetObjectFilenames);
	LTRESULT dResult;
	Model *pOldModel, *pNewModel;
	Attachment *pAttachment;
	uint32 newSocketIndex;
	LTLink *pCur, *pListHead;
	ExtraDataBackup backup;

	CHECK_PARAMS2(pStruct && pObj &&
		(pObj->m_ObjectType == OT_MODEL || pObj->m_ObjectType == OT_SPRITE));

	pOldModel = LTNULL;
	if (pObj->m_ObjectType == OT_MODEL)
		pOldModel = ((ModelInstance*)pObj)->GetModelDB();

	CPacketRef cChangePacket;
	cChangePacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Setup the new files (this terminates the old ones).
	BackupExtraData(pObj, &backup);
	sm_TermExtraData(m_pServerMgr, pObj);
	dResult = sm_InitExtraData(m_pServerMgr, pObj, pStruct);
	if (dResult != LT_OK)
	{
		RestoreExtraData(pObj, &backup);
		return dResult;
	}

	// Tell all the clients about it.
	cChangePacket->ResetWrite();
	cChangePacket->WriteType(pObj->m_ObjectID);
	sm_WriteModelFiles(pObj, cChangePacket, LTNULL);

	pListHead = &m_pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		Client *pClient = (Client*)pCur->m_pData;

		if (pClient->m_ObjInfos[pObj->m_ObjectID].m_ChangeFlags & CF_SENTINFO)
		{
			SendToClient(m_pServerMgr, pClient, 15, cChangePacket, LTFALSE, MESSAGE_GUARANTEED);
		}
	}

	// If we've changed models, then reorder the attachment socket node bindings.
	if (pOldModel)
	{
		pNewModel = ((ModelInstance*)pObj)->GetModelDB();

		for (pAttachment = pObj->m_Attachments; pAttachment; pAttachment = pAttachment->m_pNext)
		{
			if (pAttachment->m_iSocket < pOldModel->NumSockets())
			{
				if (pNewModel->FindSocket(pOldModel->GetSocket(pAttachment->m_iSocket)->m_Name, &newSocketIndex))
				{
					pAttachment->m_iSocket = newSocketIndex;
				}
			}
			// Look for it in the node list if it's not in the socket list.
			else if (pAttachment->m_iSocket < (pOldModel->NumSockets() + pOldModel->NumNodes()))
			{
				if (pNewModel->FindNode(pOldModel->GetNode(pAttachment->m_iSocket - pOldModel->NumSockets())->GetName(),
					&newSocketIndex))
				{
					pAttachment->m_iSocket = newSocketIndex + pNewModel->NumSockets();
				}
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047a3c0
LTRESULT ServerCommonLT::GetPointStatus(LTVector *pPoint)
{
	if (ic_IsPointInWorld(&m_pServerMgr->m_World.m_WorldTree, pPoint))
		return LT_INSIDE;
	else
		return LT_OUTSIDE;
}


// FUNCTION: LITHTECH 0x0047a3f0
LTRESULT ServerCommonLT::GetPointShade(LTVector *pPoint, LTVector *pColor)
{
	return si_GetPointShade(pPoint, pColor);
}


// FUNCTION: LITHTECH 0x0047a410
LTRESULT ServerCommonLT::GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags)
{
	WorldPoly *pPoly;

	*pFlags = 0;

	pPoly = w_GetPolyFromHPoly(&m_pServerMgr->m_World, hPoly);
	if (pPoly)
	{
		*pFlags = ((Surface*)pPoly->m_pSurface)->m_TextureFlags;
		return LT_OK;
	}

	RETURN_ERROR(2, CommonLT::GetPolyTextureFlags, LT_ERROR);
}


// FUNCTION: LITHTECH 0x0047a4a0
LTRESULT ServerCommonLT::GetPolyInfo(HPOLY hPoly, LTPlane **ppPlane, LTVector *pVertexList,
	uint32 nVertexListMaxSize, uint32 *pnNumVertices)
{
	WorldPoly *pPoly;
	uint32 nVertices, i;

	pPoly = w_GetPolyFromHPoly(&m_pServerMgr->m_World, hPoly);
	if (pPoly)
	{
		if (ppPlane)
			*ppPlane = pPoly->m_pPlane;

		nVertices = pPoly->m_nVertices;
		if (pVertexList)
		{
			for (i=0; i < nVertices && i < nVertexListMaxSize; i++)
			{
				*pVertexList = *((SPolyVertex*)(pPoly + 1))[i].m_Vec;
				pVertexList++;
			}
		}

		if (pnNumVertices)
			*pnNumVertices = nVertices;

		return LT_OK;
	}

	RETURN_ERROR(2, CommonLT::GetPolyInfo, LT_ERROR);
}


// FUNCTION: LITHTECH 0x0047a570
LTRESULT ServerCommonLT::GetPolySurfaceFlags(HPOLY hPoly, uint32 &dwSurfFlags)
{
	WorldPoly *pPoly;

	pPoly = w_GetPolyFromHPoly(&m_pServerMgr->m_World, hPoly);
	if (pPoly && pPoly->m_pSurface)
	{
		dwSurfFlags = ((Surface*)pPoly->m_pSurface)->m_Flags;
		return LT_OK;
	}

	RETURN_ERROR(2, CommonLT::GetPolySurfaceFlags, LT_ERROR);
}


// FUNCTION: LITHTECH 0x0047a600
LTRESULT ServerCommonLT::CreateMessage(ILTMessage* &pMsg)
{
	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047a620
inline LTRESULT ServerCommonLT::GetAttachmentObjects(HATTACHMENT hAttachment, HOBJECT &hParent, HOBJECT &hChild)
{
	FN_NAME(ServerCommonLT::GetAttachmentObjects);
	Attachment *pAttachment;

	pAttachment = (Attachment*)hAttachment;
	if (!pAttachment)
		ERR(1, LT_INVALIDPARAMS);

	hParent = sm_FindObject(m_pServerMgr, pAttachment->m_nParentID);
	hChild = sm_FindObject(m_pServerMgr, pAttachment->m_nChildID);

	if (hParent && hChild)
		return LT_OK;
	else
		ERR(1, LT_NOTINITIALIZED);
}


// ----------------------------------------------------------------------- //
// ServerModelLT
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0047a6f0
LTRESULT ServerModelLT::AddTracker(HOBJECT hObj, LTAnimTracker *pTracker)
{
	LTRESULT dResult;

	dResult = ILTModel::AddTracker(hObj, pTracker);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_MODELINFO);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0047a800
LTRESULT ServerModelLT::RemoveTracker(HOBJECT hObj, LTAnimTracker *pTracker)
{
	LTRESULT dResult;

	dResult = ILTModel::RemoveTracker(hObj, pTracker);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_MODELINFO);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0047a910
inline LTRESULT ServerModelLT::SetLooping(LTAnimTracker *pTracker, LTBOOL bLooping)
{
	FN_NAME(ServerModelLT::SetLooping);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetLooping(pTracker, bLooping);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047aa70
inline LTRESULT ServerModelLT::SetPlaying(LTAnimTracker *pTracker, LTBOOL bPlaying)
{
	FN_NAME(ServerModelLT::SetPlaying);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetPlaying(pTracker, bPlaying);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047abd0
inline LTRESULT ServerModelLT::SetCurAnimTime(LTAnimTracker *pTracker, uint32 curTime)
{
	FN_NAME(ServerModelLT::SetCurAnimTime);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetCurAnimTime(pTracker, curTime, LTTRUE);
		if (dResult == LT_OK)
		{
			trk_SetAtKeyFrame(pTracker, pTracker->m_TimeRef.m_Cur.m_Time);
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_RESETANIM);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047ad40
inline LTRESULT ServerModelLT::SetCurAnim(LTAnimTracker *pTracker, HMODELANIM hAnim)
{
	FN_NAME(ServerModelLT::SetCurAnim);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetCurAnim(pTracker, hAnim);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0047aea0
inline LTRESULT ServerModelLT::ResetAnim(LTAnimTracker *pTracker)
{
	FN_NAME(ServerModelLT::ResetAnim);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::ResetAnim(pTracker);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_RESETANIM);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047b000
inline LTRESULT ServerModelLT::SetWeightSet(LTAnimTracker *pTracker, HMODELWEIGHTSET hSet)
{
	FN_NAME(ServerModelLT::SetWeightSet);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetWeightSet(pTracker, hSet);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047b160
LTRESULT ServerModelLT::SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL bHidden)
{
	LTRESULT dResult;

	dResult = ILTModel::SetPieceHideStatus(hObj, hPiece, bHidden);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_ATTACHMENTS);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0047b280
inline LTRESULT ServerModelLT::SetAllowTransition(LTAnimTracker *pTracker, LTBOOL bAllowTransition)
{
	FN_NAME(ServerModelLT::SetAllowTransition);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetAllowTransition(pTracker, bAllowTransition);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047b3e0
inline LTRESULT ServerModelLT::SetTimeScale(LTAnimTracker *pTracker, LTFLOAT fTimeScale)
{
	FN_NAME(ServerModelLT::SetTimeScale);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetTimeScale(pTracker, fTimeScale);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// ----------------------------------------------------------------------- //
// ServerLightAnimLT
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0047b540
LTRESULT ServerLightAnimLT::FindLightAnim(const char *pName, HLIGHTANIM &hLightAnim)
{
	if (m_pServerMgr->m_World.FindLightAnim(pName, (uint32*)&hLightAnim))
	{
		return LT_OK;
	}
	else
	{
		hLightAnim = INVALID_LIGHT_ANIM;
		return LT_NOTFOUND;
	}
}


// FUNCTION: LITHTECH 0x0047b580
inline LTRESULT ServerLightAnimLT::GetNumFrames(HLIGHTANIM hLightAnim, uint32 &nFrames)
{
	FN_NAME(ServerLightAnimLT::GetNumFrames);

	nFrames = 0;
	CHECK_PARAMS2((uint32)hLightAnim < m_pServerMgr->m_World.m_LightAnims.GetSize());

	nFrames = m_pServerMgr->m_World.m_LightAnims[(uint32)hLightAnim].m_nFrames;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047b5f0
inline LTRESULT ServerLightAnimLT::GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info)
{
	FN_NAME(ServerLightAnimLT::GetLightAnimInfo);

	CHECK_PARAMS2((uint32)hLightAnim < m_pServerMgr->m_World.m_LightAnims.GetSize());

	la_GetInfo(&m_pServerMgr->m_World.m_LightAnims[(uint32)hLightAnim], &info);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047b660
inline LTRESULT ServerLightAnimLT::SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info)
{
	FN_NAME(ServerLightAnimLT::SetLightAnimInfo);
	LightAnim *pAnim;
	uint32 changed;
	LTBOOL bChanged;

	CHECK_PARAMS2((uint32)hLightAnim < m_pServerMgr->m_World.m_LightAnims.GetSize());

	pAnim = &m_pServerMgr->m_World.m_LightAnims[(uint32)hLightAnim];
	bChanged = la_InfoChanged(pAnim, &info, &changed);
	la_SetInfo(pAnim, &info);

	if (bChanged)
	{
		sm_SetLightAnimChanged(m_pServerMgr, (uint32)hLightAnim, changed);
	}

	return LT_OK;
}

// ----------------------------------------------------------------------- //
// CLTServer.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0047b710
inline LTRESULT CLTServer::GetNumClassProps(const HCLASS hClass, uint32 &count)
{
	FN_NAME(CLTServer::GetClassDef);

	count = 0;
	CHECK_PARAMS2(hClass);

	CClassData *pClassData = (CClassData*)hClass;

	// Include base classes.
	count = 0;
	ClassDef *pCurClass = pClassData->m_pClass;
	while (pCurClass)
	{
		count += (uint32)pCurClass->m_nProps;
		pCurClass = pCurClass->m_ParentClass;
	}

	return LT_OK;
}

#define MAX_CLASS_HEIRARCHY_LEN	256

// FUNCTION: LITHTECH 0x0047b790
inline LTRESULT CLTServer::GetClassProp(const HCLASS hClass, const uint32 iProp, ClassPropInfo &info)
{
	FN_NAME(CLTServer::GetClassDef);

	info.m_PropName[0] = 0;
	CHECK_PARAMS2(hClass);

	CClassData *pClassData = (CClassData*)hClass;

	// Get the heirarchy into an array.
	uint32 nClasses = 0;
	ClassDef *classes[MAX_CLASS_HEIRARCHY_LEN];
	for (ClassDef *pCurClass = pClassData->m_pClass; pCurClass; pCurClass = pCurClass->m_ParentClass)
	{
		classes[nClasses] = pCurClass;
		nClasses++;
	}

	// Go through our array and find the right property.
	uint32 iCurProp = 0;
	for (uint32 i = 0; i < nClasses; i++)
	{
		ClassDef *pClass = classes[nClasses - i - 1];

		// Is it in this class?
		if (iProp >= iCurProp && iProp < (iCurProp + pClass->m_nProps))
		{
			PropDef *pPropDef = &pClass->m_Props[iProp - iCurProp];
			strncpy(info.m_PropName, pPropDef->m_PropName, MAX_CLASSPROPINFONAME_LEN - 1);
			info.m_PropName[MAX_CLASSPROPINFONAME_LEN - 1] = 0;
			info.m_PropType = pPropDef->m_PropType;
		}

		iCurProp += (uint32)pClass->m_nProps;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047b890
inline LTRESULT CLTServer::GetClassName(const HCLASS hClass, char *pName, uint32 maxNameBytes)
{
	FN_NAME(CLTServer::GetClassName);

	CHECK_PARAMS2(hClass);
	CClassData *pClassData = (CClassData*)hClass;
	if (maxNameBytes)
	{
		strncpy(pName, pClassData->m_pClass->m_ClassName, maxNameBytes - 1);
		pName[maxNameBytes - 1] = 0;
	}
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047b900
LTRESULT CLTServer::GetClientPing(HCLIENT hClient, float &ping)
{
	CHECK_PARAMS(hClient, ILTPhysics::GetClientPing);

	Client *pClient = (Client*)hClient;
	if (pClient->m_ConnectionID)
	{
		ping = pClient->m_ConnectionID->m_Ping;
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(2, ILTPhysics::GetPing, LT_NOTINITIALIZED);
	}
}

// FUNCTION: LITHTECH 0x0047b9a0
LTRESULT CLTServer::GetNetFlags(HOBJECT hObj, uint32 &flags)
{
	CHECK_PARAMS(hObj, GetNetFlags);
	flags = hObj->sd->m_NetFlags;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047ba00
LTRESULT CLTServer::SetNetFlags(HOBJECT pObj, uint32 flags)
{
	CHECK_PARAMS(pObj, SetNetFlags);

	// Filter out unwanted ones.
	if (pObj->m_ObjectType != OT_MODEL)
		flags &= ~NETFLAG_ANIMUNGUARANTEED;

	pObj->sd->m_NetFlags = (uint16)flags;
	return LT_OK;
}

inline WorldData* GetWorldDataFromHPoly(CServerMgr *pServerMgr, HPOLY hPoly)
{
	uint32 iWorld = hPoly >> 16;
	if (iWorld < pServerMgr->m_World.m_WorldModels.GetSize())
		return pServerMgr->m_World.m_WorldModels[iWorld];

	return LTNULL;
}

// FUNCTION: LITHTECH 0x0047ba70
inline LTRESULT CLTServer::GetHPolyObject(const HPOLY hPoly, HOBJECT &hObject)
{
	FN_NAME(CLTServer::GetHPolyObject);

	CServerMgr *pServerMgr = m_pServerMgr;
	WorldData *pWorldData = GetWorldDataFromHPoly(pServerMgr, hPoly);
	if (!pWorldData)
	{
		RETURN_ERROR(5, CLTServer::GetHPolyObject, LT_NOTFOUND);
	}

	// LAME but unless it needs to get faster..  Look for a WorldModel
	// that uses this world data.
	LTLink *pListHead = &pServerMgr->m_ObjectMgr.m_ObjectLists[OT_WORLDMODEL].m_Head;
	for (LTLink *pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
	{
		WorldModelInstance *pInst = (WorldModelInstance*)pCur->m_pData;

		if (pInst->m_pOriginalBsp == pWorldData->m_pOriginalBsp)
		{
			hObject = pInst;
			return LT_OK;
		}
	}

	ERR(2, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0047bb40
inline LTRESULT CLTServer::FindNamedObjects(char *pName, BaseObjArray<HOBJECT> &objArray, uint32 *nTotalFound)
{
	FN_NAME(CLTServer::FindNamedObjects);

	uint32 tmpTotalFound = 0;
	CHECK_PARAMS2(pName);

	// Make sure array is clean...
	objArray.Reset();

	// Go thru the hash table and find all matches.
	uint32 keyLen = strlen(pName) + 1;
	HHashElement *hElement = hs_FindElement(g_pServerMgr->m_hNameTable, pName, keyLen);
	while (hElement)
	{
		LTObject *pObj = (LTObject*)hs_GetElementUserData(hElement);

		if (!(pObj->m_InternalFlags & IFLAG_OBJECTGOINGAWAY))
		{
			objArray.AddObject(pObj);
			tmpTotalFound++;
		}

		hElement = hs_FindNextElement(g_pServerMgr->m_hNameTable, hElement, pName, keyLen);
	}

	if (nTotalFound)
		*nTotalFound = tmpTotalFound;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047bc30
LTRESULT CLTServer::FindWorldModelObjectIntersections(HOBJECT hWorldModel,
	LTVector vNewPos, LTRotation rNewRot, BaseObjArray<HOBJECT> &objArray)
{
	FN_NAME(CLTServer::FindObjectIntersections);
	CHECK_PARAMS2(hWorldModel);

	// Make sure this is a world model...
	if (!hWorldModel->HasWorldModel())
	{
		RETURN_ERROR(1, ILTPhysics::FindWorldModelObjectIntersections, LT_ERROR);
	}

	// Update the world model's position/rotation, saving the old
	// position/rotation...
	WorldModelInstance *pWorldModel = (WorldModelInstance*)hWorldModel;

	LTVector vOldPos = pWorldModel->GetPos();
	LTRotation rOldRot = pWorldModel->m_Rotation;

	pWorldModel->SetPos(vNewPos);
	pWorldModel->m_Rotation = rNewRot;

	RetransformWorldModel(pWorldModel);

	// Find all the objects that intersect the world model in its new
	// position...
	float fRadius = pWorldModel->m_Radius;
	ObjectList *pList = si_FindObjectsTouchingSphere(&vNewPos, fRadius);
	if (pList)
	{
		ObjectLink *pLink = pList->m_pFirstLink;
		while (pLink)
		{
			LTObject *pTestObj = pLink->m_hObject;

			if (pTestObj && pTestObj != pWorldModel)
			{
				if (DoesBoxIntersectBSP(pWorldModel->m_pValidBsp->GetRootNode(),
					pTestObj->m_MinBox, pTestObj->m_MaxBox))
				{
					// If the object isn't going away, add it to the list...
					if (!(pTestObj->m_InternalFlags & IFLAG_OBJECTGOINGAWAY))
					{
						objArray.AddObject(pLink->m_hObject);
					}
				}
			}

			pLink = pLink->m_pNext;
		}

		si_RelinquishList(pList);
	}

	// Reset the world model's position/rotation...
	pWorldModel->SetPos(vOldPos);
	pWorldModel->m_Rotation = rOldRot;

	RetransformWorldModel(pWorldModel);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047be50
LTRESULT CLTServer::GetWorldBox(LTVector &min, LTVector &max)
{
	if (!m_pServerMgr->m_World.m_bLoaded)
	{
		RETURN_ERROR(2, ILTPhysics::GetWorldBox, LT_NOTINITIALIZED);
	}

	min = m_pServerMgr->m_World.m_BoxMin;
	max = m_pServerMgr->m_World.m_BoxMax;
	return LT_OK;
}

inline void CLTServer::SendFileIOMessage(uint8 fileType, uint8 msgID, uint16 fileID, LTBOOL bTellLocal)
{
	CPacketRef cPacket;

	// Tell the clients to do the same.
	LTLink *pListHead = &m_pServerMgr->m_Clients.m_Head;
	for (LTLink *pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
	{
		Client *pClient = (Client*)pCur->m_pData;

		// (local clients will just inherit the model)
		if (bTellLocal || !(pClient->m_ClientFlags & CFLAG_LOCAL))
		{
			cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			cPacket->WriteType(fileType);
			cPacket->WriteType(fileID);
			::SendToClient(m_pServerMgr, pClient, msgID, cPacket, LTFALSE, MESSAGE_GUARANTEED);
		}
	}
}

inline LTRESULT CLTServer::ThreadLoadTexture(char *pFilename)
{
	UsedFile *pUsedFile;

	if (sf_AddUsedFile(&m_pServerMgr->m_FileMgr, pFilename, 0, &pUsedFile) == 0)
	{
		RETURN_ERROR_PARAM(1, ThreadLoadTexture, LT_MISSINGFILE, pFilename);
	}

	SendFileIOMessage(FT_TEXTURE, SMSG_THREADLOAD, (uint16)pUsedFile->m_FileID, LTTRUE);
	return LT_OK;
}

inline LTRESULT CLTServer::UnloadTexture(char *pFilename)
{
	UsedFile *pUsedFile;

	if (sf_AddUsedFile(&m_pServerMgr->m_FileMgr, pFilename, 0, &pUsedFile) == 0)
	{
		RETURN_ERROR_PARAM(1, ThreadLoadTexture, LT_MISSINGFILE, pFilename);
	}

	SendFileIOMessage(FT_TEXTURE, SMSG_UNLOAD, (uint16)pUsedFile->m_FileID, LTTRUE);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047bee0
LTRESULT CLTServer::ThreadLoadFile(char *pFilename, uint32 type)
{
	CHECK_PARAMS(pFilename, ILTPhysics::ThreadLoadFile);

	if (type == FT_MODEL)
	{
		Model *pModel;
		UsedFile *pUsedFile;

		LTRESULT dResult = se_GetModel(m_pServerMgr, pFilename, &pModel, &pUsedFile, LTTRUE, 0);
		if (pUsedFile && (dResult == LT_OK || dResult == LT_INPROGRESS))
		{
			SendFileIOMessage(FT_MODEL, SMSG_THREADLOAD, (uint16)pUsedFile->m_FileID, LTFALSE);
		}

		return dResult;
	}
	else if (type == FT_TEXTURE)
	{
		return ThreadLoadTexture(pFilename);
	}
	else
	{
		RETURN_ERROR(1, ILTPhysics::ThreadLoadFile, LT_UNSUPPORTED);
	}
}

// FRAGILE: two reloads after SendToClient swap order depending on unrelated declarations (e.g.
// CountPercent in counter.h, the STLport includes this unit now has, other agents' edits to shared headers):
// symbol-table noise in VC6's register allocator, not a source difference. It matches again in wave 5 (after the
// CLTServer/CServerMgr class changes); if it flips back, nothing in this function needs to change.
// FUNCTION: LITHTECH 0x0047c1c0
LTRESULT CLTServer::UnloadFile(char *pFilename, uint32 type)
{
	CHECK_PARAMS(pFilename, ILTPhysics::UnloadFile);

	if (type == FT_MODEL)
	{
		UsedFile *pUsedFile;

		if (sf_AddUsedFile(&m_pServerMgr->m_FileMgr, pFilename, 0, &pUsedFile) == 0)
		{
			RETURN_ERROR_PARAM(1, ThreadLoadTexture, LT_MISSINGFILE, pFilename);
		}

		LTRESULT dResult = se_UncacheModel(m_pServerMgr, pFilename, pUsedFile);
		if (dResult != LT_OK)
			return dResult;

		SendFileIOMessage(FT_MODEL, SMSG_UNLOAD, (uint16)pUsedFile->m_FileID, LTFALSE);
		return LT_OK;
	}
	else if (type == FT_TEXTURE)
	{
		return UnloadTexture(pFilename);
	}
	else
	{
		RETURN_ERROR(1, ILTPhysics::UnloadFile, LT_UNSUPPORTED);
	}
}

// FUNCTION: LITHTECH 0x0047c4b0
inline LTRESULT CLTServer::OpenFile(char *pFilename, ILTStream **pStream)
{
	FN_NAME(ILTPhysics::OpenFile);

	if (pFilename && pStream)
	{
		*pStream = sf_OpenFile(&m_pServerMgr->m_FileMgr, pFilename);
		if (*pStream)
		{
			return LT_OK;
		}
		else
		{
			ERR(2, LT_MISSINGFILE);
		}
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x0047c560
LTRESULT CLTServer::CopyFile(const char *pszSourceFile, const char *pszDestFile)
{
	return sf_CopyFile(&m_pServerMgr->m_FileMgr, pszSourceFile, pszDestFile);
}

// FUNCTION: LITHTECH 0x0047c590
void CLTServer::SetModelAnimation(HOBJECT hObj, HMODELANIM hAnim)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
	{
		LTAnimTracker *pTracker = &pInst->m_AnimTracker;
		if (hAnim != pTracker->m_TimeRef.m_Cur.m_iAnim)
		{
			trk_SetCurAnim(pTracker, hAnim, !!(pInst->m_Flags & FLAG_ANIMTRANSITION));
			SetObjectChangeFlags(g_pServerMgr, pInst, CF_MODELINFO);
		}
	}
}

// FUNCTION: LITHTECH 0x0047c6c0
HMODELANIM CLTServer::GetModelAnimation(HOBJECT hObj)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
		return pInst->m_AnimTracker.m_TimeRef.m_Cur.m_iAnim;
	else
		return (HMODELANIM)-1;
}

// FUNCTION: LITHTECH 0x0047c6f0
void CLTServer::SetModelLooping(HOBJECT hObj, LTBOOL bLoop)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
	{
		LTAnimTracker *pTracker = &pInst->m_AnimTracker;
		if (!!(pTracker->m_Flags & AT_LOOPING) != !!bLoop)
		{
			if (bLoop)
				pTracker->m_Flags |= AT_LOOPING;
			else
				pTracker->m_Flags &= ~AT_LOOPING;

			SetObjectChangeFlags(g_pServerMgr, pInst, CF_MODELINFO);
		}
	}
}

// FUNCTION: LITHTECH 0x0047c830
LTBOOL CLTServer::GetModelLooping(HOBJECT hObj)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
		return !!(pInst->m_AnimTracker.m_Flags & AT_LOOPING);
	else
		return LTFALSE;
}

// FUNCTION: LITHTECH 0x0047c850
LTRESULT CLTServer::ResetModelAnimation(HOBJECT hObj)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (!pInst || pInst->m_ObjectType != OT_MODEL)
	{
		RETURN_ERROR(1, ILTServer::ResetModelAnimation, LT_ERROR);
	}

	trk_Reset(&pInst->m_AnimTracker);
	SetObjectChangeFlags(g_pServerMgr, pInst, CF_RESETANIM);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047c9b0
uint32 CLTServer::GetModelPlaybackState(HOBJECT hObj)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
	{
		if (trk_IsStopped(&pInst->m_AnimTracker))
			return MS_PLAYDONE;
	}

	return 0;
}

// FUNCTION: LITHTECH 0x0047c9e0
HMODELANIM CLTServer::GetAnimIndex(HOBJECT hObj, char *pAnimName)
{
	return ic_GetAnimIndex(hObj, pAnimName);
}

// FUNCTION: LITHTECH 0x0047ca00
void CLTServer::CPrint(char *pMsg, ...)
{
	va_list marker;
	char msg[500];

	va_start(marker, pMsg);
	_vsnprintf(msg, 499, pMsg, marker);
	va_end(marker);

	dsi_ConsolePrint(msg);
}

struct GPCStruct
{
	HOBJECT		*m_pList;
	uint32		m_MaxListSize;
	uint32		m_CurListSize;
	LTVector	m_Point;
};

void GPCCallback(WorldTreeObj *pObj, void *pUser);

// FUNCTION: LITHTECH 0x0047ca40
uint32 CLTServer::GetPointContainers(LTVector *pPoint, HOBJECT *pList, uint32 maxListSize)
{
	GPCStruct theStruct;

	theStruct.m_pList = pList;
	theStruct.m_MaxListSize = maxListSize;
	theStruct.m_CurListSize = 0;
	theStruct.m_Point = *pPoint;

	g_pServerMgr->m_World.m_WorldTree.FindObjectsOnPoint(pPoint, GPCCallback, &theStruct, NOA_Objects);

	return theStruct.m_CurListSize;
}

// FUNCTION: LITHTECH 0x0047caa0
LTBOOL CLTServer::GetContainerCode(HOBJECT hObj, uint16 *pCode)
{
	if (!hObj)
		return LTFALSE;

	if (hObj->m_ObjectType != OT_CONTAINER)
		return LTFALSE;

	*pCode = ((ContainerInstance*)hObj)->m_ContainerCode;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0047cad0
HSTRING CLTServer::FormatString(int messageCode, ...)
{
	va_list marker;
	uint8 *pBuf;
	int bufferLen;
	HSTRING ret;

	if (g_pServerMgr->m_ClassMgr.m_hServerResourceModule)
	{
		va_start(marker, messageCode);
		pBuf = str_FormatString(g_pServerMgr->m_ClassMgr.m_hServerResourceModule,
			messageCode, &marker, &bufferLen);
		va_end(marker);

		if (pBuf)
		{
			ret = str_CreateString(pBuf);
			str_FreeStringBuffer(pBuf);
			return ret;
		}
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x0047cb40
HSTRING CLTServer::CopyString(HSTRING hString)
{
	return str_CopyString(hString);
}

// FUNCTION: LITHTECH 0x0047cb50
HSTRING CLTServer::CreateString(char *pString)
{
	return str_CreateStringAnsi(pString);
}

// FUNCTION: LITHTECH 0x0047cb60
void CLTServer::FreeString(HSTRING hString)
{
	ic_FreeString(hString);
}

// FUNCTION: LITHTECH 0x0047cb70
LTBOOL CLTServer::CompareStrings(HSTRING hString1, HSTRING hString2)
{
	return str_CompareStrings(hString1, hString2);
}

// FUNCTION: LITHTECH 0x0047cb90
LTBOOL CLTServer::CompareStringsUpper(HSTRING hString1, HSTRING hString2)
{
	return str_CompareStringsUpper(hString1, hString2);
}

// FUNCTION: LITHTECH 0x0047cbb0
char* CLTServer::GetStringData(HSTRING hString)
{
	return str_GetStringData(hString);
}

// FUNCTION: LITHTECH 0x0047cbc0
float CLTServer::GetVarValueFloat(HCONSOLEVAR hVar)
{
	if (!hVar)
		return 0.0f;

	return ((LTCommandVar*)hVar)->floatVal;
}

// FUNCTION: LITHTECH 0x0047cbe0
char* CLTServer::GetVarValueString(HCONSOLEVAR hVar)
{
	if (!hVar)
		return LTNULL;

	return ((LTCommandVar*)hVar)->pStringVal;
}

// FUNCTION: LITHTECH 0x0047cc00
LTFLOAT CLTServer::GetTime()
{
	return g_pServerMgr->m_GameTime;
}

// FUNCTION: LITHTECH 0x0047cc10
LTFLOAT CLTServer::GetFrameTime()
{
	return g_pServerMgr->m_FrameTime;
}

// FUNCTION: LITHTECH 0x0047cc20
LTRESULT CLTServer::RemoveObject(HOBJECT hObj)
{
	if (!hObj)
		return LT_ERROR;

	AddObjectToRemoveList(g_pServerMgr, hObj);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047cc50
LTRESULT CLTServer::FreeUnusedModels()
{
	return m_pServerMgr->FreeUnusedModels();
}

// FUNCTION: LITHTECH 0x0047cc60
LTRESULT CLTServer::SendTo(const void *pData, uint32 len, const char *sAddr, uint32 port)
{
	for (uint16 i = 0; i < m_pServerMgr->m_NetMgr.m_Drivers.GetSize(); i++)
	{
		CBaseDriver *pDriver = m_pServerMgr->m_NetMgr.m_Drivers[i];
		if (pDriver->m_DriverFlags & NETDRIVER_TCPIP)
			return pDriver->SendTcpIp((void*)pData, len, (char*)sAddr, port);
	}

	return LT_NOTINITIALIZED;
}

// FUNCTION: LITHTECH 0x0047ccb0
LTRESULT CLTServer::GetClientData(HCLIENT hClient, void *&pData, uint32 &nLength)
{
	CHECK_PARAMS(hClient, ILTServer::GetClientData);

	Client *pClient = (Client*)hClient;
	pData = pClient->m_pClientData;
	nLength = pClient->m_ClientDataLen;
	return LT_OK;
}

// The child model link map at 0x280 (CLTServer::m_ChildModelLinks) is an STLport map: child model
// filename -> set of extra child model filenames (model_load.cpp reads it as pExtraChildModels).

// FUNCTION: LITHTECH 0x0047cd10
void CLTServer::LinkModelToExtraChildModel(char *child_model_key, char **associated_chmdl, int size_chmld)
{
	ExtraChildSet &setChildren = m_ChildModelLinks[std::string(child_model_key)];

	for(int i=0; i < size_chmld; i++)
		setChildren.insert(std::string(associated_chmdl[i]));
}

// FUNCTION: LITHTECH 0x0047ce60
void CLTServer::ResetModelToChildModelLink()
{
	ExtraChildMap &mapLinks = m_ChildModelLinks;
	mapLinks.clear();
}

// FUNCTION: LITHTECH 0x0047ced0
void* CLTServer::GetChildModelLinkMap()
{
	return &m_ChildModelLinks;
}

// FUNCTION: LITHTECH 0x0047cee0
LTRESULT CLTServer::SetModelFilenames(HOBJECT hObj, char *pFilename, char *pSkinName)
{
	return SetObjectFilenames(hObj, pFilename, pSkinName);
}

// SAFE_STRCPY (the SDK's inline LTStrCpy) and Common() are the three pending inline sites that keep the
// constructor's Clear() out of line while the explicit Clear() is inlined.
// FUNCTION: LITHTECH 0x0047cef0
LTRESULT CLTServer::SetObjectFilenames(HOBJECT hObj, char *pFilename, char *pSkinName)
{
	ObjectCreateStruct theStruct;

	INIT_OBJECTCREATESTRUCT(theStruct);
	SAFE_STRCPY(theStruct.m_Filename, pFilename);
	SAFE_STRCPY(theStruct.m_SkinName, pSkinName);

	return Common()->SetObjectFilenames(hObj, &theStruct);
}

// FUNCTION: LITHTECH 0x0047d010
float CLTServer::GetForceIgnoreLimit(HOBJECT hObj, float &limit)
{
	float ret = 0.0f;
	m_pPhysicsLT->GetForceIgnoreLimit(hObj, ret);
	return ret;
}

// FUNCTION: LITHTECH 0x0047d040
void CLTServer::SetForceIgnoreLimit(HOBJECT hObj, float limit)
{
	m_pPhysicsLT->SetForceIgnoreLimit(hObj, limit);
}

// FUNCTION: LITHTECH 0x0047d050
LTRESULT CLTServer::GetVelocity(HOBJECT hObj, LTVector *pVel)
{
	return m_pPhysicsLT->GetVelocity(hObj, pVel);
}

// FUNCTION: LITHTECH 0x0047d060
LTRESULT CLTServer::SetVelocity(HOBJECT hObj, LTVector *pVel)
{
	return m_pPhysicsLT->SetVelocity(hObj, pVel);
}

// FUNCTION: LITHTECH 0x0047d070
LTRESULT CLTServer::GetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	return m_pPhysicsLT->GetAcceleration(hObj, pAccel);
}

// FUNCTION: LITHTECH 0x0047d080
LTRESULT CLTServer::SetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	return m_pPhysicsLT->SetAcceleration(hObj, pAccel);
}

// FUNCTION: LITHTECH 0x0047d090
float CLTServer::GetObjectMass(HOBJECT hObj)
{
	float mass;
	m_pPhysicsLT->GetObjectMass(hObj, mass);
	return mass;
}

// FUNCTION: LITHTECH 0x0047d0b0
void CLTServer::SetObjectMass(HOBJECT hObj, float mass)
{
	m_pPhysicsLT->SetObjectMass(hObj, mass);
}

// FUNCTION: LITHTECH 0x0047d0c0
LTRESULT CLTServer::MoveObject(HOBJECT hObj, LTVector *pNewPos)
{
	return m_pPhysicsLT->MoveObject(hObj, pNewPos, 0);
}

// FUNCTION: LITHTECH 0x0047d0e0
LTRESULT CLTServer::GetStandingOn(HOBJECT hObj, CollisionInfo *pInfo)
{
	return m_pPhysicsLT->GetStandingOn(hObj, pInfo);
}

// FUNCTION: LITHTECH 0x0047d0f0
void CLTServer::GetObjectDims(HOBJECT hObj, LTVector *pNewDims)
{
	m_pPhysicsLT->GetObjectDims(hObj, pNewDims);
}

// FUNCTION: LITHTECH 0x0047d100
LTRESULT CLTServer::SetObjectDims(HOBJECT hObj, LTVector *pNewDims)
{
	return m_pPhysicsLT->SetObjectDims(hObj, pNewDims, 0);
}

// FUNCTION: LITHTECH 0x0047d120
LTRESULT CLTServer::SetObjectDims2(HOBJECT hObj, LTVector *pNewDims)
{
	return m_pPhysicsLT->SetObjectDims(hObj, pNewDims, SETDIMS_PUSHOBJECTS);
}

// FUNCTION: LITHTECH 0x0047d140
uint32 CLTServer::GetObjectFlags(HOBJECT hObj)
{
	uint32 flags = 0;
	m_pCommonLT->GetObjectFlags(hObj, OFT_Flags, flags);
	return flags;
}

// FUNCTION: LITHTECH 0x0047d170
LTRESULT CLTServer::GetGlobalForce(LTVector *pVec)
{
	CHECK_PARAMS(pVec, ILTPhysics::GetGlobalForce);
	return m_pPhysicsLT->GetGlobalForce(*pVec);
}

// FUNCTION: LITHTECH 0x0047d1c0
LTRESULT CLTServer::SetGlobalForce(LTVector *pVec)
{
	CHECK_PARAMS(pVec, ILTPhysics::SetGlobalForce);
	return m_pPhysicsLT->SetGlobalForce(*pVec);
}

// FUNCTION: LITHTECH 0x0047d210
LTRESULT CLTServer::SetFrictionCoefficient(HOBJECT hObj, float coeff)
{
	return m_pPhysicsLT->SetFrictionCoefficient(hObj, coeff);
}

// FUNCTION: LITHTECH 0x0047d220 ?GetObjectName@CLTServer@@UAEPADPAVLTObject@@@Z
char* CLTServer::GetObjectName(HOBJECT hObject)
{
	if (hObject && hObject->sd->m_hName)
		return (char*)hs_GetElementKey(hObject->sd->m_hName, LTNULL);

	return g_EmptyString;
}

// FUNCTION: LITHTECH 0x0047d250 ?GetObjectName@CLTServer@@UAEKPAVLTObject@@PADK@Z
inline LTRESULT CLTServer::GetObjectName(HOBJECT hObject, char *pName, uint32 nameBufSize)
{
	FN_NAME(ILTPhysics::GetObjectName);

	CHECK_PARAMS2(hObject && pName && nameBufSize > 0);

	if (hObject->sd->m_hName)
	{
		char *pObjName = (char*)hs_GetElementKey(hObject->sd->m_hName, LTNULL);
		strncpy(pName, pObjName, nameBufSize - 1);
		pName[nameBufSize - 1] = 0;
	}
	else
	{
		pName[0] = 0;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047d2e0
LTRESULT CLTServer::GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags)
{
	return m_pCommonLT->GetPolyTextureFlags(hPoly, pFlags);
}

// FUNCTION: LITHTECH 0x0047d2f0
LTRESULT CLTServer::GetModelAnimUserDims(HOBJECT hObj, LTVector *pDims, HMODELANIM hAnim)
{
	return m_pCommonLT->GetModelAnimUserDims(hObj, pDims, hAnim);
}

// FUNCTION: LITHTECH 0x0047d300
HMESSAGEWRITE CLTServer::StartMessageToObject(LPBASECLASS pSender, HOBJECT hSendTo, uint32 messageID)
{
	LMessageImpl *pMsg;

	if (!hSendTo)
		RETURN_ERROR(2, ILTPhysics::StartMessageToObject, LTNULL);

	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	pMsg->m_MsgType = MSGTYPE_OBJECT;
	pMsg->m_hSender = pSender ? pSender->m_hObject : LTNULL;
	pMsg->m_hObject = hSendTo;
	pMsg->m_MsgID = messageID;
	return pMsg;
}

// FUNCTION: LITHTECH 0x0047d390
HMESSAGEWRITE CLTServer::StartMessage(HCLIENT hSendTo, uint8 messageID)
{
	LMessageImpl *pMsg;

	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	pMsg->m_MsgType = MSGTYPE_CLIENT;
	pMsg->m_pClient = (Client*)hSendTo;
	pMsg->m_MsgID = messageID;
	return pMsg;
}

// FUNCTION: LITHTECH 0x0047d3c0
LTRESULT CLTServer::StartMessageToServer(LPBASECLASS pSender, uint32 messageID, HMESSAGEWRITE *hWrite)
{
	LMessageImpl *pMsg;

	if (!pSender || !hWrite)
		RETURN_ERROR(2, ILTPhysics::StartMessageToServer, LT_INVALIDPARAMS);

	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	pMsg->m_MsgType = MSGTYPE_SERVER;
	pMsg->m_MsgID = messageID;
	pMsg->m_hSender = pSender->m_hObject;
	*hWrite = pMsg;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047d440
HMESSAGEWRITE CLTServer::StartInstantSpecialEffectMessage(LTVector *pPos)
{
	LMessageImpl *pMsg;

	if (!pPos)
		RETURN_ERROR(2, ILTPhysics::StartInstantSpecialEffectMessage, LTNULL);

	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	pMsg->m_MsgType = MSGTYPE_INSTANTSFX;
	pMsg->m_Pos = *pPos;
	return pMsg;
}

// FUNCTION: LITHTECH 0x0047d4b0
HMESSAGEWRITE CLTServer::StartSpecialEffectMessage(LPBASECLASS pObject)
{
	LMessageImpl *pMsg;

	if (!pObject)
		RETURN_ERROR(2, ILTPhysics::StartSpecialEffectMessage, LTNULL);

	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	pMsg->m_MsgType = MSGTYPE_SFX;
	pMsg->m_hObject = pObject->m_hObject;
	return pMsg;
}

// FUNCTION: LITHTECH 0x0047d510
HMESSAGEWRITE CLTServer::StartHMessageWrite()
{
	CPacket *pPacket;

	pPacket = m_pServerMgr->AllocPacket();
	pPacket->Init(8192, MAX_PACKET_LEN);
	return pPacket->GetMessageImpl();
}

// FUNCTION: LITHTECH 0x0047d5a0
inline LTRESULT CLTServer::EndMessage2(HMESSAGEWRITE hMessage, uint32 flags)
{
	FN_NAME(CLTServer::EndMessage2);
	LMessageImpl *pMsg = (LMessageImpl*)hMessage;
	LTRESULT dResult;

	CHECK_PARAMS2(pMsg && !pMsg->IsInvalid());

	switch (pMsg->m_MsgType)
	{
		case MSGTYPE_CLIENT:
		{
			dResult = SendToClient(*pMsg, (uint8)pMsg->m_MsgID, (HCLIENT)pMsg->m_pClient, flags);
			pMsg->Release();
			return dResult;
		}

		case MSGTYPE_OBJECT:
		{
			dResult = SendToObject(*pMsg, pMsg->m_MsgID, pMsg->m_hSender, pMsg->m_hObject, flags);
			pMsg->Release();
			return dResult;
		}

		case MSGTYPE_SFX:
		{
			dResult = SetObjectSFXMessage(pMsg->m_hObject, *pMsg);
			pMsg->Release();
			return dResult;
		}

		case MSGTYPE_INSTANTSFX:
		{
			SendSFXMessage(*pMsg, pMsg->m_Pos, flags);
			pMsg->Release();
			return LT_OK;
		}

		case MSGTYPE_SERVER:
		{
			SendToServer(*pMsg, pMsg->m_MsgID, pMsg->m_hSender, flags);
			pMsg->Release();
			return LT_OK;
		}

		default:
		{
			RETURN_ERROR(1, EndMessage, LT_ERROR);
		}
	}
}

// FUNCTION: LITHTECH 0x0047d720
LTRESULT CLTServer::EndMessage(HMESSAGEWRITE hMessage)
{
	return EndMessage2(hMessage, MESSAGE_GUARANTEED);
}

// FUNCTION: LITHTECH 0x0047d740
inline LTRESULT CLTServer::SendToClient(ILTMessage &msg, uint8 msgID, HCLIENT hSendTo, uint32 flags)
{
	FN_NAME(CLTServer::SendToClient);
	LMessageImpl *pMsg = (LMessageImpl*)&msg;

	CHECK_PARAMS2(pMsg && !pMsg->IsInvalid());

	// The message ID goes on the end.
	pMsg->m_pPacket->WriteType(msgID);

	if (hSendTo)
	{
		sm_SendToClient(m_pServerMgr, (Client*)hSendTo, SMSG_MESSAGE, pMsg->m_pPacket, flags);
	}
	else
	{
		for (LTLink *pCur = m_pServerMgr->m_Clients.m_Head.m_pNext; pCur != &m_pServerMgr->m_Clients.m_Head; pCur = pCur->m_pNext)
			sm_SendToClient(m_pServerMgr, (Client*)pCur->m_pData, SMSG_MESSAGE, pMsg->m_pPacket, flags);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047d980
LTRESULT CLTServer::SendToObject(ILTMessage &msg, uint32 msgID, HOBJECT hSender, HOBJECT hSendTo, uint32 flags)
{
	LMessageImpl *pMsg = (LMessageImpl*)&msg;

	if (!hSendTo || pMsg->IsInvalid())
		RETURN_ERROR(2, ILTPhysics::SendToObject, LT_INVALIDPARAMS);

	// Rewind it so the object reads from the start.
	pMsg->m_pPacket->m_Pos = 1;
	pMsg->m_MsgID = msgID;
	hSendTo->sd->m_pObject->ObjectMessageFn(hSender, msgID, pMsg);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047da10
inline LTRESULT CLTServer::SetObjectSFXMessage(HOBJECT hObject, ILTMessage &msg)
{
	FN_NAME(CLTServer::SetObjectSFXMessage);
	LMessageImpl *pMsg = (LMessageImpl*)&msg;

	CHECK_PARAMS2(pMsg && !pMsg->IsInvalid());

	sm_SetObjectSpecialEffectMessage(m_pServerMgr, hObject, pMsg->m_pPacket);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047da90
inline LTRESULT CLTServer::SendSFXMessage(ILTMessage &msg, LTVector &pos, uint32 flags)
{
	FN_NAME(CLTServer::SendSFXMessage);
	LMessageImpl *pMsg = (LMessageImpl*)&msg;

	CHECK_PARAMS2(pMsg && !pMsg->IsInvalid());

	sm_SendSFXMessage(m_pServerMgr, SMSG_SFXMESSAGE, pMsg->m_pPacket, LTNULL, &pos, flags);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047db10
inline LTRESULT CLTServer::SendToServer(ILTMessage &msg, uint32 msgID, HOBJECT hSender, uint32 flags)
{
	FN_NAME(CLTServer::SendToServer);
	LMessageImpl *pMsg = (LMessageImpl*)&msg;
	IServerShell *pShell;

	CHECK_PARAMS2(pMsg && !pMsg->IsInvalid());

	pShell = g_pServerMgr->m_ClassMgr.m_pServerShell;
	if (pShell)
	{
		pMsg->m_pPacket->m_Pos = 1;
		pShell->OnObjectMessage(pMsg->m_hSender->sd->m_pObject, pMsg->m_MsgID, pMsg);
		return LT_OK;
	}

	RETURN_ERROR(2, ILTPhysics::SendToServer, LT_NOTINITIALIZED);
}


// ----------------------------------------------------------------------- //
// si_ functions.
// ----------------------------------------------------------------------- //


// Define these after the virtual methods so VC6 emits the deleting destructor here.
inline CLTServer::CLTServer(CServerMgr *pServerMgr)
	: m_ModelLT(pServerMgr), m_PhysicsLT(pServerMgr), m_LightAnimLT(pServerMgr), m_ChildModelLinks(ExtraChildMap::key_compare())
{
	m_pServerMgr = pServerMgr;
	m_pCommonLT = &m_CommonLT;
	m_pModelLT = &m_ModelLT;
	m_pTransformLT = &m_TransformLT;
	m_pPhysicsLT = &m_PhysicsLT;
	m_pLightAnimLT = &m_LightAnimLT;
	m_pSoundMgr = pServerMgr;

	m_CommonLT.SetMathLT(&m_MathLT);
	m_CommonLT.m_pServerMgr = pServerMgr;
	m_CommonLT.m_pTransformLT = &m_TransformLT;
	m_CommonLT.m_pModelLT = &m_ModelLT;
}
// FUNCTION: LITHTECH 0x0047dbe0 ??_GCLTServer@@UAEPAXI@Z
inline CLTServer::~CLTServer() {}
// FindPoliesTouchingBox's search state.
struct BoxPolyStruct
{
	HPOLY			*m_pPolyList;		// 0x00
	uint32			m_nPolyListSize;	// 0x04
	uint32			m_nPoliesFound;		// 0x08
	ObjectFilterFn	m_FilterFn;			// 0x0c
	void			*m_pUserData;		// 0x10
	LTVector		m_Center;			// 0x14
	float			m_Radius;			// 0x20
};

void FindPoliesInBsp(Node *pRoot, BoxPolyStruct *pStruct, WorldBsp *pBsp);

// FUNCTION: LITHTECH 0x0047dc70
void BoxPolyFindCallback(WorldTreeObj *pObj, void *pUser)
{
	if (pObj->GetObjType() != WTObj_DObject)
		return;

	WorldModelInstance *pWorldModel = (WorldModelInstance*)pObj;
	if (!pWorldModel->HasWorldModel())
		return;

	BoxPolyStruct *pStruct = (BoxPolyStruct*)pUser;
	if (pStruct->m_FilterFn && !pStruct->m_FilterFn(pWorldModel, pStruct->m_pUserData))
		return;

	FindPoliesInBsp(pWorldModel->m_pValidBsp->GetRootNode(), pStruct, pWorldModel->m_pValidBsp);
}

// FUNCTION: LITHTECH 0x0047dcc0
void FindPoliesInBsp(Node *pNode, BoxPolyStruct *pStruct, WorldBsp *pBsp)
{
	Node **pStackPos = g_NodeStack;

	for (;;)
	{
		if (pNode->m_Flags & (NF_IN | NF_OUT))
		{
			if (pStackPos == g_NodeStack)
				return;

			pNode = *(--pStackPos);
		}

		WorldPoly *pPoly = pNode->m_pPoly;
		float dist = pNode->GetPlane()->DistTo(pStruct->m_Center);
		if (dist > pStruct->m_Radius)
		{
			pNode = pNode->m_Sides[1];
			continue;
		}

		if (dist < -pStruct->m_Radius)
		{
			pNode = pNode->m_Sides[0];
			continue;
		}

		// The node straddles the box sphere: test its poly.
		if (pPoly && PolyTouchesBox(pPoly, LTNULL, LTNULL))
		{
			HPOLY hPoly = pBsp->MakeHPoly(pNode);

			// Add it if it's not in the list yet.
			HPOLY *pCur;
			for (pCur = pStruct->m_pPolyList; pCur < &pStruct->m_pPolyList[pStruct->m_nPolyListSize]; pCur++)
			{
				if (pCur >= &pStruct->m_pPolyList[pStruct->m_nPoliesFound] || *pCur == hPoly)
					break;
			}

			if (pCur >= &pStruct->m_pPolyList[pStruct->m_nPoliesFound])
			{
				if (pStruct->m_pPolyList && pStruct->m_nPoliesFound < pStruct->m_nPolyListSize)
					pStruct->m_pPolyList[pStruct->m_nPoliesFound] = hPoly;

				pStruct->m_nPoliesFound++;
			}
		}

		// Do the back side later, the front side now.
		if (!(pNode->m_Sides[0]->m_Flags & (NF_IN | NF_OUT)))
		{
			*pStackPos = pNode->m_Sides[0];
			pStackPos++;
		}

		pNode = pNode->m_Sides[1];
	}
}

struct SphereFindStruct
{
	ObjectList	*m_pSphereTouchList;	// 0x00
	LTVector	*m_pSphereTouchPos;		// 0x04
	float		m_SphereTouchRadius;	// 0x08
	float		m_SphereTouchRadiusSqr;	// 0x0c
	float		m_SphereTouchDiameter;	// 0x10
};

// FUNCTION: LITHTECH 0x0047de10
void SphereFindCallback(WorldTreeObj *pObj, void *pCBUser)
{
	if (pObj->GetObjType() != WTObj_DObject)
		return;

	SphereFindStruct *pStruct = (SphereFindStruct*)pCBUser;
	LTObject *pServerObj = (LTObject*)pObj;

	// (r1 + r2)^2, expanded.
	LTVector vecTo = pServerObj->GetPos() - *pStruct->m_pSphereTouchPos;
	float fRadius = pServerObj->m_Radius;
	float fDistSqr = vecTo.MagSqr();
	if (fDistSqr < pServerObj->m_Radius * pServerObj->m_Radius + fRadius * pStruct->m_SphereTouchDiameter +
		pStruct->m_SphereTouchRadiusSqr)
	{
		ObjectLink *pLink = (ObjectLink*)sb_Allocate(&g_pServerMgr->m_ObjectLinkBank);
		if (!pLink)
			return;

		pLink->m_hObject = pServerObj;
		pLink->m_pNext = pStruct->m_pSphereTouchList->m_pFirstLink;
		pStruct->m_pSphereTouchList->m_pFirstLink = pLink;
		++pStruct->m_pSphereTouchList->m_nInList;
	}
}

// FUNCTION: LITHTECH 0x0047def0
ObjectList* si_FindObjectsTouchingSphere(LTVector *pPosition, float radius)
{
	SphereFindStruct theStruct;

	// Setup the global stuff.
	theStruct.m_pSphereTouchList = (ObjectList*)sb_Allocate(&g_pServerMgr->m_ObjectListBank);
	if (!theStruct.m_pSphereTouchList)
		return LTNULL;

	theStruct.m_pSphereTouchList->m_nInList = 0;
	theStruct.m_pSphereTouchList->m_pFirstLink = LTNULL;

	theStruct.m_pSphereTouchPos = pPosition;
	theStruct.m_SphereTouchRadius = radius;
	theStruct.m_SphereTouchRadiusSqr = radius * radius;
	theStruct.m_SphereTouchDiameter = radius + radius;

	LTVector boxMin, boxMax;
	boxMin = *pPosition - LTVector(radius, radius, radius);
	boxMax = *pPosition + LTVector(radius, radius, radius);

	g_pServerMgr->m_World.m_WorldTree.FindObjectsInBox(&boxMin, &boxMax, SphereFindCallback, &theStruct, NOA_Objects);

	g_SphereFindCount += theStruct.m_pSphereTouchList->m_nInList;

	return theStruct.m_pSphereTouchList;
}

// FUNCTION: LITHTECH 0x0047e020
void si_RelinquishList(ObjectList *pList)
{
	ObjectLink *pCur, *pNext;

	if (!pList)
		return;

	pCur = pList->m_pFirstLink;
	while (pCur)
	{
		pNext = pCur->m_pNext;
		sb_Free(&g_pServerMgr->m_ObjectLinkBank, pCur);
		pCur = pNext;
	}

	sb_Free(&g_pServerMgr->m_ObjectListBank, pList);
}

// FUNCTION: LITHTECH 0x0047e070
ObjectList* si_CreateObjectList()
{
	ObjectList *pRet;

	pRet = (ObjectList*)sb_Allocate(&g_pServerMgr->m_ObjectListBank);
	pRet->m_pFirstLink = LTNULL;
	pRet->m_nInList = 0;
	return pRet;
}

// FUNCTION: LITHTECH 0x0047e0c0
ObjectLink* si_AddObjectToList(ObjectList *pList, HOBJECT hObj)
{
	ObjectLink *pLink;

	pLink = (ObjectLink*)sb_Allocate(&g_pServerMgr->m_ObjectLinkBank);
	pLink->m_pNext = pList->m_pFirstLink;
	pList->m_pFirstLink = pLink;
	pLink->m_hObject = hObj;
	pList->m_nInList++;
	return pLink;
}

// FUNCTION: LITHTECH 0x0047e110
void si_RemoveObjectFromList(ObjectList *pList, HOBJECT hObj)
{
	ObjectLink **ppPrev = &pList->m_pFirstLink;
	ObjectLink *pCur = pList->m_pFirstLink;
	while (pCur)
	{
		if (pCur->m_hObject == hObj)
		{
			*ppPrev = pCur->m_pNext;
			--pList->m_nInList;

			sb_Free(&g_pServerMgr->m_ObjectLinkBank, pCur);
			return;
		}

		ppPrev = &pCur->m_pNext;
		pCur = pCur->m_pNext;
	}
}

// Jupiter's pWorldBsp local and m_Point.DistSqr: the vector subtraction written out made VC6 order the MatVMul_H
// products differently (README, wave 6).
// FUNCTION: LITHTECH 0x0047e160
void GPCCallback(WorldTreeObj *pObj, void *pUser)
{
	if (pObj->GetObjType() != WTObj_DObject)
		return;

	ContainerInstance *pContainer = (ContainerInstance*)pObj;
	if (pContainer->m_ObjectType != OT_CONTAINER)
		return;

	GPCStruct *pStruct = (GPCStruct*)pUser;
	if (pStruct->m_CurListSize >= pStruct->m_MaxListSize)
		return;

	if (pContainer->m_pOriginalBsp->IsUntransformed())
		return;

	WorldBsp *pWorldBsp = pContainer->m_pOriginalBsp;

	float dist = pStruct->m_Point.DistSqr(pContainer->GetPos());
	LTVector boxSize = pWorldBsp->m_MaxBox - pWorldBsp->m_MinBox;
	if (dist > boxSize.MagSqr())
		return;

	// Transform the point..
	LTVector transformedPoint;
	MatVMul_H(&transformedPoint, &pContainer->m_BackTransform, &pStruct->m_Point);

	if (!ci_IsPointInsideBSP(pWorldBsp->m_RootNode, transformedPoint))
	{
		pStruct->m_pList[pStruct->m_CurListSize] = pContainer;
		pStruct->m_CurListSize++;
	}
}

// FUNCTION: LITHTECH 0x0047e330
LTRESULT si_SaveObjects(char *pszSaveFileName, ObjectList *pList, uint32 dwParam, uint32 flags)
{
	ILTStream *pStream = streamsim_Open(pszSaveFileName, "wb");
	if (!pStream)
		RETURN_ERROR(2, ILTPhysics::SaveObjects, LT_ERROR);

	sm_SaveObjects(g_pServerMgr, pStream, pList, dwParam, flags);
	pStream->Release();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047e3b0
LTRESULT si_RestoreObjects(char *pszRestoreFileName, uint32 dwParam, uint32 flags)
{
	ILTStream *pStream = streamsim_Open(pszRestoreFileName, "rb");
	if (!pStream)
		RETURN_ERROR(2, ILTPhysics::RestoreObjects, LT_ERROR);

	LTRESULT ret = sm_RestoreObjects(g_pServerMgr, pStream, dwParam, flags);
	pStream->Release();
	return ret;
}

// FUNCTION: LITHTECH 0x0047e430
LTRESULT si_LoadWorld(char *pszWorldFileName, uint32 flags)
{
	return g_pServerMgr->DoStartWorld(pszWorldFileName, flags, g_pServerMgr->m_LastTime);
}

// FUNCTION: LITHTECH 0x0047e450
LTRESULT si_RunWorld()
{
	return g_pServerMgr->DoRunWorld();
}

// FUNCTION: LITHTECH 0x0047e460
LTRESULT si_UpdateSessionName(const char* sName)
{
	return g_pServerMgr->m_NetMgr.SetSessionName((char*)sName);
}

// FUNCTION: LITHTECH 0x0047e480
LTRESULT si_GetSessionName(char* sName, uint32 dwBufferSize)
{
	if (!sName)
	{
		RETURN_ERROR(1, ILTPhysics::GetSessionName, LT_INVALIDPARAMS);
	}

	return g_pServerMgr->m_NetMgr.GetSessionName(sName, dwBufferSize);
}

// FUNCTION: LITHTECH 0x0047e4e0
LTRESULT si_GetTcpIpAddress(char* sAddress, uint32 dwBufferSize, uint16 &hostPort)
{
	if (!sAddress)
	{
		RETURN_ERROR(1, ILTPhysics::GetTcpIpAddress, LT_INVALIDPARAMS);
	}

	return g_pServerMgr->m_NetMgr.GetLocalIpAddress(sAddress, dwBufferSize, hostPort);
}

// FUNCTION: LITHTECH 0x0047e540
LTRESULT si_SendToServerApp(char *pMsg, uint32 nLen)
{
	if (g_pServerMgr->m_pServerAppHandler)
		return g_pServerMgr->m_pServerAppHandler->ShellMessageFn(pMsg, nLen);

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047e570
FileEntry* si_GetFileList(char *pDirName)
{
	if (!pDirName)
		return LTNULL;

	return sf_GetFileList(&g_pServerMgr->m_FileMgr, pDirName);
}

LTRESULT si_SetCRCString(char *szCRCFiles);
LTRESULT si_CreateStringCRC();
LTRESULT si_CreateWorldCRC();
LTRESULT si_GetGameInfo(void **ppData, uint32 *pLen);
HCLASS si_GetClass(char *pName);
LTRESULT si_GetStaticObject(HCLASS hClass, HOBJECT *obj);
HCLASS si_GetObjectClass(HOBJECT hObject);
LTBOOL si_IsKindOf(HCLASS hClass1, HCLASS hClass2);
LPBASECLASS si_CreateObject(HCLASS hClass, ObjectCreateStruct *pStruct);
LPBASECLASS si_CreateObjectProps(HCLASS hClass, ObjectCreateStruct *pStruct, char *pszProps);
uint32 si_GetServerFlags();
uint32 si_SetServerFlags(uint32 flags);
LTRESULT si_CacheFile(uint32 fileType, char *pFilename);
LTRESULT si_GetNextPoly(HPOLY *hPoly);
LTRESULT si_GetNextWMPoly(HPOLY *hPoly, HOBJECT hWorldModel);
int si_IntRandom(int min, int max);
float si_RandomScale(LTFLOAT scale);
void si_BPrint(char *pMsg, ...);
LTRESULT si_GetSkyDef(SkyDef *pDef);
LTRESULT si_SetSkyDef(SkyDef *pDef);
LTRESULT si_AddObjectToSky(HOBJECT hObj, uint32 index);
LTRESULT si_RemoveObjectFromSky(HOBJECT hObj);
LTRESULT si_GetGlobalLightObject(HOBJECT *hObj);
LTRESULT si_SetGlobalLightObject(HOBJECT hObj);
LTRESULT si_GetPortalFlags(char *pPortalName, uint32 *pFlags);
LTRESULT si_SetPortalFlags(char *pPortalName, uint32 flags);
LTBOOL si_CastRay(IntersectQuery *pQuery, IntersectInfo *pInfo);
LTBOOL si_GetPointShade(LTVector *pPoint, LTVector *pColor);
ObjectList* si_FindObjectsTouchingSphere(LTVector *pPosition, float radius);
void si_FindPoliesTouchingBox(const LTVector &vBoxMin, const LTVector &vBoxMax, HPOLY *pPolyList,
	uint32 nPolyListSize, uint32 *pnNumPoliesFound, ObjectFilterFn FilterFn, void *pUserData);
void si_RelinquishList(ObjectList *pList);
HOBJECT si_ObjectToHandle(LPBASECLASS pObject);
LPBASECLASS si_HandleToObject(HOBJECT hObject);
ObjectList* si_CreateObjectList();
ObjectLink* si_AddObjectToList(ObjectList *pList, HOBJECT hObj);
void si_RemoveObjectFromList(ObjectList *pList, HOBJECT hObj);
LTRESULT si_GetPropString(char *pPropName, char *pRet, int maxLen);
LTRESULT si_GetPropVector(char *pPropName, LTVector *pRet);
LTRESULT si_GetPropVector(char *pPropName, LTVector *pRet);
LTRESULT si_GetPropReal(char *pPropName, float *pRet);
LTRESULT si_GetPropFlags(char *pPropName, uint32 *pRet);
LTRESULT si_GetPropBool(char *pPropName, LTBOOL *pRet);
LTRESULT si_GetPropLongInt(char *pPropName, long *pRet);
LTRESULT si_GetPropRotation(char *pPropName, LTRotation *pRet);
LTRESULT si_GetPropRotationEuler(char *pPropName, LTVector *pAngles);
LTRESULT si_GetPropGeneric(char *pPropName, GenericProp *pGeneric);
LTRESULT si_DoesPropExist(char *pPropName, int *pPropType);
LTRESULT si_AttachClient(HCLIENT hParent, HCLIENT hChild);
LTRESULT si_DetachClient(HCLIENT hChild);
HCLIENT si_GetNextClient(HCLIENT hPrev);
HCLIENTREF si_GetNextClientRef(HCLIENTREF hPrev);
uint32 si_GetClientRefInfoFlags(HCLIENTREF hRef);
LTBOOL si_GetClientRefName(HCLIENTREF hRef, char *pName, int maxLen);
HOBJECT si_GetClientRefObject(HCLIENTREF hRef);
uint32 si_GetClientID(HCLIENT hClient);
LTBOOL si_GetClientName(HCLIENT hClient, char *pName, int maxLen);
void si_SetClientInfoFlags(HCLIENT hClient, uint32 dwClientFlags);
uint32 si_GetClientInfoFlags(HCLIENT hClient);
void si_SetClientUserData(HCLIENT hClient, void *pData);
void* si_GetClientUserData(HCLIENT hClient);
void si_KickClient(HCLIENT hClient);
LTRESULT si_SetClientViewPos(HCLIENT hClient, LTVector *pPos);
void si_RunGameConString(char *pString);
void si_SetGameConVar(char *pName, char *pVal);
HCONVAR si_GetGameConVar(char *pName);
LTBOOL si_IsCommandOn(HCLIENT hClient, int command);
LTRESULT si_GetLastCollision(CollisionInfo *pInfo);
LTRESULT si_PlaySound(PlaySoundInfo *pPlaySoundInfo);
LTRESULT si_IsSoundDone(HLTSOUND hSound, LTBOOL *bDone);
LTRESULT si_GetSoundDuration(HLTSOUND hSound, LTFLOAT *fDuration);
LTRESULT si_KillSound(HLTSOUND hSound);
LTRESULT si_KillSoundLoop(HLTSOUND hSound);
LTRESULT si_CreateInterObjectLink(HOBJECT hOwner, HOBJECT hLinked);
void si_BreakInterObjectLink(HOBJECT hOwner, HOBJECT hLinked);
LTRESULT si_GetSmartLinkBody(HOBJECT hOwner, LTSmartLink_Body **pRet);
LTRESULT si_ReleaseSmartLinkBody(LTSmartLink_Body *pBody);
LTRESULT si_CreateAttachment(HOBJECT hParent, HOBJECT hChild, char *pSocketName,
	LTVector *pOffset, LTRotation *pRotationOffset, HATTACHMENT *hAttachment);
LTRESULT si_RemoveAttachment(HATTACHMENT hAttachment);
LTRESULT si_FindAttachment(HOBJECT hParent, HOBJECT hChild, HATTACHMENT *hAttachment);
LTBOOL si_GetObjectColor(HOBJECT hObject, float *r, float *g, float *b, float *a);
LTBOOL si_SetObjectColor(HOBJECT hObject, float r, float g, float b, float a);
uint32 si_GetObjectUserFlags(HOBJECT hObj);
LTRESULT si_SetObjectUserFlags(HOBJECT hObj, uint32 flags);
HOBJECT si_GetNextObject(HOBJECT hObj);
HOBJECT si_GetNextInactiveObject(HOBJECT hObj);
void si_SetNextUpdate(HOBJECT hObj, LTFLOAT nextUpdate);
void si_SetDeactivationTime(HOBJECT hObj, LTFLOAT fDeactivationTime);
void si_PingObjects(HOBJECT hObj);
void si_GetObjectPos(HOBJECT hObj, LTVector *pPos);
void si_SetObjectPos(HOBJECT hObj, LTVector *pos);
void si_ScaleObject(HOBJECT hObj, LTVector *pNewScale);
LTRESULT si_GetObjectScale(HOBJECT hObj, LTVector *pScale);
LTRESULT si_TeleportObject(HOBJECT hObj, LTVector *pNewPos);
LTRESULT si_GetObjectRotation(HOBJECT hObj, LTRotation *pRotation);
LTRESULT si_SetObjectRotation(HOBJECT hObj, LTRotation *pRotation);
LTRESULT si_RotateObject(HOBJECT hObj, LTRotation *pRotation);
uint8 si_GetBlockingPriority(HOBJECT hObj);
void si_SetBlockingPriority(HOBJECT hObj, uint8 pri);
LTRESULT si_ClipSprite(HOBJECT hObj, HPOLY hPoly);
void si_TiltToPlane(HOBJECT hObj, LTVector *pNormal);
int si_GetObjectState(HOBJECT hObj);
void si_SetObjectState(HOBJECT hObj, int state);
void si_GetLightColor(HOBJECT hObject, float *r, float *g, float *b);
void si_SetLightColor(HOBJECT hObject, float r, float g, float b);
float si_GetLightRadius(HOBJECT hObj);
void si_SetLightRadius(HOBJECT hObj, float radius);
LTRESULT si_GetModelCommandString(HOBJECT hObj, char *pStr, uint32 maxLen);
void si_SetModelPlaying(HOBJECT hObj, LTBOOL bPlaying);
LTBOOL si_GetModelPlaying(HOBJECT hObj);
LTBOOL si_GetModelFilenames(HOBJECT hObj, char *pFilename, int fileBufLen, char *pSkinName, int skinBufLen);
LTBOOL si_GetModelFilenames2(HOBJECT hObj, ObjectCreateStruct *pStruct);
uint32 si_GetObjectContainers(HOBJECT hObj, HOBJECT *pContainerList, uint32 *pFlagList, uint32 maxListSize);
uint32 si_GetContainedObjects(HOBJECT hContainer, HOBJECT *pObjectList, uint32 *pFlagList, uint32 maxListSize);
LTRESULT si_SaveObjects(char *pszSaveFileName, ObjectList *pList, uint32 dwParam, uint32 flags);
LTRESULT si_RestoreObjects(char *pszRestoreFileName, uint32 dwParam, uint32 flags);
LTRESULT si_LoadWorld(char *pszWorldFileName, uint32 flags);
LTRESULT si_RunWorld();
LTRESULT si_UpdateSessionName(const char* sName);
LTRESULT si_GetSessionName(char* sName, uint32 dwBufferSize);
LTRESULT si_SendToServerApp(char *pMsg, uint32 nLen);
LTRESULT si_GetTcpIpAddress(char* sAddress, uint32 dwBufferSize, uint16 &hostPort);
FileEntry* si_GetFileList(char *pDirName);

// FUNCTION: LITHTECH 0x0047e590
void si_SetupFunctionPointers(ILTServer *pServer)
{
	pServer->SetCRCString = si_SetCRCString;
	pServer->CreateStringCRC = si_CreateStringCRC;
	pServer->CreateWorldCRC = si_CreateWorldCRC;
	pServer->GetGameInfo = si_GetGameInfo;
	pServer->GetClass = si_GetClass;
	pServer->GetStaticObject = si_GetStaticObject;
	pServer->GetObjectClass = si_GetObjectClass;
	pServer->IsKindOf = si_IsKindOf;
	pServer->CreateObject = si_CreateObject;
	pServer->CreateObjectProps = si_CreateObjectProps;
	pServer->GetServerFlags = si_GetServerFlags;
	pServer->SetServerFlags = si_SetServerFlags;
	pServer->CacheFile = si_CacheFile;
	pServer->GetNextPoly = si_GetNextPoly;
	pServer->GetNextWMPoly = si_GetNextWMPoly;
	pServer->StartCounter = ic_StartCounter;
	pServer->EndCounter = ic_EndCounter;
	pServer->Random = ic_Random;
	pServer->IntRandom = si_IntRandom;
	pServer->RandomScale = si_RandomScale;
	pServer->BPrint = si_BPrint;
	pServer->DebugOut = (void (*)(char *pMsg, ...))DebugOut;
	pServer->GetSkyDef = si_GetSkyDef;
	pServer->SetSkyDef = si_SetSkyDef;
	pServer->AddObjectToSky = si_AddObjectToSky;
	pServer->RemoveObjectFromSky = si_RemoveObjectFromSky;
	pServer->GetGlobalLightObject = si_GetGlobalLightObject;
	pServer->SetGlobalLightObject = si_SetGlobalLightObject;
	pServer->GetPortalFlags = si_GetPortalFlags;
	pServer->SetPortalFlags = si_SetPortalFlags;
	pServer->IntersectSegment = ServerIntersectSegment;
	pServer->CastRay = si_CastRay;
	pServer->GetPointShade = si_GetPointShade;
	pServer->FindObjectsTouchingSphere = si_FindObjectsTouchingSphere;
	pServer->FindPoliesTouchingBox = si_FindPoliesTouchingBox;
	pServer->RelinquishList = si_RelinquishList;
	pServer->ObjectToHandle = si_ObjectToHandle;
	pServer->HandleToObject = si_HandleToObject;
	pServer->CreateObjectList = si_CreateObjectList;
	pServer->AddObjectToList = si_AddObjectToList;
	pServer->RemoveObjectFromList = si_RemoveObjectFromList;
	pServer->GetPropString = si_GetPropString;
	pServer->GetPropVector = si_GetPropVector;
	pServer->GetPropColor = si_GetPropVector;
	pServer->GetPropReal = si_GetPropReal;
	pServer->GetPropFlags = si_GetPropFlags;
	pServer->GetPropBool = si_GetPropBool;
	pServer->GetPropLongInt = si_GetPropLongInt;
	pServer->GetPropRotation = si_GetPropRotation;
	pServer->GetPropRotationEuler = si_GetPropRotationEuler;
	pServer->GetPropGeneric = si_GetPropGeneric;
	pServer->DoesPropExist = si_DoesPropExist;
	pServer->AttachClient = si_AttachClient;
	pServer->DetachClient = si_DetachClient;
	pServer->GetNextClient = si_GetNextClient;
	pServer->GetNextClientRef = si_GetNextClientRef;
	pServer->GetClientRefInfoFlags = si_GetClientRefInfoFlags;
	pServer->GetClientRefName = si_GetClientRefName;
	pServer->GetClientRefObject = si_GetClientRefObject;
	pServer->GetClientID = si_GetClientID;
	pServer->GetClientName = si_GetClientName;
	pServer->SetClientInfoFlags = si_SetClientInfoFlags;
	pServer->GetClientInfoFlags = si_GetClientInfoFlags;
	pServer->SetClientUserData = si_SetClientUserData;
	pServer->GetClientUserData = si_GetClientUserData;
	pServer->KickClient = si_KickClient;
	pServer->SetClientViewPos = si_SetClientViewPos;
	pServer->RunGameConString = si_RunGameConString;
	pServer->SetGameConVar = si_SetGameConVar;
	pServer->GetGameConVar = si_GetGameConVar;
	pServer->IsCommandOn = si_IsCommandOn;
	pServer->UpperStrcmp = ic_UpperStrcmp;
	pServer->GetLastCollision = si_GetLastCollision;
	pServer->PlaySound = si_PlaySound;
	pServer->IsSoundDone = si_IsSoundDone;
	pServer->GetSoundDuration = si_GetSoundDuration;
	pServer->KillSound = si_KillSound;
	pServer->KillSoundLoop = si_KillSoundLoop;
	pServer->CreateInterObjectLink = si_CreateInterObjectLink;
	pServer->BreakInterObjectLink = si_BreakInterObjectLink;
	pServer->GetSmartLinkBody = si_GetSmartLinkBody;
	pServer->ReleaseSmartLinkBody = si_ReleaseSmartLinkBody;
	pServer->CreateAttachment = si_CreateAttachment;
	pServer->RemoveAttachment = si_RemoveAttachment;
	pServer->FindAttachment = si_FindAttachment;
	pServer->GetObjectColor = si_GetObjectColor;
	pServer->SetObjectColor = si_SetObjectColor;
	pServer->GetObjectUserFlags = si_GetObjectUserFlags;
	pServer->SetObjectUserFlags = si_SetObjectUserFlags;
	pServer->GetNextObject = si_GetNextObject;
	pServer->GetNextInactiveObject = si_GetNextInactiveObject;
	pServer->SetNextUpdate = si_SetNextUpdate;
	pServer->SetDeactivationTime = si_SetDeactivationTime;
	pServer->PingObjects = si_PingObjects;
	pServer->GetObjectPos = si_GetObjectPos;
	pServer->SetObjectPos = si_SetObjectPos;
	pServer->ScaleObject = si_ScaleObject;
	pServer->GetObjectScale = si_GetObjectScale;
	pServer->TeleportObject = si_TeleportObject;
	pServer->GetObjectRotation = si_GetObjectRotation;
	pServer->SetObjectRotation = si_SetObjectRotation;
	pServer->RotateObject = si_RotateObject;
	pServer->GetBlockingPriority = si_GetBlockingPriority;
	pServer->SetBlockingPriority = si_SetBlockingPriority;
	pServer->ClipSprite = si_ClipSprite;
	pServer->TiltToPlane = si_TiltToPlane;
	pServer->GetObjectState = si_GetObjectState;
	pServer->SetObjectState = si_SetObjectState;
	pServer->GetLightColor = si_GetLightColor;
	pServer->SetLightColor = si_SetLightColor;
	pServer->GetLightRadius = si_GetLightRadius;
	pServer->SetLightRadius = si_SetLightRadius;
	pServer->GetNextModelNode = ic_GetNextModelNode;
	pServer->GetModelNodeName = ic_GetModelNodeName;
	pServer->GetModelCommandString = si_GetModelCommandString;
	pServer->GetAnimName = ic_GetAnimName;
	pServer->SetModelPlaying = si_SetModelPlaying;
	pServer->GetModelPlaying = si_GetModelPlaying;
	pServer->GetModelFilenames = si_GetModelFilenames;
	pServer->GetModelFilenames2 = si_GetModelFilenames2;
	pServer->Parse = ic_Parse;
	pServer->GetObjectContainers = si_GetObjectContainers;
	pServer->GetContainedObjects = si_GetContainedObjects;
	pServer->SaveObjects = si_SaveObjects;
	pServer->RestoreObjects = si_RestoreObjects;
	pServer->LoadWorld = si_LoadWorld;
	pServer->RunWorld = si_RunWorld;
	pServer->UpdateSessionName = si_UpdateSessionName;
	pServer->GetSessionName = si_GetSessionName;
	pServer->SendToServerApp = si_SendToServerApp;
	pServer->GetTcpIpAddress = si_GetTcpIpAddress;
	pServer->GetFileList = si_GetFileList;
	pServer->FreeFileList = ic_FreeFileList;
}

// FUNCTION: LITHTECH 0x0047ea80
LTRESULT si_SetCRCString(char *szCRCFiles)
{
	if (szCRCFiles && strlen(szCRCFiles) < sizeof(g_pServerMgr->m_CRCString))
	{
		strncpy(g_pServerMgr->m_CRCString, szCRCFiles, sizeof(g_pServerMgr->m_CRCString));
		return LT_OK;
	}

	return LT_ERROR;
}

// FUNCTION: LITHTECH 0x0047eac0
LTRESULT si_CreateStringCRC()
{
	return g_pServerMgr->CreateStringCRC();
}

// FUNCTION: LITHTECH 0x0047ead0
LTRESULT si_CreateWorldCRC()
{
	return g_pServerMgr->CreateWorldCRC();
}

// FUNCTION: LITHTECH 0x0047eae0
LTRESULT si_GetGameInfo(void **ppData, uint32 *pLen)
{
	*ppData = g_pServerMgr->m_pGameInfo;
	*pLen = g_pServerMgr->m_GameInfoLen;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047eb10
HCLASS si_GetClass(char *pName)
{
	// CClassMgr::FindClassData, inlined.
	HHashElement *hElement = hs_FindElement(g_pServerMgr->m_ClassMgr.m_hClassNameHash, pName, strlen(pName));
	if (hElement)
		return (HCLASS)hs_GetElementUserData(hElement);
	else
		return LTNULL;
}

// FUNCTION: LITHTECH 0x0047eb50
LTRESULT si_GetStaticObject(HCLASS hClass, HOBJECT *obj)
{
	*obj = LTNULL;

	if (!hClass)
	{
		RETURN_ERROR(1, ILTPhysics::GetStaticObject, LT_INVALIDPARAMS);
	}

	CClassData *pClassData = (CClassData*)hClass;
	if (pClassData->m_pStaticObject)
	{
		*obj = pClassData->m_pStaticObject;
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(1, ILTPhysics::GetStaticObject, LT_ERROR);
	}
}

// FUNCTION: LITHTECH 0x0047ebe0
HCLASS si_GetObjectClass(HOBJECT hObject)
{
	if (!hObject)
	{
		RETURN_ERROR(1, ILTPhysics::GetObjectClass, LTNULL);
	}

	return (HCLASS)hObject->sd->m_pClass->m_pInternal[g_pServerMgr->m_ClassMgr.m_ClassIndex];
}

// FUNCTION: LITHTECH 0x0047ec40
LTBOOL si_IsKindOf(HCLASS hClass1, HCLASS hClass2)
{
	if (!hClass1 || !hClass2)
		return LTFALSE;

	CClassData *pClass1 = (CClassData*)hClass1;
	CClassData *pClass2 = (CClassData*)hClass2;

	ClassDef *pCur = pClass1->m_pClass;
	while (pCur)
	{
		if (pCur == pClass2->m_pClass)
			return LTTRUE;

		pCur = pCur->m_ParentClass;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0047ec70
LTRESULT si_GetPortalFlags(char *pPortalName, uint32 *pFlags)
{
	BspPortal *pPortal;

	pPortal = w_FindPortal(&g_pServerMgr->m_World, pPortalName, LTNULL, LTNULL);
	if (!pPortal)
	{
		RETURN_ERROR(1, ILTPhysics::GetPortalFlags, LT_NOTFOUND);
	}

	*pFlags = pPortal->m_Flags;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047ece0
LTRESULT si_SetPortalFlags(char *pPortalName, uint32 flags)
{
	return sm_SetPortalFlags(g_pServerMgr, pPortalName, flags);
}

// FUNCTION: LITHTECH 0x0047ed00
LTBOOL si_CastRay(IntersectQuery *pQuery, IntersectInfo *pInfo)
{
	float mag = pQuery->m_Direction.Mag();
	if (mag < 0.00001f)
		return LTFALSE;

	// Scale it to 10000.
	float scale = 10000.0f / mag;
	pQuery->m_To = pQuery->m_Direction * scale;
	pQuery->m_To += pQuery->m_From;

	return ServerIntersectSegment(pQuery, pInfo);
}

// FUNCTION: LITHTECH 0x0047edc0
void si_FindPoliesTouchingBox(const LTVector &vBoxMin, const LTVector &vBoxMax, HPOLY *pPolyList,
	uint32 nPolyListSize, uint32 *pnNumPoliesFound, ObjectFilterFn FilterFn, void *pUserData)
{
	BoxPolyStruct theStruct;

	theStruct.m_pPolyList = pPolyList;
	theStruct.m_nPolyListSize = pPolyList ? nPolyListSize : 0;
	theStruct.m_nPoliesFound = 0;

	theStruct.m_Center = (vBoxMax + vBoxMin) * 0.5f;
	theStruct.m_Radius = (vBoxMax - vBoxMin).Mag() * 0.5f;
	theStruct.m_FilterFn = FilterFn;
	theStruct.m_pUserData = pUserData;

	// Setup the box the polies are tested against.
	g_BoxFindRadius = theStruct.m_Radius;
	g_BoxFindCenter = theStruct.m_Center;
	g_BoxFindPlanes[0].m_Dist = vBoxMin.x;
	g_BoxFindPlanes[1].m_Dist = -vBoxMax.x;
	g_BoxFindPlanes[2].m_Dist = vBoxMin.y;
	g_BoxFindPlanes[3].m_Dist = -vBoxMax.y;
	g_BoxFindPlanes[4].m_Dist = vBoxMin.z;
	g_BoxFindPlanes[5].m_Dist = -vBoxMax.z;

	g_pServerMgr->m_World.m_WorldTree.FindObjectsInBox(&vBoxMin, &vBoxMax, BoxPolyFindCallback, &theStruct, NOA_Objects);

	g_PolyFindCount += theStruct.m_nPoliesFound;

	if (pnNumPoliesFound)
		*pnNumPoliesFound = theStruct.m_nPoliesFound;
}

PropEntry* _FindProp(char *pPropName);

// FUNCTION: LITHTECH 0x0047ef50
LTRESULT si_GetPropString(char *pPropName, char *pRet, int maxLen)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_STRING)
	{
		strncpy(pRet, (char*)pProp->m_Data, maxLen - 1);
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047ef90
PropEntry* _FindProp(char *pPropName)
{
	PropEntry *pCur;

	for (pCur = g_pServerMgr->m_pCurProps; pCur; pCur = pCur->m_pNext)
	{
		if (strcmp(pCur->m_Name, pPropName) == 0)
			return pCur;
	}

	return LTNULL;
}



// Used for both GetPropVector and GetPropColor.
// FUNCTION: LITHTECH 0x0047f000
LTRESULT si_GetPropVector(char *pPropName, LTVector *pRet)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && (pProp->m_Type == PT_VECTOR || pProp->m_Type == PT_COLOR))
	{
		*pRet = *(LTVector*)pProp->m_Data;
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f040
LTRESULT si_GetPropReal(char *pPropName, float *pRet)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_REAL)
	{
		*pRet = *(float*)pProp->m_Data;
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f070
LTRESULT si_GetPropFlags(char *pPropName, uint32 *pRet)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_FLAGS)
	{
		*pRet = (uint32)*(float*)pProp->m_Data;
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f0a0
LTRESULT si_GetPropBool(char *pPropName, LTBOOL *pRet)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_BOOL)
	{
		*pRet = *(char*)pProp->m_Data;
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f0d0
LTRESULT si_GetPropLongInt(char *pPropName, long *pRet)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_LONGINT)
	{
		*pRet = (long)*(float*)pProp->m_Data;
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// The original copies the euler angles through a 16-byte float array on the stack.
// FUNCTION: LITHTECH 0x0047f100
LTRESULT si_GetPropRotation(char *pPropName, LTRotation *pRet)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_ROTATION)
	{
		float angles[4];
		angles[0] = ((float*)pProp->m_Data)[0];
		angles[1] = ((float*)pProp->m_Data)[1];
		angles[2] = ((float*)pProp->m_Data)[2];
		gr_EulerToRotation(angles[0], angles[1], angles[2], pRet);
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// The property data is copied as a whole LTRotation (the 16-byte frame; VC6 forwards x and y but not z)
// and the angles come out through LTVector::Init.
// FUNCTION: LITHTECH 0x0047f160
LTRESULT si_GetPropRotationEuler(char *pPropName, LTVector *pAngles)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp && pProp->m_Type == PT_ROTATION)
	{
		LTRotation rot = *(LTRotation*)pProp->m_Data;
		pAngles->Init(rot.m_Quat[0], rot.m_Quat[1], rot.m_Quat[2]);
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f1b0
LTRESULT si_GetPropGeneric(char *pPropName, GenericProp *pGeneric)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (!pProp)
		return LT_NOTFOUND;

	gp_Init(pGeneric);

	switch (pProp->m_Type)
	{
		case PT_STRING:
		{
			gp_InitString(pGeneric, (char*)pProp->m_Data);
			return LT_OK;
		}

		case PT_VECTOR:
		case PT_COLOR:
		{
			gp_InitVector(pGeneric, (LTVector*)pProp->m_Data);
			return LT_OK;
		}

		case PT_REAL:
		case PT_FLAGS:
		case PT_LONGINT:
		{
			gp_InitFloat(pGeneric, *(float*)pProp->m_Data);
			return LT_OK;
		}

		case PT_BOOL:
		{
			gp_InitFloat(pGeneric, (float)*(char*)pProp->m_Data);
			return LT_OK;
		}

		case PT_ROTATION:
		{
			gp_InitRotation(pGeneric, (LTVector*)pProp->m_Data);
			return LT_OK;
		}
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f280
LTRESULT si_DoesPropExist(char *pPropName, int *pPropType)
{
	PropEntry *pProp = _FindProp(pPropName);
	if (pProp)
	{
		*pPropType = pProp->m_Type;
		return LT_OK;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x0047f2b0
HOBJECT si_ObjectToHandle(LPBASECLASS pObject)
{
	if (!pObject)
		return LTNULL;

	return pObject->m_hObject;
}

// FUNCTION: LITHTECH 0x0047f2c0
LPBASECLASS si_HandleToObject(HOBJECT hObject)
{
	if (!hObject)
		return LTNULL;

	return hObject->sd->m_pObject;
}

// FUNCTION: LITHTECH 0x0047f2e0
LPBASECLASS si_CreateObject(HCLASS hClass, ObjectCreateStruct *pStruct)
{
	if (!hClass || !pStruct)
		return LTNULL;

	return g_pServerMgr->EZCreateObject((CClassData*)hClass, pStruct);
}

// PRECREATE_STRINGPROP (2.0f) passed through sm_AddObjectToWorld's uint32 parameter.
#define OBJECTCREATED_STRINGPROP	0x40000000

#define MAX_PROPSTRING_LEN	100

// FUNCTION: LITHTECH 0x0047f310
LPBASECLASS si_CreateObjectProps(HCLASS hClass, ObjectCreateStruct *pStruct, char *pszProps)
{
	CClassData *pClassData = (CClassData*)hClass;
	LPBASECLASS pBaseClass;
	int len;
	LTObject *pObject;
	ConParse cParse;
	PropEntry *pProp, *pNext;
	LTRESULT dResult;
	int i;

	// Create and construct the object.
	pBaseClass = sm_AllocateObjectOfClass(g_pServerMgr, pClassData->m_pClass);

	// Parse the property string into the property list.
	g_pServerMgr->m_pCurProps = LTNULL;
	if (pszProps)
	{
		cParse.Init(pszProps);
		while (cParse.Parse())
		{
			if (cParse.m_nArgs <= 0)
				continue;

			pProp = (PropEntry*)sb_Allocate(&g_pServerMgr->m_FileIDInfoBank);
			pProp->m_Data[0] = 0;
			pProp->m_Type = PT_STRING;
			pProp->m_Name[0] = 0;
			strncpy(pProp->m_Name, cParse.m_Args[0], sizeof(pProp->m_Name) - 1);
			pProp->m_Name[sizeof(pProp->m_Name) - 1] = 0;

			len = 0;
			for (i = 1; i < cParse.m_nArgs; i++)
			{
				len += strlen(cParse.m_Args[i]);
				if (len > MAX_PROPSTRING_LEN)
					break;

				if (i != 1)
					strcat((char*)pProp->m_Data, " ");

				strcat((char*)pProp->m_Data, cParse.m_Args[i]);
			}

			pProp->m_pNext = g_pServerMgr->m_pCurProps;
			g_pServerMgr->m_pCurProps = pProp;
		}
	}

	pBaseClass->EngineMessageFn(MID_PRECREATE, pStruct, PRECREATE_STRINGPROP);

	dResult = sm_AddObjectToWorld(g_pServerMgr, pBaseClass, pClassData->m_pClass, pStruct,
		INVALID_OBJECTID, OBJECTCREATED_STRINGPROP, &pObject);

	// Free the property list.
	pProp = g_pServerMgr->m_pCurProps;
	while (pProp)
	{
		pNext = pProp->m_pNext;
		sb_Free(&g_pServerMgr->m_FileIDInfoBank, pProp);
		pProp = pNext;
	}
	g_pServerMgr->m_pCurProps = LTNULL;

	if (dResult == LT_OK)
		return pBaseClass;

	sm_FreeObjectOfClass(g_pServerMgr, pClassData->m_pClass, pBaseClass);
	return LTNULL;
}

// FUNCTION: LITHTECH 0x0047f580
uint32 si_GetServerFlags()
{
	// Build the flags from internal stuff.
	uint32 flags = g_pServerMgr->m_ServerFlags | ((g_pServerMgr->m_InternalFlags & SFLAG_DEMOPLAYBACK) >> 1);
	if (g_pServerMgr->m_InternalFlags & SFLAG_BUILDINGCACHELIST)
		flags |= SS_CACHING;

	return flags;
}

// FUNCTION: LITHTECH 0x0047f5b0
uint32 si_SetServerFlags(uint32 flags)
{
	// Only let them set certain ones..
	g_pServerMgr->m_ServerFlags = (flags & (SS_PAUSED));
	return g_pServerMgr->m_ServerFlags;
}

// FUNCTION: LITHTECH 0x0047f5d0
LTRESULT si_CacheFile(uint32 fileType, char *pFilename)
{
	return sm_CacheFile(g_pServerMgr, fileType, pFilename);
}

// FUNCTION: LITHTECH 0x0047f5f0
LTRESULT si_GetNextWMPoly(HPOLY *hPoly, HOBJECT hWorldModel)
{
	WorldModelInstance *pWorldModel;
	WorldBsp *pBsp;
	uint32 iPoly;

	if (!hPoly)
		RETURN_ERROR(1, ILTServer::GetNextWMPoly, LT_INVALIDPARAMS);

	if (!hWorldModel)
		RETURN_ERROR(1, ILTServer::GetNextWMPoly, LT_INVALIDPARAMS);

	pWorldModel = (WorldModelInstance*)hWorldModel;
	pBsp = pWorldModel->m_pValidBsp->IsUntransformed() ? LTNULL : pWorldModel->m_pValidBsp;
	if (!pBsp)
		RETURN_ERROR(1, ILTServer::GetNextWMPoly, LT_INVALIDDATA);

	if (*hPoly == INVALID_HPOLY)
	{
		*hPoly = (uint32)pBsp->m_Index << 16;
		return LT_OK;
	}

	iPoly = (*hPoly & 0xFFFF) + 1;
	if (iPoly < pBsp->m_nPolies)
	{
		*hPoly = ((uint32)pBsp->m_Index << 16) | (iPoly & 0xFFFF);
		return LT_OK;
	}

	*hPoly = INVALID_HPOLY;
	return LT_FINISHED;
}

// FUNCTION: LITHTECH 0x0047f720
LTRESULT si_GetNextPoly(HPOLY *hPoly)
{
	uint32 iWorld, iPoly, hCur;

	if (!hPoly)
		RETURN_ERROR(1, ILTPhysics::GetNextPoly, LT_INVALIDPARAMS);

	if (!g_pServerMgr->m_World.m_bLoaded)
		RETURN_ERROR(1, ILTPhysics::GetNextPoly, LT_NOTINITIALIZED);

	hCur = *hPoly;
	if (hCur == INVALID_HPOLY)
	{
		*hPoly = 0xFFFF;
		return LT_OK;
	}

	iWorld = hCur >> 16;
	iPoly = (hCur & 0xFFFF) + 1;
	if (iPoly >= g_pServerMgr->m_World.m_WorldModels[iWorld]->m_pOriginalBsp->m_nNodes)
	{
		iWorld++;
		iPoly = 0;
	}

	if (iWorld >= g_pServerMgr->m_World.m_WorldModels.GetSize())
		return LT_FINISHED;

	*hPoly = (iWorld << 16) | (iPoly & 0xFFFF);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047f800
int si_IntRandom(int min, int max)
{
	return min + (rand() % (max - (min - 1)));
}

// FUNCTION: LITHTECH 0x0047f820
float si_RandomScale(LTFLOAT scale)
{
	return ((LTFLOAT)rand() / RAND_MAX) * scale;
}

// FUNCTION: LITHTECH 0x0047f840
void si_BPrint(char *pMsg, ...)
{
	va_list marker;
	char msg[500];

	va_start(marker, pMsg);
	_vsnprintf(msg, 499, pMsg, marker);
	va_end(marker);

	BPrint(msg);
}

// FUNCTION: LITHTECH 0x0047f880
LTRESULT si_GetSkyDef(SkyDef *pDef)
{
	memcpy(pDef, &g_pServerMgr->m_SkyDef, sizeof(SkyDef));
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047f8a0
LTRESULT si_SetSkyDef(SkyDef *pDef)
{
	memcpy(&g_pServerMgr->m_SkyDef, pDef, sizeof(SkyDef));
	sm_SetSendSkyDef(g_pServerMgr);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047f8d0
LTRESULT si_AddObjectToSky(HOBJECT hObj, uint32 index)
{
	// Is it valid?
	if (!hObj)
	{
		RETURN_ERROR_PARAM(1, ILTPhysics::AddObjectToSky, LT_ERROR, "object is LTNULL");
	}

	if (index >= MAX_SKYOBJECTS)
	{
		RETURN_ERROR_PARAM(1, ILTPhysics::AddObjectToSky, LT_ERROR, "invalid index");
	}

	// Is it already in the sky?
	if (hObj->m_InternalFlags & IFLAG_INSKY)
		return LT_OK;

	hObj->m_InternalFlags |= IFLAG_INSKY;
	g_pServerMgr->m_SkyObjects[index] = hObj->m_ObjectID;
	sm_SetSendSkyDef(g_pServerMgr);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047f990
LTRESULT si_RemoveObjectFromSky(HOBJECT hObj)
{
	// Is it valid?
	if (!hObj)
	{
		RETURN_ERROR_PARAM(1, ILTPhysics::RemoveObjectFromSky, LT_ERROR, "object is LTNULL");
	}

	return sm_RemoveObjectFromSky(g_pServerMgr, hObj);
}

// FUNCTION: LITHTECH 0x0047f9e0
LTRESULT si_GetGlobalLightObject(HOBJECT *hObj)
{
	if (!hObj)
		return LT_ERROR;

	*hObj = g_pServerMgr->m_pGlobalLightObject;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047fa00
LTRESULT si_SetGlobalLightObject(HOBJECT hObj)
{
	g_pServerMgr->SetGlobalLightObject(hObj);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047fa20
LTRESULT si_AttachClient(HCLIENT hParent, HCLIENT hChild)
{
	if (!hParent || !hChild)
		RETURN_ERROR(1, ILTPhysics::AttachClient, LT_INVALIDPARAMS);

	return sm_AttachClient(g_pServerMgr, (Client*)hParent, (Client*)hChild);
}

// FUNCTION: LITHTECH 0x0047fa80
LTRESULT si_DetachClient(HCLIENT hChild)
{
	if (!hChild)
		RETURN_ERROR(1, ILTPhysics::DetachClient, LT_INVALIDPARAMS);

	return sm_DetachClient(g_pServerMgr, (Client*)hChild);
}

// FUNCTION: LITHTECH 0x0047fad0
HCLIENT si_GetNextClient(HCLIENT hPrev)
{
	if (hPrev)
	{
		Client *pPrev = (Client*)hPrev;
		if (pPrev->m_Link.m_pNext == &g_pServerMgr->m_Clients.m_Head)
			return LTNULL;

		return (HCLIENT)pPrev->m_Link.m_pNext->m_pData;
	}
	else
	{
		if (g_pServerMgr->m_Clients.m_nElements == 0)
			return LTNULL;

		return (HCLIENT)g_pServerMgr->m_Clients.m_Head.m_pNext->m_pData;
	}
}

// FUNCTION: LITHTECH 0x0047fb10
HCLIENTREF si_GetNextClientRef(HCLIENTREF hPrev)
{
	LTList *pList = &g_pServerMgr->m_ClientReferences;

	LTLink *pCurLink;
	ClientRef *pPrev = (ClientRef*)hPrev;
	if (pPrev)
	{
		pCurLink = &pPrev->m_Link;
	}
	else
	{
		pCurLink = &pList->m_Head;
	}

	if (pCurLink->m_pNext == &pList->m_Head)
		return LTNULL;
	else
		return (HCLIENTREF)pCurLink->m_pNext->m_pData;
}

// FUNCTION: LITHTECH 0x0047fb40
uint32 si_GetClientRefInfoFlags(HCLIENTREF hRef)
{
	if (!hRef)
		return 0;

	ClientRef *pRef = (ClientRef*)hRef;
	if (pRef->m_ClientFlags & CFLAG_LOCAL)
		return CIF_LOCAL;
	else
		return 0;
}

// FUNCTION: LITHTECH 0x0047fb60
LTBOOL si_GetClientRefName(HCLIENTREF hRef, char *pName, int maxLen)
{
	if (!hRef)
	{
		pName[0] = 0;
		return LTFALSE;
	}

	ClientRef *pRef = (ClientRef*)hRef;
	strncpy(pName, pRef->m_ClientName, maxLen - 1);
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0047fb90
HOBJECT si_GetClientRefObject(HCLIENTREF hRef)
{
	if (!hRef)
		return LTNULL;

	ClientRef *pRef = (ClientRef*)hRef;
	return sm_FindObject(g_pServerMgr, pRef->m_ObjectID);
}

// FUNCTION: LITHTECH 0x0047fbb0
uint32 si_GetClientID(HCLIENT hClient)
{
	if (!hClient)
		return (uint32)-1;

	return ((Client*)hClient)->m_ClientID;
}

// FUNCTION: LITHTECH 0x0047fbd0
LTBOOL si_GetClientName(HCLIENT hClient, char *pName, int maxLen)
{
	if (!hClient)
		return LTFALSE;

	Client *pClient = (Client*)hClient;
	strncpy(pName, pClient->m_Name, maxLen - 1);
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0047fc00
void si_SetClientInfoFlags(HCLIENT hClient, uint32 dwClientFlags)
{
	if (!hClient)
		return;

	Client *pClient = (Client*)hClient;

	pClient->m_ClientFlags &= ~(CFLAG_FULLRES | CFLAG_SENDCOBJROTATION | CFLAG_FORCENEXTUPDATE | CFLAG_AUTOACTIVATEOBJECTS);

	if (dwClientFlags & CIF_FULLRES)
		pClient->m_ClientFlags |= CFLAG_FULLRES;

	if (dwClientFlags & CIF_SENDCOBJROTATION)
		pClient->m_ClientFlags |= CFLAG_SENDCOBJROTATION;

	if (dwClientFlags & CIF_FORCENEXTUPDATE)
		pClient->m_ClientFlags |= CFLAG_FORCENEXTUPDATE;

	if (dwClientFlags & CIF_AUTOACTIVATEOBJECTS)
		pClient->m_ClientFlags |= CFLAG_AUTOACTIVATEOBJECTS;
}

// FUNCTION: LITHTECH 0x0047fc60
uint32 si_GetClientInfoFlags(HCLIENT hClient)
{
	uint32 flags = 0;

	if (hClient)
	{
		Client *pClient = (Client*)hClient;

		if (pClient->m_ClientFlags & CFLAG_LOCAL)
			flags = CIF_LOCAL;
		if (pClient->m_ClientFlags & CFLAG_VIRTUAL)
			flags |= CIF_PLAYBACK;
		if (pClient->m_ClientFlags & CFLAG_FULLRES)
			flags |= CIF_FULLRES;
		if (pClient->m_ClientFlags & CFLAG_SENDCOBJROTATION)
			flags |= CIF_SENDCOBJROTATION;
		if (pClient->m_ClientFlags & CFLAG_FORCENEXTUPDATE)
			flags |= CIF_FORCENEXTUPDATE;
		if (pClient->m_ClientFlags & CFLAG_AUTOACTIVATEOBJECTS)
			flags |= CIF_AUTOACTIVATEOBJECTS;
	}

	return flags;
}

// FUNCTION: LITHTECH 0x0047fca0
void si_SetClientUserData(HCLIENT hClient, void *pData)
{
	if (!hClient)
		return;

	((Client*)hClient)->m_pPluginUserData = pData;
}

// FUNCTION: LITHTECH 0x0047fcc0
void* si_GetClientUserData(HCLIENT hClient)
{
	if (!hClient)
		return LTNULL;

	return ((Client*)hClient)->m_pPluginUserData;
}

// FUNCTION: LITHTECH 0x0047fcd0
void si_KickClient(HCLIENT hClient)
{
	if (hClient)
		g_pServerMgr->m_NetMgr.Disconnect(((Client*)hClient)->m_ConnectionID, DISCONNECTREASON_KICKED);
}

// FUNCTION: LITHTECH 0x0047fd00
LTRESULT si_SetClientViewPos(HCLIENT hClient, LTVector *pPos)
{
	if (!hClient)
		RETURN_ERROR(0, SetClientViewPos, LT_INVALIDPARAMS);

	((Client*)hClient)->m_ViewPos = *pPos;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047fd60
void si_RunGameConString(char *pString)
{
	cc_HandleCommand(&g_pServerMgr->m_ConsoleState, pString);
}

// FUNCTION: LITHTECH 0x0047fd80
void si_SetGameConVar(char *pName, char *pVal)
{
	if (!pName || !pVal)
		return;

	cc_SetConsoleVariable(&g_pServerMgr->m_ConsoleState, pName, pVal);
}

// FUNCTION: LITHTECH 0x0047fdb0
HCONVAR si_GetGameConVar(char *pName)
{
	if (!pName)
		return LTNULL;

	return (HCONVAR)cc_FindConsoleVar(&g_pServerMgr->m_ConsoleState, pName);
}

// FUNCTION: LITHTECH 0x0047fdd0
LTBOOL si_IsCommandOn(HCLIENT hClient, int command)
{
	if (!hClient || command >= MAX_CLIENT_COMMANDS)
		return LTFALSE;

	Client *pClient = (Client*)hClient;
	return pClient->m_Commands[pClient->m_iCurCommands][command];
}

// FUNCTION: LITHTECH 0x0047fe10
LTRESULT si_GetLastCollision(CollisionInfo *pInfo)
{
	if (!pInfo)
	{
		RETURN_ERROR(1, ILTPhysics::GetLastCollision, LT_INVALIDPARAMS);
	}

	if (!g_pServerMgr->m_pCollisionInfo)
		return LT_ERROR;

	pInfo->m_Plane = g_pServerMgr->m_pCollisionInfo->m_Plane;
	pInfo->m_hObject = g_pServerMgr->m_pCollisionInfo->m_hObject;
	pInfo->m_hPoly = g_pServerMgr->m_pCollisionInfo->m_hPoly;
	pInfo->m_vStopVel = g_pServerMgr->m_pCollisionInfo->m_vStopVel;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0047fed0
LTRESULT si_PlaySound(PlaySoundInfo *pPlaySoundInfo)
{
	HLTSOUND hSound;
	return g_pServerMgr->PlaySound(pPlaySoundInfo, hSound);
}

// FUNCTION: LITHTECH 0x0047fef0
LTRESULT si_GetSoundDuration(HLTSOUND hSound, LTFLOAT *fDuration)
{
	return g_pServerMgr->GetSoundDuration(hSound, *fDuration);
}

// FUNCTION: LITHTECH 0x0047ff10
LTRESULT si_IsSoundDone(HLTSOUND hSound, LTBOOL *bDone)
{
	return g_pServerMgr->IsSoundDone(hSound, *bDone);
}

// FUNCTION: LITHTECH 0x0047ff30
LTRESULT si_KillSound(HLTSOUND hSound)
{
	return g_pServerMgr->KillSound(hSound);
}

// FUNCTION: LITHTECH 0x0047ff50
LTRESULT si_KillSoundLoop(HLTSOUND hSound)
{
	return g_pServerMgr->KillSoundLoop(hSound);
}

// FUNCTION: LITHTECH 0x0047ff70
LTRESULT si_CreateInterObjectLink(HOBJECT hOwner, HOBJECT hLinked)
{
	if (!hOwner || !hLinked)
		RETURN_ERROR(1, ILTPhysics::CreateInterObjectLink, LT_INVALIDPARAMS);

	return CreateInterLink(g_pServerMgr, hOwner, hLinked, LINKTYPE_INTERLINK);
}

// FUNCTION: LITHTECH 0x0047ffd0
void si_BreakInterObjectLink(HOBJECT hOwner, HOBJECT hLinked)
{
	if (!hOwner || !hLinked)
		return;

	DisconnectLinks(g_pServerMgr, hOwner, hLinked, LTFALSE);
}

// FUNCTION: LITHTECH 0x00480000
LTRESULT si_GetSmartLinkBody(HOBJECT hOwner, LTSmartLink_Body **pRet)
{
	LTRESULT dResult;

	if (!hOwner || !pRet)
		RETURN_ERROR(1, ILTPhysics::CreateSmartLink, LT_INVALIDPARAMS);

	dResult = LT_OK;
	*pRet = LTNULL;
	*pRet = (LTSmartLink_Body*)AddObjRef(hOwner);
	if (*pRet)
		dResult = CreateInterLink(g_pServerMgr, hOwner, *pRet, LINKTYPE_OBJREF);

	return dResult;
}

// FUNCTION: LITHTECH 0x00480080
LTRESULT si_ReleaseSmartLinkBody(LTSmartLink_Body *pBody)
{
	if (!pBody)
		RETURN_ERROR(1, ILTPhysics::ReleaseSmartLinkBody, LT_INVALIDPARAMS);

	ReleaseObjRef((ObjRefEntry*)pBody);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004800d0
LTBOOL si_GetObjectColor(HOBJECT hObject, float *r, float *g, float *b, float *a)
{
	if (!hObject)
		return LTFALSE;

	if (r)
		*r = hObject->m_ColorR * MATH_ONE_OVER_255;
	if (g)
		*g = hObject->m_ColorG * MATH_ONE_OVER_255;
	if (b)
		*b = hObject->m_ColorB * MATH_ONE_OVER_255;
	if (a)
		*a = hObject->m_ColorA * MATH_ONE_OVER_255;

	return LTTRUE;
}

static void si_SetColorChangeFlags(LTObject *pObject);

// FUNCTION: LITHTECH 0x00480160
LTBOOL si_SetObjectColor(HOBJECT hObject, float r, float g, float b, float a)
{
	if (!hObject)
		return LTFALSE;

	uint8 newColor[4];

	newColor[0] = (uint8)(r * 255.0f);
	newColor[1] = (uint8)(g * 255.0f);
	newColor[2] = (uint8)(b * 255.0f);
	newColor[3] = (uint8)(a * 255.0f);

	if (hObject->m_ColorR != newColor[0] || hObject->m_ColorG != newColor[1] ||
		hObject->m_ColorB != newColor[2] || hObject->m_ColorA != newColor[3])
	{
		hObject->m_ColorR = newColor[0];
		hObject->m_ColorG = newColor[1];
		hObject->m_ColorB = newColor[2];
		hObject->m_ColorA = newColor[3];

		si_SetColorChangeFlags(hObject);
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00480220
static void si_SetColorChangeFlags(LTObject *pObject)
{
	SetObjectChangeFlags(g_pServerMgr, pObject, CF_RENDERINFO);
}

// FUNCTION: LITHTECH 0x00480310
uint32 si_GetObjectUserFlags(HOBJECT hObj)
{
	if (!hObj)
		return 0;

	return hObj->m_UserFlags;
}

// FUNCTION: LITHTECH 0x00480320
LTRESULT si_SetObjectUserFlags(HOBJECT hObj, uint32 flags)
{
	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::SetObjectUserFlags, LT_ERROR);
	}

	if (hObj->m_UserFlags != flags)
	{
		hObj->m_UserFlags = flags;
		SetObjectChangeFlags(g_pServerMgr, hObj, CF_FLAGS);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00480470
HOBJECT si_GetNextObject(HOBJECT hObj)
{
	LTLink *pLink;

	if (hObj)
	{
		pLink = &hObj->sd->m_ListNode;

		pLink = pLink->m_pNext;
		if (pLink == &g_pServerMgr->m_Objects.m_Head)
			return LTNULL;
	}
	else
	{
		pLink = g_pServerMgr->m_Objects.m_Head.m_pNext;
		if (pLink == &g_pServerMgr->m_Objects.m_Head)
			return LTNULL;
	}

	LTObject *pObj = (LTObject*)pLink->m_pData;
	if (pObj->m_InternalFlags & IFLAG_INACTIVE_MASK)
	{
		return LTNULL;
	}
	else
	{
		return pObj;
	}
}

// FUNCTION: LITHTECH 0x004804c0
HOBJECT si_GetNextInactiveObject(HOBJECT hObj)
{
	LTLink *pLink;

	// Note: it cycles thru the object list backwards here because
	// the inactive objects are at the end of the list.
	if (hObj)
	{
		pLink = hObj->sd->m_ListNode.m_pPrev;
		if (pLink == &g_pServerMgr->m_Objects.m_Head)
			return LTNULL;
	}
	else
	{
		pLink = g_pServerMgr->m_Objects.m_Head.m_pPrev;
		if (pLink == &g_pServerMgr->m_Objects.m_Head)
			return LTNULL;
	}

	LTObject *pObj = (LTObject*)pLink->m_pData;
	if (pObj->m_InternalFlags & IFLAG_INACTIVE_MASK)
		return pObj;
	else
		return LTNULL;
}

// FUNCTION: LITHTECH 0x00480510
void si_SetNextUpdate(HOBJECT hObj, LTFLOAT nextUpdate)
{
	if (!hObj)
		return;

	hObj->sd->m_NextUpdate = nextUpdate;
}

// FUNCTION: LITHTECH 0x00480530
void si_SetDeactivationTime(HOBJECT hObj, LTFLOAT fDeactivationTime)
{
	if (!hObj)
		return;

	hObj->sd->m_fDeactivationTime = fDeactivationTime;
	hObj->sd->m_fDeactivateTimer = fDeactivationTime;

	// Wake it up if it auto-deactivated.
	if (hObj->m_InternalFlags & IFLAG_AUTODEACTIVATED)
		sm_SetObjectStateFlags(g_pServerMgr, hObj, hObj->m_InternalFlags & (IFLAG_INACTIVE | IFLAG_INACTIVE_TOUCH));

	if ((g_bAutoDeactivate && hObj->m_WTFrameCode == FRAMECODE_NOTINTREE) || !g_pServerMgr->m_World.m_bLoaded)
		g_pServerMgr->m_World.m_WorldTree.InsertObject(hObj, NOA_Objects);
}

// FUNCTION: LITHTECH 0x004805a0
void si_PingObjects(HOBJECT hObj)
{
	if (hObj)
		sm_ActivateObjectsNear(g_pServerMgr, hObj);
}

// FUNCTION: LITHTECH 0x004805c0
void si_GetObjectPos(HOBJECT hObj, LTVector *pPos)
{
	if (!hObj)
	{
		pPos->Init();
		return;
	}

	*pPos = hObj->m_Pos;
}

// FUNCTION: LITHTECH 0x00480600
void si_SetObjectPos(HOBJECT hObj, LTVector *pos)
{
	if (!hObj)
		return;

	FullMoveObject(g_pServerMgr, hObj, pos,
		MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS|MO_TELEPORT);
}

// FUNCTION: LITHTECH 0x00480620
void si_SetObjectState(HOBJECT hObj, int state)
{
	if (!hObj)
		return;

	// Translate the state to internal flags.
	uint32 stateFlags;
	switch (state)
	{
		case OBJSTATE_INACTIVE:
			stateFlags = IFLAG_INACTIVE;
			break;

		case OBJSTATE_INACTIVE_TOUCH:
			stateFlags = IFLAG_INACTIVE_TOUCH;
			break;

		case OBJSTATE_AUTODEACTIVATE_NOW:
			if (!g_bAutoDeactivate)
				return;
			stateFlags = IFLAG_AUTODEACTIVATED;
			break;

		default:
			stateFlags = 0;
			break;
	}

	sm_SetObjectStateFlags(g_pServerMgr, hObj, stateFlags);
}

// FUNCTION: LITHTECH 0x004806a0
int si_GetObjectState(HOBJECT hObj)
{
	if (!hObj)
		return OBJSTATE_INACTIVE;

	if (hObj->m_InternalFlags & IFLAG_INACTIVE)
		return OBJSTATE_INACTIVE;
	else if (hObj->m_InternalFlags & IFLAG_INACTIVE_TOUCH)
		return OBJSTATE_INACTIVE_TOUCH;
	else
		return OBJSTATE_ACTIVE;
}

// FUNCTION: LITHTECH 0x004806d0
LTRESULT si_GetObjectRotation(HOBJECT hObj, LTRotation *pRotation)
{
	if (!hObj)
	{
		pRotation->Init();
		RETURN_ERROR(1, ILTPhysics::GetObjectRotation, LT_ERROR);
	}

	*pRotation = hObj->m_Rotation;
	return LT_OK;
}

LTRESULT si_DoObjectRotation(HOBJECT hObj, LTRotation *pRotation, LTBOOL bSnap);

// FUNCTION: LITHTECH 0x00480750
LTRESULT si_SetObjectRotation(HOBJECT hObj, LTRotation *pRotation)
{
	return si_DoObjectRotation(hObj, pRotation, LTTRUE);
}

// FUNCTION: LITHTECH 0x00480770
LTRESULT si_DoObjectRotation(HOBJECT hObj, LTRotation *pRotation, LTBOOL bSnap)
{
	MoveState moveState;

	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::SetObjectRotation, LT_ERROR);
	}

	// Avoid setting the change flags if they're the same.
	if (hObj->m_Rotation == *pRotation)
		return LT_OK;

	if (hObj->HasWorldModel())
	{
		moveState.Setup(&g_pServerMgr->m_World.m_WorldTree, g_pServerMgr->m_MoveAbstract, hObj, hObj->m_BPriority);
		RotateWorldModel(&moveState, pRotation, !bSnap);
	}
	else
	{
		hObj->m_Rotation = *pRotation;
	}

	SetObjectChangeFlags(g_pServerMgr, hObj, CF_ROTATION | ((bSnap) ? CF_SNAPROTATION : 0));
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004809c0
LTRESULT si_RotateObject(HOBJECT hObj, LTRotation *pRotation)
{
	return si_DoObjectRotation(hObj, pRotation, LTFALSE);
}

// FUNCTION: LITHTECH 0x004809e0
void si_ScaleObject(HOBJECT hObj, LTVector *pNewScale)
{
	hObj->m_Scale = *pNewScale;
	SetObjectChangeFlags(g_pServerMgr, hObj, CF_SCALE);
}

// FUNCTION: LITHTECH 0x00480af0
LTRESULT si_GetObjectScale(HOBJECT hObj, LTVector *pScale)
{
	if (!hObj || !pScale)
		RETURN_ERROR(1, ILTPhysics::GetObjectScale, LT_ERROR);

	*pScale = hObj->m_Scale;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00480b60
LTRESULT si_TeleportObject(HOBJECT hObj, LTVector *pNewPos)
{
	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::TeleportObject, LT_INVALIDPARAMS);
	}

	si_SetObjectPos(hObj, pNewPos);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00480bb0
LTRESULT si_CreateAttachment(HOBJECT hParent, HOBJECT hChild, char *pSocketName,
	LTVector *pOffset, LTRotation *pRotationOffset, HATTACHMENT *hAttachment)
{
	LTVector vPos;
	LTRotation rRot;

	if (!hParent || !hChild)
		RETURN_ERROR(1, ILTPhysics::CreateAttachment, LT_INVALIDPARAMS);

	// Get the node index.
	uint32 nodeIndex = (uint32)-1;
	if (hParent->m_ObjectType == OT_MODEL && pSocketName)
	{
		Model *pModel = ((ModelInstance*)hParent)->GetModelDB();
		if (!pModel)
		{
			RETURN_ERROR(1, ILTPhysics::CreateAttachment, LT_INVALIDDATA);
		}

		if (!pModel->FindSocket(pSocketName, &nodeIndex))
		{
			// Look for a node if we can't find it in a socket
			if (!pModel->FindNode(pSocketName, &nodeIndex))
			{
				RETURN_ERROR(1, ILTPhysics::CreateAttachment, LT_NODENOTFOUND);
			}
			else
			{
				// Move it past the end of the socket list since it's a node attachment
				nodeIndex += pModel->NumSockets();
			}
		}
	}

	// Set it up.
	Attachment *pAttachment;
	LTRESULT dResult = om_CreateAttachment(&g_pServerMgr->m_ObjectMgr, hParent, hChild->m_ObjectID,
		nodeIndex, pOffset, pRotationOffset, &pAttachment);
	if (dResult != LT_OK)
		return dResult;

	// Move the child to its attachment position.
	GetAttachmentTransform(hParent, pAttachment, vPos, rRot);
	si_SetObjectPos(hChild, &vPos);
	si_SetObjectRotation(hChild, &rRot);

	SetObjectChangeFlags(g_pServerMgr, hParent, CF_ATTACHMENTS);

	hChild->m_InternalFlags |= IFLAG_ATTACHED;
	SetObjectChangeFlags(g_pServerMgr, hChild, CF_ATTACHMENTS);

	if (hAttachment)
		*hAttachment = (HATTACHMENT)pAttachment;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00480f30
LTRESULT si_RemoveAttachment(HATTACHMENT hAttachment)
{
	Attachment *pAttachment = (Attachment*)hAttachment;
	if (!pAttachment)
		RETURN_ERROR(1, ILTPhysics::RemoveAttachment, LT_INVALIDPARAMS);

	LTObject *pParent = sm_FindObject(g_pServerMgr, pAttachment->m_nParentID);
	if (!pParent)
		RETURN_ERROR(1, ILTPhysics::RemoveAttachment, LT_ERROR);

	LTRESULT dResult = om_RemoveAttachment(&g_pServerMgr->m_ObjectMgr, pParent, pAttachment);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(g_pServerMgr, pParent, CF_ATTACHMENTS);

		// Tell the child to send an update
		LTObject *pChild = sm_FindObject(g_pServerMgr, pAttachment->m_nChildID);
		if (pChild)
		{
			pChild->m_InternalFlags &= ~IFLAG_ATTACHED;
			SetObjectChangeFlags(g_pServerMgr, pChild, CF_ATTACHMENTS);
		}
	}
	else
	{
		RETURN_ERROR(1, ILTPhysics::RemoveAttachment, LT_NOTFOUND);
	}

	return dResult;
}

// FUNCTION: LITHTECH 0x00481210
LTRESULT si_FindAttachment(HOBJECT hParent, HOBJECT hChild, HATTACHMENT *hAttachment)
{
	if (!hParent || !hChild || !hAttachment)
	{
		RETURN_ERROR(1, ILTPhysics::FindAttachment, LT_INVALIDPARAMS);
	}

	*hAttachment = LTNULL;
	for (Attachment *pCur = hParent->m_Attachments; pCur; pCur = pCur->m_pNext)
	{
		if (sm_FindObject(g_pServerMgr, pCur->m_nChildID) == hChild)
		{
			*hAttachment = (HATTACHMENT)pCur;
			return LT_OK;
		}
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x004812b0
uint8 si_GetBlockingPriority(HOBJECT hObj)
{
	if (hObj)
		return hObj->m_BPriority;
	else
		return 0;
}

// FUNCTION: LITHTECH 0x004812d0
LTRESULT si_ClipSprite(HOBJECT hObj, HPOLY hPoly)
{
	if (!hObj)
		return LT_ERROR;

	if (hObj->m_ObjectType != OT_SPRITE)
		return LT_ERROR;

	// Only change if different.
	if (((SpriteInstance*)hObj)->m_ClipperPoly != hPoly)
	{
		((SpriteInstance*)hObj)->m_ClipperPoly = hPoly;
		SetObjectChangeFlags(g_pServerMgr, hObj, CF_MODELINFO);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004813f0
void si_SetBlockingPriority(HOBJECT hObj, uint8 pri)
{
	if (hObj)
		hObj->m_BPriority = pri;
}

// FUNCTION: LITHTECH 0x00481410
void si_TiltToPlane(HOBJECT hObj, LTVector *pNormal)
{
	LTVector q;

	// Get slope along acceleration...
	q = pNormal->Cross(hObj->m_Acceleration);
	if (q.MagSqr() > 0.001f)
	{
		q.Norm();
		LTVector slopeAccel = q.Cross(*pNormal);

		// Fix acceleration along slope...
		hObj->m_Acceleration = slopeAccel * hObj->m_Acceleration.Mag();
	}
}

// FUNCTION: LITHTECH 0x004815a0
void si_GetLightColor(HOBJECT hObject, float *r, float *g, float *b)
{
	if (!hObject)
		return;

	si_GetObjectColor(hObject, r, g, b, LTNULL);
}

// FUNCTION: LITHTECH 0x004815d0
void si_SetLightColor(HOBJECT hObject, float r, float g, float b)
{
	if (!hObject)
		return;

	si_SetObjectColor(hObject, r, g, b, (float)hObject->m_ColorA * MATH_ONE_OVER_255);
}

// FUNCTION: LITHTECH 0x00481610
float si_GetLightRadius(HOBJECT hObj)
{
	if (hObj->m_ObjectType == OT_LIGHT)
		return ((DynamicLight*)hObj)->m_LightRadius;
	else
		return 1.0f;
}

// FUNCTION: LITHTECH 0x00481630
void si_SetLightRadius(HOBJECT hObj, float radius)
{
	if (hObj->m_ObjectType == OT_LIGHT)
	{
		((DynamicLight*)hObj)->m_LightRadius = radius;
		SetObjectChangeFlags(g_pServerMgr, hObj, CF_RENDERINFO);
	}
}

// FUNCTION: LITHTECH 0x00481730
LTRESULT si_GetModelCommandString(HOBJECT hObj, char *pStr, uint32 maxLen)
{
	Model *pModel;

	if (!hObj || !pStr || hObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR(1, ILTPhysics::GetModelCommandString, LT_INVALIDPARAMS);

	pModel = ((ModelInstance*)hObj)->GetModelDB();
	if (!pModel->m_CommandString)
		RETURN_ERROR(1, ILTPhysics::GetModelCommandString, LT_ERROR);

	strncpy(pStr, pModel->m_CommandString, maxLen);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004817e0
void si_SetModelPlaying(HOBJECT hObj, LTBOOL bPlaying)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
	{
		LTAnimTracker *pTracker = &pInst->m_AnimTracker;
		if (!!(pTracker->m_Flags & AT_PLAYING) != !!bPlaying)
		{
			if (bPlaying)
				pTracker->m_Flags |= AT_PLAYING;
			else
				pTracker->m_Flags &= ~AT_PLAYING;

			SetObjectChangeFlags(g_pServerMgr, pInst, CF_MODELINFO);
		}
	}
}

// FUNCTION: LITHTECH 0x00481920
LTBOOL si_GetModelPlaying(HOBJECT hObj)
{
	ModelInstance *pInst = (ModelInstance*)hObj;

	if (pInst->m_ObjectType == OT_MODEL)
		return pInst->m_AnimTracker.m_Flags & AT_PLAYING;

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00481940
LTBOOL si_GetModelFilenames(HOBJECT hObj, char *pFilename, int fileBufLen, char *pSkinName, int skinBufLen)
{
	if (!hObj)
		return LTFALSE;

	if (hObj->m_ObjectType != OT_MODEL)
		return LTFALSE;

	if (pFilename && fileBufLen > 0)
	{
		if (hObj->sd->m_pFile)
			strncpy(pFilename, sf_GetUsedFilename(&g_pServerMgr->m_FileMgr, hObj->sd->m_pFile), fileBufLen - 1);
		else
			pFilename[0] = 0;
	}

	if (pSkinName && skinBufLen > 0)
	{
		if (hObj->sd->m_pSkins[0])
			strncpy(pSkinName, sf_GetUsedFilename(&g_pServerMgr->m_FileMgr, hObj->sd->m_pSkins[0]), skinBufLen - 1);
		else
			pSkinName[0] = 0;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x004819f0
LTBOOL si_GetModelFilenames2(HOBJECT hObj, ObjectCreateStruct *pStruct)
{
	int i;

	if (!hObj)
		return LTFALSE;

	if (hObj->m_ObjectType != OT_MODEL)
		return LTFALSE;

	if (hObj->sd->m_pFile)
		strncpy(pStruct->m_Filename, sf_GetUsedFilename(&g_pServerMgr->m_FileMgr, hObj->sd->m_pFile), MAX_CS_FILENAME_LEN - 1);
	else
		pStruct->m_Filename[0] = 0;

	for (i = 0; i < MAX_MODEL_TEXTURES; i++)
	{
		if (hObj->sd->m_pSkins[i])
			strncpy(pStruct->m_SkinNames[i], sf_GetUsedFilename(&g_pServerMgr->m_FileMgr, hObj->sd->m_pSkins[i]), MAX_CS_FILENAME_LEN - 1);
		else
			pStruct->m_SkinNames[i][0] = 0;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00481ab0
uint32 si_GetObjectContainers(HOBJECT hObj, HOBJECT *pContainerList, uint32 *pFlagList, uint32 maxListSize)
{
	uint32 count = 0;

	if (hObj)
	{
		LTLink *pListHead = &hObj->sd->m_Links;
		for (LTLink *pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
		{
			InterLink *pInterLink = (InterLink*)pCur->m_pData;

			// If we're not the owner, than this is a link for pObj being contained.
			if (pInterLink->m_Type == LINKTYPE_CONTAINER && hObj != pInterLink->m_pOwner)
			{
				if (count >= maxListSize)
					break;

				pContainerList[count] = pInterLink->m_pOwner;
				pFlagList[count] = 0;
				++count;
			}
		}
	}

	return count;
}

// FUNCTION: LITHTECH 0x00481b10
uint32 si_GetContainedObjects(HOBJECT hContainer, HOBJECT *pObjectList, uint32 *pFlagList, uint32 maxListSize)
{
	uint32 count = 0;

	if (hContainer)
	{
		LTLink *pListHead = &hContainer->sd->m_Links;
		for (LTLink *pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
		{
			InterLink *pInterLink = (InterLink*)pCur->m_pData;

			// If we're the owner, than this is a link for pObj containing something.
			if (pInterLink->m_Type == LINKTYPE_CONTAINER && hContainer == pInterLink->m_pOwner)
			{
				if (count >= maxListSize)
					break;

				pObjectList[count] = (HOBJECT)pInterLink->m_pOther;
				pFlagList[count] = 0;
				++count;
			}
		}
	}

	return count;
}


// Template code this object instantiated first (STLport string/set/map nodes of the child model link map).
// FUNCTION: LITHTECH 0x00481b70 ?deallocate@?$_STL_alloc_proxy@PAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@_STL@@U12@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@_STL@@QAEXPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@I@Z
// FUNCTION: LITHTECH 0x00481cc0 ?_M_erase@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@_STL@@AAEXPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x00481d60 ??1?$_Rb_tree_base@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00481d90 ?insert_unique@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$_Identity@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QAE?AU?$pair@U?$_Rb_tree_iterator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$_Nonconst_traits@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@_N@2@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@2@@Z
// FUNCTION: LITHTECH 0x00481f60 ?destroy_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@_STL@@IAEXPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x00481fe0 ?_M_create_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$_Identity@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@IAEPAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@2@@Z
// FUNCTION: LITHTECH 0x004820e0 ?_Construct@_STL@@YAXPAV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@1@ABV21@@Z
