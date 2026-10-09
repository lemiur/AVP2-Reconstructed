// d3d.ren unk/10001000 (0x10001000-0x100062e0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// model vertex-buffer pools + ConVars + vertex fillers + TL/0x28 polygon clip dispatchers (0x10001530, 0x10001b30) + ModelDraw
// piece callbacks; ??_H at 0x10001000 is the first copy linked. names_proposal labels it drawmodel (low), but all four Jupiter
// drawmodel.cpp functions are in the P object at 0x100241e0.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/10001000 (0x10001000-0x10007930): model vertex buffer pool globals and ConVars, the model piece drawer
// (vertex fillers, polygon clippers, the five per-piece draw callbacks, the skinning/lighting transform) and the plane
// clippers.  Speed object (16-byte aligned COMDATs): /O2; /Ob2 inlines the static initialiser bodies into their wrappers.
// FLAGS: /O2 /Ob2
#include <string.h>
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/vbpool.h"
#include "d3dren/vertfill.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/modeldraw.h"
#include "de_world.h"	// SharedTexture
#include "d3dren/tlvertex.h"
#include "d3dren/fixedpoint.h"
// 0x10001000 is the linker's one `vector constructor iterator` (??_H, 48 bytes, byte-identical to lithtech.exe 0x00401000): the compiler emits it,
// unreferenced, in every object that sees a class with an IN-CLASS inline constructor and an array member of a class with a constructor
// (ModelDraw: modeldraw.h defines the constructor in the class body, UnkType_ModelLight m_Unk3c[16]).  This is the first object linked, so it
// holds the surviving copy; no statement of this file produces it.
// FUNCTION: D3DREN 0x10001000 ??_H@YGXPAXIHP6EX0@Z@Z

// d3d.ren console variables whose constructor is called out of line (FUN_100111b5 lives in common_stuff).
// The static initialisers of this object are inlined into their wrappers, so each wrapper is the whole function.
// FUNCTION: D3DREN 0x10001030 _$E2
// GLOBAL: D3DREN 0x1004d5e0
ConVar g_CV_ModelMinTri("ModelMinTri", 0.2f);
// The four vertex buffer pools: static initialisers with the derived constructor inlined (base constructor out of
// line) and a destructor that tail-calls the base destructor.
// FUNCTION: D3DREN 0x10001050 _$E7
// FUNCTION: D3DREN 0x10001080 _$E5
// GLOBAL: D3DREN 0x1004d620
UnkType_VertexBufferPoolA<0> g_ModelTransformedVertexPoolTex1(0);
// FUNCTION: D3DREN 0x10001090 _$E12
// FUNCTION: D3DREN 0x100010c0 _$E10
// GLOBAL: D3DREN 0x1004da80
UnkType_VertexBufferPoolB<0> g_ModelTransformedVertexPoolTex2(0);
// FUNCTION: D3DREN 0x100010d0 _$E17
// FUNCTION: D3DREN 0x10001100 _$E15
// GLOBAL: D3DREN 0x1004eb48
UnkType_VertexBufferPoolA<1> g_ModelUntransformedVertexPoolTex1(1);
// FUNCTION: D3DREN 0x10001110 _$E22
// FUNCTION: D3DREN 0x10001140 _$E20
// GLOBAL: D3DREN 0x1004dae0
UnkType_VertexBufferPoolB<1> g_ModelUntransformedVertexPoolTex2(1);

// FUNCTION: D3DREN 0x10001150 _$E25
// GLOBAL: D3DREN 0x1004d5c0
ConVar g_CV_ModelDetailTextureScale("ModelDetailTextureScale", 3.0f);
// FUNCTION: D3DREN 0x10001170 _$E28
// GLOBAL: D3DREN 0x1004eb20
ConVar g_CV_SpecularPowerTest("SpecularPowerTest", 0.0f);
// FUNCTION: D3DREN 0x10001190 _$E31
// GLOBAL: D3DREN 0x1004dac0
ConVar g_CV_SpecularScaleTest("SpecularScaleTest", 0.0f);
// FUNCTION: D3DREN 0x100011b0 _$E34
// GLOBAL: D3DREN 0x1004d600
ConVar g_CV_ModelVBSize("ModelVBSize", 126.0f);
// FUNCTION: D3DREN 0x100011d0 _$E37
// GLOBAL: D3DREN 0x1004eb88
ConVar g_CV_ModelVBCount("ModelVBCount", 1.0f);
// FUNCTION: D3DREN 0x100011f0 _$E40
// GLOBAL: D3DREN 0x1004d660
ConVar g_CV_ModelVBCache("ModelVBCache", 128.0f);
// FUNCTION: D3DREN 0x10001210 _$E43
// GLOBAL: D3DREN 0x1004d5a0
ConVar g_CV_ModelVBCacheDelay("ModelVBCacheDelay", 4.0f);

// The model vertex buffer cache global (defined in unk/1003a680.cpp as a static member: its declaration costs one `_$E` number in
// every unit that includes the header, so this include comes after this unit's own static initialisers).
#include "d3dren/vbcache.h"

float g_ModelTextureUOffset;
float g_ModelTextureVOffset;

// The exe evaluates the arguments of this right to left and keeps both on the x87 stack (FillModelBaseTexCoords, CopyModelGeneratedTexCoords).
static inline void SetUV(TLVertex *pVert, float u, float v)
{
	pVert->tu = u;
	pVert->tv = v;
}

// guess: (re)creates the four model vertex buffer pools and the model cache from the ModelVB* console variables: the size
// is rounded up to a multiple of 3 (whole triangles), the count is at least 1.
// FUNCTION: D3DREN 0x10001230
void d3d_InitModelVertexBufferPools(void)
{
	if (g_CV_ModelVBSize.m_IntVal <= 1)
		g_CV_ModelVBSize.m_IntVal = 1;
	if (g_CV_ModelVBCount.m_IntVal <= 1)
		g_CV_ModelVBCount.m_IntVal = 1;
	g_CV_ModelVBSize.m_IntVal = ((g_CV_ModelVBSize.m_IntVal + 2) / 3) * 3;
	g_ModelTransformedVertexPoolTex1.Init(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 0, g_TnLRast);
	g_ModelTransformedVertexPoolTex2.Init(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 0, g_TnLRast);
	g_ModelUntransformedVertexPoolTex1.Init(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 1, g_TnLRast);
	g_ModelUntransformedVertexPoolTex2.Init(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 1, g_TnLRast);
	UnkType_VertexBufferPool *pCache = &UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache;
	pCache->Init(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCache.m_IntVal, 1, g_TnLRast);
	UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache.m_Unk60 = g_CV_ModelVBCacheDelay.m_IntVal;
}

// guess: releases the two non-cache model pools and the cache (Term, slot 3).
// FUNCTION: D3DREN 0x10001340
void d3d_TermModelVertexBufferPools(void)
{
	g_ModelTransformedVertexPoolTex1.Term();
	g_ModelTransformedVertexPoolTex2.Term();
	UnkType_VertexBufferPool *pCache = &UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache;
	pCache->Term();
}

// FUNCTION: D3DREN 0x10001370
void __fastcall FillModelBaseTexCoords(TLVertex *pDest, void *pSrc, float *pUV)
{
	SetUV(pDest, g_ModelTextureUOffset + pUV[0], g_ModelTextureVOffset + pUV[1]);
}

// FUNCTION: D3DREN 0x10001390
void __fastcall FillModelDetailTexCoords(UnkType_TLVertex40 *pDest, void *pSrc, float *pUV)
{
	pDest->tu = g_ModelTextureUOffset + pUV[0];
	pDest->tv = g_ModelTextureVOffset + pUV[1];
	pDest->tu2 = g_CV_ModelDetailTextureScale.m_FloatVal * pDest->tu;
	pDest->tv2 = g_CV_ModelDetailTextureScale.m_FloatVal * pDest->tv;
}

// FUNCTION: D3DREN 0x100013d0
void __fastcall CopyModelGeneratedTexCoords(TLVertex *pDest, TLVertex *pSrc, float *pUV)
{
	SetUV(pDest, pSrc->tu, pSrc->tv);
}

// FUNCTION: D3DREN 0x100013e0
void __fastcall FillModelBaseAndGeneratedTexCoords(UnkType_TLVertex40 *pDest, TLVertex *pSrc, float *pUV)
{
	pDest->tu = g_ModelTextureUOffset + pUV[0];
	pDest->tv = g_ModelTextureVOffset + pUV[1];
	pDest->tu2 = pSrc->tu;
	pDest->tv2 = pSrc->tv;
}

