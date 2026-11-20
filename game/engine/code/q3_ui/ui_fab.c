/*
===========================================================================
Copyright (C) 2026 FRAGNAROK contributors

This file is part of FRAGNAROK, a standalone game built on the
Quake III Arena source code, and is distributed under the same terms:
the GNU General Public License, version 2 or later.
===========================================================================
*/
//
// ui_fab.c -- FRAGNAROK menu widgets
//
// The q3_ui menu framework keeps doing focus, keyboard and mouse routing;
// these helpers set items up as owner-drawn and paint them in the HUD style
// (cgame/fab_draw.c). Coordinates are the menu's 640x480 space, which is
// 4:3 and centred, so FabUI_Left/Right give the real screen edges.

#include "ui_local.h"

#ifdef STANDALONE

#define FABUI_ARROW_W	22		// click zone of a setting row's "<" arrow

static void FabUI_DropOpen( menulist_s *l );

/*
Stock widgets (spin, toggle, slider) as compact rows: the label right-aligned
before x, the value after it, all within the stock menus' 18-unit spacing.
*/
#define FABUI_LABEL_SIZE	8.5f

static float FabUI_StockLabelWidth( const char *name ) {
	return name ? FAB_TextWidth( FabUI_Upper( name ), FABUI_LABEL_SIZE, FABUI_LABEL_SIZE * 0.18f ) : 0;
}

void FabUI_StockBounds( menucommon_s *g, float valueWidth ) {
	g->left = g->x - 12 - FabUI_StockLabelWidth( g->name );
	g->right = g->x + 12 + valueWidth;
	g->top = g->y - 1;
	g->bottom = g->y + 17;
}

void FabUI_StockRow( menucommon_s *g, qboolean focus ) {
	qboolean grayed = ( g->flags & QMF_GRAYED ) != 0;

	FabUI_Begin();
	if ( focus && !grayed ) {
		FAB_Fill( g->left - 6, g->top, g->right - g->left + 12, g->bottom - g->top, fabCyan, 0.14f );
		FAB_Fill( g->left - 6, g->top, 2, g->bottom - g->top, fabCyan, 1 );
	}
	if ( g->name ) {
		FAB_Label( g->x - 12, g->y + 12, FABUI_LABEL_SIZE, FAB_ALIGN_RIGHT, FabUI_Upper( g->name ),
			grayed ? fabDim : focus ? fabWhite : fabDim, grayed ? 0.45f : 1 );
	}
}

void FabUI_Begin( void ) {
	FAB_SetLayout( uis.xscale, uis.bias );
}

float FabUI_Left( void ) {
	return -uis.bias / uis.xscale;
}

float FabUI_Right( void ) {
	return SCREEN_WIDTH + uis.bias / uis.xscale;
}

void FabUI_Dim( float alpha ) {
	vec4_t black = { 0, 0, 0, 1 };

	FAB_Fill( FabUI_Left(), 0, FabUI_Right() - FabUI_Left(), SCREEN_HEIGHT, black, alpha );
}

// Dark band down the left of the screen that fades out to the right.
void FabUI_SideShade( float width ) {
	vec4_t black = { 0, 0, 0, 1 };
	float left = FabUI_Left();

	FAB_Fill( left, 0, width * 0.55f, SCREEN_HEIGHT, black, 0.72f );
	FAB_Fade( left + width * 0.55f, 0, width * 0.75f, SCREEN_HEIGHT, qfalse, black, 0.72f );
}

void FabUI_Header( float x, float y, const char *eyebrow, const char *title, const char *subtitle ) {
	FAB_Fill( x, y - 30, 26, 2, fabCyan, 1 );
	if ( eyebrow ) {
		FAB_Label( x, y - 14, 8, FAB_ALIGN_LEFT, eyebrow, fabCyan, 1 );
	}
	FAB_Text( x, y + 14, 26, FAB_ALIGN_LEFT, title, fabWhite, 1 );
	if ( subtitle && subtitle[0] ) {
		FAB_Label( x, y + 31, 7, FAB_ALIGN_LEFT, subtitle, fabDim, 1 );
	}
}

