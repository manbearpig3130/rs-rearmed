// The display's modes, which the renderer lists at every start (CoreGraphics' Quartz Display Services), and
// IOKit's registry, which one function asks for the Ethernet interface.
//
// There is one display, the one the game's window is on. Its modes are what the shell says they are
// (dw_display_set_modes); until it has said, the common sizes up to 1920x1080.
#define _GNU_SOURCE
#include <string.h>

#include "dw.h"
#include "dw_cf.h"

#define MAX_MODES 64
static int g_modes[MAX_MODES][2] = {{1024, 768},  {1280, 720},  {1280, 800},  {1366, 768}, {1440, 900},
                                    {1600, 900},  {1680, 1050}, {1920, 1080}};
static int g_nmodes = 8;
static const char k_mode;

typedef struct {
  char header[16];  // CoreFoundation's, which CFRelease reads
  long width, height;
} display_mode;

void dw_display_set_modes(const int (*modes)[2], int n) {
  if (n > MAX_MODES) n = MAX_MODES;
  memcpy(g_modes, modes, (size_t)n * sizeof *modes);
  g_nmodes = n;
}

unsigned dw_CGMainDisplayID(void) { return 1; }
DW_FUNCTION("_CGMainDisplayID", dw_CGMainDisplayID);

// The bit of the display in a pixel format's display mask: the first.
unsigned dw_CGDisplayIDToOpenGLDisplayMask(unsigned display) {
  (void)display;
  return 1;
}
DW_FUNCTION("_CGDisplayIDToOpenGLDisplayMask", dw_CGDisplayIDToOpenGLDisplayMask);

void *dw_CGDisplayCopyAllDisplayModes(unsigned display, const void *options) {
  (void)display, (void)options;
  void *items[MAX_MODES];
  for (int i = 0; i < g_nmodes; i++) {
    display_mode *m = dw_cf_object(&k_mode, sizeof *m);
    m->width = g_modes[i][0], m->height = g_modes[i][1];
    items[i] = m;
  }
  return dw_cf_array(items, g_nmodes);
}
DW_FUNCTION("_CGDisplayCopyAllDisplayModes", dw_CGDisplayCopyAllDisplayModes);

size_t dw_CGDisplayModeGetWidth(const void *mode) { return dw_cf_is(mode, &k_mode) ? (size_t)((const display_mode *)mode)->width : 0; }
DW_FUNCTION("_CGDisplayModeGetWidth", dw_CGDisplayModeGetWidth);

size_t dw_CGDisplayModeGetHeight(const void *mode) { return dw_cf_is(mode, &k_mode) ? (size_t)((const display_mode *)mode)->height : 0; }
DW_FUNCTION("_CGDisplayModeGetHeight", dw_CGDisplayModeGetHeight);

// Every mode is 32 bits a pixel: IOKit's name for that layout (IO32BitDirectPixels), which the renderer looks for.
void *dw_CGDisplayModeCopyPixelEncoding(const void *mode) {
  (void)mode;
  return dw_cf_string("--------RRRRRRRRGGGGGGGGBBBBBBBB");
}
DW_FUNCTION("_CGDisplayModeCopyPixelEncoding", dw_CGDisplayModeCopyPixelEncoding);

// --- IOKit's registry ---
// One function of the game's reads the built-in Ethernet interface's address there. It is answered as a machine
// with no such interface: the dictionary to match by is made, and the search finds nothing.
unsigned dw_kIOMasterPortDefault;
DW_DATA("_kIOMasterPortDefault", dw_kIOMasterPortDefault);

void *dw_IOServiceMatching(const char *name) {
  (void)name;
  static const char k_matching;
  return dw_cf_object(&k_matching, 16);
}
DW_FUNCTION("_IOServiceMatching", dw_IOServiceMatching);

void dw_CFRelease(void *object);

int dw_IOServiceGetMatchingServices(unsigned port, void *matching, unsigned *iterator) {
  (void)port;
  dw_CFRelease(matching);  // the call consumes the dictionary
  if (iterator) *iterator = 0;
  return 0;
}
DW_FUNCTION("_IOServiceGetMatchingServices", dw_IOServiceGetMatchingServices);

unsigned dw_IOIteratorNext(unsigned iterator) {
  (void)iterator;
  return 0;
}
DW_FUNCTION("_IOIteratorNext", dw_IOIteratorNext);

int dw_IOObjectRelease(unsigned object) {
  (void)object;
  return 0;
}
DW_FUNCTION("_IOObjectRelease", dw_IOObjectRelease);
