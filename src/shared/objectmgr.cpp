// Jupiter runtime/shared/src/objectmgr.cpp, Talon version.
// Talon: no tracker bank or volume effects, LTObject's bounding box/dims/radius setters are
// out of line, WorldModelInstance carries five extra world tree links, and ModelInstance
// caches its node transforms in a CMoArray.
#include <string.h>
#include "bdefs.h"
#include "objectmgr.h"
#include "de_world.h"
#include "geomroutines.h"
#include "model.h"
#include "animtracker.h"
#include "transformmaker.h"

#define WIF_MAINWORLD	(1<<2)
#define WIF_PHYSICSBSP	(1<<4)
#define WIF_VISBSP		(1<<5)

#define NOA_VisContainers	2

// WorldTree's root node is embedded at +0xc.
inline WorldTreeNode* GetRootNode(WorldTree *pTree)
{
	return (WorldTreeNode*)((uint8*)pTree + 0xc);
}

void dfree(void *ptr);


// GLOBAL: LITHTECH 0x004e45b8
LTLink g_ObjectMgrs(LTLink_Init);

// FUNCTION: LITHTECH 0x00466630 _$E2
// FUNCTION: LITHTECH 0x00466640 _$E1
// Header-emitted methods present in this object.
// FUNCTION: LITHTECH 0x0044f8f0 ??0AnimTimeRef@@QAE@XZ
// FUNCTION: LITHTECH 0x00450000 ?Init@LTMatrix@@QAEXMMMMMMMMMMMMMMMM@Z


