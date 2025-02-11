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
#include "cl_mm.h"
#include "../ui/ui_public.h"
#include "../qcommon/asyncstream.h"
#include "../ref_gl/r_frontend.h"

// Structure containing functions exported from user interface DLL
static ui_export_t *uie;

EXTERN_API_FUNC void *GetGameAPI( void * );

static mempool_t *ui_mempool;
static void *module_handle;

static async_stream_module_t *ui_async_stream;

//==============================================

/*
* CL_UIModule_Print
*/
static void CL_UIModule_Print( const char *msg ) {
	Com_Printf( "%s", msg );
}

#ifndef _MSC_VER
static void CL_UIModule_Error( const char *msg ) __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) static void CL_UIModule_Error( const char *msg );
#endif

/*
* CL_UIModule_Error
*/
static void CL_UIModule_Error( const char *msg ) {
	Com_Error( ERR_FATAL, "%s", msg );
}

/*
* CL_UIModule_GetConfigString
*/
static void CL_UIModule_GetConfigString( int i, char *str, int size ) {
	if( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_UIModule_GetConfigString: i > MAX_CONFIGSTRINGS" );
	}
	if( !str || size <= 0 ) {
		Com_Error( ERR_DROP, "CL_UIModule_GetConfigString: NULL string" );
	}

	Q_strncpyz( str, cl.configstrings[i], size );
}

/*
* CL_UIModule_MemAlloc
*/
static void *CL_UIModule_MemAlloc( size_t size, const char *filename, int fileline ) {
	return _Mem_Alloc( ui_mempool, size, MEMPOOL_USERINTERFACE, 0, filename, fileline );
}

/*
* CL_UIModule_MemFree
*/
static void CL_UIModule_MemFree( void *data, const char *filename, int fileline ) {
	_Mem_Free( data, MEMPOOL_USERINTERFACE, 0, filename, fileline );
}

//==============================================


/*
* CL_UIModule_AsyncStream_Init
*/
static void CL_UIModule_AsyncStream_Init( void ) {
	ui_async_stream = AsyncStream_InitModule( "UI", CL_UIModule_MemAlloc, CL_UIModule_MemFree );
}

/*
* CL_UIModule_AsyncStream_PerformRequest
*/
static int CL_UIModule_AsyncStream_PerformRequest( const char *url, const char *method,
												   const char *data, int timeout,
												   ui_async_stream_read_cb_t read_cb, ui_async_stream_done_cb_t done_cb, void *privatep ) {
	const char *headers[] = { NULL, NULL, NULL, NULL, NULL };

	assert( ui_async_stream );

	CL_AddSessionHttpRequestHeaders( url, headers );

	return AsyncStream_PerformRequestExt( ui_async_stream, url, method, data, headers, timeout,
										  0, read_cb, done_cb, NULL, privatep );
}

/*
* CL_UIModule_AsyncStream_Shutdown
*/
static void CL_UIModule_AsyncStream_Shutdown( void ) {
	AsyncStream_ShutdownModule( ui_async_stream );
	ui_async_stream = NULL;
}

#define UI_L10N_DOMAIN  "ui"

/*
* CL_UIModule_L10n_LoadLangPOFile
*/
static void CL_UIModule_L10n_LoadLangPOFile( const char *filepath ) {
	L10n_LoadLangPOFile( UI_L10N_DOMAIN, filepath );
}

/*
* CL_UIModule_L10n_TranslateString
*/
static const char *CL_UIModule_L10n_TranslateString( const char *string ) {
	return L10n_TranslateString( UI_L10N_DOMAIN, string );
}

/*
* CL_UIModule_L10n_ClearDomain
*/
static void CL_UIModule_L10n_ClearDomain( void ) {
	L10n_ClearDomain( UI_L10N_DOMAIN );
}

/*
* CL_UIModule_PlayerNum
*/
static int CL_UIModule_PlayerNum( void ) {
	if( cls.state < CA_CONNECTED ) {
		return -1;
	}
	return cl.playernum;
}

// TODO: Remove this useless clutter and link UI statically
static bool CL_MM_Login( const char *user, const char *password ) {
	return CLStatsowFacade::Instance()->Login( user, password );
}

static bool CL_MM_Logout( bool waitForCompletion ) {
	return CLStatsowFacade::Instance()->Logout( waitForCompletion );
}

static int CL_MM_GetLoginState() {
	return CLStatsowFacade::Instance()->GetLoginState();
}

static const wsw::string_view &CL_MM_GetLastErrorMessage() {
	return CLStatsowFacade::Instance()->GetLastErrorMessage();
}