static qboolean FabUI_Focused( menucommon_s *g ) {
	return g->parent && g->parent->cursor == g->menuPosition;
}

static void FabUI_Bounds( menucommon_s *g, float x, float y, float w, float h ) {
	g->x = x;
	g->y = y;
	g->left = x;
	g->top = y;
	g->right = x + w;
	g->bottom = y + h;
}

/*
===============================================================================

BUTTONS

===============================================================================
*/

void FabUI_DrawButton( void *self ) {
	menutext_s *t = (menutext_s *)self;
	menucommon_s *g = &t->generic;
	int flags = t->style;

	if ( FabUI_Focused( g ) ) {
		flags |= FAB_BUTTON_FOCUS;
	}
	if ( g->flags & QMF_GRAYED ) {
		flags |= FAB_BUTTON_DISABLED;
	}
	FAB_Button( g->left, g->top, g->right - g->left, g->bottom - g->top, t->string, flags );
}

void FabUI_InitButton( menutext_s *t, float x, float y, float w, float h, const char *label,
		int id, void (*callback)( void *self, int event ), int buttonFlags ) {
	t->generic.type = MTYPE_PTEXT;
	t->generic.flags = QMF_NODEFAULTINIT;
	t->generic.ownerdraw = FabUI_DrawButton;
	t->generic.id = id;
	t->generic.callback = callback;
	FabUI_Bounds( &t->generic, x, y, w, h );
	t->string = (char *)label;
	t->style = buttonFlags;
}

/*
===============================================================================

SETTING ROWS

A spin control drawn as "LABEL ........ < VALUE >". Clicking the row steps
forward; clicking the "<" steps back; left and right arrows also work.

===============================================================================
*/

static float FabUI_ArrowX( menulist_s *l ) {
	float valueX = l->generic.right - 12 - FABUI_ARROW_W;
	return valueX - FAB_TextWidth( l->itemnames[l->curvalue], 11, 0 ) - 9;
}

void FabUI_DrawSettingEx( menulist_s *l, qboolean changed ) {
	menucommon_s *g = &l->generic;
	qboolean focus = FabUI_Focused( g );
	qboolean grayed = ( g->flags & QMF_GRAYED ) != 0;
	float y = g->top, h = g->bottom - g->top;
	float baseline = y + h * 0.5f + 3.6f;
	float valueX = g->right - 12 - FABUI_ARROW_W;
	const float *valueColor = grayed ? fabDim : ( changed ? fabAmber : fabWhite );

	if ( focus && !grayed ) {
		FAB_Fill( g->left, y, g->right - g->left, h, fabCyan, 0.14f );
		FAB_Fill( g->left, y, 2, h, fabCyan, 1 );
	} else {
		FAB_Fill( g->left, y, g->right - g->left, h, fabPanelLight, 0.45f );
	}
	FAB_Label( g->left + 12, baseline, 8, FAB_ALIGN_LEFT, g->name,
		focus && !grayed ? fabWhite : fabDim, grayed ? 0.5f : 1 );

	FAB_Text( valueX, baseline + 0.5f, 11, FAB_ALIGN_RIGHT, l->itemnames[l->curvalue],
		valueColor, grayed ? 0.5f : 1 );
	if ( !grayed ) {
		float a = focus ? 1.0f : 0.45f;
		FAB_Text( FabUI_ArrowX( l ), baseline, 10, FAB_ALIGN_RIGHT, "<", fabCyan, a );
		FAB_Text( g->right - 12, baseline, 10, FAB_ALIGN_RIGHT, ">", fabCyan, a );
	}
}

void FabUI_DrawSetting( void *self ) {
	FabUI_DrawSettingEx( (menulist_s *)self, qfalse );
}

