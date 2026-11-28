// Who answers each of the image's imports. See rsl.h.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dw.h"
#include "rsl.h"

#include "imports_table.inc"  // rsl_rows[], rsl_classes[]: linux/darwin-imports.tsv, through gen_import_table.py

#if !defined(__x86_64__)
#error "the game's own code runs on x86-64 alone: other CPUs get the translated build"
#endif

#define MAX_IMPORTS 2048
#define TRAP_STRIDE 0x2000  // a read a little past an object's start still names that object
#define STUB 32

static rsl_import g_imports[MAX_IMPORTS];
static int g_nimports;
static int g_count;
static uint8_t *g_traps;    // no access: an unanswered import's address is in here
static uint8_t *g_stubs;    // with counting on
static uint64_t *g_counts;
static uint32_t g_surveyed[MAX_IMPORTS];  // calls of each unanswered import, in a survey

// The host's libraries that the `same` rows are in, besides the C library.
static const char *const k_libs[] = {"libm.so.6", "libstdc++.so.6", "libgcc_s.so.1"};

// The project's replacement libraries, each built for Linux as a shared library beside this program and answering
// the imports of one of the Mac game's libraries. One that is not there leaves its imports without an answer.
static struct {
  const char *library, *file;
  void *handle;
  int tried;
} g_shims[] = {{"libsteam_api", "libsteam_api.so", NULL, 0},
              {"OpenGL", "libRSOpenGL.so", NULL, 0},
              {"Cg", "libCg.so", NULL, 0},
              {"libBink2Macx64", "libBink2.so", NULL, 0}};

static void *shim_symbol(const char *library, const char *symbol) {
  for (size_t i = 0; i < sizeof g_shims / sizeof *g_shims; i++) {
    if (strcmp(g_shims[i].library, library)) continue;
    if (!g_shims[i].tried) {
      g_shims[i].tried = 1;
      char path[PATH_MAX];
      ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
      if (n < 0) return NULL;
      path[n] = 0;
      char *slash = strrchr(path, '/');
      if (!slash || (size_t)(slash - path) + 2 + strlen(g_shims[i].file) > sizeof path) return NULL;
      strcpy(slash + 1, g_shims[i].file);
      if (access(path, R_OK) == 0 && !(g_shims[i].handle = dlopen(path, RTLD_NOW | RTLD_LOCAL)))
        fprintf(stderr, "[rs-rearmed] %s\n", dlerror());
    }
    return g_shims[i].handle ? dlsym(g_shims[i].handle, symbol + (symbol[0] == '_')) : NULL;
  }
  return NULL;
}

void *rsl_shim_function(const char *library, const char *name) { return shim_symbol(library, name); }

