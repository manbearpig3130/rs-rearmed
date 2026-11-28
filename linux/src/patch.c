// Native code at game functions, where the game's code runs as it is: a jump written at the function's entry.
// Where the game is translated the translator does this while translating; here the function is there, and its
// first five bytes become a jump to a stub near the image.
//
// An override (rc_ov.h) takes the function's place: void F(rc_ctx *), reading the arguments from the game's
// registers and writing the result to them. Its stub puts the CPU's registers into an rc_ctx, calls it, takes rax,
// rdx, xmm0 and xmm1 back out, and returns to the game's caller. None of the function's own instructions run.
//
// A hook point (rc_hook.h) runs hooks before and after the function. Its stub makes the same context and calls
// rc_hook_call, whose `orig` runs the function from the context: the instructions the jump covered, moved, then
// the rest where it is.
//
// A mid point (rc_mid.h) runs hooks before one instruction inside a function, and an edit group (rc_mid.h) puts
// other instructions in the place of some: both further down, with the same jump and the same moving of what it
// covers, or a breakpoint where no jump can be written.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // the instruction pointer in a signal's context (REG_RIP)
#endif
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include "rc.h"
#include "rc_hook.h"
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

// r10d: the hook point's index. The same context; then rsl_hook_dispatch(context, index), which runs the hooks
// around the function (rc_hook.h).
void rsl_hook_entry(void);
void rsl_hook_dispatch(rc_ctx *c, int index);
__asm__(".text\n"
        ".globl rsl_hook_entry\n"
        ".type rsl_hook_entry,@function\n"
        "rsl_hook_entry:\n"
        "  .cfi_startproc\n"
        "  subq $408, %rsp\n"
        "  .cfi_adjust_cfa_offset 408\n"
        "  movq %rax, 0(%rsp)\n"
        "  movq %rcx, 8(%rsp)\n"
        "  movq %rdx, 16(%rsp)\n"
        "  movq %rbx, 24(%rsp)\n"
        "  leaq 408(%rsp), %rax\n"
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
        "  movl %r10d, %esi\n"
        "  cld\n"
        "  callq rsl_hook_dispatch@PLT\n"
        "  movq 0(%rsp), %rax\n"
        "  movq 16(%rsp), %rdx\n"
        "  movdqu 128(%rsp), %xmm0\n"
        "  movdqu 144(%rsp), %xmm1\n"
        "  addq $408, %rsp\n"
        "  .cfi_adjust_cfa_offset -408\n"
        "  retq\n"
        "  .cfi_endproc\n"
        ".size rsl_hook_entry, .-rsl_hook_entry\n");

// The function itself, from a context: rdi the context, rsi where the function goes on (its first instructions,
// moved, then a jump back into it). The argument registers are loaded from the context, and eight words of the
// stack above the game caller's return address are copied below this call's, so a function that takes arguments
// on the stack finds them where it looks. Its results go back into the context, and the context's stack pointer
// passes the return address, as the function's own `ret` leaves it.
void rsl_call_original(rc_ctx *c, void *go_on);
__asm__(".text\n"
        ".globl rsl_call_original\n"
        ".type rsl_call_original,@function\n"
        "rsl_call_original:\n"
        "  .cfi_startproc\n"
        "  pushq %rbx\n"
        "  .cfi_adjust_cfa_offset 8\n"
        "  .cfi_offset %rbx, -16\n"
        "  subq $64, %rsp\n"
        "  .cfi_adjust_cfa_offset 64\n"
        "  movq %rdi, %rbx\n"
        "  movq %rsi, %r11\n"
        "  movq 32(%rbx), %rax\n"
        "  movq 8(%rax), %rcx\n"
        "  movq %rcx, 0(%rsp)\n"
        "  movq 16(%rax), %rcx\n"
        "  movq %rcx, 8(%rsp)\n"
        "  movq 24(%rax), %rcx\n"
        "  movq %rcx, 16(%rsp)\n"
        "  movq 32(%rax), %rcx\n"
        "  movq %rcx, 24(%rsp)\n"
        "  movq 40(%rax), %rcx\n"
        "  movq %rcx, 32(%rsp)\n"
        "  movq 48(%rax), %rcx\n"
        "  movq %rcx, 40(%rsp)\n"
        "  movq 56(%rax), %rcx\n"
        "  movq %rcx, 48(%rsp)\n"
        "  movq 64(%rax), %rcx\n"
        "  movq %rcx, 56(%rsp)\n"
        "  movq 56(%rbx), %rdi\n"
        "  movq 48(%rbx), %rsi\n"
        "  movq 16(%rbx), %rdx\n"
        "  movq 8(%rbx), %rcx\n"
        "  movq 64(%rbx), %r8\n"
        "  movq 72(%rbx), %r9\n"
        "  movq 0(%rbx), %rax\n"
        "  movdqu 128(%rbx), %xmm0\n"
        "  movdqu 144(%rbx), %xmm1\n"
        "  movdqu 160(%rbx), %xmm2\n"
        "  movdqu 176(%rbx), %xmm3\n"
        "  movdqu 192(%rbx), %xmm4\n"
        "  movdqu 208(%rbx), %xmm5\n"
        "  movdqu 224(%rbx), %xmm6\n"
        "  movdqu 240(%rbx), %xmm7\n"
        "  callq *%r11\n"
        "  movq %rax, 0(%rbx)\n"
        "  movq %rdx, 16(%rbx)\n"
        "  movdqu %xmm0, 128(%rbx)\n"
        "  movdqu %xmm1, 144(%rbx)\n"
        "  addq $8, 32(%rbx)\n"
        "  addq $64, %rsp\n"
        "  .cfi_adjust_cfa_offset -64\n"
        "  popq %rbx\n"
        "  .cfi_adjust_cfa_offset -8\n"
        "  .cfi_restore %rbx\n"
        "  retq\n"
        "  .cfi_endproc\n"
        ".size rsl_call_original, .-rsl_call_original\n");

