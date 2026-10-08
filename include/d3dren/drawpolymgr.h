// d3d.ren DrawPolyMgr: Talon-only multi-pass polygon drawing (state-block based): a material with up to three passes of up to three
// texture stages each, the world polygons are queued per pass in hash buckets (keyed by texture or lightmap page) and drawn when
// d3d_FlushObjectQueues-time code calls the flush (FlushQueuedPolys `g_DrawPolyMgr.Flush()` when the DrawPolyMgr console variable is set).
// Owner: unit unk/10029660 (package W6).  Jupiter has no equivalent.
//
// NAME: DrawPolyMgr, DrawPolyMgr::DrawPolyAdditionalPass, Material, TextureSrcInitFn: strings of d3d.ren ("DrawPolyMgr::DrawPolyAdditionalPass:
// nQVerts != nVertices" at 0x1004bd0c, "Material '%s', pass %d, stage %d: TextureSrcInitFn failed." at 0x1004bcd0, "TEST_GOURAUD" at
// 0x1004bca4).  QVert: guess from the "nQVerts" string (the element type of the CMoArray at +0x7bc: the first 16 bytes of the
// pre-transformed vertices, x, y, z, rhw); not verified.  Everything else is invented-looking on purpose: UnkType_* classes, m_Unk<offset>
// members, FUN_<addr> functions; the roles are in the `guess:` comments.
//
// Layout (offset asserted at the bottom):
//   Material (0x1cc): name[0x20], number of passes, 3 passes (0x8c each: 8 dwords + 3 stages of 9 dwords), the instance pointer.
//   MaterialInstance (0x59c): the material, next instance, 3 pass lists (0x1dc each: 32 hash buckets, the blend factors, a list head,
//                             the D3D texture stage states of 3 stages), i.e. the per-pass "state block" and the queued polygons.
//   DrawPolyMgr is a Material (its built-in TEST_GOURAUD material) followed by its own instance and the draw state.
#ifndef __D3DREN_DRAWPOLYMGR_H__
#define __D3DREN_DRAWPOLYMGR_H__

#include <stddef.h>
#include "d3dren/d3ddevice.h"
#include "ltbasedefs.h"
#include "ltlink.h"
#include "ltdynarray.h"
#include "de_objects.h"
#include "d3dren/tlvertex.h"
#include "d3dren/polydraw.h"	// UnkType_PolyVertex
#include "../../../build/proj/LT2/lithshared/stdlith/object_bank.h"

class DrawPolyMgr;

// The queued vertex: the position and rhw of a pre-transformed vertex (16 bytes).
struct QVert
{
	LTVector	pos;	// 0x00 x, y, z
	float		w;		// 0x0c rhw
};

// One texture stage of a pass (0x24 bytes): six dwords nobody touches in d3d.ren (filled by the loader of the material, if there is one),
// then the indices of the per-stage functions.
struct UnkType_DPMStage
{
	UnkType_DPMStage() { m_Unk18 = 0; m_Unk1c = 0; m_Unk20 = 0; }

	uint32		m_Unk00[6];		// 0x00
	uint32		m_Unk18;		// 0x18 guess: index into the texture coordinate generator table (0x1004bbe0)
	uint32		m_Unk1c;		// 0x1c guess: index into the TextureSrcInitFn table (0x1004bc00) and the bind table (0x1004bc10)
	uint32		m_Unk20;		// 0x20 guess: index into the post-draw function table (0x1004bc68)
};

// One pass of a material (0x8c bytes).
struct UnkType_DPMPass
{
	UnkType_DPMPass();

	uint32			m_Unk00;	// 0x00 guess: ALPHABLENDENABLE
	uint32			m_Unk04;	// 0x04
	uint32			m_Unk08;	// 0x08
	uint32			m_Unk0c;	// 0x0c guess: the pass uses dynamic lights
	uint32			m_Unk10;	// 0x10 guess: index into the vertex colour function table (0x1004bc20)
	uint32			m_Unk14;	// 0x14 guess: index into the fog function table (0x1004bc2c)
	uint32			m_Unk18;	// 0x18 guess: index into the bucket key function table (0x1004bbd8)
	uint32			m_nStages;	// 0x1c
	UnkType_DPMStage	m_Stages[3];	// 0x20
};

// The Direct3D texture stage states of one stage of a pass list (0x18 bytes; the order of the D3DTSS_ values 1..6).
struct UnkType_DPMStageStates
{
	UnkType_DPMStageStates() { m_ColorOp = D3DTOP_DISABLE; m_AlphaOp = D3DTOP_DISABLE; }

	uint32		m_ColorOp;		// 0x00 D3DTSS_COLOROP
	uint32		m_ColorArg1;	// 0x04
	uint32		m_ColorArg2;	// 0x08
	uint32		m_AlphaOp;		// 0x0c D3DTSS_ALPHAOP
	uint32		m_AlphaArg1;	// 0x10
	uint32		m_AlphaArg2;	// 0x14
};

