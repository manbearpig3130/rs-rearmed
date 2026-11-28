// Accelerate, as much of it as the game uses: vDSP's complex FFT, which its note detection and tuner run on the
// guitar's signal, and two of vForce's four-at-once functions.
//
// vDSP_fft_zop is the out-of-place complex transform on split-complex data (the real parts in one array, the
// imaginary in another, each with a stride), of 2^log2n points, with vDSP's scaling: none in either direction, so
// forward then inverse gives the input times the number of points. A setup holds the sines and cosines for every
// size up to the one it was made for.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <xmmintrin.h>

#include "dw.h"

typedef struct {
  unsigned log2n;  // the largest size it serves
  float *cos, *sin;  // of 2*pi*k/N for k < N/2, N = 2^log2n
} fft_setup;

typedef struct {
  float *real, *imag;
} split_complex;

fft_setup *dw_vDSP_create_fftsetup(unsigned long log2n, int radix) {
  if (radix != 0 || log2n < 1 || log2n > 24) return NULL;  // kFFTRadix2
  fft_setup *s = malloc(sizeof *s);
  size_t half = (size_t)1 << (log2n - 1);
  if (!s) return NULL;
  s->log2n = (unsigned)log2n;
  s->cos = malloc(half * sizeof(float));
  s->sin = malloc(half * sizeof(float));
  if (!s->cos || !s->sin) {
    free(s->cos);
    free(s->sin);
    free(s);
    return NULL;
  }
  for (size_t k = 0; k < half; k++) {
    double a = M_PI * (double)k / (double)half;
    s->cos[k] = (float)cos(a);
    s->sin[k] = (float)sin(a);
  }
  return s;
}
DW_FUNCTION("_vDSP_create_fftsetup", dw_vDSP_create_fftsetup);

void dw_vDSP_destroy_fftsetup(fft_setup *s) {
  if (!s) return;
  free(s->cos);
  free(s->sin);
  free(s);
}
DW_FUNCTION("_vDSP_destroy_fftsetup", dw_vDSP_destroy_fftsetup);

// direction: 1 forward (e^-i), -1 inverse (e^+i).
void dw_vDSP_fft_zop(const fft_setup *s, const split_complex *in, long in_stride, const split_complex *out, long out_stride,
                     unsigned long log2n, int direction) {
  if (!s || log2n > s->log2n) return;
  size_t n = (size_t)1 << log2n;
  // the input into the output's places in bit-reversed order, then the passes there
  for (size_t i = 0; i < n; i++) {
    size_t r = 0;
    for (unsigned b = 0; b < log2n; b++) r |= ((i >> b) & 1) << (log2n - 1 - b);
    out->real[(long)r * out_stride] = in->real[(long)i * in_stride];
    out->imag[(long)r * out_stride] = in->imag[(long)i * in_stride];
  }
  size_t table_step = ((size_t)1 << s->log2n) / 2;  // the table's index for one step of a two-point pass
  for (size_t len = 2; len <= n; len <<= 1, table_step >>= 1) {
    size_t half = len / 2;
    for (size_t start = 0; start < n; start += len)
      for (size_t k = 0; k < half; k++) {
        float wr = s->cos[k * table_step], wi = direction >= 0 ? -s->sin[k * table_step] : s->sin[k * table_step];
        float *ar = &out->real[(long)(start + k) * out_stride], *ai = &out->imag[(long)(start + k) * out_stride];
        float *br = &out->real[(long)(start + k + half) * out_stride], *bi = &out->imag[(long)(start + k + half) * out_stride];
        float tr = *br * wr - *bi * wi, ti = *br * wi + *bi * wr;
        *br = *ar - tr, *bi = *ai - ti;
        *ar += tr, *ai += ti;
      }
  }
}
DW_FUNCTION("_vDSP_fft_zop", dw_vDSP_fft_zop);

// vForce's vcosf and vpowf: four floats at once.
__m128 dw_vcosf(__m128 x) {
  float v[4];
  _mm_storeu_ps(v, x);
  for (int i = 0; i < 4; i++) v[i] = cosf(v[i]);
  return _mm_loadu_ps(v);
}
DW_FUNCTION("_vcosf", dw_vcosf);

__m128 dw_vpowf(__m128 x, __m128 y) {
  float a[4], b[4];
  _mm_storeu_ps(a, x);
  _mm_storeu_ps(b, y);
  for (int i = 0; i < 4; i++) a[i] = powf(a[i], b[i]);
  return _mm_loadu_ps(a);
}
DW_FUNCTION("_vpowf", dw_vpowf);
