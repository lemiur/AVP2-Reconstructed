// d3d.ren sys/d3d/common_draw (0x1000f3a0-0x1001094c): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// begins with 5 one-byte empty static initialisers + the ViewParams initialiser.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// sys/d3d/common_stuff: the first object of the renderer that this region of d3d.ren (0x1000f3a0-0x100132a0) holds is
// not one object but five: common_draw (0x1000f3a0), common_init (0x1001094c), common_stuff (0x1001116b, the console
// variable block + dalloc + AddDebugMessage ...), conparse (0x10012ef4) and counter (0x10013215).  They are kept in one
// file because the unit map gives them to one unit; the _$E numbers below are the numbers of this single object.
// FLAGS: /O1 /Ob2
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <windows.h>
#include "ltbasedefs.h"
#include "counter.h"
#include "../../../../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/tlvertex.h"
#include "d3dren/d3ddevice.h"
#include "pixelformat.h"

// BEGIN EARLY (common_draw + common_init, 0x1000f3a0-0x1001116a)
// ------------------------------------------------------------------ //
// common_draw (Jupiter render_a/src/sys/d3d/common_draw.cpp)
// ------------------------------------------------------------------ //

// Five global objects with empty inline constructors come first in this object: they leave five empty (1-byte) static
// initialiser functions at 0x1000f3a0-0x1000f3a4.  Their identity is unknown (the objects hold no data nothing could name);
// these placeholders keep the _$E numbering of everything after them the exe's.
class UnkType_EmptyCtor { public: UnkType_EmptyCtor() {} };
// FUNCTION: D3DREN 0x1000f3a0 _$E2
UnkType_EmptyCtor s_Unk1000f3a0;
// FUNCTION: D3DREN 0x1000f3a1 _$E5
UnkType_EmptyCtor s_Unk1000f3a1;
// FUNCTION: D3DREN 0x1000f3a2 _$E8
UnkType_EmptyCtor s_Unk1000f3a2;
// FUNCTION: D3DREN 0x1000f3a3 _$E11
UnkType_EmptyCtor s_Unk1000f3a3;
// FUNCTION: D3DREN 0x1000f3a4 _$E14
UnkType_EmptyCtor s_Unk1000f3a4;

// The renderer's view state (the LTRect member's inline constructor zeroes m_Rect: the whole static initialiser).
// FUNCTION: D3DREN 0x1000f3a5 _$E17
ViewParams g_ViewParams;

// FUNCTION: D3DREN 0x1000f3af ??0ViewParams@@QAE@XZ

// Globals of other units that this part reads.
// GLOBAL: D3DREN 0x10056770
extern MainWorld *DAT_10056770;			// guess: the engine's main world (names_proposal guess_g_pMainWorld, low)

void FUN_1000f40a(WorldBsp *pBsp);

// Talon-only per-frame visit codes (no member names known): the u16 at Leaf+0x2c and WorldPoly+0x46.
// FUNCTION: D3DREN 0x1000f3c0
void d3d_IncrementFrameCode(RenderContext *pContext)
{
	uint32 i;

	// Increment the point code.
	if (pContext->m_CurFrameCode == 0xFFFF)
	{
		pContext->m_CurFrameCode = 1;

		// The frame code wrapped: reset the codes of the leaves and polies of every world model.
		for (i = 0; i < DAT_10056770->m_WorldModels.GetSize(); i++)
		{
			FUN_1000f40a(DAT_10056770->m_WorldModels[i]->m_pOriginalBsp);
		}
	}

	++pContext->m_CurFrameCode;
}

// FUNCTION: D3DREN 0x1000f40a
// guess: resets the u16 frame codes of all leaves (+0x2c of each 0x30 byte leaf) and all polies (+0x46) of a world BSP
void FUN_1000f40a(WorldBsp *pBsp)
{
	uint32 i;

	for (i = 0; i < pBsp->m_nLeafs; i++)
	{
		*(uint16 *)((uint8 *)pBsp->m_Leafs + i * 0x30 + 0x2c) = 0;
	}

	for (i = 0; i < pBsp->m_nPolies; i++)
	{
		*(uint16 *)((uint8 *)pBsp->m_Polies[i] + 0x46) = 0;
	}
}

