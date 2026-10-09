// Jupiter runtime/client/src/cutil.cpp
// Talon passes the client manager explicitly: most of these are cm_ functions rather than
// CClientMgr members (only the thiscall ones stay members). Texture tagging works on the
// SharedTexture ref-count word directly, and binding a texture is followed by a call that
// the linker folded into an empty function.
#include <windows.h>
#include "../../jupiter/dx9inc/ddraw.h"
#include <string.h>
#include <stdarg.h>
#include "bdefs.h"
#include "de_memory.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "iclientshell.h"
#include "dsys_interface.h"
#include "render.h"
#include "sprite.h"
#include "moveobject.h"
#include "servermgr.h"
#include "client_filemgr.h"
#include "model.h"
#include "animtracker.h"
#include "pixelformat.h"


#define TYPECODE_TEXTURE	3

// 0x0045d1e0
void RetransformWorldModel(WorldModelInstance *pWorldModel);

// Empty in this build (the linker folded it into 0x00473ac0); called after binding a texture.
void cm_OnTextureBound(CClientMgr *pClientMgr);

void r_UnbindTexture(SharedTexture *pTexture);		// 0x0046f660

LTRESULT cm_AddSharedTexture3(CClientMgr *pClientMgr, FileIdentifier *pIdent, SharedTexture* &pTexture);
LTRESULT cm_AddSharedTexture2(CClientMgr *pClientMgr, FileRef *pRef, SharedTexture* &pTexture);
void cm_FreeSharedTexture(CClientMgr *pClientMgr, SharedTexture *pTexture);
void cm_UntagAllTextures(CClientMgr *pClientMgr);
void cm_TagUsedTextures(CClientMgr *pClientMgr);
void cm_FreeUnusedSharedTextures(CClientMgr *pClientMgr);


// ------------------------------------------------------------------ //

