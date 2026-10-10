// d3d.ren sys/d3d/drawpolygrid (0x1002afd0-0x1002d000): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// sys/d3d/drawpolygrid (0x1002afd0-0x1002d000): Jupiter drawpolygrid.cpp, the Talon-era (DirectDraw 7 / Direct3D 7) form: PolyGrids are
// drawn with software vertex generation into 0x28-byte pre-transformed vertices (FVF 0x2c4), view frustum clipping done by the renderer
// itself; there is no shader / fresnel / bump map code (those are later additions of the Jupiter file).
// FLAGS: /O2 /Ob2
#include <math.h>
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "d3dren/tlvertex.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3d_draw.h"
#include "d3dren/d3d_texture.h"
#include "d3dren/polydraw.h"
#include "ltmatrix.h"
#include "ltquatbase.h"
#include "d3dren/3d_ops.h"
#include "sprite.h"
#include "de_world.h"

// FUNCTION: D3DREN 0x1002afd0 _$E2
// GLOBAL: D3DREN 0x10071860
ConVar g_CV_EnvMapPolyGrids("EnvMapPolyGrids", 1.0f);

// ---------------------------------------------------------------------------------------------------------------------------------
// Globals of this unit (all file statics; the module's own scratch buffers and the per-polygrid texture coordinate terms)
// ---------------------------------------------------------------------------------------------------------------------------------

// NAME: g_TriVertList / g_TriVertListSize: Jupiter drawpolygrid.cpp (`static void* g_TriVertList; static uint32 g_TriVertListSize`).  In
// Talon the list holds 0x28-byte TL vertices and the size counts vertices (Jupiter: bytes).
// GLOBAL: D3DREN 0x10070850
static UnkType_TLVertex40 *g_TriVertList = 0;
// GLOBAL: D3DREN 0x10070448
static uint32 g_TriVertListSize = 0;
// guess: the index list of the clipped polygrid (indices of the triangles that survive the clip test)
// GLOBAL: D3DREN 0x10070858
static uint16 *g_pPolyGridClippedIndices = 0;
// GLOBAL: D3DREN 0x1007044c
static uint32 g_PolyGridClippedIndexCapacity = 0;

// guess: u scale / u offset / v offset / v scale / alpha scale of the polygrid being drawn
// GLOBAL: D3DREN 0x10070438
static float g_PolyGridUScale;
// GLOBAL: D3DREN 0x1007043c
static float g_PolyGridUOffset;
// GLOBAL: D3DREN 0x10070440
static float g_PolyGridVOffset;
// GLOBAL: D3DREN 0x10070444
static float g_PolyGridVScale;
// GLOBAL: D3DREN 0x10070854
static float g_PolyGridAlphaScale;

// ---------------------------------------------------------------------------------------------------------------------------------
// Globals / functions of other units
// ---------------------------------------------------------------------------------------------------------------------------------

// Per-stage texture coordinate scale pair (u, v), indexed by the device stage (set by the texture binding code).


// The screen projection of the world polygon code (unit unk/10007930); the 0x28-byte plane clippers are declared in d3dren/tlvertex.h.
void ProjectVertexToScreen(float *pVert, const void *pViewParams);

// guess: environment map texture coordinates of one vertex (common_draw): from the viewer position, the vertex position and the
// vertex normal; writes u and v.  (Defined by unit sys/d3d/common_stuff with LTVector arguments; polydraw.h declares it with float *.)
void d3d_CalcWorldReflectionUVs(LTVector *pViewPos, LTVector *pPos, LTVector *pNormal, float *pU, float *pV);

// The shared inline helpers (d3d_GetBlendStates: d3d_draw.h, d3d_SetupTransformation: 3d_ops.h, d3d_SetTexture / d3d_DisableTexture:
// d3d_texture.h) are expanded in place by this A object, as in the exe.

// A 0x28-byte vertex as plain dwords (struct copies by `rep movsd`, no constructors: the exe's local triangle has no vector constructor loop).
struct UnkType_PGVertex
{
	uint32	m_Unk[10];
};

// The 0x10-byte colour table entries of an LTPolyGrid (Jupiter: LTVector4 m_ColorTable[256], here the engine header keeps uint32[0x400]).
struct UnkType_PGColor
{
	float	m_Unk00;	// r
	float	m_Unk04;	// g
	float	m_Unk08;	// b
	float	m_Unk0c;	// a
};

