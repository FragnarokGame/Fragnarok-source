/*
===========================================================================
Bug Hunt: two to four squads share the map with growing waves of spiders that
attack everyone. Squads can still kill each other. Every player has a limited
number of lives (g_bugLives) and sits out the rest of the round once they run
out, so surviving matters as much as fragging. Spider kills and enemy kills
both score for the team.

The round ends when only one squad still has players standing (that squad
wins), or, when everyone started on one squad, when that squad is wiped out.
With a timelimit, the squad with the most players standing at the end wins.

Lives reuse Last Man Standing's per-client bookkeeping (pers.lmsLives,
sess.lmsWaiting and the level.lms* round fields); G_LivesModeActive() covers
both modes.
===========================================================================
*/

#include "g_local.h"

#define BUGHUNT_SETTLE_MSEC		3000
#define BUGHUNT_FIRST_WAVE_MSEC	8000
#define BUGHUNT_WAVE_BREAK_MSEC	5000
#define BUGHUNT_SPAWN_MSEC		100
#define BUGHUNT_SPAWN_CLEARANCE	900.0f
#define BUGHUNT_SPAWN_REACH		2200.0f

qboolean G_BugHuntActive( void ) {
	return g_gametype.integer == GT_TEAM && g_bugHunt.integer;
}

qboolean G_LivesModeActive( void ) {
	return G_LastManStandingActive() || G_BugHuntActive();
}

qboolean G_LivesModeCanJoin( gclient_t *client ) {
	// Managed matches admit humans into bot places throughout the round.
	// An eliminated player must still wait instead of receiving fresh lives.
	return !level.lmsStarted || ( trap_Cvar_VariableIntegerValue( "sv_matchManaged" ) == 1 &&
		!( g_entities[client - level.clients].r.svFlags & SVF_BOT ) && !client->pers.menuObserver &&
		!client->pers.lmsParticipant && !client->sess.lmsWaiting );
}

qboolean G_LivesModeJoinPending( void ) {
	int i;
	if ( trap_Cvar_VariableIntegerValue( "sv_matchManaged" ) != 1 ) return qfalse;
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *client = &level.clients[i];
		if ( client->pers.connected == CON_DISCONNECTED || !G_LivesModeCanJoin( client ) ) continue;
		if ( client->pers.connected == CON_CONNECTING ) return qtrue;
#ifdef STANDALONE
		if ( client->pers.classInitialPending ) return qtrue;
#endif
	}
	return qfalse;
}

int G_LivesModeStartingLives( void ) {
	if ( level.campaign ) return level.campaign->lives;
	return G_BugHuntActive() ? (int)Com_Clamp( 1, 99, g_bugLives.integer ) : g_lmsLives.integer;
}

// Players still in the round on each team; returns the total.
static int BugHunt_Standing( int counts[TEAM_NUM_TEAMS] ) {
	int i, total = 0;

	memset( counts, 0, sizeof( int ) * TEAM_NUM_TEAMS );
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected == CON_CONNECTED && G_IsActiveTeam( cl->sess.sessionTeam ) &&
			cl->pers.lmsParticipant && cl->pers.lmsLives > 0 ) {
			counts[cl->sess.sessionTeam]++;
			total++;
		}
	}
	return total;
}

static int BugHunt_TeamsStanding( const int counts[TEAM_NUM_TEAMS], team_t *last ) {
	int i, teams = 0;

	*last = TEAM_FREE;
	for ( i = 0; i < G_ActiveTeamCount(); i++ ) {
		team_t team = BG_TeamForIndex( i );
		if ( counts[team] ) {
			teams++;
			*last = team;
		}
	}
	return teams;
}

static void BugHunt_SetState( void ) {
	trap_SetConfigstring( CS_MODE_STATE, va( "%i %i %i %i", level.lmsStarted, level.bugWave,
		G_CountSpiders(), level.lmsStarted ? level.bugNextWave : level.lmsStartTime ) );
}

/*
Where the next spider comes from. Each spider is aimed at one standing player
picked at random, so a squad draws bugs in proportion to its size: a spawn
point BUGHUNT_SPAWN_CLEARANCE to BUGHUNT_SPAWN_REACH from that player and
at least the clearance from everyone else. Without one in reach, any spawn
point clear of everyone, then the one farthest from everyone.
*/
static int BugHunt_StandingPlayers( gentity_t **players ) {
	int i, count = 0;

	for ( i = 0; i < level.numConnectedClients; i++ ) {
		gentity_t *player = &g_entities[level.sortedClients[i]];
		gclient_t *cl = player->client;
		if ( cl->pers.connected == CON_CONNECTED && player->health > 0 &&
			G_IsActiveTeam( cl->sess.sessionTeam ) && cl->pers.lmsParticipant && cl->pers.lmsLives > 0 ) {
			players[count++] = player;
		}
	}
	return count;
}

