/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "server.h"

#ifdef USE_VOIP
cvar_t *sv_voip;
cvar_t *sv_voipProtocol;
cvar_t *sv_voipRange;
#endif

serverStatic_t	svs;				// persistant server info
server_t		sv;					// local server
vm_t			*gvm = NULL;				// game virtual machine

cvar_t	*sv_fps = NULL;			// time rate for running non-clients
cvar_t	*sv_timeout;			// seconds without any message
cvar_t	*sv_zombietime;			// seconds to sink messages after disconnect
cvar_t	*sv_rconPassword;		// password for remote server commands
cvar_t	*sv_privatePassword;		// password for the privateClient slots
cvar_t	*sv_allowDownload;
cvar_t	*sv_maxclients;

cvar_t	*sv_privateClients;		// number of clients reserved for password
cvar_t	*sv_hostname;
cvar_t	*sv_master[MAX_MASTER_SERVERS];	// master server ip address
cvar_t	*sv_reconnectlimit;		// minimum seconds between connect messages
cvar_t	*sv_showloss;			// report when usercmds are lost
cvar_t	*sv_padPackets;			// add nop bytes to messages
cvar_t	*sv_killserver;			// menu system can set to 1 to shut server down
cvar_t	*sv_mapname;
cvar_t	*sv_mapChecksum;
cvar_t	*sv_serverid;
cvar_t	*sv_minRate;
cvar_t	*sv_maxRate;
cvar_t	*sv_dlRate;
cvar_t	*sv_minPing;
cvar_t	*sv_maxPing;
cvar_t	*sv_gametype;
cvar_t	*sv_pure;
cvar_t	*sv_floodProtect;
cvar_t	*sv_lanForceRate; // dedicated 1 (LAN) server forces local client rates to 99999 (bug #491)
cvar_t	*sv_frameStats;		// print a frame timing and snapshot summary every N seconds
cvar_t	*sv_lanHost;		// listen server answers LAN queries and accepts remote players
cvar_t	*sv_onlineHost;		// listen server advertises in the public directory
static netadr_t sv_masterAddresses[MAX_MASTER_SERVERS][2];
#ifdef STANDALONE
static int sv_directoryHeartbeatTime, sv_directoryAckTime;
#endif
#ifndef STANDALONE
cvar_t	*sv_strictAuth;
#endif
cvar_t	*sv_banFile;

serverBan_t serverBans[SERVER_MAXBANS];
int serverBansCount = 0;

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
static char	*SV_ExpandNewlines( char *in ) {
	static	char	string[1024];
	int		l;

	l = 0;
	while ( *in && l < sizeof(string) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			string[l++] = *in;
		}
		in++;
	}
	string[l] = 0;

	return string;
}

/*
======================
SV_ReplacePendingServerCommands

FIXME: This is ugly
======================
*/
#if 0 // unused
static int SV_ReplacePendingServerCommands( client_t *client, const char *cmd ) {
	int i, index, csnum1, csnum2;

	for ( i = client->reliableSent+1; i <= client->reliableSequence; i++ ) {
		index = i & ( MAX_RELIABLE_COMMANDS - 1 );
		//
		if ( !Q_strncmp(cmd, client->reliableCommands[ index ], strlen("cs")) ) {
			sscanf(cmd, "cs %i", &csnum1);
			sscanf(client->reliableCommands[ index ], "cs %i", &csnum2);
			if ( csnum1 == csnum2 ) {
				Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
				/*
				if ( client->netchan.remoteAddress.type != NA_BOT ) {
					Com_Printf( "WARNING: client %i removed double pending config string %i: %s\n", client-svs.clients, csnum1, cmd );
				}
				*/
				return qtrue;
			}
		}
	}
	return qfalse;
}
#endif

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index, i;

	// Local bot clients run entirely inside the server and never acknowledge
	// network snapshots or reliable commands. Queuing commands for them only
	// consumes memory and eventually overflows during large bot populations.
	if ( client->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}

	// this is very ugly but it's also a waste to for instance send multiple config string updates
	// for the same config string index in one snapshot
