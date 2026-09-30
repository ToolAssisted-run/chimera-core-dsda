/* dsda-driver.c - dsda-doom as a Chimera game core, driven as BizHawk's DSDA
 * core drives it (waterbox/dsda/BizhawkInterface.c and the C# of
 * Computers/Doom): the same command line from the same settings, the same
 * controllers turned into the same tic commands, the same automap and camera
 * controls, the same screen melt stepped a tic a frame and counted as lag.
 *
 * A step is one tic of the game, 1/35 s: the players' tic commands are set,
 * the game ticks (G_Ticker, as upstream's single-tics loop), the positional
 * sounds move, and the screen is drawn - or, while the screen melts, the melt
 * advances a tic instead (D_StepWipe, patches/0008) and the game waits. What
 * BizHawk keeps in its frontend - the turn-button hold counter, the shorttics
 * carry, the gamma the "Change Gamma" button cycles - is the machine's here,
 * in guest memory, so a savestate carries it.
 *
 * The engine's fatal errors (I_Error) and its exits halt the machine: it keeps
 * stepping, black and silent, and says why (chimera_exit). */
#include <limits.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "doomstat.h"
#include "doomdef.h"
#include "d_main.h"
#include "d_player.h"
#include "d_englsh.h"
#include "g_game.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "p_pspr.h"
#include "r_main.h"
#include "r_state.h"
#include "am_map.h"
#include "e6y.h"
#include "f_finale.h"
#include "f_wipe.h"
#include "i_sound.h"
#include "i_video.h"
#include "s_sound.h"
#include "lprintf.h"
#include "m_random.h"
#include "m_menu.h"
#include "heretic/def.h"
#include "dsda/configuration.h"
#include "dsda/messenger.h"
#include "dsda/palette.h"

#include "platform/chimera-platform.h"
#include "dsda-driver.h"
#include "dsda-input.h"

/* ------------------------------------------------------------ the games */

#include "dsda-versions.h"

/* ------------------------------------------------------------ the settings */

struct dsda_settings
{
	char version[32];
	/* BizHawk's sync settings */
	int scale_factor, internal_aspect;          /* aspect: 0 native, 1 16:9, 2 16:10, 3 4:3 */
	int complevel, skill, multiplayer_mode;
	int initial_episode, initial_map;
	int fast_monsters, monsters_respawn, no_monsters, pistol_start, coop_spawns, chain_episodes;
	int always_run, render_wipescreen, longtics;
	int mouse_turn_sensitivity, mouse_run_sensitivity, strafe50_turns;
	int prevent_level_exit, prevent_game_end, turbo;
	long rng_seed;
	int player_present[4], player_class[4];
	/* BizHawk's (non-sync) settings: part of the machine here, as Chimera has
	 * no cosmetic settings */
	int sfx_volume, music_volume, gamma, show_messages, report_secrets, hud_mode, exhud;
	int display_coordinates, display_commands, map_totals, map_time, map_coordinates;
	int map_overlay, map_details, map_trail, map_trail_size, full_vision, display_player;
};

static int leading_int(const char *s, int fallback)
{
	int v;
	return sscanf(s, "%d", &v) == 1 ? v : fallback;
}

static int enum_index(const char *value, const char *const *options, int n, int fallback)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(value, options[i])) return i;
	return fallback;
}

static const char *const k_aspects[] = { "Native", "16:9", "16:10", "4:3" };
static const char *const k_multiplayer[] = { "Single Player / Cooperative", "Deathmatch", "Alternate Deathmatch (v2.0)" };
static const char *const k_turning[] = { "16 bits (longtics)", "8 bits (shorttics)" };
static const char *const k_strafe50[] = { "Ignore", "Allow" };
static const char *const k_classes[] = { "Fighter", "Cleric", "Mage" };
static const char *const k_hud[] = { "Vanilla", "DSDA", "None" };
static const char *const k_overlay[] = { "Disabled", "Enabled", "Dark" };
static const char *const k_details[] = { "Normal", "Linedefs", "Linedefs and things" };

static void read_settings(struct dsda_settings *s)
{
	char str[64];
#define STR(name, def) (snprintf(str, sizeof str, "%s", def), wbx_setting_str(name, str, (int)sizeof str), str)
	snprintf(s->version, sizeof s->version, "%.31s", STR("version", k_versions[0].id));
	s->scale_factor = (int)wbx_setting_long("scaleFactor", 1);
	s->internal_aspect = enum_index(STR("internalAspect", "Native"), k_aspects, 4, 0);
	s->complevel = leading_int(STR("compatibilityLevel", "2 - Doom & Doom 2 v1.9"), 2);
	s->skill = leading_int(STR("skillLevel", "4 - Ultra-Violence"), 4);
	s->multiplayer_mode = enum_index(STR("multiplayerMode", k_multiplayer[0]), k_multiplayer, 3, 0);
	s->initial_episode = (int)wbx_setting_long("initialEpisode", 1);
	s->initial_map = (int)wbx_setting_long("initialMap", 1);
	s->fast_monsters = wbx_setting_bool("fastMonsters", 0);
	s->monsters_respawn = wbx_setting_bool("monstersRespawn", 0);
	s->no_monsters = wbx_setting_bool("noMonsters", 0);
	s->pistol_start = wbx_setting_bool("pistolStart", 0);
	s->coop_spawns = wbx_setting_bool("coopSpawns", 0);
	s->chain_episodes = wbx_setting_bool("chainEpisodes", 0);
	s->always_run = wbx_setting_bool("alwaysRun", 1);
	s->render_wipescreen = wbx_setting_bool("renderWipescreen", 1);
	s->longtics = enum_index(STR("turningResolution", k_turning[0]), k_turning, 2, 0) == 0;
	s->mouse_turn_sensitivity = (int)wbx_setting_long("mouseTurnSensitivity", 10);
	s->mouse_run_sensitivity = (int)wbx_setting_long("mouseRunSensitivity", 1);
	s->strafe50_turns = enum_index(STR("strafe50Turns", "Allow"), k_strafe50, 2, 1);
	s->prevent_level_exit = wbx_setting_bool("preventLevelExit", 0);
	s->prevent_game_end = wbx_setting_bool("preventGameEnd", 0);
	s->turbo = (int)wbx_setting_long("turbo", -1);
	s->rng_seed = wbx_setting_long("rngSeed", 1993);
	for (int i = 0; i < 4; i++)
	{
		char name[32];
		snprintf(name, sizeof name, "player%dPresent", i + 1);
		s->player_present[i] = wbx_setting_bool(name, i == 0);
		snprintf(name, sizeof name, "player%dClass", i + 1);
		s->player_class[i] = 1 + enum_index(STR(name, "Fighter"), k_classes, 3, 0);
	}
	s->sfx_volume = (int)wbx_setting_long("sfxVolume", 8);
	s->music_volume = (int)wbx_setting_long("musicVolume", 8);
	s->gamma = (int)wbx_setting_long("gamma", 0);
	s->show_messages = wbx_setting_bool("showMessages", 1);
	s->report_secrets = wbx_setting_bool("reportSecrets", 0);
	s->hud_mode = enum_index(STR("hudMode", "Vanilla"), k_hud, 3, 0);
	s->exhud = wbx_setting_bool("dsdaExHud", 0);
	s->display_coordinates = wbx_setting_bool("displayCoordinates", 0);
	s->display_commands = wbx_setting_bool("displayCommands", 0);
	s->map_totals = wbx_setting_bool("mapTotals", 0);
	s->map_time = wbx_setting_bool("mapTime", 0);
	s->map_coordinates = wbx_setting_bool("mapCoordinates", 0);
	s->map_overlay = enum_index(STR("mapOverlay", "Disabled"), k_overlay, 3, 0);
	s->map_details = enum_index(STR("mapDetails", "Normal"), k_details, 3, 0);
	s->map_trail = wbx_setting_bool("mapTrail", 0);
	s->map_trail_size = (int)wbx_setting_long("mapTrailSize", 105);
	s->full_vision = wbx_setting_bool("fullVision", 0);
	s->display_player = (int)wbx_setting_long("displayPlayer", 1);
#undef STR
}

