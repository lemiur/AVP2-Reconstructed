// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
// Jupiter runtime/server/src/s_object.cpp
// Talon passes the server manager explicitly to most of these.
#include <string.h>
#include <stdio.h>
#include "bdefs.h"
#include "ltengineobjects.h"
#include "s_object.h"
#include "servermgr.h"
#include "dhashtable.h"
#include "de_memory.h"
#include "model.h"
#include "animtracker.h"
#include "motion.h"
#include "interlink.h"
#include "moveobject.h"
#include "smoveabstract.h"
#include "classbind.h"
#include "serverde_impl.h"

#define IFLAG_FROMCLIENTREF		(1<<7)	// Created from a client reference (keepalive).

void w_RemoveObjectFromLeaf(LTObject *pObj);	// 0x00430680
LTRESULT sm_RemoveObjectFromWorld(CServerMgr *pServerMgr, LPBASECLASS pObject);
void ServerStringKeyCallback(LTAnimTracker *pTracker, AnimKeyFrame *pFrame, char *pExtraCmd, LTBOOL bReplace);	// 0x00477640


// FUNCTION: LITHTECH 0x00476e80
LTRESULT sm_UpdateInBspStatus(CServerMgr *pServerMgr, LTObject *pObject)
{
	if (!CanOptimizeObject(pObject))
	{
		if (pObject->m_WTFrameCode == FRAMECODE_NOTINTREE)
		{
			pServerMgr->m_World.m_WorldTree.InsertObject(pObject, 0);
		}
	}
	else
	{
		pObject->RemoveFromWorldTree();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00476ed0
uint32 sm_GetNewObjectChangeFlags(CServerMgr *pServerMgr, LTObject *pObject)
{
	uint32 changeFlags;

	changeFlags = CF_NEWOBJECT | CF_TELEPORT;

	if (pObject->m_Rotation.m_Quat[0] != 0.0f || pObject->m_Rotation.m_Quat[1] != 0.0f ||
		pObject->m_Rotation.m_Quat[2] != 0.0f || pObject->m_Rotation.m_Quat[3] != 1.0f)
	{
		changeFlags |= CF_SNAPROTATION;
	}

	if (pObject->m_Flags & CLIENT_FLAGMASK || pObject->m_UserFlags != 0)
	{
		changeFlags |= CF_FLAGS;
	}

	if (pObject->m_Scale.x != 1.0f || pObject->m_Scale.y != 1.0f || pObject->m_Scale.z != 1.0f)
	{
		changeFlags |= CF_SCALE;
	}

	// Sprites default to 255:255:255:255, everything else to 0:0:0:255.
	if (pObject->m_ObjectType == OT_SPRITE)
	{
		if (pObject->m_ColorR != 255 || pObject->m_ColorG != 255 || pObject->m_ColorB != 255 || pObject->m_ColorA != 255)
			changeFlags |= CF_RENDERINFO;
	}
	else
	{
		if (pObject->m_ColorR != 0 || pObject->m_ColorG != 0 || pObject->m_ColorB != 0 || pObject->m_ColorA != 255)
			changeFlags |= CF_RENDERINFO;
	}

	if (pObject->m_ObjectType == OT_MODEL)
	{
		changeFlags |= CF_MODELINFO;
	}

	if (pObject->m_Attachments ||
		(pObject->m_ObjectType == OT_MODEL && ((ModelInstance*)pObject)->m_HiddenPieces) ||
		(pObject->m_InternalFlags & IFLAG_HASCHILDMODELS))
	{
		changeFlags |= CF_ATTACHMENTS;
	}

	return changeFlags;
}

// Frees all the cached models.
// FUNCTION: LITHTECH 0x00476ff0
void sm_FreeAllModels(CServerMgr *pServerMgr)
{
	HHashIterator *hIterator;
	HHashElement *hElement;
	Model *pModel;

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		pModel->m_RefCount++;
	}

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		pModel = (Model*)hs_GetElementUserData(hElement);

		if (pModel->m_RefCount > 0)
			pModel->m_RefCount--;

		if (pModel->m_RefCount == 0)
		{
			DEBUG_PRINT(3, ("Removing model from resource list:  %s\n", pModel->GetFilename()));

			hs_RemoveElement(pServerMgr->m_hModelTable, hElement);
			pModel->Delete();
		}
	}

	// Whine about the ones still in use.
	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		DEBUG_PRINT(3, ("Warning!  Server unable to unload model %s due to reference count!", pModel->GetFilename()));
	}
}

