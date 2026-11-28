// The game's sound to PipeWire: the backend of the Darwin layer's audio graph (linux/darwin/dw_audio.c).
//
// One playback stream, stereo 32-bit float at the graph's rate, asking for the graph's buffer (256 frames at
// 48,000 Hz: 5.3 ms) as its latency; PipeWire gives what the rest of its graph allows. Its process callback runs on
// PipeWire's real-time thread and wants a count of frames of its own choosing, while the game is asked in buffers
// of its own size, as CoreAudio asks it: the frames between the two wait in a small queue.
//
// The inputs are PipeWire's capture devices: listed for the game by their cards' names, and opened as capture
// streams that feed the game's input procedure.
#define _GNU_SOURCE
#include <pipewire/pipewire.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <spa/param/audio/format-utils.h>
#include <stdatomic.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dw.h"
#include "rsl.h"

static struct {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;
  struct spa_hook listener;
  dw_audio_render render;
  void *ctx;
  uint32_t frames;       // the game's buffer
  float left[8192], right[8192];
  uint32_t have;         // frames waiting in the queue
  int told;
} g;

static void on_process(void *data) {
  (void)data;
  struct pw_buffer *b = pw_stream_dequeue_buffer(g.stream);
  if (!b) return;
  struct spa_data *d = &b->buffer->datas[0];
  float *out = d->data;
  if (!out) {
    pw_stream_queue_buffer(g.stream, b);
    return;
  }
  uint32_t want = d->maxsize / (2 * sizeof(float));
  if (b->requested && b->requested < want) want = (uint32_t)b->requested;
  if (want > 4096) want = 4096;
  if (!g.told) {
    g.told = 1;
    fprintf(stderr, "[audio] PipeWire asks for %u frames at a time (%.1f ms); the game is asked for %u\n", want,
            1000.0 * want / 48000.0, g.frames);
  }
  while (g.have < want) {  // the game, a buffer of its own size at a time
    g.render(g.ctx, g.left + g.have, g.right + g.have, g.frames);
    g.have += g.frames;
  }
  for (uint32_t i = 0; i < want; i++) out[2 * i] = g.left[i], out[2 * i + 1] = g.right[i];
  g.have -= want;
  memmove(g.left, g.left + want, g.have * sizeof(float));
  memmove(g.right, g.right + want, g.have * sizeof(float));
  d->chunk->offset = 0;
  d->chunk->stride = 2 * sizeof(float);
  d->chunk->size = want * 2 * sizeof(float);
  pw_stream_queue_buffer(g.stream, b);
}

static const struct pw_stream_events k_events = {PW_VERSION_STREAM_EVENTS, .process = on_process};

static int start(double rate, uint32_t frames, dw_audio_render render, void *ctx) {
  if (frames < 16 || frames > 4096) return -1;
  pw_init(NULL, NULL);
  g.render = render, g.ctx = ctx, g.frames = frames, g.have = 0, g.told = 0;
  g.loop = pw_thread_loop_new("rs-audio", NULL);
  if (!g.loop) return -1;
  char latency[32], hz[16];
  snprintf(latency, sizeof latency, "%u/%u", frames, (unsigned)rate);
  snprintf(hz, sizeof hz, "1/%u", (unsigned)rate);
  struct pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Game",
                                                  PW_KEY_APP_NAME, "Rocksmith 2014", PW_KEY_NODE_NAME, "rs-rearmed",
                                                  PW_KEY_NODE_LATENCY, latency, PW_KEY_NODE_RATE, hz, NULL);
  // RS_AUDIO_OUTPUT_NODE: the output's node by name, where it is not to be the desktop's default (latency_test.py's
  // made-up output; PipeWire's own PIPEWIRE_NODE would send the inputs' streams there as well)
  const char *to = getenv("RS_AUDIO_OUTPUT_NODE");
  if (to && *to) pw_properties_set(props, PW_KEY_TARGET_OBJECT, to);
  g.stream = pw_stream_new_simple(pw_thread_loop_get_loop(g.loop), "Rocksmith 2014", props, &k_events, NULL);
  if (!g.stream) {
    pw_thread_loop_destroy(g.loop);
    g.loop = NULL;
    return -1;
  }
  uint8_t buffer[1024];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof buffer);
  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat,
                                         &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .channels = 2, .rate = (uint32_t)rate));
  if (pw_stream_connect(g.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS, params, 1) < 0 ||
      pw_thread_loop_start(g.loop) < 0) {
    fprintf(stderr, "[audio] no PipeWire stream: the game runs without sound\n");
    pw_stream_destroy(g.stream);
    pw_thread_loop_destroy(g.loop);
    g.stream = NULL, g.loop = NULL;
    return -1;
  }
  return 0;
}

