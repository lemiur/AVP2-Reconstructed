// Talon's out-of-line ILTCSBase methods (SDK iltcsbase.h): the obsolete HMESSAGE helpers
// forward to ILTMessage, and the math helpers to the embedded ILTMath. The file name is a
// guess (the object sits between counter.cpp and cutil.cpp).
// FLAGS: /O2 /GX-
#include <stdarg.h>
#include "bdefs.h"
#include "iltcsbase.h"
#include "iltmessage.h"

// The helpers that can't return an LTRESULT just print the error.
#define CHECK_PARAMS_NORETURN(fnName) \
	GENERATE_ERROR(2, fnName, LT_INVALIDPARAMS, "")

// FUNCTION: LITHTECH 0x00423a20
LTRESULT ILTCSBase::WriteToMessageFloat(HMESSAGEWRITE hMessage, float val)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageFloat);
	return hMessage->WriteFloat(val);
}

// FUNCTION: LITHTECH 0x00423a70
LTRESULT ILTCSBase::WriteToMessageByte(HMESSAGEWRITE hMessage, uint8 val)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageByte);
	return hMessage->WriteByte(val);
}

// FUNCTION: LITHTECH 0x00423ac0
LTRESULT ILTCSBase::WriteToMessageWord(HMESSAGEWRITE hMessage, uint16 val)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageWord);
	return hMessage->WriteWord(val);
}

// FUNCTION: LITHTECH 0x00423b10
LTRESULT ILTCSBase::WriteToMessageDWord(HMESSAGEWRITE hMessage, uint32 val)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageDWord);
	return hMessage->WriteDWord(val);
}

// FUNCTION: LITHTECH 0x00423b60
LTRESULT ILTCSBase::WriteToMessageString(HMESSAGEWRITE hMessage, char *pStr)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageString);
	return hMessage->WriteString(pStr);
}

// FUNCTION: LITHTECH 0x00423bb0
LTRESULT ILTCSBase::WriteToMessageVector(HMESSAGEWRITE hMessage, LTVector *pVal)
{
	if(hMessage && pVal)
		return hMessage->WriteVector(*pVal);

	RETURN_ERROR(2, ILTCSBase::WriteToMessageVector, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00423c10
LTRESULT ILTCSBase::WriteToMessageCompVector(HMESSAGEWRITE hMessage, LTVector *pVal)
{
	if(hMessage && pVal)
		return hMessage->WriteCompVector(*pVal);

	RETURN_ERROR(2, ILTCSBase::WriteToMessageCompVector, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00423c70
LTRESULT ILTCSBase::WriteToMessageCompPosition(HMESSAGEWRITE hMessage, LTVector *pVal)
{
	if(hMessage && pVal)
		return hMessage->WriteCompPos(*pVal);

	RETURN_ERROR(2, ILTCSBase::WriteToMessageCompPosition, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00423cd0
LTRESULT ILTCSBase::WriteToMessageRotation(HMESSAGEWRITE hMessage, LTRotation *pVal)
{
	if(hMessage && pVal)
		return hMessage->WriteRotation(*pVal);

	RETURN_ERROR(2, ILTCSBase::WriteToMessageRotation, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00423d30
LTRESULT ILTCSBase::WriteToMessageHString(HMESSAGEWRITE hMessage, HSTRING hString)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageHString);
	return hMessage->WriteHString(hString);
}

// FUNCTION: LITHTECH 0x00423d80
LTRESULT ILTCSBase::WriteToMessageHMessageWrite(HMESSAGEWRITE hMessage, HMESSAGEWRITE hDataMessage)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageHMessageWrite);
	return hMessage->WriteMessage(*hDataMessage);
}

// FUNCTION: LITHTECH 0x00423dd0
LTRESULT ILTCSBase::WriteToMessageHMessageRead(HMESSAGEWRITE hMessage, HMESSAGEREAD hDataMessage)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageHMessageRead);
	return hMessage->WriteMessage(*hDataMessage);
}

// FUNCTION: LITHTECH 0x00423e20
LTRESULT ILTCSBase::WriteToMessageFormattedHString(HMESSAGEWRITE hMessage, int messageCode, ...)
{
	va_list marker;

	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageFormattedHString);

	va_start(marker, messageCode);
	hMessage->WriteHStringArgList(messageCode, &marker);
	va_end(marker);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00423e80
LTRESULT ILTCSBase::WriteToMessageObject(HMESSAGEWRITE hMessage, HOBJECT hObj)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageObject);
	return hMessage->WriteObject(hObj);
}

// FUNCTION: LITHTECH 0x00423ee0
float ILTCSBase::ReadFromMessageFloat(HMESSAGEREAD hMessage)
{
	float val;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageFloat);
		return 0.0f;
	}

	hMessage->ReadFloatFL(val);
	return val;
}

