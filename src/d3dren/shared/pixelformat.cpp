// d3d.ren shared/pixelformat (0x10035de7-0x10038830): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// FLAGS: /O1 /Ob2
// d3d.ren unit unk/10034000 (0x10034000-0x10038830): lightmap pages (64x64 DirectDraw texture pages the polygon lightmaps are
// packed into: "Unable to create (%dx%d) lightmap page.", "Lightmaps paged in %.1f seconds.", "LightAnim_BASE"), the lightmap
// plane table and SetupLMPlaneVectors (engine twin src/shared/lightmap_planes.cpp), the lightmap staging texture pools and
// the queued world polygon drawing, quat_ConvertToMatrix (engine twin src/sdk/ltquatbase.cpp), a BSP segment walk, three
// colour tables, and the pixelformat object (engine twin src/shared/pixelformat.cpp).  Several original objects: size
// objects (packed COMDATs, no padding), so FLAGS /O1 /Ob2 for all of them.
#include <windows.h>
#include <string.h>
#include <mmsystem.h>
#include "d3dren/lightmap.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/d3dstate.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/pool.h"
#include "d3dren/polydraw.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/viewparams.h"
#include "d3dren/tlvertex.h"
#include "visquery.h"
#include "ltquatbase.h"
#include <math.h>
#include "world_tree.h"

// ---- queued world polygon drawing (the polygons of lightmapped surfaces are queued per texture by QueueLightmappedPoly) --------------
// The queued polys' texture: node -> poly -> surface -> SharedTexture.
#define BUCKET_TEXTURE(pBucket)	(((Surface *)((WorldPoly *)(pBucket)->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture)

// ---- pixelformat (engine twin: src/shared/pixelformat.cpp; Jupiter runtime/shared/src/pixelformat.cpp) --------------------------------
// Where the code is emitted: function templates (Convert1Pass/Convert2Pass/ConvertDXTGeneric) are NOT inlined at /O1 and their instances land
// after the last ordinary function of the object, every out-of-line copy of an inline member (BaseBFToAny::Init, CC_*::DoConvert, or_cpy) right
// after its first caller; so the source order below is not the address order.

#define SRC_8	(*pSrc)
#define SRC_16	(*((uint16*)pSrc))
#define SRC_32	(*((uint32*)pSrc))
#define DEST_8	(*pDest)
#define DEST_16	(*((uint16*)pDest))
#define DEST_32	(*((uint32*)pDest))

#define ALPHAVAL abstract.m_AlphaValues

#define READROW_NORMAL(index, startOffset)\
	A::Or(pDestPos, 0, ALPHAVAL[(alphaData[index] >> (startOffset+0)) & 0x7]);\
	A::Or(pDestPos, 1, ALPHAVAL[(alphaData[index] >> (startOffset+3)) & 0x7]);\
	A::Or(pDestPos, 2, ALPHAVAL[(alphaData[index] >> (startOffset+6)) & 0x7]);\
	A::Or(pDestPos, 3, ALPHAVAL[(alphaData[index] >> (startOffset+9)) & 0x7]);\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

#define DECODE_LINE(lineShiftAmt)\
	A::Set(pDestPos, 0, abstract.m_Ident[(blockData>>(lineShiftAmt+0)) & 3]);\
	A::Set(pDestPos, 1, abstract.m_Ident[(blockData>>(lineShiftAmt+2)) & 3]);\
	A::Set(pDestPos, 2, abstract.m_Ident[(blockData>>(lineShiftAmt+4)) & 3]);\
	A::Set(pDestPos, 3, abstract.m_Ident[(blockData>>(lineShiftAmt+6)) & 3]);\
	pDestPos = (((uint8*)pDestPos) + pRequest->m_DestPitch);

#define DECODE_ALPHA_2ROWS() \
	DECODE_ALPHA(0, 0)\
	DECODE_ALPHA(4, 1)\
	DECODE_ALPHA(8, 2)\
	DECODE_ALPHA(12, 3)\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;\
	DECODE_ALPHA(16, 0)\
	DECODE_ALPHA(20, 1)\
	DECODE_ALPHA(24, 2)\
	DECODE_ALPHA(28, 3)\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

#define DECODE_ALPHA(shift, iPixel)\
	A::Mask(pDestPos, iPixel, invAlphaMask);\
	A::Or(pDestPos, iPixel, abstract.m_AlphaValues[(blockData>>shift) & 15]);

// (PValue_Set makes this a dynamic initializer.)
// FUNCTION: D3DREN 0x10035de7 _$E2
// FUNCTION: D3DREN 0x10035dec _$E1
// GLOBAL: D3DREN 0x10092368
static uint32 g_FullAlphaValues[16] =
{
	PValue_Set(0, 0, 0, 0), PValue_Set(17, 0, 0, 0), PValue_Set(34, 0, 0, 0), PValue_Set(51, 0, 0, 0),
	PValue_Set(68, 0, 0, 0), PValue_Set(85, 0, 0, 0), PValue_Set(102, 0, 0, 0), PValue_Set(119, 0, 0, 0),
	PValue_Set(136, 0, 0, 0), PValue_Set(153, 0, 0, 0), PValue_Set(170, 0, 0, 0), PValue_Set(187, 0, 0, 0),
	PValue_Set(204, 0, 0, 0), PValue_Set(221, 0, 0, 0), PValue_Set(238, 0, 0, 0), PValue_Set(255, 0, 0, 0)
};


// These abstract out uint16/uint32 differences for certain routines.
class Abstract_Word
{
public:
	static uint32	GetShift()	{return 1;}
	static void		Set(void *pDest, uint32 index, uint16 val)
	{
		((uint16*)pDest)[index] = val;
	}
	static void		Mask(void *pDest, uint32 index, uint32 mask)
	{
		((uint16*)pDest)[index] &= (uint16)mask;
	}
	static void		Or(void *pDest, uint32 index, uint16 mask)
	{
		((uint16*)pDest)[index] |= mask;
	}

	uint16	m_Ident[4];
	uint16	m_AlphaValues[16];
};


class Abstract_DWord
{
public:
	static uint32	GetShift()	{return 2;}
	static void		Set(void *pDest, uint32 index, uint32 val)
	{
		((uint32*)pDest)[index] = val;
	}
	static void		Mask(void *pDest, uint32 index, uint32 mask)
	{
		((uint32*)pDest)[index] &= mask;
	}
	static void		Or(void *pDest, uint32 index, uint32 mask)
	{
		((uint32*)pDest)[index] |= mask;
	}

	uint32	m_Ident[4];
	uint32	m_AlphaValues[16];
};


// ------------------------------------------------------------------------------ //
// The conversion classes.
// ------------------------------------------------------------------------------ //

