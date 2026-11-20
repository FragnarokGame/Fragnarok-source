#include "server.h"
#include "../qcommon/play_analytics.h"
#include <time.h>

#ifdef STANDALONE
static playMatch_t playMatch;
static unsigned int playSequence;
static long playHeartbeat;
static fileHandle_t playJournal;
static char playServer[37], playHostReport[37];

static int SV_PlayMode( int gametype ) {
    if ( gametype == GT_TEAM ) {
        if ( Cvar_VariableIntegerValue( "g_infected" ) ) return PLAY_INFECTED;
        if ( Cvar_VariableIntegerValue( "g_bugHunt" ) ) return PLAY_BUGHUNT;
        return Cvar_VariableIntegerValue( "g_killshift" ) ? PLAY_KILLSHIFT : PLAY_TEAM_DEATHMATCH;
    }
    if ( gametype == GT_FFA ) {
        if ( Cvar_VariableIntegerValue( "g_leprechaun" ) ) return PLAY_LEPRECHAUN;
        return Cvar_VariableIntegerValue( "g_lastManStanding" ) ? PLAY_LMS : PLAY_DEATHMATCH;
    }
    if ( gametype == GT_SINGLE_PLAYER ) return PLAY_DEATHMATCH;
    return PLAY_OTHER;
}

static void SV_PlayPublish( void ) {
    playMatch_t *m = &playMatch;
    SV_SetConfigstring( CS_PLAY_ANALYTICS, va( "%s %s %s %ld %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
        m->id, m->map, m->build, m->started, m->ended, m->finish, m->source, m->humans, m->bots,
        m->gametype, m->mode, m->campaign, m->selected, m->fraglimit, m->timelimit, m->botSkill, m->rules, m->teams, m->matchSeconds ) );
}

static void SV_PlayRecord( long now ) {
    char match[1024], body[PLAY_ANALYTICS_BODY];
    int length;
    playHeartbeat = now + 120;
    if ( !playJournal && !Play_QueueEnabled() ) return;
    Play_MatchJSON( &playMatch, match, sizeof( match ) );
    length = Com_sprintf( body, sizeof( body ),
        "{\"match\":%s,\"report\":\"%s\",\"seq\":%u,\"seen\":%ld,\"server\":\"%s\"}",
        match, playHostReport, ++playSequence, now, playServer );
    if ( playJournal ) {
        if ( FS_Write( body, length, playJournal ) != length || FS_Write( "\n", 1, playJournal ) != 1 ) {
            FS_FCloseFile( playJournal );
            playJournal = 0;
        }
    } else if ( !com_dedicated->integer ) {
        Play_Queue( playHostReport, playSequence, body );
    }
}

void SV_PlayInit( qboolean selected ) {
    playMatch_t *m = &playMatch;
    const char *token = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-";
    Com_Memset( m, 0, sizeof( *m ) );
    playServer[0] = 0;
    playSequence = 0;
    SV_SetConfigstring( CS_PLAY_ANALYTICS, "" );
    if ( Cvar_VariableIntegerValue( "g_menuBackground" ) || Cvar_VariableIntegerValue( "g_matchLobby" ) ||
         !Play_NewID( playHostReport ) || !Play_NewID( m->id ) ) return;

    Cvar_VariableStringBuffer( "mapname", m->map, sizeof( m->map ) );
    Q_strncpyz( m->build, PRODUCT_VERSION, sizeof( m->build ) );
    if ( !m->map[0] || strspn( m->map, token ) != strlen( m->map ) ||
        strspn( m->build, token ) != strlen( m->build ) ) {
        m->id[0] = 0;
        return;
    }
    m->source = com_dedicated->integer || Cvar_VariableIntegerValue( "sv_lanHost" ) ||
        Cvar_VariableIntegerValue( "sv_onlineHost" ) ? PLAY_COMMUNITY : PLAY_LOCAL;
    m->gametype = Cvar_VariableIntegerValue( "g_gametype" );
    m->mode = SV_PlayMode( m->gametype );
    m->campaign = Cvar_VariableIntegerValue( "g_campaignMission" );
    m->selected = selected && !com_dedicated->integer;
    m->fraglimit = Cvar_VariableIntegerValue( "fraglimit" );
    m->timelimit = Cvar_VariableIntegerValue( "timelimit" );
    m->botSkill = (int)( Cvar_VariableValue( "g_spSkill" ) * 100 );
    m->teams = Cvar_VariableIntegerValue( "g_teamCount" );
    m->matchSeconds = Cvar_VariableIntegerValue( "g_matchSeconds" );
    m->rules = ( Cvar_VariableIntegerValue( "g_playerClasses" ) ? 1 : 0 ) |
        ( Cvar_VariableIntegerValue( "g_davidGoliath" ) ? 2 : 0 ) |
        ( Cvar_VariableIntegerValue( "g_quickPlay" ) ? 4 : 0 );
    if ( com_dedicated->integer && SV_HistoryUUID( Cvar_VariableString( "sv_historyServer" ) ) ) {
        Cvar_VariableStringBuffer( "sv_historyServer", playServer, sizeof( playServer ) );
        FS_FOpenFileByMode( va( "history/play-%s.ndjson", m->id ), &playJournal, FS_APPEND_SYNC );
    }
    SV_PlayPublish();
}

