// Talon model_load.cpp: loads the ABC (v9-v12) model format.
// Jupiter runtime/model/src/model_load.cpp is the guide, but Talon's format and classes are older
// (see out\loader\spec_talon_abc12.md for the file layout).
// Model::Load uses STLport containers (the engine's copy in lithshared/stl).
// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#include <string.h>
#include <vector>
#include <set>
#include <map>
#include <string>
#include "bdefs.h"
#include "iltstream.h"
#include "model.h"
#include "modelallocations.h"

// Reads an LTRotation (out of line in Talon).
void LTStream_Read(ILTStream *pStream, LTRotation &rot);

// ILTServer::LinkModelToExtraChildModel: child model filename -> extra child models to load with it.
typedef std::set<std::string> ExtraChildSet;
typedef std::map<std::string, ExtraChildSet> ExtraChildMap;





// Seeks to the start of the section with the given name.
// FUNCTION: LITHTECH 0x004552c0
LTBOOL FindSection(ILTStream &file, const char *pSectionName)
{
	char sectionName[256];
	uint32 nextSection, i;

	file.SeekTo(0);

	for(i=0; i < 512; i++)
	{
		file.ReadString(sectionName, sizeof(sectionName));
		file >> nextSection;

		if(strcmp(pSectionName, sectionName) == 0)
			return LTTRUE;

		if(nextSection == (uint32)-1)
			break;

		file.SeekTo(nextSection);
	}

	return LTFALSE;
}


// ------------------------------------------------------------------------ //
// AnimNode.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00455370
// The original keeps &m_Children in a local and calls _DeleteAndDestroyArray out of line in DeleteAndClearArray2.
LTBOOL AnimNode::Load(ILTStream &file)
{
	uint32 i;
	AnimNode *pChild;

	if(!m_KeyFrames.SetSize2(m_pAnim->m_KeyFrames.GetSize(), GetModel()->GetAlloc()))
		return LTFALSE;

	for(i=0; i < m_pAnim->m_KeyFrames.GetSize(); i++)
		file.Read(&m_KeyFrames[i], sizeof(NodeKeyFrame));

	if(!m_Children.SetSizeInit4(m_pNode->NumChildren(), LTNULL, GetModel()->GetAlloc()))
		return LTFALSE;

	for(i=0; i < m_pNode->NumChildren(); i++)
	{
		pChild = LNew_2P(GetModel()->GetAlloc(), AnimNode, GetAnim(), this);
		if(!pChild)
			return LTFALSE;

		pChild->m_pNode = m_pNode->GetChild(i);
		if(!pChild->Load(file))
		{
			LDelete(GetModel()->GetAlloc(), pChild);
			DeleteAndClearArray2(m_Children, GetModel()->GetAlloc());
			return LTFALSE;
		}

		m_Children[i] = pChild;
	}

	return LTTRUE;
}



// ------------------------------------------------------------------------ //
// ModelAnim.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00455530
LTBOOL ModelAnim::Load(ILTStream &file)
{
	uint32 i, nKeyFrames;
	AnimKeyFrame *pKeyFrame;

	Term();

	if(m_pModel->LoadString(file, m_pName))
	{
		if(m_pModel->m_FileVersion >= 11)
			file >> m_Unknown18;

		if(m_pModel->m_FileVersion >= 12)
			file >> m_InterpolationMS;

		file >> nKeyFrames;
		if(!m_KeyFrames.SetSize2(nKeyFrames, m_pModel->GetAlloc()))
			return LTFALSE;

		for(i=0; i < nKeyFrames; i++)
		{
			pKeyFrame = &m_KeyFrames[i];
			file >> pKeyFrame->m_Time;
			if(!m_pModel->LoadString(file, pKeyFrame->m_pString))
				return LTFALSE;
		}

		m_pRootNode->m_pNode = m_pModel->GetRootNode();
		if(m_pRootNode->Load(file))
			return SetupNodeLists(LTTRUE);
	}

	return LTFALSE;
}


