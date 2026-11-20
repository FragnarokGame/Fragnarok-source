#include "q_shared.h"
#include "qcommon.h"
#include "play_analytics.h"

static char playProject[21];

qboolean Play_NewID( char id[37] ) {
    byte r[16];
    if ( !Sys_RandomBytes( r, sizeof( r ) ) ) return qfalse;
    r[6] = (r[6] & 15) | 64;
    r[8] = (r[8] & 63) | 128;
    Com_sprintf( id, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15] );
    return qtrue;
}

static qboolean Play_Token( const char *text, const char *allowed ) {
    return *text && strspn( text, allowed ) == strlen( text );
}

qboolean Play_MatchParse( const char *text, playMatch_t *m ) {
    const char *token = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-";
    char extra;
    if ( sscanf( text, "%36s %63s %63s %ld %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %c",
        m->id, m->map, m->build, &m->started, &m->ended, &m->finish, &m->source,
        &m->humans, &m->bots, &m->gametype, &m->mode, &m->campaign, &m->selected,
        &m->fraglimit, &m->timelimit, &m->botSkill, &m->rules, &m->teams, &m->matchSeconds, &extra ) != 19 ) return qfalse;
    return strlen( m->id ) == 36 && Play_Token( m->id, "0123456789abcdef-" ) &&
        m->id[8] == '-' && m->id[13] == '-' && m->id[18] == '-' && m->id[23] == '-' &&
        Play_Token( m->map, token ) && Play_Token( m->build, token ) &&
        m->started >= 0 && m->ended >= 0 && m->finish >= PLAY_OPEN && m->finish <= PLAY_SHUTDOWN &&
        m->source >= PLAY_LOCAL && m->source <= PLAY_COMMUNITY &&
        m->mode >= PLAY_DEATHMATCH && m->mode <= PLAY_OTHER &&
        m->humans >= 0 && m->bots >= 0 && m->humans <= MAX_CLIENTS && m->bots <= MAX_CLIENTS;
}

void Play_MatchJSON( const playMatch_t *m, char *json, size_t size ) {
    Com_sprintf( json, size,
        "{\"id\":\"%s\",\"map\":\"%s\",\"build\":\"%s\",\"started\":%ld,\"ended\":%ld,\"finish\":%d,"
        "\"source\":%d,\"humans\":%d,\"bots\":%d,\"gametype\":%d,\"mode\":%d,\"campaign\":%d,\"selected\":%d,"
        "\"fraglimit\":%d,\"timelimit\":%d,\"bot_skill\":%d,\"rules\":%d,\"teams\":%d,\"match_seconds\":%d}",
        m->id, m->map, m->build, m->started, m->ended, m->finish, m->source,
        m->humans, m->bots, m->gametype, m->mode, m->campaign, m->selected,
        m->fraglimit, m->timelimit, m->botSkill, m->rules, m->teams, m->matchSeconds );
}

void Play_QueueProject( const char *project ) {
    Q_strncpyz( playProject, project, sizeof( playProject ) );
}

qboolean Play_QueueEnabled( void ) {
    return playProject[0] && Cvar_VariableIntegerValue( "cl_playAnalytics" );
}

static void Play_QueuePath( char *path, size_t size, const char *name ) {
    Com_sprintf( path, size, "%s/play-analytics/%s/%s",
        Cvar_VariableString( "fs_homestatepath" ), playProject, name );
}

void Play_Queue( const char *report, unsigned int sequence, const char *body ) {
    char root[MAX_OSPATH], dir[MAX_OSPATH], path[MAX_OSPATH], pending[MAX_OSPATH];
    FILE *file;
    size_t size = strlen( body );
    qboolean written;
    if ( !Play_QueueEnabled() || !size || size >= PLAY_ANALYTICS_BODY ) return;
    Play_QueuePath( path, sizeof( path ), va( "%s-%08x.event", report, sequence ) );
    if ( Com_sprintf( pending, sizeof( pending ), "%s.tmp", path ) >= sizeof( pending ) ) return;
    Com_sprintf( root, sizeof( root ), "%s/play-analytics", Cvar_VariableString( "fs_homestatepath" ) );
    Play_QueuePath( dir, sizeof( dir ), "" );
    // Analytics must never abort a match if the game home becomes unwritable.
    dir[strlen( dir ) - 1] = 0;
    if ( !Sys_Mkdir( root ) || !Sys_Mkdir( dir ) || !(file = Sys_FOpen( pending, "wb" )) ) return;
    written = fwrite( body, 1, size, file ) == size;
    if ( fclose( file ) || !written || rename( pending, path ) ) remove( pending );
}

qboolean Play_QueueRead( char *name, size_t size, char body[PLAY_ANALYTICS_BODY] ) {
    char dir[MAX_OSPATH], path[MAX_OSPATH];
    char **files;
    FILE *file;
    int count, i;
    size_t bytes;
    qboolean found = qfalse;
    Play_QueuePath( dir, sizeof( dir ), "" );
    files = Sys_ListFiles( dir, ".event", NULL, &count, qfalse );
    for ( i = 0; i < count; i++ ) {
        if ( strlen( files[i] ) != 51 || !Play_Token( files[i], "0123456789abcdef-.vnt" ) ) continue;
        Play_QueuePath( path, sizeof( path ), files[i] );
        if ( !(file = Sys_FOpen( path, "rb" )) ) continue;
        bytes = fread( body, 1, PLAY_ANALYTICS_BODY, file );
        fclose( file );
        if ( !bytes || bytes >= PLAY_ANALYTICS_BODY ) continue;
        body[bytes] = 0;
        Q_strncpyz( name, files[i], size );
        found = qtrue;
        break;
    }
    Sys_FreeFileList( files );
    return found;
}

void Play_QueueRemove( const char *name ) {
    char path[MAX_OSPATH];
    Play_QueuePath( path, sizeof( path ), name );
    remove( path );
}
