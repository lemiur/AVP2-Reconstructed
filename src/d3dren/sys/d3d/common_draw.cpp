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
#include "d3dren/common_draw.h"
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

// The five LTVector globals of this object come first: the SDK's empty inline constructor (`_CVector() {}`) leaves five empty (1-byte)
// static initialiser functions at 0x1000f3a0-0x1000f3a4.  Which five of the object's seven 12-byte vectors they are is not provable from
// the bytes (an empty initialiser names nothing); these are the five d3d_InitFrame copies whole out of the SceneDesc (see common_draw.h).
// FUNCTION: D3DREN 0x1000f3a0 _$E2
LTVector g_vSceneClientVectorPrimary;
// FUNCTION: D3DREN 0x1000f3a1 _$E5
LTVector g_vSceneClientVectorSecondary;
// FUNCTION: D3DREN 0x1000f3a2 _$E8
LTVector g_GlobalLightScale;
// FUNCTION: D3DREN 0x1000f3a3 _$E11
LTVector g_GlobalVertexTint;
// FUNCTION: D3DREN 0x1000f3a4 _$E14
LTVector g_vGlobalModelDirAdd2;

// The renderer's view state (the LTRect member's inline constructor zeroes m_Rect: the whole static initialiser).
// FUNCTION: D3DREN 0x1000f3a5 _$E17
ViewParams g_ViewParams;

// FUNCTION: D3DREN 0x1000f3af ??0ViewParams@@QAE@XZ

void d3d_ClearWorldBspFrameCodes(WorldBsp *pBsp);

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
		for (i = 0; i < g_pFrameMainWorld->m_WorldModels.GetSize(); i++)
		{
			d3d_ClearWorldBspFrameCodes(g_pFrameMainWorld->m_WorldModels[i]->m_pOriginalBsp);
		}
	}

	++pContext->m_CurFrameCode;
}

// FUNCTION: D3DREN 0x1000f40a
// guess: resets the u16 frame codes of all leaves (+0x2c of each 0x30 byte leaf) and all polies (+0x46) of a world BSP
void d3d_ClearWorldBspFrameCodes(WorldBsp *pBsp)
{
	uint32 i;

	for (i = 0; i < pBsp->m_nLeafs; i++)
	{
		*(uint16 *)((uint8 *)pBsp->m_Leafs + i * 0x30 + 0x2c) = 0;
	}

	for (i = 0; i < pBsp->m_nPolies; i++)
	{
		pBsp->m_Polies[i]->m_Unk46 = 0;
	}
}

// FUNCTION: D3DREN 0x1000f458
// guess: the portal visibility test of a vis query (VisQueryRequest::m_Unknown24 = VQPortalFn): returns 0 when the portal's
// box (m_Center +- m_Dims) lies completely outside one of the six world-space clip planes of g_ViewParams
LTBOOL d3d_IsPortalInsideViewFrustum(BspPortal *pPortal)
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
void d3d_CacheProjectionCoefficients(ViewParams *pParams)
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
// A 3000-candidate permuter run found nothing better than 7.
// PARKED: complete body; 7-instruction schedule residue (operator* result copies interleaved with the next MatMul's argument setup)
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

	d3d_CacheProjectionCoefficients(pParams);

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
// FUNCTION: D3DREN 0x100102b9
// The sky position is Jupiter's `ViewMin + (ViewMax - ViewMin) * percents` as one expression of SDK operators.  The percents are
// one expression on the world extents read through g_pFrameMainWorld (Jupiter's min/max reference locals fold &m_ExtentsMax into
// the displacements instead of the exe's `add ecx, 0x150`).
void d3d_SetupSkyStuff()
{
	LTVector percents;

	if (g_pSceneDesc->m_DrawMode == DRAWMODE_NORMAL)
	{
		percents = (g_ViewParams.m_Pos - g_pFrameMainWorld->m_ExtentsMin) / (g_pFrameMainWorld->m_ExtentsMax - g_pFrameMainWorld->m_ExtentsMin);
	}
	else
	{
		percents.Init(0.5f, 0.5f, 0.5f);
	}

	const SkyDef &Def = g_pSceneDesc->m_SkyDef;
	g_ViewParams.m_SkyViewPos = Def.m_ViewMin + (Def.m_ViewMax - Def.m_ViewMin) * percents;
}

