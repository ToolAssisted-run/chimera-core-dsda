/* dsda-input.c - the controllers (dsda-input.h), built once: the driver's,
 * and the harnesses' (run-native.c, run-wbx.c), which find a movie's inputs
 * in them by name as the frontend does in the declaration */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "dsda-input.h"

static struct dsda_controller g_controllers[FORMAT_COUNT];

static void add_input(struct dsda_input *list, int *n, const char *name, int control, int port, int32_t min, int32_t max, int32_t neutral)
{
	struct dsda_input *in = &list[(*n)++];
	snprintf(in->name, sizeof in->name, "%s", name);
	in->control = control;
	in->port = port;
	in->min = min;
	in->max = max;
	in->neutral = neutral;
}

static void build_controller(int format, struct dsda_controller *c)
{
	char name[40];
	memset(c, 0, sizeof *c);
#define AXIS(label, control, min, max, neutral) \
	(snprintf(name, sizeof name, "P%d " label, port), add_input(c->axes, &c->naxes, name, control, port, min, max, neutral))
#define BUTTON(label, control) \
	(snprintf(name, sizeof name, "P%d " label, port), add_input(c->buttons, &c->nbuttons, name, control, port, 0, 1, 0))
	for (int port = 1; port <= 4; port++)
	{
		/* a tic command's byte: BizHawk's -50..50 is what the keys make, a
		 * demo may hold more (-turbo, hand-made tics) */
		AXIS("Run Speed", C_RUN_SPEED, -128, 127, 0);
		AXIS("Strafe Speed", C_STRAFE_SPEED, -128, 127, 0);
		AXIS("Turn Speed", C_TURN_SPEED, -128, 127, 0);
		/* editing a short in TAStudio would be a nightmare, so BizHawk splits
		 * it: the high byte is shorttics' whole angle units, this the
		 * fraction longtics has */
		AXIS("Turn Speed Frac.", C_TURN_FRAC, -255, 255, 0);
		/* the weapon's number + 1, as the command holds it (BT_CHANGE and four
		 * bits): BizHawk's 0..7 cannot name the chainsaw (8) or the super
		 * shotgun (9) */
		AXIS("Weapon Select", C_WEAPON_SELECT, 0, 16, 0);
		AXIS("Mouse Run", C_MOUSE_RUN, -128, 127, 0);
		/* the largest raw mouse delta, 180, with longtics */
		AXIS("Mouse Turn", C_MOUSE_TURN, -180, 180, 0);
		if (format != FORMAT_DOOM)
		{
			AXIS("Look", C_LOOK, -7, 8, 0);
			AXIS("Fly", C_FLY, -7, 8, 0);
			/* the artifact to use, by its number - any (the command's six bits:
			 * Hexen has 32). BizHawk calls it "Use Artifact", as it calls the
			 * button that uses the inventory's; a name the two share is a
			 * column the frontend cannot tell apart */
			AXIS("Artifact", C_USE_ARTIFACT_AXIS, 0, 63, 0);
		}
		/* dsda's extended command: the free look's change, a short (-32768
		 * recentres) */
		AXIS("Free Look", C_FREE_LOOK, -32768, 32767, 0);
		BUTTON("Fire", C_FIRE);
		BUTTON("Use", C_USE);
		BUTTON("Forward", C_FORWARD);
		BUTTON("Backward", C_BACKWARD);
		BUTTON("Turn Left", C_TURN_LEFT);
		BUTTON("Turn Right", C_TURN_RIGHT);
		BUTTON("Strafe Left", C_STRAFE_LEFT);
		BUTTON("Strafe Right", C_STRAFE_RIGHT);
		BUTTON("Run", C_RUN);
		BUTTON("Strafe", C_STRAFE);
		BUTTON("Weapon Select 1", C_WEAPON_1);
		BUTTON("Weapon Select 2", C_WEAPON_2);
		BUTTON("Weapon Select 3", C_WEAPON_3);
		BUTTON("Weapon Select 4", C_WEAPON_4);
		if (format == FORMAT_HEXEN)
		{
			BUTTON("Jump", C_JUMP);
			BUTTON("End Player", C_END_PLAYER);
		}
		else
		{
			BUTTON("Weapon Select 5", C_WEAPON_5);
			BUTTON("Weapon Select 6", C_WEAPON_6);
			BUTTON("Weapon Select 7", C_WEAPON_7);
		}
		if (format != FORMAT_DOOM)
		{
			BUTTON("Inventory Left", C_INVENTORY_LEFT);
			BUTTON("Inventory Right", C_INVENTORY_RIGHT);
			BUTTON("Use Artifact", C_USE_ARTIFACT);
			BUTTON("Look Up", C_LOOK_UP);
			BUTTON("Look Down", C_LOOK_DOWN);
			BUTTON("Look Center", C_LOOK_CENTER);
			BUTTON("Fly Up", C_FLY_UP);
			BUTTON("Fly Down", C_FLY_DOWN);
			BUTTON("Fly Center", C_FLY_CENTER);
		}
		/* the command's pause (BT_SPECIAL | BT_PAUSE), a toggle */
		BUTTON("Pause", C_PAUSE);
		/* dsda's extended commands: Hexen jumps with its artifact flag */
		if (format != FORMAT_HEXEN) BUTTON("Jump", C_EX_JUMP);
		BUTTON("God", C_GOD);
		BUTTON("No Clip", C_NOCLIP);
	}
#undef AXIS
#undef BUTTON
	static const struct { const char *name; int control; } machine_buttons[] = {
		{ "Change Gamma", C_CHANGE_GAMMA },
		{ "Automap Toggle", C_AUTOMAP_TOGGLE },
		{ "Automap +", C_AUTOMAP_ZOOM_IN },
		{ "Automap -", C_AUTOMAP_ZOOM_OUT },
		{ "Automap Full/Zoom", C_AUTOMAP_FULL_ZOOM },
		{ "Automap Follow", C_AUTOMAP_FOLLOW },
		{ "Automap Up", C_AUTOMAP_UP },
		{ "Automap Down", C_AUTOMAP_DOWN },
		{ "Automap Right", C_AUTOMAP_RIGHT },
		{ "Automap Left", C_AUTOMAP_LEFT },
		{ "Automap Grid", C_AUTOMAP_GRID },
		{ "Automap Mark", C_AUTOMAP_MARK },
		{ "Automap Clear Marks", C_AUTOMAP_CLEAR_MARKS },
		{ "Camera Reset", C_CAMERA_RESET },
	};
	for (size_t i = 0; i < sizeof machine_buttons / sizeof machine_buttons[0]; i++)
		add_input(c->buttons, &c->nbuttons, machine_buttons[i].name, machine_buttons[i].control, 0, 0, 1, 0);
	add_input(c->axes, &c->naxes, "Camera Mode", C_CAMERA_MODE, 0, -1, 2, -1);
	add_input(c->axes, &c->naxes, "Camera Run Speed", C_CAMERA_RUN_SPEED, 0, INT_MIN + 1, INT_MAX, 0);
	add_input(c->axes, &c->naxes, "Camera Strafe Speed", C_CAMERA_STRAFE_SPEED, 0, INT_MIN + 1, INT_MAX, 0);
	add_input(c->axes, &c->naxes, "Camera Turn Speed", C_CAMERA_TURN_SPEED, 0, -128, 127, 0);
	add_input(c->axes, &c->naxes, "Camera Fly", C_CAMERA_FLY, 0, INT_MIN + 1, INT_MAX, 0);
}

const struct dsda_controller *dsda_controller(int format)
{
	static int built;
	if (!built)
	{
		for (int f = 0; f < FORMAT_COUNT; f++) build_controller(f, &g_controllers[f]);
		built = 1;
	}
	return &g_controllers[format];
}

