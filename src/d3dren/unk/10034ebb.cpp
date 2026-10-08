// d3d.ren unk/10034ebb (0x10034ebb-0x1003579a): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// queued lightmapped world poly draw.
// FLAGS: /O1 /Ob2
// d3d.ren unit unk/10034000 (0x10034000-0x10038830): lightmap pages (64x64 DirectDraw texture pages the polygon lightmaps are
// packed into: "Unable to create (%dx%d) lightmap page.", "Lightmaps paged in %.1f seconds.", "LightAnim_BASE"), the lightmap
// plane table and SetupLMPlaneVectors (engine twin src/shared/lightmap_planes.cpp), the lightmap staging texture pools and
// the queued world polygon drawing, quat_ConvertToMatrix (engine twin src/sdk/ltquatbase.cpp), a BSP segment walk, three
// colour tables, and the pixelformat object (engine twin src/shared/pixelformat.cpp).  Several original objects: size
// objects (packed COMDATs, no padding), so FLAGS /O1 /Ob2 for all of them.
#include <windows.h>
#include <string.h>
#include <mmsystem.h>
#include "d3dren/lightmap.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/d3dstate.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/pool.h"
#include "d3dren/polydraw.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/viewparams.h"
#include "d3dren/tlvertex.h"
#include "visquery.h"
#include "ltquatbase.h"
#include <math.h>
#include "world_tree.h"

// Texture coordinate offsets/scales the world poly draw state sets (FUN_10007930's unit): world x/z of the current texture ref.
// GLOBAL: D3DREN 0x1004ffb0
extern float DAT_1004ffb0;
// GLOBAL: D3DREN 0x1004ffb4
extern float DAT_1004ffb4;
// GLOBAL: D3DREN 0x1004eba8
extern float DAT_1004eba8;
// GLOBAL: D3DREN 0x1004ebac
extern float DAT_1004ebac;
// GLOBAL: D3DREN 0x10057774
extern uint8 DAT_10057774;		// guess: alpha byte of the vertex colours
// The current texture reference: [0] SharedTexture*, [4]/[8] texture offset, [0xc]/[0x10] texture size (GlobalPanInfo, RenderStruct::m_GlobalPans).
// GLOBAL: D3DREN 0x10055ce0
extern GlobalPanInfo *DAT_10055ce0;	// &RenderStruct::m_GlobalPans[n]

// callbacks of FUN_10035629 (one queued poly each); they return non-zero when they drew something
int FUN_10035071(WorldPoly *pPoly);
int FUN_100354bf(WorldPoly *pPoly);
int FUN_10035416(WorldPoly *pPoly);
uint32 FUN_10035629(UnkType_PoolNode *pNode, int (*pfn)(WorldPoly *), int bFree, UnkType_PoolNode **ppDeferred);

// Gamma/colour byte tables of the world poly vertex colours (256 entries each).
// GLOBAL: D3DREN 0x1005a004
extern uint8 DAT_1005a004[256];
// GLOBAL: D3DREN 0x1005a104
extern uint8 DAT_1005a104[256];
// GLOBAL: D3DREN 0x1005a204
extern uint8 DAT_1005a204[256];

// GLOBAL: D3DREN 0x1007d424
extern int DAT_1007d424;		// guess: set elsewhere (lightmap draw state)
void FUN_10035348(WorldPoly *pPoly, UnkType_TLVertex40 *pVerts, UnkType_PolyVertex *pSrc, int nVerts);
void FUN_1003524b(WorldPoly *pPoly, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts);
int FUN_10020ff0(WorldPoly *pPoly, int bDirect);	// W8's unit: updates the dynamic lights of the poly's lightmap
void FUN_10013ef0(WorldPoly *pPoly);				// guess: draws the poly without its lightmap (fallback)

// ---- queued world polygon drawing (the polygons of lightmapped surfaces are queued per texture by FUN_100356b8) --------------
// The queued polys' texture: node -> poly -> surface -> SharedTexture.
#define BUCKET_TEXTURE(pBucket)	(((Surface *)((WorldPoly *)(pBucket)->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture)

