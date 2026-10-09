// Server console variables and counters, defined in engine_vars.cpp (g_LTEngineVars points at them).
#ifndef __SERVER_VARS_H__
#define __SERVER_VARS_H__

#include "ltbasedefs.h"

extern uint32 g_Ticks_ClassUpdate;
extern float g_ServerFPS;
extern float g_CV_TimeScale;
extern float g_DebugMaxPos;
extern float g_CV_DebugMaxDims;
extern LTBOOL g_CV_ShowGameTime;
extern LTBOOL g_CV_ShowClassTicks;
extern LTBOOL g_CV_ShowSphereFindTicks;
extern LTBOOL g_CV_ShowPolyFindTicks;

#endif  // __SERVER_VARS_H__
