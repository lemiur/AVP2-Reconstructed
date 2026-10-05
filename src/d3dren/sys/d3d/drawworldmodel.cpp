// d3d.ren sys/d3d/drawworldmodel (0x1002f330-0x10030bb0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// FLAGS: /O2 /Ob2
// unit unk/1002d080 (0x1002d080-0x10030bb0): three speed objects (16-byte aligned COMDATs, /O2 /Ob2), LOW unit boundary confidence in
// units.csv, but names_proposal.csv / NAMING.md give them as the TU table does, in link order:
//   0x1002d080-0x1002d63f  drawsky     (Jupiter render_a/src/sys/d3d/drawsky.cpp: d3d_DrawSkyExtents; AllSkyPortals console variable)
//   0x1002d640-0x1002f32f  drawsprite  (Jupiter drawsprite.cpp)
//   0x1002f330-0x10030baf  drawworldmodel (Jupiter drawworldmodel.cpp; DrawWorldModels console variable)
// NAME: the three object names are from names_proposal.csv (source_file column) / NAMING.md section 4; the static-initialiser numbers
// below are those of this unit's own compile (the originals restart in each object).
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/drawsky.h"
#include "d3dren/scenedesc.h"
#include "counter.h"
#include "sprite.h"
#include "d3dren/lightmap.h"
#include "d3dren/pool.h"
#include "d3dren/fixedpoint.h"
#include "de_mainworld.h"
#include "d3dren/polydraw.h"
#include "d3dren/tlvertex.h"
#include "ltmatrix.h"

// A class with an empty constructor: a file-scope object of it gets an empty static initialiser (a lone `ret`, as at the start of
// each of the three objects below).  Same idea as setupmodel.h's UnkType_EmptyCtor of package W4.
struct UnkType_EmptyCtorW6
{
	UnkType_EmptyCtorW6() {}
	int m_Unk00;
};

// ---- drawworldmodel ------------------------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1002f330 _$E2
// GLOBAL: D3DREN 0x100752b0
ConVar g_CV_DrawWorldModels("DrawWorldModels", 1.0f);
// FUNCTION: D3DREN 0x1002f350 _$E5
static UnkType_EmptyCtorW6 s_Empty4;

void d3d_DrawSolidWorldModel(ViewParams *pParams, LTObject *pObject);

// NAME: d3d_ProcessWorldModel: Jupiter drawworldmodel.cpp (names_proposal.csv, high): g_ObjectHandlers[OT_WORLDMODEL / OT_CONTAINER]
// .m_ProcessObjectFn; translucent world models (surface flag 8 of the first polygon) go to their own set, chroma keyed ones to
// the CHROMAKEY set, the rest to the solid set
// FUNCTION: D3DREN 0x1002f360
void d3d_ProcessWorldModel(LTObject *pObject)
{
	uint32 nFlags = FUN_10019880(pObject);
	if (nFlags & 8)
		d3d_GetVisibleSet()->m_TranslucentWorldModels.Add(pObject);
	else if (pObject->m_Flags2 & FLAG2_CHROMAKEY)
		d3d_GetVisibleSet()->m_Unk224.Add(pObject);
	else
		d3d_GetVisibleSet()->m_SolidWorldModels.Add(pObject);
}

// NAME: d3d_DrawSolidWorldModels: Jupiter drawworldmodel.cpp (names_proposal.csv, medium; no arguments in Talon)
// FUNCTION: D3DREN 0x1002fa50
void FUN_1002fa50()
{
	if (g_CV_DrawWorldModels.m_IntVal)
	{
		BaseObjectSet *pSet = &d3d_GetVisibleSet()->m_SolidWorldModels;
		if (pSet->m_nObjects != 0)
			pSet->Draw(&g_ViewParams, d3d_DrawSolidWorldModel);
	}
}

// guess: the same for the chroma keyed world models
// FUNCTION: D3DREN 0x1002fed0
void FUN_1002fed0()
{
	if (g_CV_DrawWorldModels.m_IntVal)
	{
		BaseObjectSet *pSet = &d3d_GetVisibleSet()->m_Unk224;
		if (pSet->m_nObjects != 0)
			pSet->Draw(&g_ViewParams, d3d_DrawSolidWorldModel);
	}
}

// NAME: d3d_QueueTranslucentWorldModels: Jupiter drawworldmodel.cpp (names_proposal.csv, medium; no arguments in Talon): the translucent
// world models are queued in the sorted list (callback d3d_DrawTranslucentWorldModel), those with an additive first polygon
// (surface flag 1<<19) are collected in a local object set and queued after the others (callback FUN_10030ad0)
void FUN_10030070(ViewParams *pParams, LTObject *pObject);
void FUN_10030ad0(ViewParams *pParams, LTObject *pObject);

// guess: an object set whose array is a local array of the function (the constructor sets the array and its size)
struct UnkType_LocalObjectSet : public BaseObjectSet
{
	UnkType_LocalObjectSet() {}
	UnkType_LocalObjectSet(LTObject **pArray, uint32 nMax) { m_nMaxObjects = nMax; m_pObjects = pArray; }
	void Init(LTObject **pArray, uint32 nMax) { m_pObjects = pArray; m_nMaxObjects = nMax; }
};

