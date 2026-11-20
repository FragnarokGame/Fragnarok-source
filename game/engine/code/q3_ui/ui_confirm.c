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

CONFIRMATION MENU

=======================================================================
*/


#include "ui_local.h"


#define ART_CONFIRM_FRAME	"menu/art/cut_frame"

#define ID_CONFIRM_NO		10
#define ID_CONFIRM_YES		11


typedef struct {
	menuframework_s menu;

	menutext_s		no;
	menutext_s		yes;

	int				slashX;
	const char *	question;
	void			(*draw)( void );
	void			(*action)( qboolean result );
	
	int style;
	const char **lines;
} confirmMenu_t;


static confirmMenu_t	s_confirm;


/*
=================
ConfirmMenu_Event
=================
*/
static void ConfirmMenu_Event( void* ptr, int event ) {
	qboolean	result;

	if( event != QM_ACTIVATED ) {
		return;
	}

	UI_PopMenu();

	if( ((menucommon_s*)ptr)->id == ID_CONFIRM_NO ) {
		result = qfalse;
	}
	else {
		result = qtrue;
	}

	if( s_confirm.action ) {
		s_confirm.action( result );
	}
}


/*
=================
ConfirmMenu_Key
=================
*/
static sfxHandle_t ConfirmMenu_Key( int key ) {
	switch ( key ) {
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
		key = K_TAB;
		break;

	case 'n':
	case 'N':
		ConfirmMenu_Event( &s_confirm.no, QM_ACTIVATED );
		break;

	case 'y':
	case 'Y':
		ConfirmMenu_Event( &s_confirm.yes, QM_ACTIVATED );
		break;
	}

	return Menu_DefaultKey( &s_confirm.menu, key );
}


#ifdef STANDALONE
/*
=================
Confirm dialog in the HUD style (cgame/fab_draw.c)
=================
*/
#define CONFIRM_X		190
#define CONFIRM_W		260
#define CONFIRM_Y		178
#define CONFIRM_BUTTON_Y	256
#define CONFIRM_BUTTON_H	26

static void ConfirmMenu_DrawButton( void *self ) {
	menutext_s *t = (menutext_s *)self;
	menucommon_s *g = &t->generic;
	int flags = 0;

	if ( g->parent->cursor == g->menuPosition ) {
		flags |= FAB_BUTTON_FOCUS;
		if ( t == &s_confirm.yes && s_confirm.no.generic.type ) {
			flags |= FAB_BUTTON_PRIMARY;
		}
	}
	FAB_Button( g->left, g->top, g->right - g->left, g->bottom - g->top, t->string, flags );
}

static void ConfirmMenu_PlaceButton( menutext_s *t, float x, float w ) {
	t->generic.type = MTYPE_PTEXT;
	t->generic.flags = QMF_NODEFAULTINIT;
	t->generic.ownerdraw = ConfirmMenu_DrawButton;
	t->generic.x = x;
	t->generic.y = CONFIRM_BUTTON_Y;
	t->generic.left = x;
	t->generic.right = x + w;
	t->generic.top = CONFIRM_BUTTON_Y;
	t->generic.bottom = CONFIRM_BUTTON_Y + CONFIRM_BUTTON_H;
}

static void ConfirmMenu_DrawFrame( float height ) {
	vec4_t black = { 0, 0, 0, 1 };

	FabUI_Begin();
	FAB_Fill( -uis.bias / uis.xscale, 0, uis.glconfig.vidWidth / uis.xscale, SCREEN_HEIGHT,
		black, 0.45f );
	FAB_Panel( CONFIRM_X, CONFIRM_Y, CONFIRM_W, height, 12, FAB_CUT_TL | FAB_CUT_BR, fabPanel, 0.97f );
	FAB_Fill( CONFIRM_X + 12, CONFIRM_Y, CONFIRM_W - 12, 2, fabCyan, 1 );
}
#endif

