// d3d.ren unk/100098d0 (0x100098d0-0x1000b349): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// world poly drawing (detail/lightmap batches), D3D7 state wrappers, StageStateSet COMDATs, polygon clip/project dispatchers
// 0x1000af16..0x1000b20c. 0x1000a2a7 matches only with the StageStateSet ctor/dtor calls kept out of line (Ob1-only in the
// scan; inline-budget effect, not a flag difference).
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/100098d0 (0x100098d0-0x1000f160, one packed /O1 region): world polygon drawing (lightmap / detail
// texture batches, the polygon clip and project dispatchers), Direct3D 7 texture binding and render-state wrappers, and
// Talon's setupmodel.cpp (ModelDraw: model lighting, LOD choice, vertex transform, the CMoArray instances it needs) with the
// SDK matrix / vector inlines that ended up out of line.
// FLAGS NOTE: the object map says /O1 /Ob2 (P object).  d3d_FlushWorldTextureBuckets matches only with /Ob1 (the exe keeps the StageStateSet ctor/dtor calls out of
// line, /Ob2 inlines them: an in-object inline-budget effect, not a flag difference); every other function matches under both, so the object is /O1.
// FLAGS: /O1
#define D3DREN_SETTEXTURE_EXTERN	// d3d_texture.h: the plain inline changes ClipPolyNear / ClipPolyLeft; keep calling the out-of-line copy
#include "d3dren/d3dstate.h"
#include "d3dren/lightmap.h"	// LightmapPage, WORLDPOLY_LMPAGE (d3d_SetLightmapTexture)
#include "d3dren/polydraw.h"
#include "d3dren/setupmodel.h"
#include "d3dren/staticlight.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "animtracker.h"
#include <math.h>

// The light members of RenderStruct (g_pStruct): include/renderstruct.h has m_GlobalLightDir at 0x138 and pads the colour at 0x144.
struct UnkType_RenderStructLightView
{
	uint8		m_Pad000[0x138];
	LTVector	m_GlobalLightDir;		// 0x138
	LTVector	m_GlobalLightColor;		// 0x144 NAME: Jupiter RenderStruct::m_GlobalLightColor (NAMING.md)
};
#define RENDERSTRUCT_LIGHTS		((UnkType_RenderStructLightView *)g_pStruct)


// ---- console variables of the world polygon code ----------------------------------------------------------------
// FUNCTION: D3DREN 0x100098d0 _$E2
// FUNCTION: D3DREN 0x100098d5 _$E1
// GLOBAL: D3DREN 0x10051448
ConVar g_CV_DetailTextures("DetailTextures", 1.0f);
// FUNCTION: D3DREN 0x100098ef _$E5
// FUNCTION: D3DREN 0x100098f4 _$E4
// GLOBAL: D3DREN 0x100513e8
ConVar g_CV_DetailTextureAdd("DetailTextureAdd", 1.0f);
// FUNCTION: D3DREN 0x1000990e _$E8
// FUNCTION: D3DREN 0x10009913 _$E7
// GLOBAL: D3DREN 0x100518b0
ConVar g_CV_DetailTextureAngle("DetailTextureAngle", 0.0f);
// FUNCTION: D3DREN 0x1000992d _$E11
// FUNCTION: D3DREN 0x10009932 _$E10
// GLOBAL: D3DREN 0x10051428
ConVar g_CV_FixSparkleys("FixSparkleys", 0.0f);
// FUNCTION: D3DREN 0x1000994c _$E14
// FUNCTION: D3DREN 0x10009951 _$E13
// GLOBAL: D3DREN 0x10051488
ConVar g_CV_LMFullBright("LMFullBright", 0.0f);
// FUNCTION: D3DREN 0x1000996b _$E17
// FUNCTION: D3DREN 0x10009970 _$E16
// GLOBAL: D3DREN 0x10051408
ConVar g_CV_EnvMapWorld("EnvMapWorld", 1.0f);
// FUNCTION: D3DREN 0x1000998a _$E20
// FUNCTION: D3DREN 0x1000998f _$E19
// GLOBAL: D3DREN 0x10051468
ConVar g_CV_LMAnim("LMAnim", 1.0f);

// ---- globals of the world polygon code (declarations: d3dren/polydraw.h, d3dren/d3dstate.h) ------------------------------------
// GLOBAL: D3DREN 0x100514a8
float g_WorldDetailTextureScale;
// GLOBAL: D3DREN 0x100518d0
float g_WorldDetailTextureAngleCos;
// GLOBAL: D3DREN 0x100513e0
float g_WorldDetailTextureAngleSin;
// GLOBAL: D3DREN 0x100528d4
LightmapPage *g_pQueuedLightmapPageHead;
// GLOBAL: D3DREN 0x100528d8
int g_bChromaKeyPass;

// guess: setter/getter pair for a global the draw code reads (name unknown).
// FUNCTION: D3DREN 0x100099a9
void d3d_SetChromaKeyPass(int nValue)
{
	g_bChromaKeyPass = nValue;
}

// FUNCTION: D3DREN 0x100099b3
int d3d_GetChromaKeyPass(void)
{
	return g_bChromaKeyPass;
}

// ---- world polygon drawing ------------------------------------------------------------------------------------------

// guess: draws one lightmapped world polygon: the 0x28-byte vertices carry the base coordinates and the lightmap coordinates;
// clipped and projected as the clip mask g_ClipFlags asks, then drawn as a triangle fan with the lightmap page (single pass
// when the device allows) and queued for the second pass / detail texture batches (g_pTexturedWorldPolyBuckets).
// Not matching: 1223 of 1223 bytes, 30 aligned mismatches ignoring stack offsets (424 vs 426 instructions).  The env-map tests are
// nested inside the clip / project tests, the vertex count is copied in both vertex-source arms, the chroma-key and normal paths
// each count the poly; an env-map poly (not projected above) goes through d3d_DrawClippedTriangleFan, the others are drawn directly.  Remaining: register
// allocation: the exe keeps nSurfFlags in edi and nTotalVerts in memory; ours swaps them (nSurfFlags declared before/after the
// env-map test, through a Surface local, int/uint types: no change), and the zero / pPoly registers follow from that.
// STUB: D3DREN 0x100099b9
void DrawLightmappedWorldPoly(WorldPoly *pPoly)
{
	UnkType_TLVertex40 aVerts[128];
	UnkType_TLVertex40 *pVerts;
	int i;
	int bNeedProject = 1;
	DWORD dwOldAlphaFunc;
	int bClipped = 0;
	int bLightmapTexture = 1;
	int bEnvMap;
	int nTotalVerts;
	UnkType_PolyVertex *pSrcVerts;
	DWORD dwOldAlphaBlend;
	TLVertex *pDest;
	TLVertex *pD;
	int nVerts;
	LPDIRECTDRAWSURFACE7 pOldTexture;
	DWORD dwOldZFunc;
	UnkType_TLVertex40 *pS;

	SharedTexture *pTexture;
	pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;
	bEnvMap = pTexture->m_eTexType != 0 && g_CV_EnvMapWorld.m_IntVal != 0 && g_bTwoTextureStageBlendValidated != 0;
	uint32 nSurfFlags = ((Surface *)pPoly->m_pSurface)->m_Flags & 0x100000;
	if (g_FixTJunc != 0)
	{
		pSrcVerts = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nTotalVerts = pPoly->m_nExtraVertices;
		nVerts = nTotalVerts;
	}
	else
	{
		pSrcVerts = (UnkType_PolyVertex *)(pPoly + 1);
		nTotalVerts = pPoly->m_nVertices;
		nVerts = nTotalVerts;
	}
	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}
	StateSet tssFogColor(D3DRENDERSTATE_FOGCOLOR, d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]));
	pVerts = aVerts;
	d3d_BuildDualTextureWorldVertices(nSurfFlags, aVerts, pSrcVerts, nVerts);
	if (g_ClipFlags != 0)
	{
		if (!bEnvMap)
		{
			if (!TransformClipProjectPolygon40(&pVerts, &nVerts, &g_ViewParams, 0))
				return;
			bClipped = pVerts != aVerts;
			bNeedProject = 0;
		}
	}
	if (g_CV_LMAnim.m_IntVal != 0)
		RelightWorldPolyIfNeeded(g_pFrameMainWorld, pPoly);
	int bFirst = WORLDPOLY_LMPAGE(pPoly)->m_Unk20 == 0;
	if (bFirst)
		WORLDPOLY_LMPAGE(pPoly)->m_Unk20 = 1;
	if (WORLDPOLY_UNK30(pPoly) != 0)
		bFirst = d3d_RefreshWorldPolyLightmap(pPoly, bFirst);
	else
		WORLDPOLY_LMPAGE(pPoly)->m_Unk20 = 1;
	if (bFirst != 0)
	{
		if (bClipped)
		{
			pVerts = aVerts;
			nVerts = nTotalVerts;
			d3d_BuildLightmappedWorldVertices(nSurfFlags, pPoly, aVerts, pSrcVerts, nTotalVerts);
			bNeedProject = 1;
			bLightmapTexture = 0;
		}
		else
		{
			d3d_UpdateWorldVertexLightmapUVs(pPoly, pVerts, pSrcVerts, nVerts);
			bLightmapTexture = 0;
		}
	}
	if (bNeedProject)
	{
		if (!bEnvMap)
		{
			if (!TransformClipProjectPolygon40(&pVerts, &nVerts, &g_ViewParams, 0))
				return;
		}
	}
	pDest = d3d_ReservePolyScratchVertices(nVerts);
	if (!pDest)
		return;
	if (g_bChromaKeyPass != 0)
	{
		if (nVerts != 0)
		{
			pD = pDest;
			i = nVerts;
			pS = pVerts;
			do
			{
				*pD = *(TLVertex *)pS;
				pD->tu = pS->tu;
				pD->tv = pS->tv;
				pD++;
				pS++;
				i--;
			} while (i != 0);
		}
		UnkType_PoolNode *pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
		pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
		pNode->m_Unk08 = nVerts;
		pNode->m_Unk0c = bLightmapTexture ? g_ClipFlags : 0;
		pNode->m_Unk14 = 3;
		if (bLightmapTexture)
		{
			if (d3d_SetLightmapTexture(pPoly, g_LightmapTextureStage))
				bLightmapTexture = 0;
		}
		if (!bLightmapTexture)
			g_pD3DDevice->GetTexture(g_LightmapTextureStage, &pOldTexture);
		d3d_FlushWorldTextureBuckets();
		if (!bLightmapTexture)
			g_pD3DDevice->SetTexture(g_LightmapTextureStage, pOldTexture);
		if (nVerts != 0)
		{
			pD = pDest;
			pS = pVerts;
			i = nVerts;
			do
			{
				pD->tu = pS->tu2;
				pD->tv = pS->tv2;
				pD++;
				pS++;
				i--;
			} while (i != 0);
		}
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &dwOldAlphaBlend);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHAFUNC, &dwOldAlphaFunc);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAFUNC, D3DCMP_ALWAYS);
		if (g_Saturate != 0)
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
		else
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZFUNC, &dwOldZFunc);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_EQUAL);
	}
	else
	{
		if (nVerts != 0)
		{
			pD = pDest;
			pS = pVerts;
			i = nVerts;
			do
			{
				*pD = *(TLVertex *)pS;
				pD->tu = pS->tu2;
				pD->tv = pS->tv2;
				pD++;
				pS++;
				i--;
			} while (i != 0);
		}
	}
	if (g_CV_LMFullBright.m_IntVal == 0)
	{
		if (bLightmapTexture)
		{
			if (!d3d_SetLightmapTexture(pPoly, g_LightmapTextureStage))
			{
				d3d_QueueWorldPoly(pPoly);
				return;
			}
		}
		if (bEnvMap)
			d3d_DrawClippedTriangleFan(pDest, nVerts, &g_ViewParams, 0x1c4);
		else
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pDest, nVerts, 0);
	}
	if (g_bChromaKeyPass != 0)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, dwOldAlphaBlend);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAFUNC, dwOldAlphaFunc);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZFUNC, dwOldZFunc);
		g_nWorldPolysDrawn++;
		g_nQueuedWorldPolyVertices += nVerts;
	}
	else
	{
		if (nVerts != 0)
		{
			pD = pDest;
			pS = pVerts;
			i = nVerts;
			do
			{
				pD->tu = pS->tu;
				pD->tv = pS->tv;
				pD++;
				pS++;
				i--;
			} while (i != 0);
		}
		UnkType_PoolNode *pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
		pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
		pNode->m_Unk08 = nVerts;
		pNode->m_Unk0c = g_ClipFlags;
		pNode->m_Unk14 = 3;
		if (g_CV_FixSparkleys.m_IntVal != 0)
			d3d_FlushWorldTextureBuckets();
		g_nWorldPolysDrawn++;
		g_nQueuedWorldPolyVertices += nVerts;
	}
}

