// Talon engine object (Jupiter runtime/shared/src/de_objects.h, server/src/s_object.h).
// One layout for the whole engine: add members here as they're recovered; never redeclare
// LTObject in another header.
#ifndef __DE_OBJECTS_H__
#define __DE_OBJECTS_H__

#include "ltbasedefs.h"
#include "world_tree.h"
#include "packet.h"
#include "ltanimtracker.h"
#include "ltdynarray.h"
#include "iltspritecontrol.h"

struct ClassDef;
class ObjectMgr;
struct SharedTexture;
struct ObjectCreateStruct;

// Internal object flags (m_InternalFlags).
#define IFLAG_APPLYPHYSICS	(1<<6)

// A polygon vertex (0x18 bytes).
struct SPolyVertex
{
	LTVector	*m_Vec;			// 0x00
	float		m_U, m_V;		// 0x04 texture coordinates (surface effects keep them current)
	float		m_Unk0c[2];	// 0x0c renderer: guess: lightmap texture coordinates (u, v) in the poly's lightmap page (AssignPolyLightmapPage)
	uint8		m_Color[4];		// 0x14 r, g, b, a (a = 255)
};

// Talon world polygon / BSP node; only what CalcMotion uses.
struct WorldPoly
{
	uint8		m_Pad00[0x08];
	uint32		m_Unk08;		// 0x08 renderer: DrawPolyMgr's queued-vertex range of the poly (first << 16 | count)
	uint32		*m_pLMAnimRefs;	// 0x0c (light anim, entry) pairs in WorldBsp::m_PolyAnimRefs
	uint32		m_nLMAnimRefs;	// 0x10
	uint16		m_Flags;		// 0x14 WPF_ (de_world.h); bits 11-13 are the lightmap plane
	uint16		m_Index;		// 0x16 index in its WorldBsp (HPOLY low word)
	LTVector	m_Center;		// 0x18 (w_TransformWorldModel)
	float		m_Radius;		// 0x24 bounding sphere radius (w_CalcBoundingSpheres)
	LTPlane		*m_pPlane;		// 0x28
	void		*m_pSurface;	// 0x2c Surface* (de_world.h)
	uint8		m_Pad30[0x38 - 0x30];
	LTVector	m_Unknown38;	// 0x38 read from the world file (w_LoadWorldBsp)
	uint16		m_iNextSurfacePoly;	// 0x44 next poly on the same Surface (0xFFFF ends)
	uint16		m_Unk46;		// 0x46 renderer: frame code of the last frame the poly was visited (cleared on wrap, d3d_ClearWorldBspFrameCodes)
	void		*m_Unk48;		// 0x48 renderer: LightmapPage* (d3dren/lightmap.h) holding the poly's lightmap, 0 = none
	uint8		m_LMWidth;		// 0x4c lightmap size in samples
	uint8		m_LMHeight;		// 0x4d
	uint8		m_Unk4e[2];	// 0x4e renderer: guess: position (x, y) of the lightmap inside its page (texels)
	SPolyVertex	*m_pVertices;	// 0x50 points at m_Vertices unless the poly grew
	uint16		m_nVertices;	// 0x54
	uint16		m_nExtraVertices;	// 0x56 counted only when m_pVertices was reallocated
	SPolyVertex	m_Vertices[0];	// 0x58 the poly's own vertices (NAME: Jupiter de_world.h WorldPoly::m_Vertices); sizeof(WorldPoly) is 0x58

	LTPlane*	GetPlane()	{ return m_pPlane; }

	// Lightmap plane index (SelectLMPlaneVector) in bits 11-13 of m_Flags (w_LoadWorldBsp: the exe's
	// xor-on-memory merge is this &=/|= pair on a uint16).
	void		SetLMPlaneVector(uint16 iPlane)	{ m_Flags &= ~0x3800; m_Flags |= ((iPlane << 11) & 0x3800); }

	uint32		GetNumVertices()
	{
		return (m_pVertices != (SPolyVertex*)(this + 1)) ? m_nExtraVertices + m_nVertices : m_nVertices;
	}
};

struct Node
{
	Node()				{ Init(0); }
	Node(uint8 nFlags)	{ Init(nFlags); }	// de_nodes NODE_IN/NODE_OUT