// (byte-identical to the exe, 368 bytes; build.py check prints RELOC only because the empty string literal "" of the local
// BaseObjectSet constructor is the shared one at 0x10062888, which lies in the uninitialised part of .data where the checker has no bytes
// to compare: the same status as BaseObjectSet::BaseObjectSet and VisibleSet::VisibleSet)
// FUNCTION: D3DREN 0x1002ff00
void FUN_1002ff00()
{
	LTObject *aAdditive[0x400];
	UnkType_LocalObjectSet cAdditive(aAdditive, 0x400);
	if (g_CV_DrawWorldModels.m_IntVal)
	{
		VisibleSet *pVisibleSet = d3d_GetVisibleSet();
		BaseObjectSet *pSet = &pVisibleSet->m_TranslucentWorldModels;
		cAdditive.ClearSet();
		for (uint32 i = 0; i < pSet->m_nObjects; i++)
		{
			LTObject *pObject = pSet->m_pObjects[i];
			if (pObject->m_Flags & FLAG_VISIBLE)
			{
				if (g_ViewParams.m_bPortalView && (pObject->m_Flags2 & FLAG2_PORTALINVISIBLE))
					continue;
			}
			else if (g_ViewParams.m_bPortalView && !(pObject->m_Flags & 0x400))
				continue;

			if (FUN_10019880(pObject) & 0x80000)
				cAdditive.Add(pObject);
			else
				DAT_1006b934->Add(pObject, FUN_10030070);
		}
		if (cAdditive.m_nObjects > 0)
		{
			for (uint32 j = 0; j < cAdditive.m_nObjects; j++)
				DAT_1006b934->Add(aAdditive[j], FUN_10030ad0);
		}
	}
}

// guess: additive world model: blend factors ONE/ONE and fog colour black around the translucent world model draw
// FUNCTION: D3DREN 0x10030ad0
void FUN_10030ad0(ViewParams *pParams, LTObject *pObject)
{
	StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
	StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
	StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, 0);

	FUN_10030070(pParams, pObject);
}

// ---- drawworldmodel: world polygon drawing ------------------------------------------------------------------------------------------

// the polygon's surface (WorldPoly::m_pSurface is a void * in de_objects.h)
#define POLY_SURFACE(p)		((Surface *)(p)->m_pSurface)
// guess: the poly's frame tag (the tagging code sets it to the frame code of the frame the poly was seen in)
#define WORLDPOLY_FRAMECODE(p)	(*(uint16 *)((uint8 *)(p) + 0x46))

// guess: the poly draw callbacks, selected per frame by FUN_100144b0 (unit unk/100132a0): no texture, lightmapped, panning sky, plain
typedef void (*UnkType_PolyDrawFn)(WorldPoly *pPoly);
// GLOBAL: D3DREN 0x10058cd8
extern UnkType_PolyDrawFn DAT_10058cd8;
// GLOBAL: D3DREN 0x10058c24
extern UnkType_PolyDrawFn DAT_10058c24;
// GLOBAL: D3DREN 0x100587e8
extern UnkType_PolyDrawFn DAT_100587e8;
// GLOBAL: D3DREN 0x1005a304
extern UnkType_PolyDrawFn DAT_1005a304;
// GLOBAL: D3DREN 0x1005ce18
extern int DAT_1005ce18;		// guess: world models are drawn through the sorted poly list (translucent/portal pass)
// GLOBAL: D3DREN 0x10056688
extern int DAT_10056688;		// guess: g_nWorldPoliesProcessed (names_proposal low)

// guess: applies the light animation pAnim to the vertex colours of one poly (see FUN_1002f780, which has the same code written out); defined
// in unit unk/100132a0 (0x100185a0, package W2)
int FUN_100185a0(void *pPolyData, uint32 nPolyData, LightAnim *pAnim, uint32 *pRef);

void FUN_1002f780(MainWorld *pWorld, WorldPoly *pPoly);

