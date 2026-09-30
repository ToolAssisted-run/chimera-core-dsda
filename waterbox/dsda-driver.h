/* dsda-driver.h - the machine the chimera exports (wbx-entry.c) drive */
#ifndef DSDA_DRIVER_H
#define DSDA_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the largest picture: 320x200 at twelve times, or the widescreen modes up to
 * the same width (BizHawk's scale factors, capped there) */
#define DSDA_VIDEO_MAX_W 3840
#define DSDA_VIDEO_MAX_H 2400

int dsdadrv_init(char *err, int errsize);
int dsdadrv_button_count(void);
int dsdadrv_axis_count(void);
void dsdadrv_set_button(int index, int state);
void dsdadrv_set_axis(int index, int32_t value);
int dsdadrv_button_active(int index);
int dsdadrv_axis_active(int index);
void dsdadrv_frame(int render);
const uint32_t *dsdadrv_video(int *w, int *h);
void dsdadrv_aspect(int *x, int *y);
const int16_t *dsdadrv_audio(int *n);
int dsdadrv_input_was_read(void);
uint64_t dsdadrv_clock(void);

int dsdadrv_domain_count(void);
const char *dsdadrv_domain_name(int i);
uint8_t *dsdadrv_domain_ptr(int i);
int64_t dsdadrv_domain_size(int i);
int dsdadrv_domain_writable(int i);
const char *dsdadrv_game_properties(void);

#ifdef __cplusplus
}
#endif

#endif