class CC_8PtoBF
{
public:

	void Init(const FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
	{
		m_pSrcPalette = pRequest->m_pSrcPalette;
	}

	// FUNCTION: D3DREN 0x10036b02 ?DoConvert@CC_8PtoBF@@QAEKPAE@Z
	uint32 DoConvert(uint8 *pSrc)
	{
		return ((uint32)m_pSrcPalette[*pSrc].rgb.r << 16) |
			((uint32)m_pSrcPalette[*pSrc].rgb.g << 8) |
			((uint32)m_pSrcPalette[*pSrc].rgb.b);
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_32 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_32 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint8);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint32);}

	const RPaletteColor	*m_pSrcPalette;
};


class BaseAnyToBF
{
public:

	void Init(const FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
	{
		Init(pFormatMgr, pRequest->m_pSrcFormat);
	}

	// FUNCTION: D3DREN 0x10036488 ?Init@BaseAnyToBF@@QAEXPBVFormatMgr@@PBVPFormat@@@Z
	void Init(const FormatMgr *pFormatMgr, const PFormat *pSrcFormat)
	{
		m_pScaleMaps[0] = pFormatMgr->m_ScaleTo8[pSrcFormat->m_nBits[0]];
		m_pScaleMaps[1] = pFormatMgr->m_ScaleTo8[pSrcFormat->m_nBits[1]];
		m_pScaleMaps[2] = pFormatMgr->m_ScaleTo8[pSrcFormat->m_nBits[2]];
		m_pScaleMaps[3] = pFormatMgr->m_ScaleTo8[pSrcFormat->m_nBits[3]];
		m_pSrcFormat = pSrcFormat;
	}

	const PFormat	*m_pSrcFormat;
	const uint8		*m_pScaleMaps[NUM_COLORPLANES];
};


class CC_8toBF : public BaseAnyToBF
{
public:

	// FUNCTION: D3DREN 0x100364cb ?DoConvert@CC_8toBF@@QAEKPAE@Z
	uint32 DoConvert(uint8 *pSrc)
	{
		uint32 dest[4];

		dest[0] = m_pScaleMaps[0][(SRC_8 & m_pSrcFormat->m_Masks[0]) >> m_pSrcFormat->m_FirstBits[0]];
		dest[1] = m_pScaleMaps[1][(SRC_8 & m_pSrcFormat->m_Masks[1]) >> m_pSrcFormat->m_FirstBits[1]];
		dest[2] = m_pScaleMaps[2][(SRC_8 & m_pSrcFormat->m_Masks[2]) >> m_pSrcFormat->m_FirstBits[2]];
		dest[3] = m_pScaleMaps[3][(SRC_8 & m_pSrcFormat->m_Masks[3]) >> m_pSrcFormat->m_FirstBits[3]];

		return (dest[CP_ALPHA] << 24) | (dest[CP_RED] << 16) | (dest[CP_GREEN] << 8) | dest[CP_BLUE];
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_32 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_32 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint8);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint32);}
};


class CC_16toBF : public BaseAnyToBF
{
public:

	// FUNCTION: D3DREN 0x10036568 ?DoConvert@CC_16toBF@@QAEKPAE@Z
	uint32 DoConvert(uint8 *pSrc)
	{
		uint32 dest[4];

		dest[0] = m_pScaleMaps[0][(SRC_16 & m_pSrcFormat->m_Masks[0]) >> m_pSrcFormat->m_FirstBits[0]];
		dest[1] = m_pScaleMaps[1][(SRC_16 & m_pSrcFormat->m_Masks[1]) >> m_pSrcFormat->m_FirstBits[1]];
		dest[2] = m_pScaleMaps[2][(SRC_16 & m_pSrcFormat->m_Masks[2]) >> m_pSrcFormat->m_FirstBits[2]];
		dest[3] = m_pScaleMaps[3][(SRC_16 & m_pSrcFormat->m_Masks[3]) >> m_pSrcFormat->m_FirstBits[3]];

		return (dest[CP_ALPHA] << 24) | (dest[CP_RED] << 16) | (dest[CP_GREEN] << 8) | dest[CP_BLUE];
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_32 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_32 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint16);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint32);}
};


class CC_32toBF : public BaseAnyToBF
{
public:
	// FUNCTION: D3DREN 0x10036605 ?DoConvert@CC_32toBF@@QAEKPAE@Z
	uint32 DoConvert(uint8 *pSrc)
	{
		uint32 dest[4];

		dest[0] = (SRC_32 >> m_pSrcFormat->m_FirstBits[0]) & 0xFF;
		dest[1] = (SRC_32 >> m_pSrcFormat->m_FirstBits[1]) & 0xFF;
		dest[2] = (SRC_32 >> m_pSrcFormat->m_FirstBits[2]) & 0xFF;
		dest[3] = (SRC_32 >> m_pSrcFormat->m_FirstBits[3]) & 0xFF;

		return (dest[CP_ALPHA] << 24) | (dest[CP_RED] << 16) | (dest[CP_GREEN] << 8) | dest[CP_BLUE];
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_32 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_32 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint32);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint32);}
};


class CC_32PtoBF
{
public:

	void Init(const FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
	{
		m_pSrcPalette = pRequest->m_pSrcPalette;
	}

	// FUNCTION: D3DREN 0x10037704 ?DoConvert@CC_32PtoBF@@QAEKPAE@Z
	uint32 DoConvert(uint8 *pSrc)
	{
		return	((uint32)m_pSrcPalette[*pSrc].rgb.a << 24) |
				((uint32)m_pSrcPalette[*pSrc].rgb.r << 16) |
				((uint32)m_pSrcPalette[*pSrc].rgb.g << 8)  |
				((uint32)m_pSrcPalette[*pSrc].rgb.b);
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_32 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_32 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint8);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint32);}

	const RPaletteColor *m_pSrcPalette;
};


class BaseBFToAny
{
public:

	void Init(const FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
	{
		Init(pFormatMgr, pRequest->m_pDestFormat);
	}

	// FUNCTION: D3DREN 0x100362a9 ?Init@BaseBFToAny@@QAEXPBVFormatMgr@@PBVPFormat@@@Z
	void Init(const FormatMgr *pFormatMgr, const PFormat *pDestFormat)
	{
		m_pDestFormat = pDestFormat;
		m_pScaleMaps[0] = &pFormatMgr->m_ScaleFrom8[m_pDestFormat->m_nBits[0]];
		m_pScaleMaps[1] = &pFormatMgr->m_ScaleFrom8[m_pDestFormat->m_nBits[1]];
		m_pScaleMaps[2] = &pFormatMgr->m_ScaleFrom8[m_pDestFormat->m_nBits[2]];
		m_pScaleMaps[3] = &pFormatMgr->m_ScaleFrom8[m_pDestFormat->m_nBits[3]];
	}

	const ScaleFrom8Table	*m_pScaleMaps[NUM_COLORPLANES];
	const PFormat			*m_pDestFormat;
};


class CC_BFto8 : public BaseBFToAny
{
public:

