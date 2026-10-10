// Jupiter runtime/kernel/src/sys/win/ltdirectmusic_impl.h, Talon layout (0x110 bytes).
// Talon is the DirectX 7 era version: one performance with a port (no audiopaths), reverb
// through the port's IKsControl, a notification thread started with _beginthread that
// reaches the manager through a static pointer, and CBaseList lists.
// Needs /I lithshared/lith and /I lithshared/controlfilemgr.
#ifndef __LTDIRECTMUSIC_IMPL_H__
#define __LTDIRECTMUSIC_IMPL_H__

#include <windows.h>
#include "ltdirectmusicloader.h"
#include "lith.h"
#include "lithchunkallocator.h"
#include "iltdirectmusic.h"

// Different command types that can be in a command queue
enum LTDMCommandTypes
{
	LTDMCommandNull = 0,
	LTDMCommandStopPlaying,
	LTDMCommandPauseQueue,
	LTDMCommandPlaySegment,
	LTDMCommandPlaySecondarySegment,
	LTDMCommandPlayMotif,
	LTDMCommandAdjustVolume,
	LTDMCommandClearOldCommands,
	LTDMCommandLoopToStart,
	LTDMCommandChangeIntensity,
	LTDMCommandStopSegment,
	LTDMCommandPlayTransition
};

enum LTDMFileTypes
{
	LTDMFileTypeNull = 0,
	LTDMFileTypeAny,
	LTDMFileTypeControlFile,
	LTDMFileTypeDLS,
	LTDMFileTypeStyle,
	LTDMFileTypeSegment,
	LTDMFileTypeChordMap,
};

// forward class defines
class CControlFileMgr;


// Main class for LTDirectMusicMgr
class CLTDirectMusicMgr : public ILTDirectMusicMgr
{
public:
	////////////////////////////////
	// external functions

	// default constructor
	CLTDirectMusicMgr();

	// Initialize the Mgr
	virtual LTRESULT Init();

	// Terminate the Mgr
	virtual LTRESULT Term();

	// Initialize a game level using the parameters in the given control file
	virtual LTRESULT InitLevel(const char* sWorkingDirectory, const char* sControlFileName, const char* sDefine1 = LTNULL,
					  const char* sDefine2 = LTNULL, const char* sDefine3 = LTNULL);

	// Teminate the current game level
	virtual LTRESULT TermLevel();

	// Begin playing music
	virtual LTRESULT Play();

	// Stop playing music
	virtual LTRESULT Stop(const LTDMEnactTypes nStart = LTDMEnactDefault);

	// Pause music playing
	virtual LTRESULT Pause(const LTDMEnactTypes nStart = LTDMEnactDefault);

	// UnPause music playing
	virtual LTRESULT UnPause();

	// Set current volume
	virtual LTRESULT SetVolume(const long nVolume);

	// Change the intensity level
	virtual LTRESULT ChangeIntensity(const int nNewIntensity, const LTDMEnactTypes nStart = LTDMEnactInvalid);

	// Play a secondary segment
	virtual LTRESULT PlaySecondary(const char* sSecondarySegment, const LTDMEnactTypes nStart = LTDMEnactDefault);

	// Stop all secondary segments with the specified name (if LTNULL it stops them all)
	virtual LTRESULT StopSecondary(const char* sSecondarySegment = LTNULL, const LTDMEnactTypes nStart = LTDMEnactDefault);

	// Play a motif
	virtual LTRESULT PlayMotif(const char* sMotifName, const LTDMEnactTypes nStart = LTDMEnactDefault);

	// Play a motif
	virtual LTRESULT PlayMotif(const char* sStyleName, const char* sMotifName, const LTDMEnactTypes nStart = LTDMEnactDefault);

	// Stop all motifs with the specified name (if LTNULL it stops them all)
	virtual LTRESULT StopMotif(const char* sMotifName = LTNULL, const LTDMEnactTypes nStart = LTDMEnactDefault);

	// Stop all motifs with the specified name (if LTNULL it stops them all)
	virtual LTRESULT StopMotif(const char* sStyleName, const char* sMotifName = LTNULL, const LTDMEnactTypes nStart = LTDMEnactDefault);

	// return the current intenisty
	virtual int GetCurIntensity();

