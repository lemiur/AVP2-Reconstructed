// Jupiter runtime/server/src/s_net.cpp: implements net-related stuff in the ServerMgr.
// Talon passes the server manager explicitly and still uses the ref-counted CPacket.
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "s_client.h"
#include "serverevent.h"
#include "ftserv.h"
#include "packet.h"
#include "server_interface.h"
#include "soundtrack.h"
#include "server_filemgr.h"
#include "animtracker.h"
#include "s_object.h"
#include "impl_common.h"
#include "de_objects.h"
#include "de_world.h"

LTRESULT sm_HandleCommand(ConsoleState *pState, char *pCommand);	// s_concommand, 0x00473e80

// Talon client-to-server packet IDs.
#define CMSG_HELLO				5
#define CMSG_GOODBYE			6
#define CMSG_UPDATE				7
#define CMSG_SOUNDUPDATE		8
#define CMSG_CONNECTSTAGE		9
#define CMSG_COMMANDSTRING		10
#define CMSG_MESSAGE			11
#define CMSG_PEERTOPEERAUTH		13

#define SMSG_PACKETGROUP		14

#define NETMGR_TRAVELDIR_CLIENT2SERVER	2

#define DISCONNECTREASON_VOLUNTARY_SERVERSIDE	5

#define OBJINFOSOUNDF_CLIENTDONE	(1<<0)

// GLOBAL: LITHTECH 0x004e49f8
ServerPacketHandlerFn g_ServerHandlers[256];


// ----------------------------------------------------------------------- //
// These 2 are notification messages from CNetMgr.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00473ee0
LTBOOL CServerMgr::NewConnectionNotify(CBaseConn *id, LTBOOL bIsLocal)
{
	SetupGlobals();
	sm_OnNewConnection(this, id, bIsLocal);
	return TRUE;
}


// FUNCTION: LITHTECH 0x00473f10
void CServerMgr::DisconnectNotify(CBaseConn *id)
{
	SetupGlobals();
	sm_OnBrokenConnection(this, id);
}


// FUNCTION: LITHTECH 0x00473f30
void CServerMgr::HandleUnknownPacket(CPacket *pPacket, uint8 senderAddr[4], uint16 senderPort)
{
	if (m_pServerAppHandler)
	{
		m_pServerAppHandler->ProcessPacket((char*)pPacket->m_Data.GetArray(), pPacket->m_DataLen,
			senderAddr, senderPort);
	}

	// Tell the server shell about unknown packet.
	if (m_ClassMgr.m_pServerShell)
	{
		m_ClassMgr.m_pServerShell->ProcessPacket((char*)pPacket->m_Data.GetArray(), pPacket->m_DataLen,
			senderAddr, senderPort);
	}
}


// Writes the model's file IDs (or just counts the bytes if pPacket is null).
// FUNCTION: LITHTECH 0x00473f90
void sm_WriteModelFiles(LTObject *pObj, CPacket *pPacket, uint32 *pSize)
{
	uint16 i;
	UsedFile *pFile;

	if (pPacket)
		pPacket->WriteType((uint16)pObj->sd->m_pFile->m_FileID);

	if (pSize)
		*pSize += 2;

	for (i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		if (pPacket)
		{
			pFile = pObj->sd->m_pSkins[i];
			pPacket->WriteType(pFile ? (uint16)pFile->m_FileID : (uint16)0xFFFF);
		}

		if (pSize)
			*pSize += 2;
	}
}


