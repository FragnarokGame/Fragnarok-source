/*
 * FRAGNAROK Discord Rich Presence. GPL-2.0-or-later.
 * Implements https://docs.discord.com/developers/topics/rpc directly so the
 * C client needs no additional SDK or runtime libraries. All IO is nonblocking.
 */
#include "client.h"
#include "cl_discord.h"
#include "../sys/sys_local.h"
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

/* Use a private parser instance, like the account client and renderer. */
#define JSON_ArrayGetFirstValue Discord_JSON_ArrayGetFirstValue
#define JSON_ArrayGetNextValue Discord_JSON_ArrayGetNextValue
#define JSON_ArrayGetIndex Discord_JSON_ArrayGetIndex
#define JSON_ArrayGetValue Discord_JSON_ArrayGetValue
#define JSON_ObjectGetNamedValue Discord_JSON_ObjectGetNamedValue
#define JSON_ValueGetType Discord_JSON_ValueGetType
#define JSON_ValueGetString Discord_JSON_ValueGetString
#define JSON_ValueGetDouble Discord_JSON_ValueGetDouble
#define JSON_ValueGetFloat Discord_JSON_ValueGetFloat
#define JSON_ValueGetInt Discord_JSON_ValueGetInt
#define JSON_IMPLEMENTATION
#include "../qcommon/json.h"

#define DISCORD_CAPACITY 65536
#define DISCORD_RETRY 10000
#define DISCORD_UPDATE 15000

static cvar_t *discordEnabled, *discordApplication;
static struct {
#ifdef _WIN32
    HANDLE pipe;
#else
    int socket;
#endif
    qboolean open, ready, clearing;
    byte input[DISCORD_CAPACITY + 9], output[DISCORD_CAPACITY + 8];
    unsigned int received, queued, sent;
    unsigned int pollAt, retryAt, openedAt, queuedAt, updateAt, nonce;
    char application[32], activity[1024];
    time_t started;
} discord;

static qboolean Discord_Due( unsigned int now, unsigned int deadline ) {
    return (int)( now - deadline ) >= 0;
}

static void Discord_Close( unsigned int now ) {
    if ( discord.open ) {
#ifdef _WIN32
        CloseHandle( discord.pipe );
#else
        close( discord.socket );
#endif
    }
    discord.open = discord.ready = discord.clearing = qfalse;
    discord.received = discord.queued = discord.sent = 0;
    discord.activity[0] = 0;
    discord.retryAt = now + DISCORD_RETRY;
}

static qboolean Discord_ValidId( const char *id ) {
    int i, length = strlen( id );
    if ( length < 17 || length > 20 || id[0] == '0' ) return qfalse;
    for ( i = 0; i < length; i++ ) {
        if ( id[i] < '0' || id[i] > '9' ) return qfalse;
    }
    return qtrue;
}

static qboolean Discord_Open( void ) {
    int i;
    char path[MAX_OSPATH];
#ifdef _WIN32
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    for ( i = 0; i < 10; i++ ) {
        Com_sprintf( path, sizeof( path ), "\\\\?\\pipe\\discord-ipc-%d", i );
        discord.pipe = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, 0,
            NULL, OPEN_EXISTING, 0, NULL );
        if ( discord.pipe == INVALID_HANDLE_VALUE ) continue;
        if ( SetNamedPipeHandleState( discord.pipe, &mode, NULL, NULL ) ) {
            discord.open = qtrue;
            return qtrue;
        }
        CloseHandle( discord.pipe );
    }
