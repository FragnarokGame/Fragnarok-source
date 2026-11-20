/* FRAGNAROK Discord activity, using the desktop client's local RPC socket. */
#ifndef CL_DISCORD_H
#define CL_DISCORD_H

/* Public application ID from the Discord Developer Portal, never a secret. */
#define FRAGNAROK_DISCORD_APPLICATION_ID "1566550378328367194"

void CL_DiscordInit( void );
void CL_DiscordFrame( void );
void CL_DiscordShutdown( void );

#endif