// ------------------------------------------------------------------------- //
// ObjectMgr.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00466650
ObjectMgr::ObjectMgr() :
	m_ObjectBankNormal(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankModel(32, MODEL_PREALLOCATIONS),
	m_ObjectBankWorldModel(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankSprite(32, SPRITE_PREALLOCATIONS),
	m_ObjectBankLight(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankCamera(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankParticleSystem(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankPolyGrid(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankLineSystem(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankContainer(32, OBJECT_PREALLOCATIONS),
	m_ObjectBankCanvas(32, OBJECT_PREALLOCATIONS)
{
	m_CurFrameCode = 0;
	m_InternalLink.Init();
}


uint32 ObjectMgr::GetFrameCode()
{
	return m_CurFrameCode;
}


// FUNCTION: LITHTECH 0x00466800
uint32 ObjectMgr::IncFrameCode()
{
	LTLink *pListHead, *pCur;
	uint32 i;

	if (m_CurFrameCode == 0xFFFFFFFF)
	{
		m_CurFrameCode = 1;

		// Reset the frame code on all the objects.
		for (i=0; i < NUM_OBJECTTYPES; i++)
		{
			pListHead = &m_ObjectLists[i].m_Head;
			for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
			{
				((LTObject*)pCur->m_pData)->m_WTFrameCode = 0;
			}
		}

		return m_CurFrameCode;
	}

	return ++m_CurFrameCode;
}


// ------------------------------------------------------------------------- //
// LTObject.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00466850 ??0LTObject@@QAE@XZ
LTObject::LTObject() : WorldTreeObj(WTObj_DObject)
{
	Clear();
	m_ObjectType = OT_NORMAL;
}

// FUNCTION: LITHTECH 0x00466880 ?IsServerObject@LTObject@@UAEIXZ
// FUNCTION: LITHTECH 0x00466890 ?GetRadius@LTObject@@UAEMXZ
// FUNCTION: LITHTECH 0x004668a0 ?IsMoveable@LTObject@@UAEIXZ

// FUNCTION: LITHTECH 0x004668b0 ??0LTObject@@QAE@D@Z
LTObject::LTObject(char objectType) : WorldTreeObj(WTObj_DObject)
{
	Clear();
	m_ObjectType = objectType;
}

// FUNCTION: LITHTECH 0x004668e0
LTObject::~LTObject()
{
	if (m_pObjectMgr)
	{
		om_RemoveAttachments(m_pObjectMgr, this);
	}

	// Remove it from the moving object lists
	dl_Remove(&cd.m_MovingLink);
	if (cd.m_fRotAccumulatedTime != 0.0f)
	{
		dl_Remove(&cd.m_RotatingLink);
	}

	RemoveFromWorldTree();
}

// FUNCTION: LITHTECH 0x00466990
void LTObject::Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct)
{
	// Get stuff out of the struct.
	m_Flags		= pStruct->m_Flags;
	m_Flags2	= pStruct->m_Flags2;
	m_UserFlags	= pStruct->m_UserFlags;
	m_Pos		= pStruct->m_Pos;
	m_Rotation	= pStruct->m_Rotation;
	m_Scale		= pStruct->m_Scale;
	m_ObjectType	= (uint8)pStruct->m_ObjectType;
	m_pObjectMgr	= pMgr;
}

// FUNCTION: LITHTECH 0x00466a20
void LTObject::Clear()
{
	// Initialize all its data.
	m_pObjectMgr = LTNULL;
	dl_TieOff(&m_Link60);
	dl_TieOff(&m_Link6C);
	m_Unknown78 = 0;
	m_Link6C.m_pData = this;
	dl_TieOff(&m_Link60);
	dl_TieOff(&m_Link);
	m_Flags = 0;
	m_Flags2 = 0;
	m_UserFlags = 0;
	m_ColorR = m_ColorG = m_ColorB = 255;
	m_ColorA = 255;
	m_Attachments = LTNULL;
	m_Rotation.Init();
	m_Scale.Init(1.0f, 1.0f, 1.0f);
	m_ObjectID = 0xFFFF;
	m_SerializeID = 0xFFFF;
	m_ObjectType = OT_NORMAL;
	m_BPriority = 0;
	m_pUserData = LTNULL;
	m_Velocity.Init();
	m_Acceleration.Init();
	m_UnknownDC = 5;
	m_FrictionCoefficient = 5; // Default friction coefficient.
	m_Mass = 30; // Default mass is 30..
	m_ForceIgnoreLimitSqr = 1000.0f * 1000.0f; // ie: don't tell them unless they change this!
	m_pStandingOn = LTNULL;
	m_pNodeStandingOn = LTNULL;
	m_MinBox.Init();
	m_MaxBox.Init();
	dl_TieOff(&m_ObjectsStandingOn);
	m_StandingOnLink.m_pData = this;
	dl_TieOff(&m_StandingOnLink);
	m_InternalFlags = 0;
	m_Unknown188 = 0;

	// Init the prediction info.
	cd.m_fMoveAccumulatedTime = 0.0f;
	cd.m_fLastUpdatePosTime = 0.0f;
	cd.m_LastUpdatePosServer.Init();
	cd.m_LastUpdateVelServer.Init();
	cd.m_LastUpdatePosClient.Init();
	cd.m_MovingLink.m_pData = this;
	dl_TieOff(&cd.m_MovingLink);
	cd.m_fRotAccumulatedTime = 0.0f;
	cd.m_fLastUpdateRotTime = 0.0f;
	cd.m_rLastUpdateRotServer.Init();
	cd.m_RotatingLink.m_pData = this;
	dl_TieOff(&cd.m_RotatingLink);

	sd = LTNULL;
	m_Pos.Init();
	m_Dims.Init(1.0f, 1.0f, 1.0f);
	m_Radius = 0.0f;
}

// FUNCTION: LITHTECH 0x00466c30
void LTObject::SetupTransform(LTMatrix &mat)
{
	gr_SetupTransformation(&m_Pos, &m_Rotation, &m_Scale, &mat);
}

// FUNCTION: LITHTECH 0x00466c60
void LTObject::GetBBox(LTVector &vMin, LTVector &vMax)
{
	vMin = m_MinBox;
	vMax = m_MaxBox;
}

// FUNCTION: LITHTECH 0x00466ca0
void LTObject::RemoveFromWorldTree()
{
	if (!(m_Flags & FLAG_REALLYCLOSE))
	{
		w_RemoveObjectFromLeaf(this);
	}

	WorldTreeObj::RemoveFromWorldTree();
}

// FUNCTION: LITHTECH 0x00466cc0
void LTObject::SetPos(LTVector pos)
{
	m_Pos = pos;
	m_MinBox = m_Pos - m_Dims;
	m_MaxBox = m_Pos + m_Dims;
	m_Radius = m_Dims.Mag() + 0.1f;
}

// FUNCTION: LITHTECH 0x00466dc0
void LTObject::SetDims(LTVector dims)
{
	m_Dims = dims;
	m_MinBox = m_Pos - m_Dims;
	m_MaxBox = m_Pos + m_Dims;
	m_Radius = m_Dims.Mag() + 0.1f;
}

// FUNCTION: LITHTECH 0x00466ea0
LTBOOL LTObject::InsertSpecial(WorldTree *pTree)
{
	// Objects which are camera-relative don't get inserted into the world tree
	if (m_Flags & FLAG_REALLYCLOSE)
	{
		// Insert them into the constant visibility list
		pTree->InsertAlwaysVisObject(this);
		return LTTRUE;
	}
	else
	{
		return LTFALSE;
	}
}


// ------------------------------------------------------------------------- //
// WorldModelInstance.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00466ec0 ??0WorldModelInstance@@QAE@XZ
WorldModelInstance::WorldModelInstance()
	: LTObject(OT_WORLDMODEL)
{
	Clear();
}

// FUNCTION: LITHTECH 0x00466ee0 ??_GWorldModelInstance@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00466f00 ??0WorldModelInstance@@QAE@D@Z
WorldModelInstance::WorldModelInstance(char objectType)
	: LTObject(objectType)
{
	Clear();
}

// FUNCTION: LITHTECH 0x00466f20
WorldModelInstance::~WorldModelInstance()
{
	RemoveFromWorldTree();
}

// FUNCTION: LITHTECH 0x00466f40
void WorldModelInstance::Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct)
{
	LTObject::Init(pMgr, pStruct);
}

// FUNCTION: LITHTECH 0x00466f50
void WorldModelInstance::Clear()
{
	uint32 i;

	m_ColorR = m_ColorG = m_ColorB = 255;

	m_pOriginalBsp = LTNULL;
	m_pWorldBsp = LTNULL;
	m_pValidBsp = LTNULL;

	m_Transform.Identity();
	m_BackTransform.Identity();

	for (i=0; i < MAX_OBJ_NODE_LINKS; i++)
	{
		dl_TieOff(&m_TreeLinks[i]);
		m_TreeLinks[i].m_pData = this;
	}
}

// FUNCTION: LITHTECH 0x00467060
LTBOOL WorldModelInstance::WTSlot4()
{
	return m_pOriginalBsp->WBSlot9();
}

// FUNCTION: LITHTECH 0x00467070
LTLink* WorldModelInstance::WTSlot5(uint32 i)
{
	return &m_TreeLinks[i];
}

// FUNCTION: LITHTECH 0x00467080
void WorldModelInstance::WTSlot6(LTObject *pObj)
{
	w_AddObjectToLeaf(m_pOriginalBsp, pObj);
}

// The default callbacks of a vis query (empty functions in the engine).
void vq_DefaultFn1();		// 0x004a4ad0
void vq_DefaultFn2();		// 0x004a4ad0
LTBOOL vq_DefaultBoolFn();	// 0x004668a0

// The vis query state the BSP vis code works from (0x0049e330 takes it and keeps it in g_pCurVisQuery).
struct VisQueryInfo
{
	VisQueryInfo()
	{
		m_pLeaf = LTNULL;
		m_pBsp = LTNULL;
		m_AddObject = (VQAddObjectFn)vq_DefaultFn1;
		m_Unknown0C = (void*)vq_DefaultFn2;
		m_Unknown10 = LTNULL;
		m_Unknown14 = (void*)vq_DefaultBoolFn;
		m_pUserData = LTNULL;
	}

	Leaf			*m_pLeaf;		// 0x00 the leaf the viewpoint is in (LTNULL if none)
	WorldBsp		*m_pBsp;		// 0x04
	VQAddObjectFn	m_AddObject;	// 0x08 VisQueryRequest::m_AddObject
	void			*m_Unknown0C;	// 0x0c VisQueryRequest::m_Unknown20
	void			*m_Unknown10;	// 0x10 VisQueryRequest::m_Unknown18
	void			*m_Unknown14;	// 0x14 VisQueryRequest::m_Unknown24
	void			*m_pUserData;	// 0x18 VisQueryRequest::m_pUserData
	uint32			m_FrameCode;	// 0x1c the world tree's frame code
};

void wb_Unknown49e330(void *p);		// 0x0049e330

// A vis query comes across a vis container: runs it through the BSP from the viewpoint's leaf.
// FUNCTION: LITHTECH 0x004670a0
void WorldModelInstance::WTSlot7(void *p)
{
	VisQueryRequest *pInfo;
	WorldBsp *pBsp;
	VisQueryInfo query;
	Node *pNode;
	Leaf *pLeaf;
	uint16 iLeaf;
	LTPlane *pPlane;
	float d;

	pInfo = (VisQueryRequest*)p;
	pBsp = m_pOriginalBsp;
	if (pBsp->m_Unknown150 == pInfo->m_pTree->GetFrameCode())
		return;

	pBsp->m_Unknown150 = pInfo->m_pTree->GetFrameCode();

	iLeaf = 0xFFFF;
	pNode = pBsp->m_RootNode;
	while (!(pNode->m_Flags & (NF_IN|NF_OUT)))
	{
		if (pNode->m_iLeaf != 0xFFFF)
			iLeaf = pNode->m_iLeaf;

		pPlane = pNode->GetPlane();
		d = pPlane->DistTo(pInfo->m_Viewpoint);
		pNode = pNode->m_Sides[d >= -0.0001f];
	}

	pLeaf = (iLeaf == 0xFFFF) ? LTNULL : &pBsp->m_Leafs[iLeaf];

	query.m_AddObject = pInfo->m_AddObject;
	query.m_pLeaf = pLeaf;
	query.m_pBsp = pBsp;
	query.m_pUserData = pInfo->m_pUserData;
	query.m_Unknown0C = pInfo->m_Unknown20;
	query.m_Unknown10 = pInfo->m_Unknown18;
	query.m_FrameCode = pInfo->m_pTree->GetFrameCode();
	query.m_Unknown14 = pInfo->m_Unknown24;
	wb_Unknown49e330(&query);
}

// FUNCTION: LITHTECH 0x004671f0
void WorldModelInstance::RemoveFromWorldTree()
{
	uint32 i;

	for (i=0; i < MAX_OBJ_NODE_LINKS; i++)
	{
		dl_Remove(&m_TreeLinks[i]);
	}

	LTObject::RemoveFromWorldTree();
}

// FUNCTION: LITHTECH 0x00467220
LTBOOL WorldModelInstance::InsertSpecial(WorldTree *pTree)
{
	WorldTreeNode *pNode;

	if (!m_pOriginalBsp)
		return LTFALSE;

	// Physics BSP and vis BSPs go on the root node.
	if (m_pOriginalBsp->GetWorldInfoFlags() & WIF_PHYSICSBSP)
	{
		GetRootNode(pTree)->AddObjectToList(&m_Links[0], NOA_Objects);
		return LTTRUE;
	}
	else if (m_pOriginalBsp->GetWorldInfoFlags() & WIF_VISBSP)
	{
		GetRootNode(pTree)->AddObjectToList(&m_Links[0], NOA_VisContainers);
		return LTTRUE;
	}

	// Terrain sections go on the node they were built for.
	if (IsUntransformed() && m_pOriginalBsp->IsUntransformed() == 1)
	{
		WTNodePath *pPath = (WTNodePath*)m_pOriginalBsp->m_NodePath;
		pNode = pTree->FindNode(pPath);
		if (pNode)
		{
			pNode->AddObjectToList(&m_Links[0], NOA_Objects);
		}
		else
		{
			dsi_ConsolePrint("ERROR!!  Terrain section '%s' has invalid m_NodePath.",
				m_pOriginalBsp ? m_pOriginalBsp->m_WorldName : "---NONAME---");
		}

		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00467300
LTBOOL WorldModelInstance::IsMoveable()
{
	return m_pWorldBsp != LTNULL;
}

// FUNCTION: LITHTECH 0x00467310
LTBOOL WorldModelInstance::IsMainWorldModel()
{
	return m_pOriginalBsp->GetWorldInfoFlags() & WIF_MAINWORLD;
}

// FUNCTION: LITHTECH 0x00467320
void WorldModelInstance::InitWorldData(WorldBsp *pOriginalBsp, WorldBsp *pWorldBsp)
{
	m_pOriginalBsp = pOriginalBsp;
	m_pWorldBsp = pWorldBsp;

	if (pWorldBsp)
	{
		m_pValidBsp = pWorldBsp;
	}
	else
	{
		m_pValidBsp = pOriginalBsp;
	}
}

// FUNCTION: LITHTECH 0x00467350
HPOLY WorldModelInstance::MakeHPoly(Node *pNode)
{
	HPOLY hPoly;

	if (m_pWorldBsp)
	{
		hPoly = m_pWorldBsp->MakeHPoly(pNode);
		if (hPoly != INVALID_HPOLY)
			return hPoly;
	}

	return m_pOriginalBsp->MakeHPoly(pNode);
}

// FUNCTION: LITHTECH 0x00467380
LTBOOL WorldModelInstance::IsUntransformed()
{
	if (m_pOriginalBsp)
	{
		return m_pOriginalBsp->IsUntransformed() == 1;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x004673a0
LTBOOL WorldModelInstance::WMSlot14()
{
	return !m_pOriginalBsp->IsUntransformed();
}


// FUNCTION: LITHTECH 0x004673b0
LTBOOL WorldModelInstance::IsPointInside(const LTVector *pPos)
{
	LTVector vTransformed;

	if (!m_pValidBsp)
		return LTFALSE;

	m_BackTransform.Apply4x4(*pPos, vTransformed);
	return m_pValidBsp->VSlot4(vTransformed);
}


// ------------------------------------------------------------------------- //
// ModelInstance.
// ------------------------------------------------------------------------- //

// In-class inline virtuals in the original (Jupiter de_objects.h has GetRadius in the class body): defined
// inline before the constructor, so the constructor's vtable emits them before ??_GModelInstance, as in the exe.
// The same holds for the slot-0x38 virtuals of SpriteInstance, DynamicLight, LTPolyGrid and Canvas::GetRadius.
// FUNCTION: LITHTECH 0x00467650
inline float ModelInstance::GetRadius()
{
	return GetModelDB()->m_VisRadius;
}

// FUNCTION: LITHTECH 0x00467660
inline float ModelInstance::GetScaledRadius()
{
	return LTMAX(m_Scale.x, LTMAX(m_Scale.y, m_Scale.z)) * GetModelDB()->m_VisRadius;
}

// FUNCTION: LITHTECH 0x004674a0
ModelInstance::ModelInstance() : LTObject(OT_MODEL)
{
	uint32 i;

	m_Unknown1B4 = 0;
	m_Unknown1B8 = 0;
	m_pSkins[0] = LTNULL;
	m_HookFn = LTNULL;
	for (i=0; i < MAX_MODEL_TEXTURES; i++)
		m_pSkins[i] = LTNULL;

	trk_Init(&m_AnimTracker, LTNULL, 0);
	m_AnimTrackers = &m_AnimTracker;
	m_AnimTracker.m_Link.m_pNext = LTNULL;
	m_AnimTracker.SetModelInstance(this);

	m_NodeControlFn = LTNULL;
	m_pNodeControlUserData = LTNULL;
	m_HiddenPieces = 0;

	for (i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		m_pSprites[i] = LTNULL;
		((uint32*)m_SpriteTrackers[i])[0] = 0;
		((uint32*)m_SpriteTrackers[i])[1] = 0;
		((uint32*)m_SpriteTrackers[i])[2] = 0;
		((uint32*)m_SpriteTrackers[i])[3] = 0;
		((uint32*)m_SpriteTrackers[i])[4] = 0;
	}

	m_Unknown2B0 = LTVector(0.0f, 0.0f, 0.0f);
	m_Unknown2BC = -1.0f;
	m_ModelLighting = LTVector(-1.0f, -1.0f, -1.0f);
	m_Unknown2CC = 1;
}

// FUNCTION: LITHTECH 0x004676f0 ??_GModelInstance@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00467710
ModelInstance::~ModelInstance()
{
}

// FUNCTION: LITHTECH 0x00467790
void ModelInstance::SetupTransformMaker(TransformMaker *pStates)
{
	LTAnimTracker *pTracker;

	pStates->m_nAnims = 0;
	pTracker = m_AnimTrackers;
	while (pTracker)
	{
		if (pStates->m_nAnims >= MAX_GVP_ANIMS)
			return;

		pTracker->SetupTimeRef(&pStates->m_Anims[pStates->m_nAnims]);
		pStates->m_nAnims++;
		pTracker = pTracker->GetNext();
	}

	pStates->m_NodeControlFn = m_NodeControlFn;
	pStates->m_pNodeControlUserData = m_pNodeControlUserData;
}

// FUNCTION: LITHTECH 0x00467800
void ModelInstance::SetupTransformMakerAnims(TransformMaker *pStates)
{
	LTAnimTracker *pTracker;

	pStates->m_nAnims = 0;
	pTracker = m_AnimTrackers;
	while (pTracker && pStates->m_nAnims < MAX_GVP_ANIMS)
	{
		pTracker->SetupTimeRef(&pStates->m_Anims[pStates->m_nAnims]);
		pStates->m_nAnims++;
		pTracker = pTracker->GetNext();
	}
}

// FUNCTION: LITHTECH 0x00467850
LTBOOL ModelInstance::FindTracker(LTAnimTracker *pTracker, LTAnimTracker **&ppPrev)
{
	LTAnimTracker **ppCur;

	ppCur = &m_AnimTrackers;
	if (m_AnimTrackers)
	{
		ppPrev = ppCur;
		while (*ppCur)
		{
			if (*ppCur == pTracker)
				return LTTRUE;

			ppCur = (LTAnimTracker**)&(*ppCur)->m_Link.m_pNext;
			ppPrev = ppCur;
		}
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00467890
uint32 ModelInstance::NumAnimTrackers()
{
	LTAnimTracker *pTracker;
	uint32 nTrackers;

	nTrackers = 0;
	for (pTracker=m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
		nTrackers++;

	return nTrackers;
}

// FUNCTION: LITHTECH 0x004678b0
LTMatrix* ModelInstance::GetTransforms()
{
	if (!IsTransformCacheValid())
		UpdateTransforms();

	return m_Transforms.GetArray();
}

// FUNCTION: LITHTECH 0x004678d0
LTMatrix* ModelInstance::GetNodeTransform(uint32 iNode)
{
	static LTMatrix mIdentity;	// 0x004e45c8 (original name unknown; "identity" sorts it before g_ObjectMgrs in .bss)

	if (!IsTransformCacheValid())
		UpdateTransforms();

	if (iNode < m_Transforms.GetSize())
		return &m_Transforms[iNode];

	mIdentity.Identity();
	return &mIdentity;
}

// FUNCTION: LITHTECH 0x00467b30
static LTBOOL CompareFrameLocators(FrameLocator *pA, FrameLocator *pB)
{
	if (pA->m_iAnim == pB->m_iAnim && pA->m_iFrame == pB->m_iFrame &&
		pA->m_Time == pB->m_Time && pA->m_iWeightSet == pB->m_iWeightSet)
	{
		return LTFALSE;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x004679b0
LTBOOL ModelInstance::IsTransformCacheValid()
{
	LTAnimTracker *pTracker;
	uint32 i;
	uint8 bDifferent;

	if (m_Unknown2CC)
	{
		m_Unknown2CC = 0;
		return LTFALSE;
	}

	if (m_Transforms.GetSize() != GetModelDB()->NumNodes())
		return LTFALSE;

	if (m_TrackerStates.GetSize() != NumAnimTrackers())
		return LTFALSE;

	i = 0;
	for (pTracker=m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
	{
		bDifferent = CompareFrameLocators(&pTracker->m_TimeRef.m_Cur, &m_TrackerStates[i]);
		if (bDifferent)
			return LTFALSE;
		i++;
	}

	if (m_NodeControlFn)
	{
		LTMatrix mIdent, mTest;

		mIdent.Identity();
		mTest = mIdent;
		m_NodeControlFn((HOBJECT)this, (HMODELNODE)-1, &mTest, m_pNodeControlUserData);
		int result = memcmp(&mIdent, &mTest, sizeof(LTMatrix));
		if (result)
			return LTFALSE;
	}

	return LTTRUE;
}

// Rebuilds the cached node transforms from the trackers.
// FUNCTION: LITHTECH 0x00467b70
void ModelInstance::UpdateTransforms()
{
	LTMatrix mToWorld, mTemp, *pMat;
	LTAnimTracker *pTracker;
	uint32 i;

	if (m_Transforms.GetSize() != GetModelDB()->NumNodes())
		m_Transforms.SetSize(GetModelDB()->NumNodes());

	if (m_TrackerStates.GetSize() != NumAnimTrackers())
		m_TrackerStates.SetSize(NumAnimTrackers());

	// Remember what the cache was built from.
	i = 0;
	for (pTracker=m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
	{
		m_TrackerStates[i] = pTracker->m_TimeRef.m_Cur;
		i++;
	}

	pMat = &mToWorld;
	if (m_NodeControlFn)
		SetupTransform(*pMat);
	else
		pMat->Identity();

	TransformMaker maker;
	maker.m_pStartMat = &mToWorld;
	maker.m_pOutput = m_Transforms.GetArray();
	maker.m_hObject = (HOBJECT)this;
	SetupTransformMaker(&maker);

	if (maker.SetupTransforms() && m_NodeControlFn)
	{
		// The node control callback works in object space: take the transforms out of the world.
		if (mToWorld.Inverse())
		{
			for (i=0; i < m_Transforms.GetSize(); i++)
			{
				mTemp = mToWorld * m_Transforms[i];
				m_Transforms[i] = mTemp;
			}
		}
	}
}

// FUNCTION: LITHTECH 0x00467e90
const char* ModelInstance::GetModelFilename()
{
	if (GetModelDB())
		return GetModelDB()->GetFilename();

	return "";
}


// ------------------------------------------------------------------------- //
// Simple objects.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00467f20
inline float SpriteInstance::CalcRadius()
{
	return LTMAX(m_Scale.x, m_Scale.y) * 363.0f;
}

// FUNCTION: LITHTECH 0x00467eb0
SpriteInstance::SpriteInstance() : LTObject(OT_SPRITE)
{
	// Sprites default to RGB 255.
	m_ColorR = m_ColorG = m_ColorB = 255;

	((uint32*)m_SpriteTracker)[0] = 0;
	((uint32*)m_SpriteTracker)[1] = 0;
	((uint32*)m_SpriteTracker)[2] = 0;
	((uint32*)m_SpriteTracker)[3] = 0;
	((uint32*)m_SpriteTracker)[4] = 0;
	m_ClipperPoly = INVALID_HPOLY;
	m_SCImpl.m_pSprite = this;
}

// FUNCTION: LITHTECH 0x00467f50 ??_GSpriteInstance@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00467f70
SpriteInstance::~SpriteInstance()
{
}

// FUNCTION: LITHTECH 0x00467fa0
inline float DynamicLight::GetLightRadius(uint32 unused)
{
	return m_LightRadius;
}

// FUNCTION: LITHTECH 0x00467f80
DynamicLight::DynamicLight() : LTObject(OT_LIGHT)
{
	m_LightRadius = 100.0f;
}

// FUNCTION: LITHTECH 0x00467fb0 ??_GDynamicLight@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00467fd0
DynamicLight::~DynamicLight()
{
}

// FUNCTION: LITHTECH 0x00467fe0
CameraInstance::CameraInstance() : LTObject(OT_CAMERA)
{
	m_Left = 0;
	m_Top = 0;
	m_Right = 0;
	m_Bottom = 0;
	m_bFullScreen = LTTRUE;
	m_xFov = m_yFov = MATH_HALFPI;
	m_LightAdd.Init();
}

// FUNCTION: LITHTECH 0x00468040 ??_GCameraInstance@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00468060
CameraInstance::~CameraInstance()
{
}

// FUNCTION: LITHTECH 0x00468070
LTParticleSystem::LTParticleSystem() : LTObject(OT_PARTICLESYSTEM)
{
	m_ParticleHead.m_pNext = m_ParticleHead.m_pPrev = &m_ParticleHead;
	m_SoftwareR = m_SoftwareG = m_SoftwareB = 255;
	m_pParticleBank = LTNULL;
	m_ColorR = m_ColorG = m_ColorB = 255;
	m_pCurTexture = LTNULL;
	m_Padding = 0;
	m_pSprite = LTNULL;
	((uint32*)m_SpriteTracker)[0] = 0;
	((uint32*)m_SpriteTracker)[1] = 0;
	((uint32*)m_SpriteTracker)[2] = 0;
	((uint32*)m_SpriteTracker)[3] = 0;
	((uint32*)m_SpriteTracker)[4] = 0;
	m_SystemCenter.x = m_SystemCenter.y = m_SystemCenter.z = 0.0f;
	m_OldCenter.x = m_OldCenter.y = m_OldCenter.z = 0.0f;
	m_nParticles = 0;
	m_nChangedParticles = 0;
	m_MinPos.x = m_MinPos.y = m_MinPos.z = 0.0f;
	m_MaxPos.x = m_MaxPos.y = m_MaxPos.z = 0.0f;
	m_OldRadius = m_SystemRadius = 1.0f;
	m_GravityAccel = -500.0f;
	m_ParticleRadius = 300.0f;
	m_Unknown258 = m_Unknown25C = -1;
	m_Unknown260 = 0;
}

// FUNCTION: LITHTECH 0x00468190 ??_GLTParticleSystem@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x004681b0
LTParticleSystem::~LTParticleSystem()
{
	PSParticle *pCur, *pNext;

	m_MinPos.Init(1e37f, 1e37f, 1e37f);
	m_MaxPos.Init(-1e37f, -1e37f, -1e37f);
	m_pCurTexture = LTNULL;

	// Move all the particles to the free list.
	if (m_pObjectMgr)
	{
		pCur = m_ParticleHead.m_pNext;
		while (pCur != &m_ParticleHead)
		{
			pNext = pCur->m_pNext;
			sb_Free(&m_pObjectMgr->m_ParticleBank, pCur);
			pCur = pNext;
		}
	}

	m_nParticles = 0;
	m_ParticleHead.m_pNext = m_ParticleHead.m_pPrev = &m_ParticleHead;
}

// FUNCTION: LITHTECH 0x00468250
void LTParticleSystem::Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct)
{
	LTObject::Init(pMgr, pStruct);
	m_pParticleBank = &pMgr->m_ParticleBank;
}

// FUNCTION: LITHTECH 0x00468310
inline float LTPolyGrid::CalcRadius()
{
	LTVector vDims;

	vDims.x = (float)(m_Width >> 1) * m_Scale.x;
	vDims.y = m_Scale.y * 128.0f;
	vDims.z = (float)(m_Height >> 1) * m_Scale.z;
	return vDims.Mag() + 1.0f;
}

// FUNCTION: LITHTECH 0x00468270
LTPolyGrid::LTPolyGrid() : LTObject(OT_POLYGRID)
{
	m_Data = LTNULL;
	m_Indices = LTNULL;
	m_xScale = m_yScale = 1.0f;
	m_Unknown1B8[0] = 0;
	m_Unknown1B8[1] = 0;
	m_Unknown1B8[2] = 0;
	m_Unknown1B8[3] = 0;
	m_Unknown1B8[4] = 0;
	m_Unknown1B8[5] = 0;
	m_Unknown1B8[6] = 0;
	m_xPan = m_yPan = 0.0f;
	m_nTris = 0;
	m_nIndices = 0;
	dl_TieOff(&m_LeafLinks);
	m_Width = m_Height = 0;
	memset(m_ColorTable, 0, sizeof(m_ColorTable));
}

// FUNCTION: LITHTECH 0x00468380 ??_GLTPolyGrid@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x004683a0
LTPolyGrid::~LTPolyGrid()
{
	if (m_Data)
	{
		dfree(m_Data);
		m_Data = LTNULL;
	}

	if (m_Indices)
	{
		dfree(m_Indices);
		m_Indices = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x004683f0
LineSystem::LineSystem() : LTObject(OT_LINESYSTEM)
{
	m_pLineBank = LTNULL;
	m_bChanged = LTFALSE;
	m_LineHead.m_pNext = m_LineHead.m_pPrev = &m_LineHead;
	m_SystemCenter.Init();
	m_SystemRadius = 0.0f;
	m_MinPos.Init();
	m_MaxPos.Init();
}

// FUNCTION: LITHTECH 0x00468460 ??_GLineSystem@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00468480
LineSystem::~LineSystem()
{
	LSLine *pCur, *pNext;

	pCur = m_LineHead.m_pNext;
	while (pCur != &m_LineHead)
	{
		pNext = pCur->m_pNext;
		sb_Free(m_pLineBank, pCur);
		pCur = pNext;
	}

	m_LineHead.m_pNext = m_LineHead.m_pPrev = &m_LineHead;
	m_pLineBank = LTNULL;
}

// FUNCTION: LITHTECH 0x004684e0
void LineSystem::Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct)
{
	LTObject::Init(pMgr, pStruct);
	m_pLineBank = &pMgr->m_LineBank;
}

// FUNCTION: LITHTECH 0x00468500
ContainerInstance::ContainerInstance() : WorldModelInstance(OT_CONTAINER)
{
	m_ContainerCode = 0xFFFF;
}

// FUNCTION: LITHTECH 0x00468520 ??_GContainerInstance@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00468540
ContainerInstance::~ContainerInstance()
{
}

// FUNCTION: LITHTECH 0x00468550
void ContainerInstance::Init(ObjectMgr *pMgr, ObjectCreateStruct *pStruct)
{
	WorldModelInstance::Init(pMgr, pStruct);
	m_ContainerCode = pStruct->m_ContainerCode;
}

// FUNCTION: LITHTECH 0x004685a0
inline float Canvas::GetRadius()
{
	return m_CanvasRadius;
}

// Identical to GetRadius: the linker folded the two (vtable slots 0x2c and 0x38 both point at 0x004685a0).
inline float Canvas::CalcRadius()
{
	return m_CanvasRadius;
}

// FUNCTION: LITHTECH 0x00468570
Canvas::Canvas() : LTObject(OT_CANVAS)
{
	m_Fn = LTNULL;
	m_pFnUserData = LTNULL;
	m_CanvasRadius = 1.0f;
}

// ??_GLTObject is byte-identical (Canvas has no destructor of its own) and the linker kept this later copy;
// both vtables point here.
// FUNCTION: LITHTECH 0x004685b0 ??_GCanvas@@UAEPAXI@Z


// ------------------------------------------------------------------------- //
// Interface functions.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004685d0
LTRESULT om_Init(ObjectMgr *pMgr, LTBOOL bClient)
{
	int i;

	memset(pMgr->m_ObjectBankPointers, 0, sizeof(pMgr->m_ObjectBankPointers));
	pMgr->m_ObjectBankPointers[OT_NORMAL] = &pMgr->m_ObjectBankNormal;
	pMgr->m_ObjectBankPointers[OT_MODEL] = &pMgr->m_ObjectBankModel;
	pMgr->m_ObjectBankPointers[OT_WORLDMODEL] = &pMgr->m_ObjectBankWorldModel;
	pMgr->m_ObjectBankPointers[OT_SPRITE] = &pMgr->m_ObjectBankSprite;
	pMgr->m_ObjectBankPointers[OT_LIGHT] = &pMgr->m_ObjectBankLight;
	pMgr->m_ObjectBankPointers[OT_CAMERA] = &pMgr->m_ObjectBankCamera;
	pMgr->m_ObjectBankPointers[OT_PARTICLESYSTEM] = &pMgr->m_ObjectBankParticleSystem;
	pMgr->m_ObjectBankPointers[OT_POLYGRID] = &pMgr->m_ObjectBankPolyGrid;
	pMgr->m_ObjectBankPointers[OT_LINESYSTEM] = &pMgr->m_ObjectBankLineSystem;
	pMgr->m_ObjectBankPointers[OT_CONTAINER] = &pMgr->m_ObjectBankContainer;
	pMgr->m_ObjectBankPointers[OT_CANVAS] = &pMgr->m_ObjectBankCanvas;

	if (bClient)
	{
		sb_Init2(&pMgr->m_ParticleBank, sizeof(PSParticle), 1024, 1024);
		sb_Init2(&pMgr->m_LineBank, sizeof(LSLine), 64, 64);
	}
	else
	{
		sb_Init(&pMgr->m_ParticleBank, sizeof(PSParticle), 64);
		sb_Init(&pMgr->m_LineBank, sizeof(LSLine), 64);
	}

	sb_Init2(&pMgr->m_AttachmentBank, sizeof(Attachment), 16, 32);

	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		dl_InitList(&pMgr->m_ObjectLists[i]);
	}

	pMgr->m_InternalLink.m_pData = pMgr;
	dl_Insert(&g_ObjectMgrs, &pMgr->m_InternalLink);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00468710
LTRESULT om_Term(ObjectMgr *pMgr)
{
	int i;
	LTLink *pCur;

	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		pMgr->m_ObjectBankPointers[i]->Term();
	}

	sb_Term(&pMgr->m_ParticleBank);
	sb_Term(&pMgr->m_LineBank);
	sb_Term(&pMgr->m_AttachmentBank);

	// Get it out of the global list if it's in there.
	for (pCur=g_ObjectMgrs.m_pNext; pCur != &g_ObjectMgrs; pCur=pCur->m_pNext)
	{
		if (pCur->m_pData == pMgr)
		{
			dl_Remove(pCur);
			break;
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00468790
LTRESULT om_CreateObject(ObjectMgr *pMgr, ObjectCreateStruct *pStruct, LTObject **ppObject)
{
	LTObject *pObject;
	unsigned short objectType;

	*ppObject = LTNULL;

	objectType = pStruct->m_ObjectType;
	if (objectType >= NUM_OBJECTTYPES)
	{
		RETURN_ERROR_PARAM(1, om_CreateObject, LT_INVALIDPARAMS, pStruct->m_Filename);
	}

	// Do the 'extra' initialization for the object type.
	pObject = (LTObject*)pMgr->m_ObjectBankPointers[objectType]->AllocVoid();
	if (!pObject)
	{
		RETURN_ERROR_PARAM(1, om_CreateObject, LT_OUTOFMEMORY, pStruct->m_Filename);
	}

	pObject->Init(pMgr, pStruct);

	// Add to the appropriate list.
	dl_AddHead(&pMgr->m_ObjectLists[objectType], &pObject->m_Link, pObject);

	*ppObject = pObject;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00468880
LTRESULT om_DestroyObject(ObjectMgr *pMgr, LTObject *pObject)
{
	dl_RemoveAt(&pMgr->m_ObjectLists[(char)pObject->m_ObjectType], &pObject->m_Link);
	if ((char)pObject->m_ObjectType < NUM_OBJECTTYPES)
	{
		pMgr->m_ObjectBankPointers[(char)pObject->m_ObjectType]->FreeVoid(pObject);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004688e0
LTRESULT om_CreateAttachment(ObjectMgr *pMgr, LTObject *pParent, uint16 nChildID, int iSocket,
	LTVector *pOffset, LTRotation *pRotationOffset, Attachment **ppAttachment)
{
	Attachment *pAttachment;

	// Setup the attachment.
	pAttachment = (Attachment*)sb_Allocate(&pMgr->m_AttachmentBank);
	if (!pAttachment)
	{
		return LT_ERROR;
	}

	pAttachment->m_pNext = pParent->m_Attachments;
	pParent->m_Attachments = pAttachment;
	pAttachment->m_nParentID = pParent->m_ObjectID;
	pAttachment->m_nChildID = nChildID;
	pAttachment->m_iSocket = iSocket;

	if (pOffset)
	{
		pAttachment->m_Offset.m_Pos = *pOffset;
	}

	if (pRotationOffset)
	{
		pAttachment->m_Offset.m_Rot = *pRotationOffset;
	}
	else
	{
		pAttachment->m_Offset.m_Rot.Init();
	}

	if (ppAttachment)
	{
		*ppAttachment = pAttachment;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004689b0
LTRESULT om_RemoveAttachment(ObjectMgr *pMgr, LTObject *pParent, Attachment *pAttachment)
{
	Attachment **ppPrev, *pCur;

	// Find it and remove it.
	ppPrev = &pParent->m_Attachments;
	pCur = pParent->m_Attachments;
	while (pCur)
	{
		if (pCur == pAttachment)
		{
			*ppPrev = pCur->m_pNext;
			sb_Free(&pMgr->m_AttachmentBank, pAttachment);
			return LT_OK;
		}

		ppPrev = &pCur->m_pNext;
		pCur = pCur->m_pNext;
	}

	RETURN_ERROR(1, ILTServer::RemoveAttachment, LT_ERROR);
}


// FUNCTION: LITHTECH 0x00468a30
void om_ClearSerializeIDs(ObjectMgr *pMgr)
{
	int i;
	LTLink *pListHead, *pCur;

	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		pListHead = &pMgr->m_ObjectLists[i].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			((LTObject*)pCur->m_pData)->m_SerializeID = 0xFFFF;
		}
	}
}


// ------------------------------------------------------------------------- //
// Template instances this file kept (CMoArray<FrameLocator> of ModelInstance).
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00468a70 ?GenGetNext@?$CMoArray@VFrameLocator@@VNoCache@@@@UBE?AVFrameLocator@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00468aa0 ?GenGetAt@?$CMoArray@VFrameLocator@@VNoCache@@@@UBE?AVFrameLocator@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00468ad0 ?GenAppend@?$CMoArray@VFrameLocator@@VNoCache@@@@UAEHAAVFrameLocator@@@Z
// FUNCTION: LITHTECH 0x00468bd0 ?GenRemoveAt@?$CMoArray@VFrameLocator@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00468d00 ?GenCopyList@?$CMoArray@VFrameLocator@@VNoCache@@@@UAEHABV?$GenList@VFrameLocator@@@@@Z
// FUNCTION: LITHTECH 0x00468e20 ?GenAppendList@?$CMoArray@VFrameLocator@@VNoCache@@@@UAEHABV?$GenList@VFrameLocator@@@@@Z
// FUNCTION: LITHTECH 0x00468f20 ?SetSize2@?$CMoArray@VFrameLocator@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00468f80 ?InternalNiceSetSize@?$CMoArray@VFrameLocator@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00469060 ?_InitArray@?$CMoArray@VFrameLocator@@VNoCache@@@@AAEXK@Z
