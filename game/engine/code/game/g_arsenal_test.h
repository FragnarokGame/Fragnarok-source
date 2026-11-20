// Cheat-only regression hooks run the same prediction, firing and collision
// paths as a real match. Included by g_cmds.c alongside its other test hooks.
#include "inv.h"
static int arsenalFailures;

static void ArsenalCheck( qboolean condition, const char *name ) {
	G_Printf( "arsenal check: %s %s\n", condition ? "PASS" : "FAIL", name );
	if ( !condition ) arsenalFailures++;
}

static void ArsenalMove( pmove_t *move, int msec, int buttons, int weapon ) {
	move->cmd.serverTime = move->ps->commandTime + msec;
	move->cmd.buttons = buttons;
	move->cmd.weapon = weapon;
	Pmove( move );
}

static void Cmd_ArsenalTest_f( gentity_t *ent ) {
	playerState_t ps;
	pmove_t move;
	int i, j, savedWeapon, savedState, total, savedTime = level.time;
	gentity_t *missile, *first = NULL, *testBot = NULL;
	vec3_t start, dir = {1,0,0};
	trace_t trace;
	char arg[16];
	if ( !CheatsOk(ent) ) return;
	trap_Argv( 1, arg, sizeof(arg) );
	if ( !strcmp(arg, "clear") ) { G_ClearSiegeTraps(ent); return; }
	arsenalFailures = 0;
	ArsenalCheck( BG_FindItemForWeapon(WP_FLAK) - bg_itemlist == MODELINDEX_FLAK &&
		BG_FindItemForAmmo(WP_FLAK) - bg_itemlist == MODELINDEX_FLAKAMMO, "flak bot item indexes match runtime" );
	memset( &move, 0, sizeof(move) );
	move.ps = &ps;
	move.trace = trap_Trace;
	move.pointcontents = trap_PointContents;
	move.tracemask = MASK_PLAYERSOLID;
	for ( i = 0; i < 10; i++ ) {
		int weapon = bg_weaponSlots[i];
		ps = ent->client->ps;
		ps.pm_type = PM_NORMAL;
		ps.pm_flags = 0;
		ps.stats[STAT_HEALTH] = 100;
		ps.stats[STAT_PLAYER_CLASS] = 0;
		ps.stats[STAT_RIFLE_TRIGGER_HELD] = ps.stats[STAT_RIFLE_TRIGGER_BUFFER] = 0;
		ps.weapon = weapon;
		ps.weaponstate = WEAPON_READY;
		ps.weaponTime = 0;
		ps.ammo[weapon] = weapon == WP_SHOTGUN || weapon == WP_GAUNTLET ? -1 : 10;
		move.gauntletHit = qtrue;
		ArsenalMove( &move, 100, BUTTON_ATTACK, weapon );
		ArsenalCheck( ps.ammo[weapon] == (weapon == WP_SHOTGUN || weapon == WP_GAUNTLET ? -1 : 9),
			va("slot %d primary fires with correct ammo", i) );
	}
	for ( i = 0; i < 2; i++ ) {
		int weapon = i ? WP_GRENADE_LAUNCHER : WP_LIGHTNING;
		ps = ent->client->ps;
		ps.pm_flags = 0;
		ps.stats[STAT_RIFLE_TRIGGER_HELD] = ps.stats[STAT_RIFLE_TRIGGER_BUFFER] = 0;
		ps.weapon = weapon; ps.weaponstate = WEAPON_READY; ps.weaponTime = 0;
		ps.ammo[weapon] = 20;
		ArsenalMove( &move, 40, BUTTON_SHIELD, weapon );
		ArsenalCheck( ps.ammo[weapon] == 19 &&
			ps.events[(ps.eventSequence - 1) & (MAX_PS_EVENTS - 1)] == EV_FIRE_WEAPON_ALT,
			i ? "Siege alternate predicts distinct fire event" : "Arc beam predicts distinct fire event" );
	}
	ps = ent->client->ps;
	ps.pm_flags = 0; ps.weapon = WP_BFG; ps.weaponstate = WEAPON_READY; ps.weaponTime = 0;
	ps.stats[STAT_RIFLE_TRIGGER_HELD] = ps.stats[STAT_RIFLE_TRIGGER_BUFFER] = 0;
	ps.ammo[WP_BFG] = 2;
	ArsenalMove( &move, 1, BUTTON_ATTACK, WP_BFG );
	ArsenalCheck( ps.ammo[WP_BFG] == 1 && ps.weaponstate == WEAPON_FIRING &&
		ps.events[(ps.eventSequence - 1) & (MAX_PS_EVENTS - 1)] == EV_FIRE_WEAPON,
		"Oden fires on the first input frame" );
	ArsenalMove( &move, 100, 0, WP_BFG );
	ArsenalMove( &move, 100, BUTTON_ATTACK, WP_BFG );
	ArsenalCheck( ps.ammo[WP_BFG] == 1 && ps.weaponTime > 0, "Oden respects shot recovery" );

	savedWeapon = ent->s.weapon;
	savedState = ent->client->ps.weapon;
	VectorCopy( ent->client->ps.origin, start ); start[2] += 26;
	ent->s.weapon = ent->client->ps.weapon = WP_FLAK;
	FireWeapon(ent);
	total = 0;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		missile = &g_entities[i];
		if ( missile->inuse && missile->parent == ent && !strcmp(missile->classname, "flak") ) {
			total++; first = missile;
		}
	}
	ArsenalCheck( total == FLAK_PELLETS, "flak fires eight physical fragments" );
	if ( first ) {
		memset( &trace, 0, sizeof(trace) );
		trace.entityNum = ENTITYNUM_WORLD;
		VectorCopy( first->r.currentOrigin, trace.endpos );
		VectorSet( trace.plane.normal, first->s.pos.trDelta[0] > 0 ? -1 : 1, 0, 0 );
		G_MissileImpact( first, &trace );
		ArsenalCheck( first->inuse && first->count == 1 && DotProduct(first->s.pos.trDelta, trace.plane.normal) > 0,
			"flak reflects off world geometry" );
		G_MissileImpact( first, &trace );
		ArsenalCheck( !first->inuse, "flak stops after its bank shot" );
	}
	ent->s.weapon = ent->client->ps.weapon = WP_LIGHTNING;
	FireWeapon(ent);
	first = NULL;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		missile = &g_entities[i];
		if ( missile->inuse && missile->parent == ent && !strcmp(missile->classname,"plasma") ) first = missile;
	}
	ArsenalCheck( first && first->s.weapon == WP_LIGHTNING &&
		fabs(VectorLength(first->s.pos.trDelta) - ARC_SPEED) < 2, "Arc primary has physical travel time" );

	G_ClearSiegeTraps(ent);
	ent->s.weapon = ent->client->ps.weapon = WP_GRENADE_LAUNCHER;
	for ( i = 0; i < SIEGE_TRAP_LIMIT + 1; i++ ) FireWeaponAlt(ent);
	total = 0; first = NULL;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		missile = &g_entities[i];
		if ( missile->inuse && missile->parent == ent && !strcmp(missile->classname,"siege_trap") ) { total++; first = missile; }
	}
	ArsenalCheck( total == SIEGE_TRAP_LIMIT, "sticky charges capped at three per owner" );
	if ( first ) {
		memset( &trace, 0, sizeof(trace) );
		trace.entityNum = ENTITYNUM_WORLD;
		VectorCopy( first->r.currentOrigin, trace.endpos );
		VectorSet( trace.plane.normal, 0,0,1 );
		G_MissileImpact( first, &trace );
		ArsenalCheck( first->s.pos.trType == TR_STATIONARY && first->wait == level.time + 600,
			"charge sticks and delays arming" );
		level.time = first->timestamp;
		first->think(first);
		ArsenalCheck( !first->inuse, "sticky charge expires after twelve seconds" );
		level.time = savedTime;
	}
	G_ClearSiegeTraps(ent);
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *target = &g_entities[i];
		if ( target->inuse && target->client && (target->r.svFlags & SVF_BOT) && target->health > 0 ) {
			int teams[MAX_CLIENTS];
			int gametype = g_gametype.integer, before = target->health;
			void (*scan)(gentity_t *);
			VectorCopy( target->r.currentOrigin, start ); start[2] += 40;
			missile = fire_grenade(ent, start, dir);
			G_MakeSiegeTrap(missile);
			G_SetOrigin(missile, start);
			missile->wait = level.time;
			scan = missile->think;
			for ( j = 0; j < level.maxclients; j++ ) {
				teams[j] = level.clients[j].sess.sessionTeam;
				level.clients[j].sess.sessionTeam = TEAM_RED;
			}
			g_gametype.integer = GT_TEAM;
			trap_LinkEntity(target);
			scan(missile);
			ArsenalCheck( missile->think == scan, "friendly players do not trigger traps" );
			target->client->sess.sessionTeam = TEAM_BLUE;
			scan(missile);
			ArsenalCheck( missile->think != scan && missile->nextthink == level.time + 250,
				"visible enemy triggers warning before explosion" );
			missile->think(missile);
			ArsenalCheck( target->health < before, "proximity explosion damages real bot" );
			g_gametype.integer = gametype;
			for ( j = 0; j < level.maxclients; j++ ) level.clients[j].sess.sessionTeam = teams[j];
			if ( missile->inuse ) G_FreeEntity(missile);
			testBot = target;
			break;
		}
	}
	ArsenalCheck( testBot != NULL, "real bot available for trap damage regression" );
	total = 0;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ )
		if ( g_entities[i].inuse && g_entities[i].parent == ent && !strcmp(g_entities[i].classname,"siege_trap") ) total++;
	ArsenalCheck( total == 0, "owner cleanup removes all traps" );
	// Sample the Test Chamber's south lane well beyond the previous blast radius.
	{
		gentity_t *inside = G_Spawn(), *outside = G_Spawn();
		vec3_t impact = {-550, -350, 50}, inner = {50, -350, 50}, outer = {550, -350, 50};

		inside->health = outside->health = 1000;
		inside->takedamage = outside->takedamage = qtrue;
		G_SetOrigin( inside, inner );
		G_SetOrigin( outside, outer );
		trap_LinkEntity( inside );
		trap_LinkEntity( outside );

		missile = fire_bfg( ent, impact, dir );
		ArsenalCheck( missile->damage == ODEN_DAMAGE && missile->splashDamage == ODEN_DAMAGE &&
			fabs(VectorLength(missile->s.pos.trDelta) - ODEN_SPEED) < 2, "Oden has slow travel and high damage" );
		memset( &trace, 0, sizeof(trace) );
		trace.entityNum = ENTITYNUM_WORLD;
		VectorCopy( impact, trace.endpos );
		VectorSet( trace.plane.normal, 0, 0, 1 );
		if ( testBot ) testBot->takedamage = qfalse;
		G_MissileImpact( missile, &trace );
		if ( testBot ) testBot->takedamage = qtrue;
		ArsenalCheck( inside->health >= 595 && inside->health <= 605 && outside->health == 1000,
			"Oden splash falls off across the arena and stops at the blast radius" );
		G_FreeEntity( missile );
		G_FreeEntity( inside );
		G_FreeEntity( outside );
	}
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		missile = &g_entities[i];
		if ( missile->inuse && missile->parent == ent && missile->s.eType == ET_MISSILE ) G_FreeEntity(missile);
	}
	ent->s.weapon = savedWeapon;
	ent->client->ps.weapon = savedState;
	G_Printf( "Arsenal mechanics: %s (%d failures)\n", arsenalFailures ? "FAIL" : "PASS", arsenalFailures );
}
