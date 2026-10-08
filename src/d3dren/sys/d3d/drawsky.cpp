// d3d.ren sys/d3d/drawsky (0x1002d080-0x1002d640): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// unit unk/1002d080 (0x1002d080-0x10030bb0): three speed objects (16-byte aligned COMDATs, /O2 /Ob2), LOW unit boundary confidence in
// units.csv, but names_proposal.csv / NAMING.md give them as the TU table does, in link order:
//   0x1002d080-0x1002d63f  drawsky     (Jupiter render_a/src/sys/d3d/drawsky.cpp: d3d_DrawSkyExtents; AllSkyPortals console variable)
//   0x1002d640-0x1002f32f  drawsprite  (Jupiter drawsprite.cpp)
//   0x1002f330-0x10030baf  drawworldmodel (Jupiter drawworldmodel.cpp; DrawWorldModels console variable)
// NAME: the three object names are from names_proposal.csv (source_file column) / NAMING.md section 4; the static-initialiser numbers
// below are those of this unit's own compile (the originals restart in each object).
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

// ---- drawsky -------------------------------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1002d080 _$E2
static UnkType_EmptyCtorW6 s_Empty1;

// guess: the sky camera's view parameters.  The exe's static initialiser (0x1002d090) is ViewParams' constructor inlined: it zeroes
// the four dwords of m_Rect (0x100739d4..e0) and nothing else (this object is /O2, which inlines it; common_stuff is /O1 and calls
// the out-of-line copy at 0x1000f3af); viewparams.h now defines the constructor inline.
// FUNCTION: D3DREN 0x1002d090 _$E5
// GLOBAL: D3DREN 0x100739b8
ViewParams g_SkyParams;

// FUNCTION: D3DREN 0x1002d0b0 _$E8
// GLOBAL: D3DREN 0x10073990
ConVar g_CV_AllSkyPortals("AllSkyPortals", 0.0f);

// GLOBAL: D3DREN 0x100566b8
extern int DAT_100566b8;
// ---- drawsky: the sky pass --------------------------------------------------------------------------------------------------------

// GLOBAL: D3DREN 0x10072d08
float g_SkyMaxY;			// Jupiter drawsky.cpp g_SkyMinX, g_SkyMinY, g_SkyMaxX, g_SkyMaxY (Ghidra names; the Talon extents are laid out
// GLOBAL: D3DREN 0x10072d0c
float g_SkyMaxX;			// as min Y, min X at 0x100739b0/b4 and max Y, max X at 0x10072d08/0c)
// GLOBAL: D3DREN 0x100739b0
float g_SkyMinY;
// GLOBAL: D3DREN 0x100739b4
float g_SkyMinX;
// GLOBAL: D3DREN 0x10048728
extern float g_SkyScale;	// guess: the SkyScale console variable's float mirror

void d3d_InitViewBox2(ViewBoxDef *pDef, float nearZ, float farZ, const ViewParams &PrevParams, float screenMinX, float screenMinY,
	float screenMaxX, float screenMaxY);
LTBOOL d3d_InitFrustum2(ViewParams *pParams, ViewBoxDef *pViewBox, float screenMinX, float screenMinY, float screenMaxX, float screenMaxY,
	LTMatrix *pMat, LTVector vScale);
int FUN_1002d0d0();


// guess: Jupiter polyclip.h's clipper dispatch as an inline function of the original (the exe expands it in several of this unit's
// functions; unit unk/100098d0 has the out-of-line copy ClipPoly): the polygon *ppVerts / *pnVerts is clipped against the planes
// of nFlags; with the UseD3DClip console variable set only the near plane is.
static inline int ClipPoly_Inline(uint32 nFlags, TLVertex **ppVerts, int *pnVerts)
{
	TLVertex *pOut;
	TLVertex *pVerts;
	int nVerts;
	char c0, c1, c2, c3, c4, c5;

	if (g_CV_UseD3DClip.m_IntVal)
	{
		nFlags &= 1;
		if (!nFlags)
			return 1;
	}
	pOut = g_pClipScratchVerts;
	pVerts = *ppVerts;
	nVerts = *pnVerts;
	if (((nFlags & 1) == 0 || ClipPolyNear(&c0, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 4) == 0 || ClipPolyLeft(&c1, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 8) == 0 || ClipPolyTop(&c2, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 0x10) == 0 || ClipPolyRight(&c3, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 0x20) == 0 || ClipPolyBottom(&c4, &pVerts, &nVerts, &pOut))
		&& ((nFlags & 2) == 0 || ClipPolyFar(&c5, &pVerts, &nVerts, &pOut)))
	{
		*ppVerts = pVerts;
		*pnVerts = nVerts;
		return 1;
	}
	return 0;
}

