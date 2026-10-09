// Jupiter runtime/sound/src/soundmgr.cpp, Talon version.
// Talon drives Miles directly (every AIL call is bracketed by AIL_lock/AIL_unlock), identifies the
// 3d providers by name, keeps per-type volumes, limits the instances per buffer, and implements
// the software filter interface (Miles filter providers).
#include <windows.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "client_filemgr.h"
#include "de_objects.h"
#include "soundmgr.h"
#include "soundinstance.h"
#include "soundbuffer.h"
#include "engine_vars.h"

#ifndef TYPECODE_SOUND
#define TYPECODE_SOUND		4
#endif




// The original calls the out-of-line CMoArray<uint8> constructor (0x004961c0, Clear() only) for
// m_SoundUpdatePacket.
// FUNCTION: LITHTECH 0x00492570
CSoundMgr::CSoundMgr()
{
	m_ClientServerType = ClientType;

	m_hDigDriver = LTNULL;

	memset(&m_InitSoundInfo, 0, sizeof(InitSoundInfo));

	m_p3DProviderList = LTNULL;

	memset(&m_3DProvider, 0, sizeof(CProvider));
	m_nNum3DSamples = 0;
	m_nMax3DSamples = 0;
	m_p3DSampleList = LTNULL;
	dl_InitList(&m_3DFreeSampleList);

	m_nNumSWSamples = 0;
	m_nMaxSWSamples = 0;
	m_pSWSampleList = LTNULL;
	dl_InitList(&m_SWFreeSampleList);

	dl_InitList(&m_SoundBufferList);
	m_SoundBufferBank.Term();
	m_LocalSoundInstanceBank.Term();
	m_AmbientSoundInstanceBank.Term();
	m_3DSoundInstanceBank.Term();

	m_h3DListener = LTNULL;
	m_vListenerPosition.Init();
	m_vLastListenerPosition.Init();
	m_vListenerVelocity.Init();
	m_vListenerForward.Init(0.0f, 0.0f, 1.0f);
	m_vListenerRight.Init(1.0f, 0.0f, 0.0f);
	m_vListenerUp.Init(0.0f, 1.0f, 0.0f);

	m_bListenerInClient = LTTRUE;

	m_nNumSoundsHeard = 0;

	memset(m_SoundInstanceList, 0, SOUNDMGR_MAXSOUNDINSTANCES * sizeof(CSoundInstance *));
	m_dwNumSoundInstances = 0;

	m_bConvert16to8 = LTFALSE;

	m_fDistanceFactor = 1.0f;

	m_bSWReverb = LTFALSE;
	m_b3DReverb = LTFALSE;
	m_fReverbVolume = 0.0f;

	m_dwReverbAcoustics = REVERB_ACOUSTICS_GENERIC;

	m_fReverbReflectTime = 0.0f;
	m_fReverbDecayTime = 0.1f;
	m_fReverbDamping = 1.0f;

	m_bDigitalHandleReleased = LTFALSE;
	m_bReacquireDigitalHandle = LTFALSE;

	m_dwCurTime = 0;
	m_dwCommitTime = 0;
	m_bCommitChanges = LTFALSE;

	m_bValid = LTFALSE;
	m_bEnabled = LTFALSE;
}

// FUNCTION: LITHTECH 0x00492840
CSoundMgr::~CSoundMgr()
{
	Term();
	ReleaseProviderList(m_p3DProviderList);
	m_p3DProviderList = LTNULL;
}

// FUNCTION: LITHTECH 0x00492910
void CSoundMgr::SetPreferences()
{
	AIL_lock();
	AIL_set_preference(AIL_ENABLE_MMX_SUPPORT, LTTRUE);
	AIL_set_preference(DIG_USE_WAVEOUT, LTFALSE);
	AIL_set_preference(DIG_REVERB_BUFFER_SIZE, 0x40000);
	AIL_set_preference(AIL_LOCK_PROTECTION, LTFALSE);
	AIL_unlock();
}

// FUNCTION: LITHTECH 0x00492940
LTRESULT CSoundMgr::Init(InitSoundInfo &soundInit)
{
	uint32 nSamples, dwIndex;
	S32 nRoomType;
	LTBOOL bRemoveSounds;
	CSoundInstance *pSoundInstance;
	CSoundBuffer *pSoundBuffer;
	LTLink *pCur;
	WAVEFORMATEX waveFormat;
	ReverbProperties reverbProperties;

	// For NT debugging..
	if (g_CV_ForceNoSound)
	{
		return LT_OK;
	}

	// Copy the init structure in case we have to do an auto term/init later.
	m_InitSoundInfo = soundInit;
	soundInit.m_dwResults = 0;

	bRemoveSounds = (soundInit.m_dwFlags & INITSOUNDINFOFLAG_RELOADSOUNDS) ? LTFALSE : LTTRUE;
	if (!m_dwNumSoundInstances)
		bRemoveSounds = LTTRUE;

	// Unload the sounds temporarily if we need to keep them around
	else if (!bRemoveSounds)
	{
		for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
		{
			pSoundInstance = m_SoundInstanceList[dwIndex];
			if (!pSoundInstance)
				continue;

			if (pSoundInstance->Unload() != LT_OK)
			{
				RemoveInstance(*pSoundInstance);
				dwIndex--;
			}
		}

		pCur = m_SoundBufferList.m_Head.m_pNext;
		while (pCur != &m_SoundBufferList.m_Head)
		{
			pSoundBuffer = (CSoundBuffer *)pCur->m_pData;
			pCur = pCur->m_pNext;
			if (!pSoundBuffer)
				continue;

			if (pSoundBuffer->Unload() != LT_OK)
			{
				RemoveBuffer(*pSoundBuffer);
			}
		}
	}

	// Start fresh
	Term(bRemoveSounds);

	m_bValid = LTTRUE;
	m_bEnabled = LTTRUE;

	// Initialize MSS
	MSS_auto_cleanup();
	AIL_startup();
	SetPreferences();

	// Set the maximum number of sw channels
	soundInit.m_nNumSWVoices = LTMIN(soundInit.m_nNumSWVoices, SOUNDMGR_MAXSOUNDINSTANCES);
	AIL_lock();
	AIL_set_preference(DIG_MIXER_CHANNELS, soundInit.m_nNumSWVoices);
	AIL_unlock();

	// Set the output format
	memset(&waveFormat, 0, sizeof(waveFormat));
	waveFormat.wFormatTag = WAVE_FORMAT_PCM;
	waveFormat.nChannels = 2;
	waveFormat.nSamplesPerSec = soundInit.m_nSampleRate;
	waveFormat.wBitsPerSample = soundInit.m_nBitsPerSample;
	waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) >> 3;
	waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

	m_bConvert16to8 = soundInit.m_dwFlags & INITSOUNDINFOFLAG_CONVERT16TO8;

	// Open the wave device
	AIL_lock();
	if (AIL_waveOutOpen(&m_hDigDriver, 0, WAVE_MAPPER, (LPWAVEFORMAT)&waveFormat))
	{
		AIL_unlock();
		Term();
		return LT_UNABLETOINITSOUND;
	}
	AIL_unlock();

	m_bDigitalHandleReleased = LTFALSE;
	m_bReacquireDigitalHandle = LTFALSE;

	Get3DProviderLists(m_p3DProviderList, LTFALSE);

	// Choose hw and 3d providers.
	if (soundInit.m_sz3DProvider[0] && Set3DProvider(soundInit.m_sz3DProvider) != LT_OK)
	{
		Term();
		return LT_NO3DSOUNDPROVIDER;
	}

	// Set the maximum number of samples for 3d
	if (m_3DProvider.m_hProvider)
	{
		AIL_lock();
		AIL_3D_provider_attribute(m_3DProvider.m_hProvider, "Maximum supported samples", &nSamples);
		AIL_unlock();

		nSamples = LTMIN(nSamples, 255);
		m_nMax3DSamples = LTMIN((uint8)nSamples, SOUNDMGR_MAXSOUNDINSTANCES);
		m_nMax3DSamples = LTMIN(m_nMax3DSamples, soundInit.m_nNum3DVoices);
	}

	// Set the maximum number of samples for sw
	AIL_lock();
	nSamples = AIL_get_preference(DIG_MIXER_CHANNELS);
	AIL_unlock();
	if (nSamples > 4)
		nSamples -= 4;
	m_nMaxSWSamples = (uint8)LTMIN(soundInit.m_nNumSWVoices, nSamples);
	m_nMaxSWSamples = (uint8)LTMIN(m_nMaxSWSamples, SOUNDMGR_MAXSOUNDINSTANCES - m_nMax3DSamples);

	// Precreate all the samples
	Create3DSamples();
	CreateSWSamples();

	// Create the 3d listener
	if (m_3DProvider.m_hProvider)
	{
		AIL_lock();
		m_h3DListener = AIL_open_3D_listener(m_3DProvider.m_hProvider);
		AIL_unlock();

		if (!m_h3DListener)
		{
			Term();
			return LT_NO3DSOUNDPROVIDER;
		}

		AIL_lock();
		AIL_set_3D_position(m_h3DListener, 0.0f, 0.0f, 0.0f);
		AIL_set_3D_velocity_vector(m_h3DListener, 0.0f, 0.0f, 0.0f);
		AIL_set_3D_orientation(m_h3DListener, m_vListenerForward.x, m_vListenerForward.y, m_vListenerForward.z,
			m_vListenerUp.x, m_vListenerUp.y, m_vListenerUp.z);
		AIL_unlock();

		// Check for eax support
		AIL_lock();
		nRoomType = AIL_3D_room_type(m_3DProvider.m_hProvider);
		AIL_unlock();
		if (nRoomType != -1)
		{
			soundInit.m_dwResults |= INITSOUNDINFORESULTS_REVERB;
			m_b3DReverb = LTTRUE;
		}
	}

	if (bRemoveSounds)
	{
		// Initialize the lists and banks
		m_SoundBufferBank.Init(32, 32);
		dl_InitList(&m_SoundBufferList);
		m_LocalSoundInstanceBank.Init(16, 16);
		m_AmbientSoundInstanceBank.Init(16, 16);
		m_3DSoundInstanceBank.Init(32, 32);

		memset(m_SoundInstanceList, 0, SOUNDMGR_MAXSOUNDINSTANCES * sizeof(CSoundInstance *));
		m_dwNumSoundInstances = 0;
	}
	// Reload the sounds
	else
	{
		pCur = m_SoundBufferList.m_Head.m_pNext;
		while (pCur != &m_SoundBufferList.m_Head)
		{
			pSoundBuffer = (CSoundBuffer *)pCur->m_pData;
			pCur = pCur->m_pNext;
			if (!pSoundBuffer)
				continue;

			if (pSoundBuffer->Reload() != LT_OK)
				RemoveBuffer(*pSoundBuffer);
		}

		for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
		{
			pSoundInstance = m_SoundInstanceList[dwIndex];
			if (!pSoundInstance)
				continue;

			if (pSoundInstance->Reload() != LT_OK)
			{
				RemoveInstance(*pSoundInstance);
				dwIndex--;
			}
		}
	}
	m_nNumSoundsHeard = 0;

	if (soundInit.m_fDistanceFactor > 0.0f)
		m_fDistanceFactor = soundInit.m_fDistanceFactor;

	soundInit.m_nVolume = LTMIN(soundInit.m_nVolume, 100);
	SetVolume((uint8)soundInit.m_nVolume);

	// Set up the reverb
	if (m_bSWReverb || m_b3DReverb)
	{
		reverbProperties.m_dwParams = REVERBPARAM_ALL;
		reverbProperties.m_fVolume = 0.0f;
		reverbProperties.m_dwAcoustics = REVERB_ACOUSTICS_GENERIC;
		reverbProperties.m_fReflectTime = 0.0f;
		reverbProperties.m_fDecayTime = 0.1f;
		reverbProperties.m_fDamping = 0.99f;
		SetReverbProperties(&reverbProperties);
	}

	m_SoundUpdatePacket.Init(MAX_PACKET_LEN, MAX_PACKET_LEN);

	m_SoundTypeVolumes.SetVolume(0, 100);

	AIL_lock();
	m_dwCurTime = AIL_ms_count();
	AIL_unlock();

	m_dwCommitTime = 0;
	m_bCommitChanges = LTFALSE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00492fb0
