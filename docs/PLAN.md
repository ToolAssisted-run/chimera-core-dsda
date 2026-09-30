# chimera-core-dsda: plan and decisions

Started 2026-09-30, from the rawgl core's shape (the driver, the harnesses, the gate, the package), with
BizHawk's DSDA core (`waterbox/dsda`, `src/BizHawk.Emulation.Cores/Computers/Doom`) as the precedent and
quickerDSDA (JaffarPlus's DSDA, ToolAssisted-run/quickerDSDA) for its movies and tests.

## Done

- **The engine**: upstream dsda-doom v0.30.0, its latest release, rather than BizHawk's fork
  (TASEmulators/dsda-doom, an older dsda). BizHawk's changes to the engine are ported onto it as patches
  0001-0009 (the headless setup, the level exit, the players and classes from the frontend, the intercepts
  overrun, the automap, `-rngseed`, the stepped melt); its `BizhawkInterface.c` is the driver's input, automap
  and camera code.
- **The platform**: no SDL, no OpenGL, no window: `waterbox/platform/` answers the engine's SDL calls (the
  clock is the machine's, a tic 1/35 s), draws nothing but the software renderer's buffer, and stubs the GL
  renderer. Upstream's `i_sound.c` is compiled as it is; its mixer is captured a tic at a time
  (`I_GrabSound`), the music by its OPL synthesizer. The configuration is `-assign`ed, never a file: no
  config, no autoload, no data folder of the host's (`-config`/`-data` point at nothing, `M_MakeDir` does
  nothing).
- **The step**: BizHawk's - the players' tic commands into the slot `G_Ticker` reads (`gametic %
  BACKUPTICS`), then `D_DoAdvanceDemo`, `M_Ticker`, `G_Ticker`, the sounds, `D_Display`. The melt is stepped
  (`D_StepWipe`, patch 0008) and its steps are lag.
- **Determinism**: the engine's `time()`, `clock_gettime()`, `getcwd()`, `access()` and the math the renderer
  calls (`atan`, `asin`, `tan`, `pow`, `sincos`) are the core's (`--wrap`, `detmath.c`); patch 0010 stops a
  sprite drawer reading pointer bytes past a patch's last column, which made the native and sandboxed pictures
  differ on two frames of 27 movies.
- **Controllers and settings**: BizHawk's, generated with the declaration (`gen-declaration.py`) from the
  core's own tables (`tools/dump-input.c`); the default keys from BizHawk's `defctrl.json`.
- **The machines**: one per game (the System), the IWAD's release a setting each machine narrows (the
  Version), the IWAD that release's firmware.
- **Savestates**: the engine's heap is guest memory; rerecord and session pass (about 7 MB a state on a
  three-player Freedoom level).
- **The gate** (`waterbox/run-gate.sh`): Freedoom 0.13.0's demos and the gate's own levels, movie ==
  `-playdemo`; native == sandbox, rerecord, session, turbo, the melt, the settings, refusals, the clock, teeth,
  chimera-run; with `-i`, the commercial IWADs' demos.

## A movie is a demo (2026-09-30)

quickerDSDA's UV-max run of all of Doom II desynced as a movie on MAP02: the movie left MAP01's intermission
five tics before the demo did. dsda keeps two behaviours, one for demos and one outside them
(`allow_incompatibility`, which is `!demorecording && !demoplayback`), and outside demos v0.30's Doom II
"Now entering" screen is longer and skippable - which BizHawk's older engine does not have. The other
differences are of the same kind and some reach the game: a removed thing's target, tracer and last enemy are
cleared at the old complevels only outside demos, weapon autoswitch and the skill flags come from the
configuration, the level-reload keys work, Heretic's finale can be skipped.

A TAS core's movie should be what its demo would be - that is what makes a demo importable as a movie, and a
movie exportable as a demo. Patch 0011 makes every run a recorded demo's (`allow_incompatibility` false),
keeping only `-pistolstart` (BizHawk's Pistol Start, which demos forbid; a movie made with it is no demo DSDA
takes). With it the Doom II run plays as a movie exactly as the demo, all 178,756 tics, and the gate's levels
leg crosses intermissions on free data.

## Turbo draws (2026-09-30)

Turbo first set the engine's `nodrawers`. `D_Display` then returns before it notices a new game state, so a
level change in turbo started no melt: its lag steps vanished, and the movie after it shifted. What drawing
touches is also the status bar's and the border's redraw state (the first pictures after turbo differed) and
the renderer's marks in the lines and sectors. So turbo no longer stops the engine drawing; it skips the
picture's conversion only, and the machine is the same with and without it (the gate compares every domain).
At 320x200 drawing is a small part of a step; at the large scale factors turbo saves less.

## Decisions

- **Upstream, not BizHawk's fork**: the latest engine, its fixes and its formats (MBF21, UMAPINFO), with
  BizHawk's changes as patches; where the two engines differ in play (v0.30's longer "Now entering" screen)
  the demo's behaviour wins (patch 0011).
- **The Version is a setting** (`version`), each machine narrowing it to its game's releases
  (`settingOverrides`), declared as `"versionSetting"` for a wizard that shows it beside the System; Chimera
  shows it on the settings page until then. The IWAD's name is fixed per game - the engine reads the game and
  its mission from it - so a release's firmware id is that name, declared once per release with its own hash
  (Chimera's "an id declared many times").
- **The compatibility level stays a setting**, as in BizHawk: a demo's own is what the engine would give it
  (`tests/lmp2sol.py`, as `G_GetOriginalDoomCompatLevel`: 3 on a game with a fourth episode, 4 on Final Doom,
  2 otherwise).
- **The raw record domains** (Players, Things, Lines, Sectors) hold the engine's pointers: native and sandbox
  compare the Game State and the domains' sizes; sandbox against sandbox compares everything.

## Left

- **The replay importer** (next): a Doom `.lmp` into a chimeraProject - the header's settings, the tics as
  the controller's axes and buttons, the melt off (as BizHawk's importer has it), the complevel from the
  version and the game. `tests/lmp2sol.py` is its vanilla half; Boom, MBF, PrBoom+ and MBF21 headers, longtics,
  and Heretic's and Hexen's formats (BizHawk's `DoomLmpImport`, `HereticLmpImport`, `HexenLmpImport` read
  them) are to come, and then the gate's demo leg reads Heretic's and Hexen's demos too.
- **More releases**: Doom 1.9 (registered and shareware), Doom II 1.666, Heretic 1.3, Hexen 1.0, the BFG and
  Unity editions, Chex Quest 3 - a line each in `gen-declaration.py` once a dump is at hand to pin.
- **Chimera's Version selector** (`"versionSetting"`): the wizard's.
