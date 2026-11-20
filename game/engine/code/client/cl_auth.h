/* FRAGNAROK online sign-in through Steam. Tickets and tokens never enter console
 * commands and are never saved to disk. */
#ifndef CL_AUTH_H
#define CL_AUTH_H

#include "../ui/ui_auth.h"

void CL_AuthInit( void );
void CL_AuthShutdown( void );
void CL_AuthFrame( void );
void CL_AuthGetStatus( uiAuthStatus_t *status );
qboolean CL_AuthRequireOnlineLogin( void );
/* Asks Steam for a fresh sign-in ticket now instead of at the next retry. */
void CL_AuthRetry( void );
void CL_AuthHistoryChallenge( const char *server, const char *run, int connection );
void CL_AuthHistoryDisconnect( void );
void CL_AuthPlayIdentity( char id[37], char username[25] );
void CL_AuthPlayFrame( void );
void CL_AuthCampaignStart( int mission, qboolean cheats );
void CL_AuthCampaignFinish( int mission, int elapsedMsec, int deaths );
void CL_AuthCampaignScore( uiCampaignScore_t *score );
void CL_AuthCampaignStanding( int mission, uiCampaignStanding_t *standing );

/* Transport runs on a worker thread and may not call engine APIs. */
int CL_AuthRequest( const char *host, const char *key, const char *path,
    const char *body, const char *bearer, char *response, size_t capacity );

#define AUTH_REFRESH_CAPACITY 2048

#endif
