// CoreAudio's devices and the audio graph, in a silent form: the model the game's audio code is written against,
// with nothing behind it that makes a sound yet.
//
// The game asks the hardware layer (AudioObject...) which devices there are and what they can do, builds a graph of
// audio units (AUGraph: a mixer into a device output) with a callback that Wwise fills, and for the guitar opens
// an input device with an I/O procedure. Here there is one output device, stereo at 48,000 Hz, and the inputs
// the sound backend has (none without one), each a device with an I/O procedure fed from what it captures.
// The graph is kept as the game made it, and while it runs a thread calls the game's callback once a buffer, on
// the clock, and throws the sound away: so Wwise runs, and the game with it. Where the program has a sound
// backend (dw_audio_set_backend: PipeWire, linux/src/audio_pipewire.c) that calls for the sound instead and
// plays it.
//
// RS_AUDIO_LOG=1 logs every question once; a question with no answer is always logged once.
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dw.h"
#include "dw_cf.h"

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
enum {
  noErr = 0,
  kUnknownProperty = (int)FOURCC('w', 'h', 'o', '?'),
  kBadObject = (int)FOURCC('!', 'o', 'b', 'j'),
  kBadPropertySize = (int)FOURCC('!', 's', 'i', 'z'),
  kIllegalOperation = (int)FOURCC('n', 'o', 'p', 'e'),
  kUnspecified = (int)FOURCC('w', 'h', 'a', 't'),
};
enum { SYSTEM = 1, DEVICE_OUT = 0x101, STREAM_OUT = 0x201 };
enum { SCOPE_GLOBAL = FOURCC('g', 'l', 'o', 'b'), SCOPE_INPUT = FOURCC('i', 'n', 'p', 't'), SCOPE_OUTPUT = FOURCC('o', 'u', 't', 'p') };

typedef struct {
  uint32_t selector, scope, element;
} prop_address;

typedef struct {  // AudioStreamBasicDescription, 40 bytes
  double rate;
  uint32_t format, flags, bytes_per_packet, frames_per_packet, bytes_per_frame, channels, bits, reserved;
} stream_format;

typedef struct {  // AudioBuffer
  uint32_t channels, bytes;
  void *data;
} audio_buffer;

typedef struct {  // AudioBufferList with room for 8 buffers
  uint32_t count;
  audio_buffer buffers[8];
} buffer_list;

typedef struct {  // AudioTimeStamp, 64 bytes
  double sample_time;
  uint64_t host_time;
  double rate_scalar;
  uint64_t word_clock;
  uint8_t smpte[24];
  uint32_t flags, reserved;
} time_stamp;
_Static_assert(sizeof(stream_format) == 40 && sizeof(time_stamp) == 64 && sizeof(audio_buffer) == 16, "CoreAudio's layouts");

static double g_rate = 48000.0;
static uint32_t g_frames = 256;  // the device's buffer: the Mac's default

// RS_AUDIO_FRAMES, the installer's "Output device buffer": the output's buffer in frames, which then stays whatever
// the game sets, as the Mac's audio module keeps it. Unset, empty or 0: the game's own (256 unless it sets
// another). Kept within 32 and 4096, as the Mac keeps it within the device's range.
static uint32_t out_frames(void) {
  const char *e = getenv("RS_AUDIO_FRAMES");
  long n = e && *e ? strtol(e, NULL, 10) : 0;
  if (n <= 0) return g_frames;
  return n < 32 ? 32 : n > 4096 ? 4096 : (uint32_t)n;
}

// The inputs: what the backend has, each a device of its own with one input stream. A device keeps its number for
// the whole run, known by the backend's name for it, so one that goes and comes back is the same device.
#define MAX_INPUTS 10  // the game opens ten at most
#define DEVICE_IN(i) (0x110 + (uint32_t)(i))
#define STREAM_IN(i) (0x210 + (uint32_t)(i))
typedef int (*io_proc)(uint32_t device, const void *now, const void *input, const void *input_time, void *output,
                       const void *output_time, void *client);
typedef struct {
  dw_audio_input info;
  int present;
  uint32_t frames;  // its buffer, as the game set it
  float volume;     // 'volm', as the game set it
  float gain;       // what that is as a gain on the samples (1 where the hardware has the level)
  io_proc proc;
  void *client;
  void *capture;    // the backend's handle while it runs
  double sample;
} input_device;
static input_device g_inputs[MAX_INPUTS];
static int g_ninputs;
static const dw_audio_backend *g_backend;
static pthread_mutex_t g_inputs_lock = PTHREAD_MUTEX_INITIALIZER;

