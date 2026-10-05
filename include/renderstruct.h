// Talon renderer interface structure (Jupiter runtime/shared/src/sys/win/renderstruct.h).
// Only recovered members. The engine fills the function hooks in r_InitRenderStruct.
#ifndef __RENDERSTRUCT_H__
#define __RENDERSTRUCT_H__

#include "ltbasedefs.h"
#include "pixelformat.h"

struct SharedTexture;
class TextureData;
class Attachment;
struct RenderStructInit;
class PFormat;

typedef void* HLTPARAM;
typedef void* HLTBUFFER;

// BlitRequest::m_BlitOptions.
#define BLIT_TRANSPARENT	1	// Transparent blit.

// A blit command (winclientde_impl).  (size 0x20)
class BlitRequest
{
public:
	BlitRequest()
	{
		m_hBuffer = NULL;
		m_BlitOptions = 0;
		m_pSrcRect = NULL;
		m_pDestRect = NULL;
		m_Alpha = 1.0f;
	}

	HLTBUFFER		m_hBuffer;			// 0x00 The buffer to blit.
	uint32			m_BlitOptions;		// 0x04 Combination of the BLIT_ flags above.
	GenericColor	m_TransparentColor;	// 0x08 Transparent color.
	LTRect			*m_pSrcRect;		// 0x0c Source (m_hBuffer) rectangle.
	LTRect			*m_pDestRect;		// 0x10 Destination rectangle.
	float			m_Alpha;			// 0x14 Alpha value (0-1).
	LTWarpPt		*m_pWarpPts;		// 0x18
	int				m_nWarpPts;			// 0x1c
};

// A render context for a world (CreateContext).
typedef void* HRENDERCONTEXT;
class MainWorld;
struct RenderContextInit
{
	MainWorld		*m_pWorld;
};

// Talon RenderStruct: only the members the engine side touches are named.
struct RenderStruct
{
	RenderStruct() {}

	LTObject*		(*ProcessAttachment)(LTObject *pParent, Attachment *pAttachment);	// 0x00
	SharedTexture*	(*GetSharedTexture)(const char *pFilename);					// 0x04
	TextureData*	(*GetTexture)(SharedTexture *pTexture);						// 0x08
	void			(*FreeTexture)(SharedTexture *pTexture);					// 0x0c
	void			(*RunConsoleString)(char *pString);							// 0x10
	void			(*ConsolePrint)(char *pMsg, ...);							// 0x14
	HLTPARAM		(*GetParameter)(char *pName);								// 0x18
	float			(*GetParameterValueFloat)(HLTPARAM hParam);					// 0x1c
	char*			(*GetParameterValueString)(HLTPARAM hParam);				// 0x20
	void			(*Unknown24)();												// 0x24 (engine passes an empty function)
	uint32			(*IncObjectFrameCode)();									// 0x28
	uint32			(*GetObjectFrameCode)();									// 0x2c
	uint16			(*IncCurTextureFrameCode)();								// 0x30
	void*			(*Alloc)(uint32 size);										// 0x34
	void			(*Free)(void *ptr);											// 0x38

	uint32			m_Width;		// 0x3c
	uint32			m_Height;		// 0x40
	int				m_bInitted;		// 0x44

	uint8			m_Pad48[0x58 - 0x48];	// 0x4c per-frame texture byte counter, 0x50 total texture memory (d3d.ren)

	// Renderer profile counters (the demo manager's PDCounters: draw counts while "ShowPerformance" is on).
	uint32			m_Ticks_TagVisibleLeaves;	// 0x58
	uint32			m_Ticks_FlushObjectQueues;	// 0x5c
	uint32			m_Ticks_Models;				// 0x60
	uint32			m_Ticks_WorldModels;		// 0x64
	uint32			m_Ticks_Translucent;		// 0x68
	uint8			m_Pad6C[0x70 - 0x6c];

	int				(*Init)(RenderStructInit *pInit);	// 0x70 Returns RENDER_OK for success, or an error code.
	void			(*Term)();							// 0x74
	void			(*BindTexture)(SharedTexture *pTexture, LTBOOL bTextureChanged);	// 0x78
	void			(*UnbindTexture)(SharedTexture *pTexture);						// 0x7c

