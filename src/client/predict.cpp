// Jupiter runtime/client/src/predict.cpp
// Talon: the client shell holds its CClientMgr (no holders), objects are moved through the
// cm_ functions, interpolation keeps a per-object time left (cd.m_fMoveAccumulatedTime), and
// pd_OnObjectRotate times rotations from the previous rotation update.
#include <windows.h>		// before the StdLith headers (clientmgr.h brings in lthread.h)
#include "bdefs.h"
#include "de_objects.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "iclientshell.h"
#include "predict.h"
#include "motion.h"
#include "iltphysics.h"
#include "iltclient.h"
#include "engine_vars.h"


// Scale the server periods by this amount.. 1.1 looks good.
// GLOBAL: LITHTECH 0x004d55f8
static float g_fServerPeriodMultiplier
	= 1.1f;


// 0x00426860
void cm_MoveObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTBOOL bForce);
// 0x00426940
void cm_RotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTRotation *pNewRot);


// FUNCTION: LITHTECH 0x0046ddf0
void pd_InitialServerUpdate(CClientShell *pShell, float gameTime)
{
	pShell->m_ClientGameTime = gameTime;
	pShell->m_ClientGameTimerSync = pShell->m_pClientMgr->m_CurTime;

	dl_TieOff( &pShell->m_MovingObjects );
	dl_TieOff( &pShell->m_RotatingObjects );
}


// FUNCTION: LITHTECH 0x0046de20
void pd_OnObjectMove(CClientShell *pShell, LTObject *pObject, LTVector *pNewPos, LTVector *pNewVel, LTBOOL bNew, LTBOOL bTeleport)
{
	ClientData *pData;
	IClientShell *pClientShell;
	float fVelMagSqr, fUpdateDelta;

	pClientShell = pShell->m_pClientMgr->m_pClientShell;
	fVelMagSqr = pNewVel->MagSqr();
	pData = &pObject->cd;

	// Teleport the object if the new update is the same as the previous update
	// and it's not moving, or if it jumped too far.
	if (pNewPos->Equals(pData->m_LastUpdatePosServer, 0.1f) && (fVelMagSqr < 0.01f))
		bTeleport = LTTRUE;
	else if (!bTeleport && (fVelMagSqr < 1000000.0f) && ((pObject->GetPos() - *pNewPos).MagSqr() > 1000000.0f))
		bTeleport = LTTRUE;

	// Remember the last update we got from the server
	fUpdateDelta = pShell->m_ClientGameTime - pData->m_fLastUpdatePosTime;

	pData->m_fLastUpdatePosTime = pShell->m_ClientGameTime;
	pData->m_LastUpdatePosServer = *pNewPos;
	pData->m_LastUpdateVelServer = *pNewVel;
	pData->m_LastUpdatePosClient = pObject->GetPos();

	if(g_bPrediction)
	{
		if(!bNew && !bTeleport && fUpdateDelta != 0.0f)
		{
			dl_Remove(&pData->m_MovingLink);
			dl_Insert(&pShell->m_MovingObjects, &pData->m_MovingLink);

			pData->m_fMoveAccumulatedTime = fUpdateDelta * g_fServerPeriodMultiplier;
		}
		else
		{
			// It's a new object.. just initialize it as nonmoving.
			dl_Remove(&pData->m_MovingLink);
			dl_TieOff(&pData->m_MovingLink);

			if (pNewVel->MagSqr() > 0.01f)
				dl_Insert(&pShell->m_MovingObjects, &pData->m_MovingLink);

			pData->m_fMoveAccumulatedTime = 0.0f;

			// Just move it since we won't be interpolating its position.
			pClientShell->OnObjectMove((HOBJECT)pObject, bNew||bTeleport, pNewPos);
			cm_MoveObject(pShell->m_pClientMgr, pObject, pNewPos, LTTRUE);
		}
	}
	else
	{
		// Just move the object like normal.
		pClientShell->OnObjectMove((HOBJECT)pObject, bNew||bTeleport, pNewPos);
		cm_MoveObject(pShell->m_pClientMgr, pObject, pNewPos, LTTRUE);
	}
}


