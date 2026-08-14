// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Deterministic M4 A/B probe.
 *
 * This does not compare to a copyrighted audio asset. It renders a fixed
 * internal program and prints stable behavioural metrics that can be captured
 * from ISLA, Bristol builds, or future reference implementations.
 */
#include "poly800_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000u
#define N (SR * 2u)

static double rms(const float* x, uint32_t from, uint32_t n)
{
    double s = 0.0;
    for (uint32_t i = from; i < n; ++i)
        s += (double)x[i] * (double)x[i];
    return sqrt(s / (double)(n - from));
}

static double stereo_rms(const float* l, const float* r, uint32_t from, uint32_t n)
{
    double s = 0.0;
    for (uint32_t i = from; i < n; ++i) {
        const double d = (double)l[i] - (double)r[i];
        s += d * d;
    }
    return sqrt(s / (double)(n - from));
}

static unsigned zc(const float* x, uint32_t from, uint32_t n)
{
    unsigned count = 0;
    for (uint32_t i = from + 1u; i < n; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++count;
    return count;
}

static void probe(const char* name, Poly800Params p, float* l, float* r)
{
    Poly800Core* core = poly800_core_create((double)SR);
    if (!core) exit(2);
    poly800_core_set_params(core, &p);
    poly800_core_note_on(core, 60, 100);
    memset(l, 0, N * sizeof(float));
    memset(r, 0, N * sizeof(float));
    poly800_core_render(core, l, r, N);
    printf("%-18s rms=%.9f zc=%u stereo=%.9f\n",
           name, rms(l, SR / 2u, N), zc(l, SR / 2u, N),
           stereo_rms(l, r, SR / 2u, N));
    poly800_core_destroy(core);
}

int main(void)
{
    float* l = calloc(N, sizeof(float));
    float* r = calloc(N, sizeof(float));
    if (!l || !r) return 2;

    Poly800Params p;
    poly800_params_default(&p);
    p.vcf_cutoff = 99.0f;
    p.vcf_resonance = 0.0f;
    p.vcf_env_intensity = 0.0f;
    p.mg_frequency = 8.0f;
    p.mg_delay = 0.0f;
    p.mg_vcf = 0.0f;

    p.mg_dco = 0.0f; p.chorus_on = 0;
    probe("dry-mg0", p, l, r);
    p.mg_dco = 7.0f;
    probe("dry-mg7", p, l, r);
    p.mg_dco = 15.0f;
    probe("dry-mg15", p, l, r);
    p.mg_dco = 0.0f; p.chorus_on = 1;
    probe("dimension-chorus", p, l, r);

    free(l); free(r);
    return 0;
}
