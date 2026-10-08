// d3d.ren sys/d3d/d3d_shadowtexture (0x1001d1b5-0x1001da60): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// file name from Jupiter is d3dshadowtexture.cpp; link order requires a name between d3d_optimizedsurface and d3d_surface.
// D3DShadowTextureFactory::Get and the static member live at 0x100323f9, a different object.
// FLAGS: /O1 /Ob2
#include <stdio.h>
#include <string.h>
#include "d3dren/d3d_surface.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/d3dtexture.h"
#include "d3dren/tlvertex.h"
#include "ltdynarray.h"
// GLOBAL: D3DREN 0x1004b4c0
extern uint32 g_ValidTileSizes[];

// ---- d3d_optimizedsurface ---------------------------------------------------------------------------------------------

#define NUM_VALIDTILESIZES ((int)(sizeof(g_ValidTileSizes) / sizeof(g_ValidTileSizes[0])))

#define DO_MASK_LOOP(srcType, destType, srcIt, destIt, keyType, keyVal, srcMask)	\
	xCounter = pRequest->m_Width;													\
	keyType colorKey = keyVal;														\
	srcIt = (srcType *)pSrcLine;													\
	destIt = (destType *)pDestLine;													\
	while (xCounter)																\
	{																				\
		xCounter--;																	\
		if ((*srcIt & ~srcMask) != colorKey)										\
			*destIt |= destAlpha32;													\
		else																		\
			*destIt &= ~destAlpha32;												\
		srcIt++; destIt++;															\
	}

// ---- globals defined by these objects ---------------------------------------------------------------------------------------

// The text of the last DirectDraw result (D3DAppErrorToString) of the shadow texture code (only the four slots below write it).
// Name unknown: DAT_ (the project owner's rule); it is a plain global of the d3dshadowtexture object (.bss, right before the
// ConVar LockOnFlip of the d3d_surface object that follows).
// GLOBAL: D3DREN 0x100606d0
char *g_pShadowTextureLastErrorString;

// A plain 0x20 byte D3DTLVERTEX (FVF 0x1c4: x, y, z, rhw, diffuse, specular, tu, tv).  Not TLVertex (tlvertex.h): its LTVector
// member has a constructor, which would make the compiler emit a static initialiser (_$E) for the array; d3d.ren has none.
struct UnkType_TLVertexPOD
{
	float x, y, z;
	float rhw;
	uint32 color;
	uint32 specular;
	float tu, tv;
};

// The quad the Draw slot (0x1001d5b5) fills and passes to DrawPrimitive: 4 vertices = 0x1005f650..0x1005f6d0.  Name unknown.
// GLOBAL: D3DREN 0x1005f650
UnkType_TLVertexPOD g_ShadowTextureQuadVerts[4];

// NAME: D3DShadowTextureFactory::m_pShadowTextureFactory: names_proposal.csv (high, Jupiter d3dshadowtexture.cpp)
// GLOBAL: D3DREN 0x1007674c
D3DShadowTextureFactory *D3DShadowTextureFactory::m_pShadowTextureFactory = NULL;

// ---- D3DShadowTexture ------------------------------------------------------------------------------------------------------

// NAME: D3DShadowTexture, D3DShadowTextureInstance, D3DShadowTextureFactory and their members: names_proposal.csv rows
// 0x1001d1b5-0x1001da29 (Jupiter render_a/src/sys/shadows/d3dshadowtexture.cpp has the same classes and bodies).  The
// constructor and destructor of D3DShadowTexture below are inlined everywhere in d3d.ren (the constructor into the Instance
// constructor, the destructor into ??_G), so the linker dropped their out of line copies: they have no address.
D3DShadowTexture::D3DShadowTexture()
{
	m_Unk04 = 0;
	m_Unk08 = 0;
	m_pD3DTexture = NULL;
}

D3DShadowTexture::~D3DShadowTexture()
{
	Term();
}

// The scalar deleting destructors: ??_G of D3DShadowTexture (vtable 0x100462e8 slot 0: sets the vtable, Term(), sets the
// IShadowTexture vtable, deletes) and of the abstract base (vtable 0x10046308 slot 0).
// FUNCTION: D3DREN 0x1001d1b5 ??_GD3DShadowTexture@@MAEPAXI@Z
// FUNCTION: D3DREN 0x1001d1dd ??_GIShadowTexture@@UAEPAXI@Z

