// lithshared stdlith/stringholder.cpp (Talon). The Talon .cpp is not on disk; this is Jupiter
// libs/stdlith/stringholder.cpp with the Talon header's types (BOOL/WORD/DWORD).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/stdlith

//------------------------------------------------------------------
//
//	FILE	  : StringHolder.cpp
//
//	PURPOSE	  : Implementation for the CStringHolder class.
//
//	CREATED	  : 5/1/96
//
//	COPYRIGHT : Monolith 1996 All Rights Reserved
//
//------------------------------------------------------------------

// Includes....
#include <string.h>
#include "stringholder.h"


#define DEFAULT_ALLOC_SIZE	150


// FUNCTION: LITHTECH 0x004b2a80 ??0CStringHolder@@QAE@XZ
CStringHolder::CStringHolder()
{
	m_AllocSize = DEFAULT_ALLOC_SIZE;
}


CStringHolder::CStringHolder( unsigned short allocSize )
{
	m_AllocSize = allocSize;
}


// FUNCTION: LITHTECH 0x004b2aa0
CStringHolder::~CStringHolder()
{
	ClearStrings();
}


// FUNCTION: LITHTECH 0x004b2b10
void CStringHolder::SetAllocSize( unsigned short size )
{
	ASSERT( m_Strings.GetSize() == 0 );
	m_AllocSize = size;
}


// FUNCTION: LITHTECH 0x004b2b20 ?AddString@CStringHolder@@QAEPADPBDH@Z
char *CStringHolder::AddString( const char *pString, BOOL bFindFirst )
{
	WORD	len = strlen(pString);
	return AddString( pString, bFindFirst, len );
}


// FUNCTION: LITHTECH 0x004b2b50 ?AddString@CStringHolder@@QAEPADPBDHK@Z
char *CStringHolder::AddString( const char *pString, BOOL bFindFirst, unsigned long len )
{
	DWORD i;
	WORD allocSize;
	BOOL bFoundOne;
	char *pRetVal;
	SBank *pBank, theString;


	ASSERT( len <= m_AllocSize );

	// See if we can find it in an array first.
	if( bFindFirst )
	{
		pRetVal = FindString( pString );
		if( pRetVal )
			return pRetVal;
	}

	// See if it'll fit in any of the arrays.
	bFoundOne = FALSE;
	for( i=0; i < m_Strings; i++ )
	{
		pBank = &m_Strings[i];

		if((len+pBank->m_StringSize+1) < pBank->m_AllocSize)
		{
			bFoundOne = TRUE;
			break;
		}
	}

	if( !bFoundOne )
	{
		allocSize = (WORD)(len+1);
		if(m_AllocSize > allocSize)
			allocSize = m_AllocSize;
		
		theString.m_pString = new char[allocSize];
		if( !theString.m_pString )
			return NULL;
		
		theString.m_AllocSize = allocSize;
		theString.m_StringSize = 0;
		if( !m_Strings.Append(theString) )
		{
			theString.m_pString;
			return NULL;
		}
		
		i = (WORD)(m_Strings - 1);
	}

	pBank = &m_Strings[i];
	pRetVal = pBank->m_pString + pBank->m_StringSize;

	memcpy( pRetVal, pString, len );
	pRetVal[len] = 0;

	pBank->m_StringSize += (WORD)(len+1);
	ASSERT( pBank->m_StringSize <= pBank->m_AllocSize );

	return pRetVal;
}


// FUNCTION: LITHTECH 0x004b2c50
char *CStringHolder::FindString( const char *pString )
{
	WORD a, i, len;
	char *pBaseString;
	SBank *pBank;

	for( a=0; a < m_Strings; a++ )
	{
		pBank = &m_Strings[a];
		pBaseString = pBank->m_pString;
		
		i=0;
		while( i < pBank->m_StringSize )
		{
			len = strlen( &pBaseString[i] );
			if( strcmp(pString, &pBaseString[i]) == 0 )
				return &pBaseString[i];
	
			i += len + 1;
		}	
	}

	return NULL;
}



// FUNCTION: LITHTECH 0x004b2d20
void CStringHolder::ClearStrings()
{
	DWORD i;

	for( i=0; i < m_Strings; i++ )
	{
		delete m_Strings[i].m_pString;
	}

	m_Strings.SetSize(0);
}




// CMoArray<SBank> template instances (vtable entries) emitted by this object:
// FUNCTION: LITHTECH 0x004b2d70 ??1?$CMoArray@VSBank@@VDefaultCache@@@@QAE@XZ
// FUNCTION: LITHTECH 0x004b2da0 ?GenAppend@?$CMoArray@VSBank@@VDefaultCache@@@@UAEHAAVSBank@@@Z
// FUNCTION: LITHTECH 0x004b2ec0 ?GenCopyList@?$CMoArray@VSBank@@VDefaultCache@@@@UAEHABV?$GenList@VSBank@@@@@Z
// FUNCTION: LITHTECH 0x004b2ff0 ?GenAppendList@?$CMoArray@VSBank@@VDefaultCache@@@@UAEHABV?$GenList@VSBank@@@@@Z
// FUNCTION: LITHTECH 0x004b30d0 ?GenSetCacheSize@?$CMoArray@VSBank@@VDefaultCache@@@@UAEXK@Z
// FUNCTION: LITHTECH 0x004b30e0 ?Insert2@?$CMoArray@VSBank@@VDefaultCache@@@@QAEHKABVSBank@@PAVLAlloc@@@Z

// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from the GPL release of the LithTech Jupiter Ex engine; see README.md (License).
