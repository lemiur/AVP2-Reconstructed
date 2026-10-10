// Jupiter runtime/client/src/client_filemgr.cpp
// Talon has no IClientFileMgr interface: the manager is a plain structure created by cf_Init and
// every cf_ function takes it (and tolerates NULL). The file transfer client is set up with
// callbacks, and the hash/format helpers are local to this file.
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "de_memory.h"
#include "de_file.h"
#include "client_filemgr.h"
#include "clientmgr.h"
#include "iclientshell.h"
#include "ftclient.h"
#include "console.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"
#include "../../build/proj/LT2/lithshared/stdlith/stringholder.h"


// ------------------------------------------------------------ //
// Constants
// ------------------------------------------------------------ //

#define NUM_CFM_SERVER_FILES    100
#define NUM_HASHED_IDENTIFIERS  200
#define MAX_FILETREES_TO_SEARCH	40

// ------------------------------------------------------------ //
// Structures.
// ------------------------------------------------------------ //

// 0x28 bytes.
struct ServerFile
{
	LTLink          m_Link;				// 0x00
	HLTFileTree     *m_hFileTree;		// 0x0c
	uint16          m_FileID;			// 0x10
	FileIdentifier  *m_pIdentifier;		// 0x14 The file identifier (if they've requested one).
	char            *m_RealFilename;	// 0x18 The server's filename.
	char            *m_ClientFilename;	// 0x1c What it is mapped to.
	uint16          m_NameLen;			// 0x20 strlen(m_ClientFilename)
	char            *m_Filename;		// 0x24
};


// 0x10 bytes.
struct ClientFileTree
{
	LTLink          m_Link;				// 0x00
	HLTFileTree     *m_hFileTree;		// 0x0c
};


// 0xe88 bytes.
struct ClientFileMgr
{
	// All the loaded file trees.
	LTLink      m_FileTrees;									// 0x000

	CStringHolder   m_Strings;									// 0x00c
	ObjectBank<FileIdentifier>  m_FileIdentifierBank;			// 0x024
	ObjectBank<ServerFile>      m_ServerFileBank;				// 0x048

	// The cache tree.
	HLTFileTree *m_hCacheTree;									// 0x06c

	// The server's files.  Wraps IDs around NUM_SERVER_FILES.
	LTLink      m_ServerFiles[NUM_CFM_SERVER_FILES];			// 0x070

	// File identifiers.. hashed on the filename.
	LTLink      m_FileIdentifiers[NUM_HASHED_IDENTIFIERS];		// 0x520

	// Our file transfer client.. the client filemgr builds its list
	// of server files from this and directs it where to transfer files to.
	FTClient    *m_hFTClient;									// 0xe80

	CClientMgr	*m_pClientMgr;									// 0xe84
};


static int cf_OnNewFile(FTClient *hClient, const char *pFilename, uint32 size, uint32 fileID);
static int cf_OnFileDone(FTClient *hClient);
static void cf_OnFn08();
static void cf_OnFn0C();
static void cf_OnFn10();
static ClientFileTree* cf_FindInFileTrees(ClientFileMgr *pMgr, const char *pFilename);
static ServerFile* cf_FindServerFile(ClientFileMgr *pMgr, uint16 fileID);


// ------------------------------------------------------------ //
// Internal helpers.
// ------------------------------------------------------------ //

// Jupiter: CHelpers::FormatFilename (without the NULL check).
// FUNCTION: LITHTECH 0x00403f10
void cf_FormatFilename(const char *pFilename, char *pOut, int outLen)
{
	strncpy(pOut, pFilename, outLen);
	pOut[outLen-1] = 0;
	_strupr(pOut);
	while(*pOut != 0)
	{
		if(*pOut == '/')
			*pOut = '\\';

		++pOut;
	}
}