	// convert a string to an enact type
	virtual LTDMEnactTypes StringToEnactType(const char* sName);

	// convert an enact type to a string
	virtual void EnactTypeToString(LTDMEnactTypes nType, char* sName);

	// return the number of intensities currently in this level (undefined if not in a level)
	virtual int GetNumIntensities();

	// return the initial intensity value for this level
	virtual int GetInitialIntensity();

	// return the initial volume.
	virtual int GetInitialVolume();

	// return the volume offset.  This offset is applied to whatever volume is set.
	virtual int GetVolumeOffset();

	////////////////////////////////////////////
	// Misc extra external functions the user doesn't normally need

	// get the directmusic performance
	IDirectMusicPerformance* GetDMPerformance() { return m_pPerformance; };

	// return the directmusic flags value that corresponds to the specified enact value
	uint32 EnactTypeToFlags(LTDMEnactTypes nEnactVal);

	// Set the directmusic working directory
	void SetWorkingDirectory (const char* sWorkingDirectory, LTDMFileTypes nFileType = LTDMFileTypeAny);

public:

	// foward class defines
	class CSegment;
	class CTransition;

	////////////////////////////////////////////
	// internal classes for command queue

	// class for command queue item (0x14 bytes)
	class CCommandItem : public CBaseListItem
	{
	public:
		// default constructor
		CCommandItem() { m_nCommandType = LTDMCommandNull; };

		// get the command type
		LTDMCommandTypes GetCommandType() { return m_nCommandType; };

		// get the next segment in this list of segments
		CCommandItem* Next() { return (CCommandItem*)CBaseListItem::Next(); };

		// get the previous segment in this list of segments
		CCommandItem* Prev() { return (CCommandItem*)CBaseListItem::Prev(); };

		// Chunk allocation new and delete operators
		void *operator new(size_t sz) { return (void*)m_ChunkAllocator.Alloc(); };
		void operator delete(void* p) {	m_ChunkAllocator.Free((CCommandItem*)p); };

		// Chunk allocator for this object (actually for the user derived ItemType class)
		static CLithChunkAllocator<CCommandItem> m_ChunkAllocator;

	public:
		// set the command type
		void SetCommandType(LTDMCommandTypes nCommandType) { m_nCommandType = nCommandType; };

		// the type of command that this is
		LTDMCommandTypes m_nCommandType;	// 0x08

		// pointer to direct music segment for use by various commands
		CSegment* m_pSegment;				// 0x0c

		// misc use int value
		int m_nVal;							// 0x10
	};

	// CCommandItemPauseQueue derived class
	class CCommandItemPauseQueue : public CCommandItem
	{
	public:
		CCommandItemPauseQueue() { SetCommandType(LTDMCommandPauseQueue); };
	};

	// CCommandItemPlaySegment derived class
	class CCommandItemPlaySegment : public CCommandItem
	{
	public:
		CCommandItemPlaySegment () { SetCommandType(LTDMCommandPlaySegment); };
		CSegment* GetSegment() { return m_pSegment; };
		void SetSegment(CSegment* pSeg) { m_pSegment = pSeg; };
	};

	// CCommandItemPlayTransition derived class
	class CCommandItemPlayTransition : public CCommandItem
	{
	public:
		CCommandItemPlayTransition () { SetCommandType(LTDMCommandPlayTransition); };
		CTransition* GetTransition() { return (CTransition*)m_pSegment; };
		void SetTransition(CTransition* pSeg) { m_pSegment = (CSegment*)pSeg; };
	};

	// CCommandItemPlaySecondarySegment derived class
	class CCommandItemPlaySecondarySegment : public CCommandItem
	{
	public:
		CCommandItemPlaySecondarySegment () { SetCommandType(LTDMCommandPlaySecondarySegment); };
		CSegment* GetSegment() { return m_pSegment; };
		void SetSegment(CSegment* pSeg) { m_pSegment = pSeg; };
	};

	// CCommandItemPlayMotif derived class
	class CCommandItemPlayMotif : public CCommandItem
	{
	public:
		CCommandItemPlayMotif () { SetCommandType(LTDMCommandPlayMotif); };
		CSegment* GetSegment() { return m_pSegment; };
		void SetSegment(CSegment* pSeg) { m_pSegment = pSeg; };
	};

