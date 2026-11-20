#include "g_local.h"

void G_CampaignInit( void ) {
	const campaignMission_t *mission;
	char map[MAX_QPATH];
	int number;

	if ( g_dedicated.integer ) return;
	number = trap_Cvar_VariableIntegerValue( "g_campaignMission" );
	mission = BG_CampaignMission( number );
	if ( !mission ) return;
	trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );
	if ( Q_stricmp( map, mission->map ) ) return;

	level.campaign = mission;
	level.campaignNumber = number;
	trap_Cvar_Set( "g_campaignResult", "0" );
}

void G_CampaignStart( void ) {
	if ( !level.campaign || !level.campaignRosterReady || level.campaignStartTime ) return;
	level.campaignStartTime = level.time;
	level.campaignStartingDeaths = level.clients[level.campaignPlayer].ps.persistant[PERS_KILLED];
}

// Receive the winner from the mode itself. A team win can count after the
// player is eliminated in Bug Hunt, while Infected requires them to stay blue.
void G_CampaignFinish( int winner, team_t team ) {
	const campaignMission_t *mission = level.campaign;
	gclient_t *player;
	campaignResult_t result;

	if ( !mission || !level.campaignRosterReady || level.campaignResult != CAMPAIGN_RUNNING ) return;
	player = &level.clients[level.campaignPlayer];
	if ( player->pers.connected != CON_CONNECTED || !player->pers.localClient ) return;

	if ( winner < 0 && team == TEAM_FREE ) {
		result = CAMPAIGN_DRAW;
	} else if ( winner == level.campaignPlayer ||
		( G_IsActiveTeam( team ) && team == level.campaignStartingTeam &&
		  ( mission->mode != CAMPAIGN_INFECTED || player->sess.sessionTeam == TEAM_BLUE ) &&
		  ( !mission->mustSurvive || player->pers.lmsLives > 0 ) ) ) {
		result = CAMPAIGN_WON;
	} else {
		result = CAMPAIGN_LOST;
	}

	level.campaignResult = result;
	if ( result == CAMPAIGN_WON && level.campaignStartTime ) {
		int elapsed = level.time - level.campaignStartTime;

		// Use the prescribed duration so the server tick cannot break a tie.
		if ( mission->mode == CAMPAIGN_INFECTED || ( mission->mode == CAMPAIGN_BUGHUNT &&
			mission->minutes && level.time - level.lmsStartTime >= mission->minutes * 60000 ) ) {
			elapsed = mission->minutes * 60000;
		}
		trap_Print( va( "FRAGCAMPAIGN %d %d %d\n", level.campaignNumber, MAX( 1, elapsed ),
			MAX( 0, player->ps.persistant[PERS_KILLED] - level.campaignStartingDeaths ) ) );
	}
	if ( result == CAMPAIGN_WON &&
		level.campaignNumber - 1 == MAX( 0, trap_Cvar_VariableIntegerValue( "ui_campaignCleared" ) ) ) {
		trap_Cvar_Set( "ui_campaignCleared", va( "%d", level.campaignNumber ) );
	}
	trap_Cvar_Set( "g_campaignResult", va( "%d", result ) );
	G_LogPrintf( "Campaign result: mission=%d result=%d\n", level.campaignNumber, result );
}