static void refresh_inputs(void) {
  if (!g_backend || !g_backend->inputs) return;
  dw_audio_input list[MAX_INPUTS];
  int n = g_backend->inputs(list, MAX_INPUTS);
  pthread_mutex_lock(&g_inputs_lock);
  for (int i = 0; i < g_ninputs; i++) g_inputs[i].present = 0;
  for (int k = 0; k < n; k++) {
    int at = -1;
    for (int i = 0; i < g_ninputs; i++)
      if (!strcmp(g_inputs[i].info.uid, list[k].uid)) at = i;
    if (at < 0 && g_ninputs < MAX_INPUTS) {
      at = g_ninputs++;
      memset(&g_inputs[at], 0, sizeof g_inputs[at]);
      g_inputs[at].frames = 128;     // an input's buffer on the Mac
      g_inputs[at].volume = 0.375f;  // where the game starts a device
      g_inputs[at].gain = 1.0f;
    }
    if (at < 0) continue;
    g_inputs[at].info = list[k];
    g_inputs[at].present = 1;
  }
  pthread_mutex_unlock(&g_inputs_lock);
}

// An input's level, as the Mac app has it. The game sets a level from 0 to 1 on a device ('volm'): calibration
// raises it in small steps until the meter is right, and a profile keeps it. The Real Tone Cable has a control of
// its own for it (18 dB of range on its USB mixer), and the game's level is that control's place in its range, as
// CoreAudio's volume scalar is. On any other input the hardware's level is the player's own setting, or there is
// none: there the level is a gain of ours on what is captured, 0.375 (where the game starts a device) being
// unity and 1 being +24 dB.
static int is_cable(const input_device *d) { return strstr(d->info.name, "Rocksmith USB") != NULL; }
static int hardware_level(const input_device *d) {
  float level;
  return is_cable(d) && g_backend && g_backend->level_get && g_backend->level_set && g_backend->level_get(&d->info, &level);
}
static float software_gain(const input_device *d) {
  if (hardware_level(d)) return 1.0f;
  float v = d->volume;
  return v <= 0.375f ? v / 0.375f : powf(10.0f, 24.0f * (v - 0.375f) / 0.625f / 20.0f);
}

static input_device *input_of(uint32_t obj) {
  uint32_t i = obj - DEVICE_IN(0);
  return i < (uint32_t)g_ninputs && g_inputs[i].present ? &g_inputs[i] : NULL;
}

static int audio_log(void) {
  static int on = -1;
  if (on < 0) {
    const char *e = getenv("RS_AUDIO_LOG");
    on = e && *e && *e != '0';
  }
  return on;
}

static const char *fourcc(uint32_t v, char *buf) {
  for (int i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)(v >> (24 - 8 * i));
    buf[i] = c >= 32 && c < 127 ? (char)c : '.';
  }
  buf[4] = 0;
  return buf;
}

// Each distinct question once: what (get, set, size, has), of which object, which property.
static void note(const char *what, uint32_t obj, const prop_address *a, int answered) {
  static struct {
    uint32_t obj, sel, scope;
    char what;
  } seen[256];
  static _Atomic int nseen;
  if (answered && !audio_log()) return;
  int n = atomic_load(&nseen);
  for (int i = 0; i < n; i++)
    if (seen[i].obj == obj && seen[i].sel == a->selector && seen[i].scope == a->scope && seen[i].what == what[0]) return;
  if (n < 256 && atomic_compare_exchange_strong(&nseen, &n, n + 1)) seen[n].obj = obj, seen[n].sel = a->selector, seen[n].scope = a->scope, seen[n].what = what[0];
  char s[5], c[5];
  fprintf(stderr, "[audio] %s '%s' (%s, element %u) of object 0x%x%s\n", what, fourcc(a->selector, s), fourcc(a->scope, c),
          a->element, obj, answered ? "" : ": no answer here");
}

static stream_format device_format(void) {
  // 32-bit float, packed, native endian, both channels in one buffer: what a device's stream is on the Mac
  stream_format f = {g_rate, FOURCC('l', 'p', 'c', 'm'), 0x1 | 0x8, 8, 1, 8, 2, 32, 0};
  return f;
}

#define PUT(T, v)                                \
  do {                                           \
    if (*size < sizeof(T)) return kBadPropertySize; \
    T value_ = (v);                              \
    memcpy(out, &value_, sizeof(T));             \
    *size = sizeof(T);                           \
    return noErr;                                \
  } while (0)

