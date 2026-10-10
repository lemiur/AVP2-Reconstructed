// Talon's BSP visibility query (not in Jupiter): walks the vis leaf's lists and collects the objects the
// callbacks of the VisQueryInfo get to see. Names are ours.
#include "bdefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "world_tree.h"
#include "visquery.h"

#define VIS_EPSILON	0.1f

VisQueryInfo *g_pCurVisQuery;
// The bit position in the current leaf list's vis bits.
// GLOBAL: LITHTECH 0x004e6274
static uint32 g_VisBitPos;
// GLOBAL: LITHTECH 0x004e6278
static uint8 *g_pVisBits;
// The end of the object buffer on the stack.
// GLOBAL: LITHTECH 0x004e627c
static LTObject **g_pVisBufEnd;

// Scratch globals of the traversal.
// GLOBAL: LITHTECH 0x004e6ddc
static Leaf *g_pVisLeaf;
// GLOBAL: LITHTECH 0x004e6de0
static LTObject **g_ppVisObj;
// GLOBAL: LITHTECH 0x004e6de4
static int g_nVisClipped;
// GLOBAL: LITHTECH 0x004e6de8
static LTObject **g_ppVisClipped;

typedef void (*VQGetObjectsFn)(LTLink *pListHead, LTObject ***pppObjects, int *pnObjects);
typedef void (*VQLeafFn)(Leaf *pLeaf, void *pUser);


// FUNCTION: LITHTECH 0x0049e6b0
static void vq_DefaultGetObjects(LTLink *pListHead, LTObject ***pppObjects, int *pnObjects)
{
	LTLink *pCur;
	LTObject *pObject;

	for(pCur=pListHead->m_pPrev; pCur != pListHead; pCur=pCur->m_pPrev)
	{
		pObject = (LTObject*)pCur->m_pData;

		if(*pppObjects < g_pVisBufEnd && pObject->m_WTFrameCode != g_pCurVisQuery->m_FrameCode)
		{
			(*pppObjects)[*pnObjects] = pObject;
			(*pnObjects)++;
		}
	}
}

// FUNCTION: LITHTECH 0x0049e700
inline uint8 vq_ReadVisBit()
{
	uint8 bit;

	bit = (uint8)((1 << (g_VisBitPos & 7)) & g_pVisBits[g_VisBitPos >> 3]);
	g_VisBitPos++;
	return bit;
}

// FUNCTION: LITHTECH 0x0049e730
inline LTObject** vq_ClipObjectsBack(LTPlane *pPlane, LTObject **ppObjects, int nObjects, int *pnOut, int bAppend)
{
	LTObject **ppOut;
	LTObject *pObject;
	LTVector pos;

	ppOut = ppObjects;
	if(bAppend)
		ppOut = ppObjects + nObjects;

	*pnOut = 0;
	if(ppOut + nObjects >= g_pVisBufEnd || nObjects == 0)
		return ppOut;

	for(; nObjects; nObjects--, ppObjects++)
	{
		pObject = *ppObjects;
		if(pObject->m_WTFrameCode != g_pCurVisQuery->m_FrameCode)
		{
			pos = pObject->m_Pos;
			if(pPlane->DistTo(pos) < pObject->m_Radius + VIS_EPSILON)
			{
				ppOut[*pnOut] = pObject;
				(*pnOut)++;
			}
		}
	}

	return ppOut;
}

// FUNCTION: LITHTECH 0x0049e800
inline LTObject** vq_ClipObjectsFront(LTPlane *pPlane, LTObject **ppObjects, int nObjects, int *pnOut)
{
	LTObject **ppOut;
	LTObject *pObject;
	LTVector pos;

	ppOut = ppObjects;
	*pnOut = 0;
	if(ppObjects + nObjects >= g_pVisBufEnd)
		return ppOut;

	for(; nObjects; nObjects--, ppObjects++)
	{
		pObject = *ppObjects;
		if(pObject->m_WTFrameCode != g_pCurVisQuery->m_FrameCode)
		{
			pos = pObject->m_Pos;
			if(pPlane->DistTo(pos) > -(pObject->m_Radius + VIS_EPSILON))
			{
				ppOut[*pnOut] = pObject;
				(*pnOut)++;
			}
		}
	}

	return ppOut;
}


