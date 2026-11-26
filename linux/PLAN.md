# Linux: status and plan

What the port needs and why is in [FINDINGS.md](FINDINGS.md) for arm64 and [X86-64.md](X86-64.md) for x86-64; this
page is what to do about it, in what order, and what has been done. The editions' own plans: [Mac](../docs/mac/PLAN.md), [Learn & Play](../docs/windows/PLAN.md).

## Status

- **Investigated, not built** (2026-11-26, from 1.20.0). Nothing of RS Re-ARMed compiles for or runs on Linux yet.
- **In the tree**: the findings, this plan, the table of the Mac game's 681 imports with what a Linux build does
  about each (`darwin-imports.tsv`), and the tool that reads them from the image and checks the table
  (`import_census.py`, ctest `linux.imports`).
- **Checked so far**: the translated game's C passes a compile check for arm64 Linux unchanged; the import table
  matches the image and a build's thunk report; which imports only the replaced parts of the game call
  ([how](FINDINGS.md#how-this-was-found)).
- **The game**: the Mac game first. Learn & Play is step 6, and would be written without a run: it is not owned
  where this work is done.
- **The machines in mind**: for arm64 an Apple silicon Mac with Fedora Asahi Remix, not at hand yet (steps 0 and 1
  do not need it); for x86-64 a desktop with Arch Linux and an NVIDIA card. Neither is driven from where this is
  written: each step comes with a script that builds, runs its check and says what it found.
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

### X2. A picture

As [arm64's step 2](#2-a-picture) on a desktop GPU: the OpenGL path on EGL, the shims built as x86-64 libraries,
the shell, a silent audio layer. The seam's calls into the game are plain calls. The three functions in the place
of the engine's Objective-C and the online stub's nine go in as jumps at their functions' entries, the first of
[the patches](X86-64.md#running-the-original-code).

Done when: the title screen, the menus, and a song's note highway without sound.

### X3. Sound and the guitar

[arm64's step 3](#3-sound-and-the-guitar) as it is: the audio layer on PipeWire, the FFT, latency measured, here
against the same machine's Proton route as well.

### X4. Install and play

[arm64's step 4](#4-install-and-play-from-a-clean-machine), with Steam at hand: the depot through its console or
`steamcmd`. No build of the game: the install decrypts the image and builds the small native part.

### X5. Fixes and mods

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
6. **x86-64**: is 0x100000000 free in a Linux process as expected; can LLVM's Mach-O linker make the test programs
   with no macOS SDK; how fast is the translated game there against the original.

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

## For the maintainer

- **Mods.** rearmed-mods, RS Companion and rearmed-songbridge are Mach-O libraries with Mac apps beside them. On a
  Linux build none loads; each would need a Linux build from its own project. Nothing changes for a Mac player. What
  those builds would rest on is unchanged: the headers, the names, the `rc_ctx` layout, the folders and the process
  name ([mods](FINDINGS.md#built-in-fixes-and-mods)).
- **Shared code.** Nothing of either edition is changed on this branch so far beyond a test's registration
  (`CMakeLists.txt`, `tools/release/scope.py`, `docs/mac/TESTING.md`) and a line in each plan.

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
