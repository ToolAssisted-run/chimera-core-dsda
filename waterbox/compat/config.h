/* config.h - dsda-doom's configuration (upstream's cmake/config.h.cin), as the
 * core builds it: no optional library (the music is the OPL emulation), no
 * mmap (the WADs are read into memory), and the data files where the host
 * mounts them - the package's dsda-doom.wad beside the project's files. */
#define PROJECT_NAME "dsda-doom"
#define PROJECT_TARNAME "dsda-doom"
#define WAD_DATA "dsda-doom.wad"
#define PROJECT_VERSION "0.30.0"
#define PROJECT_STRING "dsda-doom 0.30.0"

#define DOOMWADDIR "."
/* dsda-doom.wad, the engine's own WAD: a package asset, which Chimera mounts at
 * the guest's root (/dsda-doom.wad); the harnesses have it in the work folder */
#define DSDA_ABSOLUTE_PWAD_PATH "/"

#define HAVE_GETOPT
#define HAVE_STRSIGNAL
#define HAVE_UNISTD_H
#define HAVE_DIRENT_H
