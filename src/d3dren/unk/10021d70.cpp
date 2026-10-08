// d3d.ren unk/10021d70 (0x10021d70-0x100220f1): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// D3D state apply/restore (StateRestorer). Link order bracket: after d3d_texture, before dirtyrect (e.g. d3dstate).
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unk/10021d70 (0x10021d70-0x10023860): several small objects in one size-optimised run (/O1 /Ob1, packed COMDATs; see the FLAGS note):
//  1. the StateChange applier (UnkType_StateRestorer, d3ddevice.h) with the STLport vector<TextureState> / vector<RenderState> members
//     it needs (SDK D3DStateChange.h),
//  2. dirtyrect.cpp (Jupiter render_a/src/sys/d3d/dirtyrect.cpp),
//  3. draw_canvas.cpp (Jupiter draw_canvas.cpp: CanvasDrawMgr, an ILTCustomDraw implementation),
//  4. the world polygon draw passes (multipass gouraud / dynamic light), no Jupiter equivalent.
// Static initialiser `_$E` numbers in this file are those of the compiler for the whole file, not of the original objects.
// FLAGS NOTE: the unit default is /O1 /Ob2, which inlines ConvertCanvasBlendToD3DBlend into SetState etc. (the exe keeps them
// out of line: /Ob1 shape).  The four static initialiser wrappers `_$E6 _$E9 _$E12 _$E18` of the exe are the merged /Ob2 form; with /Ob1 the compiler
// emits `call _$E3; jmp _$E5` style wrappers (see the STUB notes at the globals).
// FLAGS: /O1 /Ob2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#define D3DREN_STATERESTORER_FULL	// d3ddevice.h: the real vector<RenderState>/vector<TextureState> members of UnkType_StateRestorer
#include <windows.h>
#include "ltbasedefs.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"

// ---- 1. the StateChange applier ---------------------------------------------------------------------------------------------
// NAME: StateChange, RenderState, TextureState and their members: Talon SDK D3DStateChange.h.  The class that applies them is
// UnkType_StateRestorer (invented name, d3ddevice.h): it applies a StateChange and remembers the old values.

// FUNCTION: D3DREN 0x10021d86 ??0UnkType_StateRestorer@@QAE@XZ
// The wrapper `_$E6` below is the merged /Ob2 form (`mov ecx, g; call ctor; push stub; call atexit; pop ecx; ret`), which is the normal shape of a
// P object (objects_v2.csv section 4), so the object is built `/O1 /Ob2`.  The two state-apply helpers are kept out of line with narrowly scoped
// compiler control so ApplyStateChange retains the exe's calls while the rest of the object keeps its /Ob2 behavior.
// FUNCTION: D3DREN 0x10021d70 _$E6
// FUNCTION: D3DREN 0x10021d9c _$E4
UnkType_StateRestorer g_TextureStateRestorer;

// guess: restore every saved state of both lists, then forget them.
// FUNCTION: D3DREN 0x10021da6 ?RestoreAllStates@UnkType_StateRestorer@@QAEXXZ
void UnkType_StateRestorer::RestoreAllStates()
{
	RestoreRenderStates();
	RestoreTextureStageStates();
}

// guess: applies pChange to the device on texture stage nStage: every render state, then every texture stage state, each one
// remembering the value it replaces.
// FUNCTION: D3DREN 0x10021db7
void UnkType_StateRestorer::ApplyStateChange(StateChange *pChange, uint32 nStage)
{
	std::vector<RenderState>::iterator itRender;
	std::vector<RenderState>::iterator itRenderEnd = pChange->m_RenderList.end();
	for (itRender = pChange->m_RenderList.begin(); itRender != itRenderEnd; ++itRender)
		ApplyRenderState(*itRender);

	std::vector<TextureState>::iterator itTexture;
	std::vector<TextureState>::iterator itTextureEnd = pChange->m_TextureList.end();
	for (itTexture = pChange->m_TextureList.begin(); itTexture != itTextureEnd; ++itTexture)
		ApplyTextureStageState(*itTexture, nStage);
}

