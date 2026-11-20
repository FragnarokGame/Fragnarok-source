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

#include <stdlib.h>
#include <stdio.h>

#ifdef USE_INTERNAL_SDL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include "../qcommon/q_shared.h"
#include "../client/snd_local.h"
#include "../client/client.h"

qboolean snd_inited = qfalse;

cvar_t *s_sdlBits;
cvar_t *s_sdlSpeed;
cvar_t *s_sdlChannels;
cvar_t *s_sdlDevSamps;
cvar_t *s_sdlMixSamps;

/* The audio callback. All the magic happens here. */
static int dmapos = 0;
static int dmasize = 0;

static SDL_AudioDeviceID sdlPlaybackDevice;

// Device names are picked in the Voice settings screen. Empty means the
// system default. The available names are published as newline-separated
// read-only cvars, the same way the OpenAL backend lists its devices.
static cvar_t *s_sdlPlaybackDevice;
static cvar_t *s_sdlPlaybackDevices;
static SDL_AudioSpec sdlPlaybackSpec;

#if defined USE_VOIP && SDL_VERSION_ATLEAST( 2, 0, 5 )
#define USE_SDL_AUDIO_CAPTURE

static SDL_AudioDeviceID sdlCaptureDevice;
static qboolean sdlCaptureRunning;
static cvar_t *s_sdlCapture;
static cvar_t *s_sdlCaptureDevice;
static cvar_t *s_sdlCaptureDevices;
static float sdlMasterGain = 1.0f;
#endif


/*
===============
SNDDMA_AudioCallback
===============
*/
static void SNDDMA_AudioCallback(void *userdata, Uint8 *stream, int len)
{
	int pos = (dmapos * (dma.samplebits/8));
	if (pos >= dmasize)
		dmapos = pos = 0;

	if (!snd_inited)  /* shouldn't happen, but just in case... */
	{
		memset(stream, '\0', len);
		return;
	}
	else
	{
		int tobufend = dmasize - pos;  /* bytes to buffer's end. */
		int len1 = len;
		int len2 = 0;

		if (len1 > tobufend)
		{
			len1 = tobufend;
			len2 = len - len1;
		}
		memcpy(stream, dma.buffer + pos, len1);
		// Consume mixed samples once. If the game stalls during a window
		// transition, the callback must play silence instead of looping them.
		memset(dma.buffer + pos, dma.samplebits == 8 ? 0x80 : 0, len1);
		if (len2 <= 0)
			dmapos += (len1 / (dma.samplebits/8));
		else  /* wraparound? */
		{
			memcpy(stream+len1, dma.buffer, len2);
			memset(dma.buffer, dma.samplebits == 8 ? 0x80 : 0, len2);
			dmapos = (len2 / (dma.samplebits/8));
		}
	}

	if (dmapos >= dmasize)
		dmapos = 0;

#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlMasterGain != 1.0f)
	{
		int i;
		if (dma.isfloat && (dma.samplebits == 32))
		{
			float *ptr = (float *) stream;
			len /= sizeof (*ptr);
			for (i = 0; i < len; i++, ptr++)
			{
				*ptr *= sdlMasterGain;
			}
		}
		else if (dma.samplebits == 16)
		{
			Sint16 *ptr = (Sint16 *) stream;
			len /= sizeof (*ptr);
			for (i = 0; i < len; i++, ptr++)
			{
				*ptr = (Sint16) (((float) *ptr) * sdlMasterGain);
			}
		}
		else if (dma.samplebits == 8)
		{
			Uint8 *ptr = (Uint8 *) stream;
			len /= sizeof (*ptr);
			for (i = 0; i < len; i++, ptr++)
			{
				*ptr = (Uint8) (((float) *ptr) * sdlMasterGain);
			}
		}
	}
#endif
}