static const wsw::string_view &CL_MM_GetProfileWebUrl() {
	return CLStatsowFacade::Instance()->GetProfileWebUrl();
}

static const wsw::string_view &CL_MM_GetProfileRmlUrl() {
	return CLStatsowFacade::Instance()->GetProfileRmlUrl();
}

static const wsw::string_view &CL_MM_GetBaseWebUrl() {
	return CLStatsowFacade::Instance()->GetBaseWebUrl();
}

//==============================================

shader_t *UI_RegisterRawPic( const char *name, int width, int height, uint8_t *data, int samples ) {
	return ::R_RegisterRawPic( name, width, height, data, samples );
}

/*
* CL_UIModule_Init
*/
void CL_UIModule_Init( void ) {
	int apiversion;
	ui_import_t import;
	dllfunc_t funcs[2];
#ifndef UI_HARD_LINKED
	void *( *GetUIAPI )( void * ) = NULL;
#endif

	CL_UIModule_Shutdown();

	Com_Printf( "------- UI initialization -------\n" );

	ui_mempool = _Mem_AllocPool( NULL, "User Interface", MEMPOOL_USERINTERFACE, __FILE__, __LINE__ );

	import.Error = CL_UIModule_Error;
	import.Print = CL_UIModule_Print;

	import.Cvar_Get = Cvar_Get;
	import.Cvar_Set = Cvar_Set;
	import.Cvar_SetValue = Cvar_SetValue;
	import.Cvar_ForceSet = Cvar_ForceSet;
	import.Cvar_String = Cvar_String;
	import.Cvar_Value = Cvar_Value;

	import.Cmd_Argc = Cmd_Argc;
	import.Cmd_Argv = Cmd_Argv;
	import.Cmd_Args = Cmd_Args;

	import.Cmd_AddCommand = Cmd_AddCommand;
	import.Cmd_RemoveCommand = Cmd_RemoveCommand;
	import.Cmd_ExecuteText = Cbuf_ExecuteText;
	import.Cmd_Execute = Cbuf_Execute;
	import.Cmd_SetCompletionFunc = Cmd_SetCompletionFunc;

	import.FS_FOpenFile = FS_FOpenFile;
	import.FS_Read = FS_Read;
	import.FS_Write = FS_Write;
	import.FS_Print = FS_Print;
	import.FS_Tell = FS_Tell;
	import.FS_Seek = FS_Seek;
	import.FS_Eof = FS_Eof;
	import.FS_Flush = FS_Flush;
	import.FS_FCloseFile = FS_FCloseFile;
	import.FS_RemoveFile = FS_RemoveFile;
	import.FS_GetFileList = FS_GetFileList;
	import.FS_GetGameDirectoryList = FS_GetGameDirectoryList;
	import.FS_FirstExtension = FS_FirstExtension;
	import.FS_MoveFile = FS_MoveFile;
	import.FS_MoveCacheFile = FS_MoveCacheFile;
	import.FS_IsUrl = FS_IsUrl;
	import.FS_FileMTime = FS_FileMTime;
	import.FS_RemoveDirectory = FS_RemoveDirectory;

	import.CL_Quit = CL_Quit;
	import.CL_SetKeyDest = CL_SetKeyDest;
	import.CL_ResetServerCount = CL_ResetServerCount;
	import.CL_GetClipboardData = CL_GetClipboardData;
	import.CL_SetClipboardData = CL_SetClipboardData;
	import.CL_FreeClipboardData = CL_FreeClipboardData;
	import.CL_IsBrowserAvailable = CL_IsBrowserAvailable;
	import.CL_OpenURLInBrowser = CL_OpenURLInBrowser;
	import.CL_ReadDemoMetaData = CL_ReadDemoMetaData;
	import.CL_PlayerNum = CL_UIModule_PlayerNum;

	import.Key_GetBindingBuf = Key_GetBindingBuf;
	import.Key_KeynumToString = Key_KeynumToString;
	import.Key_StringToKeynum = Key_StringToKeynum;
	import.Key_SetBinding = Key_SetBinding;
	import.Key_IsDown = Key_IsDown;

	import.IN_GetThumbsticks = IN_GetThumbsticks;
	import.IN_ShowSoftKeyboard = IN_ShowSoftKeyboard;
	import.IN_SupportedDevices = IN_SupportedDevices;

	import.R_ClearScene = RF_ClearScene;
	import.R_AddEntityToScene = RF_AddEntityToScene;
	import.R_AddLightToScene = RF_AddLightToScene;
	import.R_AddLightStyleToScene = RF_AddLightStyleToScene;
	import.R_AddPolyToScene = RF_AddPolyToScene;
	import.R_RenderScene = RF_RenderScene;
	import.R_BlurScreen = RF_BlurScreen;
	import.R_EndFrame = RF_EndFrame;
	import.R_RegisterWorldModel = RF_RegisterWorldModel;
	import.R_ModelBounds = R_ModelBounds;
	import.R_ModelFrameBounds = R_ModelFrameBounds;
	import.R_RegisterModel = R_RegisterModel;
	import.R_RegisterPic = R_RegisterPic;
	import.R_RegisterRawPic = UI_RegisterRawPic;
	import.R_RegisterLevelshot = R_RegisterLevelshot;
	import.R_RegisterSkin = R_RegisterSkin;
	import.R_RegisterSkinFile = R_RegisterSkinFile;
	import.R_RegisterVideo = R_RegisterVideo;
	import.R_RegisterLinearPic = R_RegisterLinearPic;
	import.R_LerpTag = RF_LerpTag;
	import.R_DrawStretchPic = RF_DrawStretchPic;
	import.R_DrawRotatedStretchPic = RF_DrawRotatedStretchPic;
	import.R_DrawStretchPoly = RF_DrawStretchPoly;
	import.R_TransformVectorToScreen = RF_TransformVectorToScreen;
	import.R_Scissor = RF_SetScissor;
	import.R_GetScissor = RF_GetScissor;
	import.R_ResetScissor = RF_ResetScissor;
	import.R_GetShaderDimensions = R_GetShaderDimensions;
	import.R_SkeletalGetNumBones = R_SkeletalGetNumBones;
	import.R_SkeletalGetBoneInfo = R_SkeletalGetBoneInfo;
	import.R_SkeletalGetBonePose = R_SkeletalGetBonePose;
	import.R_GetShaderCinematic = RF_GetShaderCinematic;

	import.S_RegisterSound = CL_SoundModule_RegisterSound;
	import.S_StartLocalSound = CL_SoundModule_StartLocalSoundByName;
	import.S_StartBackgroundTrack = CL_SoundModule_StartBackgroundTrack;
	import.S_StopBackgroundTrack = CL_SoundModule_StopBackgroundTrack;

	import.SCR_RegisterFont = SCR_RegisterFont;
	import.SCR_DrawString = SCR_DrawString;
	import.SCR_DrawStringWidth = SCR_DrawStringWidth;
	import.SCR_DrawClampString = SCR_DrawClampString;
	import.SCR_FontSize = SCR_FontSize;
	import.SCR_FontHeight = SCR_FontHeight;
	import.SCR_FontUnderline = SCR_FontUnderline;
	import.SCR_FontAdvance = SCR_FontAdvance;
	import.SCR_FontXHeight = SCR_FontXHeight;
	import.SCR_SetDrawCharIntercept = SCR_SetDrawCharIntercept;
	import.SCR_strWidth = SCR_strWidth;
	import.SCR_StrlenForWidth = SCR_StrlenForWidth;

	import.GetConfigString = CL_UIModule_GetConfigString;

	import.Milliseconds = Sys_Milliseconds;
	import.Microseconds = Sys_Microseconds;

	import.AsyncStream_UrlEncode = AsyncStream_UrlEncode;
	import.AsyncStream_UrlDecode = AsyncStream_UrlDecode;
	import.AsyncStream_PerformRequest = CL_UIModule_AsyncStream_PerformRequest;
	import.GetBaseServerURL = CL_GetBaseServerURL;

	import.VID_GetModeInfo = VID_GetModeInfo;
	import.VID_FlashWindow = VID_FlashWindow;

	import.Mem_Alloc = CL_UIModule_MemAlloc;
	import.Mem_Free = CL_UIModule_MemFree;

	import.ML_GetFilename = ML_GetFilename;
	import.ML_GetFullname = ML_GetFullname;
	import.ML_GetMapByNum = ML_GetMapByNum;

	// TODO: Just link UI statically
	import.MM_Login = CL_MM_Login;
	import.MM_Logout = CL_MM_Logout;
	import.MM_GetLoginState = CL_MM_GetLoginState;
	import.MM_GetLastErrorMessage = CL_MM_GetLastErrorMessage;
	import.MM_GetProfileWebUrl = CL_MM_GetProfileRmlUrl;
	import.MM_GetProfileRmlUrl = CL_MM_GetProfileWebUrl;
	import.MM_GetBaseWebUrl = CL_MM_GetBaseWebUrl;

	import.L10n_LoadLangPOFile = &CL_UIModule_L10n_LoadLangPOFile;
	import.L10n_TranslateString = &CL_UIModule_L10n_TranslateString;
	import.L10n_ClearDomain = &CL_UIModule_L10n_ClearDomain;
	import.L10n_GetUserLanguage = &L10n_GetUserLanguage;

#ifndef UI_HARD_LINKED
	funcs[0].name = "GetUIAPI";
	funcs[0].funcPointer = ( void ** ) &GetUIAPI;
	funcs[1].name = NULL;
	module_handle = Com_LoadLibrary( LIB_PREFIX "ui_" ARCH LIB_SUFFIX, funcs );
	if( !module_handle ) {
		Mem_FreePool( &ui_mempool );
		Com_Error( ERR_FATAL, "Failed to load UI dll" );
		uie = NULL;
		return;
	}
#endif

	uie = (ui_export_t *)GetUIAPI( &import );
	apiversion = uie->API();
	if( apiversion == UI_API_VERSION ) {
		CL_UIModule_AsyncStream_Init();

		uie->Init( viddef.width, viddef.height, VID_GetPixelRatio(),
				   APP_PROTOCOL_VERSION, APP_DEMO_EXTENSION_STR, APP_UI_BASEPATH );

		uie->ShowQuickMenu( cls.quickmenu );
	} else {
		// wrong version
		uie = NULL;
		Com_UnloadLibrary( &module_handle );
		Mem_FreePool( &ui_mempool );
		Com_Error( ERR_FATAL, "UI version is %i, not %i", apiversion, UI_API_VERSION );
	}

	Com_Printf( "------------------------------------\n" );
}

