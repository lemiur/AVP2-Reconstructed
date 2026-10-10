// d3d.ren sys/d3d/drawobjects (0x100285a0-0x10029660): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// unk/100285a0 (0x100285a0-0x10029660): object-drawing dispatch (Jupiter drawobjects.cpp): SortProfile/DrawSorted console
// variables, the g_ObjectHandlers loops, d3d_FlushObjectQueues, ObjectDrawList and its CMoArray<ObjectDrawer> copies.
// FLAGS: /O2 /Ob2
#include "d3dren/rendererconsolevars.h"
#include "d3dren/common_stuff.h"
#include "d3dren/viewparams.h"
#include "d3dren/drawobjects.h"
#include "d3dren/scenedesc.h"
#include "counter.h"

// FUNCTION: D3DREN 0x100285a0 _$E2
// GLOBAL: D3DREN 0x1006bd38
ConVar g_CV_SortProfile("SortProfile", 0.0f);
// FUNCTION: D3DREN 0x100285c0 _$E5
// GLOBAL: D3DREN 0x1006b910
ConVar g_CV_DrawSorted("DrawSorted", 1.0f);

// GLOBAL: D3DREN 0x1006b934
ObjectDrawList *g_pTranslucentObjectDrawList;

// Handlers of other units (module init/term, per-frame and per-object functions).  Ghidra names; the `guess_` names of
// names_proposal.csv are not used (no evidence).
extern void d3d_ModelModuleInit();					// OT_MODEL ModuleInit
extern void thunk_FUN_10001340();			// OT_MODEL ModuleTerm (5-byte jmp thunk)
extern void d3d_ModelPreFrame();
extern void d3d_ProcessModel(LTObject *pObject);
extern void d3d_NullPreFrameCallback();					// empty PreFrame function shared by four object types
extern void d3d_ProcessWorldModel(LTObject *pObject);
extern void d3d_ProcessSprite(LTObject *pObject);
extern void d3d_ProcessLight(LTObject *pObject);
extern void d3d_InitParticleQuadIndices();					// OT_PARTICLESYSTEM PreFrame
extern void d3d_ProcessParticles(LTObject *pObject);
extern void d3d_InitPolyGridBuffers();					// OT_POLYGRID ModuleInit
extern void d3d_TermPolyGridDraw();			// OT_POLYGRID ModuleTerm
extern void d3d_ProcessPolyGrid(LTObject *pObject);
extern void d3d_ProcessLineSystem(LTObject *pObject);
extern void d3d_NullCallback();					// OT_CANVAS PreFrame
extern void d3d_ProcessCanvas(LTObject *pObject);

// GLOBAL: D3DREN 0x1004ba78
ObjectHandler g_ObjectHandlers[11] =
{
	// OT_NORMAL
	{ 0, 0, 0, 0, 0 },
	// OT_MODEL
	{ d3d_ModelModuleInit, thunk_FUN_10001340, 0, d3d_ModelPreFrame, d3d_ProcessModel },
	// OT_WORLDMODEL
	{ 0, 0, 0, d3d_NullPreFrameCallback, d3d_ProcessWorldModel },
	// OT_SPRITE
	{ 0, 0, 0, d3d_NullPreFrameCallback, d3d_ProcessSprite },
	// OT_LIGHT
	{ 0, 0, 0, 0, d3d_ProcessLight },
	// OT_CAMERA
	{ 0, 0, 0, 0, 0 },
	// OT_PARTICLESYSTEM
	{ 0, 0, 0, d3d_InitParticleQuadIndices, d3d_ProcessParticles },
	// OT_POLYGRID
	{ d3d_InitPolyGridBuffers, d3d_TermPolyGridDraw, 0, d3d_NullPreFrameCallback, d3d_ProcessPolyGrid },
	// OT_LINESYSTEM
	{ 0, 0, 0, d3d_NullPreFrameCallback, d3d_ProcessLineSystem },
	// OT_CONTAINER (containers drawn like WorldModels)
	{ 0, 0, 0, 0, d3d_ProcessWorldModel },
	// OT_CANVAS
	{ 0, 0, 0, d3d_NullCallback, d3d_ProcessCanvas },
};

// FUNCTION: D3DREN 0x100285e0
void d3d_InitObjectModules()
{
	int i;

	for (i = 0; i < 11; i++)
	{
		if (g_ObjectHandlers[i].m_ModuleInit)
		{
			g_ObjectHandlers[i].m_ModuleInit();
		}

		g_ObjectHandlers[i].m_bModuleInitted = 1;
	}
}

// FUNCTION: D3DREN 0x10028610
void d3d_TermObjectModules()
{
	int i;

	for (i = 0; i < 11; i++)
	{
		if (g_ObjectHandlers[i].m_ModuleTerm && g_ObjectHandlers[i].m_bModuleInitted)
		{
			g_ObjectHandlers[i].m_ModuleTerm();
		}

		g_ObjectHandlers[i].m_bModuleInitted = 0;
	}
}

