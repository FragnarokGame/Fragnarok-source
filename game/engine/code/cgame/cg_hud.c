/*
===========================================================================
Copyright (C) 2026 FRAGNAROK contributors

This file is part of FRAGNAROK, a standalone game built on the
Quake III Arena source code, and is distributed under the same terms:
the GNU General Public License, version 2 or later.
===========================================================================
*/
//
// cg_hud.c -- the FRAGNAROK heads-up display
//
// The stock cgame draws in a 640x480 space stretched to the window, which
// squashes everything sideways on a widescreen display. This HUD uses a
// space that is always 480 units tall and as wide as the window's aspect
// ratio needs, so text and icons keep their shape and every element is
// anchored to the screen edge it belongs to.

#include "cg_local.h"

#ifdef STANDALONE

#include "fab_draw.h"
#include "../client/keycodes.h"

#define HUD_HEIGHT			480.0f
#define HUD_MAX_ASPECT		2.4f	// wider screens keep the HUD in a centred 2.4:1 area
#define HUD_MARGIN			18.0f

#define HUD_FEED_LINES		6
#define HUD_FEED_TIME		5000
#define HUD_FEED_MINE_TIME	8000
#define HUD_CHAT_LINES		6
#define HUD_CHAT_TIME		9000
#define HUD_CHAT_WIDTH		300.0f	// HUD units before a chat line wraps
#define HUD_CHAT_SIZE		9.0f
#define HUD_DAMAGE_ARCS		4
#define HUD_DAMAGE_TIME		1400
#define HUD_HIT_TIME		180
#define HUD_KILL_TIME		2600
#define HUD_MULTIKILL_GAP	4000
#define HUD_PICKUP_TIME		2200
#define HUD_AMMO_PIPS		30
#define HUD_LOW_AMMO		20
#define HUD_RESPAWN_DELAY	1700	// matches the server's minimum respawn time
#define HUD_VOICE_LINES		5
#define HUD_VOICE_HOLD		300		// voice packets arrive every 60ms; bridge the gaps
#define HUD_VOICE_FADE		400

typedef struct {
	char	attacker[MAX_QPATH];
	char	target[MAX_QPATH];
	int		time;
	qboolean	mine;		// local player killed or died
	qboolean	world;		// suicide or environment
} hudFeedLine_t;

typedef struct {
	char	text[TEAMCHAT_WIDTH * 3 + 1];
	int		time;
} hudChatLine_t;

typedef struct {
	float	yaw;		// world yaw pointing at the source
	int		time;
	float	strength;
} hudDamageArc_t;

typedef struct {
	qhandle_t	vignette;
	qhandle_t	hitmarker;
	qhandle_t	pistol;
	qhandle_t	rifle;
	qhandle_t	rocket;
	qhandle_t	machinegun;
	qhandle_t	sword;
	qhandle_t	infinity;
	qhandle_t	skull;

	// layout, refreshed every frame
	float		scale;		// window pixels per HUD unit
	float		width;		// HUD units across the safe area
	float		height;		// HUD units top to bottom (480 at cg_hudScale 1)
	float		xOffset;	// window pixels left of the safe area

	hudFeedLine_t	feed[HUD_FEED_LINES];
	int				feedHead;

	hudChatLine_t	chat[HUD_CHAT_LINES];
	int				chatHead;

	hudDamageArc_t	arcs[HUD_DAMAGE_ARCS];
	int				arcHead;
	int				damageFlashTime;

	int			hitTime;
	int			killTime;
	char		killName[MAX_QPATH];
	int			killStreak;

	int			pickupTime;
	int			pickupAmount;

	int			lastHealth;
	int			trailHealth;	// lags behind health so damage reads as a draining bar
	int			trailTime;
	int			healthFlashTime;
	int			lastAmmo;
	int			lastWeapon;
	int			ammoFlashTime;
	int			deathTime;

	int			serverInfoCount;
	qboolean	davidGoliath;
	qboolean	killshift;
	qboolean	lastManStanding;
	qboolean	leprechaun;
	qboolean	infected;
	qboolean	bugHunt;
	int			forceRespawn;
	char		mapTitle[64];
} hud_t;

static hud_t hud;

static struct {
	qboolean open;
	int selected;
	int selectionState;
	int spawnCount;
	int sentTime;
	int readyTime;
	float mouseX, mouseY;
} classMenu;


/*
===============================================================================

STATE

===============================================================================
*/

void CG_HudInit( void ) {
	memset( &hud, 0, sizeof( hud ) );
	memset( &classMenu, 0, sizeof( classMenu ) );
	trap_Key_SetCatcher( trap_Key_GetCatcher() & ~KEYCATCH_CGAME );
	FAB_Init();
	hud.vignette = trap_R_RegisterShaderNoMip( "gfx/2d/hud_vignette" );
	hud.hitmarker = trap_R_RegisterShaderNoMip( "gfx/2d/hud_hitmarker" );
	hud.pistol = trap_R_RegisterShaderNoMip( "gfx/2d/hud_pistol" );
	hud.sword = trap_R_RegisterShaderNoMip( "gfx/2d/hud_sword" );
	hud.rifle = trap_R_RegisterShaderNoMip( "gfx/2d/hud_rifle" );
	hud.rocket = trap_R_RegisterShaderNoMip( "gfx/2d/hud_rocket" );
	hud.machinegun = trap_R_RegisterShaderNoMip( "gfx/2d/hud_machinegun" );
	hud.infinity = trap_R_RegisterShaderNoMip( "gfx/2d/hud_infinity" );
	hud.skull = trap_R_RegisterShaderNoMip( "gfx/2d/hud_skull" );
	hud.serverInfoCount = -1;
	hud.lastHealth = hud.trailHealth = -1;
	hud.lastAmmo = -1;
	hud.lastWeapon = WP_NONE;
}

static const char *HUD_ClientName( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cgs.clientinfo[clientNum].infoValid ) {
		return "";
	}
	return cgs.clientinfo[clientNum].name;
}

void CG_HudObituary( int target, int attacker, int mod ) {
	hudFeedLine_t *line;
	int self = cg.snap ? cg.snap->ps.clientNum : -1;
	qboolean world = ( attacker < 0 || attacker >= MAX_CLIENTS || attacker == target );

	line = &hud.feed[hud.feedHead];
	hud.feedHead = ( hud.feedHead + 1 ) % HUD_FEED_LINES;
	memset( line, 0, sizeof( *line ) );
	Q_strncpyz( line->target, HUD_ClientName( target ), sizeof( line->target ) );
	if ( mod == MOD_SPIDER ) {
		world = qfalse;
		Q_strncpyz( line->attacker, "BUG", sizeof( line->attacker ) );
	} else if ( !world ) {
		Q_strncpyz( line->attacker, HUD_ClientName( attacker ), sizeof( line->attacker ) );
	}
	line->world = world;
	line->mine = ( target == self || ( !world && attacker == self ) );
	line->time = cg.time;

	if ( target == self ) {
		// CG_Obituary fills this in again when a real player made the kill
		cg.killerName[0] = 0;
	}
	if ( !world && attacker == self && target != self ) {
		if ( cg.time - hud.killTime < HUD_MULTIKILL_GAP ) {
			hud.killStreak++;
		} else {
			hud.killStreak = 1;
		}
		hud.killTime = cg.time;
		hud.hitTime = cg.time;
		Q_strncpyz( hud.killName, HUD_ClientName( target ), sizeof( hud.killName ) );
	}
}

void CG_HudHit( void ) {
	hud.hitTime = cg.time;
}

void CG_HudDamage( float yaw, qboolean directional, int damage ) {
	hud.damageFlashTime = cg.time;
	if ( directional ) {
		hudDamageArc_t *arc = &hud.arcs[hud.arcHead];
		hud.arcHead = ( hud.arcHead + 1 ) % HUD_DAMAGE_ARCS;
		arc->yaw = yaw;
		arc->time = cg.time;
		arc->strength = 0.55f + damage / 40.0f;
		if ( arc->strength > 1.0f ) {
			arc->strength = 1.0f;
		}
	}
}

void CG_HudPickup( int amount ) {
	hud.pickupTime = cg.time;
	hud.pickupAmount = amount;
}

static void HUD_PushChat( const char *text ) {
	hudChatLine_t *line;

	if ( FAB_TextWidth( text, HUD_CHAT_SIZE, 0 ) <= 0 ) {
		return;
	}
	line = &hud.chat[hud.chatHead];
	hud.chatHead = ( hud.chatHead + 1 ) % HUD_CHAT_LINES;
	Q_strncpyz( line->text, text, sizeof( line->text ) );
	line->time = cg.time;
}

