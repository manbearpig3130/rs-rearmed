// A small CoreFoundation: what the game proper asks of it. Strings (audio device names and identifiers, Wwise's
// conversions, the engine's UUIDs, the display modes' pixel encoding), arrays (the display's modes), a dictionary
// (made by one function and never read), UUIDs and the locale.
//
// An object begins with a class pointer and 32 bits of flags, as CoreFoundation's own do, because the image has
// 39 constant strings compiled into it in that form: {class, flags, characters, length}, the class being the
// address of __CFConstantStringClassReference and the flags 0x7c8 for 8-bit characters or 0x7d0 for UTF-16.
// Objects made here carry one more flag bit, by which CFRelease tells them from the image's, which are never freed.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dw.h"
#include "dw_cf.h"

#define OURS 0x80000000u    // made here: counted, and freed at the last release
#define FOREVER 0x40000000u // made here, never freed
#define WIDE 0x10u          // a string's characters are UTF-16 (0x7d0 against 0x7c8)

typedef struct {
  const void *isa;
  uint32_t flags;
  int32_t refs;
} cf_base;

typedef struct {
  cf_base base;
  const void *chars;  // 8-bit: ASCII in the image's, UTF-8 in ours; or UTF-16
  long length;        // in bytes, or in UTF-16 units
} cf_string;

typedef struct {
  cf_base base;
  long count;
  void *items[];
} cf_array;

// The classes: any addresses. The first is the one the image's constant strings point at.
long dw_cf_string_class[12];
DW_DATA("___CFConstantStringClassReference", dw_cf_string_class);
static const char k_array, k_dictionary, k_uuid, k_locale, k_boolean;

static void *make(const void *isa, size_t size) {
  cf_base *b = calloc(1, size);
  if (!b) abort();
  b->isa = isa;
  b->flags = OURS | 0x7c8;
  b->refs = 1;
  return b;
}

void *dw_cf_object(const void *kind, size_t size) { return make(kind, size < sizeof(cf_base) ? sizeof(cf_base) : size); }
int dw_cf_is(const void *object, const void *kind) { return object && ((const cf_base *)object)->isa == kind; }

static const cf_string *as_string(const void *p) { return dw_cf_is(p, dw_cf_string_class) ? p : NULL; }

// --- characters ---
// A string is read as code points whatever it holds.
static uint32_t next_point(const cf_string *s, long *at) {
  if (s->base.flags & WIDE) {
    const uint16_t *u = s->chars;
    uint32_t c = u[(*at)++];
    if (c >= 0xd800 && c < 0xdc00 && *at < s->length && u[*at] >= 0xdc00 && u[*at] < 0xe000)
      c = 0x10000 + ((c - 0xd800) << 10) + (u[(*at)++] - 0xdc00);
    return c;
  }
  const uint8_t *b = s->chars;
  uint32_t c = b[(*at)++];
  int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
  if (!more || !(s->base.flags & (OURS | FOREVER))) return c;  // the image's 8-bit strings are ASCII
  c &= 0x3f >> more;
  for (; more && *at < s->length && (b[*at] & 0xc0) == 0x80; more--) c = (c << 6) | (b[(*at)++] & 0x3f);
  return c;
}

static long utf16_units(const cf_string *s) {
  if (s->base.flags & WIDE) return s->length;
  long n = 0;
  for (long at = 0; at < s->length;) n += next_point(s, &at) >= 0x10000 ? 2 : 1;
  return n;
}

enum {
  ENC_MACROMAN = 0, ENC_LATIN1 = 0x201, ENC_ASCII = 0x600, ENC_UTF16 = 0x100, ENC_UTF16BE = 0x10000100,
  ENC_UTF16LE = 0x14000100, ENC_UTF8 = 0x08000100, ENC_UTF32 = 0x0c000100, ENC_UTF32BE = 0x18000100,
  ENC_UTF32LE = 0x1c000100
};

