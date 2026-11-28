#!/usr/bin/env python3
"""RS Re-ARMed on Linux: the game's files set up once, and the game started from them.

    rs_install.py                    start the game, after a setup when there has been none (what the app does)
    rs_install.py setup              find the Mac game, or have Steam download it, with the account's DLC; copy it;
                                     decrypt its code
        --game DIR                   the Mac game's folder, when it is not in Steam's downloads
        --steam DIR --home DIR       Steam's folder, and RS Re-ARMed's
        --no-download                only look for the game: Steam is asked for nothing
        --no-dlc                     leave the account's DLC
        --progress-lines             progress and steps as lines (a window that shows them reads these)
    rs_install.py refresh            new DLC from Steam, and the copy checked against the game's files
    rs_install.py run [-- ARGS]      start the game (ARGS go to rs-rearmed)
    rs_install.py status             what is set up

The game is the Mac game, from your own Steam account: Steam for Linux installs the Windows one, and downloads the
Mac one when its console is asked to (download_depot), which setup does. Nothing of the game is in this program.

What setup makes, in the home folder (--home, else $RS_NATIVE_HOME, else rs-rearmed under XDG's data folder), is
laid out as the Mac app's home is:

    game/        RS Re-ARMed's own copy of the game's files (tools/play-native/gamefiles.py makes and checks it, as
                 on the Mac): clones where the file system has them (btrfs, XFS), which take no room. A Steam
                 update, an uninstall or a cleared download cannot break it. Custom songs go in game/dlc.
    image/       the game's decrypted code (tools/steamless/decrypt.py), which rs-rearmed loads
    steam/       the saves (the offline Steam library's), started from Steam's own copy of them
    logs/        last-run.log when the game's output would go nowhere
    settings.env, options.env    switches, KEY=value lines (docs/mac/INSTALL.md lists them); the environment wins

The Mac's tools are used as they are, with what differs on Linux set here: where Steam keeps its files and its
downloads, how Steam is started and told a command, and a clone by the kernel's own call.
"""
import argparse
import errno
import fcntl
import glob
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
# The project's tools: beside this file in a packaged build (tools/), else the repository's.
TOOLS = next((p for p in (os.path.join(HERE, "tools"), os.path.normpath(os.path.join(HERE, "..", "..", "tools")))
              if os.path.isdir(os.path.join(p, "play-native"))), None)
if TOOLS:
    for _d in ("play-native", "steamless", "targets", "loadscreen"):
        sys.path.insert(0, os.path.join(TOOLS, _d))

APP = 221680
GAME_BINARY = os.path.join("Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014")
IMAGE_NAME = "Rocksmith2014_x64_decrypted"
PROGRAM = "rs-rearmed"
GAME_NAME = "Rocksmith 2014 Edition - Remastered"
FICLONE = 0x40049409
STEAM_PLACES = ("~/.local/share/Steam", "~/.steam/steam", "~/.var/app/com.valvesoftware.Steam/.local/share/Steam",
                "~/snap/steam/common/.local/share/Steam")
STEAM_FLATPAK = "com.valvesoftware.Steam"


class Failure(Exception):
    def __init__(self, message, code=1):
        super().__init__(message)
        self.code = code


# --- folders -------------------------------------------------------------------------------------------------

def home_folder(given=None):
    if given:
        return os.path.abspath(given)
    if os.environ.get("RS_NATIVE_HOME"):
        return os.path.abspath(os.environ["RS_NATIVE_HOME"])
    data = os.environ.get("XDG_DATA_HOME")
    return os.path.join(data if data and data.startswith("/") else os.path.expanduser("~/.local/share"), "rs-rearmed")


def steam_folder(given=None):
    """Steam's folder: the one given, $RS_STEAM_DIR, else the first of the usual places that has a library."""
    given = given or os.environ.get("RS_STEAM_DIR")
    if given:
        return os.path.abspath(given)
    for p in STEAM_PLACES:
        p = os.path.realpath(os.path.expanduser(p))
        if os.path.isdir(os.path.join(p, "steamapps")):
            return p
    return os.path.realpath(os.path.expanduser(STEAM_PLACES[0]))


