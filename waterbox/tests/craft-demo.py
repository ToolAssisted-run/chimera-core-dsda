#!/usr/bin/env python3
"""craft-demo.py - the demo formats and contents dsda-doom plays and does not
record, made from ones it records, for the gate's format legs: the engine
plays the crafted demo itself (-playdemo), and the import of it must play the
same. What each makes:

  doom12      a 1.9 demo's tics under a 1.2 header (no version byte: skill,
              episode, map, the four players)
  doom14      a 1.666 demo as 1.4 (104) and doom15 as 1.5 (105)
  lxdoom      a Boom 2.02 demo as LxDoom's (203, the Boom signature, no
              compatibility byte)
  boom200     a Boom 2.02 demo as 2.00 (its 256 bytes of options)
  options     a Boom-or-later demo's options changed: demo insurance, no
              infighting, weapon recoil, no bobbing, helper dogs, comp flags
              (MBF and later), and in MBF21's 23 comp flags (an older dsda's)
  footer      a footer of the arguments dsda reads: -complevel, -solo-net,
              -coop_spawns, -chain_episodes, an overflow's -set, -emulate,
              -spechit
  markers     the join marker (bit 6 without a weapon change) and save-game
              specials on some tics, which dsda's G_Ticker ignores
  umapinfo    PrBoom+um's UMAPINFO header before a demo

and what the importer must refuse:

  players5    a Boom-or-later demo with a fifth player
  keyframe    a DSDA demo flagged as starting from a key frame
  excmdsave   a DSDA demo whose first tic saves a game (a casual command)
  badversion  a version byte no format has

usage: craft-demo.py <kind> <in.lmp> <out.lmp>
"""
import struct
import sys


def body_start(d):
    """where a demo's tics start, and its header facts: (version, start,
    options offset, options size, players)"""
    v = d[0]
    if 104 <= v <= 111:
        return v, 13, None, 0, list(d[9:13])
    if v in (200, 201, 202):
        return v, 7 + 1 + 5 + 64 + (192 if v == 200 else 0) + 32, 13, 64, list(d[13 + 64:13 + 64 + 4])
    if v == 203 or 210 <= v <= 214:
        # MBF's signature, its compatibility byte
        return v, 7 + 1 + 5 + 64 + 32, 13, 64, list(d[13 + 64:13 + 64 + 4])
    if v == 221:
        size = 21 + d[12 + 20]
        return v, 7 + 5 + size + 32, 12, size, list(d[12 + size:12 + size + 4])
    raise SystemExit("not a demo craft-demo.py knows (version %d)" % v)


def split_marker(d, start, bpt):
    p = start
    while d[p] != 0x80:
        p += bpt
    return d[start:p], d[p:]          # the tics, the marker and what follows


def footer(cmdline):
    """a demo footer: dsda's WAD of PORTNAME and CMDLINE"""
    lumps = [(b"PORTNAME", b"chimera gate\0"), (b"CMDLINE", cmdline.encode() + b"\0")]
    data = b""
    entries = []
    pos = 12
    for name, body in lumps:
        entries.append(struct.pack("<ii8s", pos, len(body), name))
        data += body
        pos += len(body)
    return b"PWAD" + struct.pack("<ii", len(lumps), pos) + data + b"".join(entries)


def craft(kind, d):
    if kind == "keyframe":
        assert d[0] == 255 and d[2:6] == b"DSDA", "keyframe is made from a DSDA demo"
        return d[:16] + bytes([d[16] | 0x01]) + d[17:]
    if kind == "excmdsave":
        assert d[0] == 255 and d[2:6] == b"DSDA", "excmdsave is made from a DSDA demo"
        inner = d[18:]
        v, start, _, _, players = body_start(inner)
        bpt = 5 if v in (111, 214, 221) else 4
        at = 18 + start + bpt           # player one's first extended command byte
        return d[:at] + bytes([d[at] | 0x02, 0]) + d[at + 1:]
    if kind == "badversion":
        return bytes([150]) + d[1:]
    v, start, opt_at, opt_size, players = body_start(d)
    if kind == "players5":
        assert opt_at is not None, "players5 is made from a Boom-or-later demo"
        at = opt_at + opt_size + (192 if v == 200 else 0) + 4
        return d[:at] + b"\x01" + d[at + 1:]
    n = sum(1 for x in players if x)
    longtics = v in (111, 214, 221)
    bpt = (5 if longtics else 4) * n
    if kind == "doom12":
        assert v == 109, "doom12 is made from a 1.9 demo"
        tics, rest = split_marker(d, start, bpt)
        return bytes([d[1], d[2], d[3]]) + d[9:13] + tics + b"\x80"
    if kind in ("doom14", "doom15"):
        assert v == 106, "doom14/15 are made from a 1.666 demo"
        return bytes([104 if kind == "doom14" else 105]) + d[1:]
    if kind == "lxdoom":
        assert v == 202, "lxdoom is made from a Boom 2.02 demo"
        return bytes([203]) + d[1:7] + d[8:]      # the signature stays Boom's; no compatibility byte
    if kind == "boom200":
        assert v == 202, "boom200 is made from a Boom 2.02 demo"
        return bytes([200]) + d[1:opt_at + 64] + bytes(192) + d[opt_at + 64:]
    if kind == "options":
        o = bytearray(d[opt_at:opt_at + opt_size])
        if v == 221:
            o[1] = 1          # weapon recoil
            o[2] = 0          # no bobbing
            o[10] = 0         # no infighting
            o[11] = 2         # two helper dogs
            o[21 + 0] = 1     # comp_telefrag
            o[21 + 3] = 1     # comp_pain
            # an older dsda's 23 comp flags
            count = 23
            o[20] = count
            o = o[:21 + count]
        else:
            o[2] = 1          # weapon recoil
            o[5] = 0          # no bobbing
            o[9] = 1          # demo insurance
            if v >= 203:
                o[14] = 0     # no infighting
                o[15] = 1     # helper dogs
                o[26 + 0] = 1  # comp_telefrag
                o[26 + 2] = 1  # comp_vile
                o[26 + 3] = 1  # comp_pain
        return d[:opt_at] + bytes(o) + d[opt_at + opt_size:]
    if kind == "footer":
        tics, rest = split_marker(d, start, bpt)
        args = ("-iwad \"freedoom2.wad\" -solo-net -coop_spawns -chain_episodes -set overrun_spechit_emulate = 0 "
                "-set overrun_donut_emulate = 1 -emulate 2.5.0.8 -spechit 29400000")
        if 104 <= v <= 111:
            args = "-complevel 3 " + args
        return d[:start] + tics + b"\x80" + footer(args)
    if kind == "markers":
        tics, rest = split_marker(d, start, bpt)
        t = bytearray(tics)
        step = bpt // n
        bi = step - 1                      # the buttons byte, the tic's last for Doom
        for k in range(0, len(t) // step, 37):
            at = k * step + bi
            if t[at] & 0x84 == 0:
                t[at] |= 0x40              # the join marker
        for k in range(17, len(t) // step, 101):
            t[k * step + bi] = 0x80 | 0x02  # a save-game special (dsda: nothing)
        return d[:start] + bytes(t) + rest
    if kind == "umapinfo":
        return bytes([255]) + b"PR+UM\0" + bytes([1, 1, 0, 8]) + b"UMAPINFO" + b"MAP01\0\0\0" + d
    raise SystemExit("no craft %s" % kind)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    data = open(sys.argv[2], "rb").read()
    open(sys.argv[3], "wb").write(craft(sys.argv[1], data))
