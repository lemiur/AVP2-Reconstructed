// d3d.ren sys/d3d/d3d_surface (0x1001da60-0x1001e5a0): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// removes the auto_inline(off) stand-in around d3d_GetScreenFormat.
// FLAGS: /O1 /Ob2
#include <stdio.h>
#include <string.h>
#include "d3dren/d3d_surface.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/tlvertex.h"
#include "ltdynarray.h"
// GLOBAL: D3DREN 0x1004b4c0
extern uint32 g_ValidTileSizes[];

// ---- externs of other units (no GLOBAL annotation) -----------------------------------------------------------------------
extern FormatMgr g_FormatMgr;					// 0x10060710 (constructed by the static initialiser 0x1001e660)
void d3d_GetScreenFormat(PFormat *pFormat);		// 0x1001dec3 (d3d_surface unit)

// ---- d3d_optimizedsurface ---------------------------------------------------------------------------------------------

#define NUM_VALIDTILESIZES ((int)(sizeof(g_ValidTileSizes) / sizeof(g_ValidTileSizes[0])))

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

// NAME: InvalidateRect: names_proposal.csv (high, Jupiter dirtyrect.cpp InvalidateRect; unit dirtyrect)
void InvalidateRect(LTRect *pRect);		// 0x10022151

// ---- d3d_surface (0x1001da60) --------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1001da60 _$E3
// FUNCTION: D3DREN 0x1001da65 _$E2
// GLOBAL: D3DREN 0x100606d8
ConVar g_CV_LockOnFlip("LockOnFlip", 1.0f);


// NAME: g_ScreenLockRect, g_bScreenLocked: names_proposal.csv (high, Jupiter d3d_surface.cpp `RECT g_ScreenLockRect; bool g_bScreenLocked`)
// GLOBAL: D3DREN 0x100606f8
RECT g_ScreenLockRect;
// GLOBAL: D3DREN 0x10060708
int g_bScreenLocked = 0;

// d3d_optimizedsurface (another part of this unit)
LTBOOL d3d_OptimizeSurface(HLTBUFFER hBuffer, PValue transparentColor);		// 0x1001c3e4
void d3d_BlitToScreen3D(BlitRequest *pRequest);								// 0x1001c8ea
void d3d_WarpToScreen3D(BlitRequest *pRequest);								// 0x1001cc80
// dirtyrect (0x10022151)
void InvalidateRect(LTRect *pRect);

// NAME: LTRectToRect: Jupiter d3d_surface.cpp static inline helper (the exe copies the four coordinates one by one)
static inline void LTRectToRect(RECT *pDest, LTRect *pSrc)
{
	pDest->left = pSrc->left;
	pDest->top = pSrc->top;
	pDest->right = pSrc->right;
	pDest->bottom = pSrc->bottom;
}

// NAME: d3d_CreateSurface: names_proposal.csv (medium, RenderStruct::CreateSurface; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001da7f
HLTBUFFER d3d_CreateSurface(int width, int height)
{
	HRESULT hResult;
	IDirectDrawSurface7 *pSurface;
	RSurface *pRSurface;

	if (!g_pDD)
		return NULL;
	if (width == 0 || height == 0)
		return NULL;

	DDSURFACEDESC2 ddsd;
	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	ddsd.dwWidth = width;
	ddsd.dwHeight = height;
	hResult = g_pDD->CreateSurface(&ddsd, &pSurface, NULL);
	if (hResult != DD_OK)
		return NULL;

	pRSurface = (RSurface *)dalloc_z(sizeof(RSurface));
	if (!pRSurface)
	{
		pSurface->Release();
		return NULL;
	}

	pRSurface->m_pSurface = pSurface;
	pRSurface->m_LastTransparentColor = 0xFF000000;
	memset(&pRSurface->m_Desc, 0, sizeof(pRSurface->m_Desc));
	pRSurface->m_Desc.dwSize = sizeof(pRSurface->m_Desc);
	pSurface->GetSurfaceDesc(&pRSurface->m_Desc);

	return (HLTBUFFER)pRSurface;
}

// NAME: d3d_DeleteSurface: names_proposal.csv (medium, RenderStruct::DeleteSurface; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001db2d
void d3d_DeleteSurface(HLTBUFFER hSurf)
{
	RSurface *pRSurface;

	if (!hSurf)
		return;

	pRSurface = (RSurface *)hSurf;

	d3d_DestroyTiles(pRSurface);
	pRSurface->m_pSurface->Release();
	dfree(pRSurface);
}

