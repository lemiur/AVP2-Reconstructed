// d3d.ren sys/d3d/common_stuff (0x1001116b-0x10012ef4): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// ConVar block, dalloc/dfree/dsi_ConsolePrint, d3d_*ConsoleVariables, then StrUpperInPlace 0x10012eaf and CountSetBits
// 0x10012edf (no marker: could be a 69-byte object of their own).
// FLAGS: /O1 /Ob2
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <windows.h>
#include "ltbasedefs.h"
#include "counter.h"
#include "../../../../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "d3dren/common_stuff.h"
#include "d3dren/rendererconsolevars.h"
#include "d3dren/viewparams.h"
#include "d3dren/scenedesc.h"
#include "d3dren/tlvertex.h"
#include "d3dren/d3ddevice.h"
#include "pixelformat.h"

// ------------------------------------------------------------------ //
// The lists of polygons touched by dynamic lights (names unknown, shapes in the comments).
// ------------------------------------------------------------------ //

// The per-poly record of a dynamic light touching it (StructBank DAT_10056220, 0x14 bytes) and the list of lit polys
// (StructBank DAT_10056240, 8 bytes); the poly's list head is WorldPoly+0x30 (padding in the shared de_objects.h).
struct UnkType_PolyLight
{
	UnkType_PolyLight	*m_pNext;		// 0x00
	LTObject			*m_pLight;		// 0x04
	LTVector			m_Pos;			// 0x08 the light position in the world model space
};

#define WORLDPOLY_LIGHTS(p)	(*(UnkType_PolyLight**)((uint8*)(p) + 0x30))

// Functions of the device bring-up unit (sys/d3d/d3d_init) and of this unit further down.
void FUN_10012eaf(char *pStr);

// Stores a function address in a RenderStruct slot.  The slots are typed in include/renderstruct.h, but some of them are
// padding there (GetOptimized2DBlend/Color, IsInOptimized2D and the unnamed 0xc8) and several functions of the units that
// define them take Jupiter-style arguments, so the store goes through void *.
#define RS_SET(member, fn)	(*(void **)&pStruct->member = (void *)(fn))
#define RS_SET_PAD(offset, fn)	(*(void **)((uint8 *)pStruct + (offset)) = (void *)(fn))
extern RenderStruct *g_pStruct;