//	if ( SV_ReplacePendingServerCommands( client, cmd ) ) {
//		return;
//	}

	// do not send commands until the gamestate has been sent
	if( client->state < CS_PRIMED )
		return;

	// A client far behind on acknowledgements is close to being dropped for
	// overflow. Console notifications ("X entered the game" for hundreds of
	// players after a map change) are the one kind of command worth losing
	// to keep the connection; chat, configstrings and game commands still queue.
	if ( client->reliableSequence - client->reliableAcknowledge >= MAX_RELIABLE_COMMANDS * 3 / 4 &&
		!Q_strncmp( cmd, "print ", 6 ) ) {
		return;
	}

	// "loaddefered" carries no arguments and is sent once per joining player;
	// one unacknowledged copy already covers any burst of joins.
	if ( !Q_strncmp( cmd, "loaddefered", 11 ) ) {
		for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
			if ( !strcmp( client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ], cmd ) ) {
				return;
			}
		}
	}

	client->reliableSequence++;
	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient()
	// doesn't cause a recursive drop client
	if ( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 ) {
		if ( client->gamestateMessageNum == -1 )  {
			// invalid game state message 
			// this can occur in SV_DropClient() to avoid calling it more than once
			return;
		}

		Com_Printf( "===== pending server commands =====\n" );
		for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
			Com_Printf( "cmd %5d: %s\n", i, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
		Com_Printf( "cmd %5d: %s\n", i, cmd );
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
}


/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by 
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients
=================
*/
void QDECL SV_SendServerCommand(client_t *cl, const char *fmt, ...) {
	va_list		argptr;
	byte		message[MAX_MSGLEN];
	client_t	*client;
	int			j;
	
	va_start (argptr,fmt);
	Q_vsnprintf ((char *)message, sizeof(message), fmt,argptr);
	va_end (argptr);

	// Fix to http://aluigi.altervista.org/adv/q3msgboom-adv.txt
	// The actual cause of the bug is probably further downstream
	// and should maybe be addressed later, but this certainly
	// fixes the problem for now
	if ( strlen ((char *)message) > 1022 ) {
		return;
	}

	if ( cl != NULL ) {
		SV_AddServerCommand( cl, (char *)message );
		return;
	}

	// hack to echo broadcast prints to console
	if ( com_dedicated->integer && !strncmp( (char *)message, "print", 5) ) {
		Com_Printf ("broadcast: %s\n", SV_ExpandNewlines((char *)message) );
	}

	// send the data to all relevant clients
	for (j = 0, client = svs.clients; j < sv_maxclients->integer ; j++, client++) {
		SV_AddServerCommand( client, (char *)message );
	}
}


/*
==============================================================================

MASTER SERVER FUNCTIONS

==============================================================================
*/

/*
================
SV_MasterHeartbeat

Send a message to the masters every few minutes to
let it know we are alive, and log information.
We will also have a heartbeat sent when a server
changes from empty to non-empty, and full to non-full,
but not on every player enter or exit.
================
*/
#ifdef STANDALONE
#define HEARTBEAT_MSEC	60000
#else
#define HEARTBEAT_MSEC	300*1000
#endif
#define	MASTERDNS_MSEC	24*60*60*1000
void SV_MasterHeartbeat(const char *message)
{
	netadr_t (*adr)[2] = sv_masterAddresses;
	int			i;
	int			res;
	int			netenabled;

	netenabled = Cvar_VariableIntegerValue("net_enabled");

	if (!com_dedicated || !(netenabled & (NET_ENABLEV4 | NET_ENABLEV6)))
		return;
	if ( com_dedicated->integer != 2 &&
		( !sv_onlineHost->integer || Cvar_VariableIntegerValue( "g_menuBackground" ) ) ) {
#ifdef STANDALONE
		Cvar_Set( "sv_onlineStatus", "idle" );
		Cvar_Set( "sv_onlineAddress", "" );
		sv_directoryHeartbeatTime = sv_directoryAckTime = 0;
#endif
		return;
	}
	if ( sv_onlineHost->modified ) {
		sv_onlineHost->modified = qfalse;
		svs.nextHeartbeatTime = -9999;
	}
#ifdef STANDALONE
	if ( sv_directoryHeartbeatTime && svs.time - sv_directoryHeartbeatTime > 10000 &&
		sv_directoryAckTime < sv_directoryHeartbeatTime ) {
		Cvar_Set( "sv_onlineStatus", "unreachable" );
	}
#endif

	// if not time yet, don't send anything
	if ( svs.time < svs.nextHeartbeatTime )
		return;

	if ( !Q_stricmp( com_gamename->string, LEGACY_MASTER_GAMENAME ) )
		message = LEGACY_HEARTBEAT_FOR_MASTER;

	svs.nextHeartbeatTime = svs.time + HEARTBEAT_MSEC;

	// send to group masters
	for (i = 0; i < MAX_MASTER_SERVERS; i++)
	{
		if(!sv_master[i]->string[0])
			continue;

		// see if we haven't already resolved the name or if it's been over 24 hours
		// resolving usually causes hitches on win95, so only do it when needed
		if (sv_master[i]->modified || svs.time > svs.masterResolveTime[i])
		{
			sv_master[i]->modified = qfalse;
			svs.masterResolveTime[i] = svs.time + MASTERDNS_MSEC;
			
			if(netenabled & NET_ENABLEV4)
			{
				Com_Printf("Resolving %s (IPv4)\n", sv_master[i]->string);
				res = NET_StringToAdr(sv_master[i]->string, &adr[i][0], NA_IP);

				if(res == 2)
				{
					// if no port was specified, use the default master port
					adr[i][0].port = BigShort(PORT_MASTER);
				}
				
				if(res)
					Com_Printf( "%s resolved to %s\n", sv_master[i]->string, NET_AdrToStringwPort(adr[i][0]));
				else
					Com_Printf( "%s has no IPv4 address.\n", sv_master[i]->string);
			}
			
			if(netenabled & NET_ENABLEV6)
			{
				Com_Printf("Resolving %s (IPv6)\n", sv_master[i]->string);
				res = NET_StringToAdr(sv_master[i]->string, &adr[i][1], NA_IP6);

				if(res == 2)
				{
					// if no port was specified, use the default master port
					adr[i][1].port = BigShort(PORT_MASTER);
				}
				
				if(res)
					Com_Printf( "%s resolved to %s\n", sv_master[i]->string, NET_AdrToStringwPort(adr[i][1]));
				else
					Com_Printf( "%s has no IPv6 address.\n", sv_master[i]->string);
			}
		}

		if(adr[i][0].type == NA_BAD && adr[i][1].type == NA_BAD)
		{
			continue;
		}


		Com_Printf ("Sending heartbeat to %s\n", sv_master[i]->string );
#ifdef STANDALONE
		if ( i == 0 ) {
			sv_directoryHeartbeatTime = svs.time;
			if ( Q_stricmp( Cvar_VariableString( "sv_onlineStatus" ), "listed" ) )
				Cvar_Set( "sv_onlineStatus", "checking" );
		}
#endif

		// this command should be changed if the server info / status format
		// ever incompatably changes

		if(adr[i][0].type != NA_BAD)
			NET_OutOfBandPrint( NS_SERVER, adr[i][0], "heartbeat %s\n", message);
		if(adr[i][1].type != NA_BAD)
			NET_OutOfBandPrint( NS_SERVER, adr[i][1], "heartbeat %s\n", message);
	}
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
void SV_MasterShutdown( void ) {
	// send a heartbeat right now
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat(HEARTBEAT_FOR_MASTER);

	// send it again to minimize chance of drops
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat(HEARTBEAT_FOR_MASTER);

	// when the master tries to poll the server, it won't respond, so
	// it will be removed from the list
#ifdef STANDALONE
	sv_directoryHeartbeatTime = sv_directoryAckTime = 0;
	Cvar_Set( "sv_onlineStatus", "idle" );
	Cvar_Set( "sv_onlineAddress", "" );
#endif
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

// This is deliberately quite large to make it more of an effort to DoS
#define MAX_BUCKETS			16384
#define MAX_HASHES			1024

static leakyBucket_t buckets[ MAX_BUCKETS ];
static leakyBucket_t *bucketHashes[ MAX_HASHES ];
leakyBucket_t outboundLeakyBucket;

/*
================
SVC_HashForAddress
================
*/
static long SVC_HashForAddress( netadr_t address ) {
	byte 		*ip = NULL;
	size_t	size = 0;
	int			i;
	long		hash = 0;

	switch ( address.type ) {
		case NA_IP:  ip = address.ip;  size = 4; break;
		case NA_IP6: ip = address.ip6; size = 16; break;
		default: break;
	}

	for ( i = 0; i < size; i++ ) {
		hash += (long)( ip[ i ] ) * ( i + 119 );
	}

	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	hash &= ( MAX_HASHES - 1 );

	return hash;
}

/*
================
SVC_BucketForAddress

Find or allocate a bucket for an address
================
*/
static leakyBucket_t *SVC_BucketForAddress( netadr_t address, int burst, int period ) {
	leakyBucket_t	*bucket = NULL;
	int						i;
	long					hash = SVC_HashForAddress( address );
	int						now = Sys_Milliseconds();

	for ( bucket = bucketHashes[ hash ]; bucket; bucket = bucket->next ) {
		switch ( bucket->type ) {
			case NA_IP:
				if ( memcmp( bucket->ipv._4, address.ip, 4 ) == 0 ) {
					return bucket;
				}
				break;

			case NA_IP6:
				if ( memcmp( bucket->ipv._6, address.ip6, 16 ) == 0 ) {
					return bucket;
				}
				break;

			default:
				break;
		}
	}

	for ( i = 0; i < MAX_BUCKETS; i++ ) {
		int interval;

		bucket = &buckets[ i ];
		interval = now - bucket->lastTime;

		// Reclaim expired buckets
		if ( bucket->lastTime > 0 && ( interval > ( burst * period ) ||
					interval < 0 ) ) {
			if ( bucket->prev != NULL ) {
				bucket->prev->next = bucket->next;
			} else {
				bucketHashes[ bucket->hash ] = bucket->next;
			}
			
			if ( bucket->next != NULL ) {
				bucket->next->prev = bucket->prev;
			}

			Com_Memset( bucket, 0, sizeof( leakyBucket_t ) );
		}

		if ( bucket->type == NA_BAD ) {
			bucket->type = address.type;
			switch ( address.type ) {
				case NA_IP:  Com_Memcpy( bucket->ipv._4, address.ip, 4 );   break;
				case NA_IP6: Com_Memcpy( bucket->ipv._6, address.ip6, 16 ); break;
				default: break;
			}

			bucket->lastTime = now;
			bucket->burst = 0;
			bucket->hash = hash;

			// Add to the head of the relevant hash chain
			bucket->next = bucketHashes[ hash ];
			if ( bucketHashes[ hash ] != NULL ) {
				bucketHashes[ hash ]->prev = bucket;
			}

			bucket->prev = NULL;
			bucketHashes[ hash ] = bucket;

			return bucket;
		}
	}

	// Couldn't allocate a bucket for this address
	return NULL;
}

/*
================
SVC_RateLimit
================
*/
qboolean SVC_RateLimit( leakyBucket_t *bucket, int burst, int period ) {
	if ( bucket != NULL ) {
		int now = Sys_Milliseconds();
		int interval = now - bucket->lastTime;
		int expired = interval / period;
		int expiredRemainder = interval % period;

		if ( expired > bucket->burst || interval < 0 ) {
			bucket->burst = 0;
			bucket->lastTime = now;
		} else {
			bucket->burst -= expired;
			bucket->lastTime = now - expiredRemainder;
		}

		if ( bucket->burst < burst ) {
			bucket->burst++;

			return qfalse;
		}
	}

	return qtrue;
}

/*
================
SVC_RateLimitAddress

Rate limit for a particular address
================
*/
qboolean SVC_RateLimitAddress( netadr_t from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	return SVC_RateLimit( bucket, burst, period );
}

/*
================
SV_AcceptsRemoteClients

A listen server stays private unless the host opened the match to the LAN.
The live map behind the main menu is never shared. Dedicated servers and the
local client are always accepted.
================
*/
qboolean SV_AcceptsRemoteClients( netadr_t from ) {
	if ( com_dedicated->integer || NET_IsLocalAddress( from ) ) {
		return qtrue;
	}
	if ( Cvar_VariableIntegerValue( "g_menuBackground" ) ) {
		return qfalse;
	}
	return sv_lanHost->integer != 0 || sv_onlineHost->integer != 0;
}

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
static void SVC_Status( netadr_t from ) {
	char	player[1024];
	char	status[16384];
	int		i;
	client_t	*cl;
	playerState_t	*ps;
	int		statusLength;
	int		playerLength;
	char	infostring[MAX_INFO_STRING];
	qboolean truncated = qfalse;

	// ignore if we are in single player
	if ( Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableValue("ui_singlePlayerActive")) {
		return;
	}
	if ( !SV_AcceptsRemoteClients( from ) ) {
		return;
	}

	// Prevent using getstatus as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		Com_DPrintf( "SVC_Status: rate limit from %s exceeded, dropping request\n",
			NET_AdrToString( from ) );
		return;
	}

	// Allow getstatus to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) ) {
		Com_DPrintf( "SVC_Status: rate limit exceeded, dropping request\n" );
		return;
	}

	// A maximum challenge length of 128 should be more than plenty.
	if(strlen(Cmd_Argv(1)) > 128)
		return;

	strcpy( infostring, Cvar_InfoString( CVAR_SERVERINFO ) );

	// echo back the parameter to status. so master servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	status[0] = 0;
	statusLength = 0;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED && !atoi( Info_ValueForKey( cl->userinfo, "menuwatch" ) ) ) {
			ps = SV_GameClientNum( i );
			Com_sprintf (player, sizeof(player), "%i %i \"%s\"\n", 
				ps->persistant[PERS_SCORE], cl->ping, cl->name);
			playerLength = strlen(player);
			if (statusLength + playerLength >= sizeof(status) ) {
				truncated = qtrue;
				break;		// can't hold any more
			}
			strcpy (status + statusLength, player);
			statusLength += playerLength;
		}
	}
	if ( truncated ) {
		Info_SetValueForKey( infostring, "status_truncated", "1" );
	}

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}