/* ------------------------------------------------------------ the machine */

static struct
{
	struct dsda_settings s;
	const struct dsda_version *version;
	const struct dsda_controller *ctl;
	int init_done;
	int halted;
	char halt_reason[512];
	jmp_buf halt_jump;
	int halt_jump_set;

	uint64_t steps;
	uint64_t clock_ms;           /* tics, in milliseconds: tic * 1000 / 35 */
	int lag;                     /* the last step was the melt's */
	int wipe_done;

	int video_w, video_h;        /* the buffer (the resolution) */
	int aspect_x, aspect_y;

	int32_t axis[DSDA_MAX_INPUTS];
	uint8_t button[DSDA_MAX_INPUTS];
	/* BizHawk's frontend state, the machine's here */
	int turn_held[4];
	int turn_carry;
	int last_gamma_button;
	int last_player_buttons[4];
	int look_held[4];
	uint32_t last_automap;
	int automap_bigstate;
} g;

static uint32_t g_video[DSDA_VIDEO_MAX_W * DSDA_VIDEO_MAX_H];
#define AUDIO_MAX 4096
static int16_t g_audio[AUDIO_MAX * 2];
static int g_audio_n;

uint64_t chimera_clock_ms(void) { return g.clock_ms; }

void chimera_error_message(const char *msg)
{
	snprintf(g.halt_reason, sizeof g.halt_reason, "%s", msg);
}

/* the engine's exit: halt, and leave the engine where it stands - it is not
 * entered again */
void chimera_exit(int rc)
{
	if (!g.halt_reason[0])
		snprintf(g.halt_reason, sizeof g.halt_reason, rc ? "the engine stopped (%d)" : "the engine quit", rc);
	fprintf(stderr, "dsda: halted: %s\n", g.halt_reason);
	g.halted = 1;
	if (g.halt_jump_set) longjmp(g.halt_jump, 1);
	abort();
}

/* ------------------------------------------------------------ the controllers */

/* the value of a control this step: a button's (0/1), or an axis' */
static int control_value(int control, int port)
{
	for (int i = 0; i < g.ctl->nbuttons; i++)
		if (g.ctl->buttons[i].control == control && g.ctl->buttons[i].port == port) return g.button[i];
	for (int i = 0; i < g.ctl->naxes; i++)
		if (g.ctl->axes[i].control == control && g.ctl->axes[i].port == port) return g.axis[i];
	return 0;
}
#define PRESSED(control, port) (control_value(control, port) != 0)

int dsdadrv_button_count(void) { return g.ctl ? g.ctl->nbuttons : 0; }
int dsdadrv_axis_count(void) { return g.ctl ? g.ctl->naxes : 0; }

void dsdadrv_set_button(int index, int state)
{
	if (g.ctl && index >= 0 && index < g.ctl->nbuttons) g.button[index] = state ? 1 : 0;
}

void dsdadrv_set_axis(int index, int32_t value)
{
	if (!g.ctl || index < 0 || index >= g.ctl->naxes) return;
	const struct dsda_input *in = &g.ctl->axes[index];
	if (value < in->min) value = in->min;
	if (value > in->max) value = in->max;
	g.axis[index] = value;
}

/* a player not in the game has nothing; "Turn Speed Frac." is longtics' */
int dsdadrv_button_active(int index)
{
	if (!g.ctl || index < 0 || index >= g.ctl->nbuttons) return 0;
	const int port = g.ctl->buttons[index].port;
	return port == 0 || g.s.player_present[port - 1];
}

int dsdadrv_axis_active(int index)
{
	if (!g.ctl || index < 0 || index >= g.ctl->naxes) return 0;
	const struct dsda_input *in = &g.ctl->axes[index];
	if (in->control == C_TURN_FRAC && !g.s.longtics) return 0;
	return in->port == 0 || g.s.player_present[in->port - 1];
}

/* ------------------------------------------------------------ init */

static int present_count(void)
{
	int n = 0;
	for (int i = 0; i < 4; i++) n += g.s.player_present[i];
	return n;
}

/* BizHawk's resolutions (DSDA.cs _resolutions), by aspect: widescreen lowres
 * replacements that are not exactly 16:9 or 16:10, since the lowest multiple
 * of native height that is, 1280x720, does not divide nicely */