// NAME: d3d_GetSurfaceInfo: names_proposal.csv (medium, RenderStruct::GetSurfaceInfo; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001db4e
void d3d_GetSurfaceInfo(HLTBUFFER hSurf, uint32 *pWidth, uint32 *pHeight, long *pPitch)
{
	if (!hSurf)
		return;
	RSurface *pRSurface = (RSurface *)hSurf;
	if (pWidth)
		*pWidth = pRSurface->m_Desc.dwWidth;
	if (pHeight)
		*pHeight = pRSurface->m_Desc.dwHeight;
	if (pPitch)
		*pPitch = pRSurface->m_Desc.lPitch;
}

// NAME: d3d_LockSurface: names_proposal.csv (medium, RenderStruct::LockSurface; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001db7e
void *d3d_LockSurface(HLTBUFFER hSurf)
{
	if (!hSurf)
		return NULL;
	RSurface *pRSurface = (RSurface *)hSurf;

	DDSURFACEDESC2 ddsd;
	ddsd.dwSize = sizeof(ddsd);
	HRESULT hResult = pRSurface->m_pSurface->Lock(NULL, &ddsd, 0, NULL);
	if (hResult == DD_OK)
		return ddsd.lpSurface;
	else
		return NULL;
}

// NAME: d3d_UnlockSurface: names_proposal.csv (medium, RenderStruct::UnlockSurface; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001dbb4
void d3d_UnlockSurface(HLTBUFFER hSurf)
{
	if (!hSurf)
		return;
	RSurface *pRSurface = (RSurface *)hSurf;
	pRSurface->m_pSurface->Unlock(NULL);
}

// NAME: d3d_LockScreen: names_proposal.csv (medium, RenderStruct::LockScreen; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001dbca
LTBOOL d3d_LockScreen(int left, int top, int right, int bottom, void **pData, long *pPitch)
{
	if (g_pOffscreen && !g_bScreenLocked)
	{
		g_ScreenLockRect.left = left;
		g_ScreenLockRect.top = top;
		g_ScreenLockRect.right = right;
		g_ScreenLockRect.bottom = bottom;

		DDSURFACEDESC2 ddsd;
		ddsd.dwSize = sizeof(ddsd);
		HRESULT hResult = g_pOffscreen->Lock(&g_ScreenLockRect, &ddsd, DDLOCK_WAIT, NULL);
		if (hResult == DD_OK)
		{
			if (pData)
				*pData = ddsd.lpSurface;
			if (pPitch)
				*pPitch = ddsd.lPitch;
			g_bScreenLocked = 1;
			return LTTRUE;
		}
	}
	return LTFALSE;
}

// NAME: d3d_UnlockScreen: names_proposal.csv (medium, RenderStruct::UnlockScreen; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001dc4f
void d3d_UnlockScreen()
{
	if (g_pOffscreen && g_bScreenLocked)
	{
		g_pOffscreen->Unlock(&g_ScreenLockRect);
		g_bScreenLocked = 0;
		InvalidateRect((LTRect *)&g_ScreenLockRect);
	}
}

// NAME: d3d_BlitFromScreen: names_proposal.csv (medium, RenderStruct::BlitFromScreen; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001dc81
void d3d_BlitFromScreen(BlitRequest *pRequest)
{
	RSurface *pRSurface = (RSurface *)pRequest->m_hBuffer;

	// Spit out a warning if we are in 3D..
	if (g_bIn3D)
	{
		AddDebugMessage(20, "Warning: drawing a nonoptimized surface while in 3D mode.");
		if (g_pD3DDevice)
			g_pD3DDevice->EndScene();
	}

	RECT srcRect;
	LTRectToRect(&srcRect, pRequest->m_pSrcRect);
	RECT destRect;
	LTRectToRect(&destRect, pRequest->m_pDestRect);

	DDBLTFX ddbltfx;
	ddbltfx.dwSize = sizeof(ddbltfx);
	pRSurface->m_pSurface->Blt(&destRect, g_pOffscreen, &srcRect, 0, &ddbltfx);

	if (g_bIn3D)
	{
		if (g_pD3DDevice)
			g_pD3DDevice->BeginScene();
	}
}