// Writes the model's scale/dims and animation trackers (or just counts the bytes if pPacket is null).
// FUNCTION: LITHTECH 0x00474160
void WriteAnimInfo(ModelInstance *pInst, CPacket *pPacket, uint32 *pSize)
{
	LTAnimTracker *pTracker;
	ModelAnim *pAnim;
	uint16 nTrackers, wFlags;
	uint32 nPercent;

	if (pPacket)
		pPacket->m_Message.WriteCompVector(pInst->m_Dims);

	if (pSize)
		*pSize += 9;

	nTrackers = (uint16)pInst->NumAnimTrackers();
	if (nTrackers > 256)
	{
		DEBUG_PRINT(1, ("Model (%s) with > 256 AnimTrackers, ignoring animation.", pInst->GetModelFilename()));

		if (pPacket)
			pPacket->WriteType((uint8)0);

		if (pSize)
			(*pSize)++;

		return;
	}

	if (pPacket)
		pPacket->WriteType((uint8)nTrackers);

	if (pSize)
		(*pSize)++;

	for (pTracker=pInst->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
	{
		if (pPacket)
		{
			wFlags = pTracker->m_TimeRef.m_Cur.m_iAnim;
			if (pTracker->m_Flags & AT_LOOPING)
				wFlags |= 0x8000;
			if (pTracker->m_Flags & AT_PLAYING)
				wFlags |= 0x4000;

			nPercent = 0;
			pAnim = pTracker->GetCurAnim();
			if (pAnim && pAnim->GetAnimTime())
				nPercent = (pTracker->m_TimeRef.m_Cur.m_Time * 255) / pAnim->GetAnimTime();

			pPacket->WriteType(wFlags);
			pPacket->WriteType((uint8)nPercent);
			pPacket->WriteType((uint8)pTracker->m_bAllowInterpolation);
			pPacket->WriteType((uint32)pTracker->m_TimeScaleNum);
			pPacket->WriteType((uint32)pTracker->m_TimeScaleDenom);
			pPacket->WriteType((uint8)pTracker->m_TimeRef.m_Prev.m_iWeightSet);
			pPacket->WriteType((uint8)pTracker->m_TimeRef.m_Cur.m_iWeightSet);
		}

		if (pSize)
			*pSize += 14;
	}
}


// Adds to the running byte count when only measuring (pSize non-null).
static inline void AddSize(uint32 *pSize, uint32 nBytes)
{
	if (pSize)
		*pSize += nBytes;
}

// ----------------------------------------------------------------------- //
// Looks at the flags in pInfo and fills the packet with update data (or just counts the bytes
// if pPacket is null).
// ----------------------------------------------------------------------- //

// STUB: LITHTECH 0x004745c0
// Wave 6: GetMessageImpl() for ic_WriteCompWorldPos/ic_WriteCompRot: aligned 848 -> 510 (SIZE 3696 vs 4032).
// Remaining: the original inlines scale.z's WriteType(float) (we call it out of line) and, at the end, the
// 0xFFFF and hidden-piece WriteTypes one level deeper. About 8 units of direct if(0) ballast at the top (raising
// this function's own budget) fixes every inline decision (272 aligned, 1247 vs 1248 instructions); the rest is
// then registers (pObj reloaded into ebx/edi in the original), the light radius `push 0`, the hasChildModels
// byte (original: `and 0x400` early, `setne` at the store) and the ic_EncodeCompRotation push order. The real
// source of those 8 units is unknown. Tried: every single AddSize -> direct `if (pSize)` (and 175 of the 276
// pairs): 510 or worse; VEC_INIT for vObjectVelocity.Init() (848; fixes the scale.z decision but starves the
// end), GetPos() for the position, GetMessageImpl()-> for the virtual writes, LTMAX for the light radius.
// Wave 7 phase 2: the last two size updates (0xFFFF, hidden pieces) written out as `if (pSize) *pSize += n;`
// instead of AddSize (two fewer pending sites after them, and a little more own size): SIZE 510 -> DIFF 4032
// bytes, 353 aligned, everything up to the attachments (+0x84c) identical (scale.z now inlined as in the exe).
// Converting any other AddSize changes nothing (sites 0-11) or overshoots (12-21; hill-climb over all 24).
// The hasChildModels byte through an LTBOOL local (the exe's `and ebx,0x400` early and `setne` at the store):
// 238 aligned but SIZE 4048 (+16): the fullres attachment's third WriteType<float> stays out of line and the
// ebx/ebp roles swap from +0x842 on; at the end the exe inlines BaseNew inside the hidden-piece WriteType
// (vcall +4/+8) and calls _DeleteAndDestroyArray (0x414720) where we call BaseNew/BaseDelete. The inline_budget
// model can't replay this function (WriteTypeImpl's 170u sits within 1-2u of the shares; --solve finds
// nothing); the build is the oracle.
LTBOOL FillPacketFromInfo(CServerMgr *pServerMgr, Client *pClient, LTObject *pObj, ObjInfo *pInfo,
	CPacket *pPacket, uint32 *pSize)
{
	uint16 changeFlags, netFlags;
	uint8 nObjectType;
	LTVector vObjectVelocity;
	Attachment *pCur;
	CompWorldPos compPos;
	CompRot compRot;

	// Does the client's object want its rotations sent?
	changeFlags = pInfo->m_ChangeFlags;
	if (!(pClient->m_ClientFlags & CFLAG_SENDCOBJROTATION) && pClient->m_pObject == pObj)
		changeFlags &= ~(CF_ROTATION|CF_SNAPROTATION);

	// If it needs a position cap, set the position change flag.
	if (changeFlags & CF_POSITION_PREDICTIONCAP)
		changeFlags |= CF_POSITION;

	// Clear certain flags if we'll be sending that stuff unguaranteed.
	netFlags = pObj->sd->m_NetFlags;
	if (netFlags & NETFLAG_POSUNGUARANTEED)
		changeFlags &= ~CF_POSITION;

	if (netFlags & NETFLAG_ROTUNGUARANTEED)
		changeFlags &= ~CF_ROTATION;

	if (pObj->m_ObjectType == OT_MODEL && (netFlags & NETFLAG_ANIMUNGUARANTEED))
		changeFlags &= ~CF_MODELINFO;

	// If it's a new object the client will teleport it automatically.
	if (changeFlags & CF_NEWOBJECT)
	{
		changeFlags &= ~CF_TELEPORT;
		changeFlags |= CF_POSITION;
	}

	// Only send color info for objects that will use it.
	if (changeFlags & CF_RENDERINFO)
	{
		if (pObj->m_ObjectType == OT_NORMAL || pObj->m_ObjectType == OT_CONTAINER ||
			pObj->m_ObjectType == OT_CAMERA)
		{
			changeFlags &= ~CF_RENDERINFO;
		}
	}

	if (!changeFlags)
		return FALSE;

	// Write the header.
	if (changeFlags & CF_OTHERFLAGMASK)
	{
		if (pPacket)
		{
			pPacket->WriteType((uint8)(changeFlags | CF_OTHER));
			pPacket->WriteType((uint8)(changeFlags >> 8));
		}

		AddSize(pSize, 2);
	}
	else
	{
		if (pPacket)
		{
			if (!(uint8)changeFlags)
				return FALSE;

			pPacket->WriteType((uint8)changeFlags);
		}

		AddSize(pSize, 1);
	}

	// Write the object ID.
	if (pPacket)
		pPacket->WriteType(pObj->m_ObjectID);

	AddSize(pSize, 2);

	// Add some extra info if it's a new object.
	if (changeFlags & CF_NEWOBJECT)
	{
		nObjectType = pObj->m_ObjectType;

		if (pObj->sd->m_pSFXMsg)
		{
			nObjectType |= 0x40;
			if (pObj->sd->m_pSFXMsg->m_DataLen >= 256)
				nObjectType |= 0x80;
		}

		if (pObj->sd->m_bCreateFlag1 & 1)
			nObjectType |= 0x20;

		if (pPacket)
			pPacket->WriteType(nObjectType);

		AddSize(pSize, 1);

		// Write its special effect info.
		if (pObj->sd->m_pSFXMsg)
		{
			if (pPacket)
			{
				if (nObjectType & 0x80)
					pPacket->WriteType((uint16)pObj->sd->m_pSFXMsg->m_DataLen);
				else
					pPacket->WriteType((uint8)pObj->sd->m_pSFXMsg->m_DataLen);

				pPacket->WriteRaw(pObj->sd->m_pSFXMsg->m_Data.GetArray(), pObj->sd->m_pSFXMsg->m_DataLen);
			}

			if (pSize)
			{
				*pSize += (nObjectType & 0x80) ? 2 : 1;
				*pSize += pObj->sd->m_pSFXMsg->m_DataLen;
			}
		}

		// Write the WorldModel name or the filename.
		if (pObj->m_ObjectType == OT_WORLDMODEL)
		{
			if (pPacket)
				pPacket->WriteString(((WorldModelInstance*)pObj)->m_pOriginalBsp->m_WorldName);

			AddSize(pSize, 0x41);
		}
		else if (pObj->m_ObjectType == OT_CONTAINER)
		{
			if (pPacket)
			{
				pPacket->WriteString(((ContainerInstance*)pObj)->m_pOriginalBsp->m_WorldName);
				pPacket->WriteType(((ContainerInstance*)pObj)->m_ContainerCode);
			}

			AddSize(pSize, 0x43);
		}
		else if (pObj->m_ObjectType == OT_MODEL)
		{
			sm_WriteModelFiles(pObj, pPacket, pSize);
		}
		else if (pObj->m_ObjectType == OT_SPRITE)
		{
			if (pPacket)
				pPacket->WriteType((uint16)pObj->sd->m_pFile->m_FileID);

			AddSize(pSize, 2);
		}
	}

	if (changeFlags & CF_NEWOBJECT)
	{
		if (pPacket)
			pPacket->WriteType(pObj->m_BPriority);

		AddSize(pSize, 1);
	}

	// Write the model info?
	if (changeFlags & (CF_MODELINFO|CF_FORCEMODELINFO))
	{
		if (pObj->m_ObjectType == OT_MODEL)
		{
			WriteAnimInfo((ModelInstance*)pObj, pPacket, pSize);
		}
		else if (pObj->m_ObjectType == OT_SPRITE)
		{
			if (pPacket)
				pPacket->WriteType((uint32)((SpriteInstance*)pObj)->m_ClipperPoly);

			AddSize(pSize, 4);
		}
	}

	// Write which things have changed.
	if (changeFlags & CF_FLAGS)
	{
		if (pPacket)
		{
			pPacket->WriteType((uint32)(pObj->m_Flags & CLIENT_FLAGMASK));
			pPacket->WriteType((uint16)pObj->m_Flags2);
			pPacket->WriteType((uint32)pObj->m_UserFlags);
		}

		AddSize(pSize, 10);
	}

	if (changeFlags & CF_RENDERINFO)
	{
		if (pPacket)
		{
			pPacket->WriteType(pObj->m_ColorR);
			pPacket->WriteType(pObj->m_ColorG);
			pPacket->WriteType(pObj->m_ColorB);
			pPacket->WriteType(pObj->m_ColorA);
		}

		AddSize(pSize, 4);

		if (pObj->m_ObjectType == OT_LIGHT)
		{
			if (pPacket)
			{
				float fRadius = ((DynamicLight*)pObj)->m_LightRadius;
				if (fRadius < 0.0f)
					pPacket->WriteType((uint16)0);
				else
					pPacket->WriteType((uint16)fRadius);
			}

			AddSize(pSize, 2);
		}
	}

	if (changeFlags & CF_SCALE)
	{
		if (pPacket)
		{
			pPacket->WriteType(pObj->m_Scale.x);
			pPacket->WriteType(pObj->m_Scale.y);
		}

		AddSize(pSize, 8);

		if (pObj->m_ObjectType != OT_SPRITE)
		{
			if (pPacket)
				pPacket->WriteType(pObj->m_Scale.z);

			AddSize(pSize, 4);
		}
	}

	if (changeFlags & (CF_POSITION|CF_TELEPORT))
	{
		vObjectVelocity = pObj->m_Velocity;

		if (pPacket)
		{
			// Turn off the prediction cap if we're sending the prediction cap position message,
			// otherwise turn it on.
			if (!(pInfo->m_ChangeFlags & CF_POSITION))
			{
				pInfo->m_ChangeFlags &= ~CF_POSITION_PREDICTIONCAP;
				vObjectVelocity.Init();
			}
			else
			{
				pInfo->m_ChangeFlags |= CF_POSITION_PREDICTIONCAP;
			}
		}

		if (pObj->m_Flags & FLAG_FULLPOSITIONRES)
		{
			if (pPacket)
			{
				pPacket->WriteType(pObj->m_Pos.x);
				pPacket->WriteType(pObj->m_Pos.y);
				pPacket->WriteType(pObj->m_Pos.z);
				pPacket->m_Message.WriteVector(vObjectVelocity);
			}

			AddSize(pSize, 0x18);
		}
		else
		{
			if (pPacket)
			{
				ic_EncodeCompPos(&compPos, &pObj->m_Pos, &pServerMgr->m_World);
				ic_WriteCompWorldPos(pPacket->GetMessageImpl(), &compPos);
				pPacket->m_Message.WriteCompVector(vObjectVelocity);
			}

			AddSize(pSize, 0x10);
		}
	}

	if (changeFlags & (CF_ROTATION|CF_SNAPROTATION))
	{
		if (pObj->m_Flags & FLAG_FULLPOSITIONRES)
		{
			if (pPacket)
				pPacket->m_Message.WriteRotation(pObj->m_Rotation);

			AddSize(pSize, 0x10);
		}
		else
		{
			if (pPacket)
			{
				ic_EncodeCompRotation(&pObj->m_Rotation, &compRot);
				ic_WriteCompRot(pPacket->GetMessageImpl(), &compRot);
			}

			AddSize(pSize, 6);
		}
	}

	if (changeFlags & CF_ATTACHMENTS)
	{
		if (pPacket)
		{
			LTBOOL bHasChildModels = pObj->m_InternalFlags & IFLAG_HASCHILDMODELS;
			pPacket->WriteType((uint8)(bHasChildModels != 0));
		}

		AddSize(pSize, 1);

		// Write the attachments.
		for (pCur=pObj->m_Attachments; pCur; pCur=pCur->m_pNext)
		{
			if (pPacket)
			{
				pPacket->WriteType(pCur->m_nChildID);
				pPacket->WriteType(pCur->m_iSocket);
			}

			AddSize(pSize, 6);

			if (pObj->m_Flags & FLAG_FULLPOSITIONRES)
			{
				if (pPacket)
				{
					pPacket->WriteType(pCur->m_Offset.m_Pos.x);
					pPacket->WriteType(pCur->m_Offset.m_Pos.y);
					pPacket->WriteType(pCur->m_Offset.m_Pos.z);
					pPacket->m_Message.WriteRotation(pCur->m_Offset.m_Rot);
				}

				AddSize(pSize, 0x1c);
			}
			else
			{
				if (pPacket)
				{
					pPacket->m_Message.WriteCompVector(pCur->m_Offset.m_Pos);
					ic_WriteCompRotation(&pPacket->m_Message, &pCur->m_Offset.m_Rot);
				}

				AddSize(pSize, 0xf);
			}
		}

		if (pPacket)
			pPacket->WriteType((uint16)0xFFFF);

		if (pSize)
			*pSize += 2;

		// Write the hidden piece list.
		if (pObj->m_ObjectType == OT_MODEL)
		{
			if (pPacket)
				pPacket->WriteType((uint32)((ModelInstance*)pObj)->m_HiddenPieces);

			if (pSize)
				*pSize += 4;
		}
	}

	return TRUE;
}


// FUNCTION: LITHTECH 0x00475580
void sm_SendToAllClients(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket, uint32 packetFlags)
{
	LTLink *pCur, *pListHead;
	Client *pClient;

	pPacket->m_Data[0] = msgID;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (pClient->m_ConnectionID)
		{
			pServerMgr->m_NetMgr.SendPacket(pPacket, pClient->m_ConnectionID, packetFlags);
		}
	}
}


