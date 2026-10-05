// d3d.ren unk/10034000 (0x10034000-0x1003481f): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// lightmap pages.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/10034000 (0x10034000-0x10038830): lightmap pages (64x64 DirectDraw texture pages the polygon lightmaps are
// packed into: "Unable to create (%dx%d) lightmap page.", "Lightmaps paged in %.1f seconds.", "LightAnim_BASE"), the lightmap
// plane table and SetupLMPlaneVectors (engine twin src/shared/lightmap_planes.cpp), the lightmap staging texture pools and
// the queued world polygon drawing, quat_ConvertToMatrix (engine twin src/sdk/ltquatbase.cpp), a BSP segment walk, three
// colour tables, and the pixelformat object (engine twin src/shared/pixelformat.cpp).  Several original objects: size
// objects (packed COMDATs, no padding), so FLAGS /O1 /Ob2 for all of them.
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

// guess: the texture format table entry of the lightmap pages (W8's unit sys/d3d/d3d_texture); 0 when there is none.
TextureFormat *d3d_GetLightmapTextureFormat();

// Counters of the lightmap page code (cleared by PageInLightmaps): texture memory of the pages, texels assigned, (unused).
// GLOBAL: D3DREN 0x100796e8
int DAT_100796e8;
// GLOBAL: D3DREN 0x100796ec
int DAT_100796ec;
// GLOBAL: D3DREN 0x100796f0
int DAT_100796f0;

// ---- lightmap pages (the RenderContext holds the page list; PageInLightmaps builds it, FreeLightmapPages frees it) ----------------------

// guess: looks for a free rectangle of w x h texels (multiples of 4: the page's bitmap has one bit per 4x4 cell) in the pages
// of the context; on success returns 1 with the position and the page.  (The declaration order and `h * w` pin the register allocation:
// found with tools/permute.py.)
// FUNCTION: D3DREN 0x10034000
int FUN_10034000(RenderContext *pContext, uint32 w, uint32 h, uint32 *pX, uint32 *pY, LightmapPage **ppPage)
{
	uint32 nTexels;
	LightmapPage *pPage;
	uint32 x, y, dx, dy, iCell;
	int bFree;
	nTexels = h * w;

	pPage = pContext->m_pLightmapPages;
	for (;;)
	{
		if (pPage)
		{
			if (pPage->m_nUsedTexels >= nTexels)
			{
				for (y = 0; y < 0x41 - h; y += 4)
				{
					for (x = 0; x < 0x41 - w; x += 4)
					{
						iCell = (y >> 2) * 0x40 + (x >> 2);
						if (!(pPage->m_pOccupancyMap[iCell >> 3] & (1 << (iCell & 7))))
						{
							bFree = 1;
							for (dx = 0; dx < w; dx += 4)
							{
								for (dy = 0; dy < h; dy += 4)
								{
									iCell = ((dy + y) >> 2) * 0x40 + ((dx + x) >> 2);
									if (pPage->m_pOccupancyMap[iCell >> 3] & (1 << (iCell & 7)))
									{
										bFree = 0;
										break;
									}
								}
								if (!bFree)
									break;
							}

							if (bFree)
							{
								*pX = x;
								*pY = y;
								*ppPage = pPage;
								return 1;
							}
						}
					}
				}
			}
			pPage = pPage->m_pNext;
		}
		else
			break;
	}

	return 0;
}

// guess: allocates a lightmap page with its 64x64 texture surface and puts it at the head of the context's page list.
// FUNCTION: D3DREN 0x10034142
LightmapPage *FUN_10034142(RenderContext *pContext)
{
	LightmapPage *pPage = new LightmapPage;
	if (!pPage)
		return 0;

	pPage->m_pOccupancyMap = (uint8 *)dalloc_z(0x80);
	if (!pPage->m_pOccupancyMap)
	{
		dfree(pPage);
		return 0;
	}

	TextureFormat *pFormat = d3d_GetLightmapTextureFormat();
	if (!pFormat)
	{
		dfree(pPage);
		return 0;
	}

	pPage->m_nMemoryUse = pFormat->m_BytesPP << 12;

	DDSURFACEDESC2 ddsd;
	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwTextureStage = DAT_1005c838;
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_TEXTURESTAGE;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
	ddsd.ddsCaps.dwCaps2 = DDSCAPS2_DONOTPERSIST | DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_HINTDYNAMIC;
	ddsd.dwWidth = 0x40;
	ddsd.dwHeight = 0x40;
	memcpy(&ddsd.ddpfPixelFormat, &pFormat->m_PF, sizeof(DDPIXELFORMAT));

	if (g_pDD->CreateSurface(&ddsd, (LPDIRECTDRAWSURFACE7 *)&pPage->m_pSurface, NULL) != DD_OK)
	{
		dfree(pPage->m_pOccupancyMap);
		dfree(pPage);
		AddDebugMessage(4, "Unable to create (%dx%d) lightmap page.", 0x40, 0x40);
		return 0;
	}

	pPage->m_pNext = pContext->m_pLightmapPages;
	pContext->m_nLightmapPages++;
	pContext->m_pLightmapPages = pPage;
	DAT_100796e8 += 0x2000;
	*(int *)((uint8 *)g_pStruct + 0x50) += pPage->m_nMemoryUse;
	return pPage;
}

