/*
Copyright (C) 2002-2003 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "client.h"
#include "../cin/cin.h"
#include "../qcommon/asyncstream.h"
#include "../ref_gl/r_frontend.h"

static void *cge = nullptr;

static mempool_t *cl_gamemodulepool;

static void *module_handle;

static int cg_load_seq = 1;

//======================================================================

// CL_GameModule versions of the CM functions passed to the game module
// they only add sv.cms as the first parameter

//======================================================================

int CG_NumInlineModels( void ) {
	return CM_NumInlineModels( cl.cms );
}

int CG_TransformedPointContents( const vec3_t p, const cmodel_s *cmodel, const vec3_t origin, const vec3_t angles ) {
	return CM_TransformedPointContents( cl.cms, p, cmodel, origin, angles );
}

void CG_TransformedBoxTrace( trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
							 const cmodel_s *cmodel, int brushmask, const vec3_t origin, const vec3_t angles ) {
	CM_TransformedBoxTrace( cl.cms, tr, start, end, mins, maxs, cmodel, brushmask, origin, angles );
}

const cmodel_s *CG_InlineModel( int num ) {
	return CM_InlineModel( cl.cms, num );
}

void CG_InlineModelBounds( const cmodel_s *cmodel, vec3_t mins, vec3_t maxs ) {
	CM_InlineModelBounds( cl.cms, cmodel, mins, maxs );
}

const cmodel_s *CG_ModelForBBox( const vec3_t mins, const vec3_t maxs ) {
	return CM_ModelForBBox( cl.cms, mins, maxs );
}

const cmodel_s *CG_OctagonModelForBBox( const vec3_t mins, const vec3_t maxs ) {
	return CM_OctagonModelForBBox( cl.cms, mins, maxs );
}

bool CG_InPVS( const vec3_t p1, const vec3_t p2 ) {
	return CM_InPVS( cl.cms, p1, p2 );
}

void NET_GetUserCmd( int frame, usercmd_t *cmd ) {
	if( cmd ) {
		if( frame < 0 ) {
			frame = 0;
		}

		*cmd = cl.cmds[frame & CMD_MASK];
	}
}

int NET_GetCurrentUserCmdNum( void ) {
	return cls.ucmdHead;
}

void NET_GetCurrentState( int64_t *incomingAcknowledged, int64_t *outgoingSequence, int64_t *outgoingSent ) {
	if( incomingAcknowledged )
#ifdef TCP_ALLOW_CONNECT
	{ *incomingAcknowledged = cls.ucmdHead;}
#else
	{ *incomingAcknowledged = cls.ucmdAcknowledged;}
#endif
	if( outgoingSequence ) {
		*outgoingSequence = cls.ucmdHead;
	}
	if( outgoingSent ) {
		*outgoingSent = cls.ucmdSent;
	}
}

void *CG_MemAlloc( size_t size, const char *filename, int fileline ) {
	return _Mem_Alloc( cl_gamemodulepool, size, MEMPOOL_CLIENTGAME, 0, filename, fileline );
}

void CG_MemFree( void *data, const char *filename, int fileline ) {
	_Mem_Free( data, MEMPOOL_CLIENTGAME, 0, filename, fileline );
}

//==============================================

// wrap cgame listeners into proxies so that
// cinematics won't attempt to pass samples to
// entities which are not valid anymore due to
// module reload

#define MAX_CGAME_RAW_SAMPLES_LISTENERS 8

typedef struct {
	bool inuse;
	struct cinematics_s *cin;
	void *ptr;
	int load_seq;
	cg_raw_samples_cb_t rs;
	cg_get_raw_samples_cb_t grs;
} cg_raw_samples_listener_t;

cg_raw_samples_listener_t cg_raw_samples_listeners[MAX_CGAME_RAW_SAMPLES_LISTENERS];

/*
* CL_GameModule_RawSamples
*/
static void CL_GameModule_RawSamples( void *ptr, unsigned int samples,
									  unsigned int rate, unsigned short width, unsigned short channels,
									  const uint8_t *data ) {
	cg_raw_samples_listener_t *cglistener;

	// free listener
	cglistener = ( cg_raw_samples_listener_t * )ptr;

	// each listener gets samples passed exactly once
	cglistener->inuse = false;

	if( cglistener->load_seq != cg_load_seq ) {
		return;
	}
	cglistener->rs( cglistener->ptr, samples, rate, width, channels, data );
}

/*
* CL_GameModule_GetRawSamplesLength
*/
static unsigned int CL_GameModule_GetRawSamplesLength( void *ptr ) {
	cg_raw_samples_listener_t *cglistener;

	cglistener = ( cg_raw_samples_listener_t * )ptr;
	if( cglistener->load_seq != cg_load_seq ) {
		return 0;
	}
	return cglistener->grs( cglistener->ptr );
}

/*
* CL_GameModule_AddRawSamplesListener
*/
bool CG_AddRawSamplesListener( cinematics_s *cin, void *listener, cg_raw_samples_cb_t rs, cg_get_raw_samples_cb_t grs ) {
	int i;
	cg_raw_samples_listener_t *cglistener, *freel;

	freel = NULL;

	cglistener = cg_raw_samples_listeners;
	for( i = 0; i < MAX_CGAME_RAW_SAMPLES_LISTENERS; i++ ) {
		if( !freel && !cglistener->inuse ) {
			// grab a free one
			freel = cglistener;
		} else if( cglistener->inuse
				   && cglistener->cin == cin
				   && cglistener->ptr == listener
				   && cglistener->rs == rs
				   && cglistener->grs == grs ) {
			// same listener
			return true;
		}
		cglistener++;
	}

	if( !freel ) {
		return false;
	}

	// fill in our proxy
	cglistener = freel;
	cglistener->inuse = true;
	cglistener->cin = cin;
	cglistener->ptr = listener;
	cglistener->load_seq = cg_load_seq;
	cglistener->rs = (cin_raw_samples_cb_t)rs;
	cglistener->grs = (cin_get_raw_samples_cb_t)grs;

	if( !CIN_AddRawSamplesListener( cin, cglistener, &CL_GameModule_RawSamples,
									&CL_GameModule_GetRawSamplesLength ) ) {
		// free listener
		cglistener->inuse = false;
		return false;
	}

	return true;
}

