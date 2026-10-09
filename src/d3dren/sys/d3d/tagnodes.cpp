// d3d.ren sys/d3d/tagnodes (0x10038830-0x1003a680): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit sys/d3d/tagnodes (0x10038830-0x1003a680): the VisibleSet and the tagging of the objects the world tree
// query finds (Talon form of Jupiter's render_a/src/sys/d3d/tagnodes.cpp), followed by the CMoArray<WorldPoly*> template
// code the object needs (vtable 0x100464ec).
// FLAGS: /O2 /Ob2
#include <windows.h>
#include "ltbasedefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "world_tree.h"
#include "counter.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/visibleset.h"
#include "d3dren/viewparams.h"	// g_ViewParams, g_ClipFlags (clip plane mask)
#include "d3dren/drawobjects.h"	// ObjectHandler, g_ObjectHandlers

// Globals of other units that this one reads.  Names in Ghidra style unless names_proposal.csv has a high row.
// GLOBAL: D3DREN 0x100561f0
extern uint32 g_CurObjectFrameCode;		// NAME: Jupiter g_CurObjectFrameCode (names_proposal high)
#include "d3dren/d3dstate.h"
// GLOBAL: D3DREN 0x10056770
extern MainWorld *g_pFrameMainWorld;			// guess: g_pMainWorld (names_proposal guess_g_pMainWorld, low)

// FUNCTION: D3DREN 0x10038830 _$E4
// FUNCTION: D3DREN 0x10038850 _$E2
// GLOBAL: D3DREN 0x100937a8
VisibleSet g_VisibleSet;

// FUNCTION: D3DREN 0x100389f0 ?Init@BaseObjectSet@@QAEHPAVVisibleSet@@PBD@Z
int BaseObjectSet::Init(VisibleSet *pVisibleSet, const char *pSetName)
{
	if (pVisibleSet)
	{
		dl_Insert(pVisibleSet->m_Sets.m_pPrev, &m_Link);
	}

	m_pSetName = pSetName;
	return 1;
}

// Cache the object's flags, compute its visibility/portal draw decision, then call the callback once when eligible.
// FUNCTION: D3DREN 0x10038a30 ?Draw@BaseObjectSet@@QAEXPAVViewParams@@P6AX0PAVLTObject@@@Z@Z
void BaseObjectSet::Draw(ViewParams *pParams, DrawObjectFn fn)
{
	uint32 i;
	LTObject *pObj;

	for (i = 0; i < m_nObjects; i++)
	{
		uint32 flags;
		int bDraw;
		pObj = m_pObjects[i];
		flags = pObj->m_Flags;
		if (flags & FLAG_VISIBLE)
			bDraw = !pParams->m_bPortalView || !(pObj->m_Flags2 & FLAG2_PORTALINVISIBLE);
		else
			bDraw = pParams->m_bPortalView && (flags & FLAG_PORTALVISIBLE);
		if (bDraw)
			fn(pParams, pObj);
	}
}

// FUNCTION: D3DREN 0x10038aa0 ?Init@AllocSet@@QAEHPAVVisibleSet@@PADK@Z
int AllocSet::Init(VisibleSet *pVisibleSet, char *pSetName, uint32 defaultMax)
{
	HLTPARAM hParam;
	uint32 maxNum;

	Term();

	if (!BaseObjectSet::Init(pVisibleSet, pSetName))
		return 0;

	hParam = g_pStruct->GetParameter(pSetName);
	if (hParam)
	{
		maxNum = (uint32)g_pStruct->GetParameterValueFloat(hParam);
	}
	else
	{
		maxNum = defaultMax;
	}

	m_pObjects = new LTObject*[maxNum];
	if (!m_pObjects)
		return 0;

	m_nMaxObjects = maxNum;
	return 1;
}

// FUNCTION: D3DREN 0x10038b40 ?Term@AllocSet@@QAEXXZ
void AllocSet::Term()
{
	if (m_pObjects)
	{
		delete [] m_pObjects;
		m_pObjects = LTNULL;
	}

	m_nMaxObjects = 0;
	m_nObjects = 0;
}