// guess: if the poly's light-animation flag (bit 0x8000 of WorldPoly::m_Flags, part of WPF_RELIGHT) is set, clears it and
// relights its lightmap.
// FUNCTION: D3DREN 0x10009e80
void RelightWorldPolyIfNeeded(MainWorld *pWorld, WorldPoly *pPoly)
{
	if (pPoly->m_Flags & 0x8000)
	{
		pPoly->m_Flags &= 0x7fff;
		UpdatePolyAnimatedLightmap(pWorld, pPoly, 0);
	}
}

// ---- texture binding -----------------------------------------------------------------------------------------------

// guess: binds the texture data of pTex on device stage nStage unless it is already there; counts the bytes touched this frame.
// (The `return 1` inside the inner block and at the end is what keeps both callee-saved registers pushed at entry and the
// single epilogue of the exe: a plain `else` block or a bool-like return gives the shrink-wrapped or the setne form.)
// FUNCTION: D3DREN 0x10009ea5
int d3d_SetLightmapTexture(WorldPoly *pPoly, int nStage)
{
	LightmapPage *pPage = WORLDPOLY_LMPAGE(pPoly);
	if (pPage == 0)
		return 0;
	if (pPage != g_pBoundTextures[nStage] && pPage->m_Unk20)
	{
		if (pPage->m_Unk18 != g_CurFrameCode)
		{
			// RenderStruct bytes 0x48-0x6f are unnamed in include/renderstruct.h: +0x4c is a per-frame texture byte counter.
			g_pStruct->m_Unk4c += pPage->m_nMemoryUse;
			pPage->m_Unk18 = g_CurFrameCode;
		}
		g_pD3DDevice->SetTexture(nStage, pPage->m_pSurface);
		g_pBoundTextures[nStage] = pPage;
		return 1;
	}
	return 1;
}

