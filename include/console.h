// Client console interface (Jupiter client/src/console.h) and the Talon console classes
// (Jupiter client/src/sys/win/winconsole_impl.h). Layouts recovered from the constructors
// (CConsole 0x00420ff0, CConCommandBox 0x004208b0, CConHistory 0x00420d70).
#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <windows.h>
#include "ltbasedefs.h"
#include "lthread.h"
#include "../../build/proj/LT2/lithshared/stdlith/goodlinklist.h"

typedef void (*CommandHandler)(const char *pCommand);
typedef void (*ErrorLogFn)(const char *pMsg);

// 00 bb gg rr
typedef uint32 CONCOLOR;
#define CONRGB(r,g,b) (((uint32)(b)<<16) | ((uint32)(g)<<8) | (uint32)(r))

struct RenderStruct;
class CConsole;

#define MAX_CONSOLE_TEXTLEN		256
#define NUM_CONSOLE_CHARACTERS	127

#define CONSOLE_LEFT_BORDER	8
#define CONSOLE_TOP_BORDER	5

// One line of text (0x10c bytes).
class CConTextLine : public CGLLNode
{
public:

	CONCOLOR	m_Color;						// 0x08
	char		m_Text[MAX_CONSOLE_TEXTLEN];	// 0x0c

};

// A list of text lines
typedef CGLinkedList<CConTextLine*> CConTextList;

// The command box of the console (0x130 bytes).
class CConCommandBox
{
protected:
	CConsole	*m_pConsole;							// 0x00

	char	TranslateKey( uint32 key ) const;

	int		m_iCurLength;								// 0x04
	char	m_CurCommand[MAX_CONSOLE_TEXTLEN + 1];		// 0x08

	CONCOLOR	m_TextColor;							// 0x10c
	CONCOLOR	m_BackColor, m_BorderColor;				// 0x110, 0x114

	LTRect	m_Rect;										// 0x118

	uint32	m_FlashTime;								// 0x128

	int		m_iCursorPos;								// 0x12c

	void	MoveCursor( int iOffset );
	void	MoveWord( int iOffset );
	void	AddChar( char key );
	void	DeleteChar( int iOffset );
	void	DeleteWord( int iOffset );

public:

	// Construction/destruction

	CConCommandBox();
	~CConCommandBox() {};

	void	Init() {};
	void	Term();

	// Member access

	LTRect	GetRect() const { return m_Rect; };
	void	SetRect( LTRect cRect ) { m_Rect = cRect; };

	const char*		GetCommand() { return &m_CurCommand[0]; };
	void			SetCommand( const char *pCommand );

	CONCOLOR	GetTextColor() const { return m_TextColor; };
	void		SetTextColor( CONCOLOR cTextColor ) { m_TextColor = cTextColor; };

	CONCOLOR	GetBackColor() const { return m_BackColor; };
	void		SetBackColor( CONCOLOR cColor ) { m_BackColor = cColor; };

	CONCOLOR	GetBorderColor() const { return m_BorderColor; };
	void		SetBorderColor( CONCOLOR cColor ) { m_BorderColor = cColor; };

	CConsole*	GetConsole() const { return m_pConsole; };
	void		SetConsole( CConsole *pConsole ) { m_pConsole = pConsole; };

	uint32	GetFlashTime() const { return m_FlashTime; };
	void	SetFlashTime( uint32 dwTime ) { m_FlashTime = dwTime; };

	// Command manipulation
	void	Clear();

	// Event handlers
	void	OnKeyPress( uint32 key );

};

// Console list iterator (Jupiter winconsole_impl.h). Talon returns LTBOOL.
class CConIterator
{
protected:
	virtual LTBOOL	Begin() { return LTFALSE; }
	virtual LTBOOL	End() { return LTFALSE; }
	virtual LTBOOL	NextItem() { return LTFALSE; }
	virtual LTBOOL	PrevItem() { return LTFALSE; }

public:
	CConIterator() {}
	virtual ~CConIterator() {}

