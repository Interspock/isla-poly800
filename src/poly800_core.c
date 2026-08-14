// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 M4 calibration layer.
 *
 * Keep the M2 synthesis/filter core frozen below and override only the two
 * behaviours whose Bristol mappings were deliberately left provisional:
 * P83 MG->DCO and the P48 chorus effect.
 *
 * Bristol source baseline:
 *   nomadbyte/bristol-fixes @ 116fb8a2d21727676e21db5f1efe295c1ea22d61
 *   bristolpoly800.c, lfo.c, dimensionD.c, brightonPoly800.c
 */

#define poly800_core_create poly800_core_create_m2
#define poly800_core_render poly800_core_render_m2
#include "poly800_core_m2.inc"
#undef poly800_core_render
#undef poly800_core_create

#define P800_M4_CHORUS_HISTORY 4096u
#define P800_M4_CHORUS_STATE_SLOTS 6u
#define P800_M4_CHORUS_SPEED 0.104142368f
#define P800_M4_CHORUS_DEPTH 0.713488936f
#define P800_M4_CHORUS_SCAN 0.159550920f
#define P800_M4_CHORUS_GAIN 1.5f

enum {
    CH_STATE_HISTOUT = P800_M4_CHORUS_HISTORY,
    CH_STATE_SCANR,
    CH_STATE_SCANP,
    CH_STATE_CG,
    CH_STATE_DIR,
    CH_STATE_INIT
};

static float m4_chorus_sample(const Chorus* chorus, float position)
{
    while (position < 0.0f)
        position += (float)P800_M4_CHORUS_HISTORY;
    while (position >= (float)P800_M4_CHORUS_HISTORY)
        position -= (float)P800_M4_CHORUS_HISTORY;

    const uint32_t i0 = (uint32_t)position;
    const uint32_t i1 = (i0 + 1u) % P800_M4_CHORUS_HISTORY;
    const float frac = position - (float)i0;
    return chorus->delay[i0]
         + (chorus->delay[i1] - chorus->delay[i0]) * frac;
}

/*
 * Adaptation of Bristol dimensionD.c (operator 12 / chorusinit).
 *
 * The stock Poly-800 exposes only chorus on/off. Bristol has hidden extension
 * controls 58/68/78 for speed/depth/scan. For the headless stock surface we
 * freeze those to the values common to Bristol's shipped poly800 11/12/13
 * memories. P48 remains the sole visible switch.
 *
 * When P48 is off ISLA deliberately hard-bypasses the effect. Bristol's
 * generic effect path with gain=0 has a 1.5 dry coefficient, which is an
 * engine-level gain quirk rather than useful stock-program behaviour.
 */
static void m4_chorus_tick(Poly800Core* core, float mono,
    float* left, float* right)
{
    Chorus* chorus = &core->chorus;
    float* st = chorus->delay + P800_M4_CHORUS_HISTORY;
    uint32_t histin = chorus->write_pos;

    chorus->delay[histin] = mono;

    if (!core->params.chorus_on) {
        *left = mono;
        *right = mono;
        if (++histin >= P800_M4_CHORUS_HISTORY)
            histin = 0;
        chorus->write_pos = histin;
        return;
    }

    float histout = st[CH_STATE_HISTOUT - P800_M4_CHORUS_HISTORY];
    float scanr = st[CH_STATE_SCANR - P800_M4_CHORUS_HISTORY];
    float scanp = st[CH_STATE_SCANP - P800_M4_CHORUS_HISTORY];
    float cg = st[CH_STATE_CG - P800_M4_CHORUS_HISTORY];
    float dir = st[CH_STATE_DIR - P800_M4_CHORUS_HISTORY];

    if (st[CH_STATE_INIT - P800_M4_CHORUS_HISTORY] == 0.0f) {
        histout = 0.0f;
        scanr = 0.0f;
        scanp = 0.0f;
        cg = 0.0f;
        dir = 0.0f;
        st[CH_STATE_INIT - P800_M4_CHORUS_HISTORY] = 1.0f;
    }

    const float depth = P800_M4_CHORUS_DEPTH * 1024.0f;
    const float speed_hz = 0.1f + P800_M4_CHORUS_SPEED * 20.0f;
    const float speed = 1024.0f * speed_hz / (float)core->sample_rate;
    const float gain = P800_M4_CHORUS_GAIN;
    const float scan = P800_M4_CHORUS_SCAN * 0.0005f * gain;

    const float value = m4_chorus_sample(chorus, histout);

    if (dir == 0.0f) {
        cg += scan;
        if (cg > gain) {
            cg = gain;
            dir = 1.0f;
        }
    } else {
        cg -= scan;
        if (cg < 0.0f) {
            cg = 0.0f;
            dir = 0.0f;
        }
    }

    *right = mono * (1.5f - gain) + value * (gain - cg);
    *left  = mono * (1.5f - gain) + value * cg;

    chorus->delay[histin] += value * gain * 0.5f;

    if (++histin >= P800_M4_CHORUS_HISTORY)
        histin = 0;

    histout = (float)histin - scanp;
    while (histout < 0.0f)
        histout += (float)P800_M4_CHORUS_HISTORY;
    while (histout >= (float)P800_M4_CHORUS_HISTORY)
        histout -= (float)P800_M4_CHORUS_HISTORY;

    scanr += speed;
    while (scanr >= 1024.0f)
        scanr -= 1024.0f;
    scanp = (sinf((float)P800_TAU * scanr / 1024.0f) + 1.0f) * depth;

    chorus->write_pos = histin;
    st[CH_STATE_HISTOUT - P800_M4_CHORUS_HISTORY] = histout;
    st[CH_STATE_SCANR - P800_M4_CHORUS_HISTORY] = scanr;
    st[CH_STATE_SCANP - P800_M4_CHORUS_HISTORY] = scanp;
    st[CH_STATE_CG - P800_M4_CHORUS_HISTORY] = cg;
    st[CH_STATE_DIR - P800_M4_CHORUS_HISTORY] = dir;
}

