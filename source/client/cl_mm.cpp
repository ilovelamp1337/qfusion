/*
Copyright (C) 2007 Will Franklin.

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

#include "../matchmaker/mm_common.h"
#include "../matchmaker/mm_query.h"

#include "../qalgo/base64.h"

#include <errno.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <functional>

/*
* private vars
*/

typedef enum {
	LOGIN_STATE_NONE = 0,
	LOGIN_STATE_WAITING = 1,
	LOGIN_STATE_READY = 2
} cl_mm_loginState_t;

typedef enum {
	LOGIN_RESPONSE_HANDLE = -1,
	LOGIN_RESPONSE_NONE = 0,
	LOGIN_RESPONSE_WAIT = 1,
	LOGIN_RESPONSE_READY = 2
} cl_mm_loginResponse_t;

// this will be true when subsystem is started
static bool cl_mm_initialized = false;
// this will be true when we are fully logged in
static bool cl_mm_enabled = false;
// flag set when logout process is finished
static bool cl_mm_logout_semaphore = false;
// flags set when steam authentication completes
static bool cl_mm_login_steam_complete = false;
// flags set when steam authentication completes
static bool cl_mm_login_steam_ok = false;
// heartbeat counter
static unsigned int cl_mm_last_heartbeat;

// login process fields
// loginstate: 0 - nothing, 1 - step 1 (waiting for handle)
//  2 - step 2 (waiting for validation from handle)

// #define MM_LOGIN2_INTERVAL		(1*1000)	// milliseconds

#define MM_STEAM_TICKET_AUTH_TIMEOUT    10 * 1000

static mm_uuid_t cl_mm_loginHandle;
static unsigned int cl_mm_loginState = 0;
static int64_t cl_mm_loginTime = 0;
static int cl_mm_loginRetries = 0;

static char *cl_mm_errmsg = NULL;
static size_t cl_mm_errmsg_size = 0;

static char *cl_mm_profile_url = NULL;
static char *cl_mm_profile_url_rml = NULL;

static uint64_t cl_mm_steam_id = 0;
static uint8_t *cl_mm_steam_token = NULL;
static size_t cl_mm_steam_token_len = 0;

// TODO: translate the cl_mm_url into netadr_t

mempool_t *cl_mm_mempool = 0;
#define MM_Alloc( n ) Mem_Alloc( cl_mm_mempool, ( n ) )
#define MM_Free( data ) Mem_Free( data )

/*
* public vars
*/
cvar_t *cl_mm_user;
cvar_t *cl_mm_session;
cvar_t *cl_mm_autologin;

/*
* prototypes
*/
static bool CL_MM_Login2( void );
static void CL_MM_ErrorMessage( bool printToConsole, const char *format, ... );

/*
* client ratings
*/

static clientRating_t *cl_ratingAlloc( const char *gametype, float rating, float deviation, mm_uuid_t uuid ) {
	clientRating_t *cr;

	cr = (clientRating_t*)MM_Alloc( sizeof( *cr ) );
	if( !cr ) {
		return NULL;
	}

	Q_strncpyz( cr->gametype, gametype, sizeof( cr->gametype ) - 1 );
	cr->rating = rating;
	cr->deviation = deviation;
	cr->next = 0;
	cr->uuid = uuid;

	return cr;
}

static void cl_mm_StringCopy( const char *in, char **pout ) {
	char *out;
	size_t in_size;

	out = *pout;
	if( out ) {
		MM_Free( out );
		*pout = NULL;
	}

	if( !in ) {
		return;
	}

	in_size = strlen( in ) + 1;
	out = (char *)MM_Alloc( in_size );
	strcpy( out, in );
	*pout = out;
}

static clientRating_t *cl_ratingCopy( clientRating_t *other ) {
	return cl_ratingAlloc( other->gametype, other->rating, other->deviation, other->uuid );
}

// free the list of clientRatings
static void cl_ratingsFree( clientRating_t *list ) {
	clientRating_t *next;

	while( list ) {
		next = list->next;
		Mem_Free( list );
		list = next;
	}
}

