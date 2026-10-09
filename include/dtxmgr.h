// Texture data (Jupiter runtime/shared/src/dtxmgr.h). Talon layout recovered from dtxmgr.cpp
// (004352d0-00435960): TextureData has a vtable (SetupPFormat), no PFormat member, keeps the
// DTX sections and optional 8-bit alpha masks per mipmap.
#ifndef __DTXMGR_H__
#define __DTXMGR_H__

#include "ltbasedefs.h"
#include "pixelformat.h"

struct SharedTexture;
class PFormat;
class ILTStream;

#define DTX_COMMANDSTRING_LEN	128

// Current largest texture size allowed.
#define MAX_DTX_SIZE		1024

// Maximum number of mipmaps in a texture.
#define MAX_DTX_MIPMAPS		8

#define DTX_FULLBRITE		(1<<0)	// This DTX has fullbrite colors.
#define DTX_PREFER16BIT		(1<<1)	// Use 16-bit, even if in 32-bit mode.
#define DTX_MIPSALLOCED		(1<<2)	// Each TextureMipData has its texture data allocated.
#define DTX_SECTIONSFIXED	(1<<3)	// The sections count was screwed up originally.  This flag is set
									// in all the textures from now on when the count is fixed.
#define DTX_NOSYSCACHE		(1<<6)	// Not saved: tells it to not put the texture in the texture cache list.
#define DTX_PREFER4444		(1<<7)	// If in 16-bit mode, use a 4444 texture for this.
#define DTX_PREFER5551		(1<<8)	// Use 5551 if 16-bit.

#define CURRENT_DTX_VERSION	-5	// m_Version in the DTX header.

#define LT_RESTYPE_DTX		0


// 0xa4 bytes.
class DtxHeader
{
public:
	inline uint32	GetTextureGroup()	{return m_Extra[0];}
	inline uint32	GetNumMipmaps()		{return m_Extra[1];}

	inline BPPIdent	GetBPPIdent()				{return m_Extra[2] == 0 ? BPP_32 : (BPPIdent)m_Extra[2];}
	inline void		SetBPPIdent(BPPIdent id)	{m_Extra[2] = (uint8)id;}

	inline uint32	GetNonS3TCMipmapOffset()	{return (uint32)m_Extra[3];}

	inline uint32	GetUIMipmapOffset()			{return m_Extra[4];}
	inline float	GetUIMipmapScale()			{return (float)(1 << GetUIMipmapOffset());}

	inline uint32	GetTexturePriority()		{return m_Extra[5];}

	// NOTE: it adds 1.0f so all the old values of 0.0f return 1.0f.
	// (the add of 1.0f should be totally transparent to everything though!)
	inline float	GetDetailTextureScale()		{return *((float*)&m_Extra[6]) + 1.0f;}

	inline int16	GetDetailTextureAngle()		{return (int16)((m_Extra[10]) + (m_Extra[11] << 8));}

public:
	uint32	m_ResType;			// 0x00
	int32	m_Version;			// 0x04 CURRENT_DTX_VERSION
	uint16	m_BaseWidth;		// 0x08
	uint16	m_BaseHeight;		// 0x0a
	uint16	m_nMipmaps;			// 0x0c
	uint16	m_nSections;		// 0x0e

	int32	m_IFlags;			// 0x10 Combination of DTX_ flags.
	int32	m_UserFlags;		// 0x14 Flags that go on surfaces.

	// m_Extra[0] = Texture group, [1] = mipmaps to use, [2] = BPPIdent ...
	union
	{
		uint8	m_Extra[12];	// 0x18
		uint32	m_ExtraLong[3];
	};

	char	m_CommandString[DTX_COMMANDSTRING_LEN];	// 0x24
};


struct SectionHeader
{
	char	m_Type[15];
	char	m_Name[10];
	uint32	m_DataLen;	// Data length, not including SectionHeader.
};


struct DtxSection
{
	SectionHeader	m_Header;	// 0x00
	DtxSection		*m_pNext;	// 0x20
	char			m_Data[1];	// 0x24 Section data (allocated past the section).
};


// One mipmap (0x18 bytes, constructed by 0x004352d0).
struct TextureMipData
{
	TextureMipData();

	uint32		m_Width;		// 0x00
	uint32		m_Height;		// 0x04
	uint8		*m_Data;		// 0x08
	long		m_Pitch;		// 0x0c Pitch in bytes.
	uint8		*m_AlphaMask;	// 0x10 8-bit alpha mask (dtx_Alloc with bAlphaMasks)
	long		m_AlphaPitch;	// 0x14
};


struct BaseResHeader
{
	uint32	m_Type;	// 0=DTX, 1=Model, 2=Sprite
};


// 0x18c bytes, vtable 0x004c73e0.
class TextureData
{
public:
					TextureData();
					~TextureData();

	// Just calls dtx_SetupDTXFormat2.
	virtual void	SetupPFormat(PFormat *pFormat);

public:
	BaseResHeader	m_ResHeader;		// 0x04
	union
	{
		DtxHeader	m_Header;			// 0x08
		struct
		{
			uint8	m_PadHeader[0x10];
			uint32	m_Flags;			// 0x18 m_Header.m_IFlags (DTX_NOSYSCACHE: engine owns it)
		};
	};
	LTLink			m_Link;				// 0xac in g_SysCache.m_List
	uint32			m_AllocSize;		// 0xb8
	DtxSection		*m_pSections;		// 0xbc
	SharedTexture	*m_pSharedTexture;	// 0xc0
	uint32			m_Flags2;			// 0xc4 renderer flags (Jupiter m_Flags)
	uint8			*m_pDataBuffer;		// 0xc8
	TextureMipData	m_Mips[MAX_DTX_MIPMAPS];	// 0xcc
};


// 0x004353e0. Allocates the texture and initializes the mipmap data pointers.
TextureData* dtx_Alloc(BPPIdent bpp, uint32 baseWidth, uint32 baseHeight, uint32 nMipmaps,
	uint32 *pAllocSize, uint32 *pTextureDataSize, LTBOOL bAlphaMasks=LTFALSE);
// 0x004355b0
LTRESULT dtx_Create(ILTStream *pStream, TextureData **ppOut, LTBOOL bLoadSections, LTBOOL bSkipImageData);
// 0x00435920
void dtx_Destroy(TextureData *pData);
// 0x00435940
void dtx_SetupDTXFormat2(BPPIdent bpp, PFormat *pFormat);

#endif  // __DTXMGR_H__
