// Jupiter runtime/client/src/consolecommands.cpp
// Talon: the unit starts at 0x00422220 with the static initializers, then the command handlers.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bdefs.h"
#include "concommand.h"
#include "console.h"
#include "consolecommands.h"
#include "dhashtable.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "client_filemgr.h"
#include "dsys_interface.h"
#include "input.h"
#include "render.h"
#include "sprite.h"

#undef feof		// the original calls the CRT function

#include "de_memory.h"
#include "engine_vars.h"

// From engine_vars.cpp.
extern LTEngineVar* GetEngineVars();
extern int GetNumEngineVars();

#define CLIENT_TICKS_SUMMARY	(1<<0)
#define CLIENT_TICKS_RENDER		(1<<1)
#define CLIENT_TICKS_GAME		(1<<2)
#define CLIENT_TICKS_ENGINE		(1<<3)
#define CLIENT_TICKS_GRAPH		(1<<4)
#define CLIENT_TICKS_ALL		(~0)

#define TYPECODE_WORLD			0
#define INPUTMGR				g_pCommandClientMgr->m_InputMgr

void dm_HeapCompact();
void con_DoWorldCommand(char *pWorldName, char *pRecordFilename);

#define NUM_SAVEFNS (sizeof(g_SaveFns) / sizeof(g_SaveFns[0]))

#define NUM_COMMANDSTRUCTS (sizeof(g_LTCommandStructs) / sizeof(g_LTCommandStructs[0]))


// Static initializers: g_ClientConsoleState (its LTLink member has an empty constructor), then
// g_ClientConIterator.
// FUNCTION: LITHTECH 0x00422220 _$E2
// FUNCTION: LITHTECH 0x00422230 _$E1
// FUNCTION: LITHTECH 0x00422240 _$E7
// FUNCTION: LITHTECH 0x00422250 _$E4
// FUNCTION: LITHTECH 0x00422260 _$E6
// FUNCTION: LITHTECH 0x00422270 _$E5

// The main console state.
// GLOBAL: LITHTECH 0x004e3378
ConsoleState g_ClientConsoleState;

// The client console iterator
//	Note : This iterator is forward only
class CClientConIterator : public CConIterator
{
protected:
	enum EState {
		STATE_COMMAND = 0,
		STATE_CONVAR = 1,
		STATE_ENGINEVAR = 2
	};
	EState m_eState;				// 0x04
	int m_iCommandIndex;			// 0x08
	HHashIterator *m_hVarIndex;		// 0x0c
	HHashElement *m_hCurVar;		// 0x10
	int m_iEngineIndex;				// 0x14

	virtual LTBOOL Begin();
	virtual LTBOOL NextItem();

public:
	CClientConIterator();
	virtual ~CClientConIterator();

	virtual const char *Get() const;
};

// GLOBAL: LITHTECH 0x004e33bc
CClientConIterator	g_ClientConIterator;

// The client manager the console commands act on (clientmgr.h).
// GLOBAL: LITHTECH 0x004e33d4
CClientMgr *g_pCommandClientMgr = LTNULL;


// The command handlers are static in the original and referenced from g_LTCommandStructs; they are
// not static here so the compiler keeps them while the table isn't reconstructed.

//------------------------------------------------------------------
// ModelAdd, ModelDirAdd and WMAmbient (cm_Render hands them to the renderer).
// FUNCTION: LITHTECH 0x00422280
void con_ModelAdd(int argc, char *argv[])
{
	if(argc >= 3)
	{
		g_ConsoleModelAdd.x = (float)atof(argv[0]);
		g_ConsoleModelAdd.y = (float)atof(argv[1]);
		g_ConsoleModelAdd.z = (float)atof(argv[2]);

		g_ConsoleModelAdd.x = LTCLAMP(g_ConsoleModelAdd.x, 0.0f, 250.0f);
		g_ConsoleModelAdd.y = LTCLAMP(g_ConsoleModelAdd.y, 0.0f, 250.0f);
		g_ConsoleModelAdd.z = LTCLAMP(g_ConsoleModelAdd.z, 0.0f, 250.0f);
	}
}

