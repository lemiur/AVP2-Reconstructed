// Jupiter runtime/shared/src/moveobject.cpp (Talon version: no player mover, no stair stepping;
// sphere physics (FLAG2_SPHEREPHYSICS) objects are moved by CSphereMover).
#include "bdefs.h"
#include <math.h>
#include <stdlib.h>
#include "moveobject.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "world_tree.h"
#include "collision.h"
#include "counter.h"


void obj_SetupWorldModelTransform(WorldModelInstance *pWorldModel);

LTBOOL CollideAgainstWorld(MoveState *pState, WorldBsp *pWorldBsp, LTObject *pWorldBspObj,
	LTObject *pObj, LTVector &P0, LTVector &P1, LTBOOL bSlide, LTBOOL bNotify);
LTVector GetPushawayPos(LTObject *pMover, LTObject *pBlocker, LTVector *pStartPos, LTVector *pDestPos,
	int32 *pPushPlane, float *pPlaneDist);
void GetSmallestPushaway(LTVector &moverMin, LTVector &moverMax, LTVector &moverStartMin, LTVector &moverStartMax,
	LTVector &blockerMin, LTVector &blockerMax, LTVector &pushAmount, int32 &pushPlane, LTVector &vMoveDelta);
LTBOOL IsSolidWorldBsp(LTObject *pObj);
void DetectAndProcessCollisions(MoveState *pState, const LTVector &startPos, const LTVector &destPos);
inline LTBOOL DoSolidWMCollision(MoveState *pState, LTObject *pTestObj, LTVector &startPos, LTVector &destPos, LTBOOL bNotify, LTBOOL &bCollision);
inline LTBOOL DoSolidBBoxCollision(MoveState *pState, LTObject *pTestObj, LTVector &startPos, LTVector &destPos);
LTBOOL CheckIntersectOnMovement(MoveState *pState, LTObject *pTestObj, LTBOOL bPushAway);
inline void GetMovementBox(LTVector *pvMoveMin, LTVector *pvMoveMax, LTVector *pvDeltaPos, LTVector *pMinBox, LTVector *pMaxBox);
void GetBoxIntersection(LTVector *pMin1, LTVector *pMax1, LTVector *pMin2, LTVector *pMax2,
	LTVector *pOutMin, LTVector *pOutMax);
void FindObjectsCB(WorldTreeObj *pTreeObj, void *pCBUser);
int CompareObjectDists(const void *pA, const void *pB);
LTBOOL DoBoxesIntersect(LTVector &min1, LTVector &max1, LTVector &min2, LTVector &max2, float fTolerance);
void MaybeCollideWorldModel(MoveState *pState, LTObject *pTestObj);
inline LTBOOL MaybeCollide(MoveState *pState, LTObject *pTestObj, LTVector *pUnused, float *pUnused2);
void quat_ConvertToMatrix(const float *pQuat, float mat[4][4]);
void GrowDim(MoveState *pState, int32 nDim, float *pNewDim);
void CollideWorldModelCB(WorldTreeObj *pObj, void *pUser);


// GLOBAL: LITHTECH 0x004e4558
uint32 g_Ticks_MoveObject;
// GLOBAL: LITHTECH 0x004e455c
uint32 g_nMoveObjectCalls;


#define MAX_CARRIED_OBJECTS			32


struct StartPosInfo
{
	LTObject	*m_pObj;
	LTVector	m_vRelPos;
};


// GLOBAL: LITHTECH 0x004e4560
static LTVector g_PushPlanes[6] =
{
	LTVector(1.0f, 0.0f, 0.0f),
	LTVector(-1.0f, 0.0f, 0.0f),
	LTVector(0.0f, 1.0f, 0.0f),
	LTVector(0.0f, -1.0f, 0.0f),
	LTVector(0.0f, 0.0f, 1.0f),
	LTVector(0.0f, 0.0f, -1.0f)
};

// FUNCTION: LITHTECH 0x0045d000 _$E2
// FUNCTION: LITHTECH 0x0045d010 _$E1


// [kls 9/2/99] Is this a solid piece of the world?
// FUNCTION: LITHTECH 0x0045d0d0
LTBOOL IsSolidWorld(LTObject *pObj)
{
	if(pObj->IsMainWorldModel() || ((pObj->m_Flags & FLAG_SOLID) && pObj->HasWorldModel()))
	{
		return LTTRUE;
	}

	return LTFALSE;
}

// Detaches the object from whatever it was standing on.
// FUNCTION: LITHTECH 0x0045d110
void DetachObjectStanding(LTObject *pObj)
{
	if(pObj->m_pStandingOn)
	{
		dl_Remove(&pObj->m_StandingOnLink);
		pObj->m_pStandingOn = LTNULL;
	}

	pObj->m_pNodeStandingOn = LTNULL;
}

// FUNCTION: LITHTECH 0x0045d150
void DetachObjectsStandingOn(LTObject *pObj)
{
	LTLink *pCur, *pNext;
	LTObject *pStandingObj;

	pCur = pObj->m_ObjectsStandingOn.m_pNext;
	while(pCur != &pObj->m_ObjectsStandingOn)
	{
		pNext = pCur->m_pNext;

		pStandingObj = (LTObject*)pCur->m_pData;
		DetachObjectStanding(pStandingObj);
		pStandingObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;

		pCur = pNext;
	}
}

// Attaches the object to another one (standing on it).
// FUNCTION: LITHTECH 0x0045d190
void SetObjectStanding(LTObject *pObj, LTObject *pStandingOn, Node *pNode)
{
	DetachObjectStanding(pObj);

	if(pStandingOn)
	{
		// Objects that follow the BSP can't be stood on.
		if(pStandingOn->m_Flags2 & FLAG2_ORIENTMOVEMENT)
			return;

		dl_Insert(&pStandingOn->m_ObjectsStandingOn, &pObj->m_StandingOnLink);
	}

	pObj->m_pStandingOn = pStandingOn;
	pObj->m_pNodeStandingOn = pNode;
}


// FUNCTION: LITHTECH 0x0045d1e0
void RetransformWorldModel(WorldModelInstance *pWorldModel)
{
	obj_SetupWorldModelTransform(pWorldModel);
	w_TransformWorldModel(pWorldModel, &pWorldModel->m_Transform, !!(pWorldModel->m_Flags & FLAG_BOXPHYSICS));
}


inline void SetObjectBoundingBox(LTObject *pObj, LTBOOL bTransformWorldModel)
{
	if(bTransformWorldModel && pObj->HasWorldModel())
	{
		RetransformWorldModel((WorldModelInstance*)pObj);
	}
}


// FUNCTION: LITHTECH 0x0045d210
void SetupWorldModelDims(WorldModelInstance *pInstance, LTMatrix *pTransform)
{
	LTVector pts[8], minPt, maxPt;
	LTVector vDims;
	int32 i;
	WorldBsp *pWorldBsp;

	if(pInstance->m_pOriginalBsp->IsUntransformed())
		return;

	pWorldBsp = pInstance->m_pOriginalBsp;

	// Set its intial dims to the unrotated WorldModel dims.
	vDims = pWorldBsp->m_MaxBox - pWorldBsp->m_MinBox;
	vDims *= 0.5f;

	const LTVector &vPos = pInstance->GetPos();
	pts[0].Init(vPos.x+vDims.x, vPos.y+vDims.y, vPos.z+vDims.z);
	pts[1].Init(vPos.x+vDims.x, vPos.y-vDims.y, vPos.z+vDims.z);
	pts[2].Init(vPos.x+vDims.x, vPos.y+vDims.y, vPos.z-vDims.z);
	pts[3].Init(vPos.x+vDims.x, vPos.y-vDims.y, vPos.z-vDims.z);

	pts[4].Init(vPos.x-vDims.x, vPos.y+vDims.y, vPos.z+vDims.z);
	pts[5].Init(vPos.x-vDims.x, vPos.y-vDims.y, vPos.z+vDims.z);
	pts[6].Init(vPos.x-vDims.x, vPos.y+vDims.y, vPos.z-vDims.z);
	pts[7].Init(vPos.x-vDims.x, vPos.y-vDims.y, vPos.z-vDims.z);

	minPt.Init((float)MAX_CREAL, (float)MAX_CREAL, (float)MAX_CREAL);
	maxPt.Init((float)-MAX_CREAL, (float)-MAX_CREAL, (float)-MAX_CREAL);

	for(i=0; i < 8; i++)
	{
		MatVMul_InPlace_H(pTransform, &pts[i]);
		VEC_MIN(minPt, minPt, pts[i]);
		VEC_MAX(maxPt, maxPt, pts[i]);
	}

	vDims = (maxPt - minPt) * 0.5f;
	pInstance->SetDims(vDims);
}


// FUNCTION: LITHTECH 0x0045d570
void InitialWorldModelRotate(WorldModelInstance *pInstance)
{
	obj_SetupWorldModelTransform(pInstance);
	SetupWorldModelDims(pInstance, &pInstance->m_Transform);
	w_TransformWorldModel(pInstance, &pInstance->m_Transform, !!(pInstance->m_Flags & FLAG_BOXPHYSICS));
}


// Is this a WorldModel that uses full (BSP) physics?
// The out-of-line copies at 0x0045e960 (IsWorldModel) and 0x0045e990 (DoObjectsIntersect) come from
// DetectAndProcessCollisions running out of inline budget (wave 7: it does now too).
// FUNCTION: LITHTECH 0x0045e960 ?IsWorldModel@@YAIPAVLTObject@@@Z
inline LTBOOL IsWorldModel(LTObject *pObj)
{
	if(pObj->HasWorldModel() && !(pObj->m_Flags & FLAG_BOXPHYSICS))
	{
		return LTTRUE;
	}

	return LTFALSE;
}


// Determines if the two objects intersect.  Sets bWorldModel to LTTRUE if one of them is a WorldModel.
// FUNCTION: LITHTECH 0x0045e990 ?DoObjectsIntersect@@YAIPAVLTObject@@0PAV?$_CVector@M@@111MPAI@Z
inline LTBOOL DoObjectsIntersect(LTObject *pObj1, LTObject *pObj2,
	LTVector *pObj1MinBox, LTVector *pObj1MaxBox, LTVector *pObj2MinBox, LTVector *pObj2MaxBox,
	float boxTolerance, LTBOOL *bWorldModel)
{
	WorldModelInstance *pWorldModel;
	LTVector *pOtherMinBox, *pOtherMaxBox;

	if(!DoBoxesIntersect(*pObj1MinBox, *pObj1MaxBox, *pObj2MinBox, *pObj2MaxBox, boxTolerance))
		return LTFALSE;

	// If either of them is a WorldModel, do a more extensive test.
	if(pObj1->HasWorldModel() && !(pObj1->m_Flags & FLAG_BOXPHYSICS))
	{
		pWorldModel = (WorldModelInstance*)pObj1;
		pOtherMinBox = pObj2MinBox;
		pOtherMaxBox = pObj2MaxBox;
	}
	else if(pObj2->HasWorldModel() && !(pObj2->m_Flags & FLAG_BOXPHYSICS))
	{
		pWorldModel = (WorldModelInstance*)pObj2;
		pOtherMinBox = pObj1MinBox;
		pOtherMaxBox = pObj1MaxBox;
	}
	else
	{
		pWorldModel = LTNULL;
	}

	// Ok, one of them was a WorldModel, figure out if they really touch.
	if(pWorldModel)
	{
		if(bWorldModel)
			*bWorldModel = LTTRUE;

		if(DoesBoxIntersectBSP(pWorldModel->m_pValidBsp->GetRootNode(), *pOtherMinBox, *pOtherMaxBox))
		{
			return LTTRUE;
		}
		else
		{
			return LTFALSE;
		}
	}
	else
	{
		if(bWorldModel)
			*bWorldModel = LTFALSE;

		return LTTRUE;
	}
}


