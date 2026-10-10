// Jupiter runtime/shared/src/classbind.cpp
#include <stdlib.h>
#include <string.h>
#include "ltserverobj.h"
#include "classbind.h"

typedef ClassDef** (*ObjectDLLSetupFn)(int *nDefs, void *pServer, int *version);

// Empty in this build (identical-code folded at 0x004359b0).
void cb_ModuleLoaded(ClassBindModule *pModule);


// FUNCTION: LITHTECH 0x00402730
static void cb_VerifyClassDefProperties(ClassBindModule *pModule)
{
	int i, j;
	PropDef *pProp;

	for (i=0; i < pModule->m_nClassDefs; i++)
	{
		for (j=0; j < pModule->m_pClassDefs[i]->m_nProps; j++)
		{
			pProp = &pModule->m_pClassDefs[i]->m_Props[j];

			if (pProp->m_PropType >= NUM_PROPERTYTYPES || pProp->m_PropType < 0)
				pProp->m_PropType = NUM_PROPERTYTYPES;
		}
	}
}


// FUNCTION: LITHTECH 0x00402680
int cb_LoadModule(const char *pModuleName, void *pServer, ClassBindModule **ppModule, int *version)
{
	CBindModuleType *hModule;
	ObjectDLLSetupFn theFunction;
	ClassBindModule *pModule;

	if (!bm_BindModule(pModuleName, &hModule))
		return CB_CANTFINDMODULE;

	// Get the function.
	theFunction = (ObjectDLLSetupFn)bm_GetFunctionPointer(hModule, "ObjectDLLSetup");
	if (!theFunction)
	{
		bm_UnbindModule(hModule);
		return CB_NOTCLASSMODULE;
	}

	// Ok.. setup the classbindmodule.
	pModule = (ClassBindModule*)malloc(sizeof(ClassBindModule));
	pModule->m_hModule = hModule;
	pModule->m_pClassDefs = theFunction(&pModule->m_nClassDefs, pServer, version);

	if (*version != SERVEROBJ_VERSION)
	{
		free(pModule);
		bm_UnbindModule(hModule);
		return CB_VERSIONMISMATCH;
	}

	// Verify all properties!
	cb_VerifyClassDefProperties(pModule);
	cb_ModuleLoaded(pModule);

	*ppModule = pModule;
	return CB_NOERROR;
}


// FUNCTION: LITHTECH 0x004027a0
void cb_UnloadModule(ClassBindModule *pModule)
{
	bm_UnbindModule(pModule->m_hModule);
	free(pModule);
}


// FUNCTION: LITHTECH 0x004027c0
int cb_GetNumClassDefs(ClassBindModule *hModule)
{
	return hModule->m_nClassDefs;
}


// FUNCTION: LITHTECH 0x004027d0
ClassDef** cb_GetClassDefs(ClassBindModule *hModule)
{
	return hModule->m_pClassDefs;
}


// FUNCTION: LITHTECH 0x004027e0
ClassDef* cb_FindClass(ClassBindModule *pModule, const char *pClassName)
{
	int i;

	for (i=0; i < pModule->m_nClassDefs; i++)
		if (strcmp(pModule->m_pClassDefs[i]->m_ClassName, pClassName) == 0)
			return pModule->m_pClassDefs[i];

	return LTNULL;
}


// IsClassFlagSet
//
// Finds if class flags are set in this or any parent class...
// FUNCTION: LITHTECH 0x00402850
ClassDef* cb_IsClassFlagSet(ClassBindModule *hModule, ClassDef *pClass, const uint32 dwClassFlag)
{
	while (pClass)
	{
		if (pClass->m_ClassFlags & dwClassFlag)
			return pClass;

		pClass = pClass->m_ParentClass;
	}

	return LTNULL;
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x004359b0 (the empty callbacks).
void cb_ModuleLoaded(ClassBindModule *pModule)
{
}

