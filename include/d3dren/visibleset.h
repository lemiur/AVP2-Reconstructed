// d3d.ren VisibleSet (unit sys/d3d/tagnodes, owner: package W8): the per-frame lists of visible objects (the VS_* sets) and
// the node tagging code that fills them.  Talon-era form of Jupiter's render_a/src/sys/d3d/tagnodes.h.
//
// NAME: BaseObjectSet, AllocSet, VisibleSet and their members m_pObjects, m_nObjects, m_Link, m_nMaxObjects, m_pSetName,
// m_Sets: Jupiter tagnodes.h (the Talon layout agrees member for member: BaseObjectSet is 0x1c bytes, AllocSet 0x20,
// VisibleSet's sets follow the Talon-only extras).  The set members take the Jupiter names where the VS_ string of
// VisibleSet::Init (0x10038cf0) agrees (m_SolidModels = "VS_MODELS", m_TranslucentSprites = "VS_SPRITES", ...); the two
// CHROMAKEY sets are Talon-only (names_proposal.csv has only `guess_` rows for them), so they stay m_Unk184 / m_Unk224.
// NAME: g_VisibleSet (0x100937a8): Jupiter tagnodes.cpp; d3d_GetVisibleSet, d3d_ProcessAttachments, d3d_TagVisibleLeaves:
// Jupiter tagnodes.cpp (the Talon bodies differ: names_proposal.csv).
#ifndef __D3DREN_VISIBLESET_H__
#define __D3DREN_VISIBLESET_H__

#include "ltbasedefs.h"
#include "ltdynarray.h"
#include "de_objects.h"
#include "d3dren/common_stuff.h"	// AddDebugMessage

class ViewParams;		// the renderer's view state, g_ViewParams 0x10055cf8 (owner: package W1, include/d3dren/viewparams.h)
struct WorldPoly;
class VisibleSet;

// A draw callback of a set (Talon: __cdecl (ViewParams *, LTObject *), Jupiter takes a const ViewParams &).
typedef void (*DrawObjectFn)(ViewParams *pParams, LTObject *pObject);

class BaseObjectSet
{
public:
					BaseObjectSet()
					{
						m_Link.m_pData = this;
						m_nObjects = 0;
						m_pObjects = NULL;
						m_pSetName = "";
						m_nMaxObjects = 0;
					}

	// FUN_100389f0: only the set name and the VisibleSet's list (the allocation is AllocSet's).
	int				Init(VisibleSet *pVisibleSet, const char *pSetName);

	// FUN_10038a30
	void			Draw(ViewParams *pParams, DrawObjectFn fn);

	// Jupiter BaseObjectSet::Add; the Process* functions of the draw units inline it.
	void			Add(LTObject *pObject)
	{
		if (m_nObjects < m_nMaxObjects)
		{
			m_pObjects[m_nObjects] = pObject;
			m_nObjects++;
		}
		else
		{
			AddDebugMessage(1, "Set '%s' overflowed", m_pSetName);
		}
	}

	void			ClearSet()					{ m_nObjects = 0; }

	// Added by the W3 agent (defined in unit unk/10021d70, 0x10022b50): draws the visible objects of this canvas set at once when
	// their client flags say CF_SOLIDCANVAS, else adds them to pTranslucent.  pUnused is a dummy char the caller passes (never read).
	void			DrawSolidCanvasesAndCollectTranslucent(ViewParams *pParams, DrawObjectFn fn, char *pUnused, BaseObjectSet *pTranslucent);

public:
	LTObject		**m_pObjects;				// 0x00
	uint32			m_nObjects;					// 0x04

protected:
	LTLink			m_Link;						// 0x08 in VisibleSet::m_Sets
	uint32			m_nMaxObjects;				// 0x14
	const char		*m_pSetName;				// 0x18
};

// 0x20 bytes; Jupiter's AllocSet::m_pArray is not used by the Talon code (the objects array is BaseObjectSet's).
class AllocSet : public BaseObjectSet
{
public:
					AllocSet()		{}
					~AllocSet()		{ Term(); }

	int				Init(VisibleSet *pVisibleSet, char *pSetName, uint32 defaultMax);	// FUN_10038aa0
	void			Term();																// FUN_10038b40

	LTObject		**m_pArray;					// 0x1c (never touched)
};

class VisibleSet
{
public:
					VisibleSet();				// 0x10038b60

	int				Init();						// 0x10038cf0
	void			Term();
	void			ClearSet();					// 0x100390d0

public:
	LTLink			m_Sets;						// 0x00 the sets in rendering order (m_Link of each BaseObjectSet)
	int				m_Unk0c;					// 0x0c constructor stores 1; the node tagging (0x10039540) only runs when bit 0 is set
	CMoArray<WorldPoly*>	m_Unk10;			// 0x10 polys collected by the tagging code (wanted cache 0x200)
	uint32			m_nUnk24;					// 0x24 used count of m_Unk10 (its array is only appended when this reaches its size)
	CMoArray<WorldPoly*>	m_Unk28;			// 0x28 second poly array (wanted cache 0x20)
	uint32			m_nUnk3c;					// 0x3c used count of m_Unk28
	struct SortedPoly
	{
		WorldPoly	*m_pPoly;
		void		*m_pUnk;				// &g_ViewParams.m_mIdentity (stored with the poly by the tagging code)
	}				m_Unk40[32];				// 0x40
	uint32			m_nUnk140;					// 0x140 number of m_Unk40 entries

	AllocSet		m_SolidModels;				// 0x144 "VS_MODELS"
	AllocSet		m_TranslucentModels;		// 0x164 "VS_MODELS_TRANSLUCENT"
	AllocSet		m_Unk184;					// 0x184 "VS_MODELS_CHROMAKEY"
	AllocSet		m_TranslucentSprites;		// 0x1a4 "VS_SPRITES"
	AllocSet		m_NoZSprites;				// 0x1c4 "VS_SPRITES_NOZ"
	AllocSet		m_SolidWorldModels;			// 0x1e4 "VS_WORLDMODELS"
	AllocSet		m_TranslucentWorldModels;	// 0x204 "VS_WORLDMODELS_TRANSLUCENT"
	AllocSet		m_Unk224;					// 0x224 "VS_WORLDMODELS_CHROMAKEY"
	AllocSet		m_Lights;					// 0x244 "VS_LIGHTS"
	AllocSet		m_SolidPolyGrids;			// 0x264 "VS_POLYGRIDS"
	AllocSet		m_TranslucentPolyGrids;		// 0x284 "VS_POLYGRIDS_TRANSLUCENT"
	AllocSet		m_LineSystems;				// 0x2a4 "VS_LINESYSTEMS"
	AllocSet		m_ParticleSystems;			// 0x2c4 "VS_PARTICLESYSTEMS"
	AllocSet		m_SolidCanvases;			// 0x2e4 "VS_CANVASES"
	AllocSet		m_TranslucentCanvases;		// 0x304 "VS_CANVASES_TRANSLUCENT"
};											// 0x324

// GLOBAL: D3DREN 0x100937a8
extern VisibleSet g_VisibleSet;

// 0x10039e00: returns &g_VisibleSet (31 callers).
VisibleSet *d3d_GetVisibleSet();

#endif
