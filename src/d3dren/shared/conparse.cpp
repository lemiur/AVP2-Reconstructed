// d3d.ren shared/conparse (0x10012ef4-0x10013215): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// engine src/shared/conparse.cpp twin; mnemonic similarity 0.4-0.9 (Processor Pack vs RTM back end).
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

// The per-poly record of a dynamic light touching it (StructBank DAT_10056220, 0x14 bytes) and the list of lit polys
// (StructBank DAT_10056240, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h).
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

// ------------------------------------------------------------------ //
// conparse: the engine's own copy of src/shared/conparse.cpp (names from there).
// ------------------------------------------------------------------ //

LTBOOL cp_Parse(char *pCommand, const char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs);

// FUNCTION: D3DREN 0x10012ef4
LTBOOL ConParse::Parse()
{
	if(!m_pCommandPos)
		return LTFALSE;

	if(!cp_Parse(m_pCommandPos, (const char **)&m_pCommandPos, m_ArgBuffer, m_Args, &m_nArgs))
	{
		m_pCommandPos = LTNULL;
	}

	return LTTRUE;
}

// FUNCTION: D3DREN 0x10012f2b
LTBOOL ConParse::ParseFind(char *pLookFor, LTBOOL bCaseSensitive, uint32 minTokens)
{
	LTBOOL equal;

	// Must have at least one token, otherwise it can't find anything.
	if(minTokens == 0)
		minTokens = 1;

	while(Parse())
	{
		if(m_nArgs >= (int)minTokens)
		{
			if(bCaseSensitive)
				equal = (strcmp(m_Args[0], pLookFor) == 0);
			else
				equal = (stricmp(m_Args[0], pLookFor) == 0);

			if(equal)
			{
				return LTTRUE;
			}
		}
	}

	return LTFALSE;
}

#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'

enum GNTResult
{
	GNT_NoToken=0,
	GNT_GotToken,
	GNT_GotSemicolon
};

static GNTResult cp_GetNextToken(const char* &pCurPos, char* &pTokenPos);
static void cp_AddChar(char* &pTokenPos, char *pToken, char theChar);
static LTBOOL cp_ParseParen(const char* &pCurPos, char* &pTokenPos, char *pToken);
static LTBOOL cp_ParseQuote(const char* &pCurPos, char* &pTokenPos, char *pToken);

// FUNCTION: D3DREN 0x10012f7d
// (the loop is `while(1)` with the exits inside: the engine twin's do/while(status != GNT_NoToken) forwards argBuffer into the first iteration and costs 6 bytes more)
LTBOOL cp_Parse(char *pCommand, const char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs)
{
	GNTResult status;
	char *pCurArgBufferPos, *pToken;
	const char *pCurPos;


	// Parse.
	pCurPos = pCommand;
	pCurArgBufferPos = argBuffer;
	*nArgs = 0;

	while(1)
	{
		pToken = pCurArgBufferPos;
		status = cp_GetNextToken(pCurPos, pCurArgBufferPos);

		if(status == GNT_NoToken)
		{
			// All done..
			return 0;
		}
		else if(status == GNT_GotToken)
		{
			argPointers[*nArgs] = pToken;

			++(*nArgs);
			if(*nArgs >= PARSE_MAXARGS)
				break;
		}
		else if(status == GNT_GotSemicolon)
		{
			// Got a semicolon.. finish up and tell them that there are more.
			*pNewCommandPos = pCurPos;
			return 1;
		}
	}

	return 0;
}

