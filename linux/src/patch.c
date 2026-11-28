// Native functions in the place of game functions, where the game's code runs as it is: a jump written at the
// function's entry. Where the game is translated the translator leaves the function out and the native one has
// its name (F_<address>); here the function is there, and its first 13 bytes become a jump.
//
// A replacement is written as an override is everywhere (rc_ov.h): void F(rc_ctx *), reading the arguments from
// the game's registers and writing the result to them. So the jump goes to a stub that puts the CPU's registers
// into an rc_ctx, calls the native function, takes rax, rdx, xmm0 and xmm1 back out of it, and returns to the
// game's caller. The whole function is replaced: none of its own instructions run, so none has to be moved.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rc.h"
#include "rsl.h"

_Static_assert(offsetof(rc_ctx, r) == 0 && offsetof(rc_ctx, x) == 128 && offsetof(rc_ctx, mxcsr) == 384 &&
               offsetof(rc_ctx, cg_fp) == 392 && sizeof(rc_ctx) == 400, "the stub below writes rc_ctx by these offsets");

// r10: the native function. On entry the stack holds the game caller's return address, so rsp is 8 past a
// multiple of 16; 408 more bytes (the context and 8) make it a multiple again for the call.
void rsl_override_entry(void);
__asm__(".text\n"
        ".globl rsl_override_entry\n"
        ".type rsl_override_entry,@function\n"
        "rsl_override_entry:\n"
        "  .cfi_startproc\n"
        "  subq $408, %rsp\n"
        "  .cfi_adjust_cfa_offset 408\n"
        "  movq %rax, 0(%rsp)\n"
        "  movq %rcx, 8(%rsp)\n"
        "  movq %rdx, 16(%rsp)\n"
        "  movq %rbx, 24(%rsp)\n"
        "  leaq 408(%rsp), %rax\n"  // the game's rsp: at the return address, as rc_ov.h expects
        "  movq %rax, 32(%rsp)\n"
        "  movq %rbp, 40(%rsp)\n"
        "  movq %rsi, 48(%rsp)\n"
        "  movq %rdi, 56(%rsp)\n"
        "  movq %r8, 64(%rsp)\n"
        "  movq %r9, 72(%rsp)\n"
        "  movq %r10, 80(%rsp)\n"
        "  movq %r11, 88(%rsp)\n"
        "  movq %r12, 96(%rsp)\n"
        "  movq %r13, 104(%rsp)\n"
        "  movq %r14, 112(%rsp)\n"
        "  movq %r15, 120(%rsp)\n"
        "  movdqu %xmm0, 128(%rsp)\n"
        "  movdqu %xmm1, 144(%rsp)\n"
        "  movdqu %xmm2, 160(%rsp)\n"
        "  movdqu %xmm3, 176(%rsp)\n"
        "  movdqu %xmm4, 192(%rsp)\n"
        "  movdqu %xmm5, 208(%rsp)\n"
        "  movdqu %xmm6, 224(%rsp)\n"
        "  movdqu %xmm7, 240(%rsp)\n"
        "  stmxcsr 384(%rsp)\n"
        "  movq $-1, 392(%rsp)\n"
        "  movq %rsp, %rdi\n"
        "  cld\n"
        "  callq *%r10\n"
        "  movq 0(%rsp), %rax\n"
        "  movq 16(%rsp), %rdx\n"
        "  movdqu 128(%rsp), %xmm0\n"
        "  movdqu 144(%rsp), %xmm1\n"
        "  addq $408, %rsp\n"
        "  .cfi_adjust_cfa_offset -408\n"
        "  retq\n"
        "  .cfi_endproc\n"
        ".size rsl_override_entry, .-rsl_override_entry\n");

#define STUB 32
#define MAX_STUBS 512
static uint8_t *g_stubs;
static int g_nstubs;

static void put64(uint8_t *at, uint64_t v) { memcpy(at, &v, 8); }

int rsl_override(uint64_t address, void (*native)(rc_ctx *), const char *name) {
  if (!g_stubs) {
    g_stubs = mmap(NULL, MAX_STUBS * STUB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_stubs == MAP_FAILED) {
      g_stubs = NULL;
      perror("[rs-rearmed] mmap");
      return -1;
    }
  }
  if (g_nstubs == MAX_STUBS) {
    fprintf(stderr, "[rs-rearmed] more than %d replaced functions\n", MAX_STUBS);
    return -1;
  }
  // the stub: movabs r10, native; movabs r11, rsl_override_entry; jmp r11
  uint8_t *s = g_stubs + (size_t)g_nstubs * STUB;
  mprotect(g_stubs, MAX_STUBS * STUB, PROT_READ | PROT_WRITE);
  s[0] = 0x49, s[1] = 0xba;
  put64(s + 2, (uint64_t)(uintptr_t)native);
  s[10] = 0x49, s[11] = 0xbb;
  put64(s + 12, (uint64_t)(uintptr_t)rsl_override_entry);
  s[20] = 0x41, s[21] = 0xff, s[22] = 0xe3;
  mprotect(g_stubs, MAX_STUBS * STUB, PROT_READ | PROT_EXEC);
  g_nstubs++;
  // the function's entry: movabs r11, stub; jmp r11
  uint8_t jump[13] = {0x49, 0xbb, 0, 0, 0, 0, 0, 0, 0, 0, 0x41, 0xff, 0xe3};
  put64(jump + 2, (uint64_t)(uintptr_t)s);
  uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE) - 1, lo = address & ~page, hi = (address + sizeof jump + page) & ~page;
  if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE)) {
    fprintf(stderr, "[rs-rearmed] the game's code at 0x%llx (%s) cannot be written: ", (unsigned long long)address, name);
    perror(NULL);
    return -1;
  }
  memcpy((void *)address, jump, sizeof jump);
  if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC)) {
    perror("[rs-rearmed] the game's code cannot be made executable again");
    return -1;
  }
  return 0;
}

// --- the override modules this build has ---
// online: the game never calls home. The Mac app's module (tools/ws7/recomp/overrides: online_glue.c, gen/online.c,
// over shims/game's offline services), compiled as it is.
#include "rc_target_names.h"
#define OV(name) void RC_FN_##name(rc_ctx *);
#include "overrides_online.inc"
#undef OV

int rsl_install_overrides(void) {
  static const struct {
    uint64_t address;
    void (*native)(rc_ctx *);
    const char *name;
  } list[] = {
#define OV(name) {RC_ADDR_##name, RC_FN_##name, #name},
#include "overrides_online.inc"
#undef OV
  };
  for (size_t i = 0; i < sizeof list / sizeof *list; i++)
    if (rsl_override(list[i].address, list[i].native, list[i].name)) return -1;
  return (int)(sizeof list / sizeof *list);
}
