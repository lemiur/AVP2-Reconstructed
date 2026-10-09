// d3d.ren sys/d3d/drawsprite (0x1002d640-0x1002f330): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// FLAGS: /O2 /Ob2
// unit unk/1002d080 (0x1002d080-0x10030bb0): three speed objects (16-byte aligned COMDATs, /O2 /Ob2), LOW unit boundary confidence in
// units.csv, but names_proposal.csv / NAMING.md give them as the TU table does, in link order:
//   0x1002d080-0x1002d63f  drawsky     (Jupiter render_a/src/sys/d3d/drawsky.cpp: d3d_DrawSkyExtents; AllSkyPortals console variable)
//   0x1002d640-0x1002f32f  drawsprite  (Jupiter drawsprite.cpp)
//   0x1002f330-0x10030baf  drawworldmodel (Jupiter drawworldmodel.cpp; DrawWorldModels console variable)
// NAME: the three object names are from names_proposal.csv (source_file column) / NAMING.md section 4; the static-initialiser numbers
// below are those of this unit's own compile (the originals restart in each object).
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/drawsky.h"
#include "d3dren/scenedesc.h"
#include "counter.h"
#include "sprite.h"
#include "d3dren/lightmap.h"
#include "d3dren/pool.h"
#include "d3dren/fixedpoint.h"
#include "de_mainworld.h"
#include "d3dren/polydraw.h"
#include "d3dren/tlvertex.h"
#include "ltmatrix.h"
#include "d3dren/3d_ops.h"

// A class with an empty constructor: a file-scope object of it gets an empty static initialiser (a lone `ret`, as at the start of
// each of the three objects below).  Same idea as setupmodel.h's UnkType_EmptyCtor of package W4.
struct UnkType_EmptyCtorW6
{
	UnkType_EmptyCtorW6() {}
	int m_Unk00;
};

// ---- drawsprite ----------------------------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1002d640 _$E2
static UnkType_EmptyCtorW6 s_Empty2;
// FUNCTION: D3DREN 0x1002d650 _$E5
static UnkType_EmptyCtorW6 s_Empty3;

// NAME: d3d_ProcessSprite: Jupiter drawsprite.cpp (names_proposal.csv, high): g_ObjectHandlers[OT_SPRITE].m_ProcessObjectFn; Jupiter
// adds to one set, the Talon version to the NOZ set when FLAG_SPRITE_NOZ is set (BaseObjectSet::Add expanded inline)
// FUNCTION: D3DREN 0x1002e9f0
void d3d_ProcessSprite(LTObject *pObject)
{
	VisibleSet *pVisibleSet = d3d_GetVisibleSet();
	BaseObjectSet *pSet = (pObject->m_Flags & FLAG_SPRITE_NOZ) ? &pVisibleSet->m_NoZSprites : &pVisibleSet->m_TranslucentSprites;
	pSet->Add(pObject);
}

void d3d_DrawSprite(ViewParams *pParams, LTObject *pObject);
void d3d_QueueSprite(ViewParams *pParams, LTObject *pObject);

// NAME: d3d_QueueTranslucentSprites: Jupiter drawsprite.cpp (names_proposal.csv, medium; no arguments in Talon)
// FUNCTION: D3DREN 0x1002ea40
void d3d_QueueTranslucentSprites()
{
	if (g_DrawSprites)	// the DrawSprites console variable's mirror
	{
		BaseObjectSet *pSet = &d3d_GetVisibleSet()->m_TranslucentSprites;
		pSet->Draw(&g_ViewParams, d3d_QueueSprite);
	}
}

// guess: BaseObjectSet::Draw callback that queues the sprite in the sorted list of translucent objects
// FUNCTION: D3DREN 0x1002ea70
void d3d_QueueSprite(ViewParams *pParams, LTObject *pObject)
{
	g_pTranslucentObjectDrawList->Add(pObject, d3d_DrawSprite);
}


// NAME: d3d_DrawNoZSprites: Jupiter drawsprite.cpp (names_proposal.csv, high)
// FUNCTION: D3DREN 0x1002ea90
void d3d_DrawNoZSprites()
{
	if (g_DrawSprites)
	{
		BaseObjectSet *pSet = &d3d_GetVisibleSet()->m_NoZSprites;
		if (pSet->m_nObjects > 0)
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, 0);
			pSet->Draw(&g_ViewParams, d3d_DrawSprite);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, g_DefaultZEnableState);
		}
	}
}


// guess: Jupiter polyclip.h's clipper dispatch as an inline function of the original (the exe expands it in several of this unit's
// functions; unit unk/100098d0 has the out-of-line copy ClipPoly): the polygon *ppVerts / *pnVerts is clipped against the planes
// of nFlags; with the UseD3DClip console variable set only the near plane is.
static inline int ClipPoly_Inline(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	TLVertex *pOut;
	TLVertex *pVerts;
	int nVerts;
	char c0, c1, c2, c3, c4, c5;

	if (g_CV_UseD3DClip.m_IntVal)
	{
		nFlags &= 1;
		if (!nFlags)
			return 1;
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
		return 1;
	}
	return 0;
}


