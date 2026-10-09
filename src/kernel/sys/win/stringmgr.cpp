// Jupiter runtime/kernel/src/sys/win/stringmgr.cpp
// The unit starts at 004973d0 (str_Init), before the units.csv range.
#include <windows.h>
#include <string.h>
#define _MBCS
#include <tchar.h>
#include "stringmgr.h"

void* dalloc(unsigned int size);
void dfree(void *ptr);
#include "bdefs.h"
#include "engine_vars.h"

typedef struct
{
	HINSTANCE	m_hInstance;	// 0x00
} WinBind;

struct StringWrapper
{
	GLink m_GLink;			// 0x00
	int m_RefCount;			// 0x0c
	uint16 m_StringLen;		// 0x10 Number of CHARACTERS in the string.
	uint16 m_DataLen;		// 0x12 How long m_Bytes is (including null terminating character..)
	uint8 m_Bytes[1];		// 0x14
};

// GLOBAL: LITHTECH 0x004e61dc
static int g_StringMgrInitCount=0;

// GLOBAL: LITHTECH 0x004e61d0
static GLink g_StringHead; // All the strings..

// Used for "" strings so we don't allocate lots of extra memory.
// GLOBAL: LITHTECH 0x004e61e0
static StringWrapper g_ZeroLengthStringWrapper =
{
	LTNULL, LTNULL, LTNULL,
	0, 0, 0, 0
};


inline StringWrapper* AllocateStringWrapper(int nStringBytes)
{
	return (StringWrapper*)dalloc(sizeof(StringWrapper) + (nStringBytes-1));
}

inline void FreeStringWrapper(StringWrapper *pString)
{
	dfree(pString);
}

// FUNCTION: LITHTECH 0x004973d0
void str_Init()
{
	if(g_StringMgrInitCount == 0)
	{
		gn_TieOff(&g_StringHead);
	}

	++g_StringMgrInitCount;
}

// FUNCTION: LITHTECH 0x004973f0
void str_Term()
{
	GLink *pCur, *pNext;

	--g_StringMgrInitCount;
	if(g_StringMgrInitCount == 0)
	{
		// Free all the allocated strings.
		pCur = g_StringHead.m_pNext;
		while(pCur != &g_StringHead)
		{
			pNext = pCur->m_pNext;
			FreeStringWrapper((StringWrapper*)pCur->m_pData);
			pCur = pNext;
		}

		gn_TieOff(&g_StringHead);
	}
}

// FUNCTION: LITHTECH 0x00497440
void str_ShowAllStringsAllocated(StringShowFn fn, void *pUser)
{
	GLink *pCur;
	StringWrapper *pString;

	pCur = g_StringHead.m_pNext;
	while(pCur != &g_StringHead)
	{
		pString = (StringWrapper*)pCur->m_pData;
		fn((char*)pString->m_Bytes, pUser);

		pCur = pCur->m_pNext;
	}
}

// FUNCTION: LITHTECH 0x00497480
uint8* str_FormatString(CBindModuleType *hModule, int stringCode, va_list *marker, int *bufferLen)
{
	WinBind *pBind = (WinBind*)hModule;
	*bufferLen = 0;

	// Load the string..
	char tempBuffer[5000];
	uint32 nBytes = LoadString(pBind->m_hInstance, stringCode, (char*)tempBuffer, sizeof(tempBuffer));
	if(nBytes == 0)
	{
		if(g_bDebugStrings)
			dsi_ConsolePrint("Couldn't get string %d", stringCode);

		return LTNULL;
	}

	// Format it.
	uint8 *pBuffer;

	nBytes = FormatMessage(FORMAT_MESSAGE_FROM_STRING|FORMAT_MESSAGE_ALLOCATE_BUFFER,
						tempBuffer,
						0,
						0,
						(char*)&pBuffer,
						1,
						marker
						);

	if(nBytes == 0)
	{
		if(g_bDebugStrings)
			dsi_ConsolePrint("FormatMessage error on string %d", stringCode);

		return LTNULL;
	}
	else
	{
		// Talon allocates 3 bytes per character (+1).
		nBytes = nBytes * 3 + 1;
		*bufferLen = (int)nBytes;
		return pBuffer;
	}
}

// FUNCTION: LITHTECH 0x00497540
void str_FreeStringBuffer(uint8 *pBuffer)
{
	if(pBuffer)
		LocalFree(pBuffer);
}

inline void str_CalcSize(uint8 *pString, int *pNumBytes, int *pNumChars)
{
	char *pCur;

	*pNumChars = 0;
	pCur = (char*)pString;
	while(*pCur != 0)
	{
		++(*pNumChars);
		pCur = _tcsinc(pCur);
	}

	// Number of bytes we've traversed + 1 for null terminating character.
	*pNumBytes = (pCur - (char*)pString) + 1;
}

