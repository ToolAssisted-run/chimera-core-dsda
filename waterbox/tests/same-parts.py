#!/usr/bin/env python3
"""same-parts.py - a Chimera project (a zip, since 2026-11-02) holds the two
parts the core's tool wrote: its project.json and its input's lines.

usage: same-parts.py <p.chimeraProject> <project.json> <project.input>
       same-parts.py --as-chimera <p.chimeraProject> <project.json> <project.input> <package sha1>

The first form: the command line's zip holds the tool's parts byte for byte.
The second: Chimera made the zip from the tool's parts (chimera-run
--run-tool), so project.json is Chimera's writing of the same project - every
value the tool wrote, an id Chimera gave it, the core pinned to the package -
and input.input is the tool's lines byte for byte. Exits 1 and says what
differs when they are not the same.
"""
import json
import sys
import zipfile


def main():
    args = sys.argv[1:]
    as_chimera = args[:1] == ["--as-chimera"]
    if as_chimera:
        args = args[1:]
    if len(args) != (4 if as_chimera else 3):
        sys.exit(__doc__)
    with zipfile.ZipFile(args[0]) as z:
        names = sorted(z.namelist())
        doc = z.read("project.json") if "project.json" in names else b""
        lines = z.read("input.input") if "input.input" in names else b""
    want_doc = open(args[1], "rb").read()
    want_lines = open(args[2], "rb").read()
    if names != ["input.input", "project.json"]:
        sys.exit("the project holds %s" % names)
    if lines != want_lines:
        sys.exit("input.input differs from the tool's lines")
    if not as_chimera:
        if doc != want_doc:
            sys.exit("project.json differs from the tool's")
        return
    got, want = json.loads(doc), json.loads(want_doc)
    for key, value in want.items():
        if got.get(key) != value:
            sys.exit("project.json's %s is %s, the tool wrote %s" % (key, json.dumps(got.get(key))[:120], json.dumps(value)[:120]))
    if not got.get("id"):
        sys.exit("project.json has no id")
    if got.get("core", {}).get("sha1") != args[3]:
        sys.exit("the project pins %s, not the package %s" % (got.get("core", {}).get("sha1"), args[3]))


if __name__ == "__main__":
    main()
