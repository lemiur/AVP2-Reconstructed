// Jupiter runtime/kernel/src/sys/win/render.cpp
// Talon loads the renderer from a DLL (RenderDLLSetup), keeps its own system texture cache,
// and reaches the client manager through g_ClientGlob. The unit ends at 0046f6a0; the packet
// writers after it are server code.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "bdefs.h"
#include "render.h"
#include "dsys_interface.h"
#include "de_memory.h"
#include "clientmgr.h"
#include "console.h"
#include "concommand.h"
#include "consolecommands.h"
#include "de_objects.h"
#include "de_world.h"
#include "sprite.h"
#include "videomgr.h"
#include "iltclient.h"
#include "iltcommon.h"
#include "iclientshell.h"
#include "dtxmgr.h"
#include "lthread.h"
#include "client_filemgr.h"
#include "engine_vars.h"


// cutil.cpp
LTObject*	cm_FindObject(CClientMgr *pClientMgr, uint16 objectID);					// 0x004265e0
void		cm_RotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTRotation *pRot);	// 0x00426940

// winclientde_impl.
LTBOOL		cis_RendererIsHere(RenderStruct *pStruct);	// 0x0040f160
LTBOOL		cis_RendererGoingAway();					// 0x0040f320


void r_RunConsoleString(char *pStr);

typedef void (*RenderDLLSetupFn)(RenderStruct *pStruct);


// ------------------------------------------------------------ //
// Globals..
// ------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0046eb30 _$E2
// FUNCTION: LITHTECH 0x0046eb40 _$E1
SysCache g_SysCache;

RMode g_RMode;


// ------------------------------------------------------------ //
// Internal functions.
// ------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0046eb60
void r_UnloadSystemTexture(TextureData *pTexture)
{
	g_SysCache.m_CurMem -= pTexture->m_AllocSize;
	pTexture->m_pSharedTexture->m_pEngineData = LTNULL;

	dl_Remove(&pTexture->m_Link);
	--g_SysCache.m_List.m_nElements;

	dfree(pTexture->m_pDataBuffer);
	dfree(pTexture);
}


