// d3d.ren sys/d3d/d3d_drawsky (0x10019350-0x10019b10): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// function-local static stub _$E7 follows its owner 0x10019351.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// unit unk/10019350 (0x10019350-0x10019b10): Jupiter's render_a/src/sys/d3d/d3d_drawsky.cpp (renderer-specific sky drawing):
// d3d_DrawSkyObjects with the world model polygon traversal it needs, a world poly draw with the sky colour, and the dynamic light
// accumulation.  A size-optimised object (/O1 /Ob2, packed COMDATs).  The object also holds the out-of-line copies of the
// destructor of the state saver class (UnkType_StateRestorer, d3ddevice.h) and of the STLport vector bases it contains.
// NAME: the unit's file name d3d_drawsky.cpp is from names_proposal.csv / NAMING.md (the TU table, Jupiter's file order).
// FLAGS: /O1 /Ob2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#define D3DREN_STATERESTORER_FULL	// d3ddevice.h: the real vector<RenderState>/vector<TextureState> members of UnkType_StateRestorer
#include <windows.h>
#include "ltbasedefs.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/polydraw.h"
#include "d3dren/pool.h"
#include "d3dren/drawsky.h"
#include <math.h>
#include "d3dren/tlvertex.h"
#include "de_objects.h"
#include "de_world.h"

// A class with an empty constructor: a file-scope object of it gets an empty static initialiser (the first function of the unit,
// 0x10019350, is a lone `ret`).  Same idea as setupmodel.h's UnkType_EmptyCtor of package W4.
struct UnkType_EmptyCtorSky
{
	UnkType_EmptyCtorSky() {}
	int m_Unk00;
};
// FUNCTION: D3DREN 0x10019350 _$E4
static UnkType_EmptyCtorSky s_Empty;


// globals of other units used below
// GLOBAL: D3DREN 0x10055ce8
extern LTVector g_GlobalVertexTint;		// guess: the global light colour (W4's seed declares it the same way): scales the sky object colour
// GLOBAL: D3DREN 0x100566cc
extern int g_nLightTests;		// guess: the number of light tests this frame ("Num Light Tests")
// RTM compiler/NAME: d3d_SetTranslucentObjectStates / d3d_UnsetTranslucentObjectStates: Jupiter d3d_draw.h (unit unk/100132a0, package W2)
void d3d_SetTranslucentObjectStates(int bAdditive);			// 0x10013ba0
void d3d_UnsetTranslucentObjectStates(int bChangeZ);		// 0x10013df0
// NAME: d3d_DrawPolyGrid: Jupiter drawpolygrid.cpp (unit sys/d3d/drawpolygrid, package W5); d3d_DrawSprite: unit unk/1002d080
void d3d_DrawPolyGrid(ViewParams *pParams, LTObject *pGrid);	// 0x1002aff0
void d3d_DrawSprite(ViewParams *pParams, LTObject *pObject);	// 0x1002d660

// ---- sky world model polygons -------------------------------------------------------------------------------------------------------

// GLOBAL: D3DREN 0x1005a3d0
float g_fSkyWorldModelTintR;		// guess: the sky object's colour (red, green, blue as 0..1 floats, scaled by the global light colour)
// GLOBAL: D3DREN 0x1005a3d4
float g_fSkyWorldModelTintG;
// GLOBAL: D3DREN 0x1005a3d8
float g_fSkyWorldModelTintB;
// GLOBAL: D3DREN 0x10057774
extern uint8 g_nPolyVertexAlpha;		// guess: the alpha byte of the vertex colours


// guess: the vertex buffer of d3d_DrawSkyWorldPoly: a function-local static of a class with a (do nothing) destructor
struct UnkType_SkyVerts
{
	~UnkType_SkyVerts() {}
	TLVertex	m_Verts[0x80];
};

// NAME: SetUV: this is the shape of the vertex fillers of unit unk/10001000 (their SetUV): arguments evaluated right to left stay on the x87 stack
static inline void SetUV(TLVertex *pVertex, float u, float v)
{
	pVertex->tu = u;
	pVertex->tv = v;
}

