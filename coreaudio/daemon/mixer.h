/*
 * mixer.h: the pure audio math of audiod (ring buffer, linear resampler, mix,
 * clip). No I/O and no system headers beyond the C library, so
 * test/mixer_test.c can run it on the host. See PLAN.md.
 * Not thread-safe: audiod serializes access with its own mutex.
 */
#ifndef CA_MIXER_H
#define CA_MIXER_H
#include <stdint.h>

typedef struct {
    float    *buf;       /* cap * ch floats, interleaved */
    uint32_t  cap;       /* frames */
    uint32_t  ch;        /* 1 or 2 */
    uint64_t  rpos;      /* frames consumed so far (monotonic) */
    uint64_t  wpos;      /* frames produced so far (monotonic) */
    uint32_t  rate;      /* source sample rate */
    double    frac;      /* fractional source position relative to rpos, [0,1) after a pull */
    uint64_t  underrun_frames;
} ca_stream_ring;

int      ca_ring_init(ca_stream_ring *r, uint32_t cap_frames, uint32_t ch, uint32_t rate);
void     ca_ring_free(ca_stream_ring *r);
uint32_t ca_ring_avail(const ca_stream_ring *r);   /* frames readable */
uint32_t ca_ring_room(const ca_stream_ring *r);    /* frames writable */
/* Copies up to `frames` frames in; returns how many were accepted. */
uint32_t ca_ring_write(ca_stream_ring *r, const float *src, uint32_t frames);

/*
 * Pulls `out_frames` stereo frames at `out_rate` from the ring (linear
 * interpolation, source position carried in r->frac across calls) and ADDS them,
 * times `gain`, into `mix` (out_frames * 2 floats). Frames the ring cannot
 * supply are silence and are counted in r->underrun_frames. Returns the number
 * of output frames actually supplied from ring data.
 */
uint32_t ca_ring_mix_into(ca_stream_ring *r, float *mix, uint32_t out_frames,
                          uint32_t out_rate, float gain);

/* mix *= volume (0 if mute), then clamp to [-1, 1]; NaN becomes 0. */
void ca_clip(float *mix, uint32_t samples, float volume, int mute);
#endif
