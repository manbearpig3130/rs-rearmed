// The Linux shell: a window, its OpenGL context, the frame loop and input, on SDL 3. It stands where the Mac's
// Cocoa shell does (shims/platform/src/rsp_shell.m), against the same C half (rsp_platform.h) and through the same
// seam to the engine (tools/ws7/recomp/app/seam_platform.c), so what the engine is told is what the Mac app tells it:
// macOS key codes and Cocoa's characters, pointer positions from the bottom left, one frame a refresh.
//
// So far: one thread makes the context, starts the game and runs its frames; windowed only.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dw.h"
#include "rsl.h"
#include "rsp_platform.h"
#include "seam.h"

// What the shell's C half asks of the Mac's Metal session at the end of a run (rsp_renderer.h): the mark that the
// session ended well, and the shader cache's last write. OpenGL has neither.
void rsp_metal_session_end(void) {}
void rsp_metal_cache_flush(void) {}

// --- keys: SDL's scancodes (the key's place) as macOS virtual key codes, which the game's key table is indexed by ---
static int mac_keycode(SDL_Scancode sc) {
  static const struct {
    SDL_Scancode sc;
    int mac;
  } k[] = {
      {SDL_SCANCODE_A, 0x00}, {SDL_SCANCODE_S, 0x01}, {SDL_SCANCODE_D, 0x02}, {SDL_SCANCODE_F, 0x03},
      {SDL_SCANCODE_H, 0x04}, {SDL_SCANCODE_G, 0x05}, {SDL_SCANCODE_Z, 0x06}, {SDL_SCANCODE_X, 0x07},
      {SDL_SCANCODE_C, 0x08}, {SDL_SCANCODE_V, 0x09}, {SDL_SCANCODE_B, 0x0b}, {SDL_SCANCODE_Q, 0x0c},
      {SDL_SCANCODE_W, 0x0d}, {SDL_SCANCODE_E, 0x0e}, {SDL_SCANCODE_R, 0x0f}, {SDL_SCANCODE_Y, 0x10},
      {SDL_SCANCODE_T, 0x11}, {SDL_SCANCODE_1, 0x12}, {SDL_SCANCODE_2, 0x13}, {SDL_SCANCODE_3, 0x14},
      {SDL_SCANCODE_4, 0x15}, {SDL_SCANCODE_6, 0x16}, {SDL_SCANCODE_5, 0x17}, {SDL_SCANCODE_EQUALS, 0x18},
      {SDL_SCANCODE_9, 0x19}, {SDL_SCANCODE_7, 0x1a}, {SDL_SCANCODE_MINUS, 0x1b}, {SDL_SCANCODE_8, 0x1c},
      {SDL_SCANCODE_0, 0x1d}, {SDL_SCANCODE_RIGHTBRACKET, 0x1e}, {SDL_SCANCODE_O, 0x1f}, {SDL_SCANCODE_U, 0x20},
      {SDL_SCANCODE_LEFTBRACKET, 0x21}, {SDL_SCANCODE_I, 0x22}, {SDL_SCANCODE_P, 0x23}, {SDL_SCANCODE_RETURN, 0x24},
      {SDL_SCANCODE_L, 0x25}, {SDL_SCANCODE_J, 0x26}, {SDL_SCANCODE_APOSTROPHE, 0x27}, {SDL_SCANCODE_K, 0x28},
      {SDL_SCANCODE_SEMICOLON, 0x29}, {SDL_SCANCODE_BACKSLASH, 0x2a}, {SDL_SCANCODE_COMMA, 0x2b},
      {SDL_SCANCODE_SLASH, 0x2c}, {SDL_SCANCODE_N, 0x2d}, {SDL_SCANCODE_M, 0x2e}, {SDL_SCANCODE_PERIOD, 0x2f},
      {SDL_SCANCODE_TAB, 0x30}, {SDL_SCANCODE_SPACE, 0x31}, {SDL_SCANCODE_GRAVE, 0x32}, {SDL_SCANCODE_BACKSPACE, 0x33},
      {SDL_SCANCODE_ESCAPE, 0x35}, {SDL_SCANCODE_KP_PERIOD, 0x41}, {SDL_SCANCODE_KP_MULTIPLY, 0x43},
      {SDL_SCANCODE_KP_PLUS, 0x45}, {SDL_SCANCODE_KP_DIVIDE, 0x4b}, {SDL_SCANCODE_KP_ENTER, 0x4c},
      {SDL_SCANCODE_KP_MINUS, 0x4e}, {SDL_SCANCODE_KP_EQUALS, 0x51}, {SDL_SCANCODE_KP_0, 0x52},
      {SDL_SCANCODE_KP_1, 0x53}, {SDL_SCANCODE_KP_2, 0x54}, {SDL_SCANCODE_KP_3, 0x55}, {SDL_SCANCODE_KP_4, 0x56},
      {SDL_SCANCODE_KP_5, 0x57}, {SDL_SCANCODE_KP_6, 0x58}, {SDL_SCANCODE_KP_7, 0x59}, {SDL_SCANCODE_KP_8, 0x5b},
      {SDL_SCANCODE_KP_9, 0x5c}, {SDL_SCANCODE_F5, 0x60}, {SDL_SCANCODE_F6, 0x61}, {SDL_SCANCODE_F7, 0x62},
      {SDL_SCANCODE_F3, 0x63}, {SDL_SCANCODE_F8, 0x64}, {SDL_SCANCODE_F9, 0x65}, {SDL_SCANCODE_F11, 0x67},
      {SDL_SCANCODE_F10, 0x6d}, {SDL_SCANCODE_F12, 0x6f}, {SDL_SCANCODE_HOME, 0x73}, {SDL_SCANCODE_PAGEUP, 0x74},
      {SDL_SCANCODE_DELETE, 0x75}, {SDL_SCANCODE_F4, 0x76}, {SDL_SCANCODE_END, 0x77}, {SDL_SCANCODE_F2, 0x78},
      {SDL_SCANCODE_PAGEDOWN, 0x79}, {SDL_SCANCODE_F1, 0x7a}, {SDL_SCANCODE_LEFT, 0x7b}, {SDL_SCANCODE_RIGHT, 0x7c},
      {SDL_SCANCODE_DOWN, 0x7d}, {SDL_SCANCODE_UP, 0x7e},
  };
  for (size_t i = 0; i < sizeof k / sizeof *k; i++)
    if (k[i].sc == sc) return k[i].mac;
  return -1;
}

