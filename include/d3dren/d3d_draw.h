// d3d.ren unit unk/100132a0 (package W2): prototypes of the functions other units call.  Jupiter's counterpart is d3d_draw.h.
// Names with provenance carry a NAME: line; everything else keeps its Ghidra name.  Bodies: src/d3dren/unk/100132a0.cpp.
// Rule: the prototypes here must be identical to the ones other units already declare locally (decorated names must agree).
#ifndef __D3DREN_D3D_DRAW_H__
#define __D3DREN_D3D_DRAW_H__

#include "ltbasedefs.h"
#include "de_objects.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/rendererconsolevars.h"
#include "../../../build/proj/LT2/lithshared/stdlith/struct_bank.h"

// The prototypes below are the ones the other units already declare locally (decorated names must agree); the definitions are in
// src/d3dren/unk/100132a0.cpp.  Types of the arguments are guesses where the exe has no source evidence (pointers to world polys etc.).
struct WorldPoly;
struct UnkType_PoolNode;
struct UnkType_PoolBucket;
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

// ---- globals of this unit (defined in src/d3dren/sys/d3d/d3d_draw.cpp; every address is in its .bss block) -------------------------
// Fog: the scales RenderScene sets up each frame for the per-vertex fog hooks.
// GLOBAL: D3DREN 0x10057990
extern float g_fFogAlphaScale;		// guess: 255 / (FogFarZ - FogNearZ); defined by sys/d3d/common_stuff (its address is in that .bss block)
// GLOBAL: D3DREN 0x10058620
extern float g_fSkyFogAlphaScale;		// guess: 255 / (SkyFogFarZ - SkyFogNearZ)
// GLOBAL: D3DREN 0x100584f8
extern float g_fVFogValueRange;		// guess: VFogMaxYVal - VFogMinYVal (set by RenderScene when VFog is on)
// GLOBAL: D3DREN 0x10058778
extern float g_fInvVFogHeightRange;		// guess: 1 / (VFogMaxY - VFogMinY)

// GLOBAL: D3DREN 0x10058c68
extern UnkType_PoolNode *g_pFlatWorldPolyQueue;	// guess: head of a list of polys (nodes of the pool 0x10058758)
// GLOBAL: D3DREN 0x10058c90
extern UnkType_PoolBucket *g_pMultipassWorldPolyBuckets;	// guess: list of the buckets (head of a list whose nodes come from the pool at 0x10058c98; polys queued per lightmap page)

// Draw mode of the world polys (d3d_SetWorldPolyDrawMode / d3d_FullDrawScene).
// GLOBAL: D3DREN 0x1005c7e0
extern int g_bOnePassLightmappingEnabled;	// guess: one-pass lightmapping enabled (set from the g_Force1Pass console variable unless the device cannot do it)
// GLOBAL: D3DREN 0x10058730
extern int g_bPolyDrawModeOne;	// guess: member of the object at 0x1005872c (the fog alpha hook g_pfnCalcFogAlpha is its first member)
// GLOBAL: D3DREN 0x10058734
extern int g_bPolyDrawSetupComplete;	// guess: member of the object at 0x1005872c
// GLOBAL: D3DREN 0x10058d00
extern int g_bDrawGouraudFullbritePass;	// guess: draw state flag (Gouraud fullbrites in use)

// The poly draw pools (d3d_InitPolyDrawPools / d3d_TermPolyDrawPools).
// GLOBAL: D3DREN 0x10058648
extern StructBank g_PolyDrawBlockBank;		// guess: the 0x100-byte-element pool
// GLOBAL: D3DREN 0x10058800
extern uint32 g_PolyDrawPoolResetValue;

// The vertex colour tables RenderScene rebuilds when the global light scale or vertex tint changes.
// GLOBAL: D3DREN 0x1005a330
extern int g_nLastColorTableVertexTint;
// GLOBAL: D3DREN 0x10059d04
extern uint8 g_MultipassVertexTintTableR[256];	// guess: red lighting table (multipass / dynamic light pass); the next two are green and blue
// GLOBAL: D3DREN 0x10059e04
extern uint8 g_MultipassVertexTintTableG[256];
// GLOBAL: D3DREN 0x10059f04
extern uint8 g_MultipassVertexTintTableB[256];
// GLOBAL: D3DREN 0x1005a004
extern uint8 g_VertexTintTableR[256];	// guess: red lighting table
// GLOBAL: D3DREN 0x1005a104
extern uint8 g_VertexTintTableG[256];
// GLOBAL: D3DREN 0x1005a204
extern uint8 g_VertexTintTableB[256];

// The vertical fog zone and density of a height y (VFogMinY/VFogMaxY band, VFogMinYVal/VFogMaxYVal below/above it, linear in between).
// Inline helpers: ViewParams::SetupFogViewPosition (3d_ops) evaluates both for the viewer, d3d_CalcVerticalFogAlpha for the vertex and
// the viewer (the three expansions are the exe's; the scale factors multiply in this order).
inline int d3d_GetVFogZone(float y)
{
	if (y >= g_CV_VFogMaxY.m_FloatVal)
		return 1;
	else if (y <= g_CV_VFogMinY.m_FloatVal)
		return 0;
	else
		return 2;
}

inline float d3d_GetVFogDensity(float y)
{
	if (y <= g_CV_VFogMinY.m_FloatVal)
		return g_CV_VFogMinYVal.m_FloatVal;
	else if (y >= g_CV_VFogMaxY.m_FloatVal)
		return g_CV_VFogMaxYVal.m_FloatVal;
	else
		return (y - g_CV_VFogMinY.m_FloatVal) * g_fVFogValueRange * g_fInvVFogHeightRange + g_CV_VFogMinYVal.m_FloatVal;
}

#endif