// Registers a surface effect (ILTClient::AddSurfaceEffect).
// FUNCTION: LITHTECH 0x00425cd0
LTRESULT cm_AddSurfaceEffect(CClientMgr *pClientMgr, SurfaceEffectDesc *pDesc)
{
	SurfaceEffect *pEffect;

	pEffect = (SurfaceEffect*)dalloc(strlen(pDesc->m_pName) + sizeof(SurfaceEffect));
	if (pEffect)
	{
		pEffect->InitEffect = pDesc->InitEffect;
		pEffect->UpdateEffect = pDesc->UpdateEffect;
		pEffect->TermEffect = pDesc->TermEffect;
		strcpy(pEffect->m_Name, pDesc->m_pName);

		pEffect->m_pNext = (SurfaceEffect*)pClientMgr->m_Unknown12e8;
		pClientMgr->m_Unknown12e8 = (uint32)pEffect;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00425d30
LTRESULT CClientMgr::SetupError(LTRESULT theError, ...)
{
	va_list marker;
	LTRESULT dResult;

	va_start(marker, theError);
	dResult = dsi_SetupMessage(m_ErrorString, sizeof(m_ErrorString)-1, theError, marker);
	va_end(marker);

	return dResult;
}


// FUNCTION: LITHTECH 0x00425d60
LTRESULT cm_ProcessError(CClientMgr *pClientMgr, LTRESULT theError)
{
	if (theError & ERROR_DISCONNECT)
	{
		if (pClientMgr->m_pClientShell)
		{
			pClientMgr->m_pClientShell->OnEvent(LTEVENT_DISCONNECT, theError & ~(ERROR_DISCONNECT | ERROR_SHUTDOWN));
		}

		if (pClientMgr->m_pCurShell)
		{
			delete pClientMgr->m_pCurShell;
			pClientMgr->m_pCurShell = LTNULL;
		}

		dsi_DoErrorMessage(pClientMgr->m_ErrorString);

		return LT_OK;
	}
	else if (theError & ERROR_SHUTDOWN)
	{
		if (pClientMgr->m_pClientShell)
		{
			pClientMgr->m_pClientShell->OnEvent(LTEVENT_DISCONNECT, theError & ~(ERROR_DISCONNECT | ERROR_SHUTDOWN));
		}

		r_TermRender(pClientMgr, 2);

		dsi_OnClientShutdown(pClientMgr->m_ErrorString);

		return LT_ERROR;
	}
	else
	{
		return LT_OK;
	}
}


// FUNCTION: LITHTECH 0x00425e00
void CClientMgr::ForwardMessagesToScript()
{
	int i;

	if (m_pClientShell)
	{
		for (i=0; i < dsi_NumKeyDowns(); i++)
		{
			m_pClientShell->OnKeyDown(dsi_GetKeyDown(i), dsi_GetKeyDownRep(i));
		}

		for (i=0; i < dsi_NumKeyUps(); i++)
		{
			m_pClientShell->OnKeyUp(dsi_GetKeyUp(i));
		}

		dsi_ClearKeyDowns();
		dsi_ClearKeyUps();
	}
}


// FUNCTION: LITHTECH 0x00425ea0
void CClientMgr::ForwardCommandChanges(int32 *pChanges, int32 nChanges)
{
	int32 i;

	if (m_pClientShell)
	{
		for (i=0; i < nChanges; i++)
		{
			if (m_Commands[m_iCurInputSlot][pChanges[i]])
			{
				m_pClientShell->OnCommandOn(pChanges[i]);
			}
			else
			{
				m_pClientShell->OnCommandOff(pChanges[i]);
			}
		}
	}
}


// FUNCTION: LITHTECH 0x00425f00
void CClientMgr::UpdateFrameRate()
{
	m_FramerateTracker.Add(1.0f);
	m_FramerateTracker.Update(m_FrameTime);
}


// FUNCTION: LITHTECH 0x00425f30
LTRESULT cm_AddSharedTexture3(CClientMgr *pClientMgr, FileIdentifier *pIdent, SharedTexture* &pTexture)
{
	pTexture = LTNULL;
	LTBOOL bNew = LTTRUE;	// (declared after the first statement: the original keeps it in pTexture's argument slot)

	if (pIdent->m_pData)
	{
		pTexture = (SharedTexture*)pIdent->m_pData;
		bNew = LTFALSE;
	}
	else
	{
		pTexture = pClientMgr->m_SharedTextureBank.Allocate();
		memset(pTexture, 0, sizeof(*pTexture));
		dl_AddHead(&pClientMgr->m_SharedTextures, &pTexture->m_Link, pTexture);
		pTexture->m_pFile = pIdent;
		pIdent->m_pData = pTexture;
	}

	// Bind it to the renderer.
	if (!pTexture->m_pRenderData)
	{
		r_BindTexture(pTexture, LTFALSE);
		cm_OnTextureBound(pClientMgr);
	}

	// Let the game look at new textures.
	if (bNew)
	{
		pClientMgr->m_pClientShell->OnTextureLoad(&pTexture->m_pStateChange, pTexture->m_pCommandLine);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00426010
LTRESULT cm_AddSharedTexture2(CClientMgr *pClientMgr, FileRef *pRef, SharedTexture* &pTexture)
{
	FileIdentifier *pIdent;

	pTexture = LTNULL;

	pIdent = cf_GetFileIdentifier(pClientMgr->m_hFileMgr, pRef, TYPECODE_TEXTURE);
	if (pIdent)
	{
		return cm_AddSharedTexture3(pClientMgr, pIdent, pTexture);
	}
	else
	{
		return LT_MISSINGFILE;
	}
}


// FUNCTION: LITHTECH 0x00426050
SharedTexture* cm_AddSharedTexture(CClientMgr *pClientMgr, FileRef *pRef)
{
	SharedTexture *pSharedTexture;

	if (cm_AddSharedTexture2(pClientMgr, pRef, pSharedTexture) == LT_OK)
	{
		return pSharedTexture;
	}
	else
	{
		return LTNULL;
	}
}


// FUNCTION: LITHTECH 0x00426080
void cm_FreeSharedTexture(CClientMgr *pClientMgr, SharedTexture *pTexture)
{
	dl_RemoveAt(&pClientMgr->m_SharedTextures, &pTexture->m_Link);

	if (pTexture->m_pFile)
	{
		pTexture->m_pFile->m_pData = LTNULL;
	}

	r_UnbindTexture(pTexture);
	pClientMgr->m_SharedTextureBank.Free(pTexture);
}


// FUNCTION: LITHTECH 0x004260f0
void cm_FreeSharedTextures(CClientMgr *pClientMgr)
{
	LTLink *pCur, *pNext, *pListHead;

	pListHead = &pClientMgr->m_SharedTextures.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		cm_FreeSharedTexture(pClientMgr, (SharedTexture*)pCur->m_pData);
		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00426130
void TagTexture(SharedTexture *pTexture)
{
	if (pTexture)
	{
		pTexture->m_RefCount |= ST_TAGGED;
	}
}


// FUNCTION: LITHTECH 0x00426140
void _TagSpriteTextures(Sprite *pSprite)
{
	uint32 i, j;

	for (i=0; i < pSprite->m_nAnims; i++)
	{
		for (j=0; j < pSprite->m_Anims[i].m_nFrames; j++)
		{
			TagTexture(pSprite->m_Anims[i].m_Frames[j].m_pTex);
		}
	}
}


// FUNCTION: LITHTECH 0x004261a0
void cm_UntagAllTextures(CClientMgr *pClientMgr)
{
	LTLink *pListHead, *pCur;
	SharedTexture *pTexture;

	pListHead = &pClientMgr->m_SharedTextures.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pTexture = (SharedTexture*)pCur->m_pData;
		pTexture->SetFlags(0);
		pTexture->SetRefCount(0);
	}
}


void _TagWorldBspTextures(WorldBsp *bsp);

// FUNCTION: LITHTECH 0x004261d0
void cm_TagUsedTextures(CClientMgr *pClientMgr)
{
	LTLink *pCur, *pListHead;
	ModelInstance *pModelInst;
	LTParticleSystem *pSystem;
	uint32 i;
	SharedTexture *pTexture;

	// Tag textures that the game code has references to.
	pListHead = &pClientMgr->m_SharedTextures.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pTexture = ((SharedTexture*)pCur->m_pData);
		if (pTexture->GetRefCount() > 0)
		{
			TagTexture(pTexture);
		}
	}

	// Tag the textures in use by m_TextureUsers.
	pListHead = &pClientMgr->m_TextureUsers;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		TagTexture(((Model*)pCur->m_pData)->m_pFadeSpriteTex);
	}

	// Tag all linked textures
	pListHead = &pClientMgr->m_SharedTextures.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pTexture = (SharedTexture*)pCur->m_pData;
		TagTexture(pTexture->m_pLinkedTexture);
	}

	// Tag all textures we can't unload.
	if (pClientMgr->m_pCurShell)
	{
		for (i=0; i < pClientMgr->m_World.m_WorldModels.GetSize(); i++)
		{
			WorldData *pWorldData = pClientMgr->m_World.m_WorldModels[i];

			_TagWorldBspTextures(pWorldData->m_pOriginalBsp);
			if (pWorldData->m_pWorldBsp)
				_TagWorldBspTextures(pWorldData->m_pWorldBsp);
		}
	}

	// Go thru objects..
	pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pModelInst = (ModelInstance*)pCur->m_pData;
		for (i=0; i < MAX_MODEL_TEXTURES; i++)
		{
			TagTexture(pModelInst->m_pSkins[i]);
		}
	}

	pListHead = &pClientMgr->m_Sprites.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		_TagSpriteTextures((Sprite*)pCur->m_pData);
	}

	pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_PARTICLESYSTEM].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pSystem = (LTParticleSystem*)pCur->m_pData;
		TagTexture(pSystem->m_pCurTexture);
	}

	// Tag the textures the renderer holds.
	if (r_IsRenderInitted())
	{
		TagTexture(g_Render.m_pEnvMapTexture);
		for (i=0; i < 2; i++)
		{
			TagTexture(g_Render.m_GlobalPans[i].m_pTexture);
		}
	}
}