int ClassifyPolyGridTriangle(uint32 nClipFlags, uint32 *pOutFlags, float *pV0, float *pV1, float *pV2);
void GeneratePolyGridEnvMapUV(LTPolyGrid *pGrid, LTVector *pYAxis, LTVector *pXAxis, LTVector *pZAxis);
void ProjectPolyGridVertices(UnkType_TLVertex40 *pVerts, int nVerts);
void d3d_DrawPolyGrid(ViewParams *pParams, LTObject *pObj);
void d3d_QueuePolyGrid(ViewParams *pParams, LTObject *pObj);

// The two vertex generation loops of d3d_DrawPolyGrid (data bytes unsigned / signed) as separate inline helpers: the exe expands them one
// inline level deeper than the rest of the function (the calls of LTVector::operator+ / operator* inside them stay out of line).
// NAME: none, guess: GenerateUnsignedPolyGridVertices / GenerateSignedPolyGridVertices stand for the (nameless) inline helpers expanded inside 0x1002aff0.
// unsigned data bytes (FLAG_UNSIGNED): colour table index = byte, no specular store
inline void GenerateUnsignedPolyGridVertices(LTPolyGrid *pGrid, UnkType_TLVertex40 *pVert, LTVector vOrigin, LTVector &vXInc, LTVector &vYAxis, LTVector &vZInc, int bEnvMap)
{
	uint8 *pRowData = (uint8 *)pGrid->m_Data;
	LTVector vRow = vOrigin;
	float fV = 0.0f;

	uint32 y = pGrid->m_Height;
	while (y--)
	{
		uint8 *pData = pRowData;
		LTVector vCur = vRow;
		float fU = 0.0f;
		uint32 x = pGrid->m_Width;

		while (x--)
		{
			uint8 nData = *pData;
			UnkType_PGColor *pColor = (UnkType_PGColor *)pGrid->m_ColorTable + nData;

			pVert->m_Vec = vCur + vYAxis * (float)nData;
			pVert->rgb.r = g_VertexTintTableR[(uint8)(int)pColor->m_Unk00];
			pVert->rgb.g = g_VertexTintTableG[(uint8)(int)pColor->m_Unk04];
			pVert->rgb.b = g_VertexTintTableB[(uint8)(int)pColor->m_Unk08];
			pVert->rgb.a = (uint8)(g_PolyGridAlphaScale * pColor->m_Unk0c);
			if (bEnvMap)
			{
				pVert->tu2 = (fU + g_PolyGridUOffset) * g_PolyGridUScale;
				pVert->tv2 = (fV + g_PolyGridVOffset) * g_PolyGridVScale;
			}
			else
			{
				pVert->tu = (fU + g_PolyGridUOffset) * g_PolyGridUScale;
				pVert->tv = (fV + g_PolyGridVOffset) * g_PolyGridVScale;
			}

			vCur.x += vXInc.x;
			vCur.y += vXInc.y;
			vCur.z += vXInc.z;
			pData++;
			pVert++;
			fU += 1.0f;
		}

		vRow.x += vZInc.x;
		vRow.y += vZInc.y;
		vRow.z += vZInc.z;
		pRowData += pGrid->m_Width;
		fV += 1.0f;
	}
}

