// d3d.ren sys/d3d/d3d_draw (0x100132a0-0x10019350): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// one object; no evidence for a split.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/100132a0 (0x100132a0-0x10019350): world draw: fog alpha, poly queues, render state saver users, FullDrawScene,
// mirrors, line/wireframe drawing, RenderScene, and the STLport template copies at the end of the object.  Unit name evidence: the order
// (ConVars VFog.. RenderToFront, d3d_SetTranslucentObjectStates, d3d_FullDrawScene, d3d_DrawLightAddPoly/ScalePoly, d3d_DrawLine/
// DrawWireframeBox/DrawWorldTree_R, RenderScene) is the function order of Jupiter's render_a/src/sys/d3d/d3d_draw.cpp, so this object is
// probably d3d_draw.cpp (the unit keeps its address name).
// FLAGS: /O2 /Ob2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#include "d3dren/rendererconsolevars.h"

// FUNCTION: D3DREN 0x100132a0 _$E2
// GLOBAL: D3DREN 0x1005a348
ConVar g_CV_VFogMinY("VFogMinY", 0.0f);
// FUNCTION: D3DREN 0x100132c0 _$E5
// GLOBAL: D3DREN 0x1005a310
ConVar g_CV_VFogMaxY("VFogMaxY", 1300.0f);
// FUNCTION: D3DREN 0x100132e0 _$E8
// GLOBAL: D3DREN 0x10058738
ConVar g_CV_VFogMinYVal("VFogMinYVal", 0.5f);
// FUNCTION: D3DREN 0x10013300 _$E11
// GLOBAL: D3DREN 0x100585e0
ConVar g_CV_VFogMaxYVal("VFogMaxYVal", 0.0f);
// FUNCTION: D3DREN 0x10013320 _$E14
// GLOBAL: D3DREN 0x10058ce0
ConVar g_CV_VFogMax("VFogMax", 180.0f);
// FUNCTION: D3DREN 0x10013340 _$E17
// GLOBAL: D3DREN 0x10058600
ConVar g_CV_VFogDensity("VFogDensity", 1800.0f);
// FUNCTION: D3DREN 0x10013360 _$E20
// GLOBAL: D3DREN 0x10058c00
ConVar g_CV_VFog("VFog", 0.0f);
// FUNCTION: D3DREN 0x10013380 _$E23
// GLOBAL: D3DREN 0x10058628
ConVar g_CV_DrawWorldTree("DrawWorldTree", -1.0f);
// FUNCTION: D3DREN 0x100133a0 _$E26
// GLOBAL: D3DREN 0x100587a0
ConVar g_CV_DrawTerrainSections("DrawTerrainSections", 0.0f);
// FUNCTION: D3DREN 0x100133c0 _$E29
// GLOBAL: D3DREN 0x100587c0
ConVar g_CV_AlphaTest("AlphaTest", 1.0f);
// FUNCTION: D3DREN 0x100133e0 _$E32
// GLOBAL: D3DREN 0x10058cb8
ConVar g_CV_DrawPortals("DrawPortals", 1.0f);
// FUNCTION: D3DREN 0x10013400 _$E35
// GLOBAL: D3DREN 0x10058c48
ConVar g_CV_ShowPortalBounds("ShowPortalBounds", 0.0f);
// FUNCTION: D3DREN 0x10013420 _$E38
// GLOBAL: D3DREN 0x10058c70
ConVar g_CV_PortalLightmap("PortalLightmap", 1.0f);
// FUNCTION: D3DREN 0x10013440 _$E41
// GLOBAL: D3DREN 0x100585c0
ConVar g_CV_ShowTexInfo("ShowTexInfo", 0.0f);
// FUNCTION: D3DREN 0x10013460 _$E44
// GLOBAL: D3DREN 0x10058780
ConVar g_CV_RenderToFront("RenderToFront", 0.0f);

// (the plain inline d3d_SetTexture of d3d_texture.h was tried for this object: the vector destructors of d3d_DrawMirrorSurfaceOverlay come out of line but the node allocator
//  deallocate copy 0x10018f80 is no longer emitted separately, see the diagnosis of d3d_DrawMirrorSurfaceOverlay; D3DREN_SETTEXTURE_EXTERN keeps the out-of-line call)
#define D3DREN_STATERESTORER_FULL	// d3ddevice.h: the real vector<RenderState>/vector<TextureState> members of UnkType_StateRestorer
#define D3DREN_SETTEXTURE_EXTERN	// d3d_texture.h: the plain inline changes the STLport node allocator copies at the end of the object
#include <windows.h>
#include <math.h>
#include "ltbasedefs.h"
#include "ltvector.h"
#include "ltmatrix.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "world_tree.h"
#include "d3dren/polydraw.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "d3dren/tlvertex.h"
#include "d3dren/viewparams.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3d_draw.h"
#include "d3dren/common_draw.h"
#include "d3dren/common_init.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/drawpolymgr.h"
#include "d3dren/scenedesc.h"
#include "d3dren/drawobjects.h"
#include "d3dren/visibleset.h"
#include "d3dren/pool.h"
#include "d3dren/setupmodel.h"
#include "counter.h"
#include "ltcodes.h"
#include "pixelformat.h"



// ---- (merged from the scratch unit w2scratch/drawA) ----
// ---------------------------------------------------------------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------------------------------------------------------------

float g_fFogAlphaScale;		// guess: 255 / (FogFarZ - FogNearZ)
float g_fSkyFogAlphaScale;		// guess: 255 / (SkyFogFarZ - SkyFogNearZ)

// guess: the global the exe's static initialiser 0x10013480 sets to (5, 5, 5): the light scale RenderScene last handed to the lightmap
// colour tables (compared with SceneDesc::m_GlobalLightScale each frame, see d3d_RenderScene).
// FUNCTION: D3DREN 0x10013480 _$E49
// GLOBAL: D3DREN 0x1005a338
LTVector g_vLastColorTableLightScale(5.0f, 5.0f, 5.0f);

// The exe has a second static initialiser here (0x100134a0, a bare `ret`): the constructor of a global that is default-constructed with an
// empty constructor (an LTVector / class with an empty inline constructor, the global itself is not identified: it has no other writer in this
// unit).  Reproduced as an empty function; nothing in the source of this object is known that would emit it.
// FUNCTION: D3DREN 0x100134a0
void d3d_EmptyDrawGlobalInitializer()
{
}

// FUNCTION: D3DREN 0x100134b0
void __fastcall d3d_CalcDistanceFogAlpha(LTVector *pPos, uint32 *pSpecular)
{
	float fDist = (*pPos - g_ViewParams.m_FogViewPos).Mag();

	if (fDist < g_FogNearZ)
	{
		*pSpecular = 0xffffffff;
		return;
	}
	if (fDist > g_FogFarZ)
	{
		*pSpecular = 0;
		return;
	}
	fDist = (fDist - g_FogNearZ) * g_fFogAlphaScale;
	((uint8 *)pSpecular)[3] = (uint8)(0xff - (uint8)RoundFloatToInt(fDist));
}

// FUNCTION: D3DREN 0x10013560
void __fastcall d3d_CalcSkyFogAlpha(LTVector *pPos, uint32 *pSpecular)
{
	float fFog;

	if (pPos->z < g_SkyFogNearZ)
	{
		((uint8 *)pSpecular)[3] = 0xff;
		return;
	}
	if (pPos->z > g_SkyFogFarZ)
	{
		((uint8 *)pSpecular)[3] = 0;
		return;
	}
	fFog = (pPos->z - g_SkyFogNearZ) * g_fSkyFogAlphaScale;
	((uint8 *)pSpecular)[3] = (uint8)(0xff - (uint8)RoundFloatToInt(fFog));
}

float g_fVFogValueRange;		// guess: VFogMaxYVal - VFogMinYVal (set by RenderScene when VFog is on)
float g_fInvVFogHeightRange;		// guess: 1 / (VFogMaxY - VFogMinY)
// GLOBAL: D3DREN 0x10058774
float g_fVFogDensityScale;				// guess: 255 / VFogDensity (set by RenderScene when VFog is on)



// guess: Jupiter has no counterpart.  The vertical ("height") fog hook RenderScene installs in g_pfnCalcFogAlpha when the VFog console variable is on:
// integrates a fog density that is VFogMinYVal below VFogMinY, VFogMaxYVal above VFogMaxY and linear in between along the ray from the viewer to the vertex
// (the viewer's own density and zone are cached in g_ViewParams.m_fVFogViewDensity / m_nVFogViewZone by ViewParams::SetupFogViewPosition), clamps it to VFogMax and
// stores 255 - fog in the specular alpha.  Same fastcall contract as the other fog hooks (position in ecx, specular in edx).
// STUB diagnosis: 174 aligned mismatches, 74 ignoring stack offsets (was 181 / 83); the crossing point is built with the 3-float
//   constructor (its inlined charge gives the exe's 5 out-of-line ctors).  The distances are the SDK's Dist (its out-of-line
//   copy 0x1000dfcf is in the DLL), the same-zone branch computes one distance and doubles it (the exe has 6 Mag sites, `fadd st(0),st(0)`),
//   the zone and the clamped density are the inline helpers d3d_GetVFogZone / d3d_GetVFogDensity (ViewParams::SetupFogViewPosition uses
//   both and still matches), and one float (fFog) carries the density, the first leg and the result as in the exe's [ebp-4].
//   Remaining: the call set.  The exe calls Mag out of line at all 6 Dist sites, the 3-float ctor at 5 and the whole operator- at the
//   last; ours inlines Mag at the 4th and 5th site and the last operator- (nested shares: the original has less budget or more pending
//   sites after each Dist; inline_budget.py --solve finds no pending/budget combination), and the frame differs accordingly.
//   Each leg is `g_fVFogDensityScale * (distance * density)`: with the plain left-to-right chain VC6 multiplies by the global first,
//   the exe multiplies by the density first.  Under the inline-budget model the 5th/6th legs (operator- inlined, then refused) need
//   an inline charge between them, so the leg/helper structure is still open.
// STUB: D3DREN 0x100135c0
void __fastcall d3d_CalcVerticalFogAlpha(LTVector *pPos, uint32 *pSpecular)
{
	LTVector vEye = g_ViewParams.m_Pos;
	LTVector vPos = *pPos;
	int nZone;
	float fFog;

	nZone = d3d_GetVFogZone(pPos->y);

	if (g_ViewParams.m_nVFogViewZone == nZone)
	{
		if (g_ViewParams.m_nVFogViewZone == 2)
		{
			fFog = (vPos.y - g_CV_VFogMinY.m_FloatVal) * g_fVFogValueRange * g_fInvVFogHeightRange + g_ViewParams.m_fVFogViewDensity + g_CV_VFogMinYVal.m_FloatVal;
			fFog = g_fVFogDensityScale * (vEye.Dist(*pPos) * fFog);
		}
		else
		{
			fFog = g_ViewParams.m_fVFogViewDensity;
			fFog = g_fVFogDensityScale * (vEye.Dist(*pPos) * fFog * 2.0f);
		}
	}
	else
	{
		float fT = (g_CV_VFogMaxY.m_FloatVal - vEye.y) / (vPos.y - vEye.y);
		LTVector vCross((vPos.x - vEye.x) * fT + vEye.x, g_CV_VFogMaxY.m_FloatVal, (vPos.z - vEye.z) * fT + vEye.z);
		float fFogB;

		if (g_ViewParams.m_nVFogViewZone == 1)
		{
			// viewer above VFogMaxY, vertex below: the first part of the ray is at VFogMaxYVal
			fFog = g_CV_VFogMaxYVal.m_FloatVal + g_CV_VFogMaxYVal.m_FloatVal;
			fFog = g_fVFogDensityScale * (vEye.Dist(vCross) * fFog);
			fFogB = d3d_GetVFogDensity(vPos.y) + g_CV_VFogMaxYVal.m_FloatVal;
			fFog = g_fVFogDensityScale * (vCross.Dist(vPos) * fFogB) + fFog;
		}
		else
		{
			fFogB = d3d_GetVFogDensity(vEye.y) + g_CV_VFogMaxYVal.m_FloatVal;
			fFog = g_fVFogDensityScale * (vEye.Dist(vCross) * fFogB);
			fFogB = g_CV_VFogMaxYVal.m_FloatVal + g_CV_VFogMaxYVal.m_FloatVal;
			fFog = g_fVFogDensityScale * (vCross.Dist(vPos) * fFogB) + fFog;
		}
	}

	if (fFog > g_CV_VFogMax.m_FloatVal)
		fFog = g_CV_VFogMax.m_FloatVal;
	else if (fFog < 0.0f)
		fFog = 0.0f;

	((uint8 *)pSpecular)[3] = (uint8)(0xff - (uint8)RoundFloatToInt(fFog));
}

// FUNCTION: D3DREN 0x10013990
uint32 d3d_PackSqrtRGB(uint8 r, uint8 g, uint8 b)
{
	return (g_ColorSqrtTable.m_Unk00[r] << 16) | (g_ColorSqrtTable.m_Unk00[g] << 8) | g_ColorSqrtTable.m_Unk00[b];
}

// FUNCTION: D3DREN 0x100139d0
uint32 d3d_PackRGB(uint8 r, uint8 g, uint8 b)
{
	return (r << 16) | (g << 8) | b;
}

// FUNCTION: D3DREN 0x100139f0
void d3d_SetModulateAlphaTextureStates(void)
{
	if (g_UseDX6Commands)
	{
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	}
	else
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATEALPHA);
	}
}