// Keep the shared character-clamp tail after the ordinary-character store, with the escape path jumping forward to it.
// A shared result for delimiters also preserves the original return block order.
// FUNCTION: D3DREN 0x10012fd9
static GNTResult cp_GetNextToken(const char* &pCurPos, char* &pTokenPos)
{
	char *pToken;
	char curChar;

	// Skip spaces.
	while(pCurPos[0] == ' ')
		pCurPos++;

	// Is there even a string?
	if(pCurPos[0] == 0)
		return GNT_NoToken;

	pToken = pTokenPos;
	while(1)
	{
		// Get the char.
		curChar = *pCurPos;

		// End of string?
		if(curChar == 0)
		{
			break;
		}
		else if(curChar == SPECIAL_CHAR)
		{
			// Just add the next character to the string.
			pCurPos++;
			curChar = *pCurPos;
			if(curChar == 0)
			{
				break;
			}
			else
			{
				*pTokenPos = curChar;
				goto ClampCharacter;
			}
		}
		else if(curChar == ';' || iscntrl(curChar))
		{
			// If this is the first character, then return the fact that it's a semicolon.
			*pTokenPos = 0;
			++pTokenPos;
			GNTResult result;
			if(pToken[0] == 0)
			{
				// Only increment it if it's a full semicolon delimiter, so that
				// next time around parsing, it'll skip past the semicolon.
				++pCurPos;
				result = GNT_GotSemicolon;
			}
			else
			{
				result = GNT_GotToken;
			}
			return result;
		}
		else if(curChar == '(')
		{
			if(pTokenPos == pToken)
			{
				++pCurPos;
				cp_ParseParen(pCurPos, pTokenPos, pToken);
			}
			break;
		}
		else if(curChar == QUOTE_CHAR)
		{
			if(pTokenPos == pToken)
			{
				++pCurPos;
				cp_ParseQuote(pCurPos, pTokenPos, pToken);
			}
			break;
		}
		else if(curChar == ' ')
		{
			break;
		}
		else
		{
			*pTokenPos = curChar;
ClampCharacter:
			if ((pTokenPos - pToken) >= PARSE_MAXARGLEN)
			{
				*pTokenPos = 0;
				--pTokenPos;
			}
			++pTokenPos;
			++pCurPos;
		}
	}

	*pTokenPos = 0;
	++pTokenPos;
	if(pToken[0] == 0)
		return GNT_NoToken;
	else
		return GNT_GotToken;
}

// (not a function of d3d.ren: the exe inlines it everywhere; the engine has it out of line at 0x00420470)
static void cp_AddChar(char* &pTokenPos, char *pToken, char theChar)
{
	*pTokenPos = theChar;
	if((pTokenPos - pToken) >= PARSE_MAXARGLEN)
	{
		*pTokenPos = 0; // Just truncate it.
		--pTokenPos; // Decrement it so it doesn't overflow but it eats up the rest of the token.
	}

	++pTokenPos;
}

// FUNCTION: D3DREN 0x100130b6
static LTBOOL cp_ParseParen(const char* &pCurPos, char* &pTokenPos, char *pToken)
{
	while(*pCurPos != 0 && *pCurPos != ')')
	{
		if(*pCurPos == SPECIAL_CHAR)
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
			if(*pCurPos != 0)
				cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
		else if(*pCurPos == '(')
		{
			// Nested parenthesis.
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
			cp_ParseParen(pCurPos, pTokenPos, pToken);
			cp_AddChar(pTokenPos, pToken, ')');
		}
		else
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
	}

	if(*pCurPos == ')')
	{
		++pCurPos;
		if(pTokenPos == pToken)
			cp_AddChar(pTokenPos, pToken, ' ');

		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: D3DREN 0x1001317f
static LTBOOL cp_ParseQuote(const char* &pCurPos, char* &pTokenPos, char *pToken)
{
	while(*pCurPos != 0 && *pCurPos != QUOTE_CHAR)
	{
		if(*pCurPos == SPECIAL_CHAR)
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
			if(*pCurPos != 0)
				cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
		else
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
	}

	if(*pCurPos == QUOTE_CHAR)
	{
		++pCurPos;
		if(pTokenPos == pToken)
			cp_AddChar(pTokenPos, pToken, ' ');

		return LTTRUE;
	}

	return LTFALSE;
}
