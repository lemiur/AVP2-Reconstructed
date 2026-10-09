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

inline int d3d_CreateMipmapTextureSurface(UnkType_RTextureBuild *pBuild, UnkType_RTextureData *pData, uint32 iStartMipmap, uint32 nMipmaps, uint32 iFormat);
void InitLightColorClampTable();	// 0x10032a30 (lightmap unit unk/100329b0)
int ApplyPolyDynamicLightsToLightmap(MainWorld *pWorld, WorldPoly *pPoly, uint8 *pBits, long pitch, uint32 w, uint32 h, char bNot32Bit);	// 0x10032c40 (unit unk/100329b0)

// RenderStruct::GetTexture as the renderer calls it: with a second (output) argument that the engine's function ignores
// (renderstruct.h declares one parameter).
typedef TextureData *(*PFN_GetTexture2)(SharedTexture *pTexture, uint32 *pUnused);
#define GetEngineTextureDataWithOutputArg(pTexture, pUnused)	(((PFN_GetTexture2)g_pStruct->GetTexture)((pTexture), (pUnused)))
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
LTList g_TextureManagerResetList(LTLink_Init);
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
// guess: the exponent of the shadow blob alpha falloff (d3d_CreateShadowBlobTexture): an initialised float of this object (.data, 0x1004b5d8).
// GLOBAL: D3DREN 0x1004b5d8
float g_fShadowBlobFalloffExponent = 3.0f;
int g_bDXT1Supported;
int g_bDXT3Supported;
int g_bDXT5Supported;
int g_bTextureManagerInitialized;
IDirectDrawSurface7 *g_pLightmapScratchSurface;
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
// and an RTexture for it: the allocator callback the lightmap texture pools use (UnkType_LMTexturePools::CreateTexturePools).
// NAME: names_proposal.csv guess_CreateLightmapPageTexture (low, invented): not used
// NOT MATCHING (416 vs 432 bytes, 286 strict differences): the allocation failure returns early (the exe's Release path is the last
// block).  The exe calls the UnkType_RTextureData constructor out of line (0x1001e900, its only caller) while inlining the rest of
// RTexture(); ours inlines it: an inline-budget decision.  The format / surface failure epilogues are separate copies in the exe and
// one shared block in ours, and the saved-register set differs (the exe saves esi at entry).
// STUB: D3DREN 0x1001e750
RTexture *d3d_CreateLightmapRTexture(uint32 width, uint32 height, uint32 flags)
{
	TextureFormat *pFormat;
	DDSURFACEDESC2 ddsd;
	IDirectDrawSurface7 *pSurface;
	RTexture *pRTexture;

	pFormat = d3d_GetLightmapTextureFormat();
	if (!pFormat)
		return 0;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwHeight = height;
	ddsd.ddsCaps.dwCaps = flags | DDSCAPS_TEXTURE;
	ddsd.dwTextureStage = g_LightmapTextureStage;
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_TEXTURESTAGE;
	ddsd.dwWidth = width;
	ddsd.ddpfPixelFormat = pFormat->m_PF;
	if (g_pDD->CreateSurface(&ddsd, &pSurface, 0) != 0)
		return 0;

	pRTexture = g_RTextureBank.Allocate();
	if (!pRTexture)
	{
		pSurface->Release();
		return 0;
	}

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

	if (g_pLightmapScratchSurface)
	{
		g_pLightmapScratchSurface->Release();
		g_pLightmapScratchSurface = 0;
	}
	g_LightmapTexturePools.FreeTexturePools();
	g_LightmapTexturePools.CreateTexturePools(d3d_CreateLightmapRTexture);
	if (g_pLightmapScratchSurface)
	{
		g_pLightmapScratchSurface->Release();
		g_pLightmapScratchSurface = 0;
	}
	if (pFormat = d3d_GetLightmapTextureFormat())
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwHeight = 0x20;
		ddsd.dwWidth = 0x20;
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		ddsd.ddpfPixelFormat = pFormat->m_PF;
		g_pDD->CreateSurface(&ddsd, &g_pLightmapScratchSurface, 0);
	}
}

