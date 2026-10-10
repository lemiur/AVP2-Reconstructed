// Jupiter runtime/client/src/shellnet.cpp
// Talon's packet handlers read reference-counted CPackets and reach the client manager
// through CClientShell::m_pClientMgr.
// FLAGS: /O2 /GX-
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "clientshell.h"
#include "clientmgr.h"
#include "iclientshell.h"
#include "console.h"
#include "packet.h"
#include "concommand.h"
#include "client_filemgr.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "model.h"
#include "sprite.h"
#include "iltclient.h"
#include "setupobject.h"
#include "animtracker.h"
#include "moveobject.h"
#include "objectmgr.h"
#include "impl_common.h"
#include "soundmgr.h"
#include "soundinstance.h"
#include "s_client.h"
#include "predict.h"
#include "engine_vars.h"

#define SMSG_NETPROTOCOLVERSION		4
#define SMSG_UNLOADWORLD			5
#define SMSG_LOADWORLD				6
#define SMSG_CLIENTOBJECTID			7
#define SMSG_UPDATE					8
#define SMSG_UNGUARANTEEDUPDATE		10
#define SMSG_YOURID					12
#define SMSG_MESSAGE				13
#define SMSG_PACKETGROUP			14
#define SMSG_CHANGEOBJECTFILENAMES	15
#define SMSG_CONSOLEVAR				16
#define SMSG_SKYDEF					17
#define SMSG_INSTANTSPECIALEFFECT	18
#define SMSG_PORTALFLAGS			19
#define SMSG_PRELOADLIST			21
#define SMSG_THREADLOAD				23
#define SMSG_UNLOAD					24
#define SMSG_LIGHTANIMINFO			25
#define SMSG_GLOBALLIGHT			26
#define SMSG_PEERAUTH				27

#define CMSG_GOODBYE				6
#define CMSG_CONNECTSTAGE			9

#define LT_NETVERSION				6

#define LTEVENT_DISCONNECT			1

void cs_UnloadWorld(CClientShell *pShell);		// 0x00416160 (clientshell.cpp)
void r_UnbindTexture(SharedTexture *pTexture);		// 0x0046f660

#define TYPECODE_MODEL		1



// Object change flags (CF_).
#define CF_NEWOBJECT		(1<<0)
#define CF_POSITION			(1<<1)
#define CF_ROTATION			(1<<2)
#define CF_FLAGS			(1<<3)
#define CF_SCALE			(1<<4)
#define CF_MODELINFO		(1<<5)
#define CF_COLORINFO		(1<<6)
#define CF_OTHER			(1<<7)		// More change flags follow.
#define CF_ATTACHMENTS		(1<<8)
#define CF_TELEPORT			(1<<9)
#define CF_SNAPROTATION		(1<<10)
#define CF_RESETANIM		(1<<13)
#define CF_SOUNDINFO		(1<<5)		// (sounds) the server ended the loop

// Update sub-packet types.
#define UPDATESUB_PLAYSOUND		0
#define UPDATESUB_SOUNDTRACK	1
#define UPDATESUB_OBJECTREMOVES	3

// Unguaranteed update flags (in the object ID word).
#define UUF_ANIMINFO		0x1000
#define UUF_ROT				0x2000
#define UUF_POS				0x4000
#define UUF_YROTATION		0x8000
#define UUF_IDMASK			0x0FFF
#define ID_TIMESTAMP		0xFFFF

// Preload list types.
#define PRELOADTYPE_START		0
#define PRELOADTYPE_END			1
#define PRELOADTYPE_MODEL		2
#define PRELOADTYPE_TEXTURE		3
#define PRELOADTYPE_SPRITE		4
#define PRELOADTYPE_SOUND		5

#define TYPECODE_SOUND		4

#ifndef MODELFLAG_CACHED
#define MODELFLAG_CACHED	(1<<0)
#endif

#ifndef IFLAG_HASCHILDMODELS
#define IFLAG_HASCHILDMODELS	(1<<10)
#endif

// Animation trackers an update packet describes (CClientShell reads them, then applies them).
#define MAX_PACKET_ANIMTRACKERS		32

// One tracker's info out of a packet (0x18 bytes).
struct AnimTrackerInfo
{
	uint32	m_iPrevWeightSet;	// 0x00
	uint32	m_iCurWeightSet;	// 0x04
	uint16	m_wFlags;			// 0x08 animation index, 0x4000 playing, 0x8000 looping
	uint16	m_nPercent;			// 0x0a how far into the animation (0-255)
	uint8	m_bAllowInterp;		// 0x0c
	uint32	m_TimeScaleNum;		// 0x10
	uint32	m_TimeScaleDenom;	// 0x14
};

struct AnimInfoSet
{
	AnimTrackerInfo	m_Trackers[MAX_PACKET_ANIMTRACKERS];	// 0x000
	uint32			m_nTrackers;							// 0x300
};

// clientmgr.cpp, cutil.cpp
LTRESULT cm_RemoveObjectFromClientWorld(CClientMgr *pClientMgr, LTObject *pObject);			// 0x00412820
LTRESULT cm_AddObjectToClientWorld(CClientMgr *pClientMgr, uint16 objectID,
	InternalObjectSetup *pSetup, LTObject **ppObject, LTBOOL bMove, LTBOOL bRotate);			// 0x004126a0
LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 id);									// 0x004265e0
ObjectMapEntry* cm_FindRecord(CClientMgr *pClientMgr, uint16 id);							// 0x00426610
void cm_ScaleObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewScale);		// 0x00426640
void cm_RelocateObject(CClientMgr *pClientMgr, LTObject *pObject);							// 0x00426730
void cm_UntagAllTextures(CClientMgr *pClientMgr);											// 0x004261a0
void cm_TagUsedTextures(CClientMgr *pClientMgr);											// 0x004261d0
void cm_FreeUnusedSharedTextures(CClientMgr *pClientMgr);									// 0x004264e0
LTRESULT cm_SetLightAnimInfo(CClientMgr *pClientMgr, HLIGHTANIM hLightAnim, LAInfo &info, LTBOOL bForce);	// 0x00404a20
LTRESULT LoadSprite(CClientMgr *pClientMgr, FileRef *pRef, Sprite **ppSprite);				// 0x00489710

// The packet handlers and helpers below call each other before they are defined.
static LTRESULT SwitchModelAnim(CClientShell *pShell, ModelInstance *pInstance, LTAnimTracker *pTracker,
	uint32 animInfo, uint32 nPercent, LTBOOL bAllowTransition, LTBOOL bAllowReset);			// 0x0048b310
static LTRESULT ApplyAnimInfo(CClientShell *pShell, ModelInstance *pInstance, LTBOOL bAllowTransition,
	LTBOOL bAllowReset, AnimInfoSet *pSet);													// 0x0048b250
static LTRESULT UnpackObjectChange(CClientShell *pShell, uint16 changeFlags, LTObject *pObject,
	CPacket *pPacket, AnimInfoSet *pSet);													// 0x0048b3f0
static LTRESULT ReadAnimInfo(CClientShell *pShell, LTObject *pObject, CPacket *pPacket,
	LTBOOL bAllowTransition, LTBOOL bAllowReset, AnimInfoSet *pSet);						// 0x0048bc20
static void ReadAnimInfoSet(CClientShell *pShell, CPacket *pPacket, AnimInfoSet *pSet, LTObject *pObject);	// 0x0048bc60
static LTRESULT ReadNewObjectInfo(CPacket *pPacket, InternalObjectSetup *pSetup, CPacket *pSFXData);	// 0x0048bfe0
static LTRESULT ReadPlaySound(CClientShell *pShell, CPacket *pPacket);						// 0x0048c380
static LTRESULT ReadSoundSubPacket(CClientShell *pShell, CPacket *pPacket, uint16 objectID, uint16 flags);	// 0x0048c750
static LTRESULT ReadNewSoundInfo(CClientShell *pShell, CPacket *pPacket, PlaySoundInfo *pPlaySoundInfo,
	FileRef *pFileRef, float *pfOffsetTime);													// 0x0048c940
static LTRESULT ReadObjectRemoves(CClientShell *pShell, CPacket *pPacket);					// 0x0048cd40


// The main list of packet handlers.
typedef LTRESULT (*ShellPacketHandlerFn)(CClientShell *pShell, CPacket *pPacket);
struct ShellPacketHandler
{
	ShellPacketHandlerFn	fn;
};
// GLOBAL: LITHTECH 0x004e5dd0
ShellPacketHandler g_ShellHandlers[256];


