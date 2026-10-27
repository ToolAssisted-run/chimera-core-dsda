# Building the DSDA-Doom core

This repository builds dsda-doom, the speedrunning and TAS source port of
Doom, as a Chimera game core: one game engine built as a core, not an emulator
(Chimera's `docs/game-cores.md`). It plays Doom, Doom II, Final Doom, Heretic,
Hexen, Chex Quest and Freedoom from the user's own IWAD. The result is one
file, `dsda.chimeraCore`, which Chimera loads. The steps below follow
`.github/workflows/chimera.yml`, which builds and gates the core from a fresh
clone on a public Ubuntu runner.

Placeholders used below:

- `<core>`: the checkout of this repository.
- `<chimera>`: a checkout of Chimera (https://github.com/ToolAssisted-run/chimera).
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, Chimera's miniBox
  submodule (the sandbox host and the guest toolchain).

## Requirements

- Linux. CI runs on GitHub's `ubuntu-latest` runner. Cores are built on Linux;
  the package they produce runs on Linux and on Windows.
- The apt packages the workflow installs:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip
  ```

- Qt 6.12, to build Chimera's window. Chimera's `tools/ci-chimera.sh`
  installs it (from Qt's own archives, with aqt) and builds Chimera; the
  workflow keeps it in `~/qt` between runs. It is needed to run Chimera's
  contract tests, not to build the package.
- The compilers are the system's `gcc` and `g++` (from `build-essential`). The
  workflow pins no compiler version. The guest is compiled by the same `gcc`
  and `g++` over miniBox's guest sysroot (`waterbox/guest.mk`).
- No Rust toolchain. The workflow installs no Python packages with pip.
- What the scripts build or fetch themselves:
  - `build/dsda-doom.wad`, the engine's own data (fonts, HUD pictures,
    tables), built from upstream's `prboom2/data` with upstream's own tool
    through `cmake` (`waterbox/sources.mk`). Both makefiles build it and the
    package carries it.
  - Freedoom 0.13.0, the gate's content. Unless `-f` names a folder that has
    it, the gate downloads `freedoom-0.13.0.zip` from Freedoom's releases with
    `curl`, once, into `build/freedoom-0.13.0`. CI caches that folder
    (`actions/cache@v4`, key `freedoom-0.13.0`). Building the package
    downloads nothing.

## Get the sources

The workflow checks out both repositories with their submodules, recursively
(`actions/checkout@v6`, `submodules: recursive`), and takes Chimera at `main`.
By hand:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera-core-dsda.git <core>
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

In a clone made without `--recursive`:

```
git -C <core> submodule update --init --recursive
git -C <chimera> submodule update --init --recursive
```

This repository has two submodules (`.gitmodules`): `extern/dsda-doom` (the
engine, upstream v0.30.0) and `extern/zlib`.

CI puts the Chimera checkout at `<core>/chimera-checkout`. Any place works:
the scripts find miniBox in this order.

1. `-m <miniBox dir>` on `run-gate.sh` and `build-package.sh`, or `MB=<dir>`
   on the `make` command line.
2. The `MINIBOX_DIR` environment variable (what CI sets).
3. For `build-package.sh -r <chimera>`: `<chimera>/extern/chimera-common-minibox`.
4. `~/chimera/extern/chimera-common-minibox`, else
   `~/chimera/extern/tools/chimera-common-minibox` (older Chimera checkouts).

## Build miniBox

The workflow builds Chimera itself first, then miniBox. Chimera's own build
gives `<chimera>/build/dll/chimera-run` (used by the gate's engine leg) and the
managed solution (used by the contract tests). The package does not need it:
`build-package.sh` uses only miniBox.

```
<chimera>/tools/ci-chimera.sh
```

Then miniBox: the host, and the guest toolchain with C++ (dsda-doom has C++
parts, and so has the core's demo importer).

```
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

- `build/meson-linux` holds the miniBox host library
  (`source/host/libminiboxhost.so`), which the `run-wbx` harness links.
- `build/meson-cpp` holds the guest sysroot (`guest-sysroot`: musl and
  libstdc++) and the guest objects `guest.mk` links.

CI caches these two build directories (`actions/cache@v4`). By hand there is
nothing to do: the directories stay where they are, and the `[ -f ... ] ||`
guards skip `meson setup` when one is already configured.

## Build the core

`run-gate.sh` and `build-package.sh` run the builds themselves, so this step
is only needed to build without gating or packaging.

```
cd <core>
make -C waterbox -f native.mk MB=<minibox> -j"$(nproc)"
make -C waterbox -f guest.mk MB=<minibox> -j"$(nproc)"
```

- **Patches.** `patches/` holds thirteen numbered patches for
  `extern/dsda-doom`. Both makefiles apply them first, through
  `waterbox/apply-patches.sh` (`build/patches.stamp` records that it ran). The
  script judges the series as a whole. A pristine submodule gets every patch.
  A tree that already carries the whole series is left alone. Anything in
  between is an error that names the files and prints the command to start
  again. The series is first tried on a scratch copy, so a series that does
  not apply never touches the tree. The script can also be run by hand:
  `./waterbox/apply-patches.sh`.
- **The engine's data.** Both makefiles build `build/dsda-doom.wad` (see
  Requirements; the log is `build/wad.log`).
- **The guest core** (`guest.mk`): `build/guest/core.wbx`. It is dsda-doom,
  zlib and the core's own sources, built with the system compilers over
  miniBox's guest sysroot. miniBox's `check-wbx.sh` checks the result (no
  thread-local storage, no `%fs`, no red zone) before it counts as built.
- **The native reference** (`native.mk`): `build/native/run-native`,
  `build/native/run-wbx` and `build/native/lmp-import`. `run-native` is the
  same sources built for the host with no sandbox. `run-wbx` drives `core.wbx`
  through the miniBox host as the frontend does. The gate compares the two
  step by step. `lmp-import` is the command line of the core's demo importer:
  it writes a demo (`.lmp`) as a Chimera project (`README.md`, "Importing a
  demo"). None of them is part of the package.

`sources.mk` is included by both makefiles, so both build the same files with
the same defines. Each object depends on the flags it was built with: a change
of flags rebuilds it. `make -C waterbox -f guest.mk clean` and
`make -C waterbox -f native.mk clean` remove `build/guest` and `build/native`.

`waterbox/waterbox.config`, `default_keybinds.json`, `file_slots.json`,
`dsda-games.h` and `dsda-options.h` are generated by
`waterbox/gen-declaration.py` and committed. Change the generator (or
`waterbox/waterbox-base.json`, which it reads) and run
`python3 waterbox/gen-declaration.py`, never the five files.
`python3 waterbox/gen-declaration.py --check` says whether they are up to
date and writes nothing.

## Build the package

```
cd <core>
./waterbox/build-package.sh -r <chimera>
```

`waterbox/build-package.sh [-m <miniBox dir>] [-r <chimera root>] [-o <out dir>]`:

- `-r <chimera root>` writes `<chimera root>/build/Cores/dsda.chimeraCore` and
  takes miniBox from that checkout unless `-m` or `MINIBOX_DIR` names another.
  It also removes `<chimera root>/build/CoreCache/dsda-*`. This is what CI runs
  (`-r chimera-checkout`).
- `-o <out dir>` writes `<out dir>/dsda.chimeraCore` instead.
- With neither, the package is `<core>/build/package/dsda.chimeraCore`.

The script builds the guest (`guest.mk`; the log is `build/package-make.log`),
checks `core.wbx`, and packs `core.wbx`, `waterbox.config`,
`default_keybinds.json`, `file_slots.json`, `assets/dsda-doom.wad`, the
licence texts and a `build.json` that records what built the package. The
packing is deterministic: the script packs twice and stops if the two SHA-1s
differ, then prints `package sha1 <hash>`.

The version is the commit the package was built from.

- CI sets `CORE_VERSION` to the commit's full hash.
- By hand, with `CORE_VERSION` unset, the script stamps `<commit>+local`
  (twelve hex digits), or `<commit>-dirty+local` when the tree has changes.
  The patches applied inside `extern/dsda-doom` do not count as changes.

A hand-built package is for testing. Chimera's publish script refuses a
version that carries `+local` or `-dirty`.

CI publishes what its gate passed as this repository's own releases: a rolling
`dev` release on every green push to `main`, and a dated `nightly-YYYY-MM-DD`
release from the scheduled run (04:00 UTC, only when `main` moved since the
last one). The gate job uploads the package as the artifact `dsda-<commit>`
(`actions/upload-artifact@v7`), and the publish job hands it to Chimera's
reusable workflow `publish-core.yml`. There is no manual equivalent.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets into Chimera because somebody put its package file in the cores folder.

- **A release bundle of Chimera**: download the core's `.chimeraCore` package
  from this repository's Releases page
  (https://github.com/ToolAssisted-run/chimera-core-dsda/releases), or build
  it, and put it in the `cores` folder beside Chimera's program. Another folder can
  be chosen in Tools > Core Manager... > Change folder...
- **A Chimera source checkout**: the cores folder is `<chimera>/build/Cores/`.
  `./waterbox/build-package.sh -r <chimera>` writes the package straight there.

Tools > Core Manager... lists what is in the folder. Refresh List rescans it. The
same package file works on Linux and on Windows: the guest inside it is run by
Chimera's sandbox (miniBox) on either.

## Run the gates

### The core gate

```
cd <core>
MINIBOX_DIR=<minibox> ./waterbox/run-gate.sh -c <chimera>/build/dll/chimera-run
```

This is the command CI runs. The gate builds the native reference and
`core.wbx` first (logs: `build/gate-native.log`, `build/gate-guest.log`),
works in `build/gate` (emptied at each run), prints `PASS` or `FAIL` for every
leg, and ends with `gate: all legs passed` or `gate: N leg(s) failed` and a
non-zero exit.

It proves two things: the native reference and the sandboxed core are the same
machine, and a demo imported as a Chimera project plays as the engine plays
the demo itself, tic for tic. The commercial IWADs are never in the
repository, so the content is Freedoom 0.13.0, levels the gate makes
(`waterbox/tests/make-rooms.py`) and demos the engine records. The legs, as
the script's header lists them: demos, levels, formats, imports, export (the
core's own `ImportMovie`), equivalence (every step's picture, sound and lag,
the clock, the Game State), rerecord (a savestate saved and loaded before
every step), session (saved, a new host, loaded, finished), turbo, settings,
declaration (`gen-declaration.py --check`), refusals, clock (the guest has no
clock of its own), teeth (a movie one step different must digest differently),
engine and iwads.

Usage: `run-gate.sh [-m <miniBox dir>] [-f <Freedoom 0.13.0 dir>] [-i <IWAD dir>] [-c <chimera-run>]`

- `-f <dir>` names a folder with Freedoom 0.13.0's `freedoom1.wad` and
  `freedoom2.wad`. Without it the gate downloads them (see Requirements).
  Either way the gate checks each file's SHA-1 against the dumps the core
  knows and stops if it is not 0.13.0's.
- `-c <chimera-run>` runs the engine leg: the script packages the core and
  plays an imported demo's project through Chimera's own engine, headless,
  with and without rerecording. Without `-c` the leg does not run.
- `-i <dir>` runs the iwads legs on a folder of IWADs. The gate takes those
  whose SHA-1 is one of the dumps `gen-declaration.py` lists, whatever their
  names: each IWAD's own demos imported, and native == sandbox and session on
  each. Without `-i` these legs do not run. CI does not pass `-i`: those IWADs
  are the user's.

### Chimera's contract tests

Run after the package is in `<chimera>/build/Cores`:

```
CHIMERA_CORES_DIR=<chimera>/build/Cores <chimera>/build/meson-linux/source/ui/test-packages
```

They run Chimera's own checks against this package: it is readable, it is
built for an ABI this frontend runs, it makes a working factory, it binds only
buttons its controller declares, and it stamps a version. They need the
Chimera solution built and no game files.

## Files the core needs at run time

The package carries no game data but `dsda-doom.wad`, the engine's own. A
project picks a game (the System in Chimera's new-project wizard) and brings
that game's IWAD as firmware. `waterbox/waterbox.config` declares one IWAD for
each game:

| Game (`game` setting) | The IWAD the user provides (firmware id) |
| --- | --- |
| Doom II (`doom2`) | `doom2.wad` |
| Doom (`doom`) | `doom.wad` |
| Final Doom: TNT - Evilution (`tnt`) | `tnt.wad` |
| Final Doom: The Plutonia Experiment (`plutonia`) | `plutonia.wad` |
| Heretic (`heretic`) | `heretic.wad` |
| Hexen (`hexen`) | `hexen.wad` |
| Chex Quest (`chex`) | `chex.wad` |
| Freedoom: Phase 1 (`freedoom1`) | `freedoom1.wad` |
| Freedoom: Phase 2 (`freedoom2`) | `freedoom2.wad` |

- Any release of a game's IWAD is taken, and so is a modified IWAD. No hash is
  pinned in the declaration: the project pins the hash of the file it was
  given.
- PWADs and DeHackEd patches are the project's own files, in the `pwad` slot
  (`waterbox/file_slots.json`: `.wad`, `.deh`, `.bex`, in load order). The slot
  may be empty: the game itself needs none.
- No BIOS and no other firmware is needed.

## Troubleshooting

- `miniBox not found at ...; pass -m <path> or set MINIBOX_DIR`
  (`build-package.sh`): no miniBox was named and none is under `~/chimera`.
  Pass `-m`, `-r` or `MINIBOX_DIR`.
- `miniBox's C++ guest toolchain is missing: ...` (`guest.mk`): miniBox was
  built without `-Dguest_cpp=true`. Build `build/meson-cpp` as in "Build
  miniBox".
- `extern/dsda-doom is not checked out` (`apply-patches.sh`): the clone was
  made without its submodules. Run the `git submodule update` command it
  prints.
- `extern/dsda-doom is partly patched` (`apply-patches.sh`): the submodule's
  tree is neither pristine nor what the whole series leaves. The script prints
  the way back:
  `git -C extern/dsda-doom reset --hard && git -C extern/dsda-doom clean -fd && waterbox/apply-patches.sh`.
  That discards edits made in the tree: turn them into a patch first.
- `the series does not apply to the submodule's HEAD at <patch>`
  (`apply-patches.sh`): the submodule was moved without rebasing the patches.
- `FAIL freedoom: ... is not 0.13.0's` (the gate): the folder `-f` names, or
  `build/freedoom-0.13.0`, holds another release of Freedoom. The gate stops
  there.
- `FAIL declaration: out of date: ...` (the gate): a generated file differs
  from what `gen-declaration.py` writes. Run
  `python3 waterbox/gen-declaration.py` and commit the result.
- `the guest build failed (build/package-make.log)` (`build-package.sh`): read
  that log. The gate's build logs are `build/gate-native.log` and
  `build/gate-guest.log`; the data WAD's is `build/wad.log`.
- `git status` shows `extern/dsda-doom` as modified after a build: the patches
  are applied in the submodule's working tree. That is expected, and it does
  not mark the package `-dirty`.
