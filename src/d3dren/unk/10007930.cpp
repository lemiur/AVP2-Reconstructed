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

// GLOBAL: D3DREN 0x10055ce0
extern GlobalPanInfo *DAT_10055ce0;	// &RenderStruct::m_GlobalPans[n] (the current global pan texture)

// Per-stage texture coordinate scale pair (u, v), indexed by the device stage.
struct UnkType_StageUV
{
	float	m_Unk00;
	float	m_Unk04;
};
// GLOBAL: D3DREN 0x10061810
extern UnkType_StageUV DAT_10061810[8];

// GLOBAL: D3DREN 0x1004eba8
extern float DAT_1004eba8;		// guess: world poly u scale of the current texture (stage scale / texture width)
// GLOBAL: D3DREN 0x1004ebac
extern float DAT_1004ebac;		// guess: v scale
// GLOBAL: D3DREN 0x1004ffb0
extern float DAT_1004ffb0;		// guess: u offset of the current texture
// GLOBAL: D3DREN 0x1004ffb4
extern float DAT_1004ffb4;		// guess: v offset

// GLOBAL: D3DREN 0x10058758
extern UnkType_Pool DAT_10058758;	// inside a larger global that starts at 0x1005872c (+0x2c): the poly queue node pool
// GLOBAL: D3DREN 0x10058c98
extern UnkType_Pool DAT_10058c98;	// inside a larger global that starts at 0x10058c90 (+0x8)
// GLOBAL: D3DREN 0x1004ffb8
extern UnkType_PoolNode *DAT_1004ffb8;	// head of the deferred draw list
// GLOBAL: D3DREN 0x10062868
extern LTLink g_Textures;		// NAME: g_Textures: Jupiter d3d_texture.cpp DECLARE_LTLINK(g_Textures) (names_proposal, high); LRU list of RTextures

// GLOBAL: D3DREN 0x100577b8
extern uint16 DAT_100577b8;		// guess: current texture frame code (stored into SharedTexture::m_Unknown30)
// GLOBAL: D3DREN 0x10057794
extern int DAT_10057794;		// guess: g_nTextureChanges: counted per texture binding ("Texture changes: %d")

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

// GLOBAL: D3DREN 0x1005872c
extern void (__fastcall *g_pfnCalcFogAlpha)(LTVector *pPos, uint32 *pSpecular);	// guess: per-vertex fog alpha hook
// GLOBAL: D3DREN 0x10057774
extern uint8 DAT_10057774;		// guess: alpha byte of the poly vertex colour
// GLOBAL: D3DREN 0x1005a004
extern uint8 DAT_1005a004[256];	// guess: red lighting table
// GLOBAL: D3DREN 0x1005a104
extern uint8 DAT_1005a104[256];	// guess: green lighting table
// GLOBAL: D3DREN 0x1005a204
extern uint8 DAT_1005a204[256];	// guess: blue lighting table
// GLOBAL: D3DREN 0x100587e4
extern uint32 DAT_100587e4;		// guess: number of TL vertices in use in the scratch array DAT_100587fc
// GLOBAL: D3DREN 0x100587fc
extern TLVertex *DAT_100587fc;	// guess: the scratch TL vertex array
// GLOBAL: D3DREN 0x1005a368
extern uint32 DAT_1005a368;		// guess: capacity of DAT_100587fc
// GLOBAL: D3DREN 0x1005a308
extern UnkType_PoolBucket *DAT_1005a308;	// guess: list of the buckets (queued polys per texture)
// GLOBAL: D3DREN 0x100566ac
extern int DAT_100566ac;		// guess: polys drawn (statistics)
// GLOBAL: D3DREN 0x100566cc
extern int DAT_100566cc;		// guess: dynamic lights tested (statistics)
// GLOBAL: D3DREN 0x10058040
extern uint8 DAT_10058040;		// guess: fog colour bytes (see FUN_10013990)
// GLOBAL: D3DREN 0x10058041
extern uint8 DAT_10058041;
// GLOBAL: D3DREN 0x10058042
extern uint8 DAT_10058042;

void FUN_10007e5d(WorldPoly *pPoly, TLVertex *pVerts, int nVerts);

uint32 FUN_10013990(uint8 r, uint8 g, uint8 b);		// unit unk/100132a0: packs three bytes into a D3DCOLOR
int FUN_10013e80(int nVertices);					// unit unk/100132a0: grows the scratch array

