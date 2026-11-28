#!/usr/bin/env python3
"""Where the x86-64 Linux build can write into the Mac game's code: linux/patch-sites.tsv (a jump at each hook point
and mid point) and linux/patch-edits.tsv (a jump for each edit longer than its range), made and checked from the
image.

    python3 linux/check_patch_sites.py            check the tables against the image
    python3 linux/check_patch_sites.py --write    write the tables

The build runs the game's own code and puts a 5-byte jump at each hook point and mid point, and at each edit whose
new instructions do not fit its range (linux/src/patch.c), moving the instructions the jump covers. That is safe when
nothing in the function goes to an address inside the covered bytes after the first, and the covered bytes do not
run past the function's end. Neither can be told from the bytes at the site, so this reads the whole function as the
recompiler does (tools/ws7/recomp/xlat: its discovery, which follows a switch's table too, with the target's edits
and mid points in, so that an edit's own branches and a mid point's continuations count) and writes down, for each
site, how many bytes a jump covers and yes or no. Where the answer is no, the build uses a one-byte breakpoint
instead, which covers nothing but the instruction itself.

An edit's row also counts the instructions the build has to correct as it moves them (a distance from the
instruction pointer, a branch's or a call's), so the program can tell its own reading of them agrees with this one.

The tables hold addresses and counts, none of the game's bytes. Needs the decrypted image ($RC_IMAGE or
build/steamless), capstone and objdump, as the recompiler's own build does.
"""
import bisect
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
TABLE = os.path.join(HERE, "patch-sites.tsv")
EDITS = os.path.join(HERE, "patch-edits.tsv")
TARGET = "mac-x64"
IMAGE = os.environ.get("RC_IMAGE") or os.path.join(ROOT, "build", "steamless", "Rocksmith2014_x64_decrypted")
HEAD = ("# Where the x86-64 Linux build writes a 5-byte jump into the Mac game's code (linux/src/patch.c): each hook point\n"
        "# and mid point of the Mac target's tables, the bytes the jump covers (whole instructions), and whether it can be\n"
        "# written: `no` when something in the function goes to an address inside those bytes or they pass the function's\n"
        "# end (the build then uses a breakpoint there).\n"
        "# Made and checked by linux/check_patch_sites.py from the pinned image. name, address, bytes, yes|no, why.\n")
EDITS_HEAD = ("# The edits of the Mac target's edits.map whose new instructions are longer than their range: the x86-64 Linux\n"
              "# build writes a jump at the range to the new instructions (linux/src/patch.c), which covers the range and,\n"
              "# where that is under 5 bytes, the instructions after it, moved. `no` when something in the function goes to\n"
              "# an address inside the covered bytes or they pass the function's end: a breakpoint then, covering the range\n"
              "# alone. fixes: the new and the moved instructions that hold a distance the build corrects.\n"
              "# Made and checked by linux/check_patch_sites.py from the pinned image.\n"
              "# address, range, new, covered, fixes, yes|no, why.\n")
JUMP = 5


class Game:
    """The image as the recompiler reads it, with the target's tables."""

    def __init__(self, image_path):
        sys.path.insert(0, os.path.join(ROOT, "tools", "targets"))
        sys.path.insert(0, os.path.join(ROOT, "tools", "ws7", "recomp"))
        import targets
        from capstone import x86
        from xlat.core import Translator
        self.x86 = x86
        self.target = targets.Target(TARGET)
        self.lists = targets.name_lists()
        self.tr = Translator(image_path)
        self.tr.set_inner(*targets.inner_points(self.target, self.lists))
        self.edit_bytes = targets.edit_bytes
        self._labels = {}

    def function(self, addr):
        """(start, end) of the function ADDR is in."""
        f = self.tr.fsorted
        k = bisect.bisect_right(f, addr) - 1
        return f[k], f[k + 1] if k + 1 < len(f) else self.tr.text_hi

    def labels(self, addr):
        """Every address something in ADDR's function goes to: its branches and its switches' cases, as the game
        has them and as its edits make them. (Not where an edit's range ends or a mid point's continuation is: the
        program sends those to the moved instruction itself.)"""
        lo, _ = self.function(addr)
        if lo not in self._labels:
            tr = self.tr
            with_tables = tr.discover(lo)[1]
            edits, mids = tr.edits, tr.mids
            tr.edits, tr.mids = {}, {}
            try:
                game = tr.discover(lo)[1]
            finally:
                tr.edits, tr.mids = edits, mids
            ours = {a + e[1] for a, e in edits.items()} | {c for m in mids.values() for c in m[1]}
            self._labels[lo] = game | (with_tables - ours)
        return self._labels[lo]

    def bytes_at(self, addr, n):
        return self.tr.image_bytes(addr, n)

    def corrected(self, ins):
        """Whether moving INS means correcting a distance in it."""
        X = self.x86
        return (X.X86_GRP_JUMP in ins.groups or X.X86_GRP_CALL in ins.groups) and ins.operands[0].type == X.X86_OP_IMM \
            or any(op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP for op in ins.operands)


