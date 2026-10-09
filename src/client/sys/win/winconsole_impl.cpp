// Jupiter runtime/client/src/sys/win/winconsole_impl.cpp
// Talon differences: no CUI font or DrawPrim drawing in this file (no Draw/DrawTextLine),
// no client-shell OnConsolePrint hook, CConHistory has no Remove()/MoveTo()/GetIndex(),
// SetRect keeps its "unchanged" early-out, and the console font is built from a resource
// bitmap in InitFont.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bdefs.h"
#include "console.h"
#include "consolecommands.h"
#include "renderstruct.h"
#include "clientmgr.h"
#include "iltclient.h"
#include "interface_helpers.h"
#include "ltdynarray.h"
#include "pixelformat.h"
#include "streamsim.h"

// Constants
#define SEPERATOR_CHARACTERS " .()\""

// The console reaches the client manager through g_ClientGlob.m_pClientMgr (0x004de30c,
// set by RunClientApp from cm_Init()).
#include "dsys_interface.h"
#include "engine_vars.h"



// 0x00435240
void* dsi_GetMainWindow();

// LoadedBitmap over the real CMoArray (load_pcx.h mirrors an older one), as in winclientde_impl.cpp;
// LoadBackground inlines its destructor.
class LoadedBitmap
{
public:
					LoadedBitmap();		// 0x00446170

	PFormat			m_Format;			// 0x000
	RPaletteColor	m_Palette[256];		// 0x038
	unsigned long	m_Width;			// 0x438
	unsigned long	m_Height;			// 0x43C
	unsigned long	m_Pitch;			// 0x440
	CMoArray<uint8>	m_Data;				// 0x444
};

LTBOOL pcx_Create2(ILTStream *pStream, LoadedBitmap *pBitmap);	// 0x004461d0
HSURFACE cis_CreateSurfaceFromPcx(LoadedBitmap *pLoadedBitmap);	// 0x0040c650


// Convenience functions...
// (Ignores vkKey and always tests VK_CONTROL, as in Jupiter.)
inline LTBOOL IsKeyDown( uint16 vkKey )
{
	return ((GetAsyncKeyState( VK_CONTROL ) & 0x8000) != 0);
}

// ------------------------------------------------------------------ //
// CConIterator functionality
// ------------------------------------------------------------------ //
// FUNCTION: LITHTECH 0x00420730
LTBOOL CConIterator::First( const char *pValue )
{
	// Go to the beginning
	if ( !Begin() )
		return LTFALSE;

	// Jump out on an empty string
	if ( (!pValue) || (!*pValue) )
		return LTTRUE;

	// Check for the first one being the one we're looking for
	const char *pCur = Get();
	if ( !pCur )
		return LTFALSE;

	if ( strnicmp( pValue, pCur, strlen( pValue ) ) == 0 )
		return LTTRUE;

	// Find the first value
	return Next( pValue );
}

// FUNCTION: LITHTECH 0x00420790
LTBOOL CConIterator::Last( const char *pValue )
{
	// Go to the ending
	if ( !End() )
		return LTFALSE;

	// Jump out on an empty string
	if ( (!pValue) || (!*pValue) )
		return LTTRUE;

	// Check for the last one being the one we're looking for
	const char *pCur = Get();
	if ( !pCur )
		return LTFALSE;

	if ( strnicmp( pValue, pCur, strlen( pValue ) ) == 0 )
		return LTTRUE;

	// Find the first value
	return Prev( pValue );
}

// FUNCTION: LITHTECH 0x004207f0
LTBOOL CConIterator::Next( const char *pValue )
{
	if ( !pValue )
		return NextItem();

	int iLength = strlen( pValue );

	LTBOOL bFound = LTFALSE;

	while ( !bFound )
	{
		if ( !NextItem() )
			break;

		const char *pCur = Get();
		if ( !pCur )
			break;

		bFound = strnicmp( pValue, pCur, iLength ) == 0;
	}

	return bFound;
}

// FUNCTION: LITHTECH 0x00420850
LTBOOL CConIterator::Prev( const char *pValue )
{
	if ( !pValue )
		return PrevItem();

	int iLength = strlen( pValue );

	LTBOOL bFound = LTFALSE;

	while ( !bFound )
	{
		if ( !PrevItem() )
			break;

		const char *pCur = Get();
		if ( !pCur )
			break;

		bFound = strnicmp( pValue, pCur, iLength ) == 0;
	}

	return bFound;
}

// ------------------------------------------------------------------ //
// CConCommandBox functionality
// ------------------------------------------------------------------ //
// FUNCTION: LITHTECH 0x004208b0
CConCommandBox::CConCommandBox()
{
	m_TextColor = CONRGB( 255, 180, 180 );

	m_BackColor = CONRGB( 30, 30, 30 );
	m_BorderColor = CONRGB( 255, 255, 255 );

	m_pConsole = LTNULL;

	m_FlashTime = 500;

	Clear();
}