// guess: draws everything that was queued in DAT_1005a308 (one bucket per texture): the polys of textures that can be bound now
// are drawn with their lightmap (FUN_10035071), the polys of surfaces flagged 0x8000 are deferred to a second pass that draws
// them with world uvs (FUN_100354bf), and the buckets that drew something get a last pass with the lightmap alone (FUN_10035416).
// The successful bucket path advances directly to the next iteration. The explicit sb_Free bodies retain both original null checks.
// FUNCTION: D3DREN 0x10034ebb
void FUN_10034ebb()
{
	UnkType_PoolBucket *pLightmapPass = 0;
	UnkType_PoolNode *pDeferred = 0;
	UnkType_PoolBucket *pBucket, *pNext;
	SharedTexture *pTexture;
	DWORD oldState;

	pBucket = DAT_1005a308;
	if (pBucket)
	{
		do
		{
			pNext = pBucket->m_Unk08;
			if (pBucket->m_Unk04)
			{
				if (FUN_100211d0(BUCKET_TEXTURE(pBucket), g_NormalTextureStage))
				{
					if (FUN_10035629(pBucket->m_Unk04, FUN_10035071, 0, &pDeferred))
					{
						pBucket->m_Unk08 = pLightmapPass;
						pLightmapPass = pBucket;
						((UnkType_BucketOwner *)pBucket->m_Unk00)->m_Unk1c[0] = 0;
						pBucket = pNext;
						continue;
					}

					FUN_100142b0(pBucket->m_Unk04);
				}
				else
				{
					FUN_10035629(pBucket->m_Unk04, FUN_10035071, 1, &pDeferred);
				}
			}

			((UnkType_BucketOwner *)pBucket->m_Unk00)->m_Unk1c[0] = 0;
			if (pBucket && &DAT_10058c98)
			{
				StructLink *pLink = (StructLink *)pBucket;
				pLink->m_pSLNext = DAT_10058c98.m_FreeListHead;
				DAT_10058c98.m_FreeListHead = pLink;
			}
			pBucket = pNext;
		} while (pBucket);

		if (pDeferred)
		{
			if (DAT_10055ce0 && DAT_10055ce0->m_pTexture)
				d3d_SetTexture(DAT_10055ce0->m_pTexture, DAT_1005c838, 0);
			FUN_10007930();
			FUN_10035629(pDeferred, FUN_100354bf, 1, (UnkType_PoolNode **)-1);
		}

		if (pLightmapPass)
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
			g_pD3DDevice->GetTextureStageState(1, D3DTSS_COLOROP, &oldState);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

			pBucket = pLightmapPass;
			do
			{
				pNext = pBucket->m_Unk08;
				if (pBucket->m_Unk04 && (pTexture = BUCKET_TEXTURE(pBucket)) != 0)
				{
					d3d_SetTexture(pTexture, g_NormalTextureStage, 0);
					FUN_10035629(pBucket->m_Unk04, FUN_10035416, 1, (UnkType_PoolNode **)-1);
				}
				if (pBucket && &DAT_10058c98)
				{
					StructLink *pLink = (StructLink *)pBucket;
					pLink->m_pSLNext = DAT_10058c98.m_FreeListHead;
					DAT_10058c98.m_FreeListHead = pLink;
				}
				pBucket = pNext;
			} while (pBucket);

			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, oldState);
		}
	}

	DAT_1005a308 = 0;
}

// guess: sets the first texture coordinates of a vertex (the arguments stay on the x87 stack: `fld v; fld u; fstp tu; fstp tv`).
static inline void SetUV(UnkType_TLVertex40 *pVertex, float u, float v)
{
	pVertex->tu = u;
	pVertex->tv = v;
}

