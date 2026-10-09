// d3d.ren unk/100329b0 (0x100329b0-0x10034000): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// LMAnimStatic ConVar + light animation / dynamic light application.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/100329b0 (0x100329b0-0x10034000): the LMAnimStatic console variable and the lightmap light
// animation / dynamic light x87 code that builds a poly lightmap (called from the lightmap page code of unk/10034000).
// Speed object (16-byte aligned COMDATs): FLAGS /O2 /Ob2 is the module default.
// FLAGS: /O2 /Ob2
#include "d3dren/rendererconsolevars.h"
#include "d3dren/lightmap.h"
#include "d3dren/d3dtexture.h"
#include <math.h>
#include <string.h>

// The multiply-high helper of the unit: (a * b) >> 32 through the one-operand `imul`.  The exe's copies show an
// inline function with an __asm body (parameters spilled to frame slots, `mov eax, a / imul b / mov r, edx`, result
// reloaded from its own slot, 100 % of the 21 uses), the same shape as Jupiter's FixedPoint.h RoundFloatToInt; a C
// `(__int64)a * b >> 32` compiles to a call of __allmul instead.
// NAME: none (the exe has no symbol for it, it is inlined everywhere): FUN_ naming would be wrong for an inline, so it
// gets a descriptive local name.
inline int MulHigh(int a, int b)
{
	int nResult;
	__asm
	{
		mov eax, a
		imul b
		mov nResult, edx
	}
	return nResult;
}

// guess: the context of one dynamic light on one polygon's lightmap (0x64 bytes, a stack object of ApplyPolyDynamicLightsToLightmap and
// BuildShadowMappedLightAnimTexels that SetupLightmapLightContext fills): the poly's lightmap origin, the world step of one texel in lightmap x / y (P, Q
// scaled by the lightmap grid size / |P|^2), the light, the squared radius and its inverse, the destination and the
// colour of the light as fixed point (c<<17)-0xff0000 and as packed RGB555 / RGB888.
struct UnkType_LightCtx
{
	LTVector	m_Unk00;		// 0x00 lightmap origin of the polygon (WorldPoly+0x38)
	LTVector	m_Unk0c;		// 0x0c world step of one texel in x
	LTVector	m_Unk18;		// 0x18 world step of one texel in y
	LTVector	m_Unk24;		// 0x24 light position
	float		m_Unk30;		// 0x30 scratch: squared distance of the current texel to the light
	float		m_Unk34;		// 0x34 radius^2
	float		m_Unk38;		// 0x38 1 / radius^2
	uint8		*m_Unk3c;		// 0x3c destination texels
	uint32		m_Unk40;		// 0x40 width in texels
	uint32		m_Unk44;		// 0x44 height in texels
	long		m_Unk48;		// 0x48 pitch in bytes
	int			m_Unk4c;		// 0x4c light colour r as 16.16: (r << 16) - (0xff0000 - (r << 16))
	int			m_Unk50;		// 0x50 same for g
	int			m_Unk54;		// 0x54 same for b
	int			m_Unk58;		// 0x58 set when a texel was written (the result of ApplyPolyDynamicLightsToLightmap)
	uint16		m_Unk5c;		// 0x5c light colour as RGB555
	uint32		m_Unk60;		// 0x60 light colour as RGB888
};

// A node of the dynamic light list of a polygon (WorldPoly+0x30): the light and where it is for this polygon.
struct UnkType_PolyLightNode
{
	UnkType_PolyLightNode	*m_Unk00;	// 0x00 next
	DynamicLight			*m_Unk04;	// 0x04
	LTVector				m_Unk08;	// 0x08 light position
};
#define WORLDPOLY_UNK30(p)		(*(UnkType_PolyLightNode **)((uint8 *)(p) + 0x30))

// forward declarations (the unit's functions are in address order)
void AddLightmapLightToRGB32Rect(UnkType_LightCtx *pCtx, int, int);
void AddLightmapLightToRGB555Texel(uint16 *pTexel, UnkType_LightCtx *pCtx);
int BuildShadowMappedLightAnimTexels(MainWorld *pWorld, WorldPoly *pPoly, LightAnim *pAnim, LAPolyRef *pRef, uint32 *pOut);
int BuildColorLightAnimTexels(WorldPoly *pPoly, LightAnim *pAnim, LAPolyRef *pRef, uint32 *pOut);
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat);		// 0x100109fd (the engine's cutil.cpp copy)
void SetupLightmapLightContext(MainWorld *pWorld, WorldPoly *pPoly, LTVector *pLightPos, uint8 *pDest, long pitch, uint32 w, uint32 h,
	float fRadius, uint32 r, uint32 g, uint32 b, UnkType_LightCtx *pCtx);

