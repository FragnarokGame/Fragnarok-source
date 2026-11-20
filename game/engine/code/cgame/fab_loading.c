/*
===========================================================================
Copyright (C) 2026 FRAGNAROK contributors

This file is part of FRAGNAROK, a standalone game built on the
Quake III Arena source code, and is distributed under the same terms:
the GNU General Public License, version 2 or later.
===========================================================================
*/
//
// fab_loading.c -- boot and map loading screens
//
// The ui draws these while the client connects and cgame draws them while it
// loads, so both modules share the code to keep the handoff seamless.

#include "../qcommon/q_shared.h"
#include "../game/bg_public.h"
#include "../game/bg_campaign.h"
#include "fab_draw.h"

qhandle_t	trap_R_RegisterShaderNoMip( const char *name );

#define LEVELSHOT_ASPECT	( 16.0f / 9.0f )

static float loadLeft, loadRight;

static const char *loadingTips[] = {
	"Jump into a wall while airborne to kick off it. You get one wall jump each time you leave the ground.",
	"In David and Goliath each frag adds 25% size and health, but reduces movement speed by 10%.",
	"Hold TAB to see the scoreboard.",
	"Host a LAN match from Multiplayer and anyone on your network can find it and join.",
	"Human players take over filler bots' slots as they join a LAN match.",
	"Health above 100 drains back down over time. Spend it while you have it.",
	NULL
};

/*
Sets the 640x480 layout used by both screens, centred with the side
extents left open for widescreen.
*/
void FAB_LoadingBegin( int vidWidth, int vidHeight ) {
	float scale = vidHeight / 480.0f;

	if ( vidWidth * 480 < vidHeight * 640 ) {
		scale = vidWidth / 640.0f;
	}
	FAB_SetLayout( scale, ( vidWidth - 640 * scale ) * 0.5f );
	loadLeft = -FAB_XOffset() / scale;
	loadRight = 640 + FAB_XOffset() / scale;
}

void FAB_MapTitle( const char *mapname, char *out, int outSize ) {
	static const char *titles[][2] = {
		{ "rift_foundry", "RIFT FOUNDRY" },
		{ "skyreach", "SKYREACH" },
		{ "bastion", "BASTION" },
		{ "test_chamber", "TEST CHAMBER" },
		{ "station", "SHIN-KAGURA STATION" },
		{ "gallows", "GALLOWS POINT" },
	};
	const char *base = strrchr( mapname, '/' );
	char *dot;
	int i;

	for ( i = 0; i < ARRAY_LEN( titles ); i++ ) {
		if ( strstr( mapname, titles[i][0] ) ) {
			Q_strncpyz( out, titles[i][1], outSize );
			return;
		}
	}
	// "maps/foo.bsp" -> "FOO"
	Q_strncpyz( out, base ? base + 1 : mapname, outSize );
	dot = strrchr( out, '.' );
	if ( dot ) {
		*dot = 0;
	}
	Q_strupr( out );
}

