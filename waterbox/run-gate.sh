#!/bin/sh
# run-gate.sh - the DSDA core's gate: the native reference and core.wbx are
# the same machine, and a demo imported as a Chimera project (tools/
# lmp-import.py) is the demo. It runs on Freedoom 0.13.0 (free: the gate fetches
# it, or -f names it) and, when -i names a folder of them, on the IWADs the
# declaration pins.
#
# Every leg says what it compared. A demo is "the engine's own playback, tic
# for tic" when the project's movie, played by the core, and the engine
# playing the .lmp itself (-playdemo, through the native reference's
# gate-args) agree at every tic on the map, the level time, the game state,
# the game's RNG, the kills, and each player's position, angle and health.
# The legs:
#   demos        Freedoom's eight demos (DEMO1-4 of both phases: 1.9 demos of
#                one to three players), imported
#   levels       the gate's own three rooms (tests/make-rooms.py: a PWAD of
#                one-room maps, an exit switch in each) and a demo through them,
#                imported: the exits and intermissions; with the melt
#                (renderWipescreen), native = sandbox and turbo across it
#   formats      a demo of every format dsda-doom records - each complevel (1.666,
#                1.9 at 2, 3 and 4, TASDoom, Boom 2.01/2.02, MBF, PrBoom 2.1 to
#                latest, MBF21), longtics and shorttics, dsda's own format with its
#                extended commands, co-op, deathmatch, alternate deathmatch
#                without player one, the monster flags, a seed, -solo-net and
#                -coop_spawns, levels and intermissions - recorded by the engine
#                from a movie that works every input (tests/make-recording-movie.py),
#                and those it plays and does not record (tests/craft-demo.py): 1.2,
#                1.4, 1.5, LxDoom, Boom 2.00, option blocks of every layout, a
#                footer's arguments, the join marker and save-game specials,
#                PrBoom+um's header - each imported; a changed option block's
#                project without it is not the demo (teeth)
#   imports      what the importer refuses, saying why: a fifth player, a start
#                from a key frame, a game saved mid-demo, a version no format
#                has, a versionless demo without its game
#   equivalence  run-native and run-wbx on Freedoom's DEMO4 (Phase 1's E4M6,
#                three players), imported: every step's picture, sound and lag,
#                the machine's clock and the Game State domain (the raw record
#                domains - Players, Things, Lines, Sectors - hold the engine's
#                pointers, which differ between the builds, so only their sizes)
#   rerecord     the sandbox saved and loaded before every step = without,
#                every domain
#   session      saved at a step, a new host, loaded, finished = without,
#                every domain
#   turbo        the first half's pictures not converted: the rest the same (the
#                engine draws on - what drawing touches is the machine's), the
#                first half's pictures really not delivered
#   settings     the skill, complevel, map and noMonsters where the engine keeps
#                them (both builds); scaleFactor 2's picture is 640x400; the
#                pwad slot's WAD (a DEHACKED lump) and patch (.deh) each change
#                the player's initial health
#   declaration  gen-declaration.py writes what the repo holds (waterbox.config,
#                file_slots.json, default_keybinds.json, dsda-versions.h)
#   refusals     no IWAD, a version that is another game's, a version the core
#                does not have, no player, scaleFactor 13, an option block that
#                is not hex, one where the complevel has none, one of the wrong
#                size: each says why
#   clock        the guest has no clock of its own: time() and clock_gettime()
#                are the core's
#   teeth        the equivalence comparison sees a one-step difference
#   engine       (with -c) the package through Chimera's own engine, headless:
#                chimera-run --project plays an imported demo's project (Phase
#                2's DEMO1, pinned to the package; its firmware given, as the
#                tool takes it) to the harness's Game State, and the same with
#                --rerecord
#   iwads        (with -i) each IWAD in the folder whose SHA1 the declaration
#                pins: its own demos - Doom's, Doom II's, Final Doom's, Chex
#                Quest's, Heretic's and Hexen's - imported; Heretic and Hexen
#                demos recorded by the engine (the Raven header's flags, longtics,
#                Hexen's classes and warp numbers, co-op, dsda's format); native
#                = sandbox and session on each IWAD's first demo
#
# usage: run-gate.sh [-m <miniBox dir>] [-f <Freedoom 0.13.0 dir>] [-i <IWAD dir>]
#                    [-c <chimera-run>]
#   -f a folder with Freedoom 0.13.0's freedoom1.wad and freedoom2.wad; without
#      it the gate downloads freedoom-0.13.0.zip from Freedoom's releases into
#      build/freedoom-0.13.0 (once)
#   -i a folder of IWADs, found by their SHA1 whatever their names (never in the
#      repo)
#   -c runs the engine leg with that chimera-run (build/dll/chimera-run of a
#      Chimera checkout); it packages the core first
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
if [ -z "$mb" ]; then
	for d in "$HOME/chimera/extern/chimera-common-minibox" "$HOME/chimera/extern/tools/chimera-common-minibox"; do
		[ -n "$mb" ] || [ ! -d "$d" ] || mb="$d"
	done