static const int k_resolutions[4][3][2] = {
	{ { 320, 200 } },
	{ { 428, 240 }, { 854, 480 }, { 1280, 720 } },
	{ { 426, 256 }, { 854, 512 }, { 1280, 768 } },
	{ { 320, 240 } },
};
static const int k_resolution_count[4] = { 1, 3, 3, 1 };

/* the configuration BizHawk's core writes (DSDA.cs _configFile) and its
 * render settings (BizhawkInterface.c render_updates), as upstream's -assign
 * (applied over the defaults when they load, without the callbacks a change
 * at run time fires) */
static void assign_config(void (*add)(const char *))
{
	const struct dsda_settings *s = &g.s;
	char kv[96];
#define ASSIGN(...) (snprintf(kv, sizeof kv, __VA_ARGS__), add(kv))
	ASSIGN("screen_resolution=%dx%d", g.video_w, g.video_h);
	/* native resolution is treated as 4:3, so the field of view is right at
	 * higher resolutions */
	ASSIGN("render_aspect=%d", s->internal_aspect == 0 ? 3 : s->internal_aspect);
	ASSIGN("render_wipescreen=%d", s->render_wipescreen);
	ASSIGN("render_stretch_hud=1");
	ASSIGN("uncapped_framerate=0");
	ASSIGN("dsda_show_level_splits=0");
	/* no rewind keyframes: the frontend rewinds, with the machine's own states;
	 * the ring would only grow every savestate (quickerDSDA does the same) */
	ASSIGN("dsda_auto_key_frame_depth=0");
	/* the music plays through upstream's OPL synthesizer */
	ASSIGN("snd_midiplayer=opl");
	ASSIGN("usegamma=%d", s->gamma);
	ASSIGN("automap_overlay=%d", s->map_overlay);
	ASSIGN("show_messages=%d", s->show_messages);
	ASSIGN("sfx_volume=%d", s->sfx_volume);
	ASSIGN("music_volume=%d", s->music_volume);
	ASSIGN("hudadd_secretarea=%d", s->report_secrets);
	ASSIGN("dsda_exhud=%d", s->exhud);
	ASSIGN("dsda_coordinate_display=%d", s->display_coordinates);
	ASSIGN("dsda_command_display=%d", s->display_commands);
	ASSIGN("map_totals=%d", s->map_totals);
	ASSIGN("map_time=%d", s->map_time);
	ASSIGN("map_coordinates=%d", s->map_coordinates);
	ASSIGN("screenblocks=%d", s->hud_mode != 0 ? 11 : 10);
	ASSIGN("hud_displayed=%d", s->hud_mode == 2 ? 0 : 1);
	ASSIGN("map_trail=%d", s->map_trail);
	ASSIGN("map_trail_size=%d", s->map_trail_size);
	if (s->full_vision)
	{
		ASSIGN("palette_ondamage=0");
		ASSIGN("palette_onbonus=0");
		ASSIGN("palette_onpowers=0");
	}
#undef ASSIGN
}

/* what is not configuration: the automap's details, the full vision's
 * colormap (BizhawkInterface.c render_updates) - after the setup */
static void apply_render_state(void)
{
	extern int dsda_reveal_map;
	dsda_reveal_map = g.s.map_details;
	if (g.s.full_vision)
		for (int i = 0; i < g_maxplayers; i++)
			if (playeringame[i])
			{
				players[i].fixedcolormap = 1;
				players[i].powers[pw_infrared] = -1;
			}
}

static char *g_argv[96];
static int g_argc;
static char g_argbuf[96][300];

static void arg(const char *s)
{
	if (g_argc >= 95) return;
	snprintf(g_argbuf[g_argc], sizeof g_argbuf[0], "%s", s);
	g_argv[g_argc] = g_argbuf[g_argc];
	g_argc++;
}

static void argi(int v)
{
	char b[16];
	snprintf(b, sizeof b, "%d", v);
	arg(b);
}

static int has_ext(const char *name, const char *ext)
{
	const size_t n = strlen(name), m = strlen(ext);
	return n >= m && !strcasecmp(name + n - m, ext);
}

