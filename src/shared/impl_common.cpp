// Jupiter runtime/shared/src/impl_common.cpp, Talon version.
// Talon's impl_common also holds the light anim info helpers (shared by ClientLightAnimLT and
// ServerLightAnimLT) and the compressed vector/position/rotation helpers behind LMessageImpl.
#include <windows.h>		// before the StdLith headers (clientmgr.h brings in lthread.h)
#include "bdefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "impl_common.h"
#include "iltmessage.h"
#include "de_objects.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "de_file.h"
#include "clientmgr.h"
#include "packet.h"
#include "model.h"
#include "geomroutines.h"
#include "stringmgr.h"
#include "de_memory.h"
#include "../../build/proj/LT2/lithshared/stdlith/helpers.h"

void quat_ConvertToMatrix(const float *pQuat, float mat[4][4]);		// 0x0044cc80
void quat_ConvertFromMatrix(float *pQuat, const float mat[4][4]);	// 0x0044ce20

#define SIGN(x)		((x)<0 ? -1 : 1)

#define HObjToLTObj(hObj)	((LTObject*)(hObj))

struct IC_FileEntry
{
	FileEntry	m_Entry;
	char		m_StringData[1];
};

class PIWStruct
{
public:
	LTVector	m_Point;
	LTBOOL		m_bInside;
};


// ------------------------------------------------------------------ //
// Light anims.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0043d5d0
void la_GetInfo(LightAnim *pAnim, LAInfo *pInfo)
{
	pInfo->m_bShadowMap = pAnim->m_bShadowMap;
	pInfo->m_iFrames[0] = pAnim->m_iFrames[0];
	pInfo->m_iFrames[1] = pAnim->m_iFrames[1];
	pInfo->m_fPercentBetween = (float)pAnim->m_PercentBetween / 255.0f;
	pInfo->m_fBlendPercent = pAnim->m_fBlendPercent;
	pInfo->m_vLightPos = pAnim->m_vLightPos;
	pInfo->m_vLightColor = pAnim->m_vLightColor;
	pInfo->m_fLightRadius = pAnim->m_fLightRadius;
}

// FUNCTION: LITHTECH 0x0043d650
void la_SetInfo(LightAnim *pAnim, LAInfo *pInfo)
{
	pAnim->m_iFrames[0] = pInfo->m_iFrames[0];
	pAnim->m_iFrames[1] = pInfo->m_iFrames[1];
	pAnim->m_PercentBetween = (uint32)(pInfo->m_fPercentBetween * 255.0f);
	pAnim->m_fBlendPercent = pInfo->m_fBlendPercent;
	pAnim->m_vLightPos = pInfo->m_vLightPos;
	pAnim->m_vLightColor = pInfo->m_vLightColor;
	pAnim->m_fLightRadius = pInfo->m_fLightRadius;
}

