// d3d.ren unk/10007930 (0x10007930-0x10008cd0): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// poly node pool, d3d_SetTexture copy, 0x28-byte clip dispatcher 0x10008779 + 2 instances, 4 DOCLIP helpers at the end
// (0x10008b58..); possible but unproven split at 0x10008b58 (helpers are only called from drawmodelshadows).
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/10007930 (0x10007930-0x10008cd0): world polygon draw support: the deferred poly list, the texture binding, the node
// pools, the 0x28-byte vertex clippers and the line/plane intersection helpers, plus out-of-line copies of Talon SDK inlines.
// The unit name is an address (no file name evidence: no strings; names_proposal.csv guesses "drawworldmodel", low).
// FLAGS: /O1 /Ob2
#include <windows.h>
#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "ltlink.h"
#include "de_objects.h"
#include "de_world.h"
#include "d3dren/pool.h"
#include "d3dren/polydraw.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "d3dren/viewparams.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/scenedesc.h"
#include "pixelformat.h"
#include "counter.h"

// ---------------------------------------------------------------------------------------------------------------------------------
// Globals of this unit
// ---------------------------------------------------------------------------------------------------------------------------------

// g_TextureStageTexelSizes, g_fGlobalPan*, g_WorldPolyNodeBank, g_WorldPolyBucketBank and g_pDeferredGlobalPanPolys: d3dren/polydraw.h;
// g_pGlobalPanInfo, g_nTextureChanges and g_CurTextureFrameCode: d3dren/common_draw.h; g_Textures: d3dren/d3dtexture.h.

// ---------------------------------------------------------------------------------------------------------------------------------
// The poly vertex fed to the TL vertex array, the dynamic light list of a poly and the callees of other units
// ---------------------------------------------------------------------------------------------------------------------------------

// UnkType_PolyVert: include/d3dren/pool.h

// The dynamic lights touching a poly: a list at WorldPoly+0x30 (the engine pads it): { next, the light, the light position }.
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;
	DynamicLight		*m_pLight;
	LTVector			m_Pos;
};
#define POLY_LIGHTS(p)	(*(UnkType_PolyLight **)((uint8 *)(p) + 0x30))

// g_pfnCalcFogAlpha, g_nQueuedWorldPolyVertices, g_pQueuedWorldPolyVertices, g_nQueuedWorldPolyVertexCapacity, g_pTexturedWorldPolyBuckets
// and g_u8FogColor: d3dren/polydraw.h; g_VertexTintTableR/G/B: d3dren/d3d_draw.h; g_nPolyVertexAlpha, g_nWorldPolysDrawn and
// g_nLightTests: d3dren/common_draw.h.

void DrawPolyDynamicLightmaps(WorldPoly *pPoly, TLVertex *pVerts, int nVerts);

uint32 d3d_PackSqrtRGB(uint8 r, uint8 g, uint8 b);		// unit unk/100132a0: packs three bytes into a D3DCOLOR
int d3d_GrowTLVertexBuffer(int nVertices);					// unit unk/100132a0: grows the scratch array

// The dynamic light setup object of BuildDynamicLightmapPixels uses the canonical shared lightmap layout.
#include "d3dren/lightmap.h"
struct UnkType_DynLMSetup : public UnkType_LMLock
{
};
int BuildDynamicLightmapPixels(UnkType_DynLMSetup *pSetup, WorldPoly *pPoly, UnkType_PolyLight *pLight, float fScale);	// unit unk/10030bb0

// g_ClipNearInsideFlagsVertex40, g_ClipLeftInsideFlagsVertex40: d3dren/polydraw.h.

// The 0x28-byte clippers (ClipPolyNear40 .. ClipPolyFar40) and TLVertex40_ClipExtra: d3dren/tlvertex.h.

// FUNCTION: D3DREN 0x10007930
void SetupWorldTextureCoordinates(void)
{
	if (g_pGlobalPanInfo)
	{
		g_fGlobalPanUScale = (1.0f / g_pGlobalPanInfo->m_xScale) * g_TextureStageTexelSizes[g_LightmapTextureStage].m_Unk00;
		g_fGlobalPanVScale = (1.0f / g_pGlobalPanInfo->m_zScale) * g_TextureStageTexelSizes[g_LightmapTextureStage].m_Unk04;
		g_fGlobalPanUOffset = g_pGlobalPanInfo->m_xOffset;
		g_fGlobalPanVOffset = g_pGlobalPanInfo->m_zOffset;
	}
}

