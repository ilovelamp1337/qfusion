/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// common.c -- misc functions used in client and server
#include "qcommon.h"
#if defined(__GNUC__) && defined(i386)
#include <cpuid.h>
#endif
#include <setjmp.h>
#include "wswcurl.h"
#include "steam.h"
#include "../qalgo/glob.h"
#include "../qalgo/md5.h"
#include "../matchmaker/mm_common.h"

#define MAX_NUM_ARGVS	50

static qboolean	dynvars_initialized = qfalse;
static qboolean	commands_intialized = qfalse;

static int com_argc;
static char *com_argv[MAX_NUM_ARGVS+1];
static char com_errormsg[MAX_PRINTMSG];

static jmp_buf abortframe;     // an ERR_DROP occured, exit the entire frame

cvar_t *host_speeds;
cvar_t *log_stats;
cvar_t *developer;
cvar_t *timescale;
cvar_t *dedicated;
cvar_t *versioncvar;
cvar_t *revisioncvar;
cvar_t *tv_server;
cvar_t *mm_server;

static cvar_t *fixedtime;
static cvar_t *logconsole = NULL;
static cvar_t *logconsole_append;
static cvar_t *logconsole_flush;
static cvar_t *logconsole_timestamp;
static cvar_t *com_showtrace;
static cvar_t *com_introPlayed3;

static qmutex_t *com_print_mutex;

int log_stats_file = 0;
static int log_file = 0;

static int server_state = CA_UNINITIALIZED;
static int client_state = CA_UNINITIALIZED;
static qboolean	demo_playing = qfalse;

struct cmodel_state_s *server_cms = NULL;
unsigned server_map_checksum = 0;

// host_speeds times
unsigned int time_before_game;
unsigned int time_after_game;
unsigned int time_before_ref;
unsigned int time_after_ref;

/*
==============================================================

DYNVARS

==============================================================
*/

static dynvar_get_status_t Com_Sys_Uptime_f( void **val )
{
	static char buf[32];
	const quint64 us = Sys_Microseconds();
	const unsigned int h = us / 3600000000u;
	const unsigned int min = ( us / 60000000 ) % 60;
	const unsigned int sec = ( us / 1000000 ) % 60;
	const unsigned int usec = us % 1000000;
	sprintf( buf, "%02u:%02u:%02u.%06u", h, min, sec, usec );
	*val = buf;
	return DYNVAR_GET_OK;
}

/*
==============================================================

Commands

==============================================================
*/

#ifdef SYS_SYMBOL
static void Com_Sys_Symbol_f( void )
{
	const int argc = Cmd_Argc();
	if( argc == 2 || argc == 3 )
	{
		const char *const symbolName = Cmd_Argv( 1 );
		const char *const moduleName = Cmd_Argc() == 3
			? Cmd_Argv( 2 )
			: NULL;
		void *symbolAddr = Sys_GetSymbol( moduleName, symbolName );
		if( symbolAddr )
		{
			Com_Printf( "\"%s\" is \"0x%p\"\n", symbolName, symbolAddr );
		}
		else
			Com_Printf( "Error: Symbol not found\n" );
	}
	else
		Com_Printf( "usage: sys_symbol <symbolName> [moduleName]\n" );
}
#endif // SYS_SYMBOL

/*
============================================================================

CLIENT / SERVER interactions

============================================================================
*/

static int rd_target;
static char *rd_buffer;
static int rd_buffersize;
static void ( *rd_flush )( int target, const char *buffer, const void *extra );
static const void *rd_extra;

void Com_BeginRedirect( int target, char *buffer, int buffersize, 
	void ( *flush )(int, const char*, const void*), const void *extra )
{
	if( !target || !buffer || !buffersize || !flush )
		return;
	rd_target = target;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;
	rd_extra = extra;

	*rd_buffer = 0;
}

void Com_EndRedirect( void )
{
	rd_flush( rd_target, rd_buffer, rd_extra );

	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
	rd_extra = NULL;
}