void rsl_imports_begin(int count_calls) {
  g_count = count_calls;
  g_traps = mmap(NULL, (size_t)MAX_IMPORTS * TRAP_STRIDE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (g_traps == MAP_FAILED) {
    perror("[rs-rearmed] mmap");
    exit(1);
  }
  if (count_calls) {
    g_stubs = mmap(NULL, MAX_IMPORTS * STUB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    g_counts = calloc(MAX_IMPORTS, sizeof *g_counts);
    if (g_stubs == MAP_FAILED || !g_counts) {
      perror("[rs-rearmed] mmap");
      exit(1);
    }
  }
  for (size_t i = 0; i < sizeof k_libs / sizeof *k_libs; i++)
    if (!dlopen(k_libs[i], RTLD_NOW | RTLD_GLOBAL)) fprintf(stderr, "[rs-rearmed] %s\n", dlerror());
  dw_init();
}

void rsl_imports_end(void) {
  if (g_stubs) mprotect(g_stubs, MAX_IMPORTS * STUB, PROT_READ | PROT_EXEC);
}

static const struct rsl_row *row_of(const char *library, const char *symbol) {
  size_t lo = 0, hi = RSL_NROWS;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    int c = strcmp(rsl_rows[mid].symbol, symbol);
    if (!c) c = strcmp(rsl_rows[mid].library, library);
    if (!c) return &rsl_rows[mid];
    if (c < 0) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
}

// lock inc [the import's counter]; jmp the answer. r11 is no argument's register and no callee's to keep.
static uint64_t counting_stub(const rsl_import *im) {
  size_t i = (size_t)(im - g_imports);
  uint8_t *s = g_stubs + i * STUB;
  uint64_t counter = (uint64_t)&g_counts[i];
  s[0] = 0x49, s[1] = 0xbb;  // movabs r11, &counter
  memcpy(s + 2, &counter, 8);
  s[10] = 0xf0, s[11] = 0x49, s[12] = 0xff, s[13] = 0x03;  // lock inc qword [r11]
  s[14] = 0x49, s[15] = 0xbb;  // movabs r11, the answer
  memcpy(s + 16, &im->at, 8);
  s[24] = 0x41, s[25] = 0xff, s[26] = 0xe3;  // jmp r11
  return (uint64_t)s;
}

rsl_import *rsl_answer(const char *library, const char *symbol, int lazy) {
  rsl_import *im = NULL;
  for (int i = 0; i < g_nimports && !im; i++)
    if (!strcmp(g_imports[i].symbol, symbol) && !strcmp(g_imports[i].library, library)) im = &g_imports[i];
  if (!im) {
    if (g_nimports == MAX_IMPORTS) {
      fprintf(stderr, "[rs-rearmed] more than %d imports\n", MAX_IMPORTS);
      exit(1);
    }
    im = &g_imports[g_nimports];
    const struct rsl_row *row = row_of(library, symbol);
    im->library = row ? row->library : strdup(library);
    im->symbol = row ? row->symbol : strdup(symbol);
    im->cls = row ? rsl_classes[row->cls] : "?";
    // Mach-O writes a C name with an underscore in front; the host's has none.
    void *p = dw_function(symbol);
    if (!p) p = dw_data(symbol);
    if (p) {
      im->how = RSL_LAYER;
    } else if (row && row->cls == RSL_CLASS_SAME && (p = dlsym(RTLD_DEFAULT, symbol + (symbol[0] == '_')))) {
      im->how = RSL_HOST;
    } else if (row && row->cls == RSL_CLASS_SHIM && (p = shim_symbol(library, symbol))) {
      im->how = RSL_SHIM;
    } else {
      im->how = RSL_NONE;
      p = g_traps + (size_t)g_nimports * TRAP_STRIDE;
    }
    im->at = (uint64_t)p;
    g_nimports++;
  }
  if (lazy && !im->called) {
    im->called = 1;
    if (g_count && im->how != RSL_NONE) im->stub = counting_stub(im);
  }
  return im;
}

const rsl_import *rsl_import_at(uint64_t addr) {
  uint64_t base = (uint64_t)g_traps;
  if (!g_traps || addr < base || addr >= base + (uint64_t)MAX_IMPORTS * TRAP_STRIDE) return NULL;
  uint64_t i = (addr - base) / TRAP_STRIDE;
  return i < (uint64_t)g_nimports ? &g_imports[i] : NULL;
}

int rsl_survey_note(const rsl_import *im) { return __atomic_fetch_add(&g_surveyed[im - g_imports], 1, __ATOMIC_RELAXED) == 0; }

void rsl_report_survey(FILE *out) {
  int n = 0;
  for (int i = 0; i < g_nimports; i++) n += g_surveyed[i] != 0;
  fprintf(out, "survey: %d functions with no answer were called\n", n);
  for (int c = 0; c <= RSL_NCLASSES; c++)
    for (int i = 0; i < g_nimports; i++) {
      if (!g_surveyed[i]) continue;
      int k = 0;
      while (k < RSL_NCLASSES && strcmp(rsl_classes[k], g_imports[i].cls)) k++;
      if (k == c) fprintf(out, "%9u  %-8s %-14s %s\n", g_surveyed[i], g_imports[i].cls, g_imports[i].library, g_imports[i].symbol);
    }
}

const rsl_import *rsl_imports(int *n) {
  *n = g_nimports;
  return g_imports;
}

uint64_t rsl_import_calls(const rsl_import *im) { return g_counts ? g_counts[im - g_imports] : 0; }

void rsl_report_binding(FILE *out) {
  int called = 0, how[4] = {0, 0, 0, 0}, by_class[RSL_NCLASSES + 1] = {0};
  for (int i = 0; i < g_nimports; i++) {
    const rsl_import *im = &g_imports[i];
    called += im->called;
    how[im->how]++;
    if (im->how != RSL_NONE) continue;
    int c = 0;
    while (c < RSL_NCLASSES && strcmp(rsl_classes[c], im->cls)) c++;
    by_class[c]++;
  }
  fprintf(out, "imports: %d (%d called): %d the host's own, %d the Darwin layer's, %d the project's libraries', %d with no "
               "answer yet\n",
          g_nimports, called, how[RSL_HOST], how[RSL_LAYER], how[RSL_SHIM], how[RSL_NONE]);
  if (!how[RSL_NONE]) return;
  fprintf(out, "  no answer yet, by class:");
  for (int c = 0; c <= RSL_NCLASSES; c++)
    if (by_class[c]) fprintf(out, " %s %d", c < RSL_NCLASSES ? rsl_classes[c] : "not in the table", by_class[c]);
  fprintf(out, "\n");
}

static int by_calls(const void *a, const void *b) {
  uint64_t x = rsl_import_calls(*(const rsl_import *const *)a), y = rsl_import_calls(*(const rsl_import *const *)b);
  if (x != y) return x < y ? 1 : -1;
  return strcmp((*(const rsl_import *const *)a)->symbol, (*(const rsl_import *const *)b)->symbol);
}

void rsl_report_calls(FILE *out) {
  if (!g_counts) return;
  const rsl_import *list[MAX_IMPORTS];
  int n = 0;
  for (int i = 0; i < g_nimports; i++)
    if (g_counts[i]) list[n++] = &g_imports[i];
  qsort(list, (size_t)n, sizeof *list, by_calls);
  fprintf(out, "imports called: %d\n", n);
  for (int i = 0; i < n; i++)
    fprintf(out, "%11llu  %-10s %-36s %-8s %s\n", (unsigned long long)rsl_import_calls(list[i]), list[i]->library,
            list[i]->symbol, list[i]->cls,
            list[i]->how == RSL_LAYER ? "the layer's" : list[i]->how == RSL_SHIM ? "the project's library" : "the host's own");
}
