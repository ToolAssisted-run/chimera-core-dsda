#!/usr/bin/env python3
"""gen-declaration.py - the core's declaration, from one place: the games (the
machines), their IWAD releases (the version setting, each pinning its IWAD as
firmware), BizHawk's settings, and the controllers (dumped from dsda-input.c,
the driver's own lists). Writes waterbox.config, file_slots.json and
dsda-versions.h (the driver's table); the gate checks they are up to date.

usage: gen-declaration.py [--check]
"""
import collections, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- the games: machine setting "game"; all BizHawk's system, Doom
GAMES = [
    # id, label, controller format
    ("doom2", "Doom II", "doom"),
    ("doom", "Doom", "doom"),
    ("tnt", "Final Doom: TNT - Evilution", "doom"),
    ("plutonia", "Final Doom: The Plutonia Experiment", "doom"),
    ("heretic", "Heretic", "heretic"),
    ("hexen", "Hexen", "hexen"),
    ("chex", "Chex Quest", "doom"),
    ("freedoom1", "Freedoom: Phase 1", "doom"),
    ("freedoom2", "Freedoom: Phase 2", "doom"),
]

# ---- the releases of each game's IWAD: the version setting. The IWAD is
# mounted under its canonical name (upstream tells the game and its mission
# by it, d_main.c AddIWAD); each release pins the hash of the dump it was
# tested with - the wizard's folder scan finds it by that, and a file of the
# user's own is taken too, the project pinning its hash.
V = collections.namedtuple("V", "id game label iwad sha1 size episodic")
VERSIONS = [
    V("doom2-1.9", "doom2", "Doom II v1.9", "doom2.wad", "7EC7652FCFCE8DDC6E801839291F0E28EF1D5AE7", 14604584, False),
    V("doom-ultimate", "doom", "The Ultimate Doom v1.9", "doom.wad", "117015379C529573510BE08CF59810AA10BB934E", 12474561, True),
    V("tnt", "tnt", "TNT: Evilution (original release)", "tnt.wad", "139E26D801A64B404B8D898DEFCA10227A61867B", 18222568, False),
    V("plutonia", "plutonia", "The Plutonia Experiment (original release)", "plutonia.wad", "327F8C41EBD4138354E9FCA63CEBBBD1B9489749", 17417800, False),
    V("heretic-1.2", "heretic", "Heretic v1.2 (three episodes)", "heretic.wad", "A54C5D30629976A649119C5CE8BABAE2DDFB1A60", 11095516, True),
    V("hexen-1.1", "hexen", "Hexen v1.1", "hexen.wad", "4B53832F0733C1E29E5F1DE2428E5475E891AF29", 20083672, False),
    V("chex", "chex", "Chex Quest", "chex.wad", "ECA9CFF1014CE5081804E193588D96C6DDB35432", 12361532, True),
    V("freedoom1-0.13.0", "freedoom1", "Freedoom: Phase 1 v0.13.0", "freedoom1.wad", "97BB88094A51457A8DCAD98C58BE22A2D0FA9A37", None, True),
    V("freedoom1-0.12.1", "freedoom1", "Freedoom: Phase 1 v0.12.1", "freedoom1.wad", "E9BF428B73A04423EA7A0E9F4408F71DF85AB175", None, True),
    V("freedoom1-0.12.0", "freedoom1", "Freedoom: Phase 1 v0.12.0", "freedoom1.wad", "351A207B5FE520EA618BC8659D176F4C39047D20", None, True),
    V("freedoom1-0.11.3", "freedoom1", "Freedoom: Phase 1 v0.11.3", "freedoom1.wad", "36DB0EB476486FA56C43F24D9B23E9D8AFDBBFF5", None, True),
    V("freedoom1-0.11.2", "freedoom1", "Freedoom: Phase 1 v0.11.2", "freedoom1.wad", "7931A41E6B1C6B7C701AB16D7355E7685F96B1CC", None, True),
    V("freedoom1-0.11.1", "freedoom1", "Freedoom: Phase 1 v0.11.1", "freedoom1.wad", "F81E8BB84000DAECD682ABF95357CF55FB57FBB7", None, True),
    V("freedoom1-0.11", "freedoom1", "Freedoom: Phase 1 v0.11", "freedoom1.wad", "9E38DCC0D1E9FBD20382BA19A6BDF11F7A2B0502", None, True),
    V("freedoom2-0.13.0", "freedoom2", "Freedoom: Phase 2 v0.13.0", "freedoom2.wad", "975F781E6D801C0A23E3CAA33F70493EFE68A880", None, False),
    V("freedoom2-0.12.1", "freedoom2", "Freedoom: Phase 2 v0.12.1", "freedoom2.wad", "51C997F430DC12ABBA7888C2D83C237A8E0758A7", None, False),
    V("freedoom2-0.12.0", "freedoom2", "Freedoom: Phase 2 v0.12.0", "freedoom2.wad", "18C6EF3F269A80FEED16EB46C7577D29D4209098", None, False),
    V("freedoom2-0.11.3", "freedoom2", "Freedoom: Phase 2 v0.11.3", "freedoom2.wad", "0C03D1F754B98C53F292F66BC9505A29F47C6A9F", None, False),
    V("freedoom2-0.11.2", "freedoom2", "Freedoom: Phase 2 v0.11.2", "freedoom2.wad", "26FBA39016C26CF58756AA2CBF715A34F83857D3", None, False),
    V("freedoom2-0.11.1", "freedoom2", "Freedoom: Phase 2 v0.11.1", "freedoom2.wad", "8B4385450D3C622CA7AAE3D00C7E15829DA264CF", None, False),
    V("freedoom2-0.11", "freedoom2", "Freedoom: Phase 2 v0.11", "freedoom2.wad", "25110745824A107F80B078CF368A1045661DF3B5", None, False),
]