// signed data bytes: colour table index = byte + 128, specular = 0xffffffff
inline void GenerateSignedPolyGridVertices(LTPolyGrid *pGrid, UnkType_TLVertex40 *pVert, LTVector vOrigin, LTVector &vXInc, LTVector &vYAxis, LTVector &vZInc, int bEnvMap)
{
	char *pRowData = pGrid->m_Data;
	LTVector vRow = vOrigin;
	float fV = 0.0f;

	uint32 y = pGrid->m_Height;
	while (y--)
	{
		char *pData = pRowData;
		LTVector vCur = vRow;
		float fU = 0.0f;
		uint32 x = pGrid->m_Width;

		while (x--)
		{
			int nData = *pData;
			UnkType_PGColor *pColor = (UnkType_PGColor *)pGrid->m_ColorTable + (nData + 128);

			pVert->m_Vec = vCur + vYAxis * (float)nData;
			pVert->rgb.r = g_VertexTintTableR[(uint8)(int)pColor->m_Unk00];
			pVert->rgb.g = g_VertexTintTableG[(uint8)(int)pColor->m_Unk04];
			pVert->rgb.b = g_VertexTintTableB[(uint8)(int)pColor->m_Unk08];
			pVert->rgb.a = (uint8)(g_PolyGridAlphaScale * pColor->m_Unk0c);
			pVert->specular = 0xffffffff;
			if (bEnvMap)
			{
				pVert->tu2 = (fU + g_PolyGridUOffset) * g_PolyGridUScale;
				pVert->tv2 = (fV + g_PolyGridVOffset) * g_PolyGridVScale;
			}
			else
			{
				pVert->tu = (fU + g_PolyGridUOffset) * g_PolyGridUScale;
				pVert->tv = (fV + g_PolyGridVOffset) * g_PolyGridVScale;
			}

			vCur.x += vXInc.x;
			vCur.y += vXInc.y;
			vCur.z += vXInc.z;
			pData++;
			pVert++;
			fU += 1.0f;
		}

		vRow.x += vZInc.x;
		vRow.y += vZInc.y;
		vRow.z += vZInc.z;
		pRowData += pGrid->m_Width;
		fV += 1.0f;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Internal functions
// ---------------------------------------------------------------------------------------------------------------------------------

// guess: the 0x28-byte vertex clipper dispatch as an inline function of the original (the exe expands it here with its own copies of the
// vertex pointer and count; unit unk/10007930 has the out-of-line copy ClipPolygon40): the polygon *ppVerts / *pnVerts is clipped
// against the planes of nFlags; with the UseD3DClip console variable set only the near plane is.
static inline int ClipPolygon40_Inline(uint32 nFlags, UnkType_TLVertex40 **ppVerts, int *pnVerts)
{
	UnkType_TLVertex40 *pOut;
	UnkType_TLVertex40 *pVerts;
	int nVerts;
	char c0, c1, c2, c3, c4, c5;

	if (g_CV_UseD3DClip.m_IntVal)
	{
		nFlags &= 1;
		if (!nFlags)
			return 1;
	}
	pOut = (UnkType_TLVertex40 *)g_pClipScratchVerts;
	pVerts = *ppVerts;
	nVerts = *pnVerts;
	if (((nFlags & 1) == 0 || ClipPolyNear40(&c0, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 4) == 0 || ClipPolyLeft40(&c1, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 8) == 0 || ClipPolyTop40(&c2, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 0x10) == 0 || ClipPolyRight40(&c3, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 0x20) == 0 || ClipPolyBottom40(&c4, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 2) == 0 || ClipPolyFar40(&c5, &pVerts, &nVerts, &pOut)))
	{
		*ppVerts = pVerts;
		*pnVerts = nVerts;
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------------------

// NAME: d3d_DrawPolyGrid: Jupiter drawpolygrid.cpp (names_proposal.csv, high).  Talon signature: cdecl (ViewParams *, LTObject *), the
// BaseObjectSet draw callback type (DrawObjectFn).
//
// Structure (everything below follows the exe's code): blend states (d3d_GetBlendStates + four StateSets) -> frustum test of the bounding
// sphere (6 planes of ViewParams, gives the clip mask) -> texture binding (base texture on stage 0, or on stage 1 with the linked
// texture = environment map on stage 0, d3d_SetTexture expanded in place) -> texture coordinate terms -> object transform x centering
// translation (d3d_SetupTransformation, LTMatrix::Identity, MatMul) -> 0x28-byte vertex list (GenerateUnsignedPolyGridVertices/2) -> optional
// environment map coordinates (GeneratePolyGridEnvMapUV) -> either the clipped path (view transform, per triangle ClassifyPolyGridTriangle classification,
// partial triangles copied and clipped by the 0x28-byte plane clippers, then ProjectPolyGridVertices projection + DrawIndexedPrimitive) or the
// unclipped path (one combined matrix, DrawIndexedPrimitive) -> StateSet destructors restore the blend states.
//
// STUB: 5424 of 5488 bytes, 1431 vs 1432 instructions, frame 0x234 vs 0x238; the out-of-line call set is the exe's (inline_budget.py:
// model, build and exe agree, 8u inside the reproducing budget range 3838..3880u).  What settled it, with the exe's evidence:
//  - the blend states are written out as in d3d_DrawSprite (matched), not the d3d_GetBlendStates inline;
//  - the triangle clip is the 0x28-byte clipper dispatch expanded inline (ClipPolygon40_Inline): the exe copies the vertex pointer and
//    count into the dispatch's own slots and writes them back to the caller's registers after the last plane clipper;
//  - the three copied triangle vertices (UnkType_TLVertex40[3], copied with rep movsd 10) and mFull have function scope: the exe
//    gives them disjoint slots (0x190..0x208 and 0x208..0x248).
// Left: register allocation (the exe keeps the constant 0 in edi and nDestBlend in ebx, ours the reverse; nClipFlags in esi during
// the frustum loop) and 4 bytes of frame, which shift every stack offset; the x87 order of the bounding-sphere half extents
// (the exe keeps all three on the stack).  Declaration order / names of the blend locals and the scope of nTriFlags, pIn, nVerts
// do not move it.
// FUNCTION: D3DREN 0x1002ce70 ??H?$_CVector@M@@QBE?AV0@V0@@Z
// FUNCTION: D3DREN 0x1002cec0 ??D?$_CVector@M@@QBE?AV0@M@Z
// STUB: D3DREN 0x1002aff0
void d3d_DrawPolyGrid(ViewParams *pParams, LTObject *pObj)
{
	LTPolyGrid *pGrid = (LTPolyGrid *)pObj;
	uint32 nSrcBlend, nDestBlend, nFog, nFogColor;
	uint32 nClipFlags;
	int bClip;
	int bEnvMap;
	int nStage;
	uint32 nTotal;
	UnkType_TLVertex40 aVerts[3];
	LTMatrix mFull;
	int i;

	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGCOLOR, (unsigned long *)&nFogColor);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, (unsigned long *)&nFog);
	if ((pGrid->m_Flags & FLAG_FOGDISABLE) && pGrid->m_ObjectType != OT_MODEL)
		nFog = 0;

	if (pGrid->m_Flags2 & FLAG2_ADDITIVE)
	{
		nSrcBlend = D3DBLEND_ONE;
		nDestBlend = D3DBLEND_ONE;
		nFogColor = 0;
	}
	else if (pGrid->m_Flags2 & FLAG2_MULTIPLY)
	{
		nSrcBlend = D3DBLEND_ZERO;
		nDestBlend = D3DBLEND_SRCCOLOR;
		nFogColor = 0xFFFFFFFF;
	}
	else
	{
		nSrcBlend = D3DBLEND_SRCALPHA;
		nDestBlend = D3DBLEND_INVSRCALPHA;
	}
	StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, nSrcBlend);
	StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, nDestBlend);
	StateSet ssFog(D3DRENDERSTATE_FOGENABLE, nFog);
	StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, nFogColor);

	// Make sure it's initialized.
	if (!pGrid->m_Data)
		return;

	// Cull the grid's bounding sphere against the view frustum.
	LTVector vHalf(((float)pGrid->m_Width * 0.5f) * pGrid->m_Scale.x, pGrid->m_Scale.y * 128.0f, ((float)pGrid->m_Height * 0.5f) * pGrid->m_Scale.z);
	LTVector vPos = pGrid->m_Pos;
	float fRadius = vHalf.Mag() + 1.0f;
	float fNegRadius = -fRadius;

	nClipFlags = 0x3f;
	for (i = 0; i < 6; i++)
	{
		LTPlane *pPlane = &pParams->m_ClipPlanes[i];
		float fDist = pPlane->m_Normal.Dot(vPos) - pPlane->m_Dist;

		if (fDist < fNegRadius)
			return;
		if (fDist > fRadius)
			nClipFlags &= ~(1 << i);
	}

	// Set the texture(s).
	SpriteTracker *pTracker;

	if (pGrid->m_pSprite)
	{
		pTracker = (SpriteTracker *)pGrid->m_SpriteTracker;

		if (pTracker->m_pCurFrame && pTracker->m_pCurFrame->m_pTex)
		{
			SharedTexture *pTex = pTracker->m_pCurFrame->m_pTex;

			// The linked texture of the base texture is the environment map.
			if (g_bTwoTextureStageBlendValidated && g_pBoundTextures[0] && pTex->m_pLinkedTexture && pTex->m_eTexType &&
				g_CV_EnvMapPolyGrids.m_IntVal && d3d_SetTexture(pTex->m_pLinkedTexture, 0, 0))
			{
				bEnvMap = 1;
			}
			else
			{
				bEnvMap = 0;
			}

			nStage = (bEnvMap != 0);
			if (d3d_SetTexture(pTracker->m_pCurFrame->m_pTex, nStage, 0))
			{
				g_TextureStateRestorer.RestoreAllStates();
				if (pTracker->m_pCurFrame->m_pTex->m_pStateChange)
					g_TextureStateRestorer.ApplyStateChange(pTracker->m_pCurFrame->m_pTex->m_pStateChange, nStage);
				nStage = bEnvMap ? 1 : 0;
				goto Textured;
			}
		}
	}

	d3d_DisableTexture(g_NormalTextureStage);
	bEnvMap = 0;
	nStage = 0;
