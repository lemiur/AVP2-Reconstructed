// Talon kernel/net/netmgr.cpp. Jupiter's netmgr.cpp (runtime/kernel/net/src) is the guide for the
// driver/session plumbing; the guaranteed delivery (frames, acks, NAKs, fragments) is Talon-only.
// FLAGS: /O2 /GX-
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "bdefs.h"
#include "netmgr.h"
#include "engine_vars.h"

void DebugOut(const char *pStr, ...);
float time_GetTime();

CBaseDriver* ld_CreateDriver();
CBaseDriver* udp_CreateDriver();


// How long a connection waits before piggybacking an ack on an outgoing packet.
// GLOBAL: LITHTECH 0x004d4dd8 ?g_PiggybackAckTime@@3MA
float g_PiggybackAckTime = 0.125f;

// How long a connection waits before sending an ack on its own.
// GLOBAL: LITHTECH 0x004d4ddc ?g_AckSendTime@@3MA
float g_AckSendTime = 1.0f / 3.0f;

// Just used for the timestamp in debug output (this is a global so when debugging
// local games, the time frame is the same between client and server).
// FUNCTION: LITHTECH 0x004627c0 _$E2
// FUNCTION: LITHTECH 0x004627d0 _$E1
// GLOBAL: LITHTECH 0x004e45b0 ?g_NMTimeCounter@@3VCounter@@A
Counter g_NMTimeCounter(CSTART_MILLI);



// ------------------------------------------------------------------------ //
// Helpers
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004627e0
uint16 GetWordCRC(uint8 *pData, uint16 dataLen)
{
	uint16 crc, nWords, nBytes, i;

	crc = 0;
	nWords = dataLen >> 1;
	nBytes = dataLen - (nWords << 1);

	for(i=0; i != nWords; i++)
	{
		crc += *((uint16*)pData);
		pData += 2;
	}

	for(i=0; i != nBytes; i++)
	{
		crc += *((char*)pData);
		pData++;
	}

	return crc;
}


// Is this one of our group packets?
// FUNCTION: LITHTECH 0x00462850
LTBOOL IsGroupPacket(CPacket *pPacket)
{
	if((pPacket->m_Data[0] & PACKETID_MASK) == NETMGR_PACKETID && pPacket->m_Data[1] == NMPACKET_GROUP)
		return TRUE;

	return FALSE;
}


// ------------------------------------------------------------------------ //
// CBaseConn
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00462870
CBaseConn::CBaseConn()
{
	m_Unknown68 = 0;
	m_Unknown64 = 0;
	m_ConnFlags = 0;
}


// FUNCTION: LITHTECH 0x004628e0
CBaseConn::~CBaseConn()
{
	CNetMgr *pNetMgr;

	pNetMgr = m_pDriver->m_pNetMgr;
	pNetMgr->DeleteGPackets(&m_RecvQueue);
	m_pDriver->m_pNetMgr->DeleteGPackets(&m_SendQueue);
	m_pDriver->m_pNetMgr->DeleteGPackets(&m_ReadyQueue);

	// Remove all queued packets.
	GDeleteAndRemoveElementsOB(m_Latent, m_pDriver->m_pNetMgr->m_LatentPacketBank);

	if(g_TransportDebug > 0)
	{
		dsi_ConsolePrint("Conn %p closing - total sent: %d, resent: %d, ratio: %.2f",
			this, m_OutgoingFrame, m_nResent, (float)m_nResent / (float)m_OutgoingFrame);
	}
}

// FUNCTION: LITHTECH 0x004629f0 ?sb_Free@@YAXPAUStructBank_t@@PAX@Z


// FUNCTION: LITHTECH 0x00462a10
LTBOOL CBaseConn::IsInTrouble()
{
	if(!(m_ConnFlags & CONNFLAG_LOCAL) && m_AckWait > g_CV_AckTimeout)
	{
		if(g_CV_ShowThruput > 0)
			dsi_ConsolePrint("Ack timeout (%.3f)", m_AckWait);

		return TRUE;
	}

	return FALSE;
}


// ------------------------------------------------------------------------ //
// CNetMgr
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00462a60
CNetMgr::CNetMgr()
{
	m_pMainDriver = LTNULL;
	m_FrameTime = 0.0f;
	memset(&m_guidApp, 0, sizeof(m_guidApp));
	m_Flags = 0;
}


// FUNCTION: LITHTECH 0x00462b60
CNetMgr::~CNetMgr()
{
	Term();
}


// FUNCTION: LITHTECH 0x00462c20
LTBOOL CNetMgr::Init(char *pPlayerName)
{
	m_nGPacketBytes = 0;
	m_PlayerName[0] = 0;

	m_LatentPacketBank.SetCacheSize(256);
	m_nDroppedPackets = 0;
	m_pCurPrefix = "";

	strncpy(m_PlayerName, pPlayerName, sizeof(m_PlayerName));
	m_fLastTime = time_GetTime();

	SetAppGuid(LTNULL);
	m_pMainDriver = LTNULL;

	return TRUE;
}


// FUNCTION: LITHTECH 0x00462c90
void CNetMgr::Term()
{
	TermDrivers();

	m_PlayerName[0] = 0;
	m_pHandler = LTNULL;
	m_nGPacketBytes = 0;

	DeleteGPackets(&m_FreeGPackets);
	FreeFragmentGroups();
}


