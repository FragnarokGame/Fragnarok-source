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
//
/*
=======================================================================

MAIN MENU

=======================================================================
*/


#include "ui_local.h"
#include "../game/bg_campaign.h"


#define ID_SINGLEPLAYER			10
#define ID_MULTIPLAYER			11
#define ID_SETUP				12
#define ID_DEMOS				13
#define ID_CINEMATICS			14
#define ID_TEAMARENA		15
#define ID_MODS					16
#define ID_EXIT					17
#define ID_ACCOUNT                  18
#define ID_SP_QUICK_PLAY			19
#define ID_SP_CAMPAIGN				20
#define ID_SP_BACK					22
#define ID_SP_MISSION_START			23
#define ID_SP_MISSION_LEAVE			24
#define ID_LEVEL_RANDOMIZE		25
#define ID_DISCORD				26
#define ID_ACCOUNT_RETRY            90
#define ID_ACCOUNT_BACK             93
#define ID_LEVEL_ARENA			30
#define ID_LEVEL_POPULATION		31
#define ID_LEVEL_GAMETYPE		32
#define ID_LEVEL_START			33
#define ID_LEVEL_BACK			34
#define ID_LEVEL_BOTS			35
#define ID_LEVEL_TEAMS			36
#define ID_LEVEL_SCORELIMIT		37
#define ID_LEVEL_LIVES			38
#define ID_LEVEL_LOADOUT		39
#define ID_LEVEL_AMMO			49
#define ID_MP_HOST				40
#define ID_MP_FIND				41
#define ID_MP_BACK				42
#define ID_LAN_REFRESH			43
#define ID_LAN_ADDRESS			44
#define ID_LAN_CONNECT			45
#define ID_LAN_BACK				46
#define ID_MP_ONLINE				47
#define ID_MP_LAN				48
#define ID_LAN_SERVER			50	// one id per visible row
#define ID_ONLINE_REFRESH		60
#define ID_ONLINE_BACK			61
#define ID_ONLINE_HOST			62
#define ID_ONLINE_PREV			63
#define ID_ONLINE_NEXT			64
#define ID_ONLINE_SERVER			70

#define LAN_MAX_ROWS			8
#define LAN_RESCAN_MSEC			8000
#define LAN_UPDATE_MSEC			500
#define LAN_ADDRESS_LENGTH		64
#define ONLINE_MAX_SERVERS		1024
#define ONLINE_MAX_ROWS			8
#define ONLINE_RESCAN_MSEC		30000
#define ONLINE_TIMEOUT_MSEC		3000
#define ONLINE_W					500
#define ONLINE_ROW_Y				126
#define ONLINE_ROW_H				30

#define MAIN_BANNER_MODEL				"models/mapobjects/banner/banner5.md3"
#define MAIN_MENU_VERTICAL_SPACING		34


typedef struct {
	menuframework_s	menu;

	menutext_s		singleplayer;
	menutext_s		multiplayer;
	menutext_s		setup;
	menutext_s		demos;
	menutext_s		cinematics;
	menutext_s		teamArena;
	menutext_s		mods;
	menutext_s		exit;
    menutext_s      account;
	menutext_s		discord;

	qhandle_t		bannerModel;
} mainmenu_t;


static mainmenu_t s_main;

#ifdef STANDALONE
#define LEVEL_MAX_ARENAS	32

typedef struct {
	menuframework_s menu;
	menulist_s arena;
	menulist_s gametype;
	menulist_s population;
	menulist_s bots;
	menulist_s teams;
	menufield_s scoreLimit;
	menulist_s lives;
	menulist_s loadout;
	menulist_s ammo;
	menutext_s randomize;
	menutext_s start;
	menutext_s back;
	qboolean host;
	qboolean online;
	qboolean inMatch;	// opened from the pause menu
	// backdrop levelshot: the selected map's fades in over the previous one
	qhandle_t shots[LEVEL_MAX_ARENAS];
	int shotArena;
	int shotPrevArena;
	int shotChangeTime;
	int lastPopulation;
	char currentMatch[96];
	// MAP row choices: the maps that offer the selected match type, the ones
	// built for it first. arena.curvalue indexes these.
	int mapList[LEVEL_MAX_ARENAS];
	const char *mapListNames[LEVEL_MAX_ARENAS + 1];
	int mapListGametype;	// match type the list was built for
	// the map the player last picked; a match type that can't run it moves
	// the MAP row elsewhere, and switching back returns to it
	int chosenArena;
	int syncedArena;	// MAP row after the last list rebuild or pick
	// first row shown in the scrolling map list on the right, and the pick it
	// last scrolled to so a new pick from the dropdown comes into view
	int listScroll;
	int listShownPick;
} levelselect_t;

static levelselect_t s_levelselect;
// Play screen maps, read from arenas.txt by LevelSelect_LoadArenas in file order
static char levelArenaMapBuf[LEVEL_MAX_ARENAS][MAX_QPATH];
static char levelArenaNameBuf[LEVEL_MAX_ARENAS][48];
static const char *levelArenaMaps[LEVEL_MAX_ARENAS];
static const char *levelArenaNames[LEVEL_MAX_ARENAS + 1];
static int levelNumArenas;
#define LEVEL_NUM_ARENAS	levelNumArenas
#define LEVEL_QUICK_PLAY_MAX_PLAYERS 512
static const char *levelPopulationNames[] = {
	"2", "4", "8", "16", "32", "64", "128", "256", "512", "1024", NULL
};
static const int levelPopulationValues[] = {
	2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
};
static const char *levelLivesNames[] = {
	"1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15", "16", NULL
};
static const char *levelGametypeNames[] = {
	"DEATHMATCH", "DAVID AND GOLIATH", "TEAM DEATHMATCH", "KILLSHIFT", "LAST MAN STANDING",
	"INFECTED", "BUG HUNT", "KILL THE LEPRECHAUN", "ARMS RACE", NULL
};
#define LEVEL_GAMETYPE_DG		1
#define LEVEL_GAMETYPE_TEAM		2
#define LEVEL_GAMETYPE_KILLSHIFT	3
#define LEVEL_GAMETYPE_LMS		4
#define LEVEL_GAMETYPE_INFECTED	5
#define LEVEL_GAMETYPE_BUGHUNT	6
#define LEVEL_GAMETYPE_LEPRECHAUN	7
#define LEVEL_GAMETYPE_ARMSRACE	8
#define LEVEL_NUM_GAMETYPES		9
// arenas.txt "modes" / "best" tokens, in levelGametypeNames order
static const char *levelGametypeTokens[] = { "dm", "dg", "tdm", "ks", "lms", "inf", "bug", "ktl", "ar" };

static int LevelSelect_Arena( void );
static void LevelSelect_ShowArena( void );
#define LEVELSHOT_FADE_MSEC	260
#define LEVELSHOT_ASPECT	( 16.0f / 9.0f )

#define LEVEL_FIT_NONE			0	// arenas.txt "modes" leaves it out
#define LEVEL_FIT_PLAYABLE		1
#define LEVEL_FIT_BEST			2

#define MAPLIST_W				250
#define MAPLIST_ROW_H			30
#define MAPLIST_TOP				150
#define MAPLIST_VISIBLE			5
static const char *levelBotsNames[] = {
	"OFF", "FILL SLOTS", NULL
};
// With FAB_PLAYER_CLASSES the first preset enables class selection; the others
// use a shared loadout. Without it the class preset is left out of the list.
static const char *levelLoadoutNames[] = {
#if FAB_PLAYER_CLASSES
	"CLASS LOADOUTS",
#endif
	"PISTOL + RIFLE", "PISTOL ONLY", "FULL ARSENAL", "ROCKET ARENA",
	"ODIN'S SNEEZE", NULL
};
static const char *levelLoadoutWeapons[] = {
#if FAB_PLAYER_CLASSES
	"pistol rifle",
#endif
	"pistol rifle", "pistol", "all", "pistol rockets", "odin-only"
};
// First shared preset, and how far the list is shifted from the saved
// ui_matchLoadout numbering, which always counts CLASS LOADOUTS as 0.
#define LEVEL_LOADOUT_SHARED	( FAB_PLAYER_CLASSES ? 1 : 0 )
#define LEVEL_LOADOUT_SAVED_OFFSET	( FAB_PLAYER_CLASSES ? 0 : 1 )
// Single Player only. Keep this last so hosted matches can omit it.
#define LEVEL_LOADOUT_ODIN_SNEEZE ( ARRAY_LEN( levelLoadoutWeapons ) - 1 )
static const char *levelAmmoNames[] = {
	"NORMAL", "INFINITE", NULL
};
static const char *levelTeamCountNames[] = {
	"2", "3", "4", NULL
};
static const char *levelTeamNames[] = {
	"2  RED / BLUE", "3  + GREEN", "4  + YELLOW", NULL
};
#define LEVEL_SCORE_DEFAULT	16
#define LEVEL_SCORE_MAX		1024
#define LEVEL_RANDOM_SCORE_MIN	10
#define LEVEL_RANDOM_SCORE_MAX	50

typedef struct {
	menuframework_s menu;
	menutext_s host;
	menutext_s find;
	menutext_s back;
} multiplayermenu_t;

static multiplayermenu_t s_multiplayer;
static multiplayermenu_t s_lanmenu;

typedef struct {
	menuframework_s menu;
	menutext_s rows[LAN_MAX_ROWS];
	menufield_s address;
	menutext_s connect;
	menutext_s refresh;
	menutext_s back;
	int numServers;
	int lastScan;
	int lastUpdate;
	char status[64];
	char rowText[LAN_MAX_ROWS][128];
	char rowAddress[LAN_MAX_ROWS][LAN_ADDRESS_LENGTH];
} lanbrowser_t;

static lanbrowser_t s_lanbrowser;

typedef struct {
	char address[LAN_ADDRESS_LENGTH];
	char lobby[LAN_ADDRESS_LENGTH];
	int matchSlot;
	char title[128];
	char detail[160];
	int ping;
	qboolean live;
	qboolean pending;
	qboolean queried;
	int started;
} onlinematch_t;

typedef struct {
	menuframework_s menu;
	menutext_s rows[ONLINE_MAX_ROWS];
	menutext_s refresh;
	menutext_s back;
	menutext_s host;
	menutext_s previous;
	menutext_s next;
	onlinematch_t matches[ONLINE_MAX_SERVERS];
	int rowIndex[ONLINE_MAX_ROWS];
	int page;
	int directoryCount;
	int numConfigured;
	int numAddresses;
	int numLive;
	int lastScan;
	qboolean refreshing;
	qboolean focusFirstMatch;
	char status[80];
} onlinebrowser_t;

static onlinebrowser_t s_onlinebrowser;

static struct {
	menuframework_s menu;
	menutext_s any, first, second, leave;
} s_matchwaiting;

void UI_LevelSelectMenu( void );
#ifdef STANDALONE
static void UI_SinglePlayerMenu( void );
#endif
static void UI_MultiplayerMenu( void );
static void UI_LanMenu( void );
static void UI_LanBrowserMenu( void );
static void UI_OnlineBrowserMenu( void );

// every FRAGNAROK menu screen lines its column up with the left screen edge
#define FAB_MENU_W		300
static float FabMenu_X( void ) {
	return FabUI_Left() + 44;
}

/* Online sign-in is Steam's: this screen only shows who you're signed in as,
   or why you aren't, and lets you retry. */
typedef struct {
    menuframework_s menu;
    menutext_s retry, back;
    uiAuthStatus_t status;
    qboolean continueOnline;
} accountmenu_t;

static accountmenu_t s_account;

static void Account_Event( void *ptr, int event ) {
    if ( event != QM_ACTIVATED ) return;
    if ( ((menucommon_s *)ptr)->id == ID_ACCOUNT_BACK ) {
        s_account.continueOnline = qfalse;
        UI_PopMenu();
    } else {
        trap_AuthRetry();
    }
}

static sfxHandle_t Account_Key( int key ) {
    if ( key == K_ESCAPE || key == K_MOUSE2 ) s_account.continueOnline = qfalse;
    return Menu_DefaultKey( &s_account.menu, key );
}

static void Account_Draw( void ) {
    float x = 90;
    trap_AuthStatus( &s_account.status );
    if ( s_account.continueOnline && s_account.status.signedIn && !s_account.status.busy ) {
        s_account.continueOnline = qfalse;
        UI_PopMenu();
        UI_OnlineBrowserMenu();
        return;
    }
    s_account.retry.generic.flags = QMF_NODEFAULTINIT;
    if ( s_account.status.signedIn ) s_account.retry.generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
    else if ( s_account.status.busy || !s_account.status.configured ) s_account.retry.generic.flags |= QMF_GRAYED;
    if ( Menu_ItemAtCursor( &s_account.menu ) == (void *)&s_account.retry &&
        ( s_account.retry.generic.flags & ( QMF_GRAYED | QMF_INACTIVE ) ) )
        Menu_SetCursorToItem( &s_account.menu, &s_account.back );

    FabUI_Begin();
    FabUI_Dim( 0.65f );
    FAB_Panel( x - 24, 48, 508, 300, 12, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.98f );
    FabUI_Header( x, 100, "FRAGNAROK", "ACCOUNT",
        s_account.continueOnline ? "SIGN IN WITH STEAM TO PLAY ONLINE" : NULL );
    if ( s_account.status.signedIn ) {
        FAB_Text( x, 178, 18, FAB_ALIGN_LEFT, s_account.status.displayName, fabWhite, 1 );
        FAB_Label( x, 200, 7, FAB_ALIGN_LEFT, "SIGNED IN WITH STEAM", fabCyan, 1 );
        FabUI_Wrapped( x, 226, 460, 8, 13, FAB_ALIGN_LEFT,
            "Your name, match history and ranks follow your Steam account. Change your Steam name to change it here.",
            fabDim, 1 );
    } else {
        FAB_Label( x, 166, 7, FAB_ALIGN_LEFT, "NOT SIGNED IN", fabAmber, 1 );
        FabUI_Wrapped( x, 186, 460, 9, 15, FAB_ALIGN_LEFT, s_account.status.message, fabCyan, 1 );
        FabUI_Wrapped( x, 236, 460, 8, 13, FAB_ALIGN_LEFT,
            "Single player and LAN matches don't need Steam.", fabDim, 1 );
    }
    Menu_Draw( &s_account.menu );
}

static void UI_AccountMenu( void ) {
    float x = 90;
    memset( &s_account, 0, sizeof( s_account ) );
    s_account.menu.draw = Account_Draw;
    s_account.menu.key = Account_Key;
    s_account.menu.fullscreen = qfalse;
    s_account.menu.wrapAround = qtrue;
    s_account.menu.showlogo = qfalse;
    FabUI_InitButton( &s_account.retry, x, 290, 220, 30, "RETRY", ID_ACCOUNT_RETRY, Account_Event,
        FAB_BUTTON_PRIMARY | FAB_BUTTON_LEFT );
    FabUI_InitButton( &s_account.back, x + 240, 290, 220, 30, "BACK", ID_ACCOUNT_BACK, Account_Event, FAB_BUTTON_LEFT );
    Menu_AddItem( &s_account.menu, &s_account.retry );
    Menu_AddItem( &s_account.menu, &s_account.back );
    trap_AuthStatus( &s_account.status );
    UI_PushMenu( &s_account.menu );
    Menu_SetCursorToItem( &s_account.menu, s_account.status.signedIn ? (void *)&s_account.back : (void *)&s_account.retry );
}

static qboolean UI_RequireOnlineLogin( void ) {
    uiAuthStatus_t status;
    trap_AuthStatus( &status );
    if ( status.signedIn ) return qtrue;
    UI_AccountMenu();
    s_account.continueOnline = qtrue;
    Com_Printf( "Online: Steam sign-in required.\n" );
    return qfalse;
}
#endif

typedef struct {
	menuframework_s menu;	
	char errorMessage[4096];
} errorMessage_t;

static errorMessage_t s_errorMessage;

/*
=================
MainMenu_ExitAction
=================
*/
static void MainMenu_ExitAction( qboolean result ) {
	if( !result ) {
		return;
	}
	UI_PopMenu();
#ifdef STANDALONE
	trap_Cmd_ExecuteText( EXEC_APPEND, "quit\n" );
#else
	UI_CreditMenu();
#endif
}



/*
=================
Main_MenuEvent
=================
*/
void Main_MenuEvent (void* ptr, int event) {
	if( event != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_SINGLEPLAYER:
#ifdef STANDALONE
		UI_SinglePlayerMenu();
#else
		UI_SPLevelMenu();
#endif
		break;

	case ID_MULTIPLAYER:
#ifdef STANDALONE
		UI_MultiplayerMenu();
#else
		UI_ArenaServersMenu();
#endif
		break;

	case ID_SETUP:
		UI_SetupMenu();
		break;

#ifdef STANDALONE
    case ID_ACCOUNT:
        UI_AccountMenu();
        break;

	case ID_DISCORD:
		// the website redirects to the current invite, so it isn't baked into the game
		trap_Cmd_ExecuteText( EXEC_APPEND, "fab_openlink discord\n" );
		break;
#endif

	case ID_DEMOS:
		UI_DemosMenu();
		break;

	case ID_CINEMATICS:
		UI_CinematicsMenu();
		break;

	case ID_MODS:
		UI_ModsMenu();
		break;

	case ID_TEAMARENA:
		trap_Cvar_Set( "fs_game", BASETA);
		trap_Cmd_ExecuteText( EXEC_APPEND, "vid_restart;" );
		break;

	case ID_EXIT:
		UI_ConfirmMenu( "EXIT GAME?", 0, MainMenu_ExitAction );
		break;
	}
}


/*
===============
MainMenu_Cache
===============
*/
void MainMenu_Cache( void ) {
	s_main.bannerModel = trap_R_RegisterModel( MAIN_BANNER_MODEL );
}

sfxHandle_t ErrorMessage_Key(int key)
{
	trap_Cvar_Set( "com_errorMessage", "" );
	UI_MainMenu();
	return (menu_null_sound);
}

/*
===============
Main_MenuDraw
TTimo: this function is common to the main menu and errorMessage menu
===============
*/

