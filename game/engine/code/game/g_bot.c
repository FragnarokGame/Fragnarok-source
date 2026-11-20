/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
// g_bot.c

#include "g_local.h"

#ifdef STANDALONE
#include "g_bot_names.h"

static int botNameCursor;

static qboolean G_BotNameInUse( int clientNum, const char *name ) {
	int i;
	char cleanName[MAX_NETNAME];
	char otherName[MAX_NETNAME];

	Q_strncpyz( cleanName, name, sizeof( cleanName ) );
	Q_CleanStr( cleanName );
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( i == clientNum || level.clients[i].pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		Q_strncpyz( otherName, level.clients[i].pers.netname, sizeof( otherName ) );
		Q_CleanStr( otherName );
		if ( !Q_stricmp( cleanName, otherName ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

void G_UniqueBotName( int clientNum, char *name, int nameSize ) {
	if ( name[0] && !G_BotNameInUse( clientNum, name ) ) {
		return;
	}

	// The pool covers every client slot; excluding this client leaves a free name.
	do {
		Q_strncpyz( name, g_botNames[botNameCursor], nameSize );
		botNameCursor = ( botNameCursor + 1 ) % ARRAY_LEN( g_botNames );
	} while ( G_BotNameInUse( clientNum, name ) );
}

void G_ResolveBotNameCollisions( int clientNum ) {
	int i;
	char humanName[MAX_NETNAME];
	char botName[MAX_NETNAME];

	Q_strncpyz( humanName, level.clients[clientNum].pers.netname, sizeof( humanName ) );
	Q_CleanStr( humanName );
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ||
			!( g_entities[i].r.svFlags & SVF_BOT ) ) {
			continue;
		}
		Q_strncpyz( botName, level.clients[i].pers.netname, sizeof( botName ) );
		Q_CleanStr( botName );
		if ( !Q_stricmp( humanName, botName ) ) {
			ClientUserinfoChanged( i );
		}
	}
}
#endif


static int		g_numBots;
static char		*g_botInfos[MAX_BOTS];


int				g_numArenas;
static char		*g_arenaInfos[MAX_ARENAS];


#define BOT_BEGIN_DELAY_BASE		2000
#define BOT_BEGIN_DELAY_INCREMENT	1500

#define BOT_SPAWN_QUEUE_DEPTH	MAX_CLIENTS

typedef struct {
	int		clientNum;
	int		spawnTime;
} botSpawnQueue_t;

#ifdef STANDALONE
typedef struct {
	char	profile[MAX_NAME_LENGTH];
	char	name[MAX_NETNAME];
	team_t	team;
} displacedPopulationBot_t;
#endif

static botSpawnQueue_t	botSpawnQueue[BOT_SPAWN_QUEUE_DEPTH];

#ifdef STANDALONE
static displacedPopulationBot_t displacedPopulationBots[MAX_CLIENTS];
static int displacedPopulationBotCount;

qboolean G_IsCompanionClient( const gentity_t *ent ) {
	return ent && ent->client && ent->client->pers.isCompanion;
}
#else
qboolean G_IsCompanionClient( const gentity_t *ent ) {
	return qfalse;
}
#endif

vmCvar_t bot_minplayers;

#ifdef STANDALONE
static vmCvar_t bot_targetPopulation;
static vmCvar_t bot_activePopulation;
static int populationCheckTime;
static qboolean populationReadyLogged;
static qboolean G_AddBot( const char *name, float skill, const char *team, int delay, char *altname, int companionOwner,
	const vec3_t spawnOrigin, const vec3_t spawnAngles );
static void G_CheckCampaignRoster( void );
#endif

extern gentity_t	*podium1;
extern gentity_t	*podium2;
extern gentity_t	*podium3;

float trap_Cvar_VariableValue( const char *var_name ) {
	char buf[128];

	trap_Cvar_VariableStringBuffer(var_name, buf, sizeof(buf));
	return atof(buf);
}



/*
===============
G_ParseInfos
===============
*/
int G_ParseInfos( char *buf, int max, char *infos[] ) {
	char	*token;
	int		count;
	char	key[MAX_TOKEN_CHARS];
	char	info[MAX_INFO_STRING];

	count = 0;

	while ( 1 ) {
		token = COM_Parse( &buf );
		if ( !token[0] ) {
			break;
		}
		if ( strcmp( token, "{" ) ) {
			Com_Printf( "Missing { in info file\n" );
			break;
		}

		if ( count == max ) {
			Com_Printf( "Max infos exceeded\n" );
			break;
		}

		info[0] = '\0';
		while ( 1 ) {
			token = COM_ParseExt( &buf, qtrue );
			if ( !token[0] ) {
				Com_Printf( "Unexpected end of info file\n" );
				break;
			}
			if ( !strcmp( token, "}" ) ) {
				break;
			}
			Q_strncpyz( key, token, sizeof( key ) );

			token = COM_ParseExt( &buf, qfalse );
			if ( !token[0] ) {
				strcpy( token, "<NULL>" );
			}
			Info_SetValueForKey( info, key, token );
		}
		//NOTE: extra space for arena number
		infos[count] = G_Alloc(strlen(info) + strlen("\\num\\") + strlen(va("%d", MAX_ARENAS)) + 1);
		if (infos[count]) {
			strcpy(infos[count], info);
			count++;
		}
	}
	return count;
}

/*
===============
G_LoadArenasFromFile
===============
*/
static void G_LoadArenasFromFile( char *filename ) {
	int				len;
	fileHandle_t	f;
	char			buf[MAX_ARENAS_TEXT];

	len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( !f ) {
		trap_Print( va( S_COLOR_RED "file not found: %s\n", filename ) );
		return;
	}
	if ( len >= MAX_ARENAS_TEXT ) {
		trap_FS_FCloseFile( f );
		trap_Print( va( S_COLOR_RED "file too large: %s is %i, max allowed is %i\n", filename, len, MAX_ARENAS_TEXT ) );
		return;
	}

	trap_FS_Read( buf, len, f );
	buf[len] = 0;
	trap_FS_FCloseFile( f );

	g_numArenas += G_ParseInfos( buf, MAX_ARENAS - g_numArenas, &g_arenaInfos[g_numArenas] );
}

/*
===============
G_LoadArenas
===============
*/
static void G_LoadArenas( void ) {
	int			numdirs;
	vmCvar_t	arenasFile;
	char		filename[128];
	char		dirlist[1024];
	char*		dirptr;
	int			i, n;
	int			dirlen;

	g_numArenas = 0;

	trap_Cvar_Register( &arenasFile, "g_arenasFile", "", CVAR_INIT|CVAR_ROM );
	if( *arenasFile.string ) {
		G_LoadArenasFromFile(arenasFile.string);
	}
	else {
		G_LoadArenasFromFile("scripts/arenas.txt");
	}

	// get all arenas from .arena files
	numdirs = trap_FS_GetFileList("scripts", ".arena", dirlist, 1024 );
	dirptr  = dirlist;
	for (i = 0; i < numdirs; i++, dirptr += dirlen+1) {
		dirlen = strlen(dirptr);
		strcpy(filename, "scripts/");
		strcat(filename, dirptr);
		G_LoadArenasFromFile(filename);
	}
	trap_Print( va( "%i arenas parsed\n", g_numArenas ) );
	
	for( n = 0; n < g_numArenas; n++ ) {
		Info_SetValueForKey( g_arenaInfos[n], "num", va( "%i", n ) );
	}
}


/*
===============
G_GetArenaInfoByNumber
===============
*/
const char *G_GetArenaInfoByMap( const char *map ) {
	int			n;

	for( n = 0; n < g_numArenas; n++ ) {
		if( Q_stricmp( Info_ValueForKey( g_arenaInfos[n], "map" ), map ) == 0 ) {
			return g_arenaInfos[n];
		}
	}

	return NULL;
}


/*
=================
PlayerIntroSound
=================
*/
static void PlayerIntroSound( const char *modelAndSkin ) {
	char	model[MAX_QPATH];
	char	*skin;

	Q_strncpyz( model, modelAndSkin, sizeof(model) );
	skin = strrchr( model, '/' );
	if ( skin ) {
		*skin++ = '\0';
	}
	else {
		skin = model;
	}

	if( Q_stricmp( skin, "default" ) == 0 ) {
		skin = model;
	}

	trap_SendConsoleCommand( EXEC_APPEND, va( "play sound/player/announce/%s.wav\n", skin ) );
}

/*
===============
G_CountBotPlayersByName

Check connected and connecting (delay join) bots.

Returns number of bots with name on specified team or whole server if team is -1.
===============
*/
int G_CountBotPlayersByName( const char *name, int team ) {
	int			i, num;
	gclient_t	*cl;

	num = 0;
	for ( i=0 ; i< g_maxclients.integer ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
#ifdef STANDALONE
		if ( name && Q_stricmp( name, cl->pers.botProfile ) ) {
#else
		if ( name && Q_stricmp( name, cl->pers.netname ) ) {
#endif
			continue;
		}
		num++;
	}
	return num;
}

/*
===============
G_SelectRandomBotInfo

Get random least used bot info on team or whole server if team is -1.
===============
*/
int G_SelectRandomBotInfo( int team ) {
	int		selection[MAX_BOTS];
	int		n, num;
	int		count, bestCount;
	char	*value;

	// don't add duplicate bots to the server if there are less bots than bot types
	if ( team != -1 && G_CountBotPlayersByName( NULL, -1 ) < g_numBots ) {
		team = -1;
	}

	num = 0;
	bestCount = MAX_CLIENTS;
	for ( n = 0; n < g_numBots ; n++ ) {
		if ( atoi( Info_ValueForKey( g_botInfos[n], "companion" ) ) ) {
			continue;
		}
#ifdef STANDALONE
		// Display callsigns are independent of the profiles used for balancing.
		value = Info_ValueForKey( g_botInfos[n], "name" );
#else
		value = Info_ValueForKey( g_botInfos[n], "funname" );
		if ( !value[0] ) {
			value = Info_ValueForKey( g_botInfos[n], "name" );
		}
#endif
		//
		count = G_CountBotPlayersByName( value, team );

		if ( count < bestCount ) {
			bestCount = count;
			num = 0;
		}

		if ( count == bestCount ) {
			selection[num++] = n;

			if ( num == MAX_BOTS ) {
				break;
			}
		}
	}

	if ( num > 0 ) {
		num = random() * ( num - 1 );
		return selection[num];
	}

	return -1;
}

#ifdef STANDALONE
static int G_RandomBotSkill( void ) {
	return 1 + rand() % 3;
}
#endif

/*
===============
G_AddRandomBot
===============
*/
void G_AddRandomBot( int team ) {
	const char	*teamstr;
	float	skill;

#ifdef STANDALONE
	skill = G_RandomBotSkill();
#else
	skill = trap_Cvar_VariableValue( "g_spSkill" );
#endif
	teamstr = BG_TeamCommandName( (team_t)team );
	trap_SendConsoleCommand( EXEC_INSERT, va("addbot random %f %s %i\n", skill, teamstr, 0) );
}

#ifdef STANDALONE
static int G_TargetPopulation( void ) {
	int target = bot_targetPopulation.integer;

	if ( target < 0 ) {
		target = g_dedicated.integer ? 0 : MAX_CLIENTS;
	}
	if ( target > g_maxclients.integer ) {
		target = g_maxclients.integer;
	}
	// small arenas set "maxplayers" in arenas.txt
	if ( level.arenaMaxPlayers > 0 && target > level.arenaMaxPlayers ) {
		target = level.arenaMaxPlayers;
	}
	return target;
}

void G_RequestPopulationCheck( void ) {
	populationCheckTime = 0;
	populationReadyLogged = qfalse;
}

void G_RememberPopulationBot( int clientNum ) {
	gentity_t *ent;
	gclient_t *cl;
	displacedPopulationBot_t *saved;

	if ( clientNum < 0 || clientNum >= level.maxclients ) {
		return;
	}
	if ( G_TargetPopulation() <= 0 ||
		( g_gametype.integer != GT_FFA && g_gametype.integer != GT_TEAM ) ) {
		return;
	}

	ent = &g_entities[clientNum];
	cl = ent->client;
	if ( !cl || cl->pers.connected == CON_DISCONNECTED ||
		!( ent->r.svFlags & SVF_BOT ) || cl->pers.isCompanion ) {
		return;
	}

	if ( displacedPopulationBotCount >= ARRAY_LEN( displacedPopulationBots ) ) {
		memmove( displacedPopulationBots, displacedPopulationBots + 1,
			sizeof( displacedPopulationBots[0] ) * ( ARRAY_LEN( displacedPopulationBots ) - 1 ) );
		displacedPopulationBotCount = ARRAY_LEN( displacedPopulationBots ) - 1;
	}

	saved = &displacedPopulationBots[displacedPopulationBotCount++];
	Q_strncpyz( saved->profile, cl->pers.botProfile[0] ? cl->pers.botProfile : "random",
		sizeof( saved->profile ) );
	Q_strncpyz( saved->name, cl->pers.netname, sizeof( saved->name ) );
	saved->team = cl->sess.sessionTeam;
}

static void G_PushDisplacedPopulationBot( const displacedPopulationBot_t *bot ) {
	if ( displacedPopulationBotCount >= ARRAY_LEN( displacedPopulationBots ) ) {
		memmove( displacedPopulationBots, displacedPopulationBots + 1,
			sizeof( displacedPopulationBots[0] ) * ( ARRAY_LEN( displacedPopulationBots ) - 1 ) );
		displacedPopulationBotCount = ARRAY_LEN( displacedPopulationBots ) - 1;
	}
	displacedPopulationBots[displacedPopulationBotCount++] = *bot;
}

static qboolean G_TakeDisplacedPopulationBot( displacedPopulationBot_t *bot ) {
	if ( displacedPopulationBotCount <= 0 ) {
		return qfalse;
	}

	*bot = displacedPopulationBots[--displacedPopulationBotCount];
	return qtrue;
}

static void G_AddPopulationBot( team_t requestedTeam ) {
	displacedPopulationBot_t bot;
	team_t team;

	if ( !G_TakeDisplacedPopulationBot( &bot ) ) {
		G_AddRandomBot( requestedTeam );
		return;
	}

	team = bot.team;
	if ( requestedTeam == TEAM_SPECTATOR ) {
		team = TEAM_SPECTATOR;
	} else if ( team == TEAM_SPECTATOR ) {
		team = requestedTeam;
	}

	if ( !G_AddBot( bot.profile, G_RandomBotSkill(), BG_TeamCommandName( team ), 0, bot.name, -1, NULL, NULL ) ) {
		G_PushDisplacedPopulationBot( &bot );
	}
}

static qboolean G_RemovePopulationBot( void ) {
	int pass, i, teamIndex, selected, candidates;
	team_t bigTeam = TEAM_FREE;

	if ( g_gametype.integer == GT_TEAM ) {
		int bigCount = -1;
		for ( teamIndex = 0; teamIndex < G_ActiveTeamCount(); teamIndex++ ) {
			team_t team = BG_TeamForIndex( teamIndex );
			int count = TeamCount( -1, team );
			if ( count > bigCount ) {
				bigCount = count;
				bigTeam = team;
			}
		}
	}

	// Spectator reserve bots are removed before active combatants.
	for ( pass = 0; pass < 2; pass++ ) {
		selected = -1;
		candidates = 0;
		for ( i = 0; i < level.maxclients; i++ ) {
			gclient_t *cl = &level.clients[i];
			if ( cl->pers.connected == CON_DISCONNECTED ||
				!( g_entities[i].r.svFlags & SVF_BOT ) || cl->pers.isCompanion ) {
				continue;
			}
			if ( pass == 0 && cl->sess.sessionTeam != TEAM_SPECTATOR ) {
				continue;
			}
			if ( pass == 1 && bigTeam != TEAM_FREE && cl->sess.sessionTeam != bigTeam ) {
				continue;
			}
			if ( rand() % ++candidates == 0 ) {
				selected = i;
			}
		}
		if ( selected >= 0 ) {
			trap_SendConsoleCommand( EXEC_INSERT, va( "clientkick %d\n", selected ) );
			return qtrue;
		}
	}
	return qfalse;
}

static void G_CheckStandalonePopulation( void ) {
	int i, total = 0, humans = 0, active = 0;
	int target;
	int addCount, activeLimit;
	if ( G_LivesModeActive() && level.lmsStarted ) return;

	if ( populationCheckTime > level.time ) {
		return;
	}
	populationCheckTime = level.time + 250;
	trap_Cvar_Update( &bot_targetPopulation );
	trap_Cvar_Update( &bot_activePopulation );
	target = G_TargetPopulation();
	if ( target <= 0 || ( g_gametype.integer != GT_FFA && g_gametype.integer != GT_TEAM ) ) {
		return;
	}

	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected == CON_DISCONNECTED || cl->pers.menuObserver ) {
			continue;
		}
		total++;
		if ( !( g_entities[i].r.svFlags & SVF_BOT ) ) {
			humans++;
		}
		if ( cl->sess.sessionTeam != TEAM_SPECTATOR ) {
			active++;
		}
	}

	// Wait for a human before creating the synthetic population on listen
	// servers. Dedicated servers use this cvar for always-on public matches.
	if ( humans == 0 && !g_dedicated.integer ) {
		return;
	}

	if ( total == target ) {
		if ( !populationReadyLogged ) {
			G_Printf( "Standalone population ready: %d connected clients, %d active participants.\n",
				total, active );
			populationReadyLogged = qtrue;
		}
		return;
	}
	populationReadyLogged = qfalse;
	if ( total > target ) {
		G_RemovePopulationBot();
		return;
	}
	if ( total >= target ) {
		return;
	}

	addCount = target - total;
	// Every joining player publishes a reliable appearance configstring. The
	// server now holds those back while a client's acknowledgement window is
	// full, so the roster can fill at 128 players a second instead of 16.
	if ( addCount > 32 ) {
		addCount = 32;
	}
	activeLimit = bot_activePopulation.integer;
	if ( activeLimit < humans ) {
		activeLimit = humans;
	}
	if ( activeLimit > target ) {
		activeLimit = target;
	}
	for ( i = 0; i < addCount; i++ ) {
		if ( active < activeLimit ) {
			// "free" is resolved by SetTeam as each bot actually connects, so a
			// burst of queued bots still balances across every active team.
			G_AddPopulationBot( TEAM_FREE );
			active++;
		} else {
			G_AddPopulationBot( TEAM_SPECTATOR );
		}
	}
}
#endif

/*
===============
G_RemoveRandomBot
===============
*/
int G_RemoveRandomBot( int team ) {
	int i;
	gclient_t	*cl;

	for ( i=0 ; i< g_maxclients.integer ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
		trap_SendConsoleCommand( EXEC_INSERT, va("clientkick %d\n", i) );
		return qtrue;
	}
	return qfalse;
}

/*
===============
G_CountHumanPlayers
===============
*/
int G_CountHumanPlayers( int team ) {
	int i, num;
	gclient_t	*cl;

	num = 0;
	for ( i=0 ; i< g_maxclients.integer ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( cl->pers.menuObserver ) continue;
		if ( g_entities[i].r.svFlags & SVF_BOT ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
		num++;
	}
	return num;
}

/*
===============
G_CountBotPlayers

Check connected and connecting (delay join) bots.
===============
*/
int G_CountBotPlayers( int team ) {
	int i, num;
	gclient_t	*cl;

	num = 0;
	for ( i=0 ; i< g_maxclients.integer ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
		num++;
	}
	return num;
}

/*
===============
G_CheckMinimumPlayers
===============
*/
void G_CheckMinimumPlayers( void ) {
	int minplayers;
	int humanplayers, botplayers;
	static int checkminimumplayers_time;

	if (level.intermissiontime) return;
	//only check once each 10 seconds
	if (checkminimumplayers_time > level.time - 10000) {
		return;
	}
	checkminimumplayers_time = level.time;
	trap_Cvar_Update(&bot_minplayers);
	minplayers = bot_minplayers.integer;
	if (minplayers <= 0 || G_ConversionModeActive() || ( G_LivesModeActive() && level.lmsStarted )) return;

	if (g_gametype.integer >= GT_TEAM) {
		int teamIndex;
		if (minplayers >= g_maxclients.integer / G_ActiveTeamCount()) {
			minplayers = (g_maxclients.integer / G_ActiveTeamCount()) - 1;
		}
		for ( teamIndex = 0; teamIndex < G_ActiveTeamCount(); teamIndex++ ) {
			team_t team = BG_TeamForIndex( teamIndex );
			humanplayers = G_CountHumanPlayers( team );
			botplayers = G_CountBotPlayers( team );
			if ( humanplayers + botplayers < minplayers ) {
				G_AddRandomBot( team );
			} else if ( humanplayers + botplayers > minplayers && botplayers ) {
				G_RemoveRandomBot( team );
			}
		}
	}
	else if (g_gametype.integer == GT_TOURNAMENT ) {
		if (minplayers >= g_maxclients.integer) {
			minplayers = g_maxclients.integer-1;
		}
		humanplayers = G_CountHumanPlayers( -1 );
		botplayers = G_CountBotPlayers( -1 );
		//
		if (humanplayers + botplayers < minplayers) {
			G_AddRandomBot( TEAM_FREE );
		} else if (humanplayers + botplayers > minplayers && botplayers) {
			// try to remove spectators first
			if (!G_RemoveRandomBot( TEAM_SPECTATOR )) {
				// just remove the bot that is playing
				G_RemoveRandomBot( -1 );
			}
		}
	}
	else if (g_gametype.integer == GT_FFA) {
		if (minplayers >= g_maxclients.integer) {
			minplayers = g_maxclients.integer-1;
		}
		humanplayers = G_CountHumanPlayers( TEAM_FREE );
		botplayers = G_CountBotPlayers( TEAM_FREE );
		//
		if (humanplayers + botplayers < minplayers) {
			G_AddRandomBot( TEAM_FREE );
		} else if (humanplayers + botplayers > minplayers && botplayers) {
			G_RemoveRandomBot( TEAM_FREE );
		}
	}
}

/*
===============
G_CheckBotSpawn
===============
*/
void G_CheckBotSpawn( void ) {
	int		n;
	char	userinfo[MAX_INFO_VALUE];

#ifdef STANDALONE
	if ( level.campaign ) {
		G_CheckCampaignRoster();
		return;
	}
	// The main menu runs on a live map. Keep that scene quiet and inexpensive.
	if ( trap_Cvar_VariableIntegerValue( "g_menuBackground" ) ) {
		return;
	}
#endif

	G_CheckMinimumPlayers();

#ifdef STANDALONE
	G_CheckStandalonePopulation();
#endif

	for( n = 0; n < BOT_SPAWN_QUEUE_DEPTH; n++ ) {
		if( !botSpawnQueue[n].spawnTime ) {
			continue;
		}
		if ( botSpawnQueue[n].spawnTime > level.time ) {
			continue;
		}
		ClientBegin( botSpawnQueue[n].clientNum );
		botSpawnQueue[n].spawnTime = 0;

		if( g_gametype.integer == GT_SINGLE_PLAYER ) {
			trap_GetUserinfo( botSpawnQueue[n].clientNum, userinfo, sizeof(userinfo) );
			PlayerIntroSound( Info_ValueForKey (userinfo, "model") );
		}
	}
}


/*
===============
AddBotToSpawnQueue
===============
*/
static void AddBotToSpawnQueue( int clientNum, int delay ) {
	int		n;

	for( n = 0; n < BOT_SPAWN_QUEUE_DEPTH; n++ ) {
		if( !botSpawnQueue[n].spawnTime ) {
			botSpawnQueue[n].spawnTime = level.time + delay;
			botSpawnQueue[n].clientNum = clientNum;
			return;
		}
	}

	G_Printf( S_COLOR_YELLOW "Unable to delay spawn\n" );
	ClientBegin( clientNum );
}


/*
===============
G_RemoveQueuedBotBegin

Called on client disconnect to make sure the delayed spawn
doesn't happen on a freed index
===============
*/
void G_RemoveQueuedBotBegin( int clientNum ) {
	int		n;

	for( n = 0; n < BOT_SPAWN_QUEUE_DEPTH; n++ ) {
		if( botSpawnQueue[n].clientNum == clientNum ) {
			botSpawnQueue[n].spawnTime = 0;
			return;
		}
	}
}


/*
===============
G_BotConnect
===============
*/
qboolean G_BotConnect( int clientNum, qboolean restart ) {
	bot_settings_t	settings;
	char			userinfo[MAX_INFO_STRING];

	trap_GetUserinfo( clientNum, userinfo, sizeof(userinfo) );

	Q_strncpyz( settings.characterfile, Info_ValueForKey( userinfo, "characterfile" ), sizeof(settings.characterfile) );
	settings.skill = atof( Info_ValueForKey( userinfo, "skill" ) );

	if (!BotAISetupClient( clientNum, &settings, restart )) {
		trap_DropClient( clientNum, "BotAISetupClient failed" );
		return qfalse;
	}

	return qtrue;
}


/*
===============
G_AddBot
===============
*/
static qboolean G_AddBot( const char *name, float skill, const char *team, int delay, char *altname, int companionOwner,
	const vec3_t spawnOrigin, const vec3_t spawnAngles ) {
	int				clientNum;
	int				teamNum;
	int				botinfoNum;
	char			*botinfo;
	char			*key;
	char			*s;
	char			*botname;
	char			*model;
	char			*headmodel;
	char			userinfo[MAX_INFO_STRING];
#ifdef STANDALONE
	char			uniqueName[MAX_NETNAME];
#endif

	// have the server allocate a client slot
	clientNum = trap_BotAllocateClient();
	if ( clientNum == -1 ) {
		G_Printf( S_COLOR_RED "Unable to add bot. All player slots are in use.\n" );
		G_Printf( S_COLOR_RED "Start server with more 'open' slots (or check setting of sv_maxclients cvar).\n" );
		return qfalse;
	}

	// set default team
	if( !team || !*team ) {
		if( g_gametype.integer >= GT_TEAM ) {
			team = BG_TeamCommandName( PickTeam( clientNum ) );
		}
		else {
			team = "free";
		}
	}

	// get the botinfo from bots.txt
	if ( Q_stricmp( name, "random" ) == 0 ) {
		if ( !Q_stricmp( team, "spectator" ) || !Q_stricmp( team, "s" ) ) {
			teamNum = TEAM_SPECTATOR;
		}
		else {
			int teamIndex;
			teamNum = TEAM_FREE;
			for ( teamIndex = 0; teamIndex < G_ActiveTeamCount(); teamIndex++ ) {
				team_t candidate = BG_TeamForIndex( teamIndex );
				if ( !Q_stricmp( team, BG_TeamCommandName( candidate ) ) ) {
					teamNum = candidate;
					break;
				}
			}
		}

		botinfoNum = G_SelectRandomBotInfo( teamNum );

		if ( botinfoNum < 0 ) {
			G_Printf( S_COLOR_RED "Error: Cannot add random bot, no bot info available.\n" );
			trap_BotFreeClient( clientNum );
			return qfalse;
		}

		botinfo = G_GetBotInfoByNumber( botinfoNum );
	}
	else {
		botinfo = G_GetBotInfoByName( name );
	}

	if ( !botinfo ) {
		G_Printf( S_COLOR_RED "Error: Bot '%s' not defined\n", name );
		trap_BotFreeClient( clientNum );
		return qfalse;
	}

	// create the bot's userinfo
	userinfo[0] = '\0';

#ifdef STANDALONE
	uniqueName[0] = '\0';
	if ( altname && altname[0] ) {
		Q_strncpyz( uniqueName, altname, sizeof( uniqueName ) );
	}
	G_UniqueBotName( clientNum, uniqueName, sizeof( uniqueName ) );
	botname = uniqueName;
	Info_SetValueForKey( userinfo, "botprofile", Info_ValueForKey( botinfo, "name" ) );
#else
	botname = Info_ValueForKey( botinfo, "funname" );
	if( !botname[0] ) {
		botname = Info_ValueForKey( botinfo, "name" );
	}
	// check for an alternative name
	if (altname && altname[0]) {
		botname = altname;
	}
#endif
	Info_SetValueForKey( userinfo, "name", botname );
	Info_SetValueForKey( userinfo, "rate", "25000" );
	Info_SetValueForKey( userinfo, "snaps", "20" );
	Info_SetValueForKey( userinfo, "skill", va("%.2f", skill) );
	Info_SetValueForKey( userinfo, "teampref", team );
#ifdef STANDALONE
	if ( companionOwner >= 0 ) {
		Info_SetValueForKey( userinfo, "companion", "1" );
		Info_SetValueForKey( userinfo, "companionOwner", va( "%d", companionOwner ) );
	}
#endif

	if ( skill >= 1 && skill < 2 ) {
		Info_SetValueForKey( userinfo, "handicap", "50" );
	}
	else if ( skill >= 2 && skill < 3 ) {
		Info_SetValueForKey( userinfo, "handicap", "70" );
	}
	else if ( skill >= 3 && skill < 4 ) {
		Info_SetValueForKey( userinfo, "handicap", "90" );
	}

	key = "model";
	model = Info_ValueForKey( botinfo, key );
	if ( !*model ) {
		model = "visor/default";
	}
	Info_SetValueForKey( userinfo, key, model );
	key = "team_model";
	Info_SetValueForKey( userinfo, key, model );

	key = "headmodel";
	headmodel = Info_ValueForKey( botinfo, key );
	if ( !*headmodel ) {
		headmodel = model;
	}
	Info_SetValueForKey( userinfo, key, headmodel );
	key = "team_headmodel";
	Info_SetValueForKey( userinfo, key, headmodel );

	key = "gender";
	s = Info_ValueForKey( botinfo, key );
	if ( !*s ) {
		s = "male";
	}
	Info_SetValueForKey( userinfo, "sex", s );

	key = "color1";
	s = Info_ValueForKey( botinfo, key );
	if ( !*s ) {
		s = "4";
	}
	Info_SetValueForKey( userinfo, key, s );

	key = "color2";
	s = Info_ValueForKey( botinfo, key );
	if ( !*s ) {
		s = "5";
	}
	Info_SetValueForKey( userinfo, key, s );

	s = Info_ValueForKey(botinfo, "aifile");
	if (!*s ) {
		trap_Print( S_COLOR_RED "Error: bot has no aifile specified\n" );
		trap_BotFreeClient( clientNum );
		return qfalse;
	}
	Info_SetValueForKey( userinfo, "characterfile", s );

	// don't send tinfo to bots, they don't parse it
	Info_SetValueForKey( userinfo, "teamoverlay", "0" );

	// register the userinfo
	trap_SetUserinfo( clientNum, userinfo );

	// have it connect to the game as a normal client
	if ( ClientConnect( clientNum, qtrue, qtrue ) ) {
		return qfalse;
	}

	if( delay == 0 ) {
#ifdef STANDALONE
		if ( spawnOrigin ) {
			gclient_t *client = &level.clients[clientNum];
			client->pers.practiceBotSpawn = qtrue;
			VectorCopy( spawnOrigin, client->pers.practiceBotOrigin );
			VectorCopy( spawnAngles, client->pers.practiceBotAngles );
		}
#endif
		ClientBegin( clientNum );
		return qtrue;
	}

	AddBotToSpawnQueue( clientNum, delay );
	return qtrue;
}

#ifdef STANDALONE
static void G_CheckCampaignRoster( void ) {
	const campaignMission_t *mission = level.campaign;
	gclient_t *player = NULL;
	const char *rival;
	team_t team;
	int i, teamIndex, skill;

	// On large arenas, botlib can still be preparing routes after the player joins.
	if ( level.campaignRosterReady || !trap_AAS_Initialized() ) return;
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.localClient && cl->pers.connected == CON_CONNECTED &&
			cl->sess.sessionTeam != TEAM_SPECTATOR ) {
			player = cl;
			break;
		}
	}
	if ( !player ) return;

	level.campaignPlayer = i;
	level.campaignStartingTeam = player->sess.sessionTeam;
	teamIndex = BG_TeamIndex( level.campaignStartingTeam );
	for ( i = 1; i < mission->players; i++ ) {
		team = mission->teams ? BG_TeamForIndex( ( teamIndex + i ) % mission->teams ) : TEAM_FREE;
		skill = mission->allySkill && team == level.campaignStartingTeam ? mission->allySkill : mission->skill;
		rival = i == 1 ? mission->rival : NULL;
		if ( !G_AddBot( rival ? rival : "random", skill,
			BG_TeamCommandName( team ), 0, (char *)rival, -1, NULL, NULL ) ) {
			level.campaignRosterReady = qtrue;
			G_CampaignFinish( -1, TEAM_SPECTATOR );
			LogExit( "Campaign roster could not be created." );
			return;
		}
	}
	level.campaignRosterReady = qtrue;
	G_LogPrintf( "Campaign roster ready: mission=%d players=%d\n", level.campaignNumber, mission->players );
}

