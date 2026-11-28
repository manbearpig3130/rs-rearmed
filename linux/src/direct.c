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
uint64_t rc_img_lo, rc_img_hi;  // the mapped image (rc_rt.h: rc_in_image)

void rsl_direct_image(uint64_t lo, uint64_t hi) { rc_img_lo = lo, rc_img_hi = hi; }

// The game's allocator, which the fixes use for objects the game frees: the C++ library's operator new and
// delete. The Mac's sources name them as Mach-O writes them, with one more underscore than ELF has.
void *_Znwm(size_t size);
void _ZdlPv(void *p);
void *__Znwm(size_t size) { return _Znwm(size); }
void __ZdlPv(void *p) { _ZdlPv(p); }

// The .sng signature check as the game has it, for the fixes' override of it (overrides/mods/mods.c: sng_verify),
// which lets an unsigned custom song through for the song loader alone and runs the check itself for every other
// caller. On the Mac that is the crypto module's native verifier; here it is the game's own function, run from
// the context through its moved first instructions (patch.c).
void (*rsl_sng_verify_original)(rc_ctx *);
void rs_crypto_dsa_verify(rc_ctx *c) { rsl_sng_verify_original(c); }

// The Mac shell's answer to whether the renderer was chosen by the environment (rsp_renderer.h): here it always
// is, as there is one, and the fixes' settings menu shows its renderer row locked.
int rsp_renderer_env_given(void) { return 1; }

// A call with the argument registers loaded: rdi the function, rsi six integer arguments, rdx eight SSE ones (64
// bits each: a float in the low half), rcx where rax, rdx, xmm0 and xmm1 go.
void rsl_call_with(uint64_t fn, const uint64_t gi[6], const uint64_t dv[8], uint64_t out[4]);
__asm__(".text\n"
        ".globl rsl_call_with\n"
        ".type rsl_call_with,@function\n"
        "rsl_call_with:\n"
        "  .cfi_startproc\n"
        "  pushq %rbx\n"
        "  .cfi_adjust_cfa_offset 8\n"
        "  .cfi_offset %rbx, -16\n"
        "  movq %rcx, %rbx\n"
        "  movq %rdi, %r11\n"
        "  movq %rsi, %r10\n"
        "  movq 0(%rdx), %xmm0\n"
        "  movq 8(%rdx), %xmm1\n"
        "  movq 16(%rdx), %xmm2\n"
        "  movq 24(%rdx), %xmm3\n"
        "  movq 32(%rdx), %xmm4\n"
        "  movq 40(%rdx), %xmm5\n"
        "  movq 48(%rdx), %xmm6\n"
        "  movq 56(%rdx), %xmm7\n"
        "  movq 0(%r10), %rdi\n"
        "  movq 8(%r10), %rsi\n"
        "  movq 16(%r10), %rdx\n"
        "  movq 24(%r10), %rcx\n"
        "  movq 32(%r10), %r8\n"
        "  movq 40(%r10), %r9\n"
        "  movl $8, %eax\n"
        "  callq *%r11\n"
        "  movq %rax, 0(%rbx)\n"
        "  movq %rdx, 8(%rbx)\n"
        "  movq %xmm0, 16(%rbx)\n"
        "  movq %xmm1, 24(%rbx)\n"
        "  popq %rbx\n"
        "  .cfi_adjust_cfa_offset -8\n"
        "  .cfi_restore %rbx\n"
        "  retq\n"
        "  .cfi_endproc\n"
        ".size rsl_call_with, .-rsl_call_with\n");

// Integer arguments ints[0..5] in the argument registers and dbls[0..7] in the SSE ones; out as rc_rt.h says.
// More than six integer arguments (the rest go on the stack) is not written yet: no caller has them.
void rc_call_guest_ex(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t out[4]) {
  if (nint > 6 || ndbl > 8) {
    fprintf(stderr, "[rs-rearmed] a call into the game with %d integer and %d SSE arguments is not written for this build\n", nint, ndbl);
    abort();
  }
  uint64_t gi[6] = {0, 0, 0, 0, 0, 0}, dv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < nint; i++) gi[i] = ints[i];
  for (int i = 0; i < ndbl; i++) dv[i] = dbls[i];
  rsl_call_with(fn, gi, dv, out);
}

uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0_out) {
  uint64_t out[4];
  rc_call_guest_ex(fn, ints, nint, dbls, ndbl, out);
  if (xmm0_out) *xmm0_out = out[2];
  return out[0];
}

// A virtual call on an object, the game's or a native one: both hold functions the CPU runs.
uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs) {
  const uint64_t *vtable = *(const uint64_t *const *)(uintptr_t)obj;
  return rc_call_guest(vtable[slot], args, nargs, NULL, 0, NULL);
}

// Where the game's code is translated these make one side's functions callable by the other. Here both already are.
void ov_native_vtable(void *obj) { (void)obj; }
void *ov_fn(uint64_t p) { return (void *)(uintptr_t)p; }
void ov_alias_add(uint64_t vmaddr, void *native) { (void)vmaddr, (void)native; }
void rc_native_register_sret(const void *fn) { (void)fn; }

// --- mid points and edits: not in this build yet ---
// A mid point is a hook before one instruction inside a function, and an edit other instructions in the place of
// some (rc_mid.h). Both are made by the translator where the game is translated; here they need the instruction
// at the point moved, with the registers handed over whole, which is the next step after the hook points. Until
// then no point is here: rc_mid_add_name says so once for each name and answers -1, as it does on a game that
// has no such point, and the code that asked goes on without it.
#include "rc_mid.h"

uint8_t rc_edit_on[RC_EDIT_GROUPS];
uint8_t rc_mid_on[RC_MID_POINTS];

int rc_mid_add_name(const char *name, rc_mid_fn fn, void *user) {
  (void)fn, (void)user;
  fprintf(stderr, "[rs-rearmed] mid point %s: not in this build yet\n", name ? name : "(null)");
  return -1;
}
int rc_mid_is_none(int point) { return (void)point, 0; }
int rc_mid_value(int point, const char *value) { return (void)point, (void)value, -1; }
int rc_mid_cont(int point, const char *cont) { return (void)point, (void)cont, -1; }
uint64_t rc_mid_get(const rc_mid_frame *f, int value) { return (void)f, (void)value, 0; }
void rc_mid_set(rc_mid_frame *f, int value, uint64_t v) { (void)f, (void)value, (void)v; }
double rc_mid_get_f(const rc_mid_frame *f, int value) { return (void)f, (void)value, 0; }
void rc_mid_set_f(rc_mid_frame *f, int value, double v) { (void)f, (void)value, (void)v; }
int rc_edit_enable(const char *group) {
  fprintf(stderr, "[rs-rearmed] edits %s: not in this build yet\n", group ? group : "(null)");
  return -1;
}
int rc_edit_is_on(const char *group) { return (void)group, 0; }