// --- the stubs' memory ---
// One region just past the image, so that a jump from the game's code to a stub, and the moved instructions'
// own references back into the image, are within the 2 GB a 32-bit displacement reaches.
#define NEAR_SIZE (1u << 20)
static uint8_t *g_near, *g_near_at;
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static uint8_t *near_take(size_t n) {
  if (!g_near) {
    uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE) - 1;
    for (uint64_t at = (rsl_image_end() + 0x100000 + page) & ~page; at < rsl_image_end() + 0x40000000ull; at += 0x1000000) {
      void *p = mmap((void *)at, NEAR_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
      if (p == (void *)at) {
        g_near = g_near_at = p;
        break;
      }
      if (p != MAP_FAILED) munmap(p, NEAR_SIZE);
    }
    if (!g_near) {
      fprintf(stderr, "[rs-rearmed] no memory near the game's image for the patches' stubs\n");
      return NULL;
    }
  }
  if (g_near_at + n > g_near + NEAR_SIZE) {
    fprintf(stderr, "[rs-rearmed] the patches' stubs are full\n");
    return NULL;
  }
  uint8_t *p = g_near_at;
  g_near_at += (n + 15) & ~(size_t)15;
  return p;
}

static void put64(uint8_t *at, uint64_t v) { memcpy(at, &v, 8); }
static void put32(uint8_t *at, int64_t v) {
  int32_t d = (int32_t)v;
  memcpy(at, &d, 4);
}

// movabs REG, value; movabs r11, target; jmp r11. REG: 0x49ba r10, 0x48be rsi.
static uint8_t *far_jump_with(uint16_t movabs, uint64_t value, void *target) {
  uint8_t *s = near_take(23);
  if (!s) return NULL;
  s[0] = (uint8_t)(movabs >> 8), s[1] = (uint8_t)movabs;
  put64(s + 2, value);
  s[10] = 0x49, s[11] = 0xbb;
  put64(s + 12, (uint64_t)(uintptr_t)target);
  s[20] = 0x41, s[21] = 0xff, s[22] = 0xe3;
  return s;
}

// BYTES written at ADDRESS: the game's code, which is made writable for it, or a stub of ours, which is. While an
// edit group is being written the old bytes are kept, to put back if a later edit of the group cannot be done.
#define MAX_UNDO 1024
static struct {
  uint64_t address;
  uint8_t n, old[79];
} g_undo[MAX_UNDO];
static int g_nundo, g_undo_on;

static int in_stubs(uint64_t address) {
  return g_near && address >= (uint64_t)(uintptr_t)g_near && address < (uint64_t)(uintptr_t)g_near + NEAR_SIZE;
}

static int write_code(uint64_t address, const uint8_t *bytes, size_t n, const char *name) {
  if (in_stubs(address)) {
    memcpy((void *)address, bytes, n);
    return 0;
  }
  if (g_undo_on) {
    if (g_nundo == MAX_UNDO || n > sizeof g_undo[0].old) {
      fprintf(stderr, "[rs-rearmed] %s: more changes than one group may have\n", name);
      return -1;
    }
    g_undo[g_nundo].address = address, g_undo[g_nundo].n = (uint8_t)n;
    memcpy(g_undo[g_nundo++].old, (const void *)address, n);
  }
  uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE) - 1, lo = address & ~page, hi = (address + n + page) & ~page;
  if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE)) {
    fprintf(stderr, "[rs-rearmed] the game's code at 0x%llx (%s) cannot be written: ", (unsigned long long)address, name);
    perror(NULL);
    return -1;
  }
  memcpy((void *)address, bytes, n);
  if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC)) {
    perror("[rs-rearmed] the game's code cannot be made executable again");
    return -1;
  }
  return 0;
}

// The code at ADDRESS becomes `jmp STUB` (5 bytes).
static int write_jump(uint64_t address, const uint8_t *stub, const char *name) {
  uint8_t jump[5] = {0xe9, 0, 0, 0, 0};
  put32(jump + 1, (int64_t)(uintptr_t)stub - (int64_t)(address + 5));
  return write_code(address, jump, sizeof jump, name);
}

// Where no jump can be written (something in the function goes to an address inside the five bytes: linux/
// patch-sites.tsv, patch-edits.tsv), the one byte at ADDRESS becomes a breakpoint, and the signal it raises goes on
// at STUB with every register as it was. That costs a signal each time the place is reached, so it is the way in
// only where the jump is not.
#define MAX_TRAPS 64
static struct {
  uint64_t address;
  const uint8_t *stub;
} g_traps[MAX_TRAPS];
static int g_ntraps;

static void on_trap(int sig, siginfo_t *info, void *context) {
  (void)sig, (void)info;
  ucontext_t *u = context;
  uint64_t at = (uint64_t)u->uc_mcontext.gregs[REG_RIP] - 1;
  for (int i = 0; i < g_ntraps; i++)
    if (g_traps[i].address == at) {
      u->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)g_traps[i].stub;
      return;
    }
  signal(SIGTRAP, SIG_DFL);  // not one of ours: as it would have been without this handler
  raise(SIGTRAP);
}

static int write_trap(uint64_t address, const uint8_t *stub, const char *name) {
  static const uint8_t int3 = 0xcc;
  if (g_ntraps == MAX_TRAPS) {
    fprintf(stderr, "[rs-rearmed] %s: no more breakpoints than %d\n", name, MAX_TRAPS);
    return -1;
  }
  if (!g_ntraps) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTRAP, &sa, NULL)) {
      perror("[rs-rearmed] no handler for breakpoints");
      return -1;
    }
  }
  g_traps[g_ntraps].address = address, g_traps[g_ntraps].stub = stub;
  if (write_code(address, &int3, 1, name)) return -1;
  g_ntraps++;
  return 0;
}

