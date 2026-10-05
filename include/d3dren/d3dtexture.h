// d3d.ren textures (unit sys/d3d/d3d_texture, owner: package W8): the RTexture (renderer side of a SharedTexture), the
// texture format table (one DirectDraw pixel format per use: [32BIT][FULLBRITE][4444][NORMAL][INTERFACE][LIGHTMAP][BUMPMAP])
// and the texture manager entry points.  Talon-era (DirectDraw 7 / Direct3D 7) form of Jupiter's render_a/src/sys/d3d/
// d3d_texture.h.  Unknown members are m_Unk<offset>.  Extend by splitting padding at the exact offset (small targeted edits).
//
// NAME: RTexture, g_Textures, FORMAT_* indices, m_pSharedTexture, m_BaseWidth, m_BaseHeight, m_DetailTextureScale,
// m_DetailTextureAngleC/S, m_iStartMipmap, m_Flags, m_AlphaRef, m_nMemory, m_nTextureFrameCode, m_Link: Jupiter d3d_texture.h
// (the Talon RTexture is 0x4c bytes with a vtable; Jupiter's D3D9 one has a different member set, the roles agree).
// NAME: m_pSurface: Jupiter d3d_surface.h RSurface::m_pSurface (the DirectDraw surface; Jupiter's RTexture has m_pD3DTexture).
#ifndef __D3DREN_D3DTEXTURE_H__
#define __D3DREN_D3DTEXTURE_H__

#include <windows.h>
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <ddraw.h>
#include <d3d.h>
#include "ltbasedefs.h"
#include "renderstruct.h"
#include "pixelformat.h"
#include "de_world.h"		// SharedTexture
#include "dtxmgr.h"			// TextureData
#include "d3dren/lightmap.h"	// UnkType_RTextureBase (the abstract interface of RTexture and of the lightmap pages)

// Indices into the texture format table (Jupiter CTextureManager::ETEXTURE_FORMATS; the strings of the Talon code agree:
// "FORMAT_FULLBRITE texture format missing.", "FORMAT_4444 ...", "FORMAT_NORMAL ...", "FORMAT_INTERFACE ...",
// "Warning: device not lightmap capable." and the "[FULLBRITE] - " ... "[LIGHTMAP] - " lines of the texture format list).
#define FORMAT_32BIT		0
#define FORMAT_FULLBRITE	1
#define FORMAT_4444			2
#define FORMAT_NORMAL		3
#define FORMAT_INTERFACE	4
#define FORMAT_LIGHTMAP		5
#define FORMAT_BUMPMAP		6
#define NUM_TEXTUREFORMATS	7

// One enumerated texture pixel format (0xa4 bytes, allocated with dalloc by the EnumTextureFormats callback 0x1001f0d0).
// Nodes are linked into the list at 0x10062858 (m_Link.m_pData = the node).
struct TextureFormat
{
	LTLink			m_Link;				// 0x00
	DDPIXELFORMAT	m_PF;				// 0x0c copy of the enumerated pixel format (dwSize 0x20 at 0x0c, dwFlags 0x10, dwFourCC 0x14,
										//      dwRGBBitCount 0x18, R/G/B/A masks 0x1c/0x20/0x24/0x28)
	int				m_BytesPPShift;			// 0x2c log2 of the bytes per pixel (1 = 16 bit, 2 = 32 bit, 0 = palettized/other)
	int				m_BytesPP;			// 0x30 1 << m_nBytesPP
	int				m_RBits;			// 0x34 number of bits in the red mask
	int				m_GBits;			// 0x38 ... green mask
	int				m_BBits;			// 0x3c ... blue mask
	int				m_ABits;			// 0x40 ... alpha mask
	int				m_Shifts[20];		// 0x44 ten pairs (right shift, left shift) that convert a colour channel of a reference layout to this format's layout:
										//      R, G, B, A of an 8888 pixel (0x44-0x60), R, G, B of a 565 pixel (0x64-0x78), R, G, B of a 555 pixel (0x7c-0x90)
	uint16			m_rgbaMask[4];			// 0x94 the low 16 bits of the R, G, B, A masks
	int				m_b1555;			// 0x9c the format is 1555 (masks 7c00 03e0 001f 8000)
	int				m_b565;			// the format is 565 (masks f800 07e0 001f 0000)
};

// One row of the table of wanted formats that the texture manager Init matches against the enumerated ones (0x18 bytes):
// the numbers of bits of the red, green and blue masks and a pair of DDPF_ flag sets.
struct TextureFormatSpec
{
	int				m_nBitsR;			// 0x00
	int				m_nBitsG;			// 0x04
	int				m_nBitsB;			// 0x08
	uint32			m_Unk0c;			// 0x0c number of alpha bits (not compared)
	uint32			m_dwFlagsAny;		// 0x10 the format must have at least one of these DDPF_ flags
	uint32			m_dwFlagsNone;		// 0x14 ... and none of these
};