static struct
{
	Uint16	enumFormat;
	char		*stringFormat;
} formatToStringTable[ ] =
{
	{ AUDIO_U8,     "AUDIO_U8" },
	{ AUDIO_S8,     "AUDIO_S8" },
	{ AUDIO_U16LSB, "AUDIO_U16LSB" },
	{ AUDIO_S16LSB, "AUDIO_S16LSB" },
	{ AUDIO_U16MSB, "AUDIO_U16MSB" },
	{ AUDIO_S16MSB, "AUDIO_S16MSB" },
	{ AUDIO_F32LSB, "AUDIO_F32LSB" },
	{ AUDIO_F32MSB, "AUDIO_F32MSB" }
};

static int formatToStringTableSize = ARRAY_LEN( formatToStringTable );

/*
===============
SNDDMA_PrintAudiospec
===============
*/
static void SNDDMA_PrintAudiospec(const char *str, const SDL_AudioSpec *spec)
{
	int		i;
	char	*fmt = NULL;

	Com_Printf("%s:\n", str);

	for( i = 0; i < formatToStringTableSize; i++ ) {
		if( spec->format == formatToStringTable[ i ].enumFormat ) {
			fmt = formatToStringTable[ i ].stringFormat;
		}
	}

	if( fmt ) {
		Com_Printf( "  Format:   %s\n", fmt );
	} else {
		Com_Printf( "  Format:   " S_COLOR_RED "UNKNOWN\n");
	}

	Com_Printf( "  Freq:     %d\n", (int) spec->freq );
	Com_Printf( "  Samples:  %d\n", (int) spec->samples );
	Com_Printf( "  Channels: %d\n", (int) spec->channels );
}

/*
===============
SNDDMA_ListDevices

Publishes the device names SDL can see into a read-only cvar, one per line.
Names that a cvar could not store are left out, since they could not be
picked anyway.
===============
*/
static void SNDDMA_ListDevices( qboolean capture, cvar_t *list )
{
	char names[4096];
	int count = SDL_GetNumAudioDevices( capture ? SDL_TRUE : SDL_FALSE );
	int i;

	names[0] = '\0';
	for ( i = 0; i < count; i++ )
	{
		const char *name = SDL_GetAudioDeviceName( i, capture ? SDL_TRUE : SDL_FALSE );

		if ( !name || !name[0] || strpbrk( name, "\\\";\n" ) )
			continue;
		if ( names[0] )
			Q_strcat( names, sizeof( names ), "\n" );
		Q_strcat( names, sizeof( names ), name );
	}
	Cvar_Set2( list->name, names, qtrue );
}

/*
===============
SNDDMA_Devices_f

"s_devices" refreshes the device lists (for headsets plugged in after
startup) and prints them.
===============
*/
static void SNDDMA_Devices_f( void )
{
	if ( !snd_inited )
	{
		Com_Printf( "Sound is not running.\n" );
		return;
	}

	SNDDMA_ListDevices( qfalse, s_sdlPlaybackDevices );
	Com_Printf( "Playback devices (s_sdlPlaybackDevice, current \"%s\"):\n%s\n",
		s_sdlPlaybackDevice->string, s_sdlPlaybackDevices->string );
#ifdef USE_SDL_AUDIO_CAPTURE
	SNDDMA_ListDevices( qtrue, s_sdlCaptureDevices );
	Com_Printf( "Capture devices (s_sdlCaptureDevice, current \"%s\"):\n%s\n",
		s_sdlCaptureDevice->string, s_sdlCaptureDevices->string );
#endif
}

/*
===============
SNDDMA_SwitchPlayback

Moves the mixer to the device named by s_sdlPlaybackDevice without a
snd_restart. The new device is asked for exactly the format the mixer
already uses, and SDL converts if the hardware differs, so dma stays valid.
===============
*/
static void SNDDMA_SwitchPlayback( void )
{
	const char *name = s_sdlPlaybackDevice->string[0] ? s_sdlPlaybackDevice->string : NULL;
	SDL_AudioDeviceID device;

	s_sdlPlaybackDevice->modified = qfalse;

	// Close first so two callbacks never read the mix buffer at once.
	if ( sdlPlaybackDevice )
		SDL_CloseAudioDevice( sdlPlaybackDevice );

	device = SDL_OpenAudioDevice( name, SDL_FALSE, &sdlPlaybackSpec, NULL, 0 );
	if ( !device && name )
	{
		Com_Printf( "Couldn't open playback device \"%s\" (%s), using the default.\n", name, SDL_GetError() );
		device = SDL_OpenAudioDevice( NULL, SDL_FALSE, &sdlPlaybackSpec, NULL, 0 );
	}
	sdlPlaybackDevice = device;
	if ( device )
	{
		Com_Printf( "SDL playback device: %s\n", name ? name : "system default" );
		SDL_PauseAudioDevice( device, 0 );
	}
	else
	{
		Com_Printf( "SDL_OpenAudioDevice() failed: %s\n", SDL_GetError() );
	}
}

