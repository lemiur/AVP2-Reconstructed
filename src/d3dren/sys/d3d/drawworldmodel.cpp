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
	uint32 nFlags = GetWorldModelFirstSurfaceFlags(pObject);
	if (nFlags & 8)
		d3d_GetVisibleSet()->m_TranslucentWorldModels.Add(pObject);
	else if (pObject->m_Flags2 & FLAG2_CHROMAKEY)
		d3d_GetVisibleSet()->m_Unk224.Add(pObject);
	else
		d3d_GetVisibleSet()->m_SolidWorldModels.Add(pObject);
}

// NAME: d3d_DrawSolidWorldModels: Jupiter drawworldmodel.cpp (names_proposal.csv, medium; no arguments in Talon)
// FUNCTION: D3DREN 0x1002fa50
void d3d_DrawSolidWorldModels()
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
void d3d_DrawChromaKeyWorldModels()
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
// (surface flag 1<<19) are collected in a local object set and queued after the others (callback d3d_DrawAdditiveWorldModel)
void d3d_DrawTranslucentWorldModel(ViewParams *pParams, LTObject *pObject);
void d3d_DrawAdditiveWorldModel(ViewParams *pParams, LTObject *pObject);

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
void d3d_QueueTranslucentWorldModels()
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

			if (GetWorldModelFirstSurfaceFlags(pObject) & 0x80000)
				cAdditive.Add(pObject);
			else
				g_pTranslucentObjectDrawList->Add(pObject, d3d_DrawTranslucentWorldModel);
		}
		if (cAdditive.m_nObjects > 0)
		{
			for (uint32 j = 0; j < cAdditive.m_nObjects; j++)
				g_pTranslucentObjectDrawList->Add(aAdditive[j], d3d_DrawAdditiveWorldModel);
		}
	}
}

// guess: additive world model: blend factors ONE/ONE and fog colour black around the translucent world model draw
// FUNCTION: D3DREN 0x10030ad0
void d3d_DrawAdditiveWorldModel(ViewParams *pParams, LTObject *pObject)
{
	StateSet ssSrcBlend(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
	StateSet ssDestBlend(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
	StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, 0);

	d3d_DrawTranslucentWorldModel(pParams, pObject);
}

// ---- drawworldmodel: world polygon drawing ------------------------------------------------------------------------------------------

// the polygon's surface (WorldPoly::m_pSurface is a void * in de_objects.h)
#define POLY_SURFACE(p)		((Surface *)(p)->m_pSurface)
// guess: the poly's frame tag (the tagging code sets it to the frame code of the frame the poly was seen in)
#define WORLDPOLY_FRAMECODE(p)	(*(uint16 *)((uint8 *)(p) + 0x46))


// guess: applies the light animation pAnim to the vertex colours of one poly (see RelightWorldPolyVertices, which has the same code written out); defined
// in unit unk/100132a0 (0x100185a0, package W2)
int d3d_AddLightAnimVertexColors(void *pPolyData, uint32 nPolyData, LightAnim *pAnim, uint32 *pRef);

void RelightWorldPolyVertices(MainWorld *pWorld, WorldPoly *pPoly);

// guess: draws the polys ppPolies[0..nPolies) of a world model (pInstance): those facing pViewPos (the viewer in the model's space) and
// not yet drawn this frame (frame tag) are dispatched by surface: no texture, lightmapped, panning sky or plain callback after
// relighting the WPF_RELIGHT polys; with g_bPortalsEnabled set, polys of surfaces with m_Unknown3A != 0x7fff are not drawn here but collected
// in the visible set's sorted poly list (invisible surfaces with the model's transform).  nClipFlags is the clip plane mask of the model.
// NAME: guess_d3d_DrawWorldModelPolys (names_proposal.csv, low)
// FUNCTION: D3DREN 0x1002f440
void DrawWorldModelPolyList(WorldModelInstance *pInstance, WorldPoly **ppPolies, uint32 nPolies, LTVector *pViewPos, uint32 nClipFlags)
{
	uint16 nFrameCode;
	uint32 iPoly;

	g_ClipFlags = nClipFlags;
	nFrameCode = g_CurFrameCode + 1;

	if (g_bPortalsEnabled)
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
					if (g_bPortalsEnabled && pSurface->m_Unknown3A != 0x7fff)
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

						g_nWorldPolysProcessed++;
						nSurfFlags = POLY_SURFACE(pPoly)->m_Flags;
						if (!(nSurfFlags & SURF_INVISIBLE))
						{
							if (POLY_SURFACE(pPoly)->m_pTexture)
							{
								if (nSurfFlags & 0x80)
								{
									g_pfnDrawLightmappedWorldPoly(pPoly);
								}
								else
								{
									if (pPoly->m_Flags & 0x4000)
									{
										RelightWorldPolyVertices(g_pFrameMainWorld, pPoly);
										pPoly->m_Flags &= 0xbfff;
									}

									if (nSurfFlags & 0x8000)
										g_pfnDrawPanningSkyWorldPoly(pPoly);
									else
										g_pfnDrawTexturedWorldPoly(pPoly);
								}
							}
							else
							{
								g_pfnDrawUntexturedWorldPoly(pPoly);
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

					g_nWorldPolysProcessed++;
					nSurfFlags = POLY_SURFACE(pPoly)->m_Flags;
					if (!(nSurfFlags & SURF_INVISIBLE))
					{
						if (POLY_SURFACE(pPoly)->m_pTexture)
						{
							if (nSurfFlags & 0x80)
							{
								g_pfnDrawLightmappedWorldPoly(pPoly);
							}
							else
							{
								if (pPoly->m_Flags & 0x4000)
								{
									MainWorld *pWorld = g_pFrameMainWorld;
									UnkType_PolyVertex *pVerts;
									uint32 nVerts;
									uint32 j;

									if (g_FixTJunc)
									{
										pVerts = (UnkType_PolyVertex *)pPoly->m_pVertices;
										nVerts = pPoly->m_nExtraVertices;
									}
									else
									{
										pVerts = (UnkType_PolyVertex *)pPoly->m_Vertices;
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
												d3d_AddLightAnimVertexColors(pVerts, nVerts, pAnim, (uint32 *)pRef);
										}
									}

									pPoly->m_Flags &= 0xbfff;
								}

								if (nSurfFlags & 0x8000)
									g_pfnDrawPanningSkyWorldPoly(pPoly);
								else
									g_pfnDrawTexturedWorldPoly(pPoly);
							}
						}
						else
						{
							g_pfnDrawUntexturedWorldPoly(pPoly);
						}
					}
				}
			}
		}
	}
}