// FUNCTION: LITHTECH 0x00422380
void con_ModelDirAdd(int argc, char *argv[])
{
	if(argc >= 3)
	{
		g_ConsoleModelDirAdd.x = (float)atof(argv[0]);
		g_ConsoleModelDirAdd.y = (float)atof(argv[1]);
		g_ConsoleModelDirAdd.z = (float)atof(argv[2]);

		g_ConsoleModelDirAdd.x = LTCLAMP(g_ConsoleModelDirAdd.x, 0.0f, 250.0f);
		g_ConsoleModelDirAdd.y = LTCLAMP(g_ConsoleModelDirAdd.y, 0.0f, 250.0f);
		g_ConsoleModelDirAdd.z = LTCLAMP(g_ConsoleModelDirAdd.z, 0.0f, 250.0f);
	}
}

// FUNCTION: LITHTECH 0x00422480
void con_WMAmbient(int argc, char *argv[])
{
	if(argc >= 3)
	{
		g_ConsoleModelDirAdd2.x = (float)atof(argv[0]);
		g_ConsoleModelDirAdd2.y = (float)atof(argv[1]);
		g_ConsoleModelDirAdd2.z = (float)atof(argv[2]);

		g_ConsoleModelDirAdd2.x = LTCLAMP(g_ConsoleModelDirAdd2.x, 0.0f, 250.0f);
		g_ConsoleModelDirAdd2.y = LTCLAMP(g_ConsoleModelDirAdd2.y, 0.0f, 250.0f);
		g_ConsoleModelDirAdd2.z = LTCLAMP(g_ConsoleModelDirAdd2.z, 0.0f, 250.0f);
	}
}


// FUNCTION: LITHTECH 0x00422580
void con_World(int argc, char *argv[])
{
	if(argc >= 1)
	{
		con_DoWorldCommand(argv[0], LTNULL);
	}
	else
	{
		dsi_ConsolePrint("World <world name>");
	}
}


// FUNCTION: LITHTECH 0x004225b0
void con_DoWorldCommand(char *pWorldName, char *pRecordFilename)
{
	CClientShell *pShell = LTNULL;
	char str[200], testName[200];
	FileRef ref;
	StartGameRequest request;

	memset(&request, 0, sizeof(request));

	// Attempt to get the file id
	sprintf(testName, "%s.dat", pWorldName);
	ref.m_pFilename = testName;
	ref.m_FileType = FILE_ANYFILE;
	if(!cf_GetFileIdentifier(g_pCommandClientMgr->m_hFileMgr, &ref, TYPECODE_WORLD))
	{
		// Retry, prepending the worlds folder name first
		sprintf(testName, "worlds\\%s.dat", pWorldName);
		ref.m_pFilename = testName;
		if(cf_GetFileIdentifier(g_pCommandClientMgr->m_hFileMgr, &ref, TYPECODE_WORLD))
		{
			sprintf(testName, "worlds\\%s", pWorldName);
			pWorldName = testName;
		}
	}

	// Send the load request (there is no "world not found" message in Talon)
	// Add the record info..
	if(pRecordFilename)
	{
		strncpy(request.m_RecordFilename, pRecordFilename, MAX_SGR_STRINGLEN-1);
		request.m_RecordFilename[MAX_SGR_STRINGLEN-1] = 0;
	}

	pShell = g_pCommandClientMgr->m_pCurShell;
	if(pShell && (pShell->m_ShellMode == STARTGAME_NORMAL || pShell->m_ShellMode == STARTGAME_HOST) &&
		request.m_RecordFilename[0] != 0)
	{
		sprintf(str, "world %s", pWorldName);
		pShell->SendCommandToServer(str);
	}
	else
	{
		request.m_Type = STARTGAME_NORMAL;
		strncpy(request.m_WorldName, pWorldName, MAX_SGR_STRINGLEN);
		g_pCommandClientMgr->StartShell(&request);
	}
}


// FUNCTION: LITHTECH 0x00422730
void con_TimeDemo(int argc, char *argv[])
{
	char str[256];
	StartGameRequest request;

	if(argc >= 1)
	{
		if(!g_pCommandClientMgr->m_pCurShell)
		{
			memset(&request, 0, sizeof(request));
			request.m_Type = STARTGAME_NORMAL;
			g_pCommandClientMgr->StartShell(&request);
			if(!g_pCommandClientMgr->m_pCurShell)
			{
				dsi_ConsolePrint("Unable to start a server to run demo.");
				return;
			}
		}

		sprintf(str, "timedemo %s", argv[0]);
		g_pCommandClientMgr->m_pCurShell->SendCommandToServer(str);
	}
	else
	{
		dsi_ConsolePrint("TimeDemo <record filename (w/o extension)>");
	}
}