// FUNCTION: D3DREN 0x1001116b _$E2
// FUNCTION: D3DREN 0x10011170 _$E1
// GLOBAL: D3DREN 0x10057858
ConVar g_CV_TableFog("TableFog", 0.0f);
// FUNCTION: D3DREN 0x100111b5
ConVar::ConVar(char *pName, float fDefault, int *pIntLink, float *pFloatLink)
{
	m_IntVal = 0;
	m_FloatVal = 0.0f;
	m_DefaultVal = fDefault;
	m_pIntLink = pIntLink;
	m_pFloatLink = pFloatLink;
	m_pName = pName;
	m_hParam = 0;
	m_pNext = g_pConVars;
	g_pConVars = this;
}
// FUNCTION: D3DREN 0x100111f0 _$E5
// FUNCTION: D3DREN 0x100111f5 _$E4
// GLOBAL: D3DREN 0x10057aa8
ConVar g_CV_NoLMPages("NoLMPages", 0.0f, &DAT_10057994);
// FUNCTION: D3DREN 0x1001123f _$E8
// FUNCTION: D3DREN 0x10011244 _$E7
// GLOBAL: D3DREN 0x100581e0
ConVar g_CV_ModelProfile("ModelProfile", 0.0f, &DAT_10057f70);
// FUNCTION: D3DREN 0x1001128e _$E11
// FUNCTION: D3DREN 0x10011293 _$E10
// GLOBAL: D3DREN 0x10058018
ConVar g_CV_MaxModelLights("MaxModelLights", 4.0f, &DAT_1005803c);
// FUNCTION: D3DREN 0x100112df _$E14
// FUNCTION: D3DREN 0x100112e4 _$E13
// GLOBAL: D3DREN 0x10057a38
ConVar g_CV_MaxTexAspectRatio("MaxTexAspectRatio", 8.0f, &DAT_10057a5c);
// FUNCTION: D3DREN 0x10011330 _$E17
// FUNCTION: D3DREN 0x10011335 _$E16
// GLOBAL: D3DREN 0x100578f0
ConVar g_CV_32BitTextures("32BitTextures", 0.0f, &DAT_10057e2c);
// FUNCTION: D3DREN 0x1001137f _$E20
// FUNCTION: D3DREN 0x10011384 _$E19
// GLOBAL: D3DREN 0x10057930
ConVar g_CV_32BitLightmaps("32BitLightMaps", 0.0f, &g_b32BitLightmaps);
// FUNCTION: D3DREN 0x100113ce _$E23
// FUNCTION: D3DREN 0x100113d3 _$E22
// GLOBAL: D3DREN 0x100580f8
ConVar g_CV_MaxTextureSize("MaxTextureSize", 16384.0f, &DAT_10057f00);
// FUNCTION: D3DREN 0x1001141f _$E26
// FUNCTION: D3DREN 0x10011424 _$E25
// GLOBAL: D3DREN 0x10057c50
ConVar g_CV_DrawGuns("DrawGuns", 1.0f, &DAT_10057878);
// FUNCTION: D3DREN 0x1001146e _$E29
// FUNCTION: D3DREN 0x10011473 _$E28
// GLOBAL: D3DREN 0x10057fb8
ConVar g_CV_TintModels("TintModels", 1.0f, &DAT_10048788);
// FUNCTION: D3DREN 0x100114bd _$E32
// FUNCTION: D3DREN 0x100114c2 _$E31
// GLOBAL: D3DREN 0x10057be8
ConVar g_CV_Saturate("Saturate", 0.0f, &DAT_100578ec);
// FUNCTION: D3DREN 0x1001150c _$E35
// FUNCTION: D3DREN 0x10011511 _$E34
// GLOBAL: D3DREN 0x10057df8
ConVar g_CV_Bilinear("Bilinear", 1.0f, &DAT_1005782c);
// FUNCTION: D3DREN 0x1001155b _$E38
// FUNCTION: D3DREN 0x10011560 _$E37
// GLOBAL: D3DREN 0x10057b38
ConVar g_CV_EnvMapEnable("EnvMapEnable", 0.0f, &DAT_10057be0);
// FUNCTION: D3DREN 0x100115aa _$E41
// FUNCTION: D3DREN 0x100115af _$E40
// GLOBAL: D3DREN 0x10058260
ConVar g_CV_FixTJunc("FixTJunc", 0.0f, &DAT_1005811c);
// FUNCTION: D3DREN 0x100115f9 _$E44
// FUNCTION: D3DREN 0x100115fe _$E43
// GLOBAL: D3DREN 0x10057af0
ConVar g_CV_LightSaturate("LightSaturate", 1.0f, 0, &DAT_10057e18);
// FUNCTION: D3DREN 0x10011648 _$E47
// FUNCTION: D3DREN 0x1001164d _$E46
// GLOBAL: D3DREN 0x100579c0
ConVar g_CV_FilterOptimized("FilterOptimized", 0.0f, &DAT_10058118);
// FUNCTION: D3DREN 0x10011697 _$E50
// FUNCTION: D3DREN 0x1001169c _$E49
// GLOBAL: D3DREN 0x100582a0
ConVar g_CV_OptimizeSurfaces("OptimizeSurfaces", 0.0f, &DAT_10057cb8);
// FUNCTION: D3DREN 0x100116e6 _$E53
// FUNCTION: D3DREN 0x100116eb _$E52
// GLOBAL: D3DREN 0x10057ff8
ConVar g_CV_ShowSkySplits("ShowSkySplits", 0.0f, &DAT_10058038);
// FUNCTION: D3DREN 0x10011735 _$E56
// FUNCTION: D3DREN 0x1001173a _$E55
// GLOBAL: D3DREN 0x10057830
ConVar g_CV_WarbleSpeed("WarbleSpeed", 25.0f, 0, &DAT_10058408);
// FUNCTION: D3DREN 0x10011786 _$E59
// FUNCTION: D3DREN 0x1001178b _$E58
// GLOBAL: D3DREN 0x10058280
ConVar g_CV_WarbleScale("WarbleScale", 0.92f, 0, &DAT_10057e1c);
// FUNCTION: D3DREN 0x100117d7 _$E62
// FUNCTION: D3DREN 0x100117dc _$E61
// GLOBAL: D3DREN 0x100579a0
ConVar g_CV_EnvMapAll("EnvMapAll", 0.0f, &DAT_10057dd0);
// FUNCTION: D3DREN 0x10011826 _$E65
// FUNCTION: D3DREN 0x1001182b _$E64
// GLOBAL: D3DREN 0x10057eb8
ConVar g_CV_EnvPanSpeed("EnvPanSpeed", 0.0005f, 0, &DAT_1004871c);
// FUNCTION: D3DREN 0x10011877 _$E68
// FUNCTION: D3DREN 0x1001187c _$E67
// GLOBAL: D3DREN 0x100582c0
ConVar g_CV_EnvScale("EnvScale", 1.0f, 0, &DAT_10057b10);
// FUNCTION: D3DREN 0x100118c6 _$E71
// FUNCTION: D3DREN 0x100118cb _$E70
// GLOBAL: D3DREN 0x10057db0
ConVar g_CV_DrawWorld("DrawWorld", 1.0f, &DAT_10048720);
// FUNCTION: D3DREN 0x10011915 _$E74
// FUNCTION: D3DREN 0x1001191a _$E73
// GLOBAL: D3DREN 0x10057ba0
ConVar g_CV_ShadowZRange("ShadowZRange", 17.0f, 0, &DAT_10048724);
// FUNCTION: D3DREN 0x10011966 _$E77
// FUNCTION: D3DREN 0x1001196b _$E76
// GLOBAL: D3DREN 0x10057f30
ConVar g_CV_SkyScale("SkyScale", 1.0f, 0, &DAT_10048728);
// FUNCTION: D3DREN 0x100119b5 _$E80
// FUNCTION: D3DREN 0x100119ba _$E79
// GLOBAL: D3DREN 0x10057c98
ConVar g_CV_Gamma("Gamma", 100.0f, 0, &DAT_1004872c);
// FUNCTION: D3DREN 0x10011a06 _$E83
// FUNCTION: D3DREN 0x10011a0b _$E82
// GLOBAL: D3DREN 0x10058348
ConVar g_CV_GroupOffset0("GroupOffset0", 0.0f, &DAT_10057d44);
// FUNCTION: D3DREN 0x10011a55 _$E86
// FUNCTION: D3DREN 0x10011a5a _$E85
// GLOBAL: D3DREN 0x10058328
ConVar g_CV_GroupOffset1("GroupOffset1", 0.0f, &DAT_10057d48);
// FUNCTION: D3DREN 0x10011aa4 _$E89
// FUNCTION: D3DREN 0x10011aa9 _$E88
// GLOBAL: D3DREN 0x10058308
ConVar g_CV_GroupOffset2("GroupOffset2", 0.0f, &DAT_10057d4c);
// FUNCTION: D3DREN 0x10011af3 _$E92
// FUNCTION: D3DREN 0x10011af8 _$E91
// GLOBAL: D3DREN 0x100582e8
ConVar g_CV_GroupOffset3("GroupOffset3", 0.0f, &DAT_10057d50);
// FUNCTION: D3DREN 0x10011b42 _$E95
// FUNCTION: D3DREN 0x10011b47 _$E94
// GLOBAL: D3DREN 0x100581c0
ConVar g_CV_GroupOffset4("GroupOffset4", 0.0f, &DAT_10057d54);
// FUNCTION: D3DREN 0x10011b91 _$E98
// FUNCTION: D3DREN 0x10011b96 _$E97
// GLOBAL: D3DREN 0x100581a0
ConVar g_CV_GroupOffset5("GroupOffset5", 0.0f, &DAT_10057d58);
// FUNCTION: D3DREN 0x10011be0 _$E101
// FUNCTION: D3DREN 0x10011be5 _$E100
// GLOBAL: D3DREN 0x10058160
ConVar g_CV_GroupOffset6("GroupOffset6", 0.0f, &DAT_10057d5c);
// FUNCTION: D3DREN 0x10011c2f _$E104
// FUNCTION: D3DREN 0x10011c34 _$E103
// GLOBAL: D3DREN 0x10058140
ConVar g_CV_GroupOffset7("GroupOffset7", 0.0f, &DAT_10057d60);
// FUNCTION: D3DREN 0x10011c7e _$E107
// FUNCTION: D3DREN 0x10011c83 _$E106
// GLOBAL: D3DREN 0x10058220
ConVar g_CV_GroupOffset8("GroupOffset8", 0.0f, &DAT_10057d64);
// FUNCTION: D3DREN 0x10011ccd _$E110
// FUNCTION: D3DREN 0x10011cd2 _$E109
// GLOBAL: D3DREN 0x10058200
ConVar g_CV_GroupOffset9("GroupOffset9", 0.0f, &DAT_10057d68);
// FUNCTION: D3DREN 0x10011d1c _$E113
// FUNCTION: D3DREN 0x10011d21 _$E112
// GLOBAL: D3DREN 0x10058090
ConVar g_CV_TripleBuffer("TripleBuffer", 0.0f, &DAT_10058478);
// FUNCTION: D3DREN 0x10011d6b _$E116
// FUNCTION: D3DREN 0x10011d70 _$E115
// GLOBAL: D3DREN 0x10057c30
ConVar g_CV_UseDX6Commands("UseDX6Commands", 1.0f, &DAT_10048730);
// FUNCTION: D3DREN 0x10011dba _$E119
// FUNCTION: D3DREN 0x10011dbf _$E118
// GLOBAL: D3DREN 0x10057970
ConVar g_CV_InvertHack("InvertHack", 0.0f, &DAT_1005847c);
// FUNCTION: D3DREN 0x10011e09 _$E122
// FUNCTION: D3DREN 0x10011e0e _$E121
// GLOBAL: D3DREN 0x10057c08
ConVar g_CV_Force1Pass("Force1Pass", 0.0f, &DAT_10058480);
// FUNCTION: D3DREN 0x10011e58 _$E125
// FUNCTION: D3DREN 0x10011e5d _$E124
// GLOBAL: D3DREN 0x10057fd8
ConVar g_CV_LightAddPoly("LightAddPoly", 1.0f, &DAT_10048734);
// FUNCTION: D3DREN 0x10011ea7 _$E128
// FUNCTION: D3DREN 0x10011eac _$E127
// GLOBAL: D3DREN 0x10057dd8
ConVar g_CV_ModelWarble("ModelWarble", 0.0f, &DAT_10058484);
// FUNCTION: D3DREN 0x10011ef6 _$E131
// FUNCTION: D3DREN 0x10011efb _$E130
// GLOBAL: D3DREN 0x10057a60
ConVar g_CV_MMXRast("MMXRast", 0.0f, &DAT_10058490);
// FUNCTION: D3DREN 0x10011f45 _$E134
// FUNCTION: D3DREN 0x10011f4a _$E133
// GLOBAL: D3DREN 0x10057f98
ConVar g_CV_RGBRast("RGBRast", 0.0f, &DAT_10058488);
// FUNCTION: D3DREN 0x10011f94 _$E137
// FUNCTION: D3DREN 0x10011f99 _$E136
// GLOBAL: D3DREN 0x10058450
ConVar g_CV_RefRast("RefRast", 0.0f, &DAT_1005848c);
// FUNCTION: D3DREN 0x10011fe3 _$E140
// FUNCTION: D3DREN 0x10011fe8 _$E139
// GLOBAL: D3DREN 0x10057d20
ConVar g_CV_TnLRast("TnLRast", 0.0f, &DAT_10058494);
// FUNCTION: D3DREN 0x10012032 _$E143
// FUNCTION: D3DREN 0x10012037 _$E142
// GLOBAL: D3DREN 0x10057a80
ConVar g_CV_Force2Pass("Force2Pass", 0.0f, &DAT_10058498);
// FUNCTION: D3DREN 0x10012081 _$E146
// FUNCTION: D3DREN 0x10012086 _$E145
// GLOBAL: D3DREN 0x10057f78
ConVar g_CV_FogEnable("FogEnable", 0.0f, &DAT_1005849c);
// FUNCTION: D3DREN 0x100120d0 _$E149
// FUNCTION: D3DREN 0x100120d5 _$E148
// GLOBAL: D3DREN 0x100583e8
ConVar g_CV_FogColorR("FogR", 255.0f, &DAT_10048738);
// FUNCTION: D3DREN 0x10012121 _$E152
// FUNCTION: D3DREN 0x10012126 _$E151
// GLOBAL: D3DREN 0x10058240
ConVar g_CV_FogColorG("FogG", 255.0f, &DAT_1004873c);
// FUNCTION: D3DREN 0x10012172 _$E155
// FUNCTION: D3DREN 0x10012177 _$E154
// GLOBAL: D3DREN 0x10057e78
ConVar g_CV_FogColorB("FogB", 255.0f, &DAT_10048740);
// FUNCTION: D3DREN 0x100121c3 _$E158
// FUNCTION: D3DREN 0x100121c8 _$E157
// GLOBAL: D3DREN 0x10058430
ConVar g_CV_FogNearZ("FogNearZ", 0.0f, 0, &DAT_100584a0);
// FUNCTION: D3DREN 0x10012212 _$E161
// FUNCTION: D3DREN 0x10012217 _$E160
// GLOBAL: D3DREN 0x10057d00
ConVar g_CV_FogFarZ("FogFarZ", 2000.0f, 0, &DAT_10048744);
// FUNCTION: D3DREN 0x10012263 _$E164
// FUNCTION: D3DREN 0x10012268 _$E163
// GLOBAL: D3DREN 0x10057a18
ConVar g_CV_SkyFogNearZ("SkyFogNearZ", 0.0f, 0, &DAT_10057e20);
// FUNCTION: D3DREN 0x100122b2 _$E167
// FUNCTION: D3DREN 0x100122b7 _$E166
// GLOBAL: D3DREN 0x10057f50
ConVar g_CV_SkyFogFarZ("SkyFogFarZ", 2000.0f, 0, &DAT_10057d40);
// FUNCTION: D3DREN 0x10012303 _$E170
// FUNCTION: D3DREN 0x10012308 _$E169
// GLOBAL: D3DREN 0x10057ad0
ConVar g_CV_MipmapOffset("MipmapOffset", 0.0f, &DAT_100584ac);
// FUNCTION: D3DREN 0x10012352 _$E173
// FUNCTION: D3DREN 0x10012357 _$E172
// GLOBAL: D3DREN 0x10058410
ConVar g_CV_LockPVS("LockPVS", 0.0f, &DAT_100584a4);
// FUNCTION: D3DREN 0x100123a1 _$E176
// FUNCTION: D3DREN 0x100123a6 _$E175
// GLOBAL: D3DREN 0x10057b18
ConVar g_CV_ShowFullbriteModels("ShowFullbriteModels", 0.0f, &DAT_100584a8);
// FUNCTION: D3DREN 0x100123f0 _$E179
// FUNCTION: D3DREN 0x100123f5 _$E178
// GLOBAL: D3DREN 0x10058120
ConVar g_CV_BumpMap("BumpMap", 0.0f, &DAT_100584b0);
// FUNCTION: D3DREN 0x1001243f _$E182
// FUNCTION: D3DREN 0x10012444 _$E181
// GLOBAL: D3DREN 0x10057e58
ConVar g_CV_DrawSky("DrawSky", 1.0f, &DAT_10048748);
// FUNCTION: D3DREN 0x1001248e _$E185
// FUNCTION: D3DREN 0x10012493 _$E184
// GLOBAL: D3DREN 0x100580d8
ConVar g_CV_EnableSky("EnableSky", 1.0f, &DAT_1004874c);
// FUNCTION: D3DREN 0x100124dd _$E188
// FUNCTION: D3DREN 0x100124e2 _$E187
// GLOBAL: D3DREN 0x100583a8
ConVar g_CV_TextureModels("TextureModels", 1.0f, &DAT_10048750);
// FUNCTION: D3DREN 0x1001252c _$E191
// FUNCTION: D3DREN 0x10012531 _$E190
// GLOBAL: D3DREN 0x100583c8
ConVar g_CV_ShowFillInfo("ShowFillInfo", 0.0f, &DAT_100584b4);
// FUNCTION: D3DREN 0x1001257b _$E194
// FUNCTION: D3DREN 0x10012580 _$E193
// GLOBAL: D3DREN 0x10058050
ConVar g_CV_DrawAll("DrawAll", 0.0f, &DAT_100584b8);
// FUNCTION: D3DREN 0x100125ca _$E197
// FUNCTION: D3DREN 0x100125cf _$E196
// GLOBAL: D3DREN 0x10058388
ConVar g_CV_DrawSprites("DrawSprites", 1.0f, &DAT_10048754);
// FUNCTION: D3DREN 0x10012619 _$E200
// FUNCTION: D3DREN 0x1001261e _$E199
// GLOBAL: D3DREN 0x10057bc0
ConVar g_CV_DrawPolyGrids("DrawPolyGrids", 1.0f, &DAT_10048758);
// FUNCTION: D3DREN 0x10012668 _$E203
// FUNCTION: D3DREN 0x1001266d _$E202
// GLOBAL: D3DREN 0x10058070
ConVar g_CV_DrawParticles("DrawParticles", 1.0f, &DAT_10048774);
// FUNCTION: D3DREN 0x100126b7 _$E206
// FUNCTION: D3DREN 0x100126bc _$E205
// GLOBAL: D3DREN 0x10057e38
ConVar g_CV_DrawModels("DrawModels", 1.0f, &DAT_10048778);
// FUNCTION: D3DREN 0x10012706 _$E209
// FUNCTION: D3DREN 0x1001270b _$E208
// GLOBAL: D3DREN 0x10057e98
ConVar g_CV_DrawLineSystems("DrawLineSystems", 1.0f, &DAT_1004877c);
// FUNCTION: D3DREN 0x10012755 _$E212
// FUNCTION: D3DREN 0x1001275a _$E211
// GLOBAL: D3DREN 0x100580b0
ConVar g_CV_ShowSplits("ShowSplits", 0.0f, &DAT_100584bc);
// FUNCTION: D3DREN 0x100127a4 _$E215
// FUNCTION: D3DREN 0x100127a9 _$E214
// GLOBAL: D3DREN 0x10057910
ConVar g_CV_DynamicLight("DynamicLight", 1.0f, &DAT_1004875c);
// FUNCTION: D3DREN 0x100127f3 _$E218
// FUNCTION: D3DREN 0x100127f8 _$E217
// GLOBAL: D3DREN 0x10057ee0
ConVar g_CV_FastLight("FastLight", 0.0f, &DAT_100584c0);
// FUNCTION: D3DREN 0x10012842 _$E221
// FUNCTION: D3DREN 0x10012847 _$E220
// GLOBAL: D3DREN 0x10057950
ConVar g_CV_LightModels("LightModels", 1.0f, &DAT_10048760);
// FUNCTION: D3DREN 0x10012891 _$E224
// FUNCTION: D3DREN 0x10012896 _$E223
// GLOBAL: D3DREN 0x10057b80
ConVar g_CV_ModelFullbrite("ModelFullbrite", 1.0f, &DAT_10048764);
// FUNCTION: D3DREN 0x100128e0 _$E227
// FUNCTION: D3DREN 0x100128e5 _$E226
// GLOBAL: D3DREN 0x10058368
ConVar g_CV_ShowTextureCounts("ShowTextureCounts", 0.0f, &DAT_100584c4);
// FUNCTION: D3DREN 0x1001292f _$E230
// FUNCTION: D3DREN 0x10012934 _$E229
// GLOBAL: D3DREN 0x10057d70
ConVar g_CV_ShadowLodOffset("ShadowLodOffset", 100.0f, &DAT_10048768);
// FUNCTION: D3DREN 0x10012980 _$E233
// FUNCTION: D3DREN 0x10012985 _$E232
// GLOBAL: D3DREN 0x10057cc0
ConVar g_CV_MaxModelShadows("MaxModelShadows", 1.0f, &DAT_1004876c);
// FUNCTION: D3DREN 0x100129cf _$E236
// FUNCTION: D3DREN 0x100129d4 _$E235
// GLOBAL: D3DREN 0x10057d90
ConVar g_CV_LodScale("LodScale", 1.0f, 0, &DAT_10048770);
// FUNCTION: D3DREN 0x10012a1e _$E239
// FUNCTION: D3DREN 0x10012a23 _$E238
// GLOBAL: D3DREN 0x10058180
ConVar g_CV_LodOffset("LodOffset", 0.0f, &DAT_100584c8);
// FUNCTION: D3DREN 0x10012a6d _$E242
// FUNCTION: D3DREN 0x10012a72 _$E241
// GLOBAL: D3DREN 0x10057b60
ConVar g_CV_Wireframe("Wireframe", 0.0f, &DAT_100584cc);
// FUNCTION: D3DREN 0x10012abc _$E245
// FUNCTION: D3DREN 0x10012ac1 _$E244
// GLOBAL: D3DREN 0x100579f0
ConVar g_CV_ModelBoxes("ModelBoxes", 0.0f, &DAT_100584d0);
// FUNCTION: D3DREN 0x10012b0b _$E248
// FUNCTION: D3DREN 0x10012b10 _$E247
// GLOBAL: D3DREN 0x10057f08
ConVar g_CV_RenderDebug("RenderDebug", 0.0f, &DAT_100584d4);
// FUNCTION: D3DREN 0x10012b5a _$E251
// FUNCTION: D3DREN 0x10012b5f _$E250
// GLOBAL: D3DREN 0x100578c8
ConVar g_CV_ShowPolyCounts("ShowPolyCounts", 0.0f, &DAT_100584d8);
// FUNCTION: D3DREN 0x10012ba9 _$E254
// FUNCTION: D3DREN 0x10012bae _$E253
// GLOBAL: D3DREN 0x10057c70
ConVar g_CV_LightMap("LightMap", 1.0f, &DAT_10048780);
// FUNCTION: D3DREN 0x10012bf8 _$E257
// FUNCTION: D3DREN 0x10012bfd _$E256
// GLOBAL: D3DREN 0x10057ce0
ConVar g_CV_DrawFlat("DrawFlat", 0.0f, &DAT_100584dc);
// FUNCTION: D3DREN 0x10012c47 _$E260
// FUNCTION: D3DREN 0x10012c4c _$E259
// GLOBAL: D3DREN 0x100578a8
ConVar g_CV_LightmapsOnly("LightmapsOnly", 0.0f, &DAT_100584e0);
// FUNCTION: D3DREN 0x10012c96 _$E263
// FUNCTION: D3DREN 0x10012c9b _$E262
// GLOBAL: D3DREN 0x10057880
ConVar g_CV_Dither("Dither", 1.0f, &DAT_10048784);