class RTexture;

// The part of an RTexture the texture binding code reads and the RTexture virtuals forward to (0x1c bytes, vtable 0x10046390).
// The vtable and 0x1001e900 (constructor: vptr only) belong to this class; RTexture holds it as its first member and the
// virtuals reach the rest of the RTexture through m_pOwner.  1001fff0/10021290 build one on the stack (a 0x1c byte
// object whose vptr is wiped by the zero fill and whose members 10020fb0 copies into the new RTexture).
class RTextureData : public RTextureBase
{
public:
	RTextureData() {}										// 0x1001e900 (out of line copy: vptr only)

	virtual int		IsRTexture();							// 0x1001e6f0: returns 1
	virtual int		IsFullbrite();							// 0x1001e700
	virtual int		GetBaseWidth();							// 0x1001e710
	virtual int		GetBaseHeight();						// 0x1001e720

	float				m_fUScale;			// 0x04 1/width of the texture (u multiplier)
	float				m_fVScale;			// 0x08 1/height (v multiplier)
	IDirectDrawSurface7	*m_pSurface;		// 0x0c
	int					m_nMemory;			// 0x10 bytes of texture memory (RenderStruct+0x50 total, +0x48 per frame counter)
	uint16				m_nTextureFrameCode;// 0x14 frame code of the last use
	uint16				m_AlphaRef;			// 0x16 alpha reference (0 = none)
	RTexture			*m_pOwner;			// 0x18 the RTexture holding this object (the RTexture itself for the embedded one)
};

// The renderer side of a texture (0x4c bytes, allocated from the RTexture bank g_RTextureBank).
class RTexture
{
public:
	RTexture();														// 0x1001e690
	// NAME: IsFullbrite: Jupiter RTexture::IsFullbrite (names_proposal.csv medium).  The RTexture's vtable pointer is the first member's (m_Data,
	// vtable 0x10046390): slot 2 of it, called through the pointer (virtual dispatch), as the callers in the drawing code do.
	int IsFullbrite()
	{
		RTextureData *pData = &m_Data;
		return pData->IsFullbrite();
	}

	RTextureData	m_Data;				// 0x00 vtable 0x10046390 + surface etc.
	LTLink					m_Link;				// 0x1c in g_Textures (0x10062868)
	uint16					m_BaseWidth;		// 0x28 first usable mipmap width
	uint16					m_BaseHeight;		// 0x2a
	SharedTexture			*m_pSharedTexture;	// 0x2c (0 for a texture chained behind another stage)
	RTexture				*m_Unk30;			// 0x30 next RTexture of the same SharedTexture (one per device stage)
	float					m_DetailTextureScale;	// 0x34 1.0f + the header's detail scale
	float					m_DetailTextureAngleC;	// 0x38 cos of the detail angle
	float					m_DetailTextureAngleS;	// 0x3c sin
	uint16					m_Flags;			// 0x40 DTX_FULLBRITE
	uint16					m_Unk42;			// 0x42 device stage
	uint16					m_Unk44;			// 0x44 current LOD
	uint8					m_iStartMipmap;		// 0x46 first mipmap of the SharedTexture's TextureData that this RTexture uses
	uint8					m_Unk47;			// 0x47 number of mipmaps in the surface
	uint8					m_Unk48;			// 0x48 index into the format table g_TextureFormats
	uint8					m_Unk49;			// 0x49 bit 0: not the first RTexture of its SharedTexture (additional stage)
	uint8					m_Pad4a[2];			// 0x4a
};

// GLOBAL: D3DREN 0x10062830
extern TextureFormat *g_TextureFormats[NUM_TEXTUREFORMATS];		// the format table g_TextureFormats[FORMAT_*] (0x10062830-0x10062848)

// GLOBAL: D3DREN 0x10062868
extern LTLink g_Textures;		// LRU list of the RTextures (RTexture::m_Link, m_pData = the RTexture); 10007930.cpp declares it too

// RenderStruct bytes 0x4c/0x50 live in padding of the engine's renderstruct.h (m_Pad48): 0x50 = bytes of texture memory in use
// (Jupiter m_SystemTextureMemory), 0x4c = the per-frame texture byte counter.
#define RENDERSTRUCT_TEXMEM(p)			(*(int *)((uint8 *)(p) + 0x50))