// The dynamic light setup object of FUN_100325e8 uses the canonical shared lightmap layout.
#include "d3dren/lightmap.h"
struct UnkType_DynLMSetup : public UnkType_LMLock
{
};
int FUN_100325e8(UnkType_DynLMSetup *pSetup, WorldPoly *pPoly, UnkType_PolyLight *pLight, float fScale);	// unit unk/10030bb0

// GLOBAL: D3DREN 0x10094c20
extern int DAT_10094c20[56];	// guess: Jupiter polyclip.h bInside[] of the near plane clipper for 0x28-byte vertices
// GLOBAL: D3DREN 0x10094d00
extern int DAT_10094d00[56];	// guess: bInside[] of the left plane clipper

// FUN_10006e40, FUN_10007100, FUN_100073b0, FUN_10007670: the 0x28-byte clippers of unit unk/10001000 (top, right, bottom, far).
int FUN_10006e40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007100(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_100073b0(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int FUN_10007670(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
float FUN_10001a50(float *p1, float *p2, float *pOut);
float FUN_10001ac0(float *p1, float *p2, float *pOut);
void FUN_10001f20(UnkType_TLVertex40 *pPrev, UnkType_TLVertex40 *pCur, UnkType_TLVertex40 *pOut, float t);

// FUNCTION: D3DREN 0x10007930
void FUN_10007930(void)
{
	if (DAT_10055ce0)
	{
		DAT_1004eba8 = (1.0f / DAT_10055ce0->m_xScale) * DAT_10061810[DAT_1005c838].m_Unk00;
		DAT_1004ebac = (1.0f / DAT_10055ce0->m_zScale) * DAT_10061810[DAT_1005c838].m_Unk04;
		DAT_1004ffb0 = DAT_10055ce0->m_xOffset;
		DAT_1004ffb4 = DAT_10055ce0->m_zOffset;
	}
}

// FUNCTION: D3DREN 0x10007976
void FUN_10007976(void)
{
	UnkType_PoolNode *pNode;

	if (DAT_1004ffb8)
	{
		d3d_SetTexture(DAT_10055ce0->m_pTexture, g_NormalTextureStage, 0);
		FUN_10007930();
		pNode = DAT_1004ffb8;
		while (pNode)
		{
			UnkType_PoolNode *pNext = pNode->m_Unk10;
			g_ClipFlags = pNode->m_Unk0c;
			FUN_10007b41((WorldPoly *)pNode->m_Unk00);
			sb_Free(&DAT_10058758, pNode);
			pNode = pNext;
		}
		DAT_1004ffb8 = 0;
	}
}

// d3d_SetTexture is the inline of d3d_texture.h; this P object (/O1) calls it out of line from FUN_10007976, so it holds the exe's copy.
// STUB diagnosis (W2): 162 of 165 bytes, 38 aligned mismatches.  The exe tests found/bound first (`cmp esi,[nStage*4+g_pBoundTextures]; jne BIND; jmp LOD`),
//   then the two create blocks, and shares ONE `push esi; call FUN_10007a89; pop ecx` between the found-not-bound path and both create paths; the
//   `if (pRTexture && pRTexture == bound) {} else {...}` form is the closest source shape (38), the found-first/`||`/for-loop forms are 43-35, the
//   permuter reached 25.
// STUB: D3DREN 0x100079e4 ?d3d_SetTexture@@YAHPAUSharedTexture@@KK@Z

// Mask the device stage explicitly; cache the UV pair and read V before U to preserve the original load/store scheduling.
// FUNCTION: D3DREN 0x10007a89
void FUN_10007a89(RTexture *pRTexture)
{
	UnkType_RTexView *pTex = (UnkType_RTexView *)pRTexture;

	g_pBoundTextures[pTex->m_Unk42] = (RTextureBase *)pTex;
	if (pTex->m_Unk14 != g_CurFrameCode)
	{
		pTex->m_Unk1c.Remove();
		g_Textures.m_pPrev->AddAfter(&pTex->m_Unk1c);
		*(int *)&g_pStruct->m_Pad48[0] += pTex->m_Unk10;	// RenderStruct+0x48: per-frame texture byte counter
		pTex->m_Unk14 = g_CurFrameCode;
	}
	g_pD3DDevice->SetTexture(pTex->m_Unk42 & 0xff, pTex->m_Unk0c);
	if (pTex->m_Unk16)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAREF, pTex->m_Unk16);
	UnkType_StageUV *pUV = &DAT_10061810[pTex->m_Unk42];
	float v = pTex->m_Unk08;
	float u = pTex->m_Unk04;
	pUV->m_Unk00 = u;
	pUV->m_Unk04 = v;
	DAT_10057794++;
}

// guess: draws one world poly as a triangle fan with the colour, fog and texture coordinates of the current texture (DAT_10055ce0),
// then adds the dynamic lights of the poly (LMDynamic) and records the vertices in the scratch array for the lightmap pass.
// Remaining difference: 3 of 666 bytes, 2 aligned instructions. The vertex-count update at the end reloads nVerts
// into eax instead of the target's edx. Restoring fog and returning on clipping/growth failure preserves the
// original entry pushes and shared epilogue; all 212 instructions otherwise align with the target.
// STUB: D3DREN 0x10007b41
void FUN_10007b41(WorldPoly *pPoly)
{
	UnkType_TLVertex40 aVerts[0x80];
	UnkType_TLVertex40 *pVerts;
	UnkType_PolyVert *pSrc;
	int nVerts;
	struct { D3DRENDERSTATETYPE m_Type; DWORD m_Val; } saved;
	DWORD dwFogColor;

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVert *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVert *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	{
		dwFogColor = FUN_10013990(DAT_10058040, DAT_10058041, DAT_10058042);
		saved.m_Type = D3DRENDERSTATE_FOGCOLOR;
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGCOLOR, &saved.m_Val);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwFogColor);

		{
			UnkType_TLVertex40 *pDest = aVerts;
			int i;
			for (i = nVerts; i > 0; i--)
			{
				LTVector *pPos = pSrc->m_pPos;
				pDest->m_Vec.x = pPos->x;
				pDest->m_Vec.y = pPos->y;
				pDest->m_Vec.z = pPos->z;
				pDest->rgb.r = DAT_1005a004[pSrc->m_Unk16];
				pDest->rgb.g = DAT_1005a104[pSrc->m_Unk15];
				pDest->rgb.b = DAT_1005a204[pSrc->m_Unk14];
				pDest->rgb.a = DAT_10057774;
				g_pfnCalcFogAlpha(&pDest->m_Vec, &pDest->specular);
				float fV = (DAT_1004ffb4 + pPos->z) * DAT_1004ebac;
				float fU = (DAT_1004ffb0 + pPos->x) * DAT_1004eba8;
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
			FUN_100083ec(pPoly, aVerts, nVerts);

		if (!FUN_100085f2(&pVerts, &nVerts, &g_ViewParams, 0) ||
			!(DAT_100587e4 + nVerts <= DAT_1005a368 || FUN_10013e80(DAT_1005a368 + nVerts + 0x5dc)))
		{
			g_pD3DDevice->SetRenderState(saved.m_Type, saved.m_Val);
			return;
		}
		{
			TLVertex *pStore = DAT_100587fc + DAT_100587e4;
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

			DAT_10063c90.FUN_10021da6();
			if (DAT_10055ce0->m_pTexture->m_pStateChange)
				DAT_10063c90.FUN_10021db7(DAT_10055ce0->m_pTexture->m_pStateChange, g_NormalTextureStage);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pStore, nVerts, 0);
			DAT_10063c90.FUN_10021da6();

			if (POLY_LIGHTS(pPoly))
				FUN_10007e5d(pPoly, pStore, nVerts);

			pNode = FUN_10007ddb(pPoly, &DAT_1005a308, 0);
			pNode->m_Unk04 = DAT_100587e4;
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
			DAT_100587e4 += nVerts;
			DAT_100566ac++;
		}
		g_pD3DDevice->SetRenderState(saved.m_Type, saved.m_Val);
	}
}

// NAME: sb_Allocate (StdLith struct_bank.h): see pool.h.
// FUNCTION: D3DREN 0x10007ddb
UnkType_PoolNode *FUN_10007ddb(WorldPoly *pPoly, UnkType_PoolBucket **ppBucketList, int iSlot)
{
	SharedTexture *pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;
	UnkType_PoolBucket **ppSlots = (UnkType_PoolBucket **)&pTexture->m_Unknown1C;
	UnkType_PoolBucket *pBucket = ppSlots[iSlot];
	UnkType_PoolNode *pNode;

	if (!pBucket)
	{
		pBucket = (UnkType_PoolBucket *)sb_Allocate(&DAT_10058c98);
		pBucket->m_Unk00 = pTexture;
		ppSlots[iSlot] = pBucket;
		pBucket->m_Unk04 = 0;
		pBucket->m_Unk08 = *ppBucketList;
		*ppBucketList = pBucket;
	}
	pNode = (UnkType_PoolNode *)sb_Allocate(&DAT_10058758);
	pNode->m_Unk10 = pBucket->m_Unk04;
	pBucket->m_Unk04 = pNode;
	pNode->m_Unk00 = pPoly;
	return pNode;
}

// sb_Allocate is the StdLith inline of struct_bank.h; this P object (/O1) calls it out of line (FUN_10007ddb), so it holds the exe's only copy.
// NAME: sb_Allocate: StdLith struct_bank.h `inline void *sb_Allocate(StructBank *pBank)` (pop m_FreeListHead; when empty
// sb_AllocateNewStructPage(pBank, m_CacheSize), retry, 0 on failure).
// FUNCTION: D3DREN 0x10007e36 ?sb_Allocate@@YAPAXPAUStructBank_t@@@Z

// FUNCTION: D3DREN 0x100083bf
void FUN_100083bf(WorldPoly *pPoly)
{
	UnkType_PoolNode *pNode = (UnkType_PoolNode *)sb_Allocate(&DAT_10058758);
	if (pNode)
	{
		pNode->m_Unk0c = g_ClipFlags;
		pNode->m_Unk10 = DAT_1004ffb8;
		DAT_1004ffb8 = pNode;
		pNode->m_Unk00 = pPoly;
	}
}

// Out-of-line copies of Talon SDK inlines (ltmatrix.h / ltvector.h / counter.h): the exe has one copy of each, emitted after the first
// function that calls it out of line; the compiler produces them from the real inline definitions, no stand-in is needed.
// FUNCTION: D3DREN 0x10008197 ?Inverse@LTMatrix@@QAEIXZ
// FUNCTION: D3DREN 0x1000832e ?Apply4x4@LTMatrix@@QAEXABV?$_CVector@M@@AAV2@@Z

// NAME: CountAdder::~CountAdder (names_proposal.csv, high): counter.h's inline destructor `*m_pNum += cnt_EndCounter(m_Counter)`.  The exe's out-of-line copy is
// called by FUN_10007e5d, d3d_DrawParticleSystem-area code and 0x10020ff0 / 0x10021d70 / 0x10023860 (the shared COMDAT, emitted in this object); this build never keeps
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
void FUN_10007e5d(WorldPoly *pPoly, TLVertex *pVerts, int nVerts)
{
	if (g_CV_LMDynamic.m_IntVal)
	{
		LTVector P;
		UnkType_CountAdderRaw cTimer(g_pSceneDesc->m_pTicks_Render_PolyGrids);
		LTVector Q;
		struct SavedState { D3DRENDERSTATETYPE m_Type; DWORD m_Val; };
		SavedState rsAlphaBlend, rsDestBlend, rsSrcBlend, rsFogColor;
		DWORD dwOldAddress;

		LTMatrix mInvTransform;
		UnkType_PolyLight *pLight;

		pPoly->m_LMWidth = (uint8)g_CV_LMDynamicSize.m_IntVal;
		pPoly->m_LMHeight = (uint8)g_CV_LMDynamicSize.m_IntVal;
		SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);

		rsAlphaBlend.m_Type = D3DRENDERSTATE_ALPHABLENDENABLE;
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &rsAlphaBlend.m_Val);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
		rsSrcBlend.m_Type = D3DRENDERSTATE_SRCBLEND;
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_SRCBLEND, &rsSrcBlend.m_Val);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
		rsDestBlend.m_Type = D3DRENDERSTATE_DESTBLEND;
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_DESTBLEND, &rsDestBlend.m_Val);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
		rsFogColor.m_Type = D3DRENDERSTATE_FOGCOLOR;
		g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGCOLOR, &rsFogColor.m_Val);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, 0);
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

			if (setup.FUN_10034af0(pPoly, 1, 0, 0))
			{
				nBuild = FUN_100325e8(&setup, pPoly, pLight, 1.0f);
				if (setup.FUN_10034c7c(nBuild) && nBuild)
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
		d3d_SetTexture(DAT_10055ce0->m_pTexture, g_NormalTextureStage, 0);
		g_pD3DDevice->SetRenderState(rsFogColor.m_Type, rsFogColor.m_Val);
		g_pD3DDevice->SetRenderState(rsDestBlend.m_Type, rsDestBlend.m_Val);
		g_pD3DDevice->SetRenderState(rsSrcBlend.m_Type, rsSrcBlend.m_Val);
		g_pD3DDevice->SetRenderState(rsAlphaBlend.m_Type, rsAlphaBlend.m_Val);
		CountAdder_Destructor((CountAdder *)&cTimer);
	}
}

