// A colour grade over the whole picture, for mods (Linux): the frame as shown, every pixel looked up in a colour cube
// a mod gives, after the game and the overlay have drawn it. It is what a Core Image colour cube filter on the
// window's layer does on the Mac (rearmed-mods' Sunlight), which the overlay API (rs_overlay.h) has no call for: its
// OpenGL code keeps to the calls the game itself makes, so that the Mac's Metal renderer covers it, and a 3D texture
// is not among them. This is the Linux shell's own, on Mesa, and touches nothing of the Mac's.
//
//   int rs_picture_grade(const float *cube, int size);
//
// cube: size x size x size RGBA floats, 0 to 1, red fastest, then green, then blue (CIColorCube's layout), in the
// values the game writes (sRGB, not linear light); NULL takes the grade away. size 2 to 64. Any thread; the cube is
// copied, and the frame after the call is the first with it. 0, or -1 for a size out of range.
//
// The shell calls rsl_picture_grade_run after the overlay, before a test's picture is saved and the frame is shown:
// the frame is copied to a texture of its own and drawn back through the cube, with the cube's corners at the
// colours' ends (the lookup at c * (n - 1) / n + 0.5 / n). The game's state it touches is read first and written
// back after, and frames without a grade touch none.
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsl.h"

// --- the OpenGL calls, by name: the shell links no OpenGL headers ---
typedef unsigned GLenum, GLuint;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

enum {
  GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_3D = 0x806F, GL_TEXTURE0 = 0x84C0, GL_ACTIVE_TEXTURE = 0x84E0,
  GL_TEXTURE_BINDING_2D = 0x8069, GL_TEXTURE_BINDING_3D = 0x806A, GL_TEXTURE_MIN_FILTER = 0x2801,
  GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803, GL_TEXTURE_WRAP_R = 0x8072,
  GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812F, GL_RGBA = 0x1908, GL_RGBA8 = 0x8058,
  GL_RGBA16F = 0x881A, GL_FLOAT = 0x1406, GL_UNSIGNED_BYTE = 0x1401, GL_FRAMEBUFFER = 0x8D40,
  GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9, GL_READ_FRAMEBUFFER_BINDING = 0x8CAA,
  GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6, GL_READ_BUFFER = 0x0C02, GL_DRAW_BUFFER = 0x0C01, GL_BACK = 0x0405,
  GL_CURRENT_PROGRAM = 0x8B8D, GL_VERTEX_ARRAY_BINDING = 0x85B5, GL_ARRAY_BUFFER = 0x8892,
  GL_ARRAY_BUFFER_BINDING = 0x8894, GL_STATIC_DRAW = 0x88E4, GL_PIXEL_UNPACK_BUFFER = 0x88EC,
  GL_PIXEL_UNPACK_BUFFER_BINDING = 0x88EF, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_UNPACK_ROW_LENGTH = 0x0CF2,
  GL_UNPACK_IMAGE_HEIGHT = 0x806E, GL_VIEWPORT = 0x0BA2, GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71,
  GL_SCISSOR_TEST = 0x0C11, GL_CULL_FACE = 0x0B44, GL_STENCIL_TEST = 0x0B90, GL_ALPHA_TEST = 0x0BC0,
  GL_FRAMEBUFFER_SRGB = 0x8DB9, GL_COLOR_WRITEMASK = 0x0C23, GL_POLYGON_MODE = 0x0B40, GL_FRONT_AND_BACK = 0x0408,
  GL_FILL = 0x1B02, GL_TRIANGLES = 0x0004, GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30,
  GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_TRUE = 1, GL_FALSE = 0,
};