// Loads the texture and installs it.
// FUNCTION: LITHTECH 0x0046ebd0
LTRESULT r_LoadSystemTexture(SharedTexture *pSharedTexture, TextureData **ppTextureData, LTBOOL bBind)
{
	LThreadMessage unused;	// An unused local whose constructor was folded with LThreadMessage's.
	LTRESULT dResult;
	ConParse parse;
	FileRef ref;
	CClientMgr *pClientMgr;
	ILTStream *pStream;
	TextureData *pTextureData;
	SharedTexture *pLinked;
	LTLink *pLink;

	*ppTextureData = LTNULL;

	FileIdentifier *pIdent = pSharedTexture->m_pFile;
	if(!pIdent)
		return LT_NOTINITIALIZED;

	pClientMgr = g_ClientGlob.m_pClientMgr;
	pStream = cf_OpenFileIdent(pClientMgr->m_hFileMgr, pIdent);
	if(pStream)
	{
		dResult = dtx_Create(pStream, ppTextureData, LTFALSE, LTFALSE);
		pStream->Release();

		if(dResult != LT_OK)
			return dResult;
	}
	else
	{
		RETURN_ERROR_PARAM(1, r_LoadSystemTexture, LT_MISSINGFILE, pIdent->m_Filename);
	}

	// Add the new texture to the MRU list.
	pTextureData = *ppTextureData;
	pLink = &pTextureData->m_Link;
	pLink->m_pPrev = &g_SysCache.m_List.m_Head;
	pLink->m_pData = pTextureData;
	pLink->m_pNext = g_SysCache.m_List.m_Head.m_pNext;
	g_SysCache.m_List.m_Head.m_pNext->m_pPrev = pLink;
	pLink->m_pPrev->m_pNext = pLink;
	++g_SysCache.m_List.m_nElements;
	g_SysCache.m_CurMem += (*ppTextureData)->m_AllocSize;

	// Store its pointer in the SharedTexture.
	pSharedTexture->m_pEngineData = *ppTextureData;
	pSharedTexture->SetCommandLine((*ppTextureData)->m_Header.m_CommandString);
	pSharedTexture->m_Unknown3C = (*ppTextureData)->m_Header.m_Extra[4];
	(*ppTextureData)->m_pSharedTexture = pSharedTexture;

	if(bBind)
		r_BindTexture(pSharedTexture, LTFALSE);

	// Load in any linked textures depending upon what the user entered in the command string.

	// Detail texturing
	parse.Init((*ppTextureData)->m_Header.m_CommandString);
	if(parse.ParseFind("DetailTex", LTFALSE, 1))
	{
		ref.m_FileType = FILE_CLIENTFILE;
		ref.m_pFilename = parse.m_Args[1];
		pLinked = cm_AddSharedTexture(pClientMgr, &ref);
		pSharedTexture->m_eTexType = 0;
		pSharedTexture->m_pLinkedTexture = pLinked;
		return LT_OK;
	}

	// Environment mapping
	parse.Init((*ppTextureData)->m_Header.m_CommandString);
	if(parse.ParseFind("EnvMap", LTFALSE, 1))
	{
		ref.m_FileType = FILE_CLIENTFILE;
		ref.m_pFilename = parse.m_Args[1];
		pSharedTexture->m_pLinkedTexture = cm_AddSharedTexture(pClientMgr, &ref);
		pSharedTexture->m_eTexType = 1;
	}

	// Environment mapping blended with the alpha of the base texture
	parse.Init((*ppTextureData)->m_Header.m_CommandString);
	if(parse.ParseFind("EnvMapAlpha", LTFALSE, 1))
	{
		ref.m_FileType = FILE_CLIENTFILE;
		ref.m_pFilename = parse.m_Args[1];
		volatile uint32 *pType = (volatile uint32 *)&pSharedTexture->m_eTexType;
		pLinked = cm_AddSharedTexture(pClientMgr, &ref);
		*pType = 2;
		pSharedTexture->m_pLinkedTexture = pLinked;
		return LT_OK;
	}

	return LT_OK;
}


// ------------------------------------------------------------ //
// RenderStruct function implementations.
// ------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0046ee30
LTObject* r_ProcessAttachment(LTObject *pParent, Attachment *pAttachment)
{
	LTransform tAttachment;
	CClientMgr *pClientMgr = g_ClientGlob.m_pClientMgr;

	if(pClientMgr->m_pClientDE->Common()->GetAttachmentTransform((HATTACHMENT)pAttachment, tAttachment, LTTRUE) != LT_OK)
		return LTNULL;

	LTObject *pChild = cm_FindObject(pClientMgr, pAttachment->m_nChildID);
	if(!pChild)
		return LTNULL;

	cm_MoveObject(pClientMgr, pChild, &tAttachment.m_Pos, LTFALSE);
	cm_RotateObject(pClientMgr, pChild, &tAttachment.m_Rot);
	return pChild;
}


// FUNCTION: LITHTECH 0x0046eea0 _$E5
// FUNCTION: LITHTECH 0x0046eeb0 _$E4
RenderStruct g_Render;

// GLOBAL: LITHTECH 0x004e49e0
static HINSTANCE g_hRenderDLL;


static SharedTexture* r_GetSharedTexture(const char *pFilename);
static TextureData* r_GetTexture(SharedTexture *pTexture);
void r_RunConsoleString(char *pStr);
static void r_FreeTexture(SharedTexture *pTexture);
static void r_ConsolePrint(char *pMsg, ...);
static HLTPARAM r_GetParameter(char *pName);
static float r_GetParameterValueFloat(HLTPARAM hParam);
static char* r_GetParameterValueString(HLTPARAM hParam);
static void r_NullFn();
static uint32 r_IncObjectFrameCode();
static uint32 r_GetObjectFrameCode();
static uint16 r_IncCurTextureFrameCode();
static void* r_Alloc(uint32 size);
static void r_Free(void *ptr);


