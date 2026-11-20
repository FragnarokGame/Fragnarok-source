#include "client.h"
#include "cl_auth.h"
#include "cl_steam.h"
#include "../qcommon/play_analytics.h"
#include "../game/bg_campaign.h"
#include <SDL.h>
#include <time.h>
/* The renderer also embeds this parser; keep this instance private to auth. */
#define JSON_ArrayGetFirstValue Auth_JSON_ArrayGetFirstValue
#define JSON_ArrayGetNextValue Auth_JSON_ArrayGetNextValue
#define JSON_ArrayGetIndex Auth_JSON_ArrayGetIndex
#define JSON_ArrayGetValue Auth_JSON_ArrayGetValue
#define JSON_ObjectGetNamedValue Auth_JSON_ObjectGetNamedValue
#define JSON_ValueGetType Auth_JSON_ValueGetType
#define JSON_ValueGetString Auth_JSON_ValueGetString
#define JSON_ValueGetDouble Auth_JSON_ValueGetDouble
#define JSON_ValueGetFloat Auth_JSON_ValueGetFloat
#define JSON_ValueGetInt Auth_JSON_ValueGetInt
#define JSON_IMPLEMENTATION
#include "../qcommon/json.h"

typedef enum {
    AUTH_STEAM, AUTH_REFRESH, AUTH_PROFILE, AUTH_HISTORY, AUTH_CAMPAIGN, AUTH_CAMPAIGN_STANDINGS
} authStage_t;

typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t done;
    char host[128];
    char key[2048];
    char body[STEAM_TICKET_HEX + 64];
    char response[32768];
    int code;
    char bearer[8192];
    authStage_t stage;
    qboolean cancelled;
    int historyGeneration;
} authRequest_t;

static authRequest_t request;
static uiAuthStatus_t account;
static char accessToken[8192];
static char refreshToken[AUTH_REFRESH_CAPACITY];
static time_t expiresAt, refreshAt;
/* When to ask Steam for a new sign-in ticket while signed out. */
static time_t steamRetryAt;
static char guestName[MAX_CVAR_VALUE_STRING];
static qboolean nameApplied;
static char historyServer[37], historyRun[37];
static int historyConnection, historyGeneration;
static qboolean historyRequested, historySubmitted;
static time_t historyRetryAt;

typedef struct {
    int elapsedMsec, deaths, score, attempts;
    time_t retryAt;
} campaignRun_t;

static uiCampaignScore_t campaignResult;
static uiCampaignStanding_t campaignStandings[CAMPAIGN_MISSION_COUNT];
static qboolean campaignStandingsRequested;
static time_t campaignStandingsRefreshAt;
static campaignRun_t campaignPending[CAMPAIGN_MISSION_COUNT];
static char campaignOwner[64];
static qboolean campaignCheats;
static int campaignRequestMission;
static campaignRun_t campaignRequestRun;

static void Auth_Start( authStage_t stage );
static void Auth_Refresh( void );
static void Auth_ClearSession( void );

void CL_AuthCampaignStart( int mission, qboolean cheats ) {
    memset( &campaignResult, 0, sizeof( campaignResult ) );
    campaignResult.mission = mission;
    campaignCheats = cheats;
    campaignOwner[0] = 0;
    if ( mission && account.signedIn && !account.needsDisplayName ) {
        Q_strncpyz( campaignOwner, account.playerId, sizeof( campaignOwner ) );
    }
}

