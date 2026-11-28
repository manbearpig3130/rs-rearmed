// The game's side of the audio, for the Mac app's `audio` override module on Linux (overrides/src/audio.h):
// audio_in.c (the game's queue of input buffers, without the stale ones: RS_AUDIO_INPUT_TRIM) and audio_ring.c
// (at LatencyBuffer 1 the engine keeps two refills and only the output's ring has one) are compiled as they are.
// What they ask of the module's Mac half is here: the output's buffer as it started, which the layer tells
// (linux/darwin/dw_audio.c), and the cap the Mac's audio_out.c has on that buffer, half the ring, so that a call
// never asks the ring for more than it can hold.
#include "audio.h"

extern uint32_t (*dw_audio_output_cap)(void);
extern void (*dw_audio_output_started)(uint32_t frames);
extern int (*dw_thread_special)(void *(*start)(void *));
extern void (*dw_thread_begins)(void *(*start)(void *));
int dw_audio_thread_realtime(void);

_Atomic uint32_t rs_audio_out_frames;

// The inputs' peaks and clips, which the Mac's audio_chan.c logs from here: the layer has no such count yet.
void rs_audio_chan_report(void) {}

static uint32_t cap(void) { return rs_audio_ring_refills() * AK_REFILL_FRAMES / 2; }

static void started(uint32_t frames) {
  atomic_store(&rs_audio_out_frames, frames);
  rs_audio_log("output: %u frames a call, the ring %u (LatencyBuffer %u)", frames, rs_audio_ring_refills() * AK_REFILL_FRAMES,
               rs_audio_ring_refills());
}

// The module's third function, the sound engine's thread (ak_lengine_thread), which the Mac runs real-time in the
// output device's workgroup: here the thread asks the audio backend for real-time scheduling as it starts, as that
// backend's own threads have it (RS_AUDIO_RT=0: as the original, an ordinary thread). The thread fills the output's
// ring; late, and the sound crackles.
static int engine_thread(void *(*start)(void *)) { return (uint64_t)(uintptr_t)start == RC_ADDR(ak_lengine_thread); }
static void engine_thread_begins(void *(*start)(void *)) {
  (void)start;
  if (!rs_audio_env_on("RS_AUDIO_RT", 1)) return;
  int r = dw_audio_thread_realtime();
  rs_audio_log("Wwise thread: %s", r == 0 ? "real-time" : "not made real-time (the audio backend could not)");
}

__attribute__((constructor)) static void audio_glue_init(void) {
  dw_audio_output_cap = cap;
  dw_audio_output_started = started;
  dw_thread_special = engine_thread;
  dw_thread_begins = engine_thread_begins;
}