void FabUI_InitSetting( menulist_s *l, float x, float y, float w, float h, const char *name,
		int id, void (*callback)( void *self, int event ), const char **names ) {
	l->generic.type = MTYPE_SPINCONTROL;
	l->generic.flags = QMF_NODEFAULTINIT;
	l->generic.ownerdraw = FabUI_DrawSetting;
	l->generic.name = name;
	l->generic.id = id;
	l->generic.callback = callback;
	FabUI_Bounds( &l->generic, x, y, w, h );
	l->itemnames = names;
	for ( l->numitems = 0; names[l->numitems]; l->numitems++ ) {
	}
	if ( l->curvalue >= l->numitems ) {
		l->curvalue = 0;
	}
}

/*
Key filter for menus with setting rows. The stock spin control stepped on a
click anywhere on screen while it had keyboard focus; clicks now only count
over the row, and land on "<" to step back.
*/
sfxHandle_t FabUI_Key( menuframework_s *m, int key ) {
	menucommon_s *item = Menu_ItemAtCursor( m );

	if ( FabUI_IsDropdown( item ) && !( item->flags & QMF_GRAYED ) &&
		( key == K_ENTER || key == K_KP_ENTER ||
		( key == K_MOUSE1 && ( item->flags & QMF_HASMOUSEFOCUS ) ) ) ) {
		FabUI_DropOpen( (menulist_s *)item );
		return menu_move_sound;
	}
	if ( FabUI_IsDropdown( item ) && key == K_MOUSE1 ) {
		return menu_null_sound;
	}
	if ( item && item->type == MTYPE_SPINCONTROL && key == K_MOUSE1 &&
		( item->flags & QMF_NODEFAULTINIT ) ) {
		float arrowX = FabUI_ArrowX( (menulist_s *)item );

		if ( !( item->flags & QMF_HASMOUSEFOCUS ) || ( item->flags & QMF_GRAYED ) ) {
			return menu_null_sound;
		}
		if ( uis.cursorx < arrowX + 4 && uis.cursorx > arrowX - FABUI_ARROW_W ) {
			key = K_LEFTARROW;
		}
	}
	return Menu_DefaultKey( m, key );
}

/*
===============================================================================

DROPDOWNS

A setting row that opens a scrolling list of its choices instead of only
stepping through them, for settings with too many values to cycle. Left and
right arrows still step. The open list draws over the whole menu and takes
every key until it closes, so Escape closes the list rather than the menu.

===============================================================================
*/

#define FABUI_DROP_ROW_H	22
#define FABUI_DROP_VISIBLE	8
#define FABUI_DROP_BAR_W	6
#define FABUI_MAX_DETAILS	8

static const vec4_t fabDropBack = { 0.03f, 0.05f, 0.07f, 1.0f };

static struct {
	menulist_s *list;	// open dropdown, or NULL
	menuframework_s *menu;
	int highlight;
	int scroll;
	int cursorX, cursorY;	// mouse position the highlight last followed
} fabDrop;

static struct {
	menulist_s *list;
	fabDropdownDetail_t detail;
} fabDropDetails[FABUI_MAX_DETAILS];

static fabDropdownDetail_t FabUI_DropdownDetail( menulist_s *l ) {
	int i;

	for ( i = 0; i < FABUI_MAX_DETAILS; i++ ) {
		if ( fabDropDetails[i].list == l ) {
			return fabDropDetails[i].detail;
		}
	}
	return NULL;
}

// Right-aligned extra text on each choice in the open list, such as a map's size.
void FabUI_SetDropdownDetail( menulist_s *l, fabDropdownDetail_t detail ) {
	int i, slot = -1;

	for ( i = 0; i < FABUI_MAX_DETAILS; i++ ) {
		if ( fabDropDetails[i].list == l ) {
			slot = i;
			break;
		}
		if ( slot < 0 && !fabDropDetails[i].list ) {
			slot = i;
		}
	}
	if ( slot >= 0 ) {
		fabDropDetails[slot].list = l;
		fabDropDetails[slot].detail = detail;
	}
}

static int FabUI_DropVisible( void ) {
	int n = fabDrop.list->numitems;
	return n < FABUI_DROP_VISIBLE ? n : FABUI_DROP_VISIBLE;
}

