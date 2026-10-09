// Jupiter runtime/kernel/src/sys/win/client.cpp: WinMain, the main window and the main loop.
// Talon has no command line holder: WinMain builds a CmdLineArgs (from the real command line, from
// launch.dll's GetLithTechCommandLine or from -cmdfile files) and passes it down. The client manager
// is created by cm_Init and kept in g_ClientGlob.m_pClientMgr.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "bdefs.h"
#include "de_memory.h"
#include "clientmgr.h"
#include "dsys_interface.h"
#include "render.h"
#include "soundmgr.h"
#include "iclientshell.h"
#include "iltcursor.h"
#include "exceptionhandler.h"
#include "../../build/proj/LT2/lithshared/stdlith/helpers.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "engine_vars.h"
#include "struct_bank_debug.h"

#define MAX_NUM_ARGS	150
#define MAX_ARG_LENGTH	200

#define MAX_RESTREES	20

typedef int (*GetLithtechCommandLineFn)(int32 argc, char **argv,
	int32 *pOutArgc, char **pOutArgv, int32 maxNumOutArgs, int32 maxOutArgLength);

// Used for finding memory leaks (Jupiter checks it in its allocator; nothing in Talon reads it). It is
// the 0xffffffff that starts this object's .data in the exe.
// GLOBAL: LITHTECH 0x004cf52c
int g_iStopAllocCount = -1;



#define MUSIC_IMMEDIATE		0

// The client manager's CSoundMgr (embedded at CClientMgr::m_SoundMgr).
#define CLIENT_SOUNDMGR()	(&g_ClientGlob.m_pClientMgr->m_SoundMgr)

// 0x0040fbb0 (clientmgr.cpp)
SMusicMgr* GetMusicMgr();

// 0x00430710 (de_objects.cpp)
void DebugOut(const char *pMsg, ...);



// GLOBAL: LITHTECH 0x004debe0
uint32 g_CurRunIteration=0;

// GLOBAL: LITHTECH 0x004debdc
uint32 g_EngineStartMS;


///////////////////////////////////////////////////
//  Functions.

static LTBOOL StartClient(ClientGlob *pGlob, CmdLineArgs *pArgs);
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

// Looks for pArgName (with or without a leading dash) and returns the argument after it.
// FUNCTION: LITHTECH 0x00402ef0
char* cl_FindArg(const char *pArgName, int argc, char **argv)
{
	char dashArg[256];
	int i;

	sprintf(dashArg, "%s%s", "-", pArgName);

	for(i=0; i < argc; i++)
	{
		if(CHelpers::UpperStrcmp(argv[i], pArgName) || CHelpers::UpperStrcmp(argv[i], dashArg))
		{
			if(i < argc-1)
				return argv[i+1];
			else
				return g_EmptyString;
		}
	}

	return LTNULL;
}


// Searches the real command line.
// FUNCTION: LITHTECH 0x00402f90
char* cl_FindCommandLineArg(const char *pArgName)
{
	return cl_FindArg(pArgName, __argc, __argv);
}