#ifdef STANDALONE
// The backdrop map is starting up for the first time: hold the boot screen.
static qboolean Main_BootHeld( void ) {
	return ( trap_Cvar_VariableValue( "cl_menuBoot" ) || trap_Cvar_VariableValue( "cl_menuLoading" ) ) &&
		trap_Cvar_VariableValue( "g_menuBackground" );
}

// The backdrop map is reloading under a menu that is already up (the live match
// ended). Keep the menu showing over a still of the map that is coming back.
static void Main_DrawTransitionBackdrop( void ) {
	vec4_t black = { 0, 0, 0, 1 };
	float left = FabUI_Left(), w = FabUI_Right() - left;
	float s1 = 0, t1 = 0, s2 = 1, t2 = 1;
	uiClientState_t cstate;
	char info[MAX_INFO_STRING], mapname[MAX_QPATH] = "";
	qhandle_t shot = 0;

	// the map being loaded: a local server's cvar, or the remote gamestate once it is in
	trap_GetClientState( &cstate );
	if ( trap_Cvar_VariableValue( "sv_running" ) ) {
		trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	} else if ( cstate.connState >= CA_CONNECTED && trap_GetConfigString( CS_SERVERINFO, info, sizeof( info ) ) ) {
		Q_strncpyz( mapname, Info_ValueForKey( info, "mapname" ), sizeof( mapname ) );
	}
	if ( mapname[0] ) {
		shot = trap_R_RegisterShaderNoMip( va( "levelshots/%s_wide", mapname ) );
	}
	if ( !shot ) {
		shot = trap_R_RegisterShaderNoMip( "levelshots/bastion_wide" );
	}

	FAB_Fill( left, 0, w, SCREEN_HEIGHT, black, 1 );
	if ( !shot ) {
		return;
	}
	if ( w / SCREEN_HEIGHT > LEVELSHOT_ASPECT ) {
		t1 = ( 1 - LEVELSHOT_ASPECT / ( w / SCREEN_HEIGHT ) ) * 0.5f;
		t2 = 1 - t1;
	} else {
		s1 = ( 1 - ( w / SCREEN_HEIGHT ) / LEVELSHOT_ASPECT ) * 0.5f;
		s2 = 1 - s1;
	}
	FAB_Color( fabWhite, 0.6f );
	FAB_Pic( left, 0, w, SCREEN_HEIGHT, s1, t1, s2, t2, shot );
}
#endif

static void Main_MenuDraw( void ) {
#ifdef STANDALONE
	FabUI_Begin();
	if ( strlen( s_errorMessage.errorMessage ) ) {
		float y;

		FabUI_Dim( 0.55f );
		FAB_Panel( 160, 150, 320, 170, 12, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.97f );
		FAB_Fill( 172, 150, 308, 2, fabRed, 1 );
		FAB_Label( 180, 176, 8, FAB_ALIGN_LEFT, "DISCONNECTED", fabRed, 1 );
		y = FabUI_Wrapped( 180, 202, 280, 11, 16, FAB_ALIGN_LEFT,
			s_errorMessage.errorMessage, fabWhite, 1 );
		FAB_Label( 180, 304, 7, FAB_ALIGN_LEFT, "PRESS ANY KEY TO CONTINUE", fabDim,
			0.6f + 0.4f * sin( uis.realtime / 300.0f ) );
		(void)y;
		return;
	}

	{
		uiClientState_t cstate;
		float fade;

		// until the backdrop map is first up, hold the boot screen
		trap_GetClientState( &cstate );
		if ( Main_BootHeld() ) {
			FAB_LoadingBegin( uis.glconfig.vidWidth, uis.glconfig.vidHeight );
			FAB_DrawBootScreen( 0, "STARTING" );
			uis.bootFadePending = qtrue;
			return;
		}
		if ( cstate.connState != CA_ACTIVE && trap_Cvar_VariableValue( "g_menuBackground" ) ) {
			Main_DrawTransitionBackdrop();
		}
		if ( uis.bootFadePending ) {
			uis.bootFadePending = qfalse;
			uis.bootFadeStart = uis.realtime;
		}

        {
            uiAuthStatus_t account;
            trap_AuthStatus( &account );
            s_main.account.string = account.signedIn ? account.displayName : "ACCOUNT";
            FabUI_SideShade( 420 );
            Menu_Draw( &s_main.menu );
            s_main.account.string = "ACCOUNT";
        }
		// VERSION at the repository root, "-dev" unless built from its release tag
		FAB_Label( FabUI_Right() - 44, SCREEN_HEIGHT - 22, 6.5f, FAB_ALIGN_RIGHT,
			"v" PRODUCT_VERSION, fabDim, 0.6f );

		// coming out of the boot screen everything but the wordmark fades in
		fade = uis.bootFadeStart ? 1.0f - ( uis.realtime - uis.bootFadeStart ) / 700.0f : 0;
		if ( fade > 0 ) {
			FabUI_Dim( fade );
		}
		FAB_LoadingBegin( uis.glconfig.vidWidth, uis.glconfig.vidHeight );
		FAB_DrawMenuWordmark();
	}
	return;
#else
	refdef_t		refdef;
	refEntity_t		ent;
	vec3_t			origin;
	vec3_t			angles;
	float			adjust;
	float			x, y, w, h;
	vec4_t			color = {0.5, 0, 0, 1};

	// setup the refdef

	memset( &refdef, 0, sizeof( refdef ) );

	refdef.rdflags = RDF_NOWORLDMODEL;

	AxisClear( refdef.viewaxis );

	x = 0;
	y = 0;
	w = 640;
	h = 120;
	UI_AdjustFrom640( &x, &y, &w, &h );
	refdef.x = x;
	refdef.y = y;
	refdef.width = w;
	refdef.height = h;

	adjust = 0; // JDC: Kenneth asked me to stop this 1.0 * sin( (float)uis.realtime / 1000 );
	refdef.fov_x = 60 + adjust;
	refdef.fov_y = 19.6875 + adjust;

	refdef.time = uis.realtime;

	origin[0] = 300;
	origin[1] = 0;
	origin[2] = -32;

	trap_R_ClearScene();

	// add the model

	memset( &ent, 0, sizeof(ent) );

	adjust = 5.0 * sin( (float)uis.realtime / 5000 );
	VectorSet( angles, 0, 180 + adjust, 0 );
	AnglesToAxis( angles, ent.axis );
	ent.hModel = s_main.bannerModel;
	VectorCopy( origin, ent.origin );
	VectorCopy( origin, ent.lightingOrigin );
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;
	VectorCopy( ent.origin, ent.oldorigin );

	trap_R_AddRefEntityToScene( &ent );

	trap_R_RenderScene( &refdef );
	
	if (strlen(s_errorMessage.errorMessage))
	{
		UI_DrawProportionalString_AutoWrapped( 320, 192, 600, 20, s_errorMessage.errorMessage, UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, menu_text_color );
	}
	else
	{
		// standard menu drawing
		Menu_Draw( &s_main.menu );		
	}

	if (uis.demoversion) {
		UI_DrawProportionalString( 320, 372, "DEMO      FOR MATURE AUDIENCES      DEMO", UI_CENTER|UI_SMALLFONT, color );
		UI_DrawString( 320, 400, "Quake III Arena(c) 1999-2000, Id Software, Inc.  All Rights Reserved", UI_CENTER|UI_SMALLFONT, color );
	} else {
		UI_DrawString( 320, 450, "Quake III Arena(c) 1999-2000, Id Software, Inc.  All Rights Reserved", UI_CENTER|UI_SMALLFONT, color );
	}
#endif
}

#ifdef STANDALONE
/*
While the backdrop map reloads, the engine is on its connect and loading screens,
which would otherwise cover the menu. They call this so the menu stays up instead.
Nothing is drawn when the menu owns the key catcher: UI_Refresh already draws it.
*/
void UI_DrawMenuTransition( void ) {
	qboolean rebuilt = qfalse;

	if ( Main_BootHeld() ) {
		return;
	}
	// loading the new map restarts the ui, which forgets its menu until the
	// match is up: bring the main menu back now rather than at the end of the load
	if ( !uis.activemenu ) {
		UI_MainMenu();
		rebuilt = qtrue;
	}
	if ( rebuilt || !( trap_Key_GetCatcher() & KEYCATCH_UI ) ) {
		Main_MenuDraw();
	}
}

qboolean UI_MenuBootHeld( void ) {
	return Main_BootHeld();
}

// A small text link rather than a full button, so it sits quietly above the version.
static void Main_DrawLink( void *self ) {
	menutext_s *t = (menutext_s *)self;
	menucommon_s *g = &t->generic;
	qboolean focus = g->parent && g->parent->cursor == g->menuPosition;
	float x = g->right;

	x -= FAB_Label( x, g->top + 4, 7, FAB_ALIGN_RIGHT, t->string,
		focus ? fabCyan : fabWhite, focus ? 1.0f : 0.8f );
	if ( focus ) {
		FAB_Fill( x, g->bottom - 2, g->right - x, 1, fabCyan, 1 );
	}
}

qboolean UI_MenuBackgroundReady( void ) {
	return uis.activemenu == &s_main.menu && !s_errorMessage.errorMessage[0];
}
#endif

#ifdef STANDALONE
static sfxHandle_t Main_MenuKey( int key ) {
	if ( trap_Cvar_VariableValue( "cl_menuLoading" ) ) return 0;
	if ( key == K_ESCAPE || key == K_MOUSE2 ) {
		return menu_buzz_sound;
	}
	return Menu_DefaultKey( &s_main.menu, key );
}
#endif

#ifdef STANDALONE
static int LevelSelect_PopulationIndex( int population ) {
	int i;

	if ( population < levelPopulationValues[0] ) {
		population = 32;
	}
	for ( i = 0; i < ARRAY_LEN( levelPopulationValues ) - 1; i++ ) {
		if ( population <= levelPopulationValues[i] ) {
			return i;
		}
	}
	return ARRAY_LEN( levelPopulationValues ) - 1;
}

static int LevelSelect_PopulationCap( int arena ) {
	return s_levelselect.host ? UI_ArenaMaxPlayers( levelArenaMaps[arena] ) : LEVEL_QUICK_PLAY_MAX_PLAYERS;
}

static int LevelSelect_MaxPopulationIndex( int cap ) {
	int maxIndex;

	for ( maxIndex = ARRAY_LEN( levelPopulationValues ) - 1;
		maxIndex > 0 && levelPopulationValues[maxIndex] > cap; maxIndex-- ) {
	}
	return maxIndex;
}

/*
Quick Play uses one cap for every arena. Hosted matches use the map's cap.
Stepping up past the cap wraps to the smallest size.
*/
static void LevelSelect_ClampPopulation( qboolean stepped ) {
	int maxIndex = LevelSelect_MaxPopulationIndex( LevelSelect_PopulationCap( LevelSelect_Arena() ) );

	if ( s_levelselect.population.curvalue > maxIndex ) {
		s_levelselect.population.curvalue =
			( stepped && s_levelselect.lastPopulation == maxIndex ) ? 0 : maxIndex;
	}
	s_levelselect.lastPopulation = s_levelselect.population.curvalue;
}

static void LevelSelect_UpdateTeamSetting( void ) {
	if ( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_TEAM ||
		s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_KILLSHIFT ||
		s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_BUGHUNT ) {
		s_levelselect.teams.generic.flags &= ~( QMF_GRAYED | QMF_HIDDEN | QMF_INACTIVE );
	} else {
		s_levelselect.teams.generic.flags |= QMF_GRAYED | QMF_HIDDEN | QMF_INACTIVE;
	}
}

static void LevelSelect_SetScoreLimit( int limit ) {
	Com_sprintf( s_levelselect.scoreLimit.field.buffer,
		sizeof( s_levelselect.scoreLimit.field.buffer ), "%d", limit );
	s_levelselect.scoreLimit.field.cursor = strlen( s_levelselect.scoreLimit.field.buffer );
	s_levelselect.scoreLimit.field.scroll = 0;
}

static int LevelSelect_ScoreLimit( void ) {
	const char *text = s_levelselect.scoreLimit.field.buffer;
	qboolean leprechaun = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LEPRECHAUN;
	int limit = leprechaun ? LEPRECHAUN_SCORE_DEFAULT : LEVEL_SCORE_DEFAULT;
	int i;

	// Empty or malformed pasted input returns to the default; numbers stay in range.
	for ( i = 0; text[i]; i++ ) {
		if ( text[i] < '0' || text[i] > '9' ) break;
	}
	if ( i && !text[i] ) {
		limit = atoi( text );
		limit = leprechaun ? BG_LeprechaunScoreLimit( limit ) : (int)Com_Clamp( 1, LEVEL_SCORE_MAX, limit );
	}
	LevelSelect_SetScoreLimit( limit );
	return limit;
}

static void LevelSelect_UpdateTeamRows( void ) {
	qboolean team = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_TEAM;
	qboolean killshift = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_KILLSHIFT;
	qboolean lms = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LMS;
	qboolean infected = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_INFECTED;
	qboolean bugHunt = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_BUGHUNT;

	if ( team ) {
		s_levelselect.teams.generic.flags &= ~QMF_GRAYED;
	} else {
		if ( killshift || bugHunt ) s_levelselect.teams.generic.flags &= ~QMF_GRAYED;
		else s_levelselect.teams.generic.flags |= QMF_GRAYED;
	}
	if ( killshift || lms || infected || bugHunt ) s_levelselect.scoreLimit.generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
	else s_levelselect.scoreLimit.generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
	if ( lms || bugHunt ) s_levelselect.lives.generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
	else s_levelselect.lives.generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
}

/*
Map guide data comes from arenas.txt: "modes" lists the match types a map
can run (missing means all of them), "best" the ones it was built for, and
"blurb" a one-line description.
*/
static qboolean LevelSelect_HasToken( const char *list, const char *token ) {
	int len = strlen( token );

	while ( *list ) {
		while ( *list == ' ' ) {
			list++;
		}
		if ( !Q_stricmpn( list, token, len ) && ( list[len] == ' ' || !list[len] ) ) {
			return qtrue;
		}
		while ( *list && *list != ' ' ) {
			list++;
		}
	}
	return qfalse;
}

static int LevelSelect_Fit( int arena, int gametype ) {
	const char *info = UI_GetArenaInfoByMap( levelArenaMaps[arena] );
	const char *token = levelGametypeTokens[gametype];
	const char *modes;

	if ( !info ) {
		return LEVEL_FIT_PLAYABLE;
	}
	modes = Info_ValueForKey( info, "modes" );
	if ( gametype != LEVEL_GAMETYPE_LEPRECHAUN && modes[0] && !LevelSelect_HasToken( modes, token ) ) {
		return LEVEL_FIT_NONE;
	}
	return LevelSelect_HasToken( Info_ValueForKey( info, "best" ), token ) ?
		LEVEL_FIT_BEST : LEVEL_FIT_PLAYABLE;
}

// arenas index of the selected MAP row choice
static int LevelSelect_Arena( void ) {
	return s_levelselect.mapList[s_levelselect.arena.curvalue];
}

/*
Fill the MAP row with the maps that offer the selected match type, the ones
built for it first, keeping the current map selected when it's still there.
*/
static void LevelSelect_BuildMapList( int keepArena ) {
	int gametype = s_levelselect.gametype.curvalue;
	int fit, i, n = 0;

	for ( fit = LEVEL_FIT_BEST; fit >= LEVEL_FIT_PLAYABLE; fit-- ) {
		for ( i = 0; i < (int)LEVEL_NUM_ARENAS; i++ ) {
			if ( LevelSelect_Fit( i, gametype ) == fit ) {
				s_levelselect.mapList[n] = i;
				s_levelselect.mapListNames[n] = levelArenaNames[i];
				n++;
			}
		}
	}
	if ( !n ) {
		// arenas.txt rules every map out; offer them all rather than nothing
		for ( i = 0; i < (int)LEVEL_NUM_ARENAS; i++ ) {
			s_levelselect.mapList[n] = i;
			s_levelselect.mapListNames[n++] = levelArenaNames[i];
		}
	}
	s_levelselect.mapListNames[n] = NULL;
	s_levelselect.mapListGametype = gametype;
	s_levelselect.arena.itemnames = s_levelselect.mapListNames;
	s_levelselect.arena.numitems = n;
	s_levelselect.arena.curvalue = 0;
	for ( i = 0; i < n; i++ ) {
		if ( s_levelselect.mapList[i] == keepArena ) {
			s_levelselect.arena.curvalue = i;
		}
	}
	s_levelselect.syncedArena = LevelSelect_Arena();
}

// rebuild the MAP choices after a match type change
static void LevelSelect_SyncMapList( void ) {
	int arena = LevelSelect_Arena();

	if ( s_levelselect.mapListGametype == s_levelselect.gametype.curvalue ) {
		// the MAP row moved without a match type change: the player picked it
		if ( arena != s_levelselect.syncedArena ) {
			s_levelselect.chosenArena = s_levelselect.syncedArena = arena;
		}
		return;
	}
	LevelSelect_BuildMapList( s_levelselect.chosenArena );
	if ( LevelSelect_Arena() != arena ) {
		LevelSelect_ClampPopulation( qfalse );
		LevelSelect_ShowArena();
	}
}

// the player picked a map from the MAP row or the list
static void LevelSelect_ChooseArena( void ) {
	s_levelselect.chosenArena = s_levelselect.syncedArena = LevelSelect_Arena();
	LevelSelect_ClampPopulation( qfalse );
	LevelSelect_UpdateTeamRows();
	LevelSelect_ShowArena();
}