int dsdadrv_init(char *err, int errsize)
{
	read_settings(&g.s);
	g.version = NULL;
	for (size_t i = 0; i < sizeof k_versions / sizeof k_versions[0]; i++)
		if (!strcmp(k_versions[i].id, g.s.version)) g.version = &k_versions[i];
	if (!g.version)
	{
		snprintf(err, (size_t)errsize, "the setting version is \"%s\", which is none of the IWADs the core knows", g.s.version);
		return 0;
	}
	{
		/* the machine (Chimera's System) is the version's game */
		char game[32] = "";
		wbx_setting_str("game", game, (int)sizeof game);
		if (game[0] && strcmp(game, g.version->game))
		{
			snprintf(err, (size_t)errsize, "the version %s is %s's, not the game %s's", g.version->id, g.version->game, game);
			return 0;
		}
	}
	g.ctl = dsda_controller(g.version->format);
	for (int i = 0; i < g.ctl->naxes; i++) g.axis[i] = g.ctl->axes[i].neutral;

	/* the refusals BizHawk's frontend makes */
	if (!present_count())
	{
		snprintf(err, (size_t)errsize, "no player is present: player1Present .. player4Present");
		return 0;
	}
	if (g.s.display_player < 1 || g.s.display_player > 4 || !g.s.player_present[g.s.display_player - 1])
	{
		for (int i = 0; i < 4; i++)
			if (g.s.player_present[i]) { g.s.display_player = i + 1; break; }
	}
	if (g.s.scale_factor < 1 || g.s.scale_factor > 12)
	{
		snprintf(err, (size_t)errsize, "the setting scaleFactor is %d; it goes from 1 to 12", g.s.scale_factor);
		return 0;
	}

	/* the resolution (DSDA.cs) */
	{
		const int a = g.s.internal_aspect, index = g.s.scale_factor - 1;
		int w, h, mult = 1;
		if (index < k_resolution_count[a])
		{
			w = k_resolutions[a][index][0];
			h = k_resolutions[a][index][1];
		}
		else
		{
			mult = g.s.scale_factor - k_resolution_count[a] + 1;
			w = k_resolutions[a][k_resolution_count[a] - 1][0];
			h = k_resolutions[a][k_resolution_count[a] - 1][1];
		}
		g.video_w = w * mult;
		g.video_h = h * mult;
		if (g.video_w > DSDA_VIDEO_MAX_W || g.video_h > DSDA_VIDEO_MAX_H)
		{
			snprintf(err, (size_t)errsize, "scaleFactor %d at %s is %dx%d, larger than the core's %dx%d",
				g.s.scale_factor, k_aspects[a], g.video_w, g.video_h, DSDA_VIDEO_MAX_W, DSDA_VIDEO_MAX_H);
			return 0;
		}
		/* native: 320x200's multiples, shown at 4:3 as on a CRT; the others
		 * are drawn corrected */
		g.aspect_x = a == 0 ? 4 : g.video_w;
		g.aspect_y = a == 0 ? 3 : g.video_h;
	}

	/* the IWAD, as the version's firmware; the PWADs and patches, as the
	 * project's "pwad" slot has them, in order */
	FILE *f = fopen(g.version->iwad, "rb");
	if (!f)
	{
		snprintf(err, (size_t)errsize, "%s needs its IWAD, %s (firmware), which is not there", g.version->id, g.version->iwad);
		return 0;
	}
	fclose(f);

	g_argc = 0;
	arg("dsda");
	/* nothing of the host's: no configuration file, no autoloaded WADs, no
	 * data folder (savegames, stats, cached tables) - places that are not */
	arg("-config"); arg("/chimera-none/dsda-doom.cfg");
	arg("-data"); arg("/chimera-none");
	arg("-noautoload");
	arg("-iwad"); arg(g.version->iwad);
	{
		char name[256];
		int nfile = 0;
		for (int i = 0; wbx_slot_name("pwad", i, name, (int)sizeof name); i++)
		{
			if (has_ext(name, ".deh") || has_ext(name, ".bex")) continue;
			if (!nfile++) arg("-file");
			arg(name);
		}
		int ndeh = 0;
		for (int i = 0; wbx_slot_name("pwad", i, name, (int)sizeof name); i++)
		{
			if (!(has_ext(name, ".deh") || has_ext(name, ".bex"))) continue;
			if (!ndeh++) arg("-deh");
			arg(name);
		}
	}
	/* BizHawk's CreateArguments */
	arg("-warp");
	if (g.s.initial_episode != 0 && g.version->episodic) argi(g.s.initial_episode);
	argi(g.s.initial_map);
	arg("-skill"); argi(g.s.skill);
	arg("-complevel"); argi(g.s.complevel);
	if (g.s.fast_monsters) arg("-fast");
	if (g.s.monsters_respawn) arg("-respawn");
	if (g.s.no_monsters) arg("-nomonsters");
	if (g.s.pistol_start) arg("-pistolstart");
	if (g.s.coop_spawns) arg("-coop_spawns");
	if (g.s.chain_episodes) arg("-chain_episodes");
	if (g.s.longtics) arg("-longtics");
	if (g.s.multiplayer_mode == 1) arg("-deathmatch");
	if (g.s.multiplayer_mode == 2) arg("-altdeath");
	if (g.s.turbo > 0) { arg("-turbo"); argi(g.s.turbo); }
	if (present_count() > 1) arg("-solo-net");
	if (g.s.complevel >= 9) { arg("-rngseed"); argi((int)g.s.rng_seed); }
	arg("-assign");
	assign_config(arg);
#ifdef DSDA_GATE_HOOKS
	/* the native reference only (native.mk): the gate's demo leg has the
	 * engine play a demo itself (-playdemo) - more arguments, from the work
	 * folder's gate-args */
	{
		FILE *ga = fopen("gate-args", "r");
		char w[256];
		if (ga)
		{
			while (fscanf(ga, "%255s", w) == 1) arg(w);
			fclose(ga);
		}
	}
#endif
	g_argv[g_argc] = NULL;

	/* the players, before the engine starts (patches/0004) */
	for (int i = 0; i < 4; i++)
	{
		playeringame[i] = g.s.player_present[i];
		/* a class is Hexen's; the other games' players have none */
		PlayerClass[i] = g.version->format == FORMAT_HEXEN ? (pclass_t)g.s.player_class[i] : PCLASS_NULL;
	}
	displayplayer = consoleplayer = g.s.display_player - 1;

	frontend_steps_wipe = true;
	g.wipe_done = 1;
	I_SetSoundCap();

	if (setjmp(g.halt_jump))
	{
		g.halt_jump_set = 0;
		snprintf(err, (size_t)errsize, "%s", g.halt_reason);
		return 0;
	}
	g.halt_jump_set = 1;
	chimera_dsda_start(g_argc, g_argv, NULL);
	I_InitSound();
	apply_render_state();
	preventLevelExit = g.s.prevent_level_exit;
	preventGameEnd = g.s.prevent_game_end;
	g.halt_jump_set = 0;

	/* the first picture, before any step (BizHawk's dsda_init_video) */
	D_Display(-1);
	g.init_done = 1;
	return 1;
}

/* ------------------------------------------------------------ a step */

/* BizHawk's run, strafe and turn speeds (DSDA.cs) */
static const int k_run_speeds[2] = { 25, 50 };
static const int k_strafe_speeds[2] = { 24, 40 };
static const int k_turn_speeds[3] = { 640, 1280, 320 };

