// rsl: the Mac game's image in an x86-64 Linux process, run as it is (linux/X86-64.md).
//
// The game is an x86-64 program with the calling convention Linux has too, and its code makes no system call of its
// own: everything it wants of a system it asks through 681 imports. rsl_load() maps the decrypted image
// (tools/steamless's output) at its own address and writes an answer into every import's slot:
//
//   - the host's own function or object of that name, where linux/darwin-imports.tsv says `same`;
//   - the Darwin layer's (linux/darwin), where it has one;
//   - else an address in a region with no access, one per import, so that the first call or read of it stops the
//     run and names it.
//
// Nothing is translated and nothing in the image is changed: with no slide there is nothing to rebase.
#ifndef RSL_H
#define RSL_H

#include <stdint.h>
#include <stdio.h>

enum { RSL_HOST, RSL_LAYER, RSL_NONE };  // who answers an import

typedef struct {
  const char *library, *symbol;  // `libSystem`, `_pthread_key_create`: as the table writes them
  const char *cls;               // the table's class, or "?" for an import the table lacks
  int how;                       // RSL_HOST, RSL_LAYER or RSL_NONE
  int called;                    // the image calls it (a lazy pointer) as well as, or instead of, holding its address
  uint64_t at;                   // the answer
  uint64_t stub;                 // with counting on: what the lazy pointer holds instead, a few counting instructions
} rsl_import;

typedef struct {
  uint64_t lo, hi;            // the image's addresses
  uint64_t text_lo, text_hi;  // its __TEXT
  int ninits;                 // static initializers (__mod_init_func)
  int unwind;                 // 1: its unwind tables are registered with the host's unwinder
} rsl_info;

// 0, or -1 with the reason on stderr. With COUNT_CALLS every call of an import is counted (rsl_import_calls).
int rsl_load(const char *path, int count_calls, rsl_info *info);

// Runs the static initializers in the image's order, each as dyld calls it. Returns how many ran: all of them, as
// one that faults or reaches an import with no answer ends the process (exit status 3 or 4, the reason on stderr).
int rsl_run_initializers(int argc, char **argv, char **envp);
// How many initializers have a frame description the host's unwinder finds: a C++ throw unwinds through those.
int rsl_initializers_with_unwind(void);

// --- the imports (imports.c) ---
void rsl_imports_begin(int count_calls);
rsl_import *rsl_answer(const char *library, const char *symbol, int lazy);
void rsl_imports_end(void);                      // the counting stubs become executable
const rsl_import *rsl_import_at(uint64_t addr);  // the unanswered import whose no-access address this is, or NULL
const rsl_import *rsl_imports(int *n);
uint64_t rsl_import_calls(const rsl_import *im);
void rsl_report_binding(FILE *out);
void rsl_report_calls(FILE *out);

#endif
