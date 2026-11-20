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
#include "g_local.h"

#ifdef MISSIONPACK
#include "../../ui/menudef.h"			// for the voice chats
#endif

/*
==================
DeathmatchScoreboardMessage

==================
*/
static int ScoreboardEntry( char *entry, int entrySize, int clientNum ) {
	gclient_t *cl;
	int ping;
	int accuracy;
	int perfect;
	int scoreFlags = 0;

	cl = &level.clients[clientNum];
	if ( cl->pers.connected == CON_CONNECTING ) {
		ping = -1;
	} else {
		ping = cl->ps.ping < 999 ? cl->ps.ping : 999;
	}
	accuracy = cl->accuracy_shots ? cl->accuracy_hits * 100 / cl->accuracy_shots : 0;
	perfect = ( cl->ps.persistant[PERS_RANK] == 0 &&
		cl->ps.persistant[PERS_KILLED] == 0 ) ? 1 : 0;

	Com_sprintf( entry, entrySize,
		" %i %i %i %i %i %i %i %i %i %i %i %i %i %i", clientNum,
		G_LastManStandingActive() ? ( cl->pers.lmsParticipant ? cl->pers.lmsLives : -1 ) :
			cl->ps.persistant[PERS_SCORE], ping, ( level.time - cl->pers.enterTime ) / 60000,
		scoreFlags, g_entities[clientNum].s.powerups, accuracy,
		cl->ps.persistant[PERS_IMPRESSIVE_COUNT],
		cl->ps.persistant[PERS_EXCELLENT_COUNT],
		cl->ps.persistant[PERS_GAUNTLET_FRAG_COUNT],
		cl->ps.persistant[PERS_DEFEND_COUNT],
		cl->ps.persistant[PERS_ASSIST_COUNT],
		perfect, cl->ps.persistant[PERS_CAPTURES] );
	return strlen( entry );
}

void DeathmatchScoreboardMessage( gentity_t *ent ) {
	char		entry[1024];
	char		localEntry[1024];
	char		string[960];
	int			stringlength;
	int			i, j, reserve;
	int			numSorted, sentCount;
	int			clientNum, localClientNum, localLength;
	qboolean	localPending;

	// don't send scores to bots, they don't parse it
	if ( ent->r.svFlags & SVF_BOT ) {
		return;
	}

	// send the latest information on all clients
	string[0] = 0;
	stringlength = 0;
	sentCount = 0;

	numSorted = level.numConnectedClients;
	localClientNum = ent - g_entities;
	localLength = ScoreboardEntry( localEntry, sizeof( localEntry ), localClientNum );
	localPending = qtrue;
	
	for (i=0 ; i < numSorted ; i++) {
		clientNum = level.sortedClients[i];
		j = ScoreboardEntry( entry, sizeof( entry ), clientNum );
		reserve = localPending && clientNum != localClientNum ? localLength : 0;
		if ( stringlength + j + reserve >= sizeof( string ) ) {
			break;
		}
		strcpy (string + stringlength, entry);
		stringlength += j;
		sentCount++;
		if ( clientNum == localClientNum ) {
			localPending = qfalse;
		}
	}

	// The scoreboard only has room to render a page, but the requesting player
	// must always be present even when they rank below that page.
	if ( localPending && stringlength + localLength < sizeof( string ) ) {
		strcpy( string + stringlength, localEntry );
		stringlength += localLength;
		sentCount++;
	}

	trap_SendServerCommand( ent-g_entities, va("scores %i %i %i %i%s", sentCount,
		numSorted,
		level.teamScores[TEAM_RED], level.teamScores[TEAM_BLUE],
		string ) );
}


/*
==================
Cmd_Score_f

Request current scoreboard information
==================
*/
void Cmd_Score_f( gentity_t *ent ) {
	DeathmatchScoreboardMessage( ent );
}



/*
==================
CheatsOk
==================
*/
qboolean	CheatsOk( gentity_t *ent ) {
	if ( !g_cheats.integer ) {
		trap_SendServerCommand( ent-g_entities, "print \"Cheats are not enabled on this server.\n\"");
		return qfalse;
	}
	if ( ent->health <= 0 ) {
		trap_SendServerCommand( ent-g_entities, "print \"You must be alive to use this command.\n\"");
		return qfalse;
	}
	return qtrue;
}


/*
==================
ConcatArgs
==================
*/
char	*ConcatArgs( int start ) {
	int		i, c, tlen;
	static char	line[MAX_STRING_CHARS];
	int		len;
	char	arg[MAX_STRING_CHARS];

	len = 0;
	c = trap_Argc();
	for ( i = start ; i < c ; i++ ) {
		trap_Argv( i, arg, sizeof( arg ) );
		tlen = strlen( arg );
		if ( len + tlen >= MAX_STRING_CHARS - 1 ) {
			break;
		}
		memcpy( line + len, arg, tlen );
		len += tlen;
		if ( i != c - 1 ) {
			line[len] = ' ';
			len++;
		}
	}

	line[len] = 0;

	return line;
}


/*
==================
StringIsInteger
==================
*/
qboolean StringIsInteger( const char * s ) {
	int			i;
	int			len;
	qboolean	foundDigit;

	len = strlen( s );
	foundDigit = qfalse;

	for ( i=0 ; i < len ; i++ ) {
		if ( !isdigit( s[i] ) ) {
			return qfalse;
		}

		foundDigit = qtrue;
	}

	return foundDigit;
}


/*
==================
ClientNumberFromString

Returns a player number for either a number or name string
Returns -1 if invalid
==================
*/
int ClientNumberFromString( gentity_t *to, char *s, qboolean checkNums, qboolean checkNames ) {
	gclient_t	*cl;
	int			idnum;
	char		cleanName[MAX_STRING_CHARS];

	if ( checkNums ) {
		// numeric values could be slot numbers
		if ( StringIsInteger( s ) ) {
			idnum = atoi( s );
			if ( idnum >= 0 && idnum < level.maxclients ) {
				cl = &level.clients[idnum];
				if ( cl->pers.connected == CON_CONNECTED ) {
					return idnum;
				}
			}
		}
	}

	if ( checkNames ) {
		// check for a name match
		for ( idnum=0,cl=level.clients ; idnum < level.maxclients ; idnum++,cl++ ) {
			if ( cl->pers.connected != CON_CONNECTED ) {
				continue;
			}
			Q_strncpyz(cleanName, cl->pers.netname, sizeof(cleanName));
			Q_CleanStr(cleanName);
			if ( !Q_stricmp( cleanName, s ) ) {
				return idnum;
			}
		}
	}

	trap_SendServerCommand( to-g_entities, va("print \"User %s is not on the server\n\"", s));
	return -1;
}

/*
==================
Cmd_Give_f

Give items to a client
==================
*/
void Cmd_Give_f (gentity_t *ent)
{
	char		*name;
	gitem_t		*it;
	int			i;
	qboolean	give_all;
	gentity_t		*it_ent;
	trace_t		trace;

	if ( !CheatsOk( ent ) ) {
		return;
	}

	name = ConcatArgs( 1 );

	if (Q_stricmp(name, "all") == 0)
		give_all = qtrue;
	else
		give_all = qfalse;

	if (give_all || Q_stricmp( name, "health") == 0)
	{
		ent->health = ent->client->ps.stats[STAT_MAX_HEALTH];
		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "weapons") == 0)
	{
		ent->client->ps.stats[STAT_WEAPONS] = (1 << WP_NUM_WEAPONS) - 1 - 
			( 1 << WP_GRAPPLING_HOOK ) - ( 1 << WP_NONE );
		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "ammo") == 0)
	{
		for ( i = 0 ; i < MAX_WEAPONS ; i++ ) {
			ent->client->ps.ammo[i] = 999;
		}
#ifdef STANDALONE
		ent->client->ps.ammo[WP_SHOTGUN] = -1;
#endif
		if (!give_all)
			return;
	}

	if (give_all || Q_stricmp(name, "armor") == 0)
	{
		ent->client->ps.stats[STAT_ARMOR] = 200;

		if (!give_all)
			return;
	}

	if (Q_stricmp(name, "excellent") == 0) {
		ent->client->ps.persistant[PERS_EXCELLENT_COUNT]++;
		return;
	}
	if (Q_stricmp(name, "impressive") == 0) {
		ent->client->ps.persistant[PERS_IMPRESSIVE_COUNT]++;
		return;
	}
	if (Q_stricmp(name, "gauntletaward") == 0) {
		ent->client->ps.persistant[PERS_GAUNTLET_FRAG_COUNT]++;
		return;
	}
	if (Q_stricmp(name, "defend") == 0) {
		ent->client->ps.persistant[PERS_DEFEND_COUNT]++;
		return;
	}
	if (Q_stricmp(name, "assist") == 0) {
		ent->client->ps.persistant[PERS_ASSIST_COUNT]++;
		return;
	}

	// spawn a specific item right on the player
	if ( !give_all ) {
		it = BG_FindItem (name);
		if (!it) {
			return;
		}

		it_ent = G_Spawn();
		VectorCopy( ent->r.currentOrigin, it_ent->s.origin );
		it_ent->classname = it->classname;
		G_SpawnItem (it_ent, it);
		FinishSpawningItem(it_ent );
		memset( &trace, 0, sizeof( trace ) );
		Touch_Item (it_ent, ent, &trace);
		if (it_ent->inuse) {
			G_FreeEntity( it_ent );
		}
	}
}


/*
==================
Cmd_God_f

Sets client to godmode

argv(0) god
==================
*/
void Cmd_God_f (gentity_t *ent)
{
	char	*msg;

	if ( !CheatsOk( ent ) ) {
		return;
	}

	ent->flags ^= FL_GODMODE;
	if (!(ent->flags & FL_GODMODE) )
		msg = "godmode OFF\n";
	else
		msg = "godmode ON\n";

	trap_SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
}


/*
==================
Cmd_Notarget_f

Sets client to notarget

argv(0) notarget
==================
*/
void Cmd_Notarget_f( gentity_t *ent ) {
	char	*msg;

	if ( !CheatsOk( ent ) ) {
		return;
	}

	ent->flags ^= FL_NOTARGET;
	if (!(ent->flags & FL_NOTARGET) )
		msg = "notarget OFF\n";
	else
		msg = "notarget ON\n";

	trap_SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
}


/*
==================
Cmd_Noclip_f

argv(0) noclip
==================
*/
void Cmd_Noclip_f( gentity_t *ent ) {
	char	*msg;

	if ( !CheatsOk( ent ) ) {
		return;
	}

	if ( ent->client->noclip ) {
		msg = "noclip OFF\n";
	} else {
		msg = "noclip ON\n";
	}
	ent->client->noclip = !ent->client->noclip;

	trap_SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
}


/*
==================
Cmd_LevelShot_f

This is just to help generate the level pictures
for the menus.  It goes to the intermission immediately
and sends over a command to the client to resize the view,
hide the scoreboard, and take a special screenshot
==================
*/
void Cmd_LevelShot_f(gentity_t *ent)
{
	if(!ent->client->pers.localClient)
	{
		trap_SendServerCommand(ent-g_entities,
			"print \"The levelshot command must be executed by a local client\n\"");
		return;
	}

	if(!CheatsOk(ent))
		return;

	// doesn't work in single player
	if(g_gametype.integer == GT_SINGLE_PLAYER)
	{
		trap_SendServerCommand(ent-g_entities,
			"print \"Must not be in singleplayer mode for levelshot\n\"" );
		return;
	}

	BeginIntermission();
	trap_SendServerCommand(ent-g_entities, "clientLevelShot");
}


/*
==================
Cmd_TeamTask_f
==================
*/
void Cmd_TeamTask_f( gentity_t *ent ) {
	char userinfo[MAX_INFO_STRING];
	char		arg[MAX_TOKEN_CHARS];
	int task;
	int client = ent->client - level.clients;

	if ( trap_Argc() != 2 ) {
		return;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	task = atoi( arg );

	trap_GetUserinfo(client, userinfo, sizeof(userinfo));
	Info_SetValueForKey(userinfo, "teamtask", va("%d", task));
	trap_SetUserinfo(client, userinfo);
	ClientUserinfoChanged(client);
}


/*
=================
Cmd_Kill_f
=================
*/
void Cmd_Kill_f( gentity_t *ent ) {
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if (ent->health <= 0) {
		return;
	}
	ent->flags &= ~FL_GODMODE;
	ent->client->ps.stats[STAT_HEALTH] = ent->health = -999;
	player_die (ent, ent, ent, 100000, MOD_SUICIDE);
}

#ifdef STANDALONE
/*
=================
Cmd_Respawn_f

Move to a fresh spawn point without losing a frag, for when you're stuck or
just want a new start. Not while under fire, so it can't be used to escape a
fight with full health.
=================
*/
#define RESPAWN_COMBAT_MSEC		4000
#define RESPAWN_COOLDOWN_MSEC	8000

static void Cmd_Respawn_f( gentity_t *ent ) {
	gclient_t *client = ent->client;
	int wait, weapons, i;
	int ammo[MAX_WEAPONS];
	if ( G_LivesModeActive() ) {
		trap_SendServerCommand( ent - g_entities, "print \"Fresh respawn is disabled when lives are limited.\n\"" );
		return;
	}

	if ( client->sess.sessionTeam == TEAM_SPECTATOR || level.intermissiontime ) {
		return;
	}
	if ( ent->health <= 0 ) {
		// dead players already respawn on fire or jump
		return;
	}
	wait = client->pers.lastHurtTime + RESPAWN_COMBAT_MSEC - level.time;
	if ( client->pers.lastHurtTime && wait > 0 ) {
		trap_SendServerCommand( ent - g_entities,
			va( "cp \"Can't respawn under fire (%d s)\n\"", ( wait + 999 ) / 1000 ) );
		return;
	}
	wait = client->pers.lastRespawnCmdTime + RESPAWN_COOLDOWN_MSEC - level.time;
	if ( client->pers.lastRespawnCmdTime && wait > 0 ) {
		trap_SendServerCommand( ent - g_entities,
			va( "cp \"Respawn ready in %d s\n\"", ( wait + 999 ) / 1000 ) );
		return;
	}
	client->pers.lastRespawnCmdTime = level.time;
	client->pers.freshRespawn = qtrue;
	weapons = client->ps.stats[STAT_WEAPONS];
	memcpy( ammo, client->ps.ammo, sizeof( ammo ) );
	ClientSpawn( ent );
	// An unstick isn't a death: keep picked-up weapons and any extra ammo.
	client->ps.stats[STAT_WEAPONS] |= weapons;
	for ( i = 0; i < MAX_WEAPONS; i++ ) {
		if ( ammo[i] > client->ps.ammo[i] && client->ps.ammo[i] != -1 ) {
			client->ps.ammo[i] = ammo[i];
		}
	}
}
#endif

/*
=================
BroadcastTeamChange

Let everyone know about a team change
=================
*/
void BroadcastTeamChange( gclient_t *client, int oldTeam )
{
#ifdef STANDALONE
	// Filler bots and companions join by the hundred; a centre-print for each
	// one overflows every human's reliable command buffer.
	if ( g_entities[client - level.clients].r.svFlags & SVF_BOT ) {
		return;
	}
#endif
	if ( G_IsActiveTeam( client->sess.sessionTeam ) ) {
		trap_SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " joined the %s team.\n\"",
			client->pers.netname, BG_TeamCommandName( client->sess.sessionTeam ) ) );
	} else if ( client->sess.sessionTeam == TEAM_SPECTATOR && oldTeam != TEAM_SPECTATOR ) {
		trap_SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " joined the spectators.\n\"",
		client->pers.netname));
	} else if ( client->sess.sessionTeam == TEAM_FREE ) {
		trap_SendServerCommand( -1, va("cp \"%s" S_COLOR_WHITE " joined the battle.\n\"",
		client->pers.netname));
	}
}

