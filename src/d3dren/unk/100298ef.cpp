// d3d.ren unk/100298ef (0x100298ef-0x1002afd0): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// DrawPolyMgr (Talon only). Link order: name must be < drawpolygrid (not drawpolymgr).
// FLAGS: /O1 /Ob2
// unit unk/10029660 (0x10029660-0x1002afd0): two objects of a size-optimised run (/O1 /Ob2, packed, no padding):
//   0x10029660-0x100298ee  drawparticles_A (Jupiter render_a/src/sys/d3d/drawparticles_A.cpp: d3d_ProcessParticles,
//                           d3d_TestAndDrawPS, d3d_QueueTranslucentParticles; the PSSrcBlend/PSDestBlend console variables)
//   0x100298ef-0x1002afcf  DrawPolyMgr (Talon only: state-block based multi-pass polygon drawing; no Jupiter source)
// NAME: the object boundary is from names_proposal.csv / NAMING.md (TU table); the static-initialiser numbers below are
// those of this unit's own compile (the originals restart in each of the two objects).
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dstate.h"
#include "d3dren/visibleset.h"
#include "d3dren/viewparams.h"
#include "d3dren/drawobjects.h"
#include "d3dren/drawpolymgr.h"
#include "d3dren/pool.h"
#include "d3dren/setupmodel.h"
#include "d3dren/polydraw.h"
#include "d3dren/common_draw.h"
#include "d3dren/d3d_draw.h"

// ---- DrawPolyMgr -------------------------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x100298ef _$E2
// FUNCTION: D3DREN 0x100298f4 _$E1
// GLOBAL: D3DREN 0x1006e820
ConVar g_CV_DetailTextureScale("DetailTextureScale", 0.2f);
// FUNCTION: D3DREN 0x10029912 _$E5
// FUNCTION: D3DREN 0x10029917 _$E4
// GLOBAL: D3DREN 0x1006e7c0
ConVar g_CV_DrawPolyMgr("DrawPolyMgr", 0.0f);

// FUNCTION: D3DREN 0x10029931 _$E10
// FUNCTION: D3DREN 0x10029ac6 _$E8
DrawPolyMgr g_DrawPolyMgr;

// FUNCTION: D3DREN 0x10029947 ??0DrawPolyMgr@@QAE@XZ
DrawPolyMgr::DrawPolyMgr() : m_Unk7d4(0x20, 0)
{
	m_Unk76c = 0;
	InitTestGouraudMaterial();
}

// FUNCTION: D3DREN 0x10029995 ??0Material@@QAE@XZ
Material::Material()
{
	m_Name[0] = 0;
	m_nPasses = 0;
	m_pInstance = 0;
}

// FUNCTION: D3DREN 0x100299c3 ??0UnkType_DPMPass@@QAE@XZ
UnkType_DPMPass::UnkType_DPMPass()
{
	m_Unk00 = 0;
	m_Unk04 = m_Unk08 = 0;
	m_Unk0c = 0;
	m_Unk10 = 0;
	m_Unk14 = 0;
	m_Unk18 = 0;
	m_nStages = 0;
}

// FUNCTION: D3DREN 0x100299f5 ??0UnkType_DPMMaterialInstance@@QAE@XZ
UnkType_DPMMaterialInstance::UnkType_DPMMaterialInstance()
{
	m_pMaterial = 0;
	m_pNext = 0;
}

// FUNCTION: D3DREN 0x10029a1d ??0UnkType_DPMPassList@@QAE@XZ
UnkType_DPMPassList::UnkType_DPMPassList()
{
	for (int i = 0; i < 0x20; i++)
		m_Unk000[i].TieOff();
	m_Unk188.TieOff();
	m_Stages[0].m_ColorOp = D3DTOP_MODULATE;
	m_Stages[0].m_ColorArg1 = D3DTA_TEXTURE;
	m_Stages[0].m_ColorArg2 = D3DTA_DIFFUSE;
	m_Stages[0].m_AlphaOp = D3DTOP_MODULATE;
	m_Stages[0].m_AlphaArg1 = D3DTA_TEXTURE;
	m_Stages[0].m_AlphaArg2 = D3DTA_DIFFUSE;
}

// FUNCTION: D3DREN 0x10029a80 ?InitTestGouraudMaterial@DrawPolyMgr@@QAEXXZ
void DrawPolyMgr::InitTestGouraudMaterial()
{
	strcpy(m_Name, "TEST_GOURAUD");
	m_pInstance = &m_Unk1cc;
	m_Passes[0].m_Unk00 = 0;
	m_nPasses = 1;
	m_Passes[0].m_Unk10 = 1;
	m_Passes[0].m_Unk14 = 0;
	m_Passes[0].m_nStages = 1;
	m_Passes[0].m_Stages[0].m_Unk18 = 0;
	m_Passes[0].m_Stages[0].m_Unk1c = 0;
	m_Unk1cc.m_pMaterial = this;
	m_Unk1cc.m_pNext = 0;
	m_Unk76c = &m_Unk1cc;
}

// FUNCTION: D3DREN 0x10029ad0 ??1DrawPolyMgr@@QAE@XZ
DrawPolyMgr::~DrawPolyMgr()
{
}

// (the float value of g_CV_DetailTextureScale at 0x1006e824 is g_CV_DetailTextureScale.m_FloatVal)
// guess: finds/creates the RTexture of pTexture for the stage and returns its 1/width, 1/height in *pU, *pV (unit sys/d3d/d3d_texture)
int d3d_EnsureTextureAndGetUVScale(SharedTexture *pTexture, uint32 nStageFlags, float *pU, float *pV);