// One code point in an encoding, into OUT (room for 4 bytes): the bytes written, or 0 when it cannot be written.
static int put_point(uint32_t c, uint32_t encoding, uint8_t *out) {
  switch (encoding) {
    case ENC_UTF8:
      if (c < 0x80) return out[0] = (uint8_t)c, 1;
      if (c < 0x800) return out[0] = (uint8_t)(0xc0 | c >> 6), out[1] = (uint8_t)(0x80 | (c & 0x3f)), 2;
      if (c < 0x10000)
        return out[0] = (uint8_t)(0xe0 | c >> 12), out[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3f)), out[2] = (uint8_t)(0x80 | (c & 0x3f)), 3;
      return out[0] = (uint8_t)(0xf0 | c >> 18), out[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3f)),
             out[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3f)), out[3] = (uint8_t)(0x80 | (c & 0x3f)), 4;
    case ENC_UTF16: case ENC_UTF16LE: case ENC_UTF16BE: {
      uint16_t u[2];
      int n = 1;
      if (c >= 0x10000) u[0] = (uint16_t)(0xd800 + ((c - 0x10000) >> 10)), u[1] = (uint16_t)(0xdc00 + ((c - 0x10000) & 0x3ff)), n = 2;
      else u[0] = (uint16_t)c;
      for (int i = 0; i < n; i++)
        if (encoding == ENC_UTF16BE) out[2 * i] = (uint8_t)(u[i] >> 8), out[2 * i + 1] = (uint8_t)u[i];
        else out[2 * i] = (uint8_t)u[i], out[2 * i + 1] = (uint8_t)(u[i] >> 8);
      return 2 * n;
    }
    case ENC_UTF32: case ENC_UTF32LE:
      return out[0] = (uint8_t)c, out[1] = (uint8_t)(c >> 8), out[2] = (uint8_t)(c >> 16), out[3] = (uint8_t)(c >> 24), 4;
    case ENC_UTF32BE:
      return out[3] = (uint8_t)c, out[2] = (uint8_t)(c >> 8), out[1] = (uint8_t)(c >> 16), out[0] = (uint8_t)(c >> 24), 4;
    case ENC_LATIN1:
      return c < 0x100 ? (out[0] = (uint8_t)c, 1) : 0;
    default:  // ASCII, and Mac Roman, of which only its ASCII half is kept
      return c < 0x80 ? (out[0] = (uint8_t)c, 1) : 0;
  }
}

static void *string_of(const char *utf8, size_t n, uint32_t keep) {
  cf_string *s = make(dw_cf_string_class, sizeof *s + n + 1);
  char *chars = (char *)(s + 1);
  memcpy(chars, utf8, n);
  s->chars = chars;
  s->length = (long)n;
  s->base.flags = keep | 0x7c8;
  return s;
}

void *dw_cf_string(const char *utf8) { return string_of(utf8, strlen(utf8), OURS); }
void *dw_cf_const_string(const char *utf8) { return string_of(utf8, strlen(utf8), FOREVER); }

int dw_cf_utf8(const void *string, char *buf, size_t n) {
  const cf_string *s = as_string(string);
  if (!s || !n) return 0;
  size_t out = 0;
  for (long at = 0; at < s->length;) {
    uint8_t b[4];
    int k = put_point(next_point(s, &at), ENC_UTF8, b);
    if (out + (size_t)k + 1 > n) return buf[0] = 0, 0;
    memcpy(buf + out, b, (size_t)k);
    out += (size_t)k;
  }
  buf[out] = 0;
  return 1;
}

// --- the functions the image imports ---

void dw_CFRelease(void *object) {
  cf_base *b = object;
  if (!b || !(b->flags & OURS)) return;  // the image's constants, and ours that last forever
  if (__atomic_sub_fetch(&b->refs, 1, __ATOMIC_SEQ_CST) > 0) return;
  if (b->isa == &k_array) {
    cf_array *a = object;
    for (long i = 0; i < a->count; i++) dw_CFRelease(a->items[i]);
  }
  free(b);
}
DW_FUNCTION("_CFRelease", dw_CFRelease);

// Seconds since the first of January 2001.
double dw_CFAbsoluteTimeGetCurrent(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (double)(t.tv_sec - 978307200) + (double)t.tv_nsec / 1e9;
}
DW_FUNCTION("_CFAbsoluteTimeGetCurrent", dw_CFAbsoluteTimeGetCurrent);

void dw_CFShow(const void *object) {
  char text[512];
  if (dw_cf_utf8(object, text, sizeof text)) dw_log("CFShow: %s", text);
  else dw_log("CFShow: an object at %p", object);
}
DW_FUNCTION("_CFShow", dw_CFShow);

