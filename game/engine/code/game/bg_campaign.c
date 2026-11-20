#include "../qcommon/q_shared.h"
#include "bg_campaign.h"

// Keep this ladder in sync with game/FRAGS_TO_RICHES.md. Team counts are
// zero in free-for-all modes. An ally skill of zero uses the regular skill.
static const campaignMission_t missions[] = {
	// Title, map, mode, players, teams, skill, ally skill, frags, lives, minutes, rival, must survive
	{ "First Paycheck",      "test_chamber", CAMPAIGN_DM,         2, 0, 1, 0,   5, 0, 0, "Grunt", qfalse },
	{ "Last Train",          "station",      CAMPAIGN_DM,         4, 0, 1, 0,   8, 0, 0, NULL   , qfalse },
	{ "Shore Leave",         "gallows",      CAMPAIGN_TDM,        8, 2, 1, 0,  20, 0, 0, NULL   , qfalse },
	{ "Growing Pains",       "rift_foundry", CAMPAIGN_DG,         5, 0, 1, 0,   8, 0, 0, NULL   , qfalse },
	{ "Borrowed Time",       "horologe",     CAMPAIGN_LMS,        4, 0, 2, 0,   0, 4, 0, NULL   , qfalse },
	{ "Change of Sides",     "station",      CAMPAIGN_KILLSHIFT,  6, 2, 2, 0,   0, 0, 0, NULL   , qfalse },
	{ "Foundry Crew",        "rift_foundry", CAMPAIGN_TDM,       10, 2, 2, 0,  30, 0, 0, NULL   , qfalse },
	{ "The Cut",             "gallows",      CAMPAIGN_DM,         8, 0, 2, 0,  15, 0, 0, NULL   , qfalse },

	{ "Heavy Industry",      "rift_foundry", CAMPAIGN_DG,         8, 0, 2, 0,  12, 0, 0, NULL   , qfalse },
	{ "Cannon Fodder",       "gallows",      CAMPAIGN_KILLSHIFT,  9, 3, 2, 0,   0, 0, 0, NULL   , qfalse },
	{ "Tower Teams",         "skyreach",     CAMPAIGN_TDM,       16, 2, 2, 0,  40, 0, 0, NULL   , qfalse },
	{ "Dead on the Hour",    "horologe",     CAMPAIGN_LMS,        6, 0, 3, 0,   0, 3, 0, NULL   , qfalse },
	{ "Night Service",       "station",      CAMPAIGN_INFECTED,   8, 2, 2, 0,   0, 0, 2, NULL   , qfalse },
	{ "Infestation",         "bastion",      CAMPAIGN_BUGHUNT,    8, 2, 2, 0,   0, 3, 4, NULL   , qfalse },
	{ "Clockwork Crossfire", "horologe",     CAMPAIGN_TDM,       12, 3, 3, 0,  40, 0, 0, NULL   , qfalse },
	{ "Meet Major",          "rift_foundry", CAMPAIGN_DM,         6, 0, 3, 0,  18, 0, 0, "Major", qfalse },

	{ "Platform Rush",       "station",      CAMPAIGN_DM,        12, 0, 3, 0,  20, 0, 0, NULL   , qfalse },
	{ "The Giant's Wake",    "gallows",      CAMPAIGN_DG,        10, 0, 3, 0,  15, 0, 0, NULL   , qfalse },
	{ "Three Banners",       "bastion",      CAMPAIGN_TDM,       24, 3, 3, 0,  60, 0, 0, NULL   , qfalse },
	{ "Two Chances",         "horologe",     CAMPAIGN_LMS,        8, 0, 3, 0,   0, 2, 0, NULL   , qfalse },
	{ "No Ship Out",         "gallows",      CAMPAIGN_INFECTED,  10, 2, 3, 0,   0, 0, 3, NULL   , qfalse },
	{ "Swarm Front",         "skyreach",     CAMPAIGN_BUGHUNT,   12, 3, 3, 0,   0, 2, 5, NULL   , qfalse },
	{ "Midnight Express",    "station",      CAMPAIGN_TDM,       16, 2, 4, 0,  50, 0, 0, NULL   , qfalse },
	{ "Four Flags Falling",  "skyreach",     CAMPAIGN_KILLSHIFT, 24, 4, 4, 0,   0, 0, 0, NULL   , qfalse },

	{ "House Advantage",     "rift_foundry", CAMPAIGN_DG,        12, 0, 4, 0,  18, 0, 0, NULL   , qfalse },
	{ "All Hands",           "gallows",      CAMPAIGN_DM,        16, 0, 4, 0,  25, 0, 0, NULL   , qfalse },
	{ "The Longest Night",   "horologe",     CAMPAIGN_INFECTED,  12, 2, 4, 0,   0, 0, 5, NULL   , qfalse },
	{ "One Life Left",       "station",      CAMPAIGN_LMS,        8, 0, 4, 0,   0, 1, 0, NULL   , qfalse },
	{ "Fall of Bastion",     "bastion",      CAMPAIGN_KILLSHIFT, 32, 4, 4, 0,   0, 0, 0, NULL   , qfalse },
	{ "War Above the Rift",  "skyreach",     CAMPAIGN_TDM,       48, 2, 5, 4, 100, 0, 0, NULL   , qfalse },
	{ "Hold the Keep",       "bastion",      CAMPAIGN_BUGHUNT,   24, 4, 4, 0,   0, 2, 6, NULL   , qtrue  },
	{ "The Crown",           "test_chamber", CAMPAIGN_DM,         2, 0, 5, 0,  10, 0, 0, "Major", qfalse },
};

const campaignMission_t *BG_CampaignMission( int number ) {
	return number >= 1 && number <= ARRAY_LEN( missions ) ? &missions[number - 1] : NULL;
}