#define GL_CALLS(X)                                                                                               \
  X(void, glGetIntegerv, (GLenum, GLint *))                                                                       \
  X(void, glGetBooleanv, (GLenum, GLboolean *))                                                                   \
  X(void, glEnable, (GLenum))                                                                                     \
  X(void, glDisable, (GLenum))                                                                                    \
  X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                              \
  X(void, glPolygonMode, (GLenum, GLenum))                                                                        \
  X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                                                           \
  X(void, glActiveTexture, (GLenum))                                                                              \
  X(void, glGenTextures, (GLsizei, GLuint *))                                                                     \
  X(void, glBindTexture, (GLenum, GLuint))                                                                        \
  X(void, glTexParameteri, (GLenum, GLenum, GLint))                                                               \
  X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))            \
  X(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))   \
  X(void, glCopyTexSubImage2D, (GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei))                     \
  X(void, glPixelStorei, (GLenum, GLint))                                                                         \
  X(void, glBindFramebuffer, (GLenum, GLuint))                                                                    \
  X(void, glReadBuffer, (GLenum))                                                                                 \
  X(void, glDrawBuffer, (GLenum))                                                                                 \
  X(void, glGenVertexArrays, (GLsizei, GLuint *))                                                                 \
  X(void, glBindVertexArray, (GLuint))                                                                            \
  X(void, glGenBuffers, (GLsizei, GLuint *))                                                                      \
  X(void, glBindBuffer, (GLenum, GLuint))                                                                         \
  X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                               \
  X(void, glEnableVertexAttribArray, (GLuint))                                                                    \
  X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))                       \
  X(void, glDrawArrays, (GLenum, GLint, GLsizei))                                                                 \
  X(GLuint, glCreateShader, (GLenum))                                                                             \
  X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                                \
  X(void, glCompileShader, (GLuint))                                                                              \
  X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                                                               \
  X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                             \
  X(void, glDeleteShader, (GLuint))                                                                               \
  X(GLuint, glCreateProgram, (void))                                                                              \
  X(void, glAttachShader, (GLuint, GLuint))                                                                       \
  X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))                                                 \
  X(void, glLinkProgram, (GLuint))                                                                                \
  X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                                                              \
  X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                            \
  X(void, glUseProgram, (GLuint))                                                                                 \
  X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                                                        \
  X(void, glUniform1i, (GLint, GLint))                                                                            \
  X(void, glUniform1f, (GLint, GLfloat))

#define DECLARE(ret, name, args) static ret(*p_##name) args;
GL_CALLS(DECLARE)

// --- what a mod gives, from any thread ---
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static float *g_pending;    // a cube to upload (g_pending_size), or NULL
static int g_pending_size;  // 0 with g_pending NULL: take the grade away
static int g_pending_new;   // a change since the frame last looked
static int g_wanted;        // a grade is on (as the frame should be drawn)

int rs_picture_grade(const float *cube, int size) {
  if (cube && (size < 2 || size > 64)) return -1;
  float *copy = NULL;
  if (cube) {
    size_t n = (size_t)size * size * size * 4;
    copy = malloc(n * sizeof *copy);
    if (!copy) return -1;
    memcpy(copy, cube, n * sizeof *copy);
  }
  pthread_mutex_lock(&g_lock);
  free(g_pending);
  g_pending = copy, g_pending_size = cube ? size : 0, g_pending_new = 1, g_wanted = cube != NULL;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

// --- on the frame's thread, with the context current ---
static int g_ready = -1;  // the calls and the program: -1 not tried, 0 not to be had, 1 there
static GLuint g_program, g_vao, g_vbo, g_frame_tex, g_cube_tex;
static GLint g_u_scale, g_u_offset;
static int g_frame_w, g_frame_h, g_cube_size;
#define UNIT_FRAME 14  // texture units of the pass's own, so the game's are left as they are
#define UNIT_CUBE 15

static const char *k_vertex =
    "#version 120\n"
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *k_fragment =
    "#version 120\n"
    "uniform sampler2D u_frame;\n"
    "uniform sampler3D u_cube;\n"
    "uniform float u_scale, u_offset;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec3 c = texture2D(u_frame, v_uv).rgb;\n"
    "  gl_FragColor = vec4(texture3D(u_cube, c * u_scale + u_offset).rgb, 1.0);\n"
    "}\n";

static GLuint shader(GLenum kind, const char *src) {
  GLuint s = p_glCreateShader(kind);
  GLint ok = 0;
  p_glShaderSource(s, 1, &src, NULL);
  p_glCompileShader(s);
  p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = "";
    p_glGetShaderInfoLog(s, sizeof log, NULL, log);
    fprintf(stderr, "[rs-rearmed] picture grade: a shader did not compile: %s\n", log);
    p_glDeleteShader(s);
    return 0;
  }
  return s;
}