// Open list bounds: below the row, or above it when it would run off screen.
static void FabUI_DropRect( float *x, float *y, float *w, float *h ) {
	menucommon_s *g = &fabDrop.list->generic;

	*x = g->left;
	*w = g->right - g->left;
	*h = FabUI_DropVisible() * FABUI_DROP_ROW_H + 4;
	*y = g->bottom + 2;
	if ( *y + *h > SCREEN_HEIGHT - 8 ) {
		*y = g->top - 2 - *h;
	}
}

static qboolean FabUI_DropScrolls( void ) {
	return fabDrop.list->numitems > FABUI_DROP_VISIBLE;
}

static void FabUI_DropClampScroll( void ) {
	int maxScroll = fabDrop.list->numitems - FabUI_DropVisible();

	if ( fabDrop.scroll > maxScroll ) fabDrop.scroll = maxScroll;
	if ( fabDrop.scroll < 0 ) fabDrop.scroll = 0;
}

static void FabUI_DropShowHighlight( void ) {
	if ( fabDrop.highlight < fabDrop.scroll ) {
		fabDrop.scroll = fabDrop.highlight;
	} else if ( fabDrop.highlight >= fabDrop.scroll + FabUI_DropVisible() ) {
		fabDrop.scroll = fabDrop.highlight - FabUI_DropVisible() + 1;
	}
	FabUI_DropClampScroll();
}

// Choice under the mouse, or -1. The scrollbar strip doesn't count.
static int FabUI_DropHit( void ) {
	float x, y, w, h;
	int row;

	FabUI_DropRect( &x, &y, &w, &h );
	if ( FabUI_DropScrolls() ) {
		w -= FABUI_DROP_BAR_W + 4;
	}
	if ( uis.cursorx < x || uis.cursorx >= x + w || uis.cursory < y + 2 || uis.cursory >= y + h - 2 ) {
		return -1;
	}
	row = fabDrop.scroll + (int)( ( uis.cursory - y - 2 ) / FABUI_DROP_ROW_H );
	return row < fabDrop.list->numitems ? row : -1;
}

static void FabUI_DropClose( void ) {
	if ( fabDrop.menu ) {
		// the mouse may have moved focus to a row under the list
		Menu_SetCursorToItem( fabDrop.menu, fabDrop.list );
	}
	fabDrop.list = NULL;
	fabDrop.menu = NULL;
}

static void FabUI_DropOpen( menulist_s *l ) {
	fabDrop.list = l;
	fabDrop.menu = l->generic.parent;
	fabDrop.highlight = l->curvalue;
	fabDrop.scroll = l->curvalue - FABUI_DROP_VISIBLE / 2;
	fabDrop.cursorX = uis.cursorx;
	fabDrop.cursorY = uis.cursory;
	FabUI_DropClampScroll();
}

qboolean FabUI_DropdownOpen( void ) {
	if ( fabDrop.list && fabDrop.menu != uis.activemenu ) {
		// the menu changed underneath the list
		fabDrop.list = NULL;
		fabDrop.menu = NULL;
	}
	return fabDrop.list != NULL;
}

static sfxHandle_t FabUI_DropChoose( int item ) {
	menulist_s *l = fabDrop.list;

	FabUI_DropClose();
	if ( item < 0 || item >= l->numitems ) {
		return menu_out_sound;
	}
	if ( item != l->curvalue ) {
		l->curvalue = item;
		if ( l->generic.callback ) {
			l->generic.callback( l, QM_ACTIVATED );
		}
	}
	return menu_move_sound;
}

void FabUI_CloseDropdown( void ) {
	if ( FabUI_DropdownOpen() ) {
		FabUI_DropClose();
	}
}

#define FABUI_DROP_PASS		-1	// key closed the list and goes on to the menu