// FUNCTION: LITHTECH 0x00497550
HSTRING str_CreateString(uint8 *pBuffer)
{
	StringWrapper *pString;
	int nBytes, nChars;

	if( !pBuffer )
		return ( HSTRING )LTNULL;

	if( pBuffer[0] == 0)
	{
		return (HSTRING)&g_ZeroLengthStringWrapper;
	}

	str_CalcSize(pBuffer, &nBytes, &nChars);
	pString = AllocateStringWrapper(nBytes);

	pString->m_RefCount = 1;
	pString->m_StringLen = (uint16)nChars;
	pString->m_DataLen = (uint16)nBytes;
	memcpy(pString->m_Bytes, pBuffer, nBytes);

	pString->m_GLink.m_pData = pString;
	gn_Insert(&g_StringHead, &pString->m_GLink);
	return (HSTRING)pString;
}

// FUNCTION: LITHTECH 0x004975e0
HSTRING str_CreateStringAnsi(const char *pStringData)
{
	StringWrapper *pString;
	int nBytes, nChars;

	if( !pStringData )
		return ( HSTRING )LTNULL;

	if(pStringData[0] == 0)
	{
		return (HSTRING)&g_ZeroLengthStringWrapper;
	}

	nChars = strlen(pStringData);
	nBytes = nChars+1;

	pString = AllocateStringWrapper(nBytes);

	pString->m_RefCount = 1;
	pString->m_StringLen = (uint16)nChars;
	pString->m_DataLen = (uint16)nBytes;
	strcpy((char*)pString->m_Bytes, pStringData);

	pString->m_GLink.m_pData = pString;
	gn_Insert(&g_StringHead, &pString->m_GLink);
	return (HSTRING)pString;
}

// FUNCTION: LITHTECH 0x00497660
HSTRING str_CopyString(HSTRING hString)
{
	StringWrapper *pString = (StringWrapper*)hString;

	if( !pString )
		return ( HSTRING )LTNULL;

	++pString->m_RefCount;
	return hString;
}

// FUNCTION: LITHTECH 0x00497670
void str_FreeString(HSTRING hString)
{
	StringWrapper *pString = (StringWrapper*)hString;

	// Don't free this one!
	if(pString == &g_ZeroLengthStringWrapper)
		return;

	if( !pString )
		return;

	--pString->m_RefCount;
	if(pString->m_RefCount == 0)
	{
		gn_Remove(&pString->m_GLink);
		FreeStringWrapper(pString);
	}
}

// FUNCTION: LITHTECH 0x004976b0
LTBOOL str_CompareStrings(HSTRING hString1, HSTRING hString2)
{
	StringWrapper *pString1 = (StringWrapper*)hString1;
	StringWrapper *pString2 = (StringWrapper*)hString2;

	if( !pString1 || !pString2 )
		return LTFALSE;

	return _tcscmp((TCHAR*)pString1->m_Bytes, (TCHAR*)pString2->m_Bytes) == 0;
}

// FUNCTION: LITHTECH 0x004976e0
LTBOOL str_CompareStringsUpper(HSTRING hString1, HSTRING hString2)
{
	StringWrapper *pString1 = (StringWrapper*)hString1;
	StringWrapper *pString2 = (StringWrapper*)hString2;

	if( !pString1 || !pString2 )
		return LTFALSE;

	return _tcsicmp((TCHAR*)pString1->m_Bytes, (TCHAR*)pString2->m_Bytes) == 0;
}

// FUNCTION: LITHTECH 0x00497710
char* str_GetStringData(HSTRING hString)
{
	StringWrapper *pString = (StringWrapper*)hString;

	if( !pString )
		return LTNULL;

	return (char*)pString->m_Bytes;
}

// FUNCTION: LITHTECH 0x00497720
int str_GetNumStringCharacters(HSTRING hString)
{
	StringWrapper *pString = (StringWrapper*)hString;

	if( !pString )
		return 0;

	return pString->m_StringLen;
}

// FUNCTION: LITHTECH 0x00497740
uint8* str_GetStringBytes(HSTRING hString, int *pNumBytes)
{
	StringWrapper *pString = (StringWrapper*)hString;

	if( !pString )
	{
		if( pNumBytes )
			*pNumBytes = 0;
		return LTNULL;
	}

	if(pNumBytes)
		*pNumBytes = pString->m_DataLen;

	return (uint8*)pString->m_Bytes;
}
