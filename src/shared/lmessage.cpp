// Talon's ILTMessage implementation, embedded in every CPacket (packet.h; vtable 0x004c7d50).
// Jupiter replaced it with CLTMessage_Read/Write (shared/src/ltmessage.cpp). Object
// references, string resources and the world for compressed positions come from the
// engine's LMessageHelper (m_Unknown04).
// FLAGS: /O2 /GX-
#include <string.h>
#include "bdefs.h"
#include "packet.h"
#include "lmessage.h"
#include "impl_common.h"
#include "stringmgr.h"

#define GetHelper()		((LMessageHelper*)m_Unknown04)

// Bytes left to read. The original computes this in an inline helper (the operand loads come
// out in the other order, m_Pos first, when the subtraction is written in the macro); the real
// name is unknown (packet.h is frozen, so it lives here).
inline int32 GetBytesLeftToRead(CPacket *pPacket)
{
	return pPacket->m_DataLen - pPacket->m_Pos;
}

// Reading past the end of a message resets it so it can be read again.
#define CHECK_AUTORESET(pPacket) \
	if(GetBytesLeftToRead(pPacket) <= 0 && ((pPacket)->m_ErrorFlags & 1)) \
		(pPacket)->m_Pos = 1;


// FUNCTION: LITHTECH 0x00445830
LTRESULT LMessageImpl::ReadByteFL(uint8 &val)
{
	return ReadRawFL(&val, sizeof(val));
}

// FUNCTION: LITHTECH 0x00445840
LTRESULT LMessageImpl::ReadWordFL(uint16 &val)
{
	return ReadRawFL(&val, sizeof(val));
}

// FUNCTION: LITHTECH 0x00445850
LTRESULT LMessageImpl::ReadDWordFL(uint32 &val)
{
	return ReadRawFL(&val, sizeof(val));
}

LTRESULT LMessageImpl::ReadFloatFL(float &val)
{
	return ReadRawFL(&val, sizeof(val));
}