// guess: sets the first texture coordinates of a vertex (the arguments stay on the x87 stack: `fld v; fld u; fstp tu; fstp tv`).
static inline void SetUV(UnkType_TLVertex40 *pVertex, float u, float v)
{
	pVertex->tu = u;
	pVertex->tv = v;
}

static inline void SetUV(TLVertex *pVertex, float u, float v)
{
	pVertex->tu = u;
	pVertex->tv = v;
}

// guess: fills nVerts 0x28-byte vertices from the polygon's vertices: position, the current colour, the fog alpha callback,
// the texture coordinates and the second (lightmap) coordinates.
// FUNCTION: D3DREN 0x10009f0b
void d3d_BuildDualTextureWorldVertices(uint32 nFlags, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts)
{
	for (int i = 0; i < nVerts; i++)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->color = g_GlobalVertexTintColor.color;
		(*g_pfnCalcFogAlpha)(&pDest->m_Vec, &pDest->specular);
		SetUV(pDest, pSrc->m_U, pSrc->m_V);
		pDest->tu2 = pSrc->m_Unk0c;
		pDest->tv2 = pSrc->m_Unk10;
		pDest++;
		pSrc++;
	}
}

// guess: like d3d_BuildDualTextureWorldVertices, but the second texture coordinates are derived from the lightmap plane vectors and the poly's
// lightmap origin (WorldPoly +0x38): ((P . (v - origin)) / grid + 0.5) * lightmap scale.
// Source shape: the first dot product only comes out in the exe's order when it is written right-associated, `z + (y + x)`
// (left-associated terms give the load of g_pFrameMainWorld and the y/z product in the wrong places); the second one is plain.
// FUNCTION: D3DREN 0x10009f6c
void d3d_BuildLightmappedWorldVertices(uint32 nFlags, WorldPoly *pPoly, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts)
{
	LTVector P, Q;
	SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);
	for (int i = 0; i < nVerts; i++)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->color = g_GlobalVertexTintColor.color;
		(*g_pfnCalcFogAlpha)(&pDest->m_Vec, &pDest->specular);
		LTVector *pVec = pSrc->m_Vec;
		float dx = pVec->x - pPoly->m_Unknown38.x;
		float dy = pVec->y - pPoly->m_Unknown38.y;
		float dz = pVec->z - pPoly->m_Unknown38.z;
		SetUV(pDest, pSrc->m_U, pSrc->m_V);
		pDest->tu2 = ((P.z * dz + (P.y * dy + P.x * dx)) / g_pFrameMainWorld->m_LMGridSize + 0.5f) * g_TextureStageTexelSizes[0].m_Unk00;
		pDest->tv2 = ((Q.x * dx + Q.y * dy + Q.z * dz) / g_pFrameMainWorld->m_LMGridSize + 0.5f) * g_TextureStageTexelSizes[0].m_Unk04;
		pSrc++;
		pDest++;
	}
}

// guess: rewrites only the second texture coordinates of nVerts existing 0x28-byte vertices.
// Source shape: the first sum needs a per-vertex local copy of P.z (`float fPz = P.z; ... fPz * dz`); with P.z read in place the
// exe's order of the load of g_pFrameMainWorld inside the sum is not produced (found with the permuter, minimised by hand).
// FUNCTION: D3DREN 0x1000a066
void d3d_UpdateWorldVertexLightmapUVs(WorldPoly *pPoly, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts)
{
	LTVector P, Q;
	SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);
	float fHalf = 0.5f;
	for (int i = 0; i < nVerts; i++)
	{
		LTVector vOrigin = pPoly->m_Unknown38;
		float dx = pSrc->m_Vec->x - vOrigin.x;
		float dy = pSrc->m_Vec->y - vOrigin.y;
		float dz = pSrc->m_Vec->z - vOrigin.z;
		float fPz = P.z;
		pDest->tu2 = ((P.x * dx + P.y * dy + fPz * dz) / g_pFrameMainWorld->m_LMGridSize + fHalf) * g_TextureStageTexelSizes[0].m_Unk00;
		pDest->tv2 = ((Q.x * dx + Q.y * dy + Q.z * dz) / g_pFrameMainWorld->m_LMGridSize + fHalf) * g_TextureStageTexelSizes[0].m_Unk04;
		pSrc++;
		pDest++;
	}
}