void G_SpawnTestChamberBot( gentity_t *ent ) {
	vec3_t start, end, origin, angles, eye, target;
	vec3_t mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	trace_t floor, sight;
	int ring, direction, clients = 0, i;
	float yaw, distance;
	char action[16];

	if ( !level.testChamber || !ent->client->pers.localClient ||
		g_dedicated.integer || ent->r.svFlags & SVF_BOT ) return;
	trap_Argv( 1, action, sizeof( action ) );
	if ( !Q_stricmp( action, "state" ) ) {
		G_Printf( "Test Chamber player: weapons=%d weapon=%d shield=%d raised=%d health=%d\n",
			ent->client->ps.stats[STAT_WEAPONS], ent->client->ps.weapon,
			ent->client->ps.stats[STAT_SHIELD_HEALTH], !!(ent->client->ps.pm_flags & PMF_SHIELD_UP), ent->health );
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *bot = &g_entities[i];
			if ( !bot->inuse || !bot->client || !(bot->r.svFlags & SVF_BOT) ) continue;
			G_Printf( "Test Chamber opponent: client=%d health=%d distance=%.0f shots=%d hits=%d\n",
				i, bot->health, Distance( bot->client->ps.origin, ent->client->ps.origin ),
				bot->client->accuracy_shots, bot->client->accuracy_hits );
		}
		return;
	}
	if ( ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) return;
	if ( ent->client->pers.lastPracticeBotTime &&
		level.time - ent->client->pers.lastPracticeBotTime < 1000 ) return;
	ent->client->pers.lastPracticeBotTime = level.time;
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( level.clients[i].pers.connected != CON_DISCONNECTED ) clients++;
	}
	if ( clients >= MIN( level.maxclients, level.arenaMaxPlayers > 0 ? level.arenaMaxPlayers : 8 ) ) {
		trap_SendServerCommand( ent - g_entities, "print \"Test Chamber is full.\n\"" );
		return;
	}
	if ( !trap_Cvar_VariableIntegerValue( "bot_enable" ) ) {
		trap_SendServerCommand( ent - g_entities, "print \"Bots are disabled.\n\"" );
		return;
	}
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	for ( ring = 0; ring < 4; ring++ ) {
		distance = 192 + ring * 96;
		for ( direction = 0; direction < 8; direction++ ) {
			yaw = DEG2RAD( ent->client->ps.viewangles[YAW] + direction * 45 );
			VectorCopy( ent->client->ps.origin, start );
			start[0] += cos( yaw ) * distance;
			start[1] += sin( yaw ) * distance;
			VectorCopy( start, end );
			start[2] += 64;
			end[2] -= 192;
			trap_Trace( &floor, start, mins, maxs, end, ent - g_entities, MASK_PLAYERSOLID );
			if ( floor.startsolid || floor.allsolid || floor.fraction == 1 || floor.plane.normal[2] < 0.7f ||
				floor.entityNum != ENTITYNUM_WORLD ) continue;
			VectorCopy( floor.endpos, origin );
			origin[2] += 1;
			if ( trap_PointContents( origin, ent - g_entities ) & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER) ) continue;
			VectorCopy( origin, target );
			target[2] += DEFAULT_VIEWHEIGHT;
			trap_Trace( &sight, eye, NULL, NULL, target, ent - g_entities, MASK_SOLID );
			if ( sight.fraction != 1 ) continue;
			VectorClear( angles );
			angles[YAW] = vectoyaw( tv( eye[0] - origin[0], eye[1] - origin[1], 0 ) );
			if ( G_AddBot( "random", G_RandomBotSkill(),
				"free", 0, NULL, -1, origin, angles ) ) {
				G_Printf( "Test Chamber bot: spawned distance=%.0f origin=(%.0f %.0f %.0f)\n",
					Distance( origin, ent->client->ps.origin ), origin[0], origin[1], origin[2] );
				trap_SendServerCommand( ent - g_entities, "print \"Practice opponent spawned nearby.\n\"" );
				trap_SendServerCommand( -1, "loaddefered\n" );
			}
			return;
		}
	}
	trap_SendServerCommand( ent - g_entities, "print \"No clear spot nearby. Move into an open area and press B again.\n\"" );
}
#endif


