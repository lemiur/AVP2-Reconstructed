// Jupiter runtime/kernel/io/src/sys/win/de_file.cpp
// The unit starts at 00426af0 with the stream banks' static initializers and ends at 00427cf0
// (the world header readers after it are not file code). Talon's streams count their reads,
// DOS paths use '/' when the name has one, and FileTree always has the critical section.
// FLAGS: /O2 /IE:/AVP2Source/build/proj/LT2/lithshared/lith
#include <windows.h>
#include <string.h>
#include <io.h>
#include <stdio.h>
#include "bdefs.h"
#include "de_file.h"
#include "de_memory.h"
#include "genltstream.h"
#include "counter.h"
#include "lthread.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"
#include "../../build/proj/LT2/lithshared/rezmgr/rezmgr.h"
#include "engine_vars.h"




// PlayDemo profile info.
// GLOBAL: LITHTECH 0x004e3450
uint32 g_PD_FOpen=0;


// ------------------------------------------------------------------ //
// Structures and defines.
// ------------------------------------------------------------------ //

// GLOBAL: LITHTECH 0x004e344c
CRezItm* g_pDeFileLastRezItm;
// GLOBAL: LITHTECH 0x004e33d8
uint32 g_nDeFileLastRezPos;


struct FileTree
{
	TreeType	m_TreeType;			// 0x00
	CRezMgr*	m_pRezMgr;			// 0x04

	CRITICAL_SECTION m_CriticalSection;	// 0x08

	// Directory name if this is a DosTree (like e:/dedit/riot)
	// Rezfile name if this is a RezFileTree (like e:/dedit/riot/riot.rez)
	char		m_BaseName[1];		// 0x20
};


class BaseFileStream : public CGenLTStream
{
public:

			BaseFileStream()
			{
				m_FileLen = 0;
				m_SeekOffset = 0;
				m_pFile = LTNULL;
				m_pTree = LTNULL;
				m_ErrorStatus = 0;
				m_pRezItm = LTNULL;
				m_nReads = 0;
				m_nBytesRead = 0;
			}

	virtual	~BaseFileStream()
	{
		if (g_CV_ShowFileAccess >= 2)
		{
			dsi_ConsolePrint("close stream %p num reads = %u bytes read = %u", this, m_nReads, m_nBytesRead);
		}

		if(m_pTree->m_TreeType == DosTree && m_pFile)
		{
			fclose(m_pFile);
		}
	}

	LTRESULT ErrorStatus()
	{
		return m_ErrorStatus ? LT_ERROR : LT_OK;
	}

	LTRESULT GetLen(uint32 *len)
	{
		*len = m_FileLen;
		return LT_OK;
	}

	LTRESULT Write(const void *pData, uint32 dataLen)
	{
		// Can't write to these streams.
		return LT_ERROR;
	}

	uint32		m_FileLen;		// 0x04 Stored when the file is opened.
	uint32		m_SeekOffset;	// 0x08 Rez files: position inside the resource. DOS files: start offset.
	FILE		*m_pFile;		// 0x0c
	FileTree	*m_pTree;		// 0x10
	int			m_ErrorStatus;	// 0x14
	CRezItm*	m_pRezItm;		// 0x18
	uint32		m_nReads;		// 0x1c
	uint32		m_nBytesRead;	// 0x20
};


class DosFileStream : public BaseFileStream
{
public:

	void	Release();

	LTRESULT GetPos(uint32 *pos)
	{
		*pos = (uint32)(ftell(m_pFile)) - m_SeekOffset;
		return LT_OK;
	}

	LTRESULT	SeekTo(uint32 offset)
	{
		if(fseek(m_pFile, m_SeekOffset + offset, SEEK_SET) == 0)
		{
			return LT_OK;
		}
		else
		{
			m_ErrorStatus = 1;
			return LT_ERROR;
		}
	}

