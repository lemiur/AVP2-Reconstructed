// d3d.ren fixed-size node pool: grows by allocating chunks of nodes and threading them on a singly linked free list
// (sb_Allocate pops, sb_AllocateNewStructPage (0x1003b5f0, StdLith struct_bank) grows).  Seed B.  Role names are guesses (comments); members are m_Unk<offset>.
#ifndef __D3DREN_POOL_H__
#define __D3DREN_POOL_H__

#include "ltbasedefs.h"
#include "d3dren/common_stuff.h"	// dalloc (FUN_10012ce5)
#include "d3dren/tlvertex.h"		// UnkType_TLVertex40

// NAME: StructBank: LT_StdLith struct_bank.h (the Talon SDK tree has it): the pools at 0x10058758 / 0x10058c98 are StructBanks (their
// members are m_StructSize, m_AlignedStructSize, m_CacheSize, m_nPages, m_nTotalObjects, m_PageHead, m_FreeListHead: +0x18 is the free list
// head that FlushWorldTexturePolys / d3d_FreeWorldPolyQueue push freed nodes on, an inlined sb_Free).  UnkType_Pool is the name the seed gave the type;
// it is kept as an alias for the units that already use it.  The pool objects in the exe sit inside bigger globals (the object at
// 0x10058758 is 0x2c bytes into the one that starts at 0x1005872c).
#include "../../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
typedef StructBank UnkType_Pool;

// Pool node used by the deferred draw list (QueueWorldTexturePoly) and the per-owner lists (AllocateTexturePolyNode); 0x14 bytes, 0x18 for the nodes of the queued world polys (W4: DrawLightmappedWorldPoly sets +0x14).
struct UnkType_PoolNode
{
	void				*m_Unk00;		// guess: the queued item
	int					m_Unk04;
	int					m_Unk08;
	int					m_Unk0c;		// guess: the render state word (g_ClipFlags) at the time it was queued
	UnkType_PoolNode	*m_Unk10;		// guess: next node
	uint32				m_Unk14;		// 0x14 guess: draw flags of a queued lightmap poly (bit 0 lightmapped, bit 1 saturate blend, bit 2 keep vertex alpha); only set by DrawLightmappedWorldPoly
};

// The per-owner list head allocated from the second pool (0x10058c98); 0x0c bytes.
struct UnkType_PoolBucket
{
	void				*m_Unk00;		// guess: owner
	UnkType_PoolNode	*m_Unk04;		// guess: first node
	UnkType_PoolBucket	*m_Unk08;		// guess: next bucket in the caller's list
};

// Owner of the buckets: the slot array sits at +0x1c.
struct UnkType_BucketOwner
{
	uint8				m_Pad00[0x1c];
	UnkType_PoolBucket	*m_Unk1c[1];	// 0x1c  guess: one bucket per slot index
};

struct UnkType_BucketOwnerRef2
{
	uint8				m_Pad00[0x2c];
	UnkType_BucketOwner	*m_Unk2c;
};

struct UnkType_BucketOwnerRef1
{
	uint8					m_Pad00[0x2c];
	UnkType_BucketOwnerRef2	*m_Unk2c;
};

// sb_Allocate is the StdLith inline of struct_bank.h (the exe's out-of-line copy is 0x10007e36, emitted in the unk/10007930 object).

// ---- functions of unit unk/10007930 that other units call (package W2 publishes them; bodies in src/d3dren/unk/10007930.cpp) ----
// All are plain cdecl.  Roles are in the guess comments; the one name with provenance is marked.
struct SharedTexture;
class RTexture;
struct WorldPoly;
class ViewParams;

