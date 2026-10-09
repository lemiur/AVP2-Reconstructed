// d3d.ren sys/d3d/setupmodel (0x1000b349-0x1000f160): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// ModelDraw global + 14 ConVars + SetupModelLight etc. /Ob2 produces the original static-initialiser wrappers;
// the empty light-record constructor below remains out of line, as called by ModelDraw's 16-element constructor loop.
// FLAGS: /O1 /Ob2
#include "d3dren/d3dstate.h"
#include "d3dren/polydraw.h"
#include "d3dren/setupmodel.h"
#include "d3dren/staticlight.h"
#include "d3dren/3d_ops.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "animtracker.h"
#include <math.h>

// ---- data and functions of other units that the code below uses ----
void d3d_InitModelVertexBufferPools();	// unit unk/10001000: sets up the model vertex buffer pools from the console variables
void d3d_TermModelVertexBufferPools();	// unit unk/10001000: releases them
LTBOOL i_IntersectSegment(IntersectQuery *pQuery, IntersectInfo *pInfo, WorldTree *pWorldTree, LTBOOL bServer);	// 0x10030bdd
void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGB *pRGB);	// 0x1000c860

// Warble data of the model vertex projection (set up by d3d_BuildModelWarbleTables / d3d_ModelModuleInit, used by d3d_BeginModelWarbleProjection / d3d_ProjectWarbledModelVertex).
// GLOBAL: D3DREN 0x10053730
float g_ModelWarbleScales[32];		// guess: warble table (d3d_BuildModelWarbleTables)
// GLOBAL: D3DREN 0x10052900
float g_ModelWarbleDeltas[32];		// guess: the differences of consecutive warble table entries
// GLOBAL: D3DREN 0x10054870
float g_fModelWarbleFraction;			// guess: warble fraction (set per frame, 0 after the module init)
// GLOBAL: D3DREN 0x10054888
uint16 g_ModelVBCacheLastAgeFrameCode;		// guess: the frame code (g_CurFrameCode) at which the vertex buffer cache was last aged
// GLOBAL: D3DREN 0x10053278
float g_fModelWarblePhase;			// guess: warble phase (advanced per frame by another unit)
// GLOBAL: D3DREN 0x1005328c
int g_nModelWarbleIndex;			// guess: warble table index (advanced per vertex)

// The model lighting code and the light callback.
// Callback data of ModelDraw::StaticLightCB (Jupiter's SStaticLightCallbackData analogue): the model drawer and the
// matrix that takes the light positions into the model's space.
struct UnkType_StaticLightCBData
{
	ModelDraw	*m_Unk00;
	LTMatrix	m_Unk04;
};

// The renderer's view of MainWorld: the engine's MainWorld has no such member, the renderer calls one (thiscall, one
// stack argument).
struct UnkType_MainWorldView : public MainWorld
{
	WorldPoly *w_GetPolyFromHPoly(HPOLY hPoly);
};

// The light members of RenderStruct (g_pStruct): include/renderstruct.h has m_GlobalLightDir at 0x138 and pads the colour at 0x144.
struct UnkType_RenderStructLightView
{
	uint8		m_Pad000[0x138];
	LTVector	m_GlobalLightDir;		// 0x138
	LTVector	m_GlobalLightColor;		// 0x144 NAME: Jupiter RenderStruct::m_GlobalLightColor (NAMING.md)
};
#define RENDERSTRUCT_LIGHTS		((UnkType_RenderStructLightView *)g_pStruct)

// guess: the 0x2a8-byte draw state record that DrawModel declares and never uses again (its constructor call survives):
// 8 stage records, then the cache members.  The constructor is out of line (it contains a loop).
struct UnkType_DrawState
{
	UnkType_DrawState();	// 0x1000dcc7

	UnkType_StageRecord	m_Unk000[8];	// 0x000
	int					m_Unk120;		// 0x120
	uint32				m_Unk124;		// 0x124
	uint32				m_Unk128;		// 0x128
	uint32				m_Unk12c;		// 0x12c
	uint32				m_Unk130;		// 0x130
	uint32				m_Unk134;		// 0x134
	uint8				m_Pad138[0x2a8 - 0x138];
};

// ---- ModelDraw (Talon setupmodel.cpp) ------------------------------------------------------------------------------

// The model drawer; its static initialiser constructs it and registers the destructor with atexit.
// The exe's static initialiser is ONE 22-byte function (mov ecx,&g; call ctor; push stub; call _atexit; pop ecx; ret) and a 10-byte
// destructor stub: the compiler-generated `_$E` helpers merged into the wrapper, which this compiler does only with /Ob2 (the normal
// shape of a P object, objects_v2.csv section 4), so this object is built /O1 /Ob2.  ModelDraw::ModelDraw's out-of-line copy is
// 226 bytes, including sixteen calls to the empty UnkType_ModelLight constructor. Keep that constructor out of automatic
// inlining below; otherwise VC6 deletes the loop and emits only 207 bytes.
// FUNCTION: D3DREN 0x1000b349 _$E4
// FUNCTION: D3DREN 0x1000b35f _$E2
// GLOBAL: D3DREN 0x10052980
ModelDraw g_ModelDraw;

// Two list heads that only the module init (0x1000b772) and the per-frame function (0x1000b6ae) touch: LTLink globals (their
// empty constructors are the empty static initialisers; the linker folded them into one copy, 0x1000b369).
// FUNCTION: D3DREN 0x1000b369 _$E10
// GLOBAL: D3DREN 0x10054878
LTLink g_ModelFrameListHeadA;
// GLOBAL: D3DREN 0x10053280
LTLink g_ModelFrameListHeadB;

// ---- the console variables of the model drawer ----
// FUNCTION: D3DREN 0x1000b36a _$E13
// FUNCTION: D3DREN 0x1000b36f _$E12
// GLOBAL: D3DREN 0x10053290
ConVar g_CV_LightModelSprites("LightModelSprites", 0.0f);
// FUNCTION: D3DREN 0x1000b389 _$E16
// FUNCTION: D3DREN 0x1000b38e _$E15
// GLOBAL: D3DREN 0x100536d0
ConVar g_CV_ModelLODOffset("ModelLODOffset", 0.0f);
// FUNCTION: D3DREN 0x1000b3a8 _$E19
// FUNCTION: D3DREN 0x1000b3ad _$E18
// GLOBAL: D3DREN 0x10053830
ConVar g_CV_ModelZoomScale("ModelZoomScale", 1.0f);
// FUNCTION: D3DREN 0x1000b3c7 _$E22
// FUNCTION: D3DREN 0x1000b3cc _$E21
// GLOBAL: D3DREN 0x10053810
ConVar g_CV_ModelLODBlendEnable("ModelLODBlendEnable", 1.0f);
// FUNCTION: D3DREN 0x1000b3e6 _$E25
// FUNCTION: D3DREN 0x1000b3eb _$E24
// GLOBAL: D3DREN 0x100537f0
ConVar g_CV_ModelLODBlendDist("ModelLODBlendDist", 60.0f);
// FUNCTION: D3DREN 0x1000b409 _$E28
// FUNCTION: D3DREN 0x1000b40e _$E27
// GLOBAL: D3DREN 0x10053710
ConVar g_CV_ExtraFOVXOffset("ExtraFOVXOffset", 0.0f);
// FUNCTION: D3DREN 0x1000b428 _$E31
// FUNCTION: D3DREN 0x1000b42d _$E30
// GLOBAL: D3DREN 0x10053238
ConVar g_CV_ExtraFOVYOffset("ExtraFOVYOffset", 0.0f);
// FUNCTION: D3DREN 0x1000b447 _$E34
// FUNCTION: D3DREN 0x1000b44c _$E33
// GLOBAL: D3DREN 0x100537b0
ConVar g_CV_ModelFovTest("ModelFovTest", 0.0f);
// FUNCTION: D3DREN 0x1000b466 _$E37
// FUNCTION: D3DREN 0x1000b46b _$E36
// GLOBAL: D3DREN 0x10054850
ConVar g_CV_ModelSunVariance("ModelSunVariance", 0.05f);
// FUNCTION: D3DREN 0x1000b489 _$E40
// FUNCTION: D3DREN 0x1000b48e _$E39
// GLOBAL: D3DREN 0x100537d0
ConVar g_CV_ModelApplySun("ModelApplySun", 1.0f);
// FUNCTION: D3DREN 0x1000b4a8 _$E43
// FUNCTION: D3DREN 0x1000b4ad _$E42
// GLOBAL: D3DREN 0x10053258
ConVar g_CV_ModelSaturation("ModelSaturation", 2.0f);
// FUNCTION: D3DREN 0x1000b4cb _$E46
// FUNCTION: D3DREN 0x1000b4d0 _$E45
// GLOBAL: D3DREN 0x100536f0
ConVar g_CV_DrawModelsRigid("DrawModelsRigid", 1.0f);
// FUNCTION: D3DREN 0x1000b4ea _$E49
// FUNCTION: D3DREN 0x1000b4ef _$E48
// GLOBAL: D3DREN 0x100532b0
ConVar g_CV_ModelCacheRigid("ModelCacheRigid", 0.0f);
// FUNCTION: D3DREN 0x1000b509 _$E52
// FUNCTION: D3DREN 0x1000b50e _$E51
// GLOBAL: D3DREN 0x100528e0
ConVar g_CV_ModelUseTnL("ModelUseTnL", 0.0f);