void SV_PlayFrame( void ) {
    long now;
    int i;
    if ( !playMatch.id[0] || playMatch.ended || !sv.gameClients ) return;
    now = (long)time( NULL );
    if ( playMatch.started ) {
        if ( now >= playHeartbeat ) SV_PlayRecord( now );
        return;
    }
    if ( atoi( sv.configstrings[CS_WARMUP] ) || atoi( sv.configstrings[CS_INTERMISSION] ) ||
         Cvar_VariableIntegerValue( "g_matchJoining" ) ) return;
    if ( playMatch.mode == PLAY_LMS && !atoi( sv.configstrings[CS_LMS_STATE] ) ) return;
    if ( (playMatch.mode == PLAY_BUGHUNT || playMatch.mode == PLAY_INFECTED || playMatch.mode == PLAY_LEPRECHAUN) &&
        !atoi( sv.configstrings[CS_MODE_STATE] ) ) return;
    playMatch.humans = playMatch.bots = 0;
    for ( i = 0; i < sv_maxclients->integer; i++ ) {
        client_t *client = &svs.clients[i];
        if ( client->state != CS_ACTIVE || atoi( Info_ValueForKey( client->userinfo, "menuwatch" ) ) ||
            SV_GameClientNum( i )->persistant[PERS_TEAM] == TEAM_SPECTATOR ) continue;
        if ( client->netchan.remoteAddress.type == NA_BOT ) playMatch.bots++;
        else playMatch.humans++;
    }
    if ( !playMatch.humans && (playMatch.source == PLAY_LOCAL || !playMatch.bots) ) return;
    if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) playMatch.rules |= 8;
    playMatch.started = now;
    SV_PlayPublish();
    SV_PlayRecord( now );
}

void SV_PlayKill( int attacker ) {
    if ( !playMatch.started || playMatch.ended || attacker < 0 || attacker >= sv_maxclients->integer ) return;
    if ( svs.clients[attacker].netchan.remoteAddress.type != NA_BOT ) svs.clients[attacker].playKills++;
}

void SV_PlayStop( int reason ) {
    int i;
    if ( reason && playMatch.started && !playMatch.ended ) {
        playMatch.ended = MAX( playMatch.started, (long)time( NULL ) );
        playMatch.finish = reason;
        // Completion reaches the client with its final snapshot. Map changes
        // and shutdowns are also persisted directly by the host.
        SV_PlayPublish();
        if ( reason == PLAY_COMPLETED ) {
            for ( i = 0; i < sv_maxclients->integer; i++ ) {
                client_t *client = &svs.clients[i];
                if ( client->state != CS_ACTIVE || client->netchan.remoteAddress.type == NA_BOT ||
                    atoi( Info_ValueForKey( client->userinfo, "menuwatch" ) ) ) continue;
                SV_SendServerCommand( client, "playKills %s %d", playMatch.id, client->playKills );
            }
        }
        SV_PlayRecord( playMatch.ended );
    }
    if ( playJournal ) {
        FS_FCloseFile( playJournal );
        playJournal = 0;
    }
    if ( reason != PLAY_COMPLETED ) playMatch.id[0] = 0;
}
#else
void SV_PlayInit( qboolean selected ) {}
void SV_PlayFrame( void ) {}
void SV_PlayStop( int reason ) {}
void SV_PlayKill( int attacker ) {}
#endif
