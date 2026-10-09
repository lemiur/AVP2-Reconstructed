// Jupiter runtime/server/src/s_client.cpp: all the server-side client handling functions.
// Talon passes the server manager explicitly, uses the ref-counted CPacket, and also sends the
// world's light animations to the clients.
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "s_client.h"
#include "s_object.h"
#include "serverevent.h"
#include "soundtrack.h"
#include "ftserv.h"
#include "dhashtable.h"
#include "impl_common.h"
#include "packet.h"
#include "de_memory.h"
#include "streamsim.h"
#include "model.h"
#include "concommand.h"
#include "netmgr.h"
#include "world_tree.h"
#include <math.h>
#include "ltengineobjects.h"
#include "engine_vars.h"
#include "visquery.h"

// Talon server-to-client packet IDs.
#define SMSG_UPDATE				8
#define SMSG_NETPROTOCOLVERSION	4
#define SMSG_UNLOADWORLD		5
#define SMSG_LOADWORLD			6
#define SMSG_CLIENTOBJECTID		7
#define SMSG_YOURID				12

#define LT_NET_PROTOCOL_VERSION	6
#define SMSG_SKYDEF				17
#define SMSG_PACKETGROUP		14
#define SMSG_PRELOADLIST		21
#define SMSG_LIGHTANIMS			25
#define SMSG_PORTALFLAGS		19
#define SMSG_CONSOLEVAR			16
#define SMSG_GLOBALLIGHT		26

// Preload list types (SMSG_PRELOADLIST).
#define PRELOADTYPE_START		0
#define PRELOADTYPE_END			1
#define PRELOADTYPE_MODEL		2
#define PRELOADTYPE_TEXTURE		3
#define PRELOADTYPE_SPRITE		4
#define PRELOADTYPE_SOUND		5

// File types (de_codes.h).
#define FT_MODEL				0
#define FT_SPRITE				1
#define FT_TEXTURE				2
#define FT_SOUND				3

// Light animation change flags (LightAnimChange::m_ChangeFlags).
#define LIGHTANIMF_FRAMES		(1<<0)
#define LIGHTANIMF_PERCENT		(1<<1)
#define LIGHTANIMF_BLEND		(1<<2)
#define LIGHTANIMF_POS			(1<<3)
#define LIGHTANIMF_COLOR		(1<<4)
#define LIGHTANIMF_RADIUS		(1<<5)
#define LIGHTANIMF_SHADOWMAP	(LIGHTANIMF_POS | LIGHTANIMF_COLOR | LIGHTANIMF_RADIUS)
#define LIGHTANIMF_ALL			0xFF

#define SERV_RUNNINGWORLD		0

#define SIFLAG_LOCAL			(1<<1)	// A client is connected locally.

#define OBJINFOSOUNDF_CLIENTDONE	(1<<0)

#define IFLAG_INACTIVE			(1<<3)
#define IFLAG_INACTIVE_TOUCH	(1<<4)
#define IFLAG_AUTODEACTIVATED	(1<<5)
#define IFLAG_INACTIVE_TICK		(1<<9)

void		sm_ResetDeactivateTimer(LTObject *pObj);	// servermgr, 0x004867f0

#define CFLAG_SENDSKYDEF		(1<<5)
#define CFLAG_SENDGLOBALLIGHT	(1<<10)

#define IFLAG_INSKY				(1<<8)

void		clienthack_UnloadWorld();								// clientshell, 0x00416720

// What sm_UpdateClientInWorld passes around (Jupiter's UpdateInfo).
struct UpdateInfo
{
	CServerMgr	*m_pServerMgr;		// 0x00
	Client		*m_pClient;			// 0x04
	CPacketRef	m_cPacket;			// 0x08 guaranteed update packet
	CPacketRef	m_cUnguaranteed;	// 0x0c
	CPacketRef	m_cGroups[2];		// 0x10 the same two packets, by MESSAGE_GUARANTEED (unguaranteed, guaranteed)
	uint32		m_nPacketsSent;		// 0x18
	LTBOOL		m_bAutoActivate;	// 0x1c CFLAG_AUTOACTIVATEOBJECTS
};

#define ID_TIMESTAMP	0xFFFF

#define SMSG_UNGUARANTEEDUPDATE	10

// Update sub-packet types (the byte after the 0 that starts a sub-packet).
#define UPDATESUB_OBJECTREMOVES	3

// Unguaranteed update flags (the object ID shares the word).
#define UUF_ANIMINFO	0x1000
#define UUF_ROT			0x2000
#define UUF_POS			0x4000
#define UUF_YROTATION	0x8000

void		WriteUnguaranteedInfo(UpdateInfo *pInfo, LTObject *pObject, ObjInfo *pObjInfo);	// 0x00471ca0

// The list of objects sent this update (one of the client's m_SentLists).
// GLOBAL: LITHTECH 0x004e49ec
SentList *g_pCurSentList;




void		sm_SendSoundTracks(UpdateInfo *pInfo, CPacket *pPacket);	// 0x00472db0

static LTRESULT	sm_SendCacheListSection(CServerMgr *pServerMgr, Client *pClient, uint32 nStartIndex,
	CPacketRef &cPacket, uint8 nPacketID, uint16 nFileType);


// Sends the cached file list to the client
// FUNCTION: LITHTECH 0x0046f6a0
LTRESULT sm_SendCacheListToClient(CServerMgr *pServerMgr, Client *pClient, uint32 nStartIndex)
{
	CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Add models
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_MODEL, FT_MODEL);

	// Add sprites.
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_SPRITE, FT_SPRITE);

	// Add textures.
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_TEXTURE, FT_TEXTURE);

	// Add sounds.
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_SOUND, FT_SOUND);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0046f730
// Inline budget: the original calls the first two WriteTypes out of line.
static LTRESULT sm_SendCacheListSection(CServerMgr *pServerMgr, Client *pClient, uint32 nStartIndex,
	CPacketRef &cPacket, uint8 nPacketID, uint16 nFileType)
{
	LTBOOL bPacketEmpty;
	uint32 i;

	bPacketEmpty = TRUE;
	for (i=nStartIndex; i < pServerMgr->m_CacheListSize; i++)
	{
		if (pServerMgr->m_CacheList[i].m_FileType == nFileType)
		{
			if (bPacketEmpty)
			{
				cPacket->WriteType(nPacketID);
				bPacketEmpty = FALSE;
			}

			cPacket->WriteType((uint16)pServerMgr->m_CacheList[i].m_FileID);

			// Send it when it gets full.
			if (cPacket->GetSpaceLeft() < 50)
			{
				SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
				cPacket->ResetWrite();
				cPacket->WriteType(nPacketID);
			}
		}
	}

	// Send what's left.
	if (!bPacketEmpty)
	{
		SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
		cPacket->ResetWrite();
	}

	return LT_OK;
}


// Writes a light animation's changed state.
// pPacket->GetMessageImpl() (not &pPacket->m_Message) is the pending inline site that keeps the third WriteType out
// of line.
// FUNCTION: LITHTECH 0x0046f8e0
static void sm_WriteLightAnimInfo(CServerMgr *pServerMgr, LightAnim *pAnim, uint16 iLightAnim,
	CPacket *pPacket, uint32 flags)
{
	// Only shadow map lights have a position, color and radius.
	if (!pAnim->m_bShadowMap)
		flags &= ~LIGHTANIMF_SHADOWMAP;

	pPacket->WriteType(iLightAnim);
	pPacket->WriteType((uint8)flags);

	if (flags & LIGHTANIMF_FRAMES)
	{
		pPacket->WriteType((uint16)pAnim->m_iFrames[0]);
		pPacket->WriteType((uint16)pAnim->m_iFrames[1]);
	}

	if (flags & LIGHTANIMF_PERCENT)
		pPacket->WriteType((uint8)pAnim->m_PercentBetween);

	if (flags & LIGHTANIMF_BLEND)
		pPacket->WriteType((uint8)(pAnim->m_fBlendPercent * 255.0f));

	if (flags & LIGHTANIMF_POS)
		ic_WriteCompPos(pPacket->GetMessageImpl(), &pAnim->m_vLightPos, &pServerMgr->m_World);

	if (flags & LIGHTANIMF_COLOR)
	{
		pPacket->WriteType((uint8)pAnim->m_vLightColor.x);
		pPacket->WriteType((uint8)pAnim->m_vLightColor.y);
		pPacket->WriteType((uint8)pAnim->m_vLightColor.z);
	}

	if (flags & LIGHTANIMF_RADIUS)
		pPacket->WriteType(pAnim->m_fLightRadius);
}


// Sends all the world's light animations.
// FUNCTION: LITHTECH 0x0046fc40
void sm_SendAllLightAnims(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;
	uint32 i;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	for (i=0; i < pServerMgr->m_World.m_LightAnims.GetSize(); i++)
	{
		sm_WriteLightAnimInfo(pServerMgr, &pServerMgr->m_World.m_LightAnims[i], (uint16)i, pPacket, LIGHTANIMF_ALL);

		if (pPacket->GetSpaceLeft() <= 22)
		{
			SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
			pPacket->ResetWrite();
		}
	}

	if (pPacket->m_DataLen - 1 > 0)
	{
		SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
		pPacket->ResetWrite();
	}

	pPacket->Release();
}