	void		Init(uint8 nFlags)
	{
		m_Flags = nFlags;
		m_pPoly = LTNULL;
		m_PlaneType = 0;
		m_iLeaf = 0xFFFF;
		m_Sides[0] = m_Sides[1] = LTNULL;
		dl_TieOff(&m_Objects);
	}

	// Two inline levels (the /Od fullintersectline shows WorldPoly::GetPlane's own temp).
	LTPlane*	GetPlane()
	{
		if (m_pPoly && m_pPoly->m_pSurface)
			return m_pPoly->GetPlane();
		return LTNULL;
	}

	WorldPoly	*m_pPoly;		// 0x00
	Node		*m_Sides[2];	// 0x04 (impl_common ci_IsPointInsideBSP)
	CheapLTLink	m_Objects;		// 0x0c objects whose sphere lands on this node (de_nodes)
	uint16		m_iLeaf;		// 0x14 index into WorldBsp leaves, 0xFFFF if not a leaf
	uint8		m_Flags;		// 0x16 NF_
	uint8		m_PlaneType;	// 0x17
};

// Node::m_Flags.
#define NF_IN		1
#define NF_OUT		2

// Server-only object data (Jupiter s_object.h).
// An object attached to another (0x28 bytes).
class Attachment
{
public:
	LTransform		m_Offset;		// 0x00 transform offset.
	uint16			m_nParentID;	// 0x1c the parent object of this attachment.
	uint16			m_nChildID;		// 0x1e the child object of this attachment.
	uint32			m_iSocket;		// 0x20 Model node index (if the parent is not a model, this is -1).
	Attachment		*m_pNext;		// 0x24
};

struct ServerData
{
	ServerData()	{ for (int i=0; i < 4; i++) m_pSkins[i] = LTNULL; }

	LTLink		m_Links;		// 0x00 InterLinks this object is part of
	struct HHashElement	*m_hName;	// 0x0c element in CServerMgr::m_hNameTable
	float		m_NextUpdate;	// 0x10 time until the next update
	float		m_fDeactivationTime;	// 0x14
	float		m_fDeactivateTimer;		// 0x18 counts down from m_fDeactivationTime
	struct Client	*m_pClient;	// 0x1c the client this object belongs to
	LPBASECLASS	m_pObject;		// 0x20 the game object
	ClassDef	*m_pClass;		// 0x24
	LTLink		*m_pIDLink;		// 0x28 in CServerMgr::m_IDs
	CPacketRef	m_pSFXMsg;		// 0x2c special effect message (none if 0)
	struct UsedFile	*m_pFile;		// 0x30 model/sprite file
	struct UsedFile	*m_pSkins[4];	// 0x34 model skins (MAX_MODEL_TEXTURES)
	LTLink		m_ListNode;		// 0x44 in CServerMgr::m_Objects (active objects first)
	class LTObject	*m_pChangeNext;	// 0x50 next in CServerMgr::m_pChangeListHead
	uint32		m_bCreateFlag1;	// 0x54 ObjectCreateStruct::m_CreateFlags & 1
	uint16		m_ChangeFlags;	// 0x58 CF_
	uint16		m_NetFlags;		// 0x5a NETFLAG_
};

// Client-side object data (Jupiter de_objects.h ClientData), 0x5c bytes at LTObject+0x128.
// Offsets from predict.cpp.
struct ClientData
{
	float		m_fMoveAccumulatedTime;	// 0x00 (0x128) interpolation time left (Talon)
	float		m_fLastUpdatePosTime;	// 0x04 (0x12c)
	LTVector	m_LastUpdatePosServer;	// 0x08 (0x130)
	LTVector	m_LastUpdateVelServer;	// 0x14 (0x13c)
	LTVector	m_LastUpdatePosClient;	// 0x20 (0x148) object position when the update came in (Talon)
	LTLink		m_MovingLink;			// 0x2c (0x154)
	float		m_fRotAccumulatedTime;	// 0x38 (0x160)
	float		m_fLastUpdateRotTime;	// 0x3c (0x164) (Talon)
	LTRotation	m_rLastUpdateRotServer;	// 0x40 (0x168)
	LTLink		m_RotatingLink;			// 0x50 (0x178)
};

class LTObject : public WorldTreeObj
{
public:
	// vtable 0x004c7df4 (objectmgr).
	LTObject();						// 0x00466850
	LTObject(char objectType);		// 0x004668b0
	virtual ~LTObject();			// 0x004668e0

