// SPDX-License-Identifier: GPL-3.0-or-later
#include <math.h>
#include <stdio.h>

/*
 * Include the implementation so this regression can exercise the calibrated
 * internal DEG law directly without inferring timing through oscillator/filter
 * audio. This remains a test-only translation unit.
 */
#include "../src/poly800_core.c"

#define RATE 48000.0

static double decay_seconds(float breakpoint)
{
    Poly800Core* core = poly800_core_create(RATE);
    if (!core)
        return -1.0;

    EnvConfig cfg = make_stock_env_config(core,
        0.0f, 31.0f, breakpoint,
        0.0f, breakpoint, 0.0f);

    Envelope env;
    env.value = 1.0f;
    env.stage = ENV_DECAY;

    unsigned long samples = 0;
    const unsigned long limit = (unsigned long)(RATE * 10.0);
    while (env.stage == ENV_DECAY && samples < limit) {
        env_tick(&env, &cfg);
        ++samples;
    }

    poly800_core_destroy(core);
    if (samples >= limit)
        return -1.0;
    return (double)samples / RATE;
}

static double release_seconds(float release)
{
    Poly800Core* core = poly800_core_create(RATE);
    if (!core)
        return -1.0;

    EnvConfig cfg = make_stock_env_config(core,
        0.0f, 0.0f, 31.0f,
        0.0f, 31.0f, release);

    Envelope env;
    env.value = 1.0f;
    env.stage = ENV_RELEASE;

    unsigned long samples = 0;
    const unsigned long limit = (unsigned long)(RATE * 10.0);
    while (env.stage == ENV_RELEASE && samples < limit) {
        env_tick(&env, &cfg);
        ++samples;
    }

    poly800_core_destroy(core);
    if (samples >= limit)
        return -1.0;
    return (double)samples / RATE;
}

int main(void)
{
    /*
     * Korg Poly-800 Owner's Manual, DEG section:
     * with DECAY=31 the actual decay is about 0.5, 1.2, 3 and 5 seconds
     * for breakpoint 30, 29, 25 and 20 respectively.
     */
    static const struct {
        float bp;
        double expected;
    } cases[] = {
        {30.0f, 0.5},
        {29.0f, 1.2},
        {25.0f, 3.0},
        {20.0f, 5.0},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const double measured = decay_seconds(cases[i].bp);
        const double error = fabs(measured - cases[i].expected);
        printf("DEG decay31 BP=%g: %.3f s (Korg about %.1f s)\n",
               cases[i].bp, measured, cases[i].expected);

        /* The manual explicitly says "about"; 0.20 s keeps the test useful
         * without pretending the printed examples are laboratory precision. */
        if (measured < 0.0 || error > 0.20) {
            fprintf(stderr, "DEG timing misses Korg manual anchor\n");
            return 1;
        }
    }

    /*
     * Factory program 84 uses Release=20 on DEG1/DEG2/DEG3. Two isolated
     * short notes in a hardware recording fall from the key-off knee to near
     * silence in about 0.2-0.3 s. With the common Korg rate law and a full
     * scale starting level, rate 20 should therefore be around 0.30 s rather
     * than the 3.33 s produced by the old Bristol-derived rate^2 curve.
     */
    const double release20 = release_seconds(20.0f);
    printf("DEG release20 full-scale reference: %.3f s\n", release20);
    if (release20 < 0.26 || release20 > 0.34) {
        fprintf(stderr, "DEG release20 misses factory-audio anchor\n");
        return 2;
    }

    /* The same rate law applies to Attack/Decay/Slope/Release. Factory 84's
     * rate 26 is now roughly 2.14 s for a full 0..1 traversal; actual Decay and
     * Slope segments are shorter because they cover only part of the level
     * range. */
    const double n = 26.0 / 31.0;
    const double expected84 = P800_M54_DEG_MAX_SECONDS
                            * pow(n, P800_M541_DEG_RATE_EXP);
    printf("Factory 84 rate26 full-scale reference: %.3f s\n", expected84);
    if (expected84 < 2.0 || expected84 > 2.3)
        return 3;

    return 0;
}