// ---- light animation lightmaps ---------------------------------------------------------------------------------------------------
// The decompression of a light animation frame's lightmap (unit unk/10030bb0, W3): 0x10032503 yields one byte per texel (shadow
// map frames), 0x1003249f 32 bit texels; both return 0 when the data is bad.
int DecompressLightmapMaskRuns(uint8 *pData, int nSize, uint8 *pOut);
int DecompressLightmapTexelRuns(uint32 *pData, int nSize, uint32 *pOut);

// The lightmap format conversion (FormatMgr::ConvertPixels, g_FormatMgr: d3dtexture.h).

// guess: the frame of the light animation that is shown right now for a polygon: with m_iFrames[0] == m_iFrames[1], or a blend
// of at most 0 the first frame, with a blend of at least 255 the second one, else a lerp of both
#define LAFRAME(pAnim, iFrame, pRef)	(&(pAnim)->m_pFrames[iFrame][(pRef)->m_iPoly])

// The 16-bit colour clamp table: three uint16[256] runs (0, 0..255, 255), the code indexes it from element 255.
// GLOBAL: D3DREN 0x10077bc8
uint16 g_LightmapColorClampTable[768];

// guess: light falloff table, 64 entries 16.16 fixed point (InitLightFalloffScaleTable): [i] = min(1, i / 63 * scale) * 65536
// GLOBAL: D3DREN 0x100781c8
int g_LightmapLightFalloffTable[64];

// FUNCTION: D3DREN 0x100329b0 _$E2
// GLOBAL: D3DREN 0x100782c8
ConVar g_CV_LMAnimStatic("LMAnimStatic", 0.0f);

// guess: fills the falloff table g_LightmapLightFalloffTable (d3d_RenderScene calls it once per frame with a scale)
// FUNCTION: D3DREN 0x100329d0
void InitLightFalloffScaleTable(float fScale)
{
	int i = 0;
	int *p = g_LightmapLightFalloffTable;
	int n = 64;
	do
	{
		float f = ((float)i * (1.0f / 63.0f)) * fScale;
		if (f > 1.0f)
			f = 1.0f;
		*p = (int)(f * 65536.0f);
		i++;
		p++;
		n--;
	} while (n);
}

// guess: fills the colour clamp table g_LightmapColorClampTable (device lightmap init)
// FUNCTION: D3DREN 0x10032a30
void InitLightColorClampTable()
{
	uint16 i;

	for (i = 0; i < 256; i++)
	{
		g_LightmapColorClampTable[i] = 0;
		g_LightmapColorClampTable[256 + i] = i;
		g_LightmapColorClampTable[512 + i] = 255;
	}
}

