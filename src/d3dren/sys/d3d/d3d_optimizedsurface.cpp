// d3d.ren sys/d3d/d3d_optimizedsurface (0x1001bf10-0x1001d1b5): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// object order d3d_optimizedsurface < shadow texture < d3d_surface needs the shadow-texture file to be named d3d_s[h-t]*, e.g.
// d3d_shadowtexture.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren surfaces: d3d_optimizedsurface (0x1001bf10), the shadow texture classes (0x1001d1b5) and d3d_surface (0x1001da60).
// The unit proposal puts the three original objects in one unit (names: config/d3dren/NAMING.md section 4).
// FLAGS: /O1 /Ob2
#include <stdio.h>
#include <string.h>
#include "d3dren/d3d_surface.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/d3d_texture.h"
#include "d3dren/tlvertex.h"
#include "ltdynarray.h"
#include "d3dren/common_init.h"

// ---- globals defined by this object ------------------------------------------------------------------------------------

// NAME: g_ValidTileSizes: Jupiter d3d_optimizedsurface.cpp `static uint32 g_ValidTileSizes[] = {2, 4, 8, 16, 32, 64}`
// GLOBAL: D3DREN 0x1004b4c0
static uint32 g_ValidTileSizes[] = {2, 4, 8, 16, 32, 64};

// NAME: g_Optimized2DColor: Jupiter d3d_optimizedsurface.cpp (initial 0xFFFFFFFF)
// GLOBAL: D3DREN 0x1004b4d8
static uint32 g_Optimized2DColor = 0xFFFFFFFF;

// NAME: fSub: Jupiter d3d_optimizedsurface.cpp `const float fSub = 0.51f` (here a .data variable, the exe reads it from memory)
// GLOBAL: D3DREN 0x1004b4dc
static float fSub = 0.51f;

// NAME: g_Optimized2DBlend: names_proposal.csv (high, Jupiter `static LTSurfaceBlend g_Optimized2DBlend(LTSURFACEBLEND_ALPHA)`)
// GLOBAL: D3DREN 0x1005f24c
static LTSurfaceBlend g_Optimized2DBlend;

// ---- externs of other units (no GLOBAL annotation) -----------------------------------------------------------------------
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat);	// 0x100109fd (the engine's cutil.cpp copy)
void d3d_GetScreenFormat(PFormat *pFormat);		// 0x1001dec3 (d3d_surface unit)
void d3d_SetModulateAlphaTextureStates();							// 0x100139f0 guess: sets the stage 0 texture blend to modulate

// ---- d3d_optimizedsurface ---------------------------------------------------------------------------------------------

#define NUM_VALIDTILESIZES ((int)(sizeof(g_ValidTileSizes) / sizeof(g_ValidTileSizes[0])))

// NAME: d3d_GetValidTileSize: Jupiter d3d_optimizedsurface.cpp (here static: both callers inline it, no out-of-line copy exists)
static uint32 d3d_GetValidTileSize(uint32 size)
{
	for (int i = 0; i < NUM_VALIDTILESIZES; ++i)
	{
		if (size < g_ValidTileSizes[i])
		{
			return g_ValidTileSizes[i];
		}
	}
	return g_ValidTileSizes[NUM_VALIDTILESIZES - 1];
}

// NAME: d3d_DestroyTiles: names_proposal.csv (high, Jupiter d3d_optimizedsurface.cpp d3d_DestroyTiles)
// FUNCTION: D3DREN 0x1001bf10
void d3d_DestroyTiles(RSurface *pSurface)
{
	if (pSurface->m_pTiles)
	{
		uint32 count = pSurface->m_pTiles->m_nTilesX * pSurface->m_pTiles->m_nTilesY;
		for (uint32 i = 0; i < count; ++i)
		{
			SurfaceTile *pTile = &pSurface->m_pTiles->m_Tiles[i];
			if (pTile->m_pTexture)
				pTile->m_pTexture->Release();
		}

		dfree(pSurface->m_pTiles);
		pSurface->m_pTiles = NULL;
	}
}

