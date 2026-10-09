// d3d.ren model drawing: Talon's `ModelDraw` (the old, member-per-model form of Jupiter's render_a/src/sys/d3d/setupmodel.h
// class ModelDraw, global g_ModelDraw 0x10052980).  Owner: package W1 (unit unk/10001000: the per-piece draw callbacks
// 0x10002050..0x10004270, the skin/light transform 0x10004660 and 0x10005700); packages W4 (setupmodel.cpp: constructor,
// SetupModelLight, DrawModel ... in unk/100098d0) and W7 (drawmodel/drawmodelshadows in unk/100241e0) extend the class by
// splitting m_Pad.. members at their exact offsets (small targeted edits; every member has an offset comment).
//
// NAME: ModelDraw: the object of the static initialiser group `g_ModelDraw` (Jupiter setupmodel.cpp `ModelDraw g_ModelDraw;`,
// methods CallModelHook/StaticLightCB/SetupModelLight/GetDirLightAmount of the same class are in this DLL), medium confidence.
// Model, ModelPiece, PieceLOD, ModelVert, ModelTri, NewVertexWeight, ModelInstance, ModelHookData, SharedTexture:
// the engine-side decomp (include/model.h, de_objects.h, de_world.h; layouts shared with lithtech.exe).  Every other member
// name is invented (m_Unk<offset>) and the role is in the guess comment.
#ifndef __D3DREN_MODELDRAW_H__
#define __D3DREN_MODELDRAW_H__

#include <stddef.h>
#include "d3dren/vbpool.h"			// <windows.h>, DirectDraw/Direct3D 7, ltdynarray.h
#include "d3dren/tlvertex.h"
#include "model.h"
#include "de_objects.h"

// View of the vertex buffer pool's vtable slot 1 (DrawPrimitive: Draw(device, primitive type, vertex count) -> DrawPrimitiveVB,
// `ret 0xc`).  vbpool.h declares it with two arguments; this view keeps the call correct meanwhile.
class UnkType_VBPoolDrawView
{
public:
	virtual ~UnkType_VBPoolDrawView();
	virtual int Draw(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices);
};

// One of the 16 light slots of the model drawer (0x28 bytes; the 1-byte constructor FUN_1000b9d1 is empty).
// Layout as read by the vertex lighting loop of SkinAndLightPieceVertices: the light position, the squared radius, a direction and a colour.
struct UnkType_ModelLight
{
	UnkType_ModelLight();	// 0x1000b9d1 (empty; defined out of line in unit unk/100098d0 so that the 16-element loop of the ModelDraw constructor calls it)

	LTVector	m_Unk00;		// 0x00 guess: light position in model space
	float		m_Unk0c;		// 0x0c guess: squared radius (the vertex is lit when its squared distance is smaller)
	LTVector	m_Unk10;		// 0x10 guess: direction the normal is dotted with
	LTVector	m_Unk1c;		// 0x1c guess: colour (0-255 scaled)
};

// guess: one texture stage record of the draw state cache (0x24 bytes).  ResetStageRecord (UnkType_StageRecord::ResetStageRecord)
// resets everything except the two slots that the cache constructor sets to -1.   (W4)
struct UnkType_StageRecord
{
	uint32	m_Unk00;
	uint16	m_Unk04;
	uint16	m_Unk06;
	uint32	m_Unk08;
	int		m_Unk0c;	// -1 after the cache constructor
	uint16	m_Unk10;
	uint16	m_Unk12;
	uint32	m_Unk14;
	int		m_Unk18;	// -1 after the cache constructor
	float	m_Unk1c;
	uint32	m_Unk20;

	void ResetStageRecord();
};

// guess: the draw state cache that ModelDraw embeds at +0x6a4 (8 stage records and a matrix, 0x170 bytes); constructor
// FUN_1000b891 (unit unk/100098d0, W4).
struct UnkType_StateCache
{
	UnkType_StateCache();	// FUN_1000b891

	UnkType_StageRecord	m_Unk000[8];	// 0x000
	int					m_Unk120;		// 0x120
	uint32				m_Unk124;		// 0x124
	uint32				m_Unk128;		// 0x128
	LTMatrix			m_Unk12c;		// 0x12c
	uint32				m_Unk16c;		// 0x16c
};

class ShadowLightInfo;		// modelshadow.h (package W7)
struct WorldPoly;

