// Server-side client handling and server networking (Jupiter runtime/server/src/s_client.h, s_net.h).
// Talon passes the server manager explicitly. The Client structure itself is in servermgr.h.
#ifndef __S_CLIENT_H__
#define __S_CLIENT_H__

#include "servermgr.h"
#include "packet.h"

class CServerEvent;
class ModelInstance;
class CSoundTrack;

// Sound info last sent to a client for a file ID (Jupiter packetdefs.h). 10 bytes,
// allocated from CServerMgr::m_BankCAC.
struct FileIDInfo
{
	uint8		m_nChangeFlags;			// 0x00 FILEIDINFOF_
	uint16		m_wSoundPlaySoundFlags;	// 0x02
	uint8		m_nSoundPriority;		// 0x04
	uint16		m_nSoundOuterRadius;	// 0x06
	uint8		m_nSoundInnerRadius;	// 0x08
};

#define FILEIDINFOF_SOUNDPLAYSOUNDFLAGS		(1<<0)
#define FILEIDINFOF_SOUNDPRIORITY			(1<<1)
#define FILEIDINFOF_RADIUS					(1<<2)

// Links a CServerEvent into a client's event list (0x18 bytes, CServerMgr::m_ClientStructNodeBank).
struct ClientStructNode
{
	LTLink		m_mllNode;			// 0x00 in Client::m_Events (m_pData = the event)
	LTLink		m_Link;				// 0x0c in CServerEvent::m_ClientStructNodeList (m_pData = this)
};

// Server packet handlers (g_ServerHandlers, indexed by packet ID).
typedef LTRESULT (*ServerPacketHandlerFn)(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient);

// GLOBAL: LITHTECH 0x004e49f8
extern ServerPacketHandlerFn g_ServerHandlers[256];

// The local server's world (the client inherits its data in a local game; set in sm_ConnectClientToWorld).
// GLOBAL: LITHTECH 0x004deff4
extern MainWorld *g_pServerWorld;

// ------------------------------------------------------------------------ //
// s_client.cpp
// ------------------------------------------------------------------------ //
LTRESULT	sm_SendCacheListToClient(CServerMgr *pServerMgr, Client *pClient, uint32 iStart);	// 0x0046f6a0
void		sm_SendAllLightAnims(CServerMgr *pServerMgr, Client *pClient);		// 0x0046fc40
void		sm_SendChangedLightAnims(CServerMgr *pServerMgr, Client *pClient);	// 0x0046fd30
LTRESULT	sm_UpdatePuttingInWorld(CServerMgr *pServerMgr, Client *pClient);	// 0x0046fe50
LTRESULT	sm_TellClientToPreloadStuff(CServerMgr *pServerMgr, Client *pClient);	// 0x00470230
Client*		sm_OnNewConnection(CServerMgr *pServerMgr, CBaseConn *id, LTBOOL bIsLocal);	// 0x004704b0
void		sm_OnBrokenConnection(CServerMgr *pServerMgr, CBaseConn *id);		// 0x00470be0
LTRESULT	sm_AttachClient(CServerMgr *pServerMgr, Client *pParent, Client *pChild);	// 0x00470c10
LTRESULT	sm_DetachClient(CServerMgr *pServerMgr, Client *pClient);		// 0x00470db0
LTRESULT	sm_DetachClientChildren(CServerMgr *pServerMgr, Client *pClient);	// 0x00470f10
void		sm_RemoveClient(CServerMgr *pServerMgr, Client *pClient);		// 0x00470f50
LTBOOL		sm_SetClientState(CServerMgr *pServerMgr, Client *pClient, int state);	// 0x00471160
LTBOOL		sm_CanClientEnterWorld(CServerMgr *pServerMgr, Client *pClient);	// 0x004711d0
LTRESULT	sm_ConnectClientToWorld(CServerMgr *pServerMgr, Client *pClient);	// 0x00471250
void		sm_GetClientOutOfWorld(CServerMgr *pServerMgr, Client *pClient);	// 0x00471560
void		sm_UpdateClientState(CServerMgr *pServerMgr, Client *pClient);	// 0x00471610
void		sm_UpdateClientFileTransfer(CServerMgr *pServerMgr, Client *pClient);	// 0x00471660
void		sm_TracePacket(CServerMgr *pServerMgr, CPacket *pPacket);	// 0x00471680
void		sm_UpdateClientInWorld(CServerMgr *pServerMgr, Client *pClient);	// 0x00472510
Client*		sm_FindClient(CServerMgr *pServerMgr, CBaseConn *connID);		// 0x00472fe0
void		sm_SetSendSkyDef(CServerMgr *pServerMgr);						// 0x00473010
void		sm_TellClientAboutSky(CServerMgr *pServerMgr, Client *pClient);	// 0x00473040
void		sm_TellClientAboutGlobalLight(CServerMgr *pServerMgr, Client *pClient);	// 0x00473230
LTRESULT	sm_RemoveObjectFromSky(CServerMgr *pServerMgr, LTObject *pObj);	// 0x004733f0
FileIDInfo*	sm_GetClientFileIDInfo(Client *pClient, uint16 wFileID);	// 0x00473460

// ------------------------------------------------------------------------ //
// s_net.cpp
// ------------------------------------------------------------------------ //
void		sm_WriteModelFiles(LTObject *pObj, CPacket *pPacket, uint32 *pSize);	// 0x00473f90
void		WriteAnimInfo(ModelInstance *pInst, CPacket *pPacket, uint32 *pSize);	// 0x00474160
LTBOOL		FillPacketFromInfo(CServerMgr *pServerMgr, Client *pClient, LTObject *pObj, ObjInfo *pInfo,
				CPacket *pPacket, uint32 *pSize);	// 0x004745c0
void		sm_SendToAllClients(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket, uint32 packetFlags);	// 0x00475580
void		SendToClient(CServerMgr *pServerMgr, Client *pClient, uint8 msgID, CPacket *pPacket,
				LTBOOL bSendToAttachments, uint32 packetFlags);	// 0x004755d0
LTRESULT	sm_SendToClient(CServerMgr *pServerMgr, Client *pClient, uint8 msgID, CPacket *pPacket, uint32 packetFlags);	// 0x00475660
void		sm_SendToAllClientsInWorld(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket);	// 0x00475830
CServerEvent*	CreateServerEvent(CServerMgr *pServerMgr, int eventType);	// 0x00475890
void		FillSoundTrackPacketFromInfo(CServerMgr *pServerMgr, CSoundTrack *pSoundTrack, ObjInfo *pInfo,
				Client *pClient, CPacket *pPacket);	// 0x004759c0
void		GetSoundFileIDInfoFlags(FileIDInfo *pFileIDInfo, FileIDInfo *pCurrent);	// 0x004760a0
void		WriteEventToPacket(CServerMgr *pServerMgr, CServerEvent *pEvent, Client *pClient, CPacket *pPacket);	// 0x00476120
void		FillInPlaysoundMessage(CServerEvent *pEvent, Client *pClient, CPacket *pPacket);	// 0x00476140
LTBOOL		ProcessIncomingPackets(CServerMgr *pServerMgr);					// 0x00476710
void		InitServerNetHandlers();										// 0x00476800

#endif  // __S_CLIENT_H__