// guess: draws the polys ppPolies[0..nPolies) of a world model (pInstance): those facing pViewPos (the viewer in the model's space) and
// not yet drawn this frame (frame tag) are dispatched by surface: no texture, lightmapped, panning sky or plain callback after
// relighting the WPF_RELIGHT polys; with DAT_1005ce18 set, polys of surfaces with m_Unknown3A != 0x7fff are not drawn here but collected
// in the visible set's sorted poly list (invisible surfaces with the model's transform).  nClipFlags is the clip plane mask of the model.
// NAME: guess_d3d_DrawWorldModelPolys (names_proposal.csv, low)
// FUNCTION: D3DREN 0x1002f440
void FUN_1002f440(WorldModelInstance *pInstance, WorldPoly **ppPolies, uint32 nPolies, LTVector *pViewPos, uint32 nClipFlags)
{
	uint16 nFrameCode;
	uint32 iPoly;

	g_ClipFlags = nClipFlags;
	nFrameCode = g_CurFrameCode + 1;

	if (DAT_1005ce18)
	{
		VisibleSet *pVisibleSet = d3d_GetVisibleSet();

		for (iPoly = 0; iPoly < nPolies; iPoly++)
		{
			WorldPoly *pPoly = ppPolies[iPoly];

			if (WORLDPOLY_FRAMECODE(pPoly) != nFrameCode)
			{
				WORLDPOLY_FRAMECODE(pPoly) = nFrameCode;
				if (pPoly->m_pPlane->DistTo(*pViewPos) > 0.0001f)
				{
					Surface *pSurface = POLY_SURFACE(pPoly);
					if (DAT_1005ce18 && pSurface->m_Unknown3A != 0x7fff)
					{
						if (pSurface->m_Flags & SURF_INVISIBLE)
						{
							uint32 nSorted = pVisibleSet->m_nUnk140;

							if (nSorted < 0x20)
							{
								uint32 i;

								for (i = 0; i < nSorted; i++)
								{
									if (pVisibleSet->m_Unk40[i].m_pPoly == pPoly)
										goto NextPoly;
								}

								pVisibleSet->m_Unk40[nSorted].m_pPoly = pPoly;
								pVisibleSet->m_Unk40[nSorted].m_pUnk = &pInstance->m_Transform;
								pVisibleSet->m_nUnk140++;
							}
						}
					}
					else
					{
						uint32 nSurfFlags;

						DAT_10056688++;
						nSurfFlags = POLY_SURFACE(pPoly)->m_Flags;
						if (!(nSurfFlags & SURF_INVISIBLE))
						{
							if (POLY_SURFACE(pPoly)->m_pTexture)
							{
								if (nSurfFlags & 0x80)
								{
									DAT_10058c24(pPoly);
								}
								else
								{
									if (pPoly->m_Flags & 0x4000)
									{
										FUN_1002f780(DAT_10056770, pPoly);
										pPoly->m_Flags &= 0xbfff;
									}

									if (nSurfFlags & 0x8000)
										DAT_1005a304(pPoly);
									else
										DAT_100587e8(pPoly);
								}
							}
							else
							{
								DAT_10058cd8(pPoly);
							}
						}
					}
				}
			}
NextPoly:;
		}
	}
	else
	{
		for (iPoly = 0; iPoly < nPolies; iPoly++)
		{
			WorldPoly *pPoly = ppPolies[iPoly];

			if (WORLDPOLY_FRAMECODE(pPoly) != nFrameCode)
			{
				WORLDPOLY_FRAMECODE(pPoly) = nFrameCode;
				if (pPoly->m_pPlane->DistTo(*pViewPos) > 0.0001f)
				{
					uint32 nSurfFlags;

					DAT_10056688++;
					nSurfFlags = POLY_SURFACE(pPoly)->m_Flags;
					if (!(nSurfFlags & SURF_INVISIBLE))
					{
						if (POLY_SURFACE(pPoly)->m_pTexture)
						{
							if (nSurfFlags & 0x80)
							{
								DAT_10058c24(pPoly);
							}
							else
							{
								if (pPoly->m_Flags & 0x4000)
								{
									MainWorld *pWorld = DAT_10056770;
									UnkType_PolyVertex *pVerts;
									uint32 nVerts;
									uint32 j;

									if (DAT_1005811c)
									{
										pVerts = (UnkType_PolyVertex *)pPoly->m_pVertices;
										nVerts = pPoly->m_nExtraVertices;
									}
									else
									{
										pVerts = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
										nVerts = pPoly->m_nVertices;
									}

									for (j = 0; j < nVerts; j++)
										*(uint32 *)pVerts[j].m_Color = 0;

									for (j = 0; j < pPoly->m_nLMAnimRefs; j++)
									{
										uint16 *pRef = (uint16 *)&pPoly->m_pLMAnimRefs[j];
										if (pRef[0] < pWorld->m_LightAnims.GetSize())
										{
											LightAnim *pAnim = &pWorld->m_LightAnims[pRef[0]];
											if (pAnim->m_iFrames[0] != 0xffffffff && pAnim->m_fBlendPercent >= 0.02f)
												FUN_100185a0(pVerts, nVerts, pAnim, (uint32 *)pRef);
										}
									}

									pPoly->m_Flags &= 0xbfff;
								}

								if (nSurfFlags & 0x8000)
									DAT_1005a304(pPoly);
								else
									DAT_100587e8(pPoly);
							}
						}
						else
						{
							DAT_10058cd8(pPoly);
						}
					}
				}
			}
		}
	}
}

