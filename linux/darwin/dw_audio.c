// CoreAudio's devices and the audio graph, in a silent form: the model the game's audio code is written against,
// with nothing behind it that makes a sound yet.
//
// The game asks the hardware layer (AudioObject...) which devices there are and what they can do, builds a graph of
// audio units (AUGraph: a mixer into a device output) with a callback that Wwise fills, and for the guitar opens
// an input device with an I/O procedure. Here there is one output device, stereo at 48,000 Hz, and no input.
// The graph is kept as the game made it, and while it runs a thread calls the game's callback once a buffer, on
// the clock, and throws the sound away: so Wwise runs, and the game with it. A sound backend takes the thread's
// place later (linux/PLAN.md, X3).
//
// RS_AUDIO_LOG=1 logs every question once; a question with no answer is always logged once.
#define _GNU_SOURCE
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

static int get(uint32_t obj, const prop_address *a, uint32_t *size, void *out) {
  uint32_t sel = a->selector;
  if (obj == SYSTEM) {
    switch (sel) {
      case FOURCC('d', 'e', 'v', '#'): PUT(uint32_t, DEVICE_OUT);  // the devices: one
      case FOURCC('d', 'O', 'u', 't'):                               // the default output
      case FOURCC('s', 'O', 'u', 't'): PUT(uint32_t, DEVICE_OUT);   // and the one for alerts
      case FOURCC('d', 'I', 'n', ' '): PUT(uint32_t, 0);            // no input: kAudioObjectUnknown
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
    case FOURCC('f', 's', 'i', 'z'): PUT(uint32_t, g_frames);
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
  if (r == noErr && n == sizeof(void *) && (a->selector == FOURCC('l', 'n', 'a', 'm') || a->selector == FOURCC('l', 'm', 'a', 'k') || a->selector == FOURCC('u', 'i', 'd', ' ')))
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
  if (r == noErr && n == sizeof(void *) && (a->selector == FOURCC('l', 'n', 'a', 'm') || a->selector == FOURCC('l', 'm', 'a', 'k') || a->selector == FOURCC('u', 'i', 'd', ' ')))
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
  } else if (obj == DEVICE_OUT && a->selector == FOURCC('n', 's', 'r', 't') && size == 8) {
    double rate;
    memcpy(&rate, in, 8);
    r = rate == g_rate ? noErr : kUnspecified;  // one rate
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

// A device's own I/O procedure (the guitar's input device on the Mac). The one device here is asked for by the
// graph below; a procedure on it is taken and never called.
int dw_AudioDeviceCreateIOProcID(uint32_t device, void *proc, void *client, void **id) {
  (void)client;
  if (device != DEVICE_OUT) return kBadObject;
  if (audio_log()) fprintf(stderr, "[audio] an I/O procedure on device 0x%x: taken, never called\n", device);
  *id = proc;
  return noErr;
}
DW_FUNCTION("_AudioDeviceCreateIOProcID", dw_AudioDeviceCreateIOProcID);

int dw_AudioDeviceDestroyIOProcID(uint32_t device, void *id) { return (void)id, device == DEVICE_OUT ? noErr : kBadObject; }
DW_FUNCTION("_AudioDeviceDestroyIOProcID", dw_AudioDeviceDestroyIOProcID);
int dw_AudioDeviceStart(uint32_t device, void *id) { return (void)id, device == DEVICE_OUT ? noErr : kBadObject; }
DW_FUNCTION("_AudioDeviceStart", dw_AudioDeviceStart);
int dw_AudioDeviceStop(uint32_t device, void *id) { return (void)id, device == DEVICE_OUT ? noErr : kBadObject; }
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
} graph;

static void *graph_thread(void *arg) {
  graph *g = arg;
  pthread_setname_np(pthread_self(), "rs-audio-silent");
  static float scratch[MAX_BUSES][4096 * 8];
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  double sample = 0;
  while (atomic_load(&g->running)) {
    uint32_t frames = g_frames > 4096 ? 4096 : g_frames;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_stamp when = {.sample_time = sample, .host_time = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec,
                       .rate_scalar = 1.0, .flags = 1 | 2 | 4};  // sample time, host time and rate valid
    for (int n = 0; n < g->count; n++)
      for (uint32_t bus = 0; bus < MAX_BUSES; bus++) {
        unit *u = &g->nodes[n];
        if (!u->proc[bus]) continue;
        const stream_format *f = u->in[bus].channels ? &u->in[bus] : &u->out;
        uint32_t channels = f->channels ? f->channels : 2, bytes = f->bytes_per_frame ? f->bytes_per_frame : 4;
        buffer_list list = {0};
        if (f->flags & 0x20) {  // one buffer a channel
          list.count = channels > 8 ? 8 : channels;
          for (uint32_t c = 0; c < list.count; c++)
            list.buffers[c] = (audio_buffer){1, frames * bytes, scratch[c]};
        } else {
          list.count = 1;
          list.buffers[0] = (audio_buffer){channels, frames * bytes, scratch[0]};
        }
        uint32_t flags = 0;
        u->proc[bus](u->ref[bus], &flags, &when, bus, frames, &list);
      }
    sample += frames;
    long ns = (long)(1e9 * frames / g_rate);
    next.tv_nsec += ns;
    while (next.tv_nsec >= 1000000000L) next.tv_nsec -= 1000000000L, next.tv_sec++;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
  }
  return NULL;
}

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
  atomic_store(&g->running, 1);
  if (pthread_create(&g->thread, NULL, graph_thread, g)) {
    atomic_store(&g->running, 0);
    return kUnspecified;
  }
  g->started = 1;
  if (audio_log()) fprintf(stderr, "[audio] graph: started, silent, %u frames a call at %.0f Hz\n", g_frames, g_rate);
  return noErr;
}
DW_FUNCTION("_AUGraphStart", dw_AUGraphStart);

int dw_AUGraphStop(graph *g) {
  if (!g) return -50;
  if (!g->started) return noErr;
  atomic_store(&g->running, 0);
  pthread_join(g->thread, NULL);
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