// FUNCTION: D3DREN 0x1000f458
// guess: the portal visibility test of a vis query (VisQueryRequest::m_Unknown24 = VQPortalFn): returns 0 when the portal's
// box (m_Center +- m_Dims) lies completely outside one of the six world-space clip planes of g_ViewParams
LTBOOL FUN_1000f458(BspPortal *pPortal)
{
	LTVector aCorners[8];
	LTVector vCorner;
	LTPlane *pPlane;
	int i, j;

	aCorners[0].Init(pPortal->m_Center.x + pPortal->m_Dims.x, pPortal->m_Center.y + pPortal->m_Dims.y, pPortal->m_Center.z + pPortal->m_Dims.z);
	aCorners[1].Init(pPortal->m_Center.x + pPortal->m_Dims.x, pPortal->m_Center.y - pPortal->m_Dims.y, pPortal->m_Center.z + pPortal->m_Dims.z);
	aCorners[2].Init(pPortal->m_Center.x - pPortal->m_Dims.x, pPortal->m_Center.y + pPortal->m_Dims.y, pPortal->m_Center.z + pPortal->m_Dims.z);
	aCorners[3].Init(pPortal->m_Center.x - pPortal->m_Dims.x, pPortal->m_Center.y - pPortal->m_Dims.y, pPortal->m_Center.z + pPortal->m_Dims.z);
	aCorners[4].Init(pPortal->m_Center.x + pPortal->m_Dims.x, pPortal->m_Center.y + pPortal->m_Dims.y, pPortal->m_Center.z - pPortal->m_Dims.z);
	aCorners[5].Init(pPortal->m_Center.x + pPortal->m_Dims.x, pPortal->m_Center.y - pPortal->m_Dims.y, pPortal->m_Center.z - pPortal->m_Dims.z);
	aCorners[6].Init(pPortal->m_Center.x - pPortal->m_Dims.x, pPortal->m_Center.y + pPortal->m_Dims.y, pPortal->m_Center.z - pPortal->m_Dims.z);
	aCorners[7].Init(pPortal->m_Center.x - pPortal->m_Dims.x, pPortal->m_Center.y - pPortal->m_Dims.y, pPortal->m_Center.z - pPortal->m_Dims.z);

	for (i = 0; i < 6; i++)
	{
		pPlane = &g_ViewParams.m_ClipPlanes[i];
		j = 0;
		do
		{
			if (j >= 8)
				return LTFALSE;
			vCorner = aCorners[j];
			if (pPlane->m_Normal.Dot(vCorner) - pPlane->m_Dist > 0.0f)
				break;
			j++;
		} while (1);
	}

	return LTTRUE;
}

// Init the view box.
// FUNCTION: D3DREN 0x1000f551
void d3d_InitViewBox(ViewBoxDef *pDef,
	float nearZ, float farZ, float xFov, float yFov)
{
	float xTemp, yTemp;

	pDef->m_NearZ = nearZ;
	pDef->m_FarZ = farZ;

	xTemp = MATH_HALFPI - (xFov * 0.5f);
	yTemp = MATH_HALFPI - (yFov * 0.5f);

	pDef->m_WindowSize[0] = (float)(cos(xTemp) / sin(xTemp));
	pDef->m_WindowSize[1] = (float)(cos(yTemp) / sin(yTemp));

	pDef->m_COP.Init(0.0f, 0.0f, 1.0f);
}

// This can be used for recursive rendering.  Pass in the screen extents of the box
// you want it to render into and it sets up the view box accordingly.
// FUNCTION: D3DREN 0x1000f5e3
void d3d_InitViewBox2(ViewBoxDef *pDef,
	float nearZ, float farZ,
	const ViewParams&  PrevParams,
	float screenMinX, float screenMinY,
	float screenMaxX, float screenMaxY)
{
	pDef->m_NearZ			= nearZ;
	pDef->m_FarZ			= farZ;

	// Get the extents of the previous view frustum's view window.
	float xMinPrev = PrevParams.m_ViewBox.m_COP.x - PrevParams.m_ViewBox.m_WindowSize[0];
	float yMinPrev = -PrevParams.m_ViewBox.m_COP.y - PrevParams.m_ViewBox.m_WindowSize[1];
	float xMaxPrev = PrevParams.m_ViewBox.m_COP.x + PrevParams.m_ViewBox.m_WindowSize[0];
	float yMaxPrev = -PrevParams.m_ViewBox.m_COP.y + PrevParams.m_ViewBox.m_WindowSize[1];

	// Get the percents.
	float xMin = (screenMinX - (float)PrevParams.m_Rect.left) / (float)(PrevParams.m_Rect.right - PrevParams.m_Rect.left);
	float yMin = (screenMinY - (float)PrevParams.m_Rect.top) / (float)(PrevParams.m_Rect.bottom - PrevParams.m_Rect.top);
	float xMax = (screenMaxX - (float)PrevParams.m_Rect.left) / (float)(PrevParams.m_Rect.right - PrevParams.m_Rect.left);
	float yMax = (screenMaxY - (float)PrevParams.m_Rect.top) / (float)(PrevParams.m_Rect.bottom - PrevParams.m_Rect.top);

	// Note: Y is negated here because we're going from screen to world space.

	// Translate them into our COP space.
	xMin = LTLERP(xMinPrev, xMaxPrev, xMin);
	yMin = -LTLERP(yMinPrev, yMaxPrev, yMin);
	xMax = LTLERP(xMinPrev, xMaxPrev, xMax);
	yMax = -LTLERP(yMinPrev, yMaxPrev, yMax);

	pDef->m_COP.z			= 1.0f;
	pDef->m_COP.x			= (xMin + xMax) * 0.5f;
	pDef->m_COP.y			= (yMin + yMax) * 0.5f;
	pDef->m_WindowSize[0]	= (xMax - xMin) * 0.5f;
	pDef->m_WindowSize[1]	= -(yMax - yMin) * 0.5f;
}

// guess: caches six elements of the projection matrices (called at the end of d3d_InitFrustum2)
// FUNCTION: D3DREN 0x1000f6de
void FUN_1000f6de(ViewParams *pParams)
{
	pParams->m_fProjXScale = pParams->m_DeviceTimesProjection.m[0][0];
	pParams->m_fProjXOffset = pParams->m_DeviceTimesProjection.m[0][2];
	pParams->m_fProjYScale = pParams->m_DeviceTimesProjection.m[1][1];
	pParams->m_fProjYOffset = pParams->m_DeviceTimesProjection.m[1][2];
	pParams->m_fProjZScale = pParams->m_DeviceTimesProjection.m[2][2];
	pParams->m_fProjZOffset = pParams->m_DeviceTimesProjection.m[2][3];
}