// FUNCTION: D3DREN 0x1003424d
LightmapPage::LightmapPage()
{
	m_Unk04 = 0;
	m_Unk08 = 0;
	m_pOccupancyMap = 0;
	m_nUsedTexels = 0;
	m_nMemoryUse = 0;
	m_Unk18 = 0;
	m_pSurface = 0;
	m_pNext = 0;
	m_Unk20 = 1;
}

// FUNCTION: D3DREN 0x10034277
int LightmapPage::IsRTexture()
{
	return 0;
}

int LightmapPage::IsFullbrite()
{
	return 0;
}

// FUNCTION: D3DREN 0x1003427a
int LightmapPage::GetBaseWidth()
{
	return 0x40;
}

int LightmapPage::GetBaseHeight()
{
	return 0x40;
}

// FUNCTION: D3DREN 0x1003427e ??_GUnkType_LMPage@@UAEPAXI@Z

// guess: gives the polygon a place in a lightmap page (marks the cells of the page's bitmap, sets the polygon's
// lightmap texture coordinates in the page and its page pointer); polygons of unlit surfaces and polygons that are
// already assigned (or flagged by a light animation, WorldPoly::m_Flags & 0x3f) are left alone.
// Not matching (197 vs 197 instructions, 112 aligned mismatches ignoring stack offsets; frame 0x9c in the exe, 0xac here): the exe computes
// the vertex lightmap coordinates inline from x87 floats (no LTVector temporary for the delta: our `LTVector d` costs 16 frame bytes),
// keeps the poly in esi (ours ebx) and loads both lightmap sizes into registers before the page search.  Semantics are the exe's.
// STUB: D3DREN 0x1003429b
int FUN_1003429b(RenderContext *pContext, WorldPoly *pPoly)
{
	LightmapPage *pPage;
	uint32 x, y;
	uint32 i, j, iCell;
	LTVector P, Q;
	DDSURFACEDESC2 ddsd;
	uint8 *pCell;

	if (((Surface *)pPoly->m_pSurface)->m_Flags & SURF_LIGHTMAP)
	{
		if (!pPoly->m_LMHeight || !pPoly->m_LMWidth)
		{
			((Surface *)pPoly->m_pSurface)->m_Flags &= ~SURF_LIGHTMAP;
			return 1;
		}

		if (pPoly->m_Flags & 0x3f)
			return 1;

		pPoly->m_Flags = (pPoly->m_Flags & 0xffc1) | 1;

		if (!FUN_10034000(pContext, pPoly->m_LMWidth, pPoly->m_LMHeight, &x, &y, &pPage))
		{
			pPage = FUN_10034142(pContext);
			if (!pPage)
				return 0;
			y = 0;
			x = 0;
		}

		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);

		if (x + pPoly->m_LMWidth > 0x40 || y + pPoly->m_LMHeight > 0x40)
			return 0;

		for (j = 0; j < pPoly->m_LMHeight; j++)
		{
			for (i = 0; i < pPoly->m_LMWidth; i++)
			{
				iCell = ((j + y) >> 2) * 0x40 + ((x + i) >> 2);
				pCell = &pPage->m_pOccupancyMap[iCell >> 3];
				*pCell |= 1 << (iCell & 7);
				pPage->m_nUsedTexels++;
			}
		}

		SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);

		SPolyVertex *pVert = (SPolyVertex *)((uint8 *)pPoly + 0x58);
		SPolyVertex *pEnd = pVert + pPoly->GetNumVertices();
		float fScale = 0.015625f;
		while (pVert != pEnd)
		{
			LTVector d = *pVert->m_Vec - pPoly->m_Unknown38;
			float fU = (P.y * d.y + P.x * d.x + P.z * d.z) / pContext->m_pWorld->m_LMGridSize + 0.5f;
			SPOLYVERTEX_UNK0C(pVert) = fU;
			SPOLYVERTEX_UNK0C(pVert) = (fU + (float)(int)x) * fScale;
			SPOLYVERTEX_UNK10(pVert) = ((Q.z * d.z + Q.y * d.y + Q.x * d.x) / pContext->m_pWorld->m_LMGridSize + (float)(int)y) * fScale + 0.0078125f;
			pVert++;
		}

		WORLDPOLY_UNK4E(pPoly) = (uint8)x;
		WORLDPOLY_UNK4F(pPoly) = (uint8)y;
		DAT_100796ec += pPoly->m_LMHeight * pPoly->m_LMWidth * 2;
		WORLDPOLY_LMPAGE(pPoly) = pPage;
	}

	return 1;
}

