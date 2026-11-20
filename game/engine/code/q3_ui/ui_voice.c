/*
===========================================================================
Copyright (C) 2026 FRAGNAROK contributors

This file is part of FRAGNAROK, a standalone game built on the
Quake III Arena source code, and is distributed under the same terms:
the GNU General Public License, version 2 or later.
===========================================================================
*/
//
// ui_voice.c -- Settings > Voice: proximity voice chat options
//
// Everything here is a cvar the client already reads: cl_voip, the talk
// mode (cl_voipUseVAD) and its threshold, the incoming voice volume, how
// loud distant voices stay (cl_voipFarVolume), and the
// SDL microphone and speaker device names. The mic test sets cl_voipMicTest
// while the screen is open and draws the level the engine publishes in
// cl_voipMicLevel.

#include "ui_local.h"

#define ART_BACK0			"menu/art/back_0"
#define ART_BACK1			"menu/art/back_1"

#define VOICE_X				360
#define VOICE_MAX_DEVICES	16
#define VOICE_NAME_CHARS	32		// longer device names are cut short in the row

// Stock sliders step by one unit per arrow press, so these run in whole steps.
#define THRESHOLD_STEP		0.05f	// activation level: 1..20 steps, 0.05..1.0
#define VOLUME_STEP			0.1f	// voice volume: 0..20 steps, 0..2x
#define FAR_STEP			0.1f	// distant voices: 0..10 steps, 0..1x

#define ID_VOIP				10
#define ID_MODE				11
#define ID_THRESHOLD		12
#define ID_VOLUME			13
#define ID_MIC				14
#define ID_OUTPUT			15
#define ID_MICTEST			16
#define ID_BACK				17
#define ID_FAR				18

typedef struct {
	int			count;
	char		names[VOICE_MAX_DEVICES][128];		// as SDL reports them, for the cvar
	char		labels[VOICE_MAX_DEVICES][VOICE_NAME_CHARS + 4];
	const char	*items[VOICE_MAX_DEVICES + 2];		// "system default" first, then NULL
} voiceDevices_t;

typedef struct {
	menuframework_s		menu;

	menutext_s			banner;
	menuradiobutton_s	voip;
	menulist_s			mode;
	menuslider_s		threshold;
	menuslider_s		volume;
	menuslider_s		distant;
	menulist_s			mic;
	menulist_s			output;
	menulist_s			mictest;
	menubitmap_s		back;

	voiceDevices_t		mics;
	voiceDevices_t		outputs;
	char				pttKey[64];
} voiceMenu_t;

static voiceMenu_t s_voice;

static const char *voice_modes[] = {
	"push to talk",
	"voice activated",
	NULL
};

static const char *voice_tests[] = {
	"off",
	"meter",
	"hear myself",
	NULL
};

/*
=================
Voice_LoadDevices

Reads a newline-separated device list cvar and selects the saved device.
A saved device that isn't plugged in still shows, so picking nothing
doesn't silently change the setting.
=================
*/
static void Voice_LoadDevices( voiceDevices_t *d, menulist_s *list, const char *listCvar, const char *cvar ) {
	char buffer[4096], current[128];
	char *s = buffer;
	int i;

	trap_Cvar_VariableStringBuffer( listCvar, buffer, sizeof( buffer ) );
	trap_Cvar_VariableStringBuffer( cvar, current, sizeof( current ) );

	d->count = 0;
	while ( *s && d->count < VOICE_MAX_DEVICES ) {
		char *end = strchr( s, '\n' );

		if ( end ) {
			*end = '\0';
		}
		if ( *s ) {
			Q_strncpyz( d->names[d->count++], s, sizeof( d->names[0] ) );
		}
		if ( !end ) {
			break;
		}
		s = end + 1;
	}

	list->curvalue = 0;
	if ( current[0] ) {
		for ( i = 0; i < d->count; i++ ) {
			if ( !strcmp( d->names[i], current ) ) {
				break;
			}
		}
		if ( i == d->count && d->count < VOICE_MAX_DEVICES ) {
			Q_strncpyz( d->names[d->count++], current, sizeof( d->names[0] ) );
		}
		list->curvalue = i + 1;
	}

	d->items[0] = "system default";
	for ( i = 0; i < d->count; i++ ) {
		Q_strncpyz( d->labels[i], d->names[i], VOICE_NAME_CHARS + 1 );
		if ( strlen( d->names[i] ) > VOICE_NAME_CHARS ) {
			Q_strcat( d->labels[i], sizeof( d->labels[i] ), "..." );
		}
		d->items[i + 1] = d->labels[i];
	}
	d->items[d->count + 1] = NULL;
	list->itemnames = d->items;
	list->numitems = d->count + 1;
}