#ifdef USE_SDL_AUDIO_CAPTURE
/*
===============
SNDDMA_OpenCapture

Opens the microphone named by s_sdlCaptureDevice, falling back to the
system default. Capture keeps running if it was running before.
===============
*/
static void SNDDMA_OpenCapture( void )
{
	const char *name = s_sdlCaptureDevice->string[0] ? s_sdlCaptureDevice->string : NULL;
	SDL_AudioSpec spec;

	s_sdlCaptureDevice->modified = qfalse;

	if ( sdlCaptureDevice )
	{
		SDL_CloseAudioDevice( sdlCaptureDevice );
		sdlCaptureDevice = 0;
	}

	SDL_zero( spec );
	spec.freq = 48000;
	spec.format = AUDIO_S16SYS;
	spec.channels = 1;
	spec.samples = VOIP_MAX_PACKET_SAMPLES * 4;
	sdlCaptureDevice = SDL_OpenAudioDevice( name, SDL_TRUE, &spec, NULL, 0 );
	if ( !sdlCaptureDevice && name )
	{
		Com_Printf( "Couldn't open capture device \"%s\" (%s), using the default.\n", name, SDL_GetError() );
		sdlCaptureDevice = SDL_OpenAudioDevice( NULL, SDL_TRUE, &spec, NULL, 0 );
	}
	if ( sdlCaptureDevice )
	{
		Com_Printf( "SDL capture device opened: %s\n", name ? name : "system default" );
		if ( sdlCaptureRunning )
			SDL_PauseAudioDevice( sdlCaptureDevice, 0 );
	}
	else
	{
		Com_Printf( "SDL capture device failed to open (%s).\n", SDL_GetError() );
	}
}

static qboolean SNDDMA_CaptureAllowed( void )
{
	if ( !s_sdlCapture->integer )
		return qfalse;
#if USE_MUMBLE
	if ( cl_useMumble->integer )
		return qfalse;
#endif
	return qtrue;
}
#endif

/*
===============
SNDDMA_CheckDevices

Applies device choices made since the last frame.
===============
*/
static void SNDDMA_CheckDevices( void )
{
	if ( !snd_inited )
		return;
	if ( s_sdlPlaybackDevice->modified )
		SNDDMA_SwitchPlayback();
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( s_sdlCaptureDevice->modified )
	{
		if ( SNDDMA_CaptureAllowed() )
			SNDDMA_OpenCapture();
		else
			s_sdlCaptureDevice->modified = qfalse;
	}
#endif
}