// FUNCTION: D3DREN 0x10001410
void __fastcall GenerateModelTexCoordsNoOp(void *pDest, void *pSrc, float *pUV)
{
}

// guess: environment map coordinates from the vertex normal: u/v = (normal . row of the matrix at +0x590) * scale + offset
// (m_EnvMapTransform is an LTMatrix: rows 0 and 1 give u and v).
// Accumulate v before u, one term per statement. The last u term needs the named normal-x load to preserve
// the target's `fld nx; fmul m590` operand order rather than loading the matrix element first.
// FUNCTION: D3DREN 0x10001420
void __fastcall GenerateModelEnvMapCoords(UnkType_ModelDrawerVertexView *pThis, UnkType_ModelVertex *pSrc, TLVertex *pDest)
{
	UnkType_ModelVertex *pVert = pSrc;
	float v = pThis->m_Unk5a4 * pVert->m_Unk18;
	v += pThis->m_Unk5a0 * pVert->m_Unk14;
	v += pThis->m_Unk5a8 * pVert->m_Unk1c;
	float u = pThis->m_Unk594 * pVert->m_Unk18;
	u += pThis->m_Unk598 * pVert->m_Unk1c;
	float x = pVert->m_Unk14;
	u += x * pThis->m_EnvMapTransform;
	SetUV(pDest, u * pThis->m_Unk624 + pThis->m_Unk61c, v * pThis->m_Unk628 + pThis->m_Unk620);
}

// guess: light/specular dot product coordinates from the vertex normal
// FUNCTION: D3DREN 0x10001490
void __fastcall GenerateModelSpecularCoords(UnkType_ModelDrawerVertexView *pThis, UnkType_ModelVertex *pSrc, TLVertex *pDest)
{
	UnkType_Vec3 vDir = pThis->m_Unk874;
	float fDot = vDir.x * pSrc->m_Unk14 + vDir.y * pSrc->m_Unk18 + vDir.z * pSrc->m_Unk1c;

	if (fDot > 0.0f)
		pDest->tu = pDest->tv = fDot * pThis->m_Unk630;
	else
		pDest->tu = pDest->tv = 0.0f;
}

// FUNCTION: D3DREN 0x10001510
void __fastcall CopyTLVertex32(TLVertex *pDest, TLVertex *pSrc)
{
	*pDest = *pSrc;
}

// FUNCTION: D3DREN 0x10001520
void __fastcall CopyTLVertex40(UnkType_TLVertex40 *pDest, UnkType_TLVertex40 *pSrc)
{
	*pDest = *pSrc;
}


#include "d3dren/clippoly.h"

int __fastcall ClipModelPolygon40(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts);

