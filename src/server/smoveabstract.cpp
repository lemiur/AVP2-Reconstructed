// Jupiter runtime/server/src/smoveabstract.cpp
// Talon's SMoveAbstract keeps its CServerMgr and inlines the s_object helpers.
#include <windows.h>		// before the StdLith headers (servermgr.h brings in lthread.h)
#include "bdefs.h"
#include "ltengineobjects.h"
#include "smoveabstract.h"
#include "s_object.h"
#include "interlink.h"
#include "counter.h"
#include "engine_vars.h"
#include "moveobject.h"




// FUNCTION: LITHTECH 0x0048ec10
void SMoveAbstract::SetObjectChangeFlags(LTObject *pObj, uint32 flags)
{
	::SetObjectChangeFlags(m_pServerMgr, pObj, flags);
}

// FUNCTION: LITHTECH 0x0048ed10
CollisionInfo *& SMoveAbstract::GetCollisionInfo()
{
	return m_pServerMgr->m_pCollisionInfo;
}

// FUNCTION: LITHTECH 0x0048ed20
void SMoveAbstract::DoTouchNotify(LTObject *pMain, LTObject *pTouching, LTVector &stopVel, float forceMag)
{
	if (m_pServerMgr->m_pCollisionInfo)
	{
		m_pServerMgr->m_pCollisionInfo->m_vStopVel = stopVel;
		m_pServerMgr->m_pCollisionInfo->m_hObject = (HOBJECT)pTouching;

		// Only the world has polies.
		if (pTouching && !pTouching->IsMainWorldModel() && !pTouching->HasWorldModel())
		{
			m_pServerMgr->m_pCollisionInfo->m_hPoly = INVALID_HPOLY;
		}

		pMain->sd->m_pObject->EngineMessageFn(MID_TOUCHNOTIFY, pTouching, forceMag);
	}
}

// FUNCTION: LITHTECH 0x0048edb0
void SMoveAbstract::DoCrush(LTObject *pObject, LTObject *pCrusher)
{
	pObject->sd->m_pObject->EngineMessageFn(MID_CRUSH, pCrusher, 0.0f);
}

// FUNCTION: LITHTECH 0x0048edd0
void SMoveAbstract::PutObjectInContainer(LTObject *pObj, LTObject *pContainer)
{
	pObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;

	// Link them together.
	CreateInterLink(m_pServerMgr, pContainer, pObj, LINKTYPE_CONTAINER);
}

// FUNCTION: LITHTECH 0x0048ee00
void SMoveAbstract::BreakContainerLinks(LTObject *pObj)
{
	BreakInterLinks(m_pServerMgr, pObj, LINKTYPE_CONTAINER, LTFALSE);
}

// Gets the attachment's world transform (moveobject, 0x00462510).
void GetAttachmentTransform(LTObject *pParent, Attachment *pAttachment, LTVector &vPos, LTRotation &rRot);

// FUNCTION: LITHTECH 0x0048ee20
void SMoveAbstract::MoveAttachments(MoveState *pState)
{
	Attachment *pAttachment;
	LTObject *pAttachedObj;
	LTRotation newRot;
	LTVector attachPos;
	uint32 dwFlags;

	if (!pState->m_pObj->m_Attachments)
		return;

	MoveState moveState;

	// Move the attachments.
	pAttachment = pState->m_pObj->m_Attachments;
	while (pAttachment)
	{
		pAttachedObj = sm_FindObject(m_pServerMgr, pAttachment->m_nChildID);
		if (pAttachedObj)
		{
			GetAttachmentTransform(pState->m_pObj, pAttachment, attachPos, newRot);

			// Teleport the attachment to the right spot (unless it's physical)...
			dwFlags = (pAttachedObj->m_Flags & (FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER)) ?
				MO_DETACHSTANDING | MO_MOVESTANDINGONS :
				MO_DETACHSTANDING | MO_MOVESTANDINGONS | MO_TELEPORT;

			moveState.Setup(pState->m_pWorldTree, pState->m_pAbstract, pAttachedObj, pState->m_BPriority);
			MoveObject(&moveState, attachPos, dwFlags);

			// Update its rotation..
			if (!newRot.Equals(pAttachedObj->m_Rotation, 0.00001f))
			{
				if (pAttachedObj->HasWorldModel() &&
					(pAttachedObj->m_Flags & (FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER)))
				{
					moveState.Setup(pState->m_pWorldTree, pState->m_pAbstract, pAttachedObj, pAttachedObj->m_BPriority);
					RotateWorldModel(&moveState, &newRot, LTTRUE);
				}
				else
				{
					pAttachedObj->m_Rotation = newRot;
				}
			}
		}

		pAttachment = pAttachment->m_pNext;
	}
}

// FUNCTION: LITHTECH 0x0048f000
LTBOOL SMoveAbstract::ShouldPushObject(MoveState *pState, LTObject *pPusher, LTObject *pPushee)
{
	return pState->m_BPriority > pPushee->m_BPriority && pPushee->IsMoveable();
}

// FUNCTION: LITHTECH 0x0048f030
void SMoveAbstract::CheckMaxPos(MoveState *pState, LTVector *pPos)
{
	if (g_DebugMaxPos > 0.0f && pPos->MagSqr() > g_DebugMaxPos * g_DebugMaxPos)
	{
		dsi_ConsolePrint("Server: position larger than 'DebugMaxPos'");
		dsi_ConsolePrint("Object class: %s, position (%.2f, %.2f, %.2f)", pState->m_pObj->sd->m_pClass->m_ClassName,
			pPos->x, pPos->y, pPos->z);
	}
}

// Folded with another `return 1` (0x004b22a0).
uint32 SMoveAbstract::IsServer()
{
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0048f0d0
LTBOOL SMoveAbstract::CanOptimizeObject(LTObject *pObj)
{
	return ::CanOptimizeObject(pObj);
}

// FUNCTION: LITHTECH 0x0048f110
char* SMoveAbstract::GetObjectClassName(LTObject *pObject)
{
	return pObject->sd->m_pClass->m_ClassName;
}

// Register allocation: VC6 keeps 0 in esi here and splits the CountAdder add (orig: immediate zeros, `add [ecx],eax`).
// Moving MoveState before the counter makes the size match but worsens the diff.
// FUNCTION: LITHTECH 0x0048f130
void FullMoveObject(CServerMgr *pServerMgr, LTObject *pObj, const LTVector *pP1, uint32 flags)
{
	++g_nMoveObjectCalls;
	CountAdder cntAdd(&g_Ticks_MoveObject);

	MoveState moveState;
	moveState.Setup(&pServerMgr->m_World.m_WorldTree, (MoveAbstract*)pServerMgr->m_MoveAbstract, pObj, pObj->m_BPriority);
	MoveObject(&moveState, *pP1, flags);
}

// FUNCTION: LITHTECH 0x0048f200
LTRESULT SMoveAbstract::GetGlobalForce(LTObject *pObj, LTVector *pForce)
{
	if (!pObj)
	{
		if (pForce)
			*pForce = m_pServerMgr->m_MotionState.m_Info.m_Force;
		return LT_OK;
	}

	if (pForce)
		*pForce = m_pServerMgr->m_MotionState.m_Info.m_Force;
	return LT_OK;
}
