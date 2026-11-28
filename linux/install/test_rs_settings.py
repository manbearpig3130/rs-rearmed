#!/usr/bin/env python3
"""linux/install/rs_settings.py: the installer's Settings and switches on Linux. Its lists against the Mac
installer's own (tools/installer/RSReARMedInstaller.swift), and the two files it reads and writes.

    python3 linux/install/test_rs_settings.py
"""
import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rs_settings  # noqa: E402

SWIFT = os.path.join(HERE, "..", "..", "tools", "installer", "RSReARMedInstaller.swift")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class TheMacsLists(unittest.TestCase):
    def setUp(self):
        if not os.path.exists(SWIFT):
            self.skipTest("the Mac installer's source is not here (a packaged build)")
        self.swift = read(SWIFT)

    def test_the_built_in_mods_are_the_macs(self):
        mods = re.findall(r'BuiltInMod\(key: "(\w+)", title: "([^"]+)",\s*detail: "([^"]+)"', self.swift)
        self.assertEqual(len(mods), 9)
        self.assertEqual([(m.key, m.title, m.detail) for m in rs_settings.MODS], mods)

    def test_the_options_are_the_macs_in_their_order(self):
        specs = re.findall(r'OptionSpec\(key: "(\w+)", title: "([^"]+)", detail: "([^"]*)",\s*kind: \.(\w+)\(.*?'
                           r'editions: "(\w+)", group: "([^"]+)", advanced: (true|false)\)', self.swift, re.S)
        self.assertGreater(len(specs), 35)
        mine = [(o.key, o.title, o.detail, o.kind[0], o.group, "true" if o.advanced else "false") for o in rs_settings.OPTIONS]
        self.assertEqual(mine, [(k, t, d, kind, g, a) for k, t, d, kind, _, g, a in specs])

    def test_what_is_only_learn_and_plays_or_apples_is_not_called_there(self):
        editions = dict(re.findall(r'OptionSpec\(key: "(\w+)".*?editions: "(\w+)"', self.swift, re.S))
        for o in rs_settings.OPTIONS:
            if o.key.startswith(("RS_METAL_", "RS_D3D9_")):
                self.assertEqual(o.linux, "no", o.key)
            if editions[o.key] == "lp" and o.key != "RS_FULLSCREEN":  # (Linux has a window mode to choose too)
                self.assertEqual(o.linux, "no", o.key)

    def test_the_toggles_defaults_and_the_choices_are_the_macs(self):
        for key, on in re.findall(r'OptionSpec\(key: "(\w+)"[^\n]*\n\s*kind: \.toggle\(defaultOn: (true|false)\)', self.swift):
            self.assertEqual(rs_settings.OPTION[key].kind, ("toggle", on == "true"), key)
        for key, body in re.findall(r'OptionSpec\(key: "(\w+)"[^\n]*\n\s*kind: \.choice\(\[(.*?)\]\)', self.swift):
            self.assertEqual(list(rs_settings.OPTION[key].kind[1]), re.findall(r'\("([^"]*)", "([^"]*)"\)', body), key)


class Lists(unittest.TestCase):
    def test_every_option_says_whether_linux_has_it(self):
        for o in rs_settings.OPTIONS:
            self.assertIn(o.linux, ("yes", "soon", "no"), o.key)
            self.assertIn(o.group, rs_settings.GROUPS)
        self.assertEqual(len({o.key for o in rs_settings.OPTIONS}), len(rs_settings.OPTIONS))
        self.assertTrue(all(o.linux != "no" for o in rs_settings.shown()))
        self.assertIn("RS_AUDIO_FRAMES", [o.key for o in rs_settings.shown()])
        self.assertNotIn("RS_METAL_HUD", [o.key for o in rs_settings.shown()])

    def test_no_detail_shown_names_the_macs_things(self):
        for o in rs_settings.shown():
            for word in ("Metal", "launchctl", "CoreAudio", "Retina", "Apple"):
                self.assertNotIn(word, rs_settings.detail(o), o.key)

    def test_the_values_an_option_takes(self):
        v, o = rs_settings.valid, rs_settings.OPTION
        self.assertTrue(v(o["RS_AUDIO_FRAMES"], "128") and v(o["RS_AUDIO_FRAMES"], "0") and v(o["RS_AUDIO_FRAMES"], ""))
        self.assertFalse(v(o["RS_AUDIO_FRAMES"], "9000") or v(o["RS_AUDIO_FRAMES"], "-1") or v(o["RS_AUDIO_FRAMES"], "x"))
        self.assertTrue(v(o["RS_AUDIO_INPUT_GAIN_DB"], "-12.5") and v(o["RS_AUDIO_INPUT_GAIN_DB"], "36"))
        self.assertFalse(v(o["RS_AUDIO_INPUT_GAIN_DB"], "40") or v(o["RS_AUDIO_INPUT_GAIN_DB"], "loud"))
        self.assertTrue(v(o["RS_FULLSCREEN"], "1") and not v(o["RS_FULLSCREEN"], "2"))
        self.assertTrue(v(o["RS_MODS_LOG"], "1") and not v(o["RS_MODS_LOG"], "yes"))
        self.assertTrue(v(o["RS_STEAM_ID"], "76561197960265729") and not v(o["RS_STEAM_ID"], "me"))
        self.assertTrue(v(o["RS_STEAM_PERSONA"], "Any Name At All"))