// Word-wraps a chat message into HUD lines, carrying the colour across breaks.
void CG_HudChat( const char *text ) {
	char line[TEAMCHAT_WIDTH * 3 + 1];
	const char *s = text, *resume = NULL;
	char color = COLOR_WHITE, spaceColor = COLOR_WHITE;
	int len = 0, space = -1;

	while ( 1 ) {
		if ( !*s || *s == '\n' || len >= (int)sizeof( line ) - 3 ) {
			line[len] = 0;
			HUD_PushChat( line );
			if ( !*s ) {
				return;
			}
			if ( *s == '\n' ) {
				s++;
			}
			len = 0;
			space = -1;
			line[len++] = Q_COLOR_ESCAPE;
			line[len++] = color;
			continue;
		}
		if ( Q_IsColorString( s ) ) {
			color = s[1];
			line[len++] = *s++;
			line[len++] = *s++;
			continue;
		}
		if ( *s == ' ' ) {
			space = len;
			resume = s + 1;
			spaceColor = color;
		}
		line[len++] = *s++;
		line[len] = 0;
		if ( FAB_TextWidth( line, HUD_CHAT_SIZE, 0 ) > HUD_CHAT_WIDTH ) {
			if ( space > 2 ) {
				len = space;
				s = resume;
				color = spaceColor;
			} else {
				len--;
				s--;
			}
			line[len] = 0;
			HUD_PushChat( line );
			len = 0;
			space = -1;
			line[len++] = Q_COLOR_ESCAPE;
			line[len++] = color;
		}
	}
}

static void HUD_UpdateServerInfo( void ) {
	const char *info;

	if ( hud.serverInfoCount == cgs.levelStartTime ) {
		return;
	}
	hud.serverInfoCount = cgs.levelStartTime;
	info = CG_ConfigString( CS_SERVERINFO );
	hud.davidGoliath = atoi( Info_ValueForKey( info, "g_davidGoliath" ) ) != 0;
	hud.killshift = atoi( Info_ValueForKey( info, "g_killshift" ) ) != 0;
	hud.leprechaun = cgs.gametype == GT_FFA && atoi( Info_ValueForKey( info, "g_leprechaun" ) ) != 0;
	hud.lastManStanding = !hud.leprechaun && cgs.gametype == GT_FFA && atoi( Info_ValueForKey( info, "g_lastManStanding" ) ) != 0;
	hud.infected = cgs.gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_infected" ) ) != 0;
	hud.bugHunt = cgs.gametype == GT_TEAM && atoi( Info_ValueForKey( info, "g_bugHunt" ) ) != 0;
	{
		// only known when this machine runs the server; remote clients just get the prompt
		char buf[16];
		trap_Cvar_VariableStringBuffer( "g_forcerespawn", buf, sizeof( buf ) );
		hud.forceRespawn = atoi( buf );
	}

	FAB_MapTitle( cgs.mapname, hud.mapTitle, sizeof( hud.mapTitle ) );
}

static const char *HUD_ModeName( void ) {
	return FAB_ModeName( cgs.gametype, CG_ConfigString( CS_SERVERINFO ) );
}

static int HUD_PlayerCount( void ) {
	int i, count = 0;

	for ( i = 0; i < cgs.maxclients && i < MAX_CLIENTS; i++ ) {
		if ( cgs.clientinfo[i].infoValid && cgs.clientinfo[i].team != TEAM_SPECTATOR ) {
			count++;
		}
	}
	return count;
}

// Accent colour: team colour in team games, cyan otherwise.
static void HUD_Accent( vec4_t out ) {
	if ( cgs.gametype >= GT_TEAM ) {
		if ( BG_IsCombatTeam( (team_t)cg.snap->ps.persistant[PERS_TEAM] ) ) {
			BG_TeamColor( (team_t)cg.snap->ps.persistant[PERS_TEAM], out );
			return;
		}
	}
	Vector4Copy( fabCyan, out );
}

static float HUD_Pulse( int period ) {
	return 0.5f + 0.5f * sin( cg.time * 2.0f * M_PI / period );
}

static float HUD_FadeOut( int start, int total, int fade ) {
	int t = cg.time - start;

	if ( start <= 0 || t < 0 || t >= total ) {
		return 0;
	}
	if ( total - t < fade ) {
		return ( total - t ) / (float)fade;
	}
	return 1;
}

/*
===============================================================================

HEALTH, COMPANION, AMMO

===============================================================================
*/

static void HUD_Bar( float x, float y, float w, float h, float frac, float trail,
		int segments, const vec4_t color, float alpha ) {
	int i;

	frac = Com_Clamp( 0, 1, frac );
	trail = Com_Clamp( 0, 1, trail );

	FAB_Fill( x, y, w, h, fabPanelLight, alpha );
	if ( trail > frac ) {
		FAB_Fill( x + w * frac, y, w * ( trail - frac ), h, fabWhite, alpha * 0.55f );
	}
	FAB_Fill( x, y, w * frac, h, color, alpha );
	// brighter top edge gives the fill some depth
	FAB_Fill( x, y, w * frac, h * 0.3f, fabWhite, alpha * 0.25f );

	if ( segments > 1 ) {
		for ( i = 1; i < segments; i++ ) {
			FAB_Fill( x + w * i / segments - 0.5f, y, 1.0f, h, fabPanel, alpha * 0.9f );
		}
	}
}

// The health box: one translucent panel with the number in it. Its colour
// says how you are doing (green, amber from half health, red from a quarter,
// pulsing while critical), a strip along the bottom shows health out of the
// maximum, a "+" marks spawn overcharge, and it flashes red when you are hit.
static void HUD_DrawHealth( float x, float y ) {
	playerState_t *ps = &cg.snap->ps;
	int health = ps->stats[STAT_HEALTH];
	int maxHealth = ps->stats[STAT_MAX_HEALTH] > 0 ? ps->stats[STAT_MAX_HEALTH] : 100;
	int armor = ps->stats[STAT_ARMOR];
	int goliathFrags = MAX( ps->stats[STAT_GOLIATH_FRAGS], ps->generic1 );
	const float w = 84, h = 48;
	static const vec4_t healthGreen = { 0.18f, 0.75f, 0.44f, 1.0f };
	vec4_t color;
	float flash, alpha, size, width;
	char buf[32];

	if ( health < 0 ) {
		health = 0;
	}
	if ( health < hud.lastHealth ) {
		hud.healthFlashTime = cg.time;
	}
	hud.lastHealth = health;

	alpha = 0.55f;
	if ( health <= maxHealth / 4 ) {
		Vector4Copy( fabRed, color );
		alpha = 0.45f + 0.35f * HUD_Pulse( 700 );
	} else if ( health <= maxHealth / 2 ) {
		Vector4Copy( fabAmber, color );
	} else {
		Vector4Copy( healthGreen, color );
	}

	FAB_Panel( x, y, w, h, 8, FAB_CUT_TL | FAB_CUT_BR, color, alpha );
	flash = HUD_FadeOut( hud.healthFlashTime, 300, 300 );
	if ( flash > 0 ) {
		FAB_Panel( x, y, w, h, 8, FAB_CUT_TL | FAB_CUT_BR, fabRed, 0.75f * flash );
	}

	// health out of the maximum, brighter than the box so it reads at a glance
	FAB_Fill( x, y + h - 3, w, 3, fabPanel, 1 );
	FAB_Fill( x, y + h - 3, w * Com_Clamp( 0, 1, health / (float)maxHealth ), 3, color, 1 );
	if ( health > maxHealth ) {
		FAB_Label( x + w - 7, y + 12, 10, FAB_ALIGN_RIGHT, "+", fabWhite, 0.9f );
	}

	Com_sprintf( buf, sizeof( buf ), "%i", health );
	width = FAB_TextWidth( buf, 30, 0 );
	size = width > w - 16 ? 30 * ( w - 16 ) / width : 30;
	FAB_Text( x + w * 0.5f, y + 33, size, FAB_ALIGN_CENTER, buf, fabWhite, 1 );

	// armor is a slimmer chip to the right of the box, shown only while you have some
	if ( armor > 0 ) {
		static const vec4_t armorBlue = { 0.49f, 0.77f, 0.96f, 1.0f };
		static const vec4_t armorText = { 0.75f, 0.89f, 0.98f, 1.0f };
		const float cw = 66, ch = 34, cxp = x + w + 4, cyp = y + h - ch;
		const float shieldRows[] = { 10, 10, 10, 8, 6, 4, 2 };
		int row;

		FAB_Panel( cxp, cyp, cw, ch, 8, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 1 );
		FAB_Fill( cxp, cyp + ch - 2, cw, 2, armorBlue, 1 );
		for ( row = 0; row < (int)ARRAY_LEN( shieldRows ); row++ ) {
			FAB_Fill( cxp + 13 - shieldRows[row] * 0.5f, cyp + 9 + row * 2.2f, shieldRows[row], 2.2f, armorBlue, 1 );
		}
		Com_sprintf( buf, sizeof( buf ), "%i", armor );
		width = FAB_TextWidth( buf, 20, 0 );
		size = width > cw - 32 ? 20 * ( cw - 32 ) / width : 20;
		FAB_Text( cxp + 28 + ( cw - 32 ) * 0.5f, cyp + 23, size, FAB_ALIGN_CENTER, buf, armorText, 1 );
	}
	// David and Goliath growth stays above the box
	if ( hud.davidGoliath && health > 0 && goliathFrags > 0 ) {
		Com_sprintf( buf, sizeof( buf ), "GOLIATH x%i", goliathFrags );
		FAB_Label( x, y - 6, 8, FAB_ALIGN_LEFT, buf, fabAmber, 1 );
	}
}

static void HUD_DrawClass( float x, float y ) {
	playerState_t *ps = &cg.snap->ps;
	int playerClass = ps->stats[STAT_PLAYER_CLASS] - 1;
	if ( playerClass < 0 || playerClass >= CLASS_COUNT ) return;
	FAB_Label( x, y + 14, 8, FAB_ALIGN_LEFT, BG_PlayerClass( playerClass )->title, fabCyan, 1 );
	FAB_Label( x + 236, y + 14, 8, FAB_ALIGN_RIGHT,
		playerClass == CLASS_SENTINEL ? va( "SHIELD %d", ps->stats[STAT_SHIELD_HEALTH] ) :
		va( "ARMOR %d", ps->stats[STAT_ARMOR] ), fabDim, 1 );
}

