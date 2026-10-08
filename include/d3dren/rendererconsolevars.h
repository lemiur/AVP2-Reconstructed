// d3d.ren renderer console variables (the older, non-template form of Jupiter's ConVar).
//
// NAME: ConVar, g_pConVars, m_DefaultVal, m_pName, m_hParam, m_pNext: Jupiter's descendant renderer
// (jupiter/runtime/render_a/src/sys/d3d/d3d_convar.h BaseConVar/ConVar, common_stuff.cpp g_pConVars): the same
// intrusive list of console variables (m_pNext, head g_pConVars, default value, name, engine handle).  The
// d3d.ren object is 0x20 bytes without a vtable and carries both an int and a float copy of the value plus two
// optional "mirror" pointers that the per-frame update (FUN_10012e4c) writes the value through.
// NAME: g_CV_<ConsoleName>: Jupiter's g_CV_ prefix (identifier taken from rendererconsolevars.h when it lists the
// console name, else g_CV_ + the console name string found in d3d.ren).
//
// The constructor is defined in convars_b (d3d.ren's FUN_100111b5); the units that only define variables call it.
#ifndef __D3DREN_RENDERERCONSOLEVARS_H__
#define __D3DREN_RENDERERCONSOLEVARS_H__

#include "d3dren/fixedpoint.h"	// RoundFloatToInt

class ConVar
{
public:
	ConVar(char *pName, float fDefault, int *pIntLink = 0, float *pFloatLink = 0);

	int		m_IntVal;		// 0x00  guess: int copy of the value (Jupiter ConVar<int>::m_Val), set to ROUND(float) by FUN_10012e4c
	float	m_FloatVal;		// 0x04  guess: float copy of the value (Jupiter ConVar<float>::m_Val), read from the engine variable
	float	m_DefaultVal;	// 0x08
	int		*m_pIntLink;		// 0x0c  guess: optional mirror variable receiving m_IntVal
	float	*m_pFloatLink;		// 0x10  guess: optional mirror variable receiving m_FloatVal
	char	*m_pName;		// 0x14
	void	*m_hParam;		// 0x18  engine console variable handle (FUN_10012dd2(name, default))
	ConVar	*m_pNext;		// 0x1c
};