// NAME: d3d_ReallyBlitToScreen: names_proposal.csv (high, Jupiter d3d_surface.cpp d3d_ReallyBlitToScreen)
// FUNCTION: D3DREN 0x1001dd30
void d3d_ReallyBlitToScreen(BlitRequest *pRequest)
{
	RSurface *pRSurface = (RSurface *)pRequest->m_hBuffer;

	// Spit out a warning if we are in 3D..
	if (g_bIn3D)
	{
		AddDebugMessage(20, "Warning: drawing a nonoptimized surface while in 3D mode.");
		if (g_pD3DDevice)
			g_pD3DDevice->EndScene();
	}

	RECT srcRect;
	LTRectToRect(&srcRect, pRequest->m_pSrcRect);
	RECT destRect;
	LTRectToRect(&destRect, pRequest->m_pDestRect);

	DWORD dwFlags = 0;
	DDBLTFX ddbltfx;
	ddbltfx.dwSize = sizeof(ddbltfx);

	if (pRequest->m_BlitOptions & BLIT_TRANSPARENT)
	{
		if (pRSurface->m_LastTransparentColor != pRequest->m_TransparentColor.dwVal)
		{
			DDCOLORKEY colorKey;
			colorKey.dwColorSpaceLowValue = pRequest->m_TransparentColor.dwVal;
			colorKey.dwColorSpaceHighValue = pRequest->m_TransparentColor.dwVal;
			pRSurface->m_pSurface->SetColorKey(DDCKEY_SRCBLT, &colorKey);
			pRSurface->m_LastTransparentColor = pRequest->m_TransparentColor.dwVal;
		}
		dwFlags = DDBLT_KEYSRC;
	}

	g_pOffscreen->Blt(&destRect, pRSurface->m_pSurface, &srcRect, dwFlags, &ddbltfx);

	if (g_bIn3D)
	{
		if (g_pD3DDevice)
			g_pD3DDevice->BeginScene();
	}

	InvalidateRect((LTRect *)&destRect);
}

// NAME: d3d_BlitToScreen: names_proposal.csv (medium, RenderStruct::BlitToScreen; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001de1c
void d3d_BlitToScreen(BlitRequest *pRequest)
{
	if (!g_pOffscreen || !pRequest || !pRequest->m_hBuffer)
		return;

	RSurface *pRSurface = (RSurface *)pRequest->m_hBuffer;

	// Optimize the surface if we need to use a blend mode
	if (g_bInOptimized2D && g_bIn3D && (pRSurface->m_pTiles == NULL))
	{
		d3d_OptimizeSurface(pRequest->m_hBuffer, pRequest->m_TransparentColor.dwVal);
	}

	// Can this be drawn as an optimized 2D surface?
	if (pRSurface->m_pTiles && g_bInOptimized2D && g_bIn3D)
	{
		d3d_BlitToScreen3D(pRequest);
	}
	else
	{
		d3d_ReallyBlitToScreen(pRequest);
	}
}

// NAME: d3d_WarpToScreen: names_proposal.csv (medium, RenderStruct::WarpToScreen; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001de85
LTBOOL d3d_WarpToScreen(BlitRequest *pRequest)
{
	if (!g_pOffscreen || !pRequest || !pRequest->m_hBuffer)
		return LTFALSE;

	RSurface *pRSurface = (RSurface *)pRequest->m_hBuffer;

	// Can this be drawn as an optimized 2D surface?
	if (pRSurface->m_pTiles && g_bInOptimized2D && g_bIn3D)
	{
		d3d_WarpToScreen3D(pRequest);
		return LTTRUE;
	}
	return LTFALSE;
}

extern PFormat DAT_100577c8;	// 0x100577c8 the screen format (defined by the device bring-up object, sys/d3d/common_init)
extern FormatMgr g_FormatMgr;	// 0x10060710

// The implicit PFormat::operator= (the exe has the out-of-line copy here; the vptr is not copied).
// FUNCTION: D3DREN 0x1001ded2 ??4PFormat@@QAEAAV0@ABV0@@Z
// NAME: d3d_GetScreenFormat: names_proposal.csv (medium; RenderStruct slot GetScreenFormat, Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001dec3
// Its caller d3d_FillSurfaceTiles (0x1001c133) is in the d3d_optimizedsurface object, which only has the declaration (Jupiter
// d3d_optimizedsurface.cpp: `extern bool d3d_GetScreenFormat(PFormat *pFormat);`), so the exe calls it.
void d3d_GetScreenFormat(PFormat *pFormat)
{
	*pFormat = DAT_100577c8;
}