// the body of FUN_100185a0 (0x100185a0, unit unk/100132a0, W2) as the exe has it expanded inside FUN_1002f780 (the same code; a copy
// in the source or an inline function)
static inline int FUN_100185a0_Inline(void *pPolyData, uint32 nPolyData, LightAnim *pAnim, uint32 *pRef)
{
	LAPolyRef *pPolyRef = (LAPolyRef *)pRef;
	uint8 percent;
	LAPolyFrame *pFrame0, *pFrame1;
	uint8 *pColor;
	uint32 i;

	if (pPolyRef->m_iPoly >= pAnim->m_nPolies)
		return 0;

	percent = (uint8)pAnim->m_PercentBetween;
	pFrame0 = pAnim->m_pFrames[pAnim->m_iFrames[0]] + pPolyRef->m_iPoly;
	pFrame1 = pAnim->m_pFrames[pAnim->m_iFrames[1]] + pPolyRef->m_iPoly;
	if (percent == 0)
		pFrame1 = pFrame0;
	else if (percent == 0xff)
		pFrame0 = pFrame1;

	nPolyData = LTMIN(nPolyData, LTMIN(pFrame0->m_nVerts, pFrame1->m_nVerts));

	pColor = (uint8 *)pPolyData + 0x14;
	if (pFrame0 == pFrame1)
	{
		for (i = 0; i < nPolyData; i++, pColor += 0x18)
		{
			pColor[2] = DAT_10092168.m_Unk00[pColor[2] + pFrame0->m_pVertR[i]];
			pColor[1] = DAT_10092168.m_Unk00[pColor[1] + pFrame0->m_pVertG[i]];
			pColor[0] = DAT_10092168.m_Unk00[pColor[0] + pFrame0->m_pVertB[i]];
		}
	}
	else
	{
		uint32 inv = (uint8)(-percent - 1);

		for (i = 0; i < nPolyData; i++, pColor += 0x18)
		{
			pColor[2] = DAT_10092168.m_Unk00[pColor[2] + DAT_10092168.m_Unk00[DAT_10082168.m_Unk00[pFrame0->m_pVertR[i] * 0x100 + inv] + DAT_10082168.m_Unk00[pFrame1->m_pVertR[i] * 0x100 + percent]]];
			pColor[1] = DAT_10092168.m_Unk00[pColor[1] + DAT_10092168.m_Unk00[DAT_10082168.m_Unk00[percent + pFrame1->m_pVertG[i] * 0x100] + DAT_10082168.m_Unk00[inv + pFrame0->m_pVertG[i] * 0x100]]];
			pColor[0] = DAT_10092168.m_Unk00[pColor[0] + DAT_10092168.m_Unk00[DAT_10082168.m_Unk00[percent + pFrame1->m_pVertB[i] * 0x100] + DAT_10082168.m_Unk00[inv + pFrame0->m_pVertB[i] * 0x100]]];
		}
	}
	return 1;
}

void FUN_1002f780(MainWorld *pWorld, WorldPoly *pPoly);


// guess: relights the polygon pPoly: its vertex colours are cleared and every light animation that touches it adds its frame colours
// (WPF_RELIGHT polys; the lightmap-less vertex colour path).  Name from names_proposal.csv (guess_d3d_RelightWorldPoly, low); loop 2 of
// FUN_1002f440 has the same code written out.
// STUB diagnosis (W6): 720 bytes like the exe; 528 differ (register assignment): the exe reloads pPoly from the stack in each branch (ours
// hoists `mov ecx,[esp+8]` in front of the test) and has a 0x1c byte frame (ours 0x20), keeps pFrame1 in ebp/[esp+0x18] and reloads the
// anim count and the world pointer in the loop.  The inlined FUN_100185a0 body is the exe's (same order of the six table lookups); the
// two-frame form (`percent == 0` / `0xff` after loading both frames) and LTMIN(LTMIN()) were needed to get there.  Permuter 6 minutes.
// STUB: D3DREN 0x1002f780
void FUN_1002f780(MainWorld *pWorld, WorldPoly *pPoly)
{
	UnkType_PolyVertex *pVerts;
	uint32 nVerts;
	uint32 i;

	if (DAT_1005811c)
	{
		pVerts = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pVerts = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	for (i = 0; i < nVerts; i++)
		*(uint32 *)pVerts[i].m_Color = 0;

	for (i = 0; i < pPoly->m_nLMAnimRefs; i++)
	{
		uint16 *pRef = (uint16 *)&pPoly->m_pLMAnimRefs[i];
		if (pRef[0] < pWorld->m_LightAnims.GetSize())
		{
			LightAnim *pAnim = &pWorld->m_LightAnims[pRef[0]];
			if (pAnim->m_iFrames[0] != 0xffffffff && pAnim->m_fBlendPercent >= 0.02f)
				FUN_100185a0_Inline(pVerts, nVerts, pAnim, (uint32 *)pRef);
		}
	}
}

// ---- d3d_DrawSolidWorldModel ----------------------------------------------------------------------------------------------------------

// GLOBAL: D3DREN 0x10055ce8
extern LTVector DAT_10055ce8;	// guess: the global light colour (unit unk/10019350 declares it the same way)
// GLOBAL: D3DREN 0x10057774
extern uint8 DAT_10057774;		// guess: the alpha byte of the vertex colours

// guess: the current object colour (0..1 per channel: the object's colour bytes times the global light colour), set by the draw
// functions of the unit before the polys are drawn
// GLOBAL: D3DREN 0x100756d0
extern LTVector DAT_100756d0;

// guess: sets the current object colour (DAT_100756d0: the colour bytes scaled to 0..1 and multiplied by the global light colour) and
// the vertex alpha for the polys of a world model.  An inline function of the original (the compiler's inline budget decides that
// MatMul stays a call in the two callers below only with this expansion in front of it).
static inline void WMSetColor(WorldModelInstance *pInstance)
{
	DAT_100756d0.x = (float)pInstance->m_ColorR * (1.0f / 255.0f) * DAT_10055ce8.x;
	DAT_100756d0.y = (float)pInstance->m_ColorG * (1.0f / 255.0f) * DAT_10055ce8.y;
	DAT_100756d0.z = (float)pInstance->m_ColorB * (1.0f / 255.0f) * DAT_10055ce8.z;
	DAT_10057774 = pInstance->m_ColorA;
}

void FUN_100144b0(int nMode);	// 0x100144b0 (unit unk/100132a0, W2): starts a world draw: selects the poly callbacks
void FUN_100145f0(int a1);		// 0x100145f0 (unit unk/100132a0, W2): draws the queued polys and resets the stage state

// NAME: d3d_DrawSolidWorldModel: Jupiter drawworldmodel.cpp d3d_DrawSolidWorldModel (names_proposal.csv, high): bound radius frustum test,
// fog switch, the model's transform multiplied into the view matrices, the polys of the original BSP drawn by FUN_1002f440, restore
// STUB diagnosis (W6): 1104 bytes like the exe, 44 bytes differ: the first part (the frustum loop) uses ecx/edx where the exe uses edx/ecx
// and tests the loop counter against 6 where ours tests the plane pointer; the second MatVMul_H (the view position in the model's
// space) has its three terms in x, z, y order where the exe has x, y, z for w and the rows 1 and 2 but y, z, x for row 0.
// (WMSetColor and the LTVector operator* variants were tried; the operator form drops the by-value temporaries the exe has.)
// STUB: D3DREN 0x1002fa80
void d3d_DrawSolidWorldModel(ViewParams *pParams, LTObject *pObject)
{
	WorldModelInstance *pInstance = (WorldModelInstance *)pObject;
	float fRadius;
	LTVector vPos;
	LTPlane *pPlane;
	uint32 nClipFlags;
	int i;
	DWORD dwOldFog;
	LTMatrix mSaved15c, mSavedFull;
	LTVector vOldFogPos, vFogPos, vViewPos;

	fRadius = pInstance->m_pOriginalBsp->GetBoundRadius();
	vPos = pInstance->m_Pos;
	nClipFlags = 0x3f;
	pPlane = g_ViewParams.m_ClipPlanes;
	for (i = 0; i < 6; pPlane++, i++)
	{
		float fDist = pPlane->DistTo(vPos);
		if (fDist < -fRadius)
			return;
		if (fDist > fRadius)
			nClipFlags &= ~(1 << i);
	}

	WMSetColor(pInstance);

	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, &dwOldFog);
	if (dwOldFog)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, !(pInstance->m_Flags & FLAG_FOGDISABLE));

	mSaved15c = pParams->m_mClipTransform;
	mSavedFull = pParams->m_FullTransform;
	MatMul(&pParams->m_mClipTransform, &mSaved15c, &pInstance->m_Transform);
	MatMul(&pParams->m_FullTransform, &pParams->m_DeviceTimesProjection, &pParams->m_mClipTransform);

	vOldFogPos = pParams->m_FogViewPos;
	MatVMul_H(&vFogPos, &pInstance->m_BackTransform, &vOldFogPos);
	pParams->SetupFogViewPosition(vFogPos);

	FUN_100144b0(0);

	MatVMul_H(&vViewPos, &pInstance->m_BackTransform, &pParams->m_Pos);
	if (pInstance->m_pOriginalBsp->IsUntransformed() == 0)
	{
		WorldBsp *pBsp = (WorldBsp *)pInstance->m_pOriginalBsp;
		FUN_1002f440(pInstance, pBsp->m_Polies, pBsp->m_nPolies, &vViewPos, nClipFlags);
	}
	else if (pInstance->m_pOriginalBsp->IsUntransformed() == 1)
	{
		TerrainSection *pSection = (TerrainSection *)pInstance->m_pOriginalBsp;
		FUN_1002f440(pInstance, pSection->m_Polies.GetArray(), pSection->m_Polies.GetSize(), &vViewPos, nClipFlags);
	}

	FUN_100145f0(0);

	pParams->m_mClipTransform = mSaved15c;
	pParams->m_FullTransform = mSavedFull;
	pParams->SetupFogViewPosition(vOldFogPos);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, dwOldFog);
}