Poly800Core* poly800_core_create(double sample_rate)
{
    Poly800Core* core = poly800_core_create_m2(sample_rate);
    if (!core)
        return NULL;

    const uint32_t required =
        P800_M4_CHORUS_HISTORY + P800_M4_CHORUS_STATE_SLOTS;
    if (core->chorus.size < required) {
        float* resized = (float*)realloc(
            core->chorus.delay, (size_t)required * sizeof(float));
        if (!resized) {
            poly800_core_destroy(core);
            return NULL;
        }
        memset(resized + core->chorus.size, 0,
               (size_t)(required - core->chorus.size) * sizeof(float));
        core->chorus.delay = resized;
        core->chorus.size = required;
    }

    poly800_core_reset(core);
    return core;
}

void poly800_core_render(
    Poly800Core* core, float* left, float* right, uint32_t nframes)
{
    if (!core || !left || !right)
        return;

    const int limit = voice_limit(core);
    const int double_mode = core->params.dco_mode == 2;
    int highest = highest_sounding_note(core);

    const float mg_dco = core->params.mg_dco / 15.0f;
    const float vcomod = mg_dco * mg_dco * 4.0f;

    for (uint32_t frame = 0; frame < nframes; ++frame) {
        /*
         * Bristol adds +1 to the sine LFO before the delayed gain DCA, so the
         * modulation bus is unipolar: (1 + sine) * fade_gain.
         */
        const float lfo_bipolar = lfo_tick(core);
        const float lfo = lfo_bipolar + core->lfo_gain;

        /* Bristol mult2buf(): frequency *= 1 + lfo_bus * vcomod. */
        float pitch_ratio = 1.0f + lfo * vcomod;
        if (pitch_ratio < 0.001f)
            pitch_ratio = 0.001f;

        float oscillator_bus = 0.0f;
        int highest_died = 0;

        for (int i = 0; i < limit; ++i) {
            Voice* voice = &core->voices[i];
            if (!voice->active)
                continue;

            const float env1 = env_tick(&voice->deg1, &core->derived.deg1);
            oscillator_bus += render_dco(
                voice->phase1, voice->inc1,
                core->derived.dco1_mask,
                core->derived.dco1_waveform,
                pitch_ratio)
                * env1 * core->derived.dco1_level;

            if (double_mode) {
                const float env2 = env_tick(&voice->deg2, &core->derived.deg2);
                oscillator_bus += render_dco(
                    voice->phase2, voice->inc2,
                    core->derived.dco2_mask,
                    core->derived.dco2_waveform,
                    pitch_ratio)
                    * env2 * core->derived.dco2_level;
            }

            if (!voice->gate
                && voice->deg1.stage == ENV_OFF
                && (!double_mode || voice->deg2.stage == ENV_OFF)) {
                if (voice->note == highest)
                    highest_died = 1;
                voice->active = 0;
            }
        }

        if (highest_died)
            highest = highest_sounding_note(core);

        const float env3 = env_tick(&core->deg3, &core->derived.deg3);
        float mixed = oscillator_bus * P800_OSC_BUS_GAIN;

        if (core->derived.noise_level > 0.0f) {
            mixed += white_noise(core)
                   * core->derived.noise_level
                   * env3;
        }

        const float modulation =
            env3 * core->derived.env_amount
            + lfo * core->derived.lfo_vcf_amount;

        const float filtered =
            shared_filter_tick(core, mixed, modulation, highest);

        float out_l;
        float out_r;
        m4_chorus_tick(core, filtered, &out_l, &out_r);

        left[frame] += out_l * core->params.master_gain;
        right[frame] += out_r * core->params.master_gain;
    }

    normalise_quadrature(&core->lfo_sin, &core->lfo_cos);
}