	virtual LTBOOL	First( const char *pValue = LTNULL );	// 0x00420730
	virtual LTBOOL	Last( const char *pValue = LTNULL );	// 0x00420790
	virtual LTBOOL	Next( const char *pValue = LTNULL );	// 0x004207f0
	virtual LTBOOL	Prev( const char *pValue = LTNULL );	// 0x00420850

	virtual const char*	Get() const { return LTNULL; }
};


// Command history tracker for the console (0x18 bytes). Talon has no Remove().
class CConHistory : public CConIterator
{
protected:
	CConTextLine*	m_pLines;			// 0x04
	int		m_iIndex;					// 0x08
	int		m_iListSize;				// 0x0c
	int		m_iListStart, m_iListEnd;	// 0x10, 0x14

	// Parent class overrides
	virtual LTBOOL	Begin();
	virtual LTBOOL	End();
	virtual LTBOOL	NextItem();
	virtual LTBOOL	PrevItem();

public:
	CConHistory( int iListSize = 0 );
	virtual ~CConHistory();

	// Member access
	int		GetSize() const { return (m_iListSize) ? (m_iListSize - 1) : 0; };

	// Add a line to the history list
	void	Add( const char *pString );
	// Resize the history list
	LTBOOL	Resize( int iSize );
	// Clear the history list
	void	Clear() { m_iListStart = m_iListEnd = m_iIndex = 0; };

	// Parent class overrides
	virtual const char*		Get() const;
};


// The actual console class (0x3f0 bytes).
class CConsole
{
private:
	LTBOOL m_bInitialized;				// 0x00
	// This variable makes sure Term() isn't called from Init unless Init has been called un-paired
	LTBOOL m_bInitTerminate;			// 0x04
	LTBOOL m_bSaveVariablesMask;		// 0x08

protected:

	enum EConState {
		STATE_NORMAL = 0,
		STATE_COMPLETE = 1,
		STATE_HISTORY = 2
	};

	EConState	m_eState;				// 0x0c

	EConState	GetState() const { return m_eState; };
	void		SetState( EConState eState ) { m_eState = eState; };

	CConCommandBox	m_CommandBox;		// 0x10

	uint32	m_OutputFlags;				// 0x140
	int		m_FilterLevel;				// 0x144

	LTBOOL	InitFont();
	void	TermFont();

	void	FreeBackground();

	inline LTBOOL	GetTextLineBox( uint32 iLine, LTRect *pRect, LTBOOL bScreen = LTFALSE );

	void	FinishCommand();
	void	CheckVariables();

	// Command navigation mode
	void	StartNav( EConState eState );
	void	EndNav();
	char	m_aNavCommand[MAX_CONSOLE_TEXTLEN];	// 0x148

	// Internal support for command completion
	void	NextCommand();
	void	PrevCommand();
	void	MatchCommands();

	uint32	m_BackColor, m_BorderColor;	// 0x248, 0x24c

	LCriticalSection	m_CS;			// 0x250

	// The structure used for buffer stuff.
	RenderStruct*	m_pStruct;			// 0x268

	LTRect	m_Rect, m_ScrRect;			// 0x26c, 0x27c
	// Fill a rectangle with the calculated output rectangle
	void	CalcRect( LTRect &cRect );

	// The font (a 32-bit packed monochrome bitmap, m_FullFontHeight uint32's per character).
	LTRect	m_FontRect;					// 0x28c
	uint32	*m_pFontBitmapData;			// 0x29c

	// The 'ascent' part of the font. This is used for layout calculations.
	uint16	m_FontHeight;				// 0x2a0

	// The full font height.. Used for drawing.
	uint16	m_FullFontHeight;			// 0x2a2

	uint16	m_CharWidths[NUM_CONSOLE_CHARACTERS];	// 0x2a4

	HSURFACE	m_hBackground;			// 0x3a4
	float		m_fBackgroundAlpha;		// 0x3a8
	LTBOOL		m_bBackgroundOptimized;	// 0x3ac