Textured:

	// Texture coordinate terms of this grid.
	g_PolyGridUOffset = pGrid->m_xPan * g_TextureStageTexelSizes[nStage].m_Unk00;
	g_PolyGridVOffset = pGrid->m_yPan * g_TextureStageTexelSizes[nStage].m_Unk04;
	g_PolyGridUScale = (1.0f / pGrid->m_xScale) * pGrid->m_Scale.x * g_TextureStageTexelSizes[nStage].m_Unk00;
	g_PolyGridVScale = (1.0f / pGrid->m_yScale) * g_TextureStageTexelSizes[nStage].m_Unk04 * pGrid->m_Scale.z;
	bClip = (nClipFlags & 0x3f) != 0;
	g_PolyGridUScale = ((float)pGrid->m_Width / (int)(pGrid->m_Width - 1)) * g_PolyGridUScale;
	g_PolyGridVScale = ((float)pGrid->m_Height / (int)(pGrid->m_Height - 1)) * g_PolyGridVScale;
	g_PolyGridAlphaScale = (float)pGrid->m_ColorA * 0.003921569f;

	// specify that we were visible
	pGrid->m_Flags |= FLAG_INTERNAL1;

	// The transform that takes a (column, row, height) grid sample to a world position: object transform * centering translation.
	LTVector vScale(((float)(pGrid->m_Width + 1) * pGrid->m_Scale.x) / (int)pGrid->m_Width,
					pGrid->m_Scale.y,
					((float)(pGrid->m_Height + 1) * pGrid->m_Scale.z) / (int)pGrid->m_Height);
	LTMatrix mObject;
	d3d_SetupTransformation(&pGrid->m_Pos, (float *)&pGrid->m_Rotation, &vScale, &mObject);

	LTMatrix mCenter;
	mCenter.Identity();
	mCenter.m[0][3] = -((float)(pGrid->m_Width - 1) * 0.5f);
	mCenter.m[2][3] = -((float)(pGrid->m_Height - 1) * 0.5f);
	if (pGrid->m_Flags & FLAG_UNSIGNED)
		mCenter.m[1][3] = -128.0f;

	LTMatrix mGrid;
	MatMul(&mGrid, &mObject, &mCenter);

	LTVector vXInc, vYAxis, vZInc, vOrigin;
	mGrid.GetBasisVectors(&vXInc, &vYAxis, &vZInc);
	mGrid.GetTranslation(vOrigin);

	// Build the vertex list.
	nTotal = pGrid->m_Height * pGrid->m_Width;
	if (nTotal > g_TriVertListSize)
	{
		dfree(g_TriVertList);
		g_TriVertList = (UnkType_TLVertex40 *)dalloc(nTotal * sizeof(UnkType_TLVertex40));
		if (!g_TriVertList)
		{
			g_TriVertListSize = 0;
			return;
		}
		g_TriVertListSize = nTotal;
	}

	UnkType_TLVertex40 *pVerts = g_TriVertList;

	if (pGrid->m_Flags & FLAG_UNSIGNED)
		GenerateUnsignedPolyGridVertices(pGrid, pVerts, vOrigin, vXInc, vYAxis, vZInc, bEnvMap);
	else
		GenerateSignedPolyGridVertices(pGrid, pVerts, vOrigin, vXInc, vYAxis, vZInc, bEnvMap);

	// The environment map texture coordinates (first stage of the 2-stage setup).
	if (bEnvMap)
	{
		GeneratePolyGridEnvMapUV(pGrid, &vYAxis, &vXInc, &vZInc);
		d3d_SetEnvMapTextureStates(pTracker->m_pCurFrame->m_pTex->m_eTexType);
	}

	if (bClip)
	{
		// Transform to view space, clip the triangles that are not completely inside, project the rest.
		if (pGrid->m_nIndices > g_PolyGridClippedIndexCapacity)
		{
			g_pPolyGridClippedIndices = (uint16 *)dalloc(pGrid->m_nIndices * 2);
			if (!g_pPolyGridClippedIndices)
			{
				g_PolyGridClippedIndexCapacity = 0;
				return;
			}
			g_PolyGridClippedIndexCapacity = pGrid->m_nIndices;
		}

		UnkType_TLVertex40 *pCur = pVerts;
		for (i = nTotal; i; i--)
		{
			MatVMul_InPlace_H(&pParams->m_mClipTransform, &pCur->m_Vec);
			pCur++;
		}

		uint16 *pRowIndex = pGrid->m_Indices;
		uint32 nTrisPerRow = pGrid->m_Width * 2 - 2;
		uint32 nRowIndices = pGrid->m_Width * 6 - 6;
		uint32 nRows = pGrid->m_Height - 1;
		uint16 *pOut = g_pPolyGridClippedIndices;

		while (nRows--)
		{
			uint16 *pIndex = pRowIndex;
			uint32 nTris = nTrisPerRow;

			while (nTris--)
			{
				uint32 nTriFlags;
				int nResult = ClassifyPolyGridTriangle(nClipFlags, &nTriFlags,
										   (float *)(pVerts + pIndex[0]),
										   (float *)(pVerts + pIndex[1]),
										   (float *)(pVerts + pIndex[2]));
				if (nResult == 0)
				{
					*pOut++ = pIndex[0];
					*pOut++ = pIndex[1];
					*pOut++ = pIndex[2];
				}
				else if (nResult != 1)
				{
					UnkType_TLVertex40 *pIn = aVerts;
					int nVerts = 3;

					aVerts[0] = pVerts[pIndex[0]];
					aVerts[1] = pVerts[pIndex[1]];
					aVerts[2] = pVerts[pIndex[2]];

					if (!ClipPolygon40_Inline(nTriFlags, &pIn, &nVerts))
						goto NextTri;

					UnkType_TLVertex40 *pProj = pIn;
					for (i = nVerts; i; i--)
					{
						ProjectVertexToScreen((float *)pProj, &g_ViewParams);
						pProj++;
					}
					g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x2c4, pIn, nVerts, 0);
				}
NextTri:
				pIndex += 3;
			}

			pRowIndex += nRowIndices;
		}

		if ((int)((char *)pOut - (char *)g_pPolyGridClippedIndices) > 0)
		{
			ProjectPolyGridVertices(pVerts, nTotal);
			g_pD3DDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0x2c4, pVerts, nTotal, g_pPolyGridClippedIndices, pOut - g_pPolyGridClippedIndices, 0);
		}
	}
	else
	{
		mFull = pParams->m_DeviceTimesProjection * pParams->m_mClipTransform;
		UnkType_TLVertex40 *pCur = pVerts;

		for (i = nTotal; i; i--)
		{
			pCur->rhw = MatVMul_InPlace_H(&mFull, &pCur->m_Vec);
			pCur++;
		}

		if (pGrid->m_nIndices)
			g_pD3DDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0x2c4, pVerts, nTotal, pGrid->m_Indices, pGrid->m_nIndices, 0);
	}

	if (bEnvMap)
		d3d_UnsetEnvMapTextureStates();
}

