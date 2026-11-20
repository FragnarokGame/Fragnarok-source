/* Steam sign-in. The Steam library is loaded at run time, so builds without it
 * (LAN tests, dedicated servers) still run; online play then needs Steam. */
#ifndef CL_STEAM_H
#define CL_STEAM_H

#define STEAM_TICKET_HEX	5121	/* a web API ticket is at most 2560 bytes */

void CL_SteamInit( void );
void CL_SteamShutdown( void );
void CL_SteamFrame( void );
qboolean CL_SteamAvailable( void );
const char *CL_SteamStatus( void );	/* why Steam is unavailable, for the Account screen */
qboolean CL_SteamRequestTicket( void );
qboolean CL_SteamTicketPending( void );
/* Returns qtrue once, with the hex ticket, after a request finishes successfully. */
qboolean CL_SteamTakeTicket( char *hex, int size );

#endif