// Plane clippers for the remaining planes (flag bits 8, 0x10, 0x20, 2); the first argument is unused by them.
int ClipPolyTop(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyRight(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyBottom(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyFar(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);

int ClipPolyTop40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int ClipPolyRight40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int ClipPolyBottom40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int ClipPolyFar40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);

// guess: clips the polygon *ppVerts (*pnVerts vertices) against the planes selected by flags (1 near, 4 left, 8, 0x10,
// 0x20, 2); returns 0 when nothing is left.  The result replaces *ppVerts/*pnVerts (in the scratch buffer when clipped).
// One plane clipper per flag, as the twins in unk/100098d0 and unk/10007930: the near and left ones (clippoly.h, inline) are expanded
// here, which is what sends their `return 0` to the common exit; ClipPolyTop/Right/Bottom/Far are called out of line.
// The intersection helpers are called out of line: they are extern, so /Ob2 auto-inlines them only up to the 174u cap, and the
// LTVector & parameters indexed through LTVector::operator[] (the p[i] spelling of their bodies) weigh 197u/226u on the front end
// (float * indexing: 137u/151u, inlined).
// Not matching: register allocation (a zero register in ebx, pVerts in esi where the exe has edx) and the flags spill slot.
// STUB: D3DREN 0x10001530
int __fastcall ClipModelPolygon32(uint32 flags, TLVertex **ppVerts, int *pnVerts)
{
	if (g_CV_UseD3DClip.m_IntVal)	// guess: when set, only the near plane is clipped (flag bit 1)
	{
		flags &= 1;
		if (!flags)
			return 1;
	}

	TLVertex *pOut = g_pClipScratchVerts;
	TLVertex *pVerts = *ppVerts;
	int nVerts = *pnVerts;
	char bUnused0, bUnused1, bUnused2, bUnused3, bUnused4, bUnused5;

	if ((flags & 1) && !ClipPolyNear(&bUnused0, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 4) && !ClipPolyLeft(&bUnused1, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 8) && !ClipPolyTop(&bUnused2, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x10) && !ClipPolyRight(&bUnused3, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x20) && !ClipPolyBottom(&bUnused4, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 2) && !ClipPolyFar(&bUnused5, &pVerts, &nVerts, &pOut))
		return 0;

	*ppVerts = pVerts;
	*pnVerts = nVerts;
	return 1;
}

// guess: the 0x28-byte vertex twin of ClipModelPolygon32 (same shape, see there): ClipExtra TLVertex40_ClipExtra, planes ClipPolyTop40/10007100/100073b0/10007670.
// STUB: D3DREN 0x10001b30
int __fastcall ClipModelPolygon40(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts)
{
	if (g_CV_UseD3DClip.m_IntVal)	// guess: when set, only the near plane is clipped (flag bit 1)
	{
		flags &= 1;
		if (!flags)
			return 1;
	}

	UnkType_TLVertex40 *pOut = (UnkType_TLVertex40 *)g_pClipScratchVerts;
	UnkType_TLVertex40 *pVerts = *ppVerts;
	int nVerts = *pnVerts;
	char bUnused0, bUnused1, bUnused2, bUnused3, bUnused4, bUnused5;

	if ((flags & 1) && !ClipPolyNear40(&bUnused0, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 4) && !ClipPolyLeft40(&bUnused1, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 8) && !ClipPolyTop40(&bUnused2, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x10) && !ClipPolyRight40(&bUnused3, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x20) && !ClipPolyBottom40(&bUnused4, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 2) && !ClipPolyFar40(&bUnused5, &pVerts, &nVerts, &pOut))
		return 0;

	*ppVerts = pVerts;
	*pnVerts = nVerts;
	return 1;
}

// guess: Jupiter's TLVertex::ClipExtra (polyclip.h / clipline.h): interpolate everything but the position.
// FUNCTION: D3DREN 0x10001940
void TLVertex_ClipExtra(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}

// Same epsilon as Jupiter 3d_ops.h CLIP_EPSILON (0.00001f): the two constants at 0x100461c4 / 0x100461c8 are +-1e-5.
#define CLIP_EPSILON	0.00001f

// guess: intersection with the near plane z == g_ViewParams.m_NearZ; returns t, the parameter along p1->p2.  The coordinates are read
// through LTVector::operator[]: its inline calls are what puts both intersection helpers over the extern auto-inline cap.
// FUNCTION: D3DREN 0x10001a50
float IntersectNearClipPlane(LTVector &p1, LTVector &p2, LTVector &pOut)
{
	float t;
	float dz = p2[2] - p1[2];
	if (dz < -CLIP_EPSILON || dz > CLIP_EPSILON)
		t = (g_ViewParams.m_NearZ - p1[2]) / dz;
	else
		t = 0.0f;
	pOut[0] = (p2[0] - p1[0]) * t + p1[0];
	pOut[1] = (p2[1] - p1[1]) * t + p1[1];
	pOut[2] = g_ViewParams.m_NearZ;
	return t;
}

// guess: intersection with the plane x + z == 0 (left frustum plane); returns t.  The z of the second vertex goes through a named
// float first (as in the bottom plane clipper ClipPolyBottom): that is what puts p1[0] before p1[2] in the numerator.
// FUNCTION: D3DREN 0x10001ac0
float IntersectLeftClipPlane(LTVector &p1, LTVector &p2, LTVector &pOut)
{
	float fCz = p2[2];
	float d = ((p2[0] - p1[0]) + fCz) - p1[2];
	float t = (d < -CLIP_EPSILON || d > CLIP_EPSILON) ? -((p1[0] + p1[2]) / d) : 0.0f;
	pOut[1] = (p2[1] - p1[1]) * t + p1[1];
	float z = (p2[2] - p1[2]) * t + p1[2];
	pOut[2] = z;
	pOut[0] = -z;
	return t;
}

// FUNCTION: D3DREN 0x10001f20
void TLVertex40_ClipExtra(UnkType_TLVertex40 *pPrev, UnkType_TLVertex40 *pCur, UnkType_TLVertex40 *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->tu2 = (pCur->tu2 - pPrev->tu2) * t + pPrev->tu2;
	pOut->tv2 = (pCur->tv2 - pPrev->tv2) * t + pPrev->tv2;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}

// ---- model draw callbacks (merged from the W1 scratch unit) ----
// ---- the clipping callbacks (DrawPieceClipped and its "really close" twin DrawPieceClippedReallyClose) -----------------------------------------

// guess: the position-only projection of ProjectPositionWithDepthBias without the bias: the result goes through a vector, so
// pDest may be pSrc (the clipped polygon is projected in place).
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence):
static inline void ProjectPosition(TLVertex *pDest, TLVertex *pSrc)
{
	LTVector result;
	float w = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.x = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * w;
	result.y = (g_ViewParams.m_DeviceTimesProjection.m[1][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[1][3]) * w;
	result.z = (g_ViewParams.m_DeviceTimesProjection.m[2][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[2][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[2][3]) * w;
	pDest->rhw = w;
	pDest->m_Vec = result;
}
// guess: ProjectPositionWithDepthBias (unit unk/100062e0) written into the caller: the exe expands the same code at the three
// vertices of an unclipped triangle of the really close draw and calls the function out of line only for the clipped polygon.
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence):
static inline void ProjectVertexWithDepthBias(TLVertex *pDest, TLVertex *pSrc, float fZBias)
{
	LTVector result;
	float w = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.x = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * w;
	result.y = (g_ViewParams.m_DeviceTimesProjection.m[1][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[1][3]) * w;
	float z = fZBias + pSrc->m_Vec.z;
	float w2 = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][2] * z + g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.z = (g_ViewParams.m_DeviceTimesProjection.m[2][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[2][2] * z + g_ViewParams.m_DeviceTimesProjection.m[2][3]) * w2;
	pDest->rhw = w2;
	pDest->m_Vec = result;
}

// The address of the last vertex that still fits into the current buffer of the pool: the end mark of the fill loops.
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence).
static inline char *PoolLastVertex(UnkType_VertexBufferPool *p)
{
	uint32 nLast = p->m_Unk20 - p->m_Unk1c - 1;
	char *pBase = (char *)p->Lock();
	return pBase + p->GetVertexSize() * nLast;
}

// Draws what the fill loop put into the pool since the last Lock (one triangle list) and counts the triangles.
// helper written for this decompilation (the exe has it inlined at every flush; the name is mine).
static inline void FlushModelPool(ModelDraw *pDraw, TLVertex *pOut)
{
	UnkType_VertexBufferPool *p = pDraw->m_Unk608;
	uint32 nBytes = (char *)pOut - (char *)p->Lock();
	uint32 nVerts = nBytes / p->GetVertexSize();
	g_nModelTrianglesDrawn += nVerts / 3;
	((UnkType_VBPoolDrawView *)pDraw->m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nVerts);
}

// Draws the whole current buffer of the pool (it is full) and counts the triangles.
// helper written for this decompilation (the exe has it inlined at every restart; the name is mine).
static inline void DrawFullModelPool(ModelDraw *pDraw)
{
	g_nModelTrianglesDrawn += (pDraw->m_Unk608->m_Unk20 - pDraw->m_Unk608->m_Unk1c) / 3;
	((UnkType_VBPoolDrawView *)pDraw->m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, pDraw->m_Unk608->m_Unk20 - pDraw->m_Unk608->m_Unk1c);
}

// guess: back face test of a camera-space triangle after the perspective divide (x/z, y/z); m_bCullFlip mirrors the winding.
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence):
static inline LTBOOL IsFrontFacing(TLVertex *pV0, TLVertex *pV1, TLVertex *pV2)
{
	LTVector vInvZ, p0, d1, d2;
	vInvZ.x = 1.0f / pV0->m_Vec.z;
	vInvZ.y = 1.0f / pV1->m_Vec.z;
	vInvZ.z = 1.0f / pV2->m_Vec.z;
	p0.x = vInvZ.x * pV0->m_Vec.x;
	p0.y = vInvZ.x * pV0->m_Vec.y;
	d1.x = vInvZ.y * pV1->m_Vec.x - p0.x;
	d1.y = vInvZ.y * pV1->m_Vec.y - p0.y;
	d2.x = vInvZ.z * pV2->m_Vec.x - p0.x;
	d2.y = vInvZ.z * pV2->m_Vec.y - p0.y;
	float fCross = d2.x * d1.y - d2.y * d1.x;
	if (g_ViewParams.m_bCullFlip)
		fCross = -fCross;
	return fCross > 0.0f;
}

void ProjectPositionWithDepthBias(float *pDest, float *pSrc, float fZBias);

// guess: the "really close" variant (instances with FLAG_REALLYCLOSE): the z row uses z + g_CV_NearZ (ProjectPositionWithDepthBias).
// Not matching (same size, frame and slots): the load order of the three vertex indices (the exe loads 0, 1, 2 into edx/ebx/ebp; the
// declaration order 0, 2, 1 gives the registers but loads 0, 2, 1) and the x87 term order of the expanded projections (x/y swapped).
// Open: the exe expands ProjectPositionWithDepthBias at the unclipped vertices but calls it for the clipped polygon; an inline
// definition visible here inlines all four sites (no budget reaches the fourth), so the expanded copies are a separate helper here.
// Term orders of the helper's x/y rows (6 permutations) and the SDK w-row order move it by at most 4; same residue as DrawPieceClipped.
// PARKED: register/x87 order residue (vertex load order vs ebx/ebp choice, x/y term order of the depth-bias projection), as DrawPieceClipped
// STUB: D3DREN 0x10002050
int ModelDraw::DrawPieceClippedReallyClose(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->Lock();
	char *pEnd = PoolLastVertex(m_Unk608);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	char aBuf[0x500];
	while (nTris)
	{
		TLVertex *pV0 = &pVerts[pTri->m_Indices[0]];
		TLVertex *pV2 = &pVerts[pTri->m_Indices[2]];
		TLVertex *pV1 = &pVerts[pTri->m_Indices[1]];
		uint32 clipFlags;
		int nIn = 3;
		if (g_ClipFlags & 4)
		{
			nIn = (-pV0->m_Vec.z < pV0->m_Vec.x) + (-pV1->m_Vec.z < pV1->m_Vec.x) + (-pV2->m_Vec.z < pV2->m_Vec.x);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x3f;
		}
		if (nIn == 3 && (g_ClipFlags & 0x10))
		{
			nIn = (pV0->m_Vec.x < pV0->m_Vec.z) + (pV1->m_Vec.x < pV1->m_Vec.z) + (pV2->m_Vec.x < pV2->m_Vec.z);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x3b;
		}
		if (nIn == 3 && (g_ClipFlags & 8))
		{
			nIn = (pV0->m_Vec.y < pV0->m_Vec.z) + (pV1->m_Vec.y < pV1->m_Vec.z) + (pV2->m_Vec.y < pV2->m_Vec.z);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x2b;
		}
		if (nIn == 3 && (g_ClipFlags & 0x20))
		{
			nIn = (-pV0->m_Vec.z < pV0->m_Vec.y) + (-pV1->m_Vec.z < pV1->m_Vec.y) + (-pV2->m_Vec.z < pV2->m_Vec.y);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x23;
		}
		if (nIn == 3 && (g_ClipFlags & 1))
		{
			nIn = (pV0->m_Vec.z >= g_ViewParams.m_NearZ) + (pV1->m_Vec.z >= g_ViewParams.m_NearZ) + (pV2->m_Vec.z >= g_ViewParams.m_NearZ);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 3;
		}
		if (nIn == 3 && (g_ClipFlags & 2))
		{
			nIn = (pV0->m_Vec.z <= g_ViewParams.m_ClipFarZ) + (pV1->m_Vec.z <= g_ViewParams.m_ClipFarZ) + (pV2->m_Vec.z <= g_ViewParams.m_ClipFarZ);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 2;
		}
		if (nIn == 3)
		{
			if (!IsFrontFacing(pV0, pV1, pV2))
				goto Skip;
			ProjectVertexWithDepthBias(pOut, pV0, g_CV_NearZ.m_FloatVal);
			pOut->color = pV0->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV0, &pTri->m_UVs[0].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			ProjectVertexWithDepthBias(pOut, pV1, g_CV_NearZ.m_FloatVal);
			pOut->color = pV1->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV1, &pTri->m_UVs[1].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			ProjectVertexWithDepthBias(pOut, pV2, g_CV_NearZ.m_FloatVal);
			pOut->color = pV2->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV2, &pTri->m_UVs[2].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			if ((char *)pOut > pEnd && nTris > 1)
			{
				DrawFullModelPool(this);
				pOut = (TLVertex *)m_Unk608->Lock();
				pEnd = PoolLastVertex(m_Unk608);
			}
		}
		else
		{
			TLVertex *pPoly = (TLVertex *)aBuf;
			int nPoly = 3;
			TLVertex *pD = pPoly;
			pD->m_Vec = pV0->m_Vec;
			pD->color = pV0->color;
			pD->specular = m_Unk640;
			m_Unk5f4(pD, pV0, &pTri->m_UVs[0].tu);
			pD = (TLVertex *)((char *)pD + m_Unk5f8);
			pD->m_Vec = pV1->m_Vec;
			pD->color = pV1->color;
			pD->specular = m_Unk640;
			m_Unk5f4(pD, pV1, &pTri->m_UVs[1].tu);
			pD = (TLVertex *)((char *)pD + m_Unk5f8);
			pD->m_Vec = pV2->m_Vec;
			pD->color = pV2->color;
			pD->specular = m_Unk640;
			m_Unk5f4(pD, pV2, &pTri->m_UVs[2].tu);
			if (!m_Unk600(clipFlags, (void **)&pPoly, &nPoly))
				goto Skip;
			if (!IsFrontFacing(pPoly, (TLVertex *)((char *)pPoly + m_Unk5f8), (TLVertex *)((char *)pPoly + m_Unk5f8 * 2)))
				goto Skip;
			char *pPolyEnd = (char *)pPoly + m_Unk5f8 * nPoly;
			for (TLVertex *pP = pPoly; (char *)pP < pPolyEnd; pP = (TLVertex *)((char *)pP + m_Unk5f8))
			{
				LTVector vPos = pP->m_Vec;
				ProjectPositionWithDepthBias(&pP->m_Vec.x, &vPos.x, g_CV_NearZ.m_FloatVal);
			}
			if ((char *)pOut + (nPoly * 3 - 6) * m_Unk5f8 > pEnd)
			{
				FlushModelPool(this, pOut);
				m_Unk608->RestartInNextBuffer();
				pOut = (TLVertex *)m_Unk608->Lock();
				pEnd = PoolLastVertex(m_Unk608);
			}
			nPoly--;
			for (int i = 1; i < nPoly; )
			{
				m_Unk5fc(pOut, pPoly);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				m_Unk5fc(pOut, (char *)pPoly + m_Unk5f8 * i);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				i++;
				m_Unk5fc(pOut, (char *)pPoly + m_Unk5f8 * i);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			}
		}
Skip:
		nTris--;
		pTri++;
	}
	FlushModelPool(this, pOut);
	return 1;
}

// guess: the callback for pieces that cross a clip plane (m_Unk3c4[i]).  Per triangle the planes of g_ClipFlags are tested in the
// order left, right, top, bottom, near, far (count of vertices inside: none = skip the triangle).  A triangle that crosses a plane
// is built in a local vertex array and clipped by m_Unk600 against that plane and the ones not yet tested; the clipped polygon is
// back face tested, projected in place and drawn as a fan.  A triangle inside every plane is back face tested and projected
// straight into the pool.
// Not matching (same size, 31 aligned ignoring stack offsets): the vertex index load order (see DrawPieceClippedReallyClose) and the
// x87 term order of the MatVMul_H expansions and of IsFrontFacing (x/y swapped).  Loading 0, 1, 2 swaps ebx/ebp for pV1/pV2 (186);
// a vertex pointer array, the inline vInvZ back face test of DrawPieceProjected, MatVMul_InPlace_H for the clipped polygon and
// declarations at the top (C style, for(;;) loop) do not move it.  The permuter reaches 14 only with parameter copies, a reference to
// pV2->m_Vec in some of the plane tests and a float temporary (not source).
// PARKED: register/x87 order residue (vertex load order vs ebx/ebp choice, MatVMul_H term order); authentic levers exhausted, permuter states fake
// STUB: D3DREN 0x10002bc0
int ModelDraw::DrawPieceClipped(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->Lock();
	char *pEnd = PoolLastVertex(m_Unk608);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	char aBuf[0x500];
	while (nTris)
	{
		TLVertex *pV0 = &pVerts[pTri->m_Indices[0]];
		TLVertex *pV2 = &pVerts[pTri->m_Indices[2]];
		TLVertex *pV1 = &pVerts[pTri->m_Indices[1]];
		uint32 clipFlags;
		int nIn = 3;
		if (g_ClipFlags & 4)
		{
			nIn = (-pV0->m_Vec.z < pV0->m_Vec.x) + (-pV1->m_Vec.z < pV1->m_Vec.x) + (-pV2->m_Vec.z < pV2->m_Vec.x);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x3f;
		}
		if (nIn == 3 && (g_ClipFlags & 0x10))
		{
			nIn = (pV0->m_Vec.x < pV0->m_Vec.z) + (pV1->m_Vec.x < pV1->m_Vec.z) + (pV2->m_Vec.x < pV2->m_Vec.z);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x3b;
		}
		if (nIn == 3 && (g_ClipFlags & 8))
		{
			nIn = (pV0->m_Vec.y < pV0->m_Vec.z) + (pV1->m_Vec.y < pV1->m_Vec.z) + (pV2->m_Vec.y < pV2->m_Vec.z);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x2b;
		}
		if (nIn == 3 && (g_ClipFlags & 0x20))
		{
			nIn = (-pV0->m_Vec.z < pV0->m_Vec.y) + (-pV1->m_Vec.z < pV1->m_Vec.y) + (-pV2->m_Vec.z < pV2->m_Vec.y);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 0x23;
		}
		if (nIn == 3 && (g_ClipFlags & 1))
		{
			nIn = (pV0->m_Vec.z >= g_ViewParams.m_NearZ) + (pV1->m_Vec.z >= g_ViewParams.m_NearZ) + (pV2->m_Vec.z >= g_ViewParams.m_NearZ);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 3;
		}
		if (nIn == 3 && (g_ClipFlags & 2))
		{
			nIn = (pV0->m_Vec.z <= g_ViewParams.m_ClipFarZ) + (pV1->m_Vec.z <= g_ViewParams.m_ClipFarZ) + (pV2->m_Vec.z <= g_ViewParams.m_ClipFarZ);
			if (nIn == 0)
				goto Skip;
			if (nIn != 3)
				clipFlags = g_ClipFlags & 2;
		}
		if (nIn == 3)
		{
			if (!IsFrontFacing(pV0, pV1, pV2))
				goto Skip;
			pOut->rhw = MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_DeviceTimesProjection, &pV0->m_Vec);
			pOut->color = pV0->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV0, &pTri->m_UVs[0].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			pOut->rhw = MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_DeviceTimesProjection, &pV1->m_Vec);
			pOut->color = pV1->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV1, &pTri->m_UVs[1].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			pOut->rhw = MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_DeviceTimesProjection, &pV2->m_Vec);
			pOut->color = pV2->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV2, &pTri->m_UVs[2].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			if ((char *)pOut > pEnd && nTris > 1)
			{
				DrawFullModelPool(this);
				pOut = (TLVertex *)m_Unk608->Lock();
				pEnd = PoolLastVertex(m_Unk608);
			}
		}
		else
		{
			TLVertex *pPoly = (TLVertex *)aBuf;
			int nPoly = 3;
			TLVertex *pD = pPoly;
			pD->m_Vec = pV0->m_Vec;
			pD->color = pV0->color;
			pD->specular = m_Unk640;
			m_Unk5f4(pD, pV0, &pTri->m_UVs[0].tu);
			pD = (TLVertex *)((char *)pD + m_Unk5f8);
			pD->m_Vec = pV1->m_Vec;
			pD->color = pV1->color;
			pD->specular = m_Unk640;
			m_Unk5f4(pD, pV1, &pTri->m_UVs[1].tu);
			pD = (TLVertex *)((char *)pD + m_Unk5f8);
			pD->m_Vec = pV2->m_Vec;
			pD->color = pV2->color;
			pD->specular = m_Unk640;
			m_Unk5f4(pD, pV2, &pTri->m_UVs[2].tu);
			if (!m_Unk600(clipFlags, (void **)&pPoly, &nPoly))
				goto Skip;
			if (!IsFrontFacing(pPoly, (TLVertex *)((char *)pPoly + m_Unk5f8), (TLVertex *)((char *)pPoly + m_Unk5f8 * 2)))
				goto Skip;
			char *pPolyEnd = (char *)pPoly + m_Unk5f8 * nPoly;
			for (TLVertex *pP = pPoly; (char *)pP < pPolyEnd; pP = (TLVertex *)((char *)pP + m_Unk5f8))
				ProjectPosition(pP, pP);
			if ((char *)pOut + (nPoly * 3 - 6) * m_Unk5f8 > pEnd)
			{
				FlushModelPool(this, pOut);
				m_Unk608->RestartInNextBuffer();
				pOut = (TLVertex *)m_Unk608->Lock();
				pEnd = PoolLastVertex(m_Unk608);
			}
			nPoly--;
			for (int i = 1; i < nPoly; )
			{
				m_Unk5fc(pOut, pPoly);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				m_Unk5fc(pOut, (char *)pPoly + m_Unk5f8 * i);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				i++;
				m_Unk5fc(pOut, (char *)pPoly + m_Unk5f8 * i);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			}
		}
Skip:
		nTris--;
		pTri++;
	}
	FlushModelPool(this, pOut);
	return 1;
}

