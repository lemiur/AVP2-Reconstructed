// d3d.ren world polygon drawing (unit unk/100098d0, work package W4): lightmapped / detail-texture polygon batches, the queue of
// polygons waiting for their texture, the vertex scratch array and the clip/project dispatchers for 0x20- and 0x28-byte
// pre-transformed vertices.  Everything here is the renderer's own; the engine types come from de_objects.h / de_world.h.
//
// NAME: sb_Free: LT_StdLith struct_bank.h `inline void sb_Free(StructBank *pBank, void *pObj)` (pool.h includes it; the pools at
// 0x10058758 / 0x10058c98 are StructBanks); the exe inlines it with both of its null checks.  The exe's out-of-line copy of
// sb_Allocate is 0x10007e36 (emitted in the unk/10007930 object).
// NAME: StateSet, StageStateSet: Jupiter d3d_draw.h (d3dstate.h).
// Everything else keeps its Ghidra name; roles are in the comments (guess: ...).
#ifndef __D3DREN_POLYDRAW_H__
#define __D3DREN_POLYDRAW_H__

#include "ltbasedefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "d3dren/d3dstate.h"
#include "d3dren/tlvertex.h"
#include "d3dren/pool.h"
#include "d3dren/viewparams.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/lightmap.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/d3d_draw.h"	// the vertex tint tables and draw-mode globals (owner: sys/d3d/d3d_draw)

// ---- globals read by the world polygon code (the frame's world, statistics and vertex colours: common_draw.h) -------------
// GLOBAL: D3DREN 0x1005872c
extern void (__fastcall *g_pfnCalcFogAlpha)(LTVector *pPos, uint32 *pSpecular);	// guess: per-vertex fog alpha hook (the vertex position and its specular colour)
// GLOBAL: D3DREN 0x10058c40
extern void (__fastcall *g_pfnCalcSkyFogAlpha)(LTVector *pPos, uint32 *pSpecular);	// guess: the sky's per-vertex fog hook (same signature; added by W6: d3d_drawsky and drawpolymgr use it)
// guess: sphere map (environment) texture coordinates of a point seen from pViewPos on a surface with normal pNormal: *pU, *pV.
void d3d_CalcWorldReflectionUVs(LTVector *pViewPos, LTVector *pPos, LTVector *pNormal, float *pU, float *pV);
// GLOBAL: D3DREN 0x10058040
extern uint8 g_u8FogColor[3];				// guess: the clamped fog colour bytes [0] r, [1] g, [2] b (d3d_PackSqrtRGB / d3d_PackRGB pack them); defined by sys/d3d/common_stuff (its .bss block)

// Per-stage texture coordinate scale pair (u, v), indexed by the device stage: [0] = 0x10061810/14 scales the lightmap
// coordinates, [1] = 0x10061818/1c the detail texture coordinates.  Same type and GLOBAL as unit unk/10007930 (W2) declares
// it in its .cpp; the GLOBAL annotation stays there.
struct UnkType_StageUV
{
	float	m_Unk00;
	float	m_Unk04;
};
extern UnkType_StageUV g_TextureStageTexelSizes[8];

// Detail texture state shared by d3d_DrawWorldTextureBucket / d3d_DrawDualTextureWorldBucket: the scale and the cos / sin of the angle of the current detail texture.
// GLOBAL: D3DREN 0x100514a8
extern float g_WorldDetailTextureScale;				// guess: detail texture scale (DetailTextureScale * the texture's own scale)
// GLOBAL: D3DREN 0x100518d0
extern float g_WorldDetailTextureAngleCos;				// guess: cos of the detail texture angle
// GLOBAL: D3DREN 0x100513e0
extern float g_WorldDetailTextureAngleSin;				// guess: sin of the detail texture angle

// The vertex scratch array: g_nQueuedWorldPolyVertices vertices (0x20 bytes each) are in use, room for g_nQueuedWorldPolyVertexCapacity.
// GLOBAL: D3DREN 0x100587e4
extern uint32 g_nQueuedWorldPolyVertices;
// GLOBAL: D3DREN 0x100587fc
extern TLVertex *g_pQueuedWorldPolyVertices;
// GLOBAL: D3DREN 0x1005a368
extern uint32 g_nQueuedWorldPolyVertexCapacity;

// guess: grows the scratch array to nVertices elements (keeps the old ones); returns 0 when the allocation failed.
int d3d_GrowTLVertexBuffer(int nVertices);
// guess: packs three colour bytes (each through the gamma table at 0x10082068) into an RGB value; d3d_PackRGB packs them as given.
uint32 d3d_PackSqrtRGB(uint8 r, uint8 g, uint8 b);
uint32 d3d_PackRGB(uint8 r, uint8 g, uint8 b);

// guess: returns the "fullbrite" flag of the SharedTexture's renderer texture (loading it when missing); the second argument is 0 here.
int d3d_EnsureTextureAndGetFlags(SharedTexture *pTexture, uint32 nStageFlags);
// guess: gives every node of the list (linked through m_Unk10) back to the node pool.
void d3d_FreeWorldPolyQueue(UnkType_PoolNode *pList);