static team_t TeamFromString( const char *name ) {
	int i;

	for ( i = 0; i < G_ActiveTeamCount(); i++ ) {
		team_t team = BG_TeamForIndex( i );
		const char *command = BG_TeamCommandName( team );
		if ( !Q_stricmp( name, command ) ||
			( name[0] && !name[1] && tolower( name[0] ) == command[0] ) ) {
			return team;
		}
	}
	return TEAM_FREE;
}

/*
=================
SetTeam
=================
*/
void SetTeam( gentity_t *ent, const char *s ) {
	int					team, oldTeam;
	gclient_t			*client;
	int					clientNum;
	spectatorState_t	specState;
	int					specClient;
	int					teamLeader;

	//
	// see what change is requested
	//
	client = ent->client;
	if ( client->pers.menuObserver || trap_Cvar_VariableIntegerValue( "g_matchLobby" ) ) return;

	clientNum = client - level.clients;
	specClient = 0;
	specState = SPECTATOR_NOT;
	if ( !Q_stricmp( s, "scoreboard" ) || !Q_stricmp( s, "score" )  ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_SCOREBOARD;
	} else if ( !Q_stricmp( s, "follow1" ) ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FOLLOW;
		specClient = -1;
	} else if ( !Q_stricmp( s, "follow2" ) ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FOLLOW;
		specClient = -2;
	} else if ( !Q_stricmp( s, "spectator" ) || !Q_stricmp( s, "s" ) ) {
		team = TEAM_SPECTATOR;
		specState = SPECTATOR_FREE;
	} else if ( g_gametype.integer >= GT_TEAM ) {
		// if running a team game, assign player to one of the teams
		specState = SPECTATOR_NOT;
		if ( !Q_stricmp( s, "red" ) || !Q_stricmp( s, "r" ) ) {
			team = TEAM_RED;
		} else if ( !Q_stricmp( s, "blue" ) || !Q_stricmp( s, "b" ) ) {
			team = TEAM_BLUE;
		} else if ( ( !Q_stricmp( s, "green" ) || !Q_stricmp( s, "g" ) ) && G_IsActiveTeam( TEAM_GREEN ) ) {
			team = TEAM_GREEN;
		} else if ( ( !Q_stricmp( s, "yellow" ) || !Q_stricmp( s, "y" ) ) && G_IsActiveTeam( TEAM_YELLOW ) ) {
			team = TEAM_YELLOW;
		} else {
			team = TeamFromString( s );
			if ( !G_IsActiveTeam( team ) ) {
				team = TEAM_FREE;
			}
		}

		if ( team == TEAM_FREE ) {
			// pick the team with the least number of players
			team = PickTeam( clientNum );
		}

		if ( G_KillshiftActive() ) {
			if ( G_IsActiveTeam( client->sess.sessionTeam ) ) {
				if ( team != client->sess.sessionTeam ) {
					trap_SendServerCommand( clientNum, "print \"Killshift allegiance changes on death.\n\"" );
				}
				return;
			}
			team = PickTeam( clientNum );
		}

		if ( G_InfectedActive() ) {
			if ( G_IsActiveTeam( client->sess.sessionTeam ) && team != client->sess.sessionTeam ) {
				trap_SendServerCommand( clientNum, "print \"Infected teams are decided by the infection.\n\"" );
				return;
			}
			team = PickTeam( clientNum );
		}

		if ( g_teamForceBalance.integer && !G_ConversionModeActive() && !client->pers.localClient && !( ent->r.svFlags & SVF_BOT ) ) {
			int i, minimum = MAX_CLIENTS + 1;

			for ( i = 0; i < G_ActiveTeamCount(); i++ ) {
				int count = TeamCount( clientNum, BG_TeamForIndex( i ) );
				if ( count < minimum ) {
					minimum = count;
				}
			}
			if ( TeamCount( clientNum, team ) - minimum > 1 ) {
				trap_SendServerCommand( clientNum, va( "cp \"%s team has too many players.\n\"",
					BG_TeamName( team ) ) );
				return;
			}

			// It's ok, the team we are switching to has less or same number of players
		}

	} else {
		// force them to spectators if there aren't any spots free
		team = TEAM_FREE;
	}

	// override decision if limiting the players
	if ( (g_gametype.integer == GT_TOURNAMENT)
		&& level.numNonSpectatorClients >= 2 ) {
		team = TEAM_SPECTATOR;
	} else if ( !level.campaign && g_maxGameClients.integer > 0 &&
		level.numNonSpectatorClients >= g_maxGameClients.integer ) {
		team = TEAM_SPECTATOR;
	}

	//
	// decide if we will allow the change
	//
	oldTeam = client->sess.sessionTeam;
#ifdef STANDALONE
	if ( client->pers.classInitialPending && team != TEAM_SPECTATOR ) return;
#endif
	if ( G_LivesModeActive() && !level.lmsStarted && team == TEAM_SPECTATOR ) {
		client->pers.lmsLives = 0;
		client->pers.lmsParticipant = qfalse;
	}
	if ( G_LivesModeActive() && level.lmsStarted ) {
		if ( team != TEAM_SPECTATOR && oldTeam == TEAM_SPECTATOR && !G_LivesModeCanJoin( client ) ) {
			client->sess.lmsWaiting = qtrue;
			trap_SendServerCommand( clientNum, "print \"Wait for the next round.\n\"" );
			return;
		}
		if ( G_BugHuntActive() && team != TEAM_SPECTATOR && oldTeam != TEAM_SPECTATOR && team != oldTeam ) {
			trap_SendServerCommand( clientNum, "print \"Squads are locked until the next round.\n\"" );
			return;
		}
		if ( team == TEAM_SPECTATOR && oldTeam != TEAM_SPECTATOR ) {
			client->pers.lmsLives = 0;
			client->sess.lmsWaiting = qtrue;
		}
	}
	if ( team == oldTeam && team != TEAM_SPECTATOR ) {
		return;
	}

	//
	// execute the team change
	//

	// if the player was dead leave the body, but only if they're actually in game
	if ( client->ps.stats[STAT_HEALTH] <= 0 && client->pers.connected == CON_CONNECTED ) {
		CopyToBodyQue(ent);
	}

	if ( team == TEAM_SPECTATOR ) G_LeprechaunLeave( ent );

	// he starts at 'base'
	client->pers.teamState.state = TEAM_BEGIN;
	if ( oldTeam != TEAM_SPECTATOR ) {
		// Kill him (makes sure he loses flags, etc)
		ent->flags &= ~FL_GODMODE;
		ent->client->ps.stats[STAT_HEALTH] = ent->health = 0;
		player_die (ent, ent, ent, 100000, MOD_SUICIDE);

	}

	// they go to the end of the line for tournements
	if(team == TEAM_SPECTATOR && oldTeam != team)
		AddTournamentQueue(client);

	client->sess.sessionTeam = team;
	client->sess.spectatorState = specState;
	client->sess.spectatorClient = specClient;

	client->sess.teamLeader = qfalse;
	if ( G_IsActiveTeam( team ) ) {
		teamLeader = TeamLeader( team );
		// if there is no team leader or the team leader is a bot and this client is not a bot
		if ( teamLeader == -1 || ( !(g_entities[clientNum].r.svFlags & SVF_BOT) && (g_entities[teamLeader].r.svFlags & SVF_BOT) ) ) {
			SetLeader( team, clientNum );
		}
	}
	// make sure there is a team leader on the team the player came from
	if ( G_IsActiveTeam( oldTeam ) ) {
		CheckTeamLeader( oldTeam );
	}

	// get and distribute relevant parameters
	ClientUserinfoChanged( clientNum );

	// client hasn't spawned yet, they sent an early team command, teampref userinfo, or g_teamAutoJoin is enabled
	if ( client->pers.connected != CON_CONNECTED ) {
		return;
	}

	BroadcastTeamChange( client, oldTeam );

	ClientBegin( clientNum );
}

/*
=================
StopFollowing

If the client being followed leaves the game, or you just want to drop
to free floating spectator mode
=================
*/
void StopFollowing( gentity_t *ent ) {
	ent->client->ps.persistant[ PERS_TEAM ] = TEAM_SPECTATOR;	
	ent->client->sess.sessionTeam = TEAM_SPECTATOR;	
	ent->client->sess.spectatorState = SPECTATOR_FREE;
	ent->client->ps.pm_flags &= ~PMF_FOLLOW;
	ent->r.svFlags &= ~SVF_BOT;
	ent->client->ps.clientNum = ent - g_entities;

	SetClientViewAngle( ent, ent->client->ps.viewangles );

	// don't use dead view angles
	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		ent->client->ps.stats[STAT_HEALTH] = 1;
	}
}

/*
=================
Cmd_Team_f
=================
*/
void Cmd_Team_f( gentity_t *ent ) {
	int			oldTeam;
	char		s[MAX_TOKEN_CHARS];

	if ( trap_Argc() != 2 ) {
		oldTeam = ent->client->sess.sessionTeam;
		switch ( oldTeam ) {
		case TEAM_FREE:
			trap_SendServerCommand( ent-g_entities, "print \"Free team\n\"" );
			break;
		case TEAM_SPECTATOR:
			trap_SendServerCommand( ent-g_entities, "print \"Spectator team\n\"" );
			break;
		default:
			trap_SendServerCommand( ent-g_entities, va( "print \"%s team\n\"",
				BG_TeamName( oldTeam ) ) );
			break;
		}
		return;
	}

	if ( ent->client->switchTeamTime > level.time ) {
		trap_SendServerCommand( ent-g_entities, "print \"May not switch teams more than once per 5 seconds.\n\"" );
		return;
	}

	if ( level.campaign ) return;

	// if they are playing a tournement game, count as a loss
	if ( (g_gametype.integer == GT_TOURNAMENT )
		&& ent->client->sess.sessionTeam == TEAM_FREE ) {
		ent->client->sess.losses++;
	}

	trap_Argv( 1, s, sizeof( s ) );

	SetTeam( ent, s );

	ent->client->switchTeamTime = level.time + 5000;
}


/*
=================
Cmd_Follow_f
=================
*/
void Cmd_Follow_f( gentity_t *ent ) {
	int		i;
	char	arg[MAX_TOKEN_CHARS];

	if ( trap_Argc() != 2 ) {
		if ( ent->client->sess.spectatorState == SPECTATOR_FOLLOW ) {
			StopFollowing( ent );
		}
		return;
	}

	trap_Argv( 1, arg, sizeof( arg ) );
	i = ClientNumberFromString( ent, arg, qtrue, qtrue );
	if ( i == -1 ) {
		return;
	}

	// can't follow self
	if ( &level.clients[ i ] == ent->client ) {
		return;
	}

	// can't follow another spectator
	if ( level.clients[ i ].sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}

	// if they are playing a tournement game, count as a loss
	if ( (g_gametype.integer == GT_TOURNAMENT )
		&& ent->client->sess.sessionTeam == TEAM_FREE ) {
		ent->client->sess.losses++;
	}

	// first set them to spectator
	if ( ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		SetTeam( ent, "spectator" );
	}

	ent->client->sess.spectatorState = SPECTATOR_FOLLOW;
	ent->client->sess.spectatorClient = i;
}

/*
=================
Cmd_FollowCycle_f
=================
*/
void Cmd_FollowCycle_f( gentity_t *ent, int dir ) {
	int		clientnum;
	int		original;

	// if they are playing a tournement game, count as a loss
	if ( (g_gametype.integer == GT_TOURNAMENT )
		&& ent->client->sess.sessionTeam == TEAM_FREE ) {
		ent->client->sess.losses++;
	}
	// first set them to spectator
	if ( ent->client->sess.spectatorState == SPECTATOR_NOT ) {
		SetTeam( ent, "spectator" );
	}

	if ( dir != 1 && dir != -1 ) {
		G_Error( "Cmd_FollowCycle_f: bad dir %i", dir );
	}

	// if dedicated follow client, just switch between the two auto clients
	if (ent->client->sess.spectatorClient < 0) {
		if (ent->client->sess.spectatorClient == -1) {
			ent->client->sess.spectatorClient = -2;
		} else if (ent->client->sess.spectatorClient == -2) {
			ent->client->sess.spectatorClient = -1;
		}
		return;
	}

	clientnum = ent->client->sess.spectatorClient;
	original = clientnum;
	do {
		clientnum += dir;
		if ( clientnum >= level.maxclients ) {
			clientnum = 0;
		}
		if ( clientnum < 0 ) {
			clientnum = level.maxclients - 1;
		}

		// can only follow connected clients
		if ( level.clients[ clientnum ].pers.connected != CON_CONNECTED ) {
			continue;
		}

		// can't follow another spectator
		if ( level.clients[ clientnum ].sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}

		// this is good, we can use it
		ent->client->sess.spectatorClient = clientnum;
		ent->client->sess.spectatorState = SPECTATOR_FOLLOW;
		return;
	} while ( clientnum != original );

	// leave it where it was
}


/*
==================
G_Say
==================
*/

static void G_SayTo( gentity_t *ent, gentity_t *other, int mode, int color, const char *name, const char *message ) {
	if (!other) {
		return;
	}
	if (!other->inuse) {
		return;
	}
	if (!other->client) {
		return;
	}
	if ( other->client->pers.connected != CON_CONNECTED ) {
		return;
	}
	if ( mode == SAY_TEAM  && !OnSameTeam(ent, other) ) {
		return;
	}
	// no chatting to players in tournements
	if ( (g_gametype.integer == GT_TOURNAMENT )
		&& other->client->sess.sessionTeam == TEAM_FREE
		&& ent->client->sess.sessionTeam != TEAM_FREE ) {
		return;
	}

	trap_SendServerCommand( other-g_entities, va("%s \"%s%c%c%s\"", 
		mode == SAY_TEAM ? "tchat" : "chat",
		name, Q_COLOR_ESCAPE, color, message));
}

#define EC		"\x19"

