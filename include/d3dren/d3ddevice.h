// d3d.ren DirectDraw 7 / Direct3D 7 device state: the globals of the device bring-up object (sys/d3d/d3d_init, 0x10019b10-0x1001bf10).
// Owner: package W3 (unit sys/d3d/d3d_init).  Other packages include this header and never redeclare these globals.
//
// Names: none of these globals has a source-established name except where a NAME: line says so; everything else keeps its
// Ghidra name (DAT_<addr>) and the role is in the comment.  Roles were read from the code of 0x1001acc0 (device creation),
// 0x1001b8a0 (the "LISTDEVICECAPS" dump, whose strings name the capability flags), 0x1001aa70 and 0x1001a8b0.
//
// NAME: g_NormalTextureStage: Jupiter render_a/src/sys/d3d/d3d_init.cpp has `uint32 g_NormalTextureStage;` as the first global
// of the same file that holds g_bInOptimized2D (0x1005de44, also named there); in d3d.ren 0x1005de28 is a texture stage index
// (SetTexture(g_NormalTextureStage, 0), the stage argument of the StateChange applier) set to 0 by the device creation.
#ifndef __D3DREN_D3DDEVICE_H__
#define __D3DREN_D3DDEVICE_H__

#ifndef DIRECTDRAW_VERSION
#define DIRECTDRAW_VERSION 0x0700
#endif
#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0700
#endif
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include "ltbasedefs.h"
#include "renderstruct.h"

// ---- DirectDraw 7 objects (created by 0x1001acc0) -------------------------------------------------------------------------
// GLOBAL: D3DREN 0x10057810
extern IDirectDraw7 *g_pDD;			// DirectDrawCreateEx(guid, &it, IID_IDirectDraw7): GetHook("LPDIRECTDRAW")
// GLOBAL: D3DREN 0x10057814
extern IDirectDrawSurface7 *g_pPrimary;	// windowed: the primary surface (Blt, SetClipper); fullscreen: NULL
// GLOBAL: D3DREN 0x10057818
extern IDirectDrawSurface7 *g_pBackBuffer;	// windowed: the offscreen render target (== g_pOffscreen); fullscreen: the flipping primary
// GLOBAL: D3DREN 0x1005781c
extern IDirectDrawSurface7 *g_pOffscreen;	// the surface the device renders to (Ghidra/ScreenShot string name)
// GLOBAL: D3DREN 0x10057820
extern IDirectDrawSurface7 *g_pZBuffer;	// the z-buffer surface (made by 0x1001aa70, attached to g_pOffscreen)
// GLOBAL: D3DREN 0x10057828
extern uint32 g_RenderSurfaceMemoryCaps;					// DDSCAPS memory flag of the render surfaces (0 = default, 0x800 DDSCAPS_SYSTEMMEMORY)

// ---- Direct3D 7 objects ----------------------------------------------------------------------------------------------------
// GLOBAL: D3DREN 0x1005de30
extern IDirect3DDevice7 *g_pD3DDevice;		// the device: BeginScene +0x14, SetRenderState +0x50, SetTexture +0x8c, SetTextureStageState +0x94, ...
// GLOBAL: D3DREN 0x1005de34
extern IDirect3D7 *g_pD3D;			// QueryInterface(IID_IDirect3D7) result
// GLOBAL: D3DREN 0x1005de38
extern IDirectDrawClipper *g_pClipper;	// windowed-mode clipper

// GLOBAL: D3DREN 0x1005de28
extern uint32 g_NormalTextureStage;			// texture stage of the normal (base) texture; 0 after device creation

// ---- state flags --------------------------------------------------------------------------------------------------------
// GLOBAL: D3DREN 0x1005de40
extern int g_bIn3D;					// guess: g_bIn3D (Start3D set / End3D clear; Jupiter CD3D_Device::m_bIn3D)
// GLOBAL: D3DREN 0x1005de44
extern int g_bInOptimized2D;				// Jupiter d3d_init.cpp name (Ghidra)
// GLOBAL: D3DREN 0x1005de48
extern int g_OldFogEnable;					// Jupiter name (Ghidra): fog enable saved by StartOptimized2D