// FUNCTION: D3DREN 0x10007976
void FlushWorldTexturePolys(void)
{
	UnkType_PoolNode *pNode;

	if (g_pDeferredGlobalPanPolys)
	{
		d3d_SetTexture(g_pGlobalPanInfo->m_pTexture, g_NormalTextureStage, 0);
		SetupWorldTextureCoordinates();
		pNode = g_pDeferredGlobalPanPolys;
		while (pNode)
		{
			UnkType_PoolNode *pNext = pNode->m_Unk10;
			g_ClipFlags = pNode->m_Unk0c;
			DrawWorldTexturePoly((WorldPoly *)pNode->m_Unk00);
			sb_Free(&g_WorldPolyNodeBank, pNode);
			pNode = pNext;
		}
		g_pDeferredGlobalPanPolys = 0;
	}
}

// d3d_SetTexture is the inline of d3d_texture.h; this P object (/O1) calls it out of line from FlushWorldTexturePolys, so it holds the exe's copy.
// FUNCTION: D3DREN 0x100079e4 ?d3d_SetTexture@@YAHPAUSharedTexture@@KK@Z

// Mask the device stage explicitly; cache the UV pair and read V before U to preserve the original load/store scheduling.
// FUNCTION: D3DREN 0x10007a89
void d3d_BindRTexture(RTexture *pRTexture)
{
	UnkType_RTexView *pTex = (UnkType_RTexView *)pRTexture;

	g_pBoundTextures[pTex->m_Unk42] = (RTextureBase *)pTex;
	if (pTex->m_Unk14 != g_CurFrameCode)
	{
		pTex->m_Unk1c.Remove();
		g_Textures.m_pPrev->AddAfter(&pTex->m_Unk1c);
		g_pStruct->m_Unk48 += pTex->m_Unk10;
		pTex->m_Unk14 = g_CurFrameCode;
	}
	g_pD3DDevice->SetTexture(pTex->m_Unk42 & 0xff, pTex->m_Unk0c);
	if (pTex->m_Unk16)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAREF, pTex->m_Unk16);
	UnkType_StageUV *pUV = &g_TextureStageTexelSizes[pTex->m_Unk42];
	float v = pTex->m_Unk08;
	float u = pTex->m_Unk04;
	pUV->m_Unk00 = u;
	pUV->m_Unk04 = v;
	g_nTextureChanges++;
}