/*
=================
MessaheMenu_Draw
=================
*/
static void MessageMenu_Draw( void ) {
	int i,y;

#ifdef STANDALONE
	ConfirmMenu_DrawFrame( CONFIRM_BUTTON_Y + CONFIRM_BUTTON_H + 16 - CONFIRM_Y );
	y = CONFIRM_Y + 30;
	for ( i = 0; s_confirm.lines[i]; i++ ) {
		FAB_Text( 320, y, 10, FAB_ALIGN_CENTER, s_confirm.lines[i], fabWhite, 1 );
		y += 15;
	}
	Menu_Draw( &s_confirm.menu );
	if ( s_confirm.draw ) {
		s_confirm.draw();
	}
	return;
#endif
	UI_DrawNamedPic( 142, 118, 359, 256, ART_CONFIRM_FRAME );
	
	y = 188;
	for(i=0; s_confirm.lines[i]; i++)
	{
		UI_DrawProportionalString( 320, y, s_confirm.lines[i], s_confirm.style, color_red );
		y += 18;
	}

	Menu_Draw( &s_confirm.menu );

	if( s_confirm.draw ) {
		s_confirm.draw();
	}
}

/*
=================
ConfirmMenu_Draw
=================
*/
static void ConfirmMenu_Draw( void ) {
#ifdef STANDALONE
	ConfirmMenu_DrawFrame( CONFIRM_BUTTON_Y + CONFIRM_BUTTON_H + 16 - CONFIRM_Y );
	FAB_Label( CONFIRM_X + 18, CONFIRM_Y + 26, 7, FAB_ALIGN_LEFT, "CONFIRM", fabCyan, 1 );
	FAB_Text( 320, CONFIRM_Y + 54, 14, FAB_ALIGN_CENTER, s_confirm.question, fabWhite, 1 );
	Menu_Draw( &s_confirm.menu );
	if ( s_confirm.draw ) {
		s_confirm.draw();
	}
	return;
#endif
	UI_DrawNamedPic( 142, 118, 359, 256, ART_CONFIRM_FRAME );
	UI_DrawProportionalString( 320, 204, s_confirm.question, s_confirm.style, color_red );
	UI_DrawProportionalString( s_confirm.slashX, 265, "/", UI_LEFT|UI_INVERSE, color_red );

	Menu_Draw( &s_confirm.menu );

	if( s_confirm.draw ) {
		s_confirm.draw();
	}
}


/*
=================
ConfirmMenu_Cache
=================
*/
void ConfirmMenu_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_CONFIRM_FRAME );
}