static int load_calls(void) {
#define LOAD(ret, name, args) p_##name = (ret(*) args)SDL_GL_GetProcAddress(#name);
  GL_CALLS(LOAD)
#define CHECK(ret, name, args) \
  if (!p_##name) {             \
    fprintf(stderr, "[rs-rearmed] picture grade: no %s in this OpenGL: no grade\n", #name); \
    return 0;                  \
  }
  GL_CALLS(CHECK)
  return 1;
}

// The program, the triangle and the two textures (with the game's state saved: this binds them)
static int set_up(void) {
  GLuint vs = shader(GL_VERTEX_SHADER, k_vertex), fs = shader(GL_FRAGMENT_SHADER, k_fragment);
  if (!vs || !fs) return 0;
  g_program = p_glCreateProgram();
  p_glAttachShader(g_program, vs);
  p_glAttachShader(g_program, fs);
  p_glBindAttribLocation(g_program, 0, "a_pos");
  p_glLinkProgram(g_program);
  p_glDeleteShader(vs);
  p_glDeleteShader(fs);
  GLint ok = 0;
  p_glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512] = "";
    p_glGetProgramInfoLog(g_program, sizeof log, NULL, log);
    fprintf(stderr, "[rs-rearmed] picture grade: the program did not link: %s\n", log);
    return 0;
  }
  p_glUseProgram(g_program);
  p_glUniform1i(p_glGetUniformLocation(g_program, "u_frame"), UNIT_FRAME);
  p_glUniform1i(p_glGetUniformLocation(g_program, "u_cube"), UNIT_CUBE);
  g_u_scale = p_glGetUniformLocation(g_program, "u_scale");
  g_u_offset = p_glGetUniformLocation(g_program, "u_offset");
  // one triangle over the whole frame: its corners at (-1, -1), (3, -1), (-1, 3)
  static const GLfloat tri[6] = {-1, -1, 3, -1, -1, 3};
  p_glGenVertexArrays(1, &g_vao);
  p_glBindVertexArray(g_vao);
  p_glGenBuffers(1, &g_vbo);
  p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  p_glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
  p_glEnableVertexAttribArray(0);
  p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  p_glGenTextures(1, &g_frame_tex);
  p_glGenTextures(1, &g_cube_tex);
  return 1;
}

// The game's state the pass touches
typedef struct {
  GLint draw_fb, read_fb, read_buf, draw_buf, program, vao, array_buf, active, unit_frame, unit_cube, unpack_buf;
  GLint unpack_align, unpack_row, unpack_height, viewport[4], polygon[2];
  GLboolean blend, depth, scissor, cull, stencil, alpha, srgb, mask[4];
} saved;

static void save(saved *s) {
  p_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s->draw_fb);
  p_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s->read_fb);
  p_glGetIntegerv(GL_CURRENT_PROGRAM, &s->program);
  p_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
  p_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->array_buf);
  p_glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active);
  p_glActiveTexture(GL_TEXTURE0 + UNIT_FRAME);
  p_glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->unit_frame);
  p_glActiveTexture(GL_TEXTURE0 + UNIT_CUBE);
  p_glGetIntegerv(GL_TEXTURE_BINDING_3D, &s->unit_cube);
  p_glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s->unpack_buf);
  p_glGetIntegerv(GL_UNPACK_ALIGNMENT, &s->unpack_align);
  p_glGetIntegerv(GL_UNPACK_ROW_LENGTH, &s->unpack_row);
  p_glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &s->unpack_height);
  p_glGetIntegerv(GL_VIEWPORT, s->viewport);
  p_glGetIntegerv(GL_POLYGON_MODE, s->polygon);
  p_glGetBooleanv(GL_BLEND, &s->blend);
  p_glGetBooleanv(GL_DEPTH_TEST, &s->depth);
  p_glGetBooleanv(GL_SCISSOR_TEST, &s->scissor);
  p_glGetBooleanv(GL_CULL_FACE, &s->cull);
  p_glGetBooleanv(GL_STENCIL_TEST, &s->stencil);
  p_glGetBooleanv(GL_ALPHA_TEST, &s->alpha);  // fixed-function, but it applies to a GLSL program's output too
  p_glGetBooleanv(GL_FRAMEBUFFER_SRGB, &s->srgb);
  p_glGetBooleanv(GL_COLOR_WRITEMASK, s->mask);
  // the window's own read and draw buffers, which belong to framebuffer 0
  p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  p_glGetIntegerv(GL_READ_BUFFER, &s->read_buf);
  p_glGetIntegerv(GL_DRAW_BUFFER, &s->draw_buf);
}

static void enable(GLenum cap, GLboolean on) { (on ? p_glEnable : p_glDisable)(cap); }