// An input device's answers. Its one stream is 32-bit float, the channels interleaved, as CoreAudio presents any
// device; the backend converts what the hardware gives.
static int get_input(input_device *d, uint32_t obj, const prop_address *a, uint32_t *size, void *out) {
  int input = a->scope != SCOPE_OUTPUT;  // nothing in the output scope
  stream_format f = {g_rate, FOURCC('l', 'p', 'c', 'm'), 0x1 | 0x8, 4u * (uint32_t)d->info.channels, 1,
                     4u * (uint32_t)d->info.channels, (uint32_t)d->info.channels, 32, 0};
  switch (a->selector) {
    case FOURCC('l', 'n', 'a', 'm'): PUT(void *, dw_cf_string(d->info.name));
    case FOURCC('l', 'm', 'a', 'k'): PUT(void *, dw_cf_string(d->info.maker));
    case FOURCC('u', 'i', 'd', ' '): PUT(void *, dw_cf_string(d->info.uid));
    case FOURCC('m', 'u', 'i', 'd'):
      if (!d->info.model[0]) return kUnknownProperty;
      PUT(void *, dw_cf_string(d->info.model));
    case FOURCC('t', 'r', 'a', 'n'): PUT(uint32_t, d->info.usb ? FOURCC('u', 's', 'b', ' ') : FOURCC('b', 'l', 't', 'n'));
    case FOURCC('l', 'i', 'v', 'n'): PUT(uint32_t, 1);
    case FOURCC('g', 'o', 'i', 'n'): PUT(uint32_t, d->capture != NULL);
    case FOURCC('n', 's', 'r', 't'): PUT(double, g_rate);
    case FOURCC('n', 's', 'r', '#'): {
      struct { double lo, hi; } range = {g_rate, g_rate};
      if (*size < sizeof range) return kBadPropertySize;
      memcpy(out, &range, sizeof range);
      *size = sizeof range;
      return noErr;
    }
    case FOURCC('f', 's', 'i', 'z'): PUT(uint32_t, d->frames);
    case FOURCC('f', 's', 'z', '#'): {
      struct { double lo, hi; } range = {16, 4096};
      if (*size < sizeof range) return kBadPropertySize;
      memcpy(out, &range, sizeof range);
      *size = sizeof range;
      return noErr;
    }
    case FOURCC('s', 'a', 'f', 't'):
    case FOURCC('l', 't', 'n', 'c'): PUT(uint32_t, 0);
    case FOURCC('s', 't', 'm', '#'):
      if (a->scope == SCOPE_OUTPUT) {
        *size = 0;
        return noErr;
      }
      PUT(uint32_t, STREAM_IN(obj - DEVICE_IN(0)));
    case FOURCC('s', 'l', 'a', 'y'): {
      if (*size < 24) return kBadPropertySize;
      struct { uint32_t count; audio_buffer buffer; } list = {a->scope == SCOPE_INPUT ? 1u : 0u, {a->scope == SCOPE_INPUT ? (uint32_t)d->info.channels : 0u, 0, NULL}};
      memcpy(out, &list, 24);
      *size = 24;
      return noErr;
    }
    case FOURCC('s', 'f', 'm', 't'):
    case FOURCC('p', 'f', 't', ' '):  // the hardware's own format: the same, as far as the game is told
      if (!input) return kUnknownProperty;
      PUT(stream_format, f);
    case FOURCC('v', 'o', 'l', 'm'): {  // the input's level: the cable's own control, or what the game last set
      if (!input) return kUnknownProperty;
      float level = d->volume;
      if (hardware_level(d) && g_backend->level_get(&d->info, &level)) d->volume = level;
      PUT(float, level);
    }
  }
  return kUnknownProperty;
}

