// d3d.ren world/intersect_line (0x10032031-0x100323f9): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// FLAGS: /O1 /Ob2
#include <windows.h>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "world_tree.h"
#include "fullintersectline.h"
#include "geomroutines.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"


// intersect_line.cpp
class IntersectRequest
{
public:
    IntersectRequest()
    {
        m_pPoints[0] = NULL;
        m_pPoints[1] = NULL;
        m_pIPos      = NULL;

        m_pNodeHit   = NULL;
        m_pQuery     = NULL;
        m_pWorldBsp  = NULL;
    }

// Input to the routine.
public:
    LTVector        *m_pPoints[2];
    LTVector        *m_pIPos;   // Intersection position.
    IntersectQuery  *m_pQuery;
    WorldBsp		*m_pWorldBsp;

// Output (if it returns LTTRUE).
public:
    Node			*m_pNodeHit;
};

Node* IntersectLine(Node *pRoot, LTVector *pPoint1, LTVector *pPoint2,
    LTVector *pIPos, LTPlane *pIPlane);
LTBOOL IntersectLineNode(Node *pRoot, IntersectRequest *pRequest);
extern LTObject *g_pIntersection;
extern float g_IntersectionBestDistSqr;
extern LTPlane g_IntersectionPlane;
extern LTVector g_IntersectionPos;
extern HPOLY g_hWorldPoly;
#define DO_PLANE_TEST_X(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = normalDirection;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.x * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Y(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = normalDirection;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.y * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Z(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = normalDirection;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.z * normalDirection;\
            return true;\
        }\
    }


// Just sets up the current 'closest object'.
#define USE_THIS_OBJECT(pServerObj, distSqr, plane, intersectionPt, hPoly) \
    g_IntersectionBestDistSqr = distSqr;\
    g_pIntersection = pServerObj;\
    g_IntersectionPlane = plane;\
    g_IntersectionPos = intersectionPt;\
    g_hWorldPoly = hPoly;


// ---------------------------------------------------------------------------------------------------------------------------
// geomroutines.cpp (engine twin src/shared/geomroutines.cpp) and intersect_line.cpp (engine twin src/world/intersect_line.cpp)
#include <math.h>


#define INTERSECT_EPSILON	0.01f

#define FrontSide	1
#define BackSide	0


// Tells if pPt is inside the convex poly.
// d3d.ren's copy of this function tests the surface's SURF_SOLID flag itself (the engine's does it in the caller) and is out of line.
// Reuses the radius-difference vector as the Cross argument temporary so VC6 emits the original local-copy schedule.
// FUNCTION: D3DREN 0x1003223b
LTBOOL InsideConvex(WorldPoly *pPoly, LTVector *pPt)
{
	LTPlane edgePlane;
	LTVector radiusDiff;
	float edgeDot;
	LTVector *pNormal;
	SPolyVertex *pCur, *pPrev, *pEnd;

	if (!(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID))
		return LTFALSE;

	// Reject it if it's outside the radius of the poly
	radiusDiff = pPoly->m_Center - *pPt;
	if (radiusDiff.MagSqr() > (pPoly->m_Radius * pPoly->m_Radius))
		return LTFALSE;

	pNormal = &pPoly->GetPlane()->m_Normal;
	pEnd  = (SPolyVertex*)(pPoly + 1) + pPoly->m_nVertices;
	pCur  = (SPolyVertex*)(pPoly + 1);
	pPrev = pEnd - 1;

	for(; pCur != pEnd; pPrev = pCur, ++pCur)
	{
		radiusDiff = *pPrev->m_Vec - *pCur->m_Vec;
		edgePlane.m_Normal = pNormal->Cross(radiusDiff);
		edgePlane.m_Normal.Norm();
		edgePlane.m_Dist = edgePlane.m_Normal.Dot(*pCur->m_Vec);

		edgeDot = edgePlane.DistTo(*pPt);
		if(edgeDot < -INTERSECT_EPSILON)
			return LTFALSE;
	}

	return LTTRUE;
}

