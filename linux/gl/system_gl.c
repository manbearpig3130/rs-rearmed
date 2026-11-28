// libRSSystemGL.so: the system's OpenGL as libRSOpenGL's OpenGL backend expects it to be.
//
// On the Mac that backend is Apple's OpenGL.framework: libRSOpenGL (shims/opengl) loads it and looks up every
// function of its table there, gl* and CGL* alike. On Linux it loads this library in the framework's place
// (linux/compat/rs_linux.h). A name asked of this library is found here or in what it is linked with, which is
// Mesa (through libglvnd's libOpenGL). So this file has only what Mesa has not or says otherwise:
//
//   - CGL, macOS's contexts without a window, over EGL: an offscreen context is an EGL context on Mesa's
//     surfaceless display, and the current context is whatever EGL context is current, the shell's included.
//   - Apple's legacy identity. The game and Scaleform choose shader sets and paths by the version and the
//     extension list; every Mac the game runs on answers "2.1" with GLSL 1.20 and Apple's list, and a 4.6
//     compatibility context would send the game down paths no Mac ever ran. The list is the one the project's
//     Metal core answers (shims/opengl/src/metal/mtl_context.m), with the ARB assembly programs the Cg library
//     uses on OpenGL where the driver has them.
//   - Apple's names for vertex array objects, which are the core ones.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OpenGL/OpenGL.h>

#define EXPORT __attribute__((visibility("default")))

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

// --- Mesa's own functions, by name ---
// libglvnd's libOpenGL exports the core names; an older alias (glDrawArraysInstancedARB) comes from
// eglGetProcAddress, or is the function without the ending. Every name of libRSOpenGL's table is a forwarder
// that asks here once (gen_system_gl.py).
void *rs_system_gl_find(const char *name) {
  static void *lib;
  if (!lib && !(lib = dlopen("libOpenGL.so.0", RTLD_NOW | RTLD_LOCAL))) lib = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
  void *p = lib ? dlsym(lib, name) : NULL;
  if (!p) p = (void *)eglGetProcAddress(name);
  size_t n = strlen(name);
  if (!p && n > 3 && n < 120 && (!strcmp(name + n - 3, "ARB") || !strcmp(name + n - 3, "EXT"))) {
    char core[128];
    memcpy(core, name, n - 3);
    core[n - 3] = 0;
    if (!(p = lib ? dlsym(lib, core) : NULL)) p = (void *)eglGetProcAddress(core);
  }
  if (!p) {
    fprintf(stderr, "[rs-gl] the system's OpenGL has no %s\n", name);
    abort();
  }
  return p;
}
#define mesa rs_system_gl_find
#define MESA(type, name)           \
  static __typeof__(type) real;    \
  if (!real) real = (__typeof__(type))mesa(#name)

// --- CGL over EGL ---
struct _CGLPixelFormatObject {
  int core;  // kCGLPFAOpenGLProfile asked for a core profile; else Apple's legacy one: a compatibility context
};

struct _CGLContextObject {
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
  int foreign;  // not made here: the shell's, known by its EGL handle
  pthread_mutex_t lock;
  struct _CGLContextObject *group, *next;
};

static struct _CGLContextObject *g_contexts;
static pthread_mutex_t g_list = PTHREAD_MUTEX_INITIALIZER;
static __thread struct _CGLContextObject *t_current;

static EGLDisplay offscreen_display(void) {
  static EGLDisplay display = EGL_NO_DISPLAY;
  static int tried;
  pthread_mutex_lock(&g_list);
  if (!tried) {
    tried = 1;
    const char *ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (ext && strstr(ext, "EGL_MESA_platform_surfaceless"))
      display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (display == EGL_NO_DISPLAY) display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display != EGL_NO_DISPLAY && !eglInitialize(display, NULL, NULL)) display = EGL_NO_DISPLAY;
  }
  pthread_mutex_unlock(&g_list);
  return display;
}

static struct _CGLContextObject *new_context(EGLDisplay display, EGLContext context, EGLSurface surface, int foreign) {
  struct _CGLContextObject *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->display = display, c->context = context, c->surface = surface, c->foreign = foreign;
  c->group = c;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&c->lock, &attr);
  pthread_mutexattr_destroy(&attr);
  return c;
}

