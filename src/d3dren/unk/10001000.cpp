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
UnkType_VertexBufferPoolA<0> DAT_1004d620(0);
// FUNCTION: D3DREN 0x10001090 _$E12
// FUNCTION: D3DREN 0x100010c0 _$E10
// GLOBAL: D3DREN 0x1004da80
UnkType_VertexBufferPoolB<0> DAT_1004da80(0);
// FUNCTION: D3DREN 0x100010d0 _$E17
// FUNCTION: D3DREN 0x10001100 _$E15
// GLOBAL: D3DREN 0x1004eb48
UnkType_VertexBufferPoolA<1> DAT_1004eb48(1);
// FUNCTION: D3DREN 0x10001110 _$E22
// FUNCTION: D3DREN 0x10001140 _$E20
// GLOBAL: D3DREN 0x1004dae0
UnkType_VertexBufferPoolB<1> DAT_1004dae0(1);

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

float DAT_1004eb40;
float DAT_1004eb44;

// The exe evaluates the arguments of this right to left and keeps both on the x87 stack (FUN_10001370, FUN_100013d0).
static inline void SetUV(TLVertex *pVert, float u, float v)
{
	pVert->tu = u;
	pVert->tv = v;
}

// guess: (re)creates the four model vertex buffer pools and the model cache from the ModelVB* console variables: the size
// is rounded up to a multiple of 3 (whole triangles), the count is at least 1.
// FUNCTION: D3DREN 0x10001230
void FUN_10001230(void)
{
	if (g_CV_ModelVBSize.m_IntVal <= 1)
		g_CV_ModelVBSize.m_IntVal = 1;
	if (g_CV_ModelVBCount.m_IntVal <= 1)
		g_CV_ModelVBCount.m_IntVal = 1;
	g_CV_ModelVBSize.m_IntVal = ((g_CV_ModelVBSize.m_IntVal + 2) / 3) * 3;
	DAT_1004d620.FUN_1003a83d(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 0, DAT_10058494);
	DAT_1004da80.FUN_1003a83d(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 0, DAT_10058494);
	DAT_1004eb48.FUN_1003a83d(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 1, DAT_10058494);
	DAT_1004dae0.FUN_1003a83d(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCount.m_IntVal, 1, DAT_10058494);
	UnkType_VertexBufferPool *pCache = &UnkType_ModelVBCacheHolder::DAT_10093b10;
	pCache->FUN_1003a83d(g_pD3D, g_CV_ModelVBSize.m_IntVal, g_CV_ModelVBCache.m_IntVal, 1, DAT_10058494);
	UnkType_ModelVBCacheHolder::DAT_10093b10.m_Unk60 = g_CV_ModelVBCacheDelay.m_IntVal;
}

// guess: releases the two non-cache model pools and the cache (Term, slot 3).
// FUNCTION: D3DREN 0x10001340
void FUN_10001340(void)
{
	DAT_1004d620.FUN_1003a805();
	DAT_1004da80.FUN_1003a805();
	UnkType_VertexBufferPool *pCache = &UnkType_ModelVBCacheHolder::DAT_10093b10;
	pCache->FUN_1003a805();
}

// FUNCTION: D3DREN 0x10001370
void __fastcall FUN_10001370(TLVertex *pDest, void *pSrc, float *pUV)
{
	SetUV(pDest, DAT_1004eb40 + pUV[0], DAT_1004eb44 + pUV[1]);
}

// FUNCTION: D3DREN 0x10001390
void __fastcall FUN_10001390(UnkType_TLVertex40 *pDest, void *pSrc, float *pUV)
{
	pDest->tu = DAT_1004eb40 + pUV[0];
	pDest->tv = DAT_1004eb44 + pUV[1];
	pDest->tu2 = g_CV_ModelDetailTextureScale.m_FloatVal * pDest->tu;
	pDest->tv2 = g_CV_ModelDetailTextureScale.m_FloatVal * pDest->tv;
}

// FUNCTION: D3DREN 0x100013d0
void __fastcall FUN_100013d0(TLVertex *pDest, TLVertex *pSrc, float *pUV)
{
	SetUV(pDest, pSrc->tu, pSrc->tv);
}

// FUNCTION: D3DREN 0x100013e0
void __fastcall FUN_100013e0(UnkType_TLVertex40 *pDest, TLVertex *pSrc, float *pUV)
{
	pDest->tu = DAT_1004eb40 + pUV[0];
	pDest->tv = DAT_1004eb44 + pUV[1];
	pDest->tu2 = pSrc->tu;
	pDest->tv2 = pSrc->tv;
}

// FUNCTION: D3DREN 0x10001410
void __fastcall FUN_10001410(void *pDest, void *pSrc, float *pUV)
{
}

// guess: environment map coordinates from the vertex normal: u/v = (normal . row of the matrix at +0x590) * scale + offset
// (m_EnvMapTransform is an LTMatrix: rows 0 and 1 give u and v).
// Not matching (12/112 bytes; 18 before the named copy of pSrc): same 37 instructions, the x87 term order of the two sums differs.
// The exe computes v (the second SetUV argument) first as (5a4*ny + 5a0*nx) + 5a8*nz and then u as (594*ny + 598*nz) + nx*590 (the last
// product with swapped operands: `fld nx; fmul m590`); ours gives (5a4*ny + 5a8*nz)-first for v and `fld m590; fmul nx` for the last u
// product.  Source term order never changes it (72 permutations); a named copy of pSrc moves it closer; SDK MatVMul_3x3 into an
// LTVector temporary (computes u first) is worse (68 bytes); 3000 permuter candidates stopped at this form.
// STUB: D3DREN 0x10001420
void __fastcall FUN_10001420(UnkType_ModelDrawerVertexView *pThis, UnkType_ModelVertex *pSrc, TLVertex *pDest)
{
	UnkType_ModelVertex *pVert = pSrc;	// the named copy changes the x87 term order: 18 -> 12 bytes (found with tools/permute.py)
	SetUV(pDest,
		(pThis->m_EnvMapTransform * pVert->m_Unk14 + pThis->m_Unk598 * pVert->m_Unk1c + pThis->m_Unk594 * pVert->m_Unk18) * pThis->m_Unk624 + pThis->m_Unk61c,
		(pThis->m_Unk5a8 * pVert->m_Unk1c + pThis->m_Unk5a0 * pVert->m_Unk14 + pThis->m_Unk5a4 * pVert->m_Unk18) * pThis->m_Unk628 + pThis->m_Unk620);
}

// guess: light/specular dot product coordinates from the vertex normal
// FUNCTION: D3DREN 0x10001490
void __fastcall FUN_10001490(UnkType_ModelDrawerVertexView *pThis, UnkType_ModelVertex *pSrc, TLVertex *pDest)
{
	UnkType_Vec3 vDir = pThis->m_DirLightDir;
	float fDot = vDir.x * pSrc->m_Unk14 + vDir.y * pSrc->m_Unk18 + vDir.z * pSrc->m_Unk1c;

	if (fDot > 0.0f)
		pDest->tu = pDest->tv = fDot * pThis->m_Unk630;
	else
		pDest->tu = pDest->tv = 0.0f;
}

// FUNCTION: D3DREN 0x10001510
void __fastcall FUN_10001510(TLVertex *pDest, TLVertex *pSrc)
{
	*pDest = *pSrc;
}