// guess: begins an alpha tested pass: alpha test (reference function GREATER) when the AlphaTest console variable is set,
// else colour keying; then flags the world poly drawer (d3d_SetChromaKeyPass).
// The saver object for the translucent object state changes: a UnkType_StateRestorer (d3ddevice.h) defined in this object.  In the exe
// its constructor (two empty std::vectors) and destructor (restore, then free both vectors through the node allocator) are expanded
// inline into the static initialiser 0x10013a80 and the atexit function 0x10013ad0; d3ddevice.h has them as inline members (the
// out-of-line copies are 0x10021d86 / 0x100198bc).
// FUNCTION: D3DREN 0x10013a80 _$E54
// FUNCTION: D3DREN 0x10013ad0 _$E52
// (static initialiser and atexit function of g_TranslucentObjectStateRestorer: both match once the constructor and destructor are inline members, see the
// inline constructor and destructor of UnkType_StateRestorer in d3ddevice.h)
// GLOBAL: D3DREN 0x10058c28
UnkType_StateRestorer g_TranslucentObjectStateRestorer;

// The two lazily created StateChange objects (heap, never freed).
// GLOBAL: D3DREN 0x1005a370
StateChange *g_pAlphaObjectStateChange;	// alpha blending: ALPHABLENDENABLE 1, ZWRITEENABLE 0, SRCBLEND SRCALPHA, DESTBLEND INVSRCALPHA
// GLOBAL: D3DREN 0x1005a36c
StateChange *g_pAdditiveObjectStateChange;	// additive: ALPHABLENDENABLE 1, ZWRITEENABLE 0, SRCBLEND ONE, DESTBLEND ONE

// guess (invented name): builds one of the two lazily created translucent StateChange objects (alpha blending on, z writes off,
// the given blend factors); the global is passed by reference, so every use re-reads it as in the exe.
inline void d3d_CreateTranslucentStateChange(StateChange *&pChange, uint32 srcBlend, uint32 destBlend)
{
	pChange = new StateChange(RenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1));
	pChange->Add(RenderState(D3DRENDERSTATE_ZWRITEENABLE, 0));
	pChange->Add(RenderState(D3DRENDERSTATE_SRCBLEND, srcBlend));
	pChange->Add(RenderState(D3DRENDERSTATE_DESTBLEND, destBlend));
}

// guess: switches the device to the render states of translucent objects (bAdditive: additive blending), remembering the old values
// in the saver g_TranslucentObjectStateRestorer.  The StateChange objects are built on first use.
// NAME: d3d_SetTranslucentObjectStates: Jupiter d3d_draw.cpp d3d_SetTranslucentObjectStates (names_proposal high); Talon has the StateChange form.
// Both StateChange objects are built by one inline helper (the two expansions get the same STLport inline split as in the exe).
// FUNCTION: D3DREN 0x10013ba0
void d3d_SetTranslucentObjectStates(int bAdditive)
{
	g_TranslucentObjectStateRestorer.RestoreAllStates();
	if (bAdditive)
	{
		if (!g_pAdditiveObjectStateChange)
		{
			d3d_CreateTranslucentStateChange(g_pAdditiveObjectStateChange, D3DBLEND_ONE, D3DBLEND_ONE);
			if (!g_pAdditiveObjectStateChange)
				return;
		}
		g_TranslucentObjectStateRestorer.ApplyStateChange(g_pAdditiveObjectStateChange, 0);
	}
	else
	{
		if (!g_pAlphaObjectStateChange)
		{
			d3d_CreateTranslucentStateChange(g_pAlphaObjectStateChange, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
			if (!g_pAlphaObjectStateChange)
				return;
		}
		g_TranslucentObjectStateRestorer.ApplyStateChange(g_pAlphaObjectStateChange, 0);
	}
}

// NAME: d3d_UnsetTranslucentObjectStates: Jupiter d3d_draw.cpp (names_proposal medium): puts the saved states back (the bChangeZ
// argument other units pass is not used by this version).
// FUNCTION: D3DREN 0x10013df0
void d3d_UnsetTranslucentObjectStates(int bChangeZ)
{
	g_TranslucentObjectStateRestorer.RestoreAllStates();
}

// FUNCTION: D3DREN 0x10013e00
void d3d_BeginChromaKeyPolyPass(void)
{
	if (g_CV_AlphaTest.m_IntVal)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, 1);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHAFUNC, D3DCMP_GREATER);
	}
	else
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, 1);
	}
	d3d_SetChromaKeyPass(1);
}

// guess: ends the alpha tested pass.
// FUNCTION: D3DREN 0x10013e40
void d3d_EndChromaKeyPolyPass(void)
{
	if (g_CV_AlphaTest.m_IntVal)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, 0);
	}
	else
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, 0);
	}
	d3d_SetChromaKeyPass(0);
}

// guess: grows the TL vertex scratch array g_pQueuedWorldPolyVertices to nVertices entries (keeping the old ones); 0 when the allocation fails.
// FUNCTION: D3DREN 0x10013e80
int d3d_GrowTLVertexBuffer(int nVertices)
{
	TLVertex *pNew = (TLVertex *)dalloc(nVertices * sizeof(TLVertex));

	if (!pNew)
		return 0;
	memcpy(pNew, g_pQueuedWorldPolyVertices, g_nQueuedWorldPolyVertexCapacity * sizeof(TLVertex));
	if (g_pQueuedWorldPolyVertices)
		dfree(g_pQueuedWorldPolyVertices);
	g_nQueuedWorldPolyVertexCapacity = nVertices;
	g_pQueuedWorldPolyVertices = pNew;
	return 1;
}

UnkType_PoolNode *g_pFlatWorldPolyQueue;	// guess: head of a list of polys (nodes of the pool 0x10058758)

// guess: adds pPoly (with the current clip mask) to the list g_pFlatWorldPolyQueue.
// FUNCTION: D3DREN 0x10013ef0
void d3d_QueueWorldPoly(WorldPoly *pPoly)
{
	UnkType_PoolNode *pNode = (UnkType_PoolNode *)sb_Allocate(&g_WorldPolyNodeBank);

	pNode->m_Unk00 = pPoly;
	pNode->m_Unk10 = g_pFlatWorldPolyQueue;
	pNode->m_Unk0c = g_ClipFlags;
	g_pFlatWorldPolyQueue = pNode;
}

// guess: gives every node of the list (linked through m_Unk10) back to the node pool.
// FUNCTION: D3DREN 0x100142b0
void d3d_FreeWorldPolyQueue(UnkType_PoolNode *pList)
{
	while (pList)
	{
		UnkType_PoolNode *pNext = pList->m_Unk10;

		sb_Free(&g_WorldPolyNodeBank, pList);
		pList = pNext;
	}
}

// NAME: InvalidateRect: names_proposal.csv (high, Jupiter dirtyrect.cpp InvalidateRect; unit dirtyrect)
void InvalidateRect(LTRect *pRect);		// 0x10022151
// GLOBAL: D3DREN 0x10058728
int g_nRenderScenesSinceClear;				// guess: only ever cleared by d3d_Clear