	LTRESULT Read(void *pData, uint32 size)
	{
		if (g_CV_ShowFileAccess >= 4)
		{
			dsi_ConsolePrint("read %u bytes from stream %p", size, this);
		}

		if(m_ErrorStatus == 1)
		{
			memset(pData, 0, size);
			return LT_ERROR;
		}

		if(size != 0)
		{
			size_t sizeRead = fread(pData, 1, size, m_pFile);
			if(sizeRead == size)
			{
				m_nReads++;
				m_nBytesRead += sizeRead;
			}
			else
			{
				//encountered an error while reading
				memset(pData, 0, size);
				m_ErrorStatus = 1;
				return LT_ERROR;
			}
		}

		return LT_OK;
	}
};

class RezFileStream : public BaseFileStream
{
public:

	void		Release();

	LTRESULT	GetPos(uint32 *pos)
	{
		*pos = m_SeekOffset;
		return LT_OK;
	}

	LTRESULT	SeekTo(uint32 offset)
	{
		if(m_pRezItm->Seek(offset))
		{
			m_SeekOffset = offset;
			return LT_OK;
		}
		else
		{
			m_ErrorStatus = 1;
			return LT_ERROR;
		}
	}

	LTRESULT Read(void *pData, uint32 size)
	{
		size_t sizeRead;

		if (g_CV_ShowFileAccess >= 4)
		{
			dsi_ConsolePrint("read from stream %p size %u", this);
		}

		if(size != 0)
		{
			EnterCriticalSection(&m_pTree->m_CriticalSection);

			uint32 seekOffset;
			if (g_pDeFileLastRezItm == m_pRezItm)
			{
				seekOffset = m_SeekOffset;
				if (g_nDeFileLastRezPos == seekOffset)
					sizeRead = m_pRezItm->Read((BYTE*)pData, size);
				else
					sizeRead = m_pRezItm->Read((BYTE*)pData, size, seekOffset);
			}
			else
			{
				seekOffset = m_SeekOffset;
				sizeRead = m_pRezItm->Read((BYTE*)pData, size, seekOffset);
			}

			LeaveCriticalSection(&m_pTree->m_CriticalSection);

			m_SeekOffset += sizeRead;
			g_pDeFileLastRezItm = m_pRezItm;
			g_nDeFileLastRezPos = m_SeekOffset;
			if(sizeRead != size)
			{
				memset(pData, 0, size);
				m_ErrorStatus = 1;
				return LT_ERROR;
			}

			m_nReads++;
			m_nBytesRead += sizeRead;
		}
		return LT_OK;
	}
};


// FUNCTION: LITHTECH 0x00426af0 _$E4
// FUNCTION: LITHTECH 0x00426b00 _$E1
// FUNCTION: LITHTECH 0x00426b40 _$E3
// FUNCTION: LITHTECH 0x00426b50 _$E2
// GLOBAL: LITHTECH 0x004e33dc
static ObjectBank<DosFileStream, LCriticalSection> g_DosFileStreamBank(8, 8);
// FUNCTION: LITHTECH 0x00426b80 _$E9
// FUNCTION: LITHTECH 0x00426b90 _$E6
// FUNCTION: LITHTECH 0x00426bd0 _$E8
// FUNCTION: LITHTECH 0x00426be0 _$E7
// GLOBAL: LITHTECH 0x004e3414
static ObjectBank<RezFileStream, LCriticalSection> g_RezFileStreamBank(8, 8);


// FUNCTION: LITHTECH 0x00426c10
void DosFileStream::Release()
{
	g_DosFileStreamBank.Free(this);
}

// FUNCTION: LITHTECH 0x00426c40
void RezFileStream::Release()
{
	g_RezFileStreamBank.Free(this);
}