// FUNCTION: D3DREN 0x100344b0 ?GetNumVertices@WorldPoly@@QAEKXZ

// guess: leaf callback of the visibility query below: assigns the pages of every polygon of the leaf.
// GLOBAL: D3DREN 0x1007aaf4
RenderContext *DAT_1007aaf4;

// FUNCTION: D3DREN 0x100344c8
void FUN_100344c8(Leaf *pLeaf)
{
	uint32 i;

	for (i = 0; i < pLeaf->m_nPolies; i++)
	{
		if (!FUN_1003429b(DAT_1007aaf4, pLeaf->m_Polies[i]))
			return;
	}
}

// A visibility query record (visquery.h VisQueryInfo) with the defaults of VisQueryRequest's constructor.
struct UnkType_LMVisQuery : public VisQueryInfo
{
	UnkType_LMVisQuery();
};

// FUNCTION: D3DREN 0x100344f5
UnkType_LMVisQuery::UnkType_LMVisQuery()
{
	m_pLeaf = LTNULL;
	m_pBsp = LTNULL;
	m_AddObject = (VQAddObjectFn)vq_DefaultFn1;
	m_Unknown0C = (void *)vq_DefaultFn2;
	m_Unknown10 = LTNULL;
	m_Unknown14 = (void *)vq_DefaultBoolFn;
	m_pUserData = LTNULL;
}

// The renderer's copies of the visibility query default callbacks (the first two are one function after identical code folding).
// FUNCTION: D3DREN 0x1003451a
void vq_DefaultFn1()
{
}

void vq_DefaultFn2()
{
}

// FUNCTION: D3DREN 0x1003451b
LTBOOL vq_DefaultBoolFn()
{
	return 1;
}

// guess: frees the occupancy bitmaps of all pages.
// FUNCTION: D3DREN 0x1003451f
void FreeLightmapPageBitmaps(RenderContext *pContext)
{
	LightmapPage *pPage;

	for (pPage = pContext->m_pLightmapPages; pPage; pPage = pPage->m_pNext)
	{
		if (pPage->m_pOccupancyMap)
		{
			dfree(pPage->m_pOccupancyMap);
			pPage->m_pOccupancyMap = 0;
		}
	}
}

// guess: clears the renderer's per-frame data of a BSP before the pages are built: the 16 bit tag at +0x2c of every leaf
// (Leaf::m_Pad2C) and the lightmap/light animation bits of the polygon flags.
// FUNCTION: D3DREN 0x10034543
void FUN_10034543(WorldBsp *pBsp)
{
	uint32 i, j;

	for (i = 0; i < pBsp->m_nLeafs; i++)
		*(uint16 *)((uint8 *)&pBsp->m_Leafs[i] + 0x2c) = 0;

	for (j = 0; j < pBsp->m_nPolies; j++)
		pBsp->m_Polies[j]->m_Flags &= 0xffc0;
}