/*
===============
Svcmd_AddBot_f
===============
*/
void Svcmd_AddBot_f( void ) {
	float			skill;
	int				delay;
	char			name[MAX_TOKEN_CHARS];
	char			altname[MAX_TOKEN_CHARS];
	char			string[MAX_TOKEN_CHARS];
	char			team[MAX_TOKEN_CHARS];
	int				companionOwner;

	// are bots enabled?
	if ( !trap_Cvar_VariableIntegerValue( "bot_enable" ) ) {
		return;
	}

	// name
	trap_Argv( 1, name, sizeof( name ) );
	if ( !name[0] ) {
		trap_Print( "Usage: Addbot <botname> [skill 1-5] [team] [msec delay] [altname]\n" );
		return;
	}

	// skill
	trap_Argv( 2, string, sizeof( string ) );
	if ( !string[0] ) {
		skill = 4;
	}
	else {
		skill = Com_Clamp( 1, 5, atof( string ) );
	}

	// team
	trap_Argv( 3, team, sizeof( team ) );

	// delay
	trap_Argv( 4, string, sizeof( string ) );
	if ( !string[0] ) {
		delay = 0;
	}
	else {
		delay = atoi( string );
	}

	// alternative name
	trap_Argv( 5, altname, sizeof( altname ) );
	// Legacy addbot calls may pass an owner slot, but companions are retired.
	companionOwner = -1;

	G_AddBot( name, skill, team, delay, altname, companionOwner, NULL, NULL );

	// if this was issued during gameplay and we are playing locally,
	// go ahead and load the bot's media immediately
	if ( level.time - level.startTime > 1000 &&
		trap_Cvar_VariableIntegerValue( "cl_running" )
#ifdef STANDALONE
		&& G_TargetPopulation() <= 64
#endif
		) {
		trap_SendServerCommand( -1, "loaddefered\n" );	// FIXME: spelled wrong, but not changing for demo
	}
}

