// d3d.ren sys/d3d/drawmodel (0x100241e0-0x10025013): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// Ob1 object.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren model drawing: the second half of the original `drawmodel` object (0x100241e0) and the `drawmodelshadows` object
// (0x10025013..0x1002859f: planar and projected model shadows).  Unit name: the proposal's 0x100241e0 (NAMING.md section 4
// names the objects: drawmodel 0x10023860-0x10025012 of which this unit holds the tail, drawmodelshadows 0x10025013-0x1002859f).
// The three console-variable groups sit where the exe has their static initialisers (between the functions).
// (No /Ob2: with it the 20-byte FUN_10024cd7, called only by FUN_10024c8b, is expanded into its caller, the exe calls it.)
// FLAGS NOTE: the object map says /O1 /Ob2 (P object).  FUN_10024c8b matches only with /Ob1 (the exe keeps a callee out of line that
// /Ob2 inlines: an in-object inline-budget effect, not a flag difference between objects); every other function matches under both.
// FLAGS: /O1
// d3d.ren model drawing: the second half of the original `drawmodel` object (0x100241e0) and the `drawmodelshadows` object
// (0x10025013..0x1002859f: planar and projected model shadows).  Unit name: the proposal's 0x100241e0 (NAMING.md section 4
// names the objects: drawmodel 0x10023860-0x10025012 of which this unit holds the tail, drawmodelshadows 0x10025013-0x1002859f).
// The three console-variable groups sit where the exe has their static initialisers (between the functions).
// (No /Ob2: with it the 20-byte FUN_10024cd7, called only by FUN_10024c8b, is expanded into its caller, the exe calls it.)
#include <math.h>
#include <windows.h>
#include <string.h>
#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "de_objects.h"
#include "de_world.h"
#include "world_tree.h"
#include "geomroutines.h"
#include "de_mainworld.h"
#include "d3dren/modeldraw.h"
#include "d3dren/setupmodel.h"
#include "d3dren/modelshadow.h"
#include "d3dren/d3dstate.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3d_surface.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/visibleset.h"
#include "d3dren/drawobjects.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/pool.h"
#include "d3dren/d3d_draw.h"
#include "d3dren/tlvertex.h"
#include "d3dren/vertfill.h"

// FUNCTION: D3DREN 0x100241e0 _$E3
// FUNCTION: D3DREN 0x100241e5 _$E2
// GLOBAL: D3DREN 0x10067c10
ConVar g_CV_ModelSpecular("ModelSpecular", 1.0f);
// FUNCTION: D3DREN 0x100241ff _$E6
// FUNCTION: D3DREN 0x10024204 _$E5
// GLOBAL: D3DREN 0x10067bf0
ConVar g_CV_ModelTexture("ModelTexture", 1.0f);

// ---- declarations of other units -------------------------------------------------------------------------------------------

// FUN_1000b528: d3d_DrawModel / d3d_QueueModel callback of BaseObjectSet::Draw (unit setupmodel, W4).
void FUN_1000b528(ViewParams *pParams, LTObject *pObject);
// 0x100161e0: clips the 3D line pVerts[2] against the planes of the mask (0x3f = all); returns 0 when nothing is left.
int FUN_100161e0(float *pVerts, int nMask);
void d3d_DrawLine(const LTVector &src, const LTVector &dest, uint32 color);
void FUN_10024cd7();

// The static data of the original object that this scratch unit owns (all unnamed in the exe).
// GLOBAL: D3DREN 0x10068030
int DAT_10068030;				// guess: D3DRENDERSTATE_FILLMODE saved by FUN_100244b3, restored by FUN_10024cd7
// GLOBAL: D3DREN 0x10067be8
uint32 DAT_10067be8;			// guess: D3DTOP value of the stage 1 colour op while the model is drawn
// GLOBAL: D3DREN 0x10069034
int DAT_10069034;				// guess: switch of the (never enabled) bump mapped model path of FUN_1002476b
// GLOBAL: D3DREN 0x1004b99c
float DAT_1004b99c = 0.5f;		// guess: bump environment matrix _11
// GLOBAL: D3DREN 0x10069038
float DAT_10069038;				// guess: bump environment matrix _12
// GLOBAL: D3DREN 0x1006903c
float DAT_1006903c;				// guess: bump environment matrix _21
// GLOBAL: D3DREN 0x1004b9a0
float DAT_1004b9a0 = 0.5f;		// guess: bump environment matrix _22

