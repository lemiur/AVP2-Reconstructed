// Jupiter runtime/kernel/mem/src/sys/win/de_memory.cpp
// Talon allocates straight from the CRT heap and keeps a list of callbacks to run when an
// allocation fails.
#include <windows.h>
#include <new.h>
#include <malloc.h>
#include <string.h>
#include "bdefs.h"
#include "de_memory.h"
#include "counter.h"

void dsi_OnMemoryFailure();

struct MemoryFailureCallback
{
	MemoryFailureFn			m_Fn;		// 0x00
	void					*m_pUser;	// 0x04
	MemoryFailureCallback	*m_pNext;	// 0x08
};


// GLOBAL: LITHTECH 0x004e3464
static uint32 g_MemoryUsage;
// GLOBAL: LITHTECH 0x004e3478
static uint32 g_nAllocations;

// GLOBAL: LITHTECH 0x004e3474
uint32 g_nTotalAllocations;
// GLOBAL: LITHTECH 0x004e346c
uint32 g_nTotalFrees;

// GLOBAL: LITHTECH 0x004e347c
static int g_MemRefCount=0;
// GLOBAL: LITHTECH 0x004e3468
static _PNH g_OldNewHandler;

// GLOBAL: LITHTECH 0x004e3470
static MemoryFailureCallback *g_pMemoryFailureCallbacks;

// PlayDemo profile info.
// GLOBAL: LITHTECH 0x004e3480
uint32 g_PD_Malloc=0;
// GLOBAL: LITHTECH 0x004e3484
uint32 g_PD_Free=0;


// FUNCTION: LITHTECH 0x0042fe40
static int dm_NewHandler(size_t size)
{
	dsi_OnMemoryFailure();
	return 0;
}


// FUNCTION: LITHTECH 0x0042fdd0
void dm_HeapCompact()
{
	HANDLE hHeap;

	if(hHeap = GetProcessHeap())
	{
		HeapCompact(hHeap, 0);
	}
}


// FUNCTION: LITHTECH 0x0042fdf0
void dm_Init()
{
	if(g_MemRefCount == 0)
	{
		g_pMemoryFailureCallbacks = LTNULL;
		g_OldNewHandler = _set_new_handler(dm_NewHandler);

		g_MemoryUsage = 0;
		g_nAllocations = 0;
		g_nTotalAllocations = g_nTotalFrees = 0;
	}

	g_MemRefCount++;
}


// FUNCTION: LITHTECH 0x0042fe50
void dm_Term()
{
	MemoryFailureCallback *pCur, *pNext;

	g_MemRefCount--;
	if(g_MemRefCount == 0)
	{
		pCur = g_pMemoryFailureCallbacks;
		while(pCur)
		{
			pNext = pCur->m_pNext;
			free(pCur);
			pCur = pNext;
		}

		// Restore the old new handler.
		_set_new_handler(g_OldNewHandler);

		g_MemoryUsage = 0;
		g_nAllocations = 0;
	}
}


// FUNCTION: LITHTECH 0x0042fea0
void dm_AddMemoryFailureCallback(MemoryFailureFn fn, void *pUser)
{
	MemoryFailureCallback *pCallback;

	pCallback = (MemoryFailureCallback*)malloc(sizeof(MemoryFailureCallback));
	if(pCallback)
	{
		pCallback->m_Fn = fn;
		pCallback->m_pUser = pUser;
		pCallback->m_pNext = g_pMemoryFailureCallbacks;
		g_pMemoryFailureCallbacks = pCallback;
	}
}


static void dm_CallMemoryFailureCallbacks();

// FUNCTION: LITHTECH 0x0042fed0
void* dalloc(size_t size)
{
	char *ptr;

	if(size == 0)
	{
		return LTNULL;
	}

	// Try to allocate the memory.
	{
		CountAdder cntAdd(&g_PD_Malloc);
		ptr = (char*)malloc(size);
	}

	if(!ptr)
	{
		// Let everyone free what they can and try again.
		dm_CallMemoryFailureCallbacks();
		ptr = (char*)malloc(size);
		if(!ptr)
		{
			dsi_OnMemoryFailure();
		}
	}

	++g_nAllocations;
	++g_nTotalAllocations;

	return ptr;
}


// FUNCTION: LITHTECH 0x0042ff60
static void dm_CallMemoryFailureCallbacks()
{
	MemoryFailureCallback *pCur;

	for(pCur=g_pMemoryFailureCallbacks; pCur; pCur=pCur->m_pNext)
	{
		pCur->m_Fn(pCur->m_pUser);
	}
}


// FUNCTION: LITHTECH 0x0042ff80
void* dalloc_z(size_t size)
{
	void *ret;

	ret = dalloc(size);
	if(ret)
	{
		memset(ret, 0, size);
	}

	return ret;
}


// FUNCTION: LITHTECH 0x0042ffb0
void dfree(void *ptr)
{
	if(!ptr)
		return;

	--g_nAllocations;
	++g_nTotalFrees;

	{
		CountAdder cntAdd(&g_PD_Free);
		free(ptr);
	}
}

// StdLith's base allocators (STDLITH_ALLOC_OVERRIDE).
// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004235e0 (operator new).
void* DefStdlithAlloc(uint32 size)
{
	return dalloc(size);
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004235f0 (operator delete).
void DefStdlithFree(void *ptr)
{
	dfree(ptr);
}

