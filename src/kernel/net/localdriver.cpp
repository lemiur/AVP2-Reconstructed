// Talon kernel/net/localdriver.cpp (Jupiter runtime/kernel/net/src/localdriver.cpp is a rewrite).
// This object was built without optimization (frame pointers, int3 padding).
// FLAGS: /Od /Ob1 /Oi /Gy /Gf /GX-
#include <string.h>
#include "localdriver.h"
#include "packet.h"
#include "engine_vars.h"

void DebugOut(const char *pStr, ...);




// FUNCTION: LITHTECH 0x004465d0
CBaseDriver* ld_CreateDriver()
{
	return new CLocalDriver;
}

// FUNCTION: LITHTECH 0x00446610
CLocalPacketHolder::CLocalPacketHolder()
{
	m_pNextFree = LTNULL;
	m_Flags = 0;
	m_pData = LTNULL;
}

// FUNCTION: LITHTECH 0x00446640
CLocalPacketHolder::~CLocalPacketHolder()
{
	if(m_pData)
	{
		delete m_pData;
		m_pData = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x00446680
CLocalDriver::CLocalDriver()
{
	m_Waiting.m_pPrev = m_Waiting.m_pNext = &m_Waiting;

	memset(&m_Holders, 0, sizeof(m_Holders));
	m_Holders.m_pNextFree = &m_Holders;
	m_Holders.m_DataLen = -1;
	m_Holders.m_AllocSize = -1;
	m_Holders.m_Flags = 2;

	m_nPackets = 0;
	m_nWaiting = 0;
	m_nAllocatedBytes = 0;

	m_pConnection = LTNULL;
	m_pBaseConn = LTNULL;

	m_Bandwidth = 1000000.0f;
	m_bPendingConnection = LTFALSE;
}

// GetPacketOverhead is inline in the header (emitted with the vtable, before the deleting destructor).
// FUNCTION: LITHTECH 0x00446780 ?GetPacketOverhead@CLocalDriver@@UAEKXZ

// FUNCTION: LITHTECH 0x00446790 ??_GCLocalDriver@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004467c0
CLocalDriver::~CLocalDriver()
{
	Term();
}

// FUNCTION: LITHTECH 0x00446800
LTBOOL CLocalDriver::Init()
{
	strcpy(m_Name, "local");
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00446850
void CLocalDriver::Term()
{
	CLocalPacketHolder *pCur;

	// Free all the packet holders.
	pCur = m_Holders.m_pNextFree;
	while(pCur != &m_Holders)
	{
		CLocalPacketHolder *pHolder = pCur;
		pCur = pCur->m_pNextFree;

		m_nAllocatedBytes -= pHolder->m_AllocSize;
		delete pHolder;
	}

	m_Holders.m_pNextFree = &m_Holders;
	m_Waiting.m_pPrev = m_Waiting.m_pNext = &m_Waiting;
	m_nPackets = 0;
	m_nWaiting = 0;

	// Disconnect from the connection (if any).
	if(m_pConnection)
	{
		// Must have m_pConnection LTNULL before telling it to disconnect or it'll
		// get into infinite recursion.
		CLocalDriver *pConn = m_pConnection;
		m_pConnection = LTNULL;

		pConn->DisconnectFromMe();
	}

	if(m_pBaseConn)
	{
		delete m_pBaseConn;
		m_pBaseConn = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x004469a0
void CLocalDriver::Update()
{
	if(m_bPendingConnection)
	{
		m_bPendingConnection = LTFALSE;

		if(!g_bForceRemote)
		{
			m_pBaseConn->m_ConnFlags = CONNFLAG_LOCAL;
		}

		if(m_pNetMgr->NewConnectionNotify(m_pBaseConn))
			return;

		delete m_pBaseConn;
		m_pBaseConn = LTNULL;
		m_pConnection = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x00446a50
void CLocalDriver::LocalConnect(CBaseDriver *pBaseDriver)
{
	CLocalDriver *pConn;

	pConn = (CLocalDriver*)pBaseDriver;

	pConn->ConnectToMe(this);
	DoConnection(pConn);
}

// FUNCTION: LITHTECH 0x00446a90
void CLocalDriver::Disconnect(CBaseConn *id, int reason)
{
	DebugOut("CLocalDriver::Disconnect\n");
	m_pNetMgr->DisconnectNotify(id);
	Term();
}

// FUNCTION: LITHTECH 0x00446ad0
LTBOOL CLocalDriver::SendPacket(void *pData, uint32 dataLen, uint32 spaceAfter, CBaseConn *idSendTo)
{
	if(m_pConnection)
	{
		m_pConnection->RecvPacket(pData, dataLen);
		return LTTRUE;
	}

	return LTFALSE;
}

// The original calls CMoArray::GetArray out of line (00465e00) although this object otherwise
// inlines (/Ob1); the engine's CMoArray evidently doesn't define it inline. Emulated here.
#pragma inline_depth(0)
// FUNCTION: LITHTECH 0x00446b10
LTBOOL CLocalDriver::GetPacket(CPacket *pPacket)
{
	CLocalPacketHolder *pHolder;
	uint8 *pDest;

	pHolder = m_Waiting.m_pNext;
	if(!m_pBaseConn || pHolder == &m_Waiting || m_bPendingConnection)
		return LTFALSE;

	// Here's your packet...
	pDest = pPacket->m_Data.GetArray();
	memcpy(pDest, pHolder->m_pData, pHolder->m_DataLen);
	pPacket->m_DataLen = (uint16)pHolder->m_DataLen;

	// Here's who sent it...
	pPacket->m_pSender = m_pBaseConn;

	// Remove it from the waiting list.
	pHolder->m_pPrev->m_pNext = pHolder->m_pNext;
	pHolder->m_pNext->m_pPrev = pHolder->m_pPrev;
	pHolder->m_Flags &= ~1;

	// One less waiting packet...
	--m_nWaiting;
	return LTTRUE;
}

#pragma inline_depth()

// FUNCTION: LITHTECH 0x00446be0
void CLocalDriver::ConnectToMe(CLocalDriver *pDriver)
{
	DoConnection(pDriver);
}

// FUNCTION: LITHTECH 0x00446c00
void CLocalDriver::DisconnectFromMe()
{
	if(m_pBaseConn)
		Disconnect(m_pBaseConn, DISCONNECTREASON_LOCALDRIVER);
}

// FUNCTION: LITHTECH 0x00446c30
void CLocalDriver::DoConnection(CLocalDriver *pDriver)
{
	if(!m_pConnection)
	{
		m_pBaseConn = new CBaseConn;
		m_pBaseConn->m_pDriver = this;

		m_pConnection = pDriver;

		// This makes it wait until it gets update to notify everyone that
		// we made a new connection.  This is because you don't want the
		// connection notification coming in at an inconvenient time.
		m_bPendingConnection = LTTRUE;
	}
}

// FUNCTION: LITHTECH 0x00446ca0
void CLocalDriver::RecvPacket(void *pData, int dataLen)
{
	CLocalPacketHolder *pCur, *pHolder, *pTail;

	// Find the smallest free holder that fits.
	pCur = m_Holders.m_pNextFree;
	pHolder = LTNULL;
	while(pCur != &m_Holders)
	{
		if(!(pCur->m_Flags & 3) && pCur->m_AllocSize >= dataLen)
		{
			pHolder = pCur;
			break;
		}

		pCur = pCur->m_pNextFree;
	}

	if(!pHolder)
	{
		pHolder = new CLocalPacketHolder;
		memset(pHolder, 0, sizeof(CLocalPacketHolder));
		pHolder->m_AllocSize = dataLen;
		pHolder->m_pData = new uint8[dataLen];

		++m_nPackets;
		m_nAllocatedBytes += dataLen;

		// Insert it sorted by size.
		pCur = &m_Holders;
		for(;;)
		{
			if(dataLen <= pCur->m_pNextFree->m_AllocSize || pCur->m_pNextFree == &m_Holders)
			{
				pHolder->m_pNextFree = pCur->m_pNextFree;
				pCur->m_pNextFree = pHolder;
				break;
			}

			pCur = pCur->m_pNextFree;
		}
	}

	memcpy(pHolder->m_pData, pData, dataLen);
	pHolder->m_DataLen = dataLen;

	// Put it at the end of the waiting list.
	pTail = m_Waiting.m_pPrev;
	pHolder->m_pNext = pTail->m_pNext;
	pHolder->m_pPrev = pTail;
	pTail->m_pNext = pHolder;
	pHolder->m_pNext->m_pPrev = pHolder;
	pHolder->m_Flags |= 1;

	++m_nWaiting;
}
