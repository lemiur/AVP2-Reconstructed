// d3d.ren lightmaps (owner: package W9, units unk/100329b0 and unk/10034000): the lightmap pages (64x64 DirectDraw texture
// pages the world polygons' lightmaps are packed into), the staging texture pools and the lock/unlock helper that the
// dynamic light and light animation code (and the world drawing units) use to write a polygon's lightmap, the six
// lightmap planes, and the RenderContext (CreateContext's object).  Talon-era (DirectDraw 7 / Direct3D 7) code; Jupiter's
// descendant renderer has none of the page code (its lightmaps are plain D3D9 textures), so the page/pool/lock types have
// no source-established names: `UnkType_` types, `FUN_<addr>` functions, `m_Unk<offset>` members, roles in the comments.
//
// NAME: LMPlane, g_LMPlanes, SetupLMPlaneVectors, NUM_LMPLANES: Jupiter runtime/shared/src/lightmap_planes.h/.cpp (and
// the engine twin src/shared/lightmap_planes.cpp, whose g_LMPlanes initialiser is the same table).
// NAME: RenderContext: Jupiter render_a/src/sys/d3d/common_stuff.h `struct RenderContext { uint16 m_CurFrameCode; }`; the
// Talon object (CreateContext 0x1001b700, 0x10 bytes) has the lightmap page list in front of it and the frame code at 0xc.
//
// Everything that is a renderer view of engine structures (WorldPoly, SPolyVertex) lives in engine padding; the accessors
// below name the offsets without touching the engine headers (request to the lead: split the pads, see the W9 report).
#ifndef __D3DREN_LIGHTMAP_H__
#define __D3DREN_LIGHTMAP_H__

#include <windows.h>
#ifndef DIRECTDRAW_VERSION
#define DIRECTDRAW_VERSION 0x0700
#endif
#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0700
#endif
#include <ddraw.h>
#include <d3d.h>
#include "ltbasedefs.h"
#include "pixelformat.h"
#include "d3dren/common_stuff.h"	// RenderContext
#include "de_mainworld.h"		// MainWorld, LightAnim, WorldPoly (de_world.h / de_objects.h)

// Surface::m_Flags: the polygon's surface is lightmapped (NAME: Jupiter runtime/world/src/de_world.h SURF_LIGHTMAP).
#ifndef SURF_LIGHTMAP
#define SURF_LIGHTMAP		(1<<7)
#endif

// FUN_10033210 (unit unk/100329b0): builds the lightmap of a polygon from the light animations that touch it and writes it
// into its page; bPageIn is 1 when the pages are being built (PageInLightmaps).  Declared here for unit unk/10034000.
int FUN_10033210(MainWorld *pWorld, WorldPoly *pPoly, int bPageIn);

// ---- renderer data inside the engine's WorldPoly / SPolyVertex (padding there) --------------------------------------------
// WorldPoly 0x48: the lightmap page the poly's lightmap lives in (UnkType_LMPage*, 0 = none); 0x4c/0x4d are the engine's
// m_LMWidth/m_LMHeight (texels), 0x4e/0x4f the position of the lightmap inside the page (texels).
#define WORLDPOLY_LMPAGE(p)		(*(LightmapPage **)((uint8 *)(p) + 0x48))
#define WORLDPOLY_UNK4E(p)		(*((uint8 *)(p) + 0x4e))
#define WORLDPOLY_UNK4F(p)		(*((uint8 *)(p) + 0x4f))
// SPolyVertex 0x0c/0x10: the vertex's lightmap texture coordinates (page relative, written by FUN_1003429b).
#define SPOLYVERTEX_UNK0C(v)	(*(float *)((uint8 *)(v) + 0x0c))
#define SPOLYVERTEX_UNK10(v)	(*(float *)((uint8 *)(v) + 0x10))

