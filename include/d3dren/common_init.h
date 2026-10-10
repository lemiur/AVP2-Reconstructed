// d3d.ren sys/d3d/common_init (0x1001094c-0x1001116b): the globals of the device list / mode setup object that other units read.
// Owner: unit sys/d3d/common_init (src/d3dren/sys/d3d/common_init.cpp defines them).
#ifndef __D3DREN_COMMON_INIT_H__
#define __D3DREN_COMMON_INIT_H__

#include "pixelformat.h"

// The screen pixel format (filled by the device bring-up; its inline constructor stores the PFormat vtable: _$E5 0x1001095c).
// GLOBAL: D3DREN 0x100577c8
extern PFormat g_ScreenPixelFormat;
// The four ints below are defined by sys/d3d/common_stuff: their addresses lie inside that object's .bss block (the ConVar block
// 0x1005782c..0x100584f8); declared here because their readers (common_init, d3d_draw, d3d_surface) include this header.
// GLOBAL: D3DREN 0x10057e24
extern int g_nWindowBlitScaleX;				// guess: horizontal divisor of the RenderStruct width / stretch of the window blit (1)
// GLOBAL: D3DREN 0x10057e28
extern int g_nWindowBlitScaleY;				// guess: vertical divisor (1)
// GLOBAL: D3DREN 0x10057e30
extern int g_nSysMemParameter;				// guess: the "SysMem" console parameter as an int
// GLOBAL: D3DREN 0x100584e4
extern int g_bWarbleTableInitialized;		// guess: the model warble tables were built (d3d_RenderScene)

#endif
