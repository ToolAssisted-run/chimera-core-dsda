#!/usr/bin/env python3
"""gen-declaration.py - the core's declaration, from one place: the games (the
machines, each with its IWAD as firmware - any release of it), BizHawk's
settings, the controllers (dumped from dsda-input.c, the driver's own lists)
and the import dialog. Writes waterbox.config, default_keybinds.json,
file_slots.json, dsda-games.h (the driver's and the importer's table, with the
IWAD dumps the core has been tested with) and dsda-options.h; the gate checks
they are up to date.

usage: gen-declaration.py [--check | --known-iwads]
"""
import collections, json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- the texts a person reads: every one is a key of languages/en.json
# (chimera docs/translations.md). text() records the key's English and returns
# the key, each of its arguments after it behind a 0x1F; texts_table() is the
# table, the keys the driver's code names (already in it) beside these.
TEXTS = {}


def text(key, english, *args):
    if TEXTS.setdefault(key, english) != english:
        sys.exit("%s has two texts: %r and %r" % (key, TEXTS[key], english))
    return "\x1f".join([key] + [str(a) for a in args])


def texts_table():
    path = os.path.join(HERE, "languages", "en.json")
    table = json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {"texts": {}}
    table["texts"].update(TEXTS)
    table["texts"] = dict(sorted(table["texts"].items()))
    return json.dumps(table, indent="\t", ensure_ascii=False) + "\n"

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
# each game's IWAD: the name it is mounted under (its firmware id: upstream tells the game and its mission by it,
# d_main.c AddIWAD), and whether its maps come in episodes (-warp takes an episode then)
IWAD = {"doom2": ("doom2.wad", False), "doom": ("doom.wad", True), "tnt": ("tnt.wad", False),
        "plutonia": ("plutonia.wad", False), "heretic": ("heretic.wad", True), "hexen": ("hexen.wad", False),
        "chex": ("chex.wad", True), "freedoom1": ("freedoom1.wad", True), "freedoom2": ("freedoom2.wad", False)}

