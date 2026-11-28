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
import gen_edits  # noqa: E402
import gen_import_table  # noqa: E402
import gen_mids  # noqa: E402
import import_census as census  # noqa: E402
import test_first_run as made_up  # noqa: E402

BUILDS = (sys.platform.startswith("linux") and platform.machine() == "x86_64"
          and shutil.which("make") and (shutil.which("cc") or shutil.which("gcc")))
REAL = os.environ.get("RC_IMAGE") or census.DEFAULT_IMAGE
GAME = os.environ.get("RS_GAME_DIR")  # the game's folder (the Mac depot): with it, the game's start is run too


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

    def test_the_mid_points_table(self):
        """mids.map's lines as the build takes them: the address, the continuations in their order, and a point
        patch-sites.tsv says no jump can be written at."""
        text = ("# a comment\n"
                "one    0x100000010  (void *a@rdi)  # nothing more\n"
                "two    0x100000020  (uint32_t n@rax, void *t@rbx) -> grace=0x100000079, other=0x100000080  # two ways on\n"
                "three  none this game needs no call here\n"
                "four   0x100000030  ()\n")
        pts = gen_mids.points(text)
        self.assertEqual(pts, [("one", 0x100000010, []), ("two", 0x100000020, [0x100000079, 0x100000080]), ("four", 0x100000030, [])])
        unsafe = gen_mids.unsafe_sites("# head\none\t0x100000010\t5\tyes\t\nfour\t0x100000030\t7\tno\ta branch lands inside\n")
        self.assertEqual(unsafe, {"four"})
        out = gen_mids.table(pts, unsafe)
        self.assertIn('MID("two", 0x100000020ull, 1, 0x100000079ull, 0x100000080ull, 0x0ull', out)
        self.assertIn('MID("four", 0x100000030ull, 0, 0x0ull', out)
        # the repository's own: every point of mids.map with an address is in patch-sites.tsv
        with open(gen_mids.MAP, encoding="utf-8") as f:
            real = gen_mids.points(f.read())
        with open(gen_mids.SITES, encoding="utf-8") as f:
            listed = {l.split("\t")[0]: l.split("\t") for l in f if l.strip() and not l.startswith("#")}
        self.assertGreater(len(real), 100)
        for name, addr, _ in real:
            self.assertIn(name, listed)
            self.assertEqual(int(listed[name][1], 16), addr, name)

    def test_the_edits_table(self):
        """edits.map's lines as the build takes them: from the highest address down, a change inside a range as its
        offset, length and bytes, and a longer edit with its row of patch-edits.tsv."""
        rows = gen_edits.longer_rows("# head\n0x100000020\t3\t5\t8\t2\tyes\t\n0x100000040\t2\t6\t2\t0\tno\tsomething goes inside\n")
        self.assertEqual(rows, {0x100000020: (3, 5, 8, 2, True), 0x100000040: (2, 6, 2, 0, False)})
        edits = [("one", 0x100000010, 5, "0a0b0c0d", [(1, b"\x07"), (3, b"\x08\x09")], "a"),
                 ("one", 0x100000020, 3, "11111111", [(None, b"\x01\x02\x03\x04\x05")], "b"),
                 ("two", 0x100000030, 4, "22222222", [(None, b"\x90\x90")], "c"),
                 ("two", 0x100000040, 2, "33333333", [(None, bytes(6))], "d")]
        out = gen_edits.table(edits, rows).splitlines()
        self.assertEqual([l for l in out if l.startswith("EDIT")],
                         ['EDIT("two", 0x100000040ull, 2, 0x33333333u, 1, 2, 0, 0, 0)',
                          'EDIT("two", 0x100000030ull, 4, 0x22222222u, 1, 4, 0, 1, 1)',
                          'EDIT("one", 0x100000020ull, 3, 0x11111111u, 1, 8, 2, 1, 2)',
                          'EDIT("one", 0x100000010ull, 5, 0x0a0b0c0du, 0, 5, 0, 1, 3)'])
        self.assertIn("BYTES(3, 0x01, 0x01, 0x07, 0x03, 0x02, 0x08, 0x09)", out)
        self.assertIn("BYTES(2, 0x01, 0x02, 0x03, 0x04, 0x05)", out)
        with self.assertRaises(ValueError):   # a longer edit the image was never asked about
            gen_edits.table(edits, {0x100000040: rows[0x100000040]})
        with self.assertRaises(ValueError):   # a row made for another edit
            gen_edits.table(edits, {**rows, 0x100000020: (3, 9, 8, 2, True)})
        with self.assertRaises(ValueError):   # a row with no edit
            gen_edits.table(edits[:1], rows)
        # the repository's own: every edit is in, the longer ones with a row each
        real = gen_edits.made()
        self.assertGreater(real.count("\nEDIT("), 600)
        self.assertEqual(real.count("\nEDIT("), real.count("\nBYTES("))

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
        r = subprocess.run(["make", "-C", HERE, "OUT=" + cls.out, cls.out + "/rs-rearmed", cls.out + "/layer_test",
                            cls.out + "/call_test", cls.out + "/patch_test", cls.out + "/libsteam_api.so"],
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

    def test_what_is_written_into_code(self):
        """An edit group's forms and the mid points' ways in, on code of the test's own (test/patch_test.c)."""
        r = subprocess.run([os.path.join(self.out, "patch_test")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())
        self.assertIn("patch test: an edit group's four forms", r.stdout.decode())

    def test_calls_into_the_game_pass_their_arguments(self):
        r = subprocess.run([os.path.join(self.out, "call_test")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())
        self.assertIn("all checks passed", r.stdout.decode())

    def test_an_initializer_runs_and_its_import_is_counted(self):
        code, out, err = self.run_made_up(made_up.image(1, "_getpid"), "--init-only", "--count")
        self.assertEqual(code, 0, out + err)
        self.assertIn("imports: 2 (1 called): 1 the host's own, 1 the Darwin layer's, 0 the project's libraries', 0 with no answer yet", out)
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

    def test_the_games_start(self):
        """GameBootstrap, whole (the game's root, Lua, its settings, the file system, the main service), and the
        first GameFrame as far as it goes."""
        if not os.path.exists(REAL) or not GAME or not os.path.isdir(GAME):
            self.skipTest("needs the decrypted image and RS_GAME_DIR, the game's folder")
        home = os.path.join(self.tmp.name, "home")  # the port's folder for this run: its saves start empty here
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"))
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--boot", GAME, "--frames", "1"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out, err = r.stdout.decode(), r.stderr.decode()
        self.assertIn("GameBootstrap returned: the Lua owner made, the main service made", out, out + err)
        self.assertIn("12 the project's libraries'", out)  # the Steam library answers its twelve
        # The first frame ends at the first import with no answer yet. Every import of the C library, pthreads,
        # Mach, the files and Steam has one: what stops it is the audio or display layer's, or a library's.
        self.assertEqual(r.returncode, 3, out + err)
        self.assertRegex(err, r"the game called \S+ \S+ \((shim|layer)\), which has no answer yet")

    def test_the_steam_librarys_own_tests(self):
        r = subprocess.run(["make", "-C", HERE, "OUT=" + self.out, "check-steam"], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())
        self.assertIn("vtables: all slots match", r.stdout.decode())
        self.assertIn("all checks passed", r.stdout.decode())


def pkg_config(*names):
    return bool(shutil.which("pkg-config")) and subprocess.run(["pkg-config", "--exists"] + list(names)).returncode == 0


@unittest.skipUnless(BUILDS and pkg_config("sdl3", "egl", "opengl"),
                     "the shell and the renderer build where SDL 3's, EGL's and OpenGL's headers are (on Bazzite: in a distrobox)")
class Picture(unittest.TestCase):
    """The renderer's libraries from shims/opengl and shims/cg, and the game in a window. These draw: they need a GPU
    or Mesa's software renderer, and no display (EGL without a window, SDL's offscreen driver)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = os.path.join(cls.tmp.name, "out")
        r = subprocess.run(["make", "-C", HERE, "OUT=" + cls.out, "all", "gl"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode:
            raise AssertionError("make failed:\n" + r.stdout.decode())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_renderers_own_tests(self):
        """An offscreen context drawn into and read back, and the Cg library's programs on both of its backends."""
        r = subprocess.run(["make", "-C", HERE, "OUT=" + self.out, "check-gl"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        text = r.stdout.decode()
        self.assertEqual(r.returncode, 0, text)
        self.assertRegex(text, r"\d+ passed, 0 failed")
        self.assertEqual(text.count("ok (0 failures)"), 2, text)

    def test_the_game_draws_its_first_screens(self):
        if not os.path.exists(REAL) or not GAME or not os.path.isdir(GAME):
            self.skipTest("needs the decrypted image and RS_GAME_DIR, the game's folder")
        home, picture = os.path.join(self.tmp.name, "home"), os.path.join(self.tmp.name, "frame")
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"), SDL_VIDEODRIVER="offscreen", RS_AUDIO="silent")  # no window, and no sound played
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--run", GAME, "--frames", "300",
                            "--picture", picture, "--picture-every", "30"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out, err = r.stdout.decode(), r.stderr.decode()
        self.assertEqual(r.returncode, 0, out + err)
        self.assertIn("300 frames", err)
        # The first screens are dark logos on a light ground, with fades between: one of the pictures, taken every
        # half second, has both, the ground being most of it. (Which one depends on the machine's pace.)
        seen = []
        for n in range(30, 301, 30):
            with open("%s.%06d.ppm" % (picture, n), "rb") as f:
                head, size, depth, rgb = f.read().split(b"\n", 3)
            self.assertEqual((head, size, depth), (b"P6", b"1280 720", b"255"))
            self.assertEqual(len(rgb), 1280 * 720 * 3)
            pixels = [rgb[i:i + 3] for i in range(0, len(rgb), 3 * 251)]
            light = sum(1 for p in pixels if min(p) > 200) / len(pixels)
            dark = sum(1 for p in pixels if max(p) < 80) / len(pixels)
            seen.append((n, round(light, 2), round(dark, 3)))
        self.assertTrue(any(light > 0.5 and dark > 0.01 for _, light, dark in seen), seen)

    def test_a_fixed_frame_rate_and_the_windows_switches(self):
        """RS_FPS: the frames start on the shell's clock, 30 a second here, whatever the display's rate. With it
        the other switches of the installer's Picture group, which change nothing a run without a display shows
        but must not stop one: a forced window mode, the 16:9 lock, Mesa's multithreaded OpenGL."""
        if not os.path.exists(REAL) or not GAME or not os.path.isdir(GAME):
            self.skipTest("needs the decrypted image and RS_GAME_DIR, the game's folder")
        import time
        home = os.path.join(self.tmp.name, "home3")
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"), SDL_VIDEODRIVER="offscreen",
                   RS_AUDIO="silent", RS_FPS="30", RS_FULLSCREEN="0", RS_WINDOW_ANY_SHAPE="0", RS_GL_MTENGINE="1")
        start = time.monotonic()
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--run", GAME, "--frames", "150"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        took = time.monotonic() - start
        self.assertEqual(r.returncode, 0, r.stdout.decode() + r.stderr.decode())
        self.assertIn("150 frames", r.stderr.decode())
        self.assertGreater(took, 149 / 30.0 - 0.2)  # (at the display's 60 a second it is half that)

    def test_a_mod_in_the_mods_folder_is_loaded_and_its_hook_runs(self):
        """A mod is a shared library that names the game's points (runtime/rc_mod.h): the program loads those of
        the mods folder before the game starts, and the hook one asked for runs around the game's function."""
        if not os.path.exists(REAL) or not GAME or not os.path.isdir(GAME):
            self.skipTest("needs the decrypted image and RS_GAME_DIR, the game's folder")
        r = subprocess.run(["make", "-C", HERE, "OUT=" + self.out, self.out + "/test/mods/test_mod.so"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())
        home = os.path.join(self.tmp.name, "home4")
        os.makedirs(os.path.join(home, "mods"))
        shutil.copy(os.path.join(self.out, "test", "mods", "test_mod.so"), os.path.join(home, "mods"))
        with open(os.path.join(home, "mods", "not-a-mod.txt"), "w") as f:
            f.write("left alone")
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"), SDL_VIDEODRIVER="offscreen",
                   RS_AUDIO="silent")
        argv = [os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--run", GAME, "--frames", "130"]
        r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        err = r.stderr.decode()
        self.assertEqual(r.returncode, 0, r.stdout.decode() + err)
        self.assertIn("[rc] mod loaded: test_mod.so", err)
        self.assertIn("[test-mod] on mac-x64: game_frame hooked, the settings folder known", err)
        self.assertIn("no_such_point", err)  # the runtime says a point it has not got
        # the edit group it switches on is written into the game's code, and the game goes on as before with it
        self.assertIn("[rc] edits plain_packages: on (2)", err)
        self.assertIn("[rs-rearmed] edits plain_packages: 2 written into the game's code, 2 of them longer than their range", err)
        self.assertIn("[test-mod] edit groups on: 1; mid points hooked: 0", err)
        self.assertIn("[test-mod] 100 frames: 100 hooks before, 100 after; the provider was asked for files: yes", err)
        # and it draws over the game's frame through the overlay API: what it reads back is what it drew, on a
        # picture of the frame as well
        picture = os.path.join(self.tmp.name, "overlay.ppm")
        r = subprocess.run(argv + ["--picture", picture, "--picture-frame", "120"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           env=dict(env, RS_TEST_MOD_OVERLAY="1"))
        err = r.stderr.decode()
        self.assertEqual(r.returncode, 0, r.stdout.decode() + err)
        self.assertIn("[test-mod] overlay API 1: registered", err)
        self.assertRegex(err, r"\[test-mod\] overlay on a frame of \d+x\d+: the square reads ff00ffff, the image 00ff00ff")
        with open(picture, "rb") as f:
            head = f.readline(), f.readline(), f.readline()
            width = int(head[1].split()[0])
            rgb = f.read()
        at = lambda x, y: tuple(rgb[3 * (y * width + x):3 * (y * width + x) + 3])
        self.assertEqual((at(16, 16), at(44, 16)), ((255, 0, 255), (0, 255, 0)))
        r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=dict(env, RS_LOAD_MODS="0"))
        self.assertEqual(r.returncode, 0)
        self.assertNotIn("test-mod", r.stderr.decode())  # switched off: none is loaded

    def test_every_edit_and_every_mid_point_goes_in(self):
        """Every edit of the tables is written when a mod switches its group on, the longer ones read by the program
        as the tool that made patch-edits.tsv read them, and every mid point takes a hook: by a jump, or by a
        breakpoint where patch-sites.tsv says no jump can be written. With every point hooked by a function that
        changes nothing, the game starts as it does without."""
        if not os.path.exists(REAL) or not GAME or not os.path.isdir(GAME):
            self.skipTest("needs the decrypted image and RS_GAME_DIR, the game's folder")
        r = subprocess.run(["make", "-C", HERE, "OUT=" + self.out, self.out + "/test/mods/test_mod.so"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(r.returncode, 0, r.stdout.decode())
        home = os.path.join(self.tmp.name, "home5")
        os.makedirs(os.path.join(home, "mods"))
        shutil.copy(os.path.join(self.out, "test", "mods", "test_mod.so"), os.path.join(home, "mods"))
        with open(gen_mids.MAP, encoding="utf-8") as f:
            points = [name for name, _, _ in gen_mids.points(f.read())]
        with open(gen_mids.SITES, encoding="utf-8") as f:
            unsafe = gen_mids.unsafe_sites(f.read()) & set(points)
        edits = gen_edits.made()
        groups = sorted(set(re.findall(r'^EDIT\("(\w+)"', edits, re.M)))
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"), SDL_VIDEODRIVER="offscreen",
                   RS_AUDIO="silent", RS_TEST_MOD_MIDS=",".join(points))
        # the tables whole: the image's initializers alone, as the edits change what the game then does
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--init-only"], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, env=dict(env, RS_TEST_MOD_EDITS=",".join(groups)))
        out, err = r.stdout.decode(), r.stderr.decode()
        self.assertEqual(r.returncode, 0, out + err)
        for g in groups:
            n = len(re.findall(r'^EDIT\("%s"' % g, edits, re.M))
            self.assertIn("[rs-rearmed] edits %s: %d written into the game's code" % (g, n), err)
        self.assertIn("[test-mod] edit groups on: %d; mid points hooked: %d" % (len(groups), len(points)), err)
        self.assertNotIn("left as it is", err)
        self.assertIn("mid points with a hook: %d (%d by a breakpoint)" % (len(points), len(unsafe)), out)
        # and the game's start with every mid point hooked and no edit
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--run", GAME, "--frames", "130"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=dict(env, RS_TEST_MOD_EDITS=""))
        out, err = r.stdout.decode(), r.stderr.decode()
        self.assertEqual(r.returncode, 0, out + err)
        self.assertIn("mid points with a hook: %d (%d by a breakpoint)" % (len(points), len(unsafe)), out)
        reached = re.search(r"\[test-mod\] 100 frames: 100 hooks before, 100 after; .*mid points reached: (\d+)", err)
        self.assertTrue(reached, err)
        self.assertGreater(int(reached.group(1)), 0)

    def test_enter_on_the_title_screen_goes_past_the_sign_in(self):
        """The online services are replaced by the offline ones (nine game functions, patched at their entries):
        Enter on the title screen leads to the profiles, not to a sign-in dialog, and nothing reaches a network."""
        if not os.path.exists(REAL) or not GAME or not os.path.isdir(GAME):
            self.skipTest("needs the decrypted image and RS_GAME_DIR, the game's folder")
        home, picture = os.path.join(self.tmp.name, "home2"), os.path.join(self.tmp.name, "menu")
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"), SDL_VIDEODRIVER="offscreen", RS_AUDIO="silent")  # no window, and no sound played
        r = subprocess.run([os.path.join(self.out, "rs-rearmed"), "--image", REAL, "--run", GAME, "--frames", "1900",
                            "--keys", "1300:return", "--picture", picture, "--picture-every", "300"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out, err = r.stdout.decode(), r.stderr.decode()
        self.assertEqual(r.returncode, 0, out + err)
        # nine functions of the online services, two of the audio module and the eleven of the built-in fixes are
        # replaced; every hook point
        # is in, and the thirteen mid points the fixes have hooks on
        self.assertIn("replaced: 22 game functions with native ones", out)
        self.assertIn("hook points: 21; mid points with a hook: 13", out)
        self.assertNotIn("left as it is", err)

        def light(frame):
            with open("%s.%06d.ppm" % (picture, frame), "rb") as f:
                rgb = f.read().split(b"\n", 3)[3]
            pixels = [rgb[i:i + 3] for i in range(0, len(rgb), 3 * 251)]
            return sum(1 for p in pixels if min(p) > 200) / len(pixels)

        # the title screen is a logo on a light ground; the profiles are a list over a room
        self.assertGreater(light(1200), 0.8)
        self.assertLess(light(1800), 0.5)
        # The intro skip of the built-in fixes (a hook before the boot service's intro tick, the game's function
        # then running from its moved first instructions): the title screen is there within ten seconds, where
        # the intro alone takes seventeen. The same picture at frame 600 as at 1200.
        self.assertAlmostEqual(light(600), light(1200), delta=0.01)


if __name__ == "__main__":
    unittest.main()
