#include "stdafx.h"
/*
-----------------------------------------------------------------------------
 Class: RageDisplay

 Desc: See header.

 Copyright (c) 2001-2002 by the person(s) listed below.  All rights reserved.
	Chris Danford
-----------------------------------------------------------------------------
*/

#include "RageDisplay.h"

#include "RageUtil.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageException.h"


RageDisplay*		DISPLAY	= NULL;


RageDisplay::RageDisplay( HWND hWnd )
{
	LOG->Trace( "RageDisplay::RageDisplay()" );

	// Save the window handle
	m_hWnd = hWnd;
	m_pd3d = NULL;
	m_pd3dDevice = NULL;
	m_pVB = NULL;

	m_fLastUpdateTime = TIMER->GetTimeSinceStart();
	m_iFramesRenderedSinceLastCheck = 0;
	m_fFPS = 0;


	try
	{
		// Construct a Direct3D object
		m_pd3d = Direct3DCreate8( D3D_SDK_VERSION );
	}
	catch (...) 
	{
		// Edwin Evans: Catch any exception. It won't be caught by main exception handler.
		throw RageException( "Unknown exception in Direct3DCreate8." );
	}

    if( NULL == m_pd3d )
		throw RageException( "Direct3DCreate8 failed." );

	HRESULT  hr;
	if( FAILED( hr = m_pd3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &m_DeviceCaps) ) )
	{
		throw RageException( 
			"There was an error while initializing your video card.\n\n"
			"Your system is reporting that Direct3D8 hardware acceleration\n"
			"is not available.  In most cases, you can download an updated\n"
			"driver from your card's manufacturer."
		);
	}

	LOG->Trace( 
		"Video card info:\n"
		" - max texture width is %d\n"
		" - max texture height is %d\n"
		" - max texture blend stages is %d\n"
		" - max simultaneous textures is %d\n",
		m_DeviceCaps.MaxTextureWidth,
		m_DeviceCaps.MaxTextureHeight,
		m_DeviceCaps.MaxTextureBlendStages,
		m_DeviceCaps.MaxSimultaneousTextures
		);



	// Enumerate possible display modes
	LOG->Trace( "This display adaptor supports the following modes:" );
	for( UINT u=0; u<m_pd3d->GetAdapterModeCount(D3DADAPTER_DEFAULT); u++ )
	{
		D3DDISPLAYMODE mode;
		if( SUCCEEDED( m_pd3d->EnumAdapterModes( D3DADAPTER_DEFAULT, u, &mode ) ) )
		{
			//LOG->Trace( "  %ux%u %uHz, format %d", mode.Width, mode.Height, mode.RefreshRate, mode.Format );
		}
	}

	// Save the original desktop format.
	m_pd3d->GetAdapterDisplayMode( D3DADAPTER_DEFAULT, &m_DesktopMode );
}

RageDisplay::~RageDisplay()
{
	ReleaseVertexBuffer();
	// Release our D3D Device
	SAFE_RELEASE( m_pd3dDevice );
    m_pd3d->Release();
}



