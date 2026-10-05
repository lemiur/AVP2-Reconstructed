// d3d.ren sys/d3d/3d_ops (0x1000f160-0x1000f3a0): A (16-byte aligned functions) object of config/d3dren/objects_v2.csv.
// NearZ/ReallyCloseNearZ ConVars, ViewParams fog helper, d3d_CalcLightAdd.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/1000f160 (0x1000f160-0x1000f3a0): the NearZ / ReallyCloseNearZ console variables and the view helpers
// that follow them.  unit name evidence: d3d_CalcLightAdd is the first function of Jupiter's 3d_ops.cpp and the NEARZ define
// lives in 3d_ops.h, so this object is probably 3d_ops.cpp (the unit keeps its address name).
// FLAGS: /O2 /Ob2
#include <windows.h>
#include "ltbasedefs.h"
#include "de_objects.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"

// FUNCTION: D3DREN 0x1000f160 _$E2
// GLOBAL: D3DREN 0x10055cb0
ConVar g_CV_NearZ("NearZ", 7.0f);
// FUNCTION: D3DREN 0x1000f180 _$E5
// GLOBAL: D3DREN 0x10054890
ConVar g_CV_ReallyCloseNearZ("ReallyCloseNearZ", 0.01f);

// GLOBAL: D3DREN 0x100584f8
extern float g_fVFogValueRange;		// guess: derived from the VFog console variables by d3d_ReadExtraConsoleVariables (written elsewhere)
// GLOBAL: D3DREN 0x10058778
extern float g_fInvVFogHeightRange;		// guess: same

// guess: member of ViewParams (the Talon g_ViewParams, include/d3dren/viewparams.h): stores the viewer position and derives the
// vertical fog value and zone from its height; the height read is the global g_ViewParams.m_Pos.y, not an argument.
// (W5: matches with the position passed as an LTVector by value, as the call sites copy it, and the two fog scale factors multiplied in
// the exe's order.)
// FUNCTION: D3DREN 0x1000f1a0
void ViewParams::SetupFogViewPosition(LTVector vPos)
{
	float fDensity;
	int nZone;

	m_FogViewPos = vPos;

	if (g_ViewParams.m_Pos.y <= g_CV_VFogMinY.m_FloatVal)
		fDensity = g_CV_VFogMinYVal.m_FloatVal;
	else if (g_ViewParams.m_Pos.y >= g_CV_VFogMaxY.m_FloatVal)
		fDensity = g_CV_VFogMaxYVal.m_FloatVal;
	else
		fDensity = (g_ViewParams.m_Pos.y - g_CV_VFogMinY.m_FloatVal) * g_fVFogValueRange * g_fInvVFogHeightRange + g_CV_VFogMinYVal.m_FloatVal;
	m_fVFogViewDensity = fDensity;

	if (g_ViewParams.m_Pos.y >= g_CV_VFogMaxY.m_FloatVal)
		nZone = 1;
	else if (g_ViewParams.m_Pos.y <= g_CV_VFogMinY.m_FloatVal)
		nZone = 0;
	else
		nZone = 2;
	m_nVFogViewZone = nZone;
}

// GLOBAL: D3DREN 0x10056218
extern uint32 DAT_10056218;		// guess: g_nNumObjectDynamicLights (names_proposal low): lights touching the leaf
// GLOBAL: D3DREN 0x100566d0
extern DynamicLight *DAT_100566d0[];	// guess: g_ObjectDynamicLights (names_proposal low)

// NAME: d3d_CalcLightAdd: Jupiter 3d_ops.cpp d3d_CalcLightAdd (same body; Talon has no FLAG_ONLYLIGHTWORLD test and
// gates the loop on the LightModels mirror).
// FUNCTION: D3DREN 0x1000f270
void d3d_CalcLightAdd(LTObject *pObject, LTVector *pLightAdd)
{
	uint32 i;
	float distSquared, percent;
	DynamicLight *pLight;

	pLightAdd->Init();

	if (DAT_10048760)
	{
		for (i = 0; i < DAT_10056218; i++)
		{
			pLight = DAT_100566d0[i];

			distSquared = pLight->GetPos().DistSqr(pObject->GetPos());

			if (distSquared < (pLight->m_LightRadius * pLight->m_LightRadius))
			{
				percent = 1.0f - ((float)sqrt(distSquared) / pLight->m_LightRadius);
				percent *= 0.7f;

				pLightAdd->x += (float)((long)pLight->m_ColorR - (255 - (long)pLight->m_ColorR)) * percent;
				pLightAdd->y += (float)((long)pLight->m_ColorG - (255 - (long)pLight->m_ColorG)) * percent;
				pLightAdd->z += (float)((long)pLight->m_ColorB - (255 - (long)pLight->m_ColorB)) * percent;
			}
		}
	}
}
