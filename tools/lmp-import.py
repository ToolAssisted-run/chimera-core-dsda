#!/usr/bin/env python3
"""lmp-import.py - a Doom-engine demo (.lmp) as a Chimera project for the DSDA
core: everything the demo dictates - the game and its release, the
compatibility level, the skill, the map, the players and their classes, the
monster flags, the turning resolution, a Boom-or-later demo's option block,
dsda's extended commands, its footer's arguments - as the project's settings,
its IWAD as the firmware, its PWADs and patches in the slot, and its tics as
the movie's input log, frame for tic.

Every format dsda-doom plays (G_ReadDemoHeaderEx, dsda/demo.c, dsda/exdemo.c):

  Doom 1.0-1.2      no version byte, a 7-byte header (complevel 0); the monster
                    flags are not in it - the footer's, or --respawn/--fast/
                    --nomonsters
  Doom 1.4-1.9      104-109, 13 bytes (complevel 1 below 1.7; from 1.7 2, 3 on a
                    game with a fourth episode, 4 on Final Doom, or the
                    footer's -complevel)
  TASDoom           110 (complevel 6; a tic's bytes in its own order)
  Doom longtics     111 (1.9 with 16-bit turning)
  Boom 2.00-2.02    200-202 (complevel 9, 8, or 7 with its compatibility flag)
  LxDoom, MBF       203 (10 or 11, by the signature)
  PrBoom            210-214 (13-17; 214 with 16-bit turning)
  MBF21             221 (21; 16-bit turning)
  DSDA              255 + "DSDA": dsda's own header before one of the above -
                    extended commands each tic (jump, free look, god, no clip,
                    saves and loads), casual features, a start from a key frame
  PrBoom+um         255 + "PR+UM": a UMAPINFO header before one of the above
  Heretic           no version byte, 7 bytes; respawn, longtics and no-monsters
                    as bits of player one's byte (vvHeretic's)
  Hexen             the same, and each of eight players' class

and the footer dsda and PrBoom+ append after the end marker (a WAD of
PORTNAME, FEATURES and CMDLINE: the IWAD, PWADs and patches by name,
-complevel, -solo-net, -coop_spawns, -chain_episodes, the 1.2 monster flags,
-emulate, -spechit and the overflow emulation's -set).

What a movie cannot be is refused, saying why: more than four players (the
core has four ports), a demo that starts from a key frame (a saved game), one
that saves and loads mid-demo (dsda's casual extended commands).

The movie has the screen melt off, as BizHawk's importers have it: a demo's tics
are the game's, and the melt's steps would be frames of their own.

usage: lmp-import.py <demo.lmp> [-o <out.chimeraProject>] [--version <id> | --iwad <file>]
                     [--wads <dir>]... [--pwad <file>]... [--package <dsda.chimeraCore> | --config <waterbox.config>]
                     [--respawn] [--fast] [--nomonsters] [--title <text>] [--info]

  --version, --iwad  the IWAD's release (a version id, or the file itself, found
                     by its hash); without them, the footer's -iwad and the
                     format decide when they can
  --wads             folders where the demo's PWADs and patches (by the
                     footer's names) and its IWAD are looked for
  --pwad             the PWADs and patches, in order, when the footer does not
                     name them (it wins over the footer)
  --no-pwads         none, whatever the footer names (an IWAD's own demos: a fix
                     their authors recorded with, now in the IWAD)
  --longtics         a Heretic or Hexen demo recorded with -longtics whose header
                     does not say so (Hexen+'s, before vvHeretic's flag)
  --package          the core package, for its declaration and identity (its
                     name, version and hash pin the project); --config a
                     waterbox.config instead (the repo's by default)
  --info             prints what the demo dictates and writes nothing
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CONFIG = os.path.join(HERE, "..", "waterbox", "waterbox.config")

DEMOMARKER = 0x80
BT_ATTACK, BT_USE, BT_CHANGE, BT_SPECIAL = 1, 2, 4, 128
BT_SPECIALMASK, BT_PAUSE = 3, 1
XC_JUMP, XC_SAVE, XC_LOAD, XC_GOD, XC_NOCLIP, XC_LOOK = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
AFLAG_JUMP, AFLAG_SUICIDE, AFLAG_MASK = 0x80, 0x40, 0x3F
DEMOHEADER_RESPAWN, DEMOHEADER_LONGTICS, DEMOHEADER_NOMONSTERS = 0x20, 0x10, 0x02
DF_FROM_KEYFRAME, DF_CASUAL_FEATURES = 0x01, 0x02
MBF_GAME_OPTION_SIZE = 64
# the games whose 1.9 demos play at complevel 3 (a fourth episode: retail) and 4
RETAIL_GAMES = ("doom", "freedoom1")
FINAL_GAMES = ("tnt", "plutonia")
# a header's option block: where its monster flags and its seed are
OPTION_FIELDS = {"mbf": (6, 7, 8, 10), "mbf21": (3, 4, 5, 6)}
OVERRUNS = [("overrun_spechit_emulate", "overrunSpechit"), ("overrun_reject_emulate", "overrunReject"),
            ("overrun_intercept_emulate", "overrunIntercept"), ("overrun_playeringame_emulate", "overrunPlayeringame"),
            ("overrun_donut_emulate", "overrunDonut"), ("overrun_missedbackside_emulate", "overrunMissedBackside")]
CLASSES = ["Fighter", "Cleric", "Mage"]


class DemoError(Exception):
    """a demo that is not one, or one a movie cannot be"""


def s8(b):
    return b - 256 if b >= 128 else b


# ------------------------------------------------------------------ the demo

class Demo:
    """what a demo file holds: its header's fields, its tics, its footer"""

    def __init__(self):
        self.format = ""          # a name, for the reader
        self.version = None       # the version byte (None: a versionless header)
        self.family = "doom"      # doom, heretic or hexen: the tic's layout
        self.complevel = None     # None: the 1.4-1.9 rule decides with the game
        self.longtics = False
        self.tasdoom = False
        self.skill = 0
        self.episode = 1
        self.map = 1
        self.deathmatch = 0
        self.respawn = self.fast = self.nomonsters = None   # None: not in the header
        self.consoleplayer = 0
        self.players = [False] * 4
        self.classes = [0] * 4
        self.options = None       # a Boom-or-later header's option block, as it is
        self.options_layout = None
        self.rngseed = None
        self.excmd = False
        self.casual = False
        self.from_keyframe = False
        self.umapinfo = False
        self.tics = []            # each tic: one dict per player present
        self.footer = None        # the footer's lumps, by name
        self.footer_args = []
        self.footer_text = ""
        self.raven_bits = 0
        self.truncated = False    # no end marker: the tics ran to the file's end