	// Error log function.
	ErrorLogFn	m_ErrorLogFn;			// 0x3b0

	// The window it uses to get the DC and stuff.
	HWND	m_hWnd;						// 0x3b4

	CConTextList	m_TextLines;		// 0x3b8
	uint32			m_nTextLines;		// 0x3c4
	int				m_iScrollOffset;	// 0x3c8

	// The doskey-ish text command queue.
	CConHistory		m_cHistory;			// 0x3cc

	// The command callback handler
	CommandHandler	m_CommandHandler;	// 0x3e4

	// The command completion iterator
	CConIterator	*m_pCompletionIterator;	// 0x3e8

	uint32			m_Unknown3ec;		// 0x3ec

public:

				CConsole();
				~CConsole();

	LTBOOL		Init(const LTRect *pRect, CommandHandler handler, RenderStruct *pStruct,
					CConIterator *pCompletionIterator = LTNULL);
	LTBOOL		InitBare();
	void		Term(LTBOOL bDeleteTextLines = LTTRUE);

	LTRESULT	LoadBackground();

	// Member access
	CConCommandBox*		GetCommandBox() { return &m_CommandBox; }

	int		GetFilterLevel() const { return m_FilterLevel; };
	void	SetFilterLevel( int iFilterLevel ) { m_FilterLevel = iFilterLevel; };

	float	GetBackgroundAlpha() const { return m_fBackgroundAlpha; };
	void	SetBackgroundAlpha( float fValue );

	RenderStruct*	GetRenderStruct() const { return m_pStruct; };
	void			SetRenderStruct( RenderStruct *pStruct ) { m_pStruct = pStruct; };

	ErrorLogFn	GetErrorLogFn() const { return m_ErrorLogFn; };
	void		SetErrorLogFn( ErrorLogFn fn ) { m_ErrorLogFn = fn; };

	// Note:  m_Rect is on the SCREEN!
	LTRect	GetRect() const { return m_Rect; };
	void	SetRect( const LTRect &cRect );

	// All the text lines and how many it actually can display in its rectangle.
	LTBOOL	ResizeTextLines( uint32 nNewSize );

	CConHistory*	GetCommandHistory() { return &m_cHistory; };

	int		GetScrollOffset() const { return m_iScrollOffset; };

	// Filtering
	LTBOOL	FilterAction( int iFilterLevel ) { return iFilterLevel > m_FilterLevel; };

	// Print a string in a given color as long as the filter level is not too high
	void	PrintString( CONCOLOR theColor, int filterLevel, const char *pMsg );
	void	vPrintf( CONCOLOR theColor, int filterLevel, const char *pMsg, va_list vaArgs );
	void	Printf( CONCOLOR theColor, int filterLevel, const char *pMsg, ... );

	// Scrolling
	int		Scroll( int iOffset );

	// Cycle through the recent command list by the given amount
	void	CycleCommands( int iCount );

	// Tab completion iterator
	CConIterator*	GetCompletionIterator() { return m_pCompletionIterator; };
	void			SetCompletionIterator( CConIterator *pIterator );

	// Event handling
	void	OnKeyPress( uint32 key );
};

extern CConsole g_Console;
#define GETCONSOLE() (&g_Console)


// ------------------------------------------------------------------ //
// The console interface.
// ------------------------------------------------------------------ //

LTBOOL con_InitBare();
LTBOOL con_Init(LTRect *pRect, CommandHandler handler, RenderStruct *pStruct);
void con_Term(LTBOOL bDeleteTextLines);
LTRESULT con_LoadBackground();
void con_SetErrorLog(ErrorLogFn theFunction);
void con_OnKeyPress(uint32 key);

void con_PrintString(CONCOLOR theColor, int filterLevel, const char *pMsg);
void con_Printf(CONCOLOR theColor, int filterLevel, const char *pMsg, ...);
void con_WhitePrintf(const char *pMsg, ...);

// Default empty iterator (console.cpp)
extern CConIterator g_ConEmptyIterator;

#endif // __CONSOLE_H__
