// The .rez file format structures (Jupiter libs/rezmgr/rezmgr.cpp keeps them in the .cpp; Talon shares them).
#ifndef __REZMGR_FORMAT_H__
#define __REZMGR_FORMAT_H__

#include "rezmgr.h"

#pragma pack(1)
struct FileMainHeaderStruct {
  char CR1;
  char LF1;
  char FileType[RezMgrUserTitleSize];
  char CR2;
  char LF2;
  char UserTitle[RezMgrUserTitleSize];
  char CR3;
  char LF3;
  char EOF1;
  DWORD FileFormatVersion; 	// the file format version number only 1 is possible here right now
  DWORD RootDirPos; 		// Position of the root directory structure in the file
  DWORD RootDirSize;        // Size of root directory
  DWORD RootDirTime;        // Time Root dir was last updated
  DWORD NextWritePos;       // Position of first directory in the file
  DWORD Time; 				// Time resource file was last updated
  DWORD LargestKeyAry;		// Size of the largest key array in the resource file
  DWORD LargestDirNameSize; // Size of the largest directory name in the resource file (including 0 terminator)
  DWORD LargestRezNameSize;	// Size of the largest resource name in the resource file (including 0 terminator)
  DWORD LargestCommentSize;	// Size of the largest comment in the resource file (including 0 terminator)
  BYTE  IsSorted;           // If 0 then data is not sorted if 1 then it is sorted
};

enum FileDirEntryType {
  ResourceEntry = 0,
  DirectoryEntry = 1
};

struct FileDirEntryDirHeader {
  DWORD Pos; 					// File positon of dir entry
  DWORD Size;					// Size of directory data
  DWORD Time;					// Last time anything in directory was modified
//  char Name[];				// Name of this directory
};

struct FileDirEntryRezHeader {
  DWORD Pos; 					// File positon of dir entry
  DWORD Size;					// Size of directory data
  DWORD Time;					// Last time this resource was modified
  DWORD ID;                     // Resource ID number
  DWORD Type;					// Type of resource this is
  DWORD NumKeys;				// The number of keys to read in for this resource
//  char Name[];				// The name of this resource
//  char Comment[];             // The comment data for this resource
//  DWORD Keys[];				// The key values for this resource
};

struct FileDirEntryHeader {
  DWORD Type;
  union {
    FileDirEntryRezHeader Rez;
	FileDirEntryDirHeader Dir;
  };
};
#pragma pack()

#endif  // __REZMGR_FORMAT_H__