// FUNCTION: LITHTECH 0x0046eec0
void r_InitRenderStruct(LTBOOL bFullClear)
{
	if(bFullClear)
		memset(&g_Render, 0, sizeof(g_Render));
	else
		memset(&g_Render, 0, 0x108);	// Keep the members past 0x108.

	g_Render.ProcessAttachment = r_ProcessAttachment;
	g_Render.GetSharedTexture = r_GetSharedTexture;
	g_Render.GetTexture = r_GetTexture;
	g_Render.FreeTexture = r_FreeTexture;
	g_Render.RunConsoleString = r_RunConsoleString;
	g_Render.ConsolePrint = r_ConsolePrint;
	g_Render.GetParameter = r_GetParameter;
	g_Render.GetParameterValueFloat = r_GetParameterValueFloat;
	g_Render.GetParameterValueString = r_GetParameterValueString;
	g_Render.Unknown24 = r_NullFn;
	g_Render.IncObjectFrameCode = r_IncObjectFrameCode;
	g_Render.GetObjectFrameCode = r_GetObjectFrameCode;
	g_Render.IncCurTextureFrameCode = r_IncCurTextureFrameCode;
	g_Render.Alloc = r_Alloc;
	g_Render.Free = r_Free;

	g_Render.m_Unknown154 = 0;
	g_Render.m_AmbientLight = 0;
	g_Render.m_GlobalLightDir.Init(0.0f, -2.0f, -1.0f);
	g_Render.m_GlobalLightDir.Norm();
	g_Render.m_Unknown154 = 0;
	g_Render.m_AmbientLight = 0;
	g_Render.m_AmbientLight = 0;
	g_Render.m_Unknown154 = 0;
}


// FUNCTION: LITHTECH 0x0046eff0
static SharedTexture* r_GetSharedTexture(const char *pFilename)
{
	FileRef ref;

	ref.m_FileType = FILE_ANYFILE;
	ref.m_pFilename = pFilename;
	return cm_AddSharedTexture(g_ClientGlob.m_pClientMgr, &ref);
}

// FUNCTION: LITHTECH 0x0046f020
static TextureData* r_GetTexture(SharedTexture *pTexture)
{
	TextureData *pRet;

	if(pTexture->m_pEngineData)
		return (TextureData*)pTexture->m_pEngineData;

	r_LoadSystemTexture(pTexture, &pRet, LTFALSE);
	return pRet;
}

// FUNCTION: LITHTECH 0x0046f040
static void r_FreeTexture(SharedTexture *pTexture)
{
	TextureData *pData;

	pData = (TextureData*)pTexture->m_pEngineData;
	if(pData && !(pData->m_Flags & 0x40))
	{
		r_UnloadSystemTexture(pData);
	}
}

// FUNCTION: LITHTECH 0x0046f060
void r_RunConsoleString(char *pStr)
{
	cc_HandleCommand(&g_ClientConsoleState, pStr);
}

// FUNCTION: LITHTECH 0x0046f080
static void r_ConsolePrint(char *pMsg, ...)
{
	char msg[300];
	va_list marker;

	va_start(marker, pMsg);
	_vsnprintf(msg, sizeof(msg)-1, pMsg, marker);
	va_end(marker);

	con_WhitePrintf(msg);
}

// FUNCTION: LITHTECH 0x0046f0c0
static HLTPARAM r_GetParameter(char *pName)
{
	return (HLTPARAM)cc_FindConsoleVar(&g_ClientConsoleState, pName);
}

// FUNCTION: LITHTECH 0x0046f0e0
static float r_GetParameterValueFloat(HLTPARAM hParam)
{
	if(hParam)
		return ((LTCommandVar*)hParam)->floatVal;
	else
		return 0.0f;
}

// FUNCTION: LITHTECH 0x0046f100
static char* r_GetParameterValueString(HLTPARAM hParam)
{
	if(hParam)
		return ((LTCommandVar*)hParam)->pStringVal;
	else
		return LTNULL;
}

// Identical-code folded at 0x004359b0.
static void r_NullFn()
{
}

// FUNCTION: LITHTECH 0x0046f110
static uint32 r_GetObjectFrameCode()
{
	WorldTreeHelper *pHelper = &g_ClientGlob.m_pClientMgr->m_ObjectMgr;
	return pHelper->GetFrameCode();
}