static void LevelSelect_Randomize( void ) {
	int modes[LEVEL_NUM_GAMETYPES];
	int mode, arena, numModes = 0;
	int minPopulation = 0, maxPopulation;

	for ( mode = 0; mode < LEVEL_NUM_GAMETYPES; mode++ ) {
		for ( arena = 0; arena < LEVEL_NUM_ARENAS; arena++ ) {
			if ( LevelSelect_Fit( arena, mode ) != LEVEL_FIT_NONE ) {
				modes[numModes++] = mode;
				break;
			}
		}
	}
	if ( numModes ) {
		s_levelselect.gametype.curvalue = modes[rand() % numModes];
	}
	LevelSelect_BuildMapList( -1 );
	s_levelselect.arena.curvalue = rand() % s_levelselect.arena.numitems;
	s_levelselect.listShownPick = -1;
	LevelSelect_UpdateTeamSetting();
	LevelSelect_UpdateTeamRows();

	maxPopulation = LevelSelect_MaxPopulationIndex( LevelSelect_PopulationCap( LevelSelect_Arena() ) );
	if ( !( s_levelselect.teams.generic.flags & QMF_HIDDEN ) ) {
		minPopulation = MIN( maxPopulation, LevelSelect_PopulationIndex( s_levelselect.teams.curvalue + 2 ) );
	}
	s_levelselect.population.curvalue = minPopulation + rand() % ( maxPopulation - minPopulation + 1 );
	if ( !( s_levelselect.scoreLimit.generic.flags & QMF_HIDDEN ) ) {
		qboolean leprechaun = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LEPRECHAUN;
		int minScore = leprechaun ? LEPRECHAUN_SCORE_MIN : LEVEL_RANDOM_SCORE_MIN;
		int maxScore = leprechaun ? LEPRECHAUN_SCORE_MAX : LEVEL_RANDOM_SCORE_MAX;
		LevelSelect_SetScoreLimit( minScore + rand() % ( maxScore - minScore + 1 ) );
	}
	s_levelselect.loadout.curvalue = rand() % s_levelselect.loadout.numitems;
	s_levelselect.ammo.curvalue = rand() % s_levelselect.ammo.numitems;
	Menu_ActivateItem( &s_levelselect.menu, &s_levelselect.loadout.generic );
	LevelSelect_ChooseArena();
}

static float LevelSelect_ListX( void ) {
	float x = FabUI_Right() - 44 - MAPLIST_W;
	float minX = FabMenu_X() + FAB_MENU_W + 28;

	return x > minX ? x : minX;
}

// slots shown at once in the map list; more maps than this scroll
static int LevelSelect_ListVisible( void ) {
	int n = s_levelselect.arena.numitems;
	return n < MAPLIST_VISIBLE ? n : MAPLIST_VISIBLE;
}

static float LevelSelect_ListRowY( int slot ) {
	return MAPLIST_TOP + 10 + slot * MAPLIST_ROW_H;
}

static void LevelSelect_ScrollList( int scroll ) {
	int maxScroll = s_levelselect.arena.numitems - LevelSelect_ListVisible();

	s_levelselect.listScroll = scroll > maxScroll ? maxScroll : scroll < 0 ? 0 : scroll;
}

static qboolean LevelSelect_ListUnderMouse( void ) {
	float x = uis.cursorx - LevelSelect_ListX();
	float y = uis.cursory - MAPLIST_TOP;

	return x >= -14 && x <= MAPLIST_W + 14 && y >= -30 &&
		y < LevelSelect_ListRowY( LevelSelect_ListVisible() ) - MAPLIST_TOP;
}

// map list row under the mouse, or -1
static int LevelSelect_ListHit( void ) {
	float x = uis.cursorx - LevelSelect_ListX();
	float y = uis.cursory - LevelSelect_ListRowY( 0 );

	if ( x < -6 || x > MAPLIST_W + 6 || y < 0 || y >= LevelSelect_ListVisible() * MAPLIST_ROW_H ) {
		return -1;
	}
	return s_levelselect.listScroll + (int)( y / MAPLIST_ROW_H );
}

static void LevelSelect_DrawMapList( void ) {
	float x = LevelSelect_ListX(), y;
	int gametype = s_levelselect.gametype.curvalue;
	int visible = LevelSelect_ListVisible();
	int hover = LevelSelect_ListHit();
	int slot, row, arena, best;
	const char *info, *blurb;

	// a new pick (from the dropdown, keys or a match type change) scrolls into view
	if ( s_levelselect.arena.curvalue != s_levelselect.listShownPick ) {
		s_levelselect.listShownPick = s_levelselect.arena.curvalue;
		if ( s_levelselect.arena.curvalue < s_levelselect.listScroll ) {
			LevelSelect_ScrollList( s_levelselect.arena.curvalue );
		} else if ( s_levelselect.arena.curvalue >= s_levelselect.listScroll + visible ) {
			LevelSelect_ScrollList( s_levelselect.arena.curvalue - visible + 1 );
		}
	}
	LevelSelect_ScrollList( s_levelselect.listScroll );

	y = LevelSelect_ListRowY( visible ) + 70;
	FAB_Panel( x - 14, MAPLIST_TOP - 30, MAPLIST_W + 28, y - MAPLIST_TOP + 30, 10,
		FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.88f );
	FAB_Fill( x - 14, MAPLIST_TOP - 30, MAPLIST_W + 28, 2, fabCyan, 1 );
	FAB_Label( x, MAPLIST_TOP - 12, 8, FAB_ALIGN_LEFT,
		va( "MAPS FOR %s", levelGametypeNames[gametype] ), fabCyan, 1 );
	FAB_Label( x + MAPLIST_W, MAPLIST_TOP - 12, 6.5f, FAB_ALIGN_RIGHT, "PLAYERS", fabDim, 0.8f );

	for ( slot = 0; slot < visible; slot++ ) {
		qboolean selected;

		row = s_levelselect.listScroll + slot;
		selected = row == s_levelselect.arena.curvalue;
		arena = s_levelselect.mapList[row];
		best = LevelSelect_Fit( arena, gametype ) == LEVEL_FIT_BEST;
		y = LevelSelect_ListRowY( slot );
		if ( selected ) {
			FAB_Fill( x - 6, y, MAPLIST_W + 12, MAPLIST_ROW_H - 3, fabCyan, 0.14f );
			FAB_Fill( x - 6, y, 2, MAPLIST_ROW_H - 3, fabCyan, 1 );
		} else if ( row == hover ) {
			FAB_Fill( x - 6, y, MAPLIST_W + 12, MAPLIST_ROW_H - 3, fabCyan, 0.06f );
		}
		FAB_Label( x + 4, y + 13, 8, FAB_ALIGN_LEFT, levelArenaNames[arena],
			selected ? fabWhite : fabDim, 1 );
		FAB_Label( x + 4, y + 23, 6, FAB_ALIGN_LEFT, best ? "BUILT FOR THIS MODE" : "PLAYABLE",
			best ? fabCyan : fabDim, best ? 0.9f : 0.6f );
		FAB_Label( x + MAPLIST_W, y + 13, 7, FAB_ALIGN_RIGHT,
			va( "%d", LevelSelect_PopulationCap( arena ) ), selected ? fabWhite : fabDim, 0.9f );
	}

	if ( s_levelselect.arena.numitems > visible ) {
		float trackY = LevelSelect_ListRowY( 0 ), trackH = visible * MAPLIST_ROW_H - 3;
		float thumbH = trackH * visible / s_levelselect.arena.numitems;
		float thumbY = trackY + ( trackH - thumbH ) * s_levelselect.listScroll /
			( s_levelselect.arena.numitems - visible );

		FAB_Fill( x + MAPLIST_W + 8, trackY, 3, trackH, fabPanelLight, 0.7f );
		FAB_Fill( x + MAPLIST_W + 8, thumbY, 3, thumbH, fabCyan, 0.9f );
	}

	y = LevelSelect_ListRowY( visible ) + 14;
	info = UI_GetArenaInfoByMap( levelArenaMaps[LevelSelect_Arena()] );
	blurb = info ? Info_ValueForKey( info, "blurb" ) : "";
	if ( blurb[0] ) {
		FAB_Wrapped( x, y, MAPLIST_W, 7, 11, FAB_ALIGN_LEFT, blurb, fabWhite, 0.9f );
	}
	FAB_Label( x, LevelSelect_ListRowY( visible ) + 60, 6, FAB_ALIGN_LEFT,
		s_levelselect.arena.numitems > visible ?
			va( "%d MAPS  /  SCROLL FOR MORE, CLICK TO PICK", s_levelselect.arena.numitems ) :
			"CHOOSE A MATCH TYPE, THEN CLICK A MAP", fabDim, 0.7f );
}

// MAP dropdown: player cap, and whether the map was built for the match type
static const char *LevelSelect_MapDetail( menulist_s *l, int item, float *alpha ) {
	int arena = s_levelselect.mapList[item];
	qboolean best = LevelSelect_Fit( arena, s_levelselect.gametype.curvalue ) == LEVEL_FIT_BEST;

	*alpha = best ? 0.95f : 0.6f;
	return va( "%s%d PLAYERS", best ? "BEST FIT  /  " : "", LevelSelect_PopulationCap( arena ) );
}

// MATCH TYPE dropdown: how many maps offer each one
static const char *LevelSelect_ModeDetail( menulist_s *l, int item, float *alpha ) {
	int i, count = 0;

	for ( i = 0; i < (int)LEVEL_NUM_ARENAS; i++ ) {
		if ( LevelSelect_Fit( i, item ) != LEVEL_FIT_NONE ) {
			count++;
		}
	}
	return va( "%d MAP%s", count, count == 1 ? "" : "S" );
}

/*
The backdrop is the selected map's levelshot (the same one the loading screen
uses), crossfaded on change. Loading the map itself would restart the UI VM
and hitch on every pick.
*/
static void LevelSelect_ShowArena( void ) {
	int arena = LevelSelect_Arena();

	if ( !s_levelselect.inMatch ) {
		trap_Cvar_SetValue( "ui_matchArena", arena );
	}
	if ( arena == s_levelselect.shotArena ) {
		return;
	}
	s_levelselect.shotPrevArena = s_levelselect.shotArena;
	s_levelselect.shotArena = arena;
	s_levelselect.shotChangeTime = uis.realtime;
	if ( trap_Cvar_VariableValue( "developer" ) ) {
		Com_Printf( "Level select: showing mode %s, map %s\n",
			levelGametypeNames[s_levelselect.gametype.curvalue], levelArenaMaps[arena] );
	}
}

// Covers the screen with a levelshot, cropping whichever axis overflows.
static qboolean LevelSelect_DrawShot( int arena, float alpha ) {
	float left = FabUI_Left(), w = FabUI_Right() - left;
	float s1 = 0, t1 = 0, s2 = 1, t2 = 1;
	qhandle_t shot;

	if ( arena < 0 || !( shot = s_levelselect.shots[arena] ) ) {
		return qfalse;
	}
	if ( w / SCREEN_HEIGHT > LEVELSHOT_ASPECT ) {
		t1 = ( 1 - LEVELSHOT_ASPECT / ( w / SCREEN_HEIGHT ) ) * 0.5f;
		t2 = 1 - t1;
	} else {
		s1 = ( 1 - ( w / SCREEN_HEIGHT ) / LEVELSHOT_ASPECT ) * 0.5f;
		s2 = 1 - s1;
	}
	FAB_Color( fabWhite, alpha );
	FAB_Pic( left, 0, w, SCREEN_HEIGHT, s1, t1, s2, t2, shot );
	return qtrue;
}

static void LevelSelect_DrawBackdrop( void ) {
	vec4_t black = { 0, 0, 0, 1 };
	float fade = ( uis.realtime - s_levelselect.shotChangeTime ) / (float)LEVELSHOT_FADE_MSEC;

	if ( fade < 1 ) {
		LevelSelect_DrawShot( s_levelselect.shotPrevArena, 1 );
	}
	if ( !LevelSelect_DrawShot( s_levelselect.shotArena, fade > 1 ? 1 : fade ) ) {
		return;	// no levelshot: leave the live backdrop showing
	}
	// keep the map list on the right readable over a bright shot
	FAB_Fade( FabUI_Right() - 360, 0, 360, SCREEN_HEIGHT, qtrue, black, 0.45f );
}

static void LevelSelect_UpdateAmmoSetting( void ) {
	// Arms Race picks the weapon and never runs out of ammo.
	if ( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_ARMSRACE ) {
		s_levelselect.loadout.generic.flags |= QMF_GRAYED;
		s_levelselect.ammo.generic.flags |= QMF_GRAYED;
		return;
	}
	s_levelselect.loadout.generic.flags &= ~QMF_GRAYED;
	if ( s_levelselect.loadout.curvalue == LEVEL_LOADOUT_ODIN_SNEEZE ) {
		s_levelselect.ammo.curvalue = 0;
		s_levelselect.ammo.generic.flags |= QMF_GRAYED;
	} else {
		s_levelselect.ammo.generic.flags &= ~QMF_GRAYED;
	}
}

static void LevelSelect_Event( void *ptr, int event ) {
	int population, scoreLimit;
	qboolean bots, leprechaun = s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LEPRECHAUN;

	if ( event == QM_LOSTFOCUS && ((menucommon_s *)ptr)->id == ID_LEVEL_SCORELIMIT ) {
		LevelSelect_ScoreLimit();
	}
	if ( event != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_LEVEL_ARENA:
		LevelSelect_ChooseArena();
		break;

	case ID_LEVEL_POPULATION:
		LevelSelect_ClampPopulation( qtrue );
		LevelSelect_UpdateTeamRows();
		break;

	case ID_LEVEL_GAMETYPE:
		if ( leprechaun ) LevelSelect_ScoreLimit();
		LevelSelect_SyncMapList();
		LevelSelect_UpdateTeamSetting();
	case ID_LEVEL_TEAMS:
		LevelSelect_UpdateTeamRows();
		break;

	case ID_LEVEL_LOADOUT:
		LevelSelect_UpdateAmmoSetting();
		break;

	case ID_LEVEL_RANDOMIZE:
		LevelSelect_Randomize();
		break;

	case ID_LEVEL_START:
		if ( s_levelselect.online && !UI_RequireOnlineLogin() ) return;
		LevelSelect_ClampPopulation( qfalse );
		population = levelPopulationValues[s_levelselect.population.curvalue];
		bots = !s_levelselect.host || s_levelselect.bots.curvalue;
		trap_Cvar_SetValue( "sv_lanHost", s_levelselect.host );
		trap_Cvar_SetValue( "sv_onlineHost", s_levelselect.online );
		trap_Cvar_Set( "ui_lanClient", "0" );
		if ( s_levelselect.host ) {
			char name[MAX_NAME_LENGTH];

			trap_Cvar_VariableStringBuffer( "name", name, sizeof( name ) );
			Q_CleanStr( name );
			trap_Cvar_Set( "sv_hostname", name[0] ? va( "%s's Match", name ) : "FRAGNAROK Match" );
			trap_Cvar_SetValue( "ui_lanBots", bots );
		}
		trap_Cvar_SetValue( "g_gametype",
			( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_TEAM ||
			  s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_KILLSHIFT ||
			  s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_INFECTED ||
			  s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_BUGHUNT ) ? GT_TEAM : GT_FFA );
		trap_Cvar_SetValue( "g_leprechaun", leprechaun );
		trap_Cvar_SetValue( "g_killshift", s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_KILLSHIFT );
		trap_Cvar_SetValue( "g_lastManStanding", s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LMS );
		trap_Cvar_SetValue( "g_infected", s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_INFECTED );
		trap_Cvar_SetValue( "g_bugHunt", s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_BUGHUNT );
		trap_Cvar_SetValue( "g_armsRace", s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_ARMSRACE );
		if ( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LMS ) {
			trap_Cvar_SetValue( "g_lmsLives", s_levelselect.lives.curvalue + 1 );
		}
		if ( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_BUGHUNT ) {
			trap_Cvar_SetValue( "g_bugLives", s_levelselect.lives.curvalue + 1 );
		}
		if ( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_KILLSHIFT ||
			s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_BUGHUNT ) {
			trap_Cvar_SetValue( "g_teamCount", s_levelselect.teams.curvalue + 2 );
		}
		if ( s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_TEAM ) {
			trap_Cvar_SetValue( "g_teamCount", s_levelselect.teams.curvalue + 2 );
			trap_Cvar_SetValue( "ui_tdmTeams", s_levelselect.teams.curvalue + 2 );
		}
		scoreLimit = LevelSelect_ScoreLimit();
		trap_Cvar_SetValue( "ui_matchScoreLimit", scoreLimit );
		trap_Cvar_SetValue( "fraglimit",
			leprechaun || s_levelselect.gametype.curvalue <= LEVEL_GAMETYPE_TEAM ||
			s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_ARMSRACE ? scoreLimit : 0 );
		trap_Cvar_SetValue( "g_targetPopulation", bots ? population : 0 );
		trap_Cvar_SetValue( "g_activeBotLimit", bots ? population : 0 );
		trap_Cvar_SetValue( "g_companionOnly", !bots );
		trap_Cvar_SetValue( "g_davidGoliath", s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_DG );
		trap_Cvar_Set( "g_spawnWeapons", levelLoadoutWeapons[s_levelselect.loadout.curvalue] );
		trap_Cvar_SetValue( "g_playerClasses", FAB_PLAYER_CLASSES && s_levelselect.loadout.curvalue == 0 );
		trap_Cvar_SetValue( "g_infiniteAmmo", s_levelselect.ammo.curvalue );
		trap_Cvar_SetValue( "ui_matchLoadout", s_levelselect.loadout.curvalue + LEVEL_LOADOUT_SAVED_OFFSET );
		trap_Cvar_SetValue( "ui_matchAmmo", s_levelselect.ammo.curvalue );
		trap_Cvar_SetValue( "ui_matchArena", LevelSelect_Arena() );
		// remembered separately because leaving to the main menu resets the
		// game's mode cvars for the background match
		trap_Cvar_SetValue( "ui_matchMode", s_levelselect.gametype.curvalue + 1 );
		UI_ForceMenuOff();
		trap_Cvar_Set( "g_menuBackground", "0" );
		if ( s_levelselect.host ) {
			// Latch capacity through the console; forcing it would invalidate the running VM.
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "%sset sv_maxclients %d\nfreshmap %s\n",
				s_levelselect.inMatch ? "" : "killserver\n", population, levelArenaMaps[LevelSelect_Arena()] ) );
		} else {
			// A small hosted match can leave too few slots for the local population.
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "set sv_maxclients %d\nfreshmap %s quickplay\n",
				MAX( population, (int)trap_Cvar_VariableValue( "sv_maxclients" ) ),
				levelArenaMaps[LevelSelect_Arena()] ) );
		}
		break;

	case ID_LEVEL_BACK:
		UI_PopMenu();
		break;
	}
}

