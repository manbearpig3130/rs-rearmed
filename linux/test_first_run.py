#!/usr/bin/env python3
"""linux/first_run.py: its tables on any machine, and a made-up image (a dozen bytes of code that call one import)
run where the process is x86-64: an x86-64 Linux, or `arch -x86_64 /usr/bin/python3 linux/test_first_run.py` on a
Mac. The real image's initializers are run as well when it is at $RC_IMAGE or build/steamless. No game bytes here.

    python3 linux/test_first_run.py
"""
import json
import os
import platform
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import first_run  # noqa: E402
import import_census as census  # noqa: E402

X86_64 = platform.machine() in ("x86_64", "AMD64")
TEXT, DATA, LINK = 0x100000000, 0x100001000, 0x100002000
CODE = 0xf00  # the initializer, in __TEXT
# push rbp; mov rbp, rsp; call [rip + the lazy pointer at DATA]; pop rbp; ret
INIT = b"\x55\x48\x89\xe5\xff\x15" + struct.pack("<i", DATA - (TEXT + CODE + 10)) + b"\x5d\xc3"


def image(library_ordinal, imported):
    """An x86-64 Mach-O whose one initializer calls IMPORTED, of the library with that ordinal (1 libSystem, 2 AppKit)."""
    rebase = b"\x11\x22\x10\x51\x00"                                   # the initializer's pointer, at DATA + 0x10
    bind = b"\x11\x40___stderrp\0\x51\x72\x08\x90\x00"                 # a data import, at DATA + 8
    lazy = b"\x72\x00" + bytes([0x10 | library_ordinal]) + b"\x40" + imported.encode() + b"\0\x90\x00"
    strtab = b"\0" + imported.encode() + b"\0___stderrp\0"
    nlist = struct.pack("<IBBHQ", 1, 1, 0, 0, 0) + struct.pack("<IBBHQ", 2 + len(imported), 1, 0, 0, 0)
    indirect = struct.pack("<II", 0, 1)
    link, offs = b"", []
    for blob in (rebase, bind, lazy, nlist, strtab, indirect):
        offs.append(0x2000 + len(link))
        link += blob

    def segment(name, addr, size, fileoff, filesize, sects=()):
        cmd = struct.pack("<II16sQQQQIIII", census.LC_SEGMENT_64, 72 + 80 * len(sects), name, addr, size, fileoff, filesize,
                          7, 7, len(sects), 0)
        for sname, saddr, ssize, flags, first in sects:
            cmd += struct.pack("<16s16sQQIIIIIIII", sname, name, saddr, ssize, 0, 3, 0, 0, flags, first, 0, 0)
        return cmd

    def dylib(path):
        body = path.encode() + b"\0"
        body += b"\0" * (-len(body) % 8)
        return struct.pack("<IIIIII", 0xc, 24 + len(body), 24, 0, 0, 0) + body

    dysym = [0] * 18
    dysym[12], dysym[13] = offs[5], 2
    cmds = [segment(b"__PAGEZERO", 0, TEXT, 0, 0),
            segment(b"__TEXT", TEXT, 0x1000, 0, 0x1000),
            segment(b"__DATA", DATA, 0x1000, 0x1000, 0x1000,
                    [(b"__la_symbol_ptr", DATA, 8, 0x7, 0), (b"__got", DATA + 8, 8, 0x6, 1),
                     (b"__mod_init_func", DATA + 0x10, 8, 0x9, 0)]),
            segment(b"__LINKEDIT", LINK, 0x1000, 0x2000, len(link)),
            dylib("/usr/lib/libSystem.B.dylib"), dylib("/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"),
            struct.pack("<II4I", census.LC_SYMTAB, 24, offs[3], 2, offs[4], len(strtab)),
            struct.pack("<II18I", census.LC_DYSYMTAB, 80, *dysym),
            struct.pack("<II10I", census.LC_DYLD_INFO_ONLY, 48, offs[0], len(rebase), offs[1], len(bind), 0, 0,
                        offs[2], len(lazy), 0, 0)]
    body = b"".join(cmds)
    head = struct.pack("<8I", census.MH_MAGIC_64, census.CPU_X86_64, 3, 2, len(cmds), len(body), 0, 0)
    text = (head + body).ljust(CODE, b"\0") + INIT
    data = struct.pack("<QQQ", 0, 0, TEXT + CODE)
    return text.ljust(0x1000, b"\0") + data.ljust(0x1000, b"\0") + link