	// FUNCTION: D3DREN 0x100362ec ?DoConvert@CC_BFto8@@QAEEPAE@Z
	uint8 DoConvert(uint8 *pSrc)
	{
		uint8 ret;

		ret =  (*m_pScaleMaps[0])[SRC_32 >> 24] << m_pDestFormat->m_FirstBits[0];
		ret |= (*m_pScaleMaps[1])[(SRC_32 >> 16) & 0xFF] << m_pDestFormat->m_FirstBits[1];
		ret |= (*m_pScaleMaps[2])[(SRC_32 >>  8) & 0xFF] << m_pDestFormat->m_FirstBits[2];
		ret |= (*m_pScaleMaps[3])[SRC_32 & 0xFF] << m_pDestFormat->m_FirstBits[3];

		return ret;
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_8 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_8 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint32);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint8);}
};


class CC_BFto16 : public BaseBFToAny
{
public:

	// FUNCTION: D3DREN 0x10036372 ?DoConvert@CC_BFto16@@QAEGPAE@Z
	uint16 DoConvert(uint8 *pSrc)
	{
		uint16 ret;

		ret  = (uint16)(*m_pScaleMaps[0])[SRC_32 >> 24] << m_pDestFormat->m_FirstBits[0];
		ret |= (uint16)(*m_pScaleMaps[1])[(SRC_32 >> 16) & 0xFF] << m_pDestFormat->m_FirstBits[1];
		ret |= (uint16)(*m_pScaleMaps[2])[(SRC_32 >>  8) & 0xFF] << m_pDestFormat->m_FirstBits[2];
		ret |= (uint16)(*m_pScaleMaps[3])[SRC_32 & 0xFF] << m_pDestFormat->m_FirstBits[3];

		return ret;
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_16 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_16 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint32);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint16);}
};


class CC_BFto32 : public BaseBFToAny
{
public:

	// FUNCTION: D3DREN 0x100363ff ?DoConvert@CC_BFto32@@QAEKPAE@Z
	uint32 DoConvert(uint8 *pSrc)
	{
		uint32 ret;

		ret  = (uint32)(*m_pScaleMaps[0])[SRC_32 >> 24] << m_pDestFormat->m_FirstBits[0];
		ret |= (uint32)(*m_pScaleMaps[1])[(SRC_32 >> 16) & 0xFF] << m_pDestFormat->m_FirstBits[1];
		ret |= (uint32)(*m_pScaleMaps[2])[(SRC_32 >>  8) & 0xFF] << m_pDestFormat->m_FirstBits[2];
		ret |= (uint32)(*m_pScaleMaps[3])[SRC_32 & 0xFF] << m_pDestFormat->m_FirstBits[3];

		return ret;
	}

	void Convert(uint8 *pSrc, uint8 *pDest)		{ DEST_32 = DoConvert(pSrc); }
	void ConvertOr(uint8 *pSrc, uint8 *pDest)	{ DEST_32 |= DoConvert(pSrc); }

	void IncSrc(uint8* &pPos) {pPos += sizeof(uint32);}
	void IncDest(uint8* &pPos) {pPos += sizeof(uint32);}
};



// ------------------------------------------------------------------------------ //
// Conversion functions.
// ------------------------------------------------------------------------------ //

// FUNCTION: D3DREN 0x10035fe7 ?or_cpy@@YAXPAE0KKK@Z
inline void or_cpy(uint8 *pSrc, uint8 *pDest, uint32 nDWords, uint32 nWords, uint32 nBytes)
{
	uint32 count;

	count = nDWords;
	while (count) {
		--count;
		DEST_32 |= SRC_32;
		pSrc  += sizeof(uint32);
		pDest += sizeof(uint32); }

	count = nWords;
	while (count) {
		--count;
		DEST_16 |= SRC_16;
		pDest += sizeof(uint16);
		pSrc  += sizeof(uint16); }

	count = nBytes;
	while (count) {
		--count;
		*pDest |= *pSrc;
		++pSrc; ++pDest; }
}

// FUNCTION: D3DREN 0x10035e8a
LTRESULT GenericCopy(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	uint8 *pSrcLine, *pDestLine;
	uint32 count, bytesPerLine;
	uint32 copySize, nDWords, nWords, nBytes;

	bytesPerLine = pRequest->m_Width << pRequest->m_pSrcFormat->GetBytesPerPixelShift();
	pSrcLine = pRequest->m_pSrc;
	pDestLine = pRequest->m_pDest;
	count = pRequest->m_Height;

	if(pRequest->m_pSrcFormat->IsCompressed() != pRequest->m_pDestFormat->IsCompressed())
		return LT_ERROR;

	if(pRequest->m_pSrcFormat->IsCompressed())
	{
		if(pRequest->m_Flags & CONVERT_OR)
			return LT_ERROR;

		copySize = CalcImageSize(pRequest->m_pSrcFormat->m_eType, pRequest->m_Width, pRequest->m_Height);
		memcpy(pRequest->m_pDest, pRequest->m_pSrc, copySize);
		return LT_OK;
	}

	if(pRequest->m_Flags & CONVERT_OR)
	{
		nDWords = nWords = 0;
		if(!((uint32)pSrcLine & 3) && !((uint32)pDestLine & 3))
		{
			nDWords = bytesPerLine >> 2;
			nBytes = bytesPerLine - (nDWords << 2);
		}
		else if(!((uint32)pSrcLine & 1) && !((uint32)pDestLine & 1))
		{
			nWords = bytesPerLine >> 1;
			nBytes = bytesPerLine - (nWords << 1);
		}
		else
		{
			nBytes = bytesPerLine;
		}

		while (count)
		{
			count--;
			or_cpy(pDestLine, pSrcLine, nDWords, nWords, nBytes);
			pSrcLine  += pRequest->m_SrcPitch;
			pDestLine += pRequest->m_DestPitch;
		}
	}
	else
	{
		while (count)
		{
			count--;
			memcpy(pDestLine, pSrcLine, bytesPerLine);
			pSrcLine  += pRequest->m_SrcPitch;
			pDestLine += pRequest->m_DestPitch;
		}
	}

	return LT_OK;
}


