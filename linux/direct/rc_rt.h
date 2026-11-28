// Calling the game from native code (tools/ws7/recomp/runtime/rc_rt.h) where nothing is translated: a call.
#ifndef RC_DIRECT_RC_RT_H
#define RC_DIRECT_RC_RT_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Integer arguments ints[0..5] in the argument registers; returns rax. More than six, or any in the SSE
// registers, is not written yet: no caller has them.
static inline uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl,
                                     uint64_t *xmm0_out) {
  (void)dbls;
  if (nint > 6 || ndbl > 0 || xmm0_out) {
    fprintf(stderr, "[rs-rearmed] rc_call_guest with %d integer and %d SSE arguments is not written for this build\n", nint, ndbl);
    abort();
  }
  uint64_t a[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < nint; i++) a[i] = ints[i];
  return ((uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))(uintptr_t)fn)(a[0], a[1], a[2], a[3],
                                                                                                    a[4], a[5]);
}

#endif