// guess: returns room for nVerts more vertices of the scratch array (growing it when needed), 0 when that fails.
// FUNCTION: D3DREN 0x1000a134
TLVertex *d3d_ReservePolyScratchVertices(int nVerts)
{
	if (g_nQueuedWorldPolyVertices + nVerts > g_nQueuedWorldPolyVertexCapacity)
	{
		if (!d3d_GrowTLVertexBuffer(g_nQueuedWorldPolyVertexCapacity + 0x5dc + nVerts))
			return 0;
	}
	return g_pQueuedWorldPolyVertices + g_nQueuedWorldPolyVertices;
}

// guess: draws the poly now when it has a lightmap, else queues it under its lightmap record (to be drawn by the flush).
// FUNCTION: D3DREN 0x1000a16b
void d3d_DrawOrQueueLightmappedWorldPoly(WorldPoly *pPoly)
{
	if (WORLDPOLY_UNK30(pPoly))
		DrawLightmappedWorldPoly(pPoly);
	else
	{
		LightmapPage *pPage = WORLDPOLY_LMPAGE(pPoly);
		if (pPage)
		{
			UnkType_PoolNode *pNode = (UnkType_PoolNode *)sb_Allocate(&g_WorldPolyNodeBank);
			if (pNode)
			{
				if (!LMPAGE_QUEUE(pPage))
				{
					LMPAGE_NEXT(pPage) = g_pQueuedLightmapPageHead;
					g_pQueuedLightmapPageHead = pPage;
				}
				uint32 nState = g_ClipFlags;
				pNode->m_Unk00 = pPoly;
				pNode->m_Unk0c = nState;
				pNode->m_Unk10 = LMPAGE_QUEUE(pPage);
				LMPAGE_QUEUE(pPage) = pNode;
			}
		}
	}
}

// ---- second texture stage setup ------------------------------------------------------------------------------------

// guess: stage 1 is the detail texture stage: mode 1 = modulate (or add-signed when "DetailTextureAdd" is set) the
// colour and select the alpha of the first stage; mode 2 = modulate alpha + add colour.
// FUNCTION: D3DREN 0x1000a1c2
void d3d_SetEnvMapTextureStates(int nMode)
{
	switch (nMode)
	{
	case 1:
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, g_CV_DetailTextureAdd.m_IntVal ? D3DTOP_ADDSIGNED : D3DTOP_MODULATE);
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		break;
	case 2:
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATEALPHA_ADDCOLOR);
		break;
	}
}

// FUNCTION: D3DREN 0x1000a211
void d3d_UnsetEnvMapTextureStates(void)
{
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

// FUNCTION: D3DREN 0x1000a23a
void d3d_SetDetailTextureStates(void)
{
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, g_CV_DetailTextureAdd.m_IntVal ? D3DTOP_ADDSIGNED : D3DTOP_MODULATE);
}

// FUNCTION: D3DREN 0x1000a25e
void d3d_UnsetDetailTexture(void)
{
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	d3d_UnsetTexture(1);
}

// The exe's out-of-line copy of the inline d3d_DisableTexture (d3d_texture.h), called by d3d_FullDrawScene (0x10014a40); every other caller expands
// or calls the inline.  Same body as the inline (a wrapper around the inline does not match: SIZE), kept as its own definition because the call in
// d3d_FullDrawScene must stay out of line.
// FUNCTION: D3DREN 0x1000a27b
void d3d_UnsetTexture(int nStage)
{
	if (g_pBoundTextures[nStage])
	{
		g_pD3DDevice->SetTexture(nStage, 0);
		g_pBoundTextures[nStage] = 0;
	}
}