// FUNCTION: D3DREN 0x10001520
void __fastcall FUN_10001520(UnkType_TLVertex40 *pDest, UnkType_TLVertex40 *pSrc)
{
	*pDest = *pSrc;
}

// GLOBAL: D3DREN 0x10094de0
extern int DAT_10094de0[56];	// guess: Jupiter polyclip.h bInside[] of the near plane (static here)
// GLOBAL: D3DREN 0x10094ec0
extern int DAT_10094ec0[56];	// guess: bInside[] of the left plane

float FUN_10001a50(float *p1, float *p2, float *pOut);
int __fastcall FUN_10001b30(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts);
float FUN_10001ac0(float *p1, float *p2, float *pOut);
void FUN_10001940(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t);

// Plane clippers for the remaining planes (flag bits 8, 0x10, 0x20, 2); the first argument is unused by them.
int ClipPolyTop(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyRight(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyBottom(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyFar(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);

int FUN_10006e40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007100(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_100073b0(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007670(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
void FUN_10001f20(UnkType_TLVertex40 *pPrev, UnkType_TLVertex40 *pCur, UnkType_TLVertex40 *pOut, float t);
// GLOBAL: D3DREN 0x10094c20
extern int DAT_10094c20[56];	// guess: bInside[] of the near plane, 0x28-byte vertices
// GLOBAL: D3DREN 0x10094d00
extern int DAT_10094d00[56];	// guess: bInside[] of the left plane, 0x28-byte vertices

// guess: clips the polygon *ppVerts (*pnVerts vertices) against the planes selected by flags (1 near, 4 left, 8, 0x10,
// 0x20, 2); returns 0 when nothing is left.  The result replaces *ppVerts/*pnVerts (in the scratch buffer when clipped).
// Not matching: 62 of ~350 aligned instructions at best (permuter, 10k candidates).  Semantically complete (Jupiter
// polyclip.h expanded twice for the near and left plane, ClipLineZ/ClipExtra called out of line, then ClipPolyTop/10006670/
// 10006900/10006ba0 for flags 8/0x10/0x20/2).  The exe keeps flags and ppVerts in spill slots [esp+0x24]/[esp+0x34], pInside/
// nInside at [esp+0x28]/[esp+0x20] and the loop end pointer in esi; `mov eax,[g_CV]` is hoisted above the pushes.
// STUB: D3DREN 0x10001530
int __fastcall FUN_10001530(uint32 flags, TLVertex **ppVerts, int *pnVerts)
{
	TLVertex *pOut = g_pClipScratchVerts;
	if (g_CV_UseD3DClip.m_IntVal)	// guess: when set, only the near plane is clipped (flag bit 1)
	{
		flags &= 1;
		if (!flags)
			return 1;
	}

	TLVertex *pVerts = *ppVerts;
	int nVerts = *pnVerts;
	char bUnused0, bUnused1, bUnused2, bUnused3;
	float t;

	if (flags & 1)
	{
		int nInside = 0;
		int *pInside;
		TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
		int iPrev, iCur;

		DAT_1005668c++;
		pInside = DAT_10094de0;
		pCur = pVerts;
		pEnd = pCur + nVerts;
		while (pCur != pEnd)
		{
			*pInside = pCur->m_Vec.z >= g_ViewParams.m_NearZ;
			nInside += *pInside;
			++pInside;
			++pCur;
		}
		if (nInside == 0)
			return 0;
		else if (nInside != nVerts)
		{
			pOldOut = pOut;
			iPrev = nVerts - 1;
			pPrev = pVerts + iPrev;
			for (iCur = 0; iCur < nVerts; iCur++)
			{
				pCur = pVerts + iCur;
				if (DAT_10094de0[iPrev])
					*pOut++ = *pPrev;
				if (DAT_10094de0[iPrev] != DAT_10094de0[iCur])
				{
					t = FUN_10001a50(&pPrev->m_Vec.x, &pCur->m_Vec.x, &pOut->m_Vec.x);
					FUN_10001940(pPrev, pCur, pOut, t);
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

	if (flags & 4)
	{
		int nInside = 0;
		int *pInside;
		TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
		int iPrev, iCur;

		DAT_1005668c++;
		pInside = DAT_10094ec0;
		pCur = pVerts;
		pEnd = pCur + nVerts;
		while (pCur != pEnd)
		{
			*pInside = -pCur->m_Vec.z < pCur->m_Vec.x;
			nInside += *pInside;
			++pInside;
			++pCur;
		}
		if (nInside == 0)
			return 0;
		else if (nInside != nVerts)
		{
			pOldOut = pOut;
			iPrev = nVerts - 1;
			pPrev = pVerts + iPrev;
			for (iCur = 0; iCur < nVerts; iCur++)
			{
				pCur = pVerts + iCur;
				if (DAT_10094ec0[iPrev])
					*pOut++ = *pPrev;
				if (DAT_10094ec0[iPrev] != DAT_10094ec0[iCur])
				{
					t = FUN_10001ac0(&pPrev->m_Vec.x, &pCur->m_Vec.x, &pOut->m_Vec.x);
					FUN_10001940(pPrev, pCur, pOut, t);
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

	if ((flags & 8) && !ClipPolyTop(&bUnused0, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x10) && !ClipPolyRight(&bUnused1, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x20) && !ClipPolyBottom(&bUnused2, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 2) && !ClipPolyFar(&bUnused3, &pVerts, &nVerts, &pOut))
		return 0;

	*ppVerts = pVerts;
	*pnVerts = nVerts;
	return 1;
}

// guess: the 0x28-byte vertex twin of FUN_10001530 (same shape, see there): ClipExtra FUN_10001f20, planes FUN_10006e40/10007100/100073b0/10007670.
// STUB: D3DREN 0x10001b30
int __fastcall FUN_10001b30(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts)
{
	UnkType_TLVertex40 *pOut = (UnkType_TLVertex40 *)g_pClipScratchVerts;
	if (g_CV_UseD3DClip.m_IntVal)	// guess: when set, only the near plane is clipped (flag bit 1)
	{
		flags &= 1;
		if (!flags)
			return 1;
	}

	UnkType_TLVertex40 *pVerts = *ppVerts;
	int nVerts = *pnVerts;
	char bUnused0, bUnused1, bUnused2, bUnused3;
	float t;

	if (flags & 1)
	{
		int nInside = 0;
		int *pInside;
		UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
		int iPrev, iCur;

		DAT_1005668c++;
		pInside = DAT_10094c20;
		pCur = pVerts;
		pEnd = pCur + nVerts;
		while (pCur != pEnd)
		{
			*pInside = pCur->m_Vec.z >= g_ViewParams.m_NearZ;
			nInside += *pInside;
			++pInside;
			++pCur;
		}
		if (nInside == 0)
			return 0;
		else if (nInside != nVerts)
		{
			pOldOut = pOut;
			iPrev = nVerts - 1;
			pPrev = pVerts + iPrev;
			for (iCur = 0; iCur < nVerts; iCur++)
			{
				pCur = pVerts + iCur;
				if (DAT_10094c20[iPrev])
					*pOut++ = *pPrev;
				if (DAT_10094c20[iPrev] != DAT_10094c20[iCur])
				{
					t = FUN_10001a50(&pPrev->m_Vec.x, &pCur->m_Vec.x, &pOut->m_Vec.x);
					FUN_10001f20(pPrev, pCur, pOut, t);
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

	if (flags & 4)
	{
		int nInside = 0;
		int *pInside;
		UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
		int iPrev, iCur;

		DAT_1005668c++;
		pInside = DAT_10094d00;
		pCur = pVerts;
		pEnd = pCur + nVerts;
		while (pCur != pEnd)
		{
			*pInside = -pCur->m_Vec.z < pCur->m_Vec.x;
			nInside += *pInside;
			++pInside;
			++pCur;
		}
		if (nInside == 0)
			return 0;
		else if (nInside != nVerts)
		{
			pOldOut = pOut;
			iPrev = nVerts - 1;
			pPrev = pVerts + iPrev;
			for (iCur = 0; iCur < nVerts; iCur++)
			{
				pCur = pVerts + iCur;
				if (DAT_10094d00[iPrev])
					*pOut++ = *pPrev;
				if (DAT_10094d00[iPrev] != DAT_10094d00[iCur])
				{
					t = FUN_10001ac0(&pPrev->m_Vec.x, &pCur->m_Vec.x, &pOut->m_Vec.x);
					FUN_10001f20(pPrev, pCur, pOut, t);
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

	if ((flags & 8) && !FUN_10006e40(&bUnused0, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x10) && !FUN_10007100(&bUnused1, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x20) && !FUN_100073b0(&bUnused2, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 2) && !FUN_10007670(&bUnused3, &pVerts, &nVerts, &pOut))
		return 0;

	*ppVerts = pVerts;
	*pnVerts = nVerts;
	return 1;
}

// guess: Jupiter's TLVertex::ClipExtra (polyclip.h / clipline.h): interpolate everything but the position.
// FUNCTION: D3DREN 0x10001940
void FUN_10001940(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t)
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

// guess: intersection with the near plane z == g_ViewParams.m_NearZ; returns t, the parameter along p1->p2.
// FUNCTION: D3DREN 0x10001a50
float FUN_10001a50(float *p1, float *p2, float *pOut)
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
float FUN_10001ac0(float *p1, float *p2, float *pOut)
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
void FUN_10001f20(UnkType_TLVertex40 *pPrev, UnkType_TLVertex40 *pCur, UnkType_TLVertex40 *pOut, float t)
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
// guess: projects the camera-space vertex pSrc with the 4x4 matrix at g_ViewParams.m_DeviceTimesProjection.m[0][0] (perspective divide by the w row).
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence):
static inline void ProjectVertex(TLVertex *pDest, TLVertex *pSrc)
{
	float w = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[3][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	pDest->m_Vec.x = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * w;
	pDest->m_Vec.y = (g_ViewParams.m_DeviceTimesProjection.m[1][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[1][3]) * w;
	pDest->m_Vec.z = (g_ViewParams.m_DeviceTimesProjection.m[2][0] * pSrc->m_Vec.x + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pSrc->m_Vec.y + g_ViewParams.m_DeviceTimesProjection.m[2][2] * pSrc->m_Vec.z + g_ViewParams.m_DeviceTimesProjection.m[2][3]) * w;
	pDest->rhw = w;
}

// ---- the clipping callbacks (FUN_10002bc0 and its "really close" twin FUN_10002050) -----------------------------------------

// guess: 1/w projection of one camera-space vertex with the matrix at g_ViewParams.m_DeviceTimesProjection.m[0][0] (position only)
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence):
static inline void ProjectPos(float *pDest, float *pSrc)
{
	float w = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[3][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	float fY = g_ViewParams.m_DeviceTimesProjection.m[1][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[1][3];
	float fZ = g_ViewParams.m_DeviceTimesProjection.m[2][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[2][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[2][3];
	pDest[0] = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * w;
	pDest[1] = fY * w;
	pDest[3] = w;
	pDest[2] = fZ * w;
}

// The macros below write out code that the original had in macros or inline helpers; their names are mine (no evidence).
#define POOL_REFILL_END(P, E) \
	{ \
		UnkType_VertexBufferPool *pp = m_Unk608; \
		uint32 nFree2 = pp->m_Unk20 - pp->m_Unk1c; \
		(P) = (TLVertex *)pp->FUN_1003a8b1(); \
		(E) = (char *)pp->FUN_1003a8b1() + pp->vfn_Unk18() * (nFree2 - 1); \
	}

#define POOL_FLUSH_DRAW() \
	{ \
		UnkType_VertexBufferPool *pp = m_Unk608; \
		uint32 nBytes = (char *)pOut - (char *)pp->FUN_1003a8b1(); \
		uint32 nVertsOut = nBytes / pp->vfn_Unk18(); \
		DAT_1005626c += nVertsOut / 3; \
		((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nVertsOut); \
	}

// The body of the two clipping callbacks.  REALLYCLOSE adds the z bias g_CV_NearZ.m_Unk04 to the w of the z row (FUN_100062e0).
// Per triangle: the clip planes of g_ClipFlags are tested vertex by vertex (count of vertices inside: none = skip the
// triangle, not all = it has to be clipped), then the triangle is back face tested in 2D and projected.
#define CLIPPED_CALLBACK(REALLYCLOSE) \
	TLVertex *pOut = (TLVertex *)m_Unk608->FUN_1003a8b1(); \
	char *pEnd; \
	{ \
		UnkType_VertexBufferPool *pPool = m_Unk608; \
		uint32 nFree = pPool->m_Unk20 - pPool->m_Unk1c; \
		char *pBase = (char *)pPool->FUN_1003a8b1(); \
		pEnd = pBase + pPool->vfn_Unk18() * (nFree - 1); \
	} \
	ModelTri *pTri = pLOD->m_Tris.GetArray(); \
	int nTris = pLOD->m_Tris.GetSize(); \
	for (;;) \
	{ \
		if (nTris == 0) \
		{ \
			POOL_FLUSH_DRAW() \
			return 1; \
		} \
		float *pV0 = &pVerts[pTri->m_Indices[0]].m_Vec.x; \
		float *pV1 = &pVerts[pTri->m_Indices[1]].m_Vec.x; \
		float *pV2 = &pVerts[pTri->m_Indices[2]].m_Vec.x; \
		int nIn; \
		if ((g_ClipFlags & 4) == 0) \
			goto TestRight; \
		nIn = (-pV2[2] < pV2[0]) + (-pV1[2] < pV1[0]) + (-pV0[2] < pV0[0]); \
		if (nIn == 0) \
			goto Skip; \
		if (nIn != 3) \
			goto Clip; \
TestRight: \
		if (g_ClipFlags & 0x10) \
		{ \
			nIn = (pV2[0] < pV2[2]) + (pV1[0] < pV1[2]) + (pV0[0] < pV0[2]); \
			if (nIn == 0) \
				goto Skip; \
			if (nIn != 3) \
				goto Clip; \
		} \
		if (g_ClipFlags & 8) \
		{ \
			nIn = (pV2[1] < pV2[2]) + (pV1[1] < pV1[2]) + (pV0[1] < pV0[2]); \
			if (nIn == 0) \
				goto Skip; \
			if (nIn != 3) \
				goto Clip; \
		} \
		if (g_ClipFlags & 0x20) \
		{ \
			nIn = (-pV2[2] < pV2[1]) + (-pV1[2] < pV1[1]) + (-pV0[2] < pV0[1]); \
			if (nIn == 0) \
				goto Skip; \
			if (nIn != 3) \
				goto Clip; \
		} \
		if (g_ClipFlags & 1) \
		{ \
			nIn = (g_ViewParams.m_NearZ <= pV2[2]) + (g_ViewParams.m_NearZ <= pV1[2]) + (g_ViewParams.m_NearZ <= pV0[2]); \
			if (nIn == 0) \
				goto Skip; \
			if (nIn != 3) \
				goto Clip; \
		} \
		if (g_ClipFlags & 2) \
		{ \
			nIn = (pV2[2] <= g_ViewParams.m_Unk90) + (pV1[2] <= g_ViewParams.m_Unk90) + (pV0[2] <= g_ViewParams.m_Unk90); \
			if (nIn == 0) \
				goto Skip; \
			if (nIn != 3) \
				goto Clip; \
		} \
		{ \
			float fX0 = (1.0f / pV0[2]) * pV0[0]; \
			float fY0 = (1.0f / pV0[2]) * pV0[1]; \
			float fCross = ((1.0f / pV2[2]) * pV2[0] - fX0) * ((1.0f / pV1[2]) * pV1[1] - fY0) \
				- ((1.0f / pV2[2]) * pV2[1] - fY0) * ((1.0f / pV1[2]) * pV1[0] - fX0); \
			if (g_ViewParams.m_Unk4cc) \
				fCross = -fCross; \
			if (0.0f < fCross) \
			{ \
				float *apV[3] = { pV0, pV1, pV2 }; \
				for (int k = 0; k < 3; k++) \
				{ \
					if (REALLYCLOSE) \
						FUN_100062e0(&pOut->m_Vec.x, apV[k], g_CV_NearZ.m_Unk04); \
					else \
						ProjectPos(&pOut->m_Vec.x, apV[k]); \
					pOut->color = ((TLVertex *)apV[k])->color; \
					pOut->specular = m_Unk640; \
					m_Unk5f4(pOut, apV[k], &pTri->m_UVs[k].tu); \
					pOut = (TLVertex *)((char *)pOut + m_Unk5f8); \
				} \
				if ((char *)pOut > pEnd && nTris > 1) \
				{ \
					DAT_1005626c += (m_Unk608->m_Unk20 - m_Unk608->m_Unk1c) / 3; \
					((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, m_Unk608->m_Unk20 - m_Unk608->m_Unk1c); \
					POOL_REFILL_END(pOut, pEnd) \
				} \
			} \
		} \
		goto Skip; \
Clip: \
		{ \
			/* the triangle is built in a local vertex array (stride m_Unk5f8) and clipped through m_Unk600 */ \
			char aBuf[0x500]; \
			char *pPoly = aBuf; \
			float *apV[3] = { pV0, pV1, pV2 }; \
			for (int k = 0; k < 3; k++) \
			{ \
				float *pD = (float *)(pPoly + k * m_Unk5f8); \
				pD[0] = apV[k][0]; \
				pD[1] = apV[k][1]; \
				pD[2] = apV[k][2]; \
				pD[4] = apV[k][4]; \
				pD[5] = *(float *)&m_Unk640; \
				m_Unk5f4(pD, apV[k], &pTri->m_UVs[k].tu); \
			} \
			int nPoly = 3; \
			if (m_Unk600(g_ClipFlags, (void **)&pPoly, &nPoly)) \
			{ \
				int nStride = m_Unk5f8; \
				float fX0 = (1.0f / ((float *)pPoly)[2]) * ((float *)pPoly)[0]; \
				float fY0 = (1.0f / ((float *)pPoly)[2]) * ((float *)pPoly)[1]; \
				float *pP1 = (float *)(pPoly + nStride); \
				float *pP2 = (float *)(pPoly + nStride * 2); \
				float fCross = ((1.0f / pP2[2]) * pP2[0] - fX0) * ((1.0f / pP1[2]) * pP1[1] - fY0) \
					- ((1.0f / pP2[2]) * pP2[1] - fY0) * ((1.0f / pP1[2]) * pP1[0] - fX0); \
				if (g_ViewParams.m_Unk4cc) \
					fCross = -fCross; \
				if (0.0f < fCross) \
				{ \
					char *pPolyEnd = pPoly + nStride * nPoly; \
					for (char *pP = pPoly; pP < pPolyEnd; pP += m_Unk5f8) \
					{ \
						float vSrc[3] = { ((float *)pP)[0], ((float *)pP)[1], ((float *)pP)[2] }; \
						if (REALLYCLOSE) \
							FUN_100062e0((float *)pP, vSrc, g_CV_NearZ.m_Unk04); \
						else \
							ProjectPos((float *)pP, vSrc); \
					} \
					if ((char *)pOut + (nPoly * 3 - 6) * m_Unk5f8 > pEnd) \
					{ \
						POOL_FLUSH_DRAW() \
						m_Unk608->FUN_1003a7f7(); \
						POOL_REFILL_END(pOut, pEnd) \
					} \
					for (int iFan = 1; iFan < nPoly - 1; iFan++) \
					{ \
						m_Unk5fc(pOut, pPoly); \
						char *pNext = (char *)pOut + m_Unk5f8; \
						m_Unk5fc(pNext, pPoly + iFan * m_Unk5f8); \
						pNext += m_Unk5f8; \
						m_Unk5fc(pNext, pPoly + (iFan + 1) * m_Unk5f8); \
						pOut = (TLVertex *)(pNext + m_Unk5f8); \
					} \
				} \
			} \
		} \
Skip: \
		pTri++; \
		nTris--; \
	}

void FUN_100062e0(float *pDest, float *pSrc, float fZBias);


// guess: the "really close" variant (instances with FLAG_REALLYCLOSE): the z row uses z + g_CV_NearZ (FUN_100062e0).
// Not matching yet: semantically complete (written from the Ghidra C), 2928 bytes in the exe.
// STUB: D3DREN 0x10002050
int ModelDraw::FUN_10002050(PieceLOD *pLOD, TLVertex *pVerts)
{
	CLIPPED_CALLBACK(1)
}

// guess: the callback for pieces that cross a clip plane (m_Unk3c4[i]): per-triangle plane tests, clipping of the triangles
// that cross a plane through m_Unk600, software projection and culling.
// Not matching yet: semantically complete (written from the Ghidra C), 2832 bytes in the exe.
// STUB: D3DREN 0x10002bc0
int ModelDraw::FUN_10002bc0(PieceLOD *pLOD, TLVertex *pVerts)
{
	CLIPPED_CALLBACK(0)
}

// guess: the callback for pieces that need no clipping: projects the vertices in software (matrix g_ViewParams.m_DeviceTimesProjection.m[0][0]) and draws.
// STUB: D3DREN 0x100036d0
int ModelDraw::FUN_100036d0(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->FUN_1003a8b1();
	UnkType_VertexBufferPool *pPool = m_Unk608;
	uint32 nFree = pPool->m_Unk20 - pPool->m_Unk1c;
	char *pBase = (char *)pPool->FUN_1003a8b1();
	char *pEnd = pBase + pPool->vfn_Unk18() * (nFree - 1);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	for (;;)
	{
		if (nTris == 0)
		{
			UnkType_VertexBufferPool *p = m_Unk608;
			uint32 nBytes = (char *)pOut - (char *)p->FUN_1003a8b1();
			uint32 nVerts = nBytes / p->vfn_Unk18();
			DAT_1005626c += nVerts / 3;
			((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nVerts);
			return 1;
		}
		TLVertex *pV0 = &pVerts[pTri->m_Indices[0]];
		TLVertex *pV1 = &pVerts[pTri->m_Indices[1]];
		TLVertex *pV2 = &pVerts[pTri->m_Indices[2]];
		if (m_Unk8b4)
		{
			float fX0 = (1.0f / pV0->m_Vec.z) * pV0->m_Vec.x;
			float fY0 = (1.0f / pV0->m_Vec.z) * pV0->m_Vec.y;
			float fCross = ((1.0f / pV2->m_Vec.z) * pV2->m_Vec.x - fX0) * ((1.0f / pV1->m_Vec.z) * pV1->m_Vec.y - fY0)
				- ((1.0f / pV2->m_Vec.z) * pV2->m_Vec.y - fY0) * ((1.0f / pV1->m_Vec.z) * pV1->m_Vec.x - fX0);
			if (g_ViewParams.m_bCullFlip)
				fCross = -fCross;
			if (!(0.0f < fCross))
				goto Skip;
		}
		ProjectVertex(pOut, pV0);
		pOut->color = pV0->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV0, &pTri->m_UVs[0].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		ProjectVertex(pOut, pV1);
		pOut->color = pV1->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV1, &pTri->m_UVs[1].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		ProjectVertex(pOut, pV2);
		pOut->color = pV2->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV2, &pTri->m_UVs[2].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		if ((char *)pOut > pEnd && nTris > 1)
		{
			DAT_1005626c += (m_Unk608->m_Unk20 - m_Unk608->m_Unk1c) / 3;
			((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, m_Unk608->m_Unk20 - m_Unk608->m_Unk1c);
			pOut = (TLVertex *)m_Unk608->FUN_1003a8b1();
			UnkType_VertexBufferPool *p = m_Unk608;
			uint32 nFree2 = p->m_Unk20 - p->m_Unk1c;
			pEnd = (char *)p->FUN_1003a8b1() + p->vfn_Unk18() * (nFree2 - 1);
		}
Skip:
		pTri++;
		nTris--;
	}
}

// guess: the default draw callback: copies the transformed vertices of each (front facing) triangle into the pool
// STUB: D3DREN 0x10003b60
int ModelDraw::FUN_10003b60(PieceLOD *pLOD, TLVertex *pVerts)
{
	TLVertex *pOut = (TLVertex *)m_Unk608->FUN_1003a8b1();
	UnkType_VertexBufferPool *pPool = m_Unk608;
	uint32 nFree = pPool->m_Unk20 - pPool->m_Unk1c;
	char *pBase = (char *)pPool->FUN_1003a8b1();
	char *pEnd = pBase + pPool->vfn_Unk18() * (nFree - 1);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	for (;;)
	{
		if (nTris == 0)
		{
			UnkType_VertexBufferPool *p = m_Unk608;
			uint32 nBytes = (char *)pOut - (char *)p->FUN_1003a8b1();
			uint32 nVerts = nBytes / p->vfn_Unk18();
			DAT_1005626c += nVerts / 3;
			((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nVerts);
			return 1;
		}
		TLVertex *pV1 = &pVerts[pTri->m_Indices[1]];
		TLVertex *pV0 = &pVerts[pTri->m_Indices[0]];
		TLVertex *pV2 = &pVerts[pTri->m_Indices[2]];
		if (m_Unk8b4)
		{
			float fCross = (pV1->m_Vec.x - pV0->m_Vec.x) * (pV2->m_Vec.y - pV0->m_Vec.y)
				- (pV2->m_Vec.x - pV0->m_Vec.x) * (pV1->m_Vec.y - pV0->m_Vec.y);
			if (g_ViewParams.m_bCullFlip)
				fCross = -fCross;
			if (!(fCross > g_CV_ModelMinTri.m_FloatVal))
				goto Skip;
		}
		pOut->m_Vec = pV0->m_Vec;
		pOut->rhw = pV0->rhw;
		pOut->color = pV0->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV0, &pTri->m_UVs[0].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		pOut->m_Vec = pV1->m_Vec;
		pOut->rhw = pV1->rhw;
		pOut->color = pV1->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV1, &pTri->m_UVs[1].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		pOut->m_Vec = pV2->m_Vec;
		pOut->rhw = pV2->rhw;
		pOut->color = pV2->color;
		pOut->specular = m_Unk640;
		m_Unk5f4(pOut, pV2, &pTri->m_UVs[2].tu);
		pOut = (TLVertex *)((char *)pOut + m_Unk5f8);
		if ((char *)pOut > pEnd && nTris > 1)
		{
			DAT_1005626c += (m_Unk608->m_Unk20 - m_Unk608->m_Unk1c) / 3;
			((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, m_Unk608->m_Unk20 - m_Unk608->m_Unk1c);
			pOut = (TLVertex *)m_Unk608->FUN_1003a8b1();
			UnkType_VertexBufferPool *p = m_Unk608;
			uint32 nFree2 = p->m_Unk20 - p->m_Unk1c;
			pEnd = (char *)p->FUN_1003a8b1() + p->vfn_Unk18() * (nFree2 - 1);
		}
Skip:
		pTri++;
		nTris--;
	}
}

// The TnL vertex of the hardware T&L pool: position, diffuse, specular, one texture coordinate pair (0x1c bytes).  The texture
// coordinate fillers write at +0x18/+0x1c of a TLVertex, so they are handed a pointer 4 bytes before the vertex.
struct UnkType_TnLVertex
{
	LTVector	m_Vec;		// 0x00
	uint32		color;		// 0x0c
	uint32		specular;	// 0x10
	float		tu, tv;		// 0x14
};

// guess: the untransformed variant of FUN_10003b60: writes XYZ + diffuse + specular vertices (stride m_Unk5f8 - 4) so that
// Direct3D transforms them; the back face test (m_Unk8b4) is done on the model-space x/y of the vertices.
// STUB: D3DREN 0x10003e00
int ModelDraw::FUN_10003e00(PieceLOD *pLOD, TLVertex *pVerts)
{
	UnkType_TnLVertex *pOut = (UnkType_TnLVertex *)m_Unk608->FUN_1003a8b1();
	UnkType_VertexBufferPool *pPool = m_Unk608;
	uint32 nFree = pPool->m_Unk20 - pPool->m_Unk1c;
	char *pBase = (char *)pPool->FUN_1003a8b1();
	char *pEnd = pBase + pPool->vfn_Unk18() * (nFree - 1);
	ModelTri *pTri = pLOD->m_Tris.GetArray();
	int nTris = pLOD->m_Tris.GetSize();
	if (m_Unk8b4)
	{
		for (; nTris != 0; nTris--)
		{
			TLVertex *pV0 = &pVerts[pTri->m_Indices[0]];
			TLVertex *pV1 = &pVerts[pTri->m_Indices[1]];
			TLVertex *pV2 = &pVerts[pTri->m_Indices[2]];
			float fCross = (pV1->m_Vec.x - pV0->m_Vec.x) * (pV2->m_Vec.y - pV0->m_Vec.y)
				- (pV2->m_Vec.x - pV0->m_Vec.x) * (pV1->m_Vec.y - pV0->m_Vec.y);
			if (g_ViewParams.m_bCullFlip)
				fCross = -fCross;
			if (fCross > g_CV_ModelMinTri.m_FloatVal)
			{
				TLVertex *apV[3] = { pV0, pV1, pV2 };
				for (int k = 0; k < 3; k++)
				{
					pOut->m_Vec = apV[k]->m_Vec;
					pOut->color = apV[k]->color;
					pOut->specular = m_Unk640;
					m_Unk5f4((char *)pOut - 4, apV[k], &pTri->m_UVs[k].tu);
					pOut = (UnkType_TnLVertex *)((char *)pOut - 4 + m_Unk5f8);
				}
				if ((char *)pOut > pEnd && nTris > 1)
				{
					DAT_1005626c += (m_Unk608->m_Unk20 - m_Unk608->m_Unk1c) / 3;
					((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, m_Unk608->m_Unk20 - m_Unk608->m_Unk1c);
					pOut = (UnkType_TnLVertex *)m_Unk608->FUN_1003a8b1();
					UnkType_VertexBufferPool *p = m_Unk608;
					uint32 nFree2 = p->m_Unk20 - p->m_Unk1c;
					pEnd = (char *)p->FUN_1003a8b1() + p->vfn_Unk18() * (nFree2 - 1);
				}
			}
			pTri++;
		}
	}
	else
	{
		for (; nTris != 0; nTris--)
		{
			TLVertex *apV[3] = { &pVerts[pTri->m_Indices[0]], &pVerts[pTri->m_Indices[1]], &pVerts[pTri->m_Indices[2]] };
			for (int k = 0; k < 3; k++)
			{
				pOut->m_Vec = apV[k]->m_Vec;
				pOut->color = apV[k]->color;
				pOut->specular = m_Unk640;
				m_Unk5f4((char *)pOut - 4, apV[k], &pTri->m_UVs[k].tu);
				pOut = (UnkType_TnLVertex *)((char *)pOut - 4 + m_Unk5f8);
			}
			if ((char *)pOut > pEnd && nTris > 1)
			{
				DAT_1005626c += (m_Unk608->m_Unk20 - m_Unk608->m_Unk1c) / 3;
				((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, m_Unk608->m_Unk20 - m_Unk608->m_Unk1c);
				pOut = (UnkType_TnLVertex *)m_Unk608->FUN_1003a8b1();
				UnkType_VertexBufferPool *p = m_Unk608;
				uint32 nFree2 = p->m_Unk20 - p->m_Unk1c;
				pEnd = (char *)p->FUN_1003a8b1() + p->vfn_Unk18() * (nFree2 - 1);
			}
			pTri++;
		}
	}
	UnkType_VertexBufferPool *p = m_Unk608;
	uint32 nBytes = (char *)pOut - (char *)p->FUN_1003a8b1();
	uint32 nVerts = nBytes / p->vfn_Unk18();
	DAT_1005626c += nVerts / 3;
	((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nVerts);
	return 1;
}

// FUNCTION: D3DREN 0x10004230
int ModelDraw::FUN_10004230(PieceLOD *pLOD, TLVertex *pVerts)
{
	uint32 nIndices = pLOD->m_Tris.GetSize() * 3;
	DAT_1005626c += nIndices / 3;
	((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nIndices);
	return 1;
}

void d3d_BuildSpecularLookupTexture(float fSpecularPower);	// unit d3d_texture (W8): rebuilds the specular lookup table texture
void d3d_BuildSpecularLookupTexture(float fSpecularPower);	// unit d3d_texture (W8): rebuilds the specular lookup table texture

// guess: runs the draw callbacks over the pieces of the model: picks the vertex format / pool, binds each piece's skin and
// calls pfnDrawA for the pieces that need no clipping and pfnDrawB for the others.
// Not matching: 331/816 bytes, 98 aligned mismatches, same blocks and calls (audit-level equal).  Remaining differences are register
// allocation and statement scheduling: the exe keeps a zero in no register (our `if (m_Unk8ac)` forms use test), loads
// m_pModel/m_Unk82c earlier and schedules the `m_Unk4cc = -1` store before the piece loop set-up; the LOD choice
// (ModelPiece::GetLOD inline) and the loop counter/slot assignment differ.  tools/permute.py cannot run on this unit file
// (member function in a large file); not yet tried on an extracted copy.
// STUB: D3DREN 0x10004270
void ModelDraw::FUN_10004270(PFN_DrawPiece pfnDrawA, PFN_DrawPiece pfnDrawB, int a3)
{
	m_Unk5f0 = a3;
	if (m_Unk5e8 == 1)
	{
		m_Unk5f8 = 0x20;
		m_Unk5fc = (PFN_CopyVertex)FUN_10001510;
		m_Unk600 = (PFN_ClipPolygon)FUN_10001530;
		m_Unk604 = 0x1c4;
		if (m_Unk8b0)
			m_Unk608 = &DAT_1004eb48;
		else
			m_Unk608 = &DAT_1004d620;
	}
	else
	{
		m_Unk5f8 = 0x28;
		m_Unk5fc = (PFN_CopyVertex)FUN_10001520;
		m_Unk600 = (PFN_ClipPolygon)FUN_10001b30;
		m_Unk604 = 0x2c4;
		if (m_Unk8b0)
			m_Unk608 = &DAT_1004dae0;
		else
			m_Unk608 = &DAT_1004da80;
	}
	if (m_Unk608->m_Unk20 != g_CV_ModelVBSize.m_IntVal || m_Unk608->m_Unk24 != g_CV_ModelVBCount.m_IntVal)
	{
		DAT_1004d620.FUN_1003a805();
		DAT_1004da80.FUN_1003a805();
		UnkType_VertexBufferPool *pCache = &UnkType_ModelVBCacheHolder::DAT_10093b10;
		pCache->FUN_1003a805();
		FUN_10001230();
	}
	if (m_Unk8b0)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
	if (m_Unk8ac)
	{
		m_Unk608 = &UnkType_ModelVBCacheHolder::DAT_10093b10;
		UnkType_ModelVBCacheHolder::DAT_10093b10.m_Unk58 = (m_Unk604 & ~D3DFVF_XYZRHW) | D3DFVF_XYZ;
		UnkType_ModelVBCacheHolder::DAT_10093b10.m_Unk54 = m_Unk5f8 - 4;
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
		if (pLOD && !(m_pInstance->m_HiddenPieces & (1 << (i & 31))) && !m_Unk2c4[i])
		{
			if (pPiece->m_TextureIndex != m_Unk4cc)
			{
				FUN_10024589(pPiece->m_TextureIndex);
				FUN_100244b3((uint32 *)&m_Unk5f0);
				SharedTexture *pSkin = m_pInstance->m_pSkins[pPiece->m_TextureIndex];
				if (pSkin && pSkin->m_pStateChange)
				{
					DAT_10063c90.FUN_10021da6();
					DAT_10063c90.FUN_10021db7(pSkin->m_pStateChange, m_Unk34);
				}
			}
			m_Unk62c = pPiece->m_SpecularPower;
			m_Unk630 = pPiece->m_SpecularScale;
			if (g_CV_SpecularPowerTest.m_FloatVal != 0.0f)
				m_Unk62c = g_CV_SpecularPowerTest.m_FloatVal;
			if (g_CV_SpecularScaleTest.m_FloatVal != 0.0f)
				m_Unk630 = g_CV_SpecularScaleTest.m_FloatVal;
			if (m_Unk630 != 0.0f && m_Unk5ec == (PFN_GenTexCoords)FUN_10001490)
				d3d_BuildSpecularLookupTexture(m_Unk62c);
				d3d_BuildSpecularLookupTexture(m_Unk62c);
			if (!m_Unk3c4[i])
				(this->*pfnDrawA)(pLOD, pVerts);
			else
				(this->*pfnDrawB)(pLOD, pVerts);
			pVerts += pLOD->m_Verts.GetSize();
		}
	}
	if (m_Unk8b0)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
	DAT_10063c90.FUN_10021da6();
}

// Note: the exe loads the second member pointer into edx first: `pfnB = pfnA = X;` (assignment order) gives that register order
// FUNCTION: D3DREN 0x100045a0
void ModelDraw::FUN_100045a0(int a1)
{
	PFN_DrawPiece pfnA, pfnB;
	if (m_Unk8ac == 2)
	{
		pfnB = pfnA = &ModelDraw::FUN_10004230;
	}
	else if (m_Unk8ac == 1 || m_Unk8b0)
	{
		pfnB = pfnA = &ModelDraw::FUN_10003e00;
	}
	else if (m_pInstance->m_Flags & 0x40)
	{
		pfnB = pfnA = &ModelDraw::FUN_10002050;
	}
	else if (g_ClipFlags & 0x3f)
	{
		pfnA = &ModelDraw::FUN_100036d0;
		pfnB = &ModelDraw::FUN_10002bc0;
	}
	else
	{
		pfnB = pfnA = &ModelDraw::FUN_10003b60;
	}
	FUN_10004270(pfnA, pfnB, a1);
}

// ---- the skinning / lighting / projection of one piece (FUN_10004660) and its driver (FUN_10005700) ----------------------

// guess: accumulates the bone-transformed offsets of one model vertex into pOut (x, y, z, w; pOut is cleared first)
// helper written for this decompilation (not a symbol of d3d.ren: the exe has the code inlined; the name is mine, no evidence):
static inline void SkinVertexInto(ModelVert *pVert, LTMatrix *pTransforms, float *pOut)
{
	NewVertexWeight *pW = pVert->m_Weights;
	for (uint32 n = pVert->m_nWeights; n != 0; n--)
	{
		float *pM = (float *)((char *)pTransforms + pW->m_iNode * 0x40);
		pOut[0] = pM[0] * pW->m_Vec[0] + pM[1] * pW->m_Vec[1] + pM[2] * pW->m_Vec[2] + pM[3] * pW->m_Vec[3] + pOut[0];
		pOut[1] = pM[4] * pW->m_Vec[0] + pM[5] * pW->m_Vec[1] + pM[6] * pW->m_Vec[2] + pM[7] * pW->m_Vec[3] + pOut[1];
		pOut[2] = pM[8] * pW->m_Vec[0] + pM[9] * pW->m_Vec[1] + pM[10] * pW->m_Vec[2] + pM[11] * pW->m_Vec[3] + pOut[2];
		pOut[3] = pM[12] * pW->m_Vec[0] + pM[13] * pW->m_Vec[1] + pM[14] * pW->m_Vec[2] + pM[15] * pW->m_Vec[3] + pOut[3];
		pW++;
	}
}

// One loop of FUN_10004660: the exe has four copies of it (bounds on/off x LOD blend on/off); BOUNDS and BLEND are constants.
#define MODELVERT_LOOP(BOUNDS, BLEND) 	for (; nVerts != 0; nVerts--, pVert++, pDest++) 	{ 		pDest->m_Vec.x = 0.0f; 		pDest->m_Vec.y = 0.0f; 		pDest->m_Vec.z = 0.0f; 		pDest->rhw = 0.0f; 		SkinVertexInto(pVert, pTransforms, &pDest->m_Vec.x); 		if (BLEND) 		{ 			ModelVert *pVertB = &pLOD2->m_Verts.GetArray()[pVert->m_iReplacement]; 			float vb[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; 			SkinVertexInto(pVertB, pTransforms, vb); 			pDest->m_Vec.x = (vb[0] - pDest->m_Vec.x) * m_fLODBlend + pDest->m_Vec.x; 			pDest->m_Vec.y = (vb[1] - pDest->m_Vec.y) * m_fLODBlend + pDest->m_Vec.y; 			pDest->m_Vec.z = (vb[2] - pDest->m_Vec.z) * m_fLODBlend + pDest->m_Vec.z; 			pDest->rhw = 1.0f / ((vb[3] - pDest->rhw) * m_fLODBlend + pDest->rhw); 		} 		else 			pDest->rhw = 1.0f / pDest->rhw; 		pDest->m_Vec.x = pDest->rhw * pDest->m_Vec.x; 		pDest->m_Vec.y = pDest->rhw * pDest->m_Vec.y; 		pDest->m_Vec.z = pDest->rhw * pDest->m_Vec.z; 		if (BOUNDS) 		{ 			if (pMin[0] <= pDest->m_Vec.x) { if (pMax[0] < pDest->m_Vec.x) pMax[0] = pDest->m_Vec.x; } else pMin[0] = pDest->m_Vec.x; 			if (pMin[1] <= pDest->m_Vec.y) { if (pMax[1] < pDest->m_Vec.y) pMax[1] = pDest->m_Vec.y; } else pMin[1] = pDest->m_Vec.y; 			if (pMin[2] <= pDest->m_Vec.z) { if (pMax[2] < pDest->m_Vec.z) pMax[2] = pDest->m_Vec.z; } else pMin[2] = pDest->m_Vec.z; 		} 		float fDot = fDx * pVert->m_Normal.x + fDy * pVert->m_Normal.y + fDz * pVert->m_Normal.z; 		float fR = fBaseR, fG = fBaseG, fB = fBaseB; 		if (0.0f < fDot) 		{ 			fR = (fLitR - fBaseR) * fDot + fBaseR; 			fG = (fLitG - fBaseG) * fDot + fBaseG; 			fB = (fLitB - fBaseB) * fDot + fBaseB; 		} 		UnkType_ModelLight *pLight = m_Unk3c; 		UnkType_ModelLight *pLightEnd = m_Unk3c + m_nModelLights; 		for (; pLight != pLightEnd; pLight++) 		{ 			float fLd = pVert->m_Normal.x * pLight->m_Unk10.x + pVert->m_Normal.y * pLight->m_Unk10.y + pVert->m_Normal.z * pLight->m_Unk10.z; 			if (0.0f < fLd) 			{ 				float fDist = (pVert->m_Vec.y - pLight->m_Unk00.y) * (pVert->m_Vec.y - pLight->m_Unk00.y) 					+ (pVert->m_Vec.z - pLight->m_Unk00.z) * (pVert->m_Vec.z - pLight->m_Unk00.z) 					+ (pVert->m_Vec.x - pLight->m_Unk00.x) * (pVert->m_Vec.x - pLight->m_Unk00.x); 				if (fDist < pLight->m_Unk0c) 				{ 					fLd = (pLight->m_Unk0c - fDist) * fLd; 					fR = fLd * pLight->m_Unk1c.x + fR; 					fG = fLd * pLight->m_Unk1c.y + fG; 					fB = fLd * pLight->m_Unk1c.z + fB; 				} 			} 		} 		if (255.0f < fR) 			fR = 255.0f; 		if (255.0f < fG) 			fG = 255.0f; 		if (255.0f < fB) 			fB = 255.0f; 		pLighting[0] = fR + pLighting[0]; 		pLighting[1] = fG + pLighting[1]; 		pLighting[2] = fB + pLighting[2]; 		pDest->rgb.r = (uint8)RoundFloatToInt(fR); 		pDest->rgb.g = (uint8)RoundFloatToInt(fG); 		pDest->rgb.b = (uint8)RoundFloatToInt(fB); 		pDest->rgb.a = m_Unk8a8; 		pfn(pDest); 	}

// guess: skins, lights and projects the vertices of one piece into pDest (TL vertices), calls the per-vertex generator,
// accumulates the vertex colours into pLighting and (bBounds) the min/max of the projected positions into pMin/pMax.
// With the LOD blend enabled (m_bLODBlend) every vertex is the m_fLODBlend blend of the vertex of pLOD and its replacement in pLOD2.
// Not matching: semantically complete, four loop copies as in the exe; the exe's loop body differs in x87 term order and register
// allocation (not yet worked on), the function was written from the Ghidra C only.
// STUB: D3DREN 0x10004660
void ModelDraw::FUN_10004660(PieceLOD *pLOD, PieceLOD *pLOD2, TLVertex *pDest, void *pfnPerVertex, LTMatrix *pTransforms,
	float *pLighting, char bBounds, float *pMin, float *pMax)
{
	void (__fastcall *pfn)(TLVertex *) = (void (__fastcall *)(TLVertex *))pfnPerVertex;
	float fScale = m_DirLightAmount;
	float fDx = fScale * m_DirLightDir.x;
	float fDy = fScale * m_DirLightDir.y;
	float fDz = fScale * m_DirLightDir.z;
	float fBaseR = m_LightAdd.x + m_ObjectColor.x * m_AmbientLight.x;
	float fBaseG = m_ObjectColor.y * m_AmbientLight.y + m_LightAdd.y;
	float fBaseB = m_ObjectColor.z * m_AmbientLight.z + m_LightAdd.z;
	float fLitR = m_LightAdd.x + m_ObjectColor.x * m_DirLightColor.x;
	float fLitG = m_ObjectColor.y * m_DirLightColor.y + m_LightAdd.y;
	float fLitB = m_ObjectColor.z * m_DirLightColor.z + m_LightAdd.z;
	ModelVert *pVert = pLOD->m_Verts.GetArray();
	int nVerts = pLOD->m_Verts.GetSize();

	if (bBounds == 0)
	{
		if (m_bLODBlend == 0)
		{
			MODELVERT_LOOP(0, 0)
		}
		else
		{
			MODELVERT_LOOP(0, 1)
		}
	}
	else
	{
		pMin[0] = 100000.0f;
		pMin[1] = 100000.0f;
		pMin[2] = 100000.0f;
		pMax[0] = -100000.0f;
		pMax[1] = -100000.0f;
		pMax[2] = -100000.0f;
		if (m_bLODBlend == 0)
		{
			MODELVERT_LOOP(1, 0)
		}
		else
		{
			MODELVERT_LOOP(1, 1)
		}
	}
}

// guess: per piece of the model: picks the LOD(s), skins/lights/projects the piece into m_Unk82c and, when clipping is on,
// tests the 8 corners of the projected bounding box against the clip planes: m_Unk2c4[i] = completely outside (skip the piece),
// m_Unk3c4[i] = crosses a plane (needs the clipping callback).  The mean vertex light is stored in the instance (m_ModelLighting).
// STUB: D3DREN 0x10005700
void ModelDraw::FUN_10005700()
{
	LTMatrix *pTransforms;
	if (g_ClipFlags == 0)
		pTransforms = m_Unk840;
	else
		pTransforms = m_pModel->m_Transforms.GetArray();
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
			{
				pLODB = pPiece;
				if (m_nLOD != 0xffffffff)
				{
					if (m_nLOD < pPiece->m_LODs.GetSize())
						pLODB = &pPiece->m_LODs[m_nLOD];
					else
						pLODB = 0;
				}
			}
			if (!(m_pInstance->m_HiddenPieces & (1 << (i & 31))))
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
				float vMin[3], vMax[3];
				FUN_10004660(pLOD, pLODB, m_Unk82c + iVertBase, m_Unk5ec, pTransforms, &m_pInstance->m_ModelLighting.x, bBounds, vMin, vMax);
				if (bBounds)
				{
					// the 8 corners of the projected bounding box against the six clip planes
					int nNear = 0, nFar = 0, nLeft = 0, nRight = 0, nTop = 0, nBottom = 0;
					for (int c = 0; c < 8; c++)
					{
						float x = (c & 1) ? vMax[0] : vMin[0];
						float y = (c & 2) ? vMax[1] : vMin[1];
						float z = (c & 4) ? vMax[2] : vMin[2];
						nNear += g_ViewParams.m_NearZ <= z;
						nFar += z <= g_ViewParams.m_ClipFarZ;
						nLeft += -z < x;
						nRight += x < z;
						nTop += y < z;
						nBottom += -z < y;
					}
					int aCount[6] = { nNear, nFar, nLeft, nRight, nTop, nBottom };
					for (int k = 0; k < 6; k++)
					{
						if (aCount[k] == 0)
							m_Unk2c4[i] = 1;
						else if (aCount[k] < 8)
							m_Unk3c4[i] = 1;
					}
				}
				if (m_Unk2c4[i] == 0)
				{
					iVertBase += pLOD->m_Verts.GetSize();
					nTotalVerts += pLOD->m_Verts.GetSize();
				}
			}
		}
	}
	if (nTotalVerts != 0)
	{
		float fCount = (float)nTotalVerts;
		m_pInstance->m_ModelLighting.x = m_pInstance->m_ModelLighting.x / fCount;
		m_pInstance->m_ModelLighting.y = m_pInstance->m_ModelLighting.y / fCount;
		m_pInstance->m_ModelLighting.z = m_pInstance->m_ModelLighting.z / fCount;
	}
}

// The in-class inline virtuals (defined in vbpool.h), the scalar deleting destructor and the buffer creation virtuals.
// The four classes have identical members; the linker folded them, so only one instance of each is annotated:
// FUNCTION: D3DREN 0x10006140 ?vfn_Unk18@?$UnkType_VertexBufferPoolA@$0A@@@UAEHXZ
// FUNCTION: D3DREN 0x10006150 ?vfn_Unk18@?$UnkType_VertexBufferPoolB@$0A@@@UAEHXZ
// FUNCTION: D3DREN 0x10006160 ??_G?$UnkType_VertexBufferPoolA@$0A@@@UAEPAXI@Z
// FUNCTION: D3DREN 0x10006180 ?vfn_Unk1c@?$UnkType_VertexBufferPoolA@$0A@@@UAEHPAUIDirect3D7@@@Z
// FUNCTION: D3DREN 0x10006230 ?vfn_Unk1c@?$UnkType_VertexBufferPoolB@$0A@@@UAEHPAUIDirect3D7@@@Z

template <int N> int UnkType_VertexBufferPoolA<N>::vfn_Unk1c(IDirect3D7 *pD3D)
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
		FUN_1003a90f();
	return bOK;
}

template <int N> int UnkType_VertexBufferPoolB<N>::vfn_Unk1c(IDirect3D7 *pD3D)
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
		FUN_1003a90f();
	return bOK;
}
// (defined in unk/100062e0)
void __cdecl FUN_100062e0(float *pDest, float *pSrc, float fZBias);

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
int FUN_10006e40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
// (defined in unk/100062e0)
int FUN_10007100(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
// (defined in unk/100062e0)
int FUN_100073b0(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
// (defined in unk/100062e0)
int FUN_10007670(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
