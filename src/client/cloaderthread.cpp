// Jupiter runtime/client/src/cloaderthread.cpp
// Talon's loader thread still loads models (Jupiter deprecated it) and reaches its client
// manager through m_pClientMgr. Like sloaderthread.cpp on the server, the file starts with the
// client's ILTMessage helper.
#include "bdefs.h"
#include "cloaderthread.h"
#include "clientmgr.h"
#include "client_filemgr.h"
#include "stringmgr.h"
#include "engine_vars.h"

#define INVALID_OBJECTID	0xFFFF

class Model;


// 0x00489ca0 (Ghidra: CClientMgr::LoadModelData).
LTRESULT cm_LoadModelData(CClientMgr *pClientMgr, char *pFilename, FileIdentifier *pIdent, Model *&pModel);


CBindModuleType* sb_GetModule(ShellBindModule *pModule);		// 0x0048a7b0
// 0x004265e0
LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 objectID);


// ------------------------------------------------------------------ //
// CClientSerializeHelper
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004172d0
CClientSerializeHelper::CClientSerializeHelper(CClientMgr *pClientMgr)
{
	m_pClientMgr = pClientMgr;
}


// FUNCTION: LITHTECH 0x004172f0
LTRESULT CClientSerializeHelper::ReadObjectRef(ILTMessage *pMsg, HOBJECT *pObj)
{
	uint16 objectID;

	*pObj = LTNULL;
	pMsg->ReadWordFL(objectID);

	if(objectID != INVALID_OBJECTID)
	{
		*pObj = cm_FindObject(m_pClientMgr, objectID);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00417330
LTRESULT CClientSerializeHelper::WriteObjectRef(ILTMessage *pMsg, HOBJECT hObj)
{
	LTObject *pObj = (LTObject*)hObj;

	if(pObj)
		pMsg->WriteWord(pObj->m_ObjectID);
	else
		pMsg->WriteWord(INVALID_OBJECTID);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00417360
LTRESULT CClientSerializeHelper::WriteHStringArgList(ILTMessage *pMsg, int messageCode, va_list *pList)
{
	uint8 *pBuffer;
	int bufferLen;

	pBuffer = str_FormatString(sb_GetModule(m_pClientMgr->m_hShellModule),
		messageCode, pList, &bufferLen);
	if(pBuffer)
	{
		pMsg->WriteWord((uint16)bufferLen);
		pMsg->WriteRaw(pBuffer, bufferLen);
		str_FreeStringBuffer(pBuffer);
	}
	else
	{
		pMsg->WriteWord(0);
	}

	return LT_OK;
}


// Identical-code folded with another helper at 0x004c7650.
// FUNCTION: LITHTECH 0x004173d0
LTRESULT CClientSerializeHelper::GetWorld(MainWorld **ppWorld)
{
	*ppWorld = &m_pClientMgr->m_World;
	return LT_OK;
}


// ------------------------------------------------------------------ //
// CLoaderThread
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004173e0
CLoaderThread::CLoaderThread()
{
	m_pClientMgr = LTNULL;
}

// FUNCTION: LITHTECH 0x00417400 ??_GCLoaderThread@@UAEPAXI@Z


// FUNCTION: LITHTECH 0x00417420
LTBOOL CLoaderThread::IsLoadingFile(FileIdentifier *pIdent)
{
	CSAccess cs(&m_Incoming.m_MessageCS);
	CSAccess cs2(&m_Outgoing.m_MessageCS);
	GPOS pos;

	if (!m_pClientMgr)
		return LTFALSE;

	// Check all our messages.
	for (pos=m_Incoming.m_Messages; pos; )
	{
		if (m_Incoming.m_Messages.GetNext(pos)->m_Data[1].m_pData == pIdent)
			return LTTRUE;
	}

	for (pos=m_Outgoing.m_Messages; pos; )
	{
		if (m_Outgoing.m_Messages.GetNext(pos)->m_Data[1].m_pData == pIdent)
			return LTTRUE;
	}

	return LTFALSE;
}


// FUNCTION: LITHTECH 0x004174d0
void CLoaderThread::ProcessMessage(LThreadMessage &msg)
{
	if (!m_pClientMgr)
		return;

	if (msg.m_ID == CLT_LOADFILE)
	{
		switch (msg.m_Data[0].m_dwData)
		{
			case FT_MODEL:
			{
				LoadModel((FileIdentifier*)msg.m_Data[1].m_pData);
			}
			break;
		}
	}
}


// FUNCTION: LITHTECH 0x00417500
void CLoaderThread::LoadModel(FileIdentifier *pIdent)
{
	Model *pModel;
	LThreadMessage msg;
	LTRESULT dResult;

	if (!m_pClientMgr)
		return;

	if (g_CV_DebugLoaders)
	{
		dsi_ConsolePrint("CLoaderThread::LoadModel(%s)", pIdent->m_Filename);
	}

	dResult = cm_LoadModelData(m_pClientMgr, pIdent->m_Filename, pIdent, pModel);
	if (dResult == LT_OK)
	{
		msg.m_ID = CLT_LOADEDFILE;
	}
	else
	{
		msg.m_ID = CLT_LOADERROR;
	}

	msg.m_Data[0].m_dwData = FT_MODEL;
	msg.m_Data[1].m_pData = pIdent;
	msg.m_Data[2].m_pData = pModel;
	m_Outgoing.PostMessage(msg);
}