// ---- the callbacks of the passes (tables below) ----------------------------------------------------------------------------

// guess: texture coordinates of the surface texture of a stage: the vertex's u, v scaled by the texture's 1/size
// FUNCTION: D3DREN 0x10029b12
void DrawPolyMgr::GenerateScaledBaseTexCoords(UnkType_PolyVertex *pVertex, float *pOut, int iStage)
{
	pOut[0] = m_Unk770[iStage][0] * pVertex->m_U;
	pOut[1] = m_Unk770[iStage][1] * pVertex->m_V;
}

// guess: the lightmap texture coordinates of the vertex copied through
// FUNCTION: D3DREN 0x10029b3c
void DrawPolyMgr::CopySecondaryTexCoords(UnkType_PolyVertex *pVertex, float *pOut, int iStage)
{
	pOut[0] = pVertex->m_Unk0c;
	pOut[1] = pVertex->m_Unk10;
}

// guess: planar mapping from the vertex position (x, z) plus the pan offsets, then scaled by the stage's texture size
// FUNCTION: D3DREN 0x10029b52
void DrawPolyMgr::GeneratePannedPlanarTexCoords(UnkType_PolyVertex *pVertex, float *pOut, int iStage)
{
	pOut[0] = m_Unk798 * m_Unk790 + pVertex->m_Vec->x;
	pOut[1] = m_Unk79c * m_Unk794 + pVertex->m_Vec->z;
	pOut[0] = m_Unk770[iStage][0] * pOut[0];
	pOut[1] = m_Unk770[iStage][1] * pOut[1];
}

// guess: the detail texture coordinates: the vertex's u, v scaled by the detail scale
// FUNCTION: D3DREN 0x10029b9f
void DrawPolyMgr::SetDetailUV(UnkType_PolyVertex *pVertex, float *pOut, int iStage)
{
	pOut[0] = m_Unk788 * pVertex->m_U;
	pOut[1] = m_Unk78c * pVertex->m_V;
}

// guess: the generators of two and three stages call the per-stage ones (stored at +0x7ac, +0x7b0, +0x7b4) one after the other
// FUNCTION: D3DREN 0x10029bc1
void DrawPolyMgr::SetTwoStageUV(UnkType_PolyVertex *pVertex, float *pOut, int iStage)
{
	(this->*m_Unk7ac)(pVertex, pOut, 0);
	(this->*m_Unk7b0)(pVertex, pOut + 2, 1);
}

// FUNCTION: D3DREN 0x10029bed
void DrawPolyMgr::SetThreeStageUV(UnkType_PolyVertex *pVertex, float *pOut, int iStage)
{
	(this->*m_Unk7ac)(pVertex, pOut, 0);
	(this->*m_Unk7b0)(pVertex, pOut + 2, 1);
	(this->*m_Unk7b4)(pVertex, pOut + 4, 2);
}

// guess: texture sources: the surface texture's own scale
// FUNCTION: D3DREN 0x10029c2b
int DrawPolyMgr::InitSurfaceTextureSource(WorldPoly *pPoly, int iStage)
{
	return d3d_EnsureTextureAndGetUVScale(((Surface *)pPoly->m_pSurface)->m_pTexture, iStage, &m_Unk770[iStage][0], &m_Unk770[iStage][1]);
}

// guess: no texture source needed
// FUNCTION: D3DREN 0x10029c55
int DrawPolyMgr::InitLightmapTextureSource(WorldPoly *pPoly, int iStage)
{
	return 1;
}

// guess: the surface texture's linked (detail) texture: fetches its scale and sets the detail scale; always returns 0
// FUNCTION: D3DREN 0x10029c5b
int DrawPolyMgr::InitDetailTextureSource(WorldPoly *pPoly, int iStage)
{
	SharedTexture *pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;
	if (pTexture)
	{
		SharedTexture *pDetail = pTexture->m_pLinkedTexture;
		if (pDetail)
		{
			if (d3d_EnsureTextureAndGetUVScale(pDetail, iStage, &m_Unk770[iStage][0], &m_Unk770[iStage][1]))
			{
				m_Unk788 = g_CV_DetailTextureScale.m_FloatVal * m_Unk770[iStage][0];
				m_Unk78c = g_CV_DetailTextureScale.m_FloatVal * m_Unk770[iStage][1];
			}
		}
	}
	return 0;
}

// guess: no texture on the stage
// FUNCTION: D3DREN 0x10029cbb
int DrawPolyMgr::InitUntexturedSource(WorldPoly *pPoly, int iStage)
{
	d3d_UnsetTexture(iStage);
	return 1;
}

// guess: binds the surface texture on the stage
// FUNCTION: D3DREN 0x10029ccb
int DrawPolyMgr::BindSurfaceTexture(WorldPoly *pPoly, int iStage, int a3)
{
	return d3d_SetTexture(((Surface *)pPoly->m_pSurface)->m_pTexture, iStage, 0);
}

// guess: binds the lightmap page of the poly on the stage
// FUNCTION: D3DREN 0x10029ce6
int DrawPolyMgr::BindLightmapTexture(WorldPoly *pPoly, int iStage, int a3)
{
	return d3d_SetLightmapTexture(pPoly, iStage);
}

