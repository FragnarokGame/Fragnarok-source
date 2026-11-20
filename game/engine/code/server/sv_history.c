/* Private, append-only evidence for the trusted match-history collector. */
#include "server.h"
#include "../qcommon/play_analytics.h"
#include <time.h>
#ifdef STANDALONE

static fileHandle_t historyFile;
static char historyRun[37], historyServer[37];
static int historyConnection, historyNextTick;

qboolean SV_HistoryUUID( const char *value ) {
    int i;
    if ( strlen( value ) != 36 ) return qfalse;
    for ( i = 0; i < 36; i++ ) {
        if ( i == 8 || i == 13 || i == 18 || i == 23 ) {
            if ( value[i] != '-' ) return qfalse;
        } else if ( !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')) ) return qfalse;
    }
    return qtrue;
}

static void History_Write( const char *fields ) {
    char line[2048];
    int length;
    if ( !historyFile ) return;
    length = Com_sprintf( line, sizeof( line ), "{\"v\":1,\"ms\":%d,%s}\n", svs.time, fields );
    if ( FS_Write( line, length, historyFile ) != length ) {
        FS_FCloseFile( historyFile );
        historyFile = 0;
        Com_Printf( "Match history write failed; tracking disabled for this map.\n" );
    }
}

static int History_Connection( client_t *client ) {
    if ( !client->historyConnection ) client->historyConnection = ++historyConnection;
    return client->historyConnection;
}

void SV_HistoryFrame( qboolean force ) {
    int i, active, team, score, playerClass, warmup, bot;
    client_t *client;
    playerState_t *ps;
    if ( !historyFile || !sv.gameClients ) return;
    warmup = atoi( sv.configstrings[CS_WARMUP] );
    for ( i = 0; i < sv_maxclients->integer; i++ ) {
        client = &svs.clients[i];
        if ( client->state != CS_ACTIVE || atoi( Info_ValueForKey( client->userinfo, "menuwatch" ) ) ) continue;
        ps = SV_GameClientNum( i );
        team = ps->persistant[PERS_TEAM];
        score = ps->persistant[PERS_SCORE];
        playerClass = ps->stats[STAT_PLAYER_CLASS];
        active = team != TEAM_SPECTATOR && !warmup && !atoi( sv.configstrings[CS_INTERMISSION] ) && ps->pm_type != PM_INTERMISSION;
        if ( force || !client->historyConnection || client->historyActive != active || client->historyTeam != team ||
            client->historyScore != score || client->historyClass != playerClass ) {
            bot = client->netchan.remoteAddress.type == NA_BOT;
            // Bot skill lets ratings treat bot combat as play against a known, fixed opponent.
            History_Write( va( "\"event\":\"player\",\"connection\":%d,\"bot\":%s,\"skill\":%d,\"active\":%s,\"team\":%d,\"score\":%d,\"class\":%d",
                History_Connection( client ), bot ? "true" : "false",
                bot ? (int)Com_Clamp( 1, 5, atof( Info_ValueForKey( client->userinfo, "skill" ) ) ) : 0,
                active ? "true" : "false", team, score, playerClass ) );
            client->historyActive = active;
            client->historyTeam = team;
            client->historyScore = score;
            client->historyClass = playerClass;
        }
    }
    if ( force || svs.time >= historyNextTick ) {
        History_Write( "\"event\":\"tick\"" );
        historyNextTick = svs.time + 1000;
    }
}

void SV_HistoryStop( const char *reason ) {
    if ( !historyFile ) return;
    SV_HistoryFrame( qtrue );
    History_Write( va( "\"event\":\"stop\",\"reason\":\"%s\"", reason ) );
    FS_FCloseFile( historyFile );
    historyFile = 0;
}

void SV_HistoryStart( void ) {
    byte random[16];
    char map[MAX_QPATH], path[MAX_QPATH];
    const char *mode;
    int i;
    /* Apply settings from startup configs before opening this map's journal. */
    Cvar_Get( "sv_matchHistory", "0", CVAR_LATCH );
    Cvar_Get( "sv_historyServer", "", CVAR_LATCH );
    if ( !Cvar_VariableIntegerValue( "sv_matchHistory" ) ) return;
    if ( Cvar_VariableIntegerValue( "g_gametype" ) != GT_FFA && Cvar_VariableIntegerValue( "g_gametype" ) != GT_TEAM ) {
        Com_Printf( "Match history supports the free-for-all and team match types only.\n" );
        return;
    }
    Q_strncpyz( historyServer, Cvar_VariableString( "sv_historyServer" ), sizeof( historyServer ) );
    if ( !SV_HistoryUUID( historyServer ) || !Sys_RandomBytes( random, sizeof( random ) ) ) {
        Com_Printf( "Match history needs a registered sv_historyServer UUID and system entropy.\n" );
        return;
    }
    random[6] = (random[6] & 15) | 64;
    random[8] = (random[8] & 63) | 128;
    Com_sprintf( historyRun, sizeof( historyRun ), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],
        random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15] );
    Com_sprintf( path, sizeof( path ), "history/%s.jsonl", historyRun );
    FS_FOpenFileByMode( path, &historyFile, FS_APPEND_SYNC );
    if ( !historyFile ) { Com_Printf( "Could not open private match history journal.\n" ); return; }
    historyConnection = 0;
    historyNextTick = svs.time;
    for ( i = 0; i < sv_maxclients->integer; i++ ) {
        svs.clients[i].historyConnection = 0;
        svs.clients[i].historyTicketSubmitted = qfalse;
        svs.clients[i].historyNextTicket = 0;
    }
    Q_strncpyz( map, Cvar_VariableString( "mapname" ), sizeof( map ) );
    for ( i = 0; map[i]; i++ ) if ( !isalnum( (unsigned char)map[i] ) && map[i] != '_' && map[i] != '-' ) map[i] = '_';
    // Each game type gets its own rating, so name the special modes rather
    // than recording them as the Deathmatch or Team Deathmatch they run on.
    if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_TEAM ) {
        mode = Cvar_VariableIntegerValue( "g_killshift" ) ? "killshift" :
            Cvar_VariableIntegerValue( "g_infected" ) ? "infected" :
            Cvar_VariableIntegerValue( "g_bugHunt" ) ? "bug_hunt" : "team_deathmatch";
    } else {
        mode = Cvar_VariableIntegerValue( "g_lastManStanding" ) ? "last_man_standing" :
            Cvar_VariableIntegerValue( "g_davidGoliath" ) ? "david_and_goliath" :
            Cvar_VariableIntegerValue( "g_leprechaun" ) ? "kill_the_leprechaun" : "deathmatch";
    }
    History_Write( va( "\"event\":\"start\",\"server_id\":\"%s\",\"run_id\":\"%s\",\"epoch\":%ld,\"map\":\"%s\",\"mode\":\"%s\","
        "\"rules\":{\"gametype\":%d,\"fraglimit\":%d,\"timelimit\":%d,\"team_count\":%d,\"classes\":%d,\"david_goliath\":%d}",
        historyServer, historyRun, (long)time( NULL ), map, mode, Cvar_VariableIntegerValue( "g_gametype" ),
        Cvar_VariableIntegerValue( "fraglimit" ), Cvar_VariableIntegerValue( "timelimit" ),
        Cvar_VariableIntegerValue( "g_teamCount" ), Cvar_VariableIntegerValue( "g_playerClasses" ),
        Cvar_VariableIntegerValue( "g_davidGoliath" ) ) );
    Com_Printf( "Private match history journal: %s\n", path );
}

