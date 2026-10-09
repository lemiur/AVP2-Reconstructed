// d3d.ren model drawer setup, Talon's setupmodel.cpp (owner: unit unk/100098d0, work package W4): the global g_ModelDraw
// (0x10052980) and the free functions around it.  The class ModelDraw itself is defined once, in modeldraw.h (owner W1), to
// which this unit adds members at their exact offsets (so there is no second `class ModelDraw` any more).
//
// NAME: ModelDraw / g_ModelDraw: Jupiter render_a/src/sys/d3d/setupmodel.cpp+h `class ModelDraw`, `ModelDraw g_ModelDraw;`
// (the d3d.ren static initialiser 0x1000b349 constructs 0x10052980 with ModelDraw::ModelDraw 0x1000b7af and registers its
// destructor 0x1000b9d4 with atexit).  Methods CallModelHook, StaticLightCB, GetDirLightAmount, SetupModelLight and the
// static CastRayAtSky are Jupiter names whose bodies have the same shape as the d3d.ren functions.
// NAME: ModelHookData / ModelInstanceHookData / IntersectQuery / IntersectInfo / FindObjInfo: SDK and engine decomp names.
#ifndef __D3DREN_SETUPMODEL_H__
#define __D3DREN_SETUPMODEL_H__

#include "d3dren/modeldraw.h"

// STANDIN type: a class whose constructor is empty (a global of it gets an empty static initialiser, see 0x1000b369).
struct UnkType_EmptyCtor
{
	UnkType_EmptyCtor() {}
	int m_Unk00;
};

// GLOBAL: D3DREN 0x10052980
extern ModelDraw g_ModelDraw;

// ---- functions of this unit that other units call ----
class ViewParams;
int d3d_TestModelFrustum(ModelDraw *pDraw, ModelInstance *pInstance, uint32 *pClipFlags);	// guess: instance sphere against the view frustum
int d3d_TestSphereClipPlanes(LTVector *pPos, float fRadius, LTPlane *pPlanes, uint32 *pFlags);	// guess: sphere against 6 planes (0 = outside)
void d3d_QueueModel(ViewParams *pParams, LTObject *pObject);	// guess: BaseObjectSet::Draw callback of the models
void d3d_BuildModelWarbleTables();			// guess: fills the warble tables
// GLOBAL: D3DREN 0x10053278
extern float g_fModelWarblePhase;			// guess: warble phase (advanced per frame by d3d_RenderScene)
// GLOBAL: D3DREN 0x10054870
extern float g_fModelWarbleFraction;			// guess: warble fraction

#endif
