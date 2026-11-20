#ifndef PLAY_ANALYTICS_H
#define PLAY_ANALYTICS_H

#define CS_PLAY_ANALYTICS 30
#define PLAY_ANALYTICS_BODY 2048

typedef struct {
    char id[37], map[MAX_QPATH], build[64];
    long started, ended;
    int finish, source, humans, bots, gametype, mode, campaign, selected;
    int fraglimit, timelimit, botSkill, rules, teams, matchSeconds;
} playMatch_t;

enum { PLAY_OPEN, PLAY_COMPLETED, PLAY_MAP_CHANGE, PLAY_RESTART, PLAY_SHUTDOWN };
enum { PLAY_LOCAL, PLAY_COMMUNITY };
enum { PLAY_DEATHMATCH, PLAY_TEAM_DEATHMATCH, PLAY_KILLSHIFT, PLAY_LMS,
       PLAY_INFECTED, PLAY_BUGHUNT, PLAY_LEPRECHAUN, PLAY_OTHER };

qboolean Play_NewID( char id[37] );
qboolean Play_MatchParse( const char *text, playMatch_t *match );
void Play_MatchJSON( const playMatch_t *match, char *json, size_t size );
void Play_QueueProject( const char *project );
qboolean Play_QueueEnabled( void );
void Play_Queue( const char *report, unsigned int sequence, const char *body );
qboolean Play_QueueRead( char *name, size_t size, char body[PLAY_ANALYTICS_BODY] );
void Play_QueueRemove( const char *name );

#endif