/*
* Com_Printf
* 
* Both client and server can use this, and it will output
* to the apropriate place.
*/
void Com_Printf( const char *format, ... )
{
	va_list	argptr;
	char msg[MAX_PRINTMSG];

	time_t timestamp;
	char timestamp_str[MAX_PRINTMSG];
	struct tm *timestampptr;
	timestamp = time( NULL );
	timestampptr = gmtime( &timestamp );
	strftime( timestamp_str, MAX_PRINTMSG, "%Y-%m-%dT%H:%M:%SZ ", timestampptr );

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	if( rd_target )
	{
		if( (int)( strlen( msg ) + strlen( rd_buffer ) ) > ( rd_buffersize - 1 ) )
		{
			rd_flush( rd_target, rd_buffer, rd_extra );
			*rd_buffer = 0;
		}
		strcat( rd_buffer, msg );
		return;
	}

	QMutex_Lock( com_print_mutex );

	Con_Print( msg );

	// also echo to debugging console
	Sys_ConsoleOutput( msg );

	// logconsole
	if( logconsole && logconsole->modified )
	{
		logconsole->modified = qfalse;

		if( log_file )
		{
			FS_FCloseFile( log_file );
			log_file = 0;
		}

		if( logconsole->string && logconsole->string[0] )
		{
			size_t name_size;
			char *name;

			name_size = strlen( logconsole->string ) + strlen( ".log" ) + 1;
			name = ( char* )Mem_TempMalloc( name_size );
			Q_strncpyz( name, logconsole->string, name_size );
			COM_DefaultExtension( name, ".log", name_size );

			if( FS_FOpenFile( name, &log_file, ( logconsole_append && logconsole_append->integer ? FS_APPEND : FS_WRITE ) ) == -1 )
			{
				log_file = 0;
				Com_Printf( "Couldn't open: %s\n", name );
			}

			Mem_TempFree( name );
		}
	}

	if( log_file )
	{
		if( logconsole_timestamp && logconsole_timestamp->integer )
			FS_Printf( log_file, "%s", timestamp_str );
		FS_Printf( log_file, "%s", msg );
		if( logconsole_flush && logconsole_flush->integer )
			FS_Flush( log_file ); // force it to save every time
	}

	QMutex_Unlock( com_print_mutex );
}


/*
* Com_DPrintf
* 
* A Com_Printf that only shows up if the "developer" cvar is set
*/
void Com_DPrintf( const char *format, ... )
{
	va_list	argptr;
	char msg[MAX_PRINTMSG];

	if( !developer || !developer->integer )
		return; // don't confuse non-developers with techie stuff...

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}


/*
* Com_Error
* 
* Both client and server can use this, and it will
* do the apropriate things.
*/
void Com_Error( com_error_code_t code, const char *format, ... )
{
	va_list	argptr;
	char *msg = com_errormsg;
	const size_t sizeof_msg = sizeof( com_errormsg );
	static qboolean	recursive = qfalse;

	if( recursive )
	{
		Com_Printf( "recursive error after: %s", msg ); // wsw : jal : log it
		Sys_Error( "recursive error after: %s", msg );
	}
	recursive = qtrue;

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof_msg, format, argptr );
	va_end( argptr );

	if( code == ERR_DROP )
	{
		Com_Printf( "********************\nERROR: %s\n********************\n", msg );
		SV_ShutdownGame( va( "Server crashed: %s\n", msg ), qfalse );
		CL_Disconnect( msg );
		recursive = qfalse;
		longjmp( abortframe, -1 );
	}
	else
	{
		Com_Printf( "********************\nERROR: %s\n********************\n", msg );
		SV_Shutdown( va( "Server fatal crashed: %s\n", msg ) );
		CL_Shutdown();
		MM_Shutdown();
	}

	if( log_file )
	{
		FS_FCloseFile( log_file );
		log_file = 0;
	}

	Sys_Error( "%s", msg );
}