// vbcache.h declares a static class-type data member (it takes a `_$E` number): include it after this unit's own static initialisers.
#include "d3dren/vbcache.h"

// ---- d3d_QueueModel and the frustum test ----

// guess: the per-model draw callback of BaseObjectSet::Draw (Jupiter: d3d_QueueModel(const ViewParams&, LTObject*)).
// FUNCTION: D3DREN 0x1000b560 ?IsValid@LTAnimTracker@@QAEIXZ
// FUNCTION: D3DREN 0x1000b528
void d3d_QueueModel(ViewParams *pParams, LTObject *pObject)
{
	ModelInstance *pInstance = (ModelInstance *)pObject;
	ModelDraw *pDraw = &g_ModelDraw;

	if (pInstance->m_AnimTracker.IsValid())
	{
		if (d3d_TestModelFrustum(pDraw, pInstance, &g_ClipFlags))
			pDraw->DrawModel(pInstance);
	}
}

// guess: bounding sphere of the instance against the view frustum; *pClipFlags gets the planes the sphere crosses (0x3f = all).
// FUNCTION: D3DREN 0x1000b584
int d3d_TestModelFrustum(ModelDraw *pDraw, ModelInstance *pInstance, uint32 *pClipFlags)
{
	Model *pModel = pInstance->GetModelDB();
	LTPlane *pPlanes;

	pDraw->m_Unk634 = pModel->m_GlobalRadius;
	pDraw->m_Unk634 = pDraw->m_Unk634 * LTMAX(pInstance->m_Scale.x, LTMAX(pInstance->m_Scale.y, pInstance->m_Scale.z));

	pPlanes = g_ViewParams.m_ReallyCloseClipPlanes;
	if (!(pInstance->m_Flags & FLAG_REALLYCLOSE))
		pPlanes = g_ViewParams.m_ClipPlanes;

	return d3d_TestSphereClipPlanes(&pInstance->m_Pos, pDraw->m_Unk634, pPlanes, pClipFlags) != 0;
}

// guess: sphere (position, radius) against 6 planes: returns 0 when it is completely outside one, else 1 with the bit of
// every plane that the sphere does not cross cleared in *pFlags.
// FUNCTION: D3DREN 0x1000b63b
int d3d_TestSphereClipPlanes(LTVector *pPos, float fRadius, LTPlane *pPlanes, uint32 *pFlags)
{
	LTPlane *p = pPlanes;
	int i;

	*pFlags = 0x3f;
	for (i = 0; i < 6; p++, i++)
	{
		float fDist = p->m_Normal.Dot(*pPos) - p->m_Dist;

		if (fDist < -fRadius)
			return 0;
		if (fDist > fRadius)
			*pFlags &= ~(1 << i);
	}
	return 1;
}

// NAME: d3d_ModelPreFrame: Jupiter drawmodel.cpp (the OT_MODEL PreFrameFn of g_ObjectHandlers, names_proposal high).
// FUNCTION: D3DREN 0x1000b6ae
void d3d_ModelPreFrame()
{
	dl_TieOff(&g_ModelFrameListHeadA);
	dl_TieOff(&g_ModelFrameListHeadB);
}

// guess: fills the 32-entry warble tables (a sine scaled by the ModelWarble amount) and their deltas.
// FUNCTION: D3DREN 0x1000b6cd
void d3d_BuildModelWarbleTables()
{
	int i;
	float fScale;
	float fClamped;
	float fAmp;
	float fSin;
	float fSum;

	for (i = 0; i < 32; i++)
	{
		fSin = (float)sin(((float)i * 0.03125f) * 6.2831855f);
		fScale = g_WarbleScale;
		fClamped = LTCLAMP(fScale, 0.0f, 1.0f);
		fAmp = (1.0f - fClamped) * 0.5f;
		fSum = fScale;
		fSum += fAmp;
		fSum += fAmp * fSin;
		g_ModelWarbleScales[i] = fSum;
	}
	for (i = 0; i < 32; i++)
		g_ModelWarbleDeltas[i] = g_ModelWarbleScales[(i + 1) & 0x1f] - g_ModelWarbleScales[i];
}

// guess: OT_MODEL ModuleInit slot of g_ObjectHandlers.
// FUNCTION: D3DREN 0x1000b772
void d3d_ModelModuleInit()
{
	d3d_BuildModelWarbleTables();
	g_fModelWarbleFraction = 0.0f;
	g_fModelWarblePhase = 0.0f;
	dl_TieOff(&g_ModelFrameListHeadA);
	dl_TieOff(&g_ModelFrameListHeadB);
	d3d_InitModelVertexBufferPools();
}

// guess: OT_MODEL ModuleTerm slot (a jmp thunk).
// FUNCTION: D3DREN 0x1000b7aa
void thunk_FUN_10001340()
{
	d3d_TermModelVertexBufferPools();
}

// FUNCTION: D3DREN 0x1000b7af ??0ModelDraw@@QAE@XZ
// (ModelDraw::ModelDraw is defined in the class body in modeldraw.h; this is its out-of-line copy, called by the static initialiser above.)

// guess: constructor of the draw state cache embedded in the model drawer (8 stage records, a matrix).
// FUNCTION: D3DREN 0x1000b8e5 ?Identity@LTMatrix@@QAEXXZ
// FUNCTION: D3DREN 0x1000b94b ?Init@LTMatrix@@QAEXMMMMMMMMMMMMMMMM@Z
// FUNCTION: D3DREN 0x1000b891 ??0UnkType_StateCache@@QAE@XZ
UnkType_StateCache::UnkType_StateCache()
{
	int i;

	for (i = 0; i < 8; i++)
	{
		m_Unk000[i].m_Unk0c = -1;
		m_Unk000[i].m_Unk18 = -1;
		m_Unk000[i].m_Unk20 = 0;
		m_Unk000[i].ResetStageRecord();
	}
	m_Unk124 = 0;
	m_Unk128 = 0;
	m_Unk12c.Identity();
	m_Unk16c = 0;
	m_Unk120 = 1;
}

