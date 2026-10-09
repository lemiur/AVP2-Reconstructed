// Talon kernel/net/sys/win/udpdriver.cpp: the "internet" driver. Jupiter's udpdriver.cpp is a
// threaded rewrite; this one sends raw packets with winsock and leaves guaranteed delivery
// to the netmgr. Its errors still call it TCPDriver.
// FLAGS: /O2 /GX-
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bdefs.h"
#include "udpdriver.h"
#include <mmsystem.h>
#include "engine_vars.h"

void DebugOut(const char *pStr, ...);
float time_GetTime();



struct SUDPError
{
	int		m_ErrorCode;
	char	*m_ErrorString;
};

#define DEFINE_UDP_ERROR(x) { x, #x },

// GLOBAL: LITHTECH 0x004d73b0
SUDPError g_UDPErrorStrings[] =
{
	DEFINE_UDP_ERROR(WSAEINTR)
	DEFINE_UDP_ERROR(WSAEBADF)
	DEFINE_UDP_ERROR(WSAEACCES)
	DEFINE_UDP_ERROR(WSAEFAULT)
	DEFINE_UDP_ERROR(WSAEINVAL)
	DEFINE_UDP_ERROR(WSAEMFILE)
	DEFINE_UDP_ERROR(WSAEWOULDBLOCK)
	DEFINE_UDP_ERROR(WSAEINPROGRESS)
	DEFINE_UDP_ERROR(WSAEALREADY)
	DEFINE_UDP_ERROR(WSAENOTSOCK)
	DEFINE_UDP_ERROR(WSAEDESTADDRREQ)
	DEFINE_UDP_ERROR(WSAEMSGSIZE)
	DEFINE_UDP_ERROR(WSAEPROTOTYPE)
	DEFINE_UDP_ERROR(WSAENOPROTOOPT)
	DEFINE_UDP_ERROR(WSAEPROTONOSUPPORT)
	DEFINE_UDP_ERROR(WSAESOCKTNOSUPPORT)
	DEFINE_UDP_ERROR(WSAEOPNOTSUPP)
	DEFINE_UDP_ERROR(WSAEPFNOSUPPORT)
	DEFINE_UDP_ERROR(WSAEAFNOSUPPORT)
	DEFINE_UDP_ERROR(WSAEADDRINUSE)
	DEFINE_UDP_ERROR(WSAEADDRNOTAVAIL)
	DEFINE_UDP_ERROR(WSAENETDOWN)
	DEFINE_UDP_ERROR(WSAENETUNREACH)
	DEFINE_UDP_ERROR(WSAENETRESET)
	DEFINE_UDP_ERROR(WSAECONNABORTED)
	DEFINE_UDP_ERROR(WSAECONNRESET)
	DEFINE_UDP_ERROR(WSAENOBUFS)
	DEFINE_UDP_ERROR(WSAEISCONN)
	DEFINE_UDP_ERROR(WSAENOTCONN)
	DEFINE_UDP_ERROR(WSAESHUTDOWN)
	DEFINE_UDP_ERROR(WSAETOOMANYREFS)
	DEFINE_UDP_ERROR(WSAETIMEDOUT)
	DEFINE_UDP_ERROR(WSAECONNREFUSED)
	DEFINE_UDP_ERROR(WSAELOOP)
	DEFINE_UDP_ERROR(WSAENAMETOOLONG)
	DEFINE_UDP_ERROR(WSAEHOSTDOWN)
	DEFINE_UDP_ERROR(WSAEHOSTUNREACH)
	DEFINE_UDP_ERROR(WSAENOTEMPTY)
	DEFINE_UDP_ERROR(WSAEPROCLIM)
	DEFINE_UDP_ERROR(WSAEUSERS)
	DEFINE_UDP_ERROR(WSAEDQUOT)
	DEFINE_UDP_ERROR(WSAESTALE)
	DEFINE_UDP_ERROR(WSAEREMOTE)
	DEFINE_UDP_ERROR(WSAEDISCON)
	DEFINE_UDP_ERROR(WSASYSNOTREADY)
	DEFINE_UDP_ERROR(WSAVERNOTSUPPORTED)
	DEFINE_UDP_ERROR(WSANOTINITIALISED)
};

#define NUM_UDPERRORSTRINGS	(sizeof(g_UDPErrorStrings) / sizeof(g_UDPErrorStrings[0]))


char* tcp_GetLastError();
SOCKET tcp_BindToPort(CUDPDriver *pDriver, sockaddr_in *pAddr, sockaddr_in *pFinalAddr);
LTBOOL tcp_SetupLocalSockaddr(sockaddr_in *pSockInfo, uint16 portNum);
CUDPConn* FindConnByAddr2(CUDPDriver *pDriver, sockaddr_in *pAddr);
int FindAddrInList(sockaddr_in *pTest, CMoArray<CUDPQuery> &sessions);


// CBaseDriver and BaseService defaults emitted from netmgr.h.
// FUNCTION: LITHTECH 0x00433cd0 ?GetServiceList@CBaseDriver@@UAEKAAPAVNetService@@@Z
// FUNCTION: LITHTECH 0x00433ce0 ?GetSessionList@CBaseDriver@@UAEKAAPAVNetSession@@PAD@Z
// FUNCTION: LITHTECH 0x00433cf0 ?GetLocalIpAddress@CBaseDriver@@UAEIPADKAAG@Z
// FUNCTION: LITHTECH 0x00433d00 ?SendTcpIp@CBaseDriver@@UAEKPAXKPADK@Z
// FUNCTION: LITHTECH 0x00433d10 ??_GCBaseDriver@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00433d30 ??_GBaseService@@UAEPAXI@Z

// ----------------------------------------------------------------- //
// Internal helpers.
// ----------------------------------------------------------------- //

