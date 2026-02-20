/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/* Allow access to a raw mixing buffer */

#include <sys/types.h>
#include <signal.h>	/* For kill() */

#include "SDL_timer.h"
#include "SDL_audio.h"
#include "../SDL_audiomem.h"
#include "../SDL_audio_c.h"
#include "SDL_alsa_audio.h"

#ifdef SDL_AUDIO_DRIVER_ALSA_DYNAMIC
#include "SDL_name.h"
#include "SDL_loadso.h"
#else
#define SDL_NAME(X)	X
#endif

#define LOG_CONSOLE

/* The tag name used by ALSA audio */
#define DRIVER_NAME         "alsa"

/* Audio driver functions */
static int ALSA_OpenAudio(_THIS, SDL_AudioSpec *spec);
static void ALSA_WaitAudio(_THIS);
static void ALSA_PlayAudio(_THIS);
static Uint8 *ALSA_GetAudioBuf(_THIS);
static void ALSA_CloseAudio(_THIS);

#ifdef SDL_AUDIO_DRIVER_ALSA_DYNAMIC

static const char *alsa_library = SDL_AUDIO_DRIVER_ALSA_DYNAMIC;
static void *alsa_handle = NULL;
static int alsa_loaded = 0;
static int32_t *mixbuf32;

static int (*SDL_NAME(snd_pcm_open))(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
static int (*SDL_NAME(snd_pcm_close))(snd_pcm_t *pcm);
static int (*SDL_NAME(snd_pcm_drop))(snd_pcm_t *pcm);
static snd_pcm_sframes_t (*SDL_NAME(snd_pcm_writei))(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
static int (*SDL_NAME(snd_pcm_resume))(snd_pcm_t *pcm);
static int (*SDL_NAME(snd_pcm_prepare))(snd_pcm_t *pcm);
static const char *(*SDL_NAME(snd_strerror))(int errnum);
static size_t (*SDL_NAME(snd_pcm_hw_params_sizeof))(void);
static size_t (*SDL_NAME(snd_pcm_sw_params_sizeof))(void);
static void (*SDL_NAME(snd_pcm_hw_params_copy))(snd_pcm_hw_params_t *dst, const snd_pcm_hw_params_t *src);
static int (*SDL_NAME(snd_pcm_hw_params_any))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*SDL_NAME(snd_pcm_hw_params_set_access))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t access);
static int (*SDL_NAME(snd_pcm_hw_params_set_format))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val);
static int (*SDL_NAME(snd_pcm_hw_params_set_channels))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
static int (*SDL_NAME(snd_pcm_hw_params_get_channels))(const snd_pcm_hw_params_t *params, unsigned int *val);
static int (*SDL_NAME(snd_pcm_hw_params_set_rate_near))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*SDL_NAME(snd_pcm_hw_params_set_rate_resample))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val);
static int (*SDL_NAME(snd_pcm_hw_params_set_period_size_near))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val, int *dir);
static int (*SDL_NAME(snd_pcm_hw_params_set_period_size))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t val, int dir);
static int (*SDL_NAME(snd_pcm_hw_params_get_period_size))(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir);
static int (*SDL_NAME(snd_pcm_hw_params_set_periods_near))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*SDL_NAME(snd_pcm_hw_params_set_periods))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir);
static int (*SDL_NAME(snd_pcm_hw_params_get_periods))(const snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*SDL_NAME(snd_pcm_hw_params_set_buffer_size_near))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val);
static int (*SDL_NAME(snd_pcm_hw_params_get_buffer_size))(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val);
static int (*SDL_NAME(snd_pcm_hw_params))(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
/*
*/
static int (*SDL_NAME(snd_pcm_sw_params_set_avail_min))(snd_pcm_t *pcm, snd_pcm_sw_params_t *swparams, snd_pcm_uframes_t val);
static int (*SDL_NAME(snd_pcm_sw_params_current))(snd_pcm_t *pcm, snd_pcm_sw_params_t *swparams);
static int (*SDL_NAME(snd_pcm_sw_params_set_start_threshold))(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*SDL_NAME(snd_pcm_sw_params))(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*SDL_NAME(snd_pcm_nonblock))(snd_pcm_t *pcm, int nonblock);
#define snd_pcm_hw_params_sizeof SDL_NAME(snd_pcm_hw_params_sizeof)
#define snd_pcm_sw_params_sizeof SDL_NAME(snd_pcm_sw_params_sizeof)


/* cast funcs to char* first, to please GCC's strict aliasing rules. */
static struct {
	const char *name;
	void **func;
} alsa_functions[] = {
	{ "snd_pcm_open",	(void**)(char*)&SDL_NAME(snd_pcm_open)		},
	{ "snd_pcm_close",	(void**)(char*)&SDL_NAME(snd_pcm_close)	},
	{ "snd_pcm_drop",	(void**)(char*)&SDL_NAME(snd_pcm_drop)	},
	{ "snd_pcm_writei",	(void**)(char*)&SDL_NAME(snd_pcm_writei)	},
	{ "snd_pcm_resume",	(void**)(char*)&SDL_NAME(snd_pcm_resume)	},
	{ "snd_pcm_prepare",	(void**)(char*)&SDL_NAME(snd_pcm_prepare)	},
	{ "snd_strerror",	(void**)(char*)&SDL_NAME(snd_strerror)		},
	{ "snd_pcm_hw_params_sizeof",		(void**)(char*)&SDL_NAME(snd_pcm_hw_params_sizeof)		},
	{ "snd_pcm_sw_params_sizeof",		(void**)(char*)&SDL_NAME(snd_pcm_sw_params_sizeof)		},
	{ "snd_pcm_hw_params_copy",		(void**)(char*)&SDL_NAME(snd_pcm_hw_params_copy)		},
	{ "snd_pcm_hw_params_any",		(void**)(char*)&SDL_NAME(snd_pcm_hw_params_any)		},
	{ "snd_pcm_hw_params_set_access",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_access)		},
	{ "snd_pcm_hw_params_set_format",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_format)		},
	{ "snd_pcm_hw_params_set_channels",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_channels)	},
	{ "snd_pcm_hw_params_get_channels",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_get_channels)	},
	{ "snd_pcm_hw_params_set_rate_near",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_rate_near)	},
	{ "snd_pcm_hw_params_set_rate_resample",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_rate_resample)	},
	{ "snd_pcm_hw_params_set_period_size_near",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_period_size_near)	},
	{ "snd_pcm_hw_params_set_period_size",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_period_size)	},
	{ "snd_pcm_hw_params_get_period_size",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_get_period_size)	},
	{ "snd_pcm_hw_params_set_periods_near",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_periods_near)	},
	{ "snd_pcm_hw_params_set_periods",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_periods)	},
	{ "snd_pcm_hw_params_get_periods",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_get_periods)	},
	{ "snd_pcm_hw_params_set_buffer_size_near",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_set_buffer_size_near) },
	{ "snd_pcm_hw_params_get_buffer_size",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params_get_buffer_size) },
	{ "snd_pcm_hw_params",	(void**)(char*)&SDL_NAME(snd_pcm_hw_params)	},
	{ "snd_pcm_sw_params_set_avail_min",	(void**)(char*)&SDL_NAME(snd_pcm_sw_params_set_avail_min) },
	{ "snd_pcm_sw_params_current",	(void**)(char*)&SDL_NAME(snd_pcm_sw_params_current)	},
	{ "snd_pcm_sw_params_set_start_threshold",	(void**)(char*)&SDL_NAME(snd_pcm_sw_params_set_start_threshold)	},
	{ "snd_pcm_sw_params",	(void**)(char*)&SDL_NAME(snd_pcm_sw_params)	},
	{ "snd_pcm_nonblock",	(void**)(char*)&SDL_NAME(snd_pcm_nonblock)	},
};