	// CCommandItemLoopToStart derived class
	class CCommandItemLoopToStart : public CCommandItem
	{
	public:
		CCommandItemLoopToStart () { SetCommandType(LTDMCommandLoopToStart); };
		int GetNumLoops() { return m_nVal; };
		void SetNumLoops(int nNumLoops) { m_nVal = nNumLoops; };
	};

	// CCommandChangeIntensity derived class
	class CCommandChangeIntensity : public CCommandItem
	{
	public:
		CCommandChangeIntensity () { SetCommandType(LTDMCommandChangeIntensity); };
		int GetNewIntensity() { return m_nVal; };
		void SetNewIntensity(int nNewIntensity) { m_nVal = nNewIntensity; };
	};

	// CCommandItemStopSegment derived class
	class CCommandItemStopSegment : public CCommandItem
	{
	public:
		CCommandItemStopSegment () { SetCommandType(LTDMCommandStopSegment); };
		CSegment* GetSegment() { return m_pSegment; };
		void SetSegment(CSegment* pSeg) { m_pSegment = pSeg; };
	};

	// class for list of command items
	class CCommandItemList : public CBaseList
	{
	public:
		CCommandItem* GetFirst() { return (CCommandItem*)CBaseList::GetFirst(); };
		CCommandItem* GetLast() { return (CCommandItem*)CBaseList::GetLast(); };
	};

	/////////////////////////////////////////////////
	// internal classes for segment items and lists

	// class for segment item (0x54 bytes)
	class CSegment : public CBaseListItem
	{
	public:
		CSegment() { m_pDMSegment = LTNULL; m_sSegmentName[0] = '\0'; m_sSegmentNameLong = LTNULL; m_nDefaultEnact = LTDMEnactNextMeasure; };
		~CSegment() { if (m_sSegmentNameLong != LTNULL) delete [] m_sSegmentNameLong; };

		IDirectMusicSegment* GetDMSegment() { return m_pDMSegment; };
		void SetDMSegment(IDirectMusicSegment* pDMSeg) { m_pDMSegment = pDMSeg; };

		CSegment* Next() { return (CSegment*)CBaseListItem::Next(); };
		CSegment* Prev() { return (CSegment*)CBaseListItem::Prev(); };

		// get the segment name
		const char* GetSegmentName();

		// set the segment name (returns true if successful)
		LTBOOL SetSegmentName(const char* sSegmentName);

		void SetDefaultEnact(LTDMEnactTypes nDefaultEnact) {m_nDefaultEnact = nDefaultEnact;}
		LTDMEnactTypes GetDefaultEnact() { return m_nDefaultEnact; };

		// Chunk allocation new and delete operators
		void *operator new(size_t sz) { return (void*)m_ChunkAllocator.Alloc(); };
		void operator delete(void* p) {	m_ChunkAllocator.Free((CSegment*)p); };

		static CLithChunkAllocator<CSegment> m_ChunkAllocator;

	public:
		IDirectMusicSegment* m_pDMSegment;	// 0x08
		char m_sSegmentName[64];			// 0x0c
		char* m_sSegmentNameLong;			// 0x4c
		LTDMEnactTypes m_nDefaultEnact;		// 0x50
	};

	// class for list of segment items
	class CSegmentList : public CBaseList
	{
	public:
		CSegment* GetFirst() { return (CSegment*)CBaseList::GetFirst(); };
		CSegment* GetLast() { return (CSegment*)CBaseList::GetLast(); };

		// find the segment object that has the given name
		CSegment* Find(const char* sName);

		// cleanup a segment (stops, deletes, and removes from list)
		void CleanupSegment(CLTDirectMusicMgr* pLTDMMgr, CSegment* pSegment, LTDMEnactTypes nStart, LTBOOL bOnlyIfNotPlaying);

		// cleanup non-playing segments
		void CleanupSegments(CLTDirectMusicMgr* pLTDMMgr, LTBOOL bOnlyIfNotPlaying);
	};

	/////////////////////////////////////////////////
	// internal classes for segment state items and lists

	// class for segment state item (0x10 bytes)
	class CSegmentState : public CBaseListItem
	{
	public:
		CSegmentState() { m_pDMSegmentState = LTNULL; m_pSegment = LTNULL; };
		~CSegmentState() { };

