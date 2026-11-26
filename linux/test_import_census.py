#!/usr/bin/env python3
"""linux/import_census.py on a made-up Mach-O (no game bytes), and the table as it is in the repository. The table is
checked against the real image when there is one at $RC_IMAGE or build/steamless, and against itself otherwise.

    python3 linux/test_import_census.py
"""
import io
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import import_census as census  # noqa: E402

DATA, GOT, LAZY = 0x100002000, 0x100002000, 0x100002100  # __DATA, its plain pointers, its lazy pointers
SYMBOLS = ["_malloc", "__Znwm", "_glClear", "__ZN4GameC1Ev"]  # what the lazy pointers are of, in order


def uleb(n):
    out = b""
    while True:
        b, n = n & 0x7f, n >> 7
        out += bytes([b | (0x80 if n else 0)])
        if not n:
            return out


def bind(ordinal, sym, offset, done=b""):
    """One entry: the library, the name, __DATA (segment 1) at an offset, bind."""
    lib = bytes([0x10 | ordinal]) if 0 <= ordinal < 16 else b"\x20" + uleb(ordinal)
    return lib + b"\x40" + sym.encode() + b"\0" + b"\x71" + uleb(offset) + b"\x90" + done


def macho():
    """An x86-64 Mach-O with two libraries, four lazy pointers and the three bind tables."""
    plain = (bind(1, "___stderrp", 0x00) + bind(2, "__ZTVSt9bad_alloc", 0x08) + bind(2, "__Znwm", 0x10)
             + bind(0, "_own_symbol", 0x18)
             # three pointers to one symbol, 8 bytes apart: DO_BIND_ULEB_TIMES_SKIPPING_ULEB
             + b"\x12\x40___cxa_pure_virtual\0\x71" + uleb(0x20) + b"\xc0" + uleb(3) + uleb(8) + b"\x00")
    lazy = (bind(1, "_malloc", 0x100, b"\x00") + bind(2, "__Znwm", 0x108, b"\x00")
            + bind(3, "_glClear", 0x110, b"\x00"))
    weak = b"\x40__Znwm\0\x71" + uleb(0x108) + b"\x90" + b"\x40__ZN4GameC1Ev\0\x71" + uleb(0x118) + b"\x90\x00"
    strtab = b"\0"
    nlist = b""
    for s in SYMBOLS:
        nlist += struct.pack("<IBBHQ", len(strtab), 1, 0, 0, 0)
        strtab += s.encode() + b"\0"
    indirect = struct.pack("<5I", 0x80000000, 0, 1, 2, 3)  # a local one first, then the four lazy pointers

    def dylib(path):
        body = path.encode() + b"\0"
        body += b"\0" * (-len(body) % 8)
        return struct.pack("<IIIIII", 0xc, 24 + len(body), 24, 0, 0, 0) + body

    def segment(name, addr, sects=()):
        cmd = struct.pack("<II16sQQQQIIII", census.LC_SEGMENT_64, 72 + 80 * len(sects), name, addr, 0x1000, 0, 0, 7, 3,
                          len(sects), 0)
        for sname, saddr, size, flags, first in sects:
            cmd += struct.pack("<16s16sQQIIIIIIII", sname, name, saddr, size, 0, 3, 0, 0, flags, first, 0, 0)
        return cmd

    cmds = [segment(b"__TEXT", 0x100000000),
            segment(b"__DATA", DATA, [(b"__got", GOT, 0x38, 0x6, 0), (b"__la_symbol_ptr", LAZY, 0x20, 0x7, 1)]),
            dylib("/usr/lib/libSystem.B.dylib"), dylib("/usr/lib/libstdc++.6.dylib"),
            dylib("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL")]
    tail = 32 + sum(len(c) for c in cmds) + 24 + 80 + 48
    parts, at = [], tail
    offs = []
    for blob in (plain, weak, lazy, nlist, strtab, indirect):
        offs.append(at)
        parts.append(blob)
        at += len(blob)
    cmds.append(struct.pack("<II4I", census.LC_SYMTAB, 24, offs[3], len(SYMBOLS), offs[4], len(strtab)))
    dysym = [0] * 18
    dysym[12], dysym[13] = offs[5], 5
    cmds.append(struct.pack("<II18I", census.LC_DYSYMTAB, 80, *dysym))
    cmds.append(struct.pack("<II10I", census.LC_DYLD_INFO_ONLY, 48, 0, 0, offs[0], len(plain), offs[1], len(weak),
                            offs[2], len(lazy), 0, 0))
    body = b"".join(cmds)
    head = struct.pack("<8I", census.MH_MAGIC_64, census.CPU_X86_64, 3, 2, len(cmds), len(body), 0, 0)
    assert len(head + body) == tail
    return head + body + b"".join(parts)


EXPECT = {
    ("libSystem", "_malloc"): "call",
    ("libSystem", "___stderrp"): "data",
    ("libstdc++", "__Znwm"): "call",             # bound as a pointer, weak and lazy: an import with a stub
    ("libstdc++", "__ZTVSt9bad_alloc"): "data",
    ("libstdc++", "___cxa_pure_virtual"): "data",
    ("OpenGL", "_glClear"): "call",
}                                                 # not there: _own_symbol (the image's), Game::Game() (weak alone)


