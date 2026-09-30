/* i_main.c - dsda-doom's start and exit without a process of its own
 * (upstream's SDL/i_main.c): chimera_dsda_start runs what upstream's main runs
 * before its loop - the command line, the configuration's defaults, the
 * version line, then D_DoomMainSetup (patches/0001) - and the loop is the
 * driver's, one tic a step. There are no signal handlers and no process
 * priority to set; an exit (I_SafeExit: the engine's I_Error, or a quit) does
 * not end a process but halts the machine, which keeps stepping (the
 * driver's chimera_exit). */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "doomdef.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"
#include "i_main.h"
#include "z_zone.h"
#include "lprintf.h"
#include "doomstat.h"
#include "g_game.h"
#include "m_misc.h"
#include "e6y.h"
#include "dsda/args.h"
#include "dsda/time.h"

#include "chimera-platform.h"

int signal_context;

void I_Init(void)
{
	dsda_ResetTimeFunctions(fastdemo);
	I_InitSound();
}

void I_Init2(void)
{
	dsda_ResetTimeFunctions(fastdemo);
	force_singletics_to = gametic + BACKUPTICS;
}

dboolean I_Interrupted(void) { return false; }

void I_SetProcessPriority(void) {}

/* the functions upstream runs at exit save the configuration, the demo being
 * recorded, the ENDOOM screen: none of it is the machine's, so none is kept */
void I_AtExit(atexit_func_t func, dboolean run_on_error, const char *name, exit_priority_t priority)
{
	(void)func; (void)run_on_error; (void)name; (void)priority;
}

void I_SafeExit(int rc)
{
	chimera_exit(rc);
}

void chimera_dsda_start(int argc, char **argv, void (*configure)(void))
{
	char vbuf[200];

	dsda_ParseCommandLineArgs(argc, argv);
	if (dsda_Flag(dsda_arg_verbose)) I_EnableVerboseLogging();
	if (dsda_Flag(dsda_arg_quiet)) I_DisableAllLogging();

	/* e6y: conflicting command-line parameters */
	ParamsMatchingCheck();

	lprintf(LO_DEBUG, "M_LoadDefaults: Load system defaults.\n");
	M_LoadDefaults();
	/* the frontend's configuration, over the defaults, before the setup reads it */
	if (configure) configure();
	lprintf(LO_INFO, "%s\n", I_GetVersionString(vbuf, 200));

	I_PreInitGraphics();
	D_DoomMainSetup();
}
