// d3d.ren unk/10035917 (0x10035917-0x10035de7): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// BSP segment walk + three tables.
// FLAGS: /O1 /Ob2
// d3d.ren unit unk/10034000 (0x10034000-0x10038830): lightmap pages (64x64 DirectDraw texture pages the polygon lightmaps are
// packed into: "Unable to create (%dx%d) lightmap page.", "Lightmaps paged in %.1f seconds.", "LightAnim_BASE"), the lightmap
// plane table and SetupLMPlaneVectors (engine twin src/shared/lightmap_planes.cpp), the lightmap staging texture pools and
// the queued world polygon drawing, quat_ConvertToMatrix (engine twin src/sdk/ltquatbase.cpp), a BSP segment walk, three
// colour tables, and the pixelformat object (engine twin src/shared/pixelformat.cpp).  Several original objects: size
// objects (packed COMDATs, no padding), so FLAGS /O1 /Ob2 for all of them.
#include <windows.h>
#include <string.h>
#include <mmsystem.h>
#include "d3dren/lightmap.h"
#include "d3dren/common_stuff.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/d3dstate.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/pool.h"
#include "d3dren/polydraw.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/viewparams.h"
#include "d3dren/tlvertex.h"
#include "visquery.h"
#include "ltquatbase.h"
#include <math.h>
#include "world_tree.h"
#include "d3dren/modelshadow.h"

// ---- queued world polygon drawing (the polygons of lightmapped surfaces are queued per texture by QueueLightmappedPoly) --------------
// The queued polys' texture: node -> poly -> surface -> SharedTexture.
#define BUCKET_TEXTURE(pBucket)	(((Surface *)((WorldPoly *)(pBucket)->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture)

// ---- a segment walk through the BSP of a world model that collects the polygons near the segment ------------------------------
// The user record and the polygon list are drawmodelshadows' UnkType_ShadowPolyQuery / UnkType_ShadowPolys (d3dren/modelshadow.h).

// guess: one entry of the walk's explicit stack: the node still to visit and the part of the segment that goes into it
// (plain floats: a member with a constructor would give the static a constructor call, which the exe does not have)
struct UnkType_SegVec
{
	float		x, y, z;
};
struct UnkType_SegStackEntry
{
	Node			*m_Unk00;			// 0x00
	UnkType_SegVec	m_Unk04;			// 0x04
	UnkType_SegVec	m_Unk10;			// 0x10
};

// guess: the static stack of the walk (512 entries, 0x3800 bytes); the empty destructor is what the exe registers with _atexit
struct UnkType_SegStack
{
	~UnkType_SegStack() {}
	UnkType_SegStackEntry	m_Unk00[0x200];
};