// Globals written by d3d_InitFrame (Jupiter common_draw.cpp has the first three; the others are renderer statistics and
// per-frame state of the draw code, names unknown: Ghidra names, roles in the guess comments).
// NAME: g_CurFrameCode, g_CurObjectFrameCode: Jupiter common_draw.cpp / names_proposal high.  Declarations, GLOBAL annotations and
// roles: d3dren/common_draw.h (g_vSceneClientVector*, g_GlobalLightScale, g_GlobalVertexTint and g_vGlobalModelDirAdd2 are defined at the top).
uint16 g_CurFrameCode;
uint32 g_CurObjectFrameCode;
DynamicLight *g_ObjectDynamicLights[MAX_VISIBLE_LIGHTS];
uint32 g_nNumObjectDynamicLights;
MainWorld *g_pFrameMainWorld;
int g_nInitFrameArgument;
GlobalPanInfo *g_pGlobalPanInfo;
RenderContext *g_pFrameRenderContext;
uint16 g_CurTextureFrameCode;
TLRGB g_GlobalLightScaleColor;
uint8 g_nPolyVertexAlpha;
RGBColor g_GlobalVertexTintColor;
TLRGB g_GlobalModelDirAdd2Color;
float g_fScreenTriangleArea;
int g_nWorldPolysProcessed;
int g_nWorldPolysDrawn;
int g_nParticlesDrawn;
int g_nReservedFrameStatistic;
int g_nLightTests;
int g_nRejectedPolyLightTests;
int g_nSkyPortals;
int g_nSkyPolyFragments;
int g_nPolygonTriangles;
int g_nVisibleLeaves;
int g_nTextureUploads;
int g_nTextureChanges;
int g_nDynamicLightmapsRefreshed;
int g_nTextureUploadSaves;
// g_nModelTrianglesDrawn, g_nPlaneClipTests, g_ClipFlags, g_pClipScratchVerts: include/d3dren/viewparams.h

// NAME: d3d_SetFPState: Jupiter common_draw.h (same body).
inline void d3d_SetFPState()
{
	short control;

	_asm
	{
		fstcw	control		// Get FPU control word
		and	control, 0xfcff	// PC field = 00 for single precision
		fldcw	control
	}
}

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

