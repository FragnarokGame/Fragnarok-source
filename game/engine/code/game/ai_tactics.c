/* Tactical sensing and movement use the same botlib queries as the existing AI. */
#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_tactics.h"
#include "inv.h"

extern bot_state_t *botstates[MAX_CLIENTS];

static bot_range_t BotTacticsWeapon(int weapon) {
	switch (weapon) {
	case WP_GAUNTLET: return BOT_RANGE_MELEE;
	case WP_SHOTGUN: return BOT_RANGE_PISTOL; /* This slot is FRAGNAROK's pistol. */
	case WP_MACHINEGUN: return BOT_RANGE_RIFLE;
	case WP_PLASMAGUN: return BOT_RANGE_MACHINEGUN;
	case WP_GRENADE_LAUNCHER: return BOT_RANGE_GRENADE;
	case WP_ROCKET_LAUNCHER: return BOT_RANGE_ROCKET;
	case WP_LIGHTNING: return BOT_RANGE_LIGHTNING;
	case WP_RAILGUN: return BOT_RANGE_RAIL;
	case WP_BFG: return BOT_RANGE_BFG;
	default: return BOT_RANGE_RIFLE;
	}
}

void BotTacticsReset(bot_state_t *bs) {
	bs->tactics.initialized = 0;
	bs->tacticsNextThink = 0;
	bs->tacticsEnemy = -1;
}

bot_tactic_t BotTacticsUpdate(bot_state_t *bs) {
	bot_utility_input_t input;
	aas_entityinfo_t enemy;
	vec3_t delta, mins, maxs;
	static int candidates[MAX_GENTITIES];
	int count, i, sampled, maximum;
	float now = FloatTime();
	bot_style_t style = (bot_style_t)(bs->client % BOT_STYLE_COUNT);

	if (bs->enemy < 0 || BotIsDead(bs) || BotIsObserver(bs)) {
		BotTacticsReset(bs);
		return BOT_TACTIC_ATTACK;
	}
	if (bs->tacticsEnemy != bs->enemy) {
		BotTacticsReset(bs);
		bs->tacticsEnemy = bs->enemy;
	}
	if (bs->tactics.initialized && now < bs->tacticsNextThink &&
		bs->tacticsWeapon == bs->weaponnum &&
		bs->inventory[INVENTORY_HEALTH] >= bs->tacticsHealth - 10) {
		return bs->tactics.action;
	}
	BotEntityInfo(bs->enemy, &enemy);
	if (!enemy.valid || EntityIsDead(&enemy)) {
		BotTacticsReset(bs);
		return BOT_TACTIC_ATTACK;
	}
	memset(&input, 0, sizeof(input));
	input.time = now;
	input.style = style;
	maximum = bs->cur_ps.stats[STAT_MAX_HEALTH];
	if (maximum <= 0) maximum = 100;
	input.health = (float)bs->inventory[INVENTORY_HEALTH] / maximum;
	input.armor = (float)bs->inventory[INVENTORY_ARMOR] / maximum;
	input.damage = (float)(bs->lastframe_health - bs->inventory[INVENTORY_HEALTH]) / maximum;
	input.powered = bs->inventory[INVENTORY_QUAD] > 0;
	input.melee = bs->weaponnum == WP_GAUNTLET;
	input.relentless = g_infected.integer && BotTeam(bs) == TEAM_RED;
	input.visible = BotEntityVisible(bs->entitynum, bs->eye, bs->viewangles, 360, bs->enemy) > 0;
	VectorSubtract(input.visible ? enemy.origin : bs->lastenemyorigin, bs->origin, delta);
	input.distance = VectorLength(delta);
	BotUtilityRange(BotTacticsWeapon(bs->weaponnum), style,
		&input.preferredDistance, &input.distanceBand);
	bs->tacticsDistance = input.preferredDistance;
	bs->tacticsBand = input.distanceBand;
	input.enemies = input.visible ? 1 : 0;

	// Bound local awareness work even with hundreds of clients and spider NPCs.
	VectorSet(delta, 1200, 1200, 800);
	VectorSubtract(bs->origin, delta, mins);
	VectorAdd(bs->origin, delta, maxs);
	count = trap_EntitiesInBox(mins, maxs, candidates, ARRAY_LEN(candidates));
	for (i = 0, sampled = 0; i < count && sampled < 8; i++) {
		aas_entityinfo_t other;
		int entity = candidates[i];
		if (entity == bs->client || entity == bs->enemy || entity >= level.maxclients ||
			(g_entities[entity].flags & FL_NOTARGET)) continue;
		BotEntityInfo(entity, &other);
		if (!other.valid || EntityIsDead(&other) ||
			(EntityIsInvisible(&other) && !EntityIsShooting(&other))) continue;
		sampled++;
		if (BotEntityVisible(bs->entitynum, bs->eye, bs->viewangles, 160, entity) <= 0) continue;
		if (BotSameTeam(bs, entity)) input.allies++;
		else input.enemies++;
	}
	BotUtilitySelect(&input, &bs->tactics);
	bs->tacticsNextThink = now + 0.35f;
	bs->tacticsWeapon = bs->weaponnum;
	bs->tacticsHealth = bs->inventory[INVENTORY_HEALTH];
	return bs->tactics.action;
}