class Census(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.tmp = self.dir.name
        self.image = os.path.join(self.tmp, "image")
        with open(self.image, "wb") as f:
            f.write(macho())

    def tearDown(self):
        self.dir.cleanup()

    def table(self, rows):
        path = os.path.join(self.tmp, "table.tsv")
        with open(path, "w") as f:
            f.write("# a comment\n\n" + "".join("\t".join(r) + "\n" for r in rows))
        return path

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = census.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def test_library_names(self):
        for path, name in (("/usr/lib/libSystem.B.dylib", "libSystem"), ("/usr/lib/libstdc++.6.dylib", "libstdc++"),
                           ("/usr/lib/libobjc.A.dylib", "libobjc"), ("@loader_path/libsteam_api.dylib", "libsteam_api"),
                           ("@executable_path/../Frameworks/Cg.framework/Cg", "Cg"),
                           ("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL", "OpenGL")):
            self.assertEqual(census.library_name(path), name)

    def test_the_images_imports(self):
        self.assertEqual(census.read_image(self.image), EXPECT)

    def test_a_universal_binarys_x86_64_slice(self):
        thin = macho()
        fat = struct.pack(">II5I5I", census.FAT_MAGIC, 2, 0x0100000c, 0, 4096, 8, 12,
                          census.CPU_X86_64, 3, 8192, len(thin), 12)
        with open(self.image, "wb") as f:
            f.write(fat.ljust(8192, b"\0") + thin)
        self.assertEqual(census.read_image(self.image), EXPECT)

    def test_what_is_no_such_image_is_refused(self):
        with open(self.image, "wb") as f:
            f.write(b"\x7fELF" + b"\0" * 60)
        self.assertEqual(self.run_main(self.image)[0], 2)

    def test_a_table_that_matches(self):
        rows = [(lib, sym, kind, "same", "") for (lib, sym), kind in EXPECT.items()]
        code, out, err = self.run_main(self.image, "--table", self.table(rows), "--check")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("total                      3     3", out)

    def test_what_check_fails_on(self):
        rows = [(lib, sym, kind, "same", "") for (lib, sym), kind in EXPECT.items()]
        rows[0] = (rows[0][0], rows[0][1], "data", "same", "")       # a call written as data
        rows[1] = ("libSystem", "_gone", "call", "stub", "")         # one row for another symbol: one missing, one gone
        code, _, err = self.run_main(self.image, "--table", self.table(rows), "--check")
        self.assertEqual(code, 1)
        for what in ("_malloc: another kind in the table", "___stderrp: not in the table",
                     "_gone: in the table, not imported"):
            self.assertIn(what, err)

    def test_todo_rows_are_counted_and_do_not_fail(self):
        rows = [(lib, sym, kind, "todo", "") for (lib, sym), kind in EXPECT.items()]
        code, _, err = self.run_main(self.image, "--table", self.table(rows), "--check")
        self.assertEqual(code, 0)
        self.assertIn("6 imports are still todo", err)

    def test_missing_gives_rows_to_fill_in(self):
        code, out, _ = self.run_main(self.image, "--table", self.table([]), "--missing")
        self.assertEqual(code, 0)
        self.assertEqual(sorted(out.splitlines()), sorted("%s\t%s\t%s\ttodo\t" % (l, s, k) for (l, s), k in EXPECT.items()))

    def test_a_bad_table_is_refused(self):
        for rows in ([("libSystem", "_malloc", "call", "magic", "")], [("libSystem", "_malloc", "jump", "same", "")],
                     [("libSystem", "_malloc", "call")], [("libSystem", "_malloc", "call", "same", "")] * 2):
            self.assertEqual(self.run_main(self.image, "--table", self.table(rows))[0], 2, rows)

    def test_without_an_image_the_table_alone(self):
        rows = [(lib, sym, kind, "same", "") for (lib, sym), kind in EXPECT.items()]
        code, out, err = self.run_main(os.path.join(self.tmp, "none"), "--table", self.table(rows), "--check")
        self.assertEqual(code, 0)
        self.assertIn("the table alone", err)
        self.assertIn("total                      3     3", out)


class Table(unittest.TestCase):
    """The repository's own table."""

    def test_it_reads(self):
        rows = census.read_table()
        self.assertTrue(rows)
        for (lib, sym), (kind, cls, how) in rows.items():
            self.assertNotEqual(cls, "todo", "%s %s" % (lib, sym))

    def test_it_names_the_real_images_imports(self):
        image = os.environ.get("RC_IMAGE") or census.DEFAULT_IMAGE
        if not os.path.exists(image):
            self.skipTest("no decrypted image at $RC_IMAGE or build/steamless")
        self.assertEqual(census.compare(census.read_image(image), census.read_table()), ([], [], []))


if __name__ == "__main__":
    unittest.main()
