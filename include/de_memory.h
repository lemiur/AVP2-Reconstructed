// Defines all the memory management routines for DirectEngine
// (Jupiter runtime/kernel/mem/src/de_memory.h).
#ifndef __DE_MEMORY_H__
#define __DE_MEMORY_H__

#include <stddef.h>
#include "ltbasetypes.h"

// Init/term the memory system.
void dm_Init();
void dm_Term();
void dm_HeapCompact();		// 0x0042fdd0

// Talon: callbacks run when an allocation fails, before the allocation is retried
// (the client registers one to free memory).
typedef void (*MemoryFailureFn)(void *pUser);
void dm_AddMemoryFailureCallback(MemoryFailureFn fn, void *pUser);

// C dalloc/dfree functions.
void* dalloc(size_t size);		// 0x0042fed0
void* dalloc_z(size_t size);	// Allocate and zero-init.

void dfree(void *ptr);			// 0x0042ffb0

extern uint32 g_PD_Malloc;
extern uint32 g_PD_Free;

#endif  // __DE_MEMORY_H__