// ---- the lightmap pages -------------------------------------------------------------------------------------------------------
// Abstract base of the renderer's texture-like objects (vtable 0x100463a4: scalar deleting destructor + 4 pure virtuals);
// RTexture (d3dtexture.h, vtable 0x10046390) and the lightmap page (vtable 0x100464c8) derive from it.  The slot roles are
// from the RTexture implementations (0x1001e6f0.., names_proposal.csv: slot 2 = IsFullbrite and 3/4 = GetBaseWidth/Height are
// Jupiter RTexture names, medium); slot 1 returns 1 for an RTexture and 0 for a page (role unknown).
class RTextureBase
{
public:
	virtual ~RTextureBase() {}										// 0x00
	virtual int		IsRTexture() = 0;								// 0x04
	virtual int		IsFullbrite() = 0;								// 0x08
	virtual int		GetBaseWidth() = 0;								// 0x0c
	virtual int		GetBaseHeight() = 0;							// 0x10
};

// One 64x64 lightmap page (0x28 bytes, vtable 0x100464c8; built by FUN_10034142, which allocates the DirectDraw texture
// surface and the 0x80 byte occupancy bitmap).  The occupancy bitmap has one bit per 4x4 texel cell, 0x40 cells per row.
struct LightmapPage : public RTextureBase
{
	LightmapPage();													// 0x1003424d
	virtual int		IsRTexture();									// 0x10034277
	virtual int		IsFullbrite();									// 0x10034277 (same code)
	virtual int		GetBaseWidth();									// 0x1003427a: 64
	virtual int		GetBaseHeight();								// 0x1003427a (same code)

	uint32					m_Unk04;			// 0x04 next page of the list of pages that have polys waiting (world poly queue, unit unk/100098d0)
	uint32					m_Unk08;			// 0x08 the polys waiting for this page (pool nodes)
	uint8					*m_pOccupancyMap;	// 0x0c occupancy bitmap (dalloc_z(0x80)), freed by FreeLightmapPageBitmaps
	uint32					m_nUsedTexels;		// 0x10 texels of the page assigned so far (FUN_1003429b adds w*h)
	uint32					m_nMemoryUse;		// 0x14 size of the surface in bytes (FUN_10034142: bytes per pixel << 12)
	uint32					m_Unk18;			// 0x18
	IDirectDrawSurface7		*m_pSurface;		// 0x1c the page's texture surface
	uint32					m_Unk20;			// 0x20 (set to 1 by the constructor; non-zero once the first draw has set the page up)
	LightmapPage			*m_pNext;			// 0x24 next page of the RenderContext list
};

// ---- the lightmap staging textures and the lock helper ---------------------------------------------------------------------------
// guess: a lightmap staging texture pool is made per lightmap size class (4, 8, 16, 32, 64 texels square); each has a circular
// list of pool textures (RTexture, DAT_1007bfe8 + 12 * class) that the staged lightmap is copied into (IDirect3DDevice7::Load)
// round robin, and one staging texture (DAT_1007abd0[class]) that is locked for writing.
class RTexture;

// guess: a lightmap staging texture while a poly's lightmap is being written (0x54 bytes).  FUN_10034af0 picks the size class
// of the poly's lightmap (m_Width x m_Height, default: the poly's own size), optionally pre-fills the staging texture with the
// lightmap that is already in the poly's page (or clears it), and locks it; FUN_10034c7c unlocks it and, when asked, copies it
// into the next pool texture and binds that on the lightmap stage.  Callers declare one on the stack (the PFormat vptr store).
struct UnkType_LMLock
{
	uint8		*m_Unk00;		// 0x00 the locked texels (DDSURFACEDESC2::lpSurface)
	long		m_Unk04;		// 0x04 pitch in bytes
	uint32		m_Unk08;		// 0x08 size class
	PFormat		m_Unk0c;		// 0x0c pixel format of the locked surface (DDPFToPFormat)
	uint32		m_Unk44;		// 0x44 width in texels
	uint32		m_Unk48;		// 0x48 height in texels
	LTLink		*m_Unk4c;		// 0x4c list head of the pool of this size class
	RTexture	*m_Unk50;		// 0x50 the staging texture

	// bClear: 1 = fill the staging texture first (from the poly's page when it has one, else with black).  0x10034af0
	int			FUN_10034af0(WorldPoly *pPoly, int bClear, uint32 width = 0, uint32 height = 0);
	// bUpload: also copy the texels into the next pool texture and bind it (lightmap stage).  0x10034c7c
	int			FUN_10034c7c(int bUpload);
};

