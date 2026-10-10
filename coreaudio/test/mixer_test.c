/* Host test for daemon/mixer.c: cc -I daemon -o /tmp/mixer_test test/mixer_test.c daemon/mixer.c -lm */
#include "mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    ca_stream_ring r;
    /* 1: same rate passes samples through unchanged, ring wraps */
    CHECK(ca_ring_init(&r, 8, 2, 48000) == 0);
    float in[24], out[16];
    for (int i = 0; i < 12; i++) { in[i * 2] = i * 0.01f; in[i * 2 + 1] = -i * 0.01f; }
    CHECK(ca_ring_write(&r, in, 12) == 8);          /* only 8 fit */
    CHECK(ca_ring_room(&r) == 0);
    memset(out, 0, sizeof out);
    CHECK(ca_ring_mix_into(&r, out, 6, 48000, 1.0f) == 6);
    CHECK(fabsf(out[2 * 5] - 0.05f) < 1e-6f && fabsf(out[2 * 5 + 1] + 0.05f) < 1e-6f);
    CHECK(ca_ring_avail(&r) == 2);
    CHECK(ca_ring_write(&r, in + 16, 4) == 4);      /* wraps around the end */
    memset(out, 0, sizeof out);
    CHECK(ca_ring_mix_into(&r, out, 6, 48000, 1.0f) == 6);
    CHECK(fabsf(out[0] - 0.06f) < 1e-6f && fabsf(out[2 * 2] - 0.08f) < 1e-6f && fabsf(out[2 * 5] - 0.11f) < 1e-6f);
    /* 2: underrun yields silence and is counted */
    memset(out, 0, sizeof out);
    CHECK(ca_ring_mix_into(&r, out, 4, 48000, 1.0f) == 0);
    CHECK(r.underrun_frames == 4 && out[0] == 0.0f);
    ca_ring_free(&r);

    /* 3: 24000 -> 48000 doubles: linear interpolation midpoints */
    CHECK(ca_ring_init(&r, 16, 1, 24000) == 0);
    float m[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    CHECK(ca_ring_write(&r, m, 4) == 4);
    float o[16] = { 0 };
    uint32_t got = ca_ring_mix_into(&r, o, 6, 48000, 1.0f);
    CHECK(got == 6);
    CHECK(fabsf(o[0] - 0.0f) < 1e-6f && fabsf(o[2] - 0.5f) < 1e-6f && fabsf(o[4] - 1.0f) < 1e-6f && fabsf(o[6] - 0.5f) < 1e-6f);
    CHECK(ca_ring_avail(&r) == 1);                  /* consumed 3 of 4 */
    ca_ring_free(&r);

    /* 4: 44100 -> 48000 over many chunks consumes the right number of source frames */
    CHECK(ca_ring_init(&r, 4096, 2, 44100) == 0);
    static float sine[4000 * 2];
    for (int i = 0; i < 4000; i++) sine[i * 2] = sine[i * 2 + 1] = sinf(i * 0.05f);
    CHECK(ca_ring_write(&r, sine, 4000) == 4000);
    static float mix[512 * 2];
    uint32_t total = 0;
    for (int k = 0; k < 7; k++) { memset(mix, 0, sizeof mix); total += ca_ring_mix_into(&r, mix, 512, 48000, 1.0f); }
    CHECK(total == 7 * 512);
    CHECK(llabs((long long)r.rpos - (long long)(7 * 512 * 44100.0 / 48000.0)) <= 1);
    ca_ring_free(&r);

    /* 5: mono duplicates, gain, mix sums, clip + volume + mute + NaN */
    CHECK(ca_ring_init(&r, 4, 1, 48000) == 0);
    float one[2] = { 0.5f, 0.5f };
    ca_ring_write(&r, one, 2);
    float mx[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
    ca_ring_mix_into(&r, mx, 2, 48000, 0.5f);
    CHECK(fabsf(mx[0] - 0.5f) < 1e-6f && fabsf(mx[1] - 0.5f) < 1e-6f);
    ca_ring_free(&r);
    float c[5] = { 2.0f, -3.0f, 0.5f, NAN, 0.8f };
    ca_clip(c, 5, 1.0f, 0);
    CHECK(c[0] == 1.0f && c[1] == -1.0f && c[2] == 0.5f && c[3] == 0.0f);
    ca_clip(c, 5, 0.5f, 0);
    CHECK(c[0] == 0.5f && c[2] == 0.25f);
    ca_clip(c, 5, 1.0f, 1);
    CHECK(c[0] == 0.0f && c[4] == 0.0f);
    CHECK(ca_ring_init(&r, 0, 2, 48000) != 0 && ca_ring_init(&r, 4, 3, 48000) != 0);

    printf(fails ? "mixer_test: %d FAILED\n" : "mixer_test: PASS\n", fails);
    return fails != 0;
}