// NAME: d3d_MakeScreenShot: names_proposal.csv (medium; RenderStruct slot MakeScreenShot; the body is Jupiter's
// d3d_MakeScreenShotBMP on a DirectDraw 7 surface, the messages are the "ScreenShot: ..." strings of d3d.ren)
// (Matched once the module switched to the original RTM compiler: the shared failure print is the fopen block.)
// FUNCTION: D3DREN 0x1001df19
void d3d_MakeScreenShot(const char *pFilename)
{
	FMConvertRequest request;
	CMoArray<uint32> outputBuf;
	DDSURFACEDESC2 ddsd;
	HRESULT hResult;
	LTRESULT dResult;
	FILE *fp;

	if (!g_pOffscreen)
		return;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	g_pOffscreen->GetSurfaceDesc(&ddsd);

	uint32 nWidth = ddsd.dwWidth;
	uint32 nHeight = ddsd.dwHeight;
	if (!outputBuf.SetSize(nHeight * nWidth))
		return;

	ddsd.dwSize = sizeof(ddsd);
	hResult = g_pOffscreen->Lock(NULL, &ddsd, DDLOCK_WAIT, NULL);
	if (hResult != DD_OK)
	{
		dsi_ConsolePrint("ScreenShot: g_pOffscreen->Lock returned %d.", hResult);
		return;
	}

	*request.m_pSrcFormat = DAT_100577c8;
	request.m_pSrc = (uint8 *)ddsd.lpSurface;
	request.m_SrcPitch = ddsd.lPitch;
	request.m_pDestFormat->Init(BPP_32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
	request.m_pDest = (uint8 *)outputBuf.GetArray();
	request.m_DestPitch = nWidth * sizeof(uint32);
	request.m_Width = nWidth;
	request.m_Height = nHeight;
	request.m_Flags = 0;
	dResult = g_FormatMgr.ConvertPixels(&request);
	g_pOffscreen->Unlock(NULL);
	if (dResult != 0)
	{
		dsi_ConsolePrint("ScreenShot: FormatMgr::ConvertPixels returned %d.", dResult);
		return;
	}

	fp = fopen(pFilename, "wb");
	if (!fp)
	{
		dsi_ConsolePrint("ScreenShot: fopen(%s) failed.", pFilename);
		return;
	}

	BITMAPFILEHEADER fileHeader;
	BITMAPINFOHEADER infoHeader;
	memset(&fileHeader, 0, sizeof(fileHeader));
	memset(&infoHeader, 0, sizeof(infoHeader));

	fileHeader.bfType = ('M' << 8) | 'B';
	fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	fileHeader.bfSize = (nHeight * nWidth + 0x12) * 3;

	infoHeader.biSize = sizeof(infoHeader);
	infoHeader.biWidth = nWidth;
	infoHeader.biHeight = nHeight;
	infoHeader.biPlanes = 1;
	infoHeader.biBitCount = 24;
	infoHeader.biCompression = BI_RGB;

	fwrite(&fileHeader, sizeof(fileHeader), 1, fp);
	fwrite(&infoHeader, sizeof(infoHeader), 1, fp);

	for (uint32 y = 0; y < nHeight; ++y)
	{
		uint32 *pOutLine = &outputBuf[(nHeight - y - 1) * nWidth];
		for (uint32 x = 0; x < nWidth; ++x)
		{
			fwrite(pOutLine, 3, 1, fp);
			++pOutLine;
		}
	}

	fclose(fp);
	dsi_ConsolePrint("ScreenShot: Created %s successfully.", pFilename);
}

// ---- d3d_SwapBuffers -----------------------------------------------------------------------------------------------------

extern int DAT_10057e24;		// 0x10057e24 guess: horizontal stretch factor of the window blit
extern int DAT_10057e28;		// 0x10057e28 guess: vertical stretch factor
void DirtyRectSwap();			// 0x100223c8 (dirtyrect, another unit)
void ClearDirtyRects();			// 0x1002241c

// NAME: d3d_SwapBuffers: names_proposal.csv (medium; RenderStruct slot SwapBuffers; Jupiter d3d_surface.cpp)
// FUNCTION: D3DREN 0x1001e189
void d3d_SwapBuffers(uint32 flags)
{
	DDSURFACEDESC2 ddsd;
	DDBLTFX bltfx;
	RECT srcRect;
	RECT lockRect;
	RECT clientRect;
	RECT destRect;
	POINT pt;
	HWND hWnd;

	if (g_pBackBuffer)
	{
		if (flags & 4)
		{
			DirtyRectSwap();
		}
		else
		{
			if (g_CV_LockOnFlip.m_IntVal && !g_bScreenLocked)
			{
				lockRect.left = 0;
				lockRect.top = 0;
				lockRect.right = 4;
				lockRect.bottom = 4;
				ddsd.dwSize = sizeof(ddsd);
				if (g_pOffscreen->Lock(&lockRect, &ddsd, DDLOCK_WAIT, NULL) >= 0)
					g_pOffscreen->Unlock(&lockRect);
			}

			if (g_pPrimary)
			{
				if (g_bRunWindowed)
				{
					GetClientRect(g_hWnd, &clientRect);
					pt.x = clientRect.left;
					pt.y = clientRect.top;
					ClientToScreen(g_hWnd, &pt);
					hWnd = g_hWnd;
				}
				else
				{
					hWnd = GetDesktopWindow();
					GetWindowRect(hWnd, &clientRect);
					pt.x = clientRect.left;
					pt.y = clientRect.top;
				}

				if (!g_pClipper && g_pDD)
				{
					if (g_pDD->CreateClipper(0, &g_pClipper, NULL) == 0 && g_pClipper)
					{
						g_pClipper->SetHWnd(0, hWnd);
						g_pPrimary->SetClipper(g_pClipper);
					}
				}

				srcRect.left = 0;
				srcRect.top = 0;
				srcRect.right = g_ScreenWidth;
				srcRect.bottom = g_ScreenHeight;
				destRect.left = pt.x;
				destRect.top = pt.y;
				destRect.right = DAT_10057e24 * g_ScreenWidth + pt.x;
				destRect.bottom = DAT_10057e28 * g_ScreenHeight + pt.y;
				memset(&bltfx, 0, sizeof(bltfx));
				bltfx.dwSize = sizeof(bltfx);
				destRect.left = pt.x;
				destRect.top = pt.y;
				destRect.right = g_ScreenWidth + pt.x;
				destRect.bottom = g_ScreenHeight + pt.y;
				memset(&bltfx, 0, sizeof(bltfx));
				bltfx.dwSize = sizeof(bltfx);
				g_pPrimary->Blt(&destRect, g_pBackBuffer, &srcRect, DDBLT_WAIT, &bltfx);
			}
			else
			{
				if (flags & 2)
					g_pBackBuffer->Blt(NULL, g_pOffscreen, NULL, DDBLT_WAIT, NULL);
				else
					g_pBackBuffer->Flip(NULL, DDFLIP_WAIT);
			}
			ClearDirtyRects();
		}
	}
}

// ---- CMoArray<unsigned long> copies (ScreenShot's pixel buffer; vtable 0x10046340) ------------------------------------------
// Template instances of StdLith dynarray.h emitted by this object.  The other Gen* members and Init/SetSize2/_AllocateTArray
// of this instance were folded by the linker into identical copies elsewhere (0x1000e0c1.., 0x1003b034..).
// FUNCTION: D3DREN 0x1001e39c ??0?$CMoArray@KVDefaultCache@@@@QAE@XZ
// FUNCTION: D3DREN 0x1001e3be ?GenGetNext@?$CMoArray@KVDefaultCache@@@@UBEKAAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1001e3d2 ?GenGetSize@?$CMoArray@KVDefaultCache@@@@UBEKXZ
// FUNCTION: D3DREN 0x1001e3d6 ?GenFindElement@?$CMoArray@KVDefaultCache@@@@UBEHABKAAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1001e3fd ?GenSetCacheSize@?$CMoArray@KVDefaultCache@@@@UAEXK@Z
// FUNCTION: D3DREN 0x1001e407 ?Insert2@?$CMoArray@KVDefaultCache@@@@QAEHKABKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1001e4c6 ?Remove2@?$CMoArray@KVDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1001e57c ?_DeleteAndDestroyArray@?$CMoArray@KVDefaultCache@@@@AAEXPAVLAlloc@@K@Z