// guess: draws one polygon of a sky world model: the vertices get the sky colour, the dynamic lights and the sky fog, are
// clipped (mask 0x3f) and projected, the surface's texture is bound and the polygon is drawn as a triangle fan.
// FUNCTION: D3DREN 0x10019351
void d3d_DrawSkyWorldPoly(WorldPoly *pPoly)
{
	static UnkType_SkyVerts s_Verts;
	UnkType_PolyVertex *pSrc;
	TLVertex *pVerts;
	int nVerts;

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

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	TLVertex *pDest = s_Verts.m_Verts;
	for (uint32 i = nVerts; i > 0; i--)
	{
		pDest->m_Vec = *pSrc->m_Vec;
		pDest->rgb.r = (uint8)RoundFloatToInt((float)pSrc->m_Color[2] * g_fSkyWorldModelTintR);
		pDest->rgb.g = (uint8)RoundFloatToInt((float)pSrc->m_Color[1] * g_fSkyWorldModelTintG);
		pDest->rgb.b = (uint8)RoundFloatToInt((float)pSrc->m_Color[0] * g_fSkyWorldModelTintB);
		pDest->rgb.a = g_nPolyVertexAlpha;
		SetUV(pDest, pSrc->m_U, pSrc->m_V);
		pSrc++;
		pDest++;
	}

	pVerts = s_Verts.m_Verts;
	d3d_ApplyWorldPolyVertexLights(pPoly, pVerts, nVerts);

	pDest = pVerts;
	uint32 n = (uint32)nVerts;
	while (n--)
	{
		TransformPositionInPlace((float *)pDest, &g_SkyParams.m_mClipTransform.m[0][0]);
		pDest++;
	}

	if (ClipPoly(0x3f, &pVerts, &nVerts))
	{
		SharedTexture *pTexture;
		if (g_ShowSkySplits || !(pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture) || !d3d_SetTexture(pTexture, g_NormalTextureStage, 0))
			d3d_UnsetTexture(g_NormalTextureStage);

		pDest = pVerts;
		for (int n = nVerts; n != 0; n--)
		{
			g_pfnCalcSkyFogAlpha(&pDest->m_Vec, &pDest->specular);
			ProjectVertexToScreen((float *)pDest, &g_SkyParams);
			float fV = pDest->tv;
			fV *= g_TextureStageTexelSizes[0].m_Unk04;
			float fU = pDest->tu;
			fU *= g_TextureStageTexelSizes[0].m_Unk00;
			SetUV(pDest, fU, fV);
			pDest++;
		}

		g_TextureStateRestorer.RestoreAllStates();
		pTexture = ((Surface *)pPoly->m_pSurface)->m_pTexture;
		if (pTexture && pTexture->m_pStateChange)
			g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, 1);
		g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x1c4, pVerts, nVerts, 0);
		g_TextureStateRestorer.RestoreAllStates();
	}
}

// the atexit stub of the function-local static above (the class's destructor does nothing)
// FUNCTION: D3DREN 0x1001955d _$E7

// guess: the objects of the sky: a world model's polygons are drawn back to front through its BSP, with the sky colour of the object
// FUNCTION: D3DREN 0x1001955e
void d3d_DrawSkyWorldModel(LTObject *pObject)
{
	WorldModelInstance *pInstance = (WorldModelInstance *)pObject;
	Node *aStack[0x200];

	if (pInstance->WMSlot14())
	{
		g_fSkyWorldModelTintR = ((float)pObject->m_ColorR * (1.0f / 255.0f)) * g_GlobalVertexTint.x;
		g_fSkyWorldModelTintG = ((float)pObject->m_ColorG * (1.0f / 255.0f)) * g_GlobalVertexTint.y;
		g_fSkyWorldModelTintB = ((float)pObject->m_ColorB * (1.0f / 255.0f)) * g_GlobalVertexTint.z;

		WorldBsp *pBsp = pInstance->m_pOriginalBsp;
		if (pBsp->m_nPolies != 0)
		{
			Node *pNode = pBsp->m_RootNode;
			Node **ppStack = aStack;
			for (;;)
			{
				if (pNode->m_Flags & (NF_IN | NF_OUT))
				{
					if (ppStack == aStack)
						return;
					pNode = *--ppStack;
				}
				else
				{
					LTPlane *pPlane = pNode->GetPlane();
					int iSide = pPlane->DistTo(g_SkyParams.m_Pos) > 0.0f;
					if (iSide && !(((Surface *)pNode->m_pPoly->m_pSurface)->m_Flags & SURF_INVISIBLE))
						d3d_DrawSkyWorldPoly(pNode->m_pPoly);
					*ppStack++ = pNode->m_Sides[iSide];
					pNode = pNode->m_Sides[iSide == 0];
				}
			}
		}
	}
}

