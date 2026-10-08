// d3d.ren unk/10031d18 (0x10031d18-0x10031e94): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// DirectDraw device enumeration.
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

void FUN_10010967();		// common_init: frees the device list (0x10057800)

// guess: the DirectDraw callback of the enumeration (LPDDENUMCALLBACKA): creates a DirectDraw object for the device, reads its caps
// and appends a node (UnkType_DeviceNode) to the device list.
// FUNCTION: D3DREN 0x10031d82
BOOL WINAPI DDEnumCallback(GUID *lpGUID, LPSTR lpDriverDescription, LPSTR lpDriverName, LPVOID lpContext)
{
	LPDIRECTDRAW pDD;
	DDCAPS halCaps;
	DDCAPS helCaps;
	UnkType_DeviceNode *pNode;

	if (DirectDrawCreate(lpGUID, &pDD, 0) != 0)
		return TRUE;

	memset(&halCaps, 0, sizeof(halCaps));
	halCaps.dwSize = sizeof(halCaps);
	memset(&helCaps, 0, sizeof(helCaps));
	helCaps.dwSize = sizeof(helCaps);
	if (pDD->GetCaps(&halCaps, &helCaps) != 0)
	{
		pDD->Release();
		return TRUE;
	}

	pNode = (UnkType_DeviceNode *)dalloc(sizeof(UnkType_DeviceNode));
	if (pNode)
	{
		pNode->m_Unk0c = halCaps.dwCaps & DDCAPS_3D;
		pNode->m_Link.m_pData = pNode;
		if (lpGUID)
		{
			pNode->m_Guid = *lpGUID;
			pNode->m_pGuid = &pNode->m_Guid;
		}
		else
		{
			pNode->m_pGuid = 0;
		}
		strncpy(pNode->m_Unk24, lpDriverName, 100);
		strncpy(pNode->m_Unk88, lpDriverDescription, 100);
		DAT_10057800.AddAfter(&pNode->m_Link);
	}
	pDD->Release();
	return TRUE;
}

// guess: the DirectDrawEnumerateExA flavour of the callback (LPDDENUMCALLBACKEXA): ignores the monitor.
// FUNCTION: D3DREN 0x10031e7c
BOOL WINAPI DDEnumCallbackEx(GUID *lpGUID, LPSTR lpDriverDescription, LPSTR lpDriverName, LPVOID lpContext, HMONITOR hm)
{
	return DDEnumCallback(lpGUID, lpDriverDescription, lpDriverName, lpContext);
}

// guess: enumerates the DirectDraw devices into the device list: DirectDrawEnumerateExA through GetProcAddress when ddraw.dll has
// it, else the plain DirectDrawEnumerate.  Called by d3d_Init and GetSupportedModes.
// FUNCTION: D3DREN 0x10031d18
BOOL FUN_10031d18()
{
	HMODULE hModule;
	FARPROC pfnEnumEx;

	FUN_10010967();
	hModule = GetModuleHandleA("DDRAW.DLL");
	if (hModule)
	{
		pfnEnumEx = GetProcAddress(hModule, "DirectDrawEnumerateExA");
		if (pfnEnumEx)
		{
			if (((LPDIRECTDRAWENUMERATEEXA)pfnEnumEx)(DDEnumCallbackEx, 0, DDENUM_ATTACHEDSECONDARYDEVICES | DDENUM_DETACHEDSECONDARYDEVICES | DDENUM_NONDISPLAYDEVICES) == 0)
				return TRUE;
			AddDebugMessage(1, "DirectDrawEnumerateExA failed, using DirectDrawEnumerate");
		}
		else
		{
			AddDebugMessage(1, "Unable to get DirectDrawEnumerateExA proc address, using DirectDrawEnumerate");
		}
	}
	else
	{
		AddDebugMessage(1, "Unable to get ddraw.dll module handle");
	}
	FUN_10010967();
	return DirectDrawEnumerateA(DDEnumCallback, 0) == 0;
}


// ---------------------------------------------------------------------------------------------------------------------------
// geomroutines.cpp (engine twin src/shared/geomroutines.cpp) and intersect_line.cpp (engine twin src/world/intersect_line.cpp)
#include <math.h>


#define INTERSECT_EPSILON	0.01f

#define FrontSide	1
#define BackSide	0

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