// guess: draws the poly as a flat polygon later (DrawFlat console variable path): queues it on the deferred list.
void d3d_QueueWorldPoly(WorldPoly *pPoly);
// guess: uploads / refreshes the lightmap of the poly into its page (bFirst: the page record was just set up); returns 0 when the poly has none.
int d3d_RefreshWorldPolyLightmap(WorldPoly *pPoly, int bFirst);

// ---- the lightmap page of a world polygon (WorldPoly +0x48, LightmapPage of d3dren/lightmap.h) and the queued polys ------------
// Members of the lightmap page that lightmap.h (W9) leaves untyped (m_Unk04 / m_Unk08 / m_Unk20), seen by the world poly queue:
//   +0x04 the next page in the list of pages that have polys waiting (g_pQueuedLightmapPageHead)
//   +0x08 the polys waiting for this page (nodes of the pool at 0x10058758, linked through m_Unk10)
//   +0x20 non-zero once the page has been set up by the first draw
#define LMPAGE_NEXT(p)		(*(LightmapPage **)&(p)->m_Unk04)
#define LMPAGE_QUEUE(p)		(*(UnkType_PoolNode **)&(p)->m_Unk08)
// WorldPoly +0x30: non-zero when the poly has a lightmap to draw with (engine pad m_Pad30).
#define WORLDPOLY_UNK30(p)	(*(void **)((uint8 *)(p) + 0x30))

// The pools the queued polys and their buckets come from (StructBanks; +0x18 of the first is the free list at 0x10058770).
// GLOBAL: D3DREN 0x10058758
extern UnkType_Pool g_WorldPolyNodeBank;
// GLOBAL: D3DREN 0x10058c98
extern UnkType_Pool g_WorldPolyBucketBank;
// GLOBAL: D3DREN 0x1005a308
extern UnkType_PoolBucket *g_pTexturedWorldPolyBuckets;	// guess: list of buckets (polys queued per texture, linked through m_Unk08)
// GLOBAL: D3DREN 0x100528d4
extern LightmapPage *g_pQueuedLightmapPageHead;	// guess: lightmap pages with polys waiting to be drawn (linked through LMPAGE_NEXT)

// A vertex of a world polygon (SPolyVertex of de_objects.h, same layout) with the members the engine header leaves in padding
// named at their offsets: 0x0c/0x10 are the lightmap texture coordinates.  (lightmap.h's SPOLYVERTEX_UNK0C macros compile to
// x87 copies, a member of a struct to the integer copies the exe has.)
struct UnkType_PolyVertex
{
	LTVector	*m_Vec;				// 0x00
	float		m_U, m_V;			// 0x04
	float		m_Unk0c, m_Unk10;	// 0x0c guess: second (lightmap) texture coordinates
	uint8		m_Color[4];			// 0x14 r, g, b, a
};