// guess: the two sprite drawers: the camera facing one (unit-internal, 0x1002d860) and the rotatable one (0x1002e310); both take the
// view parameters, the sprite, its position (&m_Pos), its x and y scale and the frame's texture
void d3d_DrawSprite_NonRotatable(ViewParams *pParams, SpriteInstance *pInstance, LTVector *pPos, float fScaleX, float fScaleY, SharedTexture *pTexture);
void d3d_DrawRotatableSprite(ViewParams *pParams, SpriteInstance *pInstance, LTVector *pPos, float fScaleX, float fScaleY, SharedTexture *pTexture);

// NAME: d3d_DrawSprite: Jupiter drawsprite.cpp d3d_DrawSprite(Params, pObj) (names_proposal.csv, high): the BaseObjectSet / ObjectDrawList
// callback of the sprite sets.  The Talon body inlines Jupiter's d3d_GetBlendStates (d3d_draw.h) and four StateSets before it picks the
// rotatable or the camera facing drawer.
// FUNCTION: D3DREN 0x1002d660
void d3d_DrawSprite(ViewParams *pParams, LTObject *pObject)
{
	SpriteInstance *pInstance = (SpriteInstance *)pObject;
	SpriteTracker *pTracker = (SpriteTracker *)pInstance->m_SpriteTracker;
	SpriteAnim *pAnim = pTracker->m_pCurAnim;
	SpriteEntry *pFrame = pTracker->m_pCurFrame;
	DWORD dwFogColor, dwFog, srcBlend, destBlend;

	if (pAnim && pFrame && pFrame->m_pTex)
	{
		g_ClipFlags = 0x3f;

		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGCOLOR, &dwFogColor);
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, &dwFog);
		if ((pObject->m_Flags & FLAG_FOGDISABLE) && pObject->m_ObjectType != OT_MODEL)
			dwFog = 0;

		if (pObject->m_Flags2 & FLAG2_ADDITIVE)
		{
			srcBlend = D3DBLEND_ONE;
			destBlend = D3DBLEND_ONE;
			dwFogColor = 0;
		}
		else if (pObject->m_Flags2 & FLAG2_MULTIPLY)
		{
			srcBlend = D3DBLEND_ZERO;
			destBlend = D3DBLEND_SRCCOLOR;
			dwFogColor = 0xffffffff;
		}
		else
		{
			srcBlend = D3DBLEND_SRCALPHA;
			destBlend = D3DBLEND_INVSRCALPHA;
		}

		StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, srcBlend);
		StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, destBlend);
		StateSet ssFog(D3DRENDERSTATE_FOGENABLE, dwFog);
		StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, dwFogColor);

		if (pObject->m_Flags & FLAG_ROTATEABLESPRITE)
			d3d_DrawRotatableSprite(pParams, pInstance, &pInstance->m_Pos, pInstance->m_Scale.x, pInstance->m_Scale.y, pFrame->m_pTex);
		else
			d3d_DrawSprite_NonRotatable(pParams, pInstance, &pInstance->m_Pos, pInstance->m_Scale.x, pInstance->m_Scale.y, pFrame->m_pTex);
	}
}

// ---- drawsprite: the vertex helpers --------------------------------------------------------------------------------------------------

// guess: projects nVerts 0x20-byte vertices (x, y, z at +0) through the matrix pMat with the homogeneous divide (the SDK's
// MatVMul_InPlace_H inline, three sums per component and a temporary); only d3d_DrawRotatableSprite calls it
// FUNCTION: D3DREN 0x1002eaf0
void TransformVertexPositionsHomogeneous(TLVertex *pVerts, int nVerts, LTMatrix *pMat)
{
	while (nVerts != 0)
	{
		MatVMul_InPlace_H(pMat, &pVerts->m_Vec);
		pVerts++;
		nVerts--;
	}
}

// ---- drawworldmodel: world polygon drawing ------------------------------------------------------------------------------------------

// the polygon's surface (WorldPoly::m_pSurface is a void * in de_objects.h)
#define POLY_SURFACE(p)		((Surface *)(p)->m_pSurface)
// guess: the poly's frame tag (the tagging code sets it to the frame code of the frame the poly was seen in)
#define WORLDPOLY_FRAMECODE(p)	(*(uint16 *)((uint8 *)(p) + 0x46))

// ---- d3d_DrawSolidWorldModel ----------------------------------------------------------------------------------------------------------


// ---- d3d_DrawTranslucentWorldPoly ----------------------------------------------------------------------------------------------------