// guess: collects the polygons of the world model pObj (not a server object, OT_WORLDMODEL) that lie along the segment of pReq
// within its radius and that faced the camera this frame (the poly's frame tag at +0x46 equals the current frame code).
// Not matching (221 aligned mismatches, 148 ignoring stack offsets; 353 vs 360 instructions).  The user record is drawmodelshadows'
// UnkType_ShadowPolyQuery, and the split point is p1 + (p2 - p1) * t of the current sub-segment (the exe recomputes p2 - p1 every
// iteration; the old form used the first segment's delta).  Remaining: the frame layout (exe 0x8c, the stack pointer in the dead pObj
// slot [ebp+8], pNode in ebx), and block placement: the exe puts the two one-sided `pNode = m_Sides[n]` blocks and the push block after
// the pop code; our compiler keeps them in line for the continue form, the if/else-if form and the inverted-condition form alike.  The exe
// also stores pStack->m_Unk00 twice on the non-splitting path (once in that branch, once in the common tail).
// STUB: D3DREN 0x10035917
void CollectWorldModelSegmentPolys(WorldModelInstance *pObj, UnkType_ShadowPolyQuery *pReq)
{
	static UnkType_SegStack s_Stack;
	UnkType_SegStackEntry *pStack;
	Node *pNode;
	LTVector p1, p2, vDelta, vDir, vMid;
	float d1, d2, t;
	int bSide;
	uint32 i;
	LTPlane *pPlane;
	WorldPoly *pPoly;

	if (*(uint32 *)((uint8 *)pObj + 0x54) != 0 || pObj->m_ObjectType != OT_WORLDMODEL)
		return;

	p1 = pReq->m_vStart;
	p2 = pReq->m_vEnd;
	vDelta = p2 - pReq->m_vStart;
	vDir = vDelta;
	vDir.Norm(1.0f);

	pStack = s_Stack.m_Unk00;
	pNode = pObj->m_pOriginalBsp->GetRootNode();

	for (;;)
	{
		if (!(pNode->m_Flags & (NF_IN | NF_OUT)))
		{
			d1 = pNode->GetPlane()->DistTo(p1);
			d2 = pNode->GetPlane()->DistTo(p2);

			if (d1 > pReq->m_fRadius && d2 > pReq->m_fRadius)
			{
				pNode = pNode->m_Sides[1];
				continue;
			}

			if (d1 < -pReq->m_fRadius && d2 < -pReq->m_fRadius)
			{
				pNode = pNode->m_Sides[0];
				continue;
			}

			bSide = d1 > 0.0f;
			if (d1 - d2 > 0.00001f || d1 - d2 < -0.00001f)
				t = d1 / (d1 - d2);
			else
				t = 0.0f;

			vMid = p1 + (p2 - p1) * t;

			pPoly = pNode->m_pPoly;
			if (pPoly->m_Unk46 == g_CurFrameCode && pReq->m_pPolys->m_nPolys < 0x100)
			{
				pPlane = pNode->GetPlane();
				if (pPlane->DistTo(g_ViewParams.m_Pos) > 1.0f)
				{
					pPlane = pNode->GetPlane();
					if (vDir.Dot(pPlane->m_Normal) < 0.7f)
					{
						if ((vMid - pPoly->m_Center).Mag() < pPoly->m_Radius + pReq->m_fRadius)
						{
							for (i = 0; i < pReq->m_pPolys->m_nPolys; i++)
							{
								if (pReq->m_pPolys->m_Polys[i] == pPoly)
									break;
							}
							if (i >= pReq->m_pPolys->m_nPolys)
							{
								pReq->m_pPolys->m_Polys[i] = pPoly;
								pReq->m_pPolys->m_nPolys++;
							}
						}
					}
				}
			}

			if (pStack < &s_Stack.m_Unk00[0x200])
			{
				if (t > 0.0f && t < 1.0f)
				{
					pStack->m_Unk04 = *(UnkType_SegVec *)&vMid;
					pStack->m_Unk10 = *(UnkType_SegVec *)&p2;
					p2 = vMid;
				}
				else
				{
					pStack->m_Unk04 = *(UnkType_SegVec *)&p1;
					pStack->m_Unk10 = *(UnkType_SegVec *)&p2;
				}

				pStack->m_Unk00 = pNode->m_Sides[bSide == 0];
				pStack++;
				pNode = pNode->m_Sides[bSide];
				continue;
			}
		}

		if (pStack == s_Stack.m_Unk00)
			return;

		pStack--;
		pNode = pStack->m_Unk00;
		p1 = *(LTVector *)&pStack->m_Unk04;
		p2 = *(LTVector *)&pStack->m_Unk10;
	}
}

// ---- colour lookup tables (instances of data classes whose constructors fill them) ---------------------------------------------
// FUNCTION: D3DREN 0x10035ce6 _$E2
// FUNCTION: D3DREN 0x10035ce7 _$E4
UnkType_AddClampTable g_ByteSaturatingAddTable;

// FUNCTION: D3DREN 0x10035cf1
UnkType_AddClampTable::UnkType_AddClampTable()
{
	uint32 i;

	for (i = 0; i < 0x100; i++)
		m_Unk00[i] = (uint8)i;

	for (i = 0; i < 0x40; i++)
		((uint32 *)&m_Unk00[0x100])[i] = 0xffffffff;
}

// FUNCTION: D3DREN 0x10035d13 _$E7
UnkType_MulTable g_ByteMultiplyTable;

