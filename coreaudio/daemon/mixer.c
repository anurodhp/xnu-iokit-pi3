#include "mixer.h"
#include <stdlib.h>
#include <string.h>

int ca_ring_init(ca_stream_ring *r, uint32_t cap_frames, uint32_t ch, uint32_t rate)
{
    memset(r, 0, sizeof(*r));
    if (cap_frames == 0 || (ch != 1 && ch != 2) || rate == 0) return -1;
    r->buf = calloc((size_t)cap_frames * ch, sizeof(float));
    if (!r->buf) return -1;
    r->cap = cap_frames; r->ch = ch; r->rate = rate;
    return 0;
}

void ca_ring_free(ca_stream_ring *r) { free(r->buf); memset(r, 0, sizeof(*r)); }

uint32_t ca_ring_avail(const ca_stream_ring *r) { return (uint32_t)(r->wpos - r->rpos); }
uint32_t ca_ring_room(const ca_stream_ring *r)  { return r->cap - ca_ring_avail(r); }

uint32_t ca_ring_write(ca_stream_ring *r, const float *src, uint32_t frames)
{
    uint32_t room = ca_ring_room(r);
    if (frames > room) frames = room;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t at = (uint32_t)((r->wpos + i) % r->cap);
        for (uint32_t c = 0; c < r->ch; c++) r->buf[(size_t)at * r->ch + c] = src[(size_t)i * r->ch + c];
    }
    r->wpos += frames;
    return frames;
}

static void frame_at(const ca_stream_ring *r, uint64_t pos, float *l, float *rr)
{
    const float *p = &r->buf[(size_t)(pos % r->cap) * r->ch];
    *l = p[0];
    *rr = r->ch == 2 ? p[1] : p[0];
}

uint32_t ca_ring_mix_into(ca_stream_ring *r, float *mix, uint32_t out_frames,
                          uint32_t out_rate, float gain)
{
    const double step = (double)r->rate / (double)out_rate;
    double pos = r->frac;
    uint32_t supplied = 0;
    for (uint32_t i = 0; i < out_frames; i++) {
        uint64_t idx = (uint64_t)pos;
        double f = pos - (double)idx;
        uint64_t avail = ca_ring_avail(r);
        /* Interpolation needs frame idx+1 unless we sit exactly on frame idx. */
        if (f == 0.0 ? avail < idx + 1 : avail < idx + 2) {
            r->underrun_frames += out_frames - i;
            break;
        }
        float l0, r0, l1 = 0, r1 = 0;
        frame_at(r, r->rpos + idx, &l0, &r0);
        if (f != 0.0) frame_at(r, r->rpos + idx + 1, &l1, &r1);
        float l = f == 0.0 ? l0 : (float)(l0 + (l1 - l0) * f);
        float rr = f == 0.0 ? r0 : (float)(r0 + (r1 - r0) * f);
        mix[(size_t)i * 2]     += l * gain;
        mix[(size_t)i * 2 + 1] += rr * gain;
        pos += step;
        supplied++;
    }
    uint64_t used = (uint64_t)pos;
    if (used > ca_ring_avail(r)) used = ca_ring_avail(r);
    r->rpos += used;
    r->frac = pos - (double)(uint64_t)pos;
    return supplied;
}

uint32_t ca_ring_mix_faded(ca_stream_ring *r, float *mix, uint32_t out_frames,
                           uint32_t out_rate, float gain, ca_fade *fd)
{
    static float tmp[4096 * 2];     /* audiod's mixer thread is the only caller */
    if (out_frames > 4096) out_frames = 4096;
    memset(tmp, 0, (size_t)out_frames * 2 * sizeof(float));
    uint32_t n = ca_ring_mix_into(r, tmp, out_frames, out_rate, 1.0f);
    for (uint32_t i = 0; i < out_frames; i++) {
        float l, rr;
        if (i < n) {                /* data: envelope rises to full */
            l = tmp[(size_t)i * 2];
            rr = tmp[(size_t)i * 2 + 1];
            fd->last_l = l;
            fd->last_r = rr;
            if (fd->level < CA_FADE_FRAMES) fd->level++;
        } else {                    /* none: hold the last sample and decay */
            if (fd->level == 0) break;
            l = fd->last_l;
            rr = fd->last_r;
            fd->level--;
        }
        const float env = (float)fd->level / (float)CA_FADE_FRAMES;
        mix[(size_t)i * 2]     += l * env * gain;
        mix[(size_t)i * 2 + 1] += rr * env * gain;
    }
    return n;
}

void ca_clip(float *mix, uint32_t samples, float volume, int mute)
{
    for (uint32_t i = 0; i < samples; i++) {
        float v = mix[i] * (mute ? 0.0f : volume);
        if (v != v) v = 0.0f;
        mix[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }
}
