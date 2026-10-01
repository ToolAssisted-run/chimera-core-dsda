# chimera-core-dsda

[dsda-doom](https://github.com/kraflab/dsda-doom), kraflab's speedrunning and TAS source port of Doom (PrBoom+'s
successor), as a [Chimera](https://github.com/ToolAssisted-run/chimera) **game core** (`"kind": "game"`, see
Chimera's `docs/game-cores.md`): Doom, Doom II, Final Doom, Heretic, Hexen, Chex Quest and Freedoom, a tic
at a time in miniBox's sandbox, packaged as `dsda.chimeraCore`.

**Built on upstream dsda-doom v0.30.0 (its latest release), with BizHawk's DSDA core's changes ported onto it**:
BizHawk's core (`waterbox/dsda`, TASEmulators/dsda-doom, by feos and Sergio Martin) is the precedent, and this
core keeps its controllers, its settings, its automap and walking-camera controls and its level-exit
detection, on the current engine. The engine is compiled from source - its game code, its software renderer,
its sound code and OPL music (upstream's `i_sound.c` as it is, its mixer captured a tic at a time) - without
SDL, OpenGL or a window, which the core is instead (`waterbox/platform/`). quickerDSDA (JaffarPlus's DSDA) is
where its input format and its test movies come from.

## What it is

- **The game is the System, its IWAD the firmware** (`waterbox.config` "machines", the `game` setting): a project
  picks Doom II, Doom, TNT, Plutonia, Heretic, Hexen, Chex Quest, Freedoom: Phase 1 or Phase 2 and brings that
  game's IWAD - any release of it (Doom 1.666 or 1.9, Ultimate, BFG; a modified IWAD), the project pinning the file's
  hash. There is no version setting: the IWAD's content and the compatibility level say the rules, as they do for
  dsda-doom itself.

  | System (`game`) | The IWAD (firmware id) |
  |---|---|
  | Doom II | `doom2.wad` |
  | Doom | `doom.wad` |
  | Final Doom: TNT - Evilution | `tnt.wad` |
  | Final Doom: The Plutonia Experiment | `plutonia.wad` |
  | Heretic | `heretic.wad` |
  | Hexen | `hexen.wad` |
  | Chex Quest | `chex.wad` |
  | Freedoom: Phase 1 | `freedoom1.wad` |
  | Freedoom: Phase 2 | `freedoom2.wad` |

  The engine tells a game and its mission by the IWAD's name, which is why the firmware ids are the names it
  knows. The dumps the core has been tested with (Doom II 1.9, The Ultimate Doom 1.9, TNT, Plutonia, Heretic 1.2,
  Hexen 1.1, Chex Quest, Freedoom 0.11-0.13.0) are listed in `waterbox/gen-declaration.py` by hash, for telling a
  demo's game, not as a setting. Chex Quest 2 is a PWAD for Chex Quest (`chex2.wad` in the slot); Chex Quest 3's
  IWADs (`chex3v.wad`, `chex3d2.wad`) the engine knows, and the core does not declare yet.
- **PWADs and patches are the project's files** (`file_slots.json`, the `pwad` slot: .wad, .deh, .bex, in load
  order - the WADs as `-file`, then the patches as `-deh`). None for the game itself; the wizard's files step
  asks for none.
- **A step is a tic**: 1/35 s, the game's own clock. The screen melt between levels (Render Wipescreen) is
  stepped a tic at a time too, and its steps are lag - the game waits, as in BizHawk's core
  (`InputWasRead`).
- **A movie is a demo**: every run is played as upstream plays a recorded demo (`patches/0011`): the vanilla
  behaviours dsda keeps for demos - Doom II's short "Now entering" screen, the references a removed thing
  keeps at the old complevels, weapon autoswitch, the skill flags from the arguments - rather than its
  conveniences outside them - and with no Boom-or-later option block, the options dsda's playback forces for
  an old demo (no friction, no pushers: a missile on Hexen's ice explodes rather than bouncing off a wall;
  `patches/0012`). So a demo's inputs as a movie play exactly as the engine plays the demo itself, and a
  movie's inputs are a demo. (Upstream v0.30 made its "Now entering" screen longer outside demos, which
  BizHawk's older engine does not have; the Pistol Start setting stays as BizHawk has it.)
- **Every demo imports**, by the core's own importer (`waterbox/lmp-import.cpp`, below): whatever a demo dictates -
  its format's compatibility level, the skill and map, the players and their classes, the monster flags, the
  turning resolution, a Boom-or-later demo's option block, dsda's extended commands, its footer's arguments - is a
  setting of the project, its IWAD the firmware, its PWADs the slot's files, and its tics the movie, frame for
  tic.
- **The controls are BizHawk's DSDA controller** (`waterbox/dsda-input.c`, `DSDA.Controller.cs`), one for each
  game's format: for each of four players the Run, Strafe and Turn Speed axes (Turn Speed Frac. with longtics),
  Weapon Select, Mouse Run and Mouse Turn, and for Heretic and Hexen Look, Fly and Artifact; the buttons
  Fire, Use, Forward, Backward, Turn Left and Right (a short tap turns slower), Strafe Left and Right, Run,
  Strafe (turning with it held is strafe50), the weapons, and the Raven games' inventory, look and fly, Hexen's
  Jump and End Player; then the machine's Change Gamma, the automap's twelve controls and the walking camera
  (its mode, speeds and reset). A player not in the game has their inputs inactive. The default keys are
  BizHawk's (WASD, the mouse, the number keys, Tab for the automap; Pause for pause).

  Beyond BizHawk's, for all a demo can hold: the axes take a tic command's whole range - Run and Strafe Speed
  -128..127 (the keys and the mouse are still held to BizHawk's 50; an axis alone is the command's byte),
  Weapon Select the weapon's number + 1 up to 16 (the axis is the weapon itself, where a Weapon Select key
  finds the chainsaw and the super shotgun as the keys of the game do), Artifact any of Hexen's 32 (BizHawk
  calls it Use Artifact, the name of a button too: one name, two columns no frontend tells apart); each
  player has Pause (the command's pause) and, in Heretic and Hexen, Special - a special command's low seven
  bits, which those games read before they clear them (a dead player's use, the intermission's skip); and with
  the Extended Commands setting dsda's own, Jump and Free Look, and God and No Clip with its casual features.
- **Settings**: BizHawk's, by the same names and values - the compatibility level (Doom-format games), skill,
  multiplayer mode, initial episode and map, the monster flags, pistol start, co-op spawns, chained episodes,
  always run, the melt, turning resolution (shorttics or longtics), the mouse sensitivities, strafe50, Prevent
  Level Exit and Prevent Game End (the exit is seen and not taken, for level-by-level runs), turbo, the RNG
  seed (complevel 7 and up: Boom's first demos hold one too), the four players and Hexen's classes; and, not
  part of the machine, the resolution (1x-12x or BizHawk's aspect-corrected sizes), the volumes, gamma,
  messages, the HUD and extended HUD, the automap's totals, time, coordinates, overlay, details and trail, full
  vision and whose view is shown. And what a demo can dictate beyond them: Solo Net (dsda's -solo-net), the
  Boom/MBF Options (a Boom-or-later demo's option block as hex - monster memory, friction, pushers, bobbing,
  demo insurance, infighting, helper dogs, the comp flags - applied where a demo's header applies it), Extended
  Commands (off, on, on with casual features), Emulate PrBoom+ Version (-emulate), the spechit overflow's base
  address (-spechit) and the six overflow emulations (spechit, reject, intercepts, playeringame, donut, missed
  backside).
- **Properties** (Chimera's `docs/game-cores.md`): a `Game State` block (the tic, the level time, the game
  state, episode, map, skill and complevel, the level's things, lines, sectors and totals, the exit and game
  end, the melt's lag, the RNG indexes, and each player's position, angle and momentum) and each player's
  `player_t` (the Players domain: health, armour, the ready and pending weapons, ammunition, kills, items,
  secrets, the view height, the attack and use latches, the damage and bonus flashes); the
  level's things, lines and sectors are domains of their own, in the engine's records.
- **An engine error halts the machine, not the frontend**: `I_Error` stops the engine where it stands, with its
  message; the machine keeps stepping, silent.

## Importing a demo

```
build/native/lmp-import demo.lmp --iwad DOOM2.WAD [--wads <folder>]... [-o demo.chimeraProject]
build/native/lmp-import demo.lmp --game freedoom2 --wads <folder> --pwad map.wad --info
```

The importer is the core's (`waterbox/lmp-import.cpp`, C++): compiled into core.wbx, where a frontend calls it -
the export `ImportMovie`, on a loaded core instead of `Init`, the demo mounted as `movie` and the WADs it reads by
their names, its options as settings (`importIwad`, `importPwads`, `importNoPwads`,
`importLongtics`, `importRespawn`, `importFast`, `importNomonsters`), the project's parts returned as JSON
(`settings`, `firmware`, `files`, `input`, `frames`, what the demo is, notes) or `{"error": ...}`; the
declaration's `movieImport` is the dialog Chimera draws for it (the demo, the IWAD as the firmware, the PWADs and
patches into the slot, the options) - and into the command-line tool (`tools/lmp-import.cpp`, built by `native.mk`
as `build/native/lmp-import`), which finds the files in folders, pins the package and writes the whole
`.chimeraProject`. The project it writes is the demo: open it in Chimera (or `chimera-run --project`) with the
IWAD and the PWADs at hand. The game comes from the IWAD - `--iwad` (the file), `--game` (its IWAD by name, found in
`--wads`), or the demo's own footer (its `-iwad`, found in `--wads`): a dump the core knows by its hash, else by its
name and lumps; the firmware is that file, by its own hash, and a fourth episode in it (E4M1) makes a 1.9 demo The
Ultimate Doom's (complevel 3); the PWADs and patches from the footer's `-file` and `-deh`
(found by name in `--wads`) or `--pwad`, in order; `--no-pwads` takes none (an IWAD's own demos). `--package` pins
the project to a package (its version and hash); the settings' names and options are the core's own, compiled in
from the declaration.

Every format dsda-doom plays: Doom 1.0-1.2 (no version byte; its monster flags are not in it - the footer's,
or `--respawn`, `--fast`, `--nomonsters`), 1.4-1.9 (the complevel as `G_GetOriginalDoomCompatLevel` gives it:
the footer's `-complevel`, else 1 below 1.7, 3 on a game with a fourth episode, 4 on Final Doom, 2 otherwise),
TASDoom (its own byte order), 1.9 longtics, Boom 2.00-2.02 (and its compatibility flag), LxDoom, MBF, PrBoom
2.1-latest, MBF21 (with an older dsda's 23 comp flags too), dsda's own format (its extended commands each tic:
jump, free look, god, no clip), PrBoom+um's UMAPINFO header, Heretic and Hexen (their header's respawn,
longtics and no-monsters bits; Hexen's classes, and its map as the warp number MAPINFO gives it - the core's
Initial Map is what `-warp` takes, as BizHawk's), the footer's arguments (`-solo-net`, `-coop_spawns`,
`-chain_episodes`, `-emulate`, `-spechit`, the overflows' `-set`), special commands (the pause; in Doom the
others, which dsda ignores; in Heretic and Hexen their low bits) and the join marker. `--longtics` reads a
Heretic or Hexen demo recorded with `-longtics` whose header does not say so (Hexen+'s). The melt is off in the project, as BizHawk's importers have it: a
demo's tics are the game's. Refused, saying why: more than four players, a start from a key frame (a saved
game in the demo), a game saved or loaded mid-demo, a format dsda-doom does not play, a versionless demo
without its game, a PWAD the footer names and no folder has.

## What has been run

**Every demo in every IWAD at hand plays as a movie exactly as the engine plays it**, tic for tic - the map,
the level time, the game's RNG, the kills, every player's position, angle and health (`run-gate.sh`, the demos
leg): Freedoom 0.13.0's eight (1.9 demos, one to three players), The Ultimate Doom's four, Doom II's three,
TNT's three, Plutonia's three, Chex Quest's four. So do all 39 of quickerDSDA's test demos: UV-max runs of
all of Doom II (178,756 tics), TNT (305,108) and Plutonia (213,894), each through its 32 maps with the secret
levels to MAP30 (Doom II's and TNT's seen to the game's end), the four Ultimate Doom UV-max episodes (each to its E?M8's end), Freedoom 0.11's
episode 1 in one demo (75,090 tics) and its single levels, Doom II's single-level and four-player demos.

**Every demo imports as itself** (`build/native/lmp-import`): imported, the project's movie plays as the engine
plays the demo, tic for tic - the IWADs' own demos (Heretic's and Hexen's too), all of quickerDSDA's (its
full-game runs through the importer as well), a demo of every format the engine records and of every other
format it plays, made from those (the gate), and 218 of 232 demos from the DSDA archive for the IWADs and PWADs
at hand, recorded in Doom2.exe, DOOM.EXE, Heretic, Hexen, Hexen+, CHexen, jHexen, Chocolate Doom, Chocolate
Hexen, Crispy Doom, PrBoom+ 2.5.1.x (complevels 2, 3, 4, 11), DSDA-Doom 0.24-0.29, Woof, CNDoom, Nyan Doom,
Sprinkled Doom, MBF, XDRE and TASDoom TAS work: Doom II, The
Ultimate Doom, TNT, Plutonia, Heretic, Hexen (its hub demos too), Chex Quest, Eviternity (complevel 11, its
players' option blocks), Sigil, Plutonia 2. The other 14: twelve refused for a file the footer names and the
folder lacks (Woof's extras.wad, a renamed Eviternity, PL2's .deh) or a format dsda-doom does not play
either (a version byte of 70); one whose PWAD no footer names (it was not given); one Hexen+ demo recorded with
`-longtics` that its header does not say - with `--longtics` it imports and plays, 11,558 tics through the
hub.

Over them: native == sandbox (every step's picture, sound and lag, the Game State), a savestate before every
step, a new host in the middle; Heretic and Hexen from their IWADs (native == sandbox, session; their demos are
in their own formats, which the demo leg does not read yet); Chex Quest 2, its PWAD on Chex Quest (native ==
sandbox, session). PWADs from the slot, each on its game, 1,500 steps native == sandbox: Sigil and Sigil II
(E5, E6, UMAPINFO) on The Ultimate Doom, the Master Levels and Eviternity (complevel 11, its DeHackEd and
graphics) on Doom II, Plutonia 2 on Plutonia, TNT's fixed MAP31 on TNT. A PWAD on the wrong IWAD is refused
with the engine's own reason ("Texture errors: 91! PL2.WAD seems to be incompatible with DOOM 2"). And through Chimera's
own engine: chimera-run plays a demo as a movie in Chimera's format to the same machine, with and without a
savestate every frame.

## The patches

- `0001-headless-setup.patch`: `D_DoomMainSetup()`, D_DoomMain's setup without its loop, for the core's.
- `0002-error-hook.patch`: `I_Error` hands its message to a weak `chimera_error_message()` first.
- `0003-level-exit-inventory.patch`: BizHawk's level-exit and game-end detection and prevention (`G_Ticker`).
- `0004-players-from-frontend.patch`: the players in the game and whose view, the frontend's.
- `0005-intercepts-overrun-playerstarts.patch`: BizHawk's vanilla intercepts overrun, which reaches the player
  starts (TASEmulators/dsda-doom dafa0543).
- `0006-automap-for-the-frontend.patch`: the automap's zoom, pan and marks, reachable by the frontend.
- `0007-rngseed.patch`: `-rngseed`, the seed a new game starts with (BizHawk's).
- `0008-stepped-wipe.patch`: the melt a tic at a time (`D_StepWipe`), for a frontend that steps it.
- `0009-hexen-classes-from-frontend.patch`: each Hexen player's class, the frontend's.
- `0010-patch-spare-column.patch`: a spare column after a patch's last: a sprite's drawer can read past its
  pixels, which were the column table's pointers - a picture that differed with where memory lay.
- `0011-demo-exact.patch`: every run played as a recorded demo is (see above); `-pistolstart` kept.
- `0012-frontend-demo-state.patch`: what a demo's header sets at the start, the frontend's: a Boom-or-later
  option block (`G_ReadOptions`), or an old demo's forced options; dsda's extended commands; `-emulate` outside
  playback too.
- `0013-checkspot-absent-players.patch`: a deathmatch start skips the players not in the game (without player
  one, vanilla's `G_CheckSpot` dereferenced its missing body).

## Building

```
git submodule update --init
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses
make -C waterbox -f guest.mk -j$(nproc)     # core.wbx
./waterbox/build-package.sh                 # build/package/dsda.chimeraCore
```

miniBox is taken from `MB=`/`MINIBOX_DIR` (`-m` for the scripts), else a Chimera checkout's in `~/chimera`, with
its C++ guest toolchain built (`meson setup build/meson-cpp -Dguest_cpp=true`). The patches go onto
`extern/dsda-doom` on the first build (`waterbox/apply-patches.sh`, all or nothing); dsda-doom.wad, the
engine's own data, is built from upstream's `data/` with its own tool. `waterbox/gen-declaration.py` writes
`waterbox.config`, `file_slots.json`, `default_keybinds.json` and `dsda-versions.h`.

## The gate

`./waterbox/run-gate.sh [-m <miniBox>] [-f <Freedoom 0.13.0>] [-i <IWAD folder>] [-c <chimera-run>]`. The
commercial IWADs are not the core's to carry, so the gate's content is **Freedoom** (BSD-3-Clause, downloaded from
its releases when `-f` does not name it), **levels of its own** (`tests/make-rooms.py`: a PWAD of one-room maps
with an exit switch, and a demo through them) and **demos the engine records**: each demo is imported
(`build/native/lmp-import`) and its project's movie played = the engine playing the demo itself, tic for tic -
Freedoom's eight; the levels' exits and intermissions; a demo of every format dsda-doom records, from a movie that
works every input (`tests/make-recording-movie.py`: every complevel, longtics and shorttics, dsda's format with
its extended commands, co-op, deathmatch, the monster flags, a seed, the footer's arguments) and of every format
it plays and does not record, made from those (`tests/craft-demo.py`: 1.2, 1.4, 1.5, LxDoom, Boom 2.00, option
blocks of every layout, footers, special commands, PrBoom+um); a changed option block's project without it is not
the demo; five demos the importer refuses; the core's `ImportMovie` the same in both builds and as the command
line's. Then native == sandbox, rerecord and session on a three-player demo; the melt's lag in both builds; turbo,
also across the melts; the settings in both builds; the declaration up to date; seven refusals; no host clock in
the guest; teeth; and with `-c` an imported project through Chimera's own engine (`chimera-run --project`). With
`-i`, every IWAD in the folder the declaration pins: its own demos (Doom's, Doom II's, Final Doom's, Chex Quest's,
Heretic's, Hexen's) imported, Heretic and Hexen demos the engine records (their header's flags, longtics, Hexen's
classes and warp numbers, co-op, dsda's format), native == sandbox and session.

## Where things are

- `waterbox/dsda-driver.c`: the machine - the settings, the engine's arguments, the step, BizHawk's input
  translation, the automap and camera, the picture, the sound, the domains. `waterbox/dsda-properties.h`: the
  property table.
- `waterbox/dsda-input.c`: the three controllers, in BizHawk's order.
- `waterbox/platform/`: what the engine's SDL and OpenGL code was - the video (headless), the SDL shim, the
  clock, the stubs, `detmath.c` (the math the renderer calls, the same in every build).
- `waterbox/wbx-entry.c`: the exports. `run-native.c`, `run-wbx.c`, `gate-harness.h`: the harnesses.
- `waterbox/lmp-import.cpp`: the importer (the core's `ImportMovie`, and the command line's);
  `tools/lmp-import.cpp`: its command line.
- `waterbox/gen-declaration.py`, `waterbox-base.json`: the declaration. `waterbox/tests/`: the gate's tools (the
  rooms, the recording movie, the crafted demos, a project as a harness folder).
- `docs/PLAN.md`: the decisions and what is left.

## Licence

This repository is GPL-2.0-or-later, as dsda-doom is (`extern/dsda-doom`, GPL-2.0-or-later: PrBoom+'s, id
Software's Doom source and Raven's Heretic and Hexen sources, all released under the GPL). zlib
(`extern/zlib`) is under the zlib licence. `waterbox/compat/GL/gl.h` is Mesa's (MIT), `glext.h` and
`KHR/khrplatform.h` are the Khronos Group's (MIT): headers only, which the engine's sources include for their
OpenGL declarations (the GL renderer is not compiled in; the core stubs it). The package carries no game data but dsda-doom.wad, the engine's
own (GPL, built from upstream's `data/`).