// NAME: d3d_SetSolidAlpha: names_proposal.csv (high, Jupiter d3d_SetSolidAlpha; the colour argument is unused here too)
// FUNCTION: D3DREN 0x1001bf63
void d3d_SetSolidAlpha(FMConvertRequest *pRequest, uint32 destAlpha32, GenericColor &tColor)
{
	uint8 *pDestLine = pRequest->m_pDest;
	uint32 destAlpha16 = (uint16)destAlpha32;

	// Supports 16 and 32-bit..
	uint32 yCounter = pRequest->m_Height;
	while (yCounter)
	{
		--yCounter;

		uint32 xCounter = pRequest->m_Width;
		if (pRequest->m_pDestFormat->m_eType == BPP_16)
		{
			uint16 *pDest16 = (uint16 *)pDestLine;
			while (xCounter)
			{
				xCounter--;
				*pDest16 |= destAlpha16;
				++pDest16;
			}
		}
		else
		{
			uint32 *pDest32 = (uint32 *)pDestLine;
			while (xCounter)
			{
				--xCounter;
				*pDest32 |= destAlpha32;
				++pDest32;
			}
		}

		pDestLine += pRequest->m_DestPitch;
	}
}

#define DO_MASK_LOOP(srcType, destType, srcIt, destIt, keyType, keyVal, srcMask)	\
	xCounter = pRequest->m_Width;													\
	keyType colorKey = keyVal;														\
	srcIt = (srcType *)pSrcLine;													\
	destIt = (destType *)pDestLine;													\
	while (xCounter)																\
	{																				\
		xCounter--;																	\
		if ((*srcIt & ~srcMask) != colorKey)										\
			*destIt |= destAlpha32;													\
		else																		\
			*destIt &= ~destAlpha32;												\
		srcIt++; destIt++;															\
	}

// NAME: d3d_DoAlphaFromColorKey: names_proposal.csv (medium, Jupiter d3d_DoAlphaFromColorKey; the Talon version takes the
// source alpha mask as an extra argument and clears the dest alpha of the colour key pixels in all four format cases)
// FUNCTION: D3DREN 0x1001bfb4
void d3d_DoAlphaFromColorKey(FMConvertRequest *pRequest, uint32 srcAlpha32, uint32 destAlpha32, GenericColor &tColor)
{
	uint32 yCounter, xCounter;
	uint32 *pSrc32, *pDest32;
	uint16 *pSrc16, *pDest16;
	uint8 *pSrcLine, *pDestLine;

	pSrcLine = pRequest->m_pSrc;
	pDestLine = pRequest->m_pDest;

	// Supports 16 and 32-bit..
	yCounter = pRequest->m_Height;
	while (yCounter)
	{
		--yCounter;

		if (pRequest->m_pSrcFormat->m_eType == BPP_16)
		{
			if (pRequest->m_pDestFormat->m_eType == BPP_16)
			{
				DO_MASK_LOOP(uint16, uint16, pSrc16, pDest16, uint16, tColor.wVal, (uint16)srcAlpha32)
			}
			else if (pRequest->m_pDestFormat->m_eType == BPP_32)
			{
				DO_MASK_LOOP(uint16, uint32, pSrc16, pDest32, uint16, tColor.wVal, (uint16)srcAlpha32)
			}
		}
		else
		{
			if (pRequest->m_pDestFormat->m_eType == BPP_16)
			{
				DO_MASK_LOOP(uint32, uint16, pSrc32, pDest16, uint32, tColor.dwVal, srcAlpha32)
			}
			else if (pRequest->m_pDestFormat->m_eType == BPP_32)
			{
				DO_MASK_LOOP(uint32, uint32, pSrc32, pDest32, uint32, tColor.dwVal, srcAlpha32)
			}
		}

		pSrcLine += pRequest->m_SrcPitch;
		pDestLine += pRequest->m_DestPitch;
	}
}

