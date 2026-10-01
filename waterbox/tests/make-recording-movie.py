#!/usr/bin/env python3
"""make-recording-movie.py - a movie in Chimera's format that works every input
a demo can hold, for the gate's format legs to record demos with: the move
and turn axes past what keys make, strafe50 from the keys, longtics'
fractions, every weapon number (the axis and the keys), fire and use, a pause
held and let go, Heretic's and Hexen's look, fly, artifacts, special commands
and inventory, Hexen's jump, and dsda's extended commands (jump, free look, god,
no clip) - each only where the settings give it meaning. The pattern is fixed:
the same settings give the same movie.

The LogKey names every input of the game's controller (waterbox.config's): the
harnesses read a movie's columns by those names, and the driver reads an input
only where the settings make it one (an absent player's, a casual command
without the casual features, are never read).

usage: make-recording-movie.py <settings JSON> <game> <frames> <out.txt> [<waterbox.config>]
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def values_for(t, p, game, s):
    """player p's (1-4) inputs at frame t"""
    t = t + 37 * (p - 1)   # the players apart
    v = {}
    P = "P%d " % p
    v[P + "Run Speed"] = 50 if t % 120 < 60 else (-25 if t % 120 < 70 else (100 if t % 240 == 115 else 0))
    v[P + "Strafe Speed"] = 40 if t % 90 < 15 else (-127 if t % 333 == 12 else 0)
    v[P + "Turn Speed"] = (3 if t % 50 < 10 else -2 if t % 50 < 18 else 0)
    if s.get("turningResolution", "16 bits (longtics)").startswith("16"):
        v[P + "Turn Speed Frac."] = (t * 37) % 256 if t % 7 == 0 else 0
    if 200 <= t % 300 < 230:
        v[P + "Strafe"] = True
        v[P + "Turn Right"] = True    # strafe50
    if t % 97 == 50:
        v[P + "Weapon Select"] = (t // 97) % 9 + 1
    if t % 211 == 100:
        v[P + "Weapon Select 3"] = True
    v[P + "Fire"] = t % 13 < 3
    v[P + "Use"] = t % 41 == 0
    if t in (400, 431):
        v[P + "Pause"] = True
    if game in ("heretic", "hexen"):
        v[P + "Look"] = 2 if t % 50 in (10, 11, 12) else (-8 if t % 150 == 140 else 0)
        v[P + "Fly"] = 3 if t % 70 in (5, 6, 7) else 0
        if t % 150 == 75:
            v[P + "Artifact"] = (t // 150) % (11 if game == "heretic" else 33)
        if t in (650, 700):
            v[P + "Special"] = 3 if t == 650 else 2    # special commands with fire and use bits
        v[P + "Inventory Right"] = t % 177 == 30
        v[P + "Use Artifact"] = t % 177 == 31
        if game == "hexen":
            v[P + "Jump"] = t % 60 == 30
    excmd = {"Off": 0, "On": 1}.get(s.get("extendedCommands", "Off"), 2)
    if excmd:
        if game != "hexen":
            v[P + "Jump"] = t % 80 == 40
        v[P + "Free Look"] = 300 if t % 90 in range(20, 25) else (-32768 if t % 450 == 300 else 0)
        if excmd > 1:
            v[P + "God"] = t in (500, 520)
            v[P + "No Clip"] = t in (600, 640)
    return v


def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    s = json.load(open(sys.argv[1]))
    game, frames, out = sys.argv[2], int(sys.argv[3]), sys.argv[4]
    cfg = json.load(open(sys.argv[5] if len(sys.argv) == 6 else os.path.join(HERE, "..", "waterbox.config")))
    ctl = next(m for m in cfg["machines"] if m["when"][0] == game)["input"]
    port = lambda n: int(re.match(r"P(\d+) ", n).group(1)) if re.match(r"P\d+ ", n) else 0
    groups = [[] for _ in range(5)]
    for a in ctl["axes"]:
        groups[port(a["name"])].append((a["name"], a))
    for b in ctl["buttons"]:
        groups[port(b)].append((b, None))
    present = [s.get("player%dPresent" % p, p == 1) for p in range(1, 5)]
    key = "".join("#" + "".join(n + "|" for n, _ in g) for g in groups)
    lines = []
    for t in range(frames):
        v = {}
        for p in range(1, 5):
            if present[p - 1]:
                v.update(values_for(t, p, game, s))
        line = "|"
        for g in groups:
            for n, a in g:
                if a is not None:
                    x = v.get(n, a.get("neutral", 0))
                    line += "%5d," % max(a["min"], min(a["max"], x))
                else:
                    line += "X" if v.get(n) else "."
            line += "|"
        lines.append(line)
    with open(out, "w") as f:
        f.write("[Input]\nLogKey:" + key + "\n" + "".join(l + "\n" for l in lines) + "[/Input]\n")


if __name__ == "__main__":
    main()