// This will move MAX_CARRIED_OBJECTS objects standing on the moving object. If there are more than MAX_CARRIED_OBJECTS, 
// then they won't be moved
// FUNCTION: LITHTECH 0x0045d5b0
void MoveObject(MoveState *pState, LTVector P1, uint32 flags)
{
	LTObject *pWasStandingOnObj;
	LTObject *pObjOn;
	LTVector startPos, newPos, vOldPos, vDelta;
	float fDistSqr;
	LTLink *pCur, *pNext;
	StartPosInfo startPosInfo[MAX_CARRIED_OBJECTS];
	int32 nNumObjectsStanding;
	MoveState moveState;
	CollisionInfo collisionInfo, *pOldCollisionInfo;

	pState->SetupCall();

	// Don't move again if we're already moving.  Make sure we're moveable.
	if((pState->m_pObj->m_InternalFlags & IFLAG_MOVING) || !pState->m_pObj->IsMoveable())
	{
		return;
	}

	// Don't bother if it isn't really moving.
	vOldPos = pState->m_pObj->GetPos();
	vDelta = P1 - pState->m_pObj->GetPos();
	fDistSqr = vDelta.MagSqr();
	if(!(pState->m_pObj->m_Flags2 & FLAG2_SPHEREPHYSICS) || pState->m_pObj->m_pStandingOn)
	{
		if(fDistSqr < 0.01f)
			return;
	}

	// Save some work if we can.
	if(pState->m_pAbstract->CanOptimizeObject(pState->m_pObj) || (flags & MO_NOSLIDING))
	{
		if(flags & MO_SETCHANGEFLAG)
		{
			pState->m_pAbstract->SetObjectChangeFlags(pState->m_pObj, CF_POSITION);
		}

		pState->m_pObj->SetPos(P1);

		if(flags & MO_NOSLIDING)
		{
			SetObjectBoundingBox(pState->m_pObj, LTTRUE);
			pState->m_pWorldTree->InsertObject(pState->m_pObj, NOA_Objects);
		}
		return;
	}

	pState->m_pAbstract->CheckMaxPos(pState, &P1);

	// Stack the collision info so this function is re-entrant
	pOldCollisionInfo = pState->m_pAbstract->GetCollisionInfo();
	pState->m_pAbstract->GetCollisionInfo() = &collisionInfo;
	memset(&collisionInfo, 0, sizeof(CollisionInfo));

	// Break its links to containers / contained objects.
	pState->m_pAbstract->BreakContainerLinks(pState->m_pObj);

	// Set the moving flag so we can't be moved by things we push. 
	// Also set the apply physics flag so we do physics calcs next time around.
	pState->m_pObj->m_InternalFlags |= IFLAG_MOVING | IFLAG_APPLYPHYSICS;

	// If object is teleporting, then it doesn't really need to travel from somewhere...
	if(flags & MO_TELEPORT)
	{
		startPos = P1;
	}
	else
	{
		startPos = pState->m_pObj->GetPos();
	}

	// Remember who I'm standing on...
	if(pState->m_pObj->m_pStandingOn && !pState->m_pObj->m_pNodeStandingOn && !(flags & MO_TELEPORT))
	{
		pWasStandingOnObj = pState->m_pObj->m_pStandingOn;
	}
	else
	{
		pWasStandingOnObj = LTNULL;
	}

	nNumObjectsStanding = -1;
	if(flags & MO_MOVESTANDINGONS)
	{
		// Transfer the objects that are on object now, so we can move them later...
		pCur = pState->m_pObj->m_ObjectsStandingOn.m_pNext;

		while(pCur != &pState->m_pObj->m_ObjectsStandingOn)
		{
			pNext = pCur->m_pNext;
			pObjOn = (LTObject*)pCur->m_pData;
			pCur = pNext;

			if(pObjOn->m_Flags & FLAG_DONTFOLLOWSTANDING)
			{
				DetachObjectStanding(pObjOn);
			}
			else
			{
				if(nNumObjectsStanding < MAX_CARRIED_OBJECTS)
				{
					nNumObjectsStanding++;
					startPosInfo[nNumObjectsStanding].m_pObj = pObjOn;
					startPosInfo[nNumObjectsStanding].m_vRelPos = pObjOn->GetPos() - pState->m_pObj->GetPos();
				}
				else
				{
					// Too many...
					DetachObjectStanding(pObjOn);
				}
			}
		}
	}
	else
	{
		DetachObjectsStandingOn(pState->m_pObj);
	}

	// Check collisions with other objects.  We still do this even if teleporting, cuz we need
	// to be put into containers...
	if(pState->m_pObj->m_Flags & (FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER))
	{
		if(!(flags & MO_TELEPORT))
		{
			// Don't move along axes that barely changed.
			if(fabs(vDelta.x) < 0.001f)
				vDelta.x = 0.0f;
			if(fabs(vDelta.y) < 0.001f)
				vDelta.y = 0.0f;
			if(fabs(vDelta.z) < 0.001f)
				vDelta.z = 0.0f;

			P1 = startPos + vDelta;
		}

		if(flags & MO_DETACHSTANDING)
			DetachObjectStanding(pState->m_pObj);

		// Set curPos to the destination .. it'll be modified below.
		pState->m_pObj->SetPos(P1);

		// Reset its bounding box coordinates to reflect the test position.
		SetObjectBoundingBox(pState->m_pObj, LTTRUE);

		// Collide it with other objects.
		DetectAndProcessCollisions(pState, startPos, pState->m_pObj->GetPos());

		// Set their final new bounding box coordinates..
		if(pState->m_pObj->GetPos() != P1)
			SetObjectBoundingBox(pState->m_pObj, LTTRUE);
	}
	else
	{
		pState->m_pObj->SetPos(P1);

		// Set their final new bounding box coordinates..
		SetObjectBoundingBox(pState->m_pObj, LTTRUE);
	}

	// Done moving it around...
	pState->m_pWorldTree->InsertObject(pState->m_pObj, NOA_Objects);

	if(flags & MO_MOVESTANDINGONS)
	{
		// Offset everyone that was standing on us by the same amount.
		while(nNumObjectsStanding >= 0)
		{
			pObjOn = startPosInfo[nNumObjectsStanding].m_pObj;
			newPos = pState->m_pObj->GetPos() + startPosInfo[nNumObjectsStanding].m_vRelPos;
			
			moveState.Inherit(pState, pObjOn);
			moveState.m_BPriority = pObjOn->m_BPriority;
			MoveObject(&moveState, newPos, 
				MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS|(flags & MO_TELEPORT));

			// Only reset the object to be standing on us if it's still near...
			if(DoBoxesIntersect(pObjOn->m_MinBox, pObjOn->m_MaxBox, 
				pState->m_pObj->m_MinBox, pState->m_pObj->m_MaxBox, -0.01f))
			{
				SetObjectStanding(pObjOn, pState->m_pObj, LTNULL); // It's still standing on me...
			}
			
			nNumObjectsStanding--;
		}
	}

	if(pWasStandingOnObj && !pWasStandingOnObj->HasWorldModel())
	{
		if(DoObjectsIntersect(pWasStandingOnObj, pState->m_pObj, &pWasStandingOnObj->m_MinBox, 
			&pWasStandingOnObj->m_MaxBox, &pState->m_pObj->m_MinBox, &pState->m_pObj->m_MaxBox, 
			-0.01f, LTNULL))
		{
			SetObjectStanding(pState->m_pObj, pWasStandingOnObj, LTNULL);
		}
	}

	// Set its change flag.
	if((pState->m_pObj->GetPos() - vOldPos).MagSqr() > 0.01f && (flags & MO_SETCHANGEFLAG))
	{
		pState->m_pAbstract->SetObjectChangeFlags(pState->m_pObj, CF_POSITION);

		if(flags & MO_TELEPORT)
			pState->m_pAbstract->SetObjectChangeFlags(pState->m_pObj, CF_TELEPORT);
	}
	
	pState->m_pAbstract->MoveAttachments(pState);

	// We're done moving.
	pState->m_pObj->m_InternalFlags &= ~IFLAG_MOVING;

	pState->m_pAbstract->GetCollisionInfo() = pOldCollisionInfo;
}


//---------------------------------------------------------------------------//
// Do these boxes intersect to within the provided tolerance?
// FUNCTION: LITHTECH 0x0045dd50
LTBOOL DoBoxesIntersect(LTVector &min1, LTVector &max1, LTVector &min2, LTVector &max2, float fTolerance)
{
	if(	min1.x - max2.x >= -fTolerance || max1.x - min2.x <= fTolerance ||
		min1.y - max2.y >= -fTolerance || max1.y - min2.y <= fTolerance ||
		min1.z - max2.z >= -fTolerance || max1.z - min2.z <= fTolerance )
	{
		return LTFALSE;
	}
	else
	{
		return LTTRUE;
	}
}


// Moves the object if the position changed and updates its bounding box.
inline void MoveObjectTo(LTObject *pObj, const LTVector &vPos)
{
	if(vPos != pObj->GetPos())
	{
		pObj->SetPos(vPos);
		SetObjectBoundingBox(pObj, LTTRUE);
	}
}


// Determines if either object is a world model...
inline LTBOOL IsWorldModel(LTObject *pObj1, LTObject *pObj2)
{
	// If either of them is a WorldModel, do a more extensive test.
	if(pObj1->HasWorldModel() && !(pObj1->m_Flags & FLAG_BOXPHYSICS))
	{
		return LTTRUE;
	}
	else if(pObj2->HasWorldModel() && !(pObj2->m_Flags & FLAG_BOXPHYSICS))
	{
		return LTTRUE;
	}

	return LTFALSE;
}


