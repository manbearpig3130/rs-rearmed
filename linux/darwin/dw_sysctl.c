// sysctl and sysctlbyname, for the fourteen names in the image and the one numbered question it asks (the
// engine's description of the machine, and Wwise's). Each is answered as a Mac with this machine's CPU answers:
// an Intel Mac's answers on x86-64, which are what the game's code was written against.
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dw.h"

// Darwin's rule for the result: with no buffer the size is told; a buffer too small gets what fits, and ENOMEM.
static int give(void *old, size_t *oldlen, const void *value, size_t size) {
  if (!oldlen) return 0;
  if (!old) {
    *oldlen = size;
    return 0;
  }
  if (*oldlen < size) {
    memcpy(old, value, *oldlen);
    errno = ENOMEM;
    return -1;
  }
  memcpy(old, value, size);
  *oldlen = size;
  return 0;
}

static int give_int(void *old, size_t *oldlen, int64_t v) {
  // an integer is given in the width asked for, 4 or 8 bytes, as the kernel does for its numbered ones
  if (old && oldlen && *oldlen == 4) {
    int32_t v32 = (int32_t)v;
    return give(old, oldlen, &v32, 4);
  }
  return give(old, oldlen, &v, 8);
}

static int64_t cpu_hz(void) {
  static int64_t hz;
  if (hz) return hz;
  long khz = 0;
  FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
  if (f) {
    if (fscanf(f, "%ld", &khz) != 1) khz = 0;
    fclose(f);
  }
  if (!khz && (f = fopen("/proc/cpuinfo", "r"))) {
    char line[256];
    double mhz;
    while (fgets(line, sizeof line, f))
      if (sscanf(line, "cpu MHz : %lf", &mhz) == 1) {
        khz = (long)(mhz * 1000);
        break;
      }
    fclose(f);
  }
  return hz = khz > 0 ? (int64_t)khz * 1000 : 2400000000ll;
}

static int has_cpu(const char *feature) {
#if defined(__x86_64__)
  __builtin_cpu_init();
  if (!strcmp(feature, "sse3")) return __builtin_cpu_supports("sse3");
  if (!strcmp(feature, "ssse3")) return __builtin_cpu_supports("ssse3");
  return 1;  // MMX, SSE and SSE2 are every x86-64 CPU's
#else
  (void)feature;
  return -1;  // the name does not exist on a Mac without the feature's family
#endif
}

static int by_name(const char *name, void *old, size_t *oldlen) {
  long n;
  if (!strcmp(name, "hw.memsize")) {
    int64_t v = (int64_t)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    return give(old, oldlen, &v, 8);
  }
  if (!strcmp(name, "hw.logicalcpu") || !strcmp(name, "hw.physicalcpu") || !strcmp(name, "hw.ncpu")) {
    int32_t v = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);
    return give(old, oldlen, &v, 4);
  }
  if (!strcmp(name, "hw.cachelinesize"))
    return give_int(old, oldlen, (n = sysconf(_SC_LEVEL1_DCACHE_LINESIZE)) > 0 ? n : 64);
  if (!strcmp(name, "hw.l2cachesize")) return give_int(old, oldlen, (n = sysconf(_SC_LEVEL2_CACHE_SIZE)) > 0 ? n : 262144);
  if (!strcmp(name, "hw.cpufrequency")) return give_int(old, oldlen, cpu_hz());
  if (!strcmp(name, "hw.machine")) {
#if defined(__x86_64__)
    return give(old, oldlen, "x86_64", 7);
#else
    return give(old, oldlen, "arm64", 6);
#endif
  }
  static const char *const k_features[][2] = {{"hw.optional.mmx", "mmx"}, {"hw.optional.sse", "sse"},
                                              {"hw.optional.sse2", "sse2"}, {"hw.optional.sse3", "sse3"},
                                              {"hw.optional.supplementalsse3", "ssse3"}};
  for (size_t i = 0; i < sizeof k_features / sizeof *k_features; i++)
    if (!strcmp(name, k_features[i][0])) {
      int32_t v = has_cpu(k_features[i][1]);
      if (v < 0) break;
      return give(old, oldlen, &v, 4);
    }
  // The game is the Mac game on every host: it is told Darwin, of a release that ran it.
  if (!strcmp(name, "kern.ostype")) return give(old, oldlen, "Darwin", 7);
  if (!strcmp(name, "kern.osrelease")) return give(old, oldlen, "24.6.0", 7);
  dw_log("sysctl %s: no such name here", name);
  errno = ENOENT;
  return -1;
}

int dw_sysctlbyname(const char *name, void *old, size_t *oldlen, void *new, size_t newlen) {
  (void)new, (void)newlen;  // nothing is ever set
  return by_name(name, old, oldlen);
}
DW_FUNCTION("_sysctlbyname", dw_sysctlbyname);

int dw_sysctl(const int *mib, unsigned n, void *old, size_t *oldlen, void *new, size_t newlen) {
  (void)new, (void)newlen;
  enum { CTL_KERN = 1, CTL_HW = 6 };
  if (n == 2 && mib[0] == CTL_HW) {
    switch (mib[1]) {
      case 1: return by_name("hw.machine", old, oldlen);        // HW_MACHINE
      case 3: return by_name("hw.ncpu", old, oldlen);           // HW_NCPU
      case 7: return give_int(old, oldlen, sysconf(_SC_PAGESIZE));  // HW_PAGESIZE
      case 15: return give_int(old, oldlen, cpu_hz());          // HW_CPU_FREQ: the engine's description of the machine
      case 24: return by_name("hw.memsize", old, oldlen);       // HW_MEMSIZE
      case 25: return by_name("hw.ncpu", old, oldlen);          // HW_AVAILCPU
    }
  }
  if (n == 2 && mib[0] == CTL_KERN && mib[1] == 1) return by_name("kern.ostype", old, oldlen);
  if (n == 2 && mib[0] == CTL_KERN && mib[1] == 2) return by_name("kern.osrelease", old, oldlen);
  dw_log("sysctl {%d, %d%s}: no such name here", n > 0 ? mib[0] : -1, n > 1 ? mib[1] : -1, n > 2 ? ", ..." : "");
  errno = ENOENT;
  return -1;
}
DW_FUNCTION("_sysctl", dw_sysctl);