void G_Say( gentity_t *ent, gentity_t *target, int mode, const char *chatText ) {
	int			j;
	gentity_t	*other;
	int			color;
	char		name[64];
	// don't let text be too long for malicious reasons
	char		text[MAX_SAY_TEXT];
	char		location[64];

	if ( g_gametype.integer < GT_TEAM && mode == SAY_TEAM ) {
		mode = SAY_ALL;
	}

	switch ( mode ) {
	default:
	case SAY_ALL:
		G_LogPrintf( "say: %s: %s\n", ent->client->pers.netname, chatText );
		Com_sprintf (name, sizeof(name), "%s%c%c"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
		color = COLOR_GREEN;
		break;
	case SAY_TEAM:
		G_LogPrintf( "sayteam: %s: %s\n", ent->client->pers.netname, chatText );
		if (Team_GetLocationMsg(ent, location, sizeof(location)))
			Com_sprintf (name, sizeof(name), EC"(%s%c%c"EC") (%s)"EC": ", 
				ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE, location);
		else
			Com_sprintf (name, sizeof(name), EC"(%s%c%c"EC")"EC": ", 
				ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
		color = COLOR_CYAN;
		break;
	case SAY_TELL:
		if (target && target->inuse && target->client && g_gametype.integer >= GT_TEAM &&
			target->client->sess.sessionTeam == ent->client->sess.sessionTeam &&
			Team_GetLocationMsg(ent, location, sizeof(location)))
			Com_sprintf (name, sizeof(name), EC"[%s%c%c"EC"] (%s)"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE, location );
		else
			Com_sprintf (name, sizeof(name), EC"[%s%c%c"EC"]"EC": ", ent->client->pers.netname, Q_COLOR_ESCAPE, COLOR_WHITE );
		color = COLOR_MAGENTA;
		break;
	}

	Q_strncpyz( text, chatText, sizeof(text) );

	if ( target ) {
		G_SayTo( ent, target, mode, color, name, text );
		return;
	}

	// echo the text to the console
	if ( g_dedicated.integer ) {
		G_Printf( "%s%s\n", name, text);
	}

	// send it to all the appropriate clients
	for (j = 0; j < level.maxclients; j++) {
		other = &g_entities[j];
		G_SayTo( ent, other, mode, color, name, text );
	}
}

static void SanitizeChatText( char *text ) {
	int i;

	for ( i = 0; text[i]; i++ ) {
		if ( text[i] == '\n' || text[i] == '\r' ) {
			text[i] = ' ';
		}
	}
}


/*
==================
Cmd_Say_f
==================
*/
static void Cmd_Say_f( gentity_t *ent, int mode, qboolean arg0 ) {
	char		*p;

	if ( trap_Argc () < 2 && !arg0 ) {
		return;
	}

	if (arg0)
	{
		p = ConcatArgs( 0 );
	}
	else
	{
		p = ConcatArgs( 1 );
	}

	SanitizeChatText( p );

	G_Say( ent, NULL, mode, p );
}

/*
==================
Cmd_Tell_f
==================
*/
static void Cmd_Tell_f( gentity_t *ent ) {
	int			targetNum;
	gentity_t	*target;
	char		*p;
	char		arg[MAX_TOKEN_CHARS];

	if ( trap_Argc () < 3 ) {
		trap_SendServerCommand( ent-g_entities, "print \"Usage: tell <player id> <message>\n\"" );
		return;
	}

	trap_Argv( 1, arg, sizeof( arg ) );
	targetNum = ClientNumberFromString( ent, arg, qtrue, qtrue );
	if ( targetNum == -1 ) {
		return;
	}

	target = &g_entities[targetNum];
	if ( !target->inuse || !target->client ) {
		return;
	}

	p = ConcatArgs( 2 );

	SanitizeChatText( p );

	G_LogPrintf( "tell: %s to %s: %s\n", ent->client->pers.netname, target->client->pers.netname, p );
	G_Say( ent, target, SAY_TELL, p );
	// don't tell to the player self if it was already directed to this player
	// also don't send the chat back to a bot
	if ( ent != target && !(ent->r.svFlags & SVF_BOT)) {
		G_Say( ent, ent, SAY_TELL, p );
	}
}


#ifdef MISSIONPACK
static void G_VoiceTo( gentity_t *ent, gentity_t *other, int mode, const char *id, qboolean voiceonly ) {
	int color;
	char *cmd;

	if (!other) {
		return;
	}
	if (!other->inuse) {
		return;
	}
	if (!other->client) {
		return;
	}
	if ( mode == SAY_TEAM && !OnSameTeam(ent, other) ) {
		return;
	}
	// no chatting to players in tournements
	if ( g_gametype.integer == GT_TOURNAMENT ) {
		return;
	}

	if (mode == SAY_TEAM) {
		color = COLOR_CYAN;
		cmd = "vtchat";
	}
	else if (mode == SAY_TELL) {
		color = COLOR_MAGENTA;
		cmd = "vtell";
	}
	else {
		color = COLOR_GREEN;
		cmd = "vchat";
	}

	trap_SendServerCommand( other-g_entities, va("%s %d %d %d %s", cmd, voiceonly, ent->s.number, color, id));
}

void G_Voice( gentity_t *ent, gentity_t *target, int mode, const char *id, qboolean voiceonly ) {
	int			j;
	gentity_t	*other;

	if ( g_gametype.integer < GT_TEAM && mode == SAY_TEAM ) {
		mode = SAY_ALL;
	}

	if ( target ) {
		G_VoiceTo( ent, target, mode, id, voiceonly );
		return;
	}

	// echo the text to the console
	if ( g_dedicated.integer ) {
		G_Printf( "voice: %s %s\n", ent->client->pers.netname, id);
	}

	// send it to all the appropriate clients
	for (j = 0; j < level.maxclients; j++) {
		other = &g_entities[j];
		G_VoiceTo( ent, other, mode, id, voiceonly );
	}
}

/*
==================
Cmd_Voice_f
==================
*/
static void Cmd_Voice_f( gentity_t *ent, int mode, qboolean arg0, qboolean voiceonly ) {
	char		*p;

	if ( trap_Argc () < 2 && !arg0 ) {
		return;
	}

	if (arg0)
	{
		p = ConcatArgs( 0 );
	}
	else
	{
		p = ConcatArgs( 1 );
	}

	SanitizeChatText( p );

	G_Voice( ent, NULL, mode, p, voiceonly );
}

/*
==================
Cmd_VoiceTell_f
==================
*/
static void Cmd_VoiceTell_f( gentity_t *ent, qboolean voiceonly ) {
	int			targetNum;
	gentity_t	*target;
	char		*id;
	char		arg[MAX_TOKEN_CHARS];

	if ( trap_Argc () < 3 ) {
		trap_SendServerCommand( ent-g_entities, va( "print \"Usage: %s <player id> <voice id>\n\"", voiceonly ? "votell" : "vtell" ) );
		return;
	}

	trap_Argv( 1, arg, sizeof( arg ) );
	targetNum = ClientNumberFromString( ent, arg, qtrue, qtrue );
	if ( targetNum == -1 ) {
		return;
	}

	target = &g_entities[targetNum];
	if ( !target->inuse || !target->client ) {
		return;
	}

	id = ConcatArgs( 2 );

	SanitizeChatText( id );

	G_LogPrintf( "vtell: %s to %s: %s\n", ent->client->pers.netname, target->client->pers.netname, id );
	G_Voice( ent, target, SAY_TELL, id, voiceonly );
	// don't tell to the player self if it was already directed to this player
	// also don't send the chat back to a bot
	if ( ent != target && !(ent->r.svFlags & SVF_BOT)) {
		G_Voice( ent, ent, SAY_TELL, id, voiceonly );
	}
}


/*
==================
Cmd_VoiceTaunt_f
==================
*/
static void Cmd_VoiceTaunt_f( gentity_t *ent ) {
	gentity_t *who;
	int i;

	if (!ent->client) {
		return;
	}

	// insult someone who just killed you
	if (ent->enemy && ent->enemy->client && ent->enemy->client->lastkilled_client == ent->s.number) {
		// i am a dead corpse
		if (!(ent->enemy->r.svFlags & SVF_BOT)) {
			G_Voice( ent, ent->enemy, SAY_TELL, VOICECHAT_DEATHINSULT, qfalse );
		}
		if (!(ent->r.svFlags & SVF_BOT)) {
			G_Voice( ent, ent,        SAY_TELL, VOICECHAT_DEATHINSULT, qfalse );
		}
		ent->enemy = NULL;
		return;
	}
	// insult someone you just killed
	if (ent->client->lastkilled_client >= 0 && ent->client->lastkilled_client != ent->s.number) {
		who = g_entities + ent->client->lastkilled_client;
		if (who->client) {
			// who is the person I just killed
			if (who->client->lasthurt_mod == MOD_GAUNTLET) {
				if (!(who->r.svFlags & SVF_BOT)) {
					G_Voice( ent, who, SAY_TELL, VOICECHAT_KILLGAUNTLET, qfalse );	// and I killed them with a gauntlet
				}
				if (!(ent->r.svFlags & SVF_BOT)) {
					G_Voice( ent, ent, SAY_TELL, VOICECHAT_KILLGAUNTLET, qfalse );
				}
			} else {
				if (!(who->r.svFlags & SVF_BOT)) {
					G_Voice( ent, who, SAY_TELL, VOICECHAT_KILLINSULT, qfalse );	// and I killed them with something else
				}
				if (!(ent->r.svFlags & SVF_BOT)) {
					G_Voice( ent, ent, SAY_TELL, VOICECHAT_KILLINSULT, qfalse );
				}
			}
			ent->client->lastkilled_client = -1;
			return;
		}
	}

	if (g_gametype.integer >= GT_TEAM) {
		// praise a team mate who just got a reward
		for(i = 0; i < MAX_CLIENTS; i++) {
			who = g_entities + i;
			if (who->client && who != ent && who->client->sess.sessionTeam == ent->client->sess.sessionTeam) {
				if (who->client->rewardTime > level.time) {
					if (!(who->r.svFlags & SVF_BOT)) {
						G_Voice( ent, who, SAY_TELL, VOICECHAT_PRAISE, qfalse );
					}
					if (!(ent->r.svFlags & SVF_BOT)) {
						G_Voice( ent, ent, SAY_TELL, VOICECHAT_PRAISE, qfalse );
					}
					return;
				}
			}
		}
	}

	// just say something
	G_Voice( ent, NULL, SAY_ALL, VOICECHAT_TAUNT, qfalse );
}
#endif



static char	*gc_orders[] = {
	"hold your position",
	"hold this position",
	"come here",
	"cover me",
	"guard location",
	"search and destroy",
	"report"
};

static const int numgc_orders = ARRAY_LEN( gc_orders );

void Cmd_GameCommand_f( gentity_t *ent ) {
	int			targetNum;
	gentity_t	*target;
	int			order;
	char		arg[MAX_TOKEN_CHARS];

	if ( trap_Argc() != 3 ) {
		trap_SendServerCommand( ent-g_entities, va( "print \"Usage: gc <player id> <order 0-%d>\n\"", numgc_orders - 1 ) );
		return;
	}

	trap_Argv( 2, arg, sizeof( arg ) );
	order = atoi( arg );

	if ( order < 0 || order >= numgc_orders ) {
		trap_SendServerCommand( ent-g_entities, va("print \"Bad order: %i\n\"", order));
		return;
	}

	trap_Argv( 1, arg, sizeof( arg ) );
	targetNum = ClientNumberFromString( ent, arg, qtrue, qtrue );
	if ( targetNum == -1 ) {
		return;
	}

	target = &g_entities[targetNum];
	if ( !target->inuse || !target->client ) {
		return;
	}

	G_LogPrintf( "tell: %s to %s: %s\n", ent->client->pers.netname, target->client->pers.netname, gc_orders[order] );
	G_Say( ent, target, SAY_TELL, gc_orders[order] );
	// don't tell to the player self if it was already directed to this player
	// also don't send the chat back to a bot
	if ( ent != target && !(ent->r.svFlags & SVF_BOT)) {
		G_Say( ent, ent, SAY_TELL, gc_orders[order] );
	}
}

/*
==================
Cmd_Where_f
==================
*/
void Cmd_Where_f( gentity_t *ent ) {
	trap_SendServerCommand( ent-g_entities, va("print \"%s\n\"", vtos(ent->r.currentOrigin) ) );
}

static const char *gameNames[] = {
	"Free For All",
	"Tournament",
	"Single Player",
	"Team Deathmatch",
	"Capture the Flag",
	"One Flag CTF",
	"Overload",
	"Harvester"
};

/*
==================
Cmd_CallVote_f
==================
*/
void Cmd_CallVote_f( gentity_t *ent ) {
	char*	c;
	int		i;
	char	arg1[MAX_STRING_TOKENS];
	char	arg2[MAX_STRING_TOKENS];

	if ( !g_allowVote.integer ) {
		trap_SendServerCommand( ent-g_entities, "print \"Voting not allowed here.\n\"" );
		return;
	}

	if ( level.voteTime ) {
		trap_SendServerCommand( ent-g_entities, "print \"A vote is already in progress.\n\"" );
		return;
	}
	if ( ent->client->pers.voteCount >= MAX_VOTE_COUNT ) {
		trap_SendServerCommand( ent-g_entities, "print \"You have called the maximum number of votes.\n\"" );
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap_SendServerCommand( ent-g_entities, "print \"Not allowed to call a vote as spectator.\n\"" );
		return;
	}

	// make sure it is a valid command to vote on
	trap_Argv( 1, arg1, sizeof( arg1 ) );
	trap_Argv( 2, arg2, sizeof( arg2 ) );

	// check for command separators in arg2
	for( c = arg2; *c; ++c) {
		switch(*c) {
			case '\n':
			case '\r':
			case ';':
				trap_SendServerCommand( ent-g_entities, "print \"Invalid vote string.\n\"" );
				return;
			break;
		}
	}

	if ( !Q_stricmp( arg1, "map_restart" ) ) {
	} else if ( !Q_stricmp( arg1, "nextmap" ) ) {
	} else if ( !Q_stricmp( arg1, "map" ) ) {
	} else if ( !Q_stricmp( arg1, "g_gametype" ) ) {
	} else if ( !Q_stricmp( arg1, "kick" ) ) {
	} else if ( !Q_stricmp( arg1, "clientkick" ) ) {
	} else if ( !Q_stricmp( arg1, "g_doWarmup" ) ) {
	} else if ( !Q_stricmp( arg1, "timelimit" ) ) {
	} else if ( !Q_stricmp( arg1, "fraglimit" ) ) {
	} else {
		trap_SendServerCommand( ent-g_entities, "print \"Invalid vote string.\n\"" );
		trap_SendServerCommand( ent-g_entities, "print \"Vote commands are: map_restart, nextmap, map <mapname>, g_gametype <n>, kick <player>, clientkick <clientnum>, g_doWarmup, timelimit <time>, fraglimit <frags>.\n\"" );
		return;
	}

	// if there is still a vote to be executed
	if ( level.voteExecuteTime ) {
		// don't start a vote when map change or restart is in progress
		if ( !Q_stricmpn( level.voteString, "map", 3 )
			|| !Q_stricmpn( level.voteString, "nextmap", 7 ) ) {
			trap_SendServerCommand( ent-g_entities, "print \"Vote after map change.\n\"" );
			return;
		}

		level.voteExecuteTime = 0;
		trap_SendConsoleCommand( EXEC_APPEND, va("%s\n", level.voteString ) );
	}

	// special case for g_gametype, check for bad values
	if ( !Q_stricmp( arg1, "g_gametype" ) ) {
		i = atoi( arg2 );
		if( i == GT_SINGLE_PLAYER || i < GT_FFA || i >= GT_MAX_GAME_TYPE) {
			trap_SendServerCommand( ent-g_entities, "print \"Invalid gametype.\n\"" );
			return;
		}

		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %d", arg1, i );
		Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s %s", arg1, gameNames[i] );
	} else if ( !Q_stricmp( arg1, "map" ) ) {
		// special case for map changes, we want to reset the nextmap setting
		// this allows a player to change maps, but not upset the map rotation
		char	s[MAX_STRING_CHARS];

		trap_Cvar_VariableStringBuffer( "nextmap", s, sizeof(s) );
		if (*s) {
			Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s; set nextmap \"%s\"", arg1, arg2, s );
		} else {
			Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s", arg1, arg2 );
		}
		Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s", level.voteString );
	} else if ( !Q_stricmp( arg1, "nextmap" ) ) {
		char	s[MAX_STRING_CHARS];

		trap_Cvar_VariableStringBuffer( "nextmap", s, sizeof(s) );
		if (!*s) {
			trap_SendServerCommand( ent-g_entities, "print \"nextmap not set.\n\"" );
			return;
		}
		Com_sprintf( level.voteString, sizeof( level.voteString ), "vstr nextmap");
		Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s", level.voteString );
	} else if ( !Q_stricmp( arg1, "clientkick" ) || !Q_stricmp( arg1, "kick" ) ) {
		i = ClientNumberFromString( ent, arg2, !Q_stricmp( arg1, "clientkick" ), !Q_stricmp( arg1, "kick" ) );
		if ( i == -1 ) {
			return;
		}

		if ( level.clients[i].pers.localClient ) {
			trap_SendServerCommand( ent - g_entities, "print \"Cannot kick host player.\n\"" );
			return;
		}

		Com_sprintf( level.voteString, sizeof( level.voteString ), "clientkick %d", i );
		Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "kick %s", level.clients[i].pers.netname );
	} else {
		Com_sprintf( level.voteString, sizeof( level.voteString ), "%s \"%s\"", arg1, arg2 );
		Com_sprintf( level.voteDisplayString, sizeof( level.voteDisplayString ), "%s", level.voteString );
	}

	trap_SendServerCommand( -1, va("print \"%s called a vote.\n\"", ent->client->pers.netname ) );

	// start the voting, the caller automatically votes yes
	level.voteTime = level.time;
	level.voteYes = 1;
	level.voteNo = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		level.clients[i].ps.eFlags &= ~EF_VOTED;
	}
	ent->client->ps.eFlags |= EF_VOTED;

	trap_SetConfigstring( CS_VOTE_TIME, va("%i", level.voteTime ) );
	trap_SetConfigstring( CS_VOTE_STRING, level.voteDisplayString );	
	trap_SetConfigstring( CS_VOTE_YES, va("%i", level.voteYes ) );
	trap_SetConfigstring( CS_VOTE_NO, va("%i", level.voteNo ) );	
}