// guess: draws every texture bucket of the queue g_pTexturedWorldPolyBuckets (the polys that were queued per texture): buckets whose texture
// has a lightmap are drawn with the multiply blend, then the others / the second pass with the alpha blend; the queue and the
// vertex scratch array are emptied.
// FUNCTION: D3DREN 0x1000a2a7
void d3d_FlushWorldTextureBuckets(void)
{
	StageStateSet tssColorArg1(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	StageStateSet tssColorArg2(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	DWORD dwOldAlphaBlend;
	if (g_bChromaKeyPass == 0 && g_CV_LMFullBright.m_IntVal == 0)
	{
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &dwOldAlphaBlend);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
		if (g_Saturate != 0)
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
		else
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
	}
	StateSet tssFogColor(D3DRENDERSTATE_FOGCOLOR, d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]));
	UnkType_PoolBucket *pSecondPass = 0;
	UnkType_PoolBucket *pBucket = g_pTexturedWorldPolyBuckets;
	while (pBucket)
	{
		UnkType_PoolBucket *pNextBucket = pBucket->m_Unk08;
		if (g_LightmapsOnly != 0)
			d3d_FreeWorldPolyQueue(pBucket->m_Unk04);
		else if (pBucket->m_Unk04 != 0)
		{
			SharedTexture *pTexture = ((Surface *)((WorldPoly *)pBucket->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture;
			if (d3d_EnsureTextureAndGetFlags(pTexture, 0) != 0 && g_bChromaKeyPass == 0 && g_CV_LMFullBright.m_IntVal == 0)
			{
				if (g_Saturate != 0)
					d3d_DrawWorldTextureBucket(pBucket, pTexture, 0, 0);
				pBucket->m_Unk08 = pSecondPass;
				pSecondPass = pBucket;
				pBucket = pNextBucket;
				continue;
			}
			d3d_DrawWorldTextureBucket(pBucket, pTexture, 1, 0);
		}
		((UnkType_BucketOwner *)pBucket->m_Unk00)->m_Unk1c[0] = 0;
		sb_Free(&g_WorldPolyBucketBank, pBucket);
		pBucket = pNextBucket;
	}
	if (g_bChromaKeyPass == 0 && g_CV_LMFullBright.m_IntVal == 0)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
		if (g_Saturate != 0)
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
		pBucket = pSecondPass;
		while (pBucket)
		{
			UnkType_PoolBucket *pNextBucket = pBucket->m_Unk08;
			d3d_DrawWorldTextureBucket(pBucket, ((Surface *)((WorldPoly *)pBucket->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture, 1, g_Saturate);
			((UnkType_BucketOwner *)pBucket->m_Unk00)->m_Unk1c[0] = 0;
			sb_Free(&g_WorldPolyBucketBank, pBucket);
			pBucket = pNextBucket;
		}
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, dwOldAlphaBlend);
	}
	g_pTexturedWorldPolyBuckets = 0;
	g_nQueuedWorldPolyVertices = 0;
}

// NAME: StageStateSet: Jupiter render_a/src/sys/d3d/d3d_draw.h class StageStateSet (m_Stage, m_State, m_OldVal;
// constructor Get+Set, destructor restores).  The d3d.ren constructor (FUN_1000a4dc) sets the state unconditionally,
// Jupiter's skips the Set when the old value equals the new one.
// FUNCTION: D3DREN 0x1000a4dc ??0StageStateSet@@QAE@KW4_D3DTEXTURESTAGESTATETYPE@@K@Z
StageStateSet::StageStateSet(uint32 stage, D3DTEXTURESTAGESTATETYPE state, uint32 val)
{
	m_Stage = stage;
	m_State = state;
	g_pD3DDevice->GetTextureStageState(m_Stage, m_State, (unsigned long *)&m_OldVal);
	g_pD3DDevice->SetTextureStageState(m_Stage, m_State, val);
}

// FUNCTION: D3DREN 0x1000a521 ??1StageStateSet@@QAE@XZ
StageStateSet::~StageStateSet()
{
	g_pD3DDevice->SetTextureStageState(m_Stage, m_State, m_OldVal);
}

// guess: draws the polys of one texture bucket: with the second (detail / environment map) texture stage when the device can
// blend two textures and the texture has one, else with a single texture.  a3 / a4: flags for the batch drawers.
// FUNCTION: D3DREN 0x1000a538
void d3d_DrawWorldTextureBucket(UnkType_PoolBucket *pBucket, SharedTexture *pTexture, int a3, int a4)
{
	StateSet tssFogColor(D3DRENDERSTATE_FOGCOLOR, d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]));
	if (g_bTwoTextureStageBlendValidated != 0 && pTexture->m_eTexType != 0 && g_CV_EnvMapWorld.m_IntVal != 0
		&& d3d_SetTexture(pTexture->m_pLinkedTexture, 0, 0))
	{
		d3d_SetTexture(pTexture, 1, 0);
		g_TextureStateRestorer.RestoreAllStates();
		if (pTexture->m_pStateChange)
			g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, 1);
		g_WorldDetailTextureScale = g_CV_DetailTextureScale.m_FloatVal;
		d3d_SetEnvMapTextureStates(pTexture->m_eTexType);
		d3d_DrawDualTextureWorldBucket(pBucket, a3, a4);
		d3d_UnsetEnvMapTextureStates();
	}
	else
	{
		d3d_SetTexture(pTexture, 0, 0);
		if (pTexture->m_pStateChange)
			g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, 0);
		if (g_bTwoTextureStageBlendValidated != 0 && g_CV_DetailTextures.m_IntVal != 0 && g_pBoundTextures[0] != 0
			&& pTexture->m_pLinkedTexture != 0 && pTexture->m_eTexType == 0
			&& d3d_SetTexture(pTexture->m_pLinkedTexture, 1, 0))
		{
			g_WorldDetailTextureScale = g_CV_DetailTextureScale.m_FloatVal;
			if (((RTextureBase *)g_pBoundTextures[0])->IsRTexture() == 1)
			{
				g_WorldDetailTextureScale = g_WorldDetailTextureScale * ((UnkType_RTextureData *)g_pBoundTextures[0])->m_pOwner->m_DetailTextureScale;
				g_WorldDetailTextureAngleCos = ((UnkType_RTextureData *)g_pBoundTextures[0])->m_pOwner->m_DetailTextureAngleC;
				g_WorldDetailTextureAngleSin = ((UnkType_RTextureData *)g_pBoundTextures[0])->m_pOwner->m_DetailTextureAngleS;
			}
			else
			{
				g_WorldDetailTextureAngleCos = 1.0f;
				g_WorldDetailTextureAngleSin = 0.0f;
			}
			d3d_SetDetailTextureStates();
			d3d_DrawDualTextureWorldBucket(pBucket, a3, a4);
			d3d_UnsetDetailTexture();
		}
		else
			d3d_DrawSingleTextureWorldBucket(pBucket, a3, a4);
	}
	g_TextureStateRestorer.RestoreAllStates();
}

// guess: draws the polys of one texture bucket with a single texture (the base texture bound by the caller): one triangle fan
// per queued poly from the scratch array, switching alpha blending, the fog colour and the blend factor as the poly's flags
// (+0x14: bit 0 blended, bit 1 saturate, bit 2 keeps its vertex colour) ask; the lightmap coordinates are scaled by the stage
// scale unless a3.  a2 gives the nodes back to the pool.
// FUNCTION: D3DREN 0x1000a70d
void d3d_DrawSingleTextureWorldBucket(UnkType_PoolBucket *pBucket, int a2, int a3)
{
	int bAlphaBlend = 1;
	int bSaturate = 1;
	UnkType_PoolNode *pNode = pBucket->m_Unk04;
	if (pNode)
	{
		do
		{
			UnkType_PoolNode *pNext = pNode->m_Unk10;
			if (bAlphaBlend != (pNode->m_Unk14 & 1))
			{
				bAlphaBlend = pNode->m_Unk14 & 1;
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, bAlphaBlend);
				if (bAlphaBlend == 0)
				{
					uint32 dwFogColor = d3d_PackRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]);
					g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);
				}
				else
				{
					uint32 dwFogColor = d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]);
					g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);
				}
			}
			if (bAlphaBlend != 0)
			{
				if (bSaturate != ((pNode->m_Unk14 >> 1) & 1))
				{
					bSaturate = (pNode->m_Unk14 >> 1) & 1;
					if (g_Saturate != 0 && bSaturate != 0)
						g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
					else
						g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
				}
			}
			int bKeepColor = (pNode->m_Unk14 >> 2) & 1;
			TLVertex *pVerts = g_pQueuedWorldPolyVertices + pNode->m_Unk04;
			if (a3 == 0)
			{
				TLVertex *pVert = pVerts;
				int i = pNode->m_Unk08;
				while (i--)
				{
					if (bKeepColor == 0)
						pVert->color = 0xffffffff;
					SetUV(pVert, g_TextureStageTexelSizes[0].m_Unk00 * pVert->tu, g_TextureStageTexelSizes[0].m_Unk04 * pVert->tv);
					pVert++;
				}
			}
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, pNode->m_Unk08, 0);
			if (a2 != 0)
				sb_Free(&g_WorldPolyNodeBank, pNode);
			pNode = pNext;
		} while (pNode);
		if (bAlphaBlend == 0 || bSaturate == 0)
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			uint32 dwFogColor = d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);
			if (g_Saturate != 0)
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
			else
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
		}
	}
}

