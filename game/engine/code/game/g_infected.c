/*
===========================================================================
Infected: one random player starts on the red (infected) team, everyone else
on blue (survivors). A survivor who dies for any reason turns infected. The
infected are stronger while they are few: maximum health and weapon damage
scale by 1 + 2 * survivors / players, and movement speed by a fifth of that
bonus, so the first carrier is nearly three times as tough and the last
survivors face a horde at normal strength. Survivors win by lasting until the
round clock runs out; the infected win by converting everyone.
===========================================================================
*/

#include "g_local.h"

#define INFECTED_SETTLE_MSEC	3000

qboolean G_InfectedActive( void ) {
	return g_gametype.integer == GT_TEAM && g_infected.integer;
}

// Killshift and Infected both move players between teams on death, score
// teams by population and manage maximum health themselves.
qboolean G_ConversionModeActive( void ) {
	return G_KillshiftActive() || G_InfectedActive();
}

static void Infected_Count( int *survivors, int *infected ) {
	int i;
	*survivors = *infected = 0;
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected == CON_DISCONNECTED ) continue;
		if ( cl->sess.sessionTeam == TEAM_BLUE ) ( *survivors )++;
		else if ( cl->sess.sessionTeam == TEAM_RED ) ( *infected )++;
	}
}

float G_InfectedStrength( team_t team ) {
	int survivors, infected;
	if ( !G_InfectedActive() || !level.infectedStarted || team != TEAM_RED ) return 1.0f;
	Infected_Count( &survivors, &infected );
	if ( survivors + infected <= 0 ) return 1.0f;
	return 1.0f + 2.0f * survivors / ( survivors + infected );
}

float G_InfectedSpeedScale( team_t team ) {
	return 1.0f + ( G_InfectedStrength( team ) - 1.0f ) * 0.2f;
}

team_t G_InfectedPickTeam( void ) {
	return level.infectedStarted ? TEAM_RED : TEAM_BLUE;
}

void G_InfectedUpdateBalance( void ) {
	int i, survivors, infected;
	float strength;
	if ( !G_InfectedActive() ) return;
	Infected_Count( &survivors, &infected );
	level.teamScores[TEAM_BLUE] = survivors;
	level.teamScores[TEAM_RED] = infected;
	strength = G_InfectedStrength( TEAM_RED );
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *client = &level.clients[i];
		if ( client->pers.connected != CON_CONNECTED || !G_IsActiveTeam( client->sess.sessionTeam ) ) continue;
		G_ScaleMaxHealth( &g_entities[i], client->sess.sessionTeam == TEAM_RED ? strength : 1.0f );
	}
}

static void Infected_Turn( gentity_t *ent, const char *reason ) {
	G_ShiftAllegiance( ent, TEAM_RED );
	G_InfectedUpdateBalance();
	G_LogPrintf( "Infected: client=%d reason=%s\n", ent->s.number, reason );
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		trap_SendServerCommand( ent->s.number, "cp \"YOU ARE INFECTED\n\"" );
	}
	CalculateRanks();
}

// Any survivor death during a round turns the victim, so suicide is no escape.
void G_InfectedDeath( gentity_t *victim ) {
	if ( !G_InfectedActive() || !level.infectedStarted || level.intermissionQueued ||
		level.intermissiontime || victim->client->sess.sessionTeam != TEAM_BLUE ) return;
	Infected_Turn( victim, "death" );
}

static void Infected_ChooseCarrier( void ) {
	int i, candidates[MAX_CLIENTS], count = 0;
	gentity_t *carrier;
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam == TEAM_BLUE &&
			( !level.campaign || ( g_entities[i].r.svFlags & SVF_BOT ) ) ) {
			candidates[count++] = i;
		}
	}
	if ( !count ) return;
	carrier = &g_entities[candidates[rand() % count]];
	Infected_Turn( carrier, "carrier" );
	if ( carrier->health > 0 ) {
		carrier->health = carrier->client->ps.stats[STAT_HEALTH] = carrier->client->ps.stats[STAT_MAX_HEALTH];
	}
	trap_SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " is the first infected.\n\"",
		carrier->client->pers.netname ) );
}

static void Infected_SetState( int survivors, int infected ) {
	trap_SetConfigstring( CS_MODE_STATE, va( "%i %i %i %i", level.infectedStarted,
		survivors, infected, level.infectedStarted ? level.infectedEndTime : level.infectedStartTime ) );
}

/*
Round flow: wait for at least two players and a settled roster, infect one at
random, then watch for the horde converting everyone or the clock running out.
*/
void G_InfectedCheckRules( void ) {
	int survivors, infected;
	if ( level.warmupTime || trap_Cvar_VariableIntegerValue( "g_menuBackground" ) ) return;
	Infected_Count( &survivors, &infected );

	if ( !level.infectedStarted ) {
#ifdef STANDALONE
		if ( G_ClassSelectionPending() ) {
			level.infectedStartTime = 0;
			return;
		}
#endif
		if ( !level.infectedStartTime || survivors != level.infectedPendingCount ) {
			level.infectedPendingCount = survivors;
			level.infectedStartTime = level.time + INFECTED_SETTLE_MSEC;
			Infected_SetState( survivors, infected );
			return;
		}
		if ( survivors < 2 || level.time < level.infectedStartTime ) return;
		level.infectedStarted = qtrue;
		G_CampaignStart();
		level.infectedEndTime = level.time + 60000 * ( level.campaign ? level.campaign->minutes :
			(int)Com_Clamp( 1, 60, g_infectedMinutes.integer ) );
		trap_SendServerCommand( -1, "cp \"INFECTION SPREADING\n\"" );
		G_LogPrintf( "Infected start: players=%d minutes=%d\n", survivors, ( level.infectedEndTime - level.time ) / 60000 );
		Infected_ChooseCarrier();
		Infected_Count( &survivors, &infected );
		Infected_SetState( survivors, infected );
		return;
	}

	if ( !survivors && !infected ) {
		// everyone left; start over when players return
		level.infectedStarted = qfalse;
		level.infectedStartTime = 0;
		return;
	}
	// The last carrier left; infect someone else so the round can go on.
	if ( !infected && survivors >= 2 ) {
		Infected_ChooseCarrier();
		Infected_Count( &survivors, &infected );
	}
	Infected_SetState( survivors, infected );
	if ( !survivors && infected ) {
		G_CampaignFinish( -1, TEAM_RED );
		trap_SendServerCommand( -1, "cp \"THE INFECTED WIN\n\"" );
		trap_SendServerCommand( -1, "print \"The infected converted everyone.\n\"" );
		LogExit( "Infected took everyone." );
	} else if ( survivors && level.time >= level.infectedEndTime ) {
		G_CampaignFinish( -1, TEAM_BLUE );
		trap_SendServerCommand( -1, va( "cp \"%i SURVIVOR%s WIN\n\"", survivors, survivors == 1 ? "" : "S" ) );
		trap_SendServerCommand( -1, "print \"The survivors outlasted the infection.\n\"" );
		LogExit( "Survivors lasted." );
	}
}