// Builds a socket address from the string.
// FUNCTION: LITHTECH 0x00497770
LTBOOL tcp_BuildSockaddrFromString(char *pCmdData, sockaddr_in *pSockInfo)
{
	hostent *pHostName;
	unsigned char *pAddrList;
	u_long longAddr, i;
	uint32 port;
	char ip[256];
	char *pTest;

	pTest = strchr(pCmdData, ':');
	if(pTest)
	{
		memcpy(ip, pCmdData, pTest-pCmdData);
		ip[pTest-pCmdData] = 0;
		port = atoi(pTest+1);
	}
	else
	{
		port = DEFAULT_PORT;
		SAFE_STRCPY(ip, pCmdData);
	}

	if(ip[0] >= '0' && ip[0] <= '9')
	{
		// Numeric IP address.
		longAddr = ntohl(inet_addr(ip));
	}
	else
	{
		pHostName = gethostbyname(ip);
		if(!pHostName)
		{
			DebugOut("gethostbyname() returned LTNULL in tcp_BuildSockaddrFromString\n");
			return FALSE;
		}

		pAddrList = (unsigned char*)pHostName->h_addr_list[0];
		longAddr = 0;
		for(i=0; i < 4; i++)
		{
			longAddr |= (u_long)pAddrList[i] << ((3-i) * 8);
		}
	}

	memset(pSockInfo, 0, sizeof(*pSockInfo));
	pSockInfo->sin_family = AF_INET;
	pSockInfo->sin_addr.s_addr = htonl(longAddr);
	pSockInfo->sin_port = htons((u_short)port);

	return TRUE;
}


// Returns the number of bytes received, or -1.
// FUNCTION: LITHTECH 0x00497890
int tcp_RecvFrom(CUDPDriver *pDriver, SOCKET theSocket, void *pData, int dataLen, sockaddr_in *pSender)
{
	int fromSize, status;

	fromSize = sizeof(sockaddr_in);
	status = recvfrom(theSocket, (char*)pData, dataLen, 0, (sockaddr*)pSender, &fromSize);
	if(status != 0 && status != SOCKET_ERROR)
		return status;

	if(g_CV_IPDebug > 1)
	{
		if(WSAGetLastError() != WSAEWOULDBLOCK)
			dsi_ConsolePrint("recvfrom returned error %d (max packet size %d)", status, dataLen);
	}

	return -1;
}


// ----------------------------------------------------------------- //
// CUDPDriver code.
// ----------------------------------------------------------------- //

// An inline virtual in the original: the exe has it between SelectService and ??_GCUDPDriver, where VC6 puts the
// inline virtuals already defined when the constructor needs the vtable.
// FUNCTION: LITHTECH 0x004979c0 ?GetPacketOverhead@CUDPDriver@@UAEKXZ
inline uint32 CUDPDriver::GetPacketOverhead()
{
	// IP and UDP headers.
	return 28;
}


// FUNCTION: LITHTECH 0x00497900
CUDPDriver::CUDPDriver()
{
	m_bHosting = FALSE;
	m_bWSAStarted = FALSE;
	m_Socket = 0;
	m_pSessionName = LTNULL;
	m_MaxConnections = 0;
	m_QuerySocket = 0;
	SAFE_STRCPY(m_Name, "internet");
	m_DriverFlags = 1;
}

// FUNCTION: LITHTECH 0x004979b0 ?SelectService@CUDPDriver@@UAEKPAUHNETSERVICE_t@@@Z


// FUNCTION: LITHTECH 0x004979d0 ??_GCUDPDriver@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004979f0
CUDPDriver::~CUDPDriver()
{
	Term();
}


// FUNCTION: LITHTECH 0x00497a50
LTBOOL CUDPDriver::Init()
{
	int status;
	WSADATA wsaData;

	status = WSAStartup(MAKEWORD(1,1), &wsaData);
	if(status == 0)
	{
		m_bWSAStarted = TRUE;

		if(g_CV_IPDebug)
		{
			dsi_ConsolePrint("TCP/IP initialized");
			dsi_ConsolePrint("Description: %s", wsaData.szDescription);
			dsi_ConsolePrint("Status: %s", wsaData.szSystemStatus);
		}

		return TRUE;
	}
	else
	{
		DebugOut("WSAStartup() returned %d\n", status);
		return FALSE;
	}
}


// FUNCTION: LITHTECH 0x00497ae0
void CUDPDriver::TermConnections(LTBOOL bCleanup)
{
	MPOS pos;
	CUDPConn *pConn;

	// Close down any connections.
	for(pos=m_Connections; pos; )
	{
		pConn = m_Connections.GetNext(pos);
		delete pConn;
	}
	m_Connections.RemoveAll();

	if(m_Socket)
	{
		closesocket(m_Socket);
		m_Socket = 0;
	}

	// Get rid of the query stuff.
	EndQuery();

	if(m_pSessionName)
	{
		delete m_pSessionName;
		m_pSessionName = LTNULL;
	}

	if(bCleanup && m_bWSAStarted)
	{
		WSACleanup();
		m_bWSAStarted = FALSE;
	}
}


// FUNCTION: LITHTECH 0x00497ba0
void CUDPDriver::Term()
{
	MPOS pos;

	// Tell all our connections we're going away.
	for(pos=m_Connections; pos; )
	{
		SendDisconnect(m_Connections.GetNext(pos));
	}

	TermConnections(TRUE);
}


// FUNCTION: LITHTECH 0x00497be0
LTBOOL CUDPDriver::SendTo(SOCKET theSocket, void *pData, int dataLen, sockaddr_in *pAddr)
{
	return sendto(theSocket, (char*)pData, dataLen, 0, (sockaddr*)pAddr, sizeof(*pAddr)) == dataLen;
}


// FUNCTION: LITHTECH 0x00497c10
void CUDPDriver::SendDisconnect(CUDPConn *pConn)
{
	uint8 data[4];
	int i;

	data[0] = 0;
	data[1] = TCPSUB_DISCONNECT;
	for(i=0; i < 4; i++)
	{
		SendTo(m_Socket, data, 3, &pConn->m_Addr);
	}
}


// FUNCTION: LITHTECH 0x00497c50
CUDPConn* CUDPDriver::FindConnByAddr(sockaddr_in *pAddr)
{
	MPOS pos;
	CUDPConn *pConn;

	for(pos=m_Connections; pos; )
	{
		pConn = m_Connections.GetNext(pos);

		if(pConn->m_Addr.sin_port == pAddr->sin_port &&
			pConn->m_Addr.sin_addr.s_addr == pAddr->sin_addr.s_addr)
		{
			return pConn;
		}
	}
	return LTNULL;
}


