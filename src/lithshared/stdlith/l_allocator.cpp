// lithshared stdlith/l_allocator.cpp (Talon). The Talon .cpp is not on disk; this is Jupiter
// libs/stdlith/l_allocator.cpp adapted to the Talon l_allocator.h: DWORD/BOOL types and no
// bQuadWordAlign parameter. The engine defines STDLITH_ALLOC_OVERRIDE (DefStdlithAlloc/Free live in the
// engine, folded with operator new/delete), so the default implementations are not here.
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/stdlith

#include "l_allocator.h"


// FUNCTION: LITHTECH 0x004b3200 _$E4
// FUNCTION: LITHTECH 0x004b3210 _$E1
// FUNCTION: LITHTECH 0x004b3220 _$E3
// FUNCTION: LITHTECH 0x004b3230 _$E2
// GLOBAL: LITHTECH 0x004e6a18
LAlloc g_DefAlloc;

// FUNCTION: LITHTECH 0x004b3240 ??1LAlloc@@UAE@XZ
// FUNCTION: LITHTECH 0x004b3250 ?Alloc@LAlloc@@UAEPAXK@Z
// FUNCTION: LITHTECH 0x004b3260 ?Free@LAlloc@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x004b32c0 ??_GLAlloc@@UAEPAXI@Z



// -------------------------------------------------------------------------------- //
// LAllocCount implementation.
// -------------------------------------------------------------------------------- //

LAllocCount::LAllocCount(LAlloc *pDelegate)
{
	m_pDelegate = pDelegate;
	
	ClearCounts();
}


void LAllocCount::ClearCounts()
{
	m_nTotalAllocations = 0;
	m_nTotalFrees = 0;
	
	m_TotalMemoryAllocated = 0;
	m_nCurrentAllocations = 0;

	m_nAllocationFailures = 0;
}


void* LAllocCount::Alloc(DWORD size)
{
	void *pRet;
	
	if(size == 0)
		return NULL;

	pRet = m_pDelegate->Alloc(size);
	if(pRet)
	{
		m_nTotalAllocations++;
		m_TotalMemoryAllocated += size;
		m_nCurrentAllocations++;
	}
	else
	{
		m_nAllocationFailures++;
	}

	return pRet;
}


void LAllocCount::Free(void *ptr)
{
	if(!ptr)
		return;

	m_pDelegate->Free(ptr);
	m_nCurrentAllocations--;
	m_nTotalFrees++;
}



// -------------------------------------------------------------------------------- //
// LAllocSimpleBlock implementation.
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004b3270
LAllocSimpleBlock::LAllocSimpleBlock()
{
	Clear();
}

// FUNCTION: LITHTECH 0x004b32e0 ??_GLAllocSimpleBlock@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x004b3300
LAllocSimpleBlock::~LAllocSimpleBlock()
{
	Term();
}


// FUNCTION: LITHTECH 0x004b3350
BOOL LAllocSimpleBlock::Init(LAlloc *pDelegate, DWORD blockSize)
{
	Term();

	blockSize = (blockSize + 3) & ~3;

	if(blockSize > 0)
	{
		m_pBlock = (BYTE*)pDelegate->Alloc(blockSize);
		if(!m_pBlock)
			return FALSE;
	}

	m_pDelegate = pDelegate;
	m_BlockSize = blockSize;
	m_CurBlockPos = 0;

	return TRUE;
}


// FUNCTION: LITHTECH 0x004b33a0
void LAllocSimpleBlock::Term()
{
	if(m_pDelegate)
	{
		m_pDelegate->Free(m_pBlock);
	}

	Clear();
}


// FUNCTION: LITHTECH 0x004b33c0
void* LAllocSimpleBlock::Alloc(DWORD size)
{
	BYTE *pRet;

	if(size == 0)
		return NULL;

	size = ((size + 3) & ~3);
	if((m_CurBlockPos + size) > m_BlockSize)
		return NULL;

	pRet = &m_pBlock[m_CurBlockPos];
	m_CurBlockPos += size;
	return pRet;
}


void LAllocSimpleBlock::Free(void *ptr)
{
}


// FUNCTION: LITHTECH 0x004b3400
void LAllocSimpleBlock::Clear()
{
	m_pDelegate = NULL;
	m_pBlock = NULL;
	m_CurBlockPos = 0;
	m_BlockSize = 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from the GPL release of the LithTech Jupiter Ex engine; see README.md (License).
