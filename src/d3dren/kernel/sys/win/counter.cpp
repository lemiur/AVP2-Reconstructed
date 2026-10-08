// d3d.ren kernel/sys/win/counter (0x10013215-0x100132a0): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// engine kernel/sys/win/counter.cpp twin; last function ends with the 0xCC pad of the P->A edge.
// FLAGS: /O1 /Ob2
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <windows.h>
#include "ltbasedefs.h"
#include "counter.h"
#include "../../../../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/tlvertex.h"
#include "d3dren/d3ddevice.h"
#include "pixelformat.h"

// ------------------------------------------------------------------ //
// The lists of polygons touched by dynamic lights (names unknown, shapes in the comments).
// ------------------------------------------------------------------ //

// The per-poly record of a dynamic light touching it (StructBank g_PolyLightBank, 0x14 bytes) and the list of lit polys
// (StructBank g_LitPolyBank, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;		// 0x00
	LTObject			*m_pLight;		// 0x04
	LTVector			m_Pos;			// 0x08 the light position in the world model space
};

#define WORLDPOLY_LIGHTS(p)	(*(UnkType_PolyLight**)((uint8*)(p) + 0x30))

// Stores a function address in a RenderStruct slot.  The slots are typed in include/renderstruct.h, but some of them are
// padding there (GetOptimized2DBlend/Color, IsInOptimized2D and the unnamed 0xc8) and several functions of the units that
// define them take Jupiter-style arguments, so the store goes through void *.
#define RS_SET(member, fn)	(*(void **)&pStruct->member = (void *)(fn))
#define RS_SET_PAD(offset, fn)	(*(void **)((uint8 *)pStruct + (offset)) = (void *)(fn))

#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'

// ------------------------------------------------------------------ //
// counter: the engine's own copy of src/kernel/sys/win/counter.cpp (names from there).
// ------------------------------------------------------------------ //

// GLOBAL: D3DREN 0x10048c48
static unsigned __int64 g_CountDiv = 1;

class CountDivSetter
{
public:
	CountDivSetter()
	{
		LARGE_INTEGER perSec;

		QueryPerformanceFrequency(&perSec);
		g_CountDiv = 1;
	}
};
// FUNCTION: D3DREN 0x10013215 _$E2
static CountDivSetter __g_CountDivSetter;

// FUNCTION: D3DREN 0x10013236
Counter::Counter(unsigned long startMode)
{
	if(startMode == CSTART_MICRO)
		StartMicro();
	else if(startMode == CSTART_MILLI)
		StartMS();
}

void Counter::StartMS()
{
	LARGE_INTEGER *pInt;

	pInt = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(pInt);
}

void Counter::StartMicro()
{
	LARGE_INTEGER *pInt;

	pInt = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(pInt);
}

unsigned long Counter::EndMicro()
{
	LARGE_INTEGER curCount, *pInCount;

	pInCount = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(&curCount);

	return (unsigned long)((curCount.QuadPart - pInCount->QuadPart) / g_CountDiv);
}

// NAME: cnt_StartCounter, cnt_EndCounter: the engine's names (src/kernel/sys/win/counter.cpp, the same two functions)
// FUNCTION: D3DREN 0x10013254
void cnt_StartCounter(Counter &cCounter)
{
	cCounter.StartMicro();
}

// FUNCTION: D3DREN 0x1001325f
unsigned long cnt_EndCounter(Counter &cCounter)
{
	return cCounter.EndMicro();
}
