// An input's own level control, through ALSA's mixer: what CoreAudio's volume scalar is on the Mac. The audio
// layer uses it for the Real Tone Cable, whose level the game sets itself (linux/darwin/dw_audio.c).
//
// The control is the card's capture volume (the cable's is "Mic", 38 steps over 18 dB), and a level from 0 to 1
// is its place in the control's range of steps, as CoreAudio maps a USB device's. PipeWire, which has the device
// open, follows the change as it follows any mixer program's.
#include <alsa/asoundlib.h>
#include <math.h>
#include <stdio.h>

#include "rsl.h"

static snd_mixer_t *open_card(int card, snd_mixer_elem_t **capture) {
  snd_mixer_t *mixer = NULL;
  char name[24];
  snprintf(name, sizeof name, "hw:%d", card);
  if (card < 0 || snd_mixer_open(&mixer, 0) < 0) return NULL;
  if (snd_mixer_attach(mixer, name) < 0 || snd_mixer_selem_register(mixer, NULL, NULL) < 0 || snd_mixer_load(mixer) < 0) {
    snd_mixer_close(mixer);
    return NULL;
  }
  *capture = NULL;
  for (snd_mixer_elem_t *e = snd_mixer_first_elem(mixer); e && !*capture; e = snd_mixer_elem_next(e))
    if (snd_mixer_selem_is_active(e) && snd_mixer_selem_has_capture_volume(e)) *capture = e;
  if (!*capture) {
    snd_mixer_close(mixer);
    return NULL;
  }
  return mixer;
}

int rsl_alsa_level_get(int card, float *level) {
  snd_mixer_elem_t *e;
  snd_mixer_t *mixer = open_card(card, &e);
  if (!mixer) return 0;
  long lo = 0, hi = 0, v = 0;
  int ok = snd_mixer_selem_get_capture_volume_range(e, &lo, &hi) >= 0 && hi > lo &&
           snd_mixer_selem_get_capture_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &v) >= 0;
  if (ok) *level = (float)(v - lo) / (float)(hi - lo);
  snd_mixer_close(mixer);
  return ok;
}

int rsl_alsa_level_set(int card, float level) {
  snd_mixer_elem_t *e;
  snd_mixer_t *mixer = open_card(card, &e);
  if (!mixer) return 0;
  long lo = 0, hi = 0;
  int ok = snd_mixer_selem_get_capture_volume_range(e, &lo, &hi) >= 0 && hi > lo;
  if (ok) {
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    ok = snd_mixer_selem_set_capture_volume_all(e, lo + lroundf(level * (float)(hi - lo))) >= 0;
    if (snd_mixer_selem_has_capture_switch(e)) snd_mixer_selem_set_capture_switch_all(e, 1);
  }
  snd_mixer_close(mixer);
  return ok;
}
