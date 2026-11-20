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

INGAME MENU

=======================================================================
*/


#include "ui_local.h"


#define INGAME_FRAME					"menu/art/addbotframe"
//#define INGAME_FRAME					"menu/art/cut_frame"
#define INGAME_MENU_VERTICAL_SPACING	28

#define ID_TEAM					10
#define ID_ADDBOTS				11
#define ID_REMOVEBOTS			12
#define ID_SETUP				13
#define ID_SERVERINFO			14
#define ID_LEAVEARENA			15
#define ID_RESTART				16
#define ID_RESUME				18
#define ID_TEAMORDERS			19
#define ID_MAINMENU			23
#define ID_RESPAWN				24
#define ID_CHANGEMATCH			25
#define ID_VOICE				26


typedef struct {
	menuframework_s	menu;

	menubitmap_s	frame;
	menutext_s		team;
	menutext_s		setup;
	menutext_s		server;
	menutext_s		leave;
	menutext_s		restart;
	menutext_s		addbots;
	menutext_s		removebots;
	menutext_s		teamorders;
	menutext_s		mainmenu;
	menutext_s		resume;
	menutext_s		respawn;
	menutext_s		changematch;
	menutext_s		voice;
#ifdef STANDALONE
	char			title[64];
	char			subtitle[96];
	int				buttonCount;
#endif
} ingamemenu_t;

static ingamemenu_t	s_ingame;


/*
=================
InGame_RestartAction
=================
*/
static void InGame_RestartAction( qboolean result ) {
	if( !result ) {
		return;
	}

	UI_PopMenu();
#ifdef STANDALONE
	if ( UI_ActiveCampaignMission() ) {
		UI_StartCampaignMission( UI_ActiveCampaignMission() );
		return;
	}
	{
		char mapname[MAX_QPATH];

		// Replace bots atomically during server loading so a large population does
		// not flood the local client's reliable command queue with disconnects.
		trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
		trap_Cmd_ExecuteText( EXEC_APPEND, va( "freshmap %s%s\n", mapname,
			trap_Cvar_VariableValue( "g_quickPlay" ) ? " quickplay" : "" ) );
	}
#else
	trap_Cmd_ExecuteText( EXEC_APPEND, "map_restart 0\n" );
#endif
}


/*
=================
InGame_MainMenuAction
=================
*/
void UI_ReturnToMainMenu( void ) {
	UI_ForceMenuOff();
	trap_Cvar_Set( "sv_lanHost", "0" );
	trap_Cvar_Set( "sv_onlineHost", "0" );
	trap_Cvar_Set( "ui_lanClient", "0" );
	trap_Cvar_Set( "g_menuBackground", "1" );
	// The background match is plain deathmatch; the PLAY screen remembers the
	// last pick in ui_matchMode.
	trap_Cvar_SetValue( "g_gametype", GT_FFA );
	trap_Cvar_Set( "g_killshift", "0" );
	trap_Cvar_Set( "g_leprechaun", "0" );
	trap_Cvar_Set( "g_armsRace", "0" );
	trap_Cvar_Set( "g_lastManStanding", "0" );
	trap_Cvar_Set( "g_infected", "0" );
	trap_Cvar_Set( "g_bugHunt", "0" );
	trap_Cvar_Set( "g_davidGoliath", "0" );
	trap_Cvar_Set( "g_spawnWeapons", "pistol rifle" );
	trap_Cvar_Set( "g_infiniteAmmo", "0" );
	trap_Cmd_ExecuteText( EXEC_APPEND, "menu_background\n" );
}

static void InGame_MainMenuAction( qboolean result ) {
	if ( result ) UI_ReturnToMainMenu();
}