/*
===============
Svcmd_BotList_f
===============
*/
void Svcmd_BotList_f( void ) {
	int i;
	char name[MAX_TOKEN_CHARS];
	char funname[MAX_TOKEN_CHARS];
	char model[MAX_TOKEN_CHARS];
	char aifile[MAX_TOKEN_CHARS];

	trap_Print("^1name             model            aifile              funname\n");
	for (i = 0; i < g_numBots; i++) {
		Q_strncpyz(name, Info_ValueForKey( g_botInfos[i], "name" ), sizeof( name ));
		if ( !*name ) {
			strcpy(name, "UnnamedPlayer");
		}
		Q_strncpyz(funname, Info_ValueForKey( g_botInfos[i], "funname" ), sizeof( funname ));
		if ( !*funname ) {
			strcpy(funname, "");
		}
		Q_strncpyz(model, Info_ValueForKey( g_botInfos[i], "model" ), sizeof( model ));
		if ( !*model ) {
			strcpy(model, "visor/default");
		}
		Q_strncpyz(aifile, Info_ValueForKey( g_botInfos[i], "aifile"), sizeof( aifile ));
		if (!*aifile ) {
			strcpy(aifile, "bots/default_c.c");
		}
		trap_Print(va("%-16s %-16s %-20s %-20s\n", name, model, aifile, funname));
	}
}