#ifdef STANDALONE
#define WEB_STATUS_PAGE_SIZE 64
#define WEB_STATUS_RESPONSE_SIZE 32768

typedef struct {
	int clientNum;
	int score;
	int deaths;
	int health;
	int ping;
	qboolean spectator;
	qboolean bot;
	qboolean companion;
	char name[MAX_NAME_LENGTH];
} webStatusPlayer_t;

static qboolean SVC_IsLoopbackAddress( netadr_t from ) {
	int i;

	if ( from.type == NA_LOOPBACK ) {
		return qtrue;
	}
	if ( from.type == NA_IP ) {
		return from.ip[0] == 127;
	}
	if ( from.type == NA_IP6 ) {
		for ( i = 0; i < 15; i++ ) {
			if ( from.ip6[i] != 0 ) {
				return qfalse;
			}
		}
		return from.ip6[15] == 1;
	}
	return qfalse;
}

static void SVC_AppendJsonString( char *dest, int destSize, const char *value ) {
	char escaped[3];
	unsigned char c;

	Q_strcat( dest, destSize, "\"" );
	while ( *value ) {
		c = (unsigned char)*value++;
		if ( c < 32 ) {
			continue;
		}
		if ( c == '\"' || c == '\\' ) {
			escaped[0] = '\\';
			escaped[1] = (char)c;
			escaped[2] = '\0';
		} else {
			escaped[0] = (char)c;
			escaped[1] = '\0';
		}
		Q_strcat( dest, destSize, escaped );
	}
	Q_strcat( dest, destSize, "\"" );
}

