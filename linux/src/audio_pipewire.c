// The game's sound to PipeWire: the backend of the Darwin layer's audio graph (linux/darwin/dw_audio.c).
//
// One playback stream, stereo 32-bit float at the graph's rate, asking for the graph's buffer (256 frames at
// 48,000 Hz: 5.3 ms) as its latency; PipeWire gives what the rest of its graph allows. Its process callback runs on
// PipeWire's real-time thread and wants a count of frames of its own choosing, while the game is asked in buffers
// of its own size, as CoreAudio asks it: the frames between the two wait in a small queue.
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

static const dw_audio_backend k_backend = {"PipeWire", start, stop};

void rsl_audio_pipewire(void) { dw_audio_set_backend(&k_backend); }