// This doesnt update ratings, only inserts new default rating if it doesnt exist
// if gametype is NULL, use current gametype
clientRating_t *CL_AddDefaultRating( const char *gametype ) {
	clientRating_t *cr;

	cr = Rating_Find( cls.ratings, gametype );
	if( cr != NULL ) {
		return cr;
	}

	cr = cl_ratingAlloc( gametype, MM_RATING_DEFAULT, MM_DEVIATION_DEFAULT, cls.mm_session );
	if( !cr ) {
		return NULL;
	}

	cr->next = cls.ratings;
	cls.ratings = cr;

	return cr;
}

// this inserts a new one, or updates the ratings if it exists
clientRating_t *CL_AddRating( const char *gametype, float rating, float deviation ) {
	clientRating_t *cr;

	cr = Rating_Find( cls.ratings, gametype );
	if( cr != NULL ) {
		cr->rating = rating;
		cr->deviation = deviation;
		return cr;
	}

	cr = cl_ratingAlloc( gametype, MM_RATING_DEFAULT, MM_DEVIATION_DEFAULT, cls.mm_session );
	if( !cr ) {
		return NULL;
	}

	cr->next = cls.ratings;
	cls.ratings = cr;

	return cr;
}

//=================================

static void CL_MM_Logout_f( void ) {
	CL_MM_Logout( false );
}

static void CL_MM_Login_f( void ) {
	const char *user = NULL, *password = NULL;

	// first figure out the user
	if( Cmd_Argc() > 1 ) {
		user = Cmd_Argv( 1 );
	}
	if( Cmd_Argc() > 2 ) {
		password = Cmd_Argv( 2 );
	}

	CL_MM_Login( user, password );
}

//===============================================

void CL_MM_Heartbeat( void ) {
	if( !cl_mm_enabled || !Uuid_IsValidSessionId( cls.mm_session ) ) {
		return;
	}

	// push a request
	QueryObject *query = QueryObject::NewPostQuery( "client/heartbeat" );
	if( !query ) {
		return;
	}

	query->SetServerSession( cls.mm_session );
	query->SendDeletingOnCompletion( []( QueryObject * ) {} );
}

static void CL_MM_ClientConnectDone( QueryObject *query ) {
	Com_DPrintf( "CL_MM_Connect: %s\n", query->RawResponse() );

	if( !query->HasSucceeded() ) {
		CL_MM_ErrorMessage( true, "MM Connect: request error" );
	} else if( !query->ResponseJsonRoot() ) {
		CL_MM_ErrorMessage( true, "MM Connect: failed to parse data" );
	} else {
		const char *ticketString = query->GetRootString( "ticket", "" );
		if( !Uuid_FromString( ticketString, &cls.mm_ticket ) ) {
			CL_MM_ErrorMessage( true, "MM Connect: malformed ticket string %s\n", ticketString );
		}
	}

	CL_SetClientState( CA_CONNECTING );

	char buffer[UUID_BUFFER_SIZE];
	Com_DPrintf( "CL_MM_Connect: Using ticket %s\n", Uuid_ToString( buffer, cls.mm_ticket ) );
}

bool CL_MM_Connect( const netadr_t *address ) {
	/*
	* ch : this here sends a ticket request to MM.
	* upon response we can set the client state to CA_CONNECTING
	* and actually connect to the gameserver
	*/
	// Com_Printf("CL_MM_Connect %s\n", NET_AddressToString( address ) );

	cls.mm_ticket = Uuid_ZeroUuid();

	// TODO: if not logged in, force login
	if( !cl_mm_enabled ) {
		return false;
	}

	QueryObject *query = QueryObject::NewPostQuery( "client/connect" );
	if( !query ) {
		return false;
	}

	query->SetClientSession( cls.mm_session );
	query->SetServerAddress( NET_AddressToString( address ) );
	query->SendDeletingOnCompletion( [=]( QueryObject *q ) { CL_MM_ClientConnectDone( q ); } );

	return true;
}

/*
* CL_MM_CanConnect
* returns true when we can connect to a game server
* (ie logged in to MM or MM is disabled)
*/
bool CL_MM_CanConnect( void ) {
	return ( cl_mm_loginState == LOGIN_STATE_NONE );
}

