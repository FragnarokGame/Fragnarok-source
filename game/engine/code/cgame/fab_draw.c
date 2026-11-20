/*
===========================================================================
Copyright (C) 2026 FRAGNAROK contributors

This file is part of FRAGNAROK, a standalone game built on the
Quake III Arena source code, and is distributed under the same terms:
the GNU General Public License, version 2 or later.
===========================================================================
*/
//
// fab_draw.c -- 2D drawing shared by the HUD (cgame) and the menus (q3_ui)

#include "../qcommon/q_shared.h"
#include "fab_draw.h"
#include "cg_hudfont.h"

// both modules provide these syscalls with the same signatures
qhandle_t	trap_R_RegisterShaderNoMip( const char *name );
void		trap_R_SetColor( const float *rgba );
void		trap_R_DrawStretchPic( float x, float y, float w, float h,
				float s1, float t1, float s2, float t2, qhandle_t hShader );

const vec4_t fabPanel = { 0.02f, 0.04f, 0.06f, 0.62f };
const vec4_t fabPanelLight = { 0.10f, 0.15f, 0.19f, 0.55f };
const vec4_t fabCyan = { 0.42f, 0.90f, 0.95f, 1.0f };
const vec4_t fabAmber = { 1.0f, 0.70f, 0.18f, 1.0f };
const vec4_t fabRed = { 1.0f, 0.24f, 0.20f, 1.0f };
const vec4_t fabWhite = { 0.94f, 0.97f, 1.0f, 1.0f };
const vec4_t fabDim = { 0.62f, 0.70f, 0.76f, 1.0f };
static const vec4_t fabShadow = { 0.0f, 0.0f, 0.0f, 0.55f };

static struct {
	float		scale;
	float		xOffset;
	qhandle_t	white;
	qhandle_t	font;
	qhandle_t	corner;
	qhandle_t	grad;
	qhandle_t	glow;
	qhandle_t	cursor;
} fab = { 1, 0 };

void FAB_Init( void ) {
	fab.white = trap_R_RegisterShaderNoMip( "white" );
	fab.font = trap_R_RegisterShaderNoMip( "gfx/2d/hud_font" );
	fab.corner = trap_R_RegisterShaderNoMip( "gfx/2d/hud_corner" );
	fab.grad = trap_R_RegisterShaderNoMip( "gfx/2d/hud_grad" );
	fab.glow = trap_R_RegisterShaderNoMip( "gfx/2d/hud_glow" );
	fab.cursor = trap_R_RegisterShaderNoMip( "gfx/2d/hud_cursor" );
}

void FAB_SetLayout( float scale, float xOffset ) {
	fab.scale = scale;
	fab.xOffset = xOffset;
}

float FAB_Scale( void ) {
	return fab.scale;
}

float FAB_XOffset( void ) {
	return fab.xOffset;
}

/*
===============================================================================

SHAPES

===============================================================================
*/

void FAB_Pic( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, qhandle_t shader ) {
	trap_R_DrawStretchPic( fab.xOffset + x * fab.scale, y * fab.scale,
		w * fab.scale, h * fab.scale, s1, t1, s2, t2, shader );
}

void FAB_Color( const vec4_t color, float alpha ) {
	vec4_t c;

	Vector4Copy( color, c );
	c[3] *= alpha;
	trap_R_SetColor( c );
}

void FAB_Fill( float x, float y, float w, float h, const vec4_t color, float alpha ) {
	if ( w <= 0 || h <= 0 ) {
		return;
	}
	FAB_Color( color, alpha );
	FAB_Pic( x, y, w, h, 0, 0, 0, 0, fab.white );
}

/*
Fills a rectangle whose chosen corners are cut off at 45 degrees, using one
antialiased triangle texture flipped into each corner.
*/
void FAB_Panel( float x, float y, float w, float h, float cut, int corners,
		const vec4_t color, float alpha ) {
	float l, r;

	if ( cut * 2 > h ) {
		cut = h * 0.5f;
	}
	FAB_Color( color, alpha );

	// middle band
	FAB_Pic( x, y + cut, w, h - cut * 2, 0, 0, 0, 0, fab.white );

	// top band
	l = ( corners & FAB_CUT_TL ) ? cut : 0;
	r = ( corners & FAB_CUT_TR ) ? cut : 0;
	FAB_Pic( x + l, y, w - l - r, cut, 0, 0, 0, 0, fab.white );
	if ( l ) FAB_Pic( x, y, cut, cut, 1, 0, 0, 1, fab.corner );
	if ( r ) FAB_Pic( x + w - cut, y, cut, cut, 0, 0, 1, 1, fab.corner );

	// bottom band
	l = ( corners & FAB_CUT_BL ) ? cut : 0;
	r = ( corners & FAB_CUT_BR ) ? cut : 0;
	FAB_Pic( x + l, y + h - cut, w - l - r, cut, 0, 0, 0, 0, fab.white );
	if ( l ) FAB_Pic( x, y + h - cut, cut, cut, 1, 1, 0, 0, fab.corner );
	if ( r ) FAB_Pic( x + w - cut, y + h - cut, cut, cut, 0, 1, 1, 0, fab.corner );
}