// FUNCTION: LITHTECH 0x00426350
void _TagWorldBspTextures(WorldBsp *bsp)
{
	uint32 i;

	for (i=0; i < bsp->m_nSurfaces; i++)
	{
		TagTexture(bsp->m_Surfaces[i].m_pTexture);
	}
}


// FUNCTION: LITHTECH 0x00426380
void cm_TagAndFreeTextures(CClientMgr *pClientMgr)
{
	uint32 i;

	// NOTE: It runs through here twice so detail textures get freed (first pass
	// frees their owner texture and the second pass frees the detail texture).
	for (i=0; i < 2; i++)
	{
		cm_UntagAllTextures(pClientMgr);
		cm_TagUsedTextures(pClientMgr);
		cm_FreeUnusedSharedTextures(pClientMgr);
	}
}


// FUNCTION: LITHTECH 0x004263b0
void cm_BindUnboundTextures(CClientMgr *pClientMgr)
{
	LTLink *pCur;
	SharedTexture *pTexture;

	for (pCur = pClientMgr->m_SharedTextures.m_Head.m_pNext; pCur != &pClientMgr->m_SharedTextures.m_Head; pCur=pCur->m_pNext)
	{
		pTexture = (SharedTexture*)pCur->m_pData;

		if (!pTexture->m_pRenderData)
		{
			r_BindTexture(pTexture, LTFALSE);
			cm_OnTextureBound(pClientMgr);
		}
	}
}


