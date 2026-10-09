// d3d.ren sys/d3d/drawparticles (0x10008cd0-0x100098d0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// MatMul / d3d_SetupTransformation / LTVector::Init COMDAT copies follow their first caller 0x10008ce0.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unk/10008cd0 (0x10008cd0-0x100098d0): the particle system drawing (Jupiter drawparticles.cpp neighbourhood: the Talon
// version transforms and clips the particle quads itself and draws them as a TL vertex list), plus the out-of-line copies of
// SDK matrix inlines it needed (MatMul, LTVector::Init).  A object (16-byte aligned functions): the module default flags.
// FLAGS: /O2 /Ob2
#define D3DREN_FINDRTEXTURE_EXTERN	// d3d_texture.h: this object defines the out-of-line d3d_FindRTextureForStage (0x10009350)
#include <windows.h>
#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "ltquatbase.h"
#include "d3dren/3d_ops.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3dstate.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/tlvertex.h"
#include "d3dren/polydraw.h"
#include "d3dren/fixedpoint.h"
#include "d3dren/d3dtexture.h"
#include "de_objects.h"
#include "de_world.h"
#include "counter.h"

// guess: a global whose constructor does nothing (the static initialiser of the object is a single ret).
struct UnkType_EmptyCtor
{
	UnkType_EmptyCtor() {}
	uint32 m_Unk00;
};
// FUNCTION: D3DREN 0x10008cd0 _$E2
UnkType_EmptyCtor g_Unk;

// ---- out-of-line SDK inline copies ---------------------------------------------------------------------------------------

// d3d_SetupTransformation is the inline of 3d_ops.h; this object holds the exe's out-of-line copy (0x10009240), forced by the pointer.
// FUNCTION: D3DREN 0x10009240 ?d3d_SetupTransformation@@YAXPBV?$_CVector@M@@PAMPAV1@PAVLTMatrix@@@Z
// STANDIN: forces the out-of-line copy of d3d_SetupTransformation (3d_ops.h); the callers of the other objects call this copy
void (*g_pfnSetupTransformation)(const LTVector *, float *, LTVector *, LTMatrix *) = d3d_SetupTransformation;

// guess: finds the record whose stage word (+0x42) equals nStage in the chain linked through +0x30 (the per-texture stage
// records of a SharedTexture: see d3d_CreateAndLoadTexture); returns 0 when there is none.
// FUNCTION: D3DREN 0x10009350
void *d3d_FindRTextureForStage(void *pChain, uint8 nStage)
{
	uint8 *pRec = (uint8 *)pChain;
	while (pRec)
	{
		if (*(uint16 *)(pRec + 0x42) == nStage)
			return pRec;
		pRec = *(uint8 **)(pRec + 0x30);
	}
	return 0;
}

// ---- the particle system draw ---------------------------------------------------------------------------------------------------
// callees and globals of other units
void d3d_BindRTexture(RTexture *pRTexture);											// unit unk/10007930 (pool.h): binds the RTexture on its stage
RTexture *d3d_CreateAndLoadTexture(SharedTexture *pTexture, uint32 nStage, uint8 bChild);		// unit sys/d3d/d3d_texture
extern uint16 g_CurTextureFrameCode;		// guess: current texture frame code (declared by unit unk/10007930)
// GLOBAL: D3DREN 0x1004ffc0
extern float g_fParticleRedScale;		// guess: colour scale (1/255) of the particle red channel; g_fParticleGreenScale: green, g_fParticleBlueScale: blue
// GLOBAL: D3DREN 0x1004ffc4
extern float g_fParticleGreenScale;
// GLOBAL: D3DREN 0x1004ffc8
extern float g_fParticleBlueScale;
// GLOBAL: D3DREN 0x1004ffcc
extern float g_fParticleTextureUMin;		// guess: u of the left texel edge of the particle texture, g_fParticleTextureUMax: u of the right edge
// GLOBAL: D3DREN 0x1004ffd0
extern float g_fParticleTextureUMax;
// GLOBAL: D3DREN 0x1004ffd4
extern uint32 g_dwParticleFogSpecular;		// packed fog/specular colour: callback writes uint32; original 0x100097b0 copies the bits
// GLOBAL: D3DREN 0x1004ffd8
extern float g_fParticleTextureVMin;		// guess: v of the top texel edge, g_fParticleTextureVMax: v of the bottom edge
// GLOBAL: D3DREN 0x100513dc
extern float g_fParticleTextureVMax;
// GLOBAL: D3DREN 0x10055cd8
extern int g_nParticlesDrawn;		// guess: statistics: particles (quads) drawn this frame
PSParticle *d3d_DrawParticleBatch(LTParticleSystem *pSystem, PSParticle *pParticle, int nCount, LTMatrix *pMat, int nMode, float fSize);