// FUNCTION: LITHTECH 0x00422850
void con_EnableDevice(int argc, char *argv[])
{
	if( argc >= 1 )
	{
		if( !INPUTMGR->EnableDevice(INPUTMGR, argv[0]) )
			con_Printf( CONRGB(255,255,255), 1, "Error enabling device: %s", argv[0] );
	}
}


// FUNCTION: LITHTECH 0x00422890
void con_Bind( int argc, char *argv[] )
{
	int i;

	if( argc >= 2 )
	{
		INPUTMGR->ClearBindings(INPUTMGR, argv[0], argv[1]);

		for(i=2; i < argc; i++)
		{
			if(!INPUTMGR->AddBinding(INPUTMGR, argv[0], argv[1], argv[i], 0.0f, 0.0f))
			{
				con_Printf(CONRGB(255,255,255), 1, "Error binding device: %s, trigger: %s",
					argv[0], argv[1]);
			}
		}
	}
}


// FUNCTION: LITHTECH 0x00422910
void con_RangeBind( int argc, char *argv[] )
{
	int i, nActions;

	if( argc >= 4 )
	{
		INPUTMGR->ClearBindings(INPUTMGR, argv[0], argv[1]);

		nActions = (argc - 2) / 3;
		for(i=0; i < nActions; i++)
		{
			if(!INPUTMGR->AddBinding(INPUTMGR, argv[0], argv[1],
				argv[i*3+4], (float)atof(argv[i*3+2]), (float)atof(argv[i*3+3])))
			{
				con_Printf(CONRGB(255,255,255), 1, "Error binding device: %s, trigger: %s",
					argv[0], argv[1]);
			}
		}
	}
}


// FUNCTION: LITHTECH 0x004229d0
void con_UnBind( int argc, char *argv[] )
{
	if( argc >= 2 )
	{
		INPUTMGR->ClearBindings(INPUTMGR, argv[0], argv[1]);
	}
}


// FUNCTION: LITHTECH 0x00422a00
void con_Scale( int argc, char *argv[] )
{
	if( argc >= 3 )
	{
		if( !INPUTMGR->ScaleTrigger(INPUTMGR, argv[0], argv[1], (float)atof(argv[2]), 0.0f, 0.0f, 0.0f))
			con_Printf( CONRGB(255,255,255), 1, "Error finding device: %s, trigger: %s",
				argv[0], argv[1] );
	}
}


// FUNCTION: LITHTECH 0x00422a70
void con_RangeScale( int argc, char *argv[] )
{
	if( argc >= 5 )
	{
		if( !INPUTMGR->ScaleTrigger(INPUTMGR, argv[0], argv[1], (float)atof(argv[2]),
				(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]) ))
			con_Printf( CONRGB(255,255,255), 1, "Error finding device: %s, trigger: %s",
				argv[0], argv[1] );
	}
}


// FUNCTION: LITHTECH 0x00422b00
void con_AddAction(int argc, char *argv[])
{
	if(argc >= 2)
	{
		INPUTMGR->AddAction(INPUTMGR, argv[0], atoi(argv[1]));
	}
}


// FUNCTION: LITHTECH 0x00422b40
void con_SSFile( int argc, char *argv[] )
{
	if(argc >= 1)
	{
		strcpy(g_SSFile, argv[0]);
	}
}


// FUNCTION: LITHTECH 0x00422b60
void con_UpdateServer(int argc, char *argv[])
{
	// This is a command because you never really want to save this variable in the config file.
	if(argc >= 1)
	{
		g_bUpdateServer = (LTBOOL)atoi(argv[0]);
	}
}


// FUNCTION: LITHTECH 0x00422b80
void con_RenderCommand(int argc, char *argv[])
{
	g_Render.RenderCommand(argc, argv);
}


// FUNCTION: LITHTECH 0x00422b90
void con_RestartConsole(int argc, char *argv[])
{
	g_pCommandClientMgr->InitConsole();
}


// FUNCTION: LITHTECH 0x00422ba0
void con_RestartRender(int argc, char *argv[])
{
	char str[245];
	LTRESULT dResult;

	// Set renderdll automatically.
	if(argc > 0)
	{
		sprintf(str, "renderDLL %s", argv[0]);
		c_CommandHandler(str);
	}

	r_TermRender(g_pCommandClientMgr, 1);

	if((dResult = cm_StartRenderFromGlobals(g_pCommandClientMgr)) != LT_OK)
	{
		cm_ProcessError(g_pCommandClientMgr, dResult | ERROR_SHUTDOWN);
	}
}