//-----------------------------------------------------------------------------
// Name: SwitchDisplayMode()
// Desc:
//-----------------------------------------------------------------------------
bool RageDisplay::SwitchDisplayMode( 
	const bool bWindowed, const int iWidth, const int iHeight, const int iBPP, const int iFullScreenHz )
{
	LOG->Trace( "RageDisplay::SwitchDisplayModes( %d, %d, %d, %d, %d )", bWindowed, iWidth, iHeight, iBPP, iFullScreenHz );

	if( !bWindowed )
		SetCursor( NULL );


	HRESULT hr;

    // Find an pixel format for the back buffer.
	// If windowed, then dwBPP is ignored.  Use whatever works.
    CArray<D3DFORMAT,D3DFORMAT> arrayBackBufferFormats;		// throw all possibilities in here
	
	if( bWindowed )
	{
		arrayBackBufferFormats.Add( D3DFMT_R5G6B5 );
		arrayBackBufferFormats.Add( D3DFMT_X1R5G5B5 );
		arrayBackBufferFormats.Add( D3DFMT_A1R5G5B5 );
		arrayBackBufferFormats.Add( D3DFMT_R8G8B8 );
		arrayBackBufferFormats.Add( D3DFMT_X8R8G8B8 );
		arrayBackBufferFormats.Add( D3DFMT_A8R8G8B8 );
	}
	else		// full screen
	{
		// add only the formats that match dwBPP
		switch( iBPP )
		{
		case 16:
			arrayBackBufferFormats.Add( D3DFMT_R5G6B5 );
			arrayBackBufferFormats.Add( D3DFMT_X1R5G5B5 );
			arrayBackBufferFormats.Add( D3DFMT_A1R5G5B5 );
			break;
		case 32:
			arrayBackBufferFormats.Add( D3DFMT_R8G8B8 );
			arrayBackBufferFormats.Add( D3DFMT_X8R8G8B8 );
			arrayBackBufferFormats.Add( D3DFMT_A8R8G8B8 );
			break;
		default:
			throw RageException( ssprintf("Invalid BPP '%u' specified", iBPP) );
			return false;
		}
	}


	// Test each back buffer format until we find something that works.
	D3DFORMAT fmtDisplay;	// fill these in below...
	D3DFORMAT fmtBackBuffer;

	for( int i=0; i < arrayBackBufferFormats.GetSize(); i++ )
	{
		if( bWindowed )
		{
			fmtDisplay = m_DesktopMode.Format;
			fmtBackBuffer = arrayBackBufferFormats[i];
		}
		else	// Fullscreen
		{
			fmtDisplay = fmtBackBuffer = arrayBackBufferFormats[i];
		}

		LOG->Trace( "Testing format: display %d, back buffer %d, windowed %d...", fmtDisplay, fmtBackBuffer, bWindowed );

		hr = m_pd3d->CheckDeviceType( 
			D3DADAPTER_DEFAULT, 
			D3DDEVTYPE_HAL, 
			fmtDisplay, 
			fmtBackBuffer, 
			bWindowed
		);

		if( SUCCEEDED(hr) )
		{
			LOG->Trace( "This will work." );
			break;	// done searching
		}
		else
		{
			LOG->Trace( "This won't work.  Keep searching." );
		}
	}

	if( i == arrayBackBufferFormats.GetSize() )		// we didn't find an appropriate format
	{
		LOG->Trace( hr, "failed to find an appropriate format for %d, %u, %u, %u.", bWindowed, iWidth, iHeight, iBPP );
		return false;
	}



    // Set up presentation parameters for the display
    ZeroMemory( &m_d3dpp, sizeof(m_d3dpp) );
    
	m_d3dpp.BackBufferWidth			=	iWidth;
    m_d3dpp.BackBufferHeight		=	iHeight;
    m_d3dpp.BackBufferFormat		=	fmtBackBuffer;
    m_d3dpp.BackBufferCount			=	1;
    m_d3dpp.MultiSampleType			=	D3DMULTISAMPLE_NONE;
	m_d3dpp.SwapEffect				=	D3DSWAPEFFECT_DISCARD;
	m_d3dpp.hDeviceWindow			=	m_hWnd;
    m_d3dpp.Windowed				=	bWindowed;
    m_d3dpp.EnableAutoDepthStencil	=	TRUE;
    m_d3dpp.AutoDepthStencilFormat	=	D3DFMT_D16;
    m_d3dpp.Flags					=	0;
	m_d3dpp.FullScreen_RefreshRateInHz = bWindowed ? D3DPRESENT_RATE_DEFAULT : iFullScreenHz;
	m_d3dpp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

	LOG->Trace( "Present Parameters: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d", 
		m_d3dpp.BackBufferWidth,
		m_d3dpp.BackBufferHeight,
		m_d3dpp.BackBufferFormat,
		m_d3dpp.BackBufferCount,
		m_d3dpp.MultiSampleType,
		m_d3dpp.SwapEffect,
		m_d3dpp.hDeviceWindow,
		m_d3dpp.Windowed,
		m_d3dpp.EnableAutoDepthStencil,
		m_d3dpp.AutoDepthStencilFormat,
		m_d3dpp.Flags,
		m_d3dpp.FullScreen_RefreshRateInHz,
		m_d3dpp.FullScreen_PresentationInterval
	);



	if( m_pd3dDevice == NULL )
	{
		D3DADAPTER_IDENTIFIER8	identifier;
		if( FAILED( hr = m_pd3d->GetAdapterIdentifier( D3DADAPTER_DEFAULT, 0, &identifier ) ) )
		{
			LOG->Trace( hr, "GetAdapterIdentifier failed" );
			return false;
		}

		LOG->Trace( "Driver: %s.  Description: %s.", 
			identifier.Driver, 
			identifier.Description 
			);

		// device is not yet created.  We need to create it
		if( FAILED( hr = m_pd3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, 
											m_hWnd,
											D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
											&m_d3dpp, &m_pd3dDevice) ) )
		{
			LOG->Trace( hr, "failed to create device: %d, %u, %u, %u.", bWindowed, iWidth, iHeight, iBPP );
			return false;
		}
		LOG->Trace( 
			"Video card info:\n"
			" - available texture mem is %u\n",
			m_pd3dDevice->GetAvailableTextureMem()
		);
		if( m_pVB == NULL )
			CreateVertexBuffer();
	}
	else
	{
		// device is already created.  Just reset it.
		if( FAILED( hr = m_pd3dDevice->Reset( &m_d3dpp ) ) )
		{
			LOG->Trace( hr, "failed to reset device: %d, %u, %u, %u.", bWindowed, iWidth, iHeight, iBPP );
			return false;
		}
	}

	LOG->Trace( "Mode change was successful." );

	// Clear the back buffer and present it so we don't show the gibberish that was
	// in video memory from the last app.
	BeginFrame();
	EndFrame();
	ShowFrame();


	return true;
}