// Keep this red-before-green write order: VC6's vertex-base register matches the target only with this schedule.
// FUNCTION: D3DREN 0x100083ec
void FUN_100083ec(WorldPoly *pPoly, UnkType_TLVertex40 *pVerts, int param_3)
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

		DAT_100566cc++;
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
int FUN_100085f2(UnkType_TLVertex40 **ppVerts, int *pnVerts, void *pViewParams, int param_4)
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
			FUN_10008719(&pVert->m_Vec.x, &pView->m_mClipTransform.m[0][0]);
		if (!FUN_10008779(g_ClipFlags, ppVerts, pnVerts))
			return 0;
		pVert = *ppVerts;
		for (n = *pnVerts; n; n--, pVert++)
			ProjectVertexToScreen(&pVert->m_Vec.x, pView);
	}
	return 1;
}

// The coordinates are captured before any in-place stores.
// FUNCTION: D3DREN 0x10008719
void FUN_10008719(float *pVec, const float *pMatrix)
{
	float x = pMatrix[0] * pVec[0] + pMatrix[1] * pVec[1] + pMatrix[2] * pVec[2] + pMatrix[3];
	float y = pMatrix[4] * pVec[0] + pMatrix[5] * pVec[1] + pMatrix[6] * pVec[2] + pMatrix[7];
	float z = pMatrix[8] * pVec[0] + pMatrix[9] * pVec[1] + pMatrix[10] * pVec[2] + pMatrix[11];
	pVec[2] = z;
	pVec[0] = x;
	pVec[1] = y;
}

