// The shell's Metal presentation (rsp_present_metal.m) on a CAMetalLayer that is in no window: a
// layer still hands out drawables, so the layer settings, the rs_gfx layer context, clears,
// presents (frames-in-flight limit, no nextDrawable timeouts), resize, the swap interval and the
// lock can be checked without opening anything. Run with RS_GL_BACKEND=metal; with
// RS_GL_BACKEND=gl it checks that the shell stays on GL. MRC, like the shell.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATextLayer.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/rsp_present.h"

static int g_pass, g_fail;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (cond) g_pass++;                           \
    else {                                        \
      g_fail++;                                   \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

static double now_s(void) {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

static rsp_mtl *g_m;
static atomic_int g_locked;
static void *locker(void *arg) {
  (void)arg;
  rsp_mtl_lock(g_m);
  atomic_store(&g_locked, 1);
  rsp_mtl_unlock(g_m);
  return NULL;
}

// Clear to (r, g, b), read the default framebuffer's centre back, present. Returns the present time.
static double frame(float r, float g, float b, int w, int h, unsigned char px[4]) {
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  if (px) glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  double t = now_s();
  rsp_mtl_present(g_m);
  return now_s() - t;
}

// A layer whose drawables can be read back (framebufferOnly stays NO, whatever it is set to), keeping the last one it
// handed out: what the screen got, for test_presented.
@interface RSPTapLayer : CAMetalLayer
@property(nonatomic, retain) id<CAMetalDrawable> last;
@end
@implementation RSPTapLayer
- (void)setFramebufferOnly:(BOOL)only {
  (void)only;
  [super setFramebufferOnly:NO];
}
- (id<CAMetalDrawable>)nextDrawable {
  id<CAMetalDrawable> d = [super nextDrawable];
  self.last = d;
  return d;
}
- (void)dealloc {
  [_last release];
  [super dealloc];
}
@end

// The last presented frame's pixels, w x h BGRA with row 0 at the top of the screen, once the GPU is done with it
// (glFinish waits for the frame's command buffer). 0: no drawable of that size, or the copy failed.
static int read_presented(RSPTapLayer *layer, int w, int h, unsigned char *out) {
  id<CAMetalDrawable> d = layer.last;
  if (!d || (int)d.texture.width != w || (int)d.texture.height != h) return 0;
  glFinish();
  id<MTLCommandQueue> q = [layer.device newCommandQueue];
  id<MTLBuffer> b = [layer.device newBufferWithLength:(NSUInteger)w * h * 4 options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> cb = [q commandBuffer];
  id<MTLBlitCommandEncoder> e = [cb blitCommandEncoder];
  [e copyFromTexture:d.texture
             sourceSlice:0
             sourceLevel:0
            sourceOrigin:MTLOriginMake(0, 0, 0)
              sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
                toBuffer:b
       destinationOffset:0
  destinationBytesPerRow:(NSUInteger)w * 4
destinationBytesPerImage:(NSUInteger)w * h * 4];
  [e endEncoding];
  [cb commit];
  [cb waitUntilCompleted];
  int ok = cb.status == MTLCommandBufferStatusCompleted;
  if (ok) memcpy(out, b.contents, (size_t)w * h * 4);
  [b release];
  [q release];
  return ok;
}

// The frame test_presented checks, in GL's coordinates (y up): on the left one-pixel white columns on black (x even
// white), on the right red over green.
static void draw_pattern(int w, int h) {
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glClearColor(1, 1, 1, 1);
  for (int x = 0; x < w / 2; x += 2) {
    glScissor(x, 0, 1, h);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glClearColor(1, 0, 0, 1);
  glScissor(w / 2, h / 2, w - w / 2, h - h / 2);
  glClear(GL_COLOR_BUFFER_BIT);
  glClearColor(0, 1, 0, 1);
  glScissor(w / 2, 0, w - w / 2, h / 2);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
}

static int test_gl(void) {
  CHECK(!rsp_mtl_selected(), "RS_GL_BACKEND=gl: the shell must stay on GL");
  CAMetalLayer *layer = [CAMetalLayer layer];
  CHECK(rsp_mtl_create(layer, 64, 48, 1) == NULL, "no Metal context on GL");
  return 0;
}

static int test_metal(void) {
  CHECK(rsp_mtl_selected(), "RS_GL_BACKEND=metal: Metal presentation selected");
  if (!rsp_mtl_selected()) return 1;
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = MTLCreateSystemDefaultDevice();
  [layer.device release];
  g_m = rsp_mtl_create(layer, 64, 48, 1);
  CHECK(g_m != NULL, "rs_gfx_create_layer");
  if (!g_m) return 1;
  CHECK(rsp_mtl_layer(g_m) == layer, "layer");
  CHECK(layer.pixelFormat == MTLPixelFormatBGRA8Unorm, "pixelFormat %d", (int)layer.pixelFormat);
  CHECK(layer.framebufferOnly, "framebufferOnly");
  CHECK(layer.drawableSize.width == 64 && layer.drawableSize.height == 48, "drawableSize %gx%g",
        layer.drawableSize.width, layer.drawableSize.height);
  CHECK(layer.displaySyncEnabled, "displaySyncEnabled with swap interval 1");
  CHECK(layer.maximumDrawableCount == (NSUInteger)rsp_mtl_drawables(1), "maximumDrawableCount %d",
        (int)layer.maximumDrawableCount);
  CHECK(rsp_mtl_drawables(1) == 3 && rsp_mtl_drawables(0) == 2, "drawable counts 3 (sync) / 2");

  rsp_mtl_make_current(g_m);
  CHECK(rsp_mtl_cgl(g_m) != NULL, "rs_gfx_cgl token");
  CHECK((void *)CGLGetCurrentContext() == rsp_mtl_cgl(g_m), "CGLGetCurrentContext is the token");

  // The lock is recursive and excludes other threads.
  rsp_mtl_lock(g_m);
  rsp_mtl_lock(g_m);
  pthread_t t;
  pthread_create(&t, NULL, locker, NULL);
  usleep(50 * 1000);
  CHECK(!atomic_load(&g_locked), "another thread waits for the lock");
  rsp_mtl_unlock(g_m);
  usleep(20 * 1000);
  CHECK(!atomic_load(&g_locked), "still held once");
  rsp_mtl_unlock(g_m);
  pthread_join(t, NULL);
  CHECK(atomic_load(&g_locked), "lock released");

  // Frames, as drawView makes them: lock, current, clear, present, unlock.
  int w, h;
  rsp_mtl_size(g_m, &w, &h);
  CHECK(w == 64 && h == 48, "size %dx%d", w, h);
  double worst = 0, t0 = now_s();
  int bad_px = 0;
  for (int i = 0; i < 30; i++) {
    @autoreleasepool {
      rsp_mtl_lock(g_m);
      unsigned char px[4] = {0};
      float c = (i & 1) ? 1.0f : 0.0f;
      double d = frame(c, 0.5f, 1.0f - c, w, h, px);
      if (d > worst) worst = d;
      int er = (i & 1) ? 255 : 0;
      if (abs(px[0] - er) > 1 || abs(px[1] - 128) > 1 || abs(px[2] - (255 - er)) > 1 || px[3] != 255) bad_px++;
      rsp_mtl_unlock(g_m);
    }
  }
  double total = now_s() - t0;
  CHECK(glGetError() == GL_NO_ERROR, "no GL errors");
  CHECK(bad_px == 0, "cleared colour read back in %d of 30 frames", 30 - bad_px);
  // nextDrawable times out after 1 s when no drawable comes back; frames in flight are capped at 2.
  CHECK(worst < 0.5, "slowest present %.3f s", worst);
  CHECK(total < 5.0, "30 frames in %.3f s", total);

  // Swap interval 0 (RS_FPS, no_vsync): applied at the next present.
  rsp_mtl_set_swap_interval(g_m, 0);
  CHECK(layer.displaySyncEnabled, "not before the next present");
  frame(0, 0, 0, w, h, NULL);
  CHECK(!layer.displaySyncEnabled, "displaySyncEnabled off");
  CHECK(layer.maximumDrawableCount == (NSUInteger)rsp_mtl_drawables(0), "maximumDrawableCount %d",
        (int)layer.maximumDrawableCount);
  for (int i = 0; i < 10; i++) {
    @autoreleasepool {
      frame(0, 0, 0, w, h, NULL);
    }
  }
  rsp_mtl_set_swap_interval(g_m, 1);
  frame(0, 0, 0, w, h, NULL);
  CHECK(layer.displaySyncEnabled && layer.maximumDrawableCount == (NSUInteger)rsp_mtl_drawables(1),
        "back to sync");

  // Resize (reshape): drawable and default framebuffer follow.
  rsp_mtl_resize(g_m, 128, 96);
  rsp_mtl_size(g_m, &w, &h);
  CHECK(w == 128 && h == 96, "size after resize %dx%d", w, h);
  CHECK(layer.drawableSize.width == 128 && layer.drawableSize.height == 96, "drawableSize %gx%g",
        layer.drawableSize.width, layer.drawableSize.height);
  unsigned char px[4] = {0};
  rsp_mtl_lock(g_m);
  frame(1, 1, 1, w, h, px);
  rsp_mtl_unlock(g_m);
  CHECK(px[0] == 255 && px[1] == 255 && px[2] == 255, "clear after resize %d %d %d", px[0], px[1], px[2]);
  CHECK(glGetError() == GL_NO_ERROR, "no GL errors after resize");

  // Colours: the layer is tagged sRGB (RS_METAL_COLORS=vivid leaves it untagged).
  CGColorSpaceRef cs = layer.colorspace;
  CFStringRef csn = cs ? CGColorSpaceCopyName(cs) : NULL;
  CHECK(csn && CFEqual(csn, kCGColorSpaceSRGB), "layer colorspace sRGB");
  if (csn) CFRelease(csn);

  // Upscaling: a larger output size makes the drawable that size while the framebuffer stays; frames present
  // (MetalFX where it runs, else bilinear) and read back as drawn; 0 x 0 goes back.
  void (*set_out)(void *, int, int);
  *(void **)&set_out = dlsym(RTLD_DEFAULT, "rs_gfx_set_output_size");
  CHECK(set_out != NULL, "rs_gfx_set_output_size exported");
  if (set_out) {
    rsp_mtl_lock(g_m);
    set_out(rsp_mtl_context(g_m), 256, 192);
    CHECK(layer.drawableSize.width == 256 && layer.drawableSize.height == 192, "upscaled drawableSize %gx%g",
          layer.drawableSize.width, layer.drawableSize.height);
    for (int i = 0; i < 5; i++) frame(0, 1, 0, w, h, px);
    rsp_mtl_unlock(g_m);
    CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0, "frame while upscaling %d %d %d", px[0], px[1], px[2]);
    rsp_mtl_size(g_m, &w, &h);
    CHECK(w == 128 && h == 96, "framebuffer still %dx%d", w, h);
    // Downscaling (supersampling): a smaller output makes the drawable that size, and frames present the same way.
    rsp_mtl_lock(g_m);
    set_out(rsp_mtl_context(g_m), 64, 48);
    CHECK(layer.drawableSize.width == 64 && layer.drawableSize.height == 48, "downscaled drawableSize %gx%g",
          layer.drawableSize.width, layer.drawableSize.height);
    for (int i = 0; i < 5; i++) frame(0, 0, 1, w, h, px);
    rsp_mtl_unlock(g_m);
    CHECK(px[0] == 0 && px[1] == 0 && px[2] == 255, "frame while downscaling %d %d %d", px[0], px[1], px[2]);
    CHECK(glGetError() == GL_NO_ERROR, "no GL errors while downscaling");
    rsp_mtl_lock(g_m);
    set_out(rsp_mtl_context(g_m), 0, 0);
    rsp_mtl_unlock(g_m);
    CHECK(layer.drawableSize.width == 128 && layer.drawableSize.height == 96, "drawableSize back %gx%g",
          layer.drawableSize.width, layer.drawableSize.height);
  }

  // Safe load's warm-up: no disk cache outside an app bundle, so nothing to wait for.
  double p0 = now_s();
  CHECK(rsp_mtl_prepare(g_m, 5.0) == 0, "rs_gfx_prepare with nothing to warm");
  CHECK(now_s() - p0 < 1.0, "prepare with nothing to warm took %.3f s", now_s() - p0);
  rsp_mtl_hold_black(g_m);
  CHECK(layer.backgroundColor != NULL, "black behind the drawable");
  int pdone = -1, ptotal = -1;
  CHECK(rsp_mtl_prepare_progress(g_m, &pdone, &ptotal) == 0 && pdone == ptotal && pdone >= 0,
        "progress after prepare: not running, %d of %d", pdone, ptotal);

  // The progress line: a CATextLayer on the (unattached) layer, centred, replaced in place, removed.
  char buf[256];
  rsp_mtl_progress_text(buf, sizeof buf, 120, 4081);
  CHECK(!strncmp(buf, "Preparing shaders\xe2\x80\xa6 120 of 4081\n", 31), "progress text: %s", buf);
  rsp_mtl_progress_text(buf, sizeof buf, 5, 92);
  CHECK(!strcmp(buf, "Preparing shaders\xe2\x80\xa6 5 of 92"), "short progress text: %s", buf);
  rsp_mtl_progress_text(buf, sizeof buf, 0, 0);
  CHECK(!strcmp(buf, "Preparing shaders\xe2\x80\xa6"), "progress text without a total: %s", buf);
  layer.bounds = CGRectMake(0, 0, 640, 360);
  NSUInteger before = layer.sublayers.count;
  CHECK(rsp_mtl_status_layer(g_m) == NULL, "no status line at first");
  rsp_mtl_status_text(g_m, "Preparing shaders\xe2\x80\xa6 1 of 2");
  CATextLayer *st = (CATextLayer *)rsp_mtl_status_layer(g_m);
  CHECK(st && st.superlayer == layer && layer.sublayers.count == before + 1, "status line added");
  CHECK(st && [st.string isKindOfClass:[NSString class]] && [(NSString *)st.string hasSuffix:@"1 of 2"], "status text");
  CHECK(st && fabs(CGRectGetMidX(st.frame) - 320) < 1 && fabs(CGRectGetMidY(st.frame) - 180) < 1, "status line centred");
  rsp_mtl_status_text(g_m, "Preparing shaders\xe2\x80\xa6 2 of 2");
  CHECK(rsp_mtl_status_layer(g_m) == st && layer.sublayers.count == before + 1 && [(NSString *)st.string hasSuffix:@"2 of 2"],
        "status line updated in place");
  rsp_mtl_status_text(g_m, NULL);
  CHECK(rsp_mtl_status_layer(g_m) == NULL && layer.sublayers.count == before, "status line removed");

  // The shell's path: prepare on a background queue, the completion on the main queue, no status
  // line left behind (shown at once here; prepare has nothing to do, so it may never appear).
  __block int called = 0, rc_seen = -9;
  rsp_mtl_prepare_async(g_m, 5.0, 0.0, ^(int rc) {
    called = 1;
    rc_seen = rc;
    CHECK([NSThread isMainThread], "completion on the main thread");
  });
  double a0 = now_s();
  while (!called && now_s() - a0 < 10)
    [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  CHECK(called && rc_seen == 0, "prepare_async completed (rc %d)", rc_seen);
  CHECK(rsp_mtl_status_layer(g_m) == NULL && layer.sublayers.count == before, "no status line after the completion");
  return 0;
}

// What the screen gets, read back from the drawables (RSPTapLayer): the frame as drawn, scaled up (MetalFX, or
// bilinear) and scaled down (supersampling), its rows in their places each time. Scaled down 2:1 a pixel is the mean of
// its four in linear light, so one-pixel black and white columns give 186, where a mean of the stored values gives 128.
static void test_presented(void) {
  enum { FW = 128, FH = 96 };
  RSPTapLayer *layer = [RSPTapLayer layer];
  layer.device = MTLCreateSystemDefaultDevice();
  [layer.device release];
  rsp_mtl *m = rsp_mtl_create(layer, FW, FH, 1);
  CHECK(m != NULL, "a context on the test layer");
  if (!m) return;
  CHECK(!layer.framebufferOnly, "the test layer's drawables can be read back");
  void (*set_out)(void *, int, int);
  void (*set_switch)(const char *, const char *);
  *(void **)&set_out = dlsym(RTLD_DEFAULT, "rs_gfx_set_output_size");
  *(void **)&set_switch = dlsym(RTLD_DEFAULT, "rs_gfx_set_switch");
  CHECK(set_out && set_switch, "rs_gfx_set_output_size and rs_gfx_set_switch exported");
  if (!set_out || !set_switch) return;
  set_switch("RS_METAL_SHARPEN", "");  // the pixels as scaled, not sharpened
  static unsigned char px[256 * 192 * 4];
  static const struct {
    int w, h;
    const char *what;
  } runs[] = {{0, 0, "as drawn"}, {256, 192, "scaled up"}, {64, 48, "scaled down"}};
  for (int r = 0; r < 3; r++) {
    const char *what = runs[r].what;
    int dw = runs[r].w ? runs[r].w : FW, dh = runs[r].h ? runs[r].h : FH;
    rsp_mtl_lock(m);
    rsp_mtl_make_current(m);
    set_out(rsp_mtl_context(m), runs[r].w, runs[r].h);
    for (int i = 0; i < 3; i++) {
      @autoreleasepool {
        draw_pattern(FW, FH);
        rsp_mtl_present(m);
      }
    }
    int ok = read_presented(layer, dw, dh, px);
    CHECK(glGetError() == GL_NO_ERROR, "%s: no GL errors", what);
    rsp_mtl_unlock(m);
    CHECK(ok, "%s: a %dx%d drawable read back", what, dw, dh);
    if (!ok) continue;
    const unsigned char *top = px + ((size_t)(dh / 8) * dw + dw * 3 / 4) * 4;  // B, G, R, A
    const unsigned char *bottom = px + ((size_t)(dh * 7 / 8) * dw + dw * 3 / 4) * 4;
    CHECK(top[2] > 250 && top[1] < 5 && top[0] < 5, "%s: red at the top, %d %d %d", what, top[2], top[1], top[0]);
    CHECK(bottom[2] < 5 && bottom[1] > 250 && bottom[0] < 5, "%s: green at the bottom, %d %d %d", what, bottom[2],
          bottom[1], bottom[0]);
    const unsigned char *a = px + ((size_t)(dh / 2) * dw + dw / 8) * 4, *b = a + 4;  // left, an even x and the next
    if (r == 0)
      CHECK(a[0] == 255 && a[1] == 255 && a[2] == 255 && b[0] == 0 && b[1] == 0 && b[2] == 0,
            "as drawn: a white column beside a black one, %d and %d", a[0], b[0]);
    if (r == 2) {
      int even = 1;
      for (int k = 0; k < 3; k++) even = even && abs(a[k] - 186) <= 3 && abs(b[k] - 186) <= 3;
      CHECK(even, "scaled down: the columns' mean in linear light, 186: %d %d %d, %d %d %d", a[2], a[1], a[0], b[2],
            b[1], b[0]);
    }
  }
  rsp_mtl_lock(m);
  set_out(rsp_mtl_context(m), 0, 0);
  rsp_mtl_unlock(m);
}

// The cache.psarc check (rsp_psarc_check / rsp_mtl_safe_load) on files made here.
static void test_safe_load(void) {
  char dir[] = "/tmp/rsp-present-test.XXXXXX";
  if (!mkdtemp(dir)) {
    CHECK(0, "mkdtemp");
    return;
  }
  char path[256], link[256], other[256], root[256], cmd[1024];
  snprintf(path, sizeof path, "%s/data.bin", dir);
  snprintf(other, sizeof other, "%s/other.bin", dir);
  snprintf(root, sizeof root, "%s/game", dir);
  snprintf(link, sizeof link, "%s/cache.psarc", root);
  FILE *f = fopen(path, "w");
  for (int i = 0; i < 300000; i++) fputc((i * 7) & 0xff, f);
  fclose(f);
  // The same size, one byte different.
  snprintf(cmd, sizeof cmd, "cp '%s' '%s' && printf Z | dd of='%s' bs=1 seek=1000 conv=notrunc 2>/dev/null",
           path, other, other);
  CHECK(system(cmd) == 0, "copy");
  char md5[64] = "";
  snprintf(cmd, sizeof cmd, "md5 -q '%s'", path);
  FILE *p = popen(cmd, "r");
  if (p) {
    if (fgets(md5, sizeof md5, p)) md5[strcspn(md5, "\n")] = 0;
    pclose(p);
  }
  CHECK(strlen(md5) == 32, "md5 of the test file: %s", md5);
  CHECK(rsp_psarc_check(path, 300000, md5) == 0, "matching size and MD5: original");
  CHECK(rsp_psarc_check(path, 300001, md5) == 1, "another size: modded");
  CHECK(rsp_psarc_check(other, 300000, md5) == 1, "same size, other content: modded");
  CHECK(rsp_psarc_check(link, 300000, md5) == -1, "missing file");
  mkdir(root, 0755);
  CHECK(symlink(path, link) == 0, "symlink");
  CHECK(rsp_psarc_check(link, 300000, md5) == 0, "a symlink is followed");
  // The game's check: the test file is not the original cache.psarc; the override wins.
  unsetenv("RS_METAL_SAFE_LOAD");
  CHECK(rsp_mtl_safe_load(root) == 1, "modded cache.psarc: safe load on");
  snprintf(cmd, sizeof cmd, "%s/", root);
  CHECK(rsp_mtl_safe_load(cmd) == 1, "root with a trailing slash");
  setenv("RS_METAL_SAFE_LOAD", "0", 1);
  CHECK(rsp_mtl_safe_load(root) == 0, "RS_METAL_SAFE_LOAD=0 forces it off");
  setenv("RS_METAL_SAFE_LOAD", "1", 1);
  CHECK(rsp_mtl_safe_load(dir) == 1, "RS_METAL_SAFE_LOAD=1 forces it on (no cache.psarc)");
  unsetenv("RS_METAL_SAFE_LOAD");
  CHECK(rsp_mtl_safe_load(dir) == 0, "no cache.psarc: off");
  // A file of the original's size that is not the original: hashed, modded.
  CHECK(truncate(other, RSP_CACHE_PSARC_SIZE) == 0, "truncate");
  unlink(link);
  CHECK(symlink(other, link) == 0, "symlink 2");
  CHECK(rsp_mtl_safe_load(root) == 1, "original size, other content: on");
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
  system(cmd);
}

int main(void) {
  @autoreleasepool {
    if (rsp_mtl_loaded_backend() == 1) {  // libRSOpenGL's backend: RS_GL_BACKEND, default metal
      test_metal();
      test_presented();
    } else {
      test_gl();
    }
    test_safe_load();
  }
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail != 0;
}