	virtual LTBOOL	InsertSpecial(WorldTree *pTree);			// 0x00466ea0
	virtual void	RemoveFromWorldTree();						// 0x00466ca0
	virtual void	GetBBox(LTVector &vMin, LTVector &vMax);	// 0x00466c60

	virtual void	Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct);	// 0x20 (0x00466990)
	virtual LTBOOL	IsServerObject()	{ return sd != LTNULL; }		// 0x24 (name unknown)
	virtual void	SetupTransform(LTMatrix &mat);						// 0x28 (0x00466c30)
	virtual float	GetRadius()			{ return m_Radius; }			// 0x2c
	virtual LTBOOL	IsMoveable()		{ return LTTRUE; }				// 0x30
	virtual LTBOOL	IsMainWorldModel()	{ return LTFALSE; }				// 0x34

	void			Clear();										// 0x00466a20
	void			SetPos(LTVector pos);							// 0x00466cc0
	void			SetDims(LTVector dims);							// 0x00466dc0

	const LTVector&	GetPos() const	{ return m_Pos; }
	const LTVector&	GetDims() const	{ return m_Dims; }
	LTBOOL			HasWorldModel()	{ return m_ObjectType == OT_WORLDMODEL || m_ObjectType == OT_CONTAINER; }

	ObjectMgr	*m_pObjectMgr;			// 0x5c
	LTLink		m_Link60;				// 0x60 (name unknown)
	LTLink		m_Link6C;				// 0x6c m_pData = this (name unknown)
	uint32		m_Unknown78;			// 0x78
	LTLink		m_Link;					// 0x7c link in ObjectMgr::m_ObjectLists
	uint32		m_Flags;				// 0x88
	uint32		m_Flags2;				// 0x8c
	uint32		m_UserFlags;			// 0x90
	uint8		m_ColorR;				// 0x94 RGBA color info.
	uint8		m_ColorG;				// 0x95
	uint8		m_ColorB;				// 0x96
	uint8		m_ColorA;				// 0x97
	Attachment	*m_Attachments;			// 0x98 Objects attached to this one.
	LTRotation	m_Rotation;				// 0x9c
	LTVector	m_Scale;				// 0xac
	uint16		m_ObjectID;				// 0xb8 index into CServerMgr::m_ObjectMap
	uint16		m_SerializeID;			// 0xba
	uint8		m_ObjectType;			// 0xbc OT_
	uint8		m_BPriority;			// 0xbd blocking priority
	uint8		m_PadBE[0xc0 - 0xbe];
	void		*m_pUserData;			// 0xc0
	LTVector	m_Velocity;				// 0xc4
	LTVector	m_Acceleration;			// 0xd0
	uint8		m_UnknownDC;			// 0xdc (5 by default)
	uint8		m_PadDD[0xe0 - 0xdd];
	float		m_FrictionCoefficient;	// 0xe0
	float		m_Mass;					// 0xe4
	float		m_ForceIgnoreLimitSqr;	// 0xe8
	LTObject	*m_pStandingOn;			// 0xec
	Node		*m_pNodeStandingOn;		// 0xf0
	LTVector	m_MinBox;				// 0xf4
	LTVector	m_MaxBox;				// 0x100
	LTLink		m_ObjectsStandingOn;	// 0x10c
	LTLink		m_StandingOnLink;		// 0x118
	uint32		m_InternalFlags;		// 0x124
	ClientData	cd;					// 0x128 client-side data (predict.cpp)
	uint8		m_Pad184[0x188 - 0x184];
	uint16		m_Unknown188;			// 0x188 client flags (ILTClient::Get/SetObjectClientFlags)
	uint8		m_Pad18A[0x190 - 0x18a];
	ServerData	*sd;					// 0x190
	LTVector	m_Pos;					// 0x194
	LTVector	m_Dims;					// 0x1a0
	float		m_Radius;				// 0x1ac
};

// Client PolyGrid object (Jupiter world/src/de_objects.h). Offsets from pg_Init/pg_Term.
class LTPolyGrid : public LTObject
{
public:
	LTPolyGrid();					// 0x00468270
	virtual ~LTPolyGrid();			// 0x004683a0
	virtual float	CalcRadius();	// 0x38 (0x00468310, name unknown)