// FUNCTION: LITHTECH 0x00497ca0
LTRESULT CUDPDriver::GetServiceList(NetService* &pListHead)
{
	NetService *pRet;

	pRet = new NetService;
	if(!pRet)
		return LT_ERROR;

	m_Service.m_pDriver = this;
	pRet->m_handle = (HNETSERVICE)&m_Service;
	pRet->m_dwFlags = NETSERVICE_TCPIP;
	memset(&pRet->m_guidService, 0, sizeof(pRet->m_guidService));
	SAFE_STRCPY(pRet->m_sName, "Internet TCP/IP");

	pRet->m_pNext = pListHead;
	pListHead = pRet;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00497d20
LTRESULT CUDPDriver::StartQuery(char *pInfo)
{
	CUDPQuery tempQuery;
	ConParse parse;
	int bTrue;
	uint16 port;
	char *pTest;

	// Clear our old list of sessions.
	EndQuery();

	if(!tcp_SetupLocalSockaddr(&m_QueryAddr, 0) ||
		!(m_QuerySocket = tcp_BindToPort(this, &m_QueryAddr, &m_QueryAddr)))
	{
		RETURN_ERROR_PARAM(2, TCPDriver::StartQuery, LT_ERROR, "Can't bind to port");
	}

	// Parse out the list of addresses to query.
	parse.Init(pInfo);
	while(parse.Parse())
	{
		if(parse.m_nArgs > 0)
		{
			memset(&tempQuery, 0, sizeof(tempQuery));

			if(parse.m_Args[0][0] == '*')
			{
				// Broadcast.. get the port.
				pTest = strchr(parse.m_Args[0], ':');
				if(pTest)
					port = (uint16)atoi(pTest+1);
				else
					port = DEFAULT_PORT;

				tempQuery.m_Addr.sin_family = AF_INET;
				tempQuery.m_Addr.sin_port = htons(port);
				tempQuery.m_Addr.sin_addr.s_addr = INADDR_BROADCAST;
				tempQuery.m_iTime = 0xFF;
				tempQuery.m_LastResponseTime = time_GetTime();
				m_Queries.Append(tempQuery);
			}
			else
			{
				if(tcp_BuildSockaddrFromString(parse.m_Args[0], &tempQuery.m_Addr))
				{
					tempQuery.m_LastResponseTime = time_GetTime();
					m_Queries.Append(tempQuery);
				}
			}
		}
	}

	bTrue = 1;
	setsockopt(m_QuerySocket, SOL_SOCKET, SO_BROADCAST, (char*)&bTrue, sizeof(bTrue));

	// Send the first queries right away.
	m_LastQueryTime = time_GetTime() - 2.0f;
	UpdateQuery();

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00498090 ?Copy@CUDPQuery@@QAEXABV1@@Z


// FUNCTION: LITHTECH 0x00498100
LTBOOL tcp_SetupLocalSockaddr(sockaddr_in *pSockInfo, uint16 portNum)
{
	memset(pSockInfo, 0, sizeof(*pSockInfo));
	pSockInfo->sin_family = AF_INET;
	pSockInfo->sin_port = htons(portNum);

	if(g_CV_BindIP)
	{
		pSockInfo->sin_addr.s_addr = inet_addr(g_CV_BindIP);
	}
	else
	{
		pSockInfo->sin_addr.s_addr = INADDR_ANY;
	}

	return TRUE;
}


// Binds to the specified port so we can send/receive on it.
// If pFinalAddr is non-LTNULL, then it is filled in with the final address used.
// FUNCTION: LITHTECH 0x00498150
SOCKET tcp_BindToPort(CUDPDriver *pDriver, sockaddr_in *pAddr, sockaddr_in *pFinalAddr)
{
	SOCKET ret;
	int status, nameBufSize;
	unsigned long val;

	// Create a socket to send and receive through.
	ret = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(ret == SOCKET_ERROR)
	{
		DebugOut("socket() returned SOCKET_ERROR.  Last error: %s\n", tcp_GetLastError());
		return 0;
	}

	// Nonblocking please..
	val = 1;
	status = ioctlsocket(ret, FIONBIO, &val);
	if(status != 0)
	{
		dsi_ConsolePrint("ioctlsocket FIONBIO (nonblocking) returned %d", status);
		closesocket(ret);
		return 0;
	}

	// bind to it!
	status = bind(ret, (sockaddr*)pAddr, sizeof(*pAddr));
	if(status == 0)
	{
		if(pFinalAddr)
		{
			nameBufSize = sizeof(*pFinalAddr);
			getsockname(ret, (sockaddr*)pFinalAddr, &nameBufSize);
		}

		return ret;
	}
	else
	{
		DebugOut("bind() returned %d.  Last error: %s.\n", status, tcp_GetLastError());
		closesocket(ret);
		return 0;
	}
}


// Returns a string representing the last TCP/IP error.
#pragma warning(disable : 4715)
// FUNCTION: LITHTECH 0x00498220
char* tcp_GetLastError()
{
	int lastError;
	uint32 i;

	lastError = WSAGetLastError();
	for(i=0; i < NUM_UDPERRORSTRINGS; i++)
	{
		if(g_UDPErrorStrings[i].m_ErrorCode == lastError)
			return g_UDPErrorStrings[i].m_ErrorString;
	}
}
#pragma warning(default : 4715)


// Both packets are CPacketRefs declared before tempQuery (the original releases them after tempQuery's destructor).
// FUNCTION: LITHTECH 0x00498240
LTRESULT CUDPDriver::UpdateQuery()
{
	CPacketRef cQueryPacket, cPacket;
	CUDPQuery tempQuery;
	CUDPQuery *pQuery;
	sockaddr_in addr;
	uint8 queryIndex;
	uint32 subID;
	char *pInfo;
	uint32 i;
	int nBytes, index;

	queryIndex = 0;

	if(!m_QuerySocket)
		RETURN_ERROR(1, UpdateQuery, LT_NOTINITIALIZED);

	// Send out the queries every second.
	if(time_GetTime() - m_LastQueryTime >= 1.0f)
	{
		cQueryPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		cQueryPacket->m_Data[0] = 0;

		for(i=0; i < m_Queries; i++)
		{
			pQuery = &m_Queries[i];

			cQueryPacket->ResetWrite();
			cQueryPacket->WriteType((uint8)TCPSUB_QUERY);
			cQueryPacket->WriteRaw(m_pNetMgr->GetAppGuid(), sizeof(LTGUID));

			if(pQuery->m_iTime == 0xFF)
			{
				queryIndex = 0xFF;
			}
			else
			{
				queryIndex = (uint8)(pQuery->m_iTime & (NUM_QUERY_TIMES-1));
				++pQuery->m_iTime;
				pQuery->m_SendTimes[queryIndex] = time_GetTime();
			}

			cQueryPacket->WriteType(queryIndex);
			SendTo(m_QuerySocket, cQueryPacket->m_Data.GetArray(), cQueryPacket->m_DataLen, &m_Queries[i].m_Addr);
		}

		m_LastQueryTime = time_GetTime();
	}

	// Read the responses.
	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	while((nBytes = tcp_RecvFrom(this, m_QuerySocket, cPacket->m_Data.GetArray(), cPacket->m_Data.GetSize(), &addr)) != -1)
	{
		cPacket->m_DataLen = (uint16)nBytes;
		cPacket->m_Pos = 1;

		subID = cPacket->ReadType((uint8*)0);
		if((cPacket->m_ErrorFlags & PACKETERR_READOVERFLOW) || subID != TCPSUB_QUERYRESPONSE)
			continue;

		pInfo = cPacket->ReadString();
		queryIndex = cPacket->ReadType((uint8*)0);
		if(cPacket->m_ErrorFlags & PACKETERR_READOVERFLOW)
			continue;

		index = FindAddrInList(&addr, m_Queries);
		if(index == -1)
		{
			memset(&tempQuery, 0, sizeof(tempQuery));
			tempQuery.m_Addr = addr;
			if(!m_Queries.Append(tempQuery))
				continue;

			index = m_Queries.GetSize() - 1;
			if(index == -1)
				continue;
		}

		pQuery = &m_Queries[index];
		pQuery->m_LastResponseTime = time_GetTime();

		if(pQuery->m_pInfo)
			delete pQuery->m_pInfo;

		pQuery->m_pInfo = new char[strlen(pInfo) + 1];
		if(pQuery->m_pInfo)
			strcpy(pQuery->m_pInfo, pInfo);

		if(queryIndex < NUM_QUERY_TIMES && queryIndex != 0xFF)
		{
			if(pQuery->m_SendTimes[queryIndex] == 0.0f)
				pQuery->m_Ping = 0.0f;
			else
				pQuery->m_Ping = time_GetTime() - pQuery->m_SendTimes[queryIndex];
		}
	}

	// Forget hosts that stopped answering.
	for(i=0; i < m_Queries; i++)
	{
		pQuery = &m_Queries[i];
		if(time_GetTime() - pQuery->m_LastResponseTime > g_CV_IPQueryTimeout)
		{
			if(pQuery->m_pInfo)
			{
				delete pQuery->m_pInfo;
				pQuery->m_pInfo = LTNULL;
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004986d0
int FindAddrInList(sockaddr_in *pTest, CMoArray<CUDPQuery> &sessions)
{
	uint32 i;

	for(i=0; i < sessions; i++)
	{
		if(memcmp(pTest, &sessions[i].m_Addr, sizeof(*pTest)) == 0)
			return (int)i;
	}
	return -1;
}


// FUNCTION: LITHTECH 0x00498710
LTRESULT CUDPDriver::GetQueryResults(NetSession* &pListHead)
{
	uint32 i;
	CUDPQuery *pQuery;
	CUDPSession *pSession;

	pListHead = LTNULL;
	for(i=0; i < m_Queries; i++)
	{
		pQuery = &m_Queries[i];
		if(!pQuery->m_pInfo)
			continue;

		pSession = new CUDPSession(this);
		if(!pSession)
			continue;

		pSession->m_Addr = pQuery->m_Addr;
		SAFE_STRCPY(pSession->m_sName, pQuery->m_pInfo);
		pSession->m_Ping = (uint32)(pQuery->m_Ping * 1000.0f);
		memset(&pSession->m_guidInst, 0, sizeof(pSession->m_guidInst));
		*((CUDPQuery**)&pSession->m_guidInst) = pQuery;
		memset(&pSession->m_guidApp, 0, sizeof(pSession->m_guidApp));
		sprintf(pSession->m_HostIP, "%d.%d.%d.%d",
			pQuery->m_Addr.sin_addr.S_un.S_un_b.s_b1,
			pQuery->m_Addr.sin_addr.S_un.S_un_b.s_b2,
			pQuery->m_Addr.sin_addr.S_un.S_un_b.s_b3,
			pQuery->m_Addr.sin_addr.S_un.S_un_b.s_b4);
		pSession->m_HostPort = ntohs(pSession->m_Addr.sin_port);

		pSession->m_pNext = pListHead;
		pListHead = pSession;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004988a0 ??_GCUDPSession@@UAEPAXI@Z


// FUNCTION: LITHTECH 0x004988c0
LTRESULT CUDPDriver::EndQuery()
{
	if(m_QuerySocket)
	{
		closesocket(m_QuerySocket);
		m_QuerySocket = 0;
	}

	m_Queries.Term();
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00498930
LTRESULT CUDPDriver::GetSessionList(NetSession* &pListHead, char *pInfo)
{
	LTRESULT dResult;
	float startTime;

	pListHead = LTNULL;

	dResult = StartQuery(pInfo);
	if(dResult != LT_OK)
		return dResult;

	startTime = time_GetTime();
	while(time_GetTime() - startTime < 3.5f)
	{
		dResult = UpdateQuery();
		if(dResult != LT_OK)
			return dResult;
	}

	GetQueryResults(pListHead);
	EndQuery();
	return LT_OK;
}


// FUNCTION: LITHTECH 0x004989b0
LTRESULT CUDPDriver::GetSessionName(char* sName, uint32 dwBufferSize)
{
	sName[0] = 0;
	if(m_pSessionName)
	{
		strncpy(sName, m_pSessionName, dwBufferSize);
		sName[dwBufferSize-1] = 0;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004989e0
LTRESULT CUDPDriver::SetSessionName(char* sName)
{
	if(m_pSessionName)
	{
		if(strlen(m_pSessionName) >= strlen(sName))
		{
			strcpy(m_pSessionName, sName);
			return LT_OK;
		}
	}

	delete m_pSessionName;
	m_pSessionName = new char[strlen(sName) + 1];
	if(m_pSessionName)
	{
		strcpy(m_pSessionName, sName);
		return LT_OK;
	}

	return LT_ERROR;
}


// FUNCTION: LITHTECH 0x00498a70
void CUDPDriver::Disconnect(CBaseConn *id, int reason)
{
	CUDPConn *pConn = (CUDPConn*)id;

	if(!pConn)
		return;

	if(g_CV_IPDebug)
	{
		dsi_ConsolePrint("TCPDriver::Disconnect (%d) on %d.%d.%d.%d:%d", reason,
			pConn->m_Addr.sin_addr.S_un.S_un_b.s_b1,
			pConn->m_Addr.sin_addr.S_un.S_un_b.s_b2,
			pConn->m_Addr.sin_addr.S_un.S_un_b.s_b3,
			pConn->m_Addr.sin_addr.S_un.S_un_b.s_b4,
			ntohs(pConn->m_Addr.sin_port));
	}

	m_pNetMgr->DisconnectNotify(pConn);
	SendDisconnect(pConn);

	m_Connections.RemoveAt(&pConn->m_Link);
	delete pConn;
}


// FUNCTION: LITHTECH 0x00498b60
LTRESULT CUDPDriver::HostSession(NetHost *pHost)
{
	uint16 port;
	sockaddr_in addr;
	char portStr[64];

	TermConnections(FALSE);

	port = (uint16)pHost->m_Port;
	if(!port)
		port = DEFAULT_PORT;

	if(!tcp_SetupLocalSockaddr(&m_HostAddr, port))
		return LT_ERROR;

	m_Socket = tcp_BindToPort(this, &m_HostAddr, &addr);
	if(!m_Socket)
	{
		sprintf(portStr, "%d", port);
		RETURN_ERROR_PARAM(2, TCPDriver::HostSession, LT_CANTBINDTOPORT, portStr);
	}

	if(g_CV_IPDebug)
	{
		dsi_ConsolePrint("Hosting on %d.%d.%d.%d:%d",
			addr.sin_addr.S_un.S_un_b.s_b1,
			addr.sin_addr.S_un.S_un_b.s_b2,
			addr.sin_addr.S_un.S_un_b.s_b3,
			addr.sin_addr.S_un.S_un_b.s_b4,
			ntohs(addr.sin_port));
	}

	m_bHosting = TRUE;
	m_MaxConnections = pHost->m_dwMaxPlayers;
	SetSessionName(pHost->m_sName);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00498c90 ?JoinSession@CUDPDriver@@UAEKPAVNetSession@@@Z
LTRESULT CUDPDriver::JoinSession(NetSession *pSession)
{
	return JoinSession(&((CUDPSession*)pSession)->m_Addr);
}


// Wave 6: SIZE 236 -> 88 aligned (1424 bytes vs 1408). The connect-request replies are block-scoped CPacketRefs
// (their `->` uses are the pending sites that keep the accepted reply's WriteType and AddHead's InsertBefore out
// of line, as in the original; 7 free calls in the accept block do the same). The QUERY reply as a CPacketRef too
// is 95. Left: the reject path's `SendTo; Release; return` tail, which the original cross-jumps into the QUERY
// reply's identical tail (ours loads the Release vtable into eax there, the QUERY tail into edx, so they don't
// merge), and register order in the conn-request/disconnect IP prints. It calls AddHead's InsertBefore out of line
// as the original does, which emits the InsertBefore copy (wave 7: the stand-in that forced it is gone).
// Wave 7 phase 2: audit: the only difference is our extra `SendTo; vcall+8` at +0x487, the reject tail the exe
// cross-jumps into the QUERY reply's; inline_budget: out-of-line calls equal the exe's.
// PARKED: one cross-jumped reply tail (registers differ so VC6 doesn't merge) and IP-print register order (88 aligned); behaviour identical
// STUB: LITHTECH 0x00498cb0
void CUDPDriver::HandleDriverPacket(CPacket *pPacket, sockaddr_in *pSender)
{
	uint8 subPacketID, queryIndex;
	LTGUID guid;
	CUDPConn *pConn;
	LTBOOL bWrongGUID;
	int i;

	pPacket->m_Pos = 1;
	subPacketID = pPacket->ReadType((uint8*)0);
	if(pPacket->m_ErrorFlags & PACKETERR_READOVERFLOW)
		return;

	if(m_bHosting)
	{
		// Already connected?
		if(FindConnByAddr2(this, pSender))
			return;

		if(subPacketID == TCPSUB_QUERY)
		{
			pPacket->ReadRaw(&guid, sizeof(guid));
			if(memcmp(&guid, &m_pNetMgr->m_guidApp, sizeof(guid)) != 0)
				return;

			if(g_CV_IPDebug)
			{
				dsi_ConsolePrint("IP: Received query from %d.%d.%d.%d:%d, sending response",
					pSender->sin_addr.S_un.S_un_b.s_b1,
					pSender->sin_addr.S_un.S_un_b.s_b2,
					pSender->sin_addr.S_un.S_un_b.s_b3,
					pSender->sin_addr.S_un.S_un_b.s_b4,
					ntohs(pSender->sin_port));
			}

			queryIndex = pPacket->ReadType((uint8*)0);

			CPacket *pReply = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));
			pReply->m_Data[0] = 0;
			pReply->WriteType((uint8)TCPSUB_QUERYRESPONSE);
			pReply->WriteString(m_pSessionName ? m_pSessionName : g_EmptyString);
			pReply->WriteType(queryIndex);
			SendTo(m_Socket, pReply->m_Data.GetArray(), pReply->m_DataLen, pSender);
			pReply->Release();
			return;
		}
		else if(subPacketID == TCPSUB_CONNECTREQUEST)
		{
			if(g_CV_IPDebug)
			{
				dsi_ConsolePrint("IP: Received conn request from %d.%d.%d.%d:%d, sending response",
					pSender->sin_addr.S_un.S_un_b.s_b1,
					pSender->sin_addr.S_un.S_un_b.s_b2,
					pSender->sin_addr.S_un.S_un_b.s_b3,
					pSender->sin_addr.S_un.S_un_b.s_b4,
					ntohs(pSender->sin_port));
			}

			pPacket->ReadType((uint8*)0);
			pPacket->ReadRaw(&guid, sizeof(guid));
			if(pPacket->m_ErrorFlags & PACKETERR_READOVERFLOW)
				return;

			bWrongGUID = FALSE;
			if(memcmp(&m_pNetMgr->m_guidApp, &guid, sizeof(guid)) != 0)
			{
				bWrongGUID = TRUE;
			}
			else if(m_Connections.GetSize() < m_MaxConnections)
			{
				pConn = new CUDPConn;
				pConn->m_Addr = *pSender;
				pConn->m_pDriver = this;
				pConn->m_bConnected = FALSE;

				if(m_pNetMgr->NewConnectionNotify(pConn))
				{
					m_Connections.AddHead(pConn, &pConn->m_Link);

					// Tell them they're in.
					CPacketRef cReply;
					cReply = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
					cReply->m_Data[0] = 0;
					cReply->WriteType((uint8)TCPSUB_CONNECTACCEPTED);
					for(i=0; i < 5; i++)
					{
						SendTo(m_Socket, cReply->m_Data.GetArray(), cReply->m_DataLen, pSender);
					}

					pConn->m_bConnected = TRUE;
					m_pNetMgr->ResendGuaranteed(pConn);
					return;
				}
				else
				{
					delete pConn;
				}
			}

			CPacketRef cReply;
			cReply = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			cReply->m_Data[0] = 0;
			if(bWrongGUID)
				cReply->WriteType((uint8)TCPSUB_NOTSAMEGUID);
			else
				cReply->WriteType((uint8)TCPSUB_CONNECTREJECTED);

			SendTo(m_Socket, cReply->m_Data.GetArray(), cReply->m_DataLen, pSender);
			return;
		}
	}

	if(subPacketID == TCPSUB_DISCONNECT)
	{
		pConn = FindConnByAddr(pSender);
		if(!pConn)
			return;

		if(g_CV_IPDebug)
		{
			dsi_ConsolePrint("Got TCPSUB_DISCONNECT from %d.%d.%d.%d:%d",
				pSender->sin_addr.S_un.S_un_b.s_b1,
				pSender->sin_addr.S_un.S_un_b.s_b2,
				pSender->sin_addr.S_un.S_un_b.s_b3,
				pSender->sin_addr.S_un.S_un_b.s_b4,
				ntohs(pSender->sin_port));
		}

		if(m_pNetMgr->m_pHandler)
			m_pNetMgr->m_pHandler->SetDisconnectCode(4, g_EmptyString);

		m_pNetMgr->DisconnectNotify(pConn);
		m_Connections.RemoveAt(&pConn->m_Link);
		delete pConn;
	}
}


// FUNCTION: LITHTECH 0x00499230
CUDPConn* FindConnByAddr2(CUDPDriver *pDriver, sockaddr_in *pAddr)
{
	MPOS pos;
	CUDPConn *pConn;

	for(pos=pDriver->m_Connections; pos; )
	{
		pConn = pDriver->m_Connections.GetNext(pos);

		if(pConn->m_Addr.sin_addr.s_addr == pAddr->sin_addr.s_addr &&
			pConn->m_Addr.sin_port == pAddr->sin_port)
		{
			return pConn;
		}
	}
	return LTNULL;
}


// FUNCTION: LITHTECH 0x00499280
LTBOOL CUDPDriver::GetPacket(CPacket *pPacket)
{
	sockaddr_in addr;
	int nBytes;
	CUDPConn *pConn;

	if(!m_Socket)
		return FALSE;

	while((nBytes = tcp_RecvFrom(this, m_Socket, pPacket->m_Data.GetArray(), pPacket->m_Data.GetSize(), &addr)) != -1)
	{
		if(nBytes == 0)
			break;

		pPacket->m_DataLen = (uint16)nBytes;
		if(pPacket->m_Data[0] == 0)
		{
			HandleDriverPacket(pPacket, &addr);
		}
		else
		{
			pConn = FindConnByAddr(&addr);
			if(pConn)
			{
				pPacket->m_pSender = pConn;
				return TRUE;
			}

			if(m_pNetMgr->m_pHandler)
			{
				uint8 senderAddr[4];

				senderAddr[0] = addr.sin_addr.S_un.S_un_b.s_b1;
				senderAddr[1] = addr.sin_addr.S_un.S_un_b.s_b2;
				senderAddr[2] = addr.sin_addr.S_un.S_un_b.s_b3;
				senderAddr[3] = addr.sin_addr.S_un.S_un_b.s_b4;
				uint16 port = ntohs(addr.sin_port);
				m_pNetMgr->m_pHandler->HandleUnknownPacket(pPacket, senderAddr, port);
			}
		}
	}

	return FALSE;
}


// FUNCTION: LITHTECH 0x00499380
LTBOOL CUDPDriver::SendPacket(void *pData, uint32 dataLen, uint32 spaceAfter, CBaseConn *idSendTo)
{
	CUDPConn *pConn = (CUDPConn*)idSendTo;

	if(!pConn || !m_Socket || !pConn->m_bConnected)
		return FALSE;

	return SendTo(m_Socket, pData, dataLen, &pConn->m_Addr);
}


// FUNCTION: LITHTECH 0x004993d0
LTBOOL CUDPDriver::GetLocalIpAddress(char* sBuffer, uint32 dwBufferSize, uint16 &hostPort)
{
	char hostName[256];
	hostent *pHost;
	int nDevices, i;
	unsigned char *pAddrList;
	u_long longAddr;
	sockaddr_in addr;

	if(m_bHosting)
		hostPort = ntohs(m_HostAddr.sin_port);
	else
		hostPort = 0xFFFF;

	if(g_CV_IP && g_CV_IP[0])
	{
		strncpy(sBuffer, g_CV_IP, dwBufferSize);
		sBuffer[dwBufferSize-1] = 0;
		return TRUE;
	}

	i = gethostname(hostName, sizeof(hostName));
	if(i != 0)
	{
		DebugOut("gethostname() returned %d\n", i);
		return FALSE;
	}

	pHost = gethostbyname(hostName);
	if(!pHost)
	{
		DebugOut("gethostbyname() returned LTNULL in tcp_BuildSockaddrFromString\n");
		return FALSE;
	}

	for(nDevices=0; nDevices < 10000; nDevices++)
	{
		if(!pHost->h_addr_list[nDevices])
			break;
	}

	if(nDevices == 0)
	{
		dsi_ConsolePrint("No IP devices found!");
		return FALSE;
	}

	if(g_CV_IPDebug)
	{
		dsi_ConsolePrint("---- %d IP device%s ----", nDevices, nDevices > 1 ? "s" : g_EmptyString);
		for(i=0; i < nDevices; i++)
		{
			pAddrList = (unsigned char*)pHost->h_addr_list[i];
			dsi_ConsolePrint("%d.%d.%d.%d", pAddrList[0], pAddrList[1], pAddrList[2], pAddrList[3]);
		}
	}

	pAddrList = (unsigned char*)pHost->h_addr_list[0];
	longAddr = 0;
	for(i=0; i < 4; i++)
	{
		longAddr |= (u_long)pAddrList[i] << ((3-i) * 8);
	}

	addr.sin_addr.s_addr = htonl(longAddr);
	addr.sin_port = m_HostAddr.sin_port;

	sprintf(sBuffer, "%d.%d.%d.%d",
		addr.sin_addr.S_un.S_un_b.s_b1,
		addr.sin_addr.S_un.S_un_b.s_b2,
		addr.sin_addr.S_un.S_un_b.s_b3,
		addr.sin_addr.S_un.S_un_b.s_b4);

	if(g_CV_IPDebug)
	{
		dsi_ConsolePrint("SetupLocalSockaddr: %d.%d.%d.%d:%d",
			addr.sin_addr.S_un.S_un_b.s_b1,
			addr.sin_addr.S_un.S_un_b.s_b2,
			addr.sin_addr.S_un.S_un_b.s_b3,
			addr.sin_addr.S_un.S_un_b.s_b4,
			ntohs(addr.sin_port));
	}

	return TRUE;
}


// The request and reply are CPacketRefs (their destructors fold away on the early returns; each `->` is a
// pending inline site, which keeps the early WriteTypes out of line), and addrStr is 128 bytes (the frame).
// FUNCTION: LITHTECH 0x004995f0 ?JoinSession@CUDPDriver@@QAEKPAUsockaddr_in@@@Z
LTRESULT CUDPDriver::JoinSession(sockaddr_in *pAddr)
{
	sockaddr_in addr, localAddr, fromAddr, sockName;
	char addrStr[128];
	SOCKET theSocket;
	CPacketRef cRequest, cReply;
	CUDPConn *pConn;
	DWORD startTime, lastSendTime, curTime;
	int nBytes, nameLen, status;
	uint8 subID;

	addr = *pAddr;

	TermConnections(FALSE);

	if(!tcp_SetupLocalSockaddr(&localAddr, (uint16)g_CV_IPClientPort))
		return LT_ERROR;

	theSocket = tcp_BindToPort(this, &localAddr, &localAddr);
	sprintf(addrStr, "%d.%d.%d.%d:%d",
		pAddr->sin_addr.S_un.S_un_b.s_b1,
		pAddr->sin_addr.S_un.S_un_b.s_b2,
		pAddr->sin_addr.S_un.S_un_b.s_b3,
		pAddr->sin_addr.S_un.S_un_b.s_b4,
		ntohs(pAddr->sin_port));

	if(!theSocket)
	{
		RETURN_ERROR_PARAM(2, TCPDriver::JoinSession, LT_CANTBINDTOPORT, addrStr);
	}

	if(g_CV_IPDebug)
		dsi_ConsolePrint("Joining on port %d", ntohs(localAddr.sin_port));

	cRequest = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	cRequest->m_Data[0] = 0;
	cRequest->WriteType((uint8)TCPSUB_CONNECTREQUEST);
	cRequest->WriteType((uint8)0);
	cRequest->WriteRaw(&m_pNetMgr->m_guidApp, sizeof(LTGUID));

	cReply = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	startTime = timeGetTime();
	lastSendTime = startTime - 300;
	do
	{
		curTime = timeGetTime();

		// Send the request every 300ms.
		if(curTime - lastSendTime >= 300)
		{
			if(g_CV_IPDebug)
			{
				dsi_ConsolePrint("IP: Sending conn request to %d.%d.%d.%d:%d",
					addr.sin_addr.S_un.S_un_b.s_b1,
					addr.sin_addr.S_un.S_un_b.s_b2,
					addr.sin_addr.S_un.S_un_b.s_b3,
					addr.sin_addr.S_un.S_un_b.s_b4,
					ntohs(addr.sin_port));
			}

			SendTo(theSocket, cRequest->m_Data.GetArray(), cRequest->m_DataLen, &addr);
			lastSendTime = curTime;
		}

		nBytes = tcp_RecvFrom(this, theSocket, cReply->m_Data.GetArray(), cReply->m_Data.GetSize(), &fromAddr);
		if(nBytes == -1 || nBytes == 0)
			continue;

		if(g_CV_IPDebug)
			dsi_ConsolePrint("IP: Got PacketID %d (len %d) while connecting", cReply->m_Data[0], nBytes);

		cReply->m_DataLen = (uint16)nBytes;
		cReply->m_Pos = 1;
		if(cReply->m_Data[0] != 0)
			continue;

		subID = cReply->ReadType((uint8*)0);
		if(cReply->m_ErrorFlags & PACKETERR_READOVERFLOW)
			continue;

		if(subID == TCPSUB_CONNECTACCEPTED)
		{
			if(g_CV_IPDebug)
			{
				dsi_ConsolePrint("IP: Connection to %d.%d.%d.%d:%d accepted",
					addr.sin_addr.S_un.S_un_b.s_b1,
					addr.sin_addr.S_un.S_un_b.s_b2,
					addr.sin_addr.S_un.S_un_b.s_b3,
					addr.sin_addr.S_un.S_un_b.s_b4,
					ntohs(addr.sin_port));

				dsi_ConsolePrint("recvfrom addr: %d.%d.%d.%d:%d",
					fromAddr.sin_addr.S_un.S_un_b.s_b1,
					fromAddr.sin_addr.S_un.S_un_b.s_b2,
					fromAddr.sin_addr.S_un.S_un_b.s_b3,
					fromAddr.sin_addr.S_un.S_un_b.s_b4,
					ntohs(fromAddr.sin_port));
			}

			if(cReply->m_ErrorFlags & PACKETERR_READOVERFLOW)
				continue;

			pConn = new CUDPConn;
			if(!pConn)
				continue;

			pConn->m_Addr = fromAddr;
			pConn->m_bConnected = TRUE;
			pConn->m_pDriver = this;
			if(!m_pNetMgr->NewConnectionNotify(pConn))
			{
				delete pConn;
				continue;
			}

			m_Connections.AddHead(pConn, &pConn->m_Link);
			m_Socket = theSocket;

			if(g_CV_IPDebug)
			{
				nameLen = sizeof(sockName);
				status = getsockname(theSocket, (sockaddr*)&sockName, &nameLen);
				if(status == 0)
				{
					dsi_ConsolePrint("getsockname() returned %d.%d.%d.%d:%d",
						sockName.sin_addr.S_un.S_un_b.s_b1,
						sockName.sin_addr.S_un.S_un_b.s_b2,
						sockName.sin_addr.S_un.S_un_b.s_b3,
						sockName.sin_addr.S_un.S_un_b.s_b4,
						ntohs(sockName.sin_port));
				}
				else
				{
					dsi_ConsolePrint("getsockname() returned error %d", status);
				}
			}

			return LT_OK;
		}
		else if(subID == TCPSUB_CONNECTREJECTED)
		{
			if(g_CV_IPDebug)
			{
				dsi_ConsolePrint("IP: Connection to %d.%d.%d.%d:%d rejected",
					addr.sin_addr.S_un.S_un_b.s_b1,
					addr.sin_addr.S_un.S_un_b.s_b2,
					addr.sin_addr.S_un.S_un_b.s_b3,
					addr.sin_addr.S_un.S_un_b.s_b4,
					ntohs(addr.sin_port));
			}

			closesocket(theSocket);
			GENERATE_ERROR(2, TCPDriver::JoinSession, LT_REJECTED, addrStr);
			return LT_REJECTED;
		}
		else if(subID == TCPSUB_NOTSAMEGUID)
		{
			if(g_CV_IPDebug)
			{
				dsi_ConsolePrint("IP: %d.%d.%d.%d:%d not using same app LTGUID",
					addr.sin_addr.S_un.S_un_b.s_b1,
					addr.sin_addr.S_un.S_un_b.s_b2,
					addr.sin_addr.S_un.S_un_b.s_b3,
					addr.sin_addr.S_un.S_un_b.s_b4,
					ntohs(addr.sin_port));
			}

			closesocket(theSocket);
			GENERATE_ERROR(2, TCPDriver::JoinSession, LT_NOTSAMEGUID, addrStr);
			return LT_NOTSAMEGUID;
		}
	}
	while(curTime - startTime < 10000);

	GENERATE_ERROR(2, TCPDriver::JoinSession, LT_TIMEOUT, addrStr);
	return LT_TIMEOUT;
}


// FUNCTION: LITHTECH 0x00499d20
LTRESULT CUDPDriver::ConnectTCP(char* sAddress)
{
	sockaddr_in addr;

	if(!tcp_BuildSockaddrFromString(sAddress, &addr))
	{
		RETURN_ERROR(1, ConnectTCP, LT_CANTBINDTOPORT);
	}

	return JoinSession(&addr);
}


// FUNCTION: LITHTECH 0x00499d90
LTRESULT CUDPDriver::SendTcpIp(void *pData, uint32 dataLen, char *sAddr, uint32 port)
{
	sockaddr_in addr;

	if(!m_Socket)
		return LT_NOTINITIALIZED;

	if(!tcp_BuildSockaddrFromString(sAddr, &addr))
		return LT_ERROR;

	addr.sin_port = htons((u_short)port);
	SendTo(m_Socket, pData, dataLen, &addr);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00499e10
CBaseDriver* udp_CreateDriver()
{
	return new CUDPDriver;
}


// Template code this object instantiated first:
// FUNCTION: LITHTECH 0x00499e30 ?GenGetNext@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UBE?AVCUDPQuery@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00499ea0 ?GenGetAt@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UBE?AVCUDPQuery@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00499f10 ?GenAppend@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UAEHAAVCUDPQuery@@@Z
// FUNCTION: LITHTECH 0x0049a0e0 ?GenRemoveAt@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0049a2b0 ?GenRemoveAll@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x0049a310 ?GenCopyList@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UAEHABV?$GenList@VCUDPQuery@@@@@Z
// FUNCTION: LITHTECH 0x0049a4a0 ?GenAppendList@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UAEHABV?$GenList@VCUDPQuery@@@@@Z
// FUNCTION: LITHTECH 0x0049a640 ?GenFindElement@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@UBEHABVCUDPQuery@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0049a670 ?SetSize2@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0049a780 ?InternalNiceSetSize@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0049a960 ??0CUDPQuery@@QAE@XZ
// FUNCTION: LITHTECH 0x0049a9a0 ??1CUDPQuery@@QAE@XZ
// FUNCTION: LITHTECH 0x0049a9c0 ?Insert2@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@QAEHKABVCUDPQuery@@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0049ac10 ?_DeleteAndDestroyArray@?$CMoArray@VCUDPQuery@@VDefaultCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x0049ac70 ?InsertBefore@?$CMultiLinkList@PAVCUDPConn@@@@QAEPAVCMLLNode@@PAV2@PAVCUDPConn@@0@Z
// FUNCTION: LITHTECH 0x0049acb0 ?BaseDelete@@YAXPAVLAlloc@@PAVCUDPQuery@@K@Z
// FUNCTION: LITHTECH 0x0049ad00 ?BaseNew@@YAPAVCUDPQuery@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x0049ad70 ??_GCUDPQuery@@QAEPAXI@Z
