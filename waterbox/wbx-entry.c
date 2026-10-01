/* wbx-entry.c - the chimera guest ABI over dsda-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h): the same driver and exports, one in the
 * sandbox and one out of it, which is what makes the equivalence gate a proof.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>

#include "dsda-driver.h"
#include "lmp-import.h"

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

/* ------------------------------------------------------------ the importer */

/* the files an import reads (the demo, the WADs), each read whole once */
#define IMPORT_FILES 64
static struct { char name[256]; unsigned char *data; size_t size; } g_import_files[IMPORT_FILES];
static int g_import_nfiles;
static char *g_import_result;

static const unsigned char *import_read(void *ctx, const char *name, size_t *size, const char **found_name)
{
	(void)ctx;
	(void)found_name;
	for (int i = 0; i < g_import_nfiles; i++)
		if (!strcmp(g_import_files[i].name, name))
		{
			*size = g_import_files[i].size;
			return g_import_files[i].data;
		}
	if (g_import_nfiles == IMPORT_FILES) return NULL;
	FILE *f = fopen(name, "rb");
	if (!f) return NULL;
	unsigned char *data = NULL;
	size_t n = 0, cap = 0;
	for (;;)
	{
		if (n == cap)
		{
			unsigned char *grown = (unsigned char *)realloc(data, cap = cap ? cap * 2 : 1 << 16);
			if (!grown) { free(data); fclose(f); return NULL; }
			data = grown;
		}
		const size_t got = fread(data + n, 1, cap - n, f);
		if (!got) break;
		n += got;
	}
	fclose(f);
	snprintf(g_import_files[g_import_nfiles].name, sizeof g_import_files[0].name, "%s", name);
	g_import_files[g_import_nfiles].data = data;
	g_import_files[g_import_nfiles].size = n;
	g_import_nfiles++;
	*size = n;
	return data;
}

static char *import_error(const char *msg)
{
	size_t n = strlen(msg);
	char *out = (char *)malloc(n * 6 + 16);
	if (!out) return NULL;
	char *p = out + sprintf(out, "{\"error\": \"");
	for (const unsigned char *c = (const unsigned char *)msg; *c; c++)
	{
		if (*c == '"' || *c == '\\') p += sprintf(p, "\\%c", *c);
		else if (*c < 0x20 || *c >= 0x7f) p += sprintf(p, "\\u%04x", *c);
		else *p++ = (char)*c;
	}
	strcpy(p, "\"}\n");
	return out;
}

/* A demo as the parts of a Chimera project (lmp-import.h) - for a frontend,
 * which loads the core and calls this instead of Init. The demo is the mounted
 * file "movie"; the WADs it reads are mounted files by their names (the IWAD as
 * importIwad names it, or under its firmware id; the PWADs and patches by the
 * names the demo's footer gives, or importPwads's). The options are settings:
 *   importVersion     the IWAD's release (a version id)
 *   importIwad        the mounted IWAD's name, its hash the release
 *   importPwads       the PWADs and patches by name, ';' between, in order
 *   importNoPwads     none, whatever the footer names
 *   importLongtics    a Raven demo recorded with -longtics its header does not say
 *   importRespawn, importFast, importNomonsters   a 1.2 demo's monster flags
 * The JSON of the parts - "settings", "firmware", "files", "input", "frames",
 * and what the demo is ("format", "tics", "players", "footer", "port",
 * "notes") - or {"error": "why"}. */
ECL_EXPORT const char *ImportMovie(void)
{
	static char version[64], iwad[256], pwad_list[4096];
	static char *pwads[64];
	free(g_import_result);
	g_import_result = NULL;
	for (int i = 0; i < g_import_nfiles; i++) free(g_import_files[i].data);
	g_import_nfiles = 0;

	struct lmpi_options o;
	memset(&o, 0, sizeof o);
	version[0] = iwad[0] = pwad_list[0] = 0;
	if (wbx_setting_str("importVersion", version, (int)sizeof version) > 0) o.version = version;
	if (wbx_setting_str("importIwad", iwad, (int)sizeof iwad) > 0) o.iwad = iwad;
	if (wbx_setting_str("importPwads", pwad_list, (int)sizeof pwad_list) >= 0)
	{
		int n = 0;
		for (char *t = strtok(pwad_list, ";"); t && n < 64; t = strtok(NULL, ";")) pwads[n++] = t;
		o.pwads = (const char *const *)pwads;
		o.npwads = n;
	}
	o.no_pwads = wbx_setting_bool("importNoPwads", 0);
	o.longtics = wbx_setting_bool("importLongtics", 0);
	o.respawn = wbx_setting_bool("importRespawn", 0);
	o.fast = wbx_setting_bool("importFast", 0);
	o.nomonsters = wbx_setting_bool("importNomonsters", 0);
	o.read = import_read;

	size_t size;
	const unsigned char *demo = import_read(NULL, "movie", &size, NULL);
	char err[1024];
	if (!demo)
		g_import_result = import_error("no demo: the file \"movie\" is not mounted");
	else
	{
		g_import_result = lmpi_import(demo, size, &o, NULL, NULL, err, sizeof err);
		if (!g_import_result) g_import_result = import_error(err);
	}
	return g_import_result ? g_import_result : "{\"error\": \"out of memory\"}\n";
}