DOOM_GAMES = [g for g, _, f in GAMES if f == "doom"]
RAVEN_GAMES = [g for g, _, f in GAMES if f != "doom"]


def S(name, display, type_, default, description, **kw):
    s = collections.OrderedDict([("name", name), ("display", display), ("type", type_)])
    for k in ("options", "min", "max"):
        if k in kw: s[k] = kw.pop(k)
    s["default"] = default
    s["description"] = description
    if "when" in kw: s["when"] = kw.pop("when")
    assert not kw, kw
    return s


COMPLEVELS = ["0 - Doom v1.2", "1 - Doom v1.666", "2 - Doom & Doom 2 v1.9", "3 - Ultimate Doom & Doom95", "4 - Final Doom",
              "5 - DOSDoom", "6 - TASDoom", "7 - Boom's Vanilla Compatibility Mode", "8 - Boom v2.01", "9 - Boom v2.02",
              "10 - LxDoom", "11 - MBF", "12 - PrBoom v2.03beta", "13 - PrBoom v2.1.0", "14 - PrBoom v2.1.1 - 2.2.6",
              "15 - PrBoom v2.3.x", "16 - PrBoom v2.4.0", "17 - PrBoom Latest", "21 - MBF21"]
SKILLS = ["1 - I'm too young to die", "2 - Hey, not too rough", "3 - Hurt me plenty", "4 - Ultra-Violence", "5 - Nightmare!"]