static void stop(void) {
  if (!g.loop) return;
  pw_thread_loop_stop(g.loop);
  pw_stream_destroy(g.stream);
  pw_thread_loop_destroy(g.loop);
  g.stream = NULL, g.loop = NULL;
}

// --- the inputs: PipeWire's capture devices ---
// Asked each time the game lists its devices: a short connection of its own that reads the registry's nodes of
// class Audio/Source with their properties. An ALSA device is named as its card is ("Rocksmith USB Guitar
// Adapter": the name the game knows the cable by), its maker is what the card's long name has in front of that
// ("Hercules"), and its model is the name with the USB ids, as CoreAudio's model identifier is.
#define MAX_FOUND 48  // sources and sinks
static struct {
  struct pw_main_loop *loop;
  struct pw_core *core;
  struct pw_registry *registry;
  int pending;
  struct {
    struct pw_proxy *proxy;
    struct spa_hook listener;
    dw_audio_input input;
    int have;
  } nodes[MAX_FOUND];
  int n;
  int sink_cards[MAX_FOUND], nsinks;  // the ALSA cards that have an output: an input of one is an interface's
} e;

static void copy(char *to, size_t n, const char *from) { snprintf(to, n, "%s", from ? from : ""); }

static void node_info(void *data, const struct pw_node_info *info) {
  int i = (int)(intptr_t)data;
  if (!info || !info->props || e.nodes[i].have) return;
  const struct spa_dict *p = info->props;
  const char *cls = spa_dict_lookup(p, "media.class"), *uid = spa_dict_lookup(p, "node.name");
  if (cls && !strcmp(cls, "Audio/Sink")) {
    const char *card = spa_dict_lookup(p, "api.alsa.pcm.card");
    if (card && e.nsinks < MAX_FOUND) e.sink_cards[e.nsinks++] = atoi(card);
    e.nodes[i].have = -1;  // (heard, and no input)
    return;
  }
  if (!cls || strcmp(cls, "Audio/Source") || !uid) return;
  // RS_AUDIO_ONLY_INPUT: the one input listed, by a part of its node's name (latency_test.py's made-up input)
  const char *only = getenv("RS_AUDIO_ONLY_INPUT");
  if (only && *only && !strstr(uid, only)) return;
  dw_audio_input *in = &e.nodes[i].input;
  const char *card = spa_dict_lookup(p, "api.alsa.card.name"), *nick = spa_dict_lookup(p, "node.nick");
  const char *name = card ? card : nick ? nick : spa_dict_lookup(p, "node.description");
  const char *longname = spa_dict_lookup(p, "alsa.long_card_name"), *ids = spa_dict_lookup(p, "alsa.components");
  const char *channels = spa_dict_lookup(p, "audio.channels"), *bus = spa_dict_lookup(p, "device.bus");
  const char *alsa_card = spa_dict_lookup(p, "api.alsa.pcm.card");
  in->card = alsa_card ? atoi(alsa_card) : -1;
  copy(in->name, sizeof in->name, name ? name : uid);
  copy(in->uid, sizeof in->uid, uid);
  in->maker[0] = 0;
  const char *at = longname && name ? strstr(longname, name) : NULL;
  if (at > longname) {  // "Hercules Rocksmith USB Guitar Adapter at usb-...": what is in front of the name
    size_t n = (size_t)(at - longname);
    while (n && longname[n - 1] == ' ') n--;
    if (n >= sizeof in->maker) n = sizeof in->maker - 1;
    memcpy(in->maker, longname, n);
    in->maker[n] = 0;
  }
  in->model[0] = 0;
  unsigned vendor, product;
  if (ids && sscanf(ids, "USB%4x:%4x", &vendor, &product) == 2) {
    char model[sizeof in->model];
    snprintf(model, sizeof model, "%s:%04X:%04X", name ? name : uid, vendor, product);
    copy(in->model, sizeof in->model, model);
  }
  in->channels = channels ? atoi(channels) : 1;
  if (in->channels < 1) in->channels = 1;
  if (in->channels > DW_AUDIO_MAX_CHANNELS) in->channels = DW_AUDIO_MAX_CHANNELS;
  in->usb = bus && !strcmp(bus, "usb");
  e.nodes[i].have = 1;
}

