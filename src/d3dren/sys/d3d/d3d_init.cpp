// d3d.ren sys/d3d/d3d_init (0x1001a380-0x1001bf10): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// one object (bss one hash-ordered block).
// FLAGS: /O2 /Ob2
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/lightmap.h"		// RenderContext
#include "pixelformat.h"			// PFormat
#include "d3dren/common_init.h"		// g_ScreenPixelFormat
#include "d3dren/polydraw.h"		// g_u8FogColorR/G/B

// ---- callees in other units (prototypes until their owners publish headers) -------------------------------------------------
int PageInLightmaps(RenderContext *pContext);		// W9 (lightmap): page the lightmaps in, 0 = failed
void FreeLightmapPages(RenderContext *pContext);		// W9: free the lightmap pages of the context
void d3d_ReinitLightmapTextureSupport();							// W8 (d3d_texture): guess_ReinitLightmapTextureSupport
void d3d_TermTextureManager();							// W8: CTextureManager::Term
void d3d_FreeAllTextures();							// W8: CTextureManager::FreeAllTextures
void d3d_ListTextureFormats();							// W8: CTextureManager::ListTextureFormats
void d3d_ListDevices();							// common_init: list the enumerated devices (LISTDEVICES)
void d3d_EndOptimized2D();						// 0x1001c725 (W7, d3d_surface)
void d3d_NullCallback();							// 10021d70 unit (empty)
void d3d_TermPolyDrawPools();							// 100132a0 unit
void d3d_NullPreFrameCallback();							// empty PreFrame stub
void d3d_TermObjectModules();					// 0x10028610
void d3d_InitObjectModules();					// 0x100285e0
void d3d_InitPolyDrawPools();							// 100132a0 unit
void d3d_FreeDDraw();							// common_init: release the DirectDraw objects

// ---- console variables (static initialisers; the original object's _$E numbers) ------------------------------------------
// FUNCTION: D3DREN 0x1001a380 _$E2
// GLOBAL: D3DREN 0x1005c818
ConVar g_CV_UseD3DClip("UseD3DClip", 0.0f);
// FUNCTION: D3DREN 0x1001a3a0 _$E5
// GLOBAL: D3DREN 0x1005c7e8
ConVar g_CV_MipMapBias("MipMapBias", 0.0f);
// FUNCTION: D3DREN 0x1001a3c0 _$E8
// GLOBAL: D3DREN 0x1005cdf8
ConVar g_CV_Trilinear("Trilinear", 0.0f);
// FUNCTION: D3DREN 0x1001a3e0 _$E11
// GLOBAL: D3DREN 0x1005c9a8
ConVar g_CV_Anisotropic("Anisotropic", 0.0f);

// ---- extra console variables (fog, dither, texture filtering) ---------------------------------------------------------------
// State caches of d3d_ReadExtraConsoleVariables: the values last sent to the device (names unknown).
// GLOBAL: D3DREN 0x10057ac8
int g_nLastDeviceFogEnable;			// guess: fog enable last sent
// GLOBAL: D3DREN 0x10058044
int g_nLastDeviceFogRed;			// guess: fog colour R last sent
// GLOBAL: D3DREN 0x10058048
int g_nLastDeviceFogGreen;			// guess: fog colour G last sent
// GLOBAL: D3DREN 0x1005804c
int g_nLastFogBlue;			// guess: fog colour B last sent
// GLOBAL: D3DREN 0x100580d0
float g_fLastFogNearZ;			// guess: fog near z last sent
// GLOBAL: D3DREN 0x100578e8
float g_fLastDeviceFogFarZ;			// guess: fog far z last sent
// GLOBAL: D3DREN 0x100582e0
int g_nLastTableFog;			// guess: TableFog last sent
// GLOBAL: D3DREN 0x100579e0
float g_fCenteredFogRed;			// guess: 2 * (fog colour R - 128)
// GLOBAL: D3DREN 0x100579e4
float g_fCenteredFogGreen;			// guess: 2 * (fog colour G - 128)
// GLOBAL: D3DREN 0x100579e8
float g_fCenteredFogBlue;			// guess: 2 * (fog colour B - 128)
// GLOBAL: D3DREN 0x100584e8
int g_bFogStateInitialized;			// guess: the fog state above has been sent (cleared by d3d_Init)
// GLOBAL: D3DREN 0x10057ed8
int g_nLastDeviceDither;			// guess: dither last sent
// GLOBAL: D3DREN 0x100584ec
int g_bDitherStateInitialized;			// guess: the dither state has been sent (cleared by d3d_Init)
// GLOBAL: D3DREN 0x100579ec
int g_nLastDeviceBilinear;			// guess: Bilinear last sent
// GLOBAL: D3DREN 0x100584f0
int g_bTextureFilterStateInitialized;			// guess: the filter states have been sent (cleared by d3d_Init)