/*
===============
G_SpawnBots
===============
*/
static void G_SpawnBots( char *botList, int baseDelay ) {
	char		*bot;
	char		*p;
	float		skill;
	int			delay;
	char		bots[MAX_INFO_VALUE];

	podium1 = NULL;
	podium2 = NULL;
	podium3 = NULL;

#ifndef STANDALONE
	skill = trap_Cvar_VariableValue( "g_spSkill" );
	if( skill < 1 ) {
		trap_Cvar_Set( "g_spSkill", "1" );
		skill = 1;
	}
	else if ( skill > 5 ) {
		trap_Cvar_Set( "g_spSkill", "5" );
		skill = 5;
	}
#endif

	Q_strncpyz( bots, botList, sizeof(bots) );
	p = &bots[0];
	delay = baseDelay;
	while( *p ) {
		//skip spaces
		while( *p && *p == ' ' ) {
			p++;
		}
		if( !*p ) {
			break;
		}

		// mark start of bot name
		bot = p;

		// skip until space of null
		while( *p && *p != ' ' ) {
			p++;
		}
		if( *p ) {
			*p++ = 0;
		}

		// we must add the bot this way, calling G_AddBot directly at this stage
		// does "Bad Things"
#ifdef STANDALONE
		skill = G_RandomBotSkill();
#endif
		trap_SendConsoleCommand( EXEC_INSERT, va("addbot %s %f free %i\n", bot, skill, delay) );

		delay += BOT_BEGIN_DELAY_INCREMENT;
	}
}