// guess: binds the detail texture (the surface texture's linked texture) on the stage and sets the detail scale from the stage's
// lightmap/texture scale
// FUNCTION: D3DREN 0x10029cf8
int DrawPolyMgr::BindDetailTexture(WorldPoly *pPoly, int iStage, int a3)
{
	SharedTexture *pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;
	if (pTexture)
	{
		SharedTexture *pDetail = pTexture->m_pLinkedTexture;
		if (pDetail && d3d_SetTexture(pDetail, iStage, 0))
		{
			m_Unk788 = g_CV_DetailTextureScale.m_FloatVal * g_TextureStageTexelSizes[iStage].m_Unk00;
			m_Unk78c = g_CV_DetailTextureScale.m_FloatVal * g_TextureStageTexelSizes[iStage].m_Unk04;
			return 1;
		}
	}
	return 0;
}

// guess: nothing to bind. This callback is byte-identical to DllMain, so the linker folds both at the same address.
// FUNCTION: D3DREN 0x10029d5a
int DrawPolyMgr::BindNoTexture(WorldPoly *pPoly, int iStage, int a3)
{
	return 1;
}

// FUNCTION: D3DREN 0x10029d5a _DllMain@12
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
	return TRUE;
}

// guess: vertex colour functions: fullbright
// FUNCTION: D3DREN 0x10029d60
void DrawPolyMgr::SetFullbrightColor(UnkType_PolyVertex *pVertex, TLVertex *pOut)
{
	pOut->color = 0xffffffff;
}

// guess: the vertex colour through the three 256 byte tables (colour correction), the alpha from g_nPolyVertexAlpha
// FUNCTION: D3DREN 0x10029d6b
void DrawPolyMgr::SetCorrectedVertexColor(UnkType_PolyVertex *pVertex, TLVertex *pOut)
{
	pOut->rgb.r = g_VertexTintTableR[pVertex->m_Color[2]];
	pOut->rgb.g = g_VertexTintTableG[pVertex->m_Color[1]];
	pOut->rgb.b = g_VertexTintTableB[pVertex->m_Color[0]];
	pOut->rgb.a = g_nPolyVertexAlpha;
}

// guess: the vertex colour scaled by the three floats at +0x7a0
// FUNCTION: D3DREN 0x10029da6
void DrawPolyMgr::SetScaledVertexColor(UnkType_PolyVertex *pVertex, TLVertex *pOut)
{
	pOut->rgb.r = (uint8)RoundFloatToInt((float)pVertex->m_Color[2] * m_Unk7a0);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)pVertex->m_Color[1] * m_Unk7a4);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)pVertex->m_Color[0] * m_Unk7a8);
	pOut->rgb.a = g_nPolyVertexAlpha;
}

// guess: bucket key of a polygon: the surface texture pointer; its bits 2..6 select one of the 32 hash buckets
// FUNCTION: D3DREN 0x10029e1c
void GetTextureBucketKey(WorldPoly *pPoly, int *pBucket, uint32 *pKey)
{
	uint32 key = (uint32)((Surface *)pPoly->m_pSurface)->m_pTexture;
	*pKey = key;
	*pBucket = (key >> 2) & 0x1f;
}

// guess: bucket key of a polygon: its lightmap page (WorldPoly+0x48)
// FUNCTION: D3DREN 0x10029e39
void GetLightmapBucketKey(WorldPoly *pPoly, int *pBucket, uint32 *pKey)
{
	uint32 key = (uint32)pPoly->m_Unk48;
	*pKey = key;
	*pBucket = (key >> 2) & 0x1f;
}

// guess: clip (flags) and project the 0x20-byte vertices
// FUNCTION: D3DREN 0x10029e53
int __fastcall ClipAndProjectSingleTexturePoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	g_ClipFlags = nFlags;
	return d3d_ClipAndProjectTLVertices(ppVerts, pnVerts, &g_ViewParams, 0);
}

// guess: the same for the 0x28-byte vertices
// FUNCTION: D3DREN 0x10029e70
int __fastcall ClipAndProjectTwoTexturePoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	g_ClipFlags = nFlags;
	return TransformClipProjectPolygon40((UnkType_TLVertex40 **)ppVerts, pnVerts, &g_ViewParams, 0);
}

// guess: clip only, 0x20-byte vertices
// FUNCTION: D3DREN 0x10029e8d
int __fastcall ClipSingleTexturePoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	return ClipPoly(nFlags, ppVerts, pnVerts);
}

// guess: clip only, 0x28-byte vertices
// FUNCTION: D3DREN 0x10029e9e
int __fastcall ClipTwoTexturePoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	return ClipPolygon40(nFlags, (UnkType_TLVertex40 **)ppVerts, pnVerts);
}

// guess: three texture stages are not supported: nothing is left to draw
// FUNCTION: D3DREN 0x10029eaf
int __fastcall RejectThreeTexturePoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	return 0;
}

// guess: adds the dynamic lights of the poly to the 0x28-byte vertices
// FUNCTION: D3DREN 0x10029eb4
void DrawPolyMgr::AddDynamicLightToDrawBuffer(UnkType_DPMDrawBuffer *pBuffer)
{
	AddPolyDynamicVertexLighting(pBuffer->m_pPoly, (UnkType_TLVertex40 *)pBuffer->m_Verts, pBuffer->m_nVertices);
}

// guess: nothing
// FUNCTION: D3DREN 0x10029ecc
void DrawPolyMgr::SkipDrawBufferPostprocess(UnkType_DPMDrawBuffer *pBuffer)
{
}