static int ClassMenu_Hit( float x, float y ) {
	int i;
	for ( i = 0; i < CLASS_COUNT; i++ ) {
		float left = 28 + ( i % 2 ) * 300, top = 118 + ( i / 2 ) * 86;
		if ( x >= left && x <= left + 284 && y >= top && y <= top + 78 ) return i;
	}
	return -1;
}

static void ClassMenu_Confirm( void ) {
	if ( !cg.snap || !cg.snap->ps.stats[STAT_CLASS_SELECTION] ||
		cg.snap->ps.stats[STAT_CLASS_RESPAWN_WAIT] > 0 ||
		( classMenu.sentTime && cg.time - classMenu.sentTime < 500 ) ) return;
	trap_Cvar_Set( "playerclass", BG_PlayerClass( classMenu.selected )->name );
	trap_SendClientCommand( va( "selectclass %s", BG_PlayerClass( classMenu.selected )->name ) );
	classMenu.sentTime = cg.time;
}

void CG_ClassSelectionKey( int key, qboolean down ) {
	int hit;
	if ( !down || !classMenu.open || !cg.snap || !cg.snap->ps.stats[STAT_CLASS_SELECTION] ) return;
	if ( key >= '1' && key < '1' + CLASS_COUNT ) classMenu.selected = key - '1';
	else if ( key == K_LEFTARROW ) classMenu.selected = ( classMenu.selected + CLASS_COUNT - 1 ) % CLASS_COUNT;
	else if ( key == K_RIGHTARROW ) classMenu.selected = ( classMenu.selected + 1 ) % CLASS_COUNT;
	else if ( key == K_UPARROW ) classMenu.selected = ( classMenu.selected + CLASS_COUNT - 2 ) % CLASS_COUNT;
	else if ( key == K_DOWNARROW ) classMenu.selected = ( classMenu.selected + 2 ) % CLASS_COUNT;
	else if ( key == K_TAB || key == K_MWHEELDOWN ) classMenu.selected = ( classMenu.selected + 1 ) % CLASS_COUNT;
	else if ( key == K_MWHEELUP ) classMenu.selected = ( classMenu.selected + CLASS_COUNT - 1 ) % CLASS_COUNT;
	else if ( key == K_ENTER || key == K_SPACE ) ClassMenu_Confirm();
	else if ( key == K_MOUSE1 ) {
		hit = ClassMenu_Hit( classMenu.mouseX, classMenu.mouseY );
		if ( hit >= 0 ) classMenu.selected = hit;
		else if ( classMenu.mouseX >= 220 && classMenu.mouseX <= 420 &&
			classMenu.mouseY >= 392 && classMenu.mouseY <= 426 ) ClassMenu_Confirm();
	}
}

void CG_ClassSelectionMouse( int x, int y ) {
	float scale = MIN( cgs.glconfig.vidWidth / 640.0f, cgs.glconfig.vidHeight / 480.0f );
	if ( !classMenu.open || scale <= 0 ) return;
	classMenu.mouseX = Com_Clamp( 0, 640, classMenu.mouseX + x / scale );
	classMenu.mouseY = Com_Clamp( 0, 480, classMenu.mouseY + y / scale );
}

qboolean CG_ClassSelectionDraw( void ) {
	playerState_t *ps;
	float scale, offset;
	int i, flags, hit;
	char line[96];
	const vec4_t accents[CLASS_COUNT] = {
		{ 0.42f, 0.90f, 0.95f, 1 }, { 1, 0.70f, 0.18f, 1 },
		{ 0.95f, 0.45f, 0.39f, 1 }, { 0.48f, 0.94f, 0.65f, 1 },
		{ 0.75f, 0.65f, 1, 1 }
	};
	vec4_t black = { 0, 0, 0, 1 };
	if ( !cg.snap ) return qfalse;
	ps = &cg.snap->ps;
	if ( !ps->stats[STAT_CLASS_SELECTION] || cg_menuFlythrough.integer || cg.demoPlayback ||
		ps->pm_type == PM_INTERMISSION ) {
		if ( classMenu.open ) trap_Key_SetCatcher( trap_Key_GetCatcher() & ~KEYCATCH_CGAME );
		classMenu.open = qfalse;
		return qfalse;
	}
	if ( !classMenu.open || classMenu.selectionState != ps->stats[STAT_CLASS_SELECTION] ||
		classMenu.spawnCount != ps->persistant[PERS_SPAWN_COUNT] ) {
		classMenu.open = qtrue;
		classMenu.selected = MAX( 0, MIN( CLASS_COUNT - 1, ps->stats[STAT_PLAYER_CLASS] - 1 ) );
		classMenu.selectionState = ps->stats[STAT_CLASS_SELECTION];
		classMenu.spawnCount = ps->persistant[PERS_SPAWN_COUNT];
		classMenu.mouseX = 320;
		classMenu.mouseY = 409;
		classMenu.sentTime = 0;
		classMenu.readyTime = cg.time + ps->stats[STAT_CLASS_RESPAWN_WAIT];
	}
	if ( trap_Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CONSOLE ) ) {
		trap_Key_SetCatcher( trap_Key_GetCatcher() & ~KEYCATCH_CGAME );
		return qtrue;
	}
	trap_Key_SetCatcher( trap_Key_GetCatcher() | KEYCATCH_CGAME );
	scale = MIN( cgs.glconfig.vidWidth / 640.0f, cgs.glconfig.vidHeight / 480.0f );
	offset = ( cgs.glconfig.vidWidth - 640 * scale ) * 0.5f;
	FAB_SetLayout( scale, offset );
	FAB_Fill( -offset / scale, 0, cgs.glconfig.vidWidth / scale, 480, black, 0.72f );
	FAB_Label( 320, 42, 8, FAB_ALIGN_CENTER, ps->stats[STAT_CLASS_SELECTION] == 1 ? "DEPLOYMENT" : "NEXT LIFE", fabCyan, 1 );
	FAB_Text( 320, 77, 26, FAB_ALIGN_CENTER, "CHOOSE CLASS", fabWhite, 1 );
	if ( ps->stats[STAT_CLASS_SELECTION] == 2 && cg.killerName[0] ) {
		FAB_Label( 320, 105, 9, FAB_ALIGN_CENTER, va( "KILLED BY %s", cg.killerName ), fabDim, 1 );
	}
	hit = ClassMenu_Hit( classMenu.mouseX, classMenu.mouseY );
	for ( i = 0; i < CLASS_COUNT; i++ ) {
		const playerClassInfo_t *info = BG_PlayerClass( i );
		float x = 28 + ( i % 2 ) * 300, y = 118 + ( i / 2 ) * 86;
		qhandle_t icon = info->weapon == WP_ROCKET_LAUNCHER ? hud.rocket :
			info->weapon == WP_PLASMAGUN ? hud.machinegun : info->weapon == WP_GAUNTLET ? hud.sword : hud.rifle;
		FAB_Fill( x, y, 284, 78, classMenu.selected == i ? fabPanelLight : fabPanel, 1 );
		FAB_Fill( x, y, classMenu.selected == i ? 3 : 1, 78, accents[i], 1 );
		if ( hit == i ) FAB_Fill( x, y, 284, 78, fabWhite, 0.06f );
		FAB_Label( x + 14, y + 18, 12, FAB_ALIGN_LEFT, info->title, accents[i], 1 );
		FAB_Label( x + 14, y + 31, 7, FAB_ALIGN_LEFT, info->role, fabDim, 1 );
		FAB_Color( fabWhite, 0.9f );
		FAB_Pic( x + 214, y + 12, 56, 18, 0, 0, 1, 1, icon );
		Com_sprintf( line, sizeof( line ), "%d HEALTH   %d ARMOR   %d%% SPEED", info->health, info->armor, (int)( info->speedScale * 100 + 0.5f ) );
		if ( i == CLASS_SENTINEL ) Com_sprintf( line, sizeof( line ), "%d HEALTH   100-130%% SPEED", info->health );
		FAB_Label( x + 14, y + 45, 7.5f, FAB_ALIGN_LEFT, line, fabWhite, 1 );
		Com_sprintf( line, sizeof( line ), "%s / %d %s", info->weapon == WP_ROCKET_LAUNCHER ? "ROCKET LAUNCHER" :
			info->weapon == WP_PLASMAGUN ? "MACHINE GUN" : "RIFLE", info->ammo, info->weapon == WP_PLASMAGUN ? "CELLS" : "ROUNDS" );
		if ( i == CLASS_SENTINEL ) Q_strncpyz( line, "SWORD / 200 SHIELD HEALTH", sizeof( line ) );
		FAB_Label( x + 14, y + 58, 7.5f, FAB_ALIGN_LEFT, line, fabDim, 1 );
		FAB_Label( x + 14, y + 71, 6.5f, FAB_ALIGN_LEFT, info->tradeoff, fabDim, 0.85f );
	}
	flags = FAB_BUTTON_PRIMARY;
	if ( ps->stats[STAT_CLASS_RESPAWN_WAIT] > 0 ) flags |= FAB_BUTTON_DISABLED;
	else flags |= FAB_BUTTON_FOCUS;
	if ( ps->stats[STAT_CLASS_RESPAWN_WAIT] > 0 ) Com_sprintf( line, sizeof( line ), "READY IN %.1f", ps->stats[STAT_CLASS_RESPAWN_WAIT] / 1000.0f );
	else Q_strncpyz( line, ps->stats[STAT_CLASS_SELECTION] == 1 ? "DEPLOY" : "RESPAWN", sizeof( line ) );
	FAB_Button( 220, 392, 200, 34, line, flags );
	FAB_Label( 320, 453, 8, FAB_ALIGN_CENTER, classMenu.selected == CLASS_SENTINEL ?
		"LEFT CLICK: SWORD   HOLD RIGHT CLICK: SHIELD" : "PISTOL INCLUDED", fabDim, 1 );
	if ( ps->stats[STAT_CLASS_SELECTION] == 2 ) {
		int force = atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_forcerespawn" ) );
		if ( force > 0 ) FAB_Label( 320, 377, 8, FAB_ALIGN_CENTER,
			va( "AUTO RESPAWN IN %d", MAX( 0, ( classMenu.readyTime + force * 1000 - cg.time + 999 ) / 1000 ) ), fabDim, 1 );
	}
	FAB_Cursor( classMenu.mouseX, classMenu.mouseY, 18 );
	trap_R_SetColor( NULL );
	return qtrue;
}