// guess: the callback for pieces that need no clipping: projects the vertices in software (matrix g_ViewParams.m_DeviceTimesProjection.m[0][0]) and draws.
// The projection is the SDK's MatVMul_H (its one_over_w * (...) rows give the exe's x87 term order); the back face test keeps the
// three reciprocal depths in an LTVector (vInvZ.x in a register, its slot reserved: the exe's frame).
// FUNCTION: D3DREN 0x100036d0
int ModelDraw::DrawPieceProjected(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->Lock();
	char *pEnd = PoolLastVertex(m_Unk608);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	for (;;)
	{
		if (nTris == 0)
		{
			FlushModelPool(this, pOut);
			return 1;
		}
		TLVertex *pV0 = &pVerts[pTri->m_Indices[0]];
		TLVertex *pV1 = &pVerts[pTri->m_Indices[1]];
		TLVertex *pV2 = &pVerts[pTri->m_Indices[2]];
		if (m_Unk8b4)
		{
			LTVector vInvZ, p0, d1, d2;
			vInvZ.x = 1.0f / pV0->m_Vec.z;
			vInvZ.y = 1.0f / pV1->m_Vec.z;
			vInvZ.z = 1.0f / pV2->m_Vec.z;
			p0.x = vInvZ.x * pV0->m_Vec.x;
			p0.y = vInvZ.x * pV0->m_Vec.y;
			d1.x = vInvZ.y * pV1->m_Vec.x - p0.x;
			d1.y = vInvZ.y * pV1->m_Vec.y - p0.y;
			d2.x = vInvZ.z * pV2->m_Vec.x - p0.x;
			d2.y = vInvZ.z * pV2->m_Vec.y - p0.y;
			float fCross = d2.x * d1.y - d2.y * d1.x;
			if (g_ViewParams.m_bCullFlip)
				fCross = -fCross;
			if (!(fCross > 0.0f))
				goto Skip;
		}
		pOut->rhw = MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_DeviceTimesProjection, &pV0->m_Vec);
		pOut->color = pV0->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV0, &pTri->m_UVs[0].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		pOut->rhw = MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_DeviceTimesProjection, &pV1->m_Vec);
		pOut->color = pV1->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV1, &pTri->m_UVs[1].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		pOut->rhw = MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_DeviceTimesProjection, &pV2->m_Vec);
		pOut->color = pV2->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV2, &pTri->m_UVs[2].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		if ((char *)pOut > pEnd && nTris > 1)
		{
			DrawFullModelPool(this);
			pOut = (TLVertex *)m_Unk608->Lock();
			pEnd = PoolLastVertex(m_Unk608);
		}