// FUNCTION: D3DREN 0x1000b9b1
void UnkType_StageRecord::ResetStageRecord()
{
	m_Unk1c = 0.0f;
	m_Unk00 = 0;
	m_Unk04 = 0;
	m_Unk06 = 0;
	m_Unk08 = 0;
	m_Unk10 = 0;
	m_Unk12 = 0;
	m_Unk14 = 0;
}

// Empty constructor of the light record (the exe calls it 16 times from the loop of ModelDraw::ModelDraw).
// VC6 /Ob2 otherwise automatically inlines this non-inline empty function and removes the entire loop.
#pragma auto_inline(off)
// FUNCTION: D3DREN 0x1000b9d1 ??0UnkType_ModelLight@@QAE@XZ
UnkType_ModelLight::UnkType_ModelLight()
{
}
#pragma auto_inline(on)

// FUNCTION: D3DREN 0x1000b9d4 ??1ModelDraw@@QAE@XZ
ModelDraw::~ModelDraw()
{
}

// guess: grows the vertex and the node transform arrays to the size the current model needs.
// FUNCTION: D3DREN 0x1000ba1c
int ModelDraw::EnsureVertexAndTransformBuffers()
{
	if (m_pModel->m_nTotalVerts > Unk828().GetSize())
	{
		if (!Unk828().SetSize2(m_pModel->m_nTotalVerts + 0x20, &g_DefAlloc))
			return 0;
	}
	if (m_pModel->m_Transforms.GetSize() > Unk83c().GetSize())
	{
		if (!Unk83c().SetSize2(m_pModel->m_Transforms.GetSize(), &g_DefAlloc))
			return 0;
	}
	return 1;
}

// NAME: ModelDraw::CallModelHook: Jupiter setupmodel.cpp (that version sets up more members; the Talon one just fills the
// hook data and calls the scene's hook).
// Source shape: the object flags are read into a local before the first store (otherwise m_pInstance is loaded again after the
// stores and the hook pointer is not formed in the `this` register).
// FUNCTION: D3DREN 0x1000ba6e ?CallModelHook@ModelDraw@@QAEXXZ
void ModelDraw::CallModelHook()
{
	uint32 nFlags = m_pInstance->m_Flags;
	m_ModelHookData.m_Flags = MHF_USETEXTURE;
	m_ModelHookData.m_ObjectFlags = nFlags;
	m_ModelHookData.m_hObject = (HLOCALOBJ)m_pInstance;
	if (g_pSceneDesc->m_ModelHookFn)
		g_pSceneDesc->m_ModelHookFn(&m_ModelHookData, g_pSceneDesc->m_ModelHookUser);
}

// ---- the model lighting (light callback, CastRayAtSky, GetDirLightAmount, SetupModelLight, w_GetLightVal) ----

// FUNCTION: D3DREN 0x1000baa4
void ModelDraw::StaticLightCB(WorldTreeObj *pObj, void *pUser)
{
	UnkType_StaticLightCBData *pData = (UnkType_StaticLightCBData *)pUser;
	StaticLight *pStaticLight = (StaticLight *)pObj;

	pData->m_Unk00->AddModelLight(&pData->m_Unk04, &pStaticLight->m_Pos, pStaticLight->m_Radius,
		pStaticLight->m_Color.x, pStaticLight->m_Color.y, pStaticLight->m_Color.z,
		pStaticLight->m_OuterColor.x, pStaticLight->m_OuterColor.y, pStaticLight->m_OuterColor.z,
		&pStaticLight->m_Dir, pStaticLight->m_FOV);
}

// FUNCTION: D3DREN 0x1000e011 ?Mag@?$_CVector@M@@QBEMXZ
// FUNCTION: D3DREN 0x1000bafe
void ModelDraw::AddModelLight(LTMatrix *pMat, LTVector *pLightPos, float fRadius, float r, float g, float b, float r2, float g2, float b2, LTVector *pDir, float fFov)
{
	if ((uint32)m_nModelLights < m_nMaxModelLights)
	{
		LTVector vDiff = m_Unk5d0 - *pLightPos;
		float fDist = vDiff.Mag();

		if (fDist - m_pModel->m_VisRadius < fRadius)
		{
			if (fFov > -1.0f && fFov < 1.0f && fDist != 0.0f)
			{
				float fCos = (1.0f / fDist) * (vDiff.x * pDir->x + vDiff.y * pDir->y + vDiff.z * pDir->z);
				float t, fInv;

				if (fCos < fFov)
					return;

				t = (1.0f - (fCos + 1.0f) * 0.5f) / (1.0f - (fFov + 1.0f) * 0.5f);
				fInv = 1.0f - t;
				r = t * r2 + fInv * r;
				g = t * g2 + fInv * g;
				b = t * b2 + fInv * b;
			}

			UnkType_ModelLight *pLight = &m_Unk3c[m_nModelLights];

			pLight->m_Unk1c.Init(r, g, b);
			pLight->m_Unk1c.x *= g_GlobalVertexTint.x;
			pLight->m_Unk1c.y *= g_GlobalVertexTint.y;
			pLight->m_Unk1c.z *= g_GlobalVertexTint.z;
			pLight->m_Unk1c *= m_ObjectColor;
			MatVMul(&pLight->m_Unk00, pMat, pLightPos);
			pLight->m_Unk10 = pLight->m_Unk00;
			pLight->m_Unk0c = fRadius * fRadius;
			pLight->m_Unk10.Norm();
			pLight->m_Unk10 /= pLight->m_Unk0c;
			m_nModelLights++;
		}
	}
}

// FUNCTION: D3DREN 0x1000bcf3 ?MatVMul@@YAXPAV?$_CVector@M@@PAVLTMatrix@@0@Z

// The out-of-line copies of the SDK inline constructors that CastRayAtSky calls, and the renderer's MainWorld member:
// FUNCTION: D3DREN 0x1000bdeb ??0IntersectQuery@@QAE@XZ
// FUNCTION: D3DREN 0x1000be28 ??0IntersectInfo@@QAE@XZ
// FUNCTION: D3DREN 0x1000be59 ?w_GetPolyFromHPoly@UnkType_MainWorldView@@QAEPAUWorldPoly@@K@Z
// NAME: CastRayAtSky: Jupiter setupmodel.cpp static CastRayAtSky (the Talon one reads g_pFrameMainWorld, the MainWorld, instead of
// the world_bsp_client holder).
// FUNCTION: D3DREN 0x1000bd4f
LTBOOL CastRayAtSky(const LTVector &vFrom, const LTVector &vDir)
{
	IntersectQuery iQuery;
	IntersectInfo iInfo;

	iQuery.m_From = vFrom;
	iQuery.m_To = vFrom + vDir;
	iQuery.m_Flags = INTERSECT_HPOLY;

	if (i_IntersectSegment(&iQuery, &iInfo, &g_pFrameMainWorld->m_WorldTree, LTFALSE))
	{
		WorldPoly *pPoly = ((UnkType_MainWorldView *)g_pFrameMainWorld)->w_GetPolyFromHPoly(iInfo.m_hPoly);
		if (!pPoly)
			return LTFALSE;
		if (((Surface *)pPoly->m_pSurface)->m_Flags & SURF_SKY)
			return LTTRUE;
		else
			return LTFALSE;
	}
	else
	{
		return LTFALSE;
	}
}

WorldPoly *UnkType_MainWorldView::w_GetPolyFromHPoly(HPOLY hPoly)
{
	uint32 iModel = hPoly >> 16;
	WorldData *pWorldData;

	if (iModel >= m_WorldModels.GetSize())
		return LTNULL;

	pWorldData = m_WorldModels[iModel];
	if (!pWorldData)
		return LTNULL;

	return pWorldData->m_pOriginalBsp->GetPolyFromHPoly(hPoly);
}