static void Voice_FindPushToTalkKey( void ) {
	char binding[256];
	int key;

	Q_strncpyz( s_voice.pttKey, "not bound", sizeof( s_voice.pttKey ) );
	for ( key = 0; key < 256; key++ ) {
		trap_Key_GetBindingBuf( key, binding, sizeof( binding ) );
		if ( !Q_stricmp( binding, "+voiprecord" ) ) {
			trap_Key_KeynumToStringBuf( key, s_voice.pttKey, sizeof( s_voice.pttKey ) );
			break;
		}
	}
}

static void Voice_UpdateGrayed( void ) {
	qboolean off = !s_voice.voip.curvalue;
	menucommon_s *items[] = {
		&s_voice.mode.generic, &s_voice.threshold.generic, &s_voice.volume.generic,
		&s_voice.distant.generic,
	};
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( items ); i++ ) {
		if ( off ) {
			items[i]->flags |= QMF_GRAYED;
		} else {
			items[i]->flags &= ~QMF_GRAYED;
		}
	}
	// the threshold only matters when the mic opens itself
	if ( !s_voice.mode.curvalue ) {
		s_voice.threshold.generic.flags |= QMF_GRAYED;
	}
}

static void Voice_SetDevice( voiceDevices_t *d, menulist_s *list, const char *cvar ) {
	trap_Cvar_Set( cvar, list->curvalue ? d->names[list->curvalue - 1] : "" );
}

static void Voice_Close( void ) {
	trap_Cvar_Set( "cl_voipMicTest", "0" );
	UI_PopMenu();
}

static void Voice_Event( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}

	switch ( ( (menucommon_s *)ptr )->id ) {
	case ID_VOIP:
		trap_Cvar_SetValue( "cl_voip", s_voice.voip.curvalue );
		Voice_UpdateGrayed();
		break;

	case ID_MODE:
		trap_Cvar_SetValue( "cl_voipUseVAD", s_voice.mode.curvalue );
		Voice_UpdateGrayed();
		break;

	case ID_THRESHOLD:
		s_voice.threshold.curvalue = (int)( s_voice.threshold.curvalue + 0.5f );
		trap_Cvar_SetValue( "cl_voipVADThreshold", s_voice.threshold.curvalue * THRESHOLD_STEP );
		break;

	case ID_VOLUME:
		s_voice.volume.curvalue = (int)( s_voice.volume.curvalue + 0.5f );
		trap_Cvar_SetValue( "cl_voipVolume", s_voice.volume.curvalue * VOLUME_STEP );
		break;

	case ID_FAR:
		s_voice.distant.curvalue = (int)( s_voice.distant.curvalue + 0.5f );
		trap_Cvar_SetValue( "cl_voipFarVolume", s_voice.distant.curvalue * FAR_STEP );
		break;

	case ID_MIC:
		Voice_SetDevice( &s_voice.mics, &s_voice.mic, "s_sdlCaptureDevice" );
		break;

	case ID_OUTPUT:
		Voice_SetDevice( &s_voice.outputs, &s_voice.output, "s_sdlPlaybackDevice" );
		break;

	case ID_MICTEST:
		trap_Cvar_SetValue( "cl_voipMicTest", s_voice.mictest.curvalue );
		break;

	case ID_BACK:
		Voice_Close();
		break;
	}
}

static sfxHandle_t Voice_Key( int key ) {
	if ( key == K_ESCAPE || key == K_MOUSE2 ) {
		trap_Cvar_Set( "cl_voipMicTest", "0" );
	}
	return Menu_DefaultKey( &s_voice.menu, key );
}