// Sphere-physics movement box (name invented; always inlined, so the exe has no copy). Wave 7: written in
// DetectAndProcessCollisions this block makes its own size (and so its inline budget, 2x size) ~200u too big;
// as a helper the budget model puts DAPC within ~60u of the original (tools/inline_budget.py).
inline void GetSphereMoveBox(MoveState *pState, float fRadius)
{
	float fDiameter;

	fDiameter = fRadius + fRadius;
	if(pState->m_pStartPos->x > pState->m_vDestPos.x)
	{
		pState->m_vMoveMin.x = pState->m_vDestPos.x - fDiameter;
		pState->m_vMoveMax.x = fDiameter + pState->m_pStartPos->x;
	}
	else
	{
		pState->m_vMoveMin.x = pState->m_pStartPos->x - fDiameter;
		pState->m_vMoveMax.x = fDiameter + pState->m_vDestPos.x;
	}

	if(pState->m_pStartPos->y > pState->m_vDestPos.y)
	{
		pState->m_vMoveMin.y = pState->m_vDestPos.y - fDiameter;
		pState->m_vMoveMax.y = fDiameter + pState->m_pStartPos->y;
	}
	else
	{
		pState->m_vMoveMin.y = pState->m_pStartPos->y - fDiameter;
		pState->m_vMoveMax.y = fDiameter + pState->m_vDestPos.y;
	}

	if(pState->m_pStartPos->z > pState->m_vDestPos.z)
	{
		pState->m_vMoveMin.z = pState->m_vDestPos.z - fDiameter;
		pState->m_vMoveMax.z = fDiameter + pState->m_pStartPos->z;
	}
	else
	{
		pState->m_vMoveMin.z = pState->m_pStartPos->z - fDiameter;
		pState->m_vMoveMax.z = fDiameter + pState->m_vDestPos.z;
	}

}


// Wave 6 phase 2 decoded the exe's shape (rewrite in E:\AVP2Source\notes\wave6, adopted in wave 7): GetMovementBox,
// DoSolidBBoxCollision and MaybeCollide are `inline` and defined later in the file; loop 1 inlines MaybeCollide,
// loop 2 calls it; the first GetMovementBox is inlined, the last one isn't; the sphere block declares its
// SphereMoveInfo inside the block (its m_GroundNormals array is built by the `vector constructor iterator').
// Wave 7 (tools/inline_budget.py, aliases 45e960=IsWorldModel(LTObject*), 45e990=DoObjectsIntersect):
// - IsWorldModel(LTObject*) needs the braces: 41u is charged, the brace-less 39u version is free (<= 40u) and can
//   never be refused, but the exe calls it out of line from the inlined MaybeCollide.
// - With the sphere box in an inline helper the model and our build agree on every site; our out-of-line calls
//   differ from the exe's only by the `vector constructor iterator' (??_H, 49u, refused in the exe). The exe needs
//   B = 2384-2400u (ours 2516u: own size 1254u), i.e. ~60u less own code; with Jupiter's GetDims() in loop 2's
//   m_fMoveRadius (one more pending site; file-local accessor) 2456-2472u (ours 2520u, ~28u less own code).
//   --solve: one extra pending site anywhere after loop 2's Mag plus 21-44u less own code also works.
// - The exe passes two different stack locals (8 bytes apart) as MaybeCollide's unused pointer arguments, not
//   &vTemp/&vMin.x, so the locals differ from ours too. ALIGNED 655 -> 535 this wave (MaybeCollide's if/else).
// Wave 7 phase 2: the exe zeroes the 4th-argument local right after pHitObjects ([esp+0x54] at 0x45e2e8, passed
// to the out-of-line MaybeCollide in loop 2): `float fTemp; ... fTemp = 0.0f;` (535 -> 530). Budget model:
// GetDims() in all three places (both m_fMoveRadius and the sphere radius) gives 3 more pending sites and fixes
// the LTVector ctor count, but with our own size (B 2536u) it also inlines MaybeCollide in loop 2 (964); the exe
// needs B 2436-2480u with them, i.e. 28-50u less own code. A probe (GetDims x3 and three statements deleted, B
// 2482u) reproduces every exe out-of-line call yet scores 520: the remaining ~520 is frame layout (ours 0x5d4,
// exe 0x5bc), register choice and scheduling, not inlining. Tried as inline helpers (all refuted by the model or
// the build): the custom-test-objects loop (B -306u, overshoots), the whole sphere block (with and without
// GetSphereMoveBox), loop 2's reset block (ResetMoveInfo), a MoveBackToStart helper for the three
// "go back to the start" blocks; a pointer walk over objectArray.m_Objects (-5u only); the seven sphereInfo
// stores as a SphereMoveInfo(pState, &objectArray) constructor or a setup helper (fixes ??_H, but overshoots: the
// exe then needs ~35u *more* own code). With DoNonsolidCollision now inline and MaybeCollide's in-place vMin/vMax
// (its notes) the requirement is the same: -57..-77u own code without GetDims, which would also make MaybeCollide
// itself match (it is refused in loop 2 only below 527u).
// Audit: only ??_H (exe calls it out of line for m_GroundNormals) and one more out-of-line LTVector ctor (loop 1's
// m_vDeltaPos): inlining decisions. Most promising next idea: find the 28-50u of own code (an SDK macro instead
// of a written-out expression, or a block that was an inline helper in Talon) and then work the frame (the exe's
// vMin/vMax for GetBoxIntersection sit at esp+0x6c/0x78, MaybeCollide's 3rd/4th args at esp+0x5c/0x54).
// PARKED: remaining ~530 is frame/register layout; inlining differs by 2 calls (R11 model: 57-63u less own code, or one more pending site after loop 2's Mag and 18-40u less)
// STUB: LITHTECH 0x0045ddf0
void DetectAndProcessCollisions(MoveState *pState, const LTVector &startPos, const LTVector &destPos)
{
	int32 i, nRestarts;
	LTObject *pTestObj;
	LTObject *pHitObjects[2];
	LTVector vMin, vMax, vTemp;
	float fTemp;
	IntersectingObjectArray objectArray;

	pState->m_pStartPos = &startPos;
	pState->m_vDestPos = destPos;
	pState->m_vDeltaPos = destPos - startPos;
	pState->m_vMoveCenter = startPos + pState->m_vDeltaPos * 0.5f;
	pState->m_nRestart = 0.0f;
	pState->m_fMoveRadius = (0.5f * pState->m_vDeltaPos.Mag()) + pState->m_pObj->m_Dims.Mag();

	// Sphere physics objects do their own collisions.
	if(pState->m_pObj->m_Flags2 & FLAG2_SPHEREPHYSICS)
	{
		SphereMoveInfo sphereInfo;

		sphereInfo.m_pState = pState;
		sphereInfo.m_vStartPos = *pState->m_pStartPos;
		sphereInfo.m_vDestPos = pState->m_vDestPos;
		sphereInfo.m_pObj = pState->m_pObj;
		sphereInfo.m_fRadius = pState->m_pObj->m_Dims.y;
		sphereInfo.m_pObjects = &objectArray;
		objectArray.m_pState = pState;

		GetSphereMoveBox(pState, sphereInfo.m_fRadius);

		pState->m_pWorldTree->FindObjectsInBox(&pState->m_vMoveMin, &pState->m_vMoveMax,
			FindObjectsCB, &objectArray, NOA_Objects);

		MoveSphere(&sphereInfo);
		pState->m_pObj->SetPos(sphereInfo.m_vDestPos);
		return;
	}

	pState->m_pObj->m_UnknownDC = 5;

	GetMovementBox(&pState->m_vMoveMin, &pState->m_vMoveMax, &pState->m_vDeltaPos,
		&pState->m_pObj->m_MinBox, &pState->m_pObj->m_MaxBox);

	objectArray.m_pState = pState;

	if(pState->m_CustomTestObjects)
	{
		objectArray.m_nObjects = 0;
		for(i=0; i < (int32)pState->m_nCustomTestObjects; i++)
		{
			pTestObj = pState->m_CustomTestObjects[i];
			if(DoBoxesIntersect(pTestObj->m_MinBox, pTestObj->m_MaxBox,
				pState->m_vMoveMin, pState->m_vMoveMax, -1.0f))
			{
				GetBoxIntersection(&pTestObj->m_MinBox, &pTestObj->m_MaxBox,
					&pState->m_vMoveMin, &pState->m_vMoveMax, &vMin, &vMax);
				objectArray.m_Objects[objectArray.m_nObjects].m_pObject = pTestObj;
				objectArray.m_Objects[objectArray.m_nObjects].m_fDistSqr = pState->m_pStartPos->DistSqr(pTestObj->GetPos());
				objectArray.m_nObjects++;
			}
		}
	}
	else
	{
		pState->m_pWorldTree->FindObjectsInBox(&pState->m_vMoveMin, &pState->m_vMoveMax,
			FindObjectsCB, &objectArray, NOA_Objects);
	}

	pHitObjects[0] = pHitObjects[1] = LTNULL;
	fTemp = 0.0f;

	if(objectArray.m_nObjects == 0)
		return;

	// Test the nearest objects first.
	qsort(objectArray.m_Objects, objectArray.m_nObjects, sizeof(IntersectingObject), CompareObjectDists);

	nRestarts = 0;
	if(!g_CV_NewStairStep)
	{
		for(i = objectArray.m_nObjects - 1; i >= 0 && nRestarts < 10; i--)
		{
			pTestObj = objectArray.m_Objects[i].m_pObject;

			// Keep track of whether or not this is a re-iteration hit of the same object for stairstepping support
			pState->m_Unknown64 = ((pTestObj == pHitObjects[0]) || (pTestObj == pHitObjects[1])) ? nRestarts : 0;

			if(MaybeCollide(pState, pTestObj, &vTemp, &fTemp))
			{
				// If we hit this guy last time or the time before, then stop the madness...
				if((nRestarts >= 2) && ((pTestObj == pHitObjects[0]) || (pTestObj == pHitObjects[1])))
				{
					pState->m_vDestPos = *pState->m_pStartPos;
					pState->m_pObj->SetPos(*pState->m_pStartPos);
					break;
				}

				// Record the hit...
				pHitObjects[nRestarts & 1] = pTestObj;

				SetObjectBoundingBox(pState->m_pObj, LTTRUE);

				i = objectArray.m_nObjects;
				nRestarts++;

				// Reset some info...
				pState->m_vDestPos = pState->m_pObj->GetPos();
				pState->m_vDeltaPos = pState->m_vDestPos - startPos;
			}
		}
	}
	else
	{
		for(i = 0; i < objectArray.m_nObjects && nRestarts < 10; i++)
		{
			pTestObj = objectArray.m_Objects[i].m_pObject;

			if(!MaybeCollide(pState, pTestObj, &vTemp, &fTemp))
				continue;

			if(pTestObj == pHitObjects[0] && pTestObj == pHitObjects[1])
			{
				if(!IsWorldModel(pTestObj))
				{
					pState->m_vDestPos = *pState->m_pStartPos;
					pState->m_pObj->SetPos(*pState->m_pStartPos);
					break;
				}
			}
			else
			{
				// Start over.
				i = -1;
				pHitObjects[nRestarts & 1] = pTestObj;
				nRestarts++;
			}

			SetObjectBoundingBox(pState->m_pObj, LTTRUE);

			// Reset some info...
			pState->m_vDestPos = pState->m_pObj->GetPos();
			pState->m_vDeltaPos = pState->m_vDestPos - startPos;
			pState->m_vMoveCenter = *pState->m_pStartPos + pState->m_vDeltaPos * 0.5f;
			pState->m_fMoveRadius = (0.5f * pState->m_vDeltaPos.Mag()) + pState->m_pObj->m_Dims.Mag();
			GetMovementBox(&pState->m_vMoveMin, &pState->m_vMoveMax, &pState->m_vDeltaPos,
				&pState->m_pObj->m_MinBox, &pState->m_pObj->m_MaxBox);
		}
	}

	// If we didn't find someplace to go, then go back to the beginning
	if(nRestarts >= 10)
	{
		pState->m_vDestPos = *pState->m_pStartPos;
		pState->m_pObj->SetPos(*pState->m_pStartPos);
	}
}


