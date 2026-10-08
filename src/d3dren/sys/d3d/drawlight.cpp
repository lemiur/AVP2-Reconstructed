// d3d.ren sys/d3d/drawlight (0x10023860-0x10023ce0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// boundary 0x10023ce0 by function pattern only (no data tie).
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/10023860 (0x10023860-0x100241e0): the light and line system object handlers (Jupiter drawlight.cpp and
// drawlinesystem.cpp in Talon form; the file names are the Jupiter ones, the unit keeps its address name).
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
#include "d3dren/fixedpoint.h"		// RoundFloatToInt

// NAME: d3d_ProcessLight: Jupiter drawlight.cpp d3d_ProcessLight (g_ObjectHandlers[OT_LIGHT].m_ProcessObjectFn; Ghidra name).
// Talon's version queues the light into the VS_LIGHTS set (Jupiter's version fills the g_ObjectDynamicLights arrays instead).
// FUNCTION: D3DREN 0x10023860
void d3d_ProcessLight(LTObject *pObject)
{
	if (g_DynamicLight)
	{
		d3d_GetVisibleSet()->m_Lights.Add(pObject);
	}
}

// The per-poly record of a dynamic light touching it (StructBank g_PolyLightBank, 0x14 bytes) and the list of lit polys (StructBank
// g_LitPolyBank, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h: read through a macro).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;		// 0x00
	LTObject			*m_pLight;		// 0x04
	LTVector			m_Pos;			// 0x08 the light position in the world model's space
};

struct UnkType_LitPoly
{
	WorldPoly			*m_pPoly;		// 0x00
	UnkType_LitPoly		*m_pNext;		// 0x04
};

#define WORLDPOLY_LIGHTS(p)	(*(UnkType_PolyLight**)((uint8*)(p) + 0x30))
#define WORLDPOLY_FRAMECODE(p)	(*(uint16*)((uint8*)(p) + 0x46))

// GLOBAL: D3DREN 0x10056220
extern StructBank g_PolyLightBank;		// guess: UnkType_PolyLight records
// GLOBAL: D3DREN 0x10056240
extern StructBank g_LitPolyBank;		// guess: UnkType_LitPoly records
// GLOBAL: D3DREN 0x100577b4
extern UnkType_LitPoly *g_pDynamicallyLitPolys;	// guess: head of the list of polys touched by a dynamic light this frame
// GLOBAL: D3DREN 0x10056690
extern int g_nRejectedPolyLightTests;		// guess: g_nRejectedLights (names_proposal low): "Visible Leaves: %d" neighbour in the scene stats



// Adds the dynamic light pLight to the polys of the world model pWorldModel that it touches (BSP walk with an explicit stack).
// FUNCTION: D3DREN 0x100238b0
void AttachDynamicLightToWorldModelPolys(LTObject *pLight, WorldModelInstance *pWorldModel)
{
	Node *stack[1024];
	Node **pSP;
	Node *pNode;
	WorldPoly *pPoly;
	LTPlane *pPlane;
	LTVector vLight;
	float fDist;

	vLight = pWorldModel->m_BackTransform * pLight->m_Pos;

	pSP = stack;
	pNode = pWorldModel->m_pOriginalBsp->GetRootNode();
	for (;;)
	{
		while (pNode->m_Flags & (NF_IN | NF_OUT))
		{
			if (pSP == stack)
				return;

			pNode = *--pSP;
		}

		pPoly = pNode->m_pPoly;
		pPlane = pNode->GetPlane();
		fDist = pPlane->DistTo(vLight);
		if (fDist < -((DynamicLight*)pLight)->m_LightRadius)
		{
			pNode = pNode->m_Sides[0];
			continue;
		}

		if (fDist > ((DynamicLight*)pLight)->m_LightRadius)
		{
			pNode = pNode->m_Sides[1];
			continue;
		}

		if (WORLDPOLY_FRAMECODE(pPoly) == g_CurFrameCode &&
			(!(pLight->m_Flags & FLAG_DONTLIGHTBACKFACING) || fDist >= 0.01f))
		{
			float fRad = pPoly->m_Radius + ((DynamicLight*)pLight)->m_LightRadius;
			LTVector vDelta = vLight - pPoly->m_Center;
			if (vDelta.MagSqr() > fRad * fRad)
				goto Rejected;
			{
				UnkType_PolyLight *pRec;

				if (!WORLDPOLY_LIGHTS(pPoly))
				{
					UnkType_LitPoly *pLit = (UnkType_LitPoly*)sb_Allocate(&g_LitPolyBank);
					if (pLit)
					{
						pLit->m_pPoly = pPoly;
						pLit->m_pNext = g_pDynamicallyLitPolys;
						g_pDynamicallyLitPolys = pLit;
					}
				}
				else
				{
					for (pRec = WORLDPOLY_LIGHTS(pPoly); pRec; pRec = pRec->m_pNext)
					{
						if (pRec->m_pLight == pLight)
							goto Skip;
					}
				}

				pRec = (UnkType_PolyLight*)sb_Allocate(&g_PolyLightBank);
				if (pRec)
				{
					pRec->m_pLight = pLight;
					pRec->m_Pos = vLight;
					pRec->m_pNext = WORLDPOLY_LIGHTS(pPoly);
					WORLDPOLY_LIGHTS(pPoly) = pRec;
				}
			}
		}
		else
		{
Rejected:
			g_nRejectedPolyLightTests++;
		}
Skip:
		*pSP++ = pNode->m_Sides[1];
		pNode = pNode->m_Sides[0];
	}
}