class ModelDraw
{
public:
	// ---- setupmodel.cpp (unit unk/100098d0, package W4) ----
	// 0x1000b7af: defined in the class body (the compiler-generated vector constructor iterator ??_H at 0x10001000 shows that the
	// original header had it in-class); the exe's copy is the out-of-line COMDAT that the static initialiser of g_ModelDraw calls.
	ModelDraw()
	{
		m_Unk38 = -1;
		m_ModelHookData.m_hObject = 0;
		m_ModelHookData.m_Flags = 0;
		m_ModelHookData.m_ObjectFlags = 0;
		m_ModelHookData.m_LightAdd = &m_LightAdd;
		m_ModelHookData.m_ObjectColor = &m_ObjectColor;
		m_ShadowLights[0].Init(0.0f, -1.0f, 0.0f);
		m_ShadowLights[1].Init(-2.0f, -2.0f, -2.0f);
		m_ShadowLights[2].Init(2.0f, -2.0f, -1.0f);
		m_ShadowLights[0].Norm();
		m_ShadowLights[1].Norm();
		m_ShadowLights[2].Norm();
	}
	~ModelDraw();	// 0x1000b9d4

	// The three CMoArrays are real members (m_Unk814 / m_TransformedVerts / m_NodeTransforms below).  The data pointers that the vertex code
	// of unit unk/10001000 reads as plain members are the macros m_Unk82c / m_Unk840 (below the class), so that code does not
	// have to change.  (Unk814() etc. are kept as accessors for code written against the earlier view-accessor form.)
	CMoArray<uint16>	&Unk814()	{ return m_Unk814; }
	CMoArray<TLVertex>	&Unk828()	{ return m_TransformedVerts; }
	CMoArray<LTMatrix>	&Unk83c()	{ return m_NodeTransforms; }

	// NAME: CallModelHook, StaticLightCB, GetDirLightAmount, SetupModelLight: Jupiter setupmodel.h methods of the same names;
	// the Talon versions take no arguments (they work on the members) except StaticLightCB, which is the FindObjInfo callback.
	void	CallModelHook();					// 0x1000ba6e: fills the model hook data of the instance and calls the scene's hook
	static void	StaticLightCB(WorldTreeObj *pObj, void *pUser);	// 0x1000baa4
	void	AddModelLight(LTMatrix *pMat, LTVector *pLightPos, float fRadius, float r, float g, float b, float r2, float g2, float b2, LTVector *pDir, float fFov);	// guess: adds one light to m_Unk3c (colour fades from r,g,b to r2,g2,b2 across the spot cone)
	float	GetDirLightAmount();				// 0x1000be88: the fraction of the sun that reaches the instance (rays at the sky)
	void	SetupModelLight();					// 0x1000c100: transforms, ambient/sun/dynamic/static lights of the instance
	void	DrawFadeSprite(ModelInstance *pInstance, uint8 nAlpha);	// guess: draws the fade sprite of a far away model as a lit quad
	int		GetLODIndexCount();						// guess: 3 * the triangle count of the current LOD of every piece
	void	DrawModel(ModelInstance *pInstance);	// guess: the per-model entry: setup, LOD, cache lookup, lighting, draw
	void	SelectLODAndBlend();						// guess: picks the LOD (m_nLOD) from m_fModelDist and the blend (m_bLODBlend/m_fLODBlend)
	void	BuildProjectedNodeTransforms(LTMatrix *pMat);		// guess: m_Unk840[i] = *pMat * model node transform i
	int		EnsureVertexAndTransformBuffers();						// guess: grows the vertex and node transform arrays to the size of m_pModel; 1 on success

	// The per-piece draw callbacks stored by SelectPieceDrawCallbacks/DrawPiecesWithCallbacks: `this` in ecx, pLOD = the piece's selected level of
	// detail (m_Tris is read at +0x14/+0x18), pVerts = the transformed vertices (stride 0x20) filled by SkinAndLightPieceVertices.
	typedef int (ModelDraw::*PFN_DrawPiece)(PieceLOD *pLOD, TLVertex *pVerts);

	// The function pointer members (the fillers/generators/clippers of unit unk/10001000 are __fastcall; the vertex arguments
	// are TLVertex* or UnkType_TLVertex40* depending on the vertex format, hence void*).
	typedef void (__fastcall *PFN_FillTexCoords)(void *pDest, void *pSrc, float *pUV);	// FillModelBaseTexCoords family
	typedef void (__fastcall *PFN_GenTexCoords)(ModelDraw *pThis, void *pSrc, void *pDest);	// GenerateModelTexCoordsNoOp/10001420/10001490
	typedef void (__fastcall *PFN_CopyVertex)(void *pDest, void *pSrc);					// CopyTLVertex32/10001520
	typedef int (__fastcall *PFN_ClipPolygon)(uint32 flags, void **ppVerts, int *pnVerts);	// ClipModelPolygon32/10001b30

