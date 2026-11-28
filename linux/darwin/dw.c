// The layer's tables, and the objects of libSystem the image holds the address of. See dw.h.
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

#include "dw.h"

// The linker's bounds of the two sets DW_FUNCTION and DW_DATA fill.
extern dw_entry __start_dw_set_functions[], __stop_dw_set_functions[];
extern dw_entry __start_dw_set_objects[], __stop_dw_set_objects[];

static void *find(const dw_entry *e, const dw_entry *end, const char *symbol) {
  for (; e < end; e++)
    if (!strcmp(e->symbol, symbol)) return e->at;
  return NULL;
}

void *dw_function(const char *symbol) { return find(__start_dw_set_functions, __stop_dw_set_functions, symbol); }
void *dw_data(const char *symbol) { return find(__start_dw_set_objects, __stop_dw_set_objects, symbol); }

const dw_entry *dw_functions(size_t *n) {
  *n = (size_t)(__stop_dw_set_functions - __start_dw_set_functions);
  return __start_dw_set_functions;
}

const dw_entry *dw_objects(size_t *n) {
  *n = (size_t)(__stop_dw_set_objects - __start_dw_set_objects);
  return __start_dw_set_objects;
}

// ___stack_chk_guard: the value the image's functions put above their locals and check on return. On Darwin it is
// an object the image reads; glibc keeps its own in the thread block, out of the image's reach.
uintptr_t dw_stack_chk_guard;
DW_DATA("___stack_chk_guard", dw_stack_chk_guard);

// ___stdinp, ___stdoutp, ___stderrp: Darwin's stdin, stdout and stderr are these three pointers.
FILE *dw_stdinp, *dw_stdoutp, *dw_stderrp;
DW_DATA("___stdinp", dw_stdinp);
DW_DATA("___stdoutp", dw_stdoutp);
DW_DATA("___stderrp", dw_stderrp);

// _mach_task_self_: this task's port, which the image only passes back to Mach calls of this layer.
unsigned dw_mach_task_self = 0x103;
DW_DATA("_mach_task_self_", dw_mach_task_self);

void dw_init(void) {
  static int done;
  if (done) return;
  done = 1;
  if (getrandom(&dw_stack_chk_guard, sizeof dw_stack_chk_guard, 0) != (ssize_t)sizeof dw_stack_chk_guard)
    dw_stack_chk_guard = (uintptr_t)&dw_stack_chk_guard * 0x9e3779b97f4a7c15ull;
  dw_stack_chk_guard &= ~(uintptr_t)0xff;  // a zero byte first, as Darwin's has: a string copy stops at it
  dw_stdinp = stdin, dw_stdoutp = stdout, dw_stderrp = stderr;
  dw_rune_init();
}
