// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include "poly800_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BENCH_RATE 48000
#define BENCH_BLOCK 512
#define BENCH_SECONDS 6

static double
now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void
run_scenario(const char* name, int mode, int voices, int chorus)
{
    float left[BENCH_BLOCK];
    float right[BENCH_BLOCK];
    static const uint8_t notes[8] = {48, 52, 55, 59, 60, 64, 67, 71};

    Poly800Core* core = poly800_core_create(BENCH_RATE);
    Poly800Params params;
    poly800_params_default(&params);
    params.dco_mode = mode;
    params.chorus_on = chorus;
    if (mode == 2) {
        params.dco2_h8 = 1;
        params.dco2_interval = 7;
        params.dco2_detune = 2.0f;
    }
    poly800_core_set_params(core, &params);

    for (int i = 0; i < voices; ++i) {
        poly800_core_note_on(core, notes[i], 100);
    }

    const int blocks = (BENCH_SECONDS * BENCH_RATE) / BENCH_BLOCK;
    const double start = now_seconds();
    for (int b = 0; b < blocks; ++b) {
        memset(left, 0, sizeof(left));
        memset(right, 0, sizeof(right));
        poly800_core_render(core, left, right, BENCH_BLOCK);
    }
    const double elapsed = now_seconds() - start;
    const double audio_seconds = (double)blocks * BENCH_BLOCK / BENCH_RATE;
    const double samples = (double)blocks * BENCH_BLOCK;

    printf("%-22s %8.2fx realtime  %8.1f ns/sample\n",
           name, audio_seconds / elapsed, elapsed * 1e9 / samples);

    poly800_core_destroy(core);
}

int
main(void)
{
    run_scenario("WHOLE 1 voice", 1, 1, 0);
    run_scenario("WHOLE 4 voices", 1, 4, 0);
    run_scenario("WHOLE 8 voices", 1, 8, 0);
    run_scenario("DOUBLE 1 voice", 2, 1, 0);
    run_scenario("DOUBLE 4 voices", 2, 4, 0);
    run_scenario("DOUBLE 4 + chorus", 2, 4, 1);
    return 0;
}
