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
int g_LightmapPageBytesAllocated;
// GLOBAL: D3DREN 0x100796ec
int g_LightmapBytesAssigned;
// GLOBAL: D3DREN 0x100796f0
int g_LightmapPagingResetState;

// ---- lightmap pages (the RenderContext holds the page list; PageInLightmaps builds it, FreeLightmapPages frees it) ----------------------

// guess: looks for a free rectangle of w x h texels (multiples of 4: the page's bitmap has one bit per 4x4 cell) in the pages
// of the context; on success returns 1 with the position and the page.  (The declaration order and `h * w` pin the register allocation:
// found with tools/permute.py.)
// FUNCTION: D3DREN 0x10034000
int FindLightmapPageSpace(RenderContext *pContext, uint32 w, uint32 h, uint32 *pX, uint32 *pY, LightmapPage **ppPage)
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
LightmapPage *CreateLightmapPage(RenderContext *pContext)
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
	ddsd.dwTextureStage = g_LightmapTextureStage;
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
	g_LightmapPageBytesAllocated += 0x2000;
	g_pStruct->m_SystemTextureMemory += pPage->m_nMemoryUse;
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

// FUNCTION: D3DREN 0x1003427e ??_GLightmapPage@@UAEPAXI@Z

// guess: gives the polygon a place in a lightmap page (marks the cells of the page's bitmap, sets the polygon's
// lightmap texture coordinates in the page and its page pointer); polygons of unlit surfaces and polygons that are
// already assigned (or flagged by a light animation, WorldPoly::m_Flags & 0x3f) are left alone.
// Not matching (3 aligned instruction mismatches; 189 vs 190 instructions, both frames 0x9c).  The page rectangle (rc) gives the
// exe's extent checks (both sums computed before either compare).  Remaining: the exe keeps the intermediate store of the u
// coordinate (`fst [m_Unk0c]` before the + x, * scale) that our build drops as dead, and loads m_pWorld one fmul earlier.
/// Tried for the dead store: chained/compound/temporary/Dot spellings and an intervening v store (keeps it but reloads u, 12 mismatches).
// PARKED: 3-instruction residue: the exe keeps the dead intermediate u store that VC6's dead-store elimination removes in every spelling tried; a 2688-candidate permuter run found nothing
// STUB: D3DREN 0x1003429b
int AssignPolyLightmapPage(RenderContext *pContext, WorldPoly *pPoly)
{
	LightmapPage *pPage;
	int x, y;
	uint32 i, j, iCell;
	LTVector P, Q;
	DDSURFACEDESC2 ddsd;

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

		if (!FindLightmapPageSpace(pContext, pPoly->m_LMWidth, pPoly->m_LMHeight, (uint32 *)&x, (uint32 *)&y, &pPage))
		{
			pPage = CreateLightmapPage(pContext);
			if (!pPage)
				return 0;
			y = 0;
			x = 0;
		}

		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);

		RECT rc;
		rc.left = x;
		rc.top = y;
		rc.right = x + pPoly->m_LMWidth;
		rc.bottom = y + pPoly->m_LMHeight;
		if (rc.right > 0x40 || rc.bottom > 0x40)
			return 0;

		for (j = 0; j < pPoly->m_LMHeight; j++)
		{
			for (i = 0; i < pPoly->m_LMWidth; i++)
			{
				iCell = ((j + y) >> 2) * 0x40 + ((x + i) >> 2);
				pPage->m_pOccupancyMap[iCell >> 3] |= 1 << (iCell & 7);
				pPage->m_nUsedTexels++;
			}
		}

		SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);

		SPolyVertex *pVert = pPoly->m_Vertices;
		SPolyVertex *pEnd = pVert + pPoly->GetNumVertices();
		float fScale = 0.015625f;
		while (pVert != pEnd)
		{
			LTVector *pVec = pVert->m_Vec;
			float dx = pVec->x - pPoly->m_Unknown38.x;
			float dy = pVec->y - pPoly->m_Unknown38.y;
			float dz = pVec->z - pPoly->m_Unknown38.z;
			float fU = P.y * dy;
			fU += P.x * dx;
			fU += P.z * dz;
			fU = fU / pContext->m_pWorld->m_LMGridSize + 0.5f;
			pVert->m_Unk0c[0] = fU;
			pVert->m_Unk0c[0] = (pVert->m_Unk0c[0] + (float)(int)x) * fScale;
			float fV = Q.z * dz;
			fV += Q.y * dy;
			fV += Q.x * dx;
			pVert->m_Unk0c[1] = (fV / pContext->m_pWorld->m_LMGridSize + (float)(int)y) * fScale + 0.0078125f;
			pVert++;
		}

		pPoly->m_Unk4e[0] = (uint8)x;
		pPoly->m_Unk4e[1] = (uint8)y;
		g_LightmapBytesAssigned += pPoly->m_LMHeight * pPoly->m_LMWidth * 2;
		pPoly->m_Unk48 = pPage;
	}

	return 1;
}