// info holds serverinfo-style mode keys (g_davidGoliath, g_killshift, ...)
const char *FAB_ModeName( int gametype, const char *info ) {
	if ( gametype == GT_FFA && atoi( Info_ValueForKey( info, "g_leprechaun" ) ) ) return "KILL THE LEPRECHAUN";
	if ( gametype == GT_FFA && atoi( Info_ValueForKey( info, "g_lastManStanding" ) ) ) return "LAST MAN STANDING";
	if ( gametype == GT_FFA && atoi( Info_ValueForKey( info, "g_armsRace" ) ) ) return "ARMS RACE";
	if ( gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_infected" ) ) ) return "INFECTED";
	if ( gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_bugHunt" ) ) ) return "BUG HUNT";
	if ( gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_killshift" ) ) ) return "KILLSHIFT";
	if ( atoi( Info_ValueForKey( info, "g_davidGoliath" ) ) ) {
		return "DAVID AND GOLIATH";
	}
	switch ( gametype ) {
	case GT_TOURNAMENT:	return "DUEL";
	case GT_TEAM:		return "TEAM DEATHMATCH";
	case GT_CTF:		return "CAPTURE THE FLAG";
	default:			return "FREE FOR ALL";
	}
}

/*
Fills in what the map loading screen shows. info holds serverinfo-style
keys (mapname, g_gametype, g_davidGoliath, fraglimit, timelimit,
capturelimit, sv_hostname); host is left empty for local games.
*/
void FAB_LoadInfo( const char *info, const char *host, int seed, fabLoadInfo_t *out ) {
	const campaignMission_t *mission = BG_CampaignMission( atoi( Info_ValueForKey( info, "g_campaignMission" ) ) );
	int gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
	int timelimit = mission ? 0 : atoi( Info_ValueForKey( info, "timelimit" ) );
	int scorelimit;
	const char *scoreName;

	memset( out, 0, sizeof( *out ) );
	Q_strncpyz( out->mapname, Info_ValueForKey( info, "mapname" ), sizeof( out->mapname ) );
	FAB_MapTitle( out->mapname, out->title, sizeof( out->title ) );
	Q_strncpyz( out->mode, FAB_ModeName( gametype, info ), sizeof( out->mode ) );

	if ( gametype >= GT_CTF ) {
		scorelimit = atoi( Info_ValueForKey( info, "capturelimit" ) );
		scoreName = "CAPTURES";
	} else {
		scorelimit = atoi( Info_ValueForKey( info, "fraglimit" ) );
		scoreName = "FRAGS";
	}
	if ( gametype == GT_FFA && atoi( Info_ValueForKey( info, "g_leprechaun" ) ) ) {
		scorelimit = BG_LeprechaunScoreLimit( scorelimit );
		scoreName = "POINTS";
		if ( !atoi( Info_ValueForKey( info, "g_matchSeconds" ) ) ) timelimit = 0;
	}
	if ( scorelimit && timelimit ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "FIRST TO %i %s  /  %i MINUTES",
			scorelimit, scoreName, timelimit );
	} else if ( scorelimit ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "FIRST TO %i %s", scorelimit, scoreName );
	} else if ( timelimit ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "%i MINUTES", timelimit );
	}
	if ( gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_killshift" ) ) ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "%i TEAMS  /  TAKEOVER",
			BG_ClampTeamCount( atoi( Info_ValueForKey( info, "g_teamCount" ) ) ) );
	}
	if ( host && host[0] ) {
		Q_strncpyz( out->host, host, sizeof( out->host ) );
		Q_CleanStr( out->host );
	}
	if ( gametype == GT_FFA && !atoi( Info_ValueForKey( info, "g_leprechaun" ) ) &&
		atoi( Info_ValueForKey( info, "g_lastManStanding" ) ) ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "%i LIVES  /  LAST SURVIVOR WINS",
			mission ? mission->lives : (int)Com_Clamp( 1, 99, atoi( Info_ValueForKey( info, "g_lmsLives" ) ) ) );
	}
	if ( gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_infected" ) ) ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "SURVIVE %i MINUTES  /  ONE CARRIER",
			mission ? mission->minutes : (int)Com_Clamp( 1, 60, atoi( Info_ValueForKey( info, "g_infectedMinutes" ) ) ) );
	} else if ( gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_bugHunt" ) ) ) {
		Com_sprintf( out->detail, sizeof( out->detail ), "%i TEAMS  /  %i LIVES  /  SURVIVE THE SWARM",
			BG_ClampTeamCount( atoi( Info_ValueForKey( info, "g_teamCount" ) ) ),
			mission ? mission->lives : (int)Com_Clamp( 1, 99, atoi( Info_ValueForKey( info, "g_bugLives" ) ) ) );
	}
	out->tip = loadingTips[( seed < 0 ? -seed : seed ) % ( ARRAY_LEN( loadingTips ) - 1 )];
}

// Wordmark at the spot the main menu draws it, so the menu fades in around it.
static void FAB_DrawWordmark( float x, float alpha ) {
	FAB_Fill( x, 112, 30, 2, fabCyan, alpha );
	FAB_Text( x, 172, 40, FAB_ALIGN_LEFT, "FRAGNAROK", fabWhite, alpha );
}

void FAB_DrawMenuWordmark( void ) {
	FAB_DrawWordmark( loadLeft + 44, 1 );
}

static void FAB_DrawProgress( float x, float y, float w, float progress, const char *status ) {
	char pct[8];

	progress = Com_Clamp( 0, 1, progress );
	FAB_Fill( x, y, w, 2, fabPanelLight, 1 );
	FAB_Fill( x, y, w * progress, 2, fabCyan, 1 );
	if ( progress > 0 ) {
		FAB_Glow( x + w * progress, y + 1, 18, fabCyan, 0.5f );
	}
	if ( status && status[0] ) {
		FAB_Label( x, y - 8, 6.5f, FAB_ALIGN_LEFT, status, fabDim, 1 );
	}
	Com_sprintf( pct, sizeof( pct ), "%i%%", (int)( progress * 100 ) );
	FAB_Label( x + w, y - 8, 6.5f, FAB_ALIGN_RIGHT, pct, fabDim, 1 );
}