// Strings.
void *dw_CFStringCreateWithBytes(void *allocator, const uint8_t *bytes, long n, uint32_t encoding, unsigned char external) {
  (void)allocator, (void)external;
  size_t room = (size_t)n * 3 + 4, out = 0;  // UTF-8 of any of these is at most three times as long
  char *utf8 = malloc(room);
  if (!utf8) return NULL;
  int unit = encoding == ENC_UTF16 || encoding == ENC_UTF16LE || encoding == ENC_UTF16BE ? 2
             : encoding == ENC_UTF32 || encoding == ENC_UTF32LE || encoding == ENC_UTF32BE ? 4 : 1;
  int big = encoding == ENC_UTF16BE || encoding == ENC_UTF32BE;
  for (long i = 0; i + unit <= n;) {
    uint32_t c;
    if (unit == 1) {
      c = bytes[i++];
      if (encoding == ENC_UTF8 && c >= 0xc0) {
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        c &= 0x3f >> more;
        for (; more && i < n && (bytes[i] & 0xc0) == 0x80; more--) c = (c << 6) | (bytes[i++] & 0x3f);
      } else if (c >= 0x80 && encoding != ENC_LATIN1 && encoding != ENC_UTF8) {
        c = '?';  // Mac Roman's upper half is not kept
      }
    } else if (unit == 2) {
      c = big ? (uint32_t)bytes[i] << 8 | bytes[i + 1] : (uint32_t)bytes[i + 1] << 8 | bytes[i];
      i += 2;
      if (c >= 0xd800 && c < 0xdc00 && i + 2 <= n) {
        uint32_t lo = big ? (uint32_t)bytes[i] << 8 | bytes[i + 1] : (uint32_t)bytes[i + 1] << 8 | bytes[i];
        if (lo >= 0xdc00 && lo < 0xe000) c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00), i += 2;
      }
    } else {
      c = big ? (uint32_t)bytes[i] << 24 | (uint32_t)bytes[i + 1] << 16 | (uint32_t)bytes[i + 2] << 8 | bytes[i + 3]
              : (uint32_t)bytes[i + 3] << 24 | (uint32_t)bytes[i + 2] << 16 | (uint32_t)bytes[i + 1] << 8 | bytes[i];
      i += 4;
    }
    if (c > 0x10ffff) c = 0xfffd;
    out += (size_t)put_point(c, ENC_UTF8, (uint8_t *)utf8 + out);
  }
  void *s = string_of(utf8, out, OURS);
  free(utf8);
  return s;
}
DW_FUNCTION("_CFStringCreateWithBytes", dw_CFStringCreateWithBytes);

long dw_CFStringGetLength(const void *string) {
  const cf_string *s = as_string(string);
  return s ? utf16_units(s) : 0;
}
DW_FUNCTION("_CFStringGetLength", dw_CFStringGetLength);

typedef struct {
  long location, length;  // in UTF-16 units
} cf_range;

// The characters of RANGE in ENCODING: how many UTF-16 units were converted. With no buffer, the size alone.
long dw_CFStringGetBytes(const void *string, cf_range range, uint32_t encoding, uint8_t loss, unsigned char external,
                         uint8_t *buffer, long max, long *used) {
  (void)external;
  const cf_string *s = as_string(string);
  long unit = 0, done = 0, out = 0;
  for (long at = 0; s && at < s->length && unit < range.location + range.length;) {
    uint32_t c = next_point(s, &at);
    long units = c >= 0x10000 ? 2 : 1;
    if (unit >= range.location) {
      uint8_t b[4];
      int k = put_point(c, encoding, b);
      if (!k && loss) b[0] = loss, k = 1;
      if (!k || (buffer && out + k > max)) break;
      if (buffer) memcpy(buffer + out, b, (size_t)k);
      out += k;
      done += units;
    }
    unit += units;
  }
  if (used) *used = out;
  return done;
}
DW_FUNCTION("_CFStringGetBytes", dw_CFStringGetBytes);

unsigned char dw_CFStringGetCString(const void *string, char *buffer, long size, uint32_t encoding) {
  const cf_string *s = as_string(string);
  if (!s || size < 1) return 0;
  long used = 0, units = utf16_units(s);
  long done = dw_CFStringGetBytes(string, (cf_range){0, units}, encoding, 0, 0, (uint8_t *)buffer, size - 1, &used);
  buffer[used] = 0;
  return done == units;
}
DW_FUNCTION("_CFStringGetCString", dw_CFStringGetCString);