// guess: the lightmap texture pools: one global object (no data members: the staging textures DAT_1007abd0[5] and the five list
// heads DAT_1007bfe8[5] are statics in the exe's .bss) whose methods are called by the texture manager (unit sys/d3d/d3d_texture)
// when textures are (re)created and freed: FUN_10034e61 frees every pool and staging texture, FUN_10034db8 makes them (pfnCreate =
// the texture creation function 0x1001e750: width, height, flags 0x4000 pool / 0x800 staging), FUN_10034e3d forgets them.
typedef RTexture *(*PFN_CreateLMTexture)(uint32 width, uint32 height, uint32 flags);
class UnkType_LMTexturePools
{
public:
	UnkType_LMTexturePools();									// 0x10034d92 (out of line copy)
	void FUN_10034db8(PFN_CreateLMTexture pfnCreate);			// 0x10034db8
	void FUN_10034e3d();										// 0x10034e3d
	void FUN_10034e61();										// 0x10034e61
};
// GLOBAL: D3DREN 0x1007abe4
extern UnkType_LMTexturePools DAT_1007abe4;

// ---- colour lookup tables of the lightmap code (static data classes with a constructor that fills them; used by the light animation
// and dynamic light code of unit unk/100329b0 and by the world drawing units) ------------------------------------------------------
// guess: saturating byte add: [a + b] for two bytes a, b (0..255 -> a, 256..511 -> 255)
class UnkType_AddClampTable
{
public:
	UnkType_AddClampTable();									// 0x10035cf1
	uint8	m_Unk00[512];
};
// GLOBAL: D3DREN 0x10092168
extern UnkType_AddClampTable DAT_10092168;

// guess: byte multiply: [a * 256 + b] = a * b / 255
class UnkType_MulTable
{
public:
	UnkType_MulTable();											// 0x10035d1d
	uint8	m_Unk00[256 * 256];
};
// GLOBAL: D3DREN 0x10082168
extern UnkType_MulTable DAT_10082168;

// guess: [i] = sqrt(i / 512) * 255, clamped to 0..255
class UnkType_SqrtTable
{
public:
	UnkType_SqrtTable();										// 0x10035d7f
	uint8	m_Unk00[256];
};
// GLOBAL: D3DREN 0x10082068
extern UnkType_SqrtTable DAT_10082068;

// ---- the six lightmap planes (engine twin: src/shared/lightmap_planes.cpp; the renderer also has SetupLMPlaneVectors) ------
class LMPlane
{
public:
	LMPlane(LTVector inP, LTVector inQ, LTVector inNormal);		// 0x10034a77

	LTVector	P, Q, Normal;
};

#define NUM_LMPLANES	6

// GLOBAL: D3DREN 0x1007aaf8
extern LMPlane g_LMPlanes[NUM_LMPLANES];

// Sets up the lightmap texture vectors P and Q for lightmap plane iPlane (WorldPoly::m_Flags bits 11-13) and the poly's plane
// normal N: P = N x g_LMPlanes[iPlane].Q, Q = P x N.  0x10034a9d (used by 6 units).
void SetupLMPlaneVectors(uint32 iPlane, const LTVector &N, LTVector &P, LTVector &Q);

// ---- RenderContext (RenderStruct::CreateContext, 0x1001b700 in unit sys/d3d/d3d_init; one per world) ---------------------------
// The struct is defined in d3dren/common_stuff.h (W5's header, Jupiter's name): m_Unk00 the page list head, m_Unk04 the page count,
// m_Unk08 the MainWorld, m_CurFrameCode at 0x0c.  The lightmap code owns the first three.

// guess: builds the lightmap pages of the context's world (0 = failed, pages freed again); called by CreateContext (0x1001b700)
// and RebindLightmaps (0x1001b790).
int PageInLightmaps(RenderContext *pContext);					// 0x10034597
// guess: frees the pages of the context and forgets them in the world's polygons (DeleteContext, RebindLightmaps).
void FreeLightmapPages(RenderContext *pContext);					// 0x100347ac

#endif
