// d3d.ren sys/d3d/draw_canvas (0x10022424-0x10022bc5): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// FLAGS NOTE: this P object uses /O1 /Ob2.  The 0x10022424 initializer wrapper now matches; FUN_10022803 is kept out of line by the narrow pragma below.
// The remaining non-match is the source-shape difference in DrawPrimitive.
// FLAGS: /O1 /Ob2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#define D3DREN_STATERESTORER_FULL	// d3ddevice.h: the real vector<RenderState>/vector<TextureState> members of UnkType_StateRestorer
#include <windows.h>
#include "ltbasedefs.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"

// ---- 2. dirtyrect.cpp ---------------------------------------------------------------------------------------------------------
#include "ltrect.h"
#include "ltlink.h"

// NAME: g_invalidRect, g_invalidRectCount, InvalidateRect, RectangleCombine, DirtyRectSwap, ClearDirtyRects: Jupiter
// render_a/src/sys/d3d/dirtyrect.cpp (the exe's InvalidateRect is line for line the same).
#define	MAX_INVALID_RECTS		100

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

// callees and globals of other units
void FUN_100062e0(float *pDest, float *pSrc, float fScale);			// unit unk/10001000

class CanvasDrawMgr : public ILTCustomDraw
{
public:
	CanvasDrawMgr();																// 0x1002244d

	virtual LTRESULT	DrawPrimitive(LTVertex *pVerts, uint32 nVerts, uint32 flags);	// 0x100226e9
	virtual LTRESULT	SetState(LTRState state, uint32 val);							// 0x10022832
	virtual LTRESULT	GetState(LTRState state, uint32 &val);							// 0x100229d5
	virtual LTRESULT	SetTexture(const char *pTexture);								// 0x100229f2
	virtual LTRESULT	GetTexelSize(float &fSizeU, float &fSizeV);						// 0x10022a73

	void	DrawCanvas(Canvas *pCanvas);											// 0x1002246e

	void	FUN_100224ae(Canvas *pCanvas);											// save the device states, set the canvas defaults
	void	FUN_10022645();															// put the saved device states back

	// the device states DrawCanvas changes, saved before and restored after (names invented)
	uint32	m_Unk04;			// ALPHABLENDENABLE
	uint32	m_Unk08;			// ZENABLE
	uint32	m_Unk0c;			// ZWRITEENABLE
	uint32	m_Unk10;			// SRCBLEND
	uint32	m_Unk14;			// DESTBLEND
	uint32	m_Unk18;			// stage 0 D3DTSS_ADDRESS
	uint32	m_Unk1c;			// stage 0 D3DTSS_COLOROP
	uint32	m_Unk20;			// stage 0 D3DTSS_ALPHAOP
	uint32	m_Unk24;			// FOGENABLE
	uint32	m_Unk28;			// the canvas is FLAG_REALLYCLOSE
	uint32	m_States[NUM_LTRSTATES];	// 0x2c the LTRState values the canvas set
};

// the object and its console variable, in the order of the exe's static initialisers (0x10022424, 0x1002242e/0x10022433)
// FUNCTION: D3DREN 0x10022424 _$E4
// GLOBAL: D3DREN 0x10064370
CanvasDrawMgr DAT_10064370;
// FUNCTION: D3DREN 0x1002242e _$E7
// FUNCTION: D3DREN 0x10022433 _$E6
// GLOBAL: D3DREN 0x10064350
ConVar g_CV_DrawCanvases("DrawCanvases", 1.0f);

// FUNCTION: D3DREN 0x1002244d
CanvasDrawMgr::CanvasDrawMgr()
{
	memset(m_States, 0, sizeof(m_States));
	m_Unk28 = 0;
}

// FUNCTION: D3DREN 0x1002246e
void CanvasDrawMgr::DrawCanvas(Canvas *pCanvas)
{
	if (pCanvas->m_Fn)
	{
		DAT_10063c90.FUN_10021da6();
		FUN_100224ae(pCanvas);
		pCanvas->m_Fn(this, (HLOCALOBJ)pCanvas, pCanvas->m_pFnUserData);
		FUN_10022645();
	}
}

