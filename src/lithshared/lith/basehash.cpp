// lithshared lith/basehash.cpp (Talon). The Talon .cpp is not on disk (build\proj\LT2\lithshared
// has only the headers); this is a verbatim copy of Jupiter libs/lith/basehash.cpp, built against the
// Talon header lithshared/lith/basehash.h. lith.lib's basehash/virtlist objects were linked late,
// after part of the CRT (0x4c31a0).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/lith

#ifdef _WINDOWS
#include "windows.h"
#endif

#include "basehash.h"


// FUNCTION: LITHTECH 0x004c31a0
CBaseHashItem* CBaseHashItem::Next() {
  CBaseHashItem* pItem;
  pItem = (CBaseHashItem*)CBaseListItem::Next();
  if (pItem == NULL) {
    unsigned int Bin = m_nCurBin + 1;
    while (Bin < m_pParentHash->m_nNumBins) {
	  pItem = (CBaseHashItem*)m_pParentHash->m_pBinAry[Bin].m_lstItems.GetFirst();
	  if (pItem != NULL) break;
	  Bin++;
    };
  }
  return pItem;
};

CBaseHashItem* CBaseHashItem::Prev() {
  CBaseHashItem* pItem;
  pItem = (CBaseHashItem*)CBaseListItem::Prev();
  if (pItem == NULL) {
    unsigned int Bin = m_nCurBin;
    while (Bin > 0) {
	  Bin --;
	  pItem = (CBaseHashItem*)m_pParentHash->m_pBinAry[Bin].m_lstItems.GetLast();
	  if (pItem != NULL) break;
    };
  }
  return pItem;
};

// FUNCTION: LITHTECH 0x004c31f0 ??0CBaseHash@@QAE@XZ
CBaseHash::CBaseHash() {
  m_nNumBins = 0;
  m_pBinAry = NULL;
};

// FUNCTION: LITHTECH 0x004c3280 ??0CHashBin@CBaseHash@@QAE@XZ
// FUNCTION: LITHTECH 0x004c3200 ??0CBaseHash@@QAE@I@Z
CBaseHash::CBaseHash(unsigned int NumBins) {
  m_nNumBins = NumBins;
  m_pBinAry = new CHashBin[m_nNumBins];
  ASSERT(m_pBinAry != NULL);
};	

// FUNCTION: LITHTECH 0x004c3290
CBaseHash::~CBaseHash() {
  if (m_pBinAry != NULL) delete [] m_pBinAry;
};

// FUNCTION: LITHTECH 0x004c32c0
void CBaseHash::Insert(CBaseHashItem* pItem) {
  pItem->m_pParentHash = this;
  unsigned int CurBin = pItem->HashFunc();
  ASSERT(CurBin < m_nNumBins);
  pItem->m_nCurBin = CurBin;
  m_pBinAry[CurBin].m_lstItems.InsertFirst(pItem);
};

// FUNCTION: LITHTECH 0x004c32f0
void CBaseHash::Delete(CBaseHashItem* pItem) {
  ASSERT(pItem->m_nCurBin < m_nNumBins);
  m_pBinAry[pItem->m_nCurBin].m_lstItems.Delete(pItem);
};

// FUNCTION: LITHTECH 0x004c3320
CBaseHashItem* CBaseHash::GetFirst() {
  unsigned int Bin = 0;
  CBaseHashItem* pElem;
  do {
    pElem = (CBaseHashItem*)m_pBinAry[Bin].m_lstItems.GetFirst();
	Bin++;
  } while ((pElem == NULL) && (Bin < m_nNumBins));
  return pElem;
};

CBaseHashItem* CBaseHash::GetLast() {
  unsigned int Bin = m_nNumBins-1;
  CBaseHashItem* pElem;
  do {
    pElem = (CBaseHashItem*)m_pBinAry[Bin].m_lstItems.GetLast();
	if (Bin > 0) Bin--;
	else break;
  } while (pElem == NULL);
  return pElem;
};

// FUNCTION: LITHTECH 0x004c3350
CBaseHashItem*  CBaseHash::GetFirstInBin(unsigned int Bin) {
  ASSERT(Bin < m_nNumBins);
  return (CBaseHashItem*)m_pBinAry[Bin].m_lstItems.GetFirst();
};

// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from the GPL release of the LithTech Jupiter Ex engine; see README.md (License).
