/* wbx-entry.c - the chimera guest ABI over dsda-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h): the same driver and exports, one in the
 * sandbox and one out of it, which is what makes the equivalence gate a proof.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>

#include "dsda-driver.h"

static char g_load_error[1024];

/* Turbo: the picture is not converted; the engine still draws it (see
 * dsdadrv_frame - what drawing touches is part of the machine) */
ECL_INVISIBLE int chimera_render_enabled = 1;

/* the controller has more than 64 buttons: they arrive through SetButton; the
 * packed mask of FrameAdvance covers the first 64, and a step sees the union */
static uint8_t g_set_buttons[256];

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

ECL_EXPORT int Init(void)
{
	g_load_error[0] = '\0';
	return dsdadrv_init(g_load_error, (int)sizeof g_load_error);
}

/* a player not in the game has no inputs; "Turn Speed Frac." is longtics' */
ECL_EXPORT int IsButtonActive(int32_t index) { return dsdadrv_button_active(index); }
ECL_EXPORT int IsAxisActive(int32_t index) { return dsdadrv_axis_active(index); }

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < (int32_t)sizeof g_set_buttons) g_set_buttons[index] = state ? 1 : 0;
}

ECL_EXPORT void SetAxis(int32_t index, int32_t value) { dsdadrv_set_axis(index, value); }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	const int n = dsdadrv_button_count();
	for (int i = 0; i < n; i++)
		dsdadrv_set_button(i, g_set_buttons[i] | (i < 64 ? (int)((packed >> i) & 1) : 0));
	dsdadrv_frame(chimera_render_enabled);
}

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return (uint32_t *)dsdadrv_video(&w, &h);
}
ECL_EXPORT int GetVideoWidth(void)
{
	int w, h;
	dsdadrv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetVideoHeight(void)
{
	int w, h;
	dsdadrv_video(&w, &h);
	return h;
}
/* native resolution is 320x200's multiples shown at 4:3, as vanilla on a CRT;
 * the other aspects are drawn corrected */
ECL_EXPORT int GetDisplayAspectX(void)
{
	int x, y;
	dsdadrv_aspect(&x, &y);
	return x;
}
ECL_EXPORT int GetDisplayAspectY(void)
{
	int x, y;
	dsdadrv_aspect(&x, &y);
	return y;
}

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return (int16_t *)dsdadrv_audio(&n);
}
ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	dsdadrv_audio(&n);
	return n;
}

/* a step is a tic: 35 Hz */
ECL_EXPORT int GetVsyncNumerator(void) { return 35; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

/* the screen melt's steps are lag: the game waits (BizHawk's core) */
ECL_EXPORT int InputWasRead(void) { return dsdadrv_input_was_read(); }

ECL_EXPORT int GetMemoryDomainCount(void) { return dsdadrv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return dsdadrv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return dsdadrv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return dsdadrv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return dsdadrv_domain_writable(i); }

ECL_EXPORT const char *GetGameProperties(void) { return dsdadrv_game_properties(); }

ECL_EXPORT uint64_t GetCycleCount(void) { return dsdadrv_clock(); }