void CG_ClassSelectionDebug( void ) {
	const char *action = CG_Argv( 1 );
	if ( !Q_stricmp( action, "equipment" ) && cg.snap ) {
		int i;
		for ( i = 0; i < cg.snap->numEntities; i++ ) {
			entityState_t *ent = &cg.snap->entities[i];
			if ( ent->eType == ET_PLAYER && ent->time2 > 0 ) {
				CG_Printf( "sentinel remote: client=%d shield=%d raised=%d weapon=%d\n", ent->number,
				( ent->time2 & 255 ) - 1, !!( ent->time2 & SHIELD_ENTITY_RAISED ), ent->weapon );
			}
		}
		return;
	}
	if ( !Q_stricmp( action, "combat" ) && cg.snap ) {
		CG_Printf( "sentinel client: shield=%d raised=%d weapon=%d entity=%d speed=%.0f\n",
			cg.snap->ps.stats[STAT_SHIELD_HEALTH], !!( cg.predictedPlayerState.pm_flags & PMF_SHIELD_UP ),
			cg.predictedPlayerState.weapon, cg_entities[cg.snap->ps.clientNum].currentState.time2,
			BG_PlayerMoveSpeed( &cg.predictedPlayerState, cg.predictedPlayerState.pm_flags & PMF_SHIELD_UP ? BUTTON_SHIELD : 0 ) );
		return;
	}
	if ( !Q_stricmp( action, "key" ) ) CG_ClassSelectionKey( atoi( CG_Argv( 2 ) ), qtrue );
	else if ( !Q_stricmp( action, "mouse" ) ) {
		classMenu.mouseX = atof( CG_Argv( 2 ) );
		classMenu.mouseY = atof( CG_Argv( 3 ) );
		CG_ClassSelectionKey( K_MOUSE1, qtrue );
	}
	CG_Printf( "class menu: open=%d selected=%s catcher=%d state=%d\n", classMenu.open,
		BG_PlayerClass( classMenu.selected )->name, trap_Key_GetCatcher(), cg.snap ? cg.snap->ps.stats[STAT_CLASS_SELECTION] : 0 );
}

/*
The weapon column down the lower right: every gun you carry, in key order,
with its key, silhouette and ammo. The gun in your hands gets a wider cyan
row with its name. Returns the top edge so pickup text can sit above it.
*/
static float HUD_DrawWeaponColumn( float right, float bottom ) {
	playerState_t *ps = &cg.predictedPlayerState;
	const float rowW = 92, rowH = 34, activeW = 138, activeH = 42, gap = 3;
	int slot, count = 0;
	float y = bottom;
	int owned[10];

	if ( !( ps->stats[STAT_WEAPONS] & ~( 1 << WP_NONE ) ) ) {
		return bottom;
	}
	for ( slot = 0; slot < 10; slot++ ) {
		int weapon = bg_weaponSlots[slot];

		if ( ( ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) && BG_WeaponEnabled( weapon ) ) {
			owned[count++] = weapon;
		}
	}

	if ( hud.lastWeapon == ps->weapon && hud.lastAmmo != ps->ammo[ps->weapon] ) {
		hud.ammoFlashTime = cg.time;
	}
	hud.lastAmmo = ps->ammo[ps->weapon];
	hud.lastWeapon = ps->weapon;

	// the bottom row sits on the margin and the list grows upward, so the
	// column keeps its place as guns come and go
	for ( slot = count - 1; slot >= 0; slot-- ) {
		int weapon = owned[slot];
		qboolean active = weapon == ps->weapon;
		qboolean sentinelShield = weapon == WP_GAUNTLET && ps->stats[STAT_PLAYER_CLASS] == CLASS_SENTINEL + 1;
		qboolean infinite = !sentinelShield && ps->ammo[weapon] < 0;
		int ammo = sentinelShield ? ps->stats[STAT_SHIELD_HEALTH] : MAX( 0, ps->ammo[weapon] );
		int lowAmmo = weapon == WP_BFG ? 1 :
			( weapon == WP_FLAK || weapon == WP_ROCKET_LAUNCHER || weapon == WP_RAILGUN ||
			weapon == WP_GRENADE_LAUNCHER ) ? 3 : 20;
		qboolean low = !infinite && !sentinelShield && ammo <= lowAmmo;
		float w = active ? activeW : rowW, h = active ? activeH : rowH;
		float x = right - w, flash;
		const float *numColor = low ? fabRed : sentinelShield ? fabCyan : fabAmber;
		vec4_t amount;
		char buf[16];

		y -= h;
		CG_RegisterWeapon( weapon );
		FAB_Panel( x, y, w, h, 8, FAB_CUT_TL | FAB_CUT_BR, active ? fabCyan : fabPanel, active ? 0.28f : 1 );
		if ( active ) {
			FAB_Fill( x, y, 2, h, fabCyan, 1 );
		}

		// key to press
		Com_sprintf( buf, sizeof( buf ), "%i", BG_WeaponSlot( weapon ) );
		FAB_Label( x + 9, y + ( active ? 27 : 21 ), 8.5f, FAB_ALIGN_LEFT, buf, active ? fabCyan : fabDim, 1 );

		if ( active ) {
			FAB_Label( x + 26, y + 12, 7, FAB_ALIGN_LEFT, BG_FindItemForWeapon( weapon )->pickup_name, fabCyan, 1 );
		}
		FAB_Color( fabWhite, ammo == 0 && !infinite ? 0.45f : 0.9f );
		if ( active ) {
			FAB_Pic( x + 26, y + 17, 44, 17, 0, 0, 1, 1, cg_weapons[weapon].weaponIcon );
		} else {
			FAB_Pic( x + 22, y + 10, 36, 14, 0, 0, 1, 1, cg_weapons[weapon].weaponIcon );
		}

		Vector4Copy( numColor, amount );
		if ( !infinite && ammo == 0 ) {
			amount[3] = 0.5f + 0.5f * HUD_Pulse( 600 );
		}
		if ( infinite ) {
			FAB_Color( fabAmber, 1 );
			FAB_Pic( x + w - 32, y + ( active ? 22 : 11 ), 24, 14, 0, 0, 1, 1, hud.infinity );
		} else {
			flash = active ? HUD_FadeOut( hud.ammoFlashTime, 90, 90 ) : 0;
			Com_sprintf( buf, sizeof( buf ), "%i", ammo );
			FAB_Text( x + w - 8, y + ( active ? 34 : 23 ), ( active ? 15 : 12 ) + flash * 2, FAB_ALIGN_RIGHT, buf, amount, 1 );
		}
		y -= gap;
	}
	return y;
}

static void HUD_DrawPickup( float right, float y ) {
	float a = HUD_FadeOut( hud.pickupTime, HUD_PICKUP_TIME, 600 );
	float rise;
	char buf[32];

	if ( a <= 0 ) {
		return;
	}
	rise = ( cg.time - hud.pickupTime ) / (float)HUD_PICKUP_TIME * 10;
	Com_sprintf( buf, sizeof( buf ), "+%i", hud.pickupAmount );
	FAB_Label( right - 16, y - rise, 8, FAB_ALIGN_RIGHT, "AMMO", fabDim, a );
	FAB_Text( right - 16 - FAB_TextWidth( "AMMO", 8, 8 * 0.18f ) - 6, y - rise + 1, 16,
		FAB_ALIGN_RIGHT, buf, fabAmber, a );
}

/*
===============================================================================

MATCH STRIP, KILL FEED

===============================================================================
*/

static const char *HUD_TeamName( team_t team ) {
	if ( hud.infected ) return team == TEAM_RED ? "INFECTED" : "SURVIVORS";
	return BG_TeamName( team );
}

