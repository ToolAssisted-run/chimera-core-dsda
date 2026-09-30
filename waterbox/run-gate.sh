#!/bin/sh
# run-gate.sh - the DSDA core's gate: the native reference and core.wbx are
# the same machine, and a movie of a demo's inputs is the demo. It runs on
# Freedoom 0.13.0 (free: the gate fetches it, or -f names it) and, when -i names
# a folder of them, on the IWADs the declaration pins.
#
# Every leg says what it compared. The legs:
#   equivalence  run-native and run-wbx on Freedoom's DEMO4 (Phase 1's E4M6,
#                three players) as a movie: every step's picture, sound and lag,
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
#   demos        each of Freedoom's eight demos (DEMO1-4 of both phases: 1.9
#                demos of one to three players) converted to a movie
#                (tests/lmp2sol.py) = the engine playing the demo itself
#                (-playdemo, through the native reference's gate-args): every
#                tic's map, level time, game RNG, kills, and each player's
#                position, angle and health
#   levels       the gate's own three rooms (tests/make-rooms.py: a PWAD of one-room
#                maps, an exit switch in each) and a demo through them - the
#                exits and intermissions, as a movie = -playdemo; with the melt
#                (renderWipescreen), native = sandbox and turbo across it
#   settings     the skill, complevel, map and noMonsters where the engine keeps
#                them (both builds); scaleFactor 2's picture is 640x400; the
#                pwad slot's WAD (a DEHACKED lump) and patch (.deh) each change
#                the player's initial health
#   declaration  gen-declaration.py writes what the repo holds (waterbox.config,
#                file_slots.json, default_keybinds.json, dsda-versions.h)
#   refusals     no IWAD, a version that is another game's, a version the core
#                does not have, no player, scaleFactor 13: each says why
#   clock        the guest has no clock of its own: time() and clock_gettime()
#                are the core's
#   teeth        the equivalence comparison sees a one-step difference
#   engine       (with -c) the package through Chimera's own engine, headless:
#                chimera-run plays Freedoom's DEMO1 (Phase 2's MAP03) as a movie
#                in Chimera's format to the harness's Game State, and the same
#                with --rerecord
#   iwads        (with -i) each IWAD in the folder whose SHA1 the declaration
#                pins: its own demos, when they are vanilla's (Doom, Doom II,
#                Final Doom, Chex Quest), = -playdemo, and native = sandbox and
#                session on the first; Heretic and Hexen (whose demos are not
#                vanilla's) a scripted walk, native = sandbox and session
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
lmp2sol="$here/tests/lmp2sol.py"

work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"
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

