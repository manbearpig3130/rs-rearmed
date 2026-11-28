// A mod for the loader's test (linux/test_loader.py): what a mod is on Linux, a shared library in the mods folder
// that names the game's points (rc_mod.h), built with
//   cc -shared -fPIC -I tools/ws7/recomp/runtime -I tools/ws7/recomp/overrides -I build/linux/target test_mod.c -o test_mod.so
// Its constructor asks for a hook on the game's frame, and it says so when it has seen a hundred of them. It
// switches on the edit groups RS_TEST_MOD_EDITS names (commas between; plain_packages when unset) and hooks the mid
// points RS_TEST_MOD_MIDS names with a function that changes nothing.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_mod.h"

static int g_before, g_after;
static _Atomic int g_asked, g_listed, g_mid_calls;

// A mid point's hook that lets the instruction run: the game goes on as without it.
static void at_mid(rc_mid_frame *f, void *user) {
  (void)f, (void)user;
  g_mid_calls++;
}

// Each name of the list in the variable NAME (or of DFLT), to ONE; how many it took (0 from it).
static int each_name(const char *name, const char *dflt, int (*one)(const char *)) {
  const char *list = getenv(name) ? getenv(name) : dflt;
  int n = 0;
  while (*list) {
    char item[64];
    size_t len = strcspn(list, ",");
    if (len && len < sizeof item) {
      memcpy(item, list, len), item[len] = 0;
      n += one(item) == 0;
    }
    list += len + (list[len] == ',');
  }
  return n;
}
static int edits_on(const char *group) {
  int r = rc_edit_enable(group);
  if (r) fprintf(stderr, "[test-mod] the edit group %s was refused, and the mod goes without it\n", group);
  return r;
}
static int mid_hooked(const char *point) { return rc_mid_add_name(point, at_mid, NULL) < 0 ? -1 : 0; }

// A provider of files (rc_file.h): asked for every file the game opens to read. It serves none; it adds a name to
// the listing of the game's dlc folder (which the game reads past the title screen, later than the test looks).
static void *provide_open(void *user, const char *path, rc_file_info *info) {
  (void)user, (void)path, (void)info;
  g_asked++;
  return NULL;
}
static int64_t provide_read(void *user, void *file, void *buf, uint64_t n, int64_t off) {
  (void)user, (void)file, (void)buf, (void)n, (void)off;
  return -1;
}
static void provide_close(void *user, void *file) { (void)user, (void)file; }
static void provide_list(void *user, const char *dir, rc_file_add add, void *ctx) {
  (void)user;
  size_t n = strlen(dir);
  if (n >= 3 && !strcmp(dir + n - 3, "dlc")) {
    add(ctx, "from-the-test-mod.txt");
    if (!g_listed++) fprintf(stderr, "[test-mod] a name added to the listing of %s\n", dir);
  }
}

static int before_frame(rc_ctx *c, void *user) {
  (void)c, (void)user;
  g_before++;
  return 0;
}

static void after_frame(rc_ctx *c, void *user) {
  (void)c, (void)user;
  if (++g_after == 100)
    fprintf(stderr, "[test-mod] 100 frames: %d hooks before, %d after; the provider was asked for files: %s; mid points reached: %d\n",
            g_before, g_after, g_asked ? "yes" : "no", (int)g_mid_calls);
}

__attribute__((constructor)) static void test_mod_init(void) {
  int r = rc_hook_add_name("game_frame", before_frame, after_frame, NULL);
  fprintf(stderr, "[test-mod] on %s: game_frame %s, the settings folder %s\n", rc_target(), r ? "refused" : "hooked",
          getenv("RS_NATIVE_HOME") ? "known" : "not known");
  rc_hook_add_name("no_such_point", before_frame, NULL, NULL);  // refused, with a line of the runtime's
  int groups = each_name("RS_TEST_MOD_EDITS", "plain_packages", edits_on), points = each_name("RS_TEST_MOD_MIDS", "", mid_hooked);
  fprintf(stderr, "[test-mod] edit groups on: %d; mid points hooked: %d\n", groups, points);
  static const rc_file_provider files = {.open = provide_open, .read = provide_read, .close = provide_close, .list = provide_list};
  rc_file_provide(&files);
}
