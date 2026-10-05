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
static uint16 *DAT_10070858 = 0;
// GLOBAL: D3DREN 0x1007044c
static uint32 DAT_1007044c = 0;

// guess: u scale / u offset / v offset / v scale / alpha scale of the polygrid being drawn
// GLOBAL: D3DREN 0x10070438
static float DAT_10070438;
// GLOBAL: D3DREN 0x1007043c
static float DAT_1007043c;
// GLOBAL: D3DREN 0x10070440
static float DAT_10070440;
// GLOBAL: D3DREN 0x10070444
static float DAT_10070444;
// GLOBAL: D3DREN 0x10070854
static float DAT_10070854;

// ---------------------------------------------------------------------------------------------------------------------------------
// Globals / functions of other units
// ---------------------------------------------------------------------------------------------------------------------------------

// Per-stage texture coordinate scale pair (u, v), indexed by the device stage (set by the texture binding code).
struct UnkType_StageUV
{
	float	m_Unk00;
	float	m_Unk04;
};
// GLOBAL: D3DREN 0x10061810
extern UnkType_StageUV DAT_10061810[8];

// The gamma / colour correction tables the vertex colours are looked up in (one byte per input level).
// GLOBAL: D3DREN 0x1005a004
extern uint8 DAT_1005a004[256];
// GLOBAL: D3DREN 0x1005a104
extern uint8 DAT_1005a104[256];
// GLOBAL: D3DREN 0x1005a204
extern uint8 DAT_1005a204[256];