// NAME: d3d_ReadExtraConsoleVariables: Jupiter d3d_init.cpp (fog from the console variables: FogNearZ == FogFarZ disables fog
// ("This handles a TNT bug"), SetRenderState FOGCOLOR/FOGSTART/FOGEND/FOGENABLE, DITHERENABLE, anisotropic/bilinear/trilinear
// filter states); the D3D7 original caches what it sent and only updates when a value changed.
// STUB diagnosis: the three clamped fog colour bytes (0x10058040..42) are not defined in this object (its own definitions made VC6 load
//   the first one as a masked dword; the bytes sit between common_stuff's console mirrors).  Byte-exact (MATCH, all relocations) once
//   they are one 3-byte object `uint8 g_u8FogColor[3]` ([0] r, [1] g, [2] b) in polydraw.h: as three separate externs VC6 keeps 0xff
//   as an immediate and schedules the FOGCOLOR packing differently (16 aligned mismatches).  The rename touches the readers in
//   unk/10007930, unk/100098d0 and unk/10022bc5 (all still match with it); the cache stores below are in the order that matches.
// STUB: D3DREN 0x1001a400
void d3d_ReadExtraConsoleVariables()
{
	if (g_pD3DDevice)
	{
		if (!(g_DeviceTriangleCaps.dwRasterCaps & D3DPRASTERCAPS_FOGTABLE))
			g_FogEnable = 0;
		if (g_FogNearZ == g_FogFarZ)
			g_FogEnable = 0;

		if (!g_bFogStateInitialized || g_nLastDeviceFogEnable != g_FogEnable || g_nLastDeviceFogRed != g_FogR || g_nLastDeviceFogGreen != g_FogG ||
			g_nLastFogBlue != g_FogB || fabs(g_fLastFogNearZ - g_FogNearZ) > 0.001f || fabs(g_fLastDeviceFogFarZ - g_FogFarZ) > 0.001f ||
			g_nLastTableFog != g_CV_TableFog.m_IntVal)
		{
			if (g_FogR < 0)
				g_u8FogColorR = 0;
			else
				g_u8FogColorR = (g_FogR > 255) ? 255 : g_FogR;
			if (g_FogG < 0)
				g_u8FogColorG = 0;
			else
				g_u8FogColorG = (g_FogG > 255) ? 255 : g_FogG;
			if (g_FogB < 0)
				g_u8FogColorB = 0;
			else
				g_u8FogColorB = (g_FogB > 255) ? 255 : g_FogB;

			if (g_CV_TableFog.m_IntVal)
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_LINEAR);
			else
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_NONE);
			g_nLastTableFog = g_CV_TableFog.m_IntVal;

			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, (((g_u8FogColorR << 8) | g_u8FogColorG) << 8) | g_u8FogColorB);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGSTART, *(DWORD *)&g_FogNearZ);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGEND, *(DWORD *)&g_FogFarZ);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, g_FogEnable);

			g_fCenteredFogRed = (float)(g_u8FogColorR - 0x80) * 2.0f;
			g_fCenteredFogGreen = (float)(g_u8FogColorG - 0x80) * 2.0f;
			g_fCenteredFogBlue = (float)(g_u8FogColorB - 0x80) * 2.0f;
			g_nLastDeviceFogEnable = g_FogEnable;
			g_nLastDeviceFogRed = g_FogR;
			g_nLastDeviceFogGreen = g_FogG;
			g_nLastFogBlue = g_FogB;
			g_fLastFogNearZ = g_FogNearZ;
			g_fLastDeviceFogFarZ = g_FogFarZ;
			g_bFogStateInitialized = 1;
		}

		if (!g_bDitherStateInitialized || g_nLastDeviceDither != g_Dither)
		{
			if (!g_pD3DDevice)
				return;
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DITHERENABLE, g_Dither);
			g_nLastDeviceDither = g_Dither;
			g_bDitherStateInitialized = 1;
		}

		if (g_pD3DDevice)
		{
			if (g_CV_Anisotropic.m_IntVal)
			{
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, 3);
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, 3);
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAXANISOTROPY, g_CV_Anisotropic.m_IntVal);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MINFILTER, 3);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MAGFILTER, 3);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MAXANISOTROPY, g_CV_Anisotropic.m_IntVal);
			}
			else
			{
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, (g_Bilinear != 0) + 1);
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, (g_Bilinear != 0) + 1);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MINFILTER, (g_Bilinear != 0) + 1);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MAGFILTER, (g_Bilinear != 0) + 1);
			}
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, (g_CV_Trilinear.m_IntVal != 0) + 2);
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPMAPLODBIAS, *(DWORD *)&g_CV_MipMapBias.m_FloatVal);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_MIPFILTER, (g_CV_Trilinear.m_IntVal != 0) + 2);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_MIPMAPLODBIAS, *(DWORD *)&g_CV_MipMapBias.m_FloatVal);
			g_bTextureFilterStateInitialized = 1;
			g_nLastDeviceBilinear = g_Bilinear;
		}
	}
}

// ---- Z buffer format enumeration ------------------------------------------------------------------------------------------
// Context of d3d_EnumZBufferFormatsCallback: set by d3d_CreateDeviceWithZBuffer to a count and an array on its own stack frame.
// GLOBAL: D3DREN 0x1005c99c
uint32 *g_pZBufferFormatCount;
// GLOBAL: D3DREN 0x1005de24
DDPIXELFORMAT *g_pEnumeratedZBufferFormats;

// NAME: CanDrawPortals: Jupiter d3d_device.h CD3D_Device::CanDrawPortals(), here a free function reading the stencil bit depth
// of the chosen Z buffer format (DDPIXELFORMAT::dwStencilBitDepth).
// FUNCTION: D3DREN 0x1001a850
int CanDrawPortals()
{
	return g_ChosenZBufferStencilBitDepth > 0;
}

// NAME: d3d_EnumZBufferFormatsCallback: LPD3DENUMPIXELFORMATSCALLBACK (DirectX SDK type); name from names_proposal.csv (medium).
// FUNCTION: D3DREN 0x1001a860
HRESULT WINAPI d3d_EnumZBufferFormatsCallback(LPDDPIXELFORMAT lpDDPixFmt, LPVOID lpContext)
{
	if (*g_pZBufferFormatCount < 0x40)
	{
		g_pEnumeratedZBufferFormats[*g_pZBufferFormatCount] = *lpDDPixFmt;
		*g_pZBufferFormatCount = *g_pZBufferFormatCount + 1;
		return D3DENUMRET_OK;
	}
	return D3DENUMRET_OK;
}

// NAME: d3d_EnumAttachedSurfacesCallback: LPDDENUMSURFACESCALLBACK7 (DirectX SDK type); name from names_proposal.csv (medium).
// FUNCTION: D3DREN 0x1001b6f0
HRESULT WINAPI d3d_EnumAttachedSurfacesCallback(LPDIRECTDRAWSURFACE7 lpDDSurface, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext)
{
	g_pOffscreen = lpDDSurface;
	return DDENUMRET_CANCEL;
}