// This function converts from the source format to the generic 32-bit format
// through the converter you specify.
// FUNCTION: D3DREN 0x10036bd7 ?Convert1Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8PtoBF@@@Z
// FUNCTION: D3DREN 0x10036ea6 ?Convert1Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8toBF@@@Z
// FUNCTION: D3DREN 0x10037185 ?Convert1Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_16toBF@@@Z
// FUNCTION: D3DREN 0x100372d9 ?Convert1Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_BFto8@@@Z
// FUNCTION: D3DREN 0x10037422 ?Convert1Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_BFto16@@@Z
template<class C>
LTRESULT Convert1Pass(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest, C *pConverterClass)
{
	uint8 *pSrcLine, *pDestLine, *pSrcPos, *pDestPos;
	uint32 yCount, xCount;
	C converter;

	converter.Init(pFormatMgr, pRequest);

	pSrcLine = pRequest->m_pSrc;
	pDestLine = pRequest->m_pDest;
	yCount = pRequest->m_Height;
	while (yCount)
	{
		--yCount;

		pSrcPos = pSrcLine;
		pDestPos = pDestLine;
		xCount = pRequest->m_Width;

		if(pRequest->m_Flags & CONVERT_OR)
		{
			while (xCount)
			{
				--xCount;
				converter.ConvertOr(pSrcPos, pDestPos);
				converter.IncSrc(pSrcPos);
				converter.IncDest(pDestPos);
			}
		}
		else
		{
			while (xCount)
			{
				--xCount;
				converter.Convert(pSrcPos, pDestPos);
				converter.IncSrc(pSrcPos);
				converter.IncDest(pDestPos);
			}
		}

		pSrcLine  += pRequest->m_SrcPitch;
		pDestLine += pRequest->m_DestPitch;
	}

	return LT_OK;
}

// This function converts from the source format to the generic 32-bit format, then
// to the destination format through the converters you specify.
// FUNCTION: D3DREN 0x10036a34 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8PtoBF@@PAVCC_BFto8@@@Z
// FUNCTION: D3DREN 0x10036b22 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8PtoBF@@PAVCC_BFto16@@@Z
// FUNCTION: D3DREN 0x10036c60 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8PtoBF@@PAVCC_BFto32@@@Z
// FUNCTION: D3DREN 0x10036d15 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8toBF@@PAVCC_BFto8@@@Z
// FUNCTION: D3DREN 0x10036dea ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8toBF@@PAVCC_BFto16@@@Z
// FUNCTION: D3DREN 0x10036f36 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_8toBF@@PAVCC_BFto32@@@Z
// FUNCTION: D3DREN 0x10036ff2 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_16toBF@@PAVCC_BFto8@@@Z
// FUNCTION: D3DREN 0x100370ac ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_16toBF@@PAVCC_BFto16@@@Z
// FUNCTION: D3DREN 0x10037217 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_16toBF@@PAVCC_BFto32@@@Z
// FUNCTION: D3DREN 0x10037368 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_32toBF@@PAVCC_BFto8@@@Z
// FUNCTION: D3DREN 0x100374b5 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_32toBF@@PAVCC_BFto16@@@Z
// FUNCTION: D3DREN 0x10037577 ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_32toBF@@PAVCC_BFto32@@@Z
// FUNCTION: D3DREN 0x1003764f ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_32PtoBF@@PAVCC_BFto16@@@Z
// FUNCTION: D3DREN 0x1003772d ?Convert2Pass@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_32PtoBF@@PAVCC_BFto32@@@Z
template<class S, class D>
LTRESULT Convert2Pass(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest, S *pSrcTo32Bit, D *p32BitToDest)
{
	uint8 *pSrcLine, *pDestLine, *pSrcPos, *pDestPos;
	uint32 yCount, xCount;
	S srcConvert;
	D destConvert;
	uint32 tempPixel;

	srcConvert.Init(pFormatMgr, pRequest);
	destConvert.Init(pFormatMgr, pRequest);

	pSrcLine = pRequest->m_pSrc;
	pDestLine = pRequest->m_pDest;
	yCount = pRequest->m_Height;
	while (yCount)
	{
		--yCount;

		pSrcPos = pSrcLine;
		pDestPos = pDestLine;
		xCount = pRequest->m_Width;

		if(pRequest->m_Flags & CONVERT_OR)
		{
			while (xCount)
			{
				--xCount;
				srcConvert.Convert(pSrcPos, (uint8*)&tempPixel);
				destConvert.ConvertOr((uint8*)&tempPixel, pDestPos);
				srcConvert.IncSrc(pSrcPos);
				destConvert.IncDest(pDestPos);
			}
		}
		else
		{
			while (xCount)
			{
				--xCount;
				srcConvert.Convert(pSrcPos, (uint8*)&tempPixel);
				destConvert.Convert((uint8*)&tempPixel, pDestPos);
				srcConvert.IncSrc(pSrcPos);
				destConvert.IncDest(pDestPos);
			}
		}

		pSrcLine += pRequest->m_SrcPitch;
		pDestLine += pRequest->m_DestPitch;
	}

	return LT_OK;
}

// --------------------------------------------------------------------------------- //
// All the conversion function callbacks.
// --------------------------------------------------------------------------------- //

// FUNCTION: D3DREN 0x1003602c
LTRESULT Convert8Pto8(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return Convert2Pass(pFormatMgr, pRequest, (CC_8PtoBF*)LTNULL, (CC_BFto8*)LTNULL);
}

// FUNCTION: D3DREN 0x10036041
LTRESULT Convert8Pto16(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return Convert2Pass(pFormatMgr, pRequest, (CC_8PtoBF*)LTNULL, (CC_BFto16*)LTNULL);
}