	void			(*RebindLightmaps)(uint32 hContext);	// 0x80 (con_RebindLightmaps; name from the console command)
	HRENDERCONTEXT	(*CreateContext)(RenderContextInit *pInit);	// 0x84 (CClientShell::BindWorlds)
	void			(*DeleteContext)(HRENDERCONTEXT hContext);		// 0x88
	void			(*Clear)(LTRect *pRect, uint32 flags, LTVector *pColor);	// 0x8c
	LTBOOL			(*Start3D)();						// 0x90
	LTBOOL			(*End3D)();							// 0x94
	LTBOOL			(*IsIn3D)();						// 0x98
	LTBOOL			(*StartOptimized2D)();				// 0x9c
	void			(*EndOptimized2D)();				// 0xa0
	uint8			m_PadA4[0xa8 - 0xa4];
	LTBOOL			(*SetOptimized2DBlend)(LTSurfaceBlend blend);	// 0xa8
	uint8			m_PadAC[0xb0 - 0xac];
	LTBOOL			(*SetOptimized2DColor)(HLTCOLOR hColor);		// 0xb0
	uint8			m_PadB4[0xb8 - 0xb4];
	int				(*RenderScene)(struct SceneDesc *pScene);	// 0xb8 (cm_Render)
	void			(*RenderCommand)(int argc, char *argv[]);	// 0xbc (console RenderCommand)
	void*			(*GetHook)(char *pName);			// 0xc0 renderer objects by name ("LPDIRECTDRAW", "BACKBUFFER")
	void			(*SwapBuffers)(uint32 flags);		// 0xc4
	uint8			m_PadC8[0xcc - 0xc8];
	LTBOOL			(*GetScreenFormat)(PFormat *pFormat);			// 0xcc
	HLTBUFFER		(*CreateSurface)(int width, int height);		// 0xd0
	void			(*DeleteSurface)(HLTBUFFER hSurf);				// 0xd4
	void			(*GetSurfaceInfo)(HLTBUFFER hSurf, uint32 *pWidth, uint32 *pHeight, long *pPitch);	// 0xd8
	void*			(*LockSurface)(HLTBUFFER hSurf);				// 0xdc
	void			(*UnlockSurface)(HLTBUFFER hSurf);				// 0xe0
	LTBOOL			(*OptimizeSurface)(HLTBUFFER hSurf, uint32 transparentColor);	// 0xe4
	void			(*UnoptimizeSurface)(HLTBUFFER hSurf);			// 0xe8
	LTBOOL			(*LockScreen)(int left, int top, int right, int bottom, void **pData, long *pPitch);	// 0xec
	void			(*UnlockScreen)();								// 0xf0
	void			(*BlitToScreen)(BlitRequest *pRequest);		// 0xf4
	LTBOOL			(*WarpToScreen)(BlitRequest *pRequest);		// 0xf8
	void			(*MakeScreenShot)(const char *pFilename);	// 0xfc
	void			(*ReadConsoleVariables)();					// 0x100
	void			(*BlitFromScreen)(BlitRequest *pRequest);	// 0x104
	uint8			m_Pad108[0x10c - 0x108];
	SharedTexture	*m_pEnvMapTexture;	// 0x10c the "EnvMap" console command's texture (consolecommands)
	// A global pan texture (sky shadow, fog), indexed by the GLOBALPAN_ values (ILTClient::SetGlobalPanTexture/Info).
	struct RSTextureRef
	{
		SharedTexture	*m_pTexture;
		float			m_xOffset, m_zOffset;		// d3d.ren: the texture offset in u, v
		float			m_xScale, m_zScale;			// d3d.ren: the stage UV scale is divided by them
	}				m_GlobalPans[NUM_GLOBALPAN_TYPES];	// 0x110
	LTVector		m_GlobalLightDir;	// 0x138 (0,-2,-1) normalized by r_InitRenderStruct; ILTClient::Get/SetGlobalLightDir
	LTVector		m_GlobalLightColor;	// 0x144 ILTClient::Get/SetGlobalLightColor
	uint32			m_AmbientLight;		// 0x150 0-255 (ILTClient::Get/SetGlobalLightScale)
	uint32			m_Unknown154;		// 0x154
};

typedef RenderStruct::RSTextureRef GlobalPanInfo;

#define LTRENDER_VERSION	3421
#define RENDER_OK			0
#define RENDER_ERROR		1

struct RenderStructInit
{
	int		m_RendererVersion;	// 0x000 The renderer MUST set this to LTRENDER_VERSION.
	RMode	m_Mode;				// 0x004 What mode we want to use.
	void	*m_hWnd;			// 0x218 The main window.
};

#endif  // __RENDERSTRUCT_H__