/*
==================
Cmd_Vote_f
==================
*/
void Cmd_Vote_f( gentity_t *ent ) {
	char		msg[64];

	if ( !level.voteTime ) {
		trap_SendServerCommand( ent-g_entities, "print \"No vote in progress.\n\"" );
		return;
	}
	if ( ent->client->ps.eFlags & EF_VOTED ) {
		trap_SendServerCommand( ent-g_entities, "print \"Vote already cast.\n\"" );
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap_SendServerCommand( ent-g_entities, "print \"Not allowed to vote as spectator.\n\"" );
		return;
	}

	trap_SendServerCommand( ent-g_entities, "print \"Vote cast.\n\"" );

	ent->client->ps.eFlags |= EF_VOTED;

	trap_Argv( 1, msg, sizeof( msg ) );

	if ( tolower( msg[0] ) == 'y' || msg[0] == '1' ) {
		level.voteYes++;
		trap_SetConfigstring( CS_VOTE_YES, va("%i", level.voteYes ) );
	} else {
		level.voteNo++;
		trap_SetConfigstring( CS_VOTE_NO, va("%i", level.voteNo ) );	
	}

	// a majority will be determined in CheckVote, which will also account
	// for players entering or leaving
}

/*
==================
Cmd_CallTeamVote_f
==================
*/
void Cmd_CallTeamVote_f( gentity_t *ent ) {
	char*	c;
	int		i, team, cs_offset;
	char	arg1[MAX_STRING_TOKENS];
	char	arg2[MAX_STRING_TOKENS];

	team = ent->client->sess.sessionTeam;
	if ( team == TEAM_RED )
		cs_offset = 0;
	else if ( team == TEAM_BLUE )
		cs_offset = 1;
	else
		return;

	if ( !g_allowVote.integer ) {
		trap_SendServerCommand( ent-g_entities, "print \"Voting not allowed here.\n\"" );
		return;
	}

	if ( level.teamVoteTime[cs_offset] ) {
		trap_SendServerCommand( ent-g_entities, "print \"A team vote is already in progress.\n\"" );
		return;
	}
	if ( ent->client->pers.teamVoteCount >= MAX_VOTE_COUNT ) {
		trap_SendServerCommand( ent-g_entities, "print \"You have called the maximum number of team votes.\n\"" );
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap_SendServerCommand( ent-g_entities, "print \"Not allowed to call a vote as spectator.\n\"" );
		return;
	}

	// make sure it is a valid command to vote on
	trap_Argv( 1, arg1, sizeof( arg1 ) );
	arg2[0] = '\0';
	for ( i = 2; i < trap_Argc(); i++ ) {
		if (i > 2)
			strcat(arg2, " ");
		trap_Argv( i, &arg2[strlen(arg2)], sizeof( arg2 ) - strlen(arg2) );
	}

	// check for command separators in arg2
	for( c = arg2; *c; ++c) {
		switch(*c) {
			case '\n':
			case '\r':
			case ';':
				trap_SendServerCommand( ent-g_entities, "print \"Invalid vote string.\n\"" );
				return;
			break;
		}
	}

	if ( !Q_stricmp( arg1, "leader" ) ) {
		char netname[MAX_NETNAME], leader[MAX_NETNAME];

		if ( !arg2[0] ) {
			i = ent->client->ps.clientNum;
		}
		else {
			// numeric values are just slot numbers
			for (i = 0; i < 3; i++) {
				if ( !arg2[i] || arg2[i] < '0' || arg2[i] > '9' )
					break;
			}
			if ( i >= 3 || !arg2[i]) {
				i = atoi( arg2 );
				if ( i < 0 || i >= level.maxclients ) {
					trap_SendServerCommand( ent-g_entities, va("print \"Bad client slot: %i\n\"", i) );
					return;
				}

				if ( !g_entities[i].inuse ) {
					trap_SendServerCommand( ent-g_entities, va("print \"Client %i is not active\n\"", i) );
					return;
				}
			}
			else {
				Q_strncpyz(leader, arg2, sizeof(leader));
				Q_CleanStr(leader);
				for ( i = 0 ; i < level.maxclients ; i++ ) {
					if ( level.clients[i].pers.connected == CON_DISCONNECTED )
						continue;
					if (level.clients[i].sess.sessionTeam != team)
						continue;
					Q_strncpyz(netname, level.clients[i].pers.netname, sizeof(netname));
					Q_CleanStr(netname);
					if ( !Q_stricmp(netname, leader) ) {
						break;
					}
				}
				if ( i >= level.maxclients ) {
					trap_SendServerCommand( ent-g_entities, va("print \"%s is not a valid player on your team.\n\"", arg2) );
					return;
				}
			}
		}
		Com_sprintf(arg2, sizeof(arg2), "%d", i);
	} else {
		trap_SendServerCommand( ent-g_entities, "print \"Invalid vote string.\n\"" );
		trap_SendServerCommand( ent-g_entities, "print \"Team vote commands are: leader <player>.\n\"" );
		return;
	}

	Com_sprintf( level.teamVoteString[cs_offset], sizeof( level.teamVoteString[cs_offset] ), "%s %s", arg1, arg2 );

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED )
			continue;
		if (level.clients[i].sess.sessionTeam == team)
			trap_SendServerCommand( i, va("print \"%s called a team vote.\n\"", ent->client->pers.netname ) );
	}

	// start the voting, the caller automatically votes yes
	level.teamVoteTime[cs_offset] = level.time;
	level.teamVoteYes[cs_offset] = 1;
	level.teamVoteNo[cs_offset] = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if (level.clients[i].sess.sessionTeam == team)
			level.clients[i].ps.eFlags &= ~EF_TEAMVOTED;
	}
	ent->client->ps.eFlags |= EF_TEAMVOTED;

	trap_SetConfigstring( CS_TEAMVOTE_TIME + cs_offset, va("%i", level.teamVoteTime[cs_offset] ) );
	trap_SetConfigstring( CS_TEAMVOTE_STRING + cs_offset, level.teamVoteString[cs_offset] );
	trap_SetConfigstring( CS_TEAMVOTE_YES + cs_offset, va("%i", level.teamVoteYes[cs_offset] ) );
	trap_SetConfigstring( CS_TEAMVOTE_NO + cs_offset, va("%i", level.teamVoteNo[cs_offset] ) );
}

/*
==================
Cmd_TeamVote_f
==================
*/
void Cmd_TeamVote_f( gentity_t *ent ) {
	int			team, cs_offset;
	char		msg[64];

	team = ent->client->sess.sessionTeam;
	if ( team == TEAM_RED )
		cs_offset = 0;
	else if ( team == TEAM_BLUE )
		cs_offset = 1;
	else
		return;

	if ( !level.teamVoteTime[cs_offset] ) {
		trap_SendServerCommand( ent-g_entities, "print \"No team vote in progress.\n\"" );
		return;
	}
	if ( ent->client->ps.eFlags & EF_TEAMVOTED ) {
		trap_SendServerCommand( ent-g_entities, "print \"Team vote already cast.\n\"" );
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		trap_SendServerCommand( ent-g_entities, "print \"Not allowed to vote as spectator.\n\"" );
		return;
	}

	trap_SendServerCommand( ent-g_entities, "print \"Team vote cast.\n\"" );

	ent->client->ps.eFlags |= EF_TEAMVOTED;

	trap_Argv( 1, msg, sizeof( msg ) );

	if ( tolower( msg[0] ) == 'y' || msg[0] == '1' ) {
		level.teamVoteYes[cs_offset]++;
		trap_SetConfigstring( CS_TEAMVOTE_YES + cs_offset, va("%i", level.teamVoteYes[cs_offset] ) );
	} else {
		level.teamVoteNo[cs_offset]++;
		trap_SetConfigstring( CS_TEAMVOTE_NO + cs_offset, va("%i", level.teamVoteNo[cs_offset] ) );	
	}

	// a majority will be determined in TeamCheckVote, which will also account
	// for players entering or leaving
}


/*
=================
Cmd_SetViewpos_f
=================
*/
void Cmd_SetViewpos_f( gentity_t *ent ) {
	vec3_t		origin, angles;
	char		buffer[MAX_TOKEN_CHARS];
	int			i;

	if ( !g_cheats.integer ) {
		trap_SendServerCommand( ent-g_entities, "print \"Cheats are not enabled on this server.\n\"");
		return;
	}
	if ( trap_Argc() != 5 && trap_Argc() != 6 ) {
		trap_SendServerCommand( ent-g_entities, "print \"usage: setviewpos x y z yaw [pitch]\n\"");
		return;
	}

	VectorClear( angles );
	for ( i = 0 ; i < 3 ; i++ ) {
		trap_Argv( i + 1, buffer, sizeof( buffer ) );
		origin[i] = atof( buffer );
	}

	trap_Argv( 4, buffer, sizeof( buffer ) );
	angles[YAW] = atof( buffer );
	// the pitch is optional, so tools/map_tour.py can look up and down
	if ( trap_Argc() == 6 ) {
		trap_Argv( 5, buffer, sizeof( buffer ) );
		angles[PITCH] = atof( buffer );
	}

	TeleportPlayer( ent, origin, angles );
}



/*
=================
Cmd_Stats_f
=================
*/
void Cmd_Stats_f( gentity_t *ent ) {
/*
	int max, n, i;

	max = trap_AAS_PointReachabilityAreaIndex( NULL );

	n = 0;
	for ( i = 0; i < max; i++ ) {
		if ( ent->client->areabits[i >> 3] & (1 << (i & 7)) )
			n++;
	}

	//trap_SendServerCommand( ent-g_entities, va("print \"visited %d of %d areas\n\"", n, max));
	trap_SendServerCommand( ent-g_entities, va("print \"%d%% level coverage\n\"", n * 100 / max));
*/
}

/*
=================
ClientCommand
=================
*/
static void Cmd_StartWeaponFire_f( gentity_t *ent ) {
	if ( ent->health <= 0 ) {
		if ( level.time > ent->client->respawnTime ) {
			ClientRespawn( ent );
		}
		return;
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	// Reliable start/stop commands keep firing independent of movement
	// usercmd packets. The buffer preserves a very short click.
	ent->client->ps.stats[STAT_RIFLE_TRIGGER_HELD] = 1;
	ent->client->ps.stats[STAT_RIFLE_TRIGGER_BUFFER] = 150;
}

static void Cmd_StopWeaponFire_f( gentity_t *ent ) {
	ent->client->ps.stats[STAT_RIFLE_TRIGGER_HELD] = 0;
}

static void Cmd_GiveRifleAmmo_f( gentity_t *ent ) {
	// This and the test hooks below edit game state directly, so like give and god they need cheats.
	if ( !CheatsOk( ent ) ) return;
	char arg[16];
	int count = 999;

	// An optional count lets tests start a weapon nearly empty.
	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		count = atoi( arg );
		if ( count < 0 ) {
			count = 0;
		}
	}
	ent->client->ps.ammo[WP_SHOTGUN] = -1;
	ent->client->ps.ammo[WP_MACHINEGUN] = count;
	ent->client->ps.ammo[WP_ROCKET_LAUNCHER] = count;
	ent->client->ps.ammo[WP_PLASMAGUN] = count;
	trap_SendServerCommand( ent - g_entities, "print \"Weapon ammunition replenished.\n\"" );
}

#include "g_arsenal_test.h"