// FUNCTION: D3DREN 0x100344b0 ?GetNumVertices@WorldPoly@@QAEKXZ

// guess: leaf callback of the visibility query below: assigns the pages of every polygon of the leaf.
// GLOBAL: D3DREN 0x1007aaf4
RenderContext *g_pLightmapPagingContext;

// FUNCTION: D3DREN 0x100344c8
void AssignLeafLightmapPages(Leaf *pLeaf)
{
	uint32 i;

	for (i = 0; i < pLeaf->m_nPolies; i++)
	{
		if (!AssignPolyLightmapPage(g_pLightmapPagingContext, pLeaf->m_Polies[i]))
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
void ClearBspLightmapPageFlags(WorldBsp *pBsp)
{
	uint32 i, j;

	for (i = 0; i < pBsp->m_nLeafs; i++)
		*(uint16 *)((uint8 *)&pBsp->m_Leafs[i] + 0x2c) = 0;

	for (j = 0; j < pBsp->m_nPolies; j++)
		pBsp->m_Polies[j]->m_Flags &= 0xffc0;
}

// guess: builds the lightmap pages of a context: assigns every polygon of the world a page position (the polygons of the
// "LightAnim_BASE" light animation first when the world has one, in leaf order for a VisBSP), then relights them
// (UpdatePolyAnimatedLightmap).  Returns 0 (and frees the pages again) when a page could not be made.
// The model loop counter is one variable for both passes (the list phase reuses it): that puts it in the exe's stack slot.
// FUNCTION: D3DREN 0x10034597
int PageInLightmaps(RenderContext *pContext)
{
	MainWorld *pWorld = pContext->m_pWorld;
	uint32 j;
	WorldBsp *pBsp;
	LightAnim *pAnim;
	DWORD tStart;
	uint32 tElapsed;
	uint32 iClear;

	g_LightmapPagingResetState = 0;
	g_LightmapPageBytesAllocated = 0;
	g_LightmapBytesAssigned = 0;

	tStart = timeGetTime();

	for (iClear = 0; iClear < pWorld->m_WorldModels.GetSize(); iClear++)
		ClearBspLightmapPageFlags(pWorld->m_WorldModels[iClear]->m_pOriginalBsp);

	pAnim = pWorld->FindLightAnim("LightAnim_BASE", LTNULL);
	if (!pAnim || pAnim->m_nFrames < 1)
		return 0;

	for (j = 0; j < pWorld->m_WorldModels.GetSize(); j++)
	{
		uint32 i = 0;
		pBsp = pWorld->m_WorldModels[j]->m_pOriginalBsp;
		if (pBsp->m_nLeafLists > 0)
		{
			for (; i < pBsp->m_nLeafs; i++)
			{
				Leaf *pLeaf = &pBsp->m_Leafs[i];
				UnkType_LMVisQuery query;

				g_pLightmapPagingContext = pContext;
				query.m_pLeaf = pLeaf;
				query.m_pBsp = pBsp;
				query.m_Unknown0C = (void *)AssignLeafLightmapPages;
				query.m_Unknown10 = (void *)vq_DefaultFn1;
				pBsp->WBSlot11(&query);
			}
		}
		else
		{
			for (; i < pBsp->m_nPolies; i++)
			{
				if (!AssignPolyLightmapPage(pContext, pBsp->m_Polies[i]))
					goto Failed;
			}
		}
	}

	FreeLightmapPageBitmaps(pContext);

	for (j = 0; j < pWorld->m_WorldModels.GetSize(); j++)
	{
		uint32 iPoly = 0;
		pBsp = pWorld->m_WorldModels[j]->m_pOriginalBsp;
		for (; iPoly < pBsp->m_nPolies; iPoly++)
		{
			WorldPoly *pPoly = pBsp->m_Polies[iPoly];
			if (!(pPoly->m_Flags & 0x3f))
			{
				pPoly->m_Unk48 = 0;
			}
			else if (!UpdatePolyAnimatedLightmap(pWorld, pPoly, 1))
			{
				goto Failed;
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
		pBsp->m_Polies[i]->m_Unk48 = 0;
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
			g_pStruct->m_SystemTextureMemory -= pPage->m_nMemoryUse;
		}
		delete pPage;
	}

	pContext->m_pLightmapPages = 0;

	for (i = 0; i < pContext->m_pWorld->m_WorldModels.GetSize(); i++)
		ClearPolyLightmapPages(pContext->m_pWorld->m_WorldModels[i]->m_pOriginalBsp);
}

// ---- queued world polygon drawing (the polygons of lightmapped surfaces are queued per texture by QueueLightmappedPoly) --------------
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
