// d3d.ren sys/d3d/drawmodelshadows (0x10025013-0x100285a0): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// insensitive to Ob1/Ob2 in the scan.
// FLAGS: /O1 /Ob2
// d3d.ren model drawing: the second half of the original `drawmodel` object (0x100241e0) and the `drawmodelshadows` object
// (0x10025013..0x1002859f: planar and projected model shadows).  Unit name: the proposal's 0x100241e0 (NAMING.md section 4
// names the objects: drawmodel 0x10023860-0x10025012 of which this unit holds the tail, drawmodelshadows 0x10025013-0x1002859f).
// The three console-variable groups sit where the exe has their static initialisers (between the functions).
// (No /Ob2: with it the 20-byte RestoreModelFillMode, called only by DrawModelPassWithVertexCallbacks, is expanded into its caller, the exe calls it.)
#include <math.h>
#include <windows.h>
#include <string.h>
#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "de_objects.h"
#include "de_world.h"
#include "world_tree.h"
#include "geomroutines.h"
#include "de_mainworld.h"
#include "d3dren/modeldraw.h"
#include "d3dren/setupmodel.h"
#include "d3dren/modelshadow.h"
#include "d3dren/d3dstate.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3d_surface.h"
#include "d3dren/common_stuff.h"
#include "d3dren/common_draw.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/pool.h"
#include "d3dren/tlvertex.h"
#include "d3dren/vertfill.h"

// ---- declarations of other units (the lead unifies them with the real headers) ------------------------------------------------

// guess: collects the world polygons along a segment (callback of FindObjectsOnPoint, 0x10035917)
struct UnkType_SegRequest;
void __cdecl CollectWorldModelSegmentPolys(WorldModelInstance *pObj, UnkType_SegRequest *pUser);

// guess: 0x100323ff / 0x1003244f: the two cached scratch shadow textures (the factory's AllocShadowTexture/FreeShadowTexture
// behind a size cache); `A` receives the silhouette, `B` keeps the pixels of the backbuffer corner it is drawn over.
IShadowTexture * __cdecl GetCachedProjectionShadowTexture(uint32 uiSizeX, uint32 uiSizeY);
IShadowTexture * __cdecl GetCachedSilhouetteShadowTexture(uint32 uiSizeX, uint32 uiSizeY);

// A D3DTLVERTEX (FVF 0x1c4) of the silhouette quad / triangles.
struct UnkType_ShadowVertex
{
	LTVector	m_Vec;
	float		rhw;
	uint32		color;
	uint32		specular;
	float		tu, tv;
};

// FUNCTION: D3DREN 0x10025013 _$E3
// FUNCTION: D3DREN 0x10025018 _$E2
// GLOBAL: D3DREN 0x10069040
ConVar g_CV_ModelShadowAlpha("ModelShadowAlpha", 230.0f);
// FUNCTION: D3DREN 0x10025036 _$E6
// FUNCTION: D3DREN 0x1002503b _$E5
// GLOBAL: D3DREN 0x10069490
ConVar g_CV_ModelShadowOffset("ModelShadowOffset", 0.05f);
// FUNCTION: D3DREN 0x10025059 _$E9
// FUNCTION: D3DREN 0x1002505e _$E8
// GLOBAL: D3DREN 0x10069060
ConVar g_CV_ModelShadowProj("ModelShadowProj", 0.0f);

// Same epsilon as Jupiter 3d_ops.h CLIP_EPSILON.
#define CLIP_EPSILON	0.00001f

// bInside[] arrays of the polygon clippers below (declarations and GLOBAL annotations: d3dren/modelshadow.h).
int g_ShadowClipPlaneInsideFlags[56];
int g_ShadowClipFarInsideFlags[56];
int g_ShadowClipBottomInsideFlags[56];
int g_ShadowClipRightInsideFlags[56];
int g_ShadowClipTopInsideFlags[56];
int g_ShadowClipLeftInsideFlags[56];
int g_ShadowClipNearInsideFlags[56];

// Edge/plane intersection helpers of other units (pool.h / seed polyclip): return t in st(0).
float IntersectNearClipPlane(float *p1, float *p2, float *pOut);
float IntersectLeftClipPlane(float *p1, float *p2, float *pOut);
float IntersectTopClipPlane(float *p1, float *p2, float *pOut);
float IntersectRightClipPlane(float *p1, float *p2, float *pOut);
float IntersectBottomClipPlane(float *p1, float *p2, float *pOut);
float IntersectFarClipPlane(float *p1, float *p2, float *pOut);

