// d3d.ren surfaces (unit sys/d3d/d3d_surface): the RenderStruct surface functions, the optimized 2D (tile) surfaces and
// the D3DShadowTexture classes.  Talon-era (DirectDraw 7 / Direct3D 7) form of Jupiter's render_a/src/sys/d3d/
// d3d_surface.h, d3d_optimizedsurface.cpp and sys/shadows/d3dshadowtexture.h.
//
// NAME: RSurface, SurfaceTile, SurfaceTiles: Jupiter d3d_surface.h (names of the types and of m_pSurface, m_Desc,
// m_LastTransparentColor, m_pTiles, m_bTilesTransparent, m_nTilesX/Y, m_Tiles, m_SrcImageRect, m_nTileWidth/Height).  The
// Talon layout differs from Jupiter's D3D9 one (DirectDraw7 surfaces instead of D3D9 textures, no m_nXPixels/m_nYPixels).
#ifndef __D3DREN_D3D_SURFACE_H__
#define __D3DREN_D3D_SURFACE_H__

#include <windows.h>
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <ddraw.h>
#include <d3d.h>
#include "ltbasedefs.h"
#include "renderstruct.h"
#include "pixelformat.h"

// One tile of an optimized surface: a 2^n sized DirectDraw texture surface covering a 64x64 (or smaller) part of the image.
struct SurfaceTile
{
	LTRect				m_SrcImageRect;		// 0x00 the part of the source image this tile holds
	uint32				m_nTileWidth;		// 0x10
	uint32				m_nTileHeight;		// 0x14
	IDirectDrawSurface7	*m_pTexture;		// 0x18
};

struct SurfaceTiles
{
	uint32				m_nTilesX;			// 0x00
	uint32				m_nTilesY;			// 0x04
	SurfaceTile			m_Tiles[1];			// 0x08
};

// A surface handle (HLTBUFFER) of the renderer.  size 0x8c
struct RSurface
{
	IDirectDrawSurface7	*m_pSurface;				// 0x00
	DDSURFACEDESC2		m_Desc;						// 0x04
	uint32				m_LastTransparentColor;		// 0x80
	SurfaceTiles		*m_pTiles;					// 0x84
	int					m_bTilesTransparent;		// 0x88
};

// d3d_optimizedsurface part (unit-internal helpers are static in d3d_surface.cpp)
void d3d_DestroyTiles(RSurface *pSurface);			// FUN_1001bf10 (also called by DeleteSurface)

// d3d_optimizedsurface functions of the W7 package (src/d3dren/sys/d3d/d3d_optimizedsurface part, 0x1001bf63-0x1001d1b4).  Most are
// RenderStruct slots (RenderDLLSetup 0x10010ff1); the return type is LTBOOL (int) like the RenderStruct members.
struct TextureFormat;
void d3d_SetSolidAlpha(FMConvertRequest *pRequest, uint32 destAlpha32, GenericColor &tColor);										// 0x1001bf63
void d3d_DoAlphaFromColorKey(FMConvertRequest *pRequest, uint32 srcAlpha32, uint32 destAlpha32, GenericColor &tColor);			// 0x1001bfb4
LTBOOL d3d_FillSurfaceTiles(RSurface *pSurface, TextureFormat *pDestFormat, PValue transparentColor);						// 0x1001c133
void d3d_UnoptimizeSurface(HLTBUFFER hBuffer);																						// 0x1001c3d2 RenderStruct +0xe8
LTBOOL d3d_OptimizeSurface(HLTBUFFER hBuffer, PValue transparentColor);															// 0x1001c3e4 RenderStruct +0xe4
LTBOOL d3d_StartOptimized2D();																										// 0x1001c627 RenderStruct +0x9c
void d3d_EndOptimized2D();																											// 0x1001c725 RenderStruct +0xa0
LTBOOL d3d_IsInOptimized2D();																										// 0x1001c7eb RenderStruct +0xa4
LTBOOL d3d_SetOptimized2DBlend(LTSurfaceBlend blend);																				// 0x1001c7f1 RenderStruct +0xa8
LTBOOL d3d_SetOptimized2DColor(HLTCOLOR color);																					// 0x1001c8b8 RenderStruct +0xb0
LTBOOL d3d_GetOptimized2DBlend(LTSurfaceBlend &blend);																			// 0x1001c8ca RenderStruct +0xac
LTBOOL d3d_GetOptimized2DColor(HLTCOLOR &color);																					// 0x1001c8da RenderStruct +0xb4
void d3d_BlitToScreen3D(BlitRequest *pRequest);																					// 0x1001c8ea (called by the BlitToScreen slot)
void d3d_WarpToScreen3D(BlitRequest *pRequest);																					// 0x1001cc80 (called by the WarpToScreen slot)

// ---- the shadow texture classes (original object d3dshadowtexture, 0x1001d1b5-0x1001da5d) -----------------------------------
// Talon (DirectDraw 7) form of Jupiter's render_a/src/sys/shadows/d3dshadowtexture.h.  Differences: the texture is a
// DirectDraw 7 surface (m_pD3DTexture, +0xc) next to its size (+4, +8); D3DShadowTexture has abstract bases (an interface
// with 6 pure virtuals; the vtable at 0x10046308 shows dtor + 6 __purecall slots) and the factory too (vtable 0x10046334).
// NAME: IShadowTexture: Jupiter d3dmodelshadowrenderer.h forward-declares `class IShadowTexture` as the type
// GetShadowTexture() returns, and Jupiter's FreeShadowTexture still does `static_cast<D3DShadowTexture*>(pShadowTexture)`:
// the interface the Talon D3DShadowTexture derives from.  Its pure virtuals are named after the derived overrides: no name evidence.
#define MIN_SHADOW_TEXTURE_SIZE		8
#define MAX_SHADOW_TEXTURE_SIZE		256		// Jupiter has 512; Init() starts its power-of-two search at 0x100

