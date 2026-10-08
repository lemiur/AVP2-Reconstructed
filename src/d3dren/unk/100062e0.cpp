// d3d.ren unk/100062e0 (0x100062e0-0x10007930): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// really-close projection FUN_100062e0 + 8 out-of-line plane clippers (4 for 0x20-byte, 4 for 0x28-byte vertices).
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

// The model vertex buffer cache global (defined in unk/1003a680.cpp as a static member: its declaration costs one `_$E` number in
// every unit that includes the header, so this include comes after this unit's own static initialisers).
#include "d3dren/vbcache.h"

// Plane clippers for the remaining planes (flag bits 8, 0x10, 0x20, 2); the first argument is unused by them.
int ClipPolyTop(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyRight(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyBottom(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyFar(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);

int FUN_10006e40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007100(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_100073b0(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007670(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);

// Same epsilon as Jupiter 3d_ops.h CLIP_EPSILON (0.00001f): the two constants at 0x100461c4 / 0x100461c8 are +-1e-5.
#define CLIP_EPSILON	0.00001f

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
		(P) = (TLVertex *)pp->Lock(); \
		(E) = (char *)pp->Lock() + pp->vfn_Unk18() * (nFree2 - 1); \
	}

#define POOL_FLUSH_DRAW() \
	{ \
		UnkType_VertexBufferPool *pp = m_Unk608; \
		uint32 nBytes = (char *)pOut - (char *)pp->Lock(); \
		uint32 nVertsOut = nBytes / pp->vfn_Unk18(); \
		DAT_1005626c += nVertsOut / 3; \
		((UnkType_VBPoolDrawView *)m_Unk608)->Draw(g_pD3DDevice, D3DPT_TRIANGLELIST, nVertsOut); \
	}

// The body of the two clipping callbacks.  REALLYCLOSE adds the z bias g_CV_NearZ.m_FloatVal to the w of the z row (FUN_100062e0).
// Per triangle: the clip planes of g_ClipFlags are tested vertex by vertex (count of vertices inside: none = skip the
// triangle, not all = it has to be clipped), then the triangle is back face tested in 2D and projected.
#define CLIPPED_CALLBACK(REALLYCLOSE) \
	TLVertex *pOut = (TLVertex *)m_Unk608->Lock(); \
	char *pEnd; \
	{ \
		UnkType_VertexBufferPool *pPool = m_Unk608; \
		uint32 nFree = pPool->m_Unk20 - pPool->m_Unk1c; \
		char *pBase = (char *)pPool->Lock(); \
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
			nIn = (pV2[2] <= g_ViewParams.m_ClipFarZ) + (pV1[2] <= g_ViewParams.m_ClipFarZ) + (pV0[2] <= g_ViewParams.m_ClipFarZ); \
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
			if (g_ViewParams.m_bCullFlip) \
				fCross = -fCross; \
			if (0.0f < fCross) \
			{ \
				float *apV[3] = { pV0, pV1, pV2 }; \
				for (int k = 0; k < 3; k++) \
				{ \
					if (REALLYCLOSE) \
						FUN_100062e0(&pOut->m_Vec.x, apV[k], g_CV_NearZ.m_FloatVal); \
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
				if (g_ViewParams.m_bCullFlip) \
					fCross = -fCross; \
				if (0.0f < fCross) \
				{ \
					char *pPolyEnd = pPoly + nStride * nPoly; \
					for (char *pP = pPoly; pP < pPolyEnd; pP += m_Unk5f8) \
					{ \
						float vSrc[3] = { ((float *)pP)[0], ((float *)pP)[1], ((float *)pP)[2] }; \
						if (REALLYCLOSE) \
							FUN_100062e0((float *)pP, vSrc, g_CV_NearZ.m_FloatVal); \
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

// guess: projection with a z bias: the clip-space position of the vertex moved nearer by fZBias, used by the "really close" draw.
// Collect xyz in a vector, write the biased reciprocal w, then copy the vector. This preserves the target's
// three float spill slots and integer copies; x/y use the unbiased w while z uses the biased coordinate and w.
// FUNCTION: D3DREN 0x100062e0
void __cdecl FUN_100062e0(float *pDest, float *pSrc, float fZBias)
{
	LTVector result;
	float w = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.x = (g_ViewParams.m_DeviceTimesProjection.m[0][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[0][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[0][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[0][3]) * w;
	result.y = (g_ViewParams.m_DeviceTimesProjection.m[1][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[1][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[1][2] * pSrc[2] + g_ViewParams.m_DeviceTimesProjection.m[1][3]) * w;
	float z = fZBias + pSrc[2];
	float w2 = 1.0f / (g_ViewParams.m_DeviceTimesProjection.m[3][2] * z + g_ViewParams.m_DeviceTimesProjection.m[3][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[3][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[3][3]);
	result.z = (g_ViewParams.m_DeviceTimesProjection.m[2][0] * pSrc[0] + g_ViewParams.m_DeviceTimesProjection.m[2][1] * pSrc[1] + g_ViewParams.m_DeviceTimesProjection.m[2][2] * z + g_ViewParams.m_DeviceTimesProjection.m[2][3]) * w2;
	pDest[3] = w2;
	*(LTVector *)pDest = result;
}

// ---- plane clippers (merged from the W1 scratch unit) ----
// ClipExtra, written as an inline helper here (the exe expands it inside the plane clippers).
static inline void ClipExtra32(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}

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

// guess: top plane (inside: y < z), flag 8
// FUNCTION: D3DREN 0x100063e0
int ClipPolyTop(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	static int bInside[56];	// 0x10094b40
	int *pInside;
	int nInside = 0;
	TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float *pv = &(*ppOut)->m_Vec.x;
				float d = ((pCur->m_Vec.y - pPrev->m_Vec.y) - pCur->m_Vec.z) + pPrev->m_Vec.z;
				if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
					t = -((pPrev->m_Vec.y - pPrev->m_Vec.z) / d);
				else
					t = 0.0f;
				pv[0] = (pCur->m_Vec.x - pPrev->m_Vec.x) * t + pPrev->m_Vec.x;
				float z = (pCur->m_Vec.z - pPrev->m_Vec.z) * t + pPrev->m_Vec.z;
				pv[2] = z;
				pv[1] = z;
				ClipExtra32(pPrev, pCur, *ppOut, t);
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

// guess: right plane (inside: x < z), flag 0x10
// FUNCTION: D3DREN 0x10006670
int ClipPolyRight(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	static int bInside[56];	// 0x10094a60
	int *pInside;
	int nInside = 0;
	TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float *pv = &(*ppOut)->m_Vec.x;
				float d = ((pCur->m_Vec.x - pPrev->m_Vec.x) - pCur->m_Vec.z) + pPrev->m_Vec.z;
				if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
					t = -((pPrev->m_Vec.x - pPrev->m_Vec.z) / d);
				else
					t = 0.0f;
				pv[1] = (pCur->m_Vec.y - pPrev->m_Vec.y) * t + pPrev->m_Vec.y;
				float z = (pCur->m_Vec.z - pPrev->m_Vec.z) * t + pPrev->m_Vec.z;
				pv[2] = z;
				pv[0] = z;
				ClipExtra32(pPrev, pCur, *ppOut, t);
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

// guess: bottom plane (inside: -z < y), flag 0x20
// FUNCTION: D3DREN 0x10006900
int ClipPolyBottom(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	static int bInside[56];	// 0x10094980
	int *pInside;
	int nInside = 0;
	TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float d;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.y > -pCur->m_Vec.z;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float *pv = &(*ppOut)->m_Vec.x;
				float fCz = pCur->m_Vec.z;
				d = ((pCur->m_Vec.y - pPrev->m_Vec.y) + fCz) - pPrev->m_Vec.z;
				float t = (d < -CLIP_EPSILON || d > CLIP_EPSILON) ? -((pPrev->m_Vec.z + pPrev->m_Vec.y) / d) : 0.0f;
				pv[0] = (pCur->m_Vec.x - pPrev->m_Vec.x) * t + pPrev->m_Vec.x;
				float z = (pCur->m_Vec.z - pPrev->m_Vec.z) * t + pPrev->m_Vec.z;
				pv[2] = z;
				pv[1] = -z;
				ClipExtra32(pPrev, pCur, *ppOut, t);
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

// guess: far plane (inside: z <= g_ViewParams.m_ClipFarZ), flag 2
// FUNCTION: D3DREN 0x10006ba0
int ClipPolyFar(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut)
{
	static int bInside[56];	// 0x100948a0
	int *pInside;
	int nInside = 0;
	TLVertex *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float d = pCur->m_Vec.z - pPrev->m_Vec.z;
				float *pv = &(*ppOut)->m_Vec.x;
				if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
					t = (g_ViewParams.m_ClipFarZ - pPrev->m_Vec.z) / d;
				else
					t = 0.0f;
				pv[0] = (pCur->m_Vec.x - pPrev->m_Vec.x) * t + pPrev->m_Vec.x;
				pv[1] = (pCur->m_Vec.y - pPrev->m_Vec.y) * t + pPrev->m_Vec.y;
				pv[2] = g_ViewParams.m_ClipFarZ;
				ClipExtra32(pPrev, pCur, *ppOut, t);
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

// guess: top plane (inside: y < z), flag 8 (0x28-byte vertices)
// FUNCTION: D3DREN 0x10006e40
int FUN_10006e40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	static int bInside[56];	// 0x100947c0
	int *pInside;
	int nInside = 0;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float *pv = &(*ppOut)->m_Vec.x;
				float d = ((pCur->m_Vec.y - pPrev->m_Vec.y) - pCur->m_Vec.z) + pPrev->m_Vec.z;
				if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
					t = -((pPrev->m_Vec.y - pPrev->m_Vec.z) / d);
				else
					t = 0.0f;
				pv[0] = (pCur->m_Vec.x - pPrev->m_Vec.x) * t + pPrev->m_Vec.x;
				float z = (pCur->m_Vec.z - pPrev->m_Vec.z) * t + pPrev->m_Vec.z;
				pv[2] = z;
				pv[1] = z;
				ClipExtra40(pPrev, pCur, *ppOut, t);
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

// guess: right plane (inside: x < z), flag 0x10 (0x28-byte vertices)
// FUNCTION: D3DREN 0x10007100
int FUN_10007100(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	static int bInside[56];	// 0x100946e0
	int *pInside;
	int nInside = 0;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float *pv = &(*ppOut)->m_Vec.x;
				float d = ((pCur->m_Vec.x - pPrev->m_Vec.x) - pCur->m_Vec.z) + pPrev->m_Vec.z;
				if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
					t = -((pPrev->m_Vec.x - pPrev->m_Vec.z) / d);
				else
					t = 0.0f;
				pv[1] = (pCur->m_Vec.y - pPrev->m_Vec.y) * t + pPrev->m_Vec.y;
				float z = (pCur->m_Vec.z - pPrev->m_Vec.z) * t + pPrev->m_Vec.z;
				pv[2] = z;
				pv[0] = z;
				ClipExtra40(pPrev, pCur, *ppOut, t);
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

// guess: bottom plane (inside: -z < y), flag 0x20 (0x28-byte vertices)
// Not matching (11/704 bytes): identical except for the tail of the inlined ClipExtra: the exe schedules `xor ecx,ecx` (the zero
// extension for `mov cl,[pCur->rgb.a]` at vertex offset +0x13) before the store of rgb.b, ours after it.  The same function for the top and
// right planes (FUN_10006e40/10007100) and the 0x20-byte twins match with this source shape; 4000 permuter candidates found nothing.
// STUB: D3DREN 0x100073b0
int FUN_100073b0(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	static int bInside[56];	// 0x10094600
	int *pInside;
	int nInside = 0;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float d;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.y > -pCur->m_Vec.z;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float *pv = &(*ppOut)->m_Vec.x;
				float fCz = pCur->m_Vec.z;
				d = ((pCur->m_Vec.y - pPrev->m_Vec.y) + fCz) - pPrev->m_Vec.z;
				float t = (d < -CLIP_EPSILON || d > CLIP_EPSILON) ? -((pPrev->m_Vec.z + pPrev->m_Vec.y) / d) : 0.0f;
				pv[0] = (pCur->m_Vec.x - pPrev->m_Vec.x) * t + pPrev->m_Vec.x;
				float z = (pCur->m_Vec.z - pPrev->m_Vec.z) * t + pPrev->m_Vec.z;
				pv[2] = z;
				pv[1] = -z;
				ClipExtra40(pPrev, pCur, *ppOut, t);
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

// guess: far plane (inside: z <= g_ViewParams.m_ClipFarZ), flag 2 (0x28-byte vertices)
// Not matching (11/704 bytes): identical except for the tail of the inlined ClipExtra: the exe schedules `xor ecx,ecx` (the zero
// extension for `mov cl,[pCur->rgb.a]` at vertex offset +0x13) before the store of rgb.b, ours after it.  The same function for the top and
// right planes (FUN_10006e40/10007100) and the 0x20-byte twins match with this source shape; 4000 permuter candidates found nothing.
// STUB: D3DREN 0x10007670
int FUN_10007670(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	static int bInside[56];	// 0x10094520
	int *pInside;
	int nInside = 0;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	pInside = bInside;
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
			if (bInside[iPrev])
				*(*ppOut)++ = *pPrev;
			if (bInside[iPrev] != bInside[iCur])
			{
				float d = pCur->m_Vec.z - pPrev->m_Vec.z;
				float *pv = &(*ppOut)->m_Vec.x;
				if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
					t = (g_ViewParams.m_ClipFarZ - pPrev->m_Vec.z) / d;
				else
					t = 0.0f;
				pv[0] = (pCur->m_Vec.x - pPrev->m_Vec.x) * t + pPrev->m_Vec.x;
				pv[1] = (pCur->m_Vec.y - pPrev->m_Vec.y) * t + pPrev->m_Vec.y;
				pv[2] = g_ViewParams.m_ClipFarZ;
				ClipExtra40(pPrev, pCur, *ppOut, t);
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