# ---- IWAD dumps the core has been tested with: no setting - any release of a game's IWAD is its firmware, the
# project pinning the file's hash, and the compatibility level says the rules. The importer tells a demo's game by
# these hashes first (then by the file's name and lumps), and the gate finds the IWADs of a folder by them.
V = collections.namedtuple("V", "id game label iwad sha1 size episodic")
KNOWN = [
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


def S(name, display, type_, default, description, key=None, args=(), **kw):
    """a setting; its texts are keys setting.<name> and setting.<name>.about
    (key= for settings that share them, their arguments in args)"""
    key = key or "setting." + re.sub(r"[^a-z0-9]+", "_", name.lower())
    s = collections.OrderedDict([("name", name), ("display", text(key, display, *args)), ("type", type_)])
    for k in ("options", "min", "max"):
        if k in kw: s[k] = kw.pop(k)
    s["default"] = default
    s["description"] = text(key + ".about", description, *args)
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
    S("game", "Game", "enum", GAMES[0][0], "Which game the machine is (Chimera's System). It is the game the "
        "IWAD belongs to. It is not shown on the settings page.",
      options=[g for g, _, _ in GAMES]),
    S("compatibilityLevel", "Compatibility Level", "enum", "2 - Doom & Doom 2 v1.9",
      "The version of Doom, or of one of its ports, whose behaviour this "
      "movie follows. The highest level that is compatible with the original"
      " game is 'Final Doom'. Newer WADs may need a higher level. DSDA-Doom "
      "used alone defaults to MBF21, which supports the features of all "
      "lower levels but is the furthest from the original game.",
      options=COMPLEVELS, when=DOOM_GAMES),
    S("skillLevel", "Skill Level", "enum", "4 - Ultra-Violence",
      "The difficulty. The original game defaults to 'Hurt me plenty', but "
      "'Ultra-Violence' is the usual standard today.", options=SKILLS),
    S("multiplayerMode", "Multiplayer Mode", "enum", "Single Player / Cooperative", "Which multiplayer mode the game is played in.",
      options=["Single Player / Cooperative", "Deathmatch", "Alternate Deathmatch (v2.0)"]),
    S("initialEpisode", "Initial Episode", "int", 1, "The episode the game starts in. It is ignored for IWADs without "
        "episodes (for example DOOM2) and for Shareware.", min=0, max=9),
    S("initialMap", "Initial Map", "int", 1, "The map the game starts in.", min=1, max=99),
    S("fastMonsters", "Fast Monsters", "bool", False, "Makes monsters move and attack much faster. It is always on at the "
        "Nightmare! difficulty."),
    S("monstersRespawn", "Monsters Respawn", "bool", False, "Makes monsters come back to life shortly after dying. It is always "
        "on at the Nightmare! difficulty."),
    S("noMonsters", "No Monsters", "bool", False, "Removes all monsters from the level."),
    S("pistolStart", "Pistol Start", "bool", False, "Starts every level from nothing, with no weapons or items kept from"
        " earlier levels. Health is also reset to 100%."),
    S("coopSpawns", "Coop Mode Spawns", "bool", False, "Plays a single-player game with the monsters and items that the "
        "cooperative mode places."),
    S("chainEpisodes", "Chain Episodes", "bool", False, "Finishing one episode leads to the next without a break. The "
        "original game does not have this."),
    S("alwaysRun", "Always Run", "bool", True,
      "Whether the player always runs, with the slower walking speed not "
      "available. This reproduces a bug in the original Doom: setting the "
      "joystick run button to an invalid high number makes the game treat it"
      " as always pressed."),
    S("renderWipescreen", "Render Wipescreen", "bool", True,
      "Turns on the screen melt, the effect shown when Doom changes scene, "
      "for example when a level starts or ends. The original game cannot "
      "turn it off. Its frames are lag frames, in which the game waits."),
    S("turningResolution", "Turning Resolution", "enum", "16 bits (longtics)",
      "'Shorttics' is the reduced turning precision normally used for demos."
      " 'Longtics' is the normal turning precision outside demo recording. "
      "Newer demo formats support both.",
      options=["16 bits (longtics)", "8 bits (shorttics)"]),
    S("mouseTurnSensitivity", "Horizontal Mouse Sensitivity", "int", 10, "How fast the player turns when the mouse is used.", min=0, max=100),
    S("mouseRunSensitivity", "Vertical Mouse Sensitivity", "int", 1, "How fast the player runs when the mouse is used.", min=0, max=100),
    S("strafe50Turns", "Turning During Strafe50", "enum", "Allow",
      "The \"Strafe\" key is needed to turn a turning movement into the "
      "fastest sideways movement (strafe50). Without it the largest sideways"
      " value is 40. So with a keyboard and a mouse it is impossible to turn"
      " during strafe50. If strafe50 together with turning appears in a "
      "demo, the game accepts it, which makes it possible only in a tool-"
      "assisted run. This setting can turn it off for the most authentic "
      "behaviour.",
      options=["Ignore", "Allow"]),
    S("preventLevelExit", "Prevent Level Exit", "bool", False,
      "Level exits have no effect. This is useful for debugging, optimising "
      "and scripted play. The original game does not have this."),
    S("preventGameEnd", "Prevent Game End", "bool", False,
      "The triggers that end the game have no effect. This is useful for "
      "debugging, optimising and scripted play. The original game does not "
      "have this."),
    S("turbo", "Turbo", "int", -1, "Changes the player's running and sideways speed (0 to 255). -1 "
        "means it is off.", min=-1, max=255),
    S("rngSeed", "Initial RNG Seed", "int", 1993, "The starting value (seed) of the game's random number generator. It"
        " is used only for compatibility level 7 (Boom) and above. The "
        "default value is 1993.",
      min=0, max=2147483647, when=DOOM_GAMES),
] + [S("player%dPresent" % p, "Player %1 Present", "bool", p == 1, "Whether player %1 is present.",
                key="setting.player_present", args=(p,)) for p in range(1, 5)] + [
    S("player%dClass" % p, "[Hexen] Player %1 Class", "enum", "Fighter", "The Hexen class of player %1.",
      key="setting.player_class", args=(p,), options=["Fighter", "Cleric", "Mage"], when=["hexen"]) for p in range(1, 5)] + [
    S("scaleFactor", "Internal Resolution Scale Factor", "int", 1,
      "The number the internal resolution is multiplied by (1 to 12). The "
      "picture looks sharper but is less faithful to the original. The "
      "original game's resolution is 320x200, stretched to a 4:3 shape on a "
      "CRT monitor. The core's picture is at most 3840x2400, so the "
      "widescreen shapes go up to 5.",
      min=1, max=12),
    S("internalAspect", "Internal Aspect Ratio", "enum", "Native",
      "The shape of the picture the game draws. 'Native' is a multiple of "
      "320x200, and Chimera stretches it to 4:3, much like the original "
      "game. The other choices draw a picture that already has the right "
      "shape. That is useful for viewing the Automap at higher resolutions, "
      "because it avoids the distorted pixels that stretching afterwards "
      "causes.",
      options=["Native", "16:9", "16:10", "4:3"]),
    S("sfxVolume", "Sfx Volume", "int", 8, "The volume of the sound effects (0 to 15).", min=0, max=15),
    S("musicVolume", "Music Volume", "int", 8, "The volume of the music (0 to 15).", min=0, max=15),
    S("gamma", "Gamma Correction Level", "int", 0,
      "Makes the picture brighter (0 to 4). The original game's default is "
      "\"OFF\" (0). The Change Gamma button steps through the values.", min=0, max=4),
    S("showMessages", "Show Messages", "bool", True, "Shows messages about the items you pick up. The original game's "
        "default is \"ON\"."),
    S("reportSecrets", "Report Revealed Secrets", "bool", False, "Shows a message on screen when a secret is found. The original game"
        " does not have this."),
    S("hudMode", "HUD Mode", "enum", "Vanilla", "Which heads-up display (HUD) is shown.", options=["Vanilla", "DSDA", "None"]),
    S("dsdaExHud", "Extended HUD", "bool", False, "Shows DSDA-Doom's own information above the original heads-up "
        "display. The original game does not have this."),
    S("displayCoordinates", "Display Coordinates", "bool", False,
      "Shows the player's position, angle, speed and distance travelled per "
      "frame. The colour shows the kind of movement: green for SR40, blue "
      "for SR50, red for turbo or wallrun. The original game has this "
      "through the IDMYPOS cheat code, but it shows only the angle, X and Y."),
    S("displayCommands", "Display Commands", "bool", False,
      "Shows the recent inputs on the screen. The last 10 are kept, and "
      "empty inputs are left out. The original game does not have this."),
    S("mapTotals", "Automap Totals", "bool", False, "Shows the counts of kills, items and secrets on the Automap. The "
        "original game does not have this."),
    S("mapTime", "Automap Time", "bool", False, "Shows the elapsed time on the Automap. The original game does not "
        "have this."),
    S("mapCoordinates", "Automap Coordinates", "bool", False, "Shows the coordinates within the level on the Automap. The original"
        " game does not have this."),
    S("mapOverlay", "Automap Overlay", "enum", "Disabled", "Shows the Automap on top of the game view. The original game does "
        "not have this.",
      options=["Disabled", "Enabled", "Dark"]),
    S("mapDetails", "Automap Details", "enum", "Normal", "Shows all lines (linedefs) and things on the Automap. The original "
        "game has this through the IDDT cheat code.",
      options=["Normal", "Linedefs", "Linedefs and things"]),
    S("mapTrail", "Map Trail", "bool", False,
      "Shows the player's earlier positions as a trail when Automap Details "
      "is on. The original game does not have this."),
    S("mapTrailSize", "Map Trail Size", "int", 105, "How many earlier positions are shown as a trail on the Automap (1 "
        "to 350).", min=1, max=350),
    S("fullVision", "Full Vision", "bool", False, "Removes all darkness. The original game has this through the "
        "IDBEHOLDL cheat code."),
    S("displayPlayer", "Player Point of View", "int", 1,
      "Which player's point of view the picture is drawn from (1 to 4). If "
      "that player is not present, the first player who is present is used.", min=1, max=4),
] + [
    # what a demo can dictate beyond BizHawk's settings (the importer,
    # lmp-import.cpp, sets them): its header's option block and DSDA format,
    # its footer's arguments
    S("soloNet", "Solo Net", "bool", False,
      "Plays a single player as a network game (DSDA-Doom's -solo-net "
      "option), which the footer of a demo may ask for. A game with more "
      "than one player is a network game anyway."),
    S("demoOptions", "Boom/MBF Options", "string", "",
      "The game options stored in the header of a demo made for Boom or a "
      "later port, written as hexadecimal. They are the 64 bytes of Boom, "
      "MBF and PrBoom, or the 21 bytes of MBF21 followed by its "
      "compatibility flags. They cover monster memory, friction, pushers, "
      "bobbing, demo insurance, infighting, helper dogs and their distance, "
      "the monsters' behaviour and the compatibility flags. When this is "
      "empty, the defaults of the compatibility level and a PWAD's OPTIONS "
      "lump are used, as DSDA-Doom records them. The monster flags and the "
      "seed are taken from the settings, not from this value.",
      when=DOOM_GAMES),
    S("extendedCommands", "Extended Commands", "enum", "Off",
      "DSDA-Doom's extended commands, which its own DSDA demo format holds: "
      "jumping and free look, and, with the casual features, god mode and "
      "walking through walls. It is off for every other demo format.",
      options=["Off", "On", "On, with casual features"]),
    S("emulatePrBoom", "Emulate PrBoom+ Version", "string", "",
      "Plays the way an older PrBoom+ did (DSDA-Doom's -emulate option, with"
      " a version such as 2.5.0.8), which the footer of a demo may ask for. "
      "When this is empty, the game plays as DSDA-Doom does."),
    S("spechitAddress", "Spechit Overrun Base Address", "int", 0,
      "The memory address that the emulation of the spechit overflow assumes"
      " (DSDA-Doom's -spechit option), which the footer of a demo may give. "
      "0 means the address in doom2.exe (0x01C09C98).",
      min=0, max=2147483647, when=DOOM_GAMES),
    S("overrunSpechit", "Emulate Spechit Overflow", "bool", True, "Reproduces the original game's spechit overflow bug (DSDA-Doom's "
        "overrun_spechit_emulate option, which is on by default).", when=DOOM_GAMES),
    S("overrunReject", "Emulate Reject Overflow", "bool", True, "Reproduces the original game's REJECT overflow bug (the "
        "overrun_reject_emulate option, which is on by default).", when=DOOM_GAMES),
    S("overrunIntercept", "Emulate Intercepts Overflow", "bool", True, "Reproduces the original game's intercepts overflow bug (the "
        "overrun_intercept_emulate option, which is on by default).", when=DOOM_GAMES),
    S("overrunPlayeringame", "Emulate Playeringame Overflow", "bool", True, "Reproduces the original game's playeringame overflow bug (the "
        "overrun_playeringame_emulate option, which is on by default).", when=DOOM_GAMES),
    S("overrunDonut", "Emulate Donut Overflow", "bool", False, "Reproduces the original game's donut overflow bug (the "
        "overrun_donut_emulate option, which is off by default).", when=DOOM_GAMES),
    S("overrunMissedBackside", "Emulate Missed Backside Overflow", "bool", False, "Reproduces the original game's missed backside overflow bug (the "
        "overrun_missedbackside_emulate option, which is off by default).", when=DOOM_GAMES),
]


