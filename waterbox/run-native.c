/* run-native.c - the native reference for the equivalence gate.
 *
 * Links the SAME driver, dsda-doom and zlib objects the guest build uses
 * (emulibc degraded to calloc by native-shim/) and drives the exports
 * directly. The work dir holds what the sandbox would see mounted: the IWAD
 * under its firmware name, dsda-doom.wad, the PWADs a "slots" file names, and
 * the "settings" JSON.
 *
 * usage: run-native <workdir> [gate-harness options]
 */
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gate-harness.h"

extern int Init(void);
extern const char *GetLoadError(void);
extern void SetButton(int32_t index, int32_t state);
extern void SetAxis(int32_t index, int32_t value);
extern void FrameAdvance(uint64_t packed);
extern uint32_t *GetVideoBgra(void);
extern int GetVideoWidth(void);
extern int GetVideoHeight(void);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int InputWasRead(void);
extern void SetRenderingEnabled(int on);
extern int GetVsyncNumerator(void);
extern int GetVsyncDenominator(void);
extern int GetMemoryDomainCount(void);
extern const char *GetMemoryDomainName(int i);
extern uint8_t *GetMemoryDomainPtr(int i);
extern int64_t GetMemoryDomainSize(int i);
extern uint64_t GetCycleCount(void);
extern const char *GetGameProperties(void);
extern int IsButtonActive(int32_t index);
extern int IsAxisActive(int32_t index);

static void frame(void) { FrameAdvance(0); }
static const uint32_t *video(int *w, int *h)
{
	*w = GetVideoWidth();
	*h = GetVideoHeight();
	return GetVideoBgra();
}
static const int16_t *audio(int *n)
{
	*n = GetAudioSampleCount();
	return GetAudio();
}

/* a crash of the native build says where (the sandbox's says nothing) */
static void crashed(int sig)
{
	void *frames[64];
	const int n = backtrace(frames, 64);
	fprintf(stderr, "run-native: signal %d\n", sig);
	backtrace_symbols_fd(frames, n, 2);
	_exit(128 + sig);
}

int main(int argc, char **argv)
{
	signal(SIGSEGV, crashed);
	signal(SIGFPE, crashed);
	signal(SIGABRT, crashed);
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [options]\n");
		return 2;
	}
	if (chdir(argv[1]) != 0)
	{
		perror(argv[1]);
		return 1;
	}
	static struct gate_opts o;
	if (!gate_parse_opts(argc, argv, 2, &o))
		return 2;

	struct gate_core c = {
		.init = Init,
		.load_error = GetLoadError,
		.set_button = SetButton,
		.set_axis = SetAxis,
		.frame = frame,
		.video = video,
		.audio = audio,
		.input_was_read = InputWasRead,
		.domain_count = GetMemoryDomainCount,
		.domain_name = GetMemoryDomainName,
		.domain_ptr = GetMemoryDomainPtr,
		.domain_size = GetMemoryDomainSize,
		.vsync_numerator = GetVsyncNumerator,
		.vsync_denominator = GetVsyncDenominator,
		.pre_frame = NULL,
		.set_rendering = SetRenderingEnabled,
		.clock = GetCycleCount,
		.game_properties = GetGameProperties,
		.button_active = IsButtonActive,
		.axis_active = IsAxisActive,
	};
	return gate_run(&c, &o);
}
