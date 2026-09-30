#!/usr/bin/env python3
"""make-rooms.py - the gate's own levels: a PWAD of MAP01-MAP03 (Doom II's
names), each one square room with the player facing an exit switch, and a
vanilla demo (.lmp, 1.9) that plays through them - walk up, press the switch,
and through the intermission by its use presses - so the gate crosses levels
and intermissions on free data.

Each map is the trivial map dsda allows: one sector, one subsector, no nodes
(the blockmap is left for the engine to build, the reject table empty). The
walls are STARTAN3, the floor and ceiling FLOOR4_8 and CEIL3_5 - names Doom II
and Freedoom's Phase 2 both have.

The demo repeats one 200-tic cycle - 60 tics forward, 5 of use, then a use
press every 20 tics - so where a map begins in the cycle does not matter: a
press short of the wall does nothing, and the next cycle's walk ends at it.

usage: make-rooms.py <out.wad> <out.lmp> [<cycles>]
"""
import struct
import sys


def room_map():
    # clockwise, so each wall's right (front) side is inside the room
    verts = [(0, 0), (0, 256), (256, 256), (256, 0)]
    lines = []
    for i in range(4):
        special = 11 if i == 2 else 0  # the east wall: S1 exit switch
        lines.append(struct.pack('<7h', i, (i + 1) % 4, 1, special, 0, i, -1))  # flags 1: impassable
    sides = b''.join(struct.pack('<hh8s8s8sh', 0, 0, b'-', b'-', b'STARTAN3', 0) for _ in range(4))
    sector = struct.pack('<hh8s8shhh', 0, 128, b'FLOOR4_8', b'CEIL3_5', 160, 0, 0)
    vertexes = b''.join(struct.pack('<hh', x, y) for x, y in verts)
    # one seg a wall, in the subsector's order; the angle is the wall's direction
    angles = [0x4000, 0x0000, 0xC000, 0x8000]  # north, east, south, west
    segs = b''.join(struct.pack('<hhhhhh', i, (i + 1) % 4, angles[i] - 0x10000 if angles[i] >= 0x8000 else angles[i], i, 0, 0)
                    for i in range(4))
    ssectors = struct.pack('<hh', 4, 0)
    # the player, at (64, 128) facing east (angle 0), on every skill
    things = struct.pack('<hhhhh', 64, 128, 0, 1, 7)
    return [('THINGS', things), ('LINEDEFS', b''.join(lines)), ('SIDEDEFS', sides), ('VERTEXES', vertexes),
            ('SEGS', segs), ('SSECTORS', ssectors), ('NODES', b''), ('SECTORS', sector), ('REJECT', b''),
            ('BLOCKMAP', b'')]


def wad(lumps):
    data = b''
    directory = b''
    pos = 12
    for name, body in lumps:
        directory += struct.pack('<ii8s', pos, len(body), name.encode())
        data += body
        pos += len(body)
    return b'PWAD' + struct.pack('<ii', len(lumps), pos) + data + directory


def demo(cycles):
    tics = []
    for _ in range(cycles):
        for t in range(200):
            fwd = 50 if t < 60 else 0
            use = 60 <= t < 65 or (t >= 80 and t % 20 < 2)
            tics.append(struct.pack('<bbbB', fwd, 0, 0, 2 if use else 0))
    # 1.9, Ultra-Violence, MAP01, single player
    return bytes([109, 3, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0]) + b''.join(tics) + b'\x80'


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    lumps = []
    for m in (1, 2, 3):
        lumps.append(('MAP%02d' % m, b''))
        lumps += room_map()
    open(sys.argv[1], 'wb').write(wad(lumps))
    open(sys.argv[2], 'wb').write(demo(int(sys.argv[3]) if len(sys.argv) == 4 else 3))