// The linker folded this with an identical function at 0x004a7f40.
void CConCommandBox::Term()
{
	SetConsole( LTNULL );
}

// FUNCTION: LITHTECH 0x00420900
void CConCommandBox::Clear()
{
	m_CurCommand[0] = 0;
	m_iCurLength = 0;
	MoveCursor(0);
}

// FUNCTION: LITHTECH 0x00420910
void CConCommandBox::SetCommand( const char *pCommand )
{
	if ( !pCommand )
		Clear();
	else
	{
		// Copy the string
		strncpy( m_CurCommand, pCommand, MAX_CONSOLE_TEXTLEN );
		m_iCurLength = strlen( m_CurCommand );
		// Move to the end of the string
		MoveCursor( m_iCurLength );
	}
}

// FUNCTION: LITHTECH 0x00420960
void CConCommandBox::DeleteChar( int iOffset )
{
	// Find the position
	int iPosition = m_iCursorPos + iOffset;

	// Restrict to the contents of the string
	if ( (iPosition < 0) || (iPosition >= m_iCurLength) )
		return;

	// Delete the character
	strcpy( &m_CurCommand[iPosition], &m_CurCommand[iPosition + 1] );
	m_iCurLength--;
}

// FUNCTION: LITHTECH 0x004209a0
void CConCommandBox::DeleteWord( int iOffset )
{
	LTBOOL bSeperator = (iOffset < 0);
	for ( int iLoop = 0; iLoop < 2; iLoop++)
	{
		// Skip over the next set of seperators or characters
		while ( ((m_iCursorPos + iOffset) >= 0) && ((m_iCursorPos + iOffset) < m_iCurLength) &&
			((strchr( SEPERATOR_CHARACTERS, m_CurCommand[m_iCursorPos + iOffset] ) == 0) ^ bSeperator) )
		{
			DeleteChar( iOffset );
			m_iCursorPos += iOffset;
		}
		bSeperator = !bSeperator;
	}
}

// FUNCTION: LITHTECH 0x00420a20
void CConCommandBox::AddChar( char key )
{
	// Don't add past the end of the string
	if ( m_iCursorPos >= (MAX_CONSOLE_TEXTLEN - 1) )
		return;

	// Don't allow the string to get too big
	if ( m_iCurLength >= (MAX_CONSOLE_TEXTLEN - 1) )
	{
		m_iCurLength--;
		m_CurCommand[m_iCurLength] = 0;
	}

	// Make room if necessary
	if ( m_iCursorPos < m_iCurLength )
		memmove( &m_CurCommand[m_iCursorPos + 1], &m_CurCommand[m_iCursorPos], (m_iCurLength - m_iCursorPos) + 1 );
	// Otherwise make sure it's terminated
	else
		m_CurCommand[m_iCurLength + 1] = 0;

	// Put the character in the string
	m_CurCommand[m_iCursorPos] = key;

	// Update the length
	m_iCurLength++;
}

// FUNCTION: LITHTECH 0x00420a90
void CConCommandBox::MoveCursor(int iOffset)
{
	m_iCursorPos = max(min(m_iCursorPos + iOffset, m_iCurLength), 0);
}

// FUNCTION: LITHTECH 0x00420ad0
void CConCommandBox::MoveWord( int iOffset )
{
	LTBOOL bSeperator = (iOffset < 0);
	for ( int iLoop = 0; iLoop < 2; iLoop++)
	{
		// Skip over the next set of seperators or characters
		while ( ((m_iCursorPos + iOffset) >= 0) && ((m_iCursorPos + iOffset) <= m_iCurLength) &&
			((strchr( SEPERATOR_CHARACTERS, m_CurCommand[m_iCursorPos + iOffset] ) == 0) ^ bSeperator) )
			m_iCursorPos += iOffset;
		bSeperator = !bSeperator;
	}
	// Make sure we end up on a non-seperator
	while ( ((m_iCursorPos + iOffset) >= 0) && ((m_iCursorPos + iOffset) <= m_iCurLength) &&
		(strchr( SEPERATOR_CHARACTERS, m_CurCommand[m_iCursorPos] ) != 0) )
		m_iCursorPos += iOffset;
}

// FUNCTION: LITHTECH 0x00420b90
char CConCommandBox::TranslateKey(uint32 key) const
{
	char aResult[2];
	uint8 aKeyState[256];
	if ( !GetKeyboardState( aKeyState ) )
		return 0;

	switch ( ToAscii( key, 0, aKeyState, (LPWORD)aResult, 0 ) )
	{
		// No translation available
		case 0 :
			return 0;
		// 1 character translated
		case 1 :
			return aResult[0];
		// 2 characters required for translation
		case 2 :
			return aResult[1];
		// Documentation says this should never happen...
		default :
			return 0;
	}
}