static void HUD_DrawMatch( void ) {
	playerState_t *ps = &cg.snap->ps;
	float cx = hud.width * 0.5f;
	// frags left, a smaller clock in the middle, rank right
	const float sideW = 78, midW = 52, sideH = 40, midH = 30, gap = 2, y = 12;
	const float leftX = cx - midW * 0.5f - gap - sideW, midX = cx - midW * 0.5f, rightX = cx + midW * 0.5f + gap;
	int rank, players, score, secs;
	float size;
	qboolean tied;
	char buf[64];
	vec4_t accent;
	const char *leftLabel, *rightLabel;
	const float *rightColor = fabWhite;

	HUD_Accent( accent );
	players = HUD_PlayerCount();
	if ( hud.lastManStanding ) {
		int started, startTime;
		sscanf( CG_ConfigString( CS_LMS_STATE ), "%i %i %i", &started, &players, &startTime );
	}
	score = hud.lastManStanding ? MAX( 0, ps->persistant[PERS_LMS_LIVES] ) : ps->persistant[PERS_SCORE];
	rank = ( ps->persistant[PERS_RANK] & ~RANK_TIED_FLAG ) + 1;
	tied = ( ps->persistant[PERS_RANK] & RANK_TIED_FLAG ) != 0;

	FAB_Panel( leftX, y, sideW, sideH, 8, FAB_CUT_BL, fabPanel, 1 );
	FAB_Panel( midX, y, midW, midH, 8, FAB_CUT_BL | FAB_CUT_BR, fabPanel, 1 );
	FAB_Panel( rightX, y, sideW, sideH, 8, FAB_CUT_BR, fabPanel, 1 );

	leftLabel = hud.leprechaun ? "POINTS" : hud.lastManStanding ? "LIVES" : "FRAGS";
	if ( cgs.gametype >= GT_TEAM ) {
		// team games score by team: your team on the left, the lead on the right
		int ownIndex = BG_TeamIndex( (team_t)ps->persistant[PERS_TEAM] );
		int leaderIndex = 0;
		vec4_t teamColor;
		int i;

		for ( i = 1; i < cgs.teamCount; i++ ) {
			if ( cgs.teamScores[i] > cgs.teamScores[leaderIndex] ) {
				leaderIndex = i;
			}
		}
		if ( ownIndex < 0 || ownIndex >= cgs.teamCount ) {
			ownIndex = 0;
		}
		BG_TeamColor( BG_TeamForIndex( ownIndex ), teamColor );
		FAB_Fill( leftX, y, sideW, 2, teamColor, 1 );
		Com_sprintf( buf, sizeof( buf ), "%i", cgs.teamScores[ownIndex] );
		FAB_Text( leftX + sideW * 0.5f, y + 26, 20, FAB_ALIGN_CENTER, buf, teamColor, 1 );
		FAB_Label( leftX + sideW * 0.5f, y + 36, 7, FAB_ALIGN_CENTER,
			HUD_TeamName( BG_TeamForIndex( ownIndex ) ), fabDim, 1 );
		BG_TeamColor( BG_TeamForIndex( leaderIndex ), teamColor );
		Com_sprintf( buf, sizeof( buf ), "%i", cgs.teamScores[leaderIndex] );
		FAB_Text( rightX + sideW * 0.5f, y + 26, 20, FAB_ALIGN_CENTER, buf, teamColor, 1 );
		FAB_Label( rightX + sideW * 0.5f, y + 36, 7, FAB_ALIGN_CENTER,
			HUD_TeamName( BG_TeamForIndex( leaderIndex ) ), fabDim, 1 );

		if ( cgs.teamCount > 2 ) {
			float teamCell = 52.0f;
			float start = cx - teamCell * cgs.teamCount * 0.5f;
			for ( i = 0; i < cgs.teamCount; i++ ) {
				BG_TeamColor( BG_TeamForIndex( i ), teamColor );
				FAB_Fill( start + i * teamCell + 2, y + sideH + 4, teamCell - 4, 2, teamColor, 1 );
				Com_sprintf( buf, sizeof( buf ), "%s %d",
					BG_TeamName( BG_TeamForIndex( i ) ), cgs.teamScores[i] );
				FAB_Label( start + i * teamCell + teamCell * 0.5f, y + sideH + 14, 5.5f,
					FAB_ALIGN_CENTER, buf, teamColor, 0.9f );
			}
		}
	} else {
		FAB_Fill( leftX, y, sideW, 2, accent, 1 );
		Com_sprintf( buf, sizeof( buf ), "%i", score );
		FAB_Text( leftX + sideW * 0.5f, y + 27, 26, FAB_ALIGN_CENTER, buf, fabWhite, 1 );
		FAB_Label( leftX + sideW * 0.5f, y + 37, 7, FAB_ALIGN_CENTER, leftLabel, accent, 1 );

		// rank out of everyone playing, like 1/15; Last Man Standing shows who is left
		if ( hud.lastManStanding ) {
			Com_sprintf( buf, sizeof( buf ), "%i", players );
			rightLabel = "REMAINING";
		} else {
			Com_sprintf( buf, sizeof( buf ), "%i/%i", rank, MAX( players, rank ) );
			rightLabel = tied ? "TIED" : "RANK";
			if ( rank == 1 ) {
				rightColor = fabAmber;
			}
		}
		// crowded matches make a wide number, so shrink it to fit the box
		size = FAB_TextWidth( buf, 22, 0 );
		size = size > sideW - 10 ? 22 * ( sideW - 10 ) / size : 22;
		FAB_Text( rightX + sideW * 0.5f, y + 27, size, FAB_ALIGN_CENTER, buf, rightColor, 1 );
		FAB_Label( rightX + sideW * 0.5f, y + 37, 7, FAB_ALIGN_CENTER, rightLabel, fabDim, 1 );
	}

	// the clock counts down to the time limit, or up without one
	secs = cg.time - cgs.levelStartTime;
	secs = secs > 0 ? secs / 1000 : 0;
	if ( cgs.timelimit > 0 && !hud.lastManStanding && !hud.leprechaun ) {
		secs = MAX( 0, cgs.timelimit * 60 - secs );
	}
	if ( hud.infected ) {
		int started = 0, survivors = 0, infected = 0, endTime = 0;
		sscanf( CG_ConfigString( CS_MODE_STATE ), "%i %i %i %i", &started, &survivors, &infected, &endTime );
		secs = started ? MAX( 0, ( endTime - cg.time + 999 ) / 1000 ) : 0;
	}
	Com_sprintf( buf, sizeof( buf ), "%i:%02i", secs / 60, secs % 60 );
	FAB_Text( cx, y + 21, 15, FAB_ALIGN_CENTER, buf, fabWhite, 1 );

	// Bug Hunt keeps its wave line under the strip; map and mode names are gone
	if ( hud.bugHunt ) {
		int started = 0, wave = 0, bugs = 0, nextWave = 0;
		sscanf( CG_ConfigString( CS_MODE_STATE ), "%i %i %i %i", &started, &wave, &bugs, &nextWave );
		if ( started ) {
			Com_sprintf( buf, sizeof( buf ), "WAVE %i   %i BUGS   %i %s", wave, bugs,
				MAX( 0, ps->persistant[PERS_LMS_LIVES] ), ps->persistant[PERS_LMS_LIVES] == 1 ? "LIFE" : "LIVES" );
			FAB_Label( cx, y + sideH + 12, 7, FAB_ALIGN_CENTER, buf, fabDim, 0.85f );
		}
	}
}

static void HUD_DrawFeed( float right, float y ) {
	int i, n;
	float size = 9.5f, lineH = 17;

	for ( n = 0, i = 0; i < HUD_FEED_LINES; i++ ) {
		// newest at the top
		hudFeedLine_t *line = &hud.feed[( hud.feedHead - 1 - i + HUD_FEED_LINES * 2 ) % HUD_FEED_LINES];
		float a, w, tw, aw, x, base;

		if ( !line->time ) {
			continue;
		}
		a = HUD_FadeOut( line->time, line->mine ? HUD_FEED_MINE_TIME : HUD_FEED_TIME, 500 );
		if ( a <= 0 ) {
			continue;
		}
		tw = FAB_TextWidth( line->target, size, 0 );
		aw = line->world ? 0 : FAB_TextWidth( line->attacker, size, 0 );
		w = tw + aw + ( line->world ? 29 : 35 );
		x = right - w;
		base = y + n * lineH;

		FAB_Panel( x, base, w, lineH - 3, 4, FAB_CUT_TL | FAB_CUT_BR,
			line->mine ? fabAmber : fabPanel, line->mine ? a * 0.30f : a );
		if ( !line->world ) {
			FAB_Name( x + 7, base + 10.5f, size, FAB_ALIGN_LEFT, line->attacker, a );
		}
		FAB_Color( line->world ? fabDim : fabRed, a );
		FAB_Pic( right - tw - 22, base + 1.5f, 11, 11, 0, 0, 1, 1, hud.skull );
		FAB_Name( right - 7, base + 10.5f, size, FAB_ALIGN_RIGHT, line->target, a );
		n++;
	}
}

#if FAB_VOICE_CHAT
/*
Who's talking: a speaker and the name of everyone whose voice chat is
playing, newest last, fading out shortly after they stop. Your own row
shows while you transmit, so you can tell push-to-talk is working.
*/
static void HUD_DrawSpeaker( float x, float y, float a ) {
	int i;

	FAB_Fill( x, y + 4, 3, 5, fabWhite, a );
	for ( i = 0; i < 3; i++ ) {
		FAB_Fill( x + 3 + i * 1.2f, y + 3 - i, 1.2f, 7 + i * 2, fabWhite, a );
	}
	// three level bars that bounce while the voice plays
	for ( i = 0; i < 3; i++ ) {
		float h = 3 + 6 * ( 0.5f + 0.5f * sin( cg.time * 0.018f + i * 2.1f ) );
		FAB_Fill( x + 8.5f + i * 2.4f, y + 6.5f - h * 0.5f, 1.4f, h, fabCyan, a );
	}
}