# BizHawk's settings (DSDA.ISettable.cs), their names, defaults and descriptions:
# its sync settings, then its (non-sync) settings - part of the machine here,
# as Chimera has no cosmetic settings.
SETTINGS = [
    S("game", "Game", "enum", GAMES[0][0], "Which game the machine is (Chimera's System): the IWAD's. Not on the settings page.",
      options=[g for g, _, _ in GAMES]),
    S("version", "Version", "enum", VERSIONS[0].id,
      "Which release of the game's IWAD (Chimera's Version): the IWAD the project brings as firmware, by its hash. Not on the settings page.",
      options=[v.id for v in VERSIONS]),
    S("compatibilityLevel", "Compatibility Level", "enum", "2 - Doom & Doom 2 v1.9",
      "The version of Doom or its ports that this movie is meant to emulate. Highest vanilla-compatible level is 'Final Doom'. Newer WADs may require higher levels. Standalone DSDA-Doom defaults to MBF21, which supports features of all of the lower levels, but is the farthest from vanilla.",
      options=COMPLEVELS, when=DOOM_GAMES),
    S("skillLevel", "Skill Level", "enum", "4 - Ultra-Violence",
      "Difficulty setting. Vanilla defaults to 'Hurt me plenty', but the de-facto current standard is 'Ultra-Violence'.", options=SKILLS),
    S("multiplayerMode", "Multiplayer Mode", "enum", "Single Player / Cooperative", "Indicates the multiplayer mode.",
      options=["Single Player / Cooperative", "Deathmatch", "Alternate Deathmatch (v2.0)"]),
    S("initialEpisode", "Initial Episode", "int", 1, "Selects the initial episode. Ignored for non-episodic IWADs (e.g., DOOM2) and Shareware.", min=0, max=9),
    S("initialMap", "Initial Map", "int", 1, "Selects the initial map.", min=1, max=99),
    S("fastMonsters", "Fast Monsters", "bool", False, "Makes monsters move and attack much faster. Forced to 'true' when playing Nightmare! difficulty."),
    S("monstersRespawn", "Monsters Respawn", "bool", False, "Makes monsters respawn shortly after dying. Forced to 'true' when playing Nightmare! difficulty."),
    S("noMonsters", "No Monsters", "bool", False, "Removes all monsters from the level."),
    S("pistolStart", "Pistol Start", "bool", False, "Starts every level with a clean slate, with nothing carried over from previous levels. Health is reset to 100% as well."),
    S("coopSpawns", "Coop Mode Spawns", "bool", False, "Play single-player mode with cooperative mode thing spawns."),
    S("chainEpisodes", "Chain Episodes", "bool", False, "Completing one episode leads to the next without interruption. Not available in vanilla."),
    S("alwaysRun", "Always Run", "bool", True,
      "Toggles whether the player is permanently in the running state, without the slower walking speed available. This emulates a bug in vanilla Doom: setting the joystick run button to an invalid high number causes the game to always have it enabled."),
    S("renderWipescreen", "Render Wipescreen", "bool", True,
      "Enables screen melt - an effect seen when Doom changes scene, for example, when starting or exiting a level. Can't be disabled in vanilla. Its frames are lag: the game waits."),
    S("turningResolution", "Turning Resolution", "enum", "16 bits (longtics)",
      "'Shorttics' refers to decreased turning resolution normally used for demos. 'Longtics' refers to the regular turning resolution outside of a demo-recording environment. Newer demo formats support both.",
      options=["16 bits (longtics)", "8 bits (shorttics)"]),
    S("mouseTurnSensitivity", "Horizontal Mouse Sensitivity", "int", 10, "How fast the Doom player will turn when using the mouse.", min=0, max=100),
    S("mouseRunSensitivity", "Vertical Mouse Sensitivity", "int", 1, "How fast the Doom player will run when using the mouse.", min=0, max=100),
    S("strafe50Turns", "Turning During Strafe50", "enum", "Allow",
      "\"Strafe\" key is required to convert angular movement into strafe50, without it maximum strafe value is 40. So using keyboard and mouse, it's impossible to turn during strafe50. But if strafe50+turning appears in a demo, the game will process it fine, which makes it a TAS-only feature. This setting allows disabling it for maximum authenticity.",
      options=["Ignore", "Allow"]),
    S("preventLevelExit", "Prevent Level Exit", "bool", False,
      "Level exit triggers won't have an effect. This is useful for debugging / optimizing / botting purposes. Not available in vanilla."),
    S("preventGameEnd", "Prevent Game End", "bool", False,
      "Game end triggers won't have an effect. This is useful for debugging / optimizing / botting purposes. Not available in vanilla."),
    S("turbo", "Turbo", "int", -1, "Modifies the player running / strafing speed [0-255]. '-1' means Disabled.", min=-1, max=255),
    S("rngSeed", "Initial RNG Seed", "int", 1993, "Only for compatibility level 7 (Boom) and above. Default value is 1993.",
      min=0, max=2147483647, when=DOOM_GAMES),
] + [S("player%dPresent" % p, "Player %d Present" % p, "bool", p == 1, "Specifies if player %d is present" % p) for p in range(1, 5)] + [
    S("player%dClass" % p, "[Hexen] Player %d Class" % p, "enum", "Fighter", "The Hexen class to use for player %d." % p,
      options=["Fighter", "Cleric", "Mage"], when=["hexen"]) for p in range(1, 5)] + [
    S("scaleFactor", "Internal Resolution Scale Factor", "int", 1,
      "Which factor to increase internal resolution by [1 - 12]. Improves \"quality\" of the rendered image at the cost of accuracy. Vanilla resolution is 320x200 resized to 4:3 DAR on a CRT monitor. The core's picture is at most 3840x2400: the widescreen aspects go up to 5.",
      min=1, max=12),
    S("internalAspect", "Internal Aspect Ratio", "enum", "Native",
      "Sets aspect ratio of the rendered screen. 'Native' is multiples of 320x200 with aspect correction (to 4:3) applied by the frontend, similar to vanilla. Other modes produce pre-corrected image, useful for viewing Automap on higher resolutions (to avoid pixel distortion caused by external aspect correction).",
      options=["Native", "16:9", "16:10", "4:3"]),
    S("sfxVolume", "Sfx Volume", "int", 8, "Sound effects volume [0 - 15].", min=0, max=15),
    S("musicVolume", "Music Volume", "int", 8, "[0 - 15]", min=0, max=15),
    S("gamma", "Gamma Correction Level", "int", 0,
      "Increases brightness [0 - 4]. Default value in vanilla is \"OFF\" (0). The Change Gamma button cycles it.", min=0, max=4),
    S("showMessages", "Show Messages", "bool", True, "Displays messages about items you pick up. Default value in vanilla is \"ON\"."),
    S("reportSecrets", "Report Revealed Secrets", "bool", False, "Shows an on-screen notification when revealing a secret. Not available in vanilla."),
    S("hudMode", "HUD Mode", "enum", "Vanilla", "Sets heads-up display mode.", options=["Vanilla", "DSDA", "None"]),
    S("dsdaExHud", "Extended HUD", "bool", False, "Shows DSDA-Doom-specific information above vanilla heads-up-display. Not available in vanilla."),
    S("displayCoordinates", "Display Coordinates", "bool", False,
      "Shows player position, angle, velocity, and distance travelled per frame. Color indicates movement tiers: green - SR40, blue - SR50, red - turbo/wallrun. Available in vanilla via the IDMYPOS cheat code, however vanilla only shows angle, X, and Y."),
    S("displayCommands", "Display Commands", "bool", False,
      "Shows input history on the screen. History size is 10, empty commands are excluded. Not available in vanilla."),
    S("mapTotals", "Automap Totals", "bool", False, "Shows counts for kills, items, and secrets on Automap. Not available in vanilla."),
    S("mapTime", "Automap Time", "bool", False, "Shows elapsed time on Automap. Not available in vanilla."),
    S("mapCoordinates", "Automap Coordinates", "bool", False, "Shows in-level coordinates on Automap. Not available in vanilla."),
    S("mapOverlay", "Automap Overlay", "enum", "Disabled", "Shows Automap on top of gameplay. Not available in vanilla.",
      options=["Disabled", "Enabled", "Dark"]),
    S("mapDetails", "Automap Details", "enum", "Normal", "Exposes all linedefs and things. Available in vanilla via the IDDT cheat code.",
      options=["Normal", "Linedefs", "Linedefs and things"]),
    S("mapTrail", "Map Trail", "bool", False,
      "Shows previous positions of the player as a trail when Automap details are enabled. Not available in vanilla."),
    S("mapTrailSize", "Map Trail Size", "int", 105, "Number of previous positions to display as a trail on Automap [1 - 350].", min=1, max=350),
    S("fullVision", "Full Vision", "bool", False, "Disables all darkness. Available in vanilla via the IDBEHOLDL cheat code."),
    S("displayPlayer", "Player Point of View", "int", 1,
      "Which of the players' point of view to use during rendering [1 - 4]; a player not present falls to the first one present.", min=1, max=4),
] + [
    # what a demo can dictate beyond BizHawk's settings (tools/lmp-import.py
    # sets them): its header's option block and DSDA format, its footer's
    # arguments
    S("soloNet", "Solo Net", "bool", False,
      "Plays a single player as a netgame (dsda's -solo-net), as a demo's footer may say. More than one player is a netgame anyway."),
    S("demoOptions", "Boom/MBF Options", "string", "",
      "The game options a Boom-or-later demo's header holds, as hex: Boom's, MBF's and PrBoom's 64 bytes, or MBF21's 21 and its comp flags. Monster memory, friction, pushers, bobbing, demo insurance, infighting, helper dogs and their distance, the monsters' behaviour, the comp flags. Empty: the complevel's defaults and a PWAD's OPTIONS lump, as dsda records. The monster flags and the seed in it are the settings'.",
      when=DOOM_GAMES),
    S("extendedCommands", "Extended Commands", "enum", "Off",
      "dsda's extended tic commands, which its DSDA demo format holds: jumping and free look, and with the casual features god mode and no clipping. Off for every other demo format.",
      options=["Off", "On", "On, with casual features"]),
    S("emulatePrBoom", "Emulate PrBoom+ Version", "string", "",
      "Plays as an older PrBoom+ did (dsda's -emulate, a version such as 2.5.0.8), as a demo's footer may ask. Empty: as dsda plays."),
    S("spechitAddress", "Spechit Overrun Base Address", "int", 0,
      "The memory address the spechit overflow's emulation assumes (dsda's -spechit), as a demo's footer may say. 0: doom2.exe's (0x01C09C98).",
      min=0, max=2147483647, when=DOOM_GAMES),
    S("overrunSpechit", "Emulate Spechit Overflow", "bool", True, "Emulates vanilla's spechit overflow (dsda's overrun_spechit_emulate, on by default).", when=DOOM_GAMES),
    S("overrunReject", "Emulate Reject Overflow", "bool", True, "Emulates vanilla's REJECT overflow (overrun_reject_emulate, on by default).", when=DOOM_GAMES),
    S("overrunIntercept", "Emulate Intercepts Overflow", "bool", True, "Emulates vanilla's intercepts overflow (overrun_intercept_emulate, on by default).", when=DOOM_GAMES),
    S("overrunPlayeringame", "Emulate Playeringame Overflow", "bool", True, "Emulates vanilla's playeringame overflow (overrun_playeringame_emulate, on by default).", when=DOOM_GAMES),
    S("overrunDonut", "Emulate Donut Overflow", "bool", False, "Emulates vanilla's donut overflow (overrun_donut_emulate, off by default).", when=DOOM_GAMES),
    S("overrunMissedBackside", "Emulate Missed Backside Overflow", "bool", False, "Emulates vanilla's missed backside overflow (overrun_missedbackside_emulate, off by default).", when=DOOM_GAMES),
]