// FUNCTION: D3DREN 0x1000be88
float ModelDraw::GetDirLightAmount()
{
	WorldTreeNode *pWTRoot = g_pFrameMainWorld->m_WorldTree.GetRootNode();
	float fLongestDist = pWTRoot->m_Radius * -2.0f;
	LTVector vDir = RENDERSTRUCT_LIGHTS->m_GlobalLightDir * fLongestDist;

	if (g_CV_ModelSunVariance.m_FloatVal == 0.0f)
		return CastRayAtSky(m_Unk5d0, vDir) ? 1.0f : 0.0f;

	LTVector vUp, vForward, vRight;
	m_ModelTransform.GetBasisVectors(&vRight, &vUp, &vForward);
	LTVector vLightUp = RENDERSTRUCT_LIGHTS->m_GlobalLightDir.Cross(vRight);

	vLightUp *= m_pInstance->GetScaledRadius();

	LTBOOL bTopInLight = CastRayAtSky(m_Unk5d0 + vLightUp, vDir);
	LTBOOL bBottomInLight = CastRayAtSky(m_Unk5d0 - vLightUp, vDir);

	if (bTopInLight == bBottomInLight)
		return (bTopInLight) ? 1.0f : 0.0f;

	LTBOOL bMiddleInLight;
	float fVariance = 1.0f, fTop = 1.0f, fBottom = -1.0f, fMiddle;
	float fLight = 1.0f;

	while ((fVariance > g_CV_ModelSunVariance.m_FloatVal) && (bBottomInLight != bTopInLight))
	{
		fMiddle = (fTop + fBottom) * 0.5f;
		bMiddleInLight = CastRayAtSky(m_Unk5d0 + (vLightUp * fMiddle), vDir);
		fVariance *= 0.5f;
		if (!bMiddleInLight)
			fLight -= fVariance;
		if (bMiddleInLight == bTopInLight)
		{
			fTop = fMiddle;
			bTopInLight = bMiddleInLight;
		}
		else
		{
			fBottom = fMiddle;
			bBottomInLight = bMiddleInLight;
		}
	}

	return fLight;
}

// FUNCTION: D3DREN 0x1000c0bf ?GetBasisVectors@LTMatrix@@QAEXPAV?$_CVector@M@@00@Z

// FUNCTION: D3DREN 0x1000c73d ?MatVMul_3x3@@YAXPAV?$_CVector@M@@PAVLTMatrix@@0@Z

// FUNCTION: D3DREN 0x1000c790 ?Mat_InverseTransformation@@YAXPAVLTMatrix@@0@Z
// FUNCTION: D3DREN 0x1000c823 ??0FindObjInfo@@QAE@XZ
// NAME: ModelDraw::SetupModelLight: Jupiter setupmodel.cpp (the Talon version takes no arguments: the instance, the hook data
// and the light list are members of the model drawer).
// FUNCTION: D3DREN 0x1000e03c ??G?$_CVector@M@@QBE?AV0@XZ
// FUNCTION: D3DREN 0x1000c100
void ModelDraw::SetupModelLight()
{
	FindObjInfo foInfo;
	LTMatrix mInvTransform;
	UnkType_StaticLightCBData CallbackData;

	m_nModelLights = 0;
	m_nMaxModelLights = g_MaxModelLights;
	m_nMaxModelLights = LTMIN(m_nMaxModelLights, 16);

	if (m_pModel->m_bNormalRef)
	{
		mInvTransform = m_ModelTransform * (m_InvTransform * m_pModel->m_Transforms[m_pModel->m_iNormalRefNode] * m_pModel->m_mNormalRef);
		if (m_pInstance->m_Flags & FLAG_REALLYCLOSE)
			mInvTransform = g_ViewParams.m_mInvView * mInvTransform;
		mInvTransform = ~mInvTransform;
	}
	else
	{
		if (m_pInstance->m_Flags & FLAG_REALLYCLOSE)
		{
			mInvTransform = g_ViewParams.m_mInvView * m_ModelTransform;
			mInvTransform = ~mInvTransform;
		}
		else
			Mat_InverseTransformation(&m_ModelTransform, &mInvTransform);
	}

	m_ObjectColor.x = m_pInstance->m_ColorR;
	m_ObjectColor.y = m_pInstance->m_ColorG;
	m_ObjectColor.z = m_pInstance->m_ColorB;
	m_LightAdd = g_pSceneDesc->m_GlobalModelLightAdd;
	CallModelHook();
	m_ObjectColor *= 1.0f / 255.0f;
	if (g_Saturate)
		m_ObjectColor *= g_CV_ModelSaturation.m_FloatVal;

	if (m_pInstance->m_Flags & FLAG_NOLIGHT)
	{
		m_DirLightDir.Init();
		m_DirLightAmount = 0.0f;
		m_AmbientLight.Init(255.0f, 255.0f, 255.0f);
		return;
	}

	if (!g_pFrameMainWorld || !g_CV_ModelApplySun.m_IntVal ||
		(g_GlobalVertexTint.x == 0.0f && g_GlobalVertexTint.y == 0.0f && g_GlobalVertexTint.z == 0.0f) ||
		(RENDERSTRUCT_LIGHTS->m_GlobalLightColor.x == 0.0f && RENDERSTRUCT_LIGHTS->m_GlobalLightColor.y == 0.0f &&
		RENDERSTRUCT_LIGHTS->m_GlobalLightColor.z == 0.0f))
	{
		m_DirLightDir.Init();
		m_DirLightAmount = 0.0f;
	}
	else
	{
		if (m_pInstance->m_Unknown2BC < 0.0f ||
			((m_pInstance->m_Flags2 & FLAG2_DYNAMICDIRLIGHT) &&
			!m_Unk5d0.Equals(m_pInstance->m_Unknown2B0, g_CV_ModelSunVariance.m_FloatVal + 0.001f)))
		{
			m_pInstance->m_Unknown2B0 = m_Unk5d0;
			m_DirLightAmount = GetDirLightAmount();
			m_pInstance->m_Unknown2BC = m_DirLightAmount;
		}
		else
			m_DirLightAmount = m_pInstance->m_Unknown2BC;

		MatVMul_3x3(&m_DirLightDir, &mInvTransform, &RENDERSTRUCT_LIGHTS->m_GlobalLightDir);
		m_DirLightDir = -m_DirLightDir;
	}

	m_DirLightColor = RENDERSTRUCT_LIGHTS->m_GlobalLightColor * g_GlobalVertexTint;

	if (g_pFrameMainWorld)
	{
		LTRGB rgb;

		w_GetLightVal(&g_pFrameMainWorld->m_LightTable, &m_Unk5d0, &rgb);
		m_AmbientLight.x = rgb.r;
		m_AmbientLight.y = rgb.g;
		m_AmbientLight.z = rgb.b;
	}
	else
	{
		m_AmbientLight.x = 0.0f;
		m_AmbientLight.y = 0.0f;
		m_AmbientLight.z = 0.0f;
	}
	m_AmbientLight *= g_GlobalVertexTint;

	m_nModelLights = 0;

	LTVector vNoDir;
	uint32 i;

	vNoDir.Init();
	for (i = 0; i < g_nNumObjectDynamicLights; i++)
	{
		DynamicLight *pLight = g_ObjectDynamicLights[i];

		AddModelLight(&mInvTransform, &pLight->m_Pos, pLight->m_LightRadius,
			pLight->m_ColorR, pLight->m_ColorG, pLight->m_ColorB, 0.0f, 0.0f, 0.0f, &vNoDir, -1.0f);
		if ((uint32)m_nModelLights >= m_nMaxModelLights)
			break;
	}

	if ((uint32)m_nModelLights < m_nMaxModelLights && g_pFrameMainWorld)
	{
		CallbackData.m_Unk00 = this;
		CallbackData.m_Unk04 = mInvTransform;
		foInfo.m_iObjArray = NOA_Lights;
		foInfo.m_Min = m_Unk5d0 - LTVector(m_pModel->m_VisRadius, m_pModel->m_VisRadius, m_pModel->m_VisRadius);
		foInfo.m_Max = m_Unk5d0 + LTVector(m_pModel->m_VisRadius, m_pModel->m_VisRadius, m_pModel->m_VisRadius);
		foInfo.m_CB = &ModelDraw::StaticLightCB;
		foInfo.m_pCBUser = &CallbackData;
		g_pFrameMainWorld->m_WorldTree.FindObjectsInBox2(&foInfo);
	}
}