// These helpers are called out of line by the objects that precede this one in the exe (common_draw, common_init), which only see
// the declarations in common_stuff.h.
// FUNCTION: D3DREN 0x10012ce5
void *dalloc(size_t size)
{
	if (g_pStruct)
		return g_pStruct->Alloc(size);
	return malloc(size);
}

// FUNCTION: D3DREN 0x10012cfe
void *dalloc_z(size_t size)
{
	void *ptr;

	if (g_pStruct)
		ptr = g_pStruct->Alloc(size);
	else
		ptr = malloc(size);
	if (ptr)
	{
		memset(ptr, 0, size);
	}
	else
	{
		if (g_pStruct)
			g_pStruct->ConsolePrint("d3drender.dll: out of memory");
	}
	return ptr;
}

// FUNCTION: D3DREN 0x10012d44
void dfree(void *ptr)
{
	if (g_pStruct)
		g_pStruct->Free(ptr);
	else
		free(ptr);
}

// NAME: dsi_ConsolePrint (medium): see common_stuff.h
// FUNCTION: D3DREN 0x10012d5d
void dsi_ConsolePrint(const char *pMsg, ...)
{
	va_list marker;
	char msg[256];

	va_start(marker, pMsg);
	_vsnprintf(msg, 255, pMsg, marker);
	va_end(marker);
	g_pStruct->ConsolePrint(msg);
}