/*
* CL_GameModule_Init
*/
void CL_GameModule_Init( void ) {
	int64_t start;

	// stop all playing sounds
	CL_SoundModule_StopAllSounds( true, true );

	CL_GameModule_Shutdown();

	cl_gamemodulepool = _Mem_AllocPool( NULL, "Client Game Progs", MEMPOOL_CLIENTGAME, __FILE__, __LINE__ );

	SCR_EnableQuickMenu( false );

	start = Sys_Milliseconds();
	CG_Init( cls.servername, cl.playernum,
			   viddef.width, viddef.height, VID_GetPixelRatio(),
			   cls.demo.playing, cls.demo.playing ? cls.demo.filename : "",
			   cls.sv_pure, cl.snapFrameTime, APP_PROTOCOL_VERSION, APP_DEMO_EXTENSION_STR,
			   cls.mediaRandomSeed, cl.gamestart );

	cge = (void *)1;

	Com_DPrintf( "CL_GameModule_Init: %.2f seconds\n", (float)( Sys_Milliseconds() - start ) * 0.001f );

	cl.gamestart = false;
	cls.cgameActive = true;
}

/*
* CL_GameModule_Reset
*/
void CL_GameModule_Reset( void ) {
	if( cge ) {
		CG_Reset();
	}
}

/*
* CL_GameModule_Shutdown
*/
void CL_GameModule_Shutdown( void ) {
	if( !cge ) {
		return;
	}

	cg_load_seq++;
	cls.cgameActive = false;

	CG_Shutdown();
	Mem_FreePool( &cl_gamemodulepool );
	Com_UnloadGameLibrary( &module_handle );
	cge = NULL;
}

/*
* CL_GameModule_EscapeKey
*/
void CL_GameModule_EscapeKey( void ) {
	if( cge ) {
		CG_EscapeKey();
	} else if( cls.state == CA_CINEMATIC ) {
		SCR_FinishCinematic();
	}
}

/*
* CL_GameModule_GetEntitySoundOrigin
*/
void CL_GameModule_GetEntitySpatilization( int entNum, vec3_t origin, vec3_t velocity ) {
	if( cge ) {
		CG_GetEntitySpatilization( entNum, origin, velocity );
	}
}

/*
* CL_GameModule_ConfigString
*/
void CL_GameModule_ConfigString( int number, const char *value ) {
	if( cge ) {
		CG_ConfigString( number, value );
	}
}

/*
* CL_GameModule_GetSensitivityScale
*/
float CL_GameModule_GetSensitivityScale( float sens, float zoomSens ) {
	if( cge ) {
		return CG_GetSensitivityScale( sens, zoomSens );
	} else {
		return 1.0f;
	}
}

/*
* CL_GameModule_NewSnapshot
*/
bool CL_GameModule_NewSnapshot( int pendingSnapshot ) {
	snapshot_t *currentSnap, *newSnap;

	if( cge ) {
		currentSnap = ( cl.currentSnapNum <= 0 ) ? NULL : &cl.snapShots[cl.currentSnapNum & UPDATE_MASK];
		newSnap = &cl.snapShots[pendingSnapshot & UPDATE_MASK];
		return CG_NewFrameSnap( newSnap, currentSnap );
	}

	return false;
}

/*
* CL_GameModule_RenderView
*/
void CL_GameModule_RenderView( float stereo_separation ) {
	if( cge && cls.cgameActive ) {
		CG_RenderView( cls.frametime, cls.realFrameTime, cls.realtime, cl.serverTime, stereo_separation,
						 cl_extrapolate->integer && !cls.demo.playing ? cl_extrapolationTime->integer : 0 );
	}
}

/*
* CL_GameModule_InputFrame
*/
void CL_GameModule_InputFrame( int frameTime ) {
	if( cge ) {
		CG_InputFrame( frameTime );
	}
}

/*
* CL_GameModule_ClearInputState
*/
void CL_GameModule_ClearInputState( void ) {
	if( cge ) {
		CG_ClearInputState();
	}
}

/*
* CL_GameModule_GetButtonBits
*/
unsigned CL_GameModule_GetButtonBits( void ) {
	if( cge ) {
		return CG_GetButtonBits();
	}
	return 0;
}

/*
* CL_GameModule_AddViewAngles
*/
void CL_GameModule_AddViewAngles( vec3_t viewAngles ) {
	if( cge ) {
		CG_AddViewAngles( viewAngles );
	}
}

/*
* CL_GameModule_AddMovement
*/
void CL_GameModule_AddMovement( vec3_t movement ) {
	if( cge ) {
		CG_AddMovement( movement );
	}
}

/*
* CL_GameModule_MouseMove
*/
void CL_GameModule_MouseMove( int dx, int dy ) {
	if( cge ) {
		CG_MouseMove( dx, dy );
	}
}

/*
* CL_GameModule_TouchEvent
*/
void CL_GameModule_TouchEvent( int id, touchevent_t type, int x, int y, int64_t time ) {
	if( cge ) {
		CG_TouchEvent( id, type, x, y, time );
	}
}

/*
* CL_GameModule_IsTouchDown
*/
bool CL_GameModule_IsTouchDown( int id ) {
	if( cge ) {
		return CG_IsTouchDown( id );
	}

	return false;
}
