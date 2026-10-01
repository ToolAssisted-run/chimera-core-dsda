# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.

ROOT := ..
DSDA := $(ROOT)/extern/dsda-doom/prboom2/src
ZLIB := $(ROOT)/extern/zlib
MB   ?= $(or $(MINIBOX_DIR),$(firstword $(wildcard $(HOME)/chimera/extern/chimera-common-minibox $(HOME)/chimera/extern/tools/chimera-common-minibox)),$(HOME)/chimera/extern/chimera-common-minibox)

# ---- dsda-doom, upstream (the submodule extern/dsda-doom, tag v0.30.0): its
# engine - Doom, Heretic and Hexen, the renderer, the status bars and HUDs, the
# automap, the sound mixer and the OPL music - less its frontend, which the
# core is instead (platform/):
#   SDL/i_video.c                  the window: the core's own headless one
#                                  (platform/i_video.c)
#   SDL/i_sshot.c, SDL/i_sndfile.c screenshots (SDL_image) and libsndfile's
#                                  sound formats
#   i_capture.c                    video capture through an external encoder
#   dsda/game_controller.c         SDL's game controllers
#   dsda/endoom.c, textscreen/     the ENDOOM screen at exit
#   gl_*.c, dsda/gl/               the OpenGL renderer: the core draws with the
#                                  software renderer only
#   dsda/zipfile.c                 libzip's .zip loading
#   w_mmap.c                       mmap'd WADs: w_memcache.c reads them instead
#   icon.c, win_opendir.c          the window's icon, Windows' opendir
#   SDL/i_main.c                   its main: the core's start and exit instead
#                                  (platform/i_main.c)
# SDL/i_system.c and SDL/i_sound.c build as they are: their SDL
# and SDL_mixer are compat/, answered by platform/sdl-shim.c - the audio
# "device" is the frontend, which pulls each step's samples from upstream's
# own mixer (I_GrabSound, its sound-capture path).
DSDA_DIRS := . dsda dsda/hud_components dsda/mapinfo dsda/utility heretic hexen MUSIC SDL
DSDA_EXCLUDE := SDL/i_main.c SDL/i_video.c SDL/i_sshot.c SDL/i_sndfile.c i_capture.c dsda/game_controller.c \
	dsda/endoom.c dsda/zipfile.c w_mmap.c icon.c win_opendir.c $(notdir $(wildcard $(DSDA)/gl_*.c))
DSDA_ALL := $(foreach d,$(DSDA_DIRS),$(wildcard $(DSDA)/$(d)/*.c $(DSDA)/$(d)/*.cpp))
DSDA_SRCS := $(filter-out $(addprefix $(DSDA)/./,$(DSDA_EXCLUDE)) $(addprefix $(DSDA)/,$(DSDA_EXCLUDE)),$(DSDA_ALL))
DSDA_C_SRCS := $(filter %.c,$(DSDA_SRCS))
DSDA_CXX_SRCS := $(filter %.cpp,$(DSDA_SRCS))
# upstream's defines (its config.h is compat/config.h); its RANGECHECK and
# SIMPLECHECKS off and NDEBUG on, as its release build has them, in both builds. stricmp: upstream's CMake
# maps it on POSIX systems (DsdaTargetFeatures.cmake).
DSDA_DEFS := -DHAVE_CONFIG_H -DNDEBUG -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp
DSDA_CFLAGS_COMMON := -std=gnu11 -O2 $(DSDA_DEFS) -Icompat -Iplatform -I$(DSDA) -I$(DSDA)/MUSIC -I$(ZLIB)
DSDA_CXXFLAGS_COMMON := -std=gnu++17 -O2 $(DSDA_DEFS) -Icompat -Iplatform -I$(DSDA) -I$(DSDA)/MUSIC -I$(ZLIB)

# ---- zlib (the submodule extern/zlib, tag v1.3.1): ZDoom's compressed nodes
# (p_setup.c P_DecompressData) - its inflate half only
ZLIB_NAMES := adler32 crc32 inffast inflate inftrees zutil
ZLIB_SRCS := $(addprefix $(ZLIB)/,$(addsuffix .c,$(ZLIB_NAMES)))
ZLIB_CFLAGS_COMMON := -std=gnu11 -O2 -DNDEBUG -I$(ZLIB)

# ---- the core: its platform layer (platform/) and the driver
PLATFORM_NAMES := i_main i_video sdl-shim stubs detmath
CORE_C_NAMES := $(addprefix platform/,$(PLATFORM_NAMES)) dsda-input dsda-driver wbx-entry
# the demo importer (ImportMovie's, and tools/lmp-import.cpp's)
CORE_CXX_NAMES := lmp-import
CORE_HDRS := $(wildcard *.h) $(wildcard platform/*.h) $(wildcard compat/*.h)
CORE_CFLAGS_COMMON := $(DSDA_CFLAGS_COMMON)

# the calls the core answers itself
WRAP_FLAGS := -Wl,--wrap=clock_gettime -Wl,--wrap=time -Wl,--wrap=M_MakeDir -Wl,--wrap=getcwd -Wl,--wrap=access \
	-Wl,--wrap=atan -Wl,--wrap=asin -Wl,--wrap=tan -Wl,--wrap=pow -Wl,--wrap=sincos

# the patch series goes onto the submodule before anything of dsda-doom builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# dsda-doom.wad, the engine's own data (fonts, HUD pictures, tables), built
# from upstream's data/ with its own tool, rdatawad; the package carries it
# as an asset, mounted beside the project's files
WAD_DATA := $(ROOT)/build/dsda-doom.wad
$(WAD_DATA): $(wildcard $(DSDA)/../data/*) $(PATCH_STAMP)
	cmake -S $(DSDA)/../data -B $(ROOT)/build/wad -DWAD_DATA_PATH=$(abspath $@) > $(ROOT)/build/wad.log
	cmake --build $(ROOT)/build/wad >> $(ROOT)/build/wad.log

# Every object depends on the flags it was built with: a change to them
# rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