// FUNCTION: LITHTECH 0x0046e120
void pd_OnObjectRotate(CClientShell *pShell, LTObject *pObject, LTRotation *pNewRot, LTBOOL bNew, LTBOOL bSnap)
{
	ClientData *pClientData;
	IClientShell *pClientShell;
	float fUpdateDelta;

	pClientShell = pShell->m_pClientMgr->m_pClientShell;
	pClientData = &pObject->cd;

	fUpdateDelta = pShell->m_ClientGameTime - pClientData->m_fLastUpdateRotTime;
	pClientData->m_fLastUpdateRotTime = pShell->m_ClientGameTime;
	pClientData->m_rLastUpdateRotServer = *pNewRot;

	if(g_bPrediction)
	{
		if(!bNew && !bSnap && fUpdateDelta != 0.0f)
		{
			// Put it in the rotating object list.
			dl_Remove(&pClientData->m_RotatingLink);
			dl_Insert(&pShell->m_RotatingObjects, &pClientData->m_RotatingLink);

			// Add in the time...
			pClientData->m_fRotAccumulatedTime = fUpdateDelta * 2.0f;
		}
		else
		{
			// It's a new object.. just initialize it as nonmoving.
			dl_Remove(&pClientData->m_RotatingLink);
			dl_TieOff(&pClientData->m_RotatingLink);

			pClientData->m_fRotAccumulatedTime = 0.0f;

			// Just snap it since we won't be interpolating its rotation.
			pClientShell->OnObjectRotate((HOBJECT)pObject, bNew||bSnap, pNewRot);
			cm_RotateObject(pShell->m_pClientMgr, pObject, pNewRot);
		}
	}
	else
	{
		// Just move the object like normal.
		pClientShell->OnObjectRotate((HOBJECT)pObject, bNew||bSnap, pNewRot);
		cm_RotateObject(pShell->m_pClientMgr, pObject, pNewRot);
	}
}


// Where the object would be (Talon runs CalcMotion through a MotionState, and also saves and
// restores m_Flags).
// FUNCTION: LITHTECH 0x0046e720
static LTVector predict_EvaluateCurve(LTObject *pObj, const ClientData *pClientData, const LTVector &vGravity,
	float fInterpolant, float fTotalTime, LTBOOL bFullCalc)
{
	if(bFullCalc)
	{
		// Save the object's state
		LTVector vOldPos = pObj->GetPos();
		LTVector vOldVel = pObj->m_Velocity;
		LTVector vOldAccel = pObj->m_Acceleration;
		uint32 nOldInternalFlags = pObj->m_InternalFlags;
		uint32 nOldFlags = pObj->m_Flags;
		float fOldFriction = pObj->m_FrictionCoefficient;

		// Put it where the server last said it was
		pObj->SetPos(pClientData->m_LastUpdatePosServer);
		pObj->m_Velocity = pClientData->m_LastUpdateVelServer;
		pObj->m_Acceleration.Init();
		pObj->m_FrictionCoefficient = 0.0f;
		pObj->m_Flags |= FLAG_GRAVITY;

		// Calculate the movement info
		MotionState state;
		state.m_pObj = pObj;
		state.m_dt = fInterpolant * fTotalTime;
		state.m_Flags = pObj->m_Flags;
		state.m_pVelocity = &pObj->m_Velocity;
		state.m_pAcceleration = &pObj->m_Acceleration;
		state.m_Info.m_SlideRatio = 1.0f;
		state.SetForce(&vGravity);

		CalcMotion(&state);

		// Put the object back to its old state
		pObj->SetPos(vOldPos);
		pObj->m_Velocity = vOldVel;
		pObj->m_Acceleration = vOldAccel;
		pObj->m_InternalFlags = nOldInternalFlags;
		pObj->m_FrictionCoefficient = fOldFriction;
		pObj->m_Flags = nOldFlags;

		// Return the new position
		return state.m_Offset + pClientData->m_LastUpdatePosServer;
	}
	else
	{
		float fInterpTime = fTotalTime * fInterpolant;
		LTVector vServerPos = pClientData->m_LastUpdatePosServer + (pClientData->m_LastUpdateVelServer * fInterpTime) + vGravity * (fInterpTime * fInterpTime * 0.5f);
		return vServerPos;
	}
}


