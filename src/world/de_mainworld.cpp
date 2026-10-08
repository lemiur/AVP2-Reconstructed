// Jupiter runtime/world/src/de_mainworld.cpp (plus Talon's world loading: world_shared_bsp.cpp,
// WorldBsp, TerrainSection, the light anims and the static lights).
// w_AddStaticLights uses STLport containers (the engine's copy in lithshared/stl).
// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "de_mainworld.h"
#include "de_memory.h"
#include "iltstream.h"
#include "ltserverobj.h"
#include "geomroutines.h"

#include "parse_world_info.h"
#include "dutil.h"

#define CURRENT_WORLD_VERSION	70

// Which object array of the tree nodes the static lights go in.
#define NOA_Lights		1

uint32 SelectLMPlaneVector(LTVector vNormal);								// 0x00445060

Node* w_NodeForIndex(Node *pList, uint32 listSize, int index);						// 0x00429d20
void w_SetupSurfacePolyLists(WorldBsp *pBsp);										// 0x00429d50
SurfaceEffect* w_FindSurfaceEffect(SurfaceEffect *pList, const char *pName);		// 0x00429db0
void w_SetPlaneTypes(Node *pNodes, uint32 nNodes, LTBOOL bUsePlaneTypes);			// 0x00429de0
void wb_Unknown49e330(void *p);											// 0x0049e330

void w_CalcBoundingSpheres(WorldBsp *pBsp);								// 0x0042bec0
float w_CalcBoundRadius(WorldPoly **pPolies, uint32 nPolies, LTVector *pCenter);	// 0x0042c850

#define WTObj_Light		1

// Load status (Jupiter loadstatus.h ELoadWorldStatus); MainWorld::Load returns these as LTRESULTs.
#define LoadWorld_Ok				0
#define LoadWorld_InvalidVersion	1
#define LoadWorld_InvalidFile		2
#define LoadWorld_InvalidParams		3
#define LoadWorld_Error				4

// What CServerMgr::LoadWorld passes to MainWorld::Load (same layout as servermgr.cpp's).
struct WorldLoadInfo
{
	ILTStream	*m_pStream;					// 0x00
	void		*m_pUser;					// 0x04
	void		(*m_ProgressFn)(uint32 param);	// 0x08 called after each world model
	uint32		m_ProgressParam;			// 0x0c
};

// The linker folded this empty callback with every other empty function (0x004359b0).
void w_NullProgressFn(uint32 param);

// A static light in the world tree (Jupiter StaticLight), 0xa0 bytes, vtable 0x004c70f4.
class StaticLight : public WorldTreeObj
{
public:
					StaticLight() : WorldTreeObj(WTObj_Light)
					{
						dl_TieOff(&m_Link);
						m_Link.m_pData = this;
					}

	virtual void	GetBBox(LTVector &vMin, LTVector &vMax)
	{
		vMin = m_Pos - LTVector(m_Radius, m_Radius, m_Radius);
		vMax = m_Pos + LTVector(m_Radius, m_Radius, m_Radius);
	}

	LTLink			m_Link;				// 0x5c in MainWorld::m_StaticLights
	LTVector		m_Pos;				// 0x68 Position of the light
	float			m_Radius;			// 0x74 Maximum radius of the light
	LTVector		m_Color;			// 0x78 0-255
	LTVector		m_Dir;				// 0x84 Normalized direction vector for directional lights
	float			m_FOV;				// 0x90 cos(fov/2)  -1 for omnidirectional lights
	LTVector		m_OuterColor;		// 0x94
};

LTRESULT w_LoadWorldBsp(WorldLoadInfo *pInfo, MainWorld *pWorld, LTBOOL bProgress,
	WorldBsp **ppBsp, uint32 *pLoadTicks, uint32 *pPrecalcTicks, LTBOOL bUsePlaneTypes);	// 0x00428ac0
void w_AddStaticLights(ILTStream *pStream, MainWorld *pWorld, CLightTable *pTable);			// 0x00429fa0
void w_InitLightTable(CLightTable *pTable, char *pInfoString, LTVector *pMin, LTVector *pMax, LTVector *pAmbient);	// 0x00429f10
void w_LightTableAddLight(LTVector *pPos, LTVector *pColor, float radius, CLightTable *pTable);	// 0x0042ab60

// Tracks how much memory is taken up for world geometry.
// GLOBAL: LITHTECH 0x004e3454
uint32 g_WorldGeometryMemory=0;


// -------------------------------------------------------------- //
// Header.
// -------------------------------------------------------------- //