// guess: sets up the context of one dynamic light for a polygon's lightmap: pDest/pitch/w/h = the locked lightmap
// Not matching (163 vs 166 instructions, 51 aligned mismatches ignoring stack offsets): the exe does not fold the colour terms: it emits
// `mov edi,0xff0000; shl ecx,16; sub edi,ecx; sub ecx,edi` for (r << 16) - (0xff0000 - (r << 16)), where our compiler folds the same
// source to `shl ecx,17; sub ecx,0xff0000` (locals, casts and a separate difference variable tried: no change), and the exe orders the
// two |P|, |Q| normalisations (fsqrt, fdivr, fmulp) with the P*f / Q*f products interleaved differently.
// STUB: D3DREN 0x10032a60
void SetupLightmapLightContext(MainWorld *pWorld, WorldPoly *pPoly, LTVector *pLightPos, uint8 *pDest, long pitch, uint32 w, uint32 h,
	float fRadius, uint32 r, uint32 g, uint32 b, UnkType_LightCtx *pCtx)
{
	LTVector P, Q;
	float f;

	SetupLMPlaneVectors((pPoly->m_Flags & 0x3800) >> 11, pPoly->m_pPlane->m_Normal, P, Q);
	pCtx->m_Unk24 = *pLightPos;
	pCtx->m_Unk00 = pPoly->m_Unknown38;
	f = 1.0f / P.Mag();
	f = f * f * pWorld->m_LMGridSize;
	pCtx->m_Unk0c = P * f;
	f = 1.0f / Q.Mag();
	f = f * f * pWorld->m_LMGridSize;
	pCtx->m_Unk18 = Q * f;
	pCtx->m_Unk34 = fRadius * fRadius;
	pCtx->m_Unk5c = (uint16)((((r & 0xf8) << 5 | (g & 0xf8)) << 2) | ((uint16)b >> 3));
	pCtx->m_Unk60 = (r << 8 | g) << 8 | b;
	pCtx->m_Unk4c = (r << 16) - (0xff0000 - (r << 16));
	pCtx->m_Unk50 = (g << 16) - (0xff0000 - (g << 16));
	pCtx->m_Unk54 = (b << 16) - (0xff0000 - (b << 16));
	pCtx->m_Unk3c = pDest;
	pCtx->m_Unk38 = 1.0f / pCtx->m_Unk34;
	pCtx->m_Unk48 = pitch;
	pCtx->m_Unk40 = w;
	pCtx->m_Unk44 = h;
}

// guess: the per-light walks over the texels of a locked lightmap (invented names; static, inlined once each into
// ApplyPolyDynamicLightsToLightmap): the falloff-lit RGB555 rectangle (the 16-bit sibling of AddLightmapLightToRGB32Rect) and the
// flat-colour RGB555 / RGB32 rectangles.
static void AddLightmapLightToRGB555Rect(UnkType_LightCtx *pCtx)
{
	uint8 *pRow = pCtx->m_Unk3c;
	LTVector vRow = pCtx->m_Unk00;
	uint32 y, x;

	for (y = pCtx->m_Unk44; y != 0; y--)
	{
		LTVector vCol = vRow;
		uint16 *pTexel = (uint16 *)pRow;

		for (x = pCtx->m_Unk40; x != 0; x--)
		{
			pCtx->m_Unk30 = (vCol - pCtx->m_Unk24).MagSqr();
			if (pCtx->m_Unk30 < pCtx->m_Unk34)
			{
				AddLightmapLightToRGB555Texel(pTexel, pCtx);
				pCtx->m_Unk58 = 1;
			}
			pTexel++;
			vCol = vCol + pCtx->m_Unk0c;
		}
		pRow += pCtx->m_Unk48;
		vRow = vRow + pCtx->m_Unk18;
	}
}

static void FillLightmapRGB555Rect(UnkType_LightCtx *pCtx)
{
	uint8 *pRow = pCtx->m_Unk3c;
	LTVector vRow = pCtx->m_Unk00;
	uint32 y, x;

	for (y = pCtx->m_Unk44; y != 0; y--)
	{
		LTVector vCol = vRow;
		uint16 *pTexel = (uint16 *)pRow;

		for (x = pCtx->m_Unk40; x != 0; x--)
		{
			pCtx->m_Unk30 = vCol.DistSqr(pCtx->m_Unk24);
			if (pCtx->m_Unk30 < pCtx->m_Unk34)
			{
				*pTexel = pCtx->m_Unk5c;
				pCtx->m_Unk58 = 1;
			}
			pTexel++;
			vCol = vCol + pCtx->m_Unk0c;
		}
		pRow += pCtx->m_Unk48;
		vRow = vRow + pCtx->m_Unk18;
	}
}

static void FillLightmapRGB32Rect(UnkType_LightCtx *pCtx)
{
	uint8 *pRow = pCtx->m_Unk3c;
	LTVector vRow = pCtx->m_Unk00;
	uint32 y, x;

	for (y = pCtx->m_Unk44; y != 0; y--)
	{
		LTVector vCol = vRow;
		uint32 *pTexel = (uint32 *)pRow;

		for (x = pCtx->m_Unk40; x != 0; x--)
		{
			pCtx->m_Unk30 = vCol.DistSqr(pCtx->m_Unk24);
			if (pCtx->m_Unk30 < pCtx->m_Unk34)
			{
				*pTexel = pCtx->m_Unk60;
				pCtx->m_Unk58 = 1;
			}
			pTexel++;
			vCol = vCol + pCtx->m_Unk0c;
		}
		pRow += pCtx->m_Unk48;
		vRow = vRow + pCtx->m_Unk18;
	}
}