static int get(uint32_t obj, const prop_address *a, uint32_t *size, void *out) {
  uint32_t sel = a->selector;
  if (obj == SYSTEM) {
    switch (sel) {
      case FOURCC('d', 'e', 'v', '#'): {  // the devices: the inputs, then the output
        refresh_inputs();
        uint32_t list[MAX_INPUTS + 1], n = 0;
        for (int i = 0; i < g_ninputs; i++)
          if (g_inputs[i].present) list[n++] = DEVICE_IN(i);
        list[n++] = DEVICE_OUT;
        if (*size < n * 4) n = *size / 4;
        memcpy(out, list, n * 4);
        *size = n * 4;
        return noErr;
      }
      case FOURCC('d', 'O', 'u', 't'):                               // the default output
      case FOURCC('s', 'O', 'u', 't'): PUT(uint32_t, DEVICE_OUT);   // and the one for alerts
      case FOURCC('d', 'I', 'n', ' '):                               // the default input: the first, or none (kAudioObjectUnknown)
        refresh_inputs();
        for (int i = 0; i < g_ninputs; i++)
          if (g_inputs[i].present) PUT(uint32_t, DEVICE_IN(i));
        PUT(uint32_t, 0);
    }
    return kUnknownProperty;
  }
  if (input_of(obj)) return get_input(input_of(obj), obj, a, size, out);
  if (obj >= STREAM_IN(0) && obj < STREAM_IN(MAX_INPUTS) && input_of(obj - STREAM_IN(0) + DEVICE_IN(0))) {
    input_device *d = input_of(obj - STREAM_IN(0) + DEVICE_IN(0));
    stream_format f = {g_rate, FOURCC('l', 'p', 'c', 'm'), 0x1 | 0x8, 4u * (uint32_t)d->info.channels, 1,
                       4u * (uint32_t)d->info.channels, (uint32_t)d->info.channels, 32, 0};
    switch (sel) {
      case FOURCC('l', 't', 'n', 'c'): PUT(uint32_t, 0);
      case FOURCC('s', 'd', 'i', 'r'): PUT(uint32_t, 1);  // an input stream
      case FOURCC('s', 'c', 'h', 'n'): PUT(uint32_t, 1);
      case FOURCC('s', 'f', 'm', 't'):
      case FOURCC('p', 'f', 't', ' '): PUT(stream_format, f);
    }
    return kUnknownProperty;
  }
  if (obj == STREAM_OUT) {
    switch (sel) {
      case FOURCC('l', 't', 'n', 'c'): PUT(uint32_t, 0);
      case FOURCC('s', 'd', 'i', 'r'): PUT(uint32_t, 0);  // an output stream
      case FOURCC('s', 'c', 'h', 'n'): PUT(uint32_t, 1);  // its first channel
      case FOURCC('s', 'f', 'm', 't'):
      case FOURCC('p', 'f', 't', ' '): PUT(stream_format, device_format());
    }
    return kUnknownProperty;
  }
  if (obj != DEVICE_OUT) return kBadObject;
  int output = a->scope != SCOPE_INPUT;  // this device has nothing in the input scope
  switch (sel) {
    case FOURCC('l', 'n', 'a', 'm'): PUT(void *, dw_cf_string("Speakers"));            // its name, a CFString the asker releases
    case FOURCC('l', 'm', 'a', 'k'): PUT(void *, dw_cf_string("RS Re-ARMed"));
    case FOURCC('u', 'i', 'd', ' '): PUT(void *, dw_cf_string("rs-rearmed-output"));
    case FOURCC('t', 'r', 'a', 'n'): PUT(uint32_t, FOURCC('b', 'l', 't', 'n'));        // built in
    case FOURCC('l', 'i', 'v', 'n'): PUT(uint32_t, 1);                                  // alive
    case FOURCC('g', 'o', 'i', 'n'): PUT(uint32_t, 0);                                  // not running for anyone else
    case FOURCC('n', 's', 'r', 't'): PUT(double, g_rate);
    case FOURCC('n', 's', 'r', '#'): {                                                  // the rates it can run at: one
      struct { double lo, hi; } range = {g_rate, g_rate};
      if (*size < sizeof range) return kBadPropertySize;
      memcpy(out, &range, sizeof range);
      *size = sizeof range;
      return noErr;
    }
    case FOURCC('f', 's', 'i', 'z'): PUT(uint32_t, out_frames());
    case FOURCC('f', 's', 'z', '#'): {
      struct { double lo, hi; } range = {16, 4096};
      if (*size < sizeof range) return kBadPropertySize;
      memcpy(out, &range, sizeof range);
      *size = sizeof range;
      return noErr;
    }
    case FOURCC('s', 'a', 'f', 't'):
    case FOURCC('l', 't', 'n', 'c'): PUT(uint32_t, 0);
    case FOURCC('s', 't', 'm', '#'):
      if (!output || a->scope == SCOPE_GLOBAL) {
        if (a->scope == SCOPE_GLOBAL) PUT(uint32_t, STREAM_OUT);
        *size = 0;
        return noErr;
      }
      PUT(uint32_t, STREAM_OUT);
    case FOURCC('s', 'l', 'a', 'y'): {  // the channels in a scope, as a list of buffers
      if (*size < 24) return kBadPropertySize;
      struct { uint32_t count; audio_buffer buffer; } list = {a->scope == SCOPE_OUTPUT ? 1u : 0u, {a->scope == SCOPE_OUTPUT ? 2u : 0u, 0, NULL}};
      memcpy(out, &list, 24);
      *size = 24;
      return noErr;
    }
    case FOURCC('s', 'f', 'm', 't'):
    case FOURCC('p', 'f', 't', ' '):
      if (!output) return kUnknownProperty;
      PUT(stream_format, device_format());
    case FOURCC('d', 'c', 'h', '2'): {  // the two channels for stereo
      if (*size < 8) return kBadPropertySize;
      uint32_t pair[2] = {1, 2};
      memcpy(out, pair, 8);
      *size = 8;
      return noErr;
    }
  }
  return kUnknownProperty;
}

int dw_AudioObjectGetPropertyData(uint32_t obj, const prop_address *a, uint32_t qsize, const void *q, uint32_t *size, void *out) {
  (void)qsize, (void)q;
  int r = get(obj, a, size, out);
  note("get", obj, a, r == noErr);
  return r;
}
DW_FUNCTION("_AudioObjectGetPropertyData", dw_AudioObjectGetPropertyData);

void dw_CFRelease(void *object);

