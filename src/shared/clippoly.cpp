// Talon poly/box clipping (not in Jupiter). The file sorts between clientshell.cpp and
// cloaderthread.cpp; its name is unknown. PolyTouchesBox clips a world poly against the box planes
// set up in g_BoxFindPlanes (collision.cpp SetupBox and si_FindPoliesTouchingBox) and returns the
// clipped poly's extents.
#include "bdefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "counter.h"
#include "boxfind.h"

#define MAX_CLIP_VERTS		256
#define MAX_CLIP_NEWVERTS	264


// Box plane i is the axis i/2 facing this way.
// GLOBAL: LITHTECH 0x004d0798
static float g_ClipSigns[6] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

// The clipping buffers.
struct PolyClipBuffer
{
	~PolyClipBuffer() {}

	LTVector	*m_In[MAX_CLIP_VERTS];			// the poly's vertices
	LTVector	m_NewVerts[MAX_CLIP_NEWVERTS];	// vertices made by clipping
	LTVector	*m_Out[MAX_CLIP_VERTS];			// the clipped polies, one after another
};


// The function static's destructor (registered with atexit).
// FUNCTION: LITHTECH 0x004172c0 _$E2

// Same size as the original (944) and the same shape: the sphere test (VEC_DISTSQR; the same code as a VEC_SUB
// written z, y, x), the new vertex VEC_LERP (the same code as the three hand-written lines), `pIn` copied before the plane loop, vertices addressed as pIn[iPrev]/pIn[i] and reloaded each time,
// What differs is the register assignment: the original keeps pPoly in esi,
// pOut in ebp, the plane index in ebx and pNew in edx, with nIn spilled to [esp+0x10]; ours has pOut in ebx and
// the plane index in edi. The order of the local declarations has no effect (VC6 assigns slots by use).
// Wave 6 phase 2: DistSqr/MagSqr/operator forms of the sphere test and an operator lerp are worse (163-386 aligned
// mismatches against 133).
// Wave 7: the plane loop counts iPlane < 6 (VC6 strength-reduces it to the dist pointer but keeps the signed `jl`;
// a pointer compare gave `jb`) and the min/max loop is `while (nIn--)` (no pre-test; nIn is known non-zero): the
// audit now matches except the function static's names (s_Buf/its guard have no exe names yet). 167 -> 157 aligned.
// Wave 7 phase 2: ALIGNED 157 (123 ignoring stack offsets). Audit: only the function static's data (ours
// ?$S1 guard and s_Buf, the exe's unnamed 0x4df010 guard, 0x4df018 m_In, 0x4df418 m_NewVerts, 0x4e0070 m_Out: the
// same objects at the same offsets), i.e. behaviour matches; no inline candidates. Remaining: register assignment
// as described above (esi/ebp/ebx/edx in the exe vs ours).
// PARKED: register allocation only (pOut/plane index in ebp/ebx in the exe, ebx/edi here); behaviour matches
// STUB: LITHTECH 0x00416f10 ?PolyTouchesBox@@YAIPAUWorldPoly@@PAX1@Z
LTBOOL PolyTouchesBox(WorldPoly *pPoly, void *pUnknown1, void *pUnknown2)
{
	static PolyClipBuffer s_Buf;
	LTVector *pMin, *pMax, *pNew, **pIn, **pOut, **pOutStart;
	float fRadius, sign, prevDist, curDist, t;
	int nIn, nNewVerts, iPlane, axis, i, iPrev;
	float *pDist;
	LTBOOL bPrevInside, bCurInside;

	pMin = (LTVector*)pUnknown1;
	pMax = (LTVector*)pUnknown2;

	g_nPolyTouchesBoxCalls++;
	CountAdder cntAdd(&g_Ticks_PolyTouchesBox);

	if (!(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID))
		return LTFALSE;

	// Quick sphere test.
	fRadius = g_BoxFindRadius + pPoly->m_Radius;
	if (VEC_DISTSQR(g_BoxFindCenter, pPoly->m_Center) > fRadius * fRadius)
		return LTFALSE;

	// Clip the poly into the box's planes.
	pIn = s_Buf.m_In;
	pOut = pOutStart = s_Buf.m_Out;
	nIn = pPoly->m_nVertices;
	nNewVerts = 0;
	for (i=0; i < pPoly->m_nVertices; i++)
	{
		pIn[i] = ((SPolyVertex*)(pPoly + 1))[i].m_Vec;
	}

	for (iPlane=0; iPlane < 6; iPlane++)
	{
		pDist = &g_BoxFindPlanes[iPlane].m_Dist;
		sign = g_ClipSigns[iPlane];
		axis = iPlane >> 1;

		iPrev = nIn - 1;
		prevDist = sign * (&pIn[iPrev]->x)[axis] - *pDist;
		bPrevInside = LTTRUE;
		if (!(prevDist > 0.001f))
			bPrevInside = LTFALSE;

		for (i=0; i < nIn; i++)
		{
			curDist = sign * (&pIn[i]->x)[axis] - *pDist;
			bCurInside = LTTRUE;
			if (!(curDist > 0.001f))
				bCurInside = LTFALSE;

			if (bPrevInside)
				*pOut++ = pIn[iPrev];

			if (bPrevInside != bCurInside)
			{
				LTVector *pCur = pIn[i];
				t = prevDist / (prevDist - curDist);
				pNew = &s_Buf.m_NewVerts[nNewVerts];
				VEC_LERP(*pNew, *pIn[iPrev], *pCur, t);
				*pOut++ = pNew;
				nNewVerts++;
			}

			prevDist = curDist;
			bPrevInside = bCurInside;
			iPrev = i;
		}

		// The next plane clips what this one left.
		pIn = pOutStart;
		nIn = pOut - pOutStart;
		pOutStart = pOut;
		if (!nIn)
			return LTFALSE;
	}

	if (pMin && pMax)
	{
		pMin->x = pMin->y = pMin->z = (float)MAX_CREAL;
		pMax->x = pMax->y = pMax->z = (float)-MAX_CREAL;

		while (nIn--)
		{
			VEC_MIN(*pMin, *pMin, **pIn);
			VEC_MAX(*pMax, *pMax, **pIn);
			pIn++;
		}
	}

	return LTTRUE;
}
