/* FRAGNAROK tactical decisions. This interface is shared by C and C++. */
#ifndef AI_UTILITY_H
#define AI_UTILITY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	BOT_TACTIC_ATTACK,
	BOT_TACTIC_REPOSITION,
	BOT_TACTIC_RETREAT,
	BOT_TACTIC_COUNT
} bot_tactic_t;

typedef enum {
	BOT_STYLE_AGGRESSIVE,
	BOT_STYLE_CAUTIOUS,
	BOT_STYLE_SUPPORT,
	BOT_STYLE_COUNT
} bot_style_t;

typedef enum {
	BOT_RANGE_MELEE,
	BOT_RANGE_PISTOL,
	BOT_RANGE_RIFLE,
	BOT_RANGE_MACHINEGUN,
	BOT_RANGE_ROCKET,
	BOT_RANGE_GRENADE,
	BOT_RANGE_LIGHTNING,
	BOT_RANGE_RAIL,
	BOT_RANGE_BFG
} bot_range_t;

typedef struct {
	float time;
	float health; /* Fraction of this bot's maximum, including mode scaling. */
	float armor;
	float distance;
	float preferredDistance;
	float distanceBand;
	float damage;
	int enemies;
	int allies;
	int visible;
	int powered;
	int melee;
	int relentless;
	bot_style_t style;
} bot_utility_input_t;

typedef struct {
	int initialized;
	bot_tactic_t action;
	float since;
	float scores[BOT_TACTIC_COUNT];
	unsigned int decisions;
	unsigned int switches;
} bot_utility_state_t;

typedef struct {
	float forward, sideways, speed;
} bot_utility_motion_t;

void BotUtilitySelect(const bot_utility_input_t *input, bot_utility_state_t *state);
void BotUtilityMovement(const bot_utility_input_t *input, bot_tactic_t tactic,
	int client, bot_utility_motion_t *motion);
void BotUtilityRange(bot_range_t weapon, bot_style_t style, float *distance, float *band);
const char *BotUtilityActionName(bot_tactic_t action);
const char *BotUtilityStyleName(bot_style_t style);

#ifdef __cplusplus
}
#endif
#endif