// FUNCTION: D3DREN 0x10035d1d
UnkType_MulTable::UnkType_MulTable()
{
	uint32 i, j;

	for (i = 0; i < 0x100; i++)
	{
		for (j = 0; j < 0x100; j++)
			m_Unk00[i * 0x100 + j] = (uint8)(int)((float)i * (float)j * (1.0f / 255.0f));
	}
}

// FUNCTION: D3DREN 0x10035d75 _$E10
UnkType_SqrtTable g_ColorSqrtTable;

// FUNCTION: D3DREN 0x10035d7f
UnkType_SqrtTable::UnkType_SqrtTable()
{
	uint32 i;
	float v;

	for (i = 0; i < 0x100; i++)
	{
		v = (float)sqrt((float)i * 0.001953125f) * 255.0f;
		if (v < 0.0f)
			v = 0.0f;
		else if (v > 255.0f)
			v = 255.0f;
		m_Unk00[i] = (uint8)(int)v;
	}
}

// ---- pixelformat (engine twin: src/shared/pixelformat.cpp; Jupiter runtime/shared/src/pixelformat.cpp) --------------------------------
// Where the code is emitted: function templates (Convert1Pass/Convert2Pass/ConvertDXTGeneric) are NOT inlined at /O1 and their instances land
// after the last ordinary function of the object, every out-of-line copy of an inline member (BaseBFToAny::Init, CC_*::DoConvert, or_cpy) right
// after its first caller; so the source order below is not the address order.

#define SRC_8	(*pSrc)
#define SRC_16	(*((uint16*)pSrc))
#define SRC_32	(*((uint32*)pSrc))
#define DEST_8	(*pDest)
#define DEST_16	(*((uint16*)pDest))
#define DEST_32	(*((uint32*)pDest))

#define ALPHAVAL abstract.m_AlphaValues

#define READROW_NORMAL(index, startOffset)\
	A::Or(pDestPos, 0, ALPHAVAL[(alphaData[index] >> (startOffset+0)) & 0x7]);\
	A::Or(pDestPos, 1, ALPHAVAL[(alphaData[index] >> (startOffset+3)) & 0x7]);\
	A::Or(pDestPos, 2, ALPHAVAL[(alphaData[index] >> (startOffset+6)) & 0x7]);\
	A::Or(pDestPos, 3, ALPHAVAL[(alphaData[index] >> (startOffset+9)) & 0x7]);\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

#define DECODE_LINE(lineShiftAmt)\
	A::Set(pDestPos, 0, abstract.m_Ident[(blockData>>(lineShiftAmt+0)) & 3]);\
	A::Set(pDestPos, 1, abstract.m_Ident[(blockData>>(lineShiftAmt+2)) & 3]);\
	A::Set(pDestPos, 2, abstract.m_Ident[(blockData>>(lineShiftAmt+4)) & 3]);\
	A::Set(pDestPos, 3, abstract.m_Ident[(blockData>>(lineShiftAmt+6)) & 3]);\
	pDestPos = (((uint8*)pDestPos) + pRequest->m_DestPitch);

#define DECODE_ALPHA_2ROWS() \
	DECODE_ALPHA(0, 0)\
	DECODE_ALPHA(4, 1)\
	DECODE_ALPHA(8, 2)\
	DECODE_ALPHA(12, 3)\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;\
	DECODE_ALPHA(16, 0)\
	DECODE_ALPHA(20, 1)\
	DECODE_ALPHA(24, 2)\
	DECODE_ALPHA(28, 3)\
	pDestPos = (uint8*)pDestPos + pRequest->m_DestPitch;

#define DECODE_ALPHA(shift, iPixel)\
	A::Mask(pDestPos, iPixel, invAlphaMask);\
	A::Or(pDestPos, iPixel, abstract.m_AlphaValues[(blockData>>shift) & 15]);

#undef SRC_8
#undef SRC_16
#undef SRC_32
#undef DEST_8
#undef DEST_16
#undef DEST_32
#undef ALPHAVAL
#undef READROW_NORMAL
#undef DECODE_LINE
#undef DECODE_ALPHA_2ROWS
#undef DECODE_ALPHA