// FUNCTION: D3DREN 0x100224ae
void CanvasDrawMgr::FUN_100224ae(Canvas *pCanvas)
{
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &m_Unk04);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
	m_States[LTRSTATE_ALPHABLENDENABLE] = 0;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZENABLE, &m_Unk08);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, 1);
	m_States[LTRSTATE_ZREADENABLE] = 1;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZWRITEENABLE, &m_Unk0c);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 1);
	m_States[LTRSTATE_ZWRITEENABLE] = 1;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_SRCBLEND, &m_Unk10);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
	m_States[LTRSTATE_SRCBLEND] = LTBLEND_SRCALPHA;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_DESTBLEND, &m_Unk14);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
	m_States[LTRSTATE_DESTBLEND] = LTBLEND_INVSRCALPHA;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, &m_Unk24);
	if ((pCanvas->m_Flags & FLAG_FOGDISABLE) && m_Unk24)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_ADDRESS, &m_Unk18);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);
	m_States[LTRSTATE_TEXADDR] = LTTEXADDR_WRAP;
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_COLOROP, &m_Unk1c);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	m_States[LTRSTATE_COLOROP] = LTOP_MODULATE;
	g_pD3DDevice->GetTextureStageState(0, D3DTSS_ALPHAOP, &m_Unk20);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	m_States[LTRSTATE_ALPHAOP] = LTOP_SELECTDIFFUSE;
	m_Unk28 = (pCanvas->m_Flags >> 6) & 1;
	d3d_DisableTexture(g_NormalTextureStage);
}

// FUNCTION: D3DREN 0x10022645
void CanvasDrawMgr::FUN_10022645()
{
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, m_Unk04);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, m_Unk08);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, m_Unk0c);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, m_Unk10);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, m_Unk14);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, m_Unk24);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, m_Unk18);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, m_Unk1c);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, m_Unk20);
}

// STUB diagnosis: 39 of 282 bytes differ, same 103 instructions in the same order.  The exe pushes ebx/esi/edi in the entry block (scheduled between
//   the argument loads, before `cmp eax, 3`) and its early `return 1` jumps to the common epilogue `pop edi / pop esi / pop ebx`; ours moves the
//   three pushes behind the `nVertices < 3` test (the early return leaves through a bare `leave; ret`) and puts `xor eax, eax` between the pops.
//   Tried: ret variable, if/else and nested-if forms of the early return, local declaration order.
// STUB: D3DREN 0x100226e9
LTRESULT CanvasDrawMgr::DrawPrimitive(LTVertex *pVerts, uint32 nVerts, uint32 flags)
{
	TLVertex *pVertices = (TLVertex *)pVerts;
	int nVertices = (int)nVerts;
	int i;

	if (nVertices < 3)
		return 1;

	g_ClipFlags = 0x3f;
	if (!m_Unk28 && !(flags & 0x40))
	{
		if (FUN_1000af16(&pVertices, &nVertices, &g_ViewParams, 0))
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, pVertices, nVertices, 0);
	}
	else
	{
		float fOldNearZ = g_ViewParams.m_NearZ;
		g_ViewParams.m_NearZ = g_CV_ReallyCloseNearZ.m_FloatVal;
		for (i = 0; i < nVertices; i++)
			FUN_10008719((float *)((uint8 *)pVertices + i * 0x20), &g_ViewParams.m_mReallyCloseClipTransform.m[0][0]);
		if (ClipPoly(g_ClipFlags, &pVertices, &nVertices))
		{
			for (i = 0; i < nVertices; i++)
				FUN_100062e0((float *)((uint8 *)pVertices + i * 0x20), (float *)((uint8 *)pVertices + i * 0x20), g_CV_NearZ.m_FloatVal);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, pVertices, nVertices, 0);
			g_ViewParams.m_NearZ = fOldNearZ;
		}
	}
	return 0;
}

