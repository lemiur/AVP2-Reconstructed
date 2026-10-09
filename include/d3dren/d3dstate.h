// d3d.ren Direct3D 7 texture binding and render-state wrappers (owner: unit unk/100098d0, work package W4).
// Used by every drawing unit.  The device pointer itself comes from d3ddevice.h (W3).
//
// NAME: StageStateSet: Jupiter render_a/src/sys/d3d/d3d_draw.h (class StageStateSet: m_Stage, m_State, m_OldVal; the
// constructor Gets the old value, Sets the new one, the destructor restores it).
// NAME: d3d_DisableTexture (0x1000a27b, medium): Jupiter d3d_DisableTexture(stage): SetTexture(stage, NULL) if bound; defined in d3d_texture.h.
// Everything else keeps its Ghidra name (the roles are in the guess comments).
#ifndef __D3DREN_D3DSTATE_H__
#define __D3DREN_D3DSTATE_H__

#include "d3dren/d3ddevice.h"	// the DirectDraw/Direct3D 7 globals (g_pD3DDevice = IDirect3DDevice7 *, ...) and the DX headers
#include "d3dren/common_draw.h"	// the frame codes and per-frame statistics (owner: sys/d3d/common_draw)

// GLOBAL: D3DREN 0x100528d8
extern int g_bChromaKeyPass;	// guess: set by d3d_SetChromaKeyPass (flag read by the world polygon draw code)

// The texture binding code binds RTextures (d3dtexture.h) and lightmap pages (lightmap.h), both RTextureBase; a world poly
// holds its lightmap page at 0x48 (WORLDPOLY_LMPAGE, lightmap.h).  Only declared here: including lightmap.h in every
// drawing unit changes the inline budget of some (unit unk/10007930).
class RTextureBase;
struct LightmapPage;
struct WorldPoly;

// GLOBAL: D3DREN 0x100617d8
extern RTextureBase *g_pBoundTextures[8];	// the texture (RTexture or lightmap page) currently bound on each device stage

// Binds pPoly's lightmap page on device stage nStage unless it is already there; returns 0 when the poly has no page.
int d3d_SetLightmapTexture(WorldPoly *pPoly, int nStage);

// d3d_DisableTexture (d3d_texture.h): unbinds the texture of device stage nStage.  d3d_UnsetTexture is the exe's out-of-line copy of it, which
// d3d_FullDrawScene calls (unit unk/100098d0 defines it as a wrapper of the inline).
void d3d_UnsetTexture(int nStage);

// Second (detail) texture stage helpers.
void d3d_SetEnvMapTextureStates(int nMode);	// guess: set up stage 1 for the detail pass (1 = modulate/add-signed, 2 = modulate alpha + add colour)
void d3d_UnsetEnvMapTextureStates(void);		// guess: disable stage 1 colour and alpha
void d3d_SetDetailTextureStates(void);		// guess: stage 1 colour op = add-signed / modulate (DetailTextureAdd)
void d3d_UnsetDetailTexture(void);		// guess: disable stage 1 colour op and unbind its texture

void d3d_SetChromaKeyPass(int nValue);	// guess: setter of g_bChromaKeyPass
int d3d_GetChromaKeyPass(void);			// guess: getter of g_bChromaKeyPass

// Sets a render state and restores the old value in the destructor (inline in the exe: no symbols).
// NAME: StateSet: Jupiter render_a/src/sys/d3d/d3d_draw.h class StateSet (m_State, m_OldVal; the constructor Gets the old value
// and Sets the new one, the destructor Sets the old one back); the d3d.ren code (d3d_TestAndDrawPS 0x1002970e) is that exactly.
// Added by package W6 (drawparticles_A/drawsprite units use it).
class StateSet
{
public:
	StateSet(D3DRENDERSTATETYPE state, uint32 val)
	{
		m_State = state;
		g_pD3DDevice->GetRenderState(state, (unsigned long *)&m_OldVal);
		g_pD3DDevice->SetRenderState(state, val);
	}

	~StateSet()
	{
		g_pD3DDevice->SetRenderState(m_State, m_OldVal);
	}

	D3DRENDERSTATETYPE	m_State;
	uint32				m_OldVal;
};

// Sets a texture stage state and restores the old value in the destructor.
class StageStateSet
{
public:
	StageStateSet(uint32 stage, D3DTEXTURESTAGESTATETYPE state, uint32 val);
	~StageStateSet();

	uint32						m_Stage;
	D3DTEXTURESTAGESTATETYPE	m_State;
	uint32						m_OldVal;
};

#endif