// GLOBAL: D3DREN 0x100584f4
extern ConVar *g_pConVars;
// Mirror variables bound to console variables (names unknown).
// GLOBAL: D3DREN 0x10048720
extern int g_DrawWorld;
// GLOBAL: D3DREN 0x10048730
extern int g_UseDX6Commands;
// GLOBAL: D3DREN 0x10048734
extern int g_LightAddPoly;
// GLOBAL: D3DREN 0x10048738
extern int g_FogR;
// GLOBAL: D3DREN 0x1004873c
extern int g_FogG;
// GLOBAL: D3DREN 0x10048740
extern int g_FogB;
// GLOBAL: D3DREN 0x10048748
extern int g_DrawSky;
// GLOBAL: D3DREN 0x1004874c
extern int g_EnableSky;
// GLOBAL: D3DREN 0x10048750
extern int g_TextureModels;
// GLOBAL: D3DREN 0x10048754
extern int g_DrawSprites;
// GLOBAL: D3DREN 0x10048758
extern int g_DrawPolyGrids;
// GLOBAL: D3DREN 0x1004875c
extern int g_DynamicLight;
// GLOBAL: D3DREN 0x10048760
extern int g_LightModels;
// GLOBAL: D3DREN 0x10048764
extern int g_ModelFullbrite;
// GLOBAL: D3DREN 0x10048768
extern int g_ShadowLodOffset;
// GLOBAL: D3DREN 0x1004876c
extern int g_MaxModelShadows;
// GLOBAL: D3DREN 0x10048774
extern int g_DrawParticles;
// GLOBAL: D3DREN 0x10048778
extern int g_DrawModels;
// GLOBAL: D3DREN 0x1004877c
extern int g_DrawLineSystems;
// GLOBAL: D3DREN 0x10048780
extern int g_LightMap;
// GLOBAL: D3DREN 0x10048784
extern int g_Dither;
// GLOBAL: D3DREN 0x10048788
extern int g_TintModels;
// GLOBAL: D3DREN 0x1005782c
extern int g_Bilinear;
// GLOBAL: D3DREN 0x10057878
extern int g_DrawGuns;
// GLOBAL: D3DREN 0x100578ec
extern int g_Saturate;
// GLOBAL: D3DREN 0x10057994
extern int g_NoLMPages;
// GLOBAL: D3DREN 0x10057a5c
extern int g_MaxTexAspectRatio;
// GLOBAL: D3DREN 0x10057be0
extern int g_EnvMapEnable;
// GLOBAL: D3DREN 0x10057cb8
extern int g_OptimizeSurfaces;
// GLOBAL: D3DREN 0x10057d44
extern int g_GroupOffset0;
// GLOBAL: D3DREN 0x10057d48
extern int g_GroupOffset1;
// GLOBAL: D3DREN 0x10057d4c
extern int g_GroupOffset2;
// GLOBAL: D3DREN 0x10057d50
extern int g_GroupOffset3;
// GLOBAL: D3DREN 0x10057d54
extern int g_GroupOffset4;
// GLOBAL: D3DREN 0x10057d58
extern int g_GroupOffset5;
// GLOBAL: D3DREN 0x10057d5c
extern int g_GroupOffset6;
// GLOBAL: D3DREN 0x10057d60
extern int g_GroupOffset7;
// GLOBAL: D3DREN 0x10057d64
extern int g_GroupOffset8;
// GLOBAL: D3DREN 0x10057d68
extern int g_GroupOffset9;
// GLOBAL: D3DREN 0x10057dd0
extern int g_EnvMapAll;
// GLOBAL: D3DREN 0x10057e2c
extern int g_32BitTextures;
// GLOBAL: D3DREN 0x10057f00
extern int g_MaxTextureSize;
// GLOBAL: D3DREN 0x10057f70
extern int g_ModelProfile;
// GLOBAL: D3DREN 0x10057f74
extern int g_b32BitLightmaps;
// GLOBAL: D3DREN 0x10058038
extern int g_ShowSkySplits;
// GLOBAL: D3DREN 0x1005803c
extern int g_MaxModelLights;
// GLOBAL: D3DREN 0x10058118
extern int g_FilterOptimized;
// GLOBAL: D3DREN 0x1005811c
extern int g_FixTJunc;
// GLOBAL: D3DREN 0x10058478
extern int g_TripleBuffer;
// GLOBAL: D3DREN 0x1005847c
extern int g_InvertHack;
// GLOBAL: D3DREN 0x10058480
extern int g_Force1Pass;
// GLOBAL: D3DREN 0x10058484
extern int g_ModelWarble;
// GLOBAL: D3DREN 0x10058488
extern int g_RGBRast;
// GLOBAL: D3DREN 0x1005848c
extern int g_RefRast;
// GLOBAL: D3DREN 0x10058490
extern int g_MMXRast;
// GLOBAL: D3DREN 0x10058494
extern int g_TnLRast;
// GLOBAL: D3DREN 0x10058498
extern int g_Force2Pass;
// GLOBAL: D3DREN 0x1005849c
extern int g_FogEnable;
// GLOBAL: D3DREN 0x100584a4
extern int g_LockPVS;
// GLOBAL: D3DREN 0x100584a8
extern int g_ShowFullbriteModels;
// GLOBAL: D3DREN 0x100584ac
extern int g_MipmapOffset;
// GLOBAL: D3DREN 0x100584b0
extern int g_BumpMap;
// GLOBAL: D3DREN 0x100584b4
extern int g_ShowFillInfo;
// GLOBAL: D3DREN 0x100584b8
extern int g_DrawAll;
// GLOBAL: D3DREN 0x100584bc
extern int g_ShowSplits;
// GLOBAL: D3DREN 0x100584c0
extern int g_FastLight;
// GLOBAL: D3DREN 0x100584c4
extern int g_ShowTextureCounts;
// GLOBAL: D3DREN 0x100584c8
extern int g_LodOffset;
// GLOBAL: D3DREN 0x100584cc
extern int g_Wireframe;
// GLOBAL: D3DREN 0x100584d0
extern int g_ModelBoxes;
// GLOBAL: D3DREN 0x100584d4
extern int g_RenderDebug;
// GLOBAL: D3DREN 0x100584d8
extern int g_ShowPolyCounts;
// GLOBAL: D3DREN 0x100584dc
extern int g_DrawFlat;
// GLOBAL: D3DREN 0x100584e0
extern int g_LightmapsOnly;
// GLOBAL: D3DREN 0x1004871c
extern float g_EnvPanSpeed;
// GLOBAL: D3DREN 0x10048724
extern float g_ShadowZRange;
// GLOBAL: D3DREN 0x10048728
extern float g_SkyScale;
// GLOBAL: D3DREN 0x1004872c
extern float g_Gamma;
// GLOBAL: D3DREN 0x10048744
extern float g_FogFarZ;
// GLOBAL: D3DREN 0x10048770
extern float g_LodScale;
// GLOBAL: D3DREN 0x10057b10
extern float g_EnvScale;
// GLOBAL: D3DREN 0x10057d40
extern float g_SkyFogFarZ;
// GLOBAL: D3DREN 0x10057e18
extern float g_LightSaturate;
// GLOBAL: D3DREN 0x10057e1c
extern float g_WarbleScale;
// GLOBAL: D3DREN 0x10057e20
extern float g_SkyFogNearZ;
// GLOBAL: D3DREN 0x10058408
extern float g_WarbleSpeed;
// GLOBAL: D3DREN 0x100584a0
extern float g_FogNearZ;

