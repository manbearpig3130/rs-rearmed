#!/usr/bin/env python3
"""The Mac target's edits as a C table for the x86-64 Linux build (linux/src/patch.c), which writes them into the
game's code when a mod switches their group on.

    python3 linux/gen_edits.py OUT.inc     (linux/Makefile runs it)

A line of tools/targets/mac-x64/edits.map is `GROUP ADDRESS LEN:CHECK EDIT`: bytes to change inside the range, or
instructions that replace it. The table holds none of the game's bytes, and neither does this one: the check is of
the bytes the program finds there. An edit whose instructions are longer than its range has a row in
linux/patch-edits.tsv (check_patch_sites.py, from the image): how many bytes its jump covers, how many
instructions the program corrects as it moves them, and whether a jump can be written there.

The rows are in the order the program takes them, from the highest address down.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
ROWS = os.path.join(HERE, "patch-edits.tsv")
TARGET = "mac-x64"


def longer_rows(text):
    """{address: (range, new, covered, fixes, a jump can be written)} of patch-edits.tsv."""
    out = {}
    for line in text.splitlines():
        p = line.split("\t")
        if line.startswith("#") or len(p) < 6:
            continue
        out[int(p[0], 16)] = (int(p[1]), int(p[2]), int(p[3]), int(p[4]), p[5] == "yes")
    return out


def table(edits, rows):
    """EDITS: the target's (group, address, length, check, patches, where)."""
    out = ["// Made by linux/gen_edits.py from tools/targets/mac-x64/edits.map and linux/patch-edits.tsv: edit those.",
           "// BYTES(n, ...): the edit's own bytes. A whole range's new instructions; or, for changes inside the range,",
           "//   each as its offset, its length and its bytes.",
           "// EDIT(group, address, range, check, whole range, bytes a jump covers, corrected instructions, a jump can be",
           "//   written, n)"]
    seen = set()
    for k, (group, addr, n, check, patches, where) in enumerate(sorted(edits, key=lambda e: -e[1])):
        whole = patches[0][0] is None
        if whole:
            data = patches[0][1]
        else:
            data = b"".join(bytes([off, len(b)]) + b for off, b in patches)
        covered, fixes, can = n, 0, True
        if whole and len(data) > n:
            if addr not in rows:
                raise ValueError("%s: its new instructions are longer than its range and linux/patch-edits.tsv has no row "
                                 "for it: run linux/check_patch_sites.py --write" % where)
            r = rows[addr]
            if (r[0], r[1]) != (n, len(data)):
                raise ValueError("%s: linux/patch-edits.tsv was made for another edit there: run linux/check_patch_sites.py "
                                 "--write" % where)
            covered, fixes, can = r[2], r[3], r[4]
            seen.add(addr)
        out.append("BYTES(%d, %s)" % (k, ", ".join("0x%02x" % b for b in data)))
        out.append('EDIT("%s", 0x%xull, %d, 0x%su, %d, %d, %d, %d, %d)' % (group, addr, n, check, whole, covered, fixes, can, k))
    if set(rows) - seen:
        raise ValueError("linux/patch-edits.tsv has rows for edits edits.map does not have (0x%x): run "
                         "linux/check_patch_sites.py --write" % min(set(rows) - seen))
    return "\n".join(out) + "\n"


def made():
    sys.path.insert(0, os.path.join(ROOT, "tools", "targets"))
    import targets
    with open(ROWS, encoding="utf-8") as f:
        rows = longer_rows(f.read())
    return table(targets.Target(TARGET).edits, rows)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    try:
        text = made()
    except ValueError as e:
        sys.stderr.write("gen_edits: %s\n" % e)
        return 1
    with open(argv[1], "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