static void HUD_DrawVoice( float x, float y ) {
	int i, n = 0;
	float size = 9.0f, lineH = 16;

	for ( i = 0; i < cgs.maxclients && n < HUD_VOICE_LINES; i++ ) {
		int age;
		float a, w;
		qboolean self = i == cg.clientNum;

		if ( !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		age = trap_VoipTalkAge( i );
		if ( age < 0 || age >= HUD_VOICE_HOLD + HUD_VOICE_FADE ) {
			continue;
		}
		a = age < HUD_VOICE_HOLD ? 1.0f : 1.0f - ( age - HUD_VOICE_HOLD ) / (float)HUD_VOICE_FADE;
		w = FAB_TextWidth( cgs.clientinfo[i].name, size, 0 ) + 30;
		FAB_Panel( x, y + n * lineH, w, lineH - 3, 4, FAB_CUT_TL | FAB_CUT_BR,
			self ? fabCyan : fabPanel, self ? a * 0.25f : a * 0.85f );
		HUD_DrawSpeaker( x + 5, y + n * lineH, a );
		FAB_Name( x + 23, y + n * lineH + 9.5f, size, FAB_ALIGN_LEFT, cgs.clientinfo[i].name, a );
		n++;
	}
}

/*
A speaker over the head of everyone you can see talking, so you can tell
which of the players in front of you is the voice you hear. Players behind
walls get no marker; the list in the top left still names them.
*/
static void HUD_DrawVoiceMarkers( void ) {
	float tanX = tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	float tanY = tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	int i;

	for ( i = 0; i < cgs.maxclients && i < MAX_GENTITIES; i++ ) {
		centity_t *cent = &cg_entities[i];
		vec3_t head, d;
		trace_t tr;
		float fwd, px, py, x, y, a;
		int age;

		if ( i == cg.snap->ps.clientNum || !cent->currentValid || !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		age = trap_VoipTalkAge( i );
		if ( age < 0 || age >= HUD_VOICE_HOLD + HUD_VOICE_FADE ) {
			continue;
		}

		VectorCopy( cent->lerpOrigin, head );
		head[2] += 48;
		VectorSubtract( head, cg.refdef.vieworg, d );
		fwd = DotProduct( d, cg.refdef.viewaxis[0] );
		if ( fwd < 16 ) {
			continue;	// behind the camera
		}
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, head, cg.snap->ps.clientNum, CONTENTS_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}

		px = cg.refdef.x + cg.refdef.width * 0.5f * ( 1.0f - DotProduct( d, cg.refdef.viewaxis[1] ) / ( fwd * tanX ) );
		py = cg.refdef.y + cg.refdef.height * 0.5f * ( 1.0f - DotProduct( d, cg.refdef.viewaxis[2] ) / ( fwd * tanY ) );
		x = ( px - hud.xOffset ) / hud.scale;
		y = py / hud.scale;
		if ( x < 0 || x > hud.width || y < 0 || y > hud.height ) {
			continue;
		}

		a = age < HUD_VOICE_HOLD ? 1.0f : 1.0f - ( age - HUD_VOICE_HOLD ) / (float)HUD_VOICE_FADE;
		FAB_Panel( x - 10, y - 16, 20, 13, 3, FAB_CUT_TL | FAB_CUT_BR, fabPanel, a * 0.85f );
		HUD_DrawSpeaker( x - 7, y - 16, a );
	}
}
#endif

static void HUD_DrawChat( float x, float bottom ) {
	int i, n;
	float lineH = 15;

	for ( n = 0, i = 0; i < HUD_CHAT_LINES; i++ ) {
		// newest at the bottom
		hudChatLine_t *line = &hud.chat[( hud.chatHead - 1 - i + HUD_CHAT_LINES ) % HUD_CHAT_LINES];
		float a, base;

		a = HUD_FadeOut( line->time, HUD_CHAT_TIME, 600 );
		if ( a <= 0 ) {
			continue;
		}
		base = bottom - ( n + 1 ) * lineH;
		FAB_Panel( x, base, FAB_TextWidth( line->text, HUD_CHAT_SIZE, 0 ) + 14, lineH - 2, 3,
			FAB_CUT_TL | FAB_CUT_BR, fabPanel, a * 0.8f );
		FAB_Name( x + 7, base + 9.5f, HUD_CHAT_SIZE, FAB_ALIGN_LEFT, line->text, a );
		n++;
	}
}

/*
===============================================================================

CENTRE: CROSSHAIR, HIT MARKER, DAMAGE ARCS, KILL BANNER

===============================================================================
*/

static void HUD_DrawCrosshair( float cx, float cy ) {
	float size, f;
	int ca;

	if ( !cg_drawCrosshair.integer || cg.renderingThirdPerson ) {
		return;
	}
	size = cg_crosshairSize.value * 0.75f;	// 640-wide units to 480-tall units
	f = cg.time - cg.itemPickupBlendTime;
	if ( f > 0 && f < ITEM_BLOB_TIME ) {
		size *= 1 + 0.5f * f / ITEM_BLOB_TIME;
	}
	ca = cg_drawCrosshair.integer < 0 ? 0 : cg_drawCrosshair.integer;
	if ( cg_crosshairHealth.integer ) {
		vec4_t c;
		CG_ColorForHealth( c );
		trap_R_SetColor( c );
	} else {
		trap_R_SetColor( NULL );
	}
	FAB_Pic( cx - size * 0.5f, cy - size * 0.5f, size, size, 0, 0, 1, 1,
		cgs.media.crosshairShader[ca % NUM_CROSSHAIRS] );
	trap_R_SetColor( NULL );
}

static void HUD_DrawHitMarker( float cx, float cy ) {
	int t = cg.time - hud.hitTime;
	qboolean kill = ( hud.killTime == hud.hitTime && hud.killTime );
	int total = kill ? HUD_HIT_TIME * 3 : HUD_HIT_TIME;
	float a, size;

	if ( !hud.hitTime || t < 0 || t >= total ) {
		return;
	}
	a = 1.0f - t / (float)total;
	size = kill ? 30 + 8 * a : 20 + 4 * a;
	FAB_Color( kill ? fabRed : fabWhite, a );
	FAB_Pic( cx - size * 0.5f, cy - size * 0.5f, size, size, 0, 0, 1, 1, hud.hitmarker );
}

/*
Arcs of soft dots around the crosshair point at whoever hurt you. The source
direction is kept in world space so the arc swings as you turn toward it.
*/
static void HUD_DrawDamageArcs( float cx, float cy ) {
	int i, j;
	const float radius = 64;

	for ( i = 0; i < HUD_DAMAGE_ARCS; i++ ) {
		hudDamageArc_t *arc = &hud.arcs[i];
		float a, rel;

		a = HUD_FadeOut( arc->time, HUD_DAMAGE_TIME, 700 ) * arc->strength;
		if ( a <= 0 ) {
			continue;
		}
		// 0 = straight ahead (top of the circle), positive = to the left
		rel = DEG2RAD( AngleNormalize180( arc->yaw - cg.refdefViewAngles[YAW] ) );
		for ( j = -4; j <= 4; j++ ) {
			float ang = rel + j * 0.07f;
			float px = cx - sin( ang ) * radius;
			float py = cy - cos( ang ) * radius;
			float edge = 1.0f - abs( j ) / 5.0f;
			FAB_Glow( px, py, 14 + 6 * edge, fabRed, a * ( 0.35f + 0.65f * edge ) );
		}
	}
}

static void HUD_DrawKillBanner( float cx, float cy ) {
	float a = HUD_FadeOut( hud.killTime, HUD_KILL_TIME, 600 );
	float rise;
	const char *streak;

	if ( a <= 0 ) {
		return;
	}
	rise = 6 * ( 1.0f - Com_Clamp( 0, 1, ( cg.time - hud.killTime ) / 180.0f ) );

	switch ( hud.killStreak ) {
	case 1:	streak = hud.lastManStanding ? "FRAGGED" : "ELIMINATED"; break;
	case 2:	streak = "DOUBLE KILL"; break;
	case 3:	streak = "TRIPLE KILL"; break;
	case 4:	streak = "MULTI KILL"; break;
	default:	streak = "RAMPAGE"; break;
	}
	FAB_Fade( cx, cy + 50 + rise, 120, 34, qfalse, fabPanel, a * 0.8f );
	FAB_Fade( cx - 120, cy + 50 + rise, 120, 34, qtrue, fabPanel, a * 0.8f );
	FAB_Label( cx, cy + 62 + rise, 9, FAB_ALIGN_CENTER, streak,
		hud.killStreak > 1 ? fabAmber : fabRed, a );
	FAB_Name( cx, cy + 79 + rise, 13, FAB_ALIGN_CENTER, hud.killName, a );
}

static void HUD_DrawScreenEffects( void ) {
	int health = cg.snap->ps.stats[STAT_HEALTH];
	int maxHealth = cg.snap->ps.stats[STAT_MAX_HEALTH] > 0 ? cg.snap->ps.stats[STAT_MAX_HEALTH] : 100;
	float a = 0, low;

	// low-health vignette, breathing faster as health drops
	if ( health > 0 && health * 10 < maxHealth * 3 ) {
		low = 1.0f - health / ( maxHealth * 0.3f );
		a = 0.25f + 0.35f * low + 0.15f * HUD_Pulse( health * 4 < maxHealth ? 650 : 1100 );
	}
	a += 0.45f * HUD_FadeOut( hud.damageFlashTime, 450, 450 );
	if ( a <= 0 ) {
		return;
	}
	if ( a > 0.85f ) {
		a = 0.85f;
	}
	FAB_Color( fabRed, a );
	// the vignette covers the whole window, not just the safe area
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight,
		0, 0, 1, 1, hud.vignette );
	trap_R_SetColor( NULL );
}

