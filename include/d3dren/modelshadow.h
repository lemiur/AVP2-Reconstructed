// d3d.ren model shadows: Talon's drawmodelshadows.cpp (the old, non-shader form of Jupiter's
// render_a/src/sys/d3d/drawmodelshadows.cpp + modelshadowshader.h).  Unit: d3d.ren 0x10025013-0x1002701e.
// Owner of this header: work package W7 (agents shadowclip = the helpers, plus the two big functions
// ModelDraw::DrawModelShadows 0x100252c6 and 0x1002701e).  Every type is defined here once; append with small additive edits.
//
// NAME: ShadowLightInfo and its members m_vLightOrigin, m_Vecs, m_ProjectionPlane, m_vProjectionCenter, m_vWindowTopLeft,
// m_fSizeX, m_fSizeY, m_FrustumPlanes: Jupiter render_a/src/sys/d3d/modelshadowshader.h class ShadowLightInfo (the same layout up
// to 0x60: ModelDraw::DrawModelShadows 0x100252c6 fills it exactly like Jupiter's DrawModelShadows).  The Talon class has one
// more member, a 4x4 matrix at 0x60 (m_Unk60) between m_fSizeY and the frustum planes, which the polygon drawers 0x10025078 and
// 0x10026d6a read as the texture projection (rows 0, 1 and 3 give the three texture coordinates).
// The 0x24-byte vertex is the D3DTLVERTEX layout of tlvertex.h (m_Vec, rhw, colour, specular, tu, tv) plus one more float: the
// shadow code draws it as D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1 after dividing tu and tv by that float.
#ifndef __D3DREN_MODELSHADOW_H__
#define __D3DREN_MODELSHADOW_H__

#include <stddef.h>
#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "d3dren/tlvertex.h"	// TLRGB

struct WorldPoly;
class ViewParams;

// A 0x24-byte vertex of the polygon clipper (the clip helpers below, the view transform and the projection step handle
// it as nine floats).  Role name: UnkType (no source name known); the members follow TLVertex (tlvertex.h).
struct UnkType_Vertex36
{
	LTVector	m_Vec;				// 0x00 world position, then camera space, then screen space
	float		rhw;				// 0x0c set by the projection (1/z)
	union
	{
		TLRGB	rgb;				// 0x10
		uint32	color;
	};
	union
	{
		TLRGB	specular_rgb;		// 0x14 (only .a is interpolated by the clipper)
		uint32	specular;
	};
	float		tu, tv;				// 0x18, 0x1c
	float		m_Unk20;			// 0x20 guess: third (homogeneous) texture coordinate, interpolated like tu/tv
};
typedef char UnkType_Vertex36_Size[(sizeof(UnkType_Vertex36) == 0x24) ? 1 : -1];

// NAME: ShadowLightInfo: see above.  This describes how one shadow is projected onto the world.
class ShadowLightInfo
{
public:
	enum { eNumShadowLightFrustumPlanes = 6 };

	LTVector	m_vLightOrigin;			// 0x00
	LTVector	m_Vecs[3];				// 0x0c coordinate system for the texture
	LTPlane		m_ProjectionPlane;		// 0x30
	LTVector	m_vProjectionCenter;	// 0x40
	LTVector	m_vWindowTopLeft;		// 0x4c
	float		m_fSizeX;				// 0x58
	float		m_fSizeY;				// 0x5c
	LTMatrix	m_Unk60;				// 0x60 guess: world position -> texture coordinates (planar projection * scale/offset)

	// Shadowed polys are clipped into here.
	LTPlane		m_FrustumPlanes[eNumShadowLightFrustumPlanes];	// 0xa0
};