// ---- RenderStruct slots: contexts, lightmap rebind -----------------------------------------------------------------------
// NAME: d3d_CreateContext: Jupiter d3d_init.cpp (same name, same job: dalloc_z a RenderContext and set m_CurFrameCode = 0xFFFF).
// FUNCTION: D3DREN 0x1001b700
HRENDERCONTEXT d3d_CreateContext(RenderContextInit *pInit)
{
	RenderContext *pContext = (RenderContext *)dalloc_z(sizeof(RenderContext));
	if (!pContext)
		return NULL;

	pContext->m_pWorld = pInit->m_pWorld;
	pContext->m_CurFrameCode = 0xFFFF;
	if (g_NoLMPages)
	{
		dsi_ConsolePrint("Warning: NoLMPages is TRUE");
		return pContext;
	}
	if (g_bLightmapCapable)
	{
		if (!PageInLightmaps(pContext))
		{
			AddDebugMessage(0, "Warning: unable to create lightmap pages.  Lightmapping disabled.");
			g_bLightmapCapable = 0;
		}
	}
	return pContext;
}

// NAME: d3d_DeleteContext: Jupiter d3d_init.cpp.
// FUNCTION: D3DREN 0x1001b770
void d3d_DeleteContext(HRENDERCONTEXT hContext)
{
	if (hContext)
	{
		FreeLightmapPages((RenderContext *)hContext);
		dfree(hContext);
	}
}

// NAME: d3d_RebindLightmaps: RenderStruct::RebindLightmaps member (the engine header's "con_RebindLightmaps"), d3d_ prefix by
// Jupiter's convention (medium).
// FUNCTION: D3DREN 0x1001b790
void d3d_RebindLightmaps(RenderContext *pContext)
{
	if (pContext)
		FreeLightmapPages(pContext);
	if (g_bLightmapCapable)
	{
		d3d_ReinitLightmapTextureSupport();
		if (pContext)
		{
			if (!PageInLightmaps(pContext))
			{
				AddDebugMessage(0, "Warning: unable to create lightmap pages.  Lightmapping disabled.");
				g_bLightmapCapable = 0;
			}
		}
	}
}

// guess: Jupiter CD3D_Device::FreeDevice (releases the device and the objects it owns).
// FUNCTION: D3DREN 0x1001b7e0
void CD3D_Device_FreeDevice()
{
	d3d_TermTextureManager();
	if (g_pClipper)
	{
		g_pClipper->Release();
		g_pClipper = 0;
	}
	if (g_pD3DDevice)
	{
		g_pD3DDevice->Release();
		g_pD3DDevice = 0;
	}
	if (g_pZBuffer)
	{
		g_pZBuffer->Release();
		g_pZBuffer = 0;
	}
	if (g_pD3D)
	{
		g_pD3D->Release();
		g_pD3D = 0;
	}
}

// ---- module init / term (called from d3d_Init / d3d_Term) ------------------------------------------------------------------
// d3d_surface.h declares a class with a static member whose definition consumes a static-initialiser number: include it after the
// ConVar definitions above so that their `_$E` numbers stay those of the exe.
#include "d3dren/d3d_surface.h"		// D3DShadowTextureFactory

// The singleton factory of the shadow textures (class in d3d_surface.h; its constructor 0x1001d8ff stores it in
// m_pShadowTextureFactory, Get() 0x100323f9 returns it).
// FUNCTION: D3DREN 0x1001b840
void d3d_TermRendererModules()
{
	d3d_NullCallback();
	d3d_TermPolyDrawPools();
	d3d_NullPreFrameCallback();
	d3d_TermObjectModules();
	D3DShadowTextureFactory *pFactory = D3DShadowTextureFactory::Get();
	if (pFactory)
		delete pFactory;
}

// FUNCTION: D3DREN 0x1001b870
int d3d_InitRendererModules()
{
	d3d_InitPolyDrawPools();
	d3d_NullPreFrameCallback();
	d3d_InitObjectModules();
	new D3DShadowTextureFactory;
	return 1;
}

// ---- RenderStruct slots: 3D frame ----------------------------------------------------------------------------------------
// RenderStruct::GetInfoFlags (0xc8): returns 0.
// FUNCTION: D3DREN 0x1001bd70
int d3d_GetInfoFlags()
{
	return 0;
}

// NAME: d3d_Start3D: RenderStruct::Start3D member (RenderDLLSetup 0x10010ff1 stores it at +0x90), d3d_ prefix by Jupiter's
// convention (medium); Jupiter has CD3D_Device::Start3D (BeginScene) as a static member.
// FUNCTION: D3DREN 0x1001bd80
int d3d_Start3D()
{
	if (!g_bIn3D && g_pD3DDevice)
	{
		if (!g_pD3DDevice->BeginScene())
		{
			g_bIn3D = 1;
			return 1;
		}
	}
	return 0;
}

// NAME: d3d_End3D: RenderStruct::End3D member (+0x94), d3d_ prefix by Jupiter's convention (medium); Jupiter CD3D_Device::End3D.
// FUNCTION: D3DREN 0x1001bdb0
int d3d_End3D()
{
	if (g_bIn3D && g_pD3DDevice)
	{
		if (g_bInOptimized2D)
			d3d_EndOptimized2D();
		g_bIn3D = 0;
		if (g_pD3DDevice->EndScene() == DDERR_SURFACELOST)
		{
			if (SUCCEEDED(g_pBackBuffer->Restore()))
			{
				g_pOffscreen->Restore();
				g_pZBuffer->Restore();
				d3d_FreeAllTextures();
			}
		}
		return 1;
	}
	return 0;
}

// NAME: d3d_IsIn3D: RenderStruct::IsIn3D member (+0x98), d3d_ prefix by Jupiter's convention (medium).
// FUNCTION: D3DREN 0x1001be20
int d3d_IsIn3D()
{
	return g_bIn3D;
}

