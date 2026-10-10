// Jupiter runtime/server/src/server_filemgr.cpp
// Talon's file manager is a struct in CServerMgr driven by sf_ functions, and its resource
// trees are kept in a plain LTLink list.
#include <windows.h>		// before the StdLith headers (servermgr.h brings in lthread.h)
#include <string.h>
#include "bdefs.h"
#include "server_filemgr.h"
#include "servermgr.h"
#include "dhashtable.h"
#include "de_memory.h"

int fts_AddFile(FTServ *hServ, char *pFilename, uint32 fileSize, uint32 fileID, uint16 flags);


// FUNCTION: LITHTECH 0x004790c0
void sf_Init(ServerFileMgr *pMgr, CServerMgr *pServerMgr)
{
	memset(pMgr, 0, sizeof(ServerFileMgr));

	pMgr->m_hFileTable = hs_CreateHashTable(500, HASH_FILENAME);
	dl_TieOff(&pMgr->m_ResTrees);
	pMgr->m_pServerMgr = pServerMgr;
	pMgr->m_UsedFileBank.Init(64, 512);
}

// FUNCTION: LITHTECH 0x00479110
void sf_Term(ServerFileMgr *pMgr)
{
	LTLink *pCur, *pNext;
	ResTree *pTree;

	sf_ClearUsedFiles(pMgr);

	if (pMgr->m_hFileTable)
	{
		hs_DestroyHashTable(pMgr->m_hFileTable);
		pMgr->m_hFileTable = LTNULL;
	}

	pCur = pMgr->m_ResTrees.m_pNext;
	while (pCur != &pMgr->m_ResTrees)
	{
		pNext = pCur->m_pNext;
		pTree = (ResTree*)pCur->m_pData;
		df_CloseTree(pTree->m_hFileTree);
		dfree(pTree);
		pCur = pNext;
	}
	dl_TieOff(&pMgr->m_ResTrees);
}

// FUNCTION: LITHTECH 0x00479170
void sf_AddResources(ServerFileMgr *pMgr, const char **pTrees, int nTrees,
	TreeType *pTreeTypes, int *pnTreesLoaded)
{
	int i, nTreesLoaded;
	HLTFileTree *hTree;
	ResTree *pTree;

	nTreesLoaded = 0;
	for (i=0; i < nTrees; i++)
	{
		if (df_OpenTree(pTrees[i], hTree) != 0)
			continue;

		pTree = (ResTree*)dalloc(sizeof(ResTree));
		pTree->m_Link.m_pData = pTree;
		pTree->m_hFileTree = hTree;
		dl_Insert(&pMgr->m_ResTrees, &pTree->m_Link);

		if (pTreeTypes)
			pTreeTypes[nTreesLoaded] = df_GetTreeType(hTree);

		++nTreesLoaded;
	}

	*pnTreesLoaded = nTreesLoaded;
}

// FUNCTION: LITHTECH 0x00479200
ILTStream* sf_OpenFile(ServerFileMgr *pMgr, const char *pFilename)
{
	return sf_OpenFile2(pMgr, pFilename, 0, 0);
}