/*
* CL_MM_WaitForLogin( void )
* returns result of login
*/
bool CL_MM_WaitForLogin( void ) {
	while( !CL_MM_CanConnect() ) {
		QueryObject::Poll();
		CL_MM_Frame();
		Sys_Sleep( 20 );
	}

	return cl_mm_enabled;
}

void CL_MM_Frame( void ) {
	int64_t time;

	time = Sys_Milliseconds();

	if( cl_mm_loginState == LOGIN_STATE_READY && ( cl_mm_loginTime + MM_LOGIN2_INTERVAL ) <= time ) {
		if( cl_mm_loginRetries < MM_LOGIN2_RETRIES ) {
			// (Re)enter step 2 of login process
			Com_DPrintf( "Fetching login authentication\n" );
			CL_MM_Login2();
		} else {
			// reset login
			cl_mm_loginState = LOGIN_STATE_NONE;
			CL_MM_ErrorMessage( true, "MM Login: authentication timeout" );
		}
	}

	if( cl_mm_enabled ) {
		if( cl_mm_logout_semaphore ) {
			// logout process is finished so we can shutdown game
			// CL_MM_Shutdown( false );
			cl_mm_logout_semaphore = false;
			return;
		}

		// heartbeat
		if( ( cl_mm_last_heartbeat + MM_HEARTBEAT_INTERVAL ) < time ) {
			CL_MM_Heartbeat();
			cl_mm_last_heartbeat = time;
		}
	}
}

/*
* CL_MM_Initialized
*/
bool CL_MM_Initialized( void ) {
	return cl_mm_enabled;
}

static void CL_MM_LogoutDone( QueryObject * ) {
	Com_DPrintf( "MM Logout: Logged off..\n" );

	// ignore response-status and just mark us as logged-out
	cl_mm_logout_semaphore = true;
	cl_mm_enabled = false;
	cl_mm_loginState = LOGIN_STATE_NONE;
	cls.mm_session = Uuid_ZeroUuid();
	cl_mm_StringCopy( nullptr, &cl_mm_profile_url );
	cl_mm_StringCopy( nullptr, &cl_mm_profile_url_rml );
}

/*
* CL_MM_Logout
*/
bool CL_MM_Logout( bool force ) {
	if( !cl_mm_enabled || !Uuid_IsValidSessionId( cls.mm_session ) ) {
		CL_MM_ErrorMessage( true, "MM Logout: not logged in" );
		return false;
	}

	// TODO: check clientstate, has to be unconnected
	if( CL_GetClientState() > CA_DISCONNECTED ) {
		CL_MM_ErrorMessage( true, "MM Logout: can't logout from MM while connected to server" );
		return false;
	}

	QueryObject *query = QueryObject::NewPostQuery( "client/logout" );
	if( !query ) {
		return false;
	}

	cl_mm_logout_semaphore = false;

	// TODO: pull the authkey out of cvar into file
	query->SetClientSession( cls.mm_session );
	query->SendDeletingOnCompletion( [=]( QueryObject *q ) { CL_MM_LogoutDone( q ); } );

	if( !force ) {
		return true;
	}

	bool result = true;
	auto timeout = Sys_Milliseconds();
	while( !cl_mm_logout_semaphore && Sys_Milliseconds() < ( timeout + MM_LOGOUT_TIMEOUT ) ) {
		QueryObject::Poll();
		Sys_Sleep( 10 );
	}

	result = cl_mm_logout_semaphore;
	if( !cl_mm_logout_semaphore ) {
		CL_MM_ErrorMessage( true, "MM Logout: Failed to force logout" );
	} else {
		Com_DPrintf( "CL_MM_Logout: force logout successful\n" );
	}

	cl_mm_logout_semaphore = false;

	// dont call this, we are coming from shutdown
	// CL_MM_Shutdown( false );

	return result;
}

struct ScopeGuard {
	const std::function<void()> &atExit;
	bool suppressed { false };

	explicit ScopeGuard( const std::function<void()> & atExit_ ) : atExit( atExit_ ) {}

	~ScopeGuard() {
		if( !suppressed ) {
			atExit();
		}
	}

