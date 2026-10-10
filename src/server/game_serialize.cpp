// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
// Jupiter runtime/server/src/game_serialize.cpp: save/load LT objects.
// Talon passes the server manager explicitly, and the objects serialize themselves through an
// ILTMessage that wraps the save stream (a CRC follows each raw read and write).
#include <stdio.h>
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "s_object.h"
#include "interlink.h"
#include "objectmgr.h"
#include "packet.h"
#include "iltmodel.h"
#include "serverde_impl.h"
#include "game_serialize.h"
#include "moveobject.h"
#include "smoveabstract.h"
#include "server_filemgr.h"
#include "de_world.h"
#include "classbind.h"
#include "concommand.h"
#include "dhashtable.h"
#include "de_memory.h"
#include "ltengineobjects.h"

#define GAMESERIALIZE_CRC	0xABCDEFAF

#define INVALID_SERIALIZEID	0xFFFF

LTRESULT sm_CheckObjectIntegrity(CServerMgr *pServerMgr);

// Restore progress, for the load error message.
// GLOBAL: LITHTECH 0x004e3820
uint32 g_dwCurRestoreObject;
// GLOBAL: LITHTECH 0x004e3824
uint32 g_nRestoreObjects;


// ----------------------------------------------------------------------------- //
// The message the objects save and load through (12 bytes, vtable 0x004c7438). It shares
// LMessageImpl's methods (lmessage.cpp), which only go through ReadRawFL and WriteRaw and the
// helper; Talon declares them in a common base this decomp doesn't have yet.
// ----------------------------------------------------------------------------- //

class CLTMessage_Stream : public ILTMessage
{
public:
	virtual LTRESULT	Release();
	virtual char const*	ReadString();
	virtual LTRESULT	ReadByteFL(uint8 &val);
	virtual LTRESULT	ReadWordFL(uint16 &val);
	virtual LTRESULT	ReadDWordFL(uint32 &val);
	virtual LTRESULT	ReadFloatFL(float &val);
	virtual LTRESULT	ReadStringFL(char *pData, uint32 maxBytes);
	virtual LTRESULT	ReadHStringFL(HSTRING &hString);
	virtual LTRESULT	ReadHStringAsStringFL(char *pMsg, uint32 msgBufSize);
	virtual LTRESULT	ReadRawFL(void *pData, uint32 len);
	virtual LTRESULT	ReadVectorFL(LTVector &vec);
	virtual LTRESULT	ReadCompVectorFL(LTVector &vec);
	virtual LTRESULT	ReadCompPosFL(LTVector &vec);
	virtual LTRESULT	ReadRotationFL(LTRotation &rot);
	virtual LTRESULT	ReadCompRotationFL(LTRotation &rot);
	virtual LTRESULT	ReadMessageFL(ILTMessage* &pMsg);
	virtual LTRESULT	ReadObjectFL(HOBJECT &hObj);
	virtual LTRESULT	WriteByte(uint8 val);
	virtual LTRESULT	WriteWord(uint16 val);
	virtual LTRESULT	WriteDWord(uint32 val);
	virtual LTRESULT	WriteFloat(float val);
	virtual LTRESULT	WriteString(char *pData);
	virtual LTRESULT	WriteVector(LTVector &vec);
	virtual LTRESULT	WriteCompVector(LTVector &vec);
	virtual LTRESULT	WriteCompPos(LTVector &vec);
	virtual LTRESULT	WriteRotation(LTRotation &rot);
	virtual LTRESULT	WriteCompRotation(LTRotation &rot);
	virtual LTRESULT	WriteRaw(void *pData, uint32 len);
	virtual LTRESULT	WriteMessage(ILTMessage &msg);
	virtual LTRESULT	WriteHString(HSTRING hString);
	virtual LTRESULT	WriteHStringFormatted(int messageCode, ...);
	virtual LTRESULT	WriteHStringArgList(int messageCode, va_list *pList);
	virtual LTRESULT	WriteStringAsHString(char *pStr);
	virtual LTRESULT	WriteObject(HOBJECT hObj);
	virtual LTRESULT	ResetPos();
	virtual LTRESULT	GetStatus(uint32 &flags);
	virtual LTBOOL		IsInvalid();

	ILTStream*	GetStream()	{ return m_pStream; }

public:
	class LMessageHelper	*m_pHelper;	// 0x04 (CServerMgr::m_pSerializeHelper)
	ILTStream		*m_pStream;			// 0x08
};


// The save file version.
// GLOBAL: LITHTECH 0x004d3480
uint32 g_dwSaveFileVersion = 1113;


void sm_SaveObjectData(CServerMgr *pServerMgr, LTObject *pObj, ILTStream *pStream, uint32 dwParam);
void sm_SaveAttachments(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream);
void sm_SaveInterlinks(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream);