// guess: draws one world poly as a triangle fan with the colour, fog and texture coordinates of the current texture (g_pGlobalPanInfo),
// then adds the dynamic lights of the poly (LMDynamic) and records the vertices in the scratch array for the lightmap pass.
// Remaining difference: 3 of 666 bytes, 2 aligned instructions. The vertex-count update at the end reloads nVerts
// into eax instead of the target's edx. Restoring fog and returning on clipping/growth failure preserves the
// original entry pushes and shared epilogue; all 212 instructions otherwise align with the target.
// PARKED: register choice only (nVerts reload in eax instead of edx after the UV2 loop; 2 aligned, 3 bytes); 3000+ permuter candidates and loop-form A/Bs exhausted
// STUB: D3DREN 0x10007b41
void DrawWorldTexturePoly(WorldPoly *pPoly)
{
	UnkType_TLVertex40 aVerts[0x80];
	UnkType_TLVertex40 *pVerts;
	UnkType_PolyVert *pSrc;
	int nVerts;

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVert *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVert *)pPoly->m_Vertices;
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	{
		StateSet fogColor(D3DRENDERSTATE_FOGCOLOR, d3d_PackSqrtRGB(g_u8FogColor[0], g_u8FogColor[1], g_u8FogColor[2]));

		{
			UnkType_TLVertex40 *pDest = aVerts;
			int i;
			for (i = nVerts; i > 0; i--)
			{
				LTVector *pPos = pSrc->m_pPos;
				pDest->m_Vec.x = pPos->x;
				pDest->m_Vec.y = pPos->y;
				pDest->m_Vec.z = pPos->z;
				pDest->rgb.r = g_VertexTintTableR[pSrc->m_Unk16];
				pDest->rgb.g = g_VertexTintTableG[pSrc->m_Unk15];
				pDest->rgb.b = g_VertexTintTableB[pSrc->m_Unk14];
				pDest->rgb.a = g_nPolyVertexAlpha;
				g_pfnCalcFogAlpha(&pDest->m_Vec, &pDest->specular);
				float fV = (g_fGlobalPanVOffset + pPos->z) * g_fGlobalPanVScale;
				float fU = (g_fGlobalPanUOffset + pPos->x) * g_fGlobalPanUScale;
				pDest->tu = fU;
				pDest->tv = fV;
				pDest->tu2 = pSrc->m_Unk04;
				pDest->tv2 = pSrc->m_Unk08;
				pSrc++;
				pDest++;
			}
		}

		pVerts = aVerts;
		if (!g_CV_LMDynamic.m_IntVal)
			AddPolyDynamicVertexLighting(pPoly, aVerts, nVerts);

		if (!TransformClipProjectPolygon40(&pVerts, &nVerts, &g_ViewParams, 0) ||
			!(g_nQueuedWorldPolyVertices + nVerts <= g_nQueuedWorldPolyVertexCapacity || d3d_GrowTLVertexBuffer(g_nQueuedWorldPolyVertexCapacity + nVerts + 0x5dc)))
			return;
		{
			TLVertex *pStore = g_pQueuedWorldPolyVertices + g_nQueuedWorldPolyVertices;
			UnkType_PoolNode *pNode;
			TLVertex *pDst;
			UnkType_TLVertex40 *pSrcVert;
			int n;

			pDst = pStore;
			pSrcVert = pVerts;
			for (n = nVerts; n; n--)
			{
				*pDst = *(TLVertex *)pSrcVert;
				pDst++;
				pSrcVert++;
			}

			g_TextureStateRestorer.RestoreAllStates();
			if (g_pGlobalPanInfo->m_pTexture->m_pStateChange)
				g_TextureStateRestorer.ApplyStateChange(g_pGlobalPanInfo->m_pTexture->m_pStateChange, g_NormalTextureStage);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pStore, nVerts, 0);
			g_TextureStateRestorer.RestoreAllStates();

			if (POLY_LIGHTS(pPoly))
				DrawPolyDynamicLightmaps(pPoly, pStore, nVerts);

			pNode = AllocateTexturePolyNode(pPoly, &g_pTexturedWorldPolyBuckets, 0);
			pNode->m_Unk04 = g_nQueuedWorldPolyVertices;
			pNode->m_Unk08 = nVerts;
			pNode->m_Unk0c = g_ClipFlags;
			pNode->m_Unk14 = 3;
			pDst = pStore;
			pSrcVert = pVerts;
			for (n = nVerts; n; n--)
			{
				pDst->tu = pSrcVert->tu2;
				pDst->tv = pSrcVert->tv2;
				pDst++;
				pSrcVert++;
			}
			g_nQueuedWorldPolyVertices += nVerts;
			g_nWorldPolysDrawn++;
		}
	}
}

// NAME: sb_Allocate (StdLith struct_bank.h): see pool.h.
// FUNCTION: D3DREN 0x10007ddb
UnkType_PoolNode *AllocateTexturePolyNode(WorldPoly *pPoly, UnkType_PoolBucket **ppBucketList, int iSlot)
{
	SharedTexture *pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;
	UnkType_PoolBucket **ppSlots = (UnkType_PoolBucket **)&pTexture->m_Unknown1C;
	UnkType_PoolBucket *pBucket = ppSlots[iSlot];
	UnkType_PoolNode *pNode;

	if (!pBucket)
	{
		pBucket = (UnkType_PoolBucket *)sb_Allocate(&g_WorldPolyBucketBank);
		pBucket->m_Unk00 = pTexture;
		ppSlots[iSlot] = pBucket;
		pBucket->m_Unk04 = 0;
		pBucket->m_Unk08 = *ppBucketList;
		*ppBucketList = pBucket;
	}
	pNode = (UnkType_PoolNode *)sb_Allocate(&g_WorldPolyNodeBank);
	pNode->m_Unk10 = pBucket->m_Unk04;
	pBucket->m_Unk04 = pNode;
	pNode->m_Unk00 = pPoly;
	return pNode;
}

// sb_Allocate is the StdLith inline of struct_bank.h; this P object (/O1) calls it out of line (AllocateTexturePolyNode), so it holds the exe's only copy.
// NAME: sb_Allocate: StdLith struct_bank.h `inline void *sb_Allocate(StructBank *pBank)` (pop m_FreeListHead; when empty
// sb_AllocateNewStructPage(pBank, m_CacheSize), retry, 0 on failure).
// FUNCTION: D3DREN 0x10007e36 ?sb_Allocate@@YAPAXPAUStructBank_t@@@Z

