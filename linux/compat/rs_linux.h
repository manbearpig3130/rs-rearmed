// Included first in every file of the project's shared code built for Linux (-include): the two places where it
// names a macOS library to load, or expects a library's names to be the whole process's, and Darwin's clock call.
//
//   dlopen("/System/Library/Frameworks/OpenGL.framework/...")   the system's OpenGL: here libRSSystemGL.so,
//       beside the library that asks (linux/gl/system_gl.c: Mesa, with CGL's calls over EGL).
//   dlsym(RTLD_DEFAULT, "rs_gfx_backend") and the like          libRSOpenGL's helpers, which on the Mac every
//       library sees; here libraries are loaded each to itself, so libRSOpenGL.so is asked as well.
#ifndef RS_COMPAT_RS_LINUX_H
#define RS_COMPAT_RS_LINUX_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <dlfcn.h>

#ifdef __cplusplus
extern "C" {
#endif
void *rs_compat_dlopen(const char *path, int flags);
void *rs_compat_dlsym(void *handle, const char *name);
#ifdef __cplusplus
}
#endif

#define dlopen rs_compat_dlopen
#define dlsym rs_compat_dlsym

// Darwin's clock by number of nanoseconds, and its clock that does not count sleep.
#include <stdint.h>
#include <time.h>
#ifndef CLOCK_UPTIME_RAW
#define CLOCK_UPTIME_RAW CLOCK_MONOTONIC
#endif
static inline uint64_t clock_gettime_nsec_np(clockid_t clock) {
  struct timespec t;
  clock_gettime(clock, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

#endif