// Static data of other units.
extern float DAT_1005de1c;		// guess: texel scale applied to the stage UV scale pair
struct UnkType_StageUV
{
	float	m_Unk00;
	float	m_Unk04;
};
extern UnkType_StageUV DAT_10061810[8];	// per stage UV scale (unit unk/10007930)
extern IDirectDrawSurface7 *g_pSpecularTexture;	// guess: the specular lookup table texture (d3d_BuildSpecularLookupTexture)
extern float DAT_100561a0;		// guess: ViewParams (+0x4a8) component the bump matrix angle is built from
extern float DAT_100561a8;		// guess: ViewParams (+0x4b0)

// Vertex fillers stored in the drawer (unit unk/10001000, W1); declared exactly as that unit defines them, passed through the
// PFN_ casts of the ModelDraw members.
void __fastcall FUN_10001370(TLVertex *pDest, void *pSrc, float *pUV);
void __fastcall FUN_10001390(UnkType_TLVertex40 *pDest, void *pSrc, float *pUV);
void __fastcall FUN_100013d0(TLVertex *pDest, TLVertex *pSrc, float *pUV);
void __fastcall FUN_100013e0(UnkType_TLVertex40 *pDest, TLVertex *pSrc, float *pUV);
void __fastcall FUN_10001410(void *pDest, void *pSrc, float *pUV);
void __fastcall FUN_10001420(UnkType_ModelDrawerVertexView *pThis, UnkType_ModelVertex *pSrc, TLVertex *pDest);
void __fastcall FUN_10001490(UnkType_ModelDrawerVertexView *pThis, UnkType_ModelVertex *pSrc, TLVertex *pDest);

// Jupiter d3d_device.h
inline DWORD F2DW(FLOAT f) { return *((DWORD *)&f); }

// ---- d3d_DrawLine / FUN_1002421e -----------------------------------------------------------------------------------------

// guess: draws a wireframe box (the animation's dims) around the model instance.
// FUNCTION: D3DREN 0x1002421e
void ModelDraw::FUN_1002421e()
{
	d3d_DisableTexture(g_NormalTextureStage);

	ModelInstance *pInstance = m_pInstance;
	const LTVector &Pos = pInstance->m_Pos;
	const LTVector &Dims = pInstance->m_AnimTracker.GetModel()->GetAnimInfo(pInstance->m_AnimTracker.m_TimeRef.m_Cur.m_iAnim)->m_vDims;

	LTVector verts[8];
	verts[0].Init(Pos.x - Dims.x, Pos.y + Dims.y, Pos.z - Dims.z);
	verts[1].Init(Dims.x + Pos.x, Pos.y + Dims.y, Pos.z - Dims.z);
	verts[2].Init(Dims.x + Pos.x, Pos.y + Dims.y, Dims.z + Pos.z);
	verts[3].Init(Pos.x - Dims.x, Pos.y + Dims.y, Dims.z + Pos.z);
	verts[4].Init(Pos.x - Dims.x, Pos.y - Dims.y, Pos.z - Dims.z);
	verts[5].Init(Dims.x + Pos.x, Pos.y - Dims.y, Pos.z - Dims.z);
	verts[6].Init(Dims.x + Pos.x, Pos.y - Dims.y, Dims.z + Pos.z);
	verts[7].Init(Pos.x - Dims.x, Pos.y - Dims.y, Dims.z + Pos.z);

	StateSet ssZWrite(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
	StateSet ssZRead(D3DRENDERSTATE_ZENABLE, FALSE);

	for (int i = 0; i < 4; i++)
	{
		d3d_DrawLine(verts[i + 4], verts[(i + 1) % 4 + 4], 0xffffffff);
		d3d_DrawLine(verts[i], verts[(i + 1) % 4], 0xffffffff);
		d3d_DrawLine(verts[i], verts[i + 4], 0xffffffff);
	}
}

// NAME: d3d_DrawLine: Jupiter d3d_draw.h/.cpp `d3d_DrawLine(const LTVector &src, const LTVector &dest, uint32 color)` (names_proposal
// medium; the 4 colour overload is 0x10017150 in d3d_draw).  Draws a world space line.
// FUNCTION: D3DREN 0x10024427
void d3d_DrawLine(const LTVector &src, const LTVector &dest, uint32 color)
{
	TLVertex verts[2];

	verts[0].color = verts[1].color = color;
	verts[0].m_Vec = src;
	verts[1].m_Vec = dest;

	MatVMul_InPlace_H(&g_ViewParams.m_mClipTransform, &verts[0].m_Vec);
	MatVMul_InPlace_H(&g_ViewParams.m_mClipTransform, &verts[1].m_Vec);

	if (FUN_100161e0((float *)verts, 0x3f))
	{
		ProjectVertexToScreen((float *)&verts[0], &g_ViewParams);
		ProjectVertexToScreen((float *)&verts[1], &g_ViewParams);
		g_pD3DDevice->DrawPrimitive(D3DPT_LINELIST, D3DFVF_TLVERTEX, verts, 2, 0);
	}
}

// ---- FUN_100244b3 --------------------------------------------------------------------------------------------------------

// guess: starts a model draw: decides whether the model is textured, saves the fill mode (wireframe on request) and
// reports (*pbResult) whether the model fullbrite pass is wanted.
// FUNCTION: D3DREN 0x100244b3
void ModelDraw::FUN_100244b3(uint32 *pbResult)
{
	DAT_1004eb44 = 0.0f;
	DAT_1004eb40 = 0.0f;
	m_Unk4c8 = 0;
	if (DAT_10048750 && (m_ModelHookData.m_Flags & MHF_USETEXTURE))
		m_Unk4c8 = 1;
	else
		d3d_DisableTexture(g_NormalTextureStage);

	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_FILLMODE, (unsigned long *)&DAT_10068030);
	if (m_ModelHookData.m_ObjectFlags & FLAG_MODELWIREFRAME)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_WIREFRAME);

	DWORD dwAlphaBlend;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &dwAlphaBlend);

	uint32 bResult = 0;
	if (DAT_1005c808 && DAT_10048764 && g_pBoundTextures[g_NormalTextureStage] &&
		((RTexture *)g_pBoundTextures[g_NormalTextureStage])->IsFullbrite() && !dwAlphaBlend)
	{
		bResult = 1;
		if (DAT_100584a8)
		{
			bResult = 0;
			d3d_DisableTexture(g_NormalTextureStage);
		}
	}
	*pbResult = bResult;
}