/*
* Com_Quit
* 
* Both client and server can use this, and it will
* do the apropriate things.
*/
void Com_Quit( void )
{
	if( dynvars_initialized )
	{
		dynvar_t *quit = Dynvar_Lookup( "quit" );
		if( quit )
		{
			// wsw : aiwa : added "quit" event for pluggable clean-up (e.g. IRC shutdown)
			Dynvar_CallListeners( quit, NULL );
		}
		Dynvar_Destroy( quit );
	}

	SV_Shutdown( "Server quit\n" );
	CL_Shutdown();
	MM_Shutdown();

	if( log_file )
	{
		FS_FCloseFile( log_file );
		log_file = 0;
	}

	Sys_Quit();
}


/*
* Com_ServerState
*/
int Com_ServerState( void )
{
	return server_state;
}

/*
* Com_SetServerState
*/
void Com_SetServerState( int state )
{
	server_state = state;
}

/*
* Com_ServerCM
*/
struct cmodel_state_s *Com_ServerCM( unsigned *checksum )
{
	*checksum = server_map_checksum;
	return server_cms;
}

/*
* Com_SetServerCM
*/
void Com_SetServerCM( struct cmodel_state_s *cms, unsigned checksum )
{
	server_cms = cms;
	server_map_checksum = checksum;
}

int Com_ClientState( void )
{
	return client_state;
}

void Com_SetClientState( int state )
{
	client_state = state;
}

qboolean Com_DemoPlaying( void )
{
	return demo_playing;
}

void Com_SetDemoPlaying( qboolean state )
{
	demo_playing = state;
}

unsigned int Com_DaysSince1900( void )
{
	time_t long_time;
	struct tm *newtime;

	// get date from system
	time( &long_time );
	newtime = localtime( &long_time );

	return ( newtime->tm_year * 365 ) + newtime->tm_yday;
}

//============================================================================

/*
* COM_CheckParm
* 
* Returns the position (1 to argc-1) in the program's argument list
* where the given parameter apears, or 0 if not present
*/
int COM_CheckParm( char *parm )
{
	int i;

	for( i = 1; i < com_argc; i++ )
	{
		if( !strcmp( parm, com_argv[i] ) )
			return i;
	}

	return 0;
}

int COM_Argc( void )
{
	return com_argc;
}

const char *COM_Argv( int arg )
{
	if( arg < 0 || arg >= com_argc || !com_argv[arg] )
		return "";
	return com_argv[arg];
}

void COM_ClearArgv( int arg )
{
	if( arg < 0 || arg >= com_argc || !com_argv[arg] )
		return;
	com_argv[arg][0] = '\0';
}


/*
* COM_InitArgv
*/
void COM_InitArgv( int argc, char **argv )
{
	int i;

	if( argc > MAX_NUM_ARGVS )
		Com_Error( ERR_FATAL, "argc > MAX_NUM_ARGVS" );
	com_argc = argc;
	for( i = 0; i < argc; i++ )
	{
		if( !argv[i] || strlen( argv[i] ) >= MAX_TOKEN_CHARS )
			com_argv[i][0] = '\0';
		else
			com_argv[i] = argv[i];
	}
}

/*
* COM_AddParm
* 
* Adds the given string at the end of the current argument list
*/
void COM_AddParm( char *parm )
{
	if( com_argc == MAX_NUM_ARGVS )
		Com_Error( ERR_FATAL, "COM_AddParm: MAX_NUM_ARGVS" );
	com_argv[com_argc++] = parm;
}

int Com_GlobMatch( const char *pattern, const char *text, const qboolean casecmp )
{
	return glob_match( pattern, text, casecmp );
}

char *_ZoneCopyString( const char *str, const char *filename, int fileline )
{
	return _Mem_CopyString( zoneMemPool, str, filename, fileline );
}

char *_TempCopyString( const char *str, const char *filename, int fileline )
{
	return _Mem_CopyString( tempMemPool, str, filename, fileline );
}

void Info_Print( char *s )
{
	char key[512];
	char value[512];
	char *o;
	int l;

	if( *s == '\\' )
		s++;
	while( *s )
	{
		o = key;
		while( *s && *s != '\\' )
			*o++ = *s++;

		l = o - key;
		if( l < 20 )
		{
			memset( o, ' ', 20-l );
			key[20] = 0;
		}
		else
			*o = 0;
		Com_Printf( "%s", key );

		if( !*s )
		{
			Com_Printf( "MISSING VALUE\n" );
			return;
		}

		o = value;
		s++;
		while( *s && *s != '\\' )
			*o++ = *s++;
		*o = 0;

		if( *s )
			s++;
		Com_Printf( "%s\n", value );
	}
}

