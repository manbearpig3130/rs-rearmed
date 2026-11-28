# Linux, arm64 and x86-64 (investigated; x86-64 begun)

A port of RS Re-ARMed to Linux, with Linux in the place of macOS underneath the Mac game. Two machines are in mind:
an Apple silicon Mac running Fedora Asahi Remix, where the game would otherwise need an x86 emulator inside a
virtual machine and gets the same translated code as on macOS; and an x86-64 desktop, where the game's own code can
run as it is.

**There is no game to play here yet.** This folder holds the investigation of what the port takes, the plan, two
tools, and the start of the x86-64 build: a loader that maps the game's image on x86-64 Linux, answers its
imports and runs its 3,487 initializers as they are (on Bazzite, 2026-11-28). No window, no sound, no install.
The work is on the branches `port/linux-arm64` and, on top of it, `port/linux-x86_64`.

| Read | For |
|---|---|
| [FINDINGS.md](FINDINGS.md) | what each part of the project takes from macOS today, and what Linux has in its place, for arm64 |
| [X86-64.md](X86-64.md) | what differs on x86-64: the game's code runs as it is, and what that takes |
| [PLAN.md](PLAN.md) | the steps, what each proves and needs, the open questions, the risks, and a log of what was done |
| [darwin-imports.tsv](darwin-imports.tsv) | every import of the Mac game's image, with what a Linux build does about it |
| `first_run.py` | the first thing to run on an x86-64 Linux machine: the game's initializers, with nothing built |
| `Makefile`, `src`, `darwin` | the x86-64 build: the loader, and the Darwin layer that answers the game's imports |
| `compat` | the few macOS functions the project's shared libraries call, for Linux, so that `shims/` builds from its own sources |

## In short

- **The translated game is not the obstacle.** The C the recompiler writes for the Mac game (243 files, 617 MB)
  passes a compile check for `aarch64-linux-gnu` as it is. Of the system it includes four headers, none of them
  Apple's.
- **What the game calls is.** The Mac game's image imports 681 symbols from 19 macOS libraries, and the Mac app
  binds them to the real arm64 libraries of macOS. On Linux each needs an answer:

  | What a Linux build does | Imports | |
  |---|---|---|
  | The host's own function of that name (glibc, libm, GNU libstdc++) | 259 | no code |
  | The project's shims (the renderer, Cg, Bink, Steam), built for Linux | 162 | a build, and the renderer's OpenGL path on Mesa |
  | The runtime's own (`rc_file`, `rc_setjmp`) | 11 | small changes |
  | A function of ours over the host's (flags, struct layouts, `errno`) | 30 | new, a few lines each |
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

## The x86-64 build

```sh
make -C linux                              # build/linux/rs-rearmed: a C compiler, make and Python
build/linux/rs-rearmed --bind-only         # who answers each of the image's 681 imports
build/linux/rs-rearmed --init-only --count # the initializers run, and the imports they called
build/linux/rs-rearmed --boot GAME         # and the game's start, with GAME the Mac depot's folder
build/linux/rs-rearmed --boot GAME --frames 3 --survey   # what its first frames ask for that nothing answers yet
make -C linux check                        # the Darwin layer's own test, the Steam library's, and --init-only
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