// guess: clip test of one triangle against the frustum planes of nClipFlags: returns 1 when it is completely outside one plane, 0
// when it is completely inside all of them, else 2 and *pOutFlags is the set of planes that still have to clip it.
// FUNCTION: D3DREN 0x1002c560
int ClassifyPolyGridTriangle(uint32 nClipFlags, uint32 *pOutFlags, float *pV0, float *pV1, float *pV2)
{
	int nInside;

	if (nClipFlags & 4)
	{
		nInside = (-pV0[2] < pV0[0]) + (-pV1[2] < pV1[0]) + (-pV2[2] < pV2[0]);
		if (nInside == 0)
			return 1;
		if (nInside != 3)
		{
			*pOutFlags = nClipFlags & 0x3f;
			return 2;
		}
	}

	if (nClipFlags & 0x10)
	{
		nInside = (pV0[0] < pV0[2]) + (pV1[0] < pV1[2]) + (pV2[0] < pV2[2]);
		if (nInside == 0)
			return 1;
		if (nInside != 3)
		{
			*pOutFlags = nClipFlags & 0x3b;
			return 2;
		}
	}

	if (nClipFlags & 8)
	{
		nInside = (pV0[1] < pV0[2]) + (pV1[1] < pV1[2]) + (pV2[1] < pV2[2]);
		if (nInside == 0)
			return 1;
		if (nInside != 3)
		{
			*pOutFlags = nClipFlags & 0x2b;
			return 2;
		}
	}

	if (nClipFlags & 0x20)
	{
		nInside = (-pV0[2] < pV0[1]) + (-pV1[2] < pV1[1]) + (-pV2[2] < pV2[1]);
		if (nInside == 0)
			return 1;
		if (nInside != 3)
		{
			*pOutFlags = nClipFlags & 0x23;
			return 2;
		}
	}

	if (nClipFlags & 1)
	{
		nInside = (pV0[2] >= g_ViewParams.m_NearZ) + (pV1[2] >= g_ViewParams.m_NearZ) + (pV2[2] >= g_ViewParams.m_NearZ);
		if (nInside == 0)
			return 1;
		if (nInside != 3)
		{
			*pOutFlags = nClipFlags & 3;
			return 2;
		}
	}

	if (nClipFlags & 2)
	{
		nInside = (pV0[2] <= g_ViewParams.m_ClipFarZ) + (pV1[2] <= g_ViewParams.m_ClipFarZ) + (pV2[2] <= g_ViewParams.m_ClipFarZ);
		if (nInside == 0)
			return 1;
		if (nInside != 3)
		{
			*pOutFlags = nClipFlags & 2;
			return 2;
		}
	}

	return 0;
}