# a work folder: <dir> <IWAD file> <firmware id> [settings JSON]
mkwork() {
	mkdir -p "$1"
	ln -sf "$(cd "$(dirname "$2")" && pwd)/$(basename "$2")" "$1/$3"
	cp "$wad" "$1/dsda-doom.wad"
	[ -z "${4:-}" ] || printf '%s' "$4" > "$1/settings"
}
# run-wbx mounts the work folder's regular files: a link is copied in
wbx_ready() { for f in "$1"/*.wad; do [ -L "$f" ] && cp --remove-destination "$(readlink -f "$f")" "$f"; done; true; }

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
	else
		fail "freedoom: $freedoom/freedoom$p.wad ($s) is not 0.13.0's"; exit 1
	fi
done

# a vanilla demo as a movie and as the engine's own: <dir> <IWAD> <firmware id>
# <version> <game> <the IWAD's LUMP, or an .lmp> <leg> [a PWAD, in the slot]
demo_pair() {
	d="$1"
	mkdir -p "$d"
	case "$6" in
		*.lmp) cp "$6" "$d/demo.lmp" ;;
		*) python3 "$lmp2sol" --extract "$2" "$6" "$d/demo.lmp" ;;
	esac
	n=$(python3 "$lmp2sol" "$d/demo.lmp" "$d/movie.sol" "$d/settings.json" "$4" "$5")
	mkwork "$d/m" "$2" "$3" "$(cat "$d/settings.json")"
	mkwork "$d/p" "$2" "$3" "$(cat "$d/settings.json")"
	cp "$d/demo.lmp" "$d/p/"
	if [ -n "${8:-}" ]; then
		for x in m p; do cp "$8" "$d/$x/"; printf '{"pwad": ["%s"]}' "$(basename "$8")" > "$d/$x/slots"; done
	fi
	echo "-playdemo demo.lmp" > "$d/p/gate-args"
	props="Game.Tic,Game.Map,Game.Level Time,Game.State,RNG.Index,Level.Total Kills"
	for i in 1 2 3 4; do props="$props,P$i.X,P$i.Y,P$i.Z,P$i.Angle,P$i.Health"; done
	"$native" "$d/m" --frames "$n" --movie "$d/movie.sol" --trace "$d/tm" --trace-props "$props" > "$d/m.out" 2>&1
	"$native" "$d/p" --frames "$n" --trace "$d/tp" --trace-props "$props" > "$d/p.out" 2>&1
	players=$(python3 -c "import sys; print(sum(open(sys.argv[1], 'rb').read()[9:13]))" "$d/demo.lmp")
	maps="$(awk 'NR > 1 { print $5 }' "$d/tm" | uniq | tr '\n' ' ')"
	ps=s; [ "$players" != 1 ] || ps=""
	ms=""; case "${maps% }" in *" "*) ms=s ;; esac
	what="$(basename "$6") ($n tics, $players player$ps, map$ms ${maps% })"
	if [ "$(wc -l < "$d/tm")" -gt "$n" ] && cmp -s "$d/tm" "$d/tp"; then
		pass "$7: $what as a movie = the engine's own playback, tic for tic"
	else
		fail "$7: $what as a movie != the engine's playback: $(diff "$d/tm" "$d/tp" | head -1)"
	fi
}

echo "== demos"
for p in 1 2; do
	for k in 1 2 3 4; do
		demo_pair "$work/demo$p-$k" "$freedoom/freedoom$p.wad" "freedoom$p.wad" "freedoom$p-0.13.0" "freedoom$p" "DEMO$k" "demos (freedoom$p)"
	done
done

echo "== levels (the gate's own: three rooms, their exits, the intermissions)"
python3 "$here/tests/make-rooms.py" "$work/rooms.wad" "$work/rooms.lmp"
demo_pair "$work/levels" "$freedoom/freedoom2.wad" freedoom2.wad freedoom2-0.13.0 freedoom2 "$work/rooms.lmp" levels "$work/rooms.wad"
if [ "$(awk 'NR > 1 { print $5 }' "$work/levels/tm" | uniq | tr '\n' ' ')" = "1 2 3 4 " ]; then
	pass "levels: the demo exits MAP01, MAP02 and MAP03 and each intermission, to Freedoom's MAP04"
else
	fail "levels: the maps the demo went through: $(awk 'NR > 1 { print $5 }' "$work/levels/tm" | uniq | tr '\n' ' ')"
fi
# the melt: the same levels with renderWipescreen - its steps are lag, in both
# builds, and turbo across it
ml="$work/melt"
mkwork "$ml" "$freedoom/freedoom2.wad" freedoom2.wad "$(python3 -c "import json, sys; s = json.load(open(sys.argv[1])); s['renderWipescreen'] = True; print(json.dumps(s))" "$work/levels/settings.json")"
cp "$work/rooms.wad" "$ml/"
printf '{"pwad": ["rooms.wad"]}' > "$ml/slots"
wbx_ready "$ml"
mn=$(grep -vc '^#' "$work/levels/movie.sol")
"$native" "$ml" --frames "$mn" --movie "$work/levels/movie.sol" > "$ml.n" 2>/dev/null
"$wbx" "$core" "$ml" --frames "$mn" --movie "$work/levels/movie.sol" > "$ml.w" 2>/dev/null
"$wbx" "$core" "$ml" --frames "$mn" --movie "$work/levels/movie.sol" --turbo > "$ml.t" 2>/dev/null
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

echo "== equivalence (Freedoom's DEMO4, three players)"
eq="$work/demo1-4/m"
wbx_ready "$eq"
movie="$work/demo1-4/movie.sol"
frames=$(grep -vc '^#' "$movie")
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
fd2="$freedoom/freedoom2.wad"
mkwork "$work/set" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0", "skillLevel": "1", "compatibilityLevel": "9", "initialMap": 7, "noMonsters": true}'
wbx_ready "$work/set"
mkwork "$work/set0" "$fd2" freedoom2.wad '{"version": "freedoom2-0.13.0"}'
wbx_ready "$work/set0"
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
	wbx_ready "$d"
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
awk '!/^#/ { n++ } n == 501 && !/^#/ { print "||  50,   0,   0,   0,...||   0,   0,   0,   0,...||   0,   0,   0,   0,...|"; next } { print }' "$movie" > "$work/teeth.sol"
"$native" "$eq" --frames "$frames" --movie "$work/teeth.sol" > "$work/teeth.txt" 2>/dev/null
if xdigests "$work/teeth.txt" | cmp -s - "$work/n.txt.d"; then
	fail "teeth: a movie one step different digests the same"
else
	pass "teeth: a movie one step different digests differently"
fi

if [ -n "$chimera_run" ]; then
	echo "== engine ($chimera_run)"
	"$here/build-package.sh" -m "$mb" -o "$work/package" > "$work/package.log" 2>&1 || {
		tail -5 "$work/package.log"; fail "engine: the package did not build"; }
	ed="$work/demo2-1"
	python3 "$lmp2sol" --chimera "$ed/movie.sol" "$work/engine.txt"
	n=$(grep -vc '^#' "$ed/movie.sol")
	sj="$(python3 -c "import json, sys; s = json.load(open(sys.argv[1])); s['game'] = 'freedoom2'; print(json.dumps(s))" "$ed/settings.json")"
	wbx_ready "$ed/m"
	"$wbx" "$core" "$ed/m" --frames "$n" --movie "$ed/movie.sol" --dump-domain "Game State" "$work/engine-harness.gs" > /dev/null 2>&1
	# the shape of an entry, as the engine records one: the movie's must be it
	( cd "$work" && "$chimera_run" "$work/package/dsda.chimeraCore" "$fd2" "$work/engine.txt" --frames 1 --record "$work/engine-shape.txt" --settings "$sj" --firmware "freedoom2.wad=$fd2" ) > "$work/engine-shape.out" 2>&1 || true
	shape() { head -1 "$1" | sed 's/-\{0,1\}[0-9][0-9]*/0/g; s/[A-Za-z]/X/g; s/ //g'; }
	( cd "$work" && "$chimera_run" "$work/package/dsda.chimeraCore" "$fd2" "$work/engine.txt" --settings "$sj" --firmware "freedoom2.wad=$fd2" --dump "Game State=$work/engine.gs" ) > "$work/engine.out" 2>&1 || true
	( cd "$work" && "$chimera_run" "$work/package/dsda.chimeraCore" "$fd2" "$work/engine.txt" --rerecord --settings "$sj" --firmware "freedoom2.wad=$fd2" --dump "Game State=$work/engine-r.gs" ) > "$work/engine-r.out" 2>&1 || true
	if [ -f "$work/engine-shape.txt" ] && [ "$(shape "$work/engine-shape.txt" | tr '.' 'X')" = "$(shape "$work/engine.txt" | tr '.' 'X')" ] \
		&& [ -f "$work/engine.gs" ] && cmp -s "$work/engine.gs" "$work/engine-harness.gs" && cmp -s "$work/engine.gs" "$work/engine-r.gs"; then
		pass "engine: chimera-run plays Freedoom's DEMO1 ($n tics) as a Chimera movie to the harness's Game State, the same with --rerecord"
	else
		fail "engine: chimera-run"; tail -3 "$work/engine.out"
	fi
fi

if [ -n "$iwads" ]; then
	echo "== iwads ($iwads)"
	for f in "$iwads"/*; do
		[ -f "$f" ] || continue
		s="$(sha1_of "$f")"
		line="$(grep "^$s " "$work/pinned" | head -1 || true)"
		[ -n "$line" ] || continue
		set -- $line
		version=$2; game=$3; id=$4
		case $game in freedoom1|freedoom2) continue ;; esac
		lumps="$(python3 - "$f" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
_, n, o = struct.unpack('<4sii', d[:12])
for i in range(n):
    p, s, name = struct.unpack('<ii8s', d[o + 16 * i:o + 16 * i + 16])
    name = name.rstrip(b'\0').decode('latin1')
    if name.startswith('DEMO') and s > 13 and 104 <= d[p] <= 109:
        print(name)
PY
)"
		first=""
		if [ -n "$lumps" ]; then
			for lump in $lumps; do
				demo_pair "$work/iwad-$version-$lump" "$f" "$id" "$version" "$game" "$lump" "iwads ($version)"
				[ -n "$first" ] || first="$work/iwad-$version-$lump"
			done
			d="$first/m"; m="$first/movie.sol"
		else
			# a scripted walk: forward, a turn, fire
			d="$work/iwad-$version"
			mkwork "$d" "$f" "$id" "{\"version\": \"$version\"}"
			m="$d.sol"
			awk 'BEGIN { for (i = 0; i < 700; i++) printf "||  %d,   0, %4d,   0,%s..|\n", (i % 200 < 150 ? 50 : 0), (i % 200 >= 150 ? 8 : 0), (i % 7 == 0 ? "F" : ".") }' > "$m"
		fi
		wbx_ready "$d"
		n=$(grep -vc '^#' "$m")
		"$native" "$d" --frames "$n" --movie "$m" > "$d.n" 2>/dev/null
		"$wbx" "$core" "$d" --frames "$n" --movie "$m" > "$d.w" 2>/dev/null
		"$wbx" "$core" "$d" --frames "$n" --movie "$m" --session-at $((n * 3 / 5)) > "$d.s" 2>/dev/null
		compare xdigests "iwads ($version)" "$d.n" "$d.w" "native = sandbox, $n steps$([ -n "$lumps" ] && echo " of $(basename "$first" | sed 's/.*-//')" || echo " of a scripted walk")"
		compare digests "iwads ($version)" "$d.s" "$d.w" "session at step $((n * 3 / 5)) = straight"
	done
fi

echo
if [ $fails -eq 0 ]; then
	echo "gate: all legs passed"
else
	echo "gate: $fails leg(s) failed"
	exit 1
fi
