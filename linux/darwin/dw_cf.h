// The layer's small CoreFoundation (dw_cf.c), for the layer's other files that hand the game CF objects.
#ifndef DW_CF_H
#define DW_CF_H

#include <stddef.h>
#include <stdint.h>

// A new string from UTF-8 text, and a new array of N objects (which it then owns): each released by the game's
// CFRelease. dw_cf_const_string makes one that is never freed, for a constant the layer hands out many times.
void *dw_cf_string(const char *utf8);
void *dw_cf_const_string(const char *utf8);
void *dw_cf_array(void *const *items, long n);
// A new object of another file's own kind: SIZE bytes, zeroed, the first 16 the header CFRelease reads. KIND is
// any address of that file's, by which it knows its objects (dw_cf_is).
void *dw_cf_object(const void *kind, size_t size);
int dw_cf_is(const void *object, const void *kind);
// The text of a string as UTF-8, NUL-terminated, into BUF: 1, or 0 when it does not fit or is no string.
int dw_cf_utf8(const void *string, char *buf, size_t n);

#endif