// FUNCTION: LITHTECH 0x00422c10
void con_ResizeScreen(int argc, char *argv[])
{
	uint32 oldScreenWidth, oldScreenHeight;
	LTRESULT dResult;

	if(argc >= 2)
	{
		oldScreenWidth = g_ScreenWidth;
		oldScreenHeight = g_ScreenHeight;

		g_ScreenWidth = atoi(argv[0]);
		g_ScreenHeight = atoi(argv[1]);

		con_Printf(CONRGB(0,255,0), 1, "Setting screen to %dx%d", g_ScreenWidth, g_ScreenHeight);

		r_TermRender(g_pCommandClientMgr, 1);

		if(cm_StartRenderFromGlobals(g_pCommandClientMgr) != LT_OK)
		{
			g_ScreenWidth = oldScreenWidth;
			g_ScreenHeight = oldScreenHeight;

			if((dResult = cm_StartRenderFromGlobals(g_pCommandClientMgr)) != LT_OK)
			{
				cm_ProcessError(g_pCommandClientMgr, dResult | ERROR_SHUTDOWN);
			}
		}
	}
}


// FUNCTION: LITHTECH 0x00422cc0
void con_ServerCommand(int argc, char *argv[])
{
	char tempStr[300], fullCommand[500];
	int i;

	if(argc >= 1)
	{
		if(g_pCommandClientMgr->m_pCurShell)
		{
			// 'unparse' the string to send it to the server.
			fullCommand[0] = 0;
			for(i=0; i < argc; i++)
			{
				sprintf(tempStr, "\"%s\" ", argv[i]);
				strcat(fullCommand, tempStr);
			}

			g_pCommandClientMgr->m_pCurShell->SendCommandToServer(fullCommand);
		}
	}
}


// FUNCTION: LITHTECH 0x00422d70
void con_Quit(int argc, char *argv[])
{
	dsi_OnClientShutdown( LTNULL );
}


// FUNCTION: LITHTECH 0x00422d80
void con_ListInputDevices(int argc, char *argv[])
{
	INPUTMGR->ListDevices(INPUTMGR);
}


// FUNCTION: LITHTECH 0x00422da0
void con_EnvMap(int argc, char *argv[])
{
	FileRef ref;

	if(g_pCommandClientMgr && argc >= 1)
	{
		ref.m_FileType = FILE_CLIENTFILE;
		ref.m_pFilename = argv[0];
		g_Render.m_pEnvMapTexture = cm_AddSharedTexture(g_pCommandClientMgr, &ref);
		if(g_Render.m_pEnvMapTexture)
			dsi_ConsolePrint("Environment map set to %s.", argv[0]);
		else
			dsi_ConsolePrint("Couldn't find texture %s.", argv[0]);
	}
}


// FUNCTION: LITHTECH 0x00422e20
void con_RebindTextures(int argc, char *argv[])
{
	if(g_pCommandClientMgr)
	{
		cm_RebindTextures(g_pCommandClientMgr);
	}
}


// FUNCTION: LITHTECH 0x00422e40
void con_RebindLightmaps(int argc, char *argv[])
{
	if(g_pCommandClientMgr && g_pCommandClientMgr->m_pCurShell &&
		g_pCommandClientMgr->m_pCurShell->GetWorld() && g_Render.m_bInitted)
	{
		g_Render.RebindLightmaps(g_pCommandClientMgr->m_pCurShell->GetWorld()->m_Unknown1C8);
	}
	else
	{
		g_Render.RebindLightmaps(0);
	}
}


// FUNCTION: LITHTECH 0x00422e90
void con_HeapCompact(int argc, char *argv[])
{
	dm_HeapCompact();
}


