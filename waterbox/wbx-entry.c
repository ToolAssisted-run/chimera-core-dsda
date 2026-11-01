/* wbx-entry.c - the chimera guest ABI over dsda-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h): the same driver and exports, one in the
 * sandbox and one out of it, which is what makes the equivalence gate a proof.
 */
#include <ctype.h>
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

/* ------------------------------------------------------------ the tool */

/* The core's own tool, the demo importer (its package's "tools", which
 * Chimera offers under Tools > Core-Specific Tools): a Doom-engine demo made into a Chimera
 * project for this core (lmp-import.h), written to the file the person chose.
 * Chimera calls RunTool instead of Init, with no game. Its request is the
 * file "tool" (written with no spaces):
 *   {"tool":"import_demo","package":{"sha1":...,"version":...},
 *    "options":{"noPwads":...,"longtics":...,"respawn":...,"fast":...,"nomonsters":...},
 *    "inputs":{"demo":"<name>","iwad":"<name>","pwad.0":"<name>",...},
 *    "outputs":{"project":"<name>"}}
 * The demo is mounted as "demo", the IWAD as "iwad", the PWADs and patches
 * as pwad.0, pwad.1, ... in the order picked; the importer asks for each by
 * the name the person's file has (the IWAD's, the footer's), whatever its
 * case. The project pins this package by the hash and version the request
 * gives. The answer is {"done": ...} or {"refused": ...}, a key of the
 * package's texts and its arguments. */

/* the files the import reads, each read whole once: by mount name */
#define IMPORT_FILES 66
static struct { char mount[32]; char name[256]; unsigned char *data; size_t size; } g_import_files[IMPORT_FILES];
static int g_import_nfiles;
static char g_tool_answer[2048];

/* a string or a flag of one of the request's objects ("inputs", "options",
 * "package"); "" or 0 when it is not there */
static const char *tool_object(const char *req, const char *object, const char **end)
{
	char key[64];
	snprintf(key, sizeof key, "\"%s\":{", object);
	const char *at = strstr(req, key);
	if (!at) return NULL;
	at += strlen(key);
	const char *p = at;
	int depth = 1, quoted = 0;
	for (; *p && depth; p++)
	{
		if (quoted) { if (*p == '\\' && p[1]) p++; else if (*p == '"') quoted = 0; }
		else if (*p == '"') quoted = 1;
		else if (*p == '{') depth++;
		else if (*p == '}') depth--;
	}
	*end = p;
	return at;
}

static int tool_string(const char *req, const char *object, const char *name, char *out, size_t cap)
{
	out[0] = 0;
	const char *end, *at = tool_object(req, object, &end);
	if (!at) return 0;
	char key[96];
	snprintf(key, sizeof key, "\"%s\":\"", name);
	const size_t klen = strlen(key);
	for (const char *p = at; p + klen <= end; p++)
	{
		if (strncmp(p, key, klen)) continue;
		size_t n = 0;
		for (p += klen; *p && *p != '"' && n + 1 < cap; p++)
		{
			if (*p == '\\' && p[1])
			{
				p++;
				/* the escapes a JSON writer uses for a name: \" \\ \/ and \u00XX */
				if (*p == 'u' && p[1] && p[2] && p[3] && p[4])
				{
					unsigned v = 0;
					sscanf(p + 1, "%4x", &v);
					out[n++] = (char)v;
					p += 4;
					continue;
				}
			}
			out[n++] = *p;
		}
		out[n] = 0;
		return 1;
	}
	return 0;
}

static int tool_flag(const char *req, const char *object, const char *name)
{
	const char *end, *at = tool_object(req, object, &end);
	if (!at) return 0;
	char key[96];
	snprintf(key, sizeof key, "\"%s\":true", name);
	const char *found = strstr(at, key);
	return found && found < end;
}

static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
	return *a == *b;
}

static const unsigned char *import_read(void *ctx, const char *name, size_t *size, const char **found_name)
{
	(void)ctx;
	/* a name the importer asks for, as the person's file is called (or the
	 * mount itself) */
	for (int i = 0; i < g_import_nfiles; i++)
		if (same_name(g_import_files[i].name, name) || !strcmp(g_import_files[i].mount, name))
		{
			if (!g_import_files[i].data)
			{
				FILE *f = fopen(g_import_files[i].mount, "rb");
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
				g_import_files[i].data = data;
				g_import_files[i].size = n;
			}
			*size = g_import_files[i].size;
			if (found_name) *found_name = g_import_files[i].name;
			return g_import_files[i].data;
		}
	return NULL;
}