def controllers():
    """the driver's controllers, compiled from dsda-input.c"""
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "dump-input")
        subprocess.run(["cc", "-O2", "-o", exe, os.path.join(HERE, "tools", "dump-input.c"), os.path.join(HERE, "dsda-input.c")], check=True)
        return json.loads(subprocess.run([exe], check=True, capture_output=True, text=True).stdout)


# the import dialog Chimera draws from the declaration (as it draws the settings):
# ImportMovie's demo, its files (mounted under their names, each option set to
# them) and its options (settings, true when ticked) - wbx-entry.c reads them
MOVIE_IMPORT = collections.OrderedDict([
    ("menu", text("import.menu", "Import Demo (LMP)...")),
    ("movie", collections.OrderedDict([("label", text("import.demo", "Demo")), ("extensions", ["lmp"])])),
    ("files", [
        collections.OrderedDict([("option", "importIwad"), ("label", text("import.iwad", "IWAD")), ("firmware", True), ("required", True),
                                 ("extensions", ["wad"])]),
        collections.OrderedDict([("option", "importPwads"), ("label", text("import.pwads", "PWADs and patches, in load order")), ("slot", "pwad"),
                                 ("multiple", True), ("separator", ";"), ("extensions", ["wad", "deh", "bex"])]),
    ]),
    ("options", [
        collections.OrderedDict([("name", "importNoPwads"), ("display", text("import.nopwads", "No PWADs (an IWAD's own demo)")), ("type", "bool"),
            ("description", text("import.nopwads.about", "Uses none of the PWADs and patches named in the demo's "
                "footer. It is for the IWADs' own demos, which were recorded"
                " with a fix that is now part of the IWAD."))]),
        collections.OrderedDict([("name", "importLongtics"), ("display", text("import.longtics", "Recorded with longtics (Heretic, Hexen)")), ("type", "bool"),
            ("description", text("import.longtics.about", "For a Heretic or Hexen demo that was recorded with "
                "-longtics but whose header does not say so (demos from "
                "Hexen+, before vvHeretic added a header flag). Its turns "
                "are then read as 16-bit values. A demo whose header says so"
                " needs nothing."))]),
        collections.OrderedDict([("name", "importRespawn"), ("display", text("import.respawn", "Monsters respawn (Doom 1.0-1.2, Heretic, Hexen)")), ("type", "bool"),
            ("description", text("import.respawn.about", "For a demo that was recorded with -respawn but whose header"
                " does not say so. Demos of Doom 1.0 to 1.2 hold no monster "
                "flags, and Heretic and Hexen demos hold them only when "
                "DSDA-Doom recorded them. A later Doom demo format holds its"
                " own flags."))]),
        collections.OrderedDict([("name", "importFast"), ("display", text("import.fast", "Fast monsters (Doom 1.0-1.2, Heretic, Hexen)")), ("type", "bool"),
            ("description", text("import.fast.about", "For a demo that was recorded with -fast. The headers of "
                "Doom 1.0 to 1.2, Heretic and Hexen demos do not hold this. "
                "A later Doom demo format holds its own flag."))]),
        collections.OrderedDict([("name", "importNomonsters"), ("display", text("import.nomonsters", "No monsters (Doom 1.0-1.2, Heretic, Hexen)")), ("type", "bool"),
            ("description", text("import.nomonsters.about", "For a demo that was recorded with -nomonsters but whose "
                "header does not say so. Demos of Doom 1.0 to 1.2 hold no "
                "monster flags, and Heretic and Hexen demos hold them only "
                "when DSDA-Doom recorded them. A later Doom demo format "
                "holds its own flags."))]),
    ]),
])