// guess: the tables the pass indices select from (the originals are data of the object: 0x1004bbd8..0x1004bc80)
// (g_pfnCalcSkyFogAlpha, the sky's per-vertex fog function, is declared in polydraw.h)
static UnkType_DPMKeyFn s_KeyFns[2] = { GetTextureBucketKey, GetLightmapBucketKey };	// 0x1004bbd8
static UnkType_DPMUVFn s_UVFns[4] =										// 0x1004bbe0
{
	&DrawPolyMgr::GenerateScaledBaseTexCoords, &DrawPolyMgr::CopySecondaryTexCoords, &DrawPolyMgr::GeneratePannedPlanarTexCoords, &DrawPolyMgr::SetDetailUV
};
static UnkType_DPMUVFn s_UVFnsByStages[4] =								// 0x1004bbf0 (by the number of stages)
{
	0, 0, &DrawPolyMgr::SetTwoStageUV, &DrawPolyMgr::SetThreeStageUV
};
static TextureSrcInitFn s_TextureSrcInitFns[4] =						// 0x1004bc00
{
	&DrawPolyMgr::InitSurfaceTextureSource, &DrawPolyMgr::InitLightmapTextureSource, &DrawPolyMgr::InitDetailTextureSource, &DrawPolyMgr::InitUntexturedSource
};
static UnkType_DPMBindFn s_BindFns[4] =									// 0x1004bc10
{
	&DrawPolyMgr::BindSurfaceTexture, &DrawPolyMgr::BindLightmapTexture, &DrawPolyMgr::BindDetailTexture, &DrawPolyMgr::BindNoTexture
};
static UnkType_DPMColorFn s_ColorFns[3] =								// 0x1004bc20
{
	&DrawPolyMgr::SetFullbrightColor, &DrawPolyMgr::SetCorrectedVertexColor, &DrawPolyMgr::SetScaledVertexColor
};
static UnkType_DPMFogFn *s_FogFns[2] = { &g_pfnCalcFogAlpha, &g_pfnCalcSkyFogAlpha };	// 0x1004bc2c
static int s_VertexSizes[4] = { 0, 0x20, 0x28, 0x30 };					// 0x1004bc34 (by the number of stages)
static UnkType_DPMClipFn s_ClipFns[4] = { 0, ClipAndProjectSingleTexturePoly, ClipAndProjectTwoTexturePoly, RejectThreeTexturePoly };	// 0x1004bc44
static UnkType_DPMClipFn s_ClipOnlyFns[4] = { 0, ClipSingleTexturePoly, ClipTwoTexturePoly, RejectThreeTexturePoly };	// 0x1004bc54
static UnkType_DPMPostFn s_PostFns[3] =									// 0x1004bc68
{
	&DrawPolyMgr::SkipDrawBufferPostprocess, &DrawPolyMgr::AddDynamicLightToDrawBuffer, &DrawPolyMgr::SkipDrawBufferPostprocess
};
static int s_FVFs[4] = { 0, 0x1c4, 0x2c4, 0x3c4 };						// 0x1004bc74 (by the number of stages)

// guess: queues pPoly in the pass list of pass iPass: the bucket is chosen by the pass's key function, a bucket entry (node from the
// ObjectBank) is found or created for the key, and the poly is linked into it
// FUNCTION: D3DREN 0x10029ecf
void DrawPolyMgr::QueuePolyForPass(WorldPoly *pPoly, int iPass)
{
	UnkType_DPMMaterialInstance *pInstance = m_pInstance;
	int iBucket;
	uint32 key;

	s_KeyFns[m_Passes[iPass].m_Unk18](pPoly, &iBucket, &key);

	LTLink *pBucket = &pInstance->m_Lists[iPass].m_Unk000[iBucket];
	for (LTLink *pLink = pBucket->m_pNext; pLink != pBucket; pLink = pLink->m_pNext)
	{
		UnkType_DPMNode *pNode = (UnkType_DPMNode *)pLink->m_pData;
		if (pNode->m_Unk0c == key)
		{
			pNode->m_Unk00.AddAfter((CheapLTLink *)pPoly);
			return;
		}
	}

	UnkType_DPMNode *pNode = m_Unk7d4.Allocate();
	if (pNode)
	{
		pNode->m_Unk0c = key;
		pBucket->AddAfter(&pNode->m_Unk10);
		pInstance->m_Lists[iPass].m_Unk188.AddAfter(&pNode->m_Unk1c);
		pNode->m_Unk00.AddAfter((CheapLTLink *)pPoly);
	}
}

// guess: sets the render states of pass pPass (alpha blending, the blend factors) and the texture stage states of its stages
// FUNCTION: D3DREN 0x10029f9a
void DrawPolyMgr::SetPassRenderStates(UnkType_DPMPass *pPass, UnkType_DPMPassList *pList)
{
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, pPass->m_Unk00);
	uint32 i;
	i = 0;
	if (pPass->m_Unk00)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, pList->m_SrcBlend);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, pList->m_DestBlend);
	}
	for (i = 0; i < pPass->m_nStages; i++)
	{
		UnkType_DPMStageStates &st = pList->m_Stages[i];
		g_pD3DDevice->SetTextureStageState(i, D3DTSS_COLOROP, st.m_ColorOp);
		if (st.m_ColorOp != D3DTOP_DISABLE)
		{
			g_pD3DDevice->SetTextureStageState(i, D3DTSS_COLORARG1, st.m_ColorArg1);
			g_pD3DDevice->SetTextureStageState(i, D3DTSS_COLORARG2, st.m_ColorArg2);
		}
		g_pD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAOP, st.m_AlphaOp);
		if (st.m_AlphaOp != D3DTOP_DISABLE)
		{
			g_pD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAARG1, st.m_AlphaArg1);
			g_pD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAARG2, st.m_AlphaArg2);
		}
	}
}