// FUNCTION: LITHTECH 0x00445860
LTRESULT LMessageImpl::ReadStringFL(char *pData, uint32 maxBytes)
{
	FN_NAME(LMessageImpl::ReadStringFL);
	uint32 i;
	uint8 theChar;

	CHECK_PARAMS2(pData && maxBytes > 0);

	for(i=0; i < maxBytes; i++)
	{
		ReadByteFL(theChar);
		pData[i] = theChar;
		if(!theChar)
			return LT_OK;
	}

	// Skip the rest of the string.
	pData[maxBytes-1] = 0;
	do
	{
		ReadByteFL(theChar);
	}
	while(theChar);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445900
LTRESULT LMessageImpl::ReadHStringFL(HSTRING &hString)
{
	uint16 len, toRead, toSkip;
	uint8 theByte;
	uint8 buffer[8192];

	hString = LTNULL;

	ReadWordFL(len);
	toSkip = len;
	toRead = len;
	if(len != 0xFFFF)
	{
		if(len == 0)
		{
			hString = str_CreateString((uint8*)"");
		}
		else
		{
			if(len <= sizeof(buffer))
			{
				toSkip = 0;
			}
			else
			{
				toSkip -= sizeof(buffer);
				toRead = sizeof(buffer);
			}

			ReadRawFL(buffer, toRead);
			buffer[toRead-1] = 0;

			while(toSkip)
			{
				ReadByteFL(theByte);
				--toSkip;
			}

			hString = str_CreateString(buffer);
		}
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004459b0
LTRESULT LMessageImpl::ReadHStringAsStringFL(char *pMsg, uint32 msgBufSize)
{
	FN_NAME(ILTMessage::ReadHStringAsStringFL);
	uint32 i, nLen, nToRead, nToSkip;

	if(pMsg && msgBufSize > 0)
	{
		pMsg[0] = 0;
		nLen = ReadWord();
		if(nLen != 0 && nLen != 0xFFFF)
		{
			nToRead = LTMIN(nLen, msgBufSize - 1);

			for(i=0; i < nToRead; i++)
				pMsg[i] = ReadByte();
			pMsg[i] = 0;

			// Skip the rest.
			if(nLen > nToRead)
			{
				for(nToSkip = nLen - nToRead; nToSkip; nToSkip--)
					ReadByte();
			}
		}

		return LT_OK;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}

// FUNCTION: LITHTECH 0x00445a80
LTRESULT LMessageImpl::ReadObjectFL(HOBJECT &hObj)
{
	return GetHelper()->ReadObjectRef(this, &hObj);
}

// FUNCTION: LITHTECH 0x00445aa0
LTRESULT LMessageImpl::ReadVectorFL(LTVector &vec)
{
	vec.x = ReadFloat();
	vec.y = ReadFloat();
	vec.z = ReadFloat();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445af0
LTRESULT LMessageImpl::ReadCompVectorFL(LTVector &vec)
{
	float fA, fB, fC;
	uint32 dwB, dwC;
	uint8 order;

	fA = ReadFloat();
	dwB = ReadWord();
	dwC = ReadWord();
	order = ReadByte();

	// Unscale the B and C coords.
	fB = (float)(uint32)(((order & 0x18) << 13) + dwB) * fA / 262144.0f;
	if(order & (1 << 5))
		fB *= -1.0f;

	fC = (float)(uint32)(((order & 0x03) << 16) + dwC) * fA / 262144.0f;
	if(order & (1 << 2))
		fC *= -1.0f;

	switch(order >> 6)
	{
		case 1:
			vec.x = fB;
			vec.y = fA;
			vec.z = fC;
			break;

		case 2:
			vec.x = fB;
			vec.y = fC;
			vec.z = fA;
			break;

		default:
			vec.x = fA;
			vec.y = fB;
			vec.z = fC;
			break;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445c10
LTRESULT LMessageImpl::ReadCompPosFL(LTVector &vec)
{
	MainWorld *pWorld;

	GetHelper()->GetWorld(&pWorld);
	ic_ReadCompPos(this, &vec, pWorld);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445c40
LTRESULT LMessageImpl::ReadRotationFL(LTRotation &rot)
{
	rot.m_Quat[0] = ReadFloat();
	rot.m_Quat[1] = ReadFloat();
	rot.m_Quat[2] = ReadFloat();
	rot.m_Quat[3] = ReadFloat();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445ca0
LTRESULT LMessageImpl::ReadCompRotationFL(LTRotation &rot)
{
	ic_ReadCompRotation(this, &rot);
	return LT_OK;
}

// The scoped length and byte temporaries reuse the dead argument slot. The member-function call keeps WriteTypeImpl
// out of line, and the uint16 reference preserves the m_Pos store order.
// FUNCTION: LITHTECH 0x00445cc0
LTRESULT LMessageImpl::ReadMessageFL(ILTMessage* &pMsg)
{
	CPacket *pPacket;
	uint32 i;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));
	pPacket->m_Message.m_Unknown04 = m_Unknown04;
	pMsg = &pPacket->m_Message;

	{
		uint16 len;
		ReadWordFL(len);
		if(len == 0xFFFF)
		{
			pMsg = LTNULL;
			pPacket->Release();
			return LT_OK;
		}

		if(len == 0)
			goto FinishReadMessage;
		i = len;
	}
	{
		do
		{
			uint8 theByte;
			ReadByteFL(theByte);
			void (CPacket::*writeByte)(uint8) = &CPacket::WriteTypeImpl;
			(pPacket->*writeByte)(theByte);
		} while(--i);
	}

FinishReadMessage:
	pPacket->AddRef();
	uint16 &readPos = pPacket->m_Pos;
	readPos = 1;
	pPacket->Release();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445d80
LTRESULT LMessageImpl::WriteByte(uint8 val)
{
	return WriteRaw(&val, sizeof(val));
}

// FUNCTION: LITHTECH 0x00445d90
LTRESULT LMessageImpl::WriteWord(uint16 val)
{
	return WriteRaw(&val, sizeof(val));
}

// FUNCTION: LITHTECH 0x00445da0
LTRESULT LMessageImpl::WriteDWord(uint32 val)
{
	return WriteRaw(&val, sizeof(val));
}

LTRESULT LMessageImpl::WriteFloat(float val)
{
	return WriteRaw(&val, sizeof(val));
}

// FUNCTION: LITHTECH 0x00445db0
LTRESULT LMessageImpl::WriteString(char *pData)
{
	if(pData)
	{
		while(*pData)
		{
			WriteByte(*pData);
			++pData;
		}
	}

	WriteByte(0);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445df0
LTRESULT LMessageImpl::WriteObject(HOBJECT hObj)
{
	return GetHelper()->WriteObjectRef(this, hObj);
}

// FUNCTION: LITHTECH 0x00445e10
LTRESULT LMessageImpl::WriteVector(LTVector &vec)
{
	WriteFloat(vec.x);
	WriteFloat(vec.y);
	WriteFloat(vec.z);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445e40
LTRESULT LMessageImpl::WriteCompVector(LTVector &vec)
{
	ic_WriteCompVector(this, &vec);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445e60
LTRESULT LMessageImpl::WriteCompPos(LTVector &vec)
{
	MainWorld *pWorld;

	GetHelper()->GetWorld(&pWorld);
	ic_WriteCompPos(this, &vec, pWorld);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445e90
LTRESULT LMessageImpl::WriteRotation(LTRotation &rot)
{
	WriteFloat(rot.m_Quat[0]);
	WriteFloat(rot.m_Quat[1]);
	WriteFloat(rot.m_Quat[2]);
	WriteFloat(rot.m_Quat[3]);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445ed0
LTRESULT LMessageImpl::WriteCompRotation(LTRotation &rot)
{
	ic_WriteCompRotation(this, &rot);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445ef0
LTRESULT LMessageImpl::WriteMessage(ILTMessage &msg)
{
	FN_NAME(LMessageImpl::WriteMessage);
	LMessageImpl *pMsg;
	int i;

	pMsg = (LMessageImpl*)&msg;
	if(pMsg && pMsg->IsInvalid())
	{
		WriteWord(0xFFFF);
		ERR(1, LT_NOTINITIALIZED);
	}

	WriteWord((uint16)(pMsg->m_pPacket->m_DataLen - 1));
	for(i=0; i < pMsg->m_pPacket->m_DataLen - 1; i++)
	{
		WriteByte(pMsg->m_pPacket->m_Data[i+1]);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445fa0
LTRESULT LMessageImpl::WriteHString(HSTRING hString)
{
	int nBytes;
	uint8 *pBytes;

	if(hString)
	{
		pBytes = str_GetStringBytes(hString, &nBytes);
		WriteWord((uint16)nBytes);
		WriteRaw(pBytes, nBytes);
	}
	else
	{
		WriteWord(0xFFFF);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00445ff0
LTRESULT LMessageImpl::WriteHStringFormatted(int messageCode, ...)
{
	va_list marker;

	va_start(marker, messageCode);
	return WriteHStringArgList(messageCode, &marker);
}

// FUNCTION: LITHTECH 0x00446010
LTRESULT LMessageImpl::WriteHStringArgList(int messageCode, va_list *pList)
{
	return GetHelper()->WriteHStringArgList(this, messageCode, pList);
}

// FUNCTION: LITHTECH 0x00446030
LTRESULT LMessageImpl::WriteStringAsHString(char *pStr)
{
	uint16 len;

	if(pStr)
	{
		len = (uint16)(strlen(pStr) + 1);
		WriteWord(len);
		WriteRaw(pStr, len);
	}
	else
	{
		WriteWord(0xFFFF);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446080
LTRESULT LMessageImpl::Release()
{
	m_pPacket->Release();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446090
LTRESULT LMessageImpl::ReadRawFL(void *pData, uint32 len)
{
	m_pPacket->ReadRaw(pData, (uint16)len);
	CHECK_AUTORESET(m_pPacket);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004460d0
LTRESULT LMessageImpl::WriteRaw(void *pData, uint32 len)
{
	m_pPacket->WriteRaw(pData, (uint16)len);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004460f0
LTRESULT LMessageImpl::ResetPos()
{
	m_pPacket->m_Pos = 1;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446100
LTRESULT LMessageImpl::GetStatus(uint32 &flags)
{
	flags = 0;

	if(m_pPacket->m_ErrorFlags & PACKETERR_READOVERFLOW)
		flags = LMSTAT_READOVERFLOW;

	if(m_pPacket->m_ErrorFlags & PACKETERR_WRITEOVERFLOW)
		flags |= LMSTAT_WRITEOVERFLOW;

	return LT_OK;
}

// No annotation: the linker folded this body (/OPT:ICF) into the identical function at 0x0043dac0 (ic_EndCounter).
LTBOOL LMessageImpl::IsInvalid()
{
	return LTFALSE;
}