// WTObjCallback of ApplyVisibleDynamicLight's box query: world models in the light's box light their polys.
// FUNCTION: D3DREN 0x10023b00
void DynamicLightWorldModelQueryCB(WorldTreeObj *pObj, void *pUser)
{
	if (((LTObject*)pObj)->m_ObjectType == OT_WORLDMODEL)
	{
		AttachDynamicLightToWorldModelPolys((LTObject*)pUser, (WorldModelInstance*)pObj);
	}
}
// GLOBAL: D3DREN 0x10056218
extern uint32 g_nNumObjectDynamicLights;		// guess: g_nNumObjectDynamicLights (names_proposal low)
// GLOBAL: D3DREN 0x100566d0
extern DynamicLight *g_ObjectDynamicLights[];	// guess: g_ObjectDynamicLights (names_proposal low)
// GLOBAL: D3DREN 0x10056770
extern MainWorld *g_pFrameMainWorld;	// guess: g_pMainWorld (names_proposal low)



inline void QueryLightBox(MainWorld *pWorld, FindObjInfo *pInfo)
{
	pWorld->m_WorldTree.FindObjectsInBox2(pInfo);
}

// Draw callback of the VS_LIGHTS set: remembers the light for the models (the lights of the leaf) and lights the world models
// in its box.
// The radius vector temporaries emit the shared LTVector::Init COMDAT at its original address, 0x100098b0.
// FUNCTION: D3DREN 0x100098b0 ?Init@?$_CVector@M@@QAEXMMM@Z
// FUNCTION: D3DREN 0x10023b20
void ApplyVisibleDynamicLight(ViewParams *pParams, LTObject *pObject)
{
	FindObjInfo info;
	CountAdder cntAdd(g_pSceneDesc->m_pTicks_Render_PolyGrids);

	if (!(pObject->m_Flags & FLAG_ONLYLIGHTWORLD))
	{
		if (g_nNumObjectDynamicLights < 0x28)
		{
			g_ObjectDynamicLights[g_nNumObjectDynamicLights] = (DynamicLight*)pObject;
			g_nNumObjectDynamicLights++;
		}
	}

	if (pObject->m_Flags & FLAG_ONLYLIGHTOBJECTS)
		return;
	if (!g_pFrameMainWorld)
		return;

	DynamicLight *pLight = (DynamicLight*)pObject;
	info.m_Min = pObject->m_Pos - LTVector(pLight->m_LightRadius, pLight->m_LightRadius, pLight->m_LightRadius);
	info.m_Max = pObject->m_Pos + LTVector(pLight->m_LightRadius, pLight->m_LightRadius, pLight->m_LightRadius);
	info.m_CB = DynamicLightWorldModelQueryCB;
	info.m_pCBUser = pObject;
	QueryLightBox(g_pFrameMainWorld, &info);
}

// FUNCTION: D3DREN 0x10023cb0
void ApplyVisibleDynamicLights()
{
	if (g_DynamicLight)
	{
		d3d_GetVisibleSet()->m_Lights.Draw((ViewParams*)&g_ViewParams, ApplyVisibleDynamicLight);
	}
}