// Horizontal fade from color at x to transparent at x + w (or the reverse).
void FAB_Fade( float x, float y, float w, float h, qboolean toLeft,
		const vec4_t color, float alpha ) {
	FAB_Color( color, alpha );
	if ( toLeft ) {
		FAB_Pic( x, y, w, h, 1, 0, 0, 1, fab.grad );
	} else {
		FAB_Pic( x, y, w, h, 0, 0, 1, 1, fab.grad );
	}
}

void FAB_Glow( float cx, float cy, float size, const vec4_t color, float alpha ) {
	FAB_Color( color, alpha );
	FAB_Pic( cx - size * 0.5f, cy - size * 0.5f, size, size, 0, 0, 1, 1, fab.glow );
}

// Arrow pointer with its tip at (x, y). The art carries its own colours.
void FAB_Cursor( float x, float y, float size ) {
	trap_R_SetColor( NULL );
	FAB_Pic( x - size * 0.0625f, y - size * 0.0625f, size, size, 0, 0, 1, 1, fab.cursor );
}

/*
===============================================================================

TEXT

===============================================================================
*/

float FAB_TextWidth( const char *s, float size, float tracking ) {
	float w = 0;
	float scale = size / HUDFONT_PX;
	int count = 0;

	while ( *s ) {
		int c;

		if ( Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		c = *s & 255;
		if ( c < HUDFONT_FIRST || c > HUDFONT_LAST ) {
			c = '?';
		}
		w += hudFontAdvance[c - HUDFONT_FIRST] * scale;
		count++;
		s++;
	}
	if ( count > 1 ) {
		w += tracking * ( count - 1 );
	}
	return w;
}

static void FAB_TextRaw( float x, float baseline, float size, float tracking,
		const char *s, const vec4_t color, float alpha, qboolean useCodes ) {
	float scale = size / HUDFONT_PX;
	float cellW = HUDFONT_CELL_W * scale;
	float cellH = HUDFONT_CELL_H * scale;
	float top = baseline - ( HUDFONT_PAD + HUDFONT_ASCENT ) * scale;
	vec4_t c;

	Vector4Copy( color, c );
	c[3] *= alpha;
	trap_R_SetColor( c );

	while ( *s ) {
		int ch, cell;
		float s1, t1;

		if ( Q_IsColorString( s ) ) {
			if ( useCodes ) {
				VectorCopy( g_color_table[ColorIndex( s[1] )], c );
				trap_R_SetColor( c );
			}
			s += 2;
			continue;
		}
		ch = *s & 255;
		if ( ch < HUDFONT_FIRST || ch > HUDFONT_LAST ) {
			ch = '?';
		}
		if ( ch != ' ' ) {
			cell = ch - HUDFONT_FIRST;
			s1 = ( cell % HUDFONT_COLS ) * HUDFONT_CELL_W / (float)HUDFONT_ATLAS;
			t1 = ( cell / HUDFONT_COLS ) * HUDFONT_CELL_H / (float)HUDFONT_ATLAS;
			FAB_Pic( x - HUDFONT_PAD * scale, top, cellW, cellH, s1, t1,
				s1 + HUDFONT_CELL_W / (float)HUDFONT_ATLAS,
				t1 + HUDFONT_CELL_H / (float)HUDFONT_ATLAS, fab.font );
		}
		x += hudFontAdvance[ch - HUDFONT_FIRST] * scale + tracking;
		s++;
	}
}

/*
Draws text with its baseline at y. Names may carry ^ colour codes; those are
honoured unless the caller forces a colour by passing useCodes = qfalse.
Returns the drawn width.
*/
float FAB_TextExt( float x, float y, float size, float tracking, int align,
		const char *s, const vec4_t color, float alpha, qboolean useCodes ) {
	float w = FAB_TextWidth( s, size, tracking );
	float shadow = size * 0.045f + 0.4f;

	if ( align == FAB_ALIGN_CENTER ) {
		x -= w * 0.5f;
	} else if ( align == FAB_ALIGN_RIGHT ) {
		x -= w;
	}
	FAB_TextRaw( x + shadow, y + shadow, size, tracking, s, fabShadow, alpha * color[3], qfalse );
	FAB_TextRaw( x, y, size, tracking, s, color, alpha, useCodes );
	trap_R_SetColor( NULL );
	return w;
}

// No shadow and no alignment, for callers that do both themselves (q3_ui text).
void FAB_TextPlain( float x, float y, float size, float tracking, const char *s,
		const vec4_t color, qboolean useCodes ) {
	FAB_TextRaw( x, y, size, tracking, s, color, 1, useCodes );
	trap_R_SetColor( NULL );
}

float FAB_Text( float x, float y, float size, int align, const char *s,
		const vec4_t color, float alpha ) {
	return FAB_TextExt( x, y, size, 0, align, s, color, alpha, qfalse );
}

// Small spaced-out caps, used for every label.
float FAB_Label( float x, float y, float size, int align, const char *s,
		const vec4_t color, float alpha ) {
	return FAB_TextExt( x, y, size, size * 0.18f, align, s, color, alpha, qfalse );
}

float FAB_Name( float x, float y, float size, int align, const char *s, float alpha ) {
	return FAB_TextExt( x, y, size, 0, align, s, fabWhite, alpha, qtrue );
}

const char *FAB_Upper( const char *str ) {
	static char buf[1024];

	Q_strncpyz( buf, str ? str : "", sizeof( buf ) );
	return Q_strupr( buf );
}

// Greedy word wrap; returns the y below the last line.
float FAB_Wrapped( float x, float y, float width, float size, float lineHeight, int align,
		const char *text, const vec4_t color, float alpha ) {
	char line[256];
	const char *p = text;

	while ( *p ) {
		const char *end = NULL, *q = p;
		int len;

		// take whole words while the line still fits
		while ( *q && *q != '\n' ) {
			const char *w = q;

			while ( *w == ' ' ) {
				w++;
			}
			while ( *w && *w != ' ' && *w != '\n' ) {
				w++;
			}
			len = w - p < (int)sizeof( line ) - 1 ? w - p : (int)sizeof( line ) - 1;
			memcpy( line, p, len );
			line[len] = 0;
			if ( end && FAB_TextWidth( line, size, 0 ) > width ) {
				break;
			}
			end = q = w;
		}
		if ( !end ) {
			end = q;
		}
		len = end - p < (int)sizeof( line ) - 1 ? end - p : (int)sizeof( line ) - 1;
		memcpy( line, p, len );
		line[len] = 0;
		FAB_Text( x, y, size, align, line, color, alpha );
		y += lineHeight;

		p = end;
		while ( *p == ' ' ) {
			p++;
		}
		if ( *p == '\n' ) {
			p++;
		}
	}
	return y;
}

/*
===============================================================================

MENU WIDGETS

===============================================================================
*/

void FAB_Button( float x, float y, float w, float h, const char *label, int flags ) {
	const float *accent = ( flags & FAB_BUTTON_DANGER ) ? fabRed : fabCyan;
	qboolean focus = ( flags & FAB_BUTTON_FOCUS ) && !( flags & FAB_BUTTON_DISABLED );
	qboolean left = ( flags & FAB_BUTTON_LEFT ) != 0;
	float size = h * 0.42f;
	float baseline = y + h * 0.5f + size * 0.36f;
	int align = left ? FAB_ALIGN_LEFT : FAB_ALIGN_CENTER;
	float tx = left ? x + 14 : x + w * 0.5f;

	if ( flags & FAB_BUTTON_DISABLED ) {
		FAB_Panel( x, y, w, h, 6, FAB_CUT_TL | FAB_CUT_BR, fabPanelLight, 0.5f );
		FAB_Label( tx, baseline, size, align, label, fabDim, 0.35f );
		return;
	}
	if ( focus && left ) {
		// the label slides over to make room for the chevron
		tx += 6;
	}
	if ( flags & FAB_BUTTON_PRIMARY ) {
		FAB_Panel( x, y, w, h, 6, FAB_CUT_TL | FAB_CUT_BR, accent, focus ? 1.0f : 0.62f );
		FAB_Label( tx, baseline, size, align, label, fabPanel, 1 );
		if ( focus ) {
			FAB_Fill( x - 5, y + 4, 2, h - 8, accent, 1 );
			FAB_Label( x + w - 14, baseline, size, FAB_ALIGN_RIGHT, ">", fabPanel, 1 );
		}
		return;
	}
	FAB_Panel( x, y, w, h, 6, FAB_CUT_TL | FAB_CUT_BR, focus ? accent : fabPanelLight,
		focus ? 0.34f : 0.75f );
	if ( focus ) {
		FAB_Fill( x, y + 4, 3, h - 8, accent, 1 );
		FAB_Fill( x - 5, y + 4, 2, h - 8, accent, 1 );
		FAB_Label( x + w - 14, baseline, size, FAB_ALIGN_RIGHT, ">", accent, 1 );
	}
	FAB_Label( tx, baseline, size, align, label, focus ? fabWhite : fabDim, 1 );
}