// guess: draws a particle system: binds its texture (the inlined texture binding with the per-stage record search d3d_FindRTextureForStage), builds
// the object to view matrix, and draws the particles in batches of 128 with d3d_DrawParticleBatch.  Its typed locals match the SDK
// IntersectQuery (52 bytes) and IntersectInfo (40 bytes); the renderer's constructor copies are at 0x1000bdeb and 0x1000be28.
// The surrounding function body remains a STUB.
// FUNCTION: D3DREN 0x10009020 ?MatMul@@YAXPAVLTMatrix@@00@Z
// STUB: D3DREN 0x10008ce0
void d3d_DrawParticleSystem(LTParticleSystem *pSystem)
{
	IntersectQuery query;
	IntersectInfo info;
	LTMatrix mObject;
	LTMatrix mFull;
	uint32 nStage;
	SharedTexture *pTexture;
	RTexture *pRTexture;
	int nBatches, nRest;
	PSParticle *pParticle;
	LTMatrix *pView;
	int i;


	{
		CountAdder cTimer(g_pSceneDesc->m_pTicks_Render_ParticleSystems);

		nStage = g_NormalTextureStage;
		pTexture = pSystem->m_pCurTexture;
		pRTexture = 0;
		if (pTexture)
		{
			pTexture->m_Unknown30 = g_CurTextureFrameCode;
			if (pTexture->m_pRenderData)
				pRTexture = (RTexture *)d3d_FindRTextureForStage(pTexture->m_pRenderData, (uint8)nStage);
			if (!pRTexture || pRTexture != (RTexture *)g_pBoundTextures[nStage])
			{
				if (!pRTexture)
				{
					RTexture *pFirst = (RTexture *)pTexture->m_pRenderData;
					if (!pFirst)
					{
						pRTexture = d3d_CreateAndLoadTexture(pTexture, nStage, 0);
					}
					else
					{
						pRTexture = d3d_CreateAndLoadTexture(pTexture, nStage, 1);
						if (pRTexture)
						{
							pRTexture->m_Unk30 = pFirst->m_Unk30;
							pFirst->m_Unk30 = pRTexture;
						}
					}
				}
				if (pRTexture)
					d3d_BindRTexture(pRTexture);
			}
		}

		if (!pRTexture)
		{
			g_fParticleTextureVMax = 0.0f;
			g_fParticleTextureUMax = 0.0f;
			g_fParticleTextureVMin = 0.0f;
			g_fParticleTextureUMin = 0.0f;
			if (g_pBoundTextures[nStage])
			{
				g_pD3DDevice->SetTexture(nStage, 0);
				g_pBoundTextures[nStage] = 0;
			}
		}
		else
		{
			RTextureBase *pBase;

			if (pRTexture->m_Unk44)
			{
				pRTexture->m_Data.m_pSurface->SetLOD(0);
				pRTexture->m_Unk44 = 0;
			}
			g_fParticleTextureUMin = g_TextureStageTexelSizes[0].m_Unk00 + g_TextureStageTexelSizes[0].m_Unk00;
			pBase = (RTextureBase *)g_pBoundTextures[g_NormalTextureStage];
			g_fParticleTextureUMax = ((float)(uint32)pBase->GetBaseWidth() - 2.0f) * g_TextureStageTexelSizes[0].m_Unk00;
			g_fParticleTextureVMin = g_TextureStageTexelSizes[0].m_Unk04 + g_TextureStageTexelSizes[0].m_Unk04;
			g_fParticleTextureVMax = ((float)(uint32)pBase->GetBaseHeight() - 2.0f) * g_TextureStageTexelSizes[0].m_Unk04;
			g_TextureStateRestorer.RestoreAllStates();
			if (pTexture->m_pStateChange)
				g_TextureStateRestorer.ApplyStateChange(pTexture->m_pStateChange, g_NormalTextureStage);
		}

		d3d_SetupTransformation(&pSystem->m_Pos, (float *)&pSystem->m_Rotation, &pSystem->m_Scale, &mObject);
		if (pSystem->m_Flags & 0x40)
			pView = &g_ViewParams.m_mReallyCloseClipTransform;
		else
			pView = &g_ViewParams.m_mClipTransform;
		MatMul(&mFull, pView, &mObject);
		g_pfnCalcFogAlpha(&pSystem->m_Pos, &g_dwParticleFogSpecular);
		g_fParticleRedScale = 1.0f / 255.0f;
		g_fParticleGreenScale = 1.0f / 255.0f;
		g_fParticleBlueScale = 1.0f / 255.0f;

		nBatches = pSystem->m_nParticles / 128;
		nRest = pSystem->m_nParticles - nBatches * 128;
		pParticle = pSystem->m_ParticleHead.m_pNext;
		for (i = nBatches; i > 0; i--)
			pParticle = d3d_DrawParticleBatch(pSystem, pParticle, 128, &mFull, 1, 0.0f);
		if (nRest)
			d3d_DrawParticleBatch(pSystem, pParticle, nRest, &mFull, 1, 0.0f);
	}
}

