// Jupiter runtime/model/src/model.cpp, Talon version.
// Talon's model classes have vtables (CMoArray-based), Model keeps its child models in a
// fixed array at 0x194 and its node transforms in a raw array allocated from m_pAlloc.
#include <string.h>
#include <stdlib.h>
#include "bdefs.h"
#include "model.h"
#include "transformmaker.h"
#include "ltanimtracker.h"
#include "../../build/proj/LT2/lithshared/stdlith/l_allocator.h"

// Preserve the original call boundaries for node-array insertion without changing other array types.
inline void CopyNodeRelation(NodeRelation &dst, const NodeRelation &src)
{
	LTRotation &(LTRotation::*copyRotation)(const	LTRotation &) = &LTRotation::operator=;
	dst.m_Pos = src.m_Pos;
	(dst.m_Rot.*copyRotation)(src.m_Rot);
}

template<> inline BOOL CMoArray<NodeRelation, DefaultCache>::Insert2(DWORD index, const NodeRelation &toInsert, LAlloc *pAlloc)
{
	NodeRelation &(NodeRelation::*copyNode)(const NodeRelation &) = &NodeRelation::operator=;
	NodeRelation *pNewArray;
	DWORD	newSize, i;

	ASSERT( index <= m_nElements );
	if(index > m_nElements)
		return FALSE;

	// Create a new array (possibly).
	newSize = m_nElements + 1;

	if( m_Cache.GetCacheSize() == 0 )
	{
		pNewArray = _AllocateTArray( newSize + m_Cache.GetWantedCache(), pAlloc );
		if( !pNewArray )
			return FALSE;

		// Copy the old array into the new one, start inserting at index.
		for( i=0; i < index; i++ )
			pNewArray[i] = m_pArray[i];

		for( i=index; i < m_nElements; i++ )
			(pNewArray[i+1].*copyNode)(m_pArray[i]);

		// Insert the new item into the array
		(pNewArray[index].*copyNode)(toInsert);

		// Free the old array and set our pointer to the new one
		if( m_pArray )
		{
			void (*destroyArray)(LAlloc *, NodeRelation *, DWORD) = BaseDelete;
			(*destroyArray)(pAlloc, m_pArray, GetNumAllocatedElements());
			m_pArray = NULL;
			m_Cache.SetCacheSize(0);
		}

		m_Cache.SetCacheSize(m_Cache.GetWantedCache());
		m_pArray = pNewArray;
	}
	else
	{
		for( i=m_nElements; i > index; i-- )
			CopyNodeRelation(m_pArray[i], m_pArray[i-1]);

		m_Cache.SetCacheSize(m_Cache.GetCacheSize() - 1);

		m_pArray[index] = toInsert;
	}

	++m_nElements;

	return TRUE;
}


#define DEFAULT_MODEL_VIS_RADIUS	50.0f


// FUNCTION: LITHTECH 0x0044db70
LTRESULT DefaultLoadChildFn(ModelLoadRequest *pRequest, Model **ppModel)
{
	return LT_NOCHANGE;
}