#define SOFT_CLIP_THRESHOLD 0.95f

//high pass filter
static float prev_in[2]  = {0.0f, 0.0f};
static float prev_out[2] = {0.0f, 0.0f};

static inline float soft_clip(float x)
{
   if (x > 1.0f) return 1.0f;
   if (x < -1.0f) return -1.0f;
   
   if (x > SOFT_CLIP_THRESHOLD || x < -SOFT_CLIP_THRESHOLD)
   {
      float threshold = SOFT_CLIP_THRESHOLD;
      
      if (x > threshold)
         return threshold + (x - threshold) * 0.5f;
      else
         return -threshold + (x + threshold) * 0.5f;
   }
   
   return x;
}

static void UnloadALSALibrary(void) {
	if (alsa_loaded) {
		SDL_UnloadObject(alsa_handle);
		alsa_handle = NULL;
		alsa_loaded = 0;
	}
}

static int LoadALSALibrary(void) {
	int i, retval = -1;

	alsa_handle = SDL_LoadObject(alsa_library);
	if (alsa_handle) {
		alsa_loaded = 1;
		retval = 0;
		for (i = 0; i < SDL_arraysize(alsa_functions); i++) {
			*alsa_functions[i].func = SDL_LoadFunction(alsa_handle,alsa_functions[i].name);
			if (!*alsa_functions[i].func) {
				retval = -1;
				UnloadALSALibrary();
				break;
			}
		}
	}
	return retval;
}