// FUNCTION: LITHTECH 0x004755d0
void SendToClient(CServerMgr *pServerMgr, Client *pClient, uint8 msgID, CPacket *pPacket,
	LTBOOL bSendToAttachments, uint32 packetFlags)
{
	LTLink *pCur;
	Client *pAttachment;

	pPacket->m_Data[0] = msgID;

	if (pClient->m_ConnectionID)
	{
		if (!pServerMgr->m_NetMgr.SendPacket(pPacket, pClient->m_ConnectionID, packetFlags))
			++pServerMgr->m_nDroppedSendPackets;

		++pServerMgr->m_nSendPackets;
	}

	// Send it to the clients attached to this one.
	if (bSendToAttachments)
	{
		for (pCur=pClient->m_Attachments.m_pNext; pCur != &pClient->m_Attachments; pCur=pCur->m_pNext)
		{
			pAttachment = (Client*)pCur->m_pData;

			if (pAttachment->m_ConnectionID)
			{
				pServerMgr->m_NetMgr.SendPacket(pPacket, pAttachment->m_ConnectionID, packetFlags);
			}
		}
	}
}


// Sends the packet to the client, grouping small naggled packets into the client's packet buffers.
// FUNCTION: LITHTECH 0x00475660
LTRESULT sm_SendToClient(CServerMgr *pServerMgr, Client *pClient, uint8 msgID, CPacket *pPacket, uint32 packetFlags)
{
	ClientPacketBuf *pBuf;

	if ((pClient->m_ClientFlags & CFLAG_LOCAL) || !(packetFlags & MESSAGE_NAGGLEMASK) || pPacket->m_DataLen > 255)
	{
		SendToClient(g_pServerMgr, pClient, msgID, pPacket, TRUE, packetFlags);
		return LT_OK;
	}

	pBuf = &pClient->m_PacketBufs[(packetFlags >> 7) & 1][(packetFlags >> 1) & 1];

	// Flush the group if this one won't fit.
	if (pPacket->m_DataLen + 1 > pBuf->m_pPacket->GetSpaceLeft())
	{
		SendToClient(g_pServerMgr, pClient, SMSG_PACKETGROUP, pBuf->m_pPacket, TRUE, pBuf->m_Unknown8);
		pBuf->m_pPacket->ResetWrite();
	}

	// Still too big?  Send it by itself.
	if (pPacket->m_DataLen + 1 > pBuf->m_pPacket->GetSpaceLeft())
	{
		SendToClient(g_pServerMgr, pClient, SMSG_PACKETGROUP, pPacket, TRUE, packetFlags);
		return LT_OK;
	}

	pPacket->m_Data[0] = msgID;
	pBuf->m_pPacket->WriteType((uint8)pPacket->m_DataLen);
	pBuf->m_pPacket->WriteRaw(pPacket->m_Data.GetArray(), pPacket->m_DataLen);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00475830
void sm_SendToAllClientsInWorld(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket)
{
	LTLink *pCur, *pListHead;
	Client *pClient;

	pPacket->m_Data[0] = msgID;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (pClient->m_ConnectionID && pClient->m_State == CLIENT_INWORLD)
		{
			pServerMgr->m_NetMgr.SendPacket(pPacket, pClient->m_ConnectionID, MESSAGE_GUARANTEED);
		}
	}
}