// Gives each object a touch notification and creates container links for them.
// Wave 7 phase 2: `inline` in Talon (moveobject.h still declares it plainly; only this file calls it). Its copy
// sits after DetectAndProcessCollisions with the other copies DAPC requests (IsWorldModel, DoObjectsIntersect,
// DoSolidWMCollision); as an inline it is a refused site, i.e. one more pending site at the end of
// MaybeCollideWorldModel (which then MATCHES, as tools/inline_budget.py --solve predicted: k=1 at the end).
// FUNCTION: LITHTECH 0x0045ea50
inline void DoNonsolidCollision(MoveAbstract *pAbstract, LTObject *pObj1, LTObject *pObj2)
{
	CollisionInfo *pInfo;
	LTVector zeroVec;

	pInfo = pAbstract->GetCollisionInfo();

	// Setup the collision info...
	pInfo->m_Plane.m_Normal.Init();
	pInfo->m_Plane.m_Dist = 0.0f;
	pInfo->m_hPoly = LTNULL;

	zeroVec.Init();

	if(pObj1->m_Flags & FLAG_TOUCH_NOTIFY)
	{
		pAbstract->DoTouchNotify(pObj1, pObj2, zeroVec, 0.0f);
	}

	if(pObj2->m_Flags & FLAG_TOUCH_NOTIFY)
	{
		pAbstract->DoTouchNotify(pObj2, pObj1, zeroVec, 0.0f);
	}

	// Update area object touching.
	if(pObj1->m_Flags & FLAG_CONTAINER)
	{
		pAbstract->PutObjectInContainer(pObj2, pObj1);
	}

	if(pObj2->m_Flags & FLAG_CONTAINER)
	{
		pAbstract->PutObjectInContainer(pObj1, pObj2);
	}
}



// Collides the two solid objects using WorldModel physics for the one that is a WorldModel.
// CheckIntersectOnMovement's inlined copy (0x4602c7: operator+ out of line with this = pos2, vecTo copied) shows the
// original wrote `pos2 + vecTo`. Matched once MoveState::m_nRestart became a float (hand pass after wave 7).
// This is the exe's DoSolidWMCollision (Jupiter has it static, Talon inline: CheckIntersectOnMovement inlines it).
// FUNCTION: LITHTECH 0x0045eaf0
inline LTBOOL DoSolidWMCollision(MoveState *pState, LTObject *pTestObj, LTVector &startPos, LTVector &destPos, LTBOOL bNotify, LTBOOL &bCollision)
{
	LTVector pos1, pos2, vecTo, vDir;
	LTBOOL bWorldModel;
	MoveState moveState;
	LTVector minBox, maxBox;

	bCollision = LTFALSE;

	// Worldmodel moving into an object...
	if(pState->m_pObj->HasWorldModel())
	{
		// pInputObj is pushing pTestObj.
		pos2 = pTestObj->GetPos();
		
		vecTo = destPos - startPos;
		pos1 = pos2 + vecTo;
		
		vDir = vecTo;
		vDir.Norm();
		pos1 += vDir * 0.5f; // Move a little further back.

		// See if the blocker collides with the mover at any point during the move...
		if(CollideAgainstWorld(pState,
			((WorldModelInstance*)pState->m_pObj)->m_pValidBsp, 
			pState->m_pObj,
			pTestObj,
			pos1,
			pos2, 
			!(pState->m_pObj->m_Flags & FLAG_NOSLIDING),
			bNotify))
		{
			bCollision = LTTRUE;

			if(pState->m_pAbstract->ShouldPushObject(pState, pState->m_pObj, pTestObj))
			{
				// There is a collision, so move the blocker to its new position...
				moveState.Inherit(pState, pTestObj);
				moveState.m_BPriority = pTestObj->m_BPriority;
				MoveObject(&moveState, pos2, MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS);

				// If they're still intersecting, go to the below area where the pusher is stopped
				// by the blocker.  Only consider it crushing if there is significant penetration, otherwise
				// it's just date-crushing.
				minBox = pTestObj->m_MinBox;
				minBox.x += 0.1f;
				minBox.y += 0.1f;
				minBox.z += 0.1f;
				
				maxBox = pTestObj->m_MaxBox;
				maxBox.x -= 0.1f;
				maxBox.y -= 0.1f;
				maxBox.z -= 0.1f;

				if(DoObjectsIntersect(pState->m_pObj, pTestObj, 
					&pState->m_pObj->m_MinBox, &pState->m_pObj->m_MaxBox, 
					&minBox, &maxBox, 0.001f, &bWorldModel))
				{
					// Still intersecting.. send a crush message to pTestObj.
					pState->m_pAbstract->DoCrush(pTestObj, pState->m_pObj);
				}
				else
				{
					// Mover wasn't blocked...
					return LTFALSE;
				}
			}

			// mover had a lower blocking priority or the blocker couldn't be pushed out of the way...

			// Get the amount the worldobject has to move back by...
			vecTo = pTestObj->GetPos() - pos2;

			// Move the worldobject back a little...
			destPos += vecTo;

			// Mover was blocked...
			return LTTRUE;
		}
	}
	// Object moving into a worldmodel...
	else
	{
		pos2 = destPos;

		// Move the object into the worldobject to see how far it can get...
		if(CollideAgainstWorld(pState, 
			((WorldModelInstance*)pTestObj)->m_pValidBsp, 
			pTestObj,
			pState->m_pObj,
			startPos, pos2, 
			!(pState->m_pObj->m_Flags & FLAG_NOSLIDING),
			bNotify))
		{
			// There was a hit...
			bCollision = LTTRUE;

			if(pState->m_pAbstract->ShouldPushObject(pState, pState->m_pObj, pTestObj))
			{
				vecTo = destPos - pos2;
				pos1 = pTestObj->GetPos() + vecTo;

				// There is a collision, so move the blocker to its new position...
				moveState.Inherit(pState, pTestObj);
				moveState.m_BPriority = pTestObj->m_BPriority;
				MoveObject(&moveState, pos1, MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS);

				minBox = pTestObj->m_MinBox;
				minBox.x += 0.1f;
				minBox.y += 0.1f;
				minBox.z += 0.1f;
				
				maxBox = pTestObj->m_MaxBox;
				maxBox.x -= 0.1f;
				maxBox.y -= 0.1f;
				maxBox.z -= 0.1f;

				// If they're still intersecting, go to the below area where the pusher is stopped
				// by the blocker.  Only consider it crushing if there is significant penetration, otherwise
				// it's just date-crushing.
				if(DoObjectsIntersect(pState->m_pObj, pTestObj,
					&pState->m_pObj->m_MinBox, &pState->m_pObj->m_MaxBox, 
					&minBox, &maxBox, 0.001f, &bWorldModel))
				{
					// Still intersecting.. send a crush message to pTestObj.
					pState->m_pAbstract->DoCrush(pTestObj, pState->m_pObj);

					// Get the amount the mover has to move back by...
					vecTo = pTestObj->GetPos() - pos1;

					// Move the mover back a little...
					destPos += vecTo;

					// Mover blocked...
					return LTTRUE;
				}
			}
			else
			{
				// If the blocking priority is lower, then collideAgainstWorld finds our final position...
				destPos = pos2;

				// Mover blocked...
				return LTTRUE;
			}
		}
	}

	// Mover not blocked...
	return LTFALSE;
}


// FUNCTION: LITHTECH 0x0045f1a0 ?Setup@MoveState@@QAEXPAVWorldTree@@PAVMoveAbstract@@PAVLTObject@@K@Z


//collides the object against the BSP.
//returns LTTRUE if its path was diverted.
// FUNCTION: LITHTECH 0x0045f1d0
LTBOOL CollideAgainstWorld(
	MoveState*		pState,
	WorldBsp*		pWorldBsp,
	LTObject*		pWorldBspObj,
	LTObject*		pObj,	//the LTObject to collide against the world
	LTVector&		P0,		//the LTObject's initial position
	LTVector&		P1,		//the LTObject's final position
	LTBOOL			bSlide,
	LTBOOL			bNotify
)
{
	CollideRequest request;
	CollideInfo info;
	float forceMagSqr;
	LTBOOL bSolid;


	if(pObj->m_Flags & FLAG_GOTHRUWORLD)
	{
		return LTFALSE;
	}
	else
	{
		request.m_pAbstract		= pState->m_pAbstract;
		request.m_bServer		= pState->m_bServer;
		request.m_pCollisionInfo = pState->m_pAbstract->GetCollisionInfo();
		request.m_pWorld		= pWorldBsp;
		request.m_pWorldObj		= pWorldBspObj;
		request.m_OriginalPos	= P0;
		request.m_NewPos		= P1;
		request.m_Dims			= pObj->m_Dims;
		request.m_pObject		= pObj;
		request.m_bStairStep	= !!(pObj->m_Flags & FLAG_STAIRSTEP);
		request.m_bSlide		= bSlide;
		request.m_Unknown44		= pState->m_Unknown64;
		request.m_pRestart		= &pState->m_nRestart;

		CollideWithWorld(request, &info);

		// Copy the final position to the object's position.
		P1 = info.m_FinalPos;

		if(bNotify)
		{
			// See if it's standing on anything.
			bSolid = IsSolidWorld(pWorldBspObj) || (pObj->m_Flags & FLAG_SOLID);
			if(bSolid && info.m_pStandingOn)
			{
				SetObjectStanding(pObj, pWorldBspObj, info.m_pStandingOn);
			}

			// If it was stopped by anything, update for its new position.
			if(info.m_nHits > 0)
			{
				SetObjectBoundingBox(pObj, LTTRUE);

				forceMagSqr = info.m_vForce.MagSqr();
				if(forceMagSqr >= pObj->m_ForceIgnoreLimitSqr)
				{
					// Notify it at the position it hit at.
					LTVector vOldPos = pObj->GetPos();
					pObj->SetPos(P1);
					pState->m_pAbstract->DoTouchNotify(pObj, (LTObject*)pState->m_pAbstract->GetCollisionInfo()->m_hObject,
						info.m_VelOffset, (float)sqrt(forceMagSqr));
					pObj->SetPos(vOldPos);
				}

				// Apply the velocity offset.
				if(bSolid)
				{
					pObj->m_Velocity += info.m_VelOffset;
				}
			}
		}

		return info.m_nHits > 0;
	}
}