// guess: draws the polys of one texture bucket with the detail / environment map texture on stage 1: the second texture
// coordinates are the (rotated and scaled) detail coordinates of the poly, or, for an environment map texture, the sphere map
// coordinates computed from the viewer position; each poly is clipped and drawn as a 0x28-byte vertex fan.  a2 gives the nodes
// back to the pool; the third argument is not used.
// Codegen notes: the detail scale is a float[2] (u stays on the x87 stack, its slot stays unused, v's slot is reused for pNext);
// nVerts is one function-scope count shared by both branches; the queued-vertex loop counts its own copy (read first, then
// copied into nVerts); the keep-colour branch comes first and both vertex-source arms set pSrc before the count.
// FUNCTION: D3DREN 0x1000a8c0
void d3d_DrawDualTextureWorldBucket(UnkType_PoolBucket *pBucket, int a2, int a3)
{
	UnkType_TLVertex40 aVerts[80];
	float aTexMat[2][2];
	float aScale[2];
	int nVerts;
	aScale[0] = g_TextureStageTexelSizes[1].m_Unk00 * g_WorldDetailTextureScale;
	aScale[1] = g_TextureStageTexelSizes[1].m_Unk04 * g_WorldDetailTextureScale;
	aTexMat[0][0] = g_WorldDetailTextureAngleCos * aScale[0];
	aTexMat[0][1] = -(g_WorldDetailTextureAngleSin * aScale[1]);
	aTexMat[1][0] = g_WorldDetailTextureAngleSin * aScale[0];
	aTexMat[1][1] = g_WorldDetailTextureAngleCos * aScale[1];
	int bAlphaBlend = 1;
	int bSaturate = 1;
	UnkType_PoolNode *pNode = pBucket->m_Unk04;
	if (pNode)
	{
		do
		{
			UnkType_PoolNode *pNext = pNode->m_Unk10;
			WorldPoly *pPoly = (WorldPoly *)pNode->m_Unk00;
			if (bAlphaBlend != (pNode->m_Unk14 & 1))
			{
				bAlphaBlend = pNode->m_Unk14 & 1;
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, bAlphaBlend);
				if (bAlphaBlend == 0)
				{
					uint32 dwFogColor = d3d_PackRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]);
					g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);
				}
				else
				{
					uint32 dwFogColor = d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]);
					g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);
				}
			}
			if (bAlphaBlend != 0)
			{
				if (bSaturate != ((pNode->m_Unk14 >> 1) & 1))
				{
					bSaturate = (pNode->m_Unk14 >> 1) & 1;
					if (g_Saturate != 0 && bSaturate != 0)
						g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
					else
						g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
				}
			}
			int bKeepColor = (pNode->m_Unk14 >> 2) & 1;
			if (((Surface *)pPoly->m_pSurface)->m_pTexture->m_eTexType == 0 || g_CV_EnvMapWorld.m_IntVal == 0)
			{
				TLVertex *pSrc = g_pQueuedWorldPolyVertices + pNode->m_Unk04;
				UnkType_TLVertex40 *pDest = aVerts;
				int i = pNode->m_Unk08;
				nVerts = i;
				while (i--)
				{
					*(TLVertex *)pDest = *pSrc;
					if (bKeepColor == 0)
						pDest->color = 0xffffffff;
					SetUV(pDest, g_TextureStageTexelSizes[0].m_Unk00 * pSrc->tu, g_TextureStageTexelSizes[0].m_Unk04 * pSrc->tv);
					pDest->tu2 = aTexMat[0][0] * pSrc->tu + aTexMat[0][1] * pSrc->tv;
					pDest->tv2 = aTexMat[1][0] * pSrc->tu + aTexMat[1][1] * pSrc->tv;
					pSrc++;
					pDest++;
				}
				g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x2c4, aVerts, nVerts, 0);
			}
			else
			{
				UnkType_PolyVertex *pSrc;
				if (g_FixTJunc != 0)
				{
					pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
					nVerts = pPoly->m_nExtraVertices;
				}
				else
				{
					pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
					nVerts = pPoly->m_nVertices;
				}
				UnkType_TLVertex40 *pDest = aVerts;
				for (int i = nVerts; i != 0; i--)
				{
					pDest->m_Vec.x = pSrc->m_Vec->x;
					pDest->m_Vec.y = pSrc->m_Vec->y;
					pDest->m_Vec.z = pSrc->m_Vec->z;
					if (bKeepColor)
					{
						pDest->rgb.r = g_ByteMultiplyTable.m_Unk00[g_GlobalVertexTintColor.rgb.r + pSrc->m_Color[2] * 256];
						pDest->rgb.g = g_ByteMultiplyTable.m_Unk00[g_GlobalVertexTintColor.rgb.g + pSrc->m_Color[1] * 256];
						pDest->rgb.b = g_ByteMultiplyTable.m_Unk00[g_GlobalVertexTintColor.rgb.b + pSrc->m_Color[0] * 256];
						pDest->rgb.a = g_ByteMultiplyTable.m_Unk00[g_GlobalVertexTintColor.rgb.a + pSrc->m_Color[3] * 256];
					}
					else
						pDest->color = g_GlobalVertexTintColor.color;
					pDest->tu2 = g_TextureStageTexelSizes[1].m_Unk00 * pSrc->m_U;
					pDest->tv2 = g_TextureStageTexelSizes[1].m_Unk04 * pSrc->m_V;
					(*g_pfnCalcFogAlpha)(&pDest->m_Vec, &pDest->specular);
					d3d_CalcWorldReflectionUVs((LTVector *)&g_ViewParams.m_Pos, pSrc->m_Vec, &pPoly->m_pPlane->m_Normal, &pDest->tu, &pDest->tv);
					pDest->tu = g_WorldDetailTextureScale * pDest->tu;
					pDest->tv = g_WorldDetailTextureScale * pDest->tv;
					pDest++;
					pSrc++;
				}
				g_ClipFlags = pNode->m_Unk0c;
				d3d_DrawClippedDualTextureTriangleFan(aVerts, nVerts, &g_ViewParams, 0x2c4);
			}
			if (a2 != 0)
				sb_Free(&g_WorldPolyNodeBank, pNode);
			pNode = pNext;
		} while (pNode);
		if (bAlphaBlend == 0 || bSaturate == 0)
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			uint32 dwFogColor = d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);
			if (g_Saturate != 0)
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
			else
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
		}
	}
}