// Updates the object's physics (Jupiter's PhysicsUpdateObject).
inline void PhysicsUpdateObject(CServerMgr *pServerMgr, LTObject *pObj)
{
	const LTVector P0 = pObj->GetPos();
	LTVector vAcceleration = pObj->m_Acceleration;
	float dt = pServerMgr->m_FrameTime;
	ContainerPhysics cPhysics;
	MotionState *pState;
	LTLink *pCur, *pListHead;
	InterLink *pLink;
	LTVector dr;
	LTBOOL bMoved;
	int32 nActualContainers;

	// If physics is disabled, drop out early.
	if (!(pObj->m_InternalFlags & IFLAG_APPLYPHYSICS))
		return;

	pState = pServerMgr->GetMotionState();
	pState->m_dt = dt;
	pState->m_pObj = pObj;

	// If the object is a container, find what other objects are in contact with it and affect
	// their physics.
	if (!(pObj->m_Flags & FLAG_CONTAINER) && pObj->sd->m_Links.m_pNext != &pObj->sd->m_Links)
	{
		cPhysics.m_Acceleration = pObj->m_Acceleration;
		cPhysics.m_Velocity = pObj->m_Velocity;
		cPhysics.m_Flags = pObj->m_Flags;
		cPhysics.m_hObject = (HOBJECT)pObj;

		// Let each container modify the physics.
		nActualContainers = 0;
		pListHead = &pObj->sd->m_Links;
		for (pCur=pListHead->m_pNext; pCur != pListHead;)
		{
			pLink = (InterLink*)pCur->m_pData;
			pCur = pCur->m_pNext;

			if (pLink->m_Type == LINKTYPE_CONTAINER && (pLink->m_pOwner->m_Flags & FLAG_CONTAINER))
			{
				pLink->m_pOwner->sd->m_pObject->EngineMessageFn(MID_AFFECTPHYSICS, &cPhysics, 0.0f);
				nActualContainers++;
			}
		}

		pState->m_Flags = cPhysics.m_Flags;
		pState->m_pVelocity = &cPhysics.m_Velocity;
		pState->m_pAcceleration = &cPhysics.m_Acceleration;
		bMoved = CalcMotion(pState);

		pObj->sd->m_pObject->EngineMessageFn(MID_AFFECTPHYSICS, &pState->m_Offset, 0.0f);
		dr = pState->m_Offset;

		// Don't let this flag clear when in a container.
		if (nActualContainers)
			pObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	}
	else
	{
		pState->m_pAcceleration = &pObj->m_Acceleration;
		pState->m_pVelocity = &pObj->m_Velocity;
		pState->m_Flags = pObj->m_Flags;
		bMoved = CalcMotion(pState);

		if (!(pObj->m_Flags & FLAG_CONTAINER))
			pObj->sd->m_pObject->EngineMessageFn(MID_AFFECTPHYSICS, &pState->m_Offset, 0.0f);

		dr = pState->m_Offset;
	}

	if (!bMoved)
		return;

	// Call MoveObject() for it automatically if tried to move at all.
	if (dr.MagSqr() > 0.001f)
	{
		const LTVector P1 = pObj->GetPos() + dr;

		FullMoveObject(pServerMgr, pObj, &P1, MO_DETACHSTANDING | MO_MOVESTANDINGONS);

		// Remove it if it's outside.
		if ((pObj->m_Flags & FLAG_REMOVEIFOUTSIDE) && pServerMgr->m_World.m_bLoaded &&
			(pObj->GetPos().x < pServerMgr->m_World.m_BoxMin.x ||
			pObj->GetPos().y < pServerMgr->m_World.m_BoxMin.y ||
			pObj->GetPos().z < pServerMgr->m_World.m_BoxMin.z ||
			pObj->GetPos().x > pServerMgr->m_World.m_BoxMax.x ||
			pObj->GetPos().y > pServerMgr->m_World.m_BoxMax.y ||
			pObj->GetPos().z > pServerMgr->m_World.m_BoxMax.z))
		{
			AddObjectToRemoveList(pServerMgr, pObj);
			return;
		}

		// If it's still in the world, set its change flags..
		if (pObj->m_InternalFlags & IFLAG_INWORLD)
		{
			if ((pObj->GetPos() - P0).MagSqr() > 0.001f)
				SetObjectChangeFlags(pServerMgr, pObj, CF_POSITION);
		}
	}

	pObj->m_Acceleration = vAcceleration;
}