// the body of d3d_AddLightAnimVertexColors (0x100185a0, unit unk/100132a0, W2) as the exe has it expanded inside RelightWorldPolyVertices (the same code; a copy
// in the source or an inline function)
static inline int AddLightAnimVertexColorsInline(void *pPolyData, uint32 nInputPolyData, LightAnim *pAnim, uint32 *pRef)
{
	uint32 nPolyData;
	LAPolyRef *pPolyRef = (LAPolyRef *)pRef;
	uint8 percent;
	LAPolyFrame *pFrame0, *pFrame1;
	SPolyVertex *pVerts = (SPolyVertex *)pPolyData;
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

	nPolyData = LTMIN(nInputPolyData, LTMIN(pFrame0->m_nVerts, pFrame1->m_nVerts));

	if (pFrame0 == pFrame1)
	{
		for (i = 0; i < nPolyData; i++)
		{
			pVerts[i].m_Color[2] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[2] + pFrame0->m_pVertR[i]];
			pVerts[i].m_Color[1] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[1] + pFrame0->m_pVertG[i]];
			pVerts[i].m_Color[0] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[0] + pFrame0->m_pVertB[i]];
		}
	}
	else
	{
		uint32 inv = (uint8)(-percent - 1);

		for (i = 0; i < nPolyData; i++)
		{
			pVerts[i].m_Color[2] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[2] + g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[pFrame1->m_pVertR[i] * 0x100 + percent] + g_ByteMultiplyTable.m_Unk00[pFrame0->m_pVertR[i] * 0x100 + inv]]];
			pVerts[i].m_Color[1] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[1] + g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[pFrame1->m_pVertG[i] * 0x100 + percent] + g_ByteMultiplyTable.m_Unk00[pFrame0->m_pVertG[i] * 0x100 + inv]]];
			pVerts[i].m_Color[0] = g_ByteSaturatingAddTable.m_Unk00[pVerts[i].m_Color[0] + g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[pFrame1->m_pVertB[i] * 0x100 + percent] + g_ByteMultiplyTable.m_Unk00[pFrame0->m_pVertB[i] * 0x100 + inv]]];
		}
	}
	return 1;
}