// Free the textures associated with a model if nobody else is using them
// Note : This assumes that the model textures aren't being used by
// anything other than models!
// FUNCTION: LITHTECH 0x004263f0
void cm_FreeUnusedModelTextures(CClientMgr *pClientMgr, LTObject *pObject)
{
	ModelInstance *pInstance = (ModelInstance*)pObject;

	LTLink *pCur, *pListHead;
	SharedTexture *pTexture;
	uint32 nTextureLoop;

	// Handle the case where there are duplicate entries in the skins..
	for (nTextureLoop = 0; nTextureLoop < MAX_MODEL_TEXTURES; ++nTextureLoop)
	{
		for (uint32 nDupeLoop = nTextureLoop + 1; nDupeLoop < MAX_MODEL_TEXTURES; ++nDupeLoop)
		{
			if ((pInstance->m_pSkins[nTextureLoop] == pInstance->m_pSkins[nDupeLoop]) && (pInstance->m_pSkins[nTextureLoop]))
			{
				pInstance->m_pSkins[nDupeLoop] = LTNULL;
			}
		}
	}

	// Untag the textures used by this model
	for (nTextureLoop = 0; nTextureLoop < MAX_MODEL_TEXTURES; ++nTextureLoop)
	{
		pTexture = pInstance->m_pSkins[nTextureLoop];
		if (!pTexture)
			continue;
		// Make sure sprites don't get unloaded
		if (pInstance->m_pSprites[nTextureLoop])
			pTexture->m_RefCount |= ST_TAGGED;
		else if (!(pTexture->m_RefCount & ST_REFCOUNTMASK))
			pTexture->m_RefCount = 0;
	}

	// Mark all client side model object textures as touched
	pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		ModelInstance *pFlagInstance = (ModelInstance*)pCur->m_pData;

		if (pFlagInstance == pInstance)
			continue;

		for (nTextureLoop = 0; nTextureLoop < MAX_MODEL_TEXTURES; ++nTextureLoop)
		{
			TagTexture(pFlagInstance->m_pSkins[nTextureLoop]);
		}
	}

	// Get rid of the model's textures if they haven't been tagged
	for (nTextureLoop = 0; nTextureLoop < MAX_MODEL_TEXTURES; ++nTextureLoop)
	{
		pTexture = pInstance->m_pSkins[nTextureLoop];
		if (!pTexture)
			continue;
		if ((pTexture->m_RefCount & ST_TAGGED) == 0)
		{
			cm_FreeSharedTexture(pClientMgr, pTexture);
			pInstance->m_pSkins[nTextureLoop] = LTNULL;
		}
	}
}