// ---- d3d_DrawTranslucentWorldModel ------------------------------------------------------------------------------------------------------

void FUN_10030370(WorldPoly *pPoly);

// NAME: d3d_DrawTranslucentWorldModel: Jupiter drawworldmodel.cpp d3d_DrawTranslucentWorldModel role (names_proposal.csv, medium; the Talon
// version is the sorted-list callback that draws the BSP back to front itself): the same colour/fog/matrix prologue as
// d3d_DrawSolidWorldModel, the vertical fog position set to the origin, then the BSP of the original is walked with an explicit stack:
// the far side of every node first, then the node's own poly (FUN_10030370, or the visible set's sorted poly list), then the near side
// STUB diagnosis (W6): 784 vs 768 bytes: the frame is 0x20a4 against 0x20a0 (one extra local), the BSP stack index lives in ebp where the
// exe uses edi and the constant 3 of the leaf test is kept in bl (`test [ecx+0x16],bl`) where the exe has the immediate; the zero
// vector for FUN_1000f1a0 is built in a different order.  Control flow and calls are the exe's (WMSetColor expansion is what keeps
// MatMul out of line).  Permuter best 38 mismatches.
// STUB: D3DREN 0x10030070
void FUN_10030070(ViewParams *pParams, LTObject *pObject)
{
	WorldModelInstance *pInstance = (WorldModelInstance *)pObject;
	Node *aNodes[0x400];
	Node *aNear[0x400];
	DWORD dwOldFog;
	LTMatrix mSaved15c, mSavedFull;
	LTVector vOldFogPos;
	Node *pNode;
	int iStack;
	int iSide;

	WMSetColor(pInstance);

	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FOGENABLE, &dwOldFog);
	if (dwOldFog)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, !(pInstance->m_Flags & FLAG_FOGDISABLE));

	mSaved15c = pParams->m_mClipTransform;
	mSavedFull = pParams->m_FullTransform;
	vOldFogPos = pParams->m_FogViewPos;
	pParams->SetupFogViewPosition(LTVector(0.0f, 0.0f, 0.0f));
	MatMul(&pParams->m_mClipTransform, &mSaved15c, &pInstance->m_Transform);
	MatMul(&pParams->m_FullTransform, &pParams->m_DeviceTimesProjection, &pParams->m_mClipTransform);

	pNode = pInstance->m_pOriginalBsp->GetRootNode();
	iStack = 0;
	if (!(pNode->m_Flags & (NF_IN | NF_OUT)))
	{
		for (;;)
		{
			if (!(pNode->m_Flags & (NF_IN | NF_OUT)))
			{
				iSide = (pNode->m_pPoly->m_pPlane->DistTo(g_ViewParams.m_Pos) >= 0.0f);
				aNodes[iStack] = pNode;
				aNear[iStack] = pNode->m_Sides[iSide];
				iStack++;
				if (iStack < 0x400)
				{
					pNode = pNode->m_Sides[iSide == 0];
					continue;
				}
			}

			pNode = aNodes[iStack - 1];
			iStack--;
			if (pNode->m_pPoly)
			{
				WorldPoly *pPoly;

				g_ClipFlags = 0x3f;
				pPoly = pNode->m_pPoly;
				if (!(POLY_SURFACE(pPoly)->m_Flags & SURF_INVISIBLE))
				{
					if (!DAT_1005ce18 || POLY_SURFACE(pPoly)->m_Unknown3A == 0x7fff)
					{
						FUN_10030370(pPoly);
					}
					else
					{
						VisibleSet *pVisibleSet = d3d_GetVisibleSet();
						uint32 nSorted = pVisibleSet->m_nUnk140;

						if (nSorted < 0x20)
						{
							uint32 i;

							for (i = 0; i < nSorted; i++)
							{
								if (pVisibleSet->m_Unk40[i].m_pPoly == pPoly)
									goto NextNode;
							}

							pVisibleSet->m_Unk40[nSorted].m_pPoly = pPoly;
							pVisibleSet->m_Unk40[nSorted].m_pUnk = &pInstance->m_Transform;
							pVisibleSet->m_nUnk140++;
						}
					}
				}
			}
NextNode:
			pNode = aNear[iStack];
			if (iStack == 0 && (pNode->m_Flags & (NF_IN | NF_OUT)))
				break;
		}
	}

	pParams->m_mClipTransform = mSaved15c;
	pParams->m_FullTransform = mSavedFull;
	pParams->SetupFogViewPosition(vOldFogPos);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, dwOldFog);
}