def content_folders(steam):
    """Where Steam's download_depot puts what it downloads: steamapps/content under the folder its client runs in
    (steamrt64 for the 64-bit client, ubuntu12_32 before it), or under the Steam folder itself. The ones that are
    there come first; the first is where the next download is taken to go."""
    found = sorted(glob.glob(os.path.join(steam, "*", "steamapps", "content")))
    usual = [os.path.join(steam, d, "steamapps", "content") for d in ("steamrt64", "ubuntu12_64", "ubuntu12_32")
             if os.path.isdir(os.path.join(steam, d))]
    usual.append(os.path.join(steam, "steamapps", "content"))
    return found + [p for p in usual if p not in found]


def set_up(home):
    return (os.path.isfile(os.path.join(home, "game", ".rearmed", "manifest.json"))
            and os.path.isfile(os.path.join(home, "image", IMAGE_NAME)))


# --- Steam on Linux ------------------------------------------------------------------------------------------

def steam_argv():
    """How Steam is started: $RS_STEAM_EXE, the steam on the PATH, or the Flatpak. None without one."""
    exe = os.environ.get("RS_STEAM_EXE") or os.environ.get("LP_STEAM_EXE")
    if exe:
        return [exe] if os.access(exe, os.X_OK) else None
    exe = shutil.which("steam")
    if exe:
        return [exe]
    flatpak = shutil.which("flatpak")
    if flatpak and any(os.path.isdir(os.path.join(os.path.expanduser(d), "app", STEAM_FLATPAK))
                       for d in ("/var/lib/flatpak", "~/.local/share/flatpak")):
        return [flatpak, "run", STEAM_FLATPAK]
    return None


def process_running(name):
    """A process of that name runs (its comm, as pgrep -x reads it)."""
    try:
        pids = [p for p in os.listdir("/proc") if p.isdigit()]
    except OSError:
        return False
    for pid in pids:
        try:
            with open("/proc/%s/comm" % pid) as f:
                if f.read().strip() == name:
                    return True
        except OSError:
            pass
    return False


def steam_running():
    return process_running(os.environ.get("LP_STEAM_PROCESS") or "steam")


def game_running(edition=None):
    return process_running(os.environ.get("RS_GAME_PROCESS") or PROGRAM)


def spawn(argv):
    """ARGV started and left to itself (Steam's program ends at once when Steam runs, and is Steam when not)."""
    try:
        child = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                                 start_new_session=True)
    except OSError:
        return False
    threading.Thread(target=child.wait, daemon=True).start()  # its end is taken when it comes
    return True


def steam_open(url=None):
    """Steam, opened or brought to the front, at url if one is given (Steam's own program hands it on)."""
    argv = steam_argv()
    return bool(argv) and spawn(argv + ([url] if url else []))


def steam_command(steam, *words):
    """A console command for the Steam that is running: its program hands on a +command, and Steam runs it as its
    console would. The program ends at once."""
    argv = steam_argv()
    return bool(argv) and spawn(argv + ["+" + words[0]] + list(words[1:]))