// NAME: d3d_InitFrame: Jupiter common_draw.cpp d3d_InitFrame (names_proposal medium); the Talon form takes two more arguments (the
// scratch vertex buffer of the polygon clippers, stored in g_pClipScratchVerts, and an int stored in g_nInitFrameArgument) and has Jupiter's
// d3d_InitFrustum inlined (it has no copy of its own in d3d.ren).
// `/ 255.0f` divides a vector constructed from g_GlobalLightScale's members (the exe copies it through the FPU first).  The argument
// stores are in this order (the scheduler emits them in the exe's order either way); it decides which dead argument home
// the RoundFloatToInt results and the frustum's farZ/xFov get.
// FUNCTION: D3DREN 0x100103c2
LTBOOL d3d_InitFrame(SceneDesc *pDesc, TLVertex *pScratchVerts, int nUnk)
{
	RenderContext *pContext;

	// Lower the floating point precision to speed up multiplies and divides.
	d3d_SetFPState();

	// Possibly read in the new options.
	d3d_ReadConsoleVariables();

	g_nRenderFrameCount++;
	g_pClipScratchVerts = pScratchVerts;
	g_nInitFrameArgument = nUnk;
	g_pSceneDesc = pDesc;
	g_pGlobalPanInfo = g_pStruct->m_GlobalPans;

	// Get stuff out of the context (if it exists).
	pContext = (RenderContext *)pDesc->m_hRenderContext;
	if (pDesc->m_DrawMode == DRAWMODE_NORMAL)
	{
		if (!pContext)
			return 0;

		g_pFrameMainWorld = pContext->m_pWorld;
		g_pFrameRenderContext = pContext;
		d3d_IncrementFrameCode(pContext);
		g_CurFrameCode = pContext->m_CurFrameCode;
	}
	else
	{
		g_pFrameRenderContext = 0;
		g_pFrameMainWorld = 0;
	}

	g_CurObjectFrameCode = g_pStruct->IncObjectFrameCode();
	g_CurTextureFrameCode = g_pStruct->IncCurTextureFrameCode();
	g_ClipFlags = 0x3f;

	g_vSceneClientVectorPrimary = pDesc->m_Unknown38;
	g_vSceneClientVectorSecondary = pDesc->m_Unknown44;

	g_GlobalLightScale = pDesc->m_GlobalLightScale;
	g_GlobalLightScale255 = g_GlobalLightScale * 255.0f;
	g_vGlobalLightScalePerByte = LTVector(g_GlobalLightScale.x, g_GlobalLightScale.y, g_GlobalLightScale.z) / 255.0f;
	g_GlobalLightScaleColor.r = (uint8)RoundFloatToInt(g_GlobalLightScale.x * 255.0f);
	g_GlobalLightScaleColor.g = (uint8)RoundFloatToInt(g_GlobalLightScale.y * 255.0f);
	g_GlobalLightScaleColor.b = (uint8)RoundFloatToInt(g_GlobalLightScale.z * 255.0f);
	g_GlobalLightScaleColor.a = 0xff;
	g_nPolyVertexAlpha = 0xff;

	g_GlobalVertexTint = pDesc->m_GlobalVertexTint;
	g_GlobalVertexTintColor.rgb.r = (uint8)RoundFloatToInt(g_GlobalVertexTint.x * 255.0f);
	g_GlobalVertexTintColor.rgb.g = (uint8)RoundFloatToInt(g_GlobalVertexTint.y * 255.0f);
	g_GlobalVertexTintColor.rgb.b = (uint8)RoundFloatToInt(g_GlobalVertexTint.z * 255.0f);
	g_GlobalVertexTintColor.rgb.a = 0xff;

	g_vGlobalModelDirAdd2 = pDesc->m_GlobalModelDirAdd2;
	g_GlobalModelDirAdd2Color.r = (uint8)RoundFloatToInt(g_vGlobalModelDirAdd2.x);
	g_GlobalModelDirAdd2Color.g = (uint8)RoundFloatToInt(g_vGlobalModelDirAdd2.y);
	g_GlobalModelDirAdd2Color.b = (uint8)RoundFloatToInt(g_vGlobalModelDirAdd2.z);
	g_GlobalModelDirAdd2Color.a = 0xff;

	// Reset the statistics.
	g_fScreenTriangleArea = 0.0f;
	g_nWorldPolysProcessed = 0;
	g_nWorldPolysDrawn = 0;
	g_nParticlesDrawn = 0;
	g_nModelTrianglesDrawn = 0;
	g_nReservedFrameStatistic = 0;
	g_nLightTests = 0;
	g_nPlaneClipTests = 0;
	g_nRejectedPolyLightTests = 0;
	g_nSkyPortals = 0;
	g_nSkyPolyFragments = 0;
	g_nNumObjectDynamicLights = 0;
	g_nPolygonTriangles = 0;
	g_nVisibleLeaves = 0;
	g_nTextureUploads = 0;
	g_nTextureChanges = 0;
	g_nDynamicLightmapsRefreshed = 0;
	g_nTextureUploadSaves = 0;

    return d3d_InitFrustum(&g_ViewParams,
        pDesc->m_xFov, pDesc->m_yFov, g_CV_NearZ.m_FloatVal, pDesc->m_FarZ,
        (float)pDesc->m_Rect.left, (float)pDesc->m_Rect.top,
        (float)pDesc->m_Rect.right - 0.1f, (float)pDesc->m_Rect.bottom - 0.1f,
        &pDesc->m_Pos, &pDesc->m_Rotation);
}