	// guess: draws the triangles of pLOD from the already filled cache pool (m_pPool): DrawPrimitiveVB TRIANGLELIST.
	int DrawPieceCached(PieceLOD *pLOD, TLVertex *pVerts);

	// guess: runs the draw callbacks over the pieces of the model (pfnDrawA draws the pieces whose flag byte m_Unk3c4[i]
	// is 0, pfnDrawB the others).
	void DrawPiecesWithCallbacks(PFN_DrawPiece pfnDrawA, PFN_DrawPiece pfnDrawB, int a3);

	// guess: picks the draw callbacks from the render mode and calls DrawPiecesWithCallbacks.
	void SelectPieceDrawCallbacks(int a1);

	// The other four draw callbacks (not decompiled yet).
	int DrawPieceClippedReallyClose(PieceLOD *pLOD, TLVertex *pVerts);
	int DrawPieceClipped(PieceLOD *pLOD, TLVertex *pVerts);
	int DrawPieceProjected(PieceLOD *pLOD, TLVertex *pVerts);
	int DrawPieceTransformed(PieceLOD *pLOD, TLVertex *pVerts);
	int DrawPieceUntransformed(PieceLOD *pLOD, TLVertex *pVerts);

	// guess: skins, lights and projects the vertices of one piece (see PrepareModelPieceVertices, its only caller).
	void SkinAndLightPieceVertices(PieceLOD *pLOD, PieceLOD *pLOD2, TLVertex *pDest, PFN_GenTexCoords pfnPerVertex, LTMatrix *pTransforms,
		float *pLighting, char bBounds, float *pMin, float *pMax);

	// guess: per piece of the model: skin/light/project it into m_Unk82c and decide which of the draw callback variants it needs.
	void PrepareModelPieceVertices();

	// ---- drawmodel.cpp (0x1002421e-0x10024c8b, package W7) ----
	// guess: draws the 12 edges of the box of the model's current animation dimensions around the instance (ModelBoxes).
	void DrawAnimationDimensionsBox();
	// guess: begin drawing: texturing decision (m_Unk4c8), fill mode save (wireframe on FLAG_MODELWIREFRAME); *pbResult = 1 when the
	// bound texture is a fullbrite one and the object is not alpha blended (0 again when ShowFullbriteModels is set), passed on to SelectPieceDrawCallbacks.
	void BeginModelRenderPass(uint32 *pbResult);
	// guess: binds skin iSkin of the instance on the normal stage (the second/detail texture stages too) before a piece is drawn (called by DrawPiecesWithCallbacks).
	void BindModelSkinTextures(uint32 iSkin);
	// guess: the central model draw (state setup per render mode, DrawModelPassWithVertexCallbacks with the matching vertex fillers, shadows, box).
	void DrawModelRenderPasses();
	// guess: stores the vertex fillers, runs BeginModelRenderPass, PrepareModelPieceVertices (when bTransform) and SelectPieceDrawCallbacks, restores the fill mode.
	void DrawModelPassWithVertexCallbacks(PFN_FillTexCoords pfnTexFill, PFN_GenTexCoords pfnShade, int bTransform);
	// ModelDraw::DrawModelShadows (0x100252c6, unit drawmodelshadows).
	void DrawModelShadows();
	// guess: draws the shadow described by pInfo onto one world polygon (0x10025078, package W7, called per polygon by DrawModelShadows).
	void DrawBlobShadowOnWorldPoly(ShadowLightInfo *pInfo, WorldPoly *pPoly);
	// guess: projected-texture shadow path of DrawModelShadows (g_CV_ModelShadowProj set; 0x1002701e, package W7), nShadows = shadow count.
	void DrawProjectedModelShadows(uint32 nMaxShadows);
	// guess: draws the projected shadow texture described by pInfo onto one world polygon (0x10026d6a, ret 0xc, package W7; fDist = fMaxShadowDist of the light).
	void DrawProjectedShadowOnWorldPoly(ShadowLightInfo *pInfo, WorldPoly *pPoly, float fDist);