/*
* CL_UIModule_Shutdown
*/
void CL_UIModule_Shutdown( void ) {
	if( !uie ) {
		return;
	}

	CL_UIModule_AsyncStream_Shutdown();

	uie->Shutdown();
	Mem_FreePool( &ui_mempool );
	Com_UnloadLibrary( &module_handle );
	uie = NULL;

	CL_UIModule_L10n_ClearDomain();
}

/*
* CL_UIModule_TouchAllAssets
*/
void CL_UIModule_TouchAllAssets( void ) {
	if( uie ) {
		uie->TouchAllAssets();
	}
}

/*
* CL_UIModule_Refresh
*/
void CL_UIModule_Refresh( bool backGround, bool showCursor ) {
	if( uie ) {
		uie->Refresh( cls.realtime, Com_ClientState(), Com_ServerState(),
					  cls.demo.playing, cls.demo.name, cls.demo.paused, Q_rint( cls.demo.time / 1000.0f ),
					  backGround, showCursor );
	}
}

/*
* CL_UIModule_UpdateConnectScreen
*/
void CL_UIModule_UpdateConnectScreen( bool backGround ) {
	if( uie ) {
		int downloadType, downloadSpeed;

		if( cls.download.web ) {
			downloadType = DOWNLOADTYPE_WEB;
		} else if( cls.download.filenum ) {
			downloadType = DOWNLOADTYPE_SERVER;
		} else {
			downloadType = DOWNLOADTYPE_NONE;
		}

		if( downloadType ) {
#if 0
#define DLSAMPLESCOUNT 32
#define DLSSAMPLESMASK ( DLSAMPLESCOUNT - 1 )
			int i, samples;
			size_t downloadedSize;
			unsigned int downloadTime;
			static int lastFrameCount = 0, frameCount = 0;
			static unsigned int downloadSpeeds[DLSAMPLESCOUNT];
			float avDownloadSpeed;

			downloadedSize = (size_t)( cls.download.size * cls.download.percent ) - cls.download.baseoffset;
			downloadTime = Sys_Milliseconds() - cls.download.timestart;
			if( downloadTime > 200 ) {
				downloadSpeed = ( downloadedSize / 1024.0f ) / ( downloadTime * 0.001f );

				if( cls.framecount > lastFrameCount + DLSAMPLESCOUNT ) {
					frameCount = 0;
				}
				lastFrameCount = cls.framecount;

				downloadSpeeds[frameCount & DLSSAMPLESMASK] = downloadSpeed;
				frameCount = max( frameCount + 1, 1 );
				samples = min( frameCount, DLSAMPLESCOUNT );

				for( avDownloadSpeed = 0.0f, i = 0; i < samples; i++ )
					avDownloadSpeed += downloadSpeeds[i];

				avDownloadSpeed /= samples;
				downloadSpeed = (int)avDownloadSpeed;
			} else {
				lastFrameCount = -1;
				downloadSpeed = 0;
			}
#else
			size_t downloadedSize;
			unsigned int downloadTime;

			downloadedSize = (size_t)( cls.download.size * cls.download.percent ) - cls.download.baseoffset;
			downloadTime = Sys_Milliseconds() - cls.download.timestart;

			downloadSpeed = downloadTime ? ( downloadedSize / 1024.0f ) / ( downloadTime * 0.001f ) : 0;
#endif
		} else {
			downloadSpeed = 0;
		}

		uie->UpdateConnectScreen( cls.servername, cls.rejected ? cls.rejectmessage : NULL,
								  downloadType, cls.download.name, cls.download.percent * 100.0f, downloadSpeed,
								  cls.connect_count, backGround );

		CL_UIModule_Refresh( backGround, false );
	}
}

