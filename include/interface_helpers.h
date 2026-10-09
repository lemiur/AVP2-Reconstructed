// Jupiter runtime/kernel/src/sys/win/interface_helpers.h, Talon layout.
// Rectangle/2d clipping and warp helpers for the client surface interface.
// Talon differences: RenderStruct::LockSurface takes only the buffer (the pitch comes from
// CisSurface::m_Pitch), and g_ScreenFormat's type is read directly.
#ifndef __INTERFACE_HELPERS_H__
#define __INTERFACE_HELPERS_H__

#include "ltbasedefs.h"
#include "pixelformat.h"
#include "renderstruct.h"
#include "../../build/proj/LT2/lithshared/stdlith/goodlinklist.h"


// Surface flags.
#define SURFFLAG_SCREEN			(1<<0)	// This is the screen surface..
#define SURFFLAG_OPTIMIZED		(1<<1)	// This surface is 'optimized' by the renderer.
#define SURFFLAG_OPTIMIZEDIRTY	(1<<2)	// This surface has been changed and needs to be
										// reoptimized.


// These are used to simplify routines.
class Pixel16
{
public:

	typedef uint16	type;

	void	operator=(uint8 *ptr)		{m_pPixel = (uint16*)ptr;}
	void	operator=(uint16 *ptr)		{m_pPixel = (uint16*)ptr;}
	void	operator=(Pixel16 &other)	{*m_pPixel = *other.m_pPixel;}
	void	operator=(uint16 val)		{*m_pPixel = val;}
	void	operator=(GenericColor val)	{*m_pPixel = val.wVal;}
	void	operator++() {m_pPixel++;}
	LTBOOL	operator==(GenericColor &theColor) {return *m_pPixel == theColor.wVal;}
	LTBOOL	operator!=(GenericColor &theColor) {return *m_pPixel != theColor.wVal;}
	uint16	operator[](uint32 index)		{return m_pPixel[index];}
	static uint16 GetGenericColor(GenericColor &color) {return color.wVal;}

	uint16	*m_pPixel;
};

class Pixel32
{
public:

	typedef uint32	type;

	void	operator=(uint8 *ptr)		{m_pPixel = (uint32*)ptr;}
	void	operator=(uint32 *ptr)		{m_pPixel = (uint32*)ptr;}
	void	operator=(Pixel32 &other)	{*m_pPixel = *other.m_pPixel;}
	void	operator=(uint32 val)		{*m_pPixel = val;}
	void	operator=(GenericColor val)	{*m_pPixel = val.dwVal;}
	void	operator++() {m_pPixel++;}
	LTBOOL	operator==(GenericColor &theColor) {return *m_pPixel == theColor.dwVal;}
	LTBOOL	operator!=(GenericColor &theColor) {return *m_pPixel != theColor.dwVal;}
	uint32	operator[](uint32 index)		{return m_pPixel[index];}
	static uint32 GetGenericColor(GenericColor &color) {return color.dwVal;}

	uint32	*m_pPixel;
};


// The surface structure..
class CisSurface : public CGLLNode
{
	public:

		// Used when restarting the renderer to backup the surface.
		uint8		*m_pBackupBuffer;	// 0x08

		// All the surface info you always wanted but were afraid to ask for.
		HLTBUFFER	m_hBuffer;			// 0x0c
		uint32		m_Width, m_Height;	// 0x10, 0x14
		long		m_Pitch;			// 0x18

		// Is it the screen or a normal surface, etc...
		uint32		m_Flags;			// 0x1c

		// The transparent color passed to RenderStruct::OptimizeSurface.
		uint32		m_OptimizedTransparentColor;	// 0x20

		void		*m_pUserData;		// 0x24

		// Alpha value (only used on optimized surfaces blitted to the screen).
		float		m_Alpha;			// 0x28
};	// (size 0x2c)

#define MAX_WARP_POINTS	10

#define WARP_FIXED				int32
#define WARP_FIXED_SHIFT		16
#define FLOAT_TO_WARP_FIXED(x)	(WARP_FIXED)(x * (float)(1<<WARP_FIXED_SHIFT))


typedef struct
{
	short		m_DestX;
	WARP_FIXED	m_SourceX, m_SourceY;	// Fixed point source X and Y.
} WarpCoords;


// Globals from winclientde_impl.
// GLOBAL: LITHTECH 0x004dede4
extern CisSurface g_ScreenSurface;
// GLOBAL: LITHTECH 0x004def64
extern RenderStruct *g_pCisRenderStruct;
// GLOBAL: LITHTECH 0x004dede0
extern GenericColor g_TransparentColor;
// GLOBAL: LITHTECH 0x004def14
extern GenericColor g_SolidColor;
// GLOBAL: LITHTECH 0x004def18
extern PFormat g_ScreenFormat;
// GLOBAL: LITHTECH 0x004def54
extern uint32 g_nScreenPixelBytes;


// Surface functions in winclientde_impl.
CisSurface*	cis_InternalCreateSurface(uint32 width, uint32 height);	// 0x0040c520
LTRESULT	cis_DeleteSurface(HSURFACE hSurface);					// 0x0040c880


// ----------------------------------------------------------------- //
// Helper functions.
// ----------------------------------------------------------------- //

LTBOOL cis_RectIntersection(LTRect *pDest, LTRect *pRect1, LTRect *pRect2);