	void Suppress() { suppressed = true; }
};

static void CL_MM_LoginDone( QueryObject *query ) {
	char buffer[UUID_BUFFER_SIZE];

	if( cl_mm_loginState == LOGIN_STATE_NONE ) {
		Com_DPrintf( "cl_mm_login_done called when no login in process!\n" );
		return;
	}

	cl_mm_enabled = false;
	cls.mm_session = Uuid_ZeroUuid();
	Cvar_ForceSet( cl_mm_session->name, "" );

	if( !query->HasSucceeded() ) {
		// TODO: reset login
		CL_MM_ErrorMessage( true, "MM Login: StatQuery error" );
		cl_mm_loginState = LOGIN_STATE_NONE;
		return;
	}

	Com_DPrintf( "MM Login: %s\n", query->RawResponse() );

	ScopeGuard failureGuard([&]() {
		CL_MM_ErrorMessage( true, "MM Login failure" );
		cl_mm_loginHandle = Uuid_ZeroUuid();
		cl_mm_loginState = LOGIN_STATE_NONE;
	});

	if( !query->ResponseJsonRoot() ) {
		const char *msg = "MM Login: Failed to parse data at step %d";
		const char *translated = L10n_TranslateString( "common", msg );

		if( !translated ) {
			translated = msg;
		}

		CL_MM_ErrorMessage( true, va( translated, cl_mm_loginState ) );
		return;
	}

	const auto status = query->GetRootDouble( "status", 0 );
	if( status == 0 ) {
		const char *error = query->GetRootString( "error", "" );
		if( *error ) {
			Com_Printf( "MM Login: Request error at remote host: %s\n", error );
		} else {
			Com_Printf( "MM_Login: Bad or missing response status\n" );
		}
		return;
	}

	const auto ready = (int)query->GetRootDouble( "ready", LOGIN_RESPONSE_NONE );
	if( ready == LOGIN_RESPONSE_NONE ) {
		Com_DPrintf( "MM Login: Bad response ready value\n" );
		return;
	}

	if( cl_mm_loginState == LOGIN_STATE_WAITING ) {
		// here we are expecting a handle to the validation process
		if( ready == LOGIN_RESPONSE_HANDLE ) {
			// we can move to step 2
			if( !Uuid_FromString( query->GetRootString( "handle", "" ), &cl_mm_loginHandle ) ) {
				Com_DPrintf( "MM Login: Can't parse a handle\n" );
				return;
			}
			cl_mm_loginState = LOGIN_STATE_READY;
			cl_mm_loginTime = Sys_Milliseconds() /* - MM_LOGIN2_INTERVAL */;
		} else {
			Com_DPrintf( "MM Login: There is no handle\n" );
			return;
		}
	} else if( cl_mm_loginState == LOGIN_STATE_READY && ready == LOGIN_RESPONSE_READY ) {
		mm_uuid_t session_id = Uuid_ZeroUuid();
		const char *idString = query->GetRootString( "session_id", "" );
		if( !Uuid_FromString( idString, &session_id ) ) {
			Com_DPrintf( "MM Login: Can't parse session id\n" );
			return;
		}
		if( !Uuid_IsValidSessionId( session_id ) ) {
			Com_DPrintf( "MM Login: Invalid session id\n" );
			return;
		}

		// Save the session id
		cls.mm_session = session_id;
		// Stop login process
		cl_mm_loginState = LOGIN_STATE_NONE;
		// Switch to "logged in" state
		cl_mm_enabled = true;

		// See remarks of necessity of using std::optional in the corresponding server code...

		if( const cJSON *rootSection = query->ResponseJsonRoot() ) {
			ObjectReader rootReader( rootSection );
			if( const cJSON *ratingsSection = rootReader.GetArray( "ratings" ) ) {
				ArrayReader ratingsReader( ratingsSection );
				while( !ratingsReader.IsDone() ) {
					if( !ratingsReader.IsAtObject() ) {
						break;
					}
					ObjectReader entryReader( ratingsReader.GetChildObject() );
					const char *gametype = entryReader.GetString( "gametype" );
					const double rating = entryReader.GetDouble( "rating" );
					const double deviation = entryReader.GetDouble( "deviation" );
					if( !gametype || std::isnan( rating ) || std::isnan( deviation ) ) {
						break;
					}
					CL_AddRating( gametype, (float)rating, (float)deviation );
				}
			}
			if( const cJSON *infoSection = rootReader.GetObject( "player_info" ) ) {
				ObjectReader infoReader( infoSection );
				const char *profileWebUrl = infoReader.GetString( "profile_web_url", "" );
				const char *profileRmlUrl = infoReader.GetString( "profile_rml_url", profileWebUrl );
				cl_mm_StringCopy( profileWebUrl, &cl_mm_profile_url );
				cl_mm_StringCopy( profileRmlUrl, &cl_mm_profile_url_rml );
				const char *lastLoginAddress = infoReader.GetString( "last_login_ip", "N/A" );
				const char *lastLoginTimestamp = infoReader.GetString( "last_login_timestamp", "N/A" );
				Com_Printf("Last logged in from %s at %s\n", lastLoginAddress, lastLoginTimestamp );
			}
		}
	} else if( cl_mm_loginState == LOGIN_STATE_READY && status == LOGIN_RESPONSE_WAIT ) {
		Com_DPrintf( "MM Login: Waiting for handle %s", cl_mm_loginHandle.ToString( buffer ) );
	}

	if( cl_mm_loginState == LOGIN_STATE_NONE ) {
		// Should be reached if and only if the session is valid
		assert( Uuid_IsValidSessionId( cls.mm_session ) );
		CL_MM_ErrorMessage( false, "" );
		cls.mm_session.ToString( buffer );
		Com_DPrintf( "MM Login: Success, session id %s\n", buffer );
		Cvar_ForceSet( cl_mm_session->name, buffer );
	}

	failureGuard.Suppress();
}

