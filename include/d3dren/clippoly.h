#ifndef __D3DREN_CLIPPOLY_H__
#define __D3DREN_CLIPPOLY_H__

// The near and left plane clippers of the model polygon clipper: Jupiter polyclip.h expanded in an inline function per plane and
// vertex type.  ClipModelPolygon32/40 (unit unk/10001000) inline them; their out-of-line copies are ClipPolyNear/ClipPolyLeft
// (0x1000b0cd/0x1000b20c, unit unk/100098d0) and ClipPolyNear40/ClipPolyLeft40 (0x100088ec.., unit unk/10007930), which still define
// them out of line there.  The inside[] arrays are the function-local statics of the original (one copy shared by every expansion).
// The header name is invented (no evidence); *ppVerts/*pnVerts are the polygon, replaced by the clipped copy built at *ppOut (which
// advances); each returns 0 when nothing is inside.  The first argument is unused.

#include "d3dren/tlvertex.h"
#include "d3dren/viewparams.h"
#include "d3dren/pool.h"		// IntersectNearClipPlane, IntersectLeftClipPlane

// GLOBAL: D3DREN 0x10094de0
extern int g_ClipNearInsideFlagsTLVertex[56];	// guess: Jupiter polyclip.h bInside[] of the near plane (static here)
// GLOBAL: D3DREN 0x10094ec0
extern int g_ClipLeftInsideFlagsTLVertex[56];	// guess: bInside[] of the left plane
// GLOBAL: D3DREN 0x10094c20
extern int g_ClipNearInsideFlagsVertex40[56];	// guess: bInside[] of the near plane, 0x28-byte vertices
// GLOBAL: D3DREN 0x10094d00
extern int g_ClipLeftInsideFlagsVertex40[56];	// guess: bInside[] of the left plane, 0x28-byte vertices

void TLVertex_ClipExtra(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t);
void TLVertex40_ClipExtra(UnkType_TLVertex40 *pPrev, UnkType_TLVertex40 *pCur, UnkType_TLVertex40 *pOut, float t);

// One plane of polyclip.h: T the vertex type, INSIDE the inside[] array, CLIPTEST the inside test of pCur, DOCLIP the intersection
// helper, CLIPEXTRA the attribute interpolation.
#define CLIPPOLY_BODY(T, INSIDE, CLIPTEST, DOCLIP, CLIPEXTRA) \
	T *&pVerts = *ppVerts; \
	int &nVerts = *pnVerts; \
	T *&pOut = *ppOut; \
	int nInside = 0; \
	int *pInside; \
	T *pPrev, *pCur, *pEnd, *pOldOut; \
	int iPrev, iCur; \
	float t; \
	\
	g_nPlaneClipTests++; \
	pInside = INSIDE; \
	pCur = pVerts; \
	pEnd = pCur + nVerts; \
	while (pCur != pEnd) \
	{ \
		*pInside = CLIPTEST; \
		nInside += *pInside; \
		++pInside; \
		++pCur; \
	} \
	if (nInside == 0) \
		return 0; \
	else if (nInside != nVerts) \
	{ \
		pOldOut = pOut; \
		iPrev = nVerts - 1; \
		pPrev = pVerts + iPrev; \
		for (iCur = 0; iCur < nVerts; iCur++) \
		{ \
			pCur = pVerts + iCur; \
			if (INSIDE[iPrev]) \
				*pOut++ = *pPrev; \
			if (INSIDE[iPrev] != INSIDE[iCur]) \
			{ \
				t = DOCLIP(pPrev->m_Vec, pCur->m_Vec, pOut->m_Vec); \
				CLIPEXTRA(pPrev, pCur, pOut, t); \
				++pOut; \
			} \
			iPrev = iCur; \
			pPrev = pCur; \
		} \
		nVerts = pOut - pOldOut; \
		pVerts = pOldOut; \
		pOut += nVerts; \
	} \
	return 1;

// near plane z >= g_ViewParams.m_NearZ (flag 1)
inline int ClipPolyNear(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	CLIPPOLY_BODY(TLVertex, g_ClipNearInsideFlagsTLVertex, pCur->m_Vec.z >= g_ViewParams.m_NearZ, IntersectNearClipPlane, TLVertex_ClipExtra)
}

// left plane -z < x (flag 4)
inline int ClipPolyLeft(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	CLIPPOLY_BODY(TLVertex, g_ClipLeftInsideFlagsTLVertex, -pCur->m_Vec.z < pCur->m_Vec.x, IntersectLeftClipPlane, TLVertex_ClipExtra)
}

inline int ClipPolyNear40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	CLIPPOLY_BODY(UnkType_TLVertex40, g_ClipNearInsideFlagsVertex40, pCur->m_Vec.z >= g_ViewParams.m_NearZ, IntersectNearClipPlane, TLVertex40_ClipExtra)
}

inline int ClipPolyLeft40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	CLIPPOLY_BODY(UnkType_TLVertex40, g_ClipLeftInsideFlagsVertex40, -pCur->m_Vec.z < pCur->m_Vec.x, IntersectLeftClipPlane, TLVertex40_ClipExtra)
}

#endif
