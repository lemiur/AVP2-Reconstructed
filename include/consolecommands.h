// Client console commands (Jupiter client/src/consolecommands.h).
#ifndef __CONSOLECOMMANDS_H__
#define __CONSOLECOMMANDS_H__

#include "concommand.h"

extern ConsoleState g_ClientConsoleState;

void c_InitConsoleCommands();
void c_TermConsoleCommands();
void c_CommandHandler(const char *pCommand);

// The console tables are defined below the command handlers.
// GLOBAL: LITHTECH 0x004d09b8
extern LTSaveFn g_SaveFns[2];
// GLOBAL: LITHTECH 0x004d09c0
extern LTCommandStruct g_LTCommandStructs[36];

#endif