// guess: builds the lightmap pages of a context: assigns every polygon of the world a page position (the polygons of the
// "LightAnim_BASE" light animation first when the world has one, in leaf order for a VisBSP), then relights them
// (FUN_10033210).  Returns 0 (and frees the pages again) when a page could not be made.
// Not matching (146 vs 147 instructions, 49 aligned mismatches): the nested loops are laid out as in the exe now (leaf-list branch first,
// one counter for both loops); remaining differences are the placement of the query-record construction (exe stores the leaf pointer
// from `m_Leafs + offset` before DAT_1007aaf4) and the tail of the two loops (the exe tests the poly flag with a jne/jmp pair).
// STUB: D3DREN 0x10034597
int PageInLightmaps(RenderContext *pContext)
{
	MainWorld *pWorld = pContext->m_pWorld;
	uint32 i, j;
	WorldBsp *pBsp;
	LightAnim *pAnim;
	DWORD tStart;
	uint32 tElapsed;

	DAT_100796f0 = 0;
	DAT_100796e8 = 0;
	DAT_100796ec = 0;

	tStart = timeGetTime();

	for (i = 0; i < pWorld->m_WorldModels.GetSize(); i++)
		FUN_10034543(pWorld->m_WorldModels[i]->m_pOriginalBsp);

	pAnim = pWorld->FindLightAnim("LightAnim_BASE", LTNULL);
	if (!pAnim || pAnim->m_nFrames < 1)
		return 0;

	for (i = 0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		j = 0;
		pBsp = pWorld->m_WorldModels[i]->m_pOriginalBsp;
		if (pBsp->m_nLeafLists != 0)
		{
			for (; j < pBsp->m_nLeafs; j++)
			{
				UnkType_LMVisQuery query;

				DAT_1007aaf4 = pContext;
				query.m_pLeaf = &pBsp->m_Leafs[j];
				query.m_pBsp = pBsp;
				query.m_Unknown0C = (void *)FUN_100344c8;
				query.m_Unknown10 = (void *)vq_DefaultFn1;
				pBsp->WBSlot11(&query);
			}
		}
		else
		{
			for (; j < pBsp->m_nPolies; j++)
			{
				if (!FUN_1003429b(pContext, pBsp->m_Polies[j]))
					goto Failed;
			}
		}
	}

	FreeLightmapPageBitmaps(pContext);

	for (i = 0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		pBsp = pWorld->m_WorldModels[i]->m_pOriginalBsp;
		for (j = 0; j < pBsp->m_nPolies; j++)
		{
			WorldPoly *pPoly = pBsp->m_Polies[j];
			if (pPoly->m_Flags & 0x3f)
			{
				if (!FUN_10033210(pWorld, pPoly, 1))
					goto Failed;
			}
			else
			{
				WORLDPOLY_LMPAGE(pPoly) = 0;
			}
		}
	}

	tElapsed = timeGetTime() - tStart;
	AddDebugMessage(0, "Lightmaps paged in %.1f seconds.", tElapsed * 0.001f);
	return 1;

Failed:
	FreeLightmapPages(pContext);
	return 0;
}

// guess: forgets the page of every polygon of a BSP.
// FUNCTION: D3DREN 0x10034783
void ClearPolyLightmapPages(WorldBsp *pBsp)
{
	uint32 i;

	for (i = 0; i < pBsp->m_nPolies; i++)
		WORLDPOLY_LMPAGE(pBsp->m_Polies[i]) = 0;
}

// guess: frees the pages of a context (surfaces, bitmaps, page objects) and forgets them in the world's polygons.
// FUNCTION: D3DREN 0x100347ac
void FreeLightmapPages(RenderContext *pContext)
{
	LightmapPage *pPage, *pNext;
	uint32 i;

	FreeLightmapPageBitmaps(pContext);

	for (pPage = pContext->m_pLightmapPages; pPage; pPage = pNext)
	{
		IDirectDrawSurface7 *pSurface = pPage->m_pSurface;
		pNext = pPage->m_pNext;
		if (pSurface)
		{
			pSurface->Release();
			*(int *)((uint8 *)g_pStruct + 0x50) -= pPage->m_nMemoryUse;
		}
		delete pPage;
	}

	pContext->m_pLightmapPages = 0;

	for (i = 0; i < pContext->m_pWorld->m_WorldModels.GetSize(); i++)
		ClearPolyLightmapPages(pContext->m_pWorld->m_WorldModels[i]->m_pOriginalBsp);
}

// ---- queued world polygon drawing (the polygons of lightmapped surfaces are queued per texture by FUN_100356b8) --------------
// The queued polys' texture: node -> poly -> surface -> SharedTexture.
#define BUCKET_TEXTURE(pBucket)	(((Surface *)((WorldPoly *)(pBucket)->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture)

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