// FUNCTION: LITHTECH 0x0043d6c0
LTBOOL la_VectorChanged(LTVector *pVec1, LTVector *pVec2, double scale)
{
	if((int)(pVec1->x * scale) == (int)(pVec2->x * scale) &&
		(int)(pVec1->y * scale) == (int)(pVec2->y * scale) &&
		(int)(pVec1->z * scale) == (int)(pVec2->z * scale))
	{
		return LTFALSE;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0043d730
LTBOOL la_InfoChanged(LightAnim *pAnim, LAInfo *pInfo, uint32 *pChanged)
{
	*pChanged = 0;

	if(pAnim->m_iFrames[0] != pInfo->m_iFrames[0] || pAnim->m_iFrames[1] != pInfo->m_iFrames[1])
		*pChanged = 1;

	if((pAnim->m_PercentBetween >> 3) != (uint32)(pInfo->m_fPercentBetween * 32.0f))
		*pChanged |= 2;

	if((int)(pAnim->m_fBlendPercent * 32.0f) != (int)(pInfo->m_fBlendPercent * 32.0f))
		*pChanged |= 4;

	if(pAnim->m_bShadowMap)
	{
		if(la_VectorChanged(&pAnim->m_vLightPos, &pInfo->m_vLightPos, 10.0))
			*pChanged |= 8;

		if(la_VectorChanged(&pAnim->m_vLightColor, &pInfo->m_vLightColor, 255.0))
			*pChanged |= 16;

		if((int)pAnim->m_fLightRadius != (int)pInfo->m_fLightRadius)
			*pChanged |= 32;
	}

	return *pChanged != 0;
}


// ------------------------------------------------------------------ //
// Helpers.
// ------------------------------------------------------------------ //

// Walks a sphere down the BSP. Returns 2 if it straddles a plane, otherwise whether the
// node it ends in is inside (and leaves *ppNode on it).
// FUNCTION: LITHTECH 0x0043d820
int ci_IsSphereInsideBSP(Node **ppNode, LTVector *pCenter, float radius)
{
	float dist;

	while(!((*ppNode)->m_Flags & (NF_IN|NF_OUT)))
	{
		dist = (*ppNode)->GetPlane()->DistTo(*pCenter);

		if(dist > radius)
			*ppNode = (*ppNode)->m_Sides[1];
		else if(dist < -radius)
			*ppNode = (*ppNode)->m_Sides[0];
		else
			return 2;
	}

	return (*ppNode)->m_Flags & NF_IN;
}

// FUNCTION: LITHTECH 0x0043d8d0
LTBOOL ci_IsPointInsideBSP(Node *pRoot, LTVector &P)
{
	for(;;)
	{
		if(pRoot->m_Flags & NF_OUT)
			return LTFALSE;

		if(pRoot->m_Flags & NF_IN)
			return LTTRUE;

		LTPlane *pPlane = pRoot->GetPlane();
		float d = pPlane->DistTo(P);

		pRoot = pRoot->m_Sides[d > -0.001f];
	}
}

// Callback function for ic_IsPointInWorld.
// FUNCTION: LITHTECH 0x0043d970
void ic_PointInsideWorldCB(WorldTreeObj *pObj, void *pUser)
{
	LTObject *pObject;
	PIWStruct *pStruct;
	LTVector vTransformedPoint;
	WorldModelInstance *pWM;

	if(pObj->GetObjType() != WTObj_DObject)
		return;

	pObject = (LTObject*)pObj;
	if(pObject->IsMainWorldModel())
	{
		pStruct = (PIWStruct*)pUser;
		pWM = (WorldModelInstance*)pObject;

		MatVMul_H(&vTransformedPoint, &pWM->m_BackTransform, &pStruct->m_Point);

		if(!ci_IsPointInsideBSP(pWM->m_pOriginalBsp->GetRootNode(), vTransformedPoint))
		{
			pStruct->m_bInside = LTFALSE;
		}
	}
}

// FUNCTION: LITHTECH 0x0043da70
LTBOOL ic_IsPointInWorld(WorldTree *pWorldTree, LTVector *pPoint)
{
	PIWStruct theStruct;

	theStruct.m_Point = *pPoint;
	theStruct.m_bInside = LTTRUE;

	pWorldTree->FindObjectsOnPoint(pPoint, ic_PointInsideWorldCB, &theStruct, 0);
	return theStruct.m_bInside;
}


// ------------------------------------------------------------------ //
// Interface implementation functions.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0043dac0
uint32 ic_EndCounter(LTCounter *pCounter)
{
	return 0;
}

// FUNCTION: LITHTECH 0x0043dad0
LTBOOL ic_UpperStrcmp(char *pStr1, char *pStr2)
{
	return CHelpers::UpperStrcmp(pStr1, pStr2);
}


// ------------------------------------------------------------------ //
// Compressed message data.
// ------------------------------------------------------------------ //

// Writes a vector in 9 bytes (Jupiter CCompress::EncodeCompressVector):
//
// Range value:		4 bytes
// Vector vals:		3 bytes
//
// Coord A is used as the range and it can be x,y or z.  The Order bits tell which position
// Coord A is.
// FUNCTION: LITHTECH 0x0043dae0
void ic_WriteCompVector(ILTMessage *pMsg, LTVector *pVal)
{
	float fA, fB, fC, fAbsA, fAbsVec;
	uint32 dwB, dwC;
	uint8 order;

	// Pick X as the coord with largest mag...
	fAbsVec = (float)fabs(pVal->x);
	fA = pVal->x;
	fB = pVal->y;
	fC = pVal->z;
	fAbsA = fAbsVec;
	order = 0;

	// Check if y has bigger mag...
	fAbsVec = (float)fabs(pVal->y);
	if(fAbsVec > fAbsA)
	{
		fA = pVal->y;
		fB = pVal->x;
		fAbsA = fAbsVec;
		order = (1 << 6);
	}

	// Check if z has bigger mag...
	fAbsVec = (float)fabs(pVal->z);
	if(fAbsVec > fAbsA)
	{
		fA = pVal->z;
		fB = pVal->x;
		fC = pVal->y;
		fAbsA = fAbsVec;
		order = (1 << 7);
	}

	// Create scaled value for B coord...
	dwB = (uint32)((fabs(fB) / fAbsA) * ((1 << 18) - 1));
	order |= (((dwB >> 16) & 0x03) << 3);
	if(SIGN(fB) != SIGN(fA))
	{
		order |= (1 << 5);
	}

	// Create scaled value for C coord...
	dwC = (uint32)((fabs(fC) / fAbsA) * ((1 << 18) - 1));
	order |= (dwC >> 16) & 0x03;
	if(SIGN(fC) != SIGN(fA))
	{
		order |= (1 << 2);
	}

	pMsg->WriteFloat(fA);
	pMsg->WriteWord((uint16)dwB);
	pMsg->WriteWord((uint16)dwC);
	pMsg->WriteByte(order);
}

// FUNCTION: LITHTECH 0x0043dc60
void ic_EncodeCompPos(CompWorldPos *pPos, LTVector *pVal, MainWorld *pWorld)
{
	uint32 xVal, yVal, zVal;

	xVal = (uint32)((pVal->x - pWorld->m_ExtentsMin.x) * pWorld->m_ExtentsDiffInv.x * 65535.0f);
	yVal = (uint32)((pVal->y - pWorld->m_ExtentsMin.y) * pWorld->m_ExtentsDiffInv.y * 65535.0f);
	zVal = (uint32)((pVal->z - pWorld->m_ExtentsMin.z) * pWorld->m_ExtentsDiffInv.z * 65535.0f);

	pPos->m_Pos[0] = (uint16)xVal;
	pPos->m_Pos[1] = (uint16)yVal;
	pPos->m_Pos[2] = (uint16)zVal;
	pPos->m_Extra = (uint8)((xVal >> 16) | (yVal >> 16) | (zVal >> 16));
}

// FUNCTION: LITHTECH 0x0043dcf0
void ic_WriteCompWorldPos(ILTMessage *pMsg, CompWorldPos *pPos)
{
	pMsg->WriteWord(pPos->m_Pos[0]);
	pMsg->WriteWord(pPos->m_Pos[1]);
	pMsg->WriteWord(pPos->m_Pos[2]);
}

// FUNCTION: LITHTECH 0x0043dd20
void ic_WriteCompPos(ILTMessage *pMsg, LTVector *pPos, MainWorld *pWorld)
{
	CompWorldPos compPos;

	ic_EncodeCompPos(&compPos, pPos, pWorld);
	ic_WriteCompWorldPos(pMsg, &compPos);
}

// FUNCTION: LITHTECH 0x0043dd50
void ic_ReadCompPos(ILTMessage *pMsg, LTVector *pPos, MainWorld *pWorld)
{
	uint32 pos[3];
	LTVector vPercent;

	if(!pWorld)
	{
		if(pPos)
		{
			pPos->x = 0.0f;
			pPos->y = 0.0f;
			pPos->z = 0.0f;
		}
		return;
	}

	pos[0] = pMsg->ReadWord();
	pos[1] = pMsg->ReadWord();
	pos[2] = pMsg->ReadWord();

	vPercent.x = (float)pos[0] / 65535.0f;
	vPercent.y = (float)pos[1] / 65535.0f;
	vPercent.z = (float)pos[2] / 65535.0f;

	pPos->x = (pWorld->m_ExtentsMax.x - pWorld->m_ExtentsMin.x) * vPercent.x + pWorld->m_ExtentsMin.x;
	pPos->y = (pWorld->m_ExtentsMax.y - pWorld->m_ExtentsMin.y) * vPercent.y + pWorld->m_ExtentsMin.y;
	pPos->z = (pWorld->m_ExtentsMax.z - pWorld->m_ExtentsMin.z) * vPercent.z + pWorld->m_ExtentsMin.z;
}


// The closer to 1 this is, the more strict it is about when it compresses
// rotations.. closer to 0, it compresses them more often but objects
// jitter around more too.
#define ROTATION_COMPRESS_LIMIT 0.999f

inline void ic_UncompressSuperRotation(char *bytes, LTVector *up, LTVector *forward)
{
	float t;

	forward->x = (bytes[1] / 63.0f) - 1.0f;
	forward->y = (bytes[0] / 63.0f) - 1.0f;
	forward->z = (float)bytes[2] / 127.0f;

	// Compressed.. figure out the up vector.
	up->Init(forward->x, forward->y+50.0f, forward->z); // Here's our fake up vector.
	t = -forward->Dot(*up) / forward->MagSqr();
	*up += *forward * t;
}

// FUNCTION: LITHTECH 0x0043de60
void ic_EncodeCompRotation(LTRotation *pRot, CompRot *pCompRot)
{
	LTMatrix mat;
	LTBOOL bSuperCompressed;
	LTVector testUp, testForward;
	LTVector realUp;
	float dot;

	quat_ConvertToMatrix((float*)pRot, mat.m);
	realUp.x = mat.m[0][1];
	realUp.y = mat.m[1][1];
	realUp.z = mat.m[2][1];

	pCompRot->m_Bytes[0] = (char)((1.0f + mat.m[1][2]) * 63.0f + 0.5f);
	pCompRot->m_Bytes[1] = (char)((1.0f + mat.m[0][2]) * 63.0f + 0.5f);
	pCompRot->m_Bytes[2] = (char)(mat.m[2][2] * 127.0f + 0.5f);
	pCompRot->m_Bytes[3] = (char)(mat.m[0][1] * 127.0f + 0.5f);
	pCompRot->m_Bytes[4] = (char)(mat.m[1][1] * 127.0f + 0.5f);
	pCompRot->m_Bytes[5] = (char)(mat.m[2][1] * 127.0f + 0.5f);

	// Figure out if we can reduce it to 3 bytes.
	bSuperCompressed = LTFALSE;
	ic_UncompressSuperRotation(pCompRot->m_Bytes, &testUp, &testForward);
	if(VEC_MAGSQR(testUp) > 0.1f)
	{
		VEC_NORM(testUp);

		dot = VEC_DOT(testUp, realUp);
		if(dot > ROTATION_COMPRESS_LIMIT)
		{
			bSuperCompressed = LTTRUE;
		}
		else if(dot < -ROTATION_COMPRESS_LIMIT)
		{
			bSuperCompressed = LTTRUE;
			pCompRot->m_Bytes[1] = pCompRot->m_Bytes[1] == 0 ? -1 : -pCompRot->m_Bytes[1];
		}
	}

	if(bSuperCompressed)
	{
		pCompRot->m_Bytes[0] = pCompRot->m_Bytes[0] == 0 ? -1 : -pCompRot->m_Bytes[0];
	}
}

// FUNCTION: LITHTECH 0x0043e0b0
void ic_WriteCompRot(ILTMessage *pMsg, CompRot *pCompRot)
{
	pMsg->WriteByte(pCompRot->m_Bytes[0]);
	pMsg->WriteByte(pCompRot->m_Bytes[1]);
	pMsg->WriteByte(pCompRot->m_Bytes[2]);

	if(pCompRot->m_Bytes[0] >= 0)
	{
		pMsg->WriteByte(pCompRot->m_Bytes[3]);
		pMsg->WriteByte(pCompRot->m_Bytes[4]);
		pMsg->WriteByte(pCompRot->m_Bytes[5]);
	}
}

// FUNCTION: LITHTECH 0x0043e110
void ic_WriteCompRotation(ILTMessage *pMsg, LTRotation *pRot)
{
	CompRot compRot;

	ic_EncodeCompRotation(pRot, &compRot);
	ic_WriteCompRot(pMsg, &compRot);
}

// Jupiter's CCompress::UncompressRotation. Inlined into ic_ReadCompRotation, one level deeper than the reads:
// that nesting is why its Norm/Cross calls go out of line, and VC6 threads the two bytes[0] tests so the
// super-compressed branch lands after the return.
inline void ic_UncompressRotation(char *bytes, LTRotation *pRot)
{
	LTMatrix mat;
	LTVector right, up, forward;
	LTBOOL bFlip;

	if(bytes[0] < 0)
	{
		bytes[0] = -bytes[0];

		bFlip = LTFALSE;
		if(bytes[1] < 0)
		{
			bytes[1] = -bytes[1];
			bFlip = LTTRUE;
		}

		ic_UncompressSuperRotation(bytes, &up, &forward);

		up.Norm();
		forward.Norm();

		if(bFlip)
		{
			up = -up;
		}
	}
	else
	{
		forward.y = (bytes[0] / 63.0f) - 1.0f;
		forward.x = (bytes[1] / 63.0f) - 1.0f;
		forward.z = (float)bytes[2] / 127.0f;

		up.x = (float)bytes[3] / 127.0f;
		up.y = (float)bytes[4] / 127.0f;
		up.z = (float)bytes[5] / 127.0f;
	}

	// Fixup.
	right = forward.Cross(up);
	forward = up.Cross(right); // This ensures that all 3 are orthogonal.
	right.Norm();
	forward.Norm();
	up = right.Cross(forward);

	Mat_SetBasisVectors(&mat, &right, &up, &forward);
	quat_ConvertFromMatrix((float*)pRot, mat.m);
}

// FUNCTION: LITHTECH 0x0043e140
void ic_ReadCompRotation(ILTMessage *pMsg, LTRotation *pRot)
{
	CompRot compRot;

	compRot.m_Bytes[0] = (char)pMsg->ReadByte();
	compRot.m_Bytes[1] = (char)pMsg->ReadByte();
	compRot.m_Bytes[2] = (char)pMsg->ReadByte();

	if(compRot.m_Bytes[0] >= 0)
	{
		compRot.m_Bytes[3] = (char)pMsg->ReadByte();
		compRot.m_Bytes[4] = (char)pMsg->ReadByte();
		compRot.m_Bytes[5] = (char)pMsg->ReadByte();
	}

	ic_UncompressRotation(compRot.m_Bytes, pRot);
}

// FUNCTION: LITHTECH 0x0043e540 ?SetBasisVectors@LTMatrix@@QAEXPAV?$_CVector@M@@00@Z

// LTMatrix::GetBasisVectors (SDK inline) is the code-free cost the inline budget needs: seven of its nine
// stores are dead and vanish, but its size still counts, which keeps CMoArray::Insert2 out of line inside the
// inlined WriteType. The char local gives the original's early conversion.
// FUNCTION: LITHTECH 0x0043e5b0
void ic_WriteYRotation(CPacket *pPacket, LTRotation *pRot)
{
	LTMatrix mat;
	LTVector right, up, forward;
	float fAngle;
	char angle;

	quat_ConvertToMatrix((float*)pRot, mat.m);
	mat.GetBasisVectors(&right, &up, &forward);
	fAngle = (float)atan2(forward.x, forward.z);
	angle = (char)(fAngle * (127.0f / MATH_PI));
	pPacket->WriteType(angle);
}

// FUNCTION: LITHTECH 0x0043e690
void ic_ReadYRotation(CPacket *pPacket, LTRotation *pRot)
{
	char angle;

	angle = pPacket->ReadType((char*)LTNULL);
	gr_EulerToRotation(0.0f, ((float)angle / 127.0f) * MATH_PI, 0.0f, pRot);
}


// ------------------------------------------------------------------ //
// Model and misc interface functions.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0043e740
LTRESULT ic_GetNextModelNode(HOBJECT hObject, HMODELNODE hNode, HMODELNODE *pNext)
{
	LTObject *pObj;
	Model *pModel;
	uint32 index;

	pObj = HObjToLTObj(hObject);
	if(!pObj || pObj->m_ObjectType != OT_MODEL)
		RETURN_ERROR(1, GetNextModelNode, LT_INVALIDPARAMS);

	pModel = ((ModelInstance*)pObj)->GetModelDB();

	if(hNode == INVALID_MODEL_NODE)
		index = (uint32)-1;
	else
		index = hNode;

	if((index+1) >= pModel->NumNodes())
	{
		return LT_FINISHED;
	}
	else
	{
		*pNext = index + 1;
		return LT_OK;
	}
}

// FUNCTION: LITHTECH 0x0043e7c0
// The node-range test returns its own RETURN_ERROR (cross-jumped into the shared one); nesting the strncpy in an
// `if(hNode < NumNodes())` block allocates the registers differently.
LTRESULT ic_GetModelNodeName(HOBJECT hObject, HMODELNODE hNode, char *pName, uint32 maxLen)
{
	LTObject *pObj;
	Model *pModel;

	pObj = HObjToLTObj(hObject);
	if(hNode && maxLen && pObj && pObj->m_ObjectType == OT_MODEL)
	{
		pModel = ((ModelInstance*)pObj)->GetModelDB();
		if(hNode >= pModel->NumNodes())
			RETURN_ERROR(1, GetNextModelNode, LT_INVALIDPARAMS);
		strncpy(pName, pModel->m_FlatNodeList[hNode]->m_pName, maxLen-1);
		return LT_OK;
	}

	RETURN_ERROR(1, GetNextModelNode, LT_INVALIDPARAMS);
}

// ----------------------------------------------------------------------- //
// Looks thru the model animations and finds one with the given number.
// Returns (uint32)-1 if it can't find it or if it's a bad object.
// ----------------------------------------------------------------------- //
// FUNCTION: LITHTECH 0x0043e850
HMODELANIM ic_GetAnimIndex(HOBJECT hObj, char *pAnimName)
{
	LTObject *pObj;
	uint32 index;

	pObj = HObjToLTObj(hObj);
	if(pObj && pObj->m_ObjectType == OT_MODEL)
	{
		if(((ModelInstance*)pObj)->GetModelDB()->FindAnim(pAnimName, &index))
			return index;
	}

	return (HMODELANIM)-1;
}

// FUNCTION: LITHTECH 0x0043e890
const char* ic_GetAnimName(HOBJECT hObject, HMODELANIM hAnim)
{
	LTObject *pObj;
	Model *pModel;

	pObj = HObjToLTObj(hObject);
	if(pObj && pObj->m_ObjectType == OT_MODEL)
	{
		pModel = ((ModelInstance*)pObj)->GetModelDB();
		if(hAnim < pModel->NumAnims())
			return pModel->GetAnim(hAnim)->m_pName;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x0043e8d0
void ic_FreeString(HSTRING hString)
{
	if(hString)
		str_FreeString(hString);
}

// FUNCTION: LITHTECH 0x0043eaa0
static LTBOOL ic_FindFileInList(IC_FileEntry *pList, char *pName)
{
	IC_FileEntry *pCur;

	pCur = pList;
	while(pCur)
	{
		if(stricmp(pCur->m_Entry.m_pBaseFilename, pName) == 0)
			return LTTRUE;

		pCur = (IC_FileEntry*)pCur->m_Entry.m_pNext;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0043e8f0
FileEntry* ic_GetFileList(HLTFileTree **trees, int nTrees, const char *pDirName)
{
	IC_FileEntry *pEntry, *pList;
	LTFindInfo findInfo;
	unsigned long allocSize;
	char fullName[256];
	int i;

	pList = LTNULL;

	// Go thru the file trees, adding unique files (earlier ones override later ones).
	for(i=0; i < nTrees; i++)
	{
		findInfo.m_pInternal = LTNULL;
		while(df_FindNext(trees[i], pDirName, &findInfo))
		{
			if(!ic_FindFileInList(pList, findInfo.m_Name))
			{
				if(pDirName[0] == 0)
					strcpy(fullName, findInfo.m_Name);
				else if(strstr(pDirName, "/"))
					sprintf(fullName, "%s/%s", pDirName, findInfo.m_Name);
				else
					sprintf(fullName, "%s\\%s", pDirName, findInfo.m_Name);

				allocSize = (sizeof(IC_FileEntry)-1) + (strlen(findInfo.m_Name)+1) + (strlen(fullName)+1);
				pEntry = (IC_FileEntry*)dalloc(allocSize);

				pEntry->m_Entry.m_pBaseFilename = pEntry->m_StringData;
				pEntry->m_Entry.m_pFullFilename = pEntry->m_StringData + strlen(findInfo.m_Name) + 1;

				memcpy(pEntry->m_Entry.m_pBaseFilename, findInfo.m_Name, strlen(findInfo.m_Name)+1);
				memcpy(pEntry->m_Entry.m_pFullFilename, fullName, strlen(fullName)+1);

				pEntry->m_Entry.m_Type = (findInfo.m_Type == DIRECTORY_TYPE) ? TYPE_DIRECTORY : TYPE_FILE;

				pEntry->m_Entry.m_pNext = (FileEntry*)pList;
				pList = pEntry;
			}
		}
	}

	return (FileEntry*)pList;
}

// FUNCTION: LITHTECH 0x0043eae0
void ic_FreeFileList(FileEntry *pList)
{
	FileEntry *pCur, *pNext;

	pCur = pList;
	while(pCur)
	{
		pNext = pCur->m_pNext;
		dfree(pCur);
		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x0043eb00
float ic_Random(float min, float max)
{
	float randNum = (float)rand() / RAND_MAX;

	return min + (max - min) * randNum;
}

// Counters are compiled out of this build.
// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x00473ac0 (the empty void functions).
void ic_StartCounter(LTCounter *pCounter)
{
}

LTBOOL cp_Parse(char *pCommand, const char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs);	// 0x004202e0

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x0040c3d0 (ci_Parse).
int ic_Parse(char *pCommand, char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs)
{
	return cp_Parse(pCommand, (const char**)pNewCommandPos, argBuffer, argPointers, nArgs);
}

// FUNCTION: LITHTECH 0x0043eb30 ?Cross@?$_CVector@M@@QBE?AV1@V1@@Z
