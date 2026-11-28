# What a Linux port needs

What each part of RS Re-ARMed takes from macOS today, and what arm64 Linux has in its place. Read from the tree at
1.20.0 on 2026-11-26. The headings follow [how the Mac port works](../docs/mac/DESIGN.md), so the two can be read side
by side; the steps that come out of it are in [PLAN.md](PLAN.md). It is written for arm64, where the game's code is
translated; what differs on x86-64, where it can run as it is, is in [X86-64.md](X86-64.md).

Nothing in this file has run on Linux. What was measured, and how, is [at the end](#how-this-was-found); a statement
that rests on general knowledge of Linux and not on this tree says "to check".

## Overview

```
game image (the same file)                tools/targets/mac-x64 (the same tables)
   |  gen_image.py: the same C
   v
libgame_rc.a + overrides + runtime (rc_*)    a dozen Darwin calls, the va_list, two assembly files
   + import thunks                           prototypes from a header of ours, not from the macOS SDK
   + the Darwin layer            NEW         the game's 681 imports answered (darwin-imports.tsv)
   + platform shell              NEW         a Linux twin of shims/platform's Objective-C half
   + libRSOpenGL                             its OpenGL path, on EGL and Mesa; Cg, Bink and Steam shims as .so
   + audio devices               NEW         CoreAudio's device calls on PipeWire
   v  link (clang, lld)
Rocksmith2014 (ELF)  +  game files  +  a launcher script and a .desktop file, under ~/.local/share
```

The game, the recompiler and its tables stay as they are. The work is in the ring around the translated game: on
macOS that ring is mostly macOS itself.

| Piece | Today | On Linux | Size of the new work |
|---|---|---|---|
| Translated game | C from `gen_image.py` | the same C | none |
| Recompiler, tables, image decryption | Python | the same | none; `xcrun objdump` has a stand-in in `import_census.py` |
| Runtime (`rc_*`, 6,200 lines) | C, a little Darwin | small changes | tens of lines in 8 files |
| The game's imports | macOS's own arm64 libraries | the Darwin layer | 259 free, 162 shims, about 160 to write, 98 stubs |
| Shell | `rsp_shell.m`, `rsp_present_metal.m` (AppKit, 1,400 lines) | a new shell | about 1,500 lines |
| Renderer | `libRSOpenGL`: Metal (8,900 lines), or Apple's OpenGL | its OpenGL path on EGL | 500-800 lines for a picture |
| Audio | the game's own CoreAudio calls, steered by 1,800 lines of overrides and policy | the same calls on PipeWire | about 1,500 lines |
| Bink, Cg, Steam, the game's libraries | portable C and C++ | a build | a few lines each |
| Network guard | a library inserted by dyld | nothing linked, or `LD_PRELOAD` | small |
| Build | shell, CMake, Apple's tools | Linux counterparts | every script touched |
| Install | `gamefiles.py`, `install.sh`, `launch.sh`, a Swift app | the same Python, new paths, a command line | moderate |
| Mods | `.dylib` from `mods/` | `.so` | the loader: small; each mod: its own project's build |

## The machine

Fedora Asahi Remix on an Apple silicon Mac is the first target (to check on the machine, each of these):

- **arm64, 16 KB pages**, as on macOS. Other arm64 Linux has 4 KB pages; nothing here depends on the size, since the
  runtime maps the game image as one writable piece (`rc_image.c`).
- **Mesa** with OpenGL 4.6, OpenGL ES 3.2 and Vulkan 1.4 drivers for the Apple GPU. Whether the OpenGL driver gives a
  compatibility context with the ARB assembly programs is the first thing to look at ([Renderer](#renderer)).
- **PipeWire** for sound, with its JACK and ALSA faces; **Wayland**.
- **Steam** only as an x86 program: Fedora's package runs it with FEX inside a small virtual machine (muvm), because
  x86 programs expect 4 KB pages. That route also plays the Windows game through Proton, and is why a native port is
  worth having here: it takes memory an 8 GB machine does not have, and puts a virtual machine between the guitar
  and the game.
- **External displays on a MacBook Air.** Asahi's feature page lists DisplayPort over USB-C as work in progress for
  the M1 (read 2026-11-26), and an Air has no HDMI port. Until that lands the game shows on the built-in screen.

## Getting the game image

Unchanged. `tools/steamless/decrypt.py` is standard-library Python and reads only the game's file. Where the game's
files come from on Linux is under [Installer and build](#installer-and-build).

## Translation

- **The generated C is host-neutral.** Every one of the 243 parts of a 1.20.0 build passes
  `clang --target=aarch64-unknown-linux-gnu -fsyntax-only` with the build's own switches, and one was compiled to an
  ELF object. The parts include `rc_gen.h` alone, which takes `stdint.h`, `stddef.h`, `string.h` and `arm_neon.h`
  from the system.
- **The fences** (`dmb ishld`, `dmb ishst`), the NEON helpers, `__int128` and the vector types are the same on
  Linux. clang stays the compiler: the optimised build is ThinLTO and instrumented PGO.
- **`char`.** Plain `char` is signed on macOS and unsigned on arm64 Linux. The generated C has none (fixed-width
  types throughout), but the runtime, the overrides and the shims were written and tested with a signed one, and
  `thunks/sigs.py` reads a prototype's `char` as signed. Everything is to be compiled with `-fsigned-char`.
- **Flags.** `-arch arm64` goes; `libtool -static` becomes `llvm-ar`; `sysctl -n hw.ncpu` becomes `nproc`
  ([Installer and build](#installer-and-build)).
- **Memory order, later.** Asahi's kernel can switch a process to the CPU's own x86 memory order (what FEX uses; to
  check). With it the fences could go on Apple silicon, which macOS does not allow an ordinary process. Not needed
  for a port; a possible gain once it runs ([PERFORMANCE.md](../docs/mac/PERFORMANCE.md#where-the-cpu-goes)).

## Per-game tables

Unchanged, and the point of them: `tools/targets` describes the game, not the host. A Linux build of the Mac game is
target `mac-x64` (`rc_target()` says so to a mod); a Linux build of Learn & Play would be `win-x86`. No table gets a
third column.

## Runtime

`tools/ws7/recomp/runtime` is plain C but for these:

| Where | What is Darwin's | On Linux |
|---|---|---|
| `rc_image.c` | `<mach-o/loader.h>` for the game image's own format | the same constants from a header of ours (the format is the game's, on any host) |
| `rc_image.c` `handle_for`, `resolve` | binds an import with `dlopen("/System/…")` and `dlsym` | a lookup in the Darwin layer's table ([below](#the-mac-games-imports)) |
| `rc_image.c` `fixup_section` | registers the image's selectors with `sel_registerName`, and fails without the runtime | not done: nothing left sends a message ([Objective-C](#objective-c-libobjc-foundation-appkit-62)) |
| `rc_va.c`, the `printf` thunks | a `va_list` is a pointer to 8-byte slots | AAPCS64's `va_list` is a struct: built to read every argument from such slots (`__gr_offs` and `__vr_offs` 0). A `long double` there is 16 bytes, 16-aligned and IEEE quad, where the x87 value is made a `double` today |
| `rc_tramp_arm64.S`, `rc_native_arm64.S` | `_name` symbols, `@PAGE`/`@PAGEOFF` | no underscore, `:lo12:` (95 lines in all) |
| `rc_thread.c` | `pthread_threadid_np`, `pthread_get_stackaddr_np`, `pthread_get_stacksize_np` | `gettid`, `pthread_getattr_np` |
| `rc_thread.c` `live_records` | walks the frame-pointer chain, "mandatory on Darwin" | our code built with `-fno-omit-frame-pointer`; a chain it cannot follow is already handled (records kept) |
| `rc_setjmp.c` | a host `jmp_buf` of at most 256 bytes (Darwin's is 192) | glibc's is 312 on arm64 (to check): `RC_JMP_HOST_WORDS` grows, or the build stops at the file's own assert |
| `rc_file.c` | `funopen`; `st_mtimespec` | `fopencookie`; `st_mtim`; and Darwin's layouts where it meets the game ([below](#the-mac-games-imports)) |
| `rc_objc.c` | the Objective-C runtime, for struct-returning sends | left out of the build |
| `rc_mods.c` | `*.dylib` | `*.so` |
| `rc_nofile.h` | `sysctl` for the open-file limit | `/proc/sys/fs/nr_open`, or the hard limit |
| `rc32_image.c`, `rc32_teb.c` (Learn & Play) | `mach_absolute_time`, `pthread_mach_thread_np` | `clock_gettime`, `gettid` |
| `app/rc_main.c` | the bundle's path (CoreFoundation), `_NSGetExecutablePath`, the crash handler's `__ss.__pc`, `DYLD_INSERT_LIBRARIES`, `fix_objc` | `/proc/self/exe`, `uc_mcontext.pc`, [the guard](#online-services), nothing |
| `app/build.sh` | a 256 MB main stack from the linker (`-stack_size`): the initializers and the bootstrap run on the main thread | no such switch for ELF: `main` starts the rest on a thread with that stack, or raises the limit and starts itself again |

**The thunks.** `thunks/gen_thunks.py` makes one function per import that reads the x86-64 arguments from the guest
and calls the native function by its C prototype. Two things tie it to a Mac:

- The prototypes come from the macOS SDK: `sigs.py` has clang dump the AST of `hdr_system.m`, which includes
  libc, Mach, the Objective-C runtime and eleven frameworks. On Linux the functions called are ours, so the
  prototypes come from the Darwin layer's own header: each function declared once with the guest's types
  (fixed-width integers, Darwin's struct layouts), and `sigs.py` reads that header on any host. No SDK is needed and
  nothing of Apple's headers is copied.
- `imports.py` asks `xcrun objdump --macho` for the bind tables. `linux/import_census.py` reads them from the file
  itself and gives the same 597 called imports as a build's thunk report.

The thunks themselves carry over: the universal bridge (`rc_native_call`) passes only registers; a thunk for
`open` or `fcntl` calls through a variadic prototype, which the compiler handles on either system; the generator's
`__attribute__((weak_import))` becomes `weak`.

**Darwin's arm64 ABI against Linux's**, where native code meets the guest:

| | macOS arm64 | Linux arm64 | Reaches |
|---|---|---|---|
| Variadic arguments | all on the stack, 8-byte slots | registers first, a `va_list` struct | `rc_va.c`, the `printf` family's thunks, a trampoline for a variadic callback (`RC_TR_VA`) |
| `char` | signed | unsigned | everything, hence `-fsigned-char` |
| `long double` | 64 bits | 128 bits | `%Lf` in `rc_va.c` |
| `wchar_t` | signed 32 bits | unsigned 32 bits | the `wchar_t` string imports of libstdc++ (the same width) |
| Stack arguments under 8 bytes | packed | 8-byte slots | nothing: calls go through C prototypes |
| Frame pointer | always | by flag | `rc_thread.c` |

## The Mac game's imports

This is the largest finding. The Mac app maps the game's image and binds its imports to **macOS's own arm64
libraries** through the thunks ([Mac design](../docs/mac/DESIGN.md#overview)). The image imports 681 symbols from
19 libraries: 597 it calls and 84 it holds as data (variables, classes, vtables, function pointers).
[darwin-imports.tsv](darwin-imports.tsv) has a row for each with what a Linux build does about it;
`import_census.py` counts it:

| Library | Call | Data | same | adapt | emulate | runtime | shim | layer | refuse | stub |
|---|---|---|---|---|---|---|---|---|---|---|
| libSystem | 254 | 9 | 166 | 30 | 36 | 11 | | | 15 | 5 |
| OpenGL | 107 | | | | | | 105 | | | 2 |
| libstdc++ | 64 | 29 | 93 | | | | | | | |
| libobjc | 31 | 11 | | | | | | | | 42 |
| CoreFoundation | 21 | 16 | | | 29 | | | | | 8 |
| Cg | 29 | | | | | | 29 | | | |
| Bink | 16 | | | | | | 16 | | | |
| AppKit, Foundation | 2 | 18 | | | | | | | | 20 |
| ApplicationServices | 13 | | | | | | | 6 | | 7 |
| Steam | 12 | | | | | | 12 | | | |
| AudioToolbox, CoreAudio, AudioUnit | 25 | | | | | | | 25 | | |
| IOKit | 8 | 1 | | | 5 | | | | | 4 |
| CoreVideo | 7 | | | | | | | | | 7 |
| Accelerate | 5 | | | | 5 | | | | | |
| Carbon, CoreServices | 3 | | | | | | | | | 3 |
| **Total** | **597** | **84** | **259** | **30** | **75** | **11** | **162** | **31** | **15** | **98** |

The classes: **same**, the host's own function of that name; **adapt**, a function of ours with Darwin's prototype
over the host's; **emulate**, a Darwin-only facility written anew; **runtime**, the recompiler runtime's own;
**shim**, one of the project's replacement libraries; **layer**, part of a stand-in for a macOS service; **refuse**,
the network; **stub**, nothing the app runs calls it.

So 421 of the 681 need no new code (a build of the shims aside), 98 need a stub that says its name, and 162 need
writing, most of them a few lines.

**What a stub rests on.** The Mac app replaces the game's own `main` and its Cocoa shell classes, and never starts
the ARC support code linked into the game. The call sites in a build's generated C show which imports only those
three call: 58 of the 597, among them all 31 of `libobjc`, the display link, the sleep assertion, the cursor, the
alerts, `Gestalt`, `NSLog` and the two CGL locks. Fourteen more have no call site at all. None of the game's 3,487 static
initializers is in the ARC code or calls one of these imports, and nothing of the game proper calls into that code.
A call through a pointer would not show in such a count, so a run of the Mac app with its imports counted remains
the proof ([step 0 of the plan](PLAN.md#0-count-what-a-session-reaches-on-the-mac)).

### The shape of the layer

One table, name to address, made from `darwin-imports.tsv` at build time and read by `rc_image.c`'s binder in the
place of `dlsym`. A `same` row is the host's symbol; every other row is a function or object of ours under a prefix
(`dw_stat`, `dw_pthread_mutex_lock`), so nothing of ours takes glibc's names. Why not something existing: Darling
runs macOS programs on Linux, but as whole processes under its own loader and kernel server; its libraries are
Mach-O and are not made to be linked into an ELF program. The game needs a few hundred functions, most of them
libc's.

### libSystem (263)

- **Same, 166.** The string, memory, math, time and stdio functions with one meaning on both systems, called as
  they are. A `FILE *` is opaque to the game (it imports `fputc`, `feof` and `fileno` as functions and none of the
  inline `getc` internals), `struct tm` has the same layout, `CLOCKS_PER_SEC` is the same. `rand` is another
  generator with the same range.
- **Adapt, 30.** Where a number or a layout differs:
  - `open`, `fcntl`, `sem_open`: the `O_` flags (`O_CREAT` is 0x200 on Darwin, 0x40 here).
  - `stat`, `fstat`, `readdir`: Darwin's `struct stat` (144 bytes) and `struct dirent` (a 1,024-byte name, `d_namlen`).
    The game's file imports already go through the runtime's `rc_file_*`, so the conversion has one place.
  - `select`: `timeval`'s microseconds are 4 bytes on Darwin. `poll`: the band bits. `setlocale`: the categories.
  - `sigaction`, `signal`: the struct, the flags, the signal numbers.
  - **pthreads.** The types fit: glibc's mutex, condition and attribute objects are no larger than Darwin's (48,
    48 and 64 bytes in 64, 48 and 64). What differs: a mutex or condition with Darwin's **static initializer**
    carries a signature glibc would read as a locked mutex, so the lock and wait functions make such an object on
    first use; the constants (`PTHREAD_MUTEX_RECURSIVE` is 2 on Darwin, 1 here; the detach state, scheduling
    policies and scope); `pthread_key_t` is 8 bytes on Darwin and 4 here; `pthread_setname_np` takes one argument;
    `pthread_cond_timedwait` returns `ETIMEDOUT`, 60 there and 110 here.
- **`errno`.** The game reads it through `__error()`, with Darwin's numbers (`EAGAIN` is 35, 11 here; the first 34
  agree but for `EDEADLK`). One per-thread value of ours: at each look, a host `errno` that is set is translated
  into it and cleared.
- **Emulate, 36.** `OSAtomic*` (C atomics), `mach_absolute_time` and its timebase, the four Mach semaphore calls
  (POSIX semaphores behind port numbers), `sysctl` and `sysctlbyname` for the fourteen names in the image (`hw.memsize`,
  `hw.logicalcpu`, `hw.cachelinesize`, `hw.optional.sse2`, `kern.osrelease`, ...), each answered as an Apple silicon Mac
  answers it, since that is what the Mac app is tested with (on x86-64, where the game's own code runs, as an
  Intel Mac answers: [X86-64.md](X86-64.md)); the rune table the C library's `isalpha` family reads inline
  (`_DefaultRuneLocale`, `__maskrune`), `__stderrp` and its two siblings, `__stack_chk_guard`, `memset_pattern16`,
  `__sincosf_stret`, `__invert_f4`, `__exp10`, and `tcgetattr`, which answers that there is no terminal.
- **Refuse, 15.** `socket`, `connect`, `gethostbyname` and the rest answer as a machine with no network. The Mac
  app lets the game make these calls and refuses them in the guard; here they are our functions, so nothing that
  can reach a network is linked for the game, as in Learn & Play. If a run shows the game needs a local socket,
  these become adapters (`sockaddr` has a length byte on Darwin) under the guard's rule.
- **Stub, 5.** `_Block_copy`, `hash_create`, `hash_search` and `_dyld_register_func_for_add_image`, which only the
  ARC support code calls, and `dyld_stub_binder`.

### libstdc++ (93): the system's own

The game links GNU libstdc++ 4.2.1, which macOS still carries, and the Mac app binds to it
(`shims/libstdcxx`). Linux's `libstdc++.so.6` is the same library a few versions on and keeps that ABI
(`GLIBCXX_3.4`): the copy-on-write `std::string` members, the stream classes' vtables and VTTs, the tree and list
helpers, `operator new`, the `__cxxabiv1` type_info vtables that the game's 361 type_info objects point at, and
`__cxa_pure_virtual` in 1,834 vtable slots. All 93 are `same`.

- **The empty string.** Apple built its libstdc++ with fully dynamic strings; upstream does not, and hands out one
  shared empty representation that must never be freed. The game's inlined code is the upstream kind: of its 283
  calls of `_Rep::_M_destroy`, 217 show the test against `_S_empty_rep_storage` within 14 instructions before the
  count is dropped, and the others are the cold halves of such a test. So the game's strings and Linux's library
  agree.
- **Exceptions** behave as on the Mac: `__cxa_throw` is a thunk to the real one, the translator makes no landing
  pads (the image's exception tables are not read), and a throw ends in `terminate`. The 83 functions that throw
  are all in the online library, behind the online stub; elsewhere only the length and allocation error paths.
- **To check on a Linux machine first**: that all 93 names are there under their default version (an old name
  kept only as a compatibility symbol needs its version spelled out), and the two places where Apple's 4.2.1 and
  today's library could differ in layout, `mbstate_t` inside a stream position and the signedness of `wchar_t`.
- `shims/libstdcxx`'s tests (`abi_test.cpp` with `gnu_abi.h`) are the check to run first against the Linux
  library; the shim itself, a link stub and a forwarder for macOS, is not needed.

### Objective-C (libobjc, Foundation, AppKit: 62)

No Objective-C runtime is needed. In the translated game 63 functions touch a selector or a class reference:

| Functions | | |
|---|---|---|
| The game's `main` | 1 | replaced by the app's own |
| Its Cocoa shell classes (`AppDelegate`, `MainController`, `OpenGLView`) | 36 | replaced by `shims/platform`; the classes are never registered |
| The ARC support code linked into it | 22 | started by a class's `+load`, which never runs; holds 29 of the 31 `libobjc` calls |
| The clipboard's text | 1 | the engine calls it: `NSPasteboard` |
| `MessageBox` | 2 | the engine's alert, which the Mac app redirects to a native class (`rc_main.c` `fix_objc`) |
| A folder's listing, for Gamebryo's shader libraries | 1 | `NSFileManager` |

The last four are all of the engine's Objective-C. Each becomes a native function in the override way (a name in
`tools/targets`, the function left out of the translation): the clipboard from the shell, the alert as the shell's
message box, the listing with `readdir`. Then every one of the 62 imports is a stub, `rc_image.c` no longer
registers the image's 131 selectors, and `fix_objc` has nothing to do. The engine does hold the `NSOpenGLContext`
the shell hands it (`h_gl_context`), but only copies the pointer; nothing reads it (`rsp_shell.m`, where Metal
passes nil).

### CoreFoundation, IOKit, the displays (65)

What the game proper calls of these is little, and each caller is known:

- **The display's modes.** At every start the renderer lists them: two functions call `CGMainDisplayID`,
  `CGDisplayCopyAllDisplayModes`, `CGDisplayModeGetWidth` and `GetHeight`, `CGDisplayModeCopyPixelEncoding`,
  `CGDisplayIDToOpenGLDisplayMask`, with `CFArrayGetCount`, `CFArrayGetValueAtIndex` and `CFStringCompare`. The
  Linux shell answers with its display's modes (the six `layer` rows).
- **Strings**: audio device names and UIDs (`CFStringGetCString`, `GetLength`, `Compare`, `CFRelease`), Wwise's
  conversion (`CFStringCreateWithBytes`, `CFStringGetBytes`), `efd`'s UUIDs (`CFUUIDCreateWithBytes`,
  `CFUUIDCreateString`). A small CoreFoundation of ours: counted strings (the image's 39 constant strings have a
  known layout), arrays, a dictionary, UUIDs. 29 rows, a few lines each.
- **The locale** (`CFLocaleCopyCurrent`, `CFLocaleGetValue`) is asked by two functions in the online library's area;
  a run has to say whether they are reached.
- **IOKit's registry**: one function reads the primary Ethernet interface's address (`IOServiceMatching`,
  `IORegistryEntryCreateCFProperty` on the parent). It gets the answer of a machine with no such interface, unless
  a run shows the game wants one.
- **The rest are the shell's or `main`'s**: the display link (7), the sleep assertion, the cursor, the window
  level, the Carbon alerts, `Gestalt`, `NSLog`, `NSApplicationMain`. Stubs.

### Accelerate (5)

The note detection calls `vDSP_fft_zop` with setups from `vDSP_create_fftsetup` (the built-in fix keeps one a size).
An FFT of ours with vDSP's scaling and split-complex layout; `vcosf` and `vpowf` are four calls each. The results
need not match Apple's to the bit: the Intel original's do not match the arm64 library's either.

## Platform

`shims/platform` is the game's Cocoa shell rewritten, in two halves.

- **The C half is portable** and stays, 741 lines: the engine boundary (`rsp_engine.c` and the 14 hooks of
  `rsp_engine_hooks` in `include/rsp_platform.h`: bootstrap, frame, input, focus, resolution, full screen, quality,
  vsync, quit, shutdown), the key and scroll rules (`rsp_logic.c`), the paths (`rsp_paths.c`), `Rocksmith.ini`
  (`rsp_config.c`). Beside it the renderer choice (`rsp_renderer.c`), the frame-rate and scale rules (`rsp_fps.h`,
  whose clock is Mach's; `rsp_scale.h`) and the overlay (`rsp_overlay.c`, plain OpenGL).
- **The Objective-C half is the Mac's**, 1,388 lines: `rsp_shell.m` (the app, the window and view, full screen, the
  display link and render thread, keys, pointer, scrolling, focus, the sleep assertion, the alert) and
  `rsp_present_metal.m`. A Linux shell does the same against the same hooks.

What the Linux shell has to give, as the Mac's does:

- **Window and full screen**: 1280x720 at first, no smaller than 800x600, any shape; full screen as a borderless
  window on the window's display. A mode change is asked for on the main thread and applied at the start of a frame.
  Ten times a second the shell follows the engine's full-screen flag and takes a resized window as the engine's
  resolution.
- **The frame clock**: a render thread with an 8 MB stack that runs `rsp_game_frame` once a refresh with the GL
  context current, at the display's rate, or at `RS_FPS`'s on its own clock with no vsync.
- **Input in the game's terms.** `rsp_post_input(type, a, b)` takes the **macOS virtual key code** and the UTF-16
  character as Cocoa reports it (arrows and function keys as its 0xF700 code points, Command in bit 31): Scaleform's
  key table is indexed by that code, and the mod hook point `input_sink` documents the same. So the shell maps
  Linux key codes to macOS's, and the contract stays. Pointer positions have their origin bottom left, times the
  render scale; the wheel is one step an event, a touchpad 25 pixels a step (`rsp_logic.c`).
- **Focus** (`rsp_set_focus`), the pointer hidden after 10 s without a move, no screen saver while the game is open.
- **The three things the engine asked Cocoa for**: the clipboard's text, an alert, and (not the shell's) a folder
  listing ([Objective-C](#objective-c-libobjc-foundation-appkit-62)); and the display's modes
  ([the displays](#corefoundation-iokit-the-displays-65)).

SDL 3 covers the list and gives an OpenGL compatibility context on both Wayland and X11, so the shell is about
1,500 lines of C on it; the alternative is Wayland and EGL by hand. Learn & Play's window
(`win-x86/app/lp_window.m`, 579 lines) would stand on the same shell.

**Paths.** The home is `~/Library/Application Support/RS Re-ARMed` in about ten places (`RS_NATIVE_HOME` overrides
most), the caches `~/Library/Caches/rs-rearmed` in four. On Linux: `$XDG_DATA_HOME/rs-rearmed` and
`$XDG_CACHE_HOME/rs-rearmed`, from one helper. The folder's layout inside (`game/`, `mods/`, `steam/`, `logs/`,
`settings.env`, `options.env`) stays, since mods and their installers use it, and so does the process name.

## Renderer

`libRSOpenGL` (`shims/opengl`) takes the game's 105 `gl*` calls and two CGL locks and runs them on one of two
backends. The split matters for Linux:

- **There is no backend-neutral GL state.** The shared part is the wrapper table and a few specials (`rsgl.c`,
  812 lines). On the OpenGL backend the state lives in Apple's driver; on Metal the whole GL 2.1 state machine is
  inside `src/metal/` (8,900 lines of Objective-C against Metal objects), and the shader translator
  (`src/msl/`, 3,000 lines) writes Metal's language only.
- **The OpenGL backend is thin.** Apple-specific in it: the context (`NSOpenGLContext` in the shell, CGL for
  offscreen in `rs_gfx.c`), the framework's path for `dlsym` (`rsgl.c`), the SDK's headers for the generated
  table, and `nsgl_hook.m`.

**First: the OpenGL path on Mesa, through EGL.**

- **A compatibility context is required.** The Mac shell makes a legacy 2.1 context
  (`rsp_shell.m`: no profile attribute), and the stream needs one: `glAlphaFunc` every frame, GLSL `#version 120`
  from Scaleform, and ARB assembly programs from the Cg shim in its default mode on OpenGL.
- **The context should report what Apple's does.** Scaleform carries three shader sets and chooses by the GL
  version (`tools/metal/extract_scaleform.py`); on a 4.6 compatibility context the game would take a path no Mac
  ever ran. The Metal core already answers as Apple's legacy context does (version 2.1, GLSL 1.20, its extension
  list and limits, `mtl_context.m`, `mtl_state.m`); the wrappers can do the same in front of Mesa.
- **What changes**: EGL in the place of CGL in `rs_gfx.c` (a window surface, surfaceless for the tests; the
  framebuffer-0 stand-in is plain GL and stays), `eglGetProcAddress` for the loader, the twelve CGL names answered
  by the library as the Metal core does, the table generated from Khronos's headers, the three `APPLE` vertex-array
  names aliased, OpenGL as the default backend. About 500-800 lines.
- **The Cg shim** (`shims/cg`, 2,600 lines) is portable C++ but for about 20 lines. Its `glsl` mode, which every
  Metal player already runs, needs only GLSL 1.20; `arb` stays as a cross-check where Mesa has the ARB programs.
- **Nothing of the shader corpus or the warm-up is needed**: both exist for Metal's compiler and cache.
- **Tests that carry over**: `gfx_offscreen_test`, the 44 GL sequences of `metal_core_test` (single-backend),
  `msl.unit`, the Cg render tests once they make their contexts through `rs_gfx`.

**What the picture loses at first.** Every extra is Metal's: MetalFX upscaling and Retina, the render scale, MSAA,
16x anisotropic filtering, the colour tagging, the performance overlay, low-latency presentation, the
render API for mods, and the copy that gives the tuner's peg its glow (on OpenGL that draw is left to the driver).
The overlay API works on OpenGL. For parity each would be written again on GL (a scaled target and a present pass,
an anisotropy override, MSAA twins); `MANGOHUD` or `GALLIUM_HUD` can stand in for the overlay with no code.

**Not planned: a Vulkan backend.** It would be the Metal core again, 10,000 lines or more, plus a SPIR-V stage for
the translator, and its tests would want the OpenGL backend as their reference anyway. The Metal backend exists
because Apple deprecated OpenGL; Mesa has not.

**Risks**: no compatibility profile or ARB programs on the Apple GPU's driver; a tile-based GPU and draws that
sample their own target (the tuner's glow); Mesa's stricter GLSL compiler; an EGL context current on one thread at
a time, where the Mac shell shares one under the CGL lock; `libRSOpenGL.so` exporting `glClear` beside Mesa's.

## Libraries

`shims/game` (the native zlib, libpng, libjpeg, Lua, crypto and `efd` in the place of the game's own) is portable
C and C++: no Mach, dispatch, CoreFoundation or blocks. One Apple call: CommonCrypto's SHA-1 in
`src/crypto_dsa.c`, a few lines to replace. Its differential harness is another matter ([Tests](#tests)).

## Audio

Nothing replaces CoreAudio today. The game's Wwise engine calls it itself, and the override module
(`overrides/src/audio_*.c`, 980 lines) with `shims/coreaudio` (780) is policy in front of those calls, through
the thunks' hooks (`gen_thunks.py` `HOOKS`): each channel of an interface as an input of its own, the gain, the
buffer sizes, stable identifiers, the default output followed. All 25 calls end in the real framework. On Linux
that framework's part has to exist:

- **The devices** (`AudioObjectGetPropertyData` and its five siblings). The game asks the system object for the
  device list and the default output, with listeners on both, and each device for its UID, name and maker (as
  strings), its channels in and out, its buffer size and range, its sample rate, its stream format and latency, its
  input volume and an overload listener: about 16 selectors, and four more the port's own code asks.
- **The guitar input** (`AudioDeviceCreateIOProcID`, `AudioDeviceStart`, `Stop`): one callback per open device on
  the device's thread, 128 frames, of which the overrides queue mono floats for the Wwise thread; ten slots.
- **The output** (the eleven `AUGraph` calls and four of `AudioUnit`): a graph of two nodes, the hardware output
  unit and a matrix mixer, whose render callback is a game function that copies from the sink's ring. 48 kHz,
  32-bit float, not interleaved; a device buffer of 256 frames, 128 on an interface that also has inputs.
- **Around them**: Mach semaphores between the callback and the Wwise thread, which the Mac app makes a real-time
  thread in the device's workgroup.

So Linux needs **CoreAudio's device model, as much as those calls use, on PipeWire**: one library that both the
import thunks and the override module call, with the device objects and their selectors, the I/O callbacks, and a
stand-in graph that pulls the render callback and applies the mixer's volumes. About 1,500 lines. Two precedents
are in the project: upstream's iPhone port answers the same device calls from iOS's audio session
(`ios/compat/CoreAudio/CoreAudio.h`, `rs_hal_ios.m`, 330 lines, the selectors' values kept so the audio code stays
one source), and Learn & Play's WASAPI face on CoreAudio is the same job in the other direction
(`win-x86/shims/audio`, 3,000 lines).

What stays as it is: `audio_in.c`, `audio_ring.c`; against the stand-in's header, `audio_devs.c`, `audio_chan.c`,
`audio_trace.c`; with small changes, `audio_out.c` (a dispatch queue), `rs_audio_input.c` (`os_unfair_lock`, a
locale call, the `~/Library` paths) and `rs_audio_host.c` (the thread policy and the workgroup). The `si_io` hook
hands mods an `AudioBufferList`, so that struct's layout is part of the mod interface and stays.

Choices inside the layer, to settle with a guitar in hand:

- **PipeWire's own API or its JACK face.** JACK gives the callback, the ports and the latency figures with the
  least code, and is PipeWire underneath on Fedora; PipeWire's streams need no extra package and see devices as the
  desktop does.
- **Latency.** PipeWire takes a per-stream request for the buffer (`node.latency`); whether a class-compliant
  interface holds 128 frames without dropouts on this machine is the measurement to make. A direct ALSA device is
  the fallback, as WASAPI's exclusive mode is on Windows.
- **The Real Tone Cable** is told by its name: the game looks for "Rocksmith USB" in a device's name and maker, and
  the port's own check adds the cable's USB ids from the device's model UID. It is a class-compliant USB audio
  device and should appear as one more input, under the name ALSA gives it (to check).
- **Real-time threads**: Linux has no audio workgroups; `SCHED_FIFO` through the desktop's real-time broker.

## Video (Bink)

`shims/bink` decodes with a static FFmpeg 7.1.2 it downloads, checks and builds with its Bink 2 patch (C, no
assembly, no hardware decoding). The build script names `--target-os=darwin`, `-arch`, `shasum` and `lipo`, and
the export list has underscore names; the code's one Mac call is `mach_absolute_time`.

## Steam

`shims/steam` is C++17 with no Objective-C, 1,700 lines. The Mac's in it: Steam's folder
(`~/Library/Application Support/Steam`, with `config/loginusers.vdf` under it; on Linux `~/.local/share/Steam`, and
a Flatpak's own), `copyfile` and `removefile`, a `sysctl` for the boot time, `_NSGetExecutablePath` with its
`.app/Contents/MacOS` test, and the Makefile. The interfaces it shows the game are Itanium vtables, the same on
Linux.

The saves are the Mac game's on either system: a profile from the Mac app's `steam/<account>/221680/remote` plays
in a Linux build as it is, and the other way.

## Online services

Three layers keep the Mac app offline; each has its Linux form.

- **The offline Steam layer and the online stub** are the project's own code and come along.
- **The network guard** (`tools/ws8/netguard`, 286 lines) is inserted by dyld and interposes thirteen calls of the
  whole process (`connect`, `sendto`, the resolver, `system`, `popen`), and the app starts itself again when it is
  not there (`netguard_start`). On Linux the game's own socket imports are functions of ours that refuse
  ([libSystem](#libsystem-263)), so the game has no route to a socket at all, as Learn & Play has none. The guard is
  then for what else is in the process: a mod. Its Linux form can be the same rules as an `LD_PRELOAD` library,
  required whenever mods load, as Learn & Play requires it; a filter on the process's own system calls, set before
  mods load, or a network namespace of its own is stronger and needs no second start. Either has to leave local
  sockets open: PipeWire and Wayland are reached through them.
- `Rocksmith.ini`'s forced keys and the launcher's rules are shell and carry over.

## Input

The shell's work ([Platform](#platform)). The keys the Mac app adds (`mods.c`'s keyboard table) are in terms of
macOS key codes and stay.

## Built-in fixes and mods

- **The built-in fixes** are C against the game's names and the runtime, and come along. Two lean on macOS: the
  FFT setups (`fftsetup.c`, vDSP) and the worker-thread priorities (`mods.c`, macOS's two usable values); both get
  their Linux rule in the Darwin layer.
- **The mod loader** takes every `.dylib` of `mods/`; on Linux every `.so`. The headers a mod includes
  (`rc_mod.h` and the rest) are plain C; the app has no export list and exports the whole runtime, with
  `-rdynamic` on Linux, and a mod is built `-shared -fPIC`. What a mod sees stays the Mac's on any host: the
  `rc_ctx` layout, the hook names, macOS key codes at `input_sink`, an `AudioBufferList` at `si_io`.
- **The mods players have are Mach-O.** rearmed-mods, RS Companion and rearmed-songbridge would each need a Linux
  build of their own, in their own projects; their apps beside the game (the settings app, ToneHost's Audio Unit
  host, RS Companion's app) are Mac programs. Nothing breaks for a Mac player; a Linux player has no mods until
  then. This is for the maintainer and those projects to know before a Linux build is offered to anyone.

## Installer and build

**Build.** The scripts name Apple's tools throughout (`clang` by name in 37 files, `-arch arm64` in 22,
`codesign` in 11, `.dylib` in 30), and no file has a platform conditional today.

| macOS | Linux |
|---|---|
| `-arch arm64`, `-mmacosx-version-min` | nothing |
| `libtool -static` | `llvm-ar rcs` |
| `lipo`, `codesign`, `install_name_tool` | nothing; `-Wl,-rpath,'$ORIGIN'` |
| `-framework …` | the Linux libraries by pkg-config |
| `-Wl,-undefined,error`, `-Wl,-export_dynamic` | `-Wl,--no-undefined`, `-rdynamic` |
| `-Wl,-stack_size,0x10000000` | the engine on a thread with that stack |
| ThinLTO's cache and PGO through `xcrun` | lld's `--thinlto-cache-dir`, `llvm-profdata` |
| `sysctl -n hw.ncpu`, `hw.memsize` | `nproc`, `/proc/meminfo` |
| the `.app` bundle, `Info.plist`, the icon | a folder with the program and its `.so` files |
| `nm … \| grep '^_F_'`, ld64's undefined-symbol text | no underscore; lld's wording |

The root CMake project declares Objective-C and asks `xcrun` for the SDK at configure, so none of it configures on
Linux as it is; by source, the game's libraries, the online stub and the shader translator's unit test would build.

**The game's files.** `tools/play-native/gamefiles.py` holds nearly all of it in Python and is shared by both
editions. For Linux:

- **Where the Mac game comes from.** Steam for Linux does not offer the Mac depot in its library, but the route the
  installer already uses does not need it to: `download_depot 221680 221682 <manifest>` in Steam's console, the way
  the Mac installer fetches Learn & Play's Windows depot. On Asahi that Steam is the x86 one in its virtual machine
  (to check: where its files land on the host). Other routes: a copy of the game folder from macOS, on the same
  machine or another (to check: Linux reads the Mac's APFS volume only with an extra driver), or a depot downloader.
- **The copy**: `clonefile` becomes a reflink copy on btrfs, Fedora's default; the real-copy fallback exists.
- **Steam's facts** (`loginusers.vdf`, the ownership tickets, the package cache) are files with the same formats
  under Steam's Linux folder. Asking Valve's library who owns the first Rocksmith's DLC cannot work (an x86
  library); the three file sources remain.
- **DLC** are depots with both platforms' archives, fetched the same way.

**Install and start.** `install.sh` and `launch.sh` are shell; the Linux forms drop the bundle, `open -n -a`,
`launchctl` and the launch stub (which exists for macOS's privacy prompts), and add a `.desktop` file. The
settings files (`settings.env` with 9 keys, `options.env` with 38) are read by the launcher's shell, which carries
over; about ten of the keys are Metal's or CoreAudio's and have no meaning on Linux.

**The installer app** is Swift and AppKit. Its steps are shell commands a command-line installer can run in the
same order; its self-update (a signed zip of an app) has no Linux form yet.

## Learn & Play

Learn & Play's game calls nothing of the host: its imports go to the project's Win32 seam
(`win-x86/shims`, 20,500 lines of C on POSIX), and its C and C++ libraries are Microsoft's own, translated. Of
macOS it has:

| | Lines | On Linux |
|---|---|---|
| `win-x86/app/lp_window.m` | 579 | the Linux shell |
| `win-x86/shims/audio/wa_engine.c` (CoreAudio units) | 813 | the audio layer |
| The renderer: Metal only, no window on OpenGL | | the OpenGL path with a window, which the D3D9 tests already use offscreen |
| `clock_gettime_nsec_np`, QoS classes, Mach semaphores, `st_mtimespec`, `arc4random` | about 50 lines in 14 files | small replacements |
| `lp_main.c`: the bundle, paths | 540 | paths |

So once the shell, the renderer's window on OpenGL and the audio layer exist for the Mac game, Learn & Play is
mostly a build. It also needs its 4 GB guest window low in the address space (to check: nothing of Linux's sits
there) and its game image, which comes from a release's seed as on the Mac. Nobody working on this branch owns
Learn & Play, so its part would be written and not run.

## Tests

| | Suites |
|---|---|
| Run on Linux as they are, or nearly | `targets.check`, `steamless.decrypt`, `release.tools`, `play-native.cache_overlay`, `play-native.settings`, `msl.unit`, `lp.screen`, `lp.unpack`, `win-x86.shaders`, `linux.imports`; `play-native.gamefiles` but for its clone and Valve-library cases |
| After the runtime builds there | `recomp.hooks` (both runtimes), the seam test, the 32-bit `handcases` and `innercases` (Unicorn); `recomp.inner64` needs a Mach-O linker for its made-up image |
| After the renderer's EGL path | `opengl.gfx.offscreen.gl`, the 44 `metal_core_test` sequences, the Cg render tests, `lp.d3d9` on OpenGL |
| The Mac's alone | everything Metal, `opengl.system.*`, `shim.*.macho`, `libstdcxx.*`, `installer.update`, the measured runs |
| Need Rosetta | `game.difftest.x86_64`, `platform.difftest.x86_64`, every `*.x86_64` variant |

The differential harness (`shims/game/harness`) starts an x86-64 program that maps the game image and runs original
functions against the rewrites; macOS runs it under Rosetta. Linux has no such thing for a Mach-O. The rewrites do
not change with the host, so their check stays a Mac's; a Linux build runs the native half against answers a Mac
made (which are the game's output and cannot be in the repository), or leaves it out and says so.

Two checks would pass without checking on ELF, where symbols have no leading underscore: the no-network test of
the game's libraries (`shims/game/test/no_network.cmake`) and the build's count of translated functions
(`app/build-m4.sh`).

## How this was found

- **Read**: `AGENTS.md`, the README, `docs/` (both editions' design, plan, install and testing pages, the
  recompiler, the release pages), `win-x86/README.md`, `tools/targets/README.md`, and the sources named above.
  Upstream's `ios-port` branch for how a second host was added.
- **The imports**: `linux/import_census.py` on the decrypted image of the pinned build (`image_sha256` in
  `tools/targets/mac-x64/target.toml`): 597 called and 84 data imports, the same 597 as the thunk report of a
  1.20.0 build.
- **Who calls each import**: the call sites of every import's thunk in that build's generated C, by the calling
  function's place in the image (the game's `main`, its shell classes, the ARC support code, the rest), and the
  same for the functions that touch a selector or a class reference. The image's initializer list and class lists
  were read for entries into that code. Static: a call through a pointer does not show.
- **The generated C**: the 243 parts of that build, each through
  `clang --target=aarch64-unknown-linux-gnu -fsyntax-only` with the build's defines and a five-line `string.h`
  (no Linux headers were at hand); no diagnostics. One part compiled to an ELF object at `-O1`.
- **The empty string**: counted in a disassembly of the image.
- **Not done**: nothing was built for Linux beyond that check, nothing ran on Linux, the game was not run for
  this, and no Linux machine was at hand. The Mesa, PipeWire, Steam and kernel statements are from those projects'
  pages and general knowledge, not from the machine. The sizes of new code are estimates from the Mac's
  counterparts.