int dw_AudioObjectGetPropertyDataSize(uint32_t obj, const prop_address *a, uint32_t qsize, const void *q, uint32_t *size) {
  (void)qsize, (void)q;
  uint8_t buf[256];
  uint32_t n = sizeof buf;
  int r = get(obj, a, &n, buf);
  if (r == noErr && n == sizeof(void *) && (a->selector == FOURCC('l', 'n', 'a', 'm') || a->selector == FOURCC('l', 'm', 'a', 'k') || a->selector == FOURCC('u', 'i', 'd', ' ') || a->selector == FOURCC('m', 'u', 'i', 'd')))
    dw_CFRelease(*(void **)buf);  // the string made for the size alone
  *size = r == noErr ? n : 0;
  note("size of", obj, a, r == noErr);
  return r;
}
DW_FUNCTION("_AudioObjectGetPropertyDataSize", dw_AudioObjectGetPropertyDataSize);

unsigned char dw_AudioObjectHasProperty(uint32_t obj, const prop_address *a) {
  uint8_t buf[256];
  uint32_t n = sizeof buf;
  int r = get(obj, a, &n, buf);
  if (r == noErr && n == sizeof(void *) && (a->selector == FOURCC('l', 'n', 'a', 'm') || a->selector == FOURCC('l', 'm', 'a', 'k') || a->selector == FOURCC('u', 'i', 'd', ' ') || a->selector == FOURCC('m', 'u', 'i', 'd')))
    dw_CFRelease(*(void **)buf);
  note("has", obj, a, 1);
  return r == noErr;
}
DW_FUNCTION("_AudioObjectHasProperty", dw_AudioObjectHasProperty);

int dw_AudioObjectSetPropertyData(uint32_t obj, const prop_address *a, uint32_t qsize, const void *q, uint32_t size, const void *in) {
  (void)qsize, (void)q;
  int r = kUnknownProperty;
  if (obj == SYSTEM && a->selector == FOURCC('r', 'n', 'l', 'p')) r = noErr;  // the run loop for notifications: there is none to choose
  else if (obj == DEVICE_OUT && a->selector == FOURCC('f', 's', 'i', 'z') && size == 4) {
    uint32_t frames;
    memcpy(&frames, in, 4);
    if (frames >= 16 && frames <= 4096) g_frames = frames, r = noErr;
    else r = kUnspecified;
  } else if ((obj == DEVICE_OUT || input_of(obj)) && a->selector == FOURCC('n', 's', 'r', 't') && size == 8) {
    double rate;
    memcpy(&rate, in, 8);
    r = rate == g_rate ? noErr : kUnspecified;  // one rate
  } else if (input_of(obj) && a->selector == FOURCC('f', 's', 'i', 'z') && size == 4) {
    uint32_t frames;
    memcpy(&frames, in, 4);
    if (frames >= 16 && frames <= 4096) input_of(obj)->frames = frames, r = noErr;
    else r = kUnspecified;
  } else if (input_of(obj) && a->selector == FOURCC('v', 'o', 'l', 'm') && size == 4) {
    input_device *d = input_of(obj);
    float level;
    memcpy(&level, in, 4);
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    d->volume = level;
    if (hardware_level(d)) g_backend->level_set(&d->info, level);
    d->gain = software_gain(d);
    if (audio_log()) fprintf(stderr, "[audio] input \"%s\": level %.3f (%s)\n", d->info.name, (double)level, hardware_level(d) ? "the hardware's control" : "a gain of ours");
    r = noErr;
  }
  note("set", obj, a, r == noErr);
  return r;
}
DW_FUNCTION("_AudioObjectSetPropertyData", dw_AudioObjectSetPropertyData);

// Listeners: taken and kept. Nothing changes here that they would hear of.
int dw_AudioObjectAddPropertyListener(uint32_t obj, const prop_address *a, void *proc, void *client) {
  (void)proc, (void)client;
  note("listen for", obj, a, 1);
  return noErr;
}
DW_FUNCTION("_AudioObjectAddPropertyListener", dw_AudioObjectAddPropertyListener);

int dw_AudioObjectRemovePropertyListener(uint32_t obj, const prop_address *a, void *proc, void *client) {
  (void)obj, (void)a, (void)proc, (void)client;
  return noErr;
}
DW_FUNCTION("_AudioObjectRemovePropertyListener", dw_AudioObjectRemovePropertyListener);