static int QDECL SVC_CompareWebPlayers( const void *left, const void *right ) {
	const webStatusPlayer_t *a = left;
	const webStatusPlayer_t *b = right;

	if ( a->spectator != b->spectator ) {
		return a->spectator ? 1 : -1;
	}
	if ( a->score != b->score ) {
		return b->score - a->score;
	}
	if ( a->deaths != b->deaths ) {
		return a->deaths - b->deaths;
	}
	return a->clientNum - b->clientNum;
}

/*
================
SVC_WebStatus

Returns one score-sorted JSON roster page to a dashboard service running on
the same host. Keeping this loopback-only avoids creating a UDP amplifier and
keeps browser-facing HTTP concerns out of the engine.
================
*/
static void SVC_WebStatus( netadr_t from ) {
	webStatusPlayer_t players[MAX_CLIENTS];
	webStatusPlayer_t *player;
	client_t *cl;
	playerState_t *ps;
	char response[WEB_STATUS_RESPONSE_SIZE];
	char entry[512];
	char cleanName[MAX_NAME_LENGTH];
	int page;
	int first;
	int last;
	int i;
	int total = 0;
	int humans = 0;
	int bots = 0;
	int companions = 0;
	int active = 0;
	int spectators = 0;

	if ( !Cvar_VariableIntegerValue( "sv_webStatus" ) || !SVC_IsLoopbackAddress( from ) ) {
		return;
	}

	page = atoi( Cmd_Argv( 1 ) );
	if ( page < 0 ) {
		page = 0;
	}

	for ( i = 0; i < sv_maxclients->integer; i++ ) {
		cl = &svs.clients[i];
		if ( cl->state < CS_CONNECTED || atoi( Info_ValueForKey( cl->userinfo, "menuwatch" ) ) ) {
			continue;
		}

		ps = SV_GameClientNum( i );
		player = &players[total++];
		player->clientNum = i;
		player->score = ps->persistant[PERS_SCORE];
		player->deaths = ps->persistant[PERS_KILLED];
		player->health = ps->stats[STAT_HEALTH];
		player->ping = cl->ping;
		player->spectator = ps->persistant[PERS_TEAM] == TEAM_SPECTATOR;
		player->bot = cl->netchan.remoteAddress.type == NA_BOT;
		player->companion = player->bot && atoi( Info_ValueForKey( cl->userinfo, "companion" ) );
		Q_strncpyz( cleanName, cl->name, sizeof( cleanName ) );
		Q_CleanStr( cleanName );
		Q_strncpyz( player->name, cleanName[0] ? cleanName : "Unnamed", sizeof( player->name ) );

		if ( player->bot ) {
			bots++;
		} else {
			humans++;
		}
		if ( player->companion ) {
			companions++;
		}
		if ( player->spectator ) {
			spectators++;
		} else {
			active++;
		}
	}

	qsort( players, total, sizeof( players[0] ), SVC_CompareWebPlayers );
	first = page * WEB_STATUS_PAGE_SIZE;
	if ( first > total ) {
		first = total;
	}
	last = first + WEB_STATUS_PAGE_SIZE;
	if ( last > total ) {
		last = total;
	}

	Com_sprintf( response, sizeof( response ),
		"{\"server\":{\"hostname\":" );
	SVC_AppendJsonString( response, sizeof( response ), sv_hostname->string );
	Q_strcat( response, sizeof( response ), ",\"map\":" );
	SVC_AppendJsonString( response, sizeof( response ), sv_mapname->string );
	Com_sprintf( entry, sizeof( entry ),
		",\"generation\":%d,\"ready\":%d,\"capacity\":%d,\"elapsedSeconds\":%d,"
		"\"g_gametype\":%d,\"g_davidGoliath\":%d,\"g_killshift\":%d,"
		"\"g_lastManStanding\":%d,\"g_infected\":%d,\"g_bugHunt\":%d,\"g_leprechaun\":%d,\"g_armsRace\":%d",
		Cvar_VariableIntegerValue( "g_matchGeneration" ), Cvar_VariableIntegerValue( "g_matchReady" ),
		Cvar_VariableIntegerValue( "g_targetPopulation" ), Cvar_VariableIntegerValue( "g_matchElapsed" ),
		sv_gametype->integer, Cvar_VariableIntegerValue( "g_davidGoliath" ),
		Cvar_VariableIntegerValue( "g_killshift" ), Cvar_VariableIntegerValue( "g_lastManStanding" ),
		Cvar_VariableIntegerValue( "g_infected" ), Cvar_VariableIntegerValue( "g_bugHunt" ),
		Cvar_VariableIntegerValue( "g_leprechaun" ), Cvar_VariableIntegerValue( "g_armsRace" ) );
	Q_strcat( response, sizeof( response ), entry );
	Com_sprintf( entry, sizeof( entry ),
		",\"protocol\":%d,\"uptimeSeconds\":%d,\"maxClients\":%d},"
		"\"counts\":{\"total\":%d,\"humans\":%d,\"bots\":%d,\"companions\":%d,"
		"\"active\":%d,\"spectators\":%d},\"page\":%d,\"pageSize\":%d,"
		"\"pageCount\":%d,\"players\":[",
		com_protocol->integer, svs.time / 1000, sv_maxclients->integer,
		total, humans, bots, companions, active, spectators, page,
		WEB_STATUS_PAGE_SIZE, ( total + WEB_STATUS_PAGE_SIZE - 1 ) / WEB_STATUS_PAGE_SIZE );
	Q_strcat( response, sizeof( response ), entry );

	for ( i = first; i < last; i++ ) {
		player = &players[i];
		if ( i > first ) {
			Q_strcat( response, sizeof( response ), "," );
		}
		Com_sprintf( entry, sizeof( entry ),
			"{\"slot\":%d,\"rank\":%d,\"name\":",
			player->clientNum, i + 1 );
		Q_strcat( response, sizeof( response ), entry );
		SVC_AppendJsonString( response, sizeof( response ), player->name );
		// Tickets and network addresses are available only to the local match
		// coordinator. The HTTP dashboard strips these before publishing rosters.
		cl = &svs.clients[player->clientNum];
		if ( Cvar_VariableIntegerValue( "sv_matchManaged" ) && !player->bot ) {
			Q_strcat( response, sizeof( response ), ",\"ticket\":" );
			SVC_AppendJsonString( response, sizeof( response ), cl->matchTicket );
			Q_strcat( response, sizeof( response ), ",\"address\":" );
			SVC_AppendJsonString( response, sizeof( response ), NET_AdrToString( cl->netchan.remoteAddress ) );
			Com_sprintf( entry, sizeof( entry ), ",\"preference\":%d,\"connected\":%s",
				atoi( Info_ValueForKey( cl->userinfo, "matchPreference" ) ), cl->state == CS_ACTIVE ? "true" : "false" );
			Q_strcat( response, sizeof( response ), entry );
		}
		Com_sprintf( entry, sizeof( entry ),
			",\"score\":%d,\"deaths\":%d,\"health\":%d,\"ping\":%d,"
			"\"bot\":%s,\"companion\":%s,\"spectator\":%s}",
			player->score, player->deaths, player->health, player->ping,
			player->bot ? "true" : "false",
			player->companion ? "true" : "false",
			player->spectator ? "true" : "false" );
		Q_strcat( response, sizeof( response ), entry );
	}
	Q_strcat( response, sizeof( response ), "]}" );

	NET_OutOfBandPrint( NS_SERVER, from, "webStatusResponse\n%s", response );
}
#endif

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
void SVC_Info( netadr_t from ) {
	int		i, count, humans;
	char	*gamedir;
	char	infostring[MAX_INFO_STRING];

	// ignore if we are in single player
	if ( Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableValue("ui_singlePlayerActive")) {
		return;
	}
	if ( !SV_AcceptsRemoteClients( from ) ) {
		return;
	}

	// Prevent using getinfo as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		Com_DPrintf( "SVC_Info: rate limit from %s exceeded, dropping request\n",
			NET_AdrToString( from ) );
		return;
	}

	// Allow getinfo to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) ) {
		Com_DPrintf( "SVC_Info: rate limit exceeded, dropping request\n" );
		return;
	}

	/*
	 * Check whether Cmd_Argv(1) has a sane length. This was not done in the original Quake3 version which led
	 * to the Infostring bug discovered by Luigi Auriemma. See http://aluigi.altervista.org/ for the advisory.
	 */

	// A maximum challenge length of 128 should be more than plenty.
	if(strlen(Cmd_Argv(1)) > 128)
		return;

	// don't count privateclients
	count = humans = 0;
	for ( i = sv_privateClients->integer ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED &&
			!atoi( Info_ValueForKey( svs.clients[i].userinfo, "menuwatch" ) ) ) {
			count++;
			if (svs.clients[i].netchan.remoteAddress.type != NA_BOT) {
				humans++;
			}
		}
	}

	infostring[0] = 0;

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	Info_SetValueForKey( infostring, "gamename", com_gamename->string );
#ifdef STANDALONE
	Info_SetValueForKey( infostring, "sv_public", com_dedicated->integer == 2 ||
		( sv_onlineHost->integer && !Cvar_VariableIntegerValue( "g_menuBackground" ) ) ? "1" : "0" );
