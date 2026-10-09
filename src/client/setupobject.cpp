// Jupiter runtime/client/src/setupobject.cpp: client loading and setting up of objects.
// Talon passes the client manager explicitly and reaches the file manager through it.
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "client_filemgr.h"
#include "sprite.h"
#include "setupobject.h"
#include "de_objects.h"
#include "model.h"
#include "lthread.h"
#include "cloaderthread.h"
#include "nexus.h"
#include "animtracker.h"
#include "dutil.h"
#include "../../build/proj/LT2/lithshared/stdlith/helpers.h"

#define TYPECODE_MODEL		1
#define FT_MODEL			0

#define ToModel(pObj)	((ModelInstance*)(pObj))

#define TYPECODE_SPRITE		2


// FUNCTION: LITHTECH 0x00489710
LTRESULT LoadSprite(CClientMgr *pClientMgr, FileRef *pFilename, Sprite **ppSprite)
{
	Sprite *pSprite;
	ILTStream *pStream;
	FileIdentifier *pFileRef;
	const char *pFilenameString;

	*ppSprite = LTNULL;

	pFilenameString = cf_GetFilename(pClientMgr->m_hFileMgr, pFilename);

	// Get the sprite.
	pFileRef = cf_GetFileIdentifier(pClientMgr->m_hFileMgr, pFilename, TYPECODE_SPRITE);
	if (pFileRef)
	{
		if (pFileRef->m_pData)
		{
			pSprite = (Sprite*)pFileRef->m_pData;
		}
		else
		{
			// Create the sprite.
			pStream = cf_OpenFile(pClientMgr->m_hFileMgr, pFilename);
			if (!pStream)
			{
				pClientMgr->SetupError(LT_MISSINGSPRITEFILE, pFilenameString);
				RETURN_ERROR_PARAM(1, LoadSprite, LT_MISSINGSPRITEFILE, pFilenameString);
			}

			if (g_DebugLevel >= 2)
			{
				dsi_ConsolePrint("Loading sprite: %s\n", cf_GetFilename(pClientMgr->m_hFileMgr, pFilename));
			}

			pSprite = spr_Create(pClientMgr, pStream);
			if (!pSprite)
			{
				pClientMgr->SetupError(LT_INVALIDSPRITEFILE, pFilenameString);
				RETURN_ERROR_PARAM(1, LoadSprite, LT_INVALIDSPRITEFILE, pFilenameString);
			}

			// Successfully created...
			pFileRef->m_pData = pSprite;
			dl_AddHead(&pClientMgr->m_Sprites, &pSprite->m_Link, pSprite);

			pStream->Release();
		}
	}
	else
	{
		pClientMgr->SetupError(LT_MISSINGSPRITEFILE, pFilenameString);
		RETURN_ERROR_PARAM(1, LoadSprite, LT_MISSINGSPRITEFILE, pFilenameString);
	}

	*ppSprite = pSprite;
	return LT_OK;
}


LTRESULT cm_LoadModelData(CClientMgr *pClientMgr, char *pFilename, FileIdentifier *pIdent, Model *&pModel);	// 0x00489ca0