/*
=================
InGame_Event
=================
*/
void InGame_Event( void *ptr, int notification ) {
	if( notification != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_TEAM:
		UI_TeamMainMenu();
		break;

	case ID_SETUP:
		UI_SetupMenu();
		break;

#if FAB_VOICE_CHAT
	case ID_VOICE:
		UI_VoiceMenu();
		break;

#endif
	case ID_LEAVEARENA:
		trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		break;

	case ID_RESTART:
	#ifdef STANDALONE
		UI_ConfirmMenu( "RESTART THIS MATCH?", 0, InGame_RestartAction );
	#else
		UI_ConfirmMenu( "RESTART ARENA?", 0, InGame_RestartAction );
	#endif
		break;

	case ID_MAINMENU:
		UI_ConfirmMenu( "RETURN TO MAIN MENU?", 0, InGame_MainMenuAction );
		break;

	case ID_SERVERINFO:
		UI_ServerInfoMenu();
		break;

	case ID_ADDBOTS:
		UI_AddBotsMenu();
		break;

	case ID_REMOVEBOTS:
		UI_RemoveBotsMenu();
		break;

	case ID_TEAMORDERS:
		UI_TeamOrdersMenu();
		break;

	case ID_RESUME:
		UI_PopMenu();
		break;

#ifdef STANDALONE
	case ID_CHANGEMATCH:
		UI_ChangeMatchMenu();
		break;
#endif

	case ID_RESPAWN:
		UI_ForceMenuOff();
		trap_Cmd_ExecuteText( EXEC_APPEND, "respawn\n" );
		break;


	}
}


#ifdef STANDALONE
/*
=======================================================================

PAUSE MENU DRAWING

The menu framework still handles focus, keys and callbacks; every item is
owner-drawn in the same style as the HUD (cgame/fab_draw.c). Layout is in
the menu's 640x480 space, which q3_ui keeps 4:3 and centred on wide screens.

=======================================================================
*/

#define PAUSE_X			180
#define PAUSE_W			280
#define PAUSE_TOP		68
#define PAUSE_BUTTON_Y	172
#define PAUSE_BUTTON_H	26
#define PAUSE_BUTTON_GAP 5

static void InGame_DrawButton( void *self ) {
	menutext_s *t = (menutext_s *)self;
	menucommon_s *g = &t->generic;
	int flags = 0;

	if ( g->parent->cursor == g->menuPosition ) {
		flags |= FAB_BUTTON_FOCUS;
	}
	if ( g->flags & QMF_GRAYED ) {
		flags |= FAB_BUTTON_DISABLED;
	}
	if ( t == &s_ingame.resume ) {
		flags |= FAB_BUTTON_PRIMARY;
	}
	FAB_Button( g->left, g->top, g->right - g->left, g->bottom - g->top, t->string, flags );
}

static void InGame_MenuDraw( void ) {
	float left = -uis.bias / uis.xscale;
	float full = uis.glconfig.vidWidth / uis.xscale;
	float bottom = PAUSE_BUTTON_Y + s_ingame.buttonCount * ( PAUSE_BUTTON_H + PAUSE_BUTTON_GAP ) + 34;
	vec4_t black = { 0, 0, 0, 1 };

	FabUI_Begin();

	// dim the game behind the menu, darker toward the panel
	FAB_Fill( left, 0, full, SCREEN_HEIGHT, black, 0.45f );
	FAB_Fade( PAUSE_X + PAUSE_W, 0, 260, SCREEN_HEIGHT, qfalse, black, 0.35f );
	FAB_Fade( PAUSE_X - 260, 0, 260, SCREEN_HEIGHT, qtrue, black, 0.35f );
	FAB_Fill( PAUSE_X, 0, PAUSE_W, SCREEN_HEIGHT, black, 0.35f );

	FAB_Panel( PAUSE_X - 14, PAUSE_TOP, PAUSE_W + 28, bottom - PAUSE_TOP, 14,
		FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.95f );
	FAB_Fill( PAUSE_X, PAUSE_TOP, PAUSE_W + 14, 2, fabCyan, 1 );

	FAB_Label( PAUSE_X, PAUSE_TOP + 26, 8, FAB_ALIGN_LEFT, "PAUSED", fabCyan, 1 );
	FAB_Text( PAUSE_X, PAUSE_TOP + 54, 24, FAB_ALIGN_LEFT, s_ingame.title, fabWhite, 1 );
	FAB_Label( PAUSE_X, PAUSE_TOP + 70, 7, FAB_ALIGN_LEFT, s_ingame.subtitle, fabDim, 1 );

	Menu_Draw( &s_ingame.menu );

	if ( trap_Cvar_VariableValue( "sv_onlineHost" ) ) {
		char status[32], address[64];
		trap_Cvar_VariableStringBuffer( "sv_onlineStatus", status, sizeof( status ) );
		trap_Cvar_VariableStringBuffer( "sv_onlineAddress", address, sizeof( address ) );
		FAB_Label( PAUSE_X, bottom - 14, 6.5f, FAB_ALIGN_LEFT,
			!Q_stricmp( status, "listed" ) ? va( "PUBLIC MATCH  %s", address ) :
			!Q_stricmp( status, "unreachable" ) ? "NOT LISTED / CHECK UDP PORT AND DIRECTORY" : "CHECKING PUBLIC DIRECTORY",
			fabDim, 0.9f );
	} else {
		FAB_Label( PAUSE_X, bottom - 14, 6.5f, FAB_ALIGN_LEFT, "ESC  RESUME", fabDim, 0.7f );
	}
}