// The state block and the queued polygons of one pass of a material instance (0x1dc bytes).
struct UnkType_DPMPassList
{
	UnkType_DPMPassList();

	LTLink				m_Unk000[0x20];	// 0x000 guess: hash buckets keyed by texture/lightmap page pointer (>> 2 & 0x1f); empty lists tied off
	uint32				m_SrcBlend;		// 0x180 D3DRENDERSTATE_SRCBLEND
	uint32				m_DestBlend;	// 0x184 D3DRENDERSTATE_DESTBLEND
	LTLink				m_Unk188;		// 0x188 guess: list head of all queued buckets of the pass
	UnkType_DPMStageStates	m_Stages[3];	// 0x194
};

class Material;
// A material's queued polygons (0x59c bytes).
struct UnkType_DPMMaterialInstance
{
	UnkType_DPMMaterialInstance();

	Material			*m_pMaterial;	// 0x00
	UnkType_DPMMaterialInstance	*m_pNext;	// 0x04
	UnkType_DPMPassList	m_Lists[3];		// 0x08
};

// guess: Material (0x1cc bytes).
class Material
{
public:
	Material();

	char				m_Name[0x20];	// 0x00
	uint32				m_nPasses;		// 0x20
	UnkType_DPMPass		m_Passes[3];	// 0x24
	UnkType_DPMMaterialInstance	*m_pInstance;	// 0x1c8
};

// The 0x28 byte node the ObjectBank at +0x7d4 hands out: one queued bucket entry (linked into a pass list bucket at +0x10 and into the
// pass list's list head at +0x1c).
struct UnkType_DPMNode
{
	UnkType_DPMNode();
	~UnkType_DPMNode();

	CheapLTLink			m_Unk00;		// 0x00 guess: head of the polygons queued in this bucket entry
	uint32				m_Unk08;		// 0x08
	uint32				m_Unk0c;		// 0x0c guess: the key of the bucket entry (surface texture pointer or lightmap page pointer)
	LTLink				m_Unk10;		// 0x10 in the hash bucket (m_pData = the node)
	LTLink				m_Unk1c;		// 0x1c in the pass list's list head (m_pData = the node)
};

// The scratch record DrawPolyFirstPass builds a polygon in on its stack (0x142c bytes with the vertices): the polygon, one result
// slot per stage (the third argument of the texture binders), the vertex count and stride, then the pre-transformed vertices.
struct UnkType_DPMDrawBuffer
{
	WorldPoly	*m_pPoly;			// 0x00
	uint32		m_Unk04[3];			// 0x04 guess: per stage result of the texture binder
	int			m_nVertices;		// 0x10
	int			m_Unk14;			// 0x14 guess: vertex stride in bytes
	uint8		m_Verts[0x1400];	// 0x18
};

// guess: callbacks of the passes (the tables at 0x1004bbd8..0x1004bc78); each table entry has its own signature
typedef void (__cdecl *UnkType_DPMKeyFn)(WorldPoly *pPoly, int *pBucket, uint32 *pKey);
typedef void (DrawPolyMgr::*UnkType_DPMUVFn)(UnkType_PolyVertex *pVertex, float *pOut, int iStage);
typedef int (DrawPolyMgr::*TextureSrcInitFn)(WorldPoly *pPoly, int iStage);
typedef int (DrawPolyMgr::*UnkType_DPMBindFn)(WorldPoly *pPoly, int iStage, int a3);
typedef void (DrawPolyMgr::*UnkType_DPMColorFn)(UnkType_PolyVertex *pVertex, TLVertex *pOut);
typedef void (DrawPolyMgr::*UnkType_DPMPostFn)(UnkType_DPMDrawBuffer *pBuffer);
typedef void (__fastcall *UnkType_DPMFogFn)(LTVector *pPos, uint32 *pSpecular);
typedef int (__fastcall *UnkType_DPMClipFn)(uint32 nFlags, TLVertex **ppVerts, int *pnVerts);

class DrawPolyMgr : public Material
{
public:
	DrawPolyMgr();									// FUN_10029947
	~DrawPolyMgr();									// 0x10029ad0 (the atexit stub is 0x10029ac6)

	void InitTestGouraudMaterial();							// guess: sets up the built-in TEST_GOURAUD material and its instance
	void FlushQueuedPolys();							// guess: Flush: draws every queued polygon, pass by pass (called through g_DrawPolyMgr when the DrawPolyMgr console variable is set)
	void DrawPolyFirstPass(WorldPoly *pPoly, UnkType_DPMPass *pPass, int iNextPass);	// guess: draws the polygon in the first pass and queues it for the next one
	void DrawPolyAdditionalPass(WorldPoly *pPoly, UnkType_DPMPass *pPass, int iNextPass);	// the polygon in the later passes
	void DrawUntexturedBucket(UnkType_DPMNode *pNode);		// guess: draws the polygons of a bucket entry without the material (TextureSrcInitFn failed)
	void QueuePolyForPass(WorldPoly *pPoly, int iPass);	// guess: queues pPoly in the hash bucket of pass iPass (finding or creating the bucket entry)