// guess: loads a DDGAMMARAMP from a file and applies it (console command "LOADGAMMA <filename>" of d3d_RenderCommand).
// FUNCTION: D3DREN 0x1001be30
int d3d_LoadGamma(const char *pFilename)
{
	IDirectDrawGammaControl *pGammaControl;
	DDGAMMARAMP ramp;
	FILE *fp;

	if (!pFilename)
		return 0;

	fp = fopen(pFilename, "r");
	if (!fp)
		return 0;

	if (fread(&ramp, 0x600, 1, fp) != 1)
	{
		fclose(fp);
		return 0;
	}
	fclose(fp);

	if (g_bRunWindowed)
	{
		if (g_pPrimary->QueryInterface(IID_IDirectDrawGammaControl, (void **)&pGammaControl) != 0)
			return 0;
	}
	else
	{
		if (g_pBackBuffer->QueryInterface(IID_IDirectDrawGammaControl, (void **)&pGammaControl) != 0)
			return 0;
	}
	return pGammaControl->SetGammaRamp(0, &ramp) == 0;
}

// ---- Z buffer + device creation -------------------------------------------------------------------------------------------
// guess: finds a Z buffer format the device accepts (preferring one with a stencil mask), creates the Z buffer surface, attaches
// it to the render target (g_pOffscreen) and creates the Direct3D device (g_pD3DDevice) of the given type; the format that
// does not work is marked and the next one is tried.  Jupiter's counterpart is CD3D_Device::CreateDevice's Z buffer code.
// FUNCTION: D3DREN 0x1001aa70
int d3d_CreateDeviceWithZBuffer(RenderStructInit *pInit, GUID guid)
{
	uint32 nFormats;
	DDSURFACEDESC2 ddsd;
	int bTried[0x40];
	DDPIXELFORMAT formats[0x40];
	DDPIXELFORMAT *pFormat;
	uint32 i, iBest;

	memset(bTried, 0, sizeof(bTried));
	nFormats = 0;
	g_pEnumeratedZBufferFormats = formats;
	g_pZBufferFormatCount = &nFormats;
	if (g_pD3D->EnumZBufferFormats(guid, d3d_EnumZBufferFormatsCallback, 0) != 0 || nFormats == 0)
	{
		AddDebugMessage(1, "Unable to find an acceptable z-buffer format.");
		return 0;
	}

	while (nFormats > 0)
	{
		iBest = 0xFFFFFFFF;
		for (i = 0; i < nFormats; i++)
		{
			if (bTried[i])
				continue;
			if (iBest == 0xFFFFFFFF)
			{
				iBest = i;
				continue;
			}
			if (formats[iBest].dwStencilBitMask == 0 && formats[i].dwStencilBitMask != 0)
				iBest = i;
		}
		if (iBest >= 0x40)
			return 0;

		pFormat = &formats[iBest];
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		ddsd.dwWidth = pInit->m_Mode.m_Width;
		ddsd.dwHeight = pInit->m_Mode.m_Height;
		ddsd.ddsCaps.dwCaps = g_RenderSurfaceMemoryCaps | DDSCAPS_ZBUFFER;
		ddsd.ddpfPixelFormat = *pFormat;
		if (g_pDD->CreateSurface(&ddsd, &g_pZBuffer, 0) != 0)
		{
			AddDebugMessage(1, "Failed to make z-buffer.");
			d3d_FreeDDraw();
			return 0;
		}
		if (g_pOffscreen->AddAttachedSurface(g_pZBuffer) < 0)
		{
			g_pZBuffer->Release();
			return 0;
		}
		if (g_pD3D->CreateDevice(guid, g_pOffscreen, &g_pD3DDevice) >= 0)
		{
			g_ChosenZBufferPixelFormat = *pFormat;
			AddDebugMessage(1, "ZBuffer: Z Mask: %d, Stencil mask: %d", pFormat->dwRGBZBitMask, pFormat->dwStencilBitMask);
			return 1;
		}
		g_pOffscreen->DeleteAttachedSurface(0, g_pZBuffer);
		g_pZBuffer->Release();
		g_pZBuffer = 0;
		bTried[iBest] = 1;
	}
	return 0;
}

// ---- special cards -------------------------------------------------------------------------------------------------------
// NAME: CheckSpecialCards: Jupiter d3d_device.cpp CD3D_Device::CheckSpecialCards() ("First check if they want to force it. char*
// pForceMode = "ForceMode"" then vendor ids; Jupiter's body is stripped, this one is the original).  d3d.ren has no CD3D_Device
// instance: free function.
// STUB diagnosis: 448 of 448 bytes, the same 128 instructions; only the four early-return branches differ (ALIGNED 4): the exe's
// shared epilogue follows the g_bLoadWholeLightmapSurface store and the Permedia2 block has its own copy, ours shares the one after
// Permedia2.  Tried: Permedia2 at the end / inside the ForceMode test / in an else of the vendor test, the 0x1002 and 0x121a arms
// as nested ifs, separate ifs with returns, explicit returns, duplicated (non-goto) bodies; permuter 3000 candidates, no change.
// PARKED: epilogue placement of the early returns (4 jump displacements); instruction stream identical
// STUB: D3DREN 0x1001a8b0
void CheckSpecialCards()
{
	DDDEVICEIDENTIFIER2 id;
	HLTPARAM hParam;

	if (!g_pDD || !g_pStruct)
		return;

	g_bModelShadowsSupported = 1;
	g_bPowerVRWorkaround = 0;
	memset(&id, 0, sizeof(id));
	if (g_pDD->GetDeviceIdentifier(&id, 0) != 0)
		return;

	hParam = g_pStruct->GetParameter("ForceMode");
	if (hParam && _strcmpi(g_pStruct->GetParameterValueString(hParam), "PowerVR") == 0)
		goto PowerVR;

	hParam = g_pStruct->GetParameter("ForceMode");
	if (hParam && _strcmpi(g_pStruct->GetParameterValueString(hParam), "Permedia2") == 0)
		goto Permedia2;

	hParam = g_pStruct->GetParameter("ForceMode");
	if (hParam && _strcmpi(g_pStruct->GetParameterValueString(hParam), "Rage128") == 0)
		goto Rage128;

	if (id.dwVendorId != 0x104c)
	{
		if (id.dwVendorId == 0x1033)
		{
			if (id.dwDeviceId != 0x46 && id.dwDeviceId != 0x2a)
				return;
PowerVR:
			g_pStruct->ConsolePrint("POWERVR DETECTED: disabling shadows and fullbrites");
			g_bModelShadowsSupported = 0;
			g_bPowerVRWorkaround = 1;
			return;
		}
		else if (id.dwVendorId == 0x1002)
		{
			if (id.dwDeviceId == 0x5246)
			{
Rage128:
				g_bLoadWholeLightmapSurface = 1;
			}
		}
		else if (id.dwVendorId == 0x121a)
		{
			if (id.dwDeviceId == 5)
				g_bTwoTextureStageBlendValidated = 0;
		}
		return;
	}

Permedia2:
	g_pStruct->ConsolePrint("PERMEDIA 2 DETECTED: disabling lightmapping and shadows");
	g_bLightmapCapable = 0;
	g_bModelShadowsSupported = 0;
}

