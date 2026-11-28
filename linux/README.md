# Linux, arm64 and x86-64 (x86-64 plays; arm64 investigated)

A port of RS Re-ARMed to Linux, with Linux in the place of macOS underneath the Mac game. Two machines are in mind:
an Apple silicon Mac running Fedora Asahi Remix, where the game would otherwise need an x86 emulator inside a
virtual machine and gets the same translated code as on macOS; and an x86-64 desktop, where the game's own code can
run as it is.

**On x86-64 the game plays**: the Mac game's own code, as it is, in a window, with its sound and the guitar's
input through PipeWire and its videos (on Bazzite, 2026-11-28: a player calibrated a Real Tone Cable and played a
song), and the Mac app's built-in fixes module whole (no intro screens, the batched DLC mounting, the frame-rate
fixes). The game's files come from the player's own Steam: `install/rs_install.py` has Steam download the Mac
game and the account's DLC, makes RS Re-ARMed's copy and starts the game ([below](#set-up-and-play-x86-64)).
An audio interface's channels are listed as inputs of their own, as on the Mac. Mods load (a `.so` in the home
folder's `mods`: hook points, mid points, edit groups, the overlay, the files a mod serves; `rearmed-mods`
builds for it from its own `linux/`). Not there yet: anything for arm64, which is investigated only.
The work is on the branches `port/linux-arm64` and, on top of it, `port/linux-x86_64`.

| Read | For |
|---|---|
| [FINDINGS.md](FINDINGS.md) | what each part of the project takes from macOS today, and what Linux has in its place, for arm64 |
| [X86-64.md](X86-64.md) | what differs on x86-64: the game's code runs as it is, and what that takes |
| [PLAN.md](PLAN.md) | the steps, what each proves and needs, the open questions, the risks, and a log of what was done |
| [darwin-imports.tsv](darwin-imports.tsv) | every import of the Mac game's image, with what a Linux build does about it |
| [patch-sites.tsv](patch-sites.tsv) | every hook point and mid point, with whether a jump can be written there (`check_patch_sites.py` makes and checks it) |
| [patch-edits.tsv](patch-edits.tsv) | every edit whose new instructions are longer than its range, with what its jump covers (the same tool) |
| `first_run.py` | the first thing to run on an x86-64 Linux machine: the game's initializers, with nothing built |
| `Makefile`, `src`, `darwin` | the x86-64 build: the loader, and the Darwin layer that answers the game's imports |
| `install` | the installer's window (`rs_gui.py`) and its settings (`rs_settings.py`); the setup and the start: `rs_install.py`, over the Mac's install tools (`tools/play-native/gamefiles.py`, `tools/steamless/decrypt.py`); and the packaged build's launcher, desktop entry, icon and AppImage step |
| `compat` | the few macOS functions and header names the project's shared code uses, for Linux, so that `shims/` builds from its own sources |
| `gl` | `libRSSystemGL.so`: Mesa as the renderer's OpenGL backend expects Apple's OpenGL (CGL over EGL, Apple's identity) |

## In short

- **The translated game is not the obstacle.** The C the recompiler writes for the Mac game (243 files, 617 MB)
  passes a compile check for `aarch64-linux-gnu` as it is. Of the system it includes four headers, none of them
  Apple's.
- **What the game calls is.** The Mac game's image imports 681 symbols from 19 macOS libraries, and the Mac app
  binds them to the real arm64 libraries of macOS. On Linux each needs an answer:

  | What a Linux build does | Imports | |
  |---|---|---|
  | The host's own function of that name (glibc, libm, GNU libstdc++) | 257 | no code |
  | The project's shims (the renderer, Cg, Bink, Steam), built for Linux | 162 | a build, and the renderer's OpenGL path on Mesa |
  | The runtime's own (`rc_file`, `rc_setjmp`) | 11 | small changes |
  | A function of ours over the host's (flags, struct layouts, `errno`) | 32 | new, a few lines each |
  | A Darwin-only facility written anew (Mach, `OSAtomic`, vDSP, a small CoreFoundation) | 75 | new |
  | A stand-in for a macOS service (CoreAudio's devices, the display's modes) | 31 | new: the audio layer is the large one |
  | The network, refused | 15 | new, a few lines each |
  | Nothing the app runs calls it (the game's own `main`, Cocoa shell and ARC support code did) | 98 | a stub each |

- **GNU libstdc++ is the good news.** The game was built against Apple's copy of GNU libstdc++ 4.2.1, and Linux's
  `libstdc++.so.6` still exports that ABI: all 93 of those imports bind to the system's library.
- **No Objective-C runtime is needed.** With the shell rewritten, the engine's own Objective-C is four functions (the
  clipboard, an alert, one folder listing), which become native ones.
- **Around the game**, three pieces are written for macOS and need a Linux twin: the window shell (AppKit, 1,400
  lines), the renderer's context and presentation (Metal by default; its OpenGL path is thin and close to
  portable), and the audio devices (CoreAudio, which nothing replaces today). The build and install scripts are
  Apple's tools throughout.
- **Learn & Play needs far less of this than the Mac game**: its game talks only to the project's own Win32 seam,
  so of macOS it has one window file, one audio engine and the renderer. Upstream's iPhone port started with it for
  that reason. This port starts with the Mac game all the same, the game it can be run with here; the pieces under
  both (shell, renderer, audio, install) are meant to be written once.
- **On x86-64 the game's code needs no translating**: it is an x86-64 program with the calling convention Linux
  has too, and it makes no system call of its own. A loader the project's tests already have, and the same answers
  to the same imports, bound directly. What the recompiler gives besides translation (hook points, mid points,
  edits) needs a second form there as patches, or the game is translated there too: the C passes for x86-64 as
  well. Proposed to come first, as the proving ground for what both ports share ([X86-64.md](X86-64.md)).
- **A good part can be done before a Linux machine is at hand**: counting which imports a real session reaches
  (on the Mac), and everything up to the game's 3,487 static initializers running, which needs no window and no
  sound (any arm64 Linux, a virtual machine included).

## The first run

```sh
python3 linux/first_run.py --machine      # what this machine has of what the plan asks (Linux)
python3 linux/first_run.py                # the game's own code, untranslated: its 3,487 initializers run, its imports counted
arch -x86_64 /usr/bin/python3 linux/first_run.py   # the same on a Mac, against macOS's own libraries: the reference
python3 linux/test_first_run.py           # its test (ctest: linux.first_run)
```

Nothing is built or installed: the image is mapped and run in the Python process. It needs an x86-64 process and
the decrypted image ([PLAN.md](PLAN.md#x0-the-first-run-with-nothing-built) says how to get it on Linux).

## Set up and play (x86-64)

What it needs: Steam for Linux, signed in with an account that owns Rocksmith 2014 Edition - Remastered (a Steam
purchase has the Windows and the Mac edition both); Python 3; PipeWire; an OpenGL driver (Mesa). The build is
below; on Bazzite it happens in a distrobox, and the game then runs on the host.

```sh
python3 linux/install/rs_install.py setup     # once: the game from Steam, its DLC, RS Re-ARMed's copy, its code
python3 linux/install/rs_install.py           # the game (and the setup first, when there has been none)
python3 linux/install/rs_install.py refresh   # later: DLC bought since, and the copy checked
python3 linux/install/rs_install.py status    # what is set up, and where
python3 linux/install/rs_install.py run -- --size 1920x1080     # the game, with arguments for rs-rearmed
python3 linux/install/test_rs_install.py      # its test (ctest: linux.install), on any machine
```

**The installer's window** is the Mac installer's, for Linux: the game folder (empty: Steam's download), Install
with its progress, what Steam needs from you in a box of its own with a button for it, the log, Refresh Game
Files, Play, and **Settings and switches**: the built-in mods to switch off, and the options in the Mac's groups
(audio and guitar input, picture, songs and Steam account, startup and logging) with the rest under Advanced.
The page has the Mac's keys, titles and files, so a switch means one thing on every system; an option the Linux
build does not have yet is there, greyed, and those about Metal or Apple's OpenGL are left out.

```sh
python3 linux/install/rs_install.py installer   # the window (GTK 3 through PyGObject, which most desktops have)
python3 linux/install/rs_install.py menu        # RS Re-ARMed in the applications menu, with the installer as its action
```

A start from the desktop with nothing set up yet opens it. Install also puts RS Re-ARMed in the applications
menu (a desktop entry and the icon in `~/.local/share`; from an AppImage, a copy of the AppImage is kept in the
home folder's `app/` and the entry starts that, so the download can go). Without GTK for Python the same is done
from a terminal, and `settings.env` and `options.env` hold the switches as on the Mac.

**As an AppImage**, one file that holds the program, its libraries and the setup, and nothing of the game:

```sh
make -C linux all gl bink appimage            # build/linux/RS_Re-ARMed-<version>-x86_64.AppImage (3 MB)
./RS_Re-ARMed-1.20.0-x86_64.AppImage          # the game, after the setup when there has been none
./RS_Re-ARMed-1.20.0-x86_64.AppImage status   # and setup, refresh, run -- ARGS, as above
```

Started from a terminal it says what it does there; started from the desktop, the setup's progress and Steam's
steps are in a window (zenity), and the game's output is in `logs/last-run.log`. `make -C linux appdir` makes the
same thing as a folder (`build/linux/AppDir`), which runs from wherever it is: `AppDir/AppRun`. It carries the
build system's SDL 3 and uses the system's Python 3, C and C++ libraries, PipeWire, ALSA and OpenGL. It runs on
systems as new as the one it was built on (Bazzite 45 so far: glibc 2.44); a build on an older base is to come.

**The setup** asks the running Steam for the Mac game (`download_depot`, the console command, sent on Steam's own
command line, so nothing is typed; 6.9 GB into Steam's `steamapps/content`), then for each DLC the account owns,
whose Mac archives Steam for Linux does not install (each is a depot of its own; a few seconds apiece). It says
what Steam needs when Steam is not open or not signed in, and goes on by itself. Steam's own window shows none
of these downloads. A game folder from elsewhere (a copy from a Mac): `setup --game DIR`. Nothing asked of
Steam: `--no-download`; the game without the DLC: `--no-dlc`.

**What it makes** is in `~/.local/share/rs-rearmed` (`$XDG_DATA_HOME`; `--home DIR` or `RS_NATIVE_HOME` for
another place), laid out as the Mac app's home:

| | |
|---|---|
| `game/` | RS Re-ARMed's own copy of the game's files: clones on btrfs and XFS, which take no room, and a real copy (7.7 GB) elsewhere. A Steam update or a cleared download cannot break it. **Custom songs go in `game/dlc`.** |
| `game/static.psarc.rearmed` | the game's `static.psarc` with RS Re-ARMed's title logo, made by the setup (`tools/loadscreen/logo.py`: Python and `openssl` alone); each start links `static.psarc` to it, or to the original with `RS_REARMED=0` |
| `image/` | the game's code, decrypted, which `rs-rearmed` loads. It is checked to be the build RS Re-ARMed is made for |
| `steam/` | the saves. They start as a copy of Steam's own (`userdata/<account>/221680/remote`, where the Windows game played through Steam keeps the same files), without the stored Ubisoft sign-in; Steam's are never written |
| `settings.env`, `options.env` | the built-in mods switched off (`RS_MODS_INTRO=0`) and the options set (`RS_AUDIO_FRAMES=128`), written by the installer's Settings and switches page or by hand: [the Mac's list](../docs/mac/INSTALL.md), of which `install/rs_settings.py` says what the Linux build has; the environment wins |
| `app/` | a copy of the AppImage, which the applications menu's entry starts |
| `mods/` | mods, each a shared library built against `tools/ws7/recomp/runtime/rc_mod.h` (`linux/test/mods/test_mod.c` is one, with its build line), loaded in name order before the game starts; `RS_LOAD_MODS=0` loads none, `RS_MODS_PATH` names another folder or one library |
| `guitar-input.ini` | per device, by its name: `[HX Stomp]`, `Channel=5`, `GainDb=0` ([the Mac's file](../docs/mac/INSTALL.md), read by the same code); yours to write. Without it the game lists each channel of an interface as an input of its own |
| `logs/` | `last-run.log`, when the game is started with no terminal |

**Each start** does what the Mac launcher does: the game stays offline (`[Net] AutoLogin=0` and no browser, forced
in the game's `Rocksmith.ini`, which otherwise keeps what the game saved), the DLC the copy recorded counts as
owned, one game runs at a time.

The Mac's install tools do the work, as they are; `rs_install.py` gives them Linux's answers where they have the
Mac's: where Steam keeps its files and downloads, how Steam is started and told a command, a clone by the
kernel's own call (`FICLONE`), and the account's DLC from Steam's caches where Steam has written no ownership
ticket for the game (it writes one when a game is started through it, which the Mac game never is on Linux).

## The x86-64 build

```sh
make -C linux                              # build/linux/rs-rearmed: a C compiler, make and Python
build/linux/rs-rearmed --bind-only         # who answers each of the image's 681 imports
build/linux/rs-rearmed --init-only --count # the initializers run, and the imports they called
build/linux/rs-rearmed --boot GAME         # and the game's start, with GAME the Mac depot's folder
build/linux/rs-rearmed --boot GAME --frames 3 --survey   # what its first frames ask for that nothing answers yet
make -C linux check                        # the Darwin layer's own test, the Steam library's, and --init-only

# with SDL 3's, EGL's and OpenGL's headers (on Bazzite: in a distrobox), the renderer and the shell as well:
make -C linux all gl                       # libRSSystemGL.so, libRSOpenGL.so, libCg.so; rs-rearmed with the shell
make -C linux check-gl                     # the renderer's offscreen test and the Cg library's render test
make -C linux bink                         # libBink2.so, for the videos: downloads and builds FFmpeg (curl, patch, xz)
build/linux/rs-rearmed --run GAME          # the game in a window
SDL_VIDEODRIVER=offscreen build/linux/rs-rearmed --run GAME --frames 600 --picture f --picture-every 60
                                           # with no display: ten seconds, a frame saved every second (f.<n>.ppm)
#   --keys 1300:return,1500:down           keys typed by a script, each at its frame
#   F11, Alt+Enter                         full screen and back; the game's own Windowed Mode setting is followed
#   RS_FULLSCREEN=0|1  RS_FPS=N  RS_WINDOW_ANY_SHAPE=0  RS_GL_MTENGINE=1    the installer's Picture switches
#   RS_AUDIO=silent                        the game's sound asked for and dropped (PipeWire is used where it was
#                                          there to build with); RS_AUDIO_LOG=1 logs the audio layer's questions
python3 linux/test_loader.py               # its test (ctest: linux.loader)
```

The image is `--image FILE`, else `RC_IMAGE`, else `build/steamless/Rocksmith2014_x64_decrypted`. An import that
nothing answers yet stops the run when the game first calls or reads it, and is named.

## The census

```sh
python3 linux/import_census.py            # the table above, by library, from your own decrypted image
python3 linux/import_census.py --check    # fails when the table and the image name different imports
python3 linux/import_census.py --list     # every import with its class
python3 linux/test_import_census.py       # its test (ctest: linux.imports)
```

It reads the Mach-O itself, with no Apple tool, so it runs on Linux too. The image is the one
`tools/steamless/decrypt.py` makes (`build/steamless/Rocksmith2014_x64_decrypted`, or `RC_IMAGE`); without one it
counts the table alone.