// FUNCTION: D3DREN 0x10021dfd
#pragma auto_inline(off)
void UnkType_StateRestorer::ApplyTextureStageState(const TextureState &state, uint32 nStage)
{
	SaveTextureStageState(state, nStage);
	g_pD3DDevice->SetTextureStageState(nStage, state.m_TextureStateType, state.m_TextureState);
}
#pragma auto_inline(on)

// FUNCTION: D3DREN 0x10021e28
#pragma auto_inline(off)
void UnkType_StateRestorer::ApplyRenderState(const RenderState &state)
{
	SaveRenderState(state);
	g_pD3DDevice->SetRenderState(state.m_RenderStateType, state.m_RenderState);
}
#pragma auto_inline(on)

// FUNCTION: D3DREN 0x10021e47
void UnkType_StateRestorer::SaveRenderState(const RenderState &state)
{
	DWORD dwOld;
	g_pD3DDevice->GetRenderState(state.m_RenderStateType, &dwOld);
	m_Unk00.push_back(RenderState(state.m_RenderStateType, dwOld));
}

// FUNCTION: D3DREN 0x10021e80
void UnkType_StateRestorer::SaveTextureStageState(const TextureState &state, uint32 nStage)
{
	DWORD dwOld;
	g_pD3DDevice->GetTextureStageState(nStage, state.m_TextureStateType, &dwOld);
	m_Unk0c.push_back(TextureState(nStage, state.m_TextureStateType, dwOld));
}

// FUNCTION: D3DREN 0x10021ec9
void UnkType_StateRestorer::RestoreRenderStates()
{
	if (g_pD3DDevice)
	{
		std::vector<RenderState>::iterator it;
		std::vector<RenderState>::iterator itEnd = m_Unk00.end();
		for (it = m_Unk00.begin(); it != itEnd; ++it)
			g_pD3DDevice->SetRenderState(it->m_RenderStateType, it->m_RenderState);
		m_Unk00.clear();
	}
}

// FUNCTION: D3DREN 0x10021f09
void UnkType_StateRestorer::RestoreTextureStageStates()
{
	if (g_pD3DDevice)
	{
		std::vector<TextureState>::iterator it;
		std::vector<TextureState>::iterator itEnd = m_Unk0c.end();
		for (it = m_Unk0c.begin(); it != itEnd; ++it)
			g_pD3DDevice->SetTextureStageState(it->m_Stage, it->m_TextureStateType, it->m_TextureState);
		m_Unk0c.clear();
	}
}

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

// The dynamic lights touching a poly: a list at WorldPoly+0x30 (the engine pads it): { next, the light, the light position }
// (the same record unit unk/10007930 and common_stuff.cpp use under this name).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;
	DynamicLight		*m_pLight;
	LTVector			m_Pos;
};
#define POLY_LIGHTS(p)	(*(UnkType_PolyLight **)((uint8 *)(p) + 0x30))

// The STLport template copies this object emitted for the vectors above (annotated by mangled name; the compiler generates them:
// push_back / erase / the growth path of vector<TextureState>, erase of vector<RenderState>; vector<RenderState>::push_back and
// its growth path live in the object of unit unk/100132a0, 0x10018940).
// FUNCTION: D3DREN 0x10021f51 ?push_back@?$vector@UTextureState@@V?$allocator@UTextureState@@@_STL@@@_STL@@QAEXABUTextureState@@@Z
// FUNCTION: D3DREN 0x10021f7d ?erase@?$vector@URenderState@@V?$allocator@URenderState@@@_STL@@@_STL@@QAEPAURenderState@@PAU3@0@Z
// FUNCTION: D3DREN 0x10021fb3 ?erase@?$vector@UTextureState@@V?$allocator@UTextureState@@@_STL@@@_STL@@QAEPAUTextureState@@PAU3@0@Z
// FUNCTION: D3DREN 0x10021fec ?_M_insert_overflow@?$vector@UTextureState@@V?$allocator@UTextureState@@@_STL@@@_STL@@IAEXPAUTextureState@@ABU3@I@Z
