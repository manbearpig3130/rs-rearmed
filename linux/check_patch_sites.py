#!/usr/bin/env python3
"""Where a 5-byte jump can be written into the Mac game's code: linux/patch-sites.tsv, made and checked from the image.

    python3 linux/check_patch_sites.py            check the table against the image
    python3 linux/check_patch_sites.py --write    write the table

The x86-64 Linux build runs the game's own code and puts a jump at each hook point and mid point (linux/src/patch.c),
moving the instructions the jump covers. That is safe when no branch of the function lands inside the covered
bytes after the first, and the covered bytes do not run past the function's end. Neither can be told from the
bytes at the site, so this reads the whole function with the recompiler's disassembler (capstone, as the build's
requirements.txt has it) and writes down, for every site: its address, how many bytes a jump covers, and yes or no.
The table holds addresses and counts, none of the game's bytes.

Needs the decrypted image ($RC_IMAGE or build/steamless) and capstone.
"""
import bisect
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
TABLE = os.path.join(HERE, "patch-sites.tsv")
IMAGE = os.environ.get("RC_IMAGE") or os.path.join(ROOT, "build", "steamless", "Rocksmith2014_x64_decrypted")
HEAD = ("# Where the x86-64 Linux build writes a 5-byte jump into the Mac game's code (linux/src/patch.c): each hook point\n"
        "# and mid point of the Mac target's tables, the bytes the jump covers (whole instructions), and whether it can be\n"
        "# written: `no` when a branch of the function lands inside those bytes or they pass the function's end.\n"
        "# Made and checked by linux/check_patch_sites.py from the pinned image. name, address, bytes, yes|no, why.\n")


def sites():
    """[(name, address)]: the hook points, then the mid points that have an address."""
    out, names = [], []
    with open(os.path.join(ROOT, "tools", "targets", "names", "hooks.list"), encoding="utf-8") as f:
        names = [l.split()[0] for l in f if l.strip() and not l.startswith("#")]
    addr = {}
    with open(os.path.join(ROOT, "tools", "targets", "mac-x64", "symbols.map"), encoding="utf-8") as f:
        for l in f:
            p = l.split()
            if len(p) >= 2 and p[1].startswith("0x"):
                addr[p[0]] = int(p[1], 16)
    out += [(n, addr[n]) for n in names if n in addr]
    with open(os.path.join(ROOT, "tools", "targets", "mac-x64", "mids.map"), encoding="utf-8") as f:
        for l in f:
            m = re.match(r"^(\w+)\s+(0x[0-9a-fA-F]+)\s", l)
            if m:
                out.append((m.group(1), int(m.group(2), 16)))
    return out


def analyse(image_path):
    import capstone
    from capstone import x86 as X
    with open(image_path, "rb") as f:
        img = f.read()
    ncmds = struct.unpack_from("<I", img, 16)[0]
    o, text, starts = 32, None, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", img, o)
        if cmd == 0x19 and img[o + 8:o + 24].rstrip(b"\0") == b"__TEXT":
            text = struct.unpack_from("<QQQQ", img, o + 24)  # vmaddr, vmsize, fileoff, filesize
        if cmd == 0x26:  # LC_FUNCTION_STARTS
            starts = struct.unpack_from("<II", img, o + 8)
        o += size
    if not text or not starts:
        raise SystemExit("check_patch_sites: the image has no __TEXT or no function starts")
    funcs, p, a = [], starts[0], text[0]
    while p < starts[0] + starts[1]:
        v = shift = 0
        while True:
            b = img[p]
            p += 1
            v |= (b & 0x7f) << shift
            shift += 7
            if not b & 0x80:
                break
        if not v:
            break
        a += v
        funcs.append(a)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    off = lambda addr: addr - text[0] + text[2]
    rows = []
    for name, addr in sites():
        k = bisect.bisect_right(funcs, addr) - 1
        lo, hi = funcs[k], funcs[k + 1] if k + 1 < len(funcs) else addr + 64
        covered = 0
        for ins in md.disasm(img[off(addr):off(addr) + 32], addr):
            covered += ins.size
            if covered >= 5:
                break
        targets = set()
        for ins in md.disasm(img[off(lo):off(hi)], lo):
            if ins.group(X.X86_GRP_JUMP) or ins.group(X.X86_GRP_CALL):
                targets.update(op.imm for op in ins.operands if op.type == X.X86_OP_IMM)
        why = ""
        if any(addr < t < addr + covered for t in targets):
            why = "a branch of the function lands inside"
        elif addr + covered > hi:
            why = "the bytes pass the function's end"
        rows.append((name, addr, covered, "no" if why else "yes", why))
    return rows


def text_of(rows):
    return HEAD + "".join("%s\t0x%x\t%d\t%s\t%s\n" % r for r in rows)


def main(argv):
    if not os.path.exists(IMAGE):
        sys.stderr.write("check_patch_sites: no decrypted image at %s\n" % IMAGE)
        return 2
    try:
        rows = analyse(IMAGE)
    except ImportError:
        sys.stderr.write("check_patch_sites: capstone is needed (requirements.txt)\n")
        return 2
    new = text_of(rows)
    if "--write" in argv:
        with open(TABLE, "w", encoding="utf-8") as f:
            f.write(new)
        print("%d sites, %d a jump cannot be written at -> %s" % (len(rows), sum(1 for r in rows if r[3] == "no"), TABLE))
        return 0
    with open(TABLE, encoding="utf-8") as f:
        old = f.read()
    if old != new:
        sys.stderr.write("check_patch_sites: linux/patch-sites.tsv is not what the image gives: run it with --write and look at the difference\n")
        return 1
    print("patch-sites.tsv agrees with the image: %d sites, %d a jump cannot be written at" % (len(rows), sum(1 for r in rows if r[3] == "no")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