// Visits a BSP node: collects the objects of the node, hands the leaf and the objects to the callbacks when the
// vis bits say so and goes down the sides with the objects clipped to them.
// FUNCTION: LITHTECH 0x0049e8d0
inline void vq_VisitNode(Node *pNode, LTObject **ppObjects, int nObjects)
{
	WorldBsp *pBsp;
	Leaf *pLeaf;
	int bBackVis;
	int n;
	LTObject *pObject;

	for(;;)
	{
		((VQGetObjectsFn)g_pCurVisQuery->m_Unknown10)((LTLink*)&pNode->m_Objects, &ppObjects, &nObjects);

		if(pNode->m_iLeaf != 0xFFFF && vq_ReadVisBit())
		{
			pBsp = g_pCurVisQuery->m_pBsp;
			if(pNode->m_iLeaf < pBsp->m_nLeafs)
				pLeaf = &pBsp->m_Leafs[pNode->m_iLeaf];
			else
				pLeaf = LTNULL;

			g_pVisLeaf = pLeaf;
			if(pLeaf)
				((VQLeafFn)g_pCurVisQuery->m_Unknown0C)(pLeaf, g_pCurVisQuery->m_pUserData);

			g_ppVisObj = ppObjects;
			while(nObjects)
			{
				nObjects--;
				pObject = *g_ppVisObj;
				if(pObject->m_WTFrameCode != g_pCurVisQuery->m_FrameCode)
				{
					pObject->m_WTFrameCode = g_pCurVisQuery->m_FrameCode;
					g_pCurVisQuery->m_AddObject(*g_ppVisObj, g_pCurVisQuery->m_pUserData);
				}

				g_ppVisObj++;
			}

			nObjects = 0;
		}

		bBackVis = pNode->m_Sides[1]->m_Flags & 8;
		if((pNode->m_Sides[0]->m_Flags & 8) && vq_ReadVisBit())
		{
			g_ppVisClipped = vq_ClipObjectsBack(pNode->GetPlane(), ppObjects, nObjects, &g_nVisClipped, bBackVis);
			if(bBackVis)
			{
				vq_VisitNode(pNode->m_Sides[0], g_ppVisClipped, g_nVisClipped);
			}
			else
			{
				ppObjects = g_ppVisClipped;
				nObjects = g_nVisClipped;
				pNode = pNode->m_Sides[0];
				continue;
			}
		}

		if(bBackVis && vq_ReadVisBit())
		{
			ppObjects = vq_ClipObjectsFront(pNode->GetPlane(), ppObjects, nObjects, &nObjects);
			pNode = pNode->m_Sides[1];
			continue;
		}

		return;
	}
}






// Three accessor calls follow the VisitNode call in the original (they use inline budget); they emit no code.
inline void vq_Accessor() {}

typedef LTBOOL (*VQPortalFn)(BspPortal *pPortal);
typedef void (*VQObjectFn)(LTObject *pObject, void *pUser);



// Runs a visibility query through the BSP: from the viewpoint's leaf along the vis bits of its lists, or over
// every node when the viewpoint isn't in a leaf (WorldBsp::WBSlot11).
// FUNCTION: LITHTECH 0x0049e330
void wb_Unknown49e330(void *p)
{
	LTObject *objBuf[5000];
	VisQueryInfo *pQuery;
	Leaf *pLeaf;
	LeafList *pList;
	BspPortal *pPortal;
	LTObject **ppObjs;
	int nObjs;
	LTObject **ppList;
	int nList;
	uint32 iList, iNode;
	int i;

	pQuery = (VisQueryInfo*)p;
	g_pVisBufEnd = &objBuf[5000];
	if(!pQuery->m_Unknown10)
		pQuery->m_Unknown10 = (void*)vq_DefaultGetObjects;
	g_pCurVisQuery = pQuery;

	if(pQuery->m_pLeaf && pQuery->m_pLeaf->m_nLeafLists > 0)
	{
		for(iList=0; iList < pQuery->m_pLeaf->m_nLeafLists; iList++)
		{
			pList = &pQuery->m_pLeaf->m_LeafLists[iList];
			if(iList != 0)
			{
				pPortal = &pQuery->m_pBsp->m_Portals[pList->m_PortalID];
				if(!(pPortal->m_Flags & 1))
					continue;

				if(!((VQPortalFn)pQuery->m_Unknown14)(pPortal))
					continue;
			}

			g_pVisBits = pList->m_pList;
			g_VisBitPos = 0;

			ppObjs = objBuf;
			nObjs = 0;
			vq_VisitNode(pQuery->m_pBsp->m_RootNode, ppObjs, nObjs);
			vq_Accessor();
			vq_Accessor();
			vq_Accessor();

		}
	}
	else
	{
		for(iNode=0; iNode < pQuery->m_pBsp->m_nNodes; iNode++)
		{
			Node *pCurNode = &pQuery->m_pBsp->m_Nodes[iNode];

			ppList = objBuf;
			nList = 0;

			if(pCurNode->m_iLeaf != 0xFFFF)
			{
				if(pCurNode->m_iLeaf < pQuery->m_pBsp->m_nLeafs)
					pLeaf = &pQuery->m_pBsp->m_Leafs[pCurNode->m_iLeaf];
				else
					pLeaf = LTNULL;

				if(pLeaf)
					((VQLeafFn)pQuery->m_Unknown0C)(pLeaf, pQuery->m_pUserData);
			}

			((VQGetObjectsFn)pQuery->m_Unknown10)((LTLink*)&pCurNode->m_Objects, &ppList, &nList);
			for(i=0; i < nList; i++)
				pQuery->m_AddObject(ppList[i], pQuery->m_pUserData);
		}
	}
}

// The default visibility query callbacks (world_tree.h).
// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004a4ad0 (the empty destructors).
void vq_DefaultFn1()
{
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004a4ad0 (the empty destructors).
void vq_DefaultFn2()
{
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004668a0 (LTObject::IsMoveable).
LTBOOL vq_DefaultBoolFn()
{
	return LTTRUE;
}