// The 0x28-byte vertex plane clippers / projection of the world polygon code (units unk/10001000 and unk/10007930).
void ProjectVertexToScreen(float *pVert, const void *pViewParams);
int FUN_100088ec(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10008a23(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10006e40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007100(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_100073b0(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007670(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);

// guess: environment map texture coordinates of one vertex (common_draw): from the viewer position, the vertex position and the
// vertex normal; writes u and v.  (Defined by unit sys/d3d/common_stuff with LTVector arguments; polydraw.h declares it with float *.)
void FUN_1001085e(LTVector *pViewPos, LTVector *pPos, LTVector *pNormal, float *pU, float *pV);

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

int FUN_1002c560(uint32 nClipFlags, uint32 *pOutFlags, float *pV0, float *pV1, float *pV2);
void FUN_1002c840(LTPolyGrid *pGrid, LTVector *pYAxis, LTVector *pXAxis, LTVector *pZAxis);
void FUN_1002cf10(UnkType_TLVertex40 *pVerts, int nVerts);
void d3d_DrawPolyGrid(ViewParams *pParams, LTObject *pObj);
void FUN_1002cdf0(ViewParams *pParams, LTObject *pObj);

// The two vertex generation loops of d3d_DrawPolyGrid (data bytes unsigned / signed) as separate inline helpers: the exe expands them one
// inline level deeper than the rest of the function (the calls of LTVector::operator+ / operator* inside them stay out of line).
// NAME: none, guess: FUN_1002aff0_inl1 / FUN_1002aff0_inl2 stand for the (nameless) inline helpers expanded inside 0x1002aff0.
// unsigned data bytes (FLAG_UNSIGNED): colour table index = byte, no specular store
inline void FUN_1002aff0_inl1(LTPolyGrid *pGrid, UnkType_TLVertex40 *pVert, LTVector vOrigin, LTVector &vXInc, LTVector &vYAxis, LTVector &vZInc, int bEnvMap)
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
			pVert->rgb.r = DAT_1005a004[(uint8)(int)pColor->m_Unk00];
			pVert->rgb.g = DAT_1005a104[(uint8)(int)pColor->m_Unk04];
			pVert->rgb.b = DAT_1005a204[(uint8)(int)pColor->m_Unk08];
			pVert->rgb.a = (uint8)(DAT_10070854 * pColor->m_Unk0c);
			if (bEnvMap)
			{
				pVert->tu2 = (fU + DAT_1007043c) * DAT_10070438;
				pVert->tv2 = (fV + DAT_10070440) * DAT_10070444;
			}
			else
			{
				pVert->tu = (fU + DAT_1007043c) * DAT_10070438;
				pVert->tv = (fV + DAT_10070440) * DAT_10070444;
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
inline void FUN_1002aff0_inl2(LTPolyGrid *pGrid, UnkType_TLVertex40 *pVert, LTVector vOrigin, LTVector &vXInc, LTVector &vYAxis, LTVector &vZInc, int bEnvMap)
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
			pVert->rgb.r = DAT_1005a004[(uint8)(int)pColor->m_Unk00];
			pVert->rgb.g = DAT_1005a104[(uint8)(int)pColor->m_Unk04];
			pVert->rgb.b = DAT_1005a204[(uint8)(int)pColor->m_Unk08];
			pVert->rgb.a = (uint8)(DAT_10070854 * pColor->m_Unk0c);
			pVert->specular = 0xffffffff;
			if (bEnvMap)
			{
				pVert->tu2 = (fU + DAT_1007043c) * DAT_10070438;
				pVert->tv2 = (fV + DAT_10070440) * DAT_10070444;
			}
			else
			{
				pVert->tu = (fU + DAT_1007043c) * DAT_10070438;
				pVert->tv = (fV + DAT_10070440) * DAT_10070444;
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

// ---------------------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------------------

// NAME: d3d_DrawPolyGrid: Jupiter drawpolygrid.cpp (names_proposal.csv, high).  Talon signature: cdecl (ViewParams *, LTObject *), the
// BaseObjectSet draw callback type (DrawObjectFn).
//
// Structure (everything below follows the exe's code): blend states (d3d_GetBlendStates + four StateSets) -> frustum test of the bounding
// sphere (6 planes of ViewParams, gives the clip mask) -> texture binding (base texture on stage 0, or on stage 1 with the linked
// texture = environment map on stage 0, d3d_SetTexture expanded in place) -> texture coordinate terms -> object transform x centering
// translation (d3d_SetupTransformation, LTMatrix::Identity, MatMul) -> 0x28-byte vertex list (FUN_1002aff0_inl1/2) -> optional
// environment map coordinates (FUN_1002c840) -> either the clipped path (view transform, per triangle FUN_1002c560 classification,
// partial triangles copied and clipped by the 0x28-byte plane clippers, then FUN_1002cf10 projection + DrawIndexedPrimitive) or the
// unclipped path (one combined matrix, DrawIndexedPrimitive) -> StateSet destructors restore the blend states.
//
// STUB: best effort, 5376 of 5488 bytes; build.py diff -a: 1331 instruction mismatches, 734 ignoring stack offsets (1406 vs 1378 instructions).  Remaining
// differences, in order of importance:
//  1. Inline budget: the exe calls the out-of-line LTMatrix::operator* (0x10016180) for the unclipped path's `mFull = proj * view`, ours
//     expands it (its nested MatMul stays a call).  tools/inline_budget.py (needs VC6CL=toolsc6cl_d3dren.bat and DX8INC in the
//     environment) predicts every other out-of-line decision of the exe correctly for this source (B(F) = 3880u, vertex helpers inl1/inl2
//     inlined with the LTVector operators inside them refused, both MatVMul_InPlace_H calls refused) but leaves 195u at the operator*
//     site where the exe needs < 43u: the original has 153..195u more charged inline cost (or that much less own size) than this
//     source and the inline helpers whose bodies the source copies from Jupiter (d3d_GetBlendStates 154u, d3d_SetupTransformation 306u,
//     d3d_SetTexture 263u) may be sized differently there.  Not fixed with ballast.
//  2. Register allocation: the exe keeps the constant 0 in edi and nDestBlend in ebx, ours the reverse (ebp = pGrid, esi = nSrcBlend agree),
//     which shifts every use of both registers; stack frame 0x238 in the exe, smaller here; the StateSet objects sit at 0x58/0x60/0x68/0x70
//     (FogColor, Dest, Src, Fog) in the exe, in declaration order here.
//  3. x87 scheduling of the 12 products of the inline MatMul (the exe computes [1][0], [2][0], [0][1].. [2][3] and finally [0][0], [0][3];
//     the element copies of GetBasisVectors/GetTranslation sit between them) and of the integer stores around the texture coordinate terms.
//  Not source-explainable by this author: 1.  2./3. are probably consequences of 1. plus the exact declaration/statement order of the
//  original; tools/permute.py (30 min, 4 jobs) found nothing better than semantic-breaking mutations.
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
	int i;

	d3d_GetBlendStates(pGrid, nSrcBlend, nDestBlend, nFog, nFogColor);
	StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, nSrcBlend);
	StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, nDestBlend);
	StateSet ssFog(D3DRENDERSTATE_FOGENABLE, nFog);
	StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, nFogColor);

	// Make sure it's initialized.
	if (!pGrid->m_Data)
		return;

	// Cull the grid's bounding sphere against the view frustum.
	LTVector vHalf((float)pGrid->m_Width * 0.5f * pGrid->m_Scale.x, pGrid->m_Scale.y * 128.0f, (float)pGrid->m_Height * 0.5f * pGrid->m_Scale.z);
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
			if (DAT_1005de2c && g_pBoundTextures[0] && pTex->m_pLinkedTexture && pTex->m_eTexType &&
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
				DAT_10063c90.FUN_10021da6();
				if (pTracker->m_pCurFrame->m_pTex->m_pStateChange)
					DAT_10063c90.FUN_10021db7(pTracker->m_pCurFrame->m_pTex->m_pStateChange, nStage);
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
	DAT_1007043c = pGrid->m_xPan * DAT_10061810[nStage].m_Unk00;
	DAT_10070440 = pGrid->m_yPan * DAT_10061810[nStage].m_Unk04;
	DAT_10070438 = (1.0f / pGrid->m_xScale) * pGrid->m_Scale.x * DAT_10061810[nStage].m_Unk00;
	DAT_10070444 = (1.0f / pGrid->m_yScale) * DAT_10061810[nStage].m_Unk04 * pGrid->m_Scale.z;
	bClip = (nClipFlags & 0x3f) != 0;
	DAT_10070438 = ((float)pGrid->m_Width / (int)(pGrid->m_Width - 1)) * DAT_10070438;
	DAT_10070444 = ((float)pGrid->m_Height / (int)(pGrid->m_Height - 1)) * DAT_10070444;
	DAT_10070854 = (float)pGrid->m_ColorA * 0.003921569f;

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
		FUN_1002aff0_inl1(pGrid, pVerts, vOrigin, vXInc, vYAxis, vZInc, bEnvMap);
	else
		FUN_1002aff0_inl2(pGrid, pVerts, vOrigin, vXInc, vYAxis, vZInc, bEnvMap);

	// The environment map texture coordinates (first stage of the 2-stage setup).
	if (bEnvMap)
	{
		FUN_1002c840(pGrid, &vYAxis, &vXInc, &vZInc);
		d3d_SetEnvMapTextureStates(pTracker->m_pCurFrame->m_pTex->m_eTexType);
	}

	if (bClip)
	{
		// Transform to view space, clip the triangles that are not completely inside, project the rest.
		if (pGrid->m_nIndices > DAT_1007044c)
		{
			DAT_10070858 = (uint16 *)dalloc(pGrid->m_nIndices * 2);
			if (!DAT_10070858)
			{
				DAT_1007044c = 0;
				return;
			}
			DAT_1007044c = pGrid->m_nIndices;
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
		uint16 *pOut = DAT_10070858;

		while (nRows--)
		{
			uint16 *pIndex = pRowIndex;
			uint32 nTris = nTrisPerRow;

			while (nTris--)
			{
				uint32 nTriFlags;
				int nResult = FUN_1002c560(nClipFlags, &nTriFlags,
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
					UnkType_PGVertex aVerts[3];
					UnkType_TLVertex40 *pClipOut = (UnkType_TLVertex40 *)g_pClipScratchVerts;
					UnkType_TLVertex40 *pIn = (UnkType_TLVertex40 *)aVerts;
					int nVerts = 3;
					uint32 nFlags = nTriFlags;
					char bUnused0, bUnused1, bUnused2, bUnused3, bUnused4, bUnused5;

					aVerts[0] = *(UnkType_PGVertex *)&pVerts[pIndex[0]];
					aVerts[1] = *(UnkType_PGVertex *)&pVerts[pIndex[1]];
					aVerts[2] = *(UnkType_PGVertex *)&pVerts[pIndex[2]];

					// The clip of FUN_10008779 written out in place (when Direct3D clips the sides only the near plane is done here).
					if (g_CV_UseD3DClip.m_IntVal == 0 || (nFlags &= 1) != 0)
					{
						if (((nFlags & 1) && !FUN_100088ec(&bUnused0, &pIn, &nVerts, &pClipOut)) ||
							((nFlags & 4) && !FUN_10008a23(&bUnused1, &pIn, &nVerts, &pClipOut)) ||
							((nFlags & 8) && !FUN_10006e40(&bUnused2, &pIn, &nVerts, &pClipOut)) ||
							((nFlags & 0x10) && !FUN_10007100(&bUnused3, &pIn, &nVerts, &pClipOut)) ||
							((nFlags & 0x20) && !FUN_100073b0(&bUnused4, &pIn, &nVerts, &pClipOut)) ||
							((nFlags & 2) && !FUN_10007670(&bUnused5, &pIn, &nVerts, &pClipOut)))
							goto NextTri;
					}

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

		if ((int)((char *)pOut - (char *)DAT_10070858) > 0)
		{
			FUN_1002cf10(pVerts, nTotal);
			g_pD3DDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0x2c4, pVerts, nTotal, DAT_10070858, pOut - DAT_10070858, 0);
		}
	}
	else
	{
		LTMatrix mFull = pParams->m_DeviceTimesProjection * pParams->m_mClipTransform;
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
int FUN_1002c560(uint32 nClipFlags, uint32 *pOutFlags, float *pV0, float *pV1, float *pV2)
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
void FUN_1002c840(LTPolyGrid *pGrid, LTVector *pYAxis, LTVector *pXAxis, LTVector *pZAxis)
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
			FUN_1001085e((LTVector *)&g_ViewParams.m_Pos, &pCur->m_Vec, &vNormal, &pCur->tu, &pCur->tv);

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
	if (DAT_10048758)
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
	if (DAT_10048758)
	{
		d3d_GetVisibleSet()->m_TranslucentPolyGrids.Draw(&g_ViewParams, FUN_1002cdf0);
	}
}

// BaseObjectSet::Draw callback of the translucent set: queues the grid in the sorted translucent object list.
// FUNCTION: D3DREN 0x1002cdf0
void FUN_1002cdf0(ViewParams *pParams, LTObject *pObj)
{
	DAT_1006b934->Add(pObj, d3d_DrawPolyGrid);
}

// guess: ObjectHandler[OT_POLYGRID].m_ModuleInit
// FUNCTION: D3DREN 0x1002ce10
void FUN_1002ce10()
{
	g_TriVertList = 0;
	g_TriVertListSize = 0;
	DAT_10070858 = 0;
	DAT_1007044c = 0;
}

// NAME: d3d_TermPolyGridDraw: Jupiter drawpolygrid.cpp (names_proposal.csv, medium); ObjectHandler[OT_POLYGRID].m_ModuleTerm.
// FUNCTION: D3DREN 0x1002ce30
void d3d_TermPolyGridDraw()
{
	dfree(g_TriVertList);
	dfree(DAT_10070858);

	g_TriVertList = 0;
	g_TriVertListSize = 0;
	DAT_10070858 = 0;
	DAT_1007044c = 0;
}

// guess: projects the 0x28-byte vertices (x', y', z', rhw) through the matrix at g_ViewParams.m_DeviceTimesProjection.m[0][0] (== g_ViewParams.m_Unk19c): the x, y and z of each
// vertex are divided by the w of the transformed position.
// FUNCTION: D3DREN 0x1002cf10
void FUN_1002cf10(UnkType_TLVertex40 *pVerts, int nVerts)
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