// NAME: AddDebugMessage: Jupiter render_a/src/sys/d3d/common_stuff.cpp (this Talon form has no newline fix-up)
// FUNCTION: D3DREN 0x10012d92
void AddDebugMessage(int debugLevel, const char *pMsg, ...)
{
	va_list marker;
	char msg[256];

	if (debugLevel <= DAT_100584d4)
	{
		va_start(marker, pMsg);
		_vsnprintf(msg, 255, pMsg, marker);
		va_end(marker);
		g_pStruct->ConsolePrint(msg);
	}
}

// FUNCTION: D3DREN 0x10012dd2
HLTPARAM d3d_MaybeCreateCVar(const char *pName, float defaultVal)
{
	HLTPARAM hRet;

	hRet = g_pStruct->GetParameter((char *)pName);
	if (hRet)
	{
		return hRet;
	}
	else
	{
		char str[256];
		sprintf(str, "%s %f", pName, defaultVal);
		g_pStruct->RunConsoleString(str);
		return g_pStruct->GetParameter((char *)pName);
	}
}

// FUNCTION: D3DREN 0x10012e26
void d3d_CreateConsoleVariables()
{
	ConVar *pCur;

	for (pCur = g_pConVars; pCur; pCur = pCur->m_pNext)
	{
		pCur->m_hParam = d3d_MaybeCreateCVar(pCur->m_pName, pCur->m_DefaultVal);
	}
}