// NAME: d3d_Clear: RenderStruct::Clear (slot 0x8c; names_proposal.csv medium, Jupiter's d3d_ prefix); this version takes the
// colour as LTVector (0..255 per channel) where Jupiter's has an LTRGBColor.
// FUNCTION: D3DREN 0x100142f0
void d3d_Clear(LTRect *pRect, uint32 flags, LTVector *pColor)
{
	D3DRECT clearRect, *pClearRect;
	uint32 realFlags, dwColor;
	uint32 nRects;

	if (!g_pD3DDevice)
		return;

	if (pColor)
		dwColor = D3DRGB(pColor->x / 255.0f, pColor->y / 255.0f, pColor->z / 255.0f);
	else
		dwColor = 0xff000000;

	if (!pRect || (pRect->left == 0 && pRect->top == 0 && pRect->right == (int)g_ScreenWidth && pRect->bottom == (int)g_ScreenHeight))
	{
		pClearRect = 0;
		nRects = 0;
	}
	else
	{
		clearRect.x1 = pRect->left;
		clearRect.y1 = pRect->top;
		clearRect.x2 = pRect->right;
		clearRect.y2 = pRect->bottom;
		pClearRect = &clearRect;
		nRects = 1;
	}

	realFlags = 0;
	if (flags & CLEARSCREEN_SCREEN)
	{
		if (!pClearRect)
		{
			DDBLTFX fx;
			memset(&fx, 0, sizeof(fx));
			fx.dwSize = sizeof(fx);
			if (g_ScreenPixelFormat.m_eType == BPP_16)
			{
				GenericColor cColor;
				g_FormatMgr.PValueToFormatColor(&g_ScreenPixelFormat, dwColor, cColor);
				fx.dwFillColor = cColor.wVal;
			}
			else
			{
				fx.dwFillColor = dwColor;
			}
			g_pOffscreen->Blt(0, 0, 0, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
		}
		else
		{
			realFlags = D3DCLEAR_TARGET;
		}
	}
	if (flags & CLEARSCREEN_RENDER)
	{
		realFlags |= D3DCLEAR_ZBUFFER;
		if (CanDrawPortals())
			realFlags |= D3DCLEAR_STENCIL;
	}
	if (realFlags)
		g_pD3DDevice->Clear(nRects, pClearRect, realFlags, dwColor, 1.0f, 0);

	g_nRenderScenesSinceClear = 0;
	InvalidateRect(pRect);
}

// TransformPositionInPlace (world space -> camera space), ClipPoly (clip by the plane mask), ProjectVertexToScreen (camera -> screen): declared in pool.h / polydraw.h.
// MatVMul_InPlace_H: ltmatrix.h.

// guess: transforms the poly's vertices to the screen, adds its screen area to g_fScreenTriangleArea (for the "Overdraw" statistic) and
// queues the poly on the list g_pFlatWorldPolyQueue.
#pragma inline_depth(0)
inline float ProjectFillAreaVertex(LTMatrix *pMat, LTVector *pVec) { return MatVMul_InPlace_H(pMat,pVec); }
#pragma inline_depth()
// VC6's x87 operand order is reproduced by a separate vertex-array pointer and loop counters for the clipped projection passes.
// FUNCTION: D3DREN 0x10013f40
void d3d_AccumulateWorldPolyFillArea(WorldPoly *pPoly)
{
	D3DTLVERTEX aVertsRaw[30];	// plain POD storage: TLVertex has an LTVector member whose constructor would make the array call ??_H
	TLVertex *aVerts = (TLVertex *)aVertsRaw;
	int nVerts;
	TLVertex *pVerts;
	TLVertex *pDest;
	UnkType_PolyVertex *pSrc;
	int i;
	int k;
	int i2;
	float fArea;

	nVerts = pPoly->m_nVertices;
	pVerts = aVerts;
	TLVertex *pVerts2 = pVerts;
	pDest = pVerts2;
	pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
	for (uint16 j=(uint16)nVerts; j>0; j--)
	{
		pDest->m_Vec.x = pSrc->m_Vec->x;
		pDest->m_Vec.y = pSrc->m_Vec->y;
		pDest->m_Vec.z = pSrc->m_Vec->z;
		pSrc++;
		pDest++;
	}

	if (g_ClipFlags == 0)
	{
		pDest = pVerts2;
		for (i = nVerts; i; i--)
		{
			pDest->rhw = ProjectFillAreaVertex(&g_ViewParams.m_FullTransform, &pDest->m_Vec);
			pDest++;
		}
	}
	else
	{
		pDest = pVerts2;
		for (k = nVerts; k; k--)
		{
			TransformPositionInPlace((float *)pDest, (const float *)&g_ViewParams.m_mClipTransform);
			pDest++;
		}
		if (!ClipPoly(g_ClipFlags, &pVerts2, &nVerts))
			return;
		pDest = pVerts2;
		for (i2 = nVerts; i2; i2--)
		{
			ProjectVertexToScreen((float *)pDest, &g_ViewParams);
			pDest++;
		}
	}

	fArea = 0.0f;
	int nVerts2 = nVerts;
	for (uint16 n = 0; (int)n < nVerts2 - 2; n++)
	{
		TLVertex *a = pVerts2;
		TLVertex *c = pVerts2 + n + 2;
		TLVertex *b = pVerts2 + n + 1;

		fArea += b->m_Vec.y * a->m_Vec.x - b->m_Vec.x * a->m_Vec.y + a->m_Vec.y * c->m_Vec.x - a->m_Vec.x * c->m_Vec.y + b->m_Vec.x * c->m_Vec.y - b->m_Vec.y * c->m_Vec.x;
	}
	if (((Surface *)pPoly->m_pSurface)->m_Flags & 0x80)
		fArea += fArea;
	g_fScreenTriangleArea += fArea * 0.5f;

	d3d_QueueWorldPoly(pPoly);
}

// guess: draws the poly as one flat coloured triangle fan (ShowSplits debug view: the colour is the address of the poly, or of its
// surface when ShowSplits is 0), without texture; returns 1 when something was drawn.
// FUNCTION: D3DREN 0x10014100
int d3d_DrawFlatWorldPoly(WorldPoly *pPoly)
{
	D3DTLVERTEX aVertsRaw[40];	// see d3d_AccumulateWorldPolyFillArea
	TLVertex *aVerts = (TLVertex *)aVertsRaw;
	TLVertex *pVerts;
	int nVerts;
	uint32 dwColor;
	UnkType_PolyVertex *pSrc;
	TLVertex *pDest;
	int i;

	dwColor = (uint32)pPoly;
	if (!g_ShowSplits)
		dwColor = (uint32)pPoly->m_pSurface;

	d3d_DisableTexture(g_NormalTextureStage);

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
		nVerts = pPoly->m_nVertices;
	}

	pDest = aVerts;
	for (i = nVerts; i > 0; i--)
	{
		TLVertex *pOut = pDest;
		LTVector *pPos = pSrc->m_Vec;

		MatVMul_H(&pOut->m_Vec, &g_ViewParams.m_mIdentity, pPos);
		pOut->color = dwColor;
		pOut->specular = 0xffffffff;
		pDest++;
		pSrc++;
	}

	g_nPolygonTriangles += nVerts - 2;
	pVerts = aVerts;
	if (d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
	{
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
		g_nWorldPolysDrawn++;
		return 1;
	}
	return 0;
}

// ---- the poly draw callbacks (selected per frame by d3d_SelectWorldPolyDrawCallbacks) and the world poly flush -------------------------------------
int g_bOnePassLightmappingEnabled;	// guess: one-pass lightmapping enabled (set from the g_Force1Pass console variable unless the device cannot do it)
int g_bPolyDrawModeOne;	// guess: member of the object at 0x1005872c (the fog alpha hook g_pfnCalcFogAlpha is its first member)
int g_bPolyDrawSetupComplete;	// guess: member of the object at 0x1005872c
int g_bDrawGouraudFullbritePass;	// guess: draw state flag (Gouraud fullbrites in use)
UnkType_PoolBucket *g_pMultipassWorldPolyBuckets;	// guess: list of ... (head of a list whose nodes come from the pool at 0x10058c98)

extern void (*g_pfnDrawUntexturedWorldPoly)(WorldPoly *pPoly);
extern void (*g_pfnDrawTexturedWorldPoly)(WorldPoly *pPoly);
extern void (*g_pfnDrawPanningSkyWorldPoly)(WorldPoly *pPoly);
extern void (*g_pfnDrawLightmappedWorldPoly)(WorldPoly *pPoly);
void QueueWorldPolyWithClipFlags(WorldPoly *pPoly);		// 0x10022be4 (unit unk/10021d70): queues the poly under its texture (bucket list g_pMultipassWorldPolyBuckets)
void QueueLightmappedPoly(WorldPoly *pPoly);		// 0x100356b8 (unit unk/10034000)
void SetLightmapTextureStageStates();						// 0x100356d5
void DrawQueuedLightmappedPolys();						// 0x10034ebb
void ResetLightmapTextureStageStates();						// 0x10035771
void SetAdditiveAlphaBlendForMode(int a1);					// 0x100235bb
void d3d_NullCallback();						// 0x100235e1 (empty)
void FlushQueuedWorldPolyPages();						// 0x100235e2


// guess: selects the poly draw callbacks for the frame (nMode 1: only the queueing callback QueueWorldPolyWithClipFlags).  The exe has these statements
// in d3d_SetWorldPolyDrawMode and in the separate function d3d_SelectWorldPolyDrawCallbacks (every caller expanded them); one inline helper here.
// FUNCTION: D3DREN 0x10014e40
inline void d3d_SelectWorldPolyDrawCallbacks(int nMode)
{
	g_pfnDrawTexturedWorldPoly = QueueWorldPolyWithClipFlags;
	g_pfnDrawUntexturedWorldPoly = d3d_QueueWorldPoly;
	g_pfnDrawPanningSkyWorldPoly = QueueWorldTexturePoly;
	if (g_bOnePassLightmappingEnabled)
	{
		g_pfnDrawPanningSkyWorldPoly = QueueLightmappedPoly;
		g_pfnDrawLightmappedWorldPoly = QueueLightmappedPoly;
	}
	else
	{
		g_pfnDrawLightmappedWorldPoly = d3d_DrawOrQueueLightmappedWorldPoly;
	}
	if (g_ShowFillInfo)
	{
		g_pfnDrawPanningSkyWorldPoly = d3d_AccumulateWorldPolyFillArea;
		g_pfnDrawLightmappedWorldPoly = d3d_AccumulateWorldPolyFillArea;
		g_pfnDrawTexturedWorldPoly = d3d_AccumulateWorldPolyFillArea;
		g_pfnDrawUntexturedWorldPoly = d3d_AccumulateWorldPolyFillArea;
	}
	else if (g_DrawFlat)
	{
		g_pfnDrawPanningSkyWorldPoly = d3d_QueueWorldPoly;
		g_pfnDrawLightmappedWorldPoly = d3d_QueueWorldPoly;
		g_pfnDrawTexturedWorldPoly = d3d_QueueWorldPoly;
		g_pfnDrawUntexturedWorldPoly = d3d_QueueWorldPoly;
	}
	else if (!(g_LightMap && g_bLightmapCapable && (g_pFrameMainWorld->m_WorldFlags & WORLD_HASBASELIGHT)))
	{
		g_pfnDrawLightmappedWorldPoly = QueueWorldPolyWithClipFlags;
		g_pfnDrawPanningSkyWorldPoly = QueueWorldPolyWithClipFlags;
	}
	if (nMode == 1)
	{
		g_pfnDrawLightmappedWorldPoly = QueueWorldPolyWithClipFlags;
		g_pfnDrawPanningSkyWorldPoly = QueueWorldPolyWithClipFlags;
	}
	if (!g_pGlobalPanInfo->m_pTexture)
		g_pfnDrawPanningSkyWorldPoly = QueueWorldPolyWithClipFlags;
}

// guess: selects the poly draw callbacks for the world (nMode 1 = ...), and the lightmap pass flags.
// FUNCTION: D3DREN 0x100144b0
void d3d_SetWorldPolyDrawMode(int nMode)
{
	d3d_SelectWorldPolyDrawCallbacks(nMode);
	if (nMode == 1)
	{
		g_bPolyDrawSetupComplete = 1;
		g_bPolyDrawModeOne = 1;
	}
	else
	{
		g_bPolyDrawModeOne = 0;
		g_bPolyDrawSetupComplete = 1;
	}
	if (g_bGouraudFullbriteCapable)
		g_bDrawGouraudFullbritePass = (nMode != 1);
	else
		g_bDrawGouraudFullbritePass = 0;
	if (g_bOnePassLightmappingEnabled)
		SetLightmapTextureStageStates();
	else
		d3d_NullCallback();
}

void d3d_GetWorldPolyVertices(WorldPoly *pPoly, UnkType_PolyVertex **ppVerts, int *pnVerts);
int d3d_ProjectAndDrawTriangleFan(TLVertex **ppVerts, int *pnVerts);

// The original expansion uses this term order in its homogeneous projection.
inline float ProjectTriangleFanVertex(LTMatrix *pMat, LTVector *pSrc)
{
	float one_over_w = 1.0f / (((pMat->m[3][2] * pSrc->z + pMat->m[3][0] * pSrc->x) + pMat->m[3][1] * pSrc->y) + pMat->m[3][3]);
	LTVector temp;
	temp.x = one_over_w * (((pMat->m[0][2] * pSrc->z + pMat->m[0][0] * pSrc->x) + pMat->m[0][1] * pSrc->y) + pMat->m[0][3]);
	temp.y = one_over_w * (((pMat->m[1][2] * pSrc->z + pMat->m[1][0] * pSrc->x) + pMat->m[1][1] * pSrc->y) + pMat->m[1][3]);
	temp.z = one_over_w * (((pMat->m[2][1] * pSrc->y + pMat->m[2][2] * pSrc->z) + pMat->m[2][0] * pSrc->x) + pMat->m[2][3]);
	*pSrc = temp;
	return one_over_w;
}

// Vertex view with the original empty out-of-line constructor, shared by ICF with LTVector.
struct UnkType_FlushVertex : TLVertex
{
	UnkType_FlushVertex();
};
#pragma auto_inline(off)
// FUNCTION: D3DREN 0x10016150 ??0UnkType_FlushVertex@@QAE@XZ
UnkType_FlushVertex::UnkType_FlushVertex() {}
#pragma auto_inline(on)

// guess: draws the queued polys (list g_pFlatWorldPolyQueue, nodes {poly, mask, next}) one by one with the lightmap pass states.
// The array iterator and SDK calls remain out of line here. The first texture disable and the pool free
// are expanded explicitly, preserving the original call boundaries and per-node construction timing.
// FUNCTION: D3DREN 0x100145f0
#pragma inline_depth(0)
void d3d_FlushQueuedWorldDrawPasses(int a1)
{
	if (g_pMultipassWorldPolyBuckets)
	{
		SetAdditiveAlphaBlendForMode(a1);
		FlushQueuedWorldPolyPages();
		d3d_NullCallback();
	}
	d3d_FlushLightmapPageQueues();
	FlushWorldTexturePolys();
	if (g_bOnePassLightmappingEnabled)
		DrawQueuedLightmappedPolys();
	else
		d3d_FlushPendingWorldTextureBuckets();
	if (g_bOnePassLightmappingEnabled)
		ResetLightmapTextureStageStates();
	else
		d3d_NullCallback();

	if (g_pFlatWorldPolyQueue)
	{
		UnkType_PoolNode *pNode;
		UnkType_PoolNode *pNext;

		uint32 nStage = g_NormalTextureStage;
		if (g_pBoundTextures[nStage])
		{
			g_pD3DDevice->SetTexture(nStage, 0);
			g_pBoundTextures[nStage] = 0;
		}
		pNode = g_pFlatWorldPolyQueue;
		while (pNode)
		{
			TLVertex *pVerts;
			int nVerts;
			UnkType_PolyVertex *pSrc;
			TLVertex *pDest;
			WorldPoly *pPoly;
			uint32 dwColor;
			int i;

			pNext = pNode->m_Unk10;
			g_ClipFlags = pNode->m_Unk0c;
			pPoly = (WorldPoly *)pNode->m_Unk00;
			UnkType_FlushVertex aVerts[40];
			dwColor = g_ShowSplits ? (uint32)pPoly : (uint32)pPoly->m_pSurface;
			d3d_UnsetTexture(g_NormalTextureStage);
			d3d_GetWorldPolyVertices(pPoly, &pSrc, &nVerts);
			pDest = aVerts;
			for (i = nVerts; i > 0; i--)
			{
				MatVMul_H(&pDest->m_Vec, &g_ViewParams.m_mIdentity, pSrc->m_Vec);
				pDest->color = dwColor;
				pDest->specular = 0xffffffff;
				pDest++;
				pSrc++;
			}
			g_nPolygonTriangles += nVerts - 2;
			pVerts = aVerts;
			d3d_ProjectAndDrawTriangleFan(&pVerts, &nVerts);
			if (pNode && &g_WorldPolyNodeBank)
			{
				StructLink *pLink = (StructLink*)pNode;
				pLink->m_pSLNext = g_WorldPolyNodeBank.m_FreeListHead;
				g_WorldPolyNodeBank.m_FreeListHead = pLink;
			}
			pNode = pNext;
		}
		g_pFlatWorldPolyQueue = 0;
	}
	if (g_CV_DrawPolyMgr.m_IntVal)
		g_DrawPolyMgr.FlushQueuedPolys();
}

#pragma inline_depth()
// guess: the vertex array and the vertex count of the poly (honours FixTJunc).
// FUNCTION: D3DREN 0x100147b0
void d3d_GetWorldPolyVertices(WorldPoly *pPoly, UnkType_PolyVertex **ppVerts, int *pnVerts)
{
	if (g_FixTJunc)
	{
		*ppVerts = (UnkType_PolyVertex *)pPoly->m_pVertices;
		*pnVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		*ppVerts = (UnkType_PolyVertex *)pPoly->m_Vertices;
		*pnVerts = pPoly->m_nVertices;
	}
}

// guess: projects (clipping when g_ClipFlags is set) and draws the vertices as a triangle fan; 0 when nothing is left.
// FUNCTION: D3DREN 0x100147f0
int d3d_ProjectAndDrawTriangleFan(TLVertex **ppVerts, int *pnVerts)
{
	TLVertex *pVert;
	int i;

	if (g_ClipFlags == 0)
	{
		pVert = *ppVerts;
		for (i = *pnVerts; i != 0; i--)
		{
			pVert->rhw = ProjectTriangleFanVertex(&g_ViewParams.m_FullTransform, &pVert->m_Vec);
			pVert++;
		}
	}
	else
	{
		pVert = *ppVerts;
		for (i = *pnVerts; i != 0; i--)
		{
			TransformPositionInPlace((float *)pVert, (const float *)&g_ViewParams.m_mClipTransform);
			pVert++;
		}
		if (ClipPoly(g_ClipFlags, ppVerts, pnVerts))
		{
			pVert = *ppVerts;
			for (i = *pnVerts; i != 0; i--)
			{
				ProjectVertexToScreen((float *)pVert, &g_ViewParams);
				pVert++;
			}
		}
		else
			goto Empty;
	}
	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, *ppVerts, *pnVerts, 0);
	g_nWorldPolysDrawn++;
	return 1;
Empty:
	return 0;
}

// Taking the elements by value preserves the target's complete source snapshot before writing the transpose.
inline void StoreTransposedD3DMatrixElements(D3DMATRIX *out,
	float m00, float m01, float m02, float m03,
	float m10, float m11, float m12, float m13,
	float m20, float m21, float m22, float m23,
	float m30, float m31, float m32, float m33)
{
	out->_11 = m00; out->_12 = m10; out->_13 = m20; out->_14 = m30;
	out->_21 = m01; out->_22 = m11; out->_23 = m21; out->_24 = m31;
	out->_31 = m02; out->_32 = m12; out->_33 = m22; out->_34 = m32;
	out->_41 = m03; out->_42 = m13; out->_43 = m23; out->_44 = m33;
}