// The handlers (defined below; InitHandlers installs them).
LTRESULT OnUpdatePacket(CClientShell *pShell, CPacket *pPacket);					// 0x0048ada0
LTRESULT OnUnguaranteedUpdatePacket(CClientShell *pShell, CPacket *pPacket);		// 0x0048ce50
LTRESULT OnChangeObjectFilenamesPacket(CClientShell *pShell, CPacket *pPacket);	// 0x0048d150
LTRESULT OnPreloadListPacket(CClientShell *pShell, CPacket *pPacket);			// 0x0048dbb0
LTRESULT OnLightAnimInfoPacket(CClientShell *pShell, CPacket *pPacket);			// 0x0048e3b0
LTRESULT OnServerGameTime(CClientShell *pShell, CPacket *pPacket);				// 0x0048d040


// ------------------------------------------------------------------------- //
// Packet handlers.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0048a930
LTRESULT OnYourIDPacket(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_ClientID = pPacket->ReadType((uint16*)0);
	pShell->m_bLocal = pPacket->ReadType((uint8*)0);

	con_Printf(CONRGB(250,100,100), 1, "Got ID packet (%d)", pShell->m_ClientID);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048aa50
LTRESULT OnPeerAuthPacket(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_pClientMgr->OnPeerAuthPacket(pPacket);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048aa70
LTRESULT OnLoadWorldPacket(CClientShell *pShell, CPacket *pPacket)
{
	LTRESULT dResult;
	CPacketRef pResponse;

	pShell->m_ClientObjectID = 0xFFFF;

	dResult = pShell->DoLoadWorld(pPacket, LTFALSE);
	if(dResult == LT_OK)
	{
		// Tell the server we're ready.
		pResponse = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		pResponse->m_Data[0] = CMSG_CONNECTSTAGE;
		pResponse->WriteType((uint8)0);
		pShell->m_pClientMgr->m_NetMgr.SendPacket(pResponse, pShell->m_HostID, MESSAGE_GUARANTEED);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0048aba0
LTRESULT OnUnloadWorldPacket(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_ClientObjectID = 0xFFFF;
	cs_UnloadWorld(pShell);
	return LT_OK;
}



// FUNCTION: LITHTECH 0x0048abc0
LTRESULT OnPacketGroupPacket(CClientShell *pShell, CPacket *pPacket)
{
	CPacketRef pSubPacket;
	uint16 nLength;
	uint8 packetID;
	LTRESULT dResult;

	pSubPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	for(;;)
	{
		if(!((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0))
			break;
		nLength = pPacket->ReadType((uint8*)0);
		if((int)nLength > (int)(pPacket->m_DataLen - pPacket->m_Pos))
		{
			pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
			RETURN_ERROR_PARAM(1, OnMessageGroupPacket, LT_INVALIDSERVERPACKET, "invalid packet");
		}
		else if(nLength == 0)
		{
			// (This signals the end of the grouped packets).
			break;
		}

		// Set up a sub-packet.
		pSubPacket->Init(nLength, MAX_PACKET_LEN);
		pPacket->ReadRaw(pSubPacket->m_Data.GetArray(), nLength);
		pSubPacket->m_DataLen = nLength;
		pSubPacket->m_Pos = 1;

		packetID = pSubPacket->GetPacketID() & 0x3F;
		if(g_ShellHandlers[packetID].fn)
		{
			dResult = g_ShellHandlers[packetID].fn(pShell, pSubPacket);
			if(dResult != LT_OK)
				return dResult;
		}
	}

	return LT_OK;
}


// Reads one object's update out of the packet (Jupiter's ReadObjectSubPacket).
inline LTRESULT ReadObjectSubPacket(CClientShell *pShell, CPacket *pPacket, uint16 objectID, uint16 flags)
{
	LTObject *pObject;
	LTRESULT dResult;
	ObjectMapEntry *pRecord;
	IClientShell *pClientShell;
	CPacketRef cSFXData;
	ObjectCreateStruct createStruct;
	InternalObjectSetup objectSetup;
	AnimInfoSet animSet;

	cSFXData = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
	objectSetup.m_pSetup = &createStruct;
	pObject = LTNULL;

	if(flags & CF_NEWOBJECT)
	{
		// If it already exists (which it really shouldn't), then get rid of the old object.
		if(objectID == (uint16)-1)
		{
			pObject = LTNULL;
		}
		else
		{
			// Get rid of a previous object, if there is one.
			pRecord = cm_FindRecord(pShell->m_pClientMgr, objectID);
			if(pRecord && pRecord->m_pRecordData)
			{
				if(pRecord->m_nRecordType == RECORDTYPE_OBJECT)
				{
					cm_RemoveObjectFromClientWorld(pShell->m_pClientMgr, (LTObject*)pRecord->m_pRecordData);
				}
				else
				{
					GetClientILTSoundMgrImpl()->RemoveInstance(*(CSoundInstance*)pRecord->m_pRecordData);
				}
			}
		}

		createStruct.Clear();
		ReadNewObjectInfo(pPacket, &objectSetup, cSFXData);

		// Add the object.  If a new position is coming or a new rotation, then we'll take care of that later...
		dResult = cm_AddObjectToClientWorld(pShell->m_pClientMgr, objectID, &objectSetup, &pObject,
			!(flags & CF_POSITION), !(flags & CF_ROTATION));
		if(dResult != LT_OK)
			return dResult;
	}
	else
	{
		pObject = cm_FindObject(pShell->m_pClientMgr, objectID);
		if(!pObject)
		{
			pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
			RETURN_ERROR(1, ReadObjectSubPacket, LT_INVALIDSERVERPACKET);
		}
	}

	if(pObject)
	{
		dResult = UnpackObjectChange(pShell, flags, pObject, pPacket, &animSet);
		if(dResult != LT_OK)
			return dResult;

		// If it was a new object and had special effect info, notify the client shell.
		if(flags & CF_NEWOBJECT)
		{
			if(cSFXData->m_DataLen > 0)
			{
				pClientShell = pShell->m_pClientMgr->m_pClientShell;
				if(pClientShell)
				{
					cSFXData->m_Pos = 1;
					pShell->m_pClientMgr->SetupPacketMessage(cSFXData);
					pClientShell->SpecialEffectNotify(pObject, &cSFXData->m_Message);

					if(pObject->m_ObjectType == OT_MODEL)
						ApplyAnimInfo(pShell, (ModelInstance*)pObject, LTFALSE, LTTRUE, &animSet);
				}
			}
		}
	}

	return LT_OK;
}


// Processes an update packet.  Any errors generated in here will cause
// a disconnection from the server.
// Inline budget: the original reads the second change flag byte out of line (ours inlines it) and
// expands CPacketRef's temporary destructor inline; ours does the opposite. Wave 5: inline_scan p1/p2 gets at
// best 566 bytes (a free call after the SoundSubPacket objectID read), never a MATCH.
// Wave 6: Jupiter's per-branch `if(dResult != LT_OK) return dResult;` and ReadObjectSubPacket's pObject store
// after the packet_Get (the original's store order) take it from 148 to 99 aligned (ignoring stack offsets);
// the two inline decisions above (and the 4-byte frame difference from the inlined ReadType) remain.
// Wave 7 phase 2: inline_budget: the first ReadType<uint8> is refused at 142.9u (cost 143), the CF_OTHER one
// inlined at 166u where the exe refuses it (needs one more pending site after it, or 143u+ charged before);
// ReadObjectSubPacket's children all get 28u, so every ~CPacketRef (42u) is refused where the exe inlines one.
// The two requirements pull in opposite directions; --solve finds no combination (<= 6 sites, |dB| <= 300).
// PARKED: inlining decisions only (CF_OTHER ReadType, one ~CPacketRef); rechecked with the R11 model: no tail site, --solve still finds no change
// STUB: LITHTECH 0x0048ada0
LTRESULT OnUpdatePacket(CClientShell *pShell, CPacket *pPacket)
{
	uint16 flags, objectID;
	uint8 subType;
	LTRESULT dResult;

	// Debug output..
	if(g_bDebugPackets > 3)
	{
		con_WhitePrintf("Update packet with game time %f", pShell->m_GameTime);
		con_WhitePrintf("");
	}

	// The grouped sub-packets come first.
	dResult = OnPacketGroupPacket(pShell, pPacket);
	if(dResult != LT_OK)
		return dResult;

	while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
	{
		flags = pPacket->ReadType((uint8*)0);

		// Get more change flags if there are any.
		if(flags & CF_OTHER)
			flags |= (uint16)pPacket->ReadType((uint8*)0) << 8;

		// Object identification.
		if(flags)
		{
			objectID = pPacket->ReadType((uint16*)0);
			dResult = ReadObjectSubPacket(pShell, pPacket, objectID, flags);
			if(dResult != LT_OK)
				return dResult;
		}
		else
		{
			subType = pPacket->ReadType((uint8*)0);

			if(subType == UPDATESUB_PLAYSOUND)
			{
				dResult = ReadPlaySound(pShell, pPacket);
				if(dResult != LT_OK)
					return dResult;
			}
			else if(subType == UPDATESUB_SOUNDTRACK)
			{
				flags = pPacket->ReadType((uint8*)0);

				// Object identification.
				objectID = pPacket->ReadType((uint16*)0);
				dResult = ReadSoundSubPacket(pShell, pPacket, objectID, flags);
				if(dResult != LT_OK)
					return dResult;
			}
			else if(subType == UPDATESUB_OBJECTREMOVES)
			{
				dResult = ReadObjectRemoves(pShell, pPacket);
				if(dResult != LT_OK)
					return dResult;
			}
			else
			{
				pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
				RETURN_ERROR(1, OnUpdatePacket, LT_INVALIDSERVERPACKET);
			}
		}
	}

	return LT_OK;
}


// Applies the trackers read out of a packet to the model's trackers.
// FUNCTION: LITHTECH 0x0048b250
static LTRESULT ApplyAnimInfo(CClientShell *pShell, ModelInstance *pInstance, LTBOOL bAllowTransition,
	LTBOOL bAllowReset, AnimInfoSet *pSet)
{
	LTAnimTracker *pTracker;
	AnimTrackerInfo *pInfo;
	uint32 i, timeScaleNum, timeScaleDenom;
	LTRESULT dResult;

	pTracker = pInstance->m_AnimTrackers;
	for(i=0; i < pSet->m_nTrackers && pTracker; i++)
	{
		pInfo = &pSet->m_Trackers[i];

		pTracker->m_bAllowInterpolation = pInfo->m_bAllowInterp;

		timeScaleNum = pInfo->m_TimeScaleNum;
		timeScaleDenom = pInfo->m_TimeScaleDenom;
		if(timeScaleNum == 0)
			timeScaleNum = 1;
		pTracker->m_TimeScaleNum = timeScaleNum;
		if(timeScaleDenom == 0)
			timeScaleDenom = 1;
		pTracker->m_TimeScaleDenom = timeScaleDenom;

		if(bAllowReset)
		{
			if(!pInfo->m_bAllowInterp)
				pTracker->m_TimeRef.m_Prev.m_iWeightSet = pInfo->m_iCurWeightSet;
			else
				pTracker->m_TimeRef.m_Prev.m_iWeightSet = pInfo->m_iPrevWeightSet;
		}

		pTracker->m_TimeRef.m_Cur.m_iWeightSet = pInfo->m_iCurWeightSet;

		dResult = SwitchModelAnim(pShell, pInstance, pTracker, pInfo->m_wFlags, pInfo->m_nPercent,
			bAllowTransition, bAllowReset);
		if(dResult != LT_OK)
			return dResult;

		pTracker = pTracker->GetNext();
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048b310
static LTRESULT SwitchModelAnim(CClientShell *pShell, ModelInstance *pInstance, LTAnimTracker *pTracker,
	uint32 animInfo, uint32 nPercent, LTBOOL bAllowTransition, LTBOOL bAllowReset)
{
	Model *pModel;
	ModelAnim *pAnim;
	uint32 iAnim, animTime;
	LTBOOL bTransition, bPlaying;

	bTransition = (pInstance->m_Flags & FLAG_ANIMTRANSITION) && bAllowTransition;

	iAnim = animInfo & ~0xC000;
	pModel = pTracker->GetModel();
	if(iAnim >= pModel->NumAnims())
		pAnim = LTNULL;
	else
		pAnim = pModel->GetAnim(iAnim);

	bPlaying = (animInfo >> 14) & 1;

	if(animInfo & 0x8000)
		pTracker->m_Flags |= AT_LOOPING;
	else
		pTracker->m_Flags &= ~AT_LOOPING;

	if(bPlaying)
		pTracker->m_Flags |= AT_PLAYING;
	else
		pTracker->m_Flags &= ~AT_PLAYING;

	// Don't reset if they don't want.
	if(bPlaying && !bAllowReset && iAnim == pTracker->m_TimeRef.m_Cur.m_iAnim)
		return LT_OK;

	trk_SetCurAnim(pTracker, iAnim, bTransition);

	animTime = pAnim ? pAnim->GetAnimTime() : 1;
	animTime = (animTime * nPercent) / 255;

	if(bPlaying)
	{
		trk_SetCurTime(pTracker, animTime, bTransition);
	}
	else
	{
		trk_SetAtKeyFrame(pTracker, animTime);
	}

	return LT_OK;
}


void PrintPacketDebugInfo(LTObject *pObject, uint32 flags);		// 0x0048bb00

// FUNCTION: LITHTECH 0x0048b3f0
static LTRESULT UnpackObjectChange(CClientShell *pShell, uint16 changeFlags, LTObject *pObject,
	CPacket *pPacket, AnimInfoSet *pSet)
{
	LTVector newPos, newScale, newVel, offset;
	LTRotation newRot, rotationOffset;
	uint32 nodeIndex;
	LTRESULT dResult;
	uint16 childID;

	// Do some debug output.
	PrintPacketDebugInfo(pObject, changeFlags);

	// Automatically teleport new objects.
	if(changeFlags & CF_NEWOBJECT)
	{
		changeFlags |= CF_TELEPORT;
		pObject->m_BPriority = pPacket->ReadType((uint8*)0);
	}

	if(changeFlags & (CF_MODELINFO|CF_RESETANIM))
	{
		if(pObject->m_ObjectType == OT_MODEL)
		{
			dResult = ReadAnimInfo(pShell, pObject, pPacket, !(changeFlags & CF_NEWOBJECT), LTTRUE, pSet);
			if(dResult != LT_OK)
				return dResult;
		}
		else if(pObject->m_ObjectType == OT_SPRITE)
		{
			((SpriteInstance*)pObject)->m_ClipperPoly = pPacket->ReadType((uint32*)0);
		}
	}

	// Unpack the data.
	if(changeFlags & CF_FLAGS)
	{
		// Read in the flags but keep anything that wasn't in the client flag mask.
		pObject->m_Flags = pPacket->ReadType((uint32*)0) | (pObject->m_Flags & ~CLIENT_FLAGMASK);
		pObject->m_Flags2 = pPacket->ReadType((uint16*)0);
		pObject->m_UserFlags = pPacket->ReadType((uint32*)0);

		if(pObject->m_ObjectID == pShell->m_ClientObjectID)
			pObject->m_Flags |= FLAG_CLIENTNONSOLID;
	}

	if(changeFlags & CF_COLORINFO)
	{
		pObject->m_ColorR = pPacket->ReadType((uint8*)0);
		pObject->m_ColorG = pPacket->ReadType((uint8*)0);
		pObject->m_ColorB = pPacket->ReadType((uint8*)0);
		pObject->m_ColorA = pPacket->ReadType((uint8*)0);

		if(pObject->m_ObjectType == OT_LIGHT)
		{
			((DynamicLight*)pObject)->m_LightRadius = (float)pPacket->ReadType((uint16*)0);

			// Relocate the object in the BSP (if it isn't going to be relocated anyway).
			if(!(changeFlags & CF_SCALE))
				cm_RelocateObject(pShell->m_pClientMgr, pObject);
		}
	}

	if(changeFlags & CF_SCALE)
	{
		newScale.x = pPacket->ReadType((float*)0);
		newScale.y = pPacket->ReadType((float*)0);

		if(pObject->m_ObjectType == OT_SPRITE)
			newScale.z = 1.0f;
		else
			newScale.z = pPacket->ReadType((float*)0);

		cm_ScaleObject(pShell->m_pClientMgr, pObject, &newScale);
	}

	if(changeFlags & (CF_POSITION|CF_TELEPORT))
	{
		if(pObject->m_Flags & FLAG_FULLPOSITIONRES)
		{
			newPos.x = pPacket->ReadType((float*)0);
			newPos.y = pPacket->ReadType((float*)0);
			newPos.z = pPacket->ReadType((float*)0);
			newVel = pPacket->m_Message.ReadVector();
		}
		else
		{
			ic_ReadCompPos(pPacket->GetMessageImpl(), &newPos, pShell->GetWorld());
			newVel = pPacket->m_Message.ReadCompVector();
		}

		pd_OnObjectMove(pShell, pObject, &newPos, &newVel, changeFlags & CF_NEWOBJECT, changeFlags & CF_TELEPORT);
	}

	if(changeFlags & (CF_ROTATION|CF_SNAPROTATION))
	{
		if(pObject->m_Flags & FLAG_FULLPOSITIONRES)
			pPacket->m_Message >> newRot;
		else
			ic_ReadCompRotation(&pPacket->m_Message, &newRot);

		pd_OnObjectRotate(pShell, pObject, &newRot, changeFlags & CF_NEWOBJECT, changeFlags & (CF_TELEPORT|CF_SNAPROTATION));
	}

	if(changeFlags & CF_ATTACHMENTS)
	{
		if(pPacket->ReadType((uint8*)0))
			pObject->m_InternalFlags |= IFLAG_HASCHILDMODELS;
		else
			pObject->m_InternalFlags &= ~IFLAG_HASCHILDMODELS;

		// Remove its attachments.
		om_RemoveAttachments(&pShell->m_pClientMgr->m_ObjectMgr, pObject);

		while((childID = pPacket->ReadType((uint16*)0)) != INVALID_OBJECTID)
		{
			nodeIndex = pPacket->ReadType((uint32*)0);

			if(pObject->m_Flags & FLAG_FULLPOSITIONRES)
			{
				offset.x = pPacket->ReadType((float*)0);
				offset.y = pPacket->ReadType((float*)0);
				offset.z = pPacket->ReadType((float*)0);
				pPacket->m_Message >> rotationOffset;
			}
			else
			{
				offset = pPacket->m_Message.ReadCompVector();
				ic_ReadCompRotation(&pPacket->m_Message, &rotationOffset);
			}

			om_CreateAttachment(&pShell->m_pClientMgr->m_ObjectMgr, pObject, childID, nodeIndex, &offset, &rotationOffset, LTNULL);
		}

		if(pObject->m_ObjectType == OT_MODEL)
			((ModelInstance*)pObject)->m_HiddenPieces = pPacket->ReadType((uint32*)0);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048bb00
void PrintPacketDebugInfo(LTObject *pObject, uint32 flags)
{
	if(g_bDebugPackets == 2)
	{
		con_WhitePrintf("");
		con_WhitePrintf("ObjectID: %d, type %d, flags %d",
			pObject->m_ObjectID, (char)pObject->m_ObjectType, flags);

		if(flags & CF_NEWOBJECT)		con_WhitePrintf("CF_NEWOBJECT");
		if(flags & CF_POSITION)			con_WhitePrintf("CF_POSITION");
		if(flags & CF_ROTATION)			con_WhitePrintf("CF_ROTATION");
		if(flags & CF_FLAGS)			con_WhitePrintf("CF_FLAGS");
		if(flags & CF_SCALE)			con_WhitePrintf("CF_SCALE");
		if(flags & CF_MODELINFO)		con_WhitePrintf("CF_MODELINFO");
		if(flags & CF_COLORINFO)		con_WhitePrintf("CF_COLORINFO");
		if(flags & CF_ATTACHMENTS)		con_WhitePrintf("CF_ATTACHMENTS");
	}
	else if(g_bDebugPackets == 1)
	{
		if(flags & CF_NEWOBJECT)
		{
			con_WhitePrintf("");
			con_WhitePrintf("ObjectID: %d, type: %d, flags %d",
				pObject->m_ObjectID, (char)pObject->m_ObjectType, flags);
			con_WhitePrintf("CF_NEWOBJECT");
		}
	}
}


// Reads and applies animation info out of the packet.
// FUNCTION: LITHTECH 0x0048bc20
static LTRESULT ReadAnimInfo(CClientShell *pShell, LTObject *pObject, CPacket *pPacket,
	LTBOOL bAllowTransition, LTBOOL bAllowReset, AnimInfoSet *pSet)
{
	ReadAnimInfoSet(pShell, pPacket, pSet, pObject);
	return ApplyAnimInfo(pShell, (ModelInstance*)pObject, bAllowTransition, bAllowReset, pSet);
}


// The model's dims and its trackers' info: what the server's WriteAnimInfo wrote.
// FUNCTION: LITHTECH 0x0048bc60
static void ReadAnimInfoSet(CClientShell *pShell, CPacket *pPacket, AnimInfoSet *pSet, LTObject *pObject)
{
	MoveState moveState;
	LTVector newDims;
	uint32 nTrackers, i;
	AnimTrackerInfo *pInfo;
	uint16 wFlags;
	uint8 nPercent, bAllowInterp, curWeightSet;
	uint32 prevWeightSet;
	uint32 timeScaleNum, timeScaleDenom;

	newDims = pPacket->m_Message.ReadCompVector();
	if(pShell->m_pClientMgr && pObject)
	{
		moveState.Setup(&pShell->m_pClientMgr->m_World.m_WorldTree, pShell->m_pClientMgr->m_MoveAbstract,
			pObject, pObject->m_BPriority);
		ChangeObjectDimensions(&moveState, &newDims, LTFALSE, LTTRUE);
	}

	pSet->m_nTrackers = 0;
	nTrackers = pPacket->ReadType((uint8*)0);
	for(i=0; i < nTrackers; i++)
	{
		wFlags = pPacket->ReadType((uint16*)0);
		nPercent = pPacket->ReadType((uint8*)0);
		bAllowInterp = pPacket->ReadType((uint8*)0);
		timeScaleNum = pPacket->ReadType((uint32*)0);
		timeScaleDenom = pPacket->ReadType((uint32*)0);
		prevWeightSet = pPacket->ReadType((uint8*)0);
		curWeightSet = pPacket->ReadType((uint8*)0);

		if(pSet->m_nTrackers < MAX_PACKET_ANIMTRACKERS)
		{
			pInfo = &pSet->m_Trackers[pSet->m_nTrackers];
			pInfo->m_iPrevWeightSet = prevWeightSet;
			pInfo->m_iCurWeightSet = curWeightSet;
			pInfo->m_wFlags = wFlags;
			pInfo->m_nPercent = nPercent;
			pInfo->m_bAllowInterp = bAllowInterp;
			pInfo->m_TimeScaleNum = timeScaleNum;
			pInfo->m_TimeScaleDenom = timeScaleDenom;
			pSet->m_nTrackers++;
		}
	}
}


// A new object's type, filenames and special effect data.
// FUNCTION: LITHTECH 0x0048bfe0
static LTRESULT ReadNewObjectInfo(CPacket *pPacket, InternalObjectSetup *pStruct, CPacket *pSFXData)
{
	LTBOOL bSFXMessage;
	uint8 objectType, longSFXMark;
	int i;

	objectType = pPacket->ReadType((uint8*)0);

	if(objectType & 0x20)
		pStruct->m_pSetup->m_CreateFlags |= OCS_AUTOLOAD;
	else
		pStruct->m_pSetup->m_CreateFlags &= ~OCS_AUTOLOAD;

	bSFXMessage = (objectType & 0x40);
	longSFXMark = (objectType & 0x80);
	objectType = objectType & 0x1F;

	// Read special effect information.
	if(bSFXMessage)
	{
		if(longSFXMark)
			pSFXData->m_DataLen = pPacket->ReadType((uint16*)0);
		else
			pSFXData->m_DataLen = pPacket->ReadType((uint8*)0);
	}
	else
	{
		pSFXData->m_DataLen = 0;
	}

	if(pSFXData->m_DataLen)
	{
		pPacket->ReadRaw(&pSFXData->m_Data[1], pSFXData->m_DataLen);
		pSFXData->m_DataLen++;
	}

	// Read filename and/or skin name.
	pStruct->m_Filename.m_FileType = FILE_SERVERFILE;
	if(objectType == OT_WORLDMODEL)
	{
		strncpy(pStruct->m_pSetup->m_Filename, pPacket->ReadString(), MAX_CS_FILENAME_LEN);
	}
	else if(objectType == OT_CONTAINER)
	{
		strncpy(pStruct->m_pSetup->m_Filename, pPacket->ReadString(), MAX_CS_FILENAME_LEN);
		pStruct->m_pSetup->m_ContainerCode = pPacket->ReadType((uint16*)0);
	}
	else if(objectType == OT_MODEL)
	{
		// (The original sets the type again here.)
		pStruct->m_Filename.m_FileType = FILE_SERVERFILE;
		pStruct->m_Filename.m_FileID = pPacket->ReadType((uint16*)0);
		for(i=0; i < MAX_MODEL_TEXTURES; i++)
		{
			pStruct->m_SkinNames[i].m_FileType = FILE_SERVERFILE;
			pStruct->m_SkinNames[i].m_FileID = pPacket->ReadType((uint16*)0);
		}
	}
	else if(objectType == OT_SPRITE)
	{
		pStruct->m_Filename.m_FileID = pPacket->ReadType((uint16*)0);
		pStruct->m_Filename.m_FileType = FILE_SERVERFILE;
	}

	pStruct->m_pSetup->m_ObjectType = objectType;
	return LT_OK;
}




// Keep the chained zero stores behind an inline call: VC6 counts this site when
// deciding which earlier packet reads to inline. The original helper name is unknown.
static inline void InitSoundPosition(LTVector &pos)
{
	VEC_INIT(pos);
}

// FUNCTION: LITHTECH 0x0048c380
static LTRESULT ReadPlaySound(CClientShell *pShell, CPacket *pPacket)
{
	PlaySoundInfo playSoundInfo;
	FileRef playSoundFileRef;
	LTBOOL bLocalOverride;
	FileIDInfo *pFileIDInfo, fileIDInfoNew;

	PLAYSOUNDINFO_INIT(playSoundInfo);

	playSoundFileRef.m_FileType = FILE_SERVERFILE;
	playSoundFileRef.m_FileID = pPacket->ReadType((uint16*)0);

	// Get the saved fileid info structure...
	pFileIDInfo = pShell->GetClientFileIDInfo(playSoundFileRef.m_FileID);
	if(!pFileIDInfo)
		pFileIDInfo = &fileIDInfoNew;

	pFileIDInfo->m_nChangeFlags = pPacket->ReadType((uint8*)0);

	if(pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPLAYSOUNDFLAGS)
	{
		// Only a word's worth of data is needed for flags right now...
		pFileIDInfo->m_wSoundPlaySoundFlags = pPacket->ReadType((uint16*)0);
	}
	playSoundInfo.m_dwFlags = pFileIDInfo->m_wSoundPlaySoundFlags;

	playSoundInfo.m_dwFlags |= PLAYSOUND_CLIENT;
	bLocalOverride = (playSoundInfo.m_dwFlags & PLAYSOUND_CLIENTLOCAL) ? LTTRUE : LTFALSE;

	if(pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPRIORITY)
		pFileIDInfo->m_nSoundPriority = pPacket->ReadType((uint8*)0);

	playSoundInfo.m_nPriority = pFileIDInfo->m_nSoundPriority;

	if(playSoundInfo.m_dwFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
	{
		// If this is a remote positional sound, then get the radii info...
		if(pFileIDInfo->m_nChangeFlags & FILEIDINFOF_RADIUS)
		{
			pFileIDInfo->m_nSoundOuterRadius = pPacket->ReadType((uint16*)0);
			pFileIDInfo->m_nSoundInnerRadius = pPacket->ReadType((uint8*)0);
		}
		playSoundInfo.m_fOuterRadius = (float)pFileIDInfo->m_nSoundOuterRadius;
		playSoundInfo.m_fInnerRadius = (float)pFileIDInfo->m_nSoundInnerRadius * playSoundInfo.m_fOuterRadius / 255.0f;
	}

	if(playSoundInfo.m_dwFlags & PLAYSOUND_CTRL_VOL)
		playSoundInfo.m_nVolume = pPacket->ReadType((uint8*)0);
	else
		playSoundInfo.m_nVolume = 100;

	if(playSoundInfo.m_dwFlags & PLAYSOUND_CTRL_PITCH)
		playSoundInfo.m_fPitchShift = pPacket->ReadType((float*)0);
	else
		playSoundInfo.m_fPitchShift = 1.0f;

	if(playSoundInfo.m_dwFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
	{
		// If this is a remote positional sound, then get the position...
		if(!bLocalOverride)
		{
			ic_ReadCompPos(pPacket->GetMessageImpl(), &playSoundInfo.m_vPosition, pShell->GetWorld());
		}
		else
		{
			if(pShell->m_pFrameClientObject)
				playSoundInfo.m_vPosition = pShell->m_pFrameClientObject->GetPos();
			else
				InitSoundPosition(playSoundInfo.m_vPosition);
		}
	}

	if(playSoundInfo.m_dwFlags & PLAYSOUND_CTRL_TYPE)
		playSoundInfo.m_nUserSoundType = pPacket->ReadType((uint8*)0);

	if(playSoundInfo.m_dwFlags & PLAYSOUND_USER_DATA)
		playSoundInfo.m_UserData = pPacket->ReadType((uint32*)0);

	pShell->m_pClientMgr->PlaySound(&playSoundInfo, &playSoundFileRef, 0.0f);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048c750
static LTRESULT ReadSoundSubPacket(CClientShell *pShell, CPacket *pPacket, uint16 objectID, uint16 flags)
{
	CSoundInstance *pSoundInst;
	ObjectMapEntry *pRecord;
	FileRef fileRef;
	float fOffsetTime;
	PlaySoundInfo playSoundInfo;
	uint8 nFadeOut;

	pSoundInst = LTNULL;

	if(flags & CF_NEWOBJECT)
	{
		// If it already exists (which it really shouldn't), then get rid of the old object.
		if(objectID != (uint16)-1)
		{
			// Get rid of a previous sound, if there is one.
			pRecord = cm_FindRecord(pShell->m_pClientMgr, objectID);
			if(pRecord && pRecord->m_pRecordData)
			{
				if(pRecord->m_nRecordType == RECORDTYPE_OBJECT)
				{
					cm_RemoveObjectFromClientWorld(pShell->m_pClientMgr, (LTObject*)pRecord->m_pRecordData);
				}
				else
				{
					GetClientILTSoundMgrImpl()->RemoveInstance(*(CSoundInstance*)pRecord->m_pRecordData);
				}
			}
		}

		ReadNewSoundInfo(pShell, pPacket, &playSoundInfo, &fileRef, &fOffsetTime);

		playSoundInfo.m_hSound = (HLTSOUND)objectID;
	}
	else
	{
		// The playsound may have failed, which is not fatal...
		pRecord = cm_FindRecord(pShell->m_pClientMgr, objectID);
		if(pRecord && pRecord->m_pRecordData && pRecord->m_nRecordType == RECORDTYPE_SOUND)
			pSoundInst = (CSoundInstance*)pRecord->m_pRecordData;
	}

	// Object sound is attached to had a change of position.  This flag isn't set if the sound is
	// attached to the client object...
	if(flags & CF_POSITION)
	{
		ic_ReadCompPos(&pPacket->m_Message, &playSoundInfo.m_vPosition, pShell->GetWorld());

		if(pSoundInst)
			pSoundInst->SetPosition(playSoundInfo.m_vPosition, LTFALSE);
	}

	// Server is killing the loop.
	if(flags & CF_SOUNDINFO)
	{
		nFadeOut = pPacket->ReadType((uint8*)0);
		if(pSoundInst)
		{
			if(nFadeOut)
				pSoundInst->FadeOut((float)nFadeOut * 0.1f);
			else
				pSoundInst->EndLoop();
		}
	}

	if(flags & CF_NEWOBJECT)
		g_pClientMgr->PlaySound(&playSoundInfo, &fileRef, fOffsetTime);

	return LT_OK;
}




// FUNCTION: LITHTECH 0x0048c940
static LTRESULT ReadNewSoundInfo(CClientShell *pShell, CPacket *pPacket, PlaySoundInfo *pPlaySoundInfo,
	FileRef *pFileRef, float *pfOffsetTime)
{
	LTBOOL bLocalOverride;
	FileIDInfo *pFileIDInfo, fileIDInfoNew;
	uint32 dwOffsetTime;

	PLAYSOUNDINFO_INIT(*pPlaySoundInfo);

	pFileRef->m_FileType = FILE_SERVERFILE;
	pFileRef->m_FileID = pPacket->ReadType((uint16*)0);

	// Get the saved fileid info structure...
	pFileIDInfo = pShell->GetClientFileIDInfo(pFileRef->m_FileID);
	if(!pFileIDInfo)
		pFileIDInfo = &fileIDInfoNew;

	// The fileidinfo change flags indicate which pieces of information were sent.  Most of the time,
	// the info doesn't change for a particular file id.
	pFileIDInfo->m_nChangeFlags = pPacket->ReadType((uint8*)0);

	// Only a word's worth of data is needed for flags right now...
	if(pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPLAYSOUNDFLAGS)
		pFileIDInfo->m_wSoundPlaySoundFlags = pPacket->ReadType((uint16*)0);

	pPlaySoundInfo->m_dwFlags = pFileIDInfo->m_wSoundPlaySoundFlags;

	if(pPlaySoundInfo->m_dwFlags & PLAYSOUND_CLIENTLOCAL)
		bLocalOverride = LTTRUE;
	else
		bLocalOverride = LTFALSE;

	if(pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPRIORITY)
		pFileIDInfo->m_nSoundPriority = pPacket->ReadType((uint8*)0);

	pPlaySoundInfo->m_nPriority = pFileIDInfo->m_nSoundPriority;

	if(pPlaySoundInfo->m_dwFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
	{
		// If this is a remote positional sound, then get the radii info...
		if(pFileIDInfo->m_nChangeFlags & FILEIDINFOF_RADIUS)
		{
			pFileIDInfo->m_nSoundOuterRadius = pPacket->ReadType((uint16*)0);
			pFileIDInfo->m_nSoundInnerRadius = pPacket->ReadType((uint8*)0);
		}
		pPlaySoundInfo->m_fOuterRadius = (float)pFileIDInfo->m_nSoundOuterRadius;
		pPlaySoundInfo->m_fInnerRadius = (float)pFileIDInfo->m_nSoundInnerRadius * pPlaySoundInfo->m_fOuterRadius / 255.0f;
	}

	if(pPlaySoundInfo->m_dwFlags & PLAYSOUND_CTRL_VOL)
		pPlaySoundInfo->m_nVolume = pPacket->ReadType((uint8*)0);
	else
		pPlaySoundInfo->m_nVolume = 100;

	if(pPlaySoundInfo->m_dwFlags & PLAYSOUND_CTRL_PITCH)
		pPlaySoundInfo->m_fPitchShift = pPacket->ReadType((float*)0);
	else
		pPlaySoundInfo->m_fPitchShift = 1.0f;

	if(pPlaySoundInfo->m_dwFlags & PLAYSOUND_TIMESYNC)
	{
		dwOffsetTime = pPacket->ReadType((uint8*)0);
		if(dwOffsetTime == 0xFF)
			dwOffsetTime = pPacket->ReadType((uint32*)0);

		*pfOffsetTime = dwOffsetTime / 1000.0f;
	}
	else
	{
		*pfOffsetTime = 0;
	}

	if(pPlaySoundInfo->m_dwFlags & PLAYSOUND_CTRL_TYPE)
		pPlaySoundInfo->m_nUserSoundType = pPacket->ReadType((uint8*)0);

	if(pPlaySoundInfo->m_dwFlags & PLAYSOUND_USER_DATA)
		pPlaySoundInfo->m_UserData = pPacket->ReadType((uint32*)0);

	if(bLocalOverride)
	{
		if(pShell->m_pFrameClientObject)
			pPlaySoundInfo->m_vPosition = pShell->m_pFrameClientObject->GetPos();
		else
			pPlaySoundInfo->m_vPosition.Init();
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048cd40
static LTRESULT ReadObjectRemoves(CClientShell *pShell, CPacket *pPacket)
{
	uint16 id;
	ObjectMapEntry *pRecord;
	CSoundInstance *pSoundInstance;
	CClientMgr *pClientMgr;

	pClientMgr = pShell->m_pClientMgr;

	// Remove all the objects listed here.
	while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
	{
		id = pPacket->ReadType((uint16*)0);

		if(g_bDebugPackets == 1)
			con_WhitePrintf("Remove id %d", id);

		pRecord = cm_FindRecord(pClientMgr, id);
		if(pRecord && pRecord->m_pRecordData)
		{
			if(pRecord->m_nRecordType == RECORDTYPE_OBJECT)
			{
				cm_RemoveObjectFromClientWorld(pClientMgr, (LTObject*)pRecord->m_pRecordData);
			}
			else
			{
				pSoundInstance = (CSoundInstance*)pRecord->m_pRecordData;

				// If sound is ending a loop (or fading out), then just flag it for removal.
				if(pSoundInstance->GetSoundInstanceFlags() & (SOUNDINSTANCEFLAG_ENDLOOP | SOUNDINSTANCEFLAG_FADE))
					pSoundInstance->DisconnectFromServer();
				else
					GetClientILTSoundMgrImpl()->RemoveInstance(*pSoundInstance);
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048ce50
LTRESULT OnUnguaranteedUpdatePacket(CClientShell *pShell, CPacket *pPacket)
{
	uint16 id;
	LTVector newPos, newVel;
	LTRotation newRot;
	LTObject *pObject;
	LTRESULT dResult;
	AnimInfoSet animSet;

	// The grouped sub-packets come first.
	dResult = OnPacketGroupPacket(pShell, pPacket);
	if(dResult != LT_OK)
		return dResult;

	while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
	{
		id = pPacket->ReadType((uint16*)0);

		if(id == ID_TIMESTAMP)
		{
			// Read the rest of the packet.
			dResult = OnServerGameTime(pShell, pPacket);
			if(dResult != LT_OK)
				return dResult;
		}
		else
		{
			pObject = cm_FindObject(pShell->m_pClientMgr, id & UUF_IDMASK);

			if(id & UUF_POS)
			{
				ic_ReadCompPos(&pPacket->m_Message, &newPos, pShell->GetWorld());
				newVel = pPacket->m_Message.ReadCompVector();

				if(pObject)
					pd_OnObjectMove(pShell, pObject, &newPos, &newVel, LTFALSE, LTFALSE);
			}

			if(id & UUF_YROTATION)
			{
				ic_ReadYRotation(pPacket, &newRot);

				if(pObject)
					pd_OnObjectRotate(pShell, pObject, &newRot, LTFALSE, LTFALSE);
			}
			else if(id & UUF_ROT)
			{
				ic_ReadCompRotation(&pPacket->m_Message, &newRot);

				if(pObject)
					pd_OnObjectRotate(pShell, pObject, &newRot, LTFALSE, LTFALSE);
			}

			if(id & UUF_ANIMINFO)
			{
				ReadAnimInfoSet(pShell, pPacket, &animSet, pObject);

				if(pObject && pObject->m_ObjectType == OT_MODEL)
					ApplyAnimInfo(pShell, (ModelInstance*)pObject, LTTRUE, LTFALSE, &animSet);
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d040
LTRESULT OnServerGameTime(CClientShell *pShell, CPacket *pPacket)
{
	float newTime, delta;

	delta = 0.0f;
	newTime = pPacket->ReadType((float*)0);
	if(newTime > pShell->m_GameTime)
	{
		delta = newTime - pShell->m_GameTime;
		pShell->m_GameTime = newTime;
	}

	pShell->m_ServerPeriod = delta;
	return LT_OK;
}


// The original computes &pPacket->m_Message before loading messageID for the call (aligned: 5 mismatches; the
// original pushes &m_Message, then loads the shell, then reloads messageID).
// Wave 6 tried: locals for the client shell or the client manager (before SetupPacketMessage too, worse), inline
// accessors for both, GetMessageImpl(), an HMESSAGEREAD cast, uint32/int/uint16 messageID (worse).
// Phase 2 also tried: `messageID = 0;` before the if, an inline helper for the last-byte read, an inline bytes-left
// helper, a pMsg local before/after SetupPacketMessage, a (uint8) cast in the call: all unchanged or worse.
// Wave 7 phase 2: audit: behaviour matches. Same symptom as the server's OnMessagePacket (0x00476ca0).
// PARKED: load order around the OnMessage call (5 aligned); behaviour identical
// STUB: LITHTECH 0x0048d0f0
LTRESULT OnMessagePacket(CClientShell *pShell, CPacket *pPacket)
{
	uint8 messageID;

	// The message ID is the last byte.
	if((uint32)(pPacket->m_DataLen - pPacket->m_Pos) >= sizeof(uint8))
	{
		messageID = pPacket->m_Data[pPacket->m_DataLen - 1];
		--pPacket->m_DataLen;
	}
	else
	{
		messageID = 0;
	}

	pShell->m_pClientMgr->SetupPacketMessage(pPacket);
	pShell->m_pClientMgr->m_pClientShell->OnMessage(messageID, &pPacket->m_Message);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d150
LTRESULT OnChangeObjectFilenamesPacket(CClientShell *pShell, CPacket *pPacket)
{
	uint16 objectID;
	LTObject *pObject;
	ObjectCreateStruct createStruct;
	InternalObjectSetup objectSetup;
	int i;

	createStruct.Clear();
	objectSetup.m_pSetup = &createStruct;

	objectID = pPacket->ReadType((uint16*)0);
	objectSetup.m_Filename.m_FileID = pPacket->ReadType((uint16*)0);
	objectSetup.m_Filename.m_FileType = FILE_SERVERFILE;
	for(i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		objectSetup.m_SkinNames[i].m_FileID = pPacket->ReadType((uint16*)0);
		objectSetup.m_SkinNames[i].m_FileType = FILE_SERVERFILE;
	}

	pObject = cm_FindObject(pShell->m_pClientMgr, objectID);
	if(pObject)
	{
		// Reinitialize its 'extra data' stuff.
		return so_ExtraInit(pShell->m_pClientMgr, pObject, &objectSetup, LTFALSE);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d430
LTRESULT OnConsoleVar(CClientShell *pShell, CPacket *pPacket)
{
	char *pVarName, *pVarValue;

	pVarName = pPacket->ReadString();
	pVarValue = pPacket->ReadString();
	cc_SetConsoleVariable(&pShell->m_pClientMgr->m_ServerConsoleMirror, pVarName, pVarValue);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d470
LTRESULT OnSkyDef(CClientShell *pShell, CPacket *pPacket)
{
	uint16 i, nSkyObjects;

	pPacket->ReadRaw(&pShell->m_pClientMgr->m_SkyDef, sizeof(SkyDef));

	nSkyObjects = pPacket->ReadType((uint16*)0);
	if(nSkyObjects > MAX_SKYOBJECTS)
	{
		pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
		RETURN_ERROR_PARAM(1, OnSkyDef, LT_ERROR, "invalid packet");
	}

	memset(pShell->m_pClientMgr->m_SkyObjects, 0xFF, sizeof(pShell->m_pClientMgr->m_SkyObjects));
	for(i=0; i < nSkyObjects; i++)
	{
		pShell->m_pClientMgr->m_SkyObjects[i] = pPacket->ReadType((uint16*)0);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d610
// The original calls CPacket::ReadType<float> (0x0048e900) out of line for the first two reads only.
LTRESULT OnGlobalLight(CClientShell *pShell, CPacket *pPacket)
{
	ILTClient *pClientDE;
	LTVector vec;
	float fScale;

	pClientDE = pShell->m_pClientMgr->m_pClientDE;

	vec.x = pPacket->ReadType((float*)0);
	vec.y = pPacket->ReadType((float*)0);
	vec.z = pPacket->ReadType((float*)0);
	pClientDE->SetGlobalLightDir(vec);

	vec.x = pPacket->ReadType((float*)0);
	vec.y = pPacket->ReadType((float*)0);
	vec.z = pPacket->ReadType((float*)0);
	pClientDE->SetGlobalLightColor(vec);

	fScale = pPacket->ReadType((float*)0);
	vec.Init(fScale, fScale, fScale);
	pClientDE->SetGlobalLightScale(&vec);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d8e0
LTRESULT OnInstantSpecialEffect(CClientShell *pShell, CPacket *pPacket)
{
	IClientShell *pClientShell;

	pClientShell = pShell->m_pClientMgr->m_pClientShell;
	if(pClientShell)
	{
		pShell->m_pClientMgr->SetupPacketMessage(pPacket);
		pClientShell->SpecialEffectNotify(LTNULL, &pPacket->m_Message);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d910
LTRESULT OnClientObjectID(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_ClientObjectID = pPacket->ReadType((uint16*)0);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d9b0
LTRESULT OnPortalFlagsPacket(CClientShell *pShell, CPacket *pPacket)
{
	MainWorld *pWorld;
	WorldData *pWorldData;
	uint32 iWorldModel, iPortal;

	pWorld = pShell->GetWorld();
	iWorldModel = pPacket->ReadType((uint16*)0);
	iPortal = pPacket->ReadType((uint16*)0);

	if(iWorldModel < pWorld->m_WorldModels.GetSize())
	{
		pWorldData = pWorld->m_WorldModels[iWorldModel];
		if(iPortal < pWorldData->m_pOriginalBsp->m_nPortals)
		{
			pWorldData->m_pOriginalBsp->m_Portals[iPortal].m_Flags = pPacket->ReadType((uint8*)0);
			return LT_OK;
		}
	}

	pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
	RETURN_ERROR(1, OnPortalFlagsPacket, LT_INVALIDSERVERPACKET);
}


static inline void ModelAddRef(Model *pModel) { pModel->m_RefCount++; }

// FUNCTION: LITHTECH 0x0048dbb0
LTRESULT OnPreloadListPacket(CClientShell *pShell, CPacket *pPacket)
{
	uint8 type;
	FileRef ref;
	FileIdentifier *pFileIdent;
	Model *pModel;
	Sprite *pSprite;
	SharedTexture *pTexture;
	LTLink *pCur, *pListHead;
	CPacketRef cResponse;

	ref.m_FileType = FILE_SERVERFILE;

	type = pPacket->ReadType((uint8*)0);
	switch(type)
	{
		case PRELOADTYPE_START:
		{
			// Untag all the models.
			pListHead = &pShell->m_pClientMgr->m_TextureUsers;
			for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
			{
				((Model*)pCur->m_pData)->m_Flags &= ~MODELFLAG_CACHED;
			}

			// Untag all textures.
			cm_UntagAllTextures(pShell->m_pClientMgr);

			// Untag all sounds.
			GetClientILTSoundMgrImpl()->UntagAllSoundBuffers();
		}
		break;

		case PRELOADTYPE_END:
		{
			// Get rid of sounds we don't need.
			GetClientILTSoundMgrImpl()->RemoveAllUntaggedSoundBuffers();

			pShell->m_pClientMgr->FreeUnusedModels();
			cm_TagUsedTextures(pShell->m_pClientMgr);
			cm_FreeUnusedSharedTextures(pShell->m_pClientMgr);

			// Tell the server we're ready.
			cResponse = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
			cResponse->m_Data[0] = CMSG_CONNECTSTAGE;
			cResponse->WriteType((uint8)1);
			pShell->m_pClientMgr->m_NetMgr.SendPacket(cResponse, pShell->m_HostID, MESSAGE_GUARANTEED);
		}
		break;

		case PRELOADTYPE_MODEL:
		{
			while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
			{
				ref.m_FileID = pPacket->ReadType((uint16*)0);

				if(cm_LoadModel2(pShell->m_pClientMgr, &ref, &pModel, &pFileIdent, LTTRUE, LTTRUE) == LT_OK)
				{
					pModel->m_Flags |= MODELFLAG_CACHED;
					ModelAddRef(pModel);
				}
			}
		}
		break;

		case PRELOADTYPE_TEXTURE:
		{
			while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
			{
				ref.m_FileID = pPacket->ReadType((uint16*)0);
				pTexture = cm_AddSharedTexture(pShell->m_pClientMgr, &ref);
				if(pTexture)
				{
					pTexture->SetRefCount(pTexture->GetRefCount() + 1);
					pTexture->SetFlags(pTexture->GetFlags() | ST_TAGGED);
				}
			}
		}
		break;

		case PRELOADTYPE_SPRITE:
		{
			while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
			{
				ref.m_FileID = pPacket->ReadType((uint16*)0);
				LoadSprite(pShell->m_pClientMgr, &ref, &pSprite);
			}
		}
		break;

		case PRELOADTYPE_SOUND:
		{
			while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
			{
				ref.m_FileID = pPacket->ReadType((uint16*)0);
				pFileIdent = cf_GetFileIdentifier(pShell->m_pClientMgr->m_hFileMgr, &ref, TYPECODE_SOUND);
				if(pFileIdent)
				{
					GetClientILTSoundMgrImpl()->CreateBuffer(*pFileIdent);
				}
			}
		}
		break;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048df60
LTRESULT OnNetProtocolVersionPacket(CClientShell *pShell, CPacket *pPacket)
{
	uint32 version;

	version = pPacket->ReadType((uint32*)0);
	if(version == LT_NETVERSION)
		return LT_OK;

	pShell->m_pClientMgr->SetupError(LT_INVALIDNETVERSION, LT_NETVERSION, version);
	RETURN_ERROR(1, OnNetProtocolVersionPacket, LT_INVALIDNETVERSION);
}



// FUNCTION: LITHTECH 0x0048e030
LTRESULT OnThreadLoadPacket(CClientShell *pShell, CPacket *pPacket)
{
	FileRef ref;
	uint8 fileType;
	FileIdentifier *pIdent;
	Model *pModel;

	fileType = pPacket->ReadType((uint8*)0);
	ref.m_FileType = FILE_SERVERFILE;
	ref.m_FileID = pPacket->ReadType((uint16*)0);

	if(fileType == FT_MODEL)
	{
		cm_LoadModel2(pShell->m_pClientMgr, &ref, &pModel, &pIdent, LTTRUE, LTFALSE);
		return LT_OK;
	}
	else if(fileType == FT_TEXTURE)
	{
		cm_AddSharedTexture(pShell->m_pClientMgr, &ref);
		return LT_OK;
	}

	RETURN_ERROR(1, OnThreadLoadPacket, LT_INVALIDSERVERPACKET);
}


// FUNCTION: LITHTECH 0x0048e1d0
LTRESULT OnUnloadPacket(CClientShell *pShell, CPacket *pPacket)
{
	FileRef ref;
	uint8 fileType;
	FileIdentifier *pIdent;
	Model *pModel;

	fileType = pPacket->ReadType((uint8*)0);
	ref.m_FileType = FILE_SERVERFILE;
	ref.m_FileID = pPacket->ReadType((uint16*)0);

	if(fileType == FT_MODEL)
	{
		pIdent = cf_GetFileIdentifier(pShell->m_pClientMgr->m_hFileMgr, &ref, TYPECODE_MODEL);
		if(pIdent && pIdent->m_pData)
		{
			pModel = (Model*)pIdent->m_pData;
			cm_RemoveModelObjects(pShell->m_pClientMgr, pModel, pIdent);
			delete pModel;
		}

		return LT_OK;
	}
	else if(fileType == FT_TEXTURE)
	{
		pIdent = cf_GetFileIdentifier(pShell->m_pClientMgr->m_hFileMgr, &ref, TYPECODE_MODEL);
		if(pIdent && pIdent->m_pData)
		{
			r_UnbindTexture((SharedTexture*)pIdent->m_pData);
		}

		return LT_OK;
	}

	RETURN_ERROR(1, OnThreadLoadPacket, LT_INVALIDSERVERPACKET);
}


// FUNCTION: LITHTECH 0x0048e3b0
LTRESULT OnLightAnimInfoPacket(CClientShell *pShell, CPacket *pPacket)
{
	HLIGHTANIM hLightAnim;
	uint32 flags;
	LAInfo info;

	while(pPacket->m_DataLen - pPacket->m_Pos >= 3U)
	{
		hLightAnim = pPacket->ReadType((uint16*)0);
		flags = pPacket->ReadType((uint8*)0);

		// Start with what the light anim has now.
		pShell->m_pClientMgr->m_pClientDE->GetLightAnimLT()->GetLightAnimInfo(hLightAnim, info);

		if(flags & 1)
		{
			info.m_iFrames[0] = pPacket->ReadType((uint16*)0);
			info.m_iFrames[1] = pPacket->ReadType((uint16*)0);
			if((uint16)info.m_iFrames[0] == 0xFFFF)
				info.m_iFrames[0] = LIGHTANIMFRAME_NONE;
			if((uint16)info.m_iFrames[1] == 0xFFFF)
				info.m_iFrames[1] = LIGHTANIMFRAME_NONE;
		}

		if(flags & 2)
			info.m_fPercentBetween = (float)pPacket->ReadType((uint8*)0) / 255.0f;

		if(flags & 4)
			info.m_fBlendPercent = (float)pPacket->ReadType((uint8*)0) / 255.0f;

		if(flags & 8)
			ic_ReadCompPos(pPacket->GetMessageImpl(), &info.m_vLightPos, pShell->GetWorld());

		if(flags & 0x10)
		{
			info.m_vLightColor.x = (float)pPacket->ReadType((uint8*)0);
			info.m_vLightColor.y = (float)pPacket->ReadType((uint8*)0);
			info.m_vLightColor.z = (float)pPacket->ReadType((uint8*)0);
		}

		if(flags & 0x20)
			info.m_fLightRadius = pPacket->ReadType((float*)0);

		cm_SetLightAnimInfo(pShell->m_pClientMgr, hLightAnim, info, LTTRUE);
	}

	return LT_OK;
}


// ------------------------------------------------------------------------- //
// Main routines.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0048a7c0
LTBOOL CClientShell::NewConnectionNotify(CBaseConn *id, LTBOOL bIsLocal)
{
	if(m_HostID)
	{
		return LTFALSE;
	}
	else
	{
		m_HostID = id;
		m_bOnServer = LTTRUE;
		return LTTRUE;
	}
}


// FUNCTION: LITHTECH 0x0048a7e0
void CClientShell::DisconnectNotify(CBaseConn *id)
{
	con_Printf(CONRGB(250,100,100), 1, "Disconnected from server");

	m_HostID = LTNULL;

	if(m_pClientMgr->m_pClientShell)
	{
		m_pClientMgr->m_pClientShell->OnEvent(LTEVENT_DISCONNECT, 0);
	}
}


// FUNCTION: LITHTECH 0x0048a820
void CClientShell::HandleUnknownPacket(CPacket *pPacket, uint8 senderAddr[4], uint16 senderPort)
{
}


// FUNCTION: LITHTECH 0x0048a830
void CClientShell::SetDisconnectCode(uint32 nCode, char *pMsg)
{
	if(m_pClientMgr->m_pClientShell)
		m_pClientMgr->m_pClientShell->SetDisconnectCode(nCode, pMsg);
}


// FUNCTION: LITHTECH 0x0048a850
void CClientShell::InitHandlers()
{
	memset(g_ShellHandlers, 0, sizeof(g_ShellHandlers));

	g_ShellHandlers[SMSG_YOURID].fn = OnYourIDPacket;
	g_ShellHandlers[SMSG_LOADWORLD].fn = OnLoadWorldPacket;
	g_ShellHandlers[SMSG_UNLOADWORLD].fn = OnUnloadWorldPacket;
	g_ShellHandlers[SMSG_PEERAUTH].fn = OnPeerAuthPacket;

	g_ShellHandlers[SMSG_UPDATE].fn = OnUpdatePacket;
	g_ShellHandlers[SMSG_UNGUARANTEEDUPDATE].fn = OnUnguaranteedUpdatePacket;

	g_ShellHandlers[SMSG_MESSAGE].fn = OnMessagePacket;
	g_ShellHandlers[SMSG_PACKETGROUP].fn = OnPacketGroupPacket;
	g_ShellHandlers[SMSG_CHANGEOBJECTFILENAMES].fn = OnChangeObjectFilenamesPacket;
	g_ShellHandlers[SMSG_CONSOLEVAR].fn = OnConsoleVar;
	g_ShellHandlers[SMSG_SKYDEF].fn = OnSkyDef;
	g_ShellHandlers[SMSG_GLOBALLIGHT].fn = OnGlobalLight;
	g_ShellHandlers[SMSG_INSTANTSPECIALEFFECT].fn = OnInstantSpecialEffect;
	g_ShellHandlers[SMSG_CLIENTOBJECTID].fn = OnClientObjectID;
	g_ShellHandlers[SMSG_PORTALFLAGS].fn = OnPortalFlagsPacket;
	g_ShellHandlers[SMSG_PRELOADLIST].fn = OnPreloadListPacket;
	g_ShellHandlers[SMSG_NETPROTOCOLVERSION].fn = OnNetProtocolVersionPacket;

	g_ShellHandlers[SMSG_THREADLOAD].fn = OnThreadLoadPacket;
	g_ShellHandlers[SMSG_UNLOAD].fn = OnUnloadPacket;
	g_ShellHandlers[SMSG_LIGHTANIMINFO].fn = OnLightAnimInfoPacket;
}


// FUNCTION: LITHTECH 0x0048e770
LTRESULT CClientShell::ProcessPackets()
{
	CPacket *pPacket;
	uint8 packetID;
	LTRESULT dResult;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	// Process all the packets.
	m_pClientMgr->m_NetMgr.StartGettingPackets();
	while(m_pClientMgr->m_NetMgr.GetPacket(pPacket, (CBaseConn**)LTTRUE))
	{
		packetID = pPacket->GetPacketID() & 0x3F;

		// Call the appropriate packet handler.
		if(g_ShellHandlers[packetID].fn)
		{
			dResult = g_ShellHandlers[packetID].fn(this, pPacket);
			if(dResult != LT_OK)
			{
				m_pClientMgr->m_NetMgr.EndGettingPackets();
				pPacket->Release();
				return dResult;
			}
		}
		else
		{
			cf_ProcessPacket(m_pClientMgr->m_hFileMgr, *(CPacket_Read*)pPacket);
		}
	}

	m_pClientMgr->m_NetMgr.EndGettingPackets();
	if(pPacket)
		pPacket->Release();

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048e860
void CClientShell::SendGoodbye()
{
	CPacket *pPacket;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	if(m_HostID)
	{
		pPacket->m_Data[0] = CMSG_GOODBYE;
		m_pClientMgr->m_NetMgr.SendPacket(pPacket, m_HostID, MESSAGE_GUARANTEED);
	}

	if(pPacket)
		pPacket->Release();
}


// Template code this object instantiated first (the out-of-line copies the handlers call):
// FUNCTION: LITHTECH 0x0048b190 ??0ObjectCreateStruct@@QAE@XZ
// FUNCTION: LITHTECH 0x0048b210 ??0InternalObjectSetup@@QAE@XZ
// FUNCTION: LITHTECH 0x0048e8d0 ??4CPacketRef@@QAEPAVCPacket@@ABV0@@Z
// FUNCTION: LITHTECH 0x0048e900 ?ReadTypeImpl@CPacket@@QAEMPAM@Z