static void Cmd_ShowArsenal_f( gentity_t *ent ) {
	int i, flak = 0, arc = 0, traps = 0, oden = 0;
	if ( !g_cheats.integer ) return;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || e->parent != ent ) continue;
		if ( !strcmp( e->classname, "flak" ) ) flak++;
		if ( !strcmp( e->classname, "plasma" ) ) arc++;
		if ( !strcmp( e->classname, "siege_trap" ) ) traps++;
		if ( !strcmp( e->classname, "bfg" ) ) oden++;
	}
	trap_SendServerCommand( ent - g_entities, va(
		"print \"arsenal: slot=%d weapon=%d ammo=%d state=%d flak=%d arc=%d traps=%d oden=%d alt=%d\n\"",
		BG_WeaponSlot(ent->client->ps.weapon), ent->client->ps.weapon,
		ent->client->ps.ammo[ent->client->ps.weapon], ent->client->ps.weaponstate,
		flak, arc, traps, oden,
		!!(ent->client->ps.eFlags & EF_ALT_FIRING) ) );
}

static void Cmd_ShowWeaponState_f( gentity_t *ent ) {
	if ( !g_cheats.integer ) return;
	trap_SendServerCommand( ent - g_entities, va(
		"print \"weapon state: weapon=%d ammo=%d pistol=%d rifle=%d rockets=%d machinegun=%d weaponTime=%d triggerBuffer=%d triggerHeld=%d speed=%.1f shots=%d\n\"",
		ent->client->ps.weapon,
		ent->client->ps.ammo[ent->client->ps.weapon],
		ent->client->ps.ammo[WP_SHOTGUN],
		ent->client->ps.ammo[WP_MACHINEGUN],
		ent->client->ps.ammo[WP_ROCKET_LAUNCHER],
		ent->client->ps.ammo[WP_PLASMAGUN],
		ent->client->ps.weaponTime,
		ent->client->ps.stats[STAT_RIFLE_TRIGGER_BUFFER],
		ent->client->ps.stats[STAT_RIFLE_TRIGGER_HELD],
		VectorLength( ent->client->ps.velocity ),
		ent->client->accuracy_shots ) );
}

static void Cmd_SpawnEnemy_f( gentity_t *ent ) {
	if ( !CheatsOk( ent ) ) return;
	trap_SendConsoleCommand( EXEC_INSERT, "addbot Grism 3 free 0\n" );
	trap_SendServerCommand( ent - g_entities, "print \"Queued a botlib enemy.\n\"" );
}

static void Cmd_KillCombatEnemies_f( gentity_t *ent ) {
	if ( !CheatsOk( ent ) ) return;
	int i;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *enemy = &g_entities[i];
		if ( !enemy->inuse || !enemy->client || !( enemy->r.svFlags & SVF_BOT ) ||
			G_IsCompanionClient( enemy ) || enemy->health <= 0 ) continue;
		G_Damage( enemy, ent, ent, NULL, enemy->r.currentOrigin, 100000,
			DAMAGE_NO_PROTECTION, MOD_SUICIDE );
	}
}

static void Cmd_ShowBotState_f( void ) {
	if ( !g_cheats.integer ) return;
	int i;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *bot = &g_entities[i];
		float ownerDistance = -1.0f;
		if ( !bot->inuse || !bot->client || !( bot->r.svFlags & SVF_BOT ) ) continue;
		if ( G_IsCompanionClient( bot ) && bot->client->pers.companionOwner >= 0 &&
			bot->client->pers.companionOwner < level.maxclients ) {
			vec3_t separation;
			VectorSubtract( bot->r.currentOrigin,
				g_entities[bot->client->pers.companionOwner].r.currentOrigin, separation );
			ownerDistance = VectorLength( separation );
		}
		G_Printf( "bot %i name=%s role=%s owner=%i ownerDistance=%.0f health=%i ammo=%i shots=%i hits=%i origin=(%.0f %.0f %.0f) velocity=(%.0f %.0f %.0f) yaw=%.0f\n",
			i, bot->client->pers.netname,
			G_IsCompanionClient( bot ) ? "companion" : "hostile",
			G_IsCompanionClient( bot ) ? bot->client->pers.companionOwner : -1,
			ownerDistance, bot->health, bot->client->ps.ammo[WP_MACHINEGUN],
			bot->client->accuracy_shots, bot->client->accuracy_hits,
			bot->r.currentOrigin[0], bot->r.currentOrigin[1], bot->r.currentOrigin[2],
			bot->client->ps.velocity[0], bot->client->ps.velocity[1], bot->client->ps.velocity[2],
			bot->client->ps.viewangles[YAW] );
	}
}

static void Cmd_CompanionTest_f( gentity_t *ent ) {
	if ( !CheatsOk( ent ) ) return;
	int i;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *companion = &g_entities[i];
		int before;
		if ( !companion->inuse || !G_IsCompanionClient( companion ) ||
			companion->client->pers.companionOwner != ent - g_entities ||
			companion->health <= 0 ) {
			continue;
		}
		before = companion->health;
		G_Damage( companion, ent, ent, NULL, companion->r.currentOrigin,
			20, 0, MOD_MACHINEGUN );
		G_Printf( "companion friendly-fire test: client=%i before=%i after=%i\n",
			i, before, companion->health );
		return;
	}
	G_Printf( "companion friendly-fire test: no active companion\n" );
}

static void Cmd_KillCompanion_f( gentity_t *ent ) {
	if ( !CheatsOk( ent ) ) return;
	int i;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *companion = &g_entities[i];
		if ( companion->inuse && G_IsCompanionClient( companion ) &&
			companion->client->pers.companionOwner == ent - g_entities &&
			companion->health > 0 ) {
			G_Damage( companion, ent, ent, NULL, companion->r.currentOrigin,
				100000, DAMAGE_NO_PROTECTION, MOD_SUICIDE );
			G_Printf( "companion respawn test: killed client=%d\n", i );
			return;
		}
	}
	G_Printf( "companion respawn test: no active companion\n" );
}

static void Cmd_RespawnNearCompanionTest_f( gentity_t *ent ) {
	// No alive check: respawning a dead player is a fair use of this hook.
	if ( !g_cheats.integer ) return;
	ClientRespawn( ent );
}

static void Cmd_AmmoPickupTest_f( gentity_t *ent ) {
	if ( !CheatsOk( ent ) ) return;
	gitem_t *item = BG_FindItemForAmmo( WP_MACHINEGUN );
	gentity_t *drop;
	vec3_t origin, velocity;

	if ( !item ) {
		G_Printf( "ammo pickup test: rifle ammo item missing\n" );
		return;
	}
	ent->client->ps.ammo[WP_MACHINEGUN] = 0;
	VectorCopy( ent->r.currentOrigin, origin );
	origin[2] += 20;
	VectorClear( velocity );
	drop = LaunchItem( item, origin, velocity );
	drop->count = 30;
	drop->s.pos.trType = TR_STATIONARY;
	VectorClear( drop->s.pos.trDelta );
	G_SetOrigin( drop, origin );
	trap_LinkEntity( drop );
	G_Printf( "ammo pickup test: spawned at height=20 ammo=0\n" );
}

// Prints what the player carries, for the loadout and pickup regressions.
static void Cmd_ShowLoadout_f( gentity_t *ent ) {
	gclient_t *client = ent->client;

	if ( !g_cheats.integer ) return;
	G_Printf( "loadout: pistol=%d rifle=%d rockets=%d mg=%d ammo=%d/%d/%d weapon=%d\n",
		( client->ps.stats[STAT_WEAPONS] >> WP_SHOTGUN ) & 1,
		( client->ps.stats[STAT_WEAPONS] >> WP_MACHINEGUN ) & 1,
		( client->ps.stats[STAT_WEAPONS] >> WP_ROCKET_LAUNCHER ) & 1,
		( client->ps.stats[STAT_WEAPONS] >> WP_PLASMAGUN ) & 1,
		client->ps.ammo[WP_MACHINEGUN], client->ps.ammo[WP_ROCKET_LAUNCHER],
		client->ps.ammo[WP_PLASMAGUN], client->ps.weapon );
}

// Drops a map-style (respawning) rocket launcher on the player, who doesn't
// own one yet, so the next show_loadout proves weapon pickups work. An
// optional weapon number drops that gun instead, for the autoswitch checks.
static void Cmd_WeaponPickupTest_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	int weapon = WP_ROCKET_LAUNCHER;
	gitem_t *item;
	gentity_t *pickup;

	if ( !CheatsOk( ent ) ) return;
	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		weapon = atoi( arg );
	}
	item = weapon > WP_NONE && weapon < WP_NUM_WEAPONS ? BG_FindItemForWeapon( weapon ) : NULL;
	if ( !item ) {
		G_Printf( "weapon pickup test: item for weapon %d missing\n", weapon );
		return;
	}
	ent->client->ps.stats[STAT_WEAPONS] &= ~( 1 << weapon );
	ent->client->ps.ammo[weapon] = 0;
	pickup = G_Spawn();
	pickup->classname = item->classname;
	pickup->item = item;
	VectorCopy( ent->r.currentOrigin, pickup->s.origin );
	pickup->s.origin[2] += 20;
	pickup->physicsBounce = 0.50;
	RegisterItem( item );
	SaveRegisteredItems();
	FinishSpawningItem( pickup );
	G_Printf( "weapon pickup test: spawned owned=0\n" );
}

static void Cmd_BotAmmoPickupTest_f( void ) {
	if ( !g_cheats.integer ) return;
	gentity_t *bot = NULL;
	gentity_t *drop;
	gitem_t *item = BG_FindItemForAmmo( WP_MACHINEGUN );
	vec3_t origin, velocity;
	int before;
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *candidate = &g_entities[i];
		if ( !candidate->inuse || !candidate->client ||
			!( candidate->r.svFlags & SVF_BOT ) || candidate->health <= 0 ) {
			continue;
		}
		if ( !bot || G_IsCompanionClient( candidate ) ) {
			bot = candidate;
		}
		if ( G_IsCompanionClient( candidate ) ) {
			break;
		}
	}
	if ( !bot || !item ) {
		G_Printf( "bot ammo pickup test: bot or rifle ammo item missing\n" );
		return;
	}

	bot->client->ps.ammo[WP_MACHINEGUN] = 0;
	before = bot->client->ps.ammo[WP_MACHINEGUN];
	VectorCopy( bot->r.currentOrigin, origin );
	origin[2] += 20;
	VectorClear( velocity );
	drop = LaunchItem( item, origin, velocity );
	drop->count = 30;
	drop->s.pos.trType = TR_STATIONARY;
	VectorClear( drop->s.pos.trDelta );
	G_SetOrigin( drop, origin );
	trap_LinkEntity( drop );
	Touch_Item( drop, bot, NULL );
	G_Printf( "bot ammo pickup test: client=%d ammo=%d->%d\n",
		(int)( bot - g_entities ), before, bot->client->ps.ammo[WP_MACHINEGUN] );
}

static void StageWallJumpClient( gentity_t *client, float x, float z, float xVelocity ) {
	vec3_t origin = { 0, -500, 0 };
	vec3_t angles = { 0, 0, 0 };

	origin[0] = x;
	origin[2] = z;
	trap_UnlinkEntity( client );
	VectorCopy( origin, client->client->ps.origin );
	VectorSet( client->client->ps.velocity, xVelocity, 0, xVelocity ? -40 : 0 );
	client->client->ps.groundEntityNum = ENTITYNUM_NONE;
	client->client->ps.pm_time = 0;
	client->client->ps.pm_flags &= ~( PMF_ALL_TIMES | PMF_JUMP_HELD | PMF_WALLJUMP_USED );
	client->client->ps.eFlags ^= EF_TELEPORT_BIT;
	SetClientViewAngle( client, angles );
	BG_PlayerStateToEntityState( &client->client->ps, &client->s, qtrue );
	VectorCopy( client->client->ps.origin, client->r.currentOrigin );
	trap_LinkEntity( client );
}

static void Cmd_WallJumpStage_f( gentity_t *ent ) {
	char action[32];
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	if ( !Q_stricmp( action, "state" ) ) {
		G_Printf( "wall jump state: client=%d x=%.0f z=%.0f vx=%.0f used=%d held=%d grounded=%d\n", (int)( ent - g_entities ),
			ent->client->ps.origin[0], ent->client->ps.origin[2], ent->client->ps.velocity[0],
			!!( ent->client->ps.pm_flags & PMF_WALLJUMP_USED ),
			!!( ent->client->ps.pm_flags & PMF_JUMP_HELD ),
			ent->client->ps.groundEntityNum != ENTITYNUM_NONE );
		return;
	}
	StageWallJumpClient( ent, !Q_stricmp( action, "early" ) ? 880 : 936, 240, 240 );
	if ( !Q_stricmp( action, "held" ) ) ent->client->ps.pm_flags |= PMF_JUMP_HELD;
	G_Printf( "wall jump test: staged player client=%d input=%s\n", (int)( ent - g_entities ),
		action[0] ? action : "timed" );
}

static void Cmd_BotWallJumpTest_f( void ) {
	if ( !g_cheats.integer ) return;
	gentity_t *bot = NULL;
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *candidate = &g_entities[i];
		if ( !candidate->inuse || !candidate->client ||
			!( candidate->r.svFlags & SVF_BOT ) || candidate->health <= 0 ) {
			continue;
		}
		if ( !bot || G_IsCompanionClient( candidate ) ) {
			bot = candidate;
		}
		if ( G_IsCompanionClient( candidate ) ) {
			break;
		}
	}
	if ( !bot ) {
		G_Printf( "bot wall jump test: no active bot\n" );
		return;
	}

	StageWallJumpClient( bot, 936, 96, 240 );
	G_Printf( "bot wall jump test: staged client=%d\n", (int)( bot - g_entities ) );
}

static void Cmd_CorpseAmmoPickupTest_f( gentity_t *ent ) {
	if ( !CheatsOk( ent ) ) return;
	int i;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *companion = &g_entities[i];
		vec3_t origin, angles;
		if ( !companion->inuse || !G_IsCompanionClient( companion ) ||
			companion->client->pers.companionOwner != ent - g_entities ||
			companion->health <= 0 ) {
			continue;
		}
		ent->client->ps.ammo[WP_MACHINEGUN] = 0;
		VectorCopy( companion->r.currentOrigin, origin );
		origin[0] += 44;
		VectorClear( angles );
		angles[YAW] = 180;
		G_Damage( companion, ent, ent, NULL, companion->r.currentOrigin,
			100000, DAMAGE_NO_PROTECTION, MOD_SUICIDE );
		TeleportPlayer( ent, origin, angles );
		G_Printf( "corpse ammo pickup test: player offset=44 ammo=0\n" );
		return;
	}
	G_Printf( "corpse ammo pickup test: no active companion\n" );
}