void SV_HistoryChallenge( client_t *client ) {
    if ( !historyFile || client->netchan.remoteAddress.type == NA_BOT ||
        atoi( Info_ValueForKey( client->userinfo, "menuwatch" ) ) ) return;
    SV_SendServerCommand( client, "historyChallenge %s %s %d", historyServer, historyRun, History_Connection( client ) );
}

void SV_HistoryTicket( client_t *client ) {
    if ( !historyFile || client->state != CS_ACTIVE || client->netchan.remoteAddress.type == NA_BOT ||
        !client->historyConnection || Cmd_Argc() != 2 ) return;
    SV_HistoryFrame( qfalse );
    if ( !strcmp( Cmd_Argv(1), "clear" ) ) {
        if ( !client->historyTicketSubmitted ) return;
        History_Write( va( "\"event\":\"identity_clear\",\"connection\":%d", client->historyConnection ) );
        client->historyTicketSubmitted = qfalse;
    } else if ( SV_HistoryUUID( Cmd_Argv(1) ) && !client->historyTicketSubmitted && svs.time >= client->historyNextTicket ) {
        History_Write( va( "\"event\":\"ticket\",\"connection\":%d,\"ticket\":\"%s\"", client->historyConnection, Cmd_Argv(1) ) );
        client->historyTicketSubmitted = qtrue;
        client->historyNextTicket = svs.time + 1000;
    }
}