def _need(data, at, n, what):
    if at + n > len(data):
        raise DemoError("the demo ends inside its %s" % what)


def parse_demo(data, family_hint=None, longtics=False):
    """the demo in data; family_hint ('doom', 'heretic', 'hexen') is the game's,
    which a versionless header needs (1.2, Heretic and Hexen all start with the
    skill)"""
    d = Demo()
    p = 0
    _need(data, p, 1, "header")
    ver = data[p]
    p += 1

    if ver == 255:
        # dsda's header, or PrBoom+um's UMAPINFO one, before the real one
        _need(data, p, 7, "extended header")
        if data[p] == 0x1D and data[p + 1:p + 5] == b"DSDA" and data[p + 5] == 0xE6:
            dsda_version = data[p + 6]
            p += 7
            if dsda_version > 3:
                raise DemoError("its DSDA header is version %d, newer than dsda-doom 0.30's (3)" % dsda_version)
            size = {1: 8, 2: 9, 3: 10}.get(dsda_version, 0)
            _need(data, p, size, "DSDA header")
            flags = data[p + 8] if dsda_version >= 2 else 0
            p += size
            d.excmd = True
            d.casual = bool(flags & DF_CASUAL_FEATURES)
            d.from_keyframe = bool(flags & DF_FROM_KEYFRAME)
            d.format = "DSDA (%d), " % dsda_version
        elif data[p:p + 5] == b"PR+UM":
            p += 6
            _need(data, p, 1 + 2 + 1 + 8 + 8, "UMAPINFO header")
            if data[p] != 1 or data[p + 1] != 1 or data[p + 2] != 0 or data[p + 3] != 8 or data[p + 4:p + 12] != b"UMAPINFO":
                raise DemoError("its PrBoom+um extended header is not the one dsda-doom knows")
            p += 4 + 8 + 8
            d.umapinfo = True
            d.format = "PrBoom+um UMAPINFO, "
        else:
            raise DemoError("its extended header (version byte 255) is neither dsda's nor PrBoom+um's")
        _need(data, p, 1, "header")
        ver = data[p]
        p += 1

    if not (0 <= ver <= 4 or 104 <= ver <= 111 or 200 <= ver <= 214 or ver == 221):
        raise DemoError("its version byte %d is no demo format dsda-doom knows" % ver)

    if ver < 200 and ver >= 100:
        # Doom 1.4 - 1.9, TASDoom, longtics
        d.version = ver
        d.longtics = ver >= 111
        d.tasdoom = ver == 110
        _need(data, p, 12, "header")
        d.skill, d.episode, d.map, d.deathmatch, r, f, n, d.consoleplayer = data[p:p + 8]
        d.respawn, d.fast, d.nomonsters = bool(r), bool(f), bool(n)
        p += 8
        d.players = [bool(x) for x in data[p:p + 4]]
        p += 4
        d.format += {110: "TASDoom", 111: "Doom 1.9 longtics"}.get(ver, "Doom 1.%d" % (ver - 100))
    elif ver < 100:
        # versionless: Doom 1.0-1.2, Heretic, Hexen
        family = family_hint or guess_versionless_family(data, p - 1)
        d.family = family
        d.skill = ver
        _need(data, p, 2, "header")
        d.episode, d.map = data[p], data[p + 1]
        p += 2
        if family == "hexen":
            _need(data, p, 16, "header")
            raw = [(data[p + 2 * i], data[p + 2 * i + 1]) for i in range(8)]
            p += 16
            ingame = [b != 0 for b, _ in raw]
            classes = [c for _, c in raw]
            bits = raw[0][0]
        else:
            _need(data, p, 4, "header")
            ingame = [b != 0 for b in data[p:p + 4]]
            classes = [0] * 4
            bits = data[p]
            p += 4
        if any(ingame[4:]):
            raise DemoError("players %s are in the game; the core has four" %
                            ", ".join(str(i + 1) for i in range(4, len(ingame)) if ingame[i]))
        d.players = ingame[:4]
        d.classes = classes[:4]
        if family == "doom":
            d.complevel = 0
            d.format += "Doom 1.2 (or earlier)"
        else:
            # vvHeretic's special bits on player one's byte, OR'd with the
            # footer's flags (the monster flags are not in the header otherwise)
            d.raven_bits = bits
            d.longtics = bool(bits & DEMOHEADER_LONGTICS) or longtics
            d.format += family.capitalize()
    else:
        # Boom and later: a signature, then the header, then the options
        d.version = ver
        sig_at = p
        _need(data, p, 6, "signature")
        p += 6
        if ver in (200, 201, 202):
            _need(data, p, 1, "header")
            compat = data[p]
            p += 1
            d.complevel = 7 if compat else {200: 8, 201: 8, 202: 9}[ver]
            d.format += "Boom 2.0%d" % (ver - 200)
        elif ver == 203:
            if data[sig_at + 1] == ord("B"):
                d.complevel = 10
                d.format += "LxDoom"
            elif data[sig_at + 1] == ord("M"):
                d.complevel = 11
                p += 1
                d.format += "MBF"
            else:
                raise DemoError("its version 203 signature is neither LxDoom's nor MBF's")
        elif 210 <= ver <= 214:
            d.complevel = {210: 13, 211: 14, 212: 15, 213: 16, 214: 17}[ver]
            d.longtics = ver == 214
            p += 1
            d.format += "PrBoom (complevel %d)" % d.complevel
        else:  # 221
            d.complevel = 21
            d.longtics = True
            d.format += "MBF21"
        _need(data, p, 5, "header")
        d.skill, d.episode, d.map, d.deathmatch, d.consoleplayer = data[p:p + 5]
        p += 5
        if d.complevel == 21:
            _need(data, p, 21, "options")
            count = data[p + 20]
            if count > 25:
                raise DemoError("its MBF21 options name %d comp flags; dsda-doom 0.30 knows 25" % count)
            size = 21 + count
            d.options_layout = "mbf21"
        else:
            size = MBF_GAME_OPTION_SIZE
            d.options_layout = "mbf"
        _need(data, p, size, "options")
        d.options = bytes(data[p:p + size])
        r_at, f_at, n_at, seed_at = OPTION_FIELDS[d.options_layout]
        d.respawn, d.fast, d.nomonsters = bool(d.options[r_at]), bool(d.options[f_at]), bool(d.options[n_at])
        d.rngseed = struct.unpack(">I", d.options[seed_at:seed_at + 4])[0]
        p += size
        if ver == 200:
            # killough: 2.00 kept 256 bytes of options
            p += 256 - MBF_GAME_OPTION_SIZE
        _need(data, p, 32, "players")
        ingame = [bool(x) for x in data[p:p + 32]]
        p += 32
        if any(ingame[4:]):
            raise DemoError("players %s are in the game; the core has four" %
                            ", ".join(str(i + 1) for i in range(4, 32) if ingame[i]))
        d.players = ingame[:4]

    if not any(d.players):
        raise DemoError("no player is in the game")
    if d.from_keyframe:
        raise DemoError("it starts from a key frame (a saved game inside the demo); a movie starts with the game")

    # the tics, to the end marker - or, as dsda's playback ends too
    # (dsda_EndOfPlaybackStream), where a whole tic no longer fits
    present = [i for i in range(4) if d.players[i]]
    base = (4 if d.tasdoom else 5 if d.longtics else 4) + (2 if d.family != "doom" else 0) + (1 if d.excmd else 0)
    while True:
        if p >= len(data) or data[p] == DEMOMARKER:
            p += 1
            break
        if p + base * len(present) > len(data):
            d.truncated = True
            break
        tic = []
        for _ in present:
            t = {}
            if d.tasdoom:
                _need(data, p, 4, "tics")
                t["buttons"], fwd, side, turn = data[p:p + 4]
                t["fwd"], t["side"], t["turn"], t["frac"] = s8(fwd), s8(side), s8(turn), 0
                p += 4
            else:
                _need(data, p, 5 if d.longtics else 4, "tics")
                t["fwd"], t["side"] = s8(data[p]), s8(data[p + 1])
                p += 2
                if d.longtics:
                    t["frac"], t["turn"] = data[p], s8(data[p + 1])
                    p += 2
                else:
                    t["turn"], t["frac"] = s8(data[p]), 0
                    p += 1
                t["buttons"] = data[p]
                p += 1
            if d.family != "doom":
                _need(data, p, 2, "tics")
                t["lookfly"], t["arti"] = data[p], data[p + 1]
                p += 2
            if d.excmd:
                _need(data, p, 1, "tics")
                actions = data[p]
                p += 1
                t["ex"] = actions
                if actions & (XC_SAVE | XC_LOAD):
                    raise DemoError("tic %d saves or loads a game (dsda's casual extended commands); a movie cannot"
                                    % len(d.tics))
                if actions & XC_LOOK:
                    _need(data, p, 2, "tics")
                    t["look"] = struct.unpack("<h", bytes(data[p:p + 2]))[0]
                    p += 2
            tic.append(t)
        d.tics.append(tic)

    d.footer = read_footer(data, p)
    if d.footer and "CMDLINE" in d.footer:
        d.footer_text = d.footer["CMDLINE"].decode("latin1", "replace").replace("\0", " ")
        try:
            d.footer_args = shlex.split(d.footer_text)
        except ValueError:
            d.footer_args = d.footer_text.split()
    return d


