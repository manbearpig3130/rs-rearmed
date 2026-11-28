// The image mapped at its own address, bound, and its initializers run. See rsl.h. The form is that of
// shims/game/harness/rsimage.c, which does the same in an Intel process on a Mac.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

#include "macho.h"
#include "rsl.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define MAX_SEGS 16
#define MAX_LIBS 64

static rsl_info g_info;
static uint64_t *g_inits;  // __mod_init_func, in the image's memory
static int g_ninits;
static volatile int g_running = -1;  // the initializer being run
static const char *g_libs[MAX_LIBS];  // by ordinal - 1
static int g_survey;  // rsl_survey: an unanswered function is noted and returns 0
static uint64_t g_lazy_lo, g_lazy_hi;  // __la_symbol_ptr: the pointers the image's calls of its imports go through
static int g_nlibs;

static int fail(const char *path, const char *why) {
  fprintf(stderr, "[rs-rearmed] %s: %s\n", path, why);
  return -1;
}

static uint64_t uleb(const uint8_t **p) {
  uint64_t r = 0;
  int shift = 0;
  uint8_t b;
  do {
    b = *(*p)++;
    r |= (uint64_t)(b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  return r;
}

static int64_t sleb(const uint8_t **p) {
  int64_t r = 0;
  int shift = 0;
  uint8_t b;
  do {
    b = *(*p)++;
    r |= (int64_t)(b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  if (shift < 64 && (b & 0x40)) r |= -((int64_t)1 << shift);
  return r;
}

// `OpenGL` of .../OpenGL.framework/Versions/A/OpenGL, `libSystem` of /usr/lib/libSystem.B.dylib: the table's names.
static const char *library_name(const char *path) {
  const char *base = strrchr(path, '/');
  char *name = strdup(base ? base + 1 : path);
  size_t n = strlen(name);
  if (n > 6 && !strcmp(name + n - 6, ".dylib")) {
    name[n - 6] = 0;
    char *dot = strrchr(name, '.');  // libstdc++.6, libSystem.B, libobjc.A
    if (dot && dot != name && dot[1] && (!dot[2] || strspn(dot + 1, "0123456789") == strlen(dot + 1))) *dot = 0;
  }
  return name;
}

static int bind_one(uint64_t addr, int ordinal, const char *name, int64_t addend) {
  if (!name || addr < g_info.lo || addr + 8 > g_info.hi) {
    fprintf(stderr, "[rs-rearmed] a bind of %s at 0x%llx, outside the image\n", name ? name : "no symbol",
            (unsigned long long)addr);
    return -1;
  }
  const char *library = ordinal >= 1 && ordinal <= g_nlibs ? g_libs[ordinal - 1] : "?";
  // A call's pointer by where it is, not by which table binds it: the operators new and delete, which another
  // image may replace, have theirs bound ahead of time by the ordinary table.
  int lazy = addr >= g_lazy_lo && addr < g_lazy_hi;
  const rsl_import *im = rsl_answer(library, name, lazy);
  *(uint64_t *)addr = (lazy && im->stub ? im->stub : im->at) + (uint64_t)addend;
  return 0;
}

// dyld's bind opcodes. segs[] are the segments' addresses, in the file's order.
static int run_binds(const uint8_t *p, const uint8_t *end, const uint64_t *segs, int nsegs, int lazy) {
  const char *name = NULL;
  uint64_t addr = 0;
  int64_t addend = 0;
  int ordinal = 0;
  while (p < end) {
    uint8_t op = *p & BIND_OPCODE_MASK, imm = *p & BIND_IMMEDIATE_MASK;
    p++;
    switch (op) {
      case BIND_OPCODE_DONE:
        if (!lazy) return 0;
        break;  // lazy binds are records that each end in DONE
      case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
        ordinal = imm;
        break;
      case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
        ordinal = (int)uleb(&p);
        break;
      case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
        ordinal = imm ? (int8_t)(0xf0 | imm) : 0;  // this image, or a flat lookup: not an import
        break;
      case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
        name = (const char *)p;
        p += strlen(name) + 1;
        break;
      case BIND_OPCODE_SET_TYPE_IMM:
        break;
      case BIND_OPCODE_SET_ADDEND_SLEB:
        addend = sleb(&p);
        break;
      case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
        addr = (imm < nsegs ? segs[imm] : 0) + uleb(&p);
        break;
      case BIND_OPCODE_ADD_ADDR_ULEB:
        addr += uleb(&p);
        break;
      case BIND_OPCODE_DO_BIND:
        if (bind_one(addr, ordinal, name, addend)) return -1;
        addr += 8;
        break;
      case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
        if (bind_one(addr, ordinal, name, addend)) return -1;
        addr += 8 + uleb(&p);
        break;
      case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
        if (bind_one(addr, ordinal, name, addend)) return -1;
        addr += 8 + (uint64_t)imm * 8;
        break;
      case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: {
        uint64_t count = uleb(&p), skip = uleb(&p);
        for (uint64_t i = 0; i < count; i++, addr += 8 + skip)
          if (bind_one(addr, ordinal, name, addend)) return -1;
        break;
      }
      default:
        fprintf(stderr, "[rs-rearmed] an unknown bind opcode, 0x%x\n", op);
        return -1;
    }
  }
  return 0;
}

// C++ exceptions thrown and caught inside the image. The host's unwinder knows the ELF objects the dynamic linker
// loaded; the image's own DWARF tables (__TEXT,__eh_frame) are registered with it by hand. It is the unwinder
// libstdc++ throws through that must be told, so libgcc's function is looked up, not linked.
static int register_unwind(uint64_t eh, uint64_t eh_size) {
  void (*reg)(void *) = (void (*)(void *))dlsym(RTLD_DEFAULT, "__register_frame");
  if (!reg || !eh || eh_size < 8) return 0;
  // The unwinder reads entries until one of length zero. The section is the last of __TEXT, and the zeros that
  // fill its page end it; with none, there is nothing to register in place.
  if (eh + eh_size + 4 > g_info.text_hi || *(const uint32_t *)(eh + eh_size) != 0) return 0;
  reg((void *)eh);
  return 1;
}

int rsl_initializers_with_unwind(void) {
  const void *(*find)(void *, void *) = (const void *(*)(void *, void *))dlsym(RTLD_DEFAULT, "_Unwind_Find_FDE");
  if (!find) return 0;
  int n = 0;
  for (int i = 0; i < g_ninits; i++) {
    void *bases[3];
    n += find((void *)g_inits[i], bases) != NULL;
  }
  return n;
}

// A fault ends the run and says what it was. An unanswered import is an address in a region with no access
// (imports.c), so a call or a read of it lands here and is named.
void rsl_survey(int on) { g_survey = on; }

static void on_fault(int sig, siginfo_t *si, void *ctx) {
  ucontext_t *uc = ctx;
  uint64_t pc = (uint64_t)uc->uc_mcontext.gregs[REG_RIP], sp = (uint64_t)uc->uc_mcontext.gregs[REG_RSP];
  uint64_t addr = (uint64_t)si->si_addr;
  char in[96] = "";
  int i = g_running;
  if (i >= 0)
    snprintf(in, sizeof in, ", in initializer %d of %d, at 0x%llx", i + 1, g_ninits, (unsigned long long)g_inits[i]);
  const rsl_import *im = sig == SIGSEGV || sig == SIGBUS ? rsl_import_at(addr) : NULL;
  if (im && pc == addr && g_survey) {  // noted once, then as if it had returned 0
    if (rsl_survey_note(im))
      dprintf(2, "[survey] %s %s (%s), from 0x%llx%s\n", im->library, im->symbol, im->cls,
              (unsigned long long)*(const uint64_t *)sp, in);
    uc->uc_mcontext.gregs[REG_RIP] = (greg_t) * (const uint64_t *)sp;
    uc->uc_mcontext.gregs[REG_RSP] = (greg_t)(sp + 8);
    uc->uc_mcontext.gregs[REG_RAX] = 0;
    if (uc->uc_mcontext.fpregs) memset(&uc->uc_mcontext.fpregs->_xmm[0], 0, sizeof uc->uc_mcontext.fpregs->_xmm[0]);
    return;
  }
  if (im && pc == addr) {  // sp holds the call's return address
    dprintf(2, "[rs-rearmed] the game called %s %s (%s), which has no answer yet\n[rs-rearmed]   from 0x%llx%s\n",
            im->library, im->symbol, im->cls, (unsigned long long)*(const uint64_t *)sp, in);
    _exit(3);
  }
  if (im) {
    dprintf(2, "[rs-rearmed] the game read %s %s (%s), which has no answer yet\n[rs-rearmed]   at 0x%llx%s\n",
            im->library, im->symbol, im->cls, (unsigned long long)pc, in);
    _exit(3);
  }
  if (g_survey) rsl_report_survey(stderr);
  dprintf(2, "[rs-rearmed] %s: address 0x%llx, at 0x%llx (%s)%s\n", strsignal(sig), (unsigned long long)addr,
          (unsigned long long)pc, pc >= g_info.lo && pc < g_info.hi ? "the game's code" : "not the game's code", in);
  // Outside the game: which library or mod, and where in it, and who called (the frames that keep a frame pointer)
  Dl_info where;
  if (!(pc >= g_info.lo && pc < g_info.hi) && dladdr((void *)(uintptr_t)pc, &where) && where.dli_fname) {
    const char *base = strrchr(where.dli_fname, '/');
    dprintf(2, "[rs-rearmed]   in %s at +0x%llx", base ? base + 1 : where.dli_fname,
            (unsigned long long)(pc - (uint64_t)(uintptr_t)where.dli_fbase));
    if (where.dli_sname) dprintf(2, " (%s + 0x%llx)", where.dli_sname, (unsigned long long)(pc - (uint64_t)(uintptr_t)where.dli_saddr));
    dprintf(2, "\n");
  }
  _exit(4);
}

static void watch_faults(void) {
  static uint8_t stack[1 << 16];  // its own: a stack that ran out is reported too
  stack_t ss = {.ss_sp = stack, .ss_size = sizeof stack};
  sigaltstack(&ss, NULL);
  struct sigaction sa = {0};
  sa.sa_sigaction = on_fault;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
}

int rsl_load(const char *path, int count_calls, rsl_info *info) {
  memset(&g_info, 0, sizeof g_info);
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    perror(path);
    return -1;
  }
  struct stat st;
  fstat(fd, &st);
  size_t size = (size_t)st.st_size;
  const uint8_t *file = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (file == MAP_FAILED) return fail(path, "cannot be read");
  const struct mach_header_64 *mh = (const void *)file;
  if (size < sizeof *mh || mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_X86_64 ||
      mh->sizeofcmds > size - sizeof *mh)
    return fail(path, "not an x86-64 Mach-O (the decrypted image is what tools/steamless/decrypt.py writes)");

  const struct segment_command_64 *seg[MAX_SEGS];
  uint64_t segs[MAX_SEGS], lo = ~0ull, hi = 0, inits = 0, eh = 0, eh_size = 0;
  int nsegs = 0;
  const struct dyld_info_command *dyld = NULL;
  const uint8_t *lc = file + sizeof *mh;
  for (uint32_t i = 0; i < mh->ncmds; i++, lc += ((const struct load_command *)lc)->cmdsize) {
    const struct load_command *c = (const void *)lc;
    if (c->cmdsize < sizeof *c || lc + c->cmdsize > file + sizeof *mh + mh->sizeofcmds)
      return fail(path, "its load commands run past their end");
    if (c->cmd == LC_DYLD_INFO || c->cmd == LC_DYLD_INFO_ONLY) dyld = (const void *)c;
    if (c->cmd == LC_LOAD_DYLIB || c->cmd == LC_LOAD_WEAK_DYLIB || c->cmd == LC_REEXPORT_DYLIB ||
        c->cmd == LC_LAZY_LOAD_DYLIB || c->cmd == LC_LOAD_UPWARD_DYLIB) {
      if (g_nlibs < MAX_LIBS) g_libs[g_nlibs++] = library_name((const char *)lc + ((const struct dylib_command *)c)->name);
    }
    if (c->cmd != LC_SEGMENT_64) continue;
    const struct segment_command_64 *sg = (const void *)c;
    if (nsegs == MAX_SEGS) return fail(path, "more segments than expected");
    seg[nsegs] = sg;
    segs[nsegs++] = sg->vmaddr;
    if (sg->fileoff > size || sg->filesize > size - sg->fileoff || sg->filesize > sg->vmsize)
      return fail(path, "a segment runs past the file's end");
    if (!strcmp(sg->segname, "__PAGEZERO") || !sg->vmsize) continue;
    if (sg->vmaddr < lo) lo = sg->vmaddr;
    if (sg->vmaddr + sg->vmsize > hi) hi = sg->vmaddr + sg->vmsize;
    if (!strcmp(sg->segname, "__TEXT")) g_info.text_lo = sg->vmaddr, g_info.text_hi = sg->vmaddr + sg->vmsize;
    const struct section_64 *sect = (const void *)(sg + 1);
    for (uint32_t k = 0; k < sg->nsects; k++) {
      if ((sect[k].flags & SECTION_TYPE) == S_MOD_INIT_FUNC_POINTERS) inits = sect[k].addr, g_ninits = (int)(sect[k].size / 8);
      if ((sect[k].flags & SECTION_TYPE) == S_LAZY_SYMBOL_POINTERS) g_lazy_lo = sect[k].addr, g_lazy_hi = sect[k].addr + sect[k].size;
      if (!strcmp(sg->segname, "__TEXT") && !strncmp(sect[k].sectname, "__eh_frame", 16)) eh = sect[k].addr, eh_size = sect[k].size;
    }
  }
  if (!dyld || hi <= lo) return fail(path, "no segments, or no dyld information");
  if (dyld->bind_off > size || dyld->bind_size > size - dyld->bind_off || dyld->lazy_bind_off > size ||
      dyld->lazy_bind_size > size - dyld->lazy_bind_off)
    return fail(path, "its bind tables run past the file's end");
  uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE) - 1;
  hi = (hi + page) & ~page;  // the last segment, __LINKEDIT, ends where its bytes do
  for (int i = 0; i < nsegs; i++)
    if (seg[i]->vmaddr & page) return fail(path, "its segments do not fit this machine's pages");

  // One region at the image's own address: nothing is rebased, so it must be there and nowhere else.
  void *at = mmap((void *)lo, hi - lo, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (at != (void *)lo) {
    if (at != MAP_FAILED) munmap(at, hi - lo);
    fprintf(stderr, "[rs-rearmed] 0x%llx to 0x%llx, where the game's image belongs, is not free in this process\n",
            (unsigned long long)lo, (unsigned long long)hi);
    return -1;
  }
  g_info.lo = lo, g_info.hi = hi;
  for (int i = 0; i < nsegs; i++)
    if (seg[i]->vmsize && strcmp(seg[i]->segname, "__PAGEZERO"))
      memcpy((void *)seg[i]->vmaddr, file + seg[i]->fileoff, seg[i]->filesize);

  rsl_imports_begin(count_calls);
  if (run_binds(file + dyld->bind_off, file + dyld->bind_off + dyld->bind_size, segs, nsegs, 0) ||
      run_binds(file + dyld->lazy_bind_off, file + dyld->lazy_bind_off + dyld->lazy_bind_size, segs, nsegs, 1))
    return -1;
  rsl_imports_end();
  // Weak binds are left: a definition the image shares with itself already points into the image, and the
  // operators new and delete have ordinary binds as well.

  for (int i = 0; i < nsegs; i++) {  // each segment as the file asks: Mach-O's protection bits are mmap's
    if (!seg[i]->vmsize || !strcmp(seg[i]->segname, "__PAGEZERO")) continue;
    if (mprotect((void *)seg[i]->vmaddr, (seg[i]->vmsize + page) & ~page, (int)(seg[i]->initprot & 7))) {
      perror("[rs-rearmed] the image's code cannot be made executable");
      return -1;
    }
  }
  if (g_ninits && (inits < lo || inits + (uint64_t)g_ninits * 8 > hi)) return fail(path, "its initializers lie outside it");
  g_inits = (uint64_t *)inits;
  for (int i = 0; i < g_ninits; i++)
    if (g_inits[i] < lo || g_inits[i] >= hi) return fail(path, "an initializer lies outside it");
  g_info.ninits = g_ninits;
  g_info.unwind = register_unwind(eh, eh_size);
  watch_faults();
  munmap((void *)file, size);
  *info = g_info;
  return 0;
}

uint64_t rsl_image_end(void) { return g_info.hi; }

int rsl_run_initializers(int argc, char **argv, char **envp) {
  static char *apple[] = {NULL};
  for (int i = 0; i < g_ninits; i++) {
    g_running = i;
    ((void (*)(int, char **, char **, char **, void *))(uintptr_t)g_inits[i])(argc, argv, envp, apple, NULL);
  }
  g_running = -1;
  return g_ninits;
}