/*
===============================================================================

DEATH, CENTRE PRINT, WARMUP

===============================================================================
*/

static void HUD_DrawDeath( float cx ) {
	int t, wait;
	float y = hud.height * 0.5f + 70;
	char buf[64];
	qboolean ready;

	if ( !hud.deathTime ) {
		hud.deathTime = cg.time;
	}
	t = cg.time - hud.deathTime;

	FAB_Fill( -hud.xOffset / hud.scale, y - 44, cgs.glconfig.vidWidth / hud.scale, 76, fabPanel,
		Com_Clamp( 0, 1, t / 300.0f ) * 0.9f );
	FAB_Fill( cx - 60, y - 44, 120, 2, fabRed, 1 );

	if ( cg.killerName[0] ) {
		FAB_Label( cx, y - 24, 9, FAB_ALIGN_CENTER, hud.lastManStanding ? "KILLED BY" : "ELIMINATED BY", fabRed, 1 );
		FAB_Name( cx, y + 2, 22, FAB_ALIGN_CENTER, cg.killerName, 1 );
	} else {
		FAB_Label( cx, y - 24, 9, FAB_ALIGN_CENTER, "SYSTEM FAILURE", fabRed, 1 );
		FAB_Text( cx, y + 2, 22, FAB_ALIGN_CENTER, "YOU DIED", fabWhite, 1 );
	}

	ready = t >= HUD_RESPAWN_DELAY;
	if ( ready ) {
		wait = hud.forceRespawn > 0 ? hud.forceRespawn * 1000 - t : 0;
		if ( wait > 0 ) {
			Com_sprintf( buf, sizeof( buf ), "PRESS FIRE TO RESPAWN   AUTO IN %i", wait / 1000 + 1 );
		} else {
			Q_strncpyz( buf, "PRESS FIRE TO RESPAWN", sizeof( buf ) );
		}
		FAB_Label( cx, y + 22, 8.5f, FAB_ALIGN_CENTER, buf, fabWhite, 0.6f + 0.4f * HUD_Pulse( 1200 ) );
	} else {
		HUD_Bar( cx - 60, y + 16, 120, 3, t / (float)HUD_RESPAWN_DELAY, 0, 0, fabDim, 1 );
	}
}

static void HUD_DrawCenterPrint( float cx ) {
	float a, y;
	const char *s;
	char line[256];
	int l;

	if ( !cg.centerPrintTime ) {
		return;
	}
	a = HUD_FadeOut( cg.centerPrintTime, 1000 * cg_centertime.value, FADE_TIME );
	if ( a <= 0 ) {
		return;
	}
	y = 150 - cg.centerPrintLines * 9;
	s = cg.centerPrint;
	while ( *s ) {
		for ( l = 0; s[l] && s[l] != '\n' && l < (int)sizeof( line ) - 1; l++ ) {
			line[l] = s[l];
		}
		line[l] = 0;
		FAB_TextExt( cx, y, 13, 0.5f, FAB_ALIGN_CENTER, line, fabWhite, a, qtrue );
		y += 18;
		s += l;
		while ( *s && *s != '\n' ) {
			s++;
		}
		if ( *s == '\n' ) {
			s++;
		}
	}
}

static void HUD_DrawWarmup( float cx ) {
	int sec;
	char buf[64];

	if ( !cg.warmup ) {
		return;
	}
	if ( cg.warmup < 0 ) {
		FAB_Label( cx, 100, 11, FAB_ALIGN_CENTER, "WAITING FOR PLAYERS", fabWhite, 0.6f + 0.4f * HUD_Pulse( 1600 ) );
		cg.warmupCount = 0;
		return;
	}
	sec = ( cg.warmup - cg.time ) / 1000;
	if ( sec < 0 ) {
		cg.warmup = 0;
		sec = 0;
	}
	if ( sec != cg.warmupCount ) {
		cg.warmupCount = sec;
		if ( sec == 0 ) trap_S_StartLocalSound( cgs.media.count1Sound, CHAN_ANNOUNCER );
		else if ( sec == 1 ) trap_S_StartLocalSound( cgs.media.count2Sound, CHAN_ANNOUNCER );
		else if ( sec == 2 ) trap_S_StartLocalSound( cgs.media.count3Sound, CHAN_ANNOUNCER );
	}
	FAB_Label( cx, 100, 10, FAB_ALIGN_CENTER, "MATCH STARTS IN", fabDim, 1 );
	Com_sprintf( buf, sizeof( buf ), "%i", sec + 1 );
	FAB_Text( cx, 140, 40, FAB_ALIGN_CENTER, buf, fabWhite, 1 );
}

/*
===============================================================================

SCOREBOARD

===============================================================================
*/

#define HUD_SCORE_ROWS	14

static void HUD_ScoreRow( float x, float y, float w, int place, const score_t *sc, qboolean self ) {
	clientInfo_t *ci = &cgs.clientinfo[sc->client];
	const float rowH = 19;
	char buf[32];
	vec4_t accent;

	HUD_Accent( accent );
	if ( self ) {
		FAB_Fill( x, y, w, rowH - 2, accent, 0.18f );
		FAB_Fill( x, y, 3, rowH - 2, accent, 1 );
	} else {
		FAB_Fill( x, y, w, rowH - 2, fabPanelLight, place & 1 ? 0.35f : 0.6f );
	}
	Com_sprintf( buf, sizeof( buf ), "%i", place );
	FAB_Text( x + 26, y + 13, 10, FAB_ALIGN_RIGHT, buf, place == 1 ? fabAmber : fabDim, 1 );
	FAB_TextExt( x + 40, y + 13, 10, 0, FAB_ALIGN_LEFT, ci->name, fabWhite, 1, qtrue );
	if ( ci->botSkill > 0 ) {
		FAB_Label( x + w - 160, y + 12.5f, 6.5f, FAB_ALIGN_LEFT, "BOT", fabDim, 0.7f );
	}
	Com_sprintf( buf, sizeof( buf ), "%i", sc->score );
	if ( hud.lastManStanding && sc->score <= 0 ) Q_strncpyz( buf, sc->score == 0 ? "OUT" : "WAIT", sizeof( buf ) );
	FAB_Text( x + w - 90, y + 13, 11, FAB_ALIGN_RIGHT, buf, fabWhite, 1 );
	Com_sprintf( buf, sizeof( buf ), "%i", sc->ping );
	FAB_Text( x + w - 14, y + 13, 9, FAB_ALIGN_RIGHT, buf, fabDim, 1 );
}

static void HUD_DrawScoreboard( float cx ) {
	const float w = 470, rowH = 19;
	float x = cx - w * 0.5f, y = 86;
	int i, rows, selfIndex = -1, self = cg.snap->ps.clientNum;
	char buf[96];
	vec4_t accent;

	// keep the board fresh while it is held open
	if ( cg.scoresRequestTime + 2000 < cg.time ) {
		cg.scoresRequestTime = cg.time;
		trap_SendClientCommand( "score" );
	}

	HUD_Accent( accent );
	rows = cg.numScores < HUD_SCORE_ROWS ? cg.numScores : HUD_SCORE_ROWS;
	for ( i = 0; i < cg.numScores; i++ ) {
		if ( cg.scores[i].client == self ) {
			selfIndex = i;
		}
	}

	FAB_Panel( x - 10, y - 10, w + 20, 62 + ( rows + ( selfIndex >= rows ? 1 : 0 ) ) * rowH + 18,
		12, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.92f );
	FAB_Fill( x - 10 + 12, y - 10, w + 8, 2, accent, 1 );

	FAB_Text( x, y + 18, 18, FAB_ALIGN_LEFT, hud.mapTitle, fabWhite, 1 );
	Com_sprintf( buf, sizeof( buf ), "%s   %i PLAYERS", HUD_ModeName(),
		cg.totalScores > 0 ? cg.totalScores : HUD_PlayerCount() );
	FAB_Label( x, y + 32, 7.5f, FAB_ALIGN_LEFT, buf, fabDim, 1 );
	if ( hud.lastManStanding ) {
		FAB_Label( x + w, y + 18, 8, FAB_ALIGN_RIGHT, "SURVIVAL", fabAmber, 1 );
	} else if ( hud.killshift && cgs.gametype == GT_TEAM ) {
		FAB_Label( x + w, y + 18, 8, FAB_ALIGN_RIGHT, "TAKEOVER", fabAmber, 1 );
	} else if ( hud.infected ) {
		FAB_Label( x + w, y + 18, 8, FAB_ALIGN_RIGHT, "OUTLAST THE HORDE", fabAmber, 1 );
	} else if ( hud.bugHunt ) {
		FAB_Label( x + w, y + 18, 8, FAB_ALIGN_RIGHT, "LAST SQUAD STANDING", fabAmber, 1 );
	} else if ( hud.leprechaun || cgs.fraglimit > 0 ) {
		Com_sprintf( buf, sizeof( buf ), "FIRST TO %i",
			hud.leprechaun ? BG_LeprechaunScoreLimit( cgs.fraglimit ) : cgs.fraglimit );
		FAB_Label( x + w, y + 18, 8, FAB_ALIGN_RIGHT, buf, fabAmber, 1 );
	}

	y += 44;
	FAB_Label( x + 40, y + 6, 7, FAB_ALIGN_LEFT, "PLAYER", fabDim, 1 );
	FAB_Label( x + w - 90, y + 6, 7, FAB_ALIGN_RIGHT, hud.leprechaun ? "POINTS" : hud.lastManStanding ? "LIVES" : "FRAGS", fabDim, 1 );
	FAB_Label( x + w - 14, y + 6, 7, FAB_ALIGN_RIGHT, "PING", fabDim, 1 );
	y += 12;

	if ( !cg.numScores ) {
		FAB_Label( cx, y + 16, 8, FAB_ALIGN_CENTER, "WAITING FOR SCORES", fabDim, 1 );
		return;
	}
	for ( i = 0; i < rows; i++ ) {
		HUD_ScoreRow( x, y, w, i + 1, &cg.scores[i], cg.scores[i].client == self );
		y += rowH;
	}
	// the server always includes the local player even below the first page
	if ( selfIndex >= rows ) {
		FAB_Label( cx, y + 6, 7, FAB_ALIGN_CENTER, ". . .", fabDim, 1 );
		y += 10;
		HUD_ScoreRow( x, y, w, ( cg.snap->ps.persistant[PERS_RANK] & ~RANK_TIED_FLAG ) + 1,
			&cg.scores[selfIndex], qtrue );
	}
}

