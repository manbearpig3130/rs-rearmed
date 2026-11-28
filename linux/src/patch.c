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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
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

// The game's code at ADDRESS becomes `jmp STUB` (5 bytes).
static int write_jump(uint64_t address, const uint8_t *stub, const char *name) {
  uint8_t jump[5] = {0xe9, 0, 0, 0, 0};
  put32(jump + 1, (int64_t)(uintptr_t)stub - (int64_t)(address + 5));
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

int rsl_override(uint64_t address, void (*native)(rc_ctx *), const char *name) {
  uint8_t *stub = far_jump_with(0x49ba, (uint64_t)(uintptr_t)native, rsl_override_entry);
  return stub ? write_jump(address, stub, name) : -1;
}

// --- moving a function's first instructions ---
// A hook runs before a function and the function then runs itself, so the instructions the jump covers have to run
// somewhere: they are copied behind the stubs and followed by a jump back to the first one not covered. The
// instructions a function starts with are few kinds (pushes, moves, a stack adjustment, a call), and this reads
// only those: one it does not know, or a branch, and the function is left alone with a line saying so. An
// instruction that addresses by the instruction pointer has its displacement corrected for the new place.
//
// The length of the instruction at P, or -1. *DISP: the offset in it of a displacement to correct, or -1.
static int instruction(const uint8_t *p, int *disp) {
  int i = 0, word = 0, wide = 0, modrm = 0, imm = 0;
  *disp = -1;
  for (; p[i] == 0x66 || p[i] == 0xf2 || p[i] == 0xf3; i++) {
    if (p[i] == 0x66) word = 1;
    if (i == 3) return -1;
  }
  if ((p[i] & 0xf0) == 0x40) wide = (p[i++] >> 3) & 1;  // REX
  uint8_t op = p[i++];
  if (op == 0x0f) {
    uint8_t op2 = p[i++];
    static const uint8_t k[] = {0x10, 0x11, 0x12, 0x13, 0x16, 0x17, 0x28, 0x29, 0x2a, 0x2c, 0x2d, 0x2e, 0x2f, 0x51, 0x54, 0x57, 0x58,
                                0x59, 0x5a, 0x5b, 0x5c, 0x5e, 0x6e, 0x6f, 0x7e, 0x7f, 0xaf, 0xb6, 0xb7, 0xbe, 0xbf, 0xd6};
    for (size_t n = 0; n < sizeof k; n++) modrm |= k[n] == op2;
    if (op2 >= 0x40 && op2 <= 0x4f) modrm = 1;  // cmov
    if (!modrm) return -1;
  } else if ((op >= 0x50 && op <= 0x5f) || op == 0x90) {  // push, pop, nop
  } else if (op >= 0xb0 && op <= 0xb7) {
    imm = 1;
  } else if (op >= 0xb8 && op <= 0xbf) {
    imm = wide ? 8 : word ? 2 : 4;
  } else if (op == 0xe8) {  // call rel32: the displacement is from the instruction's end, as an address by rip is
    *disp = i;
    return i + 4;
  } else {
    static const uint8_t plain[] = {0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x20, 0x21, 0x22, 0x23, 0x28, 0x29, 0x2a, 0x2b,
                                    0x30, 0x31, 0x32, 0x33, 0x38, 0x39, 0x3a, 0x3b, 0x63, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
                                    0x8b, 0x8d};
    for (size_t n = 0; n < sizeof plain; n++) modrm |= plain[n] == op;
    if (op == 0x80 || op == 0x83 || op == 0xc0 || op == 0xc1 || op == 0xc6) modrm = 1, imm = 1;
    else if (op == 0x81 || op == 0xc7) modrm = 1, imm = word ? 2 : 4;
    else if (op == 0xf6 || op == 0xf7) {  // test has an immediate, the rest of the group none
      modrm = 1;
      if (((p[i] >> 3) & 7) < 2) imm = op == 0xf6 ? 1 : word ? 2 : 4;
    } else if (op == 0xfe || op == 0xff) {
      modrm = 1;
      int reg = (p[i] >> 3) & 7;
      if (op == 0xff && reg >= 2 && reg <= 5) return -1;  // an indirect call or jump
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

// The first instructions of the function at ADDRESS that a 5-byte jump covers, moved; then a jump to the rest.
static uint8_t *moved_start(uint64_t address, const char *name) {
  const uint8_t *from = (const uint8_t *)address;
  uint8_t *to = near_take(64);
  if (!to) return NULL;
  int done = 0;
  while (done < 5) {
    int disp, n = instruction(from + done, &disp);
    if (n < 0 || done + n > 40) {
      fprintf(stderr, "[rs-rearmed] %s at 0x%llx: its instruction at +%d is not one this build moves (bytes %02x %02x %02x): not hooked\n",
              name, (unsigned long long)address, done, from[done], from[done + 1], from[done + 2]);
      return NULL;
    }
    memcpy(to + done, from + done, (size_t)n);
    if (disp >= 0) {
      int32_t d;
      memcpy(&d, from + done + disp, 4);
      put32(to + done + disp, (int64_t)d + ((int64_t)(uintptr_t)from - (int64_t)(uintptr_t)to));
    }
    done += n;
  }
  to[done] = 0xe9;
  put32(to + done + 1, (int64_t)(address + (uint64_t)done) - (int64_t)(uintptr_t)(to + done + 5));
  return to;
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

// --- what this build puts in ---
// The override modules, compiled as they are (tools/ws7/recomp/overrides): `online`, the game never calls home; and
// of `mods`, the built-in fixes, the functions it replaces whole. And every hook point of the tables.
#include "rc_target_names.h"
#include "rc_ov.h"
#define OV(name) void RC_FN_##name(rc_ctx *);
#include "overrides_online.inc"
#undef OV
// The fixes module's own table of the functions it replaces (overrides/mods/mods.c).
extern const ov_entry ov_mods_entries[];
extern const int OV_MODS_COUNT;
// Two of them are half of the batched DLC mounting, whose other half is mid points: left as the game has them
// until those are in.
extern void (*rsl_sng_verify_original)(rc_ctx *);  // direct.c
static int waits_for_mid_points(const char *name) { return !strcmp(name, "dlc_update") || !strcmp(name, "asset_loader_thread"); }

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
#undef OV
  };
  int n = 0;
  for (size_t i = 0; i < sizeof list / sizeof *list; i++, n++)
    if (rsl_override(list[i].address, list[i].native, list[i].name)) return -1;
  for (int i = 0; i < OV_MODS_COUNT; i++) {
    const ov_entry *e = &ov_mods_entries[i];
    if (waits_for_mid_points(e->name)) continue;
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