// ------------------------------------------------------------------ //
// The lists of polygons touched by dynamic lights (names unknown, shapes in the comments).
// ------------------------------------------------------------------ //

// The per-poly record of a dynamic light touching it (StructBank g_PolyLightBank, 0x14 bytes) and the list of lit polys
// (StructBank g_LitPolyBank, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h).
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
int g_nLitPolies;				// guess: number of polys in the lit list ("Num Lit Polies")
// GLOBAL: D3DREN 0x10056220
StructBank g_PolyLightBank;		// guess: UnkType_PolyLight records
// GLOBAL: D3DREN 0x10056240
StructBank g_LitPolyBank;		// guess: UnkType_LitPoly records
// GLOBAL: D3DREN 0x10057778
StructBank g_PolyDrawRecordBank;		// guess: 0xc byte records of the lit polygon drawing (see unit unk/10023860)
// GLOBAL: D3DREN 0x100577b4
UnkType_LitPoly *g_pDynamicallyLitPolys;	// guess: head of the list of polys touched by a dynamic light this frame

void d3d_FreePolyLightRecords(UnkType_LitPoly *pLitPoly);

// guess: ends the lit lists of the frame: returns every record and list node to its StructBank (called once per frame by 0x10014a40)
// FUNCTION: D3DREN 0x10010777
void d3d_FreeLitPolyList()
{
	UnkType_LitPoly *pNode;
	UnkType_LitPoly *pNext;

	g_nLitPolies = 0;
	for (pNode = g_pDynamicallyLitPolys; pNode; pNode = pNext)
	{
		g_nLitPolies++;
		d3d_FreePolyLightRecords(pNode);
		pNext = pNode->m_pNext;
		sb_Free(&g_LitPolyBank, pNode);
	}
	g_pDynamicallyLitPolys = 0;
}

// guess: returns the light records of one lit poly to their StructBank
// FUNCTION: D3DREN 0x100107c3
void d3d_FreePolyLightRecords(UnkType_LitPoly *pLitPoly)
{
	UnkType_PolyLight *pRecord;
	UnkType_PolyLight *pNext;

	for (pRecord = WORLDPOLY_LIGHTS(pLitPoly->m_pPoly); pRecord; pRecord = pNext)
	{
		pNext = pRecord->m_pNext;
		sb_Free(&g_PolyLightBank, pRecord);
	}
	WORLDPOLY_LIGHTS(pLitPoly->m_pPoly) = 0;
}

// guess: tail-call wrapper of d3d_FreeLitPolyList (the frame code calls this one)
// FUNCTION: D3DREN 0x100107fb
void d3d_ClearFrameLitPolys()
{
	d3d_FreeLitPolyList();
}

// guess: creates the StructBanks of the lit lists (and of the lit polygon drawing)
// FUNCTION: D3DREN 0x10010800
void d3d_InitLitPolyPools()
{
	sb_Init2(&g_PolyLightBank, 0x14, 0x40, 0x40);
	sb_Init2(&g_LitPolyBank, 8, 0x40, 0x40);
	sb_Init2(&g_PolyDrawRecordBank, 0xc, 0x200, 0x800);
}

// FUNCTION: D3DREN 0x1001083a
void d3d_TermLitPolyPools()
{
	sb_Term(&g_PolyLightBank);
	sb_Term(&g_LitPolyBank);
	sb_Term(&g_PolyDrawRecordBank);
}

// guess: sphere-map texture coordinates (u, v) of the reflection of the view ray p1 -> p2 about the normal
// FUNCTION: D3DREN 0x1001085e
void d3d_CalcWorldReflectionUVs(LTVector *pPos1, LTVector *pPos2, LTVector *pNormal, float *pU, float *pV)
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


#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'