		IDirectMusicSegmentState* GetDMSegmentState() { return m_pDMSegmentState; };
		void SetDMSegmentState(IDirectMusicSegmentState* pDMSegState) { m_pDMSegmentState = pDMSegState; };

		CSegment* GetSegment() { return m_pSegment; };
		void SetSegment(CSegment* pSeg) { m_pSegment = pSeg; };

		CSegmentState* Next() { return (CSegmentState*)CBaseListItem::Next(); };
		CSegmentState* Prev() { return (CSegmentState*)CBaseListItem::Prev(); };

		// Chunk allocation new and delete operators
		void *operator new(size_t sz) { return (void*)m_ChunkAllocator.Alloc(); };
		void operator delete(void* p) {	m_ChunkAllocator.Free((CSegmentState*)p); };

		static CLithChunkAllocator<CSegmentState> m_ChunkAllocator;

	public:
		IDirectMusicSegmentState* m_pDMSegmentState;	// 0x08
		CSegment* m_pSegment;							// 0x0c
	};

	// class for list of segment state items
	class CSegmentStateList : public CBaseList
	{
	public:
		CSegmentState* GetFirst() { return (CSegmentState*)CBaseList::GetFirst(); };
		CSegmentState* GetLast() { return (CSegmentState*)CBaseList::GetLast(); };

		// find the segment state whose segment has the given name
		CSegmentState* Find(const char* sName);

		// find the segment state object that has the given direct music segment state pointer
		CSegmentState* Find(const IDirectMusicSegmentState* pDMSegmentState);

		// add a new segment state (creates, and add to the list)
		void CreateSegmentState(IDirectMusicSegmentState* pDMSegmentState, CSegment* pSegment);

		// cleanup a segment state (stops, deletes, and removes from list)
		void CleanupSegmentState(CLTDirectMusicMgr* pLTDMMgr, CSegmentState* pSegmentState, LTDMEnactTypes nStart, LTBOOL bOnlyIfNotPlaying);

		// cleanup segment states
		void CleanupSegmentStates(CLTDirectMusicMgr* pLTDMMgr, LTBOOL bOnlyIfNotPlaying);
	};

	/////////////////////////////////////////////////
	// internal classes for band items and lists

	// class for band item (0x10 bytes)
	class CBand : public CBaseListItem
	{
	public:
		IDirectMusicBand* GetBand() { return m_pBand; };
		void SetBand(IDirectMusicBand* pSeg) { m_pBand = pSeg; };

		CBand* Next() { return (CBand*)CBaseListItem::Next(); };
		CBand* Prev() { return (CBand*)CBaseListItem::Prev(); };

		IDirectMusicSegment* GetDMSegment() { return m_pDMSegment; };
		void SetDMSegment(IDirectMusicSegment* pDMSeg) { m_pDMSegment = pDMSeg; };

		// Chunk allocation new and delete operators
		void *operator new(size_t sz) { return (void*)m_ChunkAllocator.Alloc(); };
		void operator delete(void* p) {	m_ChunkAllocator.Free((CBand*)p); };

		static CLithChunkAllocator<CBand> m_ChunkAllocator;

	public:
		IDirectMusicBand* m_pBand;				// 0x08
		IDirectMusicSegment* m_pDMSegment;		// 0x0c
	};

	// class for list of band items
	class CBandList : public CBaseList
	{
	public:
		CBand* GetFirst() { return (CBand*)CBaseList::GetFirst(); };
		CBand* GetLast() { return (CBand*)CBaseList::GetLast(); };
	};

	/////////////////////////////////////////////////
	// internal classes for style items and lists

	// class for style item (0x50 bytes)
	class CStyle : public CBaseListItem
	{
	public:
		CStyle() { m_pDMStyle = LTNULL; m_sStyleName[0] = '\0'; m_sStyleNameLong = LTNULL; };
		~CStyle() { if (m_sStyleNameLong != LTNULL) delete [] m_sStyleNameLong; };

		IDirectMusicStyle* GetDMStyle() { return m_pDMStyle; };
		void SetDMStyle(IDirectMusicStyle* pStyle) { m_pDMStyle = pStyle; };

		CStyle* Next() { return (CStyle*)CBaseListItem::Next(); };
		CStyle* Prev() { return (CStyle*)CBaseListItem::Prev(); };

