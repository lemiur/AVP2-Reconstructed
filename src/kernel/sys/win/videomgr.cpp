// Jupiter runtime/kernel/src/sys/win/videomgr.cpp
// The unit starts at 0049d030 with the leech functions (they were left in version_resource's
// old range) and ends at 0049d310, where SmackVideoMgr starts smackvideomgrimpl.
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "videomgr.h"
#include "binkvideomgrimpl.h"
#include "smackvideomgrimpl.h"
#include "engine_vars.h"



// FUNCTION: LITHTECH 0x0049d030
LTRESULT VMSurfaceLeechFn(Nexus *pNexus, Leech *pLeech, int msg, void *pUserData)
{
	VideoInst *pVideo = (VideoInst*)pLeech->m_pUserData;
	Surface *pSurface = (Surface*)pNexus->m_pData;

	if(msg == NEXUS_NEXUSDESTROY)
	{
		*((LTBOOL*)pUserData) = LTFALSE;
		pVideo->OnSurfaceDestroyed(pSurface);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049d060
LTRESULT VMTextureLeechFn(Nexus *pNexus, Leech *pLeech, int msg, void *pUserData)
{
	VideoInst *pVideo = (VideoInst*)pLeech->m_pUserData;
	SharedTexture *pTexture = (SharedTexture*)pNexus->m_pData;

	if(msg == NEXUS_NEXUSDESTROY)
	{
		*((LTBOOL*)pUserData) = LTFALSE;
		pVideo->OnTextureDestroyed(pTexture);
	}

	return LT_OK;
}

// GLOBAL: LITHTECH 0x004d3bc0
extern LeechDef g_BaseLeech;

LeechDef g_VMSurfaceLeechDef = {VMSurfaceLeechFn, &g_BaseLeech};
LeechDef g_VMTextureLeechDef = {VMTextureLeechFn, &g_BaseLeech};


// ------------------------------------------------------------------------ //
// VideoSurfaceLink
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0049d090
VideoSurfaceLink::VideoSurfaceLink(VideoInst *pVideo)
	: m_SurfaceLeech(&g_VMSurfaceLeechDef, pVideo),
	m_TextureLeech(&g_VMTextureLeechDef, pVideo)
{
	m_pSurface = LTNULL;
	m_pTexture = LTNULL;
}

// FUNCTION: LITHTECH 0x0049d0c0
VideoSurfaceLink::~VideoSurfaceLink()
{
	if(m_pSurface)
		m_pSurface->m_Nexus.RemoveLeech(&m_SurfaceLeech);

	SetTexture(LTNULL);
}

// FUNCTION: LITHTECH 0x0049d0e0
void VideoSurfaceLink::SetSurface(Surface *pSurface)
{
	if(m_pSurface)
		m_pSurface->m_Nexus.RemoveLeech(&m_SurfaceLeech);

	m_pSurface = pSurface;
	if(pSurface)
		nexus_AddLeech(&pSurface->m_Nexus, &m_SurfaceLeech);
}

// FUNCTION: LITHTECH 0x0049d120
void VideoSurfaceLink::SetTexture(SharedTexture *pTexture)
{
	if(m_pTexture)
		((Nexus*)m_pTexture)->RemoveLeech(&m_TextureLeech);

	m_pTexture = pTexture;
	if(pTexture)
		nexus_AddLeech((Nexus*)pTexture, &m_TextureLeech);
}


// ------------------------------------------------------------------------ //
// VideoMgr
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0049d150
void VideoMgr::UpdateVideos()
{
	uint32 nRunning = 0;
	for(MPOS pos=m_Videos; pos; )
	{
		VideoInst *pVideo = m_Videos.GetNext(pos);

		pVideo->Update();

		if(pVideo->GetVideoStatus() == LT_OK)
			++nRunning;
	}

	if( g_CV_VideoDebug )
	{
		dsi_ConsolePrint("%d videos, %d active", m_Videos.GetSize(), nRunning);
	}
}


// FUNCTION: LITHTECH 0x0049d1b0
void VideoMgr::OnRenderInit()
{
	MPOS pos;

	for( pos = m_Videos; pos; )
		m_Videos.GetNext(pos)->OnRenderInit();
}


// FUNCTION: LITHTECH 0x0049d1e0
void VideoMgr::OnRenderTerm()
{
	MPOS pos;

	for( pos = m_Videos; pos; )
		m_Videos.GetNext(pos)->OnRenderTerm();
}


// -------------------------------------------------------------------------------- //
// The starting point...
//
//	Create the appropriate videomgr
// -------------------------------------------------------------------------------- //
// FUNCTION: LITHTECH 0x0049d210
VideoMgr* CreateVideoMgr(CClientMgr *pClientMgr, const char *pszName)
{
	BinkVideoMgr *pBinkMgr;
	SmackVideoMgr *pSmackMgr;

	if( stricmp( pszName, "BINK" ) == 0 )
	{
		pBinkMgr = new BinkVideoMgr(pClientMgr);
		if( pBinkMgr )
		{
			if(pBinkMgr->Init() == LT_OK)
			{
				//success, give them back the video manager
				return pBinkMgr;
			}

			//we failed to initialize
			delete pBinkMgr;
		}
	}
	else if( stricmp( pszName, "SMACKER" ) == 0 )
	{
		// Note: Bink can play Smacker files.
		pBinkMgr = new BinkVideoMgr(pClientMgr);
		if( pBinkMgr )
		{
			if(pBinkMgr->Init() == LT_OK)
			{
				return pBinkMgr;
			}

			delete pBinkMgr;
		}

		pSmackMgr = new SmackVideoMgr(pClientMgr);
		if( pSmackMgr )
		{
			if(pSmackMgr->Init() == LT_OK)
			{
				return pSmackMgr;
			}

			delete pSmackMgr;
		}
	}

	return LTNULL;
}