/*
* CL_MM_Login2
* step 2 of login process
*/
static bool CL_MM_Login2() {
	/*
	* step 2 of login process, poll the login auth with
	* the handle received in step 1
	*/

	// mm.. does this fail or what?
	if( cl_mm_loginState != LOGIN_STATE_READY || cl_mm_enabled ) {
		Com_Printf( "CL_MM_Login2: quitting early\n" );
		return false;
	}

	QueryObject *query = QueryObject::NewPostQuery( "client/login" );
	if( !query ) {
		return false;
	}

	cl_mm_loginState = LOGIN_STATE_READY;
	cl_mm_loginTime = Sys_Milliseconds();
	cl_mm_loginRetries++;

	query->SetHandle( cl_mm_loginHandle );
	query->SendDeletingOnCompletion( [=]( QueryObject *q ) { CL_MM_LoginDone( q ); } );

	return true;
}

/*
* CL_MM_LoginReal
*/
static bool CL_MM_LoginReal( const char *user, const char *password ) {
	/*
	* step 1 of login process, send the login+passwd to the server,
	* receive handle to the login process.
	* After we return, cl_mm will poll requests in step 2
	* (CL_MM_Login2)
	*/
	if( cl_mm_loginState >= LOGIN_STATE_WAITING || cl_mm_enabled ) {
		return false;
	}

	// TODO: check clientstate, has to be unconnected
	if( CL_GetClientState() > CA_DISCONNECTED ) {
		CL_MM_ErrorMessage( true, "MM Login: Can't login to MM while connected to server" );
		return false;
	}

	// TODO: validate the parameters
	QueryObject *query = QueryObject::NewPostQuery( "client/login" );
	if( !query ) {
		return false;
	}

	Com_DPrintf( "Logging in with %s %s\n", user, password );

	cl_mm_loginState = LOGIN_STATE_WAITING;
	cl_mm_loginRetries = 0;
	cl_mm_loginTime = Sys_Milliseconds();
	cl_mm_StringCopy( nullptr, &cl_mm_profile_url );
	cl_mm_StringCopy( nullptr, &cl_mm_profile_url_rml );

	query->SetLogin( user );
	query->SetPassword( password );
	query->SendDeletingOnCompletion( [=]( QueryObject *q ) { CL_MM_LoginDone( q ); } );

	return true;
}