def guess_versionless_family(data, start):
    """which of Doom 1.2, Heretic and Hexen a versionless demo is, by where its
    end marker falls (dsda's own test, made exact); the game says it when it can"""
    fits = []
    for family, header, bpt in (("doom", 7, 4), ("heretic", 7, 6), ("hexen", 19, 6)):
        if start + header > len(data):
            continue
        if family == "hexen":
            players = sum(1 for i in range(8) if data[start + 3 + 2 * i])
        else:
            players = sum(1 for b in data[start + 3:start + 7] if b)
        longtics = family != "doom" and data[start + 3] & DEMOHEADER_LONGTICS
        step = (bpt + (1 if longtics else 0)) * max(players, 1)
        q = start + header
        while q < len(data) and data[q] != DEMOMARKER:
            q += step
        if q < len(data) and data[q] == DEMOMARKER:
            fits.append(family)
    if len(fits) == 1:
        return fits[0]
    raise DemoError("its header has no version byte, and it could be %s: say the game (--version or --iwad)"
                    % (" or ".join(fits) if fits else "none of Doom 1.2, Heretic or Hexen"))


def read_footer(data, p):
    """dsda's and PrBoom+'s footer: a WAD after the end marker, its lumps by name"""
    at = data.find(b"PWAD", p)
    if at < 0 or at + 12 > len(data):
        return None
    try:
        n, ofs = struct.unpack("<ii", data[at + 4:at + 12])
        lumps = {}
        for i in range(n):
            pos, size, name = struct.unpack("<ii8s", data[at + ofs + 16 * i:at + ofs + 16 * i + 16])
            name = name.rstrip(b"\0").decode("latin1")
            if name:
                lumps[name] = data[at + pos:at + pos + size]
        return lumps
    except struct.error:
        return None