// FUNCTION: LITHTECH 0x0045f460
inline LTBOOL DoSolidBBoxCollision(MoveState *pState, LTObject *pTestObj, LTVector &startPos, LTVector &destPos)
{
	int32 pushPlane;
	float fPlaneDist;
	MoveState moveState;

	// Special case if the pusher's blocking priority is greater than the object
	// it hit.  Then it physically moves the blocker.
	if(pState->m_pAbstract->ShouldPushObject(pState, pState->m_pObj, pTestObj))
	{
		LTVector vTestPos = pTestObj->GetPos() + (destPos - startPos);
		LTVector newPos = GetPushawayPos(pTestObj, pState->m_pObj, &vTestPos, &pTestObj->m_Pos, &pushPlane, &fPlaneDist);

		moveState.Inherit(pState, pTestObj);
		moveState.m_BPriority = pTestObj->m_BPriority;
		MoveObject(&moveState, newPos, MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS);

		DoInterObjectCollisionResponse(pState->m_pAbstract, pTestObj, 
			pState->m_pObj, &g_PushPlanes[pushPlane], fPlaneDist);
						
		// If they're still intersecting, go to the below area where the pusher is stopped
		// by the blocker.
		if(DoBoxesIntersect(pState->m_pObj->m_MinBox, pState->m_pObj->m_MaxBox, 
			pTestObj->m_MinBox, pTestObj->m_MaxBox, 0.001f))
		{
			// Still intersecting.. send a crush message to pTestObj.
			pState->m_pAbstract->DoCrush(pTestObj, pState->m_pObj);
		}
		else
		{
			return LTFALSE;
		}
	}

	destPos = GetPushawayPos(pState->m_pObj, pTestObj, &startPos, &destPos, &pushPlane, &fPlaneDist);

	// Do a collision response.
	DoInterObjectCollisionResponse(pState->m_pAbstract, pState->m_pObj, 
		pTestObj, &g_PushPlanes[pushPlane], fPlaneDist);
	
	return LTTRUE;
}

// Finds where the mover ends up when it's pushed out of the blocker along the
// movement from *pStartPos to *pDestPos.
// FUNCTION: LITHTECH 0x0045f640
// The move box is built with Init(LTMIN(..), ..) (arguments evaluated z first, kept on the x87 stack), not VEC_MIN, and
// both branches end in one `return newPos;` with `newPos += *pDestPos` on the sliding-off path.
LTVector GetPushawayPos(LTObject *pMover, LTObject *pBlocker, LTVector *pStartPos, LTVector *pDestPos,
	int32 *pPushPlane, float *pPlaneDist)
{
	LTVector vNormal, pushAmount, newPos, moveMin, moveMax;
	float fDot1, fDot2;

	LTVector vDeltaPos = *pDestPos - *pStartPos;

	LTVector startMin = *pStartPos - pMover->m_Dims;
	LTVector startMax = *pStartPos + pMover->m_Dims;
	LTVector destMin = *pDestPos - pMover->m_Dims;
	LTVector destMax = *pDestPos + pMover->m_Dims;

	moveMin.Init(LTMIN(startMin.x, destMin.x), LTMIN(startMin.y, destMin.y), LTMIN(startMin.z, destMin.z));
	moveMax.Init(LTMAX(startMax.x, destMax.x), LTMAX(startMax.y, destMax.y), LTMAX(startMax.z, destMax.z));

	// Find the smallest dimension we can move the mover back on.
	GetSmallestPushaway(moveMin, moveMax, startMin, startMax,
		pBlocker->m_MinBox, pBlocker->m_MaxBox, pushAmount, *pPushPlane, vDeltaPos);

	// Is the pushplane on the maximum side of the mover...
	if(*pPushPlane & 0x01)
	{
		// Get plane distance...
		*pPlaneDist = -pBlocker->m_MinBox[(*pPushPlane - 1) >> 1];
	}
	else
	{
		// Get plane distance...
		*pPlaneDist = pBlocker->m_MaxBox[*pPushPlane >> 1];
	}

	// Was it pushing the guy up?
	if(*pPushPlane == 2 || *pPushPlane == 3)
	{
		// Set the highest guy to be standing on the lowest one.
		if(pStartPos->y > pBlocker->GetPos().y)
		{
			// Only stand on if the mover is moving down.
			if(vDeltaPos.y < 0.0f)
			{
				SetObjectStanding(pMover, pBlocker, LTNULL);
			}
		}
		else
		{
			// Only set the blocker to be standing on the mover if it moved up.
			if(vDeltaPos.y > 0.0f)
			{
				SetObjectStanding(pBlocker, pMover, LTNULL);
			}
		}
	}

	if(!(pMover->m_Flags & FLAG_NOSLIDING))
	{
		// Move it back.
		newPos = *pDestPos + pushAmount;
	}
	else
	{
		// The push amount is the distance along the normal behind the plane.  If we had used
		// a dot product, this number would be negative...
		fDot2 = -pushAmount.Mag();
		if(fDot2 > 0.0f)
		{
			vNormal = pushAmount / -fDot2;
			fDot1 = fDot2 - vNormal.Dot(vDeltaPos);
			newPos = vDeltaPos * (fDot2 / (fDot1 - fDot2));
		}
		else
		{
			newPos.Init();
		}

		newPos += *pDestPos;
	}

	return newPos;
}


//---------------------------------------------------------------------------//
// Finds the smallest distance to push the mover out of the blocker, only along
// the sides the mover came from.
// FUNCTION: LITHTECH 0x0045fa30
void GetSmallestPushaway(LTVector &moverMin, LTVector &moverMax, LTVector &moverStartMin, LTVector &moverStartMax,
	LTVector &blockerMin, LTVector &blockerMax, LTVector &pushAmount, int32 &pushPlane, LTVector &vMoveDelta)
{
	float		minPush;
	int32		minPushDim = 0;
	
	float		testPush, fMove;
	int32		i, curPushPlane, minPushPlane;

	
	minPush = (float)MAX_CREAL;
	curPushPlane = 0;
	minPushPlane = 0;
	
	for(i=0; i < 3; i++)
	{
		fMove = vMoveDelta[i];

		if(fMove <= 0.0001f && moverStartMin[i] - blockerMax[i] >= -0.0001f)
		{
			testPush = blockerMax[i] - moverMin[i];
			if(fabs(testPush) < fabs(minPush))
			{
				minPush = testPush;
				minPushDim = i;
				minPushPlane = curPushPlane;
			}
		}
		++curPushPlane;
		
		if(fMove >= -0.0001f && blockerMin[i] - moverStartMax[i] >= -0.0001f)
		{
			testPush = blockerMin[i] - moverMax[i];
			if(fabs(testPush) < fabs(minPush))
			{
				minPush = testPush;
				minPushDim = i;
				minPushPlane = curPushPlane;
			}
		}
		++curPushPlane;
	}

	pushAmount.Init(0.0f, 0.0f, 0.0f);

	if(minPush == (float)MAX_CREAL)
		minPush = 0.0f;

	pushAmount[minPushDim] = minPush;
	pushPlane = minPushPlane;
}


// FUNCTION: LITHTECH 0x0045fb80
inline void GetMovementBox(LTVector *pvMoveMin, LTVector *pvMoveMax, LTVector *pvDeltaPos, LTVector *pMinBox, LTVector *pMaxBox)
{
	*pvMoveMin = *pMinBox;
	*pvMoveMax = *pMaxBox;

	if( pvDeltaPos->x > 0.0f )
	{
		pvMoveMin->x -= pvDeltaPos->x;
	}
	else if( pvDeltaPos->x < 0.0f )
	{
		pvMoveMax->x -= pvDeltaPos->x;
	}
	if( pvDeltaPos->y > 0.0f )
	{
		pvMoveMin->y -= pvDeltaPos->y;
	}
	else if( pvDeltaPos->y < 0.0f )
	{
		pvMoveMax->y -= pvDeltaPos->y;
	}
	if( pvDeltaPos->z > 0.0f )
	{
		pvMoveMin->z -= pvDeltaPos->z;
	}
	else if( pvDeltaPos->z < 0.0f )
	{
		pvMoveMax->z -= pvDeltaPos->z;
	}
}