// Toggle settings in the client ticks.
// FUNCTION: LITHTECH 0x00422ea0
void con_ShowTicks(int argc, char *argv[])
{
	int iArgLoop;

	// Show some help if they don't specify a section
	if (argc < 1)
	{
		con_Printf(CONRGB(255,192,192), 0, "Please include at least one command:");
		con_Printf(CONRGB(255,192,192), 0, "  ALL - Show Everything");
		con_Printf(CONRGB(255,192,192), 0, "  NONE - Show Nothing");
		con_Printf(CONRGB(255,192,192), 0, "  SUMMARY - Toggle Summary information");
		con_Printf(CONRGB(255,192,192), 0, "  RENDER - Toggle Renderer");
		con_Printf(CONRGB(255,192,192), 0, "  GAME - Toggle Game processing");
		con_Printf(CONRGB(255,192,192), 0, "  ENGINE - Toggle Engine processing");
		con_Printf(CONRGB(255,192,192), 0, "  GRAPH - Toggle Summary graph");
		con_Printf(CONRGB(255,192,192), 0, "          (Red = Renderer, Green = Game, Blue = Engine, White = System)");
		return;
	}

	// Update the ShowTickCounts flags
	for (iArgLoop = 0; iArgLoop < argc; ++iArgLoop)
	{
		if (stricmp(argv[iArgLoop], "ALL") == LTNULL)
			g_ShowTickCounts = CLIENT_TICKS_ALL;
		else if (stricmp(argv[iArgLoop], "NONE") == LTNULL)
			g_ShowTickCounts = 0;
		else if ((stricmp(argv[iArgLoop], "SUMMARY") == LTNULL) ||
			(stricmp(argv[0], "1") == LTNULL))
			g_ShowTickCounts ^= CLIENT_TICKS_SUMMARY;
		else if (stricmp(argv[iArgLoop], "RENDER") == LTNULL)
			g_ShowTickCounts ^= CLIENT_TICKS_RENDER;
		else if (stricmp(argv[iArgLoop], "GAME") == LTNULL)
			g_ShowTickCounts ^= CLIENT_TICKS_GAME;
		else if (stricmp(argv[iArgLoop], "ENGINE") == LTNULL)
			g_ShowTickCounts ^= CLIENT_TICKS_ENGINE;
		else if (stricmp(argv[iArgLoop], "GRAPH") == LTNULL)
			g_ShowTickCounts ^= CLIENT_TICKS_GRAPH;
		// Dunno what they wanted....
		else
		{
			con_Printf(CONRGB(192,192,255), 0, "Error: \"%s\" is not a valid ShowTicks section");	// (sic) the argument is missing in the original
		}
	}
}


// FUNCTION: LITHTECH 0x00423080
void con_ConsoleHistory(int argc, char *argv[])
{
	// Get the history iterator
	CConHistory *pHistory = GETCONSOLE()->GetCommandHistory();
	int iCount;

	// List the current history
	if ( !pHistory->First() )
		return;

	iCount = 0;
	do
	{
		const char *pLine = pHistory->Get();
		if ( !pLine )
			break;

		dsi_ConsolePrint( (char *)pLine );
		iCount++;
	} while ( pHistory->Next() );
	dsi_ConsolePrint( "%d commands", iCount );
}


// FUNCTION: LITHTECH 0x004230e0
void con_ClearHistory(int argc, char *argv[])
{
	GETCONSOLE()->GetCommandHistory()->Clear();
	dsi_ConsolePrint( "Command history cleared." );
}


// FUNCTION: LITHTECH 0x00423100
void con_WriteHistory(int argc, char *argv[])
{
	CConHistory *pHistory;
	FILE *fOutput;
	int iCount;

	// Argument checking
	if ( argc < 1 )
	{
		dsi_ConsolePrint( "Specify a filename." );
		return;
	}

	// Start the history iterator
	pHistory = GETCONSOLE()->GetCommandHistory();

	if ( !pHistory->First() )
	{
		dsi_ConsolePrint( "No commands in history." );
		return;
	}

	// Open the file
	if ( (fOutput = fopen( argv[0], "wt" )) == LTNULL )
	{
		dsi_ConsolePrint( "Error opening file %s.", argv[0] );
		return;
	}

	iCount = 0;

	do
	{
		// Write this history line to the file
		const char *pLine = pHistory->Get();
		if ( !pLine )
			break;

		fputs( pLine, fOutput );
		fputc( '\n', fOutput );
		iCount++;
	} while ( pHistory->Next() );

	// Close the file
	fclose( fOutput );

	dsi_ConsolePrint( "Successfully wrote %d commands to %s.", iCount, argv[0] );
}