// FUNCTION: LITHTECH 0x0046f130
static uint32 r_IncObjectFrameCode()
{
	WorldTreeHelper *pHelper = &g_ClientGlob.m_pClientMgr->m_ObjectMgr;
	return pHelper->IncFrameCode();
}

// FUNCTION: LITHTECH 0x0046f150
static uint16 r_IncCurTextureFrameCode()
{
	return g_ClientGlob.m_pClientMgr->IncCurTextureFrameCode();
}

// FUNCTION: LITHTECH 0x0046f160
static void* r_Alloc(uint32 size)
{
	return dalloc(size);
}

// FUNCTION: LITHTECH 0x0046f170
static void r_Free(void *ptr)
{
	dfree(ptr);
}


// Like RETURN_ERROR_PARAM, but also clears m_bInitializingRenderer.
#define RETURN_INITRENDER_ERROR(err, errName, param) \
{ \
	dsi_OnReturnError(err); \
	if(g_DebugLevel >= 1) \
		dsi_ConsolePrint(g_ReturnErrString, "r_InitRender", errName, param); \
	g_ClientGlob.m_bInitializingRenderer = LTFALSE; \
	return (err); \
}

void sc_Init(SysCache *pCache, uint32 maxMem);

// FUNCTION: LITHTECH 0x0046f180
LTRESULT r_InitRender(CClientMgr *pClientMgr, RMode *pMode)
{
	RenderStructInit init;
	RenderDLLSetupFn setupFn;
	char str[200];
	HWND hWnd;

	if(g_ClientGlob.m_bInitializingRenderer)
		return LT_OK;

	g_ClientGlob.m_bInitializingRenderer = LTTRUE;

	// Shutdown the current renderer.
	r_TermRender(pClientMgr, 0);

	hWnd = (HWND)dsi_GetMainWindow();
	ShowWindow(hWnd, SW_RESTORE);

	sc_Init(&g_SysCache, 10000000);

	memset(&init, 0, sizeof(init));
	init.m_hWnd = hWnd;
	memcpy(&init.m_Mode, pMode, sizeof(RMode));

	// Load the render DLL.
	g_hRenderDLL = LoadLibrary(pMode->m_RenderDLL);
	if(!g_hRenderDLL)
	{
		pClientMgr->SetupError(LT_ERRORLOADINGRENDERDLL, pMode->m_RenderDLL);
		RETURN_INITRENDER_ERROR(LT_ERRORLOADINGRENDERDLL, "LT_ERRORLOADINGRENDERDLL", pMode->m_RenderDLL);
	}

	setupFn = (RenderDLLSetupFn)GetProcAddress(g_hRenderDLL, "RenderDLLSetup");
	if(setupFn)
	{
		setupFn(&g_Render);

		g_Render.m_Width = pMode->m_Width;
		g_Render.m_Height = pMode->m_Height;

		if(g_Render.Init(&init) == RENDER_OK && init.m_RendererVersion == LTRENDER_VERSION)
		{
			pClientMgr->InitConsole();

			if(!cis_RendererIsHere(&g_Render))
			{
				FreeLibrary(g_hRenderDLL);
				g_hRenderDLL = LTNULL;
				pClientMgr->SetupError(LT_UNABLETORESTOREVIDEO);
				RETURN_INITRENDER_ERROR(LT_UNABLETORESTOREVIDEO, "48", " ");
			}

			g_Render.m_bInitted = LTTRUE;

			// Let the game know the renderer is almost up.
			if(pClientMgr->m_pClientShell)
				pClientMgr->m_pClientShell->OnEvent(LTEVENT_RENDERALMOSTINITTED, 0);

			if(!pClientMgr->BindClientShellWorlds())
			{
				g_Render.m_bInitted = LTFALSE;
				FreeLibrary(g_hRenderDLL);
				g_hRenderDLL = LTNULL;
				RETURN_INITRENDER_ERROR(LT_ERRORBINDINGWORLD, "43", " ");
			}

			SetFocus(hWnd);
			pClientMgr->BindSharedTextures();

			memcpy(&g_RMode, &init.m_Mode, sizeof(RMode));

			sprintf(str, "CardDesc %s", init.m_Mode.m_InternalName);
			c_CommandHandler(str);

			sprintf(str, "RenderDLL %s", init.m_Mode.m_RenderDLL);
			c_CommandHandler(str);

			sprintf(str, "ScreenWidth %d", init.m_Mode.m_Width);
			c_CommandHandler(str);

			sprintf(str, "ScreenHeight %d", init.m_Mode.m_Height);
			c_CommandHandler(str);

			sprintf(str, "BitDepth %d", init.m_Mode.m_BitDepth);
			c_CommandHandler(str);

			con_LoadBackground();

			if(pClientMgr->m_pClientShell)
				pClientMgr->m_pClientShell->OnEvent(LTEVENT_RENDERINIT, 0);

			g_ClientGlob.m_bInitializingRenderer = LTFALSE;
			return LT_OK;
		}
	}

	FreeLibrary(g_hRenderDLL);
	g_hRenderDLL = LTNULL;
	pClientMgr->SetupError(LT_ERRORLOADINGRENDERDLL, pMode->m_RenderDLL);
	RETURN_INITRENDER_ERROR(LT_ERRORLOADINGRENDERDLL, "LT_ERRORLOADINGRENDERDLL", pMode->m_RenderDLL);
}