// Sky helper (0x100102b9, below) and the SDK inlines that the exe has out of line.
void d3d_SetupSkyStuff();
void d3d_SetupPerspectiveMatrix(LTMatrix *pMatrix, float nearZ, float farZ);

// Sets up pParams.
// pViewBox is the view window, the screen rectangle is the area on the screen that it maps into,
// pMat is the viewer matrix and vScale an extra scale that scales all the coordinates up.
// STUB: D3DREN 0x1000f72b
// Remaining difference (41 bytes / 7 aligned instructions of 952; same size 2766). The named half-screen width and height
// copies below align the device-transform and fRange scheduling. The remaining hunks are the m_mClipTransform copy relative to
// the next MatMul argument setup, and push/LEA order around the final hidden-return matrix temporary.
LTBOOL d3d_InitFrustum2(ViewParams *pParams,
	ViewBoxDef *pViewBox,
	float screenMinX, float screenMinY, float screenMaxX, float screenMaxY,
	LTMatrix *pMat, LTVector vScale)
{
	LTMatrix mTempWorld, mRotation, mScale;
	LTMatrix mFOVScale, mBackTransform;
	LTMatrix mDevice, mBackTranslate;
	LTMatrix mProjectionTransform;
	LTMatrix mFarScale, mFarProj;
	float leftX, rightX, topY, bottomY, normalZ;
	float xFarZ, yFarZ;
	uint32 i;
	LTVector forwardVec, zPlanePos;

	pParams->m_bPortalView = 0;
	pParams->m_mIdentity.Identity();
	pParams->m_mInvView = *pMat;

	/////// Copy stuff in and setup view limits.

	memcpy(&pParams->m_ViewBox, pViewBox, sizeof(pParams->m_ViewBox));
	pMat->GetTranslation(pParams->m_Pos);
	pParams->SetupFogViewPosition(pParams->m_Pos);

	pParams->m_FarZ = pViewBox->m_FarZ;
	if (pParams->m_FarZ < 3.0f) pParams->m_FarZ = 3.0f;
	if (pParams->m_FarZ > 100000.0f) pParams->m_FarZ = 100000.0f;
	pParams->m_NearZ = pViewBox->m_NearZ;
	pParams->m_ClipFarZ = pParams->m_FarZ;

	pParams->m_Rect.left = (int)RoundFloatToInt(screenMinX);
	pParams->m_Rect.top = (int)RoundFloatToInt(screenMinY);
	pParams->m_Rect.right = (int)RoundFloatToInt(screenMaxX);
	pParams->m_Rect.bottom = (int)RoundFloatToInt(screenMaxY);
	pParams->m_fScreenMinX = screenMinX;
	pParams->m_fScreenMinY = screenMinY;
	pParams->m_fScreenMaxX = screenMaxX - 1.0f;
	pParams->m_fScreenMaxY = screenMaxY - 1.0f;
	pParams->m_nScreenMinX = pParams->m_Rect.left;
	pParams->m_nScreenMinY = pParams->m_Rect.top;
	pParams->m_nScreenMaxX = pParams->m_Rect.right - 1;
	pParams->m_nScreenMaxY = pParams->m_Rect.bottom - 1;

	/////// Setup all the matrices.

	// Setup the rotation and translation transforms.
	mRotation = *pMat;
	mRotation.SetTranslation(0.0f, 0.0f, 0.0f);
	Mat_GetBasisVectors(&mRotation, &pParams->m_Right, &pParams->m_Up, &pParams->m_Forward);

	// We want to transpose (ie: when we're looking left, rotate the world to the right..)
	MatTranspose3x3(&mRotation);
	mBackTranslate.Init(
		1, 0, 0, -pParams->m_Pos.x,
		0, 1, 0, -pParams->m_Pos.y,
		0, 0, 1, -pParams->m_Pos.z,
		0, 0, 0, 1);
	MatMul(&mTempWorld, &mRotation, &mBackTranslate);

	// Scale it to get the full world transform.
	mScale.Init(
		vScale.x, 0, 0, 0,
		0, vScale.y, 0, 0,
		0, 0, vScale.z, 0,
		0, 0, 0, 1);
	MatMul(&pParams->m_mView, &mScale, &mTempWorld);

	// Shear so the center of projection is (0,0,COP.z)
	pParams->m_mShear.Init(
		1.0f, 0.0f, -pViewBox->m_COP.x/pViewBox->m_COP.z, 0.0f,
		0.0f, 1.0f, -pViewBox->m_COP.y/pViewBox->m_COP.z, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

	// Figure out X and Y scale to get frustum into unit slopes.
	pParams->m_fFovXScale = pViewBox->m_COP.z / pViewBox->m_WindowSize[0];
	pParams->m_fFovYScale = pViewBox->m_COP.z / pViewBox->m_WindowSize[1];

	pParams->m_fFovX = MATH_PI - 2.0f * (float)atan(pParams->m_fFovXScale);
	pParams->m_fFovY = MATH_PI - 2.0f * (float)atan(pParams->m_fFovYScale);

	// Squash the sides to 45 degree angles.
	mFOVScale.Init(
		pParams->m_fFovXScale, 0.0f, 0.0f, 0.0f,
		0.0f, pParams->m_fFovYScale, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

	pParams->m_mShearView = pParams->m_mShear * pParams->m_mView;
	pParams->m_mClipTransform = mFOVScale * pParams->m_mShearView;
	pParams->m_mReallyCloseClipTransform = mFOVScale * pParams->m_mShear;

	// Setup the projection transform.
	d3d_SetupPerspectiveMatrix(&mProjectionTransform, pViewBox->m_NearZ, g_ViewParams.m_FarZ);

	// Setup the projection space (-1<x<1) to device space transformation.
	pParams->m_fScreenWidth = (screenMaxX - screenMinX);
	pParams->m_fScreenHeight = (screenMaxY - screenMinY);
	pParams->m_fHalfScreenWidth = pParams->m_fScreenWidth * 0.5f;
	pParams->m_fHalfScreenHeight = pParams->m_fScreenHeight * 0.5f;
	pParams->m_fInvHalfScreenWidth = 1.0f / pParams->m_fHalfScreenWidth;
	pParams->m_fInvHalfScreenHeight = 1.0f / pParams->m_fHalfScreenHeight;
	pParams->m_fScreenCenterX = pParams->m_fHalfScreenWidth + screenMinX;
	pParams->m_fScreenCenterY = pParams->m_fHalfScreenHeight + screenMinY;
	pParams->m_fProjectionScaleX = pParams->m_fHalfScreenWidth * pParams->m_fFovXScale;
	pParams->m_fProjectionScaleY = pParams->m_fHalfScreenHeight * pParams->m_fFovYScale;
	pParams->m_fInvProjectionScaleX = 1.0f / pParams->m_fProjectionScaleX;
	pParams->m_fInvProjectionScaleY = 1.0f / pParams->m_fProjectionScaleY;
	pParams->m_fProjectionScaleProduct = pParams->m_fProjectionScaleY * pParams->m_fProjectionScaleX;

	// Setup the device transform.  It subtracts a little to account for the FP tendency
	// to slip above and below 0.5.
	mDevice.Identity();
	float fDeviceHalfWidth = pParams->m_fHalfScreenWidth;
	mDevice.m[0][0] = fDeviceHalfWidth - 0.0001f;
	mDevice.m[0][3] = pParams->m_fScreenCenterX;
	float fDeviceHalfHeight = pParams->m_fHalfScreenHeight;
	mDevice.m[1][1] = -(fDeviceHalfHeight - 0.0001f);
	mDevice.m[1][3] = pParams->m_fScreenCenterY;

	// Precalculate useful matrices.
	pParams->m_DeviceTimesProjection = mDevice * mProjectionTransform;
	pParams->m_FullTransform = pParams->m_DeviceTimesProjection * pParams->m_mClipTransform;

	FUN_1000f6de(pParams);

	float fInvFarZ = 1.0f / pViewBox->m_FarZ;
	mFarScale.Init(
		fInvFarZ, 0, 0, 0,
		0, fInvFarZ, 0, 0,
		0, 0, fInvFarZ, 0,
		0, 0, 0, 1);
	float fRange = 1.0f - fInvFarZ * pViewBox->m_NearZ;
	mFarProj.Init(
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1.0f / fRange, -((fInvFarZ * pViewBox->m_NearZ) / fRange),
		0, 0, 1, 0);
	pParams->m_mProjection = mFarProj * mFarScale * mFOVScale * pParams->m_mShear;

	/////// Setup the view frustum points in camera space.

	xFarZ = (pParams->m_FarZ * pViewBox->m_WindowSize[0]) / pViewBox->m_COP.z;
	yFarZ = (pViewBox->m_WindowSize[1] * pParams->m_FarZ) / pViewBox->m_COP.z;

	pParams->m_ViewPoints[0].Init(0.0f, 0.0f, 0.0f);
	pParams->m_ViewPoints[1].Init(-xFarZ, yFarZ, pParams->m_FarZ);
	pParams->m_ViewPoints[2].Init(xFarZ, yFarZ, pParams->m_FarZ);
	pParams->m_ViewPoints[3].Init(-xFarZ, -yFarZ, pParams->m_FarZ);
	pParams->m_ViewPoints[4].Init(xFarZ, -yFarZ, pParams->m_FarZ);

	// Transform them into world space.
	for (i = 0; i < 5; i++)
	{
		MatVMul_InPlace_Transposed3x3(&pParams->m_mView, &pParams->m_ViewPoints[i]);
		pParams->m_ViewPoints[i] += pParams->m_Pos;
	}

	/////// Setup the camera-space clipping planes.

	leftX = pViewBox->m_COP.x - pViewBox->m_WindowSize[0];
	rightX = pViewBox->m_COP.x + pViewBox->m_WindowSize[0];
	topY = pViewBox->m_COP.y + pViewBox->m_WindowSize[1];
	bottomY = pViewBox->m_COP.y - pViewBox->m_WindowSize[1];
	normalZ = pViewBox->m_COP.z;

	pParams->m_CSClipPlanes[0].m_Normal.Init(0.0f, 0.0f, 1.0f);		// Near Z
	pParams->m_CSClipPlanes[0].m_Dist = 1.0f;

	pParams->m_CSClipPlanes[1].m_Normal.Init(0.0f, 0.0f, -1.0f);	// Far Z
	pParams->m_CSClipPlanes[1].m_Dist = -pParams->m_FarZ;

	pParams->m_CSClipPlanes[2].m_Normal.Init(normalZ, 0.0f, -leftX);	// Left
	pParams->m_CSClipPlanes[4].m_Normal.Init(-normalZ, 0.0f, rightX);	// Right
	pParams->m_CSClipPlanes[3].m_Normal.Init(0.0f, -normalZ, topY);		// Top
	pParams->m_CSClipPlanes[5].m_Normal.Init(0.0f, normalZ, -bottomY);	// Bottom

	pParams->m_CSClipPlanes[2].m_Normal.Norm();
	pParams->m_CSClipPlanes[3].m_Normal.Norm();
	pParams->m_CSClipPlanes[4].m_Normal.Norm();
	pParams->m_CSClipPlanes[5].m_Normal.Norm();

	pParams->m_CSClipPlanes[3].m_Dist = pParams->m_CSClipPlanes[5].m_Dist = 0.0f;
	pParams->m_CSClipPlanes[2].m_Dist = pParams->m_CSClipPlanes[4].m_Dist = 0.0f;

	// Now setup the world space clipping planes.
	mBackTransform = pParams->m_mView;
	MatTranspose3x3(&mBackTransform);
	for (i = 0; i < 6; i++)
	{
		if (i != 0 && i != 1)
		{
			MatVMul_3x3(&pParams->m_ClipPlanes[i].m_Normal, &mBackTransform, &pParams->m_CSClipPlanes[i].m_Normal);
			pParams->m_ClipPlanes[i].m_Dist = pParams->m_ClipPlanes[i].m_Normal.Dot(pParams->m_Pos);
		}
	}

	// The Z planes need to be handled a little differently.
	forwardVec.Init(mRotation.m[2][0], mRotation.m[2][1], mRotation.m[2][2]);

	zPlanePos = forwardVec * pViewBox->m_NearZ;
	zPlanePos += pParams->m_Pos;

	MatVMul_3x3(&pParams->m_ClipPlanes[0].m_Normal,
		&mBackTransform, &pParams->m_CSClipPlanes[0].m_Normal);

	pParams->m_ClipPlanes[0].m_Dist =
		pParams->m_ClipPlanes[0].m_Normal.Dot(zPlanePos);

	zPlanePos = forwardVec * pParams->m_FarZ;
	zPlanePos += pParams->m_Pos;

	MatVMul_3x3(&pParams->m_ClipPlanes[1].m_Normal,
		&mBackTransform, &pParams->m_CSClipPlanes[1].m_Normal);

	pParams->m_ClipPlanes[1].m_Dist =
		pParams->m_ClipPlanes[1].m_Normal.Dot(zPlanePos);

	memcpy(pParams->m_ReallyCloseClipPlanes, pParams->m_CSClipPlanes, sizeof(pParams->m_CSClipPlanes));
	pParams->m_ReallyCloseClipPlanes[0].m_Dist = g_CV_ReallyCloseNearZ.m_FloatVal;
	pParams->m_bCullFlip = 0;
	pParams->m_fCullSign = 1.0f;

	d3d_SetupSkyStuff();
	return LTTRUE;
}

// SDK inline copies emitted here (first needed by d3d_InitFrustum2): out-of-line, annotated by mangled name.
// FUNCTION: D3DREN 0x100101f9 ?MatTranspose3x3@@YAXPAVLTMatrix@@@Z
// FUNCTION: D3DREN 0x10010222 ?MatVMul_InPlace_Transposed3x3@@YAXPAVLTMatrix@@PAV?$_CVector@M@@@Z

// FUNCTION: D3DREN 0x10010281
void d3d_SetupPerspectiveMatrix(LTMatrix *pMatrix, float nearZ, float farZ)
{
	pMatrix->Identity();
	pMatrix->m[2][2] = farZ / (farZ - nearZ);
	pMatrix->m[2][3] = (-(nearZ * farZ)) / (farZ - nearZ);
	pMatrix->m[3][2] = 1.0f;
	pMatrix->m[3][3] = 0.0f;
}

// NAME: d3d_SetupSkyStuff: Jupiter common_draw.cpp static d3d_SetupSkyStuff (the Talon one takes no arguments: it works on
// g_pSceneDesc->m_SkyDef and g_ViewParams)
// STUB: D3DREN 0x100102b9
// Remaining difference: 266 vs 265 bytes, 202 differing bytes. The final vector temporary preserves the original three-DWORD
// copy into m_SkyViewPos; the world-extents/percentage temporaries and register allocation still differ.
void d3d_SetupSkyStuff()
{
	LTVector percents;

	if (g_pSceneDesc->m_DrawMode == DRAWMODE_NORMAL)
	{
		const LTVector &min = DAT_10056770->m_ExtentsMin;
		const LTVector &max = DAT_10056770->m_ExtentsMax;
		percents = (g_ViewParams.m_Pos - min) / (max - min);
	}
	else
	{
		percents.Init(0.5f, 0.5f, 0.5f);
	}

	const SkyDef &Def = g_pSceneDesc->m_SkyDef;
	const LTVector &min = Def.m_ViewMin;
	LTVector vRange, vWeighted, vOut;
	VEC_SUB(vRange, Def.m_ViewMax, min);
	VEC_MUL(vWeighted, vRange, percents);
	VEC_ADD(vOut, min, vWeighted);
	g_ViewParams.m_SkyViewPos = vOut;
}

// Globals written by d3d_InitFrame (Jupiter common_draw.cpp has the first three; the others are renderer statistics and
// per-frame state of the draw code, names unknown: Ghidra names, roles in the guess comments).
// NAME: g_CurFrameCode, g_CurObjectFrameCode: Jupiter common_draw.cpp / names_proposal high
// GLOBAL: D3DREN 0x100577a0
uint16 g_CurFrameCode;
// GLOBAL: D3DREN 0x100561f0
uint32 g_CurObjectFrameCode;
// GLOBAL: D3DREN 0x10057b58
extern int DAT_10057b58;				// guess: frame counter (incremented per d3d_InitFrame, zeroed by d3d_Init)
// GLOBAL: D3DREN 0x1005625c
extern int DAT_1005625c;				// guess: the third argument of d3d_InitFrame
// GLOBAL: D3DREN 0x10055ce0
extern GlobalPanInfo *DAT_10055ce0;	// &g_pStruct->m_GlobalPans (NAMING.md: "DAT_10055ce0 = &it")
// GLOBAL: D3DREN 0x10056284
extern RenderContext *DAT_10056284;	// guess: the render context of the frame (CreateContext's object)
// GLOBAL: D3DREN 0x100577b8
extern uint16 DAT_100577b8;			// guess: current texture frame code (RenderStruct::IncCurTextureFrameCode)
// GLOBAL: D3DREN 0x100577a8
extern LTVector DAT_100577a8;			// guess: a vector the engine hands over (SceneDesc +0x38)
// GLOBAL: D3DREN 0x100566a0
extern LTVector DAT_100566a0;			// guess: SceneDesc +0x44
// GLOBAL: D3DREN 0x100561f8
extern LTVector DAT_100561f8;			// guess: the global light scale (SceneDesc +0x50, "GlobalLightScale")
// GLOBAL: D3DREN 0x10056208
extern LTVector DAT_10056208;			// guess: DAT_100561f8 * 255
// GLOBAL: D3DREN 0x100566c0
extern LTVector DAT_100566c0;			// DAT_100561f8 / 255 (original loads at 0x100104cb/0x100104d2/0x100104dd)
// GLOBAL: D3DREN 0x10056698
extern TLRGB DAT_10056698;				// guess: the light scale as a packed colour
// GLOBAL: D3DREN 0x10057774
extern uint8 DAT_10057774;				// guess: 0xff, set with the colours (an alpha byte)
// GLOBAL: D3DREN 0x10055ce8
extern LTVector DAT_10055ce8;			// guess: the global vertex tint (SceneDesc +0x5c)
// GLOBAL: D3DREN 0x100566bc
extern RGBColor DAT_100566bc;				// guess: the tint as a packed colour
// GLOBAL: D3DREN 0x10056260
extern LTVector DAT_10056260;			// guess: SceneDesc +0x68
// GLOBAL: D3DREN 0x10057798
extern TLRGB DAT_10057798;				// guess: that vector as a packed colour (not scaled)
// GLOBAL: D3DREN 0x10056694
extern float DAT_10056694;				// guess: area drawn this frame ("Tri area drawn")
// Per-frame counters of the draw code (zeroed here; printed by RenderScene).
// GLOBAL: D3DREN 0x10056688
extern int DAT_10056688;
// GLOBAL: D3DREN 0x100566ac
extern int DAT_100566ac;
// GLOBAL: D3DREN 0x10055cd8
extern int DAT_10055cd8;
// GLOBAL: D3DREN 0x1005779c
extern int DAT_1005779c;
// GLOBAL: D3DREN 0x100566cc
extern int DAT_100566cc;
// GLOBAL: D3DREN 0x10056690
extern int DAT_10056690;
// GLOBAL: D3DREN 0x100566b8
extern int DAT_100566b8;
// GLOBAL: D3DREN 0x10056270
extern int DAT_10056270;
// GLOBAL: D3DREN 0x10056218
extern uint32 DAT_10056218;	// unsigned: the exe compares it with jb/jbe
// GLOBAL: D3DREN 0x100566b0
extern int DAT_100566b0;
// GLOBAL: D3DREN 0x10055cf4
extern int DAT_10055cf4;
// GLOBAL: D3DREN 0x10056280
extern int DAT_10056280;
// GLOBAL: D3DREN 0x10057794
extern int DAT_10057794;
// GLOBAL: D3DREN 0x10056278
extern int DAT_10056278;
// GLOBAL: D3DREN 0x10055cdc
extern int DAT_10055cdc;
// DAT_1005626c, DAT_1005668c, g_ClipFlags, g_pClipScratchVerts: include/d3dren/viewparams.h

// NAME: d3d_InitFrustum: Jupiter common_draw.cpp, expanded in d3d_InitFrame in Talon.
inline LTBOOL d3d_InitFrustum(ViewParams *pParams,
    float xFov, float yFov, float nearZ, float farZ,
    float screenMinX, float screenMinY, float screenMaxX, float screenMaxY,
    const LTVector *pPos, const LTRotation *pRotation)
{
    LTMatrix mat;
    quat_ConvertToMatrix(pRotation->m_Quat, mat.m);
    mat.SetTranslation(*pPos);
    ViewBoxDef viewBox;
    d3d_InitViewBox(&viewBox, nearZ, farZ, xFov, yFov);
    return d3d_InitFrustum2(pParams, &viewBox,
        screenMinX, screenMinY, screenMaxX, screenMaxY,
        &mat, LTVector(1.0f, 1.0f, 1.0f));
}

// Component-wise SDK VEC_MULSCALAR formula, returned through the original-style vector temporary.
inline LTVector FUN_100103c2_Scale(LTVector v, float scale)
{
    LTVector ret;
    VEC_MULSCALAR(ret, v, scale);
    return ret;
}

// NAME: d3d_InitFrame: Jupiter common_draw.cpp d3d_InitFrame (names_proposal medium); the Talon form takes two more arguments (the
// scratch vertex buffer of the polygon clippers, stored in g_pClipScratchVerts, and an int stored in DAT_1005625c) and has Jupiter's
// d3d_InitFrustum inlined (it has no copy of its own in d3d.ren).
// STUB: D3DREN 0x100103c2
// Remaining difference: 939 instead of 949 bytes (486 bytes differ). The inline frustum call snapshots its FOV, near/far,
// and screen parameters before matrix construction, as in the original. The scaled vectors pass through local float
// temporaries before their DWORD copies. Some vector copies and x87 scheduling still differ.
LTBOOL d3d_InitFrame(SceneDesc *pDesc, TLVertex *pScratchVerts, int nUnk)
{
	RenderContext *pContext;
	short control;

	// d3d_SetFPState (Jupiter common_draw.h): lower the floating point precision to speed up multiplies and divides.
	_asm
	{
		fstcw	control
		and	control, 0xfcff
		fldcw	control
	}

	d3d_ReadConsoleVariables();

	DAT_10057b58++;
	g_pClipScratchVerts = pScratchVerts;
	g_pSceneDesc = pDesc;
	DAT_1005625c = nUnk;
	DAT_10055ce0 = g_pStruct->m_GlobalPans;

	// Get stuff out of the context (if it exists).
	pContext = (RenderContext *)pDesc->m_hRenderContext;
	if (pDesc->m_DrawMode == DRAWMODE_NORMAL)
	{
		if (!pContext)
			return 0;

		DAT_10056770 = pContext->m_pWorld;
		DAT_10056284 = pContext;
		d3d_IncrementFrameCode(pContext);
		g_CurFrameCode = pContext->m_CurFrameCode;
	}
	else
	{
		DAT_10056284 = 0;
		DAT_10056770 = 0;
	}

	g_CurObjectFrameCode = g_pStruct->IncObjectFrameCode();
	DAT_100577b8 = g_pStruct->IncCurTextureFrameCode();
	g_ClipFlags = 0x3f;

	DAT_100577a8 = pDesc->m_Unknown38;
	DAT_100566a0 = pDesc->m_Unknown44;

	DAT_100561f8 = pDesc->m_GlobalLightScale;
	DAT_10056208 = FUN_100103c2_Scale(DAT_100561f8, 255.0f);
	DAT_100566c0 = FUN_100103c2_Scale(DAT_100561f8, 1.0f / 255.0f);
	DAT_10056698.r = (uint8)RoundFloatToInt(DAT_100561f8.x * 255.0f);
	DAT_10056698.g = (uint8)RoundFloatToInt(DAT_100561f8.y * 255.0f);
	DAT_10056698.b = (uint8)RoundFloatToInt(DAT_100561f8.z * 255.0f);
	DAT_10056698.a = 0xff;
	DAT_10057774 = 0xff;

	DAT_10055ce8 = pDesc->m_GlobalVertexTint;
	DAT_100566bc.rgb.r = (uint8)RoundFloatToInt(DAT_10055ce8.x * 255.0f);
	DAT_100566bc.rgb.g = (uint8)RoundFloatToInt(DAT_10055ce8.y * 255.0f);
	DAT_100566bc.rgb.b = (uint8)RoundFloatToInt(DAT_10055ce8.z * 255.0f);
	DAT_100566bc.rgb.a = 0xff;

	DAT_10056260 = pDesc->m_GlobalModelDirAdd2;
	DAT_10057798.r = (uint8)RoundFloatToInt(DAT_10056260.x);
	DAT_10057798.g = (uint8)RoundFloatToInt(DAT_10056260.y);
	DAT_10057798.b = (uint8)RoundFloatToInt(DAT_10056260.z);
	DAT_10057798.a = 0xff;

	// Reset the statistics.
	DAT_10056694 = 0.0f;
	DAT_10056688 = 0;
	DAT_100566ac = 0;
	DAT_10055cd8 = 0;
	DAT_1005626c = 0;
	DAT_1005779c = 0;
	DAT_100566cc = 0;
	DAT_1005668c = 0;
	DAT_10056690 = 0;
	DAT_100566b8 = 0;
	DAT_10056270 = 0;
	DAT_10056218 = 0;
	DAT_100566b0 = 0;
	DAT_10055cf4 = 0;
	DAT_10056280 = 0;
	DAT_10057794 = 0;
	DAT_10056278 = 0;
	DAT_10055cdc = 0;

    return d3d_InitFrustum(&g_ViewParams,
        pDesc->m_xFov, pDesc->m_yFov, g_CV_NearZ.m_FloatVal, pDesc->m_FarZ,
        (float)pDesc->m_Rect.left, (float)pDesc->m_Rect.top,
        (float)pDesc->m_Rect.right - 0.1f, (float)pDesc->m_Rect.bottom - 0.1f,
        &pDesc->m_Pos, &pDesc->m_Rotation);
}

// ------------------------------------------------------------------ //
// The lists of polygons touched by dynamic lights (names unknown, shapes in the comments).
// ------------------------------------------------------------------ //

// The per-poly record of a dynamic light touching it (StructBank DAT_10056220, 0x14 bytes) and the list of lit polys
// (StructBank DAT_10056240, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;		// 0x00
	LTObject			*m_pLight;		// 0x04
	LTVector			m_Pos;			// 0x08 the light position in the world model space
};

struct UnkType_LitPoly
{
	WorldPoly			*m_pPoly;		// 0x00
	UnkType_LitPoly		*m_pNext;		// 0x04
};

#define WORLDPOLY_LIGHTS(p)	(*(UnkType_PolyLight**)((uint8*)(p) + 0x30))

// GLOBAL: D3DREN 0x10056214
int DAT_10056214;				// guess: number of polys in the lit list ("Num Lit Polies")
// GLOBAL: D3DREN 0x10056220
StructBank DAT_10056220;		// guess: UnkType_PolyLight records
// GLOBAL: D3DREN 0x10056240
StructBank DAT_10056240;		// guess: UnkType_LitPoly records
// GLOBAL: D3DREN 0x10057778
StructBank DAT_10057778;		// guess: 0xc byte records of the lit polygon drawing (see unit unk/10023860)
// GLOBAL: D3DREN 0x100577b4
UnkType_LitPoly *DAT_100577b4;	// guess: head of the list of polys touched by a dynamic light this frame

void FUN_100107c3(UnkType_LitPoly *pLitPoly);

// guess: ends the lit lists of the frame: returns every record and list node to its StructBank (called once per frame by 0x10014a40)
// FUNCTION: D3DREN 0x10010777
void FUN_10010777()
{
	UnkType_LitPoly *pNode;
	UnkType_LitPoly *pNext;

	DAT_10056214 = 0;
	for (pNode = DAT_100577b4; pNode; pNode = pNext)
	{
		DAT_10056214++;
		FUN_100107c3(pNode);
		pNext = pNode->m_pNext;
		sb_Free(&DAT_10056240, pNode);
	}
	DAT_100577b4 = 0;
}

// guess: returns the light records of one lit poly to their StructBank
// FUNCTION: D3DREN 0x100107c3
void FUN_100107c3(UnkType_LitPoly *pLitPoly)
{
	UnkType_PolyLight *pRecord;
	UnkType_PolyLight *pNext;

	for (pRecord = WORLDPOLY_LIGHTS(pLitPoly->m_pPoly); pRecord; pRecord = pNext)
	{
		pNext = pRecord->m_pNext;
		sb_Free(&DAT_10056220, pRecord);
	}
	WORLDPOLY_LIGHTS(pLitPoly->m_pPoly) = 0;
}

// guess: tail-call wrapper of FUN_10010777 (the frame code calls this one)
// FUNCTION: D3DREN 0x100107fb
void FUN_100107fb()
{
	FUN_10010777();
}

// guess: creates the StructBanks of the lit lists (and of the lit polygon drawing)
// FUNCTION: D3DREN 0x10010800
void FUN_10010800()
{
	sb_Init2(&DAT_10056220, 0x14, 0x40, 0x40);
	sb_Init2(&DAT_10056240, 8, 0x40, 0x40);
	sb_Init2(&DAT_10057778, 0xc, 0x200, 0x800);
}

// FUNCTION: D3DREN 0x1001083a
void FUN_1001083a()
{
	sb_Term(&DAT_10056220);
	sb_Term(&DAT_10056240);
	sb_Term(&DAT_10057778);
}

// guess: sphere-map texture coordinates (u, v) of the reflection of the view ray p1 -> p2 about the normal
// FUNCTION: D3DREN 0x1001085e
void FUN_1001085e(LTVector *pPos1, LTVector *pPos2, LTVector *pNormal, float *pU, float *pV)
{
	LTVector vDir = *pPos2 - *pPos1;
	float fDot;
	LTVector vRefl;

	fDot = vDir.Dot(*pNormal);
	fDot = fDot + fDot;
	vRefl = vDir - *pNormal * fDot;
	vRefl.Norm();

	if (pNormal->y * pNormal->y <= 0.54439f && pNormal->x * pNormal->x >= pNormal->z * pNormal->z)
		*pU = 0.5f - vRefl.z * 0.5f;
	else
		*pU = 0.5f - vRefl.x * 0.5f;
	*pV = 0.5f - vRefl.y * 0.5f;
}

// Stores a function address in a RenderStruct slot.  The slots are typed in include/renderstruct.h, but some of them are
// padding there (GetOptimized2DBlend/Color, IsInOptimized2D and the unnamed 0xc8) and several functions of the units that
// define them take Jupiter-style arguments, so the store goes through void *.
#define RS_SET(member, fn)	(*(void **)&pStruct->member = (void *)(fn))
#define RS_SET_PAD(offset, fn)	(*(void **)((uint8 *)pStruct + (offset)) = (void *)(fn))

// NAME: g_pStruct: Jupiter common_stuff.cpp `RenderStruct *g_pStruct`; GLOBAL annotation is in common_stuff.h
RenderStruct *g_pStruct;

#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'