// guess: LTMatrix -> D3DMATRIX (transposes: D3D's rows are the columns of the LTMatrix; Jupiter d3d_SetD3DMat's copy).
// FUNCTION: D3DREN 0x10014980
void d3d_CopyLTMatrixToD3D(const LTMatrix *pSrc, D3DMATRIX *pOut)
{
	StoreTransposedD3DMatrixElements(pOut,
		pSrc->m[0][0], pSrc->m[0][1], pSrc->m[0][2], pSrc->m[0][3],
		pSrc->m[1][0], pSrc->m[1][1], pSrc->m[1][2], pSrc->m[1][3],
		pSrc->m[2][0], pSrc->m[2][1], pSrc->m[2][2], pSrc->m[2][3],
		pSrc->m[3][0], pSrc->m[3][1], pSrc->m[3][2], pSrc->m[3][3]);
}

// ---- d3d_FullDrawScene and the world poly flush ---------------------------------------------------------------------------------
void d3d_FlushFlatWorldPolys(UnkType_PoolNode *pList);
void d3d_DrawLightAddPoly(const LTVector &vAdd);
void d3d_DrawLightScalePoly(const LTVector &vScale);
// d3d_ClearFrameLitPolys (the wrapper) / d3d_FreeLitPolyList: ends the lit lists of the frame (sys/d3d/common_stuff.cpp).
void d3d_ClearFrameLitPolys();
void d3d_FreeLitPolyList();
void TagWorldVisibility();
void d3d_FlushLightmapPageQueues(void);

// Jupiter d3d_SetD3DMat: the transposed LTMatrix as a D3D transform (d3d_CopyLTMatrixToD3D does the copy, out of line here).
static inline void d3d_SetD3DMat(D3DTRANSFORMSTATETYPE iTransform, LTMatrix *pSrc)
{
	D3DMATRIX Out;

	d3d_CopyLTMatrixToD3D(pSrc, &Out);
	g_pD3DDevice->SetTransform(iTransform, &Out);
}

// Jupiter d3d_ProcessObjectList (the Talon version has no FLAG_VISIBLE test).
static inline void d3d_ProcessObjectList(LTObject **pObjectList, int objectListSize)
{
	int i;
	LTObject *pObject;

	for (i = 0; i < objectListSize; i++)
	{
		pObject = pObjectList[i];
		if (pObject)
		{
			if (((char)pObject->m_ObjectType >= 0) && ((char)pObject->m_ObjectType < 11))
			{
				if (g_ObjectHandlers[(char)pObject->m_ObjectType].m_ProcessObjectFn)
					g_ObjectHandlers[(char)pObject->m_ObjectType].m_ProcessObjectFn(pObject);
			}
		}
	}
}

// FullDrawScene calls this helper out of line; d3d_SetWorldPolyDrawMode expands the same body.
#pragma inline_depth(0)
inline void SelectWorldPolyDrawCallbacksForScene(int nMode) { d3d_SelectWorldPolyDrawCallbacks(nMode); }
#pragma inline_depth()

// NAME: d3d_FullDrawScene: Ghidra name (high; Jupiter d3d_draw.cpp d3d_FullDrawScene).  The Talon version takes only the SceneDesc (the view is
// g_ViewParams) and draws the world itself (Jupiter: d3d_TagVisibleLeaves(Params)).
// FUNCTION: D3DREN 0x10014a40
void d3d_FullDrawScene(SceneDesc *pDesc)
{
	Counter cUnused;

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_CLIPPING, g_CV_UseD3DClip.m_IntVal != 0);
	d3d_SetD3DMat(D3DTRANSFORMSTATE_WORLD, &g_ViewParams.m_mIdentity);
	d3d_SetD3DMat(D3DTRANSFORMSTATE_VIEW, &g_ViewParams.m_mView);
	d3d_SetD3DMat(D3DTRANSFORMSTATE_PROJECTION, &g_ViewParams.m_mProjection);
	d3d_InitObjectQueues();
	d3d_ClearFrameLitPolys();

	if (pDesc->m_DrawMode == DRAWMODE_OBJECTLIST)
	{
		d3d_GetVisibleSet()->ClearSet();
		d3d_ProcessObjectList(pDesc->m_pObjectList, pDesc->m_ObjectListSize);
	}
	else
	{
		CountAdder cntAdd(&g_pStruct->m_Ticks_TagVisibleLeaves);

		SelectWorldPolyDrawCallbacksForScene(0);
		g_bPolyDrawModeOne = 0;
		g_bDrawGouraudFullbritePass = (g_bGouraudFullbriteCapable != 0);
		g_bPolyDrawSetupComplete = 1;
		if (g_bOnePassLightmappingEnabled)
			SetLightmapTextureStageStates();
		else
			d3d_NullCallback();
		TagWorldVisibility();

		if (g_pMultipassWorldPolyBuckets)
		{
			SetAdditiveAlphaBlendForMode(0);
			FlushQueuedWorldPolyPages();
			d3d_NullCallback();
		}
		d3d_FlushLightmapPageQueues();
		FlushWorldTexturePolys();
		if (g_bOnePassLightmappingEnabled)
			DrawQueuedLightmappedPolys();
		else
			d3d_FlushPendingWorldTextureBuckets();
		if (g_bOnePassLightmappingEnabled)
			ResetLightmapTextureStageStates();
		else
			d3d_NullCallback();
		if (g_pFlatWorldPolyQueue)
		{
			d3d_UnsetTexture(g_NormalTextureStage);	// the exe calls the out-of-line copy (unit unk/100098d0) here
			d3d_FlushFlatWorldPolys(g_pFlatWorldPolyQueue);
			g_pFlatWorldPolyQueue = 0;
		}
		if (g_CV_DrawPolyMgr.m_IntVal)
			g_DrawPolyMgr.FlushQueuedPolys();
	}

	{
		CountAdder cntAdd(pDesc->m_pTicks_Render_Objects);
		CountAdder cntAdd2(&g_pStruct->m_Ticks_FlushObjectQueues);

		d3d_FlushObjectQueues();
	}
	d3d_FreeLitPolyList();
	d3d_DrawLightScalePoly(pDesc->m_GlobalLightScale);
	d3d_DrawLightAddPoly(pDesc->m_GlobalLightAdd);
}

// guess: draws the queued polys (list pList: nodes {poly, mask, next}) flat coloured (colour = address of the poly, ShowSplits) and
// gives the nodes back.
// Preserve the original out-of-line projection call within the vertex loop.
#pragma inline_depth(0)
inline float TransformFlatPolyPosition(LTVector *pDest, LTMatrix *pMat, LTVector *pSrc)
{
	return MatVMul_H(pDest, pMat, pSrc);
}
#pragma inline_depth()

// FUNCTION: D3DREN 0x10014ce0
void d3d_FlushFlatWorldPolys(UnkType_PoolNode *pList)
{
	D3DTLVERTEX aVertsRaw[40];	// see d3d_AccumulateWorldPolyFillArea
	TLVertex *aVerts = (TLVertex *)aVertsRaw;
	TLVertex *pVerts;
	int nVerts;
	UnkType_PoolNode *pNode;
	UnkType_PoolNode *pNext;

	for (pNode = pList; pNode; pNode = pNext)
	{
		UnkType_PolyVertex *pSrc;
		TLVertex *pDest;
		WorldPoly *pPoly;
		uint32 dwColor;
		int i;

		pNext = pNode->m_Unk10;
		g_ClipFlags = pNode->m_Unk0c;
		pPoly = (WorldPoly *)pNode->m_Unk00;
		dwColor = (uint32)pPoly;
		if (!g_ShowSplits)
			dwColor = (uint32)pPoly->m_pSurface;
		d3d_DisableTexture(g_NormalTextureStage);
		if (g_FixTJunc)
		{
			pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
			nVerts = pPoly->m_nExtraVertices;
		}
		else
		{
			pSrc = (UnkType_PolyVertex *)pPoly->m_Vertices;
			nVerts = pPoly->m_nVertices;
		}
		pDest = aVerts;
		for (i = nVerts; i > 0; i--)
		{
			TransformFlatPolyPosition(&pDest->m_Vec, &g_ViewParams.m_mIdentity, pSrc->m_Vec);
			pDest->color = dwColor;
			pDest->specular = 0xffffffff;
			pDest++;
			pSrc++;
		}
		g_nPolygonTriangles += nVerts - 2;
		pVerts = aVerts;
		if (d3d_ClipAndProjectTLVertices(&pVerts, &nVerts, &g_ViewParams, 0))
		{
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
			g_nWorldPolysDrawn++;
		}
		sb_Free(&g_WorldPolyNodeBank, pNode);
	}
}

// The exe's TLVertex array statics of the two functions below have an (empty) constructor and destructor: the destructor is the
// atexit stub FUN_10015230 / FUN_100155a0 (`ret`).  Local view type, tlvertex.h's TLVertex is a plain struct.
struct UnkType_TLVertexCD : public TLVertex
{
	UnkType_TLVertexCD() {}
	~UnkType_TLVertexCD() {}
};

// NAME: d3d_DrawLightAddPoly: Ghidra name (high, Jupiter d3d_draw.cpp d3d_DrawLightAddPoly); the Talon version takes only the colour and
// uses the view rectangle g_ViewParams.m_Rect.
// FUNCTION: D3DREN 0x10014f20
void d3d_DrawLightAddPoly(const LTVector &vAdd)
{
	static UnkType_TLVertexCD verts[6];
	union { TLRGB rgb; uint32 color; } theColor;

	if (!g_LightAddPoly || !g_bLightAddPolyCapable)
		return;
	if ((vAdd.x < 0.001f) && (vAdd.y < 0.001f) && (vAdd.z < 0.001f))
		return;

	theColor.rgb.r = (uint8)(vAdd.x * 255.0f);
	theColor.rgb.g = (uint8)(vAdd.y * 255.0f);
	theColor.rgb.b = (uint8)(vAdd.z * 255.0f);
	theColor.rgb.a = 0xff;

	verts[0].m_Vec.x = (float)g_ViewParams.m_Rect.left;
	verts[0].m_Vec.y = (float)g_ViewParams.m_Rect.top;
	verts[0].m_Vec.z = 0.0f;
	verts[0].color = theColor.color;

	verts[1].m_Vec.x = (float)g_ViewParams.m_Rect.right;
	verts[1].m_Vec.y = (float)g_ViewParams.m_Rect.top;
	verts[1].m_Vec.z = 0.0f;
	verts[1].color = theColor.color;

	verts[2].m_Vec.x = (float)g_ViewParams.m_Rect.right;
	verts[2].m_Vec.y = (float)g_ViewParams.m_Rect.bottom;
	verts[2].m_Vec.z = 0.0f;
	verts[2].color = theColor.color;

	verts[3].m_Vec.x = (float)g_ViewParams.m_Rect.left;
	verts[3].m_Vec.y = (float)g_ViewParams.m_Rect.bottom;
	verts[3].m_Vec.z = 0.0f;
	verts[3].color = theColor.color;

	d3d_DisableTexture(g_NormalTextureStage);

	StateSet ssFogEnable(D3DRENDERSTATE_FOGENABLE, FALSE);
	StateSet ssZEnable(D3DRENDERSTATE_ZENABLE, FALSE);
	StateSet ssZWriteEnable(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
	StateSet ssAlphaBlendEnable(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
	StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
	StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, verts, 4, 0);
}

// NAME: d3d_DrawLightScalePoly: Ghidra name (high, Jupiter d3d_draw.cpp d3d_DrawLightScalePoly); Talon version, see AddPoly.
// atexit destructor stub of the function-static vertex array of d3d_DrawLightAddPoly (the element destructor is empty: a bare `ret` besides the
// ??_I array destructor iterator).
// FUNCTION: D3DREN 0x10015230 _$E57
// FUNCTION: D3DREN 0x10015240
void d3d_DrawLightScalePoly(const LTVector &vScale)
{
	static UnkType_TLVertexCD verts[6];
	union { TLRGB rgb; uint32 color; } theColor;
	float fMul;

	if ((vScale.x > 0.99f) && (vScale.y > 0.99f) && (vScale.z > 0.99f) &&
		(vScale.x < 1.01f) && (vScale.y < 1.01f) && (vScale.z < 1.01f))
	{
		if (!g_InvertHack)
			return;
		fMul = 255.0f;
	}
	else
	{
		fMul = g_InvertHack ? 255.0f : 127.5f;
	}

	theColor.rgb.r = (uint8)(vScale.x * fMul);
	theColor.rgb.g = (uint8)(vScale.y * fMul);
	theColor.rgb.b = (uint8)(vScale.z * fMul);
	theColor.rgb.a = 0xff;

	verts[0].m_Vec.x = (float)g_ViewParams.m_Rect.left;
	verts[0].m_Vec.y = (float)g_ViewParams.m_Rect.top;
	verts[0].m_Vec.z = 0.0f;
	verts[0].color = theColor.color;

	verts[1].m_Vec.x = (float)g_ViewParams.m_Rect.right;
	verts[1].m_Vec.y = (float)g_ViewParams.m_Rect.top;
	verts[1].m_Vec.z = 0.0f;
	verts[1].color = theColor.color;

	verts[2].m_Vec.x = (float)g_ViewParams.m_Rect.right;
	verts[2].m_Vec.y = (float)g_ViewParams.m_Rect.bottom;
	verts[2].m_Vec.z = 0.0f;
	verts[2].color = theColor.color;

	verts[3].m_Vec.x = (float)g_ViewParams.m_Rect.left;
	verts[3].m_Vec.y = (float)g_ViewParams.m_Rect.bottom;
	verts[3].m_Vec.z = 0.0f;
	verts[3].color = theColor.color;

	d3d_DisableTexture(g_NormalTextureStage);

	StateSet ssFogEnable(D3DRENDERSTATE_FOGENABLE, FALSE);
	StateSet ssZEnable(D3DRENDERSTATE_ZENABLE, FALSE);
	StateSet ssZWriteEnable(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
	StateSet ssAlphaBlendEnable(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
	StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, g_InvertHack ? D3DBLEND_INVDESTCOLOR : D3DBLEND_DESTCOLOR);
	StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, g_InvertHack ? D3DBLEND_ZERO : D3DBLEND_SRCCOLOR);

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, verts, 4, 0);
}