// FUNCTION: LITHTECH 0x00423f40
uint8 ILTCSBase::ReadFromMessageByte(HMESSAGEREAD hMessage)
{
	uint8 val;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageByte);
		return 0;
	}

	hMessage->ReadByteFL(val);
	return val;
}

// FUNCTION: LITHTECH 0x00423f90
uint16 ILTCSBase::ReadFromMessageWord(HMESSAGEREAD hMessage)
{
	uint16 val;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageWord);
		return 0;
	}

	hMessage->ReadWordFL(val);
	return val;
}

// FUNCTION: LITHTECH 0x00423ff0
uint32 ILTCSBase::ReadFromMessageDWord(HMESSAGEREAD hMessage)
{
	uint32 val;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageDWord);
		return 0;
	}

	hMessage->ReadDWordFL(val);
	return val;
}

// FUNCTION: LITHTECH 0x00424040
void ILTCSBase::ReadFromMessageVector(HMESSAGEREAD hMessage, LTVector *pVal)
{
	if(hMessage && pVal)
	{
		hMessage->ReadVectorFL(*pVal);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageVector);
}

// FUNCTION: LITHTECH 0x00424090
void ILTCSBase::ReadFromMessageCompVector(HMESSAGEREAD hMessage, LTVector *pVal)
{
	if(hMessage && pVal)
	{
		hMessage->ReadCompVectorFL(*pVal);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageCompVector);
}

// FUNCTION: LITHTECH 0x004240e0
void ILTCSBase::ReadFromMessageCompPosition(HMESSAGEREAD hMessage, LTVector *pVal)
{
	if(hMessage && pVal)
	{
		hMessage->ReadCompPosFL(*pVal);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageCompPosition);
}

// FUNCTION: LITHTECH 0x00424130
void ILTCSBase::ReadFromMessageRotation(HMESSAGEREAD hMessage, LTRotation *pVal)
{
	if(hMessage && pVal)
	{
		hMessage->ReadRotationFL(*pVal);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageRotation);
}

// FUNCTION: LITHTECH 0x00424180
HOBJECT ILTCSBase::ReadFromMessageObject(HMESSAGEREAD hMessage)
{
	HOBJECT hObj;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageObject);
		return LTNULL;
	}

	hMessage->ReadObjectFL(hObj);
	return hObj;
}

// FUNCTION: LITHTECH 0x004241d0
HSTRING ILTCSBase::ReadFromMessageHString(HMESSAGEREAD hMessage)
{
	HSTRING hString;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageHString);
		return LTNULL;
	}

	hMessage->ReadHStringFL(hString);
	return hString;
}

// FUNCTION: LITHTECH 0x00424220
LTRESULT ILTCSBase::ReadFromLoadSaveMessageObject(HMESSAGEREAD hMessage, HOBJECT *hObject)
{
	if(hMessage && hObject)
		return hMessage->ReadObjectFL(*hObject);

	RETURN_ERROR(2, ILTCSBase::ReadFromLoadSaveMessageObject, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00424280
HMESSAGEREAD ILTCSBase::ReadFromMessageHMessageRead(HMESSAGEREAD hMessage)
{
	ILTMessage *pMsg;

	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ReadFromMessageHMessageRead);
		return LTNULL;
	}

	pMsg = LTNULL;
	hMessage->ReadMessageFL(pMsg);
	return pMsg;
}

// FUNCTION: LITHTECH 0x004242e0
void ILTCSBase::EndHMessageRead(HMESSAGEREAD hMessage)
{
	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::EndHMessageRead);
		return;
	}

	hMessage->Release();
}

// FUNCTION: LITHTECH 0x00424330
void ILTCSBase::EndHMessageWrite(HMESSAGEWRITE hMessage)
{
	if(!hMessage)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::EndHMessageWrite);
		return;
	}

	hMessage->Release();
}

// FUNCTION: LITHTECH 0x00424380
void ILTCSBase::ResetRead(HMESSAGEREAD hRead)
{
	if(!hRead)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::ResetRead);
		return;
	}

	hRead->ResetPos();
}

// FUNCTION: LITHTECH 0x004243d0
LTRESULT ILTCSBase::GetRotationVectors(LTRotation *pRotation, LTVector *pUp, LTVector *pRight, LTVector *pForward)
{
	if(pRotation && pUp && pRight && pForward)
		return GetMathLT()->GetRotationVectors(*pRotation, *pRight, *pUp, *pForward);

	RETURN_ERROR(2, ILTCSBase::GetRotationVectors, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00424440
void ILTCSBase::GetRotationVectorsFromMatrix(LTMatrix *pMat, LTVector *pUp, LTVector *pRight, LTVector *pForward)
{
	if(pMat && pUp && pRight && pForward)
	{
		GetMathLT()->GetRotationVectorsFromMatrix(*pMat, *pRight, *pUp, *pForward);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::GetRotationVectorsFromMatrix);
}

// FUNCTION: LITHTECH 0x004244b0
void ILTCSBase::RotateAroundAxis(LTRotation *pRotation, LTVector *pAxis, float amount)
{
	if(pRotation && pAxis)
	{
		GetMathLT()->RotateAroundAxis(*pRotation, *pAxis, amount);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::RotateAroundAxis);
}

// FUNCTION: LITHTECH 0x00424510
void ILTCSBase::EulerRotateX(LTRotation *pRotation, float amount)
{
	if(!pRotation)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::EulerRotateX);
		return;
	}

	GetMathLT()->EulerRotateX(*pRotation, amount);
}