// FUNCTION: LITHTECH 0x004231d0
void con_ReadHistory(int argc, char *argv[])
{
	CConHistory *pHistory;
	FILE *fInput;
	int iCount;
	char aBuffer[MAX_CONSOLE_TEXTLEN];

	// Argument checking
	if ( argc < 1 )
	{
		dsi_ConsolePrint( "Specify a filename." );
		return;
	}

	// Open the file
	if ( (fInput = fopen( argv[0], "rt" )) == LTNULL )
	{
		dsi_ConsolePrint( "Error opening file %s.", argv[0] );
		return;
	}

	// Get the history iterator
	pHistory = GETCONSOLE()->GetCommandHistory();

	iCount = 0;

	while ( !feof( fInput ) )
	{
		// Read a line from the file
		if ( fgets( aBuffer, MAX_CONSOLE_TEXTLEN, fInput ) > 0 )
		{
			int iLength = strlen( aBuffer );
			// Remove trailing newlines
			if ( (iLength > 0) && (aBuffer[iLength - 1] == '\n') )
				aBuffer[--iLength] = 0;

			// Skip blank lines
			if ( !iLength )
				continue;

			// Add the line to the history
			pHistory->Add( aBuffer );
			iCount++;
		}
	}

	// Close the file
	fclose( fInput );

	dsi_ConsolePrint( "Successfully read %d commands from %s.", iCount, argv[0] );
}


// Execute a file.
// FUNCTION: LITHTECH 0x004232c0
void con_Exec(int argc, char *argv[])
{
	// Argument checking
	if ( argc < 1 )
	{
		dsi_ConsolePrint( "Specify a filename." );
		return;
	}

	// Execute the file
	if ( cc_RunConfigFile(&g_ClientConsoleState, argv[0], 0, VARFLAG_SAVE) )
		dsi_ConsolePrint( "Successfully executed %s.", argv[0] );
	else
		dsi_ConsolePrint( "Error executing %s.", argv[0] );
}


// Move the console window.
// FUNCTION: LITHTECH 0x00423320
void con_MoveConsole(int argc, char *argv[])
{
	LTRect rect;
	char cmd[200];

	// Report the console location
	if (!argc)
	{
		dsi_ConsolePrint("Console position : (%d,%d, %d,%d)", g_CV_ConsoleLeft, g_CV_ConsoleTop, g_CV_ConsoleRight, g_CV_ConsoleBottom);
		return;
	}

	// Change the console size
	if (stricmp(argv[0], "Top") == 0)
	{
		rect.left = -1;
		rect.top = -1;
		rect.right = -1;
		rect.bottom = -2;
	}
	else if (stricmp(argv[0], "Bottom" ) == 0)
	{
		rect.left = -1;
		rect.top = -2;
		rect.right = -1;
		rect.bottom = -1;
	}
	else if (stricmp(argv[0], "Middle") == 0)
	{
		rect.left = -4;
		rect.top = -4;
		rect.right = -4;
		rect.bottom = -4;
	}
	else if (stricmp(argv[0], "Full") == 0)
	{
		rect.left = -1;
		rect.top = -1;
		rect.right = -1;
		rect.bottom = -1;
	}
	else
	{
		if (argc < 4)
		{
			dsi_ConsolePrint("Specify Top, Bottom, Full, Middle, or 4 screen coordinates");
			return;
		}

		// Use the coordinates entered by the user
		rect.left = atoi(argv[0]);
		rect.top = atoi(argv[1]);
		rect.right = atoi(argv[2]);
		rect.bottom = atoi(argv[3]);
	}

	// Write the new console rectangle to the console variables
	sprintf(cmd, "ConsoleLeft %d", rect.left);
	c_CommandHandler(cmd);
	sprintf(cmd, "ConsoleTop %d", rect.top);
	c_CommandHandler(cmd);
	sprintf(cmd, "ConsoleRight %d", rect.right);
	c_CommandHandler(cmd);
	sprintf(cmd, "ConsoleBottom %d", rect.bottom);
	c_CommandHandler(cmd);
}


// Save functions.
// FUNCTION: LITHTECH 0x004234d0
void SaveModelAdd(FILE *fp)
{
	fprintf(fp, "ModelAdd %f %f %f\n", g_ConsoleModelAdd.x, g_ConsoleModelAdd.y, g_ConsoleModelAdd.z);
	fprintf(fp, "ModelDirAdd %f %f %f\n", g_ConsoleModelDirAdd.x, g_ConsoleModelDirAdd.y, g_ConsoleModelDirAdd.z);
	fprintf(fp, "WMAmbient %f %f %f\n", g_ConsoleModelDirAdd2.x, g_ConsoleModelDirAdd2.y, g_ConsoleModelDirAdd2.z);
}