#else
    const char *directories[] = { getenv( "XDG_RUNTIME_DIR" ), getenv( "TMPDIR" ),
        getenv( "TMP" ), getenv( "TEMP" ), "/tmp" };
    int directory;
    for ( directory = 0; directory < ARRAY_LEN( directories ); directory++ ) {
        if ( !directories[directory] || !directories[directory][0] ) continue;
        for ( i = 0; i < 10; i++ ) {
            struct sockaddr_un address;
            int flags;
            Com_sprintf( path, sizeof( path ), "%s/discord-ipc-%d", directories[directory], i );
            if ( strlen( path ) >= sizeof( address.sun_path ) ) continue;
            discord.socket = socket( AF_UNIX, SOCK_STREAM, 0 );
            if ( discord.socket < 0 ) continue;
            flags = fcntl( discord.socket, F_GETFL, 0 );
            if ( flags < 0 || fcntl( discord.socket, F_SETFL, flags | O_NONBLOCK ) < 0 ) {
                close( discord.socket );
                continue;
            }
#ifdef SO_NOSIGPIPE
            flags = 1;
            setsockopt( discord.socket, SOL_SOCKET, SO_NOSIGPIPE, &flags, sizeof( flags ) );
#endif
            memset( &address, 0, sizeof( address ) );
            address.sun_family = AF_UNIX;
            Q_strncpyz( address.sun_path, path, sizeof( address.sun_path ) );
            /* A pending local connection is retried later instead of blocking. */
            if ( connect( discord.socket, (struct sockaddr *)&address, sizeof( address ) ) == 0 ) {
                discord.open = qtrue;
                return qtrue;
            }
            close( discord.socket );
        }
    }
#endif
    return qfalse;
}

/* 0 means try next frame, -1 means the peer closed or IO failed. */
static int Discord_Read( void *buffer, unsigned int size ) {
#ifdef _WIN32
    DWORD available, read;
    if ( !PeekNamedPipe( discord.pipe, NULL, 0, NULL, &available, NULL ) ) return -1;
    if ( !available ) return 0;
    if ( size > available ) size = available;
    if ( !ReadFile( discord.pipe, buffer, size, &read, NULL ) ) return -1;
    return read ? (int)read : -1;
#else
    int count = recv( discord.socket, buffer, size, 0 );
    if ( count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ) return 0;
    return count > 0 ? count : -1;
#endif
}

static qboolean Discord_Flush( unsigned int now ) {
    int count;
    if ( !discord.queued ) return qtrue;
    if ( Discord_Due( now, discord.queuedAt + 5000 ) ) return qfalse;
#ifdef _WIN32
    {
        DWORD written;
        if ( !WriteFile( discord.pipe, discord.output + discord.sent,
            discord.queued - discord.sent, &written, NULL ) ) {
            if ( GetLastError() == ERROR_NO_DATA ) return qtrue;
            return qfalse;
        }
        count = written;
    }
#else
    {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        count = send( discord.socket, discord.output + discord.sent, discord.queued - discord.sent, flags );
        if ( count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ) return qtrue;
        if ( count <= 0 ) return qfalse;
    }
#endif
    discord.sent += count;
    if ( discord.sent == discord.queued ) discord.sent = discord.queued = 0;
    return qtrue;
}

static void Discord_Queue( unsigned int opcode, const void *payload, unsigned int length, unsigned int now ) {
    int i;
    for ( i = 0; i < 4; i++ ) {
        discord.output[i] = (opcode >> (8 * i)) & 255;
        discord.output[i + 4] = (length >> (8 * i)) & 255;
    }
    memcpy( discord.output + 8, payload, length );
    discord.queued = length + 8;
    discord.sent = 0;
    discord.queuedAt = now;
}

static unsigned int Discord_Word( const byte *bytes ) {
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8) |
        ((unsigned int)bytes[2] << 16) | ((unsigned int)bytes[3] << 24);
}

