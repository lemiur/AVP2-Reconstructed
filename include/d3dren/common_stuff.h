// d3d.ren common helpers: the Talon-era form of Jupiter's render_a/src/sys/d3d/common_stuff.h (same function names).
// Used by every unit of the renderer.  Owner: unit sys/d3d/common_stuff (package W5).
//
// NAME: g_pStruct, dalloc, dalloc_z, dfree, AddDebugMessage, d3d_MaybeCreateCVar, d3d_CreateConsoleVariables,
// d3d_ReadConsoleVariables, d3d_ReadExtraConsoleVariables: Jupiter's descendant renderer
// (jupiter/runtime/render_a/src/sys/d3d/common_stuff.cpp/.h, d3d_init.cpp); the d3d.ren bodies have the same shape.
//
// The console-variable class (ConVar, 0x100111b5) and every g_CV_* variable are in "d3dren/rendererconsolevars.h".
// The profiling counter (Counter::Counter 0x10013236, cnt_StartCounter 0x10013254, cnt_EndCounter 0x1001325f, CountAdder)
// is the engine's own counter.cpp copy: use the engine header (#include "counter.h"), it declares all of them with the
// same names and CountAdder's inlines expand to the calls the renderer makes.
#ifndef __D3DREN_COMMON_STUFF_H__
#define __D3DREN_COMMON_STUFF_H__

#include <stddef.h>
#include <windows.h>
#include "renderstruct.h"

// Both renderers use this for the render contexts (Jupiter common_stuff.h RenderContext, whose only member is
// m_CurFrameCode; the Talon object holds the world's render data and has the frame code at +0xc).
struct LightmapPage;		// d3dren/lightmap.h
class MainWorld;
struct RenderContext
{
	LightmapPage	*m_pLightmapPages;			// 0x00 guess: first lightmap page of the list (0 = none) (W9, lightmap.h)
	uint32			m_nLightmapPages;			// 0x04 guess: number of lightmap pages
	MainWorld		*m_pWorld;			// 0x08 guess: the world (RenderContextInit::m_pWorld)
	uint16			m_CurFrameCode;		// 0x0c
	uint8			m_Pad0E[0x10 - 0x0e];	// the object is 0x10 bytes (d3d_CreateContext 0x1001b700 allocates 0x10)
};

// The RenderStruct the engine passes to the renderer (its Alloc/Free/GetParameter/ConsolePrint hooks).
// GLOBAL: D3DREN 0x10058470
extern RenderStruct *g_pStruct;

// Allocation through the engine (RenderStruct::Alloc / ::Free, else the CRT).
void *dalloc(size_t size);		// 0x10012ce5: g_pStruct->Alloc, else malloc
void *dalloc_z(size_t size);	// 0x10012cfe: dalloc + zero fill; "d3drender.dll: out of memory" on failure
void dfree(void *ptr);			// 0x10012d44: g_pStruct->Free, else free

// Prints through g_pStruct->ConsolePrint (RenderStruct +0x14) after _vsnprintf into a 256 byte buffer.
// NAME: dsi_ConsolePrint (medium): the engine's name of this function (include/dsys_interface.h, same declaration);
// the renderer's own copy that forwards to RenderStruct::ConsolePrint.  Ghidra still calls it FUN_10012d5d.
void dsi_ConsolePrint(const char *pMsg, ...);		// 0x10012d5d

// Prints only if debugLevel <= the RenderDebug console variable.
void AddDebugMessage(int debugLevel, const char *pMsg, ...);		// 0x10012d92

// NAME: names_proposal.csv guess_CountSetBits (low, invented): FUN_10012edf, the number of set bits of a 32 bit mask (declared here by W8, defined in common_stuff.cpp).
int FUN_10012edf(uint32 mask);											// 0x10012edf

HLTPARAM d3d_MaybeCreateCVar(const char *pName, float defaultVal);	// 0x10012dd2
void d3d_CreateConsoleVariables();									// 0x10012e26
void d3d_ReadConsoleVariables();									// 0x10012e4c (RenderStruct::ReadConsoleVariables)
void d3d_ReadExtraConsoleVariables();								// 0x1001a400 (d3d_init, not in this unit)

// Window/mode globals (Jupiter common_stuff.h names; Ghidra names already; set by d3d_Init from the "windowed" parameter and
// RenderStructInit::m_Mode).  Declared here by the W3 agent (d3d_init and dirtyrect read them); the definitions belong to common_stuff.
// GLOBAL: D3DREN 0x10057aa0
extern int g_bRunWindowed;
// GLOBAL: D3DREN 0x10057e34
extern uint32 g_ScreenWidth;
// GLOBAL: D3DREN 0x10057f28
extern uint32 g_ScreenHeight;
// GLOBAL: D3DREN 0x10057998
extern HWND g_hWnd;			// the engine's main window (RenderStructInit::m_hWnd, stored by d3d_Init; Jupiter common_stuff.h name)

#endif