// ------------------------------------------------------------------------ //
// ModelNode.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00455670
// Only the tail differs: the original calls CMoArray::_DeleteAndDestroyArray out of line in DeleteAndClearArray2.
LTBOOL ModelNode::Load(ILTStream &file)
{
	uint32 i, j, nChildren;
	ModelNode *pChild;
	LTMatrix mGlobalTransform;

	if(!m_pModel->LoadString(file, m_pName))
		return LTFALSE;

	file >> m_NodeIndex;
	file >> m_Flags;

	// Load the global transform.
	for(i=0; i < 4; i++)
		for(j=0; j < 4; j++)
			file >> mGlobalTransform.m[i][j];

	SetGlobalTransform(mGlobalTransform);

	// Load the child nodes.
	file >> nChildren;

	if(!m_Children.SetSizeInit4(nChildren, LTNULL, m_pModel->GetAlloc()))
		return LTFALSE;

	for(i=0; i < nChildren; i++)
	{
		pChild = LNew_1P(m_pModel->GetAlloc(), ModelNode, m_pModel);
		if(!pChild)
			return LTFALSE;

		if(!pChild->Load(file))
		{
			LDelete(m_pModel->GetAlloc(), pChild);
			DeleteAndClearArray2(m_Children, m_pModel->GetAlloc());
			return LTFALSE;
		}

		m_Children[i] = pChild;
	}

	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// AnimInfo.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00455870
LTBOOL AnimInfo::Load(ILTStream &file)
{
	file >> m_vDims.x;
	file >> m_vDims.y;
	file >> m_vDims.z;

	if(m_pAnim)
		return m_pAnim->Load(file);

	return LTFALSE;
}


// ------------------------------------------------------------------------ //
// ChildInfo.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004558c0
LTBOOL ChildInfo::Load(ILTStream &file)
{
	uint32 i;

	if(!m_pParentModel)
		return LTFALSE;

	file >> m_ModelStamp;

	if(!m_Relation.SetSize2(m_pParentModel->NumNodes(), m_pParentModel->GetAlloc()))
		return LTFALSE;

	for(i=0; i < m_pParentModel->NumNodes(); i++)
	{
		file >> m_Relation[i].m_Pos;
		file >> m_Relation[i].m_Rot;
	}

	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// PieceLOD / ModelPiece.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004559c0
LTBOOL PieceLOD::Load(ILTStream &file, uint32 &curWeight)
{
	uint32 i, j, nTris, nVerts;
	uint16 val;
	ModelVert *pVert;
	NewVertexWeight *pWeight;

	// Triangles.
	file >> nTris;
	if(!m_Tris.SetSize2(nTris, m_pModel->GetAlloc()))
		return LTFALSE;

	for(i=0; i < nTris; i++)
	{
		for(j=0; j < 3; j++)
		{
			file >> m_Tris[i].m_UVs[j].tu;
			file >> m_Tris[i].m_UVs[j].tv;
			file >> m_Tris[i].m_Indices[j];
		}
	}

	// Vertices.
	file >> nVerts;
	if(!m_Verts.SetSize2(nVerts, m_pModel->GetAlloc()))
		return LTFALSE;

	for(i=0; i < nVerts; i++)
	{
		pVert = &m_Verts[i];

		file >> val;
		pVert->m_nWeights = val;
		file >> val;
		pVert->m_iReplacement = val;

		if(curWeight + pVert->m_nWeights > m_pModel->m_VertexWeights.GetSize())
			return LTFALSE;

		if(pVert->m_nWeights > 0)
		{
			pVert->m_Weights = &m_pModel->m_VertexWeights[curWeight];
			curWeight += pVert->m_nWeights;

			for(j=0; j < pVert->m_nWeights; j++)
			{
				pWeight = &pVert->m_Weights[j];
				file >> pWeight->m_iNode;
				file >> pWeight->m_Vec[0];
				file >> pWeight->m_Vec[1];
				file >> pWeight->m_Vec[2];
				file >> pWeight->m_Vec[3];
			}
		}

		file >> pVert->m_Vec.x;
		file >> pVert->m_Vec.y;
		file >> pVert->m_Vec.z;
		file >> pVert->m_Normal.x;
		file >> pVert->m_Normal.y;
		file >> pVert->m_Normal.z;
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00455cb0
LTBOOL ModelPiece::Load(ILTStream &file, uint32 &curWeight)
{
	uint16 textureIndex, nUnknown;
	uint32 i, nLODs, dummy;

	file >> textureIndex;
	m_TextureIndex = textureIndex;

	file >> m_SpecularPower;
	file >> m_SpecularScale;

	if(m_pPieceModel->m_FileVersion >= 10)
		file >> m_LODWeight;

	file >> nUnknown;
	for(i=0; i < nUnknown; i++)
		file >> dummy;

	nLODs = m_pPieceModel->m_LODDists.GetSize();
	if(!m_LODs.SetSize2(nLODs, m_pPieceModel->GetAlloc()))
		return LTFALSE;

	for(i=0; i < nLODs; i++)
		m_LODs[i].Init(m_pPieceModel);

	if(file.ReadString(m_Name, sizeof(m_Name)) != LT_OK)
		return LTFALSE;

	if(!PieceLOD::Load(file, curWeight))
		return LTFALSE;

	for(i=0; i < nLODs; i++)
	{
		if(!m_LODs[i].Load(file, curWeight))
			return LTFALSE;
	}

	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// WeightSet.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00455e40
LTBOOL WeightSet::Load(ILTStream &file)
{
	uint32 i, nWeights;

	if(file.ReadString(m_Name, sizeof(m_Name)) != LT_OK)
		return LTFALSE;

	file >> nWeights;
	if(!m_Weights.SetSize2(nWeights, m_pModel->GetAlloc()))
		return LTFALSE;

	for(i=0; i < nWeights; i++)
		file >> m_Weights[i];

	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// Model.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00455f10
LTBOOL Model::LoadString(ILTStream &file, char* &pStr)
{
	char tempStr[4096];

	if(file.ReadString(tempStr, sizeof(tempStr)) != LT_OK)
		return LTFALSE;

	pStr = (char*)AddString(tempStr);
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00455f70
LTBOOL Model::LoadAnimBindings(ModelLoadRequest *pRequest, ILTStream &file, LTBOOL bAllowUpdates)
{
	uint32 i, j, nAnimBindings, testIndex;
	ChildInfo *pInfo;
	AnimInfo *pAnimInfo;
	char animName[256];
	LTVector dims, trans;

	if(!FindSection(file, "AnimBindings"))
		return LTFALSE;

	for(i=0; i < NumChildModels(); i++)
	{
		pInfo = GetChildModel(i);

		file >> nAnimBindings;

		// If the model stamp or the number of anims is wrong then the
		// trees need to be marked as out of date.
		if(pInfo->m_pModel)
		{
			if(nAnimBindings != pInfo->m_pModel->CalcNumParentAnims() ||
				pInfo->m_ModelStamp != pInfo->m_pModel->m_SelfChildModel.m_ModelStamp)
			{
				pRequest->m_bTreesValid = LTFALSE;
			}
		}

		for(j=0; j < nAnimBindings; j++)
		{
			if(file.ReadString(animName, sizeof(animName)) != LT_OK)
				return LTFALSE;

			file >> dims;
			file >> trans;

			if(pInfo->m_pModel)
			{
				pAnimInfo = FindAnimInfo(animName, pInfo->m_pModel, &testIndex);
				if(pAnimInfo)
				{
					pAnimInfo->m_vTranslation = trans;
				}
			}
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00456100
LTBOOL Model::LoadSockets(ILTStream &file)
{
	uint32 i, nSockets;
	ModelSocket *pSocket;

	if(!FindSection(file, "Sockets"))
		return LTFALSE;

	file >> nSockets;
	if(!m_Sockets.SetSizeInit4(nSockets, LTNULL, GetAlloc()))
		return LTFALSE;

	for(i=0; i < nSockets; i++)
	{
		pSocket = LNew(GetAlloc(), ModelSocket);
		if(!pSocket)
			return LTFALSE;

		m_Sockets[i] = pSocket;

		file >> pSocket->m_iNode;
		if(pSocket->m_iNode >= NumNodes())
			return LTFALSE;

		if(file.ReadString(pSocket->m_Name, sizeof(pSocket->m_Name)) != LT_OK)
			return LTFALSE;

		file >> pSocket->m_Rot;
		file >> pSocket->m_Pos;
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00456230
LTBOOL Model::LoadWeightSets(ILTStream &file)
{
	uint32 i, nSets;
	WeightSet *pSet;

	DeleteAndClearArray2(m_WeightSets, GetAlloc());

	file >> nSets;
	if(!m_WeightSets.SetSizeInit4(nSets, LTNULL, GetAlloc()))
		return LTFALSE;

	for(i=0; i < m_WeightSets.GetSize(); i++)
	{
		pSet = m_WeightSets[i] = LNew_1P(GetAlloc(), WeightSet, this);
		if(!pSet || !pSet->Load(file))
		{
			DeleteAndClearArray2(m_WeightSets, GetAlloc());
			return LTFALSE;
		}
	}

	return LTTRUE;
}


// Wave 7 decoded the child-model block from the exe (was SIZE 3472 vs 3328; now 3312, 246 aligned mismatches,
// 131 ignoring stack offsets):
// - Behaviour fix: for a child model listed in the extra child map the exe evaluates `(*pExtraChildren)[pName]`
//   twice (begin(), then end()) but never compares: it always adds exactly one extra model (the set's first name)
//   and moves on, i.e. the original's test was discarded (written here as Talon's stray `;`). Ours looped over the
//   whole set.
// - The extra-map branch is the if body (`pExtraChildren && find(pName) != end()`); the copy into the new
//   ChildInfo is ChildInfo's implicit operator= (CMoArray::operator= -> CopyArray2 out of line, then the seven
//   members); InitChildInfo gets childRequest.m_pFilename; the `m_nChildModels > 1` test follows the
//   m_bAllChildrenLoaded store (Jupiter's order). This removed the member/function-pointer stand-ins that forced
//   CopyArray2/_Construct out of line (the exe's _Construct calls are the loop-2 push_backs, not a third-loop copy).
// - iExtra is declared where it is assigned (a function-scope iterator is zero-initialised in the prologue; the exe
//   has no such store; frame 0x154 -> 0x150).
// Left: the exe calls _Construct out of line in all four loop-2 push_backs and ~string out of line after the
// find() test (ours inlines the first push_back's _Construct and the string dtor, but calls map::lower_bound out of
// line at the second operator[] where the exe inlines it): inline budget. Every child-block error does
// `err = N; jmp cleanup`, one shared block after the "Animation" error that calls the four _Vector_base dtors
// (0x00457270) and jumps to Error; ours puts the shared dtor block at the first error. A `ChildError:` label inside
// the block (`goto ChildrenLoaded; ChildError: goto Error;`, or an `if(0)` block) is much worse (577) and breaks
// 0x00457df0 and 0x00458800.
// Earlier waves: childRequest is built before the ModelAllocations ctor; 0x00457e10 is the out-of-line 2-argument
// basic_string::_M_range_initialize<const char*>.
// Wave 7 phase 2 (budget model): B = 5060u (size 2526u). inline_budget.py needed two fixes to run on this unit
// (destructor probes of STLport classes, more probe-filter rounds); it then measures the CMoArray/BaseNew sites but
// none of the STLport costs (their call expressions don't compile in its probes), so it can't place the
// _Construct / ~string / lower_bound decisions. Its exe column confirms the audit up to ICF names: the exe has one more
// _Construct out of line (4 vs our 3), one more ~_String_base (3 vs 2 + one inlined _M_deallocate_block) and no
// out-of-line _Rb_tree lower_bound (ours 2): less budget in the child-model block's first half, more in its second.
// No behaviour difference besides these inlining decisions.
// PARKED: STLport inlining decisions in the child-model block (push_back _Construct, ~string, map lower_bound) and the shared error-cleanup layout; the R11 model can't replay it (model != build on ~50 STLport sites)
// STUB: LITHTECH 0x00456390
LTRESULT Model::Load(ModelLoadRequest *pRequest)
{
	ILTStream *pFile;
	ModelLoadRequest childRequest;
	Model *pChildModel;
	ModelPiece *pPiece;
	ModelAnim *pAnim;
	ChildInfo *pChildInfo, *pInfo;
	ExtraChildMap *pExtraChildren;
	char *pFilename;
	const char *pName;
	ModelNode *pErrNode;
	LTRESULT dResult;
	uint32 i, j, iCurAnim, nAnims, nChildModels, nPieces, nWeights, nLoads;
	uint16 nLODDists;
	uint8 reserved, err;

	childRequest.m_pFilename = LTNULL;
	childRequest.m_pFile = LTNULL;
	childRequest.m_LoadChildFn = DefaultLoadChildFn;
	childRequest.m_pLoadFnUserData = LTNULL;
	childRequest.m_bLoadChildModels = LTTRUE;
	childRequest.m_Unknown18 = LTTRUE;
	childRequest.m_bTreesValid = LTTRUE;
	childRequest.m_bAllChildrenLoaded = LTTRUE;
	childRequest.m_pExtraChildModels = LTNULL;

	ModelAllocations allocs;

	if(!pRequest->m_pFile || (pRequest->m_bLoadChildModels && !pRequest->m_LoadChildFn))
		return LT_INVALIDPARAMS;

	Term(LTFALSE);
	pFile = pRequest->m_pFile;

	// Header.
	if(!LoadHeader(*pFile, allocs))
	{
		err = 0;
		goto Error;
	}

	if(!LoadString(*pFile, m_CommandString))
	{
		err = 1;
		goto Error;
	}

	*pFile >> m_GlobalRadius;
	*pFile >> nLODDists;
	for(i=0; i < 62; i++)
		*pFile >> reserved;

	if(!m_LODDists.SetSize2(nLODDists, GetAlloc()))
		return LTFALSE;

	for(i=0; i < nLODDists; i++)
		*pFile >> m_LODDists[i].m_Dist;

	// Pieces.
	if(!FindSection(*pFile, "Pieces"))
	{
		err = 2;
		goto Error;
	}

	*pFile >> nWeights;
	if(!m_VertexWeights.SetSize2(nWeights, GetAlloc()))
	{
		err = 3;
		goto Error;
	}

	*pFile >> nPieces;
	if(!m_Pieces.SetSizeInit4(nPieces, LTNULL, GetAlloc()))
	{
		err = 4;
		goto Error;
	}

	nWeights = 0;
	for(i=0; i < nPieces; i++)
	{
		pPiece = LNew_1P(GetAlloc(), ModelPiece, this);
		if(!pPiece)
		{
			Term(LTFALSE);
			err = 5;
			goto Error;
		}

		m_Pieces[i] = pPiece;
		if(!pPiece->Load(*pFile, nWeights))
		{
			err = 6;
			goto Error;
		}
	}

	// Nodes.
	if(!FindSection(*pFile, "Nodes"))
	{
		err = 7;
		goto Error;
	}

	if(!m_pRootNode->Load(*pFile))
	{
		err = 8;
		goto Error;
	}

	if(!LoadWeightSets(*pFile))
	{
		err = 9;
		goto Error;
	}

	if(!PostLoadNodes(LTTRUE))
	{
		err = 10;
		goto Error;
	}

	// Child models.
	if(!FindSection(*pFile, "ChildModels"))
	{
		err = 11;
		goto Error;
	}

	*pFile >> nChildModels;
	if(nChildModels == 0 || nChildModels > MAX_CHILD_MODELS)
	{
		err = 12;
		goto Error;
	}

	{
	std::vector<const char*> fileNames, childFileNames;
	std::vector<ChildInfo*> childInfos, childInfoList;

	nLoads = 0;
	pExtraChildren = (ExtraChildMap*)pRequest->m_pExtraChildModels;

	if(!InitChildInfo(0, &m_SelfChildModel, this, "SELF"))
	{
		err = 13;
		goto ChildError;
	}

	if(!m_SelfChildModel.Load(*pFile))
	{
		err = 14;
		goto ChildError;
	}

	InitChildInfo(0, &m_SelfChildModel, this, "SELF");

	for(i=0; i < nChildModels-1; i++)
	{
		pFilename = LTNULL;
		if(!LoadString(*pFile, pFilename))
		{
			err = 15;
			goto ChildError;
		}
		fileNames.push_back(pFilename);

		pChildInfo = new ChildInfo;
		pChildInfo->m_pParentModel = this;
		if(!pChildInfo->Load(*pFile))
		{
			err = 16;
			goto ChildError;
		}
		childInfos.push_back(pChildInfo);
	}

	for(i=0; i < nChildModels-1; i++)
	{
		pName = fileNames[i];
		pChildInfo = childInfos[i];

		if(pExtraChildren && pExtraChildren->find(pName) != pExtraChildren->end())
		{
			// (sic) the stray ';' discards the test: the exe evaluates the second operator[] but always adds
			// exactly one extra model (the set's first name) per child model.
			ExtraChildSet::iterator iExtra = (*pExtraChildren)[pName].begin();
			if(iExtra != (*pExtraChildren)[pName].end());
			{
				m_Unknown190 = 1;
				childFileNames.push_back(AddString((*iExtra).c_str()));
				childInfoList.push_back(pChildInfo);
				nLoads++;
				iExtra++;
			}
		}
		else
		{
			childFileNames.push_back(pName);
			childInfoList.push_back(pChildInfo);
			nLoads++;
		}
	}

	for(i=0; i < nLoads; i++)
	{
		childRequest.m_pFilename = childFileNames[i];
		childRequest.m_pLoadFnUserData = pRequest->m_pLoadFnUserData;
		childRequest.m_LoadChildFn = pRequest->m_LoadChildFn;
		childRequest.m_pFile = LTNULL;
		childRequest.m_bLoadChildModels = LTFALSE;
		pChildModel = LTNULL;

		if(pRequest->m_bLoadChildModels)
		{
			dResult = pRequest->m_LoadChildFn(&childRequest, &pChildModel);
			if(dResult != LT_OK && dResult != LT_NOCHANGE)
			{
				err = 21;
				goto ChildError;
			}
		}

		if(!pChildModel)
			pRequest->m_bAllChildrenLoaded = LTFALSE;

		if(pChildModel && pChildModel->m_nChildModels > 1)
		{
			err = 18;
			goto ChildError;
		}

		pInfo = LNew(GetAlloc(), ChildInfo);
		if(!pInfo)
		{
			err = 19;
			goto ChildError;
		}

		*pInfo = *childInfoList[i];

		if(!InitChildInfo(i+1, pInfo, pChildModel, childRequest.m_pFilename))
		{
			err = 20;
			goto ChildError;
		}

		m_nChildModels++;

		pInfo->m_bTreesValid = LTFALSE;
		if(pInfo->m_pModel)
			pInfo->m_bTreesValid = VerifyChildModelTree(pInfo->m_pModel, pErrNode);

		Sleep(0);
	}

	for(i=0; i < childInfos.size(); i++)
	{
		if(childInfos[i])
			delete childInfos[i];
	}

	}

	// Animations.
	if(!FindSection(*pFile, "Animation"))
	{
		err = 22;
		goto Error;
	}

	*pFile >> nAnims;
	if(!m_Anims.SetSize2(CalcNumChildModelAnims(LTFALSE) + nAnims, m_pDefAlloc))
	{
		err = 23;
		goto Error;
	}

	for(i=0; i < nAnims; i++)
	{
		pAnim = LNew_1P(GetAlloc(), ModelAnim, this);
		if(!pAnim)
		{
			err = 24;
			goto Error;
		}

		m_Anims[i].m_pAnim = pAnim;
		m_Anims[i].m_pChildInfo = &m_SelfChildModel;
		if(!m_Anims[i].Load(*pFile))
		{
			LDelete(GetAlloc(), pAnim);
			err = 25;
			goto Error;
		}
	}

	iCurAnim = nAnims;
	for(i=0; i < NumChildModels(); i++)
	{
		pInfo = GetChildModel(i);
		pChildModel = pInfo->m_pModel;
		if(pChildModel && pChildModel != this)
		{
			pInfo->m_AnimOffset = iCurAnim;
			for(j=0; j < pChildModel->m_Anims.GetSize(); j++)
			{
				m_Anims[iCurAnim] = pChildModel->m_Anims[j];
				m_Anims[iCurAnim].m_pChildInfo = pInfo;
				iCurAnim++;
			}
		}
	}

	if(!LoadAnimBindings(pRequest, *pFile, LTTRUE))
	{
		err = 26;
		goto Error;
	}

	if(!LoadSockets(*pFile))
	{
		err = 27;
		goto Error;
	}

	if(pFile->ErrorStatus() != LT_OK)
	{
		err = 28;
		goto Error;
	}

	if(!PostLoadNodes(LTFALSE))
	{
		err = 29;
		goto Error;
	}

	ParseCommandString();
	SetNodeParentOffsets();
	return LT_OK;

ChildError:
Error:
	Term(LTFALSE);
	return err;
}

// FUNCTION: LITHTECH 0x00457090
LTBOOL Model::LoadHeader(ILTStream &file, ModelAllocations &allocs)
{
	if(!FindSection(file, "Header"))
		return LTFALSE;

	file >> m_FileVersion;
	if(m_FileVersion != 9 && m_FileVersion != 10 && m_FileVersion != 11 && m_FileVersion != 12)
		return LTFALSE;

	return allocs.Load(file) != 0;
}


// FUNCTION: LITHTECH 0x00457100
LTRESULT Model::InitAllocations(ILTStream &file, LAlloc *pDelegate)
{
	ModelAllocations allocs;
	uint32 allocSize;

	if(!LoadHeader(file, allocs))
		return LT_INVALIDFILE;

	if(allocs.CalcAllocationSize(allocSize))
	{
		if(!m_BlockAlloc.Init(pDelegate, allocSize))
			return LT_OUTOFMEMORY;

		m_pAlloc = &m_BlockAlloc;
		m_StringList.SetAlloc(&m_BlockAlloc);
		g_ModelMemory += m_BlockAlloc.GetBlockSize();
	}

	return LT_OK;
}


// ------------------------------------------------------------------------ //
// Template and inline instances this file kept (0x004571a0-0x00459170).
// Most of the range is STLport map/set/string code used by Model::Load.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00458260 ?SetSizeInit3@?$CMoArray@PAVAnimNode@@VNoCache@@@@QAEHKAAPAVAnimNode@@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00458770 ??_GWeightSet@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x004571a0 ??1?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00457260 ??0?$_Vector_base@PBDV?$allocator@PBD@_STL@@@_STL@@QAE@ABV?$allocator@PBD@1@@Z
// FUNCTION: LITHTECH 0x00457270 ??1?$_Vector_base@PBDV?$allocator@PBD@_STL@@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x004572c0 ?insert@?$map@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@_STL@@QAE?AU?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@2@U32@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@2@@Z
// FUNCTION: LITHTECH 0x00457490 ??0?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00457500 ??1?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$_Identity@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QAE@XZ
// FUNCTION: LITHTECH 0x00457590 ??0?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@QAE@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@1@ABV?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@1@@Z
// FUNCTION: LITHTECH 0x00457690 ??0?$_STL_alloc_proxy@PAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@_STL@@U12@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QAE@ABV?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@1@PAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@1@@Z
// FUNCTION: LITHTECH 0x004576a0 ?deallocate@?$allocator@U?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@_STL@@@_STL@@QBEXPAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@I@Z
// FUNCTION: LITHTECH 0x00457800 ?_M_insert_overflow@?$vector@PBDV?$allocator@PBD@_STL@@@_STL@@IAEXPAPBDABQBDI@Z
// FUNCTION: LITHTECH 0x00457970 ?_M_insert_overflow@?$vector@PAVChildInfo@@V?$allocator@PAVChildInfo@@@_STL@@@_STL@@IAEXPAPAVChildInfo@@ABQAV3@I@Z
// FUNCTION: LITHTECH 0x00457af0 ?find@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@_STL@@QAE?AU?$_Rb_tree_iterator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@U?$_Nonconst_traits@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@2@ABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@2@@Z
// FUNCTION: LITHTECH 0x00457bc0 ?_M_copy@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$_Identity@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@AAEPAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@PAU32@0@Z
// FUNCTION: LITHTECH 0x00457ca0 ?_M_erase@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$_Identity@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@AAEXPAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x00457df0 ?_Construct@_STL@@YAXPAPBDABQBD@Z
// FUNCTION: LITHTECH 0x00457e10 ?_M_range_initialize@?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@AAEXPBD0@Z
// FUNCTION: LITHTECH 0x00457ed0 ?_M_increment@?$_Rb_global@_N@_STL@@SAXPAU_Rb_tree_base_iterator@2@@Z
// FUNCTION: LITHTECH 0x00457f10 ??M_STL@@YA_NABV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@0@0@Z
// FUNCTION: LITHTECH 0x00457f80 ?deallocate@?$__node_alloc@$00$0A@@_STL@@SAXPAXI@Z
// FUNCTION: LITHTECH 0x004580c0 ?_M_compare@?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@SAHPBD000@Z
// FUNCTION: LITHTECH 0x00458120 ?destroy_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V12@U?$_Identity@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@IAEXPAU?$_Rb_tree_node@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@Z
// FUNCTION: LITHTECH 0x004582e0 ?_M_insert@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basf2f73133
// FUNCTION: LITHTECH 0x004584c0 ?_Rb_tree_rotate_left@_STL@@YAXPAU_Rb_tree_node_base@1@AAPAU21@@Z
// FUNCTION: LITHTECH 0x00458510 ?_Rb_tree_rotate_right@_STL@@YAXPAU_Rb_tree_node_base@1@AAPAU21@@Z
// FUNCTION: LITHTECH 0x00458560 ?insert_unique@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?9280cdfd
// FUNCTION: LITHTECH 0x00458720 ?_M_decrement@?$_Rb_global@_N@_STL@@SAXPAU_Rb_tree_base_iterator@2@@Z
// FUNCTION: LITHTECH 0x00458790 ??_G?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@QAEPAXI@Z
// FUNCTION: LITHTECH 0x00458800 ?_M_create_node@?$_Rb_tree@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@2@U?$_Select1st@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@@_STL@@IAEPAU?$_Rb_tree_node@U?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@@2@ABU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@2@@Z
// FUNCTION: LITHTECH 0x00458960 ??0?$_Rb_tree_base@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QAE@ABV?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@1@@Z
// FUNCTION: LITHTECH 0x00458ae0 ?get_allocator@?$_Rb_tree_base@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@_STL@@QBE?AV?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@XZ
// FUNCTION: LITHTECH 0x00458af0 ?_M_range_initialize@?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@AAEXPAD0Uforward_iterator_tag@2@@Z
// FUNCTION: LITHTECH 0x00458b90 ?CopyArray2@?$CMoArray@VNodeRelation@@VDefaultCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00458c80 ?_Rebalance@?$_Rb_global@_N@_STL@@SAXPAU_Rb_tree_node_base@2@AAPAU32@@Z
// FUNCTION: LITHTECH 0x00458e30 ?_Construct@_STL@@YAXPAU?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@1@ABU21@@Z
// FUNCTION: LITHTECH 0x00458f40 ?iterator_category@_STL@@YA?AUrandom_access_iterator_tag@1@PBD@Z
// FUNCTION: LITHTECH 0x00458f50 ?_M_allocate_block@?$_String_base@DV?$allocator@D@_STL@@@_STL@@QAEXI@Z
// FUNCTION: LITHTECH 0x00459040 ??0?$pair@$$CBV?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@V?$set@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@U?$less@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@V?$allocator@V?$basic_string@DV?$char_traits@D@_STL@@V?$allocator@D@2@@_STL@@@2@@2@@_STL@@QAE@ABU01@@Z
// FUNCTION: LITHTECH 0x00459140 ?copy@_STL@@YAPADPAD00@Z
