/* i_video.c - dsda-doom's video layer without a window (upstream's
 * SDL/i_video.c, less SDL): the engine draws its 8-bit screen with the
 * software renderer as upstream does, and the core turns it into BGRA through
 * the palette the engine last asked for (chimera_video_bgra).
 *
 * What upstream decides by the host - the screen pitch it picks by timing the
 * CPU's cache misses (I_TestCPUCacheMisses), the window, the mouse, the
 * display's modes - is fixed here: the pitch is the width rounded up to 16,
 * so every machine lays the screen out alike. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "r_things.h"
#include "r_plane.h"
#include "r_main.h"
#include "f_wipe.h"
#include "d_main.h"
#include "d_event.h"
#include "i_video.h"
#include "i_capture.h"
#include "z_zone.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "am_map.h"
#include "g_game.h"
#include "lprintf.h"
#include "i_system.h"
#include "e6y.h"
#include "dsda/args.h"
#include "dsda/configuration.h"
#include "dsda/palette.h"

#include "chimera-platform.h"

dboolean window_focused = true;
int desired_fullscreen;
int exclusive_fullscreen;
SDL_Surface *screen;
SDL_Window *sdl_window;
SDL_Renderer *sdl_renderer;
unsigned int windowid = 0;
SDL_Rect src_rect = { 0, 0, 0, 0 };
SDL_Rect window_rect = { 0, 0, 0, 0 };
SDL_Rect renderer_rect = { 0, 0, 0, 0 };
SDL_Rect viewport_rect = { 0, 0, 0, 0 };
int leds_always_off = 0;
int mouse_hide_timer = 0;

#define MAX_RESOLUTIONS_COUNT 128
const char *screen_resolutions_list[MAX_RESOLUTIONS_COUNT] = { NULL };

void *I_GetSDLWindow(void) { return NULL; }
void *I_GetSDLRenderer(void) { return NULL; }
dboolean I_WindowFocused(void) { return true; }
int I_SDLtoDoomMouseState(Uint32 buttonstate) { (void)buttonstate; return 0; }

/* the frontend's input arrives as tic commands (the driver), never as events */
void I_StartTic(void) {}
void I_StartFrame(void) {}
void I_InitMouse(void) {}
void UpdateGrab(void) {}
void I_SetWindowRect(void) {}
void I_SetViewportRect(void) {}
void I_SetWindowCaption(void) {}
void I_SetWindowIcon(void) {}
void I_ShutdownGraphics(void) {}
void I_PreInitGraphics(void) {}
void dsda_Shutdown(void) {}
void I_QueueFrameCapture(void) {}
void I_QueueScreenshot(void) {}
void I_HandleCapture(void) {}
unsigned int I_TestCPUCacheMisses(int width, int height, unsigned int mintime)
{
	(void)width; (void)height; (void)mintime;
	return 0;
}

/* ---- the palette: upstream's I_UploadNewPalette, into the colours the
 * frame is converted with (the playpal's, through the gamma table) */

static int g_palette;          /* the palette the screen is shown in */
static int g_newpal;
#define NO_PALETTE_CHANGE 1000

static void upload_palette(int pal, int force)
{
	static int cachedgamma = -1;
	dsda_playpal_t *playpal_data = dsda_PlayPalData(playpal_index);

	if (playpal_data->colours == NULL || cachedgamma != usegamma || force)
	{
		const int pplump = W_GetNumForName(playpal_data->lump_name);
		const int gtlump = W_CheckNumForName2("GAMMATBL", ns_prboom);
		const byte *palette = (const byte *)W_LumpByNum(pplump);
		const byte *gtable = (const byte *)W_LumpByNum(gtlump) + 256 * (cachedgamma = usegamma);
		const size_t num_pals = W_LumpLength(pplump) / (3 * 256) * 256;

		if (!playpal_data->colours)
			playpal_data->colours = (SDL_Color *)Z_Malloc(sizeof(*playpal_data->colours) * num_pals);
		for (size_t i = 0; i < num_pals; i++)
		{
			playpal_data->colours[i].r = gtable[palette[0]];
			playpal_data->colours[i].g = gtable[palette[1]];
			playpal_data->colours[i].b = gtable[palette[2]];
			playpal_data->colours[i].a = 255;
			palette += 3;
		}
	}
	g_palette = pal;
}