static qboolean Discord_Pump( unsigned int now ) {
    int i, count;
    unsigned int length, opcode, needed;
    if ( !Discord_Flush( now ) ) return qfalse;
    if ( discord.queued ) return qtrue;
    /* Bound the work even if a local peer continually sends data. */
    for ( i = 0; i < 16; i++ ) {
        needed = 8;
        if ( discord.received >= 8 ) {
            length = Discord_Word( discord.input + 4 );
            if ( length > DISCORD_CAPACITY ) return qfalse;
            needed += length;
        }
        if ( discord.received < needed ) {
            count = Discord_Read( discord.input + discord.received, needed - discord.received );
            if ( count < 0 ) return qfalse;
            if ( !count ) break;
            discord.received += count;
            continue;
        }
        opcode = Discord_Word( discord.input );
        length = needed - 8;
        discord.input[needed] = 0;
        if ( opcode == 2 ) return qfalse;
        if ( opcode == 3 ) {
            Discord_Queue( 4, discord.input + 8, length, now );
            discord.received = 0;
            return Discord_Flush( now );
        }
        if ( opcode == 1 ) {
            const char *json = (char *)discord.input + 8, *end = json + length;
            char event[32], command[32];
            JSON_ValueGetString( JSON_ObjectGetNamedValue( json, end, "evt" ), end, event, sizeof( event ) );
            JSON_ValueGetString( JSON_ObjectGetNamedValue( json, end, "cmd" ), end, command, sizeof( command ) );
            if ( !strcmp( event, "ERROR" ) ) {
                const char *data = JSON_ObjectGetNamedValue( json, end, "data" );
                char message[128];
                int code = JSON_ValueGetInt( JSON_ObjectGetNamedValue( data, end, "code" ), end );
                JSON_ValueGetString( JSON_ObjectGetNamedValue( data, end, "message" ), end, message, sizeof( message ) );
                Com_DPrintf( "Discord: activity request rejected (%d: %s); retrying later.\n", code, message );
                return qfalse;
            }
            if ( !strcmp( event, "READY" ) && !discord.ready ) {
                discord.ready = qtrue;
                discord.updateAt = now;
                Com_DPrintf( "Discord: desktop client ready.\n" );
            }
            if ( !strcmp( command, "SET_ACTIVITY" ) && !strcmp( event, "null" ) ) {
                Com_DPrintf( "Discord: activity acknowledged.\n" );
            }
        } else if ( opcode != 4 ) return qfalse;
        discord.received = 0;
    }
    return qtrue;
}

static void Discord_Quote( const char *value, char *out, size_t capacity ) {
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)value;
    for ( ; *p && used + 7 < capacity; p++ ) {
        if ( Q_IsColorString( (const char *)p ) ) { p++; continue; }
        if ( *p == '"' || *p == '\\' ) out[used++] = '\\';
        if ( *p < 32 || *p >= 127 ) out[used++] = ' ';
        else out[used++] = *p;
    }
    out[used] = 0;
}

static const char *Discord_Mode( const char *info ) {
    int type = atoi( Info_ValueForKey( info, "g_gametype" ) );
    if ( type == 0 && atoi( Info_ValueForKey( info, "g_leprechaun" ) ) ) return "Kill the Leprechaun";
    if ( type == 0 && atoi( Info_ValueForKey( info, "g_lastManStanding" ) ) ) return "Last Man Standing";
    if ( type == 0 && atoi( Info_ValueForKey( info, "g_armsRace" ) ) ) return "Arms Race";
    if ( type == 3 && atoi( Info_ValueForKey( info, "g_infected" ) ) ) return "Infected";
    if ( type == 3 && atoi( Info_ValueForKey( info, "g_bugHunt" ) ) ) return "Bug Hunt";
    if ( type == 3 && atoi( Info_ValueForKey( info, "g_killshift" ) ) ) return "Killshift";
    if ( atoi( Info_ValueForKey( info, "g_davidGoliath" ) ) ) return "David and Goliath";
    switch ( type ) {
    case 1: return "Duel";
    case 2: return "Single player";
    case 3: return "Team Deathmatch";
    case 4: return "Capture the Flag";
    case 5: return "One Flag CTF";
    case 6: return "Overload";
    case 7: return "Harvester";
    default: return "Deathmatch";
    }
}

static void Discord_Activity( char *out, size_t capacity ) {
    const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
    const char *details = "In menus", *state = "", *map;
    char mapName[128], quotedMap[256], quotedDetails[256], quotedState[512], stateField[600];
    if ( clc.state >= CA_CONNECTING && !Cvar_VariableIntegerValue( "g_menuBackground" ) ) {
        if ( clc.state < CA_ACTIVE ) details = "Connecting / loading";
        else {
            details = Discord_Mode( info );
            map = Info_ValueForKey( info, "mapname" );
            if ( !strcmp( map, "test_chamber" ) ) map = "Test Chamber";
            else if ( !strcmp( map, "rift_foundry" ) ) map = "Rift Foundry";
            else if ( !strcmp( map, "bastion" ) ) map = "Bastion";
            else if ( !strcmp( map, "skyreach" ) ) map = "Skyreach";
            else if ( !strcmp( map, "station" ) ) map = "Shin-Kagura Station";
            else if ( !strcmp( map, "horologe" ) ) map = "Horologe";
            else if ( !strcmp( map, "gallows" ) ) map = "Gallows Point";
            Q_strncpyz( mapName, map, sizeof( mapName ) );
            Discord_Quote( mapName, quotedMap, sizeof( quotedMap ) );
            Com_sprintf( quotedState, sizeof( quotedState ), "%s on %s",
                clc.demoplaying ? "Watching a demo" : "Playing", quotedMap );
            state = quotedState;
        }
    }
    Discord_Quote( details, quotedDetails, sizeof( quotedDetails ) );
    /* Discord rejects empty optional strings, so menus omit the map/state line. */
    stateField[0] = 0;
    if ( state[0] ) Com_sprintf( stateField, sizeof( stateField ), ",\"state\":\"%s\"", state );
    /* Only fixed strings and the escaped map enter the payload. No server or account data. */
    Com_sprintf( out, capacity,
        "{\"type\":0,\"details\":\"%s\"%s,\"timestamps\":{\"start\":%.0f}}",
        quotedDetails, stateField, (double)discord.started );
}

