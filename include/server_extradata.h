// Jupiter runtime/server/src/server_extradata.h: per-type object data (models, sprites,
// world models) and Talon's server model cache.
#ifndef __SERVER_EXTRADATA_H__
#define __SERVER_EXTRADATA_H__

#include "ltbasedefs.h"
#include "ltanimtracker.h"

class CServerMgr;
class LTObject;
struct ObjectCreateStruct;
class Model;
struct UsedFile;

LTRESULT	se_LoadModelData(CServerMgr *pServerMgr, const char *pFilename, UsedFile *pFile, Model **ppModel);	// 0x004784c0 (loads from the file, not the cache)
// 0x00478780: adds a loaded model to the cache. Returns TRUE if that failed.
LTBOOL		se_LoadChildModels(CServerMgr *pServerMgr, Model *pModel, UsedFile *pFile, uint32 flags);
LTRESULT	se_GetModel(CServerMgr *pServerMgr, char *pFilename, Model **ppModel, UsedFile **ppFile,
				LTBOOL bLoad, LTBOOL bNow);			// 0x004788c0
LTRESULT	se_UncacheModel(CServerMgr *pServerMgr, const char *pFilename, UsedFile *pFile);	// 0x00478a20

// Per-type object data: loads an object's files (sm_InitExtraData), releases them again
// (sm_TermExtraData) and keeps/restores them around a change (BackupExtraData/RestoreExtraData).
LTRESULT	sm_InitExtraData(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct);	// 0x00478f80
LTRESULT	sm_TermExtraData(CServerMgr *pServerMgr, LTObject *pObject);		// 0x00478fa0

// 0x58 bytes.
class ExtraDataBackup
{
public:
	LTAnimTracker	m_AnimTracker;	// 0x00
	UsedFile		*m_pFile;		// 0x50
	UsedFile		*m_pSkin;		// 0x54
};

LTRESULT	BackupExtraData(LTObject *pObject, ExtraDataBackup *pBackup);	// 0x00478fc0
LTRESULT	RestoreExtraData(LTObject *pObject, ExtraDataBackup *pBackup);	// 0x00479040

// The leech the server puts on the models it loads: sm_OnModelUnload drops them from the cache.
// GLOBAL: LITHTECH 0x004d5a30
extern LeechDef g_ServerModelLeechDef;

#endif  // __SERVER_EXTRADATA_H__