void con_ListCommands(int argc, char *argv[]);
void con_Set(int argc, char *argv[]);

// The release BlastServer command is empty; VC6 folds it at 0x004359b0 with other empty handlers.
static void con_BlastServer(int argc, char *argv[])
{
}

LTSaveFn g_SaveFns[2] = {
	input_SaveBindings,
	SaveModelAdd
};

LTCommandStruct g_LTCommandStructs[36] = {
	{ "BlastServer", con_BlastServer, 0 },
	{ "ListInputDevices", con_ListInputDevices, 0 },
	{ "quit", con_Quit, 0 },
	{ "serv", con_ServerCommand, 0 },
	{ "ModelAdd", con_ModelAdd, 0 },
	{ "ModelDirAdd", con_ModelDirAdd, 0 },
	{ "World", con_World, 0 },
	{ "TimeDemo", con_TimeDemo, 0 },
	{ "EnableDevice", con_EnableDevice, 0 },
	{ "RangeBind", con_RangeBind, 0 },
	{ "Bind", con_Bind, 0 },
	{ "UnBind", con_UnBind, 0 },
	{ "Scale", con_Scale, 0 },
	{ "RangeScale", con_RangeScale, 0 },
	{ "AddAction", con_AddAction, 0 },
	{ "SSFile", con_SSFile, 0 },
	{ "UpdateServer", con_UpdateServer, 0 },
	{ "RenderCommand", con_RenderCommand, 0 },
	{ "RCom", con_RenderCommand, 0 },
	{ "Set", con_Set, 0 },
	{ "ListCommands", con_ListCommands, 0 },
	{ "RestartConsole", con_RestartConsole, 0 },
	{ "RestartRender", con_RestartRender, 0 },
	{ "ResizeScreen", con_ResizeScreen, 0 },
	{ "EnvMap", con_EnvMap, 0 },
	{ "RebindTextures", con_RebindTextures, 0 },
	{ "RebindLightmaps", con_RebindLightmaps, 0 },
	{ "HeapCompact", con_HeapCompact, 0 },
	{ "ConsoleHistory", con_ConsoleHistory, 0 },
	{ "ClearHistory", con_ClearHistory, 0 },
	{ "WriteHistory", con_WriteHistory, 0 },
	{ "ReadHistory", con_ReadHistory, 0 },
	{ "Exec", con_Exec, 0 },
	{ "MoveConsole", con_MoveConsole, 0 },
	{ "ShowTicks", con_ShowTicks, 0 },
	{ "WMAmbient", con_WMAmbient, 0 }
};


//------------------------------------------------------------------
// FUNCTION: LITHTECH 0x00423560
void con_ListCommands(int argc, char *argv[])
{
	int i;

	for(i=0; i < g_ClientConsoleState.m_nCommandStructs; i++)
	{
		con_WhitePrintf(g_ClientConsoleState.m_pCommandStructs[i].pCmdName);
	}
}


//------------------------------------------------------------------
// FUNCTION: LITHTECH 0x00423590
void con_Set(int argc, char *argv[])
{
	LTCommandVar *pCurVar;
	HHashIterator *hIterator;
	HHashElement *hElement;
	hIterator = hs_GetFirstElement( g_ClientConsoleState.m_VarHash );
	while(hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		if( !hElement )
			continue;

		pCurVar = ( LTCommandVar * )hs_GetElementUserData( hElement );
		cc_PrintVarDescription(&g_ClientConsoleState, pCurVar);
	}
}


//------------------------------------------------------------------
// The engine's global operator new/delete live at these addresses (the linker folded identical
// functions, so they double as the console state's Alloc/Free).
// FUNCTION: LITHTECH 0x004235e0 ??2@YAPAXI@Z
void* operator new(size_t size)
{
	return dalloc(size);
}

// FUNCTION: LITHTECH 0x004235f0 ??3@YAXPAX@Z
void operator delete(void *ptr)
{
	dfree(ptr);
}


//------------------------------------------------------------------
//------------------------------------------------------------------
// Main parsing / command handler
//------------------------------------------------------------------
//------------------------------------------------------------------