Skip:
		nTris--;
		pTri++;
	}
}

// guess: the default draw callback: copies the transformed vertices of each (front facing) triangle into the pool
// The three vertex pointers are an array: its three frame slots stay reserved when pV[0] lives in a register (the exe's 0x14 frame with
// one unused slot), and the triangle count then takes the dead pLOD argument home.
// FUNCTION: D3DREN 0x10003b60
int ModelDraw::DrawPieceTransformed(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->Lock();
	char *pEnd = PoolLastVertex(m_Unk608);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	TLVertex *pV[3];
	while (nTris)
	{
		pV[0] = &pVerts[pTri->m_Indices[0]];
		pV[1] = &pVerts[pTri->m_Indices[1]];
		pV[2] = &pVerts[pTri->m_Indices[2]];
		if (m_Unk8b4)
		{
			float fCross = (pV[1]->m_Vec.x - pV[0]->m_Vec.x) * (pV[2]->m_Vec.y - pV[0]->m_Vec.y)
				- (pV[2]->m_Vec.x - pV[0]->m_Vec.x) * (pV[1]->m_Vec.y - pV[0]->m_Vec.y);
			if (g_ViewParams.m_bCullFlip)
				fCross = -fCross;
			if (!(fCross > g_CV_ModelMinTri.m_FloatVal))
				goto Skip;
		}
		pOut->m_Vec = pV[0]->m_Vec;
		pOut->rhw = pV[0]->rhw;
		pOut->color = pV[0]->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV[0], &pTri->m_UVs[0].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		pOut->m_Vec = pV[1]->m_Vec;
		pOut->rhw = pV[1]->rhw;
		pOut->color = pV[1]->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV[1], &pTri->m_UVs[1].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		pOut->m_Vec = pV[2]->m_Vec;
		pOut->rhw = pV[2]->rhw;
		pOut->color = pV[2]->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV[2], &pTri->m_UVs[2].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		if ((char *)pOut > pEnd && nTris > 1)
		{
			DrawFullModelPool(this);
			pOut = (TLVertex *)m_Unk608->Lock();
			pEnd = PoolLastVertex(m_Unk608);
		}
Skip:
		nTris--;
		pTri++;
	}
	FlushModelPool(this, pOut);
	return 1;
}

// The TnL vertex of the hardware T&L pool is a TLVertex without rhw (position, diffuse, specular, one texture coordinate pair, 0x1c
// bytes): after the position the output pointer steps back 4 bytes so that the TLVertex colour/texture-coordinate members land on it.

