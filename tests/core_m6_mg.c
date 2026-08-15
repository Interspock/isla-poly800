// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * White-box M6 test: include the implementation so the recovered ROM control
 * helpers can be checked directly without inferring them through filter audio.
 */
#include "../src/poly800_core_m6.c"

#include <math.h>
#include <stdio.h>

static int nearf(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

int main(void)
{
    static const uint8_t expected_freq[16] = {
        1, 2, 3, 4, 6, 8, 11, 14,
        17, 21, 25, 30, 36, 43, 52, 62
    };

    for (int i = 0; i < 16; ++i) {
        if (m6_mg_increment((float)i) != expected_freq[i]) {
            fprintf(stderr, "P81 ROM table mismatch at %d\n", i);
            return 1;
        }
    }

    if (m6_mg_delay_ticks(0.0f) != 0
        || m6_mg_delay_ticks(8.0f) != 128
        || m6_mg_delay_ticks(15.0f) != 240) {
        fprintf(stderr, "P82 ROM delay mapping mismatch\n");
        return 2;
    }

    /* Depth 15 and folded-ramp 63 is the firmware's full-scale 116 code. */
    if (m6_mul4(0xf0u, 63u) != 116u
        || m6_mul4(0x00u, 63u) != 0u) {
        fprintf(stderr, "ROM 4-bit MG multiplier mismatch\n");
        return 3;
    }

    Poly800Core* core = poly800_core_create(48000.0);
    if (!core)
        return 4;

    Poly800Params p;
    poly800_params_default(&p);
    p.mg_frequency = 0.0f; /* increment 1 makes one exact 512-tick cycle */
    p.mg_delay = 0.0f;
    p.mg_dco = 15.0f;
    p.mg_vcf = 15.0f;
    poly800_core_set_params(core, &p);
    m6_mg_reset_state(core);

    float lo = 1.0f;
    float hi = -1.0f;
    for (unsigned i = 0; i < 512; ++i) {
        m6_mg_control_tick(core);
        const float value = m6_state(core)[MG_STATE_DCO_NORM];
        if (value < lo) lo = value;
        if (value > hi) hi = value;
    }

    if (hi < 0.99f || lo > -0.99f) {
        fprintf(stderr, "MG is not bipolar full-scale: min=%f max=%f\n", lo, hi);
        poly800_core_destroy(core);
        return 5;
    }

    /* A complete bipolar cycle is 512 accumulator counts, not 256. */
    const double low_hz = P800_M6_MG_CONTROL_HZ / 512.0;
    const double high_hz = 62.0 * P800_M6_MG_CONTROL_HZ / 512.0;
    if (fabs(low_hz - 0.3662109375) > 1e-9
        || fabs(high_hz - 22.705078125) > 1e-9) {
        fprintf(stderr, "nominal MG frequency endpoints mismatch\n");
        poly800_core_destroy(core);
        return 6;
    }

    /* Delay freezes the firmware phase counter for the exact ROM tick count. */
    p.mg_delay = 15.0f;
    poly800_core_set_params(core, &p);
    m6_mg_reset_state(core);
    m6_mg_arm_note_delay(core);
    for (unsigned i = 0; i < 240; ++i)
        m6_mg_control_tick(core);

    if ((uint8_t)m6_state(core)[MG_STATE_COUNTER] != 0u
        || !nearf(m6_state(core)[MG_STATE_DELAY_TICKS], 0.0f, 1e-6f)) {
        fprintf(stderr, "MG delay did not freeze phase for 240 ticks\n");
        poly800_core_destroy(core);
        return 7;
    }

    m6_mg_control_tick(core);
    if ((uint8_t)m6_state(core)[MG_STATE_COUNTER] != 1u) {
        fprintf(stderr, "MG phase did not resume after delay\n");
        poly800_core_destroy(core);
        return 8;
    }

    /* Audio-rate scheduler must produce exactly the nominal 187.5 Hz slots. */
    m6_mg_reset_state(core);
    p.mg_delay = 0.0f;
    poly800_core_set_params(core, &p);
    float dco = 0.0f;
    float vcf = 0.0f;
    for (unsigned i = 0; i < 48000; ++i)
        m6_mg_audio_tick(core, &dco, &vcf);

    const unsigned counter_after_second =
        (unsigned)(uint8_t)m6_state(core)[MG_STATE_COUNTER];
    if (counter_after_second != 187u) {
        fprintf(stderr,
                "MG scheduler count mismatch after 1 s: %u (expected 187)\n",
                counter_after_second);
        poly800_core_destroy(core);
        return 9;
    }

    printf("M6 ROM MG OK: P81 exact, delay 0..240 ticks, multiplier max=116, "
           "triangle %.6f..%.6f Hz, bipolar %.3f..%.3f\n",
           low_hz, high_hz, lo, hi);

    poly800_core_destroy(core);
    return 0;
}
