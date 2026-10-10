#ifndef __D3DREN_CLIPINSIDE_H__
#define __D3DREN_CLIPINSIDE_H__

// The inside[] arrays of the near and left plane clippers (Jupiter polyclip.h bInside[], a function-local static of the original
// expansion, one copy shared by every expansion of the same plane and vertex type).  Shared by d3dren/clippoly.h (the inline
// expansions of unit unk/10001000) and d3dren/polydraw.h (the out-of-line copies of units unk/10007930 and unk/100098d0, which define
// them).  The header name is invented.
// GLOBAL: D3DREN 0x10094de0
extern int g_ClipNearInsideFlagsTLVertex[56];	// near plane, 0x20-byte vertices
// GLOBAL: D3DREN 0x10094ec0
extern int g_ClipLeftInsideFlagsTLVertex[56];	// left plane, 0x20-byte vertices
// GLOBAL: D3DREN 0x10094c20
extern int g_ClipNearInsideFlagsVertex40[56];	// near plane, 0x28-byte vertices
// GLOBAL: D3DREN 0x10094d00
extern int g_ClipLeftInsideFlagsVertex40[56];	// left plane, 0x28-byte vertices

#endif