static const char *levelGametypeBlurbs[] = {
	"FREE FOR ALL WITH YOUR AEGIS COMPANION AND BOTS",
	"EVERY FRAG MAKES YOU BIGGER, TOUGHER AND SLOWER",
	"TWO TO FOUR TEAMS RACE TO THE SCORE LIMIT",
	"KILLS CONVERT ENEMIES. CAPTURE EVERYONE TO WIN",
	"LIMITED LIVES. LAST ONE STANDING WINS",
	"ONE CARRIER. EVERY DEATH JOINS THE HORDE",
	"SQUADS VS SPIDER SWARMS AND EACH OTHER",
	"KILL THE LEPRECHAUN TO TAKE OVER. SURVIVE TO SCORE.",
	"ONE SHARED WEAPON THAT CHANGES EVERY 30-60 SECONDS"
};
// one blurb per match type
typedef char levelGametypeBlurbsCheck[ARRAY_LEN( levelGametypeBlurbs ) == LEVEL_NUM_GAMETYPES ? 1 : -1];

static void LevelSelect_Draw( void ) {
	const char *subtitle;

	// The selected value is authoritative. This also catches keyboard or mouse
	// changes if a widget path fails to deliver its activation callback.
	LevelSelect_SyncMapList();
	if ( LevelSelect_Arena() != s_levelselect.shotArena ) {
		LevelSelect_ShowArena();
	}
	LevelSelect_UpdateTeamSetting();
	LevelSelect_UpdateTeamRows();
	FabUI_Begin();
	LevelSelect_DrawBackdrop();
	FabUI_SideShade( 460 );
	if ( s_levelselect.inMatch ) {
		subtitle = s_levelselect.currentMatch;
	} else if ( s_levelselect.host ) {
		subtitle = s_levelselect.online ? "PUBLIC MATCH" : "LAN MATCH";
	} else {
		subtitle = levelGametypeBlurbs[s_levelselect.gametype.curvalue];
	}
	FabUI_Header( FabMenu_X(), 120, s_levelselect.inMatch ? "CHANGE MATCH" :
		s_levelselect.online ? "HOST ONLINE MATCH" : s_levelselect.host ? "HOST LAN MATCH" : "PLAY",
		"SELECT MATCH", subtitle );
	Menu_Draw( &s_levelselect.menu );
	if ( !( s_levelselect.scoreLimit.generic.flags & QMF_HIDDEN ) ) {
		FAB_Label( s_levelselect.scoreLimit.generic.right - 12,
			s_levelselect.scoreLimit.generic.top + 17, 7, FAB_ALIGN_RIGHT,
			s_levelselect.gametype.curvalue == LEVEL_GAMETYPE_LEPRECHAUN ?
				"POINTS (15-75)" : "FRAGS (1-1024)", fabDim, 0.9f );
	}
	LevelSelect_DrawMapList();
}

static sfxHandle_t LevelSelect_Key( int key ) {
	int row;
	menucommon_s *item = Menu_ItemAtCursor( &s_levelselect.menu );

	if ( item == &s_levelselect.scoreLimit.generic && ( key & K_CHAR_FLAG ) ) {
		int ch = key & ~K_CHAR_FLAG;
		if ( ch >= 32 && ( ch < '0' || ch > '9' ) ) return menu_buzz_sound;
	}

	LevelSelect_SyncMapList();
	row = LevelSelect_ListHit();

	if ( ( key == K_MWHEELUP || key == K_MWHEELDOWN ) && LevelSelect_ListUnderMouse() ) {
		LevelSelect_ScrollList( s_levelselect.listScroll + ( key == K_MWHEELUP ? -1 : 1 ) );
		if ( trap_Cvar_VariableValue( "developer" ) ) {
			Com_Printf( "Level select: map list shows %d-%d of %d\n", s_levelselect.listScroll + 1,
				s_levelselect.listScroll + LevelSelect_ListVisible(), s_levelselect.arena.numitems );
		}
		return menu_null_sound;
	}

	if ( key == K_MOUSE1 && row >= 0 ) {
		if ( row != s_levelselect.arena.curvalue ) {
			s_levelselect.arena.curvalue = row;
			LevelSelect_ChooseArena();
		}
		return menu_move_sound;
	}
	return FabUI_Key( &s_levelselect.menu, key );
}

// "NOW PLAYING  RIFT FOUNDRY / DEATHMATCH" for the change-match subtitle
static void LevelSelect_DescribeCurrentMatch( void ) {
	char info[MAX_INFO_STRING];
	char title[64];

	trap_GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	FAB_MapTitle( Info_ValueForKey( info, "mapname" ), title, sizeof( title ) );
	Com_sprintf( s_levelselect.currentMatch, sizeof( s_levelselect.currentMatch ),
		"NOW PLAYING  %s / %s", title,
		FAB_ModeName( atoi( Info_ValueForKey( info, "g_gametype" ) ), info ) );
}

/*
Every arenas.txt entry is a Play screen map, in file order, unless it sets
play "0" (Test Chamber opens from Settings instead). ui_matchArena indexes
this list, so new maps go at the end of the file to keep earlier picks.
*/
static void LevelSelect_LoadArenas( void ) {
	const char *info;
	int i, count = UI_GetNumArenas();

	levelNumArenas = 0;
	for ( i = 0; i < count && levelNumArenas < LEVEL_MAX_ARENAS; i++ ) {
		info = UI_GetArenaInfoByNumber( i );
		if ( !info || !Info_ValueForKey( info, "map" )[0] ) {
			continue;
		}
		if ( !Q_stricmp( Info_ValueForKey( info, "play" ), "0" ) ) {
			continue;
		}
		Q_strncpyz( levelArenaMapBuf[levelNumArenas], Info_ValueForKey( info, "map" ),
			sizeof( levelArenaMapBuf[0] ) );
		Q_strncpyz( levelArenaNameBuf[levelNumArenas], Info_ValueForKey( info, "longname" ),
			sizeof( levelArenaNameBuf[0] ) );
		if ( !levelArenaNameBuf[levelNumArenas][0] ) {
			Q_strncpyz( levelArenaNameBuf[levelNumArenas], levelArenaMapBuf[levelNumArenas],
				sizeof( levelArenaNameBuf[0] ) );
		}
		Q_strupr( levelArenaNameBuf[levelNumArenas] );
		levelArenaMaps[levelNumArenas] = levelArenaMapBuf[levelNumArenas];
		levelArenaNames[levelNumArenas] = levelArenaNameBuf[levelNumArenas];
		levelNumArenas++;
	}
	if ( !levelNumArenas ) {
		// no usable arenas.txt: keep the screen working on the stock map
		Q_strncpyz( levelArenaMapBuf[0], "rift_foundry", sizeof( levelArenaMapBuf[0] ) );
		Q_strncpyz( levelArenaNameBuf[0], "RIFT FOUNDRY", sizeof( levelArenaNameBuf[0] ) );
		levelArenaMaps[0] = levelArenaMapBuf[0];
		levelArenaNames[0] = levelArenaNameBuf[0];
		levelNumArenas = 1;
	}
	levelArenaNames[levelNumArenas] = NULL;
	if ( i < count ) {
		Com_Printf( S_COLOR_YELLOW "Level select: only the first %d maps fit the Play screen\n",
			LEVEL_MAX_ARENAS );
	}
	if ( trap_Cvar_VariableValue( "developer" ) ) {
		Com_Printf( "Level select: %d maps from arenas.txt\n", levelNumArenas );
	}
}

static void UI_LevelSelectMenuOpen( qboolean host, qboolean inMatch ) {
	float x = FabMenu_X(), y = 176;
	const float rowH = 26, gap = 4;
	const int loadoutCount = ARRAY_LEN( levelLoadoutWeapons ) - ( host ? 1 : 0 );
	int arena, mode, population, i;
	char mapname[MAX_QPATH];

	memset( &s_levelselect, 0, sizeof( s_levelselect ) );
	s_levelselect.host = host;
	s_levelselect.online = inMatch && trap_Cvar_VariableValue( "sv_onlineHost" ) != 0.0f;
	s_levelselect.inMatch = inMatch;
	s_levelselect.shotArena = s_levelselect.shotPrevArena = -1;
	s_levelselect.menu.draw = LevelSelect_Draw;
	s_levelselect.menu.key = LevelSelect_Key;
	s_levelselect.menu.wrapAround = qtrue;
	s_levelselect.menu.showlogo = qfalse;
	LevelSelect_LoadArenas();
	// load every levelshot up front so cycling maps never hitches
	for ( i = 0; i < (int)LEVEL_NUM_ARENAS; i++ ) {
		s_levelselect.shots[i] = trap_R_RegisterShaderNoMip(
			va( "levelshots/%s_wide", levelArenaMaps[i] ) );
	}

	arena = (int)trap_Cvar_VariableValue( "ui_matchArena" );
	if ( inMatch ) {
		LevelSelect_DescribeCurrentMatch();
		trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
		for ( mode = 0; mode < (int)LEVEL_NUM_ARENAS; mode++ ) {
			if ( !Q_stricmp( mapname, levelArenaMaps[mode] ) ) {
				arena = mode;
			}
		}
	}
	if ( arena < 0 || arena >= LEVEL_NUM_ARENAS ) {
		arena = 0;
	}

	// In a match the running mode is the truth; otherwise the last pick,
	// since the main menu resets the game's mode cvars.
	mode = (int)trap_Cvar_VariableValue( "ui_matchMode" ) - 1;
	if ( inMatch || mode < 0 || mode >= LEVEL_NUM_GAMETYPES ) {
		if ( (int)trap_Cvar_VariableValue( "g_gametype" ) == GT_TEAM ) {
			if ( trap_Cvar_VariableValue( "g_infected" ) ) mode = LEVEL_GAMETYPE_INFECTED;
			else if ( trap_Cvar_VariableValue( "g_bugHunt" ) ) mode = LEVEL_GAMETYPE_BUGHUNT;
			else mode = trap_Cvar_VariableValue( "g_killshift" ) ?
				LEVEL_GAMETYPE_KILLSHIFT : LEVEL_GAMETYPE_TEAM;
		} else if ( trap_Cvar_VariableValue( "g_leprechaun" ) ) {
			mode = LEVEL_GAMETYPE_LEPRECHAUN;
		} else if ( trap_Cvar_VariableValue( "g_lastManStanding" ) ) {
			mode = LEVEL_GAMETYPE_LMS;
		} else if ( trap_Cvar_VariableValue( "g_armsRace" ) ) {
			mode = LEVEL_GAMETYPE_ARMSRACE;
		} else {
			mode = trap_Cvar_VariableValue( "g_davidGoliath" ) ? LEVEL_GAMETYPE_DG : 0;
		}
	}
	s_levelselect.gametype.curvalue = mode;

	population = (int)trap_Cvar_VariableValue( "g_targetPopulation" );
	if ( host && population <= 0 ) {
		population = (int)trap_Cvar_VariableValue( "sv_maxclients" );
	}
	s_levelselect.population.curvalue = LevelSelect_PopulationIndex( population );
	s_levelselect.bots.curvalue = trap_Cvar_VariableValue( "ui_lanBots" ) != 0.0f;
	s_levelselect.teams.curvalue = (int)Com_Clamp( 2, 4,
		trap_Cvar_VariableValue( "g_teamCount" ) ) - 2;
	LevelSelect_SetScoreLimit( (int)Com_Clamp( 1, LEVEL_SCORE_MAX,
		trap_Cvar_VariableValue( inMatch && trap_Cvar_VariableValue( "fraglimit" ) > 0 ?
			"fraglimit" : "ui_matchScoreLimit" ) ) );
	if ( mode == LEVEL_GAMETYPE_LEPRECHAUN ) LevelSelect_ScoreLimit();
	s_levelselect.chosenArena = arena;
	// The chamber's practice loadout must not replace the saved match choices.
	// Other matches use their running modifiers; the main menu uses the last pick.
	if ( inMatch && Q_stricmp( mapname, "test_chamber" ) ) {
		char weapons[64];

		trap_Cvar_VariableStringBuffer( "g_spawnWeapons", weapons, sizeof( weapons ) );
		for ( i = LEVEL_LOADOUT_SHARED; i < loadoutCount; i++ ) {
			if ( !Q_stricmp( weapons, levelLoadoutWeapons[i] ) ) {
				s_levelselect.loadout.curvalue = i;
			}
		}
#if FAB_PLAYER_CLASSES
		if ( trap_Cvar_VariableValue( "g_playerClasses" ) != 0 ) s_levelselect.loadout.curvalue = 0;
#endif
		s_levelselect.ammo.curvalue = trap_Cvar_VariableValue( "g_infiniteAmmo" ) != 0.0f;
	} else {
		// a saved CLASS LOADOUTS pick falls back to PISTOL + RIFLE
		s_levelselect.loadout.curvalue = (int)Com_Clamp( 0, ARRAY_LEN( levelLoadoutWeapons ) - 1,
			trap_Cvar_VariableValue( "ui_matchLoadout" ) - LEVEL_LOADOUT_SAVED_OFFSET );
		if ( s_levelselect.loadout.curvalue >= loadoutCount ) s_levelselect.loadout.curvalue = 0;
		s_levelselect.ammo.curvalue = trap_Cvar_VariableValue( "ui_matchAmmo" ) != 0.0f;
	}

	// Match type first, then a map that offers it.
	FabUI_InitDropdown( &s_levelselect.gametype, x, y, FAB_MENU_W, rowH, "MATCH TYPE",
		ID_LEVEL_GAMETYPE, LevelSelect_Event, levelGametypeNames );
	FabUI_SetDropdownDetail( &s_levelselect.gametype, LevelSelect_ModeDetail );
	y += rowH + gap;
	FabUI_InitDropdown( &s_levelselect.arena, x, y, FAB_MENU_W, rowH, "MAP",
		ID_LEVEL_ARENA, LevelSelect_Event, levelArenaNames );
	FabUI_SetDropdownDetail( &s_levelselect.arena, LevelSelect_MapDetail );
	LevelSelect_BuildMapList( arena );
	LevelSelect_ClampPopulation( qfalse );
	if ( trap_Cvar_VariableValue( "developer" ) ) {
		Com_Printf( "Level select: mode %s, map %s\n", levelGametypeNames[mode],
			levelArenaMaps[LevelSelect_Arena()] );
	}
	y += rowH + gap;
	FabUI_InitSetting( &s_levelselect.population, x, y, FAB_MENU_W, rowH,
		host ? "MAX PLAYERS" : "TOTAL PLAYERS", ID_LEVEL_POPULATION, LevelSelect_Event,
		levelPopulationNames );
	y += rowH + gap;
	FabUI_InitSetting( &s_levelselect.teams, x, y, FAB_MENU_W, rowH, "TEAMS",
		ID_LEVEL_TEAMS, LevelSelect_Event, levelTeamCountNames );
	y += rowH + gap;
	FabUI_InitField( &s_levelselect.scoreLimit, x, y, FAB_MENU_W, rowH, "SCORE TO WIN",
		ID_LEVEL_SCORELIMIT, 4 );
	s_levelselect.scoreLimit.generic.flags |= QMF_NUMBERSONLY;
	s_levelselect.scoreLimit.generic.callback = LevelSelect_Event;
	s_levelselect.lives.curvalue = (int)Com_Clamp( 1, ARRAY_LEN( levelLivesNames ) - 1,
		trap_Cvar_VariableValue( mode == LEVEL_GAMETYPE_BUGHUNT ? "g_bugLives" : "g_lmsLives" ) ) - 1;
	FabUI_InitSetting( &s_levelselect.lives, x, y, FAB_MENU_W, rowH, "LIVES",
		ID_LEVEL_LIVES, LevelSelect_Event, levelLivesNames );
	y += rowH + gap;
	LevelSelect_UpdateTeamRows();
	if ( host ) {
		FabUI_InitSetting( &s_levelselect.bots, x, y, FAB_MENU_W, rowH, "BOTS",
			ID_LEVEL_BOTS, LevelSelect_Event, levelBotsNames );
		y += rowH + gap;
	}
	FabUI_InitSetting( &s_levelselect.loadout, x, y, FAB_MENU_W, rowH, "LOADOUT",
		ID_LEVEL_LOADOUT, LevelSelect_Event, levelLoadoutNames );
	s_levelselect.loadout.numitems = loadoutCount;
	y += rowH + gap;
	FabUI_InitSetting( &s_levelselect.ammo, x, y, FAB_MENU_W, rowH, "AMMO",
		ID_LEVEL_AMMO, LevelSelect_Event, levelAmmoNames );
	LevelSelect_UpdateAmmoSetting();
	y += rowH + gap;
	y += 14;
	FabUI_InitButton( &s_levelselect.start, x, y, 186, 30,
		inMatch ? "LOAD MATCH" : host ? "HOST MATCH" : "START",
		ID_LEVEL_START, LevelSelect_Event, FAB_BUTTON_PRIMARY );
	FabUI_InitButton( &s_levelselect.back, x + 194, y, FAB_MENU_W - 194, 30, "BACK",
		ID_LEVEL_BACK, LevelSelect_Event, 0 );

	Menu_AddItem( &s_levelselect.menu, &s_levelselect.gametype );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.arena );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.population );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.teams );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.scoreLimit );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.lives );
	if ( host ) {
		Menu_AddItem( &s_levelselect.menu, &s_levelselect.bots );
	}
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.loadout );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.ammo );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.start );
	Menu_AddItem( &s_levelselect.menu, &s_levelselect.back );
	if ( !host && !inMatch ) {
		FabUI_InitButton( &s_levelselect.randomize, x + 202, 112, 106, 22, "RANDOMIZE",
			ID_LEVEL_RANDOMIZE, LevelSelect_Event, 0 );
		Menu_AddItem( &s_levelselect.menu, &s_levelselect.randomize );
	}
	UI_PushMenu( &s_levelselect.menu );
	// pick the match type first, then cycle the maps that offer it
	Menu_SetCursorToItem( &s_levelselect.menu, &s_levelselect.gametype );
	LevelSelect_ShowArena();
}

