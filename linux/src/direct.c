// The recompiler runtime's functions where nothing is translated (linux/X86-64.md).
//
// The app's native code that touches the game (the seam, the override modules, later the fixes) is written against
// the runtime's headers: tools/ws7/recomp/runtime's rc.h, rc_abi.h, rc_rt.h, and overrides/rc_ov.h. Those compile
// here as they are. What they mean is simpler: the image is at its own address, so the slide is zero and an
// image address is the address; the game's registers are the CPU's, so calling a game function is a call, and a
// native object or function is one the game can call too.
#include <stdio.h>
#include <stdlib.h>

#include "rc.h"
#include "rc_ov.h"
#include "rc_rt.h"

uint64_t rc_slide;  // 0: the image is where its addresses say

static uint64_t call6(uint64_t fn, const uint64_t *ints, int nint) {
  uint64_t a[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < nint && i < 6; i++) a[i] = ints[i];
  return ((uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t))(uintptr_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
}

// Integer arguments ints[0..5] in the argument registers; returns rax. More than six, or any in the SSE registers,
// is not written yet: no caller has them.
uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0_out) {
  (void)dbls;
  if (nint > 6 || ndbl > 0 || xmm0_out) {
    fprintf(stderr, "[rs-rearmed] rc_call_guest with %d integer and %d SSE arguments is not written for this build\n", nint, ndbl);
    abort();
  }
  return call6(fn, ints, nint);
}

// A virtual call on an object, the game's or a native one: both hold functions the CPU runs.
uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs) {
  if (nargs > 6) {
    fprintf(stderr, "[rs-rearmed] a virtual call with %d arguments is not written for this build\n", nargs);
    abort();
  }
  const uint64_t *vtable = *(const uint64_t *const *)(uintptr_t)obj;
  return call6(vtable[slot], args, nargs);
}

// Where the game's code is translated these make one side's functions callable by the other. Here both already are.
void ov_native_vtable(void *obj) { (void)obj; }
void *ov_fn(uint64_t p) { return (void *)(uintptr_t)p; }
void ov_alias_add(uint64_t vmaddr, void *native) { (void)vmaddr, (void)native; }
void rc_native_register_sret(const void *fn) { (void)fn; }