// guess: draws nCount particles of a system starting at pParticle: transforms each particle to camera space and to screen space, drops
// those outside the view volume, builds a screen aligned quad of 4 TL vertices per particle (size in pixels from the particle size or
// fSize, colour from the system colour and the particle colour / alpha, the texture coordinates clipped together with the quad against the
// screen rectangle), and draws the quads with DrawIndexedPrimitive; returns the particle after the last one.
// Recovered from the original: 1.2f view-volume limit, packed uint32 fog/specular colour, and once-per-call UV endpoint loads.
// The camera and screen transforms are the SDK's MatVMul_H (aligned 435 -> 324).
// Still not byte-matched: the frame (0x4064 vs 0x4080), spills, the term order of the camera transform's x/y/z rows (the exe's
// w row is MatVMul_H's) and the vertex-store schedule differ.
// STUB: D3DREN 0x10009370
PSParticle *d3d_DrawParticleBatch(LTParticleSystem *pSystem, PSParticle *pParticle, int nCount, LTMatrix *pMat, int nMode, float fSize)
{
	TLVertex aVerts[0x80 * 4];
	TLVertex *pOut;
	float fRed, fGreen, fBlue, fAlpha;
	float fHalfSize, fHalfSizeBase;
	// Original UV endpoints are loaded once at 0x100093b6..0x10009405.
	float fBaseU0 = g_fParticleTextureUMin;
	float fBaseU1 = g_fParticleTextureUMax;
	float fBaseV0 = g_fParticleTextureVMin;
	float fBaseV1 = g_fParticleTextureVMax;


	pOut = aVerts;
	fRed = (float)pSystem->m_ColorR * g_fParticleRedScale;
	fGreen = (float)pSystem->m_ColorG * g_fParticleGreenScale;
	fBlue = (float)pSystem->m_ColorB * g_fParticleBlueScale;
	fAlpha = (float)pSystem->m_ColorA;
	fHalfSizeBase = (float)(g_ViewParams.m_Rect.right - g_ViewParams.m_Rect.left) * g_ViewParams.m_fFovXScale;
	fHalfSize = fHalfSizeBase * fSize;

	if (nMode == 1 && nCount)
	{
		int n = nCount;

		do
		{
			LTVector vCam;
			MatVMul_H(&vCam, pMat, &pParticle->m_Pos);
			float vx = vCam.x, vy = vCam.y, vz = vCam.z;
			// The original fmul at 0x100094d1 reads 1.2f from 0x100461e8.
			float fLimit = vz * 1.2f;

			if (g_ViewParams.m_NearZ < vz && vz < g_ViewParams.m_FarZ && -fLimit < vx && vx < fLimit && vy < fLimit && -fLimit < vy)
			{
				LTVector vScreen;
				float fW = MatVMul_H(&vScreen, &g_ViewParams.m_DeviceTimesProjection, &vCam);
				float sx = vScreen.x, sy = vScreen.y, sz = vScreen.z;
				float fExtent, x0, x1, y0, y1, u0, u1, v0, v1;
				uint32 dwColor;

				if (fSize == 0.0f)
					fHalfSize = fHalfSizeBase * pParticle->m_Size;
				fExtent = fW * fHalfSize;
				dwColor = ((uint32)RoundFloatToInt(fAlpha * pParticle->m_Alpha) << 24) |
					(((RoundFloatToInt(fRed * pParticle->m_Color.x) << 8 | RoundFloatToInt(fGreen * pParticle->m_Color.y)) << 8) | RoundFloatToInt(fBlue * pParticle->m_Color.z));
				x0 = sx - fExtent;
				y0 = sy - fExtent;
				x1 = sx + fExtent;
				y1 = sy + fExtent;
				u0 = fBaseU0;
				v0 = fBaseV0;
				u1 = fBaseU1;
				v1 = fBaseV1;
				if (x0 < g_ViewParams.m_fScreenMinX)
				{
					u0 = (fBaseU1 - fBaseU0) * ((g_ViewParams.m_fScreenMinX - x0) / (x1 - x0)) + fBaseU0;
					x0 = g_ViewParams.m_fScreenMinX;
				}
				if (y0 < g_ViewParams.m_fScreenMinY)
				{
					v0 = (fBaseV1 - fBaseV0) * ((g_ViewParams.m_fScreenMinY - y0) / (y1 - y0)) + fBaseV0;
					y0 = g_ViewParams.m_fScreenMinY;
				}
				u1 = fBaseU1;
				if (g_ViewParams.m_fScreenMaxX < x1)
				{
					u1 = (fBaseU1 - u0) * ((g_ViewParams.m_fScreenMaxX - x0) / (x1 - x0)) + u0;
					x1 = g_ViewParams.m_fScreenMaxX;
				}
				if (g_ViewParams.m_fScreenMaxY < y1)
				{
					v1 = (fBaseV1 - v0) * ((g_ViewParams.m_fScreenMaxY - y0) / (y1 - y0)) + v0;
					y1 = g_ViewParams.m_fScreenMaxY;
				}

				pOut[0].m_Vec.x = x0; pOut[0].m_Vec.y = y0; pOut[0].m_Vec.z = sz; pOut[0].rhw = fW; pOut[0].color = dwColor; pOut[0].specular = g_dwParticleFogSpecular; pOut[0].tu = u0; pOut[0].tv = v0;
				pOut[1].m_Vec.x = x1; pOut[1].m_Vec.y = y0; pOut[1].m_Vec.z = sz; pOut[1].rhw = fW; pOut[1].color = dwColor; pOut[1].specular = g_dwParticleFogSpecular; pOut[1].tu = u1; pOut[1].tv = v0;
				pOut[2].m_Vec.x = x1; pOut[2].m_Vec.y = y1; pOut[2].m_Vec.z = sz; pOut[2].rhw = fW; pOut[2].color = dwColor; pOut[2].specular = g_dwParticleFogSpecular; pOut[2].tu = u1; pOut[2].tv = v1;
				pOut[3].m_Vec.x = x0; pOut[3].m_Vec.y = y1; pOut[3].m_Vec.z = sz; pOut[3].rhw = fW; pOut[3].color = dwColor; pOut[3].specular = g_dwParticleFogSpecular; pOut[3].tu = u0; pOut[3].tv = v1;
				pOut += 4;
			}
			pParticle = pParticle->m_pNext;
		} while (--n);

		if (pOut != aVerts)
		{
			int nQuads = (int)(pOut - aVerts) >> 2;

			g_nParticlesDrawn += nQuads;
			g_pD3DDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0x1c4, aVerts, (DWORD)(pOut - aVerts), g_ParticleQuadIndices, nQuads * 6, 0);
		}
	}
	return pParticle;
}
