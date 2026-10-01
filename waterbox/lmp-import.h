/* lmp-import.h - the demo importer: a Doom-engine demo (.lmp) as the parts of
 * a Chimera project for this core - what the demo dictates as the settings,
 * its IWAD (the game, any release) as the firmware, its PWADs and patches as the pwad slot's files, its
 * tics as the input log, frame for tic - or as the whole .chimeraProject.
 *
 * The same code in the core (wbx-entry.c's ImportMovie, which a frontend calls
 * with the demo and the WADs mounted) and in the command-line tool
 * (tools/lmp-import.cpp). It has no file system of its own: what it needs to
 * read - the IWAD (the game: by its hash when it is a dump the core knows,
 * else by its name and lumps; a fourth episode; Hexen's MAPINFO for the warp
 * numbers; the project pins its own hash), the PWADs and patches (their hashes, the manifest's; their
 * MAPINFO) - it asks the caller for by name. */
#ifndef LMP_IMPORT_H
#define LMP_IMPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a file the import reads, by name: its bytes and size, or NULL when there is
 * none; and, when found_name is not NULL and the file has a name of its own
 * (one found whatever its case), that name - a project's manifest names the
 * file as it is. The bytes and the name stay the caller's, and must last until
 * lmpi_import returns. */
typedef const unsigned char *(*lmpi_read_fn)(void *ctx, const char *name, size_t *size, const char **found_name);

struct lmpi_options
{
	const char *iwad;             /* the IWAD, a name the reader knows; NULL: the footer's -iwad */
	const char *const *pwads;     /* the PWADs and patches by name, in order; NULL: the footer's */
	int npwads;
	int no_pwads;                 /* none, whatever the footer names (an IWAD's own demos) */
	int longtics;                 /* a Heretic or Hexen demo recorded with -longtics its header does not say */
	int respawn, fast, nomonsters; /* a 1.2 demo's monster flags, which it does not hold */
	const char *missing_hint;     /* after "which is " when a file the footer names is not there */
	lmpi_read_fn read;
	void *ctx;
};

/* the project around the parts (NULL: the parts alone) */
struct lmpi_project
{
	const char *title;
	const char *source;           /* the demo's file name, for the description */
	const char *core_name, *core_version, *core_sha1;
};

/* what was imported, for a person to read: each a line, '\n'-separated notes */
struct lmpi_summary
{
	char format[96];
	char iwad[96];                /* the game, and the IWAD dump it is (or none the core knows) */
	char complevel[48];
	char players[16];
	char footer[512];
	char port[96];
	char notes[1024];
	long tics;
};

/* the demo as JSON (malloc'd: the caller frees it) - the parts ({"game",
 * "settings", "firmware", "files", "input", "frames", "format",
 * "tics", "players", "footer", "port", "notes"}), or with a project the whole
 * .chimeraProject - or NULL and the reason in err. summary may be NULL. */
char *lmpi_import(const unsigned char *demo, size_t size, const struct lmpi_options *o,
	const struct lmpi_project *project, struct lmpi_summary *summary, char *err, size_t errsize);

/* a SHA1 as upper-case hex */
void lmpi_sha1(const unsigned char *data, size_t size, char out[41]);

#ifdef __cplusplus
}
#endif

#endif