// ---- the 0x20-byte vertex clip helpers (unit unk/10001000 and this one) --------------------------------------------------------
// The inside[] arrays of the polyclip.h expansions are function-local statics of the original that the near/left plane code
// here shares with ClipModelPolygon32.
// GLOBAL: D3DREN 0x10094de0
extern int g_ClipNearInsideFlagsTLVertex[56];	// guess: inside flags of the near plane clip
// GLOBAL: D3DREN 0x10094ec0
extern int g_ClipLeftInsideFlagsTLVertex[56];	// guess: inside flags of the left plane clip
void TLVertex_ClipExtra(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t);
// Plane clippers for the flag bits 8, 0x10, 0x20, 2 (unit unk/10001000); the first argument is unused.
int ClipPolyTop(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyRight(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyBottom(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyFar(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);

// ---- the functions of this unit (0x100099b9-0x1000b20c) --------------------------------------------------------------------------
void DrawLightmappedWorldPoly(WorldPoly *pPoly);
void RelightWorldPolyIfNeeded(MainWorld *pWorld, WorldPoly *pPoly);
void d3d_BuildDualTextureWorldVertices(uint32 nFlags, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts);
void d3d_BuildLightmappedWorldVertices(uint32 nFlags, WorldPoly *pPoly, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts);
void d3d_UpdateWorldVertexLightmapUVs(WorldPoly *pPoly, UnkType_TLVertex40 *pDest, UnkType_PolyVertex *pSrc, int nVerts);
TLVertex *d3d_ReservePolyScratchVertices(int nVerts);
void d3d_DrawOrQueueLightmappedWorldPoly(WorldPoly *pPoly);
void d3d_FlushWorldTextureBuckets(void);
void d3d_DrawWorldTextureBucket(UnkType_PoolBucket *pBucket, SharedTexture *pTexture, int a3, int a4);
void d3d_DrawSingleTextureWorldBucket(UnkType_PoolBucket *pBucket, int a2, int a3);
void d3d_DrawDualTextureWorldBucket(UnkType_PoolBucket *pBucket, int a2, int a3);
void d3d_FlushPendingWorldTextureBuckets(void);
void d3d_FlushLightmapPageQueues(void);
void d3d_DrawClippedTriangleFan(TLVertex *pVerts, int nVerts, ViewParams *pParams, uint32 nFVF);
void d3d_DrawClippedDualTextureTriangleFan(UnkType_TLVertex40 *pVerts, int nVerts, ViewParams *pParams, uint32 nFVF);
int d3d_ClipAndProjectTLVertices(TLVertex **ppVerts, int *pnVerts, ViewParams *pParams, int param_4);
int ClipPoly(uint32 nFlags, TLVertex **ppVerts, int *pnVerts);
int ClipPolyNear(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);
int ClipPolyLeft(char *pUnused, TLVertex **ppVerts, int *pnVerts, TLVertex **ppOut);

// ---- per-frame state and tables of the world polygon draw code (definitions: the owning units, not yet reconstructed) ----------
// GLOBAL: D3DREN 0x1005ce18
extern int g_bPortalsEnabled;				// guess: world models are drawn through the sorted portal path
// The world texture coordinate offsets/scales the world poly draw state sets from the current global pan texture
// (g_pGlobalPanInfo, common_draw.h) in SetupWorldTextureCoordinates.  The vertex tint tables are in d3d_draw.h.
// GLOBAL: D3DREN 0x1004ffb0
extern float g_fGlobalPanUOffset;
// GLOBAL: D3DREN 0x1004ffb4
extern float g_fGlobalPanVOffset;
// GLOBAL: D3DREN 0x1004eba8
extern float g_fGlobalPanUScale;
// GLOBAL: D3DREN 0x1004ebac
extern float g_fGlobalPanVScale;
// GLOBAL: D3DREN 0x1007d424
extern int g_bLightmapModulate2X;			// guess: lightmap stage colour op is MODULATE2X (lightmap draw state)

// The world poly draw callbacks of the frame (d3d_draw selects them per draw mode; the poly loops call them).
typedef void (*PFN_DrawWorldPoly)(WorldPoly *pPoly);
// GLOBAL: D3DREN 0x10058cd8
extern PFN_DrawWorldPoly g_pfnDrawUntexturedWorldPoly;
// GLOBAL: D3DREN 0x100587e8
extern PFN_DrawWorldPoly g_pfnDrawTexturedWorldPoly;
// GLOBAL: D3DREN 0x1005a304
extern PFN_DrawWorldPoly g_pfnDrawPanningSkyWorldPoly;
// GLOBAL: D3DREN 0x10058c24
extern PFN_DrawWorldPoly g_pfnDrawLightmappedWorldPoly;

// ---- frame state of the dynamic light drawing (sys/d3d/drawlight; definitions in sys/d3d/common_draw) ----
struct UnkType_LitPoly;
// GLOBAL: D3DREN 0x10056220
extern StructBank g_PolyLightBank;		// guess: UnkType_PolyLight records
// GLOBAL: D3DREN 0x10056240
extern StructBank g_LitPolyBank;		// guess: UnkType_LitPoly records
// GLOBAL: D3DREN 0x100577b4
extern UnkType_LitPoly *g_pDynamicallyLitPolys;	// guess: head of the list of polys touched by a dynamic light this frame

// ---- device state read by the drawing units (set by unit sys/d3d/d3d_init; d3ddevice.h would be their home, but any new
// declaration there flips the register-fragile shadow clippers of sys/d3d/drawmodelshadows) ----
// GLOBAL: D3DREN 0x1005c9a0
extern uint32 g_DefaultZEnableState;		// guess: the device's normal D3DRENDERSTATE_ZENABLE value (the no-z sprites restore it)
// GLOBAL: D3DREN 0x1005de1c
extern float g_ModelHalfTexelScale;		// guess: 0.5f set when the device is up (texel scale of the stage UV scale pair)
// ---- model drawing statistics (setupmodel.h would be their home; same drawmodelshadows constraint) ----
// GLOBAL: D3DREN 0x100587e0
extern int g_nClippedModelsDrawn;		// guess: models drawn with clipping this frame ("ModelProfile: %d clipped, %d unclipped")
// GLOBAL: D3DREN 0x10058cdc
extern int g_nUnclippedModelsDrawn;		// guess: models drawn without clipping this frame
// ---- particle drawing (units sys/d3d/drawparticles, sys/d3d/drawparticles_a) ----
// GLOBAL: D3DREN 0x1006d1b8
extern uint16 g_ParticleQuadIndices[0x300];	// the index list of the particle quads (0 1 2 0 2 3 ...), built by d3d_InitParticleQuadIndices

// ---- globals of unit unk/10007930 (the deferred global-pan poly list and the 0x28-byte near/left clippers) ----
// GLOBAL: D3DREN 0x1004ffb8
extern UnkType_PoolNode *g_pDeferredGlobalPanPolys;	// head of the deferred draw list
// GLOBAL: D3DREN 0x10094c20
extern int g_ClipNearInsideFlagsVertex40[56];	// guess: Jupiter polyclip.h bInside[] of the near plane clipper for 0x28-byte vertices
// GLOBAL: D3DREN 0x10094d00
extern int g_ClipLeftInsideFlagsVertex40[56];	// guess: bInside[] of the left plane clipper

#endif