int rsl_override(uint64_t address, void (*native)(rc_ctx *), const char *name) {
  uint8_t *stub = far_jump_with(0x49ba, (uint64_t)(uintptr_t)native, rsl_override_entry);
  return stub ? write_jump(address, stub, name) : -1;
}

// --- moving instructions ---
// A hook runs before a function, and a mid point before one instruction, and the game's own instructions then run:
// so the instructions the jump covers have to run somewhere. They are copied behind the stubs and followed by a jump
// back to the first one not covered. The mover reads the kinds of instruction that are at the tables' sites (pushes,
// moves, arithmetic, compares, calls, branches, a few SSE ones); one it does not know, and the site is left alone
// with a line saying so. What depends on where an instruction is gets corrected: a displacement from the instruction
// pointer, a call's or a jump's distance; a short branch becomes its long form.
//
// Whether a jump may be written at a site at all (no branch of the function lands inside the covered bytes) cannot
// be told from the bytes there: linux/patch-sites.tsv has that, made from the whole function by
// linux/check_patch_sites.py.
//
// The length of the instruction at P, or -1. *DISP: the offset in it of a 32-bit distance from the instruction's end
// to correct, or -1. *SHORT: 1 for a branch with an 8-bit distance. *BRANCH: 1 when the distance is to code (a
// branch's or a call's), 0 when to data.
static int instruction(const uint8_t *p, int *disp, int *short_branch, int *branch) {
  int i = 0, word = 0, wide = 0, modrm = 0, imm = 0;
  *disp = -1, *short_branch = 0, *branch = 0;
  for (; p[i] == 0x66 || p[i] == 0xf2 || p[i] == 0xf3 || p[i] == 0x2e; i++) {  // (0x2e and a row of 0x66: a long no-op's)
    if (p[i] == 0x66) word = 1;
    if (i == 12) return -1;
  }
  if ((p[i] & 0xf0) == 0x40) wide = (p[i++] >> 3) & 1;  // REX
  uint8_t op = p[i++];
  if (op == 0x0f) {
    uint8_t op2 = p[i++];
    if (op2 == 0x0b) return i;  // ud2
    if (op2 >= 0x80 && op2 <= 0x8f) {  // a conditional jump, long form
      *disp = i, *branch = 1;
      return i + 4;
    }
    static const uint8_t k[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1f, 0x28, 0x29, 0x2a, 0x2c, 0x2d, 0x2e, 0x2f, 0x51,
                                0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x6e, 0x6f, 0x7e,
                                0x7f, 0xaf, 0xb6, 0xb7, 0xbe, 0xbf, 0xd6};
    for (size_t n = 0; n < sizeof k; n++) modrm |= k[n] == op2;
    if ((op2 >= 0x40 && op2 <= 0x4f) || (op2 >= 0x90 && op2 <= 0x9f)) modrm = 1;  // cmov, setcc
    if (!modrm) return -1;
  } else if ((op >= 0x50 && op <= 0x5f) || op == 0x90 || op == 0x98 || op == 0x99 || op == 0xc3 || op == 0xc9) {
    // push, pop, nop, cdqe, cdq, ret, leave: one byte
  } else if ((op >= 0x70 && op <= 0x7f) || op == 0xeb) {  // a conditional jump or a jump, short form
    if (i != 1) return -1;  // with a prefix: not one the long form below is written for
    *short_branch = *branch = 1;
    return i + 1;
  } else if (op == 0xe8 || op == 0xe9) {  // call, jmp: a distance from the instruction's end
    *disp = i, *branch = 1;
    return i + 4;
  } else if (op == 0xc2) {
    imm = 2;  // ret n
  } else if (op >= 0xb0 && op <= 0xb7) {
    imm = 1;
  } else if (op >= 0xb8 && op <= 0xbf) {
    imm = wide ? 8 : word ? 2 : 4;
  } else if (op == 0x6a || (op < 0x40 && (op & 7) == 4) || op == 0xa8) {
    imm = 1;  // push imm8; an operation on al with an immediate
  } else if (op == 0x68 || (op < 0x40 && (op & 7) == 5) || op == 0xa9) {
    imm = word ? 2 : 4;  // push imm32; an operation on eax with an immediate
  } else {
    static const uint8_t plain[] = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1a, 0x1b,
                                    0x20, 0x21, 0x22, 0x23, 0x28, 0x29, 0x2a, 0x2b, 0x30, 0x31, 0x32, 0x33, 0x38, 0x39, 0x3a, 0x3b,
                                    0x63, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8d, 0xd0, 0xd1, 0xd2, 0xd3};
    for (size_t n = 0; n < sizeof plain; n++) modrm |= plain[n] == op;
    if (op == 0x80 || op == 0x83 || op == 0xc0 || op == 0xc1 || op == 0xc6 || op == 0x6b) modrm = 1, imm = 1;
    else if (op == 0x81 || op == 0xc7 || op == 0x69) modrm = 1, imm = word ? 2 : 4;
    else if (op == 0xf6 || op == 0xf7) {  // test has an immediate, the rest of the group none
      modrm = 1;
      if (((p[i] >> 3) & 7) < 2) imm = op == 0xf6 ? 1 : word ? 2 : 4;
    } else if (op == 0xfe || op == 0xff) {
      modrm = 1;  // inc, dec, and through a register or memory: call, jmp, push
    }
    if (!modrm) return -1;
  }
  if (modrm) {
    uint8_t m = p[i++];
    int mod = m >> 6, rm = m & 7;
    if (mod != 3) {
      if (rm == 4 && (p[i++] & 7) == 5 && mod == 0) i += 4;  // SIB, with a 32-bit displacement and no base
      if (mod == 0 && rm == 5) *disp = i, i += 4;              // by the instruction pointer
      else if (mod == 1) i += 1;
      else if (mod == 2) i += 4;
    }
  }
  return i + imm;
}