class IShadowTexture
{
public:
	virtual ~IShadowTexture() {}												// slot 0 (vtable 0x10046308, ??_G 0x1001d1dd)
	virtual bool	Init(uint32 uiSizeX, uint32 uiSizeY) = 0;					// slot 1
	virtual void	GetDimensions(uint32 *pSizeX, uint32 *pSizeY) = 0;			// slot 2  guess: GetSize
	virtual bool	BindForShadowMultiply() = 0;											// slot 3  guess: set the texture stage state / bind the texture
	virtual bool	DrawScreenQuad(uint32 x, uint32 y, uint32 color, char bFlag) = 0;	// slot 4  guess: draw it as a screen space quad
	virtual bool	CopyToOffscreen(uint32 x, uint32 y) = 0;						// slot 5  guess: BltFast the texture onto the offscreen surface
	virtual bool	CopyFromOffscreen(uint32 x, uint32 y) = 0;						// slot 6  guess: BltFast the offscreen surface into the texture
};

class D3DShadowTextureInstance;

//! D3DShadowTexture (size 0x10, vtable 0x100462e8)
class D3DShadowTexture : public IShadowTexture
{
	friend class D3DShadowTextureInstance;

protected:
	D3DShadowTexture();
	virtual ~D3DShadowTexture();

public:
	virtual bool	Init(uint32 uiSizeX, uint32 uiSizeY);						// slot 1  0x1001d1fa
	virtual void	GetDimensions(uint32 *pSizeX, uint32 *pSizeY);				// slot 2
	virtual bool	BindForShadowMultiply();												// slot 3
	virtual bool	DrawScreenQuad(uint32 x, uint32 y, uint32 color, char bFlag);	// slot 4
	virtual bool	CopyToOffscreen(uint32 x, uint32 y);							// slot 5
	virtual bool	CopyFromOffscreen(uint32 x, uint32 y);							// slot 6
	virtual void	Term();														// slot 7  0x1001d478 (a virtual: Init calls it through the vtable)

	uint32				m_Unk04;												// 0x04  guess: width  (Init's uiSizeX after rounding)
	uint32				m_Unk08;												// 0x08  guess: height (uiSizeY)
	// NAME: m_pD3DTexture: Jupiter D3DShadowTexture::m_pD3DTexture (its only member, at +4 there; here the DirectDraw 7 surface)
	IDirectDrawSurface7	*m_pD3DTexture;											// 0x0c
};

class D3DShadowTextureFactory;

//! D3DShadowTextureInstance (size 0xc)
class D3DShadowTextureInstance
{
	friend class D3DShadowTextureFactory;

public:
	D3DShadowTextureInstance(D3DShadowTextureInstance *pPrev, D3DShadowTextureInstance *pNext);	// 0x1001d886
	~D3DShadowTextureInstance();												// 0x1001d8c0

public:
	D3DShadowTextureInstance *Find(D3DShadowTexture *pShadowTexture);			// 0x1001d8eb

protected:
	D3DShadowTexture			*m_pShadowTexture;								// 0x00
	D3DShadowTextureInstance	*m_pPrev;										// 0x04
	D3DShadowTextureInstance	*m_pNext;										// 0x08
};

// The abstract base of the factory (vtable 0x10046334: dtor + 2 pure virtuals; ??_G 0x1001d9a0).  Jupiter's factory has no base
// class; the name is invented.
class UnkType_ShadowTextureFactoryBase
{
public:
	virtual ~UnkType_ShadowTextureFactoryBase() {}
	virtual IShadowTexture	*AllocShadowTexture(uint32 uiSizeX, uint32 uiSizeY) = 0;
	virtual void			FreeShadowTexture(IShadowTexture *pShadowTexture) = 0;
};

//! D3DShadowTextureFactory (size 8, vtable 0x10046328)
class D3DShadowTextureFactory : public UnkType_ShadowTextureFactoryBase
{
public:
	D3DShadowTextureFactory();													// 0x1001d8ff
	virtual ~D3DShadowTextureFactory();											// slot 0: ??_G 0x1001d939, dtor 0x1001d955

public:
	virtual IShadowTexture		*AllocShadowTexture(uint32 uiSizeX, uint32 uiSizeY);	// slot 1  0x1001d9bd (VC6 has no covariant returns: IShadowTexture* like the base)
	virtual void				FreeShadowTexture(IShadowTexture *pShadowTexture);		// slot 2  0x1001da29

	// NAME: Get: Jupiter d3dshadowtexture.h D3DShadowTextureFactory::Get().  Added by the W3 agent: the out-of-line copy (0x100323f9)
	// sits in unit unk/10030bb0 (the first object that needed it); the callers (0x1001b840, 0x100323ff, 0x1003244f) use it.
	static D3DShadowTextureFactory *Get();										// 0x100323f9

protected:
	// GLOBAL: D3DREN 0x1007674c
	static D3DShadowTextureFactory *m_pShadowTextureFactory;
	D3DShadowTextureInstance *m_pFirst;											// 0x04
};

#endif