// FUNCTION: D3DREN 0x1002a084 _$E13
// FUNCTION: D3DREN 0x1002a089 _$E12
// GLOBAL: D3DREN 0x1006e800
ConVar g_CV_TestGouraud("TestGouraud", 0.0f);
// FUNCTION: D3DREN 0x1002a0a3 _$E16
// FUNCTION: D3DREN 0x1002a0a8 _$E15
// GLOBAL: D3DREN 0x1006e7e0
ConVar g_CV_TestLightmap("TestLightmap", 1.0f);


// guess: Flush
// Not matching: 22 aligned mismatches (18 ignoring stack offsets; was 228).  What made the frame and the register use the exe's:
// iNextPass as an if/else (the ?: form compiles branch-free and takes ebx from pPass), one iStage counter for both stage loops, and
// the pass list held in pList with the empty-list test on pList->m_Unk188 before pHead is taken.  Remaining: the exe keeps pNode in
// ecx at the node loop's join points (the Free(pNode) push goes through ecx), ours in eax/memory.
// Tried: next-link/node load order (24/22), a break + iStage test or flag instead of the goto (35/30 or same), pPoly before
// iNextPass, the first-poly link reused; two 3000-candidate permuter runs found nothing better.
// PARKED: register residue: the exe keeps pNode in ecx at the node loop's joins (Free's push via ecx), no source lever found
// STUB: D3DREN 0x1002a0c2
void DrawPolyMgr::FlushQueuedPolys()
{
	uint32 oldAlphaBlend, oldSrcBlend, oldDestBlend;
	uint32 iStage;

	m_Unk768 = g_pFrameRenderContext->m_CurFrameCode;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, (unsigned long *)&oldAlphaBlend);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_SRCBLEND, (unsigned long *)&oldSrcBlend);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_DESTBLEND, (unsigned long *)&oldDestBlend);

	// reconfigure the built-in material for the TestGouraud / TestLightmap console variables
	if (g_CV_TestGouraud.m_IntVal)
	{
		m_Passes[0].m_Unk00 = 0;
		m_Passes[0].m_Unk14 = 0;
		m_nPasses = 1;
		m_Passes[0].m_Unk10 = 1;
		m_Passes[0].m_Unk18 = 0;
		m_Passes[0].m_nStages = 1;
		m_Passes[0].m_Stages[0].m_Unk18 = 0;
		m_Passes[0].m_Stages[0].m_Unk1c = 0;
	}
	else if (g_CV_TestLightmap.m_IntVal)
	{
		m_nPasses = 2;
		m_Passes[0].m_Unk00 = 0;
		m_Passes[0].m_Unk10 = 0;
		m_Passes[0].m_Unk14 = 0;
		m_Passes[0].m_Unk18 = 0;
		m_Passes[0].m_nStages = 1;
		m_Passes[0].m_Stages[0].m_Unk18 = 0;
		m_Passes[0].m_Stages[0].m_Unk1c = 0;
		m_Passes[1].m_Unk00 = 1;
		m_Unk1cc.m_Lists[1].m_SrcBlend = D3DBLEND_DESTCOLOR;
		m_Unk1cc.m_Lists[1].m_DestBlend = D3DBLEND_SRCCOLOR;
		m_Passes[1].m_Unk10 = 0;
		m_Passes[1].m_Unk14 = 0;
		m_Passes[1].m_Unk18 = 1;
		m_Passes[1].m_nStages = 1;
		m_Passes[1].m_Stages[0].m_Unk18 = 1;
		m_Passes[1].m_Stages[0].m_Unk1c = 1;
	}
	else
	{
		m_Passes[0].m_Unk00 = 0;
		m_nPasses = 1;
		m_Passes[0].m_Unk10 = 0;
		m_Passes[0].m_Unk14 = 0;
		m_Passes[0].m_nStages = 2;
		m_Passes[0].m_Stages[0].m_Unk18 = 0;
		m_Passes[0].m_Stages[0].m_Unk1c = 0;
		m_Passes[0].m_Unk18 = 0;
		UnkType_DPMStageStates &st0 = m_Unk1cc.m_Lists[0].m_Stages[0];
		st0.m_ColorOp = D3DTOP_MODULATE;
		st0.m_ColorArg1 = D3DTA_TEXTURE;
		st0.m_ColorArg2 = D3DTA_DIFFUSE;
		st0.m_AlphaOp = D3DTOP_MODULATE;
		st0.m_AlphaArg1 = D3DTA_TEXTURE;
		st0.m_AlphaArg2 = D3DTA_DIFFUSE;
		m_Passes[0].m_Stages[1].m_Unk18 = 1;
		m_Passes[0].m_Stages[1].m_Unk1c = 1;
		UnkType_DPMStageStates &st1 = m_Unk1cc.m_Lists[0].m_Stages[1];
		st1.m_ColorOp = D3DTOP_MODULATE;
		st1.m_ColorArg1 = D3DTA_TEXTURE;
		st1.m_ColorArg2 = D3DTA_CURRENT;
		st1.m_AlphaOp = D3DTOP_MODULATE;
		st1.m_AlphaArg1 = D3DTA_TEXTURE;
		st1.m_AlphaArg2 = D3DTA_CURRENT;
	}

	m_Unk7d0 = 0;
	for (UnkType_DPMMaterialInstance *pInstance = m_Unk76c; pInstance; pInstance = pInstance->m_pNext)
	{
		Material *pMaterial = pInstance->m_pMaterial;
		for (uint32 iPass = 0; iPass < pMaterial->m_nPasses; iPass++)
		{
			UnkType_DPMPass *pPass = &pMaterial->m_Passes[iPass];
			UnkType_DPMPassList *pList = &pInstance->m_Lists[iPass];
			if (pList->m_Unk188.m_pNext == &pList->m_Unk188)
				break;
			LTLink *pHead = &pList->m_Unk188;

			SetPassRenderStates(pPass, pList);

			if (pPass->m_nStages == 1)
			{
				m_Unk7b8 = s_UVFns[pPass->m_Stages[0].m_Unk18];
			}
			else
			{
				for (iStage = 0; iStage < pPass->m_nStages; iStage++)
					(&m_Unk7ac)[iStage] = s_UVFns[pPass->m_Stages[iStage].m_Unk18];
				m_Unk7b8 = s_UVFnsByStages[pPass->m_nStages];
			}

			for (LTLink *pLink = pHead->m_pNext; pLink != pHead; )
			{
				UnkType_DPMNode *pNode = (UnkType_DPMNode *)pLink->m_pData;
				LTLink *pNextLink = pLink->m_pNext;
				LTLink *pPolyLink = pNode->m_Unk00.m_pNext;
				if (pPolyLink != (LTLink *)pNode)
				{
					{
						for (iStage = 0; iStage < pPass->m_nStages; iStage++)
						{
							if (!(this->*s_TextureSrcInitFns[pPass->m_Stages[iStage].m_Unk1c])((WorldPoly *)pPolyLink, iStage))
							{
								AddDebugMessage(1, "Material '%s', pass %d, stage %d: TextureSrcInitFn failed.", pMaterial, iPass, iStage);
								if (iPass == 0)
									DrawUntexturedBucket(pNode);
								goto Next;
							}
						}
					}

					int iNextPass;
					if (iPass == pMaterial->m_nPasses - 1)
						iNextPass = 0;
					else
						iNextPass = iPass + 1;
					LTLink *pPoly = pNode->m_Unk00.m_pNext;
					if (iPass == 0)
					{
						while (pPoly != (LTLink *)pNode)
						{
							LTLink *pNextPoly = pPoly->m_pNext;
							DrawPolyFirstPass((WorldPoly *)pPoly, pPass, iNextPass);
							pPoly = pNextPoly;
						}
					}
					else
					{
						while (pPoly != (LTLink *)pNode)
						{
							LTLink *pNextPoly = pPoly->m_pNext;
							DrawPolyAdditionalPass((WorldPoly *)pPoly, pPass, iNextPass);
							pPoly = pNextPoly;
						}
					}
				}
Next:
				m_Unk7d4.Free(pNode);
				pLink = pNextLink;
			}
		}
	}

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, oldAlphaBlend);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, oldSrcBlend);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, oldDestBlend);
}