// guess: LTBLEND_* (SDK iltcustomdraw.h) to D3DBLEND: the values are the same numbers (ZERO 1 ... INVSRCALPHA 6), default ONE.
// Matching compiler control: the /O1 /Ob2 object keeps calls to this out-of-line helper.
#pragma auto_inline(off)
// FUNCTION: D3DREN 0x10022803
static int FUN_10022803(int blend)
{
	int d3dBlend = D3DBLEND_ONE;
	if (blend == LTBLEND_SRCALPHA)
		return D3DBLEND_SRCALPHA;
	if (blend == LTBLEND_INVSRCALPHA)
		return D3DBLEND_INVSRCALPHA;
	if (blend == LTBLEND_SRCCOLOR)
		return D3DBLEND_SRCCOLOR;
	if (blend == LTBLEND_INVSRCCOLOR)
		return D3DBLEND_INVSRCCOLOR;
	if (blend == LTBLEND_ZERO)
		d3dBlend = D3DBLEND_ZERO;
	return d3dBlend;
}
#pragma auto_inline(on)

// guess: LTTEXADDR_* to D3DTADDRESS and LTOP_* to D3DTOP; an unknown value retries with the default (tail recursion, which the
// compiler turned into a loop in the exe's out-of-line copies).
inline int FUN_10022990(int addr, int dflt);
inline int FUN_100229a8(int op, int dflt);

// FUNCTION: D3DREN 0x10022832
LTRESULT CanvasDrawMgr::SetState(LTRState state, uint32 val)
{
	if ((uint32)state >= NUM_LTRSTATES)
		return 1;

	switch (state)
	{
	case LTRSTATE_ALPHABLENDENABLE:
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, val);
		break;
	case LTRSTATE_ZREADENABLE:
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, val);
		break;
	case LTRSTATE_ZWRITEENABLE:
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, val);
		break;
	case LTRSTATE_SRCBLEND:
		{
			int d3dBlend = FUN_10022803(val);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, d3dBlend);
		}
		break;
	case LTRSTATE_DESTBLEND:
		{
			int d3dBlend = FUN_10022803(val);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, d3dBlend);
		}
		break;
	case LTRSTATE_TEXADDR:
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, FUN_10022990(val, LTTEXADDR_WRAP));
		break;
	case LTRSTATE_COLOROP:
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, FUN_100229a8(val, LTOP_MODULATE));
		break;
	case LTRSTATE_ALPHAOP:
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, FUN_100229a8(val, LTOP_SELECTTEXTURE));
		break;
	}
	m_States[state] = val;
	return 0;
}

// FUNCTION: D3DREN 0x10022990 ?FUN_10022990@@YAHHH@Z
inline int FUN_10022990(int addr, int dflt)
{
	switch (addr)
	{
	case LTTEXADDR_WRAP:
		return D3DTADDRESS_WRAP;
	case LTTEXADDR_CLAMP:
		return D3DTADDRESS_CLAMP;
	}
	return FUN_10022990(dflt, dflt);
}

// FUNCTION: D3DREN 0x100229a8 ?FUN_100229a8@@YAHHH@Z
inline int FUN_100229a8(int op, int dflt)
{
	switch (op)
	{
	case LTOP_SELECTTEXTURE:
		return D3DTOP_SELECTARG1;
	case LTOP_SELECTDIFFUSE:
		return D3DTOP_SELECTARG2;
	case LTOP_MODULATE:
		return D3DTOP_MODULATE;
	case LTOP_ADD:
		return D3DTOP_ADD;
	case LTOP_ADDSIGNED:
		return D3DTOP_ADDSIGNED;
	}
	return FUN_100229a8(dflt, dflt);
}

// FUNCTION: D3DREN 0x100229d5
LTRESULT CanvasDrawMgr::GetState(LTRState state, uint32 &val)
{
	if ((uint32)state >= NUM_LTRSTATES)
		return 1;
	val = m_States[state];
	return 0;
}