// A device's own I/O procedure: the guitar's input. The game makes one on an input device and starts it; from then
// it is called on the backend's audio thread with each buffer the device captured, as CoreAudio calls it: the
// input as a list of one buffer, and no output. One on the output device is taken and never called: the game's
// sound goes through the graph below.
static void input_captured(void *ctx, const float *samples, uint32_t frames) {
  input_device *d = ctx;
  io_proc proc = d->proc;
  if (!proc) return;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  time_stamp when = {.sample_time = d->sample, .host_time = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec,
                     .rate_scalar = 1.0, .flags = 1 | 2 | 4};
  static __thread float scaled[4096 * 8];
  float gain = d->gain;
  if (gain != 1.0f && frames <= 4096) {  // an input whose level is ours to apply
    uint32_t n = frames * (uint32_t)d->info.channels;
    for (uint32_t i = 0; i < n; i++) scaled[i] = samples[i] * gain;
    samples = scaled;
  }
  struct { uint32_t count; audio_buffer buffer; } input = {1, {(uint32_t)d->info.channels, frames * 4u * (uint32_t)d->info.channels, (void *)(uintptr_t)samples}};
  struct { uint32_t count; audio_buffer buffer; } output = {0, {0, 0, NULL}};
  proc(DEVICE_IN(d - g_inputs), &when, &input, &when, &output, &when, d->client);
  d->sample += frames;
}

int dw_AudioDeviceCreateIOProcID(uint32_t device, void *proc, void *client, void **id) {
  input_device *d = input_of(device);
  if (!d && device != DEVICE_OUT) return kBadObject;
  if (d) d->proc = (io_proc)proc, d->client = client;
  if (audio_log()) fprintf(stderr, "[audio] an I/O procedure on device 0x%x%s\n", device, d ? "" : ": taken, never called");
  *id = proc;
  return noErr;
}
DW_FUNCTION("_AudioDeviceCreateIOProcID", dw_AudioDeviceCreateIOProcID);

int dw_AudioDeviceStop(uint32_t device, void *id);

int dw_AudioDeviceDestroyIOProcID(uint32_t device, void *id) {
  input_device *d = input_of(device);
  if (!d) return device == DEVICE_OUT ? noErr : kBadObject;
  dw_AudioDeviceStop(device, id);
  d->proc = NULL;
  return noErr;
}
DW_FUNCTION("_AudioDeviceDestroyIOProcID", dw_AudioDeviceDestroyIOProcID);

int dw_AudioDeviceStart(uint32_t device, void *id) {
  (void)id;
  input_device *d = input_of(device);
  if (!d) return device == DEVICE_OUT ? noErr : kBadObject;
  if (d->capture) return noErr;
  if (!d->proc || !g_backend || !g_backend->capture_start) return kIllegalOperation;
  d->gain = software_gain(d);
  d->capture = g_backend->capture_start(&d->info, g_rate, d->frames, input_captured, d);
  if (audio_log() || !d->capture)
    fprintf(stderr, "[audio] input \"%s\": %s, %u frames a call\n", d->info.name, d->capture ? "started" : "could not be started", d->frames);
  return d->capture ? noErr : kUnspecified;
}
DW_FUNCTION("_AudioDeviceStart", dw_AudioDeviceStart);

int dw_AudioDeviceStop(uint32_t device, void *id) {
  (void)id;
  input_device *d = input_of(device);
  if (!d) return device == DEVICE_OUT ? noErr : kBadObject;
  if (d->capture) {
    g_backend->capture_stop(d->capture);
    d->capture = NULL;
    if (audio_log()) fprintf(stderr, "[audio] input \"%s\": stopped\n", d->info.name);
  }
  return noErr;
}
DW_FUNCTION("_AudioDeviceStop", dw_AudioDeviceStop);

// --- the graph ---
// Up to 8 units, each with a format for each of its 8 inputs and its output, and a callback on an input. The game
// makes a mixer that it feeds through a callback and connects to the output unit; what matters here is which
// callbacks there are and in what format each wants its buffers.
#define MAX_NODES 8
#define MAX_BUSES 8
typedef int (*render_proc)(void *ref, uint32_t *flags, const time_stamp *when, uint32_t bus, uint32_t frames, buffer_list *data);

typedef struct {
  uint32_t type, subtype, manufacturer, flags, mask;  // AudioComponentDescription
} component;

typedef struct unit {
  component desc;
  struct graph *graph;
  stream_format in[MAX_BUSES], out;
  render_proc proc[MAX_BUSES];
  void *ref[MAX_BUSES];
} unit;

typedef struct graph {
  unit nodes[MAX_NODES];
  int count;
  pthread_t thread;
  _Atomic int running;
  int started;
  int backend;  // started with the backend, not the silent clock
  double sample;
  float peak;
  uint32_t peak_frames;
} graph;