// atexit destructor stub of the static vertex array of d3d_DrawLightScalePoly.
// FUNCTION: D3DREN 0x100155a0 _$E59

// guess: draws the mirror poly itself (pPoly, a world poly with the mirror surface) over the reflected view: ALPHABLENDENABLE on through a state saver,
// every vertex projected with the matrix handed in (MatVMul_H, the 1/w it returns is dropped), white diffuse, the stage 0 texture scale applied to the
// vertex uv, the per-vertex fog hook for the specular alpha, clipped with the plane mask 0x3f and drawn as a triangle fan.  Only called by the mirror pass
// when the surface has the overlay bit.  The exe expands the inline d3d_SetTexture here; the call below is the out-of-line one.
// STUB diagnosis (W2): 560 of 624 bytes, 198 aligned mismatches.  Statement order, calls, constants and the loop are those of the exe.  Differences: (1) the exe expands the
//   inline d3d_SetTexture (`mov edi,[pTexture+0xc]` ... d3d_CreateAndLoadTexture / d3d_BindRTexture / SetLOD), ours calls the out-of-line copy; (2) the exe calls the two vector member
//   destructors out of line (0x10018aa0 / 0x100188e0, the node allocator's deallocate expanded inside them) after the inline ~UnkType_StateRestorer body (RestoreAllStates),
//   ours expands them.  With the plain inline d3d_SetTexture of d3d_texture.h (tried) the destructors do come out of line (as
//   ??1?$_Vector_base@URenderState, 80 bytes) but the function grows to 672 bytes and the copy of __node_alloc::deallocate (0x10018f80) is no longer emitted separately,
//   so the option is left off; the exe's ~_Vector_base copies (96 / 112 bytes) have `_STL_alloc_proxy::deallocate` and the node allocator inlined, ours differ in that.
// STUB: D3DREN 0x100155b0
void d3d_DrawMirrorSurfaceOverlay(WorldPoly *pPoly, LTMatrix *pMatrix)
{
	D3DTLVERTEX aVerts[0x80];
	UnkType_PolyVert *pSrc;
	D3DTLVERTEX *pVerts;
	int nVerts;
	int i;

	if (!d3d_SetTexture(((Surface *)pPoly->m_pSurface)->m_pTexture, g_NormalTextureStage, 0))
		return;

	{
	UnkType_StateRestorer saver;
	RenderState rsBlend(D3DRENDERSTATE_ALPHABLENDENABLE, 1);

	saver.ApplyRenderState(rsBlend);

	if (g_FixTJunc)
	{
		pSrc = (UnkType_PolyVert *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVert *)pPoly->m_Vertices;
		nVerts = pPoly->m_nVertices;
	}

	pVerts = aVerts;
	for (i = nVerts; i > 0; i--)
	{
		LTVector *pPos = pSrc->m_pPos;
		TLVertex *pDest = (TLVertex *)&pVerts[nVerts - i];

		MatVMul_H((LTVector *)&pDest->m_Vec, pMatrix, pPos);
		pDest->color = 0xffffffff;
		pDest->tu = g_TextureStageTexelSizes[0].m_Unk00 * pSrc->m_Unk04;
		pDest->tv = g_TextureStageTexelSizes[0].m_Unk04 * pSrc->m_Unk08;
		g_pfnCalcFogAlpha(pPos, &pDest->specular);
		pSrc++;
	}

	g_ClipFlags = 0x3f;
	if (d3d_ClipAndProjectTLVertices((TLVertex **)&pVerts, &nVerts, &g_ViewParams, 0))
	{
		g_nPolygonTriangles += nVerts - 2;
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
	}
	saver.RestoreAllStates();
	}
}

extern void (*g_pfnDrawVisibleReflections)();
void d3d_IncrementFrameCode(RenderContext *pContext);
void d3d_InitViewBox2(ViewBoxDef *pDef, float nearZ, float farZ,
	const ViewParams &prevParams, float minX, float minY, float maxX, float maxY);
LTBOOL d3d_InitFrustum2(ViewParams *pParams, ViewBoxDef *pViewBox,
	float minX, float minY, float maxX, float maxY, LTMatrix *pMat, LTVector vScale);

// GLOBAL: D3DREN 0x10048c50
int g_bPortalStencilClipEnabled = 1;

// Local matching forms preserve the target's render-state call sequence and SDK call depth.
// These helpers expand inline; they add no stand-in code or data.
inline void SetReflectionRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
	g_pD3DDevice->SetRenderState(state, value);
}

#pragma inline_depth(0)
inline float TransformReflectionPolyPosition(LTVector *d, LTMatrix *a, LTVector *v) { return MatVMul_H(d, a, v); }
inline void GetReflectionPolyVertices(WorldPoly *p, UnkType_PolyVertex **v, int *n) { d3d_GetWorldPolyVertices(p, v, n); }
#pragma inline_depth()

inline float GetReflectedViewCullSign(uint32 flip)
{
	float sign;
	if (flip)
		sign = -1.0f;
	else
		sign = 1.0f;
	return sign;
}

// Reflect each front-facing mirror polygon into a stencil-bounded recursive scene draw.
// The local polygon list survives the recursive visible-set rebuild; the full view and lightmap setting are restored.
// FUNCTION: D3DREN 0x10015820
void d3d_DrawReflectedWorldScenes(SceneDesc *pDesc, VisibleSet *pSet)
{
	ViewParams backupView;
	D3DTLVERTEX vertices[40];
	VisibleSet::SortedPoly pairs[32];
	uint32 nPolys;
	uint32 i;

	g_pfnDrawVisibleReflections = 0;
	nPolys = pSet->m_nUnk140;
	if (!nPolys)
		return;

	for (i = 0; i < nPolys; ++i)
		pairs[i] = pSet->m_Unk40[i];

	RenderContext *pContext = (RenderContext *)pDesc->m_hRenderContext;
	for (i = 0; i < nPolys; ++i)
	{
		VisibleSet::SortedPoly *pPair = &pairs[i];
		WorldPoly *pPoly = pPair->m_pPoly;
		LTMatrix *pMatrix = (LTMatrix *)pPair->m_pUnk;
		Surface *pSurface = (Surface *)pPoly->m_pSurface;
		if ((pSurface->m_Unknown3A & 0x7fff) != 0x7ffe)
			continue;

		LTPlane plane = pMatrix->TransformPlane(*pPoly->m_pPlane);
		if (plane.DistTo(g_ViewParams.m_Pos) < 0.0f)
			continue;

		LTMatrix cameraWorld;
		cameraWorld.Identity();
		cameraWorld.SetBasisVectors(&g_ViewParams.m_Right, &g_ViewParams.m_Up, &g_ViewParams.m_Forward);
		cameraWorld.SetTranslation(g_ViewParams.m_Pos);

		LTMatrix reflected;
		reflected.SetupReflectionMatrix(plane.m_Normal, plane.m_Normal * plane.m_Dist);
		LTMatrix copiedCamera = reflected * cameraWorld;

		d3d_IncrementFrameCode(pContext);
		g_CurFrameCode = pContext->m_CurFrameCode;
		g_CurObjectFrameCode = g_pStruct->IncObjectFrameCode();

		SetReflectionRenderState(D3DRENDERSTATE_STENCILENABLE, 1);
		SetReflectionRenderState(D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_REPLACE);
		SetReflectionRenderState(D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS);
		SetReflectionRenderState(D3DRENDERSTATE_STENCILREF, 1);
		g_ClipFlags = 0x3f;

		uint32 dwColor;
		if (g_ShowSplits)
			dwColor = (uint32)pPoly;
		else
			dwColor = (uint32)pPoly->m_pSurface;
		d3d_UnsetTexture(g_NormalTextureStage);

		UnkType_PolyVertex *pSrc;
		int nVerts;
		GetReflectionPolyVertices(pPoly, &pSrc, &nVerts);
		TLVertex *pDest = (TLVertex *)vertices;
		for (int j = nVerts; j > 0; --j)
		{
			TransformReflectionPolyPosition(&pDest->m_Vec, pMatrix, pSrc->m_Vec);
			pDest->color = dwColor;
			pDest->specular = 0xffffffff;
			++pDest;
			++pSrc;
		}
		g_nPolygonTriangles += nVerts - 2;

		TLVertex *pVerts = (TLVertex *)vertices;
		if (d3d_ProjectAndDrawTriangleFan(&pVerts, &nVerts))
		{
			SetReflectionRenderState(D3DRENDERSTATE_STENCILREF, 1);
			SetReflectionRenderState(D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP);
			SetReflectionRenderState(D3DRENDERSTATE_STENCILFUNC, D3DCMP_EQUAL);

			LTVector vMin(100000.0f, 100000.0f, 100000.0f);
			LTVector vMax(-100000.0f, -100000.0f, -100000.0f);
			float depth = g_ViewParams.m_FarZ - 1.0f;
			float rhw = 1.0f / depth;
			float z = (depth * g_ViewParams.m_fProjZScale + g_ViewParams.m_fProjZOffset) * rhw;
			for (int j = 0; j < nVerts; ++j)
			{
				TLVertex *pVert = pVerts + j;
				pVert->rhw = rhw;
				pVert->m_Vec.z = z;
				pVert->rgb.r = 255;
				pVert->rgb.b = 0;
				pVert->rgb.g = 0;
				VEC_MIN(vMin, vMin, pVert->m_Vec);
				VEC_MAX(vMax, vMax, pVert->m_Vec);
			}

			SetReflectionRenderState(D3DRENDERSTATE_ZENABLE, 1);
			SetReflectionRenderState(D3DRENDERSTATE_ZWRITEENABLE, 1);
			SetReflectionRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_ALWAYS);
			g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
			SetReflectionRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);

			if (!g_bPortalStencilClipEnabled || g_CV_ShowPortalBounds.m_IntVal)
			{
				SetReflectionRenderState(D3DRENDERSTATE_STENCILENABLE, 0);
				SetReflectionRenderState(D3DRENDERSTATE_STENCILFUNC, D3DCMP_EQUAL);
			}

			backupView = g_ViewParams;
			ViewBoxDef viewBox;
			d3d_InitViewBox2(&viewBox, g_CV_NearZ.m_FloatVal, pDesc->m_FarZ, g_ViewParams,
				vMin.x, vMin.y, vMax.x, vMax.y);
			d3d_InitFrustum2(&g_ViewParams, &viewBox, vMin.x, vMin.y, vMax.x, vMax.y,
				&copiedCamera, LTVector(1.0f, 1.0f, 1.0f));
			*(LTVector *)g_ViewParams.m_Pad4d8 = pPoly->m_Center;
			g_ViewParams.m_bCullFlip = !backupView.m_bCullFlip;
			g_ViewParams.m_fCullSign = GetReflectedViewCullSign(g_ViewParams.m_bCullFlip);
			g_ViewParams.m_bPortalView = 1;

			int oldLightMap = g_LightMap;
			if (!g_CV_PortalLightmap.m_IntVal)
				g_LightMap = 0;
			d3d_FullDrawScene(pDesc);
			g_LightMap = oldLightMap;
			g_ViewParams = backupView;

			SetReflectionRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_ALWAYS);
			if (pSurface->m_Unknown3A & 0x8000)
				d3d_DrawMirrorSurfaceOverlay(pPoly, pMatrix);
			SetReflectionRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
		}

		SetReflectionRenderState(D3DRENDERSTATE_STENCILENABLE, 0);
	}
}


// ---- (merged from the scratch unit w2scratch/drawB) ----
// Prototypes of functions of other parts / units (identical to the declarations the other units use).
int d3d_ClipTLVertexLine(float *pVerts, int nMask);					// 0x100161e0 (scratch unit scene): clips the 2 TL vertices of a line
void ProjectVertexToScreen(float *pVert, const void *pViewParams);	// 0x10008895 (unit unk/10007930)

// d3d_DrawLine expands ProjectVertexToScreen (camera space -> screen space, out-of-line copy in unit unk/10007930) inline while the other callers in this
// object call it out of line; a separate inline helper with the same body keeps the two apart (the real source had one inline function whose
// expansion the other callers' inline budget refused).  Same body as the out-of-line copy except for the x87-friendly pVert[3] reads.
inline void ProjectVertexToScreen_inline(float *pVert, const void *pViewParams)
{
	const ViewParams *pView = (const ViewParams *)pViewParams;

	pVert[3] = 1.0f / pVert[2];
	pVert[0] = (pView->m_fProjXScale * pVert[0] + pView->m_fProjXOffset * pVert[2]) * pVert[3];
	pVert[1] = (pView->m_fProjYScale * pVert[1] + pView->m_fProjYOffset * pVert[2]) * pVert[3];
	pVert[2] = (pView->m_fProjZScale * pVert[2] + pView->m_fProjZOffset) * pVert[3];
}