def controllers():
    """the driver's controllers, compiled from dsda-input.c"""
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "dump-input")
        subprocess.run(["cc", "-O2", "-o", exe, os.path.join(HERE, "tools", "dump-input.c"), os.path.join(HERE, "dsda-input.c")], check=True)
        return json.loads(subprocess.run([exe], check=True, capture_output=True, text=True).stdout)


def declaration():
    base = json.load(open(os.path.join(HERE, "waterbox-base.json")), object_pairs_hook=collections.OrderedDict)
    ctl = controllers()
    names = {"doom": "Doom Controller", "heretic": "Heretic Controller", "hexen": "Hexen Controller"}
    out = collections.OrderedDict()
    for k, v in base.items():
        if k == "input":
            out["machineSetting"] = "game"
            out["versionSetting"] = "version"
            out["machines"] = []
            for g, label, fmt in GAMES:
                vs = [v.id for v in VERSIONS if v.game == g]
                out["machines"].append(collections.OrderedDict([
                    ("id", "Doom"), ("label", label), ("when", [g]),
                    ("settingOverrides", {"version": {"options": vs}}),
                    ("input", collections.OrderedDict([("name", names[fmt]), ("_comment", v["_comment"]),
                                                       ("buttons", ctl[fmt]["buttons"]), ("axes", ctl[fmt]["axes"])])),
                ]))
            out["_machines_note"] = ("One machine a game, all of them BizHawk's Doom system; each narrows the version setting "
                                     "to its IWAD's releases (Chimera's Version), and has its game's controller.")
            continue
        out[k] = v
    out["settings"] = SETTINGS
    fw = []
    for v in VERSIONS:
        e = collections.OrderedDict([("id", v.iwad), ("display", "%s IWAD" % v.label),
            ("description", "%s of %s: the game. Yours to supply - the package carries none of the game's data. "
             "A file of your own (a modified IWAD) may take its place: the project pins its hash." % (v.iwad.upper(), v.label))])
        if v.size: e["size"] = v.size
        e["sha1"] = v.sha1
        e["name"] = v.iwad.upper() if v.game not in ("freedoom1", "freedoom2") else v.iwad
        e["requiredWhen"] = {"setting": "version", "is": v.id}
        fw.append(e)
    out["firmware"] = fw
    return out