void CSoundMgr::Term(LTBOOL bRemoveSounds)
{
	CSoundBuffer *pSoundBuffer;
	CSoundInstance *pSoundInstance;
	LTLink *pCur, *pNext;
	uint32 dwIndex;

	if (bRemoveSounds)
	{
		// Remove all the sound instances
		for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
		{
			pSoundInstance = m_SoundInstanceList[dwIndex];
			if (pSoundInstance)
			{
				if (pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_GETHANDLE)
				{
					if (g_DebugLevel >= 2)
					{
						pSoundBuffer = pSoundInstance->GetSoundBuffer();
						if (pSoundBuffer)
							dsi_ConsolePrint("Unfreed sound handle %s", pSoundBuffer->GetFileIdent()->m_Filename);
						else
							dsi_ConsolePrint("Unfreed sound handle 0x%x", pSoundInstance);
					}
				}

				RemoveInstance(*pSoundInstance);
				dwIndex--;
			}
		}
		m_dwNumSoundInstances = 0;

		m_LocalSoundInstanceBank.Term();
		m_AmbientSoundInstanceBank.Term();
		m_3DSoundInstanceBank.Term();

		// Remove all the sound buffers
		pCur = m_SoundBufferList.m_Head.m_pNext;
		while (pCur != &m_SoundBufferList.m_Head)
		{
			pSoundBuffer = (CSoundBuffer *)pCur->m_pData;
			pNext = pCur->m_pNext;
			if (!pSoundBuffer)
				continue;

			RemoveBuffer(*pSoundBuffer);

			pCur = pNext;
		}
		dl_InitList(&m_SoundBufferList);
		m_SoundBufferBank.Term();
	}

	// Remove the samples
	Remove3DSamples();
	RemoveSWSamples();

	// Release the 3d listener
	if (m_h3DListener)
	{
		AIL_lock();
		AIL_close_3D_listener(m_h3DListener);
		AIL_unlock();
		m_h3DListener = LTNULL;
	}

	// Release the providers
	if (m_3DProvider.m_hProvider)
	{
		AIL_lock();
		AIL_close_3D_provider(m_3DProvider.m_hProvider);
		AIL_unlock();
		m_3DProvider.m_hProvider = LTNULL;
	}

	// Release the waveout device
	if (m_hDigDriver)
	{
		AIL_lock();
		AIL_waveOutClose(m_hDigDriver);
		AIL_unlock();
		m_hDigDriver = LTNULL;
		m_bDigitalHandleReleased = LTFALSE;
		m_bReacquireDigitalHandle = LTFALSE;
	}

	AIL_shutdown();

	m_bValid = LTFALSE;
	m_bEnabled = LTFALSE;
}

// FUNCTION: LITHTECH 0x00493140
void CSoundMgr::ReleaseProviderList(CProvider *pProvider)
{
	CProvider *pNextProvider;

	while (pProvider)
	{
		pNextProvider = pProvider->m_pNextProvider;
		delete pProvider;
		pProvider = pNextProvider;
	}
}

