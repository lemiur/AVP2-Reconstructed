// d3d.ren sys/d3d/d3d_texture (0x1001e5a0-0x10021d70): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// one object.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// sys/d3d/d3d_texture (0x1001e5a0-0x10021d70): the texture manager of the DirectDraw 7 / Direct3D 7 renderer: the enumerated
// texture pixel formats and the FORMAT_* table, RTexture (renderer side of a SharedTexture) and its bank, texture creation
// (surface, mipmap range, RTexture), upload (r_TransferTexture), binding (BindTexture/UnbindTexture) and the lightmap /
// specular / shadow helper textures.  Talon form of Jupiter's render_a/src/sys/d3d/d3d_texture.cpp (a D3D9 file; the code
// is related but not the same).
// FLAGS: /O2 /Ob2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include "d3dren/d3dtexture.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dstate.h"
#include "d3dren/lightmap.h"
#include "d3dren/scenedesc.h"
#include "d3dren/polydraw.h"
#include "counter.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"

int FUN_10020ab0(UnkType_RTextureBuild *pBuild, UnkType_RTextureData *pData, uint32 iStartMipmap, uint32 nMipmaps, uint32 iFormat);
void FUN_10032a30();	// 0x10032a30 (lightmap unit unk/100329b0)
int FUN_10032c40(MainWorld *pWorld, WorldPoly *pPoly, uint8 *pBits, long pitch, uint32 w, uint32 h, char bNot32Bit);	// 0x10032c40 (unit unk/100329b0)
// guess: counters of the dynamic lightmap refresh (FUN_10020ff0): staging lightmaps locked, and lightmaps where no light changed a texel
// GLOBAL: D3DREN 0x10056278
extern int DAT_10056278;
// GLOBAL: D3DREN 0x10055cdc
extern int DAT_10055cdc;

// RenderStruct::GetTexture as the renderer calls it: with a second (output) argument that the engine's function ignores
// (renderstruct.h declares one parameter).
typedef TextureData *(*PFN_GetTexture2)(SharedTexture *pTexture, uint32 *pUnused);
#define FUN_GetTextureData(pTexture, pUnused)	(((PFN_GetTexture2)g_pStruct->GetTexture)((pTexture), (pUnused)))
IDirectDrawSurface7 *d3d_CreateTextureFromPixels(uint32 *pPixels, uint32 width, uint32 height, uint32 pitch);
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat);	// 0x100109fd (the engine's cutil.cpp copy)

// ---- globals defined by this object -----------------------------------------------------------------------------------------

// guess: the list of the enumerated texture formats (TextureFormat::m_Link); Jupiter keeps no such list.
// FUNCTION: D3DREN 0x1001e5a0 _$E2
// GLOBAL: D3DREN 0x10062858
LTLink g_TextureFormatList(LTLink_Init);
// guess: an LTList (count at 0x100613a8, head at 0x100613ac) that the texture manager Init clears; nothing in this unit uses it.
// FUNCTION: D3DREN 0x1001e5b0 _$E5
// GLOBAL: D3DREN 0x100613a8
LTList DAT_100613a8(LTLink_Init);
// guess: Jupiter CTextureManager::m_RTextureBank (a member there; the Talon code keeps it as a global).
// FUNCTION: D3DREN 0x1001e5d0 _$E10
// FUNCTION: D3DREN 0x1001e600 _$E8
// GLOBAL: D3DREN 0x100617e8
ObjectBank<RTexture, NullCS> g_RTextureBank;
// FUNCTION: D3DREN 0x1001e630 ??_GBaseObjectBank@@UAEPAXI@Z
// NAME: g_Textures: Jupiter d3d_texture.cpp DECLARE_LTLINK(g_Textures) (names_proposal.csv, high)
// FUNCTION: D3DREN 0x1001e650 _$E13
LTLink g_Textures(LTLink_Init);
// NAME: g_FormatMgr: Jupiter d3d_texture.cpp `FormatMgr g_FormatMgr;` (Ghidra name)
// FUNCTION: D3DREN 0x1001e660 _$E16
FormatMgr g_FormatMgr;
// FUNCTION: D3DREN 0x1001e670 _$E19
ConVar g_CV_S3TCEnable("S3TCEnable", 1.0f);

TextureFormat *g_TextureFormats[NUM_TEXTUREFORMATS];
// guess: the exponent of the shadow blob alpha falloff (FUN_1001f670): an initialised float of this object (.data, 0x1004b5d8).
// GLOBAL: D3DREN 0x1004b5d8
float DAT_1004b5d8 = 3.0f;
int DAT_10062854;
int DAT_10062850;
int DAT_1006284c;
int g_bTextureManagerInitialized;
IDirectDrawSurface7 *DAT_10062878;
IDirectDrawSurface7 *g_pShadowBlobTexture;
IDirectDrawSurface7 *g_pSpecularTexture;
float g_fSpecularTexturePower;

// FUNCTION: D3DREN 0x1001e690
RTexture::RTexture()
{
	m_Link.Init();
	m_BaseHeight = 0;
	m_BaseWidth = 0;
	m_pSharedTexture = 0;
	m_Flags = 0;
	m_Unk42 = 0;
	m_iStartMipmap = 0;
	m_Unk47 = 0;
	m_Unk48 = 0;
	m_Unk49 = 0;
	m_Unk30 = 0;
	m_Unk44 = 0;
	m_DetailTextureScale = 1.0f;
}

// FUNCTION: D3DREN 0x1001e6d0 ??_GUnkType_RTextureData@@UAEPAXI@Z
// (the scalar deleting destructors of RTexture's data class and of RTextureBase are the same code: folded by the linker)

// FUNCTION: D3DREN 0x1001e6f0 ?IsRTexture@UnkType_RTextureData@@UAEHXZ
int UnkType_RTextureData::IsRTexture()
{
	return 1;
}

// FUNCTION: D3DREN 0x1001e700 ?IsFullbrite@UnkType_RTextureData@@UAEHXZ
int UnkType_RTextureData::IsFullbrite()
{
	return m_pOwner->m_Flags;
}

// FUNCTION: D3DREN 0x1001e710 ?GetBaseWidth@UnkType_RTextureData@@UAEHXZ
int UnkType_RTextureData::GetBaseWidth()
{
	return m_pOwner->m_BaseWidth;
}

// FUNCTION: D3DREN 0x1001e720 ?GetBaseHeight@UnkType_RTextureData@@UAEHXZ
int UnkType_RTextureData::GetBaseHeight()
{
	return m_pOwner->m_BaseHeight;
}

// NAME: names_proposal.csv guess_GetLightmapTextureFormat (low, invented): not used
// FUNCTION: D3DREN 0x1001e730
TextureFormat *d3d_GetLightmapTextureFormat()
{
	if (g_b32BitLightmaps)
	{
		if (g_TextureFormats[FORMAT_32BIT])
			return g_TextureFormats[FORMAT_32BIT];
	}
	return g_TextureFormats[FORMAT_LIGHTMAP];
}

// Creates a lightmap page texture surface (the DirectDraw surface in the lightmap format, DDSD_TEXTURESTAGE for one-pass lightmapping)
// and an RTexture for it: the allocator callback the lightmap texture pools use (UnkType_LMTexturePools::FUN_10034db8).
// NAME: names_proposal.csv guess_CreateLightmapPageTexture (low, invented): not used
// NOT MATCHING (400 vs 432 bytes, 306 strict differences): earlier reciprocal stores improve scheduling, but the native width
// reciprocal remains live across overriding assignments whereas ours is stored sooner. The native out-of-line data constructor,
// separate failure epilogues, and saved-width register assignment remain unresolved. Nested/goto/else failure shapes and local
// width/height aliases do not recover those differences. All other owning-unit function bytes and relocations are preserved.
// STUB: D3DREN 0x1001e750
RTexture *FUN_1001e750(uint32 width, uint32 height, uint32 flags)
{
	TextureFormat *pFormat;
	DDSURFACEDESC2 ddsd;
	IDirectDrawSurface7 *pSurface;
	RTexture *pRTexture;

	if ((!g_b32BitLightmaps || !(pFormat = g_TextureFormats[FORMAT_32BIT])) && !(pFormat = g_TextureFormats[FORMAT_LIGHTMAP]))
		return 0;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwHeight = height;
	ddsd.ddsCaps.dwCaps = flags | DDSCAPS_TEXTURE;
	ddsd.dwTextureStage = DAT_1005c838;
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_TEXTURESTAGE;
	ddsd.dwWidth = width;
	ddsd.ddpfPixelFormat = pFormat->m_PF;
	if (g_pDD->CreateSurface(&ddsd, &pSurface, 0) != 0)
		return 0;

	pRTexture = g_RTextureBank.Allocate();
	if (pRTexture)
	{
		pRTexture->m_Data.m_Unk04 = 1.0f / (float)width;
		pRTexture->m_Data.m_Unk08 = 1.0f / (float)height;
		pRTexture->m_Data.m_pOwner = pRTexture;
		pRTexture->m_Data.m_pSurface = pSurface;
		pRTexture->m_BaseHeight = height;
		pRTexture->m_pSharedTexture = 0;
		pRTexture->m_Link.m_pData = 0;
		pRTexture->m_Flags = 0;
		pRTexture->m_BaseWidth = width;
		pRTexture->m_Data.m_nTextureFrameCode = 0;
		return pRTexture;
	}
	pSurface->Release();
	return 0;
}

// The UnkType_RTextureData constructor (vptr only), kept out of line in the exe: 0x1001e900.
// FUNCTION: D3DREN 0x1001e900 ??0UnkType_RTextureData@@QAE@XZ

// Builds the 64x64 grey lookup table texture of the specular power fPower (a diagonal band of pow(i / 64, fPower) * 255) once.
// NAME: names_proposal.csv guess_BuildSpecularLookupTexture (low, invented): not used
// FUNCTION: D3DREN 0x1001e910
void d3d_BuildSpecularLookupTexture(float fPower)
{
	uint32 pixels[64 * 64];

	if (g_fSpecularTexturePower != fPower)
	{
		g_fSpecularTexturePower = fPower;
		if (g_pSpecularTexture)
		{
			g_pSpecularTexture->Release();
			g_pSpecularTexture = 0;
		}
		memset(pixels, 0, sizeof(pixels));
		for (uint32 i = 0; i < 64; i++)
		{
			uint32 v = (uint32)((float)pow((float)i * (1.0f / 64.0f), fPower) * 255.0f);
			uint32 c = (((v << 8) | v) << 8) | v;
			pixels[i * 65] = c;
			if (i > 0)
			{
				pixels[i * 65 - 64] = c;
				pixels[i * 65 - 1] = c;
			}
		}
		g_pSpecularTexture = d3d_CreateTextureFromPixels(pixels, 64, 64, 256);
	}
}