// FUNCTION: D3DREN 0x10028640
void d3d_InitObjectQueues()
{
	int i;

	for (i = 0; i < 11; i++)
	{
		if (g_ObjectHandlers[i].m_PreFrameFn)
		{
			g_ObjectHandlers[i].m_PreFrameFn();
		}
	}
}

// Draw phases and queue functions of other units (all take no arguments: they work on g_ViewParams and the VisibleSet).
// NAME: the names of the medium rows of names_proposal.csv (Jupiter's drawing function names; Ghidra still has FUN_).
extern void d3d_DrawSolidWorldModels();				// d3d_DrawSolidWorldModels, 0x1002fa50
extern void d3d_DrawSolidModels();			// 0x10024d22
extern void d3d_DrawSolidPolyGrids();		// 0x1002cce0
extern void d3d_DrawSolidCanvases();		// 0x10022ae3
extern void d3d_BeginChromaKeyPolyPass();					// 0x10013e00 (guess: begin alpha test pass)
extern void d3d_DrawChromaKeyWorldModels();					// 0x1002fed0 (guess: draws the chromakey world models)
extern void d3d_DrawChromaKeyModels();					// 0x10024dc0 (guess: draws the chromakey models)
extern void d3d_EndChromaKeyPolyPass();					// 0x10013e40 (guess: end alpha test pass)
extern void d3d_SetTranslucentObjectStates(int);	// 0x10013ba0
extern void d3d_QueueTranslucentParticles();	// 0x100298b6
extern void d3d_QueueTranslucentPolyGrids();	// 0x1002cdc0
extern void d3d_QueueLineSystems();				// 0x10023d30
extern void d3d_QueueTranslucentWorldModels();				// d3d_QueueTranslucentWorldModels, 0x1002ff00
extern void d3d_QueueTranslucentModels();		// 0x10024deb
extern void d3d_QueueTranslucentCanvases();		// 0x10022b15
extern void d3d_QueueTranslucentSprites();				// d3d_QueueTranslucentSprites, 0x1002ea40
extern void d3d_DrawNoZSprites();				// 0x1002ea90
extern void d3d_UnsetTranslucentObjectStates(int bChangeZ);	// 0x10013df0