// Register allocation: the original keeps the world-model loop counter in memory and pObjects in ebx.
// Wave 5: moving every local declaration to every other position (156 compiles) found no improvement over 674.
// Wave 6: a statement hill-climb (156 candidates) found nothing either (31 aligned; ebx/ebp swapped for pObjects).
// Wave 7 phase 2: audit: behaviour matches; inline decisions already match the exe (no call differences).
// Remaining 41 aligned (31 ignoring stack offsets): pObjects in ebp instead of ebx and the world-model loop
// counter kept in a register where the exe keeps it in memory. Not tried yet: splitting the save loops into
// Jupiter-style helpers (changes the register pressure of the whole function).
// PARKED: register allocation only (pObjects ebx/ebp, loop counter in memory); behaviour identical; two waves of declaration/statement searches found nothing
// STUB: LITHTECH 0x00438fb0
void sm_SaveObjects(CServerMgr *pServerMgr, ILTStream *pStream, ObjectList *pList, uint32 dwParam,
	uint32 flags)
{
	uint32 terminator;
	LTObject *pObj;
	ObjectLink *pLink;
	HHashIterator *hIterator;
	HHashElement *hElement;
	uint8 bAnyMore;
	LTCommandVar *pCurVar;
	char strTemp[201];
	LTObject **pObjects;
	int nObjects;
	int i;
	WorldBsp *pBsp;
	uint32 nPortals, iPortal;

	// Make the actual list of ones we're going to save.
	pObjects = new LTObject*[pList->m_nInList];
	if (!pObjects)
		return;

	nObjects = 0;
	for (pLink=pList->m_pFirstLink; pLink; pLink=pLink->m_pNext)
	{
		pObj = (LTObject*)pLink->m_hObject;

		// Skip the always load objects.
		if (cb_IsClassFlagSet(pServerMgr->m_ClassMgr.m_ClassModule, pObj->sd->m_pClass, CF_ALWAYSLOAD))
			continue;

		pObjects[nObjects++] = pObj;
	}

	// Setup the serialize IDs.
	om_ClearSerializeIDs(&pServerMgr->m_ObjectMgr);
	for (i=0; i < nObjects; i++)
	{
		pObjects[i]->m_SerializeID = (uint16)i;
	}

	STREAM_WRITE(g_dwSaveFileVersion);
	STREAM_WRITE(nObjects);

	STREAM_WRITE(pServerMgr->m_nTargetTimeSteps);
	STREAM_WRITE(pServerMgr->m_TargetTimeBase);
	STREAM_WRITE(pServerMgr->m_TargetTime);
	STREAM_WRITE(pServerMgr->m_FrameCode);
	STREAM_WRITE(pServerMgr->m_LastTargetTimeBase);
	STREAM_WRITE(pServerMgr->m_GameTime);
	STREAM_WRITE(pServerMgr->m_TimeOffset);

	// Save the portal states.
	if (flags & SAVEOBJECTS_SAVEPORTALS)
	{
		pStream->WriteVal(pServerMgr->m_World.m_WorldModels.GetSize());

		for (i=0; i < pServerMgr->m_World.m_WorldModels.GetSize(); i++)
		{
			pBsp = pServerMgr->m_World.m_WorldModels[i]->m_pOriginalBsp;

			nPortals = pBsp->m_nPortals;
			STREAM_WRITE(nPortals);

			for (iPortal=0; iPortal < nPortals; iPortal++)
			{
				STREAM_WRITE(pBsp->m_Portals[iPortal].m_Flags);
			}
		}
	}
	else
	{
		pStream->WriteVal((uint32)-1);
	}

	// Write out the game console state..
	if (flags & SAVEOBJECTS_SAVEGAMECONSOLE)
	{
		bAnyMore = 1;

		hIterator = hs_GetFirstElement(pServerMgr->m_ConsoleState.m_VarHash);
		while (hIterator)
		{
			hElement = hs_GetNextElement(hIterator);
			pCurVar = (LTCommandVar*)hs_GetElementUserData(hElement);

			STREAM_WRITE(bAnyMore);

			// It does the strncpy stuff here so the file doesn't become unreadable later
			// if the string was too long for the read buffer.
			strncpy(strTemp, pCurVar->pVarName, sizeof(strTemp)-1);
			pStream->WriteString(strTemp);

			strncpy(strTemp, pCurVar->pStringVal, sizeof(strTemp)-1);
			pStream->WriteString(strTemp);
		}
	}

	bAnyMore = 0;
	STREAM_WRITE(bAnyMore);

	for (i=0; i < nObjects; i++)
	{
		sm_SaveObjectData(pServerMgr, pObjects[i], pStream, dwParam);
	}

	// Write the terminator.
	terminator = (uint32)-1;
	STREAM_WRITE(terminator);

	// Save attachments and interlinks.
	for (i=0; i < nObjects; i++)
	{
		sm_SaveAttachments(pServerMgr, pObjects[i], pStream);
		sm_SaveInterlinks(pServerMgr, pObjects[i], pStream);
	}

	bAnyMore = 0;
	STREAM_WRITE(bAnyMore);
	delete [] pObjects;
}