// ---- device capabilities (filled by 0x1001acc0 from GetCaps/EnumTextureFormats/...; text of 0x1001b8a0 names them) ---------
// GLOBAL: D3DREN 0x1005de20
extern int g_bLightmapCapable;					// "Lightmap capable" (cleared on Permedia 2 and when the lightmap formats are missing)
// GLOBAL: D3DREN 0x1005de2c
extern int g_bTwoTextureStageBlendValidated;					// two-texture-stage blending validated (ValidateDevice with the detail stage setup)
// GLOBAL: D3DREN 0x1005de3c
extern int g_bLoadWholeLightmapSurface;					// set by the special-card check for PowerVR / Rage128 style cards
// GLOBAL: D3DREN 0x1005c808
extern int g_bModelFullbriteCapable;					// "Model fullbrites" (not RasterCaps bit 15)
// GLOBAL: D3DREN 0x1005c80c
extern int g_bModelSpecularBlendValidated;					// ValidateDevice of the first state setup succeeded
// GLOBAL: D3DREN 0x1005c810
extern int g_bModelShadowsSupported;					// shaders usable (cleared by the PowerVR check in 0x1001a8b0)
// GLOBAL: D3DREN 0x1005c814
extern int g_bPowerVRWorkaround;					// set together with g_bModelShadowsSupported = 0 by the PowerVR check
// GLOBAL: D3DREN 0x1005c838
extern int g_LightmapTextureStage;					// one-pass lightmapping in use
// GLOBAL: D3DREN 0x1005cdc8
extern int g_bLightAddPolyCapable;					// "Light add poly"
// GLOBAL: D3DREN 0x1005cdf0
extern int g_bGouraudFullbriteCapable;					// "Gouraud fullbrites"
// GLOBAL: D3DREN 0x1005cdd0
extern DDPIXELFORMAT g_ChosenZBufferPixelFormat;			// pixel format of the chosen z-buffer (copied by 0x1001aa70); +0x10 is g_ChosenZBufferStencilBitDepth
// GLOBAL: D3DREN 0x1005cde0
extern uint32 g_ChosenZBufferStencilBitDepth;					// DDPIXELFORMAT::dwStencilBitDepth of the chosen z-buffer ("Portals" = this > 0); see 0x1001a850
// GLOBAL: D3DREN 0x1005c964
extern D3DPRIMCAPS g_DeviceTriangleCaps;			// copy of the device's dpcTriCaps (0x38 bytes): +8 dwRasterCaps = 0x1005c96c, +0x10 dwSrcBlendCaps
											// = 0x1005c974, +0x14 dwDestBlendCaps = 0x1005c978, +0x20 dwTextureCaps = 0x1005c984,
											// +0x24 dwTextureFilterCaps = 0x1005c988 (the dump text of 0x1001b8a0 tests their bits)
// The capability flags below are filled by 0x1001acc0; the role names are the labels of the LISTDEVICECAPS dump (0x1001b8a0).
// GLOBAL: D3DREN 0x1005c840
extern int g_bSrcBlendSrcColorSupported;					// "Src blend SRCCOLOR"
// GLOBAL: D3DREN 0x1005c844
extern int g_bDestBlendSrcAlphaSupported;					// "Dest blend SRCALPHA"
// GLOBAL: D3DREN 0x1005c848
extern int g_bTextureBlendAddSupported;					// "TBlend Add"
// GLOBAL: D3DREN 0x1005c84c
extern int g_bTextureBlendModulateSupported;					// "TBlend Modulate (gouraud)"
// GLOBAL: D3DREN 0x1005c850
extern int g_bDeviceDitherSupported;					// "Dither"
// GLOBAL: D3DREN 0x1005c854
extern int g_bDeviceDrawPrimitiveSupported;					// "DrawPrim"
// GLOBAL: D3DREN 0x1005c858
extern int g_bVideoMemoryTexturesSupported;					// "Vid mem textures"
// GLOBAL: D3DREN 0x1005c85c
extern int g_bSystemMemoryTexturesSupported;					// "System mem textures"
// GLOBAL: D3DREN 0x1005c860
extern int g_bAGPMemoryTexturesSupported;					// "AGP mem textures"
// GLOBAL: D3DREN 0x1005c864
extern int g_DeviceZBufferBitDepth;					// "ZBuffer Depth" (bits, from DDBD_* flags)
// GLOBAL: D3DREN 0x1005c868
extern int g_DeviceRenderBitDepth;					// "Device Depth" (bits)
// GLOBAL: D3DREN 0x1005c86c
extern int g_DeviceTotalTextureMemory;					// "Texture Memory" (0x4000 set by the creation code)
// GLOBAL: D3DREN 0x1005c870
extern int g_DeviceFreeVideoMemory;					// "Video Memory" (free video memory from GetAvailableVidMem)
// GLOBAL: D3DREN 0x1005c874
extern int g_bSurfacesLargerThanScreenSupported;					// "Surfaces larger than screen"
// GLOBAL: D3DREN 0x1005c8fc
extern uint32 g_DeviceMaxTextureWidth;					// maximum texture width  (D3DDEVICEDESC7::dwMaxTextureWidth of the GetCaps copy at 0x1005c878)
// GLOBAL: D3DREN 0x1005c900
extern uint32 g_DeviceMaxTextureHeight;					// maximum texture height (D3DDEVICEDESC7::dwMaxTextureHeight)

