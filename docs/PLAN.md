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

## The demo importer (2026-10-01)

The importer (first in Python, now `waterbox/lmp-import.cpp` - see below) reads every format dsda-doom plays and
makes the demo a project: its settings, its IWAD as the firmware, its PWADs in the slot, its tics as the movie.
What dsda's playback decides from a demo - the header (G_ReadDemoHeaderEx), dsda's own header and extended
commands (dsda/demo.c), the footer's arguments (dsda/exdemo.c) - the core now takes as settings, so a project can
say all of it:

- **The axes take a tic command's whole range**: BizHawk's Run and Strafe Speed stop at 50 and Weapon Select at
  7, which cannot name the chainsaw (key 8) or the super shotgun; hand-made and -turbo demos hold more. The
  keys' and the mouse's contributions are still held to 50; an axis alone is the byte. The weapon axis is the
  weapon's number, not a key: re-applying G_BuildTiccmd's chainsaw and super shotgun choice to a recorded
  weapon depends on the recording port's preferences (`P_WeaponPreferred`), so it stays with the keys.
- **Artifact**: BizHawk names the Raven games' artifact axis "Use Artifact", as it names the button that uses
  the inventory's. One name for two inputs is two columns no frontend tells apart (TAStudio, the mnemonics,
  the input log's reader key them by name); the import found it when Hexen's DEMO1 used an artifact the movie
  also "pressed". The axis is "Artifact" here.
- **Pause** is each player's, as the command's (BT_SPECIAL | BT_PAUSE: the player whose command it is loses
  its buttons that tic); in Doom dsda ignores the other specials and zeroes their buttons. Heretic and Hexen
  keep a special command's low bits until the player thinks - `P_DeathThink` reads its use (a dead player
  respawns), the intermission and finale its fire and use - so their controllers have Special, those seven
  bits. A Hexen+ demo found it: read without its unflagged longtics, its garbage held 0xFF, which respawned
  the player in playback and not in the movie.
- **Unflagged longtics**: Hexen+ recorded `-longtics` demos before vvHeretic's header flag; nothing in the
  file says so, and dsda plays them right only with `-longtics`. The importer's `--longtics` says it.
- **The option block** of a Boom-or-later demo (demo insurance, monster options, the comp flags) changes the
  game from the first tic; PrBoom+'s recordings carry demo insurance, which dsda's own no longer do. It is a
  string setting, the block as hex, applied by `G_ReadOptions` itself before the first `G_InitNew`, where
  playback applies it (patch 0012) - one setting rather than forty, exact for every layout (Boom's, MBF's,
  MBF21's with its comp count). The monster flags and the seed in it are the settings'.
- **An old demo's options**: playback of a 1.2-1.9, TASDoom, Heretic or Hexen demo forces friction and pushers
  off and the monster options to vanilla's, where a game outside demos has friction and pushers on - and the
  friction check that bounces a missile off a wall on ice has no complevel guard. Hexen's DEMO1 desynced at tic
  670 on a leaf; the core now forces the same when there is no option block (patch 0012). dsda's own
  recordings of those formats run with friction on, so a dsda recording of Hexen on ice is not what its
  playback plays; the demo's playback is what judges it.
- **Hexen's map is a warp number**: the core's Initial Map is what `-warp` takes (BizHawk's), and Hexen's warp
  goes through MAPINFO's `warptrans`; the importer reads the last MAPINFO (a PWAD's replaces the IWAD's) and
  writes the warp number that reaches the demo's map.
- **The RNG seed from complevel 7**: Boom 2.00's and 2.01's demos hold one; a game started at 7 or 8 without
  `-rngseed` took a time-based one.
- **Deathmatch without player one** crashed in vanilla's `G_CheckSpot` (patch 0013): such a demo would crash
  dsda's playback too, but settings must not crash the core.
- **What the gate proves**: every recordable format recorded by the engine, every other one crafted from those,
  the IWADs' own demos, all imported and played = `-playdemo`, tic for tic; quickerDSDA's demos and 218 of
  232 of the DSDA archive's for the WADs at hand (the rest: files their footers name and the folder lacks, a
  format dsda does not play, an unnamed PWAD, the unflagged longtics) the same, locally.

## The importer in the core (2026-10-01)

The importer is C++ now, `waterbox/lmp-import.cpp`, and part of the core: compiled into core.wbx and the native
reference like the driver, it answers the export `ImportMovie` - a frontend loads the core, mounts the demo as
`movie` and the WADs it reads by their names, gives the options as settings, and calls it instead of `Init`; the
parts of the project come back as JSON. The dialog is the declaration's (`movieImport`, which Chimera draws as it
draws the settings: the demo, the files and the option each sets, the options), so no Doom lives in Chimera's
code. Chimera keeps no core-specific code (its movie importers were removed with the rest), so a core that imports
its own demos is how an importer can reach a person; the frontend's side - a menu that offers a package's importer
for a file - is Chimera's to add. What the importer needs to read it asks for by name (the IWAD's hash for the
release, Hexen's MAPINFO for the warp number, the PWADs' hashes for the manifest), so the same code runs in the
sandbox and in `tools/lmp-import.cpp`, the command line that finds the files in folders and writes the
`.chimeraProject`. The settings' options come from the declaration (`dsda-options.h`, generated with it) and the
active inputs from the driver's own rule (`dsda_input_active`), so the importer cannot drift from the core. Before
the Python went, both importers imported every demo at hand - the gate's, the IWADs', quickerDSDA's, the DSDA
archive's: 449 - to the same projects and the same refusals.

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
  (the importer, as `G_GetOriginalDoomCompatLevel`: the footer's `-complevel`, else 3 on a game with a fourth
  episode, 4 on Final Doom, 2 otherwise).
- **The raw record domains** (Players, Things, Lines, Sectors) hold the engine's pointers: native and sandbox
  compare the Game State and the domains' sizes; sandbox against sandbox compares everything.

## Left

- **The importer in Chimera**: a frontend call of the core's `ImportMovie` (a session that loads the core,
  mounts the demo and the WADs, sets the options and calls the export instead of `Init`), and a menu that offers
  it for a file a package claims (`.lmp`).
- **What the importer refuses**: a start from a key frame would need the frame (dsda's saved game in the
  demo) as the movie's start; saves and loads mid-demo would need savegames inside the sandbox; Hexen's
  players 5-8 and Boom's 5-32 the core's ports.
- **Exporting a movie as a demo**: the other way, for DSDA's archive.
- **More releases**: Doom 1.9 (registered and shareware), Doom II 1.666, Heretic 1.3, Hexen 1.0, the BFG and
  Unity editions, Chex Quest 3 - a line each in `gen-declaration.py` once a dump is at hand to pin.
- **Chimera's Version selector** (`"versionSetting"`): the wizard's.