// The character Cocoa reports for a key (NSEvent's characters): what was typed, or for a key that types nothing
// one of Cocoa's own code points (the arrows and function keys from 0xF700).
static int cocoa_character(SDL_Scancode sc, SDL_Keymod mod) {
  switch (sc) {
    case SDL_SCANCODE_UP: return 0xf700;
    case SDL_SCANCODE_DOWN: return 0xf701;
    case SDL_SCANCODE_LEFT: return 0xf702;
    case SDL_SCANCODE_RIGHT: return 0xf703;
    case SDL_SCANCODE_DELETE: return 0xf728;
    case SDL_SCANCODE_HOME: return 0xf729;
    case SDL_SCANCODE_END: return 0xf72b;
    case SDL_SCANCODE_PAGEUP: return 0xf72c;
    case SDL_SCANCODE_PAGEDOWN: return 0xf72d;
    case SDL_SCANCODE_RETURN: return 0x0d;
    case SDL_SCANCODE_KP_ENTER: return 0x03;
    case SDL_SCANCODE_BACKSPACE: return 0x7f;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_ESCAPE: return 0x1b;
    default: break;
  }
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 0xf704 + (int)(sc - SDL_SCANCODE_F1);
  SDL_Keycode key = SDL_GetKeyFromScancode(sc, mod, false);
  return key >= 0x20 && key < 0x10000 && !(key & SDLK_SCANCODE_MASK) ? (int)key : 0;
}