	// guess: the texture coordinate generators (UnkType_DPMUVFn) of a stage
	void GenerateScaledBaseTexCoords(UnkType_PolyVertex *pVertex, float *pOut, int iStage);
	void CopySecondaryTexCoords(UnkType_PolyVertex *pVertex, float *pOut, int iStage);
	void GeneratePannedPlanarTexCoords(UnkType_PolyVertex *pVertex, float *pOut, int iStage);
	void SetDetailUV(UnkType_PolyVertex *pVertex, float *pOut, int iStage);
	// guess: the combined generators of 2 and 3 stages
	void SetTwoStageUV(UnkType_PolyVertex *pVertex, float *pOut, int iStage);
	void SetThreeStageUV(UnkType_PolyVertex *pVertex, float *pOut, int iStage);

	// guess: the TextureSrcInitFns
	int InitSurfaceTextureSource(WorldPoly *pPoly, int iStage);
	int InitLightmapTextureSource(WorldPoly *pPoly, int iStage);
	int InitDetailTextureSource(WorldPoly *pPoly, int iStage);
	int InitUntexturedSource(WorldPoly *pPoly, int iStage);
	// guess: the texture binders
	int BindSurfaceTexture(WorldPoly *pPoly, int iStage, int a3);
	int BindLightmapTexture(WorldPoly *pPoly, int iStage, int a3);
	int BindDetailTexture(WorldPoly *pPoly, int iStage, int a3);
	int BindNoTexture(WorldPoly *pPoly, int iStage, int a3);
	// guess: the vertex colour functions
	void SetFullbrightColor(UnkType_PolyVertex *pVertex, TLVertex *pOut);
	void SetCorrectedVertexColor(UnkType_PolyVertex *pVertex, TLVertex *pOut);
	void SetScaledVertexColor(UnkType_PolyVertex *pVertex, TLVertex *pOut);
	// guess: the post-draw functions (index m_Unk20 of the first stage): the dynamic lights of the poly are added to the vertices / nothing
	void AddDynamicLightToDrawBuffer(UnkType_DPMDrawBuffer *pBuffer);
	void SkipDrawBufferPostprocess(UnkType_DPMDrawBuffer *pBuffer);
	// guess: sets the render states and the texture stage states of pass pPass from its pass list (the state block)
	void SetPassRenderStates(UnkType_DPMPass *pPass, UnkType_DPMPassList *pList);

	UnkType_DPMMaterialInstance	m_Unk1cc;		// 0x1cc the TEST_GOURAUD material's instance
	uint16				m_Unk768;				// 0x768 guess: the render context's current frame code
	uint8				m_Pad76a[2];
	UnkType_DPMMaterialInstance	*m_Unk76c;		// 0x76c guess: first material instance
	float				m_Unk770[3][2];			// 0x770 guess: u, v scale of each stage's texture
	float				m_Unk788, m_Unk78c;		// 0x788 guess: detail texture u, v scale
	float				m_Unk790, m_Unk794;		// 0x790
	float				m_Unk798, m_Unk79c;		// 0x798
	float				m_Unk7a0, m_Unk7a4, m_Unk7a8;	// 0x7a0 guess: colour scale of the vertex colour function
	UnkType_DPMUVFn		m_Unk7ac, m_Unk7b0, m_Unk7b4;	// 0x7ac guess: texture coordinate generator of stage 0, 1, 2
	UnkType_DPMUVFn		m_Unk7b8;				// 0x7b8 guess: the generator of the current pass
	CMoArray<QVert>		m_Unk7bc;				// 0x7bc
	uint32				m_Unk7d0;				// 0x7d0 guess: number of QVerts used
	ObjectBank<UnkType_DPMNode, NullCS>	m_Unk7d4;	// 0x7d4
};

typedef char DPM_CheckMaterial[(sizeof(Material) == 0x1cc) ? 1 : -1];
typedef char DPM_CheckInstance[(sizeof(UnkType_DPMMaterialInstance) == 0x59c) ? 1 : -1];
typedef char DPM_Check768[(offsetof(DrawPolyMgr, m_Unk768) == 0x768) ? 1 : -1];
typedef char DPM_Check7bc[(offsetof(DrawPolyMgr, m_Unk7bc) == 0x7bc) ? 1 : -1];
typedef char DPM_Check7d4[(offsetof(DrawPolyMgr, m_Unk7d4) == 0x7d4) ? 1 : -1];

// GLOBAL: D3DREN 0x1006ec40
extern DrawPolyMgr g_DrawPolyMgr;

#endif
