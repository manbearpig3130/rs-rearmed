// The calls native code makes into the game's code (linux/src/direct.c): integer arguments in the six registers
// and on the stack, SSE ones, the four results, a 16-byte aligned stack at the callee, a virtual call. The callees
// here are ordinary C functions, which is what the game's are to the CPU. `make -C linux check` runs it.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint64_t rc_call_guest(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t *xmm0_out);
void rc_call_guest_ex(uint64_t fn, const uint64_t *ints, int nint, const uint64_t *dbls, int ndbl, uint64_t out[4]);
void rc_call_guest_regs(uint64_t fn, const uint64_t gi[6], int ng, const uint64_t dv[8], int nd, const uint64_t *stk,
                        int ns, uint64_t out[4]);
uint64_t ov_vcall_n(uint64_t obj, int slot, const uint64_t *args, int nargs);

static int g_failed;
#define CHECK(what, cond) \
  do { \
    if (!(cond)) { \
      printf("FAILED: %s\n", what); \
      g_failed++; \
    } \
  } while (0)

static uint64_t g_seen[12];
static int g_aligned;

// The stack at a callee's entry is 8 past a multiple of 16; code that keeps SSE values on the stack relies on it.
static __attribute__((noinline)) int aligned_here(void) {
  volatile __int128 probe __attribute__((aligned(16))) = 0;
  return ((uintptr_t)&probe & 15) == 0 && ((uintptr_t)__builtin_frame_address(0) & 15) == 0;
}

static __attribute__((noinline)) uint64_t twelve(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
                                                 uint64_t g, uint64_t h, uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
  uint64_t v[12] = {a, b, c, d, e, f, g, h, i, j, k, l};
  memcpy(g_seen, v, sizeof v);
  g_aligned = aligned_here();
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j + 11 * k + 12 * l;
}

static __attribute__((noinline)) double mixed(uint64_t a, double x, uint64_t b, float y, uint64_t c, uint64_t d, uint64_t e,
                                              uint64_t f, uint64_t g, uint64_t h, double z) {
  g_aligned = aligned_here();
  return (double)(a + b + c + d + e + f + 10 * g + 100 * h) + x + (double)y + z;
}

typedef struct {
  uint64_t lo, hi;
} pair;
static __attribute__((noinline)) pair two(uint64_t a, uint64_t b) { return (pair){a + 1, b + 2}; }

typedef struct {
  double x, y;
} point;
static __attribute__((noinline)) point two_sse(double a, double b) { return (point){a * 2, b * 3}; }

static uint64_t method(uint64_t self, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g) {
  return *(uint64_t *)(uintptr_t)(self + 8) + a + b + c + d + e + f + g;
}

static uint64_t bits(double d) {
  uint64_t u;
  memcpy(&u, &d, 8);
  return u;
}

int main(void) {
  uint64_t in[12];
  for (int n = 0; n <= 12; n++) {
    uint64_t want = 0;
    for (int i = 0; i < 12; i++) in[i] = i < n ? 0x1111111100000000ull * (uint64_t)(i + 1) + (uint64_t)i + 7 : 0;
    for (int i = 0; i < n; i++) want += (uint64_t)(i + 1) * in[i];
    memset(g_seen, 0xff, sizeof g_seen);
    g_aligned = 0;
    uint64_t got = rc_call_guest((uint64_t)(uintptr_t)twelve, in, n, NULL, 0, NULL);
    char what[64];
    snprintf(what, sizeof what, "%d integer arguments", n);
    CHECK(what, memcmp(g_seen, in, (size_t)n * 8) == 0);  // (those not given hold whatever was there)
    if (n == 12) CHECK("the result of twelve", got == want);
    snprintf(what, sizeof what, "the stack's alignment with %d integer arguments", n);
    CHECK(what, g_aligned);
  }

  // Integer and SSE arguments are counted apart: the eight integers of mixed() are a..h, the SSE ones x, y, z.
  uint64_t ints[8] = {1, 2, 3, 4, 5, 6, 7, 8}, dbls[3], out[4] = {0, 0, 0, 0};
  float y = 0.5f;
  dbls[0] = bits(1000.0);
  dbls[1] = 0;
  memcpy(&dbls[1], &y, 4);
  dbls[2] = bits(0.25);
  g_aligned = 0;
  rc_call_guest_ex((uint64_t)(uintptr_t)mixed, ints, 8, dbls, 3, out);
  CHECK("eight integer and three SSE arguments", out[2] == bits(21.0 + 70.0 + 800.0 + 1000.0 + 0.5 + 0.25));
  CHECK("the stack's alignment with both kinds", g_aligned);
  uint64_t x0 = 0;
  rc_call_guest((uint64_t)(uintptr_t)mixed, ints, 8, dbls, 3, &x0);
  CHECK("xmm0 handed back", x0 == out[2]);

  uint64_t ab[2] = {40, 50};
  rc_call_guest_ex((uint64_t)(uintptr_t)two, ab, 2, NULL, 0, out);
  CHECK("rax and rdx", out[0] == 41 && out[1] == 52);
  uint64_t dd[2] = {bits(1.5), bits(2.5)};
  rc_call_guest_ex((uint64_t)(uintptr_t)two_sse, NULL, 0, dd, 2, out);
  CHECK("xmm0 and xmm1", out[2] == bits(3.0) && out[3] == bits(7.5));

  // The general form: the registers given as they are, and the stack's words in argument order.
  uint64_t gi[6] = {1, 2, 3, 4, 5, 6}, none[8] = {0}, stk[6] = {7, 8, 9, 10, 11, 12};
  rc_call_guest_regs((uint64_t)(uintptr_t)twelve, gi, 6, none, 0, stk, 6, out);
  CHECK("the general form", out[0] == 650 && g_seen[6] == 7 && g_seen[11] == 12);
  rc_call_guest_regs((uint64_t)(uintptr_t)twelve, gi, 6, none, 0, stk, 3, out);
  CHECK("an odd number of words on the stack", g_seen[8] == 9 && g_aligned);

  // A virtual call: the object's first word is its table of functions; the object goes first.
  uint64_t table[3] = {0, 0, (uint64_t)(uintptr_t)method};
  uint64_t object[2] = {(uint64_t)(uintptr_t)table, 1000};
  uint64_t args[8] = {(uint64_t)(uintptr_t)object, 1, 2, 3, 4, 5, 6, 7};
  CHECK("a virtual call with eight arguments", ov_vcall_n((uint64_t)(uintptr_t)object, 2, args, 8) == 1028);

  if (g_failed) {
    printf("%d checks failed\n", g_failed);
    return 1;
  }
  printf("calls into the game: all checks passed\n");
  return 0;
}