// --- a frame as a picture, to see what was drawn where no one is looking ---
static void save_picture(SDL_Window *window, const char *path) {
  void (*read_pixels)(int, int, int, int, unsigned, unsigned, void *) = (void (*)(int, int, int, int, unsigned, unsigned, void *))SDL_GL_GetProcAddress("glReadPixels");
  void (*bind_framebuffer)(unsigned, unsigned) = (void (*)(unsigned, unsigned))SDL_GL_GetProcAddress("glBindFramebuffer");
  void (*get_integer)(unsigned, int *) = (void (*)(unsigned, int *))SDL_GL_GetProcAddress("glGetIntegerv");
  void (*pixel_store)(unsigned, int) = (void (*)(unsigned, int))SDL_GL_GetProcAddress("glPixelStorei");
  int w = 0, h = 0, bound = 0, pack = 4, pbo = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  unsigned char *rgb = w > 0 && h > 0 ? malloc((size_t)w * (size_t)h * 3) : NULL;
  FILE *f = rgb ? fopen(path, "wb") : NULL;
  if (!read_pixels || !bind_framebuffer || !get_integer || !pixel_store || !f) {
    fprintf(stderr, "[rs-rearmed] the frame could not be saved to %s\n", path);
    if (f) fclose(f);
    free(rgb);
    return;
  }
  get_integer(0x8caa, &bound);  // GL_READ_FRAMEBUFFER_BINDING
  get_integer(0x0d05, &pack);   // GL_PACK_ALIGNMENT
  get_integer(0x88ed, &pbo);    // GL_PIXEL_PACK_BUFFER_BINDING
  void (*bind_buffer)(unsigned, unsigned) = (void (*)(unsigned, unsigned))SDL_GL_GetProcAddress("glBindBuffer");
  if (pbo && bind_buffer) bind_buffer(0x88eb, 0);
  bind_framebuffer(0x8ca8, 0);  // the window's own
  pixel_store(0x0d05, 1);
  read_pixels(0, 0, w, h, 0x1907 /* GL_RGB */, 0x1401 /* GL_UNSIGNED_BYTE */, rgb);
  pixel_store(0x0d05, pack);
  bind_framebuffer(0x8ca8, (unsigned)bound);
  if (pbo && bind_buffer) bind_buffer(0x88eb, (unsigned)pbo);
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; y--) fwrite(rgb + (size_t)y * (size_t)w * 3, 3, (size_t)w, f);  // OpenGL's rows start at the bottom
  fclose(f);
  free(rgb);
  fprintf(stderr, "[rs-rearmed] frame saved: %s (%d x %d)\n", path, w, h);
}

// The display's modes, for the renderer's list: each size once.
static void tell_display_modes(SDL_Window *window) {
  int n = 0, count = 0, modes[64][2];
  SDL_DisplayMode **list = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window), &n);
  for (int i = 0; list && i < n && count < 64; i++) {
    int seen = 0;
    for (int k = 0; k < count; k++) seen |= modes[k][0] == list[i]->w && modes[k][1] == list[i]->h;
    if (!seen && list[i]->w >= 800 && list[i]->h >= 600) modes[count][0] = list[i]->w, modes[count++][1] = list[i]->h;
  }
  SDL_free(list);
  if (count) dw_display_set_modes(modes, count);
}