// Picks a format for every FORMAT_* use from the enumerated ones; prints "FORMAT_x texture format missing." and fails when a required one
// is missing.  NAME: Jupiter CTextureManager::SelectTextureFormats (a static function here: Init expands it, there is no copy in d3d.ren).
static LTBOOL CTextureManager_SelectTextureFormats()
{
	// The wanted formats (bits of red, green, blue, alpha; one of these DDPF_ flags; none of these) in the order of preference.
	TextureFormatSpec spec32[1] = { { 8, 8, 8, 8, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specFullbrite[2] = { { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE }, { 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec spec4444[2] = { { 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE }, { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specNormal[3] = { { 5, 6, 5, 0, DDPF_RGB, DDPF_LUMINANCE }, { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE },
		{ 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specInterface[2] = { { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE | DDPF_PALETTEINDEXED8 },
		{ 4, 4, 4, 4, DDPF_ALPHAPIXELS, DDPF_LUMINANCE | DDPF_PALETTEINDEXED8 } };
	TextureFormatSpec specLightmap[2] = { { 5, 5, 5, 0, DDPF_RGB, DDPF_LUMINANCE }, { 5, 5, 5, 1, DDPF_ALPHAPIXELS, DDPF_LUMINANCE } };
	TextureFormatSpec specBump[1] = { { 8, 8, 0, 0, DDPF_BUMPDUDV, DDPF_LUMINANCE } };

	g_TextureFormats[FORMAT_32BIT] = d3d_FindTextureFormatBySpecs(spec32, 1);
	g_TextureFormats[FORMAT_FULLBRITE] = d3d_FindTextureFormatBySpecs(specFullbrite, 2);
	if (!g_TextureFormats[FORMAT_FULLBRITE])
	{
		AddDebugMessage(0, "FORMAT_FULLBRITE texture format missing.");
		return FALSE;
	}
	g_TextureFormats[FORMAT_4444] = d3d_FindTextureFormatBySpecs(spec4444, 2);
	if (!g_TextureFormats[FORMAT_4444])
	{
		AddDebugMessage(0, "FORMAT_4444 texture format missing.");
		return FALSE;
	}
	g_TextureFormats[FORMAT_NORMAL] = d3d_FindTextureFormatBySpecs(specNormal, 3);
	if (!g_TextureFormats[FORMAT_NORMAL])
	{
		AddDebugMessage(0, "FORMAT_NORMAL texture format missing.");
		return FALSE;
	}
	g_TextureFormats[FORMAT_INTERFACE] = d3d_FindTextureFormatBySpecs(specInterface, 2);
	if (!g_TextureFormats[FORMAT_INTERFACE])
	{
		AddDebugMessage(0, "FORMAT_INTERFACE texture format missing.");
		return FALSE;
	}
	g_TextureFormats[FORMAT_LIGHTMAP] = d3d_FindTextureFormatBySpecs(specLightmap, 2);
	if (!g_TextureFormats[FORMAT_LIGHTMAP])
	{
		AddDebugMessage(0, "Warning: device not lightmap capable.");
		g_bLightmapCapable = 0;
	}
	g_TextureFormats[FORMAT_BUMPMAP] = d3d_FindTextureFormatBySpecs(specBump, 1);
	DetectDXTTextureFormatSupport();
	return TRUE;
}

// Texture manager Init: clears the tables and lists, creates the RTexture bank, enumerates the device's texture formats and picks one for
// every FORMAT_* use (CTextureManager_SelectTextureFormats).  (The Jupiter CTextureManager::Init; the lightmap texture support reset of
// d3d_ReinitLightmapTextureSupport is written out at its end.)
// NAME: names_proposal.csv CTextureManager::Init (medium, Jupiter): a global function in d3d.ren
// The format selection is a static function Init expands (its four "missing" exits cross-jump to one AddDebugMessage tail, and its
// tables share stack with the surface description of the lightmap reset); the tables are declared in the order of their use.
// FUNCTION: D3DREN 0x1001ec50
int CTextureManager_Init()
{
	memset(g_TextureFormats, 0, sizeof(g_TextureFormats));
	for (int i = 0; i < 4; i++)
		g_pBoundTextures[i] = 0;
	g_TextureManagerResetList.m_nElements = 0;
	g_TextureFormatList.TieOff();
	g_TextureManagerResetList.m_Head.TieOff();
	g_Textures.TieOff();
	g_LightmapTexturePools.ResetTexturePoolLists();
	g_RTextureBank.Init(0x40, 0);
	g_bTextureManagerInitialized = 1;
	g_pD3DDevice->EnumTextureFormats(d3d_EnumTextureFormatsCallback, 0);

	if (!CTextureManager_SelectTextureFormats())
		return 0;
	{
		TextureFormat *pLightmapFormat;
		DDSURFACEDESC2 ddsd;

		if (g_pLightmapScratchSurface)
		{
			g_pLightmapScratchSurface->Release();
			g_pLightmapScratchSurface = 0;
		}
		g_LightmapTexturePools.FreeTexturePools();
		g_LightmapTexturePools.CreateTexturePools(d3d_CreateLightmapRTexture);
		if (g_pLightmapScratchSurface)
		{
			g_pLightmapScratchSurface->Release();
			g_pLightmapScratchSurface = 0;
		}
		if (pLightmapFormat = d3d_GetLightmapTextureFormat())
		{
			memset(&ddsd, 0, sizeof(ddsd));
			ddsd.dwHeight = 0x20;
			ddsd.dwWidth = 0x20;
			ddsd.dwSize = sizeof(ddsd);
			ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
			ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
			ddsd.ddpfPixelFormat = pLightmapFormat->m_PF;
			g_pDD->CreateSurface(&ddsd, &g_pLightmapScratchSurface, 0);
		}
	}
	InitLightColorClampTable();
	d3d_CreateShadowBlobTexture();
	d3d_BuildSpecularLookupTexture(5.0f);
	return 1;
}

// The bit range of a colour mask: *pEnd = index just above the run of set bits, *pStart = index of the lowest set bit.
// NAME: names_proposal.csv guess_GetMaskBitRange (low, invented): not used
// An inline function in the original: the callback below calls the copy that follows it (0x1001f540) at all but one site and
// expands the 0x7c00 one.
inline void GetColorMaskBitRange(uint32 mask, uint32 *pEnd, uint32 *pStart)
{
	*pStart = 0;
	uint32 bit = 1;
	for (uint32 i = 0; i < 32; i++)
	{
		if ((mask & bit) != 0)
			break;
		bit = bit << 1;
		(*pStart)++;
	}
	*pEnd = *pStart;
	for (uint32 j = 0; j < 32; j++)
	{
		if ((mask & bit) == 0)
			return;
		bit = bit << 1;
		(*pEnd)++;
	}
}

// The shifts that convert a colour channel with the mask refMask into the channel of a format with the mask `mask`:
// *pRight bits to the right when the reference reaches higher, else *pLeft bits to the left.  (Inlined at its ten uses in the
// callback below.)
static void CalcColorMaskAlignmentShifts(uint32 refMask, uint32 mask, int *pRight, int *pLeft)
{
	uint32 refEnd, refStart, end, start;

	GetColorMaskBitRange(refMask, &refEnd, &refStart);
	GetColorMaskBitRange(mask, &end, &start);
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
// GetColorMaskBitRange is an inline function (its out-of-line copy follows at 0x1001f540): the exe calls it at every site but the
// constant-mask one of the 0x7c00 shift, which its budget share expands.  That needs GetColorMaskBitRange's explicit `!= 0` / `== 0`
// tests and `bit = bit << 1` (its front-end size, ~133u); `if (mask & bit)` / `bit <<= 1` (123u) expands it from the 6th pair on.
// FUNCTION: D3DREN 0x1001f0d0
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
		pNode->m_RBits = (uint16)CountMaskBits(pFormat->dwRBitMask);
		pNode->m_GBits = (uint16)CountMaskBits(pFormat->dwGBitMask);
		pNode->m_BBits = (uint16)CountMaskBits(pFormat->dwBBitMask);
		pNode->m_ABits = (uint16)CountMaskBits(pFormat->dwRGBAlphaBitMask);

		CalcColorMaskAlignmentShifts(0xff, pFormat->dwRBitMask, &pNode->m_Shifts[0], &pNode->m_Shifts[1]);
		CalcColorMaskAlignmentShifts(0xff, pFormat->dwGBitMask, &pNode->m_Shifts[2], &pNode->m_Shifts[3]);
		CalcColorMaskAlignmentShifts(0xff, pFormat->dwBBitMask, &pNode->m_Shifts[4], &pNode->m_Shifts[5]);
		CalcColorMaskAlignmentShifts(0xff, pFormat->dwRGBAlphaBitMask, &pNode->m_Shifts[6], &pNode->m_Shifts[7]);
		CalcColorMaskAlignmentShifts(0xf800, pFormat->dwRBitMask, &pNode->m_Shifts[8], &pNode->m_Shifts[9]);
		CalcColorMaskAlignmentShifts(0x7e0, pFormat->dwGBitMask, &pNode->m_Shifts[10], &pNode->m_Shifts[11]);
		CalcColorMaskAlignmentShifts(0x1f, pFormat->dwBBitMask, &pNode->m_Shifts[12], &pNode->m_Shifts[13]);
		CalcColorMaskAlignmentShifts(0x7c00, pFormat->dwRBitMask, &pNode->m_Shifts[14], &pNode->m_Shifts[15]);
		CalcColorMaskAlignmentShifts(0x3e0, pFormat->dwGBitMask, &pNode->m_Shifts[16], &pNode->m_Shifts[17]);
		CalcColorMaskAlignmentShifts(0x1f, pFormat->dwBBitMask, &pNode->m_Shifts[18], &pNode->m_Shifts[19]);

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

// FUNCTION: D3DREN 0x1001f540 ?GetColorMaskBitRange@@YAXKPAK0@Z

// Finds the first enumerated texture format that matches one of the nSpecs wanted formats (bits per colour channel, flags).
// NAME: names_proposal.csv guess_FindTextureFormat (low, invented): not used
// FUNCTION: D3DREN 0x1001f590
TextureFormat *d3d_FindTextureFormatBySpecs(const TextureFormatSpec *pSpecs, uint32 nSpecs)
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
void DetectDXTTextureFormatSupport()
{
	g_bDXT5Supported = 0;
	g_bDXT3Supported = 0;
	g_bDXT1Supported = 0;
	for (LTLink *pCur = g_TextureFormatList.m_pNext; pCur != &g_TextureFormatList; pCur = pCur->m_pNext)
	{
		TextureFormat *pFormat = (TextureFormat *)pCur->m_pData;
		if (pFormat->m_PF.dwFlags & DDPF_FOURCC)
		{
			if (pFormat->m_PF.dwFourCC == 0x31545844)
				g_bDXT1Supported = 1;
			else if (pFormat->m_PF.dwFourCC == 0x33545844)
				g_bDXT3Supported = 1;
			else if (pFormat->m_PF.dwFourCC == 0x35545844)
				g_bDXT5Supported = 1;
		}
	}
}
#pragma auto_inline(on)

// Builds the 16x16 shadow blob texture: white with an alpha of 1 - (distance from the centre / 7.5)^3.
// NAME: names_proposal.csv guess_BuildShadowBlobTexture (low, invented): not used
// FUNCTION: D3DREN 0x1001f670
void d3d_CreateShadowBlobTexture()
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
			pixels[y * 16 + x] = ((uint32)(uint8)(uint32)((1.0f - (float)pow(fDist * 0.13333334f, g_fShadowBlobFalloffExponent)) * 255.9f) << 24) | 0xffffff;
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
void CTextureManager_FreeTextureList(LTLink *pList)
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

// The DDPF_ flag name of a pixel format (the first flag set, in this order) into pStr: the DirectDraw form of Jupiter's
// d3d_D3DFormatToString (d3d_utils.cpp).  NAME: invented (not in d3d.ren: d3d_PrintFormatInfo expands it; the d3d_AddToString
// calls inside get a budget share that expands the first ten and calls the copy from the other nine).
static void d3d_DDPFFlagsToString(uint32 dwFlags, char *pStr)
{
	pStr[0] = 0;
	if (dwFlags & DDPF_ALPHA)
		d3d_AddToString(pStr, "DDPF_ALPHA");
	else if (dwFlags & DDPF_ALPHAPIXELS)
		d3d_AddToString(pStr, "DDPF_ALPHAPIXELS");
	else if (dwFlags & DDPF_ALPHAPREMULT)
		d3d_AddToString(pStr, "DDPF_ALPHAPREMULT");
	else if (dwFlags & DDPF_BUMPLUMINANCE)
		d3d_AddToString(pStr, "DDPF_BUMPLUMINANCE");
	else if (dwFlags & DDPF_BUMPDUDV)
		d3d_AddToString(pStr, "DDPF_BUMPDUDV");
	else if (dwFlags & DDPF_COMPRESSED)
		d3d_AddToString(pStr, "DDPF_COMPRESSED");
	else if (dwFlags & DDPF_FOURCC)
		d3d_AddToString(pStr, "DDPF_FOURCC");
	else if (dwFlags & DDPF_LUMINANCE)
		d3d_AddToString(pStr, "DDPF_LUMINANCE");
	else if (dwFlags & DDPF_PALETTEINDEXED1)
		d3d_AddToString(pStr, "DDPF_PALETTEINDEXED1");
	else if (dwFlags & DDPF_PALETTEINDEXED2)
		d3d_AddToString(pStr, "DDPF_PALETTEINDEXED2");
	else if (dwFlags & DDPF_PALETTEINDEXED4)
		d3d_AddToString(pStr, "DDPF_PALETTEINDEXED4");
	else if (dwFlags & DDPF_PALETTEINDEXED8)
		d3d_AddToString(pStr, "DDPF_PALETTEINDEXED8");
	else if (dwFlags & DDPF_PALETTEINDEXEDTO8)
		d3d_AddToString(pStr, "DDPF_PALETTEINDEXEDTO8");
	else if (dwFlags & DDPF_RGB)
		d3d_AddToString(pStr, "DDPF_RGB");
	else if (dwFlags & DDPF_RGBTOYUV)
		d3d_AddToString(pStr, "DDPF_RGBTOYUV");
	else if (dwFlags & DDPF_STENCILBUFFER)
		d3d_AddToString(pStr, "DDPF_STENCILBUFFER");
	else if (dwFlags & DDPF_ZBUFFER)
		d3d_AddToString(pStr, "DDPF_ZBUFFER");
	else if (dwFlags & DDPF_ZPIXELS)
		d3d_AddToString(pStr, "DDPF_ZPIXELS");
	else
		d3d_AddToString(pStr, "UNKNOWN");
}

// Prints one texture format (its DDPF_ name, bit counts and masks) through RenderStruct::ConsolePrint, or "<pStart>NONE" for a
// missing one.
// NAME: d3d_PrintFormatInfo: names_proposal.csv (medium; Jupiter d3d_PrintFormatInfo(pStart, format))
// FUNCTION: D3DREN 0x1001fa40
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
		d3d_DDPFFlagsToString(pFormat->m_PF.dwFlags, spec);
	}
	g_pStruct->ConsolePrint("%s%s - %d bits (%d %d %d %d) (%x %x %x %x)", pStart, spec, pFormat->m_PF.dwRGBBitCount,
		pFormat->m_RBits, pFormat->m_GBits, pFormat->m_BBits, pFormat->m_ABits,
		pFormat->m_PF.dwRBitMask, pFormat->m_PF.dwGBitMask, pFormat->m_PF.dwBBitMask, pFormat->m_PF.dwRGBAlphaBitMask);
}

// NAME: d3d_AddToString: names_proposal.csv (high; Jupiter d3d_AddToString(pStr, pToAdd, nBufferLen), here without the length).
// An extern function defined after its callers (as in Jupiter d3d_utils.cpp): the printer expands it at ten of its nineteen sites.
// FUNCTION: D3DREN 0x1001ff80
char *d3d_AddToString(char *pStr, const char *pToAdd)
{
	strcat(pStr, pToAdd);
	strcat(pStr, " ");
	return pStr + strlen(pStr);
}

// Creates the RTexture of a SharedTexture for the device stage nStageFlags (stage in the low byte, 0x100 = bump map stage) from the
// engine's TextureData (CTextureManager_CreateRTexture) and uploads the mipmaps (r_TransferTexture).  Returns 0 on failure.  Jupiter's
// d3d_CreateAndLoadTexture shape.  168u: under the 174u auto-inline cap, so d3d_EnsureTextureAndGetFlags / ...UVScale / d3d_BindTexture
// expand it, and the CTextureManager_CreateRTexture (906u) nested there is refused (their copy of the call goes to 0x10021290).
// Here CTextureManager_CreateRTexture is expanded with a share of ~94u: its own inline calls (d3d_GetFirstUsableMipmap,
// d3d_CreateMipmapTextureSurface, ObjectBank::Allocate (folded with AllocVoid, 0x10021c80), the UnkType_RTextureData assignment,
// CheapLTLink::AddAfter) stay calls.
// NAME: names_proposal.csv d3d_CreateAndLoadTexture (medium, Jupiter d3d_texture.cpp)
// NOT MATCHING: the exe also calls the UnkType_RTextureData destructor at each exit of the expanded body (the implicit one is 17u,
// free, expanded).  An explicit `virtual ~UnkType_RTextureData() {}` (43u) gives this function the exe's size and call set (52
// mismatches, registers) but then CTextureManager_CreateRTexture's copy refuses sb_Allocate and one more IsS3TCFormatSupported (its
// dtor sites take budget); not adopted.  Also `not al; movsx` in the format choice.
// STUB: D3DREN 0x1001fff0
RTexture *d3d_CreateAndLoadTexture(SharedTexture *pSharedTexture, uint32 nStageFlags, uint8 bAdditional)
{
	RTexture *pRTexture = 0;
	Counter cCount1(0);
	Counter cCount2(0);
	uint32 dwDummy;
	TextureData *pTextureData = GetEngineTextureDataWithOutputArg(pSharedTexture, &dwDummy);
	if (pTextureData)
	{
		UnkType_RTextureBuild build;

		build.m_pSharedTexture = pSharedTexture;
		build.m_pTextureData = pTextureData;
		build.m_nFlags = nStageFlags;
		pRTexture = CTextureManager_CreateRTexture(&build, bAdditional);
		if (pRTexture)
		{
			if (!r_TransferTexture(pRTexture, pTextureData))
			{
				AddDebugMessage(4, "Unable to transfer texture data to video memory.");
				CTextureManager_FreeTexture(pRTexture, 0);
				pRTexture = 0;
			}
		}
		g_pStruct->FreeTexture(pSharedTexture);
	}
	return pRTexture;
}


// FUNCTION: D3DREN 0x10020330 ?AddAfter@CheapLTLink@@QAEXPAV1@@Z
// FUNCTION: D3DREN 0x10020350 ??1UnkType_RTextureData@@UAE@XZ
// FUNCTION: D3DREN 0x10020fb0 ??4UnkType_RTextureData@@QAEAAV0@ABV0@@Z
// STANDIN: forces the out-of-line copy of the implicit UnkType_RTextureData destructor, which d3d_CreateAndLoadTexture calls out of
// line in the exe (at every exit of the expanded CTextureManager_CreateRTexture) and expands in our build; with inline_depth(0) the
// copy comes out byte-identical to the exe's.  It also emits the constructor copy 0x1001e900 (d3d_CreateLightmapRTexture's call).
// (not in d3d.ren)
#pragma inline_depth(0)
void StandIn_RTextureInlines()
{
	UnkType_RTextureData local;
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

	if (pTexture->m_Unk47 + pTexture->m_iStartMipmap > pTextureData->m_Header.m_nMipmaps)
	{
		AddDebugMessage(1, "r_TransferTexture: mipmap count doesn't match!");
		return 0;
	}

	bpp = pTextureData->m_Header.GetBPPIdent();

	pSurface = pTexture->m_Data.m_pSurface;
	pFormat = g_TextureFormats[pTexture->m_Unk48];
	ddsdSurf.dwSize = sizeof(DDSURFACEDESC2);
	ddsdSurf.dwFlags = DDSD_HEIGHT | DDSD_WIDTH;
	pSurface->GetSurfaceDesc(&ddsdSurf);
	surfWidth = ddsdSurf.dwWidth;
	surfHeight = ddsdSurf.dwHeight;
	AddDebugMessage(4, "Uploading a (%dx%d) texture", ddsdSurf.dwWidth, ddsdSurf.dwHeight);

	for (i = pTexture->m_iStartMipmap; i < (uint32)pTexture->m_iStartMipmap + pTexture->m_Unk47; i++)
	{
		pMip = &pTextureData->m_Mips[i];
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

// Creates the DirectDraw texture surface of an RTexture (iStartMipmap, nMipmaps and the format iFormat chosen by the caller) and fills
// in the UnkType_RTextureData pData with it (surface, 1/width and 1/height scaled by the texture's U/V shift, the AlphaRef of the DTX
// command string).  Handles the "ColorKey r g b" and "AlphaRef n" tokens of the command string, DXT formats and the aspect ratio
// limit.  The DXT / aspect ratio helpers (CTextureManager_IsS3TCFormatSupported, CTextureManager_S3TCFormatConv, AdjustAspectRatio)
// are expanded here; the colour key is PValue_Set(0, r, g, b) (its arguments are evaluated b first).  An inline function: its copy
// here is the one d3d_CreateAndLoadTexture calls; CTextureManager_CreateRTexture expands it.
// NAME: names_proposal.csv guess_CreateRTextureSurface (low, invented): not used
// FUNCTION: D3DREN 0x10020ab0 ?d3d_CreateMipmapTextureSurface@@YAHPAUUnkType_RTextureBuild@@PAVUnkType_RTextureData@@KKK@Z
inline int d3d_CreateMipmapTextureSurface(UnkType_RTextureBuild *pBuild, UnkType_RTextureData *pData, uint32 iStartMipmap, uint32 nMipmaps, uint32 iFormat)
{
	TextureData *pTextureData = pBuild->m_pTextureData;
	DDSURFACEDESC2 ddsd;
	ConParse cParse;
	PFormat cFormat;
	IDirectDrawSurface7 *pSurface;
	uint32 bpp, uOutWidth, uOutHeight, colorValue;
	GenericColor colorOut;
	float fU, fV;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_TEXTURESTAGE;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
	ddsd.ddsCaps.dwCaps2 = 0x40090;
	ddsd.dwWidth = pTextureData->m_Mips[iStartMipmap].m_Width;
	ddsd.dwHeight = pTextureData->m_Mips[iStartMipmap].m_Height;
	ddsd.dwMipMapCount = nMipmaps;
	ddsd.dwTextureStage = pBuild->m_nFlags;

	bpp = pTextureData->m_Header.GetBPPIdent();
	if (bpp != BPP_32 && g_CV_S3TCEnable.m_IntVal && CTextureManager_IsS3TCFormatSupported((BPPIdent)bpp))
	{
		memset(&ddsd.ddpfPixelFormat, 0, sizeof(ddsd.ddpfPixelFormat));
		ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
		ddsd.ddpfPixelFormat.dwFlags |= DDPF_FOURCC;
		if (!CTextureManager_S3TCFormatConv((BPPIdent)bpp, &ddsd.ddpfPixelFormat.dwFourCC))
			return 0;
	}
	else
	{
		ddsd.ddpfPixelFormat = g_TextureFormats[iFormat]->m_PF;
		AdjustAspectRatio(ddsd.dwWidth, ddsd.dwHeight, &ddsd.dwWidth, &ddsd.dwHeight);
	}

	cParse.Init(pTextureData->m_Header.m_CommandString);
	if (cParse.ParseFind("ColorKey", 0, 3) && !g_CV_AlphaTest.m_IntVal)
	{
		colorValue = PValue_Set(0, atoi(cParse.m_Args[1]), atoi(cParse.m_Args[2]), atoi(cParse.m_Args[3]));
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

	cParse.Init(pTextureData->m_Header.m_CommandString);
	if (cParse.ParseFind("AlphaRef", 0, 1))
		pData->m_AlphaRef = atoi(cParse.m_Args[1]);
	else
		pData->m_AlphaRef = 0;

	if (g_pDD->CreateSurface(&ddsd, &pSurface, 0) != 0)
	{
		AddDebugMessage(4, "Unable to create (%dx%d) texture surface.", ddsd.dwWidth, ddsd.dwHeight);
		return 0;
	}
	pSurface->SetPriority(pTextureData->m_Header.GetTexturePriority());

	AdjustAspectRatio(pTextureData->m_Mips[0].m_Width, pTextureData->m_Mips[0].m_Height, &uOutWidth, &uOutHeight);
	fU = 1.0f / (float)uOutWidth;
	pData->m_Unk04 = fU;
	fV = 1.0f / (float)uOutHeight;
	pData->m_Unk08 = fV;
	pData->m_Unk04 = pTextureData->m_Header.GetUIMipmapScale() * fU;
	pData->m_Unk08 = pTextureData->m_Header.GetUIMipmapScale() * fV;
	pData->m_pSurface = pSurface;
	return 1;
}

// NAME: d3d_GetFirstUsableMipmap: names_proposal.csv (high; Jupiter d3d_texture.cpp d3d_GetFirstUsableMipmap): the first mipmap that fits
// the largest texture the device and the MaxTextureSize variable allow (clamped to the screen unless larger surfaces are
// allowed), -1 when none does.  The height is clamped with the already clamped width (as the exe does).  An inline function (184u:
// over the 174u auto-inline cap of an extern one, and CTextureManager_CreateRTexture expands it); this is the copy
// d3d_CreateAndLoadTexture calls.
// FUNCTION: D3DREN 0x10020f20 ?d3d_GetFirstUsableMipmap@@YAHPAVTextureData@@@Z
inline int d3d_GetFirstUsableMipmap(TextureData *pTexture)
{
	uint32 maxWidth = LTMIN((uint32)g_MaxTextureSize, g_DeviceMaxTextureWidth);
	uint32 maxHeight = LTMIN((uint32)g_MaxTextureSize, g_DeviceMaxTextureHeight);
	if (!maxWidth)
		maxWidth = 256;
	if (!maxHeight)
		maxHeight = 256;

	if (!g_bSurfacesLargerThanScreenSupported)
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
// currently bound texture of the lightmap stage (it is locked and the lights are added into it in place, ApplyPolyDynamicLightsToLightmap); otherwise a
// staging lightmap is locked (UnkType_LMLock), the lights are added into it and it is copied into the next pool texture and bound.
// Returns 1 when the lightmap was updated (0 when the poly has none / nothing was built).  The time spent is added to the scene's
// polygrid tick counter.
// The first lock uses WAIT|NOSYSLOCK (0x801); SURFACEMEMORYPTR is zero in the DirectX 8 headers used by this object.
// The staging path reads the lock's pitch and texel pointer into locals after the counter increment: the exe loads both before the
// pixel-format call (pitch into edi, texels into ebx).
// FUNCTION: D3DREN 0x10020ff0
int d3d_RefreshWorldPolyLightmap(WorldPoly *pPoly, int bFirst)
{
	CountAdder cntAdd(g_pSceneDesc->m_pTicks_Render_PolyGrids);

	if (bFirst)
	{
		RTexture *pBound = (RTexture *)g_pBoundTextures[g_LightmapTextureStage];
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

		ApplyPolyDynamicLightsToLightmap(g_pFrameMainWorld, pPoly, (uint8 *)ddsd.lpSurface, ddsd.lPitch, ddsd.dwWidth, ddsd.dwHeight,
			ddsd.ddpfPixelFormat.dwRGBBitCount != 32);
		pSurface->Unlock(0);
		return 1;
	}
	else
	{
		UnkType_LMLock lock;
		if (!lock.LockStagingLightmap(pPoly, 1, 0, 0))
			return 0;

		int nBuild;
		g_nDynamicLightmapsRefreshed++;
		long nPitch = lock.m_Unk04;
		uint8 *pData = lock.m_Unk00;
		nBuild = ApplyPolyDynamicLightsToLightmap(g_pFrameMainWorld, pPoly, pData, nPitch, pPoly->m_LMWidth, pPoly->m_LMHeight,
			lock.m_Unk0c.GetType() != BPP_32);
		if (!nBuild)
			g_nTextureUploadSaves++;
		int result;
		if (lock.UnlockStagingLightmap(nBuild) && nBuild)
			result = 1;
		else
			result = 0;
		return result;
	}
}

// NAME: names_proposal.csv guess_d3d_IsTextureFullbrite (low, invented): not used
// FUNCTION: D3DREN 0x100211d0
int d3d_EnsureTextureAndGetFlags(SharedTexture *pSharedTexture, uint32 nStageFlags)
{
	if (!pSharedTexture->m_pRenderData)
	{
		if (!d3d_CreateAndLoadTexture(pSharedTexture, nStageFlags, 0))
			return 0;
	}
	return ((RTexture *)pSharedTexture->m_pRenderData)->m_Flags;
}

// Creates the RTexture of a SharedTexture's TextureData for a device stage (UnkType_RTextureBuild: SharedTexture, TextureData, stage flags;
// bAdditional bit 0: an additional-stage texture that is not stored in the SharedTexture): d3d_CreateAndLoadTexture without the upload,
// the RTexture taken from the bank and linked into g_Textures.  Returns the RTexture or 0.
// The exe expands the inline d3d_GetFirstUsableMipmap and d3d_CreateMipmapTextureSurface here; inside the latter's expansion the
// helpers IsS3TCFormatSupported / S3TCFormatConv / AdjustAspectRatio get only a small budget share (R8) and stay calls, as does the
// RTexture constructor inside ObjectBank::Allocate: the call set and the size (1744) are the exe's.
// NOT MATCHING: register and stack-slot choice (frame 0x1608 vs 0x1604: the exe shares the stage-flags temporary with pSurface; the
// exe keeps the AlphaRef in di and bpp on the stack), and `not al; movsx` in the format choice (ours `not eax`).
// STUB: D3DREN 0x10021290 ?CTextureManager_CreateRTexture@@YAPAVRTexture@@PAUUnkType_RTextureBuild@@E@Z
inline RTexture *CTextureManager_CreateRTexture(UnkType_RTextureBuild *pBuild, uint8 bAdditional)
{
	TextureData *pTextureData = pBuild->m_pTextureData;
	uint32 nStageFlags = pBuild->m_nFlags;
	UnkType_RTextureData data;
	RTexture *pRTexture;
	int iFormat;
	int iStartMipmap, nMipmaps, nAvailable, i;

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

	memset(&data, 0, sizeof(data));

	{
		int iGroup = pTextureData->m_Header.GetTextureGroup();
		if (iGroup > 9)
			iGroup = 9;
		iStartMipmap = (&g_GroupOffset0)[iGroup] + pTextureData->m_Header.GetUIMipmapOffset() + g_MipmapOffset;
		if (g_CV_S3TCEnable.m_IntVal == 0)
			iStartMipmap += pTextureData->m_Header.GetNonS3TCMipmapOffset();
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
			return 0;
		if (iStartMipmap <= iFirstUsable)
			iStartMipmap = iFirstUsable;
	}

	nMipmaps = pTextureData->m_Header.GetNumMipmaps();
	if (nMipmaps == 0)
		nMipmaps = 4;
	nAvailable = pTextureData->m_Header.m_nMipmaps - iStartMipmap;
	if (nAvailable == 0)
		return 0;
	if (nMipmaps < 1)
		nMipmaps = 1;
	else if (nMipmaps > nAvailable)
		nMipmaps = nAvailable;

	if (!d3d_CreateMipmapTextureSurface(pBuild, &data, iStartMipmap, nMipmaps, iFormat))
		return 0;

	pRTexture = g_RTextureBank.Allocate();
	if (!pRTexture)
	{
		data.m_pSurface->Release();
		return 0;
	}

	pRTexture->m_Unk30 = 0;
	pRTexture->m_Unk49 = bAdditional;
	pRTexture->m_Unk42 = (uint8)pBuild->m_nFlags;
	pRTexture->m_Data = data;
	pRTexture->m_Data.m_pOwner = pRTexture;
	pRTexture->m_iStartMipmap = iStartMipmap;
	pRTexture->m_Unk47 = nMipmaps;
	pRTexture->m_Unk48 = iFormat;
	pRTexture->m_DetailTextureScale = pTextureData->m_Header.GetDetailTextureScale();
	{
		float fAngle = (float)pTextureData->m_Header.GetDetailTextureAngle() * 0.017453292f;
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
	pRTexture->m_BaseHeight = pTextureData->m_Mips[0].m_Height;
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
	if (g_DeviceTriangleTextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
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
		return g_bDXT1Supported;
	else if (bpp == BPP_S3TC_DXT3)
		return g_bDXT3Supported;
	else if (bpp == BPP_S3TC_DXT5)
		return g_bDXT5Supported;
	else
		return 0;
}

// NAME: names_proposal.csv guess_d3d_GetTextureUVScale (low, invented): not used
// FUNCTION: D3DREN 0x10021a80
int d3d_EnsureTextureAndGetUVScale(SharedTexture *pSharedTexture, uint32 nStageFlags, float *pU, float *pV)
{
	if (!pSharedTexture->m_pRenderData)
	{
		if (!d3d_CreateAndLoadTexture(pSharedTexture, nStageFlags, 0))
			return 0;
	}
	RTexture *pRTexture = (RTexture *)pSharedTexture->m_pRenderData;
	*pU = pRTexture->m_Data.m_Unk04;
	*pV = pRTexture->m_Data.m_Unk08;
	return 1;
}

// NAME: d3d_BindTexture: names_proposal.csv (medium: RenderStruct::BindTexture, installed at RenderStruct+0x78 by RenderDLLSetup)
// The guarded do-loop retains the native entry test after GetTexture and the trailing next-texture test.
// The two unused DWORD callback outputs have separate function-scope storage; VC6 places them in the native dead argument homes.
// FUNCTION: D3DREN 0x10021b50
void d3d_BindTexture(SharedTexture *pSharedTexture, LTBOOL bTextureChanged)
{
	uint32 dwDummyFirst;
	uint32 dwDummySecond;
	RTexture *pRTexture = (RTexture *)pSharedTexture->m_pRenderData;
	if (pRTexture)
	{
		if (bTextureChanged)
		{
			TextureData *pTextureData = GetEngineTextureDataWithOutputArg(pSharedTexture, &dwDummyFirst);
			if (pTextureData)
			{
				do
				{
					if (pRTexture)
					{
						if (!r_TransferTexture(pRTexture, pTextureData))
							AddDebugMessage(4, "Unable to transfer texture data to video memory.");
						pRTexture = pRTexture->m_Unk30;
					}
					else
						break;
				} while (pRTexture);
				g_pStruct->FreeTexture(pSharedTexture);
			}
		}
	}
	else
	{
		d3d_CreateAndLoadTexture(pSharedTexture, g_NormalTextureStage, 0);
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
