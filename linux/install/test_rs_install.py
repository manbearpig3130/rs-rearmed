#!/usr/bin/env python3
"""linux/install/rs_install.py: the setup and the start of RS Re-ARMed on Linux, on made-up Steam folders and a
made-up game (a few small files). Nothing here asks the real Steam for anything, and no game bytes are here. It
runs on any machine: what only Linux has (clones by FICLONE, /proc) is checked for what it answers elsewhere too.

    python3 linux/install/test_rs_install.py
"""
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rs_install  # noqa: E402

APP = 221680


def write(path, data=b"x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())
    return path


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def appinfo(apps, version=0x29):
    """A binary appcache/appinfo.vdf with {AppID: its parent or None}: the keys as numbers into a table of strings
    at the end from version 0x29 on, as names before."""
    names = [b"appinfo", b"common", b"parent", b"name"]
    body = b""
    for app, parent in apps.items():
        key = struct.pack("<i", names.index(b"parent")) if version >= 0x29 else b"parent\0"
        kv = b"\x00" + (struct.pack("<i", 0) if version >= 0x29 else b"appinfo\0")
        if parent is not None:
            kv += b"\x02" + key + struct.pack("<i", parent)
        kv += b"\x08\x08"
        entry = b"\0" * 60 + kv  # the state, the time, the token, two hashes and the change number
        body += struct.pack("<II", app, len(entry)) + entry
    body += struct.pack("<I", 0)
    head = struct.pack("<II", 0x07564400 | version, 1)
    if version < 0x29:
        return head + body
    table = struct.pack("<I", len(names)) + b"".join(n + b"\0" for n in names)
    return head + struct.pack("<q", 16 + len(body)) + body + table


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rs-install-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.steam = os.path.join(self.tmp, "Steam")
        self.home = os.path.join(self.tmp, "home")
        env = mock.patch.dict(os.environ)
        env.start()
        self.addCleanup(env.stop)
        for k in [k for k in os.environ if k.startswith(("RS_", "LP_"))] + ["XDG_DATA_HOME"]:
            os.environ.pop(k, None)
        os.environ["RS_GAME_PROCESS"] = "rs-rearmed-test-none"  # a game that runs on this machine is not this test's
        os.environ["RS_DLC_FETCH"] = "0"

    def game(self, top):
        """A made-up Mac game in TOP."""
        for n in ("static.psarc", "songs.psarc", "audio.psarc", "session.psarc"):
            write(os.path.join(top, n), n)
        write(os.path.join(top, rs_install.GAME_BINARY), b"the game's program")
        write(os.path.join(top, "Rocksmith.ini"), "[Audio]\nLatencyBuffer=4\n[Net]\nAutoLogin=1\n")
        write(os.path.join(top, "dlc", "song_m.psarc"), b"mac song")
        write(os.path.join(top, "dlc", "song_p.psarc"), b"pc song")
        return top

    def tools(self):
        """The Mac's tools as rs_install sets them, with a decrypt that writes a made-up image."""
        gamefiles, decrypt = rs_install.the_tools()

        def fake(argv):
            write(argv[argv.index("--output") + 1], b"image of " + read(os.path.join(
                argv[argv.index("--game") + 1], rs_install.GAME_BINARY)).encode())
            return 0
        p = mock.patch.object(decrypt, "main", fake)
        p.start()
        self.addCleanup(p.stop)
        out = mock.patch("sys.stdout", io.StringIO())
        out.start()
        self.addCleanup(out.stop)
        return gamefiles

    def setup(self, *argv):
        return rs_install.main(["setup", "--home", self.home, "--steam", self.steam] + list(argv))


class Folders(Base):
    def test_the_home_folder(self):
        self.assertEqual(rs_install.home_folder("/a/b"), "/a/b")
        self.assertEqual(rs_install.home_folder(), os.path.expanduser("~/.local/share/rs-rearmed"))
        os.environ["XDG_DATA_HOME"] = "/data"
        self.assertEqual(rs_install.home_folder(), "/data/rs-rearmed")
        os.environ["XDG_DATA_HOME"] = "relative"  # not a path the specification allows: not used
        self.assertEqual(rs_install.home_folder(), os.path.expanduser("~/.local/share/rs-rearmed"))
        os.environ["RS_NATIVE_HOME"] = "/elsewhere"
        self.assertEqual(rs_install.home_folder(), "/elsewhere")

    def test_steams_folder(self):
        os.environ["RS_STEAM_DIR"] = self.steam
        self.assertEqual(rs_install.steam_folder(), self.steam)
        self.assertEqual(rs_install.steam_folder("/given"), "/given")

    def test_downloads_are_under_the_folder_the_client_runs_in(self):
        root = os.path.join(self.steam, "steamapps", "content")
        self.assertEqual(rs_install.content_folders(self.steam), [root])
        os.makedirs(os.path.join(self.steam, "ubuntu12_32"))
        old = os.path.join(self.steam, "ubuntu12_32", "steamapps", "content")
        self.assertEqual(rs_install.content_folders(self.steam), [old, root])
        os.makedirs(os.path.join(self.steam, "steamrt64"))
        new = os.path.join(self.steam, "steamrt64", "steamapps", "content")
        self.assertEqual(rs_install.content_folders(self.steam), [new, old, root])
        os.makedirs(old)  # the one that is there comes first
        self.assertEqual(rs_install.content_folders(self.steam), [old, new, root])


class Clone(Base):
    def test_a_clone_or_the_answer_that_there_is_none(self):
        src, dst = write(os.path.join(self.tmp, "a"), b"contents"), os.path.join(self.tmp, "b")
        if rs_install.reflink(src, dst):
            with open(dst, "rb") as f:
                self.assertEqual(f.read(), b"contents")
        else:
            self.assertFalse(os.path.lexists(dst))

    def test_a_file_that_is_there_is_not_replaced(self):
        src, dst = write(os.path.join(self.tmp, "a")), write(os.path.join(self.tmp, "b"), b"kept")
        with self.assertRaises(OSError):
            rs_install.reflink(src, dst)
        with open(dst, "rb") as f:
            self.assertEqual(f.read(), b"kept")


class Processes(Base):
    def test_a_process_by_its_name(self):
        self.assertFalse(rs_install.process_running("rs-rearmed-no-such-name"))
        if os.path.exists("/proc/self/comm"):
            self.assertTrue(rs_install.process_running(read("/proc/self/comm").strip()))

    def test_steam_by_the_program_given(self):
        os.environ["RS_STEAM_EXE"] = os.path.join(self.tmp, "none")
        self.assertIsNone(rs_install.steam_argv())
        exe = write(os.path.join(self.tmp, "steam"), "#!/bin/sh\necho \"$@\" >> \"%s\"\n" % os.path.join(self.tmp, "said"))
        os.chmod(exe, 0o755)
        os.environ["RS_STEAM_EXE"] = exe
        self.assertEqual(rs_install.steam_argv(), [exe])
        self.assertTrue(rs_install.steam_command(self.steam, "download_depot", "221680", "221682"))
        self.assertTrue(rs_install.steam_open("steam://open/console"))
        for _ in range(100):
            if os.path.exists(os.path.join(self.tmp, "said")) and len(read(os.path.join(self.tmp, "said")).splitlines()) == 2:
                break
            time.sleep(0.05)
        self.assertEqual(sorted(read(os.path.join(self.tmp, "said")).splitlines()),
                         ["+download_depot 221680 221682", "steam://open/console"])


class Dlc(Base):
    def test_the_apps_whose_parent_is_the_game(self):
        for version in (0x28, 0x29):
            p = write(os.path.join(self.tmp, "appinfo-%x.vdf" % version),
                      appinfo({APP: None, 100: APP, 200: APP, 300: 7, 400: None}, version))
            self.assertEqual(rs_install.app_children(p, APP), {100, 200}, hex(version))

    def test_a_file_that_is_no_app_info(self):
        self.assertIsNone(rs_install.app_children(os.path.join(self.tmp, "none"), APP))
        self.assertIsNone(rs_install.app_children(write(os.path.join(self.tmp, "x"), b"not app info at all"), APP))
        cut = appinfo({100: APP})
        self.assertIsNone(rs_install.app_children(write(os.path.join(self.tmp, "y"), cut[:20]), APP))

    def test_the_dlc_of_the_licenses_in_steams_caches(self):
        write(os.path.join(self.steam, "appcache", "appinfo.vdf"), appinfo({100: APP, 200: APP, 300: 7, 500: APP}))
        licensed = {1: {APP}, 2: {100, 300}, 3: {200}}  # 500 is the game's, and nobody's here
        self.assertEqual(rs_install.cached_dlc(self.steam, lambda path: licensed), [100, 200])
        self.assertIsNone(rs_install.cached_dlc(self.steam, lambda path: {}))
        self.assertIsNone(rs_install.cached_dlc(os.path.join(self.tmp, "none"), lambda path: licensed))

    def test_the_ticket_counts_first(self):
        gamefiles = self.tools()
        write(os.path.join(self.steam, "appcache", "appinfo.vdf"), appinfo({100: APP, 200: APP}))
        with mock.patch.object(gamefiles, "package_apps", lambda path: {1: {100, 200}}):
            self.assertEqual(gamefiles.account_dlc(self.steam, 5), [100, 200])  # no ticket: the caches
            body = struct.pack("<IQIIIIII", 4, 76561197960265728 + 5, APP, 0, 0, 0, 0, 0)
            body += struct.pack("<H", 0) + struct.pack("<H", 1) + struct.pack("<IH", 100, 0)
            ticket = (struct.pack("<I", len(body) + 4) + body).hex()
            write(os.path.join(self.steam, "userdata", "5", "config", "localconfig.vdf"),
                  '"UserLocalConfigStore"\n{\n\t"apptickets"\n\t{\n\t\t"%d"\t\t"%s"\n\t}\n}\n' % (APP, ticket))
            self.assertEqual(gamefiles.account_dlc(self.steam, 5), [100])


class Tools(Base):
    def test_the_macs_tools_have_linux_where_they_said_mac(self):
        gamefiles = self.tools()
        self.assertIs(gamefiles.steam_content_dirs, rs_install.content_folders)
        self.assertIs(gamefiles.steam_running, rs_install.steam_running)
        self.assertIs(gamefiles.SteamGate(self.steam, "the game", gamefiles.Guide()).opener, rs_install.steam_open)
        c = gamefiles.Copier("mac", "/src", "/dest")
        self.assertIs(c.clone, rs_install.reflink)
        self.assertIsNone(c.placement)
        self.assertFalse(gamefiles.mac_game_pending(self.steam))
        self.assertEqual(gamefiles.mac_depot().game, rs_install.GAME_NAME)

    def test_no_step_is_worded_for_the_mac_app(self):
        gamefiles = self.tools()
        for code, (title, text) in gamefiles.GUIDE.items():
            for word in ("this Mac", "⌘", "Click", "click Stop", "Try Again", "installer", "Applications"):
                self.assertNotIn(word, title + " " + text, code)

    def test_a_clone_is_tried_across_device_numbers(self):
        gamefiles = self.tools()
        src = self.game(os.path.join(self.tmp, "game"))
        tried = []

        def clone(a, b):
            tried.append(b)
            shutil.copyfile(a, b)
            return True
        c = gamefiles.Copier("mac", src, os.path.join(self.home, "game"), clone=clone,
                             progress=gamefiles.Progress(stream=io.StringIO()))
        os.makedirs(os.path.join(self.home, "game", ".rearmed"))
        st = os.stat(os.path.join(src, "songs.psarc"))
        other = os.stat_result(tuple(st[:2]) + (st.st_dev + 1,) + tuple(st[3:]))  # as another subvolume's file
        self.assertTrue(c._clone_works(other, os.path.join(src, "songs.psarc")))
        self.assertEqual(len(tried), 1)
        self.assertFalse(os.path.lexists(tried[0]))  # the probe is gone
        self.assertTrue(c._clone_works(other, os.path.join(src, "songs.psarc")))
        self.assertEqual(len(tried), 1)  # once per device number


class Setup(Base):
    def test_a_game_folder_given(self):
        self.tools()
        src = self.game(os.path.join(self.tmp, "game"))
        self.assertEqual(self.setup("--game", src, "--no-download"), 0)
        self.assertTrue(rs_install.set_up(self.home))
        self.assertEqual(read(os.path.join(self.home, "image", rs_install.IMAGE_NAME)), "image of the game's program")
        game = os.path.join(self.home, "game")
        self.assertEqual(read(os.path.join(game, "songs.psarc")), "songs.psarc")
        self.assertEqual(read(os.path.join(game, "dlc", "song_m.psarc")), "mac song")
        self.assertFalse(os.path.exists(os.path.join(game, "dlc", "song_p.psarc")))  # the Windows archive
        self.assertFalse(os.path.exists(os.path.join(game, "Rocksmith2014.app")))    # the game's program: the image
        out = io.StringIO()
        with mock.patch.dict(os.environ, {"RS_STEAM_DIR": self.steam}):
            rs_install.status(mock.Mock(home=self.home), out)
        self.assertIn("the game's copy: complete", out.getvalue())
        self.assertIn("song archives in game/dlc: 1", out.getvalue())

    def test_a_folder_that_is_not_the_game(self):
        self.tools()
        with mock.patch("sys.stderr", io.StringIO()) as err:
            self.assertNotEqual(self.setup("--game", self.tmp, "--no-download"), 0)
        self.assertIn("not the Mac game's folder", err.getvalue())
        self.assertFalse(rs_install.set_up(self.home))

    def test_no_game_and_nothing_asked_of_steam(self):
        gamefiles = self.tools()
        with mock.patch.object(gamefiles, "steam_command", side_effect=AssertionError("Steam was asked")), \
                mock.patch("sys.stderr", io.StringIO()) as err:
            self.assertNotEqual(self.setup("--no-download"), 0)
        self.assertIn("download_depot 221680 221682", err.getvalue())  # what to enter in Steam's console

    def depot(self, gamefiles):
        """Steam's download of the made-up game, and a depot whose figures are that program's."""
        import hashlib
        top = self.game(os.path.join(self.steam, "steamrt64", "steamapps", "content", "app_%d" % APP, "depot_221682"))
        exe = os.path.join(top, rs_install.GAME_BINARY)
        cfg = {"steam_app": APP, "steam_depot": 221682, "depot_manifest": "", "depot_disk_bytes": 0,
               "disk_margin_bytes": 0, "exe_name": rs_install.GAME_BINARY.replace(os.sep, "/"),
               "exe_size": os.path.getsize(exe), "exe_sha256": hashlib.sha256(b"the game's program").hexdigest()}
        depot = gamefiles.Depot(cfg, lambda folder: exe if os.path.isfile(os.path.join(folder, cfg["exe_name"])) else None,
                                "the Mac game", "Downloading the Mac game", rs_install.GAME_NAME, "target.toml")
        p = mock.patch.object(gamefiles, "mac_depot", lambda target=None: depot)
        p.start()
        self.addCleanup(p.stop)
        return top

    def test_steams_download_with_the_dlc_of_its_depots(self):
        gamefiles = self.tools()
        self.depot(gamefiles)
        content = os.path.join(self.steam, "steamrt64", "steamapps", "content")
        write(os.path.join(content, "app_%d" % APP, "depot_258348", "dlc", "bought_m.psarc"), b"bought")
        write(os.path.join(content, "app_%d" % APP, "depot_258348", "dlc", "bought_p.psarc"), b"bought, for Windows")
        asked = []

        class Fetch:
            def __init__(self, steam, state, progress, guide, wait, game=None):
                asked.append((steam, os.path.basename(state), game))

            def run(self):
                return []
        os.environ["RS_DLC_FETCH"] = "1"
        with mock.patch.object(gamefiles, "DlcFetch", Fetch):
            self.assertEqual(self.setup(), 0)
        self.assertEqual(asked, [(self.steam, "dlc-fetched.json", rs_install.GAME_NAME)])
        self.assertEqual(read(os.path.join(self.home, "game", "dlc", "bought_m.psarc")), "bought")
        self.assertEqual(read(os.path.join(self.home, "game", "dlc", "song_m.psarc")), "mac song")
        # a refresh: the DLC Steam has since downloaded, asked for first
        write(os.path.join(content, "app_%d" % APP, "depot_258349", "dlc", "later_m.psarc"), b"later")
        with mock.patch.object(gamefiles, "DlcFetch", Fetch):
            self.assertEqual(rs_install.main(["refresh", "--home", self.home]), 0)
        self.assertEqual(len(asked), 2)
        self.assertEqual(read(os.path.join(self.home, "game", "dlc", "later_m.psarc")), "later")

    def test_the_dlc_left(self):
        gamefiles = self.tools()
        self.depot(gamefiles)
        os.environ["RS_DLC_FETCH"] = "1"
        with mock.patch.object(gamefiles, "DlcFetch", side_effect=AssertionError("the DLC was asked for")):
            self.assertEqual(self.setup("--no-dlc"), 0)
        self.assertTrue(rs_install.set_up(self.home))

    def test_a_game_that_runs_is_left_alone(self):
        self.tools()
        src = self.game(os.path.join(self.tmp, "game"))
        with mock.patch.object(rs_install, "game_running", lambda edition=None: True), \
                mock.patch("sys.stderr", io.StringIO()) as err:
            self.assertEqual(self.setup("--game", src), 1)
        self.assertIn("quit the game first", err.getvalue())
        self.assertFalse(os.path.exists(os.path.join(self.home, "game")))


class Ini(unittest.TestCase):
    def test_the_two_keys_are_forced(self):
        text = "[Audio]\nLatencyBuffer=4\n[Net]\nAutoLogin=1\nUseProxy=1\n[Renderer.MacOS]\nFullscreen=2\nDisableBrowser=0\n"
        self.assertEqual(rs_install.offline_ini(text),
                         "[Audio]\nLatencyBuffer=4\n[Net]\nAutoLogin=0\nUseProxy=1\n[Renderer.MacOS]\n"
                         "DisableBrowser=1\nFullscreen=2\n")

    def test_in_a_file_without_them_and_in_none(self):
        self.assertEqual(rs_install.offline_ini("[Audio]\nLatencyBuffer=4\n"),
                         "[Audio]\nLatencyBuffer=4\n[Net]\nAutoLogin=0\n[Renderer.MacOS]\nDisableBrowser=1\n")
        self.assertEqual(rs_install.offline_ini(""), "[Net]\nAutoLogin=0\n[Renderer.MacOS]\nDisableBrowser=1\n")

    def test_the_games_own_line_ends_and_a_second_pass(self):
        once = rs_install.offline_ini("[Net]\r\nAutoLogin=1\r\n[Renderer.MacOS]\r\nDisableBrowser=0\r\n")
        self.assertEqual(once, "[Net]\nAutoLogin=0\n[Renderer.MacOS]\nDisableBrowser=1\n")
        self.assertEqual(rs_install.offline_ini(once), once)

    def test_a_key_of_that_name_in_another_section_stays(self):
        text = "[Other]\nAutoLogin=1\n[Net]\nAutoLogin=1\n"
        self.assertIn("[Other]\nAutoLogin=1\n", rs_install.offline_ini(text))

    def test_the_output_buffer_when_one_is_given(self):
        self.assertEqual(rs_install.offline_ini("[Audio]\nLatencyBuffer=4\nEnableMicrophone=0\n", 2),
                         "[Audio]\nLatencyBuffer=2\nEnableMicrophone=0\n[Net]\nAutoLogin=0\n[Renderer.MacOS]\n"
                         "DisableBrowser=1\n")


class Start(Base):
    def made(self):
        self.tools()
        self.assertEqual(self.setup("--game", self.game(os.path.join(self.tmp, "game")), "--no-download"), 0)
        return os.path.join(self.home, "game")

    def test_the_environment_of_a_start(self):
        game = self.made()
        write(os.path.join(self.home, "settings.env"), "# switches\nRS_MODS_INTRO=0\nPATH=/nowhere\nRS_FPS = 120\n")
        write(os.path.join(self.home, "options.env"), "RS_AUDIO_FRAMES=128\nRS_MODS_LOG=1\n")
        write(os.path.join(game, ".rearmed", "steam.env"), "# facts\nRS_STEAM_DLC=1,2\nRS_STEAM_LANGUAGE=german\n")
        env, notes = rs_install.prepare(self.home, self.steam, {"PATH": "/bin", "RS_MODS_LOG": "0", "RS_STEAM_LANGUAGE": "french"})
        self.assertEqual(env["PATH"], "/bin")  # only RS_ keys are taken from the files
        self.assertEqual((env["RS_MODS_INTRO"], env["RS_FPS"], env["RS_AUDIO_FRAMES"]), ("0", "120", "128"))
        self.assertEqual(env["RS_MODS_LOG"], "0")  # the environment wins, and the log says so
        self.assertIn("[options] RS_MODS_LOG=1 not used: the environment has RS_MODS_LOG=0", notes)
        self.assertEqual((env["RS_STEAM_DLC"], env["RS_STEAM_LANGUAGE"]), ("1,2", "french"))
        self.assertEqual(env["RS_NATIVE_HOME"], self.home)
        self.assertEqual(env["RS_STEAM_DATA_DIR"], os.path.join(self.home, "steam"))

    def test_the_ini_is_offline_and_keeps_what_the_game_saved(self):
        game = self.made()
        self.assertFalse(os.path.exists(os.path.join(game, "Rocksmith.ini")))  # the copy keeps the install's aside
        rs_install.prepare(self.home, self.steam, {})
        self.assertEqual(read(os.path.join(game, "Rocksmith.ini")),
                         "[Audio]\nLatencyBuffer=4\n[Net]\nAutoLogin=0\n[Renderer.MacOS]\nDisableBrowser=1\n")
        write(os.path.join(game, "Rocksmith.ini"), "[Audio]\nLatencyBuffer=2\n[Net]\nAutoLogin=1\n")
        rs_install.prepare(self.home, self.steam, {"RS_LATENCY_BUFFER": "99"})  # not a size the game has: left
        self.assertEqual(read(os.path.join(game, "Rocksmith.ini")),
                         "[Audio]\nLatencyBuffer=2\n[Net]\nAutoLogin=0\n[Renderer.MacOS]\nDisableBrowser=1\n")
        rs_install.prepare(self.home, self.steam, {"RS_LATENCY_BUFFER": "3"})
        self.assertIn("LatencyBuffer=3\n", read(os.path.join(game, "Rocksmith.ini")))

    def test_the_saves_start_from_steams_without_the_stored_sign_in(self):
        self.made()
        remote = os.path.join(self.steam, "userdata", "77", str(APP), "remote")
        write(os.path.join(remote, "abc_PRFLDB"), b"profile")
        write(os.path.join(remote, "LocalProfiles.json"), b"{}")
        write(os.path.join(remote, "crd"), b"sign-in")
        write(os.path.join(self.steam, "userdata", "77", "620", "remote", "other"), b"another game's")
        rs_install.prepare(self.home, self.steam, {})
        mine = os.path.join(self.home, "steam", "77", str(APP), "remote")
        self.assertEqual(sorted(os.listdir(mine)), ["LocalProfiles.json", "abc_PRFLDB"])
        self.assertEqual(os.listdir(os.path.join(self.home, "steam")), ["77"])
        self.assertIn("without crd", read(os.path.join(self.home, "logs", "seed.log")))
        # once: the saves are RS Re-ARMed's own from then on, and Steam's are never written
        write(os.path.join(mine, "abc_PRFLDB"), b"played here")
        write(os.path.join(remote, "abc_PRFLDB"), b"played in Steam")
        write(os.path.join(mine, "crd"), b"left by something")
        rs_install.prepare(self.home, self.steam, {})
        self.assertEqual(read(os.path.join(mine, "abc_PRFLDB")), "played here")
        self.assertFalse(os.path.exists(os.path.join(mine, "crd")))
        self.assertEqual(read(os.path.join(remote, "crd")), "sign-in")

    def test_no_seeding_when_switched_off(self):
        self.made()
        write(os.path.join(self.steam, "userdata", "77", str(APP), "remote", "abc_PRFLDB"), b"profile")
        rs_install.prepare(self.home, self.steam, {"RS_NATIVE_SEED": "0"})
        self.assertEqual(os.listdir(os.path.join(self.home, "steam")), [])

    def test_the_title_logo_is_the_one_that_is_there(self):
        game = self.made()
        link = os.path.join(game, "static.psarc")
        rs_install.prepare(self.home, self.steam, {})
        self.assertEqual(os.readlink(link), "static.psarc.original")
        write(os.path.join(game, "static.psarc.rearmed"), b"with the logo")
        rs_install.prepare(self.home, self.steam, {})
        self.assertEqual(read(link), "with the logo")
        rs_install.prepare(self.home, self.steam, {"RS_REARMED": "0"})
        self.assertEqual(read(link), "static.psarc")

    def test_no_copy_no_start(self):
        with self.assertRaises(rs_install.Failure) as e:
            rs_install.prepare(self.home, self.steam, {})
        self.assertEqual(e.exception.code, 3)
        os.makedirs(os.path.join(self.home, "game", ".rearmed"))  # a copy that was stopped
        with self.assertRaises(rs_install.Failure) as e:
            rs_install.prepare(self.home, self.steam, {})
        self.assertIn("incomplete", str(e.exception))

    def test_where_output_goes_nowhere(self):
        with open(os.devnull, "w") as null, open(os.path.join(self.tmp, "file"), "w") as real:
            self.assertTrue(rs_install.goes_nowhere(null.fileno()))
            self.assertFalse(rs_install.goes_nowhere(real.fileno()))
        r, w = os.pipe()
        self.assertFalse(rs_install.goes_nowhere(w))
        os.close(r)
        os.close(w)
        self.assertTrue(rs_install.goes_nowhere(w))  # closed

    def program(self):
        """A stand-in for rs-rearmed: what it was started with, to a file; it stays until told to end."""
        said = os.path.join(self.tmp, "started")
        exe = write(os.path.join(self.tmp, "rs-rearmed-stand-in"),
                    "#!/bin/sh\necho \"args: $@\"\necho \"home: $RS_NATIVE_HOME\" > \"%s\"\necho \"args: $@\" >> \"%s\"\n"
                    "while [ ! -e \"%s.end\" ]; do sleep 0.05; done\n" % (said, said, said))
        os.chmod(exe, 0o755)
        return exe, said

    def test_the_start_runs_the_program_with_the_image_and_the_copy(self):
        game = self.made()
        exe, said = self.program()
        env = dict(os.environ, RS_REARMED_BIN=exe, RS_STEAM_DIR=self.steam)
        argv = [sys.executable, os.path.join(HERE, "rs_install.py"), "--home", self.home]
        first = subprocess.Popen(argv + ["run", "--", "--frames", "5"], env=env, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
        self.addCleanup(first.kill)
        for _ in range(200):
            if os.path.exists(said) and len(read(said).splitlines()) == 2:
                break
            time.sleep(0.05)
        self.assertEqual(read(said).splitlines(),
                         ["home: %s" % self.home, "args: --image %s --run %s --frames 5"
                          % (os.path.join(self.home, "image", rs_install.IMAGE_NAME), game)])
        # one game at a time, while the first one runs
        second = subprocess.run(argv, env=env, capture_output=True, text=True)
        self.assertEqual(second.returncode, 1)
        self.assertIn("already running", second.stderr)
        write(said + ".end")
        self.assertEqual(first.wait(timeout=20), 0)
        # output that went nowhere is in the log
        self.assertIn("args: --image", read(os.path.join(self.home, "logs", "last-run.log")))
        # and with the first one over, the app's own start (no command) runs the game
        os.unlink(said)
        third = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=20)
        self.assertEqual(third.returncode, 0, third.stderr)
        self.assertIn("args: --image", third.stdout)


class Window(Base):
    """The setup's progress in zenity's window, with a stand-in for zenity that keeps what it is told."""

    def zenity(self, body):
        bin_dir = os.path.join(self.tmp, "bin")
        exe = write(os.path.join(bin_dir, "zenity"), "#!/bin/sh\n" + body)
        os.chmod(exe, 0o755)
        os.environ["PATH"] = bin_dir + os.pathsep + os.environ["PATH"]
        return os.path.join(self.tmp, "told")

    def child(self, lines, code=0, then=""):
        script = write(os.path.join(self.tmp, "setup.sh"),
                       "#!/bin/sh\n" + "".join("printf '%s\\n'\n" % line for line in lines) + then + "exit %d\n" % code)
        return ["/bin/sh", script]

    def test_the_bar_the_step_and_what_to_do(self):
        told = self.zenity('cat > "%s"\n' % os.path.join(self.tmp, "told"))
        with mock.patch("sys.stderr", io.StringIO()) as err:
            code, said = rs_install.setup_in_window([], self.child([
                "progress 0.2500 Downloading the Mac game… 1 of 4 GB",
                "guide\\tsign-in\\tSign in to Steam\\tSteam is open but nobody is signed in. Use <your> account & password.",
                "The Mac game: /somewhere",
                "guide\\t-",
                "progress 1.0000 Copying the game files…"]))
        self.assertEqual((code, said), (0, "The Mac game: /somewhere"))
        self.assertIn("The Mac game: /somewhere", err.getvalue())
        self.assertEqual(read(told).splitlines(), [
            "25", "# Downloading the Mac game… 1 of 4 GB",
            "# Downloading the Mac game… 1 of 4 GB\\n\\nSign in to Steam. Steam is open but nobody is signed in. "
            "Use &lt;your&gt; account &amp; password.",
            "# Downloading the Mac game… 1 of 4 GB",
            "99", "# Copying the game files…", "100"])

    def test_a_setup_that_fails_says_why(self):
        self.zenity("cat > /dev/null\n")
        with mock.patch("sys.stderr", io.StringIO()):
            code, said = rs_install.setup_in_window([], self.child(["progress 0.1000 Looking", "error: no room"], 3))
        self.assertEqual((code, said), (3, "error: no room"))

    def test_cancel_stops_the_setup(self):
        self.zenity("exit 1\n")  # the window closed at once
        with mock.patch("sys.stderr", io.StringIO()):
            code, _ = rs_install.setup_in_window([], self.child(
                ["progress 0.1000 Downloading"], 0, "sleep 0.5\nprintf 'progress 0.2000 Downloading\\n'\nsleep 30\n"))
        self.assertEqual(code, 130)


if __name__ == "__main__":
    unittest.main()