/*
===============================================================================

FPS

===============================================================================
*/

static void HUD_DrawFPS( float right ) {
	static int previous, times[8], index;
	int t = trap_Milliseconds(), i, total = 0;
	char buf[32];

	times[index++ & 7] = t - previous;
	previous = t;
	if ( ( !cg_drawFPS.integer && !cl_diagnostics.integer ) || index < 8 ) {
		return;
	}
	for ( i = 0; i < 8; i++ ) {
		total += times[i];
	}
	Com_sprintf( buf, sizeof( buf ), "%i FPS", total ? 8000 / total : 0 );
	FAB_Label( right, 10, 7, FAB_ALIGN_RIGHT, buf, fabDim, 0.8f );
}

/*
===============================================================================

ENTRY POINT

===============================================================================
*/

static void HUD_Layout( void ) {
	float vw = cgs.glconfig.vidWidth, vh = cgs.glconfig.vidHeight;
	float aspect = vw / vh;
	float used = aspect > HUD_MAX_ASPECT ? HUD_MAX_ASPECT : aspect;

	// cg_hudScale enlarges or shrinks every element; the layout stays edge-anchored
	hud.scale = vh / HUD_HEIGHT * Com_Clamp( 0.5f, 2.0f, cg_hudScale.value );
	hud.height = vh / hud.scale;
	hud.width = used * vh / hud.scale;
	hud.xOffset = ( vw - used * vh ) * 0.5f;
	FAB_SetLayout( hud.scale, hud.xOffset );
}

void CG_HudDraw( stereoFrame_t stereoFrame ) {
	playerState_t *ps = &cg.snap->ps;
	float cx, cy, bottom, right;
	qboolean alive = ps->stats[STAT_HEALTH] > 0;
	qboolean spectator = ps->persistant[PERS_TEAM] == TEAM_SPECTATOR;

	// the pause menu dims the game and has its own panel; keep the HUD out of its way
	if ( trap_Key_GetCatcher() & KEYCATCH_UI ) {
		return;
	}

	HUD_Layout();
	HUD_UpdateServerInfo();

	right = hud.width - HUD_MARGIN;
	bottom = hud.height - HUD_MARGIN;
	cx = hud.width * 0.5f;
	cy = hud.height * 0.5f;

	if ( ps->pm_type == PM_INTERMISSION ) {
		HUD_DrawScoreboard( cx );
		return;
	}

	if ( alive ) {
		hud.deathTime = 0;
	}

	HUD_DrawScreenEffects();

	if ( !spectator && alive ) {
		if ( stereoFrame == STEREO_CENTER ) {
			HUD_DrawCrosshair( cx, cy );
			HUD_DrawHitMarker( cx, cy );
		}
		HUD_DrawDamageArcs( cx, cy );
		HUD_DrawKillBanner( cx, cy );
	}

	if ( cg.showScores ) {
		HUD_DrawScoreboard( cx );
	} else {
		HUD_DrawMatch();
		HUD_DrawFeed( right, 16 );
	}
	HUD_DrawFPS( right );
#if FAB_VOICE_CHAT
	if ( !cg.showScores && !cg.renderingThirdPerson ) {
		HUD_DrawVoiceMarkers();
	}
	HUD_DrawVoice( HUD_MARGIN, 16 );
#endif


	if ( !cg.showScores ) {
		HUD_DrawChat( HUD_MARGIN, spectator ? bottom : bottom - 48 - 30 );
	}

	if ( spectator ) {
		FAB_Label( cx, bottom - 10, 10, FAB_ALIGN_CENTER,
			hud.lastManStanding || hud.bugHunt ? "SPECTATING / NEXT ROUND" : "SPECTATING", fabDim, 1 );
	} else {
		HUD_DrawClass( HUD_MARGIN, bottom - 48 - 26 );
		HUD_DrawHealth( HUD_MARGIN, bottom - 48 );
		HUD_DrawPickup( right, HUD_DrawWeaponColumn( right, bottom ) - 4 );
		if ( !alive ) {
			HUD_DrawDeath( cx );
		}
	}

	HUD_DrawWarmup( cx );
	if ( hud.leprechaun && !cg.warmup && !cg.showScores ) {
		int holder = atoi( CG_ConfigString( CS_MODE_STATE ) ) - 1;
		const char *objective = "CHOOSING THE LEPRECHAUN";

		if ( cg.snap->ps.eFlags & EF_LEPRECHAUN && alive && !spectator ) {
			objective = "YOU ARE THE LEPRECHAUN / +1 POINT EVERY 3 SEC";
		} else if ( holder >= 0 && holder < cgs.maxclients && cgs.clientinfo[holder].infoValid ) {
			objective = va( "HUNT THE LEPRECHAUN / %s", cgs.clientinfo[holder].name );
		}
		FAB_Label( cx, 110, 10, FAB_ALIGN_CENTER, objective, fabAmber, 1 );
	}
	if ( hud.lastManStanding && !cg.warmup && !cg.showScores ) {
		int started = 0, remaining = 0, startTime = 0;
		char buf[48];
		sscanf( CG_ConfigString( CS_LMS_STATE ), "%i %i %i", &started, &remaining, &startTime );
		if ( !started ) {
			if ( remaining < 2 ) Q_strncpyz( buf, "WAITING FOR OPPONENT", sizeof( buf ) );
			else Com_sprintf( buf, sizeof( buf ), "ROUND STARTS IN %i", MAX( 1, ( startTime - cg.time + 999 ) / 1000 ) );
			FAB_Label( cx, 110, 10, FAB_ALIGN_CENTER, buf, fabAmber, 1 );
		}
	}
	if ( hud.infected && !cg.warmup && !cg.showScores ) {
		int started = 0, survivors = 0, infected = 0, startTime = 0;
		char buf[48];
		sscanf( CG_ConfigString( CS_MODE_STATE ), "%i %i %i %i", &started, &survivors, &infected, &startTime );
		if ( !started ) {
			if ( survivors < 2 ) Q_strncpyz( buf, "WAITING FOR PLAYERS", sizeof( buf ) );
			else Com_sprintf( buf, sizeof( buf ), "INFECTION IN %i", MAX( 1, ( startTime - cg.time + 999 ) / 1000 ) );
			FAB_Label( cx, 110, 10, FAB_ALIGN_CENTER, buf, fabAmber, 1 );
		}
	}
	if ( hud.bugHunt && !cg.warmup && !cg.showScores ) {
		int started = 0, wave = 0, bugs = 0, nextWave = 0;
		char buf[48];
		sscanf( CG_ConfigString( CS_MODE_STATE ), "%i %i %i %i", &started, &wave, &bugs, &nextWave );
		if ( !started ) {
			if ( !nextWave ) Q_strncpyz( buf, "WAITING FOR SQUADS", sizeof( buf ) );
			else Com_sprintf( buf, sizeof( buf ), "BUGS INBOUND IN %i", MAX( 1, ( nextWave - cg.time + 999 ) / 1000 ) );
			FAB_Label( cx, 110, 10, FAB_ALIGN_CENTER, buf, fabAmber, 1 );
		} else if ( nextWave > cg.time ) {
			Com_sprintf( buf, sizeof( buf ), "WAVE %i IN %i", wave + 1, MAX( 1, ( nextWave - cg.time + 999 ) / 1000 ) );
			FAB_Label( cx, 110, 10, FAB_ALIGN_CENTER, buf, fabAmber, 1 );
		}
	}
	if ( !cg.showScores ) {
		HUD_DrawCenterPrint( cx );
	}
	trap_R_SetColor( NULL );
}

#endif // STANDALONE