// FUNCTION: D3DREN 0x10008779
int FUN_10008779(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts)
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

	if ((flags & 1) && !FUN_100088ec(&bUnused0, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 4) && !FUN_10008a23(&bUnused1, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 8) && !FUN_10006e40(&bUnused2, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x10) && !FUN_10007100(&bUnused3, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 0x20) && !FUN_100073b0(&bUnused4, &pVerts, &nVerts, &pOut))
		return 0;
	if ((flags & 2) && !FUN_10007670(&bUnused5, &pVerts, &nVerts, &pOut))
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

// guess: Jupiter polyclip.h expanded out of line for the 0x28-byte vertex: clips *ppVerts (*pnVerts vertices) against the near plane z >= g_ViewParams.m_NearZ.
// FUNCTION: D3DREN 0x100088ec
int FUN_100088ec(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	int *pPnVerts2 = pnVerts;	// a local copy of the parameter (permuter): decides the register reload order after the first copy loop
	int nInside = 0;
	int *pInside;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pInside = DAT_10094c20;
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
			if (DAT_10094c20[iPrev])
				*(*ppOut)++ = *pPrev;
			if (DAT_10094c20[iPrev] != DAT_10094c20[iCur])
			{
				t = FUN_10001a50(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				FUN_10001f20(pPrev, pCur, *ppOut, t);
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
int FUN_10008a23(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut)
{
	int nInside = 0;
	int *pInside;
	UnkType_TLVertex40 *pPrev, *pCur, *pEnd, *pOldOut;
	int iPrev, iCur;
	float t;

	DAT_1005668c++;
	pInside = DAT_10094d00;
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
			if (DAT_10094d00[iPrev])
				*(*ppOut)++ = *pPrev;
			if (DAT_10094d00[iPrev] != DAT_10094d00[iCur])
			{
				t = FUN_10001ac0(&pPrev->m_Vec.x, &pCur->m_Vec.x, &(*ppOut)->m_Vec.x);
				FUN_10001f20(pPrev, pCur, *ppOut, t);
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
float FUN_10008b58(float *p1, float *p2, float *pOut)
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
float FUN_10008bb4(float *p1, float *p2, float *pOut)
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
float FUN_10008c10(float *p1, float *p2, float *pOut)
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
float FUN_10008c6e(float *p1, float *p2, float *pOut)
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