// FUNCTION: D3DREN 0x10019880 ?GetWorldModelFirstSurfaceFlags@@YAKPAVLTObject@@@Z
// (the out-of-line copy of the inline function GetWorldModelFirstSurfaceFlags of drawsky.h; the first caller, d3d_DrawSkyObjects, did not inline it)

// NAME: d3d_DrawSkyObjects: Jupiter d3d_drawsky.cpp (names_proposal.csv, high): the same structure with the Talon state saver instead
// of StateSet objects, no ViewParams argument and no BSP shared world model lookup.
// FUNCTION: D3DREN 0x10019691
void d3d_DrawSkyObjects()
{
	LTObject *pSkyObject;
	DWORD oldColorOp;
	uint32 nFlags;
	UnkType_StateRestorer saver;

	// disable reading/writing to the Z buffer, set the fog distances and switch the second texture stage off
	saver.ApplyRenderState(RenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE));
	DWORD oldFogEnable;
	saver.ApplyRenderState(RenderState(D3DRENDERSTATE_ZENABLE, FALSE));
	saver.ApplyRenderState(RenderState(D3DRENDERSTATE_FOGSTART, *(DWORD *)&g_SkyFogNearZ));
	saver.ApplyRenderState(RenderState(D3DRENDERSTATE_FOGEND, *(DWORD *)&g_SkyFogFarZ));
	saver.ApplyTextureStageState(TextureState(1, D3DTSS_COLOROP, D3DTOP_DISABLE), 1);

	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, &oldFogEnable);
	g_pD3DDevice->GetTextureStageState(1, D3DTSS_COLOROP, &oldColorOp);

	for (int i = 0; i < g_pSceneDesc->m_nSkyObjects; i++)
	{
		pSkyObject = g_pSceneDesc->m_SkyObjects[i];
		if (pSkyObject->m_Flags & FLAG_VISIBLE)
		{
			if (pSkyObject->m_ObjectType == OT_WORLDMODEL)
			{
				if (pSkyObject->m_Flags & FLAG_FOGDISABLE)
					saver.ApplyRenderState(RenderState(D3DRENDERSTATE_FOGENABLE, FALSE));

				nFlags = GetWorldModelFirstSurfaceFlags(pSkyObject);
				if (nFlags & 8)
				{
					d3d_SetTranslucentObjectStates((nFlags >> 19) & 1);
				}
				else
				{
					saver.ApplyRenderState(RenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA));
					saver.ApplyRenderState(RenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE));
					d3d_UnsetTranslucentObjectStates(0);
				}

				g_nPolyVertexAlpha = pSkyObject->m_ColorA;
				d3d_DrawSkyWorldModel(pSkyObject);
			}
			else if (pSkyObject->m_ObjectType == OT_POLYGRID)
			{
				if (!(pSkyObject->m_ColorA == 0xff))
					d3d_SetTranslucentObjectStates(0);
				else
					d3d_UnsetTranslucentObjectStates(0);
				d3d_DrawPolyGrid(&g_SkyParams, pSkyObject);
			}
			else if (!(pSkyObject->m_ObjectType != OT_SPRITE))
			{
				d3d_SetTranslucentObjectStates(0);
				d3d_DrawSprite(&g_SkyParams, pSkyObject);
			}
		}
	}

	d3d_UnsetTranslucentObjectStates(0);
}