// ----------------------------------------------------------------------- //
// Creates an event of the specified type and adds it to the client's structures.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00475890
CServerEvent* CreateServerEvent(CServerMgr *pServerMgr, int type)
{
	CServerEvent *pRet;
	LTLink *pCur, *pListHead;
	Client *pClient;
	ClientStructNode *pNode;

	pRet = (CServerEvent*)sb_Allocate(&pServerMgr->m_ServerEventBank);
	memset(pRet, 0, sizeof(*pRet));
	dl_InitList(&pRet->m_ClientStructNodeList);

	// Add the events to the client structs.
	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;
		if (pClient->m_State != CLIENT_INWORLD)
			continue;

		pNode = (ClientStructNode*)sb_Allocate(&pServerMgr->m_ClientStructNodeBank);
		dl_AddHead(&pRet->m_ClientStructNodeList, &pNode->m_Link, pNode);
		dl_AddHead(&pClient->m_Events, &pNode->m_mllNode, pRet);
		pRet->m_RefCount++;
	}

	pRet->m_EventType = type;
	return pRet;
}


// ----------------------------------------------------------------------- //
// Fills the packet with the sound track's update info.
// ----------------------------------------------------------------------- //

// Matched in wave 6 with Jupiter's GetStartTime() and GetMessageImpl() (two more pending inline sites, which keep
// the volume/pitch WriteTypes out of line) and the fade time clamped with LTCLAMP into a uint8 local.
// FUNCTION: LITHTECH 0x004759c0
void FillSoundTrackPacketFromInfo(CServerMgr *pServerMgr, CSoundTrack *pSoundTrack, ObjInfo *pInfo,
	Client *pClient, CPacket *pPacket)
{
	FileIDInfo *pFileIDInfo, fileIDInfoCurrent;
	uint16 wFlags, nOuter, nInner, dwOffsetTime;
	float fFadeTime;

	if (!pPacket)
		return;

	wFlags = (uint16)pSoundTrack->m_dwFlags;

	// If the sound is attached to this client's object, then we can assume certain things about the
	// position and orientation and not have to send a message for position changes...
	if (wFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL))
	{
		if (pSoundTrack->GetObject() == pClient->m_pObject)
		{
			wFlags |= PLAYSOUND_CLIENTLOCAL;
			pInfo->m_ChangeFlags &= ~CF_POSITION;
		}
		else
		{
			if (wFlags & PLAYSOUND_CLIENTLOCAL)
			{
				// Make sure it sends the location of the client making the sound.
				pSoundTrack->m_vPosition = pSoundTrack->GetObject()->GetPos();
				pInfo->m_ChangeFlags |= CF_POSITION;
			}

			// Not attached to the client object, so this client doesn't get the PLAYSOUND_CLIENTLOCAL
			// flag, but it does need to be 3D at that point.
			wFlags &= ~PLAYSOUND_CLIENTLOCAL;
			wFlags |= PLAYSOUND_3D;
		}
	}

	pPacket->WriteType((uint8)0);
	pPacket->WriteType((uint8)1);

	// Send the change flags and the id for this sound.
	pPacket->WriteType((uint8)pInfo->m_ChangeFlags);
	pPacket->WriteType((uint16)GetLinkID(pSoundTrack->m_pIDLink));

	// Handle the new sound info...
	if (pInfo->m_ChangeFlags & CF_NEWOBJECT)
	{
		pSoundTrack->AddRef();
		pPacket->WriteType((uint16)pSoundTrack->m_pFile->m_FileID);

		nOuter = (uint16)LTMIN(pSoundTrack->m_fOuterRadius, 65535.0f);
		nInner = LTMIN((uint8)(pSoundTrack->m_fInnerRadius * 255.0f / nOuter), 255);

		// Some of the info for sounds using the same file don't change from instance to instance,
		// so the server remembers what it sent to the client for a file and only sends the changed info.
		pFileIDInfo = sm_GetClientFileIDInfo(pClient, (uint16)pSoundTrack->m_pFile->m_FileID);
		if (!pFileIDInfo)
		{
			// Should never get here.
			fileIDInfoCurrent.m_nChangeFlags = 7;
			pFileIDInfo = &fileIDInfoCurrent;
		}
		else
		{
			fileIDInfoCurrent.m_wSoundPlaySoundFlags = wFlags;
			fileIDInfoCurrent.m_nSoundPriority = pSoundTrack->m_nPriority;
			fileIDInfoCurrent.m_nSoundOuterRadius = nOuter;
			fileIDInfoCurrent.m_nSoundInnerRadius = (uint8)nInner;
			GetSoundFileIDInfoFlags(pFileIDInfo, &fileIDInfoCurrent);
		}

		// Write the change flags for the file dependent info...
		pPacket->WriteType(pFileIDInfo->m_nChangeFlags);

		if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPLAYSOUNDFLAGS)
		{
			pFileIDInfo->m_nChangeFlags &= ~FILEIDINFOF_SOUNDPLAYSOUNDFLAGS;
			pPacket->WriteType(wFlags);
		}

		if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPRIORITY)
		{
			pFileIDInfo->m_nChangeFlags &= ~FILEIDINFOF_SOUNDPRIORITY;
			pPacket->WriteType(pSoundTrack->m_nPriority);
		}

		if (wFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
		{
			if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_RADIUS)
			{
				pFileIDInfo->m_nChangeFlags &= ~FILEIDINFOF_RADIUS;
				pPacket->WriteType(nOuter);
				pPacket->WriteType((uint8)nInner);
			}
		}

		if (wFlags & PLAYSOUND_CTRL_VOL)
			pPacket->WriteType(pSoundTrack->m_nVolume);

		if (wFlags & PLAYSOUND_CTRL_PITCH)
			pPacket->WriteType(pSoundTrack->m_fPitchShift);

		if (wFlags & PLAYSOUND_TIMESYNC)
		{
			dwOffsetTime = (uint16)((1000.0 * (pServerMgr->m_GameTime - pSoundTrack->GetStartTime())) + 0.5);
			if (dwOffsetTime < 255)
			{
				pPacket->WriteType((uint8)dwOffsetTime);
			}
			else
			{
				pPacket->WriteType((uint8)0xFF);
				pPacket->WriteType((uint32)dwOffsetTime);
			}
		}

		if (wFlags & PLAYSOUND_CTRL_TYPE)
			pPacket->WriteType(pSoundTrack->m_nUserSoundType);

		if (wFlags & PLAYSOUND_USER_DATA)
			pPacket->WriteType(pSoundTrack->m_UserData);
	}

	// Position info.
	if (pInfo->m_ChangeFlags & CF_POSITION)
		ic_WriteCompPos(pPacket->GetMessageImpl(), &pSoundTrack->m_vPosition, &pServerMgr->m_World);

	// The sound was told to fade out.
	if (pInfo->m_ChangeFlags & CF_SOUNDINFO)
	{
		fFadeTime = pSoundTrack->m_Unknown5C * 10.0f;
		uint8 nFadeTime = (uint8)LTCLAMP(fFadeTime, 0.0f, 255.0f);
		pPacket->WriteType(nFadeTime);
	}
}