/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	int tmp;

	if (snd_inited)
		return qtrue;

	if (!s_sdlBits) {
		s_sdlBits = Cvar_Get("s_sdlBits", "16", CVAR_ARCHIVE);
		s_sdlSpeed = Cvar_Get("s_sdlSpeed", "0", CVAR_ARCHIVE);
		s_sdlChannels = Cvar_Get("s_sdlChannels", "2", CVAR_ARCHIVE);
		s_sdlDevSamps = Cvar_Get("s_sdlDevSamps", "0", CVAR_ARCHIVE);
		s_sdlMixSamps = Cvar_Get("s_sdlMixSamps", "0", CVAR_ARCHIVE);
		s_sdlPlaybackDevice = Cvar_Get("s_sdlPlaybackDevice", "", CVAR_ARCHIVE);
		s_sdlPlaybackDevices = Cvar_Get("s_sdlPlaybackDevices", "", CVAR_ROM | CVAR_NORESTART);
#ifdef USE_SDL_AUDIO_CAPTURE
		s_sdlCaptureDevice = Cvar_Get("s_sdlCaptureDevice", "", CVAR_ARCHIVE);
		s_sdlCaptureDevices = Cvar_Get("s_sdlCaptureDevices", "", CVAR_ROM | CVAR_NORESTART);
#endif
		Cmd_AddCommand("s_devices", SNDDMA_Devices_f);
	}

	Com_Printf( "SDL_Init( SDL_INIT_AUDIO )... " );

	if (SDL_Init(SDL_INIT_AUDIO) != 0)
	{
		Com_Printf( "FAILED (%s)\n", SDL_GetError( ) );
		return qfalse;
	}

	Com_Printf( "OK\n" );

	Com_Printf( "SDL audio driver is \"%s\".\n", SDL_GetCurrentAudioDriver( ) );

	memset(&desired, '\0', sizeof (desired));
	memset(&obtained, '\0', sizeof (obtained));

	tmp = ((int) s_sdlBits->value);
	if ((tmp != 16) && (tmp != 8))
		tmp = 16;

	desired.freq = (int) s_sdlSpeed->value;
	if(!desired.freq) desired.freq = 22050;
	desired.format = ((tmp == 16) ? AUDIO_S16SYS : AUDIO_U8);

	// I dunno if this is the best idea, but I'll give it a try...
	//  should probably check a cvar for this...
	if (s_sdlDevSamps->value)
		desired.samples = s_sdlDevSamps->value;
	else
	{
		// just pick a sane default.
		if (desired.freq <= 11025)
			desired.samples = 256;
		else if (desired.freq <= 22050)
			desired.samples = 512;
		else if (desired.freq <= 44100)
			desired.samples = 1024;
		else
			desired.samples = 2048;  // (*shrug*)
	}

	desired.channels = (int) s_sdlChannels->value;
	desired.callback = SNDDMA_AudioCallback;

	SNDDMA_ListDevices(qfalse, s_sdlPlaybackDevices);
	s_sdlPlaybackDevice->modified = qfalse;
	sdlPlaybackDevice = 0;
	if (s_sdlPlaybackDevice->string[0])
	{
		sdlPlaybackDevice = SDL_OpenAudioDevice(s_sdlPlaybackDevice->string, SDL_FALSE, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
		if (sdlPlaybackDevice == 0)
			Com_Printf("Couldn't open playback device \"%s\" (%s), using the default.\n", s_sdlPlaybackDevice->string, SDL_GetError());
		else
			Com_Printf("SDL playback device: %s\n", s_sdlPlaybackDevice->string);
	}
	if (sdlPlaybackDevice == 0)
		sdlPlaybackDevice = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (sdlPlaybackDevice == 0)
	{
		Com_Printf("SDL_OpenAudioDevice() failed: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return qfalse;
	}

	SNDDMA_PrintAudiospec("SDL_AudioSpec", &obtained);

	// Later device switches ask for exactly this format.
	sdlPlaybackSpec = obtained;
	sdlPlaybackSpec.callback = SNDDMA_AudioCallback;
	sdlPlaybackSpec.userdata = NULL;

	// dma.samples needs to be big, or id's mixer will just refuse to
	//  work at all; we need to keep it significantly bigger than the
	//  amount of SDL callback samples, and just copy a little each time
	//  the callback runs.
	// 32768 is what the OSS driver filled in here on my system. I don't
	//  know if it's a good value overall, but at least we know it's
	//  reasonable...this is why I let the user override.
	tmp = s_sdlMixSamps->value;
	if (!tmp)
		tmp = (obtained.samples * obtained.channels) * 10;

	// samples must be divisible by number of channels
	tmp -= tmp % obtained.channels;

	dmapos = 0;
	dma.samplebits = SDL_AUDIO_BITSIZE(obtained.format);
	dma.isfloat = SDL_AUDIO_ISFLOAT(obtained.format);
	dma.channels = obtained.channels;
	dma.samples = tmp;
	dma.fullsamples = dma.samples / dma.channels;
	dma.submission_chunk = 1;
	dma.speed = obtained.freq;
	dmasize = (dma.samples * (dma.samplebits/8));
	dma.buffer = calloc(1, dmasize);

#ifdef USE_SDL_AUDIO_CAPTURE
	s_sdlCapture = Cvar_Get( "s_sdlCapture", "1", CVAR_ARCHIVE | CVAR_LATCH );
	SNDDMA_ListDevices(qtrue, s_sdlCaptureDevices);
	sdlCaptureRunning = qfalse;
	s_sdlCaptureDevice->modified = qfalse;
	if (!s_sdlCapture->integer)
	{
		Com_Printf("SDL audio capture support disabled by user ('+set s_sdlCapture 1' to enable)\n");
	}
#if USE_MUMBLE
	else if (cl_useMumble->integer)
	{
		Com_Printf("SDL audio capture support disabled for Mumble support\n");
	}
#endif
	else
	{
		SNDDMA_OpenCapture();
	}

	sdlMasterGain = 1.0f;
#endif

	Com_Printf("Starting SDL audio callback...\n");
	SDL_PauseAudioDevice(sdlPlaybackDevice, 0);  // start callback.
	// don't unpause the capture device; we'll do that in StartCapture.

	Com_Printf("SDL audio initialized.\n");
	snd_inited = qtrue;
	return qtrue;
}

/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos(void)
{
	return dmapos;
}

/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown(void)
{
	if (sdlPlaybackDevice != 0)
	{
		Com_Printf("Closing SDL audio playback device...\n");
		SDL_CloseAudioDevice(sdlPlaybackDevice);
		Com_Printf("SDL audio playback device closed.\n");
		sdlPlaybackDevice = 0;
	}

#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlCaptureDevice)
	{
		Com_Printf("Closing SDL audio capture device...\n");
		SDL_CloseAudioDevice(sdlCaptureDevice);
		Com_Printf("SDL audio capture device closed.\n");
		sdlCaptureDevice = 0;
	}
#endif

	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	free(dma.buffer);
	dma.buffer = NULL;
	dmapos = dmasize = 0;
	snd_inited = qfalse;
	Com_Printf("SDL audio shut down.\n");
}

/*
===============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit(void)
{
	SDL_UnlockAudioDevice(sdlPlaybackDevice);
}

/*
===============
SNDDMA_BeginPainting
===============
*/
void SNDDMA_BeginPainting (void)
{
	SNDDMA_CheckDevices();
	SDL_LockAudioDevice(sdlPlaybackDevice);
}


#ifdef USE_VOIP
void SNDDMA_StartCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	SNDDMA_CheckDevices();
	sdlCaptureRunning = qtrue;
	if (sdlCaptureDevice)
	{
		SDL_ClearQueuedAudio(sdlCaptureDevice);
		SDL_PauseAudioDevice(sdlCaptureDevice, 0);
	}
#endif
}

int SNDDMA_AvailableCaptureSamples(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	// divided by 2 to convert from bytes to (mono16) samples.
	return sdlCaptureDevice ? (SDL_GetQueuedAudioSize(sdlCaptureDevice) / 2) : 0;
#else
	return 0;
#endif
}

void SNDDMA_Capture(int samples, byte *data)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	// multiplied by 2 to convert from (mono16) samples to bytes.
	if (sdlCaptureDevice)
	{
		SDL_DequeueAudio(sdlCaptureDevice, data, samples * 2);
	}
	else
#endif
	{
		SDL_memset(data, '\0', samples * 2);
	}
}

void SNDDMA_StopCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	sdlCaptureRunning = qfalse;
	if (sdlCaptureDevice)
	{
		SDL_PauseAudioDevice(sdlCaptureDevice, 1);
	}
#endif
}

void SNDDMA_MasterGain( float val )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	sdlMasterGain = val;
#endif
}
#endif