	char			*m_Data;		// 0x1b0 The grid data.
	unsigned short	*m_Indices;		// 0x1b4 The precalculated index list.
	union
	{
		uint32		m_Unknown1B8[7];	// 0x1b8
		struct
		{
			struct Sprite		*m_pSprite;			// 0x1b8 SetPolyGridTexture
			uint8				m_SpriteTracker[0x14];	// 0x1bc SpriteTracker (sprite.h)
			struct SharedTexture	*m_pEnvMap;		// 0x1d0 SetPolyGridEnvMap
		};
	};
	float			m_xPan, m_yPan;		// 0x1d4
	float			m_xScale, m_yScale;	// 0x1dc
	uint32			m_nTris;		// 0x1e4
	uint32			m_nIndices;		// 0x1e8
	LTLink			m_LeafLinks;	// 0x1ec
	uint32			m_Width;		// 0x1f8
	uint32			m_Height;		// 0x1fc
	uint32			m_ColorTable[0x400];	// 0x200
};

// World model object (Jupiter world/src/de_objects.h). Offsets from obj_SetupWorldModelTransform
// and w_TransformWorldModel.
class WorldBsp;
// vtable 0x004c7e2c (objectmgr).
class WorldModelInstance : public LTObject
{
public:
	WorldModelInstance();							// 0x00466ec0
	WorldModelInstance(char objectType);			// 0x00466f00
	virtual ~WorldModelInstance();					// 0x00466f20

	virtual LTBOOL	InsertSpecial(WorldTree *pTree);	// 0x00467220
	virtual void	RemoveFromWorldTree();				// 0x004671f0
	virtual LTBOOL	WTSlot4();							// 0x00467060
	virtual LTLink*	WTSlot5(uint32 i);					// 0x00467070
	virtual void	WTSlot6(LTObject *pObj);			// 0x00467080
	virtual void	WTSlot7(void *p);					// 0x004670a0
	virtual void	Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct);	// 0x00466f40
	virtual LTBOOL	IsMoveable();						// 0x00467300
	virtual LTBOOL	IsMainWorldModel();					// 0x00467310
	virtual LTBOOL	WMSlot14();							// 0x004673a0 (name unknown)

	void		Clear();							// 0x00466f50
	void		InitWorldData(WorldBsp *pOriginalBsp, WorldBsp *pWorldBsp);	// 0x00467320
	LTBOOL		IsUntransformed();					// 0x00467380 (name unknown)
	LTBOOL		IsPointInside(const LTVector *pPos);	// 0x004673b0 (name unknown)

	// 0x00467350
	HPOLY		MakeHPoly(Node *pNode);

	WorldBsp	*m_pOriginalBsp;	// 0x1b0 The original BSP (untransformed).
	WorldBsp	*m_pWorldBsp;		// 0x1b4 The transformed BSP.
	WorldBsp	*m_pValidBsp;		// 0x1b8 The BSP to use (m_pWorldBsp if transformed).
	LTLink		m_TreeLinks[MAX_OBJ_NODE_LINKS];	// 0x1bc m_pData = this
	LTMatrix	m_Transform;		// 0x1f8 World model transform.
	LTMatrix	m_BackTransform;	// 0x238 Inverse transform.
};

// Container object (OT_CONTAINER).
class ContainerInstance : public WorldModelInstance
{
public:
	ContainerInstance();			// 0x00468500
	virtual ~ContainerInstance();	// 0x00468540
	virtual void	Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct);	// 0x00468550

	uint16		m_ContainerCode;	// 0x278
};

// Model object (OT_MODEL). Offsets from serverde_impl, objectmgr and modellt_impl.
// vtable 0x004c7e68 (LTObject's 14 slots, then 14-20).
class Model;
#define MAX_MODEL_TEXTURES	4
class ModelInstance : public LTObject
{
public:
	virtual float	GetRadius();										// 0x2c (0x00467650)
	virtual void	SetupTransformMaker(class TransformMaker *pMaker);	// 0x38 (0x00467790, name unknown)
	virtual void	SetupTransformMakerAnims(class TransformMaker *pMaker);	// 0x3c (0x00467800, name unknown)
	virtual LTBOOL	FindTracker(LTAnimTracker *pTracker, LTAnimTracker **&ppPrev);	// 0x40 (0x00467850)
	virtual float	GetScaledRadius();									// 0x44 (0x00467660, name unknown)
	virtual LTMatrix*	GetTransforms();								// 0x48 (0x004678b0)
	virtual LTMatrix*	GetNodeTransform(uint32 iNode);					// 0x4c (0x004678d0)
	virtual LTBOOL	IsTransformCacheValid();							// 0x50 (0x004679b0, name unknown)