// guess: the destructor of the state saver (inline member in d3ddevice.h: puts every changed state back, then the two vectors are destroyed);
// this object holds the exe's only out-of-line copy, 0x100198bc, which the destructor of the global saver 0x10063c90 calls.
// FUNCTION: D3DREN 0x100198bc ??1UnkType_StateRestorer@@QAE@XZ
// the destructors of its two vector members are the compiler's out-of-line copies (local to this object: the exe's COMDAT copy of the
// TextureState one is 0x10018aa0 in unit unk/100132a0)
// FUNCTION: D3DREN 0x100198e5 ??1?$_Vector_base@URenderState@@V?$allocator@URenderState@@@_STL@@@_STL@@QAE@XZ
// FUNCTION: D3DREN 0x10019900 ??1?$_Vector_base@UTextureState@@V?$allocator@UTextureState@@@_STL@@@_STL@@QAE@XZ

// guess: the colours of the vertices of pPoly get the dynamic lights that touch it added: each light of the polygon's light list
// (position and colour 0..255, range m_LightRadius) lights the vertices closer than its radius with a linear falloff; the sum
// is doubled when the Saturate variable is set and clamped to 0..255.  g_nLightTests counts the light tests ("Num Light Tests").
// Keep the positive-test count separate, then initialize the per-vertex countdown after pColor.
// FUNCTION: D3DREN 0x10019923
void d3d_ApplyWorldPolyVertexLights(WorldPoly *pPoly, TLVertex *pVerts, int nVerts)
{
	for (UnkType_PolyLightRef *pRef = (UnkType_PolyLightRef *)WORLDPOLY_UNK30(pPoly); pRef; pRef = pRef->m_pNext)
	{
		DynamicLight *pLight = pRef->m_pLight;
		LTVector vLightPos = pRef->m_Pos;
		float fLightR = (float)pLight->m_ColorR;
		float fLightG = (float)pLight->m_ColorG;
		float fLightB = (float)pLight->m_ColorB;
		fLightR = fLightR - (255.0f - fLightR);
		fLightG = fLightG - (255.0f - fLightG);
		fLightB = fLightB - (255.0f - fLightB);
		g_nLightTests++;

		float fDist = pPoly->m_pPlane->DistTo(vLightPos);
		if (fDist < 0.0f)
			fDist = -fDist;
		if (fDist < pLight->m_LightRadius)
		{
			float fRadiusSqr = pLight->m_LightRadius * pLight->m_LightRadius;
			float fInvRadius = 1.0f / pLight->m_LightRadius;
			uint32 nCount = pPoly->m_nVertices;
			if (nCount > 0)
			{
				uint8 *pColor = (uint8 *)pVerts + 0x12;
				uint32 nPoly = nCount;
				do
				{
					LTVector vDelta;
					vDelta.x = *(float *)(pColor - 0x12) - vLightPos.x;
					vDelta.y = *(float *)(pColor - 0xe) - vLightPos.y;
					vDelta.z = *(float *)(pColor - 0xa) - vLightPos.z;
					float fDistSqr = vDelta.MagSqr();
					if (fDistSqr < fRadiusSqr)
					{
						float fAtten = 1.0f - (float)sqrt(fDistSqr) * fInvRadius;
						int r = (int)(fAtten * fLightR) + pColor[0];
						int g = (int)(fAtten * fLightG) + pColor[-1];
						int b = (int)(fAtten * fLightB) + pColor[-2];
						if (g_Saturate)
						{
							r *= 2;
							g *= 2;
							b *= 2;
						}
						if (r > 255)
							r = 255;
						else if (r < 0)
							r = 0;
						if (g > 255)
							g = 255;
						else if (g < 0)
							g = 0;
						if (b > 255)
							b = 255;
						else if (b < 0)
							b = 0;
						pColor[-1] = (uint8)g;
						pColor[0] = (uint8)r;
						pColor[-2] = (uint8)b;
					}
					pColor += 0x20;
				} while (--nPoly != 0);
			}
		}
	}
}