// guess: TextureSrcInitFn failed for the first pass: draws the polygons of the bucket entry with the current states, untextured
// FUNCTION: D3DREN 0x1002a8bc
void DrawPolyMgr::DrawUntexturedBucket(UnkType_DPMNode *pNode)
{
	TLVertex aVerts[0x80];
	TLVertex *pVerts;
	int nVerts;

	d3d_UnsetTexture(0);
	LTLink *pLink = pNode->m_Unk00.m_pNext;
	if (pLink == (LTLink *)pNode)
		return;
	do
	{
		WorldPoly *pPoly = (WorldPoly *)pLink;
		UnkType_PolyVertex *pSrc;
		if (g_FixTJunc)
		{
			pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
			nVerts = pPoly->m_nExtraVertices;
		}
		else
		{
			pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
			nVerts = pPoly->m_nVertices;
		}

		if (nVerts > 0x80)
		{
			dsi_ConsolePrint("Error: vertex buffer overflow");
			break;
		}

		pVerts = aVerts;
		for (int i = 0; i < nVerts; i++)
		{
			pVerts[i].m_Vec = *pSrc[i].m_Vec;
			pVerts[i].color = (uint32)pPoly;
		}

		g_ClipFlags = 0x3f;
		if (d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
		pLink = pLink->m_pNext;
	} while (pLink != (LTLink *)pNode);
}

// guess: draws pPoly in the first pass of its material: builds the pre-transformed vertices (position, colour, fog, texture
// coordinates of the pass's stages), adds the dynamic lights, clips and projects them, binds the stage textures and draws the fan;
// when another pass follows it keeps the projected positions (QVerts) and queues the polygon for the next pass
// FUNCTION: D3DREN 0x1002a40f
void DrawPolyMgr::DrawPolyFirstPass(WorldPoly *pPoly, UnkType_DPMPass *pPass, int iNextPass)
{
	UnkType_DPMDrawBuffer buf;
	UnkType_PolyVertex *pSrc;
	TLVertex *pVerts;
	int i;
	uint32 j;
	UnkType_DPMColorFn pfnColor;
	UnkType_DPMFogFn pfnFog;

	buf.m_pPoly = pPoly;
	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		buf.m_nVertices = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
		buf.m_nVertices = pPoly->m_nVertices;
	}

	i = 0;
	buf.m_Unk14 = s_VertexSizes[pPass->m_nStages];
	pfnColor = s_ColorFns[pPass->m_Unk10];
	pfnFog = *s_FogFns[pPass->m_Unk14];
	pVerts = (TLVertex *)buf.m_Verts;
	for (; i < buf.m_nVertices; i++)
	{
		*(LTVector *)pVerts = *pSrc[i].m_Vec;
		(this->*pfnColor)(&pSrc[i], pVerts);
		pfnFog((LTVector *)pVerts, &pVerts->specular);
		(this->*m_Unk7b8)(&pSrc[i], &pVerts->tu, 0);
		pVerts = (TLVertex *)((uint8 *)pVerts + buf.m_Unk14);
	}

	buf.m_Unk04[0] = buf.m_Unk04[1] = buf.m_Unk04[2] = 0;
	if (WORLDPOLY_UNK30(pPoly) && pPass->m_Unk0c)
	{
		for (j = 0; j < pPass->m_nStages; j++)
			(this->*s_PostFns[pPass->m_Stages[j].m_Unk20])(&buf);
	}

	pVerts = (TLVertex *)buf.m_Verts;
	if (s_ClipFns[pPass->m_nStages](pPoly->m_Flags & 0x3f, &pVerts, &buf.m_nVertices))
	{
		for (j = 0; j < pPass->m_nStages; j++)
			(this->*s_BindFns[pPass->m_Stages[j].m_Unk1c])(pPoly, j, buf.m_Unk04[j]);

		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, s_FVFs[pPass->m_nStages], pVerts, buf.m_nVertices, 0);

		if (iNextPass)
		{
			if (m_Unk7bc.GetSize() - m_Unk7d0 >= (uint32)buf.m_nVertices || m_Unk7bc.NiceSetSize(m_Unk7bc.GetSize() + 0x100))
			{
			pPoly->m_Unk08 = (m_Unk7d0 << 16) | (buf.m_nVertices & 0xffff);
			if (pVerts == (TLVertex *)buf.m_Verts)
				pPoly->m_Flags &= 0xffc0;
			QVert *pQ = &m_Unk7bc[m_Unk7d0];
			for (int k = 0; k < buf.m_nVertices; k++)
			{
				pQ->pos = pVerts->m_Vec;
				pQ->w = pVerts->rhw;
				pQ++;
				pVerts = (TLVertex *)((uint8 *)pVerts + buf.m_Unk14);
			}
			m_Unk7d0 += buf.m_nVertices;
			QueuePolyForPass(pPoly, 1);
			}
		}
	}
}