//============================================================================

/*
* Com_PageInMemory
*/
int paged_total;

void Com_PageInMemory( qbyte *buffer, int size )
{
	int i;

	for( i = size-1; i > 0; i -= 4096 )
		paged_total += buffer[i];
}

//============================================================================

/*
* Com_AddPurePakFile
*/
void Com_AddPakToPureList( purelist_t **purelist, const char *pakname, const unsigned checksum, mempool_t *mempool )
{
	purelist_t *purefile;
	const size_t len = strlen( pakname ) + 1;

	purefile = ( purelist_t* )Mem_Alloc( mempool ? mempool : zoneMemPool, sizeof( purelist_t ) + len );
	purefile->filename = ( char * )(( qbyte * )purefile + sizeof( *purefile ));
	memcpy( purefile->filename, pakname, len );
	purefile->checksum = checksum;
	purefile->next = *purelist;
	*purelist = purefile;
}

/*
* Com_CountPureListFiles
*/
unsigned Com_CountPureListFiles( purelist_t *purelist )
{
	unsigned numpure;
	purelist_t *iter;

	numpure = 0;
	iter = purelist;
	while( iter )
	{
		numpure++;
		iter = iter->next;
	}

	return numpure;
}

/*
* Com_FindPakInPureList
*/
purelist_t *Com_FindPakInPureList( purelist_t *purelist, const char *pakname )
{
	purelist_t *purefile = purelist;

	while( purefile )
	{
		if( !strcmp( purefile->filename, pakname ) )
			break;
		purefile = purefile->next;
	}

	return purefile;
}

/*
* Com_FreePureList
*/
void Com_FreePureList( purelist_t **purelist )
{
	purelist_t *purefile = *purelist;

	while( purefile )
	{
		purelist_t *next = purefile->next;
		Mem_Free( purefile );
		purefile = next;
	}

	*purelist = NULL;
}

//============================================================================

static unsigned int com_CPUFeatures = 0xFFFFFFFF;

static inline int CPU_haveCPUID()
{
	int has_CPUID = 0;
#if defined(__GNUC__) && defined(i386)
	has_CPUID = __get_cpuid_max( 0, NULL ) ? 1 : 0;
#elif defined(_MSC_VER) && defined(_M_IX86)
	__asm {
		pushfd                      ; Get original EFLAGS
			pop     eax
			mov     ecx, eax
			xor     eax, 200000h        ; Flip ID bit in EFLAGS
			push    eax                 ; Save new EFLAGS value on stack
			popfd                       ; Replace current EFLAGS value
			pushfd                      ; Get new EFLAGS
			pop     eax                 ; Store new EFLAGS in EAX
			xor     eax, ecx            ; Can not toggle ID bit,
			jz      done                ; Processor=80486
			mov     has_CPUID,1         ; We have CPUID support
done:
	}
#endif
	return has_CPUID;
}

static inline int CPU_getCPUIDFeatures()
{
	int features = 0;
#if defined(__GNUC__) && defined(i386)
	if( __get_cpuid_max( 0, NULL ) >= 1 ) {
		int temp, temp2, temp3;
		__get_cpuid( 1, &temp, &temp2, &temp3, &features );
	}
#elif defined(_MSC_VER) && defined(_M_IX86)
	__asm {
		xor     eax, eax            ; Set up for CPUID instruction
			cpuid                       ; Get and save vendor ID
			cmp     eax, 1              ; Make sure 1 is valid input for CPUID
			jl      done                ; We dont have the CPUID instruction
			xor     eax, eax
			inc     eax
			cpuid                       ; Get family/model/stepping/features
			mov     features, edx
done:
	}
#endif
	return features;
}

