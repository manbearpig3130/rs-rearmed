// What the program writes into the game's code (linux/src/patch.c), on code of the test's own: an edit group's four
// forms (bytes changed inside a range, new instructions that fit, longer ones behind a jump with the instructions
// after the range moved, and behind a breakpoint), a group that cannot be written whole taken back, and a mid point
// by a jump, by a breakpoint, and at an instruction an edit moved. `make -C linux check` runs it.
#define RSL_PATCH_TEST
#include "../src/patch.c"

#include <stdlib.h>

static int g_failed;
#define CHECK(what, cond) \
  do { \
    if (!(cond)) { \
      printf("FAILED: %s\n", what); \
      g_failed++; \
    } \
  } while (0)

// The made-up image: a megabyte at an address of its own, as the game's is, so the stubs go just past it.
#define IMAGE 0x200000000ull
#define IMAGE_SIZE 0x100000ull
uint64_t rsl_image_end(void) { return IMAGE + IMAGE_SIZE; }

// What the stubs call (the program's are in patch.c's tables part, which this test leaves out).
void rsl_hook_dispatch(rc_ctx *c, int index) { (void)c, (void)index; }
static int g_mid_point, g_mid_calls;
static uint64_t g_mid_rax;
uint64_t rsl_mid_dispatch(int point, rc_ctx *c, uint64_t *flags) {
  (void)flags;
  g_mid_point = point, g_mid_calls++, g_mid_rax = c->r[0];
  c->r[0] += 1000;
  return 0;
}

static uint8_t *at(unsigned off) { return (uint8_t *)(uintptr_t)(IMAGE + off); }
#define CODE(off, ...) \
  do { \
    static const uint8_t b[] = {__VA_ARGS__}; \
    memcpy(at(off), b, sizeof b); \
  } while (0)
typedef int (*fn)(int);
static int call(unsigned off, int arg) { return ((fn)(uintptr_t)(IMAGE + off))(arg); }
static void rel32(uint8_t *p, uint64_t from_end, uint64_t target) { put32(p, (int64_t)target - (int64_t)from_end); }

enum { F1 = 0x100, F2 = 0x200, F3 = 0x300, F4 = 0x400, H = 0x480, F5 = 0x500, F6 = 0x600, F7 = 0x700, F8 = 0x800, F9 = 0x900, FA = 0xa00 };

