#ifndef UI_AUTH_H
#define UI_AUTH_H

typedef struct {
    int configured;
    int busy;
    int signedIn;
    int needsDisplayName;
    int remembered;
    char displayName[25];
    char email[256];
    char playerId[64];
    char message[160];
} uiAuthStatus_t;

typedef enum {
    CAMPAIGN_SCORE_GUEST, CAMPAIGN_SCORE_UNRANKED, CAMPAIGN_SCORE_PENDING,
    CAMPAIGN_SCORE_SAVED, CAMPAIGN_SCORE_UNAVAILABLE
} uiCampaignScoreState_t;

typedef struct {
    int mission, elapsedMsec, deaths;
    int score, bestScore, rank;
    uiCampaignScoreState_t state;
} uiCampaignScore_t;

typedef struct {
    int bestScore, rank;
} uiCampaignStanding_t;

#endif