	ModelInstance();				// 0x004674a0
	virtual ~ModelInstance();		// 0x00467710

	Model*			GetModelDB()	{ return m_AnimTracker.GetModel(); }
	uint32			NumAnimTrackers();		// 0x00467890
	const char*		GetModelFilename();		// 0x00467e90 (name unknown)
	void			UpdateTransforms();		// 0x00467b70 (name unknown)

	uint32			m_Unknown1B0;	// 0x1b0
	uint32			m_Unknown1B4;	// 0x1b4
	uint32			m_Unknown1B8;	// 0x1b8
	ModelInstanceHookFn	m_HookFn;	// 0x1bc
	SharedTexture	*m_pSkins[MAX_MODEL_TEXTURES];	// 0x1c0
	LTAnimTracker	m_AnimTracker;	// 0x1d0 main animation tracker
	LTAnimTracker	*m_AnimTrackers;	// 0x220 tracker list (starts with m_AnimTracker)
	CMoArray<FrameLocator, NoCache>	m_TrackerStates;	// 0x224 tracker state the transform cache was built from
	CMoArray<LTMatrix, NoCache>		m_Transforms;		// 0x234 cached node transforms
	NodeControlFn	m_NodeControlFn;			// 0x244 ILTClient::ModelNodeControl
	void			*m_pNodeControlUserData;	// 0x248
	uint32			m_HiddenPieces;	// 0x24c one bit per piece
	struct Sprite	*m_pSprites[MAX_MODEL_TEXTURES];	// 0x250 (non-NULL when the skin comes from a sprite)
	uint8			m_SpriteTrackers[MAX_MODEL_TEXTURES][0x14];	// 0x260
	LTVector		m_Unknown2B0;		// 0x2b0
	float			m_Unknown2BC;		// 0x2bc (-1 by default)
	LTVector		m_ModelLighting;	// 0x2c0
	uint32			m_Unknown2CC;		// 0x2cc (1 by default)
};

// Client-side object types (layouts from clientde_impl).

// Sprite object (OT_SPRITE).
// ILTSpriteControl implementation embedded in SpriteInstance (vtable 0x004c7f28).
class SpriteInstance;
class SpriteControlImpl : public ILTSpriteControl
{
public:
	virtual LTRESULT	GetNumAnims(uint32 &nAnims);
	virtual LTRESULT	GetNumFrames(uint32 iAnim, uint32 &nFrames);
	virtual LTRESULT	GetCurPos(uint32 &iAnim, uint32 &iFrame);
	virtual LTRESULT	SetCurPos(uint32 iAnim, uint32 iFrame);
	virtual LTRESULT	GetFlags(uint32 &flags);
	virtual LTRESULT	SetFlags(uint32 flags);

	// Lets code cast the member straight to the interface.
	operator ILTSpriteControl*()	{ return this; }

	SpriteInstance	*m_pSprite;		// 0x04
};

class SpriteInstance : public LTObject
{
public:
	SpriteInstance();				// 0x00467eb0
	virtual ~SpriteInstance();		// 0x00467f70
	virtual float	CalcRadius();	// 0x38 (0x00467f20, name unknown)

	uint8			m_SpriteTracker[0x14];	// 0x1b0 SpriteTracker (sprite.h)
	HPOLY			m_ClipperPoly;			// 0x1c4 Poly index if this sprite is clipped.
	SpriteControlImpl	m_SCImpl;			// 0x1c8 ILTSpriteControl implementation
};

// Particle (Talon order, 0x3c bytes).
struct PSParticle
{
	LTVector		m_Velocity;			// 0x00
	LTVector		m_Color;			// 0x0c 0-255
	float			m_Alpha;			// 0x18 0-1
	float			m_Size;				// 0x1c
	PSParticle		*m_pNext;			// 0x20
	LTVector		m_Pos;				// 0x24
	float			m_Lifetime;			// 0x30 Current lifetime left
	float			m_TotalLifetime;	// 0x34 Total lifetime (i.e. initial value)
	PSParticle		*m_pPrev;			// 0x38
};