def versions_header():
    fmt = {g: f for g, _, f in GAMES}
    lines = ["/* dsda-versions.h - generated by gen-declaration.py: the IWAD releases the core",
             " * knows (the version setting's values), each with its game (the machine), the",
             " * name its IWAD is mounted under (its firmware id: upstream tells the game and",
             " * its mission by it), its controller, and whether its maps come in episodes",
             " * (-warp takes an episode then). The first is the default. */",
             "#ifndef DSDA_VERSIONS_H", "#define DSDA_VERSIONS_H", "", '#include "dsda-input.h"', "",
             "struct dsda_version", "{", "\tconst char *id;", "\tconst char *game;", "\tconst char *iwad;",
             "\tint format;", "\tint episodic;", "};", "", "static const struct dsda_version k_versions[] = {"]
    for v in VERSIONS:
        lines.append('\t{ "%s", "%s", "%s", FORMAT_%s, %d },' % (v.id, v.game, v.iwad, fmt[v.game].upper(), v.episodic))
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


SLOTS = collections.OrderedDict([
    ("_comment", "A Doom project picks its game and the release of its IWAD (Chimera's System and Version), whose IWAD is the firmware; its PWADs and DeHackEd patches come here, in the order the engine loads them (-file, then -deh)."),
    ("slots", [collections.OrderedDict([
        ("id", "pwad"), ("title", "PWADs and patches"), ("min", 0), ("max", -1), ("formats", ["wad", "deh", "bex"]),
        ("help", "The mod's WADs (.wad) and DeHackEd patches (.deh, .bex), in load order - a later WAD's lumps win over an earlier's. None for the game itself.")])]),
])