/*
* CL_MM_LoginSteamCb
*/
static void CL_MM_LoginSteamCb( void *data, size_t len ) {
	if( cl_mm_loginState >= LOGIN_STATE_WAITING || cl_mm_enabled ) {
		// already authed or in process
		return;
	}

	cl_mm_steam_token = (uint8_t *)MM_Alloc( len + 1 );
	memcpy( cl_mm_steam_token, data, len );
	cl_mm_steam_token[len] = '\0';
	cl_mm_steam_token_len = len;

	cl_mm_login_steam_complete = true;
	if( len > 0 ) {
		cl_mm_login_steam_ok = true;
	}
}

/*
* CL_MM_LoginSteam
*/
static bool CL_MM_LoginSteam( void ) {
	int64_t start_time;

	if( cl_mm_loginState >= LOGIN_STATE_WAITING || cl_mm_enabled ) {
		// already authed or in process
		return false;
	}

	cl_mm_login_steam_complete = false;
	cl_mm_login_steam_ok = false;

	if( cl_mm_steam_token != NULL ) {
		Mem_Free( cl_mm_steam_token );
		cl_mm_steam_token = NULL;
	}

	if( Steam_GetAuthSessionTicket( &CL_MM_LoginSteamCb ) == 0 ) {
		return false;
	}

	// wait for GetAuthSessionTicket callback but not for too long
	start_time = Sys_Milliseconds();
	while( !cl_mm_login_steam_complete ) {
		if( Sys_Milliseconds() > start_time + MM_STEAM_TICKET_AUTH_TIMEOUT ) {
			break;
		}
		Steam_RunFrame();
	}

	return cl_mm_login_steam_ok;
}

/*
* CL_MM_Login
*/
bool CL_MM_Login( const char *user, const char *password ) {
	if( cl_mm_steam_id != 0 ) {
		if( CL_MM_LoginSteam() ) {
			CL_MM_LoginReal( NULL, NULL );
			return true;
		}
	}

	// first figure out the user
	if( !user || user[0] == '\0' ) {
		user = cl_mm_user->string;
	} else {
		if( cl_mm_autologin->integer ) {
			Cvar_ForceSet( "cl_mm_user", user );
		}
	}

	if( user[0] == '\0' ) {
		return false;
	}

	// TODO: nicer error announcing
	if( !password || password[0] == '\0' ) {
		password = MM_PasswordRead( user );
	} else {
		if( cl_mm_autologin->integer ) {
			MM_PasswordWrite( user, password );
		}
	}

	if( password == NULL ) {
		CL_MM_ErrorMessage( true, "MM Login: Password error" );
		return false;
	}

	return CL_MM_LoginReal( user, password );
}

/*
* CL_MM_GetLoginState
*/
int CL_MM_GetLoginState( void ) {
	if( cl_mm_loginState == LOGIN_STATE_NONE ) {
		return ( cl_mm_enabled == true ? MM_LOGIN_STATE_LOGGED_IN : MM_LOGIN_STATE_LOGGED_OUT );
	}
	return MM_LOGIN_STATE_IN_PROGRESS;
}

/*
* CL_MM_GetLastErrorMessage
* Copies last MM error message
*/
size_t CL_MM_GetLastErrorMessage( char *buffer, size_t buffer_size ) {
	if( !buffer || !buffer_size ) {
		return 0;
	}
	if( !cl_mm_errmsg || !*cl_mm_errmsg ) {
		*buffer = '\0';
		return 0;
	}

	Q_strncpyz( buffer, cl_mm_errmsg, buffer_size );
	return cl_mm_errmsg_size - 1;
}

/*
* CL_MM_GetProfileURL
* Copies player's profile URL we've previously received from MM server
*/
size_t CL_MM_GetProfileURL( char *buffer, size_t buffer_size, bool rml ) {
	const char *profile_url = rml ? cl_mm_profile_url_rml : cl_mm_profile_url;

	if( !buffer || !buffer_size ) {
		return 0;
	}
	if( !profile_url || !*profile_url ) {
		*buffer = '\0';
		return 0;
	}

	Q_strncpyz( buffer, profile_url, buffer_size );
	return strlen( profile_url );
}