// FUNCTION: LITHTECH 0x00402fb0
int RunClientApp(HINSTANCE hInstance, CmdLineArgs *pArgs)
{
	MSG			msg;
	WNDCLASS	wndclass;
	ClientGlob	*pGlob;
	char		*pArg;
	RECT		screenRect, wndRect;
	int			status, nExitValue;
	LTBOOL		bOutOfMemory;

	pGlob = &g_ClientGlob;

	// Init the globals.
	memset(pGlob, 0, sizeof(*pGlob));
	nExitValue = 0;

	if(cl_FindArg("DebugStructBanks", pArgs->m_Argc, pArgs->m_Argv))
	{
		g_bDebugStructBanks = LTTRUE;
	}

	// Set the jump-to position for memory errors...
	bOutOfMemory = LTFALSE;
	if(setjmp(g_ClientGlob.m_MemoryJmp) != 0)
	{
		bOutOfMemory = LTTRUE;
		goto END_MAINLOOP;
	}

	// Set the working directory.
	pArg = cl_FindArg("workingdir", pArgs->m_Argc, pArgs->m_Argv);
	if(pArg)
	{
		SetCurrentDirectory(pArg);
	}

	pGlob->m_bInputEnabled = LTTRUE;

	// Initialize the system-dependent modules.
	status = dsi_Init();
	if(status != 0)
	{
		if(status == 1)
		{
			MessageBox(LTNULL, "Unable to load ltmsg.dll.", "Error", MB_OK);
		}
		else
		{
			MessageBox(LTNULL, "Unknown error initializing engine.", "Error", MB_OK);
		}

		dsi_Term();
		return -1;
	}

	// Initialize the client.
	pGlob->m_hInstance = hInstance;
	pGlob->m_WndClassName = "LithTech";
	pGlob->m_pClientMgr = cm_Init();
	pGlob->m_bClientActive = LTTRUE;

	pGlob->m_WndCaption = cl_FindArg("windowtitle", pArgs->m_Argc, pArgs->m_Argv);
	if(!pGlob->m_WndCaption)
	{
		pGlob->m_WndCaption = "LithTech";
	}

	pGlob->m_bBreakOnError = cl_FindArg("breakonerror", pArgs->m_Argc, pArgs->m_Argv) != NULL;

	// Create the main window.
	wndclass.style			= CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wndclass.lpfnWndProc	= MainWndProc;
	wndclass.cbClsExtra		= 0;
	wndclass.cbWndExtra		= 0;
	wndclass.hInstance		= pGlob->m_hInstance;
	wndclass.hIcon			= LoadIcon(LTNULL, IDI_APPLICATION);
	wndclass.hCursor		= LoadCursor(LTNULL, IDC_ARROW);
	wndclass.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndclass.lpszMenuName	= LTNULL;
	wndclass.lpszClassName	= pGlob->m_WndClassName;

	RegisterClass(&wndclass);

	GetWindowRect(GetDesktopWindow(), &screenRect);

	pGlob->m_hMainWnd = CreateWindow(pGlob->m_WndClassName,	// window class name
		pGlob->m_WndCaption,								// window caption
		WS_VISIBLE | WS_BORDER | WS_MAXIMIZEBOX,	// window style
		((screenRect.right - screenRect.left) - 320) / 2,	// initial x position
		((screenRect.bottom - screenRect.top) - 200) / 2,	// initial y position
		320,												// initial x size
		200,												// initial y size
		LTNULL,												// parent window handle
		LTNULL,												// window menu handle
		pGlob->m_hInstance,									// program instance handle
		LTNULL);											// creation parameters

	if(StartClient(pGlob, pArgs))
	{
		pGlob->m_bProcessWindowMessages = LTTRUE;

		ShowWindow(pGlob->m_hMainWnd, SW_SHOWNORMAL);
		UpdateWindow(pGlob->m_hMainWnd);

		while(pGlob->m_pClientMgr->Update() == LT_OK)
		{
			// Give our process high priority?
			if(g_CV_HighPriority)
			{
				dsi_ConsolePrint("Setting process to high priority");
				SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
			}

			if(g_bShowRunningTime)
			{
				dsi_ConsolePrint("Running for %.1f seconds", (float)(timeGetTime() - g_EngineStartMS) / 1000.0f);
			}

			// Center the mouse in the window.
			if(g_CV_CursorCenter && !g_ClientGlob.m_bLostFocus)
			{
				GetWindowRect(pGlob->m_hMainWnd, &wndRect);
				SetCursorPos((screenRect.right - screenRect.left) / 2, (screenRect.bottom - screenRect.top) / 2);
			}

			while(PeekMessage(&msg, LTNULL, 0, 0, PM_REMOVE))
			{
				if(msg.message == WM_QUIT)
				{
					nExitValue = msg.wParam;
					goto END_MAINLOOP;
				}

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

END_MAINLOOP:;

	// Check for error message to post...
	if(pGlob->m_ExitMessage[0])
	{
		r_TermRender(pGlob->m_pClientMgr, 2);
		dsi_MessageBox(pGlob->m_ExitMessage, pGlob->m_WndCaption);
	}

	pGlob->m_bProcessWindowMessages = LTFALSE;

	delete pGlob->m_pClientMgr;
	pGlob->m_pClientMgr = LTNULL;

	DestroyWindow(pGlob->m_hMainWnd);

	dsi_Term();

	if(bOutOfMemory)
	{
		ShowWindow(pGlob->m_hMainWnd, SW_HIDE);
		MessageBox(GetDesktopWindow(), "Out of memory", pGlob->m_WndCaption, MB_OK);
	}

	return nExitValue;
}


// FUNCTION: LITHTECH 0x00403350
static LTBOOL StartClient(ClientGlob *pGlob, CmdLineArgs *pArgs)
{
	uint32 initStartTime;
	const char *resTrees[MAX_RESTREES];
	char strVersion[32];
	uint32 nResTrees;
	int i;
	const char *pDir, *pConfigFile;
	short control;

	_asm
	{
		fstcw	control			// Get FPU control word
		and		control, 0xfcff	// PC field = 00 for single precision
		fldcw	control
	}

	// Init the client mugger.
	pGlob->m_bHost = cl_FindArg("host", pArgs->m_Argc, pArgs->m_Argv) != NULL;
	pGlob->m_pWorldName = cl_FindArg("world", pArgs->m_Argc, pArgs->m_Argv);

	// Find all the res trees.
	nResTrees = 0;
	for(i=0; i < pArgs->m_Argc-1; i++)
	{
		if(stricmp(pArgs->m_Argv[i], "-rez") == 0)
		{
			resTrees[nResTrees++] = pArgs->m_Argv[i+1];
			if(nResTrees >= MAX_RESTREES)
				break;
		}
	}

	if(cl_FindArg("noinput", pArgs->m_Argc, pArgs->m_Argv))
	{
		pGlob->m_bInputEnabled = LTFALSE;
	}

	pDir = cl_FindArg("workingdir", pArgs->m_Argc, pArgs->m_Argv);
	if(pDir)
	{
		SetCurrentDirectory(pDir);
	}

	pConfigFile = cl_FindArg("config", pArgs->m_Argc, pArgs->m_Argv);
	if(!pConfigFile)
	{
		pConfigFile = "autoexec.cfg";
	}

	initStartTime = timeGetTime();
	if(pGlob->m_pClientMgr->Init(resTrees, nResTrees, pConfigFile, pArgs) != LT_OK)
	{
		return LTFALSE;
	}

	pGlob->m_pClientMgr->m_VersionInfo.GetString(strVersion, sizeof(strVersion));
	DebugOut("LithTech build %s initialized in %.2f seconds.\n",
		strVersion, (float)(timeGetTime() - initStartTime) / 1000.0f);

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x004034d0
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	char messageString[256], fileName[200];
	int i;
	FILE *fp;
	PAINTSTRUCT ps;
	WPARAM wSysParam;

	if(g_ClientGlob.m_bProcessWindowMessages && g_ClientGlob.m_pClientMgr)
	{
		switch(message)
		{
			case WM_PAINT:
			{
				BeginPaint(hwnd, &ps);
				EndPaint(hwnd, &ps);
				return 0;
			}

			case WM_KEYDOWN:
			{
				if(g_ClientGlob.m_nKeyDowns < MAX_KEYBUFFER)
				{
					g_ClientGlob.m_KeyDowns[g_ClientGlob.m_nKeyDowns] = wParam;
					g_ClientGlob.m_KeyDownReps[g_ClientGlob.m_nKeyDowns] = lParam & 0x7FFF;
					++g_ClientGlob.m_nKeyDowns;
				}

				return 0;
			}

			case WM_KEYUP:
			{
				if(wParam == VK_F8)
				{
					// Get a free filename.
					for(i=0; i < 3000; i++)
					{
						sprintf(fileName, "%s%d.bmp", g_SSFile, i);
						fp = fopen(fileName, "rb");
						if(fp)
						{
							fclose(fp);
						}
						else
						{
							g_Render.MakeScreenShot(fileName);
							break;
						}
					}
				}
				else
				{
					if(g_ClientGlob.m_nKeyUps < MAX_KEYBUFFER)
					{
						g_ClientGlob.m_KeyUps[g_ClientGlob.m_nKeyUps++] = wParam;
					}
				}

				return 0;
			}

			case WM_CHAR:
			{
				return 0;
			}

			case WM_ERASEBKGND:
			{
				// Ignore this message if the renderer is initialized.
				if(r_IsRenderInitted())
					return 0;
			}
			break;

			case WM_ACTIVATEAPP:
			{
				if(!g_bNullRender)
				{
					// If a dialog is up, let dsi_PreDialog and dsi_PostDialog handle this stuff.
					if(!g_ClientGlob.m_bDialogUp)
					{
						if(wParam)
						{
							if(g_ClientGlob.m_bLostFocus)
							{
								// Tell the client
								g_ClientGlob.m_pClientMgr->m_pClientShell->OnEvent(LTEVENT_GAINEDFOCUS, 0);

								if(!g_ClientGlob.m_bRendererShutdown)
								{
									// Restore the video mode.
									OutputDebugString("Regained focus.. initializing renderer.\n");

									if(r_InitRender(g_ClientGlob.m_pClientMgr, &g_RMode) != LT_OK)
									{
										dsi_SetupMessage(messageString, sizeof(messageString)-1, LT_UNABLETORESTOREVIDEO, LTNULL);
										dsi_OnClientShutdown(messageString);
									}
								}

								if(CLIENT_SOUNDMGR()->IsValid())
								{
									CLIENT_SOUNDMGR()->ReacquireDigitalHandle();
								}
							}

							g_ClientGlob.m_pClientMgr->ClearInput();
							g_ClientGlob.m_bLostFocus = LTFALSE;
							g_ClientGlob.m_bClientActive = LTTRUE;

							if(g_ClientGlob.m_pClientMgr->m_pCursorMgr)
							{
								g_ClientGlob.m_pClientMgr->m_pCursorMgr->RefreshCursor();
							}
						}
						else
						{
							// Tell the client
							g_ClientGlob.m_pClientMgr->m_pClientShell->OnEvent(LTEVENT_LOSTFOCUS, 0);

							OutputDebugString("Losing focus.. shutting down renderer.\n");
							r_TermRender(g_ClientGlob.m_pClientMgr, 1);

							g_ClientGlob.m_pClientMgr->ClearInput();
							g_ClientGlob.m_bLostFocus = LTTRUE;
							g_ClientGlob.m_bClientActive = LTFALSE;

							if(!g_ClientGlob.m_bAppClosing && CLIENT_SOUNDMGR()->IsValid())
							{
								CLIENT_SOUNDMGR()->ReleaseDigitalHandle();
							}
						}
					}
				}
				else
				{
					if(wParam)
					{
						if(g_ClientGlob.m_bLostFocus)
						{
							OutputDebugString("Regained focus... Sending GAINEDFOCUS event.\n");
							g_ClientGlob.m_pClientMgr->m_pClientShell->OnEvent(LTEVENT_GAINEDFOCUS, 0);
						}

						g_ClientGlob.m_pClientMgr->ClearInput();
						g_ClientGlob.m_bLostFocus = LTFALSE;
					}
					else
					{
						if(!g_ClientGlob.m_bAppClosing)
						{
							OutputDebugString("Lost focus... Sending LOSTFOCUS event.\n");
							g_ClientGlob.m_pClientMgr->m_pClientShell->OnEvent(LTEVENT_LOSTFOCUS, 0);
							ShowWindow(g_ClientGlob.m_hMainWnd, SW_MINIMIZE);
							g_ClientGlob.m_bLostFocus = LTTRUE;
						}
					}
				}

				return (message == WM_NCACTIVATE);
			}

			case WM_SYSCOMMAND:
			{
				wSysParam = wParam & 0xFFF0;

				if(wSysParam == SC_KEYMENU)
				{
					return LTTRUE;
				}

				if(wSysParam == SC_SCREENSAVE || wSysParam == SC_MONITORPOWER)
				{
					if(!IsIconic(hwnd))
					{
						return LTTRUE;
					}
				}

				if(wSysParam == SC_CLOSE)
				{
					g_ClientGlob.m_bAppClosing = LTTRUE;

					// Kill the music and sound drivers...
					if(GetClientILTSoundMgrImpl()->IsValid())
						GetClientILTSoundMgrImpl()->StopAllSounds();
					if(GetMusicMgr() && GetMusicMgr()->m_bValid)
						GetMusicMgr()->Stop(MUSIC_IMMEDIATE);
				}
			}
			break;

			case WM_DESTROY:
			{
				dsi_OnClientShutdown(LTNULL);
				return 0;
			}
		}
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}


// Builds the engine's command line: from launch.dll if -launch is given, otherwise from the real
// command line with -cmdfile files expanded.
// FUNCTION: LITHTECH 0x00403a00
LTBOOL SetupArgs(CmdLineArgs *pArgs)
{
	int32 nArgs;
	int i, j, len;
	char quoteBuf[MAX_ARG_LENGTH];
	char *argPointers[MAX_NUM_ARGS];
	char argBuffer[MAX_NUM_ARGS][MAX_ARG_LENGTH];
	HINSTANCE hModule;
	GetLithtechCommandLineFn fn;
	int status;
	FILE *fp;
	uint32 fileLen, totalLen;
	char *pFileBuf, *pTok, *pCur;
	bool bInQuote;

	bInQuote = false;
	nArgs = 0;
	for(i=0; i < MAX_NUM_ARGS; i++)
	{
		argPointers[i] = argBuffer[i];
		argBuffer[i][0] = 0;
	}

	if(cl_FindCommandLineArg("launch"))
	{
		status = 0;
		hModule = LoadLibrary("launch.dll");
		if(hModule)
		{
			fn = (GetLithtechCommandLineFn)GetProcAddress(hModule, "GetLithTechCommandLine");
			if(fn)
			{
				status = fn(__argc, __argv, &nArgs, argPointers, MAX_NUM_ARGS, MAX_ARG_LENGTH);
			}

			FreeLibrary(hModule);
			if(status)
				goto BUILD_ARGS;
		}

		MessageBox(LTNULL, "Unable to load launch.dll.", "LithTech", MB_OK);
		return LTFALSE;
	}

	for(i=0; i < __argc; i++)
	{
		if(CHelpers::UpperStrcmp(__argv[i], "-cmdfile"))
		{
			i++;
			if(__argv[i])
			{
				fp = fopen(__argv[i], "r");
				if(fp)
				{
					fseek(fp, 0, SEEK_END);
					fileLen = ftell(fp);
					rewind(fp);

					fileLen++;
					pFileBuf = (char*)dalloc(fileLen);
					if(!fgets(pFileBuf, fileLen, fp))
					{
						fclose(fp);
						return LTFALSE;
					}

					fclose(fp);

					pTok = strtok(pFileBuf, " ");
					while(pTok)
					{
						if(!bInQuote)
						{
							if(pTok[0] == '\"')
							{
								len = strlen(pTok);
								for(j=1; j < len; j++)
									quoteBuf[j-1] = pTok[j];

								quoteBuf[j+1] = 0;
								bInQuote = true;
								continue;
							}
							else
							{
								strncpy(argBuffer[nArgs], pTok, MAX_ARG_LENGTH);
							}
						}
						else
						{
							pTok = strtok(LTNULL, "\"");
							strncat(quoteBuf, " ", 1);
							strncat(quoteBuf, pTok, strlen(pTok));
							bInQuote = false;
							strncpy(argBuffer[nArgs], quoteBuf, MAX_ARG_LENGTH);
						}

						argBuffer[nArgs][MAX_ARG_LENGTH-1] = 0;
						nArgs++;
						if(nArgs > MAX_NUM_ARGS)
							break;

						pTok = strtok(LTNULL, " ");
					}

					dfree(pFileBuf);
				}
			}
		}
		else
		{
			strncpy(argBuffer[nArgs], __argv[i], MAX_ARG_LENGTH);
			argBuffer[nArgs][MAX_ARG_LENGTH-1] = 0;
			nArgs++;
			if(nArgs > MAX_NUM_ARGS)
				break;
		}
	}

BUILD_ARGS:;
	totalLen = 0;
	for(i=0; i < nArgs; i++)
	{
		totalLen += strlen(argPointers[i]) + 1;
	}

	pArgs->m_pArgBuffer = (char*)dalloc(totalLen);
	pArgs->m_Argv = (char**)dalloc(nArgs * sizeof(char*));
	if(!pArgs->m_pArgBuffer || !pArgs->m_Argv)
		return LTFALSE;

	pCur = pArgs->m_pArgBuffer;
	for(i=0; i < nArgs; i++)
	{
		strcpy(pCur, argPointers[i]);
		pArgs->m_Argv[i] = pCur;
		pCur += strlen(argPointers[i]) + 1;
	}

	pArgs->m_Argc = nArgs;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00403d90
int WINAPI ClientWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	CmdLineArgs args;
	int ret;

	args.m_Argv = LTNULL;
	args.m_Argc = 0;
	args.m_pArgBuffer = LTNULL;

	g_EngineStartMS = timeGetTime();

	// Setup our command line args.
	if(!SetupArgs(&args))
	{
		if(args.m_Argv)
			dfree(args.m_Argv);
		if(args.m_pArgBuffer)
			dfree(args.m_pArgBuffer);
		return -1;
	}

	// Run the client once.
	ret = RunClientApp(hInstance, &args);

	// Check if we should run more than once.
	if(g_CV_PlayDemoReps > 1)
	{
		++g_CurRunIteration;

		// Keep running it.
		while(g_CurRunIteration < (uint32)g_CV_PlayDemoReps)
		{
			++g_CurRunIteration;
			ret = RunClientApp(hInstance, &args);
		}
	}

	if(args.m_Argv)
		dfree(args.m_Argv);
	if(args.m_pArgBuffer)
		dfree(args.m_pArgBuffer);

	return ret;
}


// FUNCTION: LITHTECH 0x00403e80 _WinMain@16
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	int ret = -1;

	__try
	{
		ret = ClientWinMain(hInstance, hPrevInstance, lpszCmdParam, nCmdShow);
	}
	__except(RecordExceptionInfo(GetExceptionInformation(), "main thread"))
	{
	}

	return ret;
}