// NAME: d3d_FillSurfaceTiles: names_proposal.csv (high, Jupiter d3d_FillSurfaceTiles)
// FUNCTION: D3DREN 0x1001c133
LTBOOL d3d_FillSurfaceTiles(RSurface *pSurface, TextureFormat *pDestFormat, PValue transparentColor)
{
	// setup the formats we will be using, we convert from the image format to the screen format
	LTRect rect;
	FMConvertRequest cRequest;
	PFormat srcFormat, destFormat;
	d3d_GetScreenFormat(&srcFormat);

	DDPFToPFormat(&pDestFormat->m_PF, &destFormat);

	GenericColor tColor;
	g_FormatMgr.PValueToFormatColor(&srcFormat, transparentColor, tColor);
	if (srcFormat.GetBytesPerPixel() == 4)
	{
		tColor.dwVal &= ~srcFormat.m_Masks[CP_ALPHA];
	}
	else
	{
		tColor.wVal &= ~srcFormat.m_Masks[CP_ALPHA];
	}

	cRequest.m_pSrcFormat = &srcFormat;
	cRequest.m_pDestFormat = &destFormat;
	cRequest.m_Flags = 0;
	pSurface->m_bTilesTransparent = !(transparentColor == 0xFFFFFFFF);

	// Convert the data over..
	DDSURFACEDESC2 DestDesc;
	DDSURFACEDESC2 SrcDesc;
	memset(&SrcDesc, 0, sizeof(SrcDesc));
	SrcDesc.dwSize = sizeof(SrcDesc);
	HRESULT hResult = pSurface->m_pSurface->Lock(NULL, &SrcDesc, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
	if (hResult == DD_OK)
	{
		uint8 *pSrcData = (uint8 *)SrcDesc.lpSurface;

		for (uint32 x = 0; x < pSurface->m_pTiles->m_nTilesX; x++)
		{
			for (uint32 y = 0; y < pSurface->m_pTiles->m_nTilesY; y++)
			{
				SurfaceTile *pTile = &pSurface->m_pTiles->m_Tiles[y * pSurface->m_pTiles->m_nTilesX + x];

				memset(&DestDesc, 0, sizeof(DestDesc));
				DestDesc.dwSize = sizeof(DestDesc);
				hResult = pTile->m_pTexture->Lock(NULL, &DestDesc, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
				if (hResult == DD_OK)
				{
					rect.left = x * 64;
					rect.right = rect.left + 64;
					if (rect.right > (int)pSurface->m_Desc.dwWidth)
						rect.right = pSurface->m_Desc.dwWidth;
					rect.top = y * 64;
					rect.bottom = rect.top + 64;
					if (rect.bottom > (int)pSurface->m_Desc.dwHeight)
						rect.bottom = pSurface->m_Desc.dwHeight;
					pTile->m_SrcImageRect = rect;

					// Setup the ConvertRequest.
					cRequest.m_pSrc = &pSrcData[rect.top * SrcDesc.lPitch];
					cRequest.m_pSrc += rect.left << srcFormat.GetBytesPerPixelShift();
					cRequest.m_SrcPitch = SrcDesc.lPitch;
					cRequest.m_pDest = (uint8 *)DestDesc.lpSurface;
					cRequest.m_DestPitch = DestDesc.lPitch;
					cRequest.m_Width = rect.right - rect.left;
					cRequest.m_Height = rect.bottom - rect.top;

					if (g_FormatMgr.ConvertPixels(&cRequest) == LT_OK)
					{
						// Set the alpha if we need to.
						if (transparentColor == 0xFFFFFFFF)
						{
							d3d_SetSolidAlpha(&cRequest, destFormat.m_Masks[CP_ALPHA], tColor);
						}
						else
						{
							d3d_DoAlphaFromColorKey(&cRequest, srcFormat.m_Masks[CP_ALPHA], destFormat.m_Masks[CP_ALPHA], tColor);
						}
					}

					pTile->m_pTexture->Unlock(NULL);
				}
				else
				{
					pSurface->m_pSurface->Unlock(NULL);
					return LTFALSE;
				}
			}
		}

		pSurface->m_pSurface->Unlock(NULL);
		return LTTRUE;
	}
	else
	{
		return LTFALSE;
	}
}

// NAME: d3d_UnoptimizeSurface: names_proposal.csv (medium, RenderStruct::UnoptimizeSurface; Jupiter d3d_UnoptimizeSurface)
// FUNCTION: D3DREN 0x1001c3d2
void d3d_UnoptimizeSurface(HLTBUFFER hBuffer)
{
	if (!hBuffer)
		return;

	d3d_DestroyTiles((RSurface *)hBuffer);
}

// NAME: d3d_OptimizeSurface: names_proposal.csv (medium, RenderStruct::OptimizeSurface; Jupiter d3d_OptimizeSurface)
// FUNCTION: D3DREN 0x1001c3e4
LTBOOL d3d_OptimizeSurface(HLTBUFFER hBuffer, PValue transparentColor)
{
	if (!hBuffer || !g_OptimizeSurfaces)
		return LTFALSE;

	RSurface *pSurface = (RSurface *)hBuffer;

	if (g_DeviceTriangleTextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
		return LTFALSE;

	// Do we have a texture format we can use?
	TextureFormat *pFormat = g_TextureFormats[FORMAT_INTERFACE];
	if (!pFormat)
		return LTFALSE;

	// Create the tiles if there aren't any yet.
	if (!pSurface->m_pTiles)
	{
		// How many tiles do we need?
		uint32 nTilesX = pSurface->m_Desc.dwWidth >> 6;
		uint32 nTilesY = pSurface->m_Desc.dwHeight >> 6;
		if ((nTilesX << 6) != pSurface->m_Desc.dwWidth)
			nTilesX++;
		if ((nTilesY << 6) != pSurface->m_Desc.dwHeight)
			nTilesY++;

		if (nTilesX == 0 && nTilesY == 0)
			return LTFALSE;

		// allocate the memory and setup the tile holder
		pSurface->m_pTiles = (SurfaceTiles *)dalloc_z(sizeof(SurfaceTiles) + sizeof(SurfaceTile) * ((nTilesX * nTilesY) - 1));
		if (!pSurface->m_pTiles)
			return LTFALSE;

		pSurface->m_pTiles->m_nTilesX = nTilesX;
		pSurface->m_pTiles->m_nTilesY = nTilesY;

		// now go through each tile and setup the dimensions
		for (uint32 x = 0; x < nTilesX; ++x)
		{
			uint32 tileXSize = pSurface->m_Desc.dwWidth - x * 64;
			tileXSize = d3d_GetValidTileSize(tileXSize);

			for (uint32 y = 0; y < nTilesY; ++y)
			{
				uint32 tileYSize = pSurface->m_Desc.dwHeight - y * 64;
				tileYSize = d3d_GetValidTileSize(tileYSize);

				SurfaceTile *pTile = &pSurface->m_pTiles->m_Tiles[y * nTilesX + x];

				// handle square textures
				if (g_DeviceTriangleTextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
				{
					pTile->m_nTileWidth = LTMAX(tileXSize, tileYSize);
					pTile->m_nTileHeight = LTMAX(tileXSize, tileYSize);
				}
				else
				{
					pTile->m_nTileWidth = tileXSize;
					pTile->m_nTileHeight = tileYSize;
				}

				// Create the texture surface.
				DDSURFACEDESC2 desc;
				memset(&desc, 0, sizeof(desc));
				desc.dwSize = sizeof(desc);
				desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
				desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
				desc.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
				desc.dwWidth = pTile->m_nTileWidth;
				desc.dwHeight = pTile->m_nTileHeight;
				memcpy(&desc.ddpfPixelFormat, &pFormat->m_PF, sizeof(DDPIXELFORMAT));
				HRESULT hResult = g_pDD->CreateSurface(&desc, &pTile->m_pTexture, NULL);
				if (hResult != DD_OK)
				{
					d3d_DestroyTiles(pSurface);
					return LTFALSE;
				}
			}
		}
	}

	// Convert the data.
	if (!d3d_FillSurfaceTiles(pSurface, pFormat, transparentColor))
	{
		d3d_DestroyTiles(pSurface);
		return LTFALSE;
	}

	pSurface->m_LastTransparentColor = transparentColor;
	return LTTRUE;
}

// NAME: d3d_StartOptimized2D: names_proposal.csv (medium, RenderStruct::StartOptimized2D; Jupiter d3d_StartOptimized2D)
// FUNCTION: D3DREN 0x1001c627
LTBOOL d3d_StartOptimized2D()
{
	if (!g_pD3DDevice || !g_bIn3D)
		return LTFALSE;
	if (g_bInOptimized2D)
		return LTTRUE;

	// Set states...
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, (DWORD *)&g_OldFogEnable);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, FALSE);

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, FALSE);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);

	if (!g_FilterOptimized && g_Bilinear)
	{
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_POINT);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_POINT);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTFP_POINT);
	}

	d3d_SetModulateAlphaTextureStates();

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);

	g_bInOptimized2D = TRUE;
	return TRUE;
}

