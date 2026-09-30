/* stubs.c - the parts of dsda-doom the core leaves out, as nothing:
 *
 *   - the OpenGL renderer (gl_*.c, dsda/gl/): the core draws with the software
 *     renderer (V_IsOpenGLMode() is false), so these are reached only by the
 *     few calls upstream makes whatever the renderer - to clean its textures
 *     up, read its command line - which do nothing without it
 *   - video capture, screenshots, ENDOOM, game controllers, .zip files and
 *     libsndfile's sound formats
 */
#include <stddef.h>

#include "doomtype.h"
#include "lprintf.h"

/* ---- the OpenGL renderer's state */
void *anim_flats;
void *anim_textures;
dboolean gl_use_stencil;
int gl_drawskys;
dboolean gl_ui_lightmode_indexed;
dboolean gl_automap_lightmode_indexed;
dboolean gl_menu_lightmode_indexed;

/* ---- its functions: never called with a use (V_IsOpenGLMode() is false) */
void dsda_GLEndMeltRenderTexture(void) {}
void dsda_GLLetterboxClear(void) {}
void dsda_GLSetRenderViewportParams(void) {}
void dsda_GLStartMeltRenderTexture(void) {}
void dsda_GLUpdateStatusBarVisible(void) {}
void gld_AddPlane(void) {}
void gld_AddWall(void) {}
void gld_BeginAutomapDraw(void) {}
void gld_BeginMenuDraw(void) {}
void gld_BeginUIDraw(void) {}
void gld_CleanMemory(void) {}
void gld_DrawLine(void) {}
void gld_DrawLine_f(void) {}
void gld_DrawMapLines(void) {}
void gld_DrawNumPatch(void) {}
void gld_DrawNumPatch_f(void) {}
void gld_DrawScene(void) {}
void gld_DrawShaded(void) {}
void gld_DrawWeapon(void) {}
void gld_EndAutomapDraw(void) {}
void gld_EndDrawScene(void) {}
void gld_EndMenuDraw(void) {}
void gld_EndUIDraw(void) {}
void gld_FillBlock(void) {}
void gld_FillPatch(void) {}
void gld_FillRaw(void) {}
void gld_FlushTextures(void) {}
void gld_FrustumSetup(void) {}
void gld_InitCommandLine(void) {}
void gld_InitDrawScene(void) {}
void gld_MapDrawSubsectors(void) {}
void gld_MultisamplingInit(void) {}
void gld_MultisamplingSet(void) {}
void gld_PreprocessLevel(void) {}
void gld_ProcessTexturedMap(void) {}
void gld_ProjectSprite(void) {}
void gld_ResetAutomapTransparency(void) {}
void gld_ResetTexturedAutomap(void) {}
void gld_SetPalette(void) {}
void gld_StartDrawScene(void) {}
void gld_UpdateSplitData(void) {}
void gld_clipper_SafeAddClipRange(void) {}
void gld_wipe_EndScreen(void) {}
void gld_wipe_StartScreen(void) {}
void gld_wipe_doMelt(void) {}
void gld_wipe_exitMelt(void) {}
void *GetBestFake(void) { return NULL; }
void *GetBestBleedSector(void) { return NULL; }
dboolean gld_clipper_SafeCheckRange(void) { return false; }

/* ---- video capture (i_capture.c): never on */
int cap_fps = 60;
int cap_frac;
int cap_wipescreen;
int capturing_video;
void I_CapturePrep(const char *fn) { (void)fn; I_Error("video capture (-viddump) is the frontend's, not the core's"); }

/* ---- screenshots (SDL/i_sshot.c): the frontend's */
int I_ScreenShot(const char *fname) { (void)fname; return 0; }

/* ---- ENDOOM (dsda/endoom.c) at exit: there is none */
void dsda_CacheEndoom(void) {}
void dsda_DumpEndoom(void) {}

/* ---- game controllers (dsda/game_controller.c): the input is the frontend's */
void dsda_InitGameController(void) {}
void dsda_InitGameControllerParameters(void) {}
const char *dsda_GameControllerButtonName(int button) { (void)button; return ""; }

/* ---- .zip files (dsda/zipfile.c) */
const char *dsda_UnzipFile(const char *zipped_file_name)
{
	I_Error("%s: .zip files are not read by the core; add the WADs themselves", zipped_file_name);
	return NULL;
}
const char *dsda_ReadUnzippedFile(const char *zipped_file_name) { return dsda_UnzipFile(zipped_file_name); }
void dsda_CleanZipTempDirs(void) {}

/* ---- sound effects in libsndfile's formats (SDL/i_sndfile.c): not loaded;
 * the engine skips the effect with its own warning */
void *Load_SNDFile(const void *data, void *sample, void **sampledata, void *samplelen)
{
	(void)data; (void)sample; (void)sampledata; (void)samplelen;
	return NULL;
}