static const struct pw_node_events k_node_events = {PW_VERSION_NODE_EVENTS, .info = node_info};

static void registry_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
  (void)data, (void)permissions, (void)version;
  if (strcmp(type, PW_TYPE_INTERFACE_Node) || e.n == MAX_FOUND) return;
  const char *cls = props ? spa_dict_lookup(props, "media.class") : NULL;
  if (!cls || (strcmp(cls, "Audio/Source") && strcmp(cls, "Audio/Sink"))) return;
  int i = e.n++;
  e.nodes[i].have = 0;
  e.nodes[i].proxy = pw_registry_bind(e.registry, id, type, PW_VERSION_NODE, 0);
  if (e.nodes[i].proxy) pw_proxy_add_object_listener(e.nodes[i].proxy, &e.nodes[i].listener, &k_node_events, (void *)(intptr_t)i);
}

static const struct pw_registry_events k_registry_events = {PW_VERSION_REGISTRY_EVENTS, .global = registry_global};

static void core_done(void *data, uint32_t id, int seq) {
  (void)data;
  if (id == PW_ID_CORE && seq == e.pending) pw_main_loop_quit(e.loop);
}

static const struct pw_core_events k_core_events = {PW_VERSION_CORE_EVENTS, .done = core_done};

static int inputs(dw_audio_input *list, int max) {
  pw_init(NULL, NULL);
  memset(&e, 0, sizeof e);
  e.loop = pw_main_loop_new(NULL);
  struct pw_context *context = e.loop ? pw_context_new(pw_main_loop_get_loop(e.loop), NULL, 0) : NULL;
  e.core = context ? pw_context_connect(context, NULL, 0) : NULL;
  int n = 0;
  if (e.core) {
    struct spa_hook core_listener, registry_listener;
    pw_core_add_listener(e.core, &core_listener, &k_core_events, NULL);
    e.registry = pw_core_get_registry(e.core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(e.registry, &registry_listener, &k_registry_events, NULL);
    for (int round = 0; round < 2; round++) {  // the nodes, then what each says of itself
      e.pending = pw_core_sync(e.core, PW_ID_CORE, 0);
      pw_main_loop_run(e.loop);
    }
    for (int i = 0; i < e.n; i++) {
      if (e.nodes[i].have > 0 && n < max) {
        list[n] = e.nodes[i].input;
        for (int k = 0; k < e.nsinks; k++) list[n].outputs |= list[n].card >= 0 && e.sink_cards[k] == list[n].card;
        n++;
      }
      if (e.nodes[i].proxy) pw_proxy_destroy(e.nodes[i].proxy);
    }
    pw_proxy_destroy((struct pw_proxy *)e.registry);
    pw_core_disconnect(e.core);
  }
  if (context) pw_context_destroy(context);
  if (e.loop) pw_main_loop_destroy(e.loop);
  return n;
}

// --- an open input: a capture stream on that node ---
// As for the sound out: PipeWire hands over what it has, in counts of its own, and the game's input procedure is
// called with buffers of the size the game set on the device, so the frames wait in a queue between.
struct capture {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;
  dw_audio_capture capture;
  void *ctx;
  uint32_t frames, channels, have, told;
  float queue[8192 * DW_AUDIO_MAX_CHANNELS];
};

static void on_capture(void *data) {
  struct capture *c = data;
  struct pw_buffer *b = pw_stream_dequeue_buffer(c->stream);
  if (!b) return;
  struct spa_data *d = &b->buffer->datas[0];
  if (d->data && d->chunk->size) {
    const float *in = (const float *)((const uint8_t *)d->data + d->chunk->offset);
    uint32_t n = d->chunk->size / (c->channels * sizeof(float));
    if (!c->told) {
      c->told = 1;
      fprintf(stderr, "[audio] PipeWire gives the input %u frames at a time (%.1f ms); the game is given %u\n", n, 1000.0 * n / 48000.0, c->frames);
    }
    while (n) {
      uint32_t room = 8192 - c->have, take = n < room ? n : room;
      memcpy(c->queue + (size_t)c->have * c->channels, in, (size_t)take * c->channels * sizeof(float));
      c->have += take, in += (size_t)take * c->channels, n -= take;
      uint32_t done = 0;
      while (c->have - done >= c->frames) {
        c->capture(c->ctx, c->queue + (size_t)done * c->channels, c->frames);
        done += c->frames;
      }
      c->have -= done;
      memmove(c->queue, c->queue + (size_t)done * c->channels, (size_t)c->have * c->channels * sizeof(float));
      if (!take) break;
    }
  }
  pw_stream_queue_buffer(c->stream, b);
}

static const struct pw_stream_events k_capture_events = {PW_VERSION_STREAM_EVENTS, .process = on_capture};

static void *capture_start(const dw_audio_input *input, double rate, uint32_t frames, dw_audio_capture capture, void *ctx) {
  if (frames < 16 || frames > 4096) return NULL;
  pw_init(NULL, NULL);
  struct capture *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->capture = capture, c->ctx = ctx, c->frames = frames, c->channels = (uint32_t)input->channels;
  c->loop = pw_thread_loop_new("rs-audio-in", NULL);
  char latency[32], hz[16];
  snprintf(latency, sizeof latency, "%u/%u", frames, (unsigned)rate);
  snprintf(hz, sizeof hz, "1/%u", (unsigned)rate);
  struct pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Game",
                                                  PW_KEY_APP_NAME, "Rocksmith 2014", PW_KEY_NODE_NAME, "rs-rearmed-input",
                                                  PW_KEY_TARGET_OBJECT, input->uid, PW_KEY_NODE_LATENCY, latency, PW_KEY_NODE_RATE, hz,
                                                  PW_KEY_STREAM_DONT_REMIX, "true",
                                                  // (its device pulled out: the input ends; it is not another's)
                                                  PW_KEY_NODE_DONT_RECONNECT, "true", NULL);
  c->stream = c->loop ? pw_stream_new_simple(pw_thread_loop_get_loop(c->loop), "Rocksmith 2014 guitar input", props, &k_capture_events, c) : NULL;
  uint8_t buffer[1024];
  struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof buffer);
  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat,
                                         &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .channels = c->channels, .rate = (uint32_t)rate));
  if (!c->stream ||
      pw_stream_connect(c->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS, params, 1) < 0 ||
      pw_thread_loop_start(c->loop) < 0) {
    if (c->stream) pw_stream_destroy(c->stream);
    if (c->loop) pw_thread_loop_destroy(c->loop);
    free(c);
    return NULL;
  }
  return c;
}

