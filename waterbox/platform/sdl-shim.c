/* sdl-shim.c - what compat/SDL.h and compat/SDL_mixer.h declare: SDL as
 * dsda-doom's remaining platform files (SDL/i_main.c, SDL/i_system.c,
 * SDL/i_sound.c) call it, in a machine that has no window, no audio device,
 * no thread and no clock of the host's.
 *
 *   - Time is the machine's (chimera_clock_ms, the driver's): a step of the
 *     game is one tic, 1/35 s. clock_gettime and time are the same clock
 *     (the link wraps them), so nothing of the engine reads the host's.
 *   - The audio "device" is opened at the engine's rate and never plays: the
 *     frontend takes each step's samples from the engine's own mixer, through
 *     upstream's sound-capture call (I_GrabSound).
 *   - SDL_mixer's music loaders load nothing: the songs play through upstream's
 *     own players (the OPL synthesizer), as in BizHawk's core. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "SDL.h"
#include "SDL_mixer.h"
#include "chimera-platform.h"

/* ---- time */

Uint32 SDL_GetTicks(void) { return (Uint32)chimera_clock_ms(); }
void SDL_Delay(Uint32 ms) { (void)ms; }

int __wrap_clock_gettime(clockid_t clk, struct timespec *tp)
{
	(void)clk;
	const uint64_t ms = chimera_clock_ms();
	tp->tv_sec = (time_t)(ms / 1000);
	tp->tv_nsec = (long)(ms % 1000) * 1000000L;
	return 0;
}

time_t __wrap_time(time_t *t)
{
	const time_t now = (time_t)(chimera_clock_ms() / 1000);
	if (t) *t = now;
	return now;
}

/* ---- the host's files: the executable's folder is the project's (where the
 * host mounts its files and the package's dsda-doom.wad) */
char *SDL_GetBasePath(void) { return strdup("./"); }

/* the engine makes folders for its configuration, its data (savegames, wad
 * stats, cached translucency maps) and its autoload files: the machine makes
 * none (the link wraps upstream's M_MakeDir), and its -config and -data name
 * places nothing is (the driver), so neither build reads or writes a file of
 * the host's but the ones mounted */
/* the working folder is the machine's own: "." (miniBox has no getcwd, and
 * the host's would make the native build's paths differ) */
char *__wrap_getcwd(char *buf, size_t size)
{
	if (!buf || size < 2) return NULL;
	buf[0] = '.';
	buf[1] = 0;
	return buf;
}

/* access: miniBox has none; a file is readable when it opens (a mounted
 * one), and nothing is writable - in both builds, so neither writes the
 * host's files */
int __wrap_access(const char *path, int mode)
{
	if (mode & W_OK) return -1;
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fclose(f);
	return 0;
}

int __wrap_M_MakeDir(const char *path, int require)
{
	(void)path; (void)require;
	return 0;
}

/* ---- input the frontend never gives as events */
Uint32 SDL_GetMouseState(int *x, int *y)
{
	if (x) *x = 0;
	if (y) *y = 0;
	return 0;
}
int SDL_IsTextInputActive(void) { return 0; }
void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}

/* ---- audio */

static int g_mix_rate = 44100;

int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
void SDL_PauseAudio(int pause_on) { (void)pause_on; }
void SDL_LockAudio(void) {}
void SDL_UnlockAudio(void) {}
void SDL_CloseAudio(void) {}
/* a sound effect in another format than the engine's: never converted (the
 * core has no libsndfile, and so no such sample to convert) */
int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
	SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate)
{
	(void)cvt; (void)src_format; (void)src_channels; (void)src_rate;
	(void)dst_format; (void)dst_channels; (void)dst_rate;
	return -1;
}
int SDL_ConvertAudio(SDL_AudioCVT *cvt) { (void)cvt; return -1; }

static int g_mutex;
SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex *)&g_mutex; }
int SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
int SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }
void SDL_DestroyMutex(SDL_mutex *m) { (void)m; }

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size) { (void)mem; (void)size; return NULL; }
void SDL_FreeRW(SDL_RWops *area) { (void)area; }

const char *SDL_GetError(void) { return "not in the core's SDL"; }

int Mix_OpenAudioDevice(int frequency, Uint16 format, int channels, int chunksize, const char *device, int allowed_changes)
{
	(void)format; (void)channels; (void)chunksize; (void)device; (void)allowed_changes;
	g_mix_rate = frequency;
	return 0;
}
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
	if (frequency) *frequency = g_mix_rate;
	if (format) *format = AUDIO_S16SYS;
	if (channels) *channels = 2;
	return 1;
}
void Mix_SetPostMix(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg) { (void)mix_func; (void)arg; }
void Mix_CloseAudio(void) {}
Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return NULL; }
void Mix_FreeMusic(Mix_Music *music) { (void)music; }
Mix_MusicType Mix_GetMusicType(const Mix_Music *music) { (void)music; return MUS_NONE; }
int Mix_PlayMusic(Mix_Music *music, int loops) { (void)music; (void)loops; return -1; }
int Mix_FadeInMusic(Mix_Music *music, int loops, int ms) { (void)music; (void)loops; (void)ms; return -1; }
int Mix_VolumeMusic(int volume) { (void)volume; return 0; }
int Mix_HaltMusic(void) { return 0; }
void Mix_PauseMusic(void) {}
void Mix_ResumeMusic(void) {}
const char *Mix_GetError(void) { return "no music player takes this song (the core plays MIDI and MUS through the OPL synthesizer)"; }