// guess: applies the dynamic lights of a polygon (the list at WorldPoly+0x30) to its locked lightmap pBits (w x h texels, 32 bit
// unless bNot32Bit): per light the context is built (SetupLightmapLightContext), then every texel inside the light's radius gets the light added
// with its falloff (AddLightmapLightToRGB32Rect for a 32 bit rectangle, AddLightmapLightToRGB555Texel per 16 bit texel) or, with the FastLight console variable or a
// light that has FLAG 0x10, the flat colour.  Returns 1 when any texel was changed.
// Not matching (1360 vs 1280 bytes): CFG and prologue are the exe's.  Inline/call-set wall (tools/inline_budget.py): the exe calls
// every LTVector operator of the falloff walk out of line (its share must be < 56u, ours is 242u) and inlines the 32-bit flat walk's
// inner operator+ with its ctor out of line (share 68..136u, ours 11u); the RGB555 flat walk already has the exe's call set.
// STUB: D3DREN 0x10032c40
int ApplyPolyDynamicLightsToLightmap(MainWorld *pWorld, WorldPoly *pPoly, uint8 *pBits, long pitch, uint32 w, uint32 h, char bNot32Bit)
{
	UnkType_LightCtx ctx;
	UnkType_PolyLightNode *pNode;
	DynamicLight *pLight;

	ctx.m_Unk58 = 0;
	for (pNode = WORLDPOLY_UNK30(pPoly); pNode; pNode = pNode->m_Unk00)
	{
		pLight = pNode->m_Unk04;
		SetupLightmapLightContext(pWorld, pPoly, &pNode->m_Unk08, pBits, pitch, w, h, pLight->m_LightRadius,
			pLight->m_ColorR, pLight->m_ColorG, pLight->m_ColorB, &ctx);

		if (g_FastLight == 0 && !(pLight->m_Flags & 0x10) && ctx.m_Unk3c != 0)
		{
			if (bNot32Bit)
				AddLightmapLightToRGB555Rect(&ctx);
			else
				AddLightmapLightToRGB32Rect(&ctx, 0, 0);
		}
		else if (bNot32Bit)
		{
			FillLightmapRGB555Rect(&ctx);
		}
		else
		{
			FillLightmapRGB32Rect(&ctx);
		}
	}

	return ctx.m_Unk58;
}

// guess: adds the light of the context to one RGB555 texel
// The channels live in a local array: with scalar r/g/b the compiler sinks each `+ MulHigh` into the final pack expression.
// FUNCTION: D3DREN 0x10033140
void AddLightmapLightToRGB555Texel(uint16 *pTexel, UnkType_LightCtx *pCtx)
{
	int a = g_LightmapLightFalloffTable[-(int)((1.0f - pCtx->m_Unk38 * pCtx->m_Unk30) * -63.0f)];

	int rgb[3];

	rgb[0] = (*pTexel >> 7) & 0xf8;
	rgb[0] += MulHigh(a, pCtx->m_Unk4c);
	rgb[1] = (*pTexel >> 2) & 0xf8;
	rgb[1] += MulHigh(a, pCtx->m_Unk50);
	rgb[2] = (*pTexel & 0x1f) * 8;
	rgb[2] += MulHigh(a, pCtx->m_Unk54);
	*pTexel = (uint16)((((g_LightmapColorClampTable[255 + rgb[0]] & 0xf8) << 5 | (g_LightmapColorClampTable[255 + rgb[1]] & 0xf8)) << 2) |
		(g_LightmapColorClampTable[255 + rgb[2]] >> 3));
}

