#!/usr/bin/env python3
"""The settings and switches of RS Re-ARMed on Linux: what the installer's "Settings and switches" page offers, and
the two files it writes, which every start of the game reads (rs_install.py).

The lists are the Mac installer's (tools/installer/RSReARMedInstaller.swift: builtInMods and optionSpecs), with the
same keys, titles and order, so that a switch means one thing on every system; test_rs_settings.py holds them to
the Swift source. What Linux adds to each option is whether the Linux build has it:

    yes     it works here as on the Mac
    soon    the Linux build does not have it yet: the page shows it, greyed, so that what is still to come is in
            plain sight
    no      it is about something Linux has not got (Metal, Apple's OpenGL, Direct3D): the page leaves it out

The files are the Mac's, line for line: settings.env holds the built-in mods switched off (KEY=0), options.env the
options set to something other than their default (KEY=value). No file is every default. The first line of a key
counts, a toggle is 0 or 1, and the environment wins over both.
"""
import collections
import os
import re

Mod = collections.namedtuple("Mod", "key title detail")
Option = collections.namedtuple("Option", "key title detail kind group advanced linux")

# The built-in mods a player can switch off, each on unless settings.env says KEY=0.
MODS = (
    Mod("RS_MODS_BYPASS", "Custom songs (CDLC)",
        "Songs without the game's signature load, as with RSBypass. Off: custom songs are not listed."),
    Mod("RS_MODS_FIXES", "Faster loading and other fixes (rs-mac-fixes)",
        "Large song libraries load faster; more keyboard keys, the End key in menus, more open files."),
    Mod("RS_MODS_INTRO", "Skip the intro screens", "The logos and the intro video at start-up."),
    Mod("RS_MODS_AUTOSAVE", "Skip the Auto Save warning", "The notice after choosing a profile."),
    Mod("RS_MODS_OVERLAY", "Menu additions",
        "DIRECT CONNECT for audio interfaces, ENUMERATE in place of SHOP, EXIT GAME in place of UPLAY."),
    Mod("RS_MODS_MULTIPLAYER", "Two players on any input",
        "Off: Player 2 needs a Real Tone cable, as in the original game."),
    Mod("RS_MODS_GUITARCADE_SKIP", "Skip Tuner in the Guitarcade",
        "Off: a Guitarcade game's tuner can't be skipped, as in the original game."),
    Mod("RS_MODS_FRAME_RATE", "Calibration and the tuner the same at any frame rate",
        "Off: they count in frames, as in the original game, which goes wrong above 60 Hz."),
    Mod("RS_REARMED", "Re-ARMed title logo", "Off: the game's own logo."),
)


# An option's kind: ("toggle", on by default), ("choice", ((title, value), ...)) with "" the default, or
# ("text", check, hint) where check(value) says whether a value is one the game takes and empty is the default.
def toggle(default_on):
    return ("toggle", default_on)


def choice(*pairs):
    return ("choice", pairs)


def text(check, hint):
    return ("text", check, hint)


def int_range(lo, hi):
    return lambda s: bool(re.fullmatch(r"[0-9]+", s)) and lo <= int(s) <= hi


def num_range(lo, hi):
    def check(s):
        try:
            return bool(re.fullmatch(r"-?[0-9]+(\.[0-9]+)?", s)) and lo <= float(s) <= hi
        except ValueError:
            return False
    return check


def full_match(pattern):
    return lambda s: re.fullmatch(pattern, s) is not None


AUDIO, PICTURE, SONGS, STARTUP = "Audio and guitar input", "Picture", "Songs and Steam account", "Startup and logging"