# BizHawk's default bindings for its Doom controller (Assets/defctrl.json),
# on each game's controller as far as it has the button
BINDS = {"P1 Fire": "WMouse L", "P1 Use": "Space", "P1 Forward": "W", "P1 Backward": "S", "P1 Turn Left": "E",
         "P1 Turn Right": "Q", "P1 Strafe Left": "A", "P1 Strafe Right": "D", "P1 Weapon Select 1": "Number1",
         "P1 Weapon Select 2": "Number2", "P1 Weapon Select 3": "Number3", "P1 Weapon Select 4": "Number4",
         "P1 Weapon Select 5": "Number5", "P1 Weapon Select 6": "Number6", "P1 Weapon Select 7": "Number7",
         "P1 Inventory Left": "LeftBracket", "P1 Inventory Right": "RightBracket", "P1 Use Artifact": "Enter",
         "P1 Look Up": "PageDown", "P1 Look Down": "Delete", "P1 Look Center": "End", "P1 Fly Up": "PageUp",
         "P1 Fly Down": "Insert", "P1 Fly Center": "Home", "P1 Jump": "Slash", "Change Gamma": "F11",
         "Automap Toggle": "Tab", "Automap +": "KeypadAdd", "Automap -": "KeypadSubtract", "Automap Full/Zoom": "Keypad0",
         "Automap Follow": "F", "Automap Up": "Up", "Automap Down": "Down", "Automap Right": "Right", "Automap Left": "Left",
         "Automap Grid": "G", "Automap Mark": "M", "Automap Clear Marks": "C", "P1 Pause": "Pause"}
