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
int DAT_10057ac8;			// guess: fog enable last sent
// GLOBAL: D3DREN 0x10058044
int DAT_10058044;			// guess: fog colour R last sent
// GLOBAL: D3DREN 0x10058048
int DAT_10058048;			// guess: fog colour G last sent
// GLOBAL: D3DREN 0x1005804c
int DAT_1005804c;			// guess: fog colour B last sent
// GLOBAL: D3DREN 0x100580d0
float DAT_100580d0;			// guess: fog near z last sent
// GLOBAL: D3DREN 0x100578e8
float DAT_100578e8;			// guess: fog far z last sent
// GLOBAL: D3DREN 0x100582e0
int DAT_100582e0;			// guess: TableFog last sent
// GLOBAL: D3DREN 0x10058040
uint8 DAT_10058040;			// guess: fog colour byte R (clamped), also read by the draw code
// GLOBAL: D3DREN 0x10058041
uint8 DAT_10058041;			// guess: fog colour byte G
// GLOBAL: D3DREN 0x10058042
uint8 DAT_10058042;			// guess: fog colour byte B
// GLOBAL: D3DREN 0x100579e0
float DAT_100579e0;			// guess: 2 * (fog colour R - 128)
// GLOBAL: D3DREN 0x100579e4
float DAT_100579e4;			// guess: 2 * (fog colour G - 128)
// GLOBAL: D3DREN 0x100579e8
float DAT_100579e8;			// guess: 2 * (fog colour B - 128)
// GLOBAL: D3DREN 0x100584e8
int DAT_100584e8;			// guess: the fog state above has been sent (cleared by d3d_Init)
// GLOBAL: D3DREN 0x10057ed8
int DAT_10057ed8;			// guess: dither last sent
// GLOBAL: D3DREN 0x100584ec
int DAT_100584ec;			// guess: the dither state has been sent (cleared by d3d_Init)
// GLOBAL: D3DREN 0x100579ec
int DAT_100579ec;			// guess: Bilinear last sent
// GLOBAL: D3DREN 0x100584f0
int DAT_100584f0;			// guess: the filter states have been sent (cleared by d3d_Init)