// FUNCTION: D3DREN 0x10012e4c
void d3d_ReadConsoleVariables()
{
	ConVar *pCur;

	for (pCur = g_pConVars; pCur; pCur = pCur->m_pNext)
	{
		pCur->m_FloatVal = g_pStruct->GetParameterValueFloat(pCur->m_hParam);
		pCur->m_IntVal = RoundFloatToInt(pCur->m_FloatVal);
		if (pCur->m_pFloatLink)
			*pCur->m_pFloatLink = pCur->m_FloatVal;
		if (pCur->m_pIntLink)
			*pCur->m_pIntLink = pCur->m_IntVal;
	}

	d3d_ReadExtraConsoleVariables();
	if (DAT_10057a10)
		DAT_10048780 = 0;
}

// FUNCTION: D3DREN 0x10012eaf
// guess: upper-cases a string in place (the name matcher of the device list)
void FUN_10012eaf(char *pStr)
{
	uint32 i;

	for (i = 0; i < strlen(pStr); i++)
	{
		pStr[i] = toupper(pStr[i]);
	}
}

// FUNCTION: D3DREN 0x10012edf
// guess: number of set bits of a 32-bit mask (the colour masks of a DDPIXELFORMAT)
int FUN_10012edf(uint32 mask)
{
	int nBits = 0;
	int i;

	for (i = 0; i < 32; i++)
	{
		if (mask & 1)
			nBits++;
		mask >>= 1;
	}

	return nBits;
}

#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'