/*
=================
Cmd_TrapTest_f

Cheats-only checks for the Horologe traps. "bell" puts the first live bot under the bell and pulls a
rope as the caller, so the stroke should kill the bot and credit the caller. "balcony" puts the
caller in the middle of the east balcony, where the minute hand comes down within a turn.
"blast <n>" is the same check for any map: it puts the first live bot in the middle of the volume of
the n-th button's blast trap (counting from 0, in entity order) and pulls that button as the caller;
"blast <n> <x> <y>" puts the bot at x y instead, for traps whose middle is solid (the Rift Foundry
pour, round the forge tower). On Gallows Point, 0 is the land battery and 1 the sea battery.
"knock <x> <y> <z>" hurts the first live bot as the caller and then puts it at x y z, so a fall
into a KNOCKED hazard there should be credited to the caller.
=================
*/
static void Cmd_TrapTest_f( gentity_t *ent ) {
	char action[32], arg[16], coord[16];
	vec3_t origin, angles;
	gentity_t *bot, *rope, *trap;
	int i, n;

	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	VectorClear( angles );
	if ( !Q_stricmp( action, "bell" ) ) {
		bot = NULL;
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *t = &g_entities[i];
			if ( t != ent && t->inuse && t->client && ( t->r.svFlags & SVF_BOT ) && t->health > 0 &&
				t->client->sess.sessionTeam != TEAM_SPECTATOR ) {
				bot = t;
				break;
			}
		}
		rope = NULL;
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			if ( G_ButtonReady( &g_entities[i] ) && G_BlastTrap( &g_entities[i], 0 ) ) {
				rope = &g_entities[i];
				break;
			}
		}
		if ( !bot || !rope ) {
			G_Printf( "trap test: bell needs a live bot and a ready rope (bot=%d rope=%d)\n", bot != NULL, rope != NULL );
			return;
		}
		VectorSet( origin, 0, 0, 1600 );
		TeleportPlayer( bot, origin, angles );
		rope->touch( rope, ent, NULL );
		G_Printf( "trap test: bell rung by %s with %s in the belfry\n", ent->client->pers.netname,
			bot->client->pers.netname );
	} else if ( !Q_stricmp( action, "blast" ) ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		n = atoi( arg );
		bot = NULL;
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *t = &g_entities[i];
			if ( t != ent && t->inuse && t->client && ( t->r.svFlags & SVF_BOT ) && t->health > 0 &&
				t->client->sess.sessionTeam != TEAM_SPECTATOR ) {
				bot = t;
				break;
			}
		}
		rope = NULL;
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			gentity_t *t = &g_entities[i];
			if ( t->inuse && t->classname && !Q_stricmp( t->classname, "func_button" ) && G_BlastTrap( t, 0 ) &&
				n-- == 0 ) {
				rope = t;
				break;
			}
		}
		if ( !bot || !rope || !G_ButtonReady( rope ) ) {
			G_Printf( "trap test: blast needs a live bot and a ready button (bot=%d button=%d ready=%d)\n",
				bot != NULL, rope != NULL, rope && G_ButtonReady( rope ) );
			return;
		}
		trap = G_BlastTrap( rope, 0 );
		VectorAdd( trap->r.mins, trap->r.maxs, origin );
		VectorScale( origin, 0.5f, origin );
		origin[2] = trap->r.mins[2] + 96;
		if ( trap_Argc() >= 5 ) {
			trap_Argv( 3, coord, sizeof( coord ) );
			origin[0] = atof( coord );
			trap_Argv( 4, coord, sizeof( coord ) );
			origin[1] = atof( coord );
		}
		TeleportPlayer( bot, origin, angles );
		rope->touch( rope, ent, NULL );
		G_Printf( "trap test: blast %s set off by %s with %s at %d %d %d\n", arg, ent->client->pers.netname,
			bot->client->pers.netname, (int)origin[0], (int)origin[1], (int)origin[2] );
	} else if ( !Q_stricmp( action, "knock" ) ) {
		bot = NULL;
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *t = &g_entities[i];
			if ( t != ent && t->inuse && t->client && ( t->r.svFlags & SVF_BOT ) && t->health > 0 &&
				t->client->sess.sessionTeam != TEAM_SPECTATOR ) {
				bot = t;
				break;
			}
		}
		if ( !bot || trap_Argc() < 5 ) {
			G_Printf( "trap test: knock needs a live bot and x y z (bot=%d)\n", bot != NULL );
			return;
		}
		for ( i = 0; i < 3; i++ ) {
			trap_Argv( 2 + i, coord, sizeof( coord ) );
			origin[i] = atof( coord );
		}
		G_Damage( bot, ent, ent, NULL, NULL, 1, DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
		TeleportPlayer( bot, origin, angles );
		G_Printf( "trap test: knock by %s with %s at %d %d %d\n", ent->client->pers.netname,
			bot->client->pers.netname, (int)origin[0], (int)origin[1], (int)origin[2] );
	} else if ( !Q_stricmp( action, "balcony" ) ) {
		VectorSet( origin, 960, 0, 800 );
		angles[YAW] = 180;
		TeleportPlayer( ent, origin, angles );
		G_Printf( "trap test: %s on the east balcony\n", ent->client->pers.netname );
	}
}

/*
=================
Cmd_TelefragTest_f

Cheats-only checks for teleporting onto players in team games. "step" teleports the caller onto
the first live teammate bot, which should leave both alive with the caller beside the bot.
"teammate" and "enemy" telefrag the first live bot on that side as the caller, the way an arrival
with no clear floor nearby would: the teammate costs nothing and the enemy is a normal frag.
=================
*/
static void Cmd_TelefragTest_f( gentity_t *ent ) {
	char action[32];
	vec3_t origin, angles;
	gentity_t *bot = NULL;
	qboolean wantTeammate;
	int i, score, teamScore, team;

	if ( !g_cheats.integer || !ent->client ) return;
	trap_Argv( 1, action, sizeof( action ) );
	wantTeammate = Q_stricmp( action, "enemy" ) != 0;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *t = &g_entities[i];
		if ( t != ent && t->inuse && t->client && ( t->r.svFlags & SVF_BOT ) && t->health > 0 &&
			t->client->sess.sessionTeam != TEAM_SPECTATOR && OnSameTeam( ent, t ) == wantTeammate ) {
			bot = t;
			break;
		}
	}
	if ( !bot || ent->health <= 0 ) {
		G_Printf( "telefrag test: %s needs a live caller and bot (bot=%d)\n", action, bot != NULL );
		return;
	}
	team = ent->client->sess.sessionTeam;
	score = ent->client->ps.persistant[PERS_SCORE];
	teamScore = level.teamScores[team];
	if ( !Q_stricmp( action, "step" ) ) {
		VectorCopy( bot->r.currentOrigin, origin );
		VectorClear( angles );
		TeleportPlayer( ent, origin, angles );
		G_Printf( "telefrag test: step bot_alive=%d gap=%.0f score=%d/%d team=%d/%d\n",
			bot->health > 0, Distance( ent->r.currentOrigin, bot->r.currentOrigin ),
			score, ent->client->ps.persistant[PERS_SCORE], teamScore, level.teamScores[team] );
	} else {
		G_Damage( bot, ent, ent, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_TELEFRAG );
		G_Printf( "telefrag test: %s bot_alive=%d score=%d/%d team=%d/%d\n", action, bot->health > 0,
			score, ent->client->ps.persistant[PERS_SCORE], teamScore, level.teamScores[team] );
	}
}

#ifdef STANDALONE
static void Cmd_ArmsRaceTest_f( gentity_t *ent ) {
	char action[32], arg[32];
	int i;
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	trap_Argv( 2, arg, sizeof( arg ) );
	if ( !Q_stricmp( action, "next" ) ) {
		if ( G_ArmsRaceActive() ) level.armsRaceNextTime = level.time;
	} else if ( !Q_stricmp( action, "set" ) ) {
		if ( G_ArmsRaceActive() ) G_ArmsRaceSet( atoi( arg ), 45000 );
	} else if ( !Q_stricmp( action, "drain" ) ) {
		ent->client->ps.ammo[ent->client->ps.weapon] = 0;
	} else if ( !Q_stricmp( action, "frag" ) ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *target = &g_entities[i];
			if ( target != ent && target->inuse && target->client && target->health > 0 &&
				target->client->sess.sessionTeam == TEAM_FREE ) {
				G_Damage( target, ent, ent, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_MACHINEGUN );
				break;
			}
		}
	} else if ( !Q_stricmp( action, "odds" ) ) {
		// Every roll in 150 whole cycles of the draw (21 shares: five weapons at 4, Odin's Sneeze at 1),
		// so the counts are exact shares.
		int counts[WP_NUM_WEAPONS];
		memset( counts, 0, sizeof( counts ) );
		for ( i = 0; i < 3150; i++ ) counts[G_ArmsRacePick( WP_NONE, i )]++;
		for ( i = 0; i < WP_NUM_WEAPONS; i++ ) {
			if ( counts[i] ) G_Printf( "ArmsRace odds: weapon=%d count=%d\n", i, counts[i] );
		}
	} else if ( !Q_stricmp( action, "state" ) ) {
		int pickups = 0, armed = 0, others = 0;
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			gentity_t *item = &g_entities[i];
			if ( item->inuse && item->item &&
				( item->item->giType == IT_WEAPON || item->item->giType == IT_AMMO ) ) pickups++;
		}
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *other = &g_entities[i];
			if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED ||
				other->client->sess.sessionTeam != TEAM_FREE || other->health <= 0 ) continue;
			if ( other->client->ps.stats[STAT_WEAPONS] == ( 1 << level.armsRaceWeapon ) &&
				other->client->ps.weapon == level.armsRaceWeapon ) armed++;
			else others++;
		}
		G_Printf( "ArmsRace state: active=%d weapon=%d left=%d owned=%d held=%d ammo=%d same=%d other=%d pickups=%d classes=%d\n",
			G_ArmsRaceActive(), level.armsRaceWeapon, ( level.armsRaceNextTime - level.time ) / 1000,
			ent->client->ps.stats[STAT_WEAPONS], ent->client->ps.weapon,
			ent->client->ps.ammo[ent->client->ps.weapon], armed, others, pickups, G_PlayerClassesActive() );
	}
}

static void Cmd_LastManStandingTest_f( gentity_t *ent ) {
	char action[32];
	int i;
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	if ( !Q_stricmp( action, "respawn" ) ) {
		ClientRespawn( ent );
	} else if ( !Q_stricmp( action, "hurt" ) ) {
		// an optional third argument picks the health, for the HUD state checks
		ent->health = ent->client->ps.stats[STAT_HEALTH] = trap_Argc() > 2 ? atoi( ConcatArgs( 2 ) ) : 50;
	} else if ( !Q_stricmp( action, "world" ) ) {
		G_Damage( ent, NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_FALLING );
	} else if ( !Q_stricmp( action, "frag" ) ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *target = &g_entities[i];
			if ( target != ent && target->inuse && target->client && target->health > 0 &&
				target->client->sess.sessionTeam == TEAM_FREE ) {
				G_Damage( target, ent, ent, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_MACHINEGUN );
				break;
			}
		}
	} else if ( !Q_stricmp( action, "exhaust" ) || !Q_stricmp( action, "finish" ) ||
		!Q_stricmp( action, "draw" ) ) {
		if ( !G_LastManStandingActive() || !level.lmsStarted || level.intermissionQueued || level.intermissiontime ) return;
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *target = &g_entities[i];
			if ( !target->inuse || !target->client || target->client->pers.connected != CON_CONNECTED ||
				target->client->sess.sessionTeam != TEAM_FREE ) continue;
			if ( !Q_stricmp( action, "exhaust" ) && target != ent ) continue;
			if ( !Q_stricmp( action, "finish" ) && target == ent ) continue;
			while ( target->client->pers.lmsLives > 0 ) {
				if ( target->health <= 0 ) ClientRespawn( target );
				G_Damage( target, target, target, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_SUICIDE );
			}
		}
	} else if ( !Q_stricmp( action, "state" ) ) {
		G_Printf( "LMS state: active=%d started=%d configured=%d lives=%d network=%d team=%s health=%d deaths=%d score=%d queued=%d\n",
			G_LastManStandingActive(), level.lmsStarted, g_lmsLives.integer,
			ent->client->pers.lmsLives, ent->client->ps.persistant[PERS_LMS_LIVES],
			BG_TeamCommandName( ent->client->sess.sessionTeam ), ent->health,
			ent->client->ps.persistant[PERS_KILLED], ent->client->ps.persistant[PERS_SCORE],
			level.intermissionQueued != 0 );
		for ( i = 0; i < level.maxclients; i++ ) {
			gclient_t *cl = &level.clients[i];
			if ( cl->pers.connected != CON_CONNECTED ) continue;
			G_Printf( "LMS roster: client=%d lives=%d participant=%d waiting=%d team=%s companion=%d\n",
				i, cl->pers.lmsLives, cl->pers.lmsParticipant, cl->sess.lmsWaiting,
				BG_TeamCommandName( cl->sess.sessionTeam ), G_IsCompanionClient( &g_entities[i] ) );
		}
	}
}

static void Cmd_BugHuntTest_f( gentity_t *ent ) {
	char action[32], arg[16];
	int i, standing = 0;
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	trap_Argv( 2, arg, sizeof( arg ) );
	if ( !Q_stricmp( action, "spawn" ) ) {
		// a ring of spiders around the player, facing in
		int count = MAX( 1, atoi( arg ) );
		for ( i = 0; i < count; i++ ) {
			float angle = ent->client->ps.viewangles[YAW] + i * 360.0f / count;
			vec3_t origin;
			VectorCopy( ent->r.currentOrigin, origin );
			origin[0] += cos( DEG2RAD( angle ) ) * 260;
			origin[1] += sin( DEG2RAD( angle ) ) * 260;
			origin[2] -= 24;
			G_SpawnSpider( origin, AngleNormalize360( angle + 180 ), 1.0f );
		}
	} else if ( !Q_stricmp( action, "killbugs" ) ) {
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			gentity_t *spider = &g_entities[i];
			if ( G_IsSpider( spider ) && spider->takedamage ) {
				G_Damage( spider, ent, ent, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_MACHINEGUN );
			}
		}
	} else if ( !Q_stricmp( action, "nextwave" ) ) {
		level.bugWaveQuota = 0;
		level.bugNextWave = level.time;
	} else if ( !Q_stricmp( action, "die" ) ) {
		G_Damage( ent, NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
	} else if ( !Q_stricmp( action, "respawn" ) ) {
		if ( ent->health <= 0 ) ClientRespawn( ent );
	} else if ( !Q_stricmp( action, "wipe" ) ) {
		// every other squad loses all its lives
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *player = &g_entities[i];
			if ( player->client && player->client->pers.connected == CON_CONNECTED &&
				G_IsActiveTeam( player->client->sess.sessionTeam ) && !OnSameTeam( ent, player ) ) {
				player->client->pers.lmsLives = 1;
				if ( player->health > 0 ) {
					G_Damage( player, NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
				}
			}
		}
	} else {
		for ( i = 0; i < level.maxclients; i++ ) {
			gclient_t *cl = &level.clients[i];
			if ( cl->pers.connected == CON_CONNECTED && G_IsActiveTeam( cl->sess.sessionTeam ) &&
				cl->pers.lmsParticipant && cl->pers.lmsLives > 0 ) standing++;
		}
		G_Printf( "Bug Hunt state: client=%d active=%d started=%d wave=%d quota=%d bugs=%d kills=%d team=%s health=%d lives=%d score=%d teamscore=%d standing=%d\n",
			ent->s.number, G_BugHuntActive(), level.lmsStarted, level.bugWave, level.bugWaveQuota, G_CountSpiders(), level.bugKills,
			BG_TeamCommandName( ent->client->sess.sessionTeam ), ent->health, ent->client->pers.lmsLives,
			ent->client->ps.persistant[PERS_SCORE], level.teamScores[ent->client->sess.sessionTeam], standing );
	}
}

