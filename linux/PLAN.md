# Linux: status and plan

What the port needs and why is in [FINDINGS.md](FINDINGS.md) for arm64 and [X86-64.md](X86-64.md) for x86-64; this
page is what to do about it, in what order, and what has been done. The editions' own plans: [Mac](../docs/mac/PLAN.md), [Learn & Play](../docs/windows/PLAN.md).

## Status

- **Investigated** (2026-11-26, from 1.20.0), **and begun on x86-64** (2026-11-28): a loader builds there and runs
  the game's 3,487 initializers and then the game itself, as it is, in a window: the intro, the title screen,
  and, past the sign-in that the offline services replace, the menus, with sound out, the videos, and the Real
  Tone Cable heard at the calibration screen ([X1](#x1-the-games-initializers-run), [X2](#x2-a-picture),
  [X3](#x3-sound-and-the-guitar)). Every import but the 98 that nothing should call has an answer. A player
  has calibrated and played a song with the cable (2026-11-28): no crackle, the notes detected, the latency as
  good as they know it. The built-in fixes module runs whole, at hook points and mid points. The game's files are
  set up from the player's Steam, and the game started from them, by `install/rs_install.py`
  ([X4](#x4-install-and-play)). The title logo is made there too, the window goes full screen, and an interface's
  channels are inputs of their own. Not there: mods and edits, anything for arm64.
- **In the tree**: the findings, this plan, the table of the Mac game's 681 imports with what a Linux build does
  about each (`darwin-imports.tsv`), the tool that reads them from the image and checks the table
  (`import_census.py`, ctest `linux.imports`), the first run (`first_run.py`), and the x86-64 build: the loader
  (`src`), the Darwin layer's first functions (`darwin`), a `Makefile` and their tests.
- **Checked so far**: the translated game's C passes a compile check for arm64 Linux unchanged; the import table
  matches the image and a build's thunk report; which imports only the replaced parts of the game call
  ([how](FINDINGS.md#how-this-was-found)).
- **The game**: the Mac game first. Learn & Play is step 6, and would be written without a run: it is not owned
  where this work is done.
- **Asked for** (2026-11-27, the project's issue 18): a Linux build, by a player on Bazzite with KDE, as an
  AppImage.
- **The machines in mind**: for x86-64 a PC with Bazzite, the image-based Fedora the request came from; for arm64
  an Apple silicon Mac with Fedora Asahi Remix. The Bazzite PC is at hand since 2026-11-28 (an AMD Ryzen 5 PRO
  4650GE with its integrated Radeon graphics; Bazzite 45, glibc 2.44, Mesa 26.3.1, PipeWire 1.6.11); the Mac is
  not yet.
- **The first run** (2026-11-28): `first_run.py` maps the image, answers its imports and runs its 3,487 static
  initializers in a Python process, with nothing built. In an Intel process on a Mac, against macOS's own
  libraries, all of them run, and they call 16 imports: 11 the table has as `same`, and five more
  (`pthread_mutexattr_settype`, `sched_get_priority_min` and `max`, `__bzero`, `pthread_key_create`), which
  have stand-ins in the script. **On Bazzite the same**: 3,487 of 3,487, the same 16 imports, against glibc and
  the system's libstdc++ ([X0](#x0-the-first-run-with-nothing-built)).
- **x86-64** (2026-11-26): the Mac game's code can run there as it is, and the translated C also passes a compile
  check for it ([X86-64.md](X86-64.md)). Proposed to come first ([below](#two-hosts-one-order)).

## Two hosts, one order

Both ports need the same four pieces around the game: the Darwin layer that answers its imports, the shell, the
renderer's OpenGL path and the audio layer, with the install. They differ in what runs the game's code: on x86-64
the code itself, on arm64 its translation with the thunks and the runtime in front of the layer.

Proposed: **x86-64 first, running the original code**, then arm64.

- It needs the least to boot: a loader the project already has in its test harness, and the layer. No recompiler
  work, no thunks.
- It proves the shared pieces with code known to be right. A fault on arm64 could be the layer's, the thunks' or
  the translation's; on x86-64 run directly it can only be the layer's or the shell's.
- Its machine needs no reinstall, and its OpenGL driver is not a question.
- What arm64 then adds is [its step 1](#1-the-tree-builds-on-linux-and-the-games-initializers-run) without the
  layer, and the questions that are Asahi's own.

The cost is the second mechanism for hooks, mid points and edits ([x86-64, step 5](#x5-fixes-and-mods)), which can
be put off, or avoided by translating there too.

## x86-64

[What differs](X86-64.md). Each step ends in something that runs.

### X0. The first run, with nothing built

On the machine itself, with Python and Steam alone:

- **The game's files**: Steam's console (`steam://open/console`, or Steam started with `-console`) and
  `download_depot 221680 221682 2035751420671775739`, the Mac game's depot at the build the tables are for
  (`tools/targets/mac-x64/target.toml`): 6.9 GB. Steam says where it put it, or that the account's license lacks
  the depot.
- **The image**: `python3 tools/steamless/decrypt.py --game <that folder>`.
- **The machine**: `python3 linux/first_run.py --machine` says what the open questions ask: the image's address
  free, the 259 `same` imports in the host's libraries, what OpenGL context EGL gives and with which of the game's
  extensions, PipeWire and its clock, Steam's folders, FUSE for an AppImage, what there is to build with.
- **The run**: `python3 linux/first_run.py`. Every import is counted; one that nothing answers stops the run and
  is named.

Done when: all 3,487 initializers run on x86-64 Linux, and the machine's report is in hand.

**Done 2026-11-28 on Bazzite**, first with the image decrypted on a Mac and copied over, then by this route whole:
`download_depot` in Steam on Bazzite puts the depot in
`~/.local/share/Steam/steamrt64/steamapps/content/app_221680/depot_221682` (Steam names the folder when it is
done), and `decrypt.py` there writes the same image, byte for byte. The run: 3,487 of 3,487, and the same 16 imports with the same counts as on the Mac. The
report:

| Asked | Bazzite 45, AMD Renoir graphics |
|---|---|
| The image's place | 0x100000000 is free, and memory may be made executable |
| The `same` imports | 259 of 259 found in the host's libraries (GNU libstdc++: 93 of 93) |
| OpenGL | a compatibility context from EGL with no window: 4.6 on Mesa 26.3.1 (radeonsi), GLSL 4.60 |
| The game's extensions | all six: the ARB vertex and fragment programs, `EXT_gpu_program_parameters`, S3TC, `EXT_bgra`, `ARB_get_program_binary` |
| Vulkan | 1.4 (RADV), were Zink ever wanted |
| Sound | PipeWire 1.6.11 at 48,000 Hz; its quantum is 1,024 frames by default and 32 at the least, so 128 and 256 are for the game to ask |
| Libraries on the host | `libpipewire-0.3`, `libjack`, `libasound` and `libSDL3` are all there |
| AppImage | `/dev/fuse` and `fusermount3` are there |
| To build with | `gcc` and the C library's headers on the host itself (enough for X1); no headers for EGL, SDL or PipeWire, so the later steps build in a distrobox |

### X1. The game's initializers run

- **The loader**, from `shims/game/harness/rsimage.c`: the image mapped at its own address, rebased, bound, its
  3,487 initializers run, an unanswered import named. About 400 lines.
- **The Darwin layer, first cut**, as in [arm64's step 1](#1-the-tree-builds-on-linux-and-the-games-initializers-run),
  bound by name with no thunks: the `same` rows, the `adapt` and `emulate` rows the initializers reach, and
  a function that names itself for every other row.
- **Tests without the game**: small programs built as x86-64 Mach-O (clang's Darwin target, LLVM's Mach-O linker;
  to check) that call the imports with Darwin's layouts and numbers and are run through the loader. The same
  programs serve arm64 through the translator.
- **`shims/libstdcxx`'s ABI test** against the system's library.

Done when: `--init-only` runs all 3,487 on an x86-64 Linux machine.

**Done 2026-11-28 on Bazzite.** `make -C linux` builds `build/linux/rs-rearmed` with the host's `gcc`, `make` and
Python and no other package; `rs-rearmed --init-only` maps the image at 0x100000000, binds its 681 imports and runs
all 3,487 initializers (a millisecond; the load takes 18).

- **The loader** (`src/loader.c`, `src/imports.c`, 492 lines). Nothing is rebased: the image is at its own
  address or the run stops. An import is the Darwin layer's where it has one, the host's own where the table
  says `same`, and otherwise an address in a region with no access, one per import: the first call or read of
  it stops the run with its name, its class, where it was called from and in which initializer (exit status 3).
  So a `same` row is the one way to the host's function: a name the table lacks is not bound by accident.
  `--count` puts a counter in front of every called import and lists the calls; `--bind-only` stops before
  the initializers.
- **Who answers today**: 259 the host's own, 15 the layer's, 407 nothing yet (adapt 26, emulate 64, runtime 11,
  shim 162, layer 31, refuse 15, stub 98).
- **The Darwin layer's first cut** (`darwin`, 9 functions and 6 objects): the five the initializers call
  (`pthread_key_create`, `pthread_mutexattr_settype`, `sched_get_priority_min` and `max`, `__bzero`), `__error`
  with the errno rule and its table of numbers, the rune table with `__maskrune`, `__tolower` and `__toupper`,
  the stack guard, the three standard streams and the task port. A function registers under its import's name
  where it is defined (`DW_FUNCTION`), gathered by the linker.
- **Unwinding.** The image's `__eh_frame` is registered with the host's unwinder (libgcc's, looked up at run
  time so that it is the one libstdc++ throws through). It finds a frame description for 3,275 of the 3,487
  initializers. No throw through the game's code has been tried yet.
- **Tests** (`test_loader.py`, ctest `linux.loader`): the build without a warning, the layer's own test
  (`test/layer_test.c`: each function called with Darwin's layouts and numbers), a made-up image whose
  initializer calls one import, an import with no answer named, and the real image's initializers with the
  counts `first_run.py` has.
- **Left for later**: the test programs built as Mach-O (the layer's own test and the made-up image stand in
  until arm64 needs them through the translator), and `shims/libstdcxx`'s ABI test against the system's library
  (it comes with the shims, in X2).

### X2. A picture

As [arm64's step 2](#2-a-picture) on a desktop GPU: the OpenGL path on EGL, the shims built as x86-64 libraries,
the shell, a silent audio layer. The seam's calls into the game are plain calls. The three functions in the place
of the engine's Objective-C and the online stub's nine go in as jumps at their functions' entries, the first of
[the patches](X86-64.md#running-the-original-code).

Done when: the title screen, the menus, and a song's note highway without sound.

**Begun 2026-11-28: the game's start runs.** `rs-rearmed --boot DIR` calls the game's `GameBootstrap` after the
initializers, with DIR as the game's folder (the depot as Steam downloaded it), and it runs to its end: the game's
root, Lua, `Rocksmith.ini`, the display settings, the file system with `cache.psarc` opened, the main service.

- **The layer has 84 functions** now (`linux/darwin`): pthreads (objects with Darwin's static initializers made
  on first use, the numbers, scheduling accepted and not applied), Mach's clock, semaphores and port type, the
  `OSAtomic` calls, `sysctl` for the fourteen names and the one numbered question, the files (`open`'s flags,
  Darwin's `struct stat` and `struct dirent`, `fcntl`, `poll`, `select`, `sem_open`), `setjmp`, signals, the
  locale, `strerror`, and the fifteen network calls, refused. Who answers now: 259 the host, 90 the layer, 332
  nothing yet (emulate 41, shim 162, layer 31, stub 98).
- **Names in any letter case.** The game lower-cases the paths it builds (a `Steam` folder becomes `steam`), which
  macOS's file system forgives. The layer's `dw_path` finds a path that is not there as written by looking for
  each missing part under another case. Without it the game finds none of its files on Linux.
- **`sysctl` answers as an Intel Mac** on x86-64 (SSE2 and the rest present, `x86_64`, the CPU's frequency), not as
  the Apple silicon Mac the Mac app runs on: these are the answers the game's code was written against.
- **A survey** (`--survey`): a function with no answer is noted and returns 0, and the run goes on, so one run
  lists what a stage asks for. What follows such a call proves nothing; it is for making the list.
  `--frames N` then calls `GameFrame`. The first frame asks for the Steam library (`SteamAPI_Init`, its
  callbacks and contexts) and CoreAudio's devices; then the renderer.
- **The Steam library** (`shims/steam`, the Mac app's own sources) builds for Linux as `libsteam_api.so` beside the
  program, and the loader binds a `shim` row to the library of its name. Its three tests pass there as on the
  Mac (the vtable slots, the DLC folder, the library's behaviour). The game's interfaces are called straight
  through their vtables, and its callbacks straight back. What it took: `linux/compat`, four headers and one file
  with the macOS functions it calls (`copyfile`, `removefile`, `_NSGetExecutablePath`, the boot time), and one
  `#if defined(__linux__)` in `shims/steam/src/shim.cpp` for where Steam keeps its files. The saves go to
  `steam/` in the port's folder (`RS_NATIVE_HOME`, else `rs-rearmed` under XDG's data folder).
- **With it, three frames run in a survey**, and ask for the display's modes (`CGMainDisplayID`,
  `CGDisplayCopyAllDisplayModes`, CoreFoundation's arrays), CoreAudio's devices, and Cg: the renderer starts.
- **The display's modes and a small CoreFoundation** (`dw_display.c`, `dw_cf.c`): one display whose modes the
  shell will give; strings that read the image's own constant strings as well as the layer's, arrays, UUIDs, the
  locale; IOKit's registry answering that there is no Ethernet interface. The layer has 114 functions and 16
  objects; who answers: 259 the host, 130 the layer, 12 the Steam library, 280 nothing yet (emulate 7: vDSP and
  two matrix helpers; shim 150: Cg 29, OpenGL 105, Bink 16; layer 25: the audio devices and graph; stub 98).
- **The first three frames then stop at two things only**: CoreAudio's devices and `cgCreateContext`, where the
  renderer starts.
- **The renderer's libraries build from their own sources** (`make -C linux gl`, in a distrobox with Mesa's and
  EGL's headers): `libRSOpenGL.so` from `shims/opengl` (the wrapper table, the tracing and checking tools, the
  offscreen contexts) and `libCg.so` from `shims/cg`, unchanged. Under them is one new library,
  `libRSSystemGL.so` (`linux/gl/system_gl.c`): Mesa as the OpenGL backend expects Apple's framework to be. It
  answers CGL over EGL (an offscreen context is an EGL context on Mesa's surfaceless display; the current
  context is whichever EGL context is current), reports Apple's legacy identity (version 2.1, GLSL 1.20, the
  extension list the Metal core answers, with the ARB assembly programs where the driver has them), and has a
  forwarder for each of the table's 144 OpenGL names, since libglvnd does not export every older alias. The
  Mac build's generators make the table from Khronos's headers, put in the form they read
  (`gl/khronos_headers.py`). `linux/compat` gained Apple's OpenGL header names, Mach's clock, and
  `rs_linux.h`, which maps the two places the shared code names a macOS library. No shared source is changed.
- **Their own tests pass on the Bazzite PC's Radeon** (`make -C linux check-gl`): the offscreen context's 52
  checks with every call checked for an OpenGL error, and the Cg library's render test on its `arb` and `glsl`
  backends.
- **The shell** (`src/shell.c`, SDL 3): a window with a compatibility context made through EGL, the frame loop
  (one frame a refresh, or 60 a second where the swap does not wait), keys as macOS key codes with Cocoa's
  characters, the pointer, the wheel, focus, the window's size as the engine's resolution, the display's modes
  for the renderer's list. It stands on the Mac shell's C half (`rsp_engine.c`, `rsp_logic.c`, `rsp_paths.c`,
  `rsp_config.c`) and the app's seam (`seam_platform.c`), compiled as they are against `linux/direct`: the
  runtime's `rc.h`, `rc_abi.h` and `rc_rt.h` with the meaning they have where nothing is translated (an image
  address is the address; calling the game is a call). So far one thread makes the context, starts the game and
  runs its frames, and the window is never full screen.
- **The audio layer's silent form** (`dw_audio.c`): one output device and the game's graph, its callback called
  on the clock and the sound dropped.
- **The game draws** (2026-11-28): `rs-rearmed --run GAME` goes through the intro (the publisher's logo, the
  brands) to the title screen, "Rocksmith 2014 Edition Remastered, Enter: Begin", and stays there: 3,600
  frames, a minute, with no fault and no import without an answer. Seen through `--picture`, which saves a
  frame as a file; with SDL's offscreen driver the run needs no display. Who answers: 259 the host, 157 the
  layer, 146 the project's libraries, 119 nothing yet (vDSP's 5, Bink's 16, and the 98 stubs).
- **Input works, and the first patches are in** (2026-11-28). `--keys 1300:return` types Enter on the title
  screen from a script. With the game's functions all its own, that opens the Uplay sign-in dialog. With the Mac
  app's `online` override module in place it opens the profiles' menu, over the rehearsal room in 3D.
  - **The module is the Mac app's, compiled as it is**: `overrides/src/online_glue.c`, `overrides/gen/online.c`
    and `shims/game/src/online_stub.cpp`, against the runtime's own headers (`rc.h`, `rc_abi.h`, `rc_rt.h`,
    `rc_ov.h`), which compile on Linux unchanged. `src/direct.c` has the runtime's functions as they are where
    nothing is translated: the slide is zero, `rc_call_guest` and a virtual call are calls, and the adapters
    between the game's functions and native ones do nothing. The seam uses the same headers.
  - **A patch is a jump at a function's entry** (`src/patch.c`): 13 bytes to a stub that puts the CPU's
    registers into an `rc_ctx`, calls the override, and returns its results to the game's caller. The nine
    functions are replaced whole, so no instruction of theirs is moved. This is the first form of
    [X5](#x5-fixes-and-mods)'s patches; hook points and mid points, which run the function's own instructions
    after the native code, still need the moving.
- **The menus go on** (scripted): a profile is made and saved (its `_PRFLDB` file and `LocalProfiles.json`, through
  the Steam library's storage), the auto-save notice, the choice of path with its pictures, the input selection.
  Two faults on the way were threads: the game asks for stacks sized for macOS, and glibc, which takes each
  thread's thread-local storage out of its stack, refused one once more libraries were loaded; the game then
  joined the thread it never got. A thread now has 1 MB at the least, and joining or detaching a handle of zero
  is `ESRCH`, as on macOS.
- **Videos play**: the Bink library builds from `shims/bink/bink_shim.c` as `libBink2.so` (`make -C linux bink`),
  over the same FFmpeg 7.1.2 with the same Bink 2 patch; `linux/bink/build-ffmpeg.sh` reads the version and
  the checksum from the Mac's script. The calibration screen's tutorial video is drawn.
- **Next**: a song started (the note highway); the three functions in the place of the engine's Objective-C
  when a run reaches them; full screen; the render thread; hook points, for the built-in fixes.

### X3. Sound and the guitar

**Sound out works** (2026-11-28). The audio graph's sound goes to a PipeWire stream (`src/audio_pipewire.c`, the
backend of `dw_audio.c`; the silent clock stays for a build or a machine without PipeWire, and `RS_AUDIO=silent`).
The stream asks for the game's buffer, 256 frames at 48,000 Hz, as its latency, and on the Bazzite PC PipeWire
gives it: 256 frames a call, 5.3 ms. The game is asked in buffers of its own size and PipeWire's requests are
met from a small queue between. The intro and the title play (the loudest sample of each five seconds is
logged with `RS_AUDIO_LOG=1`). Not measured yet: the latency end to end, and underruns under load.

**The Real Tone Cable is heard** (2026-11-28). The inputs are PipeWire's capture devices (`Audio/Source` nodes),
each a CoreAudio device to the game: named as its ALSA card is, which for the cable is "Rocksmith USB Guitar
Adapter", the name the game knows it by (open question 5: answered), with the maker the card's long name has
in front of that ("Hercules") and a model of the name and the USB ids. The game lists them, reads each one's
identifier, channels, name, maker, buffer range, rate and formats, sets a 128-frame buffer, makes an I/O
procedure and starts it; that opens a capture stream on the node asking for 128 frames, and PipeWire gives 128
(2.7 ms) on the Bazzite PC. The game opened the cable and the PC's own analog input, and at the calibration
screen its meter follows the cable. The FFT its note detection runs is `dw_accelerate.c` (vDSP's
`vDSP_fft_zop`, radix 2, vDSP's scaling). Not done: devices coming and going while the game runs (the list is
read again when the game asks, and no listener is told), each channel of an interface as an input of its own
and the rest of the app's device policy (`shims/coreaudio`, the `audio` override module), and real-time
scheduling for the game's audio thread.

**The input's level** (2026-11-28, from a player's first calibration on Linux). The game sets a level from 0 to 1
on an input (`volm`), starting a device at 0.375 and raising it during calibration. On the Mac that moves the
cable's own gain. Linux leaves the cable's capture control at its top, +10 dB of a range from -8 to +10, and
with the level not applied the guitar came in 11 dB hotter than the game expects: calibration passed on a soft
strum, and the game reported a sudden change of signal when it ended. Now, as the Mac app has it: the cable's
level is its ALSA capture control, the level being the control's place in its range (`src/audio_alsa.c`; 0.375
is step 14 of 37, -1.2 dB), read back from the hardware; any other input's level is a gain of ours on what is
captured, unity at 0.375 and +24 dB at 1.

[arm64's step 3](#3-sound-and-the-guitar) as it is: the audio layer on PipeWire, the FFT, latency measured, here
against the same machine's Proton route as well.

### X4. Install and play

[arm64's step 4](#4-install-and-play-from-a-clean-machine), with Steam at hand: the depot through its console or
`steamcmd`. No build of the game: the install decrypts the image.

**As an AppImage**, as asked. Run untranslated, the program holds none of the game's code: the loader, the Darwin
layer, the shell, the shims and the tools. So unlike a translated app it can be a download, made once, and the
player's part is the game's files from their own Steam. It is built as a folder that runs from wherever it is
(its libraries beside it, nothing installed), and the AppImage is that folder packed; a plain archive of the
same folder serves a system without FUSE, or one that drops AppImages. The host's C library, libstdc++, OpenGL
driver and PipeWire are used, not carried. On an image-based system such as Bazzite the build itself happens in
a container (distrobox), since the host has no compiler.

**Done (2026-11-28): the setup and the start**, `install/rs_install.py` ([README](README.md#set-up-and-play-x86-64)).
The Mac's install tools run as they are (`tools/play-native/gamefiles.py`: the depot's download with Steam's
steps and progress, the DLC, the copy with its manifest and checks; `tools/steamless/decrypt.py`: the image),
with Linux's answers set from outside, so the Mac's code and its tests are untouched:

- **Steam's files** are where the Mac's are but for the downloads: `steamapps/content` is under the folder the
  client runs in (`steamrt64` on Bazzite 45). Its logs read the same, line for line, so the Mac's progress and
  failure reading works unchanged.
- **A command for the running Steam** goes on `steam`'s own command line (`steam +download_depot ...`), as on
  the Mac; the Flatpak's the same way, not yet tried.
- **The DLC.** Steam for Linux installs the Windows archives; the Mac ones are in each DLC's own depot, fetched
  one by one as Learn & Play's are. The list of what the account owns is the ownership ticket on the Mac. Steam
  writes that when a game is started through it, so a Linux machine that never ran the Windows game has none:
  there the list is the apps of the licenses in Steam's package cache that its app info gives the game as their
  parent (`appinfo.vdf`: binary, the keys as numbers into a string table since version 0x29). On the Bazzite PC:
  219 such apps, of which 187 have a depot (the songs) and 32 have none (the packs); all 187 came, in about
  eight minutes.
- **Clones** by `FICLONE`, tried with a real file and not ruled out by the device number first, as the Mac's
  check does: btrfs gives each subvolume its own. 312 files, 7.7 GB, cloned in eight seconds.
- **The start** is the Mac launcher's, in Python: `settings.env` and `options.env`, the copy's `steam.env`, the
  offline keys in `Rocksmith.ini`, the saves seeded from Steam's without `crd`, one game at a time (a lock the
  game's process holds), the output to `logs/last-run.log` when it would go nowhere.
- **With no terminal** the setup's progress and Steam's steps are in a window (zenity), and so is what went
  wrong.

**Done (2026-11-28): the packaged build.** `make -C linux appdir` lays the program, its libraries, the setup and
the Mac's tools it uses out as an AppImage's folder (`usr/bin/rs-rearmed` the command, `usr/lib/rs-rearmed` the
program with its libraries beside it, as it looks for them, `usr/share/rs-rearmed` the setup); `make -C linux
appimage` packs it with appimagetool 1.9.1 and the runtime of 2025-11-08, each downloaded once and checked
against its SHA-256. 3.2 MB, nothing of the game in it. On the Bazzite host (not the build's container) it set
the game up in the player's real folder and started it. The build system's SDL 3 is carried, the one library a
system may lack; the system's Python 3 runs the setup. Built on Fedora 45 it needs glibc 2.44: a build on an
older base (a container with an older distribution, SDL 3 from source) is the next step for other systems.

**Done (2026-11-28): the title logo.** The Mac draws the tag with Pillow, numpy, etcpak and a font of macOS's,
none of which a Linux desktop is sure to have. The tag itself is our own artwork and no part of the game, so it
is in the tree as a picture (`tools/loadscreen/tag.png`, which a test holds to what `rearmed.py` draws), and
`tools/loadscreen/logo.py` puts it on the two logo textures with the standard library alone: a PNG reader, DXT5
blocks decoded and encoded (the blocks under the tag only; its encoder is closer to the uncompressed picture
than the Mac build's, 30.7 dB against 27.0), the archive rewritten by `psarc_pack.py`. Four seconds in the
setup or a refresh, and each start links `static.psarc` to it.

**Done (2026-11-28): the installer's window**, `install/rs_gui.py`, after the Mac installer's, in GTK 3
through PyGObject (on the Bazzite host as on most desktops; Tk and the Qt bindings are not). The work is still
`rs_install.py`'s commands, run as a child that writes progress lines, as the Mac installer runs its scripts.
What differs from the Mac's window is what Linux has no use for: no build folder, no renderer choice, no second
game. Settings and switches is the Mac's page from the Mac's lists (`install/rs_settings.py`, which a test holds
to the Swift source: keys, titles, details, groups, defaults), each option with whether the Linux build has it.
**What the page shows greyed is the list of what is still to do**: the guitar input's channel, gain and list
(`RS_AUDIO_INPUT*`), the frame rate (`RS_FPS`), the window's shape and mode (`RS_WINDOW_ANY_SHAPE`,
`RS_FULLSCREEN`), mods at startup (`RS_LOAD_MODS`), the file log. `RS_AUDIO_FRAMES` came with it: the output's
buffer asked of PipeWire (128 frames were granted: 2.7 ms). Install puts the game in the applications menu.

Not done: the account's ticket-based choice when
several accounts use one Steam (the library's own choice, the last signed in, stands), the RS1 DLC asked of
Steam (the package cache answers), a copy on another drive than the home folder.

### X5. Fixes and mods

**Hook points, mid points and the whole fixes module are in; edits and mods are not** (2026-11-28).

- **A hook point** is a 5-byte jump at the function's entry to a stub near the image (`src/patch.c`). The stub
  makes an `rc_ctx` of the CPU's registers and calls the runtime's own `rc_hook_call`; its `orig` runs the
  function from the context: the argument registers loaded, eight words of stack arguments copied, then the
  instructions the jump covered, moved behind the stubs, and a jump to the rest. The mover reads the few kinds
  of instruction a function starts with (pushes, moves, a stack adjustment, a `call`), corrects a displacement
  from the instruction pointer, and refuses anything else with a line naming the function. All 21 hook points'
  first instructions are of those kinds. The stubs' memory is just past the image, so that 32-bit displacements
  reach both ways; an override's jump is 5 bytes now too.
- **The runtime's `rc_hook.c` and the target's `rc_target.c` are compiled as they are**, and so is the fixes
  module (`overrides/mods`: `mods.c`, `mods_shared.c`, `framerate.c`, `menu_options*.c`, `fftsetup.c`). Its hooks
  register and run: no intro screens (the title is up in 5 seconds for 17), the Auto Save notice skipped, a
  song in `dlc/` counting as owned, the cache overlay's file opening, Player 2's input mode. Nine of the
  eleven functions it replaces are replaced (the keyboard's two, the mouse wheel, the thread priority, the path
  MD5 cache, the JSON-number hash's three, the `.sng` signature check, whose override runs the game's own
  check for other callers through its moved start).
- **A mid point** (a hook before one instruction inside a function) is the same jump, to a stub that steps over
  the 128 bytes a function may use below its stack pointer, saves the flags and every register into an `rc_ctx`,
  calls the runtime's own `rc_mid_call` (`rc_mid.c`, compiled as it is), takes the registers and flags back, and
  goes on: to the moved instruction and then the rest, or to the continuation a hook chose. The mover reads more
  kinds here (compares, branches, calls through memory, SSE arithmetic); a short branch becomes its long form.
  A continuation that lands among the covered instructions goes to the moved copy: `fs_exists` skips the call
  at its point that way. Only a point that has a hook is patched: the fixes' thirteen, all of which go in, and
  with them the two functions of the batched DLC mounting. The module then says: frame rate on at 8 of 8
  points, the Guitarcade Skip Tuner on, 12 options in Adv Display Settings.
- **Where a jump may be written** is not told by the bytes at the site: a branch of the function could land
  inside the five bytes. `linux/check_patch_sites.py` reads each site's whole function with the recompiler's
  disassembler and writes `linux/patch-sites.tsv` (address, bytes covered, yes or no; none of the game's bytes).
  Of the 157 sites of the tables (21 hook points, 136 mid points) a jump cannot be written at five, all of the
  Seven-String mod's; the build leaves such a point alone, with a line. They would need another way in (a
  breakpoint, or the instructions before the point moved as well).
- **Not in**: edits (other instructions in the place of some: 620, all rearmed-mods'), and mods themselves, which
  are Mach-O libraries. The runtime's switches for edits are there and nothing applies them.
- **Three things the build needed**: gcc reads a weak constant's default where it is used when optimizing, so
  `rc_hook.c` is built at `-O0` for the hook points' table to be the one `patch.c` gives; the fixes name the
  C++ allocator as Mach-O writes it (`__Znwm`); and `rc_nofile.h` asks `sysctlbyname` for the open-file limit
  (`linux/compat`).

The built-in fixes and mods are written against the runtime's hook points, mid points and edits. Two ways, decided
here with figures:

- **Patches**: entry jumps for the 21 hook points and the overrides, the 459 in-place edits as byte patches, and a
  plan made at build time for the 136 mid points and the 161 edits that replace a range. First the built-in fixes'
  share (13 mid points), then the mods'.
- **Translate on x86-64 too**: the arm64 mechanism as it is, once arm64's step 1 exists. Measure its speed against
  the original first.

## arm64: the steps

Each step ends in something that runs, so a wrong turn shows early. Sizes are of new or changed code and are
estimates from the Mac's counterparts. If x86-64 goes first, the Darwin layer, the shell, the OpenGL path, the audio
layer and the install arrive from it, and steps 1 to 4 here are what is left of each.

### 0. Count what a session reaches (on the Mac)

The table's classes come from reading; a run settles them.

- A switch in the thunk generator (`thunks/gen_thunks.py`) that counts the calls of each import, and notes the
  arguments that decide a stand-in: each `sysctl` name, each audio property selector by kind of device, each
  CoreFoundation and IOKit key. Written to a file in `logs/` when the game quits. Off, the thunks are as they are.
- A build of the Mac app with it, and one session through what the game has: the title, a profile, a song with a
  guitar, Score Attack, a Guitarcade game, Session Mode, a lesson with its video, the tone designer, Multiplayer's
  screens, ENUMERATE, a paste into the song search, EXIT GAME.
- The result goes into `darwin-imports.tsv`: every `stub` confirmed or moved, and the rows a session reaches
  marked, so step 1 starts with what the game needs first.

Needs: the Mac, the game, a guitar, one build. No Linux.

### 1. The tree builds on Linux, and the game's initializers run

No window and no sound: any arm64 Linux does, a virtual machine on the Mac included.

- **A build for Linux**: a driver under `linux/` that runs the same generators with Linux's tools
  ([the table](FINDINGS.md#installer-and-build)); everything of ours compiled with `-fsigned-char` and frame
  pointers. The shared sources have no platform conditional today; the port adds them in as few files as it can.
- **The runtime**: its dozen Darwin calls, the two assembly files, the `va_list`, the `jmp_buf` size, the main
  thread's stack, `.so` mods ([the table](FINDINGS.md#runtime)). About 150 lines in 10 files.
- **Thunks without the macOS SDK**: the Darwin layer's own header as `sigs.py`'s source of prototypes, and
  `import_census.py`'s reader in the place of `xcrun objdump`.
- **The Darwin layer, first cut**: the table from `darwin-imports.tsv`; the `same` rows; the `adapt` and `emulate`
  rows the initializers and the boot reach (memory, threads and their static initializers, `errno`, the rune table,
  files, time); every other row a function that logs its name and stops. About 1,500 lines with its tests.
- **The three native overrides** in the place of the engine's Objective-C: the clipboard, the alert, the folder
  listing, in a list of their own so that the Mac build is as it was.
- **Tests on Linux**: `shims/libstdcxx`'s ABI test and its 93 names against the system's library, the first thing
  to run; `recomp.hooks` in both runtimes; a test of each adapter against Darwin's layout and numbers.

Done when: `--bind-only` binds all 681 imports with none unresolved, and `--init-only` runs the game's 3,487
initializers.

### 2. A picture

Needs a GPU with an OpenGL compatibility context: the Asahi machine (Mesa's software renderer in a virtual machine
would do for correctness).

- **First, an hour on the machine**: what Mesa offers there (`eglinfo`, `glxinfo`): a compatibility profile, the ARB
  programs, S3TC. The renderer's route depends on it.
- **libRSOpenGL's OpenGL path on EGL**: the context (a window's and a surfaceless one), the loader, the CGL names,
  Apple's legacy identity reported, OpenGL as the default ([Renderer](FINDINGS.md#renderer)). 500-800 lines. Its
  offscreen test and the 44 GL sequences then run on Mesa.
- **The shims built for Linux**: Cg (in its `glsl` mode), Bink (FFmpeg for Linux), Steam (its folders), the
  game's libraries.
- **The shell**: a window, the frame clock, input in the game's terms, the display's modes
  ([Platform](FINDINGS.md#platform)). About 1,500 lines.
- **A silent audio layer**: CoreAudio's device model with one output and no input, its callback on a timer, so
  that Wwise runs.

Done when: the title screen, the menus by keyboard and mouse, and a song's note highway, without sound.

### 3. Sound and the guitar

Needs the machine, and an interface or the cable.

- **The audio layer on PipeWire**: the device list with each channel, the output graph and its mixer, the input
  callbacks at 128 frames, a real-time thread, the default output followed
  ([Audio](FINDINGS.md#audio)). About 1,500 lines.
- **The FFT** behind the note detection and the tuner (`vDSP_fft_zop`), if step 1 has not needed it yet.
- **Latency measured**, against the Mac app's figures on the same hardware
  ([Mac install](../docs/mac/INSTALL.md#audio-latency)).

Done when: the tuner hears a string, calibration finishes, and a song is played with a guitar.

### 4. Install and play, from a clean machine

- **The game's files** with `gamefiles.py`: Steam's Linux folders, a reflink copy, and the route for the Mac depot
  that works on Asahi ([the routes](FINDINGS.md#installer-and-build)).
- **Install and start**: the program and its libraries under `~/.local/share/rs-rearmed`, a launcher script with
  the launch rules of `launch.sh`, a `.desktop` file, the logs, the settings files written by a command.
- **Offline**: the guard's Linux form for a process with mods.
- **Mods**: the `.so` loader. The mods players have need Linux builds from their own projects
  ([for the maintainer](#for-the-maintainer)).
- **This folder's README** becomes the port's page for a player: requirements, build, where things are, playing.

Done when: the documented commands take a fresh Fedora Asahi Remix from a clone to a song.

### 5. Parity and performance

Each on its own, as wanted: the picture's extras on OpenGL (render scale, MSAA, anisotropic filtering, the overlay
through MangoHud); measured runs (`tools/perf` reads macOS's process tables today); the optimised
build (ThinLTO and PGO with lld); the fences against the kernel's x86 memory order on Apple silicon; the tests'
rules in `tools/release/scope.py` and both `TESTING.md` pages; the editions' comparison table.

### 6. Learn & Play

Its window on the shell, its audio engine on the audio layer, Direct3D 9 with a window on the OpenGL path, and
about 50 lines of small replacements ([Learn & Play](FINDINGS.md#learn--play)). Written and built, not run here.

## Open questions

To find out:

1. **Mesa on the Apple GPU**: is there a compatibility profile, and with the ARB programs? Without a compatibility
   profile the stream would need its alpha test and legacy calls rewritten for a core context, a much larger step 2
   (or Zink under it).
2. **The Mac game's files on Asahi**: does `download_depot` work in the Steam that runs there, and where do its
   files land on the host? Else which other route.
3. **128 frames through PipeWire** with a USB interface: clean, or is a direct ALSA device needed?
4. **Which imports a session reaches** (step 0), and whether the two locale calls and the Ethernet address read
   are among them.
5. **The cable's name** as ALSA gives it, against the name the game looks for.
6. **x86-64**: can LLVM's Mach-O linker make the test programs with no macOS SDK; how fast is the translated game
   there against the original. (Answered on Bazzite, 2026-11-28: 0x100000000 is free in a Linux process, and
   memory may be made executable.)
15. **A throw through the game's code** on Linux: the unwind tables are registered, and 212 of the 3,487
    initializers have no frame description there. Whether any function that an exception passes has none (Apple's
    linker keeps a compact form of its own beside DWARF) is for the first throwing path to show: the game's file
    reading throws and catches inside Crypto++.
14. **Who can download the Mac depot.** The game on Steam is one app with a Windows and a Mac depot, and an
    account that has it for the Mac has both; whether every account that bought it for Windows does is for the
    console to say (`missing license for depot`). Learn & Play's license has no Mac download
    ([Windows install](../docs/windows/INSTALL.md)), so its owners are not served by this port.

To decide:

7. **Which host first.** Proposed: x86-64 ([above](#two-hosts-one-order)).
8. **On x86-64, patches or translation** for the fixes and mods ([X5](#x5-fixes-and-mods)).
9. **Where the Linux code lives.** Upstream's iPhone port has a folder of its own (`ios/app`, `ios/compat`) and
   compiled-out changes in the shared code. The same here: `linux/` for the build, the loader, the shell and the
   Darwin layer.
10. **SDL 3, or Wayland and EGL by hand**, for the shell. SDL 3 is proposed: it is the smaller shell and brings X11.
11. **PipeWire's own API or its JACK face** for the audio layer.
12. **Whether the Mac edition takes any of it**: the three native functions in the place of the engine's
    Objective-C, and the game's socket imports refused at the import as Learn & Play's are, would each work on the
    Mac as well. AGENTS.md asks that the editions stay alike; that is the maintainer's call.
13. **Whether upstream wants a Linux port**, and when to ask. Until then the branch keeps its changes to shared
    code small and compiled out on the Mac.

## Risks

1. **The OpenGL driver** on arm64 (question 1). The Cg shim's `glsl` mode already covers a driver without ARB
   programs; a driver without a compatibility profile is the expensive case. Not a question on a desktop GPU.
2. **Audio latency**: a desktop sound server where the Mac app talks to the device. Measured in step 3; ALSA is the
   way out.
3. **A stub that is reached** through a pointer the call graph did not show. It names itself in the log and stops,
   so it costs a run, not a search. Step 0 is there to find these on the Mac first.
4. **Differences no prototype shows**: glibc's locale, `printf` and `strtod` corners, another `rand` sequence, the
   order `qsort` leaves equal items in. The check is the same session on the Mac app and the Linux one.
5. **Tile-based GPU and draws that sample their own target** (the tuner's glow), undefined in OpenGL: the Metal
   core needed a copy for it.
6. **An external display on a MacBook Air** needs DisplayPort over USB-C, which Asahi lists as in progress.
7. **Memory**: the build and the game on an 8 GB machine. The game takes about 1.2 GB on the Mac; ThinLTO's link is
   the peak of the build.
8. **A third host to keep in step.** Every change to shared code would have Linux to consider beside the two
   editions. The port's answer is a small footprint: the Darwin layer and the shell in their own folder.
9. **Steam updating the Mac build**: as for the Mac edition; `import_census.py --check` says at once when the
   imports changed.
10. **x86-64: moved instructions.** A patch over a short instruction takes its neighbours along; one that is a
    branch target, or misplaces a `rip`-relative operand, is a silent fault. The plan is made and checked at build
    time from the translator's own knowledge of each function, and a site that cannot take a jump gets a
    breakpoint; translating on x86-64 avoids the question.
11. **x86-64: a second mechanism.** Running the original code is not how the other hosts work, so a change to
    hooks, mid points or edits would have two forms to keep alike. The `rc_*` interface is the line between them.

## Decisions

Proposed; none is final before the step that tests it.

- **The Mac game first.** Learn & Play is the smaller port, and upstream's iPhone port began with it for that
  reason; this one begins with the game it can run. The shell, the renderer's window, the audio layer and the
  install are written for both.
- **A Darwin layer of our own**, a table made from `darwin-imports.tsv`, and not an existing macOS layer for
  Linux: the game needs a few hundred functions, most of them libc's, linked into an ELF program.
- **GNU libstdc++ from the system**, as the Mac app takes Apple's copy of it.
- **No Objective-C runtime**: the engine's four functions become native.
- **OpenGL on Mesa.** No Vulkan backend is planned: Mesa's OpenGL is not going away as Apple's is, and a Vulkan
  backend is the Metal core written again.
- **CoreAudio's device model as the audio seam**, as upstream's iPhone port has it, so the audio policy stays one
  source for every host.
- **The game's own contracts stay the Mac's**: macOS key codes at the input sink, Darwin's layouts at the imports,
  the Mac target's names and tables, the folder layout and the process name. A Linux build of the Mac game is the
  Mac target on another host.
- **Offline by construction**: the game's network imports refuse; the guard is for mods.
- **Built on the player's machine from their own copy**, with Fedora's clang, lld and Python; nothing of the game
  is shipped, and nothing of Apple's headers is copied.
- **The Mac edition unchanged**: what Linux needs in shared code is compiled out on the Mac.
- **On x86-64 the game's code runs as it is**, behind the same `rc_*` interface, so overrides, fixes and mods stay
  one source; translation there stays possible and is checked to compile.
- **x86-64 before arm64**, as the proving ground for what both share.
- **No Vulkan backend on either CPU**; Zink is the way out where a driver lacks the compatibility profile.
- **The Mac game on Linux too, not the Windows one.** The tables, hook points and fixes are this image's; its code
  is 64-bit and runs on an x86-64 Linux as it is; what it asks of the system is mostly the C library. The Windows
  games are 32-bit programs that ask for Windows, which is Wine's work, and Proton does it today.
- **x86-64 ships as an AppImage** made from a folder that runs anywhere, with none of the game in it.
- **The x86-64 build is a `Makefile`** (`linux/Makefile`), not part of the Mac's CMake project: Bazzite has `make`
  and `gcc` on the host and no CMake, and the loader and the layer need the C library's headers alone. What
  needs more (EGL, SDL, PipeWire) builds in a distrobox.
- **An import with no answer is an address with no access**, not a function that logs: a read of an object is
  caught as a call is, and nothing is generated per import.

## For the maintainer

- **Mods.** rearmed-mods, RS Companion and rearmed-songbridge are Mach-O libraries with Mac apps beside them. On a
  Linux build none loads; each would need a Linux build from its own project. Nothing changes for a Mac player. What
  those builds would rest on is unchanged: the headers, the names, the `rc_ctx` layout, the folders and the process
  name ([mods](FINDINGS.md#built-in-fixes-and-mods)).
- **Shared code.** Changed on this branch so far: a test's registration (`CMakeLists.txt`,
  `tools/release/scope.py`, `docs/mac/TESTING.md`), a line in each plan, and one branch in
  `shims/steam/src/shim.cpp`, inside `#if defined(__linux__)`, for the folder Steam for Linux keeps its files in.
  The Mac's library compiles the same code as before.

## Log

- **2026-11-26.** Branch `port/linux-arm64` from 1.20.0. Read the docs and the sources; wrote `import_census.py`
  with its test and ran it on the pinned image (597 called imports, 84 data, as the thunk report has them);
  classed every import (`darwin-imports.tsv`) from the names, the sources and the call sites in a 1.20.0 build's
  generated C; checked the generated C for arm64 Linux (243 parts, no diagnostics); wrote the findings and this
  plan. Next: step 0's counting switch.
- **2026-11-26, x86-64.** Branch `port/linux-x86_64` on top of the arm64 one. Checked the generated C for x86-64
  Linux (243 parts, no diagnostics, through the runtime's per-lane helpers); read the image's load commands and
  counted its instructions (no system call, segment register or AVX; 4 KB segments; DWARF unwind tables); read the
  harness's loader; counted the edits and mid points by form and the overrides by purpose. Wrote `X86-64.md` and
  the x86-64 steps here. Next, if x86-64 goes first: X1's loader and the layer's first cut, with a script to run it.
- **2026-11-28.** A Linux build was asked for (issue 18: Bazzite, an AppImage), so x86-64 goes first. Wrote
  `first_run.py` and its test: the image mapped, bound and its initializers run in a Python process, each import
  counted; and `--machine`, the report of what the plan asks of a machine. Run on a Mac in an Intel process:
  3,487 of 3,487 initializers, 16 imports called (11 `same`, 5 with stand-ins); with Linux's rule on the Mac's
  libraries the same; an import with no answer stops it by name. The rune table the script makes for
  `_DefaultRuneLocale` equals macOS's own for all 256 characters, and macOS gives 15 and 47 as the priority range
  of every policy. Next: X0 on a Bazzite machine.
- **2026-11-28, X0 on Bazzite.** A PC with Bazzite 45 (KDE, Wayland; AMD Renoir graphics). With the decrypted image
  copied from the Mac: `import_census.py --check` agrees, `first_run.py` runs 3,487 of 3,487 initializers with the
  same 16 imports and counts as on the Mac, and both tests pass there (the three that need an x86-64 process
  included). The machine's report is in [X0](#x0-the-first-run-with-nothing-built): the address free, a
  compatibility context with all six extensions, PipeWire at 48 kHz. The report's OpenGL question had a mistake
  this run found: asked for no surface type, EGL looks for a window config, which a surfaceless display has not;
  it now asks for none. Not yet tried: `download_depot` in Steam on Linux. Next: X1's loader.
- **2026-11-28, X1.** The loader in C (`linux/src`), the Darwin layer's first cut (`linux/darwin`), a `Makefile`
  and `test_loader.py`, built and run on the Bazzite PC: 3,487 of 3,487 initializers, the 16 imports and counts
  of the first run, the image's unwind tables registered. Two things the run corrected: the decrypted image's
  `__LINKEDIT` ends where its bytes do, not on a page, so the mapping is rounded up; and Darwin's `_RuneLocale`
  is 3,208 bytes (checked against the Mac's own header). The operators new and delete are bound by the ordinary
  table though the image calls them, so a call's pointer is told by its section. Next: X2, which needs the
  game's files on the machine.
- **2026-11-28, the game's start.** The Mac depot downloaded by Steam on the Bazzite PC (`download_depot`; the
  folder is under `steamrt64`, which `first_run.py --machine` now finds; its paths no longer carry the account's
  name where the home folder is a link). Decrypted there: the same image as the Mac's. The layer grew from 9
  functions to 84 in one batch, led by a survey run, and `GameBootstrap` then ran whole on the first try with
  them, once paths were found in any letter case. `GameFrame` stops at the Steam library. Next: the Steam shim.
- **2026-11-28, the Steam library.** `shims/steam` built for Linux from its own sources (`linux/compat` for four
  macOS functions; one Linux branch in `shim.cpp`). Its tests pass on Bazzite. The game's first frames then
  initialize Steam and go on to the display's modes, the audio devices and Cg.
- **2026-11-28, CoreFoundation and the display.** `dw_cf.c` and `dw_display.c`, with their tests. What the first
  frames still ask for and do not get: CoreAudio's devices (two calls) and Cg.
- **2026-11-28, a picture.** A distrobox on the Bazzite PC for the headers. The renderer's libraries and the Cg
  library built from their sources over `libRSSystemGL.so`, their tests passing on the PC's GPU; the shell on
  SDL 3 over the Mac shell's C half and the seam; the audio layer silent. The game then drew its intro and its
  title screen. Two things the runs found: the game asks `pthread_mach_thread_np` for a thread it has not made
  (no port, not a fault), and the renderer needs `__invert_f4`.
- **2026-11-28, the first menu.** Keys from a script; the `online` override module from its own sources with a
  jump at each of its nine functions; the runtime's headers used as they are (the shadow copies in
  `linux/direct` of the step before are gone). Enter on the title screen opens the profiles' menu.
- **2026-11-28, sound.** A profile made by script, and the menus after it. Threads get a stack of 1 MB at the
  least. The graph's sound to PipeWire, which grants the 256 frames asked for.
- **2026-11-28, the cable, videos, the FFT.** With a Real Tone Cable plugged into the Bazzite PC: PipeWire's
  capture devices as the game's inputs, the Bink library over FFmpeg built for Linux, and vDSP's FFT. The
  scripted new-player flow reaches the calibration screen with its video playing and its meter live; 9,900
  frames without a fault. The loader then answers 583 imports, and the 98 left are the stubs.
- **2026-11-28, played.** A player calibrated with the cable on the Bazzite PC: notes are detected. The cable
  was too sensitive and calibration ended with the game's notice of a sudden change: the input's level was not
  applied. It is now (the cable's hardware control; a gain for other inputs). For a first song, the account's
  purchased DLC was copied into the game's `dlc` folder with the list of the DLC it owns in
  `.rearmed/steam_shim.ini`, as the Mac install records it; the Steam library then reports them owned. Getting
  DLC through Steam on Linux is the install's work (X4).
- **2026-11-28, a song, and the first fixes.** The player calibrated again (no notice this time) and played a
  song through headphones: no crackle, the notes detected, the latency at least as good as on Windows by his
  ear. Then hook points in the patch engine, and the fixes module compiled as it is: the intro is skipped.
- **2026-11-28, mid points.** The 13 mid points of the built-in fixes, through the runtime's `rc_mid.c`; the
  table of where a jump can be written, from the recompiler's disassembler. One fault on the way: a
  continuation inside the bytes a jump covers, now sent to the moved copy. The whole fixes module runs.
- **2026-11-28, the setup.** `install/rs_install.py`: the Mac's install tools with Linux's answers. On the
  Bazzite PC a full setup into a test folder: the game found in Steam's download, 187 DLC fetched through the
  running Steam, the copy cloned, the image decrypted, and the game started from that folder to its profile
  screen. (The real folder waits: a game was running, and the setup refuses then.)
- **2026-11-28, a crash in the display settings.** The player opened Visual Settings and the game stopped: the
  fixes' input handlers for those screens hand the game's own handler its eight arguments, and a call into the
  game passed six (`src/direct.c`). Arguments past the sixth now go on the stack, as the convention has them, and
  the runtime's general form (`rc_call_guest_regs`) is there too; `test/call_test.c` checks up to twelve, both
  kinds, the results and the stack's alignment. Checked in the game with scripted keys: both screens open, and
  their rows move and change.
- **2026-11-28, the AppImage.** The packaged build, and the player's first run of it on the Bazzite PC: the
  setup into the real folder (the game found, 312 files cloned, the image decrypted; the DLC had come in the
  test before), then the game.
- **2026-11-28, the title logo.** `tools/loadscreen/logo.py` and `tag.png`; made in the player's copy on the
  Bazzite PC by a refresh, and the title screen shows it.
- **2026-11-28, the installer's window.** `install/rs_gui.py` and `rs_settings.py`; on the Bazzite PC, under a
  display of its own, the window ran an install into a test folder to "RS Re-ARMed is installed", and the
  settings page shows the Mac's switches. `RS_AUDIO_FRAMES` in the audio layer.
- **2026-11-28, the window's switches.** The shell follows the engine's window mode, as the Mac's does (the
  game's Windowed Mode setting; F11 and Alt+Enter turn it over; `RS_FULLSCREEN` forces it), keeps a window to
  16:9 with `RS_WINDOW_ANY_SHAPE=0`, starts frames on its own clock with `RS_FPS`, and hides a resting pointer.
  A frame a refresh is now the display's own rate where the swap does not hold it, not 60: a 144 Hz screen was
  held to 60. `RS_GL_MTENGINE=1` is Mesa's multithreaded OpenGL (`mesa_glthread`), in the place of Apple's.
  Checked without a display for the pacing (600 frames: 20 s at 30, 10 s at the default) and on a virtual X
  display with a window manager for the mode (1600 x 900 full screen, 1280 x 720 as a window).
- **2026-11-28, Adv Display Settings on Linux.** The game's own four rows and three of ours: Frame Rate, Any
  Window Shape, Multithreaded GL Engine. The renderer's choice and Metal's eight rows are not in the Linux list
  (`cache_overlay.py` LINUX_MENU_OPTIONS for the menu's definition; `menu_options_common.c` for the fixes'
  half). A start with a newer program makes the copy's menu overlay again (its tool's hash is kept beside it),
  where the Mac's installer would have. Candidates for rows of their own later, each needing work in the
  renderer's OpenGL backend: anti-aliasing, texture filtering, a render scale.
- **2026-11-28, the guitar input.** The Mac app's policy file (`shims/coreaudio/rs_audio_input.c`) is compiled
  as it is, with `compat`'s CoreAudio names, and the audio layer shows the game its entries: the cable, each
  channel of an interface as an input of one channel ("HX Stomp channel 5"), the rest, ten in all; a device
  whose channel `guitar-input.ini` or `RS_AUDIO_INPUT_CHANNEL` names is one entry of that channel; a gain from
  the same settings; samples kept within -1 and 1. The level of an input that is not the cable is the Mac's rule
  now: a gain of ours that counts once calibration has run with it (`input-level-calibrated` in the home
  folder), and `RS_AUDIO_INPUT_LEVEL=0` leaves it out. An interface is a USB device that plays as well as
  records (PipeWire's sinks tell); a USB microphone is not one. Checked in the layer's test with a made-up
  interface of eight channels; **not yet with a real interface**. `RS_AUDIO_INPUT_TRIM` (the game's own queue
  of input buffers, a hook in the game's code) is not in.
- **2026-11-28, the audio module's game half.** `overrides/src/audio_in.c` and `audio_ring.c` compiled as they
  are (two more functions replaced, a hook on the sound engine's start): the input queue is read without its
  stale buffers (`RS_AUDIO_INPUT_TRIM`), and at the game's audio setting 1 the engine keeps two refills while the
  output's ring has one. The output's buffer is capped at half that ring, as the Mac's module caps it: 128
  frames at setting 1, 256 at the default 4 (seen in a run without sound). The module's third function, the
  engine thread's real-time policy in the device's workgroup, is macOS's; a real-time engine thread here would
  go through PipeWire's own way to one.
- **2026-11-28, mods load.** The Mac app's loader (`runtime/rc_mods.c`, with `.so` for `.dylib` here) runs
  between the hook points and the mid points: a mod's constructor asks for its hooks with the image mapped, and
  the mid points are written for the ones that have a hook by then. The program is linked to offer its functions
  to what it loads. The files a mod serves (`runtime/rc_file.c`, with glibc's `fopencookie` for `funopen` and
  Linux's structure fields) are behind the layer's nine file functions, the same nine the Mac's thunks send
  there; `RS_FILE_LOG=1` logs what is served. A test mod (`test/mods/test_mod.c`) is loaded from the mods
  folder, its hook runs around the game's frame, its provider is asked for the game's files and adds a name to
  the dlc folder's listing. Not in: edit groups (`rc_edit_enable`: 620 edits, most of them Seven-String's), which
  on this host are bytes to change in the loaded code, 150 of them longer than what they replace.