// guess: draws pPoly in a later pass: the vertices are rebuilt from the QVerts the first pass kept (the polygon was already
// projected there), the colour, fog and texture coordinates of this pass are computed again, the textures bound and the fan drawn
// STUB diagnosis: 549 vs 553 bytes, 70 aligned mismatches ignoring stack offsets (97 with).  The vertex source is walked with
// pSrc++ (the exe's `add ebx, 0x18` / `add [ebp-0xc], 0x18`); the two QVert loops write through their own cursor pOut from aVerts
// (pVerts keeps aVerts / the clipper's output for DrawPrimitive, no reset), position and rhw copied as members (3 movsd + mov).
// Remaining: the exe keeps pPoly in ebx (the bind loop reuses the pPoly slot for the stage pointer), nQVerts shares its slot with the
// no-clip loop's pSrc, and the frame slot order of nQVerts / i / nVertices differs.  The queued-vertex range is read with
// LOWORD/HIWORD (the exe's `movzx` of the count and separate dword load of the start; the masked form loads once); with it pPass is in
// edi as in the exe.  The vertex buffer is 0x80 vertices of 0x28 bytes
// (0x1400): that gives the exe's frame 0x1424.  The exe copies `this` (`mov edx,ecx`) and indexes the three callback tables with absolute displacements in a
// different order, and the vertex source selection is scheduled differently; same control flow.  Permuter best 104 mismatches.
// STUB: D3DREN 0x1002a693
void DrawPolyMgr::DrawPolyAdditionalPass(WorldPoly *pPoly, UnkType_DPMPass *pPass, int iNextPass)
{
	uint8 aVerts[0x1400];
	UnkType_PolyVertex *pSrc;
	TLVertex *pVerts;
	int nVertices;
	int i;
	uint32 j;
	TLVertex *pOut;
	uint32 iStride = s_VertexSizes[pPass->m_nStages];
	UnkType_DPMColorFn pfnColor = s_ColorFns[pPass->m_Unk10];
	UnkType_DPMFogFn pfnFog = *s_FogFns[pPass->m_Unk14];

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVertices = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
		nVertices = pPoly->m_nVertices;
	}

	int nQVerts = LOWORD(pPoly->m_Unk08);
	QVert *pQ = &m_Unk7bc[HIWORD(pPoly->m_Unk08)];
	pVerts = (TLVertex *)aVerts;
	if (pPoly->m_Flags & 0x3f)
	{
		for (i = 0; i < nVertices; i++)
		{
			*(LTVector *)pVerts = *pSrc->m_Vec;
			(this->*pfnColor)(pSrc, pVerts);
			pfnFog((LTVector *)pVerts, &pVerts->specular);
			(this->*m_Unk7b8)(pSrc, &pVerts->tu, 0);
			pSrc++;
			pVerts = (TLVertex *)((uint8 *)pVerts + iStride);
		}

		pVerts = (TLVertex *)aVerts;
		if (!s_ClipFns[pPass->m_nStages](pPoly->m_Flags & 0x3f, &pVerts, &nVertices))
			return;
		if (nQVerts != nVertices)
		{
			dsi_ConsolePrint("DrawPolyMgr::DrawPolyAdditionalPass: nQVerts != nVertices");
			return;
		}

		pOut = (TLVertex *)aVerts;
		for (i = 0; i < nVertices; i++)
		{
			pOut->m_Vec = pQ->pos;
			pOut->rhw = pQ->w;
			pQ++;
			pOut = (TLVertex *)((uint8 *)pOut + iStride);
		}
	}
	else
	{
		pOut = (TLVertex *)aVerts;
		for (i = 0; i < nVertices; i++)
		{
			pOut->m_Vec = pQ->pos;
			pOut->rhw = pQ->w;
			(this->*pfnColor)(pSrc, pOut);
			pfnFog(pSrc->m_Vec, &pOut->specular);
			(this->*m_Unk7b8)(pSrc, &pOut->tu, 0);
			pQ++;
			pSrc++;
			pOut = (TLVertex *)((uint8 *)pOut + iStride);
		}
	}

	for (j = 0; j < pPass->m_nStages; j++)
		(this->*s_BindFns[pPass->m_Stages[j].m_Unk1c])(pPoly, j, 0);

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, s_FVFs[pPass->m_nStages], pVerts, nVertices, 0);

	if (iNextPass)
		QueuePolyForPass(pPoly, iNextPass);
}

