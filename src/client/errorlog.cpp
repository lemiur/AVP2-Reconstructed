// Jupiter runtime/client/src/errorlog.cpp
// Talon prints through a "- %s\n" format instead of checking for a trailing newline.
#include <stdio.h>
#include <string.h>
#include "bdefs.h"
#include "concommand.h"
#include "consolecommands.h"
#include "console.h"
#include "engine_vars.h"

// GLOBAL: LITHTECH 0x004e37ac
static FILE *g_ErrorLogFP;



// FUNCTION: LITHTECH 0x004361b0
void PrintToErrorLog(const char *pMsg)
{
	if(g_bErrorLog && g_ErrorLogFP)
	{
		fprintf(g_ErrorLogFP, "- %s\n", pMsg);

		if(g_bAlwaysFlushLog)
			fflush(g_ErrorLogFP);
	}
}

// FUNCTION: LITHTECH 0x004361f0
void InitErrorLog()
{
	char *pFilename;
	LTCommandVar *pVar = cc_FindConsoleVar(&g_ClientConsoleState, "errorlogfile");

	pFilename = "error.log";
	if(pVar)
	{
		pFilename = pVar->pStringVal;
	}

	if(g_bErrorLog)
	{
		g_ErrorLogFP = fopen(pFilename, "wtc");
	}

	con_SetErrorLog(PrintToErrorLog);
}

// FUNCTION: LITHTECH 0x00436240
void TermErrorLog()
{
	if(g_ErrorLogFP)
	{
		fflush(g_ErrorLogFP);
		fclose(g_ErrorLogFP);
		g_ErrorLogFP = LTNULL;
	}
}