int rsl_shell_run(const rsl_shell_options *opt) {
  // One way to an OpenGL context on X11 and Wayland alike: EGL, which the renderer's library asks for the
  // current context (linux/gl/system_gl.c).
  SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
  SDL_SetHint(SDL_HINT_APP_ID, "rs-rearmed");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "[rs-rearmed] no display: %s\n", SDL_GetError());
    return 1;
  }
  // Apple's legacy context is a compatibility one: the game's stream has the alpha test and fixed-function calls.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  int w = opt->width > 0 ? opt->width : 1280, h = opt->height > 0 ? opt->height : 720;
  SDL_Window *window = SDL_CreateWindow("Rocksmith 2014", w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
  if (!context || !SDL_GL_MakeCurrent(window, context)) {
    fprintf(stderr, "[rs-rearmed] no window with an OpenGL compatibility context: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetWindowMinimumSize(window, 800, 600);
  SDL_GL_SetSwapInterval(1);
  tell_display_modes(window);

  // The engine behind the hooks, and its `NSOpenGLContext`: the context as the renderer's library knows it.
  rsp_set_hooks(rcapp_seam_hooks());
  void *(*current_context)(void) = (void *(*)(void))rsl_shim_function("OpenGL", "CGLGetCurrentContext");
  if (rsp_hooks()->gl_context && current_context) rsp_hooks()->gl_context(rsp_hooks()->ctx, current_context());

  rsl_survey(opt->survey);
  rsp_boot boot = {opt->game_dir, opt->game_dir, 0, NULL};
  if (rsp_game_bootstrap(&boot)) {
    fprintf(stderr, "[rs-rearmed] the game did not start\n");
    return 1;
  }
  rsp_set_focus(1);
  rsp_set_display_mode(0, w, h);

  int done = 0;
  long frame = 0;
  Uint64 last = SDL_GetTicksNS();
  while (!done) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
          rsp_request_quit();
          break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: rsp_set_focus(1); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: rsp_set_focus(0); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          w = e.window.data1, h = e.window.data2;
          rsp_set_display_mode(rsp_is_fullscreen(), w, h);
          break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
          int code = mac_keycode(e.key.scancode);
          if (code < 0) break;
          int ch = cocoa_character(e.key.scancode, e.key.mod);
          if (e.type == SDL_EVENT_KEY_DOWN)
            rsp_post_input(RSP_EV_KEYDOWN, code, (int)rsp_pack_keydown_char((uint16_t)ch, e.key.mod & SDL_KMOD_GUI ? RSP_MOD_COMMAND : 0));
          else
            rsp_post_input(RSP_EV_KEYUP, code, ch);
          break;
        }
        case SDL_EVENT_MOUSE_MOTION:
          rsp_post_input(RSP_EV_MOUSEMOVE, rsp_mouse_engine_coord(e.motion.x), rsp_mouse_engine_coord((double)h - e.motion.y));
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
            rsp_post_input(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? RSP_EV_MOUSEDOWN : RSP_EV_MOUSEUP,
                           e.button.button == SDL_BUTTON_RIGHT, 0);
          break;
        case SDL_EVENT_MOUSE_WHEEL: {
          int up;
          if (rsp_scroll_event(e.wheel.y, &up)) rsp_post_input(RSP_EV_SCROLL, up, 1);
          break;
        }
        default: break;
      }
    }
    done = rsp_game_frame();
    frame++;
    if (opt->picture && opt->picture_every > 0 && frame % opt->picture_every == 0) {
      char path[1024];
      snprintf(path, sizeof path, "%s.%06ld.ppm", opt->picture, frame);
      save_picture(window, path);
    } else if (opt->picture && !opt->picture_every && frame == opt->picture_frame) {
      save_picture(window, opt->picture);
    }
    SDL_GL_SwapWindow(window);
    // One frame a refresh. Where the swap does not wait for one (no display, a compositor that does not hold
    // it), the loop keeps to 60 a second itself: the game's clock is the real one.
    Uint64 now = SDL_GetTicksNS();
    if (now - last < 15000000ull) SDL_DelayNS(16666667ull - (now - last));
    last = SDL_GetTicksNS();
    if (opt->frames > 0 && frame >= opt->frames) break;
  }
  rsl_survey(0);
  fprintf(stderr, "[rs-rearmed] %ld frames%s\n", frame, done ? "; the engine has shut down" : "");
  if (opt->survey) rsl_report_survey(stderr);
  return 0;
}