// GLOBAL: D3DREN 0x1006cd70
void (*g_pfnDrawVisibleReflections)();
// The native draw loop reuses one DWORD flags value for its visible and portal-visible tests.
// Keep the accessor/getter inline sites while reading the same fully constructed LTObject member.
static inline uint32 GetDrawerObjectFlags(const ObjectDrawer &d)
{
	return d.m_pObject->m_Flags;
}
// The exe's loop is the portal guard (reading s_TransObjList[i].m_pObject->m_Flags2), then a fresh read of the object pointer
// whose flags DWORD serves both tests and which is passed to the callback (`LTObject *pObject = s_TransObjList[i].m_pObject;
// uint32 flags = pObject->m_Flags;` inside the guard).  That spelling reproduces the loop instruction for instruction, but VC6 then
// keeps the constant 0 in ebx for the whole function (355 differing bytes), which the exe does not; the source below (flags read
// before the guard) avoids the cached zero and differs only in the loop (ALIGNED 22/22).  Checked without effect: local copies
// and references of m_bPortalView, int/LTBOOL flags, `!` spellings, nested ifs, `continue`, a drawer reference, declaration
// placement at loop or function scope, an inline ObjectDrawList draw helper (864 bytes), ftype probes (one int-0 store), and
// two permuter runs (the cached zero only goes with dead stores or reordered calls).
// PARKED: register wall: the exe's in-guard object read makes VC6 cache 0 in ebx for the whole function; no source spelling found
// STUB: D3DREN 0x10028660
void d3d_FlushObjectQueues()
{
	g_pStruct->Unknown24();

	{
		CountAdder cntAdd(&g_pStruct->m_Ticks_WorldModels);
		{
			CountAdder cntAdd2(g_pSceneDesc->m_pTicks_Render_WorldModels);
			d3d_DrawSolidWorldModels();
			g_pStruct->Unknown24();
		}
	}

	{
		CountAdder cntAdd(&g_pStruct->m_Ticks_Models);
		d3d_DrawSolidModels();
		g_pStruct->Unknown24();
	}

	d3d_DrawSolidPolyGrids();
	g_pStruct->Unknown24();
	d3d_DrawSolidCanvases();
	d3d_BeginChromaKeyPolyPass();
	d3d_DrawChromaKeyWorldModels();
	d3d_DrawChromaKeyModels();
	d3d_EndChromaKeyPolyPass();
	if (g_pfnDrawVisibleReflections)
		g_pfnDrawVisibleReflections();

	{
		CountAdder cntAdd(&g_pStruct->m_Ticks_Translucent);

		// this is static to prevent having to do allocations per frame
		static ObjectDrawList s_TransObjList;

		uint32 nObjects = d3d_GetVisibleSet()->m_TranslucentCanvases.m_nObjects +
			d3d_GetVisibleSet()->m_ParticleSystems.m_nObjects +
			d3d_GetVisibleSet()->m_LineSystems.m_nObjects +
			d3d_GetVisibleSet()->m_TranslucentPolyGrids.m_nObjects +
			d3d_GetVisibleSet()->m_TranslucentWorldModels.m_nObjects +
			d3d_GetVisibleSet()->m_TranslucentSprites.m_nObjects +
			d3d_GetVisibleSet()->m_TranslucentModels.m_nObjects;
		if (nObjects > s_TransObjList.GetSize())
			s_TransObjList.SetSize(nObjects);

		d3d_SetTranslucentObjectStates(0);
		d3d_QueueTranslucentParticles();
		g_pStruct->Unknown24();
		d3d_QueueTranslucentPolyGrids();
		g_pStruct->Unknown24();
		d3d_QueueLineSystems();
		g_pStruct->Unknown24();
		d3d_QueueTranslucentWorldModels();
		g_pStruct->Unknown24();
		d3d_QueueTranslucentModels();
		g_pStruct->Unknown24();
		d3d_QueueTranslucentCanvases();
		d3d_QueueTranslucentSprites();

		if (s_TransObjList.m_Unk14)
		{
			uint32 i;

			s_TransObjList.SortByViewDistance(&g_ViewParams);
			for (i = 0; i < s_TransObjList.m_Unk14; i++)
			{
				// This same-width read is inside the live object, also when the portal guard skips drawing.
				uint32 flags = GetDrawerObjectFlags(s_TransObjList[i]);
				if (g_ViewParams.m_bPortalView == 0 || (s_TransObjList[i].m_pObject->m_Flags2 & FLAG2_PORTALINVISIBLE) == 0)
				{
					if ((flags & FLAG_VISIBLE) ||
						(g_ViewParams.m_bPortalView != 0 && (flags & FLAG_PORTALVISIBLE)))
					{
						ObjectDrawer &drawer = s_TransObjList[i];
						drawer.m_pDrawFn(&g_ViewParams, drawer.m_pObject);
					}
				}
			}
		}

		s_TransObjList.m_Unk14 = 0;
		d3d_DrawNoZSprites();
		d3d_UnsetTranslucentObjectStates(1);
	}
}

// Shell-sort style gap tables of SortByViewDistance: gap = 1 << shift, from 512 down to 1.
// GLOBAL: D3DREN 0x1004bb54
int g_ObjectDrawSortGaps[10] = { 0x200, 0x100, 0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1 };
// GLOBAL: D3DREN 0x1004bb7c
int g_ObjectDrawSortGapShifts[10] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };

// The atexit target of the function static s_TransObjList (destroys the CMoArray<ObjectDrawer> base).
// FUNCTION: D3DREN 0x10028960 _$E8

// NAME: guess_ObjectDrawList_Sort (low): no evidence for a name, Ghidra's SortByViewDistance kept as a member of ObjectDrawList
// Jupiter drawobjects.cpp ObjectDrawList::CalcDistance (private static member there; the exe has no separate copy, it is
// expanded into the sort function)
float ObjectDrawList::CalcDistance(const LTObject *pObject, const ViewParams &Params)
{
	if ((pObject->m_Flags & FLAG_REALLYCLOSE) == 0)
		return (pObject->GetPos() - Params.m_Pos).MagSqr();
	else
		return pObject->GetPos().MagSqr();
}

// Native +0xb3 and +0xda store the distance in each branch. This helper retains those stores
// and the out-of-line vector constructor while preserving the CalcDistance formulas.
static inline void StoreObjectDrawerViewDistance(ObjectDrawer *pDrawer, const ViewParams &Params)
{
	if ((pDrawer->m_pObject->m_Flags & FLAG_REALLYCLOSE) == 0)
		pDrawer->m_fDistance = (pDrawer->m_pObject->GetPos() - Params.m_Pos).MagSqr();
	else
		pDrawer->m_fDistance = pDrawer->m_pObject->GetPos().MagSqr();
}

// Native 0x10028aa2 caches the array after distance calculation; sorting cannot resize it.
// Keep two inline accessor sites, with CMoArray's unsigned 32-bit index conversion.
static inline ObjectDrawer &GetObjectDrawerAt(ObjectDrawer *pArray, uint32 index)
{
	return pArray[index];
}