void CL_AuthCampaignFinish( int mission, int elapsedMsec, int deaths ) {
    campaignRun_t *pending;
    int score;
    if ( mission < 1 || mission > CAMPAIGN_MISSION_COUNT || mission != campaignResult.mission ||
        campaignResult.elapsedMsec || elapsedMsec <= 0 || deaths < 0 ) return;
    campaignResult.elapsedMsec = elapsedMsec;
    campaignResult.deaths = deaths;
    if ( !account.signedIn || !campaignOwner[0] || strcmp( campaignOwner, account.playerId ) ) return;
    if ( campaignCheats || Cvar_VariableIntegerValue( "sv_cheats" ) || Cvar_VariableIntegerValue( "bot_pause" ) ) {
        campaignResult.state = CAMPAIGN_SCORE_UNRANKED;
        return;
    }
    score = (int)( 10000.0 / ( 1.0 + elapsedMsec / 60000.0 ) * pow( 0.9, deaths ) + 0.5 );
    campaignResult.score = score;
    campaignResult.state = CAMPAIGN_SCORE_PENDING;
    pending = &campaignPending[mission - 1];
    if ( !pending->elapsedMsec || score > pending->score || ( score == pending->score &&
        ( deaths < pending->deaths || ( deaths == pending->deaths && elapsedMsec < pending->elapsedMsec ) ) ) ) {
        *pending = (campaignRun_t){ elapsedMsec, deaths, score, 0, 0 };
    }
}

void CL_AuthCampaignScore( uiCampaignScore_t *score ) {
    *score = campaignResult;
}

void CL_AuthCampaignStanding( int mission, uiCampaignStanding_t *standing ) {
    *standing = (uiCampaignStanding_t){ 0 };
    if ( !account.signedIn || account.needsDisplayName || mission < 1 || mission > CAMPAIGN_MISSION_COUNT ) return;
    *standing = campaignStandings[mission - 1];
    if ( time( NULL ) >= campaignStandingsRefreshAt ) campaignStandingsRequested = qtrue;
}

static void Auth_CampaignStandingsResponse( int code, const char *response, const char *end ) {
    uiCampaignStanding_t standings[CAMPAIGN_MISSION_COUNT] = { { 0 } };
    const char *row;

    if ( code != 200 || JSON_ValueGetType( response, end ) != JSONTYPE_ARRAY ) return;
    for ( row = JSON_ArrayGetFirstValue( response, end ); row; row = JSON_ArrayGetNextValue( row, end ) ) {
        const char *missionValue = JSON_ObjectGetNamedValue( row, end, "mission" );
        const char *scoreValue = JSON_ObjectGetNamedValue( row, end, "best_score" );
        const char *rankValue = JSON_ObjectGetNamedValue( row, end, "rank" );
        int mission = JSON_ValueGetInt( missionValue, end );
        int score = JSON_ValueGetInt( scoreValue, end );
        int rank = JSON_ValueGetInt( rankValue, end );

        if ( mission < 1 || mission > CAMPAIGN_MISSION_COUNT ||
            !scoreValue || *scoreValue < '0' || *scoreValue > '9' || score < 0 || score > 10000 || rank < 1 ) return;
        standings[mission - 1] = (uiCampaignStanding_t){ score, rank };
    }
    memcpy( campaignStandings, standings, sizeof( campaignStandings ) );
}

static void Auth_CampaignResponse( int code, int rank, int bestScore ) {
    campaignRun_t *pending = &campaignPending[campaignRequestMission - 1];
    qboolean sameRun = pending->elapsedMsec == campaignRequestRun.elapsedMsec && pending->deaths == campaignRequestRun.deaths;
    qboolean current = campaignResult.mission == campaignRequestMission && campaignResult.state == CAMPAIGN_SCORE_PENDING;

    if ( code == 200 && rank > 0 && bestScore >= 0 && bestScore <= 10000 ) {
        campaignStandings[campaignRequestMission - 1] = (uiCampaignStanding_t){ bestScore, rank };
        if ( sameRun ) memset( pending, 0, sizeof( *pending ) );
        if ( current && !pending->elapsedMsec ) {
            campaignResult.bestScore = bestScore;
            campaignResult.rank = rank;
            campaignResult.state = CAMPAIGN_SCORE_SAVED;
        }
    } else if ( sameRun ) {
        if ( ( code == 0 || code == 429 || code >= 500 ) && ++pending->attempts < 3 ) {
            pending->retryAt = time( NULL ) + 5;
        } else {
            memset( pending, 0, sizeof( *pending ) );
            if ( current ) campaignResult.state = CAMPAIGN_SCORE_UNAVAILABLE;
        }
    }
}