// ---- d3d_DrawTranslucentWorldPoly ----------------------------------------------------------------------------------------------------

#define WORLDPOLY_LIGHTS(p)	((UnkType_PolyLightRef *)*(uint32 *)((uint8 *)(p) + 0x30))

// GLOBAL: D3DREN 0x10057798
extern TLRGB DAT_10057798;		// guess: the ambient world colour added to the light grid sample of a translucent world poly
// GLOBAL: D3DREN 0x100566cc
extern int DAT_100566cc;		// guess: the number of light tests this frame ("Num Light Tests")
// GLOBAL: D3DREN 0x100577b8
extern uint16 DAT_100577b8;		// guess: the frame code a texture is stamped with when it is used (SharedTexture::m_Unknown30)
struct UnkType_RTexW6
{
	void				*m_pVtbl;		// 0x00
	float				m_Unk04;
	float				m_Unk08;
	IDirectDrawSurface7	*m_pSurface;	// 0x0c the texture surface
	uint8				m_Pad10[0x30 - 0x10];
	UnkType_RTexW6		*m_pNext;		// 0x30 next RTexture of the same SharedTexture (one per stage)
	uint8				m_Pad34[0x42 - 0x34];
	uint16				m_nStage;		// 0x42 device stage
	uint16				m_nLOD;			// 0x44 current LOD
};

// NAME: SetUV: the shape of the vertex fillers of unit unk/10001000 (arguments evaluated right to left stay on the x87 stack)
static inline void SetUV(TLVertex *pVertex, float u, float v)
{
	pVertex->tu = u;
	pVertex->tv = v;
}

void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGB *pRGB);			// 0x1000c860 (W4, unit unk/1000c860: the light grid lookup)
RTexture *FUN_1001fff0(SharedTexture *pTexture, uint32 nStage, uint8 bChild);		// 0x1001fff0 (d3d_texture): finds or creates the RTexture for the stage
void *FUN_10009350(void *pFirst, uint8 nStage);							// 0x10009350: the RTexture of the stage in the chain of pFirst, or 0
int FUN_10014100(WorldPoly *pPoly);											// 0x10014100 (W2, unit unk/100132a0): draws a poly flat