// ---- device creation ------------------------------------------------------------------------------------------------------------
// callees in other units
struct UnkType_SavedStage1;
void SaveAndSetStageOneAdditiveStates(UnkType_SavedStage1 *pState);	// unit unk/10023860 region (guess: sets up the two-stage test state in the 16 byte object)
void RestoreStageOneAdditiveStates(UnkType_SavedStage1 *pState);	// the matching restore
void SetLightmapTextureStageStates();							// W9 (lightmap): guess: lightmap texture formats set-up for one-pass lightmapping
int CTextureManager_Init();								// d3d_texture (CTextureManager::Init), also declared by d3dtexture.h

// GLOBAL: D3DREN 0x1005c878
extern D3DDEVICEDESC7 g_D3DDeviceDesc;		// copy of the device's D3DDEVICEDESC7 (wMaxSimultaneousTextures at +0xba is 0x1005c932)
extern int g_bOnePassLightmappingEnabled;				// one-pass lightmapping enabled (declared by unit unk/100132a0)
extern uint32 g_DefaultZEnableState;				// the device's normal D3DRENDERSTATE_ZENABLE value (declared by unit unk/1002d080)
// GLOBAL: D3DREN 0x1005de1c
extern float g_ModelHalfTexelScale;				// guess: 0.5f set when the device is up
void d3d_SetModulateAlphaTextureStates();					// stage 0 texture blend (modulate), d3d_draw.h
void d3d_SetDetailTextureStates(void);				// d3dstate.h: stage 1 colour op = add-signed / modulate
void d3d_UnsetDetailTexture(void);				// d3dstate.h: disable stage 1 colour op and unbind its texture