// LTFindInfo::m_pInternal..
struct LTFindData
{
	FileTree		*m_pTree;		// 0x000
	_finddata_t		m_Data;			// 0x004
	long			m_Handle;		// 0x11c
	CRezDir*		m_pCurDir;		// 0x120
	CRezTyp*		m_pCurTyp;		// 0x124
	CRezItm*		m_pCurItm;		// 0x128
	CRezDir*		m_pCurSubDir;	// 0x12c
};



// ------------------------------------------------------------------ //
// Interface functions.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00426c70
void df_Init()
{
	g_pDeFileLastRezItm = LTNULL;
	g_nDeFileLastRezPos = 0;
}

void df_Term()
{
}

// FUNCTION: LITHTECH 0x00426c80
int df_OpenTree(const char *pName, HLTFileTree *&pTreePointer)
{
	_finddata_t data;
	long handle, allocSize;
	FileTree *pTree;


	pTreePointer = NULL;

	// See if it exists..
	handle = _findfirst(pName, &data);
	if(handle == -1)
		return -1;

	_findclose(handle);


	allocSize = sizeof(FileTree) + strlen(pName);
	if(data.attrib & _A_SUBDIR)
	{
		// Ok, setup a dos tree.
		pTree = (FileTree*)dalloc_z(allocSize);
		pTree->m_TreeType = DosTree;
		pTree->m_pRezMgr = LTNULL;
	}
	else
	{
		pTree = (FileTree*)dalloc_z(allocSize);

		pTree->m_TreeType = RezFileTree;

		pTree->m_pRezMgr = new CRezMgr;
		if (pTree->m_pRezMgr == LTNULL)
			return -2;

		if(!pTree->m_pRezMgr->Open(pName))
		{
			delete pTree->m_pRezMgr;
			dfree(pTree);
			return -2;
		}

		pTree->m_pRezMgr->SetDirSeparators("\\/");
		g_pDeFileLastRezItm = LTNULL;
		g_nDeFileLastRezPos = 0;

		InitializeCriticalSection(&pTree->m_CriticalSection);
	}

	strcpy(pTree->m_BaseName, pName);
	pTreePointer = (HLTFileTree *)pTree;

	return 0;
}


// FUNCTION: LITHTECH 0x00426dd0
void df_CloseTree(HLTFileTree *hTree)
{
	FileTree *pTree;

	pTree = (FileTree*)hTree;
	if(!pTree)
		return;

	if (pTree->m_pRezMgr != LTNULL)
	{
		delete pTree->m_pRezMgr;
		pTree->m_pRezMgr = LTNULL;
		DeleteCriticalSection(&pTree->m_CriticalSection);
	}
	g_pDeFileLastRezItm = LTNULL;
	g_nDeFileLastRezPos = 0;

	dfree(pTree);
}


// FUNCTION: LITHTECH 0x00426e30
TreeType df_GetTreeType(HLTFileTree *hTree)
{
	FileTree *pTree;

	pTree = (FileTree*)hTree;
	if(!pTree)
		return DosTree;

	return pTree->m_TreeType;
}


// Builds the full DOS name of a file in the tree.
#define DF_MAKEFULLNAME(fullName, pTree, pName) \
	if(strstr(pName, "/")) \
		sprintf(fullName, "%s/%s", pTree->m_BaseName, pName); \
	else \
		sprintf(fullName, "%s\\%s", pTree->m_BaseName, pName);


