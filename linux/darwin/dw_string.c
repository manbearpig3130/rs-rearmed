// libSystem's memory and string functions that glibc has under another name or not at all.
#include <string.h>

#include "dw.h"

// __bzero: what Apple's compiler makes of a memset to zero.
void dw_bzero(void *p, size_t n) { memset(p, 0, n); }
DW_FUNCTION("___bzero", dw_bzero);