// What the game plays for the next FRAMES frames, as two channels: each callback of the graph is called with
// buffers in the format its unit was given, and what they fill is summed.
static void graph_render(void *ctx, float *left, float *right, uint32_t frames) {
  graph *g = ctx;
  static __thread float scratch[MAX_BUSES][4096 * 2];
  if (frames > 4096) frames = 4096;
  memset(left, 0, frames * sizeof(float));
  memset(right, 0, frames * sizeof(float));
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  time_stamp when = {.sample_time = g->sample, .host_time = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec,
                     .rate_scalar = 1.0, .flags = 1 | 2 | 4};  // sample time, host time and rate valid
  for (int n = 0; n < g->count; n++)
    for (uint32_t bus = 0; bus < MAX_BUSES; bus++) {
      unit *u = &g->nodes[n];
      if (!u->proc[bus]) continue;
      const stream_format *f = u->in[bus].channels ? &u->in[bus] : &u->out;
      uint32_t channels = f->channels ? f->channels : 2;
      int planar = (f->flags & 0x20) != 0, is_float = !f->format || ((f->flags & 0x1) && f->bits == 32);
      buffer_list list = {0};
      memset(scratch, 0, sizeof(float) * 2 * 4096 * (planar ? (channels > MAX_BUSES ? MAX_BUSES : channels) : 1));
      if (planar) {  // one buffer a channel
        list.count = channels > 8 ? 8 : channels;
        for (uint32_t c = 0; c < list.count; c++) list.buffers[c] = (audio_buffer){1, frames * 4, scratch[c]};
      } else {
        if (channels > 2) channels = 2;
        list.count = 1;
        list.buffers[0] = (audio_buffer){channels, frames * 4 * channels, scratch[0]};
      }
      uint32_t flags = 0;
      if (u->proc[bus](u->ref[bus], &flags, &when, bus, frames, &list) != 0 || !is_float) continue;  // an error, or a format not mixed here
      if (planar) {
        const float *l = list.buffers[0].data, *r = list.count > 1 ? list.buffers[1].data : list.buffers[0].data;
        for (uint32_t i = 0; i < frames; i++) left[i] += l[i], right[i] += r[i];
      } else {
        const float *d = list.buffers[0].data;
        for (uint32_t i = 0; i < frames; i++) left[i] += d[i * channels], right[i] += d[i * channels + (channels > 1)];
      }
    }
  g->sample += frames;
  if (audio_log()) {  // the loudest sample of each five seconds, to see that the game plays something
    float peak = g->peak;
    for (uint32_t i = 0; i < frames; i++) {
      float a = left[i] < 0 ? -left[i] : left[i], b = right[i] < 0 ? -right[i] : right[i];
      if (a > peak) peak = a;
      if (b > peak) peak = b;
    }
    g->peak = peak;
    if ((g->peak_frames += frames) >= (uint32_t)(5 * g_rate)) {
      fprintf(stderr, "[audio] the last five seconds' loudest sample: %.4f\n", (double)g->peak);
      g->peak = 0, g->peak_frames = 0;
    }
  }
}

// With no backend: the callback on the clock, the sound dropped.
static void *graph_thread(void *arg) {
  graph *g = arg;
  pthread_setname_np(pthread_self(), "rs-audio-silent");
  static float left[4096], right[4096];
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  while (atomic_load(&g->running)) {
    uint32_t frames = out_frames() > 4096 ? 4096 : out_frames();
    graph_render(g, left, right, frames);
    long ns = (long)(1e9 * frames / g_rate);
    next.tv_nsec += ns;
    while (next.tv_nsec >= 1000000000L) next.tv_nsec -= 1000000000L, next.tv_sec++;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
  }
  return NULL;
}

void dw_audio_set_backend(const dw_audio_backend *backend) { g_backend = backend; }

int dw_NewAUGraph(graph **out) {
  *out = calloc(1, sizeof(graph));
  return *out ? noErr : -108;  // memFullErr
}
DW_FUNCTION("_NewAUGraph", dw_NewAUGraph);

int dw_AUGraphStop(graph *g);

int dw_DisposeAUGraph(graph *g) {
  if (!g) return -50;  // paramErr
  dw_AUGraphStop(g);
  free(g);
  return noErr;
}
DW_FUNCTION("_DisposeAUGraph", dw_DisposeAUGraph);

int dw_AUGraphAddNode(graph *g, const component *desc, int32_t *node) {
  if (!g || g->count == MAX_NODES) return -50;
  unit *u = &g->nodes[g->count];
  u->desc = *desc;
  u->graph = g;
  *node = ++g->count;  // nodes are numbered from 1
  if (audio_log()) {
    char t[5], s[5];
    fprintf(stderr, "[audio] graph: node %d is a '%s' unit of kind '%s'\n", *node, fourcc(desc->type, t), fourcc(desc->subtype, s));
  }
  return noErr;
}
DW_FUNCTION("_AUGraphAddNode", dw_AUGraphAddNode);

static unit *node_of(graph *g, int32_t node) { return g && node >= 1 && node <= g->count ? &g->nodes[node - 1] : NULL; }

int dw_AUGraphNodeInfo(graph *g, int32_t node, component *desc, unit **out) {
  unit *u = node_of(g, node);
  if (!u) return -10860;  // kAUGraphErr_NodeNotFound
  if (desc) *desc = u->desc;
  if (out) *out = u;
  return noErr;
}
DW_FUNCTION("_AUGraphNodeInfo", dw_AUGraphNodeInfo);