// Sends the light animations that changed since the last update.
// The loop counter lives in pServerMgr's dead argument home: VC6 gives that home to the least used parameter, and the
// light animation array reference keeps pServerMgr's use count below pClient's.
// FUNCTION: LITHTECH 0x0046fd30
void sm_SendChangedLightAnims(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;
	uint32 i, iLightAnim;
	LightAnimChange *pChange;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	CMoArray<LightAnim> &anims = pServerMgr->m_World.m_LightAnims;

	for (i=0; i < pClient->m_nLightAnimChanges; i++)
	{
		pChange = &pClient->m_LightAnimChanges[i];
		iLightAnim = pChange->m_iLightAnim;
		if (iLightAnim < anims.GetSize())
		{
			sm_WriteLightAnimInfo(pServerMgr, &anims[iLightAnim], (uint16)iLightAnim,
				pPacket, pChange->m_ChangeFlags);

			if (pPacket->GetSpaceLeft() <= 22)
			{
				SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
				pPacket->ResetWrite();
			}
		}
	}

	if (pPacket->m_DataLen - 1 > 0)
	{
		SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
		pPacket->ResetWrite();
	}

	pClient->m_nLightAnimChanges = 0;
	pPacket->Release();
}




// Gets the client the rest of the way into the world once it has loaded the world (and preloaded its files).
// FUNCTION: LITHTECH 0x0046fe50
LTRESULT sm_UpdatePuttingInWorld(CServerMgr *pServerMgr, Client *pClient)
{
	CPacketRef cPacket;
	uint32 iWorld, iPortal;
	WorldBsp *pBsp;
	LPBASECLASS pObject;
	LTLink *pCur, *pListHead;
	ClientRef *pRef;
	CSoundTrack *pSoundTrack;
	ObjInfo *pInfo;

	if (pClient->m_PuttingIntoWorldStage == PUTTINGINTOWORLD_LOADEDWORLD ||
		(pClient->m_ClientFlags & CFLAG_LOCAL))
	{
		cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

		// Tell them the state of the portals.
		for (iWorld=0; iWorld < pServerMgr->m_World.m_WorldModels.GetSize(); iWorld++)
		{
			pBsp = pServerMgr->m_World.m_WorldModels[iWorld]->m_pOriginalBsp;
			for (iPortal=0; iPortal < pBsp->m_nPortals; iPortal++)
			{
				if (pBsp->m_Portals[iPortal].m_Flags)
				{
					cPacket->ResetWrite();
					cPacket->WriteType((uint16)iWorld);
					cPacket->WriteType((uint16)iPortal);
					cPacket->WriteType((uint8)pBsp->m_Portals[iPortal].m_Flags);
					SendToClient(pServerMgr, pClient, SMSG_PORTALFLAGS, cPacket, FALSE, MESSAGE_GUARANTEED);
				}
			}
		}

		// Tell them about the light animations.
		sm_SendAllLightAnims(pServerMgr, pClient);
		pClient->m_nLightAnimChanges = 0;

		// Tell them all the stuff they need to preload.
		sm_TellClientToPreloadStuff(pServerMgr, pClient);

		// Tell them about the sky.
		sm_TellClientAboutSky(pServerMgr, pClient);

		pClient->m_PuttingIntoWorldStage = PUTTINGINTOWORLD_PRELOADING;
	}

	if (pClient->m_PuttingIntoWorldStage == PUTTINGINTOWORLD_PRELOADED ||
		(pClient->m_ClientFlags & CFLAG_LOCAL))
	{
		cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

		// Call OnClientEnterWorld and setup the client's object.
		pObject = pServerMgr->m_ClassMgr.m_pServerShell->OnClientEnterWorld((HCLIENT)pClient,
			pClient->m_pClientData, pClient->m_ClientDataLen);

		if (!pObject)
		{
			dsi_ConsolePrint("Error: ServerShell::OnClientEnterWorld returned LTNULL!");
			return LT_ERROR;
		}

		pClient->m_pObject = (LTObject*)pObject->m_hObject;
		pClient->m_pObject->sd->m_pClient = pClient;

		// If they used a ClientRef's object, remove the ClientRef.
		pListHead = &pServerMgr->m_ClientReferences.m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pRef = (ClientRef*)pCur->m_pData;

			if (pRef->m_ObjectID == pClient->m_pObject->m_ObjectID)
			{
				pClient->m_pObject->m_InternalFlags &= ~IFLAG_HASCLIENTREF;

				dl_RemoveAt(&pServerMgr->m_ClientReferences, &pRef->m_Link);

				dfree(pRef);
				break;
			}
		}

		// Tell this client (and its attachments) to use the object ID if it
		// doesn't have an attachment parent.
		if (!pClient->m_pAttachmentParent)
		{
			cPacket->ResetWrite();
			cPacket->WriteType(pClient->m_pObject->m_ObjectID);
			SendToClient(pServerMgr, pClient, SMSG_CLIENTOBJECTID, cPacket, TRUE, MESSAGE_GUARANTEED);
		}

		// Reset their sent lists.
		pClient->m_SentLists[0].m_nObjectIDs = pClient->m_SentLists[1].m_nObjectIDs = 0;

		// Mark all the objects as new and find out what the client needs to be sent.
		pListHead = &pServerMgr->m_Objects.m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pObject = (LPBASECLASS)pCur->m_pData;
			pInfo = &pClient->m_ObjInfos[((LTObject*)pObject)->m_ObjectID];
			pInfo->m_ChangeFlags = (uint16)sm_GetNewObjectChangeFlags(pServerMgr, (LTObject*)pObject);
		}

		// Mark all the soundtracks as new and find out what the client needs to be sent.
		for (pCur=pServerMgr->m_SoundTrackList.m_Head.m_pNext; pCur != &pServerMgr->m_SoundTrackList.m_Head;
			pCur=pCur->m_pNext)
		{
			pSoundTrack = (CSoundTrack*)pCur->m_pData;

			pInfo = &pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)];
			pInfo->m_ChangeFlags = CF_NEWOBJECT | CF_POSITION;
			pInfo->m_nSoundFlags = 0;
		}

		pClient->m_State = CLIENT_INWORLD;
	}

	return LT_OK;
}


// Sends the model file IDs (Jupiter's CServerMgr::SendPreloadModelMsgToClient).
inline void sm_SendPreloadModelMsgToClient(CServerMgr *pServerMgr, Client *pClient, CPacketRef &cPacket, HHashIterator *&hIterator)
{
	cPacket->WriteType((uint8)PRELOADTYPE_MODEL);

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		cPacket->WriteType((uint16)((Model*)hs_GetElementUserData(hs_GetNextElement(hIterator)))->m_FileID);

		// Send it when it gets full.
		if (cPacket->GetSpaceLeft() < 50)
		{
			SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
			cPacket->ResetWrite();
			cPacket->WriteType((uint8)PRELOADTYPE_MODEL);
		}
	}

	SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
	cPacket->ResetWrite();

}


// Goes thru the current level and tells the client about everything it should preload.
// Wave 5: only register differences in the sound-list loop (pCur->m_pNext into edx not eax, the file id
// through eax not ecx). inline_scan p1/p2/b8/b16 over every statement finds no improvement; tried a UsedFile
// local, a nested if for GetFile(): no change. Wave 6: nested IsTouched/GetFile ifs, a UsedFile local in the
// condition, a uint16 file id local, m_pFile, Jupiter's in-loop declarations of pSoundData/pCur, reversed
// local declarations: all exactly 24 aligned (the register choice ignores these).
// Wave 7 phase 2: audit: behaviour matches. 24 aligned: register naming in the sound-list loop only (+0x123..
// +0x221: edx/eax/ecx permuted).
// PARKED: register naming in the sound-list loop only (24 aligned); behaviour identical; inline_scan and many local forms tried
// STUB: LITHTECH 0x00470230
LTRESULT sm_TellClientToPreloadStuff(CServerMgr *pServerMgr, Client *pClient)
{
	CPacketRef cPacket;
	HHashIterator *hIterator;
	LTLink *pCur;
	CSoundData *pSoundData;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Tell the client the preload is starting.
	cPacket->WriteType((uint8)PRELOADTYPE_START);
	SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
	cPacket->ResetWrite();

	// Send all the other files in the cache list.
	sm_SendCacheListToClient(pServerMgr, pClient, 0);

	// Send the models.
	sm_SendPreloadModelMsgToClient(pServerMgr, pClient, cPacket, hIterator);

	// Send the sounds that weren't in the cache list.
	cPacket->WriteType((uint8)PRELOADTYPE_SOUND);

	pCur = pServerMgr->m_SoundDataList.m_Head.m_pNext;
	while (pCur != &pServerMgr->m_SoundDataList.m_Head)
	{
		pSoundData = (CSoundData*)pCur->m_pData;

		pCur = pCur->m_pNext;

		if (!pSoundData)
			continue;

		if (pSoundData->IsTouched() && pSoundData->GetFile())
			cPacket->WriteType((uint16)pSoundData->GetFile()->m_FileID);

		// Send it when it gets full.
		if (cPacket->GetSpaceLeft() < 50)
		{
			SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
			cPacket->ResetWrite();
			cPacket->WriteType((uint8)PRELOADTYPE_SOUND);
		}
	}

	SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
	cPacket->ResetWrite();

	// Mark the end and send away.
	cPacket->WriteType((uint8)PRELOADTYPE_END);
	SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);

	return LT_OK;
}