// VC6 has no static_assert.
#define SHADOW_CHECKOFFSET(member, ofs)	typedef char SL_Check##member[(offsetof(ShadowLightInfo, member) == (ofs)) ? 1 : -1];
SHADOW_CHECKOFFSET(m_vLightOrigin, 0x00)
SHADOW_CHECKOFFSET(m_Vecs, 0x0c)
SHADOW_CHECKOFFSET(m_ProjectionPlane, 0x30)
SHADOW_CHECKOFFSET(m_vProjectionCenter, 0x40)
SHADOW_CHECKOFFSET(m_vWindowTopLeft, 0x4c)
SHADOW_CHECKOFFSET(m_fSizeX, 0x58)
SHADOW_CHECKOFFSET(m_Unk60, 0x60)
SHADOW_CHECKOFFSET(m_FrustumPlanes, 0xa0)
typedef char SL_CheckSize[(sizeof(ShadowLightInfo) == 0x100) ? 1 : -1];

// The object the plane clippers 0x100260ce and 0x10026300/0x10026343 work through: it holds the plane to clip against (Jupiter's
// polyclip.h expects CLIPTEST/DOCLIP macros that use a local `thePlane`, drawsprite.cpp PLANETEST/DOPLANECLIP; the Talon code
// has them as member functions of a small class whose first member points at the plane).
class UnkType_PlaneClipper
{
public:
	// guess: Jupiter's CLIPTEST for a plane: true when the point is on the front side (distance > 0).
	int IsInsidePlane(LTVector *pVec);		// 0x10026300 thiscall, ret 4 (returns 0 or 1 in eax; bool would add a movzx at the callers)
	// guess: Jupiter's DOCLIP for a plane: intersects the edge pt1-pt2 with the plane, writes the point to *pOut and returns
	// the parameter t (in st(0)); |d1 - d2| <= CLIP_EPSILON gives t = 0.
	float IntersectEdgeWithPlane(LTVector *pt1, LTVector *pt2, LTVector *pOut);	// 0x10026343 thiscall, ret 0xc

	LTPlane		*m_Unk00;				// 0x00 the clip plane
};

// Jupiter polyclip.h T::ClipExtra for the 0x24-byte vertex: interpolates tu, tv, m_Unk20 and rounds r, g, b, a of the colour and
// the alpha of the specular colour (0x10026300 .. 0x100261f9 are in address order).  Plain cdecl.
void InterpolateShadowVertexAttributes(UnkType_Vertex36 *pPrev, UnkType_Vertex36 *pCur, UnkType_Vertex36 *pOut, float t);		// 0x100261f9