// Every place instructions were moved from, with where each instruction went: an address inside the bytes a jump
// covers is no longer code, and whoever means to go there (a mid point's continuation that skips the first of two
// covered instructions, an edit's branch to the instruction after its range) goes to the moved copy instead.
#define MAX_MOVED 1024
#define MOVED_MOST 12
typedef struct {
  uint64_t address;
  int covered, n;
  uint8_t *to;
  uint16_t from_off[MOVED_MOST], to_off[MOVED_MOST];
} moved;
static moved g_moved[MAX_MOVED];
static int g_nmoved;

// Where to go for ADDRESS among M's covered bytes: the moved copy of the instruction that was there; ADDRESS
// itself outside them (the first covered byte is outside: the jump is there); 0 inside an instruction or among
// bytes that were replaced, not moved.
static uint64_t moved_in(const moved *m, uint64_t address) {
  if (address <= m->address || address >= m->address + (uint64_t)m->covered) return address;
  for (int k = 0; k < m->n; k++)
    if (m->address + m->from_off[k] == address) return (uint64_t)(uintptr_t)(m->to + m->to_off[k]);
  return 0;
}

// Where to go for ADDRESS: itself, or the moved copy of the instruction that was there (and the copy's copy, where
// a jump was since written over the copy: a mid point at an instruction an edit moved).
static uint64_t moved_address(uint64_t address) {
  for (int again = 0; again < 4; again++) {
    uint64_t at = address;
    for (int i = 0; i < g_nmoved && at == address; i++) at = moved_in(&g_moved[i], address);
    if (!at) {
      fprintf(stderr, "[rs-rearmed] 0x%llx is inside an instruction that a jump covers: nothing can go on there\n", (unsigned long long)address);
      return 0;
    }
    if (at == address) return at;
    address = at;
  }
  return address;
}

// How many bytes the moved form of an instruction of N bytes takes: a short branch becomes its long form.
static int moved_size(const uint8_t *p, int n, int short_branch) { return !short_branch ? n : p[0] == 0xeb ? 5 : 6; }

// The instruction P (N bytes: `instruction` gave DISP, SHORT_BRANCH and BRANCH), which is or stands at the address
// HERE, written at TO to do there what it does at HERE. A branch into SELF's covered bytes goes to the moved copy.
// How many bytes were written, or -1; *FIXES counts the instructions that had a distance corrected.
static int move_one(const uint8_t *p, int n, int disp, int short_branch, int branch, uint64_t here, uint8_t *to, const moved *self,
                    int *fixes) {
  int64_t there = (int64_t)(uintptr_t)to;
  if (!short_branch && disp < 0) {
    memcpy(to, p, (size_t)n);
    return n;
  }
  int32_t d = (int8_t)p[1];
  if (!short_branch) memcpy(&d, p + disp, 4);
  int64_t target = (int64_t)here + n + d, distance;
  if (branch && !(target = (int64_t)moved_in(self, (uint64_t)target))) return -1;
  int w = moved_size(p, n, short_branch);
  if (short_branch) {
    disp = w - 4;
    if (p[0] == 0xeb) to[0] = 0xe9;
    else to[0] = 0x0f, to[1] = (uint8_t)(0x80 | (p[0] & 0x0f));
  } else {
    memcpy(to, p, (size_t)n);
  }
  distance = target - (there + w);
  if (distance != (int32_t)distance) return -1;
  put32(to + disp, distance);
  ++*fixes;
  return w;
}

// The instructions at ADDRESS that a 5-byte jump covers (LEAST 5), or the one there (LEAST 1, for a breakpoint),
// moved to TO (room for ROOM bytes), then a jump to the first not covered. How many bytes were covered, or -1 with
// a line.
static int move_code(uint64_t address, uint8_t *to, int room, const char *name, int least) {
  const uint8_t *from = (const uint8_t *)address;
  int done = 0, w = 0, n, disp, short_branch, branch, fixes = 0;
  if (g_nmoved == MAX_MOVED) return -1;
  moved *m = &g_moved[g_nmoved];
  m->address = address, m->to = to, m->n = 0;
  while (done < least) {  // how far the jump reaches, first: a covered branch may go to a covered instruction
    if ((n = instruction(from + done, &disp, &short_branch, &branch)) < 0 || done + n > 40 || m->n == MOVED_MOST) {
      fprintf(stderr, "[rs-rearmed] %s at 0x%llx: its instruction at +%d is not one this build moves (bytes %02x %02x %02x %02x): left as it is\n",
              name, (unsigned long long)address, done, from[done], from[done + 1], from[done + 2], from[done + 3]);
      return -1;
    }
    m->from_off[m->n] = (uint16_t)done, m->to_off[m->n++] = (uint16_t)w;
    w += moved_size(from + done, n, short_branch), done += n;
  }
  m->covered = done;
  if (w + 5 > room) {
    fprintf(stderr, "[rs-rearmed] %s at 0x%llx: its moved instructions take more room than a stub has: left as it is\n", name,
            (unsigned long long)address);
    return -1;
  }
  for (done = 0, w = 0; done < m->covered; done += n) {
    n = instruction(from + done, &disp, &short_branch, &branch);
    int put = move_one(from + done, n, disp, short_branch, branch, address + (uint64_t)done, to + w, m, &fixes);
    if (put < 0) {
      fprintf(stderr, "[rs-rearmed] %s at 0x%llx: its instruction at +%d goes where this build cannot send it: left as it is\n", name,
              (unsigned long long)address, done);
      return -1;
    }
    w += put;
  }
  to[w] = 0xe9;
  put32(to + w + 1, (int64_t)(address + (uint64_t)m->covered) - (int64_t)(uintptr_t)(to + w + 5));
  g_nmoved++;
  return m->covered;
}