// NAME: d3d_CreateDevice: Jupiter d3d_device.cpp CD3D_Device::CreateDevice / d3d_init.cpp (the DirectDraw 7 form: DirectDrawCreateEx, cooperative
// level, display mode, primary surface (flipping chain, or window primary + offscreen with a clipper), the Z buffer and device through d3d_CreateDeviceWithZBuffer,
// the viewport, the capability flags the dump (LISTDEVICECAPS) prints, the default render states, the two-stage validation and the lightmap mode).
// Low confidence for the name (the strings are Talon's own); roles: pNode is the enumerated device node (UnkType_DeviceNode), pInit the mode
// the engine asked for.  Returns 1 on success.
// STUB diagnosis: the large local frame differs (exe: ddsd at esp+0x58, desc +0xd4, bltfx +0x1c0, DDCAPS hal +0x224 / hel +0x3a0, viewport +0x38;
//   ours ddsd at +0x48; frame size 0x50c matches once the 16-byte state object is a function-scope local and the caps/free-memory DWORDs sit in an
//   inner scope).  The initial flipping-surface failure and windowed primary-surface failure now share the primary-surface message/cleanup label,
//   matching the exe's `jne 0x1001aff3`; the retry failure keeps its separate message push at 0x1001af72.  The reference-rasterizer,
//   QueryInterface, and Force1Pass diagnostic literals now match the exe's strings at 0x1004b094, 0x1004aff8, and 0x1004aebc.
// STUB: D3DREN 0x1001acc0
int d3d_CreateDevice(UnkType_DeviceNode *pNode, RenderStructInit *pInit)
{
	DDCAPS ddcapsHel;
	DDCAPS ddcapsHal;
	DDBLTFX bltfx;
	D3DDEVICEDESC7 desc;
	DDSURFACEDESC2 ddsd;
	D3DVIEWPORT7 vp;
	DDSCAPS2 caps;
	GUID guid;
	DWORD dwCoop;
	HLTPARAM hParam;
	int i;

	ShowCursor(0);
	if (g_RefRast)
	{
		g_pStruct->ConsolePrint("USING DIRECT3D REFERENCE RASTERIZER (SLOOOOOOOW!)");
		g_RenderSurfaceMemoryCaps = DDSCAPS_SYSTEMMEMORY;
		guid = IID_IDirect3DRefDevice;
	}
	else if (g_RGBRast)
	{
		g_RenderSurfaceMemoryCaps = DDSCAPS_SYSTEMMEMORY;
		guid = IID_IDirect3DRGBDevice;
	}
	else if (g_MMXRast)
	{
		g_RenderSurfaceMemoryCaps = DDSCAPS_SYSTEMMEMORY;
		guid = IID_IDirect3DMMXDevice;
	}
	else if (g_TnLRast)
	{
		guid = IID_IDirect3DTnLHalDevice;
	}
	else
	{
		guid = IID_IDirect3DHALDevice;
	}

	g_pDD = 0;
	if (DirectDrawCreateEx(pNode->m_pGuid, (void **)&g_pDD, IID_IDirectDraw7, 0) != 0)
	{
		AddDebugMessage(1, "DirectDrawCreateEx failed.");
		d3d_FreeDDraw();
		return 0;
	}

	dwCoop = g_bRunWindowed ? DDSCL_NORMAL : DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT;
	hParam = g_pStruct->GetParameter("NullRender");
	if (hParam && g_pStruct->GetParameterValueFloat(hParam) != 0.0f)
		dwCoop |= DDSCL_NOWINDOWCHANGES;
	if (g_pDD->SetCooperativeLevel((HWND)pInit->m_hWnd, dwCoop) != 0)
	{
		AddDebugMessage(1, "SetCooperativeLevel failed.");
		d3d_FreeDDraw();
		return 0;
	}

	if (!g_bRunWindowed)
	{
		if (g_pDD->SetDisplayMode(pInit->m_Mode.m_Width, pInit->m_Mode.m_Height, pInit->m_Mode.m_BitDepth, 0, 0) != 0)
		{
			AddDebugMessage(1, "SetDisplayMode failed.");
			d3d_FreeDDraw();
			return 0;
		}
	}

	if (g_pDD->QueryInterface(IID_IDirect3D7, (void **)&g_pD3D) != 0)
	{
		AddDebugMessage(1, "QueryInterface(IID_IDirect3D7) failed.. are you running NT 4.0?");
		d3d_FreeDDraw();
		return 0;
	}

	if (!g_bRunWindowed && !g_bSpecialRenderMode)
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
		ddsd.dwBackBufferCount = (g_TripleBuffer != 0) + 1;
		if (g_pDD->CreateSurface(&ddsd, &g_pBackBuffer, 0) != 0)
		{
			if (ddsd.dwBackBufferCount != 2)
				goto PrimarySurfaceFailure;
			AddDebugMessage(0, "Unable to use triple buffering.");
			ddsd.dwBackBufferCount = 1;
			if (g_pDD->CreateSurface(&ddsd, &g_pBackBuffer, 0) != 0)
			{
				AddDebugMessage(1, "Unable to create a primary surface.");
				d3d_FreeDDraw();
				return 0;
			}
		}
		g_pOffscreen = 0;
		g_pBackBuffer->EnumAttachedSurfaces(0, d3d_EnumAttachedSurfacesCallback);
		if (!g_pOffscreen)
		{
			AddDebugMessage(1, "Couldn't get pointer to offscreen surface.");
			d3d_FreeDDraw();
			return 0;
		}
	}
	else
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		if (g_pDD->CreateSurface(&ddsd, &g_pPrimary, 0) != 0)
			goto PrimarySurfaceFailure;

		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwBackBufferCount = 0;
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		ddsd.ddsCaps.dwCaps = g_RenderSurfaceMemoryCaps | DDSCAPS_3DDEVICE;
		ddsd.dwWidth = pInit->m_Mode.m_Width;
		ddsd.dwHeight = pInit->m_Mode.m_Height;
		if (g_pDD->CreateSurface(&ddsd, &g_pBackBuffer, 0) != 0)
		{
			AddDebugMessage(1, "Failed to make rendering surface.");
			d3d_FreeDDraw();
			return 0;
		}
		g_pOffscreen = g_pBackBuffer;
		if (g_pDD->CreateClipper(0, &g_pClipper, 0) == 0)
		{
			g_pClipper->SetHWnd(0, (HWND)pInit->m_hWnd);
			g_pPrimary->SetClipper(g_pClipper);
		}
	}

	goto SurfaceCreationSucceeded;
PrimarySurfaceFailure:
	AddDebugMessage(1, "Unable to create a primary surface.");
	d3d_FreeDDraw();
	return 0;
