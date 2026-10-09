// Jupiter runtime/model/src/modelallocations.cpp, Talon version.
#include "bdefs.h"
#include "iltstream.h"
#include "modelallocations.h"


//-----------------------------------------------------------------------------
// util : WordAlign( size ) => new size
//-----------------------------------------------------------------------------
inline uint32 WordAlign(uint32 total)
{
	return (total + 3) & ~3;
}


// FUNCTION: LITHTECH 0x00459170
ModelAllocations::ModelAllocations()
{
	Clear();
}


// FUNCTION: LITHTECH 0x00459180
void ModelAllocations::Clear()
{
	m_nKeyFrames = 0;
	m_nParentAnims = 0;
	m_nNodes = 0;
	m_nPieces = 0;
	m_nChildModels = 0;
	m_nTris = 0;
	m_nVerts = 0;
	m_nVertexWeights = 0;
	m_nLODs = 0;
	m_nSockets = 0;
	m_nWeightSets = 0;
	m_nStrings = 0;
	m_StringLengths = 0;
}


// FUNCTION: LITHTECH 0x004591b0
LTBOOL ModelAllocations::Load(ILTStream &str)
{
	str >> m_nKeyFrames;
	str >> m_nParentAnims;
	str >> m_nNodes;
	str >> m_nPieces;
	str >> m_nChildModels;
	str >> m_nTris;
	str >> m_nVerts;
	str >> m_nVertexWeights;
	str >> m_nLODs;
	str >> m_nSockets;
	str >> m_nWeightSets;
	str >> m_nStrings;
	str >> m_StringLengths;

	return str.ErrorStatus() == LT_OK;
}


// FUNCTION: LITHTECH 0x00459270
LTBOOL ModelAllocations::CalcAllocationSize(uint32 &size)
{
	uint32 nChildNodes;

	if(m_nLODs < 1)
		return LTFALSE;

	size = 0;
	nChildNodes = m_nNodes - 1;

	size += m_nStrings * WordAlign(sizeof(ModelString));					// All the strings.
	// The extra 3*m_nStrings is because the string length calculation isn't word aligned..
	size += WordAlign(m_StringLengths + 3 * m_nStrings) + 1000;
	size += WordAlign(sizeof(NodeKeyFrame))		* m_nNodes * m_nKeyFrames;		// AnimNode::m_KeyFrames.
	size += WordAlign(sizeof(AnimNode*))		* m_nParentAnims * nChildNodes;	// AnimNode::m_Children arrays.
	size += WordAlign(sizeof(AnimNode))			* m_nParentAnims * nChildNodes;	// AnimNode::m_Children.
	size += WordAlign(sizeof(AnimKeyFrame))		* m_nKeyFrames;					// ModelAnim::m_KeyFrames.
	size += WordAlign(sizeof(AnimNode*))		* m_nNodes * m_nParentAnims		// ModelAnim::m_AnimNodes.
		+ WordAlign(sizeof(ModelNode*) + sizeof(ModelNode)) * nChildNodes;			// ModelNode::m_Children.
	// Preserve VC6's original operand load order for this product.
	// RULE-EXCEPTION: R17, retail loads m_nChildModels before m_nNodes for this product; the natural
	// `WordAlign(..) * m_nChildModels * m_nNodes` (also grouped, reordered, or via a local/pointer/reference)
	// loads m_nNodes first, and only a volatile read forces the original order.
	volatile uint32 &childModelCount = m_nChildModels;
	size += WordAlign(sizeof(NodeRelation))		* childModelCount * m_nNodes;	// ChildInfo::m_Relation.
	size += WordAlign(sizeof(ModelTri))			* m_nTris;						// PieceLOD::m_Tris.
	size += WordAlign(sizeof(ModelVert))		* m_nVerts;						// PieceLOD::m_Verts.
	size += WordAlign(sizeof(PieceLOD))			* (m_nLODs - 1) * m_nPieces;	// ModelPiece::m_LODs.
	size += WordAlign(sizeof(ModelPiece*))		* m_nPieces;					// Model::m_Pieces array.
	size += WordAlign(sizeof(ModelPiece))		* m_nPieces;					// Model::m_Pieces.
	size += WordAlign(sizeof(float))			* m_nWeightSets * m_nNodes;		// WeightSet::m_Weights.
	size += WordAlign(sizeof(WeightSet*))		* m_nWeightSets;				// Model::m_WeightSets array.
	size += WordAlign(sizeof(WeightSet))		* m_nWeightSets;				// Model::m_WeightSets.
	size += WordAlign(sizeof(ModelSocket*))		* m_nSockets;					// Model::m_Sockets array.
	size += WordAlign(sizeof(ModelSocket))		* m_nSockets;					// Model::m_Sockets.
	size += WordAlign(sizeof(LODDistance))		* (m_nLODs - 1);				// Model::m_LODDistances.
	size += WordAlign(sizeof(NewVertexWeight))	* m_nVertexWeights;				// Model::m_VertexWeights.
	size += WordAlign(sizeof(LTMatrix))			* m_nNodes;						// Model::m_Transforms.
	size += WordAlign(sizeof(ModelNode*))		* m_nNodes;						// Model::m_FlatNodeList.
	size += WordAlign(sizeof(ChildInfo))		* (m_nChildModels - 1);			// Model::m_ChildModels.
	size += WordAlign(sizeof(ModelAnim))		* m_nParentAnims;				// Model::m_Anims.

	return LTTRUE;
}