// GLOBAL: D3DREN 0x10057a10
extern int DAT_10057a10;

// Console variables: defined in convars_a / convars_b.
// GLOBAL: D3DREN 0x1004d5a0
extern ConVar g_CV_ModelVBCacheDelay;
// GLOBAL: D3DREN 0x1004d5c0
extern ConVar g_CV_ModelDetailTextureScale;
// GLOBAL: D3DREN 0x1004d5e0
extern ConVar g_CV_ModelMinTri;
// GLOBAL: D3DREN 0x1004d600
extern ConVar g_CV_ModelVBSize;
// GLOBAL: D3DREN 0x1004d660
extern ConVar g_CV_ModelVBCache;
// GLOBAL: D3DREN 0x1004dac0
extern ConVar g_CV_SpecularScaleTest;
// GLOBAL: D3DREN 0x1004eb20
extern ConVar g_CV_SpecularPowerTest;
// GLOBAL: D3DREN 0x1004eb88
extern ConVar g_CV_ModelVBCount;
// GLOBAL: D3DREN 0x10057830
extern ConVar g_CV_WarbleSpeed;
// GLOBAL: D3DREN 0x10057858
extern ConVar g_CV_TableFog;
// GLOBAL: D3DREN 0x10057880
extern ConVar g_CV_Dither;
// GLOBAL: D3DREN 0x100578a8
extern ConVar g_CV_LightmapsOnly;
// GLOBAL: D3DREN 0x100578c8
extern ConVar g_CV_ShowPolyCounts;
// GLOBAL: D3DREN 0x100578f0
extern ConVar g_CV_32BitTextures;
// GLOBAL: D3DREN 0x10057910
extern ConVar g_CV_DynamicLight;
// GLOBAL: D3DREN 0x10057930
extern ConVar g_CV_32BitLightmaps;
// GLOBAL: D3DREN 0x10057950
extern ConVar g_CV_LightModels;
// GLOBAL: D3DREN 0x10057970
extern ConVar g_CV_InvertHack;
// GLOBAL: D3DREN 0x100579a0
extern ConVar g_CV_EnvMapAll;
// GLOBAL: D3DREN 0x100579c0
extern ConVar g_CV_FilterOptimized;
// GLOBAL: D3DREN 0x100579f0
extern ConVar g_CV_ModelBoxes;
// GLOBAL: D3DREN 0x10057a18
extern ConVar g_CV_SkyFogNearZ;
// GLOBAL: D3DREN 0x10057a38
extern ConVar g_CV_MaxTexAspectRatio;
// GLOBAL: D3DREN 0x10057a60
extern ConVar g_CV_MMXRast;
// GLOBAL: D3DREN 0x10057a80
extern ConVar g_CV_Force2Pass;
// GLOBAL: D3DREN 0x10057aa8
extern ConVar g_CV_NoLMPages;
// GLOBAL: D3DREN 0x10057ad0
extern ConVar g_CV_MipmapOffset;
// GLOBAL: D3DREN 0x10057af0
extern ConVar g_CV_LightSaturate;
// GLOBAL: D3DREN 0x10057b18
extern ConVar g_CV_ShowFullbriteModels;
// GLOBAL: D3DREN 0x10057b38
extern ConVar g_CV_EnvMapEnable;
// GLOBAL: D3DREN 0x10057b60
extern ConVar g_CV_Wireframe;
// GLOBAL: D3DREN 0x10057b80
extern ConVar g_CV_ModelFullbrite;
// GLOBAL: D3DREN 0x10057ba0
extern ConVar g_CV_ShadowZRange;
// GLOBAL: D3DREN 0x10057bc0
extern ConVar g_CV_DrawPolyGrids;
// GLOBAL: D3DREN 0x10057be8
extern ConVar g_CV_Saturate;
// GLOBAL: D3DREN 0x10057c08
extern ConVar g_CV_Force1Pass;
// GLOBAL: D3DREN 0x10057c30
extern ConVar g_CV_UseDX6Commands;
// GLOBAL: D3DREN 0x10057c50
extern ConVar g_CV_DrawGuns;
// GLOBAL: D3DREN 0x10057c70
extern ConVar g_CV_LightMap;
// GLOBAL: D3DREN 0x10057c98
extern ConVar g_CV_Gamma;
// GLOBAL: D3DREN 0x10057cc0
extern ConVar g_CV_MaxModelShadows;
// GLOBAL: D3DREN 0x10057ce0
extern ConVar g_CV_DrawFlat;
// GLOBAL: D3DREN 0x10057d00
extern ConVar g_CV_FogFarZ;
// GLOBAL: D3DREN 0x10057d20
extern ConVar g_CV_TnLRast;
// GLOBAL: D3DREN 0x10057d70
extern ConVar g_CV_ShadowLodOffset;
// GLOBAL: D3DREN 0x10057d90
extern ConVar g_CV_LodScale;
// GLOBAL: D3DREN 0x10057db0
extern ConVar g_CV_DrawWorld;
// GLOBAL: D3DREN 0x10057dd8
extern ConVar g_CV_ModelWarble;
// GLOBAL: D3DREN 0x10057df8
extern ConVar g_CV_Bilinear;
// GLOBAL: D3DREN 0x10057e38
extern ConVar g_CV_DrawModels;
// GLOBAL: D3DREN 0x10057e58
extern ConVar g_CV_DrawSky;
// GLOBAL: D3DREN 0x10057e78
extern ConVar g_CV_FogColorB;
// GLOBAL: D3DREN 0x10057e98
extern ConVar g_CV_DrawLineSystems;
// GLOBAL: D3DREN 0x10057eb8
extern ConVar g_CV_EnvPanSpeed;
// GLOBAL: D3DREN 0x10057ee0
extern ConVar g_CV_FastLight;
// GLOBAL: D3DREN 0x10057f08
extern ConVar g_CV_RenderDebug;
// GLOBAL: D3DREN 0x10057f30
extern ConVar g_CV_SkyScale;
// GLOBAL: D3DREN 0x10057f50
extern ConVar g_CV_SkyFogFarZ;
// GLOBAL: D3DREN 0x10057f78
extern ConVar g_CV_FogEnable;
// GLOBAL: D3DREN 0x10057f98
extern ConVar g_CV_RGBRast;
// GLOBAL: D3DREN 0x10057fb8
extern ConVar g_CV_TintModels;
// GLOBAL: D3DREN 0x10057fd8
extern ConVar g_CV_LightAddPoly;
// GLOBAL: D3DREN 0x10057ff8
extern ConVar g_CV_ShowSkySplits;
// GLOBAL: D3DREN 0x10058018
extern ConVar g_CV_MaxModelLights;
// GLOBAL: D3DREN 0x10058050
extern ConVar g_CV_DrawAll;
// GLOBAL: D3DREN 0x10058070
extern ConVar g_CV_DrawParticles;
// GLOBAL: D3DREN 0x10058090
extern ConVar g_CV_TripleBuffer;
// GLOBAL: D3DREN 0x100580b0
extern ConVar g_CV_ShowSplits;
// GLOBAL: D3DREN 0x100580d8
extern ConVar g_CV_EnableSky;
// GLOBAL: D3DREN 0x100580f8
extern ConVar g_CV_MaxTextureSize;
// GLOBAL: D3DREN 0x10058120
extern ConVar g_CV_BumpMap;
// GLOBAL: D3DREN 0x10058140
extern ConVar g_CV_GroupOffset7;
// GLOBAL: D3DREN 0x10058160
extern ConVar g_CV_GroupOffset6;
// GLOBAL: D3DREN 0x10058180
extern ConVar g_CV_LodOffset;
// GLOBAL: D3DREN 0x100581a0
extern ConVar g_CV_GroupOffset5;
// GLOBAL: D3DREN 0x100581c0
extern ConVar g_CV_GroupOffset4;
// GLOBAL: D3DREN 0x100581e0
extern ConVar g_CV_ModelProfile;
// GLOBAL: D3DREN 0x10058200
extern ConVar g_CV_GroupOffset9;
// GLOBAL: D3DREN 0x10058220
extern ConVar g_CV_GroupOffset8;
// GLOBAL: D3DREN 0x10058240
extern ConVar g_CV_FogColorG;
// GLOBAL: D3DREN 0x10058260
extern ConVar g_CV_FixTJunc;
// GLOBAL: D3DREN 0x10058280
extern ConVar g_CV_WarbleScale;
// GLOBAL: D3DREN 0x100582a0
extern ConVar g_CV_OptimizeSurfaces;
// GLOBAL: D3DREN 0x100582c0
extern ConVar g_CV_EnvScale;
// GLOBAL: D3DREN 0x100582e8
extern ConVar g_CV_GroupOffset3;
// GLOBAL: D3DREN 0x10058308
extern ConVar g_CV_GroupOffset2;
// GLOBAL: D3DREN 0x10058328
extern ConVar g_CV_GroupOffset1;
// GLOBAL: D3DREN 0x10058348
extern ConVar g_CV_GroupOffset0;
// GLOBAL: D3DREN 0x10058368
extern ConVar g_CV_ShowTextureCounts;
// GLOBAL: D3DREN 0x10058388
extern ConVar g_CV_DrawSprites;
// GLOBAL: D3DREN 0x100583a8
extern ConVar g_CV_TextureModels;
// GLOBAL: D3DREN 0x100583c8
extern ConVar g_CV_ShowFillInfo;
// GLOBAL: D3DREN 0x100583e8
extern ConVar g_CV_FogColorR;
// GLOBAL: D3DREN 0x10058410
extern ConVar g_CV_LockPVS;
// GLOBAL: D3DREN 0x10058430
extern ConVar g_CV_FogNearZ;
// GLOBAL: D3DREN 0x10058450
extern ConVar g_CV_RefRast;