static inline int CPU_getCPUIDFeaturesExt()
{
	int features = 0;
#if defined(__GNUC__) && defined(i386)
	if( __get_cpuid_max( 0x80000000, NULL ) >= 0x80000001 ) {
		int temp, temp2, temp3;
		__get_cpuid( 0x80000001, &temp, &temp2, &temp3, &features );
	}
#elif defined(_MSC_VER) && defined(_M_IX86)
	__asm {
		mov     eax,80000000h       ; Query for extended functions
			cpuid                       ; Get extended function limit
			cmp     eax,80000001h
			jl      done                ; Nope, we dont have function 800000001h
			mov     eax,80000001h       ; Setup extended function 800000001h
			cpuid                       ; and get the information
			mov     features,edx
done:
	}
#endif
	return features;
}

/*
* COM_CPUFeatures
*
* CPU features detection code, taken from SDL
*/
unsigned int COM_CPUFeatures( void )
{
	if( com_CPUFeatures == 0xFFFFFFFF )
	{
		com_CPUFeatures = 0;

		if( CPU_haveCPUID() )
		{
			int CPUIDFeatures = CPU_getCPUIDFeatures();
			int CPUIDFeaturesExt = CPU_getCPUIDFeaturesExt();

			if( CPUIDFeatures & 0x00000010 )
				com_CPUFeatures |= QCPU_HAS_RDTSC;
			if( CPUIDFeatures & 0x00800000 )
				com_CPUFeatures |= QCPU_HAS_MMX;
			if( CPUIDFeaturesExt & 0x00400000 )
				com_CPUFeatures |= QCPU_HAS_MMXEXT;
			if( CPUIDFeaturesExt & 0x80000000 )
				com_CPUFeatures |= QCPU_HAS_3DNOW;
			if( CPUIDFeaturesExt & 0x40000000 )
				com_CPUFeatures |= QCPU_HAS_3DNOWEXT;
			if( CPUIDFeatures & 0x02000000 )
				com_CPUFeatures |= QCPU_HAS_SSE;
			if( CPUIDFeatures & 0x04000000 )
				com_CPUFeatures |= QCPU_HAS_SSE2;
		}
	}

	return com_CPUFeatures;
}

//========================================================

void Key_Init( void );
void Key_Shutdown( void );
void SCR_EndLoadingPlaque( void );

/*
* Com_Error_f
* 
* Just throw a fatal error to
* test error shutdown procedures
*/
#ifndef PUBLIC_BUILD
static void Com_Error_f( void )
{
	Com_Error( ERR_FATAL, "%s", Cmd_Argv( 1 ) );
}
#endif

/*
* Com_Lag_f
*/
#ifndef PUBLIC_BUILD
static void Com_Lag_f( void )
{
	int msecs;

	if( Cmd_Argc() != 2 || atoi( Cmd_Argv( 1 ) ) <= 0 )
	{
		Com_Printf( "Usage: %s <milliseconds>\n", Cmd_Argv( 0 ) );
	}

	msecs = atoi( Cmd_Argv( 1 ) );
	Sys_Sleep( msecs );
	Com_Printf( "Lagged %i milliseconds\n", msecs );
}
#endif

/*
* Q_malloc
* 
* Just like malloc(), but die if allocation fails
*/
void *Q_malloc( size_t size )
{
	void *buf = malloc( size );

	if( !buf )
		Sys_Error( "Q_malloc: failed on allocation of %i bytes.\n", size );

	return buf;
}

/*
* Q_realloc
* 
* Just like realloc(), but die if reallocation fails
*/
void *Q_realloc( void *buf, size_t newsize )
{
	void *newbuf = realloc( buf, newsize );

	if( !newbuf && newsize )
		Sys_Error( "Q_realloc: failed on allocation of %i bytes.\n", newsize );

	return newbuf;
}

/*
* Q_free
*/
void Q_free( void *buf )
{
	free( buf );
}