// ---- d3d_ClipTLVertexLine: clips the 3D line of two TL vertices ---------------------------------------------------------------------------
// Jupiter's clipline.h (the line version of polyclip.h) expanded once per plane with CLIPTEST/DOCLIP; the exe's ClipExtra (Jupiter polyclip.h
// TLVertex::ClipExtra: interpolates tu, tv and the colour bytes r, g, b, a and the specular alpha) is expanded inline each time.
static inline void TLVertex_ClipExtra_Line(TLVertex *pPrev, TLVertex *pCur, TLVertex *pOut, float t)
{
	pOut->tu = (pCur->tu - pPrev->tu) * t + pPrev->tu;
	pOut->tv = (pCur->tv - pPrev->tv) * t + pPrev->tv;
	pOut->rgb.r = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.r - pPrev->rgb.r) * t + (float)pPrev->rgb.r);
	pOut->rgb.g = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.g - pPrev->rgb.g) * t + (float)pPrev->rgb.g);
	pOut->rgb.b = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.b - pPrev->rgb.b) * t + (float)pPrev->rgb.b);
	pOut->rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->rgb.a - pPrev->rgb.a) * t + (float)pPrev->rgb.a);
	pOut->specular_rgb.a = (uint8)RoundFloatToInt((float)(int)(pCur->specular_rgb.a - pPrev->specular_rgb.a) * t + (float)pPrev->specular_rgb.a);
}

// clipline.h text with CLIPTEST and DOCLIP as macro parameters.
#define CLIPLINE_PLANE(CLIPTEST, DOCLIP) \
	if (CLIPTEST(pVerts[0].m_Vec)) \
	{ \
		if (!CLIPTEST(pVerts[1].m_Vec)) \
		{ \
			pOut = &pVerts[1]; \
			DOCLIP(pVerts[0].m_Vec, pVerts[1].m_Vec); \
			TLVertex_ClipExtra_Line(&pVerts[0], &pVerts[1], pOut, t); \
		} \
	} \
	else \
	{ \
		if (CLIPTEST(pVerts[1].m_Vec)) \
		{ \
			pOut = &pVerts[0]; \
			DOCLIP(pVerts[0].m_Vec, pVerts[1].m_Vec); \
			TLVertex_ClipExtra_Line(&pVerts[0], &pVerts[1], pOut, t); \
		} \
		else \
			return 0; \
	}

// near plane z == g_ViewParams.m_NearZ (flag 1)
#define CLIPTEST_NEAR(v)	((v).z >= g_ViewParams.m_NearZ)
#define DOCLIP_NEAR(p0, p1) \
	t = (g_ViewParams.m_NearZ - (p0).z) / ((p1).z - (p0).z); \
	pOut->m_Vec.x = ((p1).x - (p0).x) * t + (p0).x; \
	pOut->m_Vec.y = ((p1).y - (p0).y) * t + (p0).y; \
	pOut->m_Vec.z = g_ViewParams.m_NearZ
// left plane x + z == 0 (flag 4)
#define CLIPTEST_LEFT(v)	((v).x > -(v).z)
#define DOCLIP_LEFT(p0, p1) \
	t = -(((p0).z + (p0).x) / ((((p1).x - (p0).x) + (p1).z) - (p0).z)); \
	pOut->m_Vec.y = ((p1).y - (p0).y) * t + (p0).y; \
	z = ((p1).z - (p0).z) * t + (p0).z; \
	pOut->m_Vec.z = z; \
	pOut->m_Vec.x = -z
// top plane y == z (flag 8)
#define CLIPTEST_TOP(v)		((v).y < (v).z)
#define DOCLIP_TOP(p0, p1) \
	t = -(((p0).y - (p0).z) / ((((p1).y - (p0).y) - (p1).z) + (p0).z)); \
	pOut->m_Vec.x = ((p1).x - (p0).x) * t + (p0).x; \
	z = ((p1).z - (p0).z) * t + (p0).z; \
	pOut->m_Vec.z = z; \
	pOut->m_Vec.y = z
// right plane x == z (flag 0x10)
#define CLIPTEST_RIGHT(v)	((v).x < (v).z)
#define DOCLIP_RIGHT(p0, p1) \
	t = -(((p0).x - (p0).z) / ((((p1).x - (p0).x) - (p1).z) + (p0).z)); \
	pOut->m_Vec.y = ((p1).y - (p0).y) * t + (p0).y; \
	z = ((p1).z - (p0).z) * t + (p0).z; \
	pOut->m_Vec.z = z; \
	pOut->m_Vec.x = z
// bottom plane y + z == 0 (flag 0x20)
#define CLIPTEST_BOTTOM(v)	((v).y > -(v).z)
#define DOCLIP_BOTTOM(p0, p1) \
	t = -(((p0).y + (p0).z) / ((((p1).y - (p0).y) + (p1).z) - (p0).z)); \
	pOut->m_Vec.x = ((p1).x - (p0).x) * t + (p0).x; \
	z = ((p1).z - (p0).z) * t + (p0).z; \
	pOut->m_Vec.z = z; \
	pOut->m_Vec.y = -z
// far plane z == g_ViewParams.m_ClipFarZ (flag 2)
#define CLIPTEST_FAR(v)		((v).z <= g_ViewParams.m_ClipFarZ)
#define DOCLIP_FAR(p0, p1) \
	t = (g_ViewParams.m_ClipFarZ - (p0).z) / ((p1).z - (p0).z); \
	pOut->m_Vec.x = ((p1).x - (p0).x) * t + (p0).x; \
	pOut->m_Vec.y = ((p1).y - (p0).y) * t + (p0).y; \
	pOut->m_Vec.z = g_ViewParams.m_ClipFarZ

// guess: clips the line of pVerts[0], pVerts[1] (camera space TL vertices) in place against the planes of nMask (1 near, 4 left, 8 top, 0x10 right,
// 0x20 bottom, 2 far; the callers pass 0x3f); returns 0 when the whole line is outside.  Jupiter has the macro file but no function around it.
// STUB diagnosis (W2): 116 of 3952 bytes differ (same size, same instruction count 1373, 56 aligned mismatches of which ~16 are relocation operands): the
//   structure of all six planes, both branches and every DOCLIP matches.  Two small differences remain: (1) the integer loads of the 4th ClipExtra channel
//   (rgb.a) are scheduled differently in all 12 expansions (exe `mov al,[a0]; mov [ebp+8],eax; xor ebx,ebx; mov bl,[a1]; sub`, ours `mov al; xor ebx; mov bl;
//   mov [ebp+8],eax; sub`; r, g, b and the specular alpha are identical); (2) the numerator of the bottom plane t is `fld y; fadd z` in the exe, `fld z; fadd y` here.
//   Tried: channel order permutations (rgbas is the best), macro instead of inline ClipExtra (669 bytes), int locals / byte-through-color forms of the alpha line,
//   swapped sum order, dummy early references to x/y/z.
// STUB: D3DREN 0x100161e0
int d3d_ClipTLVertexLine(float *pVertsRaw, int nMask)
{
	TLVertex *pVerts = (TLVertex *)pVertsRaw;
	TLVertex *pOut;
	float t, z;

	if (nMask & 1)
	{
		CLIPLINE_PLANE(CLIPTEST_NEAR, DOCLIP_NEAR)
	}
	if (nMask & 4)
	{
		CLIPLINE_PLANE(CLIPTEST_LEFT, DOCLIP_LEFT)
	}
	if (nMask & 8)
	{
		CLIPLINE_PLANE(CLIPTEST_TOP, DOCLIP_TOP)
	}
	if (nMask & 0x10)
	{
		CLIPLINE_PLANE(CLIPTEST_RIGHT, DOCLIP_RIGHT)
	}
	if (nMask & 0x20)
	{
		CLIPLINE_PLANE(CLIPTEST_BOTTOM, DOCLIP_BOTTOM)
	}
	if (nMask & 2)
	{
		CLIPLINE_PLANE(CLIPTEST_FAR, DOCLIP_FAR)
	}
	return 1;
}

// guess: draws a line between two world space points; the exe takes the two LTVector by value (6 floats on the stack).
// NAME: d3d_DrawLine: Jupiter d3d_draw.cpp d3d_DrawLine (names_proposal.csv high); Talon's takes LTVector by value and two colours.
// The second projection keeps z in a local so VC6 emits the target's x87 operand order for the y sum.
// FUNCTION: D3DREN 0x10017150
void d3d_DrawLine(LTVector vSrc, LTVector vDest, uint32 color1, uint32 color2)
{
	D3DTLVERTEX verts[2];	// a plain POD vertex: TLVertex has an LTVector member, whose constructor would make the array call ??_H

	verts[0].color = color1;
	verts[0].specular = 0xffffffff;
	MatVMul_H((LTVector *)&verts[0].sx, &g_ViewParams.m_mClipTransform, &vSrc);
	MatVMul_H((LTVector *)&verts[1].sx, &g_ViewParams.m_mClipTransform, &vDest);
	verts[1].color = color2;
	verts[1].specular = 0xffffffff;

	if (d3d_ClipTLVertexLine(&verts[0].sx, 0x3f))
	{
		ProjectVertexToScreen_inline(&verts[0].sx, &g_ViewParams);
		{
			const ViewParams *pView = &g_ViewParams;
			float *pVert = &verts[1].sx;

			pVert[3] = 1.0f / pVert[2];
			pVert[0] = (pView->m_fProjXScale * pVert[0] + pView->m_fProjXOffset * pVert[2]) * pVert[3];
			float z = pVert[2];
			pVert[1] = (pView->m_fProjYScale * pVert[1] + pView->m_fProjYOffset * z) * pVert[3];
			pVert[2] = (pView->m_fProjZScale * pVert[2] + pView->m_fProjZOffset) * pVert[3];
		}
		g_pD3DDevice->DrawPrimitive(D3DPT_LINELIST, 0x1c4, verts, 2, 0);
	}
}

// Matching source form for the last edge's inline expansion.  The target keeps the matrix projection and screen projection
// calls out of line in this expansion; this scoped compiler control preserves that call depth.
#pragma inline_depth(0)
inline void DrawWireframeBoxEdgeInline(LTVector vSrc, LTVector vDest, uint32 color1, uint32 color2)
{
	D3DTLVERTEX verts[2];

	MatVMul_H((LTVector *)&verts[0].sx, &g_ViewParams.m_mClipTransform, &vSrc);
	verts[0].color = color1;
	verts[0].specular = 0xffffffff;
	MatVMul_H((LTVector *)&verts[1].sx, &g_ViewParams.m_mClipTransform, &vDest);
	verts[1].color = color2;
	verts[1].specular = 0xffffffff;
	if (d3d_ClipTLVertexLine(&verts[0].sx, 0x3f))
	{
		ProjectVertexToScreen(&verts[0].sx, &g_ViewParams);
		ProjectVertexToScreen(&verts[1].sx, &g_ViewParams);
		g_pD3DDevice->DrawPrimitive(D3DPT_LINELIST, 0x1c4, verts, 2, 0);
	}
}
#pragma inline_depth()

// NAME: d3d_DrawWireframeBox: Jupiter d3d_draw.cpp (same 12 edges in the same order).
// FUNCTION: D3DREN 0x100173e0
void d3d_DrawWireframeBox(const LTVector &Min, const LTVector &Max, uint32 color)
{
	// Bottom square.
	d3d_DrawLine(LTVector(Min.x, Min.y, Min.z), LTVector(Max.x, Min.y, Min.z), color, color);
	d3d_DrawLine(LTVector(Min.x, Min.y, Min.z), LTVector(Min.x, Min.y, Max.z), color, color);
	d3d_DrawLine(LTVector(Max.x, Min.y, Min.z), LTVector(Max.x, Min.y, Max.z), color, color);
	d3d_DrawLine(LTVector(Min.x, Min.y, Max.z), LTVector(Max.x, Min.y, Max.z), color, color);

	// Top square.
	d3d_DrawLine(LTVector(Min.x, Max.y, Min.z), LTVector(Max.x, Max.y, Min.z), color, color);
	d3d_DrawLine(LTVector(Min.x, Max.y, Min.z), LTVector(Min.x, Max.y, Max.z), color, color);
	d3d_DrawLine(LTVector(Max.x, Max.y, Min.z), LTVector(Max.x, Max.y, Max.z), color, color);
	d3d_DrawLine(LTVector(Min.x, Max.y, Max.z), LTVector(Max.x, Max.y, Max.z), color, color);

	// Connect squares together.
	d3d_DrawLine(LTVector(Min.x, Min.y, Min.z), LTVector(Min.x, Max.y, Min.z), color, color);
	d3d_DrawLine(LTVector(Max.x, Min.y, Min.z), LTVector(Max.x, Max.y, Min.z), color, color);
	d3d_DrawLine(LTVector(Max.x, Min.y, Max.z), LTVector(Max.x, Max.y, Max.z), color, color);
	DrawWireframeBoxEdgeInline(LTVector(Min.x, Min.y, Max.z), LTVector(Min.x, Max.y, Max.z), color, color);
}