// guess: (re)builds the lightmap of a polygon from the light animations that touch it and puts it in its page.  The animations of
// the polygon (WorldPoly::m_pLMAnimRefs) are visited in order: each one whose frame index is valid and whose blend is at least
// 0.02 gives a layer (BuildShadowMappedLightAnimTexels for a shadow map animation, BuildColorLightAnimTexels for a plain one); the first layer is the base, every
// later one is scaled by its blend (m_fBlendPercent * 255) through the multiply table and added with the saturating add table.
// With no layer the lightmap is black.  With bPageIn or the LMAnimStatic console variable the result is converted into the
// page's surface directly (through the scratch surface g_pLightmapScratchSurface); otherwise it is only built when more than one layer
// contributed (and then converted into a staging texture that the poly's lightmap is drawn from, UnkType_LMLock): a poly
// with a single plain layer keeps the page as it is.
// Not matching (1728 of 1728 bytes): the function is one `if (pPage && scratch surface)` block with a single `return 0` after it
// (the exe's failure paths share one epilogue); bPageIn itself takes the LMAnimStatic flag; the blend loop walks two pointers with
// a down counter (the exe's byte-offset induction variable); the source format reaches the request through a PFormat copy (the
// exe copy-constructs a temporary, then assigns it member by member, unrolled); ddsd and rc live at function scope (the exe does
// not overlap ddsd with the lock).  Open: the exe extracts the blend channels with shr/and on registers, ours spills the texel
// and reads bytes (`mov cl,ah`, byte loads from the spill), so the frame is 4 bytes short and the slots after it shift.
// STUB: D3DREN 0x10033210
int UpdatePolyAnimatedLightmap(MainWorld *pWorld, WorldPoly *pPoly, int bPageIn)
{
	FMConvertRequest request;
	DDSURFACEDESC2 ddsd;
	RECT rc;
	uint32 accum[0x400];
	uint32 temp[0x400];
	LightmapPage *pPage;
	int bMulti;
	int nLayers;
	uint32 nTexels, nBytes;
	uint32 iRef, i;

	pPage = WORLDPOLY_LMPAGE(pPoly);
	if (pPage && g_pLightmapScratchSurface)
	{
		pPage->m_Unk20 = 1;
		if (!pPoly->m_nLMAnimRefs)
			return 1;

		bPageIn |= (g_CV_LMAnimStatic.m_IntVal != 0);
		bMulti = 0;
		nLayers = 0;
		nTexels = pPoly->m_LMHeight * pPoly->m_LMWidth;
		nBytes = nTexels * 4;

		for (iRef = 0; iRef < pPoly->m_nLMAnimRefs; iRef++)
		{
			LAPolyRef *pRef = (LAPolyRef *)&pPoly->m_pLMAnimRefs[iRef];
			LightAnim *pAnim;
			int bOk;

			if (pRef->m_iWorld >= pWorld->m_LightAnims.GetSize())
				continue;

			pAnim = &pWorld->m_LightAnims[pRef->m_iWorld];
			if (pAnim->m_iFrames[0] == 0xffffffff || pAnim->m_fBlendPercent < 0.02f)
				continue;

			if (pAnim->m_bShadowMap)
				bOk = BuildShadowMappedLightAnimTexels(pWorld, pPoly, pAnim, pRef, temp);
			else
				bOk = BuildColorLightAnimTexels(pPoly, pAnim, pRef, temp);
			if (!bOk)
				continue;

			bMulti = iRef > 0;
			if (nLayers == 0)
			{
				memcpy(accum, temp, nBytes);
			}
			else
			{
				uint8 scale = (uint8)(int)(pAnim->m_fBlendPercent * 255.0f);

				uint32 *pAccum = accum;
				uint32 *pTemp = temp;

				for (i = nTexels; i; i--)
				{
					uint32 r = g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[(*pTemp >> 16 & 0xff) * 0x100 + scale] + (*pAccum >> 16 & 0xff)];
					uint32 g = g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[(*pTemp >> 8 & 0xff) * 0x100 + scale] + (*pAccum >> 8 & 0xff)];
					uint32 b = g_ByteSaturatingAddTable.m_Unk00[g_ByteMultiplyTable.m_Unk00[(*pTemp & 0xff) * 0x100 + scale] + (*pAccum & 0xff)];

					*pAccum = (r << 8 | g) << 8 | b;
					pAccum++;
					pTemp++;
				}
			}
			nLayers++;
		}

		if (nLayers == 0)
			memset(accum, 0, nBytes);

		if (!bPageIn)
		{
			if (bMulti)
			{
				UnkType_LMLock lock;

				if (lock.LockStagingLightmap(pPoly, 0))
				{
					PFormat srcFormat;

					srcFormat.InitPValueFormat();
					PFormat tmp(srcFormat);
					*request.m_pSrcFormat = tmp;
					request.m_pSrc = (uint8 *)accum;
					request.m_SrcPitch = pPoly->m_LMWidth * 4;
					*request.m_pDestFormat = lock.m_Unk0c;
					request.m_pDest = lock.m_Unk00;
					request.m_DestPitch = lock.m_Unk04;
					request.m_Width = pPoly->m_LMWidth;
					request.m_Height = pPoly->m_LMHeight;
					g_FormatMgr.ConvertPixels(&request);

					pPage->m_Unk20 = 0;
					pPoly->m_Flags |= 0x8000;
					return lock.UnlockStagingLightmap(1);
				}
			}
			else
			{
				pPage->m_Unk20 = 1;
				return 1;
			}
		}
		else
		{
			memset(&ddsd, 0, sizeof(ddsd));
			ddsd.dwSize = sizeof(ddsd);
			if (g_pLightmapScratchSurface->Lock(NULL, &ddsd, 0, NULL) == DD_OK)
			{
				PFormat srcFormat;

				srcFormat.InitPValueFormat();
				PFormat tmp(srcFormat);
				*request.m_pSrcFormat = tmp;
				request.m_pSrc = (uint8 *)accum;
				request.m_SrcPitch = pPoly->m_LMWidth * 4;
				DDPFToPFormat(&ddsd.ddpfPixelFormat, request.m_pDestFormat);
				request.m_pDest = (uint8 *)ddsd.lpSurface;
				request.m_DestPitch = ddsd.lPitch;
				request.m_Width = pPoly->m_LMWidth;
				request.m_Height = pPoly->m_LMHeight;
				g_FormatMgr.ConvertPixels(&request);
				g_pLightmapScratchSurface->Unlock(NULL);

				rc.left = 0;
				rc.top = 0;
				rc.right = pPoly->m_LMWidth;
				rc.bottom = pPoly->m_LMHeight;
				return pPage->m_pSurface->BltFast(WORLDPOLY_UNK4E(pPoly), WORLDPOLY_UNK4F(pPoly), g_pLightmapScratchSurface, &rc, DDBLTFAST_WAIT) == DD_OK;
			}
		}
	}
	return 0;
}