static sfxHandle_t InGame_MenuKey( int key ) {
	return FabUI_Key( &s_ingame.menu, key );
}

static void InGame_SetupButton( menutext_s *t, int row, const char *label, int id ) {
	t->generic.type = MTYPE_PTEXT;
	t->generic.flags = QMF_NODEFAULTINIT;
	t->generic.ownerdraw = InGame_DrawButton;
	t->generic.id = id;
	t->generic.callback = InGame_Event;
	t->generic.x = PAUSE_X;
	t->generic.y = PAUSE_BUTTON_Y + row * ( PAUSE_BUTTON_H + PAUSE_BUTTON_GAP );
	t->generic.left = PAUSE_X;
	t->generic.right = PAUSE_X + PAUSE_W;
	t->generic.top = t->generic.y;
	t->generic.bottom = t->generic.y + PAUSE_BUTTON_H;
	t->string = (char *)label;
}

static void InGame_BuildSubtitle( void ) {
	char info[MAX_INFO_STRING];
	const char *mode;
	int i, players = 0, maxClients;

	maxClients = (int)trap_Cvar_VariableValue( "sv_maxclients" );
	if ( maxClients <= 0 || maxClients > MAX_CLIENTS ) {
		maxClients = MAX_CLIENTS;
	}
	for ( i = 0; i < maxClients; i++ ) {
		trap_GetConfigString( CS_PLAYERS + i, info, sizeof( info ) );
		if ( info[0] && atoi( Info_ValueForKey( info, "t" ) ) != TEAM_SPECTATOR ) {
			players++;
		}
	}
	trap_GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	mode = FAB_ModeName( atoi( Info_ValueForKey( info, "g_gametype" ) ), info );
	Com_sprintf( s_ingame.subtitle, sizeof( s_ingame.subtitle ), "%s   %i PLAYERS%s", mode, players,
		trap_Cvar_VariableValue( "sv_running" ) ? "" : "   ONLINE" );
}
#endif