// FUNCTION: D3DREN 0x100083bf
void QueueWorldTexturePoly(WorldPoly *pPoly)
{
	UnkType_PoolNode *pNode = (UnkType_PoolNode *)sb_Allocate(&g_WorldPolyNodeBank);
	if (pNode)
	{
		pNode->m_Unk0c = g_ClipFlags;
		pNode->m_Unk10 = g_pDeferredGlobalPanPolys;
		g_pDeferredGlobalPanPolys = pNode;
		pNode->m_Unk00 = pPoly;
	}
}

// Out-of-line copies of Talon SDK inlines (ltmatrix.h / ltvector.h / counter.h): the exe has one copy of each, emitted after the first
// function that calls it out of line; the compiler produces them from the real inline definitions, no stand-in is needed.
// FUNCTION: D3DREN 0x10008197 ?Inverse@LTMatrix@@QAEIXZ
// FUNCTION: D3DREN 0x1000832e ?Apply4x4@LTMatrix@@QAEXABV?$_CVector@M@@AAV2@@Z

// NAME: CountAdder::~CountAdder (names_proposal.csv, high): counter.h's inline destructor `*m_pNum += cnt_EndCounter(m_Counter)`.  The exe's out-of-line copy is
// called by DrawPolyDynamicLightmaps, d3d_DrawParticleSystem-area code and 0x10020ff0 / 0x10021d70 / 0x10023860 (the shared COMDAT, emitted in this object); this build never keeps
// the destructor out of line (it is expanded at every scope exit), so the copy is written as the equivalent __fastcall function (same register contract: this in ecx).
// FUNCTION: D3DREN 0x100083ae
void __fastcall CountAdder_Destructor(CountAdder *pThis)
{
	uint32 nTicks = cnt_EndCounter(pThis->m_Counter);
	*pThis->m_pNum += nTicks;
}
// Layout-identical CountAdder view without its inline destructor; the exe's destructor copy is called explicitly at scope exit.
struct UnkType_CountAdderRaw
{
	UnkType_CountAdderRaw(uint32 *pNum)
	{
		m_pNum = pNum;
		cnt_StartCounter(m_Counter);
	}

	Counter		m_Counter;
	uint32		*m_pNum;
};
// FUNCTION: D3DREN 0x100085d3 ?MagSqr@?$_CVector@M@@QBEMXZ
// FUNCTION: D3DREN 0x1000868d ?MatVMul_InPlace_H@@YAMPAVLTMatrix@@PAV?$_CVector@M@@@Z

// guess: dynamic light pass of a world poly (LMDynamic): for every light of the poly draws the poly again with additive blending and
// texture coordinates projected from the light position.
// The executable uses the out-of-line CountAdder destructor and projects each transformed vertex with LTVector::Dot.
// FUNCTION: D3DREN 0x10007e5d
void DrawPolyDynamicLightmaps(WorldPoly *pPoly, TLVertex *pVerts, int nVerts)
{
	if (g_CV_LMDynamic.m_IntVal)
	{
		LTVector P;
		UnkType_CountAdderRaw cTimer(g_pSceneDesc->m_pTicks_Render_PolyGrids);
		LTVector Q;
		DWORD dwOldAddress;

		LTMatrix mInvTransform;
		UnkType_PolyLight *pLight;

		pPoly->m_LMWidth = (uint8)g_CV_LMDynamicSize.m_IntVal;
		pPoly->m_LMHeight = (uint8)g_CV_LMDynamicSize.m_IntVal;
		SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);

		{
			StateSet rsAlphaBlend(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			StateSet rsSrcBlend(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
			StateSet rsDestBlend(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
			StateSet rsFogColor(D3DRENDERSTATE_FOGCOLOR, 0);
			g_pD3DDevice->GetTextureStageState(0, D3DTSS_ADDRESS, &dwOldAddress);
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);

			{
				LTMatrix mCopy = g_ViewParams.m_FullTransform;
				mCopy.Inverse();
				mInvTransform = mCopy;
			}

			for (pLight = POLY_LIGHTS(pPoly); pLight; pLight = pLight->m_pNext)
			{
				UnkType_DynLMSetup setup;
				int nBuild;

				if (setup.LockStagingLightmap(pPoly, 1, 0, 0))
				{
					nBuild = BuildDynamicLightmapPixels(&setup, pPoly, pLight, 1.0f);
					if (setup.UnlockStagingLightmap(nBuild) && nBuild)
					{
						float fScale = 1.0f / (pLight->m_pLight->GetLightRadius((uint32)pLight->m_pLight) * g_CV_LMDynamicScale.m_FloatVal);
						int n = nVerts;
						if (n)
						{
							float *pUV = &pVerts->tv;
							for (; n; n--)
							{
								LTVector vWorld;
								LTVector vDelta;

								mInvTransform.Apply4x4(*(LTVector *)((uint8 *)pUV - 0x1c), vWorld);
								vDelta = vWorld - pLight->m_Pos;
								pUV[-1] = P.Dot(vDelta) * fScale + 0.5f;
								pUV[0] = Q.Dot(vDelta) * fScale + 0.5f;
								pUV += 8;
							}
						}
						g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
					}
				}
			}

			g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, dwOldAddress);
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			d3d_SetTexture(g_pGlobalPanInfo->m_pTexture, g_NormalTextureStage, 0);
		}
		CountAdder_Destructor((CountAdder *)&cTimer);
	}
}