// BEGIN call-style console variables (constructed by static initialisers that call the constructor out of line;
// units convars_a, convars_c, convars_d)
// GLOBAL: D3DREN 0x100513e8
extern ConVar g_CV_DetailTextureAdd;
// GLOBAL: D3DREN 0x10051408
extern ConVar g_CV_EnvMapWorld;
// GLOBAL: D3DREN 0x10051428
extern ConVar g_CV_FixSparkleys;
// GLOBAL: D3DREN 0x10051448
extern ConVar g_CV_DetailTextures;
// GLOBAL: D3DREN 0x10051468
extern ConVar g_CV_LMAnim;
// GLOBAL: D3DREN 0x10051488
extern ConVar g_CV_LMFullBright;
// GLOBAL: D3DREN 0x100518b0
extern ConVar g_CV_DetailTextureAngle;
// GLOBAL: D3DREN 0x100528e0
extern ConVar g_CV_ModelUseTnL;
// GLOBAL: D3DREN 0x10053238
extern ConVar g_CV_ExtraFOVYOffset;
// GLOBAL: D3DREN 0x10053258
extern ConVar g_CV_ModelSaturation;
// GLOBAL: D3DREN 0x10053290
extern ConVar g_CV_LightModelSprites;
// GLOBAL: D3DREN 0x100532b0
extern ConVar g_CV_ModelCacheRigid;
// GLOBAL: D3DREN 0x100536d0
extern ConVar g_CV_ModelLODOffset;
// GLOBAL: D3DREN 0x100536f0
extern ConVar g_CV_DrawModelsRigid;
// GLOBAL: D3DREN 0x10053710
extern ConVar g_CV_ExtraFOVXOffset;
// GLOBAL: D3DREN 0x100537b0
extern ConVar g_CV_ModelFovTest;
// GLOBAL: D3DREN 0x100537d0
extern ConVar g_CV_ModelApplySun;
// GLOBAL: D3DREN 0x100537f0
extern ConVar g_CV_ModelLODBlendDist;
// GLOBAL: D3DREN 0x10053810
extern ConVar g_CV_ModelLODBlendEnable;
// GLOBAL: D3DREN 0x10053830
extern ConVar g_CV_ModelZoomScale;
// GLOBAL: D3DREN 0x10054850
extern ConVar g_CV_ModelSunVariance;
// GLOBAL: D3DREN 0x10054890
extern ConVar g_CV_ReallyCloseNearZ;
// GLOBAL: D3DREN 0x10055cb0
extern ConVar g_CV_NearZ;
// GLOBAL: D3DREN 0x100585c0
extern ConVar g_CV_ShowTexInfo;
// GLOBAL: D3DREN 0x100585e0
extern ConVar g_CV_VFogMaxYVal;
// GLOBAL: D3DREN 0x10058600
extern ConVar g_CV_VFogDensity;
// GLOBAL: D3DREN 0x10058628
extern ConVar g_CV_DrawWorldTree;
// GLOBAL: D3DREN 0x10058738
extern ConVar g_CV_VFogMinYVal;
// GLOBAL: D3DREN 0x10058780
extern ConVar g_CV_RenderToFront;
// GLOBAL: D3DREN 0x100587a0
extern ConVar g_CV_DrawTerrainSections;
// GLOBAL: D3DREN 0x100587c0
extern ConVar g_CV_AlphaTest;
// GLOBAL: D3DREN 0x10058c00
extern ConVar g_CV_VFog;
// GLOBAL: D3DREN 0x10058c48
extern ConVar g_CV_ShowPortalBounds;
// GLOBAL: D3DREN 0x10058c70
extern ConVar g_CV_PortalLightmap;
// GLOBAL: D3DREN 0x10058cb8
extern ConVar g_CV_DrawPortals;
// GLOBAL: D3DREN 0x10058ce0
extern ConVar g_CV_VFogMax;
// GLOBAL: D3DREN 0x1005a310
extern ConVar g_CV_VFogMaxY;
// GLOBAL: D3DREN 0x1005a348
extern ConVar g_CV_VFogMinY;
// GLOBAL: D3DREN 0x1005c7e8
extern ConVar g_CV_MipMapBias;
// GLOBAL: D3DREN 0x1005c818
extern ConVar g_CV_UseD3DClip;
// GLOBAL: D3DREN 0x1005c9a8
extern ConVar g_CV_Anisotropic;
// GLOBAL: D3DREN 0x1005cdf8
extern ConVar g_CV_Trilinear;
// GLOBAL: D3DREN 0x100606d8
extern ConVar g_CV_LockOnFlip;
// GLOBAL: D3DREN 0x100617b8
extern ConVar g_CV_S3TCEnable;
// GLOBAL: D3DREN 0x10064350
extern ConVar g_CV_DrawCanvases;
// GLOBAL: D3DREN 0x10065bc0
extern ConVar g_CV_MultipassGouraud;
// GLOBAL: D3DREN 0x10067bf0
extern ConVar g_CV_ModelTexture;
// GLOBAL: D3DREN 0x10067c10
extern ConVar g_CV_ModelSpecular;
// GLOBAL: D3DREN 0x10069040
extern ConVar g_CV_ModelShadowAlpha;
// GLOBAL: D3DREN 0x10069060
extern ConVar g_CV_ModelShadowProj;
// GLOBAL: D3DREN 0x10069490
extern ConVar g_CV_ModelShadowOffset;
// GLOBAL: D3DREN 0x1006a4b0
extern ConVar g_CV_ModelShadowProjShow;
// GLOBAL: D3DREN 0x1006a4d0
extern ConVar g_CV_ModelShadowProjLOD;
// GLOBAL: D3DREN 0x1006a4f0
extern ConVar g_CV_ModelShadowProjRes;
// GLOBAL: D3DREN 0x1006b910
extern ConVar g_CV_DrawSorted;
// GLOBAL: D3DREN 0x1006bd38
extern ConVar g_CV_SortProfile;
// GLOBAL: D3DREN 0x1006cd78
extern ConVar g_CV_PSDestBlend;
// GLOBAL: D3DREN 0x1006d198
extern ConVar g_CV_PSSrcBlend;
// GLOBAL: D3DREN 0x1006e7c0
extern ConVar g_CV_DrawPolyMgr;
// GLOBAL: D3DREN 0x1006e7e0
extern ConVar g_CV_TestLightmap;
// GLOBAL: D3DREN 0x1006e800
extern ConVar g_CV_TestGouraud;
// GLOBAL: D3DREN 0x1006e820
extern ConVar g_CV_DetailTextureScale;
// GLOBAL: D3DREN 0x10071860
extern ConVar g_CV_EnvMapPolyGrids;
// GLOBAL: D3DREN 0x10071880
extern ConVar g_CV_BlockersOnly;
// GLOBAL: D3DREN 0x100718a0
extern ConVar g_CV_PortalGraph;
// GLOBAL: D3DREN 0x10071cc0
extern ConVar g_CV_PortalFlow;
// GLOBAL: D3DREN 0x10071ce0
extern ConVar g_CV_PortalsOnly;
// GLOBAL: D3DREN 0x10073990
extern ConVar g_CV_AllSkyPortals;
// GLOBAL: D3DREN 0x100752b0
extern ConVar g_CV_DrawWorldModels;
// GLOBAL: D3DREN 0x10076768
extern ConVar g_CV_LMDynamicScale;
// GLOBAL: D3DREN 0x10076788
extern ConVar g_CV_LMDynamic;
// GLOBAL: D3DREN 0x10076ba8
extern ConVar g_CV_LMDynamicSize;
// GLOBAL: D3DREN 0x100782c8
extern ConVar g_CV_LMAnimStatic;
// END call-style console variables

#endif
