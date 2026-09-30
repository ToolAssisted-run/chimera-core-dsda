/* SDL_opengl.h - OpenGL's declarations (Mesa's gl.h and glext.h, compat/GL),
 * for the engine files whose OpenGL branches the core never takes: it draws
 * with dsda-doom's software renderer only (platform/gl-stubs.c) */
#include "SDL.h"
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
