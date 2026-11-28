#!/usr/bin/env python3
"""The x86-64 Linux loader (linux/src, linux/darwin): built with `make -C linux` into a temporary folder, then the
Darwin layer's own test, a made-up image (test_first_run.py's: a dozen bytes of code that call one import), and the
real image's initializers when it is at $RC_IMAGE or build/steamless. No game bytes here.

It needs x86-64 Linux with make and a C compiler, and skips its builds anywhere else. The table the build makes
from darwin-imports.tsv is checked everywhere.

    python3 linux/test_loader.py
"""
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_import_table  # noqa: E402
import import_census as census  # noqa: E402
import test_first_run as made_up  # noqa: E402

BUILDS = (sys.platform.startswith("linux") and platform.machine() == "x86_64"
          and shutil.which("make") and (shutil.which("cc") or shutil.which("gcc")))
REAL = os.environ.get("RC_IMAGE") or census.DEFAULT_IMAGE


class Table(unittest.TestCase):
    def test_the_c_table_has_every_row_in_the_searchs_order(self):
        rows = census.read_table()
        text = gen_import_table.table(rows)
        got = re.findall(r'^    \{"([^"]+)", "([^"]+)", (\d+), (\d)\},$', text, re.M)
        self.assertEqual(len(got), len(rows))
        self.assertEqual([(s.encode(), l.encode()) for s, l, _, _ in got],
                         sorted((s.encode(), l.encode()) for l, s in rows))
        classes = list(census.CLASSES)
        for sym, lib, cls, call in got:
            kind, want, _ = rows[(lib, sym)]
            self.assertEqual((classes[int(cls)], call), (want, "1" if kind == "call" else "0"))
        self.assertIn("#define RSL_CLASS_SAME %d\n" % classes.index("same"), text)

    def test_the_layer_answers_only_what_the_table_gives_it(self):
        """Every name the layer registers is an import of the table, and not one the table calls the host's own."""
        rows = {sym: cls for (_, sym), (_, cls, _) in census.read_table().items()}
        names = []
        for name in sorted(os.listdir(os.path.join(HERE, "darwin"))):
            if name.endswith(".c"):
                with open(os.path.join(HERE, "darwin", name), encoding="utf-8") as f:
                    names += re.findall(r'^DW_(?:FUNCTION|DATA)\("([^"]+)"', f.read(), re.M)
        self.assertGreaterEqual(len(names), 10)
        self.assertEqual(len(names), len(set(names)))
        for sym in names:
            self.assertIn(sym, rows)
            self.assertIn(rows[sym], ("adapt", "emulate", "runtime", "layer", "refuse"), sym)


@unittest.skipUnless(BUILDS, "the loader builds on x86-64 Linux with make and a C compiler")
class Loader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = os.path.join(cls.tmp.name, "out")
        r = subprocess.run(["make", "-C", HERE, "OUT=" + cls.out, cls.out + "/rs-rearmed", cls.out + "/layer_test"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode:
            raise AssertionError("make failed:\n" + r.stdout.decode())
        cls.build_log = r.stdout.decode()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_loader(self, image, *args):
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", image] + list(args),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return r.returncode, r.stdout.decode(), r.stderr.decode()

    def run_made_up(self, data, *args):
        path = os.path.join(self.tmp.name, "image")
        with open(path, "wb") as f:
            f.write(data)
        return self.run_loader(path, *args)

    def test_it_builds_with_no_warning(self):
        self.assertNotIn("warning", self.build_log)

    def test_the_layers_own_test(self):
        r = subprocess.run([os.path.join(self.out, "layer_test")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())
        self.assertIn("all checks passed", r.stdout.decode())

    def test_an_initializer_runs_and_its_import_is_counted(self):
        code, out, err = self.run_made_up(made_up.image(1, "_getpid"), "--init-only", "--count")
        self.assertEqual(code, 0, out + err)
        self.assertIn("imports: 2 (1 called): 1 the host's own, 1 the Darwin layer's, 0 with no answer yet", out)
        self.assertIn("1 of 1 initializers ran", out)
        self.assertRegex(out, r"imports called: 1\n +1  libSystem +_getpid +same +the host's own")

    def test_bind_only_runs_nothing(self):
        code, out, err = self.run_made_up(made_up.image(2, "_NSApplicationMain"), "--bind-only")
        self.assertEqual(code, 0, out + err)
        self.assertIn("1 with no answer yet", out)
        self.assertIn("no answer yet, by class: stub 1", out)
        self.assertNotIn("initializers", out)

    def test_an_import_nothing_answers_stops_the_run_and_is_named(self):
        code, out, err = self.run_made_up(made_up.image(2, "_NSApplicationMain"), "--init-only")
        self.assertEqual(code, 3, out + err)
        self.assertIn("the game called AppKit _NSApplicationMain (stub), which has no answer yet", err)
        # the return address of the made-up initializer's call, and which initializer it was
        self.assertIn("from 0x%x, in initializer 1 of 1, at 0x%x" % (made_up.TEXT + made_up.CODE + 10,
                                                                    made_up.TEXT + made_up.CODE), err)

    def test_an_import_the_table_lacks_is_not_bound_to_the_host(self):
        """`getppid` is the host's, but no import of the game: only a `same` row gets the host's function."""
        self.assertNotIn(("libSystem", "_getppid"), census.read_table())
        code, out, err = self.run_made_up(made_up.image(1, "_getppid"), "--init-only")
        self.assertEqual(code, 3, out + err)
        self.assertIn("the game called libSystem _getppid (?)", err)

    def test_what_is_no_image_is_refused(self):
        path = os.path.join(self.tmp.name, "not-an-image")
        with open(path, "wb") as f:
            f.write(b"\x7fELF" + bytes(60))
        code, out, err = self.run_loader(path, "--init-only")
        self.assertEqual(code, 1, out + err)
        self.assertIn("not an x86-64 Mach-O", err)
        self.assertEqual(self.run_loader(os.path.join(self.tmp.name, "absent"), "--init-only")[0], 1)

    def test_the_real_images_initializers(self):
        if not os.path.exists(REAL):
            self.skipTest("no decrypted image at %s" % REAL)
        code, out, err = self.run_loader(REAL, "--init-only", "--count")
        self.assertEqual(code, 0, out + err)
        self.assertIn("3487 of 3487 initializers ran", out)
        self.assertIn("unwind tables registered", out)
        called = {sym: int(n) for n, sym in re.findall(r"^ +(\d+)  \S+ +(\S+) ", out, re.M)}
        # what first_run.py counts on a Mac and on Linux
        self.assertEqual(len(called), 16, out)
        self.assertEqual((called["___cxa_atexit"], called["_pthread_mutexattr_settype"], called["__Znwm"]), (290, 34, 1))
        table = census.read_table()
        counts = re.search(r"imports: (\d+) \((\d+) called\): (\d+) the host's own", out)
        self.assertEqual((int(counts.group(1)), int(counts.group(2))),
                         (len(table), sum(1 for kind, _, _ in table.values() if kind == "call")))
        self.assertEqual(int(counts.group(3)), sum(1 for _, cls, _ in table.values() if cls == "same"))


if __name__ == "__main__":
    unittest.main()