// Updates the object (called once per frame): the model's trackers, the update countdown and the
// object's physics.
// Wave 5: the original keeps ContainerPhysics' empty ctor (call 0x45c5f0, shared with RayTri's), LTVector
// MagSqr (0x438f72), operator+ (0x41f710), operator- (0x41f740) and SetObjectChangeFlags (0x477540) OUT OF
// LINE, and its frame is 0x60. Top-level ballast of ~96 units anywhere before the physics block (position
// doesn't matter) outlines them (size 1040 vs 1041 real) but still leaves ~770 bytes differing (frame and
// local order). Ruled out: moving the tracker loop into an inline helper (cost ~10, no effect); splitting the
// physics block into inline PhysicsUpdateObject (+ nested GetPhysicsVector) like Jupiter: with it the size
// is right (1056/1088) and the ctor call appears only when cPhysics lives in the nested helper (then it sits
// after the IFLAG_APPLYPHYSICS test instead of before it); any ballast then makes the whole helper
// out-of-line (size 208). So the cost is a top-level inline of ~96 units whose source is still unknown.
// Wave 6: the physics block as one inline PhysicsUpdateObject (no GetPhysicsVector, all its locals declared
// at its top) plus GetPos() for the six remove-if-outside compares: aligned 183 -> 102, SIZE 1040, and every
// out-of-line site of the original is out of line except the ContainerPhysics ctor (still inlined; its call
// is what keeps `dt` in a stack slot in the original). 8 units of direct if(0) ballast inside the helper make
// everything but the frame/ebx-ebp choice match (77 aligned, ctor still inline); 16 push the whole helper out of
// line. Tried: an IsOutsideWorld inline (Jupiter's), dt as a helper parameter, dead stores (VEC_INIT(dr),
// bMoved = FALSE: dead stores add no cost), dr.Init(), Dot for the second MagSqr: none better.
// Wave 7 phase 2: inline_budget names the exe's ContainerPhysics ctor call (0x45c5f0) ??0RayTri (ICF), so
// its "k=2" answer ignores it; with --alias "45c5f0=??0ContainerPhysics@@QAE@XZ" it says 4 extra free pending
// sites after PhysicsUpdateObject (top level, own size within +5u). Measured: 4 empty inline calls after
// PhysicsUpdateObject reproduce the exe's whole call sequence (ctor, MagSqr, operator+, FullMoveObject, ...,
// operator-, MagSqr, SetObjectChangeFlags all out of line): 111 -> 87 aligned (65 ignoring offsets), 1024
// bytes. Left then: frame 0x54 vs 0x60 (the exe has one more 12-byte temp), pServerMgr/nFrameTimeMS in
// ebp/ebx swapped, and MotionState's m_pVelocity/m_pAcceleration/m_Flags stores in the order +0xc,+0x10,+8.
// Equivalently PhysicsUpdateObject could cost ~31u more with 3 sites, ~73u with 2, ~157u with none. No real
// source for those sites found (no locals with destructors exist; Jupiter ends FullObjectUpdate with the call).
// PARKED: inlining decisions only: needs 4 more free pending sites after PhysicsUpdateObject (measured; R11 model agrees, the tail call has stack args), source unknown
// STUB: LITHTECH 0x00477120
void sm_UpdateObject(CServerMgr *pServerMgr, LTObject *pObj)
{
	uint32 nFrameTimeMS;
	LTAnimTracker *pTracker;

	// Update its server object if its a model instance.
	if (pObj->m_ObjectType == OT_MODEL)
	{
		nFrameTimeMS = (uint32)(pServerMgr->m_FrameTime * 1000.0f);
		if (nFrameTimeMS > 0)
		{
			for (pTracker=((ModelInstance*)pObj)->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
			{
				pTracker->m_StringKeyCallback = ServerStringKeyCallback;
				trk_Update(pTracker, nFrameTimeMS);
			}
		}
	}

	// Update the object (if the m_NextUpdate countdown has gone past zero).
	if (pObj->sd->m_NextUpdate > 0.0f)
	{
		pObj->sd->m_NextUpdate -= pServerMgr->m_FrameTime;

		if (pObj->sd->m_NextUpdate <= 0.0f)
		{
			// Call the update.
			pObj->m_Acceleration.Init();
			pObj->sd->m_pObject->EngineMessageFn(MID_UPDATE, LTNULL, 0.0f);

			// Don't do anything else if it was removed.
			if (!(pObj->m_InternalFlags & IFLAG_INWORLD))
				return;
		}
	}

	// Update the object's physics.
	PhysicsUpdateObject(pServerMgr, pObj);
}


// The out-of-line copy of the s_object.h inline, emitted after sm_UpdateObject (which calls it out of line).
// FUNCTION: LITHTECH 0x00477540 ?SetObjectChangeFlags@@YAKPAVCServerMgr@@PAVLTObject@@K@Z

// Model string key callback. Talon can append (or substitute) an extra command string.
// FUNCTION: LITHTECH 0x00477640
void ServerStringKeyCallback(LTAnimTracker *pTracker, AnimKeyFrame *pFrame, char *pExtraCmd, LTBOOL bReplace)
{
	LTObject *pObj;
	ArgList argList;
	char cmd[128];
	ConParse parse;

	// Make sure we have a server object for it.
	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		// Does this object care?
		if (pObj->m_Flags & FLAG_MODELKEYS)
		{
			if (pExtraCmd)
			{
				if (pFrame->m_pString[0] && !bReplace)
					sprintf(cmd, "%s; %s", pFrame->m_pString, pExtraCmd);
				else
					sprintf(cmd, "%s", pExtraCmd);

				parse.Init(cmd);
			}
			else
			{
				parse.Init(pFrame->m_pString);
			}

			argList.argv = parse.m_Args;
			while (parse.Parse())
			{
				if (parse.m_nArgs > 0)
				{
					argList.argc = parse.m_nArgs;
					pObj->sd->m_pObject->EngineMessageFn(MID_MODELSTRINGKEY, &argList, (float)pTracker->m_Index);
				}
			}
		}
	}
}

