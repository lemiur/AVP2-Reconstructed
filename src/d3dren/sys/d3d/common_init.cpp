// d3d.ren sys/d3d/common_init (0x1001094c-0x1001116b): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// DDPFToPFormat (engine cutil copy) and r_GetBufferFormatOfSurface sit inside; the names_proposal d3d_utils/d3d_init labels for
// them are not separate objects.
// FLAGS: /O1 /Ob2
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <windows.h>
#include "ltbasedefs.h"
#include "counter.h"
#include "../../../../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/tlvertex.h"
#include "d3dren/d3ddevice.h"
#include "pixelformat.h"
#include "d3dren/common_init.h"
// GLOBAL: D3DREN 0x10057b58
extern int g_nRenderFrameCount;				// guess: frame counter (incremented per d3d_InitFrame, zeroed by d3d_Init)

// ------------------------------------------------------------------ //
// The lists of polygons touched by dynamic lights (names unknown, shapes in the comments).
// ------------------------------------------------------------------ //

// The per-poly record of a dynamic light touching it (StructBank g_PolyLightBank, 0x14 bytes) and the list of lit polys
// (StructBank g_LitPolyBank, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;		// 0x00
	LTObject			*m_pLight;		// 0x04
	LTVector			m_Pos;			// 0x08 the light position in the world model space
};

#define WORLDPOLY_LIGHTS(p)	(*(UnkType_PolyLight**)((uint8*)(p) + 0x30))

// ------------------------------------------------------------------ //
// common_init (Jupiter render_a/src/sys/d3d/common_init.cpp, d3d_shell.cpp)
// ------------------------------------------------------------------ //

// The list of the enumerated DirectDraw devices (nodes: UnkType_DeviceNode, dalloc'ed by the enumeration callback in
// unit unk/10030bb0).  names_proposal: guess_g_DeviceList (low), so the Ghidra name stays.
// FUNCTION: D3DREN 0x1001094c _$E2
// GLOBAL: D3DREN 0x10057800
LTLink g_EnumeratedDeviceList(LTLink_Init);

// The screen pixel format (its inline constructor stores the PFormat vtable).
// FUNCTION: D3DREN 0x1001095c _$E5
// GLOBAL: D3DREN 0x100577c8
PFormat g_ScreenPixelFormat;

// guess: frees the device list (the node data is dalloc'ed) and ties the head off again
// FUNCTION: D3DREN 0x10010967
void d3d_FreeDeviceList()
{
	LTLink *pHead = &g_EnumeratedDeviceList;
	LTLink *pCur, *pNext;

	for (pCur = pHead->m_pNext; pCur != pHead; pCur = pNext)
	{
		pNext = pCur->m_pNext;
		dfree(pCur->m_pData);
	}
	pHead->TieOff();
}

// guess: the LISTDEVICES console command: prints "Device: %s" for each device
// FUNCTION: D3DREN 0x10010998
void d3d_ListDevices()
{
	LTLink *pCur;

	for (pCur = g_EnumeratedDeviceList.m_pPrev; pCur != &g_EnumeratedDeviceList; pCur = pCur->m_pPrev)
	{
		g_pStruct->ConsolePrint("Device: %s", ((UnkType_DeviceNode *)pCur->m_pData)->m_Unk24);
	}
}

// NAME: d3d_GetHook (medium): RenderStruct::GetHook (+0xc0), Jupiter common_init.cpp's rdll_RenderDLLSetup names the slots d3d_*
// FUNCTION: D3DREN 0x100109c6
void *d3d_GetHook(char *pName)
{
	if (strcmp(pName, "LPDIRECTDRAW") == 0)
		return g_pDD;

	if (strcmp(pName, "BACKBUFFER") == 0)
		return g_pOffscreen;

	return 0;
}

// NAME: DDPFToPFormat: the engine's own copy of src/client/cutil.cpp DDPFToPFormat (0x00426ac0)
// FUNCTION: D3DREN 0x100109fd
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat)
{
	BPPIdent type;

	if (pDDPF->dwRGBBitCount == 16)
		type = BPP_16;
	else
		type = BPP_32;

	pFormat->Init(type,
		pDDPF->dwRGBAlphaBitMask, pDDPF->dwRBitMask, pDDPF->dwGBitMask, pDDPF->dwBBitMask);
}