// ------------------------------------------------------------ //
// Interface functions.
// ------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00403f50
ClientFileMgr* cf_Init(CClientMgr *pClientMgr)
{
	ClientFileMgr *pMgr;
	uint32 i;

	pMgr = new ClientFileMgr;

	pMgr->m_hCacheTree = LTNULL;
	pMgr->m_hFTClient = LTNULL;

	pMgr->m_Strings.SetAllocSize(4096);
	pMgr->m_FileIdentifierBank.Init(64, 1024);
	pMgr->m_ServerFileBank.Init(128, 1024);

	for (i = 0; i < NUM_CFM_SERVER_FILES; i++) {
		dl_TieOff(&pMgr->m_ServerFiles[i]);
	}

	dl_TieOff(&pMgr->m_FileTrees);
	pMgr->m_pClientMgr = pClientMgr;

	for (i = 0; i < NUM_HASHED_IDENTIFIERS; i++) {
		dl_TieOff(&pMgr->m_FileIdentifiers[i]);
	}

	return pMgr;
}


// FUNCTION: LITHTECH 0x00404060
void cf_Term(ClientFileMgr *pMgr)
{
	LTLink *pCur, *pNext;
	uint32 i;

	if (!pMgr)
		return;

	cf_OnDisconnect(pMgr);

	// Free the file trees.
	pCur = pMgr->m_FileTrees.m_pNext;
	while (pCur != &pMgr->m_FileTrees)
	{
		pNext = pCur->m_pNext;
		df_CloseTree(((ClientFileTree*)pCur->m_pData)->m_hFileTree);
		dfree(pCur->m_pData);
		pCur = pNext;
	}

	// Free the file identifiers.
	for (i=0; i < NUM_HASHED_IDENTIFIERS; i++)
	{
		pCur = pMgr->m_FileIdentifiers[i].m_pNext;
		while (pCur != &pMgr->m_FileIdentifiers[i])
		{
			pNext = pCur->m_pNext;
			pMgr->m_FileIdentifierBank.Free((FileIdentifier*)pCur->m_pData);
			pCur = pNext;
		}
	}

	delete pMgr;
}

// Compiler-generated, referenced from BaseObjectBank's vtable.
// FUNCTION: LITHTECH 0x00404120 ??_GBaseObjectBank@@UAEPAXI@Z


// FUNCTION: LITHTECH 0x00404140
void cf_ProcessPacket(ClientFileMgr *pMgr, const CPacket_Read &cPacket)
{
	if (pMgr)
		ftc_ProcessPacket(pMgr->m_hFTClient, cPacket);
}


// FUNCTION: LITHTECH 0x00404160
void cf_OnConnect(ClientFileMgr *pMgr, CBaseConn *serverID)
{
	uint32 i;
	FTCInitStruct initStruct;

	if (!pMgr)
		return;

	// Setup the server file list.
	for (i=0; i < NUM_CFM_SERVER_FILES; i++)
		dl_TieOff(&pMgr->m_ServerFiles[i]);

	// Setup the cache tree.
	df_OpenTree("c:\\de_cache", pMgr->m_hCacheTree);

	// Init the file transfer client.
	initStruct.m_pNetMgr = &pMgr->m_pClientMgr->m_NetMgr;
	initStruct.m_ConnID = serverID;
	initStruct.m_NewFile = cf_OnNewFile;
	initStruct.m_Fn04 = cf_OnFileDone;
	initStruct.m_Fn08 = cf_OnFn08;
	initStruct.m_Fn0C = cf_OnFn0C;
	initStruct.m_Fn10 = cf_OnFn10;

	pMgr->m_hFTClient = ftc_Init(&initStruct);
	ftc_SetUserData1(pMgr->m_hFTClient, pMgr);
}


// FUNCTION: LITHTECH 0x004041f0
static int cf_OnNewFile(FTClient *hClient, const char *pFilename, uint32 size, uint32 fileID)
{
	ClientFileMgr *pMgr;
	ServerFile *pFile;
	ClientFileTree *pTree;
	char formattedFilename[512];

	pMgr = (ClientFileMgr*)ftc_GetUserData1(hClient);

	// Right now, it just finds the file in the client's list of resources
	// and sets up a ServerFile right away.
	pTree = cf_FindInFileTrees(pMgr, pFilename);
	if (!pTree)
	{
		con_WhitePrintf("Unable to find server file: %s", pFilename);
		if (pMgr->m_pClientMgr->m_pClientShell)
			pMgr->m_pClientMgr->m_pClientShell->SetDisconnectCode(1, pFilename);
		return NF_HAVEFILE;
	}

	cf_FormatFilename(pFilename, formattedFilename, sizeof(formattedFilename));

	// Is there already a file with this ID?
	pFile = cf_FindServerFile(pMgr, (uint16)fileID);
	if (pFile)
	{
		return NF_HAVEFILE;
	}

	pFile = pMgr->m_ServerFileBank.Allocate();
	memset(pFile, 0, sizeof(ServerFile));
	pFile->m_Filename = pMgr->m_Strings.AddString(formattedFilename);
	pFile->m_ClientFilename = pFile->m_Filename;
	pFile->m_RealFilename = pFile->m_Filename;
	pFile->m_FileID = (uint16)fileID;
	pFile->m_hFileTree = pTree->m_hFileTree;
	pFile->m_NameLen = (uint16)strlen(formattedFilename);
	pFile->m_Link.m_pData = pFile;

	dl_Insert(&pMgr->m_ServerFiles[fileID % NUM_CFM_SERVER_FILES], &pFile->m_Link);

	return NF_HAVEFILE;
}