// The null-input, missing-texture, and failed-bind guards each disable the normal texture stage and return their own LTRESULT.
// FUNCTION: D3DREN 0x100229f2
LTRESULT CanvasDrawMgr::SetTexture(const char *pTexture)
{
	if (!pTexture) {
		d3d_DisableTexture(g_NormalTextureStage);
		return 0;
	}
	SharedTexture *pShared = g_pStruct->GetSharedTexture(pTexture);
	if (!pShared) {
		d3d_DisableTexture(g_NormalTextureStage);
		return 1;
	}
	if (!d3d_SetTexture(pShared, g_NormalTextureStage, 0)) {
		d3d_DisableTexture(g_NormalTextureStage);
		return 1;
	}
	DAT_10063c90.FUN_10021da6();
	if (pShared->m_pStateChange)
		DAT_10063c90.FUN_10021db7(pShared->m_pStateChange, g_NormalTextureStage);
	return 0;
}

// FUNCTION: D3DREN 0x10022a73
LTRESULT CanvasDrawMgr::GetTexelSize(float &fSizeU, float &fSizeV)
{
	fSizeU = DAT_10061810[0].m_Unk00;
	fSizeV = DAT_10061810[0].m_Unk04;
	return 0;
}

// FUNCTION: D3DREN 0x10022a90
void d3d_DrawCanvasCB(ViewParams *pParams, LTObject *pCanvas)
{
	DAT_10064370.DrawCanvas((Canvas *)pCanvas);
}

// FUNCTION: D3DREN 0x10022a9f
void d3d_ProcessCanvas(LTObject *pObject)
{
	d3d_GetVisibleSet()->m_SolidCanvases.Add(pObject);
}

// FUNCTION: D3DREN 0x10022ae3
void d3d_DrawSolidCanvases()
{
	char dummy;
	if (g_CV_DrawCanvases.m_IntVal)
	{
		VisibleSet *pSet = d3d_GetVisibleSet();
		pSet->m_SolidCanvases.FUN_10022b50(&g_ViewParams, d3d_DrawCanvasCB, &dummy, &pSet->m_TranslucentCanvases);
	}
}

// guess: queues one translucent canvas on the sorted list (Jupiter's BaseObjectSet::Queue, open-coded per object type in Talon).
// FUNCTION: D3DREN 0x10022b3b
static void FUN_10022b3b(ViewParams *pParams, LTObject *pObject)
{
	DAT_1006b934->Add(pObject, d3d_DrawCanvasCB);
}

// FUNCTION: D3DREN 0x10022b15
void d3d_QueueTranslucentCanvases()
{
	if (g_CV_DrawCanvases.m_IntVal)
		d3d_GetVisibleSet()->m_TranslucentCanvases.Draw(&g_ViewParams, FUN_10022b3b);
}

// FUNCTION: D3DREN 0x10022ab6 ?Add@BaseObjectSet@@QAEXPAVLTObject@@@Z
// FUNCTION: D3DREN 0x10022b50
void BaseObjectSet::FUN_10022b50(ViewParams *pParams, DrawObjectFn fn, char *pUnused, BaseObjectSet *pTranslucent)
{
	uint32 i;

	pTranslucent->m_nObjects = 0;
	for (i = 0; i < m_nObjects; i++)
	{
		LTObject *pObject = m_pObjects[i];
		BOOL bDraw;
		if (pObject->m_Flags & FLAG_VISIBLE)
			bDraw = pParams->m_bPortalView == 0 || !(pObject->m_Flags2 & 1);
		else
			bDraw = pParams->m_bPortalView == 0 || (pObject->m_Flags & FLAG_PORTALVISIBLE);
		if (bDraw)
		{
			if ((uint8)~pObject->m_Unknown188 & CF_SOLIDCANVAS)	// the exe reads the client flags as a byte here (not cl)
				pTranslucent->Add(pObject);
			else
				fn(pParams, pObject);
		}
	}
}

// The dynamic lights touching a poly: a list at WorldPoly+0x30 (the engine pads it): { next, the light, the light position }
// (the same record unit unk/10007930 and common_stuff.cpp use under this name).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;
	DynamicLight		*m_pLight;
	LTVector			m_Pos;
};
#define POLY_LIGHTS(p)	(*(UnkType_PolyLight **)((uint8 *)(p) + 0x30))