// Finds the first place along the movement where the mover hits the test object.
// Approximation of the Talon version (Jupiter's CheckIntersectOnMovement structure with the
// Talon collision calls); not byte-matched yet.
// Note (wave 5): the function order of this file is now the address order of the original (inline decisions depend on
// the callee being defined before the caller).  With DoSolidWMCollision and DoSolidBBoxCollision `inline` the size is
// 4080 (original ~4080, Ghidra extent 4096) and the shape diff is ~220 of ~1000 instructions: IsWorldModel's flag tests
// (orig: one `test [obj+0x88],edx` with 0x4000 hoisted into edx, result spilled to [esp+0x54] with immediates), the frame
// (orig sub esp,0x23c, 16 bytes less than ours) and register allocation remain.  DoSolidBBoxCollision can't be `inline`
// until DetectAndProcessCollisions calls it out of line (the out-of-line copy would vanish: ERROR).
// Waiting on this function: the original calls MoveState's constructor (0x00460c60, zeroes +0/+4/+8/+0x64/+0x68) twice
// and MoveState::Inherit (0x00460c80) three times out of line, and those copies sit right after it; annotate them
// (mangled names) once this compiles that way. MaybeCollide calls Inherit too.
// Wave 7: with DoSolidBBoxCollision inline (wave-6 rewrite) it calls the ctor twice and Inherit twice out of line
// (copies annotated before MaybeCollide). Budget model (aliases 460c60/460c80): the exe needs B +156..+260u with one
// more free pending site before the final MoveObjectTo (own size +78..+130u): the original is BIGGER here; with 36
// call-sequence differences (audit) this is missing code, not an inlining question.
// Wave 7 phase 2 (decoded from the exe, 0x4609cd-0x460b14): two behaviour bugs fixed. The non-pushing branch
// calls CollideAgainstWorld with bNotify = LTFALSE (ours passed LTTRUE), does not adjust m_vDeltaPos, and
// notifies through DoNonsolidCollision only when bStopped (also when neither object is a WorldModel); the
// box-physics push passes pState->m_pObj->m_Pos itself as DoSolidBBoxCollision's destPos (no copy, no SetPos
// afterwards). With DoNonsolidCollision inline: ALIGNED 591 -> 434 (3824 bytes, exe ~4080). Audit now: only
// inlining decisions: the exe also inlines DoNonsolidCollision's two LTVector::Init, the final MoveObjectTo (with
// its operator!= and SetObjectBoundingBox) and its operator+, and calls one more LTVector ctor out of line
// (0x46084b, DoSolidBBoxCollision's vTestPos) where we call operator-. Budget model: no budget reproduces the
// exe (more budget also inlines DoSolidWMCollision's MoveState ctor, which the exe calls out of line at
// 0x460206), and no <= 6 extra pending sites do: the end of the function must be cheaper or DoSolidWMCollision
// (842u, itself a STUB) different in the original. Next idea: settle DoSolidWMCollision's source first, then
// rerun `inline_budget.py 45fc60 --solve`.
// PARKED: behaviour now matches the exe; remaining call differences are inlining decisions at the end (rechecked with R11: no tail site in the loop, --solve finds no budget/pending change)
// STUB: LITHTECH 0x0045fc60
LTBOOL CheckIntersectOnMovement(MoveState *pState, LTObject *pTestObj, LTBOOL bPushAway)
{
	float t1, t2, tClosest, tFarthest;
	int32 i, nIterations;
	LTBOOL bCollide, bMoved, bStopped;
	LTBOOL bWorldModel, bClosestIsT1;
	LTVector vTempDestPos, vNewPos;
	MoveState moveState;

	nIterations = 0;
	bWorldModel = IsWorldModel(pState->m_pObj, pTestObj);

	do
	{
		// Loop over all the planes of the blocker.  Put the position of the mover on the plane and see if the
		// objects collide...
		bMoved = bCollide = bStopped = LTFALSE;
		for(i = 0; i < 3 && !bCollide; i++)
		{
			// t == 0.0f is the start position.  There should've been no collision at the start position, so
			// start this loop assuming no collisions...
			t1 = t2 = 0.0f;

			// Find parameterized value of movement...
			if(pState->m_vDeltaPos[i] < 0.0f || pState->m_vDeltaPos[i] > 0.0f)
			{
				t1 = (pTestObj->m_MinBox[i] - (*(LTVector*)pState->m_pStartPos)[i]) / pState->m_vDeltaPos[i];
				t1 = LTCLAMP(t1, 0.0f, 1.0f);
				t2 = (pTestObj->m_MaxBox[i] - (*(LTVector*)pState->m_pStartPos)[i]) / pState->m_vDeltaPos[i];
				t2 = LTCLAMP(t2, 0.0f, 1.0f);
			}

			if(t1 < t2)
			{
				bClosestIsT1 = LTTRUE;
				tClosest = t1;
				tFarthest = t2;
			}
			else
			{
				bClosestIsT1 = LTFALSE;
				tClosest = t2;
				tFarthest = t1;
			}

			// Check if the parameterized value is within range...
			if(0.0f < tClosest || bWorldModel)
			{
				// Check for collisions at this plane if non-solid or if we are moving toward the test object's
				// minimum plane...
				if(!bPushAway || (bClosestIsT1 && pState->m_vDeltaPos[i] > 0.0f) || (!bClosestIsT1 && pState->m_vDeltaPos[i] < 0.0f))
				{
					bMoved = LTTRUE;
					VEC_LERP(vNewPos, *pState->m_pStartPos, pState->m_vDestPos, tClosest);
					MoveObjectTo(pState->m_pObj, vNewPos);

					if(DoBoxesIntersect(pState->m_pObj->m_MinBox, pState->m_pObj->m_MaxBox,
						pTestObj->m_MinBox, pTestObj->m_MaxBox, -0.01f))
					{
						bCollide = LTTRUE;
					}
				}
			}

			// Check if the parameterized value is within range...
			// Check this position if it's closer to the starting position...
			if(!bCollide && (0.0f < tFarthest || bWorldModel))
			{
				// Check for collisions at this plane if non-solid or if we are moving toward the test object's
				// maximum plane...
				if(!bPushAway || (bClosestIsT1 && pState->m_vDeltaPos[i] < 0.0f) || (!bClosestIsT1 && pState->m_vDeltaPos[i] > 0.0f))
				{
					bMoved = LTTRUE;
					VEC_LERP(vNewPos, *pState->m_pStartPos, pState->m_vDestPos, tFarthest);
					MoveObjectTo(pState->m_pObj, vNewPos);

					if(DoBoxesIntersect(pState->m_pObj->m_MinBox, pState->m_pObj->m_MaxBox,
						pTestObj->m_MinBox, pTestObj->m_MaxBox, -0.01f))
					{
						bCollide = LTTRUE;
					}
				}
			}

			if(bMoved && !bCollide)
			{
				bMoved = LTFALSE;

				// Reset the position to the end of the movement...
				MoveObjectTo(pState->m_pObj, *pState->m_pStartPos + pState->m_vDeltaPos);
			}
		}

		if(bCollide)
		{
			// Possibly push them away from each other.
			if(bPushAway)
			{
				// World model physics does complete movement volume collisions, so put the input object at the
				// destination.  No tunneling is possible...
				if(bWorldModel)
				{
					MoveObjectTo(pState->m_pObj, pState->m_vDestPos);

					bStopped = DoSolidWMCollision(pState, pTestObj, *(LTVector*)pState->m_pStartPos,
						pState->m_vDestPos, LTTRUE, bCollide);

					if(bStopped)
					{
						MoveObjectTo(pState->m_pObj, pState->m_vDestPos);
					}
				}
				else
				{
					bStopped = DoSolidBBoxCollision(pState, pTestObj, *(LTVector*)pState->m_pStartPos, pState->m_pObj->m_Pos);
				}
			}
			else
			{
				// If we've got a world model, make sure we really hit it.
				bStopped = LTTRUE;
				if(bWorldModel)
				{
					// We have to copy destpos into a temp variable, cuz CollideAgainstWorld will
					// change it...
					vTempDestPos = pState->m_vDestPos;

					if(pTestObj->HasWorldModel() && !(pTestObj->m_Flags & FLAG_BOXPHYSICS))
					{
						bStopped = CollideAgainstWorld(pState,
							((WorldModelInstance*)pTestObj)->m_pValidBsp,
							pTestObj,
							pState->m_pObj, *(LTVector*)pState->m_pStartPos, vTempDestPos,
							LTFALSE, LTFALSE);
					}
					else if(pState->m_pObj->HasWorldModel() && !(pState->m_pObj->m_Flags & FLAG_BOXPHYSICS))
					{
						bStopped = CollideAgainstWorld(pState,
							((WorldModelInstance*)pState->m_pObj)->m_pValidBsp,
							pState->m_pObj,
							pTestObj, *(LTVector*)pState->m_pStartPos, vTempDestPos,
							LTFALSE, LTFALSE);
					}
				}

				// Notify the objects if they really touch.
				if(bStopped)
				{
					DoNonsolidCollision(pState->m_pAbstract, pTestObj, pState->m_pObj);
				}

				bStopped = LTFALSE;
			}
		}

		// If we messed with the position of the mover and we weren't stopped by anything, then move us to the end...
		if(bMoved && !bStopped)
		{
			// Reset the position to the end of the movement...
			MoveObjectTo(pState->m_pObj, *pState->m_pStartPos + pState->m_vDeltaPos);
		}

	}
	// Recheck the path for object's that are pushing around other objects...
	while(bPushAway && bCollide && !bStopped && nIterations++ < 10);

	return bStopped;
}


// Out-of-line copies requested by CheckIntersectOnMovement (it calls them out of line now):
// FUNCTION: LITHTECH 0x00460c60 ??0MoveState@@QAE@XZ
// FUNCTION: LITHTECH 0x00460c80 ?Inherit@MoveState@@QAEXPAV1@PAVLTObject@@@Z


// Collides the mover with one object.  Returns LTTRUE if the mover was stopped.
// Approximation: the original inlines this into DetectAndProcessCollisions' first loop and
// inlines DoSolidBBoxCollision into this out-of-line copy.
// Wave 5: decoded from the original: it inlines IsWorldModel(pObj), IsWorldModel(pTestObj) and DoObjectsIntersect and, for
// !bWorldModel && !bTestWorldModel, DoSolidBBoxCollision (so it is 1251 bytes); calls DoSolidWMCollision (0x45eaf0),
// DoNonsolidCollision (0x45ea50) and CheckIntersectOnMovement out of line; the tunnel test builds vMin = start - dims and
// vMax = dims + start. Its out-of-line COMDAT copy sits after CheckIntersectOnMovement's (MoveState ctor 0x460c60,
// Inherit 0x460c80), i.e. it is defined after DetectAndProcessCollisions; the first (g_CV_NewStairStep == 0) loop of
// DetectAndProcessCollisions carries an inline copy of this same logic (calling IsWorldModel(pTestObj) 0x45e960 and
// DoObjectsIntersect 0x45e990 out of line, then DoSolidBBoxCollision/DoSolidWMCollision/DoNonsolidCollision/
// CheckIntersectOnMovement out of line) and only the second loop calls MaybeCollide.
// Wave 7: now emitted out of line by DAPC (in the exe's place); its inline decisions match the exe (budget model:
// B 1104u, the exe needs 1076-1168u). `dims < fabs(delta)` per axis (the exe's `and eax,0x4100` tests) and
// `if(bPushAway){...} else {DoNonsolidCollision; return}` (DoNonsolidCollision last, SetPos tail-duplicated) took it
// from SIZE 1280 / ALIGNED 272 to DIFF 1264 (exe size) / ALIGNED 197.
// Wave 7 phase 2: DoNonsolidCollision is inline (see there), one more pending site after DoSolidBBoxCollision,
// which then starved its MoveState ctor (limit 87.7 < 110u); writing the DoSolidWMCollision branch first
// (`if(bWorldModel || bTestWorldModel) DSWM else DSBB`, same block layout) restores the exe's decisions (197).
// `LTVector vMin = start - dims; LTVector vMax = start + dims;` declared in the tunnel block (constructed in place,
// as the exe does: no temp copies) scores 112 with a forced out-of-line copy, but lowers MaybeCollide's cost
// 554 -> 527u so that DetectAndProcessCollisions inlines it in loop 2 too (limit 551u) and the copy disappears
// (ERROR). Adopt it together with DetectAndProcessCollisions' missing -57..-77u of own code (its notes): that
// change drops loop 2's limit below 527u. Audit: one SetPos call fewer than the exe: the exe duplicates the
// `if(bStopped) SetPos; return bStopped;` tail into the inlined DoSolidBBoxCollision's `return LTTRUE` path (a
// layout difference, not behaviour); writing the tail in both branches gives 184 here but DAPC 594.
// PARKED: inline decisions match the exe (R11 model too: B within -44..+50u); the rest waits for DAPC's own-size fix (then adopt the in-place vMin/vMax: 112)
// STUB: LITHTECH 0x00460cb0
inline LTBOOL MaybeCollide(MoveState *pState, LTObject *pTestObj, LTVector *pUnused, float *pUnused2)
{
	LTBOOL bWorldModel, bTestWorldModel, bPushAway, bStopped, bCollide;
	LTVector vMin, vMax;

	// Don't move objects that are already moving.
	if(pTestObj->m_InternalFlags & IFLAG_MOVING)
		return LTFALSE;

	// Sphere physics does its own collisions.
	if(pState->m_pObj->m_Flags2 & FLAG2_SPHEREPHYSICS)
		return LTFALSE;

	bWorldModel = IsWorldModel(pState->m_pObj);
	bTestWorldModel = IsWorldModel(pTestObj);

	// WorldModels don't collide with each other, and honor FLAG_GOTHRUWORLD.
	if(bWorldModel)
	{
		if(bTestWorldModel || (pTestObj->m_Flags & FLAG_GOTHRUWORLD))
			return LTFALSE;
	}
	else if(bTestWorldModel && (pState->m_pObj->m_Flags & FLAG_GOTHRUWORLD))
	{
		return LTFALSE;
	}

	// Possibly push them away from eachother.
	bPushAway = (pTestObj->m_Flags & FLAG_SOLID) && (pState->m_pObj->m_Flags & FLAG_SOLID);
	if(!bPushAway && bTestWorldModel)
		bPushAway = IsSolidWorld(pTestObj);

	// If it moves further than its size, check along the whole movement so it can't tunnel.
	if(pState->m_pObj->m_Dims.x < fabs(pState->m_vDeltaPos.x) ||
		pState->m_pObj->m_Dims.y < fabs(pState->m_vDeltaPos.y) ||
		pState->m_pObj->m_Dims.z < fabs(pState->m_vDeltaPos.z))
	{
		vMin = *pState->m_pStartPos - pState->m_pObj->m_Dims;
		vMax = *pState->m_pStartPos + pState->m_pObj->m_Dims;
		if(!DoBoxesIntersect(vMin, vMax, pTestObj->m_MinBox, pTestObj->m_MaxBox, 0.0f))
		{
			return CheckIntersectOnMovement(pState, pTestObj, bPushAway);
		}
	}

	// Make sure the objects actually intersect before we do any collision and movement...
	if(!DoObjectsIntersect(pState->m_pObj, pTestObj, &pState->m_vMoveMin,
		&pState->m_vMoveMax, &pTestObj->m_MinBox, &pTestObj->m_MaxBox, 0.0f, LTNULL))
	{
		return LTFALSE;
	}

	if(bPushAway)
	{
		if(bWorldModel || bTestWorldModel)
		{
			bStopped = DoSolidWMCollision(pState, pTestObj, *(LTVector*)pState->m_pStartPos, pState->m_vDestPos,
				LTTRUE, bCollide);
		}
		else
		{
			bStopped = DoSolidBBoxCollision(pState, pTestObj, *(LTVector*)pState->m_pStartPos, pState->m_vDestPos);
		}

		if(bStopped)
			pState->m_pObj->SetPos(pState->m_vDestPos);

		return bStopped;
	}
	else
	{
		// Notify the objects.
		DoNonsolidCollision(pState->m_pAbstract, pTestObj, pState->m_pObj);
		return LTFALSE; // Nothing moved.
	}
}