// FUNCTION: LITHTECH 0x00424560
void ILTCSBase::EulerRotateY(LTRotation *pRotation, float amount)
{
	if(!pRotation)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::EulerRotateY);
		return;
	}

	GetMathLT()->EulerRotateY(*pRotation, amount);
}

// FUNCTION: LITHTECH 0x004245b0
void ILTCSBase::EulerRotateZ(LTRotation *pRotation, float amount)
{
	if(!pRotation)
	{
		CHECK_PARAMS_NORETURN(ILTCSBase::EulerRotateZ);
		return;
	}

	GetMathLT()->EulerRotateZ(*pRotation, amount);
}

// FUNCTION: LITHTECH 0x00424600
void ILTCSBase::AlignRotation(LTRotation *pRotation, LTVector *pVector, LTVector *pUp)
{
	if(pRotation && pVector)
	{
		GetMathLT()->AlignRotation(*pRotation, *pVector, *pUp);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::AlignRotation);
}

// FUNCTION: LITHTECH 0x00424660
LTRESULT ILTCSBase::SetupEuler(LTRotation *pRotation, float pitch, float yaw, float roll)
{
	CHECK_PARAMS(pRotation, ILTCSBase::SetupEuler);
	return GetMathLT()->SetupEuler(*pRotation, pitch, yaw, roll);
}

// FUNCTION: LITHTECH 0x004246c0
LTRESULT ILTCSBase::InterpolateRotation(LTRotation *pDest, LTRotation *pRot1, LTRotation *pRot2, float t)
{
	if(pDest && pRot1 && pRot2)
		return GetMathLT()->InterpolateRotation(*pDest, *pRot1, *pRot2, t);

	RETURN_ERROR(2, ILTCSBase::InterpolateRotation, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00424730
void ILTCSBase::SetupTransformationMatrix(LTMatrix *pMat, LTVector *pTranslation, LTRotation *pRotation)
{
	if(pMat && pTranslation && pRotation)
	{
		GetMathLT()->SetupTransformationMatrix(*pMat, *pTranslation, *pRotation);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::SetupTransformationMatrix);
}

// FUNCTION: LITHTECH 0x00424790
void ILTCSBase::SetupTranslationMatrix(LTMatrix *pMat, LTVector *pTranslation)
{
	if(pMat && pTranslation)
	{
		GetMathLT()->SetupTranslationMatrix(*pMat, *pTranslation);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::SetupTranslationMatrix);
}

// FUNCTION: LITHTECH 0x004247f0
void ILTCSBase::SetupRotationMatrix(LTMatrix *pMat, LTRotation *pRot)
{
	if(pMat && pRot)
	{
		GetMathLT()->SetupRotationMatrix(*pMat, *pRot);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::SetupRotationMatrix);
}

// FUNCTION: LITHTECH 0x00424850
void ILTCSBase::SetupTranslationFromMatrix(LTVector *pTranslation, LTMatrix *pMat)
{
	if(pTranslation && pMat)
	{
		GetMathLT()->SetupTranslationFromMatrix(*pTranslation, *pMat);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::SetupTranslationFromMatrix);
}

// FUNCTION: LITHTECH 0x004248b0
void ILTCSBase::SetupRotationFromMatrix(LTRotation *pRot, LTMatrix *pMat)
{
	if(pRot && pMat)
	{
		GetMathLT()->SetupRotationFromMatrix(*pRot, *pMat);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::SetupRotationFromMatrix);
}

// FUNCTION: LITHTECH 0x00424910
void ILTCSBase::SetupRotationAroundPoint(LTMatrix *pMat, LTRotation *pRot, LTVector *pPoint)
{
	if(pMat && pRot && pPoint)
	{
		GetMathLT()->SetupRotationAroundPoint(*pMat, *pRot, *pPoint);
		return;
	}

	CHECK_PARAMS_NORETURN(ILTCSBase::SetupRotationAroundPoint);
}

// Talon reuses WriteToMessageObject's name in the error report, so both bodies are identical.
// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x00423e80 (ILTCSBase::WriteToMessageObject).
LTRESULT ILTCSBase::WriteToLoadSaveMessageObject(HMESSAGEWRITE hMessage, HOBJECT hObj)
{
	CHECK_PARAMS(hMessage, ILTCSBase::WriteToMessageObject);
	return hMessage->WriteObject(hObj);
}