// ---- FUN_10024589 --------------------------------------------------------------------------------------------------------

// guess: binds skin iSkin of the instance on the normal stage (m_Unk34; the stage is disabled when ModelTexture is off or the
// bind fails), on the second texture stage m_Unk38 when it is in use, and the skin's linked (detail) texture on stage 1 when
// m_Unk30 is set, before a piece is drawn (caller FUN_10004270).  Sets the texture coordinate offsets DAT_1004eb40/44 that the
// fillers add.
// FUNCTION: D3DREN 0x10024589
void ModelDraw::FUN_10024589(uint32 iSkin)
{
	if (!m_Unk4c4)
		return;

	if (m_Unk4c8 && iSkin < 4 && m_pInstance->m_pSkins[iSkin])
	{
		SharedTexture *pSkin = m_pInstance->m_pSkins[iSkin];

		if (g_CV_ModelTexture.m_IntVal && d3d_SetTexture(pSkin, m_Unk34, m_Unk8a4))
		{
			// (written `DAT_1005de1c * scale`: that gives the exe's fld c / fld uv / fmul st(1); `scale * DAT_1005de1c` came out as fld c / fld st(0) / fmul uv)
			DAT_1004eb40 = DAT_1005de1c * DAT_10061810[0].m_Unk00;
			DAT_1004eb44 = DAT_1005de1c * DAT_10061810[0].m_Unk04;
		}
		else
		{
			d3d_DisableTexture(g_NormalTextureStage);
		}

		if (m_Unk38 != -1)
			d3d_SetTexture(pSkin, m_Unk38, m_Unk8a4);

		if (m_Unk30 && pSkin->m_pLinkedTexture)
		{
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, DAT_10067be8);
			d3d_SetTexture(pSkin->m_pLinkedTexture, 1, 0);
		}
		else if (m_Unk5e8 < 2)
		{
			g_pD3DDevice->GetTextureStageState(1, D3DTSS_COLOROP, (unsigned long *)&DAT_10067be8);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, 1);
			d3d_DisableTexture(1);
		}

		m_Unk4cc = iSkin;
	}
	else
	{
		d3d_DisableTexture(g_NormalTextureStage);
	}
}

