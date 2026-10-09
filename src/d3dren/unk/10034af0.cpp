// d3d.ren unk/10034af0 (0x10034af0-0x10034ebb): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// lightmap staging-texture lock/unlock and pool management.
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

// 0x100109fd: the engine's cutil.cpp copy (DDPIXELFORMAT to PFormat)
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat);

// guess: the staging textures and their size class table: texture sizes (width = height) and how many pool textures of each
// size are made by CreateTexturePools (the data is in the exe's .data in this order: sizes, counts).
// GLOBAL: D3DREN 0x1004bf3c
uint32 g_LightmapPoolTextureSizes[5] = { 4, 8, 0x10, 0x20, 0x40 };
// GLOBAL: D3DREN 0x1004bf50
uint32 g_LightmapPoolTextureCounts[5] = { 0x7d, 0x48, 0x18, 10, 10 };
// GLOBAL: D3DREN 0x1007abd0
RTexture *g_pLightmapStagingTextures[5];
// Not matching (133 vs 138 instructions, 24 aligned mismatches): the size-class search.  The exe keeps both the running pointer into the
// size table (compared with the end address 0x1004bf50: `jl`, as `(int)pSize < (int)&g_LightmapPoolTextureSizes[5]` gives in ResetTexturePoolLists) and a separate
// index in the parameter slots [ebp+0x10]/[ebp+8]; our compiler always strength-reduces both into one byte offset counter
// (`cmp [ebp+0x10], 0x14`).  Also the RECT of the BltFast path is stored in a different order and the exe computes `width + left`
// with width as the destination operand.
// STUB: D3DREN 0x10034af0
int UnkType_LMLock::LockStagingLightmap(WorldPoly *pPoly, int bClear, uint32 width, uint32 height)
{
	int i;
	uint32 *pSize;
	IDirectDrawSurface7 *pSurface;
	DDSURFACEDESC2 ddsd;
	RECT rc;
	HRESULT hr;

	m_Unk44 = width <= 0 ? pPoly->m_LMWidth : width;
	width = m_Unk44;

	m_Unk48 = height <= 0 ? pPoly->m_LMHeight : height;
	height = m_Unk48;

	m_Unk4c = 0;
	i = 0;
	for (pSize = g_LightmapPoolTextureSizes; (int)pSize < (int)&g_LightmapPoolTextureSizes[5]; pSize++, i++)
	{
		if (width <= *pSize && height <= *pSize)
		{
			m_Unk08 = i;
			m_Unk4c = &g_LightmapTexturePoolLists[i];
			break;
		}
	}

	if (!m_Unk4c || m_Unk4c->m_pNext == m_Unk4c)
		return 0;

	m_Unk50 = g_pLightmapStagingTextures[m_Unk08];
	pSurface = m_Unk50->m_Data.m_pSurface;

	if (bClear)
	{
		if (!WORLDPOLY_LMPAGE(pPoly) || !g_LightMap)
		{
			DDBLTFX bltfx;
			memset(&bltfx, 0, sizeof(bltfx));
			bltfx.dwSize = sizeof(bltfx);
			bltfx.dwFillColor = 0;
			hr = pSurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &bltfx);
		}
		else
		{
			rc.left = WORLDPOLY_UNK4E(pPoly);
			rc.top = WORLDPOLY_UNK4F(pPoly);
			rc.right = m_Unk44 + rc.left;
			rc.bottom = m_Unk48 + rc.top;
			hr = pSurface->BltFast(0, 0, WORLDPOLY_LMPAGE(pPoly)->m_pSurface, &rc, DDBLTFAST_WAIT);
		}

		if (hr != DD_OK)
			return 0;
	}

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	RECT rcLock;
	rcLock.right = m_Unk44;
	rcLock.top = 0;
	rcLock.left = 0;
	rcLock.bottom = m_Unk48;
	if (pSurface->Lock(&rcLock, &ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) != DD_OK)
		return 0;

	m_Unk04 = ddsd.lPitch;
	m_Unk00 = (uint8 *)ddsd.lpSurface;
	DDPFToPFormat(&ddsd.ddpfPixelFormat, &m_Unk0c);
	return 1;
}

