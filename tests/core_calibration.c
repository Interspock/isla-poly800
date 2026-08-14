// SPDX-License-Identifier: GPL-3.0-or-later
#include "poly800_core.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000
#define N 48000

static double rms(const float* x, int from, int to)
{
    double sum = 0.0;
    int count = 0;
    for (int i = from; i < to; ++i) {
        sum += (double)x[i] * (double)x[i];
        ++count;
    }
    return sqrt(sum / (double)count);
}

static int finite_buffer(const float* x, int count)
{
    for (int i = 0; i < count; ++i)
        if (!isfinite(x[i]))
            return 0;
    return 1;
}

/*
 * Estimate frequency from the average spacing of positive zero crossings.
 * Using first/last crossing is much more precise than integer crossings/sec
 * for the small 0..20-cent Poly-800 detune range.
 */
static double crossing_frequency(const float* x, int from, int to)
{
    int first = -1;
    int last = -1;
    int crossings = 0;

    for (int i = from + 1; i < to; ++i) {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    }

    if (crossings < 2 || last <= first)
        return 0.0;

    return (double)(crossings - 1) * RATE / (double)(last - first);
}

static int render(const Poly800Params* params, float* out)
{
    float right[N];
    memset(out, 0, sizeof(float) * N);
    memset(right, 0, sizeof(right));

    Poly800Core* core = poly800_core_create(RATE);
    if (!core)
        return 0;

    poly800_core_set_params(core, params);
    poly800_core_note_on(core, 60, 100);
    poly800_core_render(core, out, right, N);
    poly800_core_destroy(core);
    return 1;
}

int main(void)
{
    static float a[N];
    static float b[N];
    Poly800Params params;

    poly800_params_default(&params);
    params.vcf_cutoff = 99;
    params.vcf_resonance = 0;
    params.vcf_keytrack = 0;
    params.vcf_env_intensity = 0;
    params.mg_dco = 0;
    params.mg_vcf = 0;
    params.noise_level = 0;
    params.deg1_attack = 0;
    params.deg1_decay = 0;
    params.deg1_breakpoint = 31;
    params.deg1_slope = 0;
    params.deg1_sustain = 31;
    params.deg3_attack = 0;
    params.deg3_decay = 0;
    params.deg3_breakpoint = 31;
    params.deg3_slope = 0;
    params.deg3_sustain = 31;
    params.master_gain = 0.1f;

    /* Enabled footages are additive; they are never count-normalised. */
    params.dco1_h16 = 0;
    params.dco1_h8 = 1;
    params.dco1_h4 = 0;
    params.dco1_h2 = 0;
    params.dco1_waveform = 1;
    render(&params, a);
    params.dco1_h4 = 1;
    render(&params, b);
    const double one = rms(a, RATE / 4, RATE);
    const double two = rms(b, RATE / 4, RATE);
    printf("footage rms one %.6f two %.6f ratio %.3f\n",
           one, two, two / one);
    if (two < one * 1.03) {
        fprintf(stderr, "footage bus appears normalised\n");
        return 4;
    }

    /*
     * Isolate DCO2. Korg specifies P32 as 0..3 with -20 cents maximum;
     * detune is downward because the hardware thins clock pulses.
     */
    poly800_params_default(&params);
    params.master_gain = 0.1f;
    params.dco_mode = 2;
    params.dco1_level = 0;
    params.dco2_level = 31;
    params.dco2_h16 = 0;
    params.dco2_h8 = 1;
    params.dco2_h4 = 0;
    params.dco2_h2 = 0;
    params.dco2_waveform = 1;
    params.dco2_interval = 0;
    params.vcf_cutoff = 99;
    params.vcf_resonance = 0;
    params.vcf_keytrack = 0;
    params.vcf_env_intensity = 0;
    params.mg_dco = 0;
    params.mg_vcf = 0;
    params.noise_level = 0;
    params.deg2_attack = 0;
    params.deg2_decay = 0;
    params.deg2_breakpoint = 31;
    params.deg2_slope = 0;
    params.deg2_sustain = 31;
    params.deg3_attack = 0;
    params.deg3_decay = 0;
    params.deg3_breakpoint = 31;
    params.deg3_slope = 0;
    params.deg3_sustain = 31;

    params.dco2_detune = 0;
    render(&params, a);
    params.dco2_detune = 3;
    render(&params, b);
    const double f0 = crossing_frequency(a, RATE / 2, RATE);
    const double f3 = crossing_frequency(b, RATE / 2, RATE);
    const double ratio3 = f3 / f0;
    const double expected3 = exp2(-20.0 / 1200.0);
    printf("detune P32=3 %.3f -> %.3f Hz ratio %.6f expected %.6f\n",
           f0, f3, ratio3, expected3);
    if (!(f3 < f0) || fabs(ratio3 - expected3) > 0.003) {
        fprintf(stderr, "P32 detune endpoint is not about -20 cents\n");
        return 5;
    }

    /* Factory 84 uses P32=2: expect about -13.33 cents, not +66 cents. */
    params.dco2_detune = 2;
    render(&params, b);
    const double f2 = crossing_frequency(b, RATE / 2, RATE);
    const double ratio2 = f2 / f0;
    const double expected2 = exp2((-20.0 * 2.0 / 3.0) / 1200.0);
    printf("detune P32=2 %.3f -> %.3f Hz ratio %.6f expected %.6f\n",
           f0, f2, ratio2, expected2);
    if (!(f2 < f0) || fabs(ratio2 - expected2) > 0.003) {
        fprintf(stderr, "P32=2 is not about -13.33 cents\n");
        return 6;
    }

    /* AudioLink path: exercise Bristol's >=88 kHz filter branch at 96 kHz. */
    enum { HIGH_N = 8192 };
    float high_l[HIGH_N];
    float high_r[HIGH_N];
    memset(high_l, 0, sizeof(high_l));
    memset(high_r, 0, sizeof(high_r));

    Poly800Core* high = poly800_core_create(96000.0);
    if (!high)
        return 7;
    poly800_params_default(&params);
    params.dco_mode = 2;
    params.dco2_h8 = 1;
    params.dco2_detune = 3;
    params.vcf_cutoff = 99;
    params.vcf_resonance = 15;
    params.vcf_env_intensity = 15;
    params.mg_vcf = 15;
    params.mg_dco = 15;
    params.chorus_on = 1;
    poly800_core_set_params(high, &params);
    poly800_core_note_on(high, 48, 127);
    poly800_core_note_on(high, 55, 127);
    poly800_core_note_on(high, 60, 127);
    poly800_core_note_on(high, 64, 127);
    poly800_core_render(high, high_l, high_r, HIGH_N);

    if (!finite_buffer(high_l, HIGH_N) || !finite_buffer(high_r, HIGH_N)) {
        fprintf(stderr, "non-finite 96 kHz output\n");
        poly800_core_destroy(high);
        return 8;
    }
    printf("96 kHz filter branch finite\n");
    poly800_core_destroy(high);

    return 0;
}