/* {"<what>": "<text>"}, the text escaped for JSON */
static const char *tool_answer(const char *what, const char *text)
{
	char *p = g_tool_answer + snprintf(g_tool_answer, sizeof g_tool_answer, "{\"%s\": \"", what);
	char *const last = g_tool_answer + sizeof g_tool_answer - 8;
	for (const unsigned char *c = (const unsigned char *)text; *c && p < last; c++)
	{
		if (*c == '"' || *c == '\\') p += sprintf(p, "\\%c", *c);
		else if (*c < 0x20) p += sprintf(p, "\\u%04x", *c);
		else *p++ = (char)*c;
	}
	strcpy(p, "\"}");
	return g_tool_answer;
}

ECL_EXPORT const char *RunTool(void)
{
	static char req[32 * 1024];
	FILE *r = fopen("tool", "rb");
	const size_t got = r ? fread(req, 1, sizeof req - 1, r) : 0;
	if (r) fclose(r);
	req[got] = 0;
	for (int i = 0; i < g_import_nfiles; i++) free(g_import_files[i].data);
	memset(g_import_files, 0, sizeof g_import_files);
	g_import_nfiles = 0;
	if (!strstr(req, "\"tool\":\"import_demo\"")) return tool_answer("refused", "error.tool.unknown");

	/* the files picked: the demo, the IWAD, the PWADs in their order */
	static char pwad_names[64][256];
	static const char *pwads[64];
	int npwads = 0;
	char demo_name[256], iwad_name[256];
	tool_string(req, "inputs", "demo", demo_name, sizeof demo_name);
	tool_string(req, "inputs", "iwad", iwad_name, sizeof iwad_name);
	snprintf(g_import_files[g_import_nfiles].mount, sizeof g_import_files[0].mount, "iwad");
	snprintf(g_import_files[g_import_nfiles++].name, sizeof g_import_files[0].name, "%s", iwad_name);
	for (; npwads < 64; npwads++)
	{
		char mount[32];
		snprintf(mount, sizeof mount, "pwad.%d", npwads);
		if (!tool_string(req, "inputs", mount, pwad_names[npwads], sizeof pwad_names[0])) break;
		pwads[npwads] = pwad_names[npwads];
		snprintf(g_import_files[g_import_nfiles].mount, sizeof g_import_files[0].mount, "%s", mount);
		snprintf(g_import_files[g_import_nfiles++].name, sizeof g_import_files[0].name, "%s", pwad_names[npwads]);
	}
	snprintf(g_import_files[g_import_nfiles].mount, sizeof g_import_files[0].mount, "demo");
	snprintf(g_import_files[g_import_nfiles++].name, sizeof g_import_files[0].name, "%s", demo_name);

	struct lmpi_options o;
	memset(&o, 0, sizeof o);
	o.iwad = iwad_name[0] ? iwad_name : NULL;
	o.pwads = npwads ? (const char *const *)pwads : NULL;
	o.npwads = npwads;
	o.no_pwads = tool_flag(req, "options", "noPwads");
	o.longtics = tool_flag(req, "options", "longtics");
	o.respawn = tool_flag(req, "options", "respawn");
	o.fast = tool_flag(req, "options", "fast");
	o.nomonsters = tool_flag(req, "options", "nomonsters");
	o.read = import_read;

	size_t size;
	const unsigned char *demo = import_read(NULL, "demo", &size, NULL);
	if (!demo) return tool_answer("refused", "error.import.no_demo");

	/* the project: named after the demo, pinned to this package */
	char title[256], sha1[64], version[128];
	snprintf(title, sizeof title, "%s", demo_name);
	char *dot = strrchr(title, '.');
	if (dot && dot != title) *dot = 0;
	tool_string(req, "package", "sha1", sha1, sizeof sha1);
	tool_string(req, "package", "version", version, sizeof version);
	struct lmpi_project project = { title, demo_name, "DSDA-Doom", version, sha1 };
	struct lmpi_summary summary;
	memset(&summary, 0, sizeof summary);
	char err[1024];
	char *json = lmpi_import(demo, size, &o, &project, &summary, err, sizeof err);
	if (!json) return tool_answer("refused", err);
	FILE *out = fopen("project", "wb");
	const size_t len = strlen(json);
	int wrote = out && fwrite(json, 1, len, out) == len;
	if (out && fclose(out) != 0) wrote = 0;
	free(json);
	if (!wrote) return tool_answer("refused", "error.tool.not_written");

	/* what was made: the demo's format and length */
	char done[256];
	snprintf(done, sizeof done, "import.done\x1f%s\x1f%ld", summary.format, summary.tics);
	return tool_answer("done", done);
}