static void capture_stop(void *handle) {
  struct capture *c = handle;
  pw_thread_loop_stop(c->loop);
  pw_stream_destroy(c->stream);
  pw_thread_loop_destroy(c->loop);
  free(c);
}

// The input's own level: its ALSA card's capture control.
#ifdef RSL_HAVE_ALSA
static int level_get(const dw_audio_input *input, float *level) { return rsl_alsa_level_get(input->card, level); }
static int level_set(const dw_audio_input *input, float level) { return rsl_alsa_level_set(input->card, level); }
#else
#define level_get NULL
#define level_set NULL
#endif

// --- inputs that come and go ---
// A connection kept for the registry alone: every audio node that appears or goes raises a count, which the layer
// looks at while the game listens for changes of its device list (dw.h).
static struct {
  struct pw_thread_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct pw_registry *registry;
  struct spa_hook listener;
  uint32_t nodes[256];
  int n;
  atomic_uint count;
  int started;
} w;

static void watch_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
  (void)data, (void)permissions, (void)version;
  if (strcmp(type, PW_TYPE_INTERFACE_Node)) return;
  const char *cls = props ? spa_dict_lookup(props, "media.class") : NULL;
  if (!cls || (strcmp(cls, "Audio/Source") && strcmp(cls, "Audio/Sink"))) return;
  if (w.n < (int)(sizeof w.nodes / sizeof *w.nodes)) w.nodes[w.n++] = id;
  atomic_fetch_add(&w.count, 1);
}