/*
Keys while a list is open. Everything is swallowed so nothing behind it
reacts, except left and right: those close the list and pass on, so the row
steps as it would closed.
*/
static sfxHandle_t FabUI_DropKey( int key ) {
	int page = FabUI_DropVisible() - 1;

	switch ( key ) {
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		FabUI_DropClose();
		return FABUI_DROP_PASS;

	case K_ESCAPE:
	case K_MOUSE2:
	case K_BACKSPACE:
		FabUI_DropClose();
		return menu_out_sound;

	case K_ENTER:
	case K_KP_ENTER:
	case K_SPACE:
		return FabUI_DropChoose( fabDrop.highlight );

	case K_MOUSE1: {
		float x, y, w, h;
		int row = FabUI_DropHit();

		if ( row >= 0 ) {
			return FabUI_DropChoose( row );
		}
		FabUI_DropRect( &x, &y, &w, &h );
		if ( FabUI_DropScrolls() && uis.cursorx >= x + w - FABUI_DROP_BAR_W - 4 &&
			uis.cursorx <= x + w && uis.cursory >= y && uis.cursory <= y + h ) {
			// a click on the scrollbar track jumps there
			fabDrop.scroll = (int)( ( uis.cursory - y ) / h * fabDrop.list->numitems ) -
				FabUI_DropVisible() / 2;
			FabUI_DropClampScroll();
			return menu_null_sound;
		}
		// a click outside closes the list and does nothing else
		FabUI_DropClose();
		return menu_out_sound;
	}

	case K_MWHEELUP:
		fabDrop.scroll--;
		FabUI_DropClampScroll();
		return menu_null_sound;
	case K_MWHEELDOWN:
		fabDrop.scroll++;
		FabUI_DropClampScroll();
		return menu_null_sound;

	case K_UPARROW:
	case K_KP_UPARROW:
		fabDrop.highlight--;
		break;
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
	case K_TAB:
		fabDrop.highlight++;
		break;
	case K_PGUP:
	case K_KP_PGUP:
		fabDrop.highlight -= page;
		break;
	case K_PGDN:
	case K_KP_PGDN:
		fabDrop.highlight += page;
		break;
	case K_HOME:
	case K_KP_HOME:
		fabDrop.highlight = 0;
		break;
	case K_END:
	case K_KP_END:
		fabDrop.highlight = fabDrop.list->numitems - 1;
		break;
	default:
		return menu_null_sound;
	}
	if ( fabDrop.highlight < 0 ) fabDrop.highlight = 0;
	if ( fabDrop.highlight >= fabDrop.list->numitems ) fabDrop.highlight = fabDrop.list->numitems - 1;
	FabUI_DropShowHighlight();
	return menu_move_sound;
}

/*
qtrue when an open list took the key. The sound is returned separately
because handles are 0 with sound disabled, which can't mean "not handled".
*/
qboolean FabUI_DropdownKey( int key, sfxHandle_t *sound ) {
	*sound = 0;
	if ( !FabUI_DropdownOpen() ) {
		return qfalse;
	}
	*sound = FabUI_DropKey( key );
	if ( *sound == FABUI_DROP_PASS ) {
		*sound = 0;
		return qfalse;
	}
	return qtrue;
}

// Small triangle built from stacked bars, pointing down (or up while open).
static void FabUI_Chevron( float cx, float y, const vec4_t color, float alpha, qboolean up ) {
	int i;

	for ( i = 0; i < 4; i++ ) {
		float w = 8 - i * 2;
		FAB_Fill( cx - w * 0.5f, up ? y + 3.6f - i * 1.2f : y + i * 1.2f, w, 1.2f, color, alpha );
	}
}

