// d3d.ren unk/10032583 (0x10032583-0x100329b0): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// dynamic lightmap.
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
// FUNCTION: D3DREN 0x10032583 _$E3
// FUNCTION: D3DREN 0x10032588 _$E2
// GLOBAL: D3DREN 0x10076788
ConVar g_CV_LMDynamic("LMDynamic", 1.0f);
// FUNCTION: D3DREN 0x100325a2 _$E6
// FUNCTION: D3DREN 0x100325a7 _$E5
// GLOBAL: D3DREN 0x10076768
ConVar g_CV_LMDynamicScale("LMDynamicScale", 2.5f);
// FUNCTION: D3DREN 0x100325c5 _$E9
// FUNCTION: D3DREN 0x100325ca _$E8
// GLOBAL: D3DREN 0x10076ba8
ConVar g_CV_LMDynamicSize("LMDynamicSize", 16.0f);

// ---- dynamic lights on the lightmaps (LMDynamic) ----------------------------------------------------------------------------------
#include "pixelformat.h"
#include "ltmatrix.h"
#include "d3dren/tlvertex.h"
#include "d3dren/lightmap.h"

// The dynamic light records of a poly (WorldPoly+0x30): the same layout unit unk/10007930 and unk/10021d70 use under this name.
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;
	DynamicLight		*m_pLight;
	LTVector			m_Pos;
};

// The lightmap staging object (include/d3dren/lightmap.h: UnkType_LMLock; unit unk/10007930 declares the pointer type under this name).
struct UnkType_DynLMSetup : public UnkType_LMLock
{
};

uint32 FUN_10032825(float fIntensity, uint8 *pColor);
uint32 FUN_10032966(float fIntensity, uint8 *pColor);
void FUN_1003273a(void *pPixels, UnkType_DynLMSetup *pSetup, float fSqrDist, uint8 *pColor, int nUnused);
void FUN_1003287a(void *pPixels, UnkType_DynLMSetup *pSetup, float fSqrDist, uint8 *pColor, int nUnused);

// guess: draws the light of a dynamic light (the poly's pLight record) as a round blob of its colour into the locked staging lightmap
// (a radial falloff of the distance of the light to the poly plane); returns 0 when the light does not reach the poly or has no colour.
// fScale scales the colour (1.0 = as is).
// FUNCTION: D3DREN 0x100325e8
int FUN_100325e8(UnkType_DynLMSetup *pSetup, WorldPoly *pPoly, UnkType_PolyLight *pLight, float fScale)
{
	float fDist;
	float fRatio;
	float fSqr;
	TLRGB color;
	uint8 *pPixels;

	if (!g_CV_LMDynamic.m_IntVal)
		return 0;

	fDist = pPoly->m_pPlane->DistTo(pLight->m_Pos);
	fRatio = fDist / pLight->m_pLight->GetLightRadius((uint32)pLight->m_pLight);
	if (fRatio > 1.0f - 1.0f / (float)pSetup->m_Unk44)
		return 0;

	fSqr = fRatio * fRatio;
	color.r = (pLight->m_pLight->m_ColorR > 0x7f ? pLight->m_pLight->m_ColorR : 0x7f) - 0x7f;
	color.g = (pLight->m_pLight->m_ColorG > 0x7f ? pLight->m_pLight->m_ColorG : 0x7f) - 0x7f;
	color.b = (pLight->m_pLight->m_ColorB > 0x7f ? pLight->m_pLight->m_ColorB : 0x7f) - 0x7f;

	if (fScale != 1.0f)
	{
		color.r = (uint8)(int)(color.r * fScale);
		color.g = (uint8)(int)(color.g * fScale);
		color.b = (uint8)(int)(color.b * fScale);
	}

	if (*(uint32 *)&color == 0)
		return 0;

	pPixels = pSetup->m_Unk00;
	if (pSetup->m_Unk0c.GetType() == BPP_16)
		FUN_1003273a(pPixels, pSetup, fSqr, (uint8 *)&color, 0);
	else
		FUN_1003287a(pPixels, pSetup, fSqr, (uint8 *)&color, 0);
	return 1;
}

