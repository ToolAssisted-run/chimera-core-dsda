/* dsda-input.h - the controllers, as BizHawk's DSDA core defines them
 * (DSDA.Controller.cs), one for each input format: Doom's, Heretic's and
 * Hexen's. A machine (waterbox.config "machines") has the controller of its
 * game; the lists here are what the declaration's "buttons" and "axes" are,
 * in order (the gate compares them), and each entry says what it does for
 * the driver (dsda-driver.c).
 *
 * Per player, four players: the axes first in BizHawk's order, then the
 * buttons; after the players, the automap's and the camera's controls. A
 * player not in the game has their inputs inactive (IsButtonActive,
 * IsAxisActive), as has "Turn Speed Frac." with shorttics.
 *
 * Beyond BizHawk's, for what a demo can hold: the axes take a tic command's
 * whole range (Run and Strafe Speed a signed byte, Weapon Select any weapon
 * number, the artifact axis any artifact - "Artifact", where BizHawk's shares
 * the Use Artifact button's name), each player has Pause (the command's
 * pause, BT_SPECIAL), and with the extendedCommands setting dsda's extended
 * commands - Jump and Free Look, and God and No Clip with its casual
 * features - which are inactive otherwise. */
#ifndef DSDA_INPUT_H
#define DSDA_INPUT_H

#include <stdint.h>

enum dsda_format { FORMAT_DOOM, FORMAT_HERETIC, FORMAT_HEXEN, FORMAT_COUNT };

/* what an input is, whichever controller it is on */
enum dsda_control
{
	/* per player (the player is the entry's port) */
	C_RUN_SPEED, C_STRAFE_SPEED, C_TURN_SPEED, C_TURN_FRAC, C_WEAPON_SELECT, C_MOUSE_RUN, C_MOUSE_TURN,
	C_LOOK, C_FLY, C_USE_ARTIFACT_AXIS, C_FREE_LOOK,
	C_FIRE, C_USE, C_FORWARD, C_BACKWARD, C_TURN_LEFT, C_TURN_RIGHT, C_STRAFE_LEFT, C_STRAFE_RIGHT, C_RUN, C_STRAFE,
	C_WEAPON_1, C_WEAPON_2, C_WEAPON_3, C_WEAPON_4, C_WEAPON_5, C_WEAPON_6, C_WEAPON_7,
	C_JUMP, C_END_PLAYER,
	C_INVENTORY_LEFT, C_INVENTORY_RIGHT, C_USE_ARTIFACT, C_LOOK_UP, C_LOOK_DOWN, C_LOOK_CENTER,
	C_FLY_UP, C_FLY_DOWN, C_FLY_CENTER,
	C_PAUSE, C_EX_JUMP, C_GOD, C_NOCLIP,
	/* the machine's */
	C_CHANGE_GAMMA,
	C_AUTOMAP_TOGGLE, C_AUTOMAP_ZOOM_IN, C_AUTOMAP_ZOOM_OUT, C_AUTOMAP_FULL_ZOOM, C_AUTOMAP_FOLLOW,
	C_AUTOMAP_UP, C_AUTOMAP_DOWN, C_AUTOMAP_RIGHT, C_AUTOMAP_LEFT, C_AUTOMAP_GRID, C_AUTOMAP_MARK,
	C_AUTOMAP_CLEAR_MARKS,
	C_CAMERA_MODE, C_CAMERA_RUN_SPEED, C_CAMERA_STRAFE_SPEED, C_CAMERA_TURN_SPEED, C_CAMERA_FLY, C_CAMERA_RESET,
	C_COUNT
};

struct dsda_input
{
	char name[40];
	int control;     /* enum dsda_control */
	int port;        /* 1..4, or 0 for the machine's */
	int32_t min, max, neutral; /* an axis's range */
};

#define DSDA_MAX_INPUTS 160

/* the controller of a format: its buttons and its axes, in the declaration's
 * order; filled once (dsda-driver.c) */
struct dsda_controller
{
	struct dsda_input buttons[DSDA_MAX_INPUTS];
	int nbuttons;
	struct dsda_input axes[DSDA_MAX_INPUTS];
	int naxes;
};

const struct dsda_controller *dsda_controller(int format);

#endif