static void add_context(struct _CGLContextObject *c) {
  pthread_mutex_lock(&g_list);
  c->next = g_contexts;
  g_contexts = c;
  pthread_mutex_unlock(&g_list);
}

EXPORT CGLError CGLChoosePixelFormat(const CGLPixelFormatAttribute *attribs, CGLPixelFormatObj *pix, GLint *npix) {
  if (!pix) return kCGLBadAddress;
  struct _CGLPixelFormatObject *p = calloc(1, sizeof *p);
  if (!p) return kCGLBadAlloc;
  for (const CGLPixelFormatAttribute *a = attribs; a && *a; a++) {
    switch ((int)*a) {
      case kCGLPFAOpenGLProfile: p->core = (int)*++a != kCGLOGLPVersion_Legacy; break;
      case kCGLPFAColorSize: case kCGLPFAAlphaSize: case kCGLPFADepthSize: case kCGLPFAStencilSize:
      case kCGLPFASampleBuffers: case kCGLPFASamples:
        a++;  // each has a value; every context here has 8 bits a channel, 24 of depth and 8 of stencil
        break;
      default: break;
    }
  }
  *pix = p;
  if (npix) *npix = 1;
  return kCGLNoError;
}

EXPORT CGLError CGLDestroyPixelFormat(CGLPixelFormatObj pix) {
  free(pix);
  return kCGLNoError;
}

EXPORT CGLError CGLCreateContext(CGLPixelFormatObj pix, CGLContextObj share, CGLContextObj *ctx) {
  if (!pix || !ctx) return kCGLBadAddress;
  *ctx = NULL;
  EGLDisplay display = share ? share->display : offscreen_display();
  if (display == EGL_NO_DISPLAY) return kCGLBadDisplay;
  if (!eglBindAPI(EGL_OPENGL_API)) return kCGLBadDisplay;
  const EGLint want[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8,
                         EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
                         EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint n = 0;
  if (!eglChooseConfig(display, want, &config, 1, &n) || n < 1) return kCGLBadPixelFormat;
  const EGLint legacy[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT, EGL_NONE};
  const EGLint core[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, share ? share->context : EGL_NO_CONTEXT, pix->core ? core : legacy);
  if (context == EGL_NO_CONTEXT) return kCGLBadContext;
  const EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};  // what is drawn goes to framebuffer objects
  EGLSurface surface = eglCreatePbufferSurface(display, config, size);
  struct _CGLContextObject *c = new_context(display, context, surface, 0);
  if (!c || surface == EGL_NO_SURFACE) {
    eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    free(c);
    return kCGLBadAlloc;
  }
  if (share) c->group = share->group;
  add_context(c);
  *ctx = c;
  return kCGLNoError;
}

EXPORT CGLError CGLDestroyContext(CGLContextObj ctx) {
  if (!ctx) return kCGLBadContext;
  pthread_mutex_lock(&g_list);
  for (struct _CGLContextObject **p = &g_contexts; *p; p = &(*p)->next)
    if (*p == ctx) {
      *p = ctx->next;
      break;
    }
  pthread_mutex_unlock(&g_list);
  if (t_current == ctx) {
    eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    t_current = NULL;
  }
  if (!ctx->foreign) {
    eglDestroySurface(ctx->display, ctx->surface);
    eglDestroyContext(ctx->display, ctx->context);
  }
  pthread_mutex_destroy(&ctx->lock);
  free(ctx);
  return kCGLNoError;
}

EXPORT CGLError CGLSetCurrentContext(CGLContextObj ctx) {
  eglBindAPI(EGL_OPENGL_API);
  if (!ctx) {
    EGLDisplay display = eglGetCurrentDisplay();
    if (display != EGL_NO_DISPLAY) eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    t_current = NULL;
    return kCGLNoError;
  }
  if (!eglMakeCurrent(ctx->display, ctx->surface, ctx->surface, ctx->context)) return kCGLBadContext;
  t_current = ctx;
  return kCGLNoError;
}