// Drawn last each frame (UI_Refresh), above the whole menu.
void FabUI_DrawDropdownPopup( void ) {
	float x, y, w, h, textW;
	fabDropdownDetail_t detail;
	int i, row, hover;

	if ( !FabUI_DropdownOpen() ) {
		return;
	}
	FabUI_Begin();
	hover = FabUI_DropHit();
	if ( hover >= 0 && ( uis.cursorx != fabDrop.cursorX || uis.cursory != fabDrop.cursorY ) ) {
		// the highlight follows the mouse only once it moves, so arrow keys
		// aren't undone by a pointer resting over the list
		fabDrop.highlight = hover;
	}
	fabDrop.cursorX = uis.cursorx;
	fabDrop.cursorY = uis.cursory;

	FabUI_DropRect( &x, &y, &w, &h );
	textW = FabUI_DropScrolls() ? w - FABUI_DROP_BAR_W - 6 : w;
	detail = FabUI_DropdownDetail( fabDrop.list );
	// solid backing: the shared panel colour is see-through and the rows
	// under the list would read through it
	FAB_Fill( x, y, w, h, fabDropBack, 1 );
	FAB_Fill( x, y, w, 1, fabCyan, 0.8f );
	FAB_Fill( x, y + h - 1, w, 1, fabCyan, 0.35f );

	for ( i = 0; i < FabUI_DropVisible(); i++ ) {
		float ry, baseline;
		qboolean selected, lit;
		const char *extra;
		float extraAlpha = 0.7f;

		row = fabDrop.scroll + i;
		ry = y + 2 + i * FABUI_DROP_ROW_H;
		baseline = ry + FABUI_DROP_ROW_H * 0.5f + 3.4f;
		selected = row == fabDrop.list->curvalue;
		lit = row == fabDrop.highlight;
		if ( lit ) {
			FAB_Fill( x + 2, ry, textW - 4, FABUI_DROP_ROW_H - 1, fabCyan, 0.16f );
			FAB_Fill( x + 2, ry, 2, FABUI_DROP_ROW_H - 1, fabCyan, 1 );
		}
		if ( selected ) {
			FAB_Fill( x + 8, ry + FABUI_DROP_ROW_H * 0.5f - 2, 3, 3, fabAmber, 1 );
		}
		FAB_Text( x + 16, baseline, 10, FAB_ALIGN_LEFT, fabDrop.list->itemnames[row],
			lit || selected ? fabWhite : fabDim, 1 );
		extra = detail ? detail( fabDrop.list, row, &extraAlpha ) : NULL;
		if ( extra && extra[0] ) {
			FAB_Label( x + textW - 10, baseline - 0.5f, 6.5f, FAB_ALIGN_RIGHT, extra,
				lit ? fabCyan : fabDim, extraAlpha );
		}
	}

	if ( FabUI_DropScrolls() ) {
		float trackX = x + w - FABUI_DROP_BAR_W - 3, trackY = y + 4, trackH = h - 8;
		float thumbH = trackH * FabUI_DropVisible() / fabDrop.list->numitems;
		float thumbY = trackY + ( trackH - thumbH ) * fabDrop.scroll /
			( fabDrop.list->numitems - FabUI_DropVisible() );

		FAB_Fill( trackX, trackY, FABUI_DROP_BAR_W, trackH, fabPanelLight, 0.6f );
		FAB_Fill( trackX, thumbY, FABUI_DROP_BAR_W, thumbH, fabCyan, 0.85f );
	}
}

void FabUI_DrawDropdown( void *self ) {
	menulist_s *l = (menulist_s *)self;
	menucommon_s *g = &l->generic;
	qboolean open = fabDrop.list == l && FabUI_DropdownOpen();
	qboolean focus = open || FabUI_Focused( g );
	qboolean grayed = ( g->flags & QMF_GRAYED ) != 0;
	float y = g->top, h = g->bottom - g->top;
	float baseline = y + h * 0.5f + 3.6f;
	float valueX = g->right - 12 - FABUI_ARROW_W;

	if ( focus && !grayed ) {
		FAB_Fill( g->left, y, g->right - g->left, h, fabCyan, 0.14f );
		FAB_Fill( g->left, y, 2, h, fabCyan, 1 );
	} else {
		FAB_Fill( g->left, y, g->right - g->left, h, fabPanelLight, 0.45f );
	}
	FAB_Label( g->left + 12, baseline, 8, FAB_ALIGN_LEFT, g->name,
		focus && !grayed ? fabWhite : fabDim, grayed ? 0.5f : 1 );
	FAB_Text( valueX, baseline + 0.5f, 11, FAB_ALIGN_RIGHT, l->itemnames[l->curvalue],
		grayed ? fabDim : fabWhite, grayed ? 0.5f : 1 );
	if ( !grayed ) {
		FAB_Fill( valueX + 6, y + 5, 1, h - 10, fabCyan, 0.3f );
		FabUI_Chevron( g->right - 12 - FABUI_ARROW_W * 0.5f + 2, y + h * 0.5f - 2,
			fabCyan, focus ? 1.0f : 0.45f, open );
	}
}