// guess: draws one queued world polygon with its lightmap (first texture stage the poly's texture, second the lightmap):
// copies the vertices (with the lightmap uvs of FUN_1003429b as second texture coordinates) into a stack array of 0x28 byte
// vertices, projects/clips them, rebinds the poly's texture, applies the texture's state change and draws a triangle fan.
// Polys with more than 0x80 vertices are refused.  Returns 1 when the poly was drawn.
// The fallback stays after the successful draw so all failed paths share the original return sequence.
// FUNCTION: D3DREN 0x10035071
int FUN_10035071(WorldPoly *pPoly)
{
	UnkType_TLVertex40 verts[0x80];
	UnkType_TLVertex40 *pVerts;
	int nVerts;
	UnkType_PolyVertex *pSrc;
	UnkType_TLVertex40 *pDest;
	int i;
	StateChange *pStateChange;

	pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);

	if (pPoly->m_nVertices > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return 0;
	}

	uint32 nPolyVerts = pPoly->m_nVertices;
	pDest = verts;
	for (i = 0; i < nPolyVerts; i++)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->color = DAT_100566bc.color;
		SetUV(pDest, pSrc->m_U, pSrc->m_V);
		pDest->tu2 = pSrc->m_Unk0c;
		pDest->tv2 = pSrc->m_Unk10;
		pDest++;
		pSrc++;
	}

	nVerts = nPolyVerts;
	pVerts = verts;
	if (!FUN_100085f2(&pVerts, &nVerts, &g_ViewParams, 0))
		return 0;

	if (!WORLDPOLY_UNK30(pPoly) || !FUN_10020ff0(pPoly, 0))
	{
		if (!d3d_SetLightmapTexture(pPoly, DAT_1005c838))
			goto Fallback;
	}
	else if (pVerts == verts)
	{
		FUN_10035348(pPoly, pVerts, (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58), nVerts);
	}
	else
	{
		pVerts = verts;
		nVerts = pPoly->m_nVertices;
		FUN_1003524b(pPoly, verts, (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58), nVerts);
		if (!FUN_100085f2(&pVerts, &nVerts, &g_ViewParams, 0))
			return 0;
	}

	d3d_SetTexture(((Surface *)pPoly->m_pSurface)->m_pTexture, g_NormalTextureStage, 0);

	pDest = pVerts;
	for (i = nVerts; i; --i)
	{
		float u = pDest->tu;
		float v = pDest->tv;
		SetUV(pDest, u * DAT_10061810[0].m_Unk00, v * DAT_10061810[0].m_Unk04);
		++pDest;
	}

	DAT_10063c90.FUN_10021da6();
	pStateChange = (StateChange *)((Surface *)pPoly->m_pSurface)->m_pTexture->m_pStateChange;
	if (pStateChange)
		DAT_10063c90.FUN_10021db7(pStateChange, g_NormalTextureStage);

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x2c4, pVerts, nVerts, 0);
	DAT_10063c90.FUN_10021da6();
	DAT_100566ac++;
	return 1;
Fallback:
	FUN_10013ef0(pPoly);
	return 0;
}

// guess: fills the 0x28 byte vertices pDest from the poly's vertices pSrc (nVerts of them): positions, colour, texture uvs and
// as second uvs the planar lightmap coordinates of the vertices, scaled by the detail stage's uv scale.
// Copy the vertex before computing the delta. The SDK Dot calls preserve the original x87 evaluation and retained delta components.
// FUNCTION: D3DREN 0x1003524b
void FUN_1003524b(WorldPoly *pPoly, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts)
{
	LTVector P, Q;
	int i;

	SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);

	for (i = 0; i < nVerts; i++)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->color = DAT_100566bc.color;
		SetUV(pDest, pSrc->m_U, pSrc->m_V);
		LTVector d = *pSrc->m_Vec - pPoly->m_Unknown38;
		pDest->tu2 = (P.Dot(d) / DAT_10056770->m_LMGridSize + 0.5f) * DAT_10061810[1].m_Unk00;
		pDest->tv2 = (Q.Dot(d) / DAT_10056770->m_LMGridSize + 0.5f) * DAT_10061810[1].m_Unk04;
		pDest++;
		pSrc++;
	}
}