static void restore(const saved *s) {
  p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  p_glReadBuffer((GLenum)s->read_buf);
  p_glDrawBuffer((GLenum)s->draw_buf);
  p_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)s->read_fb);
  p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)s->draw_fb);
  p_glUseProgram((GLuint)s->program);
  p_glBindVertexArray((GLuint)s->vao);
  p_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s->array_buf);
  p_glActiveTexture(GL_TEXTURE0 + UNIT_FRAME);
  p_glBindTexture(GL_TEXTURE_2D, (GLuint)s->unit_frame);
  p_glActiveTexture(GL_TEXTURE0 + UNIT_CUBE);
  p_glBindTexture(GL_TEXTURE_3D, (GLuint)s->unit_cube);
  p_glActiveTexture((GLenum)s->active);
  p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)s->unpack_buf);
  p_glPixelStorei(GL_UNPACK_ALIGNMENT, s->unpack_align);
  p_glPixelStorei(GL_UNPACK_ROW_LENGTH, s->unpack_row);
  p_glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, s->unpack_height);
  p_glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
  p_glPolygonMode(GL_FRONT_AND_BACK, (GLenum)s->polygon[0]);
  enable(GL_BLEND, s->blend);
  enable(GL_DEPTH_TEST, s->depth);
  enable(GL_SCISSOR_TEST, s->scissor);
  enable(GL_CULL_FACE, s->cull);
  enable(GL_STENCIL_TEST, s->stencil);
  enable(GL_ALPHA_TEST, s->alpha);
  enable(GL_FRAMEBUFFER_SRGB, s->srgb);
  p_glColorMask(s->mask[0], s->mask[1], s->mask[2], s->mask[3]);
}

void rsl_picture_grade_run(int width, int height) {
  pthread_mutex_lock(&g_lock);
  int wanted = g_wanted, changed = g_pending_new;
  float *cube = g_pending;
  int size = g_pending_size;
  g_pending = NULL, g_pending_new = 0;
  pthread_mutex_unlock(&g_lock);
  if (!changed && (!wanted || !g_cube_size)) return;  // no grade: the frame is left alone
  if (g_ready < 0 && !load_calls()) g_ready = 0;
  if (!g_ready || width <= 0 || height <= 0) {
    free(cube);
    return;
  }
  saved s;
  save(&s);
  if (g_ready < 0) {
    g_ready = set_up();
    fprintf(stderr, "[rs-rearmed] picture grade: %s\n", g_ready ? "ready" : "not to be had: the frame is left alone");
    if (!g_ready) {
      restore(&s);
      free(cube);
      return;
    }
  }
  p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  p_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  p_glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
  if (changed) {
    if (cube) {
      p_glActiveTexture(GL_TEXTURE0 + UNIT_CUBE);
      p_glBindTexture(GL_TEXTURE_3D, g_cube_tex);
      p_glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      p_glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      p_glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      p_glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      p_glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
      p_glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, size, size, size, 0, GL_RGBA, GL_FLOAT, cube);
      g_cube_size = size;
    } else {
      g_cube_size = 0;
    }
    free(cube);
  }
  if (g_cube_size) {
    p_glActiveTexture(GL_TEXTURE0 + UNIT_FRAME);
    p_glBindTexture(GL_TEXTURE_2D, g_frame_tex);
    if (width != g_frame_w || height != g_frame_h) {
      p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      g_frame_w = width, g_frame_h = height;
    }
    // framebuffer 0 is bound (save): the frame as drawn, from its back buffer, then drawn back through the cube
    p_glReadBuffer(GL_BACK);
    p_glDrawBuffer(GL_BACK);
    p_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    p_glActiveTexture(GL_TEXTURE0 + UNIT_CUBE);
    p_glBindTexture(GL_TEXTURE_3D, g_cube_tex);
    p_glUseProgram(g_program);
    p_glUniform1f(g_u_scale, (float)(g_cube_size - 1) / (float)g_cube_size);
    p_glUniform1f(g_u_offset, 0.5f / (float)g_cube_size);
    p_glBindVertexArray(g_vao);
    p_glViewport(0, 0, width, height);
    p_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    p_glDisable(GL_BLEND);
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_CULL_FACE);
    p_glDisable(GL_STENCIL_TEST);
    p_glDisable(GL_ALPHA_TEST);
    p_glDisable(GL_FRAMEBUFFER_SRGB);
    p_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  restore(&s);
}