// FUNCTION: LITHTECH 0x004392f0
void sm_SaveObjectData(CServerMgr *pServerMgr, LTObject *pObj, ILTStream *pStream, uint32 dwParam)
{
	uint32 curPos, nextPos, objDataPos;
	CLTMessage_Stream cMsg;
	char *pStr;
	uint16 i;
	ILTModel *pModelLT;

	cMsg.m_pHelper = (LMessageHelper*)pServerMgr->m_pSerializeHelper;
	cMsg.m_pStream = pStream;

	// Make space for the 'next object' indicator.
	curPos = 0;

	nextPos = pStream->GetPos();
	STREAM_WRITE(curPos);

	objDataPos = pStream->GetPos();
	STREAM_WRITE(curPos);

	// Save the object state.
	pStream->WriteString(pObj->sd->m_pClass->m_ClassName);

	// Save stuff for the ObjectCreateStruct.
	STREAM_WRITE(pObj->m_ObjectType);
	STREAM_WRITE(pObj->m_Flags);
	STREAM_WRITE(pObj->m_Flags2);
	*pStream << pObj->m_Pos;
	STREAM_WRITE(pObj->m_Scale);
	STREAM_WRITE(pObj->m_Rotation);
	STREAM_WRITE(pObj->m_UserFlags);

	if (pObj->sd->m_hName)
		pStr = (char*)hs_GetElementKey(pObj->sd->m_hName, LTNULL);
	else
		pStr = "";

	pStream->WriteString(pStr);

	if (pObj->m_ObjectType == OT_WORLDMODEL)
	{
		pStream->WriteString(((WorldModelInstance*)pObj)->m_pOriginalBsp->m_WorldName);
	}
	else
	{
		pStr = sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObj->sd->m_pFile);
		pStream->WriteString(pStr);
	}

	if (pObj->m_ObjectType == OT_MODEL)
	{
		for (i=0; i < MAX_MODEL_TEXTURES; i++)
		{
			pStr = sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObj->sd->m_pSkins[i]);
			pStream->WriteString(pStr);
		}
	}
	else
	{
		pStr = sf_GetUsedFilename(&pServerMgr->m_FileMgr, pObj->sd->m_pSkins[0]);
		pStream->WriteString(pStr);
	}

	if (pObj->m_ObjectType == OT_LIGHT)
	{
		STREAM_WRITE(((DynamicLight*)pObj)->m_LightRadius);
	}

	if (pObj->m_ObjectType == OT_CONTAINER)
	{
		STREAM_WRITE(((ContainerInstance*)pObj)->m_ContainerCode);
	}

	STREAM_WRITE(pObj->sd->m_NextUpdate);
	STREAM_WRITE(pObj->sd->m_fDeactivationTime);
	STREAM_WRITE(pObj->sd->m_fDeactivateTimer);

	// Save other stuff.
	STREAM_WRITE(pObj->m_BPriority);

	// Write the special effect message.
	uint8 bSpecialEffectMessage = !!pObj->sd->m_pSFXMsg.m_pPacket;
	STREAM_WRITE(bSpecialEffectMessage);
	if (bSpecialEffectMessage)
	{
		STREAM_WRITE(pObj->sd->m_pSFXMsg->m_DataLen);
		pStream->Write(pObj->sd->m_pSFXMsg->m_Data.GetArray(), pObj->sd->m_pSFXMsg->m_DataLen);
	}

	STREAM_WRITE(pObj->m_ColorR);
	STREAM_WRITE(pObj->m_ColorG);
	STREAM_WRITE(pObj->m_ColorB);
	STREAM_WRITE(pObj->m_ColorA);
	STREAM_WRITE(pObj->m_Velocity);
	STREAM_WRITE(pObj->m_Acceleration);
	STREAM_WRITE(pObj->m_UnknownDC);
	STREAM_WRITE(pObj->m_FrictionCoefficient);
	STREAM_WRITE(pObj->m_Mass);
	STREAM_WRITE(pObj->m_ForceIgnoreLimitSqr);
	*pStream << pObj->m_Dims;
	STREAM_WRITE(pObj->m_InternalFlags);

	uint16 skyIndex;
	if (pObj->m_InternalFlags & IFLAG_INSKY)
	{
		skyIndex = 0;
		for (i=0; i < MAX_SKYOBJECTS; i++)
		{
			if (pServerMgr->m_SkyObjects[i] == pObj->m_ObjectID)
			{
				skyIndex = i;
			}
		}

		STREAM_WRITE(skyIndex);
	}

	// Save client data.
	uint8 bHasClient = !!pObj->sd->m_pClient;
	STREAM_WRITE(bHasClient);

	if (bHasClient)
	{
		STREAM_WRITE(pObj->sd->m_pClient->m_ClientFlags);
		pStream->WriteString(pObj->sd->m_pClient->m_Name);
	}

	// Set the 'object data position' indicator.
	curPos = pStream->GetPos();
	pStream->SeekTo(objDataPos);
	STREAM_WRITE(curPos);
	pStream->SeekTo(curPos);

	// Models save their hidden pieces and main animation tracker first.
	if (pObj->m_ObjectType == OT_MODEL)
	{
		STREAM_WRITE(((ModelInstance*)pObj)->m_HiddenPieces);

		pModelLT = pServerMgr->m_pServerInterface->GetModelLT();
		if (pModelLT)
			pModelLT->WriteTracker(&((ModelInstance*)pObj)->m_AnimTracker, (HMESSAGEWRITE)&cMsg);
	}

	pObj->sd->m_pObject->EngineMessageFn(MID_SAVEOBJECT, &cMsg, (float)dwParam);

	pStream->WriteVal((uint32)GAMESERIALIZE_CRC);

	// Store the 'next object position' indicator.
	curPos = pStream->GetPos();
	pStream->SeekTo(nextPos);
	STREAM_WRITE(curPos);
	pStream->SeekTo(curPos);
}


