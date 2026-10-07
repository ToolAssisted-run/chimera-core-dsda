# AGENTS.md - DSDA-Doom core for Chimera

This repository builds dsda-doom, the speedrunning and TAS source port of
Doom, as a GAME core (one game engine built as a core, not an emulator) for
Chimera (https://github.com/ToolAssisted-run/chimera), a frontend for
tool-assisted speedruns. It plays Doom, Doom II, Final Doom, Heretic, Hexen,
Chex Quest and Freedoom. It produces one file, `dsda.chimeraCore`: the engine
as a sandboxed guest (`core.wbx`), its declarations and the engine's own data
WAD. The games' IWADs are never in the repository or the package: the user
provides them.

## Layout

- `extern/dsda-doom`: upstream dsda-doom, a submodule. Never edited in place.
  `extern/zlib`: a library submodule.
- `patches/`: the thirteen numbered patches for `extern/dsda-doom`, applied
  by `waterbox/apply-patches.sh`.
- `waterbox/sources.mk`: the sources and defines both builds share, and the
  rule for `build/dsda-doom.wad`.
- `waterbox/guest.mk`: builds `build/guest/core.wbx` (the sandboxed core).
- `waterbox/native.mk`: builds `build/native/run-native`, `run-wbx` and
  `lmp-import` (the native reference, the harness that runs `core.wbx`, the
  demo importer's command line).
- `waterbox/build-package.sh`: builds the package. `run-gate.sh`: the gate.
- `waterbox/dsda-driver.c`: the machine. `dsda-input.c`: the controllers.
  `platform/`, `compat/`: what stands in for the engine's SDL and OpenGL code.
- `waterbox/lmp-import.cpp`: the demo importer (the core's `ImportMovie`).
  `tools/lmp-import.cpp`: its command line.
- `waterbox/gen-declaration.py` (with `waterbox-base.json` and
  `tools/dump-input.c` beside it): GENERATES `waterbox.config`,
  `default_keybinds.json`, `file_slots.json`, `dsda-games.h`, `dsda-options.h`.
- `waterbox/package-licenses.json`: the licence terms the package carries.
- `.github/workflows/chimera.yml`: CI. It gates, packages and publishes.
- `build/`: every output. Ignored by git.

## Set up the build environment

Linux only (CI: `ubuntu-latest`). `<chimera>` is a Chimera checkout.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip

git submodule update --init --recursive
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>

# Chimera itself: chimera-run (the gate's engine leg) and the contract tests.
# The dotnet line needs the .NET SDK 8.0.
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

# miniBox: the sandbox host and the C++ guest toolchain
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

The scripts take miniBox from `-m <dir>` (or `MB=<dir>` for `make`), else
`MINIBOX_DIR`, else a Chimera checkout in `~/chimera`.

## Build

From the repository root. One command applies the patches, builds the guest
and the engine's data WAD, and writes the package:

```
./waterbox/build-package.sh -r <chimera>
```

`-r <chimera>` writes `<chimera>/build/Cores/dsda.chimeraCore` and uses that
checkout's miniBox. `-o <dir>` writes `<dir>/dsda.chimeraCore`. With neither
the package is `build/package/dsda.chimeraCore`. `-m <dir>` names miniBox.

The native reference and the guest alone:

```
make -C waterbox -f native.mk MB=<chimera>/extern/chimera-common-minibox -j"$(nproc)"
make -C waterbox -f guest.mk MB=<chimera>/extern/chimera-common-minibox -j"$(nproc)"
```

A hand-built package stamps its version `<commit>+local` (`-dirty` when the
tree has changes) and is for testing. CI stamps the commit through
`CORE_VERSION` and publishes the releases.

## Install the core into Chimera

Chimera ships no cores and downloads nothing: a core is a file in its cores
folder. In a source checkout that is `<chimera>/build/Cores/`, where
`build-package.sh -r <chimera>` writes. In a release bundle it is the `Cores`
folder beside `Chimera.exe`, or the one chosen in File > Core Manager >
Change folder... File > Core Manager lists the folder; Refresh List rescans
it. The same package works on Linux and on Windows.

## Test before you commit

The gate must end with `gate: all legs passed`:

```
MINIBOX_DIR=<chimera>/extern/chimera-common-minibox ./waterbox/run-gate.sh -c <chimera>/build/dll/chimera-run
```

It builds both flavours itself. Its content is Freedoom 0.13.0: the gate
downloads it once into `build/freedoom-0.13.0` (`curl`, network access), or
takes it from the folder `-f <dir>` names. `-c` adds the engine leg (the
package through `chimera-run`). The legs on the other IWADs run only with
`-i <IWAD dir>`, where those files are; CI cannot run them.

Then, with the package in `<chimera>/build/Cores`, Chimera's contract tests:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

## Rules of this repository

- `extern/dsda-doom` is a submodule. A change to it is a numbered patch in
  `patches/`, applied by `waterbox/apply-patches.sh`. Never commit inside the
  submodule. The script takes the series as a whole: a pristine tree gets all
  of it, a fully patched tree is left alone, anything else is refused with the
  command to reset the tree. `extern/zlib` carries no patches.
- The five generated files (Layout) are never edited by hand. Edit
  `waterbox/gen-declaration.py` (or `waterbox-base.json`), run
  `python3 waterbox/gen-declaration.py`, commit all of it. The gate's
  declaration leg fails when they are out of date.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The gate checks it (equivalence, rerecord, session, clock).
- A movie is a demo: a demo imported as a project must play as the engine
  plays the demo itself, tic for tic (the gate's demos, levels, formats legs).
- Run the gate before committing. A new leg needs a negative control: show it
  fails when the thing it checks is broken (the gate's own `teeth` leg is the
  model; Chimera's `docs/gates.md` says why).
- Never commit IWADs or any other game file. `tests/roms-local/` and `build/`
  are ignored by git. Never add network access to the core.
- Shell scripts stay executable (git mode 100755): `waterbox/*.sh`.
- Documentation prose is plain ASCII.
- Commit messages: the subject states what is now true, often after a topic
  and a colon ("dsda: the demo importer - every format dsda-doom plays, as a
  Chimera project"). The body says what changed and why.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md`: every option, the files a user provides, troubleshooting.
- `README.md`: the games and their IWADs, importing a demo, the patches.
- `docs/PLAN.md`: the decisions behind the core, and what is left.
- The header comments of the scripts and makefiles in `waterbox/`.
- Chimera's `docs/`: `game-cores.md`, `porting-a-core.md`, `core-manager.md`,
  `gates.md`.