// The characters themselves, where the string holds them in the encoding asked for: an 8-bit string of ASCII.
const char *dw_CFStringGetCStringPtr(const void *string, uint32_t encoding) {
  const cf_string *s = as_string(string);
  if (!s || (s->base.flags & WIDE)) return NULL;
  if (encoding != ENC_UTF8 && encoding != ENC_ASCII && encoding != ENC_MACROMAN && encoding != ENC_LATIN1) return NULL;
  const char *c = s->chars;
  if (encoding != ENC_UTF8)
    for (long i = 0; i < s->length; i++)
      if ((unsigned char)c[i] >= 0x80) return NULL;
  return c[s->length] == 0 ? c : NULL;
}
DW_FUNCTION("_CFStringGetCStringPtr", dw_CFStringGetCStringPtr);

uint32_t dw_CFStringGetFastestEncoding(const void *string) {
  const cf_string *s = as_string(string);
  return s && !(s->base.flags & WIDE) ? ENC_MACROMAN : ENC_UTF16;
}
DW_FUNCTION("_CFStringGetFastestEncoding", dw_CFStringGetFastestEncoding);

uint32_t dw_CFStringGetSystemEncoding(void) { return ENC_MACROMAN; }
DW_FUNCTION("_CFStringGetSystemEncoding", dw_CFStringGetSystemEncoding);

long dw_CFStringGetMaximumSizeForEncoding(long length, uint32_t encoding) {
  switch (encoding) {
    case ENC_UTF8: return length * 3;
    case ENC_UTF16: case ENC_UTF16LE: case ENC_UTF16BE: return length * 2;
    case ENC_UTF32: case ENC_UTF32LE: case ENC_UTF32BE: return length * 4;
  }
  return length;
}
DW_FUNCTION("_CFStringGetMaximumSizeForEncoding", dw_CFStringGetMaximumSizeForEncoding);

// -1, 0 or 1. Option 1 is kCFCompareCaseInsensitive, applied to ASCII letters.
long dw_CFStringCompare(const void *a, const void *b, unsigned long options) {
  const cf_string *x = as_string(a), *y = as_string(b);
  if (!x || !y) return x == y ? 0 : x ? 1 : -1;
  for (long i = 0, k = 0;;) {
    if (i >= x->length || k >= y->length) return i < x->length ? 1 : k < y->length ? -1 : 0;
    uint32_t c = next_point(x, &i), d = next_point(y, &k);
    if (options & 1) {
      if (c >= 'A' && c <= 'Z') c += 32;
      if (d >= 'A' && d <= 'Z') d += 32;
    }
    if (c != d) return c < d ? -1 : 1;
  }
}
DW_FUNCTION("_CFStringCompare", dw_CFStringCompare);

// Arrays.
void *dw_cf_array(void *const *items, long n) {
  cf_array *a = make(&k_array, sizeof *a + (size_t)n * sizeof(void *));
  a->count = n;
  memcpy(a->items, items, (size_t)n * sizeof(void *));
  return a;
}

long dw_CFArrayGetCount(const void *array) { return dw_cf_is(array, &k_array) ? ((const cf_array *)array)->count : 0; }
DW_FUNCTION("_CFArrayGetCount", dw_CFArrayGetCount);

const void *dw_CFArrayGetValueAtIndex(const void *array, long i) {
  const cf_array *a = array;
  return dw_cf_is(array, &k_array) && i >= 0 && i < a->count ? a->items[i] : NULL;
}
DW_FUNCTION("_CFArrayGetValueAtIndex", dw_CFArrayGetValueAtIndex);

// A dictionary: the game makes one to ask IOKit for a service, and nothing reads what it holds.
void *dw_CFDictionaryCreateMutable(void *allocator, long capacity, const void *key_callbacks, const void *value_callbacks) {
  (void)allocator, (void)capacity, (void)key_callbacks, (void)value_callbacks;
  return make(&k_dictionary, sizeof(cf_base));
}
DW_FUNCTION("_CFDictionaryCreateMutable", dw_CFDictionaryCreateMutable);