	// ---- members (offsets verified in DrawPiecesWithCallbacks, SkinAndLightPieceVertices, PrepareModelPieceVertices and the five callbacks) ----
	Model			*m_pModel;				// 0x000 the model (m_Pieces at +0x34)
	ModelInstance	*m_pInstance;			// 0x004 the instance (m_Flags +0x88 bit 0x40 = really close, m_HiddenPieces +0x24c, m_pSkins +0x1c0)
	LTAnimTracker	*m_Unk008;				// 0x008 &m_pInstance->m_AnimTracker (set by 0x1000d3a7)
	ModelInstanceHookData	m_Unk00c;		// 0x00c filled by the instance's hook function (SDK ModelInstanceHookData: flags, clip plane in world space)
	LTPlane			m_Unk020;				// 0x020 the hook's clip plane transformed into model space (MIH_CLIPPLANE)
	int				m_Unk30;				// 0x030 guess: FLAG_DETAILTEXTURE (1<<3) of the instance, stored by 0x1000d3a7 (selects the detail texture pass of 0x1002476b)
	uint32			m_Unk34;				// 0x034 guess: stage / skin index argument of the state-change saver (ApplyStateChange)
	int				m_Unk38;				// 0x038 (-1 after the constructor)
	UnkType_ModelLight	m_Unk3c[16];		// 0x03c the model lights (16 x 0x28; constructor loop of 0x1000b7af)
	int				m_nModelLights;				// 0x2bc number of lights in use
	uint32			m_nMaxModelLights;				// 0x2c0 maximum number of model lights (min(MaxModelLights, 16))
	uint8			m_Unk2c4[0x100];		// 0x2c4 guess: per piece flag, set when the piece is completely outside a clip plane (not drawn)
	uint8			m_Unk3c4[0x100];		// 0x3c4 guess: per piece flag, set when the piece crosses a clip plane (needs the clipping callback)
	int				m_Unk4c4;				// 0x4c4 guess: set to 1 by DrawModelPassWithVertexCallbacks while it draws, 0 by the env map pass of DrawModelRenderPasses (BindModelSkinTextures does nothing while it is 0)
	int				m_Unk4c8;				// 0x4c8 guess: the model is drawn textured (BeginModelRenderPass: TextureModels && MHF_USETEXTURE)
	int				m_Unk4cc;				// 0x4cc guess: texture currently bound (-1 after the model draw starts)
	LTMatrix		m_ModelTransform;				// 0x4d0 guess: matrix MatVMul'ed with the light positions (0x1002701e: model lights to world space; W7)
	LTMatrix		m_Transform;				// 0x510
	LTMatrix		m_InvTransform;				// 0x550
	LTMatrix		m_EnvMapTransform;				// 0x590
	LTVector		m_Unk5d0;				// 0x5d0 guess: the instance position (really close: transformed by the inverse view)
	void			(*m_Unk5dc)();			// 0x5dc guess: begins the warble of the vertex projection (d3d_BeginModelWarbleProjection / empty d3d_BeginModelProjectionNoOp)
	void			(__fastcall *m_Unk5e0)(ModelVert *pVert, TLVertex *pOut, LTMatrix *pMatrix);	// 0x5e0 guess: projects one model vertex into the TL vertex pOut (d3d_ProjectWarbledModelVertex warbling / d3d_ProjectModelVertex)
	uint8			m_Pad5e4[0x5e8 - 0x5e4];
	uint32			m_Unk5e8;				// 0x5e8 guess: vertex format (1 = 0x20-byte TL vertices, else 0x28-byte); unsigned: BindModelSkinTextures tests `< 2` with jae
	PFN_GenTexCoords	m_Unk5ec;			// 0x5ec guess: per-vertex generator (GenerateModelTexCoordsNoOp / GenerateModelEnvMapCoords / GenerateModelSpecularCoords)
	int				m_Unk5f0;				// 0x5f0
	PFN_FillTexCoords	m_Unk5f4;			// 0x5f4 guess: texture coordinate filler (FillModelBaseTexCoords family)
	int				m_Unk5f8;				// 0x5f8 guess: vertex size in bytes (0x20 / 0x28)
	PFN_CopyVertex	m_Unk5fc;				// 0x5fc guess: vertex copy (CopyTLVertex32 / CopyTLVertex40)
	PFN_ClipPolygon	m_Unk600;				// 0x600 guess: polygon clipper (ClipModelPolygon32 / ClipModelPolygon40)
	int				m_Unk604;				// 0x604 guess: FVF of the current vertex format (0x1c4 / 0x2c4)
	UnkType_VertexBufferPool	*m_Unk608;	// 0x608 the vertex buffer pool of the current vertex format
	uint32			m_nLOD;				// 0x60c guess: level of detail (0 = piece itself, n = m_LODs[n-1])
	int				m_bLODBlend;				// 0x610 guess: LOD blend enabled
	float			m_fLODBlend;				// 0x614 guess: LOD blend amount
	int				m_Unk618;				// 0x618 guess: draw an environment map pass (tested with the EnvMapAll mirror in DrawModelRenderPasses)
	float			m_Unk61c, m_Unk620;		// 0x61c guess: environment map u/v offsets
	float			m_Unk624, m_Unk628;		// 0x624 guess: u/v scales
	float			m_Unk62c;				// 0x62c guess: specular power of the piece
	float			m_Unk630;				// 0x630 guess: specular scale of the piece
	float			m_Unk634;				// 0x634 guess: bounding sphere radius of the instance (Model::m_GlobalRadius * the largest scale), 0x1000b584
	int				m_Unk638;				// 0x638 guess: FLAG_MODELTINT on an opaque (alpha 255) instance and the g_TintModels console mirror set (0x1000d3a7)
	float			m_fModelDist;				// 0x63c guess: distance from the viewer to the instance (divided by ModelZoomScale)
	uint32			m_Unk640;				// 0x640 guess: specular colour word written into every vertex
	LTVector		m_ShadowLights[8];		// 0x644 NAME: Jupiter ModelDraw::m_ShadowLights[NUM_MODEL_SHADOWS] (DrawModelShadows reads and sets element 0; 0x1000b7af fills the first three)
	UnkType_StateCache	m_Unk6a4;			// 0x6a4 guess: draw state cache (constructor FUN_1000b891)
	CMoArray<uint16>	m_Unk814;			// 0x814 guess: (unknown use; set up by the constructor, freed by the destructor)
	CMoArray<TLVertex>	m_TransformedVerts;			// 0x828 guess: the transformed vertices of the current model (data pointer = m_Unk82c)
	CMoArray<LTMatrix>	m_NodeTransforms;			// 0x83c guess: the node transforms of the current model (data pointer = m_Unk840)
	LTVector		m_LightAdd;				// 0x850 guess: light add (ModelHookData::m_LightAdd points here)
	LTVector		m_ObjectColor;				// 0x85c guess: object colour (ModelHookData::m_ObjectColor points here)
	LTVector		m_DirLightColor;				// 0x868 guess: colour scale in full light
	LTVector		m_DirLightDir;				// 0x874 guess: light direction
	float			m_DirLightAmount;				// 0x880 guess: directional light amount
	LTVector		m_AmbientLight;				// 0x884 guess: colour scale in shadow
	ModelHookData	m_ModelHookData;				// 0x890 the model hook data (m_ObjectFlags at 0x898, m_LightAdd 0x89c, m_ObjectColor 0x8a0)
	uint32			m_Unk8a4;				// 0x8a4 guess: third argument (dwMaxLOD) of d3d_SetTexture in BindModelSkinTextures
	uint8			m_Unk8a8;				// 0x8a8 guess: alpha written into every vertex colour
	uint8			m_Pad8a9[0x8ac - 0x8a9];
	int				m_Unk8ac;				// 0x8ac guess: draw mode (2: draw the cached pool, 1: untransformed vertices)
	int				m_Unk8b0;				// 0x8b0 guess: hardware T&L
	int				m_Unk8b4;				// 0x8b4 guess: back face culling
};

// The data pointers of the two CMoArrays that the vertex code of unit unk/10001000 uses (they are the arrays' m_pArray at 0x82c
// and 0x840; the class has real CMoArray members since the constructor/destructor of unit unk/100098d0 handle them).
#define m_Unk82c	m_TransformedVerts.GetArray()
#define m_Unk840	m_NodeTransforms.GetArray()

#define MD_CHECKOFFSET(member, ofs) 	typedef char MD_Check##member[(offsetof(ModelDraw, member) == (ofs)) ? 1 : -1];
MD_CHECKOFFSET(m_Unk814, 0x814)
MD_CHECKOFFSET(m_TransformedVerts, 0x828)
MD_CHECKOFFSET(m_NodeTransforms, 0x83c)
MD_CHECKOFFSET(m_LightAdd, 0x850)
MD_CHECKOFFSET(m_ModelHookData, 0x890)
typedef char MD_CheckSize[(sizeof(ModelDraw) == 0x8b8) ? 1 : -1];

#endif