static void UI_LevelSelectMenuFor( qboolean host ) {
	UI_LevelSelectMenuOpen( host, qfalse );
}

void UI_LevelSelectMenu( void ) {
	UI_LevelSelectMenuFor( qfalse );
}

/*
Pause menu CHANGE MATCH: the same screen, filled in from the running match.
Browsing doesn't touch the match; LOAD MATCH replaces it.
*/
void UI_ChangeMatchMenu( void ) {
	UI_LevelSelectMenuOpen( trap_Cvar_VariableValue( "sv_lanHost" ) != 0.0f, qtrue );
}

/*
=======================================================================

SINGLE PLAYER

=======================================================================
*/

typedef struct {
	menuframework_s menu;
	menutext_s first, second, back;
	const char *eyebrow;
	const char *title;
	const char *subtitle;
} singleplayermenu_t;

static singleplayermenu_t s_singleplayer;
static singleplayermenu_t s_fragsToRiches;
static int campaignLaunchNumber;
static char campaignEyebrow[64], campaignTitle[64], campaignSubtitle[96], campaignDetail[96];

#define CAMPAIGN_ROWS 8

static struct {
	menuframework_s menu;
	menutext_s rows[CAMPAIGN_ROWS], previous, next, back;
	char labels[CAMPAIGN_ROWS][80];
	int first;
	int cleared;
	qboolean fromVictory;
} s_campaignList;

static struct {
	menuframework_s menu;
	menutext_s proceed;
	int number;
} s_campaignVictory;

static void SinglePlayer_Event( void *ptr, int event );

static void SinglePlayer_Draw( void ) {
	singleplayermenu_t *screen = (singleplayermenu_t *)uis.activemenu;

	FabUI_Begin();
	FabUI_SideShade( 420 );
	FabUI_Header( FabMenu_X(), 120, screen->eyebrow, screen->title, screen->subtitle );
	if ( screen == &s_fragsToRiches && campaignDetail[0] ) {
		FAB_Label( FabMenu_X(), 166, 7, FAB_ALIGN_LEFT, campaignDetail, fabDim, 1 );
	}
	Menu_Draw( &screen->menu );
}

static void SinglePlayer_Open( singleplayermenu_t *screen, const char *eyebrow,
	const char *title, const char *subtitle, const char *first, int firstId,
	const char *second, int secondId ) {
	float x = FabMenu_X();

	memset( screen, 0, sizeof( *screen ) );
	screen->menu.draw = SinglePlayer_Draw;
	screen->menu.wrapAround = qtrue;
	screen->eyebrow = eyebrow;
	screen->title = title;
	screen->subtitle = subtitle;

	if ( first ) {
		FabUI_InitButton( &screen->first, x, 180, 240, 30, first,
			firstId, SinglePlayer_Event, FAB_BUTTON_LEFT );
		Menu_AddItem( &screen->menu, &screen->first );
	}
	if ( second ) {
		FabUI_InitButton( &screen->second, x, 216, 240, 30, second,
			secondId, SinglePlayer_Event, FAB_BUTTON_LEFT );
		Menu_AddItem( &screen->menu, &screen->second );
	}
	FabUI_InitButton( &screen->back, x, 266, 240, 30, "BACK",
		ID_SP_BACK, SinglePlayer_Event, FAB_BUTTON_LEFT );
	Menu_AddItem( &screen->menu, &screen->back );
	UI_PushMenu( &screen->menu );
}

int UI_ActiveCampaignMission( void ) {
	if ( !trap_Cvar_VariableValue( "sv_running" ) || trap_Cvar_VariableValue( "g_menuBackground" ) ) return 0;
	return (int)trap_Cvar_VariableValue( "g_campaignMission" );
}

static int CampaignCleared( void ) {
	return (int)Com_Clamp( 0, CAMPAIGN_MISSION_COUNT, trap_Cvar_VariableValue( "ui_campaignCleared" ) );
}

void UI_StartCampaignMission( int number ) {
	if ( !BG_CampaignMission( number ) || number > CampaignCleared() + 1 ) return;
	UI_ForceMenuOff();
	trap_Cmd_ExecuteText( EXEC_INSERT, va( "campaignmap %d\n", number ) );
}

static void CampaignObjective( const campaignMission_t *mission ) {
	const char *objective;

	switch ( mission->mode ) {
	case CAMPAIGN_KILLSHIFT:
		objective = "CONVERT EVERYONE TO YOUR STARTING TEAM";
		break;
	case CAMPAIGN_LMS:
		objective = va( "BE THE LAST STANDING  /  %d %s", mission->lives, mission->lives == 1 ? "LIFE" : "LIVES" );
		break;
	case CAMPAIGN_INFECTED:
		objective = va( "STAY UNINFECTED FOR %d MINUTES", mission->minutes );
		break;
	case CAMPAIGN_BUGHUNT:
		objective = va( "%s  /  %d LIVES  /  %d MINUTES",
			mission->mustSurvive ? "SURVIVE AND WIN" : "WIN WITH YOUR SQUAD", mission->lives, mission->minutes );
		break;
	case CAMPAIGN_TDM:
		objective = va( "FIRST TEAM TO %d FRAGS", mission->frags );
		break;
	default:
		objective = mission->rival && mission->players == 2 ?
			va( "BEAT %s TO %d FRAGS", mission->rival, mission->frags ) : va( "FIRST TO %d FRAGS", mission->frags );
		break;
	}
	Q_strncpyz( campaignSubtitle, objective, sizeof( campaignSubtitle ) );
	Q_strupr( campaignSubtitle );
}

static sfxHandle_t CampaignResult_Key( int key ) {
	if ( key == K_ESCAPE || key == K_MOUSE2 ) {
		UI_ReturnToMainMenu();
		return menu_out_sound;
	}
	return FabUI_Key( &s_fragsToRiches.menu, key );
}

static void Campaign_Open( int number, campaignResult_t result ) {
	const campaignMission_t *mission = BG_CampaignMission( number );
	const char *button;
	char mapTitle[48];

	if ( !mission ) return;
	campaignDetail[0] = '\0';
	campaignLaunchNumber = number;
	Com_sprintf( campaignEyebrow, sizeof( campaignEyebrow ), "FRAGS TO RICHES  /  MATCH %02d OF %d",
		number, CAMPAIGN_MISSION_COUNT );
	Q_strncpyz( campaignTitle, mission->title, sizeof( campaignTitle ) );
	Q_strupr( campaignTitle );
	if ( result == CAMPAIGN_RUNNING ) {
		CampaignObjective( mission );
		FAB_MapTitle( mission->map, mapTitle, sizeof( mapTitle ) );
		Com_sprintf( campaignDetail, sizeof( campaignDetail ), "%s  /  %s  /  %d PLAYERS",
			mapTitle, levelGametypeNames[mission->mode], mission->players );
		button = number <= CampaignCleared() ? "REPLAY MATCH" : "START MATCH";
	} else {
		Q_strncpyz( campaignSubtitle, result == CAMPAIGN_DRAW ? "MATCH DRAWN" : "MATCH LOST", sizeof( campaignSubtitle ) );
		Q_strncpyz( campaignDetail, "WIN THE MATCH TO POST A SCORE", sizeof( campaignDetail ) );
		button = "RETRY MATCH";
	}
	SinglePlayer_Open( &s_fragsToRiches, campaignEyebrow, campaignTitle, campaignSubtitle,
		button, ID_SP_MISSION_START, NULL, 0 );
	if ( result != CAMPAIGN_RUNNING ) {
		s_fragsToRiches.menu.key = CampaignResult_Key;
		s_fragsToRiches.back.generic.id = ID_SP_MISSION_LEAVE;
		s_fragsToRiches.back.string = "MAIN MENU";
	}
}

static void CampaignList_SetPage( int first ) {
	int i;
	int lastPage = ( CAMPAIGN_MISSION_COUNT - 1 ) / CAMPAIGN_ROWS * CAMPAIGN_ROWS;

	s_campaignList.first = (int)Com_Clamp( 0, lastPage, first );
	for ( i = 0; i < CAMPAIGN_ROWS; i++ ) {
		int number = s_campaignList.first + i + 1;
		const campaignMission_t *mission = BG_CampaignMission( number );
		menutext_s *row = &s_campaignList.rows[i];

		row->generic.flags &= ~( QMF_GRAYED | QMF_HIDDEN | QMF_INACTIVE | QMF_HASMOUSEFOCUS );
		if ( !mission ) {
			row->generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
			continue;
		}
		if ( number > s_campaignList.cleared + 1 ) row->generic.flags |= QMF_GRAYED;
		Com_sprintf( s_campaignList.labels[i], sizeof( s_campaignList.labels[i] ),
			"%02d  %s", number, mission->title );
		Q_strupr( s_campaignList.labels[i] );
	}
	s_campaignList.previous.generic.flags &= ~QMF_GRAYED;
	s_campaignList.next.generic.flags &= ~QMF_GRAYED;
	if ( !s_campaignList.first ) s_campaignList.previous.generic.flags |= QMF_GRAYED;
	if ( s_campaignList.first == lastPage ) s_campaignList.next.generic.flags |= QMF_GRAYED;
}

static sfxHandle_t CampaignList_Step( int pages ) {
	menuframework_s *menu = &s_campaignList.menu;
	menucommon_s *item = Menu_ItemAtCursor( menu );
	int first = s_campaignList.first;

	CampaignList_SetPage( first + pages * CAMPAIGN_ROWS );
	if ( s_campaignList.first == first ) return menu_null_sound;
	if ( item && item->flags & ( QMF_GRAYED | QMF_HIDDEN ) ) {
		Menu_SetCursorToItem( menu, pages > 0 ? &s_campaignList.previous : &s_campaignList.next );
	}
	return menu_move_sound;
}

static void CampaignList_Back( void ) {
	if ( s_campaignList.fromVictory ) UI_ReturnToMainMenu();
	else UI_PopMenu();
}

static sfxHandle_t CampaignList_Key( int key ) {
	switch ( key ) {
	case K_ESCAPE:
	case K_MOUSE2:
		CampaignList_Back();
		return menu_out_sound;
	case K_MWHEELUP:
	case K_LEFTARROW:
	case K_PGUP:
		return CampaignList_Step( -1 );
	case K_MWHEELDOWN:
	case K_RIGHTARROW:
	case K_PGDN:
		return CampaignList_Step( 1 );
	default:
		return FabUI_Key( &s_campaignList.menu, key );
	}
}

static void CampaignList_Event( void *ptr, int event ) {
	int item = ((menucommon_s *)ptr)->id;

	if ( event != QM_ACTIVATED ) return;
	if ( item < CAMPAIGN_ROWS ) {
		int number = s_campaignList.first + item + 1;
		if ( BG_CampaignMission( number ) && number <= CampaignCleared() + 1 ) {
			Campaign_Open( number, CAMPAIGN_RUNNING );
		}
	} else if ( ptr == &s_campaignList.back ) {
		CampaignList_Back();
	} else {
		CampaignList_Step( ptr == &s_campaignList.previous ? -1 : 1 );
	}
}

static void CampaignList_Draw( void ) {
	float x = FabMenu_X();

	FabUI_Begin();
	FabUI_SideShade( 530 );
	FabUI_Header( x, 94, "CAMPAIGN", "FRAGS TO RICHES",
		va( "%d / %d CLEARED   |   MATCHES %02d-%02d", s_campaignList.cleared, CAMPAIGN_MISSION_COUNT,
			s_campaignList.first + 1, MIN( s_campaignList.first + CAMPAIGN_ROWS, CAMPAIGN_MISSION_COUNT ) ) );
	Menu_Draw( &s_campaignList.menu );
}

static void CampaignList_DrawRow( void *self ) {
	menutext_s *row = self;
	menucommon_s *g = &row->generic;
	int number = s_campaignList.first + g->id + 1;
	float baseline = g->top + ( g->bottom - g->top ) * 0.65f;

	if ( !( g->flags & QMF_GRAYED ) ) {
		FabUI_DrawButton( row );
		if ( number <= s_campaignList.cleared ) {
			uiCampaignStanding_t standing;

			trap_CampaignStanding( number, &standing );
			FAB_Label( g->right - 34, baseline, 8, FAB_ALIGN_RIGHT,
				standing.rank ? va( "%d PTS  /  RANK #%d", standing.bestScore, standing.rank ) : "CLEARED",
				standing.rank ? fabCyan : fabDim, 1 );
		}
		return;
	}
	FAB_Panel( g->left, g->top, g->right - g->left, g->bottom - g->top,
		6, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.78f );
	FAB_Label( g->left + 14, baseline, 9.5f, FAB_ALIGN_LEFT, row->string, fabDim, 0.72f );
	FAB_Label( g->right - 14, baseline, 8, FAB_ALIGN_RIGHT, "LOCKED", fabDim, 0.72f );
}

static void CampaignList_Open( qboolean fromVictory ) {
	float x = FabMenu_X();
	int i, current;

	memset( &s_campaignList, 0, sizeof( s_campaignList ) );
	s_campaignList.menu.draw = CampaignList_Draw;
	s_campaignList.menu.key = CampaignList_Key;
	s_campaignList.menu.wrapAround = qtrue;
	s_campaignList.cleared = CampaignCleared();
	s_campaignList.fromVictory = fromVictory;
	current = MIN( s_campaignList.cleared, CAMPAIGN_MISSION_COUNT - 1 );

	for ( i = 0; i < CAMPAIGN_ROWS; i++ ) {
		FabUI_InitButton( &s_campaignList.rows[i], x, 156 + 27 * i, 400, 23,
			s_campaignList.labels[i], i, CampaignList_Event, FAB_BUTTON_LEFT );
		s_campaignList.rows[i].generic.ownerdraw = CampaignList_DrawRow;
		Menu_AddItem( &s_campaignList.menu, &s_campaignList.rows[i] );
	}
	FabUI_InitButton( &s_campaignList.previous, x, 382, 120, 28, "PREVIOUS", CAMPAIGN_ROWS, CampaignList_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_campaignList.next, x + 140, 382, 120, 28, "NEXT", CAMPAIGN_ROWS + 1, CampaignList_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_campaignList.back, x + 280, 382, 120, 28, "BACK", CAMPAIGN_ROWS + 2, CampaignList_Event, FAB_BUTTON_LEFT );
	Menu_AddItem( &s_campaignList.menu, &s_campaignList.previous );
	Menu_AddItem( &s_campaignList.menu, &s_campaignList.next );
	Menu_AddItem( &s_campaignList.menu, &s_campaignList.back );
	CampaignList_SetPage( current / CAMPAIGN_ROWS * CAMPAIGN_ROWS );
	UI_PushMenu( &s_campaignList.menu );
	Menu_SetCursorToItem( &s_campaignList.menu, &s_campaignList.rows[current % CAMPAIGN_ROWS] );
}

static void CampaignVictory_Draw( void ) {
	const campaignMission_t *mission = BG_CampaignMission( s_campaignVictory.number );
	int cleared = CampaignCleared();
	const campaignMission_t *next = BG_CampaignMission( cleared + 1 );
	uiCampaignScore_t score;
	char title[64], rank[96];
	const char *rankLabel;

	trap_CampaignScore( &score );
	switch ( score.mission == s_campaignVictory.number ? score.state : CAMPAIGN_SCORE_GUEST ) {
	case CAMPAIGN_SCORE_PENDING: rankLabel = "POSTING SCORE..."; break;
	case CAMPAIGN_SCORE_SAVED:
		Com_sprintf( rank, sizeof( rank ), "MISSION RANK #%d  /  BEST %d PTS", score.rank, score.bestScore );
		rankLabel = rank;
		break;
	case CAMPAIGN_SCORE_UNAVAILABLE: rankLabel = "LEADERBOARD UNAVAILABLE"; break;
	case CAMPAIGN_SCORE_UNRANKED: rankLabel = "PRACTICE CLEAR  /  SCORE UNSAVED"; break;
	default: rankLabel = "SIGN IN BEFORE A RUN TO RANK"; break;
	}

	FabUI_Begin();
	FabUI_Dim( 0.72f );
	FAB_Panel( 126, 91, 388, 365, 16, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.96f );
	FAB_Fill( 292, 111, 56, 2, fabCyan, 1 );
	FAB_Label( 320, 139, 9, FAB_ALIGN_CENTER, "FRAGS TO RICHES", fabDim, 1 );
	FAB_Text( 320, 183, 42, FAB_ALIGN_CENTER, "VICTORY", fabCyan, 1 );
	Q_strncpyz( title, mission->title, sizeof( title ) );
	FAB_Text( 320, 221, 19, FAB_ALIGN_CENTER, Q_strupr( title ), fabWhite, 1 );
	if ( score.mission == s_campaignVictory.number && score.state >= CAMPAIGN_SCORE_PENDING ) {
		FAB_Label( 320, 253, 11, FAB_ALIGN_CENTER, va( "%d PTS  /  %d:%02d.%03d  /  %d %s", score.score,
			score.elapsedMsec / 60000, score.elapsedMsec / 1000 % 60, score.elapsedMsec % 1000,
			score.deaths, score.deaths == 1 ? "DEATH" : "DEATHS" ), fabWhite, 1 );
	}
	FAB_Label( 320, 280, 9, FAB_ALIGN_CENTER, rankLabel, fabCyan, 1 );
	FAB_Fill( 176, 310, 288, 4, fabPanelLight, 1 );
	FAB_Fill( 176, 310, 288 * cleared / (float)CAMPAIGN_MISSION_COUNT, 4, fabCyan, 1 );
	FAB_Label( 320, 335, 8, FAB_ALIGN_CENTER,
		va( "%d / %d MATCHES CLEARED", cleared, CAMPAIGN_MISSION_COUNT ), fabDim, 1 );
	if ( next ) {
		Q_strncpyz( title, next->title, sizeof( title ) );
		FAB_Label( 320, 371, 9, FAB_ALIGN_CENTER, va( "UP NEXT  /  %s", Q_strupr( title ) ), fabWhite, 1 );
	} else {
		FAB_Label( 320, 371, 11, FAB_ALIGN_CENTER, "CAMPAIGN COMPLETE", fabCyan, 1 );
	}
	Menu_Draw( &s_campaignVictory.menu );
}