// Draws the model shadow onto one world polygon (non-projected path of ModelDraw::DrawModelShadows 0x100252c6, which calls it per
// poly): copies the polygon (<= 0x80 vertices, else "Error: vertex buffer overflow"), lifts it by ModelShadowOffset along the poly
// normal, clips it against the six shadow frustum planes (pInfo->m_FrustumPlanes), gives every vertex the ModelShadowAlpha colour and
// the texture coordinates of the 4x4 at pInfo+0x60 (rows 0, 1, 3 = tu, tv, q), then transforms/projects it (TransformClipAndProjectShadowPolygon) and draws it
// as a TRIANGLEFAN of 0x20-byte TL vertices with tu/q, tv/q.  `this` is not used.
// Not matching (585 of 590 bytes, ~66 aligned instruction mismatches, all in two places; the algorithm and every call/global agree):
//  (1) the x87 term/operand order of the three matrix expressions: the exe has tu = z(V,M) x(V,M) y(M,V) and tv = q = x(V,M) z(M,V) y(M,V)
//      (V = vertex field as `fld`, M = matrix element as `fmul`), we produce the default z,y,x / y,x,z with M first.  Source operand order
//      and statement order of the three expressions do not change it, named float/LTVector/LTMatrix locals, pointers and references
//      to the matrix change it only to other wrong orders; toy tests (800 reference prefixes) show it depends on the first-reference
//      order of values in the whole function, which I could not recover.
//  (2) frame/slot and register allocation of the clip stage: the exe keeps the plane counter in memory ([ebp-8], dec at the top, cmp/jne
//      at the bottom), the clipper object at [ebp-0x14], pClipVerts in the dead pPoly... parameter slot [ebp+8] and pSrc in edx; we keep
//      the counter in ebx (inc/cmp 6), the clipper object in [ebp+8] and pSrc in ecx (frame 0x221c instead of 0x2220).
// Tried without effect on (2): the clip stage in its own block (kept; it is what fixes the slots of 0x10026d6a), separate loop variables per loop (that gave 74 -> 66 mismatches and is kept), unsigned/uint8/uint16 counters,
// do/while and down-counting forms, declaration order (48 permutations), an inline clip helper with reference parameters, pCur copy of pSrc.
// The ordering hint that did help: pVert = &pVerts[i] (a pointer local) in the vertex loop, which removes the per-field reloads of pVerts.
// STUB: D3DREN 0x10025078
void ModelDraw::DrawBlobShadowOnWorldPoly(ShadowLightInfo *pInfo, WorldPoly *pPoly)
{
	UnkType_Vertex36 aVerts[0x80];
	TLVertex aOut[0x80];
	UnkType_Vertex36 *pVerts;
	SPolyVertex *pSrc;
	int nVerts;
	int i, iVert, iDraw;

	if (g_FixTJunc)
	{
		pSrc = pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (SPolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	for (iVert = 0; iVert < nVerts; iVert++)
	{
		aVerts[iVert].m_Vec = *pSrc->m_Vec;
		aVerts[iVert].m_Vec += pPoly->m_pPlane->m_Normal * g_CV_ModelShadowOffset.m_FloatVal;
		pSrc++;
	}

	pVerts = aVerts;
	{
		UnkType_Vertex36 *pClipVerts, *pClipOut;
		UnkType_PlaneClipper clipper;
		LTPlane *pPlane;
		int nClip;
		int iPlane;
		pClipVerts = pVerts;
		nClip = nVerts;
		pClipOut = (UnkType_Vertex36 *)g_pClipScratchVerts;
		pPlane = pInfo->m_FrustumPlanes;
		for (iPlane = 0; iPlane < 6; iPlane++)
		{
			clipper.m_Unk00 = pPlane;
			if (!ClipShadowPolygonToPlane(&clipper, &pClipVerts, &nClip, &pClipOut))
				return;
			pPlane++;
		}
		pVerts = pClipVerts;
		nVerts = nClip;
	}

	if (pVerts != aVerts)
	{
		memcpy(aVerts, pVerts, nVerts * sizeof(UnkType_Vertex36));
		pVerts = aVerts;
	}

	for (i = 0; i < nVerts; i++)
	{
		UnkType_Vertex36 *pVert = &pVerts[i];
		pVert->color = 0;
		pVert->rgb.a = (uint8)g_CV_ModelShadowAlpha.m_IntVal;
		pVert->tu = pInfo->m_Unk60.m[0][0] * pVert->m_Vec.x + pInfo->m_Unk60.m[0][1] * pVert->m_Vec.y + pInfo->m_Unk60.m[0][2] * pVert->m_Vec.z + pInfo->m_Unk60.m[0][3];
		pVert->tv = pInfo->m_Unk60.m[1][0] * pVert->m_Vec.x + pInfo->m_Unk60.m[1][1] * pVert->m_Vec.y + pInfo->m_Unk60.m[1][2] * pVert->m_Vec.z + pInfo->m_Unk60.m[1][3];
		pVert->specular = 0xffffffff;
		pVert->m_Unk20 = pInfo->m_Unk60.m[3][0] * pVert->m_Vec.x + pInfo->m_Unk60.m[3][1] * pVert->m_Vec.y + pInfo->m_Unk60.m[3][2] * pVert->m_Vec.z + pInfo->m_Unk60.m[3][3];
	}

	g_ClipFlags = 0x3f;
	if (TransformClipAndProjectShadowPolygon(&pVerts, &nVerts, &g_ViewParams, 0))
	{
		for (iDraw = 0; iDraw < nVerts; iDraw++)
		{
			aOut[iDraw].m_Vec = pVerts[iDraw].m_Vec;
			aOut[iDraw].rhw = pVerts[iDraw].rhw;
			aOut[iDraw].color = pVerts[iDraw].color;
			aOut[iDraw].specular = pVerts[iDraw].specular;
			aOut[iDraw].tu = pVerts[iDraw].tu / pVerts[iDraw].m_Unk20;
			aOut[iDraw].tv = pVerts[iDraw].tv / pVerts[iDraw].m_Unk20;
		}
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, aOut, nVerts, 0);
	}
}

// Named query/light-origin aliases, separate traversal/drawing counters, and the guarded drawing scope reproduce the x87 Dot order.
// The call sequence equals the exe's only with the inline helper d3d_SetTextureDirect() for the final SetTexture (it is the one
// extra inline call site after the second loop's first out-of-line LTVector constructor that VC6's inline budget needs; Jupiter
// calls d3d_SetTextureDirect(pOldTexture, 0) the same way); without any extra site that constructor (info.m_vProjectionCenter)
// is inlined by our build.
// What made the frame match (so keep it): ModelDraw::m_ShadowLights at 0x644, the {nTotal + first loop} and {StateSet x2 +
// second loop} in two sibling blocks (slot sharing of ptr/nTotal with the StateSets), pOldTexture declared at function level,
// the query record 0x3c bytes with m_vOrigin/m_vDir members (the exe copies the origin and the direction into the record and
// reads them back), `GetPos() + vCenterOffset` (not vCenterOffset + GetPos(): the receiver is not copied), LTMAX for the radius.
// The result depends on d3dstate.h's StateSet (constructor uses its `state` argument, not m_State) and on the inline sites of
// the SDK headers: any change of those moves the inline decisions.
// FUNCTION: D3DREN 0x10026009 ?SetupProjectionMatrix@LTMatrix@@QAEXV?$_CVector@M@@VLTPlane@@@Z
// FUNCTION: D3DREN 0x100252c6
// NAME: ModelDraw::DrawModelShadows: names_proposal.csv (high, Jupiter drawmodelshadows.cpp)
void ModelDraw::DrawModelShadows()
{
	ShadowLightInfo info;
	int i, nShadows, k;
	UnkType_ShadowPolys polyLists[NUM_MODEL_SHADOWS];
	LTVector lightDirs[NUM_MODEL_SHADOWS];
	UnkType_ShadowPolyQuery query;
	LTVector vWindowLeft, vWindowRight, vWindowTop, vWindowBottom;
	LTPlane *pPlane;
	float fMaxShadowDist, fSizeX, fSizeY, fDistToObject;
	LTVector vCenterOffset;
	IDirectDrawSurface7 *pOldTexture;

	// Figure out how many shadows we're going to try for.
	nShadows = NUM_MODEL_SHADOWS;
	if (nShadows > g_MaxModelShadows)
		nShadows = g_MaxModelShadows;
	if (nShadows > NUM_MODEL_SHADOWS)
		nShadows = NUM_MODEL_SHADOWS;
	if (nShadows < 0)
		nShadows = 0;

	if (nShadows == 0)
		return;

	if (g_bModelShadowsSupported && g_pShadowBlobTexture && m_pModel->m_bShadowEnable)
	{

		if (g_CV_ModelShadowProj.m_IntVal)
		{
			DrawProjectedModelShadows(nShadows);
			return;
		}

		// Get stuff out of the command string.
		fMaxShadowDist = m_pModel->m_ShadowProjectLength;
		fSizeX = m_pModel->m_ShadowSizeX;
		fSizeY = m_pModel->m_ShadowSizeY;
		fDistToObject = m_pModel->m_ShadowLightDist;
		vCenterOffset = m_pModel->m_ShadowCenterOffset;

		// HACK.
		static LTVector dir(0.0f, -1.0f, 0.0f);
		m_ShadowLights[0] = dir;
		m_ShadowLights[0].Norm();

		{
			int nTotal = 0;
			for (i = 0; i < nShadows; i++)
			{
				polyLists[i].m_nPolys = 0;
				query.m_pPolys = &polyLists[i];

				query.m_vOrigin = m_pInstance->GetPos() - m_ShadowLights[i] * fDistToObject;
				lightDirs[i] = (m_pInstance->GetPos() + vCenterOffset) - query.m_vOrigin;
				lightDirs[i].Norm();

				query.m_vDir = lightDirs[i];
				LTVector *pQueryOrigin = &query.m_vOrigin;
				query.m_vStart = *pQueryOrigin;
				query.m_vEnd = query.m_vOrigin + query.m_vDir * (fDistToObject + fMaxShadowDist);
				query.m_fRadius = LTMAX(fSizeX, fSizeY);

				g_pFrameMainWorld->m_WorldTree.FindObjectsOnPoint(&m_pInstance->m_Pos, (WTObjCallback)CollectWorldModelSegmentPolys, &query, 0);

				nTotal += polyLists[i].m_nPolys;
			}

			if (nTotal == 0)
				return;
		}

		{
			g_pD3DDevice->GetTexture(0, &pOldTexture);
			g_pD3DDevice->SetTexture(0, g_pShadowBlobTexture);
			StateSet alphaBlend(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			StateSet zWrite(D3DRENDERSTATE_ZWRITEENABLE, 0);

			for (k = 0; k < nShadows; k++)
			{
				if (polyLists[k].m_nPolys == 0)
					continue;

				info.m_fSizeX = fSizeX;
				info.m_fSizeY = fSizeY;
				info.m_vLightOrigin = m_pInstance->m_Pos - lightDirs[k] * fDistToObject;
				info.m_ProjectionPlane.m_Normal = lightDirs[k];
				info.m_vProjectionCenter = info.m_vLightOrigin +
						lightDirs[k] * (fDistToObject + fMaxShadowDist);
				info.m_ProjectionPlane.m_Dist = info.m_ProjectionPlane.m_Normal.Dot(
					info.m_vProjectionCenter);

				// Setup the frame of reference (up is (0,1,0) and right is generated).
				LTVector &vLightOrigin = info.m_vLightOrigin;
				info.m_Vecs[2] = lightDirs[k];
				gr_GetPerpendicularVector(&info.m_Vecs[2], LTNULL, &info.m_Vecs[1]);
				info.m_Vecs[0] = info.m_Vecs[2].Cross(info.m_Vecs[1]);

				vWindowLeft = info.m_vProjectionCenter - info.m_Vecs[0] * fSizeX;
				vWindowRight = info.m_vProjectionCenter + info.m_Vecs[0] * fSizeX;
				vWindowTop = info.m_vProjectionCenter + info.m_Vecs[1] * fSizeY;
				vWindowBottom = info.m_vProjectionCenter - info.m_Vecs[1] * fSizeY;

				info.m_vWindowTopLeft = info.m_vProjectionCenter -
					info.m_Vecs[0] * fSizeX -
					info.m_Vecs[1] * fSizeY;

				// Setup the clipping planes.
				pPlane = &info.m_FrustumPlanes[CPLANE_NEAR_INDEX];
				pPlane->m_Normal = info.m_Vecs[2];
				pPlane->m_Dist = pPlane->m_Normal.Dot(m_pInstance->GetPos());

				pPlane = &info.m_FrustumPlanes[CPLANE_FAR_INDEX];
				pPlane->m_Normal = -info.m_Vecs[2];
				pPlane->m_Dist = pPlane->m_Normal.Dot(m_pInstance->GetPos() + info.m_Vecs[2] * fMaxShadowDist);

				pPlane = &info.m_FrustumPlanes[CPLANE_LEFT_INDEX];
				pPlane->m_Normal = (vWindowLeft - info.m_vLightOrigin).Cross(info.m_Vecs[1]);
				pPlane->m_Normal.Norm();
				pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

				pPlane = &info.m_FrustumPlanes[CPLANE_RIGHT_INDEX];
				LTVector &vRightLightOrigin = vLightOrigin;
				pPlane->m_Normal = info.m_Vecs[1].Cross(vWindowRight - vRightLightOrigin);
				pPlane->m_Normal.Norm();
				pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

				pPlane = &info.m_FrustumPlanes[CPLANE_TOP_INDEX];
				LTVector vTopLightOrigin = vRightLightOrigin;
				pPlane->m_Normal = (vWindowTop - vTopLightOrigin).Cross(info.m_Vecs[0]);
				pPlane->m_Normal.Norm();
				pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

				pPlane = &info.m_FrustumPlanes[CPLANE_BOTTOM_INDEX];
				pPlane->m_Normal = info.m_Vecs[0].Cross(vWindowBottom - info.m_vLightOrigin);
				pPlane->m_Normal.Norm();
				pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

				LTMatrix mProjection, mTexture, mScale;
				mProjection.SetupProjectionMatrix(info.m_vLightOrigin, info.m_ProjectionPlane);
				mTexture.Init(info.m_Vecs[0].x, info.m_Vecs[0].y, info.m_Vecs[0].z, -info.m_Vecs[0].Dot(info.m_vWindowTopLeft),
					info.m_Vecs[1].x, info.m_Vecs[1].y, info.m_Vecs[1].z, -info.m_Vecs[1].Dot(info.m_vWindowTopLeft),
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f);
				mScale.Init(1.0f / (info.m_fSizeX * 2.0f), 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f / (info.m_fSizeY * 2.0f), 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f);
				info.m_Unk60 = mScale * mTexture * mProjection;

				for (uint32 j = 0; j < polyLists[k].m_nPolys; j++)
					DrawBlobShadowOnWorldPoly(&info, polyLists[k].m_Polys[j]);
			}

			d3d_SetTextureDirect(pOldTexture, 0);
		}
	}
}

// The atexit destructor of DrawModelShadows' function-local `static LTVector dir` (an empty function: LTVector has no destructor).
// FUNCTION: D3DREN 0x100260cd _$E12

// FUNCTION: D3DREN 0x100260ce
int ClipShadowPolygonToPlane(UnkType_PlaneClipper *pClipper, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipPlaneInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = pClipper->IsInsidePlane(&pCur->m_Vec);
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipPlaneInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipPlaneInsideFlags[iPrev] != g_ShadowClipPlaneInsideFlags[iCur])
			{
				t = pClipper->IntersectEdgeWithPlane(&pPrev->m_Vec, &pCur->m_Vec, &(*ppOut)->m_Vec);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// FUNCTION: D3DREN 0x100261f9
void InterpolateShadowVertexAttributes(UnkType_Vertex36 *pPrev, UnkType_Vertex36 *pCur, UnkType_Vertex36 *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->m_Unk20 = (pCur->m_Unk20 - pPrev->m_Unk20) * t + pPrev->m_Unk20;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}

// FUNCTION: D3DREN 0x10026300
int UnkType_PlaneClipper::IsInsidePlane(LTVector *pVec)
{
	return m_Unk00->DistTo(*pVec) > 0.0f;
}

// FUNCTION: D3DREN 0x10026343
float UnkType_PlaneClipper::IntersectEdgeWithPlane(LTVector *pt1, LTVector *pt2, LTVector *pOut)
{
	float d1 = m_Unk00->DistTo(*pt1);
	float d2 = m_Unk00->DistTo(*pt2);
	float d = d1 - d2;
	float t;
	if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
		t = d1 / d;
	else
		t = 0.0f;
	*pOut = *pt1 + (*pt2 - *pt1) * t;
	return t;
}

// FUNCTION: D3DREN 0x10026412
int TransformClipAndProjectShadowPolygon(UnkType_Vertex36 **ppVerts, int *pnVerts, ViewParams *pViewParams, int nUnused)
{
	UnkType_Vertex36 *pVert;
	int n;

	if (g_ClipFlags == 0)
	{
		pVert = *ppVerts;
		for (n = *pnVerts; n != 0; n--)
		{
			pVert->rhw = MatVMul_InPlace_H(&pViewParams->m_FullTransform, &pVert->m_Vec);
			pVert++;
		}
	}
	else
	{
		pVert = *ppVerts;
		for (n = *pnVerts; n != 0; n--)
		{
			TransformPositionInPlace(&pVert->m_Vec.x, &pViewParams->m_mClipTransform.m[0][0]);
			pVert++;
		}
		if (!ClipShadowPolygonToViewFrustum(g_ClipFlags, ppVerts, pnVerts))
			return 0;
		pVert = *ppVerts;
		for (n = *pnVerts; n != 0; n--)
		{
			ProjectVertexToScreen(&pVert->m_Vec.x, pViewParams);
			pVert++;
		}
	}
	return 1;
}

// FUNCTION: D3DREN 0x100264ad
int ClipShadowPolygonToViewFrustum(uint32 flags, UnkType_Vertex36 **ppVerts, int *pnVerts)
{
	char bUnused0, bUnused1, bUnused2, bUnused3, bUnused4, bUnused5;
	UnkType_Vertex36 *pVerts, *pOut;
	int nVerts;

	if (g_CV_UseD3DClip.m_IntVal && !(flags &= 1))
		return 1;

	pOut = (UnkType_Vertex36 *)g_pClipScratchVerts;
	pVerts = *ppVerts;
	nVerts = *pnVerts;

	if ((flags & 1) && !ClipShadowPolygonToNearPlane(&bUnused0, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 4) && !ClipShadowPolygonToLeftPlane(&bUnused1, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 8) && !ClipShadowPolygonToTopPlane(&bUnused2, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x10) && !ClipShadowPolygonToRightPlane(&bUnused3, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x20) && !ClipShadowPolygonToBottomPlane(&bUnused4, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 2) && !ClipShadowPolygonToFarPlane(&bUnused5, &pVerts, &nVerts, &pOut))
		return 0;

	*ppVerts = pVerts;
	*pnVerts = nVerts;
	return 1;
}

// near plane z >= g_ViewParams.m_NearZ (flag bit 1)
// FUNCTION: D3DREN 0x100265c9
int ClipShadowPolygonToNearPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipNearInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.z >= g_ViewParams.m_NearZ;
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipNearInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipNearInsideFlags[iPrev] != g_ShadowClipNearInsideFlags[iCur])
			{
				t = IntersectNearClipPlane(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// left plane x + z > 0 (flag bit 4)
// FUNCTION: D3DREN 0x10026700
int ClipShadowPolygonToLeftPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipLeftInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = -pCur->m_Vec.z < pCur->m_Vec.x;
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipLeftInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipLeftInsideFlags[iPrev] != g_ShadowClipLeftInsideFlags[iCur])
			{
				t = IntersectLeftClipPlane(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// plane y < z (flag bit 8)
// FUNCTION: D3DREN 0x10026835
int ClipShadowPolygonToTopPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipTopInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.y < pCur->m_Vec.z;
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipTopInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipTopInsideFlags[iPrev] != g_ShadowClipTopInsideFlags[iCur])
			{
				t = IntersectTopClipPlane(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// plane x < z (flag bit 0x10)
// FUNCTION: D3DREN 0x10026969
int ClipShadowPolygonToRightPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipRightInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.x < pCur->m_Vec.z;
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipRightInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipRightInsideFlags[iPrev] != g_ShadowClipRightInsideFlags[iCur])
			{
				t = IntersectRightClipPlane(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// plane y > -z (flag bit 0x20)
// FUNCTION: D3DREN 0x10026a9c
int ClipShadowPolygonToBottomPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipBottomInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = -pCur->m_Vec.z < pCur->m_Vec.y;
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipBottomInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipBottomInsideFlags[iPrev] != g_ShadowClipBottomInsideFlags[iCur])
			{
				t = IntersectBottomClipPlane(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// far plane z <= g_ViewParams.m_ClipFarZ (flag bit 2)
// FUNCTION: D3DREN 0x10026bd2
int ClipShadowPolygonToFarPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut)
{
	int *pInside;
	int nInside = 0;
	UnkType_Vertex36 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	++g_nPlaneClipTests;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = g_ShadowClipFarInsideFlags;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.z <= g_ViewParams.m_ClipFarZ;
		nInside += *pInside;
		++pInside;
		++pCur;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		for (iCur = 0; iCur < *pnVerts; iCur++)
		{
			pCur = *ppVerts + iCur;
			if (g_ShadowClipFarInsideFlags[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ShadowClipFarInsideFlags[iPrev] != g_ShadowClipFarInsideFlags[iCur])
			{
				t = IntersectFarClipPlane(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				InterpolateShadowVertexAttributes(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// FUNCTION: D3DREN 0x10026d09 _$E14
// FUNCTION: D3DREN 0x10026d0e _$E13
// GLOBAL: D3DREN 0x1006a4f0
ConVar g_CV_ModelShadowProjRes("ModelShadowProjRes", 128.0f);
// FUNCTION: D3DREN 0x10026d2c _$E17
// FUNCTION: D3DREN 0x10026d31 _$E16
// GLOBAL: D3DREN 0x1006a4d0
ConVar g_CV_ModelShadowProjLOD("ModelShadowProjLOD", 1.0f);
// FUNCTION: D3DREN 0x10026d4b _$E20
// FUNCTION: D3DREN 0x10026d50 _$E19
// GLOBAL: D3DREN 0x1006a4b0
ConVar g_CV_ModelShadowProjShow("ModelShadowProjShow", 0.0f);

// Projected-texture-path variant of 0x10025078 (called per poly by 0x1002701e): the same polygon preparation, clipping and draw, but the
// vertex colour is white, the alpha follows the distance to the light, `alpha = (uint8)(ModelShadowAlpha float * max(0, 1 - |v - light| / fDist))`,
// and the specular colour is 0.  `this` is not used.  The distance must be written with the SDK operators on a named local
// (`LTVector vToLight = pVert->m_Vec - pInfo->m_vLightOrigin; vToLight.Mag()`: the by-value temporaries of operator- and the out-of-line
// Mag 0x1000e011 call give the exe's frame temporaries, and with them the exe's x87 term order of tu/tv/q and eax=matrix, ebx=vertex);
// LTVector::Dist() or an unnamed temporary gives other orders. The clip stage has to sit in its own block with its own locals (the exe
// shares the slots of pClipVerts/nClip with the vertex loop's variables) and counts down (`for (iPlane = 6; iPlane > 0; iPlane--)`).
// Not matching: 10/692 bytes differ (4 aligned instruction mismatches). The six-plane clip loop now has
// the target's down-count, delayed clipper store, bottom `test/jne`, and `pop edi` placement. Remaining differences are in the initial
// vertex-loop register choices and setup ordering. The source-plane pointer is cached without dereferencing it for an empty
// input, and the output array lives only in the successful draw branch. Keep this a STUB until the whole function is byte-exact.
// STUB: D3DREN 0x10026d6a
void ModelDraw::DrawProjectedShadowOnWorldPoly(ShadowLightInfo *pInfo, WorldPoly *pPoly, float fDist)
{
	int nVerts;
	int i, iVert, iDraw;
	SPolyVertex *pSrc, *pCur;
	UnkType_Vertex36 *pVerts;
	UnkType_Vertex36 aVerts[0x80];

	if (g_FixTJunc)
	{
		pSrc = pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (SPolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	LTPlane *pSourcePlane = pPoly->m_pPlane;
	pCur = pSrc;
	for (iVert = 0; iVert < nVerts; iVert++)
	{
		aVerts[iVert].m_Vec = *pCur->m_Vec;
		aVerts[iVert].m_Vec += pSourcePlane->m_Normal * g_CV_ModelShadowOffset.m_FloatVal;
		pCur++;
	}

	pVerts = aVerts;
	{
		UnkType_Vertex36 *pClipVerts, *pClipOut;
		UnkType_PlaneClipper clipper;
		LTPlane *pPlane;
		int nClip;
		int iPlane;
		pPlane = pInfo->m_FrustumPlanes;
		pClipOut = (UnkType_Vertex36 *)g_pClipScratchVerts;
		pClipVerts = pVerts;
		for (iPlane = 6, nClip = nVerts; iPlane != 0; )
		{
			--iPlane;
			if (!ClipShadowPolygonToPlane((clipper.m_Unk00 = pPlane, &clipper), &pClipVerts, &nClip, &pClipOut))
				return;
			pPlane++;
		}
		pVerts = pClipVerts;
		nVerts = nClip;
	}

	if (pVerts != aVerts)
	{
		memcpy(aVerts, pVerts, nVerts * sizeof(UnkType_Vertex36));
		pVerts = aVerts;
	}

	for (i = 0; i < nVerts; i++)
	{
		UnkType_Vertex36 *pVert = &pVerts[i];
		LTVector vToLight = pVert->m_Vec - pInfo->m_vLightOrigin;
		float fFactor = 1.0f - vToLight.Mag() / fDist;
		if (fFactor < 0.0f)
			fFactor = 0.0f;
		pVert->color = 0xffffffff;
		pVert->rgb.a = (uint8)(int)(g_CV_ModelShadowAlpha.m_FloatVal * fFactor);
		pVert->tu = pInfo->m_Unk60.m[0][0] * pVert->m_Vec.x + pInfo->m_Unk60.m[0][1] * pVert->m_Vec.y + pInfo->m_Unk60.m[0][2] * pVert->m_Vec.z + pInfo->m_Unk60.m[0][3];
		pVert->tv = pInfo->m_Unk60.m[1][0] * pVert->m_Vec.x + pInfo->m_Unk60.m[1][1] * pVert->m_Vec.y + pInfo->m_Unk60.m[1][2] * pVert->m_Vec.z + pInfo->m_Unk60.m[1][3];
		pVert->m_Unk20 = pInfo->m_Unk60.m[3][0] * pVert->m_Vec.x + pInfo->m_Unk60.m[3][1] * pVert->m_Vec.y + pInfo->m_Unk60.m[3][2] * pVert->m_Vec.z + pInfo->m_Unk60.m[3][3];
		pVert->specular = 0;
	}

	g_ClipFlags = 0x3f;
	if (TransformClipAndProjectShadowPolygon(&pVerts, &nVerts, &g_ViewParams, 0))
	{
		TLVertex aOut[0x80];
		for (iDraw = 0; iDraw < nVerts; iDraw++)
		{
			aOut[iDraw].m_Vec = pVerts[iDraw].m_Vec;
			aOut[iDraw].rhw = pVerts[iDraw].rhw;
			aOut[iDraw].color = pVerts[iDraw].color;
			aOut[iDraw].specular = pVerts[iDraw].specular;
			aOut[iDraw].tu = pVerts[iDraw].tu / pVerts[iDraw].m_Unk20;
			aOut[iDraw].tv = pVerts[iDraw].tv / pVerts[iDraw].m_Unk20;
		}
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, aOut, nVerts, 0);
	}
}

// ModelDraw::DrawProjectedModelShadows, 0x1002701e (5506 bytes), not matched: the projected-texture model shadow path of drawmodelshadows.  The control flow, every
// call (19 + the 45 COM calls), every constant and the whole data layout are reproduced (build.py audit: the only call difference is the
// 3-float _CVector ctor 0x1000dfb6, see item 1 below); not byte-identical yet.  Check/diff status with the RTM compiler (C1XX/C2 8168):
//   ours 5443 bytes (1651 instructions) vs exe 5506 (1645); ALIGNED 814 mismatching instructions, 241 of them when stack offsets are ignored.
// What differs (all of it is source shape, nothing is blamed on the toolchain):
//  1. The first expression of the light loop, `vModelPos - vLightW`: the exe expands operator-(LTVector by value) but keeps the nested 3-float
//     ctor out of line (call 0x1000dfb6, then `mov esi,eax` + 3 movsd into the local that Mag() and Norm() use); every later operator-/+/* of
//     the function has its ctor inlined.  Ours inlines the ctor there too.  inline_ballast "pending" shows it flips when ~100 more
//     inline-candidate call sites follow that expression (the nested share is (avail-cost)/(1+pending)), so the original had more (or
//     earlier, smaller) inline sites around it, e.g. the light search written as an inline helper (Jupiter D3DModelShadowRenderer::
//     GatherRelevantLightSources; static inline helper here is refused outright), accessors, or a smaller own size.  Also unexplained:
//     in these /O1 /Ob2 objects a function with any statement admits only ~3 `if(0)` ballast statements as a charged inline (budget probe), so
//     the wave-7 inline_budget.py model (B = max(1000u, 2 x size)) does not describe this compiler and measures B = 53u here.
//  2. Frame layout: frame 0x278c in the exe, 0x2780 ours; most scalars/LTVectors sit at different [ebp-N] (C2 orders frame slots by weighted
//     reference count and size, and shares dead slots of block locals: in the exe the dead light-distance array shares its slots with the
//     MakeInverse temporary at -0x4cc, aLightPos/Dir/Atten at -0x5ac/-0x54c/-0x4ec; only the order of the 7 matrices and of the arrays is
//     already identical).  About 570 of the 814 mismatches are only this.
//  3. x87 operand order of ~15 expressions (the quad vertex scaling `fld v; fmul scale`, the window/frustum plane arithmetic) and the
//     dup `fld st(0)` of the fMaxX/fMaxY compares: first-reference order, depends on where the values are first mentioned.
// Source shape (reconstructed from the code, not from a source file): see the per-block comments.
// LTVector::operator-(LTVector) is emitted by the light-position subtraction below; the shared COMDAT remains at 0x1000e06c.
// FUNCTION: D3DREN 0x1000e06c ??G?$_CVector@M@@QBE?AV0@V0@@Z
// STUB: D3DREN 0x1002701e
void ModelDraw::DrawProjectedModelShadows(uint32 nMaxShadows)
{
	LTVector vModelPos = m_pInstance->m_Pos;
	LTVector vDims = m_pInstance->m_Dims;
	uint32 nLights = 0;
	float aLightPos[NUM_MODEL_SHADOWS][3];
	float aLightDir[NUM_MODEL_SHADOWS][3];
	float aLightAtten[NUM_MODEL_SHADOWS];
	uint32 i, j;

	// find the nearest lights
	{
		float aLightDist[NUM_MODEL_SHADOWS];
		for (i = 0; i < (uint32)m_nModelLights; i++)
		{
			UnkType_ModelLight *pLight = &m_Unk3c[i];
			LTVector vLight = pLight->m_Unk00;
			LTVector vLightW;
			MatVMul(&vLightW, &m_ModelTransform, &vLight);
			LTVector vDelta = vModelPos - vLightW;
			float fDist = vDelta.Mag();
			LTVector vDir = vDelta;
			vDir.Norm(1.0f);

			uint32 slot = nLights;
			if (nLights == nMaxShadows)
			{
				for (slot = 0; slot < nMaxShadows; slot++)
				{
					if (aLightDist[slot] > fDist)
						break;
				}
			}
			if (slot != nMaxShadows)
			{
				aLightPos[slot][0] = vLightW.x;
				aLightPos[slot][1] = vLightW.y;
				aLightPos[slot][2] = vLightW.z;
				aLightDir[slot][0] = vDir.x;
				aLightDir[slot][1] = vDir.y;
				aLightDir[slot][2] = vDir.z;
				aLightDist[slot] = fDist;
				aLightAtten[slot] = (float)sqrt(pLight->m_Unk0c);
				if (nLights < nMaxShadows)
					nLights++;
			}
		}
	}

	if (nLights == 0)
		return;

	int nRes = LTCLAMP(g_CV_ModelShadowProjRes.m_IntVal, 8, 256);
	if (nRes & (nRes - 1))
	{
		g_pStruct->ConsolePrint("Invalid texture size for shadows -- must be power of 2...");
		g_CV_ModelShadowProjRes.m_IntVal = nRes = 128;
		g_CV_ModelShadowProjRes.m_FloatVal = 128.0f;
	}

	uint32 nLOD = LTMIN(m_nLOD + g_CV_ModelShadowProjLOD.m_IntVal, m_pModel->m_LODDists.GetSize() - 1);

	float fRadius = vDims.Mag();
	float fDiam = fRadius * 2.0f;

	// save the device state
	IDirectDrawSurface7 *pOldTexture;
	DWORD oldRS[6];
	DWORD oldTSS[7];
	g_pD3DDevice->GetTexture(0, &pOldTexture);
	g_pD3DDevice->SetTexture(0, NULL);
	g_pD3DDevice->SetTexture(1, NULL);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_ADDRESS, &oldTSS[0]);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_COLORARG1, &oldTSS[1]);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_COLORARG2, &oldTSS[2]);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_COLOROP, &oldTSS[3]);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_ALPHAARG1, &oldTSS[4]);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_ALPHAARG2, &oldTSS[5]);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_ALPHAOP, &oldTSS[6]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZENABLE, &oldRS[0]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZWRITEENABLE, &oldRS[1]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &oldRS[2]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, &oldRS[3]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_SRCBLEND, &oldRS[4]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_DESTBLEND, &oldRS[5]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);

	IShadowTexture *pTexB = GetCachedSilhouetteShadowTexture(nRes, nRes);
	UnkType_ShadowPolys aPolys[NUM_MODEL_SHADOWS];

	for (i = 0; i < nLights; i++)
	{
		float fAtten = aLightAtten[i];
		LTVector vLightPos;
		vLightPos.x = aLightPos[i][0];
		vLightPos.y = aLightPos[i][1];
		vLightPos.z = aLightPos[i][2];
		LTVector vDir;
		vDir.x = aLightDir[i][0];
		vDir.y = aLightDir[i][1];
		vDir.z = aLightDir[i][2];

		UnkType_ShadowPolyQuery query;
		aPolys[i].m_nPolys = 0;
		query.m_pPolys = &aPolys[i];
		query.m_vStart = vLightPos;
		query.m_vEnd = vLightPos + vDir * fAtten;
		query.m_fRadius = fDiam;
		query.m_vOrigin = vLightPos;
		query.m_vDir = vDir;
		g_pFrameMainWorld->m_WorldTree.FindObjectsOnPoint(&vModelPos, (WTObjCallback)CollectWorldModelSegmentPolys, &query, 0);

		if (aPolys[i].m_nPolys == 0)
			continue;

		// the light's frame of reference
		LTVector vUp0;
		vUp0.x = 0.0f;
		vUp0.y = 1.0f;
		vUp0.z = 0.0f;
		if ((float)fabs(vDir.y) > 0.9999f)
		{
			vDir.z += 0.01f;
			vDir.Norm(1.0f);
		}
		LTVector vRight = vDir.Cross(vUp0);
		vRight.Norm(1.0f);
		LTVector vUp = vRight.Cross(vDir);
		vUp.Norm(1.0f);

		// draw the silhouette of the model in the corner of the backbuffer
		pTexB->CopyFromOffscreen(0, 0);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, 0);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 0);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

		float fRes = (float)nRes;
		UnkType_ShadowVertex quad[4];
		for (j = 0; j < 4; j++)
		{
			quad[j].rhw = 0.0f;
			quad[j].color = 0xffffffff;
			quad[j].tu = 0.0f;
			quad[j].tv = 0.0f;
		}
		quad[0].m_Vec.x = 0.0f;
		quad[0].m_Vec.y = 0.0f;
		quad[0].m_Vec.z = 0.0f;
		quad[1].m_Vec.x = 0.0f;
		quad[1].m_Vec.y = fRes;
		quad[1].m_Vec.z = 0.0f;
		quad[2].m_Vec.x = fRes;
		quad[2].m_Vec.y = fRes;
		quad[2].m_Vec.z = 0.0f;
		quad[3].m_Vec.x = fRes;
		quad[3].m_Vec.y = 0.0f;
		quad[3].m_Vec.z = 0.0f;
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, quad, 4, 0);
		for (j = 0; j < 4; j++)
			quad[j].color = 0;

		float fMaxX = 0.0f;
		float fMaxY = 0.0f;
		TLVertex *pVerts = m_Unk82c;
		LTMatrix mInvView = g_ViewParams.m_mClipTransform.MakeInverse();

		uint32 iPiece;
		for (iPiece = 0; iPiece < m_pModel->NumPieces(); iPiece++)
		{
			ModelPiece *pPiece = m_pModel->GetPiece(iPiece);
			PieceLOD *pLOD = pPiece->GetLOD(nLOD);
			if (pLOD != NULL)
			{
				PieceLOD *pLOD2 = pLOD;
				if (m_bLODBlend)
					pLOD2 = pPiece->GetLOD(nLOD + 1);
				if ((m_pInstance->m_HiddenPieces & (1 << iPiece)) == 0)
				{
					LTVector vLighting, vMin, vMax;
					SkinAndLightPieceVertices(pLOD, pLOD2, pVerts, m_Unk5ec, m_pModel->m_Transforms.GetArray(), &vLighting.x, 0, &vMin.x, &vMax.x);
					TLVertex *pV = pVerts;
					uint32 nVerts = pLOD->m_Verts.GetSize();
					if (nVerts != 0)
					{
						float fScale = fRes / fRadius;
						for (j = 0; j < nVerts; j++)
						{
							LTVector vWorld;
							MatVMul(&vWorld, &mInvView, &pV->m_Vec);
							pV->m_Vec = vWorld;
							LTVector vRel = pV->m_Vec - vModelPos;
							pV->m_Vec.x = (vRight.x * vRel.x + vRight.y * vRel.y + vRight.z * vRel.z) * fScale * 0.5f;
							pV->m_Vec.y = (vUp.x * vRel.x + vUp.y * vRel.y + vUp.z * vRel.z) * fScale * 0.5f;
							pV->m_Vec.z = 0.0f;
							if (fabs(pV->m_Vec.x) > fMaxX)
								fMaxX = (float)fabs(pV->m_Vec.x);
							if (fabs(pV->m_Vec.y) > fMaxY)
								fMaxY = (float)fabs(pV->m_Vec.y);
							pV++;
						}
					}
					pVerts += nVerts;
				}
			}
		}

		float fScaleX = (fRes - 2.0f) / (fMaxX * 2.0f);
		float fScaleY = (fRes - 2.0f) / (fMaxY * 2.0f);
		pVerts = m_Unk82c;
		for (iPiece = 0; iPiece < m_pModel->NumPieces(); iPiece++)
		{
			ModelPiece *pPiece = m_pModel->GetPiece(iPiece);
			PieceLOD *pLOD = pPiece->GetLOD(nLOD);
			if (pLOD != NULL)
			{
				if ((m_pInstance->m_HiddenPieces & (1 << iPiece)) == 0)
				{
					ModelTri *pTri = pLOD->m_Tris.GetArray();
					uint32 nTris = pLOD->m_Tris.GetSize();
					if (nTris != 0)
					{
						float fHalf = fRes * 0.5f;
						for (j = 0; j < nTris; j++)
						{
							quad[0].m_Vec = pVerts[pTri->m_Indices[0]].m_Vec;
							quad[1].m_Vec = pVerts[pTri->m_Indices[1]].m_Vec;
							quad[2].m_Vec = pVerts[pTri->m_Indices[2]].m_Vec;
							quad[0].m_Vec.x *= fScaleX;
							quad[1].m_Vec.x *= fScaleX;
							quad[2].m_Vec.x *= fScaleX;
							quad[0].m_Vec.y *= fScaleY;
							quad[1].m_Vec.y *= fScaleY;
							quad[2].m_Vec.y *= fScaleY;
							quad[0].m_Vec.x += fHalf;
							quad[1].m_Vec.x += fHalf;
							quad[2].m_Vec.x += fHalf;
							quad[0].m_Vec.y += fHalf;
							quad[1].m_Vec.y += fHalf;
							quad[2].m_Vec.y += fHalf;
							g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, quad, 3, 0);
							pTri++;
						}
					}
					pVerts += pLOD->m_Verts.GetSize();
				}
			}
		}

		IShadowTexture *pTexA = GetCachedProjectionShadowTexture(nRes, nRes);
		pTexA->CopyFromOffscreen(0, 0);
		pTexB->CopyToOffscreen(0, 0);
		if (g_CV_ModelShadowProjShow.m_IntVal)
			pTexA->DrawScreenQuad(nRes, (nLights - i - 1) * nRes, 0xffffffff, 0);

		// how the shadow is projected onto the world
		ShadowLightInfo info;
		info.m_Vecs[0] = vRight;
		info.m_Vecs[1] = vUp;
		info.m_Vecs[2] = vDir;
		info.m_fSizeX = info.m_fSizeY = fDiam;
		info.m_vLightOrigin = vLightPos;
		info.m_ProjectionPlane.m_Normal = vDir;
		info.m_vProjectionCenter = vModelPos;
		info.m_ProjectionPlane.m_Dist = info.m_ProjectionPlane.m_Normal.Dot(info.m_vProjectionCenter);

		LTVector vWindowLeft = info.m_vProjectionCenter - info.m_Vecs[0] * info.m_fSizeX * 0.5f;
		LTVector vWindowRight = info.m_vProjectionCenter + info.m_Vecs[0] * info.m_fSizeX * 0.5f;
		LTVector vWindowTop = info.m_vProjectionCenter + info.m_Vecs[1] * info.m_fSizeY * 0.5f;
		LTVector vWindowBottom = info.m_vProjectionCenter - info.m_Vecs[1] * info.m_fSizeY * 0.5f;
		info.m_vWindowTopLeft = info.m_vProjectionCenter - info.m_Vecs[0] * info.m_fSizeX * 0.5f - info.m_Vecs[1] * info.m_fSizeY * 0.5f;

		LTPlane *pPlane = &info.m_FrustumPlanes[0];
		pPlane->m_Normal = info.m_Vecs[2];
		pPlane->m_Dist = pPlane->m_Normal.Dot(vModelPos - info.m_Vecs[2] * fRadius);

		pPlane = &info.m_FrustumPlanes[1];
		pPlane->m_Normal = -info.m_Vecs[2];
		pPlane->m_Dist = pPlane->m_Normal.Dot(vModelPos + info.m_Vecs[2] * fAtten);

		pPlane = &info.m_FrustumPlanes[2];
		pPlane->m_Normal = (vWindowLeft - info.m_vLightOrigin).Cross(info.m_Vecs[1]);
		pPlane->m_Normal.Norm(1.0f);
		pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

		pPlane = &info.m_FrustumPlanes[3];
		pPlane->m_Normal = info.m_Vecs[1].Cross(vWindowRight - info.m_vLightOrigin);
		pPlane->m_Normal.Norm(1.0f);
		pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

		pPlane = &info.m_FrustumPlanes[4];
		pPlane->m_Normal = (vWindowTop - info.m_vLightOrigin).Cross(info.m_Vecs[0]);
		pPlane->m_Normal.Norm(1.0f);
		pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

		pPlane = &info.m_FrustumPlanes[5];
		pPlane->m_Normal = info.m_Vecs[0].Cross(vWindowBottom - info.m_vLightOrigin);
		pPlane->m_Normal.Norm(1.0f);
		pPlane->m_Dist = pPlane->m_Normal.Dot(info.m_vLightOrigin);

		LTMatrix mShadow;
		mShadow.SetupProjectionMatrix(info.m_vLightOrigin, info.m_ProjectionPlane);

		LTMatrix mTex;
		mTex.Init(info.m_Vecs[0].x, info.m_Vecs[0].y, info.m_Vecs[0].z, -info.m_Vecs[0].Dot(info.m_vWindowTopLeft),
			info.m_Vecs[1].x, info.m_Vecs[1].y, info.m_Vecs[1].z, -info.m_Vecs[1].Dot(info.m_vWindowTopLeft),
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		LTMatrix mScale;
		mScale.Init(fScaleX / info.m_fSizeX, 0.0f, 0.0f, (1.0f - fScaleX) * 0.5f,
			0.0f, fScaleY / info.m_fSizeY, 0.0f, (1.0f - fScaleY) * 0.5f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		info.m_Unk60 = mScale * mTex * mShadow;

		// draw it onto the polygons
		pTexA->BindForShadowMultiply();
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, 1);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 0);
		for (j = 0; j < aPolys[i].m_nPolys; j++)
		{
			WorldPoly *pPoly = aPolys[i].m_Polys[j];
			LTPlane *pPolyPlane = pPoly->m_pPlane;
			float fPlaneDist = pPolyPlane->m_Dist;
			LTVector vNormal = pPolyPlane->m_Normal;
			float fSideLight = vNormal.Dot(vLightPos) - fPlaneDist;
			float fSideModel = vNormal.Dot(vModelPos) - fPlaneDist;
			if ((fSideLight > 0.0f && fSideModel > 0.0f) || (fSideLight < 0.0f && fSideModel < 0.0f))
				DrawProjectedShadowOnWorldPoly(&info, pPoly, fAtten);
		}
	}

	// restore the device state
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, oldTSS[0]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, oldTSS[1]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, oldTSS[2]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, oldTSS[3]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, oldTSS[4]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, oldTSS[5]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, oldTSS[6]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, oldRS[0]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, oldRS[1]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, oldRS[2]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, oldRS[3]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, oldRS[4]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, oldRS[5]);
	g_pD3DDevice->SetTexture(0, pOldTexture);
}