// The native comparator returns a 32-bit result; the same ObjectDrawer ordering expressions
// retain the comparison spill, frame size and register lifetimes when expanded here.
static inline int CompareObjectDrawersForDistanceSort(const ObjectDrawer &a, const ObjectDrawer &b)
{
	if ((a.m_pObject->m_Flags & FLAG_REALLYCLOSE) == (b.m_pObject->m_Flags & FLAG_REALLYCLOSE))
		return a.m_fDistance < b.m_fDistance;
	else
		return (b.m_pObject->m_Flags & FLAG_REALLYCLOSE) < (a.m_pObject->m_Flags & FLAG_REALLYCLOSE);
}

// FUNCTION: D3DREN 0x100289b0
// Both accessor sites and the out-of-line LTVector constructor preserve native inlining.
// Computing the higher-index drawer first reproduces the original sort address schedule.
void ObjectDrawList::SortByViewDistance(ViewParams *pParams)
{
	uint32 i;
	int pass;
	int j;

	// distance of every queued object to the viewer (Jupiter ObjectDrawList::CalcDistance)
	for (i = 0; i < m_Unk14; i++)
	{
		ObjectDrawer *pDrawer = &(*this)[i];

		StoreObjectDrawerViewDistance(pDrawer, *pParams);
	}

	ObjectDrawer *pArray = GetArray();

	for (pass = 0; pass < 10; pass++)
	{
		int gap = g_ObjectDrawSortGaps[pass];
		int shift = g_ObjectDrawSortGapShifts[pass];
		int n = (m_Unk14 - 1) >> shift;

		for (j = 0; j < n; j++)
		{
			ObjectDrawer *pB = &GetObjectDrawerAt(pArray, gap + (j << shift));
			ObjectDrawer *pA = &GetObjectDrawerAt(pArray, j << shift);

			if (CompareObjectDrawersForDistanceSort(*pA, *pB))
			{
				ObjectDrawer tmp = *pA;
				*pA = *pB;
				*pB = tmp;
				if (j != 0)
					j -= 2;
			}
		}
	}
}

// NAME: ObjectDrawList::Add: Jupiter drawobjects.cpp (the Talon form takes no ViewParams and no distance)
// FUNCTION: D3DREN 0x10028ba0
void ObjectDrawList::Add(LTObject *pObject, DrawObjectFn fn)
{
	if (!g_CV_DrawSorted.m_IntVal)
	{
		fn(&g_ViewParams, pObject);
		return;
	}

	// Add it to the queue.  The pooled array only grows (FlushObjectQueues sizes it from the visible set).
	if (GetSize() <= m_Unk14)
	{
		ObjectDrawer drawer;
		drawer.m_pObject = pObject;
		drawer.m_pDrawFn = fn;
		Append(drawer);
		m_Unk14++;
	}
	else
	{
		// (two accessor uses: with one the original's out-of-line Insert2 above would be inlined, the inline budget
		// needs the pending site; the address is computed once either way)
		ObjectDrawer *pDrawer = &(*this)[m_Unk14];
		(*this)[m_Unk14].m_pObject = pObject;
		pDrawer->m_pDrawFn = fn;
		m_Unk14++;
	}
}

// ------------------------------------------------------------------ //
// Template code emitted with this file: CMoArray<ObjectDrawer> (vtables 0x1004641c and 0x100463ec).
// GenBegin/GenIsValid/GenSetCacheSize (vtable slots 0, 1, 11) are folded with the copies of other element types
// (0x10039e10, 0x10039e30, 0x1003a2c0), Clear is 0x1003a2d0.
// ------------------------------------------------------------------ //

// FUNCTION: D3DREN 0x10028c30 ?GenGetNext@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UBE?AVObjectDrawer@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x10028c60 ?GenGetAt@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UBE?AVObjectDrawer@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x10028c90 ?GenAppend@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UAEHAAVObjectDrawer@@@Z
// FUNCTION: D3DREN 0x10028e10 ?GenRemoveAt@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x10028f60 ?GenRemoveAll@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x10028fa0 ?GenGetSize@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UBEKXZ
// FUNCTION: D3DREN 0x10028fb0 ?GenCopyList@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UAEHABV?$GenList@VObjectDrawer@@@@@Z
// FUNCTION: D3DREN 0x10029100 ?GenAppendList@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UAEHABV?$GenList@VObjectDrawer@@@@@Z
// FUNCTION: D3DREN 0x10029200 ?GenFindElement@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@UBEHABVObjectDrawer@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x10029230 ?Init@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x100292e0 ?SetSize2@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x10029370 ?InternalNiceSetSize@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x100294a0 ?Insert2@?$CMoArray@VObjectDrawer@@VDefaultCache@@@@QAEHKABVObjectDrawer@@PAVLAlloc@@@Z
// FUNCTION: D3DREN 0x10029640 ?BaseNew@@YAPAVObjectDrawer@@PAVLAlloc@@PAV1@K@Z