fi
freedoom=""
iwads=""
chimera_run=""
while getopts "m:f:i:c:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		f) freedoom="$OPTARG" ;;
		i) iwads="$OPTARG" ;;
		c) chimera_run="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" && pwd)"

echo "== build (miniBox $mb)"
mkdir -p "$root/build"
make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-native.log" 2>&1 || {
	tail -20 "$root/build/gate-native.log"; echo "native build failed"; exit 1; }
make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-guest.log" 2>&1 || {
	tail -20 "$root/build/gate-guest.log"; echo "guest build failed"; exit 1; }
native="$root/build/native/run-native"
wbx="$root/build/native/run-wbx"
core="$root/build/guest/core.wbx"
wad="$root/build/dsda-doom.wad"
importer="$root/tools/lmp-import.py"
tests="$here/tests"

work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work/files"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# the digests only (miniBox's own log lines go to the same stdout); across the
# builds, not the raw record domains' contents (their pointers)
digests() { grep -E '^[A-Za-z]+(\[[^]]*\])?=' "$1" || true; }
xdigests() { digests "$1" | grep -vE '^domain\[(Players|Things|Lines|Sectors)\]=' || true; }
compare() { # compare <digests|xdigests> <leg> <a> <b> <what>
	$1 "$3" > "$3.d"
	$1 "$4" > "$4.d"
	if cmp -s "$3.d" "$4.d"; then
		pass "$2: $5"
	else
		fail "$2: $5"; diff "$3.d" "$4.d" | head -20
	fi
}
value() { digests "$1" | sed -n "s/^$2=//p"; }
# a trace line: step, rate, input read, then the values; the last line's nth
last() { tail -1 "$1" | awk -v col="$2" '{print $(3 + col)}'; }
sha1_of() { sha1sum "$1" | awk '{print toupper($1)}'; }
# the firmware the declaration pins: "<sha1> <version> <game> <firmware id>" a line
python3 - "$here/waterbox.config" > "$work/pinned" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1]))
game = {v: m["when"][0] for m in cfg["machines"] for v in m["settingOverrides"]["version"]["options"]}
for f in cfg["firmware"]:
    v = f["requiredWhen"]["is"]
    print(f["sha1"].upper(), v, game[v], f["id"])
PY
# a WAD's lump to a file: wad_lump <wad> <LUMP> <out>
wad_lump() {
	python3 - "$@" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
_, n, o = struct.unpack('<4sii', d[:12])
for i in range(n):
    p, s, name = struct.unpack('<ii8s', d[o + 16 * i:o + 16 * i + 16])
    if name.rstrip(b'\0').decode('latin1').upper() == sys.argv[2]:
        open(sys.argv[3], 'wb').write(d[p:p + s]); break
else:
    sys.exit('no lump ' + sys.argv[2])
PY
}

# a work folder: <dir> <IWAD file> <firmware id> [settings JSON]
mkwork() {
	mkdir -p "$1"
	cp "$2" "$1/$3"
	cp "$wad" "$1/dsda-doom.wad"
	[ -z "${4:-}" ] || printf '%s' "$4" > "$1/settings"
}

echo "== Freedoom 0.13.0"
if [ -z "$freedoom" ]; then
	freedoom="$root/build/freedoom-0.13.0"
	if [ ! -f "$freedoom/freedoom2.wad" ]; then
		curl -sSL -o "$root/build/freedoom-0.13.0.zip" https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip
		python3 -c "
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if n.endswith(('freedoom1.wad', 'freedoom2.wad')):
        open(sys.argv[2] + '/' + n.split('/')[-1], 'wb').write(z.read(n))" "$root/build/freedoom-0.13.0.zip" "$(mkdir -p "$freedoom" && cd "$freedoom" && pwd)"
	fi
fi
for p in 1 2; do
	s="$(sha1_of "$freedoom/freedoom$p.wad")"
	if grep -q "^$s freedoom$p-0.13.0 " "$work/pinned"; then
		pass "freedoom: freedoom$p.wad is 0.13.0's, as the declaration pins it ($s)"
		ln -sf "$(cd "$freedoom" && pwd)/freedoom$p.wad" "$work/files/freedoom$p.wad"
	else
		fail "freedoom: $freedoom/freedoom$p.wad ($s) is not 0.13.0's"; exit 1
	fi