/*
=================
UI_ConfirmMenu_Stlye
=================
*/
void UI_ConfirmMenu_Style( const char *question, int style, void (*draw)( void ), void (*action)( qboolean result ) ) {
	uiClientState_t	cstate;
	int	n1, n2, n3;
	int	l1, l2, l3;

	// zero set all our globals
	memset( &s_confirm, 0, sizeof(s_confirm) );

	ConfirmMenu_Cache();

	n1 = UI_ProportionalStringWidth( "YES/NO" );
	n2 = UI_ProportionalStringWidth( "YES" ) + PROP_GAP_WIDTH;
	n3 = UI_ProportionalStringWidth( "/" )  + PROP_GAP_WIDTH;
	l1 = 320 - ( n1 / 2 );
	l2 = l1 + n2;
	l3 = l2 + n3;
	s_confirm.slashX = l2;

	s_confirm.question = question;
	s_confirm.draw = draw;
	s_confirm.action = action;
	s_confirm.style = style;

	s_confirm.menu.draw       = ConfirmMenu_Draw;
	s_confirm.menu.key        = ConfirmMenu_Key;
	s_confirm.menu.wrapAround = qtrue;

	trap_GetClientState( &cstate );
	if ( cstate.connState >= CA_CONNECTED ) {
		s_confirm.menu.fullscreen = qfalse;
	}
	else {
		s_confirm.menu.fullscreen = qtrue;
	}

	s_confirm.yes.generic.type		= MTYPE_PTEXT;      
	s_confirm.yes.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS; 
	s_confirm.yes.generic.callback	= ConfirmMenu_Event;
	s_confirm.yes.generic.id		= ID_CONFIRM_YES;
	s_confirm.yes.generic.x			= l1;
	s_confirm.yes.generic.y			= 264;
	s_confirm.yes.string			= "YES";
	s_confirm.yes.color				= color_red;
	s_confirm.yes.style				= UI_LEFT;

	s_confirm.no.generic.type		= MTYPE_PTEXT;      
	s_confirm.no.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS; 
	s_confirm.no.generic.callback	= ConfirmMenu_Event;
	s_confirm.no.generic.id			= ID_CONFIRM_NO;
	s_confirm.no.generic.x		    = l3;
	s_confirm.no.generic.y		    = 264;
	s_confirm.no.string				= "NO";
	s_confirm.no.color			    = color_red;
	s_confirm.no.style			    = UI_LEFT;

#ifdef STANDALONE
	ConfirmMenu_PlaceButton( &s_confirm.yes, CONFIRM_X + 18, ( CONFIRM_W - 46 ) / 2 );
	ConfirmMenu_PlaceButton( &s_confirm.no, CONFIRM_X + 28 + ( CONFIRM_W - 46 ) / 2,
		( CONFIRM_W - 46 ) / 2 );
	uis.cursorx = s_confirm.no.generic.left + 40;
	uis.cursory = CONFIRM_BUTTON_Y + CONFIRM_BUTTON_H / 2;
#endif
	Menu_AddItem( &s_confirm.menu,	&s_confirm.yes );             
	Menu_AddItem( &s_confirm.menu,	&s_confirm.no );

	UI_PushMenu( &s_confirm.menu );

	Menu_SetCursorToItem( &s_confirm.menu, &s_confirm.no );
}

/*
=================
UI_ConfirmMenu
=================
*/
void UI_ConfirmMenu( const char *question, void (*draw)( void ), void (*action)( qboolean result ) ) {
	UI_ConfirmMenu_Style(question, UI_CENTER|UI_INVERSE, draw, action);
}

/*
=================
UI_Message
hacked over from Confirm stuff
=================
*/
void UI_Message( const char **lines ) {
	uiClientState_t	cstate;
	int n1, l1;
	
	// zero set all our globals
	memset( &s_confirm, 0, sizeof(s_confirm) );

	ConfirmMenu_Cache();

	n1 = UI_ProportionalStringWidth( "OK" );
	l1 = 320 - ( n1 / 2 );
	
	s_confirm.lines = lines;
	s_confirm.style = UI_CENTER|UI_INVERSE|UI_SMALLFONT;

	s_confirm.menu.draw       = MessageMenu_Draw;
	s_confirm.menu.key        = ConfirmMenu_Key;
	s_confirm.menu.wrapAround = qtrue;
	
	trap_GetClientState( &cstate );
	if ( cstate.connState >= CA_CONNECTED ) {
		s_confirm.menu.fullscreen = qfalse;
	}
	else {
		s_confirm.menu.fullscreen = qtrue;
	}

	s_confirm.yes.generic.type		= MTYPE_PTEXT;      
	s_confirm.yes.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS; 
	s_confirm.yes.generic.callback	= ConfirmMenu_Event;
	s_confirm.yes.generic.id		= ID_CONFIRM_YES;
	s_confirm.yes.generic.x			= l1;
	s_confirm.yes.generic.y			= 280;
	s_confirm.yes.string			= "OK";
	s_confirm.yes.color				= color_red;
	s_confirm.yes.style				= UI_LEFT;
#ifdef STANDALONE
	ConfirmMenu_PlaceButton( &s_confirm.yes, 320 - 60, 120 );
#endif

	Menu_AddItem( &s_confirm.menu,	&s_confirm.yes );
	
	UI_PushMenu( &s_confirm.menu );

	Menu_SetCursorToItem( &s_confirm.menu, &s_confirm.yes );
}