// NAME: r_GetBufferFormatOfSurface: the string printed by d3d_Init when it fails ("r_GetBufferFormatOfSurface failed.")
// FUNCTION: D3DREN 0x10010a24
LTBOOL r_GetBufferFormatOfSurface(LPDIRECTDRAWSURFACE7 pSurface, PFormat *pFormat)
{
	DDPIXELFORMAT ddpf;

	memset(&ddpf, 0, sizeof(ddpf));
	ddpf.dwSize = sizeof(ddpf);
	if (FAILED(pSurface->GetPixelFormat(&ddpf)))
		return LTFALSE;

	DDPFToPFormat(&ddpf, pFormat);
	return LTTRUE;
}

// Functions of the device bring-up unit (sys/d3d/d3d_init) and of this unit further down.
void UppercaseStringInPlace(char *pStr);
void CD3D_Device_FreeDevice();
void d3d_TermRendererModules();
int d3d_InitRendererModules();
int d3d_CreateDevice(UnkType_DeviceNode *pDevice, RenderStructInit *pInit);

// guess: releases the DirectDraw objects (clipper, primary/offscreen surfaces, IDirectDraw7) and shows the cursor again
// (names_proposal: guess_d3d_FreeDDraw, low)
// FUNCTION: D3DREN 0x10010a69
void d3d_FreeDDraw()
{
	ShowCursor(1);
	CD3D_Device_FreeDevice();

	if (g_pPrimary)
	{
		g_pPrimary->Release();
		g_pPrimary = 0;
		if (g_pBackBuffer)
		{
			g_pBackBuffer->Release();
			g_pBackBuffer = 0;
		}
		g_pOffscreen = 0;
		if (g_pClipper)
		{
			g_pClipper->Release();
			g_pClipper = 0;
		}
	}
	else
	{
		if (g_pBackBuffer)
		{
			g_pBackBuffer->Release();
			g_pBackBuffer = 0;
		}
		if (g_pOffscreen)
			g_pOffscreen = 0;
	}

	if (g_pDD)
	{
		g_pDD->RestoreDisplayMode();
		g_pDD->SetCooperativeLevel(0, DDSCL_NORMAL);
		g_pDD->Release();
		g_pDD = 0;
	}
}

// NAME: d3d_Term: RenderStruct::Term (+0x74)
// FUNCTION: D3DREN 0x10010b13
void d3d_Term()
{
	d3d_FreeDeviceList();
	d3d_TermRendererModules();
	d3d_FreeDDraw();
}


// Globals of d3d_Init.
// GLOBAL: D3DREN 0x10057e30
extern int g_nSysMemParameter;				// guess: the "SysMem" console parameter as an int
int g_nWindowBlitScaleY;				// guess: vertical divisor of the RenderStruct height (1); GLOBAL in common_init.h
int g_nWindowBlitScaleX;				// guess: horizontal divisor of the RenderStruct width (1)
// GLOBAL: D3DREN 0x100584e4
extern int g_bWarbleTableInitialized;
// GLOBAL: D3DREN 0x100584e8
extern int g_bFogStateInitialized;
// GLOBAL: D3DREN 0x100584ec
extern int g_bDitherStateInitialized;
// GLOBAL: D3DREN 0x100584f0
extern int g_bTextureFilterStateInitialized;

BOOL EnumerateDirectDrawDevices();
int d3d_InitRendererModules();
LTBOOL d3d_IsNullRenderOn();
UnkType_DeviceNode *d3d_FindDeviceByName(char *pName);
// Minimal view of VisibleSet (include/d3dren/visibleset.h, unit sys/d3d/tagnodes): that header's CMoArray members make this object
// emit an extra static initialiser, which would shift the _$E numbers of the whole unit, so only the two declarations d3d_Init needs
// are repeated here (same mangled names).
class VisibleSet { public: int Init(); };		// 0x10038cf0
VisibleSet *d3d_GetVisibleSet();				// 0x10039e00