// The out-of-line copy of CPacket::GetSpaceLeft (packet.h); sm_TellClientToPreloadStuff keeps its calls out of line.
// FUNCTION: LITHTECH 0x00470490 ?GetSpaceLeft@CPacket@@QAEJXZ

int sm_FTCantOpenFileFn(FTServ *hServ, char *pFilename);	// 0x004a02b0 (shared return 1)


// ----------------------------------------------------------------------- //
// Interface functions.
// ----------------------------------------------------------------------- //

ILTStream*	sm_FTOpenFn(FTServ *hServ, char *pFilename);	// 0x00470bb0
void		sm_FTCloseFn(FTServ *hServ, ILTStream *pStream);	// 0x00470bd0
void		clienthack_ModelLoaded(Model *pModel);	// clientshell, 0x00416750

// Jupiter's Client::Client (inlined into sm_OnNewConnection).
inline Client::Client()
{
	int i, j;

	m_Link.TieOff();

	LightAnimChange *pChange = m_LightAnimChanges;
	for (i=0; i < MAX_LIGHTANIM_CHANGES; i++)
	{
		pChange->m_iLightAnim = 0;
		pChange->m_ChangeFlags = 0;
		pChange++;
	}

	m_nLightAnimChanges = 0;
	m_pClientData = LTNULL;
	m_ClientDataLen = 0;
	m_Unknown128 = 0.0f;
	m_Unknown12C = 0.0f;
	m_Unknown130 = 0;

	for (i=0; i < 2; i++)
	{
		for (j=0; j < 2; j++)
		{
			m_PacketBufs[i][j].Term();
		}
	}

	m_AttachmentLink.TieOff();
	m_pAttachmentParent = LTNULL;
	m_ViewPos.Init(0.0f, 0.0f, 0.0f);
	m_Attachments.TieOff();
	m_hFTServ = LTNULL;
	m_ObjInfos = LTNULL;
	m_SentLists[0].m_nObjectIDs = 0;
	m_SentLists[0].m_AllocatedSize = 0;
	m_SentLists[0].m_ObjectIDs = LTNULL;
	m_SentLists[1].m_nObjectIDs = 0;
	m_SentLists[1].m_AllocatedSize = 0;
	m_SentLists[1].m_ObjectIDs = LTNULL;
	m_iPrevSentList = 0;

	for (i=0; i < MAX_CLIENT_COMMANDS; i++)
	{
		m_Commands[0][i] = 0;
		m_Commands[1][i] = 0;
	}

	m_iCurCommands = 0;
	m_pObject = LTNULL;
	m_pPluginUserData = LTNULL;
	m_ClientID = 0;
	m_State = 0;
	m_PuttingIntoWorldStage = 0;
	m_ClientFlags = 0;
	m_Name = LTNULL;
	m_ConnectionID = LTNULL;
	m_hFileIDTable = LTNULL;
	m_Unknown3E0 = 0;
	m_Unknown3E4 = 0;
}


// A new connection came in: creates the client, tells it the protocol version and its ID, and starts
// its file transfer.
// FUNCTION: LITHTECH 0x004704b0
Client* sm_OnNewConnection(CServerMgr *pServerMgr, CBaseConn *id, LTBOOL bIsLocal)
{
	Client *pClient;
	CPacketRef cPacket;
	ClientPacketBuf *pBuf;
	FTSInitStruct initStruct;
	HHashIterator *hIterator;
	HHashElement *hElement;
	UsedFile *pUsedFile;
	LTLink *pCur, *pListHead;
	uint16 testID;
	uint32 i, j, flags;
	LTBOOL bUnique;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Clear the local flag if remote is forced.
	if (g_bForceRemote)
		bIsLocal = FALSE;

	// Add the client to the internal structures.
	pClient = new Client;

	if (bIsLocal)
	{
		pClient->m_ClientFlags |= CFLAG_LOCAL;
		pServerMgr->m_InternalFlags |= SIFLAG_LOCAL;
	}

	pClient->m_Timer.SetUpdateRate(10.0f);
	pClient->m_Unknown128 = 10.0f;

	pClient->m_ClientFlags |= CFLAG_SENDCOBJROTATION;
	pClient->m_State = CLIENT_WAITINGTOENTERWORLD;
	pClient->m_Name = (char*)dalloc(1);
	pClient->m_Name[0] = 0;
	pClient->m_AttachmentLink.m_pData = pClient;

	pClient->m_Attachments.TieOff();
	dl_InitList(&pClient->m_Events);

	for (i=0; i < 2; i++)
	{
		for (j=0; j < 2; j++)
		{
			pBuf = &pClient->m_PacketBufs[i][j];
			pBuf->m_pPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			pBuf->m_Unknown8 = (i == 1) ? MESSAGE_GUARANTEED : 0;
			pBuf->m_Unknown4 = (j == 0) ? 5.0f : 15.0f;
		}
	}

	// Setup their file transfer.
	flags = 0;
	if (bIsLocal)
		flags |= FTSFLAG_LOCAL;

	initStruct.m_OpenFn = sm_FTOpenFn;
	initStruct.m_CloseFn = sm_FTCloseFn;
	initStruct.m_CantOpenFileFn = sm_FTCantOpenFileFn;
	initStruct.m_pNetMgr = &pServerMgr->m_NetMgr;
	initStruct.m_ConnID = id;
	pClient->m_hFTServ = fts_Init(&initStruct, flags);
	fts_SetUserData1(pClient->m_hFTServ, pServerMgr);

	pClient->m_Link.m_pData = pClient;
	dl_AddHead(&pServerMgr->m_Clients, &pClient->m_Link, pClient);

	pClient->m_ObjInfos = (ObjInfo*)dalloc(sizeof(ObjInfo) * pServerMgr->m_nObjInfos);
	memset(pClient->m_ObjInfos, 0, sizeof(ObjInfo) * pServerMgr->m_nObjInfos);

	pClient->m_ConnectionID = id;
	pClient->m_pObject = LTNULL;
	pClient->m_Unknown3E0 = 0;
	pClient->m_Unknown3E4 = 0;
	pClient->m_Unknown12C = 35.0f;

	// Find a free ID.
	pClient->m_ClientID = 0;
	for (testID=0; testID < 30000; testID++)
	{
		bUnique = LTTRUE;

		pListHead = &pServerMgr->m_Clients.m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			if (((Client*)pCur->m_pData)->m_ClientID == testID)
			{
				bUnique = LTFALSE;
				break;
			}
		}

		if (bUnique)
			break;
	}

	pClient->m_ClientID = testID;

	// Initialize the fileid info list.
	pClient->m_hFileIDTable = hs_CreateHashTable(500, HASH_2BYTENUMBER);

	dsi_ConsolePrint("New client, id %d, for a total of %d.", pClient->m_ClientID, pServerMgr->m_Clients.m_nElements);

	// Tell the server shell they're in.
	pServerMgr->m_ClassMgr.m_pServerShell->OnAddClient((HCLIENT)pClient);

	// MUST BE FIRST.  Send them the protocol version.
	cPacket->ResetWrite();
	cPacket->WriteType((uint32)LT_NET_PROTOCOL_VERSION);
	SendToClient(pServerMgr, pClient, SMSG_NETPROTOCOLVERSION, cPacket, FALSE, MESSAGE_GUARANTEED);

	// Send them their ID.
	cPacket->ResetWrite();
	cPacket->WriteType(pClient->m_ClientID);
	cPacket->WriteType((uint8)bIsLocal);
	SendToClient(pServerMgr, pClient, SMSG_YOURID, cPacket, FALSE, MESSAGE_GUARANTEED);

	// Tell the file transfer about the files.
	hIterator = hs_GetFirstElement(pServerMgr->m_FileMgr.m_hFileTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		pUsedFile = (UsedFile*)hs_GetElementUserData(hElement);

		fts_AddFile(pClient->m_hFTServ, pUsedFile->GetFilename(), pUsedFile->m_FileSize, pUsedFile->m_FileID,
			(uint16)(pUsedFile->m_Flags | FFLAG_SENDWAIT));
	}
	fts_FlushAddedFiles(pClient->m_hFTServ);

	// A local client already has the models.
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
		while (hIterator)
		{
			clienthack_ModelLoaded((Model*)hs_GetElementUserData(hs_GetNextElement(hIterator)));
		}
	}

	// Now set them up to try to get into the world, after their file transfer has completed.
	sm_SetClientState(pServerMgr, pClient, CLIENT_WAITINGTOENTERWORLD);

	return pClient;
}


// FUNCTION: LITHTECH 0x00470bb0
ILTStream* sm_FTOpenFn(FTServ *hServ, char *pFilename)
{
	CServerMgr *pServerMgr = (CServerMgr*)fts_GetUserData1(hServ);
	return sf_OpenFile(&pServerMgr->m_FileMgr, pFilename);
}


// FUNCTION: LITHTECH 0x00470bd0
void sm_FTCloseFn(FTServ *hServ, ILTStream *pStream)
{
	pStream->Release();
}