# ------------------------------------------------------------------ the footer's arguments

def footer_values(args, text=""):
    """what the footer's command line says, as dsda reads it (DemoEx_GetParams;
    the overflows' -set by sscanf on the text, "-set name = value")"""
    out = {"iwad": None, "files": [], "deh": [], "flags": set(), "complevel": None, "emulate": None,
           "spechit": None, "overruns": {}}
    i = 0
    while i < len(args):
        a = args[i].lower()
        if a in ("-iwad", "-file", "-deh"):
            vals = []
            while i + 1 < len(args) and not args[i + 1].startswith("-"):
                vals.append(args[i + 1])
                i += 1
            if a == "-iwad":
                out["iwad"] = vals[0] if vals else None
            else:
                out["files" if a == "-file" else "deh"] += vals
        elif a in ("-complevel", "-emulate", "-spechit") and i + 1 < len(args):
            out[a[1:]] = args[i + 1]
            i += 1
        elif a in ("-solo-net", "-coop_spawns", "-chain_episodes", "-respawn", "-fast", "-nomonsters"):
            out["flags"].add(a)
        i += 1
    for name, value in re.findall(r"-set\s+(overrun_\w+_emulate)\s*=\s*(-?\d+)", text):
        out["overruns"][name] = int(value)
    return out


# ------------------------------------------------------------------ the core's declaration

class Declaration:
    def __init__(self, cfg, build=None):
        self.cfg = cfg
        self.build = build or {}
        self.settings = {s["name"]: s for s in cfg["settings"]}
        self.machines = {m["when"][0]: m for m in cfg["machines"]}
        self.version_game = {v: g for g, m in self.machines.items() for v in m["settingOverrides"]["version"]["options"]}
        self.firmware = {f["requiredWhen"]["is"]: f for f in cfg["firmware"]}

    @staticmethod
    def load(package=None, config=None):
        if package:
            with zipfile.ZipFile(package) as z:
                cfg = json.loads(z.read("waterbox.config"))
                build = json.loads(z.read("build.json")) if "build.json" in z.namelist() else {}
            with open(package, "rb") as f:
                build["packageSha1"] = hashlib.sha1(f.read()).hexdigest().upper()
            return Declaration(cfg, build)
        return Declaration(json.load(open(config or DEFAULT_CONFIG)))

    def option(self, name, leading):
        """the enum option of a setting whose value starts with this number"""
        for o in self.settings[name]["options"]:
            if re.match(r"%d\b" % leading, o):
                return o
        raise DemoError("the core has no %s %d" % (self.settings[name]["display"], leading))

    def version_of_sha1(self, sha1):
        for v, f in self.firmware.items():
            if f.get("sha1", "").upper() == sha1:
                return v
        return None