// The first instructions of the function at ADDRESS, moved; then a jump to the rest.
static uint8_t *moved_start(uint64_t address, const char *name) {
  uint8_t *to = near_take(96);
  return to && move_code(address, to, 96, name, 5) > 0 ? to : NULL;
}

// A hook point: the function at ADDRESS enters rc_hook_call(INDEX), with the function itself as its `orig`.
static void (*g_original[256])(rc_ctx *);

void (*rsl_hook_original(int index))(rc_ctx *) { return index >= 0 && index < 256 ? g_original[index] : NULL; }

int rsl_hook_point(uint64_t address, int index, const char *name) {
  if (index < 0 || index >= 256) return -1;
  uint8_t *go_on = moved_start(address, name);
  if (!go_on) return -1;
  uint8_t *original = far_jump_with(0x48be, (uint64_t)(uintptr_t)go_on, rsl_call_original);  // orig(c): rsi = where it goes on
  uint8_t *stub = far_jump_with(0x49ba, (uint64_t)index, rsl_hook_entry);                      // r10d = the index
  if (!original || !stub) return -1;
  g_original[index] = (void (*)(rc_ctx *))(uintptr_t)original;
  return write_jump(address, stub, name);
}

// --- mid points ---
// A mid point (rc_mid.h) is a hook before one instruction inside a function. The jump at the instruction goes to a
// stub of the point's own, which steps over the 128 bytes below the stack pointer that a function may be using,
// leaves the point's number on the stack and calls rsl_mid_entry. That puts the flags and every register into an
// rc_ctx, calls rc_mid_call through rsl_mid_dispatch, takes the registers and flags back (the hooks may have set
// them), and returns: to the stub, where the moved instruction runs and a jump goes on after it, or, when a hook
// chose one of the point's continuations, to that address in the game.
//
// On entry: the return address into the stub, the point's number above it, then the 128 bytes, then the game's stack.
// After the flags and 408 bytes for the context: the flags at 408, the return address at 416, the number at 424, and
// the game's stack pointer was 560 above.
void rsl_mid_entry(void);
uint64_t rsl_mid_dispatch(int point, rc_ctx *c, uint64_t *flags);
__asm__(".text\n"
        ".globl rsl_mid_entry\n"
        ".type rsl_mid_entry,@function\n"
        "rsl_mid_entry:\n"
        "  pushfq\n"
        "  subq $408, %rsp\n"
        "  movq %rax, 0(%rsp)\n"
        "  movq %rcx, 8(%rsp)\n"
        "  movq %rdx, 16(%rsp)\n"
        "  movq %rbx, 24(%rsp)\n"
        "  leaq 560(%rsp), %rax\n"
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
        "  movdqu %xmm8, 256(%rsp)\n"
        "  movdqu %xmm9, 272(%rsp)\n"
        "  movdqu %xmm10, 288(%rsp)\n"
        "  movdqu %xmm11, 304(%rsp)\n"
        "  movdqu %xmm12, 320(%rsp)\n"
        "  movdqu %xmm13, 336(%rsp)\n"
        "  movdqu %xmm14, 352(%rsp)\n"
        "  movdqu %xmm15, 368(%rsp)\n"
        "  stmxcsr 384(%rsp)\n"
        "  movq $-1, 392(%rsp)\n"
        "  movq %rsp, %rbx\n"
        "  movl 424(%rbx), %edi\n"
        "  movq %rbx, %rsi\n"
        "  leaq 408(%rbx), %rdx\n"
        "  andq $-16, %rsp\n"
        "  cld\n"
        "  callq rsl_mid_dispatch@PLT\n"
        "  movq %rbx, %rsp\n"
        "  testq %rax, %rax\n"
        "  jz 1f\n"
        "  movq %rax, 416(%rsp)\n"
        "1:\n"
        "  movdqu 128(%rsp), %xmm0\n"
        "  movdqu 144(%rsp), %xmm1\n"
        "  movdqu 160(%rsp), %xmm2\n"
        "  movdqu 176(%rsp), %xmm3\n"
        "  movdqu 192(%rsp), %xmm4\n"
        "  movdqu 208(%rsp), %xmm5\n"
        "  movdqu 224(%rsp), %xmm6\n"
        "  movdqu 240(%rsp), %xmm7\n"
        "  movdqu 256(%rsp), %xmm8\n"
        "  movdqu 272(%rsp), %xmm9\n"
        "  movdqu 288(%rsp), %xmm10\n"
        "  movdqu 304(%rsp), %xmm11\n"
        "  movdqu 320(%rsp), %xmm12\n"
        "  movdqu 336(%rsp), %xmm13\n"
        "  movdqu 352(%rsp), %xmm14\n"
        "  movdqu 368(%rsp), %xmm15\n"
        "  ldmxcsr 384(%rsp)\n"
        "  movq 8(%rsp), %rcx\n"
        "  movq 16(%rsp), %rdx\n"
        "  movq 24(%rsp), %rbx\n"
        "  movq 40(%rsp), %rbp\n"
        "  movq 48(%rsp), %rsi\n"
        "  movq 56(%rsp), %rdi\n"
        "  movq 64(%rsp), %r8\n"
        "  movq 72(%rsp), %r9\n"
        "  movq 80(%rsp), %r10\n"
        "  movq 88(%rsp), %r11\n"
        "  movq 96(%rsp), %r12\n"
        "  movq 104(%rsp), %r13\n"
        "  movq 112(%rsp), %r14\n"
        "  movq 120(%rsp), %r15\n"
        "  movq 0(%rsp), %rax\n"
        "  addq $408, %rsp\n"
        "  popfq\n"
        "  retq $136\n"
        ".size rsl_mid_entry, .-rsl_mid_entry\n");