// guess: as FUN_1003524b but only the second uvs (the vertices are already filled).
// (The chains of named temporaries below were found by tools/permute.py: they pin the order in which P and Q are first referenced, which
// decides the x87 term order of the two sums; the plain loop body `P.x * d.x + P.y * d.y + P.z * d.z` gives a different order.)
// FUNCTION: D3DREN 0x10035348
void FUN_10035348(WorldPoly *pPoly, UnkType_TLVertex40 *pVerts, UnkType_PolyVertex *pSrc, int nVerts)
{
	float fTmp110;
	float fTmp109;
	LTVector d;
	float fTmp45;
	float fTmp116;
	float fTmp4;
	float fTmp59;
	float fTmp49;
	float fTmp1;
	WorldPoly * pPPoly2 = pPoly;
	LTVector *pVec108 = &pPPoly2->m_pPlane->m_Normal;
	LTVector P;
	LTVector Q;

	UnkType_TLVertex40 * pVertsLocal = pVerts;
	UnkType_TLVertex40 * pPVerts2;
	int nVertsLocal = nVerts;
	int tmpNVerts = nVertsLocal;
	pPVerts2 = pVertsLocal;
	SetupLMPlaneVectors((pPPoly2->m_Flags & 0x3800) >> 11, (*pVec108), P, Q);

	int i = 0;
	for (;;)
	{
		float fTmp148 = P.z;
		if (!(i < tmpNVerts))
			break;
		fTmp109 = P.y;
		fTmp110 = fTmp148;
		float fTmp149 = Q.x;
		fTmp45 = fTmp110;
		float fTmp141 = Q.z;
		fTmp49 = fTmp141;
		float fTmp5 = fTmp49;
		fTmp116 = P.x;
		d = *pSrc->m_Vec - pPPoly2->m_Unknown38;

		fTmp4 = fTmp109;
		fTmp1 = fTmp45;
		fTmp59 = Q.y;
		pPVerts2->tu2 = ((fTmp116 * d.x + fTmp4 * d.y + fTmp1 * d.z) / DAT_10056770->m_LMGridSize + 0.5f) * DAT_10061810[1].m_Unk00;
		++pSrc;
		pPVerts2->tv2 = ((fTmp149 * d.x + fTmp59 * d.y + fTmp5 * d.z) / DAT_10056770->m_LMGridSize + 0.5f) * DAT_10061810[1].m_Unk04;
		pPVerts2++;
		++i;
	}
}

// guess: draws a queued poly with the lightmap alone: 0x20 byte vertices (uv = the lightmap uvs of the poly scaled by the stage 0
// uv scale), clipped, one triangle fan.
// FUNCTION: D3DREN 0x10035416
int FUN_10035416(WorldPoly *pPoly)
{
	TLVertex verts[64];
	TLVertex *pVerts;
	int nVerts;
	UnkType_PolyVertex *pSrc;
	TLVertex *pDest;
	uint32 i;

	uint32 nPolyVerts = pPoly->m_nVertices;
	pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
	pDest = verts;
	for (i = 0; i < nPolyVerts; i++)
	{
		float fU = DAT_10061810[0].m_Unk00 * pSrc->m_U;
		float fV = DAT_10061810[0].m_Unk04 * pSrc->m_V;
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->tu = fU;
		pDest->color = DAT_100566bc.color;
		pDest->tv = fV;
		pDest++;
		pSrc++;
	}

	nVerts = nPolyVerts;
	pVerts = verts;
	if (!FUN_1000af16(&pVerts, &nVerts, &g_ViewParams, 0))
		return 0;

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
	return 1;
}

