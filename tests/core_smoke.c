// SPDX-License-Identifier: GPL-3.0-or-later
#include "poly800_core.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
buffer_is_finite(const float* buffer, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (!isfinite(buffer[i])) {
            return 0;
        }
    }
    return 1;
}

static double
rms(const float* buffer, uint32_t count)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        sum += (double)buffer[i] * (double)buffer[i];
    }
    return sqrt(sum / (double)count);
}

int
main(void)
{
    enum { BLOCK = 4096 };
    float left[BLOCK];
    float right[BLOCK];

    Poly800Core* core = poly800_core_create(48000.0);
    if (!core) {
        fprintf(stderr, "failed to create core\n");
        return 1;
    }

    Poly800Params params;
    poly800_params_default(&params);
    poly800_core_set_params(core, &params);

    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    poly800_core_note_on(core, 60, 100);
    poly800_core_render(core, left, right, BLOCK);

    if (!buffer_is_finite(left, BLOCK) || !buffer_is_finite(right, BLOCK)) {
        fprintf(stderr, "non-finite output\n");
        poly800_core_destroy(core);
        return 2;
    }
    if (rms(left, BLOCK) < 0.001) {
        fprintf(stderr, "unexpected silence\n");
        poly800_core_destroy(core);
        return 3;
    }

    /* Exercise DOUBLE mode, maximum resonance/modulation and chorus. */
    poly800_core_all_notes_off(core);
    params.dco_mode = 2;
    params.vcf_resonance = 15.0f;
    params.vcf_cutoff = 99.0f;
    params.vcf_env_intensity = 15.0f;
    params.mg_vcf = 15.0f;
    params.mg_dco = 15.0f;
    params.chorus_on = 1;
    params.dco2_interval = 7;
    params.dco2_detune = 3.0f;
    poly800_core_set_params(core, &params);

    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    poly800_core_note_on(core, 48, 127);
    poly800_core_note_on(core, 55, 127);
    poly800_core_note_on(core, 60, 127);
    poly800_core_note_on(core, 64, 127);
    poly800_core_render(core, left, right, BLOCK);

    if (!buffer_is_finite(left, BLOCK) || !buffer_is_finite(right, BLOCK)) {
        fprintf(stderr, "non-finite stressed output\n");
        poly800_core_destroy(core);
        return 4;
    }

    poly800_core_destroy(core);
    return 0;
}