// Recurses and renders the world tree nodes down to depth iMaxDepth.
// NAME: d3d_DrawWorldTree_R: Jupiter d3d_draw.cpp (Talon version: depth limit instead of bDrawEmptyNodes).
// FUNCTION: D3DREN 0x10017930
void d3d_DrawWorldTree_R(WorldTreeNode *pNode, uint32 iDepth, uint32 iMaxDepth)
{
	uint32 i;

	if (iDepth <= iMaxDepth)
	{
		d3d_DrawWireframeBox(pNode->m_BBoxMin, pNode->m_BBoxMax, 0xffffffff);
		if (pNode->HasChildren())
		{
			for (i = 0; i < MAX_WTNODE_CHILDREN; i++)
				d3d_DrawWorldTree_R(pNode->m_Children[i], iDepth + 1, iMaxDepth);
		}
	}
}

// SDK matrix copies emitted by the real mirror pass.
// FUNCTION: D3DREN 0x10016160 ?SetTranslation@LTMatrix@@QAEXMMM@Z
// FUNCTION: D3DREN 0x10016180 ??DLTMatrix@@QAE?AV0@AAV0@@Z


// guess: the mirror pass hook (stored in g_pfnDrawVisibleReflections by d3d_RenderScene for the duration of d3d_FullDrawScene): runs the mirror pass for the
// current scene through the mirror pass above.
// FUNCTION: D3DREN 0x100161c0
void d3d_DrawVisibleReflections()
{
	d3d_DrawReflectedWorldScenes(g_pSceneDesc, d3d_GetVisibleSet());
}

// ---- (merged from the scratch unit w2scratch/scene) ----
// Prototypes of functions of other parts / units (identical to the declarations the other units use).
void d3d_DrawWireframeBox(const LTVector &Min, const LTVector &Max, uint32 color);	// 0x100173e0 (scratch unit drawB)

// guess: draws the bounding boxes of the terrain sections of every world model (DrawTerrainSections console variable): z test on, no
// texture, each box in the colour given by the sum of its section and BSP addresses.
// The original unbinds the normal stage through d3d_DisableTexture, caching the stage across SetTexture.
// Its debug colour is the sum of the section and BSP addresses (add esi,edi at 0x10017a35).
// FUNCTION: D3DREN 0x10017980
void d3d_DrawTerrainSectionBounds(MainWorld *pWorld)
{
	struct { D3DRENDERSTATETYPE m_Type; DWORD m_Val; } saved;
	uint32 i;

	saved.m_Type = D3DRENDERSTATE_ZENABLE;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZENABLE, &saved.m_Val);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, 1);
	d3d_DisableTexture(g_NormalTextureStage);

	for (i = 0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		WorldBsp *pBsp = pWorld->m_WorldModels[i]->m_pOriginalBsp;

		if (!pBsp->IsUntransformed())
		{
			uint32 j;

			for (j = 0; j < pBsp->m_TerrainSections.GetSize(); j++)
			{
				TerrainSection *pSection = &pBsp->m_TerrainSections[j];
				WorldTreeNode *pNode = pWorld->m_WorldTree.FindNode(&pSection->m_NodePath);

				if (pNode)
					d3d_DrawWireframeBox(pNode->m_BBoxMin, pNode->m_BBoxMax, (uint32)pSection + (uint32)pBsp);
			}
		}
	}
	g_pD3DDevice->SetRenderState(saved.m_Type, saved.m_Val);
}

// NAME: d3d_DrawWorldTree: Jupiter d3d_draw.cpp (renders the WorldTree nodes to the given depth; z test on, no texture).  Expanded into
// d3d_RenderScene by the exe, so the recursion's first level is inline and its own recursive call is not.
static void d3d_DrawWorldTree(WorldTree *pTree, uint32 nMaxDepth)
{
	StateSet ssZEnable(D3DRENDERSTATE_ZENABLE, TRUE);

	d3d_DisableTexture(g_NormalTextureStage);
	d3d_DrawWorldTree_R(pTree->GetRootNode(), 0, nMaxDepth);
}

// ---- d3d_RenderScene ------------------------------------------------------------------------------------------------------------------
// GLOBAL: D3DREN 0x1005a378
int g_nLastRenderToFront;				// guess: the RenderToFront value the surfaces were last swapped for (0 after the module init)
// GLOBAL: D3DREN 0x10058474
int g_bLightFalloffTableInitialized;				// guess: the light falloff table was built (cleared at module init)
// GLOBAL: D3DREN 0x10057c94
float g_fLightFalloffTableSaturation;				// guess: the scale the falloff table was built for
// GLOBAL: D3DREN 0x10052f9c
float g_fEnvPanUOffset;				// guess: environment map pan offset u (EnvPanSpeed * camera x + 0.5)
// GLOBAL: D3DREN 0x10052fa0
float g_fEnvPanVOffset;				// guess: same, v (camera z)
// GLOBAL: D3DREN 0x10052fa4
float g_fEnvMapUScale;				// guess: environment map scale u (stored twice)
// GLOBAL: D3DREN 0x10052fa8
float g_fEnvMapVScale;				// guess: environment map scale v
// GLOBAL: D3DREN 0x100587f0
LTVector g_vNegatedGlobalLightDirection;			// guess: the negated global light direction (g_pStruct->m_GlobalLightDir)
// GLOBAL: D3DREN 0x100578a0
float g_fFogDepthRange;				// guess: FogFarZ - FogNearZ
// GLOBAL: D3DREN 0x10057a58
float g_fWarbleTableScale;				// guess: the warble value the warble table was built for

int g_nLastColorTableVertexTint;
uint8 g_MultipassVertexTintTableR[256];	// guess: red lighting table (multipass / dynamic light pass); the next two are green and blue
uint8 g_MultipassVertexTintTableG[256];
uint8 g_MultipassVertexTintTableB[256];
uint8 g_VertexTintTableR[256];	// guess: red lighting table
uint8 g_VertexTintTableG[256];
uint8 g_VertexTintTableB[256];
// GLOBAL: D3DREN 0x1006cd70
extern void (*g_pfnDrawVisibleReflections)();	// guess: optional callback run after the solid objects (RenderScene sets it)
void __fastcall d3d_NullPreFrameCallback(LTVector *pPos, uint32 *pSpecular);	// 0x1002cc80: the table fog hook
int CanDrawPortals();
void d3d_DrawVisibleReflections();
LTBOOL d3d_InitFrame(SceneDesc *pDesc, TLVertex *pScratchVerts, int nUnk);
void InitLightFalloffScaleTable(float fScale);

// guess: one channel of the lightmap colour tables: table[i] = min(i * fScale, 255) with 16.16 fixed point accumulation
// (the exe expands this nine times).
static inline void BuildColorTable(uint8 *pTable, float fStep)
{
	uint32 nAcc = 0;
	uint32 nStep = (uint32)(int)fStep;
	int i;

	for (i = 0; i < 256; i++)
	{
		*pTable++ = (uint8)(nAcc >> 16);
		nAcc += nStep;
		if (nAcc > 0xff0000)
			nAcc = 0xff0000;
	}
}

// NAME: d3d_RenderScene: RenderStruct::RenderScene (include/renderstruct.h, set by RenderDLLSetup); the d3d_ prefix is Jupiter's (names_proposal medium).
// guess: the frame: swaps the surfaces when RenderToFront changed, sets up the environment/fog/light tables, d3d_InitFrame, d3d_FullDrawScene
// with the mirror hook installed, the statistics consoles, the world tree wireframe and the warble phase.
// The world tree block is Jupiter's d3d_DrawWorldTree (StateSet, d3d_DisableTexture, the root through d3d_DrawWorldTree_R) expanded
// here, the light scale compares are the SDK's LTDIFF, the portal flag is one `&&` expression, and the ShowTexInfo strings carry the
// exe's column padding.
// FUNCTION: D3DREN 0x10017aa0
int d3d_RenderScene(SceneDesc *pDesc)
{
	Counter cCounter;
	uint8 aScratch[0x5000];

	if (!pDesc || !g_pBackBuffer || !g_pD3DDevice || !g_pDD || !g_bIn3D)
		return 0;

	// Can't render cameras while in optimized 2d.
	if (g_bInOptimized2D)
	{
		AddDebugMessage(0, "Error: tried to render 3D while in optimized 2D mode.");
		return 0;
	}

	if (g_CV_RenderToFront.m_IntVal != g_nLastRenderToFront)
	{
		IDirectDrawSurface7 *pOld = g_pOffscreen;

		g_pOffscreen = g_pBackBuffer;
		g_pBackBuffer = pOld;
		g_pOffscreen->AddAttachedSurface(g_pZBuffer);
		g_pBackBuffer->DeleteAttachedSurface(0, g_pZBuffer);
		g_pD3DDevice->SetRenderTarget(g_pOffscreen, 0);
		g_nLastRenderToFront = g_CV_RenderToFront.m_IntVal;
	}

	g_bPortalsEnabled = g_CV_DrawPortals.m_IntVal && CanDrawPortals();

	if (!g_bLightFalloffTableInitialized || g_LightSaturate != g_fLightFalloffTableSaturation)
	{
		InitLightFalloffScaleTable(g_LightSaturate);
		g_fLightFalloffTableSaturation = g_LightSaturate;
		g_bLightFalloffTableInitialized = 1;
	}

	g_fEnvPanUOffset = g_EnvPanSpeed * pDesc->m_Pos.x + 0.5f;
	g_fEnvPanVOffset = g_EnvPanSpeed * pDesc->m_Pos.z + 0.5f;
	g_nUnclippedModelsDrawn = 0;
	g_nClippedModelsDrawn = 0;
	g_fEnvMapUScale = (1.0f / g_EnvScale) * 0.5f;
	g_fEnvMapVScale = g_fEnvMapUScale;
	g_vNegatedGlobalLightDirection = -g_pStruct->m_GlobalLightDir;

	if (d3d_InitFrame(pDesc, (TLVertex *)aScratch, 0x5000))
	{
		g_pfnCalcSkyFogAlpha = d3d_CalcSkyFogAlpha;
		g_fSkyFogAlphaScale = (1.0f / (g_SkyFogFarZ - g_SkyFogNearZ)) * 255.0f;
		g_fFogDepthRange = g_FogFarZ - g_FogNearZ;
		g_fFogAlphaScale = (1.0f / g_fFogDepthRange) * 255.0f;

		if (g_CV_TableFog.m_IntVal)
			g_pfnCalcFogAlpha = d3d_NullPreFrameCallback;
		else if (g_CV_VFog.m_IntVal)
		{
			g_fVFogValueRange = g_CV_VFogMaxYVal.m_FloatVal - g_CV_VFogMinYVal.m_FloatVal;
			g_pfnCalcFogAlpha = d3d_CalcVerticalFogAlpha;
			g_fInvVFogHeightRange = 1.0f / (g_CV_VFogMaxY.m_FloatVal - g_CV_VFogMinY.m_FloatVal);
			g_fVFogDensityScale = 255.0f / g_CV_VFogDensity.m_FloatVal;
		}
		else
			g_pfnCalcFogAlpha = d3d_CalcDistanceFogAlpha;

		if (LTDIFF(g_GlobalLightScale.x, g_vLastColorTableLightScale.x) > 0.001f ||
			LTDIFF(g_GlobalLightScale.y, g_vLastColorTableLightScale.y) > 0.001f ||
			LTDIFF(g_GlobalLightScale.z, g_vLastColorTableLightScale.z) > 0.001f ||
			*(uint32 *)&g_GlobalVertexTintColor != (uint32)g_nLastColorTableVertexTint)
		{
			if (g_Saturate)
			{
				BuildColorTable(g_MultipassVertexTintTableR, (g_GlobalVertexTint.x + g_GlobalVertexTint.x) * 65536.0f);
				BuildColorTable(g_MultipassVertexTintTableG, (g_GlobalVertexTint.y + g_GlobalVertexTint.y) * 65536.0f);
				BuildColorTable(g_MultipassVertexTintTableB, (g_GlobalVertexTint.z + g_GlobalVertexTint.z) * 65536.0f);
			}
			else
			{
				BuildColorTable(g_MultipassVertexTintTableR, g_GlobalVertexTint.x * 65536.0f);
				BuildColorTable(g_MultipassVertexTintTableG, g_GlobalVertexTint.y * 65536.0f);
				BuildColorTable(g_MultipassVertexTintTableB, g_GlobalVertexTint.z * 65536.0f);
			}
			BuildColorTable(g_VertexTintTableR, g_GlobalVertexTint.x * 65536.0f);
			BuildColorTable(g_VertexTintTableG, g_GlobalVertexTint.y * 65536.0f);
			BuildColorTable(g_VertexTintTableB, g_GlobalVertexTint.z * 65536.0f);
			g_vLastColorTableLightScale.x = g_GlobalLightScale.x;
			g_vLastColorTableLightScale.y = g_GlobalLightScale.y;
			g_vLastColorTableLightScale.z = g_GlobalLightScale.z;
			g_nLastColorTableVertexTint = *(uint32 *)&g_GlobalVertexTintColor;
		}

		g_pTexturedWorldPolyBuckets = 0;
		g_pMultipassWorldPolyBuckets = 0;
		g_pFlatWorldPolyQueue = 0;
		g_nQueuedWorldPolyVertices = 0;
		g_pStruct->m_Unk4c = 0;
		g_pStruct->m_Unk48 = 0;

		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FILLMODE, g_Wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
		if (!g_CV_ShowPortalBounds.m_IntVal)
		{
			g_pfnDrawVisibleReflections = d3d_DrawVisibleReflections;
			d3d_FullDrawScene(pDesc);
			g_pfnDrawVisibleReflections = 0;
		}
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);

		if (g_ShowTextureCounts)
		{
			AddDebugMessage(0, "Texture changes: %d", g_nTextureChanges);
			AddDebugMessage(0, "Texture uploads: %d", g_nTextureUploads);
		}

		if (g_ShowPolyCounts)
		{
			g_pStruct->ConsolePrint("World Polies Processed: %d", g_nWorldPolysProcessed);
			g_pStruct->ConsolePrint("World Polies Drawn: %d", g_nWorldPolysDrawn);
			g_pStruct->ConsolePrint("Num Light Tests: %d", g_nLightTests);
			g_pStruct->ConsolePrint("Num Lit Polies: %d (textured and uploaded: %d)", g_nLitPolies, g_nDynamicLightmapsRefreshed);
			g_pStruct->ConsolePrint("Model triangles drawn: %d", g_nModelTrianglesDrawn);
			g_pStruct->ConsolePrint("Particles drawn: %d", g_nParticlesDrawn);
			g_pStruct->ConsolePrint("Num Clip Tests: %d", g_nPlaneClipTests);
			g_pStruct->ConsolePrint("Visible Leaves: %d", g_nVisibleLeaves);
			g_pStruct->ConsolePrint("Sky Portals: %d", g_nSkyPortals);
			g_pStruct->ConsolePrint("Sky Polies (fragments): %d", g_nSkyPolyFragments);
			g_pStruct->ConsolePrint("Portals: %d", d3d_GetVisibleSet()->m_nUnk140);
			g_pStruct->ConsolePrint("Visible lights: %d, rejected: %d", g_nNumObjectDynamicLights, g_nRejectedPolyLightTests);
			g_pStruct->ConsolePrint("Texture upload saves: %d", g_nTextureUploadSaves);
			g_pStruct->ConsolePrint("Triangles: %d", g_nPolygonTriangles);
		}

		if (g_ShowFillInfo)
		{
			g_pStruct->ConsolePrint("Tri area drawn: %.3f", g_fScreenTriangleArea);
			g_pStruct->ConsolePrint("Overdraw: %.3f", g_fScreenTriangleArea / (float)((g_ViewParams.m_Rect.right - g_ViewParams.m_Rect.left) * (g_ViewParams.m_Rect.bottom - g_ViewParams.m_Rect.top)));
		}

		if (g_CV_ShowTexInfo.m_IntVal)
		{
			D3DDEVINFO_TEXTUREMANAGER info;

			g_pD3DDevice->GetInfo(D3DDEVINFOID_TEXTUREMANAGER, &info, sizeof(info));
			dsi_ConsolePrint("ShowTexInfo ----------------------------");
			dsi_ConsolePrint("bThrashing        - %d", info.bThrashing);
			dsi_ConsolePrint("dwNumEvicts       - %d", info.dwNumEvicts);
			dsi_ConsolePrint("dwNumVidCreates   - %d", info.dwNumVidCreates);
			dsi_ConsolePrint("dwNumTexturesUsed - %d", info.dwNumTexturesUsed);
			dsi_ConsolePrint("dwNumUsedTexInVid - %d", info.dwNumUsedTexInVid);
			dsi_ConsolePrint("dwWorkingSet      - %d", info.dwWorkingSet);
			dsi_ConsolePrint("dwWorkingSetBytes - %d", info.dwWorkingSetBytes);
			dsi_ConsolePrint("dwTotalManaged    - %d", info.dwTotalManaged);
			dsi_ConsolePrint("dwTotalBytes      - %d", info.dwTotalBytes);
			dsi_ConsolePrint("dwLastPri         - %d", info.dwLastPri);
		}
	}

	// Draw the world tree?
	if ((int)g_CV_DrawWorldTree.m_IntVal > -1 && g_pFrameMainWorld)
		d3d_DrawWorldTree(&g_pFrameMainWorld->m_WorldTree, g_CV_DrawWorldTree.m_IntVal);

	if (g_CV_DrawTerrainSections.m_IntVal && g_pFrameMainWorld)
		d3d_DrawTerrainSectionBounds(g_pFrameMainWorld);

	d3d_NullCallback();

	if (!g_bWarbleTableInitialized || g_WarbleScale != g_fWarbleTableScale)
	{
		AddDebugMessage(9, "Rebuilding warble table");
		d3d_BuildModelWarbleTables();
		g_bWarbleTableInitialized = 1;
		g_fWarbleTableScale = g_WarbleScale;
	}

	g_fModelWarblePhase = g_WarbleSpeed * g_pSceneDesc->m_FrameTime + g_fModelWarblePhase;
	g_fModelWarbleFraction = g_fModelWarblePhase - (float)floor(g_fModelWarblePhase);

	if (g_ModelProfile)
		g_pStruct->ConsolePrint("ModelProfile: %d clipped, %d unclipped", g_nClippedModelsDrawn, g_nUnclippedModelsDrawn);

	g_nRenderScenesSinceClear++;
	return 0;
}

