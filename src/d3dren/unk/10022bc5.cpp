// d3d.ren unk/10022bc5 (0x10022bc5-0x10023860): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// world poly passes (multipass gouraud, single pass, queue/flush). The agents split at 0x10023398: not supported.
// FLAGS: /O1 /Ob2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#define D3DREN_STATERESTORER_FULL	// d3ddevice.h: the real vector<RenderState>/vector<TextureState> members of UnkType_StateRestorer
#include <windows.h>
#include "ltbasedefs.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"

// ---- 2. dirtyrect.cpp ---------------------------------------------------------------------------------------------------------
#include "ltrect.h"
#include "ltlink.h"

// NAME: g_invalidRect, g_invalidRectCount, InvalidateRect, RectangleCombine, DirtyRectSwap, ClearDirtyRects: Jupiter
// render_a/src/sys/d3d/dirtyrect.cpp (the exe's InvalidateRect is line for line the same).
#define	MAX_INVALID_RECTS		100

// ---- 3. draw_canvas.cpp ---------------------------------------------------------------------------------------------------------
// NAME: CanvasDrawMgr, DrawCanvas, d3d_DrawCanvasCB, d3d_ProcessCanvas, d3d_DrawSolidCanvases, d3d_QueueTranslucentCanvases:
// Jupiter render_a/src/sys/d3d/draw_canvas.cpp/.h (the Talon versions differ: CanvasDrawMgr implements ILTCustomDraw (SDK
// iltcustomdraw.h: DrawPrimitive, SetState, GetState, SetTexture, GetTexelSize) and keeps the D3D states it changes so that
// DrawCanvas can put them back).
#include <string.h>
#include "iltcustomdraw.h"
#include "de_objects.h"
#include "de_world.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dstate.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/tlvertex.h"
#include "d3dren/polydraw.h"
#include "d3dren/pool.h"
#include "d3dren/draw_canvas.h"
#include "d3dren/scenedesc.h"
#include "ltmatrix.h"
#include "counter.h"

// ---- 4. the world polygon draw passes ---------------------------------------------------------------------------------------------
// No Jupiter equivalent (the Talon renderer's own poly code).  Everything here keeps Ghidra names; the roles are in the comments.
// The unit ends the object that starts at 0x10022bc5 with the atexit stub of DrawMultipassGouraudWorldPoly's local static (0x10023397); the code
// from 0x10023398 on (single pass draw and the queue flush) is a second object of the same source file or a following one.

// FUNCTION: D3DREN 0x10022bc5 _$E4
// FUNCTION: D3DREN 0x10022bca _$E3
// GLOBAL: D3DREN 0x10065bc0
ConVar g_CV_MultipassGouraud("MultipassGouraud", 1.0f);

// callees and globals of other units
// NAME: UnkType_PolyVertex (polydraw.h): the 0x18-byte world poly vertex; the bytes at +0x14..0x16 are b, g, r (the lighting tables are
// indexed in that order below).
extern UnkType_PoolBucket *g_pMultipassWorldPolyBuckets;	// guess: list of the buckets (polys queued per lightmap page); see unit unk/100132a0
// GLOBAL: D3DREN 0x10057774
extern uint8 g_nPolyVertexAlpha;				// guess: alpha byte of the poly vertex colour
// GLOBAL: D3DREN 0x10059d04
extern uint8 g_MultipassVertexTintTableR[256];			// guess: red lighting table (the multipass / dynamic light pass); the next two are the green and blue ones
// GLOBAL: D3DREN 0x10059e04
extern uint8 g_MultipassVertexTintTableG[256];
// GLOBAL: D3DREN 0x10059f04
extern uint8 g_MultipassVertexTintTableB[256];
// GLOBAL: D3DREN 0x1005a004
extern uint8 g_VertexTintTableR[256];			// guess: red table of the single pass ("saturate" variant); a104 green, a204 blue
// GLOBAL: D3DREN 0x1005a104
extern uint8 g_VertexTintTableG[256];
// GLOBAL: D3DREN 0x1005a204
extern uint8 g_VertexTintTableB[256];
void d3d_ApplyWorldPolyVertexLights(WorldPoly *pPoly, TLVertex *pVerts, int nVerts);	// unit unk/10019350 (drawsky.h)
int d3d_DrawFlatWorldPoly(WorldPoly *pPoly);										// unit unk/100132a0
extern int g_bDrawGouraudFullbritePass;				// guess: draw state flag (Gouraud fullbrites in use); declared by unit unk/100132a0