# In the Mac installer's order (its optionSpecs), every one of them: the Mac launcher takes them all from the file,
# and a file brought over from a Mac keeps its lines.
OPTIONS = (
    Option("RS_AUDIO_FRAMES", "Output device buffer", "Frames per callback; 0 leaves the device's. Default 256.",
           text(int_range(0, 8192), "256"), AUDIO, False, "yes"),
    Option("RS_LATENCY_BUFFER", "Latency buffer",
           "The game's output buffer in steps of 256 frames, 1-16. Empty: the game's saved choice.",
           text(int_range(1, 16), "1-16"), AUDIO, False, "yes"),
    Option("RS_AUDIO_RT", "Real-time audio thread", "The audio thread runs in the device's workgroup. Off: as the original.",
           toggle(True), AUDIO, True, "no"),
    Option("RS_AUDIO_INPUT_TRIM", "Drop stale guitar input",
           "Up to 8 ms of old input never reaches the speakers. Off: as the original.", toggle(True), AUDIO, True, "yes"),
    Option("RS_AUDIO_INPUT", "Guitar input fixes", "The channel, gain and list below. Off: the game's input as it is.",
           toggle(True), AUDIO, True, "yes"),
    Option("RS_AUDIO_INPUT_CHANNEL", "Input channel", "From 1, for every device. Empty: every channel is listed.",
           text(int_range(1, 256), "1-256"), AUDIO, False, "yes"),
    Option("RS_AUDIO_INPUT_GAIN_DB", "Input gain", "Fixed gain in dB, -24 to 36. Empty: none.",
           text(num_range(-24, 36), "dB"), AUDIO, False, "yes"),
    Option("RS_AUDIO_INPUT_FILE", "Guitar input file",
           "Another per-device Channel and GainDb file. Empty: guitar-input.ini.", text(None, "path"), AUDIO, True, "yes"),
    Option("RS_AUDIO_INPUT_LEVEL", "Calibration boost",
           "Calibration can boost a quiet interface by up to 24 dB. Off: the level stays the device's own volume.",
           toggle(True), AUDIO, True, "yes"),
    Option("RS_AUDIO_CHANNEL_ENTRIES", "Input list length",
           "How many inputs are listed; 0: one entry per device. Default 10.", text(int_range(0, 256), "10"), AUDIO, True,
           "yes"),
    Option("RS_AUDIO_SET_RATE", "Set devices to 48 kHz", "Off: devices keep their sample rate.", toggle(True), AUDIO, True,
           "no"),
    Option("RS_AUDIO_RS_NAMES", "Rocksmith device names", "Only the cable is called Rocksmith. Off: CoreAudio's names.",
           toggle(True), AUDIO, True, "no"),
    Option("RS_AUDIO_STABLE_UIDS", "Stable device identities",
           "An interface isn't lost when another device fails to open. Off: as the original.", toggle(True), AUDIO, True,
           "no"),
    Option("RS_AUDIO_INPUTS_FIRST", "Inputs listed first",
           "An unreadable output-only device can't end the device scan. Off: as the original.", toggle(True), AUDIO, True,
           "no"),
    Option("RS_FPS", "Frame rate", "Frames per second; a fixed rate turns vsync off. Empty: follows the display.",
           text(int_range(1, 1000), "60"), PICTURE, False, "yes"),
    Option("RS_WINDOW_ANY_SHAPE", "Window takes any shape", "Off: 16:9 with bars, as on Windows.", toggle(True), PICTURE,
           False, "yes"),
    Option("RS_GL_MTENGINE", "Multithreaded GL engine", "OpenGL only, off by default. On: Apple's multithreaded engine.",
           toggle(False), PICTURE, True, "yes"),
    Option("RS_METAL_RETINA", "Full Retina resolution",
           "Renders every pixel, about 4x the GPU work. Off: scaled up with MetalFX.", toggle(False), PICTURE, False, "no"),
    Option("RS_METAL_SCALE", "Render scale",
           "Less GPU work for a softer picture, menus and text too; scaled up with MetalFX.",
           choice(("Default (100%)", ""), ("90%", "90"), ("75%", "75"), ("50%", "50")), PICTURE, True, "no"),
    Option("RS_METAL_UPSCALE", "MetalFX upscaling",
           "Off: a softer picture on a Retina screen for clearly less GPU work (a plain bilinear scale).", toggle(True),
           PICTURE, False, "no"),
    Option("RS_METAL_COLORS", "Colour", "Vivid leaves the picture untagged: more saturated on wide-colour screens.",
           choice(("Default (sRGB)", ""), ("Vivid", "vivid")), PICTURE, True, "no"),
    Option("RS_METAL_ANISO", "Texture filtering", "Least anisotropic filtering, 1-16; 1 keeps the game's.",
           choice(("Default (16x)", ""), ("1x (the game's)", "1"), ("2x", "2"), ("4x", "4"), ("8x", "8"), ("16x", "16")),
           PICTURE, True, "no"),
    Option("RS_METAL_MSAA", "Anti-aliasing", "MSAA on the game's 3D picture.",
           choice(("Default (4x)", ""), ("Off", "1"), ("2x", "2"), ("4x", "4"), ("8x", "8")), PICTURE, False, "no"),
    Option("RS_METAL_DRAWABLES", "Low-latency presentation",
           "2 frames queued instead of 3: each one shows a refresh sooner, a late one as a repeat.",
           choice(("Default (3 with vsync)", ""), ("On (2)", "2")), PICTURE, True, "no"),
    Option("RS_METAL_HUD", "Performance overlay",
           "Apple's Metal HUD over the game (macOS 13 and later): frame rate, frame times, GPU memory.", toggle(False),
           PICTURE, False, "no"),
    Option("RS_FULLSCREEN", "Window mode", "Whatever the game's setting, or forced.",
           choice(("Default (the game's setting)", ""), ("Window", "0"), ("Full screen", "1")), PICTURE, False, "yes"),
    Option("RS_D3D9_DISPLAY", "Desktop size", "As in 1920x1080. Empty: the screen's.",
           text(full_match(r"[0-9]+x[0-9]+"), "1920x1080"), PICTURE, True, "no"),
    Option("RS_STEAM_DLC", "The game's DLC list (for tests)",
           "AppIDs like 248750,258341 that count as the game's DLC, in place of the copy's list. Leave empty: the copy's "
           "list has your DLC and every song in the copy.", text(None, "AppIDs"), SONGS, True, "yes"),
    Option("RS_STEAM_OWNED", "First Rocksmith DLC list (for tests)",
           "AppIDs of first Rocksmith DLC that count as owned, added to the ones a copy or refresh found. Leave empty: "
           "the copy finds the DLC you own.", text(None, "AppIDs"), SONGS, True, "yes"),
    Option("RS_STEAM_LANGUAGE", "Language", "As in german. Empty: the copy's.", text(None, "english"), SONGS, True, "yes"),
    Option("RS_STEAM_ID", "Steam account", "The account whose saves and profiles play. Empty: the copy's.",
           text(full_match(r"[0-9]+"), "SteamID64"), SONGS, True, "yes"),
    Option("RS_STEAM_PERSONA", "Player name", "Empty: the name of the account Steam last logged in, else Player.",
           text(None, "Player"), SONGS, True, "yes"),
    Option("RS_MODS_BUDGET_MS", "DLC mounting budget",
           "Ms per frame for mounting archives; 0 mounts one a frame. Default 50.", text(int_range(0, 1000), "50"),
           STARTUP, False, "yes"),
    Option("RS_MODS_LOG", "Log the built-in fixes", "To last-run.log.", toggle(False), STARTUP, True, "yes"),
    Option("RS_LOAD_MODS", "Load mods at startup", "Off: starts without the mods in mods/.", toggle(True), STARTUP, False,
           "soon"),
    Option("RS_AUDIO_LOG", "Log the audio setup", "To last-run.log.", toggle(False), STARTUP, True, "yes"),
    Option("RS_STEAM_LOG", "Log the Steam layer", "To last-run.log.", toggle(False), STARTUP, True, "yes"),
    Option("RS_FILE_LOG", "Log files mods serve", "To last-run.log.", toggle(False), STARTUP, True, "soon"),
)