// guess: the constructor and destructor of a bucket entry node (the exe's ObjectBank::Allocate / Free call them out of line): self-linked
// head and two links whose m_pData is the node; the destructor unlinks the two links
// FUNCTION: D3DREN 0x1002ad1b ??0UnkType_DPMNode@@QAE@XZ
UnkType_DPMNode::UnkType_DPMNode()
{
	dl_TieOff(&m_Unk00);
	dl_TieOff(&m_Unk10);
	m_Unk10.m_pData = this;
	dl_TieOff(&m_Unk1c);
	m_Unk1c.m_pData = this;
}

// FUNCTION: D3DREN 0x1002ad60 ??1UnkType_DPMNode@@QAE@XZ
UnkType_DPMNode::~UnkType_DPMNode()
{
	dl_Remove(&m_Unk10);
	dl_Remove(&m_Unk1c);
}

// ---- template and inline instances the object needs (CMoArray<QVert>, ObjectBank<UnkType_DPMNode>); emitted after the code above ----
// FUNCTION: D3DREN 0x1002a990 ??0?$CMoArray@UQVert@@VDefaultCache@@@@QAE@XZ
// FUNCTION: D3DREN 0x1002a9b2 ?GenGetNext@?$CMoArray@UQVert@@VDefaultCache@@@@UBE?AUQVert@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1002a9d4 ?GenAppend@?$CMoArray@UQVert@@VDefaultCache@@@@UAEHAAUQVert@@@Z
// FUNCTION: D3DREN 0x1002a9e9 ?GenRemoveAt@?$CMoArray@UQVert@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1002a9fa ?GenRemoveAll@?$CMoArray@UQVert@@VDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x1002aa07 ?GenCopyList@?$CMoArray@UQVert@@VDefaultCache@@@@UAEHABV?$GenList@UQVert@@@@@Z
// FUNCTION: D3DREN 0x1002aaba ?GenAppendList@?$CMoArray@UQVert@@VDefaultCache@@@@UAEHABV?$GenList@UQVert@@@@@Z
// FUNCTION: D3DREN 0x1002ab75 ?GenFindElement@?$CMoArray@UQVert@@VDefaultCache@@@@UBEHABUQVert@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1002ab9c ?AllocVoid@?$ObjectBank@UUnkType_DPMNode@@VNullCS@@@@UAEPAXXZ
// FUNCTION: D3DREN 0x1002aba1 ?FreeVoid@?$ObjectBank@UUnkType_DPMNode@@VNullCS@@@@UAEXPAX@Z
// FUNCTION: D3DREN 0x1002abad ?Init@?$CMoArray@UQVert@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1002abe2 ?SetSize2@?$CMoArray@UQVert@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1002ac44 ?InternalNiceSetSize@?$CMoArray@UQVert@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1002acff ?Allocate@?$ObjectBank@UUnkType_DPMNode@@VNullCS@@@@QAEPAUUnkType_DPMNode@@XZ
// FUNCTION: D3DREN 0x1002ad39 ?Free@?$ObjectBank@UUnkType_DPMNode@@VNullCS@@@@QAEXPAUUnkType_DPMNode@@@Z
// FUNCTION: D3DREN 0x1002ad92 ?Term@?$ObjectBank@UUnkType_DPMNode@@VNullCS@@@@UAEXXZ
// FUNCTION: D3DREN 0x1002adad ??_G?$ObjectBank@UUnkType_DPMNode@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: D3DREN 0x1002addd ?Insert2@?$CMoArray@UQVert@@VDefaultCache@@@@QAEHKABUQVert@@PAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1002aede ?Remove2@?$CMoArray@UQVert@@VDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1002afb9 ?BaseNew@@YAPAUQVert@@PAVLAlloc@@PAU1@K@Z