// The context that is current, whoever made it so: one of ours, or the shell's, which gets an object here the
// first time it is seen.
EXPORT CGLContextObj CGLGetCurrentContext(void) {
  eglBindAPI(EGL_OPENGL_API);
  EGLContext context = eglGetCurrentContext();
  if (context == EGL_NO_CONTEXT) return t_current = NULL;
  if (t_current && t_current->context == context) return t_current;
  struct _CGLContextObject *c;
  pthread_mutex_lock(&g_list);
  for (c = g_contexts; c; c = c->next)
    if (c->context == context) break;
  pthread_mutex_unlock(&g_list);
  if (!c && (c = new_context(eglGetCurrentDisplay(), context, eglGetCurrentSurface(EGL_DRAW), 1))) add_context(c);
  return t_current = c;
}

EXPORT CGLShareGroupObj CGLGetShareGroup(CGLContextObj ctx) { return ctx ? (CGLShareGroupObj)ctx->group : NULL; }

// The shell says which of its contexts share objects: CONTEXT with WITH (their EGL handles).
EXPORT void rs_system_gl_share(void *context, void *with) {
  struct _CGLContextObject *a = NULL, *b = NULL;
  pthread_mutex_lock(&g_list);
  for (struct _CGLContextObject *c = g_contexts; c; c = c->next) {
    if (c->context == (EGLContext)context) a = c;
    if (c->context == (EGLContext)with) b = c;
  }
  if (a && b) a->group = b->group;
  pthread_mutex_unlock(&g_list);
}

EXPORT CGLError CGLLockContext(CGLContextObj ctx) {
  if (!ctx) return kCGLBadContext;
  pthread_mutex_lock(&ctx->lock);
  return kCGLNoError;
}

EXPORT CGLError CGLUnlockContext(CGLContextObj ctx) {
  if (!ctx) return kCGLBadContext;
  pthread_mutex_unlock(&ctx->lock);
  return kCGLNoError;
}

// The engine switches of Apple's driver (its multithreaded engine): nothing to switch here.
EXPORT CGLError CGLEnable(CGLContextObj ctx, CGLContextEnable pname) {
  (void)pname;
  return ctx ? kCGLNoError : kCGLBadContext;
}

EXPORT CGLError CGLDisable(CGLContextObj ctx, CGLContextEnable pname) {
  (void)pname;
  return ctx ? kCGLNoError : kCGLBadContext;
}

EXPORT const char *CGLErrorString(CGLError error) {
  switch (error) {
    case kCGLNoError: return "no error";
    case kCGLBadAttribute: return "invalid pixel format attribute";
    case kCGLBadPixelFormat: return "invalid pixel format";
    case kCGLBadContext: return "invalid context";
    case kCGLBadDisplay: return "invalid display";
    case kCGLBadAddress: return "invalid address";
    case kCGLBadAlloc: return "memory allocation failure";
    default: return "CGL error";
  }
}

// --- Apple's legacy identity ---
static const char *const k_extensions[] = {
    "GL_ARB_color_buffer_float", "GL_ARB_depth_buffer_float", "GL_ARB_depth_texture",
    "GL_ARB_draw_elements_base_vertex", "GL_ARB_draw_instanced", "GL_ARB_fragment_shader",
    "GL_ARB_framebuffer_object", "GL_ARB_half_float_pixel", "GL_ARB_half_float_vertex",
    "GL_ARB_instanced_arrays", "GL_ARB_multitexture", "GL_ARB_shader_objects",
    "GL_ARB_shading_language_100", "GL_ARB_texture_border_clamp", "GL_ARB_texture_compression",
    "GL_ARB_texture_cube_map", "GL_ARB_texture_float", "GL_ARB_texture_mirrored_repeat",
    "GL_ARB_texture_non_power_of_two", "GL_ARB_texture_rg", "GL_ARB_vertex_array_bgra",
    "GL_ARB_vertex_buffer_object", "GL_ARB_vertex_shader", "GL_EXT_abgr", "GL_EXT_bgra",
    "GL_EXT_blend_color", "GL_EXT_blend_equation_separate", "GL_EXT_blend_func_separate",
    "GL_EXT_blend_minmax", "GL_EXT_blend_subtract", "GL_EXT_framebuffer_object",
    "GL_EXT_packed_depth_stencil", "GL_EXT_stencil_two_side", "GL_EXT_stencil_wrap",
    "GL_EXT_texture_compression_dxt1", "GL_EXT_texture_compression_s3tc",
    "GL_EXT_texture_filter_anisotropic", "GL_EXT_vertex_array_bgra", "GL_APPLE_packed_pixels",
    "GL_APPLE_vertex_array_object", "GL_ATI_separate_stencil", "GL_SGIS_generate_mipmap",
    "GL_SGIS_texture_edge_clamp", "GL_SGIS_texture_lod",
};
// What Apple's OpenGL lists and the Metal core leaves out, which the Cg library runs its programs with on OpenGL.
static const char *const k_programs[] = {"GL_ARB_vertex_program", "GL_ARB_fragment_program", "GL_EXT_gpu_program_parameters"};