SurfaceCreationSucceeded:
	memset(&bltfx, 0, sizeof(bltfx));
	bltfx.dwSize = sizeof(bltfx);
	for (i = 4; i; i--)
	{
		g_pOffscreen->Blt(0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &bltfx);
		g_pBackBuffer->Flip(0, DDFLIP_WAIT);
	}

	if (!d3d_CreateDeviceWithZBuffer(pInit, guid))
	{
		AddDebugMessage(1, "Failed to create device.");
		d3d_FreeDDraw();
		g_TnLRast = 0;
		return 0;
	}

	memset(&vp, 0, sizeof(vp));
	vp.dwX = 0;
	vp.dwY = 0;
	vp.dwWidth = pInit->m_Mode.m_Width;
	vp.dwHeight = pInit->m_Mode.m_Height;
	vp.dvMinZ = 0.0f;
	vp.dvMaxZ = 1.0f;
	if (g_pD3DDevice->SetViewport(&vp) != 0)
	{
		AddDebugMessage(1, "IDirect3DDevice::SetCurrentViewport failed.");
		d3d_FreeDDraw();
		return 0;
	}

	IDirect3DDevice7 *pDevice = g_pD3DDevice;
	memset(&ddcapsHel, 0, sizeof(ddcapsHel));
	memset(&ddcapsHal, 0, sizeof(ddcapsHal));
	ddcapsHal.dwSize = sizeof(DDCAPS);
	ddcapsHel.dwSize = sizeof(DDCAPS);
	g_pDD->GetCaps(&ddcapsHal, &ddcapsHel);
	g_bSurfacesLargerThanScreenSupported = (ddcapsHal.dwCaps2 >> 12) & 1;

	memset(&desc, 0, sizeof(desc));
	pDevice->GetCaps(&desc);

	{
		DWORD dwTotalTex, dwFreeTex, dwTotalVid, dwFreeVid;

		memset(&caps, 0, sizeof(caps));
		caps.dwCaps = DDSCAPS_TEXTURE;
		g_pDD->GetAvailableVidMem(&caps, &dwTotalTex, &dwFreeTex);
		memset(&caps, 0, sizeof(caps));
		caps.dwCaps = DDSCAPS_VIDEOMEMORY;
		g_pDD->GetAvailableVidMem(&caps, &dwTotalVid, &dwFreeVid);

		g_bDeviceDrawPrimitiveSupported = (desc.dwDevCaps >> 10) & 1;
		g_bVideoMemoryTexturesSupported = (desc.dwDevCaps >> 9) & 1;
		g_bSystemMemoryTexturesSupported = (desc.dwDevCaps >> 8) & 1;
		g_bAGPMemoryTexturesSupported = (desc.dwDevCaps >> 12) & 1;
		if (desc.dwDeviceZBufferBitDepth == DDBD_8)
			g_DeviceZBufferBitDepth = 8;
		else if (desc.dwDeviceZBufferBitDepth == DDBD_16)
			g_DeviceZBufferBitDepth = 16;
		else if (desc.dwDeviceZBufferBitDepth == DDBD_24)
			g_DeviceZBufferBitDepth = 24;
		else
			g_DeviceZBufferBitDepth = desc.dwDeviceZBufferBitDepth == DDBD_32 ? 32 : -1;
		if (desc.dwDeviceRenderBitDepth == DDBD_8)
			g_DeviceRenderBitDepth = 8;
		else if (desc.dwDeviceRenderBitDepth == DDBD_16)
			g_DeviceRenderBitDepth = 16;
		else if (desc.dwDeviceRenderBitDepth == DDBD_24)
			g_DeviceRenderBitDepth = 24;
		else
			g_DeviceRenderBitDepth = desc.dwDeviceRenderBitDepth == DDBD_32 ? 32 : -1;
		g_bDestBlendSrcAlphaSupported = (desc.dpcTriCaps.dwDestBlendCaps >> 4) & 1;
		g_bSrcBlendSrcColorSupported = (desc.dpcTriCaps.dwSrcBlendCaps >> 2) & 1;
		g_bTextureBlendModulateSupported = (desc.dpcTriCaps.dwTextureBlendCaps >> 1) & 1;
		g_bTextureBlendAddSupported = (desc.dpcTriCaps.dwTextureBlendCaps >> 7) & 1;
		g_DeviceFreeVideoMemory = dwTotalVid;
		g_bDeviceDitherSupported = desc.dpcTriCaps.dwRasterCaps & 1;
		g_DeviceTotalTextureMemory = dwTotalTex;
		g_DefaultZEnableState = 1;
		memcpy(&g_D3DDeviceDesc, &desc, 0x3b * 4);
		memcpy(&g_DeviceTriangleCaps, &desc.dpcTriCaps, 0xe * 4);
	}

	g_pD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_LINEAR);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ANTIALIAS, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, g_DefaultZEnableState);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DITHERENABLE, 1);
	d3d_SetModulateAlphaTextureStates();
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CLIPPING, g_CV_UseD3DClip.m_IntVal != 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_LIGHTING, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_EXTENTS, 0);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	{
		uint32 aState[4];
		DWORD dwPasses;

		SaveAndSetStageOneAdditiveStates((UnkType_SavedStage1 *)aState);
		g_bModelSpecularBlendValidated = g_pD3DDevice->ValidateDevice(&dwPasses) == 0;
		RestoreStageOneAdditiveStates((UnkType_SavedStage1 *)aState);
		d3d_SetDetailTextureStates();
		g_bTwoTextureStageBlendValidated = g_pD3DDevice->ValidateDevice(&dwPasses) == 0;
		if (g_D3DDeviceDesc.wMaxSimultaneousTextures < 2)
			g_bTwoTextureStageBlendValidated = 0;
		d3d_UnsetDetailTexture();
	}

	g_bModelFullbriteCapable = (~g_DeviceTriangleCaps.dwRasterCaps >> 15) & 1;
	if (g_bModelFullbriteCapable && (g_DeviceTriangleCaps.dwSrcBlendCaps & 0x10))
		g_bGouraudFullbriteCapable = 1;
	else
		g_bGouraudFullbriteCapable = 0;
	if ((g_DeviceTriangleCaps.dwSrcBlendCaps & 1) && (g_DeviceTriangleCaps.dwDestBlendCaps & 4))
		g_bLightmapCapable = 1;
	else
		g_bLightmapCapable = 0;
	if ((g_DeviceTriangleCaps.dwSrcBlendCaps & 2) && (g_DeviceTriangleCaps.dwDestBlendCaps & 2))
		g_bLightAddPolyCapable = 1;
	else
		g_bLightAddPolyCapable = 0;

	g_LightmapTextureStage = 0;
	g_NormalTextureStage = 0;
	g_bOnePassLightmappingEnabled = g_Force1Pass;
	if (g_Force1Pass)
	{
		DWORD dwPasses;

		SetLightmapTextureStageStates();
		if (g_pD3DDevice->ValidateDevice(&dwPasses) == 0 && g_D3DDeviceDesc.wMaxSimultaneousTextures > 1)
		{
			g_LightmapTextureStage = 1;
			AddDebugMessage(0, "Using 1 pass lightmapping!");
		}
		else
		{
			g_bOnePassLightmappingEnabled = 0;
			AddDebugMessage(0, "Force1Pass doesn't work on this card.  Using 2 pass lightmapping.");
		}
	}
	else
	{
		AddDebugMessage(0, "Using 2 pass lightmapping.");
	}

	CheckSpecialCards();
	d3d_LoadGamma("lithtech.gam");
	if (!CTextureManager_Init())
	{
		d3d_FreeDDraw();
		return 0;
	}
	g_ModelHalfTexelScale = 0.5f;
	return 1;
}

// ---- RenderCommand --------------------------------------------------------------------------------------------------------
// the screen format g_ScreenPixelFormat: common_init.h (filled by the device bring-up, defined in sys/d3d/common_init)

int d3d_PortalFileCommand(char *pFileName);						// unk/1002d000 stub (returns 0)

