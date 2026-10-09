// Jupiter runtime/client/src/cnet.cpp
// Talon's update packet carries the update rate and the command changes; it is only sent when
// something changed (or sounds are queued), and the sound packet is appended when it fits.
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "packet.h"
#include "soundmgr.h"
#include "engine_vars.h"

#define CMSG_UPDATE			7
#define CMSG_SOUNDUPDATE	8

#define MESSAGE_GUARANTEED	(1<<7)

#define MAX_UPDATE_COMMANDS	64

#define SOUNDPACKET (m_SoundMgr.GetSoundUpdatePacket())


CPacketRef packet_Get(uint16 maxSize, uint16 cacheSize);	// 0x00469120


// FUNCTION: LITHTECH 0x00417a50
void CClientMgr::SendUpdate(CNetMgr *pNetMgr, CBaseConn *pConnID, int32 *pCommands, int nCommands)
{
	CPacketRef cPacket;
	uint8 updateRate;
	LTBOOL bRateChanged;
	uint32 nToSave;
	int i;
	uint8 commands[256];

	m_TimeSinceUpdate += m_FrameTime;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	updateRate = LTCLAMP((uint8)g_CV_UpdateRate, 2, 60);
	cPacket->WriteType(updateRate);
	bRateChanged = updateRate != m_LastUpdateRate;
	m_LastUpdateRate = updateRate;

	// Add the command changes.
	cPacket->WriteType((uint8)nCommands);
	for (i=0; i < nCommands; i++)
	{
		cPacket->WriteType((uint8)pCommands[i]);
		commands[i] = (uint8)pCommands[i];
	}

	nToSave = (nCommands < MAX_UPDATE_COMMANDS) ? nCommands : MAX_UPDATE_COMMANDS;

	// Don't send it if nothing changed.
	CPacket &soundPacket = m_SoundMgr.GetSoundUpdatePacket();
	if (bRateChanged || nCommands > MAX_UPDATE_COMMANDS || m_nLastCommands != nToSave ||
		(int)soundPacket.m_DataLen - 1 > 0 || memcmp(m_LastCommands, pCommands, nToSave) != 0)
	{
		cPacket->m_Data[0] = CMSG_UPDATE;

		// Add the sound packet (or send it on its own if it doesn't fit).
		if (!cPacket->WritePacket(&soundPacket))
		{
			SOUNDPACKET.m_Data[0] = CMSG_SOUNDUPDATE;
			pNetMgr->SendPacket(&soundPacket, pConnID, 0);
		}

		if ((int)SOUNDPACKET.m_DataLen - 1 > 0)
		{
			soundPacket.m_Pos = 1;
			soundPacket.m_DataLen = 1;
		}

		pNetMgr->SendPacket(cPacket, pConnID, MESSAGE_GUARANTEED);

		memcpy(m_LastCommands, pCommands, nToSave);
		m_nLastCommands = nToSave;
		m_TimeSinceUpdate = 0.0f;
	}
}

// Template code this object instantiated first (the out-of-line copy of the packet.h
// WriteTypeImpl<uint8> that SendUpdate calls):
// FUNCTION: LITHTECH 0x00417c20 ?WriteTypeImpl@CPacket@@QAEXE@Z