// NAME: D3DShadowTexture::Init: names_proposal.csv (medium, Jupiter D3DShadowTexture::Init(uiSizeX, uiSizeY)).  The Talon form
// rounds a non power of two size down to one bit (search from MAX_SHADOW_TEXTURE_SIZE 0x100), creates a DirectDraw 7 texture
// surface in the pixel format of g_pOffscreen (texture managed) and fills it with a radial ramp: every byte of a pixel =
// min(1, 1.5*r2/R2 + 0.25) * 255 (r2 = squared distance from the centre, R2 = the squared half diagonal), dark in the middle.
// (A plain `for` over the bytes of a pixel is what VC6 turns into the `rep stosd` fill; memset() stays a call without /Oi.)
// FUNCTION: D3DREN 0x1001d1fa
bool D3DShadowTexture::Init(uint32 uiSizeX, uint32 uiSizeY)
{
	bool bSizeXPowerOf2 = ((uiSizeX & (uiSizeX - 1)) == 0);
	bool bSizeYPowerOf2 = ((uiSizeY & (uiSizeY - 1)) == 0);

	if (!bSizeXPowerOf2)
	{
		// make sure the dimensions are power of 2
		uint32 uiNewSizeX;
		for (uiNewSizeX = MAX_SHADOW_TEXTURE_SIZE; (uiNewSizeX & uiSizeX) == 0; uiNewSizeX >>= 1);
		uiSizeX = uiNewSizeX;
	}

	if (!bSizeYPowerOf2)
	{
		uint32 uiNewSizeY;
		for (uiNewSizeY = MAX_SHADOW_TEXTURE_SIZE; (uiNewSizeY & uiSizeY) == 0; uiNewSizeY >>= 1);
		uiSizeY = uiNewSizeY;
	}

	Term();

	DDPIXELFORMAT ddpf;
	memset(&ddpf, 0, sizeof(ddpf));
	ddpf.dwSize = sizeof(ddpf);
	HRESULT hResult = g_pOffscreen->GetPixelFormat(&ddpf);
	g_pShadowTextureLastErrorString = D3DAppErrorToString(hResult);

	DDSURFACEDESC2 ddsd;
	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
	ddsd.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
	ddsd.dwWidth = uiSizeX;
	ddsd.dwHeight = uiSizeY;
	memcpy(&ddsd.ddpfPixelFormat, &ddpf, sizeof(ddpf));

	IDirectDrawSurface7 *pSurface = NULL;
	hResult = g_pDD->CreateSurface(&ddsd, &pSurface, NULL);
	g_pShadowTextureLastErrorString = D3DAppErrorToString(hResult);
	if (hResult == DD_OK)
	{
		m_Unk04 = uiSizeX;
		m_Unk08 = uiSizeY;
		m_pD3DTexture = pSurface;
	}

	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = uiSizeX;
	rect.bottom = uiSizeY;
	hResult = pSurface->Lock(&rect, &ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL);
	if (hResult == DD_OK)
	{
		uint32 uiBytesPerPixel = ddsd.ddpfPixelFormat.dwRGBBitCount >> 3;
		uint32 uiPadBytes = (ddsd.lPitch / uiBytesPerPixel - uiSizeX) * uiBytesPerPixel;

		float fHalfX = (float)uiSizeX * 0.5f;
		float fHalfY = (float)uiSizeY * 0.5f;
		float fInvRadius2 = 1.0f / (fHalfY * fHalfY + fHalfX * fHalfX);

		uint8 *pDest = (uint8 *)ddsd.lpSurface;
		for (uint32 y = 0; y < uiSizeY; y++)
		{
			for (uint32 x = 0; x < uiSizeX; x++)
			{
				float dx = (float)x - fHalfX;
				float dy = (float)y - fHalfY;
				float fDist = (dx * dx + dy * dy) * fInvRadius2;
				float fValue = fDist + fDist;
				fValue = (fValue + 0.25f) - 0.25f * fValue;
				if (fValue > 1.0f)
					fValue = 1.0f;
				uint8 uiValue = (uint8)(int)(fValue * 255.0f);
				for (uint32 i = 0; i < uiBytesPerPixel; i++)
					pDest[i] = uiValue;
				pDest += uiBytesPerPixel;
			}
			pDest += uiPadBytes;
		}
		pSurface->Unlock(&rect);
	}
	return hResult == DD_OK;
}

// NAME: D3DShadowTexture::Term: names_proposal.csv (medium, Jupiter D3DShadowTexture::Term).  Virtual in Talon (slot 7, one past
// the interface: Init calls it through the vtable); the destructor calls it directly.
// FUNCTION: D3DREN 0x1001d478
void D3DShadowTexture::Term()
{
	if (m_pD3DTexture != NULL)
	{
		m_pD3DTexture->Release();
		m_pD3DTexture = NULL;
	}
	m_Unk04 = 0;
	m_Unk08 = 0;
}