/*
===============
G_LoadBotsFromFile
===============
*/
static void G_LoadBotsFromFile( char *filename ) {
	int				len;
	fileHandle_t	f;
	char			buf[MAX_BOTS_TEXT];

	len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( !f ) {
		trap_Print( va( S_COLOR_RED "file not found: %s\n", filename ) );
		return;
	}
	if ( len >= MAX_BOTS_TEXT ) {
		trap_Print( va( S_COLOR_RED "file too large: %s is %i, max allowed is %i\n", filename, len, MAX_BOTS_TEXT ) );
		trap_FS_FCloseFile( f );
		return;
	}

	trap_FS_Read( buf, len, f );
	buf[len] = 0;
	trap_FS_FCloseFile( f );

	g_numBots += G_ParseInfos( buf, MAX_BOTS - g_numBots, &g_botInfos[g_numBots] );
}

/*
===============
G_LoadBots
===============
*/
static void G_LoadBots( void ) {
	vmCvar_t	botsFile;
	int			numdirs;
	char		filename[128];
	char		dirlist[1024];
	char*		dirptr;
	int			i;
	int			dirlen;

	if ( !trap_Cvar_VariableIntegerValue( "bot_enable" ) ) {
		return;
	}

	g_numBots = 0;

	trap_Cvar_Register( &botsFile, "g_botsFile", "", CVAR_INIT|CVAR_ROM );
	if( *botsFile.string ) {
		G_LoadBotsFromFile(botsFile.string);
	}
	else {
		G_LoadBotsFromFile("scripts/bots.txt");
	}

	// get all bots from .bot files
	numdirs = trap_FS_GetFileList("scripts", ".bot", dirlist, 1024 );
	dirptr  = dirlist;
	for (i = 0; i < numdirs; i++, dirptr += dirlen+1) {
		dirlen = strlen(dirptr);
		strcpy(filename, "scripts/");
		strcat(filename, dirptr);
		G_LoadBotsFromFile(filename);
	}
	trap_Print( va( "%i bots parsed\n", g_numBots ) );
}