// guess: draws a queued poly in the second pass with world uvs (planar mapping from the world position through the offsets
// DAT_1004ffb0/b4 and scales DAT_1004eba8/ac) and the gamma-corrected vertex colours.
// Snapshotting both UV inputs before scaling preserves the original x87 load/multiply order.
// FUNCTION: D3DREN 0x100354bf
int FUN_100354bf(WorldPoly *pPoly)
{
	UnkType_TLVertex40 verts[64];
	UnkType_TLVertex40 *pVerts;
	int nVerts;
	UnkType_PolyVertex *pSrc;
	UnkType_TLVertex40 *pDest;
	uint32 i;
	StateChange *pStateChange;

	uint32 nPolyVerts = pPoly->m_nVertices;
	pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
	pDest = verts;
	for (i = 0; i < nPolyVerts; i++)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pDest->rgb.r = DAT_1005a004[pSrc->m_Color[2]];
		pDest->rgb.g = DAT_1005a104[pSrc->m_Color[1]];
		pDest->rgb.b = DAT_1005a204[pSrc->m_Color[0]];
		pDest->rgb.a = DAT_10057774;
		SetUV(pDest, pSrc->m_U, pSrc->m_V);
		pDest->tu2 = (DAT_1004ffb0 + pSrc->m_Vec->x) * DAT_1004eba8;
		pDest->tv2 = (DAT_1004ffb4 + pSrc->m_Vec->z) * DAT_1004ebac;
		pDest++;
		pSrc++;
	}

	nVerts = nPolyVerts;
	pVerts = verts;
	FUN_100083ec(pPoly, verts, nVerts);
	if (!FUN_100085f2(&pVerts, &nVerts, &g_ViewParams, 0))
		return 0;

	d3d_SetTexture(((Surface *)pPoly->m_pSurface)->m_pTexture, g_NormalTextureStage, 0);

	pDest = pVerts;
	for (i = nVerts; i; --i)
	{
		float u = pDest->tu;
		float v = pDest->tv;
		SetUV(pDest, u * DAT_10061810[0].m_Unk00, v * DAT_10061810[0].m_Unk04);
		++pDest;
	}

	DAT_10063c90.FUN_10021da6();
	pStateChange = (StateChange *)((Surface *)pPoly->m_pSurface)->m_pTexture->m_pStateChange;
	if (pStateChange)
		DAT_10063c90.FUN_10021db7(pStateChange, g_NormalTextureStage);

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x2c4, pVerts, nVerts, 0);
	DAT_10063c90.FUN_10021da6();
	return 1;
}

// guess: calls pfn for every queued poly of the list (linked through m_Unk10); polys of surfaces flagged 0x8000 are copied to
// the list *ppDeferred instead unless ppDeferred is -1 (the second pass); bFree gives the nodes back to the pool.  Returns the OR
// of the callbacks' results (1 for a deferred poly).
// FUNCTION: D3DREN 0x10035629
uint32 FUN_10035629(UnkType_PoolNode *pNode, int (*pfn)(WorldPoly *), int bFree, UnkType_PoolNode **ppDeferred)
{
	uint32 result = 0;
	UnkType_PoolNode *pNext;
	UnkType_PoolNode *pNew;

	if (pNode)
	{
		do
		{
			pNext = pNode->m_Unk10;
			if (!(((Surface *)((WorldPoly *)pNode->m_Unk00)->m_pSurface)->m_Flags & 0x8000) || ppDeferred == (UnkType_PoolNode **)-1)
			{
				g_ClipFlags = pNode->m_Unk0c;
				result |= pfn((WorldPoly *)pNode->m_Unk00);
			}
			else
			{
				pNew = (UnkType_PoolNode *)sb_Allocate(&DAT_10058758);
				if (pNew)
				{
					result |= 1;
					pNew->m_Unk00 = pNode->m_Unk00;
					pNew->m_Unk0c = pNode->m_Unk0c;
					pNew->m_Unk10 = *ppDeferred;
					*ppDeferred = pNew;
				}
			}

			if (bFree && pNode)
				sb_Free(&DAT_10058758, pNode);

			pNode = pNext;
		} while (pNode);
	}

	return result;
}

// guess: queues pPoly in the per-texture list DAT_1005a308 with the current clip state word.
// FUNCTION: D3DREN 0x100356b8
void FUN_100356b8(WorldPoly *pPoly)
{
	FUN_10007ddb(pPoly, &DAT_1005a308, 0)->m_Unk0c = g_ClipFlags;
}