// guess: world-poly draw state: stores the per-stage UV scale (g_TextureStageTexelSizes/14 * 1/size of the current texture ref) and the texture offsets.
void SetupWorldTextureCoordinates(void);
// guess: draws every poly queued by QueueWorldTexturePoly (list g_pDeferredGlobalPanPolys) with the texture of g_pGlobalPanInfo, then empties the list.
void FlushWorldTexturePolys(void);
// NAME: d3d_SetTexture: names_proposal.csv medium (Jupiter d3d_texture.h d3d_SetTexture(SharedTexture*, uint32 nStage, ...)): finds or
// creates the RTexture of pTexture for the stage, binds it (d3d_BindRTexture) and sets its LOD; returns 0 when there is no texture.
// dwMaxLOD: the argument of IDirectDrawSurface7::SetLOD (vtable +0xbc), which is the only thing the third argument is used for.
#include "d3dren/d3d_texture.h"	// d3d_SetTexture (inline; the exe's out-of-line copy is 0x100079e4 in unk/10007930), d3d_DisableTexture
// guess: binds the RTexture on its device stage (LRU list, SetTexture, ALPHAREF, per-stage UV scale, texture change counter).
void d3d_BindRTexture(RTexture *pRTexture);												// 0x10007a89
// guess: draws one world poly (more than 0x80 vertices: "Error: vertex buffer overflow") into the TL vertex buffer.
void DrawWorldTexturePoly(WorldPoly *pPoly);
// guess: adds pPoly to the per-texture bucket list of slot iSlot (buckets from the pool at 0x10058c98, nodes from 0x10058758).
UnkType_PoolNode *AllocateTexturePolyNode(WorldPoly *pPoly, UnkType_PoolBucket **ppBucketList, int iSlot);
// guess: adds the dynamic lights of the poly to the vertex colours of the 0x28-byte TL vertices pVerts.
// The third argument (the vertex count the callers pass) is not used: the body counts WorldPoly::m_nVertices.
void AddPolyDynamicVertexLighting(WorldPoly *pPoly, UnkType_TLVertex40 *pVerts, int param_3);
// guess: pushes pPoly on the deferred draw list g_pDeferredGlobalPanPolys.
void QueueWorldTexturePoly(WorldPoly *pPoly);
// guess: projects/clips the 0x28-byte vertex array (*ppVerts, *pnVerts) through the view transform at pViewParams (g_ViewParams,
// 0x10055cf8): in camera space with the clip mask g_ClipFlags when that is non-zero.  Returns 0 when nothing is left.
// The fourth argument is not used (every caller in the exe passes 0).
int TransformClipProjectPolygon40(UnkType_TLVertex40 **ppVerts, int *pnVerts, void *pViewParams, int param_4);
// guess: world space -> camera space for one vertex position: in-place 3x4 transform of pVec by the row-major matrix at pMatrix
// (g_ViewParams+0x15c).
void TransformPositionInPlace(float *pVec, const float *pMatrix);
// guess: clips the 0x28-byte polygon by the planes of the mask flags; same contract as Jupiter polyclip.h.
int ClipPolygon40(uint32 flags, UnkType_TLVertex40 **ppVerts, int *pnVerts);
// guess: camera space -> screen space for one vertex: rhw = 1/z, x, y scaled and offset through the viewport values at pViewParams+0x31c.., z mapped.
void ProjectVertexToScreen(float *pVert, const void *pViewParams);
// guess: the 0x28-byte-vertex plane clippers (near ClipPolyNear40 / left ClipPolyLeft40); first argument unused (Jupiter polyclip.h expanded out of line).
int ClipPolyNear40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);
int ClipPolyLeft40(char *pUnused, UnkType_TLVertex40 **ppVerts, int *pnVerts, UnkType_TLVertex40 **ppOut);

// The four line/plane intersection helpers (callers: the line system draw code): intersection of the edge p1-p2 with a clip plane.
float IntersectTopClipPlane(float *p1, float *p2, float *pOut);	// plane y == z
float IntersectRightClipPlane(float *p1, float *p2, float *pOut);	// plane x == z
float IntersectBottomClipPlane(float *p1, float *p2, float *pOut);	// plane y == -z
float IntersectFarClipPlane(float *p1, float *p2, float *pOut);	// plane z == far

// A vertex of a world poly as DrawWorldTexturePoly reads it (SPolyVertex of de_objects.h, 0x18 bytes, with the padding named).
struct UnkType_PolyVert
{
	LTVector	*m_pPos;			// 0x00
	float		m_Unk04;			// 0x04 guess: second texture coordinate u (lightmap)
	float		m_Unk08;			// 0x08 guess: second texture coordinate v
	uint8		m_Pad0c[0x14 - 0x0c];
	uint8		m_Unk14;			// 0x14 colour index (blue table)
	uint8		m_Unk15;			// 0x15 colour index (green table)
	uint8		m_Unk16;			// 0x16 colour index (red table)
	uint8		m_Pad17;
};

#endif