// FUNCTION: LITHTECH 0x004264e0
void cm_FreeUnusedSharedTextures(CClientMgr *pClientMgr)
{
	LTLink *pCur, *pNext, *pListHead;
	SharedTexture *pTexture;

	// Get rid of untagged ones.
	pListHead = &pClientMgr->m_SharedTextures.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;

		pTexture = (SharedTexture*)pCur->m_pData;

		if (!(pTexture->m_RefCount & ST_TAGGED))
		{
			cm_FreeSharedTexture(pClientMgr, pTexture);
		}

		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00426520
void cm_AddToObjectMap(CClientMgr *pClientMgr, uint16 id)
{
	ObjectMapEntry *newMap;
	uint32 newSize;

	// Make sure the object map is large enough.
	if (pClientMgr->m_ObjectMapSize <= id)
	{
		newSize = id + 100;
		newMap = (ObjectMapEntry*)dalloc(sizeof(ObjectMapEntry) * newSize);
		memset(newMap, 0, sizeof(ObjectMapEntry) * newSize);

		memcpy(newMap, pClientMgr->m_ObjectMap, sizeof(ObjectMapEntry) * pClientMgr->m_ObjectMapSize);
		dfree(pClientMgr->m_ObjectMap);
		pClientMgr->m_ObjectMap = newMap;
		pClientMgr->m_ObjectMapSize = newSize;
	}
}


// FUNCTION: LITHTECH 0x004265b0
void cm_ClearObjectMapEntry(CClientMgr *pClientMgr, uint16 id)
{
	if (id < pClientMgr->m_ObjectMapSize)
	{
		pClientMgr->m_ObjectMap[id].m_nRecordType = 0;
		pClientMgr->m_ObjectMap[id].m_pRecordData = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x004265e0
LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 id)
{
	if (id < pClientMgr->m_ObjectMapSize && pClientMgr->m_ObjectMap[id].m_nRecordType == RECORDTYPE_OBJECT)
		return (LTObject *)pClientMgr->m_ObjectMap[id].m_pRecordData;

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00426610
ObjectMapEntry* cm_FindRecord(CClientMgr *pClientMgr, uint16 id)
{
	if (id < pClientMgr->m_ObjectMapSize)
		return &pClientMgr->m_ObjectMap[id];

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00426640
void cm_ScaleObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewScale)
{
	LTBOOL bDifferent;

	bDifferent = !(pNewScale->x == pObject->m_Scale.x && pNewScale->y == pObject->m_Scale.y && pNewScale->z == pObject->m_Scale.z);

	pObject->m_Scale = *pNewScale;
	pClientMgr->m_World.m_WorldTree.InsertObject(pObject, 0);

	// Redo their dims if it's a model.
	if (bDifferent && pObject->m_ObjectType == OT_MODEL)
	{
		cm_UpdateModelDims(pClientMgr, (ModelInstance*)pObject);
	}
}


// FUNCTION: LITHTECH 0x004266d0
// Extern, not static: a static helper is emitted after its first caller (cm_MoveObject), the exe has it here.
void cm_WarnLargeDims(LTObject *pObject, float fRadius)
{
	if (g_DebugLevel >= 1)
	{
		if (pObject->m_ObjectType == OT_MODEL && ((ModelInstance*)pObject)->GetModelDB())
		{
			dsi_ConsolePrint("Model %s has suspiciously large internal dims (%f).",
				((ModelInstance*)pObject)->GetModelDB()->GetFilename(), fRadius);
		}
		else
		{
			dsi_ConsolePrint("An object (type %d) has suspiciously large internal dims (%f).",
				(char)pObject->m_ObjectType, fRadius);
		}
	}
}


// FUNCTION: LITHTECH 0x00426730
void cm_RelocateObject(CClientMgr *pClientMgr, LTObject *pObject)
{
	cm_MoveObject(pClientMgr, pObject, &pObject->m_Pos, LTTRUE);
}


// FUNCTION: LITHTECH 0x00426750
void cm_UpdateModelDims(CClientMgr *pClientMgr, ModelInstance *pInstance)
{
	MoveState moveState;
	LTAnimTracker *pTracker;
	AnimInfo *pAnim;
	LTVector theDims;

	// Don't do it if they don't want us to.
	if (pInstance->m_Unknown188 & CF_DONTSETDIMS)
		return;

	pTracker = &pInstance->m_AnimTracker;
	if (pTracker->IsValid() && pClientMgr->m_pCurShell)
	{
		pAnim = pTracker->GetModel()->GetAnimInfo(pTracker->m_TimeRef.m_Cur.m_iAnim);

		moveState.Setup(&pClientMgr->m_World.m_WorldTree, pClientMgr->m_MoveAbstract, pInstance, pInstance->m_BPriority);

		theDims = pAnim->m_vDims;
		theDims.x *= pInstance->m_Scale.x;
		theDims.y *= pInstance->m_Scale.y;
		theDims.z *= pInstance->m_Scale.z;

		ChangeObjectDimensions(&moveState, &theDims, LTFALSE, LTTRUE);
	}
}


// FUNCTION: LITHTECH 0x00426860
void cm_MoveObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTBOOL bForce)
{
	LTVector vDiff;

	if (!bForce)
	{
		vDiff = pObject->GetPos() - *pNewPos;
		if (vDiff.MagSqr() < 0.001f)
			return;
	}

	pObject->SetPos(*pNewPos);

	if (pObject->m_Radius > 3000.0f)
	{
		cm_WarnLargeDims(pObject, pObject->LTObject::GetRadius());
	}

	if (pClientMgr)
	{
		// Do special stuff if it's a WorldModel.
		if (pObject->HasWorldModel())
		{
			RetransformWorldModel((WorldModelInstance*)pObject);
		}

		pClientMgr->m_World.m_WorldTree.InsertObject(pObject, 0);
	}
}


// FUNCTION: LITHTECH 0x00426940
// One InsertObject after the if/else (VC6 duplicates it into both branches); writing it in each branch
// CSEs &m_WorldTree into a register.
void cm_RotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTRotation *pNewRot)
{
	MoveState theState;

	if (pClientMgr)
	{
		// Do special stuff if it's a WorldModel.
		if (pObject->HasWorldModel())
		{
			// Call RotateWorldModel to setup its dims and relocate it in the WorldTree
			// and stuff.
			theState.Setup(&pClientMgr->m_World.m_WorldTree, pClientMgr->m_MoveAbstract, pObject, pObject->m_BPriority);
			RotateWorldModel(&theState, pNewRot, LTFALSE);
		}
		else
		{
			pObject->m_Rotation = *pNewRot;
		}

		pClientMgr->m_World.m_WorldTree.InsertObject(pObject, 0);
	}
	else
	{
		pObject->m_Rotation = *pNewRot;
	}
}


// FUNCTION: LITHTECH 0x00426a40
void cm_MoveAndRotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTRotation *pNewRot)
{
	pObject->SetPos(*pNewPos);
	pObject->m_Rotation = *pNewRot;

	if (pClientMgr)
	{
		// Do special stuff for a WorldModel.
		if (pObject->HasWorldModel())
		{
			RetransformWorldModel((WorldModelInstance*)pObject);
		}

		pClientMgr->m_World.m_WorldTree.InsertObject(pObject, 0);
	}
}


// Fills a PFormat from a DirectDraw pixel format (smackvideomgrimpl).
// FUNCTION: LITHTECH 0x00426ac0
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat)
{
	BPPIdent type;

	if (pDDPF->dwRGBBitCount == 16)
		type = BPP_16;
	else
		type = BPP_32;

	pFormat->Init(type,
		pDDPF->dwRGBAlphaBitMask, pDDPF->dwRBitMask, pDDPF->dwGBitMask, pDDPF->dwBBitMask);
}