// guess: draws the queued lightmap polys if there are any.
// FUNCTION: D3DREN 0x1000ac7b
void d3d_FlushPendingWorldTextureBuckets(void)
{
	if (g_pTexturedWorldPolyBuckets)
		d3d_FlushWorldTextureBuckets();
}

// guess: draws the polys that were queued per lightmap page (g_pQueuedLightmapPageHead) with DrawLightmappedWorldPoly, frees their nodes and empties
// the queue; the fog colour is set around the loop.
// FUNCTION: D3DREN 0x1000ac8a
void d3d_FlushLightmapPageQueues(void)
{
	StateSet tssFogColor(D3DRENDERSTATE_FOGCOLOR, d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]));
	LightmapPage *pPage = g_pQueuedLightmapPageHead;
	while (pPage)
	{
		LightmapPage *pNextPage = LMPAGE_NEXT(pPage);
		UnkType_PoolNode *pNode = LMPAGE_QUEUE(pPage);
		while (pNode)
		{
			UnkType_PoolNode *pNext = pNode->m_Unk10;
			g_ClipFlags = pNode->m_Unk0c;
			DrawLightmappedWorldPoly((WorldPoly *)pNode->m_Unk00);
			sb_Free(&g_WorldPolyNodeBank, pNode);
			pNode = pNext;
		}
		LMPAGE_NEXT(pPage) = 0;
		LMPAGE_QUEUE(pPage) = 0;
		pPage = pNextPage;
	}
	g_pQueuedLightmapPageHead = 0;
}

// guess: draws the polygon pVerts (nVerts 0x20-byte vertices, vertex format nFVF) as a triangle fan: projected at once when it
// needs no clipping (g_ClipFlags == 0, or with "UseD3DClip" set and the near plane not involved), else as one projected and
// clipped triangle per fan triangle.
// FUNCTION: D3DREN 0x1000ad48
void d3d_DrawClippedTriangleFan(TLVertex *pVerts, int nVerts, ViewParams *pParams, uint32 nFVF)
{
	TLVertex aTri[32];
	TLVertex *pTri;
	TLVertex *pTriVerts;
	int nTriVerts;

	if (g_ClipFlags == 0 || (g_CV_UseD3DClip.m_IntVal && (g_ClipFlags & 1) == 0))
	{
		if (d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, pParams, 0))
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, nFVF, pVerts, nVerts, 0);
	}
	else if (nVerts > 2)
	{
		pTri = pVerts;
		do
		{
			aTri[0] = pVerts[0];
			aTri[1] = pTri[1];
			aTri[2] = pTri[2];
			nTriVerts = 3;
			pTriVerts = aTri;
			if (d3d_ClipAndProjectTLVertices(&pTriVerts, &nTriVerts, pParams, 0))
				g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, nFVF, pTriVerts, nTriVerts, 0);
			pTri++;
			nVerts--;
		} while (nVerts > 2);
	}
}

// guess: the 0x28-byte twin of d3d_DrawClippedTriangleFan (the vertices carry the second texture coordinates).
// FUNCTION: D3DREN 0x1000ae2f
void d3d_DrawClippedDualTextureTriangleFan(UnkType_TLVertex40 *pVerts, int nVerts, ViewParams *pParams, uint32 nFVF)
{
	UnkType_TLVertex40 aTri[32];
	UnkType_TLVertex40 *pTri;
	UnkType_TLVertex40 *pTriVerts;
	int nTriVerts;

	if (g_ClipFlags == 0 || (g_CV_UseD3DClip.m_IntVal && (g_ClipFlags & 1) == 0))
	{
		if (TransformClipProjectPolygon40(&pVerts, &nVerts, pParams, 0))
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, nFVF, pVerts, nVerts, 0);
	}
	else if (nVerts > 2)
	{
		pTri = pVerts;
		do
		{
			aTri[0] = pVerts[0];
			aTri[1] = pTri[1];
			aTri[2] = pTri[2];
			nTriVerts = 3;
			pTriVerts = aTri;
			if (TransformClipProjectPolygon40(&pTriVerts, &nTriVerts, pParams, 0))
				g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, nFVF, pTriVerts, nTriVerts, 0);
			pTri++;
			nVerts--;
		} while (nVerts > 2);
	}
}

// guess: projects the 0x20-byte vertices *ppVerts (*pnVerts of them) to the screen: without clip planes (g_ClipFlags == 0)
// through the view * projection matrix (rhw = 1 / w); else to camera space, clipped (ClipPoly) and then projected
// (ProjectVertexToScreen).  Returns 0 when the clipping leaves nothing.  0x28-byte twin: TransformClipProjectPolygon40.  The fourth argument is not used
// (every caller passes 0, as for TransformClipProjectPolygon40).
// FUNCTION: D3DREN 0x1000af16
int d3d_ClipAndProjectTLVertices(TLVertex **ppVerts, int *pnVerts, ViewParams *pParams, int param_4)
{
	TLVertex *pVert;
	int i;

	if (g_ClipFlags == 0)
	{
		pVert = *ppVerts;
		for (i = *pnVerts; i != 0; i--)
		{
			pVert->rhw = MatVMul_InPlace_H(&pParams->m_FullTransform, &pVert->m_Vec);
			pVert++;
		}
	}
	else
	{
		pVert = *ppVerts;
		for (i = *pnVerts; i != 0; i--)
		{
			TransformPositionInPlace(&pVert->m_Vec.x, &pParams->m_mClipTransform.m[0][0]);
			pVert++;
		}
		if (!ClipPoly(g_ClipFlags, ppVerts, pnVerts))
			return 0;
		pVert = *ppVerts;
		for (i = *pnVerts; i != 0; i--)
		{
			ProjectVertexToScreen(&pVert->m_Vec.x, pParams);
			pVert++;
		}
	}
	return 1;
}