// guess: builds the light animation lightmap of a polygon for a shadow map light animation (the frames are 8 bit coverage masks of
// a point light): blends the two decompressed masks by m_PercentBetween, then lights every texel of the polygon with the light of
// the animation (position, colour, radius) scaled by the mask: pOut gets the colour as 32 bit texels (0 where unlit).
// Not matching (864 vs 816 bytes): the frame selection follows the exe (single-frame arms first, each failing on its own).  Open:
// the exe merges the failure returns into one block, counts the mask blend down, and calls operator- of the texel walk out of
// line while MagSqr and both operator+ stay inline (tools/inline_budget.py: ours inlines all; no top-level budget gives that mix).
// STUB: D3DREN 0x100338d0
int BuildShadowMappedLightAnimTexels(MainWorld *pWorld, WorldPoly *pPoly, LightAnim *pAnim, LAPolyRef *pRef, uint32 *pOut)
{
	uint8 maskA[0x400];
	uint8 maskB[0x400];
	uint8 mask[0x400];
	UnkType_LightCtx ctx;
	uint32 iFrame0, iFrame1;
	int nBlend;
	LAPolyFrame *pFrame0, *pFrame1;
	uint32 i, nTexels;
	uint8 *pMask;
	uint32 *pRow, *pTexel;
	LTVector vRow, vCol;
	uint32 x, y;

	iFrame0 = pAnim->m_iFrames[0];
	iFrame1 = pAnim->m_iFrames[1];
	nBlend = pAnim->m_PercentBetween;

	if (iFrame0 == iFrame1 || nBlend <= 0)
		pFrame0 = LAFRAME(pAnim, iFrame0, pRef);
	else if (nBlend >= 0xff)
		pFrame0 = LAFRAME(pAnim, iFrame1, pRef);
	else
	{
		pFrame0 = LAFRAME(pAnim, iFrame0, pRef);
		pFrame1 = LAFRAME(pAnim, iFrame1, pRef);
		if (!pFrame0->m_LightmapSize && !pFrame1->m_LightmapSize)
			return 0;
		if (!DecompressLightmapMaskRuns(pFrame0->m_pLightmap, pFrame0->m_LightmapSize, maskA))
			return 0;
		if (!DecompressLightmapMaskRuns(pFrame1->m_pLightmap, pFrame1->m_LightmapSize, maskB))
			return 0;

		nTexels = pPoly->m_LMHeight * pPoly->m_LMWidth;
		for (i = 0; i < nTexels; i++)
			mask[i] = (uint8)(((int)(maskB[i] - maskA[i]) * nBlend >> 8) + maskA[i]);
		goto Lit;
	}

	if (!pFrame0->m_LightmapSize || !DecompressLightmapMaskRuns(pFrame0->m_pLightmap, pFrame0->m_LightmapSize, mask))
		return 0;

Lit:
	SetupLightmapLightContext(pWorld, pPoly, &pAnim->m_vLightPos, (uint8 *)pOut, pPoly->m_LMWidth * 4, pPoly->m_LMWidth, pPoly->m_LMHeight,
		pAnim->m_fLightRadius, (uint32)pAnim->m_vLightColor.x, (uint32)pAnim->m_vLightColor.y, (uint32)pAnim->m_vLightColor.z, &ctx);

	pMask = mask;
	pRow = pOut;
	vRow = ctx.m_Unk00;
	for (y = ctx.m_Unk44; y != 0; y--)
	{
		pTexel = pRow;
		vCol = vRow;
		for (x = ctx.m_Unk40; x != 0; x--)
		{
			ctx.m_Unk30 = (vCol - ctx.m_Unk24).MagSqr();
			if (*pMask && ctx.m_Unk30 < ctx.m_Unk34)
			{
				int a = (int)((uint32)g_LightmapLightFalloffTable[-(int)((1.0f - ctx.m_Unk38 * ctx.m_Unk30) * -63.0f)] * *pMask >> 8);

				*pTexel = ((g_LightmapColorClampTable[255 + MulHigh(a, ctx.m_Unk4c)] << 8) |
					g_LightmapColorClampTable[255 + MulHigh(a, ctx.m_Unk50)]) << 8 |
					g_LightmapColorClampTable[255 + MulHigh(a, ctx.m_Unk54)];
			}
			else
				*pTexel = 0;
			pMask++;
			pTexel++;
			vCol = vCol + ctx.m_Unk0c;
		}
		pRow = (uint32 *)((uint8 *)pRow + ctx.m_Unk48);
		vRow = vRow + ctx.m_Unk18;
	}

	return 1;
}