// FUNCTION: LITHTECH 0x00470be0
void sm_OnBrokenConnection(CServerMgr *pServerMgr, CBaseConn *id)
{
	Client *pClient = sm_FindClient(pServerMgr, id);

	if (pClient)
	{
		sm_RemoveClient(pServerMgr, pClient);
	}
}


// FUNCTION: LITHTECH 0x00470c10
LTRESULT sm_AttachClient(CServerMgr *pServerMgr, Client *pParent, Client *pChild)
{
	CPacket *pPacket;

	if (pParent == pChild)
	{
		RETURN_ERROR(1, sm_AttachClient, LT_INVALIDPARAMS);
	}

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	sm_DetachClient(pServerMgr, pChild);

	pChild->m_pAttachmentParent = pParent;
	dl_Insert(&pParent->m_Attachments, &pChild->m_AttachmentLink);

	// Tell the client to use the new object ID.
	if (pParent->m_pObject)
	{
		pPacket->ResetWrite();
		pPacket->WriteType(pParent->m_pObject->m_ObjectID);
		SendToClient(pServerMgr, pChild, SMSG_CLIENTOBJECTID, pPacket, TRUE, MESSAGE_GUARANTEED);
	}

	if (pPacket)
		pPacket->Release();

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00470db0
LTRESULT sm_DetachClient(CServerMgr *pServerMgr, Client *pClient)
{
	// Talon never gets a packet here, so this writes through a null packet.
	CPacketRef cPacket;

	if (pClient->m_pAttachmentParent)
	{
		dl_Remove(&pClient->m_AttachmentLink);
		pClient->m_pAttachmentParent = LTNULL;

		// Tell the client to use its normal object.
		if (pClient->m_pObject)
		{
			cPacket->ResetWrite();
			cPacket->WriteType(pClient->m_pObject->m_ObjectID);
			SendToClient(pServerMgr, pClient, SMSG_CLIENTOBJECTID, cPacket, TRUE, MESSAGE_GUARANTEED);
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00470f10
LTRESULT sm_DetachClientChildren(CServerMgr *pServerMgr, Client *pClient)
{
	LTLink *pCur, *pNext;

	pCur = pClient->m_Attachments.m_pNext;
	while (pCur != &pClient->m_Attachments)
	{
		pNext = pCur->m_pNext;
		sm_DetachClient(pServerMgr, (Client*)pCur->m_pData);
		pCur = pNext;
	}

	return LT_OK;
}


static void sm_FreeClient(CServerMgr *pServerMgr, Client *pClient);

// FUNCTION: LITHTECH 0x00470f50
void sm_RemoveClient(CServerMgr *pServerMgr, Client *pClient)
{
	LTLink *pCur;
	CSoundTrack *pSoundTrack;
	ObjInfo *pInfo;

	dsi_ConsolePrint("Removing client, id %d, leaving %d.",
		pClient->m_ClientID, pServerMgr->m_Clients.m_nElements-1);

	// Undo all attachments.
	sm_DetachClient(pServerMgr, pClient);
	sm_DetachClientChildren(pServerMgr, pClient);

	// Remove the client reference in any sound tracks...
	for (pCur=pServerMgr->m_SoundTrackList.m_Head.m_pNext; pCur != &pServerMgr->m_SoundTrackList.m_Head;
		pCur=pCur->m_pNext)
	{
		pSoundTrack = (CSoundTrack*)pCur->m_pData;

		pInfo = &pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)];
		if (!(pInfo->m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE))
		{
			pSoundTrack->Release(&pInfo->m_nSoundFlags);
		}
	}

	// If they had a local connection, clear the server's local flag.
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		pServerMgr->m_InternalFlags &= ~SIFLAG_LOCAL;
	}

	// Get them out of the world..
	sm_SetClientState(pServerMgr, pClient, CLIENT_CONNECTED);

	// Notify the shell.
	pServerMgr->m_ClassMgr.m_pServerShell->OnRemoveClient((HCLIENT)pClient);

	// Remove the client.
	dl_RemoveAt(&pServerMgr->m_Clients, &pClient->m_Link);
	sm_FreeClient(pServerMgr, pClient);
}


// Frees up everything (Jupiter's Client::~Client).
// FUNCTION: LITHTECH 0x00471050
static void sm_FreeClient(CServerMgr *pServerMgr, Client *pClient)
{
	LTLink *pCur, *pNext;
	CServerEvent *pEvent;
	HHashIterator *hIterator;
	HHashElement *hElement;
	void *pInfo;

	fts_Term(pClient->m_hFTServ);

	pCur = pClient->m_Events.m_Head.m_pNext;
	while (pCur != &pClient->m_Events.m_Head)
	{
		pNext = pCur->m_pNext;
		pEvent = (CServerEvent*)pCur->m_pData;
		pEvent->DecrementRefCount();
		pCur = pNext;
	}

	dfree(pClient->m_ObjInfos);
	dfree(pClient->m_SentLists[0].m_ObjectIDs);
	dfree(pClient->m_SentLists[1].m_ObjectIDs);

	dfree(pClient->m_Name);

	// Free the fileid info structures...
	hIterator = hs_GetFirstElement(pClient->m_hFileIDTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		if (hElement)
		{
			pInfo = hs_GetElementUserData(hElement);
			sb_Free(&g_pServerMgr->m_BankCAC, pInfo);
		}
	}
	hs_DestroyHashTable(pClient->m_hFileIDTable);

	delete pClient->m_pClientData;

	delete pClient;
}


// FUNCTION: LITHTECH 0x00471160
LTBOOL sm_SetClientState(CServerMgr *pServerMgr, Client *pClient, int state)
{
	if (pClient->m_State == state)
		return TRUE;

	if (state == CLIENT_INWORLD)
	{
		if (sm_CanClientEnterWorld(pServerMgr, pClient))
		{
			sm_ConnectClientToWorld(pServerMgr, pClient);
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		// If they're in the world, remove them from the world.
		if (pClient->m_State == CLIENT_INWORLD)
		{
			sm_GetClientOutOfWorld(pServerMgr, pClient);
		}
	}

	pClient->m_State = state;
	return TRUE;
}


// FUNCTION: LITHTECH 0x004711d0
LTBOOL sm_CanClientEnterWorld(CServerMgr *pServerMgr, Client *pClient)
{
	if (pServerMgr->m_State != SERV_RUNNINGWORLD)
		return FALSE;

	if (!(pClient->m_ClientFlags & CFLAG_GOT_HELLO))
		return FALSE;

	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		return TRUE;
	}
	else
	{
		if (pClient->m_ClientFlags & CFLAG_WANTALLFILES)
		{
			return (fts_GetNumNeededFiles(pClient->m_hFTServ) == 0) &&
				(fts_GetNumTotalFiles(pClient->m_hFTServ) == 0);
		}
		else
		{
			return fts_GetNumNeededFiles(pClient->m_hFTServ) == 0;
		}
	}
}


// Tells the client to load the world (and its console state), and gets it into the world if it's local.
// FUNCTION: LITHTECH 0x00471250
LTRESULT sm_ConnectClientToWorld(CServerMgr *pServerMgr, Client *pClient)
{
	CPacketRef cPacket;
	HHashIterator *hIterator;
	HHashElement *hElement;
	LTCommandVar *pCurVar;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	if (pServerMgr->m_State == SERV_RUNNINGWORLD)
	{
		// Tell them to get rid of what they have.
		cPacket->ResetWrite();
		SendToClient(pServerMgr, pClient, SMSG_UNLOADWORLD, cPacket, FALSE, MESSAGE_GUARANTEED);

		// A local client inherits the server's world data.
		if (pClient->m_ClientFlags & CFLAG_LOCAL)
			g_pServerWorld = &pServerMgr->m_World;

		// Send the client the console state...
		hIterator = hs_GetFirstElement(pServerMgr->m_ConsoleState.m_VarHash);
		while (hIterator)
		{
			hElement = hs_GetNextElement(hIterator);
			if (!hElement)
				continue;

			pCurVar = (LTCommandVar*)hs_GetElementUserData(hElement);

			cPacket->ResetWrite();
			cPacket->WriteString(pCurVar->pVarName);
			cPacket->WriteString(pCurVar->pStringVal);
			SendToClient(pServerMgr, pClient, SMSG_CONSOLEVAR, cPacket, FALSE, MESSAGE_GUARANTEED);
		}

		// Let the shell look at the client.
		uint32 nVerifyCode = 0;
		pServerMgr->m_ClassMgr.m_pServerShell->VerifyClient((HCLIENT)pClient, pClient->m_pClientData, nVerifyCode);

		cPacket->ResetWrite();
		cPacket->WriteType(pServerMgr->m_GameTime);
		cPacket->WriteType((uint16)pServerMgr->m_pWorldFile->m_FileID);
		cPacket->WriteType(nVerifyCode);
		cPacket->WriteString(pServerMgr->m_CRCString);
		cPacket->WriteType(pServerMgr->m_StringCRC);
		cPacket->WriteType(pServerMgr->m_WorldCRC);
		SendToClient(pServerMgr, pClient, SMSG_LOADWORLD, cPacket, FALSE, MESSAGE_GUARANTEED);

		// Alrighty.. wait till we get the acknowledgement back.
		pClient->m_State = CLIENT_PUTTINGINWORLD;
		pClient->m_PuttingIntoWorldStage = PUTTINGINTOWORLD_LOADINGWORLD;

		// Get them in right away if they're local.
		if (pClient->m_ClientFlags & CFLAG_LOCAL)
		{
			sm_UpdatePuttingInWorld(pServerMgr, pClient);
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00471560
void sm_GetClientOutOfWorld(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	pServerMgr->m_ClassMgr.m_pServerShell->OnClientExitWorld((HCLIENT)pClient);

	if (pClient->m_pObject)
	{
		pClient->m_pObject->sd->m_pClient = LTNULL;
	}

	pClient->m_pObject = LTNULL;

	// Send them an UNLOADWORLD packet.
	pPacket->ResetWrite();
	SendToClient(pServerMgr, pClient, SMSG_UNLOADWORLD, pPacket, FALSE, MESSAGE_GUARANTEED);

	// If they're local, call the function to get them out of the world
	// so the local client removes its objects.
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		clienthack_UnloadWorld();
	}

	pPacket->Release();
}


// FUNCTION: LITHTECH 0x00471610
void sm_UpdateClientState(CServerMgr *pServerMgr, Client *pClient)
{
	// Try to put them in the world.
	if (pClient->m_State == CLIENT_WAITINGTOENTERWORLD)
	{
		sm_SetClientState(pServerMgr, pClient, CLIENT_INWORLD);
	}
	else if (pClient->m_State == CLIENT_PUTTINGINWORLD)
	{
		sm_UpdatePuttingInWorld(pServerMgr, pClient);
	}

	// Update any files they're transferring.
	sm_UpdateClientFileTransfer(pServerMgr, pClient);
}


// FUNCTION: LITHTECH 0x00471660
void sm_UpdateClientFileTransfer(CServerMgr *pServerMgr, Client *pClient)
{
	fts_Update(pClient->m_hFTServ, pServerMgr->m_TrueFrameTime);
}


// Sends the packet and starts a new one if it doesn't have room for nRoomNeeded more bytes
// (always if nRoomNeeded is -1). Returns TRUE if it sent the packet.
// Prints all the packet data into the packet.trc file.
// FUNCTION: LITHTECH 0x00471680
void sm_TracePacket(CServerMgr *pServerMgr, CPacket *pPacket)
{
	uint32 i;

	if (!g_CV_STracePackets)
		return;

	if (!pServerMgr->m_pTracePacketFile)
	{
		pServerMgr->m_pTracePacketFile = streamsim_Open("packet.trc", "wb");
		if (!pServerMgr->m_pTracePacketFile)
			return;
	}

	for (i=0; i < pPacket->m_DataLen; i++)
	{
		pServerMgr->m_pTracePacketFile->Write(&pPacket->m_Data.GetArray()[i], 1);
	}

	if (g_CV_DelimitPackets)
	{
		*pServerMgr->m_pTracePacketFile << '*' << 'E' << 'N' << 'D' << '*';
	}
}


// The count alias preserves VC6's original update order around the inlined WriteType.
// FUNCTION: LITHTECH 0x00471760
LTBOOL sm_FlushUpdate(UpdateInfo *pInfo, CPacket *pPacket, uint8 packetID, int nRoomNeeded)
{
	uint32 packetFlags;

	if (!(nRoomNeeded != -1 && pPacket->GetSpaceLeft() > nRoomNeeded + 4))
	{
		packetFlags = 0;
		if (packetID == SMSG_UPDATE)
			packetFlags = MESSAGE_GUARANTEED;

		sm_TracePacket(pInfo->m_pServerMgr, pPacket);
		SendToClient(pInfo->m_pServerMgr, pInfo->m_pClient, packetID, pPacket, FALSE, packetFlags);

		pPacket->ResetWrite();
		uint32 *pNPacketsSent = &pInfo->m_nPacketsSent;
		pPacket->WriteType((uint8)0);
		(*pNPacketsSent)++;
		return LTTRUE;
	}
	return LTFALSE;
}


inline LTBOOL ShouldSendToClient(CServerMgr *pServerMgr, LTObject *pObject)
{
	if (!(pObject->m_Flags & FLAG_FORCECLIENTUPDATE))
	{
		// See if this object can't be interacted with.
		if (!(pObject->m_Flags & (FLAG_VISIBLE | FLAG_SOLID | FLAG_RAYHIT)))
			return LTFALSE;

		// If it is a normal object type, only inform the client about it if it has a special
		// effect message.
		if (pObject->m_ObjectType == OT_NORMAL && !pObject->sd->m_pSFXMsg)
			return LTFALSE;
	}

	// This happens sometimes when an object removes another object in its
	// destructor.. not a big deal.
	if (!(pObject->m_InternalFlags & IFLAG_INWORLD))
		return LTFALSE;

	// Don't tell them about models that failed to load.
	if (pObject->m_ObjectType == OT_MODEL &&
		((ModelInstance*)pObject)->GetModelDB() == pServerMgr->m_pDefaultModel)
		return LTFALSE;

	return LTTRUE;
}


// Adds the object to g_pCurSentList.
inline void AddObjectIdToSentList(LTObject *pObject)
{
	uint16 *pNewIDs;

	if (g_pCurSentList->m_nObjectIDs >= g_pCurSentList->m_AllocatedSize)
	{
		pNewIDs = (uint16*)dalloc(sizeof(uint16) * (g_pCurSentList->m_AllocatedSize + 200));
		memcpy(pNewIDs, g_pCurSentList->m_ObjectIDs, sizeof(uint16) * g_pCurSentList->m_nObjectIDs);
		dfree(g_pCurSentList->m_ObjectIDs);
		g_pCurSentList->m_ObjectIDs = pNewIDs;
		g_pCurSentList->m_AllocatedSize += 200;
	}

	g_pCurSentList->m_ObjectIDs[g_pCurSentList->m_nObjectIDs] = pObject->m_ObjectID;
	g_pCurSentList->m_nObjectIDs++;
}


// Marks the object with CF_SENTINFO and sends any change info it has.
inline void sm_AddObjectChangeInfo(UpdateInfo *pInfo, LTObject *pObject, ObjInfo *pObjInfo)
{
	uint32 size;

	// Check if we already sent this.
	if (pObjInfo->m_ChangeFlags & CF_SENTINFO)
		return;

	AddObjectIdToSentList(pObject);

	size = 0;
	FillPacketFromInfo(pInfo->m_pServerMgr, pInfo->m_pClient, pObject, pObjInfo, LTNULL, &size);
	sm_FlushUpdate(pInfo, pInfo->m_cPacket, SMSG_UPDATE, size);
	FillPacketFromInfo(pInfo->m_pServerMgr, pInfo->m_pClient, pObject, pObjInfo, pInfo->m_cPacket, LTNULL);
	WriteUnguaranteedInfo(pInfo, pObject, pObjInfo);

	// Clear 'em.
	pObjInfo->m_ChangeFlags = (uint16)((pObjInfo->m_ChangeFlags & CF_CLEARMASK) | CF_SENTINFO);
}


// FUNCTION: LITHTECH 0x00471920
void UpdateSendToClientState(LTObject *pObject, UpdateInfo *pInfo)
{
	Attachment *pAttachment;
	LTObject *pAttachedObj;

	if (pObject->m_ObjType != WTObj_DObject)
		return;

	// Don't send over the main world model.
	if (pObject->IsMainWorldModel())
		return;

	if (pObject->m_ObjectType == OT_WORLDMODEL &&
		((WorldModelInstance*)pObject)->m_pOriginalBsp->IsUntransformed() == 1)
		return;

	if (!ShouldSendToClient(pInfo->m_pServerMgr, pObject))
		return;

	sm_AddObjectChangeInfo(pInfo, pObject, &pInfo->m_pClient->m_ObjInfos[pObject->m_ObjectID]);

	for (pAttachment=pObject->m_Attachments; pAttachment; pAttachment=pAttachment->m_pNext)
	{
		pAttachedObj = sm_FindObject(pInfo->m_pServerMgr, pAttachment->m_nChildID);
		if (!pAttachedObj)
			continue;

		if (!ShouldSendToClient(pInfo->m_pServerMgr, pAttachedObj))
			continue;

		sm_AddObjectChangeInfo(pInfo, pAttachedObj, &pInfo->m_pClient->m_ObjInfos[pAttachedObj->m_ObjectID]);
	}
}


// Writes the position, rotation and animation of an object that sends them unguaranteed.
// FUNCTION: LITHTECH 0x00471ca0
void WriteUnguaranteedInfo(UpdateInfo *pInfo, LTObject *pObject, ObjInfo *pObjInfo)
{
	uint32 flags, size;
	uint16 netFlags, bAnimInfo;

	netFlags = pObject->sd->m_NetFlags;
	if (!(netFlags & (NETFLAG_ANIMUNGUARANTEED | NETFLAG_POSUNGUARANTEED | NETFLAG_ROTUNGUARANTEED)))
		return;

	flags = 0;
	if (netFlags & NETFLAG_POSUNGUARANTEED)
		flags |= UUF_POS;

	if (netFlags & NETFLAG_ROTUNGUARANTEED)
	{
		if (pObject->m_Flags & FLAG_YROTATION)
			flags |= UUF_YROTATION;
		else
			flags |= UUF_ROT;
	}

	if (netFlags & NETFLAG_ANIMUNGUARANTEED)
		flags |= UUF_ANIMINFO;

	bAnimInfo = flags & UUF_ANIMINFO;

	size = 25;
	if (bAnimInfo)
		WriteAnimInfo((ModelInstance*)pObject, LTNULL, &size);

	sm_FlushUpdate(pInfo, pInfo->m_cUnguaranteed, SMSG_UNGUARANTEEDUPDATE, size);

	pInfo->m_cUnguaranteed->WriteType((uint16)(pObject->m_ObjectID | flags));

	if (flags & UUF_POS)
	{
		ic_WriteCompPos(&pInfo->m_cUnguaranteed->m_Message, &pObject->m_Pos, &pInfo->m_pServerMgr->m_World);
		pInfo->m_cUnguaranteed->m_Message.WriteCompVector(pObject->m_Velocity);
	}

	if (flags & UUF_YROTATION)
		ic_WriteYRotation(pInfo->m_cUnguaranteed, &pObject->m_Rotation);
	else if (flags & UUF_ROT)
		ic_WriteCompRotation(&pInfo->m_cUnguaranteed->m_Message, &pObject->m_Rotation);

	if (bAnimInfo)
		WriteAnimInfo((ModelInstance*)pObject, pInfo->m_cUnguaranteed, LTNULL);
}


// Mark the end-update info (or just count its bytes if pPacket is null).
// FUNCTION: LITHTECH 0x00471e60
void WriteEndUpdateInfo(CServerMgr *pServerMgr, Client *pClient, CPacket *pPacket, uint32 *pSize)
{
	if (pPacket)
	{
		pPacket->WriteType((uint16)ID_TIMESTAMP);
		pPacket->WriteType(pServerMgr->m_GameTime);
	}

	if (pSize)
		*pSize += 6;
}


// Moves the naggled packets the client has waiting into the update packets.
// FUNCTION: LITHTECH 0x00471fe0
void sm_FlushPacketGroups(CServerMgr *pServerMgr, UpdateInfo *pInfo, Client *pClient)
{
	uint32 i, j;
	CPacket *pDest;
	ClientPacketBuf *pBuf;

	for (i=0; i < 2; i++)
	{
		pDest = pInfo->m_cGroups[i];
		for (j=0; j < 2; j++)
		{
			pBuf = &pClient->m_PacketBufs[i][j];
			if (pBuf->m_pPacket->m_DataLen - 1 > 0)
			{
				if (pBuf->m_pPacket->m_DataLen - 1 < pDest->GetSpaceLeft())
				{
					pDest->WriteRaw(&pBuf->m_pPacket->m_Data.GetArray()[1], pBuf->m_pPacket->m_DataLen - 1);
				}
				else
				{
					SendToClient(pServerMgr, pClient, SMSG_PACKETGROUP, pBuf->m_pPacket, TRUE, pBuf->m_Unknown8);
				}

				pBuf->m_pPacket->ResetWrite();
			}
		}

		pDest->WriteType((uint8)0);
	}
}


// Activates objects near the client (a world tree callback).
// FUNCTION: LITHTECH 0x00471dd0
void sm_ActivateObjectCB(LTObject *pObject, UpdateInfo *pInfo)
{
	if (pObject->m_ObjType != WTObj_DObject)
		return;

	if (!ShouldSendToClient(pInfo->m_pServerMgr, pObject))
		return;

	if (pInfo->m_bAutoActivate)
	{
		pObject->m_InternalFlags &= ~IFLAG_INACTIVE_TICK;
		sm_ResetDeactivateTimer(pObject);
	}
}


// Collects the objects in a visible node the client should be sent (a vis query callback).
// FUNCTION: LITHTECH 0x00472d10
static void sm_GetVisibleObjectsCB(LTLink *pListHead, LTObject ***pppObjects, int *pnObjects)
{
	UpdateInfo *pInfo;
	LTLink *pCur;
	LTObject *pObject;

	pInfo = (UpdateInfo*)g_pCurVisQuery->m_pUserData;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObject = (LTObject*)pCur->m_pData;

		// The client objects come after the server objects.
		if (!pObject->sd)
			return;

		if (ShouldSendToClient(pInfo->m_pServerMgr, pObject))
		{
			(*pppObjects)[*pnObjects] = pObject;
			(*pnObjects)++;
		}
	}
}


// Tells the client to remove the objects it was sent last frame but isn't being sent now (and resets
// what it needs to be told if they come back).
// FUNCTION: LITHTECH 0x00472200
void WriteObjectRemoves(UpdateInfo *pInfo, SentList *pPrevList)
{
	uint32 counter, nRemoves;
	ObjectMapEntry *pRecord;
	uint16 *pCurID;
	CSoundTrack *pSoundTrack;

	nRemoves = 0;
	counter = pPrevList->m_nObjectIDs;
	pCurID = pPrevList->m_ObjectIDs;
	while (counter--)
	{
		if (!(pInfo->m_pClient->m_ObjInfos[*pCurID].m_ChangeFlags & CF_SENTINFO))
		{
			// Start a new update packet if this one is full.
			if (sm_FlushUpdate(pInfo, pInfo->m_cPacket, SMSG_UPDATE, 16))
				nRemoves = 0;

			// Write the object removes header if necessary.
			if (nRemoves == 0)
			{
				pInfo->m_cPacket->WriteType((uint8)0);
				pInfo->m_cPacket->WriteType((uint8)UPDATESUB_OBJECTREMOVES);
			}

			pInfo->m_cPacket->WriteType(*pCurID);
			++nRemoves;

			// Find this record...
			pRecord = sm_FindRecord(pInfo->m_pServerMgr, *pCurID);
			if (pRecord && pRecord->m_pRecordData)
			{
				if (pRecord->m_nRecordType == RECORDTYPE_LTOBJECT)
				{
					pInfo->m_pClient->m_ObjInfos[*pCurID].m_ChangeFlags =
						(uint16)sm_GetNewObjectChangeFlags(pInfo->m_pServerMgr, (LTObject*)pRecord->m_pRecordData);
				}
				else
				{
					pSoundTrack = (CSoundTrack*)pRecord->m_pRecordData;

					// If the client hasn't already told us the sound is done, then remove the reference...
					if (!(pInfo->m_pClient->m_ObjInfos[*pCurID].m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE))
					{
						pSoundTrack->Release(&pInfo->m_pClient->m_ObjInfos[*pCurID].m_nSoundFlags);
					}

					pInfo->m_pClient->m_ObjInfos[*pCurID].m_ChangeFlags = CF_NEWOBJECT | CF_POSITION;
				}
			}
		}

		++pCurID;
	}
}


// Sends all the objects.
// FUNCTION: LITHTECH 0x004724a0
void SendAllObjects(CServerMgr *pServerMgr, ObjectMgr *pObjectMgr, UpdateInfo *pInfo)
{
	uint32 i;
	LTLink *pListHead, *pCur;
	LTObject *pObject;

	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		pListHead = &pObjectMgr->m_ObjectLists[i].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pObject = (LTObject*)pCur->m_pData;

			if (pObject->m_InternalFlags & IFLAG_INACTIVE_TICK)
			{
				pObject->m_InternalFlags &= ~IFLAG_INACTIVE_TICK;
				sm_SetObjectStateFlags(pServerMgr, pObject, (pObject->m_InternalFlags & (IFLAG_INACTIVE | IFLAG_INACTIVE_TOUCH)) | IFLAG_AUTODEACTIVATED);
			}

			UpdateSendToClientState(pObject, pInfo);
		}
	}
}