def sites(game):
    """[(name, address)]: the hook points, then the mid points that have an address."""
    t, out = game.target, []
    out += [(n, t.symbols[n][0] if isinstance(t.symbols[n], tuple) else t.symbols[n]) for n in game.lists["hooks"] if n in t.symbols]
    out += [(name, a) for name, (a, _, _) in t.mids.items() if a is not None]
    return out


def why_not(game, addr, covered):
    lo, hi = game.function(addr)
    if any(addr < x < addr + covered for x in game.labels(addr)):
        return "something in the function goes inside"
    if addr + covered > hi:
        return "the bytes pass the function's end"
    return ""


def site_rows(game):
    rows = []
    for name, addr in sites(game):
        covered = 0
        for ins in game.tr.md.disasm(game.bytes_at(addr, 32), addr):
            covered += ins.size
            if covered >= JUMP:
                break
        why = why_not(game, addr, covered)
        rows.append((name, addr, covered, "no" if why else "yes", why))
    return rows


def edit_rows(game, site_list):
    """The longer edits' rows. The code is edited as the program edits it, with every group on and from the highest
    address down, so that a jump which needs the instructions after its range finds them as they will be: the
    game's, another edit's bytes, or another edit's jump."""
    tr = game.tr
    edits = sorted(game.target.edits, key=lambda e: -e[1])
    code = {}   # address -> byte, where an edit has written

    def read(addr, n):
        b = bytearray(game.bytes_at(addr, n))
        for k in range(n):
            if addr + k in code:
                b[k] = code[addr + k]
        return bytes(b)

    def write(addr, data):
        for k, v in enumerate(data):
            code[addr + k] = v

    rows, spans = [], {}
    for group, addr, n, check, patches, where in edits:
        whole = patches[0][0] is None
        new = game.edit_bytes(game.bytes_at(addr, n), patches)
        if not whole or len(new) <= n:
            write(addr, new + b"\x90" * (n - len(new)))
            spans[addr] = (addr + n, group, where)
            continue
        news = list(tr.md.disasm(new, addr))
        fixes, covered = sum(1 for i in news if game.corrected(i)), n
        moved = []
        while covered < JUMP:
            ins = next(tr.md.disasm(read(addr + covered, 16), addr + covered))
            moved.append(ins)
            covered += ins.size
        why = why_not(game, addr, covered)
        for a2, (end2, group2, where2) in spans.items():
            if addr + n <= a2 < addr + covered and group2 != group:
                raise SystemExit("check_patch_sites: %s: the jump there would cover an edit of another group (%s), which "
                                 "the build does not do" % (where, where2))
        if why:
            covered = n   # a breakpoint: the range alone
        else:
            fixes += sum(1 for i in moved if game.corrected(i))
        write(addr, b"\xe9\0\0\0\0" + b"\x90" * (covered - JUMP) if not why else b"\xcc" + b"\x90" * (n - 1))
        spans[addr] = (addr + covered, group, where)
        rows.append((addr, n, len(new), covered, fixes, "no" if why else "yes", why))
    # A hook point's or a mid point's jump and an edit's: the program does the edits first, so a site among an
    # edit's bytes moves what the edit left. One form it does not do: a jump of a longer edit beginning inside a
    # site's covered bytes.
    longer = {r[0] for r in rows}
    for name, addr, covered, _, _ in site_list:
        for a2 in longer:
            if addr < a2 < addr + covered:
                raise SystemExit("check_patch_sites: %s: the edit at 0x%x has its jump inside this site's covered bytes, "
                                 "which the build does not do" % (name, a2))
    return sorted(rows)


def text_of(rows):
    return HEAD + "".join("%s\t0x%x\t%d\t%s\t%s\n" % r for r in rows)


def edits_text_of(rows):
    return EDITS_HEAD + "".join("0x%x\t%d\t%d\t%d\t%d\t%s\t%s\n" % r for r in rows)


def main(argv):
    if not os.path.exists(IMAGE):
        sys.stderr.write("check_patch_sites: no decrypted image at %s\n" % IMAGE)
        return 2
    try:
        game = Game(IMAGE)
    except ImportError:
        sys.stderr.write("check_patch_sites: capstone is needed (requirements.txt)\n")
        return 2
    rows = site_rows(game)
    new = {TABLE: text_of(rows), EDITS: edits_text_of(edit_rows(game, rows))}
    said = "%d sites, %d a jump cannot be written at; %d longer edits, %d of them" % (
        len(rows), sum(1 for r in rows if r[3] == "no"), new[EDITS].count("\n") - EDITS_HEAD.count("\n"),
        new[EDITS].count("\tno\t"))
    if "--write" in argv:
        for path, text in new.items():
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        print(said)
        return 0
    bad = 0
    for path, text in new.items():
        try:
            with open(path, encoding="utf-8") as f:
                old = f.read()
        except OSError:
            old = None
        if old != text:
            sys.stderr.write("check_patch_sites: linux/%s is not what the image gives: run it with --write and look at the "
                             "difference\n" % os.path.basename(path))
            bad = 1
    if not bad:
        print("patch-sites.tsv and patch-edits.tsv agree with the image: " + said)
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
