// rs-rearmed for x86-64 Linux: the Mac game's image loaded and run as it is. See rsl.h and linux/X86-64.md.
//
//   rs-rearmed [--image FILE] --bind-only    map and bind, say who answers the imports
//   rs-rearmed [--image FILE] --init-only    and run the static initializers
//   --count                                  count each import's calls, and list them
//
// The image is the decrypted one tools/steamless/decrypt.py writes: --image, else $RC_IMAGE, else
// build/steamless/Rocksmith2014_x64_decrypted under the current folder. Exit status: 0; 1 for an image that cannot
// be loaded; 2 for what is not written yet; 3 when the game reached an import with no answer; 4 for a fault.
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rsl.h"

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv, char **envp) {
  const char *path = getenv("RC_IMAGE");
  int bind_only = 0, init_only = 0, count = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--image") && i + 1 < argc) path = argv[++i];
    else if (!strcmp(argv[i], "--bind-only")) bind_only = 1;
    else if (!strcmp(argv[i], "--init-only")) init_only = 1;
    else if (!strcmp(argv[i], "--count")) count = 1;
    else {
      fprintf(stderr, "usage: %s [--image FILE] [--count] --bind-only | --init-only\n", argv[0]);
      return 2;
    }
  }
  if (!path || !*path) path = "build/steamless/Rocksmith2014_x64_decrypted";

  rsl_info info;
  double t0 = now();
  if (rsl_load(path, count, &info)) return 1;
  double t1 = now();
  printf("image: 0x%llx to 0x%llx, loaded in %.3f s; unwind tables %s\n", (unsigned long long)info.lo,
         (unsigned long long)info.hi, t1 - t0, info.unwind ? "registered" : "not registered");
  rsl_report_binding(stdout);
  if (bind_only) return 0;
  if (!init_only) {
    fprintf(stderr, "rs-rearmed: the game's start is not written yet (linux/PLAN.md, X2): --init-only runs what is\n");
    return 2;
  }

  fflush(stdout);
  int ran = rsl_run_initializers(argc, argv, envp);
  printf("%d of %d initializers ran in %.3f s", ran, info.ninits, now() - t1);
  if (info.unwind) printf("; %d of them have a frame description the unwinder finds", rsl_initializers_with_unwind());
  printf("\n");
  if (count) rsl_report_calls(stdout);
  fflush(NULL);
  _exit(0);  // not exit: the game's own destructors, registered by its initializers, are no part of this run
}