#endif

#ifdef LEGACY_PROTOCOL
	if(com_legacyprotocol->integer > 0)
		Info_SetValueForKey(infostring, "protocol", va("%i", com_legacyprotocol->integer));
	else
#endif
		Info_SetValueForKey(infostring, "protocol", va("%i", com_protocol->integer));

	Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va("%i", count) );
	Info_SetValueForKey(infostring, "g_humanplayers", va("%i", humans));
	Info_SetValueForKey( infostring, "sv_maxclients", 
		va("%i", sv_maxclients->integer - sv_privateClients->integer ) );
	Info_SetValueForKey( infostring, "gametype", va("%i", sv_gametype->integer ) );
	Info_SetValueForKey( infostring, "g_killshift", Cvar_VariableString( "g_killshift" ) );
	Info_SetValueForKey( infostring, "g_leprechaun", Cvar_VariableString( "g_leprechaun" ) );
	Info_SetValueForKey( infostring, "g_armsRace", Cvar_VariableString( "g_armsRace" ) );
	Info_SetValueForKey( infostring, "g_lastManStanding", Cvar_VariableString( "g_lastManStanding" ) );
	Info_SetValueForKey( infostring, "g_infected", Cvar_VariableString( "g_infected" ) );
	Info_SetValueForKey( infostring, "g_bugHunt", Cvar_VariableString( "g_bugHunt" ) );
	Info_SetValueForKey( infostring, "g_davidGoliath", Cvar_VariableString( "g_davidGoliath" ) );
	Info_SetValueForKey( infostring, "match_lobby", Cvar_VariableString( "sv_matchLobbyAddress" ) );
	Info_SetValueForKey( infostring, "match_slot", Cvar_VariableString( "sv_matchSlot" ) );
#ifdef STANDALONE
	{
		int freeSlots = 0, playing = 0;
		for ( i = sv_privateClients->integer; i < sv_maxclients->integer; i++ ) {
			client_t *client = &svs.clients[i];
			if ( client->state == CS_FREE ) freeSlots++;
			if ( client->state == CS_ACTIVE &&
				!atoi( Info_ValueForKey( client->userinfo, "menuwatch" ) ) &&
				SV_GameClientNum( i )->persistant[PERS_TEAM] != TEAM_SPECTATOR ) playing++;
		}
		Info_SetValueForKey( infostring, "g_menuCamera", Cvar_VariableString( "g_menuCamera" ) );
		Info_SetValueForKey( infostring, "menu_freeSlots", va( "%i", freeSlots ) );
		Info_SetValueForKey( infostring, "menu_players", va( "%i", playing ) );
	}
#endif
	Info_SetValueForKey( infostring, "pure", va("%i", sv_pure->integer ) );
	Info_SetValueForKey(infostring, "g_needpass", va("%d", Cvar_VariableIntegerValue("g_needpass")));

#ifdef USE_VOIP
	if (sv_voipProtocol->string && *sv_voipProtocol->string) {
		Info_SetValueForKey( infostring, "voip", sv_voipProtocol->string );
	}
#endif

	if( sv_minPing->integer ) {
		Info_SetValueForKey( infostring, "minPing", va("%i", sv_minPing->integer) );
	}
	if( sv_maxPing->integer ) {
		Info_SetValueForKey( infostring, "maxPing", va("%i", sv_maxPing->integer) );
	}
	gamedir = Cvar_VariableString( "fs_game" );
	if( *gamedir ) {
		Info_SetValueForKey( infostring, "game", gamedir );
	}

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}

/*
================
SVC_FlushRedirect

================
*/
static void SV_FlushRedirect( char *outputbuf ) {
	NET_OutOfBandPrint( NS_SERVER, svs.redirectAddress, "print\n%s", outputbuf );
}

