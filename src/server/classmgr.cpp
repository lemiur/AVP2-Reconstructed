// Jupiter runtime/server/src/classmgr.cpp
#include <string.h>
#include "ltengineobjects.h"
#include "servermgr.h"
#include "s_object.h"
#include "dhashtable.h"

#ifndef HASH_RAW
#define HASH_RAW	2
#endif

LTRESULT dsi_LoadServerObjects(CClassMgr *pClassMgr);
LTRESULT sm_SetupError(CServerMgr *pServerMgr, LTRESULT err, ...);
LTRESULT sm_AddObjectToWorld(CServerMgr *pServerMgr, LPBASECLASS pObject, ClassDef *pClass,
	ObjectCreateStruct *pStruct, uint16 objectID, uint32 createMethod, LTObject **ppObject);

#define FUNCTION_POINTER(theClass, theOffset) (*(void**)(((char*)theClass + theOffset)))

static LTRESULT InitExtraClassData(CClassMgr *pClassMgr);
static void SetupClassFunctions(CClassMgr *pClassMgr);
static LTRESULT CreateStaticObjects(CClassMgr *pClassMgr);


// ----------------------------------------------------------------------- //
// CClassMgr functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00402870
CClassMgr::CClassMgr()
{
	memset(this, 0, sizeof(CClassMgr));
}

// FUNCTION: LITHTECH 0x00402890
CClassMgr::~CClassMgr()
{
	Term();
}