// lea rsp, [rsp-128]; push POINT; call [rsl_mid_entry]; then the moved instruction and the jump on. TRAP: by a
// breakpoint, where a jump cannot be written.
static int mid_point(uint64_t address, int point, const char *name, int trap) {
  uint8_t *s = near_take(26 + 96);
  if (!s) return -1;
  static const uint8_t lea[5] = {0x48, 0x8d, 0x64, 0x24, 0x80};
  memcpy(s, lea, 5);
  s[5] = 0x68;
  put32(s + 6, point);
  s[10] = 0xff, s[11] = 0x15;  // call [rip + 2]: the pointer after the two-byte jump that the return lands on
  put32(s + 12, 2);
  s[16] = 0xeb, s[17] = 0x08;
  put64(s + 18, (uint64_t)(uintptr_t)rsl_mid_entry);
  if (move_code(address, s + 26, 96, name, trap ? 1 : 5) < 0) return -1;
  return trap ? write_trap(address, s, name) : write_jump(address, s, name);
}

// --- edits ---
// An edit (rc_mid.h) is instructions of the game that a mod switches to others, a group at a time. Where the
// game's code is translated, both forms are translated and the group is a switch the code reads. Here it is bytes
// to change in the loaded code, when the mod asks (in its constructor, before the game runs and before the mid
// points are written, so that a mid point among an edit's bytes moves what the edit left):
//   - bytes inside the range, or new instructions no longer than the range: written there, the rest no-ops;
//   - new instructions longer than the range: they go to a stub, corrected for where they are now (they were
//     written as if at the range's address), and the range becomes a jump to it. Under five bytes the jump covers
//     the instructions after the range too, which are moved behind the new ones as a hook's are;
//   - where the table says no jump can be written (linux/patch-edits.tsv), a breakpoint, which covers the range
//     alone.
// The edits are taken from the highest address down, so a jump that needs the instructions after its range finds
// them as they will be: the game's, another edit's bytes, or another edit's jump. The table has none of the game's
// bytes: each edit carries a check of the ones it expects, and how its longer form reads to the tool that made the
// table; where the program's own reading differs, or anything else stops one edit, every edit of the group is taken
// back and the group is refused.
typedef struct {
  const char *group;
  uint64_t address;
  uint8_t range;      // the bytes the edit replaces
  uint32_t check;     // the first four bytes of the SHA-256 of the game's bytes there
  uint8_t whole;      // 1: BYTES are new instructions for the range; 0: (offset, length, bytes) changes inside it
  uint8_t covered;    // the bytes the longer form's jump covers
  uint8_t fixes;      // the instructions the longer form has corrected
  uint8_t can;        // 0: no jump can be written there
  uint16_t size;
  const uint8_t *bytes;
} rsl_edit;

static uint32_t rotr(uint32_t x, int n) { return x >> n | x << (32 - n); }

// The first four bytes of the SHA-256 of P[0..N), N under 56 (one block).
static uint32_t check_of(const uint8_t *p, size_t n) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
      0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
      0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
      0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
      0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, w[64], v[8];
  uint8_t block[128] = {0};
  size_t blocks = n < 56 ? 1 : 2;
  if (n > 119) return 0;
  memcpy(block, p, n);
  block[n] = 0x80;
  block[blocks * 64 - 2] = (uint8_t)(n * 8 >> 8), block[blocks * 64 - 1] = (uint8_t)(n * 8);
  for (size_t b = 0; b < blocks; b++) {
    for (int i = 0; i < 16; i++) {
      const uint8_t *q = block + b * 64 + i * 4;
      w[i] = (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3];
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ w[i - 15] >> 3, s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ w[i - 2] >> 10;
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 64; i++) {
      uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25), ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
      uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22), maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      memmove(v + 1, v, 7 * sizeof *v);
      v[4] += t1, v[0] = t1 + s0 + maj;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
  }
  return h[0];
}

// One edit whose new instructions are longer than its range. 0, or -1 with a line.
static int edit_longer(const rsl_edit *e, int *traps) {
  char name[64];
  snprintf(name, sizeof name, "the %s edit at 0x%llx", e->group, (unsigned long long)e->address);
  const uint8_t *game = (const uint8_t *)e->address;
  int n, disp, short_branch, branch, fixes = 0, w = 0, covered = e->range;
  if (g_nmoved == MAX_MOVED) return -1;
  moved *m = &g_moved[g_nmoved];
  m->address = e->address, m->n = 0;
  for (int at = 0; at < e->size; at += n) {  // the room the new instructions take
    if ((n = instruction(e->bytes + at, &disp, &short_branch, &branch)) < 0 || at + n > e->size) {
      fprintf(stderr, "[rs-rearmed] %s: its new instruction at +%d is not one this build moves\n", name, at);
      return -1;
    }
    w += moved_size(e->bytes + at, n, short_branch);
  }
  while (e->can && covered < 5) {  // and the instructions after the range that the jump covers
    if ((n = instruction(game + covered, &disp, &short_branch, &branch)) < 0 || m->n == MOVED_MOST) {
      fprintf(stderr, "[rs-rearmed] %s: the instruction at +%d, which its jump would cover, is not one this build moves\n", name, covered);
      return -1;
    }
    m->from_off[m->n] = (uint16_t)covered, m->to_off[m->n++] = (uint16_t)w;
    w += moved_size(game + covered, n, short_branch), covered += n;
  }
  if (covered != e->covered) {
    fprintf(stderr, "[rs-rearmed] %s: its jump covers %d bytes as this build reads them, %d in the table\n", name, covered, e->covered);
    return -1;
  }
  uint8_t *stub = near_take((size_t)w + 5);
  if (!stub) return -1;
  m->to = stub, m->covered = covered;
  w = 0;
  for (int k = 0, at = 0; at < e->size || k < m->n; at += n) {
    int ours = at < e->size;  // the new instructions, then the moved ones
    const uint8_t *p = ours ? e->bytes + at : game + m->from_off[k];
    uint64_t here = e->address + (ours ? (uint64_t)at : m->from_off[k++]);
    n = instruction(p, &disp, &short_branch, &branch);
    int put = move_one(p, n, disp, short_branch, branch, here, stub + w, m, &fixes);
    if (put < 0) {
      fprintf(stderr, "[rs-rearmed] %s: an instruction of it goes where this build cannot send it\n", name);
      return -1;
    }
    w += put;
  }
  stub[w] = 0xe9;
  put32(stub + w + 1, (int64_t)(e->address + (uint64_t)covered) - (int64_t)(uintptr_t)(stub + w + 5));
  if (fixes != e->fixes) {
    fprintf(stderr, "[rs-rearmed] %s: %d of its instructions hold a distance as this build reads them, %d in the table\n", name, fixes,
            e->fixes);
    return -1;
  }
  g_nmoved++;
  uint8_t rest[80];
  memset(rest, 0x90, sizeof rest);
  if (!e->can) {
    ++*traps;
    return write_trap(e->address, stub, name) || (covered > 1 && write_code(e->address + 1, rest, (size_t)covered - 1, name)) ? -1 : 0;
  }
  return write_jump(e->address, stub, name) || (covered > 5 && write_code(e->address + 5, rest, (size_t)covered - 5, name)) ? -1 : 0;
}