// Jupiter TLVertex::SetTCoords (3d_ops.h): the exe inlines it into the vertex builders (both arguments on the FPU stack, then two stores).
inline void TLVertex_SetTCoords(TLVertex *pVert, float inTU, float inTV)
{
	pVert->tu = inTU;
	pVert->tv = inTV;
}

// guess: queues the poly on the lightmap page bucket list of the multipass / second pass.
// FUNCTION: D3DREN 0x10022be4
void QueueWorldPolyWithClipFlags(WorldPoly *pPoly)
{
	AllocateTexturePolyNode(pPoly, &g_pMultipassWorldPolyBuckets, 1)->m_Unk0c = g_ClipFlags;
}

// guess: builds the TL vertices of a poly (nVerts of them, 0x20 bytes each) from its vertices with the multipass lighting tables
// (g_MultipassVertexTintTableR..), the vertex alpha g_nPolyVertexAlpha and the fog hook g_pfnCalcFogAlpha; the texture coordinates of the poly vertex are the first set.
// FUNCTION: D3DREN 0x1002329f
void BuildGouraudWorldPolyVertices(TLVertex *pDest, UnkType_PolyVertex *pSrc, int nVerts)
{
	int i;

	for (i = nVerts; i > 0; i--)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->rgb.r = g_MultipassVertexTintTableR[pSrc->m_Color[2]];
		pDest->rgb.g = g_MultipassVertexTintTableG[pSrc->m_Color[1]];
		pDest->rgb.b = g_MultipassVertexTintTableB[pSrc->m_Color[0]];
		pDest->rgb.a = g_nPolyVertexAlpha;
		g_pfnCalcFogAlpha(&pDest->m_Vec, &pDest->specular);
		TLVertex_SetTCoords(pDest, pSrc->m_U, pSrc->m_V);
		pSrc++;
		pDest++;
	}
}

// guess: the same with the second set of lighting tables (g_VertexTintTableR..).
// FUNCTION: D3DREN 0x1002331b
void BuildSaturatedGouraudWorldPolyVertices(TLVertex *pDest, UnkType_PolyVertex *pSrc, int nVerts)
{
	int i;

	for (i = nVerts; i > 0; i--)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->rgb.r = g_VertexTintTableR[pSrc->m_Color[2]];
		pDest->rgb.g = g_VertexTintTableG[pSrc->m_Color[1]];
		pDest->rgb.b = g_VertexTintTableB[pSrc->m_Color[0]];
		pDest->rgb.a = g_nPolyVertexAlpha;
		g_pfnCalcFogAlpha(&pDest->m_Vec, &pDest->specular);
		TLVertex_SetTCoords(pDest, pSrc->m_U, pSrc->m_V);
		pSrc++;
		pDest++;
	}
}

// guess: scales the texture coordinates of the TL vertices by the u / v scale of stage 0 (g_TextureStageTexelSizes / DAT_10061814).
// FUNCTION: D3DREN 0x1002358b
void ScaleWorldPolyStageZeroTexCoords(TLVertex *pVerts, int nVerts)
{
	while (nVerts--)
	{
		float u = pVerts->tu;
		float v = pVerts->tv;
		TLVertex_SetTCoords(pVerts, u * g_TextureStageTexelSizes[0].m_Unk00, v * g_TextureStageTexelSizes[0].m_Unk04);
		pVerts++;
	}
}

// guess: for mode 0 (blend: source 5 = SRCALPHASAT?, destination 2 = ONE): the render states of the detail / saturate blend.
// FUNCTION: D3DREN 0x100235bb
void SetAdditiveAlphaBlendForMode(int nMode)
{
	if (nMode == 0)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
	}
}

// The dynamic lights touching a poly: a list at WorldPoly+0x30 (the engine pads it): { next, the light, the light position }
// (the same record unit unk/10007930 and common_stuff.cpp use under this name).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;
	DynamicLight		*m_pLight;
	LTVector			m_Pos;
};
#define POLY_LIGHTS(p)	(*(UnkType_PolyLight **)((uint8 *)(p) + 0x30))

