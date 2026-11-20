#ifndef BG_CAMPAIGN_H
#define BG_CAMPAIGN_H

typedef enum {
	CAMPAIGN_DM, CAMPAIGN_DG, CAMPAIGN_TDM, CAMPAIGN_KILLSHIFT,
	CAMPAIGN_LMS, CAMPAIGN_INFECTED, CAMPAIGN_BUGHUNT
} campaignMode_t;

typedef enum {
	CAMPAIGN_RUNNING, CAMPAIGN_WON, CAMPAIGN_LOST, CAMPAIGN_DRAW
} campaignResult_t;

typedef struct {
	const char *title;
	const char *map;
	campaignMode_t mode;
	int players, teams;
	int skill, allySkill;
	int frags, lives, minutes;
	const char *rival;
	qboolean mustSurvive;
} campaignMission_t;

#define CAMPAIGN_MISSION_COUNT 32

const campaignMission_t *BG_CampaignMission( int number );

#endif