/*
* CL_UIModule_Keydown
*/
void CL_UIModule_Keydown( int key ) {
	if( uie ) {
		uie->Keydown( UI_CONTEXT_MAIN, key );
	}
}

/*
* CL_UIModule_Keyup
*/
void CL_UIModule_Keyup( int key ) {
	if( uie ) {
		uie->Keyup( UI_CONTEXT_MAIN, key );
	}
}

/*
* CL_UIModule_KeydownQuick
*/
void CL_UIModule_KeydownQuick( int key ) {
	if( uie ) {
		uie->Keydown( UI_CONTEXT_QUICK, key );
	}
}

/*
* CL_UIModule_KeyupQuick
*/
void CL_UIModule_KeyupQuick( int key ) {
	if( uie ) {
		uie->Keyup( UI_CONTEXT_QUICK, key );
	}
}

/*
* CL_UIModule_CharEvent
*/
void CL_UIModule_CharEvent( wchar_t key ) {
	if( uie ) {
		uie->CharEvent( UI_CONTEXT_MAIN, key );
	}
}

/*
* CL_UIModule_TouchEvent
*/
bool CL_UIModule_TouchEvent( int id, touchevent_t type, int x, int y ) {
	if( uie ) {
		return uie->TouchEvent( UI_CONTEXT_MAIN, id, type, x, y );
	}

	return false;
}