// NAME: d3d_Init: RenderStruct::Init (+0x70), Jupiter common_init.cpp's rdll_RenderDLLSetup name for the slot (names_proposal medium)
// STUB: D3DREN 0x10010b22
// Remaining difference: 20 of 747 bytes (8 aligned instructions, 7 ignoring stack offsets). The device-search and failure
// paths now follow the original; only the RECT setup at 0x10010d1a-0x10010d30 differs in register choice and scheduling.
// The original deliberately reuses the horizontal center for both left and top; retain that behavior.
int d3d_Init(RenderStructInit *pInit)
{
	UnkType_DeviceNode *pDevice;
	LTLink *pCur;
	char *pWindowed;
	RECT rcDesktop, rcWindow;

	pInit->m_RendererVersion = LTRENDER_VERSION;

	d3d_CreateConsoleVariables();
	d3d_ReadConsoleVariables();

	pWindowed = g_pStruct->GetParameterValueString(g_pStruct->GetParameter("windowed"));
	g_bRunWindowed = (pWindowed && atoi(pWindowed) == 1);

	g_nSysMemParameter = (int)g_pStruct->GetParameterValueFloat(g_pStruct->GetParameter("SysMem"));

	g_hWnd = (HWND)pInit->m_hWnd;
	g_bSpecialRenderMode = 0;
	g_RenderSurfaceMemoryCaps = 0x4000;
	g_nWindowBlitScaleY = 1;
	g_nWindowBlitScaleX = 1;
	g_ScreenWidth = pInit->m_Mode.m_Width;
	g_ScreenHeight = pInit->m_Mode.m_Height;

	if (!EnumerateDirectDrawDevices())
		return 10;

	pDevice = 0;
	if (pInit->m_Mode.m_InternalName[0])
	{
		pDevice = d3d_FindDeviceByName(pInit->m_Mode.m_InternalName);
		if (pDevice)
		{
			if (d3d_CreateDevice(pDevice, pInit))
				goto DeviceReady;

			AddDebugMessage(1, "Can't initialize hardware device: %s", pInit->m_Mode.m_InternalName);
		}
		else
		{
			AddDebugMessage(1, "Can't find hardware device: %s", pInit->m_Mode.m_InternalName);
		}
	}

	for (pCur = g_EnumeratedDeviceList.m_pNext; pCur != &g_EnumeratedDeviceList; pCur = pCur->m_pNext)
	{
		pDevice = (UnkType_DeviceNode *)pCur->m_pData;
		if (d3d_CreateDevice(pDevice, pInit))
			goto DeviceReady;
	}

	if (pCur == &g_EnumeratedDeviceList)
		goto NoDevice;

DeviceReady:
	if (pDevice == 0)
	{
NoDevice:
		d3d_FreeDeviceList();
		d3d_TermRendererModules();
		d3d_FreeDDraw();
		AddDebugMessage(0, "Can't find any d3d devices to use!");
		return 1;
	}
	else
	{
		strncpy(pInit->m_Mode.m_InternalName, pDevice->m_Unk24, 127);
		strncpy(pInit->m_Mode.m_Description, pDevice->m_Unk88, 127);
		AddDebugMessage(0, "Using Direct3D Device %s", pDevice->m_Unk24);

		g_nRenderFrameCount = 0;
		g_bWarbleTableInitialized = 0;
		g_bFogStateInitialized = 0;
		g_bDitherStateInitialized = 0;
		g_bTextureFilterStateInitialized = 0;

		if (!g_bRunWindowed && !d3d_IsNullRenderOn())
		{
			SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		}
		else
		{
			// Center the window on the desktop.
			GetWindowRect(GetDesktopWindow(), &rcDesktop);
			rcWindow.top = (uint32)((rcDesktop.right - pInit->m_Mode.m_Width) - rcDesktop.left) >> 1;
			rcWindow.right = pInit->m_Mode.m_Width + rcWindow.top;
			rcWindow.bottom = rcWindow.top + pInit->m_Mode.m_Height;
			rcWindow.left = rcWindow.top;
			AdjustWindowRect(&rcWindow, 0xcf0000, 0);
			if (rcWindow.left < 0)
			{
				rcWindow.right -= rcWindow.left;
				rcWindow.left = 0;
			}
			if (rcWindow.top < 0)
			{
				rcWindow.bottom -= rcWindow.top;
				rcWindow.top = 0;
			}
			SetWindowPos(g_hWnd, 0, rcWindow.left, rcWindow.top,
			rcWindow.right - rcWindow.left, rcWindow.bottom - rcWindow.top, SWP_NOOWNERZORDER);
		}

		if (!r_GetBufferFormatOfSurface(g_pBackBuffer, &g_ScreenPixelFormat))
		{
			d3d_FreeDeviceList();
			d3d_TermRendererModules();
			d3d_FreeDDraw();
			AddDebugMessage(0, "r_GetBufferFormatOfSurface failed.");
			return 1;
		}
		else
		{
			d3d_InitRendererModules();
			g_pStruct->m_Width = g_pStruct->m_Width / g_nWindowBlitScaleX;
			g_pStruct->m_Height = g_pStruct->m_Height / g_nWindowBlitScaleY;

			if (!d3d_GetVisibleSet()->Init())
			{
				d3d_FreeDeviceList();
				d3d_TermRendererModules();
				d3d_FreeDDraw();
				AddDebugMessage(0, "VisibleSet::Init failed (invalid object list size?).");
				return 1;
			}
			return 0;
		}
	}
}

