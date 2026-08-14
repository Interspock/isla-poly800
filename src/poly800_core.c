// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 M4 calibration layer + stock-hardware corrections.
 *
 * Keep the M2 synthesis/filter core frozen below and override behaviours where
 * later hardware evidence is stronger than Bristol's extended emulation path:
 * stock DCO harmonic weighting, P32 DCO2 detune, P83 MG->DCO and P48 chorus.
 *
 * MG source baseline:
 *   nomadbyte/bristol-fixes @ 116fb8a2d21727676e21db5f1efe295c1ea22d61
 *   bristolpoly800.c, lfo.c
 *
 * Stock-hardware source references:
 *   Korg Poly-800 owner/service manuals for the additive square-wave DCO,
 *   DCO2 detune (-20 cents maximum), and the fixed MN3209/MN3102 BBD chorus.
 *
 * Bristol remains the architecture/code oracle where it follows the stock
 * instrument, but its frontend/extensions do not override explicit Korg
 * hardware specifications when the two conflict.
 */

#define poly800_core_create poly800_core_create_m2
#define poly800_core_render poly800_core_render_m2
#define poly800_core_reset poly800_core_reset_m2
#define poly800_core_set_params poly800_core_set_params_m2
#define render_dco render_dco_m2
#include "poly800_core_m2.inc"
#undef render_dco
#undef poly800_core_set_params
#undef poly800_core_reset
#undef poly800_core_render
#undef poly800_core_create

#define P800_M4_CHORUS_HISTORY 4096u
#define P800_M4_CHORUS_STATE_SLOTS 2u

/*
 * Stock MkI DCO harmonic law.
 *
 * The Poly-800 does not switch each footage generator between square and saw.
 * The tone generator always supplies square waves at 16', 8', 4' and 2'.
 * P12/P22 select the resistor-mix relationship:
 *
 *   waveform 1: 1, 1,   1,   1
 *   waveform 2: 1, 1/2, 1/4, 1/8
 *
 * With all four footages enabled the second relationship forms the familiar
 * stepped approximation to a sawtooth.  This supersedes the M2 provisional
 * interpretation that rendered four independent saw oscillators.
 */
static float render_dco(float phase[P800_HARMONICS],
    const float increment[P800_HARMONICS],
    uint8_t mask, int waveform, float pitch_ratio)
{
    static const float step_weight[P800_HARMONICS] = {
        1.0f, 0.5f, 0.25f, 0.125f
    };
    float out = 0.0f;

    for (unsigned i = 0; i < P800_HARMONICS; ++i) {
        if (!(mask & (1u << i)))
            continue;

        const float inc = increment[i] * pitch_ratio;
        const float weight = waveform == 2 ? step_weight[i] : 1.0f;
        out += square_sample(&phase[i], inc)
             * P800_DCO_HARMONIC_GAIN * weight;
    }
    return out;
}

/*
 * Hardware-informed fixed chorus constants.
 *
 * The MkI Poly-800 uses one MN3209 BBD driven by an MN3102 clock and exposes
 * only chorus ON/OFF. The service manual describes a fixed modulated clock;
 * the owner manual describes the result as a warm, subtle stereo ambience.
 *
 * These constants deliberately model that fixed behaviour rather than
 * Bristol's editable Dimension extension, whose default memory values create
 * a much deeper audible delay sweep than the stock instrument.
 */
#define P800_M4_CHORUS_RATE_HZ       0.55f
#define P800_M4_CHORUS_DELAY_SEC     0.0068f
#define P800_M4_CHORUS_DEPTH_SEC     0.00060f
#define P800_M4_CHORUS_WET           0.25f
#define P800_M4_CHORUS_DRY           (1.0f - P800_M4_CHORUS_WET)

enum {
    CH_STATE_PHASE = P800_M4_CHORUS_HISTORY,
    CH_STATE_INIT
};