class Tables(unittest.TestCase):
    def test_the_rune_table_has_darwins_bits(self):
        t = first_run.rune_locale()
        bits = lambda c: struct.unpack_from("<I", t, first_run.RUNE_TYPES + 4 * ord(c))[0]
        A, D, L, P, S, U, X = (first_run.CT_A, first_run.CT_D, first_run.CT_L, first_run.CT_P, first_run.CT_S,
                               first_run.CT_U, first_run.CT_X)
        self.assertEqual(bits("A") & (A | U | L | X | 0xff), A | U | X | 10)   # a hex digit keeps its value
        self.assertEqual(bits("z") & (A | U | L | X), A | L)
        self.assertEqual(bits("7") & (D | X | A | 0xff), D | X | 7)
        self.assertTrue(bits(" ") & S and bits("\n") & S and bits("~") & P)
        self.assertEqual(bits("\x80"), 0)
        self.assertEqual(struct.unpack_from("<I", t, first_run.RUNE_LOWER + 4 * ord("Q"))[0], ord("q"))
        self.assertEqual(struct.unpack_from("<I", t, first_run.RUNE_UPPER + 4 * ord("q"))[0], ord("Q"))
        self.assertEqual(len(t), first_run.RUNE_SIZE)

    def test_the_made_up_images_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "image")
            with open(path, "wb") as f:
                f.write(image(1, "_getpid"))
            self.assertEqual(census.read_image(path), {("libSystem", "_getpid"): "call", ("libSystem", "___stderrp"): "data"})
            im = first_run.Image(path)
            self.assertEqual((im.lo, im.hi, im.inits_at), (TEXT, LINK + 0x1000, (DATA + 0x10, 1)))
            self.assertEqual(list(im.rebases([0, TEXT, DATA, LINK])), [DATA + 0x10])
            self.assertEqual(im.exports(), {})

    def test_an_export_trie(self):
        im = first_run.Image.__new__(first_run.Image)
        # the root has one child, "_main"; that node is a symbol at offset 0x1b00 with a child "2" at 0x2000
        node2 = b"\x03\x00\x80\x40\x00"                          # flags 0, address 0x2000, no children
        node1 = b"\x03\x00\x80\x36\x01" + b"2\0" + bytes([9 + 8])   # flags 0, address 0x1b00, one child, after this
        trie = b"\x00\x01" + b"_main\0" + bytes([9]) + node1 + node2
        im.segs = [("__PAGEZERO", 0, TEXT, 0, 0), ("__TEXT", TEXT, 0x1000, 0, 0x1000)]
        im.stream = lambda k: trie
        self.assertEqual(im.exports(), {"_main": TEXT + 0x1b00, "_main2": TEXT + 0x2000})

    def test_the_machine_report_never_fails(self):
        import io
        out = io.StringIO()
        self.assertEqual(first_run.machine(out), 0)
        self.assertIn("System:", out.getvalue())


@unittest.skipUnless(X86_64, "the game's code is x86-64: run in an x86-64 process")
class Run(unittest.TestCase):
    def run_image(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            path, out = os.path.join(tmp, "image"), os.path.join(tmp, "out.json")
            with open(path, "wb") as f:
                f.write(data)
            r = subprocess.run([sys.executable, os.path.join(HERE, "first_run.py"), path, "--json", out],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            with open(out) as f:
                return r.returncode, json.load(f), r.stdout.decode()

    def test_an_initializer_runs_and_its_import_is_counted(self):
        code, result, text = self.run_image(image(1, "_getpid"))
        self.assertEqual((code, result["ran"], result["total"], result["stopped"]), (0, 1, 1, None), text)
        self.assertEqual([(c["library"], c["symbol"], c["calls"]) for c in result["called"]], [("libSystem", "_getpid", 1)])

    def test_an_import_nothing_answers_stops_the_run_and_is_named(self):
        code, result, text = self.run_image(image(2, "_NSApplicationMain"))
        self.assertEqual((code, result["ran"]), (1, 0), text)
        self.assertIn("AppKit _NSApplicationMain", result["stopped"])
        self.assertIn("initializer 1 of 1, at 0x%x" % (TEXT + CODE), result["stopped"])

    def test_the_real_images_initializers(self):
        path = os.environ.get("RC_IMAGE") or census.DEFAULT_IMAGE
        if not os.path.exists(path):
            self.skipTest("no decrypted image at $RC_IMAGE or build/steamless")
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "out.json")
            r = subprocess.run([sys.executable, os.path.join(HERE, "first_run.py"), path, "--json", out],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            with open(out) as f:
                result = json.load(f)
        self.assertEqual((r.returncode, result["ran"], result["stopped"]), (0, result["total"], None), r.stdout.decode())
        self.assertGreater(result["total"], 3000)


if __name__ == "__main__":
    unittest.main()