static sfxHandle_t CampaignVictory_Key( int key ) {
	if ( key == K_ESCAPE || key == K_MOUSE2 ) return menu_null_sound;
	return FabUI_Key( &s_campaignVictory.menu, key );
}

static void CampaignVictory_Continue( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) return;
	UI_ForceMenuOff();
	CampaignList_Open( qtrue );
}

static void CampaignVictory_Open( int number ) {
	memset( &s_campaignVictory, 0, sizeof( s_campaignVictory ) );
	s_campaignVictory.number = number;
	s_campaignVictory.menu.draw = CampaignVictory_Draw;
	s_campaignVictory.menu.key = CampaignVictory_Key;
	FabUI_InitButton( &s_campaignVictory.proceed, 210, 413, 220, 29, "CONTINUE", 0,
		CampaignVictory_Continue, FAB_BUTTON_PRIMARY );
	Menu_AddItem( &s_campaignVictory.menu, &s_campaignVictory.proceed );
	UI_PushMenu( &s_campaignVictory.menu );
}

void UI_ShowCampaignResult( void ) {
	int number = UI_ActiveCampaignMission();
	campaignResult_t result = (int)trap_Cvar_VariableValue( "g_campaignResult" );

	if ( !BG_CampaignMission( number ) || result == CAMPAIGN_RUNNING ) return;
	UI_ForceMenuOff();
	if ( result == CAMPAIGN_WON ) CampaignVictory_Open( number );
	else Campaign_Open( number, result );
}

static void UI_SinglePlayerMenu( void ) {
	SinglePlayer_Open( &s_singleplayer, "FRAGNAROK", "SINGLE PLAYER", "CHOOSE HOW TO PLAY",
		"QUICK PLAY", ID_SP_QUICK_PLAY, "CAMPAIGN", ID_SP_CAMPAIGN );
}

static void SinglePlayer_Event( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_SP_QUICK_PLAY:
		UI_LevelSelectMenu();
		break;
	case ID_SP_CAMPAIGN:
		CampaignList_Open( qfalse );
		break;
	case ID_SP_MISSION_START:
		UI_StartCampaignMission( campaignLaunchNumber );
		break;
	case ID_SP_MISSION_LEAVE:
		UI_ReturnToMainMenu();
		break;
	case ID_SP_BACK:
		UI_PopMenu();
		break;
	}
}


/*
=======================================================================

MULTIPLAYER

A listen server opened with sv_lanHost answers LAN broadcasts, so other
players find it under Find LAN Matches. Players outside the broadcast
domain can still type the host's address.

=======================================================================
*/

static void Multiplayer_Event( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_MP_ONLINE:
		UI_OnlineBrowserMenu();
		break;

	case ID_MP_LAN:
		UI_LanMenu();
		break;

	case ID_MP_HOST:
		UI_LevelSelectMenuFor( qtrue );
		break;

	case ID_MP_FIND:
		UI_LanBrowserMenu();
		break;

	case ID_MP_BACK:
		UI_PopMenu();
		break;
	}
}

static void Multiplayer_Draw( void ) {
	FabUI_Begin();
	FabUI_SideShade( 420 );
	FabUI_Header( FabMenu_X(), 120, "FRAGNAROK", "MULTIPLAYER", NULL );
	Menu_Draw( &s_multiplayer.menu );
}

static void UI_MultiplayerMenu( void ) {
    uiAuthStatus_t account;
    float x = FabMenu_X();
    trap_AuthStatus( &account );
    if ( account.signedIn && account.needsDisplayName ) {
        UI_AccountMenu();
        return;
    }
	memset( &s_multiplayer, 0, sizeof( s_multiplayer ) );
	s_multiplayer.menu.draw = Multiplayer_Draw;
	s_multiplayer.menu.fullscreen = qfalse;
	s_multiplayer.menu.wrapAround = qtrue;
	s_multiplayer.menu.showlogo = qfalse;

	FabUI_InitButton( &s_multiplayer.host, x, 180, 240, 30, "ONLINE",
		ID_MP_ONLINE, Multiplayer_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_multiplayer.find, x, 216, 240, 30, "LAN PLAY",
		ID_MP_LAN, Multiplayer_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_multiplayer.back, x, 266, 240, 30, "BACK",
		ID_MP_BACK, Multiplayer_Event, FAB_BUTTON_LEFT );

	Menu_AddItem( &s_multiplayer.menu, &s_multiplayer.host );
	Menu_AddItem( &s_multiplayer.menu, &s_multiplayer.find );
	Menu_AddItem( &s_multiplayer.menu, &s_multiplayer.back );
	UI_PushMenu( &s_multiplayer.menu );
}

static void LanMenu_Draw( void ) {
	FabUI_Begin();
	FabUI_SideShade( 420 );
	FabUI_Header( FabMenu_X(), 120, "MULTIPLAYER", "LAN PLAY", NULL );
	Menu_Draw( &s_lanmenu.menu );
}

static void UI_LanMenu( void ) {
	float x = FabMenu_X();

	memset( &s_lanmenu, 0, sizeof( s_lanmenu ) );
	s_lanmenu.menu.draw = LanMenu_Draw;
	s_lanmenu.menu.fullscreen = qfalse;
	s_lanmenu.menu.wrapAround = qtrue;
	s_lanmenu.menu.showlogo = qfalse;
	FabUI_InitButton( &s_lanmenu.host, x, 180, 240, 30, "HOST LAN MATCH",
		ID_MP_HOST, Multiplayer_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_lanmenu.find, x, 216, 240, 30, "FIND LAN MATCHES",
		ID_MP_FIND, Multiplayer_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_lanmenu.back, x, 266, 240, 30, "BACK",
		ID_MP_BACK, Multiplayer_Event, FAB_BUTTON_LEFT );
	Menu_AddItem( &s_lanmenu.menu, &s_lanmenu.host );
	Menu_AddItem( &s_lanmenu.menu, &s_lanmenu.find );
	Menu_AddItem( &s_lanmenu.menu, &s_lanmenu.back );
	UI_PushMenu( &s_lanmenu.menu );
}

static void LanBrowser_Scan( void ) {
	s_lanbrowser.lastScan = uis.realtime;
	s_lanbrowser.lastUpdate = 0;
	trap_Cmd_ExecuteText( EXEC_APPEND, "localservers\n" );
}

static void Multiplayer_Join( const char *address ) {
	if ( !address[0] ) {
		return;
	}
	// Addresses are interpolated into an engine command, so reject command syntax.
	{
		const char *p;
		for ( p = address; *p; p++ ) {
			if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
				( *p >= '0' && *p <= '9' ) || strchr( ".:-[]", *p ) ) ) return;
		}
	}
	// The joining client leaves its own menu scene; flag it so the main menu
	// can bring that scene back if the connection ends.
	trap_Cvar_Set( "sv_lanHost", "0" );
	trap_Cvar_Set( "sv_onlineHost", "0" );
	trap_Cvar_Set( "ui_lanClient", "1" );
	trap_Cmd_ExecuteText( EXEC_APPEND, va( "set g_menuBackground 0\nconnect %s\n", address ) );
}

static void LanBrowser_Update( void ) {
	char info[MAX_STRING_CHARS];
	char hostname[32];
	char mapname[MAX_QPATH];
	int count, i, humans, bots;

	if ( uis.realtime - s_lanbrowser.lastScan >= LAN_RESCAN_MSEC ) {
		LanBrowser_Scan();
	}
	if ( s_lanbrowser.lastUpdate && uis.realtime - s_lanbrowser.lastUpdate < LAN_UPDATE_MSEC ) {
		return;
	}
	s_lanbrowser.lastUpdate = uis.realtime;

	count = trap_LAN_GetServerCount( AS_LOCAL );
	// A rescan clears the engine's list; keep the old rows until replies land.
	if ( count <= 0 && s_lanbrowser.numServers > 0 &&
		uis.realtime - s_lanbrowser.lastScan < 1500 ) {
		return;
	}
	if ( count > LAN_MAX_ROWS ) {
		count = LAN_MAX_ROWS;
	}

	s_lanbrowser.numServers = 0;
	for ( i = 0; i < count; i++ ) {
		trap_LAN_GetServerInfo( AS_LOCAL, i, info, sizeof( info ) );
		if ( !info[0] ) {
			continue;
		}
		Q_strncpyz( hostname, Info_ValueForKey( info, "hostname" ), sizeof( hostname ) );
		Q_CleanStr( hostname );
		Q_strncpyz( mapname, Info_ValueForKey( info, "mapname" ), sizeof( mapname ) );
		humans = atoi( Info_ValueForKey( info, "g_humanplayers" ) );
		bots = atoi( Info_ValueForKey( info, "clients" ) ) - humans;
		if ( bots < 0 ) {
			bots = 0;
		}
		Q_strncpyz( s_lanbrowser.rowAddress[s_lanbrowser.numServers],
			Info_ValueForKey( info, "addr" ), LAN_ADDRESS_LENGTH );
		Com_sprintf( s_lanbrowser.rowText[s_lanbrowser.numServers], sizeof( s_lanbrowser.rowText[0] ),
			"%s   %s / %s   %d %s, %d bots", hostname[0] ? hostname : "Unnamed",
			Q_strupr( mapname ), FAB_ModeName( atoi( Info_ValueForKey( info, "gametype" ) ), info ),
			humans, humans == 1 ? "player" : "players", bots );
		s_lanbrowser.numServers++;
	}

	for ( i = 0; i < LAN_MAX_ROWS; i++ ) {
		if ( i < s_lanbrowser.numServers ) {
			s_lanbrowser.rows[i].generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
		} else {
			s_lanbrowser.rows[i].generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
		}
	}

	if ( s_lanbrowser.numServers ) {
		Com_sprintf( s_lanbrowser.status, sizeof( s_lanbrowser.status ),
			"%d %s FOUND. CLICK ONE TO JOIN.", s_lanbrowser.numServers,
			s_lanbrowser.numServers == 1 ? "MATCH" : "MATCHES" );
	} else if ( uis.realtime - s_lanbrowser.lastScan < 1500 ) {
		Q_strncpyz( s_lanbrowser.status, "SEARCHING THE LOCAL NETWORK", sizeof( s_lanbrowser.status ) );
	} else {
		Q_strncpyz( s_lanbrowser.status, "NO LAN MATCHES FOUND YET", sizeof( s_lanbrowser.status ) );
	}
}

static void LanBrowser_Event( void *ptr, int event ) {
	int id;

	if ( event != QM_ACTIVATED ) {
		return;
	}

	id = ((menucommon_s *)ptr)->id;
	if ( id >= ID_LAN_SERVER && id < ID_LAN_SERVER + LAN_MAX_ROWS ) {
		if ( id - ID_LAN_SERVER < s_lanbrowser.numServers ) {
			Multiplayer_Join( s_lanbrowser.rowAddress[id - ID_LAN_SERVER] );
		}
		return;
	}

	switch ( id ) {
	case ID_LAN_CONNECT:
		Multiplayer_Join( s_lanbrowser.address.field.buffer );
		break;

	case ID_LAN_REFRESH:
		LanBrowser_Scan();
		break;

	case ID_LAN_BACK:
		UI_PopMenu();
		break;
	}
}

static sfxHandle_t LanBrowser_Key( int key ) {
	menucommon_s *item = Menu_ItemAtCursor( &s_lanbrowser.menu );

	// Enter in the address box joins instead of moving to the next item.
	if ( item == &s_lanbrowser.address.generic &&
		( key == K_ENTER || key == K_KP_ENTER ) ) {
		Multiplayer_Join( s_lanbrowser.address.field.buffer );
		return menu_in_sound;
	}
	return Menu_DefaultKey( &s_lanbrowser.menu, key );
}

#define LAN_W		400
#define LAN_ROW_Y	132
#define LAN_ROW_H	24

static void LanBrowser_Draw( void ) {
	float x = FabMenu_X();

	LanBrowser_Update();
	FabUI_Begin();
	FabUI_SideShade( 540 );
	FabUI_Header( x, 84, "MULTIPLAYER", "LAN MATCHES", s_lanbrowser.status );
	if ( !s_lanbrowser.numServers ) {
		FAB_Fill( x, LAN_ROW_Y, LAN_W, LAN_ROW_H * 2, fabPanelLight, 0.35f );
		FAB_Label( x + LAN_W * 0.5f, LAN_ROW_Y + LAN_ROW_H + 3, 7.5f, FAB_ALIGN_CENTER,
			"MATCHES HOSTED ON YOUR NETWORK SHOW UP HERE", fabDim, 0.8f );
	}
	Menu_Draw( &s_lanbrowser.menu );
}

static void UI_LanBrowserMenu( void ) {
	float x = FabMenu_X();
	float y;
	int i;

	memset( &s_lanbrowser, 0, sizeof( s_lanbrowser ) );
	s_lanbrowser.menu.draw = LanBrowser_Draw;
	s_lanbrowser.menu.key = LanBrowser_Key;
	s_lanbrowser.menu.fullscreen = qfalse;
	s_lanbrowser.menu.wrapAround = qtrue;
	s_lanbrowser.menu.showlogo = qfalse;

	for ( i = 0; i < LAN_MAX_ROWS; i++ ) {
		FabUI_InitButton( &s_lanbrowser.rows[i], x, LAN_ROW_Y + i * ( LAN_ROW_H + 3 ), LAN_W,
			LAN_ROW_H, s_lanbrowser.rowText[i], ID_LAN_SERVER + i, LanBrowser_Event, FAB_BUTTON_LEFT );
		s_lanbrowser.rows[i].generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
		Menu_AddItem( &s_lanbrowser.menu, &s_lanbrowser.rows[i] );
	}

	y = LAN_ROW_Y + LAN_MAX_ROWS * ( LAN_ROW_H + 3 ) + 10;
	FabUI_InitField( &s_lanbrowser.address, x, y, LAN_W, 26, "ADDRESS", ID_LAN_ADDRESS,
		LAN_ADDRESS_LENGTH - 1 );
	Menu_AddItem( &s_lanbrowser.menu, &s_lanbrowser.address );

	y += 34;
	FabUI_InitButton( &s_lanbrowser.connect, x, y, 150, 28, "JOIN ADDRESS",
		ID_LAN_CONNECT, LanBrowser_Event, FAB_BUTTON_PRIMARY );
	FabUI_InitButton( &s_lanbrowser.refresh, x + 158, y, 120, 28, "REFRESH",
		ID_LAN_REFRESH, LanBrowser_Event, 0 );
	FabUI_InitButton( &s_lanbrowser.back, x + 286, y, LAN_W - 286, 28, "BACK",
		ID_LAN_BACK, LanBrowser_Event, 0 );
	Menu_AddItem( &s_lanbrowser.menu, &s_lanbrowser.connect );
	Menu_AddItem( &s_lanbrowser.menu, &s_lanbrowser.refresh );
	Menu_AddItem( &s_lanbrowser.menu, &s_lanbrowser.back );

	Q_strncpyz( s_lanbrowser.status, "SEARCHING THE LOCAL NETWORK", sizeof( s_lanbrowser.status ) );
	UI_PushMenu( &s_lanbrowser.menu );
	// Focus Refresh rather than the hidden first row.
	Menu_SetCursorToItem( &s_lanbrowser.menu, &s_lanbrowser.refresh );
	LanBrowser_Scan();
}

// Normalize configured IPv4 endpoints before using them in console commands
// and matching them against the engine's numeric ping addresses.
static qboolean OnlineBrowser_Address( const char *text, char *address, int size ) {
	int parts[5], i, digits;

	for ( i = 0; i < 5; i++ ) {
		parts[i] = digits = 0;
		while ( *text >= '0' && *text <= '9' ) {
			parts[i] = parts[i] * 10 + *text++ - '0';
			if ( ++digits > 5 || parts[i] > ( i == 4 ? 65535 : 255 ) ) {
				return qfalse;
			}
		}
		if ( !digits ) {
			return qfalse;
		}
		if ( i < 3 ) {
			if ( *text++ != '.' ) return qfalse;
		} else if ( i == 3 ) {
			if ( !*text ) {
				parts[4] = 27960;
				break;
			}
			if ( *text++ != ':' ) return qfalse;
		} else if ( *text || !parts[4] ) {
			return qfalse;
		}
	}
	Com_sprintf( address, size, "%d.%d.%d.%d:%d",
		parts[0], parts[1], parts[2], parts[3], parts[4] );
	return qtrue;
}

static void OnlineBrowser_ClearPings( void ) {
	int i;

	for ( i = 0; i < MAX_PINGREQUESTS; i++ ) {
		trap_LAN_ClearPing( i );
	}
}

static void OnlineBrowser_Add( const char *address ) {
	int i;
	if ( !address[0] || s_onlinebrowser.numAddresses == ONLINE_MAX_SERVERS ) return;
	for ( i = 0; i < s_onlinebrowser.numAddresses; i++ ) {
		if ( !Q_stricmp( address, s_onlinebrowser.matches[i].address ) ) return;
	}
	Q_strncpyz( s_onlinebrowser.matches[s_onlinebrowser.numAddresses++].address,
		address, sizeof( s_onlinebrowser.matches[0].address ) );
}