static void Cmd_InfectedTest_f( gentity_t *ent ) {
	char action[32];
	int i, survivors = 0, infected = 0;
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	if ( !Q_stricmp( action, "die" ) ) {
		G_Damage( ent, NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
	} else if ( !Q_stricmp( action, "respawn" ) ) {
		if ( ent->health <= 0 ) ClientRespawn( ent );
	} else if ( !Q_stricmp( action, "killall" ) ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *player = &g_entities[i];
			if ( player->client && player->client->pers.connected == CON_CONNECTED &&
				player->client->sess.sessionTeam == TEAM_BLUE && player->health > 0 ) {
				G_Damage( player, NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
			}
		}
	} else if ( !Q_stricmp( action, "timeout" ) ) {
		level.infectedEndTime = level.time;
	} else {
		for ( i = 0; i < level.maxclients; i++ ) {
			gclient_t *cl = &level.clients[i];
			if ( cl->pers.connected != CON_CONNECTED ) continue;
			if ( cl->sess.sessionTeam == TEAM_BLUE ) survivors++;
			else if ( cl->sess.sessionTeam == TEAM_RED ) infected++;
		}
		G_Printf( "Infected state: active=%d started=%d team=%s health=%d/%d speed=%d survivors=%d infected=%d strength=%.4f\n",
			G_InfectedActive(), level.infectedStarted, BG_TeamCommandName( ent->client->sess.sessionTeam ),
			ent->health, ent->client->ps.stats[STAT_MAX_HEALTH], ent->client->ps.speed, survivors, infected,
			G_InfectedStrength( TEAM_RED ) );
	}
}

static void Cmd_KillshiftTest_f( gentity_t *ent ) {
	char action[32];
	int i, companion = -1;
	gentity_t *target = NULL;
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( level.clients[i].pers.connected == CON_CONNECTED &&
			level.clients[i].pers.isCompanion && level.clients[i].pers.companionOwner == ent->s.number ) {
			companion = i;
		}
	}
	if ( !Q_stricmp( action, "setup" ) ) {
		level.warmupTime = 0;
		trap_SetConfigstring( CS_WARMUP, "0" );
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *player = &g_entities[i];
			if ( !player->client || player->client->pers.connected != CON_CONNECTED ) continue;
			player->client->sess.sessionTeam = ( player == ent || i == companion ) ? TEAM_RED : TEAM_BLUE;
			player->client->ps.persistant[PERS_TEAM] = player->client->sess.sessionTeam;
			ClientUserinfoChanged( i );
			if ( player->health <= 0 ) ClientRespawn( player );
		}
		CalculateRanks();
		ent->client->ps.persistant[PERS_KILLED] = 0;
		ent->client->ps.persistant[PERS_SCORE] = 0;
		ent->health = ent->client->ps.stats[STAT_HEALTH] = ent->client->ps.stats[STAT_MAX_HEALTH] / 2;
	} else if ( !Q_stricmp( action, "respawn" ) ) {
		ClientRespawn( ent );
	} else if ( !Q_stricmp( action, "world" ) ) {
		G_Damage( ent, NULL, NULL, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
	} else if ( !Q_stricmp( action, "friendly" ) ) {
		if ( companion >= 0 ) {
			int before = g_entities[companion].health;
			G_Damage( &g_entities[companion], ent, ent, NULL, NULL, 20, DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
			G_Printf( "Killshift friendly: before=%d after=%d\n", before, g_entities[companion].health );
		}
	} else if ( !Q_stricmp( action, "damage" ) || !Q_stricmp( action, "killed" ) || !Q_stricmp( action, "frag" ) ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *player = &g_entities[i];
			if ( player != ent && player->client && player->client->pers.connected == CON_CONNECTED &&
				player->health > 0 && !OnSameTeam( ent, player ) ) {
				target = player;
				break;
			}
		}
		if ( !target ) { G_Printf( "Killshift test: no enemy\n" ); return; }
		if ( !Q_stricmp( action, "killed" ) ) {
			G_Damage( ent, target, target, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_MACHINEGUN );
		} else if ( !Q_stricmp( action, "damage" ) ) {
			target->health = target->client->ps.stats[STAT_HEALTH] = 500;
			target->client->ps.stats[STAT_ARMOR] = 0;
			G_Damage( target, ent, ent, NULL, NULL, 20, DAMAGE_NO_PROTECTION | DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
			G_Printf( "Killshift damage: taken=%d strength=%.4f\n", 500 - target->health,
				G_KillshiftStrength( ent->client->sess.sessionTeam ) );
		} else {
			G_Damage( target, ent, ent, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_MACHINEGUN );
		}
	} else {
		G_Printf( "Killshift state: active=%d teams=%d team=%s health=%d/%d deaths=%d score=%d companion=%s\n",
			G_KillshiftActive(), G_ActiveTeamCount(), BG_TeamCommandName( ent->client->sess.sessionTeam ), ent->health,
			ent->client->ps.stats[STAT_MAX_HEALTH], ent->client->ps.persistant[PERS_KILLED],
			ent->client->ps.persistant[PERS_SCORE], companion >= 0 ?
			BG_TeamCommandName( level.clients[companion].sess.sessionTeam ) : "none" );
		for ( i = 0; i < G_ActiveTeamCount(); i++ ) {
			team_t team = BG_TeamForIndex( i );
			G_Printf( "Killshift team: %s count=%d score=%d strength=%.4f\n", BG_TeamCommandName( team ),
				TeamCount( -1, team ), level.teamScores[team], G_KillshiftStrength( team ) );
		}
	}
}

static void Cmd_DavidGoliathTest_f( gentity_t *ent ) {
	char action[32], value[32];
	int i;

	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	if ( !Q_stricmp( action, "respawn" ) ) {
		ClientRespawn( ent );
	} else if ( !Q_stricmp( action, "health" ) && ent->health > 0 ) {
		trap_Argv( 2, value, sizeof( value ) );
		ent->health = ent->client->ps.stats[STAT_HEALTH] = MAX( 1, atoi( value ) );
	} else if ( !Q_stricmp( action, "frag" ) || !Q_stricmp( action, "damage" ) ) {
		int targetNum;

		if ( !CheatsOk( ent ) ) return;
		trap_Argv( 2, value, sizeof( value ) );
		targetNum = value[0] ? atoi( value ) : -1;
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *target = &g_entities[i];
			if ( targetNum >= 0 && i != targetNum ) continue;
			if ( !target->inuse || !target->client || !( target->r.svFlags & SVF_BOT ) ||
				G_IsCompanionClient( target ) || target->health <= 0 ) continue;
			if ( !Q_stricmp( action, "damage" ) ) {
				target->health = target->client->ps.stats[STAT_HEALTH] = 100;
				target->client->ps.stats[STAT_ARMOR] = 0;
				G_Damage( target, ent, ent, NULL, NULL, 20, DAMAGE_NO_PROTECTION | DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
				G_Printf( "David and Goliath damage test: health=%d\n", target->health );
			} else {
				G_Damage( target, ent, ent, NULL, NULL, 10000, DAMAGE_NO_PROTECTION | DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
			}
			return;
		}
		G_Printf( "David and Goliath test: no living bot\n" );
	} else if ( !Q_stricmp( action, "place" ) && trap_Argc() == 6 ) {
		vec3_t origin, angles = { 1000000, 0, 0 };

		for ( i = 0; i < 3; i++ ) {
			trap_Argv( i + 2, value, sizeof( value ) );
			origin[i] = atof( value );
		}
		// The sentinel pitch suppresses the usual teleport launch.
		VectorClear( ent->client->ps.velocity );
		TeleportPlayer( ent, origin, angles );
		trap_Argv( 5, value, sizeof( value ) );
		VectorSet( angles, 0, atof( value ), 0 );
		SetClientViewAngle( ent, angles );
	} else if ( !Q_stricmp( action, "position" ) ) {
		const float *origin = ent->client->ps.origin;
		trace_t trace;

		trap_Trace( &trace, origin, ent->r.mins, ent->r.maxs, origin, ent->s.number, MASK_PLAYERSOLID );
		G_Printf( "David and Goliath position: x=%.3f y=%.3f z=%.3f solid=%d width=%.4f\n",
			origin[0], origin[1], origin[2], trace.startsolid || trace.allsolid, ent->r.maxs[0] );
	} else {
		G_Printf( "David and Goliath state: frags=%d scale=%.4f health=%d/%d statHealth=%d speed=%d width=%.4f height=%.4f\n",
			ent->health > 0 ? ent->client->ps.stats[STAT_GOLIATH_FRAGS] : 0,
			BG_DavidGoliathScale( ent->client->ps.generic1 ), ent->health,
			ent->client->ps.stats[STAT_MAX_HEALTH], ent->client->ps.stats[STAT_HEALTH], ent->client->ps.speed,
			ent->r.maxs[0], ent->r.maxs[2] );
	}
}
#endif

static void Cmd_CombatTestShot_f( gentity_t *ent ) {
	static int lockedTarget = -1;
	static qboolean hasLockedOrigin = qfalse;
	static vec3_t lockedOrigin;
	gentity_t *nearest = NULL;
	float nearestDistance = 999999.0f;
	trace_t trace;
	vec3_t start;
	vec3_t targetPoint;
	vec3_t direction;
	int i;

	// It teleports bots and fires on them, so keep it off public servers.
	if ( !CheatsOk( ent ) ) return;

	VectorCopy( ent->r.currentOrigin, start );
	start[2] += ent->client->ps.viewheight;
	if ( lockedTarget >= 0 && lockedTarget < level.maxclients ) {
		gentity_t *enemy = &g_entities[lockedTarget];
		if ( enemy->inuse && enemy->client && ( enemy->r.svFlags & SVF_BOT ) &&
			!G_IsCompanionClient( enemy ) && enemy->takedamage && enemy->health > 0 ) {
			nearest = enemy;
			VectorSubtract( enemy->r.currentOrigin, ent->r.currentOrigin, direction );
			nearestDistance = VectorLength( direction );
		} else {
			lockedTarget = -1;
			hasLockedOrigin = qfalse;
		}
	}

	if ( !nearest ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *enemy = &g_entities[i];
			float distance;
			if ( !enemy->inuse || !enemy->client || !( enemy->r.svFlags & SVF_BOT ) ||
				G_IsCompanionClient( enemy ) || !enemy->takedamage || enemy->health <= 0 ) continue;
			VectorCopy( enemy->r.currentOrigin, targetPoint );
			targetPoint[2] += enemy->client->ps.viewheight;
			trap_Trace( &trace, start, NULL, NULL, targetPoint, ent->s.number, MASK_SHOT );
			if ( trace.entityNum != enemy->s.number ) continue;
			VectorSubtract( enemy->r.currentOrigin, ent->r.currentOrigin, direction );
			distance = VectorLength( direction );
			if ( distance < nearestDistance ) {
				nearestDistance = distance;
				nearest = enemy;
			}
		}
	}
	if ( !nearest ) {
		G_Printf( "combat test shot: no visible bot target\n" );
		return;
	}
	if ( lockedTarget != nearest->s.number || !hasLockedOrigin ) {
		VectorCopy( nearest->r.currentOrigin, lockedOrigin );
		hasLockedOrigin = qtrue;
	}
	lockedTarget = nearest->s.number;
	G_SetOrigin( nearest, lockedOrigin );
	VectorCopy( lockedOrigin, nearest->client->ps.origin );
	VectorClear( nearest->client->ps.velocity );
	trap_LinkEntity( nearest );
	VectorSubtract( nearest->r.currentOrigin, ent->r.currentOrigin, direction );
	nearestDistance = VectorLength( direction );
	direction[2] += nearest->client->ps.viewheight - ent->client->ps.viewheight;
	vectoangles( direction, ent->client->ps.viewangles );
	// Always test with the hitscan rifle. The held weapon drifts during a busy
	// match (auto-switch on pickup, mid-change WP_NONE), and a rocket would only
	// land after the health below has already been printed.
	ent->client->ps.weapon = WP_MACHINEGUN;
	ent->client->ps.weaponstate = WEAPON_READY;
	ent->s.weapon = ent->client->ps.weapon;
	G_Printf( "combat test shot: player weapon=%i target=%i distance=%.0f\n",
		ent->s.weapon, nearest->s.number, nearestDistance );
	FireWeapon( ent );
	G_Printf( "combat test result: target=%i health=%i\n",
		nearest->s.number, nearest->health );
}

#ifdef STANDALONE
static void Cmd_SentinelTest_f( gentity_t *ent ) {
	char action[32], arg[32], amount[16];
	vec3_t dir, right;
	playerState_t *ps = &ent->client->ps;
	gentity_t *bot = NULL;
	int i, damage, mod = MOD_MACHINEGUN, flags = DAMAGE_NO_KNOCKBACK;
	if ( !g_cheats.integer ) return;
	trap_Argv( 1, action, sizeof( action ) );
	trap_Argv( 2, arg, sizeof( arg ) );
	trap_Argv( 3, amount, sizeof( amount ) );
	for ( i = level.maxclients - 1; i >= 0; i-- ) {
		if ( g_entities[i].inuse && g_entities[i].client && g_entities[i].r.svFlags & SVF_BOT ) {
			bot = &g_entities[i];
			break;
		}
	}
	if ( !Q_stricmp( action, "stage" ) ) {
		vec3_t origin = { 0, -350, 24 }, angles = { 0, 180, 0 };
		origin[0] += atof( arg );
		G_SetOrigin( ent, origin );
		VectorCopy( origin, ps->origin );
		VectorClear( ps->velocity );
		SetClientViewAngle( ent, angles );
		trap_LinkEntity( ent );
	} else if ( !Q_stricmp( action, "heal" ) ) {
		ent->health = ps->stats[STAT_HEALTH] = ps->stats[STAT_MAX_HEALTH];
		ps->stats[STAT_ARMOR] = 0;
	} else if ( !Q_stricmp( action, "hit" ) ) {
		int before = ent->health, shield = ps->stats[STAT_SHIELD_HEALTH];
		damage = amount[0] ? MAX( 1, MIN( 1000, atoi( amount ) ) ) : 20;
		AngleVectors( ps->viewangles, dir, right, NULL );
		if ( !Q_stricmp( arg, "side" ) ) VectorCopy( right, dir );
		else if ( Q_stricmp( arg, "back" ) ) VectorNegate( dir, dir );
		if ( !Q_stricmp( arg, "rocket" ) ) mod = MOD_ROCKET;
		if ( !Q_stricmp( arg, "splash" ) ) { mod = MOD_ROCKET_SPLASH; flags |= DAMAGE_RADIUS; }
		if ( !Q_stricmp( arg, "melee" ) ) mod = MOD_GAUNTLET;
		if ( !Q_stricmp( arg, "world" ) ) mod = MOD_FALLING;
		if ( !Q_stricmp( arg, "unprotected" ) ) flags |= DAMAGE_NO_PROTECTION;
		G_Damage( ent, NULL, NULL, dir, ent->r.currentOrigin, damage, flags, mod );
		G_Printf( "sentinel hit: kind=%s health=%d->%d shield=%d->%d raised=%d\n",
			arg, before, ent->health, shield, ps->stats[STAT_SHIELD_HEALTH], !!( ps->pm_flags & PMF_SHIELD_UP ) );
	} else if ( !Q_stricmp( action, "target" ) && bot ) {
		vec3_t origin;
		AngleVectors( ps->viewangles, dir, NULL, NULL );
		VectorMA( ps->origin, atof( arg ), dir, origin );
		origin[2] = ps->origin[2];
		G_SetOrigin( bot, origin );
		VectorCopy( origin, bot->client->ps.origin );
		VectorClear( bot->client->ps.velocity );
		bot->health = bot->client->ps.stats[STAT_HEALTH] = 100;
		bot->client->ps.stats[STAT_ARMOR] = 0;
		bot->client->ps.stats[STAT_SHIELD_HEALTH] = 0;
		trap_LinkEntity( bot );
	} else if ( !Q_stricmp( action, "bullet" ) && bot ) {
		int before = ent->health, shield = ps->stats[STAT_SHIELD_HEALTH];
		vec3_t angles;
		VectorSubtract( ps->origin, bot->client->ps.origin, dir );
		vectoangles( dir, angles );
		SetClientViewAngle( bot, angles );
		bot->client->ps.weapon = bot->s.weapon = WP_MACHINEGUN;
		FireWeapon( bot );
		G_Printf( "sentinel bullet: health=%d->%d shield=%d->%d\n", before, ent->health,
			shield, ps->stats[STAT_SHIELD_HEALTH] );
	} else if ( !Q_stricmp( action, "targetstate" ) && bot ) {
		G_Printf( "sentinel target: health=%d\n", bot->health );
	} else if ( !Q_stricmp( action, "motion" ) ) {
		G_Printf( "sentinel motion: raised=%d crouched=%d cap=%.0f actual=%.0f\n",
			!!( ps->pm_flags & PMF_SHIELD_UP ), !!( ps->pm_flags & PMF_DUCKED ),
			BG_PlayerMoveSpeed( ps, ent->client->pers.cmd.buttons ),
			sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ) );
	} else if ( !Q_stricmp( action, "swing" ) ) {
		G_Printf( "sentinel swing: raised=%d firing=%d shield=%d speed=%.0f\n",
			!!( ps->pm_flags & PMF_SHIELD_UP ), ps->weaponstate == WEAPON_FIRING && ps->weaponTime > 0,
			ps->stats[STAT_SHIELD_HEALTH], BG_PlayerMoveSpeed( ps, ent->client->pers.cmd.buttons ) );
	} else {
		G_Printf( "sentinel state: class=%s health=%d shield=%d raised=%d weapon=%d weapons=%d entity=%d\n",
			BG_PlayerClass( ent->client->pers.activeClass )->name, ent->health, ps->stats[STAT_SHIELD_HEALTH],
			!!( ps->pm_flags & PMF_SHIELD_UP ), ps->weapon, ps->stats[STAT_WEAPONS], ent->s.time2 );
	}
}
#endif

