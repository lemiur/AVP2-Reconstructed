// d3d.ren object drawing dispatch: the Talon-era form of Jupiter's render_a/src/sys/d3d/drawobjects.h/.cpp.
// Owner: unit unk/100285a0 (package W5).
//
// NAME: ObjectHandler, g_ObjectHandlers, ObjectDrawer, ObjectDrawList, d3d_InitObjectModules, d3d_TermObjectModules,
// d3d_InitObjectQueues, d3d_FlushObjectQueues, ObjectDrawList::Add: Jupiter drawobjects.h/.cpp (names_proposal.csv:
// g_ObjectHandlers high, ObjectDrawList::Add high; the Talon bodies and layouts differ from Jupiter's as noted below).
#ifndef __D3DREN_DRAWOBJECTS_H__
#define __D3DREN_DRAWOBJECTS_H__

#include "ltbasedefs.h"
#include "ltdynarray.h"
#include "de_objects.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"		// DrawObjectFn: void (*)(ViewParams *, LTObject *)

// One entry per object type (OT_NORMAL .. OT_CANVAS = 11 entries, 0x14 bytes each; Jupiter's m_bCheckWorldVisibility and
// m_GetDims do not exist in Talon, nor does the volume effect entry).
struct ObjectHandler
{
	void		(*m_ModuleInit)();					// 0x00 one-time module init
	void		(*m_ModuleTerm)();					// 0x04 one-time module term
	int			m_bModuleInitted;					// 0x08 4 bytes in Talon (Jupiter: bool)
	void		(*m_PreFrameFn)();					// 0x0c called before each frame
	void		(*m_ProcessObjectFn)(LTObject *pObject);	// 0x10 called as objects are tagged visible
};

// GLOBAL: D3DREN 0x1004ba78
extern ObjectHandler g_ObjectHandlers[11];

void d3d_InitObjectModules();		// 0x100285e0
void d3d_TermObjectModules();		// 0x10028610
void d3d_InitObjectQueues();		// 0x10028640
void d3d_FlushObjectQueues();		// 0x10028660

// Object rendering wrapper for sorting (Jupiter drawobjects.h ObjectDrawer; 12 bytes).
class ObjectDrawer
{
public:
	ObjectDrawer() {};

	// Distance comparison (Jupiter ObjectDrawer::operator<): REALLYCLOSE objects first, then farthest first.
	inline bool operator<(const ObjectDrawer &cOther) const {
		if ((m_pObject->m_Flags & FLAG_REALLYCLOSE) == (cOther.m_pObject->m_Flags & FLAG_REALLYCLOSE))
			return (m_fDistance < cOther.m_fDistance);
		else
			return (cOther.m_pObject->m_Flags & FLAG_REALLYCLOSE) < (m_pObject->m_Flags & FLAG_REALLYCLOSE);
	}

public:
	LTObject		*m_pObject;		// 0x00
	DrawObjectFn	m_pDrawFn;		// 0x04 the function used to draw this object
	float			m_fDistance;	// 0x08 squared distance to the viewer (filled in by the sort)
};

class ObjectDrawList;
// GLOBAL: D3DREN 0x1006b934
extern ObjectDrawList *g_pTranslucentObjectDrawList;	// &d3d_FlushObjectQueues::s_TransObjList: set by the list's constructor

// Mixed-object rendering list for sorting translucent objects in Z order (Jupiter: a priority_queue member; Talon:
// the array of the CMoArray base plus a count of the queued entries at +0x14, sorted in place by SortByViewDistance).
// The constructor stores `this` in the global g_pTranslucentObjectDrawList (every Queue* callback adds through that pointer).
class ObjectDrawList : public CMoArray<ObjectDrawer>
{
public:
	ObjectDrawList() { m_Unk14 = 0; g_pTranslucentObjectDrawList = this; }

	// Add an object to draw to the list (draws immediately when the DrawSorted console variable is 0).
	void Add(LTObject *pObject, DrawObjectFn fn);			// 0x10028ba0

	// Computes the distances and sorts the entries (shell-sort passes with the gap tables at 0x1004bb54/0x1004bb7c).
	void SortByViewDistance(ViewParams *pParams);

private:
	static float CalcDistance(const LTObject *pObject, const ViewParams &Params);	// Jupiter name (expanded into SortByViewDistance)

public:
	uint32			m_Unk14;		// 0x14 number of queued entries (guess: Jupiter has no such member)
};

#endif
