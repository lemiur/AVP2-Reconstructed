// d3d.ren sys/d3d/common_init (0x1001094c-0x1001116b): the globals of the device list / mode setup object that other units read.
// Owner: unit sys/d3d/common_init (src/d3dren/sys/d3d/common_init.cpp defines them).
#ifndef __D3DREN_COMMON_INIT_H__
#define __D3DREN_COMMON_INIT_H__

#include "pixelformat.h"

// The screen pixel format (filled by the device bring-up; its inline constructor stores the PFormat vtable: _$E5 0x1001095c).
// GLOBAL: D3DREN 0x100577c8
extern PFormat g_ScreenPixelFormat;
// GLOBAL: D3DREN 0x10057e24
extern int g_nWindowBlitScaleX;				// guess: horizontal divisor of the RenderStruct width / stretch of the window blit (1)
// GLOBAL: D3DREN 0x10057e28
extern int g_nWindowBlitScaleY;				// guess: vertical divisor (1)
// TODO(identity): no unit defines this flag yet.  Its .bss neighbours 0x100584e8/ec/f0 (g_bFogStateInitialized,
// g_bDitherStateInitialized, g_bTextureFilterStateInitialized) are defined in sys/d3d/d3d_init and, like it, cleared by d3d_Init
// (common_init), so its owner is probably sys/d3d/d3d_init; declared here because both readers (common_init, d3d_draw) include this header.
// GLOBAL: D3DREN 0x100584e4
extern int g_bWarbleTableInitialized;		// guess: the model warble tables were built (d3d_RenderScene)

#endif
