// The C locale's character table in Darwin's layout. On Darwin isalpha, isdigit, tolower and the like are inline:
// the image reads _DefaultRuneLocale itself for a character below 256, and calls __maskrune, __tolower and
// __toupper above that. The game never leaves the C locale, so the table is that locale's and no other.
#include <string.h>

#include "dw.h"

// <_ctype.h>'s bits. The low byte of a digit's entry is its value.
enum {
  CT_A = 0x100, CT_C = 0x200, CT_D = 0x400, CT_G = 0x800, CT_L = 0x1000, CT_P = 0x2000, CT_S = 0x4000,
  CT_U = 0x8000, CT_X = 0x10000, CT_B = 0x20000, CT_R = 0x40000,
};

struct dw_rune_locale dw_default_rune_locale;
DW_DATA("__DefaultRuneLocale", dw_default_rune_locale);

void dw_rune_init(void) {
  struct dw_rune_locale *t = &dw_default_rune_locale;
  memset(t, 0, sizeof *t);
  memcpy(t->magic, "RuneMagA", 8);
  memcpy(t->encoding, "NONE", 4);
  t->invalid_rune = 0xfffd;
  for (int c = 0; c < 256; c++) {
    uint32_t bits = 0;
    int lower = c >= 'a' && c <= 'z', upper = c >= 'A' && c <= 'Z', digit = c >= '0' && c <= '9';
    if (c < 32 || c == 127) bits |= CT_C;
    if (c == ' ' || (c >= '\t' && c <= '\r')) bits |= CT_S;
    if (c == ' ' || c == '\t') bits |= CT_B;
    if (c >= 32 && c < 127) bits |= CT_R;
    if (c > 32 && c < 127) bits |= CT_G;
    if (digit) bits |= CT_D | CT_X | (uint32_t)(c - '0');
    else if (lower || upper) {
      bits |= CT_A | (upper ? CT_U : CT_L);
      int hex = (c | 0x20) - 'a';
      if (hex < 6) bits |= CT_X | (uint32_t)(hex + 10);
    } else if (c > 32 && c < 127) bits |= CT_P;
    t->runetype[c] = bits;
    t->maplower[c] = (uint32_t)(upper ? c + 32 : c);
    t->mapupper[c] = (uint32_t)(lower ? c - 32 : c);
  }
}

// Above the table (a character of 256 or more) the C locale has no classes and no cases.
int dw_maskrune(int c, unsigned long mask) {
  return c >= 0 && c < 256 ? (int)(dw_default_rune_locale.runetype[c] & mask) : 0;
}
DW_FUNCTION("___maskrune", dw_maskrune);

int dw_tolower(int c) { return c >= 0 && c < 256 ? (int)dw_default_rune_locale.maplower[c] : c; }
DW_FUNCTION("___tolower", dw_tolower);

int dw_toupper(int c) { return c >= 0 && c < 256 ? (int)dw_default_rune_locale.mapupper[c] : c; }
DW_FUNCTION("___toupper", dw_toupper);
