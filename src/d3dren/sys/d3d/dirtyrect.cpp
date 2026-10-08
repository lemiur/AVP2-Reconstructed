// d3d.ren sys/d3d/dirtyrect (0x100220f1-0x10022424): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// FLAGS: /O1 /Ob2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#define D3DREN_STATERESTORER_FULL	// d3ddevice.h: the real vector<RenderState>/vector<TextureState> members of UnkType_StateRestorer
#include <windows.h>
#include "ltbasedefs.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"

// ---- 2. dirtyrect.cpp ---------------------------------------------------------------------------------------------------------
#include "ltrect.h"
#include "ltlink.h"

// the main window (RenderStructInit::m_hWnd); Jupiter common_stuff.h / Ghidra name.  (Unit sys/d3d/d3d_surface declares it the same way, without the annotation.)
// GLOBAL: D3DREN 0x10057998
extern HWND g_hWnd;

// Two objects with an out-of-line constructor taking a byte and tying a list head off at +0xc (nothing else in the DLL touches
// them; role unknown).  They precede the dirty rects in the exe's data and static initialisers.
class UnkType_TiedList
{
public:
	UnkType_TiedList(uint8 b) { Init(b); }		// inline: expanded into the static initialisers (0x100220f1, 0x10022123)
	void Init(uint8 b);							// the exe calls this out of line

	void		*m_Unk00;
	void		*m_Unk04;
	void		*m_Unk08;
	CheapLTLink	m_Unk0c;		// list head (links to itself)
	uint16		m_Unk14;
	uint8		m_Unk16;
	uint8		m_Unk17;
};

// FUNCTION: D3DREN 0x100220fe ?Init@UnkType_TiedList@@QAEXE@Z
void UnkType_TiedList::Init(uint8 b)
{
	m_Unk14 = 0xffff;
	m_Unk16 = b;
	m_Unk00 = 0;
	m_Unk17 = 0;
	m_Unk08 = 0;
	m_Unk04 = 0;
	m_Unk0c.TieOff();
}

// FUNCTION: D3DREN 0x100220f1 _$E4
// GLOBAL: D3DREN 0x10063ca8
UnkType_TiedList g_TiedListType1(1);
// FUNCTION: D3DREN 0x10022123 _$E7
// GLOBAL: D3DREN 0x10063cc0
UnkType_TiedList g_TiedListType2(2);

// NAME: g_invalidRect, g_invalidRectCount, InvalidateRect, RectangleCombine, DirtyRectSwap, ClearDirtyRects: Jupiter
// render_a/src/sys/d3d/dirtyrect.cpp (the exe's InvalidateRect is line for line the same).
#define	MAX_INVALID_RECTS		100

// FUNCTION: D3DREN 0x10022130 _$E10
// FUNCTION: D3DREN 0x10022135 _$E9
// GLOBAL: D3DREN 0x10063d08
LTRect	g_invalidRect[MAX_INVALID_RECTS];
// GLOBAL: D3DREN 0x10064348
uint32	g_invalidRectCount = 0;

inline bool RectangleContains(LTRect *outer, LTRect *inner)
{
	return (outer->left <= inner->left && outer->top <= inner->top && outer->right >= inner->right && outer->bottom >= inner->bottom);
}

// FUNCTION: D3DREN 0x100222c0 ?RectangleCombine@@YA?AVLTRect@@PAV1@0@Z
inline LTRect RectangleCombine(LTRect *r1, LTRect *r2)
{
	LTRect r;
	r.left   = (r1->left   < r2->left  ) ? r1->left   : r2->left;
	r.top    = (r1->top    < r2->top   ) ? r1->top    : r2->top;
	r.right  = (r1->right  > r2->right ) ? r1->right  : r2->right;
	r.bottom = (r1->bottom > r2->bottom) ? r1->bottom : r2->bottom;
	return r;
}

inline uint32 RectangleArea(LTRect *r)
{
	return (r->right - r->left) * (r->bottom - r->top);
}