void FAB_DrawBootScreen( float progress, const char *status ) {
	vec4_t black = { 0, 0, 0, 1 };
	float x = loadLeft + 44;

	FAB_Fill( loadLeft, 0, loadRight - loadLeft, 480, black, 1 );
	FAB_DrawWordmark( x, 1 );
	FAB_DrawProgress( x, 214, 236, progress, status );
}

/*
Covers the screen with the levelshot, cropping whichever axis overflows.
Returns qfalse when the map has none.
*/
static qboolean FAB_DrawLevelshot( const char *mapname ) {
	qhandle_t shot = trap_R_RegisterShaderNoMip( va( "levelshots/%s_wide", mapname ) );
	float w = loadRight - loadLeft;
	float s1 = 0, t1 = 0, s2 = 1, t2 = 1;

	if ( !mapname[0] || !shot ) {
		return qfalse;
	}
	if ( w / 480.0f > LEVELSHOT_ASPECT ) {
		float visible = LEVELSHOT_ASPECT / ( w / 480.0f );

		t1 = ( 1 - visible ) * 0.5f;
		t2 = 1 - t1;
	} else {
		float visible = ( w / 480.0f ) / LEVELSHOT_ASPECT;

		s1 = ( 1 - visible ) * 0.5f;
		s2 = 1 - s1;
	}
	FAB_Color( fabWhite, 1 );
	FAB_Pic( loadLeft, 0, w, 480, s1, t1, s2, t2, shot );
	return qtrue;
}

void FAB_DrawMapLoading( const fabLoadInfo_t *info, float progress, const char *status ) {
	vec4_t black = { 0, 0, 0, 1 };
	float x = loadLeft + 44;
	float right = loadRight - 44;
	float y;
	int i;

	FAB_Fill( loadLeft, 0, loadRight - loadLeft, 480, black, 1 );
	if ( FAB_DrawLevelshot( info->mapname ) ) {
		// darken toward the bottom-left where the text sits
		FAB_Fill( loadLeft, 0, 260, 480, black, 0.55f );
		FAB_Fade( loadLeft + 260, 0, 340, 480, qfalse, black, 0.55f );
		for ( i = 0; i < 12; i++ ) {
			FAB_Fill( loadLeft, 260 + i * 20, loadRight - loadLeft, 20, black, 0.07f * ( i + 1 ) );
		}
	} else {
		FAB_Fade( loadLeft, 0, ( loadRight - loadLeft ) * 0.7f, 480, qfalse, fabPanelLight, 0.5f );
	}

	y = 300;
	FAB_Fill( x, y, 30, 2, fabCyan, 1 );
	if ( info->mode[0] ) {
		FAB_Label( x, y + 18, 8, FAB_ALIGN_LEFT, info->mode, fabCyan, 1 );
	}
	FAB_Text( x, y + 60, 40, FAB_ALIGN_LEFT, info->title[0] ? info->title : "CONNECTING", fabWhite, 1 );
	y += 80;
	if ( info->detail[0] ) {
		FAB_Label( x, y, 7, FAB_ALIGN_LEFT, info->detail, fabDim, 1 );
		y += 14;
	}
	if ( info->host[0] ) {
		FAB_Label( x, y, 7, FAB_ALIGN_LEFT, va( "SERVER  %s", FAB_Upper( info->host ) ), fabDim, 0.8f );
		y += 14;
	}
	if ( info->cheats ) {
		FAB_Label( x, y, 7, FAB_ALIGN_LEFT, "CHEATS ENABLED", fabAmber, 1 );
	}

	if ( info->tip ) {
		float tx = right - 230;

		FAB_Label( tx, 360, 7, FAB_ALIGN_LEFT, "TIP", fabCyan, 1 );
		FAB_Wrapped( tx, 378, 230, 10, 14, FAB_ALIGN_LEFT, info->tip, fabWhite, 0.9f );
	}

	FAB_DrawProgress( x, 448, right - x, progress, status );
}