//-----------------------------------------------------------------------------
// Name: Reset()
// Desc:
//-----------------------------------------------------------------------------
HRESULT RageDisplay::Reset()
{
	return m_pd3dDevice->Reset( &m_d3dpp );
}


//-----------------------------------------------------------------------------
// Name: BeginFrame()
// Desc:
//-----------------------------------------------------------------------------
HRESULT RageDisplay::BeginFrame()
{
	//////////////////////////////////////////////////////////////
	// Do some fancy testing to make sure the D3D deivce is ready.
	// This is mainly used when the the app is reactivated
	// after the user has Alt-tabed out of full screen mode.
	//////////////////////////////////////////////////////////////

	// Test cooperative level
    HRESULT hr;

    // Test the cooperative level to see if it's okay to render
    if( FAILED( hr = m_pd3dDevice->TestCooperativeLevel() ) )
    {
        // If the device was lost, do not render until we get it back
        if( D3DERR_DEVICELOST == hr )
            return hr;			// not ready to render

        // Check if the device needs to be resized.
        if( D3DERR_DEVICENOTRESET == hr )
	        return hr;

		return hr;
    }


	m_pd3dDevice->Clear( 0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
						 D3DCOLOR_XRGB(0,0,0), 1.0f, 0x00000000 );
	
	
	if ( FAILED( hr  = m_pd3dDevice->BeginScene() ) )
		return E_FAIL;

	// disable culling so backward polys can be drawn
	m_pd3dDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
	m_pd3dDevice->SetRenderState( D3DRS_LIGHTING, FALSE );

	// Enable Alpha Blending and Testing
	m_pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
	//m_pd3dDevice->SetRenderState( D3DRS_ALPHATESTENABLE,  TRUE );
	//m_pd3dDevice->SetRenderState( D3DRS_ALPHAREF,         0x08 );
	//m_pd3dDevice->SetRenderState( D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL );
            
	m_pd3dDevice->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA );
	m_pd3dDevice->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );

	m_pd3dDevice->SetRenderState( D3DRS_ZENABLE,      FALSE );
//	m_pd3dDevice->SetRenderState( D3DRS_ZENABLE,      TRUE );
//	m_pd3dDevice->SetRenderState( D3DRS_ZWRITEENABLE, TRUE );


    m_pd3dDevice->SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    m_pd3dDevice->SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );


	// Don't tile texture coords.  This creates ugly wrapping artifacts on textures that have to be rescaled.
    //m_pd3dDevice->SetTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_BORDER );
    //m_pd3dDevice->SetTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_BORDER );



	return S_OK;
}


HRESULT RageDisplay::EndFrame()
{
	m_pd3dDevice->EndScene();


	// update stats
	m_iFramesRenderedSinceLastCheck++;

	float fTimeNow = TIMER->GetTimeSinceStart();
	if( fTimeNow - m_fLastUpdateTime > 1.0f )	// update stats every 1 sec.
	{
		m_fFPS = (float)m_iFramesRenderedSinceLastCheck;
		m_iFramesRenderedSinceLastCheck = 0;
		m_fLastUpdateTime = fTimeNow;

		LOG->Trace( "FPS: %.0f", m_fFPS );
	}


	return S_OK;
}

HRESULT RageDisplay::ShowFrame()
{
	if( m_pd3dDevice )
		m_pd3dDevice->Present( 0, 0, 0, 0 );

	return S_OK;
}




HRESULT RageDisplay::Invalidate()
{
	return S_OK;
}


HRESULT RageDisplay::Restore()
{

	return S_OK;
}




void RageDisplay::CreateVertexBuffer()
{
	HRESULT hr;
	if( FAILED( hr = GetDevice()->CreateVertexBuffer( 
									MAX_NUM_VERTICIES * sizeof(RAGEVERTEX),
									D3DUSAGE_WRITEONLY, D3DFVF_RAGEVERTEX,
									D3DPOOL_MANAGED, &m_pVB ) ) )
		throw RageException( hr, "Vertex Buffer Could Not Be Created" );
}


void RageDisplay::ReleaseVertexBuffer()
{
	SAFE_RELEASE( m_pVB );
}