// ---- (merged from the scratch unit w2scratch/stl) ----
// Callees of other units (names with provenance: struct_bank.h sb_Init/sb_Init2/sb_Term from the StdLith library objects at 0x1003b520...).
void d3d_InitLitPolyPools();	// unit sys/d3d/common_stuff: inits the three struct banks at 0x10056220/0x10056240/0x10057778
void d3d_TermLitPolyPools();	// ... and tears them down

StructBank g_PolyDrawBlockBank;		// guess: the 0x100-byte-element pool
uint32 g_PolyDrawPoolResetValue;

// guess: initialises the pools the poly drawing code allocates its queue nodes and buckets from.
// FUNCTION: D3DREN 0x100184f0
void d3d_InitPolyDrawPools()
{
	d3d_InitLitPolyPools();
	g_PolyDrawPoolResetValue = 0;
	sb_Init(&g_PolyDrawBlockBank, 0x100, 0x10);
	sb_Init(&g_WorldPolyBucketBank, 0xc, 100);
	sb_Init2(&g_WorldPolyNodeBank, 0x18, 0x80, 0x300);
	g_pQueuedWorldPolyVertices = 0;
	g_nLastColorTableVertexTint = 0;
}

// guess: tears the pools down again.
// FUNCTION: D3DREN 0x10018550
void d3d_TermPolyDrawPools()
{
	d3d_TermLitPolyPools();
	sb_Term(&g_PolyDrawBlockBank);
	sb_Term(&g_WorldPolyBucketBank);
	sb_Term(&g_WorldPolyNodeBank);
	if (g_pQueuedWorldPolyVertices)
	{
		dfree(g_pQueuedWorldPolyVertices);
		g_pQueuedWorldPolyVertices = 0;
	}
}

// Look-up tables (lightmap.h, package W9): g_ByteSaturatingAddTable saturating add (index = a + b, 0..0x1ff), g_ByteMultiplyTable multiplication (index = a * 0x100 + b).

// guess: adds the per-vertex colours of the light animation frames (blended by m_PercentBetween) of one poly to the vertex colours of
// pPolyData (0x18-byte poly vertices, colour bytes at +0x14/+0x15/+0x16); returns 0 when the poly index is outside the animation.
// The vertex data is an SPolyVertex array (m_Color, indexed per vertex), the blended term is
// Mul[frame1 * 0x100 + percent] + Mul[frame0 * 0x100 + (uint8)(0xff - percent)] written in place (a named uint8 inv costs the exe's
// register assignment), and the clamped count is its own local (the exe stores it into the dead nPolyData slot on both paths).
// FUNCTION: D3DREN 0x100185a0
int d3d_AddLightAnimVertexColors(void *pPolyData, uint32 nPolyData, LightAnim *pAnim, uint32 *pRef)
{
	LAPolyRef *pPolyRef = (LAPolyRef *)pRef;
	uint8 percent;
	LAPolyFrame *pFrame0, *pFrame1;
	SPolyVertex *pVerts = (SPolyVertex *)pPolyData;
	uint32 i;
	uint32 nVerts;

	if (pPolyRef->m_iPoly >= pAnim->m_nPolies)
		return 0;

	percent = (uint8)pAnim->m_PercentBetween;
	pFrame0 = pAnim->m_pFrames[pAnim->m_iFrames[0]] + pPolyRef->m_iPoly;
	pFrame1 = pAnim->m_pFrames[pAnim->m_iFrames[1]] + pPolyRef->m_iPoly;
	if (percent == 0)
		pFrame1 = pFrame0;
	else if (percent == 0xff)
		pFrame0 = pFrame1;

	nVerts = LTMIN(nPolyData, LTMIN(pFrame0->m_nVerts, pFrame1->m_nVerts));

	if (pFrame0 == pFrame1)
	{
		for (i = 0; i < nVerts; i++)
		{
			pVerts[i].m_Color[2] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[2] + pFrame0->m_pVertR[i]];
			pVerts[i].m_Color[1] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[1] + pFrame0->m_pVertG[i]];
			pVerts[i].m_Color[0] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[0] + pFrame0->m_pVertB[i]];
		}
	}
	else
	{
		for (i = 0; i < nVerts; i++)
		{
			pVerts[i].m_Color[2] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[2] + g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[pFrame1->m_pVertR[i] * 0x100 + percent] + g_ByteMultiplyTable.m_Unk00[pFrame0->m_pVertR[i] * 0x100 + (uint8)(0xff - percent)]]];
			pVerts[i].m_Color[1] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[1] + g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[pFrame1->m_pVertG[i] * 0x100 + percent] + g_ByteMultiplyTable.m_Unk00[pFrame0->m_pVertG[i] * 0x100 + (uint8)(0xff - percent)]]];
			pVerts[i].m_Color[0] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[0] + g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[pFrame1->m_pVertB[i] * 0x100 + percent] + g_ByteMultiplyTable.m_Unk00[pFrame0->m_pVertB[i] * 0x100 + (uint8)(0xff - percent)]]];
		}
	}
	return 1;
}

// SDK Dot copy emitted by the mirror pass's plane transformation.
// FUNCTION: D3DREN 0x100187c0 ?Dot@?$_CVector@M@@QBEMV1@@Z

// ---- STLport template copies (std::vector<RenderState> / vector<TextureState> and the node allocator of STLport, the version in
// lithshared\stl).  Most copies come out of the real users above (StateChange::Add / StateChange(RenderState)); the destructors still
// require the explicitly marked stand-in below.  Names: names_proposal.csv rows with source `h:stl_*`
// (provenance exe-decomp), the mangled names are the compiler's.
// FUNCTION: D3DREN 0x100187e0 ??0?$vector@URenderState@@V?$allocator@URenderState@@@_STL@@@_STL@@QAE@IABURenderState@@ABV?$allocator@URenderState@@@1@@Z
// FUNCTION: D3DREN 0x100188e0 ??1?$vector@URenderState@@V?$allocator@URenderState@@@_STL@@@_STL@@QAE@XZ
// FUNCTION: D3DREN 0x10018940 ?push_back@?$vector@URenderState@@V?$allocator@URenderState@@@_STL@@@_STL@@QAEXABURenderState@@@Z
// FUNCTION: D3DREN 0x10018a90 ??0?$_Vector_base@UTextureState@@V?$allocator@UTextureState@@@_STL@@@_STL@@QAE@ABV?$allocator@UTextureState@@@1@@Z
// FUNCTION: D3DREN 0x10018aa0 ??1?$vector@UTextureState@@V?$allocator@UTextureState@@@_STL@@@_STL@@QAE@XZ
// FUNCTION: D3DREN 0x10018b10 ??0?$_STL_alloc_proxy@PAURenderState@@U1@V?$allocator@URenderState@@@_STL@@@_STL@@QAE@ABV?$allocator@URenderState@@@1@PAURenderState@@@Z
// FUNCTION: D3DREN 0x10018b20 ?_M_do_lock@?$_STL_mutex_spin@$0A@@_STL@@SAXPCK@Z
// FUNCTION: D3DREN 0x10018c10 ?_M_insert_overflow@?$vector@URenderState@@V?$allocator@URenderState@@@_STL@@@_STL@@IAEXPAURenderState@@ABU3@I@Z
// FUNCTION: D3DREN 0x10018db0 ?_Construct@_STL@@YAXPAURenderState@@ABU2@@Z
// FUNCTION: D3DREN 0x10018dd0 ?allocate@?$__node_alloc@$00$0A@@_STL@@SAPAXI@Z
// FUNCTION: D3DREN 0x10018f80 ?deallocate@?$__node_alloc@$00$0A@@_STL@@SAXPAXI@Z
// FUNCTION: D3DREN 0x100190c0 ?_S_refill@?$__node_alloc@$00$0A@@_STL@@CAPAXI@Z
// FUNCTION: D3DREN 0x10019220 ?_S_chunk_alloc@?$__node_alloc@$00$0A@@_STL@@CAPADIAAH@Z

// The mirror pass constructs arrays of 12-byte vectors (0x1001582a passes this constructor to the array iterator).
// The real ViewParams construction above now emits this copy.
// FUNCTION: D3DREN 0x10016150 ??0?$_CVector@M@@QAE@XZ

// The current StateChange users expand both vector destructors.  Force the separately linked SDK/STLport copies
// without changing those callers; remove this stand-in once a recovered caller emits them naturally.
#pragma inline_depth(0)
// STANDIN: forces the out-of-line RenderState/TextureState vector destructors (not in d3d.ren).
void EmitStateVectorDestructors(std::vector<RenderState> *pRenderStates, std::vector<TextureState> *pTextureStates)
{
	pRenderStates->~vector();
	pTextureStates->~vector();
}
#pragma inline_depth()
