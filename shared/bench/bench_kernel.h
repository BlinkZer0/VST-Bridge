/*
 * VST gHost: fixed DSP benchmark kernel.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The same source is compiled
 *   - for x86_64 Windows (payload/tests/hello.c, run under Wine + Box64), and
 *   - for arm64 Android (app native lib),
 * so the ratio of the two run times is a first estimate of Box64 overhead on
 * audio-style code. The arithmetic is plain IEEE float/double with no libm
 * and no FMA contraction (build with -ffp-contract=off), so both builds
 * should produce the *same checksum*. A mismatch would mean Box64 is
 * computing floats differently, which is worth knowing early.
 */
#ifndef VSTGHOST_BENCH_KERNEL_H
#define VSTGHOST_BENCH_KERNEL_H

#include <stdint.h>

#define VG_BENCH_VOICES 32
#define VG_BENCH_RATE 48000
#define VG_BENCH_SECONDS 60

typedef struct {
    float phase;
    float inc;
    float z1;
    float coeff;
} vg_bench_voice;

/* Renders VG_BENCH_SECONDS of a 32-voice naive saw + one-pole lowpass synth
 * and returns a checksum of every output sample. */
static double vg_bench_run(void)
{
    vg_bench_voice v[VG_BENCH_VOICES];
    uint32_t seed = 12345u;
    for (int i = 0; i < VG_BENCH_VOICES; ++i) {
        seed = seed * 1664525u + 1013904223u;
        float detune = (float)(seed >> 8) / 16777216.0f; /* [0,1) */
        v[i].phase = 0.0f;
        v[i].inc = (55.0f * (1.0f + (float)i * 0.37f) + detune) / (float)VG_BENCH_RATE;
        v[i].z1 = 0.0f;
        v[i].coeff = 0.02f + 0.9f * ((float)i / (float)VG_BENCH_VOICES);
    }

    const int frames = VG_BENCH_RATE * VG_BENCH_SECONDS;
    double checksum = 0.0;
    for (int n = 0; n < frames; ++n) {
        float l = 1e-20f, r = 1e-20f; /* keep the filters out of denormal range */
        for (int i = 0; i < VG_BENCH_VOICES; ++i) {
            vg_bench_voice *p = &v[i];
            p->phase += p->inc;
            if (p->phase >= 1.0f) p->phase -= 1.0f;
            float s = 2.0f * p->phase - 1.0f;
            p->z1 += p->coeff * (s - p->z1);
            if (i & 1) r += p->z1; else l += p->z1;
        }
        checksum += (double)l * 0.5 + (double)r * 0.25;
    }
    return checksum;
}

#endif