// NAME: d3d_EndOptimized2D: names_proposal.csv (medium, RenderStruct::EndOptimized2D; Jupiter d3d_EndOptimized2D)
// FUNCTION: D3DREN 0x1001c725
void d3d_EndOptimized2D()
{
	if (!g_pD3DDevice || !g_bIn3D)
		return;
	if (!g_bInOptimized2D)
		return;

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, TRUE);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, g_OldFogEnable);

	if (!g_FilterOptimized && g_Bilinear)
	{
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_LINEAR);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTFP_POINT);
	}

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);

	g_bInOptimized2D = FALSE;
}

// NAME: d3d_IsInOptimized2D: names_proposal.csv (medium, Jupiter d3d_optimizedsurface.cpp)
// FUNCTION: D3DREN 0x1001c7eb
LTBOOL d3d_IsInOptimized2D()
{
	return g_bInOptimized2D;
}

// NAME: d3d_SetOptimized2DBlend: names_proposal.csv (medium, Jupiter d3d_optimizedsurface.cpp)
// FUNCTION: D3DREN 0x1001c7f1
LTBOOL d3d_SetOptimized2DBlend(LTSurfaceBlend blend)
{
	g_Optimized2DBlend = blend;

	switch (blend)
	{
		case LTSURFACEBLEND_ALPHA:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
			break;
		}
		case LTSURFACEBLEND_SOLID:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
			break;
		}
		case LTSURFACEBLEND_ADD:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
			break;
		}
		case LTSURFACEBLEND_MULTIPLY:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
			break;
		}
		case LTSURFACEBLEND_MULTIPLY2:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTCOLOR);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
			break;
		}
		case LTSURFACEBLEND_MASK:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCCOLOR);
			break;
		}
		case LTSURFACEBLEND_MASKADD:
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCCOLOR);
			break;
		}
		default:
		{
			return LTFALSE;
		}
	}

	return LTTRUE;
}