# Where a detail's words are the Mac's: the same switch in Linux's.
LINUX_DETAIL = {
    "RS_AUDIO_FRAMES": "Frames per callback asked of PipeWire, which may give another size. Default 256.",
    "RS_WINDOW_ANY_SHAPE": "Off: a window keeps 16:9, as the original game's.",
    "RS_AUDIO_INPUT_FILE": "Another per-device Channel and GainDb file. Empty: guitar-input.ini in RS Re-ARMed's folder.",
    "RS_GL_MTENGINE": "Off by default. On: Mesa's multithreaded OpenGL, the drawing calls on a thread of the driver's.",
    "RS_FULLSCREEN": "Whatever the game's Windowed Mode setting, or forced at each start. F11 or Alt+Enter changes it "
                     "while the game runs.",
}
GROUPS = (AUDIO, PICTURE, SONGS, STARTUP)
MOD_KEYS = tuple(m.key for m in MODS)
OPTION = {o.key: o for o in OPTIONS}
SOON = "Not in the Linux build yet."


def detail(option):
    return LINUX_DETAIL.get(option.key, option.detail)


def shown():
    """The options the page shows: all but those about what Linux has not got."""
    return [o for o in OPTIONS if o.linux != "no"]


def valid(option, value):
    """VALUE is one the option takes ("" is always: the default)."""
    if value == "":
        return True
    kind = option.kind
    if kind[0] == "toggle":
        return value in ("0", "1")
    if kind[0] == "choice":
        return value in [v for _, v in kind[1]]
    return kind[1] is None or bool(kind[1](value))


