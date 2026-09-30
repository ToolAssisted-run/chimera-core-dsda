/* SDL_mixer.h - the SDL_mixer calls dsda-doom's sound layer (SDL/i_sound.c)
 * makes, answered by the core (platform/sdl-shim.c): the "device" is opened at
 * the engine's rate and the post-mix callback - the engine's own mixer of
 * sound effects and its OPL music - is what the frontend's samples come from.
 * SDL_mixer's own music (its MIDI, Ogg and MP3 decoders) is not there: a song
 * none of upstream's players takes stays silent, as in BizHawk's core. */
#ifndef CHIMERA_SDL_MIXER_SHIM_H
#define CHIMERA_SDL_MIXER_SHIM_H
#include "SDL.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _Mix_Music Mix_Music;
typedef enum { MUS_NONE, MUS_CMD, MUS_WAV, MUS_MOD, MUS_MID, MUS_OGG, MUS_MP3 } Mix_MusicType;
#define MIX_DEFAULT_FORMAT AUDIO_S16SYS
int Mix_OpenAudioDevice(int frequency, Uint16 format, int channels, int chunksize, const char *device, int allowed_changes);
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
void Mix_SetPostMix(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg);
void Mix_CloseAudio(void);
Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc);
void Mix_FreeMusic(Mix_Music *music);
Mix_MusicType Mix_GetMusicType(const Mix_Music *music);
int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_FadeInMusic(Mix_Music *music, int loops, int ms);
int Mix_VolumeMusic(int volume);
int Mix_HaltMusic(void);
void Mix_PauseMusic(void);
void Mix_ResumeMusic(void);
const char *Mix_GetError(void);
#ifdef __cplusplus
}
#endif
#endif
