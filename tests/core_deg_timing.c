// SPDX-License-Identifier: GPL-3.0-or-later
#include <math.h>
#include <stdio.h>

/*
 * Include the implementation so this regression can exercise the calibrated
 * internal DEG law directly without inferring timing through oscillator/filter
 * audio.  This remains a test-only translation unit.
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

    /* Factory 84 uses DEG1 decay=slope=26 and sustain=0.  With equal rates,
     * the full peak-to-zero traversal should be the rate-26 full-scale time. */
    const double n = 26.0 / 31.0;
    const double expected84 = P800_M54_DEG_MAX_SECONDS * n * n;
    printf("Factory 84 DEG1 peak-to-zero reference: %.3f s\n", expected84);
    if (expected84 < 5.5 || expected84 > 5.8)
        return 2;

    return 0;
}