void dw_CFDictionarySetValue(void *dictionary, const void *key, const void *value) { (void)dictionary, (void)key, (void)value; }
DW_FUNCTION("_CFDictionarySetValue", dw_CFDictionarySetValue);

// UUIDs: sixteen bytes, each its own argument, and their text in capitals.
typedef struct {
  cf_base base;
  uint8_t bytes[16];
} cf_uuid;

void *dw_CFUUIDCreateWithBytes(void *allocator, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5,
                               uint8_t b6, uint8_t b7, uint8_t b8, uint8_t b9, uint8_t b10, uint8_t b11, uint8_t b12,
                               uint8_t b13, uint8_t b14, uint8_t b15) {
  (void)allocator;
  cf_uuid *u = make(&k_uuid, sizeof *u);
  const uint8_t bytes[16] = {b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15};
  memcpy(u->bytes, bytes, 16);
  return u;
}
DW_FUNCTION("_CFUUIDCreateWithBytes", dw_CFUUIDCreateWithBytes);

void *dw_CFUUIDCreateString(void *allocator, const void *uuid) {
  (void)allocator;
  if (!dw_cf_is(uuid, &k_uuid)) return NULL;
  const uint8_t *b = ((const cf_uuid *)uuid)->bytes;
  char text[40];
  snprintf(text, sizeof text, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", b[0], b[1], b[2],
           b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return dw_cf_string(text);
}
DW_FUNCTION("_CFUUIDCreateString", dw_CFUUIDCreateString);

// The locale: English in the United States, on every machine. The game's own language is Steam's setting.
const void *dw_kCFLocaleLanguageCode, *dw_kCFLocaleCountryCode, *dw_kCFLocaleIdentifier;
DW_DATA("_kCFLocaleLanguageCode", dw_kCFLocaleLanguageCode);
DW_DATA("_kCFLocaleCountryCode", dw_kCFLocaleCountryCode);
DW_DATA("_kCFLocaleIdentifier", dw_kCFLocaleIdentifier);
static const void *g_language, *g_country, *g_identifier;

void *dw_CFLocaleCopyCurrent(void) { return make(&k_locale, sizeof(cf_base)); }
DW_FUNCTION("_CFLocaleCopyCurrent", dw_CFLocaleCopyCurrent);

const void *dw_CFLocaleGetValue(const void *locale, const void *key) {
  (void)locale;
  return key == dw_kCFLocaleLanguageCode ? g_language : key == dw_kCFLocaleCountryCode ? g_country
         : key == dw_kCFLocaleIdentifier ? g_identifier : NULL;
}
DW_FUNCTION("_CFLocaleGetValue", dw_CFLocaleGetValue);

// The constants: each symbol is a variable that holds the object.
const void *dw_kCFAllocatorDefault;  // NULL is the default allocator
DW_DATA("_kCFAllocatorDefault", dw_kCFAllocatorDefault);
static cf_base g_true = {&k_boolean, FOREVER, 1};
const void *dw_kCFBooleanTrue = &g_true;
DW_DATA("_kCFBooleanTrue", dw_kCFBooleanTrue);
double dw_kCFCoreFoundationVersionNumber = 1953.1;  // macOS 13's
DW_DATA("_kCFCoreFoundationVersionNumber", dw_kCFCoreFoundationVersionNumber);
long dw_kCFTypeDictionaryKeyCallBacks[6], dw_kCFTypeDictionaryValueCallBacks[5];  // tables of functions nothing here calls
DW_DATA("_kCFTypeDictionaryKeyCallBacks", dw_kCFTypeDictionaryKeyCallBacks);
DW_DATA("_kCFTypeDictionaryValueCallBacks", dw_kCFTypeDictionaryValueCallBacks);

__attribute__((constructor)) static void cf_init(void) {
  dw_kCFLocaleLanguageCode = dw_cf_const_string("kCFLocaleLanguageCodeKey");
  dw_kCFLocaleCountryCode = dw_cf_const_string("kCFLocaleCountryCodeKey");
  dw_kCFLocaleIdentifier = dw_cf_const_string("kCFLocaleIdentifierKey");
  g_language = dw_cf_const_string("en");
  g_country = dw_cf_const_string("US");
  g_identifier = dw_cf_const_string("en_US");
}