void SV_HistoryLeave( client_t *client, const char *reason ) {
    const char *category = strstr( reason, "timed out" ) ? "timeout" :
        !strcmp( reason, "disconnected" ) ? "disconnect" : !strcmp( reason, "bot_removed" ) ? "bot_removed" : "removed";
    if ( !historyFile || !client->historyConnection ) return;
    SV_HistoryFrame( qtrue );
    History_Write( va( "\"event\":\"leave\",\"connection\":%d,\"reason\":\"%s\"", client->historyConnection, category ) );
    client->historyConnection = 0;
}

qboolean SV_HistoryGameEvent( const char *message ) {
    int attacker, victim, mod, friendly, warmup;
    int red, blue, green, yellow, winner;
    if ( strncmp( message, "FRAGHISTORY ", 12 ) ) return qfalse;
    SV_HistoryFrame( qfalse );
    if ( sscanf( message, "FRAGHISTORY kill %d %d %d %d %d", &attacker, &victim, &mod, &friendly, &warmup ) == 5 ) {
        if ( warmup || victim < 0 || victim >= sv_maxclients->integer ) return qtrue;
        if ( attacker != victim && !friendly ) SV_PlayKill( attacker );
        if ( !historyFile ) return qtrue;
        History_Write( va( "\"event\":\"kill\",\"attacker\":%d,\"victim\":%d,\"mod\":%d,\"friendly\":%s",
            attacker >= 0 && attacker < sv_maxclients->integer ? History_Connection( &svs.clients[attacker] ) : 0,
            History_Connection( &svs.clients[victim] ), mod, friendly ? "true" : "false" ) );
    } else if ( sscanf( message, "FRAGHISTORY end %d %d %d %d %d", &red, &blue, &green, &yellow, &winner ) == 5 ) {
        SV_PlayStop( PLAY_COMPLETED );
        History_Write( va( "\"event\":\"round_end\",\"result\":{\"team_scores\":{\"red\":%d,\"blue\":%d,\"green\":%d,\"yellow\":%d},\"winner_connection\":%d}",
            red, blue, green, yellow, winner >= 0 && winner < sv_maxclients->integer ? History_Connection( &svs.clients[winner] ) : 0 ) );
    }
    else if ( !strcmp( message, "FRAGHISTORY restart\n" ) ) History_Write( "\"event\":\"restart\"" );
    return qtrue;
}
#else
qboolean SV_HistoryUUID( const char *value ) { return qfalse; }
void SV_HistoryStart( void ) {}
void SV_HistoryStop( const char *reason ) {}
void SV_HistoryFrame( qboolean force ) {}
void SV_HistoryChallenge( client_t *client ) {}
void SV_HistoryTicket( client_t *client ) {}
void SV_HistoryLeave( client_t *client, const char *reason ) {}
qboolean SV_HistoryGameEvent( const char *message ) { return qfalse; }
#endif