// FUNCTION: LITHTECH 0x00479220
ILTStream* sf_OpenFile2(ServerFileMgr *pMgr, const char *pFilename, int bAddUsedFile, short flags)
{
	HHashElement *hElement;
	UsedFile *pUsedFile;
	LTLink *pCur;
	HLTFileTree *hTree;
	ILTStream *pRet;

	hElement = hs_FindElement(pMgr->m_hFileTable, pFilename, strlen(pFilename)+1);
	if (hElement)
	{
		pUsedFile = (UsedFile*)hs_GetElementUserData(hElement);
		if (pUsedFile)
			return df_Open(pUsedFile->m_pTree->m_hFileTree, pFilename, 0);
	}

	for (pCur=pMgr->m_ResTrees.m_pNext; pCur != &pMgr->m_ResTrees; pCur=pCur->m_pNext)
	{
		hTree = ((ResTree*)pCur->m_pData)->m_hFileTree;
		pRet = df_Open(hTree, pFilename, 0);
		if (pRet)
		{
			if (bAddUsedFile)
				sf_AddUsedFile(pMgr, pFilename, flags, LTNULL);

			return pRet;
		}
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x004792c0
ILTStream* sf_OpenFile3(ServerFileMgr *pMgr, UsedFile *pUsedFile)
{
	char *pFilename;

	pFilename = sf_GetUsedFilename(pMgr, pUsedFile);
	return df_Open(pUsedFile->m_pTree->m_hFileTree, pFilename, 0);
}

// FUNCTION: LITHTECH 0x004792f0
LTRESULT sf_CopyFile(ServerFileMgr *pMgr, const char *pSrc, const char *pDest)
{
	LTLink *pCur;
	HLTFileTree *hTree;
	ILTStream *pStream;
	int status;

	for (pCur=pMgr->m_ResTrees.m_pNext; pCur != &pMgr->m_ResTrees; pCur=pCur->m_pNext)
	{
		hTree = ((ResTree*)pCur->m_pData)->m_hFileTree;
		pStream = df_Open(hTree, pSrc, 0);
		if (!pStream)
			continue;

		status = df_Save(pStream, pDest);
		pStream->Release();
		return status ? LT_OK : LT_ERROR;
	}

	return LT_NOTFOUND;
}

// FUNCTION: LITHTECH 0x00479350
LTBOOL sf_DoesFileExist(ServerFileMgr *pMgr, const char *pFilename, ResTree **ppTree, uint32 *pFileSize)
{
	LTLink *pCur;
	ResTree *pTree;
	LTFindInfo info;

	for (pCur=pMgr->m_ResTrees.m_pNext; pCur != &pMgr->m_ResTrees; pCur=pCur->m_pNext)
	{
		pTree = (ResTree*)pCur->m_pData;
		if (!df_GetFileInfo(pTree->m_hFileTree, pFilename, &info))
			continue;

		if (ppTree)
			*ppTree = pTree;

		if (pFileSize)
			*pFileSize = info.m_Size;

		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x004793d0
FileEntry* sf_GetFileList(ServerFileMgr *pMgr, const char *pDirName)
{
	HLTFileTree *trees[MAX_FILETREES_TO_SEARCH];
	int nTrees;
	LTLink *pCur;

	if (!pMgr)
		return LTNULL;

	nTrees = 0;
	for (pCur=pMgr->m_ResTrees.m_pNext; pCur != &pMgr->m_ResTrees; pCur=pCur->m_pNext)
	{
		trees[nTrees] = ((ResTree*)pCur->m_pData)->m_hFileTree;
		nTrees++;
		if (nTrees >= MAX_FILETREES_TO_SEARCH)
			break;
	}

	return ic_GetFileList(trees, nTrees, pDirName);
}

// FUNCTION: LITHTECH 0x00479420
int sf_AddUsedFile(ServerFileMgr *pMgr, const char *pFilename, short flags, UsedFile **ppFile)
{
	UsedFile *pFile;
	HHashElement *hElement;
	uint32 strLen, fileSize;
	ResTree *pTree;
	LTLink *pCur, *pListHead;
	Client *pClient;

	if (ppFile)
		*ppFile = LTNULL;

	strLen = strlen(pFilename);
	hElement = hs_FindElement(pMgr->m_hFileTable, pFilename, strLen+1);
	if (hElement)
	{
		pFile = (UsedFile*)hs_GetElementUserData(hElement);
		if (ppFile)
			*ppFile = pFile;
		return 1;
	}
	else
	{
		if (!sf_DoesFileExist(pMgr, pFilename, &pTree, &fileSize))
			return 0;

		pFile = pMgr->m_UsedFileBank.Allocate();
		hElement = hs_AddElement(pMgr->m_hFileTable, pFilename, strlen(pFilename)+1);
		pFile->m_Flags = flags;
		pFile->m_pTree = pTree;
		pFile->m_FileSize = fileSize;
		pFile->m_FileID = pMgr->m_CurrentFileID++;
		hs_SetElementUserData(hElement, pFile);
		pFile->m_hElement = hElement;

		if (ppFile)
			*ppFile = pFile;

		// Tell the clients about it.
		pListHead = &pMgr->m_pServerMgr->m_Clients.m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pClient = (Client*)pCur->m_pData;
			fts_AddFile(pClient->m_hFTServ, pFile->GetFilename(),
				pFile->m_FileSize, pFile->m_FileID, pFile->m_Flags);
		}

		return 2;
	}
}

// FUNCTION: LITHTECH 0x00479550
void sf_ClearUsedFiles(ServerFileMgr *pMgr)
{
	HHashIterator *hIterator;
	HHashElement *hElement;
	UsedFile *pFile;

	hIterator = hs_GetFirstElement(pMgr->m_hFileTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		pFile = (UsedFile*)hs_GetElementUserData(hElement);
		pMgr->m_UsedFileBank.Free(pFile);
		hs_RemoveElement(pMgr->m_hFileTable, hElement);
	}

	pMgr->m_CurrentFileID = 0;
}

// FUNCTION: LITHTECH 0x004795c0
char* sf_GetUsedFilename(ServerFileMgr *pMgr, UsedFile *pFile)
{
	if (pFile)
		return (char*)hs_GetElementKey(pFile->m_hElement, LTNULL);
	else
		return "";
}