/*
=================
Voice_DrawMeter

The level bar uses a square-root scale so quiet speech still moves it; the
amber tick is where voice activation opens the mic.
=================
*/
static void Voice_DrawMeter( float x, float y, float w ) {
	float level = trap_Cvar_VariableValue( "cl_voipMicLevel" );
	float threshold = s_voice.threshold.curvalue * THRESHOLD_STEP;
	qboolean testing = s_voice.mictest.curvalue != 0;
	qboolean open = !s_voice.mode.curvalue || level >= threshold;
	float fill = sqrt( Com_Clamp( 0, 1, level ) );
	float tick = sqrt( Com_Clamp( 0, 1, threshold ) );
	float h = 8;

	FAB_Label( x - 12, y + 8, 8.5f, FAB_ALIGN_RIGHT, "MIC LEVEL", fabDim, 1 );
	FAB_Fill( x, y, w, h, fabPanelLight, 0.8f );
	if ( testing ) {
		FAB_Fill( x, y, w * fill, h, open ? fabCyan : fabDim, 1 );
	}
	if ( s_voice.mode.curvalue ) {
		FAB_Fill( x + w * tick - 1, y - 3, 2, h + 6, fabAmber, 1 );
	}
	FAB_Label( x, y + 22, 7, FAB_ALIGN_LEFT,
		testing ? ( s_voice.mictest.curvalue == 2 ? "SPEAK: YOU SHOULD HEAR YOURSELF" : "SPEAK TO SEE YOUR LEVEL" )
			: "TURN ON MIC TEST TO CHECK YOUR MICROPHONE", fabDim, 1 );
}

static void Voice_Draw( void ) {
	char hint[128];

	// the engine turns the test off if anything else takes the screen
	if ( s_voice.mictest.curvalue && !trap_Cvar_VariableValue( "cl_voipMicTest" ) ) {
		s_voice.mictest.curvalue = 0;
	}

	Menu_Draw( &s_voice.menu );

	FabUI_Begin();
	Voice_DrawMeter( VOICE_X + 12, s_voice.mictest.generic.y + 30, 150 );

	Com_sprintf( hint, sizeof( hint ), "PUSH TO TALK KEY: %s   (SETTINGS > CONTROLS > MISC)",
		FabUI_Upper( s_voice.pttKey ) );
	FAB_Label( 320, 108, 7.5f, FAB_ALIGN_CENTER, hint, fabDim, 1 );
	FAB_Label( 320, 396, 7, FAB_ALIGN_CENTER,
		"PROXIMITY CHAT: VOICES FADE WITH DISTANCE AND STOP AT CLOSED DOORS", fabDim, 0.8f );
}

static void Voice_InitRow( menucommon_s *g, int type, const char *name, int id, int y ) {
	g->type = type;
	g->name = name;
	g->flags = QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	g->callback = Voice_Event;
	g->id = id;
	g->x = VOICE_X;
	g->y = y;
}