// guess: draws one translucent world poly: relights it (WPF_RELIGHT), colours the vertices with the light grid sample at the poly
// centre plus the ambient colour times the object colour, adds the dynamic lights that touch it, transforms, clips and projects
// the 0x28-byte vertices and draws them with the base texture (plus the detail texture when it has one) as a triangle fan.
// NAME: guess_d3d_DrawTranslucentWorldPoly (names_proposal.csv, low)
// STUB diagnosis (W6): 1952 vs 1888 bytes.  The structure follows the exe block by block (relight, light grid sample, env map test, vertex
// colour loop, dynamic light loop, transform, clip, project, texture bind with its three outcomes, detail texture, scale loop,
// DrawPrimitive); differences: pPoly lives in esi (the exe: ebx), the u/v copy of the vertex loop is integer moves where the exe uses
// x87 loads, the exe calls _CVector<float>::Dot (by value, 0x100187c0) in the dynamic light loop where ours inlines it, and its
// d3d_SetTexture expansion for the detail texture is a call.  Not iterated further.
// STUB: D3DREN 0x10030370
void FUN_10030370(WorldPoly *pPoly)
{
	UnkType_TLVertex40 aVerts[0x80];
	UnkType_PolyVertex *pSrc;
	UnkType_TLVertex40 *pDest;
	UnkType_TLVertex40 *pVerts;
	LTRGB lightRGB;
	int nVerts;
	int bEnvMap;
	uint32 nR, nG, nB;
	int nObjR, nObjG, nObjB;
	int i;

	bEnvMap = 0;
	if (WORLDPOLY_FRAMECODE(pPoly) == g_CurFrameCode + 1)
		return;
	WORLDPOLY_FRAMECODE(pPoly) = g_CurFrameCode + 1;

	if (pPoly->m_Flags & 0x4000)
	{
		MainWorld *pWorld = DAT_10056770;
		UnkType_PolyVertex *pRelightVerts;
		uint32 nRelightVerts;
		uint32 j;

		if (DAT_1005811c)
		{
			pRelightVerts = (UnkType_PolyVertex *)pPoly->m_pVertices;
			nRelightVerts = pPoly->m_nExtraVertices;
		}
		else
		{
			pRelightVerts = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
			nRelightVerts = pPoly->m_nVertices;
		}

		for (j = 0; j < nRelightVerts; j++)
			*(uint32 *)pRelightVerts[j].m_Color = 0;

		for (j = 0; j < pPoly->m_nLMAnimRefs; j++)
		{
			uint16 *pRef = (uint16 *)&pPoly->m_pLMAnimRefs[j];
			if (pRef[0] < pWorld->m_LightAnims.GetSize())
			{
				LightAnim *pAnim = &pWorld->m_LightAnims[pRef[0]];
				if (pAnim->m_iFrames[0] != 0xffffffff && pAnim->m_fBlendPercent >= 0.02f)
					FUN_100185a0(pRelightVerts, nRelightVerts, pAnim, (uint32 *)pRef);
			}
		}

		pPoly->m_Flags &= 0xbfff;
	}

	w_GetLightVal(&DAT_10056770->m_LightTable, &pPoly->m_Center, &lightRGB);

	if (POLY_SURFACE(pPoly)->m_pTexture)
	{
		if (POLY_SURFACE(pPoly)->m_pTexture->m_eTexType != 0 && g_CV_EnvMapWorld.m_IntVal)
			bEnvMap = 1;
		else
			bEnvMap = 0;
	}

	if (DAT_1005811c)
	{
		pSrc = (UnkType_PolyVertex *)pPoly->m_pVertices;
		nVerts = pPoly->m_nExtraVertices;
	}
	else
	{
		pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		nVerts = pPoly->m_nVertices;
	}

	if (nVerts > 0x80)
	{
		dsi_ConsolePrint("Error: vertex buffer overflow");
		return;
	}

	nR = DAT_10057798.r + lightRGB.r;
	nG = DAT_10057798.g + lightRGB.g;
	nB = DAT_10057798.b + lightRGB.b;
	nObjR = (int)(DAT_100756d0.x * 255.0f);
	nObjG = (int)(DAT_100756d0.y * 255.0f);
	nObjB = (int)(DAT_100756d0.z * 255.0f);

	pDest = aVerts;
	for (i = nVerts; i > 0; i--)
	{
		uint32 r, g, b;

		pDest->m_Vec = *pSrc->m_Vec;
		r = pSrc->m_Color[2] + nR;
		g = pSrc->m_Color[1] + nG;
		b = pSrc->m_Color[0] + nB;
		if (r > 0xff)
			r = 0xff;
		if (g > 0xff)
			g = 0xff;
		if (b > 0xff)
			b = 0xff;
		pDest->rgb.r = (uint8)((r * nObjR) >> 8);
		pDest->rgb.g = (uint8)((g * nObjG) >> 8);
		pDest->rgb.b = (uint8)((b * nObjB) >> 8);
		pDest->rgb.a = DAT_10057774;
		SetUV((TLVertex *)pDest, pSrc->m_U, pSrc->m_V);
		if (bEnvMap)
			FUN_1001085e(&g_ViewParams.m_Pos, &pDest->m_Vec, &pPoly->m_pPlane->m_Normal, &pDest->tu2, &pDest->tv2);
		pSrc++;
		pDest++;
	}

	for (UnkType_PolyLightRef *pRef = WORLDPOLY_LIGHTS(pPoly); pRef; pRef = pRef->m_pNext)
	{
		DynamicLight *pLight = pRef->m_pLight;
		LTVector vLightPos = pRef->m_Pos;
		float fLightR = (float)pLight->m_ColorR;
		float fLightG = (float)pLight->m_ColorG;
		float fLightB = (float)pLight->m_ColorB;
		fLightR = fLightR - (255.0f - fLightR);
		fLightG = fLightG - (255.0f - fLightG);
		fLightB = fLightB - (255.0f - fLightB);
		DAT_100566cc++;

		float fDist = pPoly->m_pPlane->DistTo(vLightPos);
		if (fDist < 0.0f)
			fDist = -fDist;
		if (fDist < pLight->m_LightRadius)
		{
			float fRadiusSqr = pLight->m_LightRadius * pLight->m_LightRadius;
			float fInvRadius = 1.0f / pLight->m_LightRadius;
			uint32 nPoly = pPoly->m_nVertices;
			if (nPoly > 0)
			{
				uint8 *pColor = (uint8 *)aVerts + 0x12;
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
						if (DAT_100578ec)
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
					pColor += 0x28;
				} while (--nPoly != 0);
			}
		}
	}

	g_ClipFlags = 0x3f;
	pVerts = aVerts;
	pDest = aVerts;
	for (i = nVerts; i != 0; i--)
	{
		FUN_10008719(&pDest->m_Vec.x, &g_ViewParams.m_mClipTransform.m[0][0]);
		g_pfnCalcFogAlpha(&pDest->m_Vec, &pDest->specular);
		pDest++;
	}

	if (!FUN_10008779(g_ClipFlags, &pVerts, &nVerts))
	{
		d3d_UnsetDetailTexture();
		return;
	}

	pDest = pVerts;
	for (i = nVerts; i != 0; i--)
	{
		ProjectVertexToScreen(&pDest->m_Vec.x, &g_ViewParams);
		pDest++;
	}

	{
		SharedTexture *pTexture = POLY_SURFACE(pPoly)->m_pTexture;

		if (pTexture)
		{
			UnkType_RTexW6 *pRTexture;
			UnkType_RTexW6 *pFirst = (UnkType_RTexW6 *)pTexture->m_pRenderData;
			uint32 nStage = g_NormalTextureStage;

			pTexture->m_Unknown30 = DAT_100577b8;
			if (pFirst && (pRTexture = (UnkType_RTexW6 *)FUN_10009350(pFirst, (uint8)nStage)) != 0)
			{
				if (pRTexture != (UnkType_RTexW6 *)g_pBoundTextures[nStage])
					FUN_10007a89((RTexture *)pRTexture);
			}
			else
			{
				pFirst = (UnkType_RTexW6 *)pTexture->m_pRenderData;
				if (pFirst)
				{
					pRTexture = (UnkType_RTexW6 *)FUN_1001fff0(pTexture, nStage, 1);
					if (!pRTexture)
					{
						d3d_UnsetDetailTexture();
						FUN_10014100(pPoly);
						return;
					}
					pRTexture->m_pNext = pFirst->m_pNext;
					pFirst->m_pNext = pRTexture;
				}
				else
				{
					pRTexture = (UnkType_RTexW6 *)FUN_1001fff0(pTexture, nStage, 0);
					if (!pRTexture)
					{
						d3d_UnsetDetailTexture();
						FUN_10014100(pPoly);
						return;
					}
				}
				FUN_10007a89((RTexture *)pRTexture);
			}

			if (pRTexture->m_nLOD != 0)
			{
				pRTexture->m_pSurface->SetLOD(0);
				pRTexture->m_nLOD = 0;
			}

			DAT_10063c90.FUN_10021da6();
			if (POLY_SURFACE(pPoly)->m_pTexture->m_pStateChange)
				DAT_10063c90.FUN_10021db7(POLY_SURFACE(pPoly)->m_pTexture->m_pStateChange, g_NormalTextureStage);

			if (DAT_1005de2c && g_CV_DetailTextures.m_IntVal && g_pBoundTextures[0] && POLY_SURFACE(pPoly)->m_pTexture->m_pLinkedTexture &&
				(!POLY_SURFACE(pPoly)->m_pTexture->m_eTexType || g_CV_EnvMapWorld.m_IntVal) &&
				d3d_SetTexture(POLY_SURFACE(pPoly)->m_pTexture->m_pLinkedTexture, 1, 0))
			{
				d3d_SetDetailTextureStates();
			}
			else
			{
				d3d_UnsetDetailTexture();
			}
		}
		else
		{
			if (g_pBoundTextures[g_NormalTextureStage])
			{
				g_pD3DDevice->SetTexture(g_NormalTextureStage, NULL);
				g_pBoundTextures[g_NormalTextureStage] = 0;
			}
			d3d_UnsetDetailTexture();
		}
	}

	pDest = pVerts;
	for (i = nVerts; i != 0; i--)
	{
		pDest->tu *= DAT_10061810[0].m_Unk00;
		pDest->tv *= DAT_10061810[0].m_Unk04;
		pDest++;
	}

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x2c4, pVerts, nVerts, 0);
	DAT_10063c90.FUN_10021da6();
	d3d_UnsetDetailTexture();
}

// the sprite size / bias constants of Jupiter drawsprite.cpp
#define SPRITE_POSITION_ZBIAS	-20.0f
#define SPRITE_MINFACTORDIST	10.0f
#define SPRITE_MAXFACTORDIST	500.0f
#define SPRITE_MINFACTOR		0.1f
#define SPRITE_MAXFACTOR		2.0f
