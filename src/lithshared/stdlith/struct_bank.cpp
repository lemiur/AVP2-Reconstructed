// lithshared stdlith/struct_bank.cpp (Talon). The Talon .cpp is not on disk; this is Jupiter
// libs/stdlith/struct_bank.cpp with the Talon header's types (unsigned long/int/BOOL instead of
// uint32/int32/LTBOOL).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/stdlith


#include "stdlith.h"
#include "struct_bank.h"
#include "struct_bank_debug.h"


int g_bDebugStructBanks = 0;



// FUNCTION: LITHTECH 0x004b26b0
void sb_Init(StructBank *pBank, unsigned long structSize, unsigned long cacheSize)
{
	pBank->m_StructSize = structSize;
	
	pBank->m_AlignedStructSize = structSize;
	if(pBank->m_AlignedStructSize < sizeof(StructLink))
		pBank->m_AlignedStructSize = sizeof(StructLink);

	if(pBank->m_AlignedStructSize % 4 != 0)
	{
		pBank->m_AlignedStructSize += (4 - (pBank->m_AlignedStructSize % 4));
 	}
	
	pBank->m_nPages = 0;
	pBank->m_nTotalObjects = 0;
	pBank->m_CacheSize = cacheSize;
	pBank->m_PageHead = NULL;
	pBank->m_FreeListHead = NULL;
}


// FUNCTION: LITHTECH 0x004b2700
int sb_Init2(StructBank *pBank, unsigned long structSize, unsigned long cacheSize, unsigned long nPreallocations)
{
	sb_Init(pBank, structSize, cacheSize);

	#ifdef _DEBUG
		// No preallocations if debugging..
		if(g_bDebugStructBanks)
			return 1;
	#endif

	return sb_AllocateNewStructPage(pBank, nPreallocations);
}


void sb_FreeAll(StructBank *pBank)
{
	StructBankPage *pPage;
	StructLink *pStruct;
	BYTE *pDataPos;
	unsigned long count;

	pBank->m_FreeListHead = NULL;

	pPage = pBank->m_PageHead;
	while(pPage)
	{
		#ifdef _DEBUG
			memset(pPage->m_Data, 0xEA, pBank->m_AlignedStructSize * pPage->m_nObjects);
		#endif

		pDataPos = (BYTE*)pPage->m_Data;
		count = pPage->m_nObjects;
		while(count--)
		{
			pStruct = (StructLink*)pDataPos;
			pStruct->m_pSLNext = pBank->m_FreeListHead;
			pBank->m_FreeListHead = pStruct;
			pDataPos += pBank->m_AlignedStructSize;
		}
		
		pPage = pPage->m_pNext;
	}
}


// FUNCTION: LITHTECH 0x004b2730
void sb_Term2(StructBank *pBank, int bCheckObjects)
{
	StructBankPage *pPage;
	StructBankPage *pNext;

	#ifdef _DEBUG
		unsigned long freeListCount;
		StructLink *pCur;

		if(bCheckObjects)
		{
			// Make sure everything is freed.
			freeListCount = 0;
			for(pCur=pBank->m_FreeListHead; pCur; pCur=pCur->m_pSLNext)
			{
				++freeListCount;
			}

			ASSERT(freeListCount == pBank->m_nTotalObjects);
		}
	#endif

	pPage = pBank->m_PageHead;
	while(pPage)
	{
		pNext = pPage->m_pNext;
		g_DefAlloc.Free(pPage);
		pPage = pNext;
	}

	pBank->m_PageHead = NULL;
	pBank->m_FreeListHead = NULL;
	pBank->m_nPages = 0;
	pBank->m_nTotalObjects = 0;
}


// FUNCTION: LITHTECH 0x004b2770
void sb_Term(StructBank *pBank)
{
	sb_Term2(pBank, 0);
}


// FUNCTION: LITHTECH 0x004b2780
int sb_AllocateNewStructPage(StructBank *pBank, unsigned long nAllocations)
{
	StructLink *pStruct;
	StructBankPage *pPage;
	BYTE *pDataPos;
	unsigned long count;

	
	if(nAllocations == 0)
		return 1;

	// Allocate a new page.
	pPage = (StructBankPage*)g_DefAlloc.Alloc((pBank->m_AlignedStructSize*nAllocations) + (sizeof(StructBankPage)-sizeof(unsigned long)));
	if(!pPage)
		return 0;

	pPage->m_pNext = pBank->m_PageHead;
	pPage->m_nObjects = nAllocations;

	pBank->m_PageHead = pPage;

	++pBank->m_nPages;
	pBank->m_nTotalObjects += nAllocations;

	// Put its contents into the free list.
	pDataPos = (BYTE*)pPage->m_Data;
	count = nAllocations;
	while(count--)
	{
		pStruct = (StructLink*)pDataPos;
		pStruct->m_pSLNext = pBank->m_FreeListHead;
		pBank->m_FreeListHead = pStruct;
		pDataPos += pBank->m_AlignedStructSize;
	}

	return 1;
}


BOOL sb_IsObjectAllocated(StructBank *pBank, void *pObj)
{
	BYTE *pBytes;
	unsigned long count;

	// The first 4 bytes are our StructLink, but the rest should be 0xEA.

	pBytes = (BYTE*)pObj;
	pBytes += sizeof(StructLink*);

	if(pBank->m_StructSize > sizeof(StructLink*))
	{
		count = pBank->m_StructSize - sizeof(StructLink*);
		while(count--)
		{
			if(*pBytes != 0xEA)
				return TRUE;
		
			++pBytes;
		}
	
		// All bytes are 0xEA.. this object is freed.
		return FALSE;
	}
	else
	{
		return TRUE;
	}
}

