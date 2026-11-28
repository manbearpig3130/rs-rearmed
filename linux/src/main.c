// rs-rearmed for x86-64 Linux: the Mac game's image loaded and run as it is. See rsl.h and linux/X86-64.md.
//
//   rs-rearmed [--image FILE] --bind-only    map and bind, say who answers the imports
//   rs-rearmed [--image FILE] --init-only    and run the static initializers
//   rs-rearmed [--image FILE] --boot DIR     and call the game's GameBootstrap, with DIR as the game's folder
//   --frames N                               with --boot: then N calls of the game's GameFrame
//   --count                                  count each import's calls, and list them
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
  const char *boot = NULL;
  int bind_only = 0, init_only = 0, count = 0, survey = 0, frames = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--image") && i + 1 < argc) path = argv[++i];
    else if (!strcmp(argv[i], "--bind-only")) bind_only = 1;
    else if (!strcmp(argv[i], "--init-only")) init_only = 1;
    else if (!strcmp(argv[i], "--count")) count = 1;
    else if (!strcmp(argv[i], "--survey")) survey = 1;
    else if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
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
  if (boot) {
    char exe[4096];
    if (snprintf(exe, sizeof exe, "%s/Rocksmith2014.app/Contents/MacOS/Rocksmith2014", boot) < (int)sizeof exe)
      setenv("RS_MAC_EXECUTABLE", exe, 0);
  }

  rsl_info info;
  double t0 = now();
  if (rsl_load(path, count, &info)) return 1;
  double t1 = now();
  printf("image: 0x%llx to 0x%llx, loaded in %.3f s; unwind tables %s\n", (unsigned long long)info.lo,
         (unsigned long long)info.hi, t1 - t0, info.unwind ? "registered" : "not registered");
  rsl_report_binding(stdout);
  if (bind_only) return 0;
  if (!init_only && !boot) {
    fprintf(stderr, "rs-rearmed: the game's start is not written yet (linux/PLAN.md, X2): --init-only runs what is\n");
    return 2;
  }

  fflush(stdout);
  int ran = rsl_run_initializers(argc, argv, envp);
  printf("%d of %d initializers ran in %.3f s", ran, info.ninits, now() - t1);
  if (info.unwind) printf("; %d of them have a frame description the unwinder finds", rsl_initializers_with_unwind());
  printf("\n");
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