/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
static void SVC_RemoteCommand( netadr_t from, msg_t *msg ) {
	qboolean	valid;
	char		remaining[1024];
	// TTimo - scaled down to accumulate, but not overflow anything network wise, print wise etc.
	// (OOB messages are the bottleneck here)
#define SV_OUTPUTBUF_LENGTH (1024 - 16)
	char		sv_outputbuf[SV_OUTPUTBUF_LENGTH];
	char *cmd_aux;

	// Prevent using rcon as an amplifier and make dictionary attacks impractical
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		Com_DPrintf( "SVC_RemoteCommand: rate limit from %s exceeded, dropping request\n",
			NET_AdrToString( from ) );
		return;
	}

	if ( !strlen( sv_rconPassword->string ) ||
		strcmp (Cmd_Argv(1), sv_rconPassword->string) ) {
		static leakyBucket_t bucket;

		// Make DoS via rcon impractical
		if ( SVC_RateLimit( &bucket, 10, 1000 ) ) {
			Com_DPrintf( "SVC_RemoteCommand: rate limit exceeded, dropping request\n" );
			return;
		}

		valid = qfalse;
		Com_Printf ("Bad rcon from %s: %s\n", NET_AdrToString (from), Cmd_ArgsFrom(2) );
	} else {
		valid = qtrue;
		Com_Printf ("Rcon from %s: %s\n", NET_AdrToString (from), Cmd_ArgsFrom(2) );
	}

	// start redirecting all print outputs to the packet
	svs.redirectAddress = from;
	Com_BeginRedirect (sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect);

	if ( !strlen( sv_rconPassword->string ) ) {
		Com_Printf ("No rconpassword set on the server.\n");
	} else if ( !valid ) {
		Com_Printf ("Bad rconpassword.\n");
	} else {
		remaining[0] = 0;
		
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
		// get the command directly, "rcon <pass> <command>" to avoid quoting issues
		// extract the command by walking
		// since the cmd formatting can fuckup (amount of spaces), using a dumb step by step parsing
		cmd_aux = Cmd_Cmd();
		cmd_aux+=4;
		while(cmd_aux[0]==' ')
			cmd_aux++;
		while(cmd_aux[0] && cmd_aux[0]!=' ') // password
			cmd_aux++;
		while(cmd_aux[0]==' ')
			cmd_aux++;
		
		Q_strcat( remaining, sizeof(remaining), cmd_aux);
		
		Cmd_ExecuteString (remaining);

	}

	Com_EndRedirect ();
}

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );		// skip the -1 marker

	if (!Q_strncmp("connect", (char *) &msg->data[4], 7)) {
		Huff_Decompress(msg, 12);
	}

	s = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);
	Com_DPrintf ("SV packet %s : %s\n", NET_AdrToString(from), c);

	if (!Q_stricmp(c, "getstatus")) {
		SVC_Status( from );
#ifdef STANDALONE
	} else if (!Q_stricmp(c, "matchcontrol")) {
		if ( SVC_IsLoopbackAddress( from ) && Cvar_VariableIntegerValue( "sv_matchManaged" ) &&
			Cvar_VariableString( "sv_matchKey" )[0] &&
			!strcmp( Cmd_Argv( 1 ), Cvar_VariableString( "sv_matchKey" ) ) &&
			strlen( Cmd_Argv( 2 ) ) <= 32 ) {
			char request[33];
			Q_strncpyz( request, Cmd_Argv( 2 ), sizeof( request ) );
			NET_OutOfBandPrint( NS_SERVER, from, "matchControlResponse %s %s", request, SV_MatchControl() );
		}
	} else if (!Q_stricmp(c, "getwebstatus")) {
		SVC_WebStatus( from );
	} else if (!Q_stricmp(c, "fragnarokRegistered")) {
		if ( ( com_dedicated->integer == 2 || sv_onlineHost->integer ) &&
			( NET_CompareAdr( from, sv_masterAddresses[0][0] ) ||
			  NET_CompareAdr( from, sv_masterAddresses[0][1] ) ) ) {
			netadr_t address;
			if ( NET_StringToAdr( Cmd_Argv(1), &address, NA_IP ) == 1 ) {
				sv_directoryAckTime = svs.time;
				Cvar_Set( "sv_onlineStatus", "listed" );
				Cvar_Set( "sv_onlineAddress", NET_AdrToStringwPort( address ) );
				Com_Printf( "Online directory: listed at %s\n", NET_AdrToStringwPort( address ) );
			}
		}
#endif
  } else if (!Q_stricmp(c, "getinfo")) {
		SVC_Info( from );
	} else if (!Q_stricmp(c, "getchallenge")) {
		SV_GetChallenge(from);
	} else if (!Q_stricmp(c, "connect")) {
		SV_DirectConnect( from );
#ifndef STANDALONE
	} else if (!Q_stricmp(c, "ipAuthorize")) {
		SV_AuthorizeIpPacket( from );
#endif
	} else if (!Q_stricmp(c, "rcon")) {
		SVC_RemoteCommand( from, msg );
	} else if (!Q_stricmp(c, "disconnect")) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	} else {
		Com_DPrintf ("bad connectionless packet from %s:\n%s\n",
			NET_AdrToString (from), s);
	}
}

//============================================================================

/*
=================
SV_PacketEvent
=================
*/
void SV_PacketEvent( netadr_t from, msg_t *msg ) {
	int			i;
	client_t	*cl;
	int			qport;

	// check for connectionless packet (0xffffffff) first
	if ( msg->cursize >= 4 && *(int *)msg->data == -1) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );				// sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for (i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if (cl->netchan.remoteAddress.port != from.port) {
			Com_Printf( "SV_PacketEvent: fixing up a translated port\n" );
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if (SV_Netchan_Process(cl, msg)) {
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = svs.time;	// don't timeout
				SV_ExecuteClientMessage( cl, msg );
			}
		}
		return;
	}
}


/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
static void SV_CalcPings( void ) {
	int			i, j;
	client_t	*cl;
	int			total, count;
	int			delta;
	playerState_t	*ps;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state != CS_ACTIVE ) {
			cl->ping = 999;
			continue;
		}
		if ( !cl->gentity ) {
			cl->ping = 999;
			continue;
		}
		if ( cl->gentity->r.svFlags & SVF_BOT ) {
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			if ( cl->frames[j].messageAcked <= 0 ) {
				continue;
			}
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			count++;
			total += delta;
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
		}

		// let the game dll know about the ping
		ps = SV_GameClientNum( i );
		ps->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer 
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
static void SV_CheckTimeouts( void ) {
	int		i;
	client_t	*cl;
	int			droppoint;
	int			zombiepoint;

	droppoint = svs.time - 1000 * sv_timeout->integer;
	zombiepoint = svs.time - 1000 * sv_zombietime->integer;

	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		// message times may be wrong across a changelevel
		if (cl->lastPacketTime > svs.time) {
			cl->lastPacketTime = svs.time;
		}

		if (cl->state == CS_ZOMBIE
		&& cl->lastPacketTime < zombiepoint) {
			// using the client id cause the cl->name is empty at this point
			Com_DPrintf( "Going from CS_ZOMBIE to CS_FREE for client %d\n", i );
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime < droppoint) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient (cl, "timed out"); 
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
}