#define WORLDPOLY_LIGHTS(p)	((UnkType_PolyLightRef *)*(uint32 *)((uint8 *)(p) + 0x30))
// GLOBAL: D3DREN 0x100577b8
extern uint16 g_CurTextureFrameCode;		// guess: the frame code a texture is stamped with when it is used (SharedTexture::m_Unknown30)

void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGB *pRGB);			// 0x1000c860 (W4, unit unk/1000c860: the light grid lookup)
RTexture *d3d_CreateAndLoadTexture(SharedTexture *pTexture, uint32 nStage, uint8 bChild);		// 0x1001fff0 (d3d_texture): finds or creates the RTexture for the stage

// ---- d3d_ClipSprite ------------------------------------------------------------------------------------------------------------------

// Jupiter polyclip.h T::ClipExtra for TLVertex (unit unk/10001000 has the out-of-line copy TLVertex_ClipExtra; d3d_ClipSprite has it expanded)
static inline void TLVertex_ClipExtra(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}

// NAME: d3d_ClipSprite: Jupiter drawsprite.cpp template d3d_ClipSprite<T>(pInstance, hPoly, ppPoints, pnPoints, pOut), here the TLVertex
// instance (names_proposal.csv, high): the viewer must be in front of the clipper poly (hPoly); the sprite polygon is clipped on every
// edge plane of that poly (Jupiter polyclip.h expanded once per edge: CLIPTEST = DistTo(point) > 0, DOCLIP = plane intersection)
// STUB diagnosis: 1120 bytes like the exe, 412 vs 413 instructions.  The poly lookup is an inline helper returning NULL (the
// renderer's form of the engine's cm_GetPolyFromHPoly, clientde_impl.cpp): both failures then share the `!pPoly` return and the exe's
// separate return epilogues after each guard come out.  The clip loop counters are signed (jle/jl).  Left: register and frame-slot
// allocation (the exe keeps pPoly in esi with its home at [ebp-0x24], ours edi / [ebp-0x68]) and the schedule of the edge-plane code.
// guess: d3d_GetPolyFromHPoly (invented name, after the engine's cm_GetPolyFromHPoly).
static inline WorldPoly *d3d_GetPolyFromHPoly(MainWorld *pWorld, HPOLY hPoly)
{
	uint32 iModel;
	WorldData *pWorldData;

	iModel = hPoly >> 16;
	if (iModel >= pWorld->m_WorldModels.GetSize())
		return LTNULL;

	pWorldData = pWorld->m_WorldModels[iModel];
	if (!pWorldData)
		return LTNULL;

	return pWorldData->m_pOriginalBsp->GetPolyFromHPoly(hPoly);
}