// Read the file header.  Returns FALSE if the version is invalid.
// The next thing after the header is the world info string.
// FUNCTION: LITHTECH 0x00427cf0
LTBOOL w_ReadWorldHeader(ILTStream *pStream, uint32 &version, uint32 &objectDataPos, uint32 &renderDataPos)
{
	uint32 dummyNum;

	//read the version.
	STREAM_READ(version);

	//Check the version;
	if (version != CURRENT_WORLD_VERSION)
		return LTFALSE;

	//read the position of the object data.
	*pStream >> objectDataPos;

	//read the position of the rendering data (light anims).
	*pStream >> renderDataPos;

	//read 8 uint32's.
	*pStream >> dummyNum >> dummyNum >> dummyNum >> dummyNum;
	*pStream >> dummyNum >> dummyNum >> dummyNum >> dummyNum;

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00427db0
LTRESULT w_GetWorldInfoString(ILTStream *pStream, char *pInfoString, uint32 maxLen, uint32 *pActualLen)
{
	uint32 version, objectDataPos, renderDataPos;
	uint32 len;

	if (!w_ReadWorldHeader(pStream, version, objectDataPos, renderDataPos))
		return LT_INVALIDWORLDFILE;

	STREAM_READ(len);
	if (maxLen == 0)
	{
		pInfoString[0] = 0;
	}
	else
	{
		if ((len+1) > maxLen)
			len = maxLen - 1;

		pStream->Read(pInfoString, len);
		pInfoString[len] = 0;
	}

	if (pActualLen)
		*pActualLen = len;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00427e40
void w_TermSurfaceEffects(MainWorld *pWorld)
{
	SurfaceEffectInst *pCur, *pNext;

	pCur = pWorld->m_pSurfaceEffects;
	while (pCur)
	{
		pNext = pCur->m_pNext;
		pCur->m_pEffect->TermEffect(pCur->m_pData);
		dfree(pCur);
		pCur = pNext;
	}

	pWorld->m_pSurfaceEffects = LTNULL;
}


// FUNCTION: LITHTECH 0x00427e80
WorldData* w_FindWorldModel(MainWorld *pWorld, const char *pName)
{
	uint32 i;
	WorldData *pWorldData;

	for (i=0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		pWorldData = pWorld->m_WorldModels[i];
		if (stricmp(pWorldData->m_pOriginalBsp->m_WorldName, pName) == 0)
			return pWorldData;
	}

	return LTNULL;
}


// -------------------------------------------------------------- //
// Interface functions.
// -------------------------------------------------------------- //

// The out-of-line copy of the SDK's inline MatVMul (ltmatrix.h).
// FUNCTION: LITHTECH 0x00428120 ?MatVMul@@YAXPAV?$_CVector@M@@PAVLTMatrix@@0@Z

// The pDestPt local decides the term order of the inlined MatVMul's sums (without it the loads come out z, y, x).
// FUNCTION: LITHTECH 0x00427ed0
void w_TransformWorldModel(WorldModelInstance *pInst, LTMatrix *pMat, LTBOOL bPartial)
{
	uint32 i;
	LTVector planePt;
	WorldBsp *pSrc, *pDest;

	if (pInst->m_pOriginalBsp->IsUntransformed())
		return;

	pSrc = pInst->m_pOriginalBsp;
	pDest = pInst->m_pWorldBsp;
	if (!pSrc || !pDest)
		return;

	// Only do the root node center so DObject::GetCenter works.
	if (bPartial)
	{
		return;
	}

	// Transform the points.
	for (i=0; i < pDest->m_nPoints; i++)
	{
		LTVector *pDestPt = &pDest->m_Points[i];
		MatVMul(pDestPt, pMat, &pSrc->m_Points[i]);
	}

	// Transform the planes!
	for (i=0; i < pDest->m_nPlanes; i++)
	{
		planePt = pSrc->m_Planes[i].m_Normal * pSrc->m_Planes[i].m_Dist;
		MatVMul_InPlace(pMat, &planePt);

		MatVMul_3x3(&pDest->m_Planes[i].m_Normal, pMat, &pSrc->m_Planes[i].m_Normal);
		pDest->m_Planes[i].m_Dist = pDest->m_Planes[i].m_Normal.Dot(planePt);
	}

	// Transform the polies...
	for (i=0; i < pDest->m_nPolies; i++)
	{
		MatVMul(&pDest->m_Polies[i]->m_Center, pMat, &pSrc->m_Polies[i]->m_Center);
	}
}


// FUNCTION: LITHTECH 0x00428180
BspPortal* w_FindBspPortal(WorldBsp *pBsp, const char *pName, uint32 *pIndex)
{
	uint32 i;

	for (i=0; i < pBsp->m_nPortals; i++)
	{
		if (strcmp(pBsp->m_Portals[i].m_pName, pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			return &pBsp->m_Portals[i];
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00428200
BspPortal* w_FindPortal(MainWorld *pWorld, const char *pName, uint32 *pWorldIndex, uint32 *pPortalIndex)
{
	uint32 i;
	BspPortal *pPortal;

	for (i=0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		pPortal = w_FindBspPortal(pWorld->m_WorldModels[i]->m_pOriginalBsp, pName, pPortalIndex);
		if (pPortal)
		{
			if (pWorldIndex)
				*pWorldIndex = i;

			return pPortal;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00428260
MainWorldNamedEntry* w_FindNamedEntry(MainWorld *pWorld, const char *pName, uint32 *pIndex)
{
	uint32 i;

	for (i=0; i < pWorld->m_nNamedEntries; i++)
	{
		if (stricmp(pWorld->m_NamedEntries[i].m_Name, pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			return &pWorld->m_NamedEntries[i];
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x004282d0
LTBOOL w_MakeSpecialName(const char *pName, int id, char *pBuf, uint32 bufLen)
{
	if (bufLen < 8)
		return LTFALSE;

	if (strlen(pName) > (bufLen - 8))
		return LTFALSE;

	sprintf(pBuf, "%s%d %s", "#$#", id, pName);
	return LTTRUE;
}


// Not inline in Talon: BaseNew<LightAnim> (clientmgr 0x00414880) and the CMoArray<LightAnim> copies call it.
// FUNCTION: LITHTECH 0x00428320 ??0LightAnim@@QAE@XZ
LightAnim::LightAnim()
{
	m_vLightPos.Init();
	m_vLightColor.Init();
	m_fLightRadius = 0.0f;
	m_Name[0] = 0;
	m_bShadowMap = LTFALSE;
	m_pFrames = LTNULL;
	m_nFrames = 0;
	m_pPolyRefs = LTNULL;
	m_nPolies = 0;
	m_iFrames[0] = m_iFrames[1] = 0xFFFFFFFF;
	m_PercentBetween = 0;
	m_fBlendPercent = 1.0f;
}


// ----------------------------------------------------------------------------- //
// WorldData.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00428360
WorldData::WorldData()
{
	Clear();
}

// FUNCTION: LITHTECH 0x00428370
WorldData::~WorldData()
{
	Term();
}

// FUNCTION: LITHTECH 0x00428380
void WorldData::Clear()
{
	m_Flags = 0;
	m_pOriginalBsp = NULL;
	m_pWorldBsp = NULL;
	m_pValidBsp = NULL;
}

// FUNCTION: LITHTECH 0x00428390
void WorldData::Term()
{
	if (m_Flags & WD_ORIGINALBSPALLOCED && m_pOriginalBsp)
		delete m_pOriginalBsp;

	if (m_Flags & WD_WORLDBSPALLOCED && m_pWorldBsp)
		delete m_pWorldBsp;

	Clear();
}


// ----------------------------------------------------------------------------- //
// MainWorld.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004283e0
MainWorld::MainWorld()
{
	Clear();
}


// FUNCTION: LITHTECH 0x004284f0
void MainWorld::Clear()
{
	m_BoxMin.Init();
	m_BoxMax.Init();
	dl_TieOff(&m_StaticLights);
	m_pSurfaceEffects = LTNULL;
	m_LMGridSize = 0.0f;

	m_ExtentsMin.Init();
	m_ExtentsMax.Init();
	m_ExtentsDiffInv.Init();

	m_WorldFlags = 0;
	m_pWorldStream = LTNULL;
	m_NamedEntries = LTNULL;
	m_nNamedEntries = 0;
	m_pWorldInfoString = LTNULL;
	dl_TieOff(&m_Link1BC);
	m_Unknown1C8 = 0;
	m_bLoaded = LTFALSE;
	dl_TieOff(&m_InheritedWorlds);
	dl_TieOff(&m_InheritLink);
	m_bInherited = LTFALSE;

	m_LightTable.Reset();
}


// Jupiter's WorldData accessors (OriginalBSP() twice, SetValidBsp) are the three inline sites after SetSizeInit2 that
// keep its _DeleteAndDestroyArray out of line; the for loop gives the original's unsigned `jbe` head test.
// FUNCTION: LITHTECH 0x004285c0
LTRESULT MainWorld::Load(WorldLoadInfo *pInfo)
{
	ILTStream *pStream;
	uint32 i, len, version, objectDataPos;
	uint32 nWorldModels, nextPos, startPos;
	uint32 loadTicks, precalcTicks;
	LTRESULT dResult;
	WorldData *pWorldModel;
	LTVector ambientLight;

	if (!pInfo->m_pStream)
		return LoadWorld_InvalidParams;

	Term();

	pStream = pInfo->m_pStream;
	if (!pInfo->m_ProgressFn)
		pInfo->m_ProgressFn = w_NullProgressFn;

	if (!w_ReadWorldHeader(pStream, version, objectDataPos, m_RenderDataPos))
		return LoadWorld_InvalidVersion;

	loadTicks = 0;
	m_FileVersion = version;
	precalcTicks = 0;

	// Read the world info string.
	STREAM_READ(len);
	m_pWorldInfoString = (char*)dalloc(len + 1);
	if (!m_pWorldInfoString)
	{
		Term();
		return LoadWorld_Error;
	}

	pStream->Read(m_pWorldInfoString, len);
	m_pWorldInfoString[len] = 0;

	STREAM_READ(m_LMGridSize);

	// Read the extents of the world.
	*pStream >> m_ExtentsMin >> m_ExtentsMax;

	m_BoxMin = m_ExtentsMin - LTVector(100.0f, 100.0f, 100.0f);
	m_BoxMax = m_ExtentsMax + LTVector(100.0f, 100.0f, 100.0f);

	m_ExtentsDiffInv.x = 1.0f / (m_ExtentsMax.x - m_ExtentsMin.x);
	m_ExtentsDiffInv.y = 1.0f / (m_ExtentsMax.y - m_ExtentsMin.y);
	m_ExtentsDiffInv.z = 1.0f / (m_ExtentsMax.z - m_ExtentsMin.z);

	// Read in the world tree.
	if (!m_WorldTree.LoadLayout(pStream))
	{
		Term();
		return LoadWorld_Error;
	}

	// Read the world models.
	STREAM_READ(nWorldModels);
	if (!m_WorldModels.SetSizeInit2(nWorldModels, LTNULL))
	{
		Term();
		return LoadWorld_Error;
	}

	for (i=0; i < nWorldModels; i++)
	{
		pWorldModel = m_WorldModels[i] = new WorldData;
		if (!pWorldModel)
		{
			Term();
			return LoadWorld_Error;
		}

		STREAM_READ(nextPos);
		pStream->GetPos(&startPos);

		dResult = w_LoadWorldBsp(pInfo, this, LTFALSE, &pWorldModel->m_pOriginalBsp, &loadTicks, &precalcTicks, LTTRUE);
		if (dResult != LoadWorld_Ok)
		{
			Term();
			return dResult;
		}

		// Moving world models get a second copy to transform.
		if (pWorldModel->OriginalBSP()->m_WorldInfoFlags & WIF_MOVEABLE)
		{
			pStream->SeekTo(startPos);
			dResult = w_LoadWorldBsp(pInfo, this, LTFALSE, &pWorldModel->m_pWorldBsp, &loadTicks, &precalcTicks, LTFALSE);
			if (dResult != LoadWorld_Ok)
			{
				Term();
				return dResult;
			}
		}

		if (pWorldModel->m_pOriginalBsp->WBSlot9())
			m_WorldFlags |= WORLD_HASVISBSP;

		pWorldModel->m_Flags |= WD_ORIGINALBSPALLOCED | WD_WORLDBSPALLOCED;
		pWorldModel->SetValidBsp();

		pWorldModel->OriginalBSP()->m_Index = (uint16)i;
		if (pWorldModel->m_pWorldBsp)
			pWorldModel->m_pWorldBsp->m_Index = (uint16)i;

		pInfo->m_ProgressFn(pInfo->m_ProgressParam);
		pStream->SeekTo(nextPos);
	}

	// Precalculate stuff.
	ParseAmbientLight(m_pWorldInfoString, &ambientLight);

	if (!SetupLeafPolies())
	{
		Term();
		return LoadWorld_InvalidFile;
	}

	CalcBoundingSpheres();

	if (!SetupSkyPolies())
	{
		Term();
		return LoadWorld_Error;
	}

	w_InitLightTable(&m_LightTable, m_pWorldInfoString, &m_ExtentsMin, &m_ExtentsMax, &ambientLight);
	w_AddStaticLights(pStream, this, &m_LightTable);

	if (g_DebugLevel > 1)
	{
		dsi_ConsolePrint("World load: %d ticks, precalculate: %d ticks", loadTicks, precalcTicks);
	}

	pInfo->m_ProgressFn(pInfo->m_ProgressParam);

	if (pStream->ErrorStatus() != LT_OK)
	{
		Term();
		return LoadWorld_InvalidFile;
	}

	m_bLoaded = LTTRUE;
	return LoadWorld_Ok;
}


// Calls the progress function between the sections of a world model.
inline void w_LoadProgress(WorldLoadInfo *pInfo, LTBOOL bProgress)
{
	if (bProgress)
		pInfo->m_ProgressFn(pInfo->m_ProgressParam);
}

// Loads one world model's BSP (Jupiter WorldBsp::Load). See out/loader/spec_talon_dat70.md for
// the format. bProgress calls pInfo's progress function between the sections (arguments 5 and 6 are unused).
// Wave 6 phase 2 rewrote it from the exe's call sequence (1332 -> 296 aligned mismatches, 125 ignoring stack
// offsets): 12-byte STREAM_READs of the vectors, the 4-byte poly size alignment, dead colour reads into locals,
// the error block inside `if (ErrorStatus() != LT_OK)`, an `int j` for the node sides, a pointer local for the
// light anim refs, the w_LoadProgress inline (its 11 sites plus the 3 `>>` of a point keep m_PolyAnimRefs.SetSize's
// _DeleteAndDestroyArray out of line, as in the exe). Wave 7: `(v - O).Dot(P)` (the exe sums the terms z, y, x)
// and the PBlockTable failure jumping to the error block's return (the exe tail-merges it: `goto Invalid`; the
// size now matches) took it to 280 (105 ignoring stack offsets). Left: the exe's frame is 4 bytes bigger: it packs
// the sub-dword locals into one more slot (bHasEffect, colorB and colorG each alone; iLeaf+colorR, lmWidth+lmHeight
// paired; ours pairs colorG+colorB, iLeaf+lmWidth, colorR+lmHeight). Names and declaration order don't change the
// layout (/FAs listings; toys show the order follows first use); colorG as uint16 fixes most offsets but reads 2
// bytes. Also left: the Surface::m_Flags / WorldPoly::m_Flags stores (the exe re-reads memory: xor/and/xor; bitfield
// views and explicit xor forms compile to our CSE'd code), the x87 order of the first Dot, register choices around
// the progress calls and the terrain section memory sum.
// Wave 7 phase 2: the two flag merges are inline setters with a parameter: Surface::SetFlags(uint32) and
// WorldPoly::SetLMPlaneVector(uint16), each `m_Flags &= ~M; m_Flags |= (v & M);` (written in the caller, the same
// statements compile to and/or; through a parameter VC6 emits the exe's xor-on-memory merge with two reads).
// 280 (105) -> 257 (86 ignoring stack offsets); both sites now match. Audit: `calls 2` are ICF names
// (_DeleteAndDestroyArray<uint32>/BaseNew<uint32> folded with identical instances), `imm` the frame offsets: no
// behaviour difference. Budget model: every site's decision agrees with the exe once ICF names are
// accounted for (its "operator new 0/0/3" row is a naming artefact). Left: the sub-dword local packing above
// (frame 4 bytes smaller), the x87 order of the first Dot, register choices around the progress calls.
// PARKED: stack slot packing of the sub-dword locals and register choice (86 aligned ignoring stack offsets); behaviour identical
// STUB: LITHTECH 0x00428ac0
LTRESULT w_LoadWorldBsp(WorldLoadInfo *pInfo, MainWorld *pWorld, LTBOOL bProgress,
	WorldBsp **ppBsp, uint32 *pLoadTicks, uint32 *pPrecalcTicks, LTBOOL bUsePlaneTypes)
{
	ILTStream *pStream;
	WorldBsp *pBsp;
	WorldPoly *pPoly;
	Surface *pSurface;
	Leaf *pLeaf;
	LeafList *pCurList;
	Node *pNode;
	BspPortal *pPortal;
	SPolyVertex *pVert;
	SurfaceEffect *pEffect;
	SurfaceEffectInst *pInst;
	SurfaceData surfaceData;
	ConParse parse;
	uint32 i, k, dummy, polyDataSize, polyPos, iCurLeafPoly, iCurListData, iCurAnimRef;
	uint32 nPoints, nPlanes, nSurfaces, nPolies, nLeafs, nVerts, visListSize, nPolyAnimRefs;
	uint32 textureNameLen, curNamePos, startPos, surfaceIndex, planeIndex, nSections, surfaceFlags;
	uint32 nLeafPolies, polyIndex, portalDummy;
	uint8 nBaseVerts, nExtraVerts, bHasEffect, colorR, colorG, colorB;
	uint16 tempWord, lmWidth, lmHeight, nAnimRefs, pointIndex, iLeaf;
	int32 sideIndex;
	int j;
	void *pEffectData;
	uint32 *pAnimRef;
	LTVector O, P, Q;
	char effectName[128], effectParam[256], portalName[128];

	pStream = pInfo->m_pStream;
	*ppBsp = LTNULL;

	pBsp = new WorldBsp;

	// Header.
	STREAM_READ(dummy);
	STREAM_READ(dummy);
	STREAM_READ(dummy);
	STREAM_READ(dummy);
	STREAM_READ(dummy);
	STREAM_READ(dummy);
	STREAM_READ(dummy);
	STREAM_READ(dummy);

	STREAM_READ(pBsp->m_WorldInfoFlags);
	STREAM_READ(pBsp->m_MaxTreeDepth);
	pStream->ReadString(pBsp->m_WorldName, 0x40);

	STREAM_READ(nPoints);
	STREAM_READ(nPlanes);
	STREAM_READ(nSurfaces);
	STREAM_READ(pBsp->m_nPortals);
	STREAM_READ(nPolies);
	STREAM_READ(nLeafs);
	STREAM_READ(nVerts);
	STREAM_READ(visListSize);
	STREAM_READ(pBsp->m_nLeafLists);
	STREAM_READ(pBsp->m_nNodes);
	STREAM_READ(pBsp->m_nLeafPolies);
	STREAM_READ(nPolyAnimRefs);

	STREAM_READ(pBsp->m_MinBox);
	STREAM_READ(pBsp->m_MaxBox);
	STREAM_READ(pBsp->m_WorldTranslation);

	w_LoadProgress(pInfo, bProgress);

	// Texture names.
	STREAM_READ(textureNameLen);
	STREAM_READ(pBsp->m_nTextures);
	pBsp->m_TextureNameData = (char*)dalloc_z(textureNameLen);
	pBsp->m_TextureNames = (char**)dalloc_z(pBsp->m_nTextures * sizeof(char*));

	curNamePos = 0;
	for (i=0; i < pBsp->m_nTextures; i++)
	{
		pBsp->m_TextureNames[i] = &pBsp->m_TextureNameData[curNamePos];
		do
		{
			pStream->Read(&pBsp->m_TextureNameData[curNamePos], 1);
			curNamePos++;
		}
		while (pBsp->m_TextureNameData[curNamePos-1] != 0);
	}

	w_LoadProgress(pInfo, bProgress);

	// Size the poly data.
	polyDataSize = 0;
	pStream->GetPos(&startPos);
	for (i=0; i < nPolies; i++)
	{
		STREAM_READ(nBaseVerts);
		STREAM_READ(nExtraVerts);
		polyDataSize += sizeof(WorldPoly) + ((uint32)nBaseVerts + nExtraVerts) * sizeof(SPolyVertex);
		if (polyDataSize & 3)
			polyDataSize += 4 - (polyDataSize & 3);
	}
	pStream->SeekTo(startPos);

	// Allocate everything.
	pBsp->m_PolyData = (char*)dalloc_z(polyDataSize);
	pBsp->m_PolyDataSize = polyDataSize;
	pBsp->m_Points = (LTVector*)dalloc_z(nPoints * sizeof(LTVector));
	pBsp->m_Nodes = new Node[pBsp->m_nNodes];
	pBsp->m_Polies = (WorldPoly**)dalloc_z(nPolies * sizeof(WorldPoly*));
	pBsp->m_Leafs = (Leaf*)dalloc_z(nLeafs * sizeof(Leaf));
	pBsp->m_Planes = (LTPlane*)dalloc_z(nPlanes * sizeof(LTPlane));
	pBsp->m_Surfaces = new Surface[nSurfaces];
	pBsp->m_LeafListData = (uint8*)dalloc_z(visListSize);
	pBsp->m_LeafPolies = (WorldPoly**)dalloc_z(pBsp->m_nLeafPolies * sizeof(WorldPoly*));
	pBsp->m_LeafLists = (LeafList*)dalloc_z(pBsp->m_nLeafLists * sizeof(LeafList));
	pBsp->m_Portals = (BspPortal*)dalloc_z(pBsp->m_nPortals * sizeof(BspPortal));
	if (!pBsp->m_PolyAnimRefs.SetSize(nPolyAnimRefs))
		goto Error;

	pBsp->m_MemoryUse = sizeof(WorldBsp) + polyDataSize +
		nPoints * sizeof(LTVector) + pBsp->m_nNodes * sizeof(Node) + nPolies * sizeof(WorldPoly*) +
		nLeafs * sizeof(Leaf) + nPlanes * sizeof(LTPlane) + nSurfaces * sizeof(Surface) +
		pBsp->m_nLeafPolies * sizeof(WorldPoly*) + pBsp->m_nLeafLists * sizeof(LeafList) +
		pBsp->m_nTextures * sizeof(char*) + pBsp->m_PolyAnimRefs.GetSize() * sizeof(uint32) +
		textureNameLen + visListSize;

	pBsp->m_nPoints = nPoints;
	pBsp->m_nPolies = nPolies;
	pBsp->m_nLeafs = nLeafs;
	pBsp->m_nPlanes = nPlanes;
	pBsp->m_nSurfaces = nSurfaces;
	pBsp->m_LeafListDataSize = visListSize;

	w_LoadProgress(pInfo, bProgress);

	// Set up the polies.
	polyPos = 0;
	for (i=0; i < nPolies; i++)
	{
		STREAM_READ(nBaseVerts);
		STREAM_READ(nExtraVerts);

		pPoly = (WorldPoly*)&pBsp->m_PolyData[polyPos];
		pPoly->m_Index = (uint16)i;
		pBsp->m_Polies[i] = pPoly;
		pPoly->m_nVertices = nBaseVerts;
		pPoly->m_nExtraVertices = nExtraVerts;
		polyPos += sizeof(WorldPoly) + ((uint32)nBaseVerts + nExtraVerts) * sizeof(SPolyVertex);
		if (polyPos & 3)
			polyPos += 4 - (polyPos & 3);
	}

	// Leaves.
	pCurList = pBsp->m_LeafLists;
	iCurLeafPoly = 0;
	iCurListData = 0;
	for (i=0; i < nLeafs; i++)
	{
		pLeaf = &pBsp->m_Leafs[i];
		dl_TieOff(&pLeaf->m_LeafLinks);
		pLeaf->m_LeafLists = pCurList;

		STREAM_READ(tempWord);
		if (tempWord == 0xFFFF)
		{
			// This leaf uses the lists from another leaf.
			STREAM_READ(tempWord);
			pLeaf->m_nLeafLists = pBsp->m_Leafs[tempWord].m_nLeafLists;
			pLeaf->m_LeafLists = pBsp->m_Leafs[tempWord].m_LeafLists;
		}
		else
		{
			pLeaf->m_nLeafLists = tempWord;
			for (k=0; k < pLeaf->m_nLeafLists; k++)
			{
				STREAM_READ(tempWord);
				pCurList->m_PortalID = tempWord;
				STREAM_READ(tempWord);
				pCurList->m_ListSize = tempWord;
				pCurList->m_pList = &pBsp->m_LeafListData[iCurListData];
				pStream->Read(pCurList->m_pList, pCurList->m_ListSize);
				iCurListData += pCurList->m_ListSize;
				pCurList++;
			}
		}

		STREAM_READ(nLeafPolies);
		pLeaf->m_nPolies = nLeafPolies;
		pLeaf->m_Polies = &pBsp->m_LeafPolies[iCurLeafPoly];
		for (k=0; k < nLeafPolies; k++)
		{
			pStream->Read(&pLeaf->m_Polies[k], 2);
			pStream->Read((uint8*)&pLeaf->m_Polies[k] + 2, 2);
		}

		STREAM_READ(pLeaf->m_Unknown28);
		iCurLeafPoly += nLeafPolies;
	}

	w_LoadProgress(pInfo, bProgress);

	// Planes.
	for (i=0; i < nPlanes; i++)
	{
		STREAM_READ(pBsp->m_Planes[i].m_Normal);
		STREAM_READ(pBsp->m_Planes[i].m_Dist);
	}

	w_LoadProgress(pInfo, bProgress);

	// Surfaces.
	for (i=0; i < nSurfaces; i++)
	{
		pSurface = &pBsp->m_Surfaces[i];

		STREAM_READ(pSurface->O);
		STREAM_READ(pSurface->P);
		STREAM_READ(pSurface->Q);
		STREAM_READ(pSurface->m_Unknown36);
		STREAM_READ(surfaceFlags);
		pSurface->SetFlags(surfaceFlags);
		STREAM_READ(pSurface->m_Unknown3C);
		STREAM_READ(colorR);
		STREAM_READ(colorG);
		STREAM_READ(colorB);
		STREAM_READ(bHasEffect);

		if (bHasEffect == 1)
		{
			pStream->ReadString(effectName, sizeof(effectName)-1);
			pStream->ReadString(effectParam, sizeof(effectParam)-1);

			pEffect = w_FindSurfaceEffect((SurfaceEffect*)pInfo->m_pUser, effectName);
			if (pEffect)
			{
				parse.Init(effectParam);
				parse.Parse();

				surfaceData.O = pSurface->O;
				surfaceData.P = pSurface->P;
				surfaceData.Q = pSurface->Q;
				surfaceData.m_pInternalWorld = pWorld;
				surfaceData.m_pInternalWorldBsp = pBsp;
				surfaceData.m_pInternalSurface = pSurface;

				pEffectData = pEffect->InitEffect(&surfaceData, parse.m_nArgs, parse.m_Args);
				if (pEffectData)
				{
					pInst = (SurfaceEffectInst*)dalloc_z(sizeof(SurfaceEffectInst));
					pInst->m_pEffect = pEffect;
					pInst->m_pSurface = pSurface;
					pInst->m_pBsp = pBsp;
					pInst->m_pNext = pWorld->m_pSurfaceEffects;
					pWorld->m_pSurfaceEffects = pInst;
					pInst->m_pData = pEffectData;
				}
			}
		}

		STREAM_READ(pSurface->m_TextureFlags);
	}

	w_LoadProgress(pInfo, bProgress);

	// Points.
	for (i=0; i < nPoints; i++)
	{
		*pStream >> pBsp->m_Points[i].x >> pBsp->m_Points[i].y >> pBsp->m_Points[i].z;
	}

	// Poly data.
	iCurAnimRef = 0;
	for (i=0; i < nPolies; i++)
	{
		pPoly = pBsp->m_Polies[i];

		*pStream >> pPoly->m_Unknown38;
		STREAM_READ(lmWidth);
		STREAM_READ(lmHeight);
		pPoly->m_LMWidth = (uint8)lmWidth;
		pPoly->m_LMHeight = (uint8)lmHeight;

		STREAM_READ(nAnimRefs);
		pPoly->m_nLMAnimRefs = nAnimRefs;
		pPoly->m_pLMAnimRefs = &pBsp->m_PolyAnimRefs.GetArray()[iCurAnimRef];
		iCurAnimRef += pPoly->m_nLMAnimRefs;
		if (iCurAnimRef > pBsp->m_PolyAnimRefs.GetSize())
			goto Error;

		for (k=0; k < pPoly->m_nLMAnimRefs; k++)
		{
			pAnimRef = &pPoly->m_pLMAnimRefs[k];
			pStream->Read(pAnimRef, 2);
			pStream->Read((uint8*)pAnimRef + 2, 2);
		}

		STREAM_READ(surfaceIndex);
		if (surfaceIndex >= pBsp->m_nSurfaces)
			goto Error;
		pPoly->m_pSurface = &pBsp->m_Surfaces[surfaceIndex];

		STREAM_READ(planeIndex);
		if (planeIndex >= pBsp->m_nPlanes)
			goto Error;
		pPoly->m_pPlane = &pBsp->m_Planes[planeIndex];
		pPoly->SetLMPlaneVector(SelectLMPlaneVector(pPoly->m_pPlane->m_Normal));

		STREAM_READ(O);
		STREAM_READ(P);
		STREAM_READ(Q);

		pVert = (SPolyVertex*)(pPoly + 1);
		for (k=0; k < (uint32)pPoly->m_nVertices + pPoly->m_nExtraVertices; k++)
		{
			STREAM_READ(pointIndex);
			if (pointIndex >= pBsp->m_nPoints)
				goto Error;

			pVert[k].m_Vec = &pBsp->m_Points[pointIndex];
			STREAM_READ(pVert[k].m_Color[2]);
			STREAM_READ(pVert[k].m_Color[1]);
			STREAM_READ(pVert[k].m_Color[0]);
			pVert[k].m_Color[3] = 0xFF;

			pVert[k].m_U = P.Dot(*pVert[k].m_Vec - O);
			pVert[k].m_V = (*pVert[k].m_Vec - O).Dot(Q);
		}

		// Render the second ring if there is one.
		if (pPoly->m_nExtraVertices == 0)
		{
			pPoly->m_nExtraVertices = pPoly->m_nVertices;
			pPoly->m_pVertices = pVert;
		}
		else
		{
			pPoly->m_pVertices = &pVert[pPoly->m_nVertices];
		}
	}

	w_LoadProgress(pInfo, bProgress);

	// Nodes.
	for (i=0; i < pBsp->m_nNodes; i++)
	{
		pNode = &pBsp->m_Nodes[i];
		pNode->m_Flags = 8;

		STREAM_READ(polyIndex);
		if (polyIndex >= pBsp->m_nPolies)
			goto Error;
		pNode->m_pPoly = pBsp->m_Polies[polyIndex];

		STREAM_READ(iLeaf);
		pNode->m_iLeaf = iLeaf;

		for (j=0; j < 2; j++)
		{
			STREAM_READ(sideIndex);
			pNode->m_Sides[j] = w_NodeForIndex(pBsp->m_Nodes, pBsp->m_nNodes, sideIndex);
			if (!pNode->m_Sides[j])
				goto Error;
		}
	}

	w_SetPlaneTypes(pBsp->m_Nodes, pBsp->m_nNodes, bUsePlaneTypes);

	w_LoadProgress(pInfo, bProgress);

	// Portals.
	for (i=0; i < pBsp->m_nPortals; i++)
	{
		pPortal = &pBsp->m_Portals[i];
		pPortal->m_Index = (uint16)i;

		pStream->ReadString(portalName, sizeof(portalName)-1);
		pPortal->m_pName = (char*)dalloc(strlen(portalName) + 1);
		strcpy(pPortal->m_pName, portalName);

		STREAM_READ(portalDummy);
		STREAM_READ(portalDummy);
		STREAM_READ(pPortal->m_Unknown04);
		STREAM_READ(pPortal->m_Center);
		STREAM_READ(pPortal->m_Dims);
	}

	w_LoadProgress(pInfo, bProgress);

	if (!pBsp->m_PBlockTable.Load(pStream))
		goto Invalid;

	pBsp->m_MemoryUse += pBsp->m_PBlockTable.m_nBlocks * sizeof(PBlock);

	w_LoadProgress(pInfo, bProgress);

	STREAM_READ(sideIndex);
	pBsp->m_RootNode = w_NodeForIndex(pBsp->m_Nodes, pBsp->m_nNodes, sideIndex);
	if (!pBsp->m_RootNode)
		goto Error;

	// Terrain sections.
	STREAM_READ(nSections);
	if (!pBsp->m_TerrainSections.SetSize(nSections))
		goto Error;

	for (i=0; i < nSections; i++)
	{
		if (!pBsp->m_TerrainSections[i].Load(pBsp, pStream, i))
			goto Error;

		pBsp->m_MemoryUse += (pBsp->m_TerrainSections[i].m_Polies.GetSize() +
			(pBsp->m_TerrainSections[i].m_Nodes.GetSize() * 3 + pBsp->m_TerrainSections[i].m_PBlockTable.m_nBlocks) * 2) * 4;
	}

	w_SetupSurfacePolyLists(pBsp);

	if (pStream->ErrorStatus() != LT_OK)
	{
Error:;
		delete pBsp;
Invalid:;
		return LoadWorld_InvalidFile;
	}

	w_LoadProgress(pInfo, bProgress);

	pBsp->CalcBoundRadius();
	g_WorldGeometryMemory += pBsp->m_MemoryUse;
	*ppBsp = pBsp;
	return LoadWorld_Ok;
}


// -------------------------------------------------------------- //
// Helpers.
// -------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00429d20
Node* w_NodeForIndex(Node *pList, uint32 listSize, int index)
{
	if (index == -1)
	{
		return NODE_IN;
	}
	else if (index == -2)
	{
		return NODE_OUT;
	}
	else if (index >= (int)listSize)
	{
		return NULL;
	}
	else
	{
		return &pList[index];
	}
}


// Links each surface's polies together (Surface::m_iFirstPoly, WorldPoly::m_iNextSurfacePoly).
// FUNCTION: LITHTECH 0x00429d50
void w_SetupSurfacePolyLists(WorldBsp *pBsp)
{
	uint32 i;
	WorldPoly *pPoly;
	Surface *pSurface;

	for (i=0; i < pBsp->m_nSurfaces; i++)
	{
		pBsp->m_Surfaces[i].m_iFirstPoly = 0xFFFF;
	}

	for (i=0; i < pBsp->m_nPolies; i++)
	{
		pPoly = pBsp->m_Polies[i];
		pSurface = (Surface*)pPoly->m_pSurface;
		if (pSurface)
		{
			pPoly->m_iNextSurfacePoly = pSurface->m_iFirstPoly;
			pSurface->m_iFirstPoly = (uint16)i;
		}
	}
}


// FUNCTION: LITHTECH 0x00429db0
SurfaceEffect* w_FindSurfaceEffect(SurfaceEffect *pList, const char *pName)
{
	SurfaceEffect *pCur;

	for (pCur=pList; pCur; pCur=pCur->m_pNext)
	{
		if (du_UpperStrcmp(pCur->m_Name, pName))
			return pCur;
	}

	return LTNULL;
}


#define PLANE_EP 			0.99999f

// FUNCTION: LITHTECH 0x00429de0
void w_SetPlaneTypes(Node *pNodes, uint32 nNodes, LTBOOL bUsePlaneTypes)
{
	uint32 i;
	Node *pNode;

	for (i=0; i < nNodes; i++)
	{
		pNode = &pNodes[i];

		pNode->m_PlaneType = PLANE_GENERIC;
		if (bUsePlaneTypes)
		{
			if (pNode->GetPlane()->m_Normal.x > PLANE_EP)
				pNode->m_PlaneType = PLANE_POSX;
			else if (pNode->GetPlane()->m_Normal.x < -PLANE_EP)
				pNode->m_PlaneType = PLANE_NEGX;
			else if (pNode->GetPlane()->m_Normal.y > PLANE_EP)
				pNode->m_PlaneType = PLANE_POSY;
			else if (pNode->GetPlane()->m_Normal.y < -PLANE_EP)
				pNode->m_PlaneType = PLANE_NEGY;
			else if (pNode->GetPlane()->m_Normal.z > PLANE_EP)
				pNode->m_PlaneType = PLANE_POSZ;
			else if (pNode->GetPlane()->m_Normal.z < -PLANE_EP)
				pNode->m_PlaneType = PLANE_NEGZ;
		}
	}
}


// Creates the light table and fills it with the ambient light.
// FUNCTION: LITHTECH 0x00429f10
void w_InitLightTable(CLightTable *pTable, char *pInfoString, LTVector *pMin, LTVector *pMax, LTVector *pAmbient)
{
	uint32 i;
	LTRGB color;

	color.r = (uint8)pAmbient->x;
	color.g = (uint8)pAmbient->y;
	color.b = (uint8)pAmbient->z;

	pTable->InitLightTable(pMin, pMax, ParseLightTableRes(pInfoString));

	pTable->m_pData = (LTRGB*)dalloc(pTable->m_nData * sizeof(LTRGB));
	if (pTable->m_pData)
	{
		for (i=0; i < pTable->m_nData; i++)
		{
			pTable->m_pData[i] = color;
		}

		g_WorldGeometryMemory += pTable->m_nData;
	}
}


// FUNCTION: LITHTECH 0x0042aab0 ?GetBBox@StaticLight@@UAEXAAV?$_CVector@M@@0@Z
// FUNCTION: LITHTECH 0x0042ab40 ??_GStaticLight@@UAEPAXI@Z


// Goes thru the light objects in the world file and adds the static lights. Lights that
// light objects either go into the light table ("FastLightObjects") or become StaticLights in
// the world tree. Lights listed in a LightGroup object ("Object..." properties) are skipped.
// Wave 6 phase 2: `bool bAddLight` (the exe tests a byte), StaticLight's link set up with dl_TieOff + m_pData
// and propFlags declared in the property loops took it from 254 to 97 aligned mismatches (43 -> 21 ignoring
// stack offsets). Left: stack slot assignment (most offsets differ), the std::find loop keeping end() in a
// register, and register choices in the inlined __node_alloc deallocation at the end. Scoping objectDataLen /
// objectPos / propType / propDataLen into the loops made it worse.
// Wave 7 phase 2: audit: `calls 2` are ICF names (the exe's _STL_alloc_proxy ctor and ~CHashBin are folded with
// identical functions) and `data` is StaticLight's vtable (0x4c70f4, unnamed in the exe): no behaviour difference.
// Budget model: every STLport callee cost is unknown to the tool (STL call expressions don't
// compile in its probes), so it can't say more; the out-of-line call counts match the exe up to ICF names.
// Still 97 aligned (21 ignoring stack offsets).
// PARKED: stack slot assignment and register choice (21 aligned ignoring stack offsets); behaviour identical
// STUB: LITHTECH 0x00429fa0
void w_AddStaticLights(ILTStream *pStream, MainWorld *pWorld, CLightTable *pTable)
{
	float brightScale = 1.0f;
	std::vector<std::string> groupedLights;
	std::vector<std::string>::iterator itGrouped;
	uint32 i, iProp, nObjects, nProps, startPos, objectPos;
	uint16 objectDataLen, propDataLen;
	uint8 propType;
	char name[256], propData[256];
	LTBOOL bLightObjects, bFastLightObjects;
	bool bAddLight;
	float radius, fov;
	LTVector color, outerColor, pos, dir, vRight, vUp, vForward;
	StaticLight *pLight;

	// The lights in light groups don't light objects.
	bFastLightObjects = LTTRUE;
	pStream->GetPos(&startPos);
	STREAM_READ(nObjects);
	for (i=0; i < nObjects; i++)
	{
		STREAM_READ(objectDataLen);
		pStream->GetPos(&objectPos);
		pStream->ReadString(name, sizeof(name));

		if (strcmp(name, "LightGroup") == 0)
		{
			STREAM_READ(nProps);
			for (iProp=0; iProp < nProps; iProp++)
			{
				uint32 propFlags;
				pStream->ReadString(name, sizeof(name));
				STREAM_READ(propType);
				STREAM_READ(propFlags);
				STREAM_READ(propDataLen);

				if (propDataLen > sizeof(propData))
					pStream->SeekTo(objectPos + objectDataLen);
				else if (propType == PT_STRING)
					pStream->ReadString(propData, sizeof(propData));
				else
					pStream->Read(propData, propDataLen);

				if (propType == PT_STRING && strncmp(name, "Object", 6) == 0)
				{
					std::string objectName = propData;
					if (!objectName.empty())
						groupedLights.push_back(propData);
				}
			}
		}
		else
		{
			pStream->SeekTo(objectPos + objectDataLen);
		}
	}

	// Now add the lights.
	pStream->SeekTo(startPos);
	STREAM_READ(nObjects);
	for (i=0; i < nObjects; i++)
	{
		STREAM_READ(objectDataLen);
		pStream->GetPos(&objectPos);
		pStream->ReadString(name, sizeof(name));

		if (strcmp(name, "Light") != 0 && strcmp(name, "DirLight") != 0 &&
			strcmp(name, "ObjectLight") != 0)
		{
			pStream->SeekTo(objectPos + objectDataLen);
			continue;
		}

		VEC_INIT(color);
		radius = 0.0f;
		bLightObjects = LTTRUE;
		VEC_INIT(dir);
		fov = -1.0f;
		bAddLight = true;

		STREAM_READ(nProps);
		for (iProp=0; iProp < nProps; iProp++)
		{
			uint32 propFlags;
			pStream->ReadString(name, sizeof(name));
			STREAM_READ(propType);
			STREAM_READ(propFlags);
			STREAM_READ(propDataLen);

			if (propDataLen > sizeof(propData))
				pStream->SeekTo(objectPos + objectDataLen);
			else if (propType == PT_STRING)
				pStream->ReadString(propData, sizeof(propData));
			else
				pStream->Read(propData, propDataLen);

			if (propType == PT_STRING && strcmp(name, "Name") == 0)
			{
				itGrouped = std::find(groupedLights.begin(), groupedLights.end(), std::string(propData));
				if (itGrouped != groupedLights.end())
				{
					bAddLight = false;
					continue;
				}
			}

			if (!bAddLight)
				continue;

			if (propType == PT_REAL && strcmp(name, "LightRadius") == 0)
			{
				radius = *((float*)propData);
			}
			else if (propType == PT_COLOR && (strcmp(name, "LightColor") == 0 || strcmp(name, "InnerColor") == 0))
			{
				color = *((LTVector*)propData);
			}
			else if (propType == PT_COLOR && strcmp(name, "OuterColor") == 0)
			{
				outerColor = *((LTVector*)propData);
			}
			else if (propType == PT_VECTOR && strcmp(name, "Pos") == 0)
			{
				pos = *((LTVector*)propData);
			}
			else if (propType == PT_BOOL && strcmp(name, "LightObjects") == 0)
			{
				bLightObjects = *((char*)propData);
			}
			else if (propType == PT_BOOL && strcmp(name, "FastLightObjects") == 0)
			{
				bFastLightObjects = *((char*)propData);
			}
			else if (propType == PT_ROTATION && strcmp(name, "Rotation") == 0)
			{
				gr_GetEulerVectors(*((LTVector*)propData), vRight, vUp, vForward);
				dir = vForward;
			}
			else if (propType == PT_REAL && strcmp(name, "FOV") == 0)
			{
				fov = (float)cos(*((float*)propData) * (MATH_PI / 360.0f));
			}
			else if (propType == PT_REAL && strcmp(name, "BrightScale") == 0)
			{
				brightScale = *((float*)propData);
			}
		}

		// Light the sample points up.
		if (!bLightObjects || !bAddLight)
			continue;

		color *= brightScale;
		outerColor *= brightScale;

		if (bFastLightObjects)
		{
			w_LightTableAddLight(&pos, &color, radius, pTable);
		}
		else
		{
			pLight = new StaticLight;
			if (pLight)
			{
				pLight->m_Color = color;
				pLight->m_OuterColor = outerColor;
				pLight->m_Pos = pos;
				pLight->m_Radius = radius;
				pLight->m_Dir = dir;
				pLight->m_FOV = fov;
				dl_Insert(&pWorld->m_StaticLights, &pLight->m_Link);
			}
		}
	}

	pWorld->InsertStaticLights(&pWorld->m_StaticLights);
}


// Adds a light to the light table samples in its radius ("fast" light objects).
// 42 bytes differ: the prologue loads pTable before pushing edi, and the blue channel is
// extracted later than in the original.
// Wave 6 tried for the colour: separate component stores, VEC_SET, Init without casts (no change), LTVector
// temporary or reading through pSample (SIZE).
// Wave 7 phase 2: audit: behaviour matches. 6 aligned: the prologue loads pTable (esi) before `push edi` in ours and
// after it in the exe (+0x5), and the `and eax, 0xff` of the blue byte is scheduled first in the exe (+0x1ae), last in
// ours. Tried (no change): a block-scoped `LTRGB color`, a block-scoped newColor, the LTVector(r,g,b) constructor,
// `(float)(int)` casts. Untried: a different source for the six range[] lines (e.g. a min/max LTVector pair).
// Hand pass 2: reading pPos->x into a local first (fX) gives the exe's prologue (pPos loaded before pTable): 6 -> 4
// aligned. Left: `and eax, 0xff` (the blue byte) right after the `color = *pSample` store in the exe, last in ours;
// statement orders around the colour read, int temporaries and inline setters taking int/uint8/float don't move it.
// PARKED: scheduling only (one byte-mask placement; 4 aligned)
// STUB: LITHTECH 0x0042ab60
void w_LightTableAddLight(LTVector *pPos, LTVector *pColor, float radius, CLightTable *pTable)
{
	int range[3][2];
	int i, x, y, z;
	float radiusSqr, distSqr, scale;
	LTVector samplePos, newColor;
	LTRGB *pSample, color;
	float fX;

	fX = pPos->x;
	range[0][0] = (int)((fX - radius - pTable->m_LookupStart.x) * pTable->m_InvBlockSize.x);
	range[0][1] = (int)((pPos->x + radius - pTable->m_LookupStart.x) * pTable->m_InvBlockSize.x);
	range[1][0] = (int)((pPos->y - radius - pTable->m_LookupStart.y) * pTable->m_InvBlockSize.y);
	range[1][1] = (int)((pPos->y + radius - pTable->m_LookupStart.y) * pTable->m_InvBlockSize.y);
	range[2][0] = (int)((pPos->z - radius - pTable->m_LookupStart.z) * pTable->m_InvBlockSize.z);
	range[2][1] = (int)((pPos->z + radius - pTable->m_LookupStart.z) * pTable->m_InvBlockSize.z);

	for (i=0; i < 3; i++)
	{
		range[i][0] = (range[i][0] < 0) ? 0 : ((range[i][0] > (int)pTable->m_DimsMinus1[i]) ? pTable->m_DimsMinus1[i] : range[i][0]);
		range[i][1] = (range[i][1] < 0) ? 0 : ((range[i][1] > (int)pTable->m_DimsMinus1[i]) ? pTable->m_DimsMinus1[i] : range[i][1]);

		if (range[i][0] > range[i][1])
			range[i][0] = range[i][1];
	}

	radiusSqr = radius * radius;
	for (x=range[0][0]; x <= range[0][1]; x++)
	{
		samplePos.x = (float)x * pTable->m_BlockSize.x + pTable->m_LookupStart.x;
		for (y=range[1][0]; y <= range[1][1]; y++)
		{
			samplePos.y = (float)y * pTable->m_BlockSize.y + pTable->m_LookupStart.y;
			for (z=range[2][0]; z <= range[2][1]; z++)
			{
				samplePos.z = (float)z * pTable->m_BlockSize.z + pTable->m_LookupStart.z;

				distSqr = VEC_DISTSQR(samplePos, *pPos);
				if (distSqr < radiusSqr)
				{
					scale = (float)sqrt(distSqr) / radius;
					pSample = &pTable->m_pData[x + y*pTable->m_Dims[0] + z*pTable->m_XSizeTimesYSize];
					color = *pSample;
					scale = 1.0f - scale;

					newColor.Init((float)color.r, (float)color.g, (float)color.b);
					newColor += *pColor * scale * scale;

					if (newColor.x < 0.0f) newColor.x = 0.0f; else if (newColor.x > 255.0f) newColor.x = 255.0f;
					if (newColor.y < 0.0f) newColor.y = 0.0f; else if (newColor.y > 255.0f) newColor.y = 255.0f;
					if (newColor.z < 0.0f) newColor.z = 0.0f; else if (newColor.z > 255.0f) newColor.z = 255.0f;

					color.r = (uint8)newColor.x;
					color.g = (uint8)newColor.y;
					color.b = (uint8)newColor.z;
					*pSample = color;
				}
			}
		}
	}
}


// FUNCTION: LITHTECH 0x0042aeb0
LTBOOL MainWorld::InheritFrom(MainWorld *pOther)
{
	uint32 i;
	WorldData *pSrc, *pDest;

	Term();

	m_bInherited = LTTRUE;
	if (!m_WorldTree.Inherit(&pOther->m_WorldTree))
		goto Error;

	if (!m_WorldModels.SetSize(pOther->m_WorldModels.GetSize()))
		goto Error;

	SetArray(m_WorldModels, (WorldData*)LTNULL);

	if (!m_SkyPolies.CopyArray(pOther->m_SkyPolies))
		goto Error;

	m_LightAnims.CopyPointers(pOther->m_LightAnims);

	for (i=0; i < m_WorldModels.GetSize(); i++)
	{
		pSrc = pOther->m_WorldModels[i];

		pDest = new WorldData;
		if (!pDest)
			goto Error;

		m_WorldModels[i] = pDest;

		// Share the BSPs.
		pDest->m_pOriginalBsp = pSrc->m_pOriginalBsp;
		pDest->m_pWorldBsp = pSrc->m_pWorldBsp;
		pDest->m_Flags &= ~(WD_ORIGINALBSPALLOCED | WD_WORLDBSPALLOCED);

		// The VisBSP's nodes hold objects, so it gets its own nodes.
		if (pSrc->m_pOriginalBsp->WBSlot9())
		{
			pDest->m_Flags |= WD_ORIGINALBSPALLOCED;
			pDest->m_pOriginalBsp = new WorldBsp;
			if (!pDest->m_pOriginalBsp)
				goto Error;

			if (!pDest->m_pOriginalBsp->InheritFrom(pSrc->m_pOriginalBsp))
				goto Error;
		}

		pDest->m_pValidBsp = pDest->m_pWorldBsp ? pDest->m_pWorldBsp : pDest->m_pOriginalBsp;
	}

	m_RenderDataPos = pOther->m_RenderDataPos;
	m_BoxMin = pOther->m_BoxMin;
	m_BoxMax = pOther->m_BoxMax;
	m_StaticLights = pOther->m_StaticLights;
	m_pSurfaceEffects = pOther->m_pSurfaceEffects;
	m_LMGridSize = pOther->m_LMGridSize;
	m_LightTable = pOther->m_LightTable;
	m_ExtentsMin = pOther->m_ExtentsMin;
	m_ExtentsMax = pOther->m_ExtentsMax;
	m_ExtentsDiffInv = pOther->m_ExtentsDiffInv;
	m_WorldFlags = pOther->m_WorldFlags;
	m_NamedEntries = pOther->m_NamedEntries;
	m_nNamedEntries = pOther->m_nNamedEntries;
	m_pWorldInfoString = pOther->m_pWorldInfoString;
	m_pWorldStream = pOther->m_pWorldStream;
	m_FileVersion = pOther->m_FileVersion;

	InsertStaticLights(&pOther->m_StaticLights);
	dl_Insert(&pOther->m_InheritedWorlds, &m_InheritLink);

	m_bLoaded = LTTRUE;
	return LTTRUE;

Error:;
	Term();
	return LTFALSE;
}


// FUNCTION: LITHTECH 0x0042b1c0
void MainWorld::Term()
{
	LTLink *pCur, *pNext;
	uint32 i;
	WorldData *pWorldModel;

	m_WorldTree.Term();

	// Term the worlds that share our data.
	pCur = m_InheritedWorlds.m_pNext;
	while (pCur != &m_InheritedWorlds)
	{
		pNext = pCur->m_pNext;
		((MainWorld*)pCur->m_pData)->Term();
		pCur = pNext;
	}

	dl_Remove(&m_InheritLink);

	if (m_bInherited)
	{
		// Our data belongs to the world we inherited from.
		m_LightAnims.TermPointers();

		for (i=0; i < m_WorldModels.GetSize(); i++)
		{
			pWorldModel = m_WorldModels[i];
			if (pWorldModel)
			{
				if (pWorldModel->m_Flags & WD_ORIGINALBSPALLOCED)
					pWorldModel->m_pOriginalBsp->TermNodes();

				pWorldModel->Clear();
			}
		}

		DeleteAndClearArray(m_WorldModels);
	}
	else
	{
		m_LightAnims.Term();
		ClearWorldData();

		// Delete the static lights.
		pCur = m_StaticLights.m_pNext;
		while (pCur != &m_StaticLights)
		{
			pNext = pCur->m_pNext;
			delete (StaticLight*)pCur->m_pData;
			pCur = pNext;
		}

		dfree(m_pWorldInfoString);
		w_TermSurfaceEffects(this);
		m_LightTable.FreeAll();

		if (m_NamedEntries)
			delete [] m_NamedEntries;

		DeleteAndClearArray(m_WorldModels);
	}

	Clear();
}


// FUNCTION: LITHTECH 0x0042b3b0
void MainWorld::ClearWorldData()
{
	if (m_pWorldStream)
	{
		m_pWorldStream->Release();
		m_pWorldStream = LTNULL;
	}
}


// Collects the polies with SURF_SKY.
// FUNCTION: LITHTECH 0x0042b3d0
LTBOOL MainWorld::SetupSkyPolies()
{
	uint32 i, j, nSkyPolies, iCurSkyPoly;
	WorldBsp *pBsp;
	WorldPoly *pPoly;

	nSkyPolies = 0;
	for (i=0; i < m_WorldModels.GetSize(); i++)
	{
		pBsp = m_WorldModels[i]->m_pOriginalBsp;
		for (j=0; j < pBsp->m_nPolies; j++)
		{
			if (((Surface*)pBsp->m_Polies[j]->m_pSurface)->m_Flags & SURF_SKY)
				nSkyPolies++;
		}
	}

	if (!m_SkyPolies.SetSize(nSkyPolies))
		return LTFALSE;

	iCurSkyPoly = 0;
	for (i=0; i < m_WorldModels.GetSize(); i++)
	{
		pBsp = m_WorldModels[i]->m_pOriginalBsp;
		for (j=0; j < pBsp->m_nPolies; j++)
		{
			pPoly = pBsp->m_Polies[j];
			if (((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SKY)
			{
				if (iCurSkyPoly < m_SkyPolies.GetSize())
				{
					m_SkyPolies[iCurSkyPoly] = pPoly;
					iCurSkyPoly++;
				}
			}
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0042b4f0
void MainWorld::InsertStaticLights(LTLink *pListHead)
{
	LTLink *pCur;

	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		m_WorldTree.InsertObject((WorldTreeObj*)pCur->m_pData, NOA_Lights);
	}
}


// FUNCTION: LITHTECH 0x0042b520
LTBOOL MainWorld::InitWorldModel(WorldModelInstance *pInstance, const char *pName)
{
	WorldData *pWorldModel;
	TerrainSection *pSection;
	WorldTreeNode *pNode;
	LTVector vHalfDims;
	LTVector vMin;
	uint32 iSection;
	LTVector vMax;
	char worldName[128];

	// Terrain sections are named w_MakeSpecialName style: "#$#<section> <world model>".
	if (strstr(pName, "#$#") == pName)
	{
		sscanf(&pName[strlen("#$#")], "%d %s", &iSection, worldName);

		pWorldModel = w_FindWorldModel(this, worldName);
		if (pWorldModel && iSection < pWorldModel->m_pOriginalBsp->m_TerrainSections.GetSize())
		{
			pSection = &pWorldModel->m_pOriginalBsp->m_TerrainSections[iSection];
			pInstance->InitWorldData((WorldBsp*)pSection, LTNULL);

			pNode = m_WorldTree.FindNode(&pSection->m_NodePath);
			if (pNode)
			{
				pNode->GetBBox(&vMin, &vMax);
				vHalfDims = (vMax - vMin) * 0.5f;
				pInstance->SetDims(vHalfDims);
				pInstance->SetPos(vMin + vHalfDims);
				return LTTRUE;
			}
		}
	}
	else
	{
		pWorldModel = w_FindWorldModel(this, pName);
		if (pWorldModel)
		{
			pInstance->InitWorldData(pWorldModel->m_pOriginalBsp, pWorldModel->m_pWorldBsp);
			return LTTRUE;
		}
	}

	return LTFALSE;
}


// Loads the light anims (the render data at m_RenderDataPos).
// Wave 8: the frame loop works through a pointer to the anim's frame slot (ppFrames): the exe computes
// &pAnim->m_pFrames[iFrame] once per frame and re-reads the slot, never pAnim->m_pFrames. Indexing
// pAnim->m_pFrames[iFrame][iPoly] directly spilled `this` (246 aligned, parked as register allocation only).
// FUNCTION: LITHTECH 0x0042b6e0
LTRESULT MainWorld::LoadObjects(ILTStream *pStream)
{
	uint32 nPolyRefs, nFrames, nDataBytes, nPolyFrames, nAnims;
	uint32 i, iFrame, iPoly, iVert;
	uint32 iCurPolyRef, iCurFrame, iCurData, iCurPolyFrame;
	LightAnim *pAnim;
	LAPolyFrame *pFrame, **ppFrames;

	m_WorldFlags &= ~WORLD_HASBASELIGHT;
	pStream->SeekTo(m_RenderDataPos);

	STREAM_READ(nPolyRefs);
	STREAM_READ(nFrames);
	STREAM_READ(nDataBytes);
	STREAM_READ(nPolyFrames);
	STREAM_READ(nAnims);

	if (!m_LightAnims.SetSize(nAnims) ||
		!m_LightAnimPolyRefs.SetSize(nPolyRefs) ||
		!m_LightAnimFrames.SetSize(nFrames) ||
		!m_LightAnimData.SetSize(nDataBytes) ||
		!m_LightAnimPolyFrames.SetSize(nPolyFrames))
	{
		TermLightAnims();
		return LT_OUTOFMEMORY;
	}

	iCurPolyFrame = 0;
	iCurData = 0;
	iCurPolyRef = 0;
	iCurFrame = 0;
	for (i=0; i < nAnims; i++)
	{
		pAnim = &m_LightAnims[i];

		pStream->ReadString(pAnim->m_Name, sizeof(pAnim->m_Name));
		pAnim->m_pFrames = &m_LightAnimFrames.GetArray()[iCurFrame];
		pAnim->m_pPolyRefs = &m_LightAnimPolyRefs.GetArray()[iCurPolyRef];
		STREAM_READ(pAnim->m_bShadowMap);
		STREAM_READ(pAnim->m_nFrames);
		STREAM_READ(pAnim->m_nPolies);

		iCurFrame += pAnim->m_nFrames;
		iCurPolyRef += pAnim->m_nPolies;
		if (iCurFrame > m_LightAnimFrames.GetSize() || iCurPolyRef > m_LightAnimPolyRefs.GetSize())
			goto Error;

		for (iPoly=0; iPoly < pAnim->m_nPolies; iPoly++)
		{
			STREAM_READ(pAnim->m_pPolyRefs[iPoly].m_iWorld);
			STREAM_READ(pAnim->m_pPolyRefs[iPoly].m_iPoly);
		}

		for (iFrame=0; iFrame < pAnim->m_nFrames; iFrame++)
		{
			ppFrames = &pAnim->m_pFrames[iFrame];
			*ppFrames = &m_LightAnimPolyFrames.GetArray()[iCurPolyFrame];
			iCurPolyFrame += pAnim->m_nPolies;
			if (iCurPolyFrame > m_LightAnimPolyFrames.GetSize())
				goto Error;

			// Lightmaps.
			for (iPoly=0; iPoly < pAnim->m_nPolies; iPoly++)
			{
				pFrame = &(*ppFrames)[iPoly];

				pFrame->m_pLightmap = &m_LightAnimData.GetArray()[iCurData];
				STREAM_READ(pFrame->m_LightmapSize);
				iCurData += pFrame->m_LightmapSize;
				if (iCurData > m_LightAnimData.GetSize())
					goto Error;

				pStream->Read(pFrame->m_pLightmap, pFrame->m_LightmapSize);
			}

			// Vertex colors.
			if (m_FileVersion == CURRENT_WORLD_VERSION)
			{
				for (iPoly=0; iPoly < pAnim->m_nPolies; iPoly++)
				{
					pFrame = &(*ppFrames)[iPoly];

					STREAM_READ(pFrame->m_nVerts);

					pFrame->m_pVertR = &m_LightAnimData.GetArray()[iCurData];
					iCurData += pFrame->m_nVerts;
					if (iCurData > m_LightAnimData.GetSize())
						goto Error;

					for (iVert=0; iVert < pFrame->m_nVerts; iVert++)
						pStream->Read(&pFrame->m_pVertR[iVert], 1);

					pFrame->m_pVertG = &m_LightAnimData.GetArray()[iCurData];
					iCurData += pFrame->m_nVerts;
					if (iCurData > m_LightAnimData.GetSize())
						goto Error;

					for (iVert=0; iVert < pFrame->m_nVerts; iVert++)
						pStream->Read(&pFrame->m_pVertG[iVert], 1);

					pFrame->m_pVertB = &m_LightAnimData.GetArray()[iCurData];
					iCurData += pFrame->m_nVerts;
					if (iCurData > m_LightAnimData.GetSize())
						goto Error;

					for (iVert=0; iVert < pFrame->m_nVerts; iVert++)
						pStream->Read(&pFrame->m_pVertB[iVert], 1);
				}
			}
			else
			{
				for (iPoly=0; iPoly < pAnim->m_nPolies; iPoly++)
				{
					pFrame = &(*ppFrames)[iPoly];
					pFrame->m_pVertR = LTNULL;
					pFrame->m_pVertG = LTNULL;
					pFrame->m_pVertB = LTNULL;
					pFrame->m_nVerts = 0;
				}
			}
		}
	}

	if (pStream->ErrorStatus() != LT_OK)
	{
		TermLightAnims();
		return LT_ERROR;
	}

	pAnim = FindLightAnim("LightAnim_BASE", LTNULL);
	if (pAnim)
	{
		m_WorldFlags |= WORLD_HASBASELIGHT;
		pAnim->m_iFrames[0] = pAnim->m_iFrames[1] = 0;
		pAnim->m_PercentBetween = 0;
	}

	return LT_OK;

Error:;
	TermLightAnims();
	return LT_ERROR;
}


// FUNCTION: LITHTECH 0x0042bbf0
void MainWorld::TermLightAnims()
{
	m_LightAnims.Term();
	m_LightAnimPolyRefs.Term();
	m_LightAnimFrames.Term();
	m_LightAnimData.Term();
	m_LightAnimPolyFrames.Term();
}


// FUNCTION: LITHTECH 0x0042bcb0
LightAnim* MainWorld::FindLightAnim(const char *pName, uint32 *pIndex)
{
	uint32 i;
	LightAnim *pAnim;

	for (i=0; i < m_LightAnims.GetSize(); i++)
	{
		pAnim = &m_LightAnims[i];
		if (stricmp(pAnim->m_Name, pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			return pAnim;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0042bd00
void MainWorld::UpdateLightAnimPolies(LightAnim *pAnim)
{
	LAPolyRef *pCur, *pEnd;

	pCur = pAnim->m_pPolyRefs;
	pEnd = pCur + pAnim->m_nPolies;
	while (pCur != pEnd)
	{
		m_WorldModels[pCur->m_iWorld]->m_pOriginalBsp->m_Polies[pCur->m_iPoly]->m_Flags |= WPF_RELIGHT;
		pCur++;
	}
}


// Turns the leaves' (world model, poly) index pairs into poly pointers.
// FUNCTION: LITHTECH 0x0042bd50
LTBOOL MainWorld::SetupLeafPolies()
{
	WorldBsp *pBsp, *pModelBsp;
	Leaf *pLeaf;
	LAPolyRef *pRef;
	uint32 i, j;

	pBsp = GetVisBSP();
	if (!pBsp)
		return LTFALSE;

	for (i=0; i < pBsp->m_nLeafs; i++)
	{
		pLeaf = &pBsp->m_Leafs[i];
		for (j=0; j < pLeaf->m_nPolies; j++)
		{
			pRef = (LAPolyRef*)&pLeaf->m_Polies[j];
			if (pRef->m_iWorld >= m_WorldModels.GetSize())
				return LTFALSE;

			pModelBsp = m_WorldModels[pRef->m_iWorld]->m_pOriginalBsp;
			if (pRef->m_iPoly >= pModelBsp->m_nPolies)
				return LTFALSE;

			pLeaf->m_Polies[j] = pModelBsp->m_Polies[pRef->m_iPoly];
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0042be10
WorldBsp* MainWorld::GetVisBSP()
{
	uint32 i;
	WorldBsp *pBsp;

	for (i=0; i < m_WorldModels.GetSize(); i++)
	{
		pBsp = m_WorldModels[i]->m_pOriginalBsp;
		if (pBsp->m_WorldInfoFlags & WIF_VISBSP)
			return pBsp;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0042be40
WorldBsp* MainWorld::GetPhysicsBSP()
{
	uint32 i;
	WorldBsp *pBsp;

	for (i=0; i < m_WorldModels.GetSize(); i++)
	{
		pBsp = m_WorldModels[i]->m_pOriginalBsp;
		if (pBsp->m_WorldInfoFlags & WIF_PHYSICSBSP)
			return pBsp;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0042be70
void MainWorld::CalcBoundingSpheres()
{
	uint32 i;
	WorldData *pWorldModel;

	for (i=0; i < m_WorldModels.GetSize(); i++)
	{
		pWorldModel = m_WorldModels[i];

		if (pWorldModel->m_pOriginalBsp)
			w_CalcBoundingSpheres(pWorldModel->m_pOriginalBsp);

		if (pWorldModel->m_pWorldBsp)
			w_CalcBoundingSpheres(pWorldModel->m_pWorldBsp);
	}
}


// The base ring of a poly (SPolyVertex array right after the WorldPoly).
#define POLY_BASEVERT(pPoly, i)		(((SPolyVertex*)((pPoly) + 1))[i].m_Vec)

// Computes the bounding spheres of all the leaves and polies.
// FUNCTION: LITHTECH 0x0042bec0
void w_CalcBoundingSpheres(WorldBsp *pBsp)
{
	uint32 i, j, k;
	int nVerts;
	Leaf *pLeaf;
	WorldPoly *pPoly;
	float dist;

	for (i=0; i < pBsp->m_nLeafs; i++)
	{
		pLeaf = &pBsp->m_Leafs[i];

		nVerts = 0;
		pLeaf->m_Center.Init();
		for (j=0; j < pLeaf->m_nPolies; j++)
		{
			pPoly = pLeaf->m_Polies[j];
			if (!pPoly)
				continue;

			for (k=0; k < pPoly->m_nVertices; k++)
			{
				pLeaf->m_Center += *POLY_BASEVERT(pPoly, k);
				nVerts++;
			}
		}

		pLeaf->m_Center *= 1.0f / (float)nVerts;

		pLeaf->m_Radius = 0.0f;
		for (j=0; j < pLeaf->m_nPolies; j++)
		{
			pPoly = pLeaf->m_Polies[j];
			if (!pPoly)
				continue;

			for (k=0; k < pPoly->m_nVertices; k++)
			{
				dist = pLeaf->m_Center.Dist(*POLY_BASEVERT(pPoly, k));
				if (dist > pLeaf->m_Radius)
					pLeaf->m_Radius = dist;
			}
		}

		pLeaf->m_Radius += 5.0f;
	}

	for (i=0; i < pBsp->m_nPolies; i++)
	{
		pPoly = pBsp->m_Polies[i];

		pPoly->m_Center.Init();
		for (k=0; k < pPoly->m_nVertices; k++)
		{
			pPoly->m_Center += *POLY_BASEVERT(pPoly, k);
		}

		pPoly->m_Center *= 1.0f / (float)pPoly->m_nVertices;

		pPoly->m_Radius = 0.0f;
		for (k=0; k < pPoly->m_nVertices; k++)
		{
			dist = pPoly->m_Center.Dist(*POLY_BASEVERT(pPoly, k));
			if (dist > pPoly->m_Radius)
				pPoly->m_Radius = dist;
		}

		pPoly->m_Radius += 0.2f;
	}
}


// ----------------------------------------------------------------------------- //
// PBlockTable.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0042c1c0
void PBlockTable::Term()
{
	uint32 i;

	for (i=0; i < m_nBlocks; i++)
	{
		dfree(m_pBlocks[i].m_pNodes);
	}

	dfree(m_pBlocks);
}


// FUNCTION: LITHTECH 0x0042c200
LTBOOL PBlockTable::Load(ILTStream *pStream)
{
	uint32 i;
	PBlock *pBlock;

	Term();

	pStream->Read(m_Size, sizeof(m_Size));
	m_XZSize = m_Size[0] * m_Size[1];
	m_nBlocks = m_Size[0] * m_Size[1] * m_Size[2];
	if (m_nBlocks)
	{
		m_pBlocks = (PBlock*)dalloc_z(m_nBlocks * sizeof(PBlock));
		if (!m_pBlocks)
		{
			Term();
			return LTFALSE;
		}
	}

	pStream->Read(&m_BlockSize, sizeof(m_BlockSize));
	pStream->Read(&m_Origin, sizeof(m_Origin));

	for (i=0; i < m_nBlocks; i++)
	{
		pBlock = &m_pBlocks[i];

		pStream->Read(&pBlock->m_nNodes, sizeof(pBlock->m_nNodes));
		pStream->Read(&pBlock->m_iRoot, sizeof(pBlock->m_iRoot));
		if (pBlock->m_nNodes > 0)
		{
			pBlock->m_pNodes = dalloc(pBlock->m_nNodes * 6);
			if (!pBlock->m_pNodes)
			{
				Term();
				return LTFALSE;
			}
		}

		pStream->Read(pBlock->m_pNodes, pBlock->m_nNodes * 6);
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0042c300
PBlock* PBlockTable::GetBlock(LTVector vPos)
{
	int x, y, z;

	x = (int)((vPos.x - m_Origin.x) / m_BlockSize.x + 0.0001f);
	y = (int)((vPos.y - m_Origin.y) / m_BlockSize.y + 0.0001f);
	z = (int)((vPos.z - m_Origin.z) / m_BlockSize.z + 0.0001f);

	if (x < 0) x = 0; else if (x > (int)m_Size[0]-1) x = m_Size[0]-1;
	if (y < 0) y = 0; else if (y > (int)m_Size[1]-1) y = m_Size[1]-1;
	if (z < 0) z = 0; else if (z > (int)m_Size[2]-1) z = m_Size[2]-1;

	if (x < 0 || y < 0 || z < 0)
		return LTNULL;

	return &m_pBlocks[x + y*m_Size[0] + z*m_XZSize];
}


// ----------------------------------------------------------------------------- //
// TerrainSection.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0042c3b0
TerrainSection::TerrainSection()
{
	m_RootNode = NODE_OUT;
	m_pWorldBsp = LTNULL;
}


// FUNCTION: LITHTECH 0x0042c4a0
TerrainSection::~TerrainSection()
{
	Term();
}


// FUNCTION: LITHTECH 0x0042c510
void TerrainSection::Term()
{
	m_Polies.Term();
	m_RootNode = NODE_OUT;
	m_Nodes.Term();
	m_pWorldBsp = LTNULL;
	m_PBlockTable.Term();
}


// The original calls m_Polies.SetSize2 out of line; ours inlines it (inline budget).
// FUNCTION: LITHTECH 0x0042c580
LTBOOL TerrainSection::Load(WorldBsp *pBsp, ILTStream *pStream, int iSection)
{
	uint32 i, j, index, nNodes, nPolies;
	Node *pNode;

	Term();

	m_pWorldBsp = pBsp;
	if (!w_MakeSpecialName(pBsp->m_WorldName, iSection, m_WorldName, sizeof(m_WorldName)))
	{
		dsi_ConsolePrint("** Terrain %s's name is too long, can't hold TerrainSections.");
		return LTFALSE;
	}

	// Nodes.
	STREAM_READ(nNodes);
	if (!m_Nodes.SetSize(nNodes))
		return LTFALSE;

	for (i=0; i < nNodes; i++)
	{
		pNode = &m_Nodes[i];
		pNode->m_Flags = 8;

		STREAM_READ(index);
		if (index >= pBsp->m_nPolies)
			goto Error;

		pNode->m_pPoly = pBsp->m_Polies[index];

		for (j=0; j < 2; j++)
		{
			STREAM_READ(index);
			pNode->m_Sides[j] = w_NodeForIndex(m_Nodes.GetArray(), m_Nodes.GetSize(), index);
			if (!pNode->m_Sides[j])
				goto Error;
		}
	}

	STREAM_READ(index);
	m_RootNode = w_NodeForIndex(m_Nodes.GetArray(), m_Nodes.GetSize(), index);
	if (!m_RootNode)
	{
		Term();
		return LTFALSE;
	}

	// Polies.
	STREAM_READ(nPolies);
	if (!m_Polies.SetSize(nPolies))
		goto Error;

	SetArray(m_Polies, (WorldPoly*)LTNULL);

	for (i=0; i < nPolies; i++)
	{
		STREAM_READ(index);
		if (index >= pBsp->m_nPolies)
			goto Error;

		m_Polies[i] = pBsp->m_Polies[index];
	}

	if (!m_PBlockTable.Load(pStream))
		goto Error;

	m_NodePath.Load(pStream);

	*pStream >> m_Center;

	w_SetPlaneTypes(m_Nodes.GetArray(), m_Nodes.GetSize(), LTTRUE);
	CalcBoundRadius();
	return LTTRUE;

Error:;
	Term();
	return LTFALSE;
}


// FUNCTION: LITHTECH 0x0042c820
void TerrainSection::CalcBoundRadius()
{
	m_BoundRadius = w_CalcBoundRadius(m_Polies.GetArray(), m_Polies.GetSize(), &m_Center);
}


// The distance from pCenter to the farthest vertex of the polies.
// FUNCTION: LITHTECH 0x0042c850
float w_CalcBoundRadius(WorldPoly **pPolies, uint32 nPolies, LTVector *pCenter)
{
	uint32 i, j;
	WorldPoly *pPoly;
	float maxDist, dist;

	maxDist = 0.0f;
	for (i=0; i < nPolies; i++)
	{
		pPoly = pPolies[i];
		for (j=0; j < pPoly->m_nVertices; j++)
		{
			dist = pCenter->Dist(*POLY_BASEVERT(pPoly, j));
			if (dist > maxDist)
				maxDist = dist;
		}
	}

	return maxDist;
}


// FUNCTION: LITHTECH 0x0042c900
Node* TerrainSection::GetRootNode()
{
	return m_RootNode;
}

// FUNCTION: LITHTECH 0x0042c910
HPOLY TerrainSection::MakeHPoly(Node *pNode)
{
	return m_pWorldBsp->MakeHPoly(pNode);
}

// FUNCTION: LITHTECH 0x0042c920
WorldPoly* TerrainSection::GetPolyFromHPoly(HPOLY hPoly)
{
	return m_pWorldBsp->GetPolyFromHPoly(hPoly);
}

// FUNCTION: LITHTECH 0x0042c930
LTBOOL TerrainSection::VSlot4(LTVector vPos)
{
	return (LTBOOL)m_PBlockTable.GetBlock(vPos);
}

// FUNCTION: LITHTECH 0x0042c960
PBlockTable* TerrainSection::GetPBlockTable()
{
	return &m_PBlockTable;
}

// FUNCTION: LITHTECH 0x0042c970
LTVector* TerrainSection::GetPBlockOrigin()
{
	return &m_PBlockTable.m_Origin;
}

// FUNCTION: LITHTECH 0x0042c980
Node* TerrainSection::GetNodes()
{
	return m_Nodes.GetArray();
}

// FUNCTION: LITHTECH 0x0042c990
uint32 TerrainSection::GetWorldInfoFlags()
{
	return m_pWorldBsp->m_WorldInfoFlags;
}

// FUNCTION: LITHTECH 0x0042c9a0
float TerrainSection::GetBoundRadius()
{
	return m_BoundRadius;
}

// The linker folded this with every other "return 1" (0x004b22a0).
uint32 TerrainSection::IsUntransformed()
{
	return 1;
}

// Folded with every other "return 0" (0x0043dac0).
LTBOOL TerrainSection::WBSlot9()
{
	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0042c9b0
WorldBspBase::WorldBspBase()
{
	m_WorldName[0] = 0;
}


// ----------------------------------------------------------------------------- //
// WorldBsp.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0042c9c0
WorldBsp::WorldBsp()
{
	Clear();
}


// FUNCTION: LITHTECH 0x0042ca90
WorldBsp::~WorldBsp()
{
	Term();
}


// FUNCTION: LITHTECH 0x0042cb10
void WorldBsp::Clear()
{
	m_BspFlags = 0;
	m_MemoryUse = 0;

	m_Planes = NULL;
	m_nPlanes = 0;

	m_Nodes = NULL;
	m_nNodes = 0;

	m_Surfaces = NULL;
	m_nSurfaces = 0;

	m_LeafLists = NULL;
	m_nLeafLists = 0;

	m_Leafs = NULL;
	m_nLeafs = 0;

	m_LeafPolies = NULL;
	m_nLeafPolies = 0;
	m_LeafListDataSize = 0;

	m_RootNode = NULL;

	m_Polies = NULL;
	m_nPolies = 0;

	m_Points = NULL;
	m_nPoints = 0;

	m_Portals = NULL;
	m_nPortals = 0;

	m_TextureNameData = NULL;
	m_TextureNames = NULL;
	m_nTextures = 0;

	m_MinBox.Init();
	m_MaxBox.Init();
	m_MaxTreeDepth = 0;
	m_UnknownE0 = 0;
	m_UnknownE4 = 0;

	m_WorldTranslation.Init();
	m_pUnknownF4 = NULL;

	m_Index = 0;

	m_PolyData = NULL;
	m_PolyDataSize = 0;
	m_LeafListData = NULL;

	m_WorldInfoFlags = 0;
	m_Unknown150 = -1;
}


// FUNCTION: LITHTECH 0x0042cc10
void WorldBsp::Term()
{
	uint32 i;

	if (!(m_BspFlags & 1))
	{
		m_TerrainSections.Term();
		m_PBlockTable.Term();

		for (i=0; i < m_nPortals; i++)
		{
			if (m_Portals[i].m_pName)
				dfree(m_Portals[i].m_pName);
		}

		if (m_Portals)
			dfree(m_Portals);

		if (m_LeafLists)
			dfree(m_LeafLists);

		if (m_PolyData)
			dfree(m_PolyData);

		if (m_LeafListData)
			dfree(m_LeafListData);

		if (m_Leafs)
			dfree(m_Leafs);

		if (m_LeafPolies)
			dfree(m_LeafPolies);

		if (m_Polies)
			dfree(m_Polies);

		if (m_Points)
			dfree(m_Points);

		if (m_Planes)
			dfree(m_Planes);

		if (m_Surfaces)
			delete [] m_Surfaces;

		if (m_TextureNames)
			dfree(m_TextureNames);

		if (m_TextureNameData)
			dfree(m_TextureNameData);

		if (m_pUnknownF4)
			dfree(m_pUnknownF4);

		g_WorldGeometryMemory -= m_MemoryUse;
	}

	TermNodes();
	Clear();
}


// FUNCTION: LITHTECH 0x0042cde0
void WorldBsp::TermNodes()
{
	if (m_Nodes)
	{
		delete [] m_Nodes;
		m_Nodes = NULL;
	}
}


// SAFE_STRCPY for the world name (the SDK's inline LTStrCpy) is the pending inline site that keeps the
// CopyArray2 calls of the inlined TerrainSection::operator= out of line.
// FUNCTION: LITHTECH 0x0042ce00
LTBOOL WorldBsp::InheritFrom(WorldBsp *pOther)
{
	uint32 i, j;
	Node *pSrc, *pDest;

	Term();

	// The nodes are ours (objects get linked to them).
	m_Nodes = new Node[pOther->m_nNodes];
	if (!m_Nodes)
		return LTFALSE;

	for (i=0; i < pOther->m_nNodes; i++)
	{
		pSrc = &pOther->m_Nodes[i];
		pDest = &m_Nodes[i];

		pDest->m_iLeaf = pSrc->m_iLeaf;
		dl_TieOff(&pDest->m_Objects);
		for (j=0; j < 2; j++)
		{
			pDest->m_Sides[j] = RemapNode(pOther, pSrc->m_Sides[j]);
		}
	}

	m_nNodes = pOther->m_nNodes;
	m_RootNode = RemapNode(pOther, pOther->m_RootNode);

	if (!m_TerrainSections.CopyArray(pOther->m_TerrainSections))
		return LTFALSE;

	// Everything else is shared.
	m_Planes = pOther->m_Planes;
	m_nPlanes = pOther->m_nPlanes;
	m_Surfaces = pOther->m_Surfaces;
	m_nSurfaces = pOther->m_nSurfaces;
	m_LeafLists = pOther->m_LeafLists;
	m_nLeafLists = pOther->m_nLeafLists;
	m_Leafs = pOther->m_Leafs;
	m_nLeafs = pOther->m_nLeafs;
	m_LeafPolies = pOther->m_LeafPolies;
	m_nLeafPolies = pOther->m_nLeafPolies;
	m_LeafListDataSize = pOther->m_LeafListDataSize;
	m_RootNode = pOther->m_RootNode;
	m_Polies = pOther->m_Polies;
	m_nPolies = pOther->m_nPolies;
	m_PolyDataSize = pOther->m_PolyDataSize;
	m_Points = pOther->m_Points;
	m_nPoints = pOther->m_nPoints;
	m_Portals = pOther->m_Portals;
	m_nPortals = pOther->m_nPortals;
	m_TextureNameData = pOther->m_TextureNameData;
	m_TextureNames = pOther->m_TextureNames;
	m_nTextures = pOther->m_nTextures;

	SAFE_STRCPY(m_WorldName, pOther->m_WorldName);

	m_MinBox = pOther->m_MinBox;
	m_MaxBox = pOther->m_MaxBox;
	m_MaxTreeDepth = pOther->m_MaxTreeDepth;
	m_UnknownE0 = pOther->m_UnknownE0;
	m_UnknownE4 = pOther->m_UnknownE4;
	m_WorldTranslation = pOther->m_WorldTranslation;
	m_pUnknownF4 = pOther->m_pUnknownF4;
	m_Index = pOther->m_Index;
	m_PolyData = pOther->m_PolyData;
	m_LeafListData = pOther->m_LeafListData;
	m_PBlockTable = pOther->m_PBlockTable;
	m_WorldInfoFlags = pOther->m_WorldInfoFlags;
	m_BoundRadius = pOther->m_BoundRadius;

	m_BspFlags |= 1;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0042d2b0
void WorldBsp::CalcBoundRadius()
{
	m_BoundRadius = w_CalcBoundRadius(m_Polies, m_nPolies, &m_WorldTranslation);
}

// FUNCTION: LITHTECH 0x0042d2e0
void WorldBsp::WBSlot11(void *p)
{
	wb_Unknown49e330(p);
}

// FUNCTION: LITHTECH 0x0042d2f0
uint32 WorldBsp::GetWorldInfoFlags()
{
	return m_WorldInfoFlags;
}

// FUNCTION: LITHTECH 0x0042d300
LTBOOL WorldBsp::WBSlot9()
{
	return (m_WorldInfoFlags >> 5) & 1;
}

// FUNCTION: LITHTECH 0x0042d310
float WorldBsp::GetBoundRadius()
{
	return m_BoundRadius;
}

// FUNCTION: LITHTECH 0x0042d320
Node* WorldBsp::GetRootNode()
{
	return m_RootNode;
}

// FUNCTION: LITHTECH 0x0042d330
HPOLY WorldBsp::MakeHPoly(Node *pNode)
{
	if (pNode->m_pPoly >= (WorldPoly*)m_PolyData &&
		pNode->m_pPoly < (WorldPoly*)(m_PolyData + m_PolyDataSize))
	{
		return ((uint32)m_Index << 16) | pNode->m_pPoly->m_Index;
	}
	else
	{
		return INVALID_HPOLY;
	}
}

// FUNCTION: LITHTECH 0x0042d370
WorldPoly* WorldBsp::GetPolyFromHPoly(HPOLY hPoly)
{
	uint32 polyIndex;

	polyIndex = hPoly & 0xFFFF;
	if (polyIndex < m_nPolies)
		return m_Polies[polyIndex];
	else
		return NULL;
}

// FUNCTION: LITHTECH 0x0042d3a0
LTBOOL WorldBsp::VSlot4(LTVector vPos)
{
	return (LTBOOL)m_PBlockTable.GetBlock(vPos);
}

// FUNCTION: LITHTECH 0x0042d3d0
PBlockTable* WorldBsp::GetPBlockTable()
{
	return &m_PBlockTable;
}

// FUNCTION: LITHTECH 0x0042d3e0
LTVector* WorldBsp::GetPBlockOrigin()
{
	return &m_PBlockTable.m_Origin;
}

// FUNCTION: LITHTECH 0x0042d3f0
Node* WorldBsp::GetNodes()
{
	return m_Nodes;
}

// Folded with every other "return 0" (0x0043dac0).
uint32 WorldBsp::IsUntransformed()
{
	return 0;
}


// FUNCTION: LITHTECH 0x0042d400
Node* WorldBsp::RemapNode(WorldBsp *pOther, Node *pNode)
{
	if (pNode == NODE_IN)
		return NODE_IN;

	if (pNode == NODE_OUT)
		return NODE_OUT;

	if (pNode)
		return &m_Nodes[pNode - pOther->m_Nodes];

	return LTNULL;
}


// ----------------------------------------------------------------------------- //
// SharedTexture.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0042d450
SharedTexture::SharedTexture()
{
	m_pEngineData = LTNULL;
	m_pRenderData = LTNULL;
	m_pFile = LTNULL;
	m_pLinkedTexture = LTNULL;
	m_eTexType = 0;
	m_Unknown30 = 0;
	m_RefCount = 0;
	m_pStateChange = LTNULL;
	m_pCommandLine = LTNULL;
	m_Link.m_pData = this;
	memset(&m_Unknown1C, 0, sizeof(m_Unknown1C));
	m_Nexus.Init(this);
}


// FUNCTION: LITHTECH 0x0042d490
SharedTexture::~SharedTexture()
{
	delete [] (char*)m_pCommandLine;
}


// FUNCTION: LITHTECH 0x0042d4b0
void SharedTexture::SetCommandLine(const char *pCommandLine)
{
	uint32 len;

	len = strlen(pCommandLine);
	if (len)
	{
		if (m_pCommandLine)
		{
			delete [] (char*)m_pCommandLine;
			m_pCommandLine = LTNULL;
		}

		m_pCommandLine = new char[len+1];
		strcpy((char*)m_pCommandLine, pCommandLine);
	}
}


// ----------------------------------------------------------------------------- //
// Template code first instantiated here (dynarray.h, l_allocator.h).
// The linker folded identical instantiations (/OPT:ICF); each is annotated with one of them.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0042d510 ?GenAppend@?$CMoArray@UNode@@VDefaultCache@@@@UAEHAAUNode@@@Z
// FUNCTION: LITHTECH 0x0042d650 ?GenRemoveAt@?$CMoArray@UNode@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0042d770 ?GenCopyList@?$CMoArray@UNode@@VDefaultCache@@@@UAEHABV?$GenList@UNode@@@@@Z
// FUNCTION: LITHTECH 0x0042d8b0 ?GenAppendList@?$CMoArray@UNode@@VDefaultCache@@@@UAEHABV?$GenList@UNode@@@@@Z
// FUNCTION: LITHTECH 0x0042d9c0 ?GenFindElement@?$CMoArray@UNode@@VDefaultCache@@@@UBEHABUNode@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0042e1d0 ?GenRemoveAll@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x0042e6f0 ?GenFindElement@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UBEHABVTerrainSection@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0042e8e0 ?Clear@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@AAEXXZ
// FUNCTION: LITHTECH 0x0042eb90 ?Init@?$CMoArray@EVDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x0042ec10 ?Init@?$CMoArray@PAULAPolyFrame@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x0042ee80 ?SetSize2@?$CMoArray@VTerrainSection@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0042f180 ?Init@?$CMoArray@ULightAnim@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x0042f200 ?Init@?$CMoArray@ULAPolyFrame@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x0042f6d0 ?CopyArray2@?$CMoArray@PAUWorldPoly@@VDefaultCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0042f770 ?_DeleteAndDestroyArray@?$CMoArray@VTerrainSection@@VDefaultCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x0042f7c0 ?_InitArray@?$CMoArray@ULAPolyRef@@VDefaultCache@@@@AAEXK@Z
// FUNCTION: LITHTECH 0x0042fc00 ?BaseDelete@@YAXPAVLAlloc@@PAVTerrainSection@@K@Z
// FUNCTION: LITHTECH 0x0042fc40 ?BaseNew@@YAPAVTerrainSection@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x0042ec90 ?SetSize2@?$CMoArray@UNode@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0042ed50 ?InternalNiceSetSize@?$CMoArray@UNode@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0042fab0 ?CopyArray2@?$CMoArray@UNode@@VDefaultCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0042fba0 ?BaseNew@@YAPAUNode@@PAVLAlloc@@PAU1@K@Z
// FUNCTION: LITHTECH 0x0042d9f0 ?GenGetNext@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UBE?AVTerrainSection@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0042dad0 ?GenGetAt@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UBE?AVTerrainSection@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0042dbb0 ?GenAppend@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UAEHAAVTerrainSection@@@Z
// FUNCTION: LITHTECH 0x0042deb0 ?GenRemoveAt@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0042e230 ?GenCopyList@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UAEHABV?$GenList@VTerrainSection@@@@@Z
// FUNCTION: LITHTECH 0x0042e4a0 ?GenAppendList@?$CMoArray@VTerrainSection@@VDefaultCache@@@@UAEHABV?$GenList@VTerrainSection@@@@@Z
// FUNCTION: LITHTECH 0x0042e720 ??4TerrainSection@@QAEAAV0@ABV0@@Z
// FUNCTION: LITHTECH 0x0042e8b0 ??4WorldBspBase@@QAEAAV0@ABV0@@Z
// FUNCTION: LITHTECH 0x0042ef60 ?InternalNiceSetSize@?$CMoArray@VTerrainSection@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0042fdb0 ??_GTerrainSection@@QAEPAXI@Z

// STLport code first instantiated here (w_AddStaticLights' vector<string>).
// FUNCTION: LITHTECH 0x0042aa90 ?_Atomic_swap@_STL@@YAKPAKK@Z
// FUNCTION: LITHTECH 0x0042e8f0 ?_M_deallocate_block@?$_String_base@DV?$allocator@D@_STL@@@_STL@@QAEXXZ
// FUNCTION: LITHTECH 0x0042ea40 ?deallocate@?$_STL_alloc_proxy@PADDV?$allocator@D@_STL@@@_STL@@QAEXPADI@Z
// FUNCTION: LITHTECH 0x0042f280 ?_M_insert_overflow@?$vector@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@IAEXPAV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@2@ABV32@I@Z
// FUNCTION: LITHTECH 0x0042f490 ??0?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@QAE@ABV01@@Z
// FUNCTION: LITHTECH 0x0042f4f0 ?allocate@?$__node_alloc@$00$0A@@_STL@@SAPAXI@Z
// FUNCTION: LITHTECH 0x0042f6a0 ?_S_nsec_sleep@?$_STL_mutex_spin@$0A@@_STL@@SAXH@Z
// FUNCTION: LITHTECH 0x0042f7e0 ?_M_range_initialize@?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@AAEXPBD0Uforward_iterator_tag@2@@Z
// FUNCTION: LITHTECH 0x0042f8a0 ?_S_refill@?$__node_alloc@$00$0A@@_STL@@CAPAXI@Z
// FUNCTION: LITHTECH 0x0042fa10 ?_M_range_initialize@?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@AAEXPAD0@Z
// FUNCTION: LITHTECH 0x0042fc80 ?_S_chunk_alloc@?$__node_alloc@$00$0A@@_STL@@CAPADIAAH@Z