static inline float FUN_1002d0d0_Transform(LTVector *pDest, const LTMatrix *pMat, const LTVector *pSrc)
{
	float fW = pMat->m[3][2] * pSrc->z;
	fW += pMat->m[3][1] * pSrc->y;
	fW += pMat->m[3][0] * pSrc->x;
	fW += pMat->m[3][3];
	fW = 1.0f / fW;
	float fX = pMat->m[0][2] * pSrc->z;
	fX += pMat->m[0][1] * pSrc->y;
	fX += pMat->m[0][0] * pSrc->x;
	fX += pMat->m[0][3];
	pDest->x = fX * fW;
	float fY = pMat->m[1][0] * pSrc->x;
	fY += pMat->m[1][2] * pSrc->z;
	fY += pMat->m[1][1] * pSrc->y;
	fY += pMat->m[1][3];
	pDest->y = fY * fW;
	float fZ = pMat->m[2][0] * pSrc->x;
	fZ += pMat->m[2][2] * pSrc->z;
	fZ += pMat->m[2][1] * pSrc->y;
	fZ += pMat->m[2][3];
	pDest->z = fZ * fW;
	return fW;
}

static inline float FUN_1002d0d0_Project(LTVector *pDest, const LTMatrix *pMat, const LTVector *pSrc)
{
	float fW = pMat->m[3][2] * pSrc->z;
	fW += pMat->m[3][0] * pSrc->x;
	fW += pMat->m[3][1] * pSrc->y;
	fW += pMat->m[3][3];
	fW = 1.0f / fW;
	float fX = pMat->m[0][2] * pSrc->z;
	fX += pMat->m[0][0] * pSrc->x;
	fX += pMat->m[0][1] * pSrc->y;
	fX += pMat->m[0][3];
	pDest->x = fW * fX;
	float fY = pMat->m[1][2] * pSrc->z;
	fY += pMat->m[1][0] * pSrc->x;
	fY += pMat->m[1][1] * pSrc->y;
	fY += pMat->m[1][3];
	pDest->y = fW * fY;
	float fZ = pMat->m[2][2] * pSrc->z;
	fZ += pMat->m[2][0] * pSrc->x;
	fZ += pMat->m[2][1] * pSrc->y;
	fZ += pMat->m[2][3];
	pDest->z = fW * fZ;
	return fW;
}
// guess: the screen extents of the sky: every visible sky portal polygon (the polygons the tagging code collected, or the world's
// sky polygons when AllSkyPortals is set) is transformed to camera space, clipped and projected, and its extents are accumulated in
// g_SkyMinX/Y, g_SkyMaxX/Y; true when both extents are more than 0.9 (Jupiter's ExtendSkyBounds + the "> 0.9f" test).
// STUB diagnosis (2026-10-07, tenth pass): 639/1008 bytes differ at the same 1008-byte extent. The first camera-space
// transform follows the target's z,y,x order for W/X and x,z,y for Y/Z; the projected-vertex transform uses z,x,y per row.
// Fetching each polygon count before its array pointer and snapshotting/incrementing the source and destination cursors before
// each transform reduce strict differences from 680 to 639 bytes. The advisory ALIGNED score worsens from 76/71 to 123/117
// (including/ignoring stack offsets). Remaining differences are in list-selection and clipping control flow, register allocation,
// and projection/extent scheduling; no unsafe access or changed vertex bounds is involved.
// STUB: D3DREN 0x1002d0d0
int FUN_1002d0d0()
{
	VisibleSet *pVisibleSet = d3d_GetVisibleSet();
	WorldPoly **ppPolys;
	uint32 nPolys;
	uint32 nClipFlags;
	TLVertex aVerts[0x28];

	g_SkyMinY = 10000.0f;
	g_SkyMinX = 10000.0f;
	g_SkyMaxY = -10000.0f;
	g_SkyMaxX = -10000.0f;

	if (g_CV_AllSkyPortals.m_IntVal && DAT_10056770)
	{
		nPolys = DAT_10056770->m_SkyPolies.GetSize();
		ppPolys = DAT_10056770->m_SkyPolies.GetArray();
		nClipFlags = 0x3d;
	}
	else
	{
		nPolys = pVisibleSet->m_nUnk3c;
		ppPolys = pVisibleSet->m_Unk28.GetArray();
		nClipFlags = 0x3f;
	}

	for (; nPolys != 0; nPolys--)
	{
		WorldPoly *pPoly = *ppPolys;
		UnkType_PolyVertex *pSrc = (UnkType_PolyVertex *)((uint8 *)pPoly + 0x58);
		TLVertex *pDest = aVerts;
		int i;
		LTMatrix *pMat = (LTMatrix *)&g_ViewParams.m_mClipTransform;
		for (i = 0; i < pPoly->m_nVertices; i++)
		{
			UnkType_PolyVertex *pCurSrc = pSrc++;
			TLVertex *pCurDest = pDest++;
			FUN_1002d0d0_Transform(&pCurDest->m_Vec, pMat, pCurSrc->m_Vec);
		}

		TLVertex *pVerts = aVerts;
		int nVerts = pPoly->m_nVertices;
		if (ClipPoly_Inline(nClipFlags, &pVerts, &nVerts))
		{
			for (i = 0; i < nVerts; i++)
			{
				LTVector v;
				float fW = FUN_1002d0d0_Project(&v, (LTMatrix *)&g_ViewParams.m_DeviceTimesProjection, &pVerts[i].m_Vec);
				float fX = v.x;
				float fY = v.y;
				if (fX <= g_SkyMinX)
					g_SkyMinX = fX;
				if (fY <= g_SkyMinY)
					g_SkyMinY = fY;
				if (g_SkyMaxX <= fX)
					g_SkyMaxX = fX;
				if (g_SkyMaxY <= fY)
					g_SkyMaxY = fY;
			}
			DAT_100566b8++;
		}
		ppPolys++;
	}

	return (g_SkyMaxX - g_SkyMinX > 0.9f) && (g_SkyMaxY - g_SkyMinY > 0.9f);
}