# ---- what the controls and the system are called ----
# The frontend keeps no table of these: a core says what its own are called.
# MNEMONICS is the letter each button writes into a movie's text and heads its
# input column with, by the button's name - whole, or without its player ("P2
# Up" is found under "Up"), so one line serves every pad. AXIS_HEADERS is the
# short header of each axis's column. (An entry is read by position: a letter
# may change and no movie made before it is harmed.)
MNEMONICS = {
    "Fire": "F", "Use": "U", "Forward": "^", "Backward": "v", "Turn Left": "{", "Turn Right": "}",
    "Strafe Left": "<", "Strafe Right": ">", "Run": "R", "Strafe": "S", "Weapon Select 1": "1",
    "Weapon Select 2": "2", "Weapon Select 3": "3", "Weapon Select 4": "4", "Weapon Select 5": "5",
    "Weapon Select 6": "6", "Weapon Select 7": "7", "Pause": "P", "Jump": "J", "God": "G",
    "No Clip": "N", "Change Gamma": "G", "Automap Toggle": "a", "Automap +": "+", "Automap -": "-",
    "Automap Full/Zoom": "z", "Automap Follow": "f", "Automap Up": "u", "Automap Down": "d",
    "Automap Right": "r", "Automap Left": "l", "Automap Grid": "g", "Automap Mark": "m",
    "Automap Clear Marks": "c", "Camera Reset": "R", "Inventory Left": "[", "Inventory Right": "]",
    "Use Artifact": "A", "Look Up": "K", "Look Down": "D", "Look Center": "C", "Fly Up": "u",
    "Fly Down": "p", "Fly Center": "c", "End Player": "E",
}
AXIS_HEADERS = {
    "P1 Run Speed": "Run", "P1 Strafe Speed": "Str", "P1 Turn Speed": "Turn",
    "P1 Turn Speed Frac.": "turn", "P1 Weapon Select": "W", "P1 Mouse Run": "mRun",
    "P1 Mouse Turn": "mTurn", "P1 Free Look": "fLook", "P2 Run Speed": "Run",
    "P2 Strafe Speed": "Str", "P2 Turn Speed": "Turn", "P2 Turn Speed Frac.": "turn",
    "P2 Weapon Select": "W", "P2 Mouse Run": "mRun", "P2 Mouse Turn": "mTurn",
    "P2 Free Look": "fLook", "P3 Run Speed": "Run", "P3 Strafe Speed": "Str",
    "P3 Turn Speed": "Turn", "P3 Turn Speed Frac.": "turn", "P3 Weapon Select": "W",
    "P3 Mouse Run": "mRun", "P3 Mouse Turn": "mTurn", "P3 Free Look": "fLook",
    "P4 Run Speed": "Run", "P4 Strafe Speed": "Str", "P4 Turn Speed": "Turn",
    "P4 Turn Speed Frac.": "turn", "P4 Weapon Select": "W", "P4 Mouse Run": "mRun",
    "P4 Mouse Turn": "mTurn", "P4 Free Look": "fLook", "Camera Mode": "cM",
    "Camera Run Speed": "cRun", "Camera Strafe Speed": "cStr", "Camera Turn Speed": "cTurn",
    "Camera Fly": "cFly", "P1 Look": "Look", "P1 Fly": "Fly", "P1 Artifact": "Art",
    "P1 Special": "Spec", "P2 Look": "Look", "P2 Fly": "Fly", "P2 Artifact": "Art",
    "P2 Special": "Spec", "P3 Look": "Look", "P3 Fly": "Fly", "P3 Artifact": "Art",
    "P3 Special": "Spec", "P4 Look": "Look", "P4 Fly": "Fly", "P4 Artifact": "Art",
    "P4 Special": "Spec",
}
SYSTEM_NAMES = {
    "Doom": "Doom",
}


