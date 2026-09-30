#!/usr/bin/env python3
"""lmp2sol.py - a vanilla Doom demo (.lmp, versions 1.4 to 1.9: 104-109) as
the gate harness's movie, and the settings that play it.

The movie is quickerDSDA's .sol (its lmpConverter's): a line per tic,
|fwd,str,trn,wpn,FAX| for each player in the game, the demo's bytes as they
are - forward and side move, the turn's high byte, and the button byte's bits
2-4 and 5 (BT_CHANGE and the weapon's number, which gate-harness.h turns into
the Weapon Select axis). The settings are the demo header's: the skill, the
map, the monster flags, the players and whose view, and the complevel the
engine gives a demo of that version on that game (G_GetOriginalDoomCompatLevel:
1.4-1.666 are 1, a 1.9 demo is 3 on a game with a fourth episode, 4 on Final
Doom, 2 on the others - Doom II, and Chex Quest's one episode).

The same movie in Chimera's own format (--chimera), for chimera-run: a
line is the console's group - Camera Mode, the camera's four speeds, and its
fourteen buttons - then each present player's: Run, Strafe and Turn Speed,
Weapon Select, Mouse Run and Turn (with shorttics Turn Speed Frac. is not
active and not in the line), then the seventeen buttons, Fire and Use first.

usage: lmp2sol.py <demo.lmp> <out.sol> <out settings> <version id> <game>
       lmp2sol.py --extract <wad> <LUMP> <out.lmp>
       lmp2sol.py --chimera <movie.sol> <out.txt>
"""
import json
import struct
import sys

# the multiplayerMode setting's options (dsda-driver.c's k_multiplayer)
MULTIPLAYER = ['Single Player / Cooperative', 'Deathmatch', 'Alternate Deathmatch (v2.0)']


def extract(wad, lump, out):
    d = open(wad, 'rb').read()
    _, n, o = struct.unpack('<4sii', d[:12])
    for i in range(n):
        p, s, name = struct.unpack('<ii8s', d[o + 16 * i:o + 16 * i + 16])
        if name.rstrip(b'\0').decode('latin1').upper() == lump.upper():
            open(out, 'wb').write(d[p:p + s])
            return
    sys.exit('%s has no lump %s' % (wad, lump))


def complevel_of(demover, game):
    if demover < 107:
        return 1
    if game in ('doom', 'freedoom1'):
        return 3
    if game in ('tnt', 'plutonia'):
        return 4
    return 2


def convert(lmp, sol, settings, version, game):
    d = open(lmp, 'rb').read()
    if not 104 <= d[0] <= 109:
        sys.exit('%s: demo version %d is not vanilla 1.4-1.9 (104-109)' % (lmp, d[0]))
    complevel = complevel_of(d[0], game)
    skill, episode, gamemap, dm, respawn, fast, nomonsters, console = d[1:9]
    present = list(d[9:13])
    players = sum(present)
    pos = 13
    lines = []
    while pos < len(d) and d[pos] != 0x80:
        line = '|'
        for _ in range(players):
            fwd, side, turn = struct.unpack('<bbb', d[pos:pos + 3])
            bits = d[pos + 3]
            pos += 4
            line += '|%4d,%4d,%4d,%4d,%s%s%s' % (fwd, side, turn, (bits >> 2) & 7, 'F' if bits & 1 else '.',
                                              'A' if bits & 2 else '.', 'X' if bits & 32 else '.')
        lines.append(line + '|')
    open(sol, 'w').write('\n'.join(lines) + '\n')
    s = {'version': version, 'compatibilityLevel': str(complevel), 'skillLevel': str(skill + 1),
         'initialEpisode': episode, 'initialMap': gamemap, 'multiplayerMode': MULTIPLAYER[min(dm, 2)],
         'monstersRespawn': bool(respawn), 'fastMonsters': bool(fast), 'noMonsters': bool(nomonsters),
         'displayPlayer': console + 1, 'renderWipescreen': False, 'turningResolution': '8 bits (shorttics)'}
    for i in range(4):
        s['player%dPresent' % (i + 1)] = bool(present[i])
    json.dump(s, open(settings, 'w'))
    return len(lines)


def weapon_axis(wpn, alt):
    """the Weapon Select axis of a .sol weapon field (gate-harness.h's decode)"""
    raw = (wpn & 7) << 2 | alt << 5
    return ((raw >> 3) & 7) + 1 if raw & 4 else 0


def chimera(sol, out):
    lines = []
    for line in open(sol):
        if line.startswith('#'):
            continue
        entry = '|%5d,' % -1 + '%5d,' % 0 * 4 + '.' * 14
        for seg in line.strip().split('|'):
            f = [x.strip() for x in seg.split(',')]
            if len(f) != 5:
                continue
            fwd, side, turn, wpn, fax = int(f[0]), int(f[1]), int(f[2]), int(f[3]), f[4]
            axes = (fwd, side, turn, weapon_axis(wpn, fax[2] == 'X'), 0, 0)
            entry += '|' + ''.join('%5d,' % a for a in axes) + ('F' if fax[0] == 'F' else '.') + \
                ('U' if fax[1] == 'A' else '.') + '.' * 15
        lines.append(entry + '|')
    open(out, 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--extract':
        extract(*sys.argv[2:])
    elif len(sys.argv) == 4 and sys.argv[1] == '--chimera':
        chimera(*sys.argv[2:])
    elif len(sys.argv) == 6:
        print(convert(*sys.argv[1:]))
    else:
        sys.exit(__doc__)