static void Discord_SetActivity( const char *activity, unsigned int now ) {
    char payload[1400];
    Com_sprintf( payload, sizeof( payload ),
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":%s},\"nonce\":\"%u\"}",
        Sys_PID(), activity, ++discord.nonce );
    Discord_Queue( 1, payload, strlen( payload ), now );
}

void CL_DiscordInit( void ) {
    memset( &discord, 0, sizeof( discord ) );
    discord.started = time( NULL );
    discordEnabled = Cvar_Get( "cl_discordPresence", "1", CVAR_ARCHIVE );
    discordApplication = Cvar_Get( "cl_discordApplicationId", FRAGNAROK_DISCORD_APPLICATION_ID,
        CVAR_ARCHIVE | CVAR_PROTECTED );
}

void CL_DiscordFrame( void ) {
    unsigned int now = Sys_Milliseconds();
    char activity[1024], handshake[128];
    qboolean enabled;
    if ( !discordEnabled || !Discord_Due( now, discord.pollAt ) ) return;
    discord.pollAt = now + 100;
    enabled = discordEnabled->integer && Discord_ValidId( discordApplication->string );
    if ( discord.open && (discord.clearing || !enabled || strcmp( discord.application, discordApplication->string )) ) {
        if ( discord.ready && !discord.clearing ) {
            if ( !Discord_Flush( now ) ) { Discord_Close( now ); return; }
            if ( discord.queued ) return;
            Discord_SetActivity( "null", now );
            discord.clearing = qtrue;
        }
        if ( !Discord_Flush( now ) || !discord.queued ) {
            Discord_Close( now );
            discord.retryAt = now;
        }
        return;
    }
    if ( !enabled ) return;
    if ( !discord.open ) {
        if ( !Discord_Due( now, discord.retryAt ) ) return;
        discord.retryAt = now + DISCORD_RETRY;
        if ( !Discord_Open() ) return;
        Q_strncpyz( discord.application, discordApplication->string, sizeof( discord.application ) );
        discord.openedAt = now;
        Com_sprintf( handshake, sizeof( handshake ), "{\"v\":1,\"client_id\":\"%s\"}", discord.application );
        Discord_Queue( 0, handshake, strlen( handshake ), now );
    }
    if ( !Discord_Pump( now ) || (!discord.ready && Discord_Due( now, discord.openedAt + 10000 )) ) {
        Discord_Close( now );
        return;
    }
    if ( !discord.ready || discord.queued || !Discord_Due( now, discord.updateAt ) ) return;
    Discord_Activity( activity, sizeof( activity ) );
    if ( !strcmp( activity, discord.activity ) ) return;
    Discord_SetActivity( activity, now );
    Q_strncpyz( discord.activity, activity, sizeof( discord.activity ) );
    discord.updateAt = now + DISCORD_UPDATE;
    if ( !Discord_Flush( now ) ) Discord_Close( now );
}

void CL_DiscordShutdown( void ) {
    unsigned int now = Sys_Milliseconds();
    if ( discord.open && discord.ready ) {
        if ( Discord_Flush( now ) && !discord.queued ) {
            Discord_SetActivity( "null", now );
            Discord_Flush( now );
        }
    }
    Discord_Close( now );
    discordEnabled = discordApplication = NULL;
}
