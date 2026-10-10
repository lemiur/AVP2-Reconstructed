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
#include "d3dren/d3d_texture.h"

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


// Inline helpers of ClipAndProjectPolyWithDepthBias and the sprite drawers (the exe expands them); their out-of-line copies are TransformPositionInPlace
// (0x10008719, unit unk/10007930) and ProjectPositionWithDepthBias (0x100062e0, unit unk/100062e0), which take float pointers.
// helper written for this decompilation (TransformPositionInPlace expanded): camera space transform of one position, in place.
static inline void TransformTLVertexInPlace(TLVertex *pVert, const float *pMatrix)
{
	float x = pMatrix[0] * pVert->m_Vec.x + pMatrix[1] * pVert->m_Vec.y + pMatrix[2] * pVert->m_Vec.z + pMatrix[3];
	float y = pMatrix[4] * pVert->m_Vec.x + pMatrix[5] * pVert->m_Vec.y + pMatrix[6] * pVert->m_Vec.z + pMatrix[7];
	float z = pMatrix[8] * pVert->m_Vec.x + pMatrix[9] * pVert->m_Vec.y + pMatrix[10] * pVert->m_Vec.z + pMatrix[11];
	pVert->m_Vec.z = z;
	pVert->m_Vec.x = x;
	pVert->m_Vec.y = y;
}

// helper written for this decompilation (ProjectPositionWithDepthBias expanded): projects the vertex in place; x and y use the
// unbiased w, z and rhw those of the position moved fZBias along z.  The bias parameter itself carries the biased z (a separate
// local gives the exe's code only out of line).
static inline void ProjectTLVertexWithDepthBias(TLVertex *pVert, float fZBias)
{
	LTVector result;
	float w = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][2] * pVert->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[3][0] * pVert->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pVert->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.x = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pVert->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pVert->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pVert->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * w;
	result.y = (g_ViewParams.m_DeviceTimesProjection.m[1][0] * pVert->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pVert->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pVert->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[1][3]) * w;
	fZBias += pVert->m_Vec.z;
	float w2 = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][2] * fZBias + g_ViewParams.m_DeviceTimesProjection.m[3][0] * pVert->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pVert->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.z = (g_ViewParams.m_DeviceTimesProjection.m[2][0] * pVert->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pVert->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[2][2] * fZBias + g_ViewParams.m_DeviceTimesProjection.m[2][3]) * w2;
	pVert->rhw = w2;
	pVert->m_Vec = result;
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

// ---- d3d_DrawSprite (the camera facing sprite) ----------------------------------------------------------------------------------------


void d3d_CalcLightAdd(LTObject *pObject, LTVector *pLightAdd);		// 0x1000f270 (unit unk/1000f160)