int dw_AUGraphOpen(graph *g) { return g ? noErr : -50; }
DW_FUNCTION("_AUGraphOpen", dw_AUGraphOpen);
int dw_AUGraphClose(graph *g) { return g ? noErr : -50; }
DW_FUNCTION("_AUGraphClose", dw_AUGraphClose);
int dw_AUGraphInitialize(graph *g) { return g ? noErr : -50; }
DW_FUNCTION("_AUGraphInitialize", dw_AUGraphInitialize);

int dw_AUGraphConnectNodeInput(graph *g, int32_t from, uint32_t output, int32_t to, uint32_t input) {
  if (!node_of(g, from) || !node_of(g, to)) return -10860;
  if (audio_log()) fprintf(stderr, "[audio] graph: node %d output %u feeds node %d input %u\n", from, output, to, input);
  return noErr;
}
DW_FUNCTION("_AUGraphConnectNodeInput", dw_AUGraphConnectNodeInput);

typedef struct {
  render_proc proc;
  void *ref;
} render_callback;

int dw_AUGraphSetNodeInputCallback(graph *g, int32_t node, uint32_t input, const render_callback *cb) {
  unit *u = node_of(g, node);
  if (!u || input >= MAX_BUSES) return -10860;
  u->ref[input] = cb->ref;
  u->proc[input] = cb->proc;
  if (audio_log()) fprintf(stderr, "[audio] graph: the game fills node %d input %u\n", node, input);
  return noErr;
}
DW_FUNCTION("_AUGraphSetNodeInputCallback", dw_AUGraphSetNodeInputCallback);

int dw_AUGraphStart(graph *g) {
  if (!g) return -50;
  if (g->started) return noErr;
  if (g_backend && g_backend->start(g_rate, out_frames(), graph_render, g) == 0) {
    g->backend = 1;
  } else {
    atomic_store(&g->running, 1);
    if (pthread_create(&g->thread, NULL, graph_thread, g)) {
      atomic_store(&g->running, 0);
      return kUnspecified;
    }
  }
  g->started = 1;
  if (audio_log())
    fprintf(stderr, "[audio] graph: started, %s, %u frames a call at %.0f Hz\n", g->backend ? g_backend->name : "silent", out_frames(), g_rate);
  return noErr;
}
DW_FUNCTION("_AUGraphStart", dw_AUGraphStart);

int dw_AUGraphStop(graph *g) {
  if (!g) return -50;
  if (!g->started) return noErr;
  if (g->backend) {
    g_backend->stop();
    g->backend = 0;
  } else {
    atomic_store(&g->running, 0);
    pthread_join(g->thread, NULL);
  }
  g->started = 0;
  return noErr;
}
DW_FUNCTION("_AUGraphStop", dw_AUGraphStop);

// A unit's properties: the stream formats are kept (8: kAudioUnitProperty_StreamFormat), the rest is accepted.
int dw_AudioUnitSetProperty(unit *u, uint32_t id, uint32_t scope, uint32_t element, const void *data, uint32_t size) {
  if (!u) return -50;
  if (id == 8 && size == sizeof(stream_format)) {
    if (scope == 1 && element < MAX_BUSES) memcpy(&u->in[element], data, size);  // kAudioUnitScope_Input
    else if (scope == 2) memcpy(&u->out, data, size);                            // kAudioUnitScope_Output
  }
  if (audio_log()) {
    char t[5];
    fprintf(stderr, "[audio] unit '%s': property %u set (scope %u, element %u, %u bytes)\n", fourcc(u->desc.subtype, t), id, scope, element, size);
    if (id == 8 && size == sizeof(stream_format)) {
      const stream_format *f = data;
      fprintf(stderr, "[audio]   format: %.0f Hz, %u channels, %u bits, flags 0x%x, %u bytes a frame\n", f->rate, f->channels, f->bits, f->flags, f->bytes_per_frame);
    }
  }
  return noErr;
}
DW_FUNCTION("_AudioUnitSetProperty", dw_AudioUnitSetProperty);

int dw_AudioUnitSetParameter(unit *u, uint32_t id, uint32_t scope, uint32_t element, float value, uint32_t offset) {
  (void)id, (void)scope, (void)element, (void)value, (void)offset;
  return u ? noErr : -50;
}
DW_FUNCTION("_AudioUnitSetParameter", dw_AudioUnitSetParameter);

int dw_AudioUnitUninitialize(unit *u) { return u ? noErr : -50; }
DW_FUNCTION("_AudioUnitUninitialize", dw_AudioUnitUninitialize);

int dw_AudioOutputUnitStop(unit *u) { return u ? dw_AUGraphStop(u->graph) : -50; }
DW_FUNCTION("_AudioOutputUnitStop", dw_AudioOutputUnitStop);