def _bare(name):
    """A control's name without its player: "P2 Up" -> "Up"."""
    head, _, rest = name.partition(" ")
    return rest if rest and head[:1] == "P" and head[1:].isdigit() else name


def mnemonics_for(buttons):
    """The "mnemonics" of an input declaration: a letter for every one of its
    buttons, and for nothing else. A button nobody gave a letter stops the
    build - the engine would give it its rule's guess, and two columns of one
    pad would share a letter with nobody having decided it."""
    out = {}
    for b in buttons:
        key = b if b in MNEMONICS else _bare(b)
        if key not in MNEMONICS:
            raise SystemExit("no mnemonic for the button %r (MNEMONICS in %s)" % (b, __file__))
        out[key] = MNEMONICS[key]
    return out


def with_headers(axes):
    """The axes with their column headers; an axis nobody named stops the build."""
    missing = [a["name"] for a in axes if a["name"] not in AXIS_HEADERS]
    if missing:
        raise SystemExit("no header for the axes %s (AXIS_HEADERS in %s)" % (missing, __file__))
    return [dict(a, header=AXIS_HEADERS[a["name"]]) for a in axes]


def declaration():
    base = json.load(open(os.path.join(HERE, "waterbox-base.json")), object_pairs_hook=collections.OrderedDict)
    ctl = controllers()
    names = {"doom": "Doom Controller", "heretic": "Heretic Controller", "hexen": "Hexen Controller"}
    out = collections.OrderedDict()
    for k, v in base.items():
        if k == "input":
            out["machineSetting"] = "game"
            out["machines"] = []
            for g, label, fmt in GAMES:
                out["machines"].append(collections.OrderedDict([
                    ("id", "Doom"), ("label", label), ("when", [g]),
                    ("input", collections.OrderedDict([("name", names[fmt]), ("_comment", v["_comment"]),
                                                       ("buttons", ctl[fmt]["buttons"]),
                                                       ("mnemonics", mnemonics_for(ctl[fmt]["buttons"])),
                                                       ("axes", with_headers(ctl[fmt]["axes"]))])),
                ]))
            out["_machines_note"] = ("One machine a game, all of them BizHawk's Doom system, each with its game's controller; "
                                     "the IWAD the project brings (any release) and the compatibility level say the rest.")
            continue
        out[k] = v
    out["systemNames"] = SYSTEM_NAMES
    out["settings"] = SETTINGS
    fw = []
    for g, label, _ in GAMES:
        iwad = IWAD[g][0]
        name = iwad.upper() if g not in ("freedoom1", "freedoom2") else iwad
        e = collections.OrderedDict([("id", iwad), ("display", text("firmware.iwad", "%1 IWAD", label)),
            ("description", text("firmware.iwad.about", "%1 is the game. Any release of it can be used, and so can a modified "
             "IWAD. You have to supply it. The package contains none of the game's data. The project records exactly "
             "which file it was, and the compatibility level decides which release's rules the game follows.", name))])
        e["name"] = name
        e["requiredWhen"] = {"setting": "game", "is": g}
        fw.append(e)
    out["firmware"] = fw
    out["movieImport"] = MOVIE_IMPORT
    return out