int BotTacticsMove(bot_state_t *bs) {
	aas_entityinfo_t enemy;
	bot_utility_input_t input;
	bot_utility_motion_t motion;
	bot_tactic_t tactic;
	vec3_t forward, side, move, up = {0, 0, 1};
	int i;
	tactic = BotTacticsUpdate(bs);
	BotEntityInfo(bs->enemy, &enemy);
	// A lost target is pursued through its last known AAS goal by the chase node.
	if (!BotEntityVisible(bs->entitynum, bs->eye, bs->viewangles, 360, bs->enemy)) return qfalse;
	VectorSubtract(enemy.origin, bs->origin, forward);
	forward[2] = 0;
	memset(&input, 0, sizeof(input));
	input.time = FloatTime();
	input.distance = VectorNormalize(forward);
	input.preferredDistance = bs->tacticsDistance;
	input.distanceBand = bs->tacticsBand;
	input.melee = bs->weaponnum == WP_GAUNTLET;
	BotUtilityMovement(&input, tactic, bs->client, &motion);
	if (motion.speed == 0) return qtrue;
	CrossProduct(forward, up, side);
	if (bs->flags & BFL_STRAFERIGHT) VectorNegate(side, side);
	for (i = 0; i < 2; i++) {
		VectorScale(forward, motion.forward, move);
		VectorMA(move, motion.sideways, side, move);
		VectorNormalize(move);
		if (trap_BotMoveInDirection(bs->ms, move, motion.speed, MOVE_WALK)) {
			if (i) bs->flags ^= BFL_STRAFERIGHT;
			return qtrue;
		}
		VectorNegate(side, side);
	}
	// A blocked range adjustment can sidestep an obstacle without constant dodging.
	if (motion.forward != 0 && !input.melee) {
		for (i = 0; i < 2; i++) {
			if (trap_BotMoveInDirection(bs->ms, side, 180, MOVE_WALK)) {
				if (i) bs->flags ^= BFL_STRAFERIGHT;
				return qtrue;
			}
			VectorNegate(side, side);
		}
	}
	return qtrue;
}

void BotTacticsReport(void) {
	int i, bots = 0;
	unsigned int decisions = 0, switches = 0;
	for (i = 0; i < level.maxclients; i++) {
		bot_state_t *bs = botstates[i];
		if (!bs || !bs->inuse) continue;
		bots++;
		decisions += bs->tactics.decisions;
		switches += bs->tactics.switches;
		G_Printf("Bot tactics: client=%d style=%s action=%s decisions=%u switches=%u range=%.0f scores=%.2f/%.2f/%.2f\n",
			i, BotUtilityStyleName((bot_style_t)(i % BOT_STYLE_COUNT)),
			bs->tactics.initialized ? BotUtilityActionName(bs->tactics.action) : "none",
			bs->tactics.decisions, bs->tactics.switches, bs->tacticsDistance,
			bs->tactics.scores[0], bs->tactics.scores[1], bs->tactics.scores[2]);
	}
	G_Printf("Bot tactics summary: backend=AI-Toolkit bots=%d decisions=%u switches=%u\n", bots, decisions, switches);
}
