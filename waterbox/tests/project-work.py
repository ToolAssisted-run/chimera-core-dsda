#!/usr/bin/env python3
"""project-work.py - a Chimera project as the harnesses' work folder: its
settings, its firmware under its id and its slot's files (found by their
hashes among the files and folders given), dsda-doom.wad, the slot map, and its
input log as movie.txt. Prints the movie's frame count.

usage: project-work.py <p.chimeraProject> <work dir> <dsda-doom.wad> <file or folder>...
"""
import hashlib
import json
import os
import shutil
import sys


def sha1(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest().upper()


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    project, work, wad, folders = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    p = json.load(open(project))
    by_sha1 = {}
    for d in folders:
        paths = [d] if os.path.isfile(d) else [os.path.join(d, f) for f in sorted(os.listdir(d))]
        for path in paths:
            if os.path.isfile(path) and os.path.getsize(path) < 1 << 30:
                by_sha1.setdefault(sha1(path), path)
    os.makedirs(work, exist_ok=True)
    for fw in p["firmware"]:
        if fw["sha1"] not in by_sha1:
            sys.exit("%s: the firmware %s (%s) is in none of the folders" % (project, fw["id"], fw["sha1"]))
        shutil.copy(by_sha1[fw["sha1"]], os.path.join(work, fw["id"]))
    names = []
    for f in p["files"]:
        if f["sha1"] not in by_sha1:
            sys.exit("%s: %s (%s) is in none of the folders" % (project, f["name"], f["sha1"]))
        shutil.copy(by_sha1[f["sha1"]], os.path.join(work, f["name"]))
        names.append(f["name"])
    shutil.copy(wad, os.path.join(work, "dsda-doom.wad"))
    json.dump(p["settings"], open(os.path.join(work, "settings"), "w"))
    json.dump({"pwad": names}, open(os.path.join(work, "slots"), "w"))
    with open(os.path.join(work, "movie.txt"), "w") as f:
        f.write(p["input"])
    print(sum(1 for line in p["input"].splitlines() if line.startswith("|")))


if __name__ == "__main__":
    main()