void RelightWorldPolyVertices(MainWorld *pWorld, WorldPoly *pPoly);


// guess: relights the polygon pPoly: its vertex colours are cleared and every light animation that touches it adds its frame colours
// (WPF_RELIGHT polys; the lightmap-less vertex colour path).  Name from names_proposal.csv (guess_d3d_RelightWorldPoly, low); loop 2 of
// DrawWorldModelPolyList has the same code written out.
// STUB diagnosis: 720/720 bytes, 423 strict differences. A separate clamped count and per-branch green-origin
// cursor restore the native branch-local polygon loads and colour offsets. Frame size (0x24 versus native 0x1c),
// register allocation, nested-min scheduling and table-lookup scheduling still differ.
// STUB: D3DREN 0x1002f780
void RelightWorldPolyVertices(MainWorld *pWorld, WorldPoly *pPoly)
{
	UnkType_PolyVertex *pVerts;
	uint32 nVerts;
	uint32 i;

	if (g_FixTJunc)
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
				AddLightAnimVertexColorsInline(pVerts, nVerts, pAnim, (uint32 *)pRef);
		}
	}
}

// ---- d3d_DrawSolidWorldModel ----------------------------------------------------------------------------------------------------------

// GLOBAL: D3DREN 0x10057774
extern uint8 g_nPolyVertexAlpha;		// guess: the alpha byte of the vertex colours

// guess: the current object colour (0..1 per channel: the object's colour bytes times the global light colour), set by the draw
// functions of the unit before the polys are drawn
// GLOBAL: D3DREN 0x100756d0
extern LTVector g_WorldModelObjectColor;

// guess: sets the current object colour (g_WorldModelObjectColor: the colour bytes scaled to 0..1 and multiplied by the global light colour) and
// the vertex alpha for the polys of a world model.  An inline function of the original (the compiler's inline budget decides that
// MatMul stays a call in the two callers below only with this expansion in front of it).
static inline void WMSetColor(WorldModelInstance *pInstance)
{
	g_WorldModelObjectColor.x = ((float)pInstance->m_ColorR * (1.0f / 255.0f)) * g_GlobalVertexTint.x;
	g_WorldModelObjectColor.y = ((float)pInstance->m_ColorG * (1.0f / 255.0f)) * g_GlobalVertexTint.y;
	g_WorldModelObjectColor.z = ((float)pInstance->m_ColorB * (1.0f / 255.0f)) * g_GlobalVertexTint.z;
	g_nPolyVertexAlpha = pInstance->m_ColorA;
}

void d3d_SetWorldPolyDrawMode(int nMode);	// 0x100144b0 (unit unk/100132a0, W2): starts a world draw: selects the poly callbacks
void d3d_FlushQueuedWorldDrawPasses(int a1);		// 0x100145f0 (unit unk/100132a0, W2): draws the queued polys and resets the stage state

// guess: the MatVMul_H variant inlined into the world-model view-position transform at 0x1002fd1e.
// The SDK formula is accumulated in the target's x87 order: w/y/z use x,y,z, while the x row uses y,z,x.
// Keep this as an inline helper: writing the expansion in the caller changes its MatMul inlining decisions.
static inline float TransformWorldModelViewPosition(LTVector *pDest, LTMatrix *pMat, LTVector *pSrc)
{
	float fW = pMat->m[3][0] * pSrc->x;
	fW += pMat->m[3][1] * pSrc->y;
	fW += pMat->m[3][2] * pSrc->z;
	fW += pMat->m[3][3];
	fW = 1.0f / fW;
	float fX = pMat->m[0][1] * pSrc->y;
	fX += pMat->m[0][2] * pSrc->z;
	fX += pMat->m[0][0] * pSrc->x;
	fX += pMat->m[0][3];
	pDest->x = fW * fX;
	float fY = pMat->m[1][0] * pSrc->x;
	fY += pMat->m[1][1] * pSrc->y;
	fY += pMat->m[1][2] * pSrc->z;
	fY += pMat->m[1][3];
	pDest->y = fW * fY;
	float fZ = pMat->m[2][0] * pSrc->x;
	fZ += pMat->m[2][1] * pSrc->y;
	fZ += pMat->m[2][2] * pSrc->z;
	fZ += pMat->m[2][3];
	pDest->z = fW * fZ;
	return fW;
}