def sha1_of(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest().upper()


def find_file(name, dirs):
    """a file of this name in the folders, whatever its case"""
    want = os.path.basename(name).lower()
    for d in dirs:
        for f in sorted(os.listdir(d)):
            if f.lower() == want and os.path.isfile(os.path.join(d, f)):
                return os.path.join(d, f)
    return None


def wad_lump(path, name):
    """a WAD's last lump of this name, or None"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] not in (b"IWAD", b"PWAD"):
        return None
    n, ofs = struct.unpack("<ii", data[4:12])
    found = None
    for i in range(n):
        pos, size, nm = struct.unpack("<ii8s", data[ofs + 16 * i:ofs + 16 * i + 16])
        if nm.rstrip(b"\0").decode("latin1").upper() == name:
            found = data[pos:pos + size]
    return found


def hexen_warp(gamemap, iwad, pwads):
    """the warp number that reaches Hexen's map gamemap: the engine reads the
    last MAPINFO loaded (a PWAD's replaces the IWAD's), where each map's
    warptrans defaults to its number, and -warp N goes to the first map whose
    warptrans is N (P_TranslateMap)"""
    text = None
    for path in [iwad] + [p for p in pwads if p.lower().endswith(".wad")]:
        lump = wad_lump(path, "MAPINFO")
        if lump is not None:
            text = lump.decode("latin1", "replace")
    if text is None:
        raise DemoError("no MAPINFO in the IWAD or the PWADs: Hexen's maps are reached by their warp numbers")
    trans = {}
    tokens = re.findall(r'"[^"]*"|[^\s]+', re.sub(r";[^\n]*", "", text))
    i, current = 0, None
    while i < len(tokens):
        t = tokens[i].lower()
        if t == "map" and i + 1 < len(tokens) and tokens[i + 1].isdigit():
            current = int(tokens[i + 1])
            trans[current] = current
            i += 2
            continue
        if t == "warptrans" and current is not None and i + 1 < len(tokens) and tokens[i + 1].lstrip("-").isdigit():
            trans[current] = int(tokens[i + 1])
            i += 2
            continue
        i += 1
    if gamemap not in trans:
        raise DemoError("MAPINFO has no map %d" % gamemap)
    warp = trans[gamemap]
    first = next((m for m in range(1, 99) if trans.get(m) == warp), None)
    if first != gamemap:
        raise DemoError("Hexen's -warp %d goes to map %d, not the demo's %d: the core starts a game by its warp number" % (warp, first, gamemap))
    return warp


# ------------------------------------------------------------------ the import

def complevel_of(demo, game, footer):
    """the compatibility level the engine plays the demo at"""
    if demo.family != "doom":
        return None
    if not (demo.version and 104 <= demo.version <= 111):
        return demo.complevel   # 1.2's 0, a Boom-or-later header's own
    # 1.4-1.9, TASDoom, longtics: G_GetOriginalDoomCompatLevel - the footer's
    # -complevel first, then the version and the game
    if footer["complevel"] is not None and footer["complevel"].lstrip("-").isdigit():
        return int(footer["complevel"])
    if demo.version == 110:
        return 6
    if demo.version < 107:
        return 1
    if game in RETAIL_GAMES:
        return 3
    if game in FINAL_GAMES:
        return 4
    return 2


def resolve_version(demo, decl, footer, version, iwad, wad_dirs):
    """the IWAD's release: --version, --iwad's hash, or the footer's -iwad"""
    if iwad:
        v = decl.version_of_sha1(sha1_of(iwad))
        if not v:
            raise DemoError("%s (SHA1 %s) is none of the IWADs the core pins" % (iwad, sha1_of(iwad)))
        return v, iwad
    if version:
        if version not in decl.version_game:
            raise DemoError("the core has no version %s (it has %s)" % (version, ", ".join(decl.version_game)))
        return version, None
    candidates = list(decl.version_game)
    if footer["iwad"]:
        name = os.path.basename(footer["iwad"]).lower()
        candidates = [v for v in candidates if decl.firmware[v]["id"].lower() == name]
        found = find_file(footer["iwad"], wad_dirs) if wad_dirs else None
        if found:
            v = decl.version_of_sha1(sha1_of(found))
            if v:
                return v, found
    if demo.family != "doom":
        candidates = [v for v in candidates if decl.version_game[v] == demo.family]
    if len(candidates) == 1:
        return candidates[0], None
    raise DemoError("the demo does not say which IWAD it is for%s: give --version (%s) or --iwad"
                    % (" beyond its name, %s," % footer["iwad"] if footer["iwad"] else "",
                       ", ".join(candidates) if candidates else "none fits"))


def import_demo(data, decl, version=None, iwad=None, wad_dirs=(), pwads=None, monster_flags=(), name="demo", no_pwads=False,
                longtics=False):
    """the project's parts: settings, firmware, files, input log, notes"""
    footer = footer_values([])
    family_hint = None
    if version and version in decl.version_game:
        family_hint = {"heretic": "heretic", "hexen": "hexen"}.get(decl.version_game[version], "doom")
    elif iwad:
        v = decl.version_of_sha1(sha1_of(iwad))
        if v:
            family_hint = {"heretic": "heretic", "hexen": "hexen"}.get(decl.version_game[v], "doom")
    demo = parse_demo(data, family_hint, longtics)
    if longtics and demo.family == "doom":
        raise DemoError("--longtics is for Heretic and Hexen demos; a Doom demo's format says its turning")
    footer = footer_values(demo.footer_args, demo.footer_text)
    version, iwad_path = resolve_version(demo, decl, footer, version, iwad, wad_dirs)
    game = decl.version_game[version]
    family = {"heretic": "heretic", "hexen": "hexen"}.get(game, "doom")
    if family != demo.family:
        raise DemoError("it is a %s demo, and %s is %s's" % (demo.family.capitalize(), version, game))
    notes = []

    s = {"game": game, "version": version}
    if family == "doom":
        cl = complevel_of(demo, game, footer)
        s["compatibilityLevel"] = decl.option("compatibilityLevel", cl)
    if demo.skill > 4:
        raise DemoError("its skill %d is past Nightmare (4)" % demo.skill)
    s["skillLevel"] = decl.settings["skillLevel"]["options"][demo.skill]
    s["initialEpisode"] = demo.episode
    s["initialMap"] = demo.map
    s["multiplayerMode"] = decl.settings["multiplayerMode"]["options"][min(demo.deathmatch, 2)]
    flags = footer["flags"] | set(monster_flags)
    if demo.respawn is not None:
        respawn, fast, nomonsters = demo.respawn, demo.fast, demo.nomonsters
    else:
        # 1.2 and the Raven games: the footer's flags (or the person's), and
        # the Raven games' bits on player one's byte
        respawn = "-respawn" in flags or bool(demo.raven_bits & DEMOHEADER_RESPAWN)
        fast = "-fast" in flags
        nomonsters = "-nomonsters" in flags or bool(demo.raven_bits & DEMOHEADER_NOMONSTERS)
        if family == "doom" and not ({"-respawn", "-fast", "-nomonsters"} & flags):
            notes.append("a 1.2 demo does not hold its monster flags: none assumed (--respawn, --fast, --nomonsters)")
    s["monstersRespawn"], s["fastMonsters"], s["noMonsters"] = bool(respawn), bool(fast), bool(nomonsters)
    for i in range(4):
        s["player%dPresent" % (i + 1)] = demo.players[i]
        if family == "hexen":
            if demo.classes[i] > 2:
                raise DemoError("player %d's class is %d; Hexen's are 0-2" % (i + 1, demo.classes[i]))
            s["player%dClass" % (i + 1)] = CLASSES[demo.classes[i]]
    s["displayPlayer"] = demo.consoleplayer + 1 if demo.consoleplayer < 4 and demo.players[demo.consoleplayer] else \
        next(i + 1 for i in range(4) if demo.players[i])
    s["turningResolution"] = "16 bits (longtics)" if demo.longtics else "8 bits (shorttics)"
    s["renderWipescreen"] = False
    s["strafe50Turns"] = "Allow"
    s["preventLevelExit"] = False
    s["preventGameEnd"] = False
    s["pistolStart"] = False
    if family == "doom":
        if demo.rngseed is not None:
            s["rngSeed"] = demo.rngseed - (1 << 32) if demo.rngseed >= 1 << 31 else demo.rngseed
        s["demoOptions"] = demo.options.hex().upper() if demo.options is not None else ""
        s["spechitAddress"] = int(footer["spechit"], 0) if footer["spechit"] else 0
        for cfg_name, setting in OVERRUNS:
            if cfg_name in footer["overruns"]:
                s[setting] = bool(footer["overruns"][cfg_name])
    s["soloNet"] = "-solo-net" in flags
    s["coopSpawns"] = "-coop_spawns" in flags
    s["chainEpisodes"] = "-chain_episodes" in flags
    s["emulatePrBoom"] = footer["emulate"] or ""
    s["extendedCommands"] = ["Off", "On", "On, with casual features"][(1 if demo.excmd else 0) + (1 if demo.casual else 0)]
    for k in s:
        if k not in decl.settings:
            raise DemoError("the core declares no setting %s (an older package?)" % k)

    # the firmware: the IWAD, by the version's hash
    fw = decl.firmware[version]
    firmware = [{"id": fw["id"], "sha1": fw["sha1"].upper()}]

    # the files: --pwad, else the footer's -file and -deh, found in --wads
    files = []
    if no_pwads:
        paths = []
        if footer["files"] or footer["deh"]:
            notes.append("its footer names %s; none taken (--no-pwads)" % ", ".join(footer["files"] + footer["deh"]))
    elif pwads:
        paths = list(pwads)
    else:
        paths = []
        for n in footer["files"] + footer["deh"]:
            if os.path.basename(n).lower() in ("dsda-doom.wad", "prboom-plus.wad", "prboom.wad"):
                continue  # the port's own data, the core's
            found = find_file(n, wad_dirs) if wad_dirs else None
            if not found:
                raise DemoError("its footer names %s, which is in none of the folders given (--wads), or give --pwad" % n)
            paths.append(found)
    wads = [p for p in paths if not p.lower().endswith((".deh", ".bex"))]
    patches = [p for p in paths if p.lower().endswith((".deh", ".bex"))]
    for p in wads + patches:
        files.append({"name": os.path.basename(p), "sha1": sha1_of(p), "slot": "pwad"})
    if not files and (footer["files"] or footer["deh"]) and pwads is None:
        notes.append("its footer names no PWAD the core needs")

    if game == "hexen":
        # the core's initial map is what -warp takes, as BizHawk's
        iw = iwad_path or (find_file(fw["id"], wad_dirs) if wad_dirs else None)
        if not iw or sha1_of(iw) != fw["sha1"].upper():
            raise DemoError("a Hexen demo's map is reached by its warp number, which the IWAD's MAPINFO says: give --iwad or --wads")
        s["initialMap"] = hexen_warp(demo.map, iw, wads)
        if s["initialMap"] != demo.map:
            notes.append("Hexen's map %d is warp %d" % (demo.map, s["initialMap"]))

    if demo.truncated:
        notes.append("it has no end marker: its tics run to the file's end, as dsda plays them")

    log, frames = input_log(demo, decl, game, s)
    info = {"format": demo.format, "complevel": s.get("compatibilityLevel"), "tics": len(demo.tics),
            "players": [i + 1 for i in range(4) if demo.players[i]], "footer": " ".join(demo.footer_args),
            "port": (demo.footer or {}).get("PORTNAME", b"").decode("latin1", "replace").strip("\0\n "),
            "notes": notes, "iwad": iwad_path}
    return s, firmware, files, log, frames, info


# ------------------------------------------------------------------ the input log

def active_inputs(decl, game, s):
    """the controller's active inputs, in the engine's order: a group a port
    (the machine's first), each group's axes then its buttons, as the core
    declares them; what a setting leaves out is inactive (dsdadrv_*_active)"""
    ctl = decl.machines[game]["input"]
    present = [s["player%dPresent" % p] for p in range(1, 5)]
    longtics = s["turningResolution"].startswith("16")
    excmd = {"Off": 0, "On": 1}.get(s["extendedCommands"], 2)

    def port_of(name):
        m = re.match(r"P(\d+) ", name)
        return int(m.group(1)) if m else 0

    def active(name, axis):
        port = port_of(name)
        if port and not present[port - 1]:
            return False
        tail = name.split(" ", 1)[1] if port else name
        if axis and tail == "Turn Speed Frac." and not longtics:
            return False
        if axis and tail == "Free Look" and not excmd:
            return False
        if not axis and tail == "Jump" and game != "hexen" and not excmd:
            return False
        if not axis and tail in ("God", "No Clip") and excmd < 2:
            return False
        return True

    axes = [(a["name"], a) for a in ctl["axes"] if active(a["name"], True)]
    buttons = [b for b in ctl["buttons"] if active(b, False)]
    ngroups = max([port_of(n) for n, _ in axes] + [port_of(b) for b in buttons]) + 1
    groups = [[] for _ in range(ngroups)]
    for n, a in axes:
        groups[port_of(n)].append((n, a))
    for b in buttons:
        groups[port_of(b)].append((b, None))
    return groups


def input_log(demo, decl, game, s):
    groups = active_inputs(decl, game, s)
    key = "".join("#" + "".join(n + "|" for n, _ in g) for g in groups)
    present = [i for i in range(4) if demo.players[i]]
    hexen = game == "hexen"
    lines = []
    for ticno, tic in enumerate(demo.tics):
        values = {}
        for slot, t in zip(present, tic):
            P = "P%d " % (slot + 1)
            values[P + "Run Speed"] = t["fwd"]
            values[P + "Strafe Speed"] = t["side"]
            values[P + "Turn Speed"] = t["turn"]
            values[P + "Turn Speed Frac."] = t["frac"]
            b = t["buttons"]
            if b & BT_SPECIAL:
                # a special command: in Doom only its pause does anything
                # (dsda's G_Ticker), and it has no buttons after; the Raven
                # games keep its low bits until the player thinks (a dead
                # player's use, the intermission's skip read them) - Special
                if demo.family == "doom" or (b & 0x7F) in (0, 1):
                    if (b & BT_SPECIALMASK) == BT_PAUSE:
                        values[P + "Pause"] = True
                else:
                    values[P + "Special"] = b & 0x7F
            else:
                values[P + "Fire"] = bool(b & BT_ATTACK)
                values[P + "Use"] = bool(b & BT_USE)
                if b & BT_CHANGE:
                    # the weapon's bits as they are (BT_WEAPONMASK, four), + 1
                    values[P + "Weapon Select"] = ((b >> 3) & 15) + 1
            if demo.family != "doom":
                look, fly = t["lookfly"] & 15, t["lookfly"] >> 4
                values[P + "Look"] = look - 16 if look > 8 else look
                values[P + "Fly"] = fly - 16 if fly > 8 else fly
                arti = t["arti"]
                if not hexen and arti & ~AFLAG_MASK:
                    raise DemoError("tic %d: Heretic's artifact byte %d has Hexen's flags" % (ticno, arti))
                values[P + "Artifact"] = arti & AFLAG_MASK
                if hexen:
                    values[P + "Jump"] = bool(arti & AFLAG_JUMP)
                    values[P + "End Player"] = bool(arti & AFLAG_SUICIDE)
            if "ex" in t:
                ex = t["ex"]
                if ex & XC_JUMP:
                    if hexen:
                        raise DemoError("tic %d: an extended jump in Hexen, which jumps with its artifact flag" % ticno)
                    values[P + "Jump"] = True
                if ex & XC_LOOK:
                    values[P + "Free Look"] = t.get("look", 0)
                if ex & XC_GOD:
                    values[P + "God"] = True
                if ex & XC_NOCLIP:
                    values[P + "No Clip"] = True
        line = "|"
        for g in groups:
            for n, a in g:
                if a is not None:
                    v = values.get(n, a.get("neutral", 0))
                    if v is True:
                        raise DemoError("tic %d: %s is an axis, given a press" % (ticno, n))
                    if not a["min"] <= v <= a["max"]:
                        raise DemoError("tic %d: %s is %d, past the core's %d..%d" % (ticno, n, v, a["min"], a["max"]))
                    line += "%5d," % v
                else:
                    v = values.get(n)
                    if v not in (None, False, True):
                        raise DemoError("tic %d: %s is a button, given %s" % (ticno, n, v))
                    line += mnemonic(n) if v else "."
            line += "|"
        lines.append(line)
        # every value the tic set must have a column, or the movie loses it
        for n, v in values.items():
            if v and v is not True and not any(n == m for g in groups for m, _ in g):
                raise DemoError("tic %d: %s is %s, and the core's controller has no such input active" % (ticno, n, v))
            if v is True and not any(n == m for g in groups for m, _ in g):
                raise DemoError("tic %d: %s is pressed, and the core's controller has no such input active" % (ticno, n))
    return "[Input]\nLogKey:" + key + "\n" + "".join(l + "\n" for l in lines) + "[/Input]\n", len(lines)


def mnemonic(name):
    tail = name.split(" ", 1)[1] if re.match(r"P\d+ ", name) else name
    return {"Fire": "F", "Use": "U", "Pause": "P", "Jump": "J", "End Player": "E", "God": "G", "No Clip": "N"}.get(tail, tail[0])


def project(title, decl, s, firmware, files, log, frames, info):
    core = {"name": decl.cfg.get("coreName", "DSDA-Doom"), "version": decl.build.get("version", ""),
            "sha1": decl.build.get("packageSha1", "")}
    return {
        "title": title,
        "description": "Imported from %s: %s, %d tics%s." % (
            info.get("source", "a demo"), info["format"], info["tics"],
            (", recorded with " + info["port"]) if info["port"] else ""),
        "core": core,
        "rerecords": 0,
        "files": files,
        "settings": s,
        "firmware": firmware,
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Tasproj v1.1",
            "Platform": decl.machines[s["game"]].get("id", "Doom"),
            "LastInputFrame": str(max(frames - 1, 0)),
            "VsyncNumerator": "35",
            "VsyncDenominator": "1",
        },
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("demo")
    ap.add_argument("-o", "--out")
    ap.add_argument("--version")
    ap.add_argument("--iwad")
    ap.add_argument("--wads", action="append", default=[])
    ap.add_argument("--pwad", action="append")
    ap.add_argument("--no-pwads", action="store_true")
    ap.add_argument("--longtics", action="store_true")
    ap.add_argument("--package")
    ap.add_argument("--config")
    ap.add_argument("--respawn", action="store_true")
    ap.add_argument("--fast", action="store_true")
    ap.add_argument("--nomonsters", action="store_true")
    ap.add_argument("--title")
    ap.add_argument("--info", action="store_true")
    a = ap.parse_args()
    decl = Declaration.load(a.package, a.config)
    data = open(a.demo, "rb").read()
    flags = [f for f, on in (("-respawn", a.respawn), ("-fast", a.fast), ("-nomonsters", a.nomonsters)) if on]
    try:
        s, fw, files, log, frames, info = import_demo(data, decl, a.version, a.iwad, a.wads, a.pwad, flags,
                                                     os.path.basename(a.demo), a.no_pwads, a.longtics)
    except DemoError as e:
        sys.exit("%s: %s" % (a.demo, e))
    info["source"] = os.path.basename(a.demo)
    print("%s: %s, %d tics, player%s %s, %s%s" % (
        os.path.basename(a.demo), info["format"], info["tics"], "s" if len(info["players"]) > 1 else "",
        ", ".join(map(str, info["players"])), s["version"],
        (", " + s["compatibilityLevel"]) if "compatibilityLevel" in s else ""), file=sys.stderr)
    if info["footer"]:
        print("  footer: %s" % info["footer"], file=sys.stderr)
    for n in info["notes"]:
        print("  note: %s" % n, file=sys.stderr)
    if a.info:
        print(json.dumps({"settings": s, "firmware": fw, "files": files, "frames": frames}, indent=2))
        return
    out = a.out or os.path.splitext(a.demo)[0] + ".chimeraProject"
    p = project(a.title or os.path.splitext(os.path.basename(a.demo))[0], decl, s, fw, files, log, frames, info)
    with open(out, "w") as f:
        json.dump(p, f, indent=2)
        f.write("\n")
    print("  wrote %s" % out, file=sys.stderr)


if __name__ == "__main__":
    main()