// The distinct HRESULT-valued Load calls preserve the original source-rectangle paths: &rc when g_bLoadWholeLightmapSurface is false, NULL when it is true.
// FUNCTION: D3DREN 0x10034c7c
int UnkType_LMLock::UnlockStagingLightmap(int bUpload)
{
	RECT rc;
	rc.top = 0;
	rc.right = m_Unk44;
	rc.bottom = m_Unk48;
	rc.left = 0;
	RTexture *pTexture;

	m_Unk50->m_Data.m_pSurface->Unlock(&rc);

	if (bUpload)
	{
		pTexture = (RTexture *)m_Unk4c->m_pNext->m_pData;
		if (pTexture->m_Data.m_nTextureFrameCode != g_CurFrameCode)
			pTexture->m_Data.m_nTextureFrameCode = g_CurFrameCode;

		HRESULT result;
		if (!g_bLoadWholeLightmapSurface)
			result = g_pD3DDevice->Load(pTexture->m_Data.m_pSurface, NULL, m_Unk50->m_Data.m_pSurface, &rc, 0);
		else
			result = g_pD3DDevice->Load(pTexture->m_Data.m_pSurface, NULL, m_Unk50->m_Data.m_pSurface, NULL, 0);
		if (result != D3D_OK)
			return 0;

		dl_Remove(&pTexture->m_Link);
		dl_Insert(m_Unk4c->m_pPrev, &pTexture->m_Link);

		g_pBoundTextures[g_LightmapTextureStage] = (RTextureBase *)pTexture;
		g_pD3DDevice->SetTexture(g_LightmapTextureStage, pTexture->m_Data.m_pSurface);

		float fInvSize = 1.0f / (float)g_LightmapPoolTextureSizes[m_Unk08];
		g_TextureStageTexelSizes[g_LightmapTextureStage].m_Unk00 = fInvSize;
		g_TextureStageTexelSizes[g_LightmapTextureStage].m_Unk04 = fInvSize;
	}

	return 1;
}

// ---- lightmap staging texture pools (a data-less class: its constructor and methods only touch the statics above) ----------
// FUNCTION: D3DREN 0x10034d82 _$E2
// FUNCTION: D3DREN 0x10034d87 _$E1
LTLink g_LightmapTexturePoolLists[5];

// FUNCTION: D3DREN 0x10034d88 _$E5
// GLOBAL: D3DREN 0x1007abe4
UnkType_LMTexturePools g_LightmapTexturePools;

// FUNCTION: D3DREN 0x10034db8
void UnkType_LMTexturePools::CreateTexturePools(PFN_CreateLMTexture pfnCreate)
{
	int i;
	uint32 j;
	uint32 size;
	RTexture *pTexture;

	if (g_TextureFormats[FORMAT_LIGHTMAP])
	{
		for (i = 0; i < 5; i++)
		{
			size = g_LightmapPoolTextureSizes[i];
			for (j = 0; j < g_LightmapPoolTextureCounts[i]; j++)
			{
				pTexture = pfnCreate(size, size, 0x4000);
				if (pTexture)
				{
					pTexture->m_Link.m_pData = pTexture;
					g_LightmapTexturePoolLists[i].AddAfter(&pTexture->m_Link);
				}
			}

			g_pLightmapStagingTextures[i] = pfnCreate(size, size, 0x800);
		}
	}
}

// FUNCTION: D3DREN 0x10034e3d
void UnkType_LMTexturePools::ResetTexturePoolLists()
{
	int i;
	LTLink *pLink = g_LightmapTexturePoolLists;

	for (i = 0; i < 5; i++)
		g_pLightmapStagingTextures[i] = 0;

	// (the exe compares the running pointer with the end signed: jl)
	for (; (int)pLink < (int)&g_LightmapTexturePoolLists[5]; pLink++)
		pLink->TieOff();
}

// FUNCTION: D3DREN 0x10034d92
UnkType_LMTexturePools::UnkType_LMTexturePools()
{
	int i;
	LTLink *pLink = g_LightmapTexturePoolLists;

	for (i = 0; i < 5; i++)
		g_pLightmapStagingTextures[i] = 0;

	for (; (int)pLink < (int)&g_LightmapTexturePoolLists[5]; pLink++)
		pLink->TieOff();
}

// FUNCTION: D3DREN 0x10034e61
void UnkType_LMTexturePools::FreeTexturePools()
{
	int i;

	for (i = 0; i < 5; i++)
	{
		LTLink link(LTLink_Init);

		if (g_pLightmapStagingTextures[i])
		{
			link.m_pData = g_pLightmapStagingTextures[i];
			g_LightmapTexturePoolLists[i].AddAfter(&link);
			g_pLightmapStagingTextures[i] = 0;
		}

		CTextureManager_FreeTextureList(&g_LightmapTexturePoolLists[i]);
	}
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