// NAME: d3d_DrawSolidWorldModel: Jupiter drawworldmodel.cpp d3d_DrawSolidWorldModel (names_proposal.csv, high): bound radius frustum test,
// fog switch, the model's transform multiplied into the view matrices, the polys of the original BSP drawn by DrawWorldModelPolyList, restore
// MATCH: initialize the clip counter before the position copy; preserve WMSetColor's byte-to-float scaling before global-light
// multiplication, and use the target's per-term homogeneous transform above.
// FUNCTION: D3DREN 0x1002fa80
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
	i = 0;
	vPos = pInstance->m_Pos;
	nClipFlags = 0x3f;
	pPlane = g_ViewParams.m_ClipPlanes;
	for (; i < 6; pPlane++, i++)
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

	d3d_SetWorldPolyDrawMode(0);

	TransformWorldModelViewPosition(&vViewPos, &pInstance->m_BackTransform, &pParams->m_Pos);
	if (pInstance->m_pOriginalBsp->IsUntransformed() == 0)
	{
		WorldBsp *pBsp = (WorldBsp *)pInstance->m_pOriginalBsp;
		DrawWorldModelPolyList(pInstance, pBsp->m_Polies, pBsp->m_nPolies, &vViewPos, nClipFlags);
	}
	else if (pInstance->m_pOriginalBsp->IsUntransformed() == 1)
	{
		TerrainSection *pSection = (TerrainSection *)pInstance->m_pOriginalBsp;
		DrawWorldModelPolyList(pInstance, pSection->m_Polies.GetArray(), pSection->m_Polies.GetSize(), &vViewPos, nClipFlags);
	}

	d3d_FlushQueuedWorldDrawPasses(0);

	pParams->m_mClipTransform = mSaved15c;
	pParams->m_FullTransform = mSavedFull;
	pParams->SetupFogViewPosition(vOldFogPos);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FOGENABLE, dwOldFog);
}

// ---- d3d_DrawTranslucentWorldModel ------------------------------------------------------------------------------------------------------

void DrawTranslucentWorldModelPoly(WorldPoly *pPoly);