// FUNCTION: LITHTECH 0x00426e50
int df_GetFileInfo(HLTFileTree *hTree, const char *pName, LTFindInfo *pInfo)
{
	FileTree *pTree;
	_finddata_t data;
	long handle, curRet;
	char fullName[500];
	CRezItm* pRezItm;

	pTree = (FileTree*)hTree;
	if(!pTree)
		return 0;

	if(pTree->m_TreeType == DosTree)
	{
		DF_MAKEFULLNAME(fullName, pTree, pName);

		handle = _findfirst(fullName, &data);
		curRet = handle;
		while(curRet != -1)
		{
			if(!(data.attrib & _A_SUBDIR))
			{
				strncpy(pInfo->m_Name, data.name, sizeof(pInfo->m_Name)-1);
				pInfo->m_Type = FILE_TYPE;
				pInfo->m_Size = data.size;
				memcpy(&pInfo->m_Date, &data.time_write, 4);

				_findclose(handle);
				return 1;
			}

			curRet = _findnext(handle, &data);
		}

		// Didn't find any files...
		_findclose(handle);
		return 0;
	}
	else
	{
		pRezItm = pTree->m_pRezMgr->GetRezFromDosPath(pName);
		if (pRezItm != LTNULL)
		{
			strncpy(pInfo->m_Name, pRezItm->GetName(), sizeof(pInfo->m_Name)-1);
			pInfo->m_Type = FILE_TYPE;
			pInfo->m_Size = pRezItm->GetSize();
			pInfo->m_Date = pRezItm->GetTime();
			return 1;
		}
		else
		{
			return 0;
		}
	}
}


// FUNCTION: LITHTECH 0x00426fb0
int df_GetFullFilename(HLTFileTree *hTree, const char *pName, char *pOutName, int maxLen)
{
	FileTree *pTree;

	pTree = (FileTree*)hTree;
	if(!pTree)
		return 0;

	if(pTree->m_TreeType != DosTree)
		return 0;

	DF_MAKEFULLNAME(pOutName, pTree, pName);
	return 1;
}


// FUNCTION: LITHTECH 0x00427020
ILTStream* df_Open(HLTFileTree *hTree, const char *pName, int openMode)
{
	FileTree *pTree = (FileTree*)hTree;
	if(!pTree)
		return LTNULL;

	if(pTree->m_TreeType == DosTree)
	{
		char fullName[500];
		DF_MAKEFULLNAME(fullName, pTree, pName);

		FILE *fp;
		{
			CountAdder cntAdd(&g_PD_FOpen);
			fp = fopen(fullName, "rb");
		}
		if(!fp)
			return LTNULL;

		fseek(fp, 0, SEEK_END);
		uint32 fileLen = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		// Use fp to setup the stream.
		DosFileStream *pDosStream = g_DosFileStreamBank.Allocate();
		pDosStream->m_pFile = fp;
		pDosStream->m_pTree = pTree;
		pDosStream->m_FileLen = fileLen;
		pDosStream->m_SeekOffset = 0;

		if (g_CV_ShowFileAccess >= 1)
		{
			dsi_ConsolePrint("stream %p open file %s size = %u",pDosStream,pName,fileLen);
		}

		return pDosStream;
	}
	else
	{
		CRezItm* pRezItm = pTree->m_pRezMgr->GetRezFromDosPath(pName);
		if (pRezItm == LTNULL) return LTNULL;

		uint32 fileLen = pRezItm->GetSize();

		// Use fp to setup the stream.
		RezFileStream *pRezStream = g_RezFileStreamBank.Allocate();
		pRezStream->m_pRezItm = pRezItm;
		pRezStream->m_pTree = pTree;
		pRezStream->m_FileLen = fileLen;
		pRezStream->m_SeekOffset = 0;

		if (g_CV_ShowFileAccess >= 1)
		{
			dsi_ConsolePrint("stream %p open rez %s size = %u",pRezStream,pName,fileLen);
		}

		return pRezStream;
	}
}


