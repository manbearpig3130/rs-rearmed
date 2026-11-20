// The Metal layer's switches (not in the original; the installer's and Adv Display Settings'), in both editions:
//
//   RS_METAL_DRAWABLES=2|3  the layer's drawables, else 3 with vsync (Apple's default: a late frame does not block
//                           nextDrawable for a whole refresh) and 2 without (RS_FPS: presents are not held for the
//                           display, so a third only adds latency). 2 with vsync is low-latency presentation: a
//                           frame is on the screen a refresh sooner, and one that is late shows as a repeated one.
//   RS_METAL_HUD=1          Apple's Metal Performance HUD over the game: its frame rate, frame times and GPU
//                           memory. On macOS 13 and later the HUD is enabled before Metal starts (MTL_HUD_ENABLED,
//                           rsp_layer_env, first in each main) and the layer's developerHUDProperties show or hide
//                           it (rsp_layer_show_hud), so it can be turned on and off as the game runs; before 13,
//                           only MTL_HUD_ENABLED, with RS_METAL_HUD=1.
//
// Each edition's presenter applies them to its CAMetalLayer: the Mac's src/rsp_present_metal.m, Learn & Play's
// win-x86/app/lp_window.m.
#ifndef RSP_LAYER_H
#define RSP_LAYER_H

#include <stdlib.h>
#include <string.h>

// The drawables a value of RS_METAL_DRAWABLES asks for: 2 or 3, else 0 (the default's).
static inline int rsp_layer_drawables_value(const char *e) {
  if (!e || !*e) return 0;
  char *end;
  long n = strtol(e, &end, 10);
  return !*end && (n == 2 || n == 3) ? (int)n : 0;
}

// The layer's drawables for presents held for the display (sync) or not.
static inline int rsp_layer_drawables(int sync) {
  static int forced = -1;
  if (forced < 0) forced = rsp_layer_drawables_value(getenv("RS_METAL_DRAWABLES"));
  return forced ? forced : sync ? 3 : 2;
}

// RS_METAL_HUD: on unless unset, empty, or starting with 0, n, N, f or F (as the launchers read toggles).
static inline int rsp_layer_hud(void) {
  static int on = -1;
  if (on < 0) {
    const char *e = getenv("RS_METAL_HUD");
    on = e && *e && !strchr("0nNfF", *e);
  }
  return on;
}

// First in main, before anything starts Metal: the HUD's own switch (for the layer to show it or not, from 13 on).
static inline void rsp_layer_env(void) {
  if (__builtin_available(macOS 13.0, *)) setenv("MTL_HUD_ENABLED", "1", 1);
  else if (rsp_layer_hud()) setenv("MTL_HUD_ENABLED", "1", 1);
}

#ifdef __OBJC__
#import <QuartzCore/CAMetalLayer.h>

// The HUD on LAYER shown, or hidden.
static inline void rsp_layer_show_hud(CAMetalLayer *layer, int on) {
  if (@available(macOS 13.0, *))
    layer.developerHUDProperties = @{@"mode" : on ? @"default" : @"disabled", @"logging" : @"disabled"};
}
#endif

#endif
