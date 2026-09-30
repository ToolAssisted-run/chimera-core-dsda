/* chimera-platform.h - between the platform layer (platform/) and the driver */
#ifndef CHIMERA_PLATFORM_H
#define CHIMERA_PLATFORM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* the machine's clock (the driver): a tic is 1000/35 ms */
uint64_t chimera_clock_ms(void);
/* the engine's screen, through its palette, as BGRA (platform/i_video.c) */
int chimera_video_width(void);
int chimera_video_height(void);
void chimera_video_bgra(uint32_t *out);
/* the engine's start (platform/i_main.c): upstream's main before its loop;
 * configure runs between the configuration's defaults and the setup */
void chimera_dsda_start(int argc, char **argv, void (*configure)(void));
/* the engine's exit - I_Error or a quit - which halts the machine (the driver);
 * it does not return */
void chimera_exit(int rc) __attribute__((noreturn));
/* I_Error's message, before the exit (patches/0002) */
void chimera_error_message(const char *msg);
#ifdef __cplusplus
}
#endif
#endif