void I_SetPalette(int pal) { g_newpal = pal; }

void I_FinishUpdate(void)
{
	if (g_newpal != NO_PALETTE_CHANGE)
	{
		upload_palette(g_newpal, false);
		g_newpal = NO_PALETTE_CHANGE;
	}
}

/* ---- the resolution: upstream's, from the configuration and the command
 * line (-width, -height, -geom), which the driver writes from the settings */

void I_InitBuffersRes(void)
{
	R_InitMeltRes();
	R_InitSpritesRes();
	R_InitBuffersRes();
	R_InitPlanesRes();
	R_InitVisplanesRes();
}

void I_GetScreenResolution(void)
{
	int width, height;
	const char *screen_resolution = dsda_StringConfig(dsda_config_screen_resolution);

	desired_screenwidth = 320;
	desired_screenheight = 200;
	if (screen_resolution && sscanf(screen_resolution, "%dx%d", &width, &height) == 2)
	{
		desired_screenwidth = width;
		desired_screenheight = height;
	}
}

void I_CalculateRes(int width, int height)
{
	SCREENWIDTH = width;
	SCREENHEIGHT = height;
	SCREENPITCH = (width + 15) & ~15;
}

void I_InitScreenResolution(void)
{
	dsda_arg_t *arg;

	I_GetScreenResolution();
	arg = dsda_Arg(dsda_arg_width);
	if (arg->found) desired_screenwidth = arg->value.v_int;
	arg = dsda_Arg(dsda_arg_height);
	if (arg->found) desired_screenheight = arg->value.v_int;
	desired_fullscreen = 0;

	V_InitMode(VID_MODESW);
	I_CalculateRes(desired_screenwidth, desired_screenheight);
	V_FreeScreens();

	for (int i = 0; i < 3; i++)
	{
		screens[i].width = SCREENWIDTH;
		screens[i].height = SCREENHEIGHT;
		screens[i].pitch = SCREENPITCH;
	}
	screens[4].width = SCREENWIDTH;
	screens[4].height = SCREENHEIGHT;
	screens[4].pitch = SCREENPITCH;

	I_InitBuffersRes();
	lprintf(LO_DEBUG, "I_InitScreenResolution: Using resolution %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}

void I_UpdateVideoMode(void)
{
	/* [FG] aspect ratio correction for the canonical video modes */
	if ((SCREENHEIGHT == 200 || SCREENHEIGHT == 400) && dsda_IntConfig(dsda_config_aspect_ratio_correction))
		ACTUALHEIGHT = 6 * SCREENHEIGHT / 5;
	else
		ACTUALHEIGHT = SCREENHEIGHT;

	screens[0].not_on_heap = false;
	V_AllocScreens();
	R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);

	/* e6y: wide-res - some initialisations before level precache */
	R_ExecuteSetViewSize();
	V_SetPalette(0);
	upload_palette(0, true);

	ST_SetResolution();
	AM_SetResolution();

	src_rect.w = SCREENWIDTH;
	src_rect.h = SCREENHEIGHT;
}

void I_InitGraphics(void)
{
	static int firsttime = 1;
	if (!firsttime) return;
	firsttime = 0;
	lprintf(LO_DEBUG, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
	I_UpdateVideoMode();
}

/* ---- the frame, for the frontend */

int chimera_video_width(void) { return SCREENWIDTH; }
int chimera_video_height(void) { return SCREENHEIGHT; }

void chimera_video_bgra(uint32_t *out)
{
	const dsda_playpal_t *playpal_data = dsda_PlayPalData(playpal_index);
	uint32_t lut[256];

	if (!playpal_data->colours || !screens[0].data)
	{
		memset(out, 0, sizeof(uint32_t) * SCREENWIDTH * SCREENHEIGHT);
		return;
	}
	const SDL_Color *c = playpal_data->colours + 256 * g_palette;
	for (int i = 0; i < 256; i++)
		lut[i] = 0xFF000000u | ((uint32_t)c[i].r << 16) | ((uint32_t)c[i].g << 8) | c[i].b;
	for (int y = 0; y < SCREENHEIGHT; y++)
	{
		const byte *src = screens[0].data + (size_t)y * screens[0].pitch;
		uint32_t *dst = out + (size_t)y * SCREENWIDTH;
		for (int x = 0; x < SCREENWIDTH; x++) dst[x] = lut[src[x]];
	}
}