// NAME: d3d_IsNullRenderOn: Jupiter common_init.cpp (same job: the "nullrender" console parameter != 0)
// FUNCTION: D3DREN 0x10010e0d
LTBOOL d3d_IsNullRenderOn()
{
	return g_pStruct->GetParameterValueFloat(g_pStruct->GetParameter("nullrender")) != 0.0f;
}

// guess: finds an enumerated device by (upper-cased) name; names_proposal: guess_d3d_FindDevice (low)
// FUNCTION: D3DREN 0x10010e36
UnkType_DeviceNode *d3d_FindDeviceByName(char *pName)
{
	char szRequested[100];
	char szDevice[100];
	LTLink *pCur;
	UnkType_DeviceNode *pDevice;

	strncpy(szRequested, pName, 100);
	UppercaseStringInPlace(szRequested);

	for (pCur = g_EnumeratedDeviceList.m_pNext; pCur != &g_EnumeratedDeviceList; pCur = pCur->m_pNext)
	{
		pDevice = (UnkType_DeviceNode *)pCur->m_pData;
		strncpy(szDevice, pDevice->m_Unk24, 100);
		UppercaseStringInPlace(szDevice);
		if (strcmp(szRequested, szDevice) == 0)
			return pDevice;
	}

	return 0;
}

// GLOBAL: D3DREN 0x100577c0
RMode *g_pModeList;						// the list GetSupportedModes returns (head; RMode::m_pNext)
// GLOBAL: D3DREN 0x1005780c
UnkType_DeviceNode *g_pModeEnumerationDevice;		// guess: the device whose display modes are being enumerated

// guess: IDirectDraw4::EnumDisplayModes callback of GetSupportedModes (names_proposal: guess_EnumDisplayModesCallback, low):
// adds a 16/32 bit mode of the current device to g_pModeList
// FUNCTION: D3DREN 0x10010eb3
HRESULT WINAPI d3d_EnumDisplayModeCallback(LPDDSURFACEDESC2 pDesc, LPVOID pContext)
{
	RMode *pMode;

	if (pDesc->ddpfPixelFormat.dwRGBBitCount == 16 || pDesc->ddpfPixelFormat.dwRGBBitCount == 32)
	{
		pMode = (RMode *)malloc(sizeof(RMode));
		if (pMode)
		{
			pMode->m_bHardware = 1;
			strncpy(pMode->m_InternalName, g_pModeEnumerationDevice->m_Unk24, 127);
			strncpy(pMode->m_Description, g_pModeEnumerationDevice->m_Unk88, 127);
			pMode->m_Width = pDesc->dwWidth;
			pMode->m_Height = pDesc->dwHeight;
			pMode->m_BitDepth = pDesc->ddpfPixelFormat.dwRGBBitCount;
			pMode->m_pNext = g_pModeList;
			g_pModeList = pMode;
		}
	}

	return DDENUMRET_OK;
}

// The enumeration of the DirectDraw devices (0x10031d18, unit unk/10030bb0).
BOOL EnumerateDirectDrawDevices();

// Export of the DLL: the engine calls it through GetProcAddress ("GetSupportedModes", dsys_interface.cpp).
// FUNCTION: D3DREN 0x10010f44
extern "C" RMode *GetSupportedModes()
{
	LTLink *pCur;
	UnkType_DeviceNode *pDevice;
	LPDIRECTDRAW pDD;
	LPDIRECTDRAW4 pDD4;

	g_pModeList = 0;
	EnumerateDirectDrawDevices();

	for (pCur = g_EnumeratedDeviceList.m_pNext; pCur != &g_EnumeratedDeviceList; pCur = pCur->m_pNext)
	{
		pDevice = (UnkType_DeviceNode *)pCur->m_pData;
		if (DirectDrawCreate(pDevice->m_pGuid, &pDD, 0) == 0)
		{
			if (pDD->QueryInterface(IID_IDirectDraw4, (void **)&pDD4) == 0)
			{
				g_pModeEnumerationDevice = pDevice;
				pDD4->EnumDisplayModes(0, 0, 0, d3d_EnumDisplayModeCallback);
				pDD4->Release();
			}
			pDD->Release();
		}
	}

	d3d_FreeDeviceList();
	return g_pModeList;
}