def games_header():
    fmt = {g: f for g, _, f in GAMES}
    lines = ["/* dsda-games.h - generated by gen-declaration.py: the games the core plays (the game",
             " * setting's values: the machines), each with the name its IWAD is mounted under (its",
             " * firmware id: upstream tells the game and its mission by it), its controller, and",
             " * whether its maps come in episodes (-warp takes an episode then); then the IWAD dumps",
             " * the core has been tested with, by SHA1 - for telling a demo's game, not a setting.",
             " * The first game is the default. */",
             "#ifndef DSDA_GAMES_H", "#define DSDA_GAMES_H", "", '#include "dsda-input.h"', "",
             "struct dsda_game", "{", "\tconst char *id;", "\tconst char *iwad;", "\tint format;", "\tint episodic;", "};", "",
             "static const struct dsda_game k_games[] = {"]
    for g, _, f in GAMES:
        lines.append('\t{ "%s", "%s", FORMAT_%s, %d },' % (g, IWAD[g][0], f.upper(), IWAD[g][1]))
    lines += ["};", "", "struct dsda_known_iwad", "{", "\tconst char *game;", "\tconst char *label;", "\tconst char *sha1;", "};", "",
              "static const struct dsda_known_iwad k_known_iwads[] = {"]
    for v in KNOWN:
        lines.append('\t{ "%s", "%s", "%s" },' % (v.game, v.label, v.sha1.upper()))
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def options_header():
    """the settings' names and every enum setting's options, for the importer
    (lmp-import.cpp), which writes them as a project's values"""
    c = lambda s: '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    lines = ["/* dsda-options.h - generated by gen-declaration.py: the settings the core",
             " * declares, by name, and each enum setting's options as waterbox.config has",
             " * them - what a project's settings say. Included by lmp-import.cpp. */",
             "#ifndef DSDA_OPTIONS_H", "#define DSDA_OPTIONS_H", "",
             "static const char *const k_setting_names[] = {"]
    lines += ["\t%s," % c(s["name"]) for s in SETTINGS]
    lines += ["\tnullptr", "};", ""]
    for s in SETTINGS:
        if s["type"] == "enum" and s["name"] != "game":
            name = re.sub(r"\d", "", s["name"]) if re.match(r"player\dClass", s["name"]) else s["name"]
            if any(l.startswith("static const char *const k_options_%s[]" % name) for l in lines):
                continue
            lines.append("static const char *const k_options_%s[] = { %s, nullptr };" % (name, ", ".join(c(o) for o in s["options"])))
    lines += ["", "#endif", ""]
    return "\n".join(lines)