// Interpolates the objects towards their last server update.
// FUNCTION: LITHTECH 0x0046e250
void pd_Update(CClientShell *pShell)
{
	float timeDelta, curTime;
	LTLink *pCur;
	LTObject *pObj;
	LTVector newPos;
	float fTimeLeft, fParam;
	LTRotation rRot;
	IClientShell *pClientShell;

	pClientShell = pShell->m_pClientMgr->m_pClientShell;

	if(g_bPrediction)
	{
		// Figure out the delta..
		curTime = pShell->m_pClientMgr->m_CurTime;
		timeDelta = curTime - pShell->m_ClientGameTimerSync;
		if(timeDelta < 0.0f)
			timeDelta = 0.0f;

		pShell->m_ClientGameTime += timeDelta;
		pShell->m_ClientGameTimerSync = curTime;

		// Get global gravity
		LTVector vGravity, vGlobalGravity;
		pShell->m_pClientMgr->m_pClientDE->Physics()->GetGlobalForce(vGlobalGravity);

		// Interpolate movement of each object.
		pCur = pShell->m_MovingObjects.m_pNext;
		while(pCur != &pShell->m_MovingObjects)
		{
			pObj = (LTObject*)pCur->m_pData;
			pCur = pCur->m_pNext;

			ClientData *pClientData = &(pObj->cd);

			if((pObj->m_Flags & FLAG_GRAVITY) != 0)
				vGravity = vGlobalGravity;
			else
				vGravity.Init();

			float fTimeOffset = pShell->m_ClientGameTime - pClientData->m_fLastUpdatePosTime;

			fTimeOffset = LTCLAMP(fTimeOffset, 0.0f, g_CV_MaxExtrapolateTime);

			LTBOOL bTeleport = LTFALSE;

			if((pClientData->m_LastUpdateVelServer.Mag() <= 0.0f) ||
				(g_CV_MaxExtrapolateTime <= 0.0f) ||
				(fTimeOffset == g_CV_MaxExtrapolateTime))
			{
				newPos = predict_EvaluateCurve(pObj, pClientData, LTVector(0.0f, 0.0f, 0.0f), 0.0f, g_CV_MaxExtrapolateTime, LTTRUE);
				if(fTimeOffset < g_CV_MaxExtrapolateTime)
				{
					newPos = (newPos + pObj->GetPos()) * 0.5f;
				}
				else
				{
					bTeleport = LTTRUE;
					dl_Remove(&pObj->cd.m_MovingLink);
					dl_TieOff(&pObj->cd.m_MovingLink);
				}
			}
			else
			{
				newPos = predict_EvaluateCurve(pObj, pClientData, vGravity, fTimeOffset / g_CV_MaxExtrapolateTime, g_CV_MaxExtrapolateTime, LTTRUE);
				newPos = (newPos + pObj->GetPos()) * 0.5f;
			}

			// Move it..
			pClientShell->OnObjectMove((HOBJECT)pObj, LTFALSE, &newPos);

			// Use the physics if it's solid, unless it needs to teleport
			if(((pObj->m_Flags & FLAG_SOLID) != 0) && (!bTeleport))
				pShell->m_pClientMgr->m_pClientDE->Physics()->MoveObject((HOBJECT)pObj, &newPos, 0);
			else
				cm_MoveObject(pShell->m_pClientMgr, pObj, &newPos, LTTRUE);
		}

		// Interpolate rotation of each object.
		pCur = pShell->m_RotatingObjects.m_pNext;
		while(pCur != &pShell->m_RotatingObjects)
		{
			pObj = (LTObject*)pCur->m_pData;
			pCur = pCur->m_pNext;

			fTimeLeft = LTMIN(pObj->cd.m_fRotAccumulatedTime, timeDelta);
			fParam = fTimeLeft / pObj->cd.m_fRotAccumulatedTime;

			LTRotation rTemp;
			quat_Slerp((float*)&rTemp, (float*)&pObj->m_Rotation, (float*)&pObj->cd.m_rLastUpdateRotServer, fParam);
			quat_Slerp((float*)&rRot, (float*)&pObj->m_Rotation, (float*)&rTemp, 0.5f);

			pObj->cd.m_fRotAccumulatedTime -= fTimeLeft;

			if(pObj->cd.m_fRotAccumulatedTime <= 0.0f)
			{
				rRot = pObj->cd.m_rLastUpdateRotServer;
				pObj->cd.m_fRotAccumulatedTime = 0.0f;
				dl_Remove(&pObj->cd.m_RotatingLink);
				dl_TieOff(&pObj->cd.m_RotatingLink);
			}

			pClientShell->OnObjectRotate((HOBJECT)pObj, LTFALSE, &rRot);
			cm_RotateObject(pShell->m_pClientMgr, pObj, &rRot);
		}
	}
}


// Out-of-line copy of the inline MotionState::SetForce (MotionState's constructor calls it):
// FUNCTION: LITHTECH 0x00411670 ?SetForce@MotionState@@QAEXPBV?$_CVector@M@@@Z
