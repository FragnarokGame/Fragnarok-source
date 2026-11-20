/*
Steam sign-in.

FRAGNAROK talks to Steam only through the Steamworks flat C API, loaded from
steam_api64.dll / libsteam_api.dylib / libsteam_api.so next to the game when it
is there. No Steamworks headers are compiled in: the few functions and the one
callback used are declared below from the SDK's documented C interface.

The game asks Steam for a Web API ticket and hands it to cl_auth.c, which sends
it to the steam-login Supabase function; the function verifies it with Valve.
*/

#include "client.h"
#include "cl_steam.h"
#include "../sys/sys_loadlib.h"

char *Sys_BinaryPath( void );

#ifdef _WIN32
#define STEAM_CALL __cdecl
#define STEAM_LIBRARY "steam_api64.dll"
#elif defined( __APPLE__ )
#define STEAM_CALL
#define STEAM_LIBRARY "libsteam_api.dylib"
#else
#define STEAM_CALL
#define STEAM_LIBRARY "libsteam_api.so"
#endif

/* Must match the identity the steam-login function checks. */
#define STEAM_TICKET_IDENTITY	"fragnarok"
/* k_iSteamUserCallbacks (100) + 68: GetTicketForWebApiResponse_t */
#define STEAM_CALLBACK_WEB_TICKET	168
#define STEAM_TICKET_BYTES		2560

typedef int steamPipe_t;
typedef unsigned int steamTicket_t;
typedef char steamErrMsg_t[1024];

/* steam_api_internal.h: CallbackMsg_t */
typedef struct {
	int user;
	int callback;
	unsigned char *param;
	int paramSize;
} steamCallbackMsg_t;

/* isteamuser.h: GetTicketForWebApiResponse_t (all four-byte fields, so packing
   doesn't change the layout) */
typedef struct {
	steamTicket_t ticket;
	int result;
	int size;
	unsigned char data[STEAM_TICKET_BYTES];
} steamWebTicket_t;

static struct {
	void *library;
	qboolean ready;
	steamPipe_t pipe;
	void *user;
	char status[160];

	steamTicket_t pendingTicket;
	qboolean pending, ready_ticket;
	char ticket[STEAM_TICKET_HEX];

	int ( STEAM_CALL *InitFlat )( steamErrMsg_t *error );
	void ( STEAM_CALL *Shutdown )( void );
	steamPipe_t ( STEAM_CALL *GetHSteamPipe )( void );
	void ( STEAM_CALL *DispatchInit )( void );
	void ( STEAM_CALL *DispatchRunFrame )( steamPipe_t pipe );
	int ( STEAM_CALL *DispatchNext )( steamPipe_t pipe, steamCallbackMsg_t *msg );
	void ( STEAM_CALL *DispatchFree )( steamPipe_t pipe );
	void *( STEAM_CALL *SteamUser )( void );
	steamTicket_t ( STEAM_CALL *GetWebTicket )( void *user, const char *identity );
	void ( STEAM_CALL *CancelTicket )( void *user, steamTicket_t ticket );
} steam;

static void *Steam_Function( const char *name ) {
	void *function = Sys_LoadFunction( steam.library, name );
	if ( !function ) Com_Printf( "Steam: %s is missing from " STEAM_LIBRARY ".\n", name );
	return function;
}

static void *Steam_LoadLibrary( void ) {
	void *library;
#ifdef __APPLE__
	/* Inside the app bundle the library sits in Contents/Frameworks. */
	library = Sys_LoadLibrary( va( "%s/../Frameworks/" STEAM_LIBRARY, Sys_BinaryPath() ) );
	if ( library ) return library;
#endif
	library = Sys_LoadLibrary( va( "%s%c" STEAM_LIBRARY, Sys_BinaryPath(), PATH_SEP ) );
	return library ? library : Sys_LoadLibrary( STEAM_LIBRARY );
}