// Creates a texture surface (32 bit, else 4444 format) from a block of ARGB pixels, converted by the format manager.
// NAME: names_proposal.csv guess_CreateTextureFromPixels (low, invented): not used
// FUNCTION: D3DREN 0x1001e9f0
IDirectDrawSurface7 *d3d_CreateTextureFromPixels(uint32 *pPixels, uint32 width, uint32 height, uint32 pitch)
{
	FMConvertRequest cRequest;
	TextureFormat *pFormat = g_TextureFormats[FORMAT_32BIT];
	if (!pFormat)
		pFormat = g_TextureFormats[FORMAT_4444];
	if (pFormat)
	{
		DDSURFACEDESC2 ddsd;
		DDSURFACEDESC2 ddsdLock;
		IDirectDrawSurface7 *pSurface;

		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
		ddsd.ddsCaps.dwCaps2 = 0x40090;
		ddsd.dwWidth = width;
		ddsd.dwHeight = height;
		ddsd.ddpfPixelFormat = pFormat->m_PF;
		if (g_pDD->CreateSurface(&ddsd, &pSurface, 0) == 0)
		{
			memset(&ddsdLock, 0, sizeof(ddsdLock));
			ddsdLock.dwSize = sizeof(ddsdLock);
			if (pSurface->Lock(0, &ddsdLock, 0, 0) == 0)
			{
				cRequest.m_pSrcFormat->InitPValueFormat();
				cRequest.m_pSrc = (uint8 *)pPixels;
				cRequest.m_SrcPitch = pitch;
				DDPFToPFormat(&pFormat->m_PF, cRequest.m_pDestFormat);
				cRequest.m_pDest = (uint8 *)ddsdLock.lpSurface;
				cRequest.m_DestPitch = ddsdLock.lPitch;
				cRequest.m_Width = width;
				cRequest.m_Height = height;
				g_FormatMgr.ConvertPixels(&cRequest);
				pSurface->Unlock(0);
				return pSurface;
			}
			pSurface->Release();
		}
	}
	return 0;
}

// Re-creates the lightmap texture pools and the 0x20x0x20 lightmap format dummy surface.
// NAME: names_proposal.csv guess_ReinitLightmapTextureSupport (low, invented): not used
// FUNCTION: D3DREN 0x1001eb80
void d3d_ReinitLightmapTextureSupport()
{
	TextureFormat *pFormat;
	DDSURFACEDESC2 ddsd;

	if (DAT_10062878)
	{
		DAT_10062878->Release();
		DAT_10062878 = 0;
	}
	DAT_1007abe4.FUN_10034e61();
	DAT_1007abe4.FUN_10034db8(FUN_1001e750);
	if (DAT_10062878)
	{
		DAT_10062878->Release();
		DAT_10062878 = 0;
	}
	if ((g_b32BitLightmaps && (pFormat = g_TextureFormats[FORMAT_32BIT])) || (pFormat = g_TextureFormats[FORMAT_LIGHTMAP]))
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwHeight = 0x20;
		ddsd.dwWidth = 0x20;
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		ddsd.ddpfPixelFormat = pFormat->m_PF;
		g_pDD->CreateSurface(&ddsd, &DAT_10062878, 0);
	}
}

