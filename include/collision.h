// Talon world collision (Jupiter runtime/shared/src/collision.h). Layouts from
// CollideAgainstWorld (moveobject).
#ifndef __COLLISION_H__
#define __COLLISION_H__

#include "ltbasedefs.h"

class MoveAbstract;
class MoveState;
class WorldBsp;
class LTObject;
struct Node;

// 0x4c bytes.
struct CollideRequest
{
	// Constructor
	CollideRequest() : m_Unknown44(0), m_pRestart(LTNULL) {};

	// Abstraction layer.
	MoveAbstract	*m_pAbstract;		// 0x00
	CollisionInfo	*m_pCollisionInfo;	// 0x04
	uint32			m_bServer;			// 0x08 (MoveState::m_bServer)

	// The world to collide against.
	WorldBsp		*m_pWorld;			// 0x0c
	LTObject		*m_pWorldObj;		// 0x10

	// The movement.
	LTVector		m_OriginalPos;		// 0x14
	LTVector		m_NewPos;			// 0x20

	// Dimensions of the object trying to move.
	LTVector		m_Dims;				// 0x2c

	// FLAG_STAIRSTEP is set on the object.
	LTBOOL			m_bStairStep;		// 0x38

	// The LTObject doing the movement.  This MUST be set.  It will calculate
	// collision response, modify its velocity, and notify the object.
	LTObject		*m_pObject;			// 0x3c

	// Should the object slide along polygons
	LTBOOL			m_bSlide;			// 0x40

	uint32			m_Unknown44;		// 0x44 (MoveState::m_Unknown64)
	float			*m_pRestart;		// 0x48 (&MoveState::m_nRestart); the stair step code of CollideWithWorld reads and
										//      adds to it as a float (the height the cylinder was raised)
};

// 0x2c bytes.
struct CollideInfo
{
	// The final (unclipped) position the object wound up at.
	LTVector	m_FinalPos;			// 0x00

	// CollideWithWorld() will set this, telling you how many times it pushed the object off of something.
	uint32		m_nHits;			// 0x0c

	// If FLAG_STAIRSTEP is specified, this will set m_pStandingOn to
	// NULL or the object that it is standing on.
	Node		*m_pStandingOn;		// 0x10

	// Force of collisions
	LTVector	m_vForce;			// 0x14

	// This is the velocity offset.  You should apply this after calling CollideWithWorld.
	// This is here so you can do a touch notify on the object before changing the velocity.
	LTVector	m_VelOffset;		// 0x20
};


// Talon spheres keep the radius first.
struct PhysicsSphere
{
	float		m_Radius;
	LTVector	m_Center;
};

// The moving box's bounding spheres at the start and end of the movement (SetupBox).
// GLOBAL: LITHTECH 0x004e0ca0
extern PhysicsSphere g_StartSphere;
// GLOBAL: LITHTECH 0x004e24b0
extern PhysicsSphere g_EndSphere;
// The radius of the moving box (from its dims).
// GLOBAL: LITHTECH 0x004e0494
extern float g_BoxRadius;
// PolyTouchesBox's profiling counters (reset by CollideWithWorld).
// GLOBAL: LITHTECH 0x004e0490
extern uint32 g_Ticks_PolyTouchesBox;
// GLOBAL: LITHTECH 0x004e24a8
extern uint32 g_nPolyTouchesBoxCalls;

// "NewCollision" and "NewStairStep" console variables (the table at 0x004d2424).
// GLOBAL: LITHTECH 0x004d2164
extern int32 g_CV_NewCollision;
// 0 = the old (Talon) stair step and object collision path, else the newer one.
// GLOBAL: LITHTECH 0x004d2168
extern int32 g_CV_NewStairStep;


// The objects a sphere move can hit (shared by moveobject.cpp's MoveObject and collision.cpp's MoveSphere).
#define MAX_INTERSECTING_OBJECTS	128

struct IntersectingObject
{
	LTObject	*m_pObject;
	float		m_fDistSqr;		// From the start of the move to the overlap's center.
};

class IntersectingObjectArray
{
public:
	MoveState			*m_pState;		// 0x00
	int32				m_nObjects;		// 0x04
	IntersectingObject	m_Objects[MAX_INTERSECTING_OBJECTS];	// 0x08

	IntersectingObjectArray()
	{
		m_nObjects = 0;
	}
};

// Talon sphere physics (FLAG2_SPHEREPHYSICS) move request, 0x140 bytes.
struct SphereMoveInfo
{
	LTObject				*m_pObj;		// 0x00
	MoveState				*m_pState;		// 0x04
	IntersectingObjectArray	*m_pObjects;	// 0x08 the objects it can hit
	LTVector				m_vStartPos;	// 0x0c
	LTVector				m_vDestPos;		// 0x18 in: where it wants to go, out: where it ended up
	float					m_fRadius;		// 0x24
	float					m_fHitTime;		// 0x28
	LTVector				m_vHitNormal;	// 0x2c
	uint8					m_nIterations;	// 0x38
	uint8					m_Pad39[3];
	int32					m_nNormals;		// 0x3c
	LTVector				m_Normals[10];	// 0x40
	int32					m_nGroundNormals;	// 0xb8
	LTVector				m_GroundNormals[10];	// 0xbc
	int32					m_bGround;		// 0x134
	uint8					m_Pad138[0x140 - 0x138];
};

// Moves a sphere physics object (0x00419d40).
LTBOOL MoveSphere(SphereMoveInfo *pInfo);

// Does this box intersect this BSP tree? (0x0041aed0)
LTBOOL DoesBoxIntersectBSP(Node *pRoot, LTVector &vMin, LTVector &vMax);

// Collides the axis-aligned box with the world (0x0041bdd0).
void CollideWithWorld(CollideRequest &request, CollideInfo *pInfo);

// Does a collision response for the object on the node's plane: fills in the collision info and stops
// the object's velocity on pStopPlane (0x00419b20).  Talon passes the pieces of the request.
void DoObjectCollisionResponse(CollisionInfo *pCollisionInfo, CollideInfo *pInfo, LTObject *pObject,
	LTObject *pWorldObj, WorldBsp *pWorld, Node *pNode, LTVector *pStopPlane);

// Adds a plane the box got pushed out of (pID identifies the polygon to skip duplicates) and pushes the
// movement's end out of all the planes added so far (0x0041d770).
void AddPushPlane(LTPlane *pPlane, void *pID);

// Sets up the collision info and stopping velocities for two objects hitting each other (0x0041f3d0).
void DoInterObjectCollisionResponse(MoveAbstract *pAbstract, LTObject *pObj1, LTObject *pObj2,
	LTVector *pNormal, float fPlaneDist);

#endif  // __COLLISION_H__