// FUNCTION: LITHTECH 0x00420c00
void CConCommandBox::OnKeyPress(uint32 key)
{
	switch ( key )
	{
		case VK_ESCAPE :
			Clear();
			break;
		case VK_LEFT :
			if ( IsKeyDown( VK_CONTROL ) )
				// Ctrl+Left = word left
				MoveWord( -1 );
			else
				MoveCursor( -1 );
			break;
		case VK_RIGHT :
			if ( IsKeyDown( VK_CONTROL ) )
				// Ctrl+Right = word right
				MoveWord( 1 );
			else
				MoveCursor( 1 );
			break;
		case VK_HOME :
			MoveCursor( -m_iCursorPos );
			break;
		case VK_END :
			if ( IsKeyDown( VK_CONTROL ) )
				// Ctrl+End = delete the rest of the buffer
				AddChar( 0 );
			else
				MoveCursor( m_iCurLength );
			break;
		case VK_BACK :
			if ( IsKeyDown( VK_CONTROL ) )
				// Ctrl+Backspace = delete the next word
				DeleteWord( -1 );
			else
			{
				DeleteChar( -1 );
				MoveCursor( -1 );
			}
			break;
		case VK_DELETE :
			if ( IsKeyDown( VK_CONTROL ) )
				// Ctrl+Delete = delete the next word
				DeleteWord( 0 );
			else
				DeleteChar( 0 );
			break;
		default :
			char chChar = TranslateKey( key );
			if (chChar)
			{
				AddChar( chChar );
				MoveCursor( 1 );
			}
			break;
	}
}

// ------------------------------------------------------------------ //
// CConHistory functionality
// ------------------------------------------------------------------ //
// FUNCTION: LITHTECH 0x00420d70
CConHistory::CConHistory( int iListSize ) :
	m_pLines(LTNULL),
	m_iIndex(0),
	m_iListSize(0),
	m_iListStart(0),
	m_iListEnd(0)
{
	Resize( iListSize );
}

// FUNCTION: LITHTECH 0x00420da0 ??_GCConHistory@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00420dc0
CConHistory::~CConHistory()
{
	Resize( 0 );
}

// FUNCTION: LITHTECH 0x00420de0
LTBOOL CConHistory::Begin()
{
	m_iIndex = m_iListStart;
	return m_iIndex != m_iListEnd;
}

// FUNCTION: LITHTECH 0x00420e00
LTBOOL CConHistory::End()
{
	m_iIndex = m_iListEnd;
	return PrevItem();
}

// FUNCTION: LITHTECH 0x00420e10
LTBOOL CConHistory::NextItem()
{
	if ( m_iIndex == m_iListEnd )
		return LTFALSE;
	m_iIndex++;
	if ( m_iIndex >= m_iListSize )
		m_iIndex = 0;
	return m_iIndex != m_iListEnd;
}