qboolean FabUI_IsDropdown( const menucommon_s *g ) {
	return g && g->ownerdraw == FabUI_DrawDropdown;
}

void FabUI_InitDropdown( menulist_s *l, float x, float y, float w, float h, const char *name,
		int id, void (*callback)( void *self, int event ), const char **names ) {
	FabUI_InitSetting( l, x, y, w, h, name, id, callback, names );
	l->generic.ownerdraw = FabUI_DrawDropdown;
}

/*
===============================================================================

TEXT FIELD

===============================================================================
*/

static void FabUI_DrawField( void *self ) {
	menufield_s *f = (menufield_s *)self;
	menucommon_s *g = &f->generic;
	qboolean focus = FabUI_Focused( g );
	float h = g->bottom - g->top;
	float baseline = g->top + h * 0.5f + 3.8f;
	float textX = g->left + 96;
	char before[MAX_EDIT_LINE];
    char display[MAX_EDIT_LINE];
    int cursor = f->field.cursor;
    const char *text = f->field.buffer;

    if ( g->flags & QMF_AUTHFIELD ) {
        int length, scroll = f->field.scroll;
        Q_strncpyz( display, text, sizeof( display ) );
        length = strlen( display );
        if ( g->flags & QMF_PASSWORD ) memset( display, '*', length );
        /* Keep the caret visible using the font's actual glyph widths. */
        if ( scroll > cursor ) scroll = cursor;
        if ( scroll < 0 ) scroll = 0;
        if ( scroll > length ) scroll = length;
        Q_strncpyz( before, display, sizeof( before ) );
        before[cursor] = 0;
        while ( scroll < cursor && FAB_TextWidth( before + scroll, 11, 0 ) > g->right - textX - 16 ) scroll++;
        f->field.scroll = scroll;
        text = display + scroll;
        cursor -= scroll;
        while ( strlen( text ) > (unsigned int)cursor && FAB_TextWidth( text, 11, 0 ) > g->right - textX - 12 ) {
            display[strlen( display ) - 1] = 0;
        }
    }

	FAB_Fill( g->left, g->top, g->right - g->left, h, focus ? fabCyan : fabPanelLight,
		focus ? 0.14f : 0.45f );
	if ( focus ) {
		FAB_Fill( g->left, g->top, 2, h, fabCyan, 1 );
	}
	FAB_Label( g->left + 12, baseline, 8, FAB_ALIGN_LEFT, g->name, focus ? fabWhite : fabDim, 1 );
	FAB_Fill( textX - 6, g->top + 4, g->right - textX, h - 8, fabPanel, 0.8f );
	FAB_Text( textX, baseline + 0.5f, 11, FAB_ALIGN_LEFT, text, fabWhite, 1 );

	if ( focus && ( ( uis.realtime >> 9 ) & 1 ) ) {
		Q_strncpyz( before, text, sizeof( before ) );
		if ( cursor >= 0 && cursor < (int)sizeof( before ) ) {
			before[cursor] = 0;
		}
		FAB_Fill( textX + FAB_TextWidth( before, 11, 0 ) + 1, g->top + 6, 1.2f, h - 12, fabCyan, 1 );
	}
}

void FabUI_InitField( menufield_s *f, float x, float y, float w, float h, const char *name,
		int id, int maxchars ) {
	f->generic.type = MTYPE_FIELD;
	f->generic.flags = QMF_NODEFAULTINIT;
	f->generic.ownerdraw = FabUI_DrawField;
	f->generic.name = name;
	f->generic.id = id;
	FabUI_Bounds( &f->generic, x, y, w, h );
	f->field.widthInChars = maxchars;
	f->field.maxchars = maxchars;
}

#endif // STANDALONE