// FUNCTION: LITHTECH 0x00462cc0
LTRESULT CNetMgr::InitDrivers()
{
	TermDrivers();
	AddDriver("dplay2");
	AddDriver("internet");
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00462cf0
void CNetMgr::TermDrivers()
{
	uint32 prevSize;

	while(m_Drivers.GetSize() > 0)
	{
		prevSize = m_Drivers.GetSize();
		RemoveDriver(m_Drivers[0]);
		if(prevSize == m_Drivers.GetSize())
			break;
	}
}


// FUNCTION: LITHTECH 0x00462d20
LTRESULT CNetMgr::GetServiceList(NetService *&pListHead)
{
	uint32 i;

	pListHead = LTNULL;
	for(i=0; i < m_Drivers; i++)
	{
		m_Drivers[i]->GetServiceList(pListHead);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00462d60
LTRESULT CNetMgr::FreeServiceList(NetService *pListHead)
{
	NetService *pCur, *pNext;

	for(pCur=pListHead; pCur; pCur=pNext)
	{
		pNext = pCur->m_pNext;
		delete pCur;
	}
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00462d90
LTRESULT CNetMgr::SelectService(HNETSERVICE hService)
{
	BaseService *pService;

	pService = (BaseService*)hService;
	if(m_Drivers.FindElement(pService->m_pDriver) == BAD_INDEX)
	{
		return LT_NOTINITIALIZED;
	}
	else
	{
		if(pService->m_pDriver->SelectService(hService) == LT_OK)
		{
			m_pMainDriver = pService->m_pDriver;
			return LT_OK;
		}
		else
		{
			return LT_ERROR;
		}
	}
}


// FUNCTION: LITHTECH 0x00462e00
LTRESULT CNetMgr::GetSessionList(NetSession* &pListHead, char *pInfo)
{
	if(!m_pMainDriver)
		return LT_NOTINITIALIZED;

	return m_pMainDriver->GetSessionList(pListHead, pInfo);
}


// FUNCTION: LITHTECH 0x00462e20
LTRESULT CNetMgr::FreeSessionList(NetSession *pListHead)
{
	NetSession *pCur, *pNext;

	for(pCur=pListHead; pCur; pCur=pNext)
	{
		pNext = pCur->m_pNext;
		delete pCur;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00462e50
LTRESULT CNetMgr::GetSessionName(char *pName, uint32 bufLen)
{
	if(!m_pMainDriver)
	{
		pName[0] = 0;
		return LT_NOTINITIALIZED;
	}
	return m_pMainDriver->GetSessionName(pName, bufLen);
}


// FUNCTION: LITHTECH 0x00462e70
LTRESULT CNetMgr::SetSessionName(char *pName)
{
	if(!m_pMainDriver)
	{
		return LT_NOTINITIALIZED;
	}
	return m_pMainDriver->SetSessionName(pName);
}


// FUNCTION: LITHTECH 0x00462e90
LTRESULT CNetMgr::GetLocalIpAddress(char *pAddress, uint32 bufLen, uint16 &hostPort)
{
	if(!m_pMainDriver)
	{
		pAddress[0] = 0;
		return LT_NOTINITIALIZED;
	}
	return m_pMainDriver->GetLocalIpAddress(pAddress, bufLen, hostPort);
}


// The ack is built in cPacket itself (the original releases the old packet there and has no second CPacketRef).
// FUNCTION: LITHTECH 0x00462eb0
void CNetMgr::Update(char *pPrefix, float curTime, LTBOOL bAllowTimeout)
{
	uint32 i;
	float timeDelta;
	CBaseConn *pConn;
	GPOS pos;
	LatentPacket *pLatent;
	CPacketRef cPacket;

	m_pCurPrefix = pPrefix;

	timeDelta = curTime - m_fLastTime;
	if(timeDelta < 0.001f)
		timeDelta = 0.001f;
	else if(timeDelta > 0.3f)
		timeDelta = 0.3f;

	m_fLastTime = curTime;
	m_FrameTime = timeDelta;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	for(i=0; i < m_Connections; i++)
	{
		pConn = m_Connections[i];

		if(pConn->m_ConnFlags & CONNFLAG_FORCEDISCONNECT)
		{
			cPacket->ResetWrite();
			cPacket->m_Data[0] = NETMGR_PACKETID;
			cPacket->WriteType((uint8)NMPACKET_DISCONNECT);
			LagOrSend(cPacket, pConn, 0);
			LagOrSend(cPacket, pConn, 0);
			LagOrSend(cPacket, pConn, 0);
			Disconnect(pConn, DISCONNECTREASON_FORCED);
			i = (uint32)-1;
			continue;
		}

		// Countdown the latent packets and ship them off.
		for(pos=pConn->m_Latent.GetHeadPosition(); pos; )
		{
			pLatent = pConn->m_Latent.GetNext(pos);

			pLatent->m_SendTimeCounter += timeDelta;
			if(pLatent->m_SendTimeCounter >= g_CV_LatencySim)
			{
				ReallySendPacket(&pLatent->m_Packet, pConn);

				pConn->m_Latent.RemoveAt(pLatent);
				m_LatentPacketBank.Free(pLatent);
			}
		}

		// Update timers and do guaranteed delivery stuff...
		pConn->m_SendPPS.Update(timeDelta);
		pConn->m_SendBPS.Update(timeDelta);
		pConn->m_RecvPPS.Update(timeDelta);
		pConn->m_RecvBPS.Update(timeDelta);

		pConn->m_AckWait += timeDelta;
		pConn->m_AckTimer += timeDelta;
		pConn->m_PingTimer -= timeDelta;
		pConn->m_RecvWait += timeDelta;

		if(!(pConn->m_ConnFlags & CONNFLAG_LOCAL) && pConn->m_AckTimer > g_AckSendTime)
		{
			NetDebugOut2(pConn, 2, "Sending ack  local: %d remote: %d.", pConn->m_IncomingFrame-1, pConn->m_OutgoingFrame);

			cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			FillAckPacket(pConn, cPacket);
			LagOrSend(cPacket, pConn, 0);
		}

		if(bAllowTimeout && g_CV_AllowTimeout)
		{
			if(pConn->m_RecvWait > pConn->m_pDriver->m_Bandwidth)
			{
				--i;

				if(g_DebugLevel > 0)
					dsi_ConsolePrint("Connection dead (timed out).");

				RemoveConnFragments(pConn);
				m_pHandler->SetDisconnectCode(5, "");
				pConn->m_pDriver->Disconnect(pConn, DISCONNECTREASON_DEAD);
			}
		}
		else
		{
			pConn->m_RecvWait = 0.0f;
		}
	}

	// Update the global send rate tracker
	m_SendBPS.Update(timeDelta);

	// Output stats on stuff.
	if(g_CV_ShowConnStats)
	{
		for(i=0; i < m_Connections; i++)
		{
			pConn = m_Connections[i];

			NetDebugOut2(pConn, 0, "Conn %d Info - Ping: %.2f", i, pConn->m_Ping);

			NetDebugOut2(pConn, 0, "Conn %d Recv - Wait: %.1f, Q Size: %d, PPS: %.1f, BPS: %.1f",
				i, pConn->m_RecvWait, pConn->m_RecvQueue.GetSize(), pConn->m_RecvPPS.GetRate(), pConn->m_RecvBPS.GetRate());

			NetDebugOut2(pConn, 0, "Conn %d Send - Wait: %.1f, Q Size: %d, PPS: %.1f, BPS: %.1f",
				i, pConn->m_SendWait, pConn->m_SendQueue.GetSize(), pConn->m_SendPPS.GetRate(), pConn->m_SendBPS.GetRate());
		}
	}

	// Update the drivers.
	for(i=0; i < m_Drivers; i++)
		m_Drivers[i]->Update();
}

// FUNCTION: LITHTECH 0x004632e0 ??1CPacket@@UAE@XZ
// FUNCTION: LITHTECH 0x00463320 ?AddRef@CPacketBase@@UAEXXZ
// FUNCTION: LITHTECH 0x00463330 ?Release@CPacketBase@@UAEXXZ
// FUNCTION: LITHTECH 0x00463340 ?Free@CPacketBase@@UAEXXZ
// FUNCTION: LITHTECH 0x00463350 ??_GCPacketBase@@UAEPAXI@Z


// The receive-queue loop is `for(;;){ if(!pos) break; ...}`: as `for(pos=...; pos; )` or `while(pos)` VC6 places
// its body after the second NAK loop.
// FUNCTION: LITHTECH 0x00463370
void CNetMgr::FillAckPacket(CBaseConn *pConn, CPacket *pPacket)
{
	uint32 nNaks, frame;
	GPOS pos;
	GPacket *pGPacket;

	pPacket->ResetWrite();
	pPacket->m_Data[0] = NETMGR_PACKETID | PACKETFLAG_SEND;
	pPacket->WriteType((uint8)NMPACKET_ACK);

	// Ask for a ping every few seconds.
	if(pConn->m_PingTimer < 0.0f)
	{
		++pConn->m_PingID;
		pConn->m_PingCounter.StartMS();
		pConn->m_PingTimer = ((float)rand() / RAND_MAX) * 3.0f;

		pPacket->WriteType((uint8)1);
		pPacket->WriteType(pConn->m_PingID);
	}
	else
	{
		pPacket->WriteType((uint8)0);
	}

	// The last frame we got in order and the next one we'll send.
	pPacket->WriteType(pConn->m_IncomingFrame ? pConn->m_IncomingFrame - 1 : (uint32)-1);
	pPacket->WriteType(pConn->m_OutgoingFrame);

	// NAK the frames missing between the ones we have.
	nNaks = 0;
	frame = pConn->m_IncomingFrame;
	pos = pConn->m_RecvQueue.GetHeadPosition();
	for(;;)
	{
		if(!pos)
			break;

		pGPacket = pConn->m_RecvQueue.GetNext(pos);

		while(frame < pGPacket->m_FrameNum && nNaks < MAX_NAKS)
		{
			pPacket->WriteType(frame);
			NetDebugOut(1, "NAK sent on packet frame %d", frame);
			++frame;
			++nNaks;
		}

		++frame;
		if(nNaks >= MAX_NAKS)
		{
			pConn->m_AckTimer = 0.0f;
			return;
		}
	}

	for(; frame < pConn->m_HighestFrame; frame++)
	{
		pPacket->WriteType(frame);
		NetDebugOut(1, "NAK sent on packet frame %d", frame);

		if(++nNaks >= MAX_NAKS)
			break;
	}

	pConn->m_AckTimer = 0.0f;
}


// FUNCTION: LITHTECH 0x00463710
CBaseDriver* CNetMgr::AddDriver(char *pInfo)
{
	CBaseDriver *pDriver = LTNULL;

	if(strcmp(pInfo, "local") == 0)
	{
		pDriver = ld_CreateDriver();
	}
	else if(strcmp(pInfo, "internet") == 0)
	{
		pDriver = udp_CreateDriver();
	}

	if(pDriver)
	{
		pDriver->m_pNetMgr = this;

		if(!pDriver->Init())
		{
			delete pDriver;
			return LTNULL;
		}

		m_Drivers.Append(pDriver);
	}

	return pDriver;
}


// FUNCTION: LITHTECH 0x00463860
void CNetMgr::RemoveDriver(CBaseDriver *pDriver)
{
	uint32 index = m_Drivers.FindElement(pDriver);

	if(pDriver == m_pMainDriver)
		m_pMainDriver = LTNULL;

	delete pDriver;
	m_Drivers.Remove(index);
}


// FUNCTION: LITHTECH 0x00463980
void CNetMgr::Disconnect(CBaseConn *id, int reason)
{
	if(!id)
	{
		return;
	}

	RemoveConnFragments(id);
	NetDebugOut2(id, 1, "CNetMgr::Disconnect called, (reason %d).", reason);
	id->m_pDriver->Disconnect(id, reason);
}


// FUNCTION: LITHTECH 0x004639c0
LTBOOL CNetMgr::SendPacket(CPacket *pPacket, CBaseConn *idSendTo, uint32 packetFlags)
{
	uint8 oldID;

	if(!idSendTo)
		return FALSE;

	packetFlags &= (PACKETFLAG_GUARANTEED|PACKETFLAG_SEND);

	// Drop unguaranteed packets if the connection is backed up.
	if(!((uint8)packetFlags & PACKETFLAG_GUARANTEED) && !(idSendTo->m_ConnFlags & CONNFLAG_LOCAL))
	{
		if(idSendTo->m_RecvQueue.GetSize() > (uint32)g_CV_ConnTroubleCount2 ||
			idSendTo->m_SendQueue.GetSize() > (uint32)g_CV_ConnTroubleCount2)
		{
			return TRUE;
		}
	}

	idSendTo->m_SendWait = 0.0f;

	oldID = pPacket->m_Data[0];
	pPacket->m_Data[0] = (oldID & PACKETID_MASK) | (uint8)packetFlags;
	return LagOrSend(pPacket, idSendTo, oldID);
}


// FUNCTION: LITHTECH 0x00463a40
void CNetMgr::StartGettingPackets()
{
	m_Flags |= NETMGR_GETTINGPACKETS;
}


// FUNCTION: LITHTECH 0x00463a50
void CNetMgr::EndGettingPackets()
{
	m_Flags &= ~NETMGR_GETTINGPACKETS;
}


// FUNCTION: LITHTECH 0x00463a60
LTBOOL CNetMgr::GetPacket(CPacket *pPacket, CBaseConn **pSender)
{
	uint32 i;
	CBaseDriver *pDriver;
	CBaseConn *pConn;
	GPacket *pGPacket;
	GPOS pos;

	// Check the drivers.
	for(i=0; i < m_Drivers; i++)
	{
		pDriver = m_Drivers[i];

		while(pDriver->GetPacket(pPacket))
		{
			IncRecvCounter(pPacket->m_pSender, pPacket->m_DataLen + pDriver->GetPacketOverhead());

			pPacket->m_Pos = 1;
			if(HandleReceivedPacket(pPacket, pPacket->m_pSender, TRUE))
				return TRUE;
		}
	}

	// Hand out the packets waiting on the connections.
	for(i=0; i < m_Connections; i++)
	{
		pConn = m_Connections[i];

		for(pos=pConn->m_ReadyQueue.GetHeadPosition(); pos; )
		{
			pGPacket = pConn->m_ReadyQueue.GetNext(pos);

			memcpy(pPacket->m_Data.GetArray(), pGPacket->m_pData, pGPacket->m_DataLen);
			pPacket->m_DataLen = (uint16)pGPacket->m_DataLen;
			pPacket->m_Pos = 1;
			pPacket->m_pSender = pConn;

			pConn->m_ReadyQueue.RemoveAt(pGPacket);
			FreeGPacket(pGPacket);

			if(HandleReceivedPacket(pPacket, pConn, FALSE))
				return TRUE;
		}

		if(!(pConn->m_ConnFlags & CONNFLAG_LOCAL))
		{
			do
			{
				pGPacket = FindGPacket(&pConn->m_RecvQueue, pConn->m_IncomingFrame);
				if(!pGPacket)
					break;

				pPacket->m_pSender = pConn;
				pPacket->m_DataLen = (uint16)pGPacket->m_DataLen;
				memcpy(pPacket->m_Data.GetArray(), pGPacket->m_pData, pPacket->m_DataLen);

				NetDebugOut2(pConn, 1, "Resolving guaranteed packet %d", pConn->m_IncomingFrame);

				pPacket->m_Pos = 1;
				if(HandleReceivedPacket(pPacket, pConn, FALSE))
					return TRUE;
			} while(1);
		}
	}

	return FALSE;
}


// FUNCTION: LITHTECH 0x00463c40
void CNetMgr::ResendGuaranteed(CBaseConn *pConn)
{
	GPOS pos;
	GPacket *pGPacket;

	if(!pConn)
		return;

	for(pos=pConn->m_SendQueue.GetHeadPosition(); pos; )
	{
		pGPacket = pConn->m_SendQueue.GetNext(pos);
		SendFragmented(pGPacket->m_pData, pGPacket->m_DataLen, pGPacket->m_AllocSize - pGPacket->m_DataLen, pConn);
	}
}


// FUNCTION: LITHTECH 0x00463c90
LTBOOL CNetMgr::LagOrSend(CPacket *pPacket, CBaseConn *idSendTo, uint8 oldPacketID)
{
	LatentPacket *pLatent;
	LTBOOL bRet;

	if(g_CV_LatencySim > 0.001f)
	{
		// Add it as a latent packet on this connection
		pLatent = m_LatentPacketBank.Allocate();
		pLatent->m_Packet.Init(pPacket->m_DataLen + 50, MAX_PACKET_LEN);
		memcpy(pLatent->m_Packet.m_Data.GetArray(), pPacket->m_Data.GetArray(), pPacket->m_DataLen);
		pLatent->m_Packet.m_DataLen = pPacket->m_DataLen;
		pLatent->m_Packet.m_Pos = pPacket->m_Pos;
		idSendTo->m_Latent.AddTail(pLatent);
		pLatent->m_SendTimeCounter = 0.0f;

		pPacket->m_Data[0] = oldPacketID;
		return TRUE;
	}

	bRet = ReallySendPacket(pPacket, idSendTo);
	if(bRet)
	{
		IncSendCounter(idSendTo, pPacket->m_DataLen + idSendTo->m_pDriver->GetPacketOverhead());
	}

	pPacket->m_Data[0] = oldPacketID;
	return bRet;
}

// FUNCTION: LITHTECH 0x00463e10 ??0CPacket@@QAE@XZ
// FUNCTION: LITHTECH 0x00463ed0 ??_GCPacket@@UAEPAXI@Z


// FUNCTION: LITHTECH 0x00463f30
void CNetMgr::IncRecvCounter(CBaseConn *id, uint32 packetLen)
{
	if(!id)
		return;

	id->m_RecvPPS.Add(1.0f);
	id->m_RecvBPS.Add((float)packetLen);
}


// FUNCTION: LITHTECH 0x00463f70
void CNetMgr::IncSendCounter(CBaseConn *id, uint32 packetLen)
{
	if(!id)
		return;

	id->m_SendPPS.Add(1.0f);
	id->m_SendBPS.Add((float)packetLen);
	// Don't count the bandwidth of our local connection in the global count
	if((id->m_ConnFlags & CONNFLAG_LOCAL) == 0)
		m_SendBPS.Add((float)packetLen);
}


// FUNCTION: LITHTECH 0x00463fd0
void CNetMgr::NextIncomingFrame(CBaseConn *pConn)
{
	if(pConn)
	{
		FreeGPacketsUpTo(&pConn->m_RecvQueue, pConn->m_IncomingFrame);
		++pConn->m_IncomingFrame;
	}
}


// FUNCTION: LITHTECH 0x00463ff0
LTBOOL CNetMgr::NewConnectionNotify(CBaseConn *id)
{
	uint32 i;

	DebugOut("NewConnectionNotify\n");

	if(m_pHandler)
	{
		// Init the connection.
		id->m_HighestFrame = 0;
		id->m_IncomingFrame = 0;
		id->m_OutgoingFrame = 0;
		id->m_SendWait = 0.0f;
		id->m_RecvWait = 0.0f;
		id->m_Unknown6C = 0;
		id->m_PingTimer = 0.0f;
		id->m_Ping = 0.0f;
		for(i=0; i < 3; i++)
			id->m_PingTimes[i] = 0.0f;
		id->m_AckWait = 0.0f;
		id->m_AckTimer = 0.0f;
		id->m_nResent = 0;

		m_Connections.Append(id);

		if(m_pHandler->NewConnectionNotify(id, id->m_ConnFlags & CONNFLAG_LOCAL))
		{
			return TRUE;
		}
		else
		{
			m_Connections.Remove(m_Connections.LastI());
		}
	}

	return FALSE;
}


// FUNCTION: LITHTECH 0x004641c0
void CNetMgr::DisconnectNotify(CBaseConn *id)
{
	uint32 index = m_Connections.FindElement(id);

	DebugOut("DisconnectNotify\n");

	// Remove the connection.
	if(index != BAD_INDEX)
	{
		if(m_pHandler)
			m_pHandler->DisconnectNotify(id);

		m_Connections.Remove(index);
	}
}


// FUNCTION: LITHTECH 0x004642f0
LTBOOL CNetMgr::SendFragmented(void *pData, uint32 dataLen, uint32 spaceAfter, CBaseConn *pConn)
{
	uint32 frameNum, fragSize, start, end, i;
	LTBOOL bRet;

	if(dataLen < MAX_PACKET_LEN + 1)
		return pConn->m_pDriver->SendPacket(pData, dataLen, spaceAfter, pConn);

	frameNum = *((uint32*)&((uint8*)pData)[dataLen - 4]);
	dataLen -= 4;
	NetDebugOut2(pConn, 2, "Fragmenting packet %d", frameNum);

	CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	cPacket->m_Data[0] = NETMGR_PACKETID;

	start = 0;
	fragSize = (dataLen / MAX_PACKET_FRAGMENTS) + 1;
	bRet = TRUE;
	for(i=0; i < MAX_PACKET_FRAGMENTS; i++)
	{
		end = LTMIN(start + fragSize, dataLen);

		if(end == start)
		{
			NetDebugOut(0, "Unable to complete fragment upload!");
			break;
		}

		cPacket->ResetWrite();
		cPacket->WriteType((uint8)NMPACKET_FRAGMENT);
		cPacket->WriteType((uint8)(i | FRAGMENT_INDEXFLAG));
		cPacket->WriteType(frameNum);
		cPacket->WriteRaw(&((uint8*)pData)[start], (uint16)(end - start));

		if(!pConn->m_pDriver->SendPacket(cPacket->m_Data.GetArray(), cPacket->m_DataLen,
			cPacket->m_Data.GetSize() - cPacket->m_DataLen, pConn))
		{
			bRet = FALSE;
			break;
		}

		start = end;
	}

	return bRet;
}


// The saved packet state is a struct local: as three scalars VC6 puts them below the spilled `this` (wave 6).
// FUNCTION: LITHTECH 0x00464460
LTBOOL CNetMgr::ReallySendPacket(CPacket *pPacket, CBaseConn *idSendTo)
{
	CPacketRef cGroup, cAck;
	GPacket *pGPacket, *pCur;
	GPOS pos;
	int spaceLeft;
	LTBOOL bRet;

	// The caller's packet, restored before returning (a guaranteed packet gets its frame number appended).
	struct
	{
		CPacket	*m_pPacket;
		uint32	m_DataLen, m_Pos;
	} restore;

	restore.m_Pos = restore.m_DataLen = 0;
	restore.m_pPacket = LTNULL;

	// Guaranteed packets get their frame number at the end.
	if(pPacket->m_Data[0] & PACKETFLAG_GUARANTEED)
	{
		restore.m_pPacket = pPacket;
		restore.m_DataLen = pPacket->m_DataLen;
		restore.m_Pos = pPacket->m_Pos;
		pPacket->WriteType(idSendTo->m_OutgoingFrame);
	}

	if(pPacket->m_ErrorFlags & (PACKETERR_READOVERFLOW|PACKETERR_WRITEOVERFLOW))
	{
		if(pPacket->m_ErrorFlags & PACKETERR_READOVERFLOW)
			dsi_ConsolePrint("*** Packet read overflow, disconnecting ***");
		else
			dsi_ConsolePrint("*** Packet write overflow, disconnecting ***");

		idSendTo->m_ConnFlags |= CONNFLAG_FORCEDISCONNECT;
		pPacket->m_ErrorFlags &= ~(PACKETERR_READOVERFLOW|PACKETERR_WRITEOVERFLOW);

		if(restore.m_pPacket)
		{
			restore.m_pPacket->m_DataLen = (uint16)restore.m_DataLen;
			restore.m_pPacket->m_Pos = (uint16)restore.m_Pos;
		}
		return FALSE;
	}

	pGPacket = LTNULL;
	if(pPacket->m_Data[0] & PACKETFLAG_GUARANTEED)
	{
		// Keep it until it's acked.
		if(!(idSendTo->m_ConnFlags & CONNFLAG_LOCAL))
		{
			pGPacket = AllocGPacket(pPacket->m_DataLen);
			memcpy(pGPacket->m_pData, pPacket->m_Data.GetArray(), pPacket->m_DataLen);
			pGPacket->m_DataLen = pPacket->m_DataLen;
			pGPacket->m_FrameNum = idSendTo->m_OutgoingFrame;
			idSendTo->m_SendQueue.Append(pGPacket);	// (Append, not AddTail: InsertAfter stays out of line)
		}

		++idSendTo->m_OutgoingFrame;
	}

	if((pPacket->m_ErrorFlags & PACKETERR_FRAGMENTED) && pGPacket)
	{
		pGPacket->m_bResend = TRUE;

		if(restore.m_pPacket)
		{
			restore.m_pPacket->m_DataLen = (uint16)restore.m_DataLen;
			restore.m_pPacket->m_Pos = (uint16)restore.m_Pos;
		}
		return TRUE;
	}

	NetDebugOut2(idSendTo, 5, "Sent packet %d to connection %p (packet ID %d (%d), length %d).",
		idSendTo->m_OutgoingFrame, idSendTo, pPacket->m_Data[0] & PACKETID_MASK, pPacket->m_Data[0], pPacket->m_DataLen);

	// Send along any guaranteed packets waiting to go out.
	if((pPacket->m_ErrorFlags & PACKETERR_GROUPED) && idSendTo->m_SendQueue.GetHeadPosition())
	{
		for(pos=idSendTo->m_SendQueue.GetHeadPosition(); pos; )
		{
			pCur = idSendTo->m_SendQueue.GetNext(pos);
			if(!pCur->m_bResend)
				continue;

			spaceLeft = (int)pPacket->m_MaxSize - (int)pPacket->m_Pos - 7;
			if(spaceLeft < 0)
				spaceLeft = 0;

			if((int)(pCur->m_DataLen + 4) < spaceLeft)
			{
				if(!IsGroupPacket(pPacket))
				{
					StartGroupPacket(cGroup);
					AddToGroupPacket(cGroup, pPacket);
					pPacket = cGroup;
				}

				AddDataToGroupPacket(pPacket, pCur->m_pData, pCur->m_DataLen);
			}
			else
			{
				SendFragmented(pCur->m_pData, pCur->m_DataLen, pCur->m_AllocSize - pCur->m_DataLen, idSendTo);
			}

			pCur->m_bResend = FALSE;
		}
	}

	// Piggyback an ack if it is time.
	if(idSendTo->m_AckTimer > g_PiggybackAckTime && !(m_Flags & NETMGR_GETTINGPACKETS) &&
		!(idSendTo->m_ConnFlags & CONNFLAG_LOCAL))
	{
		spaceLeft = (int)pPacket->m_MaxSize - (int)pPacket->m_Pos - 7;
		if(spaceLeft >= 0 && spaceLeft >= 44)
		{
			NetDebugOut2(idSendTo, 2, "Piggybacking ack local: %d, remote: %d.", idSendTo->m_OutgoingFrame, idSendTo->m_IncomingFrame);

			cAck = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			FillAckPacket(idSendTo, cAck);

			if(IsGroupPacket(pPacket))
			{
				AddToGroupPacket(pPacket, cAck);
			}
			else
			{
				StartGroupPacket(cGroup);
				AddToGroupPacket(cGroup, pPacket);
				AddToGroupPacket(cGroup, cAck);
				pPacket = cGroup;
			}
		}
	}

	if(!(idSendTo->m_ConnFlags & CONNFLAG_LOCAL) && (pPacket->m_Data[0] & PACKETFLAG_GUARANTEED))
	{
		bRet = SendFragmented(pPacket->m_Data.GetArray(), pPacket->m_DataLen,
			pPacket->m_Data.GetSize() - pPacket->m_DataLen, idSendTo);

		if(restore.m_pPacket)
		{
			restore.m_pPacket->m_DataLen = (uint16)restore.m_DataLen;
			restore.m_pPacket->m_Pos = (uint16)restore.m_Pos;
		}

		return bRet;
	}
	else
	{
		bRet = idSendTo->m_pDriver->SendPacket(pPacket->m_Data.GetArray(), pPacket->m_DataLen,
			pPacket->m_Data.GetSize() - pPacket->m_DataLen, idSendTo);

		if(restore.m_pPacket)
		{
			restore.m_pPacket->m_DataLen = (uint16)restore.m_DataLen;
			restore.m_pPacket->m_Pos = (uint16)restore.m_Pos;
		}

		return bRet;
	}
}


// HandleNetMgrPacket takes (pSender, pPacket). Each branch strips its own trailer (VC6 cross-jumps the two
// identical tails, which is why the guaranteed branch jumps into the middle of the other one), and the statement
// after the if/else (unreachable) makes VC6 merge the `return FALSE` blocks into the last copy.
// FUNCTION: LITHTECH 0x00464870
LTBOOL CNetMgr::HandleReceivedPacket(CPacket *pPacket, CBaseConn *pSender, LTBOOL bMaybeDrop)
{
	uint32 frame;
	uint16 dataLen, trailer;
	GPacket *pGPacket;

	if(g_CV_DropRate > 0.0001f && bMaybeDrop)
	{
		if(pSender->m_ConnFlags & CONNFLAG_LOCAL)
		{
			dsi_ConsolePrint("Warning: using DropRate on local connection");
			dsi_ConsolePrint("         MUST use ForceRemote in addition");
		}

		// Ok, maybe drop the packet.
		if(((float)rand() / RAND_MAX) * 100.0f < g_CV_DropRate)
		{
			++m_nDroppedPackets;

			frame = (uint32)-1;
			if(pPacket->m_Data[0] & PACKETFLAG_GUARANTEED)
				frame = *((uint32*)&pPacket->m_Data[pPacket->m_DataLen - 4]);

			NetDebugOut2(pSender, 1, "Dropping packet %lu", frame);
			return FALSE;
		}
	}

	pSender->m_RecvWait = 0.0f;

	if(pSender->m_ConnFlags & CONNFLAG_LOCAL)
	{
		++pSender->m_nPacketsReceived;
		NetDebugOut2(pSender, 4, "Got packet %d from connection %p (packet ID %d, length %d).",
			pSender->m_nPacketsReceived, pSender, pPacket->m_Data[0] & PACKETID_MASK, pPacket->m_DataLen);

		if(HandleUnknownPacket(pSender, pPacket) || HandleNetMgrPacket(pSender, pPacket))
			return FALSE;

		if(pPacket->m_Data[0] & PACKETFLAG_GUARANTEED)
			pPacket->m_DataLen -= 4;

		return TRUE;
	}

	if(pSender->m_ConnFlags & CONNFLAG_CRC)
	{
		dataLen = pPacket->m_DataLen;
		if(dataLen < 2)
			return FALSE;

		if(*((uint16*)&pPacket->m_Data[dataLen - 2]) != GetWordCRC(pPacket->m_Data.GetArray(), dataLen - 2))
		{
			NetDebugOut2(pSender, 1, "Bad CRC packet .. first byte %d.", (char)pPacket->m_Data[0]);
			return FALSE;
		}
	}

	if(HandleNetMgrPacket(pSender, pPacket))
		return FALSE;

	if(pPacket->m_Data[0] & PACKETFLAG_GUARANTEED)
	{
		trailer = 4;
		if(pSender->m_ConnFlags & CONNFLAG_CRC)
			trailer = 6;
		if(pPacket->m_DataLen < trailer)
			return FALSE;

		frame = *((uint32*)&pPacket->m_Data[pPacket->m_DataLen - trailer]);
		if(frame == pSender->m_IncomingFrame)
		{
			NetDebugOut2(pSender, 2, "Received guaranteed packet %d", frame);
			NextIncomingFrame(pSender);
		}
		else if(frame < pSender->m_IncomingFrame)
		{
			NetDebugOut2(pSender, 1, "Ignoring duplicate packet %d/%d", frame, pSender->m_IncomingFrame);
			return FALSE;
		}
		else if(frame > pSender->m_IncomingFrame)
		{
			if(frame > pSender->m_IncomingFrame + 256)
				NetDebugOut2(pSender, 1, "Warning! Suspicious frame count %d on msg ID %d", frame, pPacket->m_Data[0]);

			if(FindGPacket(&pSender->m_RecvQueue, frame))
			{
				NetDebugOut2(pSender, 2, "Received duplicate out of order packet %d", frame);
				return FALSE;
			}

			// Hold onto it until we get the frames before it.
			NetDebugOut2(pSender, 2, "Received out of order packet %d", frame);
			pGPacket = AllocGPacket(pPacket->m_DataLen);
			pGPacket->m_DataLen = pPacket->m_DataLen;
			memcpy(pGPacket->m_pData, pPacket->m_Data.GetArray(), pPacket->m_DataLen);
			pGPacket->m_FrameNum = frame;
			if(!InsertGPacket(&pSender->m_RecvQueue, pGPacket))
				FreeGPacket(pGPacket);

			pSender->m_AckTimer += g_AckSendTime * 0.5f;
			return FALSE;
		}

		NetDebugOut2(pSender, 4, "Got packet %d from %p (packet ID %d (%d), length %d).",
			pSender->m_IncomingFrame, pSender, pPacket->m_Data[0] & PACKETID_MASK, pPacket->m_Data[0], pPacket->m_DataLen);

		if(HandleUnknownPacket(pSender, pPacket))
			return FALSE;

		trailer = 4;
		if(pSender->m_ConnFlags & CONNFLAG_CRC)
			trailer = 6;

		if(pPacket->m_DataLen < trailer)
			return FALSE;

		pPacket->m_DataLen -= trailer;
		++pSender->m_nPacketsReceived;
		return TRUE;
	}
	else
	{
		NetDebugOut2(pSender, 4, "Got packet from %p (packet ID %d, length %d).",
			pSender, pPacket->m_Data[0] & PACKETID_MASK, pPacket->m_DataLen);

		trailer = 0;
		if(pSender->m_ConnFlags & CONNFLAG_CRC)
			trailer = 2;

		if(pPacket->m_DataLen < trailer)
			return FALSE;

		pPacket->m_DataLen -= trailer;
		++pSender->m_nPacketsReceived;
		return TRUE;
	}

	return FALSE;
}


// FUNCTION: LITHTECH 0x00464c30
CFragmentGroup* CNetMgr::FindFragmentGroup(uint32 frameNum, CBaseConn *pConn)
{
	GPOS pos;
	CFragmentGroup *pGroup;

	for(pos=m_FragmentGroups.GetHeadPosition(); pos; )
	{
		pGroup = m_FragmentGroups.GetNext(pos);
		if(pGroup->m_FrameNum == frameNum && pGroup->m_pConn == pConn)
			return pGroup;
	}

	return LTNULL;
}


// Stores the packet ID byte. An inline setter in the original (packet.h is frozen, so it lives here): the packet
// pointer is evaluated before the ReadType call and the m_Data pointer after it.
inline void SetPacketID(CPacket *pPacket, uint8 id)
{
	pPacket->m_Data[0] = id;
}

// FUNCTION: LITHTECH 0x00464c70
LTBOOL CNetMgr::AddFragment(CPacket *pPacket, uint8 index, CFragmentGroup *pGroup)
{
	CPacketRef *pFragment;

	pFragment = &pGroup->m_Fragments[index];
	if(pFragment->m_pPacket)
		return FALSE;

	*pFragment = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// The first fragment has the original packet ID.
	if(index == 0)
		SetPacketID(pGroup->m_Fragments[0], pPacket->ReadType((uint8*)0));

	(*pFragment)->ResetWrite();
	(*pFragment)->WriteRaw(&pPacket->m_Data[pPacket->m_Pos], pPacket->m_DataLen - pPacket->m_Pos);
	return TRUE;
}


// FUNCTION: LITHTECH 0x00464d30
LTBOOL CNetMgr::IsFragmentGroupComplete(CFragmentGroup *pGroup)
{
	uint32 i;

	for(i=0; i < MAX_PACKET_FRAGMENTS; i++)
	{
		if(!pGroup->m_Fragments[i].m_pPacket)
			return FALSE;
	}

	return TRUE;
}


// FUNCTION: LITHTECH 0x00464d60
CFragmentGroup* CNetMgr::AllocFragmentGroup()
{
	GPOS pos;
	uint32 count;
	CFragmentGroup *pGroup;

	count = 0;
	for(pos=m_FragmentGroups.GetHeadPosition(); pos; )
	{
		m_FragmentGroups.GetNext(pos);
		++count;
	}

	if(count > 5)
	{
		// Reuse the oldest one.
		pGroup = m_FragmentGroups.RemoveTail();
		pGroup->Clear();
	}
	else
	{
		pGroup = new CFragmentGroup;
	}

	m_FragmentGroups.AddHead(pGroup);
	return pGroup;
}


// FUNCTION: LITHTECH 0x00464eb0
void CNetMgr::FreeFragmentGroup(CFragmentGroup *pGroup)
{
	if(pGroup)
	{
		m_FragmentGroups.RemoveAt(pGroup);
		delete pGroup;
	}
}


// FUNCTION: LITHTECH 0x00464f20
void CNetMgr::FreeFragmentGroups()
{
	while(m_FragmentGroups.GetHeadPosition())
	{
		FreeFragmentGroup(m_FragmentGroups.GetHead());
	}
}


// FUNCTION: LITHTECH 0x00464f50
void CNetMgr::RemoveConnFragments(CBaseConn *pConn)
{
	GPOS pos;
	CFragmentGroup *pGroup;

	for(pos=m_FragmentGroups.GetHeadPosition(); pos; )
	{
		pGroup = m_FragmentGroups.GetNext(pos);
		if(pGroup->m_pConn)
		{
			if(pGroup->m_pConn == pConn || pGroup->m_FrameNum < pGroup->m_pConn->m_IncomingFrame)
				FreeFragmentGroup(pGroup);
		}
	}
}


// Bytes left to read in a packet: an inline helper in the original (each use is one more pending inline site
// in HandleNetMgrPacket). Its real name is unknown and packet.h is frozen, so it lives here.
inline uint32 GetPacketBytesLeft(CPacket *pPacket)
{
	return pPacket->m_DataLen - pPacket->m_Pos;
}

// Wave 6: matching size and inlining (1584 bytes, aligned 69): the parameters are (pSender, pPacket) (the
// exe pushes them in that order; HandleReceivedPacket too), naks[] has 16 slots (0x4c frame), cReply stays at
// function scope (its constructor's zero store is hoisted to the top and VC6 folds its destructor away on every
// path that never assigns it, but each destructor is still a pending inline site), the ping history shifts with
// an inline memcpy, and the bytes-left tests go through an inline helper (three more pending sites; without it
// PINGREPLY/GROUP/FRAGMENT inline ReadType where the original calls it). Left: x87 order of `m_Ping +=
// m_PingTimes[i]` (the original loads m_PingTimes[i] and adds m_Ping from memory, as if m_Ping were referenced
// first; memmove instead of memcpy fixes it but is a call), the memcpy's load order, register naming in the
// DISCONNECT/GROUP blocks and the FRAGMENT `Invalid fragment count` return, which VC6 cross-jumps into the final
// else's NetDebugOut2 call where the original keeps its own tail. Tried: a switch (85), the final else as a
// trailing statement (changes inlining), int/int32/uint16 helper return types (worse).
// Wave 7: the function sits on an inline-budget edge: any added statement (a trailing `return`, an empty `else {}`,
// a `pPingTimes` local, `m_Ping = t + m_Ping`) inlines the FRAGMENT fragInfo ReadType (1680 bytes), and extra pending
// sites after it (GetPacketID, WriteData) don't undo that. None of those shapes stops the cross-jump either. Toys:
// VC6 merges the two `NetDebugOut2; return TRUE` blocks for this if-chain shape (also with an inline helper, a bRet
// local, a nested if or do/while(0)); a switch, or the FRAGMENT test written success-first (`if(ok){...} err`),
// keeps them apart, but the first changes the dispatch code and the second puts the error block after the body.
// The ping copy as a two-float struct assignment is 67 (memcpy 69).
// Wave 7 phase 2: inline_budget: our out-of-line calls equal the exe's (the model alone predicts one fewer
// ReadTypeImpl). 69 aligned: x87 order, memcpy load order, register naming and the FRAGMENT cross-jump.
// PARKED: register/x87 order and one cross-jumped error tail; inlining and behaviour identical
// STUB: LITHTECH 0x00464fb0
LTBOOL CNetMgr::HandleNetMgrPacket(CBaseConn *pSender, CPacket *pPacket)
{
	CPacketRef cReply;
	uint8 subID, fragInfo, fragIndex;
	uint16 pingID, subLen;
	uint32 ackFrame, naks[MAX_NAKS*2], nNaks, i, nPings, frameNum;
	GPacket *pGPacket;
	CFragmentGroup *pGroup;

	if((pPacket->m_Data[0] & PACKETID_MASK) != NETMGR_PACKETID)
		return FALSE;

	subID = pPacket->ReadType((uint8*)0);
	if(subID == NMPACKET_ACK)
	{
		pSender->m_AckWait = 0.0f;

		// Ping request?
		if(pPacket->ReadType((uint8*)0))
		{
			pingID = pPacket->ReadType((uint16*)0);

			cReply = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			cReply->ResetWrite();
			cReply->m_Data[0] = NETMGR_PACKETID;
			cReply->WriteType((uint8)NMPACKET_PINGREPLY);
			cReply->WriteType(pingID);
			SendPacket(cReply, pSender, 0);
		}

		ackFrame = pPacket->ReadType((uint32*)0);
		pSender->m_HighestFrame = pPacket->ReadType((uint32*)0);
		FreeGPacketsAbove(&pSender->m_RecvQueue, pSender->m_HighestFrame);

		nNaks = 0;
		while(GetPacketBytesLeft(pPacket) >= 4)
		{
			naks[nNaks] = pPacket->ReadType((uint32*)0);
			if(++nNaks >= MAX_NAKS)
				break;
		}

		if(ackFrame != (uint32)-1)
		{
			FreeGPacketsUpTo(&pSender->m_SendQueue, ackFrame);
			NetDebugOut2(pSender, 2, "Got an ack on local %d, remote %d", ackFrame, pSender->m_HighestFrame);
		}

		for(i=0; i < nNaks; i++)
		{
			pGPacket = FindGPacket(&pSender->m_SendQueue, naks[i]);
			if(pGPacket)
			{
				if((pGPacket->m_nNaks + i) % 5 == 0)
				{
					NetDebugOut2(pSender, 1, "Resending frame %d", naks[i]);
					SendFragmented(pGPacket->m_pData, pGPacket->m_DataLen, pGPacket->m_AllocSize - pGPacket->m_DataLen, pSender);
				}

				++pGPacket->m_nNaks;
			}
			else
			{
				NetDebugOut(1, "Error in SUB_ACK: missing requested packet %d", naks[i]);
			}
		}

		return TRUE;
	}
	else if(subID == NMPACKET_DISCONNECT)
	{
		NetDebugOut2(pSender, 1, "Forced disconnect!");
		RemoveConnFragments(pSender);
		pSender->m_pDriver->Disconnect(pSender, 4);
		m_pHandler->SetDisconnectCode(4, "");
		return TRUE;
	}
	else if(subID == NMPACKET_PINGREPLY)
	{
		pingID = pPacket->ReadType((uint16*)0);
		if(pingID != pSender->m_PingID)
			return TRUE;

		memcpy(&pSender->m_PingTimes[0], &pSender->m_PingTimes[1], sizeof(float) * 2);
		pSender->m_PingTimes[2] = (float)pSender->m_PingCounter.EndMS() * 0.001f;

		nPings = 0;
		for(i=0; i < 3; i++)
		{
			pSender->m_Ping += pSender->m_PingTimes[i];
			if(pSender->m_PingTimes[i] > 0.0f)
				++nPings;
		}

		if(nPings)
			pSender->m_Ping /= (float)nPings;

		return TRUE;
	}
	else if(subID == NMPACKET_GROUP)
	{
		while(GetPacketBytesLeft(pPacket) >= 2)
		{
			subLen = pPacket->ReadType((uint16*)0);
			if((int)subLen <= (int)GetPacketBytesLeft(pPacket))
			{
				pGPacket = AllocGPacket(subLen);
				pPacket->ReadRaw(pGPacket->m_pData, subLen);
				pGPacket->m_DataLen = subLen;
				pSender->m_ReadyQueue.AddTail(pGPacket);
			}
			else
			{
				NetDebugOut(0, "Invalid PACKETGROUP subpacket length");
			}
		}

		return TRUE;
	}
	else if(subID == NMPACKET_FRAGMENT)
	{
		RemoveConnFragments(LTNULL);

		fragInfo = pPacket->ReadType((uint8*)0);
		if((fragInfo & 0xf0) != FRAGMENT_INDEXFLAG)
		{
			NetDebugOut2(pSender, 0, "Invalid fragment count received!");
			return TRUE;
		}

		fragIndex = fragInfo & 0xf;
		frameNum = pPacket->ReadType((uint32*)0);

		pGroup = FindFragmentGroup(frameNum, pSender);
		if(!pGroup)
		{
			pGroup = AllocFragmentGroup();
			pGroup->m_FrameNum = frameNum;
			pGroup->m_pConn = pSender;
		}

		AddFragment(pPacket, fragIndex, pGroup);
		if(!IsFragmentGroupComplete(pGroup))
			return TRUE;

		// Put the packet back together.
		NetDebugOut2(pSender, 2, "Completed fragmented packet %d", pGroup->m_FrameNum);

		pPacket->ResetWrite();
		pPacket->m_Data[0] = pGroup->m_Fragments[0]->m_Data[0];
		for(i=0; i < MAX_PACKET_FRAGMENTS; i++)
		{
			pPacket->WriteRaw(&pGroup->m_Fragments[i]->m_Data[1], pGroup->m_Fragments[i]->m_DataLen - 1);
		}

		pPacket->WriteType(frameNum);
		pPacket->m_Pos = 1;
		FreeFragmentGroup(pGroup);
		return FALSE;
	}
	else
	{
		NetDebugOut2(pSender, 1, "Got an invalid internal packet!");
		return TRUE;
	}
}


// FUNCTION: LITHTECH 0x004655e0
LTBOOL CNetMgr::HandleUnknownPacket(CBaseConn *pSender, CPacket *pPacket)
{
	return FALSE;
}


// FUNCTION: LITHTECH 0x004655f0
void CNetMgr::FreeGPacketsUpTo(GPacketList *pList, uint32 frameNum)
{
	GPOS pos;
	GPacket *pGPacket;

	for(pos=pList->GetHeadPosition(); pos; )
	{
		pGPacket = pList->GetNext(pos);
		if(pGPacket->m_FrameNum > frameNum)
			return;

		pList->RemoveAt(pGPacket);
		FreeGPacket(pGPacket);
	}
}


// FUNCTION: LITHTECH 0x00465670
void CNetMgr::FreeGPacketsAbove(GPacketList *pList, uint32 frameNum)
{
	GPOS pos;
	GPacket *pGPacket;

	for(pos=pList->GetHeadPosition(); pos; )
	{
		pGPacket = pList->GetNext(pos);
		if(pGPacket->m_FrameNum > frameNum)
		{
			pList->RemoveAt(pGPacket);
			FreeGPacket(pGPacket);
		}
	}
}


// FUNCTION: LITHTECH 0x004656f0
LTBOOL CNetMgr::InsertGPacket(GPacketList *pList, GPacket *pPacket)
{
	GPOS pos;
	GPacket *pCur;
	int count;

	count = 0;
	for(pos=pList->GetHeadPosition(); pos; )
	{
		if(count >= g_CV_NetMaxQueue)
			break;

		pCur = pList->GetNext(pos);
		++count;

		if(pPacket->m_FrameNum < pCur->m_FrameNum)
		{
			pList->InsertBefore(pCur, pPacket);
			return TRUE;
		}
	}

	if(count >= g_CV_NetMaxQueue)
	{
		NetDebugOut(1, "Dropped guaranteed packet due to queue overflow");
		return FALSE;
	}

	pList->AddTail(pPacket);
	return TRUE;
}


// FUNCTION: LITHTECH 0x004657d0
void CNetMgr::FreeGPacket(GPacket *pPacket)
{
	GPOS pos;
	GPacket *pCur;

	pPacket->m_DataLen = 0;

	for(pos=m_FreeGPackets.GetHeadPosition(); pos; )
	{
		pCur = m_FreeGPackets.GetNext(pos);
		if(pPacket->m_AllocSize >= pCur->m_AllocSize)
			break;
	}

	if(pos)
		m_FreeGPackets.InsertBefore(pCur, pPacket);
	else
		m_FreeGPackets.AddTail(pPacket);
}


// FUNCTION: LITHTECH 0x00465890
GPacket* CNetMgr::FindGPacket(GPacketList *pList, uint32 frameNum)
{
	GPOS pos;
	GPacket *pGPacket;

	for(pos=pList->GetHeadPosition(); pos; )
	{
		pGPacket = pList->GetNext(pos);
		if(pGPacket->m_FrameNum == frameNum)
			return pGPacket;
		else if(pGPacket->m_FrameNum > frameNum)
			break;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x004658d0
GPacket* CNetMgr::AllocGPacket(uint32 size)
{
	GPOS pos;
	GPacket *pGPacket;

	size += 2;

	for(pos=m_FreeGPackets.GetHeadPosition(); pos; )
	{
		pGPacket = m_FreeGPackets.GetNext(pos);
		if(pGPacket->m_AllocSize >= (int)size)
		{
			m_FreeGPackets.RemoveAt(pGPacket);
			pGPacket->m_nNaks = 0;
			pGPacket->m_bResend = 0;
			return pGPacket;
		}
	}

	pGPacket = new GPacket;
	memset(pGPacket, 0, sizeof(GPacket));
	pGPacket->m_pData = new uint8[size];
	pGPacket->m_AllocSize = size;
	m_nGPacketBytes += size;
	return pGPacket;
}


// FUNCTION: LITHTECH 0x00465990
void CNetMgr::DeleteGPackets(GPacketList *pList)
{
	GPOS pos;
	GPacket *pGPacket;

	for(pos=pList->GetHeadPosition(); pos; )
	{
		pGPacket = pList->GetNext(pos);
		if(pGPacket->m_pData)
		{
			delete [] pGPacket->m_pData;
			m_nGPacketBytes -= pGPacket->m_AllocSize;
		}

		delete pGPacket;
	}

	pList->RemoveAll();
}


// WriteRaw is an inline wrapper around WriteData (packet.h): its pending site keeps CMoArray::Insert2 out of line.
// FUNCTION: LITHTECH 0x00465b20
void CNetMgr::AddDataToGroupPacket(CPacket *pGroup, void *pData, uint32 dataLen)
{
	pGroup->WriteType((uint16)dataLen);
	pGroup->WriteRaw(pData, (uint16)dataLen);
}


// FUNCTION: LITHTECH 0x00465a00
void CNetMgr::StartGroupPacket(CPacketRef &cPacket)
{
	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	cPacket->m_Data[0] = NETMGR_PACKETID;
	cPacket->WriteType((uint8)NMPACKET_GROUP);
}


// FUNCTION: LITHTECH 0x00465b00
void CNetMgr::AddToGroupPacket(CPacket *pGroup, CPacket *pPacket)
{
	AddDataToGroupPacket(pGroup, pPacket->m_Data.GetArray(), pPacket->m_DataLen);
}


// FUNCTION: LITHTECH 0x00465bf0
void CNetMgr::SetAppGuid(LTGUID* pAppGuid)
{
	if(pAppGuid)
	{
		memcpy(&m_guidApp, pAppGuid, sizeof(m_guidApp));
	}
	else
	{
		memset(&m_guidApp, 0, sizeof(m_guidApp));
	}
}


// FUNCTION: LITHTECH 0x00465c30
CBaseDriver* CNetMgr::GetDriver(char* sDriver)
{
	uint32 i;
	CBaseDriver *pDriver;

	for(i=0; i < m_Drivers.GetSize(); i++)
	{
		pDriver = m_Drivers[i];
		if(pDriver)
		{
			if(stricmp(pDriver->m_Name, sDriver) == 0)
			{
				return pDriver;
			}
		}
	}

	return LTNULL;
}


// Helper for debugging.
// FUNCTION: LITHTECH 0x00465c80
void CNetMgr::NetDebugOut(int debugLevel, char *pMsg, ...)
{
	char msg[500];
	va_list marker;

	if(debugLevel > g_TransportDebug)
		return;

	va_start(marker, pMsg);
	_vsnprintf(msg, sizeof(msg)-1, pMsg, marker);
	va_end(marker);

	dsi_ConsolePrint("(%d) %s%s", g_NMTimeCounter.CountMS(), m_pCurPrefix, msg);
}


// FUNCTION: LITHTECH 0x00465cf0
void CNetMgr::NetDebugOut2(CBaseConn *pConn, int debugLevel, char *pMsg, ...)
{
	char msg[500];
	va_list marker;

	if(debugLevel > g_TransportDebug)
		return;

	if(!g_bLocalDebug && ((pConn->m_ConnFlags & CONNFLAG_LOCAL) != 0))
		return;

	va_start(marker, pMsg);
	_vsnprintf(msg, sizeof(msg)-1, pMsg, marker);
	va_end(marker);

	dsi_ConsolePrint("(%d) %s%s", g_NMTimeCounter.CountMS(), m_pCurPrefix, msg);
}


// Template and inline code this object instantiated first:
// FUNCTION: LITHTECH 0x00465d70 ?GenBegin@?$CGLinkedList@PAVGPacket@@@@UBE?AVGenListPos@@XZ
// FUNCTION: LITHTECH 0x00465d80 ?GenIsValid@?$CGLinkedList@PAVGPacket@@@@UBEHABVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00465d90 ?GenGetAt@?$CGLinkedList@PAVGPacket@@@@UBEPAVGPacket@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00465da0 ?GenAppend@?$CGLinkedList@PAVGPacket@@@@UAEHAAPAVGPacket@@@Z
// FUNCTION: LITHTECH 0x00465df0 ?GenRemoveAll@?$CGLinkedList@PAVGPacket@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00465e00 ?GenGetSize@?$CGLinkedList@PAVGPacket@@@@UBEKXZ
// FUNCTION: LITHTECH 0x00465e10 ?GenCopyList@?$CGLinkedList@PAVGPacket@@@@UAEHABV?$GenList@PAVGPacket@@@@@Z
// FUNCTION: LITHTECH 0x00465e20 ?GenAppendList@?$CGLinkedList@PAVGPacket@@@@UAEHABV?$GenList@PAVGPacket@@@@@Z
// FUNCTION: LITHTECH 0x00465eb0 ?AllocVoid@?$ObjectBank@VLatentPacket@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00465f40 ?Init@CPacket@@QAEXGG@Z
// FUNCTION: LITHTECH 0x00466010 ?FreeVoid@?$ObjectBank@VLatentPacket@@VNullCS@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00466050 ?GenCopyList@?$CMoArray@PAVCBaseConn@@VDefaultCache@@@@UAEHABV?$GenList@PAVCBaseConn@@@@@Z
// FUNCTION: LITHTECH 0x00466170 ?GenAppend@?$CMoArray@PAVCBaseConn@@VDefaultCache@@@@UAEHAAPAVCBaseConn@@@Z
// FUNCTION: LITHTECH 0x004662f0 ?Term@?$ObjectBank@VLatentPacket@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00466310 ??_G?$ObjectBank@VLatentPacket@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00466350 ??_GLatentPacket@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x004663b0 ?AddHead@?$CGLinkedList@PAVLatentPacket@@@@QAEPAVCGLLNode@@PAVLatentPacket@@@Z
// FUNCTION: LITHTECH 0x00466400 ?InsertAfter@?$CGLinkedList@PAVGPacket@@@@QAEPAVCGLLNode@@PAV2@PAVGPacket@@@Z
// The body of CPacket::ReadType<uint8> (see packet.h), refused by HandleNetMgrPacket and others.
// FUNCTION: LITHTECH 0x00466270 ?ReadTypeImpl@CPacket@@QAEEPAE@Z
// FUNCTION: LITHTECH 0x00466430 ?Insert2@?$CMoArray@PAVCBaseConn@@VDefaultCache@@@@QAEHKABQAVCBaseConn@@PAVLAlloc@@@Z
