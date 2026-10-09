// Jupiter runtime/sound/src/soundinstance.cpp, Talon version.
// Talon calls Miles directly (bracketed by AIL_lock/AIL_unlock), sets up loop blocks when the sound
// starts rendering, streams from a file handle, supports KillSoundFade and software filters, and
// scales volumes by the per-type volume (ILTClientSoundMgr::SetVolumeByType).
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include "bdefs.h"
#include "servermgr.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "de_objects.h"
#include "soundmgr.h"
#include "soundinstance.h"
#include "soundbuffer.h"
#include "engine_vars.h"




// Inline in the header in Talon; never inlined by the original.
// FUNCTION: LITHTECH 0x0048ff40
CSoundInstance::CSoundInstance()
{
	m_eType = SOUNDTYPE_LOCAL;
	m_pSoundBuffer = LTNULL;
	m_dwPlaySoundFlags = 0;
	m_dwSoundInstanceFlags = SOUNDINSTANCEFLAG_READY;
	m_nPriority = 0;
	m_nVolume = 100;
	m_fPitchShift = 1.0f;
	m_dwOffsetTime = 0;
	m_dwTimer = 0;
	m_dwFadeTime = 0;
	m_dwLastTime = 0;
	m_dwPauseCount = 0;
	m_fFadeVolume = 1.0f;
	m_dwResumeTime = 0;
	m_hSound = (HLTSOUND)INVALID_OBJECTID;
	m_hSample = LTNULL;
	m_h3DSample = LTNULL;
	m_hStream = LTNULL;
	m_hStreamFile = INVALID_HANDLE_VALUE;
	m_nNumCollisions = 0;
	m_fModifiedPriority = 0;
	m_dwListIndex = 0;
	dl_TieOff(&m_BufferLink);
	m_nFilter = -1;
	m_nUserSoundType = 0;
	for (int i = 0; i < SOUNDINSTANCE_MAXFILTERPARAMS; i++)
	{
		m_nFilterParam[i] = -1;
		m_fFilterParamValue[i] = 0.0f;
	}
}

// FUNCTION: LITHTECH 0x0048ffd0 ?GetPosition@CSoundInstance@@UAEKAAV?$_CVector@M@@@Z
// FUNCTION: LITHTECH 0x0048fff0 ?HasFilter@CSoundInstance@@UAEIXZ
// FUNCTION: LITHTECH 0x00490000 ?SetPosition@CSoundInstance@@UAEKABV?$_CVector@M@@I@Z
// FUNCTION: LITHTECH 0x00490020 ??_GCSoundInstance@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x00490040
CSoundInstance::~CSoundInstance()
{
	CSoundInstance::Term();
}

// FUNCTION: LITHTECH 0x00490050
LTRESULT CSoundInstance::Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime)
{
	// Start fresh
	Term();

	m_dwSoundInstanceFlags = SOUNDINSTANCEFLAG_READY;

	m_pSoundBuffer = &soundBuffer;
	m_pFileIdent = (FileIdentifier *)m_pSoundBuffer->GetFileIdent();

	m_dwPlaySoundFlags = playSoundInfo.m_dwFlags;
	m_nPriority = playSoundInfo.m_nPriority;
	m_nUserSoundType = playSoundInfo.m_nUserSoundType;
	if (m_dwPlaySoundFlags & PLAYSOUND_CTRL_VOL)
		m_nVolume = playSoundInfo.m_nVolume;
	if (m_dwPlaySoundFlags & PLAYSOUND_CTRL_PITCH)
		m_fPitchShift = playSoundInfo.m_fPitchShift;
	if (m_dwPlaySoundFlags & PLAYSOUND_CTRL_TYPE)
		m_nUserSoundType = playSoundInfo.m_nUserSoundType;

	// Add in optional random pitch.
	m_fPitchShift *= (1.0f + m_pSoundBuffer->RandomPitchMod());
	if (m_fPitchShift <= 0.0f)
		m_fPitchShift = 1.0f;

	m_dwOffsetTime = dwOffsetTime;
	m_nNumCollisions = 0;
	m_hSample = LTNULL;
	m_h3DSample = LTNULL;
	m_hStream = LTNULL;
	m_fModifiedPriority = (float)m_nPriority + 1.0f;
	m_dwResumeTime = 0;

	// If the sound is client side, then set the sound handle as this object
	if (m_dwPlaySoundFlags & PLAYSOUND_CLIENT)
	{
		m_hSound = (HLTSOUND)INVALID_OBJECTID;
		if (m_dwPlaySoundFlags & PLAYSOUND_GETHANDLE)
			playSoundInfo.m_hSound = (HLTSOUND)this;
		else
			playSoundInfo.m_hSound = LTNULL;
	}
	// If server side sound, then the hsound comes from the playsoundinfo
	else
	{
		m_hSound = (HLTSOUND)playSoundInfo.m_hSound;
		cm_AddToObjectMap(g_pClientMgr, (uint16)playSoundInfo.m_hSound);
		g_pClientMgr->m_ObjectMap[(uint16)playSoundInfo.m_hSound].m_nRecordType = RECORDTYPE_SOUND;
		g_pClientMgr->m_ObjectMap[(uint16)playSoundInfo.m_hSound].m_pRecordData = this;
	}

	// Get instance and force decompress if 3d.
	if (m_pSoundBuffer->AddInstance(*this) != LT_OK)
		return LT_ERROR;

	m_dwListIndex = 0;

	// Set timer.  Adjust for pitch shift.
	m_dwDuration = (uint32)((float)m_pSoundBuffer->GetDuration() / m_fPitchShift);
	SetTimer(m_dwDuration);

	// Check if there is an offset into the sound
	if (m_dwOffsetTime > 0)
	{
		// Check if offset goes past the end of the sound
		if (m_dwOffsetTime > m_dwDuration)
		{
			// Check if it should wrap
			if (m_dwPlaySoundFlags & PLAYSOUND_LOOP)
			{
				SetTimer(m_dwTimer - ((m_dwOffsetTime - m_dwDuration) % m_dwDuration));
			}
			else
			{
				m_dwSoundInstanceFlags &= ~SOUNDINSTANCEFLAG_PLAYING;
				m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_DONE;
			}
		}
		else
		{
			SetTimer(m_dwTimer - m_dwOffsetTime);
		}
	}

	m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_FIRSTUPDATE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490240
void CSoundInstance::Term()
{
	// Make sure we're stopped
	Stop();

	DisconnectFromServer();

	// Kill any open stream.
	if (m_hStream)
	{
		AIL_lock();
		AIL_close_stream(m_hStream);
		AIL_unlock();
		m_hStream = LTNULL;
	}

	if (m_hStreamFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hStreamFile);
		m_hStreamFile = INVALID_HANDLE_VALUE;
	}

	m_dwSoundInstanceFlags = SOUNDINSTANCEFLAG_READY;
	if (m_pSoundBuffer)
	{
		m_pSoundBuffer->RemoveInstance(*this);
		// Unload the buffer if we're only playing it once
		if ((m_dwPlaySoundFlags & PLAYSOUND_ONCE) != 0)
		{
			// Unload if nobody's using it
			if (m_pSoundBuffer->GetInstanceList()->m_nElements == 0)
				m_pSoundBuffer->Unload();
		}
		m_pSoundBuffer = LTNULL;
	}
	dl_TieOff(&m_BufferLink);
	m_pFileIdent = LTNULL;

	m_dwPlaySoundFlags = 0;
	m_nUserSoundType = 0;
	m_nPriority = 0;
	m_nVolume = 100;
	m_fPitchShift = 1.0f;
	m_dwOffsetTime = 0;
	m_nNumCollisions = 0;
	m_dwResumeTime = 0;

	m_hSample = LTNULL;
	m_h3DSample = LTNULL;
	m_hStream = LTNULL;

	m_fModifiedPriority = 0;
	m_dwListIndex = 0;

	m_nFilter = -1;
	for (int i = 0; i < SOUNDINSTANCE_MAXFILTERPARAMS; i++)
	{
		m_nFilterParam[i] = -1;
		m_fFilterParamValue[i] = 0.0f;
	}
}