// Export of the DLL: frees the list GetSupportedModes returned.
// FUNCTION: D3DREN 0x10010fd3
extern "C" void FreeModeList(RMode *pModes)
{
	RMode *pNext;

	if (pModes)
	{
		do
		{
			pNext = pModes->m_pNext;
			free(pModes);
			pModes = pNext;
		} while (pModes);
	}
}

// The functions the engine's RenderStruct gets (slot names from include/renderstruct.h, d3d_ prefix from Jupiter
// common_init.cpp rdll_RenderDLLSetup, names_proposal medium).  Declared here with the member signatures; their definitions
// are in the units of the device (d3d_init), surface (d3d_surface, d3d_texture) and draw code.
void d3d_BindTexture(SharedTexture *pTexture, LTBOOL bTextureChanged);							// 0x10021b50
void d3d_UnbindTexture(SharedTexture *pTexture);												// 0x10021c60
void d3d_Clear(LTRect *pRect, uint32 flags, LTVector *pColor);									// 0x100142f0
int d3d_RenderScene(SceneDesc *pScene);															// 0x10017aa0
void d3d_SwapBuffers(uint32 flags);																// 0x1001e189
int d3d_GetInfoFlags();																				// 0x1001bd70 (slot 0xc8, returns 0)
void d3d_GetScreenFormat(PFormat *pFormat);													// 0x1001dec3
HLTBUFFER d3d_CreateSurface(int width, int height);												// 0x1001da7f
void d3d_DeleteSurface(HLTBUFFER hSurf);														// 0x1001db2d
void d3d_GetSurfaceInfo(HLTBUFFER hSurf, uint32 *pWidth, uint32 *pHeight, long *pPitch);		// 0x1001db4e
void *d3d_LockSurface(HLTBUFFER hSurf);															// 0x1001db7e
void d3d_UnlockSurface(HLTBUFFER hSurf);														// 0x1001dbb4
LTBOOL d3d_LockScreen(int left, int top, int right, int bottom, void **pData, long *pPitch);	// 0x1001dbca
void d3d_UnlockScreen();																		// 0x1001dc4f
void d3d_MakeScreenShot(const char *pFilename);													// 0x1001df19
void d3d_BlitToScreen(BlitRequest *pRequest);													// 0x1001de1c
void d3d_BlitFromScreen(BlitRequest *pRequest);													// 0x1001dc81
LTBOOL d3d_WarpToScreen(BlitRequest *pRequest);													// 0x1001de85
// The optimized 2D / surface functions of unit sys/d3d/d3d_surface (prototypes of its header include/d3dren/d3d_surface.h, which
// is not included here: it declares a class with a static initialiser that would shift the _$E numbering of this object).
void d3d_UnoptimizeSurface(HLTBUFFER hBuffer);							// 0x1001c3d2
LTBOOL d3d_OptimizeSurface(HLTBUFFER hBuffer, PValue transparentColor);	// 0x1001c3e4
LTBOOL d3d_StartOptimized2D();											// 0x1001c627
void d3d_EndOptimized2D();												// 0x1001c725
LTBOOL d3d_IsInOptimized2D();											// 0x1001c7eb
LTBOOL d3d_SetOptimized2DBlend(LTSurfaceBlend blend);					// 0x1001c7f1
LTBOOL d3d_SetOptimized2DColor(HLTCOLOR color);							// 0x1001c8b8
LTBOOL d3d_GetOptimized2DBlend(LTSurfaceBlend &blend);					// 0x1001c8ca
LTBOOL d3d_GetOptimized2DColor(HLTCOLOR &color);						// 0x1001c8da
HRENDERCONTEXT d3d_CreateContext(RenderContextInit *pInit);										// 0x1001b700 (unit sys/d3d/d3d_init)
void d3d_DeleteContext(HRENDERCONTEXT hContext);												// 0x1001b770
void d3d_RebindLightmaps(RenderContext *pContext);												// 0x1001b790
int d3d_Start3D();																				// 0x1001bd80
int d3d_End3D();																				// 0x1001bdb0
int d3d_IsIn3D();																				// 0x1001be20
void d3d_RenderCommand(int argc, char **argv);													// 0x1001b8a0
int d3d_Init(RenderStructInit *pInit);															// 0x10010b22