// Is the client's connection backed up?
inline LTBOOL IsClientInTrouble(Client *pClient)
{
	if (pClient->m_ConnectionID)
	{
		if (pClient->m_ConnectionID->m_RecvQueue.GetSize() > (uint32)g_CV_ConnTroubleCount ||
			pClient->m_ConnectionID->m_SendQueue.GetSize() > (uint32)g_CV_ConnTroubleCount ||
			pClient->m_ConnectionID->IsInTrouble())
		{
			return LTTRUE;
		}
	}

	return LTFALSE;
}


// Sends the client everything it needs to see this frame.
// The bandwidth divisor is a float product (`* 1.0f`): a plain (float) cast folds into fidiv, the exe has fild + fdivp.
// The force-update loop has its own counter (j): with i reused, the reloads after the inlined AddObjectIdToSentList swap.
// FUNCTION: LITHTECH 0x00472510
void sm_UpdateClientInWorld(CServerMgr *pServerMgr, Client *pClient)
{
	UpdateInfo updateInfo;
	VisQueryRequest request;
	ForceUpdate forceUpdate;
	HOBJECT aObjects[MAX_FORCEUPDATE_OBJECTS];
	LTObject *pClientObject, *pObject;
	SentList *pPrevList, *pCurList;
	LTLink *pCur, *pNext;
	CServerEvent *pEvent;
	ObjInfo *pObjInfo;
	uint32 i;
	uint32 j;

	// If the client's queue is backed up, wait until it's ok.
	if (IsClientInTrouble(pClient))
		return;

	// If they're not in the world, they don't need to be updated...
	if (pClient->m_State != CLIENT_INWORLD)
		return;

	// Throttle the update rate to the available bandwidth.
	if (!(pClient->m_ClientFlags & (CFLAG_LOCAL | CFLAG_FORCENEXTUPDATE)))
	{
		float fRatio, fScale, fTarget, fRate;

		if (!pClient->m_Timer.Update())
			return;

		if (pServerMgr->m_nSendPackets)
			fRatio = 1.0f - (float)pServerMgr->m_nDroppedSendPackets / pServerMgr->m_nSendPackets;
		else
			fRatio = 1.0f;

		if (g_DebugLevel > 0 && fRatio < 0.999f)
			dsi_ConsolePrint("Overflowing at %0.0f percent", (1.0f - fRatio) * 100.0f);

		if (g_CV_SendBandwidth > 0)
		{
			fScale = pServerMgr->m_NetMgr.m_SendBPS.GetRate() / (g_CV_SendBandwidth * 1.0f);
			if (fScale > 0.75f)
			{
				fScale += 0.25f;
				fScale = LTMIN(fScale, 2.0f);

				fScale = 2.0f - fScale;
				fRatio *= fScale * fScale;
			}
		}

		fTarget = fRatio * pClient->m_Unknown128;
		fTarget = LTMAX(2.0f, fTarget);

		fRate = pClient->m_Timer.GetUpdateRate();
		if (fTarget > fRate)
			fRate = fRate + (fTarget - fRate) * 0.01f;
		else
			fRate = (fRate + fTarget) * 0.5f;

		if (g_DebugLevel > 0 && fabsf(fRate - fTarget) > 0.1f)
			dsi_ConsolePrint("Connection %d: choked update rate %0.0f", pClient->m_ClientID, fRate);

		pClient->m_Timer.SetUpdateRate(fRate);
	}

	pClient->m_ClientFlags &= ~CFLAG_FORCENEXTUPDATE;

	// Init the update info.
	updateInfo.m_pServerMgr = pServerMgr;
	updateInfo.m_pClient = pClient;
	updateInfo.m_cGroups[1] = updateInfo.m_cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	updateInfo.m_cGroups[0] = updateInfo.m_cUnguaranteed = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	updateInfo.m_nPacketsSent = 0;
	updateInfo.m_bAutoActivate = (pClient->m_ClientFlags >> 9) & 1;

	// Move the naggled packets the client has waiting into the update packets.
	sm_FlushPacketGroups(pServerMgr, &updateInfo, pClient);

	// Tell it about the light animations that changed.
	sm_SendChangedLightAnims(pServerMgr, pClient);

	pPrevList = &pClient->m_SentLists[pClient->m_iPrevSentList];
	pCurList = &pClient->m_SentLists[!pClient->m_iPrevSentList];
	g_pCurSentList = pCurList;

	// Clear the CF_SENTINFO flag on all the objects we sent info on earlier.
	for (i=0; i < pPrevList->m_nObjectIDs; i++)
	{
		pClient->m_ObjInfos[pPrevList->m_ObjectIDs[i]].m_ChangeFlags &= ~CF_SENTINFO;
	}

	// Build the new SentInfo list.
	pCurList->m_nObjectIDs = 0;

	// Activate everything they can see.
	request.m_Viewpoint = pClient->m_ViewPos;
	request.m_ViewRadius = 10000.0f;
	request.m_AddObject = (VQAddObjectFn)sm_ActivateObjectCB;
	request.m_Unknown18 = (void*)sm_GetVisibleObjectsCB;
	request.m_pUserData = &updateInfo;
	pServerMgr->m_World.m_WorldTree.DoVisQuery(&request);

	// Send all the alive objects to the client.
	SendAllObjects(pServerMgr, &pServerMgr->m_ObjectMgr, &updateInfo);

	if (g_CV_FlashClients)
		g_CV_FlashClients = 0;

	// The client's object, the sky objects and the objects the shell wants are always updated.
	forceUpdate.m_nObjects = 0;
	forceUpdate.m_Objects = aObjects;

	if (pClient->m_pAttachmentParent)
		pClientObject = pClient->m_pAttachmentParent->m_pObject;
	else
		pClientObject = pClient->m_pObject;

	if (pClientObject)
	{
		forceUpdate.m_Objects[forceUpdate.m_nObjects] = (HOBJECT)pClientObject;
		forceUpdate.m_nObjects++;
	}

	for (i=0; i < MAX_SKYOBJECTS; i++)
	{
		if (pServerMgr->m_SkyObjects[i] != INVALID_OBJECTID)
		{
			pObject = sm_FindObject(pServerMgr, pServerMgr->m_SkyObjects[i]);
			if (pObject)
			{
				forceUpdate.m_Objects[forceUpdate.m_nObjects] = (HOBJECT)pObject;
				forceUpdate.m_nObjects++;
			}
		}
	}

	if (pClientObject)
		pClientObject->sd->m_pObject->EngineMessageFn(MID_GETFORCEUPDATEOBJECTS, &forceUpdate, 0.0f);

	for (j=0; j < forceUpdate.m_nObjects; j++)
	{
		pObject = (LTObject*)forceUpdate.m_Objects[j];
		if (!pObject)
			continue;

		uint32 size;
		pObjInfo = &pClient->m_ObjInfos[pObject->m_ObjectID];

		// Check if we already sent this.
		if (pObjInfo->m_ChangeFlags & CF_SENTINFO)
			continue;

		AddObjectIdToSentList(pObject);

		size = 0;
		FillPacketFromInfo(updateInfo.m_pServerMgr, updateInfo.m_pClient, pObject, pObjInfo, LTNULL, &size);
		sm_FlushUpdate(&updateInfo, updateInfo.m_cPacket, SMSG_UPDATE, size);
		FillPacketFromInfo(updateInfo.m_pServerMgr, updateInfo.m_pClient, pObject, pObjInfo, updateInfo.m_cPacket, LTNULL);
		WriteUnguaranteedInfo(&updateInfo, pObject, pObjInfo);

		// Clear 'em.
		pObjInfo->m_ChangeFlags = (uint16)((pObjInfo->m_ChangeFlags & CF_CLEARMASK) | CF_SENTINFO);
	}

	// Add all the event subpackets.
	pCur = pClient->m_Events.m_Head.m_pNext;
	while (pCur != &pClient->m_Events.m_Head)
	{
		pNext = pCur->m_pNext;
		pEvent = (CServerEvent*)pCur->m_pData;

		sm_FlushUpdate(&updateInfo, updateInfo.m_cPacket, SMSG_UPDATE, 35);
		WriteEventToPacket(pServerMgr, pEvent, pClient, updateInfo.m_cPacket);

		dl_RemoveAt(&pClient->m_Events, pCur);
		pEvent->DecrementRefCount();

		pCur = pNext;
	}

	// Send sound tracking data.
	sm_SendSoundTracks(&updateInfo, updateInfo.m_cPacket);

	// Write the list of objects to remove (objects we didn't send info on).
	WriteObjectRemoves(&updateInfo, pPrevList);

	// Mark the end of the update info.
	{
	uint32 size;
	size = 0;
	WriteEndUpdateInfo(pServerMgr, pClient, LTNULL, &size);
	sm_FlushUpdate(&updateInfo, updateInfo.m_cUnguaranteed, SMSG_UNGUARANTEEDUPDATE, size);
	}
	WriteEndUpdateInfo(pServerMgr, pClient, updateInfo.m_cUnguaranteed, LTNULL);

	updateInfo.m_cPacket->m_ErrorFlags |= PACKETERR_FRAGMENTED;
	updateInfo.m_cUnguaranteed->m_ErrorFlags |= PACKETERR_GROUPED;

	// Send them..
	if (updateInfo.m_cPacket->m_DataLen - 1 > 1)
		sm_FlushUpdate(&updateInfo, updateInfo.m_cPacket, SMSG_UPDATE, -1);

	sm_FlushUpdate(&updateInfo, updateInfo.m_cUnguaranteed, SMSG_UNGUARANTEEDUPDATE, -1);

	pClient->m_iPrevSentList = !pClient->m_iPrevSentList;

	// Send the sky definition if need be.
	if (pClient->m_ClientFlags & CFLAG_SENDSKYDEF)
	{
		pClient->m_ClientFlags &= ~CFLAG_SENDSKYDEF;
		sm_TellClientAboutSky(pServerMgr, pClient);
	}

	// Send the global light object if need be.
	if (pClient->m_ClientFlags & CFLAG_SENDGLOBALLIGHT)
	{
		pClient->m_ClientFlags &= ~CFLAG_SENDGLOBALLIGHT;
		sm_TellClientAboutGlobalLight(pServerMgr, pClient);
	}
}


