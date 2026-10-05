// d3d.ren sys/d3d/drawparticles (0x10008cd0-0x100098d0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// MatMul / d3d_SetupTransformation / LTVector::Init COMDAT copies follow their first caller 0x10008ce0.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unk/10008cd0 (0x10008cd0-0x100098d0): the particle system drawing (Jupiter drawparticles.cpp neighbourhood: the Talon
// version transforms and clips the particle quads itself and draws them as a TL vertex list), plus the out-of-line copies of
// SDK matrix inlines it needed (MatMul, LTVector::Init).  A object (16-byte aligned functions): the module default flags.
// FLAGS: /O2 /Ob2
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
// records of a SharedTexture: see FUN_1001fff0); returns 0 when there is none.
// FUNCTION: D3DREN 0x10009350
void *FUN_10009350(void *pChain, uint8 nStage)
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
void FUN_10007a89(RTexture *pRTexture);											// unit unk/10007930 (pool.h): binds the RTexture on its stage
RTexture *FUN_1001fff0(SharedTexture *pTexture, uint32 nStage, uint8 bChild);		// unit sys/d3d/d3d_texture
extern uint16 DAT_100577b8;		// guess: current texture frame code (declared by unit unk/10007930)
// GLOBAL: D3DREN 0x1004ffc0
extern float DAT_1004ffc0;		// guess: colour scale (1/255) of the particle red channel; DAT_1004ffc4: green, DAT_1004ffc8: blue
// GLOBAL: D3DREN 0x1004ffc4
extern float DAT_1004ffc4;
// GLOBAL: D3DREN 0x1004ffc8
extern float DAT_1004ffc8;
// GLOBAL: D3DREN 0x1004ffcc
extern float DAT_1004ffcc;		// guess: u of the left texel edge of the particle texture, DAT_1004ffd0: u of the right edge
// GLOBAL: D3DREN 0x1004ffd0
extern float DAT_1004ffd0;
// GLOBAL: D3DREN 0x1004ffd4
extern float DAT_1004ffd4;		// guess: specular colour of every particle vertex
// GLOBAL: D3DREN 0x1004ffd8
extern float DAT_1004ffd8;		// guess: v of the top texel edge, DAT_100513dc: v of the bottom edge
// GLOBAL: D3DREN 0x100513dc
extern float DAT_100513dc;
// GLOBAL: D3DREN 0x10055cd8
extern int DAT_10055cd8;		// guess: statistics: particles (quads) drawn this frame
extern uint16 DAT_1006d1b8[0x300];	// the index list of the quads (0 1 2 0 2 3 ...); declared by unit unk/10029660
PSParticle *FUN_10009370(LTParticleSystem *pSystem, PSParticle *pParticle, int nCount, LTMatrix *pMat, int nMode, float fSize);