// Note: this clips a little differently than ClipRectsNonScaled.  It first
// CLAMPS the source coordinates to be inside the source rectangle, then clips
// the destination rectangle.
LTBOOL cis_ClipRectsScaled(
	int srcWidth, int srcHeight, int sLeft, int sTop, int sRight, int sBottom,
	int destWidth, int destHeight, int dLeft, int dTop, int dRight, int dBottom,
	LTRect *pSrcRect, LTRect *pDestRect);

LTBOOL cis_ClipRectsNonScaled(
	int srcWidth, int srcHeight, int sLeft, int sTop, int sRight, int sBottom,
	int destWidth, int destHeight, int dLeft, int dTop, LTRect *pSrcRect, LTRect *pDestRect);

void cis_GetWarpCoordinates(WarpCoords *pLeftCoords, WarpCoords *pRightCoords,
	LTWarpPt *pCoords, int nCoords, uint32 &outputMinY, uint32 &outputMaxY);

LTBOOL cis_Clip2dPoly(LTWarpPt* &pCoords, int &nCoords, float rectLeft, float rectTop,
	float rectRight, float rectBottom);

LTRESULT cis_DrawWarp(CisSurface *pDest, CisSurface *pSrc,
	WarpCoords *pLeftCoords, WarpCoords *pRightCoords, uint32 minY, uint32 maxY);

LTRESULT cis_DrawWarpTransparent(CisSurface *pDest, CisSurface *pSrc,
	WarpCoords *pLeftCoords, WarpCoords *pRightCoords, uint32 minY, uint32 maxY);

LTRESULT cis_DrawWarpSolidColor(CisSurface *pDest, CisSurface *pSrc,
	WarpCoords *pLeftCoords, WarpCoords *pRightCoords, uint32 minY, uint32 maxY);


inline LTBOOL cis_IsScreenSurface(CisSurface *pSurface)
{
	return pSurface == &g_ScreenSurface;
}

inline void* cis_LockSurface(CisSurface *pSurface, long &pitch, LTBOOL bSetDirty=LTFALSE)
{
	void *pData;

	if(!g_pCisRenderStruct)
		return LTNULL;

	pitch = 0;

	if(cis_IsScreenSurface(pSurface))
	{
		if(g_pCisRenderStruct->LockScreen(
			0, 0, g_ScreenSurface.m_Width, g_ScreenSurface.m_Height, &pData, &pitch))
		{
			return pData;
		}
		else
		{
			return LTNULL;
		}
	}
	else
	{
		pitch = pSurface->m_Pitch;
		pData = g_pCisRenderStruct->LockSurface(pSurface->m_hBuffer);
		if(bSetDirty && pData)
			pSurface->m_Flags |= SURFFLAG_OPTIMIZEDIRTY;

		return pData;
	}
}

inline void cis_UnlockSurface(CisSurface *pSurface)
{
	if(cis_IsScreenSurface(pSurface))
	{
		g_pCisRenderStruct->UnlockScreen();
	}
	else
	{
		g_pCisRenderStruct->UnlockSurface(pSurface->m_hBuffer);
	}
}

// The rectangle cis_Clip2dPoly clips warp polygons to (interface_helpers.cpp).
extern float g_RectLeft;
extern float g_RectTop;
extern float g_RectRight;
extern float g_RectBottom;

class CLeftWarpTest	{ public: LTBOOL operator()(LTWarpPt &pt) {return pt.dest_x >= g_RectLeft;} };
class CTopWarpTest	{ public: LTBOOL operator()(LTWarpPt &pt) {return pt.dest_y >= g_RectTop;} };
class CRightWarpTest	{ public: LTBOOL operator()(LTWarpPt &pt) {return pt.dest_x < g_RectRight;} };
class CBottomWarpTest	{ public: LTBOOL operator()(LTWarpPt &pt) {return pt.dest_y < g_RectBottom;} };

#define DO_WARPCLIP(pt1, pt2, destCoord1, destCoord2, coord) \
	float t = (coord - pt1.destCoord1) / (pt2.destCoord1 - pt1.destCoord1);\
	pOut->destCoord1 = coord;\
	pOut->destCoord2 = pt1.destCoord2 + (pt2.destCoord2 - pt1.destCoord2) * t;\
	pOut->source_x = pt1.source_x + (pt2.source_x - pt1.source_x) * t;\
	pOut->source_y = pt1.source_y + (pt2.source_y - pt1.source_y) * t;

class CLeftWarpClip {
	public:
		void operator()(LTWarpPt &pPt1, LTWarpPt &pPt2, LTWarpPt *pOut)
		{
			DO_WARPCLIP(pPt1, pPt2, dest_x, dest_y, g_RectLeft);
		}
};

class CRightWarpClip {
	public:
		void operator()(LTWarpPt &pPt1, LTWarpPt &pPt2, LTWarpPt *pOut)
		{
			DO_WARPCLIP(pPt1, pPt2, dest_x, dest_y, g_RectRight);
		}
};

class CTopWarpClip {
	public:
		void operator()(LTWarpPt &pPt1, LTWarpPt &pPt2, LTWarpPt *pOut)
		{
			DO_WARPCLIP(pPt1, pPt2, dest_y, dest_x, g_RectTop);
		}
};

class CBottomWarpClip {
	public:
		void operator()(LTWarpPt &pPt1, LTWarpPt &pPt2, LTWarpPt *pOut)
		{
			DO_WARPCLIP(pPt1, pPt2, dest_y, dest_x, g_RectBottom);
		}
};

#endif  // __INTERFACE_HELPERS_H__