// FUNCTION: LITHTECH 0x004274b0
int df_FindNext(HLTFileTree *hTree, const char *pDirName, LTFindInfo *pInfo)
{
	LTFindData *pFindData;
	char filter[400];
	FileTree *pTree;
	long curRet;
	CRezItm* pRezItm;
	CRezDir* pRezDir;
	CRezTyp* pRezTyp;
	CRezDir* pRezSubDir;
	char	 sItemType[5];

	if(!hTree)
		return 0;

	pTree = (FileTree*)hTree;
	if(pTree->m_TreeType == DosTree)
	{
		if(!pInfo->m_pInternal)
		{
			if(strstr(pDirName, "/"))
				sprintf(filter, "%s/%s/*.*", pTree->m_BaseName, pDirName);
			else
				sprintf(filter, "%s\\%s\\*.*", pTree->m_BaseName, pDirName);

			pFindData = (LTFindData*)dalloc(sizeof(LTFindData));
			pFindData->m_pTree = pTree;
			pFindData->m_Handle = _findfirst(filter, &pFindData->m_Data);
			curRet = pFindData->m_Handle;

			pInfo->m_pInternal = pFindData;
		}
		else
		{
			pFindData = (LTFindData*)pInfo->m_pInternal;
			curRet = _findnext(pFindData->m_Handle, &pFindData->m_Data);
		}

		// Find a valid name..
		for(;;)
		{
			if(curRet == -1)
			{
				pInfo->m_pInternal = LTNULL;

				if(pFindData->m_Handle != -1)
					_findclose(pFindData->m_Handle);

				dfree(pFindData);
				return 0;
			}

			if(pFindData->m_Data.name[0] != '.')
				break;

			curRet = _findnext(pFindData->m_Handle, &pFindData->m_Data);
		}

		// Ok, found a valid one.
		strncpy(pInfo->m_Name, pFindData->m_Data.name, sizeof(pInfo->m_Name)-1);
		memcpy(&pInfo->m_Date, &pFindData->m_Data.time_write, 4);
		pInfo->m_Size = pFindData->m_Data.size;
		pInfo->m_Type = (pFindData->m_Data.attrib & _A_SUBDIR) ? DIRECTORY_TYPE : FILE_TYPE;

		return 1;
	}
	else
	{
		// If this is the first call to FindNext then set everything up
		if(pInfo->m_pInternal == LTNULL)
		{
			// find the directory
			pRezDir = pTree->m_pRezMgr->GetDirFromPath(pDirName);
			if(pRezDir == LTNULL)
			{
				return 0;
			}

			// get the first type
			pRezTyp = pRezDir->GetFirstType();
			if (pRezTyp != LTNULL)
			{
				// get the first item
				pRezItm = pRezDir->GetFirstItem(pRezTyp);
			}
			else
			{
				pRezItm = LTNULL;
			}

			// if there were no files then get the first sub directory
			if (pRezItm == LTNULL)
			{
				pRezSubDir = pRezDir->GetFirstSubDir();
				if (pRezSubDir == LTNULL)
				{
					return 0;
				}
			}
			else pRezSubDir = LTNULL;

			// allocate the finddata structure
			pFindData = (LTFindData*)dalloc(sizeof(LTFindData));
			pInfo->m_pInternal = pFindData;

			// fill in the members of the finddata structure
			pFindData->m_pTree = pTree;
			pFindData->m_pCurDir = pRezDir;
			pFindData->m_pCurTyp = pRezTyp;
			pFindData->m_pCurItm = pRezItm;
			pFindData->m_pCurSubDir = pRezSubDir;
		}

		// If this is not the first call then just get the next entry (or if at end clean up)
		else
		{
			pFindData = (LTFindData*)pInfo->m_pInternal;

			// are we searching for items
			if (pFindData->m_pCurItm != LTNULL)
			{
				pFindData->m_pCurItm = pFindData->m_pCurDir->GetNextItem(pFindData->m_pCurItm);

				// if we are finished with all the items of this type go to the next
				while(pFindData->m_pCurItm == LTNULL)
				{
					pFindData->m_pCurTyp = pFindData->m_pCurDir->GetNextType(pFindData->m_pCurTyp);

					// if we are done with all the types then we need to switch to checking sub directories
					if (pFindData->m_pCurTyp == LTNULL)
					{
						pFindData->m_pCurSubDir = pFindData->m_pCurDir->GetFirstSubDir();
						if (pFindData->m_pCurSubDir == LTNULL)
						{
							dfree(pInfo->m_pInternal);
							pInfo->m_pInternal = LTNULL;
							return 0;
						}
						break;
					}

					// get the first item of this type
					else pFindData->m_pCurItm = pFindData->m_pCurDir->GetFirstItem(pFindData->m_pCurTyp);
				}
			}

			// otherwise we are searching for sub directories
			else
			{
				pFindData->m_pCurSubDir = pFindData->m_pCurDir->GetNextSubDir(pFindData->m_pCurSubDir);
				if (pFindData->m_pCurSubDir == LTNULL)
				{
					dfree(pInfo->m_pInternal);
					pInfo->m_pInternal = LTNULL;
					return 0;
				}
			}
		}

		// is this a resource item
		if (pFindData->m_pCurItm != LTNULL)
		{
			strncpy(pInfo->m_Name, pFindData->m_pCurItm->GetName(), sizeof(pInfo->m_Name)-4);
			pTree->m_pRezMgr->TypeToStr(pFindData->m_pCurItm->GetType(),sItemType);
			strcat(pInfo->m_Name, ".");
			strcat(pInfo->m_Name, sItemType);
			pInfo->m_Date = pFindData->m_pCurItm->GetTime();
			pInfo->m_Type = FILE_TYPE;
			pInfo->m_Size = pFindData->m_pCurItm->GetSize();
		}

		// is this a sub directory
		else if (pFindData->m_pCurSubDir != LTNULL)
		{
			strncpy(pInfo->m_Name, pFindData->m_pCurSubDir->GetDirName(), sizeof(pInfo->m_Name)-1);
			pInfo->m_Date = pFindData->m_pCurSubDir->GetTime();
			pInfo->m_Type = DIRECTORY_TYPE;
			pInfo->m_Size = 0;
		}

		else
		{
			return 0; // THIS SHOULD NEVER HAPPEN!
		}

		return 1;
	}
}