// Keep this red-before-green write order: VC6's vertex-base register matches the target only with this schedule.
// FUNCTION: D3DREN 0x100083ec
void AddPolyDynamicVertexLighting(WorldPoly *pPoly, UnkType_TLVertex40 *pVerts, int param_3)
{
	UnkType_PolyLight *pNode;

	for (pNode = POLY_LIGHTS(pPoly); pNode; pNode = pNode->m_pNext)
	{
		DynamicLight *pLight = pNode->m_pLight;
		float fR = (float)pLight->m_ColorR;
		float fG = (float)pLight->m_ColorG;
		float fB = (float)pLight->m_ColorB;
		LTVector vPos = pNode->m_Pos;
		float fDist;

		fR = fR - (255.0f - fR);
		fG = fG - (255.0f - fG);
		fB = fB - (255.0f - fB);

		g_nLightTests++;
		fDist = vPos.x * pPoly->m_pPlane->m_Normal.x + vPos.y * pPoly->m_pPlane->m_Normal.y + vPos.z * pPoly->m_pPlane->m_Normal.z - pPoly->m_pPlane->m_Dist;
		if (fDist < 0.0f)
			fDist = -fDist;

		if (fDist < pLight->m_LightRadius)
		{
			float fRadiusSqr = pLight->m_LightRadius * pLight->m_LightRadius;
			float fInvRadius = 1.0f / pLight->m_LightRadius;
			uint32 n = pPoly->m_nVertices;
			UnkType_TLVertex40 *pVert = pVerts;

			if (n <= 0)
				continue;
			for (; n; n--, pVert++)
			{
				LTVector vDelta;

				vDelta.x = pVert->m_Vec.x - vPos.x;
				vDelta.y = pVert->m_Vec.y - vPos.y;
				vDelta.z = pVert->m_Vec.z - vPos.z;
				float fDistSqr = vDelta.MagSqr();
				if (fDistSqr < fRadiusSqr)
				{
					float t = 1.0f - (float)sqrt(fDistSqr) * fInvRadius;
					int r = (int)(t * fR) + pVert->rgb.r;
					int g = (int)(t * fG) + pVert->rgb.g;
					int b = (int)(t * fB) + pVert->rgb.b;

					if (g_Saturate)
					{
						r *= 2;
						g *= 2;
						b *= 2;
					}
					if (r > 255) r = 255; else if (r < 0) r = 0;
					if (g > 255) g = 255; else if (g < 0) g = 0;
					if (b > 255) b = 255; else if (b < 0) b = 0;
					pVert->rgb.r = (uint8)r;
					pVert->rgb.g = (uint8)g;
					pVert->rgb.b = (uint8)b;
				}
			}
		}
	}
}

