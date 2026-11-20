// AI Toolkit chooses tactics; the C game module executes them through botlib.
#include "ai_utility.h"
#include "../thirdparty/aitoolkit/include/aitoolkit/utility.hpp"
#include <algorithm>
#include <cmath>

namespace {
struct tactical_board {
	float scores[BOT_TACTIC_COUNT];
	bot_tactic_t selected = BOT_TACTIC_ATTACK;
};

template <bot_tactic_t Tactic>
class tactical_action final : public aitoolkit::utility::action<tactical_board> {
public:
	float score(const tactical_board& board) const override { return board.scores[Tactic]; }
	void apply(tactical_board& board) const override { board.selected = Tactic; }
};

float unit(float value) { return std::clamp(value, 0.0f, 1.0f); }
}

void BotUtilityRange(bot_range_t weapon, bot_style_t style, float *distance, float *band) {
	float range = 600.0f, tolerance = 140.0f;
	switch (weapon) {
	case BOT_RANGE_MELEE: range = 32.0f; tolerance = 16.0f; break;
	case BOT_RANGE_PISTOL: range = 380.0f; tolerance = 120.0f; break;
	case BOT_RANGE_RIFLE: range = 700.0f; tolerance = 180.0f; break;
	case BOT_RANGE_MACHINEGUN: range = 520.0f; tolerance = 160.0f; break;
	case BOT_RANGE_ROCKET: range = 600.0f; tolerance = 160.0f; break;
	case BOT_RANGE_GRENADE: range = 480.0f; tolerance = 140.0f; break;
	case BOT_RANGE_LIGHTNING: range = 400.0f; tolerance = 100.0f; break;
	case BOT_RANGE_RAIL: range = 1100.0f; tolerance = 260.0f; break;
	case BOT_RANGE_BFG: range = 650.0f; tolerance = 180.0f; break;
	}
	if (weapon != BOT_RANGE_MELEE) {
		if (style == BOT_STYLE_AGGRESSIVE) range *= 0.8f;
		if (style == BOT_STYLE_CAUTIOUS) range *= 1.2f;
	}
	*distance = range;
	*band = tolerance;
}

void BotUtilitySelect(const bot_utility_input_t *input, bot_utility_state_t *state) {
	// Construct the three shared actions once, with no allocation per bot or tick.
	static const aitoolkit::utility::evaluator<tactical_board> evaluator(
		aitoolkit::utility::action_list<tactical_board>(
			tactical_action<BOT_TACTIC_ATTACK>{},
			tactical_action<BOT_TACTIC_REPOSITION>{},
			tactical_action<BOT_TACTIC_RETREAT>{}));
	const auto& in = *input;
	tactical_board board;
	const float health = unit(in.health);
	const float protection = unit(health + unit(in.armor) * 0.35f);
	const float danger = unit((in.enemies - in.allies - 1) * 0.25f);
	const float error = unit((std::fabs(in.distance - in.preferredDistance) - in.distanceBand)
		/ std::max(in.preferredDistance, 160.0f));
	const float aggression = in.style == BOT_STYLE_AGGRESSIVE ? 0.14f :
		(in.style == BOT_STYLE_CAUTIOUS ? -0.12f : 0.0f);
	const float support = in.style == BOT_STYLE_SUPPORT ?
		(in.allies > 0 ? 0.12f : -0.08f) : 0.0f;
	const float injury = 1.0f - protection;

	board.scores[BOT_TACTIC_ATTACK] = std::max(0.01f,
		0.48f + 0.3f * protection + aggression + support + (in.powered ? 0.25f : 0.0f)
		- 0.3f * error - 0.3f * danger - 0.2f * unit(in.damage));
	board.scores[BOT_TACTIC_REPOSITION] = std::max(0.01f,
		0.28f + 0.65f * error + 0.12f * danger + (in.style == BOT_STYLE_CAUTIOUS ? 0.08f : 0.0f));
	board.scores[BOT_TACTIC_RETREAT] = std::max(0.01f,
		0.08f + 0.95f * injury * injury + 0.4f * danger + 0.25f * unit(in.damage)
		- aggression - support * 0.5f - (in.powered ? 0.25f : 0.0f));
	// Losing sight is an opportunity to disengage, rather than circling a wall.
	if (!in.visible) board.scores[BOT_TACTIC_REPOSITION] = 0.01f;
	if (in.melee) board.scores[BOT_TACTIC_REPOSITION] = 0.01f;
	if (in.relentless) {
		board.scores[BOT_TACTIC_ATTACK] += 0.5f;
		board.scores[BOT_TACTIC_RETREAT] = 0.01f;
	}
	for (int i = 0; i < BOT_TACTIC_COUNT; ++i) state->scores[i] = board.scores[i];
	const bool emergency = health < 0.2f && !in.relentless;
	const bool valid = state->initialized && state->action >= BOT_TACTIC_ATTACK
		&& state->action < BOT_TACTIC_COUNT && in.time >= state->since;
	if (valid && !emergency && !(in.relentless && state->action == BOT_TACTIC_RETREAT)) {
		// A small advantage must persist before abandoning an ongoing maneuver.
		if (in.time - state->since < 0.75f) {
			++state->decisions;
			return;
		}
		board.scores[state->action] += 0.14f;
	}
	if (emergency) board.scores[BOT_TACTIC_RETREAT] += 1.0f;
	evaluator.run(board);
	++state->decisions;
	if (!valid || board.selected != state->action) {
		if (valid) ++state->switches;
		state->action = board.selected;
		state->since = in.time;
	}
	state->initialized = 1;
}

void BotUtilityMovement(const bot_utility_input_t *input, bot_tactic_t tactic,
	int client, bot_utility_motion_t *motion) {
	*motion = {};
	if (input->melee) {
		if (input->distance > 24.0f) {
			motion->forward = 1.0f;
			motion->speed = 400.0f;
		}
		return;
	}
	if (tactic == BOT_TACTIC_RETREAT) motion->forward = -1.0f;
	else if (input->distance > input->preferredDistance + input->distanceBand) motion->forward = 1.0f;
	else if (input->distance < input->preferredDistance - input->distanceBand) motion->forward = -1.0f;

	// Settle into firing range. Brief, slower sidesteps leave readable aim windows.
	const float phase = std::fmod(input->time + client * 0.37f, 3.2f);
	const bool sidestep = phase < 0.6f;
	if (motion->forward != 0.0f) {
		motion->sideways = sidestep ? 0.25f : 0.0f;
		motion->speed = 400.0f;
	} else if (tactic == BOT_TACTIC_ATTACK && sidestep) {
		motion->sideways = 1.0f;
		motion->speed = 180.0f;
	}
}

const char *BotUtilityActionName(bot_tactic_t action) {
	switch (action) {
	case BOT_TACTIC_ATTACK: return "attack";
	case BOT_TACTIC_REPOSITION: return "reposition";
	case BOT_TACTIC_RETREAT: return "retreat";
	default: return "none";
	}
}

const char *BotUtilityStyleName(bot_style_t style) {
	switch (style) {
	case BOT_STYLE_AGGRESSIVE: return "aggressive";
	case BOT_STYLE_CAUTIOUS: return "cautious";
	case BOT_STYLE_SUPPORT: return "support";
	default: return "unknown";
	}
}