int main(void) {
  void *image = mmap((void *)IMAGE, IMAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (image != (void *)IMAGE) {
    perror("no memory for the made-up image");
    return 1;
  }
  CHECK("the check is the SHA-256's first four bytes", check_of((const uint8_t *)"abc", 3) == 0xba7816bfu);
  CHECK("and of more than one block's worth", check_of((const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56) == 0x248d6a61u);

  // F1, F2, F3: mov eax, 1; ret
  CODE(F1, 0xb8, 1, 0, 0, 0, 0xc3);
  CODE(F2, 0xb8, 1, 0, 0, 0, 0xc3);
  CODE(F3, 0xb8, 1, 0, 0, 0, 0xc3);
  // F4: xor eax, eax; mov ecx, [rip + 0x38] (F4 + 0x40: 40); add eax, ecx; ret.  H: add eax, 1; ret
  CODE(F4, 0x31, 0xc0, 0x8b, 0x0d, 0x38, 0, 0, 0, 0x01, 0xc8, 0xc3);
  CODE(F4 + 0x40, 40, 0, 0, 0);
  CODE(H, 0x83, 0xc0, 0x01, 0xc3);
  // F5: xor eax, eax; L: add eax, edi; dec edi; jnz L; ret  (a branch goes to the instruction after the first)
  CODE(F5, 0x31, 0xc0, 0x01, 0xf8, 0xff, 0xcf, 0x75, 0xfa, 0xc3);
  // F6: mov eax, 7; ret.  F7: mov eax, 7; add eax, 1; add eax, 1; ret
  CODE(F6, 0xb8, 7, 0, 0, 0, 0xc3);
  CODE(F7, 0xb8, 7, 0, 0, 0, 0x83, 0xc0, 0x01, 0x83, 0xc0, 0x01, 0xc3);
  // F8: xor eax, eax; add eax, 5; add eax, 1; ret
  CODE(F8, 0x31, 0xc0, 0x83, 0xc0, 0x05, 0x83, 0xc0, 0x01, 0xc3);
  // F9, FA: mov eax, 1; ret
  CODE(F9, 0xb8, 1, 0, 0, 0, 0xc3);
  CODE(FA, 0xb8, 1, 0, 0, 0, 0xc3);
  if (mprotect(image, IMAGE_SIZE, PROT_READ | PROT_EXEC)) return 1;
  CHECK("the made-up functions run", call(F1, 0) == 1 && call(F4, 0) == 40 && call(F5, 3) == 6 && call(F8, 0) == 6);

  // The edits, from the highest address down as the build's table has them.
  static const uint8_t f1_change[] = {1, 1, 7};                       // at +1, one byte: mov eax, 7
  static const uint8_t f2_new[] = {0x31, 0xc0};                       // xor eax, eax
  static const uint8_t f3_new[] = {0xb8, 2, 0, 0, 0, 0x83, 0xc0, 3};  // mov eax, 2; add eax, 3
  // mov eax, 2; test edi, edi; je F4+2 (the instruction after the range, which the jump covers); add eax, 100; call H
  static uint8_t f4_new[] = {0xb8, 2, 0, 0, 0, 0x85, 0xff, 0x74, 0xf9, 0x83, 0xc0, 100, 0xe8, 0, 0, 0, 0};
  rel32(f4_new + 13, IMAGE + F4 + 17, IMAGE + H);
  static const uint8_t f5_new[] = {0xb8, 100, 0, 0, 0};  // mov eax, 100
  static const uint8_t f8_new[] = {0xb8, 10, 0, 0, 0};   // mov eax, 10
  static const uint8_t f9_change[] = {1, 1, 9};
  rsl_edit edits[] = {
      {"bad", IMAGE + FA, 5, 0, 0, 5, 0, 1, sizeof f9_change, f9_change},
      {"bad", IMAGE + F9, 5, 0, 0, 5, 0, 1, sizeof f9_change, f9_change},
      {"moved", IMAGE + F8, 2, 0, 1, 5, 0, 1, sizeof f8_new, f8_new},
      {"good", IMAGE + F5, 2, 0, 1, 2, 0, 0, sizeof f5_new, f5_new},
      {"good", IMAGE + F4, 2, 0, 1, 8, 3, 1, sizeof f4_new, f4_new},
      {"good", IMAGE + F3, 5, 0, 1, 5, 0, 1, sizeof f3_new, f3_new},
      {"good", IMAGE + F2, 5, 0, 1, 5, 0, 1, sizeof f2_new, f2_new},
      {"good", IMAGE + F1, 5, 0, 0, 5, 0, 1, sizeof f1_change, f1_change},
  };
  int count = (int)(sizeof edits / sizeof *edits);
  for (int i = 0; i < count; i++) edits[i].check = check_of((const uint8_t *)edits[i].address, edits[i].range);
  edits[1].check ^= 1;  // F9's bytes are not the ones its edit was made for

  CHECK("a group is written", edits_write(edits, count, "good") == 5);
  CHECK("bytes changed inside a range", call(F1, 0) == 7);
  CHECK("new instructions shorter than the range", call(F2, 0) == 0 && at(F2)[2] == 0x90 && at(F2)[5] == 0xc3);
  CHECK("longer ones, the range holding a jump", call(F3, 0) == 5 && at(F3)[0] == 0xe9);
  CHECK("longer ones in a range under five bytes: the next instruction moved, its distance and the new call's corrected",
        call(F4, 1) == 143);
  CHECK("and a new branch to that instruction going to its moved copy", call(F4, 0) == 42);
  CHECK("longer ones where no jump can be written: a breakpoint, and the branch into the next instruction still lands",
        at(F5)[0] == 0xcc && call(F5, 3) == 106 && call(F5, 1) == 101);
  CHECK("a group of another name is left alone", call(F8, 0) == 6 && call(F9, 0) == 1);

  uint8_t before[6];
  memcpy(before, at(FA), sizeof before);
  CHECK("a group with an edit the game's bytes are not right for is refused", edits_write(edits, count, "bad") == -1);
  CHECK("and the edits of it that were written are taken back", call(FA, 0) == 1 && !memcmp(before, at(FA), sizeof before) && call(F9, 0) == 1);
  edits[2].covered = 6;
  CHECK("an edit this build reads otherwise than its table is refused", edits_write(edits, count, "moved") == -1 && call(F8, 0) == 6);
  edits[2].covered = 5;
  CHECK("the group with the mid point's instruction in it is written", edits_write(edits, count, "moved") == 1 && call(F8, 0) == 16);

  // Mid points: by a jump, by a breakpoint at a one-byte instruction, and at an instruction an edit moved.
  CHECK("a mid point by a jump", mid_point(IMAGE + F7 + 5, 2, "by a jump", 0) == 0 && call(F7, 0) == 1009 && g_mid_point == 2 && g_mid_rax == 7);
  CHECK("a mid point by a breakpoint", mid_point(IMAGE + F6 + 5, 3, "by a breakpoint", 1) == 0 && at(F6)[5] == 0xcc && call(F6, 0) == 1007 &&
                                           g_mid_point == 3 && g_mid_rax == 7);
  uint64_t copy = moved_address(IMAGE + F8 + 2);
  CHECK("an instruction an edit moved is found at its copy", copy && copy != IMAGE + F8 + 2 && in_stubs(copy));
  CHECK("a mid point there", mid_point(copy, 4, "at a moved instruction", 0) == 0 && call(F8, 0) == 1016 && g_mid_point == 4 && g_mid_rax == 10);
  CHECK("each ran once", g_mid_calls == 3);
  CHECK("an address in a replaced range leads nowhere", moved_address(IMAGE + F3 + 2) == 0);

  if (g_failed) return 1;
  printf("patch test: an edit group's four forms, a refused group taken back, mid points by a jump, by a breakpoint and at a moved instruction\n");
  return 0;
}