/*
* CL_MM_GetBaseWebURL
*/
size_t CL_MM_GetBaseWebURL( char *buffer, size_t buffer_size ) {
	const char *web_url = APP_MATCHMAKER_WEB_URL;

	if( !buffer || !buffer_size ) {
		return 0;
	}
	if( !web_url || !*web_url ) {
		*buffer = '\0';
		return 0;
	}

	Q_strncpyz( buffer, web_url, buffer_size );
	return strlen( web_url );
}

/*
* CL_MM_ErrorMessage
* Stores error message in local buffer and optionally prints it to console
*/
static void CL_MM_ErrorMessage( bool printToConsole, const char *format, ... ) {
	va_list argptr;
	char string[2048];
	size_t string_len;

	va_start( argptr, format );
	string_len = Q_vsnprintfz( string, sizeof( string ), format, argptr );
	va_end( argptr );

	if( string_len >= cl_mm_errmsg_size ) {
		if( cl_mm_errmsg ) {
			MM_Free( cl_mm_errmsg );
		}
		cl_mm_errmsg_size = string_len + 1;
		cl_mm_errmsg = (char *)MM_Alloc( cl_mm_errmsg_size );
	}

	strcpy( cl_mm_errmsg, string );

	if( printToConsole ) {
		Com_Printf( "%s\n", cl_mm_errmsg );
	}
}

void CL_MM_Init( void ) {
	if( cl_mm_initialized ) {
		return;
	}

	cl_mm_enabled = false;

	cls.mm_session = Uuid_ZeroUuid();

	cl_mm_loginHandle = Uuid_ZeroUuid();
	cl_mm_loginState = LOGIN_STATE_NONE;
	cl_mm_loginTime = 0;
	cl_mm_loginRetries = 0;

	if( !cl_mm_mempool ) {
		cl_mm_errmsg = NULL;
		cl_mm_errmsg_size = 0;
	}

	if( !cl_mm_mempool ) {
		cl_mm_mempool = Mem_AllocPool( NULL, "cl_mm" );
	}

	/*
	* create cvars
	*/
	cl_mm_session = Cvar_Get( "cl_mm_session", "", CVAR_READONLY | CVAR_USERINFO );
	cl_mm_autologin = Cvar_Get( "cl_mm_autologin", "1", CVAR_ARCHIVE );

	// TODO: remove as cvar
	cl_mm_user = Cvar_Get( "cl_mm_user", "", CVAR_ARCHIVE );

	/*
	* add commands
	*/
	Cmd_AddCommand( "mm_login", CL_MM_Login_f );
	Cmd_AddCommand( "mm_logout", CL_MM_Logout_f );

	Cvar_ForceSet( cl_mm_session->name, "" );

	/*
	* steam
	*/
	cl_mm_steam_id = Steam_GetSteamID();

	/*
	* login
	*/
	if( cl_mm_autologin->integer || cl_mm_steam_id != 0 ) {
		CL_MM_Login( NULL, NULL );
	}

	cl_mm_initialized = true;
}

void CL_MM_Shutdown( bool logout ) {
	if( !cl_mm_initialized ) {
		return;
	}

	if( logout && cl_mm_enabled ) {
		// logout is always forced at this stage
		CL_MM_Logout( true );
	}

	Cvar_ForceSet( cl_mm_session->name, "" );

	Cmd_RemoveCommand( "mm_login" );
	Cmd_RemoveCommand( "mm_logout" );

	cls.mm_session = Uuid_ZeroUuid();
	cls.mm_ticket = Uuid_ZeroUuid();

	cl_mm_loginHandle = Uuid_ZeroUuid();
	cl_mm_loginState = LOGIN_STATE_NONE;
	cl_mm_loginTime = 0;
	cl_mm_loginRetries = 0;

	Mem_FreePool( &cl_mm_mempool );
	cl_mm_mempool = 0;

	cl_mm_errmsg = NULL;
	cl_mm_errmsg_size = 0;

	cl_mm_profile_url = NULL;
	cl_mm_profile_url_rml = NULL;

	cl_mm_steam_token = NULL;

	cl_mm_initialized = false;
}
