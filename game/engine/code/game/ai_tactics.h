#ifndef AI_TACTICS_H
#define AI_TACTICS_H
#ifdef BOT_USE_AITOOLKIT
#include "ai_utility.h"
struct bot_state_s;
void BotTacticsReset(struct bot_state_s *bs);
bot_tactic_t BotTacticsUpdate(struct bot_state_s *bs);
int BotTacticsMove(struct bot_state_s *bs);
void BotTacticsReport(void);
#endif
#endif