// FUNCTION: LITHTECH 0x00490310
LTRESULT CSoundInstance::DisconnectFromServer()
{
	// If this is a server sound, then remove it from the id list...
	if ((uint16)m_hSound != INVALID_OBJECTID && !(m_dwPlaySoundFlags & PLAYSOUND_CLIENT))
	{
		cm_ClearObjectMapEntry(g_pClientMgr, (uint16)m_hSound);
		m_hSound = (HLTSOUND)INVALID_OBJECTID;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490340
LTRESULT CSoundInstance::Stop(LTBOOL bForce)
{
	m_dwTimer = 0;
	m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_DONE;
	Silence(bForce);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490360
LTRESULT CSoundInstance::Silence(LTBOOL bForce)
{
	m_dwSoundInstanceFlags &= ~SOUNDINSTANCEFLAG_PLAYING;

	if (m_hSample)
	{
		if (!g_bStopFilteredSamples && HasFilter())
			AIL_set_sample_volume(m_hSample, 0);
		else
			AIL_stop_sample(m_hSample);
		GetClientILTSoundMgrImpl()->LinkSampleSoundInstance(m_hSample, LTNULL);
		GetClientILTSoundMgrImpl()->ReleaseSWSample(m_hSample);
		m_hSample = LTNULL;
	}
	else if (m_h3DSample)
	{
		AIL_lock();
		// Only stop the sound if it's still going
		if (bForce || AIL_3D_sample_status(m_h3DSample) == SMP_PLAYING)
			AIL_stop_3D_sample(m_h3DSample);
		AIL_unlock();

		GetClientILTSoundMgrImpl()->Link3DSampleSoundInstance(m_h3DSample, LTNULL);
		GetClientILTSoundMgrImpl()->Release3DSample(m_h3DSample);
		m_h3DSample = LTNULL;
	}
	else if (m_hStream)
	{
		AIL_lock();
		AIL_close_stream(m_hStream);
		m_hStream = LTNULL;
		AIL_unlock();
	}

	if (m_hStreamFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hStreamFile);
		m_hStreamFile = INVALID_HANDLE_VALUE;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490470
LTRESULT CSoundInstance::Pause()
{
	if (m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING)
		Silence(LTTRUE);

	if (m_dwPauseCount == 0)
	{
		m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_PAUSED;
		m_dwResumeTime = m_dwTimer;
	}
	m_dwPauseCount++;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004904a0
LTRESULT CSoundInstance::Resume()
{
	if (!(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PAUSED) || m_dwPauseCount == 0)
		return LT_ERROR;

	m_dwPauseCount--;
	if (m_dwPauseCount == 0)
	{
		m_dwSoundInstanceFlags &= ~SOUNDINSTANCEFLAG_PAUSED;
		SetTimer(m_dwResumeTime);
		m_dwResumeTime = 0;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004904e0
LTBOOL CSoundInstance::UpdateTimer(uint32 dwFrameTime)
{
	uint32 dwCurTime, dwModLoopPoint[2];
	int nTime;
	float fPitchScale;

	// Don't update the timer if paused
	if (m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PAUSED)
	{
		return LTTRUE;
	}

	// The first update doesn't update the timer because the full frametime wasn't
	// spent playing the sound
	if (m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_FIRSTUPDATE)
	{
		m_dwSoundInstanceFlags &= ~SOUNDINSTANCEFLAG_FIRSTUPDATE;
		return LTTRUE;
	}

	// Check if the sound is playing through channel
	if (m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING)
	{
		// Base frame time on a timer
		AIL_lock();
		dwCurTime = AIL_ms_count();
		AIL_unlock();

		dwFrameTime = dwCurTime - m_dwLastTime;
		m_dwLastTime = dwCurTime;
	}

	if (!m_pSoundBuffer)
	{
		SetTimer(0);
		return LTFALSE;
	}

	// Check if timer has an initial delay.  If the delay just ran out, then
	// start the sound at the beginning, ignoring any time into the sound.  This
	// is done because of the granularity of the framerate could chop the
	// beginning of the sound off
	if (m_dwTimer > m_dwDuration && m_dwTimer - m_dwDuration < dwFrameTime)
		SetTimer(m_dwDuration);
	else
	{
		// If looping and not heading for the end, then make sure the timer stays within the loop period.
		if (GetPlaySoundFlags() & PLAYSOUND_LOOP && !(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_ENDLOOP))
		{
			// Get ascending timer.
			nTime = (int)m_dwDuration - (int)m_dwTimer;
			fPitchScale = 1.0f / m_fPitchShift;
			dwModLoopPoint[0] = (uint32)((float)m_pSoundBuffer->GetLoopPoint(0) * fPitchScale);
			dwModLoopPoint[1] = (uint32)((float)m_pSoundBuffer->GetLoopPoint(1) * fPitchScale);
			if (dwModLoopPoint[1] - nTime <= dwFrameTime)
			{
				nTime = nTime + dwFrameTime - dwModLoopPoint[0];
				nTime %= dwModLoopPoint[1] - dwModLoopPoint[0];
				nTime += dwModLoopPoint[0];
				m_dwTimer = m_dwDuration - nTime;
			}
			else if (m_nNumCollisions != 1)
			{
				m_dwTimer -= dwFrameTime;
			}
		}
		// Check if sound just timed out
		else if (m_dwTimer <= dwFrameTime)
		{
			m_dwTimer = 0;
		}
		// Skip updating the timer if sound just had a collision.  This allows
		// the sound to start on the next frame.  Otherwise update it
		else if (m_nNumCollisions != 1)
		{
			m_dwTimer -= dwFrameTime;
		}
	}

	// Fade out.
	if (m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_FADE)
	{
		m_fFadeVolume -= (float)dwFrameTime / (float)m_dwFadeTime;
		if (m_fFadeVolume <= 0.0f)
		{
			m_fFadeVolume = 0.0f;
			m_dwTimer = 0;
			return LTFALSE;
		}
	}

	// Check if our timer ran out.
	if (m_dwTimer == 0)
	{
		// If it's not looping or ending a loop, then it's done.
		if (!(m_dwPlaySoundFlags & PLAYSOUND_LOOP) || (m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_ENDLOOP))
			return LTFALSE;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00490680
LTRESULT CSoundInstance::AcquireSample()
{
	// Get a sample
	if (!m_hSample)
	{
		m_hSample = GetClientILTSoundMgrImpl()->GetFreeSWSample();
		if (!m_hSample)
			return LT_ERROR;
	}

	AIL_lock();

	AIL_init_sample(m_hSample);
	AIL_set_sample_type(m_hSample, m_pSoundBuffer->GetSampleType(), 0);

	// Check if this sample should not have reverb
	if (!GetClientILTSoundMgrImpl()->UseSWReverb() || !(m_dwPlaySoundFlags & PLAYSOUND_REVERB))
	{
		AIL_set_sample_reverb(m_hSample, 0.0f, 0.0f, 0.0f);
	}

	AIL_set_sample_volume(m_hSample, (m_nVolume * 127) / 100);

	// Compressed sounds are decompressed by Miles as they play.
	if (m_pSoundBuffer->IsCompressed() && !m_pSoundBuffer->GetDecompressedSoundBuffer())
	{
		if (!AIL_set_sample_file(m_hSample, m_pSoundBuffer->GetFileData(), 0))
		{
			GetClientILTSoundMgrImpl()->ReleaseSWSample(m_hSample);
			m_hSample = LTNULL;
			return LT_ERROR;
		}
	}
	else
	{
		AIL_set_sample_address(m_hSample, m_pSoundBuffer->GetSoundData(), m_pSoundBuffer->GetSoundDataLen());
	}

	AIL_set_sample_playback_rate(m_hSample, (S32)((float)m_pSoundBuffer->GetPlaybackRate() * m_fPitchShift + 0.5f));
	AIL_set_sample_pan(m_hSample, 64);

	GetClientILTSoundMgrImpl()->LinkSampleSoundInstance(m_hSample, this);

	AIL_unlock();

	ApplyFilter();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490800
LTRESULT CSoundInstance::Acquire3DSample()
{
	LTVector vPos, vTemp;
	S32 nPlayBackRate;
	float fLevel;

	if (!m_h3DSample)
	{
		m_h3DSample = GetClientILTSoundMgrImpl()->GetFree3DSample(this);
		if (!m_h3DSample)
			return LT_ERROR;
	}

	if (m_pSoundBuffer->GetSoundBufferFlags() & SOUNDBUFFERFLAG_STREAM)
		return LT_OK;

	AIL_lock();

	// If this sample isn't already setup with this buffer, then initialize it.
	if ((CSoundBuffer *)AIL_3D_user_data(m_h3DSample, SAMPLE_BUFFER) != m_pSoundBuffer)
	{
		// 3d samples need the decompressed data.
		if (m_pSoundBuffer->IsCompressed() && !m_pSoundBuffer->GetDecompressedSoundBuffer() &&
			m_pSoundBuffer->DecompressData() != LT_OK)
		{
			AIL_unlock();
			GetClientILTSoundMgrImpl()->Release3DSample(m_h3DSample);
			m_h3DSample = LTNULL;
			return LT_ERROR;
		}

		if (!AIL_set_3D_sample_info(m_h3DSample, m_pSoundBuffer->GetSampleInfo()))
		{
			AIL_unlock();
			GetClientILTSoundMgrImpl()->Release3DSample(m_h3DSample);
			m_h3DSample = LTNULL;
			return LT_ERROR;
		}

		// get the playback rate including any pitch-shifting
		nPlayBackRate = (S32)((float)m_pSoundBuffer->GetPlaybackRate() * m_fPitchShift + 0.5f);
		if (nPlayBackRate != m_pSoundBuffer->GetPlaybackRate())
			AIL_set_3D_sample_playback_rate(m_h3DSample, nPlayBackRate);

		AIL_set_3D_user_data(m_h3DSample, SAMPLE_BUFFER, (S32)m_pSoundBuffer);
	}

	AIL_set_3D_sample_distances(m_h3DSample, m_fOuterRadius, m_fInnerRadius);

	AIL_set_3D_sample_volume(m_h3DSample, (m_nVolume * 127) / 100);

	AIL_3D_position(m_h3DSample, &vTemp.x, &vTemp.y, &vTemp.z);
	Get3DSamplePosition(vPos);
	if (vTemp.DistSqr(vPos) > 0.001f)
		AIL_set_3D_position(m_h3DSample, vPos.x, vPos.y, vPos.z);
	AIL_set_3D_velocity_vector(m_h3DSample, 0.0f, 0.0f, 0.0f);

	// Check if this sample should not have reverb
	if (GetClientILTSoundMgrImpl()->Use3DReverb() && !(m_dwPlaySoundFlags & PLAYSOUND_REVERB))
	{
		fLevel = 0.0f;
		AIL_set_3D_sample_preference(m_h3DSample, "EAX sample reverb mix", &fLevel);
	}

	GetClientILTSoundMgrImpl()->Link3DSampleSoundInstance(m_h3DSample, this);

	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490a40
LTRESULT CSoundInstance::AcquireStream()
{
	char szStreamName[32];
	char szFileName[_MAX_PATH+1];
	uint32 nFilePos, nFileSize;

	if (!m_pSoundBuffer)
		return LT_ERROR;

	if (!m_hStream)
	{
		if (m_hStreamFile != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_hStreamFile);
			m_hStreamFile = INVALID_HANDLE_VALUE;
		}

		// Open the file directly.
		if (!df_GetRawInfo(m_pSoundBuffer->GetFileIdent()->m_hFileTree,
			m_pSoundBuffer->GetFileIdent()->m_Filename, szFileName, _MAX_PATH, &nFilePos, &nFileSize))
			return LT_ERROR;

		m_hStreamFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (m_hStreamFile == INVALID_HANDLE_VALUE)
			return LT_ERROR;

		if (nFilePos)
		{
			if (SetFilePointer(m_hStreamFile, nFilePos, NULL, FILE_BEGIN) == 0xFFFFFFFF)
			{
				CloseHandle(m_hStreamFile);
				m_hStreamFile = INVALID_HANDLE_VALUE;
				return LT_ERROR;
			}
		}

		// Miles opens the stream from the file handle.
		sprintf(szStreamName, "\\\\\\\\%d", m_hStreamFile);

		AIL_lock();
		m_hStream = AIL_open_stream(GetClientILTSoundMgrImpl()->GetDigDriver(), szStreamName, 0);
		AIL_unlock();

		if (!m_hStream)
		{
			CloseHandle(m_hStreamFile);
			m_hStreamFile = INVALID_HANDLE_VALUE;
			return LT_ERROR;
		}
	}

	AIL_lock();
	AIL_set_stream_playback_rate(m_hStream, (S32)((float)m_pSoundBuffer->GetPlaybackRate() * m_fPitchShift + 0.5f));
	AIL_set_stream_pan(m_hStream, 64);
	AIL_set_stream_volume(m_hStream, (m_nVolume * 127) / 100);
	GetClientILTSoundMgrImpl()->LinkStreamSoundInstance(m_hStream, this);
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490c20
LTRESULT CSoundInstance::StartRendering()
{
	const WAVEFORMATEX *pWaveFormat;
	uint32 dwCurPos;
	S32 nPos1, nPos2;
	float fModPoint, fPitchScale;

	if (!m_hSample && !m_h3DSample && !m_hStream)
	{
		return LT_ERROR;
	}

	if (!m_pSoundBuffer)
		return LT_ERROR;

	pWaveFormat = m_pSoundBuffer->GetWaveFormat();
	if (!pWaveFormat)
	{
		return LT_ERROR;
	}

	// Check if we're to loop the sound.
	if (m_dwPlaySoundFlags & PLAYSOUND_LOOP)
	{
		// Check if there's a valid cue point region.
		if (m_pSoundBuffer->GetWaveHeader(LTFALSE).m_CuePoint.m_bValid)
		{
			fPitchScale = 1.0f / m_fPitchShift;
			fModPoint = (float)m_pSoundBuffer->GetLoopPoint(0) * fPitchScale;
			nPos1 = (S32)((fModPoint / (float)m_dwDuration) * (float)m_pSoundBuffer->GetSoundDataLen());
			nPos1 = LTCLAMP(nPos1, (S32)0, (S32)m_pSoundBuffer->GetSoundDataLen());
			fModPoint = (float)m_pSoundBuffer->GetLoopPoint(1) * fPitchScale;
			nPos2 = (S32)((fModPoint / (float)m_dwDuration) * (float)m_pSoundBuffer->GetSoundDataLen());
			nPos2 = LTCLAMP(nPos2, nPos1, (S32)m_pSoundBuffer->GetSoundDataLen());
		}
		// No cue points, so loop the whole thing.
		else
		{
			nPos1 = 0;
			nPos2 = -1;
		}

		AIL_lock();
		if (m_hSample)
		{
			AIL_set_sample_loop_block(m_hSample, nPos1, nPos2);
			AIL_set_sample_loop_count(m_hSample, 0);
		}
		else if (m_h3DSample)
		{
			AIL_set_3D_sample_loop_block(m_h3DSample, nPos1, nPos2);
			AIL_set_3D_sample_loop_count(m_h3DSample, 0);
		}
		else if (m_hStream)
		{
			AIL_set_stream_loop_count(m_hStream, 0);
		}
		AIL_unlock();
	}
	// Keep a muted filtered sample looping on its tail so its filter state survives.
	else if (m_hSample && !g_bStopFilteredSamples && HasFilter())
	{
		AIL_set_sample_loop_block(m_hSample, m_pSoundBuffer->GetSoundDataLen() - 2, m_pSoundBuffer->GetSoundDataLen() - 1);
		AIL_set_sample_loop_count(m_hSample, m_pSoundBuffer->GetSoundDataLen());
	}

	m_nNumCollisions = 0;

	// Handle streaming sounds.
	if (m_hStream)
	{
		// If the timer is at the beginning, then just start it.
		if (m_dwTimer == m_dwDuration)
		{
			AIL_lock();
			AIL_start_stream(m_hStream);
		}
		else
		{
			dwCurPos = m_dwDuration - m_dwTimer;
			AIL_lock();
			AIL_set_stream_ms_position(m_hStream, dwCurPos);
			AIL_pause_stream(m_hStream, 0);
		}
		AIL_unlock();
	}
	else if (m_h3DSample)
	{
		// Byte offset into the data, on a block boundary.
		dwCurPos = (uint32)((1.0f - (float)m_dwTimer / (float)m_dwDuration) * (float)m_pSoundBuffer->GetSoundDataLen());
		dwCurPos -= dwCurPos % pWaveFormat->nBlockAlign;

		AIL_lock();
		if (AIL_3D_sample_status(m_h3DSample) != SMP_PLAYING)
		{
			if (dwCurPos == 0)
				AIL_start_3D_sample(m_h3DSample);
			else
			{
				AIL_set_3D_sample_offset(m_h3DSample, dwCurPos);
				AIL_resume_3D_sample(m_h3DSample);
			}
		}
		else
		{
			AIL_set_3D_sample_offset(m_h3DSample, dwCurPos);
		}
		AIL_unlock();
	}
	else if (m_hSample)
	{
		dwCurPos = m_dwDuration - m_dwTimer;
		AIL_lock();
		AIL_set_sample_ms_position(m_hSample, dwCurPos);
		AIL_resume_sample(m_hSample);
		AIL_unlock();
	}

	m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_PLAYING | SOUNDINSTANCEFLAG_WASPLAYING;

	// Record time
	AIL_lock();
	m_dwLastTime = AIL_ms_count();
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00490fc0
LTRESULT CSoundInstance::EndLoop()
{
	// Check if we're to loop the sound.
	if (!(m_dwPlaySoundFlags & PLAYSOUND_LOOP))
		return LT_ERROR;

	AIL_lock();

	// Stop looping sound.
	if (m_hSample)
	{
		AIL_set_sample_loop_block(m_hSample, 0, -1);
		AIL_set_sample_loop_count(m_hSample, 1);
	}
	else if (m_h3DSample)
	{
		AIL_set_3D_sample_loop_block(m_h3DSample, 0, -1);
		AIL_set_3D_sample_loop_count(m_h3DSample, 1);
	}
	else if (m_hStream)
	{
		AIL_set_stream_loop_count(m_hStream, 1);
	}

	AIL_unlock();

	m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_ENDLOOP;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00491060
LTRESULT CSoundInstance::FadeOut(float fFadeOutTime)
{
	m_dwFadeTime = (uint32)(fFadeOutTime * 1000.0f + 0.5f);
	m_fFadeVolume = 1.0f;
	m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_FADE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00491090
LTRESULT CSoundInstance::Unload()
{
	Silence(LTTRUE);

	if (m_pSoundBuffer)
	{
		m_pSoundBuffer->RemoveInstance(*this);
		m_pSoundBuffer = LTNULL;
	}
	dl_TieOff(&m_BufferLink);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004910c0
LTRESULT CSoundInstance::Reload()
{
	// Start fresh
	Unload();

	// Make sure our file still exists
	if (!m_pFileIdent || !m_pFileIdent->m_pData)
		return LT_ERROR;

	m_pSoundBuffer = (CSoundBuffer *)m_pFileIdent->m_pData;

	// Get instance and force decompress if 3d.
	if (m_pSoundBuffer->AddInstance(*this) != LT_OK)
		return LT_ERROR;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004910f0
LTRESULT CSoundInstance::SetFilter(const char *pFilter)
{
	HPROENUM next;
	HPROVIDER hFilter;
	char *pName;
	int nFilter;
	LTBOOL bNotFound;

	hFilter = 0;
	bNotFound = LTFALSE;
	m_nFilter = -1;

	if (pFilter)
	{
		// Find the Miles filter provider by name.
		next = HPROENUM_FIRST;
		nFilter = 0;
		while (AIL_enumerate_filters(&next, &hFilter, &pName))
		{
			if (stricmp(pName, pFilter) == 0)
				break;

			hFilter = 0;
			nFilter++;
		}

		if (!hFilter)
			bNotFound = LTTRUE;
		else
			m_nFilter = nFilter;
	}

	// Forget the old filter's parameters.
	for (int i = 0; i < SOUNDINSTANCE_MAXFILTERPARAMS; i++)
		m_nFilterParam[i] = -1;

	if (m_hSample)
		AIL_set_sample_processor(m_hSample, DP_FILTER, hFilter);

	return bNotFound ? LT_NOTFOUND : LT_OK;
}

// FUNCTION: LITHTECH 0x004911c0
LTRESULT CSoundInstance::SetFilterParam(const char *pParam, float fValue)
{
	const char *pFilterName;
	uint32 nParam;

	if (!pParam)
		return LT_INVALIDPARAMS;

	if (GetClientILTSoundMgrImpl()->GetFilterName(m_nFilter, &pFilterName) != LT_OK)
		return LT_NOTFOUND;

	if (GetClientILTSoundMgrImpl()->GetFilterParamIndex(pFilterName, pParam, &nParam) != LT_OK)
		return LT_NOTFOUND;

	// Remember the value, so it can be applied when the sound gets a sample.
	for (uint32 i = 0; i < SOUNDINSTANCE_MAXFILTERPARAMS; i++)
	{
		if (m_nFilterParam[i] == (int)nParam || m_nFilterParam[i] == -1)
		{
			m_nFilterParam[i] = nParam;
			m_fFilterParamValue[i] = fValue;
			break;
		}
	}

	if (m_hSample)
		AIL_set_filter_sample_preference(m_hSample, pParam, &fValue);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00491280
LTRESULT CSoundInstance::GetFilterParam(const char *pParam, float *pValue)
{
	const char *pFilterName;
	uint32 nParam;
	LTBOOL bFound;

	if (!pParam)
		return LT_INVALIDPARAMS;

	if (GetClientILTSoundMgrImpl()->GetFilterName(m_nFilter, &pFilterName) != LT_OK)
		return LT_NOTFOUND;

	if (GetClientILTSoundMgrImpl()->GetFilterParamIndex(pFilterName, pParam, &nParam) != LT_OK)
		return LT_NOTFOUND;

	if (m_hSample)
	{
		AIL_filter_sample_attribute(m_hSample, pParam, pValue);
		return LT_OK;
	}

	bFound = LTFALSE;
	for (uint32 i = 0; i < SOUNDINSTANCE_MAXFILTERPARAMS; i++)
	{
		if (m_nFilterParam[i] == (int)nParam)
		{
			*pValue = m_fFilterParamValue[i];
			bFound = LTTRUE;
			break;
		}
	}

	return bFound ? LT_OK : LT_ERROR;
}

// FUNCTION: LITHTECH 0x00491350
LTRESULT CSoundInstance::ApplyFilter()
{
	HPROENUM next;
	HPROVIDER hFilter;
	char *pName;
	HINTENUM nextAttrib;
	RIB_INTERFACE_ENTRY attrib;
	int nFilter, nParam;

	if (!m_hSample || m_nFilter == -1)
		return LT_OK;

	// Find the filter provider.
	nFilter = m_nFilter;
	next = HPROENUM_FIRST;
	while (AIL_enumerate_filters(&next, &hFilter, &pName))
	{
		if (nFilter == 0)
		{
			AIL_set_sample_processor(m_hSample, DP_FILTER, hFilter);

			// Set the remembered parameters.
			nextAttrib = HINTENUM_FIRST;
			nParam = 0;
			while (AIL_enumerate_filter_sample_attributes(hFilter, &nextAttrib, &attrib))
			{
				for (uint32 i = 0; i < SOUNDINSTANCE_MAXFILTERPARAMS; i++)
				{
					if (m_nFilterParam[i] == -1)
						break;

					if (m_nFilterParam[i] == nParam)
					{
						AIL_set_filter_sample_preference(m_hSample, attrib.entry_name, &m_fFilterParamValue[i]);
						break;
					}
				}

				nParam++;
			}

			return LT_OK;
		}

		nFilter--;
	}

	return LT_ERROR;
}


// FUNCTION: LITHTECH 0x00491450
LTRESULT CLocalSoundInstance::Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime)
{
	LTRESULT dResult;

	if ((dResult = CSoundInstance::Init(soundBuffer, playSoundInfo, dwOffsetTime)) != LT_OK)
		return dResult;

	// Local sounds are always in earshot
	m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_EARSHOT;

	SetPosition(GetClientILTSoundMgrImpl()->GetListenerPosition(), LTTRUE);
	m_fInnerRadius = 5.0f;
	m_fOuterRadius = 10.0f;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004914a0
LTRESULT CLocalSoundInstance::Get3DSamplePosition(LTVector &vPos)
{
	// Put the sound right in front of the listener
	// Position is relative to listener, not absolute
	vPos = GetClientILTSoundMgrImpl()->GetListenerFront();
	vPos *= 5.0f;

	return LT_OK;
}

LTRESULT CLocalSoundInstance::Preupdate(LTVector const& vListenerPos)
{
	return LT_OK;
}

// FUNCTION: LITHTECH 0x004914f0
LTRESULT CLocalSoundInstance::UpdateOutput(uint32 dwFrameTime)
{
	ReverbProperties reverbProperties;
	LTVector vPosition, vUp;
	LTVector vTemp;
	uint16 nTypeVolume;
	uint8 nVolume;
	H3DPOBJECT h3DListener;

	// Make sure we have a channel
	if (!m_hSample && !m_h3DSample && !m_hStream)
		return LT_ERROR;

	if (GetClientILTSoundMgrImpl()->CommitChanges() || !(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING))
	{
		GetClientILTSoundMgrImpl()->GetVolumeByType(nTypeVolume, m_nUserSoundType);
		nVolume = (uint8)(((float)nTypeVolume * 0.01f) * m_nVolume * m_fFadeVolume);
		nVolume = LTMIN(nVolume, 100);

		if (m_hSample)
		{
			// Set new reverb
			if (GetClientILTSoundMgrImpl()->UseSWReverb() && (m_dwPlaySoundFlags & PLAYSOUND_REVERB))
			{
				reverbProperties.m_dwParams = REVERBPARAM_VOLUME | REVERBPARAM_REFLECTTIME | REVERBPARAM_DECAYTIME;
				GetClientILTSoundMgrImpl()->GetReverbProperties(&reverbProperties);
				AIL_lock();
				AIL_set_sample_reverb(m_hSample, reverbProperties.m_fVolume, reverbProperties.m_fReflectTime,
					reverbProperties.m_fDecayTime);
				AIL_unlock();
			}

			AIL_lock();
			if (nVolume != (uint8)AIL_sample_volume(m_hSample))
				AIL_set_sample_volume(m_hSample, nVolume);
			AIL_unlock();
		}
		else if (m_hStream)
		{
			AIL_lock();
			if (nVolume != (uint8)AIL_stream_volume(m_hStream))
				AIL_set_stream_volume(m_hStream, nVolume);
			AIL_unlock();
		}
		else if (m_h3DSample)
		{
			// Put the sound right in front of the listener
			// Position is relative to listener, not absolute
			h3DListener = GetClientILTSoundMgrImpl()->GetListenerObject();
			AIL_lock();
			AIL_3D_orientation(h3DListener, &vPosition.x, &vPosition.y, &vPosition.z, &vUp.x, &vUp.y, &vUp.z);
			vPosition *= 5.0f;

			// Update the position & velocity
			AIL_3D_position(m_h3DSample, &vTemp.x, &vTemp.y, &vTemp.z);
			if (vTemp.DistSqr(vPosition) > 0.5f)
				AIL_set_3D_position(m_h3DSample, vPosition.x, vPosition.y, vPosition.z);
			AIL_unlock();

			AIL_lock();
			if (nVolume != (uint8)AIL_3D_sample_volume(m_h3DSample))
				AIL_set_3D_sample_volume(m_h3DSample, nVolume);
			AIL_unlock();
		}
	}

	// Check if not played yet
	if (!(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING))
	{
		if (StartRendering() != LT_OK)
			return LT_ERROR;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00491790
CAmbientSoundInstance::CAmbientSoundInstance()
{
	m_eType = SOUNDTYPE_AMBIENT;
	m_vPosition.Init();
	m_fInnerRadius = 0.0f;
	m_fOuterRadius = 0.0f;
}

// FUNCTION: LITHTECH 0x004917c0
LTRESULT CAmbientSoundInstance::Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime)
{
	LTRESULT dResult;
	if ((dResult = CSoundInstance::Init(soundBuffer, playSoundInfo, dwOffsetTime)) != LT_OK)
		return dResult;

	SetPosition(playSoundInfo.m_vPosition);
	m_fInnerRadius = playSoundInfo.m_fInnerRadius;
	m_fOuterRadius = playSoundInfo.m_fOuterRadius + 0.01f;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00491810
LTRESULT CAmbientSoundInstance::Get3DSamplePosition(LTVector &vPos)
{
	LTVector vUp;
	float fDist;

	fDist = m_vPosition.Dist(GetClientILTSoundMgrImpl()->GetListenerPosition());

	// Put the sound in front of the listener at a distance
	// Position is relative to listener not absolute
	vPos = GetClientILTSoundMgrImpl()->GetListenerFront();
	vUp = vPos.Cross(GetClientILTSoundMgrImpl()->GetListenerRight());
	vPos *= fDist;

	return LT_OK;
}

// Identical to C3DSoundInstance::Preupdate (folded by the linker).
// FUNCTION: LITHTECH 0x004918b0
LTRESULT CAmbientSoundInstance::Preupdate(LTVector const& vListenerPos)
{
	return PreUpdatePositionalSound(vListenerPos);
}

inline LTRESULT CSoundInstance::PreUpdatePositionalSound(LTVector const& vListenerPos)
{
	float fDist;

	if (m_dwPlaySoundFlags & PLAYSOUND_CLIENTLOCAL)
	{
		if (g_pClientMgr->m_pCurShell && g_pClientMgr->m_pCurShell->m_pFrameClientObject)
			m_vPosition = g_pClientMgr->m_pCurShell->m_pFrameClientObject->GetPos();
	}

	// Calculate distance from listener to sound...
	fDist = vListenerPos.Dist(m_vPosition);

	// Put priority between 0 and 1
	if (fDist > 1.0f)
		m_fModifiedPriority = m_nVolume / 100.0f / fDist;
	else
		m_fModifiedPriority = m_nVolume / 100.0f;

	// Offset by the real priority
	m_fModifiedPriority += (float)m_nPriority;

	if (fDist <= 1.5f * m_fOuterRadius)
	{
		m_dwSoundInstanceFlags |= SOUNDINSTANCEFLAG_EARSHOT;
	}
	else
	{
		m_dwSoundInstanceFlags &= ~SOUNDINSTANCEFLAG_EARSHOT;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004919c0
LTRESULT CAmbientSoundInstance::UpdateOutput(uint32 dwFrameTime)
{
	uint16 nTypeVolume;
	uint8 nScaledVolume;
	float fDist, fDistSqrd, fMinDistSqrd, fMaxDistSqrd, fScale;
	LTVector vPosition, vUp;
	LTVector vTemp;

	// Make sure we have a channel
	if (!m_hSample && !m_h3DSample && !m_hStream)
		return LT_ERROR;

	if (GetClientILTSoundMgrImpl()->CommitChanges() || !(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING))
	{
		if (m_hSample || m_hStream)
		{
			// Calculate distance from listener to sound...
			fDistSqrd = m_vPosition.DistSqr(GetClientILTSoundMgrImpl()->GetListenerPosition());
			fMinDistSqrd = m_fInnerRadius * m_fInnerRadius;
			fMaxDistSqrd = m_fOuterRadius * m_fOuterRadius;

			// Find volume scale based on distance...
			if (fDistSqrd <= fMinDistSqrd)
				fScale = 1.0f;
			else if (fDistSqrd >= fMaxDistSqrd)
				fScale = 0.0f;
			else
			{
				// These calculations fall off from 1.0 at mindist to 0 at maxdist linearly...
				fDist = (float)sqrt(fDistSqrd);
				fScale = 1.0f - ((fDist - m_fInnerRadius) / (m_fOuterRadius - m_fInnerRadius));
			}

			// Scale volume for roll-off...
			GetClientILTSoundMgrImpl()->GetVolumeByType(nTypeVolume, m_nUserSoundType);
			nScaledVolume = (uint8)(((float)nTypeVolume * 0.01f) * m_nVolume * m_fFadeVolume * fScale);
			nScaledVolume = LTMIN(nScaledVolume, 100);

			// Set new volume...
			AIL_lock();
			if (m_hSample)
			{
				if (nScaledVolume != (uint8)AIL_sample_volume(m_hSample))
					AIL_set_sample_volume(m_hSample, nScaledVolume);
			}
			else
			{
				if (nScaledVolume != (uint8)AIL_stream_volume(m_hStream))
					AIL_set_stream_volume(m_hStream, nScaledVolume);
			}
			AIL_unlock();
		}
		else if (m_h3DSample)
		{
			fDist = m_vPosition.Dist(GetClientILTSoundMgrImpl()->GetListenerPosition());

			// Put the sound in front of the listener at a distance
			// Position is relative to listener not absolute
			vPosition = GetClientILTSoundMgrImpl()->GetListenerFront();
			vUp = vPosition.Cross(GetClientILTSoundMgrImpl()->GetListenerRight());
			vPosition *= fDist;

			AIL_lock();
			AIL_3D_position(m_h3DSample, &vTemp.x, &vTemp.y, &vTemp.z);
			if (vTemp.DistSqr(vPosition) > 0.5f)
				AIL_set_3D_position(m_h3DSample, vPosition.x, vPosition.y, vPosition.z);
			AIL_unlock();

			GetClientILTSoundMgrImpl()->GetVolumeByType(nTypeVolume, m_nUserSoundType);
			nScaledVolume = (uint8)(((float)nTypeVolume * 0.01f) * m_nVolume * m_fFadeVolume);
			nScaledVolume = LTMIN(nScaledVolume, 100);

			AIL_lock();
			if (nScaledVolume != (uint8)AIL_3D_sample_volume(m_h3DSample))
				AIL_set_3D_sample_volume(m_h3DSample, nScaledVolume);
			AIL_unlock();
		}
	}

	// Check if not played yet
	if (!(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING))
	{
		if (StartRendering() != LT_OK)
			return LT_ERROR;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00491d40
C3DSoundInstance::C3DSoundInstance()
{
	m_eType = SOUNDTYPE_3D;
	m_vPosition.Init();
	m_vLastPosition.Init();
	m_vVelocity.Init();
	m_fInnerRadius = 0.0f;
	m_fOuterRadius = 0.0f;
}

// FUNCTION: LITHTECH 0x00491da0
LTRESULT C3DSoundInstance::SetPosition(const LTVector &vPos, LTBOOL bTeleport)
{
	m_vPosition = vPos;
	if (bTeleport)
	{
		m_vLastPosition = vPos;
		m_vVelocity.Init();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00491e00
LTRESULT C3DSoundInstance::Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime)
{
	LTRESULT dResult;
	if ((dResult = CSoundInstance::Init(soundBuffer, playSoundInfo, dwOffsetTime)) != LT_OK)
		return dResult;

	SetPosition(playSoundInfo.m_vPosition, LTTRUE);
	m_fInnerRadius = playSoundInfo.m_fInnerRadius;
	m_fOuterRadius = playSoundInfo.m_fOuterRadius + 0.01f;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00491e50
LTRESULT C3DSoundInstance::Get3DSamplePosition(LTVector &vPos)
{
	// Update the position & velocity
	// Position is relative to listener not absolute
	vPos = m_vPosition - GetClientILTSoundMgrImpl()->GetListenerPosition();

	return LT_OK;
}

LTRESULT C3DSoundInstance::Preupdate(LTVector const& vListenerPos)
{
	return PreUpdatePositionalSound(vListenerPos);
}

// FUNCTION: LITHTECH 0x00491ec0
LTRESULT C3DSoundInstance::UpdateOutput(uint32 dwFrameTime)
{
	uint32 nVolume, nPan;
	LTVector vRelPos, vNormRelPos, vTemp;
	float fDistSqrd, fMinDistSqrd, fMaxDistSqrd, fScale, fFrontScale, fDist;
	LTVector vPosition, vVelocity;

	// Make sure we have a channel
	if (!m_hSample && !m_h3DSample && !m_hStream)
		return LT_ERROR;

	if (GetClientILTSoundMgrImpl()->CommitChanges() || !(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING))
	{
		// Update the velocity.
		m_vVelocity = m_vPosition - m_vLastPosition;
		if (dwFrameTime > 0)
		{
			m_vVelocity *= 1.0f / (float)dwFrameTime;
		}
		else
		{
			m_vVelocity.Init();
		}
		m_vLastPosition = m_vPosition;

		// Calculate distance from listener to sound...
		vRelPos = GetClientILTSoundMgrImpl()->GetListenerPosition() - m_vPosition;
		vNormRelPos = vRelPos;
		vNormRelPos.Norm();

		// Handle panning 3d using 2d buffers.
		if (m_hSample || m_hStream)
		{
			uint16 nTypeVolume;

			//------------------------
			// Volume section...
			//------------------------

			fDistSqrd = vRelPos.MagSqr();
			fMinDistSqrd = m_fInnerRadius * m_fInnerRadius;
			fMaxDistSqrd = m_fOuterRadius * m_fOuterRadius;

			// Find volume scale based on distance...
			if (fDistSqrd <= fMinDistSqrd)
				fScale = 1.0f;
			else if (fDistSqrd >= fMaxDistSqrd)
				fScale = 0.0f;
			else
			{
				// These calculations fall off from 1.0 at mindist to 0 at maxdist...
				fDist = (float)sqrt(fDistSqrd);
				fScale = 1.0f - ((fDist - m_fInnerRadius) / (m_fOuterRadius - m_fInnerRadius));
			}

			// Calculate how much the sound is in back of listener.  Sounds will be quieter when
			// they come from behind.  vRelPos's direction is relative to the sound, so if the listener
			// is pointing at sound, then dot product will be negative...
			fFrontScale = GetClientILTSoundMgrImpl()->GetListenerFront().Dot(vNormRelPos);

			// Sound is in front of listener, no volume loss...
			if (fFrontScale < 0.0f)
			{
				fFrontScale = 1.0f;
			}
			// Sound is behind listener, so scale it down by at most 25%...
			else
			{
				fFrontScale = 1.0f - 0.25f * fFrontScale;
			}

			GetClientILTSoundMgrImpl()->GetVolumeByType(nTypeVolume, m_nUserSoundType);
			nVolume = (uint32)(((float)nTypeVolume * 0.01f) * m_nVolume * m_fFadeVolume * fFrontScale * fScale);
			nVolume = LTMIN(nVolume, 100);

			AIL_lock();
			if (m_hSample)
			{
				if (nVolume != (uint32)AIL_sample_volume(m_hSample))
					AIL_set_sample_volume(m_hSample, nVolume);
			}
			else
			{
				if (nVolume != (uint32)AIL_stream_volume(m_hStream))
					AIL_set_stream_volume(m_hStream, nVolume);
			}
			AIL_unlock();

			//------------------------
			// Pan section...
			//------------------------

			// Get pan scale factor...
			fScale = -GetClientILTSoundMgrImpl()->GetListenerRight().Dot(vNormRelPos);
			// Always play a little in the other side...
			fScale = LTCLAMP(fScale, -0.75f, 0.75f);
			nPan = (uint32)((fScale + 1.0f) * 64.0f);

			// Set pan...
			AIL_lock();
			if (m_hSample)
			{
				if (nPan != (uint32)AIL_sample_pan(m_hSample))
					AIL_set_sample_pan(m_hSample, nPan);
			}
			else
			{
				if (nPan != (uint32)AIL_stream_pan(m_hStream))
					AIL_set_stream_pan(m_hStream, nPan);
			}
			AIL_unlock();
		}
		// Handle true 3d
		else if (m_h3DSample)
		{
			uint16 nTypeVolume;

			// Update the position & velocity
			// Position is relative to listener not absolute
			vPosition = m_vPosition - GetClientILTSoundMgrImpl()->GetListenerPosition();

			AIL_lock();
			AIL_3D_position(m_h3DSample, &vTemp.x, &vTemp.y, &vTemp.z);
			if (vTemp.DistSqr(vPosition) > 0.5f)
				AIL_set_3D_position(m_h3DSample, vPosition.x, vPosition.y, vPosition.z);

			vVelocity = m_vVelocity - GetClientILTSoundMgrImpl()->GetListenerVelocity();
			AIL_3D_velocity(m_h3DSample, &vTemp.x, &vTemp.y, &vTemp.z);
			if (vTemp.DistSqr(vVelocity) > 0.5f)
			{
				AIL_set_3D_velocity_vector(m_h3DSample,
					vVelocity.x * GetClientILTSoundMgrImpl()->GetDistanceFactor(),
					vVelocity.y * GetClientILTSoundMgrImpl()->GetDistanceFactor(),
					vVelocity.z * GetClientILTSoundMgrImpl()->GetDistanceFactor());
			}

			if (!(m_dwPlaySoundFlags & PLAYSOUND_REVERB))
				AIL_set_3D_sample_effects_level(m_h3DSample, 0.0f);
			else
				AIL_set_3D_sample_effects_level(m_h3DSample, 1.0f);

			// set the volume of the sound
			GetClientILTSoundMgrImpl()->GetVolumeByType(nTypeVolume, m_nUserSoundType);
			nVolume = (uint8)(((float)nTypeVolume * 0.01f) * m_nVolume * m_fFadeVolume);
			nVolume = LTMIN(nVolume, 100);
			if (nVolume != (uint8)AIL_3D_sample_volume(m_h3DSample))
				AIL_set_3D_sample_volume(m_h3DSample, nVolume);
			AIL_unlock();
		}
	}

	// Check if not played yet
	if (!(m_dwSoundInstanceFlags & SOUNDINSTANCEFLAG_PLAYING))
	{
		if (StartRendering() != LT_OK)
			return LT_ERROR;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004924e0
LTRESULT C3DSoundInstance::SetObstruction(LTFLOAT fLevel)
{
	AIL_lock();
	AIL_set_3D_sample_obstruction(m_h3DSample, fLevel);
	AIL_unlock();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00492510
LTRESULT C3DSoundInstance::GetObstruction(LTFLOAT &fLevel)
{
	AIL_lock();
	fLevel = AIL_3D_sample_obstruction(m_h3DSample);
	AIL_unlock();
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00492540
LTRESULT C3DSoundInstance::SetOcclusion(LTFLOAT fLevel)
{
	AIL_lock();
	AIL_set_3D_sample_occlusion(m_h3DSample, fLevel);
	AIL_unlock();
	return LT_OK;
}

// Talon has no occlusion getter in Miles; identical to GetObstruction (folded by the linker).
LTRESULT C3DSoundInstance::GetOcclusion(LTFLOAT &fLevel)
{
	AIL_lock();
	fLevel = AIL_3D_sample_obstruction(m_h3DSample);
	AIL_unlock();
	return LT_OK;
}