// FUNCTION: LITHTECH 0x0046f520
void sc_Init(SysCache *pCache, uint32 maxMem)
{
	dl_InitList(&pCache->m_List);
	pCache->m_CurMem = 0;
	pCache->m_MaxMem = maxMem;
}


// FUNCTION: LITHTECH 0x0046f540
LTRESULT r_TermRender(CClientMgr *pClientMgr, int surfaceHandling)
{
	LTLink *pCur, *pNext;

	if(!g_Render.m_bInitted)
		return LT_OK;

	// Notify the game.
	if(pClientMgr->m_pClientShell)
		pClientMgr->m_pClientShell->OnEvent(LTEVENT_RENDERTERM, 0);

	if(pClientMgr->m_pVideoMgr)
		pClientMgr->m_pVideoMgr->OnRenderTerm();

	if(!cis_RendererGoingAway() && surfaceHandling == 1)
	{
		pClientMgr->SetupError(LT_UNABLETORESTOREVIDEO);
		RETURN_ERROR(1, r_TermRender, LT_UNABLETORESTOREVIDEO);
	}

	pClientMgr->TermConsole();
	pClientMgr->UnbindSharedTextures();
	pClientMgr->UnbindClientShellWorlds();

	if(g_hRenderDLL)
	{
		g_Render.Term();
		FreeLibrary(g_hRenderDLL);
		g_hRenderDLL = LTNULL;
	}

	// Get rid of all the loaded textures.
	for(pCur=g_SysCache.m_List.m_Head.m_pNext; pCur != &g_SysCache.m_List.m_Head; pCur=pNext)
	{
		pNext = pCur->m_pNext;
		r_UnloadSystemTexture((TextureData*)pCur->m_pData);
	}

	if(g_CV_CursorCenter)
		ClipCursor(LTNULL);

	g_Render.m_bInitted = LTFALSE;
	r_InitRenderStruct(LTFALSE);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0046f650
void r_BindTexture(SharedTexture *pSharedTexture, LTBOOL bTextureChanged)
{
	if(g_Render.m_bInitted)
	{
		g_Render.BindTexture(pSharedTexture, bTextureChanged);
	}
}


// FUNCTION: LITHTECH 0x0046f660
void r_UnbindTexture(SharedTexture *pSharedTexture)
{
	TextureData *pData;

	if(g_Render.m_bInitted)
	{
		g_Render.UnbindTexture(pSharedTexture);
	}

	pData = (TextureData*)pSharedTexture->m_pEngineData;
	if(pData && !(pData->m_Flags & 0x40))
	{
		r_UnloadSystemTexture(pData);
		pSharedTexture->m_pEngineData = LTNULL;
	}
}
