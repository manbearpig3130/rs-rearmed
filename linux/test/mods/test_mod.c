// A mod for the loader's test (linux/test_loader.py): what a mod is on Linux, a shared library in the mods folder
// that names the game's points (rc_mod.h), built with
//   cc -shared -fPIC -I tools/ws7/recomp/runtime -I tools/ws7/recomp/overrides -I build/linux/target test_mod.c -o test_mod.so
// Its constructor asks for a hook on the game's frame, and it says so when it has seen a hundred of them. It
// switches on the edit groups RS_TEST_MOD_EDITS names (commas between; plain_packages when unset) and hooks the mid
// points RS_TEST_MOD_MIDS names with a function that changes nothing. With RS_TEST_MOD_OVERLAY=1 it draws over the
// game's frame through the overlay API (rs_overlay.h), found by name as a mod for every edition finds it: a
// magenta square and an image, and it reads the frame back to say what it finds where it drew.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>

#include "rc_mod.h"
#include "rs_overlay.h"

static int g_before, g_after;
static _Atomic int g_asked, g_listed, g_mid_calls;

// The overlay's callback, once a frame after the game has drawn it: a square of one colour, an image of another
// beside it (premultiplied, half transparent in its right half), and the frame read back at the middle of each.
static void draw_over(rs_overlay_frame *f, void *user) {
  (void)user;
  static rs_overlay_image *image;
  static int said;
  rs_overlay_image *(*create)(rs_overlay_frame *, int, int) = dlsym(RTLD_DEFAULT, "rs_overlay_image_create");
  int (*update)(rs_overlay_frame *, rs_overlay_image *, const void *, int) = dlsym(RTLD_DEFAULT, "rs_overlay_image_update");
  int (*draw)(rs_overlay_frame *, rs_overlay_image *, float, float, float, float, float) = dlsym(RTLD_DEFAULT, "rs_overlay_draw_image");
  int (*fill)(rs_overlay_frame *, float, float, float, float, uint32_t) = dlsym(RTLD_DEFAULT, "rs_overlay_fill_rect");
  int (*read)(rs_overlay_frame *, int, int, int, int, void *) = dlsym(RTLD_DEFAULT, "rs_overlay_read_pixels");
  if (!create || !update || !draw || !fill || !read) return;
  if (!image && (image = create(f, 16, 16))) {
    uint8_t px[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; i++) {
      int half = i % 16 >= 8;  // opaque green, then green at half alpha (premultiplied)
      px[4 * i] = 0, px[4 * i + 1] = half ? 128 : 255, px[4 * i + 2] = 0, px[4 * i + 3] = half ? 128 : 255;
    }
    update(f, image, px, 0);
  }
  fill(f, 8, 8, 16, 16, 0xff00ffffu);
  if (image) draw(f, image, 40, 8, 16, 16, 1.0f);
  uint8_t a[4] = {0}, b[4] = {0};
  if (!said && f->index >= 20 && !read(f, 16, 16, 1, 1, a) && !read(f, 44, 16, 1, 1, b)) {
    said = 1;
    fprintf(stderr, "[test-mod] overlay on a frame of %dx%d: the square reads %02x%02x%02x%02x, the image %02x%02x%02x%02x\n", f->width,
            f->height, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
  }
}

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
  if (getenv("RS_TEST_MOD_OVERLAY")) {
    int (*version)(void) = dlsym(RTLD_DEFAULT, "rs_overlay_api_version");
    int (*add)(rs_overlay_fn, void *) = dlsym(RTLD_DEFAULT, "rs_overlay_register");
    fprintf(stderr, "[test-mod] overlay API %d: %s\n", version ? version() : 0, add && !add(draw_over, NULL) ? "registered" : "not there");
  }
  static const rc_file_provider files = {.open = provide_open, .read = provide_read, .close = provide_close, .list = provide_list};
  rc_file_provide(&files);
}