// FUNCTION: LITHTECH 0x00423600
void c_InitConsoleCommands()
{
	memset(&g_ClientConsoleState, 0, sizeof(g_ClientConsoleState));

	g_ClientConsoleState.m_SaveFns = g_SaveFns;
	g_ClientConsoleState.m_nSaveFns = NUM_SAVEFNS;

	g_ClientConsoleState.m_pEngineVars = GetEngineVars();
	g_ClientConsoleState.m_nEngineVars = GetNumEngineVars();

	g_ClientConsoleState.m_pCommandStructs = g_LTCommandStructs;
	g_ClientConsoleState.m_nCommandStructs = NUM_COMMANDSTRUCTS;

	g_ClientConsoleState.ConsolePrint = con_WhitePrintf;

	g_ClientConsoleState.Alloc = operator new;
	g_ClientConsoleState.Free = operator delete;

	cc_InitState(&g_ClientConsoleState);

	// Set up the completion iterator
	GETCONSOLE()->SetCompletionIterator( &g_ClientConIterator );
}


// FUNCTION: LITHTECH 0x00423690
void c_TermConsoleCommands()
{
	cc_TermState(&g_ClientConsoleState);
}


// FUNCTION: LITHTECH 0x004236a0
void c_CommandHandler(const char *pCommand)
{
	cc_HandleCommand(&g_ClientConsoleState, pCommand);
}



// FUNCTION: LITHTECH 0x004236c0
CClientConIterator::CClientConIterator() :
	m_eState(STATE_COMMAND),
	m_iCommandIndex(0),
	m_hVarIndex(0),
	m_hCurVar(0),
	m_iEngineIndex(0)
{
}

// Compiler-generated scalar deleting destructor.
// FUNCTION: LITHTECH 0x004236e0 ??_GCClientConIterator@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00423700
CClientConIterator::~CClientConIterator()
{
	// Nothing to destruct...
}

// FUNCTION: LITHTECH 0x00423710
LTBOOL CClientConIterator::Begin()
{
	// Go to the beginning of the command list
	m_eState = STATE_COMMAND;
	m_iCommandIndex = 0;

	// This isn't guaranteed to be true, but I'm pretty sure it will be...
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00423720
LTBOOL CClientConIterator::NextItem()
{
	LTBOOL bEnd = LTFALSE;

	switch (m_eState)
	{
		case STATE_COMMAND :
		{
			// Move to the next command
			m_iCommandIndex++;
			// Overflow to the variable list
			if ( m_iCommandIndex >= g_ClientConsoleState.m_nCommandStructs )
			{
				// Go to variable mode
				m_eState = STATE_CONVAR;
				m_hVarIndex = 0;
				bEnd = !NextItem();
			}
			break;
		}
		case STATE_CONVAR :
		{
			if (!m_hVarIndex)
			{
				m_hVarIndex = hs_GetFirstElement( g_ClientConsoleState.m_VarHash );
				m_hCurVar = LTNULL;
			}

			// find the next variable
			do {
				m_hCurVar = hs_GetNextElement(m_hVarIndex);
			} while ( m_hVarIndex && !m_hCurVar );

			// Overflow to the engine variable list
			if (!m_hVarIndex)
			{
				m_eState = STATE_ENGINEVAR;
				m_iEngineIndex = -1;
				bEnd = !NextItem();
			}
			break;
		}
		case STATE_ENGINEVAR :
		{
			if (m_iCommandIndex >= GetNumEngineVars())
			{
				// Nothing to overflow to, so we're at the end
				bEnd = LTTRUE;
			}
			else
			{
				m_iEngineIndex++;
			}
		}
	}

	return !bEnd;
}

// FUNCTION: LITHTECH 0x00423810
const char *CClientConIterator::Get() const
{
	const char *pResult = LTNULL;

	switch (m_eState)
	{
		// Get a command
		case STATE_COMMAND :
		{
			// Make sure we're not past the end of the list
			if ( m_iCommandIndex < g_ClientConsoleState.m_nCommandStructs )
				pResult = g_ClientConsoleState.m_pCommandStructs[m_iCommandIndex].pCmdName;
			break;
		}
		// Get a console variable
		case STATE_CONVAR :
		{
			// Make sure we're not past the end of the list
			if ( m_hCurVar )
				pResult = (( LTCommandVar * )hs_GetElementUserData( m_hCurVar ))->pVarName;
			break;
		}
		// Get an engine variable
		case STATE_ENGINEVAR :
		{
			// Make sure we're not past the end of the list
			if ( m_iEngineIndex < GetNumEngineVars() )
				pResult = GetEngineVars()[m_iEngineIndex].pVarName;
			break;
		}
	}

	return pResult;
}
