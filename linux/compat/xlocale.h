// macOS's <xlocale.h> for the project's shared code on Linux: the conversions that take a locale, where a NULL
// one is the C locale (a number in a settings file reads the same whatever the user's language).
#ifndef RS_COMPAT_XLOCALE_H
#define RS_COMPAT_XLOCALE_H

#include <locale.h>
#include <stdlib.h>

static inline float rs_strtof_c(const char *s, char **end) {
  static locale_t c;
  if (!c) c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
  return c ? strtof_l(s, end, c) : strtof(s, end);
}
static inline float rs_strtof_l(const char *s, char **end, locale_t loc) { return loc ? strtof_l(s, end, loc) : rs_strtof_c(s, end); }
#define strtof_l(s, end, loc) rs_strtof_l((s), (end), (locale_t)(loc))

#endif
