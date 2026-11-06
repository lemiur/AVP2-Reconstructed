// lithshared stdlith/dynarray.cpp (Talon). The Talon .cpp is not on disk (build\proj\LT2\lithshared has
// only the headers); this is Jupiter libs/stdlith/dynarray.cpp with uint32 -> DWORD (Talon dynarray.h).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/stdlith

#include "dynarray.h"


// FUNCTION: LITHTECH 0x004b3410
DWORD MoArray_FindElementMemcmp(
	const void *pToFind,
	const void *pArray, 
	DWORD nElements, 
	DWORD elementSize)
{
	const char *pCur = (const char *)pToFind;
	DWORD i;


	for(i=0; i < nElements; i++)
	{
		if(memcmp(pCur, pToFind, elementSize) == 0)
			return i;
	
		pCur += elementSize;
	}

	return BAD_INDEX;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from the GPL release of the LithTech Jupiter Ex engine; see README.md (License).
