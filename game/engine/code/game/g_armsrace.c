/*
===========================================================================
Arms Race: a free-for-all where everyone carries the same single weapon. The
map has no weapon or ammo pickups and nobody drops any. Every 30 to 60 seconds
the game draws a new weapon at random and swaps it into every player's hands.
Ammo never runs out, since there is nothing else to fall back on. Odin's
Sneeze is in the draw but comes up far less often than the other weapons, and
it alone is rationed: two charges, one coming back every ten seconds.
===========================================================================
*/

#include "g_local.h"

#define ARMSRACE_MIN_MSEC	30000
#define ARMSRACE_MAX_MSEC	60000
#define ARMSRACE_ODIN_CHARGES		2
#define ARMSRACE_ODIN_RECHARGE_MSEC	10000

typedef struct {
	int weapon;
	int weight;	// share of the draw
} armsRaceWeapon_t;

// Odin's Sneeze gets a quarter of an ordinary weapon's share. Weapons this
// build holds back (BG_WeaponEnabled) are skipped by the draw, so the odds
// are shares of the weapons that exist.
static const armsRaceWeapon_t armsRaceWeapons[] = {
	{ WP_SHOTGUN, 4 },			// pistol
	{ WP_MACHINEGUN, 4 },		// rifle
	{ WP_ROCKET_LAUNCHER, 4 },
	{ WP_PLASMAGUN, 4 },
	{ WP_FLAK, 4 },
	{ WP_LIGHTNING, 4 },
	{ WP_RAILGUN, 4 },
	{ WP_GRENADE_LAUNCHER, 4 },
	{ WP_BFG, 1 },
};

qboolean G_ArmsRaceActive( void ) {
	return g_gametype.integer == GT_FFA && g_armsRace.integer && !g_lastManStanding.integer &&
		!g_leprechaun.integer;
}

static qboolean ArmsRace_InDraw( int weapon, int current ) {
	return weapon != current && BG_WeaponEnabled( weapon );
}

// roll is any non-negative number. The weapon in use is left out of the draw
// so every change is a real change.
int G_ArmsRacePick( int current, int roll ) {
	int i, total = 0;

	for ( i = 0; i < (int)ARRAY_LEN( armsRaceWeapons ); i++ ) {
		if ( ArmsRace_InDraw( armsRaceWeapons[i].weapon, current ) ) total += armsRaceWeapons[i].weight;
	}
	roll %= total;
	for ( i = 0; i < (int)ARRAY_LEN( armsRaceWeapons ); i++ ) {
		if ( !ArmsRace_InDraw( armsRaceWeapons[i].weapon, current ) ) continue;
		roll -= armsRaceWeapons[i].weight;
		if ( roll < 0 ) return armsRaceWeapons[i].weapon;
	}
	return WP_MACHINEGUN;
}

// The pistol keeps its usual endless magazine; the rest are topped up each
// frame, as with infinite ammo, because bots read -1 as an empty gun.
// Odin's Sneeze is the exception: it holds its usual two charges and wins one
// back every ten seconds, so nobody is left unarmed but nobody can spam it.
void G_ArmsRaceRefill( gclient_t *client ) {
	if ( level.armsRaceWeapon == WP_BFG ) {
		if ( client->ps.ammo[WP_BFG] >= ARMSRACE_ODIN_CHARGES ) {
			client->armsRaceRechargeTime = level.time + ARMSRACE_ODIN_RECHARGE_MSEC;
		} else if ( level.time >= client->armsRaceRechargeTime ) {
			client->ps.ammo[WP_BFG]++;
			client->armsRaceRechargeTime = level.time + ARMSRACE_ODIN_RECHARGE_MSEC;
		}
		return;
	}
	client->ps.ammo[level.armsRaceWeapon] = level.armsRaceWeapon == WP_SHOTGUN ? -1 : 999;
}

void G_ArmsRaceEquip( gclient_t *client ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( armsRaceWeapons ); i++ ) {
		client->ps.ammo[armsRaceWeapons[i].weapon] = 0;
	}
	client->ps.stats[STAT_WEAPONS] = 1 << level.armsRaceWeapon;
	client->ps.weapon = level.armsRaceWeapon;
	// A switch in progress would finish on the weapon that was just taken away.
	client->ps.weaponstate = WEAPON_READY;
	if ( level.armsRaceWeapon == WP_BFG ) client->ps.ammo[WP_BFG] = ARMSRACE_ODIN_CHARGES;
	G_ArmsRaceRefill( client );
}

static void ArmsRace_Announce( int clientNum ) {
	trap_SendServerCommand( clientNum, va( "armsrace %d", level.armsRaceWeapon ) );
}

void G_ArmsRaceSet( int weapon, int msec ) {
	int i;

	level.armsRaceWeapon = weapon;
	level.armsRaceNextTime = level.time + msec;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ||
			ent->client->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0 ) continue;
		G_ArmsRaceEquip( ent->client );
	}
	ArmsRace_Announce( -1 );
	G_LogPrintf( "ArmsRace: weapon=%d name=%s seconds=%d\n", weapon,
		BG_FindItemForWeapon( weapon )->pickup_name, msec / 1000 );
}

static int ArmsRace_Duration( void ) {
	return ARMSRACE_MIN_MSEC + rand() % ( ARMSRACE_MAX_MSEC - ARMSRACE_MIN_MSEC + 1 );
}

// Called once the level is cleared, before any player spawns, so the first
// spawn already carries the opening weapon.
void G_ArmsRaceInit( void ) {
	if ( !G_ArmsRaceActive() ) return;
	level.armsRaceWeapon = G_ArmsRacePick( WP_NONE, rand() );
	level.armsRaceNextTime = level.time + ArmsRace_Duration();
	G_Printf( "Arms Race: opening weapon %s\n", BG_FindItemForWeapon( level.armsRaceWeapon )->pickup_name );
}

// Tells a player who has just entered the game what the round's weapon is.
void G_ArmsRaceClientBegin( gentity_t *ent ) {
	if ( !G_ArmsRaceActive() || ( ent->r.svFlags & SVF_BOT ) ) return;
	ArmsRace_Announce( ent - g_entities );
}

void G_ArmsRaceFrame( void ) {
	if ( !G_ArmsRaceActive() || level.intermissiontime || level.intermissionQueued ) return;
	if ( level.time >= level.armsRaceNextTime ) {
		G_ArmsRaceSet( G_ArmsRacePick( level.armsRaceWeapon, rand() ), ArmsRace_Duration() );
	}
}