// NAME: d3d_SetOptimized2DColor: names_proposal.csv (medium, Jupiter d3d_optimizedsurface.cpp)
// FUNCTION: D3DREN 0x1001c8b8
LTBOOL d3d_SetOptimized2DColor(HLTCOLOR color)
{
	g_Optimized2DColor = 0xFF000000 | (color & 0xFFFFFF);

	return LTTRUE;
}

// NAME: d3d_GetOptimized2DBlend: names_proposal.csv (medium, Jupiter d3d_optimizedsurface.cpp)
// FUNCTION: D3DREN 0x1001c8ca
LTBOOL d3d_GetOptimized2DBlend(LTSurfaceBlend &blend)
{
	blend = g_Optimized2DBlend;
	return LTTRUE;
}

// NAME: d3d_GetOptimized2DColor: names_proposal.csv (medium, Jupiter d3d_optimizedsurface.cpp)
// FUNCTION: D3DREN 0x1001c8da
LTBOOL d3d_GetOptimized2DColor(HLTCOLOR &color)
{
	color = g_Optimized2DColor;
	return LTTRUE;
}

// NAME: InvalidateRect: names_proposal.csv (high, Jupiter dirtyrect.cpp InvalidateRect; unit dirtyrect)
void InvalidateRect(LTRect *pRect);		// 0x10022151