// The lightmap staging object BuildDynamicLightmapPixels fills (unit unk/10007930 declares it under this name; include/d3dren/lightmap.h calls the same
// 0x54-byte object UnkType_LMLock, which is what the locals below are).
struct UnkType_DynLMSetup;
int BuildDynamicLightmapPixels(UnkType_DynLMSetup *pSetup, WorldPoly *pPoly, UnkType_PolyLight *pLight, float fScale);	// unit unk/10030bb0
void DrawWorldPolyDynamicLightPasses(WorldPoly *pPoly, TLVertex *pVerts, int nVerts);
void DrawAndFreeQueuedWorldPolyNodes(UnkType_PoolNode *pNode);
void DrawSinglePassGouraudWorldPoly(WorldPoly *pPoly);
void QueueClippedLinkedTextureWorldPolyPass(WorldPoly *pPoly);
void QueueEnvMappedWorldPolyPass(WorldPoly *pPoly);

// layout of CountAdder (counter.h) without the inline destructor: the exe destroys the local through the
// out-of-line copy (unit unk/10007930's CountAdder_Destructor); the class dtor cannot be kept out of line by source shape.
struct UnkType_CountAdderRaw
{
	UnkType_CountAdderRaw(uint32 *pNum)
	{
		m_pNum = pNum;
		cnt_StartCounter(m_Counter);
	}

	Counter		m_Counter;
	uint32		*m_pNum;
};
void __fastcall CountAdder_Destructor(CountAdder *pThis);	// unit unk/10007930: the out-of-line CountAdder::~CountAdder copy

// guess: the poly vertex scratch array of the multipass draw.  In the original it is a function-local static of DrawMultipassGouraudWorldPoly of a class that has a
// destructor (the exe has the guard bit at 0x10067be0 and registers the empty stub 0x10023397 with atexit); the array itself is at 0x10065be0.
struct UnkType_ScratchVerts
{
	TLVertex	m_aVerts[0x100];
	~UnkType_ScratchVerts() {}
};

// guess: multipass gouraud: draws the poly with the Gouraud (vertex colour) lighting, queueing it so that the lightmap pass draws it again
// (bSaturate selects the second set of lighting tables and the saturate blend of the second pass).  Dynamic lights are drawn by DrawWorldPolyDynamicLightPasses.
// FUNCTION: D3DREN 0x10023397 _$E7
// FUNCTION: D3DREN 0x10022c01
void DrawMultipassGouraudWorldPoly(WorldPoly *pPoly, int bSaturate)
{
	static UnkType_ScratchVerts s_Scratch;
	int bClip = 1;
	int bEnvMap;
	UnkType_PolyVertex *pSrc;
	int nVerts;
	TLVertex *pVerts;
	TLVertex *pDest;
	DWORD dwOldAlphaBlend;
	DWORD dwOldAlphaFunc;
	DWORD dwOldZFunc;
	UnkType_PoolNode *pNode;

	bEnvMap = ((Surface *)pPoly->m_pSurface)->m_pTexture->m_eTexType != 0 && g_CV_EnvMapWorld.m_IntVal != 0 && g_bTwoTextureStageBlendValidated != 0;

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	pVerts = s_Scratch.m_aVerts;
	if (bSaturate)
		BuildSaturatedGouraudWorldPolyVertices(pVerts, pSrc, nVerts);
	else
		BuildGouraudWorldPolyVertices(pVerts, pSrc, nVerts);

	if (g_ClipFlags && !bEnvMap)
	{
		if (!d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
			return;
		bClip = 0;
	}

	if (!g_CV_LMDynamic.m_IntVal)
		d3d_ApplyWorldPolyVertexLights(pPoly, pVerts, nVerts);

	if (bClip && !bEnvMap)
	{
		if (!d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
			return;
	}

	pDest = d3d_ReservePolyScratchVertices(nVerts);
	if (!pDest)
		return;
	memcpy(pDest, pVerts, nVerts << 5);

	if (d3d_GetChromaKeyPass())
	{
		pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
		pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
		pNode->m_Unk08 = nVerts;
		pNode->m_Unk0c = g_ClipFlags;
		pNode->m_Unk14 = 3;
		d3d_FlushPendingWorldTextureBuckets();
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &dwOldAlphaBlend);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHAFUNC, &dwOldAlphaFunc);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAFUNC, D3DCMP_ALWAYS);
		if (g_Saturate && bSaturate)
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
		else
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZFUNC, &dwOldZFunc);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_EQUAL);
	}

	if (!g_CV_LMFullBright.m_IntVal)
	{
		DWORD dwFogColor = d3d_PackSqrtRGB(g_u8FogColorR, g_u8FogColorG, g_u8FogColorB);
		StateSet ssFog(D3DRENDERSTATE_FOGCOLOR, dwFogColor);

		d3d_UnsetTexture(g_NormalTextureStage);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
		if (bEnvMap)
			d3d_DrawClippedTriangleFan(pDest, nVerts, &g_ViewParams, 0x1c4);
		else
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pDest, nVerts, 0);
		if (POLY_LIGHTS(pPoly))
			DrawWorldPolyDynamicLightPasses(pPoly, pVerts, nVerts);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	}

	if (d3d_GetChromaKeyPass())
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, dwOldAlphaBlend);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAFUNC, dwOldAlphaFunc);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZFUNC, dwOldZFunc);
	}
	else
	{
		pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
		pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
		pNode->m_Unk08 = nVerts;
		pNode->m_Unk0c = g_ClipFlags;
		pNode->m_Unk14 = (bSaturate ? 2 : 0) | 1;
		if (g_CV_FixSparkleys.m_IntVal)
			d3d_FlushPendingWorldTextureBuckets();
	}

	g_nQueuedWorldPolyVertices += nVerts;
	g_nWorldPolysDrawn++;
}

