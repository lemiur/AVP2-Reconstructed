// Jupiter runtime/shared/src/nexus.h
// A Nexus holds a singly linked list of Leeches that get notified of nexus messages.
#ifndef __NEXUS_H__
#define __NEXUS_H__

#include "ltbasetypes.h"

class Nexus;
class Leech;

#define NEXUS_NEXUSDESTROY  0   // Nexus is being destroyed.  pUserData is a LTBOOL*

struct LeechDef
{
	LTRESULT	(*m_Fn)(Nexus *pNexus, Leech *pLeech, int msg, void *pUserData);
	LeechDef	*m_pParent;
};

// The root leech definition (leech.cpp).
extern LeechDef g_BaseLeech;

class Nexus
{
public:
				Nexus();
				~Nexus();

	LTBOOL		Init(void *pData);
	void		Term();

	LTRESULT	SendMessage(int msg, void *pUserData);

	void		RemoveLeech(Leech *pLeech);
	Leech*		FindLeech(LeechDef *pDef);

	Leech		*m_LeechHead;	// 0x00
	void		*m_pData;		// 0x04
};

class Leech
{
public:
				Leech()								{Init(NULL, NULL);}
				Leech(LeechDef *pDef)				{Init(pDef, NULL);}
				Leech(LeechDef *pDef, void *pUserData)	{Init(pDef, pUserData);}

	void		Init(LeechDef *pDef, void *pUserData)
	{
		m_Def = pDef;
		m_pUserData = pUserData;
		m_pNext = NULL;
	}

	LeechDef	*m_Def;			// 0x00
	void		*m_pUserData;	// 0x04
	Leech		*m_pNext;		// 0x08
};

#endif