void CL_SteamInit( void ) {
	steamErrMsg_t error;
	int result;

	if ( steam.library || com_dedicated->integer ) return;
	memset( &steam, 0, sizeof( steam ) );
	/* +set cl_steam 0 keeps automated tests and LAN-only play off Steam. */
	if ( !Cvar_Get( "cl_steam", "1", CVAR_INIT )->integer ) {
		Q_strncpyz( steam.status, "Steam is turned off (cl_steam 0). Online play needs Steam.", sizeof( steam.status ) );
		Com_Printf( "Steam: turned off (cl_steam 0).\n" );
		return;
	}
	steam.library = Steam_LoadLibrary();
	if ( !steam.library ) {
		Q_strncpyz( steam.status, "This build doesn't include Steam. Online play needs the Steam version.",
			sizeof( steam.status ) );
		Com_Printf( "Steam: " STEAM_LIBRARY " not found; online sign-in is off.\n" );
		return;
	}
	steam.InitFlat = Steam_Function( "SteamAPI_InitFlat" );
	steam.Shutdown = Steam_Function( "SteamAPI_Shutdown" );
	steam.GetHSteamPipe = Steam_Function( "SteamAPI_GetHSteamPipe" );
	steam.DispatchInit = Steam_Function( "SteamAPI_ManualDispatch_Init" );
	steam.DispatchRunFrame = Steam_Function( "SteamAPI_ManualDispatch_RunFrame" );
	steam.DispatchNext = Steam_Function( "SteamAPI_ManualDispatch_GetNextCallback" );
	steam.DispatchFree = Steam_Function( "SteamAPI_ManualDispatch_FreeLastCallback" );
	steam.SteamUser = Steam_Function( "SteamAPI_SteamUser_v023" );
	steam.GetWebTicket = Steam_Function( "SteamAPI_ISteamUser_GetAuthTicketForWebApi" );
	steam.CancelTicket = Steam_Function( "SteamAPI_ISteamUser_CancelAuthTicket" );
	if ( !steam.InitFlat || !steam.Shutdown || !steam.GetHSteamPipe || !steam.DispatchInit ||
		!steam.DispatchRunFrame || !steam.DispatchNext || !steam.DispatchFree || !steam.SteamUser ||
		!steam.GetWebTicket || !steam.CancelTicket ) {
		Q_strncpyz( steam.status, "This copy of the Steam library is too old. Reinstall FRAGNAROK from Steam.",
			sizeof( steam.status ) );
		Sys_UnloadLibrary( steam.library );
		steam.library = NULL;
		return;
	}

	error[0] = 0;
	result = steam.InitFlat( &error );
	if ( result != 0 ) {
		/* 2 = Steam isn't running, 3 = Steam client out of date */
		Q_strncpyz( steam.status, result == 2 ? "Steam isn't running. Start Steam to play online." :
			result == 3 ? "Your Steam client is out of date. Update Steam to play online." :
			"Couldn't connect to Steam. Launch FRAGNAROK from Steam to play online.", sizeof( steam.status ) );
		Com_Printf( "Steam: not available (%d) %s\n", result, error );
		return;
	}
	steam.DispatchInit();
	steam.pipe = steam.GetHSteamPipe();
	steam.user = steam.SteamUser();
	if ( !steam.user ) {
		Q_strncpyz( steam.status, "Couldn't reach your Steam account.", sizeof( steam.status ) );
		steam.Shutdown();
		return;
	}
	steam.ready = qtrue;
	steam.status[0] = 0;
	Com_Printf( "Steam: connected.\n" );
}

void CL_SteamShutdown( void ) {
	if ( steam.ready ) {
		if ( steam.pending ) steam.CancelTicket( steam.user, steam.pendingTicket );
		steam.Shutdown();
	}
	/* Leave the library loaded: Steam's own threads can still be running after
	 * SteamAPI_Shutdown, and unloading the code under them crashed the game on quit.
	 * The process exits right after this, and a later CL_SteamInit only bumps the
	 * library's load count. */
	memset( &steam, 0, sizeof( steam ) );
}

static void Steam_WebTicket( const steamWebTicket_t *response ) {
	int i;
	static const char digits[] = "0123456789abcdef";

	if ( !steam.pending || response->ticket != steam.pendingTicket ) return;
	steam.pending = qfalse;
	/* 1 = k_EResultOK */
	if ( response->result != 1 || response->size <= 0 || response->size > STEAM_TICKET_BYTES ) {
		Com_Printf( "Steam: ticket request failed (%d).\n", response->result );
		Q_strncpyz( steam.status, "Steam couldn't confirm your account. Retrying...", sizeof( steam.status ) );
		return;
	}
	for ( i = 0; i < response->size; i++ ) {
		steam.ticket[i * 2] = digits[response->data[i] >> 4];
		steam.ticket[i * 2 + 1] = digits[response->data[i] & 15];
	}
	steam.ticket[response->size * 2] = 0;
	steam.ready_ticket = qtrue;
	Com_Printf( "Steam: sign-in ticket ready (%d bytes).\n", response->size );
	steam.status[0] = 0;
}

void CL_SteamFrame( void ) {
	steamCallbackMsg_t msg;

	if ( !steam.ready ) return;
	steam.DispatchRunFrame( steam.pipe );
	while ( steam.DispatchNext( steam.pipe, &msg ) ) {
		if ( msg.callback == STEAM_CALLBACK_WEB_TICKET && msg.paramSize >= (int)sizeof( steamWebTicket_t ) )
			Steam_WebTicket( (const steamWebTicket_t *)msg.param );
		steam.DispatchFree( steam.pipe );
	}
}

qboolean CL_SteamAvailable( void ) {
	return steam.ready;
}

const char *CL_SteamStatus( void ) {
	return steam.status;
}

qboolean CL_SteamRequestTicket( void ) {
	if ( !steam.ready || steam.pending ) return qfalse;
	steam.ready_ticket = qfalse;
	steam.pendingTicket = steam.GetWebTicket( steam.user, STEAM_TICKET_IDENTITY );
	/* 0 = k_HAuthTicketInvalid */
	steam.pending = steam.pendingTicket != 0;
	return steam.pending;
}

qboolean CL_SteamTicketPending( void ) {
	return steam.pending;
}

qboolean CL_SteamTakeTicket( char *hex, int size ) {
	if ( !steam.ready_ticket ) return qfalse;
	steam.ready_ticket = qfalse;
	Q_strncpyz( hex, steam.ticket, size );
	Com_Memset( steam.ticket, 0, sizeof( steam.ticket ) );
	return qtrue;
}