// FUNCTION: D3DREN 0x100085f2
int TransformClipProjectPolygon40(UnkType_TLVertex40 **ppVerts, int *pnVerts, void *pViewParams, int param_4)
{
	ViewParams *pView = (ViewParams *)pViewParams;
	UnkType_TLVertex40 *pVert;
	int n;

	if (g_ClipFlags == 0)
	{
		pVert = *ppVerts;
		for (n = *pnVerts; n; n--, pVert++)
			pVert->rhw = MatVMul_InPlace_H(&pView->m_FullTransform, &pVert->m_Vec);
	}
	else
	{
		pVert = *ppVerts;
		for (n = *pnVerts; n; n--, pVert++)
			TransformPositionInPlace(&pVert->m_Vec.x, &pView->m_mClipTransform.m[0][0]);
		if (!ClipPolygon40(g_ClipFlags, ppVerts, pnVerts))
			return 0;
		pVert = *ppVerts;
		for (n = *pnVerts; n; n--, pVert++)
			ProjectVertexToScreen(&pVert->m_Vec.x, pView);
	}
	return 1;
}

// The coordinates are captured before any in-place stores.
// FUNCTION: D3DREN 0x10008719
void TransformPositionInPlace(float *pVec, const float *pMatrix)
{
	float x = pMatrix[0] * pVec[0] + pMatrix[1] * pVec[1] + pMatrix[2] * pVec[2] + pMatrix[3];
	float y = pMatrix[4] * pVec[0] + pMatrix[5] * pVec[1] + pMatrix[6] * pVec[2] + pMatrix[7];
	float z = pMatrix[8] * pVec[0] + pMatrix[9] * pVec[1] + pMatrix[10] * pVec[2] + pMatrix[11];
	pVec[2] = z;
	pVec[0] = x;
	pVec[1] = y;
}

