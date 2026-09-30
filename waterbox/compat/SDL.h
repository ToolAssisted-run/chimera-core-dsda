/* SDL.h - the little of SDL2 dsda-doom's engine still names once its
 * platform layer (SDL/) is the core's (platform/): the types its headers carry
 * and a few calls, answered in platform/i_system.c. No window, no audio device,
 * no thread, no clock of the host's. */
#ifndef CHIMERA_SDL_SHIM_H
#define CHIMERA_SDL_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_Thread SDL_Thread;

typedef struct SDL_PixelFormat SDL_PixelFormat;
typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

/* ---- audio: the device is the frontend, which pulls a step's samples from
 * the engine's own mixer (platform/sdl-shim.c) */
typedef Uint16 SDL_AudioFormat;
#define AUDIO_U8 0x0008
#define AUDIO_S8 0x8008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16 AUDIO_S16LSB
#define AUDIO_S16SYS AUDIO_S16LSB
#define AUDIO_F32 0x8120
#define SDL_AUDIO_ISFLOAT(x) (((x) & 0x0100) != 0)
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
#define SDL_INIT_AUDIO 0x00000010u
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL_AudioSpec
{
	int freq;
	SDL_AudioFormat format;
	Uint8 channels;
	Uint8 silence;
	Uint16 samples;
	Uint16 padding;
	Uint32 size;
	SDL_AudioCallback callback;
	void *userdata;
} SDL_AudioSpec;
typedef struct SDL_AudioCVT
{
	int needed;
	Uint8 *buf;
	int len;
	int len_cvt;
	int len_mult;
	double len_ratio;
} SDL_AudioCVT;
int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
	SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);
int SDL_InitSubSystem(Uint32 flags);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
void SDL_CloseAudio(void);

/* ---- mutexes: one thread */
typedef struct SDL_mutex SDL_mutex;
SDL_mutex *SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex *m);
int SDL_UnlockMutex(SDL_mutex *m);
void SDL_DestroyMutex(SDL_mutex *m);

/* ---- memory streams, for SDL_mixer's loaders (none loads) */
typedef struct SDL_RWops SDL_RWops;
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
void SDL_FreeRW(SDL_RWops *area);

const char *SDL_GetError(void);
void SDL_Delay(Uint32 ms);
char *SDL_GetBasePath(void);

/* the machine's milliseconds (platform/sdl-shim.c) */
Uint32 SDL_GetTicks(void);
Uint32 SDL_GetMouseState(int *x, int *y);
int SDL_IsTextInputActive(void);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);

#ifdef __cplusplus
}
#endif

#endif
#define SDL_VERSION_ATLEAST(X, Y, Z) 1