// Engine twin (src/world/intersect_line.cpp); see the notes there on the inline budget of InsideConvex.
// FUNCTION: D3DREN 0x10032031
static LTBOOL InternalIntersectLineNode(
	Node *pRoot,
	IntersectRequest *pRequest,
	LTVector *pPoint1,
	LTVector *pPoint2)
{
	LTVector point1;
	LTVector iPoint;
	float intersection_t;
	float dot1, dot2;
	int side1;

	point1 = *pPoint1;

	while((pRoot->m_Flags & (NF_IN|NF_OUT)) == 0)
	{
		// Go into the correct side.
		dot1 = pRoot->GetPlane()->DistTo(point1);
		dot2 = pRoot->GetPlane()->DistTo(*pPoint2);

		// Handle the segment being entirely on one side of the plane
		if(dot1 > INTERSECT_EPSILON && dot2 > INTERSECT_EPSILON)
		{
			pRoot = pRoot->m_Sides[FrontSide];
		}
		else if(dot1 < -INTERSECT_EPSILON && dot2 < -INTERSECT_EPSILON)
		{
			pRoot = pRoot->m_Sides[BackSide];
		}
		else
		{
			// Ok, it crosses this plane.. go into the side that pFrom is on first.
			if((dot1 < -INTERSECT_EPSILON) || (dot1 > INTERSECT_EPSILON))
				side1 = (int)(dot1 > 0.0f);
			else
				side1 = (int)(dot2 < 0.0f);

			// Get the difference between the distances
			intersection_t = dot2 - dot1;
			if(intersection_t != 0.0f)
			{
				if((dot1 < -INTERSECT_EPSILON) || (dot1 > INTERSECT_EPSILON))
				{
					// Find the point of intersection
					intersection_t = -dot1 / intersection_t;
					VEC_LERP(iPoint, point1, *pPoint2, intersection_t);

					// Test the side the starting point is on.
					if(InternalIntersectLineNode(pRoot->m_Sides[side1], pRequest, &point1, &iPoint))
						return LTTRUE;

					// Check for a polygon intersection
					if((side1 == FrontSide) && pRoot->m_pPoly)
					{
						if(InsideConvex(pRoot->m_pPoly, &iPoint))
						{
							IntersectQuery *pQuery = pRequest->m_pQuery;
							if(!(pQuery && pQuery->m_PolyFilterFn && pRequest->m_pWorldBsp) ||
								pQuery->m_PolyFilterFn(pRequest->m_pWorldBsp->MakeHPoly(pRoot), pQuery->m_pUserData))
							{
								// Congratulations, we have a winner!
								pRequest->m_pNodeHit = pRoot;
								*pRequest->m_pIPos = iPoint;
								return LTTRUE;
							}
						}
					}
					else if(intersection_t > (1.0f - INTERSECT_EPSILON))
					{
						// Jump out if the ray doesn't go to the "other" side
						return LTFALSE;
					}

					// Clip the segment to the plane
					point1 = iPoint;
				}
			}

			// Go into the other side.
			pRoot = pRoot->m_Sides[!side1];
		}
	}

	return LTFALSE;
}

// FUNCTION: D3DREN 0x10032384
LTBOOL IntersectLineNode(
	Node *pRoot,
	IntersectRequest *pRequest)
{
	return InternalIntersectLineNode(
		pRoot,
		pRequest,
		pRequest->m_pPoints[0],
		pRequest->m_pPoints[1]
		);
}

// FUNCTION: D3DREN 0x1003239b
Node* IntersectLine(Node *pRoot, LTVector *pPoint1, LTVector *pPoint2, LTVector *pIPos, LTPlane *pIPlane)
{
	IntersectRequest req;

	req.m_pPoints[0] = pPoint1;
	req.m_pPoints[1] = pPoint2;
	req.m_pIPos = pIPos;

	if(IntersectLineNode(pRoot, &req))
	{
		*pIPlane = *req.m_pNodeHit->GetPlane();
		return req.m_pNodeHit;
	}

	return LTNULL;
}

// d3d_surface.h declares a class with a static member whose definition consumes a static-initialiser number: included here, after
// the static initialisers of the first object, so that their `_$E` numbers stay those of the exe.
#include "d3dren/d3d_surface.h"		// D3DShadowTextureFactory, IShadowTexture

// ---- dynamic lightmap console variables -----------------------------------------------------------------------------------------
#include "d3dren/rendererconsolevars.h"

// ---- dynamic lights on the lightmaps (LMDynamic) ----------------------------------------------------------------------------------
#include "pixelformat.h"
#include "ltmatrix.h"
#include "d3dren/tlvertex.h"
#include "d3dren/lightmap.h"
