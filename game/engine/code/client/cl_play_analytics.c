#include "client.h"
#include "cl_auth.h"
#include "../qcommon/play_analytics.h"
#include <time.h>

static char installId[37];

/* A game-home ID counts repeat guest play without collecting hardware details. */
static void CL_PlayInstall( void ) {
    char path[MAX_OSPATH], extra;
    FILE *file;
    if ( installId[0] ) return;
    Com_sprintf( path, sizeof( path ), "%s/play-analytics.id", Cvar_VariableString( "fs_homestatepath" ) );
    file = Sys_FOpen( path, "rb" );
    if ( file ) {
        int length = (int)fread( installId, 1, 36, file );
        int tail = fread( &extra, 1, 1, file );
        int i;
        qboolean valid = qtrue;
        fclose( file );
        installId[36] = 0;
        for ( i = 0; i < length; i++ ) {
            if ( i == 8 || i == 13 || i == 18 || i == 23 ) {
                if ( installId[i] != '-' ) valid = qfalse;
            } else if ( !strchr( "0123456789abcdef", installId[i] ) || !installId[i] ) valid = qfalse;
        }
        if ( length == 36 && !tail && valid ) return;
        installId[0] = 0;
    }
    if ( !Play_NewID( installId ) ) return;
    file = Sys_FOpen( path, "wb" );
    if ( file ) {
        qboolean written = fwrite( installId, 1, 36, file ) == 36;
        if ( fclose( file ) || !written ) installId[0] = 0;
    } else installId[0] = 0;
}

static struct {
    playMatch_t match;
    char report[37], player[37], username[25];
    long joined, firstPlayed, nextReport, lastSeen;
    unsigned int sequence;
    int activeMsec, snapshotTime, score, deaths, team, rank, kills;
    qboolean playing, hasResult, hasKills;
} participation;

static void CL_PlayRecord( long now, const char *reason ) {
    char match[1024], result[160] = "null", kills[32] = "", body[PLAY_ANALYTICS_BODY];
    if ( !participation.match.started || !participation.report[0] ) return;
    if ( participation.hasResult ) {
        if ( participation.hasKills ) Com_sprintf( kills, sizeof( kills ), ",\"kills\":%d", participation.kills );
        Com_sprintf( result, sizeof( result ), "{\"score\":%d,\"deaths\":%d,\"team\":%d,\"rank\":%d%s}",
            participation.score, participation.deaths, participation.team, participation.rank, kills );
    }
    Play_MatchJSON( &participation.match, match, sizeof( match ) );
    Com_sprintf( body, sizeof( body ),
        "{\"match\":%s,\"report\":\"%s\",\"seq\":%u,\"seen\":%ld,"
        "\"player\":{\"install\":\"%s\",\"account\":\"%s\",\"username\":\"%s\",\"joined\":%ld,\"first_played\":%ld,"
        "\"left\":%ld,\"reason\":\"%s\",\"active_ms\":%d,\"result\":%s}}",
        match, participation.report, ++participation.sequence, now, installId, participation.player,
        participation.username, participation.joined, participation.firstPlayed, *reason ? now : 0,
        reason, participation.activeMsec, result );
    Play_Queue( participation.report, participation.sequence, body );
    participation.nextReport = now + 120;
    participation.lastSeen = now;
}

void CL_PlayKills( const char *match, int kills ) {
    if ( participation.report[0] && !strcmp( match, participation.match.id ) && kills >= 0 ) {
        participation.kills = kills;
        participation.hasKills = qtrue;
    }
}

void CL_PlayDisconnect( const char *reason ) {
    if ( participation.report[0] && Play_QueueEnabled() ) {
        long now = MAX( participation.lastSeen, (long)time( NULL ) );
        CL_PlayRecord( now, reason );
    }
    Com_Memset( &participation, 0, sizeof( participation ) );
}

void CL_PlayMatchChanged( const char *text ) {
    playMatch_t match;
    // A restart can replace the terminal configstring before the next rendered
    // frame. Completion stays on the snapshot path to retain the final score.
    if ( participation.report[0] && Play_MatchParse( text, &match ) &&
         match.ended && match.finish != PLAY_COMPLETED && !strcmp( match.id, participation.match.id ) ) {
        participation.match = match;
        CL_PlayDisconnect( "match_changed" );
    }
}

void CL_PlayFrame( void ) {
    playMatch_t match;
    const playerState_t *ps = &cl.snap.ps;
    qboolean playing, firstPlay = qfalse;
    long now;
    int delta;
    if ( !Play_QueueEnabled() ) {
        Com_Memset( &participation, 0, sizeof( participation ) );
        return;
    }
    if ( clc.state != CA_ACTIVE || !cl.snap.valid || clc.demoplaying ||
         Cvar_VariableIntegerValue( "menuwatch" ) || Cvar_VariableIntegerValue( "g_menuBackground" ) ) return;
    if ( !Play_MatchParse( cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAY_ANALYTICS], &match ) ) {
        CL_PlayDisconnect( "match_changed" );
        return;
    }
    if ( strcmp( match.id, participation.match.id ) ) {
        CL_PlayDisconnect( "match_changed" );
        if ( match.ended ) return;
        if ( !Play_NewID( participation.report ) ) return;
        CL_PlayInstall();
        CL_AuthPlayIdentity( participation.player, participation.username );
        participation.joined = (long)time( NULL );
        participation.snapshotTime = cl.snap.serverTime;
    }
    participation.match = match;
    if ( !participation.report[0] || !match.started ) return;
    now = MAX( participation.lastSeen, MAX( participation.joined, (long)time( NULL ) ) );
    playing = ps->clientNum == clc.clientNum && !(ps->pm_flags & (PMF_FOLLOW | PMF_SCOREBOARD)) &&
        ps->persistant[PERS_TEAM] != TEAM_SPECTATOR;
    delta = cl.snap.serverTime - participation.snapshotTime;
    if ( playing && participation.playing && delta > 0 && delta <= 1000 &&
         participation.activeMsec <= INT_MAX - delta ) participation.activeMsec += delta;
    participation.snapshotTime = cl.snap.serverTime;
    participation.playing = playing;
    if ( playing ) {
        if ( !participation.firstPlayed && !match.ended ) {
            participation.firstPlayed = now;
            firstPlay = qtrue;
        }
        if ( participation.firstPlayed ) {
            participation.hasResult = qtrue;
            participation.score = ps->persistant[PERS_SCORE];
            participation.deaths = ps->persistant[PERS_KILLED];
            participation.team = ps->persistant[PERS_TEAM];
            participation.rank = ps->persistant[PERS_RANK];
        }
    }
    if ( match.ended ) {
        CL_PlayRecord( now, match.finish == PLAY_COMPLETED ? "finished" : "match_changed" );
        // Retain the match ID so intermission snapshots cannot start a new visit.
        participation.report[0] = 0;
    } else if ( firstPlay || now >= participation.nextReport ) {
        CL_PlayRecord( now, "" );
    }
}