/*
* CL_UIModule_TouchEventQuick
*/
bool CL_UIModule_TouchEventQuick( int id, touchevent_t type, int x, int y ) {
	if( uie ) {
		return uie->TouchEvent( UI_CONTEXT_QUICK, id, type, x, y );
	}

	return false;
}

/*
* CL_UIModule_IsTouchDown
*/
bool CL_UIModule_IsTouchDown( int id ) {
	if( uie ) {
		return uie->IsTouchDown( UI_CONTEXT_MAIN, id );
	}

	return false;
}

/*
* CL_UIModule_IsTouchDownQuick
*/
bool CL_UIModule_IsTouchDownQuick( int id ) {
	if( uie ) {
		return uie->IsTouchDown( UI_CONTEXT_QUICK, id );
	}

	return false;
}

/*
* CL_UIModule_CancelTouches
*/
void CL_UIModule_CancelTouches( void ) {
	if( uie ) {
		uie->CancelTouches( UI_CONTEXT_QUICK );
		uie->CancelTouches( UI_CONTEXT_MAIN );
	}
}

/*
* CL_UIModule_ForceMenuOn
*/
void CL_UIModule_ForceMenuOn( void ) {
	if( uie ) {
		Cbuf_ExecuteText( EXEC_NOW, "menu_force 1" );
	}
}

/*
* CL_UIModule_ForceMenuOff
*/
void CL_UIModule_ForceMenuOff( void ) {
	if( uie ) {
		uie->ForceMenuOff();
	}
}

/*
* CL_UIModule_ShowQuickMenu
*/
void CL_UIModule_ShowQuickMenu( bool show ) {
	if( uie ) {
		uie->ShowQuickMenu( show );
	}
}

/*
* CL_UIModule_HaveQuickMenu
*/
bool CL_UIModule_HaveQuickMenu( void ) {
	if( uie ) {
		return uie->HaveQuickMenu();
	}
	return false;
}

/*
* CL_UIModule_AddToServerList
*/
void CL_UIModule_AddToServerList( const char *adr, const char *info ) {
	if( uie ) {
		uie->AddToServerList( adr, info );
	}
}

/*
* CL_UIModule_MouseMove
*/
void CL_UIModule_MouseMove( int frameTime, int dx, int dy ) {
	if( uie ) {
		uie->MouseMove( UI_CONTEXT_MAIN, frameTime, dx, dy );
	}
}

/*
* CL_UIModule_MouseSet
*/
void CL_UIModule_MouseSet( int mx, int my, bool showCursor ) {
	if( uie ) {
		uie->MouseSet( UI_CONTEXT_MAIN, mx, my, showCursor );
	}
}