#define SAVEBUFSIZE 1024*16

// Save the contents of a steam to a file
// returns 1 if successful 0 if an error occured
// FUNCTION: LITHTECH 0x00427850
int df_Save(ILTStream *hFile, const char *pName)
{
	unsigned char* pSaveBuf;
	uint32 nBytesSaved;
	uint32 nAmountToRead, fileLen;
	FILE* pSaveFile;

	if(!hFile || hFile->ErrorStatus() != LT_OK)
		return 0;

	// open the save file for writing
	pSaveFile = fopen(pName, "wb");
	if (pSaveFile == LTNULL) return 0;

	// allocate memory for a temp buffer
	pSaveBuf = new uint8[SAVEBUFSIZE];
	if (pSaveBuf == LTNULL)
	{
		fclose(pSaveFile);
		return 0;
	}

	// set to the start of the stream
	hFile->SeekTo(0);

	// initialize number of bytes saved
	nBytesSaved = 0;

	// loop to copy entire file
	hFile->GetLen(&fileLen);
	while (nBytesSaved < fileLen)
	{
		// figure out how much data to read into the buffer
		nAmountToRead = fileLen - nBytesSaved;
		if (nAmountToRead > SAVEBUFSIZE) nAmountToRead = SAVEBUFSIZE;

		// read data from the stream
		hFile->Read(pSaveBuf, nAmountToRead);
		if (hFile->ErrorStatus() != LT_OK) break;

		// write out data to the save file and increment save counter
		if (fwrite(pSaveBuf, nAmountToRead, 1, pSaveFile) != 1) break;
		else nBytesSaved += nAmountToRead;
	}

	// clean up temp buffer
	delete [] pSaveBuf;

	// close the save file
	fclose(pSaveFile);

	// return correct success or fail status
	if (nBytesSaved != fileLen) return 0;
	else return 1;
}