// guess: GetSize (vtable slot 2): writes the size of the texture to two out pointers.  Name: none (FUN_).
// FUNCTION: D3DREN 0x1001d496
void D3DShadowTexture::GetDimensions(uint32 *pSizeX, uint32 *pSizeY)
{
	*pSizeX = m_Unk04;
	*pSizeY = m_Unk08;
}

// guess: vtable slot 3, binds the texture: stages 0-7 off, stage 0 = this surface with clamped addressing, colour = texture blended
// by the diffuse alpha, alpha = diffuse alpha, alpha blend ZERO / SRCCOLOR (multiplies the framebuffer by the texture).  FUN_ name:
// no evidence for the role.
// FUNCTION: D3DREN 0x1001d4ab
bool D3DShadowTexture::BindForShadowMultiply()
{
	IDirectDrawSurface7 *pTexture = m_pD3DTexture;
	for (int i = 0; i < 8; i++)
	{
		g_pD3DDevice->SetTexture(i, NULL);
		g_pD3DDevice->SetTextureStageState(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
		g_pD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	}
	g_pD3DDevice->SetTexture(0, pTexture);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_BLENDDIFFUSEALPHA);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ZERO);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_SRCCOLOR);
	return true;
}

// guess: vtable slot 4, draws the texture as a screen space quad at (x, y) with the given diffuse colour (bFlag: 0 = alpha blend
// SRCALPHA/INVSRCALPHA, else plain blend enable); saves and restores z / alpha blend render states around it.  FUN_ name.
// (The old render states live in one array and the corner coordinates in named float locals: both are needed for the stack
// layout and the x87 order of the exe.)
// FUNCTION: D3DREN 0x1001d5b5
bool D3DShadowTexture::DrawScreenQuad(uint32 x, uint32 y, uint32 color, char bFlag)
{
	BindForShadowMultiply();

	DWORD dwOld[5];
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZENABLE, &dwOld[0]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ZWRITEENABLE, &dwOld[1]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &dwOld[2]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_SRCBLEND, &dwOld[3]);
	g_pD3DDevice->GetRenderState(D3DRENDERSTATE_DESTBLEND, &dwOld[4]);

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 0);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, bFlag != 0);
	if (bFlag == 0)
	{
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
		g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);

	for (int i = 0; i < 4; i++)
	{
		g_ShadowTextureQuadVerts[i].z = 0.0f;
		g_ShadowTextureQuadVerts[i].rhw = 1.0f;
		g_ShadowTextureQuadVerts[i].color = color;
		g_ShadowTextureQuadVerts[i].specular = 0;
	}

	float fLeft = (float)x;
	float fTop = (float)y;
	float fRight = (float)m_Unk04 + fLeft;
	float fBottom = (float)m_Unk08 + fTop;
	g_ShadowTextureQuadVerts[0].x = fLeft;
	g_ShadowTextureQuadVerts[0].y = fTop;
	g_ShadowTextureQuadVerts[0].tu = 0.0f;
	g_ShadowTextureQuadVerts[0].tv = 0.0f;
	g_ShadowTextureQuadVerts[1].x = fLeft;
	g_ShadowTextureQuadVerts[1].y = fBottom;
	g_ShadowTextureQuadVerts[1].tu = 0.0f;
	g_ShadowTextureQuadVerts[1].tv = 1.0f;
	g_ShadowTextureQuadVerts[2].x = fRight;
	g_ShadowTextureQuadVerts[2].y = fBottom;
	g_ShadowTextureQuadVerts[2].tu = 1.0f;
	g_ShadowTextureQuadVerts[2].tv = 1.0f;
	g_ShadowTextureQuadVerts[3].x = fRight;
	g_ShadowTextureQuadVerts[3].y = fTop;
	g_ShadowTextureQuadVerts[3].tu = 1.0f;
	g_ShadowTextureQuadVerts[3].tv = 0.0f;
	g_pD3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, g_ShadowTextureQuadVerts, 4, 0);

	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, dwOld[0]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, dwOld[1]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, dwOld[2]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, dwOld[3]);
	g_pD3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, dwOld[4]);
	g_pD3DDevice->SetTextureStageState(0, D3DTSS_ADDRESS, D3DTADDRESS_WRAP);
	return true;
}