// Loads the model now, or has the loader thread load it.
// FUNCTION: LITHTECH 0x004898b0
LTRESULT cm_LoadModel(CClientMgr *pClientMgr, FileIdentifier *pIdent, Model **ppModel, LTBOOL bNow)
{
	LTRESULT dResult;

	if (g_DebugLevel >= 2)
	{
		dsi_ConsolePrint("(Client) loading model: %s", pIdent->m_Filename);
	}

	if (!bNow)
	{
		// Already loading it?
		if (pClientMgr->m_LoaderThread.IsLoadingFile(pIdent))
			return LT_INPROGRESS;

		LThreadMessage msg;

		msg.m_ID = CLT_LOADFILE;
		msg.m_Data[0].m_dwData = FT_MODEL;
		msg.m_Data[1].m_pData = pIdent;
		pClientMgr->m_LoaderThread.PostMessage(msg);
		return LT_INPROGRESS;
	}

	dResult = cm_LoadModelData(pClientMgr, pIdent->m_Filename, pIdent, *ppModel);
	if (dResult == LT_OK)
	{
		cm_BindModel(pClientMgr, *ppModel, pIdent, LTTRUE);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x00489970
LTRESULT cm_LoadModel2(CClientMgr *pClientMgr, FileRef *pRef, Model **ppModel, FileIdentifier **ppIdent,
	LTBOOL bLoad, LTBOOL bNow)
{
	FileIdentifier *pIdent;

	pIdent = cf_GetFileIdentifier(pClientMgr->m_hFileMgr, pRef, TYPECODE_MODEL);
	*ppIdent = pIdent;
	if (pIdent)
	{
		if (pIdent->m_pData)
		{
			*ppModel = (Model*)pIdent->m_pData;
			return LT_OK;
		}

		if (bLoad)
		{
			return cm_LoadModel(pClientMgr, pIdent, ppModel, bNow);
		}

		return LT_INPROGRESS;
	}

	RETURN_ERROR(1, LoadModel2, LT_MISSINGFILE);
}


Leech* nexus_CreateLeech(LeechDef *pDef, void *pUserData);	// 0x004448f0
LTRESULT nexus_AddLeech(Nexus *pNexus, Leech *pLeech);		// 0x00444970

// What ClientLoadChildModelCB gets (ModelLoadRequest::m_pLoadFnUserData).
struct ClientLoadChildInfo
{
	CClientMgr	*m_pClientMgr;		// 0x00
	char		*m_pFilename;		// 0x04 the parent model
};


// Loads a child model (the model's path is the parent's directory).
// FUNCTION: LITHTECH 0x00489a10
// The bind failure repeats the delete + LT_ERROR block that VC merges with the final one, and the final one is an
// else branch: with a statement after the if, VC cross-jumps the two LT_MISSINGMODELFILE tails the other way (README,
// wave 6).
LTRESULT ClientLoadChildModelCB(ModelLoadRequest *pRequest, Model **ppModel)
{
	ClientLoadChildInfo *pInfo;
	FileRef ref;
	FileIdentifier *pIdent;
	Model *pModel;
	LTRESULT dResult;
	char fullName[512];
	char path[512];

	*ppModel = LTNULL;
	pInfo = (ClientLoadChildInfo*)pRequest->m_pLoadFnUserData;

	// The child model is in the same directory as its parent.
	CHelpers::ExtractNames(pInfo->m_pFilename, path, LTNULL, LTNULL, LTNULL);
	if (path[0] == 0)
		sprintf(fullName, "%s", pRequest->m_pFilename);
	else
		sprintf(fullName, "%s\\%s", path, pRequest->m_pFilename);

	ref.m_FileType = FILE_CLIENTFILE;
	ref.m_pFilename = fullName;
	pIdent = cf_GetFileIdentifier(pInfo->m_pClientMgr->m_hFileMgr, &ref, TYPECODE_MODEL);
	if (!pIdent)
	{
		RETURN_ERROR_PARAM(1, cm_LoadChildModel, LT_MISSINGMODELFILE, fullName);
	}

	// Already loaded?
	if (pIdent->m_pData)
	{
		*ppModel = (Model*)pIdent->m_pData;
		return LT_OK;
	}

	pModel = new Model(&g_DefAlloc, &g_DefAlloc);
	if (!pModel)
	{
		RETURN_ERROR(1, cm_LoadChildModel, LT_OUTOFMEMORY);
	}

	pRequest->m_pFile = cf_OpenFileIdent(pInfo->m_pClientMgr->m_hFileMgr, pIdent);
	if (!pRequest->m_pFile)
	{
		delete pModel;
		RETURN_ERROR_PARAM(1, cm_LoadChildModel, LT_MISSINGMODELFILE, fullName);
	}

	dResult = pModel->InitAllocations(*pRequest->m_pFile, &g_DefAlloc);
	if (dResult == LT_OK)
		dResult = pModel->Load(pRequest);

	pRequest->m_pFile->Release();

	if (dResult == LT_OK)
	{
		pModel->SetFilename(fullName);
		if (cm_BindModel(pInfo->m_pClientMgr, pModel, pIdent, LTFALSE) != LT_OK)
		{
			delete pModel;
			RETURN_ERROR(1, cm_LoadChildModel, LT_ERROR);
		}

		*ppModel = pModel;
		return LT_OK;
	}
	else
	{
		delete pModel;
		RETURN_ERROR(1, cm_LoadChildModel, LT_ERROR);
	}
}


// FUNCTION: LITHTECH 0x00489ca0
LTRESULT cm_LoadModelData(CClientMgr *pClientMgr, char *pFilename, FileIdentifier *pIdent, Model *&pModel)
{
	ModelLoadRequest request;
	ClientLoadChildInfo childInfo;
	LTRESULT dResult;

	request.m_pFilename = LTNULL;
	request.m_pFile = LTNULL;
	request.m_LoadChildFn = DefaultLoadChildFn;
	request.m_pLoadFnUserData = LTNULL;
	request.m_bLoadChildModels = LTTRUE;
	request.m_Unknown18 = LTTRUE;
	request.m_bTreesValid = LTTRUE;
	request.m_bAllChildrenLoaded = LTTRUE;
	request.m_pExtraChildModels = LTNULL;

	pModel = new Model(&g_DefAlloc, &g_DefAlloc);
	if (!pModel)
	{
		RETURN_ERROR(1, cm_LoadModelData, LT_OUTOFMEMORY);
	}

	request.m_pFile = cf_OpenFileIdent(pClientMgr->m_hFileMgr, pIdent);
	if (!request.m_pFile)
	{
		if (pModel)
			delete pModel;

		pClientMgr->SetupError(LT_MISSINGMODELFILE, pFilename);
		RETURN_ERROR_PARAM(1, cm_LoadModelData, LT_MISSINGMODELFILE, pFilename);
	}

	dResult = pModel->InitAllocations(*request.m_pFile, &g_DefAlloc);
	if (dResult == LT_OK)
	{
		childInfo.m_pClientMgr = pClientMgr;
		childInfo.m_pFilename = pFilename;
		request.m_LoadChildFn = ClientLoadChildModelCB;
		request.m_pLoadFnUserData = &childInfo;
		dResult = pModel->Load(&request);
	}

	request.m_pFile->Release();

	if (dResult == LT_OK && pModel->SetFilename(pFilename))
	{
		// (The trees-valid flag Talon checks is the one at 0x18, model.h's m_Unknown18.)
		if (!request.m_Unknown18)
		{
			if (pModel)
				delete pModel;

			DEBUG_PRINT(1, ("Child model trees invalid in model %s", pFilename));
			RETURN_ERROR(1, cm_LoadModelData, LT_INVALIDMODELFILE);
		}
		else if (!request.m_bAllChildrenLoaded)
		{
			if (pModel)
				delete pModel;

			DEBUG_PRINT(1, ("Missing one or more child models in model %s", pFilename));
			RETURN_ERROR(1, cm_LoadModelData, LT_INVALIDMODELFILE);
		}

		nexus_AddLeech(&pModel->m_Nexus, nexus_CreateLeech(&g_ClientModelLeechDef, pIdent));
		return LT_OK;
	}

	if (pModel)
		delete pModel;

	DEBUG_PRINT(1, ("Error %d loading model %s", dResult, pFilename));
	return dResult;
}


// A model finished loading: binds it to its file identifier (or frees it if another copy won).
// FUNCTION: LITHTECH 0x00489f50
LTRESULT cm_BindModel(CClientMgr *pClientMgr, Model *pModel, FileIdentifier *pIdent, LTBOOL bUpdateObjects)
{
	ConParse parse;
	FileRef ref;
	LTLink *pCur;
	ModelInstance *pInst;

	if (pIdent->m_pData)
	{
		// Somebody else loaded it already.
		if (pModel)
			delete pModel;

		return LT_OK;
	}

	parse.Init(pModel->m_CommandString);
	if (parse.ParseFind("FadeSpriteTex", LTFALSE, 1))
	{
		ref.m_FileType = FILE_ANYFILE;
		ref.m_pFilename = parse.m_Args[1];
		pModel->m_pFadeSpriteTex = cm_AddSharedTexture(pClientMgr, &ref);
	}

	dl_Insert(&pClientMgr->m_TextureUsers, &pModel->m_Link);
	pIdent->m_pData = pModel;
	nexus_AddLeech(&pModel->m_Nexus, nexus_CreateLeech(&g_ClientModelLeechDef, pIdent));

	// Give it to the objects that were waiting for it.
	if (bUpdateObjects && (pIdent->m_Flags & 1))
	{
		for (pCur=pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head.m_pNext;
			pCur != &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head; pCur=pCur->m_pNext)
		{
			pInst = (ModelInstance*)pCur->m_pData;
			if (pInst->m_Unknown1B4 == (uint32)pIdent)
				pInst->m_AnimTracker.SetModel(pModel);
		}

		pIdent->m_Flags &= ~1;
	}

	return LT_OK;
}


// Does anything still reference the model (the client's leech on it)?
// FUNCTION: LITHTECH 0x0048a070
LTBOOL cm_IsModelReferenced(CClientMgr *pClientMgr, Model *pModel)
{
	return (&pModel->m_Nexus)->FindLeech(&g_ClientModelLeechDef) != LTNULL;
}


LTRESULT cm_RemoveObjectFromClientWorld(CClientMgr *pClientMgr, LTObject *pObject);	// 0x00412820

// Removes the client objects that use pModel.
// FUNCTION: LITHTECH 0x0048a090
void cm_RemoveModelObjects(CClientMgr *pClientMgr, Model *pModel, FileIdentifier *pIdent)
{
	LTLink *pCur, *pNext, *pListHead;
	LTObject *pObject;

	pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		pObject = (LTObject*)pCur->m_pData;

		if (((ModelInstance*)pObject)->GetModelDB() == pModel)
			cm_RemoveObjectFromClientWorld(pClientMgr, pObject);

		pCur = pNext;
	}

	pIdent->m_pData = LTNULL;

	if (!cm_IsModelReferenced(pClientMgr, pModel))
	{
		dl_Remove(&pModel->m_Link);
	}
}


LTRESULT cm_AddSharedTexture2(CClientMgr *pClientMgr, FileRef *pRef, SharedTexture *&pTexture);	// 0x00426010

LTRESULT ModelSetTexture(CClientMgr *pClientMgr, FileRef *pRef, ModelInstance *&pInstance, uint32 index);

// ------------------------------------------------------------------ //
// Static functions to implement the table functions.
// ------------------------------------------------------------------ //

// ModelSetTexture is defined after this function (as in the exe): when VC6 has already compiled it, it knows the
// callee never writes through its ModelInstance *& and caches pModelInstance in a register.
// FUNCTION: LITHTECH 0x0048a100
LTRESULT ModelExtraInit(CClientMgr *pClientMgr, LTObject *pObject,
	InternalObjectSetup *pSetup, LTBOOL bLocalFromServer)
{
	Model *pModel;
	FileIdentifier *pIdent;
	LTAnimTracker *pTracker;
	LTRESULT dResult;
	LTBOOL bLoad;
	uint32 i;
	ModelInstance *pModelInstance;

	pModelInstance = ToModel(pObject);

	bLoad = pSetup->m_pSetup->m_CreateFlags & OCS_AUTOLOAD;
	pModelInstance->m_Unknown2CC = 1;

	// Release the old model.
	if (pModelInstance->GetModelDB() && pModelInstance->GetModelDB()->m_RefCount > 0)
		pModelInstance->GetModelDB()->m_RefCount--;

	if (!bLocalFromServer)
	{
		dResult = cm_LoadModel2(pClientMgr, &pSetup->m_Filename, &pModel, &pIdent, bLoad, bLoad);
		if (dResult == LT_OK)
		{
			if (pSetup->m_bResetAnimations)
			{
				for (pTracker=pModelInstance->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
				{
					if (!pTracker->IsValid() || pTracker->GetModel() != pModel)
						trk_Init(pTracker, pModel, 0);
				}
			}
		}
		else if (dResult == LT_INPROGRESS)
		{
			// Use the default model until the loader thread has it.
			pModel = pClientMgr->m_pDefaultModel;
			if (pSetup->m_bResetAnimations)
			{
				for (pTracker=pModelInstance->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
				{
					if (!pTracker->IsValid() || pTracker->GetModel() != pModel)
					{
						trk_Init(pTracker, pModel, 0);
						pTracker->m_Flags |= AT_ALLOWINVALID;
					}
				}
			}

			pModelInstance->m_Unknown1B4 = (uint32)pIdent;
			pIdent->m_Flags |= 1;
		}
		else
		{
			return dResult;
		}
	}

	if (pModelInstance->GetModelDB())
		pModelInstance->GetModelDB()->m_RefCount++;

	for (i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		pModelInstance->m_pSkins[i] = LTNULL;
		ModelSetTexture(pClientMgr, &pSetup->m_SkinNames[i], pModelInstance, i);
	}

	return LT_OK;
}


// Sets a model skin from a .dtx or the current frame of a .spr.
// FUNCTION: LITHTECH 0x0048a2b0
LTRESULT ModelSetTexture(CClientMgr *pClientMgr, FileRef *pRef, ModelInstance *&pInstance, uint32 index)
{
	int len;
	char endChars[4];
	const char *pName;
	LTRESULT dResult;

	pInstance->m_pSprites[index] = LTNULL;

	pName = cf_GetFilename(pClientMgr->m_hFileMgr, pRef);
	len = strlen(pName);

	if (len > 3)
	{
		endChars[0] = pName[len-3];
		endChars[1] = pName[len-2];
		endChars[2] = pName[len-1];
		endChars[3] = 0;

		if (du_UpperStrcmp(endChars, "DTX"))
		{
			return cm_AddSharedTexture2(pClientMgr, pRef, pInstance->m_pSkins[index]);
		}
		else if (du_UpperStrcmp(endChars, "SPR"))
		{
			dResult = LoadSprite(pClientMgr, pRef, &pInstance->m_pSprites[index]);
			if (dResult != LT_OK)
				return dResult;

			spr_InitTracker((SpriteTracker*)pInstance->m_SpriteTrackers[index], pInstance->m_pSprites[index]);
			if (((SpriteTracker*)pInstance->m_SpriteTrackers[index])->m_pCurFrame)
			{
				pRef->m_pFilename = ((SpriteTracker*)pInstance->m_SpriteTrackers[index])->m_pCurFrame->m_pTex->m_pFile->m_Filename;
				return cm_AddSharedTexture2(pClientMgr, pRef, pInstance->m_pSkins[index]);
			}
		}
	}

	return LT_ERROR;
}


// Just decrement the reference count when terminating a model object.
// FUNCTION: LITHTECH 0x0048a3f0
static LTRESULT ModelExtraTerm(CClientMgr *pClientMgr, LTObject *pObject)
{
	Model *pModel;

	pModel = ((ModelInstance*)pObject)->GetModelDB();
	if (pModel && pModel->m_RefCount > 0)
		pModel->m_RefCount--;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048a420
static LTRESULT WorldModelExtraInit(CClientMgr *pClientMgr, LTObject *pObject,
	InternalObjectSetup *pSetup, LTBOOL bLocalFromServer)
{
	WorldModelInstance *pInst;
	WorldBsp *pWorldBsp;

	if (bLocalFromServer)
		return LT_OK;

	if (!pClientMgr)
	{
		pClientMgr->SetupError(LT_MISSINGWORLDMODEL, pSetup->m_pSetup->m_Filename);
		RETURN_ERROR_PARAM(1, WorldModelExtraInit, LT_MISSINGWORLDMODEL, pSetup->m_pSetup->m_Filename);
	}

	pInst = (WorldModelInstance*)pObject;
	if (pClientMgr->m_World.InitWorldModel(pInst, pSetup->m_pSetup->m_Filename))
	{
		if (!pInst->m_pOriginalBsp->IsUntransformed())
		{
			pWorldBsp = (WorldBsp*)pInst->m_pOriginalBsp;
			pInst->SetDims((pWorldBsp->m_MaxBox - pWorldBsp->m_MinBox) * 0.5f);
		}
	}
	else
	{
		RETURN_ERROR_PARAM(1, WorldModelExtraInit, LT_MISSINGWORLDMODEL, pSetup->m_pSetup->m_Filename);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048a580
static LTRESULT SpriteExtraInit(CClientMgr *pClientMgr, LTObject *pObject,
	InternalObjectSetup *pSetup, LTBOOL bLocalFromServer)
{
	Sprite *pSprite;
	LTRESULT dResult;

	dResult = LoadSprite(pClientMgr, &pSetup->m_Filename, &pSprite);
	if (dResult != LT_OK)
		return dResult;

	// Create the sprite instance.
	spr_InitTracker((SpriteTracker*)&((SpriteInstance*)pObject)->m_SpriteTracker, pSprite);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048a5c0
static LTRESULT ContainerExtraInit(CClientMgr *pClientMgr, LTObject *pObject,
	InternalObjectSetup *pSetup, LTBOOL bLocalFromServer)
{
	LTRESULT dResult;

	if (bLocalFromServer)
		return LT_OK;

	dResult = WorldModelExtraInit(pClientMgr, pObject, pSetup, bLocalFromServer);
	if (dResult != LT_OK)
		return dResult;

	((ContainerInstance*)pObject)->m_ContainerCode = pSetup->m_pSetup->m_ContainerCode;
	return LT_OK;
}


// ------------------------------------------------------------------ //
// Interface functions.
// ------------------------------------------------------------------ //

// (Talon's LTObject::m_ObjectType is a char: the table index is sign extended.)
struct ExtraInitStruct
{
	LTRESULT	(*Init)(CClientMgr *pClientMgr, LTObject *pObject,
		InternalObjectSetup *pSetup, LTBOOL bLocalFromServer);
	LTRESULT	(*Term)(CClientMgr *pClientMgr, LTObject *pObject);
};


// GLOBAL: LITHTECH 0x004d6bf8
ExtraInitStruct g_ExtraInitStructs[NUM_OBJECTTYPES] =
{
	LTNULL, LTNULL,							// OT_NORMAL
	ModelExtraInit, ModelExtraTerm,			// OT_MODEL
	WorldModelExtraInit, LTNULL,			// OT_WORLDMODEL
	SpriteExtraInit, LTNULL,				// OT_SPRITE
	LTNULL, LTNULL,							// OT_LIGHT
	LTNULL, LTNULL,							// OT_CAMERA
	LTNULL, LTNULL,							// OT_PARTICLESYSTEM
	LTNULL, LTNULL,							// OT_POLYGRID
	LTNULL, LTNULL,							// OT_LINESYSTEM
	ContainerExtraInit, LTNULL,				// OT_CONTAINER
	LTNULL, LTNULL							// OT_CANVAS
};


// FUNCTION: LITHTECH 0x0048a600
LTRESULT so_ExtraInit(CClientMgr *pClientMgr, LTObject *pObject, InternalObjectSetup *pSetup,
	LTBOOL bFromLocalServer)
{
	if (g_ExtraInitStructs[(char)pObject->m_ObjectType].Init)
		return g_ExtraInitStructs[(char)pObject->m_ObjectType].Init(pClientMgr, pObject, pSetup, bFromLocalServer);
	else
		return LT_OK;
}


// FUNCTION: LITHTECH 0x0048a620
LTRESULT so_ExtraTerm(CClientMgr *pClientMgr, LTObject *pObject)
{
	if (g_ExtraInitStructs[(char)pObject->m_ObjectType].Term)
		return g_ExtraInitStructs[(char)pObject->m_ObjectType].Term(pClientMgr, pObject);
	else
		return LT_OK;
}