// Jupiter polyclip.h expanded for the plane of *pClipper: clips the polygon *ppVerts (*pnVerts vertices) against it, writing the
// clipped polygon to **ppOut (then *ppOut advances past it).  Returns 0 when nothing is left (all outside), else 1 (*ppVerts and
// *pnVerts then describe the polygon, in place when it was fully inside).  cdecl; the first argument is a plane clipper.
int ClipShadowPolygonToPlane(UnkType_PlaneClipper *pClipper, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x100260ce

// guess: the same view-space clippers for the six view frustum planes (flag bit 1 near z == g_ViewParams.m_NearZ, 4 left x+z == 0, 8 y == z,
// 0x10 x == z, 0x20 y == -z, 2 far z == g_ViewParams.m_ClipFarZ); the first argument is unused (the address of a dummy local). cdecl; same contract
// as ClipShadowPolygonToPlane.
int ClipShadowPolygonToNearPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x100265c9 near
int ClipShadowPolygonToLeftPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x10026700 left
int ClipShadowPolygonToTopPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x10026835 y == z
int ClipShadowPolygonToRightPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x10026969 x == z
int ClipShadowPolygonToBottomPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x10026a9c y == -z
int ClipShadowPolygonToFarPlane(char *pUnused, UnkType_Vertex36 **ppVerts, int *pnVerts, UnkType_Vertex36 **ppOut);	// 0x10026bd2 far

// guess: clips the polygon against the planes selected by the bits of flags (g_ClipFlags holds 0x3f when the shadow code calls it).
// With the console variable that makes the renderer use the D3D clipper (DAT_1005c818) only bit 1 is processed.
int ClipShadowPolygonToViewFrustum(uint32 flags, UnkType_Vertex36 **ppVerts, int *pnVerts);	// 0x100264ad

// guess: transforms the polygon into view space and clips it like Jupiter's polyclip, or only projects it when g_ClipFlags is 0
// (the vertex type counterpart of TransformClipProjectPolygon40).  pViewParams is g_ViewParams (0x10055cf8).  The fourth argument is not used.
int TransformClipAndProjectShadowPolygon(UnkType_Vertex36 **ppVerts, int *pnVerts, ViewParams *pViewParams, int nUnused);	// 0x10026412

// Draws the shadow of the model onto one world polygon and the projected-texture variant: both are members of ModelDraw
// (setupmodel.h declares ModelDraw::DrawBlobShadowOnWorldPoly and ::DrawProjectedShadowOnWorldPoly, thiscall with an unused `this`):
//   void ModelDraw::DrawBlobShadowOnWorldPoly(ShadowLightInfo *pInfo, WorldPoly *pPoly);				// 0x10025078 ret 8
//   void ModelDraw::DrawProjectedShadowOnWorldPoly(ShadowLightInfo *pInfo, WorldPoly *pPoly, float fDist);	// 0x10026d6a ret 0xc

// NAME: NUM_MODEL_SHADOWS: Jupiter render_a/src/sys/d3d/drawmodelshadows.cpp (the value 8: the per-light arrays of both shadow paths)
#ifndef NUM_MODEL_SHADOWS
#define NUM_MODEL_SHADOWS		8
#endif

// NAME: Jupiter 3d_ops.h CPLANE_*_INDEX (the order of ShadowLightInfo::m_FrustumPlanes)
#ifndef CPLANE_NEAR_INDEX
#define CPLANE_NEAR_INDEX		0
#define CPLANE_FAR_INDEX		1
#define CPLANE_LEFT_INDEX		2
#define CPLANE_TOP_INDEX		3
#define CPLANE_RIGHT_INDEX		4
#define CPLANE_BOTTOM_INDEX		5
#endif

// guess (role names invented): the polygons found for one shadow light, filled by the callback CollectWorldModelSegmentPolys (the world-tree
// FindObjectsOnPoint callback of both shadow paths): the polygon array at +0x00 with its count at +0x400.
struct UnkType_ShadowPolys
{
	WorldPoly	*m_Polys[0x100];	// 0x000
	uint32		m_nPolys;			// 0x400
};

// guess: the user record of the callback CollectWorldModelSegmentPolys: the polygon list and the segment from the light.  0x3c bytes: with the
// trailing dword the frame of ModelDraw::DrawModelShadows has the exe's layout.
struct UnkType_ShadowPolyQuery
{
	UnkType_ShadowPolys	*m_pPolys;	// 0x00
	LTVector			m_vStart;	// 0x04
	LTVector			m_vEnd;		// 0x10
	float				m_fRadius;	// 0x1c
	LTVector			m_vOrigin;	// 0x20 guess: the light origin the segment starts at (the callback does not read it)
	LTVector			m_vDir;		// 0x2c guess: the direction of the segment (not read by the callback either)
	uint32				m_Unk38;	// 0x38 not touched by DrawModelShadows
};

// bInside[] arrays of the seven expanded polygon clippers of drawmodelshadows.cpp (one per clip function, 0xe0 bytes apart): the
// polygon clipper of Jupiter's polyclip.h keeps it as a static of the expanded clipper.  Defined in drawmodelshadows.cpp.
// GLOBAL: D3DREN 0x10094440
extern int g_ShadowClipPlaneInsideFlags[56];
// GLOBAL: D3DREN 0x10093f00
extern int g_ShadowClipFarInsideFlags[56];
// GLOBAL: D3DREN 0x10093fe0
extern int g_ShadowClipBottomInsideFlags[56];
// GLOBAL: D3DREN 0x100940c0
extern int g_ShadowClipRightInsideFlags[56];
// GLOBAL: D3DREN 0x100941a0
extern int g_ShadowClipTopInsideFlags[56];
// GLOBAL: D3DREN 0x10094280
extern int g_ShadowClipLeftInsideFlags[56];
// GLOBAL: D3DREN 0x10094360
extern int g_ShadowClipNearInsideFlags[56];

#endif
