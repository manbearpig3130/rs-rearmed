// The game's sound to PipeWire: the backend of the Darwin layer's audio graph (linux/darwin/dw_audio.c).
//
// One playback stream, stereo 32-bit float at the graph's rate, asking for the graph's buffer (256 frames at
// 48,000 Hz: 5.3 ms) as its latency; PipeWire gives what the rest of its graph allows. Its process callback runs on
// PipeWire's real-time thread and wants a count of frames of its own choosing, while the game is asked in buffers
// of its own size, as CoreAudio asks it: the frames between the two wait in a small queue.
//
// The inputs are PipeWire's capture devices: listed for the game by their cards' names, and opened as capture
// streams that feed the game's input procedure.
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (in->channels > 8) in->channels = 8;
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
  float queue[8192 * 8];
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
                                                  PW_KEY_STREAM_DONT_REMIX, "true", NULL);
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

static const dw_audio_backend k_backend = {"PipeWire", start, stop, inputs, capture_start, capture_stop, level_get, level_set};

void rsl_audio_pipewire(void) { dw_audio_set_backend(&k_backend); }
