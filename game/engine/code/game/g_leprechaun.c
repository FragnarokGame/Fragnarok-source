#include "g_local.h"

qboolean G_LeprechaunActive( void ) {
	return g_gametype.integer == GT_FFA && g_leprechaun.integer;
}

#ifdef STANDALONE

#define LEPRECHAUN_SETTLE_MSEC 2000
#define LEPRECHAUN_POINT_MSEC 3000

static qboolean Leprechaun_Eligible( const gentity_t *ent ) {
	return ent && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED &&
		ent->client->sess.sessionTeam == TEAM_FREE && !ent->client->pers.menuObserver &&
		!ent->client->pers.classInitialPending && !G_IsCompanionClient( ent );
}

static void Leprechaun_Transform( gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;

	ps->eFlags |= EF_LEPRECHAUN;
	ent->s.eFlags |= EF_LEPRECHAUN;
	// Nearby listeners need the sound source even around a corner.
	ent->r.svFlags |= SVF_BROADCAST;
	ps->stats[STAT_MAX_HEALTH] = 3 * ent->client->pers.maxHealth;
	ent->health = ps->stats[STAT_HEALTH] = ps->stats[STAT_MAX_HEALTH];
	ps->viewheight = BG_PlayerViewHeight( ps );
	BG_PlayerBounds( ps->generic1, ps->eFlags, ( ps->pm_flags & PMF_DUCKED ) != 0,
		qfalse, ent->r.mins, ent->r.maxs );
	trap_LinkEntity( ent );

	level.leprechaunNextPoint = level.time + LEPRECHAUN_POINT_MSEC;
	trap_SetConfigstring( CS_MODE_STATE, va( "%d", ent->s.number + 1 ) );
	G_LogPrintf( "Leprechaun: client=%d health=%d\n", ent->s.number, ent->health );
}

void G_LeprechaunLeave( gentity_t *ent ) {
	if ( level.leprechaun != ent ) return;

	ent->r.svFlags &= ~SVF_BROADCAST;
	level.leprechaun = NULL;
	level.leprechaunNextPoint = 0;
	trap_SetConfigstring( CS_MODE_STATE, "" );
}

void G_LeprechaunDeath( gentity_t *victim, gentity_t *attacker ) {
	if ( level.leprechaun != victim ) return;

	G_LeprechaunLeave( victim );
	if ( level.intermissionQueued || level.intermissiontime || victim == attacker ||
		!Leprechaun_Eligible( attacker ) ) return;

	// A posthumous kill reserves the role for the killer's next spawn.
	level.leprechaun = attacker;
	if ( attacker->health > 0 ) Leprechaun_Transform( attacker );
}

void G_LeprechaunRunFrame( void ) {
	gentity_t *holder = level.leprechaun;
	int score, limit;

	if ( !G_LeprechaunActive() || level.intermissionQueued || level.intermissiontime || level.warmupTime ||
		trap_Cvar_VariableIntegerValue( "g_menuBackground" ) ||
		trap_Cvar_VariableIntegerValue( "g_matchJoining" ) ) return;

	if ( !holder ) {
		int i, count = 0;
		gentity_t *candidates[MAX_CLIENTS];

		if ( !level.leprechaunStarted ) {
			if ( G_ClassSelectionPending() || level.numPlayingClients < 2 ) {
				level.leprechaunPickTime = 0;
				return;
			}
			if ( !level.leprechaunPickTime || level.leprechaunRosterSize != level.numPlayingClients ) {
				level.leprechaunPickTime = level.time + LEPRECHAUN_SETTLE_MSEC;
				level.leprechaunRosterSize = level.numPlayingClients;
				return;
			}
			if ( level.time < level.leprechaunPickTime ) return;
		}

		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *ent = &g_entities[i];
			if ( Leprechaun_Eligible( ent ) && ent->health > 0 ) candidates[count++] = ent;
		}
		if ( !count ) return;
		level.leprechaunStarted = qtrue;
		level.leprechaun = candidates[rand() % count];
		Leprechaun_Transform( level.leprechaun );
		return;
	}

	if ( holder->health <= 0 ) return;
	if ( !level.leprechaunNextPoint ) {
		Leprechaun_Transform( holder );
		return;
	}
	if ( level.time < level.leprechaunNextPoint ) return;

	level.leprechaunNextPoint += LEPRECHAUN_POINT_MSEC;
	score = ++holder->client->ps.persistant[PERS_SCORE];
	CalculateRanks();
	limit = BG_LeprechaunScoreLimit( g_fraglimit.integer );
	if ( score >= limit && !level.intermissionQueued ) {
		trap_SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " wins Kill the Leprechaun with %d points!\n\"",
			holder->client->pers.netname, score ) );
		LogExit( "Leprechaun point limit hit." );
	}
}

#else

void G_LeprechaunLeave( gentity_t *ent ) {}
void G_LeprechaunDeath( gentity_t *victim, gentity_t *attacker ) {}
void G_LeprechaunRunFrame( void ) {}

#endif
