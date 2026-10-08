// d3d.ren unit unk/100132a0 (package W2): prototypes of the functions other units call.  Jupiter's counterpart is d3d_draw.h.
// Names with provenance carry a NAME: line; everything else keeps its Ghidra name.  Bodies: src/d3dren/unk/100132a0.cpp.
// Rule: the prototypes here must be identical to the ones other units already declare locally (decorated names must agree).
#ifndef __D3DREN_D3D_DRAW_H__
#define __D3DREN_D3D_DRAW_H__

#include "ltbasedefs.h"
#include "de_objects.h"
#include "d3dren/d3ddevice.h"

// The prototypes below are the ones the other units already declare locally (decorated names must agree); the definitions are in
// src/d3dren/unk/100132a0.cpp.  Types of the arguments are guesses where the exe has no source evidence (pointers to world polys etc.).
struct WorldPoly;
struct UnkType_PoolNode;
struct LightAnim;
class LTRect;

uint32 d3d_PackSqrtRGB(uint8 r, uint8 g, uint8 b);		// 0x10013990: packs three bytes through the table at 0x10082068 into a D3DCOLOR
uint32 d3d_PackRGB(uint8 r, uint8 g, uint8 b);		// 0x100139d0
void d3d_SetModulateAlphaTextureStates();									// 0x100139f0: stage 0 texture blend (modulate), or the TEXTUREMAPBLEND render state
// NAME: d3d_SetTranslucentObjectStates / d3d_UnsetTranslucentObjectStates: Jupiter d3d_draw.cpp (names_proposal high / medium).
void d3d_SetTranslucentObjectStates(int bAdditive);	// 0x10013ba0
void d3d_UnsetTranslucentObjectStates(int bChangeZ);	// 0x10013df0 (the argument is not used by this version)
void d3d_BeginChromaKeyPolyPass();									// 0x10013e00: begin alpha test pass (AlphaTest console variable)
void d3d_EndChromaKeyPolyPass();									// 0x10013e40: end alpha test pass
int d3d_GrowTLVertexBuffer(int nVertices);						// 0x10013e80: grows the 0x20-byte TL vertex scratch array
void d3d_QueueWorldPoly(WorldPoly *pPoly);					// 0x10013ef0: pushes pPoly on the flat poly list g_pFlatWorldPolyQueue
void d3d_FreeWorldPolyQueue(UnkType_PoolNode *pList);			// 0x100142b0: returns every node of a deferred list to the pool
int d3d_ClipTLVertexLine(float *pVerts, int nMask);			// 0x100161e0 (not written yet): clips a 2-vertex line against the plane mask
void d3d_InitPolyDrawPools();									// 0x100184f0: initialise the poly draw pools
void d3d_TermPolyDrawPools();									// 0x10018550: tear them down
int d3d_AddLightAnimVertexColors(void *pPolyData, uint32 nPolyData, LightAnim *pAnim, uint32 *pRef);	// 0x100185a0 (the exe returns 0 / 1)

// NAME: d3d_GetBlendStates: Jupiter d3d_draw.h (the d3d.ren body is the same; Talon adds `m_ObjectType != OT_MODEL` to the fog disable test:
// FLAG_FOGDISABLE shares its bit with FLAG_ANIMTRANSITION of models).  An inline function of the header: the exe's out-of-line copy is
// 0x10024f8c (emitted in the drawmodel object, the first one that did not expand it); the A objects expand it, the P objects call it.
inline void d3d_GetBlendStates(LTObject *pObject, uint32 &srcBlend, uint32 &destBlend, uint32 &dwFog, uint32 &dwFogColor)
{
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGCOLOR, (unsigned long *)&dwFogColor);

	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, (unsigned long *)&dwFog);
	if ((pObject->m_Flags & FLAG_FOGDISABLE) && pObject->m_ObjectType != OT_MODEL)
	{
		dwFog = 0;
	}

	if (pObject->m_Flags2 & FLAG2_ADDITIVE)
	{
		srcBlend	= D3DBLEND_ONE;
		destBlend	= D3DBLEND_ONE;
		dwFogColor	= 0;
	}
	else if (pObject->m_Flags2 & FLAG2_MULTIPLY)
	{
		srcBlend	= D3DBLEND_ZERO;
		destBlend	= D3DBLEND_SRCCOLOR;
		dwFogColor	= 0xFFFFFFFF;
	}
	else
	{
		srcBlend	= D3DBLEND_SRCALPHA;
		destBlend	= D3DBLEND_INVSRCALPHA;
	}
}

#endif
