// Talon-only: ray intersection against a model's triangles (ILTModel::IntersectRays).
// Not in Jupiter; the file name is a guess (it sorts between modellt_impl and motion).
#include "bdefs.h"
#include "de_objects.h"
#include "model.h"
#include "transformmaker.h"
#include "modelrayintersect.h"


// Transformed vertices and triangles of the piece LOD being tested.
// FUNCTION: LITHTECH 0x0045add0 _$E7
// FUNCTION: LITHTECH 0x0045ade0 _$E3
// FUNCTION: LITHTECH 0x0045ae10 _$E6
// FUNCTION: LITHTECH 0x0045ae20 _$E4
// The static members' destructors share one guard byte (0x004e4550), one bit each.
// GLOBAL: LITHTECH 0x004e453c
CMoArray<LTVector> CModelRayIntersect::s_RayVerts;

// FUNCTION: LITHTECH 0x0045ae80 _$E12
// FUNCTION: LITHTECH 0x0045ae90 _$E9
// FUNCTION: LITHTECH 0x0045aec0 _$E11
// FUNCTION: LITHTECH 0x0045aed0 _$E10
// GLOBAL: LITHTECH 0x004e4528
CMoArray<RayTri> CModelRayIntersect::s_RayTris;


// FUNCTION: LITHTECH 0x0045af30
LTBOOL CModelRayIntersect::Init(HOBJECT hModel, const LTVector &vCamPos, int32 nLODOffset)
{
	m_hModel = hModel;
	m_pModel = ((ModelInstance*)hModel)->GetModelDB();
	m_iLOD = CalcLOD(vCamPos, nLODOffset);
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0045af60
// Dist() inlines operator- but, with the m_LODDists.GetSize() sites pending after it, leaves the
// LTVector(x,y,z) constructor (0x00412960) and Mag() (0x0041f6a0) out of line.
uint32 CModelRayIntersect::CalcLOD(const LTVector &vCamPos, int32 nLODOffset)
{
	float fDist;
	uint32 i, iLOD;

	fDist = vCamPos.Dist(m_hModel->GetPos());

	if(nLODOffset > 0)
	{
		if(nLODOffset > (int32)m_pModel->m_LODDists.GetSize())
			nLODOffset = m_pModel->m_LODDists.GetSize();

		fDist += *m_pModel->GetLODDist(nLODOffset);
	}
	else if(nLODOffset < 0)
	{
		nLODOffset = -nLODOffset;
		if(nLODOffset < 0)
			nLODOffset = 0;
		else if(nLODOffset > (int32)m_pModel->m_LODDists.GetSize())
			nLODOffset = m_pModel->m_LODDists.GetSize();

		fDist -= *m_pModel->GetLODDist(nLODOffset);
	}

	iLOD = 0;
	for(i=0; i < m_pModel->m_LODDists.GetSize()+1; i++)
	{
		if(fDist > *m_pModel->GetLODDist(i))
			iLOD = i;
	}

	return iLOD;
}


// FUNCTION: LITHTECH 0x0045b0a0
// The explicit m_pOutput store (redundant with the constructor's) places the zero store after &mTransform.
LTBOOL CModelRayIntersect::Setup()
{
	LTMatrix mTransform;

	m_hModel->SetupTransform(mTransform);

	TransformMaker tMaker;
	tMaker.m_pStartMat = &mTransform;
	tMaker.m_pOutput = LTNULL;
	tMaker.m_hObject = m_hModel;
	((ModelInstance*)m_hModel)->SetupTransformMaker(&tMaker);

	if(!tMaker.SetupTransforms())
	{
		dsi_ConsolePrint("Model::SetupTransforms failed for %s.", m_pModel->GetFilename());
		return LTFALSE;
	}

	return LTTRUE;
}


// The original keeps aPieces in its argument slot and doesn't duplicate the piece loop test.
// Wave 6 phase 2 (no change, 33 aligned): nested `if(!hidden){...}` instead of continue, if/else for iPiece,
// iPiece = i then override, a while loop, swapped NULL/NumPieces stores, m_Pieces.GetSize(), a pRay local,
// ++iRay; `iPiece = i; if(aPieces) ...` is worse (36).
// Wave 7 phase 2: ALIGNED 33. Audit: only immediates (ours' extra `mov eax,1` of a duplicated `return LTTRUE`
// epilogue after the piece-count test, and `add eax,0x1c` where the exe has `lea eax,[edx+0x1c]`): behaviour matches,
// no inline candidates. Left: aPieces lives in edx at entry (the exe keeps it in its argument slot) and the zero-count
// test is rotated with its own epilogue (exe: `jbe` to the shared one).
// PARKED: register/loop-rotation differences only; behaviour matches
// STUB: LITHTECH 0x0045b170
LTBOOL CModelRayIntersect::Intersect(HMODELPIECE *aPieces, uint32 nPieceCount, ILTModel::LTRayResult *aRays, uint32 nRayCount)
{
	ModelPiece *pPiece;
	PieceLOD *pLOD;
	uint32 i, iRay, iPiece;

	if(!aPieces || !nPieceCount)
	{
		aPieces = LTNULL;
		nPieceCount = m_pModel->NumPieces();
	}

	for(iRay=0; iRay < nRayCount; iRay++)
	{
		aRays[iRay].m_bIntersect = LTFALSE;
		aRays[iRay].m_fDistance = aRays[iRay].m_fMaxDist;
	}

	for(i=0; i < nPieceCount; i++)
	{
		iPiece = aPieces ? aPieces[i] : i;
		m_iPiece = iPiece;

		pPiece = m_pModel->GetPiece(iPiece);
		if(((ModelInstance*)m_hModel)->m_HiddenPieces & (1 << iPiece))
			continue;

		pLOD = pPiece->GetLOD(m_iLOD);
		if(!SetupArrays(pLOD))
			return LTFALSE;

		TransformVerts(pLOD);
		SetupTris(pLOD);

		for(iRay=0; iRay < nRayCount; iRay++)
			IntersectRay(&aRays[iRay]);
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0045b260
LTBOOL CModelRayIntersect::SetupArrays(PieceLOD *pLOD)
{
	if(pLOD->m_Verts.GetSize() > s_RayVerts.GetSize())
	{
		if(!s_RayVerts.SetSize(pLOD->m_Verts.GetSize() + 32))
			return LTFALSE;
	}

	if(pLOD->m_Tris.GetSize() > s_RayTris.GetSize())
	{
		if(!s_RayTris.SetSize(pLOD->m_Tris.GetSize() + 32))
			return LTFALSE;
	}

	return LTTRUE;
}


// Skins the LOD's vertices with the model's node transforms.
// FUNCTION: LITHTECH 0x0045b3b0
// The SDK's MatVMul_Add (ltmatrix.h) into a float[4] gives the original's frame and x87 term order, and the
// homogeneous divide reuses vOut[3]. The vertex pointer comes from GetArray() + i: `&pLOD->m_Verts[i]` flips the
// operand order of the x row's m[0][0]*pVec[0] product.
void CModelRayIntersect::TransformVerts(PieceLOD *pLOD)
{
	LTMatrix *pTransforms;
	ModelVert *pVert;
	NewVertexWeight *pWeight;
	LTVector *pOut;
	uint32 i, iWeight;
	float vOut[4];

	pTransforms = m_pModel->m_Transforms.GetArray();
	pOut = s_RayVerts.GetArray();

	for(i=0; i < pLOD->m_Verts.GetSize(); i++)
	{
		pVert = pLOD->m_Verts.GetArray() + i;

		vOut[0] = vOut[1] = vOut[2] = vOut[3] = 0.0f;
		for(iWeight=0; iWeight < pVert->m_nWeights; iWeight++)
		{
			pWeight = &pVert->m_Weights[iWeight];
			MatVMul_Add(vOut, &pTransforms[pWeight->m_iNode], pWeight->m_Vec);
		}

		vOut[3] = 1.0f / vOut[3];
		pOut[i].x = vOut[0] * vOut[3];
		pOut[i].y = vOut[1] * vOut[3];
		pOut[i].z = vOut[2] * vOut[3];
	}
}


// FUNCTION: LITHTECH 0x0045b4f0
void CModelRayIntersect::SetupTris(PieceLOD *pLOD)
{
	LTVector *pVerts;
	ModelTri *pTri;
	RayTri *pRayTri;
	uint32 i;

	pVerts = s_RayVerts.GetArray();
	m_nTris = pLOD->m_Tris.GetSize();

	for(i=0; i < m_nTris; i++)
	{
		pRayTri = &s_RayTris[i];
		pTri = &pLOD->m_Tris[i];

		pRayTri->m_vPt = pVerts[pTri->m_Indices[0]];
		pRayTri->m_vEdge1 = pVerts[pTri->m_Indices[1]] - pRayTri->m_vPt;
		pRayTri->m_vEdge2 = pVerts[pTri->m_Indices[2]] - pRayTri->m_vPt;
	}
}


inline LTBOOL RayIntersectTri(RayTri *pTri, ILTModel::LTRayResult *pRay, float &t)
{
	LTVector vT;
	float fInvDet, u, v;

	const LTVector &vP = pTri->m_vEdge2.Cross(pRay->m_vDir);
	fInvDet = 1.0f / pTri->m_vEdge1.Dot(vP);

	vT = pRay->m_vOrigin - pTri->m_vPt;
	u = vT.Dot(vP) * fInvDet;
	if(u < 0.0f || u > 1.0f)
		return LTFALSE;

	const LTVector &vQ = pTri->m_vEdge1.Cross(vT);
	v = pRay->m_vDir.Dot(vQ) * fInvDet;
	if(v < 0.0f || u + v > 1.0f)
		return LTFALSE;

	t = pTri->m_vEdge2.Dot(vQ) * fInvDet;
	return LTTRUE;
}

inline LTVector GetTriNormal(RayTri *pTri)
{
	LTVector vNormal = pTri->m_vEdge2.Cross(pTri->m_vEdge1);
	vNormal.Norm();
	return vNormal;
}


// Moller-Trumbore ray/triangle test against every prepared triangle.
// Call structure recovered from the disassembly: vP = e2.Cross(dir) and vQ = e1.Cross(vT) (the by-value
// argument is the vector that gets copied), vNormal = e2.Cross(e1).  The original calls the
// LTVector(x,y,z) constructor out of line in the first Cross and the operator-, Cross out of line for vQ and
// Dot out of line for t (0x00412960, 0x0043eb30, 0x0041f6d0) but inlines the normal's Cross, Mag and *=;
// our inline budget gives all-or-nothing, so the call pattern isn't reproduced yet.
// Wave 6 phase 2: the normal is vNormal.Norm() (exe: fsqrt, `test ah,0x44` == 0 test, fdivr 1.0, in-place multiplies;
// same code as the hand-written form when inlined). "Early out, late in" means the triangle test sits one inline level
// deeper: with the Moller-Trumbore part (up to t) in an inline helper `RayIntersectTri(pTri, pRay, t)` the out-of-line
// set becomes ctor, ctor, Dot, Cross, Dot, Dot (exe: ctor, ctor, Cross, Dot), i.e. the helper's share is ~5-7 units
// too small; the whole loop body in a helper gives ctor, Cross, Norm (worse). Not adopted.
// Wave 7 phase 2 (tools/inline_budget.py --variants): the Moller-Trumbore helper plus a second inline helper that
// returns the normal by value (`pRay->m_vNormal = GetTriNormal(pTri)`) reproduces every exe out-of-line call (ctor,
// ctor, Cross, Dot; the model and the build agree): one pending site after RayIntersectTri instead of two (Cross,
// Norm) gives its children the share the exe needs (336-390u). ALIGNED 245/217 -> 160/53 (helper names invented;
// both are always inlined, so the exe has no copy). Left: the frame (ours 0x80, exe 0x64: the helpers' locals don't
// share slots like the original's), `1.0f / Dot` as fdivr where the exe does fld 1.0; fdiv st(1), and 64 bytes of
// size. Direct-initialised vP/vT/vQ in the helper (constructed in place, as the exe reads the Cross result
// buffer directly) score 125 but inline Cross and Dot (the build disagrees with the model there).
// vP and vQ bound as const references to the Cross results keep the call set and read the result buffers in place, as
// the exe does (ALIGNED 144/37 -> 14/8, same size); direct initialisation instead inlines Cross (no budget reproduces it).
// Left: `1.0f / Dot` is fdivr in ours; the exe keeps the dot on the x87 stack (fld 1.0; fdiv st(1); fstp; fstp st(0)),
// VC6's reciprocal form for several divides by one value (LTMatrix::Normalize's VEC_DIVSCALAR in GetNodeTransform).
// Tried: a named fDet (with and without dividing u/v/t by it, every subset), the divide after vT, an inline reciprocal
// helper, VEC_DOT for the inline Dots (shifts the inline decisions).
// STUB: LITHTECH 0x0045b640
void CModelRayIntersect::IntersectRay(ILTModel::LTRayResult *pRay)
{
	RayTri *pTri;
	float t;
	uint32 i;

	for(i=0; i < m_nTris; i++)
	{
		pTri = &s_RayTris[i];

		if(!RayIntersectTri(pTri, pRay, t))
			continue;

		if(t < pRay->m_fDistance && t > pRay->m_fMinDist)
		{
			pRay->m_fDistance = t;
			pRay->m_bIntersect = LTTRUE;

			pRay->m_vNormal = GetTriNormal(pTri);
			pRay->m_nPiece = m_iPiece;
		}
	}
}


// ------------------------------------------------------------------------ //
// CMoArray instances (0x0045b930-0x0045c600).
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0045b930 ?GenGetNext@?$CMoArray@URayTri@@VDefaultCache@@@@UBE?AURayTri@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045b960 ?GenGetAt@?$CMoArray@URayTri@@VDefaultCache@@@@UBE?AURayTri@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045b990 ?GenAppend@?$CMoArray@URayTri@@VDefaultCache@@@@UAEHAAURayTri@@@Z
// FUNCTION: LITHTECH 0x0045bad0 ?GenRemoveAt@?$CMoArray@URayTri@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045bbf0 ?GenCopyList@?$CMoArray@URayTri@@VDefaultCache@@@@UAEHABV?$GenList@URayTri@@@@@Z
// FUNCTION: LITHTECH 0x0045bd30 ?GenAppendList@?$CMoArray@URayTri@@VDefaultCache@@@@UAEHABV?$GenList@URayTri@@@@@Z
// FUNCTION: LITHTECH 0x0045be40 ?GenFindElement@?$CMoArray@URayTri@@VDefaultCache@@@@UBEHABURayTri@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045be70 ?GenAppend@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEHAAV?$_CVector@M@@@Z
// FUNCTION: LITHTECH 0x0045bfe0 ?GenRemoveAt@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045c120 ?GenCopyList@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEHABV?$GenList@V?$_CVector@M@@@@@Z
// FUNCTION: LITHTECH 0x0045c260 ?GenAppendList@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEHABV?$GenList@V?$_CVector@M@@@@@Z
// FUNCTION: LITHTECH 0x0045c360 ?GenFindElement@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UBEHABV?$_CVector@M@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045c390 ?InternalNiceSetSize@?$CMoArray@URayTri@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0045c4a0 ?InternalNiceSetSize@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0045c5b0 ?BaseNew@@YAPAURayTri@@PAVLAlloc@@PAU1@K@Z
// FUNCTION: LITHTECH 0x0045c5d0 ?BaseNew@@YAPAV?$_CVector@M@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x0045c5f0 ??0RayTri@@QAE@XZ