static void OnlineBrowser_Scan( void ) {
	char servers[MAX_STRING_CHARS], address[LAN_ADDRESS_LENGTH], *cursor, *token;
	OnlineBrowser_ClearPings();
	memset( s_onlinebrowser.matches, 0, sizeof( s_onlinebrowser.matches ) );
	s_onlinebrowser.numAddresses = s_onlinebrowser.numLive = 0;
	s_onlinebrowser.directoryCount = 0;
	s_onlinebrowser.page = 0;
	s_onlinebrowser.lastScan = uis.realtime;
	s_onlinebrowser.refreshing = qtrue;
	trap_Cvar_VariableStringBuffer( "ui_onlineServers", servers, sizeof( servers ) );
	cursor = servers;
	while ( ( token = COM_Parse( &cursor ) )[0] ) {
		if ( OnlineBrowser_Address( token, address, sizeof( address ) ) ) OnlineBrowser_Add( address );
	}
	s_onlinebrowser.numConfigured = s_onlinebrowser.numAddresses;
	trap_Cmd_ExecuteText( EXEC_NOW, va( "globalservers 1 %d empty full\n",
		(int)trap_Cvar_VariableValue( "protocol" ) ) );
}

static void OnlineBrowser_Update( void ) {
	char address[LAN_ADDRESS_LENGTH], info[MAX_INFO_STRING], mapname[MAX_QPATH];
	onlinematch_t *match;
	int i, j, ping, clients, humans, bots, pending, row, count, pages;
	qboolean waiting;

	if ( !s_onlinebrowser.refreshing && uis.realtime - s_onlinebrowser.lastScan >= ONLINE_RESCAN_MSEC ) {
		OnlineBrowser_Scan();
	}
	count = trap_LAN_GetServerCount( AS_GLOBAL );
	if ( s_onlinebrowser.refreshing && count > s_onlinebrowser.directoryCount ) {
		for ( i = s_onlinebrowser.directoryCount; i < count; i++ ) {
			trap_LAN_GetServerAddressString( AS_GLOBAL, i, address, sizeof( address ) );
			OnlineBrowser_Add( address );
		}
		s_onlinebrowser.directoryCount = count;
	}
	waiting = count < 0 && uis.realtime - s_onlinebrowser.lastScan < ONLINE_TIMEOUT_MSEC;
	if ( s_onlinebrowser.refreshing ) {
		for ( i = 0; i < MAX_PINGREQUESTS; i++ ) {
			trap_LAN_GetPing( i, address, sizeof( address ), &ping );
			if ( !address[0] ) continue;
			trap_LAN_GetPingInfo( i, info, sizeof( info ) );
			if ( !ping || !info[0] ) continue;
			for ( j = 0; j < s_onlinebrowser.numAddresses; j++ ) {
				match = &s_onlinebrowser.matches[j];
				if ( !match->pending || Q_stricmp( address, match->address ) ) continue;
				match->pending = qfalse;
				match->live = qtrue;
				match->ping = ping;
				OnlineBrowser_Address( Info_ValueForKey( info, "match_lobby" ), match->lobby, sizeof( match->lobby ) );
				match->matchSlot = atoi( Info_ValueForKey( info, "match_slot" ) );
				FAB_MapTitle( Info_ValueForKey( info, "mapname" ), mapname, sizeof( mapname ) );
				Q_CleanStr( mapname );
				Com_sprintf( match->title, sizeof( match->title ), "%s / %s",
					mapname, FAB_ModeName( atoi( Info_ValueForKey( info, "gametype" ) ), info ) );
				clients = Com_Clamp( 0, MAX_CLIENTS, atoi( Info_ValueForKey( info, "clients" ) ) );
				humans = Com_Clamp( 0, clients, atoi( Info_ValueForKey( info, "g_humanplayers" ) ) );
				bots = clients - humans;
				Com_sprintf( match->detail, sizeof( match->detail ), "%d %s + %d bots%s",
					humans, humans == 1 ? "player" : "players", bots,
					atoi( Info_ValueForKey( info, "g_needpass" ) ) ? " / LOCKED" : "" );
				if ( trap_Cvar_VariableValue( "developer" ) ) {
					Com_Printf( "Online match: %s %s / %s (%d ms)\n",
						match->address, match->title, match->detail, ping );
				}
				break;
			}
			trap_LAN_ClearPing( i );
		}
	}

	pending = row = 0;
	for ( i = 0; i < s_onlinebrowser.numAddresses; i++ ) {
		match = &s_onlinebrowser.matches[i];
		if ( match->pending && uis.realtime - match->started >= ONLINE_TIMEOUT_MSEC ) {
			match->pending = match->live = qfalse;
		}
		if ( match->pending ) pending++;
		if ( match->live ) row++;
	}
	// Reuse the engine's bounded ping queue in batches, including lists larger than one page.
	for ( i = 0; i < s_onlinebrowser.numAddresses; i++ ) {
		match = &s_onlinebrowser.matches[i];
		if ( match->queried ) continue;
		if ( pending < MAX_PINGREQUESTS ) {
			match->queried = match->pending = qtrue;
			match->started = uis.realtime;
			trap_Cmd_ExecuteText( EXEC_NOW, va( "ping %s\n", match->address ) );
			pending++;
		}
		waiting = qtrue;
	}
	s_onlinebrowser.numLive = row;
	pages = row ? ( row + ONLINE_MAX_ROWS - 1 ) / ONLINE_MAX_ROWS : 1;
	if ( s_onlinebrowser.page >= pages ) s_onlinebrowser.page = pages - 1;
	row = j = 0;
	for ( i = 0; i < s_onlinebrowser.numAddresses && j < ONLINE_MAX_ROWS; i++ ) {
		if ( !s_onlinebrowser.matches[i].live ) continue;
		if ( row++ < s_onlinebrowser.page * ONLINE_MAX_ROWS ) continue;
		s_onlinebrowser.rowIndex[j] = i;
		s_onlinebrowser.rows[j++].generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
	}
	for ( i = j; i < ONLINE_MAX_ROWS; i++ ) {
		s_onlinebrowser.rows[i].generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
		if ( Menu_ItemAtCursor( &s_onlinebrowser.menu ) == &s_onlinebrowser.rows[i].generic ) {
			Menu_SetCursorToItem( &s_onlinebrowser.menu, &s_onlinebrowser.refresh );
		}
	}
	if ( s_onlinebrowser.focusFirstMatch && j ) {
		// Match discovery finishes after opening the menu; preserve any navigation made while waiting.
		if ( Menu_ItemAtCursor( &s_onlinebrowser.menu ) == &s_onlinebrowser.refresh.generic ) {
			Menu_SetCursorToItem( &s_onlinebrowser.menu, &s_onlinebrowser.rows[0] );
		}
		s_onlinebrowser.focusFirstMatch = qfalse;
	}
	s_onlinebrowser.previous.generic.flags = s_onlinebrowser.page ? QMF_NODEFAULTINIT : QMF_NODEFAULTINIT | QMF_GRAYED;
	s_onlinebrowser.next.generic.flags = s_onlinebrowser.page + 1 < pages ? QMF_NODEFAULTINIT : QMF_NODEFAULTINIT | QMF_GRAYED;
	if ( s_onlinebrowser.refreshing && !pending && !waiting && trap_Cvar_VariableValue( "developer" ) ) {
		Com_Printf( "Online browser: %d live matches, %d configured endpoints, %d directory endpoints\n",
			s_onlinebrowser.numLive, s_onlinebrowser.numConfigured, s_onlinebrowser.directoryCount );
	}
	s_onlinebrowser.refreshing = pending > 0 || waiting;
	if ( s_onlinebrowser.numLive ) {
		Com_sprintf( s_onlinebrowser.status, sizeof( s_onlinebrowser.status ), "%d %s / PAGE %d OF %d%s",
			s_onlinebrowser.numLive, s_onlinebrowser.numLive == 1 ? "MATCH" : "MATCHES",
			s_onlinebrowser.page + 1, pages, count < 0 && !waiting ? " / DIRECTORY OFFLINE" : "" );
	} else {
		Q_strncpyz( s_onlinebrowser.status, s_onlinebrowser.refreshing ? "CHECKING ONLINE MATCHES" :
			count < 0 ? "DIRECTORY UNAVAILABLE" : "NO ONLINE MATCHES",
			sizeof( s_onlinebrowser.status ) );
	}
}

static void OnlineBrowser_RowDraw( void *self ) {
	menucommon_s *g = &((menutext_s *)self)->generic;
	onlinematch_t *match = &s_onlinebrowser.matches[s_onlinebrowser.rowIndex[g->id - ID_ONLINE_SERVER]];
	qboolean focus = g->parent->cursor == g->menuPosition;
	float width, size;

	FAB_Fill( g->left, g->top, ONLINE_W, ONLINE_ROW_H, focus ? fabCyan : fabPanelLight, focus ? 0.22f : 0.75f );
	if ( focus ) FAB_Fill( g->left, g->top + 3, 2, ONLINE_ROW_H - 6, fabCyan, 1 );
	width = FAB_TextWidth( match->title, 11, 0 );
	size = width > ONLINE_W - 100 ? 11 * ( ONLINE_W - 100 ) / width : 11;
	FAB_Text( g->left + 12, g->top + 12, size, FAB_ALIGN_LEFT, match->title, fabWhite, 1 );
	width = FAB_TextWidth( match->detail, 8, 0 );
	size = width > ONLINE_W - 24 ? 8 * ( ONLINE_W - 24 ) / width : 8;
	FAB_Text( g->left + 12, g->top + 25, size, FAB_ALIGN_LEFT, match->detail, fabDim, 1 );
	FAB_Text( g->right - 12, g->top + 12, 8, FAB_ALIGN_RIGHT, va( "%d ms", match->ping ), fabCyan, 1 );
}

static void OnlineBrowser_Event( void *ptr, int event ) {
	int id = ((menucommon_s *)ptr)->id;

	if ( event != QM_ACTIVATED ) return;
	if ( id != ID_ONLINE_BACK && !UI_RequireOnlineLogin() ) return;
	if ( id >= ID_ONLINE_SERVER && id < ID_ONLINE_SERVER + ONLINE_MAX_ROWS ) {
		onlinematch_t *match = &s_onlinebrowser.matches[s_onlinebrowser.rowIndex[id - ID_ONLINE_SERVER]];
		if ( match->live ) {
			OnlineBrowser_ClearPings();
			trap_Cvar_SetValue( "matchPreference", match->matchSlot );
			Multiplayer_Join( match->lobby[0] ? match->lobby : match->address );
		}
	} else if ( id == ID_ONLINE_REFRESH ) {
		OnlineBrowser_Scan();
	} else if ( id == ID_ONLINE_BACK ) {
		OnlineBrowser_ClearPings();
		UI_PopMenu();
	} else if ( id == ID_ONLINE_HOST ) {
		OnlineBrowser_ClearPings();
		UI_LevelSelectMenuFor( qtrue );
		s_levelselect.online = qtrue;
	} else if ( id == ID_ONLINE_PREV ) {
		if ( s_onlinebrowser.page > 0 ) s_onlinebrowser.page--;
	} else if ( id == ID_ONLINE_NEXT ) {
		if ( ( s_onlinebrowser.page + 1 ) * ONLINE_MAX_ROWS < s_onlinebrowser.numLive ) s_onlinebrowser.page++;
	}
}

static sfxHandle_t OnlineBrowser_Key( int key ) {
	if ( key == K_ESCAPE || key == K_MOUSE2 ) OnlineBrowser_ClearPings();
	return Menu_DefaultKey( &s_onlinebrowser.menu, key );
}

static void OnlineBrowser_Draw( void ) {
	float x = FabMenu_X();
	uiAuthStatus_t status;

	trap_AuthStatus( &status );
	if ( !status.signedIn || status.needsDisplayName ) {
		OnlineBrowser_ClearPings();
		UI_PopMenu();
		UI_RequireOnlineLogin();
		return;
	}

	OnlineBrowser_Update();
	FabUI_Begin();
	FabUI_SideShade( 620 );
	FabUI_Header( x, 78, "MULTIPLAYER", "ONLINE", s_onlinebrowser.status );
	Menu_Draw( &s_onlinebrowser.menu );
}

static void MatchWaiting_Event( void *ptr, int event ) {
	int id = ((menucommon_s *)ptr)->id;
	if ( event != QM_ACTIVATED ) return;
	if ( id == 3 ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		UI_ForceMenuOff();
	} else {
		trap_Cvar_SetValue( "matchPreference", id );
	}
}

static sfxHandle_t MatchWaiting_Key( int key ) {
	if ( key == K_ESCAPE || key == K_MOUSE2 ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		UI_ForceMenuOff();
		return menu_out_sound;
	}
	return Menu_DefaultKey( &s_matchwaiting.menu, key );
}

static void MatchWaiting_Draw( void ) {
	char info[MAX_INFO_STRING];
	float x = FabMenu_X();
	int position = (int)trap_Cvar_VariableValue( "ui_matchQueuePosition" );
	int preference = (int)trap_Cvar_VariableValue( "matchPreference" );
	trap_GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	FabUI_Begin();
	FabUI_Dim( 1.0f );
	FabUI_Header( x, 90, "MULTIPLAYER", position > 0 ? "WAITING ROOM" : "JOINING MATCH", position > 0 ?
		va( "QUEUE POSITION %d / %s", position, preference ? va( "PREFER MATCH %d", preference ) : "EITHER MATCH" ) :
		"RESERVING YOUR PLACE IN A MATCH" );
	FAB_Text( x, 159, 9, FAB_ALIGN_LEFT, "YOU WILL JOIN AUTOMATICALLY WHEN A PLACE OPENS.", fabWhite, 1 );
	FAB_Text( x, 179, 8, FAB_ALIGN_LEFT, "BOTS MAKE ROOM IMMEDIATELY. QUEUE WHEN BOTH MATCHES ARE FULL.", fabDim, 1 );
	FAB_Text( x, 213, 8, FAB_ALIGN_LEFT, Info_ValueForKey( info, "g_matchSummary1" ), fabCyan, 1 );
	FAB_Text( x, 235, 8, FAB_ALIGN_LEFT, Info_ValueForKey( info, "g_matchSummary2" ), fabCyan, 1 );
	Menu_Draw( &s_matchwaiting.menu );
}

void UI_MatchWaitingFrame( void ) {
	uiClientState_t state;
	char info[MAX_INFO_STRING];
	float x = FabMenu_X();
	trap_GetClientState( &state );
	if ( state.connState != CA_ACTIVE ) return;
	trap_GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	if ( !atoi( Info_ValueForKey( info, "g_matchLobby" ) ) ) {
		if ( uis.activemenu == &s_matchwaiting.menu ) UI_ForceMenuOff();
		return;
	}
	if ( uis.activemenu == &s_matchwaiting.menu ) return;
	memset( &s_matchwaiting, 0, sizeof( s_matchwaiting ) );
	s_matchwaiting.menu.draw = MatchWaiting_Draw;
	s_matchwaiting.menu.key = MatchWaiting_Key;
	// Keep cgame processing reliable server commands behind the opaque menu.
	// Fullscreen menus suspend that work, including automatic match transfers.
	s_matchwaiting.menu.fullscreen = qfalse;
	s_matchwaiting.menu.wrapAround = qtrue;
	FabUI_InitButton( &s_matchwaiting.any, x, 279, 300, 30, "WAIT FOR EITHER MATCH", 0, MatchWaiting_Event, FAB_BUTTON_PRIMARY );
	FabUI_InitButton( &s_matchwaiting.first, x, 315, 300, 30, "PREFER MATCH 1", 1, MatchWaiting_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_matchwaiting.second, x, 351, 300, 30, "PREFER MATCH 2", 2, MatchWaiting_Event, FAB_BUTTON_LEFT );
	FabUI_InitButton( &s_matchwaiting.leave, x, 407, 180, 30, "LEAVE WAITING ROOM", 3, MatchWaiting_Event, FAB_BUTTON_LEFT );
	Menu_AddItem( &s_matchwaiting.menu, &s_matchwaiting.any );
	Menu_AddItem( &s_matchwaiting.menu, &s_matchwaiting.first );
	Menu_AddItem( &s_matchwaiting.menu, &s_matchwaiting.second );
	Menu_AddItem( &s_matchwaiting.menu, &s_matchwaiting.leave );
	UI_ForceMenuOff();
	UI_PushMenu( &s_matchwaiting.menu );
}

static void UI_OnlineBrowserMenu( void ) {
	float x = FabMenu_X();
	int i;

	if ( !UI_RequireOnlineLogin() ) return;
	memset( &s_onlinebrowser, 0, sizeof( s_onlinebrowser ) );
	s_onlinebrowser.focusFirstMatch = qtrue;
	s_onlinebrowser.menu.draw = OnlineBrowser_Draw;
	s_onlinebrowser.menu.key = OnlineBrowser_Key;
	s_onlinebrowser.menu.fullscreen = qfalse;
	s_onlinebrowser.menu.wrapAround = qtrue;
	s_onlinebrowser.menu.showlogo = qfalse;
	for ( i = 0; i < ONLINE_MAX_ROWS; i++ ) {
		FabUI_InitButton( &s_onlinebrowser.rows[i], x, ONLINE_ROW_Y + i * ( ONLINE_ROW_H + 2 ), ONLINE_W, ONLINE_ROW_H, "",
			ID_ONLINE_SERVER + i, OnlineBrowser_Event, FAB_BUTTON_LEFT );
		s_onlinebrowser.rows[i].generic.ownerdraw = OnlineBrowser_RowDraw;
		s_onlinebrowser.rows[i].generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
		Menu_AddItem( &s_onlinebrowser.menu, &s_onlinebrowser.rows[i] );
	}
	FabUI_InitButton( &s_onlinebrowser.host, x, 386, 200, 26, "HOST ONLINE MATCH",
		ID_ONLINE_HOST, OnlineBrowser_Event, FAB_BUTTON_PRIMARY );
	FabUI_InitButton( &s_onlinebrowser.previous, x + 210, 386, 40, 26, "<",
		ID_ONLINE_PREV, OnlineBrowser_Event, 0 );
	FabUI_InitButton( &s_onlinebrowser.next, x + 258, 386, 40, 26, ">",
		ID_ONLINE_NEXT, OnlineBrowser_Event, 0 );
	FabUI_InitButton( &s_onlinebrowser.refresh, x, 418, 150, 26, "REFRESH",
		ID_ONLINE_REFRESH, OnlineBrowser_Event, 0 );
	FabUI_InitButton( &s_onlinebrowser.back, x + 158, 418, 120, 26, "BACK",
		ID_ONLINE_BACK, OnlineBrowser_Event, 0 );
	Menu_AddItem( &s_onlinebrowser.menu, &s_onlinebrowser.host );
	Menu_AddItem( &s_onlinebrowser.menu, &s_onlinebrowser.previous );
	Menu_AddItem( &s_onlinebrowser.menu, &s_onlinebrowser.next );
	Menu_AddItem( &s_onlinebrowser.menu, &s_onlinebrowser.refresh );
	Menu_AddItem( &s_onlinebrowser.menu, &s_onlinebrowser.back );
	UI_PushMenu( &s_onlinebrowser.menu );
	Menu_SetCursorToItem( &s_onlinebrowser.menu, &s_onlinebrowser.refresh );
	OnlineBrowser_Scan();
}