// FUNCTION: LITHTECH 0x00493170
LTRESULT CSoundMgr::Get3DProviderLists(CProvider *&p3DProviderList, LTBOOL bVerifyOpens)
{
	HPROENUM next;
	HPROVIDER hProvider;
	HDIGDRIVER hDigDriver;
	char *szName;
	CProvider *pProvider;
	uint32 dwCaps, dwProviderID;
	S32 dwResult;

	// Check if we don't already have the lists
	if (!m_p3DProviderList)
	{
		// This could be called before sound is initialized
		if (!m_bValid)
		{
			MSS_auto_cleanup();
			AIL_startup();
			SetPreferences();

			AIL_lock();
			if (AIL_waveOutOpen(&hDigDriver, 0, WAVE_MAPPER, LTNULL))
			{
				AIL_unlock();
				AIL_shutdown();
				return LT_ERROR;
			}
			AIL_unlock();
		}

		next = HPROENUM_FIRST;
		AIL_lock();
		while (AIL_enumerate_3D_providers(&next, &hProvider, &szName))
		{
			dwCaps = 0;

			if (strcmp(szName, "Microsoft DirectSound3D with Creative Labs EAX(TM)") == 0)
				dwProviderID = SOUND3DPROVIDERID_DS3D_HARDWARE_EAX;
			else if (strcmp(szName, "Aureal A3D Interactive(TM)") == 0)
				dwProviderID = SOUND3DPROVIDERID_A3D;
			else if (strcmp(szName, "Microsoft DirectSound3D hardware support") == 0)
				dwProviderID = SOUND3DPROVIDERID_DS3D_HARDWARE;
			else if (strcmp(szName, "Microsoft DirectSound3D software emulation") == 0)
				dwProviderID = SOUND3DPROVIDERID_DS3D_SOFTWARE;
			else if (strcmp(szName, "Intel Realistic Sound Experience(TM)") == 0)
				dwProviderID = SOUND3DPROVIDERID_INTEL_RSX;
			else if (strcmp(szName, "Miles 2D Stereo Positional Audio") == 0)
				dwProviderID = SOUND3DPROVIDERID_MILES3D;
			else
				dwProviderID = SOUND3DPROVIDERID_UNKNOWN;

			if (bVerifyOpens)
			{
				// Make sure the provider works
				if (AIL_open_3D_provider(hProvider) != M3D_NOERR)
					continue;

				// Check for eax support
				AIL_3D_provider_attribute(hProvider, "EAX environment selection", &dwResult);
				if (dwResult != -1)
					dwCaps = SOUND3DPROVIDER_CAPS_REVERB;

				AIL_close_3D_provider(hProvider);
			}

			pProvider = new CProvider;
			pProvider->m_pNextProvider = m_p3DProviderList;
			m_p3DProviderList = pProvider;
			m_p3DProviderList->m_dwCaps = dwCaps;
			m_p3DProviderList->m_dwProviderID = dwProviderID;
			pProvider->m_hProvider = hProvider;
			LTStrCpy(pProvider->m_szProviderName, szName, sizeof(pProvider->m_szProviderName));
		}
		AIL_unlock();

		if (!m_bValid)
		{
			AIL_lock();
			AIL_waveOutClose(hDigDriver);
			AIL_unlock();
			AIL_shutdown();
		}
	}

	p3DProviderList = m_p3DProviderList;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004933a0
LTRESULT CSoundMgr::Set3DProvider(char *psz3DProviderName)
{
	CProvider *pProvider;

	if (!m_bValid)
		return LT_ERROR;

	m_3DProvider.m_hProvider = LTNULL;
	pProvider = m_p3DProviderList;
	while (pProvider)
	{
		// Check if found provider
		if (strcmp(pProvider->m_szProviderName, psz3DProviderName) == 0)
		{
			m_3DProvider = *pProvider;
			break;
		}

		pProvider = pProvider->m_pNextProvider;
	}

	if (!m_3DProvider.m_hProvider)
		return LT_ERROR;

	AIL_lock();
	if (AIL_open_3D_provider(m_3DProvider.m_hProvider) != M3D_NOERR)
	{
		AIL_unlock();
		m_3DProvider.m_hProvider = LTNULL;
		return LT_ERROR;
	}
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00493470
LTRESULT CSoundMgr::Create3DSamples()
{
	uint8 nSample;

	if (!m_bValid)
		return LT_ERROR;

	dl_InitList(&m_3DFreeSampleList);

	// Check if 3d is available
	if (!m_3DProvider.m_hProvider)
		return LT_ERROR;

	if (m_p3DSampleList)
	{
		Remove3DSamples();
		m_p3DSampleList = LTNULL;
	}

	// Create an array of the 3D samples
	if (m_nMax3DSamples > 0)
	{
		m_p3DSampleList = new CSample[m_nMax3DSamples];

		AIL_lock();
		for (nSample = 0; nSample < m_nMax3DSamples; nSample++)
		{
			m_p3DSampleList[nSample].m_h3DSample = AIL_allocate_3D_sample_handle(m_3DProvider.m_hProvider);
			if (!m_p3DSampleList[nSample].m_h3DSample)
				break;
			AIL_set_3D_user_data(m_p3DSampleList[nSample].m_h3DSample, SAMPLE_TYPE, SAMPLETYPE_3D);
			AIL_set_3D_user_data(m_p3DSampleList[nSample].m_h3DSample, SAMPLE_LISTITEM, (S32)&m_p3DSampleList[nSample]);
			dl_AddHead(&m_3DFreeSampleList, &m_p3DSampleList[nSample].m_Link, &m_p3DSampleList[nSample]);
		}
		AIL_unlock();

		m_nNum3DSamples = nSample;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004935b0
LTRESULT CSoundMgr::Remove3DSamples()
{
	CSoundInstance *pSoundInstance;
	uint8 nSample;

	// Check if 3d is available
	if (!m_3DProvider.m_hProvider)
		return LT_ERROR;

	// Remove the array of samples
	if (m_p3DSampleList && m_nNum3DSamples)
	{
		nSample = m_nNum3DSamples;
		while (nSample-- > 0)
		{
			// Get rid of any instance using this sample
			pSoundInstance = GetLink3DSampleSoundInstance(m_p3DSampleList[nSample].m_h3DSample);
			if (pSoundInstance)
			{
				pSoundInstance->Silence(LTTRUE);
			}

			AIL_lock();
			AIL_release_3D_sample_handle(m_p3DSampleList[nSample].m_h3DSample);
			AIL_unlock();
		}

		delete[] m_p3DSampleList;
		m_p3DSampleList = LTNULL;
		m_nNum3DSamples = 0;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00493670
LTRESULT CSoundMgr::CreateSWSamples()
{
	uint8 nSample;

	if (!m_bValid)
		return LT_ERROR;

	dl_InitList(&m_SWFreeSampleList);

	// Check if SW is available
	if (!m_hDigDriver)
		return LT_ERROR;

	if (m_pSWSampleList)
	{
		RemoveSWSamples();
		m_pSWSampleList = LTNULL;
	}

	// Create an array of the SW samples
	if (m_nMaxSWSamples > 0)
	{
		m_pSWSampleList = new CSample[m_nMaxSWSamples];

		AIL_lock();
		for (nSample = 0; nSample < m_nMaxSWSamples; nSample++)
		{
			m_pSWSampleList[nSample].m_hSample = AIL_allocate_sample_handle(m_hDigDriver);
			if (!m_pSWSampleList[nSample].m_hSample)
				break;
			AIL_set_sample_user_data(m_pSWSampleList[nSample].m_hSample, SAMPLE_TYPE, SAMPLETYPE_SW);
			AIL_set_sample_user_data(m_pSWSampleList[nSample].m_hSample, SAMPLE_LISTITEM, (S32)&m_pSWSampleList[nSample]);
			dl_AddHead(&m_SWFreeSampleList, &m_pSWSampleList[nSample].m_Link, &m_pSWSampleList[nSample]);
		}
		AIL_unlock();

		m_nNumSWSamples = nSample;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004937b0
LTRESULT CSoundMgr::RemoveSWSamples()
{
	CSoundInstance *pSoundInstance;
	uint8 nSample;

	// Check if SW is available
	if (!m_hDigDriver)
		return LT_ERROR;

	// Remove the array of samples
	if (m_pSWSampleList && m_nNumSWSamples)
	{
		nSample = m_nNumSWSamples;
		while (nSample-- > 0)
		{
			// Get rid of any instance using this sample
			pSoundInstance = GetLinkSampleSoundInstance(m_pSWSampleList[nSample].m_hSample);
			if (pSoundInstance)
			{
				pSoundInstance->Silence(LTTRUE);
			}

			AIL_lock();
			AIL_release_sample_handle(m_pSWSampleList[nSample].m_hSample);
			AIL_unlock();
		}

		delete[] m_pSWSampleList;
		m_pSWSampleList = LTNULL;
		m_nNumSWSamples = 0;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00493870
CSoundBuffer *CSoundMgr::CreateBuffer(FileIdentifier &fileIdent)
{
	CSoundBuffer *pSoundBuffer;

	if (!m_bValid)
	{
		return LTNULL;
	}

	// Check if buffer already exists
	if (fileIdent.m_pData)
	{
		// Make sure it's a sound
		if ((fileIdent.m_TypeCode == TYPECODE_SOUND) || (fileIdent.m_TypeCode == TYPECODE_UNKNOWN))
		{
			pSoundBuffer = (CSoundBuffer *)fileIdent.m_pData;
			pSoundBuffer->SetTouched(LTTRUE);
			return pSoundBuffer;
		}
		else
		{
			return LTNULL;
		}
	}

	// Create a new sound buffer
	pSoundBuffer = m_SoundBufferBank.Allocate();
	if (!pSoundBuffer)
	{
		return LTNULL;
	}

	if (pSoundBuffer->Init(fileIdent) != LT_OK)
	{
		m_SoundBufferBank.Free(pSoundBuffer);
		return LTNULL;
	}

	dl_AddTail(&m_SoundBufferList, (LTLink *)pSoundBuffer->GetLink(), pSoundBuffer);

	return pSoundBuffer;
}

// FUNCTION: LITHTECH 0x00493950
LTRESULT CSoundMgr::RemoveBuffer(CSoundBuffer &soundBuffer)
{
	LTLink *pCur;
	CSample *pSample;

	dl_RemoveAt(&m_SoundBufferList, (LTLink *)soundBuffer.GetLink());

	if ((soundBuffer.GetSoundBufferFlags() & SOUNDBUFFERFLAG_STREAM) == 0)
	{
		// Remove any sample-buffer links
		pCur = m_3DFreeSampleList.m_Head.m_pNext;
		while (pCur != &m_3DFreeSampleList.m_Head)
		{
			pSample = (CSample *)pCur->m_pData;
			pCur = pCur->m_pNext;

			if (!pSample || !pSample->m_h3DSample)
				continue;

			if ((CSoundBuffer *)AIL_3D_user_data(pSample->m_h3DSample, SAMPLE_BUFFER) == &soundBuffer)
				AIL_set_3D_user_data(pSample->m_h3DSample, SAMPLE_BUFFER, 0);
		}
	}

	m_SoundBufferBank.Free(&soundBuffer);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00493a00
LTRESULT CSoundMgr::RemoveInstance(CSoundInstance &soundInstance)
{
	uint32 dwIndex;
	SoundType eSoundType;

	eSoundType = soundInstance.GetType();

	dwIndex = soundInstance.GetListIndex();
	if (m_dwNumSoundInstances > 1 && dwIndex < m_dwNumSoundInstances - 1)
	{
		m_SoundInstanceList[dwIndex] = m_SoundInstanceList[m_dwNumSoundInstances-1];
		m_SoundInstanceList[dwIndex]->SetListIndex(dwIndex);
	}
	if (m_dwNumSoundInstances)
	{
		m_SoundInstanceList[m_dwNumSoundInstances-1] = LTNULL;
		m_dwNumSoundInstances--;
	}
	soundInstance.Term();

	if (eSoundType == SOUNDTYPE_LOCAL)
	{
		m_LocalSoundInstanceBank.Free((CLocalSoundInstance *)&soundInstance);
	}
	else if (eSoundType == SOUNDTYPE_AMBIENT)
	{
		m_AmbientSoundInstanceBank.Free((CAmbientSoundInstance *)&soundInstance);
	}
	else if (eSoundType == SOUNDTYPE_3D)
	{
		m_3DSoundInstanceBank.Free((C3DSoundInstance *)&soundInstance);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00493ab0
LTRESULT CSoundMgr::StopAllSounds()
{
	uint32 nIndex;
	CSoundInstance *pSoundInstance;

	for (nIndex = 0; nIndex < m_dwNumSoundInstances; nIndex++)
	{
		pSoundInstance = m_SoundInstanceList[nIndex];
		if (!pSoundInstance)
			continue;

		pSoundInstance->Stop(LTTRUE);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00493af0
CSoundInstance *CSoundMgr::FindSoundInstance(HLTSOUND hSound, LTBOOL bClientSound)
{
	CSoundInstance *pSoundInstance;
	uint32 dwIndex;

	if (bClientSound)
	{
		if (!hSound)
			return LTNULL;

		pSoundInstance = (CSoundInstance *)hSound;
		if (pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_CLIENT)
			return pSoundInstance;
	}
	else
	{
		if (hSound == (HLTSOUND)INVALID_OBJECTID)
			return LTNULL;

		// Look for sound handle in sound instance list
		for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
		{
			pSoundInstance = m_SoundInstanceList[dwIndex];
			if (!pSoundInstance)
				continue;

			if (pSoundInstance->GetHSoundDE() == hSound)
				if (!(pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_CLIENT))
					return pSoundInstance;
		}
	}

	// Didn't find it
	return LTNULL;
}

// The flag clear after the done-sound write goes through Set/GetPlaySoundFlags: those two pending inline sites keep
// WriteType's CMoArray::Insert2 out of line, as in the original.
// FUNCTION: LITHTECH 0x00493b50
LTRESULT CSoundMgr::Update()
{
	uint32 dwIndex, dwSearchIndex;
	CSoundInstance *pSoundInstance, *pSearchSoundInstance;

	HSAMPLE hSample;

	LTObject *pClientObject = LTNULL;
	LTVector vDeltaPos;
	LTVector vVelocity;
	uint32 dwFrameTime, dwCurTime;
	LTVector vTemp, vTemp2;

	if (!m_bValid || !m_bEnabled)
	{
		return LT_ERROR;
	}

	// Sometimes it takes a while to get the digital handle back
	if (m_bDigitalHandleReleased && m_bReacquireDigitalHandle)
		ReacquireDigitalHandle();

	// Clear the sound update packet so we start fresh.
	m_SoundUpdatePacket.m_Pos = 1;
	m_SoundUpdatePacket.m_DataLen = 1;

	// Update the timer
	AIL_lock();
	dwCurTime = AIL_ms_count();
	AIL_unlock();

	dwFrameTime = dwCurTime - m_dwCurTime;

	// See if it's time to commit all the sound changes
	if (dwCurTime > m_dwCommitTime)
	{
		m_bCommitChanges = LTTRUE;
		m_dwCommitTime = dwCurTime + COMMITTIME;
	}

	// If the listener is inside the client, then use the client object's position and orientation...
	if (m_bListenerInClient)
	{
		if (g_pClientMgr->m_pCurShell)
			pClientObject = g_pClientMgr->m_pCurShell->m_pFrameClientObject;
		if (pClientObject)
		{
			m_vListenerPosition = pClientObject->GetPos();
			quat_GetVectors((float *)&pClientObject->m_Rotation,
				(float*)&m_vListenerRight, (float*)&m_vListenerUp, (float*)&m_vListenerForward);
		}
		else
		{
			m_vListenerPosition.Init();
			m_vListenerRight.Init(1.0f, 0.0f, 0.0f);
			m_vListenerUp.Init(0.0f, 1.0f, 0.0f);
			m_vListenerForward.Init(0.0f, 0.0f, 1.0f);
		}
	}

	// Update listener position and velocity
	if (dwFrameTime > 0)
	{
		m_vListenerVelocity = m_vListenerPosition - m_vLastListenerPosition;
		m_vListenerVelocity *= m_fDistanceFactor / (float)dwFrameTime;
	}
	else
	{
		m_vListenerVelocity.Init();
	}
	m_vLastListenerPosition = m_vListenerPosition;

	// Set the listener orientation
	if (m_h3DListener && m_bCommitChanges)
	{
		AIL_lock();
		AIL_3D_orientation(m_h3DListener, &vTemp.x, &vTemp.y, &vTemp.z, &vTemp2.x, &vTemp2.y, &vTemp2.z);
		if (vTemp.DistSqr(m_vListenerForward) > 0.001f || vTemp2.DistSqr(m_vListenerUp) > 0.001f)
			AIL_set_3D_orientation(m_h3DListener, m_vListenerForward.x, m_vListenerForward.y, m_vListenerForward.z,
				m_vListenerUp.x, m_vListenerUp.y, m_vListenerUp.z);
		AIL_unlock();
	}

	// Start off with no sounds heard
	m_nNumSoundsHeard = 0;
	// Check if no sounds are waiting
	if (!m_dwNumSoundInstances)
		return LT_OK;

	// Preupdate the sounds
	for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
	{
		pSoundInstance = m_SoundInstanceList[dwIndex];
		if (!pSoundInstance)
			continue;

		if (pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_DONE)
			continue;

		pSoundInstance->Preupdate(m_vListenerPosition);
		// Check if sound is within ear shot...
		if (!(pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_EARSHOT))
		{
			pSoundInstance->Silence();
		}
	}

	if (m_dwNumSoundInstances > 1)
	{
		qsort(m_SoundInstanceList, m_dwNumSoundInstances, sizeof(CSoundInstance *), CompareSoundInstances);

		// Reset the indices
		for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
		{
			pSoundInstance = m_SoundInstanceList[dwIndex];
			if (!pSoundInstance)
			{
				m_dwNumSoundInstances = dwIndex;
				break;
			}

			// The sort messes up the indices, so this needs to happen
			pSoundInstance->SetListIndex(dwIndex);
		}
	}

	// Give sounds channel samples based on priority
	for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
	{
		pSoundInstance = m_SoundInstanceList[dwIndex];

		if (pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_DONE)
			continue;

		if (!(pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_EARSHOT))
			continue;

		// Check if sound already has a sample channel
		if (pSoundInstance->GetSample() || pSoundInstance->Get3DSample() || pSoundInstance->GetStream())
			continue;

		if (!pSoundInstance->GetSoundBuffer())
			continue;

		// Check if there's an initial delay.
		if (pSoundInstance->GetTimer() > pSoundInstance->GetDuration())
			continue;

		// Make sure there aren't other instances of the same buffer playing at the same start time.
		if (!pSoundInstance->GetSoundBuffer()->CanPlay(*pSoundInstance))
			continue;

		// Handle streaming sounds
		if (pSoundInstance->GetSoundBuffer()->GetSoundBufferFlags() & SOUNDBUFFERFLAG_STREAM)
		{
			pSoundInstance->AcquireStream();
			continue;
		}

		// Check if sample needs reverb or is 3d.  Filtered sounds need a 2d sample.
		if (m_nMax3DSamples && (pSoundInstance->GetType() == SOUNDTYPE_3D ||
			(m_b3DReverb && pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_REVERB)) &&
			!pSoundInstance->HasFilter())
		{
			// Try to get a free 3d sample
			if (pSoundInstance->Acquire3DSample() == LT_OK)
				continue;
		}
		else if (pSoundInstance->AcquireSample() == LT_OK)
			continue;

		// Find the lowest priority sound with a sample and snag it
		for (dwSearchIndex = m_dwNumSoundInstances - 1; dwSearchIndex > dwIndex; dwSearchIndex--)
		{
			pSearchSoundInstance = m_SoundInstanceList[dwSearchIndex];

			if (m_nMax3DSamples && (pSoundInstance->GetType() == SOUNDTYPE_3D ||
				(m_b3DReverb && pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_REVERB)) &&
				!pSoundInstance->HasFilter())
			{
				// Check if it has a 3d sample
				if (pSearchSoundInstance->Get3DSample())
				{
					// Check if new sound is non-streaming 3d sound
					if (pSoundInstance->GetType() == SOUNDTYPE_3D && !(pSoundInstance->GetSoundBuffer()->GetSoundBufferFlags() & SOUNDBUFFERFLAG_STREAM))
					{
						pSearchSoundInstance->Silence();
						pSoundInstance->Acquire3DSample();
						break;
					}
				}
			}
			// Check if it has a regular sample
			else if ((hSample = pSearchSoundInstance->GetSample()) != LTNULL)
			{
				pSearchSoundInstance->Silence();
				pSoundInstance->AcquireSample();
				break;
			}
		}
	}

	// Update the instances
	for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
	{
		pSoundInstance = m_SoundInstanceList[dwIndex];

		// Only update the output on sounds with channel samples
		if (pSoundInstance->Get3DSample() || pSoundInstance->GetSample() || pSoundInstance->GetStream())
		{
			m_nNumSoundsHeard++;
			pSoundInstance->UpdateOutput(dwFrameTime);
		}

		// If the sound has finished playing, it can be removed if it doesn't have a handle...
		if (!pSoundInstance->UpdateTimer(dwFrameTime))
		{
			pSoundInstance->Stop();

			// If the sound doesn't have a handle, or isn't being controlled by the server, then we can remove it.
			// Otherwise, the user will remove it, or the server will.
			if ((pSoundInstance->GetSoundInstanceFlags() & (SOUNDINSTANCEFLAG_ENDLOOP | SOUNDINSTANCEFLAG_FADE)) ||
				!(pSoundInstance->GetPlaySoundFlags() & (PLAYSOUND_GETHANDLE | PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED)))
			{
				RemoveInstance(*pSoundInstance);
				dwIndex--;
			}
			// If the server needs to know when this sound is done, then tell it, but keep the instance around
			else if (!(pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_CLIENT) &&
				(pSoundInstance->GetPlaySoundFlags() & (PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED)) &&
				!(pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_LOOP) &&
				pSoundInstance->GetHSoundDE() != (HLTSOUND)INVALID_OBJECTID)
			{
				m_SoundUpdatePacket.WriteType((uint16)pSoundInstance->GetHSoundDE());
				pSoundInstance->SetPlaySoundFlags(pSoundInstance->GetPlaySoundFlags() & ~(PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED));
			}
		}
	}

	// Update the timer
	m_dwCurTime = dwCurTime;
	m_bCommitChanges = LTFALSE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004942f0
int CSoundMgr::CompareSoundInstances(const void *pElem1, const void *pElem2)
{
	CSoundInstance *pSoundInstance1, *pSoundInstance2;
	int nTimePlaying1, nTimePlaying2;

	pSoundInstance1 = *(CSoundInstance **)pElem1;
	pSoundInstance2 = *(CSoundInstance **)pElem2;

	// Null pointers are considered low order
	if (pSoundInstance1)
	{
		if (!pSoundInstance2)
			return -1;
	}
	else if (pSoundInstance2)
		return 1;
	else
		return 0;

	// Local sounds have highest priority of all, and go before all non-local sounds in list
	if (pSoundInstance1->GetType() == SOUNDTYPE_LOCAL)
	{
		if (pSoundInstance2->GetType() != SOUNDTYPE_LOCAL)
			return -1;
	}
	else if (pSoundInstance2->GetType() == SOUNDTYPE_LOCAL)
		return 1;

	// If sound has higher priority, then insert it before the test sound
	if (pSoundInstance1->GetModifiedPriority() > pSoundInstance2->GetModifiedPriority())
		return -1;

	// Check if sounds have equal priority
	if (pSoundInstance1->GetModifiedPriority() == pSoundInstance2->GetModifiedPriority())
	{
		// Playing sounds have a slightly higher priority than non-playing
		if (pSoundInstance1->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_PLAYING)
		{
			if (!(pSoundInstance2->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_PLAYING))
				return -1;
		}
		else if (pSoundInstance2->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_PLAYING)
			return 1;

		if (!pSoundInstance1->GetSoundBuffer() || !pSoundInstance2->GetSoundBuffer())
			return 0;

		// Get the amount of time the sound has been alive.  If there is a pre-delay, then it has low priority
		nTimePlaying1 = pSoundInstance1->GetDuration() - pSoundInstance1->GetTimer();
		if (nTimePlaying1 < 0)
			return 1;

		// Get the amount of time the sound has been alive.  If there is a pre-delay, then it has low priority
		nTimePlaying2 = pSoundInstance2->GetDuration() - pSoundInstance2->GetTimer();
		if (nTimePlaying2 < 0)
			return -1;

		// Neither sound is playing.  Give priority to newest sound.
		if (nTimePlaying1 < nTimePlaying2)
			return -1;
		else if (nTimePlaying1 > nTimePlaying2)
			return 1;
		else
			return 0;
	}

	return 1;
}

// FUNCTION: LITHTECH 0x004943d0
LTRESULT CSoundMgr::SetVolume(uint16 nVolume)
{
	if (!m_bValid || !m_hDigDriver)
		return LT_ERROR;

	if (nVolume > 100)
		nVolume = 100;

	AIL_lock();
	AIL_set_digital_master_volume(m_hDigDriver, (uint16)((float)nVolume * 1.27f));
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494440
LTRESULT CSoundMgr::GetVolume(uint16 &nVolume)
{
	if (!m_bValid || !m_hDigDriver)
	{
		nVolume = 100;
		return LT_OK;
	}

	AIL_lock();
	nVolume = (uint16)((float)AIL_digital_master_volume(m_hDigDriver) * (100.0f / 127.0f));
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004944b0
LTRESULT CSoundMgr::SetVolumeByType(uint16 nVolume, uint8 nSoundType)
{
	if (nVolume > 100)
		nVolume = 100;

	m_SoundTypeVolumes.SetVolume(nSoundType, (uint8)nVolume);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004944e0
LTRESULT CSoundMgr::GetVolumeByType(uint16 &nVolume, uint8 nSoundType)
{
	nVolume = m_SoundTypeVolumes.GetVolume(nSoundType);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494500 ?PlaySoundA@CSoundMgr@@QAEKAAUPlaySoundInfo@@AAUFileIdentifier@@K@Z
LTRESULT CSoundMgr::PlaySound(PlaySoundInfo &playSoundInfo, FileIdentifier &fileIdent, uint32 dwOffsetTime)
{
	CSoundBuffer *pSoundBuffer;
	CSoundInstance *pSoundInstance, *pOldSoundInstance;
	SoundType eType;
	LTRESULT nError;

	// If client side sound, then initialize the handle just in case it fails
	if (playSoundInfo.m_dwFlags & PLAYSOUND_CLIENT)
		playSoundInfo.m_hSound = LTNULL;

	// Make sure it was initialized
	if (!m_bValid)
	{
		return LT_ERROR;
	}

	// Check if turned off
	if (!m_bEnabled)
	{
		return LT_OK;
	}

	// Get the sound buffer for this file
	pSoundBuffer = CreateBuffer(fileIdent);
	if (!pSoundBuffer)
	{
		return LT_ERROR;
	}

	// Play sound in the player's head, if global local sound, or client local sound and listener is in client...
	if (((playSoundInfo.m_dwFlags & PLAYSOUND_CLIENTLOCAL) && m_bListenerInClient) ||
		(!(playSoundInfo.m_dwFlags & PLAYSOUND_AMBIENT) && !(playSoundInfo.m_dwFlags & PLAYSOUND_3D)))
	{
		pSoundInstance = m_LocalSoundInstanceBank.Allocate();
	}
	else if (playSoundInfo.m_dwFlags & PLAYSOUND_AMBIENT)
	{
		pSoundInstance = m_AmbientSoundInstanceBank.Allocate();
	}
	else
	{
		pSoundInstance = m_3DSoundInstanceBank.Allocate();
	}

	if (!pSoundInstance)
	{
		return LT_ERROR;
	}

	// Initialize the sound
	nError = pSoundInstance->Init(*pSoundBuffer, playSoundInfo, dwOffsetTime);

	if (nError == LT_OK)
	{
		// Make sure there is room
		if (m_dwNumSoundInstances < SOUNDMGR_MAXSOUNDINSTANCES)
		{
			m_SoundInstanceList[m_dwNumSoundInstances] = pSoundInstance;
			pSoundInstance->SetListIndex(m_dwNumSoundInstances);
			m_dwNumSoundInstances++;
		}
		else
			nError = LT_ERROR;
	}

	if (nError != LT_OK)
	{
		eType = pSoundInstance->GetType();
		pSoundInstance->Term();
		if (eType == SOUNDTYPE_LOCAL)
			m_LocalSoundInstanceBank.Free((CLocalSoundInstance *)pSoundInstance);
		else if (eType == SOUNDTYPE_AMBIENT)
			m_AmbientSoundInstanceBank.Free((CAmbientSoundInstance *)pSoundInstance);
		else if (eType == SOUNDTYPE_3D)
			m_3DSoundInstanceBank.Free((C3DSoundInstance *)pSoundInstance);

		playSoundInfo.m_hSound = LTNULL;
	}
	else
	{
		// Don't let one buffer hog the instances.
		if (pSoundBuffer->GetInstanceList()->m_nElements > g_dwMaxInstancesPerBuffer)
		{
			pOldSoundInstance = pSoundBuffer->GetLowestPriorityInstance();
			if (pOldSoundInstance)
				RemoveInstance(*pOldSoundInstance);
		}
	}

	return nError;
}

// FUNCTION: LITHTECH 0x00494710
LTRESULT CSoundMgr::LinkSampleSoundInstance(HSAMPLE hSample, CSoundInstance *pSoundInstance)
{
	if (!hSample)
		return LT_ERROR;

	AIL_lock();
	AIL_set_sample_user_data(hSample, SAMPLE_INSTANCE, (S32)pSoundInstance);
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494750
CSoundInstance *CSoundMgr::GetLinkSampleSoundInstance(HSAMPLE hSample)
{
	CSoundInstance *pSoundInstance;

	if (!hSample)
		return LTNULL;

	AIL_lock();
	pSoundInstance = (CSoundInstance *)AIL_sample_user_data(hSample, SAMPLE_INSTANCE);
	AIL_unlock();

	return pSoundInstance;
}

// FUNCTION: LITHTECH 0x00494780
LTRESULT CSoundMgr::Link3DSampleSoundInstance(H3DSAMPLE h3DSample, CSoundInstance *pSoundInstance)
{
	if (!h3DSample)
		return LT_ERROR;

	AIL_lock();
	AIL_set_3D_user_data(h3DSample, SAMPLE_INSTANCE, (S32)pSoundInstance);
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004947c0
CSoundInstance *CSoundMgr::GetLink3DSampleSoundInstance(H3DSAMPLE h3DSample)
{
	CSoundInstance *pSoundInstance;

	if (!h3DSample)
		return LTNULL;

	AIL_lock();
	pSoundInstance = (CSoundInstance *)AIL_3D_user_data(h3DSample, SAMPLE_INSTANCE);
	AIL_unlock();

	return pSoundInstance;
}

// FUNCTION: LITHTECH 0x004947f0
LTRESULT CSoundMgr::LinkStreamSoundInstance(HSTREAM hStream, CSoundInstance *pSoundInstance)
{
	if (!hStream)
		return LT_ERROR;

	AIL_lock();
	AIL_set_stream_user_data(hStream, SAMPLE_INSTANCE, (S32)pSoundInstance);
	AIL_unlock();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494830
HSAMPLE CSoundMgr::GetFreeSWSample()
{
	LTLink *pLink;
	CSample *pSample;

	if (m_SWFreeSampleList.m_nElements > 0)
	{
		pLink = m_SWFreeSampleList.m_Head.m_pNext;
		dl_RemoveAt(&m_SWFreeSampleList, pLink);
		dl_TieOff(pLink);
		pSample = (CSample *)pLink->m_pData;

		return pSample->m_hSample;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00494880
H3DSAMPLE CSoundMgr::GetFree3DSample(CSoundInstance *pSoundInstance)
{
	LTLink *pLink;
	CSample *pSample, *pTestSample;
	LTVector vTestPos, vPos;
	H3DSAMPLE h3DSample;
	CSoundBuffer *pSoundBuffer;

	pSample = LTNULL;
	h3DSample = LTNULL;

	// If there is more than one element, search the list
	if (m_3DFreeSampleList.m_nElements > 0)
	{
		// Look through the free samples and find one that matches our new sound the best.
		pSoundInstance->Get3DSamplePosition(vPos);
		pLink = m_3DFreeSampleList.m_Head.m_pPrev;
		while (pLink != &m_3DFreeSampleList.m_Head)
		{
			pTestSample = (CSample *)pLink->m_pData;

			if (pTestSample)
			{
				pSoundBuffer = (CSoundBuffer *)AIL_3D_user_data(pTestSample->m_h3DSample, SAMPLE_BUFFER);

				// Check if this sample used the same soundbuffer that we need
				if (pSoundBuffer == pSoundInstance->GetSoundBuffer())
				{
					// This is the best so far.
					pSample = pTestSample;

					// Check if the position is the same, if so, we can't get any better.
					AIL_3D_position(pSample->m_h3DSample, &vTestPos.x, &vTestPos.y, &vTestPos.z);

					if (vPos.DistSqr(vTestPos) < 0.001f)
					{
						break;
					}
				}
			}

			pLink = pLink->m_pPrev;
		}

		// If we didn't find one we like, then just choose the head.
		if (!pSample && m_3DFreeSampleList.m_Head.m_pNext)
		{
			pSample = (CSample *)m_3DFreeSampleList.m_Head.m_pNext->m_pData;
		}
	}

	// Remove it from the free list.
	if (pSample)
	{
		dl_RemoveAt(&m_3DFreeSampleList, &pSample->m_Link);
		dl_TieOff(&pSample->m_Link);
		h3DSample = pSample->m_h3DSample;
	}

	// If there is only one free sample left, then make sure it has had end called on it.
	if (m_3DFreeSampleList.m_nElements == 1 && m_3DFreeSampleList.m_Head.m_pNext)
	{
		pSample = (CSample *)m_3DFreeSampleList.m_Head.m_pNext->m_pData;
		if (pSample)
		{
			AIL_end_3D_sample(pSample->m_h3DSample);
		}
	}

	return h3DSample;
}

// FUNCTION: LITHTECH 0x004949c0
void CSoundMgr::ReleaseSWSample(HSAMPLE hSample)
{
	CSample *pSample;

	AIL_lock();
	pSample = (CSample *)AIL_sample_user_data(hSample, SAMPLE_LISTITEM);
	AIL_unlock();

	if (pSample)
		dl_AddTail(&m_SWFreeSampleList, &pSample->m_Link, pSample);
}

// FUNCTION: LITHTECH 0x00494a10
void CSoundMgr::Release3DSample(H3DSAMPLE h3DSample)
{
	CSample *pSample;

	AIL_lock();
	pSample = (CSample *)AIL_3D_user_data(h3DSample, SAMPLE_LISTITEM);
	AIL_unlock();

	if (pSample)
		dl_AddTail(&m_3DFreeSampleList, &pSample->m_Link, pSample);
}

// FUNCTION: LITHTECH 0x00494a60
LTRESULT CSoundMgr::UntagAllSoundBuffers()
{
	LTLink *pCur, *pNext;

	if (!m_bValid)
		return LT_ERROR;

	// Untag the sounds...
	pCur = m_SoundBufferList.m_Head.m_pNext;
	while (pCur != &m_SoundBufferList.m_Head)
	{
		pNext = pCur->m_pNext;
		((CSoundBuffer *)pCur->m_pData)->SetTouched(LTFALSE);
		pCur = pNext;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494aa0
LTRESULT CSoundMgr::RemoveAllUntaggedSoundBuffers()
{
	CSoundBuffer *pSoundBuffer;
	CSoundInstance *pSoundInstance;
	LTLink *pCurSound;
	int32 nIndex;

	if (!m_bValid)
		return LT_ERROR;

	// Remove all sounds except ones with handles
	for (nIndex = 0; nIndex < (int32)m_dwNumSoundInstances; nIndex++)
	{
		pSoundInstance = m_SoundInstanceList[nIndex];
		if (!pSoundInstance)
			continue;

		if (pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_GETHANDLE)
			continue;

		RemoveInstance(*pSoundInstance);
		nIndex--;
	}

	// Remove unused sound buffers
	pCurSound = m_SoundBufferList.m_Head.m_pNext;
	while (pCurSound != &m_SoundBufferList.m_Head)
	{
		pSoundBuffer = (CSoundBuffer *)pCurSound->m_pData;
		pCurSound = pCurSound->m_pNext;

		if (!pSoundBuffer)
			continue;

		// Check if not tagged
		if (!pSoundBuffer->IsTouched())
		{
			// Check if there are any sound instances using the buffer
			if (pSoundBuffer->GetInstanceList()->m_nElements == 0)
			{
				RemoveBuffer(*pSoundBuffer);
			}
		}
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494b30 ?SetListener@CSoundMgr@@QAEKIPAV?$_CVector@M@@00I@Z
LTRESULT CSoundMgr::SetListener(LTBOOL bListenerInClient, LTVector *pvListenerPos, LTVector *pvListenerForward, LTVector *pvListenerRight, LTBOOL bTeleport)
{
	m_bListenerInClient = bListenerInClient;

	if (!m_bValid || !m_bEnabled)
		return LT_ERROR;

	if (!m_bListenerInClient)
	{
		if (pvListenerPos)
		{
			// Update listener position and velocity
			m_vListenerPosition = *pvListenerPos;
			if (bTeleport)
			{
				m_vLastListenerPosition = *pvListenerPos;
			}
		}

		if (pvListenerForward)
		{
			m_vListenerForward = *pvListenerForward;
		}
		if (pvListenerRight)
		{
			m_vListenerRight = *pvListenerRight;
		}
		if (pvListenerForward && pvListenerRight)
		{
			m_vListenerUp = pvListenerForward->Cross(*pvListenerRight);
		}
	}
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494c70
LTRESULT CSoundMgr::ReleaseDigitalHandle()
{
	if (!m_hDigDriver)
		return LT_ERROR;

	if (!m_3DProvider.m_hProvider)
	{
		if (!m_bDigitalHandleReleased)
		{
			AIL_lock();
			if (!AIL_digital_handle_release(m_hDigDriver))
			{
				AIL_unlock();
				return LT_ERROR;
			}
			AIL_unlock();

			m_bDigitalHandleReleased = LTTRUE;
			m_bReacquireDigitalHandle = LTFALSE;
		}
	}
	else
	{
		PauseSounds();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494cf0
LTRESULT CSoundMgr::ReacquireDigitalHandle()
{
	if (!m_3DProvider.m_hProvider)
	{
		if (m_bDigitalHandleReleased)
		{
			if (!m_hDigDriver)
				return LT_ERROR;

			AIL_lock();
			if (!AIL_digital_handle_reacquire(m_hDigDriver))
			{
				AIL_unlock();
				m_bReacquireDigitalHandle = LTTRUE;
				return LT_ERROR;
			}
			AIL_unlock();

			m_bDigitalHandleReleased = LTFALSE;
		}
	}
	else
	{
		ResumeSounds();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494d70
LTRESULT CSoundMgr::PauseSounds()
{
	uint32 dwIndex;
	CSoundInstance *pSoundInstance;

	if (!m_bValid)
		return LT_OK;

	for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
	{
		pSoundInstance = m_SoundInstanceList[dwIndex];
		if (!pSoundInstance)
			continue;

		pSoundInstance->Pause();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494db0
LTRESULT CSoundMgr::ResumeSounds()
{
	uint32 dwIndex;
	CSoundInstance *pSoundInstance;

	if (!m_bValid)
		return LT_OK;

	for (dwIndex = 0; dwIndex < m_dwNumSoundInstances; dwIndex++)
	{
		pSoundInstance = m_SoundInstanceList[dwIndex];
		if (!pSoundInstance)
			continue;

		pSoundInstance->Resume();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00494df0
LTRESULT CSoundMgr::SetReverbProperties(ReverbProperties *pReverbProperties)
{
	LTBOOL bChange;

	if (!pReverbProperties)
		RETURN_ERROR(1, CSoundMgr::SetReverbProperties, LT_INVALIDPARAMS);

	if (!m_bValid || (!m_bSWReverb && !m_b3DReverb))
		return LT_ERROR;

	bChange = LTFALSE;
	if (pReverbProperties->m_dwParams & REVERBPARAM_ACOUSTICS)
	{
		// Get new values and make sure they are in range
		if (pReverbProperties->m_dwAcoustics >= REVERB_ACOUSTICS_COUNT)
			pReverbProperties->m_dwAcoustics = REVERB_ACOUSTICS_GENERIC;

		if (m_dwReverbAcoustics != pReverbProperties->m_dwAcoustics)
		{
			bChange = LTTRUE;
			m_dwReverbAcoustics = pReverbProperties->m_dwAcoustics;
		}
	}

	if (pReverbProperties->m_dwParams & REVERBPARAM_VOLUME)
	{
		pReverbProperties->m_fVolume = LTCLAMP(pReverbProperties->m_fVolume, 0.0f, 1.0f);

		if (m_fReverbVolume != pReverbProperties->m_fVolume)
		{
			bChange = LTTRUE;
			m_fReverbVolume = pReverbProperties->m_fVolume;
		}
	}

	if (pReverbProperties->m_dwParams & REVERBPARAM_REFLECTTIME)
		m_fReverbReflectTime = LTCLAMP(pReverbProperties->m_fReflectTime, 0.0f, 5.0f);
	if (pReverbProperties->m_dwParams & REVERBPARAM_DECAYTIME)
	{
		pReverbProperties->m_fDecayTime = LTCLAMP(pReverbProperties->m_fDecayTime, 0.001f, 20.0f);

		if (m_fReverbDecayTime != pReverbProperties->m_fDecayTime)
		{
			bChange = LTTRUE;
			m_fReverbDecayTime = pReverbProperties->m_fDecayTime;
		}
	}
	if (pReverbProperties->m_dwParams & REVERBPARAM_DAMPING)
	{
		pReverbProperties->m_fDamping = LTCLAMP(pReverbProperties->m_fDamping, 0.0f, 2.0f);
		if (m_fReverbDamping != pReverbProperties->m_fDamping)
		{
			bChange = LTTRUE;
			m_fReverbDamping = pReverbProperties->m_fDamping;
		}
	}

	if (m_b3DReverb && bChange)
	{
		AIL_lock();
		AIL_set_3D_room_type(m_3DProvider.m_hProvider, m_dwReverbAcoustics);
		AIL_unlock();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495010
LTRESULT CSoundMgr::GetReverbProperties(ReverbProperties *pReverbProperties)
{
	if (!pReverbProperties)
		RETURN_ERROR(1, CSoundMgr::GetReverbProperties, LT_INVALIDPARAMS);

	if (!m_bValid || (!m_bSWReverb && !m_b3DReverb))
		return LT_ERROR;

	if (pReverbProperties->m_dwParams & REVERBPARAM_ACOUSTICS)
		pReverbProperties->m_dwAcoustics = m_dwReverbAcoustics;
	if (pReverbProperties->m_dwParams & REVERBPARAM_VOLUME)
		pReverbProperties->m_fVolume = m_fReverbVolume;
	if (pReverbProperties->m_dwParams & REVERBPARAM_REFLECTTIME)
		pReverbProperties->m_fReflectTime = m_fReverbReflectTime;
	if (pReverbProperties->m_dwParams & REVERBPARAM_DECAYTIME)
		pReverbProperties->m_fDecayTime = m_fReverbDecayTime;
	if (pReverbProperties->m_dwParams & REVERBPARAM_DECAYTIME)
		pReverbProperties->m_fDamping = m_fReverbDamping;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004950d0 ?PlaySoundA@CSoundMgr@@UAEKPAUPlaySoundInfo@@AAPAUHLTSOUND_t@@@Z
LTRESULT CSoundMgr::PlaySound(PlaySoundInfo *pPlaySoundInfo, HLTSOUND &hResult)
{
	FileRef playSoundFileRef;
	FileIdentifier *pIdent;
	LTRESULT playResult;

	hResult = LTNULL;

	if (!pPlaySoundInfo)
		return LT_INVALIDPARAMS;

	if (!IsValid() || !IsEnabled())
		return LT_ERROR;

	if (!g_pClientMgr)
		return LT_ERROR;

	// These options don't work for client only sounds...
	pPlaySoundInfo->m_dwFlags &= ~PLAYSOUND_TIME & ~PLAYSOUND_ATTACHED & ~PLAYSOUND_TIMESYNC;

	// Make sure the client flag is set...
	pPlaySoundInfo->m_dwFlags |= PLAYSOUND_CLIENT;

	playSoundFileRef.m_pFilename = pPlaySoundInfo->m_szSoundName;
	playSoundFileRef.m_FileType = FILE_CLIENTFILE;

	pIdent = cf_GetFileIdentifier(g_pClientMgr->m_hFileMgr, &playSoundFileRef, TYPECODE_SOUND);
	if (!pIdent)
	{
		if (g_DebugLevel >= 2)
		{
			dsi_ConsolePrint("Missing sound file %s", cf_GetFilename(g_pClientMgr->m_hFileMgr, &playSoundFileRef));
		}

		return LT_ERROR;
	}

	playResult = PlaySound(*pPlaySoundInfo, *pIdent, 0);
	if (playResult == LT_OK)
		hResult = (HLTSOUND)m_SoundInstanceList[m_dwNumSoundInstances - 1];

	return playResult;
}

// FUNCTION: LITHTECH 0x004951d0
LTRESULT CSoundMgr::GetSoundDuration(HLTSOUND hSound, LTFLOAT &fDuration)
{
	CSoundInstance *pSoundInstance;

	if (!IsValid())
	{
		fDuration = 0;
		return LT_OK;
	}

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::GetSoundDuration, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::GetSoundDuration, LT_INVALIDPARAMS);

	if (!pSoundInstance->GetSoundBuffer())
		RETURN_ERROR(1, CSoundMgr::GetSoundDuration, LT_INVALIDPARAMS);

	fDuration = pSoundInstance->GetSoundBuffer()->GetDuration() / 1000.0f;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004952f0
LTRESULT CSoundMgr::IsSoundDone(HLTSOUND hSound, LTBOOL &bDone)
{
	CSoundInstance *pSoundInstance;

	bDone = LTTRUE;

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if ((pSoundInstance) && ((pSoundInstance->GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_DONE) == 0))
		bDone = LTFALSE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495330
LTRESULT CSoundMgr::KillSound(HLTSOUND hSound)
{
	CSoundInstance *pSoundInstance;

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		return LT_INVALIDPARAMS;

	RemoveInstance(*pSoundInstance);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495360
LTRESULT CSoundMgr::KillSoundLoop(HLTSOUND hSound)
{
	CSoundInstance *pSoundInstance;

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		return LT_INVALIDPARAMS;

	pSoundInstance->EndLoop();

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495390
LTRESULT CSoundMgr::KillSoundFade(HLTSOUND hSound, LTFLOAT fFadeOutTime)
{
	CSoundInstance *pSoundInstance;

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		return LT_INVALIDPARAMS;

	pSoundInstance->FadeOut(fFadeOutTime);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004953c0
LTRESULT CSoundMgr::GetSound3DProviderLists(Sound3DProvider *&pSound3DProviderList, LTBOOL bVerify)
{
	CProvider *pProvider, *p3DProviderList;
	Sound3DProvider *pSound3DProvider;

	pSound3DProviderList = LTNULL;
	if (Get3DProviderLists(p3DProviderList, bVerify) != LT_OK)
		return LT_ERROR;

	pProvider = p3DProviderList;
	while (pProvider)
	{
		pSound3DProvider = new Sound3DProvider;
		pSound3DProvider->m_pNextProvider = pSound3DProviderList;
		pSound3DProviderList = pSound3DProvider;
		LTStrCpy(pSound3DProvider->m_szProvider, pProvider->m_szProviderName, sizeof(pSound3DProvider->m_szProvider));
		pSound3DProvider->m_dwCaps = pProvider->m_dwCaps;
		pSound3DProvider->m_dwProviderID = pProvider->m_dwProviderID;
		pProvider = pProvider->m_pNextProvider;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495450
LTRESULT CSoundMgr::ReleaseSound3DProviderList(Sound3DProvider *pSound3DProviderList)
{
	Sound3DProvider *pSound3DProvider;

	if (!pSound3DProviderList)
		return LT_INVALIDPARAMS;

	while (pSound3DProviderList)
	{
		pSound3DProvider = pSound3DProviderList->m_pNextProvider;
		delete pSound3DProviderList;
		pSound3DProviderList = pSound3DProvider;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495480
LTRESULT CSoundMgr::InitSound(InitSoundInfo *pSoundInfo)
{
	if (!pSoundInfo)
		RETURN_ERROR(1, CSoundMgr::InitSound, LT_INVALIDPARAMS);

	return Init(*pSoundInfo);
}

// FUNCTION: LITHTECH 0x004954d0
LTRESULT CSoundMgr::SetSoundOcclusion(HLTSOUND hSound, LTFLOAT fLevel)
{
	CSoundInstance *pSoundInstance;

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::SetSoundOcclusion, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::SetSoundOcclusion, LT_ERROR);

	pSoundInstance->SetOcclusion(fLevel);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495570
LTRESULT CSoundMgr::GetSoundOcclusion(HLTSOUND hSound, LTFLOAT *pLevel)
{
	CSoundInstance *pSoundInstance;

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::GetSoundOcclusion, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::GetSoundOcclusion, LT_ERROR);

	if (pLevel)
		pSoundInstance->GetOcclusion(*pLevel);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495610
LTRESULT CSoundMgr::SetSoundObstruction(HLTSOUND hSound, LTFLOAT fLevel)
{
	CSoundInstance *pSoundInstance;

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::SetSoundObstruction, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::SetSoundObstruction, LT_ERROR);

	pSoundInstance->SetObstruction(fLevel);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004956b0
LTRESULT CSoundMgr::GetSoundObstruction(HLTSOUND hSound, LTFLOAT *pLevel)
{
	CSoundInstance *pSoundInstance;

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::GetSoundObstruction, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::GetSoundObstruction, LT_ERROR);

	if (pLevel)
		pSoundInstance->GetObstruction(*pLevel);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495750
LTRESULT CSoundMgr::GetSoundPosition(HLTSOUND hSound, LTVector *pPos)
{
	CSoundInstance *pSoundInstance;

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::GetSoundPosition, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::GetSoundPosition, LT_ERROR);

	if (pPos)
		pSoundInstance->GetPosition(*pPos);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x004957f0
LTRESULT CSoundMgr::SetSoundPosition(HLTSOUND hSound, LTVector *pPos)
{
	CSoundInstance *pSoundInstance;

	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::SetSoundPosition, LT_INVALIDPARAMS);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::SetSoundPosition, LT_ERROR);

	if (pPos)
		pSoundInstance->SetPosition(*pPos);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495890 ?SetListener@CSoundMgr@@UAEKIPAV?$_CVector@M@@PAVLTRotation@@I@Z
LTRESULT CSoundMgr::SetListener(LTBOOL bListenerInClient, LTVector *pPos, LTRotation *pRot, LTBOOL bTeleport)
{
	LTVector vForward, vRight, vUp;

	if (bListenerInClient || !pRot)
		SetListener(bListenerInClient, pPos, LTNULL, LTNULL, bTeleport);
	else
	{
		quat_GetVectors((float *)pRot, (float *)&vRight, (float *)&vUp, (float *)&vForward);
		SetListener(bListenerInClient, pPos, &vForward, &vRight, bTeleport);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495910
LTRESULT CSoundMgr::ReleaseSoundHandle(HLTSOUND hSound)
{
	CSoundInstance *pSoundInstance;

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		return LT_INVALIDPARAMS;

	if (!(pSoundInstance->GetPlaySoundFlags() & PLAYSOUND_GETHANDLE))
		return LT_ERROR;

	pSoundInstance->m_dwPlaySoundFlags &= ~PLAYSOUND_GETHANDLE;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495950
LTRESULT CSoundMgr::SetSoundFilter(HLTSOUND hSound, const char *pFilter)
{
	CSoundInstance *pSoundInstance;

	// Make sure we've got a valid sound...
	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::SetSoundFilter, LT_ERROR);

	pSoundInstance = FindSoundInstance(hSound, LTFALSE);
	if (!pSoundInstance)
	{
		pSoundInstance = FindSoundInstance(hSound, LTTRUE);
		if (!pSoundInstance)
			RETURN_ERROR(1, CSoundMgr::SetSoundFilter, LT_INVALIDPARAMS);
	}

	// Set the filter
	if (!pSoundInstance->SetFilter(pFilter))
		RETURN_ERROR(1, CSoundMgr::SetSoundFilter, LT_ERROR);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495a50
LTRESULT CSoundMgr::SetSoundFilterParam(HLTSOUND hSound, const char *pParam, float fValue)
{
	CSoundInstance *pSoundInstance;

	// Parameter validation..
	if (!pParam)
		RETURN_ERROR(1, CSoundMgr::SetSoundFilterParam, LT_INVALIDPARAMS);

	// Make sure we've got a valid sound...
	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::SetSoundFilterParam, LT_ERROR);

	pSoundInstance = FindSoundInstance(hSound, LTFALSE);
	if (!pSoundInstance)
	{
		pSoundInstance = FindSoundInstance(hSound, LTTRUE);
		if (!pSoundInstance)
			RETURN_ERROR(1, CSoundMgr::SetSoundFilterParam, LT_INVALIDPARAMS);
	}

	// Set the filter parameter
	if (!pSoundInstance->SetFilterParam(pParam, fValue))
		RETURN_ERROR(1, CSoundMgr::SetSoundFilterParam, LT_NOTFOUND);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495ba0
LTRESULT CSoundMgr::GetSoundFilterParam(HLTSOUND hSound, const char *pParam, float *pValue)
{
	CSoundInstance *pSoundInstance;

	// Parameter validation..
	if (!pParam || !pValue)
		RETURN_ERROR(1, CSoundMgr::GetSoundFilterParam, LT_INVALIDPARAMS);

	// Make sure we've got a valid sound...
	if (!hSound)
		RETURN_ERROR(1, CSoundMgr::GetSoundFilterParam, LT_ERROR);

	pSoundInstance = FindSoundInstance(hSound, LTTRUE);
	if (!pSoundInstance)
		RETURN_ERROR(1, CSoundMgr::GetSoundFilterParam, LT_INVALIDPARAMS);

	// Only 2d samples have filters.
	if (!pSoundInstance->GetSample())
		RETURN_ERROR(1, CSoundMgr::GetSoundFilterParam, LT_INVALIDPARAMS);

	// Get the filter parameter
	if (!pSoundInstance->GetFilterParam(pParam, pValue))
		RETURN_ERROR(1, CSoundMgr::GetSoundFilterParam, LT_NOTFOUND);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495ce0
LTRESULT CSoundMgr::GetFilterName(uint32 nIndex, const char **pFilter)
{
	HPROENUM next;
	HPROVIDER hFilter;
	char *pName;

	if (!pFilter)
		RETURN_ERROR(1, CSoundMgr::GetFilterName, LT_INVALIDPARAMS);

	*pFilter = LTNULL;

	next = HPROENUM_FIRST;
	while (AIL_enumerate_filters(&next, &hFilter, &pName))
	{
		if (nIndex == 0)
		{
			*pFilter = pName;
			break;
		}

		nIndex--;
	}

	if (!*pFilter)
		RETURN_ERROR(1, CSoundMgr::GetFilterName, LT_NOTFOUND);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00495dd0
LTRESULT CSoundMgr::GetFilterIndex(const char *pFilter, uint32 *pIndex)
{
	HPROENUM next;
	HPROVIDER hFilter;
	char *pName;

	if (!pFilter || !pIndex)
		RETURN_ERROR(1, CSoundMgr::GetFilterIndex, LT_INVALIDPARAMS);

	*pIndex = 0;

	next = HPROENUM_FIRST;
	while (AIL_enumerate_filters(&next, &hFilter, &pName))
	{
		// Talon bug: the test is inverted.
		if (stricmp(pFilter, pName) != 0)
			return LT_OK;

		(*pIndex)++;
	}

	*pIndex = 0;
	RETURN_ERROR(1, CSoundMgr::GetFilterIndex, LT_NOTFOUND);
}

// FUNCTION: LITHTECH 0x00495ed0
LTRESULT CSoundMgr::GetFilterParamName(uint32 nIndex, const char *pFilter, const char **pParam)
{
	HPROENUM next;
	HPROVIDER hFilter;
	char *pName;
	HINTENUM nextAttrib;
	RIB_INTERFACE_ENTRY attrib;

	if (!pFilter || !pParam)
		RETURN_ERROR(1, CSoundMgr::GetFilterParamName, LT_INVALIDPARAMS);

	*pParam = LTNULL;

	// Find the filter.
	next = HPROENUM_FIRST;
	while (AIL_enumerate_filters(&next, &hFilter, &pName))
	{
		if (stricmp(pFilter, pName) == 0)
			break;

		hFilter = 0;
	}

	if (!hFilter)
		RETURN_ERROR(1, CSoundMgr::GetFilterParamName, LT_NOTFOUND);

	// Find the parameter.
	nextAttrib = HINTENUM_FIRST;
	while (AIL_enumerate_filter_sample_attributes(hFilter, &nextAttrib, &attrib))
	{
		if (nIndex == 0)
		{
			*pParam = attrib.entry_name;
			break;
		}

		nIndex--;
	}

	if (!*pParam)
		RETURN_ERROR(1, CSoundMgr::GetFilterParamName, LT_NOTFOUND);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00496060
LTRESULT CSoundMgr::GetFilterParamIndex(const char *pFilter, const char *pParam, uint32 *pIndex)
{
	HINTENUM nextAttrib;
	RIB_INTERFACE_ENTRY attrib;
	HPROENUM next;
	HPROVIDER hFilter;
	char *pName;

	if (!pFilter || !pParam || !pIndex)
		RETURN_ERROR(1, CSoundMgr::GetFilterParamIndex, LT_INVALIDPARAMS);

	*pIndex = 0;

	// Find the filter.
	next = HPROENUM_FIRST;
	while (AIL_enumerate_filters(&next, &hFilter, &pName))
	{
		if (stricmp(pFilter, pName) == 0)
			break;

		hFilter = 0;
	}

	if (!hFilter)
		RETURN_ERROR(1, CSoundMgr::GetFilterParamIndex, LT_NOTFOUND);

	// Find the parameter.
	nextAttrib = HINTENUM_FIRST;
	while (AIL_enumerate_filter_sample_attributes(hFilter, &nextAttrib, &attrib))
	{
		if (stricmp(attrib.entry_name, pParam) == 0)
			return LT_OK;

		(*pIndex)++;
	}

	*pIndex = 0;
	RETURN_ERROR(1, CSoundMgr::GetFilterParamIndex, LT_NOTFOUND);
}


// Out-of-line template code instantiated here.
// FUNCTION: LITHTECH 0x004961c0 ??0?$CMoArray@EVDefaultCache@@@@QAE@XZ
// FUNCTION: LITHTECH 0x004961e0 ?AllocVoid@?$ObjectBank@VCSoundBuffer@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00496220 ?AllocVoid@?$ObjectBank@VCLocalSoundInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00496260 ?AllocVoid@?$ObjectBank@VCAmbientSoundInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x004962a0 ?AllocVoid@?$ObjectBank@VC3DSoundInstance@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x004962e0 ?Term@?$ObjectBank@VCSoundBuffer@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00496300 ?Term@?$ObjectBank@VCLocalSoundInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00496320 ?Term@?$ObjectBank@VC3DSoundInstance@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00496340 ??_G?$ObjectBank@VCSoundBuffer@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00496380 ??_G?$ObjectBank@VCLocalSoundInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004963c0 ??0CLocalSoundInstance@@QAE@XZ
// FUNCTION: LITHTECH 0x004963e0 ??_G?$ObjectBank@VCAmbientSoundInstance@@VNullCS@@@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00496420 ??_G?$ObjectBank@VC3DSoundInstance@@VNullCS@@@@UAEPAXI@Z