/*
* Qcommon_InitCommands
*/
void Qcommon_InitCommands( void )
{
	assert( !commands_intialized );

#ifdef SYS_SYMBOL
	Cmd_AddCommand( "sys_symbol", Com_Sys_Symbol_f );
#endif
#ifndef PUBLIC_BUILD
	Cmd_AddCommand( "error", Com_Error_f );
	Cmd_AddCommand( "lag", Com_Lag_f );
#endif

	Cmd_AddCommand( "irc_connect", Irc_Connect_f );
	Cmd_AddCommand( "irc_disconnect", Irc_Disconnect_f );

	if( dedicated->integer )
		Cmd_AddCommand( "quit", Com_Quit );

	commands_intialized = qtrue;
}

/*
* Qcommon_ShutdownCommands
*/
void Qcommon_ShutdownCommands( void )
{
	if( !commands_intialized )
		return;

#ifdef SYS_SYMBOL
	Cmd_RemoveCommand( "sys_symbol" );
#endif
#ifndef PUBLIC_BUILD
	Cmd_RemoveCommand( "error" );
	Cmd_RemoveCommand( "lag" );
#endif

	Cmd_RemoveCommand( "irc_connect" );
	Cmd_RemoveCommand( "irc_disconnect" );

	if( dedicated->integer )
		Cmd_RemoveCommand( "quit" );

	commands_intialized = qfalse;
}