// GLOBAL: D3DREN 0x1005c984
extern uint32 DAT_1005c984;			// D3DPRIMCAPS.dwTextureCaps copy of the device caps (0x20 = D3DPTEXTURECAPS_SQUAREONLY); W7's optsurface.cpp declares it too
// DXT support flags, set by 0x1001f600 from the enumerated formats (Jupiter m_bSupportsDXT1/3/5).
// GLOBAL: D3DREN 0x10062854
extern int DAT_10062854;			// DXT1 supported
// GLOBAL: D3DREN 0x10062850
extern int DAT_10062850;			// DXT3 supported
// GLOBAL: D3DREN 0x1006284c
extern int DAT_1006284c;			// DXT5 supported

// Helper textures and state of the texture manager (cleared by Init/Term).
// GLOBAL: D3DREN 0x10062874
extern int g_bTextureManagerInitialized;							// the texture manager is initialised (set by Init, cleared by Term)
// GLOBAL: D3DREN 0x10062878
extern IDirectDrawSurface7 *DAT_10062878;			// the 0x20x0x20 lightmap format dummy surface (0x1001eb80 / the tail of Init)
// GLOBAL: D3DREN 0x1006287c
extern IDirectDrawSurface7 *g_pShadowBlobTexture;			// the 16x16 shadow blob texture (0x1001f670); W7's modelshadows.cpp declares it too
// GLOBAL: D3DREN 0x10062880
extern IDirectDrawSurface7 *g_pSpecularTexture;			// the 64x64 specular lookup texture (0x1001e910); W7's drawmodel.cpp declares it too
// GLOBAL: D3DREN 0x10062884
extern float g_fSpecularTexturePower;							// the specular power the lookup texture was built for

// ---- the texture manager's functions (free functions in d3d.ren; Jupiter's CTextureManager members) ----------------------------
// NAME: FUN_<addr> with the Jupiter CTextureManager member name in the comment where names_proposal.csv has one (medium): the
// d3d.ren versions work on globals and have no `this`.
int FUN_10021960(BPPIdent bpp, uint32 *pFourCC);			// 0x10021960 S3TCFormatConv: DXT FOURCC of a compressed BPPIdent
int FUN_10021a50(BPPIdent bpp);								// 0x10021a50 IsS3TCFormatSupported
int d3d_GetFirstUsableMipmap(TextureData *pTexture);		// 0x10020f20
void AdjustAspectRatio(uint32 width, uint32 height, uint32 *outWidth, uint32 *outHeight);	// 0x100219b0
TextureFormat *FUN_1001f590(const TextureFormatSpec *pSpecs, uint32 nSpecs);	// 0x1001f590 search the enumerated formats for the first spec that matches
HRESULT WINAPI FUN_1001f0d0(LPDDPIXELFORMAT pFormat, LPVOID pContext);	// 0x1001f0d0 d3d_EnumTextureFormatsCallback
void FUN_1001f850(RTexture *pTexture, int bChained);		// 0x1001f850 FreeTexture (frees the chained stage textures; an additional-stage RTexture only when bChained)
// The three values the texture creation functions take from the caller (SharedTexture, its TextureData and the device stage flags).
struct UnkType_RTextureBuild
{
	SharedTexture	*m_pSharedTexture;	// 0x00
	TextureData		*m_pTextureData;	// 0x04
	uint32			m_nFlags;			// 0x08 stage in the low byte, 0x100 = bump map stage
};
int r_TransferTexture(RTexture *pTexture, TextureData *pTextureData);	// 0x10020360 copies the mipmaps of pTextureData into the surface chain of pTexture; 0 on failure
RTexture *FUN_10021290(UnkType_RTextureBuild *pBuild, int bAdditional);	// 0x10021290 CreateRTexture
void FUN_1001f920(LTLink *pList);							// 0x1001f920 FreeTexture on every RTexture of an LTLink list, then tie the head off
void d3d_FreeAllTextures();
void FUN_1001f600();										// 0x1001f600 sets the DXT support flags from the enumerated formats
void FUN_1001f670();										// 0x1001f670 builds the shadow blob texture
void d3d_ReinitLightmapTextureSupport();										// 0x1001eb80 re-creates the lightmap texture pools and the lightmap dummy surface
void d3d_BuildSpecularLookupTexture(float fPower);							// 0x1001e910 builds the specular lookup texture
int FUN_1001ec50();											// 0x1001ec50 CTextureManager::Init (called by the device creation 0x1001acc0)
void d3d_TermTextureManager();										// 0x1001f770 Term (CTextureManager::Term)
void d3d_ListTextureFormats();										// 0x1001f9b0 ListTextureFormats (LISTTEXTUREFORMATS)
void d3d_PrintFormatInfo(const char *pStart, TextureFormat *pFormat);	// 0x1001fa40 prints one format with its DDPF_ flag names										// 0x1001f960 FreeAllTextures (g_Textures)
void d3d_UnbindTexture(SharedTexture *pSharedTexture);		// 0x10021c60 (RenderStruct::UnbindTexture)

#endif