// NAME: d3d_RenderCommand: Jupiter d3d_init.cpp (the LISTDEVICES / LISTTEXTUREFORMATS / LISTDEVICECAPS / FREETEXTURES console
// commands, same strings); Talon adds LOADGAMMA and PortalFile.  RenderStruct::RenderCommand member (+0xbc).
// FUNCTION: D3DREN 0x1001b8a0
void d3d_RenderCommand(int argc, char **argv)
{
	if (argc > 0)
	{
		if (_strcmpi(argv[0], "LISTDEVICES") == 0)
		{
			d3d_ListDevices();
			return;
		}
		if (_strcmpi(argv[0], "LISTTEXTUREFORMATS") == 0)
		{
			d3d_ListTextureFormats();
			return;
		}
		if (_strcmpi(argv[0], "LISTDEVICECAPS") == 0)
		{
			if (g_pDD)
			{
				DDDEVICEIDENTIFIER2 id;
				memset(&id, 0, sizeof(id));
				if (g_pDD->GetDeviceIdentifier(&id, 0) == 0)
				{
					g_pStruct->ConsolePrint("---------------------------------------------------------------");
					g_pStruct->ConsolePrint("Driver: %s", id.szDriver);
					g_pStruct->ConsolePrint("Description: %s", id.szDescription);
					g_pStruct->ConsolePrint("Product: 0x%x, Version: 0x%x, SubVersion: 0x%x, Build: 0x%x",
						(uint32)id.liDriverVersion.HighPart >> 16, id.liDriverVersion.HighPart & 0xffff,
						id.liDriverVersion.LowPart >> 16, id.liDriverVersion.LowPart & 0xffff);
					g_pStruct->ConsolePrint("VendorID: 0x%x, DeviceID: 0x%x, SubSysID: 0x%x, Revision: 0x%x",
						id.dwVendorId, id.dwDeviceId, id.dwSubSysId, id.dwRevision);
					g_pStruct->ConsolePrint("Width: %d, Height: %d, BitDepth: %d", g_ScreenWidth, g_ScreenHeight,
						(g_ScreenPixelFormat.m_eType == BPP_16) ? 16 : 32);
					g_pStruct->ConsolePrint("---------------------------------------------------------------");
				}
			}
			g_pStruct->ConsolePrint("Portals: %s", (g_ChosenZBufferStencilBitDepth > 0) ? "Yes" : "No");
			g_pStruct->ConsolePrint("Model fullbrites: %s, Gouraud fullbrites: %s", g_bModelFullbriteCapable ? "Yes" : "No", g_bGouraudFullbriteCapable ? "Yes" : "No");
			g_pStruct->ConsolePrint("Lightmap capable: %s, Light add poly: %s", g_bLightmapCapable ? "Yes" : "No", g_bLightAddPolyCapable ? "Yes" : "No");
			g_pStruct->ConsolePrint("DrawPrim: %s, Dither: %s", g_bDeviceDrawPrimitiveSupported ? "Yes" : "No", g_bDeviceDitherSupported ? "Yes" : "No");
			g_pStruct->ConsolePrint("Src blend SRCCOLOR: %s, Dest blend SRCALPHA: %s", g_bSrcBlendSrcColorSupported ? "Yes" : "No", g_bDestBlendSrcAlphaSupported ? "Yes" : "No");
			g_pStruct->ConsolePrint("TBlend Add: %s, TBlend Modulate (gouraud): %s", g_bTextureBlendAddSupported ? "Yes" : "No", g_bTextureBlendModulateSupported ? "Yes" : "No");
			g_pStruct->ConsolePrint("LINEARMIPNEAREST supported: %s", (g_DeviceTriangleCaps.dwTextureFilterCaps & D3DPTFILTERCAPS_LINEARMIPNEAREST) ? "Yes" : "No");
			g_pStruct->ConsolePrint("Square textures only: %s, Vid mem textures: %s", (g_DeviceTriangleCaps.dwTextureCaps & D3DPTEXTURECAPS_SQUAREONLY) ? "Yes" : "No", g_bVideoMemoryTexturesSupported ? "Yes" : "No");
			g_pStruct->ConsolePrint("System mem textures: %s, AGP mem textures: %s", g_bSystemMemoryTexturesSupported ? "Yes" : "No", g_bAGPMemoryTexturesSupported ? "Yes" : "No");
			g_pStruct->ConsolePrint("Texture Memory: %d, Video Memory: %d", g_DeviceTotalTextureMemory, g_DeviceFreeVideoMemory);
			g_pStruct->ConsolePrint("ZBuffer Depth: %d, Device Depth: %d", g_DeviceZBufferBitDepth, g_DeviceRenderBitDepth);
			g_pStruct->ConsolePrint("Z-test: %s, Table fog: %s, Palette alpha: %s", (g_DeviceTriangleCaps.dwRasterCaps & D3DPRASTERCAPS_ZTEST) ? "Yes" : "No",
				(g_DeviceTriangleCaps.dwRasterCaps & D3DPRASTERCAPS_FOGTABLE) ? "Yes" : "No", (g_DeviceTriangleCaps.dwTextureCaps & D3DPTEXTURECAPS_ALPHAPALETTE) ? "Yes" : "No");
			g_pStruct->ConsolePrint("Max texture size: (%d x %d)", g_DeviceMaxTextureWidth, g_DeviceMaxTextureHeight);
			g_pStruct->ConsolePrint("Surfaces larger than screen: %s", g_bSurfacesLargerThanScreenSupported ? "Yes" : "No");
			return;
		}
		if (_strcmpi(argv[0], "FREETEXTURES") == 0)
		{
			d3d_FreeAllTextures();
			return;
		}
		if (_strcmpi(argv[0], "LOADGAMMA") == 0)
		{
			if (argc != 2)
			{
				g_pStruct->ConsolePrint("Usage: loadgamma <filename>");
				return;
			}
			d3d_LoadGamma(argv[1]);
			return;
		}
		if (_strcmpi(argv[0], "PortalFile") == 0)
		{
			if (argc >= 2)
			{
				d3d_PortalFileCommand(argv[1]);
				return;
			}
			d3d_NullCallback();
		}
	}
}