// Gets the overlap of two boxes.
// FUNCTION: LITHTECH 0x004611a0
void GetBoxIntersection(LTVector *pMin1, LTVector *pMax1, LTVector *pMin2, LTVector *pMax2,
	LTVector *pOutMin, LTVector *pOutMax)
{
	VEC_MIN(*pOutMax, *pMax1, *pMax2);
	VEC_MAX(*pOutMin, *pMin1, *pMin2);
}


// Called by WorldTree::FindObjectsInBox.
// Hand pass 2: the tail is `vCenter = (vMax - vMin) * 0.5f; vCenter += vMin;` and VEC_DISTSQR against the start
// position (the exe keeps the half extent in its own slot, copies it for `+=`'s by-value argument and subtracts
// *m_pStartPos straight from memory; operator-/DistSqr copy the start position instead): 84 -> 33 aligned, the tail and
// the frame (vCenter 0x10, vMin 0x1c, vMax 0x28) now identical. Left: a register permutation (exe pObject=edi,
// pArray=ebx, the 0x400/m_bServer/pState temporaries in esi; ours ebx/esi/edi). A whole-function `MoveState *pState`
// puts pObject in edi but takes ebx for itself (31); block- or tail-scoped pState/pMoveObj locals, the type test on
// pTreeObj and Jupiter's if/else around the body (62+) don't help.
// PARKED: register permutation only (pObject/pArray/temporaries); behaviour matches
// STUB: LITHTECH 0x00461260
void FindObjectsCB(WorldTreeObj *pTreeObj, void *pCBUser)
{
	LTObject *pObject;
	IntersectingObjectArray *pArray;
	int32 iObject;
	LTVector vCenter, vMin, vMax;


	pObject = (LTObject*)pTreeObj;
	if(pObject->GetObjType() != WTObj_DObject)
		return;

	pArray = (IntersectingObjectArray*)pCBUser;
	
	iObject = pArray->m_nObjects;
	if(iObject >= MAX_INTERSECTING_OBJECTS)
	{
		dsi_ConsolePrint("FindObjectsCB: Overflowed (more than %d intersecting objects)!",
			MAX_INTERSECTING_OBJECTS);
		return;
	}

	if(pObject == pArray->m_pState->m_pObj)
		return;

	// Check for no possible collisions between these objects...
	if(!(pObject->m_Flags & (FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER)))
		return;

	// Semi-solid objects don't collide with each other.
	if((pObject->m_Flags2 & FLAG2_SEMISOLID) && (pArray->m_pState->m_pObj->m_Flags2 & FLAG2_SEMISOLID))
		return;

	if(!pArray->m_pState->m_bServer)
	{
		if(pObject->m_Flags & FLAG_CLIENTNONSOLID || pArray->m_pState->m_pObj->m_Flags & FLAG_CLIENTNONSOLID)
			return;

		if(!(pObject->m_Flags & FLAG_SOLID) || !(pArray->m_pState->m_pObj->m_Flags & FLAG_SOLID))
			return;
	}

	pArray->m_Objects[iObject].m_pObject = pObject;

	// The world is always tested last.
	if(IsSolidWorldBsp(pObject))
	{
		pArray->m_Objects[iObject].m_fDistSqr = FLT_MAX;
		pArray->m_nObjects++;
		return;
	}

	// Sort by the distance to the center of the overlap with the movement box.
	GetBoxIntersection(&pObject->m_MinBox, &pObject->m_MaxBox, 
		&pArray->m_pState->m_vMoveMin, &pArray->m_pState->m_vMoveMax, &vMin, &vMax);
	vCenter = (vMax - vMin) * 0.5f;
	vCenter += vMin;
	pArray->m_Objects[iObject].m_fDistSqr = VEC_DISTSQR(vCenter, *pArray->m_pState->m_pStartPos);
	pArray->m_nObjects++;
}


// Is this the main world or a terrain BSP?
// FUNCTION: LITHTECH 0x00461410
LTBOOL IsSolidWorldBsp(LTObject *pObj)
{
	if(pObj->m_ObjectType == OT_WORLDMODEL)
	{
		if((((WorldModelInstance*)pObj)->m_pOriginalBsp->GetWorldInfoFlags() & WIF_MAINWORLD) ||
			(((WorldModelInstance*)pObj)->m_pOriginalBsp->GetWorldInfoFlags() & WIF_TERRAIN))
		{
			return LTTRUE;
		}
	}

	return LTFALSE;
}


// qsort callback: sorts intersecting objects by distance.
// FUNCTION: LITHTECH 0x00461450
int CompareObjectDists(const void *pA, const void *pB)
{
	if(((IntersectingObject*)pA)->m_fDistSqr < ((IntersectingObject*)pB)->m_fDistSqr)
		return -1;
	else if(((IntersectingObject*)pA)->m_fDistSqr > ((IntersectingObject*)pB)->m_fDistSqr)
		return 1;
	else
		return 0;
}


// FUNCTION: LITHTECH 0x00461490
LTBOOL ChangeObjectDimensions(MoveState *pState, LTVector *pNewDims, uint32 bCollide, LTBOOL bTrivialReject)
{
	LTVector vDims, vMinDims;
	LTBOOL bRet;
	LTObject *pObj;
	float distSqr;

	pState->SetupCall();
	pObj = pState->m_pObj;

	// See if the dims didn't change...
	LTVector vDiff = pObj->m_Dims - *pNewDims;
	distSqr = VEC_MAGSQR(vDiff);
	if(bTrivialReject && distSqr < 0.0001f)
		return LTTRUE;

	if(bCollide && (pObj->m_Flags & (FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER)))
	{
		vDims = *pNewDims;

		// Shrink it first.
		VEC_MIN(vMinDims, *pNewDims, pObj->m_Dims);
		pObj->SetDims(vMinDims);

		// Then grow it where it needs to.
		if(pObj->m_Dims.y < pNewDims->y)
			GrowDim(pState, 1, &pNewDims->y);

		if(pObj->m_Dims.x < pNewDims->x)
			GrowDim(pState, 0, &pNewDims->x);

		if(pObj->m_Dims.z < pNewDims->z)
			GrowDim(pState, 2, &pNewDims->z);

		LTVector vDiff2 = vDims - pObj->m_Dims;
		bRet = LTTRUE;
		if(!(VEC_MAGSQR(vDiff2) < 0.001f))
			bRet = LTFALSE;

		*pNewDims = pObj->m_Dims;
	}
	else
	{
		pObj->SetDims(*pNewDims);
		bRet = LTTRUE;
	}

	DetachObjectStanding(pObj);
	SetObjectBoundingBox(pObj, LTTRUE);

	pState->m_pWorldTree->InsertObject(pObj, NOA_Objects);
	return bRet;
}


// Indexing the vectors as `(&v.x)[nDim]` for the reads and the first write (a repeated `v[nDim]` through
// LTVector::operator[] gets its address CSE'd into a register) and SetDims(pObj->m_Dims + vTemp) (operand order,
// no 12 byte temp) fixed most of it; the "move it back down" branch moves vNewPos (not vTemp) and keeps the new low
// end in fLow, which also fixed the nDim/pObj register swap.
// FUNCTION: LITHTECH 0x00461700
void GrowDim(MoveState *pState, int32 nDim, float *pNewDim)
{
	MoveState moveState;
	LTVector vNewPos, vTemp;
	float fDiff, fOldPos, fLow, fHigh, fGrow;

	LTObject *pObj = pState->m_pObj;

	// Find the amount to grow...
	fDiff = *pNewDim - pObj->m_Dims[nDim];

	// Get original position...
	vNewPos = pObj->GetPos();
	fOldPos = vNewPos[nDim];

	// Move the object in negative dir...
	(&vNewPos.x)[nDim] -= fDiff;
	moveState.Inherit(pState, pObj);
	moveState.m_CustomTestObjects = pState->m_CustomTestObjects;
	moveState.m_nCustomTestObjects = pState->m_nCustomTestObjects;
	MoveObject(&moveState, vNewPos, MO_DETACHSTANDING);

	// Remember where we ended up
	vTemp = pObj->GetPos();
	fLow = (&vTemp.x)[nDim];

	// Move the object in positive dir...
	vNewPos = pObj->GetPos();
	fGrow = fDiff * 2.0f;
	vNewPos[nDim] += fGrow;
	MoveObject(&moveState, vNewPos, MO_DETACHSTANDING);

	// Remember where we ended up
	vTemp = pObj->GetPos();
	fHigh = (&vTemp.x)[nDim];

	if(fOldPos + fDiff > fHigh)
	{
		if(fOldPos - fDiff < fLow)
		{
			vNewPos = pObj->GetPos();
			fDiff = (fHigh - fLow) * 0.5f;
			vNewPos[nDim] -= fDiff;
		}
		else
		{
			// Move it back down and see where it stops.
			vNewPos = pObj->GetPos();
			vNewPos[nDim] -= fGrow;
			MoveObject(&moveState, vNewPos, MO_DETACHSTANDING);

			vTemp = pObj->GetPos();
			fLow = vTemp[nDim];
			vNewPos = pObj->GetPos();
			fDiff = (fHigh - fLow) * 0.5f;
			vNewPos[nDim] += fDiff;
		}
	}
	else
	{
		vNewPos = pObj->GetPos();
		fDiff = (fHigh - fLow) * 0.5f;
		vNewPos[nDim] -= fDiff;
	}

	// Center object...
	MoveObject(&moveState, vNewPos, MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS);

	// Set the dim...
	vTemp.Init();
	vTemp[nDim] = fDiff;
	pObj->SetDims(pObj->m_Dims + vTemp);
}