// Texture manager Init: clears the tables and lists, creates the RTexture bank, enumerates the device's texture formats and picks one for
// every FORMAT_* use; prints "FORMAT_x texture format missing." and fails when a required one is missing.  (The Jupiter CTextureManager::
// Init + SelectTextureFormats; the lightmap texture support reset of d3d_ReinitLightmapTextureSupport is inlined at its end.)
// NAME: names_proposal.csv CTextureManager::Init (medium, Jupiter): a global function in d3d.ren
// NOT MATCHING (1216 vs 1152 bytes): all seven wanted-format tables are initialized before selection, and FUN_1001f600 is kept
// out of line as in the exe.  The remaining difference is register and stack-slot selection: the exe reuses table storage for
// the later surface description and writes repeated values from registers, while this compilation uses a larger frame and some
// immediate stores.
// STUB: D3DREN 0x1001ec50
int CTextureManager_Init()
{
	memset(g_TextureFormats, 0, sizeof(g_TextureFormats));
	g_pBoundTextures[0] = 0;
	g_pBoundTextures[1] = 0;
	DAT_100613a8.m_nElements = 0;
	g_pBoundTextures[2] = 0;
	g_pBoundTextures[3] = 0;
	g_TextureFormatList.TieOff();
	DAT_100613a8.m_Head.TieOff();
	g_Textures.TieOff();
	DAT_1007abe4.FUN_10034e3d();
	g_RTextureBank.Init(0x40, 0);
	g_bTextureManagerInitialized = 1;
	g_pD3DDevice->EnumTextureFormats(d3d_EnumTextureFormatsCallback, 0);

	// The wanted formats (bits of red, green, blue, alpha; one of these DDPF_ flags; none of these) in the order of preference.
	TextureFormatSpec spec32[1] = { { 8, 8, 8, 8, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specBump[1] = { { 8, 8, 0, 0, DDPF_BUMPDUDV, DDPF_LUMINANCE } };
	TextureFormatSpec spec4444[2] = { { 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE }, { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specInterface[2] = { { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE | DDPF_PALETTEINDEXED8 },
		{ 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE | DDPF_PALETTEINDEXED8 } };
	TextureFormatSpec specLightmap[2] = { { 5, 5, 5, 0, DDPF_RGB, DDPF_LUMINANCE }, { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specFullbrite[2] = { { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE }, { 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specNormal[3] = { { 5, 6, 5, 0, DDPF_RGB, DDPF_LUMINANCE }, { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE },
		{ 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };

	g_TextureFormats[FORMAT_32BIT] = FUN_1001f590(spec32, 1);
	g_TextureFormats[FORMAT_FULLBRITE] = FUN_1001f590(specFullbrite, 2);
	if (!g_TextureFormats[FORMAT_FULLBRITE])
	{
		AddDebugMessage(0, "FORMAT_FULLBRITE texture format missing.");
		return 0;
	}
	g_TextureFormats[FORMAT_4444] = FUN_1001f590(spec4444, 2);
	if (!g_TextureFormats[FORMAT_4444])
	{
		AddDebugMessage(0, "FORMAT_4444 texture format missing.");
		return 0;
	}
	g_TextureFormats[FORMAT_NORMAL] = FUN_1001f590(specNormal, 3);
	if (!g_TextureFormats[FORMAT_NORMAL])
	{
		AddDebugMessage(0, "FORMAT_NORMAL texture format missing.");
		return 0;
	}
	g_TextureFormats[FORMAT_INTERFACE] = FUN_1001f590(specInterface, 2);
	if (!g_TextureFormats[FORMAT_INTERFACE])
	{
		AddDebugMessage(0, "FORMAT_INTERFACE texture format missing.");
		return 0;
	}
	g_TextureFormats[FORMAT_LIGHTMAP] = FUN_1001f590(specLightmap, 2);
	if (!g_TextureFormats[FORMAT_LIGHTMAP])
	{
		AddDebugMessage(0, "Warning: device not lightmap capable.");
		DAT_1005de20 = 0;
	}
	g_TextureFormats[FORMAT_BUMPMAP] = FUN_1001f590(specBump, 1);
	FUN_1001f600();
	{
		TextureFormat *pLightmapFormat;
		DDSURFACEDESC2 ddsd;

		if (DAT_10062878)
		{
			DAT_10062878->Release();
			DAT_10062878 = 0;
		}
		DAT_1007abe4.FUN_10034e61();
		DAT_1007abe4.FUN_10034db8(FUN_1001e750);
		if (DAT_10062878)
		{
			DAT_10062878->Release();
			DAT_10062878 = 0;
		}
		if ((g_b32BitLightmaps && (pLightmapFormat = g_TextureFormats[FORMAT_32BIT])) || (pLightmapFormat = g_TextureFormats[FORMAT_LIGHTMAP]))
		{
			memset(&ddsd, 0, sizeof(ddsd));
			ddsd.dwHeight = 0x20;
			ddsd.dwWidth = 0x20;
			ddsd.dwSize = sizeof(ddsd);
			ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
			ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
			ddsd.ddpfPixelFormat = pLightmapFormat->m_PF;
			g_pDD->CreateSurface(&ddsd, &DAT_10062878, 0);
		}
	}
	FUN_10032a30();
	FUN_1001f670();
	d3d_BuildSpecularLookupTexture(5.0f);
	return 1;
}

// The bit range of a colour mask: *pEnd = index just above the run of set bits, *pStart = index of the lowest set bit.
// NAME: names_proposal.csv guess_GetMaskBitRange (low, invented): not used
// An inline function in the original: the callback below calls the copy that follows it (0x1001f540) from its first sites and
// expands it in the later ones.
inline void FUN_1001f540(uint32 mask, uint32 *pEnd, uint32 *pStart)
{
	*pStart = 0;
	uint32 bit = 1;
	for (uint32 i = 0; i < 32; i++)
	{
		if (mask & bit)
			break;
		bit <<= 1;
		(*pStart)++;
	}
	*pEnd = *pStart;
	for (uint32 j = 0; j < 32; j++)
	{
		if (!(mask & bit))
			return;
		bit <<= 1;
		(*pEnd)++;
	}
}

// The shifts that convert a colour channel with the mask refMask into the channel of a format with the mask `mask`:
// *pRight bits to the right when the reference reaches higher, else *pLeft bits to the left.  (Inlined at its ten uses in the
// callback below.)
static void FUN_CalcShift(uint32 refMask, uint32 mask, int *pRight, int *pLeft)
{
	uint32 refEnd, refStart, end, start;

	FUN_1001f540(refMask, &refEnd, &refStart);
	FUN_1001f540(mask, &end, &start);
	if (refEnd > end)
	{
		*pRight = refEnd - end;
		*pLeft = 0;
	}
	else
	{
		*pRight = 0;
		*pLeft = end - refEnd;
	}
}



// IDirect3DDevice7::EnumTextureFormats callback: records every enumerated pixel format in the list g_TextureFormatList with its bit counts
// and the channel shifts for the conversion routines.
// NAME: d3d_EnumTextureFormatsCallback: names_proposal.csv (medium, LPD3DENUMPIXELFORMATSCALLBACK shape)
// NOT MATCHING (1152 vs 1136 bytes): same statements and order as the exe.  FUN_1001f540 (the bit range of a mask; its out-of-line copy
// follows this function at 0x1001f540, so it is an inline function in the original) is called at the first seven of the ten
// shift calculations (two calls each) in the exe and the same in ours up to there, but ours expands FUN_1001f540 from the seventh site
// on, while the exe expands only the constant-mask call at some later sites (the 0x7c00 and 0x3e0 ones) and keeps calling for the
// variable-mask one.  With a free inline call after the 7th site and one after the 10th the first seven sites are byte-identical to the
// exe, registers included (pNode in ebx, pFormat in ebp; without them ours swaps the two), and the size is the exe's; no placement of
// pending calls or ballast reproduces the exe's pattern of expansions after that (about 40 placements and counts tried), so the
// original nests the shift helper differently from FUN_CalcShift (inline, two FUN_1001f540 calls).
// STUB: D3DREN 0x1001f0d0
HRESULT WINAPI d3d_EnumTextureFormatsCallback(LPDDPIXELFORMAT pFormat, LPVOID pContext)
{
	TextureFormat *pNode = (TextureFormat *)dalloc(sizeof(TextureFormat));
	if (pNode)
	{
		pNode->m_PF = *pFormat;
		if (pNode->m_PF.dwFlags & DDPF_PALETTEINDEXED8)
		{
			pNode->m_BytesPPShift = 0;
		}
		else
		{
			if (pNode->m_PF.dwRGBBitCount == 16)
				pNode->m_BytesPPShift = 1;
			else if (pNode->m_PF.dwRGBBitCount == 32)
				pNode->m_BytesPPShift = 2;
		}
		pNode->m_BytesPP = 1 << pNode->m_BytesPPShift;
		pNode->m_RBits = (uint16)FUN_10012edf(pFormat->dwRBitMask);
		pNode->m_GBits = (uint16)FUN_10012edf(pFormat->dwGBitMask);
		pNode->m_BBits = (uint16)FUN_10012edf(pFormat->dwBBitMask);
		pNode->m_ABits = (uint16)FUN_10012edf(pFormat->dwRGBAlphaBitMask);

		FUN_CalcShift(0xff, pFormat->dwRBitMask, &pNode->m_Shifts[0], &pNode->m_Shifts[1]);
		FUN_CalcShift(0xff, pFormat->dwGBitMask, &pNode->m_Shifts[2], &pNode->m_Shifts[3]);
		FUN_CalcShift(0xff, pFormat->dwBBitMask, &pNode->m_Shifts[4], &pNode->m_Shifts[5]);
		FUN_CalcShift(0xff, pFormat->dwRGBAlphaBitMask, &pNode->m_Shifts[6], &pNode->m_Shifts[7]);
		FUN_CalcShift(0xf800, pFormat->dwRBitMask, &pNode->m_Shifts[8], &pNode->m_Shifts[9]);
		FUN_CalcShift(0x7e0, pFormat->dwGBitMask, &pNode->m_Shifts[10], &pNode->m_Shifts[11]);
		FUN_CalcShift(0x1f, pFormat->dwBBitMask, &pNode->m_Shifts[12], &pNode->m_Shifts[13]);
		FUN_CalcShift(0x7c00, pFormat->dwRBitMask, &pNode->m_Shifts[14], &pNode->m_Shifts[15]);
		FUN_CalcShift(0x3e0, pFormat->dwGBitMask, &pNode->m_Shifts[16], &pNode->m_Shifts[17]);
		FUN_CalcShift(0x1f, pFormat->dwBBitMask, &pNode->m_Shifts[18], &pNode->m_Shifts[19]);

		pNode->m_rgbaMask[0] = (uint16)pFormat->dwRBitMask;
		pNode->m_rgbaMask[1] = (uint16)pFormat->dwGBitMask;
		pNode->m_rgbaMask[2] = (uint16)pFormat->dwBBitMask;
		pNode->m_rgbaMask[3] = (uint16)pFormat->dwRGBAlphaBitMask;
		pNode->m_b1555 = pFormat->dwRBitMask == 0x7c00 && pFormat->dwGBitMask == 0x3e0 && pFormat->dwBBitMask == 0x1f &&
			pFormat->dwRGBAlphaBitMask == 0x8000;
		pNode->m_b565 = pFormat->dwRBitMask == 0xf800 && pFormat->dwGBitMask == 0x7e0 && pFormat->dwBBitMask == 0x1f &&
			pFormat->dwRGBAlphaBitMask == 0;
		pNode->m_Link.m_pData = pNode;
		dl_Insert(&g_TextureFormatList, &pNode->m_Link);
		return D3DENUMRET_OK;
	}
	return D3DENUMRET_OK;
}

// FUNCTION: D3DREN 0x1001f540 ?FUN_1001f540@@YAXKPAK0@Z

// Finds the first enumerated texture format that matches one of the nSpecs wanted formats (bits per colour channel, flags).
// NAME: names_proposal.csv guess_FindTextureFormat (low, invented): not used
// FUNCTION: D3DREN 0x1001f590
TextureFormat *FUN_1001f590(const TextureFormatSpec *pSpecs, uint32 nSpecs)
{
	for (uint32 i = 0; i < nSpecs; i++)
	{
		for (LTLink *pCur = g_TextureFormatList.m_pNext; pCur != &g_TextureFormatList; pCur = pCur->m_pNext)
		{
			TextureFormat *pFormat = (TextureFormat *)pCur->m_pData;
			if (pFormat->m_RBits == pSpecs[i].m_nBitsR && pFormat->m_GBits == pSpecs[i].m_nBitsG &&
				pFormat->m_BBits == pSpecs[i].m_nBitsB &&
				(pSpecs[i].m_dwFlagsAny & pFormat->m_PF.dwFlags) != 0 &&
				(pSpecs[i].m_dwFlagsNone & pFormat->m_PF.dwFlags) == 0)
			{
				return pFormat;
			}
		}
	}
	return 0;
}

// Sets the DXT1/3/5 support flags from the enumerated FOURCC formats.
// NAME: names_proposal.csv guess_DetectS3TCSupport (low, invented): not used
// FUNCTION: D3DREN 0x1001f600
#pragma auto_inline(off)
void FUN_1001f600()
{
	DAT_1006284c = 0;
	DAT_10062850 = 0;
	DAT_10062854 = 0;
	for (LTLink *pCur = g_TextureFormatList.m_pNext; pCur != &g_TextureFormatList; pCur = pCur->m_pNext)
	{
		TextureFormat *pFormat = (TextureFormat *)pCur->m_pData;
		if (pFormat->m_PF.dwFlags & DDPF_FOURCC)
		{
			if (pFormat->m_PF.dwFourCC == 0x31545844)
				DAT_10062854 = 1;
			else if (pFormat->m_PF.dwFourCC == 0x33545844)
				DAT_10062850 = 1;
			else if (pFormat->m_PF.dwFourCC == 0x35545844)
				DAT_1006284c = 1;
		}
	}
}
#pragma auto_inline(on)

// Builds the 16x16 shadow blob texture: white with an alpha of 1 - (distance from the centre / 7.5)^3.
// NAME: names_proposal.csv guess_BuildShadowBlobTexture (low, invented): not used
// FUNCTION: D3DREN 0x1001f670
void FUN_1001f670()
{
	FMConvertRequest cRequest;
	uint32 pixels[16 * 16];
	int x, y;

	if (g_pShadowBlobTexture)
	{
		g_pShadowBlobTexture->Release();
		g_pShadowBlobTexture = 0;
	}
	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x++)
		{
			float dx = (float)x - 7.5f;
			float dy = (float)y - 7.5f;
			float fDist = (float)sqrt(dx * dx + dy * dy);
			if (fDist > 7.5f)
				fDist = 7.5f;
			pixels[y * 16 + x] = ((uint32)(uint8)(uint32)((1.0f - (float)pow(fDist * 0.13333334f, DAT_1004b5d8)) * 255.9f) << 24) | 0xffffff;
		}
	}
	g_pShadowBlobTexture = d3d_CreateTextureFromPixels(pixels, 16, 16, 64);
}

// NAME: names_proposal.csv CTextureManager::Term (medium, Jupiter): a global function in d3d.ren
// FUNCTION: D3DREN 0x1001f770
void d3d_TermTextureManager()
{
	if (g_bTextureManagerInitialized)
	{
		d3d_FreeAllTextures();
		g_RTextureBank.Term();
		if (g_pShadowBlobTexture)
		{
			g_pShadowBlobTexture->Release();
			g_pShadowBlobTexture = 0;
		}
		if (g_pSpecularTexture)
		{
			g_pSpecularTexture->Release();
			g_pSpecularTexture = 0;
		}
		LTLink *pFormats = &g_TextureFormatList;
		LTLink *pCur = pFormats->m_pNext;
		while (pCur != pFormats)
		{
			LTLink *pNext = pCur->m_pNext;
			dfree(pCur->m_pData);
			pCur = pNext;
		}
		pFormats->TieOff();
		memset(g_TextureFormats, 0, sizeof(g_TextureFormats));
		g_bTextureManagerInitialized = 0;
	}
}

// Frees an RTexture: the chain of its other stages, the SharedTexture link, the surface, the g_Textures link, then the bank.
// NAME: names_proposal.csv CTextureManager::FreeTexture (medium, Jupiter): a global function in d3d.ren
// FUNCTION: D3DREN 0x1001f850
void CTextureManager_FreeTexture(RTexture *pTexture, int bChained)
{
	if (pTexture->m_Unk49 & 1)
	{
		if (!bChained)
			return;
	}
	else
	{
		RTexture *pChild = pTexture->m_Unk30;
		while (pChild)
		{
			RTexture *pNext = pChild->m_Unk30;
			CTextureManager_FreeTexture(pChild, 1);
			pChild = pNext;
		}
	}

	if (pTexture->m_pSharedTexture)
	{
		pTexture->m_pSharedTexture->m_pRenderData = 0;
		pTexture->m_pSharedTexture = 0;
	}

	RENDERSTRUCT_TEXMEM(g_pStruct) -= pTexture->m_Data.m_nMemory;

	for (int i = 0; i < 2; i++)
	{
		if (pTexture == (RTexture *)g_pBoundTextures[i])
			g_pBoundTextures[i] = 0;
	}

	if (pTexture->m_Data.m_pSurface)
		pTexture->m_Data.m_pSurface->Release();

	if (pTexture->m_Link.m_pData)
		pTexture->m_Link.Remove();

	g_RTextureBank.Free(pTexture);
}

// FreeTexture on every RTexture of an LTLink list (the head's m_pData is not used), then ties the head off.
// NAME: names_proposal.csv guess_FreeRTextureList (low, invented): not used
// FUNCTION: D3DREN 0x1001f920
void FUN_1001f920(LTLink *pList)
{
	LTLink *pCur = pList->m_pNext;
	while (pCur != pList)
	{
		LTLink *pNext = pCur->m_pNext;
		CTextureManager_FreeTexture((RTexture *)pCur->m_pData, 0);
		pCur = pNext;
	}
	pList->TieOff();
}

// NAME: names_proposal.csv CTextureManager::FreeAllTextures (medium, Jupiter): a global function in d3d.ren
// FUNCTION: D3DREN 0x1001f960
void d3d_FreeAllTextures()
{
	LTLink *pListHead = &g_Textures;
	LTLink *pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		LTLink *pNext = pCur->m_pNext;
		CTextureManager_FreeTexture((RTexture *)pCur->m_pData, 0);
		pCur = pNext;
	}
	pListHead->TieOff();
}

// NAME: names_proposal.csv CTextureManager::ListTextureFormats (medium, Jupiter): a global function in d3d.ren
// FUNCTION: D3DREN 0x1001f9b0
void d3d_ListTextureFormats()
{
	for (LTLink *pCur = g_TextureFormatList.m_pNext; pCur != &g_TextureFormatList; pCur = pCur->m_pNext)
		d3d_PrintFormatInfo("", (TextureFormat *)pCur->m_pData);
	d3d_PrintFormatInfo("[FULLBRITE] - ", g_TextureFormats[FORMAT_FULLBRITE]);
	d3d_PrintFormatInfo("[4444] - ", g_TextureFormats[FORMAT_4444]);
	d3d_PrintFormatInfo("[NORMAL] - ", g_TextureFormats[FORMAT_NORMAL]);
	d3d_PrintFormatInfo("[INTERFACE] - ", g_TextureFormats[FORMAT_INTERFACE]);
	d3d_PrintFormatInfo("[LIGHTMAP] - ", g_TextureFormats[FORMAT_LIGHTMAP]);
}

// d3d_AddToString (0x1001ff80, defined below the printer): appends the text and a space to pStr and returns the end of the string.
char *d3d_AddToString(char *pStr, const char *pToAdd);

// Prints one texture format (its DDPF_ name, bit counts and masks) through RenderStruct::ConsolePrint, or "<pStart>NONE" for a
// missing one.
// NAME: d3d_PrintFormatInfo: names_proposal.csv (medium; Jupiter d3d_PrintFormatInfo(pStart, format))
// NOT MATCHING (576 vs 1344 bytes): d3d_AddToString is an inline function in the original (its out-of-line copy follows this
// function in the exe): the exe expands it in the first ten branches of the flag chain (each expansion is the two inline strcats, no
// tail merging) and calls the out-of-line copy from the other nine.  Written as `inline`, our build expands all nineteen sites and
// merges the identical bodies (also 576 bytes, and no out-of-line copy is emitted); with a code-free cost of 7 units added to the
// inline body the same ten sites expand, but the count is then one site off: the exe also keeps the second `flags & DDPF_FOURCC` test
// of the chain (the 7th branch, the "DDPF_FOURCC" text, dead after the sprintf branch) while ours folds it as dead, so ours expands
// the first ten live sites.  The spelling of the chain that keeps the dead test was not found (tried: a local copy of the flags,
// direct member reads, a reference to the pixel format, the goto form, `== DDPF_FOURCC`).  The exe's local buffers are two
// 256 byte arrays (frame 0x200), the source here has them.  d3d_AddToString is therefore written non-inline below so that its code
// (which is byte-identical) is checked.
// STUB: D3DREN 0x1001fa40
void d3d_PrintFormatInfo(const char *pStart, TextureFormat *pFormat)
{
	char spec[256];
	char fourCC[256];

	if (!pFormat)
	{
		g_pStruct->ConsolePrint("%sNONE", pStart);
		return;
	}
	if (pFormat->m_PF.dwFlags & DDPF_FOURCC)
	{
		*(uint32 *)fourCC = pFormat->m_PF.dwFourCC;
		fourCC[4] = 0;
		sprintf(spec, "FOURCC( %s )", fourCC);
	}
	else
	{
		spec[0] = 0;
		if (pFormat->m_PF.dwFlags & DDPF_ALPHA)
			d3d_AddToString(spec, "DDPF_ALPHA");
		else if (pFormat->m_PF.dwFlags & DDPF_ALPHAPIXELS)
			d3d_AddToString(spec, "DDPF_ALPHAPIXELS");
		else if (pFormat->m_PF.dwFlags & DDPF_ALPHAPREMULT)
			d3d_AddToString(spec, "DDPF_ALPHAPREMULT");
		else if (pFormat->m_PF.dwFlags & DDPF_BUMPLUMINANCE)
			d3d_AddToString(spec, "DDPF_BUMPLUMINANCE");
		else if (pFormat->m_PF.dwFlags & DDPF_BUMPDUDV)
			d3d_AddToString(spec, "DDPF_BUMPDUDV");
		else if (pFormat->m_PF.dwFlags & DDPF_COMPRESSED)
			d3d_AddToString(spec, "DDPF_COMPRESSED");
		else if (pFormat->m_PF.dwFlags & DDPF_FOURCC)
			d3d_AddToString(spec, "DDPF_FOURCC");
		else if (pFormat->m_PF.dwFlags & DDPF_LUMINANCE)
			d3d_AddToString(spec, "DDPF_LUMINANCE");
		else if (pFormat->m_PF.dwFlags & DDPF_PALETTEINDEXED1)
			d3d_AddToString(spec, "DDPF_PALETTEINDEXED1");
		else if (pFormat->m_PF.dwFlags & DDPF_PALETTEINDEXED2)
			d3d_AddToString(spec, "DDPF_PALETTEINDEXED2");
		else if (pFormat->m_PF.dwFlags & DDPF_PALETTEINDEXED4)
			d3d_AddToString(spec, "DDPF_PALETTEINDEXED4");
		else if (pFormat->m_PF.dwFlags & DDPF_PALETTEINDEXED8)
			d3d_AddToString(spec, "DDPF_PALETTEINDEXED8");
		else if (pFormat->m_PF.dwFlags & DDPF_PALETTEINDEXEDTO8)
			d3d_AddToString(spec, "DDPF_PALETTEINDEXEDTO8");
		else if (pFormat->m_PF.dwFlags & DDPF_RGB)
			d3d_AddToString(spec, "DDPF_RGB");
		else if (pFormat->m_PF.dwFlags & DDPF_RGBTOYUV)
			d3d_AddToString(spec, "DDPF_RGBTOYUV");
		else if (pFormat->m_PF.dwFlags & DDPF_STENCILBUFFER)
			d3d_AddToString(spec, "DDPF_STENCILBUFFER");
		else if (pFormat->m_PF.dwFlags & DDPF_ZBUFFER)
			d3d_AddToString(spec, "DDPF_ZBUFFER");
		else if (pFormat->m_PF.dwFlags & DDPF_ZPIXELS)
			d3d_AddToString(spec, "DDPF_ZPIXELS");
		else
			d3d_AddToString(spec, "UNKNOWN");
	}
	g_pStruct->ConsolePrint("%s%s - %d bits (%d %d %d %d) (%x %x %x %x)", pStart, spec, pFormat->m_PF.dwRGBBitCount,
		pFormat->m_RBits, pFormat->m_GBits, pFormat->m_BBits, pFormat->m_ABits,
		pFormat->m_PF.dwRBitMask, pFormat->m_PF.dwGBitMask, pFormat->m_PF.dwBBitMask, pFormat->m_PF.dwRGBAlphaBitMask);
}

// NAME: d3d_AddToString: names_proposal.csv (high; Jupiter d3d_AddToString(pStr, pToAdd, nBufferLen), here without the length).
// An inline function in the original: this is its out-of-line copy (see the printer above); the code is identical either way.
// FUNCTION: D3DREN 0x1001ff80
char *d3d_AddToString(char *pStr, const char *pToAdd)
{
	strcat(pStr, pToAdd);
	strcat(pStr, " ");
	return pStr + strlen(pStr);
}

// Creates the RTexture of a SharedTexture for the device stage nStageFlags (stage in the low byte, 0x100 = bump map stage): picks the
// format from the DTX flags, the first mipmap and the number of mipmaps from the header, builds the surface (FUN_10020ab0) and the
// RTexture, links it into g_Textures and uploads the mipmaps (r_TransferTexture).  Returns 0 on failure.  The older of the two
// creation functions of the object (10021290 does the same with everything expanded in place).
// NAME: names_proposal.csv d3d_CreateAndLoadTexture (medium, Jupiter d3d_texture.cpp)
// NOT MATCHING (944 vs 832 bytes): the function is the same code as the exe's (the format choice, the mipmap range, the detail
// scale/angle, the memory count, the FreeTexture exit all line up) but the exe calls four tiny functions out of line that we
// expand: RTexture's bank allocation (ObjectBank<RTexture>::AllocVoid, 0x10021c80, called through `mov ecx,&bank`), the implicit
// UnkType_RTextureData assignment (0x10020fb0, called with the temporary), CheapLTLink::AddAfter (0x10020330, called on g_Textures)
// and the implicit UnkType_RTextureData destructor (0x10020350, called on every exit path: the stack object is a block-scope
// local).  With `#pragma inline_depth(0)` around the function the dtor and AddAfter copies come out byte-identical (0x10020330 and
// 0x10020350 MATCH) but then the RTextureData constructor is called too, which the exe inlines (a vptr store), so no single pragma
// reproduces the exe; the later creation function (10021290) expands AddAfter and the bank allocation in place.  The out-of-line
// copies are therefore a budget effect of this function that has not been found (tried: 24..300 units of code-free ballast
// at the top; they change the size non-monotonically).  The three copies (0x10020330, 0x10020350, 0x10020fb0) are emitted and
// verified through the STANDIN below, which calls them with inline_depth(0).
// STUB: D3DREN 0x1001fff0
RTexture *d3d_CreateAndLoadTexture(SharedTexture *pSharedTexture, uint32 nStageFlags, uint8 bAdditional)
{
	RTexture *pRTexture = 0;
	Counter cCount1(0);
	Counter cCount2(0);
	uint32 dwDummy;
	TextureData *pTextureData = FUN_GetTextureData(pSharedTexture, &dwDummy);
	if (!pTextureData)
		return 0;

	UnkType_RTextureBuild build;
	UnkType_RTextureData data;
	int iFormat;
	int iStartMipmap, nMipmaps, nAvailable, i;

	build.m_pSharedTexture = pSharedTexture;
	build.m_pTextureData = pTextureData;
	build.m_nFlags = nStageFlags;

	if (nStageFlags & 0x100)
	{
		if (!g_TextureFormats[FORMAT_BUMPMAP])
			goto done;
		iFormat = FORMAT_BUMPMAP;
	}
	else
	{
		uint32 dtxFlags = pTextureData->m_Flags;
		if (!(dtxFlags & DTX_PREFER16BIT) && g_32BitTextures && g_TextureFormats[FORMAT_32BIT])
			iFormat = FORMAT_32BIT;
		else if (dtxFlags & DTX_PREFER5551)
			iFormat = FORMAT_FULLBRITE;
		else if (dtxFlags & DTX_PREFER4444)
			iFormat = FORMAT_4444;
		else
			iFormat = ((~dtxFlags & DTX_FULLBRITE) << 1) | 1;
	}

	memset(&data, 0, sizeof(data));

	{
		int iGroup = pTextureData->m_Header.m_Extra[0];
		if (iGroup > 9)
			iGroup = 9;
		iStartMipmap = (&g_GroupOffset0)[iGroup] + pTextureData->m_Header.m_Extra[4] + g_MipmapOffset;
		if (g_CV_S3TCEnable.m_IntVal == 0)
			iStartMipmap += pTextureData->m_Header.m_Extra[3];
		if (iStartMipmap < 0)
			iStartMipmap = 0;
		else if (iStartMipmap > 3)
			iStartMipmap = 3;
	}
	if (iStartMipmap > (int)pTextureData->m_Header.m_nMipmaps - 1)
		iStartMipmap = pTextureData->m_Header.m_nMipmaps - 1;

	{
		int iFirstUsable = d3d_GetFirstUsableMipmap(pTextureData);
		if (iFirstUsable == -1)
			goto done;
		if (iStartMipmap <= iFirstUsable)
			iStartMipmap = iFirstUsable;
	}

	nMipmaps = pTextureData->m_Header.m_Extra[1];
	if (nMipmaps == 0)
		nMipmaps = 4;
	nAvailable = pTextureData->m_Header.m_nMipmaps - iStartMipmap;
	if (nAvailable == 0)
		goto done;
	if (nMipmaps < 1)
		nMipmaps = 1;
	else if (nMipmaps > nAvailable)
		nMipmaps = nAvailable;

	if (!FUN_10020ab0(&build, &data, iStartMipmap, nMipmaps, iFormat))
		goto done;

	pRTexture = (RTexture *)g_RTextureBank.AllocVoid();
	if (!pRTexture)
	{
		data.m_pSurface->Release();
		goto done;
	}

	pRTexture->m_Unk30 = 0;
	pRTexture->m_Unk49 = bAdditional;
	pRTexture->m_Unk42 = (uint8)build.m_nFlags;
	pRTexture->m_Data = data;
	pRTexture->m_Data.m_pOwner = pRTexture;
	pRTexture->m_iStartMipmap = iStartMipmap;
	pRTexture->m_Unk47 = nMipmaps;
	pRTexture->m_Unk48 = iFormat;
	pRTexture->m_DetailTextureScale = *(float *)&pTextureData->m_Header.m_Extra[6] + 1.0f;
	{
		float fAngle = (float)(int16)(pTextureData->m_Header.m_Extra[11] * 256 + pTextureData->m_Header.m_Extra[10]) * 0.017453292f;
		pRTexture->m_DetailTextureAngleC = (float)cos(fAngle);
		pRTexture->m_DetailTextureAngleS = (float)sin(fAngle);
	}
	pRTexture->m_Data.m_nMemory = 0;
	for (i = iStartMipmap; i < iStartMipmap + nMipmaps; i++)
		pRTexture->m_Data.m_nMemory += (pTextureData->m_Mips[i].m_Width * pTextureData->m_Mips[i].m_Height) << g_TextureFormats[iFormat]->m_BytesPPShift;
	pRTexture->m_pSharedTexture = build.m_pSharedTexture;
	if (!(bAdditional & 1))
		build.m_pSharedTexture->m_pRenderData = pRTexture;
	pRTexture->m_Flags = pTextureData->m_Header.m_IFlags & DTX_FULLBRITE;
	pRTexture->m_BaseWidth = pTextureData->m_Mips[0].m_Width;
	pRTexture->m_BaseHeight = pTextureData->m_Mips[0].m_Height;
	pRTexture->m_Link.m_pData = pRTexture;
	g_Textures.AddAfter(&pRTexture->m_Link);
	RENDERSTRUCT_TEXMEM(g_pStruct) += pRTexture->m_Data.m_nMemory;
	data.~UnkType_RTextureData();
	if (!r_TransferTexture(pRTexture, pTextureData))
	{
		AddDebugMessage(4, "Unable to transfer texture data to video memory.");
		CTextureManager_FreeTexture(pRTexture, 0);
		pRTexture = 0;
	}
done:
	g_pStruct->FreeTexture(pSharedTexture);
	return pRTexture;
}


// STANDIN: forces the out-of-line copies of three tiny functions that d3d_CreateAndLoadTexture calls out of line in the exe and expands in
// our build (CheapLTLink::AddAfter on g_Textures, the implicit UnkType_RTextureData destructor and its implicit assignment, the
// latter called with the stack temporary).  With inline_depth(0) the copies come out byte-identical to the exe's.  (not in d3d.ren)
// FUNCTION: D3DREN 0x10020330 ?AddAfter@CheapLTLink@@QAEXPAV1@@Z
// FUNCTION: D3DREN 0x10020350 ??1UnkType_RTextureData@@UAE@XZ
// FUNCTION: D3DREN 0x10020fb0 ??4UnkType_RTextureData@@QAEAAV0@ABV0@@Z
#pragma inline_depth(0)
void StandIn_RTextureInlines(LTLink *pLink, LTLink *pAfter, UnkType_RTextureData *pDst, UnkType_RTextureData *pSrc)
{
	UnkType_RTextureData local;
	pLink->AddAfter(pAfter);
	*pDst = *pSrc;
}
#pragma inline_depth()

// Copies the mipmaps of pTextureData that pTexture uses (m_iStartMipmap, m_Unk47 of them) into the surface chain of the texture:
// per mipmap the surface is locked and filled according to the TextureData's BPPIdent: 32 bit RGBA source converted with
// FormatMgr::ConvertPixels into the texture format; DXT1/3/5 copied (or, when the surface is not a FOURCC surface, copied into a
// temporary system memory DXT surface and BltFast'ed); for the bump map format (DDPF_BUMPDUDV, 16 bit) the 8 bit luminance of the
// source is turned into signed (du, dv) pairs.  A mipmap smaller than the surface level is tiled to fill it.  Returns 0 on failure.
// NAME: r_TransferTexture: Ghidra / names_proposal.csv (high; Jupiter d3d_texture.cpp r_TransferTexture)
// NOT MATCHING (first transcription of the whole function from the disassembly, the byte comparison has not been iterated):
// the code layout follows the exe's (the same locals, the BUMPDUDV / BPP_32 / DXT branch order, the common tiling tail).
// STUB: D3DREN 0x10020360
int r_TransferTexture(RTexture *pTexture, TextureData *pTextureData)
{
	FMConvertRequest cReq;
	DDSURFACEDESC2 ddsdSurf;
	DDSURFACEDESC2 ddsdLock;
	IDirectDrawSurface7 *pSurface;
	TextureFormat *pFormat;
	TextureMipData *pMip;
	uint32 bpp, i, surfWidth, surfHeight;

	if ((uint32)pTexture->m_Unk47 + pTexture->m_iStartMipmap > pTextureData->m_Header.m_nMipmaps)
	{
		AddDebugMessage(1, "r_TransferTexture: mipmap count doesn't match!");
		return 0;
	}

	bpp = pTextureData->m_Header.m_Extra[2];
	if (bpp == 0)
		bpp = BPP_32;

	pSurface = pTexture->m_Data.m_pSurface;
	pFormat = g_TextureFormats[pTexture->m_Unk48];
	ddsdSurf.dwSize = sizeof(DDSURFACEDESC2);
	ddsdSurf.dwFlags = DDSD_HEIGHT | DDSD_WIDTH;
	pSurface->GetSurfaceDesc(&ddsdSurf);
	surfWidth = ddsdSurf.dwWidth;
	surfHeight = ddsdSurf.dwHeight;
	AddDebugMessage(4, "Uploading a (%dx%d) texture", ddsdSurf.dwWidth, ddsdSurf.dwHeight);

	pMip = &pTextureData->m_Mips[pTexture->m_iStartMipmap];
	for (i = pTexture->m_iStartMipmap; i < (uint32)pTexture->m_iStartMipmap + pTexture->m_Unk47; i++, pMip++)
	{
		uint32 mipWidth, mipHeight;
		long pitch;
		uint8 *pBits;
		DDSCAPS2 caps;

		if (!pSurface)
			return 0;

		mipWidth = pMip->m_Width;
		mipHeight = pMip->m_Height;
		ddsdLock.dwSize = sizeof(DDSURFACEDESC2);
		if (pSurface->Lock(0, &ddsdLock, DDLOCK_WRITEONLY, 0) != 0)
			return 0;
		pitch = ddsdLock.lPitch;
		pBits = (uint8 *)ddsdLock.lpSurface;

		if (ddsdLock.ddpfPixelFormat.dwFlags & DDPF_BUMPDUDV)
		{
			FMConvertRequest cReqBump;
			uint8 *pTemp;
			uint8 *pDst;
			uint32 y, x;

			if (ddsdLock.ddpfPixelFormat.dwRGBBitCount != 16)
				goto Fail;
			pTemp = (uint8 *)operator new(pMip->m_Width * pMip->m_Height);
			if (!pTemp)
				goto Fail;
			pTextureData->SetupPFormat(cReqBump.m_pSrcFormat);
			cReqBump.m_pSrc = pMip->m_Data;
			cReqBump.m_SrcPitch = pMip->m_Pitch;
			cReqBump.m_pDestFormat->Init(BPP_8, 0xff, 0, 0, 0);
			cReqBump.m_Height = pMip->m_Height;
			cReqBump.m_pDest = pTemp;
			cReqBump.m_DestPitch = pMip->m_Width;
			cReqBump.m_Width = pMip->m_Width;
			if (g_FormatMgr.ConvertPixels(&cReqBump) != LT_OK)
			{
				operator delete(pTemp);
				goto Fail;
			}

			memset(pBits, 0, pMip->m_Width * 2);
			pDst = pBits;
			for (y = pMip->m_Height; y; y--)
			{
				*(uint16 *)pDst = 0;
				pDst += pitch;
			}
			pDst = pBits;
			for (y = 1; y < pMip->m_Height; y++)
			{
				uint8 *pSrc = pTemp + pMip->m_Width * y + 1;
				uint8 *pOut = pDst;

				for (x = 1; x < pMip->m_Width; x++)
				{
					uint32 cur = *pSrc;
					uint32 up = pSrc[-(int)pMip->m_Width];
					*pOut++ = (char)((int)(cur - pSrc[-1]) >> 1);
					*pOut++ = (char)((int)(cur - up) >> 1);
					pSrc++;
				}
				pDst += pitch;
			}
			operator delete(pTemp);
		}
		else if (bpp == BPP_32)
		{
			cReq.m_pSrcFormat->InitPValueFormat();
			cReq.m_pSrc = pMip->m_Data;
			cReq.m_SrcPitch = pMip->m_Pitch;
			DDPFToPFormat(&pFormat->m_PF, cReq.m_pDestFormat);
			cReq.m_pDest = pBits;
			cReq.m_DestPitch = pitch;
			cReq.m_Width = mipWidth;
			cReq.m_Height = mipHeight;
			cReq.m_Flags = 0;
			if (g_FormatMgr.ConvertPixels(&cReq) != LT_OK)
				goto Fail;
		}
		else
		{
			uint32 fourCC, size;

			if (bpp == BPP_S3TC_DXT1)
				fourCC = 0x31545844;
			else if (bpp == BPP_S3TC_DXT3)
				fourCC = 0x33545844;
			else if (bpp == BPP_S3TC_DXT5)
				fourCC = 0x35545844;
			else
				goto Fail;
			size = CalcImageSize((BPPIdent)bpp, pMip->m_Width, pMip->m_Height);

			if (ddsdSurf.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
			{
				if (fourCC != ddsdSurf.ddpfPixelFormat.dwFourCC || (long)size != ddsdLock.lPitch)
					goto Fail;
				memcpy(ddsdLock.lpSurface, pMip->m_Data, size);
			}
			else
			{
				IDirectDrawSurface7 *pTemp;
				DDSURFACEDESC2 ddsdTemp;
				DDSURFACEDESC2 ddsdTempLock;
				HRESULT hr;

				pSurface->Unlock(0);
				memset(&ddsdTemp, 0, sizeof(ddsdTemp));
				ddsdTemp.dwSize = sizeof(ddsdTemp);
				ddsdTemp.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
				ddsdTemp.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
				ddsdTemp.lpSurface = pMip->m_Data;
				ddsdTemp.dwHeight = pMip->m_Height;
				ddsdTemp.dwWidth = pMip->m_Width;
				ddsdTemp.lPitch = size;
				ddsdTemp.ddpfPixelFormat.dwFlags |= DDPF_FOURCC;
				ddsdTemp.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
				ddsdTemp.ddpfPixelFormat.dwFourCC = fourCC;
				if (g_pDD->CreateSurface(&ddsdTemp, &pTemp, 0) != 0)
					return 0;

				memset(&ddsdTempLock, 0, sizeof(ddsdTempLock));
				ddsdTempLock.dwSize = sizeof(ddsdTempLock);
				if (pTemp->Lock(0, &ddsdTempLock, DDLOCK_WAIT | DDLOCK_WRITEONLY, 0) != 0)
				{
					pTemp->Release();
					return 0;
				}
				if ((uint32)ddsdTempLock.lPitch != size)
				{
					pTemp->Unlock(0);
					pTemp->Release();
					return 0;
				}
				memcpy(ddsdTempLock.lpSurface, pMip->m_Data, size);
				pTemp->Unlock(0);
				hr = pSurface->BltFast(0, 0, pTemp, 0, DDBLTFAST_WAIT);
				pTemp->Release();
				if (hr != 0)
					return 0;

				ddsdLock.dwSize = sizeof(DDSURFACEDESC2);
				if (pSurface->Lock(0, &ddsdLock, DDLOCK_WRITEONLY, 0) != 0)
					return 0;
			}
		}

		if (bpp == BPP_32)
		{
			// tile the mipmap over the surface level when the level is larger than the mipmap (aspect ratio limit)
			uint32 nTilesX = surfWidth / mipWidth;
			uint32 nTilesY;
			int k;

			for (k = 1; k < (int)nTilesX; k++)
			{
				uint8 *pSrcRow = pBits;
				uint8 *pDstRow = pBits + pFormat->m_BytesPP * k * mipWidth;
				uint32 row;

				for (row = mipHeight; row; row--)
				{
					memcpy(pDstRow, pSrcRow, mipWidth << pFormat->m_BytesPPShift);
					pSrcRow += pitch;
					pDstRow += pitch;
				}
			}

			nTilesY = surfHeight / mipHeight;
			if ((int)nTilesY > 1)
			{
				uint32 nOffset = pitch * mipHeight;
				uint32 nNext = nOffset;
				uint32 nLeft = nTilesY - 1;

				do
				{
					uint8 *pSrcRow = pBits;
					uint8 *pDstRow = pBits + nNext;
					uint32 row;

					for (row = mipHeight; row; row--)
					{
						memcpy(pDstRow, pSrcRow, surfWidth << pFormat->m_BytesPPShift);
						pSrcRow += pitch;
						pDstRow += pitch;
					}
					nNext += nOffset;
				} while (--nLeft);
			}
		}

		pSurface->Unlock(0);
		caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
		caps.dwCaps2 = 0;
		caps.dwCaps3 = 0;
		caps.dwCaps4 = 0;
		pSurface->GetAttachedSurface(&caps, &pSurface);
		surfWidth >>= 1;
		surfHeight >>= 1;
	}
	return 1;

Fail:
	pSurface->Unlock(0);
	return 0;
}

static inline int InlineIsS3TCSupported(uint32 bpp)
{
	if (bpp == 4)
		return DAT_10062854;
	if (bpp == 5)
		return DAT_10062850;
	return bpp == 6 ? DAT_1006284c : 0;
}

// Creates the DirectDraw texture surface of an RTexture (iStartMipmap, nMipmaps and the format iFormat chosen by the caller) and fills
// in the UnkType_RTextureData pData with it (surface, 1/width and 1/height scaled by the texture's U/V shift, the AlphaRef of the DTX
// command string).  Handles the "ColorKey r g b" and "AlphaRef n" tokens of the command string, DXT formats and the aspect ratio
// limit.  The older generation of the code: the DXT / aspect ratio helpers are written out here, the newer CTextureManager_CreateRTexture calls
// CTextureManager_S3TCFormatConv / FUN_100219b0 / CTextureManager_IsS3TCFormatSupported instead.
// NAME: names_proposal.csv guess_CreateRTextureSurface (low, invented): not used
// NOT MATCHING (1072 vs 1136 bytes): the statements, their order and the stores are the exe's (flags 0x121007, caps 0x401008 /
// 0x40090, the DXT FOURCC branch, the ColorKey / AlphaRef parsing with the command string initialised again before each ParseFind,
// CreateSurface, SetPriority, 1/w and 1/h scaled by 1 << Extra[4]); what differs is the layout of the first branch and the register
// that follows from it: the exe falls through from `if (bpp == 0)` into the shared (non-DXT) path and keeps the DXT path behind a
// jne, with ebp = bpp; ours emits the DXT path first and jumps to the shared one.  Tried: `if (bpp == 0) bpp = 3; else if (...)`
// against two separate ifs, nesting the DXT test inside `if (bpp != 0)`.
// STUB: D3DREN 0x10020ab0
int FUN_10020ab0(UnkType_RTextureBuild *pBuild, UnkType_RTextureData *pData, uint32 iStartMipmap, uint32 nMipmaps, uint32 iFormat)
{
	TextureData *pTextureData = pBuild->m_pTextureData;
	DDSURFACEDESC2 ddsd;
	PFormat cFormat;
	ConParse cParse;
	uint32 bpp, width, height, uOutWidth, uOutHeight, colorValue;
	GenericColor colorOut;
	float fU, fV;
	int bSupported;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_TEXTURESTAGE;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
	ddsd.ddsCaps.dwCaps2 = 0x40090;
	ddsd.dwWidth = pTextureData->m_Mips[iStartMipmap].m_Width;
	ddsd.dwHeight = pTextureData->m_Mips[iStartMipmap].m_Height;
	ddsd.dwMipMapCount = nMipmaps;
	ddsd.dwTextureStage = pBuild->m_nFlags;

	bpp = pTextureData->m_Header.m_Extra[2];
	if (bpp == 0)
	{
		bpp = 3;
	}
	else if (bpp != 3 && g_CV_S3TCEnable.m_IntVal && InlineIsS3TCSupported(bpp))
	{
		bSupported = 1;
		memset(&ddsd.ddpfPixelFormat, 0, sizeof(ddsd.ddpfPixelFormat));
		ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
		ddsd.ddpfPixelFormat.dwFlags |= DDPF_FOURCC;
		if (bpp == 4)
			ddsd.ddpfPixelFormat.dwFourCC = 0x31545844;
		else if (bpp == 5)
			ddsd.ddpfPixelFormat.dwFourCC = 0x33545844;
		else if (bpp == 6)
			ddsd.ddpfPixelFormat.dwFourCC = 0x35545844;
		else
			return 0;
		goto ParseColorKey;
	}

	bSupported = 0;
	ddsd.ddpfPixelFormat = g_TextureFormats[iFormat]->m_PF;
	width = ddsd.dwWidth;
	height = ddsd.dwHeight;
	if (DAT_1005c984 & D3DPTEXTURECAPS_SQUAREONLY)
	{
		width = height = LTMAX(width, height);
	}
	else if (g_MaxTexAspectRatio > 0)
	{
		uint32 *pMin = width > height ? &height : &width;
		uint32 *pMax = width > height ? &width : &height;
		if ((int)(*pMax / *pMin) > g_MaxTexAspectRatio)
			*pMin = *pMax / (uint32)g_MaxTexAspectRatio;
	}
	ddsd.dwWidth = width;
	ddsd.dwHeight = height;

ParseColorKey:
	cParse.Init(pTextureData->m_Header.m_CommandString);
	if (cParse.ParseFind("ColorKey", 0, 3) && !g_CV_AlphaTest.m_IntVal)
	{
		uint32 r = atoi(cParse.m_Args[1]);
		uint32 g = atoi(cParse.m_Args[2]);
		uint32 b = atoi(cParse.m_Args[3]);
		colorValue = (((b << 8) | g) << 8) | r;
		if (bSupported)
			cFormat.InitPValueFormat();
		else
			DDPFToPFormat(&ddsd.ddpfPixelFormat, &cFormat);
		if (g_FormatMgr.PValueToFormatColor(&cFormat, colorValue, colorOut) == LT_OK)
		{
			ddsd.dwFlags |= DDSD_CKSRCBLT;
			ddsd.ddckCKSrcBlt.dwColorSpaceLowValue = colorOut.dwVal;
			ddsd.ddckCKSrcBlt.dwColorSpaceHighValue = colorOut.dwVal;
		}
	}

	cParse.Init(pTextureData->m_Header.m_CommandString);
	if (cParse.ParseFind("AlphaRef", 0, 1))
		pData->m_AlphaRef = atoi(cParse.m_Args[1]);
	else
		pData->m_AlphaRef = 0;

	if (g_pDD->CreateSurface(&ddsd, &pData->m_pSurface, 0) != 0)
	{
		AddDebugMessage(4, "Unable to create (%dx%d) texture surface.", ddsd.dwWidth, ddsd.dwHeight);
		return 0;
	}
	pData->m_pSurface->SetPriority(pTextureData->m_Header.m_Extra[5]);

	width = pTextureData->m_Mips[iStartMipmap].m_Width;
	height = pTextureData->m_Mips[iStartMipmap].m_Height;
	uOutWidth = width;
	uOutHeight = height;
	if (DAT_1005c984 & D3DPTEXTURECAPS_SQUAREONLY)
	{
		uOutWidth = uOutHeight = LTMAX(width, height);
	}
	else if (g_MaxTexAspectRatio > 0)
	{
		uint32 *pMin = width > height ? &height : &width;
		uint32 *pMax = width > height ? &width : &height;
		if ((int)(*pMax / *pMin) > g_MaxTexAspectRatio)
			*pMin = *pMax / (uint32)g_MaxTexAspectRatio;
		uOutWidth = width;
		uOutHeight = height;
	}
	fU = 1.0f / (float)uOutWidth;
	pData->m_Unk04 = fU;
	fV = 1.0f / (float)uOutHeight;
	pData->m_Unk08 = fV;
	pData->m_Unk04 = (float)(1 << pTextureData->m_Header.m_Extra[4]) * fU;
	pData->m_Unk08 = (float)(1 << pTextureData->m_Header.m_Extra[4]) * fV;
	return 1;
}


// NAME: d3d_GetFirstUsableMipmap: names_proposal.csv (high; Jupiter d3d_texture.cpp d3d_GetFirstUsableMipmap): the first mipmap that fits
// the largest texture the device and the MaxTextureSize variable allow (clamped to the screen unless larger surfaces are
// allowed), -1 when none does.  The height is clamped with the already clamped width (as the exe does).
// FUNCTION: D3DREN 0x10020f20
int d3d_GetFirstUsableMipmap(TextureData *pTexture)
{
	uint32 maxWidth = LTMIN((uint32)g_MaxTextureSize, DAT_1005c8fc);
	uint32 maxHeight = LTMIN((uint32)g_MaxTextureSize, DAT_1005c900);
	if (!maxWidth)
		maxWidth = 256;
	if (!maxHeight)
		maxHeight = 256;

	if (!DAT_1005c874)
	{
		maxWidth = LTMIN(maxWidth, g_ScreenWidth);
		maxHeight = LTMIN(maxWidth, g_ScreenHeight);
	}

	for (uint32 i = 0; i < pTexture->m_Header.m_nMipmaps; i++)
	{
		if (pTexture->m_Mips[i].m_Width <= maxWidth && pTexture->m_Mips[i].m_Height <= maxHeight)
			return i;
	}
	return -1;
}

// Refreshes the lightmap of a world polygon with its dynamic lights (list at WorldPoly+0x30): bFirst = the poly's lightmap lives in the
// currently bound texture of the lightmap stage (it is locked and the lights are added into it in place, FUN_10032c40); otherwise a
// staging lightmap is locked (UnkType_LMLock), the lights are added into it and it is copied into the next pool texture and bound.
// Returns 1 when the lightmap was updated (0 when the poly has none / nothing was built).  The time spent is added to the scene's
// polygrid tick counter.
// The first lock uses WAIT|NOSYSLOCK (0x801); SURFACEMEMORYPTR is zero in the DirectX 8 headers used by this object.
// The early-return form for the missing surface / failed lock and the final lock result reduces the aligned diff to 66 instructions
// (45 ignoring stack offsets), from 121. Current source is 464 bytes with 362 byte differences; local/register layout still differs.
// STUB: D3DREN 0x10020ff0
int FUN_10020ff0(WorldPoly *pPoly, int bFirst)
{
	CountAdder cntAdd(g_pSceneDesc->m_pTicks_Render_PolyGrids);

	if (bFirst)
	{
		RTexture *pBound = (RTexture *)g_pBoundTextures[DAT_1005c838];
		if (!pBound)
			return 1;

		IDirectDrawSurface7 *pSurface = pBound->m_Data.m_pSurface;
		if (!pSurface)
			return 1;
		DDSURFACEDESC2 ddsd;
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		if (pSurface->Lock(0, &ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, 0) < 0)
			return 1;

		FUN_10032c40(DAT_10056770, pPoly, (uint8 *)ddsd.lpSurface, ddsd.lPitch, ddsd.dwWidth, ddsd.dwHeight,
			ddsd.ddpfPixelFormat.dwRGBBitCount != 32);
		pSurface->Unlock(0);
		return 1;
	}
	else
	{
		UnkType_LMLock lock;
		if (!lock.FUN_10034af0(pPoly, 1, 0, 0))
			return 0;

		int nBuild;
		DAT_10056278++;
		nBuild = FUN_10032c40(DAT_10056770, pPoly, lock.m_Unk00, lock.m_Unk04, pPoly->m_LMWidth, pPoly->m_LMHeight,
			lock.m_Unk0c.GetType() != BPP_32);
		if (!nBuild)
			DAT_10055cdc++;
		int result;
		if (lock.FUN_10034c7c(nBuild) && nBuild)
			result = 1;
		else
			result = 0;
		return result;
	}
}

// NAME: names_proposal.csv guess_d3d_IsTextureFullbrite (low, invented): not used
// FUNCTION: D3DREN 0x100211d0
int FUN_100211d0(SharedTexture *pSharedTexture, uint32 nStageFlags)
{
	if (!pSharedTexture->m_pRenderData)
	{
		Counter cCount1(0);
		Counter cCount2(0);
		uint32 dwDummy;
		TextureData *pTextureData = FUN_GetTextureData(pSharedTexture, &dwDummy);
		if (pTextureData)
		{
			UnkType_RTextureBuild build;
			build.m_pSharedTexture = pSharedTexture;
			build.m_pTextureData = pTextureData;
			build.m_nFlags = nStageFlags;
			RTexture *pRTexture = CTextureManager_CreateRTexture(&build, 0);
			if (pRTexture && !r_TransferTexture(pRTexture, pTextureData))
			{
				AddDebugMessage(4, "Unable to transfer texture data to video memory.");
				CTextureManager_FreeTexture(pRTexture, 0);
				pRTexture = 0;
			}
			g_pStruct->FreeTexture(pSharedTexture);
			if (pRTexture)
				goto done;
		}
		return 0;
	}
done:
	return ((RTexture *)pSharedTexture->m_pRenderData)->m_Flags;
}

// Creates the RTexture of a SharedTexture's TextureData for a device stage (UnkType_RTextureBuild: SharedTexture, TextureData, stage flags;
// bAdditional bit 0: an additional-stage texture that is not stored in the SharedTexture): the newer generation of d3d_CreateAndLoadTexture +
// FUN_10020ab0 with the surface creation written out in place (DXT / aspect ratio through the helpers 0x10021a50 / 0x10021960 /
// 0x100219b0), the RTexture taken from the bank and linked into g_Textures.  The mipmaps are not transferred here.  Returns the
// RTexture or 0.  The 0x1608 byte frame is the ConParse of the command string.
// NOT MATCHING: first transcription from the disassembly (the check output gives the sizes); the first usable mipmap search and the
// bank allocation are expanded in place in the exe (our d3d_GetFirstUsableMipmap / ObjectBank::Allocate are calls / an out-of-line
// AllocVoid in the same inline-budget position as in d3d_CreateAndLoadTexture).
// STUB: D3DREN 0x10021290
RTexture *CTextureManager_CreateRTexture(UnkType_RTextureBuild *pBuild, int bAdditional)
{
	TextureData *pTextureData = pBuild->m_pTextureData;
	uint32 nStageFlags = pBuild->m_nFlags;
	DDSURFACEDESC2 ddsd;
	PFormat cFormat;
	ConParse cParse;
	IDirectDrawSurface7 *pSurface;
	RTexture *pRTexture;
	uint32 iFormat, iStartMipmap, nMipmaps, nAvailable, bpp, i;
	int iFirstUsable;
	uint32 width, height, colorValue;
	uint16 alphaRef;
	GenericColor colorOut;
	float fU, fV;

	if (nStageFlags & 0x100)
	{
		if (!g_TextureFormats[FORMAT_BUMPMAP])
			return 0;
		iFormat = FORMAT_BUMPMAP;
	}
	else
	{
		uint32 dtxFlags = pTextureData->m_Flags;
		if (!(dtxFlags & DTX_PREFER16BIT) && g_32BitTextures && g_TextureFormats[FORMAT_32BIT])
			iFormat = FORMAT_32BIT;
		else if (dtxFlags & DTX_PREFER5551)
			iFormat = FORMAT_FULLBRITE;
		else if (dtxFlags & DTX_PREFER4444)
			iFormat = FORMAT_4444;
		else
			iFormat = ((~dtxFlags & DTX_FULLBRITE) << 1) | 1;
	}

	{
		int iGroup = pTextureData->m_Header.m_Extra[0];
		int iStart;

		if (iGroup > 9)
			iGroup = 9;
		iStart = (&g_GroupOffset0)[iGroup] + pTextureData->m_Header.m_Extra[4] + g_MipmapOffset;
		if (g_CV_S3TCEnable.m_IntVal == 0)
			iStart += pTextureData->m_Header.m_Extra[3];
		if (iStart < 0)
			iStart = 0;
		else if (iStart > 3)
			iStart = 3;
		iStartMipmap = iStart;
	}
	if ((int)iStartMipmap > (int)pTextureData->m_Header.m_nMipmaps - 1)
		iStartMipmap = pTextureData->m_Header.m_nMipmaps - 1;

	iFirstUsable = d3d_GetFirstUsableMipmap(pTextureData);
	if (iFirstUsable == -1)
		return 0;
	if ((int)iStartMipmap <= iFirstUsable)
		iStartMipmap = iFirstUsable;

	nMipmaps = pTextureData->m_Header.m_Extra[1];
	if (nMipmaps == 0)
		nMipmaps = 4;
	nAvailable = pTextureData->m_Header.m_nMipmaps - iStartMipmap;
	if (nAvailable == 0)
		return 0;
	if ((int)nMipmaps < 1)
		nMipmaps = 1;
	else if ((int)nMipmaps > (int)nAvailable)
		nMipmaps = nAvailable;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_TEXTURESTAGE;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
	ddsd.ddsCaps.dwCaps2 = 0x40090;
	ddsd.dwWidth = pTextureData->m_Mips[iStartMipmap].m_Width;
	ddsd.dwHeight = pTextureData->m_Mips[iStartMipmap].m_Height;
	ddsd.dwMipMapCount = nMipmaps;
	ddsd.dwTextureStage = nStageFlags;

	bpp = pTextureData->m_Header.m_Extra[2];
	if (bpp == 0)
	{
		bpp = BPP_32;
	}
	else if (bpp != BPP_32 && g_CV_S3TCEnable.m_IntVal && CTextureManager_IsS3TCFormatSupported((BPPIdent)bpp))
	{
		memset(&ddsd.ddpfPixelFormat, 0, sizeof(ddsd.ddpfPixelFormat));
		ddsd.ddpfPixelFormat.dwFlags |= DDPF_FOURCC;
		ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
		if (!CTextureManager_S3TCFormatConv((BPPIdent)bpp, &ddsd.ddpfPixelFormat.dwFourCC))
			return 0;
		goto ParseColorKey;
	}
	ddsd.ddpfPixelFormat = g_TextureFormats[iFormat]->m_PF;
	AdjustAspectRatio(ddsd.dwWidth, ddsd.dwHeight, &ddsd.dwWidth, &ddsd.dwHeight);

ParseColorKey:
	cParse.Init(pTextureData->m_Header.m_CommandString);
	if (cParse.ParseFind("ColorKey", 0, 3) && !g_CV_AlphaTest.m_IntVal)
	{
		uint32 r = atoi(cParse.m_Args[1]);
		uint32 g = atoi(cParse.m_Args[2]);
		uint32 b = atoi(cParse.m_Args[3]);

		colorValue = (((b << 8) | g) << 8) | r;
		if (bpp != BPP_32 && g_CV_S3TCEnable.m_IntVal && CTextureManager_IsS3TCFormatSupported((BPPIdent)bpp))
			cFormat.InitPValueFormat();
		else
			DDPFToPFormat(&ddsd.ddpfPixelFormat, &cFormat);
		if (g_FormatMgr.PValueToFormatColor(&cFormat, colorValue, colorOut) == LT_OK)
		{
			ddsd.dwFlags |= DDSD_CKSRCBLT;
			ddsd.ddckCKSrcBlt.dwColorSpaceLowValue = colorOut.dwVal;
			ddsd.ddckCKSrcBlt.dwColorSpaceHighValue = colorOut.dwVal;
		}
	}

	alphaRef = 0;
	cParse.Init(pTextureData->m_Header.m_CommandString);
	if (cParse.ParseFind("AlphaRef", 0, 1))
		alphaRef = atoi(cParse.m_Args[1]);

	if (g_pDD->CreateSurface(&ddsd, &pSurface, 0) != 0)
	{
		AddDebugMessage(4, "Unable to create (%dx%d) texture surface.", ddsd.dwWidth, ddsd.dwHeight);
		return 0;
	}
	pSurface->SetPriority(pTextureData->m_Header.m_Extra[5]);

	AdjustAspectRatio(pTextureData->m_Mips[0].m_Width, pTextureData->m_Mips[0].m_Height, &width, &height);
	fU = (float)(1 << pTextureData->m_Header.m_Extra[4]) * (1.0f / (float)width);
	fV = (float)(1 << pTextureData->m_Header.m_Extra[4]) * (1.0f / (float)height);

	pRTexture = g_RTextureBank.Allocate();
	if (!pRTexture)
	{
		pSurface->Release();
		return 0;
	}
	pRTexture->m_Unk30 = 0;
	pRTexture->m_Unk49 = bAdditional;
	pRTexture->m_Data.m_Unk04 = fU;
	pRTexture->m_Data.m_Unk08 = fV;
	pRTexture->m_Unk42 = (uint8)nStageFlags;
	pRTexture->m_Data.m_pSurface = pSurface;
	pRTexture->m_Data.m_AlphaRef = alphaRef;
	pRTexture->m_Data.m_pOwner = pRTexture;
	pRTexture->m_iStartMipmap = iStartMipmap;
	pRTexture->m_Unk47 = nMipmaps;
	pRTexture->m_Unk48 = iFormat;
	pRTexture->m_DetailTextureScale = *(float *)&pTextureData->m_Header.m_Extra[6] + 1.0f;
	{
		float fAngle = (float)(int16)(pTextureData->m_Header.m_Extra[11] * 256 + pTextureData->m_Header.m_Extra[10]) * 0.017453292f;
		pRTexture->m_DetailTextureAngleC = (float)cos(fAngle);
		pRTexture->m_DetailTextureAngleS = (float)sin(fAngle);
	}
	pRTexture->m_Data.m_nMemory = 0;
	for (i = iStartMipmap; i < iStartMipmap + nMipmaps; i++)
		pRTexture->m_Data.m_nMemory += (pTextureData->m_Mips[i].m_Width * pTextureData->m_Mips[i].m_Height) << g_TextureFormats[iFormat]->m_BytesPPShift;
	pRTexture->m_pSharedTexture = pBuild->m_pSharedTexture;
	if (!(bAdditional & 1))
		pBuild->m_pSharedTexture->m_pRenderData = pRTexture;
	pRTexture->m_Flags = pTextureData->m_Header.m_IFlags & DTX_FULLBRITE;
	pRTexture->m_BaseWidth = pTextureData->m_Mips[0].m_Width;
	pRTexture->m_BaseHeight = height;
	pRTexture->m_Link.m_pData = pRTexture;
	g_Textures.AddAfter(&pRTexture->m_Link);
	RENDERSTRUCT_TEXMEM(g_pStruct) += pRTexture->m_Data.m_nMemory;
	return pRTexture;
}


// NAME: names_proposal.csv CTextureManager::S3TCFormatConv (medium, Jupiter): a global function in d3d.ren (BPPIdent -> DXT FOURCC)
// FUNCTION: D3DREN 0x10021960
int CTextureManager_S3TCFormatConv(BPPIdent bpp, uint32 *pFourCC)
{
	if (bpp == BPP_S3TC_DXT1)
	{
		*pFourCC = 0x31545844;	// 'DXT1'
		return 1;
	}
	if (bpp == BPP_S3TC_DXT3)
	{
		*pFourCC = 0x33545844;	// 'DXT3'
		return 1;
	}
	if (bpp == BPP_S3TC_DXT5)
	{
		*pFourCC = 0x35545844;	// 'DXT5'
		return 1;
	}
	return 0;
}

// NAME: AdjustAspectRatio: names_proposal.csv (high; exact Jupiter d3d_texture.cpp AdjustAspectRatio)
// FUNCTION: D3DREN 0x100219b0
void AdjustAspectRatio(uint32 width, uint32 height, uint32 *outWidth, uint32 *outHeight)
{
	if (DAT_1005c984 & D3DPTEXTURECAPS_SQUAREONLY)
	{
		width = height = LTMAX(width, height);
	}
	else
	{
		if (g_MaxTexAspectRatio > 0)
		{
			uint32 *pMin = width > height ? &height : &width;
			uint32 *pMax = width > height ? &width : &height;
			if ((int)(*pMax / *pMin) > g_MaxTexAspectRatio)
			{
				*pMin = *pMax / (uint32)g_MaxTexAspectRatio;
			}
		}
	}
	*outWidth = width;
	*outHeight = height;
}

// NAME: names_proposal.csv CTextureManager::IsS3TCFormatSupported (medium, Jupiter): a global function in d3d.ren
// FUNCTION: D3DREN 0x10021a50
int CTextureManager_IsS3TCFormatSupported(BPPIdent bpp)
{
	if (bpp == BPP_S3TC_DXT1)
		return DAT_10062854;
	if (bpp == BPP_S3TC_DXT3)
		return DAT_10062850;
	return bpp == BPP_S3TC_DXT5 ? DAT_1006284c : 0;
}

// NAME: names_proposal.csv guess_d3d_GetTextureUVScale (low, invented): not used
// FUNCTION: D3DREN 0x10021a80
int FUN_10021a80(SharedTexture *pSharedTexture, uint32 nStageFlags, float *pU, float *pV)
{
	if (!pSharedTexture->m_pRenderData)
	{
		Counter cCount1(0);
		Counter cCount2(0);
		uint32 dwDummy;
		TextureData *pTextureData = FUN_GetTextureData(pSharedTexture, &dwDummy);
		if (pTextureData)
		{
			UnkType_RTextureBuild build;
			build.m_pSharedTexture = pSharedTexture;
			build.m_pTextureData = pTextureData;
			build.m_nFlags = nStageFlags;
			RTexture *pRTexture = CTextureManager_CreateRTexture(&build, 0);
			if (pRTexture && !r_TransferTexture(pRTexture, pTextureData))
			{
				AddDebugMessage(4, "Unable to transfer texture data to video memory.");
				CTextureManager_FreeTexture(pRTexture, 0);
				pRTexture = 0;
			}
			g_pStruct->FreeTexture(pSharedTexture);
			if (pRTexture)
				goto done;
		}
		return 0;
	}
done:
	RTexture *pRTexture = (RTexture *)pSharedTexture->m_pRenderData;
	*pU = pRTexture->m_Data.m_Unk04;
	*pV = pRTexture->m_Data.m_Unk08;
	return 1;
}

// NAME: d3d_BindTexture: names_proposal.csv (medium: RenderStruct::BindTexture, installed at RenderStruct+0x78 by RenderDLLSetup)
// NOT MATCHING (256 vs 272 bytes, 14 mismatching instructions of 91): the code is the exe's, with one exception: the exe tests the
// RTexture pointer again at the top of the retransfer loop (`test esi,esi; je` right after the GetTexture call; the loop is entered
// from the `if (pRTexture)` above), our build folds that second test because the first one dominates it.  Tried: for loop, a copy of the
// pointer, reloading m_pRenderData after the call (this reloads esi, which the exe does not), an inline helper for the loop.
// STUB: D3DREN 0x10021b50
void d3d_BindTexture(SharedTexture *pSharedTexture, LTBOOL bTextureChanged)
{
	RTexture *pRTexture = (RTexture *)pSharedTexture->m_pRenderData;
	if (pRTexture)
	{
		if (bTextureChanged)
		{
			uint32 dwDummy;
			TextureData *pTextureData = FUN_GetTextureData(pSharedTexture, &dwDummy);
			if (pTextureData)
			{
				while (pRTexture)
				{
					if (!r_TransferTexture(pRTexture, pTextureData))
						AddDebugMessage(4, "Unable to transfer texture data to video memory.");
					pRTexture = pRTexture->m_Unk30;
				}
				g_pStruct->FreeTexture(pSharedTexture);
			}
		}
	}
	else
	{
		uint32 nStage = g_NormalTextureStage;
		Counter cCount1(0);
		Counter cCount2(0);
		uint32 dwDummy;
		TextureData *pTextureData = FUN_GetTextureData(pSharedTexture, &dwDummy);
		if (pTextureData)
		{
			UnkType_RTextureBuild build;
			build.m_pSharedTexture = pSharedTexture;
			build.m_pTextureData = pTextureData;
			build.m_nFlags = nStage;
			RTexture *pNew = CTextureManager_CreateRTexture(&build, 0);
			if (pNew && !r_TransferTexture(pNew, pTextureData))
			{
				AddDebugMessage(4, "Unable to transfer texture data to video memory.");
				CTextureManager_FreeTexture(pNew, 0);
			}
			g_pStruct->FreeTexture(pSharedTexture);
		}
	}
}

// NAME: d3d_UnbindTexture: names_proposal.csv (medium: RenderStruct::UnbindTexture, installed at RenderStruct+0x7c by RenderDLLSetup;
// the d3d_ prefix is Jupiter's rdll_RenderDLLSetup convention)
// FUNCTION: D3DREN 0x10021c60
void d3d_UnbindTexture(SharedTexture *pSharedTexture)
{
	RTexture *pTexture = (RTexture *)pSharedTexture->m_pRenderData;
	if (pTexture)
		CTextureManager_FreeTexture(pTexture, 0);
}

// Template code of the RTexture bank (ObjectBank<RTexture, NullCS>, stdlith object_bank.h): the out-of-line copies of its virtuals.
// FUNCTION: D3DREN 0x10021c80 ?AllocVoid@?$ObjectBank@VRTexture@@VNullCS@@@@UAEPAXXZ

// FUNCTION: D3DREN 0x10021cf0 ?FreeVoid@?$ObjectBank@VRTexture@@VNullCS@@@@UAEXPAX@Z

// NAME: ObjectBank<RTexture,NullCS>::Term: names_proposal.csv (high, object_bank.h ObjectBank::Term)
// FUNCTION: D3DREN 0x10021d10 ?Term@?$ObjectBank@VRTexture@@VNullCS@@@@UAEXXZ

// FUNCTION: D3DREN 0x10021d30 ??_G?$ObjectBank@VRTexture@@VNullCS@@@@UAEPAXI@Z