// Set the change flags based on what's different...
// FUNCTION: LITHTECH 0x004760a0
void GetSoundFileIDInfoFlags(FileIDInfo *pFileIDInfo, FileIDInfo *pCurrent)
{
	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPLAYSOUNDFLAGS || pFileIDInfo->m_wSoundPlaySoundFlags != pCurrent->m_wSoundPlaySoundFlags)
	{
		pFileIDInfo->m_nChangeFlags |= FILEIDINFOF_SOUNDPLAYSOUNDFLAGS;
		pFileIDInfo->m_wSoundPlaySoundFlags = pCurrent->m_wSoundPlaySoundFlags;
	}
	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPRIORITY || pFileIDInfo->m_nSoundPriority != pCurrent->m_nSoundPriority)
	{
		pFileIDInfo->m_nChangeFlags |= FILEIDINFOF_SOUNDPRIORITY;
		pFileIDInfo->m_nSoundPriority = pCurrent->m_nSoundPriority;
	}
	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_RADIUS || pFileIDInfo->m_nSoundOuterRadius != pCurrent->m_nSoundOuterRadius || pFileIDInfo->m_nSoundInnerRadius != pCurrent->m_nSoundInnerRadius)
	{
		pFileIDInfo->m_nChangeFlags |= FILEIDINFOF_RADIUS;
		pFileIDInfo->m_nSoundOuterRadius = pCurrent->m_nSoundOuterRadius;
		pFileIDInfo->m_nSoundInnerRadius = pCurrent->m_nSoundInnerRadius;
	}
}