/*
==================
SV_CheckPaused
==================
*/
static qboolean SV_CheckPaused( void ) {
	int		count;
	client_t	*cl;
	int		i;

	// A host's pause menu must not stop an advertised match or its heartbeat.
	if ( sv_lanHost->integer || sv_onlineHost->integer ) {
		if ( sv_paused->integer ) Cvar_Set( "sv_paused", "0" );
		return qfalse;
	}

	if ( !cl_paused->integer ) {
		return qfalse;
	}

	// only pause if there is just a single client connected
	count = 0;
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if ( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type != NA_BOT ) {
			count++;
		}
	}

	if ( count > 1 ) {
		// don't pause
		if (sv_paused->integer)
			Cvar_Set("sv_paused", "0");
		return qfalse;
	}

	if (!sv_paused->integer)
		Cvar_Set("sv_paused", "1");
	return qtrue;
}

/*
==================
SV_FrameMsec
Return time in millseconds until processing of the next server frame.
==================
*/
int SV_FrameMsec(void)
{
	if(sv_fps)
	{
		int frameMsec;
		
		frameMsec = 1000.0f / sv_fps->value;
		
		if(frameMsec < sv.timeResidual)
			return 0;
		else
			return frameMsec - sv.timeResidual;
	}
	else
		return 1;
}

/*
==================
Frame statistics

sv_frameStats N prints a summary every N seconds: how long server frames
take (bot AI, game simulation and snapshot sending, in milliseconds with
microsecond precision) and how snapshots to human clients fared, including
how many were held back by rate limits or split into several packets.
Percentiles show the spikes that players feel as stutter.
==================
*/
#define FRAMESTATS_MAX_SAMPLES	4096

typedef struct {
	int		windowStart;
	int		samples;
	int		ticks;
	int		overBudget;
	int		total[FRAMESTATS_MAX_SAMPLES];
	int		bots[FRAMESTATS_MAX_SAMPLES];
	int		game[FRAMESTATS_MAX_SAMPLES];
	int		send[FRAMESTATS_MAX_SAMPLES];
	int		snapshots;
	int		snapshotBytes;
	int		maxSnapshotBytes;
	int		fragmented;
	int		rateDelayed;
	int		queueDelayed;
} frameStats_t;

static frameStats_t	svFrameStats;

static int SV_FrameStatsCompare( const void *a, const void *b ) {
	return *(const int *)a - *(const int *)b;
}

// Sorts the samples in place and returns "avg p50 p99 max" in milliseconds.
static const char *SV_FrameStatsSummary( int *values, int count ) {
	int		i;
	int64_t	sum = 0;

	if ( !count ) {
		return "-";
	}
	for ( i = 0; i < count; i++ ) {
		sum += values[i];
	}
	qsort( values, count, sizeof( int ), SV_FrameStatsCompare );
	return va( "avg %.2f p50 %.2f p99 %.2f max %.2f", sum / (double)count / 1000.0,
		values[count / 2] / 1000.0, values[( count * 99 ) / 100] / 1000.0,
		values[count - 1] / 1000.0 );
}

static void SV_FrameStatsRecord( int ticks, int bots, int game, int send, int total, int frameMsec ) {
	frameStats_t	*fs = &svFrameStats;

	// Calls that ran no game tick only poll the network; leave them out so
	// they do not dilute the per-frame figures.
	if ( !ticks || fs->samples >= FRAMESTATS_MAX_SAMPLES ) {
		return;
	}
	fs->total[fs->samples] = total;
	fs->bots[fs->samples] = bots;
	fs->game[fs->samples] = game;
	fs->send[fs->samples] = send;
	fs->samples++;
	fs->ticks += ticks;
	if ( total > frameMsec * 1000 ) {
		fs->overBudget++;
	}
}

void SV_FrameStatsSnapshot( int bytes ) {
	frameStats_t	*fs = &svFrameStats;

	if ( sv_frameStats->integer <= 0 ) {
		return;
	}
	fs->snapshots++;
	fs->snapshotBytes += bytes;
	if ( bytes > fs->maxSnapshotBytes ) {
		fs->maxSnapshotBytes = bytes;
	}
	if ( bytes >= FRAGMENT_SIZE ) {
		fs->fragmented++;
	}
}

void SV_FrameStatsDelayed( qboolean queued ) {
	if ( sv_frameStats->integer <= 0 ) {
		return;
	}
	if ( queued ) {
		svFrameStats.queueDelayed++;
	} else {
		svFrameStats.rateDelayed++;
	}
}

static void SV_FrameStatsFlush( void ) {
	frameStats_t	*fs = &svFrameStats;
	int				now = Sys_Milliseconds();
	int				humans = 0;
	int				i;

	if ( sv_frameStats->integer <= 0 ) {
		fs->windowStart = 0;
		return;
	}
	if ( !fs->windowStart ) {
		Com_Memset( fs, 0, sizeof( *fs ) );
		fs->windowStart = now;
		return;
	}
	if ( now - fs->windowStart < sv_frameStats->integer * 1000 ) {
		return;
	}

	for ( i = 0; i < sv_maxclients->integer; i++ ) {
		if ( svs.clients[i].state == CS_ACTIVE &&
		     svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
			humans++;
		}
	}

	Com_Printf( "frameStats: %.1fs frames %d ticks %d overBudget %d\n",
		( now - fs->windowStart ) / 1000.0, fs->samples, fs->ticks, fs->overBudget );
	Com_Printf( "frameStats total: %s\n", SV_FrameStatsSummary( fs->total, fs->samples ) );
	Com_Printf( "frameStats bots: %s\n", SV_FrameStatsSummary( fs->bots, fs->samples ) );
	Com_Printf( "frameStats game: %s\n", SV_FrameStatsSummary( fs->game, fs->samples ) );
	Com_Printf( "frameStats send: %s\n", SV_FrameStatsSummary( fs->send, fs->samples ) );
	Com_Printf( "netStats: humans %d snapshots %d avgBytes %d maxBytes %d fragmented %d rateDelayed %d queueDelayed %d\n",
		humans, fs->snapshots, fs->snapshots ? fs->snapshotBytes / fs->snapshots : 0,
		fs->maxSnapshotBytes, fs->fragmented, fs->rateDelayed, fs->queueDelayed );

	Com_Memset( fs, 0, sizeof( *fs ) );
	fs->windowStart = now;
}

