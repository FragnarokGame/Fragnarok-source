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

CREDITS

Settings > Credits. Lists the engine, libraries and assets the game ships
with and their licenses. Keep it in step with
assets/source/licenses/THIRD-PARTY-NOTICES.txt and tools/copy-licenses.py,
which put the full license texts in the licenses folder next to the game.

=======================================================================
*/


#include "ui_local.h"

#define ID_CREDITS_BACK		10

#define CREDITS_X			74
#define CREDITS_COL2_X		340
#define CREDITS_TOP			150

typedef struct {
	const char *heading;	// a section starts when this is set
	const char *name;
	const char *detail;
} creditLine_t;

// Left column: the engine and assets.
static const creditLine_t creditsAssets[] = {
	{ "ENGINE", "ioquake3", "Quake III Arena source code by id Software. GPL v2" },
	{ "CHARACTERS AND BOTS", "OpenArena 0.8.8", "Sarge, GRiSM and Skelebot models, voices and bots. GPL v2" },
	{ "TEXTURES", "High Seas Textures", "by Bradley D. (strideh.itch.io), on Gallows Point" },
	{ "FONT", "Oxanium", "by Severin Meyer. SIL Open Font License 1.1" },
	{ NULL, NULL, NULL }
};

// Right column: libraries compiled into or shipped with the game.
static const creditLine_t creditsLibraries[] = {
	{ "LIBRARIES", "SDL2 2.32.8", "Sam Lantinga. zlib license" },
	{ NULL, "zlib 1.3.1", "Jean-loup Gailly and Mark Adler. zlib license" },
	{ NULL, "libjpeg 9f", "Independent JPEG Group license" },
	{ NULL, "libogg 1.3.6", "Xiph.Org Foundation. BSD license" },
	{ NULL, "Opus 1.5.2", "Xiph.Org, Skype and contributors. BSD license" },
	{ NULL, "opusfile 0.12", "Xiph.Org Foundation. BSD license" },
	{ NULL, "AI Toolkit", "David Delassus. MIT license" },
	{ NULL, NULL, NULL }
};

typedef struct {
	menuframework_s	menu;
	menutext_s		back;
} creditsmenu_t;

static creditsmenu_t	s_credits;


static void UI_CreditMenu_Event( void *ptr, int event ) {
	if ( event == QM_ACTIVATED && ((menucommon_s *)ptr)->id == ID_CREDITS_BACK ) {
		UI_PopMenu();
	}
}

static float UI_CreditMenu_Column( float x, float y, const creditLine_t *line ) {
	for ( ; line->name; line++ ) {
		if ( line->heading ) {
			if ( y > CREDITS_TOP ) {
				y += 10;
			}
			FAB_Label( x, y, 7, FAB_ALIGN_LEFT, line->heading, fabCyan, 1 );
			y += 15;
		}
		FAB_Text( x, y, 11, FAB_ALIGN_LEFT, line->name, fabWhite, 1 );
		y += 12;
		y = FabUI_Wrapped( x, y, 230, 7, 11, FAB_ALIGN_LEFT, line->detail, fabDim, 1 ) + 4;
	}
	return y;
}

static void UI_CreditMenu_Draw( void ) {
	FabUI_Begin();
	FabUI_Dim( 0.65f );
	FAB_Panel( CREDITS_X - 24, 48, 564, 396, 12, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.98f );
	FabUI_Header( CREDITS_X, 100, "FRAGNAROK", "CREDITS", "BUILT WITH" );

	UI_CreditMenu_Column( CREDITS_X, CREDITS_TOP, creditsAssets );
	UI_CreditMenu_Column( CREDITS_COL2_X, CREDITS_TOP, creditsLibraries );

	// The IJG license asks binary releases to carry this sentence.
	FabUI_Wrapped( CREDITS_X, 372, 500, 7, 11, FAB_ALIGN_LEFT,
		"This software is based in part on the work of the Independent JPEG Group. "
		"Full license texts are in the licenses folder next to the game.", fabDim, 1 );

	Menu_Draw( &s_credits.menu );
}

static sfxHandle_t UI_CreditMenu_Key( int key ) {
	return FabUI_Key( &s_credits.menu, key );
}

// For "fab_menu status".
qboolean UI_CreditMenuActive( void ) {
	return uis.activemenu == &s_credits.menu;
}

/*
===============
UI_CreditMenu
===============
*/
void UI_CreditMenu( void ) {
	memset( &s_credits, 0, sizeof( s_credits ) );
	s_credits.menu.draw = UI_CreditMenu_Draw;
	s_credits.menu.key = UI_CreditMenu_Key;
	s_credits.menu.fullscreen = qfalse;
	s_credits.menu.wrapAround = qtrue;

	FabUI_InitButton( &s_credits.back, CREDITS_X, 402, 160, 28, "BACK",
		ID_CREDITS_BACK, UI_CreditMenu_Event, FAB_BUTTON_PRIMARY );
	Menu_AddItem( &s_credits.menu, &s_credits.back );

	UI_PushMenu( &s_credits.menu );
}