class Files(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp(prefix="rs-settings-test-")
        self.addCleanup(shutil.rmtree, self.home, True)

    def write(self, name, text):
        with open(os.path.join(self.home, name), "w", encoding="utf-8") as f:
            f.write(text)

    def test_the_mods_switched_off(self):
        self.assertEqual(rs_settings.read_mods_off(self.home), set())
        rs_settings.save_mods_off(self.home, {"RS_REARMED", "RS_MODS_INTRO"})
        text = read(rs_settings.settings_file(self.home))
        self.assertTrue(text.endswith("RS_MODS_INTRO=0\nRS_REARMED=0\n"))  # in the page's order
        self.assertTrue(text.startswith("# "))
        self.assertEqual(rs_settings.read_mods_off(self.home), {"RS_REARMED", "RS_MODS_INTRO"})
        rs_settings.save_mods_off(self.home, set())
        self.assertFalse(os.path.exists(rs_settings.settings_file(self.home)))  # every mod on: no file

    def test_the_settings_file_as_the_launchers_read_it(self):
        self.write("settings.env", " RS_MODS_INTRO = 0 \n# RS_MODS_BYPASS=0\nRS_MODS_FIXES=1\nRS_MODS_FIXES=0\n"
                                   "RS_MODS_OVERLAY=off\nRS_FPS=0\nPATH=0\n")
        self.assertEqual(rs_settings.read_mods_off(self.home), {"RS_MODS_INTRO"})

    def test_the_options(self):
        self.assertEqual(rs_settings.read_options(self.home), {})
        rs_settings.save_options(self.home, {"RS_MODS_LOG": "1", "RS_AUDIO_FRAMES": "128", "RS_FPS": "", "RS_NONE": "1"})
        text = read(rs_settings.options_file(self.home))
        self.assertTrue(text.endswith("RS_AUDIO_FRAMES=128\nRS_MODS_LOG=1\n"))
        self.assertEqual(rs_settings.read_options(self.home), {"RS_AUDIO_FRAMES": "128", "RS_MODS_LOG": "1"})
        rs_settings.save_options(self.home, {})
        self.assertFalse(os.path.exists(rs_settings.options_file(self.home)))

    def test_the_options_file_as_the_launchers_read_it(self):
        self.write("options.env", "# a note\n  RS_STEAM_PERSONA = A Name With Spaces  \nRS_MODS_LOG=yes\nRS_AUDIO_LOG=1\n"
                                  "RS_AUDIO_LOG=0\nRS_FPS=\nRS_UNKNOWN=1\nRS_METAL_HUD=1\n")
        self.assertEqual(rs_settings.read_options(self.home),
                         {"RS_STEAM_PERSONA": "A Name With Spaces", "RS_AUDIO_LOG": "1", "RS_METAL_HUD": "1"})

    def test_a_starts_environment(self):
        self.write("settings.env", "RS_MODS_INTRO=0\nRS_MODS_BYPASS=1\n")
        self.write("options.env", "RS_AUDIO_FRAMES=128\nRS_MODS_LOG=1\n")
        env, notes = rs_settings.start_environment(self.home, {"RS_MODS_LOG": "0", "HOME": "/h"})
        self.assertEqual(env, {"RS_MODS_LOG": "0", "HOME": "/h", "RS_MODS_INTRO": "0", "RS_AUDIO_FRAMES": "128"})
        self.assertEqual(notes, ["[settings] RS_MODS_INTRO=0", "[options] RS_AUDIO_FRAMES=128",
                                 "[options] RS_MODS_LOG=1 not used: the environment has RS_MODS_LOG=0"])


if __name__ == "__main__":
    unittest.main()