ANALOG = {"P1 Mouse Run": {"Value": "RMouse Y", "Mult": 1.0, "Deadzone": 0.0},
          "P1 Mouse Turn": {"Value": "RMouse X", "Mult": 1.0, "Deadzone": 0.0}}


def keybinds():
    ctl = controllers()
    names = {"doom": "Doom Controller", "heretic": "Heretic Controller", "hexen": "Hexen Controller"}
    out = collections.OrderedDict([("_comment", ["BizHawk's default bindings for its DSDA core's controller (Assets/defctrl.json): WASD, "
        "the mouse to fire and to turn and run, E and Q to turn, the number keys for the weapons, the Raven games' inventory, look and "
        "fly keys, Tab and the keypad for the automap, F11 for the gamma. Each game's controller has them as far as it has the button."])])
    out["AllTrollers"] = collections.OrderedDict((names[f], collections.OrderedDict((k, v) for k, v in BINDS.items() if k in ctl[f]["buttons"])) for f in names)
    out["AllTrollersAutoFire"] = collections.OrderedDict((names[f], {}) for f in names)
    out["AllTrollersAnalog"] = collections.OrderedDict((names[f], collections.OrderedDict((k, v) for k, v in ANALOG.items() if k in [a["name"] for a in ctl[f]["axes"]])) for f in names)
    return out


def main():
    outputs = {
        "default_keybinds.json": json.dumps(keybinds(), indent=2) + "\n",
        "waterbox.config": json.dumps(declaration(), indent=2, ensure_ascii=False) + "\n",
        "file_slots.json": json.dumps(SLOTS, indent=2, ensure_ascii=False) + "\n",
        "dsda-versions.h": versions_header(),
    }
    stale = []
    for name, text in outputs.items():
        path = os.path.join(HERE, name)
        old = open(path).read() if os.path.exists(path) else None
        if old != text:
            stale.append(name)
            if "--check" not in sys.argv:
                open(path, "w").write(text)
    if "--check" in sys.argv:
        if stale: print("out of date: " + ", ".join(stale)); sys.exit(1)
        print("the declaration is up to date")
    else:
        print("wrote " + (", ".join(stale) if stale else "nothing (up to date)"))


if __name__ == "__main__":
    main()