// guess: builds the light animation lightmap of a polygon (plain light animation, 32 bit colour texels): decompresses the frame(s)
// of the polygon for the animation's current frame pair into pOut (w * h texels) and blends them by m_PercentBetween (0-255).
// Not matching (512 of 512 bytes): each single-frame case returns on its own (the exe cross-jumps the identical decodes), the
// channels come from the SDK GETRGB and are clamped one by one.  Residue in the blend loop: the exe merges the output pointer into
// the source pointer's induction variable (`[ecx+edi-4]`, edi = &buf0[i]); ours walks the sources as a frame offset and pOut apart.
// STUB: D3DREN 0x10033c00
int BuildColorLightAnimTexels(WorldPoly *pPoly, LightAnim *pAnim, LAPolyRef *pRef, uint32 *pOut)
{
	uint32 buf0[0x400];
	uint32 buf1[0x400];
	uint32 iFrame0, iFrame1;
	int nBlend;
	LAPolyFrame *pFrame0, *pFrame1;
	uint32 i, nTexels;

	if (pRef->m_iPoly < pAnim->m_nPolies)
	{
		iFrame0 = pAnim->m_iFrames[0];
		iFrame1 = pAnim->m_iFrames[1];
		nBlend = pAnim->m_PercentBetween;

		if (iFrame0 == iFrame1)
		{
			pFrame0 = LAFRAME(pAnim, iFrame0, pRef);
			if (pFrame0->m_LightmapSize)
				return DecompressLightmapTexelRuns((uint32 *)pFrame0->m_pLightmap, pFrame0->m_LightmapSize, pOut);
			return 0;
		}

		if (nBlend <= 0)
		{
			pFrame0 = LAFRAME(pAnim, iFrame0, pRef);
			if (pFrame0->m_LightmapSize)
				return DecompressLightmapTexelRuns((uint32 *)pFrame0->m_pLightmap, pFrame0->m_LightmapSize, pOut);
			return 0;
		}

		if (nBlend >= 0xff)
		{
			pFrame0 = LAFRAME(pAnim, iFrame1, pRef);
			if (pFrame0->m_LightmapSize)
				return DecompressLightmapTexelRuns((uint32 *)pFrame0->m_pLightmap, pFrame0->m_LightmapSize, pOut);
			return 0;
		}

		pFrame0 = LAFRAME(pAnim, iFrame0, pRef);
		pFrame1 = LAFRAME(pAnim, iFrame1, pRef);
		if (pFrame0->m_LightmapSize || pFrame1->m_LightmapSize)
		{
			if (DecompressLightmapTexelRuns((uint32 *)pFrame0->m_pLightmap, pFrame0->m_LightmapSize, buf0))
			{
				if (DecompressLightmapTexelRuns((uint32 *)pFrame1->m_pLightmap, pFrame1->m_LightmapSize, buf1))
				{
					nTexels = pPoly->m_LMHeight * pPoly->m_LMWidth;
					uint32 *pIn0 = buf0;
					uint32 *pIn1 = buf1;
					for (i = 0; i != nTexels; i++)
					{
						int r0, g0, b0, r1, g1, b1;

						GETRGB(*pIn0, r0, g0, b0);
						GETRGB(*pIn1, r1, g1, b1);
						int r = (((r1 - r0) * nBlend) >> 8) + r0;
						if (r > 0xff)
							r = 0xff;
						int g = (((g1 - g0) * nBlend) >> 8) + g0;
						if (g > 0xff)
							g = 0xff;
						int b = (((b1 - b0) * nBlend) >> 8) + b0;
						if (b > 0xff)
							b = 0xff;
						pOut[i] = (r << 8 | g) << 8 | b;
						pIn0++;
						pIn1++;
					}
					return 1;
				}
			}
		}
	}

	return 0;
}

