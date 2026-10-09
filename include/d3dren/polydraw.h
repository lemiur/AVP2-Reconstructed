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

// ---- globals read by the world polygon code ------------------------------------------------------------------------------
// GLOBAL: D3DREN 0x10056770
extern MainWorld *g_pFrameMainWorld;			// guess: g_pMainWorld (m_LMGridSize at +0xf8 is the lightmap grid spacing)

// GLOBAL: D3DREN 0x1005872c
extern void (__fastcall *g_pfnCalcFogAlpha)(LTVector *pPos, uint32 *pSpecular);	// guess: per-vertex fog alpha hook (the vertex position and its specular colour)
// GLOBAL: D3DREN 0x10058c40
extern void (__fastcall *g_pfnCalcSkyFogAlpha)(LTVector *pPos, uint32 *pSpecular);	// guess: the sky's per-vertex fog hook (same signature; added by W6: d3d_drawsky and drawpolymgr use it)
// GLOBAL: D3DREN 0x100566bc
extern RGBColor g_GlobalVertexTintColor;			// guess: the diffuse colour the poly vertices are drawn with
// guess: sphere map (environment) texture coordinates of a point seen from pViewPos on a surface with normal pNormal: *pU, *pV.
void d3d_CalcWorldReflectionUVs(LTVector *pViewPos, LTVector *pPos, LTVector *pNormal, float *pU, float *pV);
// GLOBAL: D3DREN 0x100566ac
extern int g_nWorldPolysDrawn;				// guess: number of polys drawn (statistics)
// GLOBAL: D3DREN 0x10058040
extern uint8 g_u8FogColorR;				// guess: fog colour byte 0 (d3d_PackSqrtRGB / d3d_PackRGB pack three of them)
// GLOBAL: D3DREN 0x10058041
extern uint8 g_u8FogColorG;				// guess: fog colour byte 1
// GLOBAL: D3DREN 0x10058042
extern uint8 g_u8FogColorB;				// guess: fog colour byte 2

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
float IntersectNearClipPlane(float *p1, float *p2, float *pOut);
float IntersectLeftClipPlane(float *p1, float *p2, float *pOut);
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
// GLOBAL: D3DREN 0x10057774
extern uint8 g_nPolyVertexAlpha;				// guess: the alpha byte of the poly vertex colours
// GLOBAL: D3DREN 0x10055ce8
extern LTVector g_GlobalVertexTint;			// guess: the global light colour (SceneDesc +0x5c): scales the vertex colours
// GLOBAL: D3DREN 0x100566cc
extern int g_nLightTests;					// guess: dynamic light tests this frame ("Num Light Tests")
// GLOBAL: D3DREN 0x1005ce18
extern int g_bPortalsEnabled;				// guess: world models are drawn through the sorted portal path
// GLOBAL: D3DREN 0x10058c90
extern UnkType_PoolBucket *g_pMultipassWorldPolyBuckets;	// guess: list of the buckets (polys queued per lightmap page)
// GLOBAL: D3DREN 0x10058d00
extern int g_bDrawGouraudFullbritePass;		// guess: draw state flag (Gouraud fullbrites in use)
// Lighting / gamma byte tables of the vertex colours (256 entries each): the multipass (dynamic light) set and the single pass set.
// GLOBAL: D3DREN 0x10059d04
extern uint8 g_MultipassVertexTintTableR[256];
// GLOBAL: D3DREN 0x10059e04
extern uint8 g_MultipassVertexTintTableG[256];
// GLOBAL: D3DREN 0x10059f04
extern uint8 g_MultipassVertexTintTableB[256];
// GLOBAL: D3DREN 0x1005a004
extern uint8 g_VertexTintTableR[256];
// GLOBAL: D3DREN 0x1005a104
extern uint8 g_VertexTintTableG[256];
// GLOBAL: D3DREN 0x1005a204
extern uint8 g_VertexTintTableB[256];
// The current global pan texture reference (&RenderStruct::m_GlobalPans[n]) and the world texture coordinate offsets/scales
// the world poly draw state sets from it (SetupWorldTextureCoordinates).
// GLOBAL: D3DREN 0x10055ce0
extern GlobalPanInfo *g_pGlobalPanInfo;
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

#endif