// FUNCTION: LITHTECH 0x0044db80
LTBOOL VerifyChildModel_R(ModelNode *pParentNode, ModelNode *pChildNode, ModelNode* &pErrNode)
{
	uint32 i;

	if(pParentNode->NumChildren() != pChildNode->NumChildren())
	{
		pErrNode = pParentNode;
		return LTFALSE;
	}

	for(i=0; i < pParentNode->NumChildren(); i++)
	{
		if(!VerifyChildModel_R(pParentNode->GetChild(i), pChildNode->GetChild(i), pErrNode))
			return LTFALSE;
	}

	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// ModelStringList.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044dbe0
ModelStringList::ModelStringList(LAlloc *pAlloc)
{
	m_pAlloc = pAlloc;
	m_StringList = LTNULL;
}


// FUNCTION: LITHTECH 0x0044dc00
ModelStringList::~ModelStringList()
{
	Term();
}


// FUNCTION: LITHTECH 0x0044dc10
void ModelStringList::Term()
{
	ModelString *pCurString, *pNextString;

	pCurString = m_StringList;
	while(pCurString)
	{
		pNextString = pCurString->m_pNext;
		GetAlloc()->Free(pCurString);
		pCurString = pNextString;
	}
	m_StringList = LTNULL;
}


// FUNCTION: LITHTECH 0x0044dc40
const char* ModelStringList::AddString(const char *pString)
{
	ModelString *pCur, *pRet;
	uint32 dwSize;

	// Quick exit..
	if(!pString || pString[0] == 0)
		return g_EmptyString;

	pCur = m_StringList;
	while(pCur)
	{
		if(strcmp(pCur->m_String, pString) == 0)
			return pCur->m_String;

		pCur = pCur->m_pNext;
	}

	// Ok, add a new string.
	dwSize = sizeof(ModelString) - 1 + strlen(pString) + 1;
	pRet = (ModelString*)GetAlloc()->Alloc(dwSize);
	if(!pRet)
		return g_EmptyString;

	pRet->m_AllocSize = dwSize;
	pRet->m_pNext = m_StringList;
	m_StringList = pRet;

	strcpy(pRet->m_String, pString);
	return pRet->m_String;
}


// FUNCTION: LITHTECH 0x0044dd00
LTBOOL ModelStringList::SetAlloc(LAlloc *pAlloc)
{
	if(m_StringList)
		return LTFALSE;

	m_pAlloc = pAlloc;
	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// AnimTimeRef / AnimKeyFrame.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044dd20
LTBOOL AnimTimeRef::IsValid()
{
	return m_pModel &&
		m_Prev.m_iAnim < m_pModel->NumAnims() && m_Cur.m_iAnim < m_pModel->NumAnims() &&
		m_Prev.m_iFrame < m_pModel->GetAnim(m_Prev.m_iAnim)->m_KeyFrames.GetSize() &&
		m_Cur.m_iFrame < m_pModel->GetAnim(m_Cur.m_iAnim)->m_KeyFrames.GetSize() &&
		m_Percent >= 0.0f && m_Percent <= 1.0f;
}


// ------------------------------------------------------------------------ //
// NewVertexWeight / ModelVert.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044dda0
NewVertexWeight::NewVertexWeight()
{
	m_Vec[0] = m_Vec[1] = m_Vec[2] = m_Vec[3] = 0.0f;
	m_iNode = 0;
}


// FUNCTION: LITHTECH 0x0044ddc0
ModelVert::ModelVert()
{
	m_Weights = LTNULL;
	m_nWeights = 0;
	m_Vec.Init();
	m_Normal.Init();
}


// FUNCTION: LITHTECH 0x0044dde0
ChildInfo::ChildInfo()
{
	m_AnimOffset = 0;
	m_pFilename = g_EmptyString;
	m_pModel = LTNULL;
	m_pParentModel = LTNULL;
	m_Unknown14 = 0;
	m_bTreesValid = LTTRUE;
}

// FUNCTION: LITHTECH 0x0044de20
ChildInfo::~ChildInfo()
{
	Term();
}

// FUNCTION: LITHTECH 0x0044de60
void ChildInfo::Term()
{
	m_pModel = LTNULL;
	m_pFilename = g_EmptyString;
	m_Relation.Term(m_pParentModel->m_pAlloc);
}


// FUNCTION: LITHTECH 0x0044dea0
AnimKeyFrame::AnimKeyFrame()
{
	m_Time = 0;
	m_pString = g_EmptyString;
	m_KeyType = KEYTYPE_POSITION;
	m_Callback = LTNULL;
	m_pUser = LTNULL;
}


// ------------------------------------------------------------------------ //
// AnimNode.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044dec0 ??0AnimNode@@QAE@XZ
AnimNode::AnimNode()
{
	Clear();
}

// FUNCTION: LITHTECH 0x0044df20 ?GetAnim@AnimNode@@UAEPAVModelAnim@@XZ

// FUNCTION: LITHTECH 0x0044df30 ??_GAnimNode@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x0044df50 ??0AnimNode@@QAE@PAVModelAnim@@PAV0@@Z
AnimNode::AnimNode(ModelAnim *pAnim, AnimNode *pParent)
{
	Clear();
	m_pAnim = pAnim;
	m_pParentNode = pParent;
}

// FUNCTION: LITHTECH 0x0044dfc0
AnimNode::~AnimNode()
{
	Term();
}

// FUNCTION: LITHTECH 0x0044e020
void AnimNode::Clear()
{
	m_pAnim = LTNULL;
	m_pParentNode = LTNULL;
	m_pNode = LTNULL;
	m_KeyFrames = CMoArray<NodeKeyFrame, NoCache>();
}

// FUNCTION: LITHTECH 0x0044e110 ??4NodeKeyFrame@@QAEXABV0@@Z

// FUNCTION: LITHTECH 0x0044e150
void AnimNode::Term()
{
	Model *pModel;
	LAlloc *pAlloc;
	uint32 i;

	pModel = GetModel();
	m_KeyFrames.SetSize2(0, pModel->m_pAlloc);

	pAlloc = GetModel()->m_pAlloc;
	for(i=0; i < NumChildren(); i++)
	{
		LDelete(pAlloc, m_Children[i]);
	}

	m_Children.SetSize2(0, pAlloc);
	m_Children.GetSize();	// pending inline call: the original had one more inline site here
}

// FUNCTION: LITHTECH 0x0044e1e0 ?SetAnim@AnimNode@@UAEXPAVModelAnim@@@Z

// FUNCTION: LITHTECH 0x0044e1f0
AnimNode* AnimNode::Create(ModelAnim *pAnim, AnimNode *pParent)
{
	return new AnimNode(pAnim, pParent);
}

// FUNCTION: LITHTECH 0x0044e210
LTBOOL AnimNode::FillNodeList(uint32 &curNodeIndex)
{
	uint32 i;

	if(m_pAnim->m_AnimNodes && curNodeIndex < GetModel()->NumNodes())
	{
		m_pAnim->m_AnimNodes[curNodeIndex] = this;
		++curNodeIndex;

		for(i=0; i < NumChildren(); i++)
		{
			if(!GetChild(i)->FillNodeList(curNodeIndex))
				return LTFALSE;
		}

		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0044e280
LTBOOL AnimNode::SetNode_R(ModelNode *pNode)
{
	uint32 i;

	m_pNode = pNode;
	if(NumChildren() != pNode->NumChildren())
		return LTFALSE;

	for(i=0; i < NumChildren(); i++)
	{
		GetChild(i)->SetNode_R(pNode->GetChild(i));
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0044e2d0
Model* AnimNode::GetModel()
{
	return m_pAnim->m_pModel;
}


// ------------------------------------------------------------------------ //
// ModelAnim.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044e2e0
ModelAnim::ModelAnim(Model *pModel)
{
	m_pModel = pModel;
	m_pName = g_EmptyString;
	m_AnimNodes = LTNULL;
	m_Unknown18 = -1;
	m_InterpolationMS = 200;
	m_pRootNode = &m_RootNode;
	m_pRootNode->SetAnim(this);
}

// FUNCTION: LITHTECH 0x0044e340 ??_GModelAnim@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x0044e360
ModelAnim::~ModelAnim()
{
	Term();
}

// FUNCTION: LITHTECH 0x0044e3b0
void ModelAnim::Term()
{
	if(m_AnimNodes)
	{
		m_pModel->m_pAlloc->Free(m_AnimNodes);
		m_AnimNodes = LTNULL;
	}

	m_KeyFrames.Term(m_pModel->m_pAlloc);
	m_RootNode.Term();
	FreeRootNode();
}

// FUNCTION: LITHTECH 0x0044e410
void ModelAnim::SetModel(Model *pModel)
{
	m_pName = (char*)pModel->AddString(m_pName);
	m_pModel = pModel;
	m_pRootNode->SetNode_R(pModel->GetRootNode());
}

// FUNCTION: LITHTECH 0x0044e440
ModelAnim* ModelAnim::Create(Model *pModel)
{
	return new ModelAnim(pModel);
}

// FUNCTION: LITHTECH 0x0044e460
void ModelAnim::FreeRootNode()
{
	if(m_pRootNode)
	{
		if(m_pRootNode != &m_RootNode)
		{
			delete m_pRootNode;
		}
	}

	m_pRootNode = &m_RootNode;
}

// FUNCTION: LITHTECH 0x0044e4a0
LTBOOL ModelAnim::SetupNodeLists(LTBOOL bRebuild)
{
	return PrecalcNodeLists(bRebuild);
}


// FUNCTION: LITHTECH 0x0044e4b0
LTBOOL ModelAnim::PrecalcNodeLists(LTBOOL bRebuild)
{
	LAlloc *pAlloc;

	if(!bRebuild && m_AnimNodes)
		return LTTRUE;

	if(m_AnimNodes)
		m_pModel->m_pAlloc->Free(m_AnimNodes);

	pAlloc = m_pModel->m_pAlloc;
	m_AnimNodes = (AnimNode**)pAlloc->Alloc(m_pModel->NumNodes() * 4);
	if(!m_AnimNodes)
		return LTFALSE;

	{
		uint32 curNodeIndex = 0;
		AnimNode *pRoot = m_pRootNode;
		return pRoot->FillNodeList(curNodeIndex);
	}
}

// FUNCTION: LITHTECH 0x0044e520
uint32 ModelAnim::GetAnimTime()
{
	if(m_KeyFrames.GetSize() > 0)
		return m_KeyFrames[m_KeyFrames.GetSize()-1].m_Time;
	else
		return 0;
}


// ------------------------------------------------------------------------ //
// ModelNode.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044e5b0 ?MNSlot1@ModelNode@@UAEXKKK@Z

// FUNCTION: LITHTECH 0x0044e570 ??0ModelNode@@QAE@XZ
ModelNode::ModelNode()
{
	Clear();
	m_pModel = LTNULL;
}

// FUNCTION: LITHTECH 0x0044e5c0 ??_GModelNode@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x0044e5e0 ??0ModelNode@@QAE@PAVModel@@@Z
ModelNode::ModelNode(Model *pModel)
{
	Clear();
	m_pModel = pModel;
}

// FUNCTION: LITHTECH 0x0044e610
ModelNode::~ModelNode()
{
	Term();
}

// FUNCTION: LITHTECH 0x0044e650
void ModelNode::Term()
{
	LAlloc *pAlloc;
	uint32 i;

	pAlloc = m_pModel->m_pAlloc;
	for(i=0; i < NumChildren(); i++)
	{
		LDelete(pAlloc, m_Children[i]);
	}

	m_Children.Term(pAlloc);

	Clear();
}

// FUNCTION: LITHTECH 0x0044e6c0
void ModelNode::Clear()
{
	m_pName = g_EmptyString;
	m_NodeIndex = 0;
	m_Flags = 0;
	m_vOffsetFromParent.Init();
	m_mGlobalTransform.Identity();
	m_mInvGlobalTransform.Identity();
}

// FUNCTION: LITHTECH 0x0044e770
ModelNode* ModelNode::Create(Model *pModel)
{
	return new ModelNode(pModel);
}

// FUNCTION: LITHTECH 0x0044e790
void ModelNode::SetModel(Model *pModel)
{
	uint32 i;

	m_pModel = pModel;
	m_pName = (char*)pModel->AddString(m_pName);

	for(i=0; i < NumChildren(); i++)
	{
		GetChild(i)->SetModel(pModel);
	}
}

// FUNCTION: LITHTECH 0x0044e7e0
uint32 ModelNode::CalcNumNodes()
{
	uint32 i, total;

	total = 1;
	for(i=0; i < NumChildren(); i++)
		total += GetChild(i)->CalcNumNodes();

	return total;
}


// FUNCTION: LITHTECH 0x0044e810
LTBOOL ModelNode::FillNodeList(uint32 &curNodeIndex)
{
	uint32 i;

	if(curNodeIndex >= m_pModel->m_FlatNodeList.GetSize())
		return LTFALSE;

	m_NodeIndex = (uint16)curNodeIndex;
	m_pModel->m_FlatNodeList[curNodeIndex] = this;
	++curNodeIndex;

	for(i=0; i < NumChildren(); i++)
	{
		if(!GetChild(i)->FillNodeList(curNodeIndex))
			return LTFALSE;
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0044e870
void ModelNode::SetParent_R(uint32 iParent)
{
	uint32 i;

	m_iParentNode = iParent;
	for(i=0; i < NumChildren(); i++)
	{
		GetChild(i)->SetParent_R(m_NodeIndex);
	}
}


// ------------------------------------------------------------------------ //
// AnimInfo / PieceLOD / ModelPiece / WeightSet.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044e540
AnimInfo::AnimInfo()
{
	m_pAnim = LTNULL;
	m_pChildInfo = LTNULL;
	m_vDims.Init(128.0f, 16.0f, 16.0f);
	m_vTranslation.Init(0.0f, 0.0f, 0.0f);
}

// FUNCTION: LITHTECH 0x0044e8b0 ??0PieceLOD@@QAE@PAVModel@@@Z
PieceLOD::PieceLOD(Model *pModel)
{
	Init(pModel);
}

// FUNCTION: LITHTECH 0x0044e910 ??0PieceLOD@@QAE@XZ
PieceLOD::PieceLOD()
{
	Init(LTNULL);
}

// FUNCTION: LITHTECH 0x0044e970
void PieceLOD::Init(Model *pModel)
{
	m_pModel = pModel;
}

// FUNCTION: LITHTECH 0x0044e980
PieceLOD::~PieceLOD()
{
	m_Tris.Term(m_pModel->GetAlloc());
	m_Verts.Term(m_pModel->GetAlloc());
}

// FUNCTION: LITHTECH 0x0044ea20
ModelPiece::ModelPiece(Model *pModel) : PieceLOD(pModel)
{
	m_Name[0] = 0;
	m_TextureIndex = 0;
	m_pPieceModel = pModel;
	m_SpecularScale = 1.0f;
	m_LODWeight = 1.0f;
	m_SpecularPower = 5.0f;
}

// FUNCTION: LITHTECH 0x0044ea60
ModelPiece::~ModelPiece()
{
	Term();
}

// FUNCTION: LITHTECH 0x0044eac0
void ModelPiece::Term()
{
	LAlloc *pAlloc;

	pAlloc = m_pPieceModel->m_pAlloc;
	m_LODs.Term(pAlloc);
}

// FUNCTION: LITHTECH 0x0044eb20
WeightSet::WeightSet(Model *pModel)
{
	m_Name[0] = 0;
	m_pModel = pModel;
}

// FUNCTION: LITHTECH 0x0044eb50
WeightSet::~WeightSet()
{
	m_Weights.Term(m_pModel->m_pAlloc);
}


// FUNCTION: LITHTECH 0x0044ebb0
ModelSocket::ModelSocket()
{
	m_Name[0] = 0;
	m_iNode = 0;
	m_Pos.Init();
	m_Rot.Init();
	m_Unknown30 = 0;
}


// ------------------------------------------------------------------------ //
// Model.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044ebe0
Model::Model(LAlloc *pAlloc, LAlloc *pDefAlloc) :
	m_StringList(pAlloc),
	m_LODDist0(0.0f)
{
	m_pAlloc = pAlloc;
	m_pDefAlloc = pDefAlloc;
	m_GlobalRadius = 96.0f;
	m_Link.m_pData = this;

	m_pFilename = g_pNoModelFilename;
	m_nTotalVerts = 0;
	m_Flags = 0;
	m_RefCount = 0;
	m_FlatNodeList = CMoArray<ModelNode*, NoCache>();

	m_nNodeDWords = 0;
	m_VisRadius = DEFAULT_MODEL_VIS_RADIUS;
	m_bNoAnimation = LTFALSE;
	SetFadeRange(20000.0f, 20000.0f);
	m_FadeSpriteSizeY = 100.0f;
	m_FadeSpriteSizeX = 100.0f;
	m_ShadowProjectLength = 200.0f;
	m_ShadowLightDist = 200.0f;
	m_ShadowSizeX = 50.0f;
	m_ShadowSizeY = 50.0f;
	m_pFadeSpriteTex = LTNULL;
	m_AmbientLight = 0.25f;
	m_DirLight = 0.75f;
	m_bFovOffset = LTFALSE;
	m_bSpecularEnable = LTFALSE;
	m_bShadowEnable = LTFALSE;
	m_ShadowCenterOffset.Init();
	m_bRigid = LTFALSE;
	m_iNormalRefNode = (uint32)-1;
	m_iNormalRefAnim = (uint32)-1;
	m_bNormalRef = LTFALSE;
	m_Transforms = CMoArray<LTMatrix, NoCache>();

	m_CommandString = g_EmptyString;
	m_pRootNode = &m_RootNode;
	m_pRootNode->SetModel(this);

	m_nChildModels = 1;
	m_ChildModels[0] = &m_SelfChildModel;
	GetSelfChildModel()->m_pParentModel = this;
	GetSelfChildModel()->m_pModel = this;
	m_SelfChildModel.m_ModelStamp = rand();
	m_Unknown190 = 0;

	// Memory tracking
	g_ModelMemory += sizeof(Model);
}

// FUNCTION: LITHTECH 0x0044eed0 ??_GModel@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x0044eef0
Model::~Model()
{
	Term(LTTRUE);

	// Memory tracking
	g_ModelMemory -= sizeof(Model);
	g_ModelMemory -= m_BlockAlloc.GetBlockSize();
}

// FUNCTION: LITHTECH 0x0044f050
void Model::Term(LTBOOL bX)
{
	m_Nexus.Term();

	if(m_pRootNode != &m_RootNode)
	{
		LDelete(GetAlloc(), m_pRootNode);
	}

	m_RootNode.Term();
	m_pRootNode = &m_RootNode;
	DeleteAndClearArray2(m_Pieces, GetAlloc());

	TermAnims();
	TermChildModels(bX);
	DeleteAndClearArray2(m_Sockets, GetAlloc());
	DeleteAndClearArray2(m_WeightSets, GetAlloc());
	m_Transforms.Term(GetAlloc());
	m_VertexWeights.Term(GetAlloc());
	m_FlatNodeList.Term(GetAlloc());
	m_StringList.Term();
	FreeFilename();
	m_LODDists.Term(GetAlloc());
	m_nTotalVerts = 0;
}

// FUNCTION: LITHTECH 0x0044f200
void Model::TermAnims()
{
	uint32 i;
	ModelAnim *pAnim;

	for(i=0; i < NumAnims(); i++)
	{
		pAnim = GetAnim(i);
		if(pAnim->m_pModel == this)
		{
			LDelete(GetAlloc(), pAnim);
		}
	}

	m_Anims.Term(m_pDefAlloc);
}

// FUNCTION: LITHTECH 0x0044f280
void Model::TermChildModels(LTBOOL bX)
{
	uint32 i;
	ChildInfo *pInfo;

	for(i=0; i < NumChildModels(); i++)
	{
		pInfo = GetChildModel(i);
		if(pInfo)
		{
			if(pInfo != GetSelfChildModel())
			{
				if(pInfo->m_pModel && pInfo->m_pModel != this)
				{
					if(pInfo->m_pModel->m_RefCount > 0)
						pInfo->m_pModel->m_RefCount--;

					if(bX)
					{
						if(pInfo->m_pModel->m_RefCount == 0 && pInfo->m_pModel)
							pInfo->m_pModel->Delete();
					}
				}
			}

			pInfo->Term();
			if(pInfo != GetSelfChildModel() && pInfo != &m_SelfChildModel)
			{
				LDelete(GetAlloc(), pInfo);
			}
		}
	}

	m_nChildModels = 1;
	m_ChildModels[0] = &m_SelfChildModel;
}

// FUNCTION: LITHTECH 0x0044f360
LTBOOL Model::PostLoadNodes(LTBOOL bX)
{
	uint32 i, curVert;

	if(AllocTransforms(bX) && AllocFlatNodeList(bX))
	{
		curVert = 0;
		for(i=0; i < NumPieces(); i++)
		{
			GetPiece(i)->m_VertOffset = curVert;
			curVert += GetPiece(i)->m_Verts.GetSize();
		}

		for(i=0; i < NumAnims(); i++)
		{
			if(GetAnim(i)->m_pModel == this)
			{
				if(!GetAnim(i)->SetupNodeLists(bX))
					return LTFALSE;
			}
		}

		m_nTotalVerts = CalcNumVerts();
		m_nTotalTris = CalcNumTris(0);
		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0044f410
LTBOOL Model::AllocTransforms(LTBOOL bForce)
{
	uint32 nNodes;

	if(!bForce && NumNodes() == m_pRootNode->CalcNumNodes())
		return LTTRUE;

	m_Transforms.Term();
	nNodes = m_pRootNode->CalcNumNodes();
	if(!m_Transforms.SetSize2(nNodes, GetAlloc()))
		return LTFALSE;

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0044f4b0
LTBOOL Model::AllocFlatNodeList(LTBOOL bForce)
{
	uint32 nNodes, curNodeIndex;

	if(!bForce && m_FlatNodeList.GetSize() == NumNodes())
		return LTTRUE;

	m_FlatNodeList.Term(GetAlloc());
	nNodes = NumNodes();
	if(!m_FlatNodeList.SetSizeInit4(nNodes, LTNULL, GetAlloc()))
		return LTFALSE;

	curNodeIndex = 0;
	if(!GetRootNode()->FillNodeList(curNodeIndex))
		return LTFALSE;

	m_nNodeDWords = nNodes / 4;
	if(nNodes % 4)
		m_nNodeDWords++;

	GetRootNode()->SetParent_R((uint32)-1);
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0044f590
ModelPiece* Model::FindPiece(const char *pName, uint32 *index)
{
	uint32 i;
	ModelPiece *pPiece;

	for(i=0; i < NumPieces(); i++)
	{
		pPiece = GetPiece(i);
		if(stricmp(pName, pPiece->m_Name) == 0)
		{
			if(index)
				*index = i;

			return pPiece;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f5e0
WeightSet* Model::FindWeightSet(const char *pName, uint32 *index)
{
	uint32 i;
	WeightSet *pSet;

	for(i=0; i < m_WeightSets.GetSize(); i++)
	{
		pSet = m_WeightSets[i];
		if(stricmp(pName, (const char*)pSet) == 0)
		{
			if(index)
				*index = i;

			return pSet;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f630
const char* Model::AddString(const char *pStr)
{
	return m_StringList.AddString(pStr);
}


// FUNCTION: LITHTECH 0x0044f640
ModelNode* Model::FindNode(const char *pName, uint32 *index)
{
	uint32 i;

	for(i=0; i < NumNodes(); i++)
	{
		if(stricmp(GetNode(i)->GetName(), pName) == 0)
		{
			if(index)
				*index = i;

			return GetNode(i);
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f6a0
ModelAnim* Model::FindAnim(const char *pName, uint32 *index, AnimInfo **ppInfo)
{
	uint32 i;
	ModelAnim *pAnim;

	for(i=0; i < NumAnims(); i++)
	{
		pAnim = GetAnim(i);
		if(stricmp(pAnim->m_pName, pName) == 0)
		{
			if(index)
				*index = i;

			if(ppInfo)
				*ppInfo = GetAnimInfo(i);

			return pAnim;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f720
AnimInfo* Model::FindAnimInfo(const char *pAnimName, Model *pOwner, uint32 *index)
{
	uint32 i;
	AnimInfo *pInfo;

	for(i=0; i < NumAnims(); i++)
	{
		pInfo = GetAnimInfo(i);
		if(pInfo->m_pAnim->m_pModel == pOwner && stricmp(pInfo->m_pAnim->m_pName, pAnimName) == 0)
		{
			if(index)
				*index = i;

			return pInfo;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f790
void Model::SetNodeParentOffsets()
{
	uint32 i;
	ModelNode *pNode, *pParent;
	LTMatrix mLocal;

	for(i=0; i < NumNodes(); i++)
	{
		pNode = GetNode(i);

		if(pNode->m_iParentNode < NumNodes())
		{
			pParent = GetNode(pNode->m_iParentNode);
			mLocal = pParent->m_mGlobalTransform.MakeInverseTransform() * pNode->m_mGlobalTransform;
			mLocal.GetTranslation(pNode->m_vOffsetFromParent);
		}
		else
		{
			pNode->m_vOffsetFromParent.Init();
		}
	}
}

// FUNCTION: LITHTECH 0x0044f830 ?Mat_InverseTransformation@@YAXPAVLTMatrix@@0@Z
// The header-emitted AnimTimeRef constructor (0x0044f8f0) and LTMatrix::Init (0x00450000) copies are annotated in
// shared/objectmgr.cpp, which owns the emitted object copies.

// FUNCTION: LITHTECH 0x0044f920
uint32 Model::CalcNumTris(uint32 iLOD)
{
	uint32 i, total;
	ModelPiece *pPiece;
	PieceLOD *pLOD;

	total = 0;
	for(i=0; i < NumPieces(); i++)
	{
		pPiece = GetPiece(i);
		pLOD = pPiece->GetLOD(iLOD);
		if(pLOD)
			total += pLOD->m_Tris.GetSize();
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044f970
uint32 Model::CalcNumVerts()
{
	uint32 i, total;

	total = 0;
	for(i=0; i < NumPieces(); i++)
	{
		total += GetPiece(i)->m_Verts.GetSize();
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044f990
uint32 Model::CalcNumChildModelAnims(LTBOOL bIncludeSelf)
{
	uint32 i, total;
	ChildInfo *pChildModel;

	total = 0;
	for(i=0; i < NumChildModels(); i++)
	{
		pChildModel = GetChildModel(i);

		if(!pChildModel->m_pModel)
			continue;

		if(pChildModel == GetSelfChildModel() && !bIncludeSelf)
			continue;

		total += pChildModel->m_pModel->m_Anims.GetSize();
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044f9e0
uint32 Model::CalcNumParentAnims()
{
	uint32 i, total;

	total = 0;
	for(i=0; i < NumAnims(); i++)
	{
		if(GetAnim(i)->m_pModel == this)
			++total;
	}

	return total;
}


// Parses the model's command string (the "ModelEdit" properties).
// Keep the NormalRef reset stores in their original order. Reload the node index through a volatile pointer after
// SetupTransforms so VC6 uses the same register and load sequence for the transform copy.
// FUNCTION: LITHTECH 0x0044fa10
void Model::ParseCommandString()
{
	struct FloatCommand
	{
		const char	*m_pName;
		float		*m_pValue;
	};

	ConParse parse;
	TransformMaker maker;
	FloatCommand floatCommands[7];
	uint32 i;

	floatCommands[0].m_pName = "VisRadius";				floatCommands[0].m_pValue = &m_VisRadius;
	floatCommands[1].m_pName = "AmbientLight";			floatCommands[1].m_pValue = &m_AmbientLight;
	floatCommands[2].m_pName = "DirLight";				floatCommands[2].m_pValue = &m_DirLight;
	floatCommands[3].m_pName = "ShadowProjectLength";	floatCommands[3].m_pValue = &m_ShadowProjectLength;
	floatCommands[4].m_pName = "ShadowLightDist";		floatCommands[4].m_pValue = &m_ShadowLightDist;
	floatCommands[5].m_pName = "ShadowSizeX";			floatCommands[5].m_pValue = &m_ShadowSizeX;
	floatCommands[6].m_pName = "ShadowSizeY";			floatCommands[6].m_pValue = &m_ShadowSizeY;

	if(!m_CommandString)
		return;

	m_bFovOffset = LTFALSE;
	parse.Init(m_CommandString);
	while(parse.Parse())
	{
		switch(parse.m_nArgs)
		{
			case 1:
			{
				if(stricmp("NoAnimation", parse.m_Args[0]) == 0)
					m_bNoAnimation = LTTRUE;
				else if(stricmp("ShadowEnable", parse.m_Args[0]) == 0)
					m_bShadowEnable = LTTRUE;
				else if(stricmp("SpecularEnable", parse.m_Args[0]) == 0)
					m_bSpecularEnable = LTTRUE;
				else if(stricmp("Rigid", parse.m_Args[0]) == 0)
					m_bRigid = LTTRUE;
			}
			break;

			case 2:
			{
				for(i=0; i < 7; i++)
				{
					if(floatCommands[i].m_pName && stricmp(floatCommands[i].m_pName, parse.m_Args[0]) == 0)
						*floatCommands[i].m_pValue = (float)atof(parse.m_Args[1]);
				}

				if(stricmp("FadeRangeMin", parse.m_Args[0]) == 0)
				{
					SetFadeRange((float)atof(parse.m_Args[1]), m_FadeRangeMax);
				}
				else if(stricmp("FadeRangeMax", parse.m_Args[0]) == 0)
				{
					SetFadeRange(m_FadeRangeMin, (float)atof(parse.m_Args[1]));
				}
				else if(stricmp("FovXOffset", parse.m_Args[0]) == 0)
				{
					m_FovXOffset = MATH_DEGREES_TO_RADIANS((float)atof(parse.m_Args[1]));
					m_bFovOffset = LTTRUE;
				}
				else if(stricmp("FovYOffset", parse.m_Args[0]) == 0)
				{
					m_FovYOffset = MATH_DEGREES_TO_RADIANS((float)atof(parse.m_Args[1]));
					m_bFovOffset = LTTRUE;
				}
			}
			break;

			case 3:
			{
				if(stricmp("FadeSpriteSize", parse.m_Args[0]) == 0)
				{
					m_FadeSpriteSizeX = (float)atof(parse.m_Args[1]);
					m_FadeSpriteSizeY = (float)atof(parse.m_Args[2]);
				}
				else if(stricmp("NormalRef", parse.m_Args[0]) == 0)
				{
					// Node name and animation name: the reference transform is the first frame of the animation.
					m_bNormalRef = LTFALSE;
					m_iNormalRefNode = (uint32)-1;
					if(FindNode(parse.m_Args[1], &m_iNormalRefNode))
					{
						m_iNormalRefAnim = (uint32)-1;
						if(FindAnim(parse.m_Args[2], &m_iNormalRefAnim, LTNULL))
						{
							maker.m_Anims[0].Init(this, m_iNormalRefAnim, 0, m_iNormalRefAnim, 0, 0.0f);
							maker.m_nAnims = 1;
							if(maker.SetupTransforms())
							{
								volatile uint32 *normalNode = &m_iNormalRefNode;
								m_mNormalRef = m_Transforms[*normalNode];
								m_bNormalRef = LTTRUE;
							}
						}
					}
				}
			}
			break;

			case 4:
			{
				if(stricmp("ShadowCenterOffset", parse.m_Args[0]) == 0)
				{
					m_ShadowCenterOffset.x = (float)atof(parse.m_Args[1]);
					m_ShadowCenterOffset.y = (float)atof(parse.m_Args[2]);
					m_ShadowCenterOffset.z = (float)atof(parse.m_Args[3]);
				}
			}
			break;
		}
	}
}

// FUNCTION: LITHTECH 0x0044fed0 ?Init@AnimTimeRef@@QAEXPAVModel@@KKKKM@Z

// FUNCTION: LITHTECH 0x0044ff10
void Model::SetFadeRange(float fMin, float fMax)
{
	m_FadeRangeMin = fMin;
	m_FadeRangeMinSqr = fMin * fMin;
	m_FadeRangeMax = fMax;
	m_FadeRangeMaxSqr = fMax * fMax;
}

// FUNCTION: LITHTECH 0x0044ff50
LTBOOL Model::SetFilename(const char *pInFilename)
{
	char *pFilename;

	FreeFilename();

	pFilename = new char[strlen(pInFilename) + 1];
	if(!pFilename)
		return LTFALSE;

	strcpy(pFilename, pInFilename);
	m_pFilename = pFilename;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0044ffa0
void Model::FreeFilename()
{
	if(m_pFilename != g_pNoModelFilename)
	{
		delete [] m_pFilename;
		m_pFilename = g_pNoModelFilename;
	}
	else
	{
		m_pFilename = g_pNoModelFilename;
	}
}


// FUNCTION: LITHTECH 0x0044ffd0
LTBOOL Model::VerifyChildModelTree(Model *pChild, ModelNode* &pErrNode)
{
	return VerifyChildModel_R(GetRootNode(), pChild->GetRootNode(), pErrNode);
}


// FUNCTION: LITHTECH 0x00450080
LTBOOL Model::InitChildInfo(uint32 index, ChildInfo *pChildModel, Model *pModel, const char *pFilename)
{
	if(index >= MAX_CHILD_MODELS)
		return LTFALSE;

	pChildModel->m_pFilename = pFilename;
	pChildModel->m_pParentModel = this;
	pChildModel->m_pModel = pModel;
	pChildModel->m_AnimOffset = 0;

	if(pModel)
	{
		if(pModel != this)
		{
			pModel->m_RefCount++;
		}
	}

	m_ChildModels[index] = pChildModel;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x004500e0
ModelSocket* Model::FindSocket(const char *pName, uint32 *index)
{
	uint32 i;
	ModelSocket *pSocket;

	for(i=0; i < NumSockets(); i++)
	{
		pSocket = GetSocket(i);
		if(stricmp(pSocket->m_Name, pName) == 0)
		{
			if(index)
				*index = i;

			return pSocket;
		}
	}

	return LTNULL;
}


// ------------------------------------------------------------------------ //
// CMoArray instances.
// The linker kept model.obj's copies of the arrays used by the model classes
// (0x00450140-0x004552c0, after this file's code).  They are instantiated by the
// constructors, destructors and Term functions above.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00450140 ?GenAppend@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEHAAVNodeKeyFrame@@@Z
// FUNCTION: LITHTECH 0x004502a0 ??4LTRotation@@QAEAAV0@ABV0@@Z
// FUNCTION: LITHTECH 0x004502c0 ?GenRemoveAt@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450440 ?GenCopyList@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VNodeKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00450590 ?GenAppendList@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VNodeKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x004506c0 ?GenRemoveAt@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450790 ?GenRemoveAll@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x004507c0 ?GenAppend@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEHAAVAnimKeyFrame@@@Z
// FUNCTION: LITHTECH 0x004508b0 ?GenRemoveAt@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004509b0 ?GenCopyList@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VAnimKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00450ae0 ?GenAppendList@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VAnimKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00450be0 ?GenFindElement@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UBEHABVAnimKeyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450c10 ?GenGetNext@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UBE?AVNodeRelation@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450c60 ?GenGetAt@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UBE?AVNodeRelation@@AAVGenListPos@@@Z
// VC6 keeps the NodeRelation copy used by this specialization out of line; the other array instantiations
// inline their copies. Keep Insert2 specialized so its copy sites retain that compiler decision.
// FUNCTION: LITHTECH 0x00450cb0 ?GenAppend@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEHAAVNodeRelation@@@Z
// FUNCTION: LITHTECH 0x00450ea0 ?GenRemoveAt@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451040 ?GenRemoveAll@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00451070 ?GenCopyList@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEHABV?$GenList@VNodeRelation@@@@@Z
// FUNCTION: LITHTECH 0x004511d0 ?GenAppendList@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEHABV?$GenList@VNodeRelation@@@@@Z
// FUNCTION: LITHTECH 0x00451300 ?GenFindElement@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UBEHABVNodeRelation@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451330 ?GenAppend@?$CMoArray@VModelVert@@VNoCache@@@@UAEHAAVModelVert@@@Z
// FUNCTION: LITHTECH 0x00451420 ?GenRemoveAt@?$CMoArray@VModelVert@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451520 ?GenCopyList@?$CMoArray@VModelVert@@VNoCache@@@@UAEHABV?$GenList@VModelVert@@@@@Z
// FUNCTION: LITHTECH 0x00451650 ?GenAppendList@?$CMoArray@VModelVert@@VNoCache@@@@UAEHABV?$GenList@VModelVert@@@@@Z
// FUNCTION: LITHTECH 0x00451750 ?GenGetNext@?$CMoArray@VModelVert@@VNoCache@@@@UBE?AVModelVert@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451780 ?GenAppend@?$CMoArray@VModelTri@@VNoCache@@@@UAEHAAVModelTri@@@Z
// FUNCTION: LITHTECH 0x00451900 ?GenRemoveAt@?$CMoArray@VModelTri@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451aa0 ?GenCopyList@?$CMoArray@VModelTri@@VNoCache@@@@UAEHABV?$GenList@VModelTri@@@@@Z
// FUNCTION: LITHTECH 0x00451bf0 ?GenAppendList@?$CMoArray@VModelTri@@VNoCache@@@@UAEHABV?$GenList@VModelTri@@@@@Z
// FUNCTION: LITHTECH 0x00451d10 ?GenFindElement@?$CMoArray@VModelVert@@VNoCache@@@@UBEHABVModelVert@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451d40 ?GenGetNext@?$CMoArray@VPieceLOD@@VNoCache@@@@UBE?AVPieceLOD@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451da0 ?GenGetAt@?$CMoArray@VPieceLOD@@VNoCache@@@@UBE?AVPieceLOD@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451e00 ?GenAppend@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEHAAVPieceLOD@@@Z
// FUNCTION: LITHTECH 0x00451f60 ?GenRemoveAt@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452190 ??4ModelTri@@QAEXABV0@@Z
// FUNCTION: LITHTECH 0x004521d0 ?GenRemoveAll@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00452220 ?GenCopyList@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEHABV?$GenList@VPieceLOD@@@@@Z
// FUNCTION: LITHTECH 0x00452420 ?GenAppendList@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEHABV?$GenList@VPieceLOD@@@@@Z
// FUNCTION: LITHTECH 0x00452610 ?GenGetNext@?$CMoArray@MVDefaultCache@@@@UBEMAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452630 ?GenGetAt@?$CMoArray@MVDefaultCache@@@@UBEMAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452640 ?GenCopyList@?$CMoArray@MVDefaultCache@@@@UAEHABV?$GenList@M@@@Z
// FUNCTION: LITHTECH 0x00452760 ?GenAppendList@?$CMoArray@MVDefaultCache@@@@UAEHABV?$GenList@M@@@Z
// FUNCTION: LITHTECH 0x00452830 ?GenAppend@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEHAAPAVModelNode@@@Z
// FUNCTION: LITHTECH 0x004528d0 ?GenCopyList@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEHABV?$GenList@PAVModelNode@@@@@Z
// FUNCTION: LITHTECH 0x004529d0 ?GenAppendList@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEHABV?$GenList@PAVModelNode@@@@@Z
// FUNCTION: LITHTECH 0x00452aa0 ?GenGetNext@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UBE?AVAnimKeyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452ad0 ?GenGetAt@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UBE?AVAnimKeyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452b00 ?GenAppend@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEHAAVNewVertexWeight@@@Z
// FUNCTION: LITHTECH 0x00452bf0 ?GenRemoveAt@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452cf0 ?GenCopyList@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEHABV?$GenList@VNewVertexWeight@@@@@Z
// FUNCTION: LITHTECH 0x00452e20 ?GenAppendList@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEHABV?$GenList@VNewVertexWeight@@@@@Z
// FUNCTION: LITHTECH 0x00452f20 ?GenGetNext@?$CMoArray@VLTMatrix@@VNoCache@@@@UBE?AVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452f50 ?GenGetAt@?$CMoArray@VLTMatrix@@VNoCache@@@@UBE?AVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452f80 ?GenAppend@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEHAAVLTMatrix@@@Z
// FUNCTION: LITHTECH 0x00453070 ?GenRemoveAt@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00453180 ?GenCopyList@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEHABV?$GenList@VLTMatrix@@@@@Z
// FUNCTION: LITHTECH 0x004532b0 ?GenAppendList@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEHABV?$GenList@VLTMatrix@@@@@Z
// FUNCTION: LITHTECH 0x004533b0 ?GenFindElement@?$CMoArray@VLTMatrix@@VNoCache@@@@UBEHABVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004533e0 ?GenAppend@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEHAAVLODDistance@@@Z
// FUNCTION: LITHTECH 0x004534d0 ?GenRemoveAt@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004535c0 ?GenCopyList@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEHABV?$GenList@VLODDistance@@@@@Z
// FUNCTION: LITHTECH 0x004536e0 ?GenAppendList@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEHABV?$GenList@VLODDistance@@@@@Z
// FUNCTION: LITHTECH 0x004537c0 ?GenGetAt@?$CMoArray@PAVModelNode@@VNoCache@@@@UBEPAVModelNode@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004537d0 ?GenSetCacheSize@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEXK@Z
// FUNCTION: LITHTECH 0x004537e0 ?GenGetAt@?$CMoArray@VModelVert@@VNoCache@@@@UBE?AVModelVert@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00453810 ?GenAppend@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEHAAVAnimInfo@@@Z
// FUNCTION: LITHTECH 0x00453900 ?GenRemoveAt@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00453a00 ?GenGetSize@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UBEKXZ
// FUNCTION: LITHTECH 0x00453a10 ?GenCopyList@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEHABV?$GenList@VAnimInfo@@@@@Z
// FUNCTION: LITHTECH 0x00453b40 ?GenAppendList@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEHABV?$GenList@VAnimInfo@@@@@Z
// FUNCTION: LITHTECH 0x00453c40 ??4NodeRelation@@QAEAAV0@ABV0@@Z
// FUNCTION: LITHTECH 0x00453c80 ?SetSize2@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453cf0 ?InternalNiceSetSize@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453e10 ?InternalNiceSetSize@?$CMoArray@PAVModelNode@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453ec0 ?InternalNiceSetSize@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453fb0 ?InternalNiceSetSize@?$CMoArray@VNodeRelation@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454100 ?SetSize2@?$CMoArray@PAVModelNode@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454160 ?SetSize2@?$CMoArray@VModelVert@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004541e0 ?InternalNiceSetSize@?$CMoArray@VModelVert@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004542c0 ?SetSize2@?$CMoArray@VModelTri@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454320 ?InternalNiceSetSize@?$CMoArray@VModelTri@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454440 ?InternalNiceSetSize@?$CMoArray@VPieceLOD@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454590 ?Init@?$CMoArray@PAVModelNode@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454600 ?Init@?$CMoArray@VNewVertexWeight@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454660 ?SetSize2@?$CMoArray@VNewVertexWeight@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004546e0 ?InternalNiceSetSize@?$CMoArray@VNewVertexWeight@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004547d0 ?Init@?$CMoArray@VLTMatrix@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454830 ?SetSize2@?$CMoArray@VLTMatrix@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454890 ?InternalNiceSetSize@?$CMoArray@VLTMatrix@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454960 ?Init@?$CMoArray@VLODDistance@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x004549e0 ?SetSize2@?$CMoArray@VLODDistance@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454a70 ?InternalNiceSetSize@?$CMoArray@VLODDistance@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454b70 ?Init@?$CMoArray@VAnimInfo@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454bd0 ?SetSize2@?$CMoArray@VAnimInfo@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454c50 ?InternalNiceSetSize@?$CMoArray@VAnimInfo@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454d40 ?_DeleteAndDestroyArray@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x00454d60 ?_DeleteAndDestroyArray@?$CMoArray@VPieceLOD@@VNoCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x00454da0 ?BaseDelete@@YAXPAVLAlloc@@PAVModelPiece@@K@Z
// FUNCTION: LITHTECH 0x00454dd0 ?BaseDelete@@YAXPAVLAlloc@@PAVWeightSet@@K@Z
// FUNCTION: LITHTECH 0x00454e00 ?BaseNew@@YAPAVNodeKeyFrame@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00454e20 ?BaseNew@@YAPAVAnimKeyFrame@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00454e60 ?CopyArray2@?$CMoArray@PAVModelNode@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454ef0 ?CopyArray2@?$CMoArray@VModelVert@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454fa0 ?BaseNew@@YAPAVModelVert@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00454fe0 ?CopyArray2@?$CMoArray@VModelTri@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004550a0 ?BaseNew@@YAPAVModelTri@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x004550c0 ?BaseDelete@@YAXPAVLAlloc@@PAVPieceLOD@@K@Z
// FUNCTION: LITHTECH 0x004550f0 ?BaseNew@@YAPAVPieceLOD@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455130 ?BaseNew@@YAPAVNewVertexWeight@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455170 ?CopyArray2@?$CMoArray@VLTMatrix@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00455200 ?BaseNew@@YAPAVLTMatrix@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455220 ?BaseNew@@YAPAVLODDistance@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455260 ?BaseNew@@YAPAVAnimInfo@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x004552a0 ??_GPieceLOD@@QAEPAXI@Z

