// d3d.ren model vertex fillers: small __fastcall functions that the model drawer (DrawPiecesWithCallbacks, UnkType_ModelDrawer in
// modeldraw.h) stores as function pointers and calls per vertex.  Two families, both with the vertex in the destination
// buffer in ecx or on the stack:
//   texture coordinate fillers:  (TLVertex *pDest, void *pSrc, float *pUV)       FillModelBaseTexCoords, 10001390, 100013d0, 100013e0, 10001410
//   normal based generators:     (drawer view *pThis, model vertex *pSrc, TLVertex *pDest)   GenerateModelEnvMapCoords, 10001490
// All type and member names are invented; roles in the guess comments.
#ifndef __D3DREN_VERTFILL_H__
#define __D3DREN_VERTFILL_H__

#include "ltbasedefs.h"
#include "d3dren/tlvertex.h"

// A plain 3 float vector (copied by value into a local by GenerateModelSpecularCoords).
struct UnkType_Vec3
{
	float	x, y, z;
};

// A vertex of the model's own buffer (stride 0x20).  Only the three floats at +0x14 are used by the generators.
struct UnkType_ModelVertex
{
	uint8	m_Pad00[0x14];
	float	m_Unk14;	// 0x14  guess: normal x
	float	m_Unk18;	// 0x18  guess: normal y
	float	m_Unk1c;	// 0x1c  guess: normal z
};

// The members of the model drawer object (UnkType_ModelDrawer) that the generators read.  Partial view: same offsets as
// the drawer, padded.
struct UnkType_ModelDrawerVertexView
{
	uint8	m_Pad000[0x590];
	float	m_EnvMapTransform, m_Unk594, m_Unk598;	// 0x590  guess: first row of the environment map matrix
	uint8	m_Pad59c[4];
	float	m_Unk5a0, m_Unk5a4, m_Unk5a8;	// 0x5a0  guess: second row
	uint8	m_Pad5ac[0x61c - 0x5ac];
	float	m_Unk61c, m_Unk620;				// 0x61c  guess: u/v offsets
	float	m_Unk624, m_Unk628;				// 0x624  guess: u/v scales
	uint8	m_Pad62c[4];
	float	m_Unk630;						// 0x630  guess: specular scale
	uint8	m_Pad634[0x874 - 0x634];
	UnkType_Vec3	m_Unk874;				// 0x874  guess: light direction
};

// GLOBAL: D3DREN 0x1004eb40
extern float g_ModelTextureUOffset;		// guess: u offset added to the model texture coordinates
// GLOBAL: D3DREN 0x1004eb44
extern float g_ModelTextureVOffset;		// guess: v offset

#endif