// Writes a server event into the update packet.
// FUNCTION: LITHTECH 0x00476120
void WriteEventToPacket(CServerMgr *pServerMgr, CServerEvent *pEvent, Client *pClient, CPacket *pPacket)
{
	if (pEvent->m_EventType == EVENT_PLAYSOUND)
		FillInPlaysoundMessage(pEvent, pClient, pPacket);
}


// Writes the PlaySound event: the file, the changed file info, and the sound's parameters.
// pPacket->GetMessageImpl() for ic_WriteCompPos is the pending inline site that was missing (the wave-4 note
// "12-20 units of ballast near the top").
// FUNCTION: LITHTECH 0x00476140
void FillInPlaysoundMessage(CServerEvent *pEvent, Client *pClient, CPacket *pPacket)
{
	PlaySoundInfo *pPlaySoundInfo;
	FileIDInfo *pFileIDInfo, fileIDInfoCurrent;
	uint16 wFlags, nOuter, nInner;
	LTBOOL bLocalOverride;

	if (!pEvent || !pPacket || !pEvent->m_pUsedFile)
		return;

	pPlaySoundInfo = &pEvent->m_PlaySoundInfo;
	wFlags = (uint16)pPlaySoundInfo->m_dwFlags;

	// Check if the sound is supposed to be played locally to a client object...
	bLocalOverride = FALSE;
	if (wFlags & PLAYSOUND_CLIENTLOCAL)
	{
		if ((LTObject*)pPlaySoundInfo->m_hObject == pClient->m_pObject)
		{
			bLocalOverride = TRUE;
		}
		else
		{
			wFlags &= ~PLAYSOUND_CLIENTLOCAL;

			// Play it as a 3D sound on the non-local clients.
			wFlags |= PLAYSOUND_3D;
		}
	}

	// Check if sound is in range...
	if (wFlags & (PLAYSOUND_3D | PLAYSOUND_AMBIENT))
	{
		if (pPlaySoundInfo->m_vPosition.DistSqr(pClient->m_pObject->m_Pos) > 4.0f * pPlaySoundInfo->m_fOuterRadius * pPlaySoundInfo->m_fOuterRadius)
			return;
	}

	// Signal the event subpacket.
	pPacket->WriteType((uint8)0);
	pPacket->WriteType((uint8)0);
	pPacket->WriteType((uint16)pEvent->m_pUsedFile->m_FileID);

	nOuter = (uint16)LTMIN(pPlaySoundInfo->m_fOuterRadius, 65535.0f);
	nInner = LTMIN((uint16)(pPlaySoundInfo->m_fInnerRadius * 255.0f / nOuter), 255);

	// Some of the info for sounds using the same file don't change from instance to instance, so
	// the server remembers what it sent to the client for a file and only sends the changed info...
	pFileIDInfo = sm_GetClientFileIDInfo(pClient, (uint16)pEvent->m_pUsedFile->m_FileID);
	if (!pFileIDInfo)
	{
		// Should never get here.
		fileIDInfoCurrent.m_nChangeFlags = 7;
		pFileIDInfo = &fileIDInfoCurrent;
	}
	else
	{
		// Compare the current values with the value we sent last time and create change flags...
		fileIDInfoCurrent.m_wSoundPlaySoundFlags = wFlags;
		fileIDInfoCurrent.m_nSoundPriority = pPlaySoundInfo->m_nPriority;
		fileIDInfoCurrent.m_nSoundOuterRadius = nOuter;
		fileIDInfoCurrent.m_nSoundInnerRadius = (uint8)nInner;
		GetSoundFileIDInfoFlags(pFileIDInfo, &fileIDInfoCurrent);
	}

	// Write the change flags for the file dependent info...
	pPacket->WriteType(pFileIDInfo->m_nChangeFlags);

	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPLAYSOUNDFLAGS)
	{
		pFileIDInfo->m_nChangeFlags &= ~FILEIDINFOF_SOUNDPLAYSOUNDFLAGS;
		pPacket->WriteType(wFlags);
	}

	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPRIORITY)
	{
		pFileIDInfo->m_nChangeFlags &= ~FILEIDINFOF_SOUNDPRIORITY;
		pPacket->WriteType(pPlaySoundInfo->m_nPriority);
	}

	if (wFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
	{
		if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_RADIUS)
		{
			pFileIDInfo->m_nChangeFlags &= ~FILEIDINFOF_RADIUS;
			pPacket->WriteType(nOuter);
			pPacket->WriteType((uint8)nInner);
		}
	}

	if (wFlags & PLAYSOUND_CTRL_VOL)
		pPacket->WriteType(pPlaySoundInfo->m_nVolume);

	if (wFlags & PLAYSOUND_CTRL_PITCH)
		pPacket->WriteType(pPlaySoundInfo->m_fPitchShift);

	if (!bLocalOverride && (wFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D)))
		ic_WriteCompPos(pPacket->GetMessageImpl(), &pPlaySoundInfo->m_vPosition, &g_pServerMgr->m_World);

	if (wFlags & PLAYSOUND_CTRL_TYPE)
		pPacket->WriteType(pPlaySoundInfo->m_nUserSoundType);

	if (wFlags & PLAYSOUND_USER_DATA)
		pPacket->WriteType(pPlaySoundInfo->m_UserData);
}


