// d3d.ren sys/d3d/drawparticles_a (0x10029660-0x100298ef): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// boundary exact value 0x100298ef = next ConVar group.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// unit unk/10029660 (0x10029660-0x1002afd0): two objects of a size-optimised run (/O1 /Ob2, packed, no padding):
//   0x10029660-0x100298ee  drawparticles_A (Jupiter render_a/src/sys/d3d/drawparticles_A.cpp: d3d_ProcessParticles,
//                           d3d_TestAndDrawPS, d3d_QueueTranslucentParticles; the PSSrcBlend/PSDestBlend console variables)
//   0x100298ef-0x1002afcf  DrawPolyMgr (Talon only: state-block based multi-pass polygon drawing; no Jupiter source)
// NAME: the object boundary is from names_proposal.csv / NAMING.md (TU table); the static-initialiser numbers below are
// those of this unit's own compile (the originals restart in each of the two objects).
// FLAGS: /O1 /Ob2
// unit unk/10029660 (0x10029660-0x1002afd0): two objects of a size-optimised run (/O1 /Ob2, packed, no padding):
//   0x10029660-0x100298ee  drawparticles_A (Jupiter render_a/src/sys/d3d/drawparticles_A.cpp: d3d_ProcessParticles,
//                           d3d_TestAndDrawPS, d3d_QueueTranslucentParticles; the PSSrcBlend/PSDestBlend console variables)
//   0x100298ef-0x1002afcf  DrawPolyMgr (Talon only: state-block based multi-pass polygon drawing; no Jupiter source)
// NAME: the object boundary is from names_proposal.csv / NAMING.md (TU table); the static-initialiser numbers below are
// those of this unit's own compile (the originals restart in each of the two objects).
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dstate.h"
#include "d3dren/visibleset.h"
#include "d3dren/viewparams.h"
#include "d3dren/drawobjects.h"
#include "d3dren/drawpolymgr.h"
#include "d3dren/pool.h"
#include "d3dren/d3d_draw.h"
#include "d3dren/setupmodel.h"
#include "d3dren/polydraw.h"

// ---- drawparticles_A ---------------------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x10029660 _$E2
// FUNCTION: D3DREN 0x10029665 _$E1
// GLOBAL: D3DREN 0x1006d198
ConVar g_CV_PSSrcBlend("PSSrcBlend", -1.0f);
// FUNCTION: D3DREN 0x10029683 _$E5
// FUNCTION: D3DREN 0x10029688 _$E4
// GLOBAL: D3DREN 0x1006cd78
ConVar g_CV_PSDestBlend("PSDestBlend", -1.0f);

// guess: the particle systems' shared index buffer: the quad list 0,1,2, 0,2,3 / 4,5,6, ... for 128 quads (0x300 indices), filled once
// (the g_ObjectHandlers[OT_PARTICLESYSTEM] pre-frame function; the drawing code is in unit unk/10008cd0).
// GLOBAL: D3DREN 0x1006d1b8
extern uint16 g_ParticleQuadIndices[0x300];
// GLOBAL: D3DREN 0x1006e7b8
int g_bParticleQuadIndicesInitialized;

// FUNCTION: D3DREN 0x100296a6
void d3d_InitParticleQuadIndices()
{
	if (!g_bParticleQuadIndicesInitialized)
	{
		g_bParticleQuadIndicesInitialized = 1;
		uint16 *pIdx = g_ParticleQuadIndices;
		uint16 n = 0;
		for (int i = 0; i < 0x80; i++)
		{
			*pIdx++ = n++;
			*pIdx++ = n++;
			*pIdx++ = n++;
			*pIdx++ = n - 3;
			*pIdx++ = n - 1;
			*pIdx++ = n++;
		}
	}
}

// NAME: d3d_ProcessParticles: Jupiter drawparticles_A.cpp (names_proposal.csv, high)
// FUNCTION: D3DREN 0x100296f7
void d3d_ProcessParticles(LTObject *pObject)
{
	d3d_GetVisibleSet()->m_ParticleSystems.Add(pObject);
}

// ---- externals (other units) ----
// NAME: d3d_DrawParticleSystem: Jupiter drawparticles.cpp (names_proposal.csv; unit unk/10008cd0 has the d3d.ren copy)
void d3d_DrawParticleSystem(LTParticleSystem *pSystem);

// NAME: d3d_TestAndDrawPS: Jupiter drawparticles_A.cpp (names_proposal.csv: medium; the d3d.ren body adds the sphere-in-frustum
// test that Jupiter's lacks)
// FUNCTION: D3DREN 0x1002970e
void d3d_TestAndDrawPS(ViewParams *pParams, LTObject *pObj)
{
	float radius;
	LTParticleSystem *pSystem;
	uint32 srcBlend, destBlend, dwFog, dwFogColor;
	uint32 clipFlags;

	pSystem = (LTParticleSystem *)pObj;
	radius = pSystem->m_SystemRadius * LTMAX(pSystem->m_Scale.x, LTMAX(pSystem->m_Scale.y, pSystem->m_Scale.z));

	if (d3d_TestSphereClipPlanes(&pSystem->m_SystemCenter, radius, (pSystem->m_Flags & FLAG_REALLYCLOSE) ? g_ViewParams.m_ReallyCloseClipPlanes : g_ViewParams.m_ClipPlanes, &clipFlags))
	{
		pSystem->m_Flags |= FLAG_INTERNAL1;

		d3d_GetBlendStates(pSystem, srcBlend, destBlend, dwFog, dwFogColor);
		StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, srcBlend);
		StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, destBlend);
		StateSet ssFog(D3DRENDERSTATE_FOGENABLE, dwFog);
		StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, dwFogColor);

		d3d_DrawParticleSystem(pSystem);
	}
}

// guess: BaseObjectSet::Draw callback that queues the particle system for sorted drawing (g_ObjectHandlers' translucent pass)
void d3d_QueueParticleSystemDraw(ViewParams *pParams, LTObject *pObject);

// NAME: d3d_QueueTranslucentParticles: Jupiter drawparticles_A.cpp (names_proposal.csv, high; Talon takes no arguments)
// FUNCTION: D3DREN 0x100298b6
void d3d_QueueTranslucentParticles()
{
	if (g_DrawParticles)	// the DrawParticles console variable's mirror
	{
		VisibleSet *pVisibleSet = d3d_GetVisibleSet();
		pVisibleSet->m_ParticleSystems.Draw(&g_ViewParams, d3d_QueueParticleSystemDraw);
	}
}

// FUNCTION: D3DREN 0x100298da
void d3d_QueueParticleSystemDraw(ViewParams *pParams, LTObject *pObject)
{
	g_pTranslucentObjectDrawList->Add(pObject, d3d_TestAndDrawPS);
}
