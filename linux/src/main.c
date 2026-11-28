// rs-rearmed for x86-64 Linux: the Mac game's image loaded and run as it is. See rsl.h and linux/X86-64.md.
//
//   rs-rearmed [--image FILE] --bind-only    map and bind, say who answers the imports
//   rs-rearmed [--image FILE] --init-only    and run the static initializers
//   rs-rearmed [--image FILE] --boot DIR     and call the game's GameBootstrap, with DIR as the game's folder
//   --frames N                               with --boot: then N calls of the game's GameFrame
//   rs-rearmed [--image FILE] --run DIR      the game in a window, DIR being the game's folder (needs SDL 3)
//       --frames N    stop after N frames         --size WxH        the window's size (1280x720)
//       --picture FILE --picture-frame N          save frame N as a PPM file
//       --picture FILE --picture-every N          or every Nth frame, as FILE.<frame>.ppm
//       --keys 600:return,900:down                keys typed by a script, each at its frame
//   --count                                  count each import's calls, and list them
//   --no-overrides                           leave every game function as it is (the online services too)
//   --survey                                 with --boot: a function with no answer is noted and returns 0, and
//                                            the run goes on, to list what the game's start asks for
//
// The image is the decrypted one tools/steamless/decrypt.py writes: --image, else $RC_IMAGE, else
// build/steamless/Rocksmith2014_x64_decrypted under the current folder. Exit status: 0; 1 for an image that cannot
// be loaded; 2 for what is not written yet; 3 when the game reached an import with no answer; 4 for a fault.
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rc_target_names.h"  // the game's addresses by name: tools/targets/mac-x64, through targets.py
#include "rsl.h"

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv, char **envp) {
  const char *path = getenv("RC_IMAGE");
  const char *boot = NULL, *run = NULL, *picture = NULL, *keys = NULL;
  int width = 0, height = 0;
  long picture_frame = 1, picture_every = 0;
  int bind_only = 0, init_only = 0, count = 0, survey = 0, frames = 0, no_overrides = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--image") && i + 1 < argc) path = argv[++i];
    else if (!strcmp(argv[i], "--bind-only")) bind_only = 1;
    else if (!strcmp(argv[i], "--init-only")) init_only = 1;
    else if (!strcmp(argv[i], "--count")) count = 1;
    else if (!strcmp(argv[i], "--survey")) survey = 1;
    else if (!strcmp(argv[i], "--no-overrides")) no_overrides = 1;
    else if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--run") && i + 1 < argc) run = argv[++i];
    else if (!strcmp(argv[i], "--picture") && i + 1 < argc) picture = argv[++i];
    else if (!strcmp(argv[i], "--picture-frame") && i + 1 < argc) picture_frame = atol(argv[++i]);
    else if (!strcmp(argv[i], "--picture-every") && i + 1 < argc) picture_every = atol(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &width, &height) == 2) i++;
    else {
      fprintf(stderr, "usage: %s [--image FILE] [--count] --bind-only | --init-only | --boot DIR [--survey]\n", argv[0]);
      return 2;
    }
  }
  if (!path || !*path) path = "build/steamless/Rocksmith2014_x64_decrypted";

  // What the project's libraries read from the environment, before any of them loads: the game stays offline
  // (the Steam library's stub mode, as the Mac app sets it), and the game's own program in the game's folder
  // stands for this one where a library finds the game by the program's place (linux/compat).
  setenv("RS_STEAM_MODE", "stub", 0);
  // The port's own folder, laid out as the Mac app's home is (steam/ for the saves): RS_NATIVE_HOME, else
  // rs-rearmed under XDG's data folder.
  char home[2048], dir[2304];
  const char *e = getenv("RS_NATIVE_HOME"), *xdg = getenv("XDG_DATA_HOME"), *user = getenv("HOME");
  if (e && *e) snprintf(home, sizeof home, "%s", e);
  else if (xdg && *xdg == '/') snprintf(home, sizeof home, "%s/rs-rearmed", xdg);
  else snprintf(home, sizeof home, "%s/.local/share/rs-rearmed", user ? user : "");
  snprintf(dir, sizeof dir, "%s/steam", home);
  setenv("RS_STEAM_DATA_DIR", dir, 0);
  // The guitar input's settings file, in that folder (the Mac's is in its own; shims/coreaudio/rs_audio_input.h).
  snprintf(dir, sizeof dir, "%s/guitar-input.ini", home);
  setenv("RS_AUDIO_INPUT_FILE", dir, 0);
  setenv("RS_NATIVE_HOME", home, 0);  // (where the level's "calibration has run" mark is kept)
  // The renderer's library on OpenGL (its Metal backend is the Mac's), and the Cg library's cache of program
  // pairs out of the Mac's ~/Library.
  setenv("RS_GL_BACKEND", "gl", 1);
  setenv("RS_CG_PAIR_CACHE", "0", 0);
  if (boot || run) {
    char exe[4096];
    if (snprintf(exe, sizeof exe, "%s/Rocksmith2014.app/Contents/MacOS/Rocksmith2014", boot ? boot : run) < (int)sizeof exe)
      setenv("RS_MAC_EXECUTABLE", exe, 0);
  }

  rsl_info info;
  double t0 = now();
  if (rsl_load(path, count, &info)) return 1;
  double t1 = now();
  printf("image: 0x%llx to 0x%llx, loaded in %.3f s; unwind tables %s\n", (unsigned long long)info.lo,
         (unsigned long long)info.hi, t1 - t0, info.unwind ? "registered" : "not registered");
  // The game's own code needs no change to run; these are the functions the app replaces on every host. A
  // made-up image (the tests') has none of them: only an image of the game's size is patched.
  rsl_direct_image(info.lo, info.hi);
  if (!no_overrides && info.hi - info.lo > 0x1000000) {
    int n = rsl_install_overrides();
    if (n < 0) return 1;
    printf("replaced: %d game functions with native ones (the online services, offline; the audio's two; the built-in fixes)\n", n);
    int hooks = rsl_install_hooks();
    printf("hook points: %d; mid points with a hook: %d\n", hooks, rsl_install_mids());
  }
  rsl_report_binding(stdout);
  if (bind_only) return 0;
  if (!init_only && !boot && !run) {
    fprintf(stderr, "rs-rearmed: the game's start is not written yet (linux/PLAN.md, X2): --init-only runs what is\n");
    return 2;
  }

  fflush(stdout);
  int ran = rsl_run_initializers(argc, argv, envp);
  printf("%d of %d initializers ran in %.3f s", ran, info.ninits, now() - t1);
  if (info.unwind) printf("; %d of them have a frame description the unwinder finds", rsl_initializers_with_unwind());
  printf("\n");
  if (run) {
#ifdef RSL_HAVE_SHELL
    fflush(stdout);
#ifdef RSL_HAVE_PIPEWIRE
    const char *audio = getenv("RS_AUDIO");  // RS_AUDIO=silent: the game's sound is asked for and dropped
    if (!(audio && !strcmp(audio, "silent"))) rsl_audio_pipewire();
#endif
    rsl_shell_options opt = {run, width, height, frames, survey, picture, picture_frame, picture_every, keys};
    int status = rsl_shell_run(&opt);
    if (count) rsl_report_calls(stdout);
    fflush(NULL);
    _exit(status);
#else
    (void)picture, (void)picture_frame, (void)picture_every, (void)width, (void)height, (void)keys;
    fprintf(stderr, "rs-rearmed: built without the shell (SDL 3's headers were not there): --run is not in this build\n");
    return 2;
#endif
  }
  if (boot) {
    // GameBootstrap(app_dir, cwd): the game's root, Lua, Rocksmith.ini, the display settings, the file system,
    // the main service. The Mac app calls it from the shell, with the window and its OpenGL context made.
    printf("GameBootstrap, with the game in %s\n", boot);
    fflush(stdout);
    rsl_survey(survey);
    ((void (*)(const char *, const char *))(uintptr_t)RC_ADDR_game_bootstrap)(boot, boot);
    printf("GameBootstrap returned: the Lua owner %s, the main service %s\n",
           *(void **)(uintptr_t)RC_ADDR_lua_owner ? "made" : "not made",
           *(void **)(uintptr_t)RC_ADDR_main_service ? "made" : "not made");
    fflush(stdout);
    int quit = 0, f = 0;
    for (; f < frames && !quit; f++) quit = ((int (*)(void))(uintptr_t)RC_ADDR_game_frame)() & 0xff;
    if (frames) printf("GameFrame: %d calls%s\n", f, quit ? ", the last said the engine has shut down" : "");
    rsl_survey(0);
    if (survey) rsl_report_survey(stdout);
  }
  if (count) rsl_report_calls(stdout);
  fflush(NULL);
  _exit(0);  // not exit: the game's own destructors, registered by its initializers, are no part of this run
}