// FUNCTION: D3DREN 0x10038b60 ??0VisibleSet@@QAE@XZ
VisibleSet::VisibleSet()
{
	dl_TieOff(&m_Sets);
	m_Unk0c = 1;
	m_Unk10.SetCacheSize(0x200);
	m_Unk28.SetCacheSize(0x20);
	ClearSet();
}

// FUNCTION: D3DREN 0x10038cd0 ??0BaseObjectSet@@QAE@XZ

// FUNCTION: D3DREN 0x10038cf0 ?Init@VisibleSet@@QAEHXZ
int VisibleSet::Init()
{
	int bErr;

	bErr = 0;

	bErr |= !m_SolidModels.Init(this, "VS_MODELS", 128);
	bErr |= !m_TranslucentModels.Init(this, "VS_MODELS_TRANSLUCENT", 128);
	bErr |= !m_Unk184.Init(this, "VS_MODELS_CHROMAKEY", 128);

	bErr |= !m_TranslucentSprites.Init(this, "VS_SPRITES", 128);
	bErr |= !m_NoZSprites.Init(this, "VS_SPRITES_NOZ", 128);

	bErr |= !m_SolidWorldModels.Init(this, "VS_WORLDMODELS", 64);
	bErr |= !m_TranslucentWorldModels.Init(this, "VS_WORLDMODELS_TRANSLUCENT", 64);
	bErr |= !m_Unk224.Init(this, "VS_WORLDMODELS_CHROMAKEY", 64);

	bErr |= !m_Lights.Init(this, "VS_LIGHTS", 40);

	bErr |= !m_SolidPolyGrids.Init(this, "VS_POLYGRIDS", 32);
	bErr |= !m_TranslucentPolyGrids.Init(this, "VS_POLYGRIDS_TRANSLUCENT", 32);

	bErr |= !m_LineSystems.Init(this, "VS_LINESYSTEMS", 32);
	bErr |= !m_ParticleSystems.Init(this, "VS_PARTICLESYSTEMS", 64);

	bErr |= !m_SolidCanvases.Init(this, "VS_CANVASES", 32);
	bErr |= !m_TranslucentCanvases.Init(this, "VS_CANVASES_TRANSLUCENT", 32);

	if (bErr)
	{
		Term();
	}

	return !bErr;
}

void VisibleSet::Term()
{
	LTLink *pCur, *pNext;
	BaseObjectSet *pSet;

	for (pCur = m_Sets.m_pNext; pCur != &m_Sets; pCur = pNext)
	{
		pNext = pCur->m_pNext;
		pSet = (BaseObjectSet*)pCur->m_pData;
	}

	dl_TieOff(&m_Sets);
}