/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
void SV_Frame( int msec ) {
	int		frameMsec;
	int		startTime;
	int		ticks = 0;
	int64_t	statsStart = 0, statsMark = 0, statsNow;
	int		statsBots = 0, statsGame = 0;
	qboolean	stats = sv_frameStats->integer > 0;

	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
		SV_Shutdown ("Server was killed");
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if (!com_sv_running->integer)
	{
		// Running as a server, but no map loaded
#ifdef DEDICATED
		// Block until something interesting happens
		Sys_Sleep(-1);
#endif

		return;
	}

	// allow pause if only the local client is connected
	if ( SV_CheckPaused() ) {
		return;
	}

	// if it isn't time for the next frame, do nothing
	if ( sv_fps->integer < 1 ) {
		Cvar_Set( "sv_fps", "10" );
	}

	frameMsec = 1000 / sv_fps->integer * com_timescale->value;
	// don't let it scale below 1ms
	if(frameMsec < 1)
	{
		Cvar_Set("timescale", va("%f", sv_fps->integer / 1000.0f));
		frameMsec = 1;
	}

	sv.timeResidual += msec;

	if (!com_dedicated->integer) SV_BotFrame (sv.time + sv.timeResidual);

	// if time is about to hit the 32nd bit, kick all clients
	// and clear sv.time, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( svs.time > 0x70000000 ) {
		SV_Shutdown( "Restarting server due to time wrapping" );
		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "mapname" ) ) );
		return;
	}
	// this can happen considerably earlier when lots of clients play and the map doesn't change
	if ( svs.nextSnapshotEntities >= 0x7FFFFFFE - svs.numSnapshotEntities ) {
		SV_Shutdown( "Restarting server due to numSnapshotEntities wrapping" );
		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "mapname" ) ) );
		return;
	}

	if( sv.restartTime && sv.time >= sv.restartTime ) {
		sv.restartTime = 0;
		Cbuf_AddText( "map_restart 0\n" );
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString_Big( CVAR_SYSTEMINFO ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds ();
	} else {
		startTime = 0;	// quite a compiler warning
	}

	if ( stats ) {
		statsStart = Sys_Microseconds();
	}

	// update ping based on the all received frames
	SV_CalcPings();

	if (com_dedicated->integer) SV_BotFrame (sv.time);

	if ( stats ) {
		statsMark = Sys_Microseconds();
		statsBots = statsMark - statsStart;
	}

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
		sv.timeResidual -= frameMsec;
		svs.time += frameMsec;
		sv.time += frameMsec;
		ticks++;

		// let everything in the world think and move
		VM_Call (gvm, GAME_RUN_FRAME, sv.time);
		SV_HistoryFrame( qfalse );
		SV_PlayFrame();
	}

	if ( stats ) {
		statsNow = Sys_Microseconds();
		statsGame = statsNow - statsMark;
		statsMark = statsNow;
	}

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds () - startTime;
	}

	// check timeouts
	SV_CheckTimeouts();

	// send messages back to the clients
	SV_SendClientMessages();

	if ( stats ) {
		statsNow = Sys_Microseconds();
		SV_FrameStatsRecord( ticks, statsBots, statsGame, statsNow - statsMark,
			statsNow - statsStart, frameMsec );
	}
	SV_FrameStatsFlush();

	// send a heartbeat to the master if needed
	SV_MasterHeartbeat(HEARTBEAT_FOR_MASTER);
}

/*
====================
SV_ClientIsRateLimited

The local player and LAN clients are never choked by their rate setting.
====================
*/
qboolean SV_ClientIsRateLimited(client_t *client)
{
	return !(client->netchan.remoteAddress.type == NA_LOOPBACK ||
		(sv_lanForceRate->integer && Sys_IsLANAddress(client->netchan.remoteAddress)));
}

/*
====================
SV_RateMsec

Return the number of msec until another message can be sent to
a client based on its rate settings
====================
*/

#define UDPIP_HEADER_SIZE 28
#define UDPIP6_HEADER_SIZE 48

int SV_RateMsec(client_t *client)
{
	int rate, rateMsec;
	int messageSize;
	
	messageSize = client->netchan.lastSentSize;
	rate = client->rate;

	if(sv_maxRate->integer)
	{
		if(sv_maxRate->integer < 1000)
			Cvar_Set( "sv_MaxRate", "1000" );
		if(sv_maxRate->integer < rate)
			rate = sv_maxRate->integer;
	}

	if(sv_minRate->integer)
	{
		if(sv_minRate->integer < 1000)
			Cvar_Set("sv_minRate", "1000");
		if(sv_minRate->integer > rate)
			rate = sv_minRate->integer;
	}

	if(client->netchan.remoteAddress.type == NA_IP6)
		messageSize += UDPIP6_HEADER_SIZE;
	else
		messageSize += UDPIP_HEADER_SIZE;
		
	rateMsec = messageSize * 1000 / ((int) (rate * com_timescale->value));
	rate = Sys_Milliseconds() - client->netchan.lastSentTime;
	
	if(rate > rateMsec)
		return 0;
	else
		return rateMsec - rate;
}

/*
====================
SV_SendQueuedPackets

Send download messages and queued packets in the time that we're idle, i.e.
not computing a server frame or sending client snapshots.
Return the time in msec until we expect to be called next
====================
*/

int SV_SendQueuedPackets(void)
{
	int numBlocks;
	int dlStart, deltaT, delayT;
	static int dlNextRound = 0;
	int timeVal = INT_MAX;

	// Send out fragmented packets now that we're idle
	delayT = SV_SendQueuedMessages();
	if(delayT >= 0)
		timeVal = delayT;

	if(sv_dlRate->integer)
	{
		// Rate limiting. This is very imprecise for high
		// download rates due to millisecond timedelta resolution
		dlStart = Sys_Milliseconds();
		deltaT = dlNextRound - dlStart;

		if(deltaT > 0)
		{
			if(deltaT < timeVal)
				timeVal = deltaT + 1;
		}
		else
		{
			numBlocks = SV_SendDownloadMessages();

			if(numBlocks)
			{
				// There are active downloads
				deltaT = Sys_Milliseconds() - dlStart;

				delayT = 1000 * numBlocks * MAX_DOWNLOAD_BLKSIZE;
				delayT /= sv_dlRate->integer * 1024;

				if(delayT <= deltaT + 1)
				{
					// Sending the last round of download messages
					// took too long for given rate, don't wait for
					// next round, but always enforce a 1ms delay
					// between DL message rounds so we don't hog
					// all of the bandwidth. This will result in an
					// effective maximum rate of 1MB/s per user, but the
					// low download window size limits this anyways.
					if(timeVal > 2)
						timeVal = 2;

					dlNextRound = dlStart + deltaT + 1;
				}
				else
				{
					dlNextRound = dlStart + delayT;
					delayT -= deltaT;

					if(delayT < timeVal)
						timeVal = delayT;
				}
			}
		}
	}
	else
	{
		if(SV_SendDownloadMessages())
			timeVal = 0;
	}

	return timeVal;
}
