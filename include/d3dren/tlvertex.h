// d3d.ren pre-transformed ("TL") vertex types and the polygon-clip helpers that work on them.
// Seed B (clip/interpolation math, 0x10001940-0x10002000).
#ifndef __D3DREN_TLVERTEX_H__
#define __D3DREN_TLVERTEX_H__

#include "ltbasedefs.h"

// NAME: Jupiter runtime/render_a/src/sys/d3d/3d_ops.h TLRGB: byte order b,g,r,a (TLVertex_ClipExtra interpolates the
// bytes in the order +0x12 (r), +0x11 (g), +0x10 (b), +0x13 (a), which is a TLRGB's r, g, b, a).
struct TLRGB
{
	uint8	b;
	uint8	g;
	uint8	r;
	uint8	a;
};
// NAME: RGBColor: Jupiter render_a/src/sys/d3d/3d_ops.h `struct RGBColor { union { TLRGB rgb; uint32 color; }; }`.
struct RGBColor
{
	union
	{
		TLRGB	rgb;
		uint32	color;
	};
};


// A 0x20-byte pre-transformed vertex, as the renderer stores it before DrawPrimitive.
// NAME: TLVertex: Jupiter 3d_ops.h class TLVertex (m_Vec, rhw, color, tu, tv in the same order); that D3D9 descendant dropped
// the specular colour, so its size is 0x1c.  The 0x20 layout is also D3DTLVERTEX (DX7 d3dtypes.h, FVF XYZRHW|DIFFUSE|
// SPECULAR|TEX1): `specular` is named after it.  TLVertex_ClipExtra reads/writes exactly these members.
struct TLVertex
{
	LTVector	m_Vec;				// 0x00 sx, sy, sz (screen or camera space)
	float		rhw;				// 0x0c
	union
	{
		TLRGB	rgb;				// 0x10
		uint32	color;
	};
	union
	{
		TLRGB	specular_rgb;		// 0x14 (only .a is interpolated)
		uint32	specular;
	};
	float		tu, tv;				// 0x18, 0x1c
};

// A 0x28-byte vertex with a second texture coordinate pair (FVF ...|TEX2); the renderer selects this layout (vertex
// size at CRenderer+0x5f8 == 0x28) when the second texture stage is used.  Unknown role name: UnkType.
struct UnkType_TLVertex40
{
	LTVector	m_Vec;				// 0x00
	float		rhw;				// 0x0c
	union
	{
		TLRGB	rgb;				// 0x10
		uint32	color;
	};
	union
	{
		TLRGB	specular_rgb;		// 0x14
		uint32	specular;
	};
	float		tu, tv;				// 0x18, 0x1c
	float		tu2, tv2;			// 0x20, 0x24
};

#endif