// NAME: d3d_ReadExtraConsoleVariables: Jupiter d3d_init.cpp (fog from the console variables: FogNearZ == FogFarZ disables fog
// ("This handles a TNT bug"), SetRenderState FOGCOLOR/FOGSTART/FOGEND/FOGENABLE, DITHERENABLE, anisotropic/bilinear/trilinear
// filter states); the D3D7 original caches what it sent and only updates when a value changed.
// STUB diagnosis: 1104 of 1104 bytes (same size), 594 differ: the exe keeps the clamp limit 0xff in eax for the three fog colour channels
//   (`mov eax, 0xff` before the first `jge`, then `cmp edx, eax` / `mov [DAT], al`), ours compares with the immediate 0xff each time; the byte
//   globals are read with `xor ecx, ecx; mov cl, [DAT]` / `mov bl, [DAT]` in a different register order afterwards.  Same control flow otherwise.
// STUB: D3DREN 0x1001a400
void d3d_ReadExtraConsoleVariables()
{
	if (g_pD3DDevice)
	{
		if (!(DAT_1005c964.dwRasterCaps & D3DPRASTERCAPS_FOGTABLE))
			DAT_1005849c = 0;
		if (DAT_100584a0 == DAT_10048744)
			DAT_1005849c = 0;

		if (!DAT_100584e8 || DAT_10057ac8 != DAT_1005849c || DAT_10058044 != DAT_10048738 || DAT_10058048 != DAT_1004873c ||
			DAT_1005804c != DAT_10048740 || fabs(DAT_100580d0 - DAT_100584a0) > 0.001f || fabs(DAT_100578e8 - DAT_10048744) > 0.001f ||
			DAT_100582e0 != g_CV_TableFog.m_IntVal)
		{
			if (DAT_10048738 < 0)
				DAT_10058040 = 0;
			else
				DAT_10058040 = (DAT_10048738 > 255) ? 255 : DAT_10048738;
			if (DAT_1004873c < 0)
				DAT_10058041 = 0;
			else
				DAT_10058041 = (DAT_1004873c > 255) ? 255 : DAT_1004873c;
			if (DAT_10048740 < 0)
				DAT_10058042 = 0;
			else
				DAT_10058042 = (DAT_10048740 > 255) ? 255 : DAT_10048740;

			if (g_CV_TableFog.m_IntVal)
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_LINEAR);
			else
				g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_NONE);
			DAT_100582e0 = g_CV_TableFog.m_IntVal;

			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGCOLOR, (((DAT_10058040 << 8) | DAT_10058041) << 8) | DAT_10058042);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGSTART, *(DWORD *)&DAT_100584a0);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGEND, *(DWORD *)&DAT_10048744);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, DAT_1005849c);

			DAT_100579e0 = (float)(DAT_10058040 - 0x80) * 2.0f;
			DAT_10057ac8 = DAT_1005849c;
			DAT_10058044 = DAT_10048738;
			DAT_1005804c = DAT_10048740;
			DAT_10058048 = DAT_1004873c;
			DAT_100579e4 = (float)(DAT_10058041 - 0x80) * 2.0f;
			DAT_100578e8 = DAT_10048744;
			DAT_100580d0 = DAT_100584a0;
			DAT_100584e8 = 1;
			DAT_100579e8 = (float)(DAT_10058042 - 0x80) * 2.0f;
		}

		if (!DAT_100584ec || DAT_10057ed8 != DAT_10048784)
		{
			if (!g_pD3DDevice)
				return;
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DITHERENABLE, DAT_10048784);
			DAT_10057ed8 = DAT_10048784;
			DAT_100584ec = 1;
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
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, (DAT_1005782c != 0) + 1);
				g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, (DAT_1005782c != 0) + 1);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MINFILTER, (DAT_1005782c != 0) + 1);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_MAGFILTER, (DAT_1005782c != 0) + 1);
			}
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, (g_CV_Trilinear.m_IntVal != 0) + 2);
			g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPMAPLODBIAS, *(DWORD *)&g_CV_MipMapBias.m_FloatVal);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_MIPFILTER, (g_CV_Trilinear.m_IntVal != 0) + 2);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_MIPMAPLODBIAS, *(DWORD *)&g_CV_MipMapBias.m_FloatVal);
			DAT_100584f0 = 1;
			DAT_100579ec = DAT_1005782c;
		}
	}
}

// ---- Z buffer format enumeration ------------------------------------------------------------------------------------------
// Context of d3d_EnumZBufferFormatsCallback: set by FUN_1001aa70 to a count and an array on its own stack frame.
// GLOBAL: D3DREN 0x1005c99c
uint32 *DAT_1005c99c;
// GLOBAL: D3DREN 0x1005de24
DDPIXELFORMAT *DAT_1005de24;

// NAME: CanDrawPortals: Jupiter d3d_device.h CD3D_Device::CanDrawPortals(), here a free function reading the stencil bit depth
// of the chosen Z buffer format (DDPIXELFORMAT::dwStencilBitDepth).
// FUNCTION: D3DREN 0x1001a850
int CanDrawPortals()
{
	return DAT_1005cde0 > 0;
}