static char g_version[160], g_extensions[4096];
static int g_nextensions;

static int driver_has(const char *driver, const char *name) {
  size_t n = strlen(name);
  for (const char *p = driver; p && (p = strstr(p, name)); p += n)
    if ((p == driver || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1;
  return 0;
}

// With a context current. The driver's own extension list is read once, to see which of Apple's the driver has not
// (S3TC, where a driver is built without it) and whether it has the assembly programs.
static void identity(void) {
  static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&once);
  if (!g_version[0]) {
    MESA(const GLubyte *(*)(GLenum), glGetString);
    const char *version = (const char *)real(GL_VERSION), *driver = (const char *)real(GL_EXTENSIONS);
    size_t o = 0;
    for (size_t i = 0; i < sizeof k_extensions / sizeof *k_extensions; i++) {
      const char *e = k_extensions[i];
      // Apple's own two are answered by this library whatever the driver; the rest only where the driver has them
      // (a core context lists none through this call, and gets Apple's list whole).
      if (driver && *driver && strncmp(e, "GL_APPLE_", 9) && !driver_has(driver, e)) continue;
      o += (size_t)snprintf(g_extensions + o, sizeof g_extensions - o, "%s ", e), g_nextensions++;
    }
    for (size_t i = 0; i < sizeof k_programs / sizeof *k_programs; i++)
      if (driver && driver_has(driver, k_programs[i]))
        o += (size_t)snprintf(g_extensions + o, sizeof g_extensions - o, "%s ", k_programs[i]), g_nextensions++;
    snprintf(g_version, sizeof g_version, "2.1 RS Re-ARMed (%s)", version ? version : "?");
  }
  pthread_mutex_unlock(&once);
}

EXPORT const GLubyte *glGetString(GLenum name) {
  MESA(const GLubyte *(*)(GLenum), glGetString);
  if (!CGLGetCurrentContext()) return real(name);
  switch (name) {
    case GL_VENDOR: return (const GLubyte *)"Apple";
    case GL_VERSION: identity(); return (const GLubyte *)g_version;
    case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"1.20";
    case GL_EXTENSIONS: identity(); return (const GLubyte *)g_extensions;
  }
  return real(name);  // the renderer's name is the driver's own
}

// A legacy 2.1 context has no numbered version; the count of extensions is the list's above.
EXPORT void glGetIntegerv(GLenum pname, GLint *data) {
  MESA(void (*)(GLenum, GLint *), glGetIntegerv);
  if (pname == GL_NUM_EXTENSIONS && CGLGetCurrentContext()) {
    identity();
    *data = g_nextensions;
    return;
  }
  if (pname == GL_MAJOR_VERSION || pname == GL_MINOR_VERSION) return;  // GL_INVALID_ENUM there: nothing is written
  real(pname, data);
}

// --- Apple's names for vertex array objects ---
EXPORT void glBindVertexArrayAPPLE(GLuint array) {
  MESA(void (*)(GLuint), glBindVertexArray);
  real(array);
}

EXPORT void glGenVertexArraysAPPLE(GLsizei n, GLuint *arrays) {
  MESA(void (*)(GLsizei, GLuint *), glGenVertexArrays);
  real(n, arrays);
}

EXPORT void glDeleteVertexArraysAPPLE(GLsizei n, const GLuint *arrays) {
  MESA(void (*)(GLsizei, const GLuint *), glDeleteVertexArrays);
  real(n, arrays);
}