// Sets the texture coordinates of a vertex (Jupiter 3d_ops.h TLVertex::SetTCoords, here a local helper: the 0x20 TLVertex of
// tlvertex.h has no such member).
static inline void SetTCoords(TLVertex *pVert, float u, float v)
{
	pVert->tu = u;
	pVert->tv = v;
}

// NAME: d3d_BlitToScreen3D: names_proposal.csv (high, Jupiter d3d_BlitToScreen3D_Old shape: the Talon version walks the source
// rectangle in 64 pixel steps, one DrawPrimitive triangle fan per tile)
// FUNCTION: D3DREN 0x1001c8ea
void d3d_BlitToScreen3D(BlitRequest *pRequest)
{
	RSurface *pRSurface = (RSurface *)pRequest->m_hBuffer;

	if (pRequest->m_TransparentColor.dwVal != pRSurface->m_LastTransparentColor)
		d3d_OptimizeSurface(pRSurface, pRequest->m_TransparentColor.dwVal);

	SurfaceTiles *pTiles = pRSurface->m_pTiles;
	if (!pTiles)
		return;

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, g_Optimized2DBlend != LTSURFACEBLEND_SOLID);

	LTRect *pSrcRect = pRequest->m_pSrcRect;
	LTRect *pDestRect = pRequest->m_pDestRect;
	float srcRectWidth = (float)(pSrcRect->right - pSrcRect->left);
	float srcRectHeight = (float)(pSrcRect->bottom - pSrcRect->top);
	float destRectWidth = (float)(pDestRect->right - pDestRect->left);
	float destRectHeight = (float)(pDestRect->bottom - pDestRect->top);

	// Init default stuff in the verts.
	TLVertex verts[4];
	verts[0].color = verts[1].color = verts[2].color = verts[3].color = g_Optimized2DColor;
	verts[0].rgb.a = verts[1].rgb.a = verts[2].rgb.a = verts[3].rgb.a = (uint8)(pRequest->m_Alpha * 255.0f);
	verts[0].rhw = verts[1].rhw = verts[2].rhw = verts[3].rhw = 1.0f;

	// Remember the previous texture in there.
	IDirectDrawSurface7 *pOldTexture = NULL;
	g_pD3DDevice->GetTexture(0, &pOldTexture);

	// Draw each tile.
	int x, y, nextX, nextY;
	x = pSrcRect->left;
	if (x < pSrcRect->right)
	do
	{
		int nTileX = x / 64;
		SurfaceTile *pColTile = &pTiles->m_Tiles[nTileX];
		// Shape evidence: the exe stores nextX = x + 64 first and then narrows it (a one-expression LTMIN(.., LTMIN(x + 64, ..)) does not
		// store it early); the same for nextY below.
		nextX = x + 64;
		nextX = LTMIN(pColTile->m_SrcImageRect.right, LTMIN(nextX, pSrcRect->right));

		for (y = pSrcRect->top; y < pSrcRect->bottom; y = nextY)
		{
			SurfaceTile *pTile = &pTiles->m_Tiles[(y / 64) * pTiles->m_nTilesX + nTileX];
			nextY = y + 64;
			nextY = LTMIN(pTile->m_SrcImageRect.bottom, LTMIN(nextY, pSrcRect->bottom));

			float destLeft = ((float)(x - pSrcRect->left) / srcRectWidth) * destRectWidth + (float)pDestRect->left;
			float destRight = ((float)(nextX - pSrcRect->left) / srcRectWidth) * destRectWidth + (float)pDestRect->left;
			float destTop = ((float)(y - pSrcRect->top) / srcRectHeight) * destRectHeight + (float)pDestRect->top;
			float destBottom = ((float)(nextY - pSrcRect->top) / srcRectHeight) * destRectHeight + (float)pDestRect->top;

			float tDestLeft = (float)(x - pTile->m_SrcImageRect.left) / (float)pTile->m_nTileWidth;
			float tDestRight = (float)(nextX - pTile->m_SrcImageRect.left) / (float)pTile->m_nTileWidth;
			float tDestTop = (float)(y - pTile->m_SrcImageRect.top) / (float)pTile->m_nTileHeight;
			float tDestBottom = (float)(nextY - pTile->m_SrcImageRect.top) / (float)pTile->m_nTileHeight;

			// This adjusts for the DX fill convention so the optimized surfaces cover
			// the same pixels that nonoptimized surfaces would.
			destLeft = LTMAX(0.0f, destLeft - fSub);
			destTop = LTMAX(0.0f, destTop - fSub);
			destRight = LTMAX(0.0f, destRight - fSub);
			destBottom = LTMAX(0.0f, destBottom - fSub);

			// Draw the poly!
			verts[0].m_Vec.Init(destLeft, destTop, 1.0f);
			verts[1].m_Vec.Init(destRight, destTop, 1.0f);
			verts[2].m_Vec.Init(destRight, destBottom, 1.0f);
			verts[3].m_Vec.Init(destLeft, destBottom, 1.0f);

			SetTCoords(&verts[0], tDestLeft, tDestTop);
			SetTCoords(&verts[1], tDestRight, tDestTop);
			SetTCoords(&verts[2], tDestRight, tDestBottom);
			SetTCoords(&verts[3], tDestLeft, tDestBottom);

			g_pD3DDevice->SetTexture(0, pTile->m_pTexture);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, verts, 4, 0);
		}

		x = nextX;
	} while (x < pSrcRect->right);

	g_pD3DDevice->SetTexture(0, pOldTexture);
	InvalidateRect(pDestRect);
}