SLOTS = collections.OrderedDict([
    ("_comment", "This file lists the kinds of file a project of this core can hold. "
        "Chimera's New Project window is built from it. A Doom project "
        "chooses its game (Chimera's System). The IWAD of that game, in any "
        "release, is the firmware. The project's PWADs and DeHackEd patches "
        "are listed here, in the order the engine loads them (-file first, "
        "then -deh)."),
    ("slots", [collections.OrderedDict([
        ("id", "pwad"), ("title", text("slot.pwad", "PWADs and patches")), ("min", 0), ("max", -1), ("formats", ["wad", "deh", "bex"]),
        ("help", text("slot.pwad.help", "The WADs (.wad) and DeHackEd patches (.deh, .bex) of the mod, "
            "in the order they are loaded. Where two WADs hold the same "
            "lump, the later one wins. Give none to play the game itself."))])]),
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
    if "--known-iwads" in sys.argv:   # for the gate: "<SHA1> <game> <firmware id>" a line
        for v in KNOWN: print(v.sha1.upper(), v.game, v.iwad)
        return
    # the dialog sets only what ImportMovie reads, into a slot the core has
    entry = open(os.path.join(HERE, "wbx-entry.c")).read()
    for name in [f["option"] for f in MOVIE_IMPORT["files"]] + [o["name"] for o in MOVIE_IMPORT["options"]]:
        if '"%s"' % name not in entry:
            sys.exit("movieImport: wbx-entry.c's ImportMovie does not read %s" % name)
    for f in MOVIE_IMPORT["files"]:
        if "slot" in f and f["slot"] not in [slot["id"] for slot in SLOTS["slots"]]:
            sys.exit("movieImport: no file slot %s" % f["slot"])
    outputs = {
        "default_keybinds.json": json.dumps(keybinds(), indent=2) + "\n",
        "waterbox.config": json.dumps(declaration(), indent=2, ensure_ascii=False) + "\n",
        "file_slots.json": json.dumps(SLOTS, indent=2, ensure_ascii=False) + "\n",
        "dsda-games.h": games_header(),
        "dsda-options.h": options_header(),
    }
    outputs["languages/en.json"] = texts_table()
    stale = []
    for name, content in outputs.items():
        path = os.path.join(HERE, name)
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != content:
            stale.append(name)
            if "--check" not in sys.argv:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                open(path, "w", encoding="utf-8").write(content)
    if "--check" in sys.argv:
        if stale: print("out of date: " + ", ".join(stale)); sys.exit(1)
        print("the declaration is up to date")
    else:
        print("wrote " + (", ".join(stale) if stale else "nothing (up to date)"))


if __name__ == "__main__":
    main()