/*
=================
UI_FabMenuDebug

"fab_menu <screen>" opens one screen directly.
=================
*/
void UI_FabMenuDebug( const char *name ) {
	if ( !Q_stricmp( name, "play" ) ) UI_LevelSelectMenuFor( qfalse );
    else if ( !Q_stricmp( name, "account" ) ) UI_AccountMenu();
    else if ( !Q_stricmp( name, "accountstatus" ) ) {
        uiAuthStatus_t status;
        trap_AuthStatus( &status );
        Com_Printf( "Account status: configured=%d busy=%d signedIn=%d %s\n",
            status.configured, status.busy, status.signedIn, status.message );
    }
	else if ( !Q_stricmp( name, "status" ) ) {
		Com_Printf( "Menu status: %s\n", uis.activemenu == &s_account.menu ?
			( s_account.continueOnline ? "online-login" : "account" ) :
			uis.activemenu == &s_main.menu ? "main" :
			uis.activemenu == &s_singleplayer.menu ? "single-player" :
			uis.activemenu == &s_campaignList.menu ? "campaign" :
			uis.activemenu == &s_campaignVictory.menu ? "campaign-victory" :
			uis.activemenu == &s_fragsToRiches.menu ? "frags-to-riches" :
			uis.activemenu == &s_levelselect.menu && !s_levelselect.host && !s_levelselect.inMatch ? "play" :
			uis.activemenu == &s_multiplayer.menu ? "multiplayer" :
			uis.activemenu == &s_lanmenu.menu ? "lan-play" :
			uis.activemenu == &s_lanbrowser.menu ? "lan-browser" :
			uis.activemenu == &s_matchwaiting.menu ? "waiting" :
			uis.activemenu == &s_onlinebrowser.menu ? "online" :
			UI_CreditMenuActive() ? "credits" : "other" );
		if ( uis.activemenu == &s_campaignList.menu ) {
			Com_Printf( "Campaign list: first=%d cleared=%d\n", s_campaignList.first + 1, s_campaignList.cleared );
		} else if ( uis.activemenu == &s_campaignVictory.menu ) {
			uiCampaignScore_t score;
			trap_CampaignScore( &score );
			Com_Printf( "Campaign score: mission=%d elapsed=%d deaths=%d score=%d best=%d rank=%d state=%d\n",
				score.mission, score.elapsedMsec, score.deaths, score.score, score.bestScore, score.rank, score.state );
		}
	}
	else if ( !Q_stricmp( name, "host" ) ) UI_LevelSelectMenuFor( qtrue );
	else if ( !Q_stricmp( name, "frags-to-riches" ) ) CampaignList_Open( qfalse );
	else if ( !Q_stricmp( name, "change" ) ) UI_ChangeMatchMenu();
	else if ( !Q_stricmp( name, "mp" ) ) UI_MultiplayerMenu();
	else if ( !Q_stricmp( name, "lanplay" ) ) UI_LanMenu();
	else if ( !Q_stricmp( name, "lan" ) ) UI_LanBrowserMenu();
	else if ( !Q_stricmp( name, "online" ) ) UI_OnlineBrowserMenu();
	else if ( !Q_stricmp( name, "activate" ) || !Q_stricmp( name, "focus" ) ) {
        qboolean focusOnly = !Q_stricmp( name, "focus" );
		// a dropdown left open by an earlier step would take this key
		FabUI_CloseDropdown();
		if ( trap_Argc() > 2 && uis.activemenu ) {
			int item = atoi( UI_Argv( 2 ) );
			if ( item >= 0 && item < uis.activemenu->nitems ) {
				Menu_SetCursor( uis.activemenu, item );
			}
		}
        if ( focusOnly ) return;
		UI_KeyEvent( K_ENTER, qtrue );
		UI_KeyEvent( K_ENTER, qfalse );
	}
	else if ( !Q_stricmp( name, "setup" ) ) UI_SetupMenu();
	else if ( !Q_stricmp( name, "player" ) ) UI_PlayerSettingsMenu();
	else if ( !Q_stricmp( name, "controls" ) ) UI_ControlsMenu();
	else if ( !Q_stricmp( name, "system" ) ) UI_GraphicsOptionsMenu();
	else if ( !Q_stricmp( name, "options" ) ) UI_PreferencesMenu();
#if FAB_VOICE_CHAT
	else if ( !Q_stricmp( name, "voice" ) ) UI_VoiceMenu();
	else if ( !Q_stricmp( name, "credits" ) ) UI_CreditMenu();
#endif

	else if ( !Q_stricmp( name, "confirm" ) ) UI_ConfirmMenu( "EXIT GAME?", 0, NULL );
	else if ( !Q_stricmp( name, "pause" ) ) UI_InGameMenu();
	else if ( !Q_stricmp( name, "back" ) ) UI_PopMenu();
	else if ( !Q_stricmp( name, "mouse" ) ) {
		// "fab_menu mouse x y" puts the cursor at 640x480 coordinates
		uis.cursorx = atoi( UI_Argv( 2 ) );
		uis.cursory = atoi( UI_Argv( 3 ) );
		UI_MouseEvent( 0, 0 );
		Com_Printf( "fab_menu: cursor %d,%d menu cursor %d catcher %d active %p\n", uis.cursorx, uis.cursory,
			uis.activemenu ? uis.activemenu->cursor : -1, trap_Key_GetCatcher(), (void *)uis.activemenu );
	}
	else if ( !Q_stricmp( name, "key" ) && trap_Argc() > 2 ) {
		// Numeric UI keycodes also accept K_CHAR_FLAG for field input.
		int key = atoi( UI_Argv( 2 ) );
		UI_KeyEvent( key, qtrue );
		UI_KeyEvent( key, qfalse );
		if ( uis.activemenu ) {
			menucommon_s *item = Menu_ItemAtCursor( uis.activemenu );
			if ( item && item->type == MTYPE_SPINCONTROL ) {
				menulist_s *setting = (menulist_s *)item;
				Com_Printf( "fab_menu setting: %s=%s\n", item->name,
					setting->itemnames[setting->curvalue] );
			}
			if ( FabUI_DropdownOpen() ) {
				Com_Printf( "fab_menu dropdown open\n" );
			}
		}
	}
	else UI_MainMenu();
}
#endif


/*
===============
UI_TeamArenaExists
===============
*/
static qboolean UI_TeamArenaExists( void ) {
	int		numdirs;
	char	dirlist[2048];
	char	*dirptr;
  char  *descptr;
	int		i;
	int		dirlen;

	numdirs = trap_FS_GetFileList( "$modlist", "", dirlist, sizeof(dirlist) );
	dirptr  = dirlist;
	for( i = 0; i < numdirs; i++ ) {
		dirlen = strlen( dirptr ) + 1;
    descptr = dirptr + dirlen;
		if (Q_stricmp(dirptr, BASETA) == 0) {
			return qtrue;
		}
    dirptr += dirlen + strlen(descptr) + 1;
	}
	return qfalse;
}


/*
===============
UI_MainMenu

The main menu only comes up when not in a game,
so make sure that the attract loop server is down
and that local cinematics are killed
===============
*/
void UI_MainMenu( void ) {
	int		y;
	qboolean teamArena = qfalse;
	int		style = UI_CENTER | UI_DROPSHADOW;

#ifdef STANDALONE
	style = UI_RIGHT | UI_DROPSHADOW;
	trap_Cvar_Set( "sv_killserver", "0" );
	memset( &s_main, 0, sizeof( s_main ) );
	memset( &s_errorMessage, 0, sizeof( s_errorMessage ) );
	MainMenu_Cache();

	// A multiplayer session that ended (host left, timeout, Escape while connecting)
	// leaves the client with no world. Bring back the live menu map once.
	if ( trap_Cvar_VariableValue( "ui_lanClient" ) && !trap_Cvar_VariableValue( "sv_running" ) ) {
		uiClientState_t cstate;

		trap_GetClientState( &cstate );
		if ( cstate.connState == CA_DISCONNECTED ) {
			trap_Cvar_Set( "ui_lanClient", "0" );
			trap_Cmd_ExecuteText( EXEC_APPEND, "menu_background\n" );
		}
	}

	trap_Cvar_VariableStringBuffer( "com_errorMessage", s_errorMessage.errorMessage,
		sizeof( s_errorMessage.errorMessage ) );
	if ( strlen( s_errorMessage.errorMessage ) ) {
		s_errorMessage.menu.draw = Main_MenuDraw;
		s_errorMessage.menu.key = ErrorMessage_Key;
		s_errorMessage.menu.fullscreen = qfalse;
		s_errorMessage.menu.wrapAround = qtrue;
		s_errorMessage.menu.showlogo = qfalse;

		trap_Key_SetCatcher( KEYCATCH_UI );
		uis.menusp = 0;
		UI_PushMenu( &s_errorMessage.menu );
		return;
	}

	s_main.menu.draw = Main_MenuDraw;
	s_main.menu.key = Main_MenuKey;
	s_main.menu.fullscreen = qfalse;
	s_main.menu.wrapAround = qtrue;
	s_main.menu.showlogo = qfalse;

	{
		float x = FabMenu_X();

		FabUI_InitButton( &s_main.singleplayer, x, 212, 230, 32, "SINGLE PLAYER",
			ID_SINGLEPLAYER, Main_MenuEvent, FAB_BUTTON_PRIMARY | FAB_BUTTON_LEFT );
		FabUI_InitButton( &s_main.multiplayer, x, 252, 230, 30, "MULTIPLAYER",
			ID_MULTIPLAYER, Main_MenuEvent, FAB_BUTTON_LEFT );
		FabUI_InitButton( &s_main.setup, x, 312, 230, 30, "SETTINGS",
			ID_SETUP, Main_MenuEvent, FAB_BUTTON_LEFT );
		/* Keep the same 8-unit gap as the play buttons so these rows don't touch. */
		FabUI_InitButton( &s_main.account, x, 350, 230, 30, "ACCOUNT",
			ID_ACCOUNT, Main_MenuEvent, FAB_BUTTON_LEFT );
		FabUI_InitButton( &s_main.exit, x, 388, 230, 30, "EXIT",
			ID_EXIT, Main_MenuEvent, FAB_BUTTON_LEFT | FAB_BUTTON_DANGER );
		(void)style;
	}
	{
		// bottom right, just above the version number
		const char *label = "JOIN OUR DISCORD";
		float w = FAB_TextWidth( label, 7, 7 * 0.18f ) + 8;

		FabUI_InitButton( &s_main.discord, FabUI_Right() - 44 - w, SCREEN_HEIGHT - 44, w, 16,
			label, ID_DISCORD, Main_MenuEvent, 0 );
		s_main.discord.generic.ownerdraw = Main_DrawLink;
	}

	Menu_AddItem( &s_main.menu, &s_main.singleplayer );
	Menu_AddItem( &s_main.menu, &s_main.multiplayer );
	Menu_AddItem( &s_main.menu, &s_main.setup );
    Menu_AddItem( &s_main.menu, &s_main.account );
    Menu_AddItem( &s_main.menu, &s_main.exit );
	Menu_AddItem( &s_main.menu, &s_main.discord );

	trap_Key_SetCatcher( KEYCATCH_UI );
	uis.menusp = 0;
	UI_PushMenu( &s_main.menu );
	return;
#endif

#ifndef STANDALONE
	trap_Cvar_Set( "sv_killserver", "1" );
#endif

	if( !uis.demoversion && !ui_cdkeychecked.integer ) {
		char	key[17];

		trap_GetCDKey( key, sizeof(key) );
		if( trap_VerifyCDKey( key, NULL ) == qfalse ) {
			UI_CDKeyMenu();
			return;
		}
	}
	
	memset( &s_main, 0 ,sizeof(mainmenu_t) );
	memset( &s_errorMessage, 0 ,sizeof(errorMessage_t) );

	// com_errorMessage would need that too
	MainMenu_Cache();
	
	trap_Cvar_VariableStringBuffer( "com_errorMessage", s_errorMessage.errorMessage, sizeof(s_errorMessage.errorMessage) );
	if (strlen(s_errorMessage.errorMessage))
	{	
		s_errorMessage.menu.draw = Main_MenuDraw;
		s_errorMessage.menu.key = ErrorMessage_Key;
		s_errorMessage.menu.fullscreen = qtrue;
		s_errorMessage.menu.wrapAround = qtrue;
		s_errorMessage.menu.showlogo = qtrue;		

		trap_Key_SetCatcher( KEYCATCH_UI );
		uis.menusp = 0;
		UI_PushMenu ( &s_errorMessage.menu );
		
		return;
	}

	s_main.menu.draw = Main_MenuDraw;
	s_main.menu.fullscreen = qtrue;
	s_main.menu.wrapAround = qtrue;
	s_main.menu.showlogo = qtrue;

	y = 134;
	s_main.singleplayer.generic.type		= MTYPE_PTEXT;
	s_main.singleplayer.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.singleplayer.generic.x			= 320;
	s_main.singleplayer.generic.y			= y;
	s_main.singleplayer.generic.id			= ID_SINGLEPLAYER;
	s_main.singleplayer.generic.callback	= Main_MenuEvent; 
	s_main.singleplayer.string				= "SINGLE PLAYER";
	s_main.singleplayer.color				= color_red;
	s_main.singleplayer.style				= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.multiplayer.generic.type			= MTYPE_PTEXT;
	s_main.multiplayer.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.multiplayer.generic.x			= 320;
	s_main.multiplayer.generic.y			= y;
	s_main.multiplayer.generic.id			= ID_MULTIPLAYER;
	s_main.multiplayer.generic.callback		= Main_MenuEvent; 
	s_main.multiplayer.string				= "MULTIPLAYER";
	s_main.multiplayer.color				= color_red;
	s_main.multiplayer.style				= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.setup.generic.type				= MTYPE_PTEXT;
	s_main.setup.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.setup.generic.x					= 320;
	s_main.setup.generic.y					= y;
	s_main.setup.generic.id					= ID_SETUP;
	s_main.setup.generic.callback			= Main_MenuEvent; 
	s_main.setup.string						= "SETUP";
	s_main.setup.color						= color_red;
	s_main.setup.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.demos.generic.type				= MTYPE_PTEXT;
	s_main.demos.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.demos.generic.x					= 320;
	s_main.demos.generic.y					= y;
	s_main.demos.generic.id					= ID_DEMOS;
	s_main.demos.generic.callback			= Main_MenuEvent; 
	s_main.demos.string						= "DEMOS";
	s_main.demos.color						= color_red;
	s_main.demos.style						= style;

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.cinematics.generic.type			= MTYPE_PTEXT;
	s_main.cinematics.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.cinematics.generic.x				= 320;
	s_main.cinematics.generic.y				= y;
	s_main.cinematics.generic.id			= ID_CINEMATICS;
	s_main.cinematics.generic.callback		= Main_MenuEvent; 
	s_main.cinematics.string				= "CINEMATICS";
	s_main.cinematics.color					= color_red;
	s_main.cinematics.style					= style;

	if ( !uis.demoversion && UI_TeamArenaExists() ) {
		teamArena = qtrue;
		y += MAIN_MENU_VERTICAL_SPACING;
		s_main.teamArena.generic.type			= MTYPE_PTEXT;
		s_main.teamArena.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
		s_main.teamArena.generic.x				= 320;
		s_main.teamArena.generic.y				= y;
		s_main.teamArena.generic.id				= ID_TEAMARENA;
		s_main.teamArena.generic.callback		= Main_MenuEvent; 
		s_main.teamArena.string					= "TEAM ARENA";
		s_main.teamArena.color					= color_red;
		s_main.teamArena.style					= style;
	}

	if ( !uis.demoversion ) {
		y += MAIN_MENU_VERTICAL_SPACING;
		s_main.mods.generic.type			= MTYPE_PTEXT;
		s_main.mods.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
		s_main.mods.generic.x				= 320;
		s_main.mods.generic.y				= y;
		s_main.mods.generic.id				= ID_MODS;
		s_main.mods.generic.callback		= Main_MenuEvent; 
		s_main.mods.string					= "MODS";
		s_main.mods.color					= color_red;
		s_main.mods.style					= style;
	}

	y += MAIN_MENU_VERTICAL_SPACING;
	s_main.exit.generic.type				= MTYPE_PTEXT;
	s_main.exit.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_main.exit.generic.x					= 320;
	s_main.exit.generic.y					= y;
	s_main.exit.generic.id					= ID_EXIT;
	s_main.exit.generic.callback			= Main_MenuEvent; 
	s_main.exit.string						= "EXIT";
	s_main.exit.color						= color_red;
	s_main.exit.style						= style;

	Menu_AddItem( &s_main.menu,	&s_main.singleplayer );
	Menu_AddItem( &s_main.menu,	&s_main.multiplayer );
	Menu_AddItem( &s_main.menu,	&s_main.setup );
	Menu_AddItem( &s_main.menu,	&s_main.demos );
	Menu_AddItem( &s_main.menu,	&s_main.cinematics );
	if (teamArena) {
		Menu_AddItem( &s_main.menu,	&s_main.teamArena );
	}
	if ( !uis.demoversion ) {
		Menu_AddItem( &s_main.menu,	&s_main.mods );
	}
	Menu_AddItem( &s_main.menu,	&s_main.exit );             

	trap_Key_SetCatcher( KEYCATCH_UI );
	uis.menusp = 0;
	UI_PushMenu ( &s_main.menu );
		
}