// NAME: d3d_EnumZBufferFormatsCallback: LPD3DENUMPIXELFORMATSCALLBACK (DirectX SDK type); name from names_proposal.csv (medium).
// FUNCTION: D3DREN 0x1001a860
HRESULT WINAPI d3d_EnumZBufferFormatsCallback(LPDDPIXELFORMAT lpDDPixFmt, LPVOID lpContext)
{
	if (*DAT_1005c99c < 0x40)
	{
		DAT_1005de24[*DAT_1005c99c] = *lpDDPixFmt;
		*DAT_1005c99c = *DAT_1005c99c + 1;
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
	if (DAT_10057994)
	{
		dsi_ConsolePrint("Warning: NoLMPages is TRUE");
		return pContext;
	}
	if (DAT_1005de20)
	{
		if (!PageInLightmaps(pContext))
		{
			AddDebugMessage(0, "Warning: unable to create lightmap pages.  Lightmapping disabled.");
			DAT_1005de20 = 0;
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
	if (DAT_1005de20)
	{
		d3d_ReinitLightmapTextureSupport();
		if (pContext)
		{
			if (!PageInLightmaps(pContext))
			{
				AddDebugMessage(0, "Warning: unable to create lightmap pages.  Lightmapping disabled.");
				DAT_1005de20 = 0;
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
void FUN_1001b840()
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
int FUN_1001b870()
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
int FUN_1001be30(const char *pFilename)
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
// STUB diagnosis (608 vs 592 bytes, same flow and calls): register allocation of the format search loop.  The exe keeps the
// loop index in eax, a pointer to bTried[i] in edx and a pointer to formats[i].dwStencilBitMask in esi (cmp dword ptr [esi], 0),
// the best index in ebx and pFormat in ebp, and re-loads formats[best] with `mov edi,ebx; shl edi,5`; ours strength-reduces
// formats[i] to an offset register (mov ebx,[esp+ecx+0x1a8]) and keeps best*32 in edi.  Tried: condition order and nesting,
// int/uint32 best and bTried, a pointer to the current format, a pointer to the best one.
// STUB: D3DREN 0x1001aa70
int FUN_1001aa70(RenderStructInit *pInit, GUID guid)
{
	uint32 nFormats;
	DDSURFACEDESC2 ddsd;
	int bTried[0x40];
	DDPIXELFORMAT formats[0x40];
	DDPIXELFORMAT *pFormat;
	uint32 i, iBest;

	nFormats = 0;
	memset(bTried, 0, sizeof(bTried));
	DAT_1005de24 = formats;
	DAT_1005c99c = &nFormats;
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
			if (bTried[i] == 0 && (iBest == 0xFFFFFFFF || (formats[iBest].dwStencilBitMask == 0 && formats[i].dwStencilBitMask != 0)))
				iBest = i;
		}
		if (iBest >= 0x40)
			return 0;

		pFormat = &formats[iBest];
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		ddsd.dwHeight = pInit->m_Mode.m_Height;
		ddsd.dwWidth = pInit->m_Mode.m_Width;
		ddsd.ddsCaps.dwCaps = DAT_10057828 | DDSCAPS_ZBUFFER;
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
			DAT_1005cdd0 = *pFormat;
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
// STUB diagnosis: 448 of 448 bytes (same size, 216 bytes differ, 134 vs 128 instructions).  The exe has no register for the constant 1 (every store
// of 1 is `mov dword ptr [..], 1`, only esi = 0 is kept, and `push edi` comes after the early exits), ours keeps 1 in edi for the four sites
// (DAT_1005c810, DAT_1005c814, DAT_1005de3c twice).  The exe merges the two DAT_1005de3c = 1 sites (the ForceMode "Rage128" test and the vendor
// 0x1002 test jump to one block, and the PowerVR block follows the 0x1033 test inline); the variants that do the same with a Rage128 label are 432
// bytes with another layout.
// STUB: D3DREN 0x1001a8b0
void CheckSpecialCards()
{
	DDDEVICEIDENTIFIER2 id;
	HLTPARAM hParam;

	if (!g_pDD || !g_pStruct)
		return;

	memset(&id, 0, sizeof(id));
	DAT_1005c810 = 1;
	DAT_1005c814 = 0;
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
	{
		DAT_1005de3c = 1;
		return;
	}

	if (id.dwVendorId == 0x104c)
		goto Permedia2;

	if (id.dwVendorId == 0x1033)
	{
		if (id.dwDeviceId != 0x46 && id.dwDeviceId != 0x2a)
			return;
PowerVR:
		g_pStruct->ConsolePrint("POWERVR DETECTED: disabling shadows and fullbrites");
		DAT_1005c810 = 0;
		DAT_1005c814 = 1;
		return;
	}
	if (id.dwVendorId == 0x1002)
	{
		if (id.dwDeviceId == 0x5246)
			DAT_1005de3c = 1;
		return;
	}
	if (id.dwVendorId != 0x121a)
		return;
	if (id.dwDeviceId == 5)
		DAT_1005de2c = 0;
	return;

Permedia2:
	g_pStruct->ConsolePrint("PERMEDIA 2 DETECTED: disabling lightmapping and shadows");
	DAT_1005de20 = 0;
	DAT_1005c810 = 0;
}

// ---- device creation ------------------------------------------------------------------------------------------------------------
// callees in other units
struct UnkType_SavedStage1;
void FUN_100246b7(UnkType_SavedStage1 *pState);	// unit unk/10023860 region (guess: sets up the two-stage test state in the 16 byte object)
void FUN_1002473b(UnkType_SavedStage1 *pState);	// the matching restore
void FUN_100356d5();							// W9 (lightmap): guess: lightmap texture formats set-up for one-pass lightmapping
int FUN_1001ec50();								// d3d_texture (CTextureManager::Init), also declared by d3dtexture.h

// GLOBAL: D3DREN 0x1005c878
extern D3DDEVICEDESC7 DAT_1005c878;		// copy of the device's D3DDEVICEDESC7 (wMaxSimultaneousTextures at +0xba is 0x1005c932)
extern int DAT_1005c7e0;				// one-pass lightmapping enabled (declared by unit unk/100132a0)
extern uint32 DAT_1005c9a0;				// the device's normal D3DRENDERSTATE_ZENABLE value (declared by unit unk/1002d080)
// GLOBAL: D3DREN 0x1005de1c
extern float DAT_1005de1c;				// guess: 0.5f set when the device is up
void FUN_100139f0();					// stage 0 texture blend (modulate), d3d_draw.h
void d3d_SetDetailTextureStates(void);				// d3dstate.h: stage 1 colour op = add-signed / modulate
void d3d_UnsetDetailTexture(void);				// d3dstate.h: disable stage 1 colour op and unbind its texture

// NAME: d3d_CreateDevice: Jupiter d3d_device.cpp CD3D_Device::CreateDevice / d3d_init.cpp (the DirectDraw 7 form: DirectDrawCreateEx, cooperative
// level, display mode, primary surface (flipping chain, or window primary + offscreen with a clipper), the Z buffer and device through FUN_1001aa70,
// the viewport, the capability flags the dump (LISTDEVICECAPS) prints, the default render states, the two-stage validation and the lightmap mode).
// Low confidence for the name (the strings are Talon's own); roles: pNode is the enumerated device node (UnkType_DeviceNode), pInit the mode
// the engine asked for.  Returns 1 on success.
// STUB diagnosis: the large local frame differs (exe: ddsd at esp+0x58, desc +0xd4, bltfx +0x1c0, DDCAPS hal +0x224 / hel +0x3a0, viewport +0x38;
//   ours ddsd at +0x48; frame size 0x50c matches once the 16-byte state object is a function-scope local and the caps/free-memory DWORDs sit in an
//   inner scope).  The initial flipping-surface failure and windowed primary-surface failure now share the primary-surface message/cleanup label,
//   matching the exe's `jne 0x1001aff3`; the retry failure keeps its separate message push at 0x1001af72.  The reference-rasterizer,
//   QueryInterface, and Force1Pass diagnostic literals now match the exe's strings at 0x1004b094, 0x1004aff8, and 0x1004aebc.
// STUB: D3DREN 0x1001acc0
int FUN_1001acc0(UnkType_DeviceNode *pNode, RenderStructInit *pInit)
{
	DDCAPS ddcapsHel;
	DDCAPS ddcapsHal;
	DDBLTFX bltfx;
	D3DDEVICEDESC7 desc;
	DDSURFACEDESC2 ddsd;
	D3DVIEWPORT7 vp;
	uint32 aState[4];
	GUID guid;
	DWORD dwCoop;
	HLTPARAM hParam;
	int i;

	ShowCursor(0);
	if (DAT_1005848c)
	{
		g_pStruct->ConsolePrint("USING DIRECT3D REFERENCE RASTERIZER (SLOOOOOOOW!)");
		DAT_10057828 = DDSCAPS_SYSTEMMEMORY;
		guid = IID_IDirect3DRefDevice;
	}
	else if (DAT_10058488)
	{
		DAT_10057828 = DDSCAPS_SYSTEMMEMORY;
		guid = IID_IDirect3DRGBDevice;
	}
	else if (DAT_10058490)
	{
		DAT_10057828 = DDSCAPS_SYSTEMMEMORY;
		guid = IID_IDirect3DMMXDevice;
	}
	else if (DAT_10058494)
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

	if (!g_bRunWindowed && !DAT_10057a10)
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
		ddsd.dwBackBufferCount = (DAT_10058478 != 0) + 1;
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
		ddsd.dwHeight = pInit->m_Mode.m_Height;
		ddsd.dwWidth = pInit->m_Mode.m_Width;
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		ddsd.ddsCaps.dwCaps = DAT_10057828 | DDSCAPS_3DDEVICE;
		ddsd.dwSize = sizeof(ddsd);
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

	if (!FUN_1001aa70(pInit, guid))
	{
		AddDebugMessage(1, "Failed to create device.");
		d3d_FreeDDraw();
		DAT_10058494 = 0;
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

	memset(&ddcapsHel, 0, sizeof(ddcapsHel));
	memset(&ddcapsHal, 0, sizeof(ddcapsHal));
	ddcapsHal.dwSize = sizeof(DDCAPS);
	ddcapsHel.dwSize = sizeof(DDCAPS);
	g_pDD->GetCaps(&ddcapsHal, &ddcapsHel);
	DAT_1005c874 = (ddcapsHal.dwCaps2 >> 12) & 1;

	memset(&desc, 0, sizeof(desc));
	g_pD3DDevice->GetCaps(&desc);

	{
		DDSCAPS2 caps;
		DWORD dwTotalTex, dwFreeTex, dwTotalVid, dwFreeVid;

		memset(&caps, 0, sizeof(caps));
		caps.dwCaps = DDSCAPS_TEXTURE;
		g_pDD->GetAvailableVidMem(&caps, &dwTotalTex, &dwFreeTex);
		memset(&caps, 0, sizeof(caps));
		caps.dwCaps = DDSCAPS_VIDEOMEMORY;
		g_pDD->GetAvailableVidMem(&caps, &dwTotalVid, &dwFreeVid);

		DAT_1005c854 = (desc.dwDevCaps >> 10) & 1;
		DAT_1005c858 = (desc.dwDevCaps >> 9) & 1;
		DAT_1005c85c = (desc.dwDevCaps >> 8) & 1;
		DAT_1005c860 = (desc.dwDevCaps >> 12) & 1;
		if (desc.dwDeviceZBufferBitDepth == DDBD_8)
			DAT_1005c864 = 8;
		else if (desc.dwDeviceZBufferBitDepth == DDBD_16)
			DAT_1005c864 = 16;
		else if (desc.dwDeviceZBufferBitDepth == DDBD_24)
			DAT_1005c864 = 24;
		else
			DAT_1005c864 = desc.dwDeviceZBufferBitDepth == DDBD_32 ? 32 : -1;
		if (desc.dwDeviceRenderBitDepth == DDBD_8)
			DAT_1005c868 = 8;
		else if (desc.dwDeviceRenderBitDepth == DDBD_16)
			DAT_1005c868 = 16;
		else if (desc.dwDeviceRenderBitDepth == DDBD_24)
			DAT_1005c868 = 24;
		else
			DAT_1005c868 = desc.dwDeviceRenderBitDepth == DDBD_32 ? 32 : -1;
		DAT_1005c850 = desc.dpcTriCaps.dwRasterCaps & 1;
		DAT_1005c840 = (desc.dpcTriCaps.dwSrcBlendCaps >> 2) & 1;
		DAT_1005c844 = (desc.dpcTriCaps.dwDestBlendCaps >> 4) & 1;
		DAT_1005c84c = (desc.dpcTriCaps.dwTextureBlendCaps >> 1) & 1;
		DAT_1005c870 = dwFreeVid;
		DAT_1005c848 = (desc.dpcTriCaps.dwTextureBlendCaps >> 7) & 1;
		DAT_1005c86c = dwTotalTex;
		DAT_1005c9a0 = 1;
		memcpy(&DAT_1005c878, &desc, 0x3b * 4);
		memcpy(&DAT_1005c964, &desc.dpcTriCaps, 0xe * 4);
	}

	g_pD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGTABLEMODE, D3DFOG_LINEAR);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ANTIALIAS, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, DAT_1005c9a0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DITHERENABLE, 1);
	FUN_100139f0();
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CLIPPING, g_CV_UseD3DClip.m_IntVal != 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_LIGHTING, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_EXTENTS, 0);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	{
		DWORD dwPasses;

		FUN_100246b7((UnkType_SavedStage1 *)aState);
		DAT_1005c80c = g_pD3DDevice->ValidateDevice(&dwPasses) == 0;
		FUN_1002473b((UnkType_SavedStage1 *)aState);
		d3d_SetDetailTextureStates();
		DAT_1005de2c = g_pD3DDevice->ValidateDevice(&dwPasses) == 0;
		if (DAT_1005c878.wMaxSimultaneousTextures < 2)
			DAT_1005de2c = 0;
		d3d_UnsetDetailTexture();
	}

	DAT_1005c808 = (~DAT_1005c964.dwRasterCaps >> 15) & 1;
	if (DAT_1005c808 && (DAT_1005c964.dwSrcBlendCaps & 0x10))
		DAT_1005cdf0 = 1;
	else
		DAT_1005cdf0 = 0;
	if ((DAT_1005c964.dwSrcBlendCaps & 1) && (DAT_1005c964.dwDestBlendCaps & 4))
		DAT_1005de20 = 1;
	else
		DAT_1005de20 = 0;
	if ((DAT_1005c964.dwSrcBlendCaps & 2) && (DAT_1005c964.dwDestBlendCaps & 2))
		DAT_1005cdc8 = 1;
	else
		DAT_1005cdc8 = 0;

	DAT_1005c838 = 0;
	g_NormalTextureStage = 0;
	DAT_1005c7e0 = DAT_10058480;
	if (DAT_10058480)
	{
		DWORD dwPasses;

		FUN_100356d5();
		if (g_pD3DDevice->ValidateDevice(&dwPasses) == 0 && DAT_1005c878.wMaxSimultaneousTextures > 1)
		{
			DAT_1005c838 = 1;
			AddDebugMessage(0, "Using 1 pass lightmapping!");
		}
		else
		{
			DAT_1005c7e0 = 0;
			AddDebugMessage(0, "Force1Pass doesn't work on this card.  Using 2 pass lightmapping.");
		}
	}
	else
	{
		AddDebugMessage(0, "Using 2 pass lightmapping.");
	}

	CheckSpecialCards();
	FUN_1001be30("lithtech.gam");
	if (!FUN_1001ec50())
	{
		d3d_FreeDDraw();
		return 0;
	}
	DAT_1005de1c = 0.5f;
	return 1;
}

// ---- RenderCommand --------------------------------------------------------------------------------------------------------
// the screen format (filled by the device bring-up, defined in sys/d3d/common_init)
// GLOBAL: D3DREN 0x100577c8
extern PFormat DAT_100577c8;

int FUN_1002d07c(char *pFileName);						// unk/1002d000 stub (returns 0)

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
						(DAT_100577c8.m_eType == BPP_16) ? 16 : 32);
					g_pStruct->ConsolePrint("---------------------------------------------------------------");
				}
			}
			g_pStruct->ConsolePrint("Portals: %s", (DAT_1005cde0 > 0) ? "Yes" : "No");
			g_pStruct->ConsolePrint("Model fullbrites: %s, Gouraud fullbrites: %s", DAT_1005c808 ? "Yes" : "No", DAT_1005cdf0 ? "Yes" : "No");
			g_pStruct->ConsolePrint("Lightmap capable: %s, Light add poly: %s", DAT_1005de20 ? "Yes" : "No", DAT_1005cdc8 ? "Yes" : "No");
			g_pStruct->ConsolePrint("DrawPrim: %s, Dither: %s", DAT_1005c854 ? "Yes" : "No", DAT_1005c850 ? "Yes" : "No");
			g_pStruct->ConsolePrint("Src blend SRCCOLOR: %s, Dest blend SRCALPHA: %s", DAT_1005c840 ? "Yes" : "No", DAT_1005c844 ? "Yes" : "No");
			g_pStruct->ConsolePrint("TBlend Add: %s, TBlend Modulate (gouraud): %s", DAT_1005c848 ? "Yes" : "No", DAT_1005c84c ? "Yes" : "No");
			g_pStruct->ConsolePrint("LINEARMIPNEAREST supported: %s", (DAT_1005c964.dwTextureFilterCaps & D3DPTFILTERCAPS_LINEARMIPNEAREST) ? "Yes" : "No");
			g_pStruct->ConsolePrint("Square textures only: %s, Vid mem textures: %s", (DAT_1005c964.dwTextureCaps & D3DPTEXTURECAPS_SQUAREONLY) ? "Yes" : "No", DAT_1005c858 ? "Yes" : "No");
			g_pStruct->ConsolePrint("System mem textures: %s, AGP mem textures: %s", DAT_1005c85c ? "Yes" : "No", DAT_1005c860 ? "Yes" : "No");
			g_pStruct->ConsolePrint("Texture Memory: %d, Video Memory: %d", DAT_1005c86c, DAT_1005c870);
			g_pStruct->ConsolePrint("ZBuffer Depth: %d, Device Depth: %d", DAT_1005c864, DAT_1005c868);
			g_pStruct->ConsolePrint("Z-test: %s, Table fog: %s, Palette alpha: %s", (DAT_1005c964.dwRasterCaps & D3DPRASTERCAPS_ZTEST) ? "Yes" : "No",
				(DAT_1005c964.dwRasterCaps & D3DPRASTERCAPS_FOGTABLE) ? "Yes" : "No", (DAT_1005c964.dwTextureCaps & D3DPTEXTURECAPS_ALPHAPALETTE) ? "Yes" : "No");
			g_pStruct->ConsolePrint("Max texture size: (%d x %d)", DAT_1005c8fc, DAT_1005c900);
			g_pStruct->ConsolePrint("Surfaces larger than screen: %s", DAT_1005c874 ? "Yes" : "No");
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
			FUN_1001be30(argv[1]);
			return;
		}
		if (_strcmpi(argv[0], "PortalFile") == 0)
		{
			if (argc >= 2)
			{
				FUN_1002d07c(argv[1]);
				return;
			}
			d3d_NullCallback();
		}
	}
}