/*
 * P32 hardware correction.
 *
 * Bristol's Brighton shim routes P32 through the positive half of NRO
 * FINETUNE, which spans roughly +1 semitone. Korg's owner/service manuals
 * explicitly specify the MkI DCO2 DETUNE control as 0..3 with 20 cents maximum,
 * and the service description says the hardware detune circuit lowers pitch by
 * thinning clock pulses. The stock LV2 therefore maps 0..3 linearly to
 * 0..-20 cents.
 *
 * IMPORTANT: this function must be idempotent. LV2 hosts commonly push the
 * same control values every process block. The first implementation multiplied
 * the already-corrected ratio again on every block, eventually driving DCO2
 * towards 0 Hz. Rebuild the absolute stock ratio from program parameters each
 * time instead of applying a relative multiplier.
 */
static void apply_stock_dco2_detune(Poly800Core* core)
{
    const float raw = clampf(core->params.dco2_detune, 0.0f, 3.0f);
    const double cents = -20.0 * (double)raw / 3.0;
    const double stock_ratio = exp2(cents / 1200.0);
    const double tune_ratio = exp2((double)core->params.tune_cents / 1200.0);

    core->derived.dco2_ratio = tune_ratio
        * ldexp(1.0, core->params.dco2_octave - 2)
        * exp2((double)core->params.dco2_interval / 12.0)
        * stock_ratio;

    refresh_all_voice_increments(core);
}

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
 * Fixed MkI-style BBD chorus approximation.
 *
 * Important differences from Bristol dimensionD.c:
 * - no hidden user/editable speed, depth or scan controls;
 * - no regenerative delay feedback;
 * - dry signal always remains present when chorus is enabled;
 * - short, sample-rate-independent BBD-like delay modulation;
 * - two complementary taps from the same history create a restrained stereo
 *   image without a conspicuous left/right pan sweep.
 *
 * History and LFO phase keep running while P48 is off so switching the chorus
 * on does not start from an empty delay line.
 */
static void m4_chorus_tick(Poly800Core* core, float mono,
    float* left, float* right)
{
    Chorus* chorus = &core->chorus;
    float* st = chorus->delay + P800_M4_CHORUS_HISTORY;
    uint32_t histin = chorus->write_pos;

    float phase = st[CH_STATE_PHASE - P800_M4_CHORUS_HISTORY];
    if (st[CH_STATE_INIT - P800_M4_CHORUS_HISTORY] == 0.0f) {
        phase = 0.0f;
        st[CH_STATE_INIT - P800_M4_CHORUS_HISTORY] = 1.0f;
    }

    chorus->delay[histin] = mono;

    const float centre = (float)core->sample_rate * P800_M4_CHORUS_DELAY_SEC;
    const float depth = (float)core->sample_rate * P800_M4_CHORUS_DEPTH_SEC;
    const float lfo = sinf(phase);

    const float read_l = (float)histin - (centre + depth * lfo);
    const float read_r = (float)histin - (centre - depth * lfo);
    const float wet_l = m4_chorus_sample(chorus, read_l);
    const float wet_r = m4_chorus_sample(chorus, read_r);

    if (core->params.chorus_on) {
        *left = mono * P800_M4_CHORUS_DRY + wet_l * P800_M4_CHORUS_WET;
        *right = mono * P800_M4_CHORUS_DRY + wet_r * P800_M4_CHORUS_WET;
    } else {
        *left = mono;
        *right = mono;
    }

    if (++histin >= P800_M4_CHORUS_HISTORY)
        histin = 0;

    phase += (float)P800_TAU * P800_M4_CHORUS_RATE_HZ
           / (float)core->sample_rate;
    if (phase >= (float)P800_TAU)
        phase -= (float)P800_TAU;

    chorus->write_pos = histin;
    st[CH_STATE_PHASE - P800_M4_CHORUS_HISTORY] = phase;
}

void poly800_core_set_params(Poly800Core* core, const Poly800Params* params)
{
    if (!core || !params)
        return;

    poly800_core_set_params_m2(core, params);
    apply_stock_dco2_detune(core);
}

void poly800_core_reset(Poly800Core* core)
{
    if (!core)
        return;

    poly800_core_reset_m2(core);
    apply_stock_dco2_detune(core);
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