// guess: draws a particle system: binds its texture (the inlined texture binding with the per-stage record search FUN_10009350), builds
// the object to view matrix, and draws the particles in batches of 128 with FUN_10009370.  Its typed locals match the SDK
// IntersectQuery (52 bytes) and IntersectInfo (40 bytes); the renderer's constructor copies are at 0x1000bdeb and 0x1000be28.
// The surrounding function body remains a STUB.
// FUNCTION: D3DREN 0x10009020 ?MatMul@@YAXPAVLTMatrix@@00@Z
// STUB: D3DREN 0x10008ce0
void FUN_10008ce0(LTParticleSystem *pSystem)
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
			pTexture->m_Unknown30 = DAT_100577b8;
			if (pTexture->m_pRenderData)
				pRTexture = (RTexture *)FUN_10009350(pTexture->m_pRenderData, (uint8)nStage);
			if (!pRTexture || pRTexture != (RTexture *)g_pBoundTextures[nStage])
			{
				if (!pRTexture)
				{
					RTexture *pFirst = (RTexture *)pTexture->m_pRenderData;
					if (!pFirst)
					{
						pRTexture = FUN_1001fff0(pTexture, nStage, 0);
					}
					else
					{
						pRTexture = FUN_1001fff0(pTexture, nStage, 1);
						if (pRTexture)
						{
							pRTexture->m_Unk30 = pFirst->m_Unk30;
							pFirst->m_Unk30 = pRTexture;
						}
					}
				}
				if (pRTexture)
					FUN_10007a89(pRTexture);
			}
		}

		if (!pRTexture)
		{
			DAT_100513dc = 0.0f;
			DAT_1004ffd0 = 0.0f;
			DAT_1004ffd8 = 0.0f;
			DAT_1004ffcc = 0.0f;
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
			DAT_1004ffcc = DAT_10061810[0].m_Unk00 + DAT_10061810[0].m_Unk00;
			pBase = (RTextureBase *)g_pBoundTextures[g_NormalTextureStage];
			DAT_1004ffd0 = ((float)(uint32)pBase->GetBaseWidth() - 2.0f) * DAT_10061810[0].m_Unk00;
			DAT_1004ffd8 = DAT_10061810[0].m_Unk04 + DAT_10061810[0].m_Unk04;
			DAT_100513dc = ((float)(uint32)pBase->GetBaseHeight() - 2.0f) * DAT_10061810[0].m_Unk04;
			DAT_10063c90.FUN_10021da6();
			if (pTexture->m_pStateChange)
				DAT_10063c90.FUN_10021db7(pTexture->m_pStateChange, g_NormalTextureStage);
		}

		d3d_SetupTransformation(&pSystem->m_Pos, (float *)&pSystem->m_Rotation, &pSystem->m_Scale, &mObject);
		if (pSystem->m_Flags & 0x40)
			pView = &g_ViewParams.m_mReallyCloseClipTransform;
		else
			pView = &g_ViewParams.m_mClipTransform;
		MatMul(&mFull, pView, &mObject);
		g_pfnCalcFogAlpha(&pSystem->m_Pos, (uint32 *)&DAT_1004ffd4);
		DAT_1004ffc0 = 1.0f / 255.0f;
		DAT_1004ffc4 = 1.0f / 255.0f;
		DAT_1004ffc8 = 1.0f / 255.0f;

		nBatches = pSystem->m_nParticles / 128;
		nRest = pSystem->m_nParticles - nBatches * 128;
		pParticle = pSystem->m_ParticleHead.m_pNext;
		for (i = nBatches; i > 0; i--)
			pParticle = FUN_10009370(pSystem, pParticle, 128, &mFull, 1, 0.0f);
		if (nRest)
			FUN_10009370(pSystem, pParticle, nRest, &mFull, 1, 0.0f);
	}
}