// FUNCTION: D3DREN 0x100390d0 ?ClearSet@VisibleSet@@QAEXXZ
void VisibleSet::ClearSet()
{
	LTLink *pCur;
	BaseObjectSet *pSet;

	m_nUnk24 = 0;
	m_nUnk3c = 0;
	m_nUnk140 = 0;
	for (pCur = m_Sets.m_pNext; pCur != &m_Sets; pCur = pCur->m_pNext)
	{
		pSet = (BaseObjectSet*)pCur->m_pData;
		pSet->ClearSet();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Node tagging.  The Talon engine queries the world tree (VisQueryRequest) and calls back into the renderer.
// ---------------------------------------------------------------------------------------------------------------------

// GLOBAL: D3DREN 0x10055cf4
extern int g_nVisibleLeaves;				// guess: g_nVisibleLeaves (names_proposal low): "Visible Leaves: %d"
// GLOBAL: D3DREN 0x10056688
extern int g_nWorldPolysProcessed;				// guess: g_nWorldPoliesProcessed (names_proposal low)
// GLOBAL: D3DREN 0x100584a4
extern int g_LockPVS;				// g_CV_LockPVS mirror (names_proposal medium, not in rendererconsolevars.h yet)
// GLOBAL: D3DREN 0x100584b8
extern int g_DrawAll;				// g_CV_DrawAll mirror (names_proposal medium)
// GLOBAL: D3DREN 0x100584dc
extern int g_DrawFlat;				// g_CV_DrawFlat mirror (names_proposal medium)
// GLOBAL: D3DREN 0x1005811c
extern int g_FixTJunc;				// g_CV_FixTJunc mirror (names_proposal medium)
// GLOBAL: D3DREN 0x1005ce18
extern int g_bPortalsEnabled;

// Raw views while the Talon layouts of these are not published (ViewParams: owner W1; WorldPoly::m_Pad46 and
// Leaf::m_Pad2C are padding in the shared headers and may only be renamed there, so they are read through casts).
#define WORLDPOLY_FRAMECODE(p)	(*(uint16*)((uint8*)(p) + 0x46))
#define LEAF_FRAMECODE(p)		(*(uint16*)((uint8*)(p) + 0x2c))
#define VIEW_CLIPPLANES		(g_ViewParams.m_ClipPlanes)	// the 6 view frustum planes {nx, ny, nz, dist}

// the objects of a leaf link (de_nodes LeafLink): the object pointer is at +0x18
struct UnkType_LeafLink
{
	uint8		m_Pad00[0x18];
	LTObject	*m_pObject;
};

// The test every tagging callback does before it hands an object to its handler (Talon d3d_ShouldProcessObject).
static inline int d3d_ShouldProcessObject(LTObject *pObject)
{
	return	g_ObjectHandlers[(char)pObject->m_ObjectType].m_ProcessObjectFn &&
			(char)pObject->m_ObjectType != OT_POLYGRID &&
			!(pObject->m_Flags2 & FLAG2_SKYOBJECT) &&
			(((pObject->m_Flags & FLAG_VISIBLE) && (!g_ViewParams.m_bPortalView || !(pObject->m_Flags2 & FLAG2_PORTALINVISIBLE))) ||
			 (!(pObject->m_Flags & FLAG_VISIBLE) && g_ViewParams.m_bPortalView && (pObject->m_Flags & FLAG_PORTALVISIBLE)));
}

// Process attachments (possibly recursively).
// FUNCTION: D3DREN 0x10039440
void d3d_ProcessAttachments(LTObject *pObject, uint32 depth)
{
	Attachment *pCur;
	LTObject *pAttachedObject;

	if (depth > 32)
		return;

	pCur = pObject->m_Attachments;
	while (pCur)
	{
		pAttachedObject = g_pStruct->ProcessAttachment(pObject, pCur);

		if (pAttachedObject && pAttachedObject->m_WTFrameCode != g_CurObjectFrameCode)
		{
			// Allow it to recursively process attachments.
			d3d_ProcessAttachments(pAttachedObject, depth + 1);

			if (d3d_ShouldProcessObject(pAttachedObject))
			{
				g_ObjectHandlers[(char)pAttachedObject->m_ObjectType].m_ProcessObjectFn(pAttachedObject);
				pAttachedObject->m_WTFrameCode = g_CurObjectFrameCode;
			}
		}

		pCur = pCur->m_pNext;
	}
}

// Appends to one of the two poly arrays of g_VisibleSet (an array that grows only when its used count reaches its size).
static inline void d3d_AddPoly(CMoArray<WorldPoly*> &array, uint32 &nUsed, WorldPoly *pPoly)
{
	if (nUsed >= array.GetSize())
	{
		if (array.Append(pPoly))
			nUsed++;
	}
	else
	{
		array[nUsed] = pPoly;
		nUsed++;
	}
}

// Appends a visible world poly to the lists of g_VisibleSet (called for every poly of a tagged leaf).
static inline void d3d_TagPoly(WorldPoly *pPoly)
{
	Surface *pSurface;
	VisibleSet::SortedPoly *pEntry;
	uint32 i;

	if (WORLDPOLY_FRAMECODE(pPoly) != g_CurFrameCode)
	{
		pSurface = (Surface*)pPoly->m_pSurface;
		if (!(pSurface->m_Flags & SURF_INVISIBLE))
		{
			if ((pSurface->m_Flags & 0x10) && !g_DrawFlat)
			{
				d3d_AddPoly(g_VisibleSet.m_Unk28, g_VisibleSet.m_nUnk3c, pPoly);
			}
			else if (pSurface->m_Unknown3A == 0x7fff)
			{
				d3d_AddPoly(g_VisibleSet.m_Unk10, g_VisibleSet.m_nUnk24, pPoly);
			}
			else if (g_bPortalsEnabled)
			{
				if (g_VisibleSet.m_nUnk140 < 32)
				{
					for (i = 0; i < g_VisibleSet.m_nUnk140; i++)
					{
						if (g_VisibleSet.m_Unk40[i].m_pPoly == pPoly)
							goto Tagged;
					}

					pEntry = &g_VisibleSet.m_Unk40[g_VisibleSet.m_nUnk140];
					pEntry->m_pPoly = pPoly;
					pEntry->m_pUnk = &g_ViewParams.m_mIdentity;
					g_VisibleSet.m_nUnk140++;
				}
			}
			else
			{
				d3d_AddPoly(g_VisibleSet.m_Unk10, g_VisibleSet.m_nUnk24, pPoly);
			}
		}
Tagged:
		WORLDPOLY_FRAMECODE(pPoly) = g_CurFrameCode;
	}
}

// Tags every object and poly of a visibility BSP (the world has a VisBSP and no portal view).
// FUNCTION: D3DREN 0x10039100
void ProcessAllBspObjectsAndPolys(WorldBsp *pBsp)
{
	uint32 i;
	Node *pNode;
	LTLink *pHead, *pCur;
	LTObject *pObject;
	WorldPoly *pPoly;

	for (i = 0; i < pBsp->m_nNodes; i++)
	{
		pNode = &pBsp->m_Nodes[i];
		pHead = (LTLink*)&pNode->m_Objects;
		for (pCur = pHead->m_pPrev; pCur != pHead; pCur = pCur->m_pPrev)
		{
			pObject = (LTObject*)pCur->m_pData;
			if (pObject->sd)
				break;

			if (d3d_ShouldProcessObject(pObject))
			{
				pObject->m_WTFrameCode = g_CurObjectFrameCode;
				g_ObjectHandlers[(char)pObject->m_ObjectType].m_ProcessObjectFn(pObject);

				if (pObject->m_Attachments)
				{
					d3d_ProcessAttachments(pObject, 0);
				}
			}
		}
	}

	for (i = 0; i < pBsp->m_nPolies; i++)
	{
		pPoly = pBsp->m_Polies[i];
		d3d_TagPoly(pPoly);
	}
}

// The default callbacks of a vis query (VisQueryRequest's constructor stores them): d3d.ren has its own copies.
// Called by d3d_TagVisibleLeaves only through the constructor; defined in another unit.

// 0x1000f458 (common_draw): the leaf visibility test of a vis query (VisQueryRequest::m_Unknown24).
LTBOOL d3d_IsPortalInsideViewFrustum(BspPortal *pPortal);
// 0x100185a0 (d3d_draw, W2): applies one light anim to a poly's lightmap data.
int d3d_AddLightAnimVertexColors(void *pPolyData, uint32 nPolyData, LightAnim *pAnim, uint32 *pRef);
// the draw callbacks the poly draw loop calls (function pointers set by the draw units)
typedef void (*PFN_DrawWorldPoly)(WorldPoly *pPoly);
// GLOBAL: D3DREN 0x10058cd8
extern PFN_DrawWorldPoly g_pfnDrawUntexturedWorldPoly;
// GLOBAL: D3DREN 0x100587e8
extern PFN_DrawWorldPoly g_pfnDrawTexturedWorldPoly;
// GLOBAL: D3DREN 0x1005a304
extern PFN_DrawWorldPoly g_pfnDrawPanningSkyWorldPoly;
// GLOBAL: D3DREN 0x10058c24
extern PFN_DrawWorldPoly g_pfnDrawLightmappedWorldPoly;
void d3d_DrawSky();		// 0x1002d4c0 (unit unk/1002d080, W6; named there from names_proposal.csv, see drawsky.h)
void ApplyVisibleDynamicLights();		// 0x10023cb0 (unit unk/10023860, W8)

// Called from the render-scene function: picks the VisBSP path (1) unless the world has a VisBSP and DrawAll is off.
void d3d_TagVisibleLeaves(int bUseVisBSP);
// FUNCTION: D3DREN 0x10039510
void TagWorldVisibility()
{
	if ((g_pFrameMainWorld->m_WorldFlags & WORLD_HASVISBSP) && !g_DrawAll)
		d3d_TagVisibleLeaves(0);
	else
		d3d_TagVisibleLeaves(1);
}

// The vis query callbacks (defined below, after this function: address order).
void d3d_CheckAndProcessObject(LTObject *pObject);
void CollectProcessableObjects(LTLink *pHead, LTObject ***ppObjects, uint32 *pnObjects);
void ProcessVisibleLeaf(Leaf *pLeaf);
LTBOOL d3d_IsWorldNodeVisible(WorldTreeNode *pNode);

// Tags the visible leaves (world tree query or VisBSP walk), queues the objects and draws the tagged world polys.
// bUseVisBSP = 0: the Talon world tree query (callbacks below); else walk the VisBSP (ProcessAllBspObjectsAndPolys).
// FUNCTION: D3DREN 0x10039540
void d3d_TagVisibleLeaves(int bUseVisBSP)
{
	VisQueryRequest request;
	WorldBsp *pBsp;
	WorldPoly **ppPoly;
	uint32 nPolys, i, j, nVerts;
	WorldPoly *pPoly;
	Surface *pSurface;
	uint32 surfFlags;
	void *pVerts;
	LTVector vViewPos, vCenter;
	float fDist;
	LTVector *pViewPos;
	MainWorld *pWorld;
	LTPlane *pPlane;

	if ((g_VisibleSet.m_Unk0c & 1) && !g_LockPVS)
	{
		g_VisibleSet.ClearSet();

		if (bUseVisBSP)
		{
			pBsp = g_pFrameMainWorld->GetVisBSP();
			if (pBsp)
			{
				ProcessAllBspObjectsAndPolys(pBsp);
			}
			else
			{
				dsi_ConsolePrint("ERROR: GetVisBSP returned LTNULL!");
			}
		}
		else
		{
			CountAdder cntAdd(&g_pStruct->m_Ticks_TagVisibleLeaves);

			request.m_iObjArray = NOA_Objects;
			pViewPos = g_ViewParams.m_bPortalView ? (LTVector*)((uint8*)&g_ViewParams + 0x4d8) : &g_ViewParams.m_Pos;
			request.m_Viewpoint = *pViewPos;
			request.m_ViewRadius = 10000.0f;
			request.m_AddObject = (VQAddObjectFn)d3d_CheckAndProcessObject;
			request.m_Unknown18 = (void*)CollectProcessableObjects;
			request.m_pUserData = LTNULL;
			request.m_Unknown20 = (void*)ProcessVisibleLeaf;
			request.m_Unknown24 = (void*)d3d_IsPortalInsideViewFrustum;
			request.m_NodeFilterFn = d3d_IsWorldNodeVisible;
			g_pFrameMainWorld->m_WorldTree.DoVisQuery(&request);
		}
	}

	d3d_DrawSky();
	ApplyVisibleDynamicLights();

	if (g_DrawWorld)
	{
		ppPoly = g_VisibleSet.m_Unk10.GetArray();
		if (g_VisibleSet.m_nUnk24)
		{
			nPolys = g_VisibleSet.m_nUnk24;
			do
			{
				pPoly = *ppPoly;
				if (pPoly->m_pPlane->DistTo(g_ViewParams.m_Pos) > 0.01f)
				{
					g_ClipFlags = 0x3f;
					pPlane = VIEW_CLIPPLANES;
					for (i = 0; i < 6; pPlane++, i++)
					{
						fDist = pPlane->DistTo(pPoly->m_Center);
						if (fDist < -pPoly->m_Radius)
							goto NextPoly;

						if (pPoly->m_Radius < fDist)
							g_ClipFlags &= ~(1 << i);
					}

					g_nWorldPolysProcessed++;

					pSurface = (Surface*)pPoly->m_pSurface;
					surfFlags = pSurface->m_Flags;
					if (!(surfFlags & SURF_INVISIBLE))
					{
						if (pSurface->m_pTexture)
						{
							if (surfFlags & 0x80)
							{
								g_pfnDrawLightmappedWorldPoly(pPoly);
							}
							else
							{
								if (pPoly->m_Flags & 0x4000)
								{
									pWorld = g_pFrameMainWorld;
									if (g_FixTJunc)
									{
										pVerts = pPoly->m_pVertices;
										nVerts = pPoly->m_nExtraVertices;
									}
									else
									{
										pVerts = pPoly->m_Vertices;
										nVerts = pPoly->m_nVertices;
									}

									for (j = 0; j < nVerts; j++)
										*(uint32*)((uint8*)pVerts + j * 0x18 + 0x14) = 0;

									for (j = 0; j < pPoly->m_nLMAnimRefs; j++)
									{
										uint32 iAnim = (uint16)pPoly->m_pLMAnimRefs[j];
										if (iAnim < pWorld->m_LightAnims.GetSize())
										{
											LightAnim *pAnim = &pWorld->m_LightAnims[iAnim];
											if (pAnim->m_iFrames[0] != 0xffffffff && pAnim->m_fBlendPercent >= 0.02f)
												d3d_AddLightAnimVertexColors(pVerts, nVerts, pAnim, &pPoly->m_pLMAnimRefs[j]);
										}
									}

									pPoly->m_Flags &= 0xbfff;
								}

								if (surfFlags & 0x8000)
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
NextPoly:
				ppPoly++;
				nPolys--;
			} while (nPolys);
		}
	}
}

// VQAddObjectFn: an object the world tree query found.
// FUNCTION: D3DREN 0x10039900
void d3d_CheckAndProcessObject(LTObject *pObject)
{
	if (!(pObject->m_InternalFlags & 0x400))
	{
		if (d3d_ShouldProcessObject(pObject))
		{
			g_ObjectHandlers[(char)pObject->m_ObjectType].m_ProcessObjectFn(pObject);

			if (pObject->m_Attachments)
			{
				d3d_ProcessAttachments(pObject, 0);
			}
		}
	}
}

// VisQueryRequest::m_Unknown18: the objects of a node's list.
// FUNCTION: D3DREN 0x10039a40
void CollectProcessableObjects(LTLink *pHead, LTObject ***ppObjects, uint32 *pnObjects)
{
	LTLink *pCur;
	LTObject *pObject;

	pCur = pHead->m_pPrev;
	while (pCur != pHead && !(pObject = (LTObject*)pCur->m_pData)->sd)
	{
		if (d3d_ShouldProcessObject(pObject))
		{
			(*ppObjects)[*pnObjects] = pObject;
			(*pnObjects)++;
		}

		pCur = pCur->m_pPrev;
	}
}

// VisQueryRequest::m_Unknown20: a leaf the query reached.
// FUNCTION: D3DREN 0x10039ae0
void ProcessVisibleLeaf(Leaf *pLeaf)
{
	LTLink *pCur;
	LTObject *pObject;
	WorldPoly **ppPoly, **ppPolyEnd;

	LEAF_FRAMECODE(pLeaf) = g_CurFrameCode;
	g_nVisibleLeaves++;

	for (pCur = pLeaf->m_LeafLinks.m_pNext; pCur != (LTLink*)&pLeaf->m_LeafLinks; pCur = pCur->m_pNext)
	{
		pObject = ((UnkType_LeafLink*)pCur->m_pData)->m_pObject;

		if ((char)pObject->m_ObjectType == OT_POLYGRID && (pObject->m_Flags & FLAG_VISIBLE) && pObject->m_WTFrameCode != g_CurObjectFrameCode)
		{
			pObject->m_WTFrameCode = g_CurObjectFrameCode;
			g_ObjectHandlers[OT_POLYGRID].m_ProcessObjectFn(pObject);

			if (pObject->m_Attachments)
			{
				d3d_ProcessAttachments(pObject, 0);
			}
		}
	}

	ppPoly = pLeaf->m_Polies;
	ppPolyEnd = ppPoly + pLeaf->m_nPolies;
	for (; ppPolyEnd != ppPoly; ppPoly++)
	{
		d3d_TagPoly(*ppPoly);
	}
}

// VisQueryRequest::m_NodeFilterFn: is a world tree node (its bounding sphere) inside the view frustum?
// FUNCTION: D3DREN 0x10039d90
LTBOOL d3d_IsWorldNodeVisible(WorldTreeNode *pNode)
{
	LTVector vCenter;
	float fRadius;
	int i;

	vCenter = pNode->m_Center;
	fRadius = -pNode->m_Radius;

	for (i = 0; i < 6; i++)
	{
		if (VEC_DOT(vCenter, VIEW_CLIPPLANES[i].m_Normal) - VIEW_CLIPPLANES[i].m_Dist < fRadius)
			return LTFALSE;
	}

	return LTTRUE;
}

// FUNCTION: D3DREN 0x10039e00
VisibleSet *d3d_GetVisibleSet()
{
	return &g_VisibleSet;
}

// ---------------------------------------------------------------------------------------------------------------------
// CMoArray<WorldPoly*, DefaultCache> (VisibleSet::m_Unk10 / m_Unk28): the template code emitted with this object, in the
// order the compiler emits it.  The exe has no copies of GenRemoveAll / GenGetSize here (identical-code folding merged
// them into other CMoArray instances elsewhere), GenBegin / GenIsValid / GenSetCacheSize / Clear are shared with the other
// instances of the 4-byte T family.
// ---------------------------------------------------------------------------------------------------------------------
// The exe builds the returned GenListPos {m_Index = 0, m_SubIndex uninitialised} in a local temporary and copies it to the hidden
// return slot (32 bytes): the plain SDK CMoArray::GenBegin of the original (RTM) front end.
// FUNCTION: D3DREN 0x10039e10 ?GenBegin@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBE?AVGenListPos@@XZ
// FUNCTION: D3DREN 0x10039e30 ?GenIsValid@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBEHABVGenListPos@@@Z
// FUNCTION: D3DREN 0x10039e50 ?GenGetNext@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBEPAUWorldPoly@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x10039e70 ?GenGetAt@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBEPAUWorldPoly@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x10039e80 ?GenAppend@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEHAAPAUWorldPoly@@@Z
// FUNCTION: D3DREN 0x10039f90 ?GenRemoveAt@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1003a0a0 ?GenCopyList@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEHABV?$GenList@PAUWorldPoly@@@@@Z
// FUNCTION: D3DREN 0x1003a1c0 ?GenAppendList@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEHABV?$GenList@PAUWorldPoly@@@@@Z
// FUNCTION: D3DREN 0x1003a290 ?GenFindElement@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UBEHABQAUWorldPoly@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1003a2c0 ?GenSetCacheSize@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@UAEXK@Z
// FUNCTION: D3DREN 0x1003a2d0 ?Clear@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@AAEXXZ
// FUNCTION: D3DREN 0x1003a2e0 ?Init@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1003a3a0 ?SetSize2@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003a430 ?InternalNiceSetSize@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003a530 ?Insert2@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@QAEHKABQAUWorldPoly@@PAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003a640 ?BaseDelete@@YAXPAVLAlloc@@PAPAUWorldPoly@@K@Z
// FUNCTION: D3DREN 0x1003a660 ?BaseNew@@YAPAPAUWorldPoly@@PAVLAlloc@@PAPAU1@K@Z