void ClientCommand( int clientNum ) {
	gentity_t *ent;
	char	cmd[MAX_TOKEN_CHARS];

	ent = g_entities + clientNum;
	if (!ent->client || ent->client->pers.connected != CON_CONNECTED) {
		if (ent->client && ent->client->pers.localClient) {
			// Handle early team command sent by UI when starting a local
			// team play game.
			trap_Argv( 0, cmd, sizeof( cmd ) );
			if (Q_stricmp (cmd, "team") == 0) {
				Cmd_Team_f (ent);
			}
		}
		return;		// not fully in game yet
	}


	trap_Argv( 0, cmd, sizeof( cmd ) );

	if ( ent->client->pers.menuObserver ) return;

	if (Q_stricmp (cmd, "say") == 0) {
		Cmd_Say_f (ent, SAY_ALL, qfalse);
		return;
	}
	if (Q_stricmp (cmd, "say_team") == 0) {
		Cmd_Say_f (ent, SAY_TEAM, qfalse);
		return;
	}
	if (Q_stricmp (cmd, "tell") == 0) {
		Cmd_Tell_f ( ent );
		return;
	}
#ifdef MISSIONPACK
	if (Q_stricmp (cmd, "vsay") == 0) {
		Cmd_Voice_f (ent, SAY_ALL, qfalse, qfalse);
		return;
	}
	if (Q_stricmp (cmd, "vsay_team") == 0) {
		Cmd_Voice_f (ent, SAY_TEAM, qfalse, qfalse);
		return;
	}
	if (Q_stricmp (cmd, "vtell") == 0) {
		Cmd_VoiceTell_f ( ent, qfalse );
		return;
	}
	if (Q_stricmp (cmd, "vosay") == 0) {
		Cmd_Voice_f (ent, SAY_ALL, qfalse, qtrue);
		return;
	}
	if (Q_stricmp (cmd, "vosay_team") == 0) {
		Cmd_Voice_f (ent, SAY_TEAM, qfalse, qtrue);
		return;
	}
	if (Q_stricmp (cmd, "votell") == 0) {
		Cmd_VoiceTell_f ( ent, qtrue );
		return;
	}
	if (Q_stricmp (cmd, "vtaunt") == 0) {
		Cmd_VoiceTaunt_f ( ent );
		return;
	}
#endif
	if (Q_stricmp (cmd, "score") == 0) {
		Cmd_Score_f (ent);
		return;
	}

	// ignore all other commands when at intermission
	if (level.intermissiontime) {
		Cmd_Say_f (ent, qfalse, qtrue);
		return;
	}

	if (Q_stricmp (cmd, "give") == 0)
		Cmd_Give_f (ent);
	// fire_start and fire_stop are the real held-fire path from cl_input.c, so
	// unlike the test hooks after them they must work without cheats.
	else if (Q_stricmp (cmd, "fire_start") == 0)
		Cmd_StartWeaponFire_f (ent);
	else if (Q_stricmp (cmd, "fire_stop") == 0)
		Cmd_StopWeaponFire_f (ent);
	else if (Q_stricmp (cmd, "giveammo") == 0)
		Cmd_GiveRifleAmmo_f (ent);
	else if (Q_stricmp (cmd, "show_weapon_state") == 0)
		Cmd_ShowWeaponState_f (ent);
	else if (Q_stricmp (cmd, "spawn_enemy") == 0)
		Cmd_SpawnEnemy_f (ent);
	else if (Q_stricmp (cmd, "kill_enemies") == 0)
		Cmd_KillCombatEnemies_f( ent );
	else if (Q_stricmp (cmd, "show_enemy_state") == 0)
		Cmd_ShowBotState_f();
	else if (Q_stricmp (cmd, "companion_test") == 0)
		Cmd_CompanionTest_f( ent );
	else if (Q_stricmp (cmd, "kill_companion") == 0)
		Cmd_KillCompanion_f( ent );
	else if (Q_stricmp (cmd, "respawn_near_companion_test") == 0)
		Cmd_RespawnNearCompanionTest_f( ent );
	else if (Q_stricmp (cmd, "ammo_pickup_test") == 0)
		Cmd_AmmoPickupTest_f( ent );
	else if (Q_stricmp (cmd, "show_loadout") == 0)
		Cmd_ShowLoadout_f( ent );
	else if (Q_stricmp (cmd, "weapon_pickup_test") == 0)
		Cmd_WeaponPickupTest_f( ent );
	else if (Q_stricmp (cmd, "bot_ammo_pickup_test") == 0)
		Cmd_BotAmmoPickupTest_f();
	else if (Q_stricmp (cmd, "walljump_stage") == 0)
		Cmd_WallJumpStage_f( ent );
	else if (Q_stricmp (cmd, "bot_walljump_test") == 0)
		Cmd_BotWallJumpTest_f();
	else if (Q_stricmp (cmd, "corpse_ammo_pickup_test") == 0)
		Cmd_CorpseAmmoPickupTest_f( ent );
	else if (Q_stricmp (cmd, "combat_test_shot") == 0)
		Cmd_CombatTestShot_f( ent );
#ifdef STANDALONE
	else if (Q_stricmp (cmd, "testbot") == 0)
		G_SpawnTestChamberBot( ent );
	else if (Q_stricmp (cmd, "arsenal_test") == 0)
		Cmd_ArsenalTest_f( ent );
	else if (Q_stricmp (cmd, "show_arsenal") == 0)
		Cmd_ShowArsenal_f( ent );
	else if (Q_stricmp (cmd, "sentinel_test") == 0)
		Cmd_SentinelTest_f( ent );
	else if (Q_stricmp (cmd, "class_test") == 0) {
		int i;
		char action[MAX_TOKEN_CHARS];
		if ( !g_cheats.integer ) return;
		trap_Argv( 1, action, sizeof( action ) );
		if ( !Q_stricmp( action, "roster" ) ) {
			for ( i = 0; i < level.maxclients; i++ ) {
				gentity_t *player = &g_entities[i];
				if ( !player->client || player->client->pers.connected != CON_CONNECTED ) continue;
				G_Printf( "class roster: client=%d bot=%d class=%s weapon=%d health=%d armor=%d speed=%d\n",
					i, !!( player->r.svFlags & SVF_BOT ), BG_PlayerClass( player->client->pers.activeClass )->name,
					player->client->ps.weapon, player->health, player->client->ps.stats[STAT_ARMOR], player->client->ps.speed );
			}
		} else {
			G_ClassSelectionStatus( ent );
			G_Printf( "class state: active=%d class=%s choice=%s selection=%d wait=%d health=%d max=%d armor=%d speed=%d weapon=%d ammo=%d/%d/%d spawn=%d lives=%d team=%s round=%d\n",
				G_PlayerClassesActive(), BG_PlayerClass( ent->client->pers.activeClass )->name,
				BG_PlayerClass( ent->client->pers.playerClass )->name, ent->client->ps.stats[STAT_CLASS_SELECTION],
				ent->client->ps.stats[STAT_CLASS_RESPAWN_WAIT], ent->health, ent->client->ps.stats[STAT_MAX_HEALTH],
				ent->client->ps.stats[STAT_ARMOR], ent->client->ps.speed, ent->client->ps.weapon,
				ent->client->ps.ammo[WP_MACHINEGUN], ent->client->ps.ammo[WP_ROCKET_LAUNCHER], ent->client->ps.ammo[WP_PLASMAGUN],
				ent->client->ps.persistant[PERS_SPAWN_COUNT], ent->client->pers.lmsLives, BG_TeamCommandName( ent->client->sess.sessionTeam ),
				level.lmsStarted || level.infectedStarted );
		}
	}
	else if (Q_stricmp (cmd, "selectclass") == 0) {
		char name[MAX_TOKEN_CHARS];
		trap_Argv( 1, name, sizeof( name ) );
		G_SelectPlayerClass( ent, name );
	}
	else if (Q_stricmp (cmd, "trap_test") == 0)
		Cmd_TrapTest_f( ent );
	else if (Q_stricmp (cmd, "telefrag_test") == 0)
		Cmd_TelefragTest_f( ent );
	else if (Q_stricmp (cmd, "lms_test") == 0)
		Cmd_LastManStandingTest_f( ent );
	else if (Q_stricmp (cmd, "armsrace_test") == 0)
		Cmd_ArmsRaceTest_f( ent );
	else if (Q_stricmp (cmd, "bughunt_test") == 0)
		Cmd_BugHuntTest_f( ent );
	else if (Q_stricmp (cmd, "infected_test") == 0)
		Cmd_InfectedTest_f( ent );
	else if (Q_stricmp (cmd, "killshift_test") == 0)
		Cmd_KillshiftTest_f( ent );
	else if (Q_stricmp (cmd, "david_goliath_test") == 0)
		Cmd_DavidGoliathTest_f( ent );
#endif
	else if (Q_stricmp (cmd, "god") == 0)
		Cmd_God_f (ent);
	else if (Q_stricmp (cmd, "notarget") == 0)
		Cmd_Notarget_f (ent);
	else if (Q_stricmp (cmd, "noclip") == 0)
		Cmd_Noclip_f (ent);
	else if (Q_stricmp (cmd, "kill") == 0)
		Cmd_Kill_f (ent);
#ifdef STANDALONE
	else if (Q_stricmp (cmd, "respawn") == 0)
		Cmd_Respawn_f (ent);
#endif
	else if (Q_stricmp (cmd, "teamtask") == 0)
		Cmd_TeamTask_f (ent);
	else if (Q_stricmp (cmd, "levelshot") == 0)
		Cmd_LevelShot_f (ent);
	else if (Q_stricmp (cmd, "follow") == 0)
		Cmd_Follow_f (ent);
	else if (Q_stricmp (cmd, "follownext") == 0)
		Cmd_FollowCycle_f (ent, 1);
	else if (Q_stricmp (cmd, "followprev") == 0)
		Cmd_FollowCycle_f (ent, -1);
	else if (Q_stricmp (cmd, "team") == 0)
		Cmd_Team_f (ent);
	else if (Q_stricmp (cmd, "where") == 0)
		Cmd_Where_f (ent);
	else if (Q_stricmp (cmd, "callvote") == 0)
		Cmd_CallVote_f (ent);
	else if (Q_stricmp (cmd, "vote") == 0)
		Cmd_Vote_f (ent);
	else if (Q_stricmp (cmd, "callteamvote") == 0)
		Cmd_CallTeamVote_f (ent);
	else if (Q_stricmp (cmd, "teamvote") == 0)
		Cmd_TeamVote_f (ent);
	else if (Q_stricmp (cmd, "gc") == 0)
		Cmd_GameCommand_f( ent );
	else if (Q_stricmp (cmd, "setviewpos") == 0)
		Cmd_SetViewpos_f( ent );
	else if (Q_stricmp (cmd, "stats") == 0)
		Cmd_Stats_f( ent );
	else
		trap_SendServerCommand( clientNum, va("print \"unknown cmd %s\n\"", cmd ) );
}
