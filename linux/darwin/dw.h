// The Darwin layer: what the Mac game imports from macOS, answered on Linux.
//
// linux/darwin-imports.tsv classes every import. A `same` row is the host's own function and has nothing here. The
// others are functions and objects of this folder, each registered under the name the image imports it by:
//
//     int dw_pthread_key_create(unsigned long *key, void (*destructor)(void *)) { ... }
//     DW_FUNCTION("_pthread_key_create", dw_pthread_key_create);
//
// Rules:
//   - Layouts, flags and numbers are Darwin's at this boundary, the host's below it. Each function says what differs.
//   - A function that returns an error number (pthreads) returns Darwin's: DW_ERR(r).
//   - A function that fails through errno just fails: the game reads errno through __error(), which moves a set
//     host errno into a Darwin-numbered value of its own (dw_errno.c).
//   - Nothing here reaches a network.
#ifndef DW_H
#define DW_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *symbol;  // with Mach-O's underscore in front
  void *at;
} dw_entry;

// The entries are gathered by the linker: a section whose name is a C identifier gets __start_ and __stop_ symbols.
#define DW_FUNCTION(symbol, fn) \
  static dw_entry dw_function_##fn __attribute__((used, section("dw_set_functions"), aligned(8))) = {symbol, (void *)(fn)}
#define DW_DATA(symbol, object) \
  static dw_entry dw_data_##object __attribute__((used, section("dw_set_objects"), aligned(8))) = {symbol, (void *)&(object)}

void *dw_function(const char *symbol);  // NULL: the layer has none of that name
void *dw_data(const char *symbol);
const dw_entry *dw_functions(size_t *n);
const dw_entry *dw_objects(size_t *n);
void dw_init(void);  // once, before the image is bound: the objects that are made at start

// A line on stderr, `[darwin] ...`: what the layer refused or did not know. RS_DARWIN_LOG=0 turns them off.
void dw_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// A path as the game wrote it, found on a file system that tells letter cases apart (dw_file.c): PATH itself
// when it is there, else BUF with each missing part replaced by the name that differs in case alone.
const char *dw_path(const char *path, char *buf, size_t n);

// The display's modes as the shell knows them, {width, height} each, for the renderer's list (dw_display.c).
void dw_display_set_modes(const int (*modes)[2], int n);

// Where the game's sound goes (dw_audio.c). A backend is started when the game starts its audio graph, and from
// then calls RENDER on its own audio thread whenever it wants FRAMES more frames (at most 4096), which fills the
// two channels with what the game plays. Without one, a thread calls it on the clock and drops the sound.
typedef void (*dw_audio_render)(void *ctx, float *left, float *right, uint32_t frames);
// An input the backend has: a capture device as the game is to see it. The game knows the Real Tone Cable by
// "Rocksmith USB" in its name or maker.
typedef struct {
  char name[128], maker[64];  // "Rocksmith USB Guitar Adapter", "Hercules"
  char uid[192];              // the backend's own name for it, the same at every start
  char model[192];            // name and USB ids, as CoreAudio's model identifier has them; or empty
  int channels, usb;
} dw_audio_input;
// What an open input hears: FRAMES frames, the channels interleaved, on the backend's audio thread.
typedef void (*dw_audio_capture)(void *ctx, const float *samples, uint32_t frames);
typedef struct {
  const char *name;
  int (*start)(double rate, uint32_t frames, dw_audio_render render, void *ctx);  // 0, or the silent clock is used
  void (*stop)(void);
  int (*inputs)(dw_audio_input *list, int max);  // the inputs there are now: how many (NULL: none ever)
  // An input opened: CAPTURE is called with FRAMES frames at a time. A handle for capture_stop, or NULL.
  void *(*capture_start)(const dw_audio_input *input, double rate, uint32_t frames, dw_audio_capture capture, void *ctx);
  void (*capture_stop)(void *handle);
} dw_audio_backend;
void dw_audio_set_backend(const dw_audio_backend *backend);

// errno: Darwin's number for the host's, and DW_ERR for a returned one.
int dw_errno_from_host(int host);
#define DW_ERR(r) dw_errno_from_host(r)
int *dw_error(void);  // the game's __error()

// Darwin's _RuneLocale, which the image's inlined isalpha and the like read (dw_rune.c).
struct dw_rune_range {
  int nranges;
  void *ranges;
};
struct dw_rune_locale {
  char magic[8], encoding[32];
  void *sgetrune, *sputrune;
  int invalid_rune;
  uint32_t runetype[256], maplower[256], mapupper[256];
  struct dw_rune_range runetype_ext, maplower_ext, mapupper_ext;
  void *variable;
  int variable_len;
  int ncharclasses;
  void *charclasses;
};
extern struct dw_rune_locale dw_default_rune_locale;
void dw_rune_init(void);

#endif