// guess: the untransformed variant of DrawPieceTransformed: writes XYZ + diffuse + specular vertices (stride m_Unk5f8 - 4) so that
// Direct3D transforms them; the back face test (m_Unk8b4) is done on the model-space x/y of the vertices.
// The vertex pointer array and the count decremented before the cursor step as in DrawPieceTransformed.
// FUNCTION: D3DREN 0x10003e00
int ModelDraw::DrawPieceUntransformed(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->Lock();
	char *pEnd = PoolLastVertex(m_Unk608);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	TLVertex *pV[3];
	if (m_Unk8b4)
	{
		while (nTris)
		{
			pV[0] = &pVerts[pTri->m_Indices[0]];
			pV[1] = &pVerts[pTri->m_Indices[1]];
			pV[2] = &pVerts[pTri->m_Indices[2]];
			float fCross = (pV[1]->m_Vec.x - pV[0]->m_Vec.x) * (pV[2]->m_Vec.y - pV[0]->m_Vec.y)
				- (pV[2]->m_Vec.x - pV[0]->m_Vec.x) * (pV[1]->m_Vec.y - pV[0]->m_Vec.y);
			if (g_ViewParams.m_bCullFlip)
				fCross = -fCross;
			if (fCross > g_CV_ModelMinTri.m_FloatVal)
			{
				pOut->m_Vec = pV[0]->m_Vec;
				pOut = (TLVertex *)((char *)pOut - 4);
				pOut->color = pV[0]->color;
				pOut->specular = m_Unk640;
				m_Unk5f4(pOut, pV[0], &pTri->m_UVs[0].tu);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				pOut->m_Vec = pV[1]->m_Vec;
				pOut = (TLVertex *)((char *)pOut - 4);
				pOut->color = pV[1]->color;
				pOut->specular = m_Unk640;
				m_Unk5f4(pOut, pV[1], &pTri->m_UVs[1].tu);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				pOut->m_Vec = pV[2]->m_Vec;
				pOut = (TLVertex *)((char *)pOut - 4);
				pOut->color = pV[2]->color;
				pOut->specular = m_Unk640;
				m_Unk5f4(pOut, pV[2], &pTri->m_UVs[2].tu);
				pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
				if ((char *)pOut > pEnd && nTris > 1)
				{
					DrawFullModelPool(this);
					pOut = (TLVertex *)m_Unk608->Lock();
					pEnd = PoolLastVertex(m_Unk608);
				}
			}
			nTris--;
			pTri++;
		}
	}
	else
	{
		while (nTris)
		{
			pV[0] = &pVerts[pTri->m_Indices[0]];
			pV[1] = &pVerts[pTri->m_Indices[1]];
			pV[2] = &pVerts[pTri->m_Indices[2]];
			pOut->m_Vec = pV[0]->m_Vec;
			pOut = (TLVertex *)((char *)pOut - 4);
			pOut->color = pV[0]->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV[0], &pTri->m_UVs[0].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			pOut->m_Vec = pV[1]->m_Vec;
			pOut = (TLVertex *)((char *)pOut - 4);
			pOut->color = pV[1]->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV[1], &pTri->m_UVs[1].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			pOut->m_Vec = pV[2]->m_Vec;
			pOut = (TLVertex *)((char *)pOut - 4);
			pOut->color = pV[2]->color;
			pOut->specular = m_Unk640;
			m_Unk5f4(pOut, pV[2], &pTri->m_UVs[2].tu);
			pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
			if ((char *)pOut > pEnd && nTris > 1)
			{
				DrawFullModelPool(this);
				pOut = (TLVertex *)m_Unk608->Lock();
				pEnd = PoolLastVertex(m_Unk608);
			}
			nTris--;
			pTri++;
		}
	}
	FlushModelPool(this, pOut);
	return 1;
}

