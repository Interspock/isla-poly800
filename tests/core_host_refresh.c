// SPDX-License-Identifier: GPL-3.0-or-later
#include "poly800_core.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000
#define N 48000

static double crossing_frequency(const float* x, int from, int to)
{
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
            ++crossings;
    return (double)crossings * RATE / (double)(to - from);
}

int main(void)
{
    float left[N];
    float right[N];
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));

    Poly800Params p;
    poly800_params_default(&p);
    p.master_gain = 0.1f;
    p.dco_mode = 2;
    p.dco1_level = 0.0f;
    p.dco2_level = 31.0f;
    p.dco2_octave = 2;
    p.dco2_interval = 0;
    p.dco2_detune = 2.0f;
    p.dco2_h16 = 0;
    p.dco2_h8 = 1;
    p.dco2_h4 = 0;
    p.dco2_h2 = 0;
    p.dco2_waveform = 1;
    p.noise_level = 0.0f;
    p.vcf_cutoff = 99.0f;
    p.vcf_resonance = 0.0f;
    p.vcf_keytrack = 0;
    p.vcf_env_intensity = 0.0f;
    p.mg_dco = 0.0f;
    p.mg_vcf = 0.0f;
    p.chorus_on = 0;
    p.deg2_attack = 0.0f;
    p.deg2_decay = 0.0f;
    p.deg2_breakpoint = 31.0f;
    p.deg2_slope = 0.0f;
    p.deg2_sustain = 31.0f;
    p.deg3_attack = 0.0f;
    p.deg3_decay = 0.0f;
    p.deg3_breakpoint = 31.0f;
    p.deg3_slope = 0.0f;
    p.deg3_sustain = 31.0f;

    Poly800Core* core = poly800_core_create(RATE);
    if (!core)
        return 1;

    /*
     * Ardour/LV2-style stress: the host may present unchanged control values
     * every process block. Re-applying identical params must be idempotent.
     */
    for (int i = 0; i < 4096; ++i)
        poly800_core_set_params(core, &p);

    poly800_core_note_on(core, 60, 127);
    poly800_core_render(core, left, right, N);

    const double f = crossing_frequency(left, RATE / 2, RATE);
    const double expected = 261.625565 * exp2((-20.0 * 2.0 / 3.0) / 1200.0);
    printf("repeated host refresh DCO2 frequency %.3f Hz expected %.3f Hz\n",
           f, expected);

    poly800_core_destroy(core);

    if (!isfinite(f) || f < expected - 2.0 || f > expected + 2.0) {
        fprintf(stderr,
            "unchanged host control refresh corrupted DCO2 pitch/state\n");
        return 2;
    }

    return 0;
}