/*
=================
InGame_MenuInit
=================
*/
void InGame_MenuInit( void ) {
	int		y;
	uiClientState_t	cs;
	char	info[MAX_INFO_STRING];
	char	mapname[MAX_QPATH];
	int		team;
#ifdef STANDALONE
	int buttonRow = 0;
	qboolean localMatch = trap_Cvar_VariableValue( "sv_running" ) != 0;
	qboolean campaign = UI_ActiveCampaignMission() != 0;
	qboolean canRestart = localMatch && !trap_Cvar_VariableValue( "sv_onlineHost" );
	qboolean canChangeMatch = canRestart && !campaign;
#endif

	memset( &s_ingame, 0 ,sizeof(ingamemenu_t) );

	InGame_Cache();

	s_ingame.menu.wrapAround = qtrue;
	s_ingame.menu.fullscreen = qfalse;

#ifdef STANDALONE
	s_ingame.menu.draw = InGame_MenuDraw;
	s_ingame.menu.key = InGame_MenuKey;

	trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	FAB_MapTitle( mapname, s_ingame.title, sizeof( s_ingame.title ) );

	// Map and match type change together on the shared match screen.
	InGame_SetupButton( &s_ingame.resume, buttonRow++, "RESUME", ID_RESUME );
	if ( !campaign ) InGame_SetupButton( &s_ingame.respawn, buttonRow++, "RESPAWN", ID_RESPAWN );
#if FAB_VOICE_CHAT
	InGame_SetupButton( &s_ingame.voice, buttonRow++, "VOICE SETTINGS", ID_VOICE );
#endif
	if ( canChangeMatch ) {
		InGame_SetupButton( &s_ingame.changematch, buttonRow++, "CHANGE MAP / MODE", ID_CHANGEMATCH );
	}
	if ( canRestart ) {
		InGame_SetupButton( &s_ingame.restart, buttonRow++, "RESTART MATCH", ID_RESTART );
	}
	InGame_SetupButton( &s_ingame.mainmenu, buttonRow++, "MAIN MENU", ID_MAINMENU );
	s_ingame.buttonCount = buttonRow;

	// Players who joined someone else's match can't change the host's settings.
	if ( !localMatch ) {
		s_ingame.mainmenu.string = "LEAVE MATCH";
	}
	InGame_BuildSubtitle();

	Menu_AddItem( &s_ingame.menu, &s_ingame.resume );
	if ( !campaign ) Menu_AddItem( &s_ingame.menu, &s_ingame.respawn );
#if FAB_VOICE_CHAT
	Menu_AddItem( &s_ingame.menu, &s_ingame.voice );
#endif
	if ( canChangeMatch ) {
		Menu_AddItem( &s_ingame.menu, &s_ingame.changematch );
	}
	if ( canRestart ) {
		Menu_AddItem( &s_ingame.menu, &s_ingame.restart );
	}
	Menu_AddItem( &s_ingame.menu, &s_ingame.mainmenu );
	return;
#endif

	s_ingame.frame.generic.type			= MTYPE_BITMAP;
	s_ingame.frame.generic.flags		= QMF_INACTIVE;
	s_ingame.frame.generic.name			= INGAME_FRAME;
	s_ingame.frame.generic.x			= 320-233;//142;
	s_ingame.frame.generic.y			= 240-166;//118;
	s_ingame.frame.width				= 466;//359;
	s_ingame.frame.height				= 332;//256;

	//y = 96;
	y = 88;
	s_ingame.team.generic.type			= MTYPE_PTEXT;
	s_ingame.team.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.team.generic.x				= 320;
	s_ingame.team.generic.y				= y;
	s_ingame.team.generic.id			= ID_TEAM;
	s_ingame.team.generic.callback		= InGame_Event; 
	s_ingame.team.string				= "START";
	s_ingame.team.color					= color_red;
	s_ingame.team.style					= UI_CENTER|UI_SMALLFONT;

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.addbots.generic.type		= MTYPE_PTEXT;
	s_ingame.addbots.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.addbots.generic.x			= 320;
	s_ingame.addbots.generic.y			= y;
	s_ingame.addbots.generic.id			= ID_ADDBOTS;
	s_ingame.addbots.generic.callback	= InGame_Event; 
	s_ingame.addbots.string				= "ADD BOTS";
	s_ingame.addbots.color				= color_red;
	s_ingame.addbots.style				= UI_CENTER|UI_SMALLFONT;
	if( !trap_Cvar_VariableValue( "sv_running" ) || !trap_Cvar_VariableValue( "bot_enable" ) || (trap_Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER)) {
		s_ingame.addbots.generic.flags |= QMF_GRAYED;
	}

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.removebots.generic.type		= MTYPE_PTEXT;
	s_ingame.removebots.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.removebots.generic.x			= 320;
	s_ingame.removebots.generic.y			= y;
	s_ingame.removebots.generic.id			= ID_REMOVEBOTS;
	s_ingame.removebots.generic.callback	= InGame_Event; 
	s_ingame.removebots.string				= "REMOVE BOTS";
	s_ingame.removebots.color				= color_red;
	s_ingame.removebots.style				= UI_CENTER|UI_SMALLFONT;
	if( !trap_Cvar_VariableValue( "sv_running" ) || !trap_Cvar_VariableValue( "bot_enable" ) || (trap_Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER)) {
		s_ingame.removebots.generic.flags |= QMF_GRAYED;
	}

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.teamorders.generic.type		= MTYPE_PTEXT;
	s_ingame.teamorders.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.teamorders.generic.x			= 320;
	s_ingame.teamorders.generic.y			= y;
	s_ingame.teamorders.generic.id			= ID_TEAMORDERS;
	s_ingame.teamorders.generic.callback	= InGame_Event; 
	s_ingame.teamorders.string				= "TEAM ORDERS";
	s_ingame.teamorders.color				= color_red;
	s_ingame.teamorders.style				= UI_CENTER|UI_SMALLFONT;
	if( !(trap_Cvar_VariableValue( "g_gametype" ) >= GT_TEAM) ) {
		s_ingame.teamorders.generic.flags |= QMF_GRAYED;
	}
	else {
		trap_GetClientState( &cs );
		trap_GetConfigString( CS_PLAYERS + cs.clientNum, info, MAX_INFO_STRING );
		team = atoi( Info_ValueForKey( info, "t" ) );
		if( team == TEAM_SPECTATOR ) {
			s_ingame.teamorders.generic.flags |= QMF_GRAYED;
		}
	}

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.setup.generic.type			= MTYPE_PTEXT;
	s_ingame.setup.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.setup.generic.x			= 320;
	s_ingame.setup.generic.y			= y;
	s_ingame.setup.generic.id			= ID_SETUP;
	s_ingame.setup.generic.callback		= InGame_Event; 
	s_ingame.setup.string				= "SETUP";
	s_ingame.setup.color				= color_red;
	s_ingame.setup.style				= UI_CENTER|UI_SMALLFONT;

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.server.generic.type		= MTYPE_PTEXT;
	s_ingame.server.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.server.generic.x			= 320;
	s_ingame.server.generic.y			= y;
	s_ingame.server.generic.id			= ID_SERVERINFO;
	s_ingame.server.generic.callback	= InGame_Event; 
	s_ingame.server.string				= "SERVER INFO";
	s_ingame.server.color				= color_red;
	s_ingame.server.style				= UI_CENTER|UI_SMALLFONT;

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.restart.generic.type		= MTYPE_PTEXT;
	s_ingame.restart.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.restart.generic.x			= 320;
	s_ingame.restart.generic.y			= y;
	s_ingame.restart.generic.id			= ID_RESTART;
	s_ingame.restart.generic.callback	= InGame_Event; 
	s_ingame.restart.string				= "RESTART ARENA";
	s_ingame.restart.color				= color_red;
	s_ingame.restart.style				= UI_CENTER|UI_SMALLFONT;
	if( !trap_Cvar_VariableValue( "sv_running" ) ) {
		s_ingame.restart.generic.flags |= QMF_GRAYED;
	}

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.resume.generic.type			= MTYPE_PTEXT;
	s_ingame.resume.generic.flags			= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.resume.generic.x				= 320;
	s_ingame.resume.generic.y				= y;
	s_ingame.resume.generic.id				= ID_RESUME;
	s_ingame.resume.generic.callback		= InGame_Event; 
	s_ingame.resume.string					= "RESUME GAME";
	s_ingame.resume.color					= color_red;
	s_ingame.resume.style					= UI_CENTER|UI_SMALLFONT;

	y += INGAME_MENU_VERTICAL_SPACING;
	s_ingame.leave.generic.type			= MTYPE_PTEXT;
	s_ingame.leave.generic.flags		= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_ingame.leave.generic.x			= 320;
	s_ingame.leave.generic.y			= y;
	s_ingame.leave.generic.id			= ID_LEAVEARENA;
	s_ingame.leave.generic.callback		= InGame_Event; 
	s_ingame.leave.string				= "LEAVE ARENA";
	s_ingame.leave.color				= color_red;
	s_ingame.leave.style				= UI_CENTER|UI_SMALLFONT;

	Menu_AddItem( &s_ingame.menu, &s_ingame.frame );
	Menu_AddItem( &s_ingame.menu, &s_ingame.team );
	Menu_AddItem( &s_ingame.menu, &s_ingame.addbots );
	Menu_AddItem( &s_ingame.menu, &s_ingame.removebots );
	Menu_AddItem( &s_ingame.menu, &s_ingame.teamorders );
	Menu_AddItem( &s_ingame.menu, &s_ingame.setup );
	Menu_AddItem( &s_ingame.menu, &s_ingame.server );
	Menu_AddItem( &s_ingame.menu, &s_ingame.restart );
	Menu_AddItem( &s_ingame.menu, &s_ingame.resume );
	Menu_AddItem( &s_ingame.menu, &s_ingame.leave );
}


/*
=================
InGame_Cache
=================
*/
void InGame_Cache( void ) {
	trap_R_RegisterShaderNoMip( INGAME_FRAME );
}


/*
=================
UI_InGameMenu
=================
*/
void UI_InGameMenu( void ) {
	// force as top level menu
	uis.menusp = 0;  

	// set menu cursor to a nice location
#ifdef STANDALONE
	uis.cursorx = 320;
	uis.cursory = PAUSE_BUTTON_Y + PAUSE_BUTTON_H / 2;
#else
	uis.cursorx = 245;
	uis.cursory = 216;
#endif

	InGame_MenuInit();
	UI_PushMenu( &s_ingame.menu );
#ifdef STANDALONE
	// UI_PushMenu resets the cursor to the first item
	Menu_SetCursorToItem( &s_ingame.menu, &s_ingame.resume );
#endif
}