// NAME: d3d_DrawSky: Jupiter drawsky.cpp d3d_DrawSkyExtents (names_proposal.csv, medium): the same sequence (extents, view box, frustum
// with the sky camera position and SkyScale, d3d_DrawSkyObjects); the Talon one finds the extents itself and takes no arguments
// STUB diagnosis (W6): 384 bytes like the exe (two counter epilogues, same calls in the same order); 184 bytes differ: the exe loads the
// four extents into edx, eax, ecx, edx (g_SkyMaxY first) where ours uses ecx, edx, eax, and the matrix copy is interleaved with other
// register choices.  Permuter best 47 mismatches.
// STUB: D3DREN 0x1002d4c0
void d3d_DrawSky()
{
	CountAdder cntAdd((uint32 *)((uint8 *)g_pStruct + 0x6c));

	if (!g_DrawSky || !g_EnableSky || g_pSceneDesc->m_nSkyObjects <= 0)
		return;
	if (!FUN_1002d0d0())
		return;
	{
		ViewBoxDef viewBox;
		d3d_InitViewBox2(&viewBox, 0.01f, g_pSceneDesc->m_FarZ, g_ViewParams, g_SkyMinX, g_SkyMinY, g_SkyMaxX, g_SkyMaxY);

		LTMatrix mat;
		mat = g_ViewParams.m_mInvView;
		mat.SetTranslation(g_ViewParams.m_SkyViewPos);

		d3d_InitFrustum2(&g_SkyParams, &viewBox, g_SkyMinX, g_SkyMinY, g_SkyMaxX, g_SkyMaxY, &mat,
			LTVector(g_SkyScale, g_SkyScale, g_SkyScale));

		d3d_DrawSkyObjects();
	}
}

// ---- drawworldmodel: world polygon drawing ------------------------------------------------------------------------------------------

// the polygon's surface (WorldPoly::m_pSurface is a void * in de_objects.h)
#define POLY_SURFACE(p)		((Surface *)(p)->m_pSurface)
// guess: the poly's frame tag (the tagging code sets it to the frame code of the frame the poly was seen in)
#define WORLDPOLY_FRAMECODE(p)	(*(uint16 *)((uint8 *)(p) + 0x46))

// ---- d3d_DrawTranslucentWorldPoly ----------------------------------------------------------------------------------------------------

#define WORLDPOLY_LIGHTS(p)	((UnkType_PolyLightRef *)*(uint32 *)((uint8 *)(p) + 0x30))

// the sprite size / bias constants of Jupiter drawsprite.cpp
#define SPRITE_POSITION_ZBIAS	-20.0f
#define SPRITE_MINFACTORDIST	10.0f
#define SPRITE_MAXFACTORDIST	500.0f
#define SPRITE_MINFACTOR		0.1f
#define SPRITE_MAXFACTOR		2.0f