// guess: sets the texture stage states of the lightmap passes: stage 0 modulates the diffuse colour with the base texture,
// stage 1 (unless g_LightmapsOnly) modulates the lightmap with it (add when DAT_1007d424).
// The stage 1 setup calls are written out in both branches and the compiler tail-merges the two shared ones; written as a
// single copy after the if/else, the constant 2 is not CSE'd into edi (it needs 6 uses; the merged copy shows only 5 pushes).
// FUNCTION: D3DREN 0x100356d5
void FUN_100356d5()
{
	uint32 op;

	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

	op = D3DTOP_MODULATE;
	if (DAT_1007d424)
		op = D3DTOP_MODULATE2X;

	if (g_LightmapsOnly)
	{
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	}
	else
	{
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, op);
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	}
}

// guess: restores the texture stage states after the lightmap passes.
// FUNCTION: D3DREN 0x10035771
void FUN_10035771()
{
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

// ---- pixelformat (engine twin: src/shared/pixelformat.cpp; Jupiter runtime/shared/src/pixelformat.cpp) --------------------------------
// Where the code is emitted: function templates (Convert1Pass/Convert2Pass/ConvertDXTGeneric) are NOT inlined at /O1 and their instances land
// after the last ordinary function of the object, every out-of-line copy of an inline member (BaseBFToAny::Init, CC_*::DoConvert, or_cpy) right
// after its first caller; so the source order below is not the address order.

#define SRC_8	(*pSrc)
#define SRC_16	(*((uint16*)pSrc))
#define SRC_32	(*((uint32*)pSrc))
#define DEST_8	(*pDest)
#define DEST_16	(*((uint16*)pDest))
#define DEST_32	(*((uint32*)pDest))

#define ALPHAVAL abstract.m_AlphaValues

#define READROW_NORMAL(index, startOffset)\
	A::Or(pDestPos, 0, ALPHAVAL[(alphaData[index] >> (startOffset+0)) & 0x7]);\
	A::Or(pDestPos, 1, ALPHAVAL[(alphaData[index] >> (startOffset+3)) & 0x7]);\
	A::Or(pDestPos, 2, ALPHAVAL[(alphaData[index] >> (startOffset+6)) & 0x7]);\
	A::Or(pDestPos, 3, ALPHAVAL[(alphaData[index] >> (startOffset+9)) & 0x7]);\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

#define DECODE_LINE(lineShiftAmt)\
	A::Set(pDestPos, 0, abstract.m_Ident[(blockData>>(lineShiftAmt+0)) & 3]);\
	A::Set(pDestPos, 1, abstract.m_Ident[(blockData>>(lineShiftAmt+2)) & 3]);\
	A::Set(pDestPos, 2, abstract.m_Ident[(blockData>>(lineShiftAmt+4)) & 3]);\
	A::Set(pDestPos, 3, abstract.m_Ident[(blockData>>(lineShiftAmt+6)) & 3]);\
	pDestPos = (((uint8*)pDestPos) + pRequest->m_DestPitch);

#define DECODE_ALPHA_2ROWS() \
	DECODE_ALPHA(0, 0)\
	DECODE_ALPHA(4, 1)\
	DECODE_ALPHA(8, 2)\
	DECODE_ALPHA(12, 3)\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;\
	DECODE_ALPHA(16, 0)\
	DECODE_ALPHA(20, 1)\
	DECODE_ALPHA(24, 2)\
	DECODE_ALPHA(28, 3)\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

#define DECODE_ALPHA(shift, iPixel)\
	A::Mask(pDestPos, iPixel, invAlphaMask);\
	A::Or(pDestPos, iPixel, abstract.m_AlphaValues[(blockData>>shift) & 15]);

#undef SRC_8
#undef SRC_16
#undef SRC_32
#undef DEST_8
#undef DEST_16
#undef DEST_32
#undef ALPHAVAL
#undef READROW_NORMAL
#undef DECODE_LINE
#undef DECODE_ALPHA_2ROWS
#undef DECODE_ALPHA