// guess: draws nCount particles of a system starting at pParticle: transforms each particle to camera space and to screen space, drops
// those outside the view volume, builds a screen aligned quad of 4 TL vertices per particle (size in pixels from the particle size or
// fSize, colour from the system colour and the particle colour / alpha, the texture coordinates clipped together with the quad against the
// screen rectangle), and draws the quads with DrawIndexedPrimitive; returns the particle after the last one.  Not iterated against the exe yet.
// STUB: D3DREN 0x10009370
PSParticle *FUN_10009370(LTParticleSystem *pSystem, PSParticle *pParticle, int nCount, LTMatrix *pMat, int nMode, float fSize)
{
	TLVertex aVerts[0x80 * 4];
	TLVertex *pOut;
	float fRed, fGreen, fBlue, fAlpha;
	float fHalfSize, fHalfSizeBase;
	float *m = &pMat->m[0][0];

	pOut = aVerts;
	fRed = (float)pSystem->m_ColorR * DAT_1004ffc0;
	fGreen = (float)pSystem->m_ColorG * DAT_1004ffc4;
	fBlue = (float)pSystem->m_ColorB * DAT_1004ffc8;
	fAlpha = (float)pSystem->m_ColorA;
	fHalfSizeBase = (float)(g_ViewParams.m_Rect.right - g_ViewParams.m_Rect.left) * g_ViewParams.m_fFovXScale;
	fHalfSize = fHalfSizeBase * fSize;

	if (nMode == 1 && nCount)
	{
		int n = nCount;

		do
		{
			float fX = pParticle->m_Pos.x, fY = pParticle->m_Pos.y, fZ = pParticle->m_Pos.z;
			float fInvW = 1.0f / (m[13] * fY + m[12] * fX + m[14] * fZ + m[15]);
			float vx = (m[1] * fY + fZ * m[2] + m[0] * fX + m[3]) * fInvW;
			float vy = (fZ * m[6] + m[4] * fX + fY * m[5] + m[7]) * fInvW;
			float vz = (fZ * m[10] + m[8] * fX + fY * m[9] + m[11]) * fInvW;
			float fLimit = vz * 0.5f;

			if (g_ViewParams.m_NearZ < vz && vz < g_ViewParams.m_FarZ && -fLimit < vx && vx < fLimit && vy < fLimit && -fLimit < vy)
			{
				LTMatrix &mP = g_ViewParams.m_DeviceTimesProjection;
				float fW = 1.0f / (mP.m[3][2] * vz + mP.m[3][0] * vx + mP.m[3][1] * vy + mP.m[3][3]);
				float sx = (mP.m[0][2] * vz + mP.m[0][0] * vx + mP.m[0][1] * vy + mP.m[0][3]) * fW;
				float sy = (mP.m[1][2] * vz + mP.m[1][0] * vx + mP.m[1][1] * vy + mP.m[1][3]) * fW;
				float sz = (mP.m[2][2] * vz + mP.m[2][0] * vx + mP.m[2][1] * vy + mP.m[2][3]) * fW;
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
				u0 = DAT_1004ffcc;
				v0 = DAT_1004ffd8;
				u1 = DAT_1004ffd0;
				v1 = DAT_100513dc;
				if (x0 < g_ViewParams.m_fScreenMinX)
				{
					u0 = (DAT_1004ffd0 - DAT_1004ffcc) * ((g_ViewParams.m_fScreenMinX - x0) / (x1 - x0)) + DAT_1004ffcc;
					x0 = g_ViewParams.m_fScreenMinX;
				}
				if (y0 < g_ViewParams.m_fScreenMinY)
				{
					v0 = (DAT_100513dc - DAT_1004ffd8) * ((g_ViewParams.m_fScreenMinY - y0) / (y1 - y0)) + DAT_1004ffd8;
					y0 = g_ViewParams.m_fScreenMinY;
				}
				u1 = DAT_1004ffd0;
				if (g_ViewParams.m_fScreenMaxX < x1)
				{
					u1 = (DAT_1004ffd0 - u0) * ((g_ViewParams.m_fScreenMaxX - x0) / (x1 - x0)) + u0;
					x1 = g_ViewParams.m_fScreenMaxX;
				}
				if (g_ViewParams.m_fScreenMaxY < y1)
				{
					v1 = (DAT_100513dc - v0) * ((g_ViewParams.m_fScreenMaxY - y0) / (y1 - y0)) + v0;
					y1 = g_ViewParams.m_fScreenMaxY;
				}

				pOut[0].m_Vec.x = x0; pOut[0].m_Vec.y = y0; pOut[0].m_Vec.z = sz; pOut[0].rhw = fW; pOut[0].color = dwColor; pOut[0].specular = (uint32)DAT_1004ffd4; pOut[0].tu = u0; pOut[0].tv = v0;
				pOut[1].m_Vec.x = x1; pOut[1].m_Vec.y = y0; pOut[1].m_Vec.z = sz; pOut[1].rhw = fW; pOut[1].color = dwColor; pOut[1].specular = (uint32)DAT_1004ffd4; pOut[1].tu = u1; pOut[1].tv = v0;
				pOut[2].m_Vec.x = x1; pOut[2].m_Vec.y = y1; pOut[2].m_Vec.z = sz; pOut[2].rhw = fW; pOut[2].color = dwColor; pOut[2].specular = (uint32)DAT_1004ffd4; pOut[2].tu = u1; pOut[2].tv = v1;
				pOut[3].m_Vec.x = x0; pOut[3].m_Vec.y = y1; pOut[3].m_Vec.z = sz; pOut[3].rhw = fW; pOut[3].color = dwColor; pOut[3].specular = (uint32)DAT_1004ffd4; pOut[3].tu = u0; pOut[3].tv = v1;
				pOut += 4;
			}
			pParticle = pParticle->m_pNext;
		} while (--n);

		if (pOut != aVerts)
		{
			int nQuads = (int)(pOut - aVerts) >> 2;

			DAT_10055cd8 += nQuads;
			g_pD3DDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0x1c4, aVerts, (DWORD)(pOut - aVerts), DAT_1006d1b8, nQuads * 6, 0);
		}
	}
	return pParticle;
}