// FUNCTION: LITHTECH 0x00472db0
void sm_SendSoundTracks(UpdateInfo *pInfo, CPacket *pPacket)
{
	LTLink *pCur;
	CSoundTrack *pSoundTrack;
	ObjInfo *pObjInfo;
	float fMaxDistSqr;
	uint16 *pNewIDs;

	for (pCur=pInfo->m_pServerMgr->m_SoundTrackList.m_Head.m_pNext; pCur != &pInfo->m_pServerMgr->m_SoundTrackList.m_Head;
		pCur=pCur->m_pNext)
	{
		pSoundTrack = (CSoundTrack*)pCur->m_pData;

		if (!(pInfo->m_pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)].m_ChangeFlags & CF_SOUNDINFO))
		{
			// Check if the sound was removed.
			if (pSoundTrack->GetRemove())
				continue;

			// Check if the sound is done...
			if (pSoundTrack->m_pSoundData && pSoundTrack->GetTimeLeft() <= 0.0f)
				continue;

			// Check if the sound is within 2x its outer radius from the client or the viewer pos...
			if (pSoundTrack->m_dwFlags & (PLAYSOUND_3D | PLAYSOUND_AMBIENT))
			{
				fMaxDistSqr = 4.0f * pSoundTrack->m_fOuterRadius * pSoundTrack->m_fOuterRadius;
				if ((pSoundTrack->m_vPosition.DistSqr(pInfo->m_pClient->m_pObject->GetPos()) < fMaxDistSqr) ||
					(pSoundTrack->m_vPosition.DistSqr(pInfo->m_pClient->m_ViewPos) < fMaxDistSqr))
				{
				}
				else
				{
					continue;
				}
			}
		}

		sm_FlushUpdate(pInfo, pPacket, SMSG_UPDATE, 45);

		pObjInfo = &pInfo->m_pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)];

		if (g_pCurSentList->m_nObjectIDs >= g_pCurSentList->m_AllocatedSize)
		{
			pNewIDs = (uint16*)dalloc(sizeof(uint16) * (g_pCurSentList->m_AllocatedSize + 200));
			memcpy(pNewIDs, g_pCurSentList->m_ObjectIDs, sizeof(uint16) * g_pCurSentList->m_nObjectIDs);
			dfree(g_pCurSentList->m_ObjectIDs);
			g_pCurSentList->m_ObjectIDs = pNewIDs;
			g_pCurSentList->m_AllocatedSize += 200;
		}

		g_pCurSentList->m_ObjectIDs[g_pCurSentList->m_nObjectIDs] = (uint16)GetLinkID(pSoundTrack->m_pIDLink);
		g_pCurSentList->m_nObjectIDs++;

		// If the client already told us the sound is done, then don't send it again...
		if (pObjInfo->m_ChangeFlags && !(pObjInfo->m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE))
		{
			FillSoundTrackPacketFromInfo(pInfo->m_pServerMgr, pSoundTrack, pObjInfo, pInfo->m_pClient, pPacket);
		}

		// Clear 'em.
		pObjInfo->m_ChangeFlags = CF_SENTINFO;
	}
}