static int edit_one(const rsl_edit *e, int *longer, int *traps) {
  uint8_t now[64];
  const uint8_t *game = (const uint8_t *)e->address;
  if (e->range > sizeof now || check_of(game, e->range) != e->check) {
    fprintf(stderr, "[rs-rearmed] the %s edit at 0x%llx: the game's %d bytes there are not the ones the table was made for\n", e->group,
            (unsigned long long)e->address, e->range);
    return -1;
  }
  if (e->whole && e->size > e->range) {
    ++*longer;
    return edit_longer(e, traps);
  }
  if (e->whole) {
    memset(now, 0x90, sizeof now);
    memcpy(now, e->bytes, e->size);
  } else {
    memcpy(now, game, e->range);
    for (int at = 0; at + 2 <= e->size; at += 2 + e->bytes[at + 1]) {
      if (e->bytes[at] + e->bytes[at + 1] > e->range || at + 2 + e->bytes[at + 1] > e->size) return -1;
      memcpy(now + e->bytes[at], e->bytes + at + 2, e->bytes[at + 1]);
    }
  }
  return write_code(e->address, now, e->range, e->group);
}

// Every edit of GROUP in TABLE (COUNT rows, from the highest address down), written into the code: how many, or
// -1 with a line and the code as it was.
static int edits_write(const rsl_edit *table, int count, const char *group) {
  int n = 0, longer = 0, traps = 0, moved_before = g_nmoved, traps_before = g_ntraps;
  uint8_t *stubs_before = g_near_at;
  g_nundo = 0, g_undo_on = 1;
  for (int i = 0; i < count; i++) {
    if (strcmp(table[i].group, group)) continue;
    if (edit_one(&table[i], &longer, &traps)) {
      g_undo_on = 0;
      while (g_nundo--) write_code(g_undo[g_nundo].address, g_undo[g_nundo].old, g_undo[g_nundo].n, group);
      g_nundo = 0, g_nmoved = moved_before, g_ntraps = traps_before;
      if (stubs_before) g_near_at = stubs_before;
      fprintf(stderr, "[rs-rearmed] edits %s: none of them written (%d were, and are taken back)\n", group, n);
      return -1;
    }
    n++;
  }
  g_undo_on = 0;
  fprintf(stderr, "[rs-rearmed] edits %s: %d written into the game's code, %d of them longer than their range (a jump to them", group, n,
          longer);
  if (traps) fprintf(stderr, "; a breakpoint for %d", traps);
  fprintf(stderr, ")\n");
  return n;
}

#ifndef RSL_PATCH_TEST  // (test/patch_test.c: the above on code of the test's own)
// --- what this build puts in ---
// The override modules, compiled as they are (tools/ws7/recomp/overrides): `online`, the game never calls home; and
// of `mods`, the built-in fixes, the functions it replaces whole. And every hook point of the tables.
#include "rc_target_names.h"
#include "rc_ov.h"
#define OV(name) void RC_FN_##name(rc_ctx *);
#include "overrides_online.inc"
#include "overrides_audio.inc"  // the audio module's two that are not the Mac's alone (src/audio_glue.c)
#undef OV
// The fixes module's own table of the functions it replaces (overrides/mods/mods.c).
extern const ov_entry ov_mods_entries[];
extern const int OV_MODS_COUNT;
extern void (*rsl_sng_verify_original)(rc_ctx *);  // direct.c

// rc_hook.c's table of the hook points, by index: every name of tools/targets/names/hooks.list.
const uint64_t rc_hook_points[] = {
#define HOOK(name) RC_ADDR_##name,
#include "hooks.inc"
#undef HOOK
};
const int rc_hook_npoints = (int)(sizeof rc_hook_points / sizeof *rc_hook_points);
static const char *const k_hook_names[] = {
#define HOOK(name) #name,
#include "hooks.inc"
#undef HOOK
};

void rsl_hook_dispatch(rc_ctx *c, int index) { rc_hook_call(index, c, g_original[index]); }