// FUNCTION: D3DREN 0x10036056
LTRESULT Convert8Pto32(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pDestFormat->IsSameFormat(&pFormatMgr->m_32BitFormat))
	{
		return Convert1Pass(pFormatMgr, pRequest, (CC_8PtoBF*)LTNULL);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_8PtoBF*)LTNULL, (CC_BFto32*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x1003608d
LTRESULT Convert8to8(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pSrcFormat->IsSameFormat(pRequest->m_pDestFormat))
	{
		return GenericCopy(pFormatMgr, pRequest);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_8toBF*)LTNULL, (CC_BFto8*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x100360c1
LTRESULT Convert8to16(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return Convert2Pass(pFormatMgr, pRequest, (CC_8toBF*)LTNULL, (CC_BFto16*)LTNULL);
}

// FUNCTION: D3DREN 0x100360d6
LTRESULT Convert8to32(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pDestFormat->IsSameFormat(&pFormatMgr->m_32BitFormat))
	{
		return Convert1Pass(pFormatMgr, pRequest, (CC_8toBF*)LTNULL);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_8toBF*)LTNULL, (CC_BFto32*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x1003610d
LTRESULT Convert16to8(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return Convert2Pass(pFormatMgr, pRequest, (CC_16toBF*)LTNULL, (CC_BFto8*)LTNULL);
}

// FUNCTION: D3DREN 0x10036122
LTRESULT Convert16to16(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pSrcFormat->IsSameFormat(pRequest->m_pDestFormat))
	{
		return GenericCopy(pFormatMgr, pRequest);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_16toBF*)LTNULL, (CC_BFto16*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x10036156
LTRESULT Convert16to32(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pDestFormat->IsSameFormat(&pFormatMgr->m_32BitFormat))
	{
		return Convert1Pass(pFormatMgr, pRequest, (CC_16toBF*)LTNULL);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_16toBF*)LTNULL, (CC_BFto32*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x1003618d
LTRESULT Convert32to8(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pSrcFormat->IsSameFormat(&pFormatMgr->m_32BitFormat))
	{
		return Convert1Pass(pFormatMgr, pRequest, (CC_BFto8*)LTNULL);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_32toBF*)LTNULL, (CC_BFto8*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x100361c3
LTRESULT Convert32to16(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pSrcFormat->IsSameFormat(&pFormatMgr->m_32BitFormat))
	{
		return Convert1Pass(pFormatMgr, pRequest, (CC_BFto16*)LTNULL);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_32toBF*)LTNULL, (CC_BFto16*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x100361f9
LTRESULT Convert32to32(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	if(pRequest->m_pDestFormat->IsSameFormat(&pFormatMgr->m_32BitFormat))
	{
		return GenericCopy(pFormatMgr, pRequest);
	}
	else
	{
		return Convert2Pass(pFormatMgr, pRequest, (CC_32toBF*)LTNULL, (CC_BFto32*)LTNULL);
	}
}

// FUNCTION: D3DREN 0x1003622f
LTRESULT Convert32Pto16(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return Convert2Pass(pFormatMgr, pRequest, (CC_32PtoBF*)LTNULL, (CC_BFto16*)LTNULL);
}

// FUNCTION: D3DREN 0x10036244
LTRESULT Convert32Pto32(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return Convert2Pass(pFormatMgr, pRequest, (CC_32PtoBF*)LTNULL, (CC_BFto32*)LTNULL);
}


// FUNCTION: D3DREN 0x100377e2 ?ConvertDXTGeneric@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_BFto16@@PAVAbstract_Word@@@Z
// FUNCTION: D3DREN 0x10038011 ?ConvertDXTGeneric@@YAKPAVFormatMgr@@PBVFMConvertRequest@@PAVCC_BFto32@@PAVAbstract_DWord@@@Z
template<class C, class A>
LTRESULT ConvertDXTGeneric(FormatMgr *pFormatMgr,
	const FMConvertRequest *pRequest, C *pConvert, A *pAbstract)
{
	A abstract;
	C ccBFtoGeneric;
	CC_16toBF cc16toBF;
	uint32 nBlocksX, nBlocksY;
	uint32 xBlock, yBlock;
	uint32 ident32[4];
	uint32 comp[2][4];
	uint8 *pSrcPos8;
	uint16 *pSrcPos16;
	void *pDestPos;
	uint16 val1, val2;
	int i;	// signed: with uint32 the g_FullAlphaValues copy loop is not strength-reduced; the exe's is (pointer loop, 'jl')
	uint32 blockData, bytesPerBlockShift, alphaExtra, invAlphaMask;
	LTBOOL bAlpha, bInterpolatedAlpha;
	uint32 tempIndex;
	uint32 alphaData[2], alphaShift, defaultPValueAlphaMask, defaultByteAlphaMask;
	uint8 *pAlphaScaleTable;


	// Will we be decompressing with alpha?
	defaultPValueAlphaMask = PVALUE_ALPHAMASK;
	defaultByteAlphaMask = 0xFF;
	bAlpha = bInterpolatedAlpha = LTFALSE;
	if(pRequest->m_pSrcFormat->m_eType == BPP_S3TC_DXT3)
	{
		bAlpha = LTTRUE;
	}
	else if(pRequest->m_pSrcFormat->m_eType == BPP_S3TC_DXT5)
	{
		bAlpha = bInterpolatedAlpha = LTTRUE;

		alphaShift = pRequest->m_pDestFormat->m_FirstBits[CP_ALPHA];
		pAlphaScaleTable = pFormatMgr->m_ScaleFrom8[
			pRequest->m_pDestFormat->m_nBits[CP_ALPHA]];

		defaultByteAlphaMask = defaultPValueAlphaMask = 0;
	}

	invAlphaMask = ~pRequest->m_pDestFormat->m_Masks[CP_ALPHA];

	cc16toBF.Init(pFormatMgr, &pFormatMgr->m_RGB565Format);
	ccBFtoGeneric.Init(pFormatMgr, pRequest->m_pDestFormat);

	if(bAlpha)
	{
		bytesPerBlockShift = 4;
		alphaExtra = 8; // 8 bytes of alpha data.

		for(i=0; i < 16; i++)
		{
			ccBFtoGeneric.Convert((uint8*)&g_FullAlphaValues[i], (uint8*)&abstract.m_AlphaValues[i]);
		}
	}
	else
	{
		bytesPerBlockShift = 3;
		alphaExtra = 0;
	}

	nBlocksX = pRequest->m_Width >> 2;
	nBlocksY = pRequest->m_Height >> 2;

	// For each block...
	for(yBlock=0; yBlock < nBlocksY; yBlock++)
	{
		for(xBlock=0; xBlock < nBlocksX; xBlock++)
		{
			pSrcPos8 = pRequest->m_pSrc + (xBlock<<bytesPerBlockShift) + ((yBlock*nBlocksX)<<bytesPerBlockShift);
			pSrcPos8 += alphaExtra;
			pSrcPos16 = (uint16*)pSrcPos8;

			// 16-bit 565 values.
			val1 = *pSrcPos16;
			val2 = *(pSrcPos16 + 1);

			// Convert to base format.
			cc16toBF.Convert((uint8*)&val1, (uint8*)&ident32[0]);
			cc16toBF.Convert((uint8*)&val2, (uint8*)&ident32[1]);

			// Get the components.
			PValue_Get(ident32[0], comp[0][0], comp[0][1], comp[0][2], comp[0][3]);
			PValue_Get(ident32[1], comp[1][0], comp[1][1], comp[1][2], comp[1][3]);

			ident32[0] |= defaultPValueAlphaMask;
			ident32[1] |= defaultPValueAlphaMask;

			// Convert to output format.
			if(val1 > val2)
			{
				// 4-color block, alpha is opaque.
				ident32[2] = PValue_Set(
					defaultByteAlphaMask,
					(comp[0][1]*2 + comp[1][1]) / 3,
					(comp[0][2]*2 + comp[1][2]) / 3,
					(comp[0][3]*2 + comp[1][3]) / 3);

				ident32[3] = PValue_Set(
					defaultByteAlphaMask,
					(comp[0][1] + comp[1][1]*2) / 3,
					(comp[0][2] + comp[1][2]*2) / 3,
					(comp[0][3] + comp[1][3]*2) / 3);
			}
			else
			{
				// 3-color block, last color is translucent alpha.
				ident32[2] = PValue_Set(
					defaultByteAlphaMask,
					(comp[0][1] + comp[1][1]) >> 1,
					(comp[0][2] + comp[1][2]) >> 1,
					(comp[0][3] + comp[1][3]) >> 1);

				ident32[3] = 0;
			}

			ccBFtoGeneric.Convert((uint8*)&ident32[0], (uint8*)&abstract.m_Ident[0]);
			ccBFtoGeneric.Convert((uint8*)&ident32[1], (uint8*)&abstract.m_Ident[1]);
			ccBFtoGeneric.Convert((uint8*)&ident32[2], (uint8*)&abstract.m_Ident[2]);
			ccBFtoGeneric.Convert((uint8*)&ident32[3], (uint8*)&abstract.m_Ident[3]);

			// The next 4 bytes are the pixel data.
			blockData = *((uint32*)(pSrcPos8 + 4));
			pDestPos = pRequest->m_pDest +
				((yBlock<<2) * pRequest->m_DestPitch) + (xBlock<<(2+A::GetShift()));

			DECODE_LINE(0);
			DECODE_LINE(8);
			DECODE_LINE(16);
			DECODE_LINE(24);

			// Read in the alpha block?
			if(bAlpha)
			{
				pDestPos = pRequest->m_pDest +
					((yBlock<<2) * pRequest->m_DestPitch) + (xBlock<<(2+A::GetShift()));

				pSrcPos8 = pRequest->m_pSrc + (xBlock<<bytesPerBlockShift) + ((yBlock*nBlocksX)<<bytesPerBlockShift);

				if(bInterpolatedAlpha)
				{
					// 2 bytes for the alpha values.
					ALPHAVAL[0] = *pSrcPos8;
					ALPHAVAL[1] = *(pSrcPos8+1);

					if(ALPHAVAL[0] > ALPHAVAL[1])
					{
						// 8 values going between these alpha values.
						ALPHAVAL[2] = (ALPHAVAL[0]*6 + ALPHAVAL[1]*1) / 7;
						ALPHAVAL[3] = (ALPHAVAL[0]*5 + ALPHAVAL[1]*2) / 7;
						ALPHAVAL[4] = (ALPHAVAL[0]*4 + ALPHAVAL[1]*3) / 7;
						ALPHAVAL[5] = (ALPHAVAL[0]*3 + ALPHAVAL[1]*4) / 7;
						ALPHAVAL[6] = (ALPHAVAL[0]*2 + ALPHAVAL[1]*5) / 7;
						ALPHAVAL[7] = (ALPHAVAL[0]*1 + ALPHAVAL[1]*6) / 7;
					}
					else
					{
						// 6 values going between these alpha values.  The others are 0 and 0xFF.
						ALPHAVAL[2] = (ALPHAVAL[0]*4 + ALPHAVAL[1]*1) / 5;
						ALPHAVAL[3] = (ALPHAVAL[0]*3 + ALPHAVAL[1]*2) / 5;
						ALPHAVAL[4] = (ALPHAVAL[0]*2 + ALPHAVAL[1]*3) / 5;
						ALPHAVAL[5] = (ALPHAVAL[0]*1 + ALPHAVAL[1]*4) / 5;
						ALPHAVAL[6] = 0;
						ALPHAVAL[7] = 0xFF;
					}

					// Put them in the dest format.
					ALPHAVAL[0] = pAlphaScaleTable[ALPHAVAL[0]] << alphaShift;
					ALPHAVAL[1] = pAlphaScaleTable[ALPHAVAL[1]] << alphaShift;
					ALPHAVAL[2] = pAlphaScaleTable[ALPHAVAL[2]] << alphaShift;
					ALPHAVAL[3] = pAlphaScaleTable[ALPHAVAL[3]] << alphaShift;
					ALPHAVAL[4] = pAlphaScaleTable[ALPHAVAL[4]] << alphaShift;
					ALPHAVAL[5] = pAlphaScaleTable[ALPHAVAL[5]] << alphaShift;
					ALPHAVAL[6] = pAlphaScaleTable[ALPHAVAL[6]] << alphaShift;
					ALPHAVAL[7] = pAlphaScaleTable[ALPHAVAL[7]] << alphaShift;

					// 6 bytes for the pixels (3 bits per pixel, 16 pixels).
					alphaData[0] = *((uint32*)(pSrcPos8+2));
					alphaData[1] = *((uint16*)(pSrcPos8+6));

					// Row 1.
						READROW_NORMAL(0, 0);

					// Row 2.
						READROW_NORMAL(0, 12);

					// Row 3.
						A::Or(pDestPos, 0, ALPHAVAL[(alphaData[0] >> 24) & 0x7]);
						A::Or(pDestPos, 1, ALPHAVAL[(alphaData[0] >> 27) & 0x7]);

						// As luck would have it, one of the pixels spans the border.
						tempIndex = alphaData[0] >> 30; // Get the 2 LSoBs.
						tempIndex |= (alphaData[1] & 0x1) << 2; // The MSoB.
						A::Or(pDestPos, 2, ALPHAVAL[tempIndex]);

						A::Or(pDestPos, 3, ALPHAVAL[(alphaData[1] >> 1) & 0x7]);
						pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

					// Row 4.
						READROW_NORMAL(1, 4);
				}
				else
				{
					// 2 rows worth.
					blockData = *((uint32*)pSrcPos8);
					DECODE_ALPHA_2ROWS();

					blockData = *((uint32*)(pSrcPos8 + 4));
					DECODE_ALPHA_2ROWS();
				}
			}
		}
	}

	return LT_OK;
}


// FUNCTION: D3DREN 0x10036259
LTRESULT ConvertDXTto16(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return ConvertDXTGeneric(pFormatMgr, pRequest, (CC_BFto16*)LTNULL, (Abstract_Word*)LTNULL);
}

// FUNCTION: D3DREN 0x1003626e
LTRESULT ConvertDXTto32(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest)
{
	return ConvertDXTGeneric(pFormatMgr, pRequest, (CC_BFto32*)LTNULL, (Abstract_DWord*)LTNULL);
}


// --------------------------------------------------------------------------------- //
// Pixel color convert functions.
// --------------------------------------------------------------------------------- //

// Takes a pixel in the base format and converts to D's format.
template<class D>
inline void CVP_FromPValueTemplate(FormatMgr *pFormatMgr,
	PFormat *pFormat, uint8 *pSrc, uint8 *pDest, D *p32BitToDest)
{
	D destConvert;

	destConvert.Init(pFormatMgr, pFormat);
	destConvert.Convert(pSrc, pDest);
}

// FUNCTION: D3DREN 0x10036283
void CPV_BFto8(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest)
{
	CVP_FromPValueTemplate(pFormatMgr, pFormat, pSrc, pDest, (CC_BFto8*)LTNULL);
}

// FUNCTION: D3DREN 0x1003634b
void CPV_BFto16(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest)
{
	CVP_FromPValueTemplate(pFormatMgr, pFormat, pSrc, pDest, (CC_BFto16*)LTNULL);
}

// FUNCTION: D3DREN 0x100363d9
void CPV_BFto32(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest)
{
	CVP_FromPValueTemplate(pFormatMgr, pFormat, pSrc, pDest, (CC_BFto32*)LTNULL);
}


template<class D>
inline void CVP_ToPValueTemplate(FormatMgr *pFormatMgr,
	PFormat *pFormat, uint8 *pSrc, uint8 *pDest, D *pToPValue)
{
	D destConvert;

	destConvert.Init(pFormatMgr, pFormat);
	destConvert.Convert(pSrc, pDest);
}

// FUNCTION: D3DREN 0x10036462
void CPV_8toBF(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest)
{
	CVP_ToPValueTemplate(pFormatMgr, pFormat, pSrc, pDest, (CC_8toBF*)LTNULL);
}

// FUNCTION: D3DREN 0x10036542
void CPV_16toBF(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest)
{
	CVP_ToPValueTemplate(pFormatMgr, pFormat, pSrc, pDest, (CC_16toBF*)LTNULL);
}

// FUNCTION: D3DREN 0x100365df
void CPV_32toBF(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest)
{
	CVP_ToPValueTemplate(pFormatMgr, pFormat, pSrc, pDest, (CC_32toBF*)LTNULL);
}


// (Initialised data lands in the exe in definition order: these two tables precede the function tables.)
// log2 of the bytes per pixel for each format type.
// GLOBAL: D3DREN 0x1004bf78
uint32 g_PixelBytesShift[NUM_BIT_TYPES] = {0, 0, 1, 2, 0, 0, 0, 0};

// The bytes per pixel for each format type (0 if not applicable).
// GLOBAL: D3DREN 0x1004bf98
uint32 g_PixelBytes[NUM_BIT_TYPES] = {1, 1, 2, 4, 0, 0, 0, 1};

// ------------------------------------------------------------------------------ //
// Function tables.
// ------------------------------------------------------------------------------ //
typedef LTRESULT (*ConvertPixelsFn)(FormatMgr *pFormatMgr, const FMConvertRequest *pRequest);
typedef void (*ConvertPValueFn)(FormatMgr *pFormatMgr, PFormat *pFormat, uint8 *pSrc, uint8 *pDest);

// GLOBAL: D3DREN 0x1004bfb8
ConvertPixelsFn g_ConvertPixelsFns[NUM_BIT_TYPES][NUM_BIT_TYPES] =
{
	GenericCopy,	Convert8Pto8,	Convert8Pto16,	Convert8Pto32,	LTNULL,		LTNULL,		LTNULL,			LTNULL,
	LTNULL,			Convert8to8,	Convert8to16,	Convert8to32,	LTNULL,		LTNULL,		LTNULL,			LTNULL,
	LTNULL,			Convert16to8,	Convert16to16,	Convert16to32,	LTNULL,		LTNULL,		LTNULL,			LTNULL,
	LTNULL,			Convert32to8,	Convert32to16,	Convert32to32,	LTNULL,		LTNULL,		LTNULL,			LTNULL,
	LTNULL,			LTNULL,			ConvertDXTto16, ConvertDXTto32,	GenericCopy,LTNULL,		LTNULL,			LTNULL,
	LTNULL,			LTNULL,			ConvertDXTto16, ConvertDXTto32,	LTNULL,		GenericCopy,LTNULL,			LTNULL,
	LTNULL,			LTNULL,			ConvertDXTto16, ConvertDXTto32,	LTNULL,		LTNULL,		GenericCopy,	LTNULL,
	LTNULL,			LTNULL,			Convert32Pto16,	Convert32Pto32,	LTNULL,		LTNULL,		LTNULL,			GenericCopy
};

// Base-format to any.
// GLOBAL: D3DREN 0x1004c0b8
ConvertPValueFn g_ConvertFromPValueFns[NUM_BIT_TYPES] =
{
	LTNULL, CPV_BFto8, CPV_BFto16, CPV_BFto32, LTNULL, LTNULL, LTNULL, LTNULL
};

// GLOBAL: D3DREN 0x1004c0d8
ConvertPValueFn g_ConvertToPValueFns[NUM_BIT_TYPES] =
{
	LTNULL, CPV_8toBF, CPV_16toBF, CPV_32toBF, LTNULL, LTNULL, LTNULL, LTNULL
};



// ------------------------------------------------------------------------------ //
// PFormat.
// ------------------------------------------------------------------------------ //

// Figures out where the bits start and end.  left is the MSB, right is the LSB.
// (left will be a greater number than right).
// FUNCTION: D3DREN 0x100366bd
static void GetMaskBounds(uint32 mask, uint32 *pLeft, uint32 *pRight)
{
	uint32 testMask, i;

	// Starting with 1, find out where the mask starts.
	*pRight = 0;
	testMask = 1;
	for(i=0; i < 32; i++)
	{
		if(testMask & mask)
			break;

		testMask <<= 1;
		(*pRight)++;
	}

	// Now find where it ends.
	*pLeft = *pRight;
	for(i=0; i < 32; i++)
	{
		if(!(testMask & mask))
			break;

		testMask <<= 1;
		(*pLeft)++;
	}
}


// FUNCTION: D3DREN 0x1003668a
static void SetBitCountAndRightShift(PFormat *pFormat, uint32 iPlane)
{
	uint32 left, right;

	GetMaskBounds(pFormat->m_Masks[iPlane], &left, &right);
	pFormat->m_nBits[iPlane] = left - right;
	pFormat->m_FirstBits[iPlane] = right;
}

// FUNCTION: D3DREN 0x1003664e
void PFormat::Init(BPPIdent Type, uint32 aMask, uint32 rMask, uint32 gMask, uint32 bMask)
{
	m_eType				= Type;
	m_Masks[CP_ALPHA]	= aMask;
	m_Masks[CP_RED]		= rMask;
	m_Masks[CP_GREEN]	= gMask;
	m_Masks[CP_BLUE]	= bMask;

	for(uint32 i=0; i < NUM_COLORPLANES; i++)
	{
		SetBitCountAndRightShift(this, i);
	}
}


// FUNCTION: D3DREN 0x100366f6
void PFormat::InitPValueFormat()
{
	Init(BPP_32, PVALUE_ALPHAMASK, PVALUE_REDMASK, PVALUE_GREENMASK, PVALUE_BLUEMASK);
}

// FUNCTION: D3DREN 0x10036711
uint32 PFormat::GetBytesPerPixelShift() const
{
	return g_PixelBytesShift[m_eType];
}

// FUNCTION: D3DREN 0x1003671c
BPPIdent PFormat::GetType() const
{
	return m_eType;
}

// FUNCTION: D3DREN 0x10036720
LTBOOL PFormat::IsSameFormat(PFormat *pOther) const
{
	return
		m_eType    == pOther->m_eType &&
		m_Masks[0] == pOther->m_Masks[0] &&
		m_Masks[1] == pOther->m_Masks[1] &&
		m_Masks[2] == pOther->m_Masks[2] &&
		m_Masks[3] == pOther->m_Masks[3];
}

// FUNCTION: D3DREN 0x10036756
uint32 PFormat::GetBytesPerPixel() const
{
	return g_PixelBytes[m_eType];
}


// ------------------------------------------------------------------------------ //
// FMConvertRequest.
// ------------------------------------------------------------------------------ //

// FUNCTION: D3DREN 0x10036761
FMConvertRequest::FMConvertRequest()
{
	m_pSrcFormat = &m_DefaultSrcFormat;
	m_pDestFormat = &m_DefaultDestFormat;
	m_pSrcPalette = LTNULL;
	m_pSrc = LTNULL;
	m_SrcPitch = 0xFFFFFFFF;
	m_pDest = LTNULL;
	m_DestPitch = 0xFFFFFFFF;
	m_Width = 0xFFFFFFFF;
	m_Height = 0xFFFFFFFF;
	m_Flags = 0;
}


// FUNCTION: D3DREN 0x10036797
LTBOOL FMConvertRequest::IsValid() const
{
	if(m_pSrcFormat && m_pSrc &&
		m_pDestFormat && m_pDest &&
		m_Width != 0xFFFFFFFF && m_Height != 0xFFFFFFFF)
	{
		// (Compressed data doesn't have a pitch value).
		if(!m_pSrcFormat->IsCompressed())
		{
			if(m_SrcPitch == 0xFFFFFFFF)
				return LTFALSE;
		}

		if(!m_pDestFormat->IsCompressed())
		{
			if(m_DestPitch == 0xFFFFFFFF)
				return LTFALSE;
		}

		if(m_pSrcFormat->IsCompressed() || m_pDestFormat->IsCompressed())
		{
			// Compressed images must be 4x4 blocks.
			if((m_Width & 3) || (m_Height & 3))
			{
				return LTFALSE;
			}
		}

		// Make sure it has a palette if it's 8 bit.
		if(m_pSrcFormat->m_eType == BPP_8P)
		{
			if(!m_pSrcPalette)
				return LTFALSE;
		}

		if(m_pSrcFormat->m_eType == BPP_32P)
		{
			if(!m_pSrcPalette)
				return LTFALSE;
		}

		return LTTRUE;
	}

	return LTFALSE;
}


// ------------------------------------------------------------------------------ //
// FMRect.
// ------------------------------------------------------------------------------ //

// ------------------------------------------------------------------------------ //
// FormatMgr.
// ------------------------------------------------------------------------------ //

// FUNCTION: D3DREN 0x10036850
FormatMgr::FormatMgr()
{
	m_32BitFormat.InitPValueFormat();
	m_RGB565Format.Init(BPP_16, 0, 0xF800, 0x7E0, 0x1F);

	InitScaleTables();
}


// FUNCTION: D3DREN 0x1003689c
LTRESULT FormatMgr::ConvertPixels(const FMConvertRequest *pRequest)
{
	ConvertPixelsFn fn;

	if(!pRequest->IsValid())
	{
		return LT_ERROR;
	}

	// Do generic conversion.
	fn = g_ConvertPixelsFns[pRequest->m_pSrcFormat->GetType()][pRequest->m_pDestFormat->GetType()];
	if(!fn)
	{
		return LT_UNSUPPORTED;
	}

	return fn(this, pRequest);
}


// FUNCTION: D3DREN 0x100368dc
void FormatMgr::InitScaleTables()
{
	uint32 i, maxVal, j;

	m_ScaleTo8[0] = m_0to8;
	m_ScaleTo8[1] = m_1to8;
	m_ScaleTo8[2] = m_2to8;
	m_ScaleTo8[3] = m_3to8;
	m_ScaleTo8[4] = m_4to8;
	m_ScaleTo8[5] = m_5to8;
	m_ScaleTo8[6] = m_6to8;
	m_ScaleTo8[7] = m_7to8;
	m_ScaleTo8[8] = m_8to8;

	for(i=0; i < NUM_SCALE_TABLES; i++)
	{
		// Setup X to 8 bits.
		maxVal = (1 << i) - 1;
		for(j=0; j <= maxVal; j++)
		{
			if(maxVal == 0)
			{
				m_ScaleTo8[i][j] = 0;
			}
			else
			{
				m_ScaleTo8[i][j] = (uint8)((j * 255) / maxVal);
			}
		}

		// Setup 8 bits to X.
		for(j=0; j < 256; j++)
		{
			m_ScaleFrom8[i][j] = (uint8)((j * maxVal) / 255);
		}
	}
}


// FUNCTION: D3DREN 0x100369d0
LTRESULT FormatMgr::PValueToFormatColor(PFormat *pFormat, PValue in, GenericColor &out)
{
	if(g_ConvertFromPValueFns[pFormat->m_eType])
	{
		g_ConvertFromPValueFns[pFormat->m_eType](this, pFormat, (uint8*)&in, (uint8*)&out);
		return LT_OK;
	}
	else
	{
		return LT_ERROR;
	}
}


// FUNCTION: D3DREN 0x100369fe
uint32 CalcImageSize(BPPIdent bpp, uint32 width, uint32 height)
{
	if(IsFormatCompressed(bpp))
	{
		if(bpp == BPP_S3TC_DXT1)
			return (width * height) >> 1;
		else
			return width * height;
	}
	else
	{
		return width * height * g_PixelBytes[bpp];
	}
}

#undef SRC_8
#undef SRC_16
#undef SRC_32
#undef DEST_8
#undef DEST_16
#undef DEST_32
#undef ALPHAVAL
#undef READROW_NORMAL
#undef DECODE_LINE
#undef DECODE_ALPHA_2ROWS
#undef DECODE_ALPHA