// FUNCTION: D3DREN 0x10008779
int ClipPolygon40(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts)
{
	UnkType_TLVertex40 *pOut;
	UnkType_TLVertex40 *pVerts;
	int nVerts;
	char bUnused0, bUnused1, bUnused2, bUnused3, bUnused4, bUnused5;

	if (g_CV_UseD3DClip.m_IntVal)
	{
		flags &= 1;
		if (!flags)
			return 1;
	}

	pOut = (UnkType_TLVertex40 *)g_pClipScratchVerts;
	pVerts = *ppVerts;
	nVerts = *pnVerts;

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

// guess: camera space -> screen space for one vertex (rhw = 1/z and the viewport scale/offset of the ViewParams).
// FUNCTION: D3DREN 0x10008895
void ProjectVertexToScreen(float *pVert, const void *pViewParams)
{
	float *pV = pVert;	// a local copy of the parameter (permuter: it changes the x87 term order of the y expression)
	const ViewParams *pView = (const ViewParams *)pViewParams;
	float rhw = 1.0f / pV[2];

	pV[3] = rhw;
	pV[0] = (pView->m_fProjXScale * pV[0] + pView->m_fProjXOffset * pV[2]) * rhw;
	pV[1] = (pView->m_fProjYScale * pV[1] + pView->m_fProjYOffset * pV[2]) * rhw;
	pV[2] = (pView->m_fProjZScale * pV[2] + pView->m_fProjZOffset) * rhw;
}

// The inside[] arrays of the two 0x28-byte clippers below: function-local statics of the original polyclip.h expansion (the exe
// has them with the other clipper statics at the end of .bss); this unit holds the out-of-line copies.
// GLOBAL: D3DREN 0x10094c20
int g_ClipNearInsideFlagsVertex40[56];
// GLOBAL: D3DREN 0x10094d00
int g_ClipLeftInsideFlagsVertex40[56];

// guess: Jupiter polyclip.h expanded out of line for the 0x28-byte vertex: clips *ppVerts (*pnVerts vertices) against the near plane z >= g_ViewParams.m_NearZ.
// FUNCTION: D3DREN 0x100088ec
int ClipPolyNear40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	int *pPnVerts2 = pnVerts;	// a local copy of the parameter (permuter): decides the register reload order after the first copy loop
	int nInside = 0;
	int *pInside;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	g_nPlaneClipTests++;
	pInside = g_ClipNearInsideFlagsVertex40;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	while (pCur != pEnd)
	{
		*pInside = pCur->m_Vec.z >= g_ViewParams.m_NearZ;
		++pCur;
		nInside += *pInside;
		++pInside;
	}

	if (nInside == 0)
		return 0;
	else if (nInside != *pnVerts)
	{
		pOldOut = *ppOut;
		iPrev = *pnVerts - 1;
		pPrev = *ppVerts + iPrev;
		iCur = 0;
		while (iCur < *pPnVerts2)
		{
			pCur = *ppVerts + iCur;
			if (g_ClipNearInsideFlagsVertex40[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ClipNearInsideFlagsVertex40[iPrev] != g_ClipNearInsideFlagsVertex40[iCur])
			{
				t = IntersectNearClipPlane(pPrev->m_Vec, pCur->m_Vec, (*ppOut)->m_Vec);
				TLVertex40_ClipExtra(pPrev, pCur, *ppOut, t);
				++*ppOut;
			}
			iPrev = iCur;
			pPrev = pCur;
			iCur++;
		}
		*pnVerts = *ppOut - pOldOut;
		*ppVerts = pOldOut;
		*ppOut += *pnVerts;
	}
	return 1;
}

// FUNCTION: D3DREN 0x10008a23
int ClipPolyLeft40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	int nInside = 0;
	int *pInside;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	g_nPlaneClipTests++;
	pInside = g_ClipLeftInsideFlagsVertex40;
	pCur = *ppVerts;
	pEnd = pCur + *pnVerts;
	while (pCur != pEnd)
	{
		*pInside = -pCur->m_Vec.z < pCur->m_Vec.x;
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
			if (g_ClipLeftInsideFlagsVertex40[iPrev])
				*(*ppOut)++ = *pPrev;
			if (g_ClipLeftInsideFlagsVertex40[iPrev] != g_ClipLeftInsideFlagsVertex40[iCur])
			{
				t = IntersectLeftClipPlane(pPrev->m_Vec, pCur->m_Vec, (*ppOut)->m_Vec);
				TLVertex40_ClipExtra(pPrev, pCur, *ppOut, t);
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

// Same epsilon as Jupiter 3d_ops.h CLIP_EPSILON.
#define CLIP_EPSILON	0.00001f

// Line / plane intersection helpers for the line system's clipper (Jupiter polyclip.h DOCLIP bodies, out of line); each returns t, the
// parameter along p1->p2, left on the x87 stack for the caller (hence `float`).
// guess: plane y == z (the top plane)
// FUNCTION: D3DREN 0x10008b58
float IntersectTopClipPlane(LTVector &p1, LTVector &p2, LTVector &pOut)
{
	float t;
	float d = ((p2[1] - p1[1]) - p2[2]) + p1[2];
	if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
		t = -((p1[1] - p1[2]) / d);
	else
		t = 0.0f;
	pOut[0] = (p2[0] - p1[0]) * t + p1[0];
	float z = (p2[2] - p1[2]) * t + p1[2];
	pOut[2] = z;
	pOut[1] = z;
	return t;
}

// guess: plane x == z
// FUNCTION: D3DREN 0x10008bb4
float IntersectRightClipPlane(LTVector &p1, LTVector &p2, LTVector &pOut)
{
	float t;
	float d = ((p2[0] - p1[0]) - p2[2]) + p1[2];
	if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
		t = -((p1[0] - p1[2]) / d);
	else
		t = 0.0f;
	pOut[1] = (p2[1] - p1[1]) * t + p1[1];
	float z = (p2[2] - p1[2]) * t + p1[2];
	pOut[2] = z;
	pOut[0] = z;
	return t;
}

// guess: plane y == -z
// FUNCTION: D3DREN 0x10008c10
float IntersectBottomClipPlane(LTVector &p1, LTVector &p2, LTVector &pOut)
{
	float t;
	float d = ((p2[1] - p1[1]) + p2[2]) - p1[2];
	if (d < -CLIP_EPSILON || d > CLIP_EPSILON)
		t = -((p1[2] + p1[1]) / d);
	else
		t = 0.0f;
	pOut[0] = (p2[0] - p1[0]) * t + p1[0];
	float z = (p2[2] - p1[2]) * t + p1[2];
	pOut[2] = z;
	pOut[1] = -z;
	return t;
}

// guess: intersection with the plane z == g_ViewParams.m_ClipFarZ
// FUNCTION: D3DREN 0x10008c6e
float IntersectFarClipPlane(LTVector &p1, LTVector &p2, LTVector &pOut)
{
	float t;
	float dz = p2[2] - p1[2];
	if (dz < -CLIP_EPSILON || dz > CLIP_EPSILON)
		t = (g_ViewParams.m_ClipFarZ - p1[2]) / dz;
	else
		t = 0.0f;
	pOut[0] = (p2[0] - p1[0]) * t + p1[0];
	pOut[1] = (p2[1] - p1[1]) * t + p1[1];
	pOut[2] = g_ViewParams.m_ClipFarZ;
	return t;
}