// FUNCTION: LITHTECH 0x00472fe0
Client* sm_FindClient(CServerMgr *pServerMgr, CBaseConn *connID)
{
	LTLink *pCur;
	Client *pClient;

	for (pCur=pServerMgr->m_Clients.m_Head.m_pNext; pCur != &pServerMgr->m_Clients.m_Head; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;
		if (pClient->m_ConnectionID == connID)
			return pClient;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00473010
void sm_SetSendSkyDef(CServerMgr *pServerMgr)
{
	LTLink *pCur;

	for (pCur=pServerMgr->m_Clients.m_Head.m_pNext; pCur != &pServerMgr->m_Clients.m_Head; pCur=pCur->m_pNext)
	{
		((Client*)pCur->m_pData)->m_ClientFlags |= CFLAG_SENDSKYDEF;
	}
}


// FUNCTION: LITHTECH 0x00473040
void sm_TellClientAboutSky(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;
	uint16 i;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	pPacket->WriteRaw(&pServerMgr->m_SkyDef, sizeof(pServerMgr->m_SkyDef));
	pPacket->WriteType((uint16)MAX_SKYOBJECTS);
	for (i=0; i < MAX_SKYOBJECTS; i++)
	{
		pPacket->WriteType(pServerMgr->m_SkyObjects[i]);
	}

	SendToClient(pServerMgr, pClient, SMSG_SKYDEF, pPacket, FALSE, MESSAGE_GUARANTEED);
	pPacket->Release();
}


// Tells the client about the global light (the sun).
// FUNCTION: LITHTECH 0x00473230
void sm_TellClientAboutGlobalLight(CServerMgr *pServerMgr, Client *pClient)
{
	CPacketRef cPacket;
	StaticSunLight *pSun;
	LTRotation rRotation;
	LTMatrix mMatrix;
	LTVector vRight, vUp, vForward;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	pSun = (StaticSunLight*)pServerMgr->m_pServerInterface->HandleToObject(pServerMgr->GetGlobalLightObject());
	pServerMgr->m_pServerInterface->GetObjectRotation(pServerMgr->GetGlobalLightObject(), &rRotation);

	rRotation.ConvertToMatrix(mMatrix);
	mMatrix.GetBasisVectors(&vRight, &vUp, &vForward);

	cPacket->WriteType(vForward.x);
	cPacket->WriteType(vForward.y);
	cPacket->WriteType(vForward.z);

	cPacket->WriteType(pSun->m_InnerColor.x);
	cPacket->WriteType(pSun->m_InnerColor.y);
	cPacket->WriteType(pSun->m_InnerColor.z);

	cPacket->WriteType(pSun->m_BrightScale);

	SendToClient(pServerMgr, pClient, SMSG_GLOBALLIGHT, cPacket, FALSE, MESSAGE_GUARANTEED);
}


// FUNCTION: LITHTECH 0x004733f0
LTRESULT sm_RemoveObjectFromSky(CServerMgr *pServerMgr, LTObject *pObject)
{
	uint32 i;

	if (!(~pObject->m_InternalFlags & IFLAG_INSKY))
	{
		pObject->m_InternalFlags &= ~IFLAG_INSKY;
		sm_SetSendSkyDef(pServerMgr);

		for (i=0; i < MAX_SKYOBJECTS; i++)
		{
			if (g_pServerMgr->m_SkyObjects[i] == pObject->m_ObjectID)
			{
				pServerMgr->m_SkyObjects[i] = INVALID_OBJECTID;
				sm_SetSendSkyDef(g_pServerMgr);
				break;
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00473460
FileIDInfo* sm_GetClientFileIDInfo(Client *pClient, uint16 wFileID)
{
	HHashElement *hElement;
	FileIDInfo *pFileIDInfo;

	hElement = hs_FindElement(pClient->m_hFileIDTable, &wFileID, 2);
	if (hElement)
	{
		pFileIDInfo = (FileIDInfo*)hs_GetElementUserData(hElement);
		if (pFileIDInfo)
			return pFileIDInfo;
	}

	pFileIDInfo = (FileIDInfo*)sb_Allocate_z(&g_pServerMgr->m_BankCAC);
	if (!pFileIDInfo)
		return LTNULL;

	pFileIDInfo->m_nChangeFlags = FILEIDINFOF_SOUNDPLAYSOUNDFLAGS | FILEIDINFOF_SOUNDPRIORITY | FILEIDINFOF_RADIUS;

	hElement = hs_AddElement(pClient->m_hFileIDTable, &wFileID, 2);
	if (!hElement)
	{
		dfree(pFileIDInfo);
		return LTNULL;
	}

	hs_SetElementUserData(hElement, pFileIDInfo);
	return pFileIDInfo;
}


// Finds the client's pending change for a light animation.
// FUNCTION: LITHTECH 0x00473510
LTBOOL sm_FindLightAnimChange(Client *pClient, uint32 iLightAnim, uint32 *pIndex)
{
	uint32 i;

	for (i=0; i < pClient->m_nLightAnimChanges; i++)
	{
		if (pClient->m_LightAnimChanges[i].m_iLightAnim == (uint16)iLightAnim)
		{
			*pIndex = i;
			return TRUE;
		}
	}

	return FALSE;
}


// Queues a light animation change for all the clients.
// FUNCTION: LITHTECH 0x00473550
void sm_SetLightAnimChanged(CServerMgr *pServerMgr, uint32 iLightAnim, uint32 flags)
{
	LTLink *pCur, *pListHead;
	Client *pClient;
	uint32 index;

	if (iLightAnim >= pServerMgr->m_World.m_LightAnims.GetSize())
		return;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (sm_FindLightAnimChange(pClient, iLightAnim, &index))
		{
			pClient->m_LightAnimChanges[index].m_ChangeFlags |= (uint16)flags;
		}
		else
		{
			// Full?  Flush them.
			if (pClient->m_nLightAnimChanges >= MAX_LIGHTANIM_CHANGES)
				sm_SendChangedLightAnims(pServerMgr, pClient);

			pClient->m_LightAnimChanges[pClient->m_nLightAnimChanges].m_iLightAnim = (uint16)iLightAnim;
			pClient->m_LightAnimChanges[pClient->m_nLightAnimChanges].m_ChangeFlags = (uint16)flags;
			pClient->m_nLightAnimChanges++;
		}
	}
}


// Out-of-line copies of inline functions (inline budget); they follow the first function that calls
// them out of line.
// FUNCTION: LITHTECH 0x00470b50 ?AddAfter@CheapLTLink@@QAEXPAV1@@Z
// FUNCTION: LITHTECH 0x00470b70 ?Term@ClientPacketBuf@@QAEXXZ
// FUNCTION: LITHTECH 0x00470ba0 ??0ClientPacketBuf@@QAE@XZ
// FUNCTION: LITHTECH 0x004735f0 ??1CPacketRef@@QAE@XZ
// FUNCTION: LITHTECH 0x00473600 ?WriteTypeImpl@CPacket@@QAEXM@Z