// guess: clips the polygon *ppVerts (*pnVerts vertices) against the planes of the mask nFlags (1 near, 4 left, 8, 0x10, 0x20,
// 2 = the 0x20-byte vertex clippers); the result replaces *ppVerts / *pnVerts.  Returns 0 when nothing is left.  With the
// "UseD3DClip" console variable set only the near plane is clipped here.  The six `char` locals are the unused first
// arguments of the plane clippers (all six exist separately in the original: their addresses are passed).
// FUNCTION: D3DREN 0x1000afb1
int ClipPoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	TLVertex *pOut;
	TLVertex *pVerts;
	int nVerts;
	char c0, c1, c2, c3, c4, c5;
	int bResult;

	if (g_CV_UseD3DClip.m_IntVal)
	{
		nFlags &= 1;
		if (!nFlags)
		{
			bResult = 1;
			goto Exit;	// (a plain `return 1;` makes the compiler push esi/edi after this test; the exe pushes them first)
		}
	}
	pOut = g_pClipScratchVerts;
	pVerts = *ppVerts;
	nVerts = *pnVerts;
	if (((nFlags & 1) == 0 || ClipPolyNear(&c0, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 4) == 0 || ClipPolyLeft(&c1, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 8) == 0 || ClipPolyTop(&c2, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 0x10) == 0 || ClipPolyRight(&c3, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 0x20) == 0 || ClipPolyBottom(&c4, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 2) == 0 || ClipPolyFar(&c5, &pVerts, &nVerts, &pOut)))
	{
		*ppVerts = pVerts;
		*pnVerts = nVerts;
		bResult = 1;
	}
	else
		bResult = 0;
Exit:
	return bResult;
}

// The inside[] arrays of ClipPolyNear / ClipPolyLeft: function-local statics of the original polyclip.h expansion (the exe has
// them with the other clipper statics at the end of .bss); this unit holds the out-of-line copies.
// GLOBAL: D3DREN 0x10094de0
int g_ClipNearInsideFlagsTLVertex[56];
// GLOBAL: D3DREN 0x10094ec0
int g_ClipLeftInsideFlagsTLVertex[56];

// guess: Jupiter polyclip.h expanded for the near plane (z >= g_ViewParams.m_NearZ) on 0x20-byte vertices: *ppVerts / *pnVerts are the
// polygon (replaced by the clipped copy built at *ppOut, which advances); returns 0 when nothing is inside.  The first
// argument is unused.  Its inside[] array is a function-local static of the original (0x10094de0), shared with the copy
// inlined in ClipModelPolygon32.
// FUNCTION: D3DREN 0x1000b0cd
int ClipPolyNear(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	TLVertex *&pVerts = *ppVerts;
	int &nVerts = *pnVerts;
	TLVertex *&pOut = *ppOut;
	int nInside = 0;
	int *pInside;
	TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	g_nPlaneClipTests++;
	pInside = g_ClipNearInsideFlagsTLVertex;
	pCur = pVerts;
	pEnd = pCur + nVerts;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.z >= g_ViewParams.m_NearZ;
		nInside += *pInside;
		++pInside;
		++pCur;
	}
	if (nInside == 0)
		return 0;
	else if (nInside != nVerts)
	{
		pOldOut = pOut;
		iPrev = nVerts - 1;
		pPrev = pVerts + iPrev;
		for (iCur = 0; iCur < nVerts; iCur++)
		{
			pCur = pVerts + iCur;
			if (g_ClipNearInsideFlagsTLVertex[iPrev])
				*pOut++ = *pPrev;
			if (g_ClipNearInsideFlagsTLVertex[iPrev] != g_ClipNearInsideFlagsTLVertex[iCur])
			{
				t = IntersectNearClipPlane(pPrev->m_Vec, pCur->m_Vec, pOut->m_Vec);
				TLVertex_ClipExtra(pPrev, pCur, pOut, t);
				++pOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		nVerts = pOut - pOldOut;
		pVerts = pOldOut;
		pOut += nVerts;
	}
	return 1;
}

// guess: the same for the left plane (-z < x), inside[] at 0x10094ec0.
// FUNCTION: D3DREN 0x1000b20c
int ClipPolyLeft(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	TLVertex *&pVerts = *ppVerts;
	int &nVerts = *pnVerts;
	TLVertex *&pOut = *ppOut;
	int nInside = 0;
	int *pInside;
	TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	g_nPlaneClipTests++;
	pInside = g_ClipLeftInsideFlagsTLVertex;
	pCur = pVerts;
	pEnd = pCur + nVerts;
	while (pCur != pEnd)
	{
		*pInside = -pCur->m_Vec.z < pCur->m_Vec.x;
		nInside += *pInside;
		++pInside;
		++pCur;
	}
	if (nInside == 0)
		return 0;
	else if (nInside != nVerts)
	{
		pOldOut = pOut;
		iPrev = nVerts - 1;
		pPrev = pVerts + iPrev;
		for (iCur = 0; iCur < nVerts; iCur++)
		{
			pCur = pVerts + iCur;
			if (g_ClipLeftInsideFlagsTLVertex[iPrev])
				*pOut++ = *pPrev;
			if (g_ClipLeftInsideFlagsTLVertex[iPrev] != g_ClipLeftInsideFlagsTLVertex[iCur])
			{
				t = IntersectLeftClipPlane(pPrev->m_Vec, pCur->m_Vec, pOut->m_Vec);
				TLVertex_ClipExtra(pPrev, pCur, pOut, t);
				++pOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		nVerts = pOut - pOldOut;
		pVerts = pOldOut;
		pOut += nVerts;
	}
	return 1;
}

// vbcache.h declares a static class-type data member (it takes a `_$E` number): include it after this unit's own static initialisers.
#include "d3dren/vbcache.h"