// guess: fills the 16 bit (RGB 555) staging lightmap: pixel = colour * (1 - distance^2) for the pixels inside the unit circle of the
// light (the border row and column stay black).
// STUB diagnosis: 2 bytes of 235 differ.  The member loads, width test, and pixel setup now match; only the final y increment differs:
//   the exe emits `fld fY; fadd fStepY`, ours emits `fld fStepY; fadd fY`.  Both source operand orders compile the same way; a named
//   float temporary changes other instructions and was reverted.
// STUB: D3DREN 0x1003273a
void FUN_1003273a(void *pPixels, UnkType_DynLMSetup *pSetup, float fSqrDist, uint8 *pColor, int nUnused)
{
	uint32 nPitch;
	uint32 nWidth, nHeight;
	uint32 x, y;
	float fX, fY, fStepX, fStepY;
	uint16 *pRow;
	uint16 *pPixel;

	nPitch = (uint32)pSetup->m_Unk04 >> 1;
	nHeight = pSetup->m_Unk48;
	nWidth = pSetup->m_Unk44;
	memset(pPixels, 0, (uint32)pSetup->m_Unk04 * nHeight >> 2);
	nWidth -= 2;
	nHeight -= 2;
	fY = -1.0f;
	fStepY = 2.0f / (float)nHeight;
	pRow = (uint16 *)pPixels + nPitch + 1;
	fStepX = 2.0f / (float)nWidth;
	for (y = nHeight; y; y--)
	{
		float fYSqr = fY * fY + fSqrDist;

		fX = -1.0f;
		if (nWidth > 0)
		{
			pPixel = pRow;
			x = nWidth;
			do
			{
				float fSqr = fX * fX + fYSqr;
				if (fSqr < 1.0f)
					*pPixel = (uint16)FUN_10032825(1.0f - fSqr, pColor);
				fX += fStepX;
				pPixel++;
			} while (--x);
		}
		fY = fStepY + fY;
		pRow += nPitch;
	}
}

// guess: packs colour * intensity into a 15 bit (RGB 555) texel through the multiplication table.
// FUNCTION: D3DREN 0x10032825
uint32 FUN_10032825(float fIntensity, uint8 *pColor)
{
	int iRow = ((uint8)(int)(fIntensity * 255.0f)) * 0x100;

	return (((DAT_10082168.m_Unk00[iRow + pColor[2]] & 0xf8) << 5 | (DAT_10082168.m_Unk00[iRow + pColor[1]] & 0xf8)) << 2) |
		(DAT_10082168.m_Unk00[iRow + pColor[0]] >> 3);
}

// guess: the 32 bit version of FUN_1003273a.
// STUB diagnosis: 2 bytes of 236 differ, the same y-increment x87 operand order as FUN_1003273a.
// STUB: D3DREN 0x1003287a
void FUN_1003287a(void *pPixels, UnkType_DynLMSetup *pSetup, float fSqrDist, uint8 *pColor, int nUnused)
{
	uint32 nPitch;
	uint32 nWidth, nHeight;
	uint32 x, y;
	float fX, fY, fStepX, fStepY;
	uint32 *pRow;
	uint32 *pPixel;

	nPitch = (uint32)pSetup->m_Unk04 >> 2;
	nHeight = pSetup->m_Unk48;
	nWidth = pSetup->m_Unk44;
	memset(pPixels, 0, (uint32)pSetup->m_Unk04 * nHeight >> 2);
	nWidth -= 2;
	nHeight -= 2;
	fY = -1.0f;
	fStepY = 2.0f / (float)nHeight;
	pRow = (uint32 *)pPixels + nPitch + 1;
	fStepX = 2.0f / (float)nWidth;
	for (y = nHeight; y; y--)
	{
		float fYSqr = fY * fY + fSqrDist;

		fX = -1.0f;
		if (nWidth > 0)
		{
			pPixel = pRow;
			x = nWidth;
			do
			{
				float fSqr = fX * fX + fYSqr;
				if (fSqr < 1.0f)
					*pPixel = FUN_10032966(1.0f - fSqr, pColor);
				fX += fStepX;
				pPixel++;
			} while (--x);
		}
		fY = fStepY + fY;
		pRow += nPitch;
	}
}

// guess: packs colour * intensity into a 24 bit RGB texel (the 32 bit version of FUN_10032825).
// FUNCTION: D3DREN 0x10032966
uint32 FUN_10032966(float fIntensity, uint8 *pColor)
{
	int iRow = ((uint8)(int)(fIntensity * 255.0f)) * 0x100;

	return (DAT_10082168.m_Unk00[iRow + pColor[2]] << 8 | DAT_10082168.m_Unk00[iRow + pColor[1]]) << 8 |
		DAT_10082168.m_Unk00[iRow + pColor[0]];
}