/*
* Qcommon_Init
*/
void Qcommon_Init( int argc, char **argv )
{
	if( setjmp( abortframe ) )
		Sys_Error( "Error during initialization: %s", com_errormsg );

	QThreads_Init();

	com_print_mutex = QMutex_Create();

	// initialize memory manager
	Memory_Init();

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	COM_InitArgv( argc, argv );

	Cbuf_Init();

	// initialize cmd/cvar/dynvar tries
	Cmd_PreInit();
	Cvar_PreInit();
	Dynvar_PreInit();

	// create basic commands and cvars
	Cmd_Init();
	Cvar_Init();
	Dynvar_Init();
	dynvars_initialized = qtrue;

	wswcurl_init();

	Key_Init();

	// we need to add the early commands twice, because
	// a basepath or cdpath needs to be set before execing
	// config files, but we want other parms to override
	// the settings of the config files
	Cbuf_AddEarlyCommands( qfalse );
	Cbuf_Execute();

	// wsw : aiwa : create dynvars (needs to be completed before .cfg scripts are executed)
	Dynvar_Create( "sys_uptime", qtrue, Com_Sys_Uptime_f, DYNVAR_READONLY );
	Dynvar_Create( "frametick", qfalse, DYNVAR_WRITEONLY, DYNVAR_READONLY );
	Dynvar_Create( "quit", qfalse, DYNVAR_WRITEONLY, DYNVAR_READONLY );
	Dynvar_Create( "irc_connected", qfalse, Irc_GetConnected_f, Irc_SetConnected_f );

	Sys_InitDynvars();
	CL_InitDynvars();

#ifdef TV_SERVER_ONLY
	tv_server = Cvar_Get( "tv_server", "1", CVAR_NOSET );
	Cvar_ForceSet( "tv_server", "1" );
#else
	tv_server = Cvar_Get( "tv_server", "0", CVAR_NOSET );
#endif

#ifdef DEDICATED_ONLY
	dedicated =	    Cvar_Get( "dedicated", "1", CVAR_NOSET );
	Cvar_ForceSet( "dedicated", "1" );
#else
	dedicated =	    Cvar_Get( "dedicated", "0", CVAR_NOSET );
#endif

#ifdef MATCHMAKER
	mm_server =	    Cvar_Get( "mm_server", "1", CVAR_READONLY );
	Cvar_ForceSet( "mm_server", "1" );
#else
	mm_server =	    Cvar_Get( "mm_server", "0", CVAR_READONLY );
#endif

	FS_Init();

	Cbuf_AddText( "exec default.cfg\n" );
	if( !dedicated->integer )
	{
		Cbuf_AddText( "exec config.cfg\n" );
		Cbuf_AddText( "exec autoexec.cfg\n" );
	}
	else if( mm_server->integer )
	{
		Cbuf_AddText( "exec mmaker_autoexec.cfg\n" );
	}
	else if( tv_server->integer )
	{
		Cbuf_AddText( "exec tvserver_autoexec.cfg\n" );
	}
	else
	{
		Cbuf_AddText( "exec dedicated_autoexec.cfg\n" );
	}

	Cbuf_AddEarlyCommands( qtrue );
	Cbuf_Execute();

	//
	// init commands and vars
	//
	Memory_InitCommands();

	Qcommon_InitCommands();

	host_speeds =	    Cvar_Get( "host_speeds", "0", 0 );
	log_stats =	    Cvar_Get( "log_stats", "0", 0 );
	developer =	    Cvar_Get( "developer", "0", 0 );
	timescale =	    Cvar_Get( "timescale", "1.0", CVAR_CHEAT );
	fixedtime =	    Cvar_Get( "fixedtime", "0", CVAR_CHEAT );
	if( tv_server->integer )
		logconsole =	    Cvar_Get( "logconsole", "tvconsole.log", CVAR_ARCHIVE );
	else if( dedicated->integer )
		logconsole =	    Cvar_Get( "logconsole", "wswconsole.log", CVAR_ARCHIVE );
	else
		logconsole =	    Cvar_Get( "logconsole", "", CVAR_ARCHIVE );
	logconsole_append = Cvar_Get( "logconsole_append", "1", CVAR_ARCHIVE );
	logconsole_flush =  Cvar_Get( "logconsole_flush", "0", CVAR_ARCHIVE );
	logconsole_timestamp =	Cvar_Get( "logconsole_timestamp", "0", CVAR_ARCHIVE );

	com_showtrace =	    Cvar_Get( "com_showtrace", "0", 0 );
	com_introPlayed3 =   Cvar_Get( "com_introPlayed3", "0", CVAR_ARCHIVE );

	Cvar_Get( "irc_server", "irc.quakenet.org", CVAR_ARCHIVE );
	Cvar_Get( "irc_port", "6667", CVAR_ARCHIVE );
	Cvar_Get( "irc_nick", APPLICATION "Player", CVAR_ARCHIVE );
	Cvar_Get( "irc_user", APPLICATION "User", CVAR_ARCHIVE );
	Cvar_Get( "irc_password", "", CVAR_ARCHIVE );

	Cvar_Get( "gamename", APPLICATION, CVAR_READONLY );
	versioncvar = Cvar_Get( "version", APP_VERSION_STR " " CPUSTRING " " __DATE__ " " BUILDSTRING, CVAR_SERVERINFO|CVAR_READONLY );
	revisioncvar = Cvar_Get( "revision", SVN_RevString(), CVAR_READONLY );

	Sys_Init();

	NET_Init();
	Netchan_Init();

	CM_Init();

	Steam_LoadLibrary();

	Com_ScriptModule_Init();

	MM_Init();

	SV_Init();
	CL_Init();

	SCR_EndLoadingPlaque();

	if( !dedicated->integer )
	{
		Cbuf_AddText( "exec stuffcmds.cfg\n" );
	}
	else if( mm_server->integer )
	{
		Cbuf_AddText( "exec mmaker_stuffcmds.cfg\n" );
	}
	else if( tv_server->integer )
	{
		Cbuf_AddText( "exec tvserver_stuffcmds.cfg\n" );
	}
	else
	{
		Cbuf_AddText( "exec dedicated_stuffcmds.cfg\n" );
	}

	// add + commands from command line
	if( !Cbuf_AddLateCommands() )
	{
		// if the user didn't give any commands, run default action

		if( !dedicated->integer )
		{
			// only play the introduction sequence once
			if( !com_introPlayed3->integer )
			{
				Cvar_ForceSet( com_introPlayed3->name, "1" );
#ifndef __MACOSX__
				Cbuf_AddText( "cinematic intro.roq\n" );
#endif
			}
		}
	}
	else
	{
		// the user asked for something explicit
		// so drop the loading plaque
		SCR_EndLoadingPlaque();
	}

	Com_Printf( "\n====== %s Initialized ======\n", APPLICATION );

	Cbuf_Execute();
}