// ----------------------------------------------------------------------- //
// Reads in all packets from the net.
// ----------------------------------------------------------------------- //

// The else branch's pSender local gives the exe's register for m_hFTServ (eax, not ecx) before fts_ProcessPacket.
// FUNCTION: LITHTECH 0x00476710
LTBOOL ProcessIncomingPackets(CServerMgr *pServerMgr)
{
	CPacket *pPacket;
	ServerPacketHandlerFn *pHandler;
	Client *pClient;
	LTRESULT dResult;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	pServerMgr->m_NetMgr.StartGettingPackets();

	// Talon's second GetPacket parameter is a constant 2 (the travel direction in Jupiter).
	while (pServerMgr->m_NetMgr.GetPacket(pPacket, (CBaseConn**)NETMGR_TRAVELDIR_CLIENT2SERVER))
	{
		pHandler = &g_ServerHandlers[pPacket->m_Data[0] & 0x3f];
		if (*pHandler)
		{
			pClient = pPacket->m_pSender ? sm_FindClient(pServerMgr, pPacket->m_pSender) : LTNULL;

			dResult = (*pHandler)(pServerMgr, pPacket, pClient);
			if (dResult != LT_OK)
			{
				pServerMgr->m_NetMgr.EndGettingPackets();
				pPacket->Release();
				return dResult;
			}
		}
		else
		{
			// Let the file transfer manager have the packet (including the ID...)
			CBaseConn *pSender = pPacket->m_pSender;
			pClient = sm_FindClient(pServerMgr, pSender);
			if (pClient)
			{
				fts_ProcessPacket(pClient->m_hFTServ, pPacket);
			}
		}
	}

	pServerMgr->m_NetMgr.EndGettingPackets();
	if (pPacket)
		pPacket->Release();

	return LT_OK;
}