// guess: dynamic lights of the poly (LMDynamic): for every light of the poly draws the poly again with additive blending and the
// texture coordinates projected from the light position (the lightmap of the light is BuildDynamicLightmapPixels's).
// The exe destroys the CountAdder out of line (`lea ecx, [ebp-0x44]; call 0x100083ae`, the COMDAT copy of CountAdder::~CountAdder emitted by
// unit unk/10007930) -- unit unk/10007930's DrawPolyDynamicLightmaps has the same out-of-line dtor call, and no source shape tried (toys, decl order,
// dead inline sites, inline_budget/inline_ballast probes) keeps counter.h's in-class dtor out of line with the RTM C1XX under /O1 /Ob2, so
// the local is the layout-identical UnkType_CountAdderRaw (ctor inlined exactly like CountAdder's) plus an explicit call to CountAdder_Destructor at
// the scope end.  The x87 products come from LTVector::Dot (`P.Dot(vDelta)`): written out as products they compile to `fld st(i); fmul [P]`
// instead of the exe's `fld [P]; fmul st(i)`.  The StateSets/StageStateSets sit in their own block so their dtors run before the counter dtor.
// FUNCTION: D3DREN 0x10022f85
void DrawWorldPolyDynamicLightPasses(WorldPoly *pPoly, TLVertex *pVerts, int nVerts)
{
	if (g_CV_LMDynamic.m_IntVal)
	{
		UnkType_CountAdderRaw cTimer(g_pSceneDesc->m_pTicks_Render_PolyGrids);
		LTVector P, Q;
		LTMatrix mInvTransform;
		UnkType_PolyLight *pLight;

		pPoly->m_LMWidth = (uint8)g_CV_LMDynamicSize.m_IntVal;
		pPoly->m_LMHeight = (uint8)g_CV_LMDynamicSize.m_IntVal;
		SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);

		{
			StateSet ssAlpha(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			StateSet ssSrc(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
			StateSet ssDest(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
			StateSet ssFog(D3DRENDERSTATE_FOGCOLOR, 0);
			StageStateSet ssAddress(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
			StageStateSet ssColorOp(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);

			{
				LTMatrix mCopy = g_ViewParams.m_FullTransform;
				mCopy.Inverse();
				mInvTransform = mCopy;
			}

			for (pLight = POLY_LIGHTS(pPoly); pLight; pLight = pLight->m_pNext)
			{
				UnkType_LMLock lock;
				int nBuild;

				if (lock.LockStagingLightmap(pPoly, 1, 0, 0))
				{
					nBuild = BuildDynamicLightmapPixels((UnkType_DynLMSetup *)&lock, pPoly, pLight, g_CV_MultipassGouraud.m_IntVal ? 1.0f : 2.0f);
					if (lock.UnlockStagingLightmap(nBuild) && nBuild)
					{
						float fScale = 1.0f / (pLight->m_pLight->GetLightRadius((uint32)pLight->m_pLight) * g_CV_LMDynamicScale.m_FloatVal);
						TLVertex *pVert = pVerts;
						int n;

						for (n = nVerts; n; n--)
						{
							LTVector vWorld;
							LTVector vDelta;

							mInvTransform.Apply4x4(pVert->m_Vec, vWorld);
							vDelta = vWorld - pLight->m_Pos;
							pVert->tu = P.Dot(vDelta) * fScale + 0.5f;
							pVert->tv = Q.Dot(vDelta) * fScale + 0.5f;
							pVert++;
						}
						g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
						if (g_Saturate)
							g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
					}
				}
			}
		}
		CountAdder_Destructor((CountAdder *)&cTimer);
	}
}

// guess: single pass draw of a world poly: Gouraud vertex colours and the texture (and the stage state change of its texture), or when the
// texture is a fullbrite one and g_bDrawGouraudFullbritePass is set, once with the texture and once more with full brightness additively.
// FUNCTION: D3DREN 0x10023398
void DrawSinglePassGouraudWorldPoly(WorldPoly *pPoly)
{
	TLVertex aVerts[0x80];
	TLVertex *pVerts;
	int nVerts;
	UnkType_PolyVertex *pSrc;
	RGBColor color;

	if (WORLDPOLY_UNK30(pPoly) && g_CV_LMDynamic.m_IntVal)
	{
		DrawMultipassGouraudWorldPoly(pPoly, 0);
		return;
	}

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	pVerts = aVerts;
	BuildGouraudWorldPolyVertices(pVerts, pSrc, nVerts);
	if (!g_CV_LMDynamic.m_IntVal)
		d3d_ApplyWorldPolyVertexLights(pPoly, pVerts, nVerts);
	if (!d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
		return;

	if (!d3d_SetTexture(((Surface *)pPoly->m_pSurface)->m_pTexture, g_NormalTextureStage, 0))
	{
		d3d_DrawFlatWorldPoly(pPoly);
	}
	else
	{
		ScaleWorldPolyStageZeroTexCoords(pVerts, nVerts);
		if (((RTexture *)g_pBoundTextures[g_NormalTextureStage])->IsFullbrite() && g_bDrawGouraudFullbritePass)
		{
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
			if (g_FogEnable)
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);

			color.rgb.a = 0xff;
			color.rgb.r = g_MultipassVertexTintTableR[255];
			color.rgb.g = g_MultipassVertexTintTableG[255];
			color.rgb.b = g_MultipassVertexTintTableB[255];
			if (nVerts)
			{
				TLVertex *pVert = pVerts;
				int n;
				for (n = nVerts; n; n--)
				{
					pVert->color = color.color;
					pVert++;
				}
			}

			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
			if (g_FogEnable)
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
		}
		else
		{
			StateChange *pChange = ((Surface *)pPoly->m_pSurface)->m_pTexture->m_pStateChange;
			if (pChange)
				g_TextureStateRestorer.ApplyStateChange(pChange, 0);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
			g_TextureStateRestorer.RestoreAllStates();
		}
		g_nWorldPolysDrawn++;
	}
}

// guess: nothing (the PreFrame hook of OT_CANVAS in the object handler table, see unit unk/100285a0).
// FUNCTION: D3DREN 0x100235e1
void d3d_NullCallback(void)
{
}

// guess: draws everything that was queued per lightmap page by QueueWorldPolyWithClipFlags (every page's polys, then the page is reset) and returns the buckets to the pool.
// FUNCTION: D3DREN 0x100235e2
void FlushQueuedWorldPolyPages(void)
{
	UnkType_PoolBucket *pBucket;

	pBucket = g_pMultipassWorldPolyBuckets;
	if (pBucket)
	{
		do
		{
			UnkType_PoolBucket *pNext = pBucket->m_Unk08;

			DrawAndFreeQueuedWorldPolyNodes((UnkType_PoolNode *)pBucket->m_Unk04);
			((LightmapPage *)pBucket->m_Unk00)->m_Unk20 = 0;
			sb_Free(&g_WorldPolyBucketBank, pBucket);
			pBucket = pNext;
		} while (pBucket);
	}
	g_pMultipassWorldPolyBuckets = 0;
}

// guess: draws the queued polys of one page (a list of nodes linked through m_Unk10) with the render state word each was queued with and gives the
// nodes back to the pool: multipass gouraud when MultipassGouraud is on, else by the kind of the texture of the poly.
// FUNCTION: D3DREN 0x1002362a
void DrawAndFreeQueuedWorldPolyNodes(UnkType_PoolNode *pNode)
{
	if (pNode)
	{
		do
		{
			UnkType_PoolNode *pNext;

			pNext = pNode->m_Unk10;
			g_ClipFlags = pNode->m_Unk0c;
			if (g_CV_MultipassGouraud.m_IntVal)
			{
				DrawMultipassGouraudWorldPoly((WorldPoly *)pNode->m_Unk00, 1);
			}
			else
			{
				WorldPoly *pPoly = (WorldPoly *)pNode->m_Unk00;
				SharedTexture *pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;

				if (!g_bTwoTextureStageBlendValidated || !pTexture || !pTexture->m_pLinkedTexture)
					DrawSinglePassGouraudWorldPoly(pPoly);
				else if (pTexture->m_eTexType)
					QueueEnvMappedWorldPolyPass(pPoly);
				else
					QueueClippedLinkedTextureWorldPolyPass(pPoly);
			}
			sb_Free(&g_WorldPolyNodeBank, pNode);
			pNode = pNext;
		} while (pNode);
	}
}

// guess: the draw pass of an environment mapped poly: the single pass (unless EnvMapWorld) and the poly queued on the second pass list
// (flag 4: the environment map pass).
// FUNCTION: D3DREN 0x100236ae
void QueueEnvMappedWorldPolyPass(WorldPoly *pPoly)
{
	UnkType_PolyVertex *pSrc;
	int nVerts;
	TLVertex *pDest;
	UnkType_PoolNode *pNode;

	if (WORLDPOLY_UNK30(pPoly) && g_CV_LMDynamic.m_IntVal)
	{
		DrawMultipassGouraudWorldPoly(pPoly, 0);
		return;
	}

	if (!g_CV_EnvMapWorld.m_IntVal)
		DrawSinglePassGouraudWorldPoly(pPoly);

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	pDest = d3d_ReservePolyScratchVertices(nVerts);
	if (pDest)
	{
		BuildGouraudWorldPolyVertices(pDest, pSrc, nVerts);
		if (!g_CV_LMDynamic.m_IntVal)
			d3d_ApplyWorldPolyVertexLights(pPoly, pDest, nVerts);
		pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
		pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
		pNode->m_Unk08 = nVerts;
		pNode->m_Unk0c = g_ClipFlags;
		pNode->m_Unk14 = 4;
		g_nQueuedWorldPolyVertices += nVerts;
		g_nWorldPolysDrawn++;
	}
}

// guess: the draw pass of a poly whose texture has a detail texture (m_eTexType 0): clips the poly and queues it on the second pass list with state
// word 0 (flag 4).
// FUNCTION: D3DREN 0x10023763
void QueueClippedLinkedTextureWorldPolyPass(WorldPoly *pPoly)
{
	TLVertex aVerts[0x80];
	TLVertex *pVerts;
	int nVerts;
	UnkType_PolyVertex *pSrc;
	TLVertex *pDest;
	UnkType_PoolNode *pNode;

	if (WORLDPOLY_UNK30(pPoly) && g_CV_LMDynamic.m_IntVal)
	{
		DrawMultipassGouraudWorldPoly(pPoly, 0);
		return;
	}

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	BuildGouraudWorldPolyVertices(aVerts, pSrc, nVerts);
	pVerts = aVerts;
	if (!g_CV_LMDynamic.m_IntVal)
		d3d_ApplyWorldPolyVertexLights(pPoly, pVerts, nVerts);
	if (!d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
		return;

	pDest = d3d_ReservePolyScratchVertices(nVerts);
	if (pDest)
	{
		memcpy(pDest, pVerts, nVerts << 5);
		pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
		pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
		pNode->m_Unk08 = nVerts;
		pNode->m_Unk0c = 0;
		pNode->m_Unk14 = 4;
		g_nQueuedWorldPolyVertices += nVerts;
		g_nWorldPolysDrawn++;
	}
}
