// d3d.ren sys/d3d/common_init (0x1001094c-0x1001116b): the globals of the device list / mode setup object that other units read.
// Owner: unit sys/d3d/common_init (src/d3dren/sys/d3d/common_init.cpp defines them).
#ifndef __D3DREN_COMMON_INIT_H__
#define __D3DREN_COMMON_INIT_H__

#include "pixelformat.h"

// The screen pixel format (filled by the device bring-up; its inline constructor stores the PFormat vtable: _$E5 0x1001095c).
// GLOBAL: D3DREN 0x100577c8
extern PFormat g_ScreenPixelFormat;

#endif