// FUNCTION: LITHTECH 0x00439800
LTRESULT CLTMessage_Stream::ReadRawFL(void *pData, uint32 len)
{
	LTRESULT dResult;
	uint32 crc;

	dResult = GetStream()->Read(pData, len);
	if (len && dResult == LT_OK)
	{
		dResult = GetStream()->Read(&crc, sizeof(crc));
		if (crc != GAMESERIALIZE_CRC)
			dResult = LT_ERROR;
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x00439850
LTRESULT CLTMessage_Stream::WriteRaw(void *pData, uint32 len)
{
	LTRESULT dResult;
	uint32 crc;

	dResult = GetStream()->Write(pData, len);
	if (len && dResult == LT_OK)
	{
		crc = GAMESERIALIZE_CRC;
		dResult = GetStream()->Write(&crc, sizeof(crc));
	}

	return dResult;
}


// Identical-code folded with every other "return 0" (0x00439890 is the copy that was kept).
// FUNCTION: LITHTECH 0x00439890
LTRESULT CLTMessage_Stream::Release()
{
	return LT_OK;
}

char const* CLTMessage_Stream::ReadString()
{
	return LTNULL;
}

LTRESULT CLTMessage_Stream::ResetPos()
{
	return LT_OK;
}


// FUNCTION: LITHTECH 0x004398a0
LTRESULT CLTMessage_Stream::GetStatus(uint32 &flags)
{
	flags = 0;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x004398b0
void sm_SaveAttachments(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream)
{
	Attachment *pCur;
	LTObject *pChild;
	uint16 count;

	// Get the number of (valid) attachments.
	count = 0;
	for (pCur=pObject->m_Attachments; pCur; pCur=pCur->m_pNext)
	{
		pChild = sm_FindObject(pServerMgr, pCur->m_nChildID);
		if (pChild && pChild->m_SerializeID != INVALID_SERIALIZEID)
		{
			++count;
		}
	}

	// Save each one out.
	STREAM_WRITE(count);
	for (pCur=pObject->m_Attachments; pCur; pCur=pCur->m_pNext)
	{
		pChild = sm_FindObject(pServerMgr, pCur->m_nChildID);
		if (pChild && pChild->m_SerializeID != INVALID_SERIALIZEID)
		{
			STREAM_WRITE(pCur->m_Offset.m_Pos);
			STREAM_WRITE(pCur->m_Offset.m_Rot);
			STREAM_WRITE(pChild->m_SerializeID);
			STREAM_WRITE(pCur->m_iSocket);
		}
	}
}


// FUNCTION: LITHTECH 0x00439980
void sm_SaveInterlinks(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream)
{
	uint16 count;
	LTLink *pCur;
	InterLink *pLink;
	LTObject *pOther;
	uint8 tempType;

	count = 0;
	for (pCur=pObject->sd->m_Links.m_pNext; pCur != &pObject->sd->m_Links; pCur=pCur->m_pNext)
	{
		pLink = (InterLink*)pCur->m_pData;

		if (pLink->m_pOwner == pObject &&
			(pLink->m_Type == LINKTYPE_INTERLINK || pLink->m_Type == LINKTYPE_CONTAINER))
		{
			pOther = (LTObject*)pLink->m_pOther;
			if (pOther->m_SerializeID != INVALID_SERIALIZEID)
			{
				++count;
			}
		}
	}

	STREAM_WRITE(count);
	for (pCur=pObject->sd->m_Links.m_pNext; pCur != &pObject->sd->m_Links; pCur=pCur->m_pNext)
	{
		pLink = (InterLink*)pCur->m_pData;

		if (pLink->m_pOwner == pObject &&
			(pLink->m_Type == LINKTYPE_INTERLINK || pLink->m_Type == LINKTYPE_CONTAINER))
		{
			pOther = (LTObject*)pLink->m_pOther;
			if (pOther->m_SerializeID != INVALID_SERIALIZEID)
			{
				tempType = (uint8)pLink->m_Type;
				STREAM_WRITE(tempType);
				STREAM_WRITE(pOther->m_SerializeID);
			}
		}
	}
}


LTRESULT sm_CreateNextObject(CServerMgr *pServerMgr, ILTStream *pStream, LTObject **ppObj,
	uint32 dwParam);
LTRESULT sm_RestoreNextObject(CServerMgr *pServerMgr, ILTStream *pStream, LTObject *pObj,
	uint32 dwParam);
LTRESULT sm_RestoreAttachments(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream,
	LTObject **pObjects, int objectCount);
LTRESULT sm_RestoreInterlinks(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream,
	LTObject **pObjects, int objectCount);
float time_GetTime();


// FUNCTION: LITHTECH 0x00439a50
LTRESULT sm_RestoreObjects(CServerMgr *pServerMgr, ILTStream *pStream, uint32 dwParam, uint32 flags)
{
	uint32 version, startPos;
	uint32 dummy_nTargetTimeSteps;
	float dummy_TargetTimeBase;
	float dummy_TargetTime;
	uint32 dummy_FrameCode;
	float dummy_LastTargetTimeBase;
	float dummy_GameTime;
	float dummy_TimeOffset;
	LTRESULT dResult;
	LTObject **pObjects;
	int i, objectCount;
	uint32 nWorldModels, nPortals, iPortal;
	WorldBsp *pBsp;
	uint8 bMore;
	char tempStr[256], tempStr2[256], cmd[512];

	STREAM_READ(version);
	STREAM_READ(objectCount);

	// Used for debugging...
	g_nRestoreObjects = objectCount;

	// Restore timers.
	if (flags & RESTOREOBJECTS_RESTORETIME)
	{
		STREAM_READ(pServerMgr->m_nTargetTimeSteps);
		STREAM_READ(pServerMgr->m_TargetTimeBase);
		STREAM_READ(pServerMgr->m_TargetTime);
		STREAM_READ(pServerMgr->m_FrameCode);
		STREAM_READ(pServerMgr->m_LastTargetTimeBase);
		STREAM_READ(pServerMgr->m_GameTime);
		STREAM_READ(pServerMgr->m_TimeOffset);
	}
	else
	{
		STREAM_READ(dummy_nTargetTimeSteps);
		STREAM_READ(dummy_TargetTimeBase);
		STREAM_READ(dummy_TargetTime);
		STREAM_READ(dummy_FrameCode);
		STREAM_READ(dummy_LastTargetTimeBase);
		STREAM_READ(dummy_GameTime);
		STREAM_READ(dummy_TimeOffset);
	}

	// Restore the portal states.
	STREAM_READ(nWorldModels);
	if (nWorldModels != (uint32)-1)
	{
		if (nWorldModels != pServerMgr->m_World.m_WorldModels.GetSize())
		{
			RETURN_ERROR(1, sm_RestoreObjects, LT_NOTINITIALIZED);
		}

		for (i=0; i < nWorldModels; i++)
		{
			STREAM_READ(nPortals);

			pBsp = pServerMgr->m_World.m_WorldModels[i]->m_pOriginalBsp;
			if (nPortals != pBsp->m_nPortals)
			{
				RETURN_ERROR(1, sm_RestoreObjects, LT_NOTINITIALIZED);
			}

			for (iPortal=0; iPortal < nPortals; iPortal++)
			{
				STREAM_READ(pBsp->m_Portals[iPortal].m_Flags);
			}
		}
	}

	pServerMgr->m_TimeOffset = pServerMgr->m_TargetTime - (time_GetTime() + pServerMgr->m_TimeOffset);

	if (version != g_dwSaveFileVersion)
	{
		RETURN_ERROR(1, sm_RestoreObjects, LT_INVALIDVERSION);
	}

	sm_CheckObjectIntegrity(pServerMgr);

	// Read in the console state variables.
	for (;;)
	{
		STREAM_READ(bMore);
		if (!bMore)
			break;

		pStream->ReadString(tempStr, sizeof(tempStr));
		pStream->ReadString(tempStr2, sizeof(tempStr2));
		if (pStream->ErrorStatus() != LT_OK)
		{
			RETURN_ERROR(1, RestoreObjects, LT_INVALIDFILE);
		}

		sprintf(cmd, "%s %s", tempStr, tempStr2);
		cc_HandleCommand2(&pServerMgr->m_ConsoleState, cmd, CC_NOCOMMANDS);
	}

	startPos = pStream->GetPos();

	pObjects = (LTObject**)dalloc_z(sizeof(LTObject*) * objectCount);

	// Create the objects and restore their engine variable state.
	pStream->SeekTo(startPos);
	i = 0;
	while ((dResult = sm_CreateNextObject(pServerMgr, pStream, &pObjects[i++], dwParam)) == LT_OK);

	if (dResult == LT_FINISHED)
	{
		// Set their serialize IDs.
		om_ClearSerializeIDs(&pServerMgr->m_ObjectMgr);
		for (i=0; i < objectCount; i++)
		{
			pObjects[i]->m_SerializeID = (uint16)i;
		}

		sm_CheckObjectIntegrity(pServerMgr);

		// Then tell each object to restore itself.
		pStream->SeekTo(startPos);
		g_dwCurRestoreObject = 0;
		while ((dResult = sm_RestoreNextObject(pServerMgr, pStream,
			pObjects[g_dwCurRestoreObject++], dwParam)) == LT_OK);

		// If all was well, restore attachments.
		if (dResult == LT_FINISHED)
		{
			for (i=0; i < objectCount; i++)
			{
				dResult = sm_RestoreAttachments(pServerMgr, pObjects[i], pStream, pObjects, objectCount);
				if (dResult != LT_OK)
					break;

				dResult = sm_RestoreInterlinks(pServerMgr, pObjects[i], pStream, pObjects, objectCount);
				if (dResult != LT_OK)
					break;
			}
		}
	}

	dfree(pObjects);

	if (dResult != LT_OK && dResult != LT_FINISHED)
		return dResult;

	sm_CheckObjectIntegrity(pServerMgr);
	return LT_OK;
}



// ------------------------------------------------------------------------ //
// Creates the next object from a file.
// ------------------------------------------------------------------------ //

// PRECREATE_SAVEGAME (3.0f) passed through sm_AddObjectToWorld's uint32 parameter.
#define OBJECTCREATED_SAVEGAME	0x40400000

// The mystery locals are a ServerData (tempData): the next-update/deactivation values are read into its fields.
// FUNCTION: LITHTECH 0x00439f40
LTRESULT sm_CreateNextObject(CServerMgr *pServerMgr, ILTStream *pStream, LTObject **ppObj,
	uint32 dwParam)
{
	uint32 nextObjectPos, objDataPos;
	ObjectCreateStruct createStruct;
	LTObject tempObj;
	ServerData tempData;
	LTVector tempVec;
	char className[256];
	ClassDef *pClass;
	LPBASECLASS pBaseClass;
	LTObject *pObj;
	LTRESULT dResult;
	float tempRadius = 0.0f;
	uint8 bSpecialEffectMessage, bHasClient;
	uint16 messageLen, i;
	uint16 skyIndex = 0;
	uint32 tempInternalFlags, clientFlags;
	char clientName[512];
	ClientRef *pClientRef;
	MoveState moveState;

	STREAM_READ(nextObjectPos);
	if (nextObjectPos == (uint32)-1)
		return LT_FINISHED;

	STREAM_READ(objDataPos);

	// Setup an ObjectCreateStruct and create the object.  It reads each member into the
	// LTObject first to make sure it reads in the right data type.
	createStruct.Clear();
	pStream->ReadString(className, sizeof(className));

	pClass = cb_FindClass(pServerMgr->m_ClassMgr.m_ClassModule, className);
	if (!pClass)
	{
		sm_SetupError(pServerMgr, LT_CANTRESTOREOBJECT, className);
		RETURN_ERROR_PARAM(1, sm_CreateNextObject, LT_CANTRESTOREOBJECT, className);
	}

	STREAM_READ(tempObj.m_ObjectType);
	STREAM_READ(tempObj.m_Flags);
	STREAM_READ(tempObj.m_Flags2);

	*pStream >> tempVec;
	tempObj.SetPos(tempVec);

	STREAM_READ(tempObj.m_Scale);
	STREAM_READ(tempObj.m_Rotation);
	STREAM_READ(tempObj.m_UserFlags);

	createStruct.m_ObjectType = (char)tempObj.m_ObjectType;
	createStruct.m_Flags = tempObj.m_Flags;
	createStruct.m_Flags2 = tempObj.m_Flags2;
	createStruct.m_Pos = tempObj.GetPos();
	createStruct.m_Scale = tempObj.m_Scale;
	createStruct.m_Rotation = tempObj.m_Rotation;

	pStream->ReadString(createStruct.m_Name, MAX_CS_FILENAME_LEN);
	pStream->ReadString(createStruct.m_Filename, MAX_CS_FILENAME_LEN);

	if (tempObj.m_ObjectType == OT_MODEL)
	{
		for (i=0; i < MAX_MODEL_TEXTURES; i++)
		{
			pStream->ReadString(createStruct.m_SkinNames[i], MAX_CS_FILENAME_LEN);
		}
	}
	else
	{
		pStream->ReadString(createStruct.m_SkinName, MAX_CS_FILENAME_LEN);
	}

	if (tempObj.m_ObjectType == OT_LIGHT)
	{
		STREAM_READ(tempRadius);
	}

	if (tempObj.m_ObjectType == OT_CONTAINER)
	{
		STREAM_READ(createStruct.m_ContainerCode);
	}

	STREAM_READ(tempData.m_NextUpdate);
	STREAM_READ(tempData.m_fDeactivationTime);
	STREAM_READ(tempData.m_fDeactivateTimer);

	createStruct.m_NextUpdate = tempData.m_NextUpdate;
	createStruct.m_fDeactivationTime = tempData.m_fDeactivationTime;

	// Create the object.
	pBaseClass = sm_AllocateObjectOfClass(pServerMgr, pClass);

	pBaseClass->EngineMessageFn(MID_PRECREATE, &createStruct, PRECREATE_SAVEGAME);

	dResult = sm_AddObjectToWorld(g_pServerMgr, pBaseClass, pClass, &createStruct, INVALID_OBJECTID,
		OBJECTCREATED_SAVEGAME, &pObj);
	if (dResult != LT_OK)
		return dResult;

	// Copy data over.
	pObj->m_UserFlags = tempObj.m_UserFlags;
	if (pObj->m_ObjectType == OT_LIGHT)
	{
		((DynamicLight*)pObj)->m_LightRadius = tempRadius;
	}

	if (pObj->sd->m_fDeactivationTime == tempData.m_fDeactivationTime)
		pObj->sd->m_fDeactivateTimer = tempData.m_fDeactivateTimer;

	// Now read the rest of the data in.
	STREAM_READ(pObj->m_BPriority);

	// Read the special effect message.
	STREAM_READ(bSpecialEffectMessage);
	if (bSpecialEffectMessage)
	{
		STREAM_READ(messageLen);

		pObj->sd->m_pSFXMsg = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		pObj->sd->m_pSFXMsg->Init(messageLen, MAX_PACKET_LEN);
		pObj->sd->m_pSFXMsg->m_DataLen = messageLen;

		if (pObj->sd->m_pSFXMsg->m_Data.GetSize() < messageLen)
		{
			RETURN_ERROR_PARAM(1, sm_CreateNextObject, LT_INVALIDFILE, className);
		}

		pStream->Read(pObj->sd->m_pSFXMsg->m_Data.GetArray(), messageLen);
	}

	STREAM_READ(pObj->m_ColorR);
	STREAM_READ(pObj->m_ColorG);
	STREAM_READ(pObj->m_ColorB);
	STREAM_READ(pObj->m_ColorA);
	STREAM_READ(pObj->m_Velocity);
	STREAM_READ(pObj->m_Acceleration);
	STREAM_READ(pObj->m_UnknownDC);
	STREAM_READ(pObj->m_FrictionCoefficient);
	STREAM_READ(pObj->m_Mass);
	STREAM_READ(pObj->m_ForceIgnoreLimitSqr);

	*pStream >> tempVec;
	pObj->SetDims(tempVec);

	STREAM_READ(tempInternalFlags);

	if (tempInternalFlags & IFLAG_INSKY)
	{
		STREAM_READ(skyIndex);
		if (skyIndex >= MAX_SKYOBJECTS)
		{
			sm_SetupError(pServerMgr, LT_CANTRESTOREOBJECT, className);
			RETURN_ERROR_PARAM(1, sm_CreateNextObject, LT_CANTRESTOREOBJECT, className);
		}
	}

	moveState.Setup(&pServerMgr->m_World.m_WorldTree, pServerMgr->m_MoveAbstract, pObj, pObj->m_BPriority);
	ChangeObjectDimensions(&moveState, &pObj->m_Dims, LTFALSE, LTFALSE);

	// This makes sure it gets in the correct active/inactive list.
	sm_SetObjectStateFlags(pServerMgr, pObj, tempInternalFlags & IFLAG_INACTIVE_MASK);
	pObj->m_InternalFlags = tempInternalFlags;

	// AddObjectToWorld ignores m_Pos for world models so really move it.
	FullMoveObject(pServerMgr, pObj, &createStruct.m_Pos, MO_SETCHANGEFLAG | MO_NOSLIDING);

	// Put it back in the sky..
	if (tempInternalFlags & IFLAG_INSKY)
	{
		pServerMgr->m_SkyObjects[skyIndex] = pObj->m_ObjectID;
		sm_SetSendSkyDef(g_pServerMgr);
	}

	if (tempInternalFlags & IFLAG_HASCHILDMODELS)
		SetObjectChangeFlags(pServerMgr, pObj, CF_ATTACHMENTS);

	// Read in client info.
	STREAM_READ(bHasClient);
	if (bHasClient)
	{
		STREAM_READ(clientFlags);
		pStream->ReadString(clientName, sizeof(clientName));

		pClientRef = (ClientRef*)dalloc(sizeof(ClientRef) + strlen(clientName));
		pClientRef->m_ClientFlags = clientFlags;
		strcpy(pClientRef->m_ClientName, clientName);
		pClientRef->m_ObjectID = pObj->m_ObjectID;
		pObj->m_InternalFlags |= IFLAG_HASCLIENTREF;

		dl_AddHead(&pServerMgr->m_ClientReferences, &pClientRef->m_Link, pClientRef);
	}

	*ppObj = pObj;
	pStream->SeekTo(nextObjectPos);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0043aca0
LTRESULT sm_RestoreAttachments(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream,
	LTObject **pObjects, int objectCount)
{
	uint16 i, count, serializeID;
	uint32 nodeIndex;
	LTVector offset;
	LTRotation rotationOffset;
	LTObject *pChild;

	STREAM_READ(count);
	for (i=0; i < count; i++)
	{
		STREAM_READ(offset);
		STREAM_READ(rotationOffset);
		STREAM_READ(serializeID);
		STREAM_READ(nodeIndex);

		if (serializeID >= objectCount)
		{
			RETURN_ERROR(1, RestoreAttachments, LT_ERROR);
		}

		pChild = pObjects[serializeID];
		om_CreateAttachment(&pServerMgr->m_ObjectMgr, pObject, pChild->m_ObjectID, nodeIndex,
			&offset, &rotationOffset, LTNULL);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0043ada0
LTRESULT sm_RestoreInterlinks(CServerMgr *pServerMgr, LTObject *pObject, ILTStream *pStream,
	LTObject **pObjects, int objectCount)
{
	uint16 i, count;
	uint8 tempType;
	uint16 id;

	STREAM_READ(count);
	for (i=0; i < count; i++)
	{
		STREAM_READ(tempType);
		STREAM_READ(id);

		if (id >= objectCount)
		{
			RETURN_ERROR(1, RestoreInterlinks, LT_ERROR);
		}

		CreateInterLink(pServerMgr, pObject, pObjects[id], tempType);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0043aac0
LTRESULT sm_RestoreNextObject(CServerMgr *pServerMgr, ILTStream *pStream, LTObject *pObj,
	uint32 dwParam)
{
	uint32 nextObjectPos, objDataPos, dwCRC;
	CLTMessage_Stream cMsg;
	char className[256];
	ILTModel *pModelLT;

	cMsg.m_pHelper = (LMessageHelper*)pServerMgr->m_pSerializeHelper;
	cMsg.m_pStream = pStream;

	STREAM_READ(nextObjectPos);
	if (nextObjectPos == (uint32)-1)
		return LT_FINISHED;

	STREAM_READ(objDataPos);
	pStream->ReadString(className, sizeof(className));
	pStream->SeekTo(objDataPos);

	// Models save their hidden pieces and main animation tracker first.
	if (pObj->m_ObjectType == OT_MODEL)
	{
		STREAM_READ(((ModelInstance*)pObj)->m_HiddenPieces);

		pModelLT = pServerMgr->m_pServerInterface->GetModelLT();
		if (pModelLT)
			pModelLT->ReadTracker(&((ModelInstance*)pObj)->m_AnimTracker, (HMESSAGEREAD)&cMsg);
	}

	pObj->sd->m_pObject->EngineMessageFn(MID_LOADOBJECT, &cMsg, (float)dwParam);

	STREAM_READ(dwCRC);
	if (dwCRC != GAMESERIALIZE_CRC)
	{
		uint32 curPos = pStream->GetPos();

		dsi_ConsolePrint("LOAD ERROR: Couldn't restore object (%d of %d) '%s'!!!",
			g_dwCurRestoreObject, g_nRestoreObjects, className);
		dsi_ConsolePrint("  Object saved %d bytes", nextObjectPos - objDataPos);
		dsi_ConsolePrint("  Object tried to load %d bytes!", curPos - objDataPos);
		if (curPos < nextObjectPos)
			dsi_ConsolePrint("  Restore UNDER wrote %d bytes!", nextObjectPos - curPos);
		else
			dsi_ConsolePrint("  Restore OVER wrote %d bytes!", curPos - nextObjectPos);

		RETURN_ERROR(1, sm_RestoreNextObject, LT_INVALIDFILE);
	}

	sm_CheckObjectIntegrity(pServerMgr);
	pStream->SeekTo(nextObjectPos);
	return LT_OK;
}