// the sprite size / bias constants of Jupiter drawsprite.cpp
// They are file constants, not literals: the exe loads them from .rdata (the argument -20.0 is pushed from memory) and computes
// MAXFACTORDIST - MINFACTORDIST and MAXFACTOR - MINFACTOR at run time, which VC6 does for `static const float` but folds for literals.
// GLOBAL: D3DREN 0x10046498
static const float SPRITE_MINFACTORDIST = 10.0f;
// GLOBAL: D3DREN 0x1004649c
static const float SPRITE_MAXFACTORDIST = 500.0f;
// GLOBAL: D3DREN 0x100464a0
static const float SPRITE_MINFACTOR = 0.1f;
// GLOBAL: D3DREN 0x100464a4
static const float SPRITE_MAXFACTOR = 2.0f;
// GLOBAL: D3DREN 0x100464a8
static const float SPRITE_POSITION_ZBIAS = -20.0f;

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

		c.x = ((float)pInstance->m_ColorR * g_GlobalVertexTint.x + vAdd.x) * ((float)lightRGB.r * 0.003921569f);
		if (c.x < 0.0f)
			c.x = 0.0f;
		else if (c.x > 255.0f)
			c.x = 255.0f;

		c.y = ((float)pInstance->m_ColorG * g_GlobalVertexTint.y + vAdd.y) * ((float)lightRGB.g * 0.003921569f);
		if (c.y < 0.0f)
			c.y = 0.0f;
		else if (c.y > 255.0f)
			c.y = 255.0f;

		c.z = ((float)pInstance->m_ColorB * g_GlobalVertexTint.z + vAdd.z) * ((float)lightRGB.b * 0.003921569f);
		if (c.z < 0.0f)
			c.z = 0.0f;
		else if (c.z > 255.0f)
			c.z = 255.0f;

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
// STUB diagnosis: 2736 bytes like the exe, 811 vs 799 instructions; the out-of-line call set is the exe's (LTVector constructor
// inside the inlined operator- and Mag of the unused distance, both bias projections inline).  It came from the sprite code's own
// helpers: TLVertex::SetTCoords for every tu/tv pair (also in the texture rotation loop, matched in ModelDraw::DrawFadeSprite),
// LTVector::Init for the corners, LTRotation::ConvertToMatrix into an LTMatrix for FLAG2_SPRITE_TROTATE, chained colour/specular
// stores (the exe stores each value to the four vertices in a row), and the base size read as unsigned (fild qword).
// Left: the frame is 4 bytes smaller (0x104 vs 0x108: vCam at ebp-0x40, the exe -0x44, and the slot order of the distance temporary,
// lightRGB and the colour terms differs); the exe lays the `create + link` block of the d3d_SetTexture expansion before the `found`
// compare (an inline search written `while (p) { if (match) return p; ... }` gives that order but breaks d3d_DrawPolyGrid, so the
// header form stays); the x87 operand order of the third MatVMul term.
// STUB: D3DREN 0x1002d860
void d3d_DrawSprite_NonRotatable(ViewParams *pParams, SpriteInstance *pInstance, LTVector *pPos, float fScaleX, float fScaleY, SharedTexture *pTexture)
{
	LTVector vCam;
	float fNearZ;
	uint32 nSpecular;
	TLVertex aVerts[4];
	TLVertex *pVerts;
	int nVerts;
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

	pInstance->m_Pos.Dist(pParams->m_Pos);

	if (!d3d_SetTexture(pTexture, g_NormalTextureStage, 0))
		return;

	fWidth = (float)(uint32)g_pBoundTextures[g_NormalTextureStage]->GetBaseWidth();
	fHeight = (float)(uint32)g_pBoundTextures[g_NormalTextureStage]->GetBaseHeight();
	uMin = g_TextureStageTexelSizes[0].m_Unk00 + g_TextureStageTexelSizes[0].m_Unk00;
	uMax = (fWidth - 2.0f) * g_TextureStageTexelSizes[0].m_Unk00;
	vMin = g_TextureStageTexelSizes[0].m_Unk04 + g_TextureStageTexelSizes[0].m_Unk04;
	vMax = (fHeight - 2.0f) * g_TextureStageTexelSizes[0].m_Unk04;

	fHalfX = fWidth * pParams->m_fFovXScale * fScaleX;
	fHalfY = fHeight * pParams->m_fFovYScale * fScaleY;
	if (pInstance->m_Flags & FLAG_GLOWSPRITE)
	{
		float fFactor = (vCam.z - SPRITE_MINFACTORDIST) / (SPRITE_MAXFACTORDIST - SPRITE_MINFACTORDIST);
		fFactor = LTCLAMP(fFactor, 0.0f, 1.0f);
		fFactor = SPRITE_MINFACTOR + ((SPRITE_MAXFACTOR - SPRITE_MINFACTOR) * fFactor);
		fHalfX *= fFactor;
		fHalfY *= fFactor;
	}

	nColor = SpriteGetColor(pInstance);

	aVerts[0].m_Vec.Init(vCam.x - fHalfX, vCam.y + fHalfY, vCam.z);
	aVerts[1].m_Vec.Init(vCam.x + fHalfX, vCam.y + fHalfY, vCam.z);
	aVerts[2].m_Vec.Init(vCam.x + fHalfX, vCam.y - fHalfY, vCam.z);
	aVerts[3].m_Vec.Init(vCam.x - fHalfX, vCam.y - fHalfY, vCam.z);

	aVerts[0].SetTCoords(uMin, vMin);
	aVerts[1].SetTCoords(uMax, vMin);
	aVerts[2].SetTCoords(uMax, vMax);
	aVerts[3].SetTCoords(uMin, vMax);

	aVerts[0].color = aVerts[1].color = aVerts[2].color = aVerts[3].color = nColor;
	aVerts[0].specular = aVerts[1].specular = aVerts[2].specular = aVerts[3].specular = nSpecular;

	if (pInstance->m_Flags2 & FLAG2_SPRITE_TROTATE)
	{
		LTMatrix mRot;
		float fCenterU = (uMin + uMax) * 0.5f;
		float fCenterV = (vMin + vMax) * 0.5f;

		pInstance->m_Rotation.ConvertToMatrix(mRot);
		for (i = 0; i < 4; i++)
		{
			float fDV = aVerts[i].tv - fCenterV;
			float fDU = aVerts[i].tu - fCenterU;
			aVerts[i].SetTCoords(fDV * mRot.m[1][0] + fDU * mRot.m[0][0] + fCenterU, fDV * mRot.m[1][1] + fDU * mRot.m[0][1] + fCenterV);
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
		if (pInstance->m_Flags & FLAG_SPRITEBIAS)
		{
			float fBias = SPRITE_POSITION_ZBIAS;
			if (SPRITE_POSITION_ZBIAS + vCam.z < g_CV_NearZ.m_FloatVal)
				fBias = g_CV_NearZ.m_FloatVal - vCam.z;

			for (i = 0; i < nVerts; i++)
				ProjectTLVertexWithDepthBias(&pVerts[i], fBias);
		}
		else if (pInstance->m_Flags & FLAG_REALLYCLOSE)
		{
			for (i = 0; i < nVerts; i++)
				ProjectTLVertexWithDepthBias(&pVerts[i], g_CV_NearZ.m_FloatVal);
		}
		else
		{
			TLVertex *pVert = pVerts;
			for (i = nVerts; i != 0; i--)
			{
				ProjectVertexToScreen(&pVert->m_Vec.x, &g_ViewParams);
				pVert++;
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

// the helpers of d3d_DrawRotatableSprite, defined after it (in the exe they follow the set callbacks)
void TransformVertexPositionsHomogeneous(TLVertex *pVerts, int nVerts, LTMatrix *pMat);
int d3d_ClipSprite(SpriteInstance *pInstance, HPOLY hPoly, TLVertex **ppPoints, uint32 *pnPoints, TLVertex *pOut);
int ClipAndProjectPolyWithDepthBias(TLVertex **ppVerts, int *pnVerts, ViewParams *pParams, int a4, float fBias);

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
// STUB diagnosis: 1824 vs 1760 bytes, 553 vs 538 instructions.  Evidence used: the exe calls LTVector::operator- and Mag out of line
// with the by-value copy of pParams->m_Pos (LTVector::Dist), MatVMul_3x3 followed by a copy back into vForward
// (MatVMul_InPlace_3x3), and computes the facing test from a by-value copy of *pPos (operator- / Dot inlined); the colour terms
// and the corner stores have the exe's shape.  Wall: inline call set (tools/inline_budget.py).  The exe calls operator- and Mag
// (nested in Dist), d3d_FindRTextureForStage (nested in d3d_SetTexture, after a `pTexture->m_pRenderData` test the caller makes)
// and TransformVertexPositionsHomogeneous out of line; ours inlines all four.  The model needs Dist's share < 62u (ours 165u:
// about 24 top-level inline sites after it, ours 8-10), d3d_FindRTextureForStage's limit < 51u (ours 112-142u) and 43u more
// charge before TransformVertexPositionsHomogeneous.  Measured, not kept: TransformVertexPositionsHomogeneous, d3d_ClipSprite
// and ClipAndProjectPolyWithDepthBias as templates (the exe emits them after the set callbacks, as template instances at the end
// of the object would be; refused templates are pending sites: Dist's share drops to 137u, but the template TransformVertex-
// PositionsHomogeneous is then inlined everywhere and loses its out-of-line copy); a d3d_SetTexture helper that tests
// m_pRenderData before d3d_FindRTextureForStage (include/d3dren/d3d_texture.h) refuses the search only at >= 64u, which
// contradicts d3d_DrawPolyGrid (the exe inlines both searches there), and any new declaration in that header moves
// ClipPolyNear40 / ClipPolyLeft40 (unit unk/10007930) by 4 bytes.
// Helper recovery (measured with inline_budget.py, not kept): a Jupiter-style vertex fill helper (d3d_SetupVertexPos / SetupVert,
// 82u) for the four corners lowers B to 1276u and gives Dist the exe's decisions, but then SpriteGetColor and the fourth fill are
// refused (the exe expands both); with the colour code also written out Dist and SpriteGetColor fit but B rises to 2078u and
// TransformVertexPositionsHomogeneous and the search stay inline (closest budget 1224u).  The exe needs roughly 12 more free top-level
// sites after Dist and 70..95u less own size at once; no helper with evidence in Jupiter or the SDK headers gives both.
// Structural pass: the vertex is TLVertex (FVF 0x1c4); the out-of-line LTVector copies the exe calls are this SDK header's (matched
// in other units) and no callee is an ICF twin.  With TLVertex::SetTCoords the model gives Dist 113u (Mag refused, as in the exe) but
// operator-, d3d_FindRTextureForStage and TransformVertexPositionsHomogeneous stay inline; LTVector::Init corners (as in the camera
// facing drawer) and the three helpers as templates do not reproduce the exe either (closest budget 1084u: SpriteGetColor refused).
// STUB: D3DREN 0x1002e310
void d3d_DrawRotatableSprite(ViewParams *pParams, SpriteInstance *pInstance, LTVector *pPos, float fScaleX, float fScaleY, SharedTexture *pTexture)
{
	TLVertex aClipped[300];
	TLVertex aVerts[4];
	LTMatrix mRotation;
	LTVector vForward;
	uint32 nSpecular;
	TLVertex *pVerts;
	uint32 nVerts;
	float fWidth, fHeight;
	float uMin, uMax, vMin, vMax;
	uint32 nColor;
	int *pOrder;
	TLVertex *pVert;

	pInstance->m_Pos.Dist(pParams->m_Pos);

	if (!d3d_SetTexture(pTexture, g_NormalTextureStage, 0))
		return;

	g_TextureStateRestorer.RestoreAllStates();
	if (pTexture->m_pStateChange)
		g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, g_NormalTextureStage);

	fWidth = (float)(g_pBoundTextures[g_NormalTextureStage]->GetBaseWidth() >> pTexture->m_Unknown3C);
	fHeight = (float)(g_pBoundTextures[g_NormalTextureStage]->GetBaseHeight() >> pTexture->m_Unknown3C);

	g_pfnCalcFogAlpha(&pInstance->m_Pos, &nSpecular);

	uMin = g_TextureStageTexelSizes[0].m_Unk00 + g_TextureStageTexelSizes[0].m_Unk00;
	uMax = (fWidth - 2.0f) * g_TextureStageTexelSizes[0].m_Unk00;
	vMin = g_TextureStageTexelSizes[0].m_Unk04 + g_TextureStageTexelSizes[0].m_Unk04;
	vMax = (fHeight - 2.0f) * g_TextureStageTexelSizes[0].m_Unk04;

	d3d_SetupTransformation(&pInstance->m_Pos, (float *)&pInstance->m_Rotation, &pInstance->m_Scale, &mRotation);

	// which side of the sprite the viewer is on decides the corner order (winding)
	vForward.Init(0.0f, 0.0f, -1.0f);
	MatVMul_InPlace_3x3(&mRotation, &vForward);
	pOrder = s_CornerOrderBack;
	if (vForward.Dot(pParams->m_Pos - *pPos) >= 0.0f)
		pOrder = s_CornerOrderFront;

	nColor = SpriteGetColor(pInstance);

	pVert = &aVerts[pOrder[0]];
	pVert->m_Vec.x = fWidth;
	pVert->m_Vec.y = fHeight;
	pVert->m_Vec.z = 0.0f;
	pVert->color = nColor;
	pVert->specular = nSpecular;
	pVert->SetTCoords(uMin, vMin);

	pVert = &aVerts[pOrder[1]];
	pVert->m_Vec.x = -fWidth;
	pVert->m_Vec.y = fHeight;
	pVert->m_Vec.z = 0.0f;
	pVert->color = nColor;
	pVert->specular = nSpecular;
	pVert->SetTCoords(uMax, vMin);

	pVert = &aVerts[pOrder[2]];
	pVert->m_Vec.x = -fWidth;
	pVert->m_Vec.y = -fHeight;
	pVert->m_Vec.z = 0.0f;
	pVert->color = nColor;
	pVert->specular = nSpecular;
	pVert->SetTCoords(uMax, vMax);

	pVert = &aVerts[pOrder[3]];
	pVert->m_Vec.x = fWidth;
	pVert->m_Vec.y = -fHeight;
	pVert->m_Vec.z = 0.0f;
	pVert->color = nColor;
	pVert->specular = nSpecular;
	pVert->SetTCoords(uMin, vMax);

	TransformVertexPositionsHomogeneous(aVerts, 4, &mRotation);

	pVerts = aVerts;
	nVerts = 4;
	if (pInstance->m_ClipperPoly != INVALID_HPOLY)
	{
		if (!d3d_ClipSprite(pInstance, pInstance->m_ClipperPoly, &pVerts, &nVerts, aClipped))
			return;
	}

	if (pInstance->m_Flags & FLAG_SPRITEBIAS)
	{
		if (!ClipAndProjectPolyWithDepthBias(&pVerts, (int *)&nVerts, &g_ViewParams, 0, SPRITE_POSITION_ZBIAS))
			return;
	}
	else
	{
		if (!d3d_ClipAndProjectTLVertices(&pVerts, (int *)&nVerts, &g_ViewParams, 0))
			return;
	}

	d3d_DrawDevicePrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
}

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
// STUB diagnosis: 1120 bytes like the exe, 413 instructions.  The poly lookup is an inline helper returning NULL (the renderer's
// form of the engine's cm_GetPolyFromHPoly, clientde_impl.cpp): both failures then share the `!pPoly` return and the exe's separate
// return epilogues after each guard come out.  The clip loop counters are signed (jle/jl), and the clip test counter is incremented
// after the plane distance (that fixes the registers and frame slots of the whole function).  Left (12 bytes): the order of the two
// reloads (pPoly into esi, pCurPoint into eax) at the three exits of the polygon rebuild (0x1002efac/efc9/efd1); declaration order,
// names, loop forms, the template form and the lifetimes of thePlane/vecTo do not change it.
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

// PARKED: 12 bytes, the order of two spill reloads (esi/eax) at the rebuild-loop exits; source levers and a 3000-candidate permuter run leave it
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
		thePlane.m_Dist = VEC_DOT(thePlane.m_Normal, *pCurPoint->m_Vec);
		g_nPlaneClipTests++;

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
// FUNCTION: D3DREN 0x1002f010
int ClipAndProjectPolyWithDepthBias(TLVertex **ppVerts, int *pnVerts, ViewParams *pParams, int a4, float fBias)
{
	TLVertex *pVert;
	int i;

	pVert = *ppVerts;
	for (i = *pnVerts; i != 0; i--)
	{
		float *pMat = &pParams->m_mClipTransform.m[0][0];
		TransformTLVertexInPlace(pVert, pMat);
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
		float fBiasZ;

		if (fBias + pVert->m_Vec.z < g_CV_NearZ.m_FloatVal)
			fBiasZ = g_CV_NearZ.m_FloatVal - pVert->m_Vec.z;
		else
			fBiasZ = fBias;

		ProjectTLVertexWithDepthBias(pVert, fBiasZ);
		pVert++;
	}
	return 1;
}
