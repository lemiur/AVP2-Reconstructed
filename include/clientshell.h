// Talon client shell (Jupiter runtime/client/src/clientshell.h). Only recovered members.
// One definition for the engine: add members here at their exact offsets.
#ifndef __CLIENTSHELL_H__
#define __CLIENTSHELL_H__

#include "ltbasedefs.h"
#include "sprite.h"
#include "netmgr.h"

class LTObject;

class CBaseConn;
class CBaseDriver;
class CPacket;
class CClientMgr;
struct HHashTable;
struct FileIdentifier;
struct StartGameRequest;
struct FileIDInfo;


// vtable 0x004c6dc0.
class CClientShell : public CNetHandler
{
public:
	virtual			~CClientShell();

	// CNetHandler (shellnet.cpp).
	virtual LTBOOL	NewConnectionNotify(CBaseConn *id, LTBOOL bIsLocal);	// 0x0048a7c0
	virtual void	DisconnectNotify(CBaseConn *id);						// 0x0048a7e0
	virtual void	HandleUnknownPacket(CPacket *pPacket, uint8 senderAddr[4], uint16 senderPort);	// 0x0048a820
	virtual void	SetDisconnectCode(uint32 nCode, char *pMsg);			// 0x0048a830


					CClientShell();

	LTBOOL			Init(CClientMgr *pClientMgr);
	void			Term();

	LTRESULT		StartupClient(CBaseDriver *pDriver);
	LTRESULT		StartupLocal(StartGameRequest *pRequest, LTBOOL bHost, CBaseDriver *pServerDriver);
	LTRESULT		CreateServerMgr();

	LTRESULT		Update();

	void			SendCommandToServer(char *pCommand);
	void			SendPacketToServer(CPacket *pPacket);

	void			RemoveAllObjects();
	void			NotifyWorldClosing();
	LTRESULT		DoLoadWorld(CPacket *pPacket, LTBOOL bLocal);
	void			CloseWorlds();
	LTBOOL			CreateVisContainerObjects();
	LTBOOL			BindWorlds();
	void			UnbindWorlds();
	FileIDInfo*		GetClientFileIDInfo(uint16 wFileID);
	class MainWorld*	GetWorld();

	// shellnet.cpp
	void			InitHandlers();			// 0x0048a850
	LTRESULT		ProcessPackets();		// 0x0048e770
	void			SendGoodbye();			// 0x0048e860

	LTObject*		GetClientObject();		// 0x0048e980

	// Lookup table for converting from 4 bits to 8 bits.
	uint8			m_ColorSignExtend[16];	// 0x04
	float			m_LastGameTime;			// 0x14

	float			m_GameTime;				// 0x18 ILTClient::GetGameTime
	float			m_GameFrameTime;		// 0x1c ILTClient::GetGameFrameTime
	float			m_ServerPeriodTrack;	// 0x20
	float			m_ServerPeriod;			// 0x24

	// On the first update from the server, this is synchronized with the server game time.
	float			m_ClientGameTime;		// 0x28
	// The current time in sync with m_ClientGameTime.
	float			m_ClientGameTimerSync;	// 0x2c
	CBaseDriver		*m_pDriver;				// 0x30 our net driver
	CBaseConn		*m_HostID;				// 0x34 the server connection
	uint16			m_ClientID;				// 0x38 our client ID on the server (0xFFFF if none)
	uint8			m_Pad3A[0x3c - 0x3a];
	LTBOOL			m_bLocal;				// 0x3c we host the server

	class CServerMgr	*m_pServerMgr;		// 0x40 the local server, if we host one
	// Objects being interpolated by the prediction code (predict.cpp).
	LTLink			m_MovingObjects;		// 0x44
	LTLink			m_RotatingObjects;		// 0x50
	LTBOOL			m_bOnServer;			// 0x5c set by NewConnectionNotify
	uint8			m_Pad60[0x6c - 0x60];
	uint16			m_ClientObjectID;		// 0x6c our client object's ID (0xFFFF if none)
	uint8			m_Pad6e[0x70 - 0x6e];
	FileIdentifier	*m_pLastWorld;			// 0x70 the world we loaded last
	uint32			m_Unknown74;			// 0x74

	class CClientMgr	*m_pClientMgr;		// 0x78
	LTBOOL			m_bWorldOpened;			// 0x7c
	uint32			m_KillTag;				// 0x80

	int				m_ShellMode;			// 0x84 STARTGAME_ mode (ILTClient::GetGameMode)
	LTObject		*m_pFrameClientObject;	// 0x88 client object the camera/listener follows
	// Server file IDs to FileIDInfo (see GetClientFileIDInfo).
	HHashTable		*m_hFileIDTable;		// 0x8c

};

// An animated world texture: a Surface whose texture is a sprite (SetPolyTexturePointers puts one
// on CClientMgr::m_SurfaceSprites; UpdateAnimations in cobject.cpp steps it). 0x20 bytes.
struct Surface;
struct SurfaceSprite
{
	struct Surface		*m_pSurface;		// 0x00
	SurfaceSprite		*m_pNext;			// 0x04
	struct Sprite		*m_pSprite;			// 0x08
	SpriteTracker		m_SpriteTracker;	// 0x0c
};

// Other frame profile counters (names unknown).
// GLOBAL: LITHTECH 0x004decf4
extern uint32 g_Ticks_FrameServer;
// GLOBAL: LITHTECH 0x004debf4
extern uint32 g_Ticks_FrameNet;
// GLOBAL: LITHTECH 0x004ded50
extern uint32 g_Ticks_FrameClientShell;
extern uint32 g_Ticks_NetUpdate;
extern uint32 g_Ticks_ServerUpdate;
extern uint32 g_Ticks_ProcessPackets;
extern uint32 g_Ticks_GameClientShell;

#endif  // __CLIENTSHELL_H__