// FUNCTION: D3DREN 0x10022151
void InvalidateRect(LTRect *pRect)
{
	if (pRect==NULL) {						// Invalidate the whole screen...
		g_invalidRect[0].left	= 0;
		g_invalidRect[0].top	= 0;
		g_invalidRect[0].right	= g_ScreenWidth;
		g_invalidRect[0].bottom	= g_ScreenHeight;
		g_invalidRectCount		= 1;
		return; }

	if (g_invalidRectCount==0) {
		g_invalidRect[g_invalidRectCount] = *pRect;
		++g_invalidRectCount; }
	else {									// Attempt to combine this invalid area with one we already have...
		uint32 i;
		LTRect comb;
		int area1, area2, areac, areaBest;
		int bestRef = -1;

		if (g_invalidRectCount>=MAX_INVALID_RECTS-1) {
			InvalidateRect(NULL); return; }

		for (i=0; i<g_invalidRectCount; i++) {
			if (RectangleContains(&g_invalidRect[i], pRect)) return; }
		for (i=0; i<g_invalidRectCount; i++) {
			comb = RectangleCombine(&g_invalidRect[i], pRect);
			area1 = RectangleArea(&g_invalidRect[i]);
			area2 = RectangleArea(pRect);
			areac = RectangleArea(&comb);
			if (areac < area1 + area2) {
				if (bestRef < 0 || area1 + area2 - areac > areaBest) {
					areaBest = area1 + area2 - areac;
					bestRef = i; } } }
		if (bestRef >= 0)
			g_invalidRect[bestRef] = RectangleCombine(&g_invalidRect[bestRef], pRect);
		else
			g_invalidRect[g_invalidRectCount++] = *pRect; }
}

// guess: windowed flavour of the swap: the dirty rects are blitted from the offscreen surface to the primary surface at the
// window's client position.
// FUNCTION: D3DREN 0x1002231c
void BlitDirtyRectsToPrimary()
{
	RECT rcClient;
	RECT rcDest;
	uint32 i;

	if (g_pPrimary)
	{
		GetClientRect(g_hWnd, &rcClient);
		ClientToScreen(g_hWnd, (POINT *)&rcClient.left);
		ClientToScreen(g_hWnd, (POINT *)&rcClient.right);
		for (i = 0; i < g_invalidRectCount; i++)
		{
			rcDest.left = g_invalidRect[i].left + rcClient.left;
			rcDest.top = g_invalidRect[i].top + rcClient.top;
			rcDest.right = g_invalidRect[i].right + rcClient.left;
			rcDest.bottom = g_invalidRect[i].bottom + rcClient.top;
			g_pPrimary->Blt(&rcDest, g_pOffscreen, (RECT *)&g_invalidRect[i], DDBLT_WAIT, 0);
		}
		g_invalidRectCount = 0;
	}
}

// This routine copies the dirty rectangles from the offscreen surface to the screen.
// FUNCTION: D3DREN 0x100223c8
void DirtyRectSwap()
{
	uint32 i;

	if (g_invalidRectCount == 0)
		return;

	if (g_pPrimary)
	{
		BlitDirtyRectsToPrimary();
		return;
	}

	for (i = 0; i < g_invalidRectCount; i++)
		g_pBackBuffer->Blt((RECT *)&g_invalidRect[i], g_pOffscreen, (RECT *)&g_invalidRect[i], DDBLT_WAIT, 0);
	g_invalidRectCount = 0;
}

// FUNCTION: D3DREN 0x1002241c
void ClearDirtyRects()
{
	g_invalidRectCount = 0;
}

// ---- 3. draw_canvas.cpp ---------------------------------------------------------------------------------------------------------
// NAME: CanvasDrawMgr, DrawCanvas, d3d_DrawCanvasCB, d3d_ProcessCanvas, d3d_DrawSolidCanvases, d3d_QueueTranslucentCanvases:
// Jupiter render_a/src/sys/d3d/draw_canvas.cpp/.h (the Talon versions differ: CanvasDrawMgr implements ILTCustomDraw (SDK
// iltcustomdraw.h: DrawPrimitive, SetState, GetState, SetTexture, GetTexelSize) and keeps the D3D states it changes so that
// DrawCanvas can put them back).
#include <string.h>
#include "iltcustomdraw.h"
#include "de_objects.h"
#include "de_world.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dstate.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/tlvertex.h"
#include "d3dren/polydraw.h"
#include "d3dren/pool.h"
#include "d3dren/draw_canvas.h"
#include "d3dren/scenedesc.h"
#include "ltmatrix.h"
#include "counter.h"

// The dynamic lights touching a poly: a list at WorldPoly+0x30 (the engine pads it): { next, the light, the light position }
// (the same record unit unk/10007930 and common_stuff.cpp use under this name).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;
	DynamicLight		*m_pLight;
	LTVector			m_Pos;
};
#define POLY_LIGHTS(p)	(*(UnkType_PolyLight **)((uint8 *)(p) + 0x30))