enum
{
	BUTTON_INVENTORY_LEFT = 1 << 3,
	BUTTON_INVENTORY_RIGHT = 1 << 4,
	BUTTON_ARTIFACT_USE = 1 << 6,
	BUTTON_LOOK_UP = 1 << 7,
	BUTTON_LOOK_DOWN = 1 << 8,
	BUTTON_LOOK_CENTER = 1 << 9,
	BUTTON_FLY_UP = 1 << 10,
	BUTTON_FLY_DOWN = 1 << 11,
	BUTTON_FLY_CENTER = 1 << 12,
	ARTI_END_PLAYER = 1 << 6,
	ARTI_JUMP = 1 << 7,
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* normally keyboard input ends the cast: none reaches the game, so a
 * movement does (BizHawk's finale_inputs) */
static void finale_inputs(void)
{
	if (gamestate != GS_FINALE) return;
	event_t event;
	memset(&event, 0, sizeof event);
	event.type = ev_keydown;
	F_Responder(&event);
}

/* a player's tic command, from their controller (BizHawk's FrameAdvance, then
 * player_input) */
static void player_input(int i)
{
	const int port = i + 1;
	player_t *player = &players[i];
	/* the slot G_Ticker reads this tic */
	ticcmd_t *dest = &local_cmds[i][gametic % BACKUPTICS];
	const int strafe = PRESSED(C_STRAFE, port);
	const int speed = PRESSED(C_RUN, port) || g.s.always_run;
	int turn_speed = 0;
	int run, strafing, turning, weapon, buttons = 0, flylook = 0, arti = 0;

	memset(dest, 0, sizeof *dest);

	/* a lower speed for tapping the turn buttons */
	if (PRESSED(C_TURN_RIGHT, port) || PRESSED(C_TURN_LEFT, port))
	{
		g.turn_held[i]++;
		turn_speed = g.turn_held[i] < 6 ? k_turn_speeds[2] : k_turn_speeds[speed];
	}
	else
		g.turn_held[i] = 0;

	run = control_value(C_RUN_SPEED, port);
	strafing = control_value(C_STRAFE_SPEED, port);
	weapon = control_value(C_WEAPON_SELECT, port);
	/* the core counts angles counterclockwise */
	turning = control_value(C_TURN_SPEED, port) << 8;
	if (g.s.longtics) turning += control_value(C_TURN_FRAC, port);

	/* the weapon buttons override the axis, a higher one a lower */
	static const int weapon_controls[7] = { C_WEAPON_1, C_WEAPON_2, C_WEAPON_3, C_WEAPON_4, C_WEAPON_5, C_WEAPON_6, C_WEAPON_7 };
	for (int unit = 1; unit <= 7; unit++)
		if (PRESSED(weapon_controls[unit - 1], port)) weapon = unit;

	if (PRESSED(C_FORWARD, port)) run = k_run_speeds[speed];
	if (PRESSED(C_BACKWARD, port)) run = -k_run_speeds[speed];
	/* turning with strafe held is ADDED to these: strafe50 */
	if (PRESSED(C_STRAFE_RIGHT, port)) strafing = k_strafe_speeds[speed];
	if (PRESSED(C_STRAFE_LEFT, port)) strafing = -k_strafe_speeds[speed];
	if (strafe)
	{
		if (PRESSED(C_TURN_RIGHT, port)) strafing += k_strafe_speeds[speed];
		if (PRESSED(C_TURN_LEFT, port)) strafing -= k_strafe_speeds[speed];
	}
	else
	{
		if (PRESSED(C_TURN_RIGHT, port)) turning -= turn_speed;
		if (PRESSED(C_TURN_LEFT, port)) turning += turn_speed;
	}

	/* the mouse: running (the divider the core's), turning */
	run -= (int)(control_value(C_MOUSE_RUN, port) * g.s.mouse_run_sensitivity / 8.0);
	run = clampi(run, -k_run_speeds[1], k_run_speeds[1]);
	{
		const int mouse = control_value(C_MOUSE_TURN, port) * g.s.mouse_turn_sensitivity;
		if (strafe) strafing += mouse / 5;
		else turning -= mouse;
	}
	/* strafe speed is limited to the max run speed, NOT the max strafe speed */
	strafing = clampi(strafing, -k_run_speeds[1], k_run_speeds[1]);

	/* shorttics: one byte in movies, two in the core */
	if (!g.s.longtics)
	{
		const int desired = turning + g.turn_carry;
		turning = (desired + 128) & 0xff00;
		g.turn_carry = desired - turning;
		turning = ((turning + 128) >> 8) << 8;
	}
	if (g.s.strafe50_turns == 0 && abs(strafing) > k_strafe_speeds[1])
		turning = 0;

	if (PRESSED(C_FIRE, port)) buttons |= BT_ATTACK;
	if (PRESSED(C_USE, port)) buttons |= BT_USE;
	if (weapon > 0) buttons |= BT_CHANGE;

	if (g.version->format != FORMAT_DOOM)
	{
		int look = control_value(C_LOOK, port), fly = control_value(C_FLY, port);
		if (look < 0) look += 16;
		if (fly < 0) fly += 16;
		flylook = (fly << 4) + look;
		arti = control_value(C_USE_ARTIFACT_AXIS, port);
		if (PRESSED(C_INVENTORY_LEFT, port)) buttons |= BUTTON_INVENTORY_LEFT;
		if (PRESSED(C_INVENTORY_RIGHT, port)) buttons |= BUTTON_INVENTORY_RIGHT;
		if (PRESSED(C_USE_ARTIFACT, port)) buttons |= BUTTON_ARTIFACT_USE;
		if (PRESSED(C_LOOK_UP, port)) buttons |= BUTTON_LOOK_UP;
		if (PRESSED(C_LOOK_DOWN, port)) buttons |= BUTTON_LOOK_DOWN;
		if (PRESSED(C_LOOK_CENTER, port)) buttons |= BUTTON_LOOK_CENTER;
		if (PRESSED(C_FLY_UP, port)) buttons |= BUTTON_FLY_UP;
		if (PRESSED(C_FLY_DOWN, port)) buttons |= BUTTON_FLY_DOWN;
		if (PRESSED(C_FLY_CENTER, port)) buttons |= BUTTON_FLY_CENTER;
		if (g.version->format == FORMAT_HEXEN)
		{
			if (PRESSED(C_JUMP, port)) arti |= ARTI_JUMP;
			if (PRESSED(C_END_PLAYER, port)) arti |= ARTI_END_PLAYER;
		}
	}

	/* BizHawkInterface.c player_input */
	{
		const int extra = buttons & 0x1ff8;
		int lspeed, look = 0, flyheight = 0;
		dest->forwardmove = (signed char)run;
		dest->sidemove = (signed char)strafing;
		dest->lookfly = (unsigned char)flylook;
		dest->arti = (unsigned char)arti;
		dest->angleturn = (short)turning;
		dest->buttons = (byte)(buttons & 0x7);

		if (extra & BUTTON_INVENTORY_LEFT && !(g.last_player_buttons[i] & BUTTON_INVENTORY_LEFT)) InventoryMoveLeft();
		if (extra & BUTTON_INVENTORY_RIGHT && !(g.last_player_buttons[i] & BUTTON_INVENTORY_RIGHT)) InventoryMoveRight();

		/* the rest is G_BuildTiccmd's */
		if (extra & BUTTON_ARTIFACT_USE && !(g.last_player_buttons[i] & BUTTON_ARTIFACT_USE))
		{
			if (inventory)
			{
				player->readyArtifact = player->inventory[inv_ptr].type;
				inventory = false;
				dest->arti &= ~AFLAG_MASK;
			}
			else
				dest->arti |= player->inventory[inv_ptr].type & AFLAG_MASK;
		}
		if (extra & (BUTTON_LOOK_DOWN | BUTTON_LOOK_UP)) ++g.look_held[i];
		else g.look_held[i] = 0;
		lspeed = g.look_held[i] < 6 ? 1 : 2;
		if (extra & BUTTON_LOOK_UP) look = lspeed;
		if (extra & BUTTON_LOOK_DOWN) look = -lspeed;
		if (extra & BUTTON_LOOK_CENTER) look = TOCENTER;
		if (extra & BUTTON_FLY_UP) flyheight = 5;
		if (extra & BUTTON_FLY_DOWN) flyheight = -5;
		if (extra & BUTTON_FLY_CENTER) { flyheight = TOCENTER; look = TOCENTER; }
		if (look != 0 && player->playerstate == PST_LIVE)
		{
			if (look < 0) look += 16;
			dest->lookfly = look;
		}
		if (flyheight != 0)
		{
			if (flyheight < 0) flyheight += 16;
			dest->lookfly |= flyheight << 4;
		}

		if (dest->buttons & BT_CHANGE)
		{
			int newweapon = weapon - 1;
			if (!demo_compatibility)
			{
				if (newweapon == wp_fist && player->weaponowned[wp_chainsaw] && player->readyweapon != wp_chainsaw
					&& (player->readyweapon == wp_fist || !player->powers[pw_strength] || P_WeaponPreferred(wp_chainsaw, wp_fist)))
					newweapon = wp_chainsaw;
				if (newweapon == wp_shotgun && gamemode == commercial && player->weaponowned[wp_supershotgun]
					&& (!player->weaponowned[wp_shotgun] || player->readyweapon == wp_shotgun
						|| (player->readyweapon != wp_supershotgun && P_WeaponPreferred(wp_supershotgun, wp_shotgun))))
					newweapon = wp_supershotgun;
			}
			dest->buttons |= newweapon << BT_WEAPONSHIFT;
		}
		if (dest->forwardmove || dest->sidemove || dest->lookfly || dest->arti)
			finale_inputs();
		g.last_player_buttons[i] = extra;
	}
}

/* the automap's controls (BizHawkInterface.c automap_inputs) */
#define FTOM(x) FixedMul(((x) << 16), scale_ftom)
#define M_ZOOMIN ((int)((float)FRACUNIT * (1.00f + map_scroll_speed / 200.0f)))
#define M_ZOOMOUT ((int)((float)FRACUNIT / (1.00f + map_scroll_speed / 200.0f)))

static void automap_inputs(uint32_t b)
{
	const uint32_t edge = b & ~g.last_automap;
#define AM(bit) (1u << (bit))
	m_paninc.x = m_paninc.y = 0;
	if (edge & AM(0))
	{
		if (automap_on) { AM_Stop(true); g.automap_bigstate = 0; }
		else AM_Start(true);
	}
	if (edge & AM(4))
	{
		dsda_ToggleConfig(dsda_config_automap_follow, true);
		dsda_AddMessage(automap_follow ? AMSTR_FOLLOWON : AMSTR_FOLLOWOFF);
	}
	if (edge & AM(9))
	{
		dsda_ToggleConfig(dsda_config_automap_grid, true);
		dsda_AddMessage(automap_grid ? AMSTR_GRIDON : AMSTR_GRIDOFF);
	}
	if (edge & AM(10) && !raven)
	{
		AM_addMark();
		doom_printf("%s %d", AMSTR_MARKEDSPOT, markpointnum - 1);
	}
	if (edge & AM(11))
	{
		AM_clearMarks();
		dsda_AddMessage(AMSTR_MARKSCLEARED);
	}
	if (edge & AM(3))
	{
		g.automap_bigstate = !g.automap_bigstate;
		if (g.automap_bigstate) { AM_saveScaleAndLoc(); AM_minOutWindowScale(); }
		else AM_restoreScaleAndLoc();
	}
	if (b & AM(2))
	{
		mtof_zoommul = M_ZOOMOUT;
		ftom_zoommul = M_ZOOMIN;
		curr_mtof_zoommul = mtof_zoommul;
		zoom_leveltime = leveltime;
	}
	else if (b & AM(1))
	{
		mtof_zoommul = M_ZOOMIN;
		ftom_zoommul = M_ZOOMOUT;
		curr_mtof_zoommul = mtof_zoommul;
		zoom_leveltime = leveltime;
	}
	else
	{
		stop_zooming = true;
		if (leveltime != zoom_leveltime) AM_StopZooming();
	}
	if (!automap_follow)
	{
		if (b & AM(5)) m_paninc.y += FTOM(map_pan_speed);
		if (b & AM(6)) m_paninc.y -= FTOM(map_pan_speed);
		if (b & AM(7)) m_paninc.x += FTOM(map_pan_speed);
		if (b & AM(8)) m_paninc.x -= FTOM(map_pan_speed);
	}
#undef AM
	g.last_automap = b;
}

/* the walking camera (BizHawkInterface.c walkcam_inputs): Camera Mode is
 * its type, the rest move it */
static void walkcam_inputs(void)
{
	const int mode = control_value(C_CAMERA_MODE, 0);
	mobj_t *mo = players[consoleplayer].mo;
	if (!mo) return;
	sector_t *sec = R_PointInSector(mo->x, mo->y);
	if (mode != walkcamera.type && mode >= 0)
	{
		walkcamera.type = mode;
		walkcamera.z = sec->floorheight + 41 * FRACUNIT;
		P_SyncWalkcam(true, true);
	}
	if (!walkcamera.type) return;
	if (PRESSED(C_CAMERA_RESET, 0))
	{
		walkcamera.x = mo->x;
		walkcamera.y = mo->y;
		walkcamera.z = sec->floorheight + 41 * FRACUNIT;
		walkcamera.angle = mo->angle;
	}
	{
		const int run = control_value(C_CAMERA_RUN_SPEED, 0);
		const int strafe = control_value(C_CAMERA_STRAFE_SPEED, 0);
		const int turn = control_value(C_CAMERA_TURN_SPEED, 0) << 8;
		walkcamera.x += FixedMul((ORIG_FRICTION / 4) * run, finecosine[walkcamera.angle >> ANGLETOFINESHIFT]);
		walkcamera.y += FixedMul((ORIG_FRICTION / 4) * run, finesine[walkcamera.angle >> ANGLETOFINESHIFT]);
		walkcamera.x += FixedMul((ORIG_FRICTION / 6) * strafe, finecosine[(walkcamera.angle - ANG90) >> ANGLETOFINESHIFT]);
		walkcamera.y += FixedMul((ORIG_FRICTION / 6) * strafe, finesine[(walkcamera.angle - ANG90) >> ANGLETOFINESHIFT]);
		walkcamera.z += control_value(C_CAMERA_FLY, 0) * FRACUNIT;
		walkcamera.angle += (turn / 8) << ANGLETOFINESHIFT;
	}
}

static void update_domains(void);

void dsdadrv_frame(int render)
{
	g.steps++;
	g.clock_ms = g.steps * 1000 / TICRATE;
	g_audio_n = 0;
	if (g.halted || !g.init_done)
	{
		g.lag = 1;
		memset(g_audio, 0, sizeof g_audio[0] * 2 * (snd_samplerate / TICRATE));
		g_audio_n = snd_samplerate / TICRATE;
		return;
	}
	if (setjmp(g.halt_jump))
	{
		g.halt_jump_set = 0;
		g.lag = 1;
		memset(g_video, 0, sizeof(uint32_t) * g.video_w * g.video_h);
		return;
	}
	g.halt_jump_set = 1;

	/* "Change Gamma" cycles 0..4, from the setting (BizHawk's frontend) */
	{
		const int gamma_button = PRESSED(C_CHANGE_GAMMA, 0);
		if (gamma_button && !g.last_gamma_button)
		{
			g.s.gamma = (g.s.gamma + 1) % 5;
			dsda_UpdateIntConfig(dsda_config_usegamma, g.s.gamma, true);
		}
		g.last_gamma_button = gamma_button;
	}

	{
		uint32_t automap = 0;
		static const int automap_controls[12] = {
			C_AUTOMAP_TOGGLE, C_AUTOMAP_ZOOM_IN, C_AUTOMAP_ZOOM_OUT, C_AUTOMAP_FULL_ZOOM, C_AUTOMAP_FOLLOW, C_AUTOMAP_UP,
			C_AUTOMAP_DOWN, C_AUTOMAP_RIGHT, C_AUTOMAP_LEFT, C_AUTOMAP_GRID, C_AUTOMAP_MARK, C_AUTOMAP_CLEAR_MARKS };
		for (int i = 0; i < 12; i++)
			if (PRESSED(automap_controls[i], 0)) automap |= 1u << i;
		if (gamestate == GS_LEVEL)
		{
			automap_inputs(automap);
			walkcam_inputs();
		}
		if (automap) finale_inputs();
	}

	for (int i = 0; i < 4; i++)
		if (g.s.player_present[i]) player_input(i);

	/* turbo does not stop the engine drawing: D_Display starts the melt, keeps
	 * the status bar's and the border's redraw state, and the view marks the
	 * lines and sectors it draws - an undrawn step would be another machine.
	 * Turbo only skips the picture's conversion. */

	if (wipe_in_progress)
	{
		g.wipe_done = D_StepWipe();
		g.lag = 1;
	}
	else
	{
		/* upstream's single-tics loop */
		if (advancedemo) D_DoAdvanceDemo();
		M_Ticker();
		G_Ticker();
		gametic++;
		maketic = gametic;
		if (players[displayplayer].mo) S_UpdateSounds();
		D_Display(-1);
		g.lag = 0;
	}

	/* the step's sound: 1/35 s of the engine's mixer */
	{
		const int n = snd_samplerate / TICRATE;
		const int16_t *s = (const int16_t *)I_GrabSound(n);
		g_audio_n = n;
		if (s) memcpy(g_audio, s, sizeof(int16_t) * 2 * n);
		else memset(g_audio, 0, sizeof(int16_t) * 2 * n);
	}
	g.halt_jump_set = 0;

	if (render) chimera_video_bgra(g_video);
	update_domains();
}

const uint32_t *dsdadrv_video(int *w, int *h)
{
	*w = g.video_w;
	*h = g.video_h;
	return g_video;
}

void dsdadrv_aspect(int *x, int *y)
{
	*x = g.aspect_x;
	*y = g.aspect_y;
}

const int16_t *dsdadrv_audio(int *n)
{
	*n = g_audio_n;
	return g_audio;
}

int dsdadrv_input_was_read(void) { return !g.lag; }
uint64_t dsdadrv_clock(void) { return g.clock_ms; }

/* ------------------------------------------------------------ memory */

/* BizHawk's artificial domains (BizhawkInterface.c dsda_read_memory_array):
 * the players, the things (in thinker order, as xdre tracks them), the lines
 * and the sectors, each record padded to a round size; bytes of no record are
 * 0x88, as BizHawk's MEMORY_NULL. Copies, made after each step: reading them
 * never touches the engine. */
#define PAD_PLAYER 0x400
#define PAD_THING 0x200
#define PAD_LINE 0x100
#define PAD_SECTOR 0x200
#define MEMORY_NULL 0x88

/* a domain's records: the buffer grows with the largest count seen (so a
 * savestate carries only what was ever used), and records gone since the last
 * step are MEMORY_NULL again */
struct record_domain
{
	uint8_t *data;
	size_t capacity;   /* bytes */
	int used;          /* records */
};

static void copy_records(struct record_domain *d, int count, int pad, const void *base, size_t size, const void *const *ptrs)
{
	const size_t need = (size_t)count * pad;
	if (need > d->capacity)
	{
		size_t cap = d->capacity ? d->capacity : (size_t)pad * 64;
		while (cap < need) cap *= 2;
		d->data = realloc(d->data, cap);
		memset(d->data + d->capacity, MEMORY_NULL, cap - d->capacity);
		d->capacity = cap;
	}
	for (int i = 0; i < count; i++)
	{
		uint8_t *rec = d->data + (size_t)i * pad;
		const void *src = ptrs ? ptrs[i] : (const uint8_t *)base + (size_t)i * size;
		memcpy(rec, src, size < (size_t)pad ? size : (size_t)pad);
		if (size < (size_t)pad) memset(rec + size, MEMORY_NULL, (size_t)pad - size);
	}
	if (d->used > count) memset(d->data + (size_t)count * pad, MEMORY_NULL, (size_t)(d->used - count) * pad);
	d->used = count;
}

struct game_state
{
	int32_t gametic;       /* 0 */
	int32_t leveltime;     /* 4 */
	int32_t gamestate;     /* 8 */
	int32_t gameepisode;   /* 12 */
	int32_t gamemap;       /* 16 */
	int32_t gameskill;     /* 20 */
	int32_t complevel;     /* 24 */
	int32_t things;        /* 28 */
	int32_t lines;         /* 32 */
	int32_t sectors;       /* 36 */
	int32_t totalkills;    /* 40 */
	int32_t totalitems;    /* 44 */
	int32_t totalsecret;   /* 48 */
	uint8_t level_exit;    /* 52 */
	uint8_t game_end;      /* 53 */
	uint8_t halted;        /* 54 */
	uint8_t lag;           /* 55 */
	uint64_t steps;        /* 56 */
	int32_t rng_index;     /* 64 */
	int32_t prng_index;    /* 68 */
	/* 72: each player's thing, where it is (0 without one) */
	struct { int32_t x, y, z; uint32_t angle; int32_t momx, momy, momz, pad; } player[4];
};

static struct game_state g_state;
static uint8_t g_players_domain[4 * PAD_PLAYER];
static struct record_domain g_things, g_lines, g_sectors;
static const void **g_thing_ptrs;
static int g_thing_ptrs_cap;

static void update_domains(void)
{
	for (int i = 0; i < 4; i++)
	{
		uint8_t *rec = g_players_domain + i * PAD_PLAYER;
		memset(rec, MEMORY_NULL, PAD_PLAYER);
		if (playeringame[i]) memcpy(rec, &players[i], sizeof(player_t) < PAD_PLAYER ? sizeof(player_t) : PAD_PLAYER);
	}
	int nthings = 0;
	if (gamestate == GS_LEVEL)
		for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
			if (th->function == P_MobjThinker || th->function == P_BlasterMobjThinker)
			{
				if (nthings == g_thing_ptrs_cap)
				{
					g_thing_ptrs_cap = g_thing_ptrs_cap ? g_thing_ptrs_cap * 2 : 256;
					g_thing_ptrs = realloc(g_thing_ptrs, sizeof *g_thing_ptrs * (size_t)g_thing_ptrs_cap);
				}
				g_thing_ptrs[nthings++] = th;
			}
	copy_records(&g_things, nthings, PAD_THING, NULL, sizeof(mobj_t), g_thing_ptrs);
	copy_records(&g_lines, gamestate == GS_LEVEL ? numlines : 0, PAD_LINE, lines, sizeof(line_t), NULL);
	copy_records(&g_sectors, gamestate == GS_LEVEL ? numsectors : 0, PAD_SECTOR, sectors, sizeof(sector_t), NULL);

	g_state.gametic = gametic;
	g_state.leveltime = leveltime;
	g_state.gamestate = gamestate;
	g_state.gameepisode = gameepisode;
	g_state.gamemap = gamemap;
	g_state.gameskill = gameskill;
	g_state.complevel = compatibility_level;
	g_state.things = nthings;
	g_state.lines = g_lines.used;
	g_state.sectors = g_sectors.used;
	g_state.totalkills = totalkills;
	g_state.totalitems = totalitems;
	g_state.totalsecret = totalsecret;
	g_state.level_exit = reachedLevelExit != 0;
	g_state.game_end = reachedGameEnd != 0;
	g_state.halted = (uint8_t)g.halted;
	g_state.lag = (uint8_t)g.lag;
	g_state.steps = g.steps;
	g_state.rng_index = rng.rndindex;
	g_state.prng_index = rng.prndindex;
	for (int i = 0; i < 4; i++)
	{
		const mobj_t *mo = playeringame[i] ? players[i].mo : NULL;
		memset(&g_state.player[i], 0, sizeof g_state.player[i]);
		if (!mo) continue;
		g_state.player[i].x = mo->x;
		g_state.player[i].y = mo->y;
		g_state.player[i].z = mo->z;
		g_state.player[i].angle = mo->angle;
		g_state.player[i].momx = mo->momx;
		g_state.player[i].momy = mo->momy;
		g_state.player[i].momz = mo->momz;
	}
}

static const struct { const char *name; } k_domains[] = {
	{ "Game State" }, { "Players" }, { "Things" }, { "Lines" }, { "Sectors" },
};

int dsdadrv_domain_count(void) { return (int)(sizeof k_domains / sizeof k_domains[0]); }
const char *dsdadrv_domain_name(int i) { return i >= 0 && i < dsdadrv_domain_count() ? k_domains[i].name : ""; }

uint8_t *dsdadrv_domain_ptr(int i)
{
	switch (i)
	{
	case 0: return (uint8_t *)&g_state;
	case 1: return g_players_domain;
	case 2: return g_things.data;
	case 3: return g_lines.data;
	case 4: return g_sectors.data;
	}
	return NULL;
}

/* the record domains are as large as the most records they have held */
int64_t dsdadrv_domain_size(int i)
{
	switch (i)
	{
	case 0: return sizeof g_state;
	case 1: return sizeof g_players_domain;
	case 2: return (int64_t)g_things.capacity;
	case 3: return (int64_t)g_lines.capacity;
	case 4: return (int64_t)g_sectors.capacity;
	}
	return 0;
}

int dsdadrv_domain_writable(int i) { (void)i; return 0; }

#include "dsda-properties.h"
