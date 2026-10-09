// d3d.ren d3d_texture.h: the shared inline helpers of the texture binding (Jupiter render_a/src/sys/d3d/d3d_texture.h).  One definition for
// every object of the renderer.  In the exe they are `inline` functions of this header: the out-of-line COMDAT copies are 0x100079e4
// (d3d_SetTexture, emitted in the unk/10007930 object) and 0x1000a27b (d3d_DisableTexture, emitted in unk/100098d0); the A objects
// (/O2 /Ob2) expand them, the P objects (/O1) call the copy.  d3d_SetTextureDirect has no out-of-line copy.
//
// NAME: d3d_SetTexture, d3d_DisableTexture, d3d_SetTextureDirect: Jupiter render_a/src/sys/d3d/d3d_texture.h (names_proposal.csv medium
// for 0x100079e4 / 0x1000a27b; the d3d.ren bodies are the Talon DirectDraw 7 form of Jupiter's).
#ifndef __D3DREN_D3D_TEXTURE_H__
#define __D3DREN_D3D_TEXTURE_H__

#include "d3dren/d3ddevice.h"
#include "d3dren/d3dstate.h"
#include "ltlink.h"
#include "de_world.h"			// SharedTexture

class RTexture;

// The members of the renderer side of a texture (RTexture, d3dtexture.h) that the inline helpers and the binding code touch.  A layout-only view
// (including d3dtexture.h here would pull lightmap.h into every object, which changes the code of ClipPolyNear40 / ClipPolyLeft40, 4 bytes each).
struct UnkType_RTexView
{
	void				*m_pVtbl;		// 0x00
	float				m_Unk04;		// 0x04 guess: texture coordinate scale u
	float				m_Unk08;		// 0x08 guess: v
	IDirectDrawSurface7	*m_Unk0c;		// 0x0c the texture surface
	int					m_Unk10;		// 0x10 size in bytes (added to the per-frame texture byte counter)
	uint16				m_Unk14;		// 0x14 frame code of the last use
	uint16				m_Unk16;		// 0x16 alpha reference (0 = none)
	uint8				m_Pad18[4];		// 0x18
	LTLink				m_Unk1c;		// 0x1c link in g_Textures
	uint8				m_Pad28[0x30 - 0x28];
	UnkType_RTexView	*m_Unk30;		// 0x30 next RTexture of the same SharedTexture (one per stage)
	uint8				m_Pad34[0x42 - 0x34];
	uint16				m_Unk42;		// 0x42 device stage
	uint16				m_Unk44;		// 0x44 current LOD
};

// d3d_CreateAndLoadTexture (sys/d3d/d3d_texture): finds or creates the RTexture of pTexture for the stage (bChild: chain it behind the first one).
RTexture *d3d_CreateAndLoadTexture(SharedTexture *pTexture, uint32 nStage, uint8 bChild);
// d3d_BindRTexture (unk/10007930): binds the RTexture on its device stage (LRU list, SetTexture, ALPHAREF, per-stage UV scale, change counter).
void d3d_BindRTexture(RTexture *pRTexture);

// Finds or creates the RTexture of pTexture for the stage, binds it and sets its LOD; returns 0 when there is no texture.
// dwMaxLOD: the argument of IDirectDrawSurface7::SetLOD, which is the only thing the third argument is used for.
// The stage search runs only on a non-empty chain, and the create path reads m_pRenderData again: drawparticles' expansion (search
// called out of line at 0x10008daa) tests the chain head before the call and re-reads it after a failed search.
//
// A unit whose currently matching functions change when the plain inline is expanded (unk/100098d0: ClipPolyNear, ClipPolyLeft; d3d_draw:
// the STLport node allocator copies) defines D3DREN_SETTEXTURE_EXTERN before including this header and keeps calling the out-of-line copy.
#ifdef D3DREN_SETTEXTURE_EXTERN
int d3d_SetTexture(SharedTexture *pTexture, uint32 nStage, uint32 dwMaxLOD);		// 0x100079e4
#else
// NAME: d3d_FindRTextureForStage: the RTexture of stage nStage in the chain of a SharedTexture's renderer textures (linked through
// +0x30, stage word +0x42), or 0.  An inline of this header (d3d_SetTexture expands it); the exe's out-of-line copy is 0x10009350,
// defined in sys/d3d/drawparticles, which defines D3DREN_FINDRTEXTURE_EXTERN before including this header.
#ifdef D3DREN_FINDRTEXTURE_EXTERN
void *d3d_FindRTextureForStage(void *pChain, uint8 nStage);		// 0x10009350
#else
inline void *d3d_FindRTextureForStage(void *pChain, uint8 nStage)
{
	UnkType_RTexView *pRTexture = (UnkType_RTexView *)pChain;
	while (pRTexture && pRTexture->m_Unk42 != nStage)
		pRTexture = pRTexture->m_Unk30;
	return pRTexture;
}
#endif

inline int d3d_SetTexture(SharedTexture *pTexture, uint32 nStage, uint32 dwMaxLOD)
{
	UnkType_RTexView *pRTexture;
	UnkType_RTexView *pFirst;

	if (!pTexture)
		return 0;

	pRTexture = (UnkType_RTexView *)pTexture->m_pRenderData;
	pTexture->m_Unknown30 = g_CurTextureFrameCode;
	if (pRTexture && (pRTexture = (UnkType_RTexView *)d3d_FindRTextureForStage(pRTexture, (uint8)nStage)) != 0)
	{
		if (pRTexture != (UnkType_RTexView *)g_pBoundTextures[nStage])
			goto Bind;
	}
	else
	{
		pFirst = (UnkType_RTexView *)pTexture->m_pRenderData;
		if (pFirst)
		{
			UnkType_RTexView *pNew = (UnkType_RTexView *)d3d_CreateAndLoadTexture(pTexture, nStage, 1);
			if (!pNew)
				return 0;
			pNew->m_Unk30 = pFirst->m_Unk30;
			pFirst->m_Unk30 = pNew;
			pRTexture = pNew;
		}
		else
		{
			pRTexture = (UnkType_RTexView *)d3d_CreateAndLoadTexture(pTexture, nStage, 0);
			if (!pRTexture)
				return 0;
		}
Bind:
		d3d_BindRTexture((RTexture *)pRTexture);
	}

	if (pRTexture->m_Unk44 != dwMaxLOD)
	{
		pRTexture->m_Unk0c->SetLOD(dwMaxLOD);
		pRTexture->m_Unk44 = (uint16)dwMaxLOD;
	}
	return 1;
}
#endif

// Unbinds the texture of device stage nStage.
inline void d3d_DisableTexture(uint32 nStage)
{
	if (g_pBoundTextures[nStage])
	{
		g_pD3DDevice->SetTexture(nStage, 0);
		g_pBoundTextures[nStage] = 0;
	}
}

// Jupiter d3d_SetTextureDirect(pTexture, nStage): a plain IDirect3DDevice7::SetTexture.  The exe has no out-of-line copy: it only shows as an
// extra inline call site (the inline budget of the vector operators in d3d_WarpToScreen3D and DrawModelShadows depends on it).
inline void d3d_SetTextureDirect(IDirectDrawSurface7 *pTexture, uint32 nStage)
{
	g_pD3DDevice->SetTexture(nStage, pTexture);
}

#endif