#else

static void UnloadALSALibrary(void) {
	return;
}

static int LoadALSALibrary(void) {
	return 0;
}

#endif /* SDL_AUDIO_DRIVER_ALSA_DYNAMIC */

static const char *get_audio_device(int channels)
{
	const char *device;
	
	device = SDL_getenv("AUDIODEV");	/* Is there a standard variable name? */
	if ( device == NULL ) {
		switch (channels) {
		case 6:
			device = "plug:surround51";
			break;
		case 4:
			device = "plug:surround40";
			break;
		default:
			device = "default";
			break;
		}
	}
	return device;
}

/* Audio driver bootstrap functions */

static int Audio_Available(void)
{
	int available;
	int status;
	snd_pcm_t *handle;

	available = 0;
	if (LoadALSALibrary() < 0) {
		return available;
	}
	status = SDL_NAME(snd_pcm_open)(&handle, get_audio_device(2), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if ( status >= 0 ) {
		available = 1;
        	SDL_NAME(snd_pcm_close)(handle);
	}
	UnloadALSALibrary();
	return(available);
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
	UnloadALSALibrary();
}

static SDL_AudioDevice *Audio_CreateDevice(int devindex)
{
	SDL_AudioDevice *this;

	/* Initialize all variables that we clean on shutdown */
	LoadALSALibrary();
	this = (SDL_AudioDevice *)SDL_malloc(sizeof(SDL_AudioDevice));
	if ( this ) {
		SDL_memset(this, 0, (sizeof *this));
		this->hidden = (struct SDL_PrivateAudioData *)
				SDL_malloc((sizeof *this->hidden));
	}
	if ( (this == NULL) || (this->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( this ) {
			SDL_free(this);
		}
		return(0);
	}
	SDL_memset(this->hidden, 0, (sizeof *this->hidden));

	/* Set the function pointers */
	this->OpenAudio = ALSA_OpenAudio;
	this->WaitAudio = ALSA_WaitAudio;
	this->PlayAudio = ALSA_PlayAudio;
	this->GetAudioBuf = ALSA_GetAudioBuf;
	this->CloseAudio = ALSA_CloseAudio;

	this->free = Audio_DeleteDevice;

	return this;
}

AudioBootStrap ALSA_bootstrap = {
	DRIVER_NAME, "ALSA PCM audio",
	Audio_Available, Audio_CreateDevice
};

/* This function waits until it is possible to write a full sound buffer */
static void ALSA_WaitAudio(_THIS)
{
	/* We're in blocking mode, so there's nothing to do here */
}


/*
 * http://bugzilla.libsdl.org/show_bug.cgi?id=110
 * "For Linux ALSA, this is FL-FR-RL-RR-C-LFE
 *  and for Windows DirectX [and CoreAudio], this is FL-FR-C-LFE-RL-RR"
 */
#define SWIZ6(T) \
    T *ptr = (T *) mixbuf; \
    Uint32 i; \
    for (i = 0; i < this->spec.samples; i++, ptr += 6) { \
        T tmp; \
        tmp = ptr[2]; ptr[2] = ptr[4]; ptr[4] = tmp; \
        tmp = ptr[3]; ptr[3] = ptr[5]; ptr[5] = tmp; \
    }

static __inline__ void swizzle_alsa_channels_6_64bit(_THIS) { SWIZ6(Uint64); }
static __inline__ void swizzle_alsa_channels_6_32bit(_THIS) { SWIZ6(Uint32); }
static __inline__ void swizzle_alsa_channels_6_16bit(_THIS) { SWIZ6(Uint16); }
static __inline__ void swizzle_alsa_channels_6_8bit(_THIS) { SWIZ6(Uint8); }

#undef SWIZ6


/*
 * Called right before feeding this->mixbuf to the hardware. Swizzle channels
 *  from Windows/Mac order to the format alsalib will want.
 */
static __inline__ void swizzle_alsa_channels(_THIS)
{
    if (this->spec.channels == 6) {
        const Uint16 fmtsize = (this->spec.format & 0xFF); /* bits/channel. */
        if (fmtsize == 16)
            swizzle_alsa_channels_6_16bit(this);
        else if (fmtsize == 8)
            swizzle_alsa_channels_6_8bit(this);
        else if (fmtsize == 32)
            swizzle_alsa_channels_6_32bit(this);
        else if (fmtsize == 64)
            swizzle_alsa_channels_6_64bit(this);
    }

    /* !!! FIXME: update this for 7.1 if needed, later. */
}


/* snd_pcm_recover() is available in alsa-lib >= 1.0.11 */
static int ALSA_pcm_recover(snd_pcm_t *handle, int err, int silent)
{
	(void) silent;
	if (err == -EINTR) return 0;
	if (err == -EPIPE) {		/* under-run */
		err = SDL_NAME(snd_pcm_prepare)(handle);
		return (err < 0)? err : 0;
	}
	if (err == -ESTRPIPE) {
		/* wait until suspend flag is released */
		while ((err = SDL_NAME(snd_pcm_resume)(handle)) == -EAGAIN)
			SDL_Delay(100);
		if (err < 0) err = SDL_NAME(snd_pcm_prepare)(handle);
		return (err < 0)? err : 0;
	}
	return err;
}
static void ALSA_PlayAudio(_THIS)
{
	
	snd_pcm_state_t state = snd_pcm_state(pcm_handle);
	if (state == SND_PCM_STATE_SUSPENDED) {
		int err = snd_pcm_resume(pcm_handle);
		if (err < 0) {
			snd_pcm_prepare(pcm_handle);
		}
	}
	if (state == SND_PCM_STATE_XRUN || state == SND_PCM_STATE_DRAINING) {
	    snd_pcm_prepare(pcm_handle);
	}
   int status;
    snd_pcm_uframes_t frames_left;
    const int16_t *src = (const int16_t *)mixbuf;
	
    int32_t *dst = &mixbuf32[0];

    const int channels = 2;  // stéréo
    const snd_pcm_uframes_t total_frames = this->spec.samples;

    swizzle_alsa_channels(this);
	/* Conversion S16 → S32 */
float max_input = 0.0f;
float max_output = 0.0f;
int ch = 0;
snd_pcm_uframes_t i = 0;
    // High pass filter pour atténuer les basses + conversion S16 -> S32
    float alpha = 0.95f; // ajuste le cutoff (~100 Hz à 44100 Hz)
for (i = 0; i < total_frames; i++) {
    for (ch = 0; ch < channels; ch++) {
        float sample = (float)src[i * channels + ch] / 32768.0f;
        dst[i * channels + ch] = (int32_t)(sample * 2147483647.0f);
    /* Track max pour debug */
        float abs_val = (sample < 0) ? -sample : sample;
        if (abs_val > max_input) max_input = abs_val;
        
        abs_val = (float)dst[i * channels + ch] / 2147483647.0f;
        if (abs_val < 0) abs_val = -abs_val;
        if (abs_val > max_output) max_output = abs_val;
    }
}

/* Log une fois par seconde */
static int frame_count = 0;
if (frame_count++ % 100 == 0) {
    fprintf(stderr, "[SDL Audio] total frame %d, Max input: %.3f, Max output: %.3f\n", total_frames,
            max_input, max_output);
	}
 	frames_left = total_frames;
    const int frame_size = this->spec.channels * sizeof(int32_t);
	
    const Uint8 *sample_buf = (const Uint8 *)dst;
	int eagain_retry = 1;
    while (frames_left > 0 && this->enabled) {
        status = SDL_NAME(snd_pcm_writei)(pcm_handle, sample_buf, frames_left);
        
         if (status == -EPIPE || status == -EINTR || status == -ESTRPIPE)
         {
            if (ALSA_pcm_recover(pcm_handle, status, 1) < 0)
               return -1;
            break;
         }
         else if (status == -EAGAIN)
         {
            if (eagain_retry == 1)
            {
               eagain_retry = 0;
               continue;
            }
            break;
         }
         else if (status < 0)
            return -1;
        
        sample_buf += status * frame_size;
        frames_left -= status;
    }
}

static Uint8 *ALSA_GetAudioBuf(_THIS)
{
	return(mixbuf);
}

static void ALSA_CloseAudio(_THIS)
{
	fprintf(stderr, "close audio\n");
	if ( mixbuf != NULL ) {
		SDL_FreeAudioMem(mixbuf);
		mixbuf = NULL;
	}
	if(mixbuf32 != NULL)
	{
		SDL_free(mixbuf32);
		mixbuf32 = NULL;
	}
	if ( pcm_handle ) {
		/* Wait for the submitted audio to drain
		   snd_pcm_drop() can hang, so don't use that.
		 */
		Uint32 delay = ((this->spec.samples * 1000) / this->spec.freq) * 2;
		SDL_Delay(delay*4);
		SDL_NAME(snd_pcm_drop)(pcm_handle);  /* Vide les buffers */
		SDL_NAME(snd_pcm_close)(pcm_handle);
		pcm_handle = NULL;
	}
}

static int ALSA_finalize_hardware(_THIS, SDL_AudioSpec *spec, snd_pcm_hw_params_t *hwparams, int override)
{
	int status;
	snd_pcm_uframes_t bufsize;
	

	
	/* Get samples for the actual buffer size */
	status = SDL_NAME(snd_pcm_hw_params_get_buffer_size)(hwparams, &bufsize);
	if ( status < 0 ) {
		return(-1);
	}
	if ( !override && bufsize != spec->samples * 2 ) {
		return(-1);
	}

	/* FIXME: Is this safe to do? */
	//spec->samples = 1024;
	
	/* This is useful for debugging */
	if ( getenv("SDL_AUDIO_ALSA_DEBUG") ) {
		snd_pcm_uframes_t persize = 0;
		unsigned int periods = 0;

		SDL_NAME(snd_pcm_hw_params_get_period_size)(hwparams, &persize, NULL);
		SDL_NAME(snd_pcm_hw_params_get_periods)(hwparams, &periods, NULL);
		fprintf(stderr, "ALSA: mode period size = %ld, periods = %u, buffer size = %lu\n", persize, periods, bufsize);
	}
	return(0);
}

static int ALSA_set_period_size(_THIS, SDL_AudioSpec *spec, snd_pcm_hw_params_t *params, int override)
{
	const char *env;
	int status;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_uframes_t frames;
	unsigned int periods;

	/* Copy the hardware parameters for this setup */
	snd_pcm_hw_params_alloca(&hwparams);
	SDL_NAME(snd_pcm_hw_params_copy)(hwparams, params);

	if ( !override ) {
		env = getenv("SDL_AUDIO_ALSA_SET_PERIOD_SIZE");
		if ( env ) {
			override = SDL_atoi(env);
			if ( override == 0 ) {
				return(-1);
			}
		}
	}

	frames = 1024;
	status = SDL_NAME(snd_pcm_hw_params_set_period_size)(pcm_handle, hwparams, frames, NULL);
	if ( status < 0 ) {
		return(-1);
	}

	periods = 4;
	status = SDL_NAME(snd_pcm_hw_params_set_periods)(pcm_handle, hwparams, periods, NULL);
	if ( status < 0 ) {
		return(-1);
	}

	return ALSA_finalize_hardware(this, spec, hwparams, override);
}

static int ALSA_set_buffer_size(_THIS, SDL_AudioSpec *spec, snd_pcm_hw_params_t *params, int override)
{
	const char *env;
	int status;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_uframes_t frames;

	/* Copy the hardware parameters for this setup */
	snd_pcm_hw_params_alloca(&hwparams);
	SDL_NAME(snd_pcm_hw_params_copy)(hwparams, params);

	if ( !override ) {
		env = getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE");
		if ( env ) {
			override = SDL_atoi(env);
			if ( override == 0 ) {
				return(-1);
			}
		}
	}

	frames = 4096;
	status = SDL_NAME(snd_pcm_hw_params_set_buffer_size_near)(pcm_handle, hwparams, frames);
	if ( status < 0 ) {
		return(-1);
	}

	return ALSA_finalize_hardware(this, spec, hwparams, override);
}

static int ALSA_OpenAudio(_THIS, SDL_AudioSpec *spec)
{
	int                  status;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_format_t     format;
	unsigned int         rate;
	unsigned int 	     channels;
	Uint16               test_format;

	/* Open the audio device */
	/* Name of device should depend on # channels in spec */
	status = SDL_NAME(snd_pcm_open)(&pcm_handle, get_audio_device(spec->channels), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);

	if ( status < 0 ) {
		SDL_SetError("Couldn't open audio device: %s", SDL_NAME(snd_strerror)(status));
		return(-1);
	}

	/* Switch to blocking mode for playback */
	/* Note: this must happen before hw/sw params are set. */
	SDL_NAME(snd_pcm_nonblock)(pcm_handle, 0);

	/* Figure out what the hardware is capable of */
	snd_pcm_hw_params_alloca(&hwparams);
	status = SDL_NAME(snd_pcm_hw_params_any)(pcm_handle, hwparams);
	if ( status < 0 ) {
		SDL_SetError("Couldn't get hardware config: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}
#ifdef LOG_CONSOLE
	fprintf(stderr,"Set alsa to RW\n");
#endif
	/* SDL only uses interleaved sample output */
	status = SDL_NAME(snd_pcm_hw_params_set_access)(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set interleaved access: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}
#ifdef LOG_CONSOLE
	fprintf(stderr,"END Set alsa to RW status %d\n", status);
#endif
	/* Try for a closest match on audio format */
	
	//force format to PCM_S32_LE
	status = -1;
	format = SND_PCM_FORMAT_S32_LE;
	if ( format != 0 ) {
		status = SDL_NAME(snd_pcm_hw_params_set_format)(pcm_handle, hwparams, format);
	}
	
	if ( status < 0 ) {
		SDL_SetError("Couldn't find any hardware audio formats");
		ALSA_CloseAudio(this);
		return(-1);
	}
	spec->format = AUDIO_S16LSB;
	//spec->samples=1024;
	/* Set the number of channels */
	status = SDL_NAME(snd_pcm_hw_params_set_channels)(pcm_handle, hwparams, spec->channels);
	channels = spec->channels;
	if ( status < 0 ) {
		status = SDL_NAME(snd_pcm_hw_params_get_channels)(hwparams, &channels);
		if ( status < 0 ) {
			SDL_SetError("Couldn't set audio channels");
			ALSA_CloseAudio(this);
			return(-1);
		}
		spec->channels = channels;
	}

	/* Set the audio rate */
	rate = spec->freq;
	SDL_NAME(snd_pcm_hw_params_set_rate_resample)(pcm_handle, hwparams, 0);
	status = SDL_NAME(snd_pcm_hw_params_set_rate_near)(pcm_handle, hwparams, &rate, NULL);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set audio frequency: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}
	spec->freq = rate;
	spec->samples = 1024;
	//set period size and buffer size directly
	unsigned int frames = 1024;
	status = SDL_NAME(snd_pcm_hw_params_set_period_size_near)(pcm_handle, hwparams, &frames, NULL);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set period size: %s", SDL_NAME(snd_strerror)(status));
		return(-1);
	}
	frames = 4096;
	status = SDL_NAME(snd_pcm_hw_params_set_buffer_size_near)(pcm_handle, hwparams, &frames);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set buffer size: %s", SDL_NAME(snd_strerror)(status));
		return(-1);
	}
	/* "set" the hardware with the desired parameters */
	status = SDL_NAME(snd_pcm_hw_params)(pcm_handle, hwparams);
	if ( status < 0 ) {
		return(-1);
	}
	/* Set the buffer size, in samples */
//	if ( ALSA_set_period_size(this, spec, hwparams, 0) < 0 &&
//	     ALSA_set_buffer_size(this, spec, hwparams, 0) < 0 ) {
//		/* Failed to set desired buffer size, do the best you can... */
//		if ( ALSA_set_period_size(this, spec, hwparams, 1) < 0 ) {
//			SDL_SetError("Couldn't set hardware audio parameters: %s", SDL_NAME(snd_strerror)(status));
//			ALSA_CloseAudio(this);
//			return(-1);
//		}
//	}

	/* Set the software parameters */
	snd_pcm_sw_params_alloca(&swparams);
	status = SDL_NAME(snd_pcm_sw_params_current)(pcm_handle, swparams);
	if ( status < 0 ) {
		SDL_SetError("Couldn't get software config: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}
	status = SDL_NAME(snd_pcm_sw_params_set_avail_min)(pcm_handle, swparams, spec->samples);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set minimum available samples: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}
	status = SDL_NAME(snd_pcm_sw_params_set_start_threshold)(pcm_handle, swparams, frames/2);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set start threshold: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}
	status = SDL_NAME(snd_pcm_sw_params)(pcm_handle, swparams);
	if ( status < 0 ) {
		SDL_SetError("Couldn't set software audio parameters: %s", SDL_NAME(snd_strerror)(status));
		ALSA_CloseAudio(this);
		return(-1);
	}

	/* Calculate the final parameters for this audio specification */
	SDL_CalculateAudioSpec(spec);

	/* Allocate mixing buffer */
	mixlen = spec->size;
	mixbuf = (Uint8 *)SDL_AllocAudioMem(mixlen);

	if ( mixbuf == NULL ) {
		ALSA_CloseAudio(this);
		return(-1);
	}
	SDL_memset(mixbuf, spec->silence, spec->size);

	//buffer for converting S16 to S32
	int total_samples = spec->samples * spec->channels;
	fprintf(stderr, "allocated %d bytes\n", total_samples * sizeof(int32_t));
	mixbuf32 = SDL_malloc(total_samples * sizeof(int32_t));
	if (!mixbuf32) return -1;
	SDL_memset(mixbuf32, 0, total_samples * sizeof(int32_t));
	/* We're ready to rock and roll. :-) */
	return(0);
}