done
fd2="$freedoom/freedoom2.wad"
# the IWADs -i names, in the files folder under their firmware ids
if [ -n "$iwads" ]; then
	for f in "$iwads"/*; do
		[ -f "$f" ] || continue
		line="$(grep "^$(sha1_of "$f") " "$work/pinned" | head -1 || true)"
		[ -n "$line" ] || continue
		set -- $line
		case $3 in freedoom1|freedoom2) continue ;; esac
		ln -sf "$(cd "$(dirname "$f")" && pwd)/$(basename "$f")" "$work/files/$4"
		echo "$2 $3 $4" >> "$work/iwads"
	done
fi

props="Game.Tic,Game.Map,Game.Level Time,Game.State,RNG.Index,Level.Total Kills"
for i in 1 2 3 4; do props="$props,P$i.X,P$i.Y,P$i.Z,P$i.Angle,P$i.Health"; done

# import_check <dir> <demo.lmp> <leg> [importer arguments...]: the demo imported
# (dir/p.chimeraProject), its movie = the engine's own playback
import_check() {
	d="$1"; lmp="$2"; leg="$3"; shift 3
	mkdir -p "$d"
	if ! python3 "$importer" "$lmp" -o "$d/p.chimeraProject" --wads "$work/files" "$@" 2> "$d/import.err"; then
		fail "$leg: $(basename "$lmp") does not import: $(tail -1 "$d/import.err")"; return 0
	fi
	if ! n=$(python3 "$tests/project-work.py" "$d/p.chimeraProject" "$d/m" "$wad" "$work/files" 2> "$d/work.err"); then
		fail "$leg: $(basename "$lmp")'s project: $(tail -1 "$d/work.err")"; return 0
	fi
	cp -r "$d/m" "$d/p"
	rm "$d/p/movie.txt"
	cp "$lmp" "$d/p/demo.lmp"
	echo "-playdemo demo.lmp" > "$d/p/gate-args"
	"$native" "$d/m" --frames "$n" --movie "$d/m/movie.txt" --trace "$d/tm" --trace-props "$props" > "$d/m.out" 2>&1 || true
	"$native" "$d/p" --frames "$n" --trace "$d/tp" --trace-props "$props" > "$d/p.out" 2>&1 || true
	maps="$(awk 'NR > 1 { print $5 }' "$d/tm" | uniq | tr '\n' ' ')"
	what="$(head -1 "$d/import.err" | sed 's/^[^:]*: //')"
	if [ "$(wc -l < "$d/tm")" -gt "$n" ] && cmp -s "$d/tm" "$d/tp"; then
		pass "$leg: $(basename "$lmp") - $what, map${maps% } - imported = the engine's own playback, tic for tic"
	else
		fail "$leg: $(basename "$lmp") - $what - imported != the engine's playback: $(diff "$d/tm" "$d/tp" | head -1)"
	fi
}

# record <dir> <version> <game> <frames> <settings JSON> [extra gate-args] [PWAD]:
# the engine records dir/rec.lmp from a movie that works every input
record() {
	d="$1"; version="$2"; game="$3"; frames="$4"; settings="$5"; extra="${6:-}"; pwad="${7:-}"
	id="$(awk -v v="$version" '$2 == v { print $4 }' "$work/pinned" | head -1)"
	mkwork "$d/r" "$work/files/$id" "$id" "$settings"
	if [ -n "$pwad" ]; then
		cp "$pwad" "$d/r/"
		printf '{"pwad": ["%s"]}' "$(basename "$pwad")" > "$d/r/slots"
	fi
	python3 "$tests/make-recording-movie.py" "$d/r/settings" "$game" "$frames" "$d/rec-movie.txt"
	echo "-record $d/rec $extra" > "$d/r/gate-args"
	"$native" "$d/r" --frames "$frames" --movie "$d/rec-movie.txt" > "$d/r.out" 2>&1 || true
	[ -f "$d/rec.lmp" ] || { fail "formats: $(basename "$d") recorded nothing: $(grep -m1 loadError "$d/r.out" || true)"; return 1; }
}

echo "== demos"
for p in 1 2; do
	for k in 1 2 3 4; do
		mkdir -p "$work/demo$p-$k"
		wad_lump "$freedoom/freedoom$p.wad" DEMO$k "$work/demo$p-$k/freedoom$p-DEMO$k.lmp"
		# an IWAD's own demos play from it alone (DEMO3's footer names the fix
		# Phase 2's MAP22 was recorded with, since in the IWAD)
		import_check "$work/demo$p-$k" "$work/demo$p-$k/freedoom$p-DEMO$k.lmp" "demos (freedoom$p)" --version freedoom$p-0.13.0 --no-pwads
	done
done

echo "== levels (the gate's own: three rooms, their exits, the intermissions)"
python3 "$tests/make-rooms.py" "$work/files/rooms.wad" "$work/rooms.lmp"
import_check "$work/levels" "$work/rooms.lmp" levels --version freedoom2-0.13.0 --pwad "$work/files/rooms.wad"
if [ "$(awk 'NR > 1 { print $5 }' "$work/levels/tm" | uniq | tr '\n' ' ')" = "1 2 3 4 " ]; then
	pass "levels: the demo exits MAP01, MAP02 and MAP03 and each intermission, to Freedoom's MAP04"
else
	fail "levels: the maps the demo went through: $(awk 'NR > 1 { print $5 }' "$work/levels/tm" | uniq | tr '\n' ' ')"
fi
# the melt: the same levels with renderWipescreen - its steps are lag, in both
# builds, and turbo across it
ml="$work/melt"
cp -r "$work/levels/m" "$ml"
python3 -c "import json, sys; s = json.load(open(sys.argv[1])); s['renderWipescreen'] = True; json.dump(s, open(sys.argv[1], 'w'))" "$ml/settings"
mn=$(grep -c '^|' "$ml/movie.txt")
"$native" "$ml" --frames "$mn" --movie "$ml/movie.txt" > "$ml.n" 2>/dev/null
"$wbx" "$core" "$ml" --frames "$mn" --movie "$ml/movie.txt" > "$ml.w" 2>/dev/null
"$wbx" "$core" "$ml" --frames "$mn" --movie "$ml/movie.txt" --turbo > "$ml.t" 2>/dev/null
lag=$(value "$ml.n" lagFrames)
if [ "${lag:-0}" -gt 0 ]; then
	compare xdigests melt "$ml.n" "$ml.w" "native = sandbox with the melt ($lag of $mn steps lag)"
else
	fail "melt: no step was lag"
fi
digests "$ml.t" | grep -v '^videoHash=' > "$ml.t.d"
digests "$ml.w" | grep -v '^videoHash=' > "$ml.w.d"
if cmp -s "$ml.t.d" "$ml.w.d"; then
	pass "melt: turbo across the level changes - the melts, the steps and memory the same"
else
	fail "melt: turbo changed the machine"; diff "$ml.t.d" "$ml.w.d" | head -5
fi

echo "== formats (recorded by the engine, crafted, imported)"
fmt="$work/formats"
st='"turningResolution": "8 bits (shorttics)"'
fdv=freedoom2-0.13.0
fcase() { # fcase <name> <settings JSON> [extra gate-args] [PWAD] [version game]
	n="$1"; set_="$2"; ex="${3:-}"; pw="${4:-}"; v="${5:-$fdv}"; g="${6:-freedoom2}"
	if record "$fmt/$n" "$v" "$g" 1000 "$set_" "$ex" "$pw"; then
		cp "$fmt/$n/rec.lmp" "$fmt/$n/$n.lmp"
		import_check "$fmt/$n/i" "$fmt/$n/$n.lmp" formats --version "$v"
	fi
}
for cl in 1 2 4 6 8 9 11 13 14 15 16; do
	fcase cl$cl "{\"version\": \"$fdv\", \"compatibilityLevel\": \"$cl\", $st}"
done
fcase cl3 "{\"version\": \"freedoom1-0.13.0\", \"compatibilityLevel\": \"3\", $st}" "" "" freedoom1-0.13.0 freedoom1
fcase cl2-longtics "{\"version\": \"$fdv\", \"compatibilityLevel\": \"2\"}"
fcase cl17 "{\"version\": \"$fdv\", \"compatibilityLevel\": \"17\"}"
fcase cl21 "{\"version\": \"$fdv\", \"compatibilityLevel\": \"21\"}"
fcase cl21-shorttics "{\"version\": \"$fdv\", \"compatibilityLevel\": \"21\", $st}"
fcase cl21-dsda "{\"version\": \"$fdv\", \"compatibilityLevel\": \"21\", \"extendedCommands\": \"On, with casual features\"}" -dsdademo
fcase cl9-dsda "{\"version\": \"$fdv\", \"compatibilityLevel\": \"9\", \"extendedCommands\": \"On, with casual features\", $st}" -dsdademo
fcase cl2-coop3 "{\"version\": \"$fdv\", \"compatibilityLevel\": \"2\", \"player2Present\": true, \"player3Present\": true, $st}"
fcase cl9-dm4 "{\"version\": \"$fdv\", \"compatibilityLevel\": \"9\", \"multiplayerMode\": \"Deathmatch\", \"player2Present\": true, \"player3Present\": true, \"player4Present\": true, \"displayPlayer\": 2, $st}"
fcase cl21-altdm "{\"version\": \"$fdv\", \"compatibilityLevel\": \"21\", \"multiplayerMode\": \"Alternate Deathmatch (v2.0)\", \"player1Present\": false, \"player2Present\": true, \"player4Present\": true}"
fcase cl11-flags "{\"version\": \"$fdv\", \"compatibilityLevel\": \"11\", \"fastMonsters\": true, \"monstersRespawn\": true, \"skillLevel\": \"2\", \"initialMap\": 5, \"rngSeed\": -123456789, $st}"
fcase cl3-nomonsters "{\"version\": \"freedoom1-0.13.0\", \"compatibilityLevel\": \"3\", \"noMonsters\": true, \"initialEpisode\": 2, \"initialMap\": 3, \"skillLevel\": \"5\", $st}" "" "" freedoom1-0.13.0 freedoom1
fcase rooms-cl9 "{\"version\": \"$fdv\", \"compatibilityLevel\": \"9\", $st}" "" "$work/files/rooms.wad"
fcase rooms-cl21-solonet "{\"version\": \"$fdv\", \"compatibilityLevel\": \"21\", \"soloNet\": true, \"coopSpawns\": true}" "" "$work/files/rooms.wad"
if grep -q '"soloNet": true' "$fmt/rooms-cl21-solonet/i/p.chimeraProject" && grep -q '"coopSpawns": true' "$fmt/rooms-cl21-solonet/i/p.chimeraProject"; then
	pass "formats: the footer's -solo-net and -coop_spawns are the imported project's soloNet and coopSpawns"
else
	fail "formats: the footer's -solo-net and -coop_spawns did not reach the project"
fi
for kc in doom12:cl2 doom14:cl1 doom15:cl1 lxdoom:cl9 boom200:cl9 options:cl9 options:cl11 options:cl17 options:cl21 footer:cl2 footer:cl9 markers:cl2 markers:cl9 umapinfo:cl9 umapinfo:cl21; do
	k=${kc%%:*}; src=${kc#*:}
	[ -f "$fmt/$src/rec.lmp" ] || continue
	mkdir -p "$fmt/craft-$k-$src"
	python3 "$tests/craft-demo.py" "$k" "$fmt/$src/rec.lmp" "$fmt/craft-$k-$src/$k-$src.lmp"
	import_check "$fmt/craft-$k-$src/i" "$fmt/craft-$k-$src/$k-$src.lmp" formats --version $fdv
done
# teeth: the MBF demo whose options were changed, imported without them, is not the demo
t="$fmt/craft-options-cl11/i"
if [ -f "$t/p.chimeraProject" ]; then
	cp -r "$t/m" "$t/teeth"
	python3 -c "import json, sys; s = json.load(open(sys.argv[1])); s['demoOptions'] = ''; json.dump(s, open(sys.argv[1], 'w'))" "$t/teeth/settings"
	"$native" "$t/teeth" --frames "$(grep -c '^|' "$t/teeth/movie.txt")" --movie "$t/teeth/movie.txt" --trace "$t/tt" --trace-props "$props" > /dev/null 2>&1 || true
	if cmp -s "$t/tt" "$t/tp"; then fail "formats: the MBF demo's changed options make no difference"; else pass "formats: without its changed option block, the MBF demo's project is not the demo (teeth)"; fi
fi

echo "== imports (what the importer refuses)"
ref="$work/refused"
mkdir -p "$ref"
refuse_import() { # refuse_import <craft> <source> <expected> <what> [importer arguments]
	k="$1"; src="$2"; want="$3"; what="$4"; shift 4
	[ -f "$fmt/$src/rec.lmp" ] || { fail "imports: $what - no $src recording"; return 0; }
	python3 "$tests/craft-demo.py" "$k" "$fmt/$src/rec.lmp" "$ref/$k.lmp"
	if out="$(python3 "$importer" "$ref/$k.lmp" --info "$@" 2>&1)"; then
		fail "imports: $what - imported"
	elif echo "$out" | grep -q -- "$want"; then
		pass "imports: $what - $(echo "$out" | head -1 | sed 's/^[^:]*: //')"
	else
		fail "imports: $what - $(echo "$out" | tail -1)"
	fi
}
refuse_import players5 cl9 "the core has four" "a fifth player" --version $fdv
refuse_import keyframe cl21-dsda "starts from a key frame" "a start from a key frame" --version $fdv
refuse_import excmdsave cl21-dsda "saves or loads a game" "a game saved mid-demo" --version $fdv
refuse_import badversion cl2 "no demo format dsda-doom knows" "a version no format has" --version $fdv
refuse_import doom12 cl2 "does not say which IWAD" "a versionless demo without its game"

echo "== equivalence (Freedoom's DEMO4, three players)"
eq="$work/demo1-4/m"
movie="$eq/movie.txt"
frames=$(grep -c '^|' "$movie")
"$native" "$eq" --frames "$frames" --movie "$movie" > "$work/n.txt" 2>/dev/null
"$wbx" "$core" "$eq" --frames "$frames" --movie "$movie" > "$work/w.txt" 2> "$work/w.err"
compare xdigests equivalence "$work/n.txt" "$work/w.txt" "native = sandbox, $frames steps (clock $(value "$work/n.txt" clock) ms, lag $(value "$work/n.txt" lagFrames))"
"$wbx" "$core" "$eq" --frames "$frames" --movie "$movie" --rerecord > "$work/r.txt" 2> "$work/r.err"
compare digests rerecord "$work/r.txt" "$work/w.txt" "save+load before every step = straight ($(sed -n 's/stateBytes=//p' "$work/r.err") state bytes)"
at=$((frames * 3 / 5))
"$wbx" "$core" "$eq" --frames "$frames" --movie "$movie" --session-at "$at" > "$work/s.txt" 2> "$work/s.err"
compare digests session "$work/s.txt" "$work/w.txt" "saved at step $at, loaded into a new host = straight"
# in the sandbox, whose addresses are fixed: every domain, the raw records too
"$wbx" "$core" "$eq" --frames "$frames" --movie "$movie" --turbo > "$work/t.txt" 2>/dev/null
digests "$work/t.txt" | grep -v '^videoHash=' > "$work/t.d"
digests "$work/w.txt" | grep -v '^videoHash=' > "$work/n.d"
if cmp -s "$work/t.d" "$work/n.d" && [ "$(value "$work/t.txt" videoHash)" != "$(value "$work/w.txt" videoHash)" ]; then
	pass "turbo: the first half's pictures not delivered - the second half's pictures, the sound, the steps and memory the same"
else
	fail "turbo"; diff "$work/t.d" "$work/n.d" | head
fi

echo "== settings"
mkwork "$work/set" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "skillLevel": "1", "compatibilityLevel": "9", "initialMap": 7, "noMonsters": true}'
mkwork "$work/set0" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0"}'
for build in native wbx; do
	if [ $build = native ]; then run="$native"; else run="$wbx $core"; fi
	sp="Game.Skill,Game.Compatibility Level,Game.Map,Level.Total Kills"
	$run "$work/set0" --frames 2 --trace "$work/set0.$build" --trace-props "$sp" > /dev/null 2>&1
	$run "$work/set" --frames 2 --trace "$work/set.$build" --trace-props "$sp" > /dev/null 2>&1
	a="$(last "$work/set0.$build" 1) $(last "$work/set0.$build" 2) $(last "$work/set0.$build" 3)"
	b="$(last "$work/set.$build" 1) $(last "$work/set.$build" 2) $(last "$work/set.$build" 3) $(last "$work/set.$build" 4)"
	k0="$(last "$work/set0.$build" 4)"
	if [ "$a" = "3 2 1" ] && [ "$b" = "0 9 7 0" ] && [ "$k0" -gt 0 ]; then
		pass "settings ($build): defaults are Ultra-Violence (3), complevel 2, MAP01 with $k0 monsters; skill 1, complevel 9, map 7, noMonsters give 0 9 7 and none"
	else
		fail "settings ($build): defaults $a (want 3 2 1), set $b (want 0 9 7 0)"
	fi
done
mkwork "$work/scale" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "scaleFactor": 2}'
"$native" "$work/scale" --frames 2 --screenshot "1:$work/scale.tga" > /dev/null 2>&1 || true
dims="$(python3 -c "import struct, sys; d = open(sys.argv[1], 'rb').read(18); print('%dx%d' % struct.unpack('<HH', d[12:16]))" "$work/scale.tga" 2>/dev/null || echo none)"
if [ "$dims" = 640x400 ]; then pass "settings: scaleFactor 2 draws 640x400"; else fail "settings: scaleFactor 2 draws $dims"; fi
# the pwad slot: a WAD with a DEHACKED lump, and a patch
python3 - "$work" <<'PY'
import struct, sys
w = sys.argv[1]
def deh(health):
    return ("Patch File for DeHackEd v3.0\nDoom version = 21\nPatch format = 6\n\nMisc 0\nInitial Health = %d\n" % health).encode()
lump = deh(42)
open(w + "/health42.wad", "wb").write(b"PWAD" + struct.pack("<ii", 1, 12 + len(lump)) + lump + struct.pack("<ii8s", 12, len(lump), b"DEHACKED"))
open(w + "/health37.deh", "wb").write(deh(37))
PY
for c in wad:health42.wad:42 deh:health37.deh:37; do
	kind=${c%%:*}; rest=${c#*:}; file=${rest%%:*}; want=${rest#*:}
	d="$work/slot-$kind"
	mkwork "$d" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0"}'
	cp "$work/$file" "$d/"
	printf '{"pwad": ["%s"]}' "$file" > "$d/slots"
	"$native" "$d" --frames 2 --trace "$d/tn" --trace-props P1.Health > /dev/null 2>&1
	"$wbx" "$core" "$d" --frames 2 --trace "$d/tw" --trace-props P1.Health > /dev/null 2>&1
	if [ "$(last "$d/tn" 1)" = "$want" ] && [ "$(last "$d/tw" 1)" = "$want" ]; then
		pass "settings: the pwad slot's $file gives the player $want health (both builds)"
	else
		fail "settings: the pwad slot's $file: health $(last "$d/tn" 1) native, $(last "$d/tw" 1) sandbox (want $want)"
	fi
done

echo "== declaration"
if out="$(python3 "$here/gen-declaration.py" --check 2>&1)"; then pass "declaration: $out"; else fail "declaration: $out"; fi

echo "== refusals"
refuse() { # refuse <dir> <expected text> <what>
	if out="$("$native" "$1" --frames 1 2>/dev/null)"; then
		fail "refusals: $3 - Init succeeded"
	elif echo "$out" | grep -q -- "$2"; then
		pass "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p')"
	else
		fail "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p')"
	fi
}
mkdir -p "$work/no-iwad"
cp "$wad" "$work/no-iwad/"
printf '{"version": "freedoom2-0.13.0"}' > "$work/no-iwad/settings"
refuse "$work/no-iwad" "needs its IWAD, freedoom2.wad (firmware), which is not there" "no IWAD"
mkwork "$work/other-game" "$fd2" freedoom2.wad '{"game": "doom2", "version": "freedoom2-0.13.0"}'
refuse "$work/other-game" "is freedoom2's, not the game doom2's" "a version of another game"
mkwork "$work/no-version" "$fd2" freedoom2.wad '{"version": "doom3"}'
refuse "$work/no-version" "none of the IWADs the core knows" "a version the core does not have"
mkwork "$work/no-player" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "player1Present": false}'
refuse "$work/no-player" "no player is present" "no player"
mkwork "$work/scale13" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "scaleFactor": 13}'
refuse "$work/scale13" "it goes from 1 to 12" "scaleFactor 13"
mkwork "$work/opt-hex" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "compatibilityLevel": "9", "demoOptions": "0g"}'
refuse "$work/opt-hex" "demoOptions is not hex" "an option block that is not hex"
mkwork "$work/opt-cl2" "$fd2" freedoom2.wad "{\"version\": \"freedoom2-0.13.0\", \"compatibilityLevel\": \"2\", \"demoOptions\": \"$(printf '%0128d' 0)\"}"
refuse "$work/opt-cl2" "complevel 2 has none" "an option block at complevel 2"
mkwork "$work/opt-size" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "compatibilityLevel": "11", "demoOptions": "0102"}'
refuse "$work/opt-size" "is 2 bytes; complevel 11's option block is 64" "an option block of the wrong size"

echo "== clock"
# the engine's time() and clock_gettime() are the core's (--wrap); musl's own
# clock_gettime stays for its __timedwait (pthread's waits), which nothing of
# the single-threaded engine reaches - it must have no other caller
cg="$(nm "$core" | awk '$3 == "__clock_gettime" { sub(/^0+/, "", $1); print $1 }')"
callers="$(objdump -d --no-show-raw-insn "$core" | awk -v a="$cg" '/^[0-9a-f]+ <.*>:$/ { fn = $2 } a != "" && index($0, a) && fn != "<__clock_gettime>:" { print fn }' | sort -u | tr -d '<>:' | tr '\n' ' ')"
if nm "$core" | grep -qE ' (T|W|t) (time|gettimeofday|__real_time|__real_clock_gettime)$'; then
	fail "clock: the guest links a time() or gettimeofday() of the C library's"
elif [ -n "$(echo $callers | tr ' ' '\n' | grep -vxE '__timedwait|__timedwait_cp' || true)" ]; then
	fail "clock: musl's clock_gettime is called by $callers"
else
	pass "clock: time() and clock_gettime() are only the core's (a step is 1/35 s); musl's clock_gettime is its __timedwait's only (${callers% })"
fi

echo "== teeth"
# step 500: player one runs at 127 instead (the first axis of the second group)
awk 'BEGIN { n = 0 } /^\|/ { if (n++ == 500) { k = split($0, g, "|"); sub(/^ *-?[0-9]+,/, "  127,", g[3]); out = "|"; for (i = 2; i < k; i++) out = out g[i] "|"; print out; next } } { print }' "$movie" > "$work/teeth.movie"
"$native" "$eq" --frames "$frames" --movie "$work/teeth.movie" > "$work/teeth.txt" 2>/dev/null
if cmp -s "$work/teeth.movie" "$movie"; then
	fail "teeth: the movie did not change"
elif xdigests "$work/teeth.txt" | cmp -s - "$work/n.txt.d"; then
	fail "teeth: a movie one step different digests the same"
else
	pass "teeth: a movie one step different digests differently"
fi

if [ -n "$chimera_run" ]; then
	echo "== engine ($chimera_run)"
	"$here/build-package.sh" -m "$mb" -o "$work/package" > "$work/package.log" 2>&1 || {
		tail -5 "$work/package.log"; fail "engine: the package did not build"; }
	ed="$work/engine"
	mkdir -p "$ed"
	python3 "$importer" "$work/demo2-1/freedoom2-DEMO1.lmp" --version freedoom2-0.13.0 --no-pwads --package "$work/package/dsda.chimeraCore" -o "$ed/p.chimeraProject" 2> "$ed/import.err"
	n=$(python3 "$tests/project-work.py" "$ed/p.chimeraProject" "$ed/m" "$wad" "$work/files")
	"$wbx" "$core" "$ed/m" --frames "$n" --movie "$ed/m/movie.txt" --dump-domain "Game State" "$ed/harness.gs" > /dev/null 2>&1
	( cd "$ed" && "$chimera_run" --project "$ed/p.chimeraProject" "$work/package/dsda.chimeraCore" --files "$work/files" --firmware "freedoom2.wad=$fd2" --dump "Game State=$ed/engine.gs" ) > "$ed/engine.out" 2>&1 || true
	( cd "$ed" && "$chimera_run" --project "$ed/p.chimeraProject" "$work/package/dsda.chimeraCore" --files "$work/files" --firmware "freedoom2.wad=$fd2" --rerecord --dump "Game State=$ed/engine-r.gs" ) > "$ed/engine-r.out" 2>&1 || true
	if [ -f "$ed/engine.gs" ] && cmp -s "$ed/engine.gs" "$ed/harness.gs" && cmp -s "$ed/engine.gs" "$ed/engine-r.gs"; then
		pass "engine: chimera-run --project plays Freedoom's DEMO1 ($n tics), imported and pinned to the package, to the harness's Game State, the same with --rerecord"
	else
		fail "engine: chimera-run --project"; tail -3 "$ed/engine.out"
	fi
fi

if [ -n "$iwads" ] && [ -f "$work/iwads" ]; then
	echo "== iwads ($iwads)"
	while read -r version game id; do
		f="$work/files/$id"
		lumps="$(python3 - "$f" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
_, n, o = struct.unpack('<4sii', d[:12])
for i in range(n):
    p, s, name = struct.unpack('<ii8s', d[o + 16 * i:o + 16 * i + 16])
    name = name.rstrip(b'\0').decode('latin1')
    if name.startswith('DEMO') and s > 7:
        print(name)
PY
)"
		first=""
		for lump in $lumps; do
			d="$work/iwad-$version-$lump"
			mkdir -p "$d"
			wad_lump "$f" "$lump" "$d/$version-$lump.lmp"
			import_check "$d" "$d/$version-$lump.lmp" "iwads ($version)" --version "$version" --no-pwads
			[ -n "$first" ] || first="$d"
		done
		# the Raven games' demos, recorded: the header's flags, longtics, the
		# classes, co-op, dsda's format
		case $game in
			heretic)
				fcase heretic "{\"version\": \"$version\", $st}" "" "" "$version" heretic
				fcase heretic-flags "{\"version\": \"$version\", \"monstersRespawn\": true, \"skillLevel\": \"2\", \"initialEpisode\": 2, \"initialMap\": 4}" "" "" "$version" heretic
				fcase heretic-coop "{\"version\": \"$version\", \"noMonsters\": true, \"player2Present\": true, \"player3Present\": true, $st}" "" "" "$version" heretic
				fcase heretic-dsda "{\"version\": \"$version\", \"extendedCommands\": \"On, with casual features\"}" -dsdademo "" "$version" heretic ;;
			hexen)
				fcase hexen-cleric "{\"version\": \"$version\", \"player1Class\": \"Cleric\", $st}" "" "" "$version" hexen
				fcase hexen-coop "{\"version\": \"$version\", \"player1Class\": \"Mage\", \"player2Present\": true, \"player4Present\": true, \"player4Class\": \"Cleric\", \"initialMap\": 13}" "" "" "$version" hexen
				fcase hexen-dsda "{\"version\": \"$version\", \"extendedCommands\": \"On, with casual features\", \"player1Class\": \"Mage\"}" -dsdademo "" "$version" hexen ;;
		esac
		[ -n "$first" ] || continue
		dd="$first/m"
		[ -d "$dd" ] || continue
		n=$(grep -c '^|' "$dd/movie.txt")
		"$native" "$dd" --frames "$n" --movie "$dd/movie.txt" > "$first.n" 2>/dev/null
		"$wbx" "$core" "$dd" --frames "$n" --movie "$dd/movie.txt" > "$first.w" 2>/dev/null
		"$wbx" "$core" "$dd" --frames "$n" --movie "$dd/movie.txt" --session-at $((n * 3 / 5)) > "$first.s" 2>/dev/null
		compare xdigests "iwads ($version)" "$first.n" "$first.w" "native = sandbox, $n steps of $(basename "$first" | sed 's/.*-//')"
		compare digests "iwads ($version)" "$first.s" "$first.w" "session at step $((n * 3 / 5)) = straight"
	done < "$work/iwads"
fi

echo
if [ $fails -eq 0 ]; then
	echo "gate: all legs passed"
else
	echo "gate: $fails leg(s) failed"
	exit 1
fi