// FUNCTION: LITHTECH 0x004028a0
LTBOOL CClassMgr::Init(CServerMgr *pServerMgr)
{
	memset(this, 0, sizeof(CClassMgr));
	m_pServerMgr = pServerMgr;
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x004028c0
void CClassMgr::Term()
{
	int i;

	if (m_hClassNameHash)
	{
		hs_DestroyHashTable(m_hClassNameHash);
		m_hClassNameHash = LTNULL;
	}

	if (m_ClassDatas)
	{
		for (i=0; i < m_nClassDatas; i++)
		{
			sb_Term(&m_ClassDatas[i].m_ObjectBank);
			m_ClassDatas[i].m_pClass->m_pInternal[m_ClassIndex] = LTNULL;
		}

		delete [] m_ClassDatas;
		m_ClassDatas = LTNULL;
	}

	if (m_pServerShell)
		m_DeleteServerShellFn(m_pServerShell);

	if (m_hShellModule)
		sb_UnloadShellModule(m_hShellModule);

	if (m_ClassModule)
		cb_UnloadModule(m_ClassModule);

	if (m_hServerResourceModule)
	{
		bm_UnbindModule(m_hServerResourceModule);
		m_hServerResourceModule = LTNULL;
	}

	m_hShellModule = LTNULL;
	m_ClassModule = LTNULL;
	m_pServerShell = LTNULL;
}

// FUNCTION: LITHTECH 0x00402970
CClassData* CClassMgr::FindClassData(const char *pName)
{
	HHashElement *hElement = hs_FindElement(m_hClassNameHash, pName, strlen(pName));

	if (hElement)
	{
		return (CClassData*)hs_GetElementUserData(hElement);
	}
	else
	{
		return LTNULL;
	}
}


// ----------------------------------------------------------------------- //
// Exposed functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004029b0
LTRESULT LoadServerBinaries(CClassMgr *pClassMgr)
{
	LTRESULT dResult;

	// Load the server module.
	if ((dResult = dsi_LoadServerObjects(pClassMgr)) != LT_OK)
		return dResult;

	InitExtraClassData(pClassMgr);

	// Hook anything pointing to BaseClass functions.
	pClassMgr->m_pBaseClass = cb_FindClass(pClassMgr->m_pServerMgr->GetClassModule(), "BaseClass");
	if (!pClassMgr->m_pBaseClass)
	{
		sm_SetupError(pClassMgr->m_pServerMgr, LT_MISSINGCLASS, "%s", "BaseClass");
		RETURN_ERROR_PARAM(1, LoadServerBinaries, LT_MISSINGCLASS, "BaseClass");
	}

	SetupClassFunctions(pClassMgr);

	// Create the server shell.
	pClassMgr->m_pServerShell = pClassMgr->m_CreateServerShellFn(pClassMgr->m_pServerMgr->m_pServerInterface);
	if (!pClassMgr->m_pServerShell)
	{
		sm_SetupError(pClassMgr->m_pServerMgr, LT_CANTCREATESERVERSHELL, "");
		RETURN_ERROR(1, LoadServerBinaries, LT_ERROR);
	}

	return CreateStaticObjects(pClassMgr);
}


// ----------------------------------------------------------------------- //
// Static functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00402ab0
static LTRESULT InitExtraClassData(CClassMgr *pClassMgr)
{
	ClassDef *pClass, **pClasses;
	int i, nClasses;
	CClassData *pClassData;
	HHashElement *hElement;

	pClasses = cb_GetClassDefs(pClassMgr->m_ClassModule);
	nClasses = cb_GetNumClassDefs(pClassMgr->m_ClassModule);

	if (nClasses > 0)
	{
		if (pClasses[0]->m_pInternal[0])
		{
			if (pClasses[0]->m_pInternal[1])
			{
				RETURN_ERROR(1, InitExtraClassData, LT_ERROR);
			}
			else
			{
				pClassMgr->m_ClassIndex = 1;
			}
		}
		else
		{
			pClassMgr->m_ClassIndex = 0;
		}

		pClassMgr->m_hClassNameHash = hs_CreateHashTable(50, HASH_RAW);

		pClassMgr->m_ClassDatas = new CClassData[nClasses];
		pClassMgr->m_nClassDatas = nClasses;

		memset(pClassMgr->m_ClassDatas, 0, sizeof(CClassData)*nClasses);

		for (i=0; i < nClasses; i++)
		{
			pClass = pClasses[i];

			pClassData = &pClassMgr->m_ClassDatas[i];

			sb_Init2(&pClassData->m_ObjectBank, pClass->m_ClassObjectSize, 32, 8);
			pClassData->m_ClassID = (uint16)i;
			pClassData->m_pClass = pClass;
			pClass->m_pInternal[pClassMgr->m_ClassIndex] = pClassData;

			hElement = hs_AddElement(pClassMgr->m_hClassNameHash, pClass->m_ClassName, strlen(pClass->m_ClassName));
			if (!hElement)
				RETURN_ERROR(1, InitExtraClassData, LT_ERROR);

			hs_SetElementUserData(hElement, pClassData);
		}
	}

	return LT_ERROR;
}


static void SetupClassNullFunctions(ClassDef *pClass, uint32 offset);

// FUNCTION: LITHTECH 0x00402c30
static void SetupClassFunctions(CClassMgr *pClassMgr)
{
	ClassDef *pClass, **pClasses;
	int i, nClasses;

	pClasses = cb_GetClassDefs(pClassMgr->m_ClassModule);
	nClasses = cb_GetNumClassDefs(pClassMgr->m_ClassModule);

	// Make any LTNULL functions go to their parent class.
	for (i=0; i < nClasses; i++)
	{
		pClass = pClasses[i];

		SetupClassNullFunctions(pClass, (char*)&pClass->m_ConstructFn - (char*)pClass);
		SetupClassNullFunctions(pClass, (char*)&pClass->m_DestructFn - (char*)pClass);
	}
}


// FUNCTION: LITHTECH 0x00402c80
static void SetupClassNullFunctions(ClassDef *pClass, uint32 offset)
{
	if (FUNCTION_POINTER(pClass, offset) == LTNULL)
	{
		if (pClass->m_ParentClass)
		{
			SetupClassNullFunctions(pClass->m_ParentClass, offset);
			FUNCTION_POINTER(pClass, offset) = FUNCTION_POINTER(pClass->m_ParentClass, offset);
		}
	}
}



// FUNCTION: LITHTECH 0x00402cb0
static LTRESULT CreateStaticObjects(CClassMgr *pClassMgr)
{
	int i;
	ObjectCreateStruct theStruct;
	LPBASECLASS pObject;
	ClassDef *pClass;
	LTRESULT dResult;

	for (i=0; i < pClassMgr->m_nClassDatas; i++)
	{
		theStruct.Clear();

		pClass = pClassMgr->m_ClassDatas[i].m_pClass;

		if (pClass->m_ClassFlags & CF_STATIC)
		{
			pObject = sm_AllocateObjectOfClass(pClassMgr->m_pServerMgr, pClass);
			dResult = sm_AddObjectToWorld(pClassMgr->m_pServerMgr, pObject,
				pClass, &theStruct, INVALID_OBJECTID,
				OBJECTCREATED_NORMAL,
				&pClassMgr->m_ClassDatas[i].m_pStaticObject);
		}
	}

	return LT_OK;
}


// Out-of-line copy of the SDK inline LTRotation::Init (ltrotation.h), emitted into this
// object because ObjectCreateStruct's constructor in CreateStaticObjects calls it.
// FUNCTION: LITHTECH 0x00402ed0 ?Init@LTRotation@@QAEXMMMM@Z
