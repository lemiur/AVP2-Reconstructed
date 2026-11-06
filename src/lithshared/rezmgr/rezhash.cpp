// lithshared rezmgr/rezhash.cpp (Talon). Verbatim copy of Jupiter libs/rezmgr/rezhash.cpp (the Talon .cpp
// is not on disk) without the non-Win32 stricmp and CRezItmHashByID::Next (inline in the Talon rezhash.h).
// The linker folded the identical CRezItmHashTableByName/CRezDirHashTable HashFunc and Find pairs (ICF).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/lith /IE:/AVP2Source/build/proj/LT2/lithshared/rezmgr


#include "assert.h"
#include <stdio.h>
#include <stdlib.h>
#define REZMGRDONTUNDEF
#include "rezmgr.h"
#include <string.h>



// -----------------------------------------------------------------------------------------
// CRezItmHashByName

// FUNCTION: LITHTECH 0x004b1be0
unsigned int CRezItmHashByName::HashFunc() {
  if (m_pRezItm == NULL) return 0;
  else return GetParentHash()->HashFunc(m_pRezItm->GetName());
};


// -----------------------------------------------------------------------------------------
// CRezItmHashTableByName

// FUNCTION: LITHTECH 0x004b1c00
unsigned int CRezItmHashTableByName::HashFunc(REZCNAME pStr) {
  if (pStr == NULL) return 0;
  unsigned int Count;
  for (Count = 0; *pStr != '\0'; pStr++) { Count++; };
  ASSERT(GetNumBins() > 0);
  return (Count % GetNumBins());
};

// -----------------------------------------------------------------------------------------
// CRezTypeHash

// FUNCTION: LITHTECH 0x004b1c30
unsigned int CRezTypeHash::HashFunc() {
  ASSERT(m_pRezTyp != NULL);
  return GetParentHash()->HashFunc(m_pRezTyp->GetType());
};


// -----------------------------------------------------------------------------------------
// CRezTypeHashTable

// FUNCTION: LITHTECH 0x004b1c40
unsigned int CRezTypeHashTable::HashFunc(REZTYPE nType) {
  ASSERT(GetNumBins() > 0);
  return (nType % GetNumBins());
};

// FUNCTION: LITHTECH 0x004b1c50
CRezTyp* CRezTypeHashTable::Find(REZTYPE nType) {
  CRezTypeHash* pItm = GetFirstInBin(HashFunc(nType));
  while (pItm != NULL) {
    ASSERT(pItm->GetRezTyp() != NULL);
    if (pItm->GetRezTyp()->GetType() == nType) return pItm->GetRezTyp();
    pItm = pItm->NextInBin();
  }
  return NULL;
};


// -----------------------------------------------------------------------------------------
// CRezDirHash

// FUNCTION: LITHTECH 0x004b1c90
unsigned int CRezDirHash::HashFunc() {
  ASSERT(m_pRezDir != NULL);
  return GetParentHash()->HashFunc(m_pRezDir->GetDirName());
};


// -----------------------------------------------------------------------------------------
// CRezDirHashTable

// (folded into CRezItmHashTableByName::HashFunc at 0x004b1c00)
unsigned int CRezDirHashTable::HashFunc(REZCDIRNAME pStr) {
  ASSERT(pStr != NULL);
  if (pStr == NULL) return 0;
  unsigned int Count;
  for (Count = 0; *pStr != '\0'; pStr++) { Count++; };
  ASSERT(GetNumBins() > 0);
  return (Count % GetNumBins());
};

// (folded into CRezItmHashTableByName::Find at 0x004b1ca0)
CRezDir* CRezDirHashTable::Find(REZCDIRNAME sName, BOOL bIgnoreCase) {
  ASSERT(sName != NULL);
  if (sName == NULL) return NULL;
  CRezDirHash* pItm = GetFirstInBin(HashFunc(sName));
  if (bIgnoreCase) {
    while (pItm != NULL) {
      ASSERT(pItm->GetRezDir() != NULL);
      ASSERT(pItm->GetRezDir()->GetDirName() != NULL);
      if (stricmp(pItm->GetRezDir()->GetDirName(),sName) == 0) return pItm->GetRezDir();
      pItm = pItm->NextInBin();
    }
  }
  else {
    while (pItm != NULL) {
      ASSERT(pItm->GetRezDir() != NULL);
      ASSERT(pItm->GetRezDir()->GetDirName() != NULL);
      if (strcmp(pItm->GetRezDir()->GetDirName(),sName) == 0) return pItm->GetRezDir();
      pItm = pItm->NextInBin();
    }
  }
  return NULL;
};

// The linker kept this copy (placed after CRezDirHashTable::HashFunc) when it folded CRezDirHashTable::Find into
// CRezItmHashTableByName::Find, so the surviving function sits last in the object.
// FUNCTION: LITHTECH 0x004b1ca0
CRezItm* CRezItmHashTableByName::Find(REZCNAME sName, BOOL bIgnoreCase) {
  ASSERT(sName != NULL);
  if (sName == NULL) return NULL;
  CRezItmHashByName* pItm = GetFirstInBin(HashFunc(sName));
  if (bIgnoreCase) {
    while (pItm != NULL) {
      ASSERT(pItm->GetRezItm() != NULL);
      ASSERT(pItm->GetRezItm()->GetName() != NULL);
      if (stricmp(pItm->GetRezItm()->GetName(),sName) == 0) return pItm->GetRezItm();
      pItm = pItm->NextInBin();
    }
  }
  else {
    while (pItm != NULL) {
      ASSERT(pItm->GetRezItm() != NULL);
      ASSERT(pItm->GetRezItm()->GetName() != NULL);
      if (strcmp(pItm->GetRezItm()->GetName(),sName) == 0) return pItm->GetRezItm();
      pItm = pItm->NextInBin();
    }
  }
  return NULL;
};

// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from the GPL release of the LithTech Jupiter Ex engine; see README.md (License).