// Returns raw file information for the file specified in pName found in the tree hTree
// FUNCTION: LITHTECH 0x00427930
int df_GetRawInfo(HLTFileTree *hTree, const char *pName, char* sFileName, unsigned int nMaxFileName, uint32* nPos, uint32* nSize)
{
	char fullName[500];
	FileTree *pTree;
	FILE *fp;
	CRezItm* pRezItm;

	// cast as a FileTree
	pTree = (FileTree*)hTree;
	if(!pTree) return 0;

	// if this is a dos file tree
	if(pTree->m_TreeType == DosTree)
	{
		// create the full name for the file
		DF_MAKEFULLNAME(fullName, pTree, pName);

		// open the file to make sure it is ok
		fp = fopen(fullName, "rb");
		if(!fp)	return 0;

		// figure out the length of the file
		fseek(fp, 0, SEEK_END);
		*nSize = ftell(fp);

		// the position is the start of the file
		*nPos = 0;

		// close the file
		fclose(fp);

		// copy over the name
		strncpy(sFileName, fullName, nMaxFileName);
		sFileName[nMaxFileName-1] = 0;

		// return error if name is too long
		if (nMaxFileName <= strlen(fullName)) return 0;

		// return LTTRUE everything went OK
		return 1;
	}
	else
	{
		// get the rez item that corresponds to this resource
		pRezItm = pTree->m_pRezMgr->GetRezFromDosPath(pName);
		if (pRezItm == LTNULL) return 0;

		// copy over the name
		strncpy(sFileName, pRezItm->DirectRead_GetFullRezName(), nMaxFileName);
		sFileName[nMaxFileName-1] = 0;

		// return error if name is too long
		if (nMaxFileName <= strlen(pRezItm->DirectRead_GetFullRezName()))
			return 0;

		// get the position of the resource in the resource file
		*nPos = pRezItm->DirectRead_GetFileOffset();

		// get the size of the resource
		*nSize = pRezItm->GetSize();

		// return LTTRUE everything went OK
		return 1;
	}
}


// Virtual and template members emitted here.
// FUNCTION: LITHTECH 0x00427270 ?ErrorStatus@BaseFileStream@@UAEKXZ
// FUNCTION: LITHTECH 0x00427280 ?GetLen@BaseFileStream@@UAEKPAK@Z
// FUNCTION: LITHTECH 0x00427290 ??0CGenLTStream@@QAE@XZ
// FUNCTION: LITHTECH 0x004272a0 ??_GCGenLTStream@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004272c0 ?GetPos@DosFileStream@@UAEKPAK@Z
// FUNCTION: LITHTECH 0x004272e0 ?SeekTo@DosFileStream@@UAEKK@Z
// FUNCTION: LITHTECH 0x00427310 ?Read@DosFileStream@@UAEKPAXK@Z
// FUNCTION: LITHTECH 0x004273b0 ?SeekTo@RezFileStream@@UAEKK@Z
// Read m_SeekOffset after testing the cached item, matching the original load order.
// FUNCTION: LITHTECH 0x004273e0 ?Read@RezFileStream@@UAEKPAXK@Z
// FUNCTION: LITHTECH 0x00427ac0 ?AllocVoid@?$ObjectBank@VDosFileStream@@VLCriticalSection@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00427b30 ?AllocVoid@?$ObjectBank@VRezFileStream@@VLCriticalSection@@@@UAEPAXXZ
// The identical RezFileStream COMDAT follows its AllocVoid instance in the original ICF order.
// FUNCTION: LITHTECH 0x00427ba0 ?FreeVoid@?$ObjectBank@VRezFileStream@@VLCriticalSection@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00427be0 ?Term@?$ObjectBank@VDosFileStream@@VLCriticalSection@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00427c10 ??_G?$ObjectBank@VDosFileStream@@VLCriticalSection@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00427c50 ??_G?$ObjectBank@VRezFileStream@@VLCriticalSection@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00427c90 ??_GBaseFileStream@@UAEPAXI@Z