static void Auth_CampaignSubmit( void ) {
    int i;
    time_t now = time( NULL );

    for ( i = 0; i < CAMPAIGN_MISSION_COUNT; i++ ) {
        const campaignRun_t *run = &campaignPending[i];
        if ( !run->elapsedMsec || now < run->retryAt ) continue;
        campaignRequestMission = i + 1;
        campaignRequestRun = *run;
        Com_sprintf( request.body, sizeof( request.body ), "{\"p_mission\":%d,\"p_elapsed_ms\":%d,\"p_deaths\":%d}",
            campaignRequestMission, run->elapsedMsec, run->deaths );
        Q_strncpyz( request.bearer, accessToken, sizeof( request.bearer ) );
        Auth_Start( AUTH_CAMPAIGN );
        return;
    }
    if ( campaignStandingsRequested ) {
        campaignStandingsRequested = qfalse;
        campaignStandingsRefreshAt = now + 30;
        Q_strncpyz( request.body, "{}", sizeof( request.body ) );
        Q_strncpyz( request.bearer, accessToken, sizeof( request.bearer ) );
        Auth_Start( AUTH_CAMPAIGN_STANDINGS );
    }
}

static qboolean Auth_UUID( const char *value ) {
    int i;
    if ( strlen( value ) != 36 ) return qfalse;
    for ( i = 0; i < 36; i++ ) {
        if ( i == 8 || i == 13 || i == 18 || i == 23 ) {
            if ( value[i] != '-' ) return qfalse;
        } else if ( !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')) ) return qfalse;
    }
    return qtrue;
}

void CL_AuthHistoryDisconnect( void ) {
    historyGeneration++;
    historyRun[0] = historyServer[0] = 0;
    historyConnection = 0;
    historyRequested = historySubmitted = qfalse;
    historyRetryAt = 0;
}

void CL_AuthHistoryChallenge( const char *server, const char *run, int connection ) {
    if ( !Auth_UUID( server ) || !Auth_UUID( run ) || connection <= 0 ) return;
    if ( !strcmp( historyServer, server ) && !strcmp( historyRun, run ) && historyConnection == connection ) return;
    CL_AuthHistoryDisconnect();
    Q_strncpyz( historyServer, server, sizeof( historyServer ) );
    Q_strncpyz( historyRun, run, sizeof( historyRun ) );
    historyConnection = connection;
}

static void Auth_Clear( void *data, size_t size ) {
    volatile unsigned char *p = data;
    while ( size-- ) *p++ = 0;
}

static qboolean Auth_String( const char *json, const char *end, const char *key,
        char *out, unsigned int size ) {
    const char *value = JSON_ObjectGetNamedValue( json, end, key );
    unsigned int length;
    out[0] = 0;
    if ( !value || JSON_ValueGetType( value, end ) != JSONTYPE_STRING ) return qfalse;
    length = JSON_ValueGetString( value, end, out, size );
    return length > 1 && length < size;
}

/* Steam names arrive cleaned by the steam-login function: printable ASCII,
 * trimmed, at most 24 characters, without the quotes, backslashes, semicolons
 * and colour codes that userinfo can't carry. */
static qboolean Auth_ValidName( const char *name ) {
    const unsigned char *p;
    size_t length = name ? strlen( name ) : 0;
    if ( length < 1 || length > 24 || name[0] == ' ' || name[length - 1] == ' ' ) return qfalse;
    for ( p = (const unsigned char *)name; *p; p++ ) {
        if ( *p < 32 || *p > 126 || *p == '"' || *p == '\\' || *p == ';' || *p == '^' ) return qfalse;
    }
    return qtrue;
}

static qboolean Auth_ValidToken( const char *token ) {
    const unsigned char *p;
    for ( p = (const unsigned char *)token; *p; p++ ) {
        if ( !( ( *p >= 'A' && *p <= 'Z' ) || ( *p >= 'a' && *p <= 'z' ) ||
            ( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' || *p == '.' ) ) return qfalse;
    }
    return token[0] != 0;
}

static void Auth_ApplyName( void ) {
    if ( !account.signedIn || !account.displayName[0] ) return;
    if ( !nameApplied ) {
        Cvar_VariableStringBuffer( "name", guestName, sizeof( guestName ) );
        nameApplied = qtrue;
    }
    if ( strcmp( Cvar_VariableString( "name" ), account.displayName ) )
        Cvar_Set( "name", account.displayName );
}

static int SDLCALL Auth_Worker( void *data ) {
    authRequest_t *job = data;
    const char *path = "/functions/v1/steam-login";
    if ( job->stage == AUTH_PROFILE ) path = "/rest/v1/rpc/get_player_profile";
    else if ( job->stage == AUTH_REFRESH ) path = "/auth/v1/token?grant_type=refresh_token";
    else if ( job->stage == AUTH_HISTORY ) path = "/rest/v1/rpc/create_match_ticket";
    else if ( job->stage == AUTH_CAMPAIGN ) path = "/rest/v1/rpc/submit_campaign_clear";
    else if ( job->stage == AUTH_CAMPAIGN_STANDINGS ) path = "/rest/v1/rpc/get_my_campaign_standings";
    job->code = CL_AuthRequest( job->host, job->key,
        path, job->body, job->bearer, job->response, sizeof( job->response ) );
    Auth_Clear( job->body, sizeof( job->body ) );
    Auth_Clear( job->bearer, sizeof( job->bearer ) );
    SDL_AtomicSet( &job->done, 1 );
    return 0;
}

void CL_AuthInit( void ) {
    char *config = NULL;
    char ref[64];
    int i, length;

    Play_QueueProject( "" );
    memset( &account, 0, sizeof( account ) );
    memset( &request, 0, sizeof( request ) );
    Auth_Clear( refreshToken, sizeof( refreshToken ) );
    Auth_Clear( accessToken, sizeof( accessToken ) );
    expiresAt = refreshAt = steamRetryAt = 0;
    CL_SteamInit();
    Q_strncpyz( account.message, "Online sign-in is not configured.", sizeof( account.message ) );
    length = FS_ReadFile( "auth.json", (void **)&config );
    if ( length <= 0 ) {
        if ( config ) FS_FreeFile( config );
        return;
    }
    if ( length > 8192 ||
        !Auth_String( config, config + length, "project_ref", ref, sizeof( ref ) ) ||
        !Auth_String( config, config + length, "publishable_key", request.key, sizeof( request.key ) ) ) {
        FS_FreeFile( config );
        return;
    }
    FS_FreeFile( config );
    for ( i = 0; ref[i]; i++ ) {
        if ( !( ( ref[i] >= 'a' && ref[i] <= 'z' ) || ( ref[i] >= '0' && ref[i] <= '9' ) ) ) return;
    }
    if ( strlen( ref ) != 20 || strchr( request.key, '\\' ) ||
        ( strncmp( request.key, "sb_publishable_", 15 ) && strncmp( request.key, "eyJ", 3 ) ) ) return;
    for ( i = 0; request.key[i]; i++ ) {
        if ( (unsigned char)request.key[i] < 33 || (unsigned char)request.key[i] > 126 ) return;
    }
    Com_sprintf( request.host, sizeof( request.host ), "%s.supabase.co", ref );
    account.configured = qtrue;
    Play_QueueProject( ref );
    /* Sign-in starts on its own from CL_AuthFrame once Steam answers. */
    Q_strncpyz( account.message, CL_SteamAvailable() ? "Signing in with Steam..." : CL_SteamStatus(),
        sizeof( account.message ) );
}

static void Auth_Start( authStage_t stage ) {
    request.stage = stage;
    request.cancelled = qfalse;
    request.response[0] = 0;
    SDL_AtomicSet( &request.done, 0 );
    account.busy = qtrue;
    request.thread = SDL_CreateThread( Auth_Worker, "account-login", &request );
    if ( !request.thread ) {
        account.busy = qfalse;
        Auth_Clear( request.body, sizeof( request.body ) );
        Auth_Clear( request.bearer, sizeof( request.bearer ) );
        if ( stage == AUTH_CAMPAIGN ) {
            Auth_CampaignResponse( 0, 0, 0 );
            return;
        }
        if ( stage == AUTH_CAMPAIGN_STANDINGS ) return;
        Q_strncpyz( account.message, "Unable to start sign-in. Retrying shortly.", sizeof( account.message ) );
        if ( stage == AUTH_STEAM ) steamRetryAt = time( NULL ) + 30;
        if ( stage == AUTH_PROFILE ) {
            Auth_ClearSession();
            refreshAt = time( NULL ) + 30;
        }
        if ( stage == AUTH_REFRESH ) refreshAt = time( NULL ) + 30;
    }
}

static void Auth_Refresh( void ) {
    /* Tokens are validated before reaching this JSON body. */
    Com_sprintf( request.body, sizeof( request.body ), "{\"refresh_token\":\"%s\"}", refreshToken );
    Auth_Start( AUTH_REFRESH );
}

/* Sends a finished Steam ticket to the steam-login function, or asks Steam
 * for one. The ticket is the only credential; nothing is saved to disk. */
static void Auth_SteamSignIn( void ) {
    char ticket[STEAM_TICKET_HEX];
    if ( !account.configured || account.signedIn || request.thread ) return;
    if ( !CL_SteamAvailable() ) {
        Q_strncpyz( account.message, CL_SteamStatus(), sizeof( account.message ) );
        return;
    }
    if ( CL_SteamTakeTicket( ticket, sizeof( ticket ) ) ) {
        Com_sprintf( request.body, sizeof( request.body ), "{\"ticket\":\"%s\"}", ticket );
        Auth_Clear( ticket, sizeof( ticket ) );
        request.bearer[0] = 0;
        Q_strncpyz( account.message, "Signing in with Steam...", sizeof( account.message ) );
        Auth_Start( AUTH_STEAM );
        return;
    }
    if ( CL_SteamTicketPending() || time( NULL ) < steamRetryAt ) return;
    /* A ticket that never arrives is asked for again after this long. */
    steamRetryAt = time( NULL ) + 30;
    if ( CL_SteamRequestTicket() )
        Q_strncpyz( account.message, "Signing in with Steam...", sizeof( account.message ) );
    else if ( CL_SteamStatus()[0] )
        Q_strncpyz( account.message, CL_SteamStatus(), sizeof( account.message ) );
}

void CL_AuthRetry( void ) {
    if ( account.signedIn || request.thread ) return;
    steamRetryAt = 0;
    Auth_SteamSignIn();
}

static void Auth_ClearSession( void ) {
    if ( historySubmitted ) CL_AddReliableCommand( "historyTicket clear", qfalse );
    historySubmitted = historyRequested = qfalse;
    historyRetryAt = 0;
    historyGeneration++;
    account.signedIn = qfalse;
    account.needsDisplayName = qfalse;
    memset( campaignPending, 0, sizeof( campaignPending ) );
    memset( campaignStandings, 0, sizeof( campaignStandings ) );
    campaignStandingsRequested = qfalse;
    campaignStandingsRefreshAt = 0;
    CL_AuthCampaignStart( campaignResult.mission, qfalse );
    Auth_Clear( account.displayName, sizeof( account.displayName ) );
    if ( nameApplied ) Cvar_Set( "name", guestName );
    nameApplied = qfalse;
    Auth_Clear( guestName, sizeof( guestName ) );
    Auth_Clear( account.email, sizeof( account.email ) );
    Auth_Clear( account.playerId, sizeof( account.playerId ) );
    Auth_Clear( accessToken, sizeof( accessToken ) );
    expiresAt = 0;
}

/* Drops the session and starts again from a fresh Steam ticket. */
static void Auth_Restart( const char *message, int delaySeconds ) {
    request.cancelled = qtrue;
    Auth_ClearSession();
    Auth_Clear( refreshToken, sizeof( refreshToken ) );
    refreshAt = 0;
    steamRetryAt = time( NULL ) + delaySeconds;
    Q_strncpyz( account.message, message, sizeof( account.message ) );
}

void CL_AuthFrame( void ) {
    const char *end, *user, *value;
    int seconds;
    char email[256], playerId[64];
    CL_SteamFrame();
    if ( account.signedIn && time( NULL ) >= expiresAt ) {
        Auth_ClearSession();
        Q_strncpyz( account.message, "Session renewal pending. Retrying...", sizeof( account.message ) );
    }
    Auth_ApplyName();
    if ( !request.thread ) {
        if ( refreshToken[0] && time( NULL ) >= refreshAt ) {
            Auth_Refresh();
            return;
        }
        if ( !account.signedIn && !refreshToken[0] ) {
            Auth_SteamSignIn();
            return;
        }
        if ( account.signedIn && !account.needsDisplayName && historyRun[0] && !historyRequested && time( NULL ) >= historyRetryAt ) {
            Com_sprintf( request.body, sizeof( request.body ), "{\"p_server\":\"%s\",\"p_run\":\"%s\",\"p_connection\":%d}",
                historyServer, historyRun, historyConnection );
            Q_strncpyz( request.bearer, accessToken, sizeof( request.bearer ) );
            request.historyGeneration = historyGeneration;
            historyRequested = qtrue;
            Auth_Start( AUTH_HISTORY );
        } else if ( account.signedIn && !account.needsDisplayName ) {
            Auth_CampaignSubmit();
        }
        return;
    }
    if ( !SDL_AtomicGet( &request.done ) ) return;
    SDL_WaitThread( request.thread, NULL );
    request.thread = NULL;
    account.busy = qfalse;
    if ( request.cancelled ) {
        Auth_Clear( request.response, sizeof( request.response ) );
        return;
    }
    end = request.response + strlen( request.response );
    if ( request.stage == AUTH_CAMPAIGN_STANDINGS ) {
        if ( account.signedIn ) Auth_CampaignStandingsResponse( request.code, request.response, end );
        Auth_Clear( request.response, sizeof( request.response ) );
        return;
    }
    if ( request.stage == AUTH_CAMPAIGN ) {
        const char *rank = JSON_ObjectGetNamedValue( request.response, end, "rank" );
        const char *bestScore = JSON_ObjectGetNamedValue( request.response, end, "best_score" );
        if ( account.signedIn ) Auth_CampaignResponse( request.code,
            rank ? JSON_ValueGetInt( rank, end ) : 0, bestScore ? JSON_ValueGetInt( bestScore, end ) : -1 );
        Auth_Clear( request.response, sizeof( request.response ) );
        return;
    }
    if ( request.stage == AUTH_REFRESH && ( request.code == 0 || request.code == 429 || request.code >= 500 ) ) {
        refreshAt = time( NULL ) + 30;
        if ( !account.signedIn ) Q_strncpyz( account.message,
            "Sign-in service unavailable. Retrying shortly.", sizeof( account.message ) );
        goto finished;
    }
    if ( request.stage == AUTH_HISTORY ) {
        char ticket[37], command[64];
        if ( request.historyGeneration == historyGeneration && account.signedIn && request.code == 200 &&
            Auth_String( request.response, end, "ticket", ticket, sizeof( ticket ) ) && Auth_UUID( ticket ) ) {
            Com_sprintf( command, sizeof( command ), "historyTicket %s", ticket );
            CL_AddReliableCommand( command, qfalse );
            historySubmitted = qtrue;
            Auth_Clear( command, sizeof( command ) );
        } else if ( request.historyGeneration == historyGeneration && account.signedIn ) {
            historyRequested = qfalse;
            historyRetryAt = time( NULL ) + 30;
        }
        Auth_Clear( ticket, sizeof( ticket ) );
        Auth_Clear( request.response, sizeof( request.response ) );
        return;
    }
    if ( request.stage == AUTH_PROFILE ) {
        char name[25];
        if ( request.code == 200 &&
            Auth_String( request.response, end, "display_name", name, sizeof( name ) ) && Auth_ValidName( name ) ) {
            Q_strncpyz( account.displayName, name, sizeof( account.displayName ) );
            account.needsDisplayName = qfalse;
            Q_strncpyz( account.message, "Signed in with Steam.", sizeof( account.message ) );
            Auth_ApplyName();
        } else if ( request.code == 0 || request.code == 429 || request.code >= 500 ) {
            Auth_ClearSession();
            refreshAt = time( NULL ) + 30;
            Q_strncpyz( account.message, "Player profile unavailable. Retrying shortly.", sizeof( account.message ) );
        } else {
            Auth_Restart( "Could not load your player profile. Retrying...", 30 );
        }
        goto finished;
    }
    if ( request.code >= 200 && request.code < 300 ) {
        char nextAccess[8192], nextRefresh[AUTH_REFRESH_CAPACITY];
        user = JSON_ObjectGetNamedValue( request.response, end, "user" );
        value = JSON_ObjectGetNamedValue( request.response, end, "expires_in" );
        seconds = value ? JSON_ValueGetInt( value, end ) : 0;
        if ( user && Auth_String( user, end, "id", playerId, sizeof( playerId ) ) && Auth_UUID( playerId ) &&
            Auth_String( user, end, "email", email, sizeof( email ) ) && seconds > 0 &&
            Auth_String( request.response, end, "access_token", nextAccess, sizeof( nextAccess ) ) &&
            Auth_ValidToken( nextAccess ) &&
            Auth_String( request.response, end, "refresh_token", nextRefresh, sizeof( nextRefresh ) ) &&
            Auth_ValidToken( nextRefresh ) &&
            ( !account.signedIn || !strcmp( account.playerId, playerId ) ) ) {
            qboolean renewing = account.signedIn;
            Q_strncpyz( accessToken, nextAccess, sizeof( accessToken ) );
            Q_strncpyz( refreshToken, nextRefresh, sizeof( refreshToken ) );
            Auth_Clear( nextAccess, sizeof( nextAccess ) );
            Auth_Clear( nextRefresh, sizeof( nextRefresh ) );
            account.signedIn = qtrue;
            if ( !renewing ) account.needsDisplayName = qtrue;
            expiresAt = time( NULL ) + seconds;
            refreshAt = time( NULL ) + ( seconds > 60 ? seconds - 60 : 1 );
            Q_strncpyz( account.email, email, sizeof( account.email ) );
            Q_strncpyz( account.playerId, playerId, sizeof( account.playerId ) );
            if ( renewing ) goto finished;
            Q_strncpyz( request.body, "{}", sizeof( request.body ) );
            Q_strncpyz( request.bearer, accessToken, sizeof( request.bearer ) );
            Auth_Clear( request.response, sizeof( request.response ) );
            Q_strncpyz( account.message, "Loading player profile...", sizeof( account.message ) );
            Auth_Start( AUTH_PROFILE );
            return;
        } else {
            Auth_Restart( "Unexpected sign-in response. Retrying...", 30 );
        }
        Auth_Clear( nextAccess, sizeof( nextAccess ) );
        Auth_Clear( nextRefresh, sizeof( nextRefresh ) );
    } else if ( request.stage == AUTH_REFRESH ) {
        /* An expired or revoked session just signs in with Steam again. */
        Auth_Restart( "Session expired. Signing in with Steam again...", 0 );
    } else if ( request.code == 403 ) {
        Auth_Restart( "This Steam account can't play FRAGNAROK online.", 3600 );
    } else if ( request.code == 400 || request.code == 401 ) {
        Auth_Restart( "Steam couldn't confirm your account. Retrying in a minute.", 60 );
    } else {
        Auth_Restart( request.code == 429 ? "Too many sign-in attempts. Retrying shortly." :
            "Sign-in service unavailable. Retrying shortly.", 30 );
    }
finished:
    /* Never log provider responses or leave their tokens in the request buffer. */
    Auth_Clear( request.response, sizeof( request.response ) );
    Com_Printf( "Account: %s\n", account.message );
}

void CL_AuthGetStatus( uiAuthStatus_t *status ) {
    CL_AuthFrame();
    *status = account;
    status->remembered = 0;
}

qboolean CL_AuthRequireOnlineLogin( void ) {
    uiAuthStatus_t status;
    CL_AuthGetStatus( &status );
    if ( status.signedIn && !status.needsDisplayName ) return qtrue;
    Com_Printf( "Sign in with Steam before playing online.\n" );
    Cbuf_AddText( "fab_menu online\n" );
    return qfalse;
}

void CL_AuthShutdown( void ) {
    /* Let a request in flight finish so its tokens are cleared, not leaked. */
    while ( request.thread ) {
        while ( !SDL_AtomicGet( &request.done ) ) SDL_Delay( 1 );
        CL_AuthFrame();
    }
    Auth_ClearSession();
    CL_AuthHistoryDisconnect();
    Auth_Clear( refreshToken, sizeof( refreshToken ) );
    Auth_Clear( &request, sizeof( request ) );
    Auth_Clear( &account, sizeof( account ) );
    CL_SteamShutdown();
}

/* Play analytics uploads have their own worker so an outage cannot delay sign-in.
 * Only serialized event data survives a restart. Bearer tokens stay in memory. */
static struct {
    SDL_Thread *thread;
    SDL_atomic_t done;
    char name[64], host[128], key[2048], body[PLAY_ANALYTICS_BODY + 16], bearer[8192];
    int code;
    time_t next;
} playUpload;

static int SDLCALL Auth_PlayUpload( void *unused ) {
    const char *path = "/rest/v1/rpc/record_play_analytics";
    char response[512];
    playUpload.code = CL_AuthRequest( playUpload.host, playUpload.key, path,
        playUpload.body, playUpload.bearer, response, sizeof( response ) );
    if ( playUpload.code == 401 && playUpload.bearer[0] ) {
        playUpload.code = CL_AuthRequest( playUpload.host, playUpload.key, path,
            playUpload.body, "", response, sizeof( response ) );
    }
    Auth_Clear( playUpload.bearer, sizeof( playUpload.bearer ) );
    Auth_Clear( playUpload.body, sizeof( playUpload.body ) );
    SDL_AtomicSet( &playUpload.done, 1 );
    return 0;
}

void CL_AuthPlayIdentity( char id[37], char username[25] ) {
    Q_strncpyz( id, account.signedIn ? account.playerId : "", 37 );
    Q_strncpyz( username, account.signedIn ? account.displayName : "", 25 );
}

void CL_AuthPlayFrame( void ) {
    char body[PLAY_ANALYTICS_BODY], owner[37];
    const char *end, *player;
    time_t now = time( NULL );
    if ( playUpload.thread ) {
        if ( !SDL_AtomicGet( &playUpload.done ) ) return;
        SDL_WaitThread( playUpload.thread, NULL );
        playUpload.thread = NULL;
        if ( !strcmp( playUpload.host, request.host ) &&
             ((playUpload.code >= 200 && playUpload.code < 300) || playUpload.code == 400) ) {
            Play_QueueRemove( playUpload.name );
            playUpload.next = now + 1;
        } else {
            playUpload.next = now + 30;
        }
    }
    if ( !Play_QueueEnabled() || now < playUpload.next ) return;
    if ( !Play_QueueRead( playUpload.name, sizeof( playUpload.name ), body ) ) {
        playUpload.next = now + 2;
        return;
    }
    end = body + strlen( body );
    player = JSON_ObjectGetNamedValue( body, end, "player" );
    if ( player && account.signedIn && Auth_String( player, end, "account", owner, sizeof( owner ) ) &&
         !strcmp( owner, account.playerId ) ) {
        Q_strncpyz( playUpload.bearer, accessToken, sizeof( playUpload.bearer ) );
    }
    Q_strncpyz( playUpload.host, request.host, sizeof( playUpload.host ) );
    Q_strncpyz( playUpload.key, request.key, sizeof( playUpload.key ) );
    Com_sprintf( playUpload.body, sizeof( playUpload.body ), "{\"p_event\":%s}", body );
    SDL_AtomicSet( &playUpload.done, 0 );
    playUpload.thread = SDL_CreateThread( Auth_PlayUpload, "play-analytics", NULL );
    if ( !playUpload.thread ) {
        Auth_Clear( playUpload.bearer, sizeof( playUpload.bearer ) );
        Auth_Clear( playUpload.body, sizeof( playUpload.body ) );
        playUpload.next = now + 30;
    }
}
