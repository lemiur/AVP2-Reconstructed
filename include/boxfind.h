// The box FindPoliesTouchingBox tests polies against (PolyTouchesBox reads these). Defined in collision.cpp:
// g_BoxFindRadius is directly followed by g_BoxFindCenter in memory (they are read as one sphere).
#ifndef __BOXFIND_H__
#define __BOXFIND_H__

#include "ltbasedefs.h"

extern float g_BoxFindRadius;
extern LTVector g_BoxFindCenter;
extern LTPlane g_BoxFindPlanes[6];

// PolyTouchesBox profiling counter.
// GLOBAL: LITHTECH 0x004e24a8
extern uint32 g_nPolyTouchesBoxCalls;

#endif  // __BOXFIND_H__