// Rotates the world model and moves objects out of the way.
// FUNCTION: LITHTECH 0x004619f0
void RotateWorldModel(MoveState *pState, LTRotation *pRotation, LTBOOL bDoCollisions)
{
	LTMatrix fullTransform;
	LTMatrix temp, backTrans, backRot, forwardRot, forwardTrans;
	LTVector newPos;
	MoveState moveState;
	WorldModelInstance *pObj;
	LTLink *pCur, *pNext;
	LTObject *pObjOn;
	int32 nNumObjectsStanding;
	StartPosInfo startPosInfo[MAX_CARRIED_OBJECTS];
	uint32 i;
	CollisionInfo collisionInfo, *pOldCollisionInfo;


	++g_nMoveObjectCalls;
	CountAdder cnt(&g_Ticks_MoveObject);

	pState->SetupCall();


	pObj = (WorldModelInstance*)pState->m_pObj;

	// Don't bother if it isn't rotating.
	if(*pRotation == pObj->m_Rotation)
		return;

	// Build the rotation transformation for objects.

	// back to origin, rotate (original WM rotation) transposed, rotate (new WM rotation), back to WM pos
	LTVector vPos = pObj->GetPos();
	backTrans.Init(
		1.0f, 0.0f, 0.0f, -vPos.x,
		0.0f, 1.0f, 0.0f, -vPos.y,
		0.0f, 0.0f, 1.0f, -vPos.z,
		0.0f, 0.0f, 0.0f, 1.0f);

	forwardTrans.Init(
		1.0f, 0.0f, 0.0f, vPos.x,
		0.0f, 1.0f, 0.0f, vPos.y,
		0.0f, 0.0f, 1.0f, vPos.z,
		0.0f, 0.0f, 0.0f, 1.0f);

	quat_ConvertToMatrix(pObj->m_Rotation.m_Quat, backRot.m);
	MatTranspose3x3(&backRot);

	quat_ConvertToMatrix(pRotation->m_Quat, forwardRot.m);

	// Transform it into its new rotation and setup its new dims.
	pObj->m_Rotation = *pRotation;
	SetObjectBoundingBox(pObj, LTTRUE);
	SetupWorldModelDims(pObj, &pObj->m_Transform);

	// Relocate it in the BSP.
	pState->m_pWorldTree->InsertObject(pState->m_pObj, NOA_Objects);

	if(!bDoCollisions)
	{
		return;
	}
	
	// If it's only using box physics, don't test for object intersections.
	if(pObj->m_Flags & FLAG_BOXPHYSICS)
		return;

	MatMul(&fullTransform, &forwardTrans, &forwardRot);
	MatMul(&temp, &fullTransform, &backRot);
	MatMul(&fullTransform, &temp, &backTrans);

	// Stack the collision info so this function is re-entrant
	pOldCollisionInfo = pState->m_pAbstract->GetCollisionInfo();
	pState->m_pAbstract->GetCollisionInfo() = &collisionInfo;
	memset(&collisionInfo, 0, sizeof(CollisionInfo));

	// Transfer the objects that are on object now, so we can move them later...
	pCur = pObj->m_ObjectsStandingOn.m_pNext;
	nNumObjectsStanding = -1;
	while(pCur != &pObj->m_ObjectsStandingOn)
	{
		pNext = pCur->m_pNext;
		pObjOn = (LTObject*)pCur->m_pData;
		pCur = pNext;

		if(pObjOn->m_Flags & FLAG_DONTFOLLOWSTANDING)
		{
			DetachObjectStanding(pObjOn);
		}
		else
		{
			if(nNumObjectsStanding < MAX_CARRIED_OBJECTS)
			{
				nNumObjectsStanding++;
				startPosInfo[nNumObjectsStanding].m_pObj = pObjOn;
				startPosInfo[nNumObjectsStanding].m_vRelPos = pObjOn->GetPos();
			}
			// Too many...
			else
				DetachObjectStanding(pObjOn);
		}
	}
	
	// Find any intersecting objects and push them away.
	pState->m_pWMObjectTransform = &fullTransform;

	// Flag us as moving so no one can push us.
	pState->m_pObj->m_InternalFlags |= IFLAG_MOVING;
	
	if(pState->m_CustomTestObjects)
	{
		for(i=0; i < pState->m_nCustomTestObjects; i++)
		{
			MaybeCollideWorldModel(pState, pState->m_CustomTestObjects[i]);
		}
	}
	else
	{
		// Find objects and test them out..
		pState->m_pWorldTree->FindObjectsInBox(
			&pState->m_pObj->m_MinBox, &pState->m_pObj->m_MaxBox,
			CollideWorldModelCB, pState, NOA_Objects);
	}

	// Offset everyone that was standing on us by the same amount.
	while(nNumObjectsStanding >= 0)
	{
		pObjOn = startPosInfo[nNumObjectsStanding].m_pObj;

		// Rotate its position.
		MatVMul_H(&newPos, &fullTransform, &startPosInfo[nNumObjectsStanding].m_vRelPos);
		moveState.Inherit(pState, pObjOn);
		moveState.m_BPriority = pObjOn->m_BPriority;
		// Move down a little so it will hit the object again...
		newPos.y -= 0.5f;
		MoveObject(&moveState, newPos, MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS);

		nNumObjectsStanding--;
	}

	// We're done moving.
	pState->m_pObj->m_InternalFlags &= ~IFLAG_MOVING;

	pState->m_pAbstract->GetCollisionInfo() = pOldCollisionInfo;
}

// FUNCTION: LITHTECH 0x00461f80 ?Equals@LTRotation@@QBEIABV1@M@Z


// Inline budget: the original calls Mag() (inside dir.Norm()) and the LTVector(x,y,z) constructor (for
// pTestObj->m_Velocity = pos2 - pos1) out of line.  Pending-call experiments reproduce the Mag call (extra inline
// sites after Norm) but not the single ctor call without also moving the ctors in the min/max loop.
// Wave 6 phase 2: it MATCHES (all three calls, size 1264) with Jupiter's `pTestObj->GetDims()` in the min/max loop
// (2 pending sites after Norm; tested with a file-local accessor) plus 6-9 units of code-free inline cost anywhere
// before Norm (ballast at the top, before the solid test or before `dir = pos1 - pos2` all match; 3-5 units give
// DIFF 47 aligned, 10+ worse). Ballast inside IsWorldModel matches with 4-6 units; IsWorldModel written as nested
// ifs / explicit object types gives only the 3-5 unit result (47). The real source of the cost is still unknown.
// Wave 7: LTObject::GetDims() added (Jupiter's accessor): SIZE 1248 / ALIGNED 110 -> DIFF 1264 (exe size) / 47; only
// the ctor of `m_Velocity = pos2 - pos1` is still inlined. IsWorldModel's braces now charge 41u before Norm (it was
// free). Budget model: B 1248u, the exe needs 1160-1204u (~22-44u less own code), or --solve: two more free pending
// sites at the end with -7..+25u own size, or one more before Inherit with 11-31u less.
// Wave 7 phase 2: MATCH: the pending site at the end is the else branch's DoNonsolidCollision, an inline function
// (refused here, so it is called out of line but still counts).
// FUNCTION: LITHTECH 0x00461ff0
void MaybeCollideWorldModel(MoveState *pState, LTObject *pTestObj)
{
	LTVector pos1, pos2;
	LTVector dir, min, max, vTemp;
	LTBOOL bRet;
	MoveState moveState;
	WorldModelInstance *pInst;
	int32 betterNotUp;

	// Don't worry about other WorldModels.
	if(IsWorldModel(pTestObj))
		return;

	pInst = (WorldModelInstance*)pState->m_pObj;

	// Don't worry about it if they don't even intersect.
	if(!DoBoxesIntersect(pState->m_pObj->m_MinBox, pState->m_pObj->m_MaxBox, 
		pTestObj->m_MinBox, pTestObj->m_MaxBox, -0.001f))
		return;

	if(!DoesBoxIntersectBSP(pInst->m_pValidBsp->GetRootNode(), pTestObj->m_MinBox, pTestObj->m_MaxBox))
		return;

	// Do a solid collision?
	if((pState->m_pObj->m_Flags & FLAG_SOLID) && (pTestObj->m_Flags & FLAG_SOLID))
	{
		// pInputObj is pushing pTestObj.
		pos2 = pTestObj->GetPos();
		MatVMul_H(&pos1, pState->m_pWMObjectTransform, &pos2);

		dir = pos1 - pos2;
		if(dir.MagSqr() < 0.001f)
			return;
		
		// This helps avoid error from small rotations.. make sure the starting position does not intersect
		// the WorldModel.
		dir = pTestObj->GetPos() - pState->m_pObj->GetPos();
		dir.Norm();

		betterNotUp = 200;
		while(betterNotUp-- > 0)
		{
			min = pos1 - pTestObj->GetDims();
			max = pos1 + pTestObj->GetDims();

			if(!DoesBoxIntersectBSP(pInst->m_pValidBsp->GetRootNode(), min, max))
				break;
			
			pos1 += dir;
		}
			
		// Setup fake velocity for the collision response.
		vTemp = pTestObj->m_Velocity;
		pTestObj->m_Velocity = pos2 - pos1;
		bRet = CollideAgainstWorld(pState, 
			pInst->m_pValidBsp, 
			pState->m_pObj,
			pTestObj,
			pos1, pos2, 
			!(pTestObj->m_Flags & FLAG_NOSLIDING),
			LTTRUE);

		pTestObj->m_Velocity = vTemp;

		if(bRet)
		{
			moveState.Inherit(pState, pTestObj);
			moveState.m_BPriority = pTestObj->m_BPriority;
			MoveObject(&moveState, pos2, MO_DETACHSTANDING|MO_SETCHANGEFLAG|MO_MOVESTANDINGONS);

			// If they're still intersecting, this means the world model pushed the object into the world
			// so send the object a crush message.
			if(DoesBoxIntersectBSP(pInst->m_pValidBsp->GetRootNode(),
				pTestObj->m_MinBox, pTestObj->m_MaxBox))
			{
				pState->m_pAbstract->DoCrush(pTestObj, pState->m_pObj);
			}
		}
	}
	else
	{
		DoNonsolidCollision(pState->m_pAbstract, pState->m_pObj, pTestObj);
	}
}


// FUNCTION: LITHTECH 0x004624e0
void CollideWorldModelCB(WorldTreeObj *pObj, void *pUser)
{
	MoveState *pState;
	LTObject *pObject;


	pState = (MoveState*)pUser;

	if(pObj->GetObjType() != WTObj_DObject)
		return;

	pObject = (LTObject*)pObj;
	if(pObject == pState->m_pObj)
		return;

	MaybeCollideWorldModel(pState, pObject);
}


// Gets the attachment's world transform.
// The rotation's ConvertToMatrix (not quat_ConvertToMatrix on m_Quat) fixes the inlined quat_Mul's out[QX] term order.
// FUNCTION: LITHTECH 0x00462510
void GetAttachmentTransform(LTObject *pParent, Attachment *pAttachment, LTVector &vPos, LTRotation &rRot)
{
	LTMatrix mat;

	vPos = pAttachment->m_Offset.m_Pos;
	pParent->m_Rotation.ConvertToMatrix(mat);
	mat.Apply3x3(vPos);
	vPos += pParent->GetPos();

	rRot = pParent->m_Rotation * pAttachment->m_Offset.m_Rot;
}