// FUNCTION: D3DREN 0x1000e70c ?Cross@?$_CVector@M@@QBE?AV1@V1@@Z
// FUNCTION: D3DREN 0x1000e756 ?Equals@?$_CVector@M@@QBEIABV1@M@Z

// NAME: w_GetLightVal: the engine's twin 0x00405980 (clientde_impl.cpp, a different revision / compiler).
// FUNCTION: D3DREN 0x1000c860
void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGB *pRGB)
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

// ---- the model drawing ----

void d3d_DrawDevicePrimitive(D3DPRIMITIVETYPE type, DWORD dwVertexTypeDesc, LPVOID lpvVertices, DWORD dwVertexCount, DWORD dwFlags);	// 0x1000d340 below

// GLOBAL: D3DREN 0x1005a004
extern uint8 g_VertexTintTableR[256];	// guess: gamma table of the red channel (255 = full light)
// GLOBAL: D3DREN 0x1005a104
extern uint8 g_VertexTintTableG[256];	// guess: gamma table of the green channel
// GLOBAL: D3DREN 0x1005a204
extern uint8 g_VertexTintTableB[256];	// guess: gamma table of the blue channel

// guess: draws the fade sprite of a model that is far away (Model::m_pFadeSpriteTex, size m_FadeSpriteSizeX/Y) as a lit camera
// facing quad around the instance position; nAlpha is the vertex alpha.
// Not matching (best effort, 843 vs 1639 bytes; same calls (FUN_100079e4 texture bind, Cross 0x1000e70c, Norm 0x1000e6d9, w_GetLightVal,
// d3d_ClipAndProjectTLVertices, d3d_DrawDevicePrimitive), same four corners P-R+U, P+R+U, P+R-U, P-R-U in the same order).  Where it differs: in the exe the
// SDK operators of LTVector are expanded inline but the LTVector(x, y, z) constructor inside every operator+ / operator- after the
// first one stays an out-of-line call (0x1000dfb6: `mov eax,ecx; mov [eax],arg1..3; ret 0xc`, called with the three x87 results
// pushed right to left, its result copied with movsd into the vertex), and the inline temporaries live in 0x104 bytes of frame
// (vUp at [ebp-0xc], the Cross result and vRight at [ebp-0x28], sizes through spilled temporaries such as [ebp-0x44]); ours inlines
// every constructor, so the corner code is half the size.  That is the inline-budget outcome of the original statement structure
// (nested sites get a shrinking share, tools/inline_budget.py R8): the number of inline-candidate calls the original made after each
// operator is not known; tools/inline_budget.py does not run on this unit here (its callee-cost probes all report 9000u).  Explicit
// LTVector(x, y, z) corners (753 bytes) and by-value operators were tried and are further away.  0x1000dfb6 (25 bytes) is not
// emitted for the same reason: only a function that leaves the constructor out of line produces it.
// STUB: D3DREN 0x1000ccd9
void ModelDraw::DrawFadeSprite(ModelInstance *pInstance, uint8 nAlpha)
{
	Model *pModel = pInstance->GetModelDB();
	LTVector vUp(0.0f, 1.0f, 0.0f);

	if (pModel->m_pFadeSpriteTex)
	{
		TLVertex aVerts[4];
		TLVertex *pVerts;
		int nVerts;
		LTVector vRight, vSizeUp, vSizeRight;
		LTRGB rgb;
		uint8 r, g, b;

		d3d_SetTexture(pModel->m_pFadeSpriteTex, g_NormalTextureStage, 0);

		vRight = (m_Unk5d0 - g_ViewParams.m_Pos).Cross(vUp);
		vRight.y = 0.0f;
		vRight.Norm();

		vUp = LTVector(0.0f, pModel->m_FadeSpriteSizeY, 0.0f);
		vSizeUp = vUp * pInstance->m_Scale.y;
		vSizeRight = vRight * pModel->m_FadeSpriteSizeX * pInstance->m_Scale.x;

		aVerts[0].m_Vec = (m_Unk5d0 - vSizeRight) + vSizeUp;
		aVerts[1].m_Vec = (m_Unk5d0 + vSizeRight) + vSizeUp;
		aVerts[2].m_Vec = (m_Unk5d0 + vSizeRight) - vSizeUp;
		aVerts[3].m_Vec = (m_Unk5d0 - vSizeRight) - vSizeUp;

		aVerts[0].tu = g_TextureStageTexelSizes[0].m_Unk00;
		aVerts[0].tv = g_TextureStageTexelSizes[0].m_Unk04;
		aVerts[1].tu = 1.0f - g_TextureStageTexelSizes[0].m_Unk00;
		aVerts[1].tv = g_TextureStageTexelSizes[0].m_Unk04;
		aVerts[2].tu = 1.0f - g_TextureStageTexelSizes[0].m_Unk00;
		aVerts[2].tv = 1.0f - g_TextureStageTexelSizes[0].m_Unk04;
		aVerts[3].tu = g_TextureStageTexelSizes[0].m_Unk00;
		aVerts[3].tv = 1.0f - g_TextureStageTexelSizes[0].m_Unk04;

		if (g_CV_LightModelSprites.m_IntVal && g_pFrameMainWorld)
		{
			w_GetLightVal(&g_pFrameMainWorld->m_LightTable, &m_Unk5d0, &rgb);
			r = g_VertexTintTableR[rgb.r];
			g = g_VertexTintTableG[rgb.g];
			b = g_VertexTintTableB[rgb.b];
		}
		else
		{
			r = g_VertexTintTableR[255];
			g = g_VertexTintTableG[255];
			b = g_VertexTintTableB[255];
		}

		for (int i = 0; i < 4; i++)
		{
			aVerts[i].rgb.b = b;
			aVerts[i].rgb.g = g;
			aVerts[i].rgb.r = r;
			aVerts[i].rgb.a = nAlpha;
			aVerts[i].specular = m_Unk640;
		}

		pVerts = aVerts;
		nVerts = 4;
		g_ClipFlags = 0x3f;
		if (d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
			d3d_DrawDevicePrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
	}
}


// guess: draws primitives through the device (wrapper around IDirect3DDevice7::DrawPrimitive).
// FUNCTION: D3DREN 0x1000d340
void d3d_DrawDevicePrimitive(D3DPRIMITIVETYPE type, DWORD dwVertexTypeDesc, LPVOID lpvVertices, DWORD dwVertexCount, DWORD dwFlags)
{
	g_pD3DDevice->DrawPrimitive(type, dwVertexTypeDesc, lpvVertices, dwVertexCount, dwFlags);
}

// guess: 3 * the triangle count of the current LOD (m_Unk60c) of every piece.
// FUNCTION: D3DREN 0x1000d35f
int ModelDraw::GetLODIndexCount()
{
	uint32 i;
	int nTris = 0;

	for (i = 0; i < m_pModel->NumPieces(); i++)
	{
		PieceLOD *pLOD = m_pModel->GetPiece(i)->GetLOD(m_nLOD);

		if (pLOD)
			nTris += pLOD->m_Tris.GetSize();
	}
	return nTris * 3;
}

// guess: node transforms of the model (m_Unk840[i] = *pMat * model node transform i).
// FUNCTION: D3DREN 0x1000df7a
void ModelDraw::BuildProjectedNodeTransforms(LTMatrix *pMat)
{
	uint32 i;

	for (i = 0; i < m_pModel->m_Transforms.GetSize(); i++)
		MatMul(&m_Unk840[i], pMat, &m_pModel->m_Transforms[i]);
}

// guess: key of the rigid model cache (the vertex buffer cache's two keys): the specular colour word bits and the LOD index.
// FUNCTION: D3DREN 0x1000de1f
uint32 d3d_PackRigidModelCacheKey(uint32 nKey1, uint32 nKey2)
{
	return ((((((nKey2 & ~0x3f) << 6) | (nKey2 & 0x30)) << 6 | (nKey2 & 0xc)) << 6 | (nKey2 & 3)) << 6) | ((nKey1 >> 2) & 0x3f3f3f3f);
}

// guess: picks the LOD (m_Unk60c) from the distance m_fModelDist; with ModelLODBlendEnable it also computes the blend (m_bLODBlend/m_fLODBlend).
// FUNCTION: D3DREN 0x1000de56
void ModelDraw::SelectLODAndBlend()
{
	float fRange;
	float fDist;
	uint32 i;

	m_nLOD = 0;
	fDist = m_fModelDist;
	for (i = 0; i < m_pModel->m_LODDists.GetSize() + 1; i++)
	{
		if (fDist > *m_pModel->GetLODDist(i))
			m_nLOD = i;
	}

	m_bLODBlend = 0;
	if (g_CV_ModelLODBlendEnable.m_IntVal)
	{
		if (m_nLOD + 1 < m_pModel->m_LODDists.GetSize() + 1)
		{
			float *pNext = m_pModel->GetLODDist(m_nLOD + 1);
			float fBlend;
			float fDelta;

			fRange = g_CV_ModelLODBlendDist.m_FloatVal;
			fBlend = (*pNext - *m_pModel->GetLODDist(m_nLOD)) * 0.5f;
			fRange = LTMIN(fRange, fBlend);
			fDelta = *pNext - fDist;

			if (fDelta < fRange)
			{
				m_fLODBlend = 1.0f - fDelta / fRange;
				m_bLODBlend = 1;
			}
		}
	}
}

// guess: nothing to do when the warble is off.
// FUNCTION: D3DREN 0x1000dd18
void d3d_BeginModelProjectionNoOp(void)
{
}

// guess: projects one model vertex into a TL vertex (no warble).
// FUNCTION: D3DREN 0x1000dd34 ?MatVMul_H@@YAMPAV?$_CVector@M@@PAVLTMatrix@@0@Z
// FUNCTION: D3DREN 0x1000dd19
void __fastcall d3d_ProjectModelVertex(ModelVert *pVert, TLVertex *pOut, LTMatrix *pMat)
{
	pOut->rhw = MatVMul_H(&pOut->m_Vec, pMat, &pVert->m_Vec);
}

// guess: begins the warble of the vertex projection: the table index follows the warble phase.
// FUNCTION: D3DREN 0x1000ddb4
void d3d_BeginModelWarbleProjection(void)
{
	g_nModelWarbleIndex = (int)g_fModelWarblePhase;
}

// guess: projects one model vertex into a TL vertex with the warble of the current table entry.
// FUNCTION: D3DREN 0x1000ddc5
void __fastcall d3d_ProjectWarbledModelVertex(ModelVert *pVert, TLVertex *pOut, LTMatrix *pMat)
{
	LTVector v;
	float fScale;
	uint32 iEntry = g_nModelWarbleIndex & 0x1f;

	fScale = g_fModelWarbleFraction * g_ModelWarbleDeltas[iEntry] + g_ModelWarbleScales[iEntry];
	g_nModelWarbleIndex++;
	v.x = fScale * pVert->m_Vec.x;
	v.y = fScale * pVert->m_Vec.y;
	v.z = fScale * pVert->m_Vec.z;
	pOut->rhw = MatVMul_H(&pOut->m_Vec, pMat, &v);
}

// ---- out-of-line copies of Talon SDK inlines ----------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1000dcc7 ??0UnkType_DrawState@@QAE@XZ
UnkType_DrawState::UnkType_DrawState()
{
	int i;

	for (i = 0; i < 8; i++)
	{
		m_Unk000[i].m_Unk0c = -1;
		m_Unk000[i].m_Unk18 = -1;
		m_Unk000[i].m_Unk20 = 0;
		m_Unk000[i].ResetStageRecord();
	}
	m_Unk124 = 0;
	m_Unk128 = 0;
	m_Unk12c = 0;
	m_Unk130 = 0;
	m_Unk134 = 0;
	m_Unk120 = 0;
}

// guess: the per-model entry: caches the instance, fades the model by its distance (DrawFadeSprite draws the fade sprite), picks the
// LOD, looks the model up in the rigid vertex buffer cache, builds the matrices, lights it and calls DrawModelRenderPasses.
// Not matching (2126 vs 2129 bytes, 628 vs 627 instructions; all code and the stack frame are the same, 60 of the 70 aligned
// differences are unrelocated addresses): the matrix products are the SDK's `LTMatrix::operator*` (a hidden shared temporary at
// [ebp-0x68]; with explicit MatMul calls and a named temporary the two REALLYCLOSE / else products were merged into one call with a
// selected argument, which the exe does not do).  What is left is the direction of one cross-jump: after the products the exe emits the
// shared `rep movsd` of `m_Unk510 = <temp>` inside the field-of-view block and lets the default case of the m_Unk8ac switch jump back
// to it (0x1000d953), ours emits it in the default case and jumps forward from the field-of-view block (3 bytes of size difference
// in that region, `push 0x10; pop ecx` placement in the else branch).  No source shape tried (if/else vs ternary vs pointer for the
// matrix source, memcpy, copy placement, block order) changes it; permuter 3000 candidates did not either.
// FUNCTION: D3DREN 0x1000dbf8 ?TransformPlane@LTMatrix@@QAE?AVLTPlane@@AAV2@@Z
// FUNCTION: D3DREN 0x1000dc6d ?Apply@LTMatrix@@QAEXABV?$_CVector@M@@AAV2@@Z
// FUNCTION: D3DREN 0x1000dfb6 ??0?$_CVector@M@@QAE@MMM@Z
// FUNCTION: D3DREN 0x1000dfcf ?Dist@?$_CVector@M@@QBEMABV1@@Z
// STUB: D3DREN 0x1000d3a7
void ModelDraw::DrawModel(ModelInstance *pInstance)
{
	UnkType_DrawState state;
	LTMatrix mWork;
	float fOldNearZ;
	float fDistSqr;

	m_Unk008 = &pInstance->m_AnimTracker;
	m_pModel = m_Unk008->GetModel();
	m_pInstance = pInstance;

	if (pInstance->m_Flags & FLAG_REALLYCLOSE)
		g_ViewParams.m_mInvView.Apply(pInstance->m_Pos, m_Unk5d0);
	else
		m_Unk5d0 = pInstance->m_Pos;

	if (!g_CV_DrawModelsRigid.m_IntVal && m_pModel->m_bRigid)
		return;
	if (!g_DrawGuns && (pInstance->m_Flags & FLAG_REALLYCLOSE))
		return;

	m_Unk30 = (m_pInstance->m_Flags >> 3) & 1;
	g_pfnCalcFogAlpha(&m_Unk5d0, &m_Unk640);

	fDistSqr = (m_Unk5d0 - g_ViewParams.m_Pos).MagSqr();
	if (m_pModel->m_FadeRangeMaxSqr >= m_pModel->m_FadeRangeMinSqr)
	{
		if (fDistSqr > m_pModel->m_FadeRangeMaxSqr)
		{
			DrawFadeSprite(pInstance, 0xff);
			return;
		}
		if (fDistSqr <= m_pModel->m_FadeRangeMinSqr)
			m_Unk8a8 = m_pInstance->m_ColorA;
		else
		{
			float fDist = (float)sqrt(fDistSqr);
			uint8 nAlpha = (uint8)RoundFloatToInt((1.0f - (fDist - m_pModel->m_FadeRangeMin) / (m_pModel->m_FadeRangeMax - m_pModel->m_FadeRangeMin)) * m_pInstance->m_ColorA);

			m_Unk8a8 = nAlpha;
			DrawFadeSprite(pInstance, 0xff - nAlpha);
		}
	}
	else
	{
		if (fDistSqr < m_pModel->m_FadeRangeMaxSqr)
			return;
		if (fDistSqr < m_pModel->m_FadeRangeMinSqr)
		{
			float fDist = (float)sqrt(fDistSqr);

			m_Unk8a8 = (uint8)RoundFloatToInt(((fDist - m_pModel->m_FadeRangeMax) / (m_pModel->m_FadeRangeMin - m_pModel->m_FadeRangeMax)) * m_pInstance->m_ColorA);
		}
		else
			m_Unk8a8 = m_pInstance->m_ColorA;
	}

	m_Unk618 = ((g_EnvMapAll || (pInstance->m_Flags & FLAG_ENVIRONMENTMAP)) && g_EnvMapEnable && g_pStruct->m_pEnvMapTexture) ? 1 : 0;

	m_fModelDist = g_ViewParams.m_Pos.Dist(m_Unk5d0);
	m_fModelDist = m_fModelDist / g_CV_ModelZoomScale.m_FloatVal;
	SelectLODAndBlend();

	if (m_pModel->m_bRigid && g_CV_ModelCacheRigid.m_IntVal)
	{
		ModelInstance *pInst = m_pInstance;
		int nKey = d3d_PackRigidModelCacheKey(m_Unk640, m_nLOD);

		if (!m_bLODBlend && m_Unk8a8 == m_pInstance->m_ColorA)
		{
			if (UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache.SelectEntry((int)pInst, nKey))
				m_Unk8ac = 2;
			else if (UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache.AllocateEntry((int)pInst, nKey, GetLODIndexCount(), 0))
				m_Unk8ac = 1;
			else
				m_Unk8ac = 0;
		}
		else
			m_Unk8ac = 0;
	}
	else
		m_Unk8ac = 0;

	if (g_ModelVBCacheLastAgeFrameCode != g_CurFrameCode)
	{
		UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache.AgeEntries();
		g_ModelVBCacheLastAgeFrameCode = g_CurFrameCode;
	}

	m_Unk638 = (g_TintModels && (pInstance->m_Flags & FLAG_MODELTINT) && pInstance->m_ColorA == 0xff) ? 1 : 0;

	m_Unk8b0 = (m_Unk8ac || (g_CV_ModelUseTnL.m_IntVal && !(m_pInstance->m_Flags & FLAG_REALLYCLOSE) && !m_pModel->m_bFovOffset)) ? 1 : 0;

	d3d_SetupTransformation(&m_pInstance->m_Pos, (float *)&m_pInstance->m_Rotation, &m_pInstance->m_Scale, &m_ModelTransform);

	if (m_pModel->m_bFovOffset)
	{
		LTMatrix mProj, mTemp2;
		float fFovX, fFovY;

		if (g_CV_ModelFovTest.m_IntVal)
		{
			fFovX = (g_CV_ExtraFOVXOffset.m_FloatVal + m_pModel->m_FovXOffset) * g_ViewParams.m_fFovX;
			if (fFovX < 0.17453292f)
				fFovX = 0.17453292f;
			else if (fFovX > 2.9670596f)
				fFovX = 2.9670596f;
			fFovY = (g_CV_ExtraFOVYOffset.m_FloatVal + m_pModel->m_FovYOffset) * g_ViewParams.m_fFovY;
			if (fFovY < 0.17453292f)
				fFovY = 0.17453292f;
			else if (fFovY > 2.9670596f)
				fFovY = 2.9670596f;
		}
		else
		{
			fFovX = g_CV_ExtraFOVXOffset.m_FloatVal * 0.017453292f + m_pModel->m_FovXOffset + g_ViewParams.m_fFovX;
			if (fFovX < 0.17453292f)
				fFovX = 0.17453292f;
			else if (fFovX > 2.9670596f)
				fFovX = 2.9670596f;
			fFovY = g_CV_ExtraFOVYOffset.m_FloatVal * 0.017453292f + m_pModel->m_FovYOffset + g_ViewParams.m_fFovY;
			if (fFovY < 0.17453292f)
				fFovY = 0.17453292f;
			else if (fFovY > 2.9670596f)
				fFovY = 2.9670596f;
		}

		mProj.Init((float)tan(fFovX * 0.5f), 0.0f, 0.0f, 0.0f,
			0.0f, (float)tan(fFovY * 0.5f), 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		if (pInstance->m_Flags & FLAG_REALLYCLOSE)
		{
			mWork = mProj * g_ViewParams.m_mShear;
		}
		else
		{
			mWork = mProj * g_ViewParams.m_mShearView;
		}
		mTemp2 = g_ViewParams.m_DeviceTimesProjection * mWork;
		m_Transform = mWork * m_ModelTransform;
	}
	else
	{
		if (pInstance->m_Flags & FLAG_REALLYCLOSE)
			mWork = g_ViewParams.m_mReallyCloseClipTransform;
		else
			mWork = g_ViewParams.m_mClipTransform;
		switch (m_Unk8ac)
		{
		case 1:
			m_Transform = m_ModelTransform;
			g_ClipFlags = 1;
			break;
		case 2:
			m_Transform.Identity();
			g_ClipFlags = 1;
			break;
		default:
			if (!m_Unk8b0)
			{
				m_Transform = mWork * m_ModelTransform;
			}
			else
			{
				m_Transform = m_ModelTransform;
				g_ClipFlags = 1;
			}
			break;
		}
	}

	{
		LTMatrix mInv = m_Transform;

		mInv.Inverse();
		m_InvTransform = mInv;
	}

	if (!EnsureVertexAndTransformBuffers())
		return;

	fOldNearZ = g_ViewParams.m_NearZ;
	if (m_pInstance->m_Flags & FLAG_REALLYCLOSE)
	{
		g_ClipFlags |= 1;
		g_ViewParams.m_NearZ = g_CV_ReallyCloseNearZ.m_FloatVal;
	}

	if (m_Unk8ac != 2)
	{
		LTMatrix *pTransforms = m_pInstance->GetTransforms();
		uint32 i;

		for (i = 0; i < m_pModel->m_Transforms.GetSize(); i++)
		{
			m_pModel->m_Transforms[i] = m_Transform * pTransforms[i];
		}
		if (!g_ClipFlags)
			BuildProjectedNodeTransforms(&g_ViewParams.m_DeviceTimesProjection);
		SetupModelLight();
	}

	if (m_Unk618 && m_pModel->m_bNormalRef)
	{
		LTMatrix mA, mB;

		mA = g_ViewParams.m_mView * m_pModel->m_Transforms[m_pModel->m_iNormalRefNode];
		MatMul(&mB, &mA, &m_pModel->m_mNormalRef);
		m_EnvMapTransform = mB;
	}
	else
		m_EnvMapTransform = g_ViewParams.m_mView;

	if (m_pInstance->m_NodeControlFn && !(m_pInstance->m_Unknown188 & 8))
		g_ClipFlags |= 0x3f;

	m_Unk5dc = g_ModelWarble ? d3d_BeginModelWarbleProjection : d3d_BeginModelProjectionNoOp;
	m_Unk5e0 = g_ModelWarble ? d3d_ProjectWarbledModelVertex : d3d_ProjectModelVertex;

	m_Unk00c.m_Flags = 0;
	if (m_pInstance->m_HookFn)
		m_pInstance->m_HookFn((HLOCALOBJ)m_pInstance, &m_Unk00c, 0);
	if (m_Unk00c.m_Flags & MIH_CLIPPLANE)
	{
		m_Unk020 = g_ViewParams.m_mClipTransform.TransformPlane(m_Unk00c.m_ClipPlane);
		g_ClipFlags |= 1;
	}

	DrawModelRenderPasses();
	g_ViewParams.m_NearZ = fOldNearZ;
	if (g_ClipFlags)
		g_nClippedModelsDrawn++;
	else
		g_nUnclippedModelsDrawn++;
}
// ---- CMoArray instances requested by ModelDraw (compiler generated, no source of their own) -----------------------
// CMoArray<unsigned short> (vtable 0x10046204), CMoArray<TLVertex> (0x10046234), CMoArray<LTMatrix> (0x10046264): the
// constructors, the virtual GenList slots and the out-of-line members.  The slots GenBegin and GenIsValid are identical
// for every T: the linker folded them into one copy each (0x1000e0c1, 0x1000e291).  GenGetSize, GenSetCacheSize and
// _DeleteAndDestroyArray were folded into the copies at 0x1001e3d2, 0x1001e3fd and 0x1001e57c (another unit's range).
// FUNCTION: D3DREN 0x1000e09f ??0?$CMoArray@GVDefaultCache@@@@QAE@XZ
// FUNCTION: D3DREN 0x1000e26f ??0?$CMoArray@UTLVertex@@VDefaultCache@@@@QAE@XZ
// FUNCTION: D3DREN 0x1000e4ac ??0?$CMoArray@VLTMatrix@@VDefaultCache@@@@QAE@XZ
// The slots GenBegin and GenIsValid are identical for every T and the linker folded them: this is the surviving copy.
// FUNCTION: D3DREN 0x1000e0c1 ?GenBegin@?$CMoArray@GVDefaultCache@@@@UBE?AVGenListPos@@XZ
// FUNCTION: D3DREN 0x1000e0d7 ?GenGetNext@?$CMoArray@GVDefaultCache@@@@UBEGAAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e0ec ?GenGetAt@?$CMoArray@GVDefaultCache@@@@UBEGAAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e0fc ?GenAppend@?$CMoArray@GVDefaultCache@@@@UAEHAAG@Z
// FUNCTION: D3DREN 0x1000e111 ?GenRemoveAt@?$CMoArray@GVDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e122 ?GenRemoveAll@?$CMoArray@GVDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x1000e12f ?GenCopyList@?$CMoArray@GVDefaultCache@@@@UAEHABV?$GenList@G@@@Z
// FUNCTION: D3DREN 0x1000e1b9 ?GenAppendList@?$CMoArray@GVDefaultCache@@@@UAEHABV?$GenList@G@@@Z
// FUNCTION: D3DREN 0x1000e248 ?GenFindElement@?$CMoArray@GVDefaultCache@@@@UBEHABGAAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e291 ?GenIsValid@?$CMoArray@GVDefaultCache@@@@UBEHABVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e2a1 ?GenGetNext@?$CMoArray@UTLVertex@@VDefaultCache@@@@UBE?AUTLVertex@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e2c4 ?GenGetAt@?$CMoArray@UTLVertex@@VDefaultCache@@@@UBE?AUTLVertex@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e2e2 ?GenAppend@?$CMoArray@UTLVertex@@VDefaultCache@@@@UAEHAAUTLVertex@@@Z
// FUNCTION: D3DREN 0x1000e2f7 ?GenRemoveAt@?$CMoArray@UTLVertex@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e308 ?GenRemoveAll@?$CMoArray@UTLVertex@@VDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x1000e315 ?GenCopyList@?$CMoArray@UTLVertex@@VDefaultCache@@@@UAEHABV?$GenList@UTLVertex@@@@@Z
// FUNCTION: D3DREN 0x1000e3c9 ?GenAppendList@?$CMoArray@UTLVertex@@VDefaultCache@@@@UAEHABV?$GenList@UTLVertex@@@@@Z
// FUNCTION: D3DREN 0x1000e485 ?GenFindElement@?$CMoArray@UTLVertex@@VDefaultCache@@@@UBEHABUTLVertex@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e4ce ?GenGetNext@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UBE?AVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e4f1 ?GenGetAt@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UBE?AVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e50f ?GenAppend@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UAEHAAVLTMatrix@@@Z
// FUNCTION: D3DREN 0x1000e524 ?GenRemoveAt@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e535 ?GenRemoveAll@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x1000e542 ?GenCopyList@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UAEHABV?$GenList@VLTMatrix@@@@@Z
// FUNCTION: D3DREN 0x1000e5f6 ?GenAppendList@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UAEHABV?$GenList@VLTMatrix@@@@@Z
// FUNCTION: D3DREN 0x1000e6b2 ?GenFindElement@?$CMoArray@VLTMatrix@@VDefaultCache@@@@UBEHABVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1000e7cc ?Init@?$CMoArray@GVDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1000e801 ?SetSize2@?$CMoArray@GVDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000e860 ?InternalNiceSetSize@?$CMoArray@GVDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000e90f ?Init@?$CMoArray@UTLVertex@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1000e944 ?SetSize2@?$CMoArray@UTLVertex@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000e9a6 ?InternalNiceSetSize@?$CMoArray@UTLVertex@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ea62 ?Init@?$CMoArray@VLTMatrix@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1000ea97 ?SetSize2@?$CMoArray@VLTMatrix@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000eaf6 ?InternalNiceSetSize@?$CMoArray@VLTMatrix@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ebb2 ?Insert2@?$CMoArray@GVDefaultCache@@@@QAEHKABGPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ec7a ?Remove2@?$CMoArray@GVDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ed35 ?_AllocateTArray@?$CMoArray@GVDefaultCache@@@@AAEPAGKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ed4c ?Insert2@?$CMoArray@UTLVertex@@VDefaultCache@@@@QAEHKABUTLVertex@@PAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ee57 ?Remove2@?$CMoArray@UTLVertex@@VDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000ef41 ?Insert2@?$CMoArray@VLTMatrix@@VDefaultCache@@@@QAEHKABVLTMatrix@@PAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000f049 ?Remove2@?$CMoArray@VLTMatrix@@VDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1000f130 ?_AllocateTArray@?$CMoArray@VLTMatrix@@VDefaultCache@@@@AAEPAVLTMatrix@@KPAVLAlloc@@@Z
// The out-of-class member template and the allocation helper that the CMoArray<TLVertex> code needs.
// FUNCTION: D3DREN 0x1000e6d9 ?Norm@?$_CVector@M@@QAEXM@Z
// FUNCTION: D3DREN 0x1000f148 ?BaseNew@@YAPAUTLVertex@@PAVLAlloc@@PAU1@K@Z
