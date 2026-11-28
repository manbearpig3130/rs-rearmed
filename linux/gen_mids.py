#!/usr/bin/env python3
"""The Mac target's mid points as a C table for the x86-64 Linux build (linux/src/patch.c): each point's address and
the addresses of its continuations, which the runtime's own table (rc_target.c) has by name alone.

    python3 linux/gen_mids.py OUT.inc     (linux/Makefile runs it)

A line of tools/targets/mac-x64/mids.map is `NAME ADDRESS (values) [-> CONT=ADDRESS, ...]`, or `NAME none ...` for a
point this game does not need. A point named in linux/patch-sites.tsv as one a 5-byte jump cannot be written at (a
branch of its function lands inside the jump) is marked, and the build leaves it alone.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAP = os.path.join(HERE, "..", "tools", "targets", "mac-x64", "mids.map")
SITES = os.path.join(HERE, "patch-sites.tsv")
LINE = re.compile(r"^(\w+)\s+(0x[0-9a-fA-F]+)\s*\(([^)]*)\)\s*(?:->\s*([^#]*))?")
MAX_CONTS = 8


def points(text):
    """[(name, address, [continuation addresses])] of the points that have an address, in the file's order."""
    out = []
    for line in text.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        conts = []
        for part in (m.group(4) or "").split(","):
            part = part.strip()
            if part:
                name, _, addr = part.partition("=")
                conts.append(int(addr, 16))
        if len(conts) > MAX_CONTS:
            raise ValueError("%s has more than %d continuations" % (m.group(1), MAX_CONTS))
        out.append((m.group(1), int(m.group(2), 16), conts))
    return out


def unsafe_sites(text):
    """The names patch-sites.tsv marks `no`: name, address, bytes, yes|no, why."""
    names = set()
    for line in text.splitlines():
        p = line.split("\t")
        if len(p) >= 4 and not line.startswith("#") and p[3] == "no":
            names.add(p[0])
    return names


def table(pts, unsafe):
    out = ["// Made by linux/gen_mids.py from tools/targets/mac-x64/mids.map and linux/patch-sites.tsv: edit those.",
           "// MID(name, address, a jump can be written there, the continuations' addresses...)"]
    for name, addr, conts in pts:
        conts = conts + [0] * (MAX_CONTS - len(conts))
        out.append('MID("%s", 0x%xull, %d, %s)' % (name, addr, 0 if name in unsafe else 1, ", ".join("0x%xull" % c for c in conts)))
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    with open(MAP, encoding="utf-8") as f:
        pts = points(f.read())
    unsafe = set()
    if os.path.exists(SITES):
        with open(SITES, encoding="utf-8") as f:
            unsafe = unsafe_sites(f.read())
    with open(argv[1], "w", encoding="utf-8") as f:
        f.write(table(pts, unsafe))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