// ---- the enumerated DirectDraw devices (0x10031d18 fills the list in unit unk/10030bb0; common_stuff walks it) ---------------------
// One node per enumerated DirectDraw device (0xec bytes, dalloc'ed by the enumeration callback 0x10031d82), linked with the
// LTLink at its start (AddAfter on the list head g_EnumeratedDeviceList) and carrying itself in m_pData.  Names invented (UnkType_, m_Unk).
#include "ltlink.h"
struct UnkType_DeviceNode
{
	LTLink		m_Link;				// 0x00 list link (m_pData = this)
	uint32		m_Unk0c;			// 0x0c DDCAPS::dwCaps & DDCAPS_3D of the device (set by the callback)
	GUID		m_Guid;				// 0x10 copy of the device's GUID (when it has one)
	GUID		*m_pGuid;			// 0x20 &m_Guid, or 0 for the primary device
	char		m_Unk24[100];		// 0x24 the driver name the enumeration passed (copied into RMode::m_InternalName)
	char		m_Unk88[100];		// 0x88 the description the enumeration passed (copied into RMode::m_Description)
};
// GLOBAL: D3DREN 0x10057800
extern LTLink g_EnumeratedDeviceList;			// the list head of the enumerated devices (nodes are UnkType_DeviceNode)

// ---- the StateChange applier (unit unk/10021d70) ---------------------------------------------------------------------------------
// Object that applies a Talon SDK D3DStateChange.h `StateChange` (vector<RenderState> at +0, vector<TextureState> at +0xc) to the
// device and remembers the old values so that RestoreAllStates can put them back (SetRenderState/SetTextureStageState through the
// device at 0x1005de30).  The class name is invented (UnkType_ prefix); the SDK types are real.  The two vectors are 3 dwords each
// (STLport layout).  The STL stays out of this header (it is included by every unit): unit unk/10021d70 defines
// D3DREN_STATERESTORER_FULL before including D3DStateChange.h to get the real members; the layout is identical.
struct StateChange;
struct RenderState;
struct TextureState;
#ifdef D3DREN_STATERESTORER_FULL
#include "D3DStateChange.h"
#endif
class UnkType_StateRestorer
{
public:
	// Inline members in the original: d3d_DrawSkyObjects (0x10019691) and the static initialisers / atexit functions of the saver globals have
	// the constructor (two empty vectors) and the destructor (restore, then free both vectors) expanded; 0x10021d86 and 0x100198bc are the
	// compiler's out-of-line COMDAT copies (emitted in the unk/10021d70 and d3d_drawsky objects).
	UnkType_StateRestorer()		{}
	~UnkType_StateRestorer()	{ RestoreAllStates(); }
	void RestoreAllStates();								// restore every saved render/texture stage state, then forget them
	void ApplyStateChange(StateChange *pChange, uint32 nStage);	// apply pChange (saving the old values); nStage = texture stage
	// Added by package W6 (d3d_DrawSkyObjects, 0x10019691, calls them on a local saver): apply one state, remembering the old value.
	void ApplyRenderState(const RenderState &state);				// 0x10021e28: SetRenderState(pState->m_RenderStateType, pState->m_RenderState) after saving the old one (the caller passes a temporary: const reference)
	void ApplyTextureStageState(const TextureState &state, uint32 nStage);	// 0x10021dfd: SetTextureStageState(nStage, pState->m_TextureStateType, pState->m_TextureState) after saving the old one
	// Added by the W3 agent (helpers inside unit unk/10021d70, member functions of the same object):
	void SaveRenderState(const RenderState &state);				// 0x10021e47: GetRenderState + remember (type, old value)
	void SaveTextureStageState(const TextureState &state, uint32 nStage);	// 0x10021e80: GetTextureStageState + remember (stage, type, old value)
	void RestoreRenderStates();									// 0x10021ec9: restore the saved render states, then forget them
	void RestoreTextureStageStates();									// 0x10021f09: restore the saved texture stage states, then forget them

#ifdef D3DREN_STATERESTORER_FULL
	std::vector<RenderState>	m_Unk00;				// 0x00 saved render states
	std::vector<TextureState>	m_Unk0c;				// 0x0c saved texture stage states
#else
	uint8						m_Pad00[0x18];
#endif
};
// GLOBAL: D3DREN 0x10063c90
extern UnkType_StateRestorer g_TextureStateRestorer;

// ---- functions of sys/d3d/d3d_init that other units call -----------------------------------------------------------------------
// FUN_10019b10: DDERR_* / D3DERR_* code to descriptive text.
// NAME: D3DAppErrorToString: from the DirectX d3dapp sample's error-text function of that name (name medium, from memory of the
// sample: a switch over DDERR_/D3DERR_ codes returning text); not verified against a file on disk.
char *D3DAppErrorToString(HRESULT hr);							// 0x10019b10
// NAME: CanDrawPortals: Jupiter CD3D_Device::CanDrawPortals() (returns the stencil capability); d3d.ren has it as a free function.
int CanDrawPortals();											// 0x1001a850 (returns g_ChosenZBufferStencilBitDepth != 0)
void d3d_ReadExtraConsoleVariables();							// 0x1001a400 (declared in common_stuff.h as well)

#endif
