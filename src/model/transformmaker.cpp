// Jupiter runtime/model/src/transformmaker.cpp (Talon version: weight set pairs, normalized
// layers, child model node relations, node control callback).
#include "transformmaker.h"


// FUNCTION: LITHTECH 0x0049c590
LTBOOL TransformMaker::IsValid()
{
	uint32 i;
	Model *pModel;
	AnimTimeRef *pAnim;

	if(m_nAnims == 0 || m_nAnims > MAX_GVP_ANIMS)
		return LTFALSE;

	pModel = m_Anims[0].m_pModel;
	for(i=0; i < m_nAnims; i++)
	{
		pAnim = &m_Anims[i];

		if(!pAnim->IsValid() || pAnim->m_pModel != pModel)
			return LTFALSE;

		// Make sure the weight set is valid.
		if(i != 0)
		{
			if(pAnim->m_Cur.m_iWeightSet >= pModel->NumWeightSets())
				return LTFALSE;
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0049c5f0
LTBOOL TransformMaker::SetupTransforms()
{
	if(!SetupCall())
		return LTFALSE;

	Recurse(m_pModel->GetRootNode()->GetNodeIndex(), m_pStartMat);
	return LTTRUE;
}


// Remaining diff: the two inlined GetWeightSet() lookups swap edx/edi (register allocation only; orig: model edx,
// weight-set index edi). Tried: indices in locals, a local AnimTimeRef pointer, ++i.
// Wave 6 tried: eight bodies for Model::GetWeightSet (ternaries, if/else, GetSize vs NumWeightSets, operator[] vs
// GetArray; it is only used here) and a statement hill-climb: no change (13 aligned mismatches).
// Wave 7 phase 2: audit: behaviour matches. Still 13 aligned (edx/edi swap in the two inlined GetWeightSet lookups).
// Tried: the loop counter declared in the for (Jupiter), m_pRecursePath stored first (both 13), the Cur weight set
// first (19).
// PARKED: register allocation only (edx/edi in the inlined GetWeightSet pair, 13 aligned); behaviour identical
// STUB: LITHTECH 0x0049c630
LTBOOL TransformMaker::SetupCall()
{
	uint32 i;

	if(!IsValid())
		return LTFALSE;

	m_pModel = m_Anims[0].m_pModel;
	m_pRecursePath = LTNULL;

	// Cache the information that's going to be used while recursing
	for(i=0; i < m_nAnims; i++)
	{
		m_PrevWeightSets[i]	= m_pModel->GetWeightSet(m_Anims[i].m_Prev.m_iWeightSet);
		m_CurWeightSets[i]	= m_pModel->GetWeightSet(m_Anims[i].m_Cur.m_iWeightSet);
		m_pAnimPrev[i]		= m_pModel->GetAnim(m_Anims[i].m_Prev.m_iAnim);
		m_pAnimCur[i]		= m_pModel->GetAnim(m_Anims[i].m_Cur.m_iAnim);
	}

	if(!m_pStartMat)
	{
		m_mIdentity.Identity();
		m_pStartMat = &m_mIdentity;
	}

	if(!m_pOutput)
		m_pOutput = m_pModel->m_Transforms.GetArray();

	m_pChildInfo = m_pModel->GetAnimInfo(m_Anims[0].m_Prev.m_iAnim)->m_pChildInfo;
	return LTTRUE;
}


// Remaining diff (shape-identical, ~20 instructions): prologue scheduling (orig loads iAnim into eax first, resolves both
// m_pAnim*/AnimNode chains before the key frame arrays). AnimNode/key frame locals and statement order make no difference.
// Wave 6 tried: inline key-frame helpers (from the ModelAnim or the AnimNode), GetArray() indexing and pointer
// arithmetic (no change), Jupiter's inter_frame_param local for m_Percent (much worse).
// Wave 7 phase 2: audit: behaviour matches. 24 aligned, all in the prologue (+0x3..+0x50): the exe resolves both
// m_pAnimPrev/m_pAnimCur -> GetAnimNode chains interleaved before either key frame address. Tried: pKey2 first,
// a key frame base pointer, GetArray()[i] (all 33). Untried: AnimNode locals for both nodes assigned before the
// key frames together with InitTransformAdditive's pKey2-first order.
// PARKED: prologue scheduling of the two AnimNode chains (24 aligned); behaviour identical
// STUB: LITHTECH 0x0049c770
void TransformMaker::InitTransform(uint32 iAnim, uint32 iNode, LTRotation &outQuat, LTVector &outVec)
{
	AnimTimeRef *pTimeRef;
	NodeKeyFrame *pKey1, *pKey2;
	LTVector *pTrans1, *pTrans2;

	pTimeRef = &m_Anims[iAnim];
	pKey1 = &m_pAnimPrev[iAnim]->GetAnimNode(iNode)->m_KeyFrames[pTimeRef->m_Prev.m_iFrame];
	pKey2 = &m_pAnimCur[iAnim]->GetAnimNode(iNode)->m_KeyFrames[pTimeRef->m_Cur.m_iFrame];

	outQuat.Slerp(pKey1->m_Quaternion, pKey2->m_Quaternion, pTimeRef->m_Percent);
	outVec = pKey1->m_vTranslation + (pKey2->m_vTranslation - pKey1->m_vTranslation) * pTimeRef->m_Percent;

	// Apply the per-animation translation to the root node.
	if(iNode == 0)
	{
		pTrans1 = &m_pModel->GetAnimInfo(pTimeRef->m_Prev.m_iAnim)->m_vTranslation;
		pTrans2 = &m_pModel->GetAnimInfo(pTimeRef->m_Cur.m_iAnim)->m_vTranslation;
		outVec += *pTrans1 + (*pTrans2 - *pTrans1) * pTimeRef->m_Percent;
	}
}


// Remaining diff: register allocation and stack slot assignment of the by-value vector temporaries.
// Wave 7 phase 2: audit: behaviour matches. pKey2 assigned before pKey1 (with pBase first) took the aligned score from
// 92 (95) to 49 (23 ignoring stack offsets); what remains is the order of the two frame-index loads in the prologue
// (the exe reads m_Prev.m_iFrame first) and the stack slots of the lerp temporaries (exe 0x30.., ours 0x20..; the
// final `outVec - pBase->m_vTranslation` temporaries get the low slots in the exe). Tried with no gain: pKey2 before
// pBase (89), pBase after pTimeRef (59), `pBase + frame`, GetArray()[i], &m_KeyFrames[0], `outVec -= ` (SIZE),
// the outVec statement first (109), a split lerp (SIZE).
// Wave 9: AnimNode/ModelAnim locals, GetArray()+offset forms, references for the key-frame vectors, a named percent, a
// split lerp, quat_Slerp/VEC_LERP, pTimeRef last: none below 49/16; permuter 15k candidates (all mutation kinds): no gain.
// PARKED: register/schedule only (16 aligned ignoring stack: the exe resolves pKey2 first and spills it into the dead iAnim home); behaviour identical
// STUB: LITHTECH 0x0049c920
void TransformMaker::InitTransformAdditive(uint32 iAnim, uint32 iNode, LTRotation &outQuat, LTVector &outVec)
{
	AnimTimeRef *pTimeRef;
	NodeKeyFrame *pBase, *pKey1, *pKey2;

	pTimeRef = &m_Anims[iAnim];
	pBase = m_pAnimPrev[iAnim]->GetAnimNode(iNode)->m_KeyFrames.GetArray();
	pKey2 = &m_pAnimCur[iAnim]->GetAnimNode(iNode)->m_KeyFrames[pTimeRef->m_Cur.m_iFrame];
	pKey1 = &pBase[pTimeRef->m_Prev.m_iFrame];

	outQuat.Slerp(pKey1->m_Quaternion, pKey2->m_Quaternion, pTimeRef->m_Percent);
	outVec = pKey1->m_vTranslation + (pKey2->m_vTranslation - pKey1->m_vTranslation) * pTimeRef->m_Percent;

	// frame = base + offset
	outQuat = ~pBase->m_Quaternion * outQuat;
	outVec = outVec - pBase->m_vTranslation;
}


// FUNCTION: LITHTECH 0x0049cac0
float TransformMaker::BlendTransform(uint32 iAnim, uint32 iNode, float fTotalWeight, LTBOOL bNormalize)
{
	float fPercent, fPrevWeight, fCurWeight, fWeight;
	LTRotation qTransform, qTemp;
	LTVector vTransform;

	fWeight = (fTotalWeight > 1.0f) ? 1.0f : fTotalWeight;

	if(bNormalize)
	{
		// Fill up what the earlier animations left.
		fPercent = 1.0f - fWeight;
	}
	else
	{
		fPrevWeight = m_PrevWeightSets[iAnim] ? m_PrevWeightSets[iAnim]->m_Weights[iNode] : 0.0f;
		fCurWeight = m_CurWeightSets[iAnim] ? m_CurWeightSets[iAnim]->m_Weights[iNode] : 0.0f;

		if(fPrevWeight == 2.0f)
			fPercent = fCurWeight;
		else if(fCurWeight == 2.0f)
			fPercent = 2.0f;
		else
			fPercent = (fCurWeight - fPrevWeight) * m_Anims[iAnim].m_Percent + fPrevWeight;
	}

	// skip this blend if the weight for this anim is zero.
	if(fPercent == 0.0f)
		return 0.0f;

	if(fPercent == 2.0f)
	{
		// Add the animation.
		InitTransformAdditive(iAnim, iNode, qTransform, vTransform);

		m_Quat = m_Quat * qTransform;
		m_vTrans = m_vTrans + vTransform;
		return 0.0f;
	}
	else
	{
		InitTransform(iAnim, iNode, qTransform, vTransform);
		qTemp = m_Quat;
		m_Quat.Slerp(qTemp, qTransform, fPercent);
		m_vTrans = m_vTrans + (vTransform - m_vTrans) * fPercent;
		return fPercent;
	}
}


// Matched by tools/permute.py: the locals in this declaration order, the loop counter declared at its loop, and
// the node offset passed to SetTranslation through a named copy and a reference to it (that gives the exe's
// register choice and lea order around ConvertToMatrix(m_mRelation)).
// FUNCTION: LITHTECH 0x0049cd00
void TransformMaker::Recurse(uint32 iNode, LTMatrix *pParentT)
{
	NodeRelation *pRelation;
	static LTMatrix mScratchMat;
	float fWeight, fPrevWeight;
	ModelNode *pNode;
	LTMatrix *pMyGlobal;

	for(;;)
	{
		pMyGlobal = &m_pOutput[iNode];

		// Apply animation data (first one inits, the rest are blended in).
		InitTransform(0, iNode, m_Quat, m_vTrans);

		if(m_PrevWeightSets[0] && m_CurWeightSets[0] && m_Anims[0].m_Percent != 2.0f)
		{
			fPrevWeight = m_PrevWeightSets[0]->m_Weights[iNode];
			fWeight = (m_CurWeightSets[0]->m_Weights[iNode] - fPrevWeight) * m_Anims[0].m_Percent + fPrevWeight;
		}
		else
		{
			fWeight = 0.0f;
		}

		uint32 i;
		for(i=1; i < m_nAnims; i++)
		{
			fWeight += BlendTransform(i, iNode, fWeight, m_Anims[i].m_bNormalize);
		}

		pNode = m_pModel->GetNode(iNode);

		// Update the global matrix.
		m_Quat.ConvertToMatrix(m_mTemp);

		// Use the offset from the parent if this node only uses rotation data
		// from the animation.
		if(pNode->m_Flags & MNODE_ROTATIONONLY)
		{
			LTVector vOffset = pNode->m_vOffsetFromParent;
			LTVector &vOffsetRef = vOffset;
			m_mTemp.SetTranslation(vOffsetRef);
		}
		else
		{
			m_mTemp.SetTranslation(m_vTrans);
		}

		MatMul(&mScratchMat, pParentT, &m_mTemp);

		// Go into the space of the model the animation came from.
		pRelation = m_pRelation = &m_pChildInfo->m_Relation[iNode];
		pRelation->m_Rot.ConvertToMatrix(m_mRelation);
		m_mRelation.SetTranslation(pRelation->m_Pos);
		MatMul(pMyGlobal, &mScratchMat, &m_mRelation);

		if(m_NodeControlFn)
			m_NodeControlFn(m_hObject, iNode, pMyGlobal, m_pNodeControlUserData);

		// Do the children..
		if(m_pRecursePath)
		{
			if(m_iCurPath > 0)
			{
				m_iCurPath--;
				// Start over the loop at the next point in the path
				iNode = m_pRecursePath[m_iCurPath];
				pParentT = pMyGlobal;
			}
			else
				break;
		}
		else
		{
			uint32 nNumChildren = pNode->NumChildren();
			if(nNumChildren)
			{
				// Recurse for the beginning of the child list
				--nNumChildren;
				for(i=0; i < nNumChildren; i++)
				{
					Recurse(pNode->m_Children[i]->GetNodeIndex(), pMyGlobal);
				}
				// Iterate for the final child
				m_iCurPath = 1;
				iNode = pNode->m_Children[nNumChildren]->GetNodeIndex();
				pParentT = pMyGlobal;
			}
			else
				// Jump out of the list..  No children.
				break;
		}
	}
}