// guess: vtable slot 5, BltFast of this texture onto g_pOffscreen at (x, y).  FUN_ name.
// (The exe reads m_pD3DTexture into a local first; without it the member load comes after the RECT stores.)
// FUNCTION: D3DREN 0x1001d7e6
bool D3DShadowTexture::CopyToOffscreen(uint32 x, uint32 y)
{
	IDirectDrawSurface7 *pTexture = m_pD3DTexture;
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = m_Unk04;
	rect.bottom = m_Unk08;
	HRESULT hResult = g_pOffscreen->BltFast(x, y, pTexture, &rect, DDBLTFAST_WAIT);
	g_pShadowTextureLastErrorString = D3DAppErrorToString(hResult);
	return hResult == DD_OK;
}

// guess: vtable slot 6, BltFast of g_pOffscreen into this texture (the silhouette grab).  FUN_ name.  (Same local as above.)
// FUNCTION: D3DREN 0x1001d836
bool D3DShadowTexture::CopyFromOffscreen(uint32 x, uint32 y)
{
	IDirectDrawSurface7 *pTexture = m_pD3DTexture;
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = m_Unk04;
	rect.bottom = m_Unk08;
	HRESULT hResult = pTexture->BltFast(x, y, g_pOffscreen, &rect, DDBLTFAST_WAIT);
	g_pShadowTextureLastErrorString = D3DAppErrorToString(hResult);
	return hResult == DD_OK;
}

// ---- D3DShadowTextureInstance ----------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1001d886
D3DShadowTextureInstance::D3DShadowTextureInstance(D3DShadowTextureInstance *pPrev, D3DShadowTextureInstance *pNext)
{
	m_pShadowTexture = new D3DShadowTexture;
	m_pPrev = pPrev;
	m_pNext = pNext;
}

// FUNCTION: D3DREN 0x1001d8c0
D3DShadowTextureInstance::~D3DShadowTextureInstance()
{
	delete m_pShadowTexture;

	if (m_pPrev)
		m_pPrev->m_pNext = m_pNext;
	if (m_pNext)
		m_pNext->m_pPrev = m_pPrev;
}

// FUNCTION: D3DREN 0x1001d8eb
D3DShadowTextureInstance *D3DShadowTextureInstance::Find(D3DShadowTexture *pShadowTexture)
{
	if (pShadowTexture == m_pShadowTexture)
		return this;
	if (m_pNext != NULL)
		return m_pNext->Find(pShadowTexture);
	return NULL;
}

// ---- D3DShadowTextureFactory -----------------------------------------------------------------------------------------------

// FUNCTION: D3DREN 0x1001d8ff
D3DShadowTextureFactory::D3DShadowTextureFactory()
{
	if (m_pShadowTextureFactory == NULL)
		m_pShadowTextureFactory = this;

	m_pFirst = new D3DShadowTextureInstance(NULL, NULL);
}

// vtable 0x10046328 slot 0
// FUNCTION: D3DREN 0x1001d939 ??_GD3DShadowTextureFactory@@UAEPAXI@Z

// FUNCTION: D3DREN 0x1001d955
D3DShadowTextureFactory::~D3DShadowTextureFactory()
{
	if (m_pShadowTextureFactory == this)
		m_pShadowTextureFactory = NULL;

	while (m_pFirst != NULL)
	{
		D3DShadowTextureInstance *pShadowTextureInstance = m_pFirst->m_pNext;
		delete m_pFirst;
		m_pFirst = pShadowTextureInstance;
	}
}

// The abstract base of the factory: ??_G of vtable 0x10046334 (dtor + 2 pure virtuals).
// FUNCTION: D3DREN 0x1001d9a0 ??_GUnkType_ShadowTextureFactoryBase@@UAEPAXI@Z

// FUNCTION: D3DREN 0x1001d9bd
IShadowTexture *D3DShadowTextureFactory::AllocShadowTexture(uint32 uiSizeX, uint32 uiSizeY)
{
	m_pFirst->m_pNext = new D3DShadowTextureInstance(m_pFirst, m_pFirst->m_pNext);

	D3DShadowTexture *pShadowTexture = (m_pFirst->m_pNext != NULL ? m_pFirst->m_pNext->m_pShadowTexture : NULL);
	if (!pShadowTexture->Init(uiSizeX, uiSizeY))
	{
		delete m_pFirst->m_pNext;
		pShadowTexture = NULL;
	}

	return pShadowTexture;
}

// FUNCTION: D3DREN 0x1001da29
void D3DShadowTextureFactory::FreeShadowTexture(IShadowTexture *pShadowTexture)
{
	delete m_pFirst->Find(static_cast<D3DShadowTexture *>(pShadowTexture));
}
