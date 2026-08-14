// SPDX-License-Identifier: GPL-3.0-or-later
#include "poly800_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 48000u

static double energy(const float* x, uint32_t n)
{
    double e = 0.0;
    for (uint32_t i = 0; i < n; ++i)
        e += (double)x[i] * (double)x[i];
    return e;
}

static double rms_from(const float* x, uint32_t from, uint32_t n)
{
    double e = 0.0;
    for (uint32_t i = from; i < n; ++i)
        e += (double)x[i] * (double)x[i];
    return sqrt(e / (double)(n - from));
}

static unsigned zero_crossings(const float* x, uint32_t n)
{
    unsigned z = 0;
    for (uint32_t i = 1; i < n; ++i)
        if ((x[i - 1] < 0.0f && x[i] >= 0.0f)
            || (x[i - 1] >= 0.0f && x[i] < 0.0f))
            ++z;
    return z;
}

static int finite_stereo(const float* l, const float* r, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (!isfinite(l[i]) || !isfinite(r[i]))
            return 0;
    return 1;
}

static void render_case_at(Poly800Params p, double rate,
    float* l, float* r, uint32_t frames)
{
    Poly800Core* core = poly800_core_create(rate);
    if (!core) {
        fprintf(stderr, "create failed at %.0f Hz\n", rate);
        exit(2);
    }
    poly800_core_set_params(core, &p);
    poly800_core_note_on(core, 60, 100);
    memset(l, 0, frames * sizeof(float));
    memset(r, 0, frames * sizeof(float));
    poly800_core_render(core, l, r, frames);
    poly800_core_destroy(core);
}

static void render_case(Poly800Params p, float* l, float* r)
{
    render_case_at(p, 48000.0, l, r, FRAMES);
}

int main(void)
{
    float* l0 = calloc(FRAMES, sizeof(float));
    float* r0 = calloc(FRAMES, sizeof(float));
    float* l1 = calloc(FRAMES, sizeof(float));
    float* r1 = calloc(FRAMES, sizeof(float));
    if (!l0 || !r0 || !l1 || !r1)
        return 2;

    Poly800Params p;
    poly800_params_default(&p);
    p.vcf_cutoff = 99.0f;
    p.vcf_resonance = 0.0f;
    p.vcf_env_intensity = 0.0f;
    p.mg_frequency = 8.0f;
    p.mg_delay = 0.0f;
    p.mg_vcf = 0.0f;

    p.mg_dco = 0.0f;
    p.chorus_on = 0;
    render_case(p, l0, r0);
    if (!finite_stereo(l0, r0, FRAMES)) {
        fprintf(stderr, "MG0 produced non-finite output\n");
        return 1;
    }
    for (uint32_t i = 0; i < FRAMES; ++i) {
        if (fabsf(l0[i] - r0[i]) > 1.0e-7f) {
            fprintf(stderr, "chorus-off output is not mono\n");
            return 1;
        }
    }

    p.mg_dco = 15.0f;
    render_case(p, l1, r1);
    if (!finite_stereo(l1, r1, FRAMES)) {
        fprintf(stderr, "MG15 produced non-finite output\n");
        return 1;
    }

    const unsigned z0 = zero_crossings(l0 + 12000, FRAMES - 12000);
    const unsigned z1 = zero_crossings(l1 + 12000, FRAMES - 12000);
    if (z1 <= z0 + z0 / 2) {
        fprintf(stderr, "P83 did not produce Bristol-scale frequency modulation: %u vs %u\n", z0, z1);
        return 1;
    }

    p.mg_dco = 0.0f;
    p.chorus_on = 1;
    render_case(p, l1, r1);
    if (!finite_stereo(l1, r1, FRAMES)) {
        fprintf(stderr, "chorus produced non-finite output\n");
        return 1;
    }

    double stereo_delta = 0.0;
    for (uint32_t i = 12000; i < FRAMES; ++i) {
        const double d = (double)l1[i] - (double)r1[i];
        stereo_delta += d * d;
    }
    if (stereo_delta <= 1.0e-8) {
        fprintf(stderr, "stock BBD chorus did not create stereo decorrelation\n");
        return 1;
    }

    /*
     * P48 must remain a subtle ambience, not become the obvious pitch/delay
     * sweep produced by Bristol's hidden Dimension controls. A simple stable
     * C4 should retain approximately the dry zero-crossing density and level.
     */
    const unsigned zc_chorus =
        zero_crossings(l1 + 12000, FRAMES - 12000);
    const unsigned zc_margin = z0 / 6; /* +/-16.7%; old Dimension path fails. */
    if (zc_chorus + zc_margin < z0 || zc_chorus > z0 + zc_margin) {
        fprintf(stderr,
                "P48 chorus is sweeping pitch too deeply: dry=%u chorus=%u\n",
                z0, zc_chorus);
        return 1;
    }

    const double dry_rms = rms_from(l0, 12000, FRAMES);
    const double chorus_rms = rms_from(l1, 12000, FRAMES);
    const double level_ratio = chorus_rms / dry_rms;
    if (level_ratio < 0.55 || level_ratio > 1.25) {
        fprintf(stderr,
                "P48 chorus changed level too aggressively: ratio=%.6f\n",
                level_ratio);
        return 1;
    }

    /* AudioLink production path: exercise M4 on Bristol's >=88k VCF branch. */
    render_case_at(p, 96000.0, l1, r1, FRAMES);
    if (!finite_stereo(l1, r1, FRAMES)) {
        fprintf(stderr, "96 kHz M4 chorus produced non-finite output\n");
        return 1;
    }

    printf("M4 calibration OK: zc MG0=%u MG15=%u chorus=%u level=%.6f stereo-delta=%.9g energy=%.9g; 96k finite\n",
           z0, z1, zc_chorus, level_ratio, stereo_delta, energy(l1, FRAMES));

    free(l0); free(r0); free(l1); free(r1);
    return 0;
}