// ---- FUN_100246b7 / FUN_1002473b -----------------------------------------------------------------------------------------

// guess: the stage 1 states FUN_100246b7 saves and FUN_1002473b restores.
struct UnkType_SavedStage1
{
	uint32	m_Unk00;	// D3DTSS_COLOROP
	uint32	m_Unk04;	// D3DTSS_ADDRESS
};

// guess: saves the stage 1 colour op and address mode in *pSaved, then sets additive blending of the stage 1 texture.  Not used
// by the model drawing: its caller is the device creation (0x1001acc0), which uses it for the ValidateDevice test of the
// second stage; it lives in this object because of DAT_10067be8.
// FUNCTION: D3DREN 0x100246b7
void FUN_100246b7(UnkType_SavedStage1 *pSaved)
{
	g_pD3DDevice->GetTextureStageState(1, D3DTSS_COLOROP, (unsigned long *)&pSaved->m_Unk00);
	g_pD3DDevice->GetTextureStageState(1, D3DTSS_ADDRESS, (unsigned long *)&pSaved->m_Unk04);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
	DAT_10067be8 = D3DTOP_ADD;
}

// guess: restores what FUN_100246b7 saved (caller: 0x1001acc0).
// FUNCTION: D3DREN 0x1002473b
void FUN_1002473b(UnkType_SavedStage1 *pSaved)
{
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, pSaved->m_Unk00);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_ADDRESS, pSaved->m_Unk04);
}

// ---- FUN_1002476b --------------------------------------------------------------------------------------------------------