static void watch_global_remove(void *data, uint32_t id) {
  (void)data;
  for (int i = 0; i < w.n; i++)
    if (w.nodes[i] == id) {
      w.nodes[i] = w.nodes[--w.n];
      atomic_fetch_add(&w.count, 1);
      return;
    }
}

static const struct pw_registry_events k_watch_events = {PW_VERSION_REGISTRY_EVENTS, .global = watch_global, .global_remove = watch_global_remove};

static pthread_once_t g_watch_once = PTHREAD_ONCE_INIT;
static void watch_start(void) {
  pw_init(NULL, NULL);
  w.loop = pw_thread_loop_new("rs-audio-watch", NULL);
  w.context = w.loop ? pw_context_new(pw_thread_loop_get_loop(w.loop), NULL, 0) : NULL;
  w.core = w.context ? pw_context_connect(w.context, NULL, 0) : NULL;
  if (w.core) {
    w.registry = pw_core_get_registry(w.core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(w.registry, &w.listener, &k_watch_events, NULL);
  }
  if (w.loop) pw_thread_loop_start(w.loop);
}

static unsigned changes(void) {
  pthread_once(&g_watch_once, watch_start);
  return atomic_load(&w.count);
}

// The calling thread real-time, as PipeWire's own audio threads are: by the scheduler itself where the user may ask
// it (a limit on real-time priority above none), else through RTKit, the system service a desktop has for it, which
// gives round robin at up to 20 to a thread whose process has a limit on its real-time run time and that gives the
// policy up on a fork. (PipeWire's own helper for this serves only threads it made itself.) As PipeWire does, the
// desktop's portal is asked first (the session's D-Bus: it speaks to RTKit for the session's programs, also one
// started from another session, which RTKit itself turns away), then RTKit on the system's D-Bus; with the D-Bus
// library every system with PipeWire has, loaded when it is first wanted.
static int rtkit_realtime(int system_bus, const char *service, const char *path, const char *interface, pid_t tid, unsigned priority) {
  static void *lib;
  static void *(*bus_get_private)(int, void *), *(*message_new)(const char *, const char *, const char *, const char *);
  static int (*append_args)(void *, int, ...);
  static void *(*send_and_block)(void *, void *, int, void *);
  static void (*message_unref)(void *), (*connection_close)(void *), (*connection_unref)(void *), (*error_init)(void *),
      (*error_free)(void *), (*set_exit)(void *, int);
  if (!lib) {
    lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return -ENOENT;
    bus_get_private = dlsym(lib, "dbus_bus_get_private"), message_new = dlsym(lib, "dbus_message_new_method_call");
    append_args = dlsym(lib, "dbus_message_append_args"), send_and_block = dlsym(lib, "dbus_connection_send_with_reply_and_block");
    message_unref = dlsym(lib, "dbus_message_unref"), connection_close = dlsym(lib, "dbus_connection_close");
    connection_unref = dlsym(lib, "dbus_connection_unref"), error_init = dlsym(lib, "dbus_error_init");
    error_free = dlsym(lib, "dbus_error_free"), set_exit = dlsym(lib, "dbus_connection_set_exit_on_disconnect");
  }
  if (!bus_get_private || !message_new || !append_args || !send_and_block || !message_unref || !connection_close ||
      !connection_unref || !error_init || !error_free)
    return -ENOSYS;
  uint64_t error[8];  // a DBusError, with room to spare
  error_init(error);
  void *bus = bus_get_private(system_bus ? 1 : 0, error);
  if (!bus) {
    error_free(error);
    return -ECONNREFUSED;
  }
  if (set_exit) set_exit(bus, 0);
  int r = -EIO;
  void *call = message_new(service, path, interface, "MakeThreadRealtimeWithPID");
  if (call) {
    uint64_t process = (uint64_t)getpid(), thread = (uint64_t)tid;
    uint32_t prio = priority;
    if (append_args(call, 't', &process, 't', &thread, 'u', &prio, 0)) {
      void *reply = send_and_block(bus, call, 2000, error);
      if (reply) {
        message_unref(reply);
        r = 0;
      } else {
        const char *name = ((const char **)error)[0], *message = ((const char **)error)[1];  // (a DBusError's first two)
        if (getenv("RS_AUDIO_LOG"))
          fprintf(stderr, "[audio] %s, priority %u: %s (%s)\n", service, prio, message ? message : "refused", name ? name : "no reason given");
        r = -EPERM;
      }
    }
    message_unref(call);
  }
  error_free(error);
  connection_close(bus);
  connection_unref(bus);
  return r;
}

static int realtime(void) {
  struct sched_param none = {0}, rr = {.sched_priority = 20};
  struct rlimit limit;
  if (!sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &rr)) return 0;  // the user may ask the scheduler itself
  // RTKit's conditions: the policy given up on a fork, and a limit on the process's real-time run time (200 ms,
  // PipeWire's own figure, where there is none or a longer one).
  sched_setscheduler(0, SCHED_OTHER | SCHED_RESET_ON_FORK, &none);
  if (!getrlimit(RLIMIT_RTTIME, &limit) && (limit.rlim_max == RLIM_INFINITY || limit.rlim_max > 200000)) {
    limit.rlim_cur = limit.rlim_max = 200000;  // (both: RTKit looks at the hard one; PipeWire sets the same two)
    setrlimit(RLIMIT_RTTIME, &limit);
  }
  pid_t tid = (pid_t)syscall(SYS_gettid);
  if (getenv("RS_AUDIO_LOG")) {
    getrlimit(RLIMIT_RTTIME, &limit);
    fprintf(stderr, "[audio] asking for a real-time thread %d: run-time limit %lld/%lld us, policy 0x%x\n", (int)tid,
            (long long)limit.rlim_cur, (long long)limit.rlim_max, sched_getscheduler(0));
  }
  int r = -1;
  for (unsigned priority = 20; priority >= 5 && r; priority /= 2) {
    r = rtkit_realtime(0, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Realtime", tid, priority);
    if (r) r = rtkit_realtime(1, "org.freedesktop.RealtimeKit1", "/org/freedesktop/RealtimeKit1", "org.freedesktop.RealtimeKit1", tid, priority);
  }
  return r;
}

static const dw_audio_backend k_backend = {"PipeWire", start, stop, inputs, capture_start, capture_stop, level_get, level_set, changes, realtime};

void rsl_audio_pipewire(void) { dw_audio_set_backend(&k_backend); }