// FUNCTION: D3DREN 0x10004230
int ModelDraw::DrawPieceCached(PieceLOD *pLOD, TLVertex *pVerts)
{
	uint32 nIndices = pLOD->m_Tris.GetSize() * 3;
	g_nModelTrianglesDrawn += nIndices / 3;
	((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nIndices);
	return 1;
}

void d3d_BuildSpecularLookupTexture(float fSpecularPower);	// unit d3d_texture (W8): rebuilds the specular lookup table texture

// guess: runs the draw callbacks over the pieces of the model: picks the vertex format / pool, binds each piece's skin and
// calls pfnDrawA for the pieces that need no clipping and pfnDrawB for the others.
// FUNCTION: D3DREN 0x10004270
void ModelDraw::DrawPiecesWithCallbacks(PFN_DrawPiece pfnDrawA, PFN_DrawPiece pfnDrawB, int a3)
{
	m_Unk5f0 = a3;
	if (m_Unk5e8 == 1)
	{
		m_Unk5f8 = 0x20;
		m_Unk5fc = (PFN_CopyVertex)CopyTLVertex32;
		m_Unk600 = (PFN_ClipPolygon)ClipModelPolygon32;
		m_Unk604 = 0x1c4;
		if (m_Unk8b0)
			m_Unk608 = &g_ModelUntransformedVertexPoolTex1;
		else
			m_Unk608 = &g_ModelTransformedVertexPoolTex1;
	}
	else
	{
		m_Unk5f8 = 0x28;
		m_Unk5fc = (PFN_CopyVertex)CopyTLVertex40;
		m_Unk600 = (PFN_ClipPolygon)ClipModelPolygon40;
		m_Unk604 = 0x2c4;
		if (m_Unk8b0)
			m_Unk608 = &g_ModelUntransformedVertexPoolTex2;
		else
			m_Unk608 = &g_ModelTransformedVertexPoolTex2;
	}
	if (m_Unk608->m_Unk20 != g_CV_ModelVBSize.m_IntVal || m_Unk608->m_Unk24 != g_CV_ModelVBCount.m_IntVal)
	{
		g_ModelTransformedVertexPoolTex1.Term();
		g_ModelTransformedVertexPoolTex2.Term();
		UnkType_VertexBufferPool *pCache = &UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache;
		pCache->Term();
		d3d_InitModelVertexBufferPools();
	}
	if (m_Unk8b0)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
	if (m_Unk8ac)
	{
		uint32 nFVF = (m_Unk604 & ~D3DFVF_XYZRHW) | D3DFVF_XYZ;
		uint32 nStride = m_Unk5f8 - 4;
		m_Unk608 = &UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache;
		UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache.m_Unk58 = nFVF;
		UnkType_ModelVBCacheHolder::s_ModelVertexBufferCache.m_Unk54 = nStride;
	}
	int bCull;
	if (m_Unk8ac == 1 || m_Unk8b0)
		bCull = 0;
	else
		bCull = 1;
	m_Unk8b4 = bCull;
	m_Unk4cc = -1;
	TLVertex *pVerts = m_Unk82c;
	for (uint32 i = 0; i < m_pModel->m_Pieces.GetSize(); i++)
	{
		ModelPiece *pPiece = m_pModel->m_Pieces[i];
		PieceLOD *pLOD = pPiece->GetLOD(m_nLOD);
		if (pLOD && !(m_pInstance->m_HiddenPieces & (1 << i)) && !m_Unk2c4[i])
		{
			if (pPiece->m_TextureIndex != m_Unk4cc)
			{
				BindModelSkinTextures(pPiece->m_TextureIndex);
				BeginModelRenderPass((uint32 *)&m_Unk5f0);
				SharedTexture *pSkin = m_pInstance->m_pSkins[pPiece->m_TextureIndex];
				if (pSkin)
				{
					StateChange *pStateChange = pSkin->m_pStateChange;
					if (pStateChange)
					{
						g_TextureStateRestorer.RestoreAllStates();
						g_TextureStateRestorer.ApplyStateChange(pStateChange, m_Unk34);
					}
				}
			}
			m_Unk62c = pPiece->m_SpecularPower;
			m_Unk630 = pPiece->m_SpecularScale;
			if (g_CV_SpecularPowerTest.m_FloatVal != 0.0f)
				m_Unk62c = g_CV_SpecularPowerTest.m_FloatVal;
			if (g_CV_SpecularScaleTest.m_FloatVal != 0.0f)
				m_Unk630 = g_CV_SpecularScaleTest.m_FloatVal;
			if (m_Unk630 != 0.0f && m_Unk5ec == (PFN_GenTexCoords)GenerateModelSpecularCoords)
				d3d_BuildSpecularLookupTexture(m_Unk62c);
			if (m_Unk3c4[i])
				(this->*pfnDrawB)(pLOD, pVerts);
			else
				(this->*pfnDrawA)(pLOD, pVerts);
			pVerts += pLOD->m_Verts.GetSize();
		}
	}
	if (m_Unk8b0)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
	g_TextureStateRestorer.RestoreAllStates();
}

// Note: the exe loads the second member pointer into edx first: `pfnB = pfnA = X;` (assignment order) gives that register order
// FUNCTION: D3DREN 0x100045a0
void ModelDraw::SelectPieceDrawCallbacks(int a1)
{
	PFN_DrawPiece pfnA, pfnB;
	if (m_Unk8ac == 2)
	{
		pfnB = pfnA = &ModelDraw::DrawPieceCached;
	}
	else if (m_Unk8ac == 1 || m_Unk8b0)
	{
		pfnB = pfnA = &ModelDraw::DrawPieceUntransformed;
	}
	else if (m_pInstance->m_Flags & 0x40)
	{
		pfnB = pfnA = &ModelDraw::DrawPieceClippedReallyClose;
	}
	else if (g_ClipFlags & 0x3f)
	{
		pfnA = &ModelDraw::DrawPieceProjected;
		pfnB = &ModelDraw::DrawPieceClipped;
	}
	else
	{
		pfnB = pfnA = &ModelDraw::DrawPieceTransformed;
	}
	DrawPiecesWithCallbacks(pfnA, pfnB, a1);
}

// ---- the skinning / lighting / projection of one piece (SkinAndLightPieceVertices) and its driver (PrepareModelPieceVertices) ----------------------

// The four loops of SkinAndLightPieceVertices (bounds on/off x LOD blend on/off) are built from the macros below; the exe has the
// four copies.  The macro names are mine (no evidence).
// Skins the vertex into pDest: SDK MatVMul_Add per bone weight, the weight sum accumulated in rhw.
#define SKIN_MODEL_VERTEX() \
		pDest->m_Vec.x = 0.0f; \
		pDest->m_Vec.y = 0.0f; \
		pDest->m_Vec.z = 0.0f; \
		pDest->rhw = 0.0f; \
		pWeight = pVert->m_Weights; \
		for (nWeights = pVert->m_nWeights; nWeights != 0; nWeights--) \
		{ \
			MatVMul_Add(&pDest->m_Vec.x, &pTransforms[pWeight->m_iNode], pWeight->m_Vec); \
			pWeight++; \
		}

// Blends the skinned vertex with its replacement vertex in the coarser LOD pLOD2 (m_fLODBlend) and divides by the weight sum.
#define BLEND_MODEL_VERTEX() \
		{ \
			float vB[4]; \
			vB[0] = vB[1] = vB[2] = vB[3] = 0.0f; \
			pWeight = pVertB->m_Weights; \
			for (nWeights = pVertB->m_nWeights; nWeights != 0; nWeights--) \
			{ \
				MatVMul_Add(vB, &pTransforms[pWeight->m_iNode], pWeight->m_Vec); \
				pWeight++; \
			} \
			pDest->m_Vec.x = pDest->m_Vec.x + (vB[0] - pDest->m_Vec.x) * m_fLODBlend; \
			pDest->m_Vec.y = pDest->m_Vec.y + (vB[1] - pDest->m_Vec.y) * m_fLODBlend; \
			pDest->m_Vec.z = pDest->m_Vec.z + (vB[2] - pDest->m_Vec.z) * m_fLODBlend; \
			pDest->rhw = 1.0f / (pDest->rhw + (vB[3] - pDest->rhw) * m_fLODBlend); \
		}

// Grows the bounding box pMin/pMax by the projected vertex.
#define UPDATE_MODEL_BOUNDS() \
		if (pDest->m_Vec.x < pMin->x) \
			pMin->x = pDest->m_Vec.x; \
		else if (pDest->m_Vec.x > pMax->x) \
			pMax->x = pDest->m_Vec.x; \
		if (pDest->m_Vec.y < pMin->y) \
			pMin->y = pDest->m_Vec.y; \
		else if (pDest->m_Vec.y > pMax->y) \
			pMax->y = pDest->m_Vec.y; \
		if (pDest->m_Vec.z < pMin->z) \
			pMin->z = pDest->m_Vec.z; \
		else if (pDest->m_Vec.z > pMax->z) \
			pMax->z = pDest->m_Vec.z;

// Lights the vertex: the directional light (between the shadow and the lit colour) plus the model lights, clamped to 255, added
// to the piece's light sum; then the per-vertex generator.
#define LIGHT_MODEL_VERTEX() \
		{ \
			float fDot = VEC_DOT(vDir, pVert->m_Normal); \
			if (fDot > 0.0f) \
				VEC_LERP(vColor, vBase, vLit, fDot) \
			else \
				vColor = vBase; \
			for (UnkType_ModelLight *pLight = m_Unk3c; pLight != &m_Unk3c[m_nModelLights]; pLight++) \
			{ \
				float fLightDot = pLight->m_Unk10.Dot(pVert->m_Normal); \
				if (fLightDot > 0.0f) \
				{ \
					float fDistSqr = (pVert->m_Vec - pLight->m_Unk00).MagSqr(); \
					if (fDistSqr < pLight->m_Unk0c) \
						vColor += pLight->m_Unk1c * ((pLight->m_Unk0c - fDistSqr) * fLightDot); \
				} \
			} \
			if (vColor.x > 255.0f) \
				vColor.x = 255.0f; \
			if (vColor.y > 255.0f) \
				vColor.y = 255.0f; \
			if (vColor.z > 255.0f) \
				vColor.z = 255.0f; \
			*pLighting += vColor; \
			pDest->rgb.r = (uint8)RoundFloatToInt(vColor.x); \
			pDest->rgb.g = (uint8)RoundFloatToInt(vColor.y); \
			pDest->rgb.b = (uint8)RoundFloatToInt(vColor.z); \
			pDest->rgb.a = m_Unk8a8; \
			pfnPerVertex(this, pVert, pDest); \
		}

// guess: skins, lights and projects the vertices of one piece into pDest (TL vertices), calls the per-vertex generator,
// accumulates the vertex colours into pLighting and (bBounds) the min/max of the projected positions into pMin/pMax.
// With the LOD blend enabled (m_bLODBlend) every vertex is the m_fLODBlend blend of the vertex of pLOD and its replacement in pLOD2.
// Not matching (4240 vs 4256 bytes; frame and ebp slots as the exe): register allocation.  The exe keeps `this` in ebx (spilled to
// [ebp-4] where the first skin loop of the blend copies needs ebx for pTransforms), computes the replacement vertex before the skin
// loop and walks pVert from +0x18; ours keeps `this` in edi and computes pVertB after the loop.  pLighting/pMin/pMax are LTVector*
// (the exe initialises pMin with Init and pMax through an LTVector temporary).
// STUB: D3DREN 0x10004660
void ModelDraw::SkinAndLightPieceVertices(PieceLOD *pLOD, PieceLOD *pLOD2, TLVertex *pDest, PFN_GenTexCoords pfnPerVertex, LTMatrix *pTransforms,
	LTVector *pLighting, char bBounds, LTVector *pMin, LTVector *pMax)
{
	LTVector vDir = m_DirLightDir * m_DirLightAmount;
	LTVector vBase = m_AmbientLight * m_ObjectColor + m_LightAdd;
	LTVector vLit = m_DirLightColor * m_ObjectColor + m_LightAdd;
	LTVector vColor;
	ModelVert *pVert = pLOD->m_Verts.GetArray();
	int nVerts = pLOD->m_Verts.GetSize();
	NewVertexWeight *pWeight;
	uint32 nWeights;

	if (!bBounds)
	{
		if (m_bLODBlend)
		{
			ModelVert *pVerts2 = pLOD2->m_Verts.GetArray();
			for (; nVerts != 0; nVerts--, pVert++, pDest++)
			{
				ModelVert *pVertB = &pVerts2[pVert->m_iReplacement];
				SKIN_MODEL_VERTEX()
				BLEND_MODEL_VERTEX()
				pDest->m_Vec *= pDest->rhw;
				LIGHT_MODEL_VERTEX()
			}
		}
		else
		{
			for (; nVerts != 0; nVerts--, pVert++, pDest++)
			{
				SKIN_MODEL_VERTEX()
				pDest->rhw = 1.0f / pDest->rhw;
				pDest->m_Vec *= pDest->rhw;
				LIGHT_MODEL_VERTEX()
			}
		}
	}
	else
	{
		pMin->Init(100000.0f, 100000.0f, 100000.0f);
		*pMax = LTVector(-100000.0f, -100000.0f, -100000.0f);
		if (m_bLODBlend)
		{
			ModelVert *pVerts2 = pLOD2->m_Verts.GetArray();
			for (; nVerts != 0; nVerts--, pVert++, pDest++)
			{
				ModelVert *pVertB = &pVerts2[pVert->m_iReplacement];
				SKIN_MODEL_VERTEX()
				BLEND_MODEL_VERTEX()
				pDest->m_Vec *= pDest->rhw;
				UPDATE_MODEL_BOUNDS()
				LIGHT_MODEL_VERTEX()
			}
		}
		else
		{
			for (; nVerts != 0; nVerts--, pVert++, pDest++)
			{
				SKIN_MODEL_VERTEX()
				pDest->rhw = 1.0f / pDest->rhw;
				pDest->m_Vec *= pDest->rhw;
				UPDATE_MODEL_BOUNDS()
				LIGHT_MODEL_VERTEX()
			}
		}
	}
}

// guess: per piece of the model: picks the LOD(s), skins/lights/projects the piece into m_Unk82c and, when clipping is on,
// tests the 8 corners of the projected bounding box against the clip planes: m_Unk2c4[i] = completely outside (skip the piece),
// m_Unk3c4[i] = crosses a plane (needs the clipping callback).  The mean vertex light is stored in the instance (m_ModelLighting).
// FUNCTION: D3DREN 0x10005700
void ModelDraw::PrepareModelPieceVertices()
{
	LTMatrix *pTransforms;
	if (g_ClipFlags)
		pTransforms = m_pModel->m_Transforms.GetArray();
	else
		pTransforms = m_Unk840;
	PieceLOD *pLODB = 0;
	int iVertBase = 0;
	uint32 nTotalVerts = 0;
	for (uint32 i = 0; i < m_pModel->m_Pieces.GetSize(); i++)
	{
		ModelPiece *pPiece = m_pModel->m_Pieces[i];
		PieceLOD *pLOD = pPiece->GetLOD(m_nLOD);
		if (pLOD)
		{
			if (m_bLODBlend)
				pLODB = pPiece->GetLOD(m_nLOD + 1);
			if (!(m_pInstance->m_HiddenPieces & (1 << i)))
			{
				m_Unk62c = pPiece->m_SpecularPower;
				m_Unk630 = pPiece->m_SpecularScale;
				if (g_CV_SpecularPowerTest.m_FloatVal != 0.0f)
					m_Unk62c = g_CV_SpecularPowerTest.m_FloatVal;
				if (g_CV_SpecularScaleTest.m_FloatVal != 0.0f)
					m_Unk630 = g_CV_SpecularScaleTest.m_FloatVal;
				char bBounds;
				if ((g_ClipFlags & 0x3f) == 0 || (m_pInstance->m_Flags & 0x40))
					bBounds = 0;
				else
					bBounds = 1;
				LTVector vMin, vMax;
				SkinAndLightPieceVertices(pLOD, pLODB, m_Unk82c + iVertBase, m_Unk5ec, pTransforms, &m_pInstance->m_ModelLighting, bBounds, &vMin, &vMax);
				if (bBounds)
				{
					// the 8 corners of the bounding box (bit 2: x, bit 1: y, bit 0: z from vMax) against the six clip planes
					LTVector pts[8];
					pts[0].Init(vMin.x, vMin.y, vMin.z);
					pts[1].Init(vMin.x, vMin.y, vMax.z);
					pts[2].Init(vMin.x, vMax.y, vMin.z);
					pts[3].Init(vMin.x, vMax.y, vMax.z);
					pts[4].Init(vMax.x, vMin.y, vMin.z);
					pts[5].Init(vMax.x, vMin.y, vMax.z);
					pts[6].Init(vMax.x, vMax.y, vMin.z);
					pts[7].Init(vMax.x, vMax.y, vMax.z);
					int nIn;
#define BOX_CLIP_TEST(T) \
					nIn = T(0) + T(1) + T(2) + T(3) + T(4) + T(5) + T(6) + T(7); \
					if (nIn == 0) \
						m_Unk2c4[i] = 1; \
					else if (nIn < 8) \
						m_Unk3c4[i] = 1;
#define IN_NEAR(k)		(pts[k].z >= g_ViewParams.m_NearZ)
#define IN_FAR(k)		(pts[k].z <= g_ViewParams.m_ClipFarZ)
#define IN_LEFT(k)		(pts[k].x > -pts[k].z)
#define IN_RIGHT(k)		(pts[k].x < pts[k].z)
#define IN_TOP(k)		(pts[k].y < pts[k].z)
#define IN_BOTTOM(k)	(pts[k].y > -pts[k].z)
					BOX_CLIP_TEST(IN_NEAR)
					BOX_CLIP_TEST(IN_FAR)
					BOX_CLIP_TEST(IN_LEFT)
					BOX_CLIP_TEST(IN_RIGHT)
					BOX_CLIP_TEST(IN_TOP)
					BOX_CLIP_TEST(IN_BOTTOM)
#undef IN_BOTTOM
#undef IN_TOP
#undef IN_RIGHT
#undef IN_LEFT
#undef IN_FAR
#undef IN_NEAR
#undef BOX_CLIP_TEST
				}
				if (m_Unk2c4[i] == 0)
				{
					iVertBase += pLOD->m_Verts.GetSize();
					nTotalVerts += pLOD->m_Verts.GetSize();
				}
			}
		}
	}
	if (nTotalVerts > 0)
	{
		m_pInstance->m_ModelLighting.x /= (float)nTotalVerts;
		m_pInstance->m_ModelLighting.y /= (float)nTotalVerts;
		m_pInstance->m_ModelLighting.z /= (float)nTotalVerts;
	}
}

// The in-class inline virtuals (defined in vbpool.h), the scalar deleting destructor and the buffer creation virtuals.
// The four classes have identical members; the linker folded them, so only one instance of each is annotated:
// FUNCTION: D3DREN 0x10006140 ?GetVertexSize@?$UnkType_VertexBufferPoolA@$0A@@@UAEHXZ
// FUNCTION: D3DREN 0x10006150 ?GetVertexSize@?$UnkType_VertexBufferPoolB@$0A@@@UAEHXZ
// FUNCTION: D3DREN 0x10006160 ??_G?$UnkType_VertexBufferPoolA@$0A@@@UAEPAXI@Z
// FUNCTION: D3DREN 0x10006180 ?CreateVertexBuffers@?$UnkType_VertexBufferPoolA@$0A@@@UAEHPAUIDirect3D7@@@Z
// FUNCTION: D3DREN 0x10006230 ?CreateVertexBuffers@?$UnkType_VertexBufferPoolB@$0A@@@UAEHPAUIDirect3D7@@@Z

template <int N> int UnkType_VertexBufferPoolA<N>::CreateVertexBuffers(IDirect3D7 *pD3D)
{
	D3DVERTEXBUFFERDESC desc;
	LPDIRECT3DVERTEXBUFFER7 pVB;
	int bOK;
	uint32 i;

	memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwCaps = D3DVBCAPS_WRITEONLY;
	desc.dwFVF = m_Unk28 ? (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)
		: (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1);
	if (m_Unk2c == 0)
		desc.dwCaps = D3DVBCAPS_WRITEONLY | D3DVBCAPS_SYSTEMMEMORY;
	desc.dwNumVertices = m_Unk20;
	bOK = 1;
	for (i = 0; i < m_Unk24 && bOK; i++)
	{
		if (SUCCEEDED(pD3D->CreateVertexBuffer(&desc, &pVB, 0)))
			m_Unk04[i] = pVB;
		else
			bOK = 0;
	}
	if (!bOK)
		ReleaseVertexBuffers();
	return bOK;
}

template <int N> int UnkType_VertexBufferPoolB<N>::CreateVertexBuffers(IDirect3D7 *pD3D)
{
	D3DVERTEXBUFFERDESC desc;
	LPDIRECT3DVERTEXBUFFER7 pVB;
	int bOK;
	uint32 i;

	memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwCaps = D3DVBCAPS_WRITEONLY;
	desc.dwFVF = m_Unk28 ? (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2)
		: (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2);
	if (m_Unk2c == 0)
		desc.dwCaps = D3DVBCAPS_WRITEONLY | D3DVBCAPS_SYSTEMMEMORY;
	desc.dwNumVertices = m_Unk20;
	bOK = 1;
	for (i = 0; i < m_Unk24 && bOK; i++)
	{
		if (SUCCEEDED(pD3D->CreateVertexBuffer(&desc, &pVB, 0)))
			m_Unk04[i] = pVB;
		else
			bOK = 0;
	}
	if (!bOK)
		ReleaseVertexBuffers();
	return bOK;
}
// (defined in unk/100062e0)
void __cdecl ProjectPositionWithDepthBias(float *pDest, float *pSrc, float fZBias);

static inline void ClipExtra40(UnkType_TLVertex40 *pPrev, UnkType_TLVertex40 *pCur, UnkType_TLVertex40 *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->tu2 = (pCur->tu2 - pPrev->tu2) * t + pPrev->tu2;
	pOut->tv2 = (pCur->tv2 - pPrev->tv2) * t + pPrev->tv2;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}
// (defined in unk/100062e0)
int ClipPolyTop40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
// (defined in unk/100062e0)
int ClipPolyRight40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
// (defined in unk/100062e0)
int ClipPolyBottom40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
// (defined in unk/100062e0)
int ClipPolyFar40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
