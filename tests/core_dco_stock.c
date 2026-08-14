// SPDX-License-Identifier: GPL-3.0-or-later
#include "poly800_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000
#define N (RATE * 2)

static double tone_amplitude(const float* x, int from, int to, double hz)
{
    double re = 0.0;
    double im = 0.0;
    const double step = 2.0 * 3.14159265358979323846 * hz / RATE;

    for (int i = from; i < to; ++i) {
        const double phase = step * (double)i;
        re += (double)x[i] * cos(phase);
        im -= (double)x[i] * sin(phase);
    }

    return 2.0 * sqrt(re * re + im * im) / (double)(to - from);
}

static int render_waveform(int waveform, float* out)
{
    float right[N];
    memset(out, 0, sizeof(float) * N);
    memset(right, 0, sizeof(right));

    Poly800Params p;
    poly800_params_default(&p);
    p.master_gain = 0.5f;
    p.dco_mode = 1;
    p.dco1_octave = 2;
    p.dco1_waveform = waveform;
    p.dco1_h16 = 1;
    p.dco1_h8 = 1;
    p.dco1_h4 = 1;
    p.dco1_h2 = 1;
    p.dco1_level = 2.0f;
    p.noise_level = 0.0f;
    p.chorus_on = 0;

    p.vcf_cutoff = 99.0f;
    p.vcf_resonance = 0.0f;
    p.vcf_keytrack = 0;
    p.vcf_env_intensity = 0.0f;
    p.mg_dco = 0.0f;
    p.mg_vcf = 0.0f;

    p.deg1_attack = 0.0f;
    p.deg1_decay = 0.0f;
    p.deg1_breakpoint = 31.0f;
    p.deg1_slope = 0.0f;
    p.deg1_sustain = 31.0f;
    p.deg3_attack = 0.0f;
    p.deg3_decay = 0.0f;
    p.deg3_breakpoint = 31.0f;
    p.deg3_slope = 0.0f;
    p.deg3_sustain = 31.0f;

    Poly800Core* core = poly800_core_create(RATE);
    if (!core)
        return 0;
    poly800_core_set_params(core, &p);
    poly800_core_note_on(core, 60, 127);
    poly800_core_render(core, out, right, N);
    poly800_core_destroy(core);
    return 1;
}

int main(void)
{
    static float equal[N];
    static float step[N];
    static const double freq[4] = {
        130.8127825, 261.625565, 523.251130, 1046.502260
    };
    static const double expected[4] = {1.0, 0.5, 0.25, 0.125};
    static const double lo[4] = {0.80, 0.35, 0.15, 0.05};
    static const double hi[4] = {1.20, 0.65, 0.35, 0.22};

    if (!render_waveform(1, equal) || !render_waveform(2, step))
        return 1;

    const int from = RATE / 2;
    const int to = N;

    for (int i = 0; i < 4; ++i) {
        const double a = tone_amplitude(equal, from, to, freq[i]);
        const double b = tone_amplitude(step, from, to, freq[i]);
        const double ratio = b / (a + 1.0e-30);
        printf("footage %d ratio %.4f expected %.3f\n",
               16 >> i, ratio, expected[i]);
        if (!isfinite(ratio) || ratio < lo[i] || ratio > hi[i]) {
            fprintf(stderr,
                "stock waveform-2 harmonic weighting is incorrect at index %d\n",
                i);
            return 2 + i;
        }
    }

    return 0;
}