		// get the style name
		const char* GetStyleName();

		// set the style name (returns true if successful)
		LTBOOL SetStyleName(const char* sStyleName);

		// Chunk allocation new and delete operators
		void *operator new(size_t sz) { return (void*)m_ChunkAllocator.Alloc(); };
		void operator delete(void* p) {	m_ChunkAllocator.Free((CStyle*)p); };

		static CLithChunkAllocator<CStyle> m_ChunkAllocator;

	public:
		IDirectMusicStyle* m_pDMStyle;	// 0x08
		char m_sStyleName[64];			// 0x0c
		char* m_sStyleNameLong;			// 0x4c
	};

	// class for list of style items
	class CStyleList : public CBaseList
	{
	public:
		CStyle* GetFirst() { return (CStyle*)CBaseList::GetFirst(); };
		CStyle* GetLast() { return (CStyle*)CBaseList::GetLast(); };

		// find the style object that has the given name
		CStyle* Find(const char* sName);
	};

	/////////////////////////////////////////////////
	// internal classes for dls bank items and lists

	// class for DLSBank item (0x0c bytes)
	class CDLSBank : public CBaseListItem
	{
	public:
		IDirectMusicCollection* GetDLSBank() { return m_pDLSBank; };
		void SetDLSBank(IDirectMusicCollection* pSeg) { m_pDLSBank = pSeg; };

		CDLSBank* Next() { return (CDLSBank*)CBaseListItem::Next(); };
		CDLSBank* Prev() { return (CDLSBank*)CBaseListItem::Prev(); };

		// Chunk allocation new and delete operators
		void *operator new(size_t sz) { return (void*)m_ChunkAllocator.Alloc(); };
		void operator delete(void* p) {	m_ChunkAllocator.Free((CDLSBank*)p); };

		static CLithChunkAllocator<CDLSBank> m_ChunkAllocator;

	public:
		IDirectMusicCollection* m_pDLSBank;		// 0x08
	};

	// class for list of DLSBank items
	class CDLSBankList : public CBaseList
	{
	public:
		CDLSBank* GetFirst() { return (CDLSBank*)CBaseList::GetFirst(); };
		CDLSBank* GetLast() { return (CDLSBank*)CBaseList::GetLast(); };
	};

	//////////////////////////////////////////////////
	// internal class that defines a transition (0x0c bytes)
	class CTransition
	{
	public:
		LTDMEnactTypes GetEnactTime() { return m_nEnactTime; };
		void SetEnactTime(LTDMEnactTypes nEnactTime) { m_nEnactTime = nEnactTime; };

		LTBOOL GetManual() { return m_bManual; };
		void SetManual(LTBOOL bManual) { m_bManual = bManual; };

		IDirectMusicSegment* GetDMSegment() { return m_pDMSegment; };
		void SetDMSegment(IDirectMusicSegment* pDMSeg) { m_pDMSegment = pDMSeg; };

	public:
		LTDMEnactTypes m_nEnactTime;		// 0x00
		LTBOOL m_bManual;					// 0x04
		IDirectMusicSegment* m_pDMSegment;	// 0x08
	};

	//////////////////////////////////////////////////
	// internal class that defines an intensity level (0x10 bytes)
	class CIntensity
	{
	public:
		~CIntensity();

		int GetNumLoops() { return m_nNumLoops; };
		void SetNumLoops(int nNumLoops) { m_nNumLoops = nNumLoops; };

		int GetIntensityToSetAtFinish() { return m_nIntensityToSetAtFinish; };
		void SetIntensityToSetAtFinish(int nIntensityToSetAtFinish) { m_nIntensityToSetAtFinish = nIntensityToSetAtFinish; };

		CSegmentList& GetSegmentList() { return m_lstSegments; };

	public:
		int m_nNumLoops;					// 0x00
		int m_nIntensityToSetAtFinish;		// 0x04
		CSegmentList m_lstSegments;			// 0x08
	};

public:

	////////////////////////////////////////////
	// internal functions

	// Initialize all the basic directmusic stuff (Com, Performance, Loader)
	LTBOOL InitDirectMusic();

	// Initialize the synthesizer state
	LTBOOL InitPerformance();

	// Remove and release the synthesizer port
	LTBOOL TermPort();

