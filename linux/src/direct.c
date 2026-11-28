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

// The Mac shell's way to hand a changed switch on as the game runs (rsp_present.h), which is for its Metal
// presentation and renderer: 0, the switch is read as the process starts. Linux's own rows are that kind.
int rsp_live_switch(const char *name, const char *value) {
  (void)name, (void)value;
  return 0;
}

// The calling thread's context (rc.h). Where the game's code is translated it holds the thread's registers between
// calls; here the registers are the CPU's and nothing reads it. The fixes name it where they set a register for a
// call into Learn & Play's game, which this one is not.
rc_ctx *rc_thread_ctx(void) {
  static _Thread_local rc_ctx c;
  return &c;
}

// A call with the argument registers loaded: rdi the function, rsi six integer arguments, rdx eight SSE ones (64
// bits each: a float in the low half), rcx where rax, rdx, xmm0 and xmm1 go, r8 the arguments that go on the
// stack and r9 how many (8 bytes each, in argument order: the callee finds the first just above its return
// address). The stack is kept 16-byte aligned at the call, and al says eight SSE registers may be in use.
void rsl_call_with(uint64_t fn, const uint64_t gi[6], const uint64_t dv[8], uint64_t out[4], const uint64_t *stack,
                   uint64_t nstack);
__asm__(".text\n"
        ".globl rsl_call_with\n"
        ".type rsl_call_with,@function\n"
        "rsl_call_with:\n"
        "  .cfi_startproc\n"
        "  pushq %rbp\n"
        "  .cfi_def_cfa_offset 16\n"
        "  .cfi_offset %rbp, -16\n"
        "  movq %rsp, %rbp\n"
        "  .cfi_def_cfa_register %rbp\n"
        "  pushq %rbx\n"
        "  pushq %r12\n"
        "  .cfi_offset %rbx, -24\n"
        "  .cfi_offset %r12, -32\n"
        "  movq %rcx, %rbx\n"
        "  movq %rdi, %r11\n"
        "  movq %rsi, %r10\n"
        "  leaq 1(%r9), %rax\n"   // room for the stack's arguments, an even number of words
        "  andq $-2, %rax\n"
        "  shlq $3, %rax\n"
        "  subq %rax, %rsp\n"
        "  xorl %eax, %eax\n"
        "1:\n"
        "  cmpq %r9, %rax\n"
        "  jae 2f\n"
        "  movq (%r8,%rax,8), %r12\n"
        "  movq %r12, (%rsp,%rax,8)\n"
        "  incq %rax\n"
        "  jmp 1b\n"
        "2:\n"
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
        "  leaq -16(%rbp), %rsp\n"
        "  popq %r12\n"
        "  popq %rbx\n"
        "  popq %rbp\n"
        "  .cfi_def_cfa %rsp, 8\n"
        "  retq\n"
        "  .cfi_endproc\n"
        ".size rsl_call_with, .-rsl_call_with\n");

// The general form (rc_rt.h): six integer registers, eight SSE ones, and ns words on the stack in argument order.
void rc_call_guest_regs(uint64_t fn, const uint64_t gi[6], int ng, const uint64_t dv[8], int nd, const uint64_t *stk,
                        int ns, uint64_t out[4]) {
  uint64_t g[6] = {0, 0, 0, 0, 0, 0}, d[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < ng && i < 6; i++) g[i] = gi[i];
  for (int i = 0; i < nd && i < 8; i++) d[i] = dv[i];
  rsl_call_with(fn, g, d, out, stk, ns > 0 ? (uint64_t)ns : 0);
}

// Integer arguments ints[0..5] in the argument registers and the rest on the stack, as the convention has them;
// dbls[0..7] in the SSE registers; out as rc_rt.h says. More than eight SSE arguments has no caller, and stops.
void rc_call_guest_ex(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t out[4]) {
  if (ndbl > 8) {
    fprintf(stderr, "[rs-rearmed] a call into the game with %d SSE arguments is not written for this build\n", ndbl);
    abort();
  }
  rc_call_guest_regs(fn, ints, nint < 6 ? nint : 6, dbls, ndbl, nint > 6 ? ints + 6 : NULL, nint > 6 ? nint - 6 : 0, out);
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