// ----------------------------------------------------------------------- //
//   Packet handlers
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00476870
static LTRESULT OnSoundUpdatePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint16 objectID;
	ObjectMapEntry *pRecord;
	CSoundTrack *pSoundTrack;

	if (!pClient)
		return LT_OK;

	// Client has sent the sounds it has finished playing...
	while (pPacket->m_DataLen - pPacket->m_Pos != 0)
	{
		// Pull the sound track info out of the message...
		objectID = pPacket->ReadType((uint16*)0);
		pRecord = sm_FindRecord(pServerMgr, objectID);
		if (!pRecord || pRecord->m_nRecordType != RECORDTYPE_SOUND)
			continue;

		pSoundTrack = (CSoundTrack*)pRecord->m_pRecordData;
		if (!pSoundTrack)
			continue;

		// Skip it if the sound isn't done yet
		if (pClient->m_ObjInfos[objectID].m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE)
			continue;

		pSoundTrack->Release(&pClient->m_ObjInfos[objectID].m_nSoundFlags);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476990
static LTRESULT OnClientUpdatePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint8 *pPrevCommands, *pCurCommands;
	float fRate;
	uint16 i, nCommands;
	int nChanged;
	uint8 changed[256];

	if (!pClient)
		return LT_OK;

	// Swap the command buffers.
	pPrevCommands = pClient->m_Commands[pClient->m_iCurCommands];
	pCurCommands = pClient->m_Commands[!pClient->m_iCurCommands];
	memset(pCurCommands, 0, MAX_CLIENT_COMMANDS);
	pClient->m_iCurCommands = !pClient->m_iCurCommands;

	fRate = (float)pPacket->ReadType((uint8*)0);
	if (fRate != pClient->m_Unknown128)
	{
		pClient->m_Unknown128 = fRate;
		pClient->m_Timer.SetUpdateRate(fRate);
	}

	// Read the commands that are on.
	nCommands = pPacket->ReadType((uint8*)0);
	for (i=nCommands; i > 0; i--)
	{
		pCurCommands[pPacket->ReadType((uint8*)0)] = 1;
	}

	// Tell the server about the ones that changed.
	nChanged = 0;
	for (i=0; i < MAX_CLIENT_COMMANDS; i++)
	{
		if (pCurCommands[i] != pPrevCommands[i])
			changed[nChanged++] = (uint8)i;
	}

	pServerMgr->ProcessClientCommands(pClient, changed, nChanged);

	if (pPacket->m_DataLen - pPacket->m_Pos > 0)
	{
		return OnSoundUpdatePacket(pServerMgr, pPacket, pClient);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476c20
static LTRESULT OnClientDisconnectPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	if (pPacket->m_pSender)
	{
		// This will in turn call DisconnectNotify().
		pServerMgr->m_NetMgr.Disconnect(pPacket->m_pSender, DISCONNECTREASON_VOLUNTARY_SERVERSIDE);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476c40
static LTRESULT OnCommandStringPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		return sm_HandleCommand(&pServerMgr->m_ConsoleState, pPacket->ReadString());
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476c70
static LTRESULT OnPeerToPeerAuthPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	if (pClient && pServerMgr->m_ClassMgr.m_pServerShell)
	{
		pServerMgr->OnPeerToPeerAuthPacket(pClient, pPacket);
	}

	return LT_OK;
}


// eax/edx swapped for the message ID and the shell vtable (4 bytes).
// Wave 5 tried: GetMessageImpl(), a local for the shell, a local for the message handle, initialising
// messageID to 0: no change (the other OnMessagePacket, the client's at 0x0048d0f0, differs the same way).
// Wave 6 tried: early `return LT_OK` guards (one or two), `messageID = 0;` before the if, the inverted if/else,
// inline helpers for the message ID read (by value and by reference), for the remaining-bytes test and for the
// server shell, and an HMESSAGEREAD local: still 3 aligned mismatches.
// Wave 7 phase 2: audit: behaviour matches. Same symptom as the client's OnMessagePacket (0x0048d0f0).
// PARKED: eax/edx swapped for the message ID and the shell vtable (3 aligned); behaviour identical
// STUB: LITHTECH 0x00476ca0
static LTRESULT OnMessagePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint8 messageID;

	if (pClient && pServerMgr->m_ClassMgr.m_pServerShell)
	{
		pServerMgr->SetupPacketMessage(pPacket);

		// The message ID is the last byte.
		if ((uint32)(pPacket->m_DataLen - pPacket->m_Pos) >= 1)
		{
			messageID = pPacket->m_Data.GetArray()[pPacket->m_DataLen - 1];
			pPacket->m_DataLen--;
		}
		else
		{
			messageID = 0;
		}

		pServerMgr->m_ClassMgr.m_pServerShell->OnMessage((HCLIENT)pClient, messageID,
			(HMESSAGEREAD)&pPacket->m_Message);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476d20
static LTRESULT OnConnectStagePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint8 type;

	type = pPacket->ReadType((uint8*)0);
	if (type == 0)
	{
		pClient->m_PuttingIntoWorldStage = PUTTINGINTOWORLD_LOADEDWORLD;
	}
	else
	{
		pClient->m_PuttingIntoWorldStage = PUTTINGINTOWORLD_PRELOADED;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476dc0
static LTRESULT OnHelloPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	pClient->m_ClientDataLen = pPacket->ReadType((uint16*)0);
	if (pClient->m_ClientDataLen)
	{
		pClient->m_pClientData = new char[pClient->m_ClientDataLen];
		pPacket->ReadRaw(pClient->m_pClientData, (uint16)pClient->m_ClientDataLen);
	}

	pClient->m_ClientFlags |= CFLAG_GOT_HELLO;
	return LT_OK;
}


// ----------------------------------------------------------------------- //
// Just sets up some pointers to functions for packet receivers.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00476800
void InitServerNetHandlers()
{
	memset(g_ServerHandlers, 0, sizeof(g_ServerHandlers));

	g_ServerHandlers[CMSG_GOODBYE] = OnClientDisconnectPacket;
	g_ServerHandlers[CMSG_UPDATE] = OnClientUpdatePacket;
	g_ServerHandlers[CMSG_SOUNDUPDATE] = OnSoundUpdatePacket;
	g_ServerHandlers[CMSG_COMMANDSTRING] = OnCommandStringPacket;
	g_ServerHandlers[CMSG_MESSAGE] = OnMessagePacket;
	g_ServerHandlers[CMSG_CONNECTSTAGE] = OnConnectStagePacket;
	g_ServerHandlers[CMSG_HELLO] = OnHelloPacket;
	g_ServerHandlers[CMSG_PEERTOPEERAUTH] = OnPeerToPeerAuthPacket;
}