// guess: adds the light of the context to every 32 bit texel of the rectangle (the two extra arguments are unused)
// The channels live in a local array, as in AddLightmapLightToRGB555Texel.
// FUNCTION: D3DREN 0x10033e00
void AddLightmapLightToRGB32Rect(UnkType_LightCtx *pCtx, int, int)
{
	uint32 *pRow = (uint32 *)pCtx->m_Unk3c;
	LTVector vRow = pCtx->m_Unk00;
	uint32 y, x;

	for (y = pCtx->m_Unk44; y != 0; y--)
	{
		LTVector vCol = vRow;
		uint32 *pTexel = pRow;

		for (x = pCtx->m_Unk40; x != 0; x--)
		{
			pCtx->m_Unk30 = (vCol - pCtx->m_Unk24).MagSqr();
			if (pCtx->m_Unk30 < pCtx->m_Unk34)
			{
				int a = g_LightmapLightFalloffTable[-(int)((1.0f - pCtx->m_Unk38 * pCtx->m_Unk30) * -63.0f)];

				int rgb[3];

				rgb[0] = ((uint8 *)pTexel)[2];
				rgb[0] += MulHigh(a, pCtx->m_Unk4c);
				rgb[1] = ((uint8 *)pTexel)[1];
				rgb[1] += MulHigh(a, pCtx->m_Unk50);
				rgb[2] = *pTexel & 0xff;
				rgb[2] += MulHigh(a, pCtx->m_Unk54);
				*pTexel = ((g_LightmapColorClampTable[255 + rgb[0]] << 8) | g_LightmapColorClampTable[255 + rgb[1]]) << 8 |
					g_LightmapColorClampTable[255 + rgb[2]];
				pCtx->m_Unk58 = 1;
			}
			pTexel++;
			vCol = vCol + pCtx->m_Unk0c;
		}
		pRow = (uint32 *)((uint8 *)pRow + pCtx->m_Unk48);
		vRow = vRow + pCtx->m_Unk18;
	}
}