// Particle system object (OT_PARTICLESYSTEM).
struct StructBank_t;
class LTParticleSystem : public LTObject
{
public:
	LTParticleSystem();				// 0x00468070
	virtual ~LTParticleSystem();	// 0x004681b0
	virtual void	Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct);	// 0x00468250

	PSParticle		m_ParticleHead;		// 0x1b0 Lists of particles.
	struct StructBank_t	*m_pParticleBank;	// 0x1ec Where the particles come from.
	struct SharedTexture	*m_pCurTexture;	// 0x1f0 Current texture for particles.
	uint8			m_SoftwareR;		// 0x1f4
	uint8			m_SoftwareG;		// 0x1f5
	uint8			m_SoftwareB;		// 0x1f6
	uint8			m_Padding;			// 0x1f7
	struct Sprite	*m_pSprite;			// 0x1f8
	uint8			m_SpriteTracker[0x14];	// 0x1fc
	LTVector		m_SystemCenter;		// 0x210
	float			m_SystemRadius;		// 0x21c
	LTVector		m_OldCenter;		// 0x220
	float			m_OldRadius;		// 0x22c
	int				m_nParticles;		// 0x230 Total number of particles in the system.
	int				m_nChangedParticles;	// 0x234 New or changed particles since the last update.
	LTVector		m_MinPos;			// 0x238 Min and max particle positions.
	LTVector		m_MaxPos;			// 0x244
	float			m_GravityAccel;		// 0x250
	float			m_ParticleRadius;	// 0x254
	int32			m_Unknown258;		// 0x258 (-1 by default)
	int32			m_Unknown25C;		// 0x25c (-1 by default)
	uint32			m_Unknown260;		// 0x260 psFlags (ILTClient::SetupParticleSystem)
};

// Canvas object (OT_CANVAS).
class Canvas : public LTObject
{
public:
	Canvas();						// 0x00468570
	virtual float	GetRadius();	// 0x004685a0
	virtual float	CalcRadius();	// 0x38 (folded with GetRadius by the linker, name unknown)

	CanvasDrawFn	m_Fn;				// 0x1b0
	void			*m_pFnUserData;		// 0x1b4
	float			m_CanvasRadius;		// 0x1b8
};

// Dynamic light object (OT_LIGHT), 0x1b4 bytes. vtable 0x004c7f44.
class DynamicLight : public LTObject
{
public:
	DynamicLight();					// 0x00467f80
	virtual ~DynamicLight();		// 0x00467fd0
	virtual float	GetLightRadius(uint32 unused);	// 0x38 (0x00467fa0, name unknown)

	float			m_LightRadius;		// 0x1b0
};

// Camera object (OT_CAMERA), 0x1d8 bytes. vtable 0x004c7f80.
class CameraInstance : public LTObject
{
public:
	CameraInstance();				// 0x00467fe0
	virtual ~CameraInstance();		// 0x00468060

	int				m_Left, m_Top, m_Right, m_Bottom;	// 0x1b0
	float			m_xFov, m_yFov;		// 0x1c0
	int				m_bFullScreen;		// 0x1c8
	LTVector		m_LightAdd;			// 0x1cc
};

// One line in a LineSystem (0x44 bytes).
struct LSLinePt
{
	LTVector		m_Pos;				// 0x00
	float			r, g, b, a;			// 0x0c
};

struct LSLine
{
	LSLinePt		m_Points[2];		// 0x00
	class LineSystem	*m_pSystem;		// 0x38
	LSLine			*m_pPrev;			// 0x3c
	LSLine			*m_pNext;			// 0x40
};

// Line system object (OT_LINESYSTEM), 0x224 bytes. vtable 0x004c8030.
class LineSystem : public LTObject
{
public:
	LineSystem();					// 0x004683f0
	virtual ~LineSystem();			// 0x00468480
	virtual void	Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct);	// 0x004684e0

	StructBank_t	*m_pLineBank;		// 0x1b0 Where the lines come from.
	LTBOOL			m_bChanged;			// 0x1b4
	LSLine			m_LineHead;			// 0x1b8
	// NOTE: Talon's linesystem.cpp (0x00445170) uses 0x1fc as the min extent, 0x208 as the max,
	// 0x214 as the center and 0x220 as the radius. The names below follow Jupiter's order,
	// which objectmgr.cpp's constructor relies on; linesystem.cpp uses the macros below.
	LTVector		m_SystemCenter;		// 0x1fc
	float			m_SystemRadius;		// 0x208
	LTVector		m_MinPos;			// 0x20c
	LTVector		m_MaxPos;			// 0x218
};

#endif  // __DE_OBJECTS_H__