/*
* Qcommon_Frame
*/
void Qcommon_Frame( unsigned int realmsec )
{
	static dynvar_t	*frametick = NULL;
	static quint64 fc = 0;
	char *s;
	int time_before = 0, time_between = 0, time_after = 0;
	static unsigned int gamemsec;

	if( setjmp( abortframe ) )
		return; // an ERR_DROP was thrown

	if( log_stats->modified )
	{
		log_stats->modified = qfalse;

		if( log_stats->integer && !log_stats_file )
		{
			if( FS_FOpenFile( "stats.log", &log_stats_file, FS_WRITE ) != -1 )
			{
				FS_Printf( log_stats_file, "entities,dlights,parts,frame time\n" );
			}
			else
			{
				log_stats_file = 0;
			}
		}
		else if( log_stats_file )
		{
			FS_FCloseFile( log_stats_file );
			log_stats_file = 0;
		}
	}

	if( fixedtime->integer > 0 )
	{
		gamemsec = fixedtime->integer;
	}
	else if( timescale->value >= 0 )
	{
		static float extratime = 0.0f;
		gamemsec = extratime + (float)realmsec * timescale->value;
		extratime = ( extratime + (float)realmsec * timescale->value ) - (float)gamemsec;
	}
	else
	{
		gamemsec = realmsec;
	}

	if( com_showtrace->integer )
	{
		Com_Printf( "%4i traces %4i brush traces %4i points\n",
			c_traces, c_brush_traces, c_pointcontents );
		c_traces = 0;
		c_brush_traces = 0;
		c_pointcontents = 0;
	}

	wswcurl_perform();

	FS_Frame();

	Steam_RunFrame();

	if( dedicated->integer )
	{
		do
		{
			s = Sys_ConsoleInput();
			if( s )
				Cbuf_AddText( va( "%s\n", s ) );
		}
		while( s );

		Cbuf_Execute();
	}

	// keep the random time dependent
	rand();

	if( host_speeds->integer )
		time_before = Sys_Milliseconds();

	SV_Frame( realmsec, gamemsec );

	if( host_speeds->integer )
		time_between = Sys_Milliseconds();

	CL_Frame( realmsec, gamemsec );

	if( host_speeds->integer )
		time_after = Sys_Milliseconds();

	if( host_speeds->integer )
	{
		int all, sv, gm, cl, rf;

		all = time_after - time_before;
		sv = time_between - time_before;
		cl = time_after - time_between;
		gm = time_after_game - time_before_game;
		rf = time_after_ref - time_before_ref;
		sv -= gm;
		cl -= rf;
		Com_Printf( "all:%3i sv:%3i gm:%3i cl:%3i rf:%3i\n",
			all, sv, gm, cl, rf );
	}

	MM_Frame( realmsec );

	// wsw : aiwa : generic observer pattern to plug in arbitrary functionality
	if( !frametick )
		frametick = Dynvar_Lookup( "frametick" );
	Dynvar_CallListeners( frametick, &fc );
	++fc;
}

/*
* Qcommon_Shutdown
*/
void Qcommon_Shutdown( void )
{
	static qboolean isdown = qfalse;

	if( isdown )
	{
		printf( "Recursive shutdown\n" );
		return;
	}
	isdown = qtrue;

	Com_ScriptModule_Shutdown();
	CM_Shutdown();
	Netchan_Shutdown();
	NET_Shutdown();
	Key_Shutdown();

	Steam_UnloadLibrary();

	Qcommon_ShutdownCommands();
	Memory_ShutdownCommands();

	if( log_stats_file )
	{
		FS_FCloseFile( log_stats_file );
		log_stats_file = 0;
	}
	if( log_file )
	{
		FS_FCloseFile( log_file );
		log_file = 0;
	}
	logconsole = NULL;
	FS_Shutdown();

	wswcurl_cleanup();

	Dynvar_Shutdown();
	dynvars_initialized = qfalse;
	Cvar_Shutdown();
	Cmd_Shutdown();
	Cbuf_Shutdown();
	Memory_Shutdown();
	
	QMutex_Destroy( &com_print_mutex );

	QThreads_Shutdown();
}
