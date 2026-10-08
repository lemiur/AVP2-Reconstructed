// d3d.ren sky drawing and the world polygon helpers of the same object (unit unk/10019350, Jupiter render_a/src/sys/d3d/d3d_drawsky.cpp
// and drawsky.cpp; owner: package W6).  Prototypes of what other units call.
//
// NAME: d3d_DrawSkyObjects: Jupiter d3d_drawsky.cpp (names_proposal.csv, high; the Talon one takes no arguments and draws with the global
// sky view parameters g_SkyParams).  d3d_DrawSky: Jupiter drawsky.cpp d3d_DrawSkyExtents (names_proposal.csv, medium; the Talon one has
// no arguments either).  Everything else keeps its Ghidra name; roles are in the `guess:` comments.
#ifndef __D3DREN_DRAWSKY_H__
#define __D3DREN_DRAWSKY_H__

#include "ltbasedefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "d3dren/viewparams.h"
#include "d3dren/tlvertex.h"

class DynamicLight;

// One entry of the list of dynamic lights that touch a world polygon (WorldPoly+0x30, linked through the first member): the light
// and its position as the light-test code copied it (0x14 bytes).
struct UnkType_PolyLightRef
{
	UnkType_PolyLightRef	*m_pNext;	// 0x00
	DynamicLight			*m_pLight;	// 0x04 (+0x94..0x96 colour, +0x1b0 radius)
	LTVector				m_Pos;		// 0x08
};

// The sky camera's view parameters (filled by d3d_DrawSky from the real ones and the SkyScale console variable).
// GLOBAL: D3DREN 0x100739b8
extern ViewParams g_SkyParams;

// guess: the surface flags of the first polygon of a world model's original BSP (0 when it has none): an inline function of the
// original (the draw units expand it, the /O1 sky object has an out-of-line copy at 0x10019880)
inline uint32 GetWorldModelFirstSurfaceFlags(LTObject *pObject)
{
	WorldModelInstance *pInstance = (WorldModelInstance *)pObject;
	if (pInstance->m_pOriginalBsp)
	{
		if (pInstance->WMSlot14() && pInstance->m_pOriginalBsp->m_nPolies > 0)
			return ((Surface *)pInstance->m_pOriginalBsp->m_Polies[0]->m_pSurface)->m_Flags;
	}
	return 0;
}

void d3d_DrawSkyObjects();		// 0x10019691: draws the sky objects (world models, poly grids, sprites) of the scene description
// guess: adds the dynamic lights that touch pPoly (its light list) to the colours of the 0x20-byte vertices pVerts (one per vertex of
// the poly); the third argument (the vertex count the callers pass) is not used.  Called by the world polygon draw functions of
// units unk/10021d70 and unk/10023860 as well.
void d3d_ApplyWorldPolyVertexLights(WorldPoly *pPoly, TLVertex *pVerts, int nVerts);
// d3d_DrawSky (Jupiter d3d_DrawSkyExtents): the whole sky pass (0x1002d4c0 in unit unk/1002d080).
void d3d_DrawSky();

#endif
