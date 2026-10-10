// Engine-wide basics (Jupiter runtime/kernel/src/bdefs.h): error-return macros, debug level and
// the console print used everywhere. Include this instead of redeclaring these per unit.
#ifndef __BDEFS_H__
#define __BDEFS_H__

#include "ltbasedefs.h"

#define MAX_CREAL	1E+37

// Talon StdLith default allocator (l_allocator.h).
class LAlloc;
// GLOBAL: LITHTECH 0x004e6a18
extern LAlloc g_DefAlloc;

extern int32 g_DebugLevel;
extern char *g_ReturnErrString;
void dsi_OnReturnError(LTRESULT err);
// 0x004351f0 (Ghidra: dsi_PrintToConsole)
void dsi_ConsolePrint(const char *pMsg, ...);

#define RETURN_ERROR_PARAM(debugLevel, fnName, err, param) { \
	dsi_OnReturnError(err);\
	if (g_DebugLevel >= debugLevel) \
		dsi_ConsolePrint(g_ReturnErrString, #fnName, #err, param); \
	return (err); }

#define RETURN_ERROR_NO_TOKEN_PASTE(debugLevel, fnName, err) \
{ \
	dsi_OnReturnError(err);\
	if (g_DebugLevel >= debugLevel)\
	{\
		dsi_ConsolePrint(g_ReturnErrString, fnName, #err, " "); \
	}\
	return (err); \
}

#define RETURN_ERROR(debugLevel, fnName, err) \
	RETURN_ERROR_NO_TOKEN_PASTE(debugLevel, #fnName, err)

// Per-function name pointer: a static char* in .data, read by ERR and CHECK_PARAMS2.
#define FN_NAME(name) \
	static char *___bdefs__pFnName = #name;

#define ERR(code, error) \
	RETURN_ERROR_NO_TOKEN_PASTE(code, ___bdefs__pFnName, error)

#define DEBUG_PRINT(debugLevel, toPrint) { \
	if (g_DebugLevel >= debugLevel) \
		dsi_ConsolePrint toPrint; }

// A lot like RETURN_ERROR, but it doesn't return the error value.
#define GENERATE_ERROR(debugLevel, fnName, err, errorString)\
{\
	dsi_OnReturnError(err);\
	if (g_DebugLevel >= debugLevel)\
	{\
		dsi_ConsolePrint(g_ReturnErrString, #fnName, #err, errorString); \
	}\
}

#define CHECK_PARAMS(condition, fnName)\
	if (!(condition)) \
	{ RETURN_ERROR(2, fnName, LT_INVALIDPARAMS); }

#define CHECK_PARAMS2(condition)\
	if (!(condition)) \
	{ RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS); }

#endif  // __BDEFS_H__