static void Voice_MenuInit( void ) {
	int y;

	memset( &s_voice, 0, sizeof( s_voice ) );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );

	// pick up headsets plugged in since the game started
	if ( trap_Cvar_VariableValue( "s_initsound" ) ) {
		trap_Cmd_ExecuteText( EXEC_NOW, "s_devices\n" );
	}


	s_voice.menu.wrapAround = qtrue;
	s_voice.menu.fullscreen = qtrue;
	s_voice.menu.draw = Voice_Draw;
	s_voice.menu.key = Voice_Key;

	s_voice.banner.generic.type = MTYPE_BTEXT;
	s_voice.banner.generic.x = 320;
	s_voice.banner.generic.y = 16;
	s_voice.banner.string = "VOICE";
	s_voice.banner.color = color_white;
	s_voice.banner.style = UI_CENTER;

	y = 132;
	Voice_InitRow( &s_voice.voip.generic, MTYPE_RADIOBUTTON, "Voice Chat:", ID_VOIP, y );
	y += BIGCHAR_HEIGHT + 4;
	Voice_InitRow( &s_voice.mode.generic, MTYPE_SPINCONTROL, "Talk Mode:", ID_MODE, y );
	s_voice.mode.itemnames = voice_modes;
	y += BIGCHAR_HEIGHT + 4;
	Voice_InitRow( &s_voice.threshold.generic, MTYPE_SLIDER, "Activation Level:", ID_THRESHOLD, y );
	s_voice.threshold.minvalue = 1;
	s_voice.threshold.maxvalue = 20;
	y += BIGCHAR_HEIGHT + 4;
	Voice_InitRow( &s_voice.volume.generic, MTYPE_SLIDER, "Voice Volume:", ID_VOLUME, y );
	s_voice.volume.minvalue = 0;
	s_voice.volume.maxvalue = 20;
	y += BIGCHAR_HEIGHT + 4;
	Voice_InitRow( &s_voice.distant.generic, MTYPE_SLIDER, "Distant Voices:", ID_FAR, y );
	s_voice.distant.minvalue = 0;
	s_voice.distant.maxvalue = 10;
	y += BIGCHAR_HEIGHT + 12;
	Voice_InitRow( &s_voice.mic.generic, MTYPE_SPINCONTROL, "Microphone:", ID_MIC, y );
	y += BIGCHAR_HEIGHT + 4;
	Voice_InitRow( &s_voice.output.generic, MTYPE_SPINCONTROL, "Speakers:", ID_OUTPUT, y );
	y += BIGCHAR_HEIGHT + 12;
	Voice_InitRow( &s_voice.mictest.generic, MTYPE_SPINCONTROL, "Mic Test:", ID_MICTEST, y );
	s_voice.mictest.itemnames = voice_tests;

	s_voice.back.generic.type = MTYPE_BITMAP;
	s_voice.back.generic.name = ART_BACK0;
	s_voice.back.generic.flags = QMF_LEFT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_voice.back.generic.callback = Voice_Event;
	s_voice.back.generic.id = ID_BACK;
	s_voice.back.generic.x = 0;
	s_voice.back.generic.y = 480 - 64;
	s_voice.back.width = 128;
	s_voice.back.height = 64;
	s_voice.back.focuspic = ART_BACK1;

	s_voice.voip.curvalue = trap_Cvar_VariableValue( "cl_voip" ) != 0;
	s_voice.mode.curvalue = trap_Cvar_VariableValue( "cl_voipUseVAD" ) != 0;
	s_voice.threshold.curvalue = Com_Clamp( 1, 20,
		(int)( trap_Cvar_VariableValue( "cl_voipVADThreshold" ) / THRESHOLD_STEP + 0.5f ) );
	s_voice.volume.curvalue = Com_Clamp( 0, 20,
		(int)( trap_Cvar_VariableValue( "cl_voipVolume" ) / VOLUME_STEP + 0.5f ) );
	s_voice.distant.curvalue = Com_Clamp( 0, 10,
		(int)( trap_Cvar_VariableValue( "cl_voipFarVolume" ) / FAR_STEP + 0.5f ) );

	s_voice.mictest.curvalue = Com_Clamp( 0, 2, trap_Cvar_VariableValue( "cl_voipMicTest" ) );
	Voice_LoadDevices( &s_voice.mics, &s_voice.mic, "s_sdlCaptureDevices", "s_sdlCaptureDevice" );
	Voice_LoadDevices( &s_voice.outputs, &s_voice.output, "s_sdlPlaybackDevices", "s_sdlPlaybackDevice" );
	Voice_FindPushToTalkKey();

	Menu_AddItem( &s_voice.menu, &s_voice.banner );
	Menu_AddItem( &s_voice.menu, &s_voice.voip );
	Menu_AddItem( &s_voice.menu, &s_voice.mode );
	Menu_AddItem( &s_voice.menu, &s_voice.threshold );
	Menu_AddItem( &s_voice.menu, &s_voice.volume );
	Menu_AddItem( &s_voice.menu, &s_voice.distant );
	Menu_AddItem( &s_voice.menu, &s_voice.mic );
	Menu_AddItem( &s_voice.menu, &s_voice.output );
	Menu_AddItem( &s_voice.menu, &s_voice.mictest );
	Menu_AddItem( &s_voice.menu, &s_voice.back );

	Voice_UpdateGrayed();
}

/*
=================
UI_VoiceMenu
=================
*/
void UI_VoiceMenu( void ) {
	Voice_MenuInit();
	UI_PushMenu( &s_voice.menu );
}
