// d3d.ren sys/d3d/drawlinesystem (0x10023ce0-0x100241e0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// FLAGS: /O2 /Ob2
// d3d.ren unit unk/10023860 (0x10023860-0x100241e0): the light and line system object handlers (Jupiter drawlight.cpp and
// drawlinesystem.cpp in Talon form; the file names are the Jupiter ones, the unit keeps its address name).
#include <windows.h>
#include "ltbasedefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "world_tree.h"
#include "counter.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/d3dstate.h"		// g_pD3DDevice (the device), g_pBoundTextures (the textures bound per stage)
#include "d3dren/tlvertex.h"
#include "d3dren/pool.h"			// ProjectVertexToScreen (projects a TL vertex)
#include "d3dren/3d_ops.h"
#include "d3dren/fixedpoint.h"		// RoundFloatToInt

// The per-poly record of a dynamic light touching it (StructBank g_PolyLightBank, 0x14 bytes) and the list of lit polys (StructBank
// g_LitPolyBank, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h: read through a macro).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;		// 0x00
	LTObject			*m_pLight;		// 0x04
	LTVector			m_Pos;			// 0x08 the light position in the world model's space
};

#define WORLDPOLY_LIGHTS(p)	(*(UnkType_PolyLight**)((uint8*)(p) + 0x30))
#define WORLDPOLY_FRAMECODE(p)	(*(uint16*)((uint8*)(p) + 0x46))

// NAME: d3d_ProcessLineSystem: Jupiter drawlinesystem.cpp (Ghidra name); Talon has no DrawLineSystems check here (the console
// variable gates d3d_QueueLineSystems).
// FUNCTION: D3DREN 0x10023ce0
void d3d_ProcessLineSystem(LTObject *pObject)
{
	d3d_GetVisibleSet()->m_LineSystems.Add(pObject);
}

// Draw callback of the line system set (queues the system for the sorted translucent pass).
void d3d_QueueLineSystemDraw(ViewParams *pParams, LTObject *pObject);

// NAME: d3d_QueueLineSystems: Jupiter drawlinesystem.cpp (Ghidra name): the translucent line system queueing hook.
// FUNCTION: D3DREN 0x10023d30
void d3d_QueueLineSystems()
{
	if (g_DrawLineSystems)
	{
		AllocSet *pSet = &d3d_GetVisibleSet()->m_LineSystems;
		if (pSet->m_nObjects)
		{
			pSet->Draw((ViewParams*)&g_ViewParams, d3d_QueueLineSystemDraw);
		}
	}
}

void d3d_DrawLineSystem(ViewParams *pParams, LTObject *pObject);

// FUNCTION: D3DREN 0x10023d60
void d3d_QueueLineSystemDraw(ViewParams *pParams, LTObject *pObject)
{
	g_pTranslucentObjectDrawList->Add(pObject, d3d_DrawLineSystem);
}


// GLOBAL: D3DREN 0x1005849c
extern int g_FogEnable;		// guess: g_CV_FogEnable mirror (names_proposal medium)

// 0x100161e0 (unit unk/100132a0): clips the 3D line pVerts[2] against the planes of the mask (0x3f = all); 0 when nothing is left.
int d3d_ClipTLVertexLine(float *pVerts, int nMask);

// NAME: d3d_DrawLineSystem: Jupiter drawlinesystem.cpp (names_proposal medium; the Talon body transforms and clips the two
// points itself, then DrawPrimitive's them as a pre-transformed line list).
// FUNCTION: D3DREN 0x10023d80
void d3d_DrawLineSystem(ViewParams *pParams, LTObject *pObject)
{
	float rhw0;
	float fAlphaScale;
	LSLine *pLine;
	LTMatrix mObject;
	LTMatrix mFinal;

	LineSystem *pSystem = (LineSystem*)pObject;

	if (g_FogEnable)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
	}

	d3d_DisableTexture(g_NormalTextureStage);

	d3d_SetupTransformation(&pObject->GetPos(), (float*)&pObject->m_Rotation, &pObject->m_Scale, &mObject);
	MatMul(&mFinal, &g_ViewParams.m_mClipTransform, &mObject);

	fAlphaScale = (float)pSystem->m_ColorA;

	pLine = pSystem->m_LineHead.m_pNext;
	if (pLine != &pSystem->m_LineHead)
	{
		do
		{
			TLVertex Verts[2];

			rhw0 = MatVMul_H(&Verts[0].m_Vec, &mFinal, &pLine->m_Points[0].m_Pos);
			Verts[0].rhw = rhw0;
			Verts[0].rgb.r = (uint8)RoundFloatToInt(pLine->m_Points[0].r * 255.0f);
			Verts[0].rgb.g = (uint8)RoundFloatToInt(pLine->m_Points[0].g * 255.0f);
			Verts[0].rgb.b = (uint8)RoundFloatToInt(pLine->m_Points[0].b * 255.0f);
			Verts[0].rgb.a = (uint8)RoundFloatToInt(fAlphaScale * pLine->m_Points[0].a);

			Verts[1].rhw = MatVMul_H(&Verts[1].m_Vec, &mFinal, &pLine->m_Points[1].m_Pos);
			Verts[1].rgb.r = (uint8)RoundFloatToInt(pLine->m_Points[1].r * 255.0f);
			Verts[1].rgb.g = (uint8)RoundFloatToInt(pLine->m_Points[1].g * 255.0f);
			Verts[1].rgb.b = (uint8)RoundFloatToInt(pLine->m_Points[1].b * 255.0f);
			Verts[1].rgb.a = (uint8)RoundFloatToInt(fAlphaScale * pLine->m_Points[1].a);

			if (d3d_ClipTLVertexLine((float*)Verts, 0x3f))
			{
				ProjectVertexToScreen((float*)&Verts[0], &g_ViewParams);
				ProjectVertexToScreen((float*)&Verts[1], &g_ViewParams);
				g_pD3DDevice->DrawPrimitive(D3DPT_LINELIST, 0x1c4, Verts, 2, 0);
			}

			pLine = pLine->m_pNext;
		} while (pLine != &pSystem->m_LineHead);
	}

	if (g_FogEnable)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
	}
}