// guess: generates the environment map texture coordinates of every vertex of the grid from the height differences of its
// neighbours (the normal is built from the three axis vectors of the transformed grid) and stores them as the first texture coordinate pair
// (tu, tv) of the 0x28-byte vertices of g_TriVertList.
// FUNCTION: D3DREN 0x1002c840
void GeneratePolyGridEnvMapUV(LTPolyGrid *pGrid, LTVector *pYAxis, LTVector *pXAxis, LTVector *pZAxis)
{
	UnkType_TLVertex40 *pVert = g_TriVertList;
	char *pData = pGrid->m_Data;
	float fScale = pYAxis->MagSqr() * 0.0078125f;
	float fInvZ = 1.0f / pZAxis->Mag();
	float fInvX = 1.0f / pXAxis->Mag();
	uint32 y = pGrid->m_Height;

	while (y--)
	{
		uint32 x = pGrid->m_Width;
		UnkType_TLVertex40 *pCur = pVert;
		char *pCurData = pData;

		while (x--)
		{
			LTVector vZ, vX, vNormal;

			if (y < pGrid->m_Height - 1)
			{
				if (y > 0)
				{
					vZ = *pZAxis * ((float)((int)pCurData[-(int)pGrid->m_Width] - (int)pCurData[pGrid->m_Width]) * fInvZ * fScale * 0.5f);
				}
				else
				{
					vZ = *pZAxis * ((float)((int)pCurData[-(int)pGrid->m_Width] - (int)*pCurData) * fInvZ * fScale * 0.5f);
				}
			}
			else
			{
				vZ = *pZAxis * ((float)((int)pCurData[pGrid->m_Width] - (int)*pCurData) * fInvZ * fScale * 0.5f);
			}

			if (x < pGrid->m_Width - 1)
			{
				if (x > 0)
				{
					vX = *pXAxis * ((float)((int)pCurData[-1] - (int)pCurData[1]) * fInvX * fScale * 0.5f);
				}
				else
				{
					vX = *pXAxis * ((float)((int)pCurData[-1] - (int)*pCurData) * fInvX * fScale * 0.5f);
				}
			}
			else
			{
				vX = *pXAxis * ((float)((int)pCurData[1] - (int)*pCurData) * fInvX * fScale * 0.5f);
			}

			vNormal = *pYAxis + vX + vZ;
			vNormal.Norm(1.0f);
			d3d_CalcWorldReflectionUVs((LTVector *)&g_ViewParams.m_Pos, &pCur->m_Vec, &vNormal, &pCur->tu, &pCur->tv);

			pCur++;
			pCurData++;
		}

		pVert += pGrid->m_Width;
		pData += pGrid->m_Width;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// External functions
// ---------------------------------------------------------------------------------------------------------------------------------

// A lone `ret`: the linker folded every identical empty function into this copy (the PreFrame slot of the WORLDMODEL, SPRITE, POLYGRID
// and LINESYSTEM entries of g_ObjectHandlers).
// FUNCTION: D3DREN 0x1002cc80
void d3d_NullPreFrameCallback()
{
}

// NAME: d3d_ProcessPolyGrid: Jupiter drawpolygrid.cpp (names_proposal.csv, high); the Talon version has no DrawPolyGrids/translucency
// test (d3d_DrawSolidPolyGrids sorts the translucent grids out at draw time).
// FUNCTION: D3DREN 0x1002cc90
void d3d_ProcessPolyGrid(LTObject *pObject)
{
	d3d_GetVisibleSet()->m_SolidPolyGrids.Add(pObject);
}

// NAME: d3d_DrawSolidPolyGrids: names_proposal.csv (medium): Jupiter d3d_DrawSolidPolyGrids' role.  Draws the polygrids whose alpha is 255 at
// once and moves the others to the translucent set.
// FUNCTION: D3DREN 0x1002cce0
void d3d_DrawSolidPolyGrids()
{
	if (g_DrawPolyGrids)
	{
		VisibleSet *pSet = d3d_GetVisibleSet();
		uint32 i;

		pSet->m_TranslucentPolyGrids.ClearSet();
		for (i = 0; i < pSet->m_SolidPolyGrids.m_nObjects; i++)
		{
			LTObject *pObj = pSet->m_SolidPolyGrids.m_pObjects[i];

			// The BaseObjectSet::Draw filter (a portal view draws only what is not FLAG2_PORTALINVISIBLE / what is FLAG_PORTALVISIBLE).
			if (pObj->m_Flags & FLAG_VISIBLE)
			{
				if (g_ViewParams.m_bPortalView && (pObj->m_Flags2 & FLAG2_PORTALINVISIBLE))
					continue;
			}
			else
			{
				if (g_ViewParams.m_bPortalView && !(pObj->m_Flags & FLAG_PORTALVISIBLE))
					continue;
			}

			if (pObj->m_ColorA != 255)
				pSet->m_TranslucentPolyGrids.Add(pObj);
			else
				d3d_DrawPolyGrid(&g_ViewParams, pObj);
		}
	}
}

// NAME: d3d_QueueTranslucentPolyGrids: Jupiter drawpolygrid.cpp (names_proposal.csv, medium).
// FUNCTION: D3DREN 0x1002cdc0
void d3d_QueueTranslucentPolyGrids()
{
	if (g_DrawPolyGrids)
	{
		d3d_GetVisibleSet()->m_TranslucentPolyGrids.Draw(&g_ViewParams, d3d_QueuePolyGrid);
	}
}

// BaseObjectSet::Draw callback of the translucent set: queues the grid in the sorted translucent object list.
// FUNCTION: D3DREN 0x1002cdf0
void d3d_QueuePolyGrid(ViewParams *pParams, LTObject *pObj)
{
	g_pTranslucentObjectDrawList->Add(pObj, d3d_DrawPolyGrid);
}

// guess: ObjectHandler[OT_POLYGRID].m_ModuleInit
// FUNCTION: D3DREN 0x1002ce10
void d3d_InitPolyGridBuffers()
{
	g_TriVertList = 0;
	g_TriVertListSize = 0;
	g_pPolyGridClippedIndices = 0;
	g_PolyGridClippedIndexCapacity = 0;
}

// NAME: d3d_TermPolyGridDraw: Jupiter drawpolygrid.cpp (names_proposal.csv, medium); ObjectHandler[OT_POLYGRID].m_ModuleTerm.
// FUNCTION: D3DREN 0x1002ce30
void d3d_TermPolyGridDraw()
{
	dfree(g_TriVertList);
	dfree(g_pPolyGridClippedIndices);

	g_TriVertList = 0;
	g_TriVertListSize = 0;
	g_pPolyGridClippedIndices = 0;
	g_PolyGridClippedIndexCapacity = 0;
}

// guess: projects the 0x28-byte vertices (x', y', z', rhw) through the matrix at g_ViewParams.m_DeviceTimesProjection.m[0][0] (== g_ViewParams.m_Unk19c): the x, y and z of each
// vertex are divided by the w of the transformed position.
// FUNCTION: D3DREN 0x1002cf10
void ProjectPolyGridVertices(UnkType_TLVertex40 *pVerts, int nVerts)
{
	for (; nVerts != 0; nVerts--)
	{
		LTVector vNew;
		float fRHW = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][0] * pVerts->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pVerts->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][2] * pVerts->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[3][3]);

		vNew.x = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pVerts->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pVerts->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pVerts->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * fRHW;
		vNew.y = (g_ViewParams.m_DeviceTimesProjection.m[1][0] * pVerts->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pVerts->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pVerts->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[1][3]) * fRHW;
		vNew.z = (g_ViewParams.m_DeviceTimesProjection.m[2][0] * pVerts->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pVerts->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[2][2] * pVerts->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[2][3]) * fRHW;
		pVerts->m_Vec = vNew;
		pVerts->rhw = fRHW;
		pVerts++;
	}
}