// Stores a function address in a RenderStruct slot.  The slots are typed in include/renderstruct.h, but some of them are
// padding there (GetOptimized2DBlend/Color, IsInOptimized2D and the unnamed 0xc8) and several functions of the units that
// define them take Jupiter-style arguments, so the store goes through void *.
#define RS_SET(member, fn)	(*(void **)&pStruct->member = (void *)(fn))
#define RS_SET_PAD(offset, fn)	(*(void **)((uint8 *)pStruct + (offset)) = (void *)(fn))

// Export of the DLL: the engine passes its RenderStruct and the renderer fills in the function table.
// FUNCTION: D3DREN 0x10010ff1
extern "C" void RenderDLLSetup(RenderStruct *pStruct)
{
	g_pStruct = pStruct;

	RS_SET(Init, d3d_Init);
	RS_SET(Term, d3d_Term);
	RS_SET(BindTexture, d3d_BindTexture);
	RS_SET(UnbindTexture, d3d_UnbindTexture);
	RS_SET(RebindLightmaps, d3d_RebindLightmaps);
	RS_SET(CreateContext, d3d_CreateContext);
	RS_SET(DeleteContext, d3d_DeleteContext);
	RS_SET(Clear, d3d_Clear);
	RS_SET(Start3D, d3d_Start3D);
	RS_SET(End3D, d3d_End3D);
	RS_SET(IsIn3D, d3d_IsIn3D);
	RS_SET(StartOptimized2D, d3d_StartOptimized2D);
	RS_SET(EndOptimized2D, d3d_EndOptimized2D);
	RS_SET(SetOptimized2DBlend, d3d_SetOptimized2DBlend);
	RS_SET_PAD(0xac, d3d_GetOptimized2DBlend);
	RS_SET(SetOptimized2DColor, d3d_SetOptimized2DColor);
	RS_SET_PAD(0xb4, d3d_GetOptimized2DColor);
	RS_SET_PAD(0xa4, d3d_IsInOptimized2D);
	RS_SET(OptimizeSurface, d3d_OptimizeSurface);
	RS_SET(UnoptimizeSurface, d3d_UnoptimizeSurface);
	RS_SET(RenderScene, d3d_RenderScene);
	RS_SET(RenderCommand, d3d_RenderCommand);
	RS_SET(GetHook, d3d_GetHook);
	RS_SET(SwapBuffers, d3d_SwapBuffers);
	RS_SET_PAD(0xc8, d3d_GetInfoFlags);
	RS_SET(GetScreenFormat, d3d_GetScreenFormat);
	RS_SET(CreateSurface, d3d_CreateSurface);
	RS_SET(DeleteSurface, d3d_DeleteSurface);
	RS_SET(GetSurfaceInfo, d3d_GetSurfaceInfo);
	RS_SET(LockSurface, d3d_LockSurface);
	RS_SET(UnlockSurface, d3d_UnlockSurface);
	RS_SET(LockScreen, d3d_LockScreen);
	RS_SET(UnlockScreen, d3d_UnlockScreen);
	RS_SET(MakeScreenShot, d3d_MakeScreenShot);
	RS_SET(ReadConsoleVariables, d3d_ReadConsoleVariables);
	RS_SET(BlitToScreen, d3d_BlitToScreen);
	RS_SET(BlitFromScreen, d3d_BlitFromScreen);
	RS_SET(WarpToScreen, d3d_WarpToScreen);
}
// END EARLY

// NAME: g_hWnd, g_bRunWindowed, g_ScreenWidth, g_ScreenHeight: Jupiter common_stuff.cpp globals of the same names (names_proposal high);
// their GLOBAL annotations are in common_stuff.h.
HWND g_hWnd;
int g_bRunWindowed;
uint32 g_ScreenWidth, g_ScreenHeight;
// (defined in sys/d3d/common_stuff)
void UppercaseStringInPlace(char *pStr);

#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'