// STUB: D3DREN 0x1002ebb0
int d3d_ClipSprite(SpriteInstance *pInstance, HPOLY hPoly, TLVertex **ppPoints, uint32 *pnPoints, TLVertex *pOut)
{
	LTPlane thePlane;
	float dot, d1, d2;
	SPolyVertex *pPrevPoint, *pCurPoint, *pEndPoint;
	LTVector vecTo;
	TLVertex *pVerts;
	int nVerts;
	WorldPoly *pPoly;

	if (!g_pFrameMainWorld)
		return 0;

	// Get the correct poly.
	pPoly = d3d_GetPolyFromHPoly(g_pFrameMainWorld, hPoly);
	if (!pPoly)
		return 0;

	// First see if the viewer is on the frontside of the poly.
	dot = pPoly->GetPlane()->DistTo(g_ViewParams.m_Pos);
	if (dot <= 0.01f)
		return 0;

	pVerts = *ppPoints;
	nVerts = *pnPoints;

	// Clip on each edge plane.
	pEndPoint = &((SPolyVertex *)(pPoly + 1))[pPoly->m_nVertices];
	pPrevPoint = pEndPoint - 1;
	for (pCurPoint = (SPolyVertex *)(pPoly + 1); pCurPoint != pEndPoint; )
	{
		VEC_SUB(vecTo, *pCurPoint->m_Vec, *pPrevPoint->m_Vec);
		VEC_CROSS(thePlane.m_Normal, vecTo, pPoly->GetPlane()->m_Normal);
		VEC_NORM(thePlane.m_Normal);
		g_nPlaneClipTests++;
		thePlane.m_Dist = VEC_DOT(thePlane.m_Normal, *pCurPoint->m_Vec);

		{
			int bInside[50], *pInside;
			uint32 nInside = 0;
			TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
			int iPrev, iCur;
			float t;

			pCur = pVerts;
			pEnd = pCur + nVerts;
			pInside = bInside;
			while (pCur != pEnd)
			{
				*pInside = (thePlane.DistTo(pCur->m_Vec) > 0.0f);
				nInside += *pInside;
				++pInside;
				++pCur;
			}

			if (nInside == 0)
			{
				return 0;
			}
			else if (nInside != nVerts)
			{
				pOldOut = pOut;

				iPrev = nVerts - 1;
				pPrev = pVerts + iPrev;
				for (iCur = 0; iCur < nVerts; iCur++)
				{
					pCur = pVerts + iCur;

					if (bInside[iPrev])
						*pOut++ = *pPrev;

					if (bInside[iPrev] != bInside[iCur])
					{
						d1 = thePlane.DistTo(pPrev->m_Vec);
						d2 = thePlane.DistTo(pCur->m_Vec);
						t = -d1 / (d2 - d1);
						pOut->m_Vec.x = pPrev->m_Vec.x + ((pCur->m_Vec.x - pPrev->m_Vec.x) * t);
						pOut->m_Vec.y = pPrev->m_Vec.y + ((pCur->m_Vec.y - pPrev->m_Vec.y) * t);
						pOut->m_Vec.z = pPrev->m_Vec.z + ((pCur->m_Vec.z - pPrev->m_Vec.z) * t);

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
		}

		pPrevPoint = pCurPoint;
		++pCurPoint;
	}

	*ppPoints = pVerts;
	*pnPoints = nVerts;
	return 1;
}

// ---- the biased sprite projection ----------------------------------------------------------------------------------------------------


// guess: transforms the 0x20-byte vertices *ppVerts (*pnVerts of them) to camera space, clips them against the planes of the current
// clip mask (g_ClipFlags; 0 when nothing is left) and projects them to the screen; the z (and the reciprocal w) is that of the vertex
// moved fBias along z, but not in front of the near plane (the sprite bias: Jupiter SPRITE_POSITION_ZBIAS).  The fourth argument is not
// used (the callers pass 0, as for d3d_ClipAndProjectTLVertices, its sibling without the bias).
// NAME: guess_d3d_ProjectBiasedSpriteVerts (names_proposal.csv, low)
// STUB diagnosis (W6): 784 vs 800 bytes.  The exe expands MatVMul_InPlace (one temporary: y goes through the stack, x and z stay on the
// x87 stack, z/x/y term order) in the first loop; ours calls the SDK's out-of-line MatVMul (same finding as W2's TransformPositionInPlace: no
// source form of the SDK call or of an explicit expansion gives the exe's code).  The clip dispatch and the biased projection loop
// are the exe's.
// STUB: D3DREN 0x1002f010
int ClipAndProjectPolyWithDepthBias(TLVertex **ppVerts, int *pnVerts, ViewParams *pParams, int a4, float fBias)
{
	TLVertex *pVert;
	int i;

	pVert = *ppVerts;
	for (i = *pnVerts; i != 0; i--)
	{
		MatVMul_InPlace(&pParams->m_mClipTransform, &pVert->m_Vec);
		pVert++;
	}

	if (g_ClipFlags != 0)
	{
		if (!ClipPoly_Inline(g_ClipFlags, ppVerts, pnVerts))
			return 0;
	}

	pVert = *ppVerts;
	for (i = *pnVerts; i != 0; i--)
	{
		LTMatrix *pMat = &g_ViewParams.m_DeviceTimesProjection;
		LTVector vProj, vProjBiased, vBiased;
		float fBiasZ = fBias;
		float fW, fWBiased;

		if (fBias + pVert->m_Vec.z < g_CV_NearZ.m_FloatVal)
			fBiasZ = g_CV_NearZ.m_FloatVal - pVert->m_Vec.z;

		fW = MatVMul_H(&vProj, pMat, &pVert->m_Vec);
		vBiased = pVert->m_Vec;
		vBiased.z += fBiasZ;
		fWBiased = MatVMul_H(&vProjBiased, pMat, &vBiased);
		pVert->m_Vec.x = vProj.x;
		pVert->m_Vec.y = vProj.y;
		pVert->rhw = fWBiased;
		pVert->m_Vec.z = vProjBiased.z;
		pVert++;
	}
	return 1;
}

// ---- d3d_DrawSprite (the camera facing sprite) ----------------------------------------------------------------------------------------


void d3d_CalcLightAdd(LTObject *pObject, LTVector *pLightAdd);		// 0x1000f270 (unit unk/1000f160)

// the sprite size / bias constants of Jupiter drawsprite.cpp
#define SPRITE_POSITION_ZBIAS	-20.0f
#define SPRITE_MINFACTORDIST	10.0f
#define SPRITE_MAXFACTORDIST	500.0f
#define SPRITE_MINFACTOR		0.1f
#define SPRITE_MAXFACTOR		2.0f

// guess: the d3d_SetTexture sequence the exe has expanded in the sprite and world poly draw functions (W2's d3d_SetTexture, 0x100079e4:
// finds or creates the RTexture of pTexture for the stage, binds it, resets its LOD); false when there is no texture or no RTexture
// could be made
static inline int SpriteSetTexture(SharedTexture *pTexture, uint32 nStage)
{
	RTexture *pRTexture;
	RTexture *pFirst;

	if (!pTexture)
		return 0;

	pFirst = (RTexture *)pTexture->m_pRenderData;
	pTexture->m_Unknown30 = g_CurTextureFrameCode;
	for (pRTexture = pFirst; pRTexture; pRTexture = pRTexture->m_Unk30)
	{
		if (pRTexture->m_Unk42 == (uint8)nStage)
			break;
	}

	if (pRTexture && pRTexture == (RTexture *)g_pBoundTextures[nStage])
	{
	}
	else
	{
		if (!pRTexture)
		{
			if (!pFirst)
			{
				pRTexture = d3d_CreateAndLoadTexture(pTexture, nStage, 0);
				if (!pRTexture)
					return 0;
			}
			else
			{
				pRTexture = d3d_CreateAndLoadTexture(pTexture, nStage, 1);
				if (!pRTexture)
					return 0;
				pRTexture->m_Unk30 = pFirst->m_Unk30;
				pFirst->m_Unk30 = pRTexture;
			}
		}
		d3d_BindRTexture(pRTexture);
	}

	if (pRTexture->m_Unk44 != 0)
	{
		pRTexture->m_Data.m_pSurface->SetLOD(0);
		pRTexture->m_Unk44 = 0;
	}
	return 1;
}

// guess: the colour of a sprite: its colour bytes times the ambient world colour, plus (unless FLAG_NOLIGHT) the light grid sample at
// its position and the dynamic lights (d3d_CalcLightAdd), clamped to 0..255; Jupiter drawsprite.cpp d3d_GetSpriteColor.  An inline
// function of the original (expanded in both sprite draw functions).
static inline uint32 SpriteGetColor(SpriteInstance *pInstance)
{
	TLRGB color;

	color.a = pInstance->m_ColorA;
	if (!(pInstance->m_Flags & FLAG_NOLIGHT) && g_pFrameMainWorld)
	{
		LTRGB lightRGB;
		LTVector vAdd;
		LTVector c;

		w_GetLightVal(&g_pFrameMainWorld->m_LightTable, &pInstance->m_Pos, &lightRGB);
		d3d_CalcLightAdd(pInstance, &vAdd);

		c.x = (float)lightRGB.r * 0.003921569f * ((float)pInstance->m_ColorR * g_GlobalVertexTint.x + vAdd.x);
		if (c.x >= 0.0f)
		{
			if (c.x > 255.0f)
				c.x = 255.0f;
		}
		else
			c.x = 0.0f;

		c.y = (float)lightRGB.g * 0.003921569f * ((float)pInstance->m_ColorG * g_GlobalVertexTint.y + vAdd.y);
		if (c.y >= 0.0f)
		{
			if (c.y > 255.0f)
				c.y = 255.0f;
		}
		else
			c.y = 0.0f;

		c.z = (float)lightRGB.b * 0.003921569f * ((float)pInstance->m_ColorB * g_GlobalVertexTint.z + vAdd.z);
		if (c.z >= 0.0f)
		{
			if (c.z > 255.0f)
				c.z = 255.0f;
		}
		else
			c.z = 0.0f;

		color.r = (uint8)RoundFloatToInt(c.x);
		color.g = (uint8)RoundFloatToInt(c.y);
		color.b = (uint8)RoundFloatToInt(c.z);
	}
	else
	{
		color.r = (uint8)RoundFloatToInt((float)pInstance->m_ColorR * g_GlobalVertexTint.x);
		color.g = (uint8)RoundFloatToInt((float)pInstance->m_ColorG * g_GlobalVertexTint.y);
		color.b = (uint8)RoundFloatToInt((float)pInstance->m_ColorB * g_GlobalVertexTint.z);
	}
	return *(uint32 *)&color;
}

// NAME: guess_d3d_DrawSprite_NonRotatable (names_proposal.csv, medium): Jupiter's static d3d_DrawSprite(Params, pInstance, pShared)
// overload: the sprite is a camera facing quad around pPos (m_Pos) of the size of its texture times fScaleX/fScaleY (and the glow
// factor with FLAG_GLOWSPRITE), lit like the other objects, optionally with its texture coordinates rotated by the object rotation
// (FLAG2_SPRITE_TROTATE), clipped to the clip mask, projected (with the z bias for FLAG_SPRITEBIAS) and drawn as a fan.
// STUB diagnosis (W6): written from the disassembly: 2720 vs 2736 bytes; the branch order of the REALLYCLOSE test is the exe's, the
// first 0x17f bytes line up, after that the block placement of the d3d_SetTexture expansion (the exe lays the `create + link` block
// before the `found` compare) and register/frame assignment shift everything (839 aligned mismatches).  Not iterated further.
// STUB: D3DREN 0x1002d860
void d3d_DrawSprite_NonRotatable(ViewParams *pParams, SpriteInstance *pInstance, LTVector *pPos, float fScaleX, float fScaleY, SharedTexture *pTexture)
{
	LTVector vCam;
	float fNearZ;
	uint32 nSpecular;
	TLVertex aVerts[4];
	TLVertex *pVerts;
	int nVerts;
	RTexture *pBound;
	float fWidth, fHeight, fHalfX, fHalfY;
	float uMin, uMax, vMin, vMax;
	uint32 nColor;
	float fSavedNearPlane;
	int i;

	if (pInstance->m_Flags & FLAG_REALLYCLOSE)
	{
		MatVMul(&vCam, &pParams->m_mReallyCloseClipTransform, pPos);
		fNearZ = g_CV_ReallyCloseNearZ.m_FloatVal;
	}
	else
	{
		MatVMul(&vCam, &pParams->m_mClipTransform, pPos);
		fNearZ = g_CV_NearZ.m_FloatVal;
	}

	g_pfnCalcFogAlpha(&pInstance->m_Pos, &nSpecular);
	if (vCam.z <= fNearZ)
		return;

	{
		LTVector vDelta(pInstance->m_Pos.x - pParams->m_Pos.x, pInstance->m_Pos.y - pParams->m_Pos.y, pInstance->m_Pos.z - pParams->m_Pos.z);
		vDelta.Mag();
	}

	if (!SpriteSetTexture(pTexture, g_NormalTextureStage))
		return;

	pBound = (RTexture *)g_pBoundTextures[g_NormalTextureStage];
	fWidth = (float)pBound->m_Data.GetBaseWidth();
	fHeight = (float)pBound->m_Data.GetBaseHeight();
	uMin = g_TextureStageTexelSizes[0].m_Unk00 + g_TextureStageTexelSizes[0].m_Unk00;
	uMax = (fWidth - 2.0f) * g_TextureStageTexelSizes[0].m_Unk00;
	vMin = g_TextureStageTexelSizes[0].m_Unk04 + g_TextureStageTexelSizes[0].m_Unk04;
	vMax = (fHeight - 2.0f) * g_TextureStageTexelSizes[0].m_Unk04;

	fHalfX = fWidth * pParams->m_fFovXScale * fScaleX;
	fHalfY = fHeight * pParams->m_fFovYScale * fScaleY;
	if (pInstance->m_Flags & FLAG_GLOWSPRITE)
	{
		float fFactor = (vCam.z - SPRITE_MINFACTORDIST) / (SPRITE_MAXFACTORDIST - SPRITE_MINFACTORDIST);
		if (fFactor >= 0.0f)
		{
			if (fFactor > 1.0f)
				fFactor = 1.0f;
		}
		else
			fFactor = 0.0f;
		fFactor = (SPRITE_MAXFACTOR - SPRITE_MINFACTOR) * fFactor + SPRITE_MINFACTOR;
		fHalfX *= fFactor;
		fHalfY *= fFactor;
	}

	nColor = SpriteGetColor(pInstance);

	aVerts[0].m_Vec.x = vCam.x - fHalfX;
	aVerts[0].m_Vec.y = vCam.y + fHalfY;
	aVerts[0].m_Vec.z = vCam.z;
	aVerts[0].color = nColor;
	aVerts[0].specular = nSpecular;
	aVerts[0].tu = uMin;
	aVerts[0].tv = vMin;
	aVerts[1].m_Vec.x = vCam.x + fHalfX;
	aVerts[1].m_Vec.y = vCam.y + fHalfY;
	aVerts[1].m_Vec.z = vCam.z;
	aVerts[1].color = nColor;
	aVerts[1].specular = nSpecular;
	aVerts[1].tu = uMax;
	aVerts[1].tv = vMin;
	aVerts[2].m_Vec.x = vCam.x + fHalfX;
	aVerts[2].m_Vec.y = vCam.y - fHalfY;
	aVerts[2].m_Vec.z = vCam.z;
	aVerts[2].color = nColor;
	aVerts[2].specular = nSpecular;
	aVerts[2].tu = uMax;
	aVerts[2].tv = vMax;
	aVerts[3].m_Vec.x = vCam.x - fHalfX;
	aVerts[3].m_Vec.y = vCam.y - fHalfY;
	aVerts[3].m_Vec.z = vCam.z;
	aVerts[3].color = nColor;
	aVerts[3].specular = nSpecular;
	aVerts[3].tu = uMin;
	aVerts[3].tv = vMax;

	if (pInstance->m_Flags2 & FLAG2_SPRITE_TROTATE)
	{
		float mRot[4][4];
		float fCenterU = (uMin + uMax) * 0.5f;
		float fCenterV = (vMin + vMax) * 0.5f;

		quat_ConvertToMatrix((float *)&pInstance->m_Rotation, mRot);
		for (i = 0; i < 4; i++)
		{
			float fDV = aVerts[i].tv - fCenterV;
			float fDU = aVerts[i].tu - fCenterU;
			aVerts[i].tu = fDV * mRot[1][0] + fDU * mRot[0][0] + fCenterU;
			aVerts[i].tv = fDV * mRot[1][1] + fDU * mRot[0][1] + fCenterV;
		}
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
	}

	fSavedNearPlane = g_ViewParams.m_NearZ;
	pVerts = aVerts;
	nVerts = 4;
	if (pInstance->m_Flags & FLAG_REALLYCLOSE)
		g_ViewParams.m_NearZ = g_CV_ReallyCloseNearZ.m_FloatVal;

	if (ClipPoly_Inline(g_ClipFlags, &pVerts, &nVerts))
	{
		if (!(pInstance->m_Flags & FLAG_SPRITEBIAS))
		{
			if (!(pInstance->m_Flags & FLAG_REALLYCLOSE))
			{
				for (i = nVerts; i != 0; i--)
					ProjectVertexToScreen(&pVerts[nVerts - i].m_Vec.x, &g_ViewParams);
			}
			else
			{
				for (i = 0; i < nVerts; i++)
				{
					LTMatrix *pMat = &g_ViewParams.m_DeviceTimesProjection;
					LTVector vProj, vProjBiased;
					float fW = MatVMul_H(&vProj, pMat, &pVerts[i].m_Vec);
					LTVector vBiased = pVerts[i].m_Vec;
					float fWBiased;

					vBiased.z += g_CV_NearZ.m_FloatVal;
					fWBiased = MatVMul_H(&vProjBiased, pMat, &vBiased);
					pVerts[i].m_Vec.x = vProj.x;
					pVerts[i].m_Vec.y = vProj.y;
					pVerts[i].rhw = fWBiased;
					pVerts[i].m_Vec.z = vProjBiased.z;
				}
			}
		}
		else
		{
			float fBias = SPRITE_POSITION_ZBIAS;
			if (SPRITE_POSITION_ZBIAS + vCam.z < g_CV_NearZ.m_FloatVal)
				fBias = g_CV_NearZ.m_FloatVal - vCam.z;

			for (i = 0; i < nVerts; i++)
			{
				LTMatrix *pMat = &g_ViewParams.m_DeviceTimesProjection;
				LTVector vProj, vProjBiased;
				float fW = MatVMul_H(&vProj, pMat, &pVerts[i].m_Vec);
				LTVector vBiased = pVerts[i].m_Vec;
				float fWBiased;

				vBiased.z += fBias;
				fWBiased = MatVMul_H(&vProjBiased, pMat, &vBiased);
				pVerts[i].m_Vec.x = vProj.x;
				pVerts[i].m_Vec.y = vProj.y;
				pVerts[i].rhw = fWBiased;
				pVerts[i].m_Vec.z = vProjBiased.z;
			}
		}

		g_TextureStateRestorer.RestoreAllStates();
		if (pTexture->m_pStateChange)
			g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, g_NormalTextureStage);
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
	}

	if (pInstance->m_Flags2 & FLAG2_SPRITE_TROTATE)
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);
	g_ViewParams.m_NearZ = fSavedNearPlane;
}

// ---- d3d_DrawRotatableSprite ------------------------------------------------------------------------------------------------------------

void d3d_DrawDevicePrimitive(D3DPRIMITIVETYPE type, DWORD dwVertexTypeDesc, LPVOID lpvVertices, DWORD dwVertexCount, DWORD dwFlags);	// 0x1000d340 (W4, unit unk/100098d0): DrawPrimitive wrapper

// the order the four corners of the rotatable sprite are stored in, for a viewer in front of / behind it (two int[4] tables at
// 0x1004bd9c and 0x1004bdac)
// GLOBAL: D3DREN 0x1004bd9c
static int s_CornerOrderFront[4] = { 0, 1, 2, 3 };
// GLOBAL: D3DREN 0x1004bdac
static int s_CornerOrderBack[4] = { 3, 2, 1, 0 };

// NAME: d3d_DrawRotatableSprite: Jupiter drawsprite.cpp d3d_DrawRotatableSprite (names_proposal.csv, high): the sprite quad (the size of its
// texture) is turned by the object rotation and scale, placed at the object position, optionally clipped to a clipper poly
// (d3d_ClipSprite), projected (with the z bias for FLAG_SPRITEBIAS) and drawn as a fan
// STUB diagnosis (W6): written from the disassembly: 1824 vs 1760 bytes (the exe's d3d_SetTexture expansion calls d3d_FindRTextureForStage for the
// chain walk and its TL vertex array is built differently); not iterated.
// STUB: D3DREN 0x1002e310
void d3d_DrawRotatableSprite(ViewParams *pParams, SpriteInstance *pInstance, LTVector *pPos, float fScaleX, float fScaleY, SharedTexture *pTexture)
{
	TLVertex aClipped[300];
	TLVertex aVerts[4];
	LTMatrix mRotation;
	LTVector vFacing, vForward;
	uint32 nSpecular;
	TLVertex *pVerts;
	uint32 nVerts;
	RTexture *pBound;
	float fWidth, fHeight;
	float uMin, uMax, vMin, vMax;
	uint32 nColor;
	const int *pOrder;
	int iVert;
	int bResult;

	{
		LTVector vDelta = pInstance->m_Pos - pParams->m_Pos;
		vDelta.Mag();
	}

	if (!SpriteSetTexture(pTexture, g_NormalTextureStage))
		return;

	g_TextureStateRestorer.RestoreAllStates();
	if (pTexture->m_pStateChange)
		g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, g_NormalTextureStage);

	pBound = (RTexture *)g_pBoundTextures[g_NormalTextureStage];
	fWidth = (float)(pBound->m_Data.GetBaseWidth() >> pTexture->m_Unknown3C);
	fHeight = (float)(pBound->m_Data.GetBaseHeight() >> pTexture->m_Unknown3C);

	g_pfnCalcFogAlpha(&pInstance->m_Pos, &nSpecular);

	uMin = g_TextureStageTexelSizes[0].m_Unk00 + g_TextureStageTexelSizes[0].m_Unk00;
	uMax = (fWidth - 2.0f) * g_TextureStageTexelSizes[0].m_Unk00;
	vMin = g_TextureStageTexelSizes[0].m_Unk04 + g_TextureStageTexelSizes[0].m_Unk04;
	vMax = (fHeight - 2.0f) * g_TextureStageTexelSizes[0].m_Unk04;

	d3d_SetupTransformation(&pInstance->m_Pos, (float *)&pInstance->m_Rotation, &pInstance->m_Scale, &mRotation);

	vForward.Init(0.0f, 0.0f, -1.0f);
	MatVMul_3x3(&vFacing, &mRotation, &vForward);

	// which side of the sprite the viewer is on decides the corner order (winding)
	pOrder = s_CornerOrderBack;
	if (0.0f <= vFacing.x * (pParams->m_Pos.x - pPos->x) + vFacing.y * (pParams->m_Pos.y - pPos->y) + vFacing.z * (pParams->m_Pos.z - pPos->z))
		pOrder = s_CornerOrderFront;

	nColor = SpriteGetColor(pInstance);

	iVert = pOrder[0];
	aVerts[iVert].m_Vec.x = fWidth;
	aVerts[iVert].m_Vec.y = fHeight;
	aVerts[iVert].m_Vec.z = 0.0f;
	aVerts[iVert].color = nColor;
	aVerts[iVert].specular = nSpecular;
	aVerts[iVert].tu = uMin;
	aVerts[iVert].tv = vMin;

	iVert = pOrder[1];
	aVerts[iVert].m_Vec.x = -fWidth;
	aVerts[iVert].m_Vec.y = fHeight;
	aVerts[iVert].m_Vec.z = 0.0f;
	aVerts[iVert].color = nColor;
	aVerts[iVert].specular = nSpecular;
	aVerts[iVert].tu = uMax;
	aVerts[iVert].tv = vMin;

	iVert = pOrder[2];
	aVerts[iVert].m_Vec.x = -fWidth;
	aVerts[iVert].m_Vec.y = -fHeight;
	aVerts[iVert].m_Vec.z = 0.0f;
	aVerts[iVert].color = nColor;
	aVerts[iVert].specular = nSpecular;
	aVerts[iVert].tu = uMax;
	aVerts[iVert].tv = vMax;

	iVert = pOrder[3];
	aVerts[iVert].m_Vec.x = fWidth;
	aVerts[iVert].m_Vec.y = -fHeight;
	aVerts[iVert].m_Vec.z = 0.0f;
	aVerts[iVert].color = nColor;
	aVerts[iVert].specular = nSpecular;
	aVerts[iVert].tu = uMin;
	aVerts[iVert].tv = vMax;

	TransformVertexPositionsHomogeneous(aVerts, 4, &mRotation);

	pVerts = aVerts;
	nVerts = 4;
	if (pInstance->m_ClipperPoly != INVALID_HPOLY)
	{
		if (!d3d_ClipSprite(pInstance, pInstance->m_ClipperPoly, &pVerts, &nVerts, aClipped))
			return;
	}

	if (!(pInstance->m_Flags & FLAG_SPRITEBIAS))
		bResult = d3d_ClipAndProjectTLVertices(&pVerts, (int *)&nVerts, &g_ViewParams, 0);
	else
		bResult = ClipAndProjectPolyWithDepthBias(&pVerts, (int *)&nVerts, &g_ViewParams, 0, SPRITE_POSITION_ZBIAS);

	if (bResult)
		d3d_DrawDevicePrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
}