def copy_text(text):
    """TEXT on the clipboard, where the desktop has a program for it."""
    for argv in (["wl-copy"], ["xclip", "-selection", "clipboard"], ["xsel", "--clipboard", "--input"]):
        if shutil.which(argv[0]):
            try:
                subprocess.run(argv, input=text.encode(), check=False, timeout=5, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
                return True
            except (OSError, subprocess.SubprocessError):
                pass
    return False


def reflink(src, dst):
    """A clone of SRC at DST, as gamefiles.clonefile is on the Mac: True when DST is now one, False when the file
    system cannot make it (another one, or one without clones); other errors raise."""
    with open(src, "rb") as s:
        fd = os.open(dst, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        try:
            fcntl.ioctl(fd, FICLONE, s.fileno())
        except OSError as e:
            os.close(fd)
            os.unlink(dst)
            if e.errno in (errno.EXDEV, errno.ENOTSUP, errno.EOPNOTSUPP, errno.ENOSYS, errno.EINVAL, errno.ENOTTY,
                           errno.EBADF, errno.EPERM, errno.ETXTBSY):
                return False
            raise
        os.close(fd)
    shutil.copystat(src, dst)
    return True


def app_children(appinfo, parent):
    """The AppIDs whose app info names PARENT as their parent (a DLC's game), from Steam's appcache/appinfo.vdf:
    after a header, one entry per app (its id, the entry's size, then binary key-values), and from version 0x29
    on the keys as numbers into a table of strings at the end. None for a file that is not one."""
    try:
        with open(appinfo, "rb") as f:
            d = f.read()
    except OSError:
        return None
    if len(d) < 16 or d[1:4] != b"DV\x07":
        return None
    at, end = 8, len(d)
    try:
        if d[0] >= 0x29:
            end = struct.unpack_from("<q", d, 8)[0]
            at = 16
            if not at <= end <= len(d) - 4:
                return None
            count = struct.unpack_from("<I", d, end)[0]
            names = d[end + 4:].split(b"\0", count)[:count]
            if b"parent" not in names:
                return set()
            key = struct.pack("<i", names.index(b"parent"))
        else:
            key = b"parent\0"
        wanted = (b"\x02" + key + struct.pack("<i", parent), b"\x01" + key + b"%d\0" % parent)
        out = set()
        while at + 8 <= end:
            app, size = struct.unpack_from("<II", d, at)
            if app == 0:
                break
            entry = d[at + 8:at + 8 + size]
            if any(w in entry for w in wanted):
                out.add(app)
            at += 8 + size
        return out
    except (struct.error, ValueError):
        return None


def cached_dlc(steam, packages, app=APP):
    """The game's DLC that an account of this Steam has a license for, from Steam's two caches: the apps of the
    packages of its accounts' licenses (PACKAGES reads appcache/packageinfo.vdf: gamefiles.package_apps) that the
    app info gives the game as their parent. For a Steam that has no ownership ticket of the game, which it only
    writes once the game was started through it, as nobody starts the Mac game through Steam for Linux. The
    caches don't say which account: Steam itself refuses a DLC the signed-in one does not own. None without the
    caches."""
    children = app_children(os.path.join(steam, "appcache", "appinfo.vdf"), app)
    licensed = packages(os.path.join(steam, "appcache", "packageinfo.vdf"))
    if children is None or not licensed:
        return None
    return sorted(children & set().union(*licensed.values()))


# What the user is told at a step of Steam's, where the Mac's words (gamefiles.GUIDE) are the Mac's.
GUIDE = {
    "steam-missing": ("Install Steam",
                      "Steam isn't on this computer yet. Install it (your distribution has it, and so does Steam's "
                      "website), open it and sign in. The setup goes on by itself."),
    "steam-starting": ("Steam is starting",
                       "Steam is opening, and it may update itself first, which can take a few minutes. If it asks "
                       "you to sign in, use the account that owns %(game)s. The setup goes on by itself."),
    "sign-in": ("Sign in to Steam",
                "Steam is open but nobody is signed in. In Steam's window, sign in with the account that owns "
                "%(game)s. If Steam is in Offline Mode, open the Steam menu and choose Go Online…. The setup goes on "
                "by itself when you are signed in."),
    "steam-offline": ("Steam lost its connection",
                      "Steam isn't connected right now. Check that this computer is on the internet. Steam "
                      "reconnects by itself, and the setup goes on when it has."),
    "wrong-account-mac": ("This Steam account doesn't have the game",
                          "Steam is signed in as %(name)s, and Steam says that account can't download %(game)s for "
                          "Mac, which is the edition RS Re-ARMed runs (a Steam purchase of the game has both the "
                          "Windows and the Mac edition). In Steam, open the Steam menu, choose Change Account…, and "
                          "sign in with the account that owns the game. Steam restarts, and the setup goes on by "
                          "itself."),
    "wrong-account": ("This Steam account doesn't have the game",
                      "Steam is signed in as %(name)s, and Steam says that account doesn't own %(game)s. In Steam, "
                      "open the Steam menu, choose Change Account…, and sign in with the account that owns it. Steam "
                      "restarts, and the setup goes on by itself."),
    "installing-game": ("Steam is installing the game",
                        "Steam is installing or updating %(game)s in its library. Nothing to do here: leave Steam "
                        "open. The setup goes on by itself when it is done."),
    "downloading": ("Steam is downloading the game",
                    "Nothing to do here: leave Steam open. Steam's own window doesn't show this download, only the "
                    "progress here does. If it is stopped, the next setup continues from where it was."),
    "download-stalled": ("The download has stopped moving",
                         "Nothing has arrived for a while. Check that this computer is on the internet, and that "
                         "Steam is open and signed in. It goes on by itself when it can."),
    "download-failed": ("Steam couldn't download the game", "Steam says: %(why)s. The setup tries again by itself."),
    "paste": ("One step in Steam, please",
              "Steam didn't start the download by itself, so it needs your help once. Steam's console is open: click "
              "in the box at the bottom of the console window, paste or type this command, and press Enter. "
              "The command is: %(command)s"),
}


def the_tools():
    """gamefiles and decrypt, with Linux in the places where they say Mac. gamefiles' own functions find these by
    their names when they run; what its classes took as defaults when they were defined is set in the classes."""
    if not TOOLS:
        raise Failure("the project's tools (tools/play-native, tools/steamless) are not beside this program", 2)
    import decrypt
    import gamefiles
    if getattr(gamefiles, "linux", False):
        return gamefiles, decrypt
    gamefiles.linux = True
    gamefiles.clonefile = reflink
    gamefiles.steam_content_dirs = content_folders
    gamefiles.download_content_dir = lambda steam: content_folders(steam)[0]
    gamefiles.steam_exe = lambda steam: (steam_argv() or [None])[0]
    gamefiles.steam_running = steam_running
    gamefiles.steam_open = steam_open
    gamefiles.steam_command = steam_command
    gamefiles.running = game_running
    gamefiles.PROCESS["mac"] = PROGRAM
    gamefiles.GAME_MAC = GAME_NAME
    # Steam for Linux installs the Windows game: an install of it that Steam is busy with is not this game's.
    gamefiles.mac_game_pending = lambda steam: False
    gamefiles.GUIDE.update(GUIDE)

    ticket_dlc = gamefiles.account_dlc

    def account_dlc(steam, account, app=gamefiles.APP_ID):
        """The account's DLC by its ownership ticket, as on the Mac; else by Steam's caches (cached_dlc)."""
        owned = ticket_dlc(steam, account, app)
        return owned if owned is not None else cached_dlc(steam, gamefiles.package_apps, app)
    gamefiles.account_dlc = account_dlc

    def open_console(self):
        copy_text(self.command)
        steam_open("steam://open/console")
    gamefiles.DepotDownload.open_console = open_console

    mac_gate = gamefiles.SteamGate

    class SteamGate(mac_gate):
        def __init__(self, steam, game, guide, **kw):
            kw.setdefault("opener", steam_open)
            super().__init__(steam, game, guide, **kw)

    mac_copier = gamefiles.Copier

    class Copier(mac_copier):
        def __init__(self, *a, **kw):
            kw.setdefault("clone", reflink)
            kw.setdefault("is_running", game_running)
            kw.setdefault("placement", None)  # the copy is in the home folder: the Mac's "on the game's drive" is not here
            super().__init__(*a, **kw)

        def _clone_works(self, src_st, src):
            """Tried with a real file, once per device number, as on the Mac, but without the Mac's test that the
            two are one volume first: btrfs gives each subvolume its own number, and clones across them."""
            key = src_st.st_dev
            if key not in self._clone_ok:
                probe = os.path.join(self.dest, gamefiles.META, "probe" + gamefiles.TMP_SUFFIX)
                try:
                    if os.path.lexists(probe):
                        os.unlink(probe)
                    ok = self.clone(os.path.realpath(src), probe)
                except OSError:
                    ok = False
                finally:
                    if os.path.lexists(probe):
                        os.unlink(probe)
                self._clone_ok[key] = ok
            return self._clone_ok[key]

    gamefiles.SteamGate = SteamGate
    gamefiles.Copier = Copier
    return gamefiles, decrypt


# --- setup ---------------------------------------------------------------------------------------------------

def fetch_dlc(gamefiles, steam, home, lines, say):
    """The DLC the signed-in account owns, into Steam's downloads: the Mac archives are in each DLC's own depot,
    and Steam for Linux installs only the Windows ones. Never fails the setup: a refresh gets what is missing."""
    if os.environ.get("RS_DLC_FETCH", "1") == "0":
        return
    try:
        gamefiles.DlcFetch(steam, os.path.join(home, "dlc-fetched.json"), gamefiles.Progress(lines),
                           gamefiles.Guide(lines), 30.0, game=gamefiles.GAME_MAC).run()
    except (gamefiles.Failure, OSError) as e:
        say("note: the DLC was not downloaded (%s). `refresh` gets it later." % e)


def make_title_logo(home, say):
    """RS Re-ARMed's title logo, static.psarc.rearmed in the copy, made from the copy's own static.psarc
    (tools/loadscreen/logo.py: the tag is a picture beside it, and nothing but Python and openssl is needed). Each
    start links static.psarc to it (title_logo). Never fails a setup: without it the game shows its own logo."""
    game = os.path.join(home, "game")
    src, dst = os.path.join(game, "static.psarc.original"), os.path.join(game, "static.psarc.rearmed")
    if os.environ.get("RS_REARMED", "1") == "0" or os.path.isfile(dst) or not os.path.isfile(src):
        return
    try:
        import logo
        logo.build(src, dst)
        say("The title logo: made (%s)." % dst)
    except (Exception, SystemExit) as e:  # noqa: BLE001  (the archive's key not found exits, as the tools' own runs do)
        if os.path.exists(dst + ".part"):
            os.unlink(dst + ".part")
        say("note: the title logo was not made (%s). The game shows its own." % e)


def setup(args):
    home, steam = home_folder(args.home), steam_folder(args.steam)
    gamefiles, decrypt = the_tools()
    lines = args.progress_lines
    progress = gamefiles.Progress(lines)
    say = progress.say
    os.environ["RS_NATIVE_HOME"] = home  # (gamefiles.mac_home)
    for d in ("image", "steam", "logs"):
        os.makedirs(os.path.join(home, d), exist_ok=True)
    if game_running():
        raise Failure("RS Re-ARMed is running: quit the game first.")
    # 1. the Mac game: the folder given, else Steam's download of it, started here when it is not there
    try:
        game, origin = gamefiles.mac_game(steam, args.game, progress, gamefiles.Guide(lines),
                                          download=not args.no_download)
    except gamefiles.Failure as e:
        hint = ""
        if args.no_download and not args.game:
            depot = gamefiles.mac_depot()
            hint = ("\nSteam downloads it for an account that owns %s: run setup without --no-download, or enter "
                    "this in Steam's console (steam://open/console):\n\n    %s\n"
                    % (GAME_NAME, depot.command() if depot else "download_depot %d 221682" % APP))
        raise Failure(str(e) + hint, e.code or 1)
    say("The Mac game: %s" % game)
    # 2. its code, checked to be the build this program is for
    image = os.path.join(home, "image", IMAGE_NAME)
    code = decrypt.main(["--game", game, "--output", image])
    if code:
        raise Failure("the game's program in %s could not be used (see above)" % game, code)
    # 3. the DLC, then the copy, which takes the Mac archives from Steam's downloads
    dlc = []
    if gamefiles.depot_source(game):
        if not args.no_dlc and not args.no_download:
            fetch_dlc(gamefiles, steam, home, lines, say)
        dlc = gamefiles.steam_dlc_folders(steam)
    dest = os.path.join(home, "game")
    try:
        gamefiles.Copier("mac", game, dest, dlc, steam, gamefiles.Progress(lines)).run()
        gamefiles.key_from_image(dest, image, gamefiles.Progress(lines))  # the archives' key, for the menus' overlay
    except gamefiles.Failure as e:
        raise Failure(str(e), e.code or 1)
    make_title_logo(home, say)
    say("RS Re-ARMed is set up in %s. Custom songs go in %s." % (home, os.path.join(dest, "dlc")))
    return 0


def refresh(args):
    home = home_folder(args.home)
    gamefiles, _ = the_tools()
    lines = args.progress_lines
    progress = gamefiles.Progress(lines)
    os.environ["RS_NATIVE_HOME"] = home
    dest = os.path.join(home, "game")
    if not os.path.isdir(os.path.join(dest, ".rearmed")):
        raise Failure("There is no copy of the game's files in %s yet: run setup first." % home, 3)
    info = gamefiles.read_json(os.path.join(dest, gamefiles.META, gamefiles.SOURCE_INFO)) or {}
    if not args.no_dlc and info.get("steam") and gamefiles.depot_source(info.get("source") or ""):
        fetch_dlc(gamefiles, info["steam"], home, lines, progress.say)
    try:
        gamefiles.refresh(dest, progress)
    except gamefiles.Failure as e:
        raise Failure(str(e), e.code or 1)
    make_title_logo(home, progress.say)  # (a new static.psarc took the old one's logo with it: gamefiles)
    return 0


# --- the start -----------------------------------------------------------------------------------------------

ENV_KEY = re.compile(r"RS_[A-Z0-9_]+$")


def read_env(path, keys=None):
    """KEY=VALUE lines, # a comment, the ends' whitespace ignored; the first of a key counts. KEYS: a pattern the
    keys have to match (others are left out)."""
    out = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, _, v = line.partition("=")
                k, v = k.strip(), v.strip()
                if k not in out and v and (keys is None or keys.match(k)):
                    out[k] = v
    except OSError:
        pass
    return out


def offline_ini(text, latency=None):
    """The game's Rocksmith.ini with the keys the Mac launcher forces too: [Net] AutoLogin=0 and [Renderer.MacOS]
    DisableBrowser=1, in a file that lacks the key or its section as well, and [Audio] LatencyBuffer when one is
    given. The game takes its defaults for every key a file lacks."""
    forced = {"[Net]": ("AutoLogin", "0"), "[Renderer.MacOS]": ("DisableBrowser", "1")}
    if latency:
        forced["[Audio]"] = ("LatencyBuffer", str(latency))
    out, seen, section = [], set(), None
    for line in text.replace("\r", "").split("\n"):
        if line.startswith("["):
            section = line.strip()
        key = forced.get(section)
        if key and line.startswith(key[0] + "="):
            continue
        out.append(line)
        if line.strip() in forced and line.strip() not in seen:
            seen.add(line.strip())
            out.append("%s=%s" % forced[line.strip()])
    while out and not out[-1]:
        out.pop()
    for section, (k, v) in forced.items():
        if section not in seen:
            out += [section, "%s=%s" % (k, v)]
    return "\n".join(out) + "\n"


def write_ini(game, env):
    """What the game saved stays; the install's copy of the ini is only the start, and no file at all is one too."""
    path = os.path.join(game, "Rocksmith.ini")
    text = ""
    for p in (path, os.path.join(game, ".rearmed", "Rocksmith.ini")):
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                text = f.read()
            break
        except OSError:
            pass
    latency = env.get("RS_LATENCY_BUFFER", "")
    new = offline_ini(text, latency if re.match(r"([1-9]|1[0-6])$", latency) else None)
    if new != text or not os.path.exists(path):
        with open(path + ".new", "w", encoding="utf-8") as f:
            f.write(new)
        os.replace(path + ".new", path)


def seed_saves(home, steam, log):
    """The first start: the offline saves begin as a copy of Steam's own (every account with Rocksmith saves),
    without `crd`, the stored Ubisoft sign-in, which never reaches the game. Steam's folder is never written."""
    for src in sorted(glob.glob(os.path.join(steam, "userdata", "*", str(APP), "remote"))):
        account = os.path.basename(os.path.dirname(os.path.dirname(src)))
        dst = os.path.join(home, "steam", account, str(APP), "remote")
        if os.path.isdir(dst) or not os.path.isdir(src):
            continue
        tmp = dst + ".tmp"
        shutil.rmtree(tmp, ignore_errors=True)
        shutil.copytree(src, tmp, ignore=shutil.ignore_patterns("crd"))
        os.rename(tmp, dst)
        log("seeded %s from %s (without crd)" % (dst, src))


def no_credentials(home):
    for d, _, files in os.walk(os.path.join(home, "steam")):
        if "crd" in files:
            os.unlink(os.path.join(d, "crd"))


def title_logo(game, env):
    """static.psarc is a link to one of the two copies: the one with RS Re-ARMed's title logo when it is there."""
    link = os.path.join(game, "static.psarc")
    if not os.path.isfile(os.path.join(game, "static.psarc.original")):
        return  # a copy without the pair: the game's own file is the file
    want = ("static.psarc.rearmed" if os.path.isfile(os.path.join(game, "static.psarc.rearmed"))
            and env.get("RS_REARMED", "1") != "0" else "static.psarc.original")
    if os.path.islink(link) and os.readlink(link) == want:
        return
    if os.path.lexists(link + ".new"):
        os.unlink(link + ".new")
    os.symlink(want, link + ".new")
    os.replace(link + ".new", link)


def program():
    """rs-rearmed: $RS_REARMED_BIN, else a packaged build's (this file is in its usr/share/rs-rearmed, the program
    in its usr/lib/rs-rearmed), else the repository's build."""
    for p in (os.environ.get("RS_REARMED_BIN"), os.path.join(HERE, "..", "..", "lib", "rs-rearmed", PROGRAM),
              os.path.join(HERE, "..", "..", "build", "linux", PROGRAM)):
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return os.path.abspath(p)
    raise Failure("rs-rearmed is not built (make -C linux all gl bink), and RS_REARMED_BIN does not name it", 2)


def goes_nowhere(fd):
    """Output on fd is lost or is not ours to fill: closed, /dev/null, or a socket (a desktop's journal)."""
    try:
        st = os.fstat(fd)
    except OSError:
        return True
    if stat.S_ISSOCK(st.st_mode):
        return True
    try:
        null = os.stat("/dev/null")
    except OSError:
        return False
    return stat.S_ISCHR(st.st_mode) and st.st_rdev == null.st_rdev


def prepare(home, steam, base=None):
    """Everything a start does before the game's program is run; the environment for it."""
    game = os.path.join(home, "game")
    if not set_up(home):
        if os.path.isdir(os.path.join(game, ".rearmed")) and not os.path.isfile(os.path.join(game, ".rearmed", "manifest.json")):
            raise Failure("The copy of the game's files is incomplete (a setup or refresh was stopped). Run setup "
                          "again: what is copied is kept.", 3)
        raise Failure("RS Re-ARMed has no copy of the game's files in %s yet: run setup first." % home, 3)
    env = dict(os.environ if base is None else base)
    notes = []
    # settings.env and options.env, then what the copy recorded (the DLC the account owns, the language, the
    # account): each for a key the environment does not have already.
    for name in ("settings.env", "options.env"):
        for k, v in read_env(os.path.join(home, name), ENV_KEY).items():
            if k in env and env[k] != v:
                notes.append("[%s] %s=%s not used: the environment has %s=%s" % (name[:-4], k, v, k, env[k]))
            else:
                env[k] = v
                notes.append("[%s] %s=%s" % (name[:-4], k, v))
    for k, v in read_env(os.path.join(game, ".rearmed", "steam.env")).items():
        env.setdefault(k, v)
    env["RS_NATIVE_HOME"] = home
    env.setdefault("RS_STEAM_DATA_DIR", os.path.join(home, "steam"))
    os.makedirs(os.path.join(home, "logs"), exist_ok=True)
    os.makedirs(os.path.join(home, "steam"), exist_ok=True)

    def seed_log(line):
        with open(os.path.join(home, "logs", "seed.log"), "a", encoding="utf-8") as f:
            f.write(line + "\n")
    if env.get("RS_NATIVE_SEED", "1") != "0":
        try:
            seed_saves(home, steam, seed_log)
        except OSError as e:
            notes.append("[saves] Steam's saves were not copied: %s" % e)
    no_credentials(home)
    title_logo(game, env)
    write_ini(game, env)
    return env, notes


def run(args):
    home, steam = home_folder(args.home), steam_folder(getattr(args, "steam", None))
    game, image = os.path.join(home, "game"), os.path.join(home, "image", IMAGE_NAME)
    exe = program()
    env, notes = prepare(home, steam)
    # One game at a time: the lock is the game's from here, and goes when it ends.
    lock = os.open(os.path.join(home, "logs", "running.lock"), os.O_RDWR | os.O_CREAT, 0o644)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        raise Failure("RS Re-ARMed is already running.")
    os.set_inheritable(lock, True)
    lost = [fd for fd in (1, 2) if goes_nowhere(fd)]
    if lost:
        log = os.open(os.path.join(home, "logs", "last-run.log"), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        for fd in lost:
            os.dup2(log, fd)
        os.close(log)
    for line in notes:
        os.write(2, (line + "\n").encode())
    os.execve(exe, [exe, "--image", image, "--run", game] + list(args.rest), env)


def status(args, out=sys.stdout):
    home, steam = home_folder(args.home), steam_folder(None)
    game = os.path.join(home, "game")
    out.write("home: %s\n" % home)
    out.write("the game's copy: %s\n" % ("complete" if os.path.isfile(os.path.join(game, ".rearmed", "manifest.json"))
                                         else "not made"))
    out.write("the game's code: %s\n" % ("decrypted" if os.path.isfile(os.path.join(home, "image", IMAGE_NAME))
                                         else "not made"))
    try:
        songs = len([n for n in os.listdir(os.path.join(game, "dlc")) if n.endswith("_m.psarc")])
    except OSError:
        songs = 0
    out.write("song archives in game/dlc: %d\n" % songs)
    out.write("Steam: %s\n" % (steam if os.path.isdir(steam) else "not found"))
    found = [d for d in (os.path.join(c, "app_%d" % APP, "depot_221682") for c in content_folders(steam))
             if os.path.isfile(os.path.join(d, GAME_BINARY))]
    out.write("Steam's download of the Mac game: %s\n" % (found[0] if found else "not there"))
    return 0


# --- with no terminal: a window for the setup's progress and for what went wrong ------------------------------

def on_terminal():
    return sys.stderr.isatty() or sys.stdout.isatty()


def tell(text, error=True):
    """TEXT in a window where there is no terminal to read it in (zenity or kdialog), and on stderr."""
    sys.stderr.write("rs-rearmed: %s\n" % text)
    if on_terminal() or not (os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")):
        return
    for argv in (["zenity", "--error" if error else "--info", "--title", "RS Re-ARMed", "--width", "480", "--text", text],
                 ["kdialog", "--title", "RS Re-ARMed", "--error" if error else "--msgbox", text]):
        if shutil.which(argv[0]):
            try:
                subprocess.run(argv, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            except OSError:
                pass


def setup_in_window(argv, child=None):
    """The setup as a child that writes progress lines (CHILD: its command, for the tests), shown by zenity's
    progress window: the bar, the step, and under it what the user has to do (Steam's steps). Cancel stops the
    setup; what is done is kept. Returns the setup's exit status (130: cancelled) and what it said last (the
    error's text)."""
    child = subprocess.Popen(child or [sys.executable, "-I", os.path.abspath(__file__), "setup", "--progress-lines"] + argv,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    bar = subprocess.Popen(["zenity", "--progress", "--title", "RS Re-ARMed", "--width", "560", "--auto-close",
                            "--text", "Setting up the game's files…"], stdin=subprocess.PIPE, text=True,
                           stderr=subprocess.DEVNULL)
    said, step, todo = [], "Setting up the game's files…", ""

    def show(percent=None):
        text = step + ("\n\n" + todo if todo else "")  # (zenity reads \n in a line as a line's end, and markup)
        text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\n", "\\n")
        try:
            if percent is not None:
                bar.stdin.write("%d\n" % percent)
            bar.stdin.write("# %s\n" % text)
            bar.stdin.flush()
        except (OSError, ValueError):
            pass
    cancelled = False
    for line in child.stdout:
        line = line.rstrip("\n")
        if bar.poll() is not None:  # Cancel, or the window closed
            cancelled = True
            child.terminate()
            break
        if line.startswith("progress "):
            _, fraction, label = (line.split(" ", 2) + [""])[:3]
            step = label or step
            try:
                show(min(int(float(fraction) * 100), 99))
            except ValueError:
                show()
        elif line.startswith("guide\t"):
            parts = line.split("\t")
            todo = "" if len(parts) < 4 else "%s. %s" % (parts[2], parts[3])
            show()
        elif line.strip():
            said.append(line)
            sys.stderr.write(line + "\n")
    code = child.wait()
    try:
        bar.stdin.write("100\n")
        bar.stdin.close()
    except (OSError, ValueError):
        pass
    bar.wait()
    return (130 if cancelled else code), "\n".join(said[-3:])


def start(args):
    """What the app does: the game, after a setup when there has been none."""
    home = home_folder(args.home)
    if not set_up(home):
        argv = (["--home", home] if args.home else []) + (["--steam", args.steam] if args.steam else [])
        if on_terminal() or not shutil.which("zenity"):
            args.game, args.no_download, args.no_dlc, args.progress_lines = None, False, False, False
            setup(args)
        else:
            code, said = setup_in_window(argv)
            if code == 130:
                return code
            if code:
                raise Failure(said or "the setup did not finish", code)
    return run(args)


def main(argv=None):
    ap = argparse.ArgumentParser(description="RS Re-ARMed on Linux: set up the game's files, and start the game.")
    ap.add_argument("--home")
    ap.add_argument("--steam")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("setup")
    p.add_argument("--game")
    p.add_argument("--steam", default=argparse.SUPPRESS)
    p.add_argument("--home", default=argparse.SUPPRESS)
    p.add_argument("--no-download", action="store_true")
    p.add_argument("--no-dlc", action="store_true")
    p.add_argument("--progress-lines", action="store_true")
    p = sub.add_parser("refresh")
    p.add_argument("--home", default=argparse.SUPPRESS)
    p.add_argument("--no-dlc", action="store_true")
    p.add_argument("--progress-lines", action="store_true")
    p = sub.add_parser("run")
    p.add_argument("--home", default=argparse.SUPPRESS)
    p.add_argument("--steam", default=argparse.SUPPRESS)
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p = sub.add_parser("status")
    p.add_argument("--home", default=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "setup":
            return setup(a)
        if a.cmd == "refresh":
            return refresh(a)
        if a.cmd == "status":
            return status(a)
        a.rest = getattr(a, "rest", [])
        a.rest = a.rest[1:] if a.rest[:1] == ["--"] else a.rest
        return run(a) if a.cmd == "run" else start(a)
    except Failure as e:
        tell(str(e))
        return e.code
    except KeyboardInterrupt:
        sys.stderr.write("\nstopped: what is done is kept, and the next run goes on from there\n")
        return 130


if __name__ == "__main__":
    sys.exit(main())