// NAME: d3d_DrawTranslucentWorldModel: Jupiter drawworldmodel.cpp d3d_DrawTranslucentWorldModel role (names_proposal.csv, medium; the Talon
// version is the sorted-list callback that draws the BSP back to front itself): the same colour/fog/matrix prologue as
// d3d_DrawSolidWorldModel, the vertical fog position set to the origin, then the BSP of the original is walked with an explicit stack:
// the far side of every node first, then the node's own poly (DrawTranslucentWorldModelPoly, or the visible set's sorted poly list), then the near side
// Native traversal descends through the far child in an inner loop; explicit XYZ plane arithmetic
// preserves the original view snapshot and x87 scheduling (768 bytes, strict byte/relocation MATCH).
// FUNCTION: D3DREN 0x10030070
void d3d_DrawTranslucentWorldModel(ViewParams *pParams, LTObject *pObject)
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
			while (!(pNode->m_Flags & (NF_IN | NF_OUT)))
			{
				// Native 0x100301f9-0x10030211 snapshots view XYZ before the plane load.
				LTVector vView = g_ViewParams.m_Pos;
				LTPlane *pPlane = pNode->m_pPoly->m_pPlane;
				iSide = ((pPlane->m_Normal.x * vView.x + pPlane->m_Normal.y * vView.y + pPlane->m_Normal.z * vView.z - pPlane->m_Dist) >= 0.0f);
				aNodes[iStack] = pNode;
				aNear[iStack] = pNode->m_Sides[iSide];
				iStack++;
				if (iStack < 0x400)
				{
					pNode = pNode->m_Sides[!iSide];
					continue;
				}
				break;
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
					if (!g_bPortalsEnabled || POLY_SURFACE(pPoly)->m_Unknown3A == 0x7fff)
					{
						DrawTranslucentWorldModelPoly(pPoly);
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
extern TLRGB g_GlobalModelDirAdd2Color;		// guess: the ambient world colour added to the light grid sample of a translucent world poly
// GLOBAL: D3DREN 0x100566cc
extern int g_nLightTests;		// guess: the number of light tests this frame ("Num Light Tests")
// GLOBAL: D3DREN 0x100577b8
extern uint16 g_CurTextureFrameCode;		// guess: the frame code a texture is stamped with when it is used (SharedTexture::m_Unknown30)
// NAME: SetUV: the shape of the vertex fillers of unit unk/10001000 (arguments evaluated right to left stay on the x87 stack)
static inline void SetUV(TLVertex *pVertex, float u, float v)
{
	pVertex->tu = u;
	pVertex->tv = v;
}

void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGB *pRGB);			// 0x1000c860 (W4, unit unk/1000c860: the light grid lookup)
RTexture *d3d_CreateAndLoadTexture(SharedTexture *pTexture, uint32 nStage, uint8 bChild);		// 0x1001fff0 (d3d_texture): finds or creates the RTexture for the stage
int d3d_DrawFlatWorldPoly(WorldPoly *pPoly);											// 0x10014100 (W2, unit unk/100132a0): draws a poly flat

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
void DrawTranslucentWorldModelPoly(WorldPoly *pPoly)
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
		MainWorld *pWorld = g_pFrameMainWorld;
		UnkType_PolyVertex *pRelightVerts;
		uint32 nRelightVerts;
		uint32 j;

		if (g_FixTJunc)
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
					d3d_AddLightAnimVertexColors(pRelightVerts, nRelightVerts, pAnim, (uint32 *)pRef);
			}
		}

		pPoly->m_Flags &= 0xbfff;
	}

	w_GetLightVal(&g_pFrameMainWorld->m_LightTable, &pPoly->m_Center, &lightRGB);

	if (POLY_SURFACE(pPoly)->m_pTexture)
	{
		if (POLY_SURFACE(pPoly)->m_pTexture->m_eTexType != 0 && g_CV_EnvMapWorld.m_IntVal)
			bEnvMap = 1;
		else
			bEnvMap = 0;
	}

	if (g_FixTJunc)
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

	nR = g_GlobalModelDirAdd2Color.r + lightRGB.r;
	nG = g_GlobalModelDirAdd2Color.g + lightRGB.g;
	nB = g_GlobalModelDirAdd2Color.b + lightRGB.b;
	nObjR = (int)(g_WorldModelObjectColor.x * 255.0f);
	nObjG = (int)(g_WorldModelObjectColor.y * 255.0f);
	nObjB = (int)(g_WorldModelObjectColor.z * 255.0f);

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
		pDest->rgb.a = g_nPolyVertexAlpha;
		SetUV((TLVertex *)pDest, pSrc->m_U, pSrc->m_V);
		if (bEnvMap)
			d3d_CalcWorldReflectionUVs(&g_ViewParams.m_Pos, &pDest->m_Vec, &pPoly->m_pPlane->m_Normal, &pDest->tu2, &pDest->tv2);
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
		g_nLightTests++;

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
		TransformPositionInPlace(&pDest->m_Vec.x, &g_ViewParams.m_mClipTransform.m[0][0]);
		g_pfnCalcFogAlpha(&pDest->m_Vec, &pDest->specular);
		pDest++;
	}

	if (!ClipPolygon40(g_ClipFlags, &pVerts, &nVerts))
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
			if (!d3d_SetTexture(pTexture, g_NormalTextureStage, 0))
			{
				d3d_UnsetDetailTexture();
				d3d_DrawFlatWorldPoly(pPoly);
				return;
			}

			g_TextureStateRestorer.RestoreAllStates();
			if (POLY_SURFACE(pPoly)->m_pTexture->m_pStateChange)
				g_TextureStateRestorer.ApplyStateChange(POLY_SURFACE(pPoly)->m_pTexture->m_pStateChange, g_NormalTextureStage);

			if (g_bTwoTextureStageBlendValidated && g_CV_DetailTextures.m_IntVal && g_pBoundTextures[0] && POLY_SURFACE(pPoly)->m_pTexture->m_pLinkedTexture &&
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
		pDest->tu *= g_TextureStageTexelSizes[0].m_Unk00;
		pDest->tv *= g_TextureStageTexelSizes[0].m_Unk04;
		pDest++;
	}

	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0x2c4, pVerts, nVerts, 0);
	g_TextureStateRestorer.RestoreAllStates();
	d3d_UnsetDetailTexture();
}

// the sprite size / bias constants of Jupiter drawsprite.cpp
#define SPRITE_POSITION_ZBIAS	-20.0f
#define SPRITE_MINFACTORDIST	10.0f
#define SPRITE_MAXFACTORDIST	500.0f
#define SPRITE_MINFACTOR		0.1f
#define SPRITE_MAXFACTOR		2.0f
