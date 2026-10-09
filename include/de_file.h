// Talon file trees (Jupiter runtime/kernel/io/src/sys/win/de_file.h).
#ifndef __DE_FILE_H__
#define __DE_FILE_H__

#include "ltbasedefs.h"

typedef void* HLTFileTree;  // A file tree identifier.

enum TreeType
{
	RezFileTree,
	DosTree
};

#define DIRECTORY_TYPE	0
#define FILE_TYPE		1

#define DFOPEN_READ	0

// 0x110 bytes.
struct LTFindInfo
{
	int				m_Type;			// 0x000 Is this a directory or file?
	char			m_Name[256];	// 0x004
	uint32			m_Date;			// 0x104 File date/time identifier.
	uint32			m_Size;			// 0x108 File size.
	void			*m_pInternal;	// 0x10c
};

struct FileEntry;

int			df_OpenTree(const char *pName, HLTFileTree *&pTreePointer);
void		df_CloseTree(HLTFileTree *hTree);
TreeType	df_GetTreeType(HLTFileTree *hTree);
int			df_GetFileInfo(HLTFileTree *hTree, const char *pName, LTFindInfo *pInfo);
ILTStream*	df_Open(HLTFileTree *hTree, const char *pName, int openMode=DFOPEN_READ);
int			df_Save(ILTStream *hFile, const char *pName);

void		df_Init();
void		df_Term();	// empty (identical-code folded at 0x00473ac0)
int			df_GetFullFilename(HLTFileTree *hTree, const char *pName, char *pOutName, int maxLen);
int			df_FindNext(HLTFileTree *hTree, const char *pDirName, LTFindInfo *pInfo);
int			df_GetRawInfo(HLTFileTree *hTree, const char *pName, char *sFileName,
	unsigned int nMaxFileName, uint32 *nPos, uint32 *nSize);

// impl_common.
FileEntry*	ic_GetFileList(HLTFileTree **trees, int nTrees, const char *pDirName);

// PlayDemo profile counters.
extern uint32 g_PD_FOpen;

#endif  // __DE_FILE_H__