// guess: the central model draw (called once per model by 0x1000d3a7).  Clears the per-piece flag bytes, then draws the pieces:
// first an environment map pass when EnvMapAll (or m_Unk618) is set and the engine holds an environment map texture
// (RenderStruct::m_pEnvMapTexture: FUN_10005700 + FUN_100045a0 with the fillers 0x100013d0/0x10001420, alpha blending on);
// unless EnvMapAll is set the normal pass follows: the bump mapped path (switch DAT_10069034, never set), the specular table
// pass (ModelSpecular, validated device, Model::m_bSpecularEnable, table texture present), the detail texture pass (m_Unk30)
// or the plain pass, each through FUN_10024c8b with its texture coordinate filler.  Then the ALPHABLENDENABLE state is put
// back, the shadows (FLAG_SHADOW) and the model box (ModelBoxes) are drawn and the near z is restored (FLAG_REALLYCLOSE).
// The d3d.ren StageStateSet constructor sets unconditionally, so the four temporaries of the specular pass (constructed and
// destroyed immediately) have no effect: kept as the exe has them.
// FUNCTION: D3DREN 0x1002476b
void ModelDraw::FUN_1002476b()
{
	for (uint32 i = 0; i < 0x100; i++)
	{
		m_Unk3c4[i] = 0;
		m_Unk2c4[i] = 0;
	}

	m_Unk38 = -1;
	m_Unk5e8 = 1;
	m_Unk34 = 0;

	DWORD dwOldAlphaBlend;
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &dwOldAlphaBlend);

	IDirectDrawSurface7 *pOldTex;
	int bTransform = 1;
	if ((DAT_10057dd0 || m_Unk618) && g_pStruct->m_pEnvMapTexture)
	{
		m_Unk5f4 = (PFN_FillTexCoords)FUN_100013d0;
		m_Unk5ec = (PFN_GenTexCoords)FUN_10001420;
		FUN_10005700();
		d3d_SetTexture(g_pStruct->m_pEnvMapTexture, g_NormalTextureStage, 0);
		m_Unk4c4 = 0;
		FUN_100045a0(0);
		bTransform = 0;
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
	}

	if ((float)m_Unk8a8 < 255.0f)
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);

	if (!DAT_10057dd0)
	{
		if (DAT_10069034)
		{
			m_Unk5e8 = 2;
			m_Unk38 = 0x101;
			g_pD3DDevice->GetTexture(2, &pOldTex);
			g_pD3DDevice->SetTexture(2, g_pSpecularTexture);
			StageStateSet ss0(1, D3DTSS_TEXCOORDINDEX, 0);
			StageStateSet ss1(1, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
			StageStateSet ss2(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			StageStateSet ss3(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
			double dAngle = atan2((double)DAT_100561a0, (double)DAT_100561a8);
			DAT_1004b99c = (float)cos(dAngle);
			DAT_10069038 = (float)-sin(dAngle);
			DAT_1006903c = (float)sin(dAngle);
			DAT_1004b9a0 = (float)cos(dAngle);
			StageStateSet ss4(1, D3DTSS_BUMPENVMAT00, F2DW(DAT_1004b99c));
			StageStateSet ss5(1, D3DTSS_BUMPENVMAT01, F2DW(DAT_10069038));
			StageStateSet ss6(1, D3DTSS_BUMPENVMAT10, F2DW(DAT_1006903c));
			StageStateSet ss7(1, D3DTSS_BUMPENVMAT11, F2DW(DAT_1004b9a0));
			StageStateSet ss8(1, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
			StageStateSet ss9(1, D3DTSS_MINFILTER, D3DTFN_LINEAR);
			StageStateSet ss10(2, D3DTSS_TEXCOORDINDEX, 1);
			StageStateSet ss11(2, D3DTSS_COLOROP, D3DTOP_ADD);
			StageStateSet ss12(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			StageStateSet ss13(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
			StageStateSet ss14(2, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
			StageStateSet ss15(2, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
			StageStateSet ss16(2, D3DTSS_MINFILTER, D3DTFN_LINEAR);
			FUN_10024c8b((PFN_FillTexCoords)FUN_100013e0, (PFN_GenTexCoords)FUN_10001490, 1);
			g_pD3DDevice->SetTexture(2, pOldTex);
		}
		else
		{
			if (g_CV_ModelSpecular.m_Unk00 && DAT_1005c80c && m_pModel->m_bSpecularEnable && DAT_10062880)
			{
				m_Unk5e8 = 2;
					g_pD3DDevice->GetTexture(1, &pOldTex);
				g_pD3DDevice->SetTexture(1, g_pSpecularTexture);
				StageStateSet(1, D3DTSS_COLOROP, D3DTOP_ADD);
				StageStateSet(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
				StageStateSet(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
				StageStateSet(1, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
				FUN_10024c8b((PFN_FillTexCoords)FUN_100013e0, (PFN_GenTexCoords)FUN_10001490, 1);
				g_pD3DDevice->SetTexture(1, pOldTex);
			}
			else if (m_Unk30)
			{
				m_Unk5e8 = 2;
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
				DAT_10067be8 = D3DTOP_ADDSIGNED;
				FUN_10024c8b((PFN_FillTexCoords)FUN_10001390, (PFN_GenTexCoords)FUN_10001410, 1);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
				g_pD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			}
			else
			{
				DAT_10067be8 = D3DTOP_DISABLE;
				FUN_10024c8b((PFN_FillTexCoords)FUN_10001370, (PFN_GenTexCoords)FUN_10001410, bTransform);
			}
		}
	}

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, dwOldAlphaBlend);

	if (m_ModelHookData.m_ObjectFlags & FLAG_SHADOW)
		DrawModelShadows();

	if (DAT_100584d0)
		FUN_1002421e();

	if (m_ModelHookData.m_ObjectFlags & FLAG_REALLYCLOSE)
		g_ViewParams.m_NearZ = g_CV_NearZ.m_FloatVal;
}

// ---- FUN_10024c8b / FUN_10024cd7 -----------------------------------------------------------------------------------------

// guess: stores the vertex fillers, begins the draw (FUN_100244b3), transforms the pieces (FUN_10005700) when asked and
// runs the draw callbacks (FUN_100045a0).
// FUNCTION: D3DREN 0x10024c8b
void ModelDraw::FUN_10024c8b(PFN_FillTexCoords pfnTexFill, PFN_GenTexCoords pfnShade, int bTransform)
{
	m_Unk5f4 = pfnTexFill;
	m_Unk5ec = pfnShade;
	m_Unk4c4 = 1;
	uint32 bResult;
	FUN_100244b3(&bResult);
	if (bTransform)
		FUN_10005700();
	FUN_100045a0(bResult);
	FUN_10024cd7();
}

// guess: restores the fill mode FUN_100244b3 saved.
// The exe keeps this 20 byte function out of line (FUN_10024c8b calls it); that is the /Ob1 behaviour of this object (a plain function
// is not an inline candidate), the reason this unit has no /Ob2 in its FLAGS line (with /Ob2 it would be expanded into FUN_10024c8b).
// FUNCTION: D3DREN 0x10024cd7
void FUN_10024cd7()
{
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_FILLMODE, DAT_10068030);
}

// ---- d3d_ProcessModel ----------------------------------------------------------------------------------------------------

// NAME: d3d_ProcessModel: names_proposal high (Jupiter drawmodel.cpp d3d_ProcessModel); g_ObjectHandlers[OT_MODEL].m_ProcessObjectFn.
// FUNCTION: D3DREN 0x10024ceb
void d3d_ProcessModel(LTObject *pObject)
{
	VisibleSet *pVisibleSet = d3d_GetVisibleSet();

	if (pObject->m_ColorA != 0xff)
	{
		pVisibleSet->m_TranslucentModels.Add(pObject);
	}
	else
	{
		if (pObject->m_Flags2 & FLAG2_CHROMAKEY)
			pVisibleSet->m_Unk184.Add(pObject);
		else
			pVisibleSet->m_SolidModels.Add(pObject);
	}
}

// ---- d3d_DrawSolidModels / chromakey -------------------------------------------------------------------------------------

// NAME: d3d_DrawSolidModels: names_proposal medium (Jupiter drawmodel.cpp d3d_DrawSolidModels); Talon draws the models
// immediately instead of queueing a piece list.
// FUNCTION: D3DREN 0x10024d22
void d3d_DrawSolidModels()
{
	if (DAT_10048778)
	{
		BaseObjectSet *pSet = &d3d_GetVisibleSet()->m_SolidModels;
		if (pSet->m_nObjects)
		{
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
			g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
			pSet->Draw(&g_ViewParams, FUN_1000b528);
			g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		}
	}
}

// guess: the same for the chromakey models (Talon only).
// FUNCTION: D3DREN 0x10024dc0
void FUN_10024dc0()
{
	if (DAT_10048778)
	{
		BaseObjectSet *pSet = &d3d_GetVisibleSet()->m_Unk184;
		if (pSet->m_nObjects)
			pSet->Draw(&g_ViewParams, FUN_1000b528);
	}
}

// ---- translucent models ----------------------------------------------------------------------------------------------------

static void d3d_DrawTranslucentModel(ViewParams *pParams, LTObject *pObject);

// NAME: d3d_QueueTranslucentModels: names_proposal high (Jupiter drawmodel.cpp d3d_QueueTranslucentModels).
// FUNCTION: D3DREN 0x10024deb
void d3d_QueueTranslucentModels()
{
	if (DAT_10048778)
	{
		AllocSet *pSet = &d3d_GetVisibleSet()->m_TranslucentModels;
		if (pSet->m_nObjects)
		{
			for (uint32 i = 0; i < pSet->m_nObjects; i++)
				DAT_1006b934->Add(pSet->m_pObjects[i], d3d_DrawTranslucentModel);
		}
	}
}

// NAME: d3d_DrawTranslucentModel: names_proposal medium (Jupiter drawmodel.cpp static d3d_DrawTranslucentModel).
// FUNCTION: D3DREN 0x10024e2e
static void d3d_DrawTranslucentModel(ViewParams *pParams, LTObject *pObject)
{
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);

	uint32 srcBlend, destBlend, dwFog, dwFogColor;
	d3d_GetBlendStates(pObject, srcBlend, destBlend, dwFog, dwFogColor);

	StateSet ssSrc(D3DRENDERSTATE_SRCBLEND, srcBlend);
	StateSet ssDest(D3DRENDERSTATE_DESTBLEND, destBlend);
	StateSet ssFog(D3DRENDERSTATE_FOGENABLE, dwFog);
	StateSet ssFogColor(D3DRENDERSTATE_FOGCOLOR, dwFogColor);

	FUN_1000b528(&g_ViewParams, pObject);

	g_pD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

// d3d_GetBlendStates is the inline of d3d_draw.h; the exe's out-of-line copy (0x10024f8c) is emitted here, the first object that called it.
// FUNCTION: D3DREN 0x10024f8c ?d3d_GetBlendStates@@YAXPAVLTObject@@AAK111@Z

// Same epsilon as Jupiter 3d_ops.h CLIP_EPSILON.
#define CLIP_EPSILON	0.00001f