	// Terminate everything that InitDirectMusic set up
	void TermDirectMusic();

	// read in and set up DLS Banks from control file
	LTBOOL ReadDLSBanks(CControlFileMgr& controlFile);

	// read in and load styles and bands from control file
	LTBOOL ReadStylesAndBands(CControlFileMgr& controlFile);

	// read in intensity descriptions from control file
	LTBOOL ReadIntensities(CControlFileMgr& controlFile);

	// read in secondary segments from control file
	LTBOOL ReadSecondarySegments(CControlFileMgr& controlFile);

	// read in motifs from control file
	LTBOOL ReadMotifs(CControlFileMgr& controlFile);

	// read in transition matrix from control file
	LTBOOL ReadTransitions(CControlFileMgr& controlFile);

	// Load Segment
	LTBOOL LoadSegment(const char* sSegmentName );

	// Load DLS Bank
	LTBOOL LoadDLSBank(const char* sFileName);

	// Load Style and associated bands
	LTBOOL LoadStyleAndBands(char* sStyleFileName, CControlFileMgr& controlFile);

	// Load Band
	LTBOOL LoadBand(IDirectMusicStyle* pStyle, const char* sBandName);

	// Clear the command queue
	void ClearCommands();

	// Get a pointer to the transition from one intenisty to another
	CTransition* GetTransition(int nFrom, int nTo);

	// Set reverb parameters
	LTBOOL SetReverbParameters(DMUS_WAVES_REVERB_PARAMS params);

	// Enable reverb
	LTBOOL EnableReverb();

	// Disable reverb
	LTBOOL DisableReverb();

	// Initialize reverb parameters from control file and set up reverb in DirectMusic
	LTBOOL InitReverb(CControlFileMgr& controlFile);

	// Terminate reverb if it was enabled.
	LTBOOL TermReverb();

	////////////////////////////////////////////
	// internal member variables

	LTBOOL m_bInitialized;							// 0x04
	LTBOOL m_bLevelInitialized;						// 0x08
	IDirectMusic* m_pDirectMusic;					// 0x0c
	CLTDMLoader* m_pLoader;							// 0x10
	IDirectMusicPerformance* m_pPerformance;		// 0x14
	IDirectMusicPort* m_pPort;						// 0x18
	DMUS_PORTPARAMS m_portParams;					// 0x1c
	CSegmentList m_lstSegments;						// 0x40
	CSegmentList m_lstMotifs;						// 0x48
	CBandList m_lstBands;							// 0x50
	CStyleList m_lstStyles;							// 0x58
	CDLSBankList m_lstDLSBanks;						// 0x60
	CCommandItemList m_lstCommands;					// 0x68
	CCommandItemList m_lstCommands2;				// 0x70
	CCommandItem* m_pLastCommand;					// 0x78
	CSegmentStateList m_lstPrimairySegmentsPlaying;	// 0x7c
	CSegmentStateList m_lstSecondarySegmentsPlaying;// 0x84
	CSegmentStateList m_lstMotifsPlaying;			// 0x8c
	CRITICAL_SECTION m_CommandQueueCriticalSection;	// 0x94
	int m_nCurIntensity;							// 0xac
	int m_nNumIntensities;							// 0xb0
	int m_nInitialIntensity;						// 0xb4
	int m_nInitialVolume;							// 0xb8
	int m_nVolumeOffset;							// 0xbc
	int m_nNumPChannels;							// 0xc0
	int m_nNumVoices;								// 0xc4
	int m_nSynthSampleRate;							// 0xc8
	CIntensity* m_aryIntensities;					// 0xcc
	int m_nNumTransitions;							// 0xd0
	CTransition* m_aryTransitions;					// 0xd4
	char* m_sWorkingDirectoryAny;					// 0xd8
	char* m_sWorkingDirectoryControlFile;			// 0xdc
	GUID m_guid;									// 0xe0
	HANDLE m_hNotify;								// 0xf0
	LTBOOL m_bExitNotificationThread;				// 0xf4
	unsigned long m_hThread;						// 0xf8
	LTBOOL m_bUseReverb;							// 0xfc
	DMUS_WAVES_REVERB_PARAMS m_ReverbParameters;	// 0x100
};


#endif // __LTDIRECTMUSIC_IMPL_H__
