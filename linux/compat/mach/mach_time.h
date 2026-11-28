// Mach's clock for the project's shared code on Linux: nanoseconds of the monotonic clock, timebase 1/1.
#ifndef RS_COMPAT_MACH_MACH_TIME_H
#define RS_COMPAT_MACH_MACH_TIME_H

#include <stdint.h>
#include <time.h>

typedef struct mach_timebase_info {
  uint32_t numer, denom;
} mach_timebase_info_data_t, *mach_timebase_info_t;

static inline int mach_timebase_info(mach_timebase_info_t info) {
  info->numer = info->denom = 1;
  return 0;
}

static inline uint64_t mach_absolute_time(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

#endif
