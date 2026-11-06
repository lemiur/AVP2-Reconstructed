// lithshared stdlith/helpers.cpp (Talon). The Talon .cpp is not on disk; this is Jupiter
// libs/stdlith/helpers.cpp with the Talon header's types (BOOL/DWORD) and without the functions the
// Talon helpers.h doesn't declare (IsFileAbsolute, RemoveExtension, FormatFilename).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/stdlith

//------------------------------------------------------------------
//
//	FILE	  : Helpers.cpp
//
//	PURPOSE	  : Implements the CHelpers class.
//
//	CREATED	  : November 26 1996
//
//	COPYRIGHT : Microsoft 1996 All Rights Reserved
//
//------------------------------------------------------------------

// Includes....
#ifdef _WINDOWS
	#include <windows.h>
#endif

#include <ctype.h>
#include "helpers.h"
#include "stdlithdefs.h"


// FUNCTION: LITHTECH 0x004b27e0 _$E2
// FUNCTION: LITHTECH 0x004b27f0 _$E1
static char		g_UpperTable[256];
static CHelpers	g_Helpers;



// FUNCTION: LITHTECH 0x004b2800
CHelpers::CHelpers()
{
	for( WORD i=0; i < 256; i++ )		// 16-bit counter (Jupiter: uint32); the loop counts down in a separate register
		g_UpperTable[i] = (char)toupper( i );
}


// FUNCTION: LITHTECH 0x004b2830
BOOL CHelpers::UpperStrcmp( const char *pInputString1, const char *pInputString2 )
{
	int				curPos=0;
	unsigned char	*pStr1 = (unsigned char*)pInputString1, *pStr2 = (unsigned char*)pInputString2;

	while(1)
	{
		if( g_UpperTable[pStr1[curPos]] != g_UpperTable[pStr2[curPos]] )
			return FALSE;

		if( pStr1[curPos] == 0 )
			return TRUE;

		++curPos;
	}

	return FALSE;
}


#ifdef _WINDOWS
	BOOL CHelpers::ExtractFullPath( const char *pInPath, char *pOutPath, DWORD outPathLen )
	{
		char		*pFilePart;
		
		if( ::GetFullPathName(pInPath, outPathLen, pOutPath, &pFilePart) == 0 )
			strcpy( pOutPath, pInPath );

		return TRUE;
	}
#endif


// FUNCTION: LITHTECH 0x004b2870
BOOL CHelpers::ExtractPathAndFileName( const char *pInputPath, char *pPathName, char *pFileName )
{
	char delimiters[2];
	int i, len, lastDelimiter;

	
	delimiters[0] = '\\';
	delimiters[1] = '/';
		
	pPathName[0] = pFileName[0] = 0;

	len = strlen(pInputPath);
	lastDelimiter = -1;
	
	for( i=0; i < len; i++ )
	{
		if( pInputPath[i] == delimiters[0] || pInputPath[i] == delimiters[1] )
		{
			lastDelimiter = i;
		}
	}

	if( lastDelimiter == -1 )
	{
		pPathName[0] = 0;
		strcpy( pFileName, pInputPath );
	}
	else
	{
		memcpy( pPathName, pInputPath, lastDelimiter );
		pPathName[lastDelimiter] = 0;
	
		memcpy( pFileName, &pInputPath[lastDelimiter+1], len-lastDelimiter );
		pFileName[len-lastDelimiter] = 0;
	}

	return TRUE;
}


// FUNCTION: LITHTECH 0x004b2920
BOOL CHelpers::ExtractFileNameAndExtension( const char *pInputFilename, char *pFilename, char *pExtension )
{
	int		i, len, lastDot;
	char	delimiter = '.';

	len = strlen(pInputFilename);
	lastDot = len;

	for( i=0; i < len; i++ )
	{
		if( pInputFilename[i] == delimiter )
		{
			lastDot = i;
		}
	}
	
	memcpy( pFilename, pInputFilename, lastDot );
	pFilename[lastDot] = 0;

	memcpy( pExtension, &pInputFilename[lastDot+1], len-lastDot );
	pExtension[len-lastDot] = 0;

	return TRUE;
}


// FUNCTION: LITHTECH 0x004b29a0
BOOL CHelpers::ExtractNames( const char *pFullPath, char *pPathname, char *pFilename, char *pFileTitle, char *pExt )
{
	char		pathName[256], fileName[256], fileTitle[256], ext[256];


	ExtractPathAndFileName( pFullPath, pathName, fileName );
	ExtractFileNameAndExtension( fileName, fileTitle, ext );

	if( pPathname )
		strcpy( pPathname, pathName );

	if( pFilename )
		strcpy( pFilename, fileName );

	if( pFileTitle )
		strcpy( pFileTitle, fileTitle );

	if( pExt )
		strcpy( pExt, ext );
	
	return TRUE;
}


char* CHelpers::GetNextDirName( char *pIn, char *pOut )
{
	DWORD		len = strlen(pIn);
	DWORD		inPos=0, outPos=0;


	// Skip \ characters.
	while( (pIn[inPos] == '/') || (pIn[inPos] == '\\') )
		inPos++;

	if( pIn[inPos] == 0 )
		return NULL;

	while( (pIn[inPos] != '/') && (pIn[inPos] != '\\') && (pIn[inPos] != 0) )
		pOut[outPos++] = pIn[inPos++];

	pOut[outPos] = 0;
	return &pIn[inPos];
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from the GPL release of the LithTech Jupiter Ex engine; see README.md (License).
