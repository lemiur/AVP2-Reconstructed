// Object movement (Talon layout recovered from lithtech.exe; Jupiter shared/src/moveobject.h).
#ifndef __MOVEOBJECT_H__
#define __MOVEOBJECT_H__

#include "ltbasedefs.h"
#include "de_objects.h"

class MoveState;
class WorldTree;
class ILTPhysics;

// MoveObject flags.
#define MO_DETACHSTANDING	(1<<0)
#define MO_SETCHANGEFLAG	(1<<1)
#define MO_MOVESTANDINGONS	(1<<2)
#define MO_TELEPORT			(1<<3)
#define MO_GOTHRUWORLD		(1<<4)	// Don't do world collision.
#define MO_NOSLIDING		(1<<5)	// Talon: just set the position (no collisions), then update the BSP and world tree (MOVEOBJECT_NCTELEPORT).

// The abstraction MoveObject uses to talk to the client or the server.
class MoveAbstract
{
public:
	virtual void			SetObjectChangeFlags(LTObject *pObj, uint32 flags)=0;
	virtual CollisionInfo *&GetCollisionInfo()=0;
	virtual void			DoTouchNotify(LTObject *pMain, LTObject *pTouching, LTVector &stopVel, float force)=0;
	virtual void			PutObjectInContainer(LTObject *pObj, LTObject *pContainer)=0;
	virtual void			BreakContainerLinks(LTObject *pObj)=0;
	virtual void			MoveAttachments(MoveState *pState)=0;
	virtual LTBOOL			ShouldPushObject(MoveState *pState, LTObject *pPusher, LTObject *pPushee)=0;
	virtual void			DoCrush(LTObject *pObject, LTObject *pCrusher)=0;
	virtual void			CheckMaxPos(MoveState *pState, LTVector *pPos)=0;
	virtual uint32			IsServer()=0;
	virtual LTBOOL			CanOptimizeObject(LTObject *pObject)=0;
	virtual char*			GetObjectClassName(LTObject *pObject)=0;
	virtual ILTPhysics*		GetPhysics()=0;
	virtual LTRESULT		GetGlobalForce(LTObject *pObj, LTVector *pForce)=0;	// Talon only
};


// 0x6c bytes.
class MoveState
{
public:
	MoveState()
	{
		m_pWorldTree = LTNULL;
		m_pAbstract = LTNULL;
		m_pObj = LTNULL;
		m_Unknown64 = 0;
		m_nRestart = 0.0f;
	}

	void Setup(WorldTree *pWorldTree,
		MoveAbstract *pAbstract, LTObject *pObj, uint32 bPriority)
	{
		m_pWorldTree = pWorldTree;
		m_pAbstract = pAbstract;
		m_pObj = pObj;
		m_BPriority = bPriority;
		m_CustomTestObjects = LTNULL;
		m_nCustomTestObjects = 0;
	}

// Set these before calling MoveObject.
	WorldTree		*m_pWorldTree;			// 0x00
	MoveAbstract	*m_pAbstract;			// 0x04
	LTObject		*m_pObj;				// 0x08
	uint32			m_BPriority;			// 0x0c What blocking priority are we using?
	LTObject		**m_CustomTestObjects;	// 0x10 Tells it to only test these objects for collision.
	uint32			m_nCustomTestObjects;	// 0x14

	void			SetupCall()
	{
		m_bServer = m_pAbstract->IsServer();
	}

	void			Inherit(MoveState *pOther, LTObject *pObj)
	{
		Setup(pOther->m_pWorldTree,
			pOther->m_pAbstract, pObj, pOther->m_BPriority);
	}

// Used internally, don't set.
	uint32			m_bServer;				// 0x18
	const LTVector	*m_pStartPos;			// 0x1c
	LTVector		m_vDestPos;				// 0x20
	LTVector		m_vDeltaPos;			// 0x2c
	LTVector		m_vMoveCenter;			// 0x38
	float			m_fMoveRadius;			// 0x44
	LTVector		m_vMoveMin;				// 0x48
	LTVector		m_vMoveMax;				// 0x54
	LTMatrix		*m_pWMObjectTransform;	// 0x60
	uint32			m_Unknown64;			// 0x64
	float			m_nRestart;				// 0x68 the height the stair step code raised the cylinder (a float)
};

// LTObject::m_InternalFlags.
#define IFLAG_MOVING		(1<<1)	// Set while MoveObject moves the object.

// Change flags MoveObject sets (MoveAbstract::SetObjectChangeFlags).
#ifndef CF_POSITION
#define CF_POSITION			(1<<1)
#endif
#ifndef CF_TELEPORT
#define CF_TELEPORT			(1<<9)
#endif

// Sets up the necessary structures to make pObj stand on pStandingOn (0x0045d190).
void SetObjectStanding(LTObject *pObj, LTObject *pStandingOn, Node *pNode);

// Detach this object from whatever it's standing on (0x0045d110).
void DetachObjectStanding(LTObject *pObj);

// Detach any objects standing on this object (0x0045d150).
void DetachObjectsStandingOn(LTObject *pObj);

// Retransforms a WorldModel's BSP to its current position/rotation (0x0045d1e0).
void RetransformWorldModel(WorldModelInstance *pWorldModel);

// Called when a WorldModel is created to setup its bounding box for its initial rotation (0x0045d570).
void InitialWorldModelRotate(WorldModelInstance *pInstance);

// Process a non-solid collision (0x0045ea50).
void DoNonsolidCollision(MoveAbstract *pAbstract, LTObject *pObj1, LTObject *pObj2);

// THE function to move an object (0x0045d5b0).
void MoveObject(MoveState *pState, LTVector moveTo, uint32 flags);

// 0x00461490. Returns TRUE if the object could take the new dimensions.
LTBOOL ChangeObjectDimensions(MoveState *pState, LTVector *pNewDims, uint32 bPushObjects, LTBOOL bUnknown);

// Rotates the world model and moves objects out of the way (0x004619f0).
void RotateWorldModel(MoveState *pState, LTRotation *pNewRot, LTBOOL bForce);

extern uint32 g_Ticks_MoveObject;
extern uint32 g_nMoveObjectCalls;

#endif  // __MOVEOBJECT_H__