// NAME: d3d_WarpToScreen3D: names_proposal.csv (high, Jupiter d3d_WarpToScreen3D; the Talon version has no byAlpha local, no
// StateSet and no VERIFY_RENDERSTATE)
// FUNCTION: D3DREN 0x1001cc80
void d3d_WarpToScreen3D(BlitRequest *pRequest)
{
	RSurface *pRSurface = (RSurface *)pRequest->m_hBuffer;
	LTWarpPt *pWarpPts = pRequest->m_pWarpPts;
	IDirectDrawSurface7 *pOldTexture = NULL;
	SurfaceTiles *pTiles = pRSurface->m_pTiles;
	SurfaceTile *pTile;

	// If there aren't any tiles, get us out of here cause it's not an optimized surface
	if (!pTiles)
		return;

	// Get the number of tiles in this surface
	uint32 nXTiles = pRSurface->m_pTiles->m_nTilesX;
	uint32 nYTiles = pRSurface->m_pTiles->m_nTilesY;

	// Setup the widths of our surface that we're going to blit
	float srcRectWidth = (float)(pRequest->m_pSrcRect->right - pRequest->m_pSrcRect->left);
	float srcRectHeight = (float)(pRequest->m_pSrcRect->bottom - pRequest->m_pSrcRect->top);

	// Set the rendering device to allow for translucency
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, pRSurface->m_bTilesTransparent || (pRequest->m_Alpha != 1.0f));

	// Remember the previous texture so we can reset it at the end
	g_pD3DDevice->GetTexture(0, &pOldTexture);

	// Set all of the verticies to the same default values for color, alpha, and rhw
	TLVertex verts[12];
	verts[0].color = verts[1].color = verts[2].color = verts[3].color = g_Optimized2DColor;
	verts[0].rgb.a = verts[1].rgb.a = verts[2].rgb.a = verts[3].rgb.a = (uint8)(pRequest->m_Alpha * 255.0f);
	verts[0].rhw = verts[1].rhw = verts[2].rhw = verts[3].rhw = 1.0f;

	// Setup the directions of the x and y drawing planes
	LTVector vWarpStart(pWarpPts[0].dest_x, pWarpPts[0].dest_y, 1.0f);
	LTVector vWarpDirX = LTVector(pWarpPts[1].dest_x, pWarpPts[1].dest_y, 1.0f) - vWarpStart;
	LTVector vWarpDirY = LTVector(pWarpPts[3].dest_x, pWarpPts[3].dest_y, 1.0f) - vWarpStart;

	// Get the scale values
	float fScaleX = VEC_MAG(vWarpDirX) / srcRectWidth;
	float fScaleY = VEC_MAG(vWarpDirY) / srcRectHeight;

	// Set the vectors to the appropriate scale
	vWarpDirX.Norm(fScaleX);
	vWarpDirY.Norm(fScaleY);

	uint32 xPos, yPos = 0;

	// Calculate the values for our verticies based off of the passed in warp points
	for (uint32 i = 0; i < nYTiles; i++)
	{
		// Reset the X position
		xPos = 0;

		for (uint32 j = 0; j < nXTiles; j++)
		{
			// Get a pointer to the current tile
			pTile = &pTiles->m_Tiles[i * nXTiles + j];

			// Setup the destination points
			verts[0].m_Vec = vWarpStart + (vWarpDirX * (float)xPos) + (vWarpDirY * (float)yPos);
			verts[1].m_Vec = verts[0].m_Vec + (vWarpDirX * (float)pTile->m_nTileWidth);
			verts[2].m_Vec = verts[0].m_Vec + (vWarpDirX * (float)pTile->m_nTileWidth) + (vWarpDirY * (float)pTile->m_nTileHeight);
			verts[3].m_Vec = verts[0].m_Vec + (vWarpDirY * (float)pTile->m_nTileHeight);

			// Setup the source U,V coordinates
			SetTCoords(&verts[0], 0.0f, 0.0f);
			SetTCoords(&verts[1], 1.0f, 0.0f);
			SetTCoords(&verts[2], 1.0f, 1.0f);
			SetTCoords(&verts[3], 0.0f, 1.0f);

			// Set the texture to the correct tile, and draw it using the verticies
			d3d_SetTextureDirect(pTile->m_pTexture, 0);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, verts, 4, 0);

			// Increment the X position
			xPos += pTile->m_nTileWidth;
		}

		// Increment the Y position
		yPos += pTiles->m_Tiles[i * nXTiles].m_nTileHeight;
	}

	// Reset our texture to the one that was loaded before we entered this function
	g_pD3DDevice->SetTexture(0, pOldTexture);
}

// d3d_optimizedsurface (another part of this unit)
LTBOOL d3d_OptimizeSurface(HLTBUFFER hBuffer, PValue transparentColor);		// 0x1001c3e4
void d3d_BlitToScreen3D(BlitRequest *pRequest);								// 0x1001c8ea
void d3d_WarpToScreen3D(BlitRequest *pRequest);								// 0x1001cc80
// dirtyrect (0x10022151)
void InvalidateRect(LTRect *pRect);

// (defined in sys/d3d/d3d_surface)
void d3d_GetScreenFormat(PFormat *pFormat);