/*
===============
G_GetBotInfoByNumber
===============
*/
char *G_GetBotInfoByNumber( int num ) {
	if( num < 0 || num >= g_numBots ) {
		trap_Print( va( S_COLOR_RED "Invalid bot number: %i\n", num ) );
		return NULL;
	}
	return g_botInfos[num];
}


/*
===============
G_GetBotInfoByName
===============
*/
char *G_GetBotInfoByName( const char *name ) {
	int		n;
	char	*value;

	for ( n = 0; n < g_numBots ; n++ ) {
		value = Info_ValueForKey( g_botInfos[n], "name" );
		if ( !Q_stricmp( value, name ) ) {
			return g_botInfos[n];
		}
	}

	return NULL;
}

/*
===============
G_InitBots
===============
*/
void G_InitBots( qboolean restart ) {
	int			fragLimit;
	int			timeLimit;
	const char	*arenainfo;
	char		*strValue;
	int			basedelay;
	char		map[MAX_QPATH];
	char		serverinfo[MAX_INFO_STRING];

#ifdef STANDALONE
	botNameCursor = rand() % ARRAY_LEN( g_botNames );
	populationCheckTime = 0;
	populationReadyLogged = qfalse;
	displacedPopulationBotCount = 0;
#endif

	G_LoadBots();
	G_LoadArenas();
#ifdef STANDALONE
	{
		char mapname[MAX_QPATH];
		const char *info;

		trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
		info = G_GetArenaInfoByMap( mapname );
		level.arenaMaxPlayers = info && !trap_Cvar_VariableIntegerValue( "g_quickPlay" ) ?
			atoi( Info_ValueForKey( info, "maxplayers" ) ) : 0;
		level.testChamber = !level.campaign && !Q_stricmp( mapname, "test_chamber" );
	}
#endif

	trap_Cvar_Register( &bot_minplayers, "bot_minplayers", "0", CVAR_SERVERINFO );
#ifdef STANDALONE
	trap_Cvar_Register( &bot_targetPopulation, "g_targetPopulation", "32", CVAR_ARCHIVE );
	trap_Cvar_Register( &bot_activePopulation, "g_activeBotLimit", "32", CVAR_ARCHIVE );
#endif

	if( g_gametype.integer == GT_SINGLE_PLAYER ) {
		trap_GetServerinfo( serverinfo, sizeof(serverinfo) );
		Q_strncpyz( map, Info_ValueForKey( serverinfo, "mapname" ), sizeof(map) );
		arenainfo = G_GetArenaInfoByMap( map );
		if ( !arenainfo ) {
			return;
		}

		strValue = Info_ValueForKey( arenainfo, "fraglimit" );
		fragLimit = atoi( strValue );
		if ( fragLimit ) {
			trap_Cvar_Set( "fraglimit", strValue );
		}
		else {
			trap_Cvar_Set( "fraglimit", "0" );
		}

		strValue = Info_ValueForKey( arenainfo, "timelimit" );
		timeLimit = atoi( strValue );
		if ( timeLimit ) {
			trap_Cvar_Set( "timelimit", strValue );
		}
		else {
			trap_Cvar_Set( "timelimit", "0" );
		}

		if ( !fragLimit && !timeLimit ) {
			trap_Cvar_Set( "fraglimit", "10" );
			trap_Cvar_Set( "timelimit", "0" );
		}

		basedelay = BOT_BEGIN_DELAY_BASE;
		strValue = Info_ValueForKey( arenainfo, "special" );
		if( Q_stricmp( strValue, "training" ) == 0 ) {
			basedelay += 10000;
		}

		if( !restart ) {
			G_SpawnBots( Info_ValueForKey( arenainfo, "bots" ), basedelay );
		}
	}
#ifdef STANDALONE
	else if ( !level.campaign && !restart && ( g_gametype.integer == GT_FFA || g_gametype.integer == GT_TEAM ) ) {
		// Standalone combat uses the original Q3 bot clients and botlib AI.
		if ( !trap_Cvar_VariableIntegerValue( "g_companionOnly" ) ) {
			G_SpawnBots( "Grism Sarge Grunt Major", 750 );
		}
	}
#endif
}
