// d3d.ren sdk/ltquatbase (0x1003579a-0x10035917): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
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

// ---- queued world polygon drawing (the polygons of lightmapped surfaces are queued per texture by QueueLightmappedPoly) --------------
// The queued polys' texture: node -> poly -> surface -> SharedTexture.
#define BUCKET_TEXTURE(pBucket)	(((Surface *)((WorldPoly *)(pBucket)->m_Unk04->m_Unk00)->m_pSurface)->m_pTexture)

// ---- quat_ConvertToMatrix (the Talon SDK's sdk/inc/ltquatbase.cpp, compiled into the renderer too; engine twin src/sdk/ltquatbase.cpp) ----
// FUNCTION: D3DREN 0x1003579a
void quat_ConvertToMatrix(const float *pQuat, float mat[4][4])
{
	static float s, xs, ys, zs, wx, wy, wz, xx, xy, xz, yy, yz, zz;

	// get the values for matrix calcuation...
	s = 2.0f / ((pQuat[0] * pQuat[0]) + (pQuat[1] * pQuat[1]) +
		(pQuat[2] * pQuat[2]) + (pQuat[3] * pQuat[3]));

	xs = pQuat[0] * s;
	ys = pQuat[1] * s;
	zs = pQuat[2] * s;

	wx = pQuat[3] * xs;
	wy = pQuat[3] * ys;
	wz = pQuat[3] * zs;

	xx = pQuat[0] * xs;
	xy = pQuat[0] * ys;
	xz = pQuat[0] * zs;

	yy = pQuat[1] * ys;
	yz = pQuat[1] * zs;

	zz = pQuat[2] * zs;

	// Fill in matrix
	mat[0][0] = 1.0f - (yy + zz);
	mat[0][1] = xy - wz;
	mat[0][2] = xz + wy;
	mat[0][3] = 0.0f;

	mat[1][0] = xy + wz;
	mat[1][1] = 1.0f - (xx + zz);
	mat[1][2] = yz - wx;
	mat[1][3] = 0.0f;

	mat[2][0] = xz - wy;
	mat[2][1] = yz + wx;
	mat[2][2] = 1.0f - (xx + yy);
	mat[2][3] = 0.0f;

	mat[3][0] = 0.0f;
	mat[3][1] = 0.0f;
	mat[3][2] = 0.0f;
	mat[3][3] = 1.0f;
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
