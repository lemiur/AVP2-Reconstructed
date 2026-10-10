// Jupiter runtime/shared/src/bdefs.cpp: the engine-wide debug level and error-report format (bdefs.h).
// The object holds no code; its .data and .bss lie between animtracker's and bindmgr's.
#include "bdefs.h"


// GLOBAL: LITHTECH 0x004de2b0
int32	g_DebugLevel=0;
// GLOBAL: LITHTECH 0x004cf15c
char *g_ReturnErrString = "LT ERROR: %s returned %s (%s)";