// FUNCTION: LITHTECH 0x00420e40
LTBOOL CConHistory::PrevItem()
{
	if ( m_iIndex == m_iListStart )
		return LTFALSE;
	m_iIndex--;
	if ( m_iIndex < 0 )
		m_iIndex = m_iListSize - 1;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00420e70
void CConHistory::Add( const char *pString )
{
	// Make sure the list exists
	if ( !m_iListSize )
		return;
	// Copy the string
	strncpy( m_pLines[m_iListEnd].m_Text, pString, MAX_CONSOLE_TEXTLEN );
	// Move the end of the list
	m_iListEnd++;
	if ( m_iListEnd >= m_iListSize )
		m_iListEnd = 0;
	// Move the beginning of the list if necessary
	if ( m_iListStart == m_iListEnd )
	{
		m_iListStart++;
		if ( m_iListStart >= m_iListSize )
			m_iListStart = 0;
	}
}

// Resize the history list
// FUNCTION: LITHTECH 0x00420ee0
LTBOOL CConHistory::Resize( int iSize )
{
	// Jump out if it's already the right size
	if ( m_iListSize == (iSize + 1) )
		return LTTRUE;

	CConTextLine *pNewList = LTNULL;

	// Allocate a new buffer
	if ( iSize )
	{
		iSize++;
		pNewList = new CConTextLine[iSize];
		if ( !pNewList )
			return LTFALSE;

		// Move over the old lines
		if ( End() )
		{
			int iIndex = iSize - 1;
			do
			{
				const char *pLine = Get();
				if ( !pLine )
					break;
				strncpy( pNewList[iIndex].m_Text, pLine, MAX_CONSOLE_TEXTLEN );
			} while ( (--iIndex) && (PrevItem()) );
			m_iListStart = iIndex + 1;
			m_iListEnd = 0;
		}
		else
			m_iListStart = m_iListEnd = 0;
	}
	else
		m_iListStart = m_iListEnd = 0;

	// Get rid of the old lines
	if ( m_iListSize )
		delete [] m_pLines;

	// Update the list buffer variables
	m_iListSize = iSize;
	m_pLines = pNewList;

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00420fc0
const char *CConHistory::Get() const
{
	if ( (m_iIndex < 0) || (m_iIndex >= m_iListSize) || (m_iIndex == m_iListEnd) )
		return LTNULL;

	return &(m_pLines[m_iIndex].m_Text[0]);
}

// ------------------------------------------------------------------ //
// CConsole functionality
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00420ff0
CConsole::CConsole()
{
	m_pFontBitmapData = LTNULL;

	m_hWnd = 0;
	m_CommandHandler = LTNULL;
	m_FilterLevel = 1;

	m_BackColor = CONRGB( 0, 0, 0 );
	m_BorderColor = CONRGB( 255, 255, 255 );

	m_FontHeight = 1;

	m_pStruct = LTNULL;
	m_ErrorLogFn = LTNULL;
	m_hBackground = LTNULL;
	m_fBackgroundAlpha = 1.0f;
	m_bBackgroundOptimized = LTFALSE;

	m_pCompletionIterator = &g_ConEmptyIterator;

	m_bInitialized = LTTRUE;
	m_bInitTerminate = LTFALSE;
}

// FUNCTION: LITHTECH 0x004210e0
CConsole::~CConsole()
{
	Term( LTTRUE );

	m_bInitialized = LTFALSE;
}

// FUNCTION: LITHTECH 0x00421130
LTBOOL CConsole::Init(const LTRect *pRect, CommandHandler handler, RenderStruct *pStruct, CConIterator *pCompletionIterator )
{
	HWND hWnd = (HWND)dsi_GetMainWindow();

	if ( m_bInitTerminate )
		Term( LTFALSE );
	m_bInitTerminate = LTTRUE;

	if (!InitFont()) {
		Term(LTTRUE);
		return LTFALSE; }

	m_hWnd = hWnd;
	// Note : This has no effect now that the window position is in a console variable
	m_ScrRect = *pRect;
	m_CommandHandler = handler;
	m_pStruct = pStruct;

	m_iScrollOffset = 0;

	if ( !m_TextLines.GetSize() )
		ResizeTextLines( 200 );

	// Setup the command box.
	GetCommandBox()->SetConsole( this );

	if ( !m_cHistory.GetSize() )
		m_cHistory.Resize( 20 );

	if ( pCompletionIterator )
		SetCompletionIterator( pCompletionIterator );

	// Read in the variable states
	CheckVariables();

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00421210
LTBOOL CConsole::InitBare()
{
	if ( !m_TextLines.GetSize() )
		ResizeTextLines( 200 );
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00421230
void CConsole::Term(LTBOOL bDeleteTextLines)
{
	FreeBackground();

	if ( bDeleteTextLines )
	{
		GDeleteAndRemoveElements( m_TextLines );
		m_cHistory.Resize( 0 );
	}

	TermFont();
	GetCommandBox()->Term();

	m_hWnd = 0;
	m_CommandHandler = LTNULL;
	m_pStruct = LTNULL;

	m_bInitTerminate = LTFALSE;
}

// FUNCTION: LITHTECH 0x004212c0
void CConsole::FreeBackground()
{
	if ( !m_hBackground )
		return;

	cis_DeleteSurface( m_hBackground );
	m_hBackground = LTNULL;
}


// Builds the console font from the console font bitmap resource (a 16x16 character sheet of 8-bit palettized pixels).
// Remaining diff (107 aligned, was 163 and SIZE): only register assignment. Ours is a cyclic permutation of the
// original's callee-saved registers (ours ebp/edi/esi/ebx = original ebx/ebp/edi/esi: this, the stream, the DC,
// charHeight...). Wave 6 phase 2 fixed the BPP_8P test (the original rejects anything but palettized), the
// m_FullFontHeight/m_FontHeight store order, bResult cleared just before pcx_Create2 and `if(*pSrc) ...; pSrc++`.
// Tried without effect: pSrc[x], array-subscript forms of pDest/pSrc/pRowStart, `*pDest = bits; pDest++`,
// `(hDC = GetDC()) != LTNULL`, a separate index for the width fill, `iRow << 4`, a statement hill-climb.
// Wave 7: the console variable is "ConsoleFont" and the resource type "#12345" (we had "ConsoleFontRes" and "PCX").
// Wave 7 phase 2: audit: `calls 2` are GetDC/ReleaseDC through IAT slots the namemap doesn't name yet (0x4c6280,
// 0x4c627c): same calls, no behaviour difference. 107 aligned, a cyclic permutation of the callee-saved registers.
// PARKED: register allocation only (callee-saved registers permuted, 107 aligned); behaviour identical
// STUB: LITHTECH 0x004212f0
LTBOOL CConsole::InitFont()
{
	LoadedBitmap bitmap;
	LTBOOL bResult;
	LTCommandVar *pVar;
	uint32 resID;
	HRSRC hResource;
	HGLOBAL hGlobal;
	void *pResData;
	ILTStream *pStream;
	HDC hDC;
	uint32 charHeight, charWidth, iRow, iCol, x, y, mask, bits, iChar;
	uint8 *pRowStart, *pSrc;
	uint32 *pDest;

	if(!m_CS.IsValid())
		return LTFALSE;

	resID = 140;
	pVar = cc_FindConsoleVar(&g_ClientConsoleState, "ConsoleFont");
	if(pVar)
		resID = atoi(pVar->pStringVal);

	hResource = FindResource(LTNULL, MAKEINTRESOURCE(resID & 0xFFFF), "#12345");
	if(!hResource)
	{
		hResource = FindResource(LTNULL, MAKEINTRESOURCE(140), "#12345");
		if(!hResource)
			return LTFALSE;
	}

	hGlobal = LoadResource(LTNULL, hResource);
	if(!hGlobal)
		return LTFALSE;

	pResData = LockResource(hGlobal);
	if(!pResData)
		return LTFALSE;

	pStream = streamsim_OpenMemStream(256);
	if(!pStream)
		return LTFALSE;

	pStream->Write(pResData, SizeofResource(LTNULL, hResource));
	pStream->SeekTo(0);

	bResult = LTFALSE;
	if(pcx_Create2(pStream, &bitmap) && bitmap.m_Format.m_eType == BPP_8P)
	{
		hDC = GetDC(m_hWnd);
		if(hDC)
		{
			charHeight = bitmap.m_Height >> 4;
			charWidth = bitmap.m_Width >> 4;

			m_pFontBitmapData = new uint32[charHeight * 256];
			if(m_pFontBitmapData)
			{
				m_FullFontHeight = (uint16)charHeight;
				m_FontHeight = (uint16)charHeight;
				for(iChar=0; iChar < NUM_CONSOLE_CHARACTERS; iChar++)
					m_CharWidths[iChar] = (uint16)charWidth;

				for(iRow=0; iRow < 16; iRow++)
				{
					pRowStart = bitmap.m_Data.GetArray() + bitmap.m_Pitch * iRow * charHeight;
					iChar = iRow * 16;
					for(iCol=0; iCol < 16; iCol++)
					{
						pDest = m_pFontBitmapData + m_FullFontHeight * iChar;
						for(y=0; y < charHeight; y++)
						{
							pSrc = pRowStart + y * bitmap.m_Pitch;
							bits = 0;
							mask = 1;
							for(x=0; x < charWidth; x++)
							{
								if(*pSrc)
									bits |= mask;

								mask += mask;
								pSrc++;
							}

							*pDest++ = bits;
						}

						pRowStart += charWidth;
						iChar++;
					}
				}

				bResult = LTTRUE;
			}

			ReleaseDC(m_hWnd, hDC);
		}
	}

	pStream->Release();
	return bResult;
}

// FUNCTION: LITHTECH 0x00421630
void CConsole::TermFont()
{
	if (m_pFontBitmapData) {
		delete m_pFontBitmapData;
		m_pFontBitmapData = LTNULL; }
}

// FUNCTION: LITHTECH 0x00421660
LTBOOL CConsole::ResizeTextLines( uint32 nNewSize )
{
	// Jump out if the number of lines hasn't changed
	if ( m_TextLines.GetSize() == nNewSize )
		return LTTRUE;

	CConTextLine	*pLine;

	while ( m_TextLines.GetSize() < nNewSize )
	{
		pLine = new CConTextLine;

		memset( pLine, 0, sizeof(CConTextLine) );
		m_TextLines.AddHead( pLine );
	}

	while ( m_TextLines.GetSize() > nNewSize )
		delete m_TextLines.RemoveHead();

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00421750
void CConsole::FinishCommand()
{
	EndNav();

	char *pCurCommand = (char *)(GetCommandBox()->GetCommand());
	if (pCurCommand[0] == 0)
		return;

	PrintString( GetCommandBox()->GetTextColor(), 0, pCurCommand );

	m_cHistory.Add( pCurCommand );

	if ( m_CommandHandler )
		m_CommandHandler( pCurCommand );

	GetCommandBox()->Clear();
}

// FUNCTION: LITHTECH 0x004217a0
void CConsole::CheckVariables()
{
	// Read from the console's variables
	SetBackgroundAlpha( g_CV_ConsoleAlpha );
	// Note : This makes sure the console buffer always has at least 10 lines in it..
	ResizeTextLines( (uint32)LTMAX( g_CV_ConsoleBufferLen , 10 ) );
	m_cHistory.Resize( (uint32)LTMAX( g_CV_ConsoleHistoryLen, 5 ) );
	// Update the window rectangle
	LTRect cRect;
	cRect.top = g_CV_ConsoleTop;
	cRect.left = g_CV_ConsoleLeft;
	cRect.bottom = g_CV_ConsoleBottom;
	cRect.right = g_CV_ConsoleRight;
	SetRect( cRect );
}

// FUNCTION: LITHTECH 0x00421820
void CConsole::CycleCommands(int iCount)
{
	if (!iCount)
		return;

	LTBOOL bForward = iCount > 0;
	iCount = abs( iCount );

	// Pull up the first entry in the history list
	if ( GetState() != STATE_HISTORY )
	{
		// Go to the first match in the history list
		LTBOOL bMatch;
		if ( bForward )
			bMatch = m_cHistory.First( GetCommandBox()->GetCommand() );
		else
			bMatch = m_cHistory.Last( GetCommandBox()->GetCommand() );

		if ( !bMatch )
			return;

		StartNav( STATE_HISTORY );
		iCount--;
	}

	LTBOOL bContinue = LTTRUE;
	// Pull up the next entry in the history list
	while ( iCount && bContinue )
	{
		if ( bForward )
			bContinue = m_cHistory.Next( m_aNavCommand );
		else
			bContinue = m_cHistory.Prev( m_aNavCommand );
		iCount--;
	}

	if ( !bContinue )
	{
		// End navigation mode
		GetCommandBox()->SetCommand( m_aNavCommand );
		EndNav();
	}
	else
		GetCommandBox()->SetCommand( m_cHistory.Get() );
}

// FUNCTION: LITHTECH 0x004218e0
void CConsole::SetCompletionIterator(CConIterator *pIterator)
{
	if ( !pIterator )
		m_pCompletionIterator = &g_ConEmptyIterator;
	else
		m_pCompletionIterator = pIterator;
}

// FUNCTION: LITHTECH 0x00421900
void CConsole::NextCommand()
{
	// Start the iterator if we're not navigating yet
	if ( GetState() != STATE_COMPLETE )
	{
		// Find the first match
		if ( !GetCompletionIterator()->First( GetCommandBox()->GetCommand() ) )
			return;

		// Go into navigation mode
		StartNav( STATE_COMPLETE );
	}
	// Otherwise find the next one
	else if ( !GetCompletionIterator()->Next( m_aNavCommand ) )
	{
		// Start over if we get to the end
		GetCommandBox()->SetCommand( m_aNavCommand );
		EndNav();
		return;
	}

	// Show the new item
	GetCommandBox()->SetCommand( GetCompletionIterator()->Get() );
}

// FUNCTION: LITHTECH 0x00421960
void CConsole::PrevCommand()
{
	// Start the iterator if we're not navigating yet
	if ( GetState() != STATE_COMPLETE )
	{
		// Find the first match
		if ( !GetCompletionIterator()->Last( GetCommandBox()->GetCommand() ) )
			return;

		// Go into navigation mode
		StartNav( STATE_COMPLETE );
	}
	// Otherwise find the previous one
	else if ( !GetCompletionIterator()->Prev( m_aNavCommand ) )
	{
		// Start over if we get to the beginning
		GetCommandBox()->SetCommand( m_aNavCommand );
		EndNav();
		return;
	}

	// Show the new item
	GetCommandBox()->SetCommand( GetCompletionIterator()->Get() );
}

// FUNCTION: LITHTECH 0x004219c0
void CConsole::MatchCommands()
{
	// Display a list of what matches
	LTBOOL bContinue = GetCompletionIterator()->First( GetCommandBox()->GetCommand() );
	if ( bContinue )
		Printf( GetCommandBox()->GetTextColor(), 0, ">%s*>", GetCommandBox()->GetCommand() );
	while ( bContinue )
	{
		char *pString = (char *)GetCompletionIterator()->Get();
		if ( !pString )
			break;
		PrintString( GetCommandBox()->GetTextColor(), 0, pString );
		bContinue = GetCompletionIterator()->Next( GetCommandBox()->GetCommand() );
	}
}

// FUNCTION: LITHTECH 0x00421a30
void CConsole::StartNav( EConState eState )
{
	// Jump out if we're already navigating
	if ( GetState() == eState )
		return;

	// Make a copy of whatever's in the command buffer
	strncpy( m_aNavCommand, GetCommandBox()->GetCommand(), MAX_CONSOLE_TEXTLEN );

	// Change our state
	SetState( eState );
}

// FUNCTION: LITHTECH 0x00421a60
void CConsole::EndNav()
{
	// Jump out if we're not navigating
	if ( GetState() == STATE_NORMAL )
		return;

	// Change our state
	SetState( STATE_NORMAL );
}

// The console's client interface (Jupiter's ilt_client). An inline accessor in the original: going through it
// changes the register allocation and scheduling of LoadBackground and SetBackgroundAlpha.
inline ILTClient* GetClientDE()
{
	return g_ClientGlob.m_pClientMgr->m_pClientDE;
}

// FUNCTION: LITHTECH 0x00421a70
LTRESULT CConsole::LoadBackground()
{
	ILTStream *pStream;
	LoadedBitmap bitmap;
	LTRESULT dResult;

	FreeBackground();

	dResult = LT_ERROR;
	if ( (pStream = streamsim_Open("console.pcx", "rb")) == LTNULL )
		return dResult;

	if ( pcx_Create2(pStream, &bitmap) )
	{
		m_hBackground = cis_CreateSurfaceFromPcx( &bitmap );

		m_bBackgroundOptimized = GetClientDE()->OptimizeSurface(m_hBackground, RGB(0,0,0));
		SetBackgroundAlpha( m_fBackgroundAlpha );

		dResult = LT_OK;
	}

	pStream->Release();

	return dResult;
}

// FUNCTION: LITHTECH 0x00421b80
void CConsole::SetBackgroundAlpha( float fValue )
{
	if ( !m_hBackground )
		return;

	m_fBackgroundAlpha = fValue;

	if ( m_bBackgroundOptimized != LT_OK )
		m_bBackgroundOptimized = GetClientDE()->OptimizeSurface(m_hBackground, RGB(0,0,0));

	GetClientDE()->SetSurfaceAlpha( m_hBackground, fValue );
}

// FUNCTION: LITHTECH 0x00421bf0
int CConsole::Scroll( int iOffset )
{
	m_iScrollOffset = min( max( m_iScrollOffset + iOffset, 0 ), (int)(m_TextLines.GetSize() - m_nTextLines + 1));
	return m_iScrollOffset;
}

inline LTBOOL CConsole::GetTextLineBox(uint32 iLine, LTRect *pRect, LTBOOL bScreen)
{
	LTRect cRect;

	if (bScreen)
		cRect = m_ScrRect;
	else
		CalcRect( cRect );

	pRect->top = cRect.top + (iLine * m_FontHeight) + CONSOLE_TOP_BORDER;
	pRect->bottom = cRect.top + ((iLine+1) * m_FontHeight) + 1;
	pRect->left = cRect.left + CONSOLE_LEFT_BORDER;
	pRect->right = cRect.right - 1;

	if ( ((pRect->bottom - pRect->top) > 0) && ((pRect->right - pRect->left) > 0) )
		return LTTRUE;
	else
		return LTFALSE;
}

// FUNCTION: LITHTECH 0x00421c40
void CConsole::SetRect( const LTRect &cRect )
{
	// Jump out if it hasn't changed
	if ((m_Rect.left == cRect.left) && (m_Rect.top == cRect.top) && (m_Rect.right == cRect.right) && (m_Rect.bottom == cRect.bottom))
		return;

	m_Rect = cRect;

	LTRect TempRect;
	CalcRect( TempRect );

	m_nTextLines = (TempRect.bottom - TempRect.top) / m_FontHeight;
	m_nTextLines -= 2;	// Leave 1 for the command box.

	LTRect cCommandRect;
	GetTextLineBox( m_nTextLines, &cCommandRect);
	GetCommandBox()->SetRect( cCommandRect );
}

// FUNCTION: LITHTECH 0x00421d50
void CConsole::CalcRect( LTRect &cRect )
{
	// Just default to the rendering rectangle if we don't have a rendering structure yet
	if ( !GetRenderStruct() )
	{
		cRect = m_Rect;
		return;
	}

	// Get the new rectangle based on the screen width and height for negative values
	if ( m_Rect.top == -1 )
		cRect.top = 0;
	else if ( m_Rect.top < 0 )
		cRect.top = (int)GetRenderStruct()->m_Height / (-m_Rect.top);
	else
		cRect.top = m_Rect.top;

	if ( m_Rect.left == -1 )
		cRect.left = 0;
	else if ( m_Rect.left < 0 )
		cRect.left = (int)GetRenderStruct()->m_Width / (-m_Rect.left);
	else
		cRect.left = m_Rect.left;

	if ( m_Rect.right == -1 )
		cRect.right = (int)GetRenderStruct()->m_Width;
	else if ( m_Rect.right < 0 )
		cRect.right = (int)GetRenderStruct()->m_Width - ((int)GetRenderStruct()->m_Width / (-m_Rect.right));
	else
		cRect.right = m_Rect.right;

	if ( m_Rect.bottom == -1 )
		cRect.bottom = (int)GetRenderStruct()->m_Height;
	else if ( m_Rect.bottom < 0 )
		cRect.bottom = (int)GetRenderStruct()->m_Height - ((int)GetRenderStruct()->m_Height / (-m_Rect.bottom));
	else
		cRect.bottom = m_Rect.bottom;

	// Clip the rectangle to the screen
	cRect.right = min( cRect.right, (int)GetRenderStruct()->m_Width );
	cRect.bottom = min( cRect.bottom, (int)GetRenderStruct()->m_Height );

	// Restrict the rectangle to a minimum size
	cRect.right = max( cRect.right, 160 );
	cRect.bottom = max( cRect.bottom, m_FullFontHeight * 4 );
	cRect.left = min( cRect.left, cRect.right - 160 );
	cRect.top = min( cRect.top, cRect.bottom - (m_FullFontHeight * 4) );
}

// FUNCTION: LITHTECH 0x00421ed0
void CConsole::PrintString(CONCOLOR theColor, int filterLevel, const char *pMsg)
{
	// Protect agains error messages that happen after the destructor has been called
	if (!m_bInitialized)
		return;

	if ( filterLevel > m_FilterLevel )
		return;

	CConTextLine*	pLine;

	m_CS.Enter();

	if ( m_ErrorLogFn )
		m_ErrorLogFn( pMsg );

	if ( g_CV_TraceConsole )
	{
		OutputDebugString( pMsg );
		OutputDebugString( "\n" );
	}

	if ( m_TextLines.GetSize() > 0 )
	{
		int iLength;
		const char *pNewLine;
		do
		{
			// Search for a newline character
			pNewLine = strchr( pMsg, '\n' );
			if ( pNewLine && (pNewLine[1]) )
				iLength = pNewLine - pMsg;
			else
			{
				pNewLine = LTNULL;
				iLength = MAX_CONSOLE_TEXTLEN - 1;
			}

			// Get the head of the text list
			pLine = m_TextLines.GetHead();
			// Make sure the list doesn't get too long
			m_TextLines.RemoveAt( pLine );

			// Copy the message
			pLine->m_Color = theColor;
			strncpy( pLine->m_Text, pMsg, iLength );

			// Remove the newline character
			if ( pNewLine )
			{
				pLine->m_Text[iLength] = 0;
				pMsg = &pNewLine[1];
			}

			// Add it to the end of the list
			m_TextLines.AddTail( pLine );
		} while ( pNewLine );
	}

	m_CS.Leave();
}

// FUNCTION: LITHTECH 0x00422030
void CConsole::vPrintf(CONCOLOR theColor, int filterLevel, const char *pMsg, va_list vaArgs)
{
	if ( FilterAction( filterLevel ) )
		return;

	char str[500];
	_vsnprintf(str, sizeof(str)-1, pMsg, vaArgs);
	PrintString(theColor, filterLevel, str);
}

// FUNCTION: LITHTECH 0x00422090
void CConsole::Printf(CONCOLOR theColor, int filterLevel, const char *pMsg, ...)
{
	va_list vaArgs;
	va_start(vaArgs, pMsg);
	vPrintf(theColor, filterLevel, pMsg, vaArgs);
	va_end(vaArgs);
}

// FUNCTION: LITHTECH 0x004220b0
void CConsole::OnKeyPress(uint32 key)
{
	switch (key)
	{
		case VK_RETURN :
			FinishCommand();
			break;
		case VK_UP :
			if ( IsKeyDown( VK_CONTROL ) )
				Scroll( 1 );
			else
				CycleCommands( -1 );
			break;
		case VK_DOWN :
			if ( IsKeyDown( VK_CONTROL ) )
				Scroll( -1 );
			else
				CycleCommands( 1 );
			break;
		case VK_TAB :
			if ( IsKeyDown( VK_CONTROL ) )
				MatchCommands();
			else if ( IsKeyDown( VK_SHIFT ) )
				PrevCommand();
			else
				NextCommand();
			break;
		case VK_PRIOR :
			if ( IsKeyDown( VK_CONTROL ) )
				Scroll( (int)m_TextLines.GetSize() );
			else
				Scroll( (int)m_nTextLines );
			break;
		case VK_NEXT :
			if ( IsKeyDown( VK_CONTROL ) )
				Scroll( -GetScrollOffset() );
			else
				Scroll( -(int)m_nTextLines );
			break;
		default :
			EndNav();
			GetCommandBox()->OnKeyPress( key );
			break;
	}
}
