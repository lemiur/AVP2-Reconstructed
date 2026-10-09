// d3d.ren sys/d3d/common_draw: the per-frame globals d3d_InitFrame sets up (Jupiter render_a/src/sys/d3d/common_draw.h, "Externs").
// Owner: unit sys/d3d/common_draw (src/d3dren/sys/d3d/common_draw.cpp defines every variable declared here).  The addresses
// all lie in that object's hash-ordered .bss block (0x10055cd8-0x100577bc, between 3d_ops's NearZ ConVar and common_init's g_pModeList).
//
// NAME: g_CurFrameCode, g_CurObjectFrameCode, g_ObjectDynamicLights, g_nNumObjectDynamicLights, MAX_VISIBLE_LIGHTS: Jupiter common_draw.h.
// Everything else keeps its current name (the names are evidence for the .bss hash order: never rename one without the gate);
// roles are in the guess comments.
#ifndef __D3DREN_COMMON_DRAW_H__
#define __D3DREN_COMMON_DRAW_H__

#include "ltbasedefs.h"
#include "d3dren/common_stuff.h"
#include "d3dren/tlvertex.h"

class DynamicLight;

// Talon value: ApplyVisibleDynamicLight (0x10023b20) stops at 0x28 lights, and the array is 0xa0 bytes (Jupiter has 64).
#define MAX_VISIBLE_LIGHTS	40

// GLOBAL: D3DREN 0x100577a0
extern uint16 g_CurFrameCode;
// GLOBAL: D3DREN 0x100561f0
extern uint32 g_CurObjectFrameCode;

// The list of dynamic lights that will be used to light objects.
// GLOBAL: D3DREN 0x100566d0
extern DynamicLight *g_ObjectDynamicLights[MAX_VISIBLE_LIGHTS];
// GLOBAL: D3DREN 0x10056218
extern uint32 g_nNumObjectDynamicLights;	// unsigned: the exe compares it with jb/jbe

// GLOBAL: D3DREN 0x10056770
extern MainWorld *g_pFrameMainWorld;			// guess: the engine's main world (names_proposal guess_g_pMainWorld, low)
// GLOBAL: D3DREN 0x1005625c
extern int g_nInitFrameArgument;				// guess: the third argument of d3d_InitFrame
// GLOBAL: D3DREN 0x10055ce0
extern GlobalPanInfo *g_pGlobalPanInfo;	// &g_pStruct->m_GlobalPans (NAMING.md: "g_pGlobalPanInfo = &it")
// GLOBAL: D3DREN 0x10056284
extern RenderContext *g_pFrameRenderContext;	// guess: the render context of the frame (CreateContext's object)
// GLOBAL: D3DREN 0x100577b8
extern uint16 g_CurTextureFrameCode;			// guess: current texture frame code (RenderStruct::IncCurTextureFrameCode)

// The scene vectors d3d_InitFrame copies out of the SceneDesc, and the colours derived from them.
// GLOBAL: D3DREN 0x100577a8
extern LTVector g_vSceneClientVectorPrimary;			// guess: a vector the engine hands over (SceneDesc +0x38)
// GLOBAL: D3DREN 0x100566a0
extern LTVector g_vSceneClientVectorSecondary;			// guess: SceneDesc +0x44
// GLOBAL: D3DREN 0x100561f8
extern LTVector g_GlobalLightScale;			// guess: the global light scale (SceneDesc +0x50, "GlobalLightScale")
// GLOBAL: D3DREN 0x10055ce8
extern LTVector g_GlobalVertexTint;			// guess: the global vertex tint (SceneDesc +0x5c)
// GLOBAL: D3DREN 0x10056260
extern LTVector g_vGlobalModelDirAdd2;			// guess: SceneDesc +0x68
// TODO(identity): the object has only five empty static initialisers (0x1000f3a0-0x1000f3a4, the five LTVector definitions in
// common_draw.cpp), but seven 12-byte vectors in its .bss; these two derived ones are therefore not LTVectors with the SDK's empty
// constructor (a constructor-less vector type?).  Their definitions are left out until the type is known.
// GLOBAL: D3DREN 0x10056208
extern LTVector g_GlobalLightScale255;			// guess: g_GlobalLightScale * 255
// GLOBAL: D3DREN 0x100566c0
extern LTVector g_vGlobalLightScalePerByte;			// g_GlobalLightScale / 255 (original loads at 0x100104cb/0x100104d2/0x100104dd)
// GLOBAL: D3DREN 0x10056698
extern TLRGB g_GlobalLightScaleColor;				// guess: the light scale as a packed colour
// GLOBAL: D3DREN 0x10057774
extern uint8 g_nPolyVertexAlpha;				// guess: 0xff, set with the colours (an alpha byte)
// GLOBAL: D3DREN 0x100566bc
extern RGBColor g_GlobalVertexTintColor;				// guess: the tint as a packed colour
// GLOBAL: D3DREN 0x10057798
extern TLRGB g_GlobalModelDirAdd2Color;				// guess: that vector as a packed colour (not scaled)

// Per-frame statistics (zeroed by d3d_InitFrame; printed by RenderScene).
// GLOBAL: D3DREN 0x10056694
extern float g_fScreenTriangleArea;				// guess: area drawn this frame ("Tri area drawn")
// GLOBAL: D3DREN 0x10056688
extern int g_nWorldPolysProcessed;
// GLOBAL: D3DREN 0x100566ac
extern int g_nWorldPolysDrawn;
// GLOBAL: D3DREN 0x10055cd8
extern int g_nParticlesDrawn;
// GLOBAL: D3DREN 0x1005779c
extern int g_nReservedFrameStatistic;
// GLOBAL: D3DREN 0x100566cc
extern int g_nLightTests;
// GLOBAL: D3DREN 0x10056690
extern int g_nRejectedPolyLightTests;
// GLOBAL: D3DREN 0x100566b8
extern int g_nSkyPortals;
// GLOBAL: D3DREN 0x10056270
extern int g_nSkyPolyFragments;
// GLOBAL: D3DREN 0x100566b0
extern int g_nPolygonTriangles;
// GLOBAL: D3DREN 0x10055cf4
extern int g_nVisibleLeaves;
// GLOBAL: D3DREN 0x10056280
extern int g_nTextureUploads;
// GLOBAL: D3DREN 0x10057794
extern int g_nTextureChanges;
// GLOBAL: D3DREN 0x10056278
extern int g_nDynamicLightmapsRefreshed;
// GLOBAL: D3DREN 0x10055cdc
extern int g_nTextureUploadSaves;
// GLOBAL: D3DREN 0x10056214
extern int g_nLitPolies;				// guess: number of polys in the lit list ("Num Lit Polies")

#endif