// FUNCTION: LITHTECH 0x00404320
static ClientFileTree* cf_FindInFileTrees(ClientFileMgr *pMgr, const char *pFilename)
{
	LTLink *pCur;
	ClientFileTree *pTree;
	LTFindInfo fileInfo;

	for (pCur=pMgr->m_FileTrees.m_pNext; pCur != &pMgr->m_FileTrees; pCur=pCur->m_pNext)
	{
		pTree = (ClientFileTree*)pCur->m_pData;

		if (df_GetFileInfo(pTree->m_hFileTree, pFilename, &fileInfo))
		{
			return pTree;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00404380
static ServerFile* cf_FindServerFile(ClientFileMgr *pMgr, uint16 fileID)
{
	uint16 hashed;
	ServerFile *pFile;
	LTLink *pCur, *pListHead;

	hashed = fileID % NUM_CFM_SERVER_FILES;

	pListHead = &pMgr->m_ServerFiles[hashed];
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pFile = (ServerFile*)pCur->m_pData;
		if (pFile->m_FileID == fileID)
			return pFile;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x004043d0
static int cf_OnFileDone(FTClient *hClient)
{
	return 0;
}

// Three empty callbacks; the linker folded them with other empty functions at 0x004359b0.
static void cf_OnFn08()
{
}

static void cf_OnFn0C()
{
}

static void cf_OnFn10()
{
}


// FUNCTION: LITHTECH 0x004043e0
void cf_OnDisconnect(ClientFileMgr *pMgr)
{
	uint32 i;
	LTLink *pCur, *pNext;
	ServerFile *pFile;

	if (!pMgr)
		return;

	if (pMgr->m_hCacheTree) {
		df_CloseTree(pMgr->m_hCacheTree);
		pMgr->m_hCacheTree = LTNULL;
	}

	// Free all the server files.
	for (i=0; i < NUM_CFM_SERVER_FILES; i++) {
		pCur = pMgr->m_ServerFiles[i].m_pNext;
		while (pCur != &pMgr->m_ServerFiles[i]) {
			pNext = pCur->m_pNext;

			pFile = (ServerFile*)pCur->m_pData;
			if (pFile->m_pIdentifier) {
				pMgr->m_FileIdentifierBank.Free(pFile->m_pIdentifier);
			}

			dl_Remove(pCur);
			pMgr->m_ServerFileBank.Free(pFile);

			pCur = pNext;
		}
	}

	// Free the file transfer client.
	if (pMgr->m_hFTClient) {
		ftc_Term(pMgr->m_hFTClient);
		pMgr->m_hFTClient = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x004044b0
void cf_AddResourceTrees(ClientFileMgr *pMgr, const char **pTreeNames, int nTrees,
	TreeType *pTypes, int *pnTreesLoaded)
{
	ClientFileTree *pTree;
	HLTFileTree *hTree;
	int i, nTreesLoaded;

	if (!pMgr)
		return;

	nTreesLoaded = 0;
	for (i=0; i < nTrees; i++) {
		df_OpenTree(pTreeNames[i], hTree);
		if (hTree) {
			pTree = (ClientFileTree*)dalloc(sizeof(ClientFileTree));
			pTree->m_hFileTree = hTree;
			pTree->m_Link.m_pData = pTree;
			dl_Insert(&pMgr->m_FileTrees, &pTree->m_Link);

			if (pTypes) {
				pTypes[nTreesLoaded] = df_GetTreeType(hTree);
			}

			++nTreesLoaded;
		}
	}

	if (pnTreesLoaded)
		*pnTreesLoaded = nTreesLoaded;
}


// FUNCTION: LITHTECH 0x00404540
const char* cf_GetFilename(ClientFileMgr *pMgr, FileRef *pFileRef)
{
	ServerFile *pServerFile;

	if (!pMgr)
		return "";

	if (pFileRef->m_FileType == FILE_SERVERFILE)
	{
		pServerFile = cf_FindServerFile(pMgr, pFileRef->m_FileID);
		if (pServerFile)
			return pServerFile->m_ClientFilename;
		else
			return "";
	}
	else
	{
		return pFileRef->m_pFilename;
	}
}


// FUNCTION: LITHTECH 0x00404580
FileEntry* cf_GetFileList(ClientFileMgr *pMgr, const char *pDirName)
{
	HLTFileTree *trees[MAX_FILETREES_TO_SEARCH];
	int nTrees;
	LTLink *pCur;

	if (!pMgr)
		return LTNULL;

	nTrees = 0;
	for (pCur=pMgr->m_FileTrees.m_pNext; pCur != &pMgr->m_FileTrees; pCur=pCur->m_pNext) {
		trees[nTrees++] = ((ClientFileTree*)pCur->m_pData)->m_hFileTree;
		if (nTrees >= MAX_FILETREES_TO_SEARCH)
			break;
	}

	return ic_GetFileList(trees, nTrees, pDirName);
}


// ------------------------------------------------------------------------
// GetFileIdentifier( file-ref, type-code )
//
// If the file is a server file, meaning that the server indicated to the
// client that it had loaded this file, look for the file by file id.
// If you find the server file, look for it in the cache tree,
//    if found return the file identifier associated with it.
//    else keep the filename from the file and go on to create a new file out it.
// Else if the file is not specified as a serverfile, create an identifier for it.
// ------------------------------------------------------------------------
// FUNCTION: LITHTECH 0x004045e0
FileIdentifier* cf_GetFileIdentifier(ClientFileMgr *pMgr, FileRef *pDesc, uint8 typeCode)
{
	ServerFile *pFile;
	uint16 strLen;
	uint32 hash, i;
	const char *pFilename;
	char *pFilenamePos;
	LTLink *pCur, *pListHead;
	FileIdentifier *pIdentifier, *pTest;
	char upperFilename[256];
	ClientFileTree *pTree;

	if (!pMgr)
		return LTNULL;

	// First see if a file identifier for it exists.
	if (pDesc->m_FileType == FILE_SERVERFILE) {
		// It's a server file.
		pFile = cf_FindServerFile(pMgr, pDesc->m_FileID);
		if (!pFile) return LTNULL;

		// If the file comes from the cache, then its FileIdentifier comes right
		// from the ServerFile.  Otherwise, just hash its name up like a normal file.
		if (pFile->m_hFileTree == pMgr->m_hCacheTree)
		{
			if (pFile->m_pIdentifier &&
				((typeCode == TYPECODE_UNKNOWN) ||
				(pFile->m_pIdentifier->m_TypeCode == TYPECODE_UNKNOWN) ||
				(pFile->m_pIdentifier->m_TypeCode == typeCode)))
			{
				return pFile->m_pIdentifier;
			}
			else {
				pFilename = pDesc->m_pFilename;
			}
		}
		else {
			pFilename = pFile->m_ClientFilename;
		}
	}
	else {
		pFilename = pDesc->m_pFilename;
	}

	if (!pFilename) return LTNULL;

	cf_FormatFilename(pFilename, upperFilename, sizeof(upperFilename));

	// Hash the name.
	strLen = 0;
	hash = 0;
	pFilenamePos = upperFilename;
	for (i=0; i < 32000; i++) {
		if (*pFilenamePos == 0) break;

		hash += (uint32)*pFilenamePos * i;
		++pFilenamePos;
		++strLen;
	}

	hash %= NUM_HASHED_IDENTIFIERS;

	// See if it's in the hashed list.
	pListHead = &pMgr->m_FileIdentifiers[hash];
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext) {
		pTest = (FileIdentifier*)pCur->m_pData;

		if (pTest->m_NameLen == strLen) {
			// Yep, return it..
			if (strcmp(pTest->m_Filename, upperFilename) == 0) {
				if ((pTest->m_TypeCode == typeCode) || (typeCode == TYPECODE_UNKNOWN) || (pTest->m_TypeCode == TYPECODE_UNKNOWN)) {
					return pTest;
				}
			}
		}
	}

	// Nope.. make sure the file exists.
	pTree = cf_FindInFileTrees(pMgr, upperFilename);
	if (!pTree) return LTNULL;

	// Ok, it exists.. setup a FileIdentifier for it.
	pIdentifier = pMgr->m_FileIdentifierBank.Allocate();
	memset(pIdentifier, 0, sizeof(FileIdentifier));
	pIdentifier->m_hFileTree = pTree->m_hFileTree;
	pIdentifier->m_FileID = (uint16)-1;
	pIdentifier->m_NameLen = strLen;
	pIdentifier->m_Link.m_pData = pIdentifier;
	pIdentifier->m_TypeCode = typeCode;
	pIdentifier->m_Filename = pMgr->m_Strings.AddString(upperFilename);
	dl_Insert(&pMgr->m_FileIdentifiers[hash], &pIdentifier->m_Link);

	return pIdentifier;
}


// FUNCTION: LITHTECH 0x004047f0
ILTStream* cf_OpenFileIdent(ClientFileMgr *pMgr, FileIdentifier *pFile)
{
	if (!pMgr)
		return LTNULL;

	return df_Open(pFile->m_hFileTree, pFile->m_Filename, 0);
}


// FUNCTION: LITHTECH 0x00404820
ILTStream* cf_OpenFile(ClientFileMgr *pMgr, FileRef *pDesc)
{
	ServerFile *pFile;
	ClientFileTree *pTree;
	LTLink *pCur;
	ILTStream *pStream;

	if (!pMgr)
		return LTNULL;

	if (pDesc->m_FileType == FILE_SERVERFILE) {
		// Just get a ServerFile from it.
		pFile = cf_FindServerFile(pMgr, pDesc->m_FileID);
		if (pFile) {
			return df_Open(pFile->m_hFileTree, pFile->m_ClientFilename, 0);
		}
		else {
			return LTNULL;
		}
	}
	else {
		// Look in the main file trees.
		for (pCur=pMgr->m_FileTrees.m_pNext; pCur != &pMgr->m_FileTrees; pCur=pCur->m_pNext) {
			pTree = (ClientFileTree*)pCur->m_pData;

			pStream = df_Open(pTree->m_hFileTree, pDesc->m_pFilename, 0);
			if (pStream) {
				return pStream;
			}
		}

		// Possibly check the cache tree.
		if (pDesc->m_FileType == FILE_ANYFILE) {
			pStream = df_Open(pMgr->m_hCacheTree, pDesc->m_pFilename, 0);
			if (pStream) {
				return pStream;
			}
		}
	}

	// Couldn't find it..
	return LTNULL;
}


// FUNCTION: LITHTECH 0x004048b0
LTRESULT cf_CopyFile(ClientFileMgr *pMgr, const char *pSrc, const char *pDest)
{
	LTLink *pCur;
	ClientFileTree *pTree;
	ILTStream *pStream;
	int status;

	if (!pMgr)
		return LT_ERROR;

	// Look in the main file trees.
	for (pCur=pMgr->m_FileTrees.m_pNext; pCur != &pMgr->m_FileTrees; pCur=pCur->m_pNext) {
		pTree = (ClientFileTree*)pCur->m_pData;

		pStream = df_Open(pTree->m_hFileTree, pSrc, 0);
		if (pStream) {
			status = df_Save(pStream, pDest);
			pStream->Release();

			return status ? LT_OK : LT_ERROR;
		}
	}

	return LT_NOTFOUND;
}


// ObjectBank template code instantiated here (the other virtuals of both banks were folded
// with identical copies elsewhere).
// FUNCTION: LITHTECH 0x00404920 ?Term@?$ObjectBank@UServerFile@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00404940 ??_G?$ObjectBank@UFileIdentifier@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00404980 ??_G?$ObjectBank@UServerFile@@VNullCS@@@@UAEPAXI@Z