int rsl_install_overrides(void) {
  static const struct {
    uint64_t address;
    void (*native)(rc_ctx *);
    const char *name;
  } list[] = {
#define OV(name) {RC_ADDR_##name, RC_FN_##name, #name},
#include "overrides_online.inc"
#include "overrides_audio.inc"
#undef OV
  };
  int n = 0;
  for (size_t i = 0; i < sizeof list / sizeof *list; i++, n++)
    if (rsl_override(list[i].address, list[i].native, list[i].name)) return -1;
  for (int i = 0; i < OV_MODS_COUNT; i++) {
    const ov_entry *e = &ov_mods_entries[i];
    if (!strcmp(e->name, "sng_verify")) {  // its override runs the function itself for most callers (direct.c)
      uint8_t *go_on = moved_start(e->vmaddr, e->name);
      uint8_t *original = go_on ? far_jump_with(0x48be, (uint64_t)(uintptr_t)go_on, rsl_call_original) : NULL;
      if (!original) continue;  // not replaced: the game checks every signature, as without the fixes
      rsl_sng_verify_original = (void (*)(rc_ctx *))(uintptr_t)original;
    }
    if (rsl_override(e->vmaddr, e->fn, e->name)) return -1;
    n++;
  }
  return n;
}

// How many hook points were put in; one whose first instructions this build does not move is left out, with a line.
int rsl_install_hooks(void) {
  int n = 0;
  for (int i = 0; i < rc_hook_npoints; i++) n += rsl_hook_point(rc_hook_points[i], i, k_hook_names[i]) == 0;
  return n;
}

// The mid points: the tables' (rc_target.c, by name), with each one's address and continuations from mids.map and
// whether a jump can be written there (gen_mids.py). Only a point that has a hook is put in.
#include "rc_mid.h"
extern const rc_mid_name rc_target_mids[];
static const struct {
  const char *name;
  uint64_t address;
  int can;
  uint64_t cont[RC_MID_MAXCONTS];
} k_mids[] = {
#define MID(name, address, can, ...) {name, address, can, {__VA_ARGS__}},
#include "mids.inc"
#undef MID
};
static uint64_t g_mid_cont[RC_MID_POINTS][RC_MID_MAXCONTS];

uint64_t rsl_mid_dispatch(int point, rc_ctx *c, uint64_t *flags) {
  uint32_t f = (uint32_t)*flags;
  int go = rc_mid_call(point, c, &f);
  *flags = (*flags & ~0xffffffffull) | f;
  return go > 0 && go <= RC_MID_MAXCONTS ? g_mid_cont[point][go - 1] : 0;
}

static int g_mid_traps;
int rsl_mid_breakpoints(void) { return g_mid_traps; }

int rsl_install_mids(void) {
  int n = 0;
  for (size_t i = 0; i < sizeof k_mids / sizeof *k_mids; i++) {
    int point = -1;
    for (int k = 0; k < RC_MID_POINTS && rc_target_mids[k].name && point < 0; k++)
      if (!strcmp(rc_target_mids[k].name, k_mids[i].name)) point = k;
    if (point < 0 || !rc_mid_on[point]) continue;  // no hook asked for it
    // Where an edit's jump covers the instruction it is at its moved copy that the point goes in, with a jump:
    // nothing else goes to a copy. Elsewhere a point the table says no jump can be written at has a breakpoint.
    uint64_t at = moved_address(k_mids[i].address);
    int trap = at == k_mids[i].address && !k_mids[i].can;
    if (!at || mid_point(at, point, k_mids[i].name, trap)) continue;
    g_mid_traps += trap;
    memcpy(g_mid_cont[point], k_mids[i].cont, sizeof g_mid_cont[point]);
    n++;
  }
  // With every jump written: a continuation that lands among covered instructions, this point's or another's.
  for (int point = 0; point < RC_MID_POINTS; point++)
    for (int k = 0; k < RC_MID_MAXCONTS; k++)
      if (g_mid_cont[point][k]) g_mid_cont[point][k] = moved_address(g_mid_cont[point][k]);
  return n;
}

// The mods folder (runtime/rc_mods.c, the Mac app's own loader; a mod is a .so here).
int rsl_load_mods(const char *home, const char *game) {
  char dir[2304];
  snprintf(dir, sizeof dir, "%s/mods", home);
  const char *mods = rc_mods_where(dir);
  int n = mods ? rc_mods_count(mods) : 0;
  if (n <= 0) return 0;
  if (game && chdir(game)) fprintf(stderr, "[rs-rearmed] cannot go to the game folder %s\n", game);
  rc_mods_load(mods);
  return n;
}

// The target's edits (gen_edits.py, from edits.map and patch-edits.tsv), from the highest address down.
#define BYTES(n, ...) static const uint8_t k_edit_bytes_##n[] = {__VA_ARGS__};
#define EDIT(group, address, range, check, whole, covered, fixes, can, n)
#include "edits.inc"
#undef BYTES
#undef EDIT
static const rsl_edit k_edits[] = {
#define BYTES(n, ...)
#define EDIT(group, address, range, check, whole, covered, fixes, can, n) \
  {group, address, range, check, whole, covered, fixes, can, sizeof k_edit_bytes_##n, k_edit_bytes_##n},
#include "edits.inc"
#undef BYTES
#undef EDIT
};

// A mod switches an edit group on (rc_mid.h), in its constructor. The runtime's own function (rc_mid.c, under
// another name here: linux/Makefile) says whether the group may be switched on and keeps the switch; the edits are
// then written, and a group that cannot be written whole is off again and refused, as the interface has a group
// refused on a game without it: a mod that went on regardless would have the game's own layouts under its feet.
int rc_edit_enable_translated(const char *group);
extern const rc_edit_name rc_target_edits[];

int rc_edit_enable(const char *group) {
  if (group && rc_edit_is_on(group)) return 0;
  if (rc_edit_enable_translated(group)) return -1;
  if (edits_write(k_edits, (int)(sizeof k_edits / sizeof *k_edits), group) >= 0) return 0;
  for (int k = 0; k < RC_EDIT_GROUPS && rc_target_edits[k].name; k++)
    if (!strcmp(rc_target_edits[k].name, group)) rc_edit_on[k] = 0;
  return -1;
}
#endif  // RSL_PATCH_TEST