def settings_file(home):
    return os.path.join(home, "settings.env")


def options_file(home):
    return os.path.join(home, "options.env")


def _lines(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().split("\n")
    except OSError:
        return []


def read_mods_off(home):
    """The built-in mods settings.env switches off, read as the launchers read it: whitespace ignored, # a comment,
    KEY=0 or KEY=1 of the mods' keys, the first of a key counts."""
    seen, off = set(), set()
    for raw in _lines(settings_file(home)):
        line = "".join(raw.split())
        if line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        if key not in MOD_KEYS or value not in ("0", "1") or key in seen:
            continue
        seen.add(key)
        if value == "0":
            off.add(key)
    return off


def save_mods_off(home, off):
    """The whole file: the mods switched off, or no file when none is."""
    path = settings_file(home)
    off = [k for k in MOD_KEYS if k in off]
    if not off:
        if os.path.exists(path):
            os.unlink(path)
        return
    os.makedirs(home, exist_ok=True)
    _write(path, "# The built-in mods switched off, written by the RS Re-ARMed Installer (Settings and switches…) and "
                 "read at every\n# start of the game. KEY=0 is off; the switches are in docs/mac/INSTALL.md, "
                 "\"Settings and switches\".\n" + "".join("%s=0\n" % k for k in off))


def read_options(home):
    """options.env's values by key: ends trimmed, # a comment, the first of a key counts, a toggle 0 or 1 (any
    other value of one did nothing, and is not kept: saving it would turn it into "off")."""
    out = {}
    for raw in _lines(options_file(home)):
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if key not in OPTION or not value or key in out:
            continue
        if OPTION[key].kind[0] == "toggle" and value not in ("0", "1"):
            continue
        out[key] = value
    return out


def save_options(home, values):
    """The whole file: the options set to something other than their default, or no file."""
    path = options_file(home)
    values = {k: v for k, v in values.items() if k in OPTION and v != ""}
    if not values:
        if os.path.exists(path):
            os.unlink(path)
        return
    os.makedirs(home, exist_ok=True)
    _write(path, "# The options set in the RS Re-ARMed Installer (Settings and switches…) and read at every\n# start of "
                 "the game. Each line is KEY=value; the switches are in docs/mac/INSTALL.md, \"Settings and switches\".\n"
           + "".join("%s=%s\n" % (o.key, values[o.key]) for o in OPTIONS if o.key in values))


def _write(path, content):
    with open(path + ".new", "w", encoding="utf-8") as f:
        f.write(content)
    os.replace(path + ".new", path)


def start_environment(home, env):
    """What a start adds to the environment ENV from the two files, as the Mac launcher does: (the new environment,
    a log line for each line used or not). A key the environment has with another value keeps that value."""
    env, notes = dict(env), []
    from_settings = [(k, "0" if k in read_mods_off(home) else None) for k in MOD_KEYS]
    for name, pairs in (("settings", [(k, v) for k, v in from_settings if v is not None]),
                        ("options", [(o.key, read_options(home)[o.key]) for o in OPTIONS if o.key in read_options(home)])):
        for key, value in pairs:
            if key in env and env[key] != value:
                notes.append("[%s] %s=%s not used: the environment has %s=%s" % (name, key, value, key, env[key]))
            else:
                env[key] = value
                notes.append("[%s] %s=%s" % (name, key, value))
    return env, notes