// A free spot for a spider within a short scatter of point, so a burst of
// spawns at one spawn point doesn't stack them inside each other.
static qboolean BugHunt_ScatterSpot( const vec3_t point, vec3_t origin ) {
	vec3_t mins = { -24, -24, 0 }, maxs = { 24, 24, 40 };
	int attempt;

	for ( attempt = 0; attempt < 8; attempt++ ) {
		trace_t tr;
		vec3_t start, end;
		float angle = random() * M_PI * 2, radius = attempt ? 40 + random() * 120 : 0;

		VectorSet( start, point[0] + cos( angle ) * radius, point[1] + sin( angle ) * radius, point[2] + 24 );
		VectorCopy( start, end );
		end[2] -= 160;
		trap_Trace( &tr, start, mins, maxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.7f ) {
			continue;
		}
		// make sure the spot it would occupy is empty, other spiders included
		trap_Trace( &tr, tr.endpos, mins, maxs, tr.endpos, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( !tr.startsolid ) {
			VectorCopy( tr.endpos, origin );
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean BugHunt_SpawnSpot( vec3_t origin, float *yaw ) {
	gentity_t *spot = NULL, *players[MAX_CLIENTS], *focus;
	vec3_t nearFocus[64], clear[64], fallback;
	float nearYaw[64], clearYaw[64], fallbackYaw = 0, bestNearest = -1;
	int numPlayers, numNear = 0, numClear = 0;

	numPlayers = BugHunt_StandingPlayers( players );
	focus = numPlayers ? players[rand() % numPlayers] : NULL;
	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		float nearest = 1e18f;
		int i;

		for ( i = 0; i < numPlayers; i++ ) {
			nearest = MIN( nearest, Distance( players[i]->r.currentOrigin, spot->s.origin ) );
		}
		if ( nearest > bestNearest ) {
			bestNearest = nearest;
			VectorCopy( spot->s.origin, fallback );
			fallbackYaw = spot->s.angles[YAW];
		}
		if ( nearest < BUGHUNT_SPAWN_CLEARANCE ) {
			continue;
		}
		if ( focus && Distance( focus->r.currentOrigin, spot->s.origin ) <= BUGHUNT_SPAWN_REACH ) {
			// reservoir sample so big maps don't overflow the list
			i = numNear < ARRAY_LEN( nearFocus ) ? numNear : rand() % ( numNear + 1 );
			if ( i < ARRAY_LEN( nearFocus ) ) {
				VectorCopy( spot->s.origin, nearFocus[i] );
				nearYaw[i] = spot->s.angles[YAW];
			}
			numNear++;
		}
		i = numClear < ARRAY_LEN( clear ) ? numClear : rand() % ( numClear + 1 );
		if ( i < ARRAY_LEN( clear ) ) {
			VectorCopy( spot->s.origin, clear[i] );
			clearYaw[i] = spot->s.angles[YAW];
		}
		numClear++;
	}
	if ( numNear ) {
		int pick = rand() % MIN( numNear, ARRAY_LEN( nearFocus ) );
		*yaw = nearYaw[pick];
		return BugHunt_ScatterSpot( nearFocus[pick], origin );
	}
	if ( numClear ) {
		int pick = rand() % MIN( numClear, ARRAY_LEN( clear ) );
		*yaw = clearYaw[pick];
		return BugHunt_ScatterSpot( clear[pick], origin );
	}
	if ( bestNearest >= 0 ) {
		*yaw = fallbackYaw;
		return BugHunt_ScatterSpot( fallback, origin );
	}
	return qfalse;
}

// Alive cap: g_bugMax, or with 0, 20 per standing player between 40 and 300.
static int BugHunt_MaxAlive( int standing ) {
	if ( g_bugMax.integer > 0 ) {
		return (int)Com_Clamp( 1, 400, g_bugMax.integer );
	}
	return (int)Com_Clamp( 40, 300, 20 * standing );
}

static void BugHunt_RunWaves( int standing ) {
	int alive = G_CountSpiders(), maxAlive = BugHunt_MaxAlive( standing );

	if ( level.bugNextWave && level.time >= level.bugNextWave && !level.bugWaveQuota ) {
		level.bugWave++;
		// per standing player, so every squad's share of the swarm grows with it
		level.bugWaveSize = MAX( 12, standing * ( 5 + 2 * ( level.bugWave - 1 ) ) );
		level.bugWaveQuota = level.bugWaveSize;
		level.bugNextWave = 0;
		trap_SendServerCommand( -1, va( "cp \"WAVE %i\n\"", level.bugWave ) );
		G_LogPrintf( "Bug Hunt wave: wave=%d size=%d standing=%d max=%d\n", level.bugWave, level.bugWaveSize,
			standing, maxAlive );
	}
	if ( level.bugWaveQuota > 0 && level.time >= level.bugNextSpawn ) {
		// spawn in bursts that grow with the player count
		int burst = (int)Com_Clamp( 1, 8, ( standing + 1 ) / 2 );
		while ( burst-- > 0 && level.bugWaveQuota > 0 && alive < maxAlive ) {
			vec3_t origin;
			float yaw;
			if ( !BugHunt_SpawnSpot( origin, &yaw ) ) {
				break;
			}
			G_SpawnSpider( origin, yaw, 1.0f + 0.1f * ( level.bugWave - 1 ) );
			level.bugWaveQuota--;
			alive++;
		}
		level.bugNextSpawn = level.time + BUGHUNT_SPAWN_MSEC;
	}
	// Next wave once 70% of the current one is dead.
	if ( !level.bugWaveQuota && !level.bugNextWave && alive <= level.bugWaveSize * 3 / 10 ) {
		level.bugNextWave = level.time + BUGHUNT_WAVE_BREAK_MSEC;
	}
}

void G_BugHuntSpiderKilled( gentity_t *spider, gentity_t *attacker ) {
	if ( !G_BugHuntActive() ) {
		return;
	}
	level.bugKills++;
	G_LogPrintf( "Bug Hunt kill: spider=%d killer=%d kills=%d\n", spider->s.number,
		attacker && attacker->client ? attacker->s.number : -1, level.bugKills );
}

static void BugHunt_End( const char *centre, const char *reason ) {
	trap_SendServerCommand( -1, va( "cp \"%s\n\"", centre ) );
	trap_SendServerCommand( -1, va( "print \"%s Waves survived: %i. Bugs killed: %i.\n\"",
		centre, MAX( 0, level.bugWave - 1 ), level.bugKills ) );
	LogExit( reason );
}

void G_BugHuntCheckRules( void ) {
	int counts[TEAM_NUM_TEAMS], standing, teams, timelimit, startTime;
	team_t last;

	if ( level.warmupTime || trap_Cvar_VariableIntegerValue( "g_menuBackground" ) ) {
		return;
	}
	standing = BugHunt_Standing( counts );
	teams = BugHunt_TeamsStanding( counts, &last );

	if ( !level.lmsStarted ) {
#ifdef STANDALONE
		if ( G_ClassSelectionPending() ) {
			level.lmsStartTime = 0;
			return;
		}
#endif
		// Let the squads finish joining before locking the round.
		if ( !level.lmsStartTime || standing != level.lmsPendingCount ) {
			level.lmsPendingCount = standing;
			level.lmsStartTime = standing ? level.time + BUGHUNT_SETTLE_MSEC : 0;
			BugHunt_SetState();
			return;
		}
		if ( !standing || level.time < level.lmsStartTime ) {
			return;
		}
		level.lmsStarted = qtrue;
		G_CampaignStart();
		level.bugTeamsAtStart = teams;
		level.bugWave = 0;
		level.bugNextWave = level.time + BUGHUNT_FIRST_WAVE_MSEC;
		trap_SendServerCommand( -1, "cp \"BUG HUNT\n\"" );
		G_LogPrintf( "Bug Hunt start: players=%d teams=%d lives=%d\n", standing, teams, G_LivesModeStartingLives() );
		BugHunt_SetState();
		return;
	}

	BugHunt_RunWaves( standing );
	BugHunt_SetState();

	timelimit = level.campaign ? level.campaign->minutes : g_timelimit.integer;
	startTime = level.campaign ? level.lmsStartTime : level.startTime;
	if ( timelimit > 0 && level.time - startTime >= timelimit * 60000 ) {
		int i, best = 0;
		team_t winner = TEAM_FREE;
		qboolean tied = qfalse;
		for ( i = 0; i < G_ActiveTeamCount(); i++ ) {
			team_t team = BG_TeamForIndex( i );
			if ( counts[team] > best ) {
				best = counts[team];
				winner = team;
				tied = qfalse;
			} else if ( counts[team] == best && best ) {
				tied = qtrue;
			}
		}
		if ( winner != TEAM_FREE && !tied ) {
			G_CampaignFinish( -1, winner );
			BugHunt_End( va( "%s SQUAD HOLDS THE LINE", BG_TeamName( winner ) ), "Bug Hunt timelimit." );
		} else {
			G_CampaignFinish( -1, TEAM_FREE );
			BugHunt_End( "TIME UP. NO SQUAD HELD", "Bug Hunt timelimit draw." );
		}
		return;
	}

	if ( teams > ( level.bugTeamsAtStart >= 2 ? 1 : 0 ) || G_LivesModeJoinPending() ) {
		level.lmsFinishTime = 0;
		return;
	}
	// Resolve simultaneous deaths before naming the survivors.
	if ( !level.lmsFinishTime ) {
		level.lmsFinishTime = level.time + 100;
	}
	if ( level.time < level.lmsFinishTime ) {
		return;
	}
	if ( teams == 1 ) {
		G_CampaignFinish( -1, last );
		BugHunt_End( va( "%s SQUAD SURVIVES", BG_TeamName( last ) ), "Bug Hunt survivors." );
	} else {
		G_CampaignFinish( -1, TEAM_SPECTATOR );
		BugHunt_End( "THE SWARM WINS", "Bug Hunt swarm." );
	}
}
