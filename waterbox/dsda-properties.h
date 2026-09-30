/* dsda-properties.h - GetGameProperties (Chimera's docs/game-cores.md): the
 * Game State block (the step's copy of the engine's globals, each player's
 * position) and the players' player_t fields in the Players domain. Fixed-point
 * values (x, y, z, momenta) are 16.16: 65536 is one map unit. Read-only: the
 * domains are copies. Included by dsda-driver.c only. */

const char *dsdadrv_game_properties(void)
{
	static char json[32768];
	if (json[0]) return json;
	int n = 0;
#define P(...) n += snprintf(json + n, sizeof json - (size_t)n, __VA_ARGS__)
#define GS(name, field, type, group, desc) \
	P("    { \"name\": \"%s\", \"domain\": \"Game State\", \"offset\": %d, \"type\": \"%s\", \"group\": \"%s\", \"writable\": false, \"description\": \"%s\" },\n", \
		name, (int)offsetof(struct game_state, field), type, group, desc)
	P("{\n  \"properties\": [\n");
	GS("Game.Tic", gametic, "s32", "Game", "The game's tic (gametic)");
	GS("Game.Level Time", leveltime, "s32", "Game", "Tics on this level (leveltime)");
	P("    { \"name\": \"Game.State\", \"domain\": \"Game State\", \"offset\": %d, \"type\": \"s32\", \"group\": \"Game\", \"writable\": false, "
	  "\"values\": { \"0\": \"Level\", \"1\": \"Intermission\", \"2\": \"Finale\", \"3\": \"Demo Screen\" }, \"description\": \"gamestate\" },\n",
	  (int)offsetof(struct game_state, gamestate));
	GS("Game.Episode", gameepisode, "s32", "Game", "gameepisode");
	GS("Game.Map", gamemap, "s32", "Game", "gamemap");
	GS("Game.Skill", gameskill, "s32", "Game", "gameskill (0: I'm too young to die .. 4: Nightmare!)");
	GS("Game.Compatibility Level", complevel, "s32", "Game", "compatibility_level");
	GS("Level.Things", things, "s32", "Level", "The things on the level (the Things domain's records)");
	GS("Level.Lines", lines, "s32", "Level", "numlines");
	GS("Level.Sectors", sectors, "s32", "Level", "numsectors");
	GS("Level.Total Kills", totalkills, "s32", "Level", "totalkills");
	GS("Level.Total Items", totalitems, "s32", "Level", "totalitems");
	GS("Level.Total Secrets", totalsecret, "s32", "Level", "totalsecret");
	GS("Level.Exit Reached", level_exit, "bool", "Level", "The level's exit was reached this tic (with preventLevelExit, and not taken)");
	GS("Level.Game End Reached", game_end, "bool", "Level", "The game's end was reached this tic");
	GS("Machine.Halted", halted, "bool", "Machine", "The engine stopped on an error");
	GS("Machine.Lag", lag, "bool", "Machine", "The last step was the screen melt's, the game waiting");
	GS("Machine.Steps", steps, "u64", "Machine", "Steps since power-on: one tic each");
	GS("RNG.Index", rng_index, "s32", "RNG", "rndindex: P_Random's, the game's (vanilla's table index)");
	GS("RNG.Misc Index", prng_index, "s32", "RNG", "prndindex: M_Random's (pr_misc - the status bar's face, the menus; not the game's)");
	for (int i = 0; i < 4; i++)
	{
		static const struct { const char *name; size_t off; const char *type; const char *desc; } fields[] = {
			{ "X", 0, "s32", "16.16 fixed point" }, { "Y", 4, "s32", "16.16 fixed point" }, { "Z", 8, "s32", "16.16 fixed point" },
			{ "Angle", 12, "u32", "a full turn is 2^32" }, { "Momentum X", 16, "s32", "16.16 fixed point" },
			{ "Momentum Y", 20, "s32", "16.16 fixed point" }, { "Momentum Z", 24, "s32", "16.16 fixed point" },
		};
		for (size_t f = 0; f < sizeof fields / sizeof fields[0]; f++)
			P("    { \"name\": \"P%d.%s\", \"domain\": \"Game State\", \"offset\": %d, \"type\": \"%s\", \"group\": \"Player %d\", \"writable\": false, \"description\": \"%s\" },\n",
				i + 1, fields[f].name, (int)(offsetof(struct game_state, player) + i * sizeof g_state.player[0] + fields[f].off), fields[f].type, i + 1, fields[f].desc);
		static const struct { const char *name; size_t off; const char *desc; } pfields[] = {
			{ "Health", offsetof(player_t, health), "health" },
			{ "Armor", offsetof(player_t, armorpoints), "armorpoints" },
			{ "Armor Type", offsetof(player_t, armortype), "armortype" },
			{ "Ready Weapon", offsetof(player_t, readyweapon), "readyweapon" },
			{ "Pending Weapon", offsetof(player_t, pendingweapon), "pendingweapon" },
			{ "Kills", offsetof(player_t, killcount), "killcount" },
			{ "Items", offsetof(player_t, itemcount), "itemcount" },
			{ "Secrets", offsetof(player_t, secretcount), "secretcount" },
			{ "View Z", offsetof(player_t, viewz), "viewz, 16.16 fixed point" },
			{ "Refire", offsetof(player_t, refire), "refire" },
			{ "Attack Down", offsetof(player_t, attackdown), "attackdown" },
			{ "Use Down", offsetof(player_t, usedown), "usedown" },
			{ "Damage Count", offsetof(player_t, damagecount), "damagecount" },
			{ "Bonus Count", offsetof(player_t, bonuscount), "bonuscount" },
		};
		for (size_t f = 0; f < sizeof pfields / sizeof pfields[0]; f++)
			P("    { \"name\": \"P%d.%s\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"s32\", \"group\": \"Player %d\", \"writable\": false, \"description\": \"%s\" },\n",
				i + 1, pfields[f].name, (int)(i * PAD_PLAYER + pfields[f].off), i + 1, pfields[f].desc);
	}
	P("    { \"name\": \"Ammo\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"s32\", \"count\": %d, \"stride\": 4, \"group\": \"Player 1\", \"writable\": false, "
	  "\"description\": \"Player 1's ammo, by type (am_clip, am_shell, am_cell, am_misl)\" }\n", (int)offsetof(player_t, ammo), NUMAMMO);
	P("  ]\n}\n");
#undef GS
#undef P
	return json;
}