#define PT_STRING_LOAD	0

// PRECREATE_NORMAL (1.0f) passed through sm_AddObjectToWorld's uint32 parameter.
#define OBJECTCREATED_NORMAL_LOAD	0x3f800000


// Creates the world's objects from the world file.
// SAFE_STRCPY for the class name (the SDK's inline LTStrCpy) is the pending inline site that keeps the
// createStruct constructor's Init calls out of line.
// FUNCTION: LITHTECH 0x00477750
LTRESULT LoadObjects(CServerMgr *pServerMgr, ILTStream *pStream, char *pWorldName, LTBOOL bAllObjects)
{
	uint32 i, k, nObjects, nProperties, nObjectDataOffset, dwDummy;
	uint16 propLen, objDataLen;
	uint8 propCode;
	char typeName[256], propName[256], propString[600];
	ClassDef *pClass;
	LPBASECLASS pObject;
	LTObject *pObj;
	ObjectCreateStruct createStruct;
	PropEntry *pProp, *pNext;
	uint32 objStartPos, dwPropFlags;

	pStream->SeekTo(0);
	STREAM_READ(dwDummy);
	STREAM_READ(nObjectDataOffset);

	// Load the objects.
	pStream->SeekTo(nObjectDataOffset);

	// For each object....
	STREAM_READ(nObjects);
	for (i=0; i < nObjects; i++)
	{
		STREAM_READ(objDataLen);
		objStartPos = pStream->GetPos();

		pStream->ReadString(typeName, sizeof(typeName));

		if (pStream->ErrorStatus() != LT_OK)
		{
			sm_SetupError(pServerMgr, LT_INVALIDWORLDFILE, pWorldName);
			RETURN_ERROR(1, LoadObjects, LT_INVALIDWORLDFILE);
		}

		// Get the class.
		pClass = cb_FindClass(pServerMgr->m_ClassMgr.m_ClassModule, typeName);

		// Set things up to succeed anyway if we don't have that class.
		if (pClass)
		{
			// If it's not supposed to be created at runtime, ignore it.
			if (pClass->m_ClassFlags & CF_NORUNTIME)
			{
				pClass = LTNULL;
			}
			// If only loading LOADALWAYS objects, then skip the ones without the flag set...
			else if (!bAllObjects && !cb_IsClassFlagSet(pServerMgr->m_ClassMgr.m_ClassModule, pClass, CF_ALWAYSLOAD))
			{
				pClass = LTNULL;
			}
		}
		else
		{
			// This can happen if a level used an object that did not exist in the class module.
			dsi_ConsolePrint("Server is missing class %s", typeName);
		}

		// Create and construct an instance of it.
		if (pClass)
			pObject = sm_AllocateObjectOfClass(pServerMgr, pClass);
		else
			pObject = LTNULL;

		createStruct.Clear();
		createStruct.m_Flags = 0;
		createStruct.m_ObjectType = OT_NORMAL;
		createStruct.m_Filename[0] = 0;
		createStruct.m_SkinName[0] = 0;
		createStruct.m_Pos.Init();

		// Read in all the properties.
		STREAM_READ(nProperties);
		for (k=0; k < nProperties; k++)
		{
			// Name.
			pStream->ReadString(propName, sizeof(propName));

			// Property length.
			STREAM_READ(propCode);
			STREAM_READ(dwPropFlags);
			STREAM_READ(propLen);

			pProp = (PropEntry*)dalloc(propLen + sizeof(PropEntry) - sizeof(pProp->m_Data));
			pProp->m_Type = propCode;
			strncpy(pProp->m_Name, propName, sizeof(pProp->m_Name) - 1);

			if (propCode == PT_STRING_LOAD)
			{
				pStream->ReadString(propString, sizeof(propString));
				strncpy((char*)pProp->m_Data, propString, propLen - 1);
			}
			else
			{
				pStream->Read(pProp->m_Data, propLen);
			}

			pProp->m_pNext = g_pServerMgr->m_pCurProps;
			g_pServerMgr->m_pCurProps = pProp;
		}

		if (pClass && pObject)
		{
			SAFE_STRCPY(createStruct.m_ClassName, typeName);

			pObject->EngineMessageFn(MID_PRECREATE, &createStruct, PRECREATE_WORLDFILE);

			if (sm_AddObjectToWorld(pServerMgr, pObject, pClass, &createStruct, INVALID_OBJECTID,
				OBJECTCREATED_NORMAL_LOAD, &pObj) != LT_OK)
			{
				sm_FreeObjectOfClass(pServerMgr, pClass, pObject);
			}
		}

		// Free the property list.
		pProp = g_pServerMgr->m_pCurProps;
		while (pProp)
		{
			pNext = pProp->m_pNext;
			dfree(pProp);
			pProp = pNext;
		}

		g_pServerMgr->m_pCurProps = LTNULL;
	}

	if (pStream->ErrorStatus() != LT_OK)
	{
		sm_SetupError(pServerMgr, LT_INVALIDWORLDFILE, pWorldName);
		RETURN_ERROR(1, LoadObjects, LT_INVALIDWORLDFILE);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00477ce0
void AddObjectToRemoveList(CServerMgr *pServerMgr, LTObject *pObj)
{
	LTLink *pLink;

	if ((~pServerMgr->m_InternalFlags & SIFLAG_REMOVINGALLOBJECTS) &&
		(~pObj->m_InternalFlags & IFLAG_OBJECTGOINGAWAY))
	{
		pLink = g_DLinkBank.Allocate();
		pLink->m_pData = pObj;
		dl_Insert(&pServerMgr->m_RemovedObjectHead, pLink);

		pObj->m_InternalFlags |= IFLAG_OBJECTGOINGAWAY;
		w_RemoveObjectFromLeaf(pObj);
		pObj->m_InternalFlags &= ~IFLAG_INWORLD;
	}
}

// FUNCTION: LITHTECH 0x00477d80
void sm_RemoveObjectsThatNeedToGetRemoved(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext;
	LTObject *pObj;

	pCur = pServerMgr->m_RemovedObjectHead.m_pNext;
	while (pCur != &pServerMgr->m_RemovedObjectHead)
	{
		pObj = (LTObject*)pCur->m_pData;
		sm_RemoveObjectFromWorld(pServerMgr, pObj->sd->m_pObject);

		pNext = pCur->m_pNext;
		dl_Remove(pCur);
		g_DLinkBank.Free(pCur);
		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x00477de0
LTObject* sm_FindObject(CServerMgr *pServerMgr, uint16 objectID)
{
	ObjectMapEntry *pRecord;

	if (objectID < pServerMgr->m_ObjectMap.GetSize())
	{
		pRecord = &pServerMgr->m_ObjectMap[objectID];
		if (pRecord->m_nRecordType == RECORDTYPE_LTOBJECT)
			return (LTObject*)pRecord->m_pRecordData;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00477e10
ObjectMapEntry* sm_FindRecord(CServerMgr *pServerMgr, uint16 objectID)
{
	if (objectID < pServerMgr->m_ObjectMap.GetSize())
		return &pServerMgr->m_ObjectMap[objectID];

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00477e40
LTLink* sm_FindInFreeList(CServerMgr *pServerMgr, uint16 objectID)
{
	LTLink *pCur, *pListHead;

	pListHead = &pServerMgr->m_FreeIDs;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		if ((GetLinkID(pCur) & ~IDFLAG_MASK) == objectID)
			return pCur;
	}

	return LTNULL;
}

// Copies a special effect message (minus its packet ID) into the object.
// FUNCTION: LITHTECH 0x00477e80
void sm_SetObjectSpecialEffectMessage(CServerMgr *pServerMgr, LTObject *pObj, CPacket *pPacket)
{
	if (!pObj->sd->m_pSFXMsg)
		pObj->sd->m_pSFXMsg = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	pObj->sd->m_pSFXMsg->Init(pPacket->m_DataLen, MAX_PACKET_LEN);
	memcpy(pObj->sd->m_pSFXMsg->m_Data.GetArray(), pPacket->m_Data.GetArray() + 1, pPacket->m_DataLen - 1);
	pObj->sd->m_pSFXMsg->m_DataLen = pPacket->m_DataLen - 1;

	sm_UpdateInBspStatus(pServerMgr, pObj);
}

// FUNCTION: LITHTECH 0x00477fb0
void sm_SetObjectStateFlags(CServerMgr *pServerMgr, LTObject *pObj, uint32 flags)
{
	if (flags == (pObj->m_InternalFlags & IFLAG_INACTIVE_MASK))
		return;

	pObj->m_InternalFlags = (pObj->m_InternalFlags & ~IFLAG_INACTIVE_MASK) | flags;

	// Inactive objects live at the end of the list.
	dl_RemoveAt(&g_pServerMgr->m_Objects, &pObj->sd->m_ListNode);
	if (flags)
	{
		dl_AddTail(&g_pServerMgr->m_Objects, &pObj->sd->m_ListNode, pObj);
		pObj->sd->m_pObject->EngineMessageFn(MID_DEACTIVATING, LTNULL, 0.0f);
	}
	else
	{
		dl_AddHead(&g_pServerMgr->m_Objects, &pObj->sd->m_ListNode, pObj);
		pObj->sd->m_pObject->EngineMessageFn(MID_ACTIVATING, LTNULL, 0.0f);
	}
}

// FUNCTION: LITHTECH 0x00478070
void sm_ClearClientReferenceList(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext, *pListHead;

	pListHead = &pServerMgr->m_ClientReferences.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		dfree(pCur->m_pData);
		pCur = pNext;
	}

	dl_InitList(&pServerMgr->m_ClientReferences);
}

// FUNCTION: LITHTECH 0x004780c0
ClientRef* sm_FindClientRefFromObject(CServerMgr *pServerMgr, LTObject *pObj)
{
	LTLink *pCur, *pListHead;
	ClientRef *pRef;

	pListHead = &pServerMgr->m_ClientReferences.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pRef = (ClientRef*)pCur->m_pData;
		if (pRef->m_ObjectID == pObj->m_ObjectID)
			return pRef;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00478100
void sm_RemoveOldClientRefObjects(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext, *pListHead;
	LTObject *pObj;

	pListHead = &pServerMgr->m_Objects.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		pObj = (LTObject*)pCur->m_pData;

		if (pObj->m_InternalFlags & IFLAG_FROMCLIENTREF)
			sm_RemoveObjectFromWorld(pServerMgr, pObj->sd->m_pObject);

		pCur = pNext;
	}
}

// A model the loader thread unloaded.
struct ModelUnloadRequest
{
	uint32		m_Unknown0;		// 0x00
	Model		*m_pModel;		// 0x04
};

struct ModelUnloadMsg
{
	uint32				m_Unknown0;		// 0x00
	ModelUnloadRequest	*m_pRequest;	// 0x04
};

// Model unload callback: drops the model from the server's cache.
// FUNCTION: LITHTECH 0x00478150
LTRESULT sm_OnModelUnload(void *pUser, ModelUnloadMsg *pMsg, LTRESULT status)
{
	ModelUnloadRequest *pRequest;
	CServerMgr *pServerMgr;
	HHashElement *hElement;
	char *pFilename;

	if (status == LT_OK)
	{
		pRequest = pMsg->m_pRequest;
		pServerMgr = g_pServerMgr;
		if (pServerMgr)
		{
			pFilename = pRequest->m_pModel->GetFilename();
			delete pRequest;

			hElement = hs_FindElement(pServerMgr->m_hModelTable, pFilename, strlen(pFilename));
			if (hElement)
			{
				DEBUG_PRINT(3, ("Removing model from resource list:  %s\n", pFilename));
				hs_RemoveElement(pServerMgr->m_hModelTable, hElement);
			}
		}
	}

	return LT_OK;
}
