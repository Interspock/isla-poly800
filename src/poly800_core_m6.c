// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 M6 ROM-grounded control layer.
 *
 * M6 keeps the M4/M5 stock DCO, DEG, detune and chorus corrections and
 * replaces Bristol's provisional sine-LFO path with the control algorithm
 * recovered from the EX-800 80C85 firmware:
 *
 *   - P81 indexes the original 16-byte MG increment table at ROM 0x14EE;
 *   - an 8-bit phase accumulator is folded into the original triangle ramp;
 *   - the sign bit flips on accumulator overflow, yielding the full bipolar
 *     0 -> +peak -> 0 -> -peak -> 0 waveform over 512 accumulator counts;
 *   - P83/P84 use the firmware's four-bit fixed-point multiplier;
 *   - P82 uses the original LINEAR_TABLE entries and delay counter law;
 *   - the engine runs at the nominal firmware scheduler rate of 3000/16 Hz.
 *
 * The interrupt oscillator in the service documentation is specified as
 * roughly 2.4..3.6 kHz. 3000 Hz is therefore used as the nominal clock.
 * The DCO transfer uses the documented +/-160 cent full-scale MG calibration.
 * For VCF modulation M6 retains the established ISLA/Bristol filter-domain
 * full-scale constant while replacing its waveform and depth law; converting
 * the ROM DAC code into an exact NJM2069 cutoff law remains analog calibration
 * work, not something the firmware alone can prove.
 *
 * The recovered DEG LOG_TABLE/LINEAR_TABLE data is intentionally not mapped
 * directly to audio amplitude here. Those bytes are DAC/control-domain values
 * followed by analog VCAs. M5.4's manual/audio-calibrated envelope transfer is
 * retained until that analog stage is modelled, avoiding a false 'ROM exact'
 * amplitude curve.
 */

#define poly800_core_create poly800_core_create_m2
#define poly800_core_render poly800_core_render_m2
#define poly800_core_reset poly800_core_reset_m2
#define poly800_core_set_params poly800_core_set_params_m2
#define poly800_core_note_on poly800_core_note_on_m2
#define render_dco render_dco_m2
#include "poly800_core_m2.inc"
#undef render_dco
#undef poly800_core_note_on
#undef poly800_core_set_params
#undef poly800_core_reset
#undef poly800_core_render
#undef poly800_core_create

#define P800_M4_CHORUS_HISTORY 4096u
#define P800_M6_STATE_SLOTS 9u
#define P800_M6_IRQ_NOMINAL_HZ 3000.0f
#define P800_M6_MG_CONTROL_HZ (P800_M6_IRQ_NOMINAL_HZ / 16.0f)
#define P800_M6_DCO_MG_MAX_CENTS 160.0f
#define P800_M6_VCF_DOMAIN_MAX 8.0f
#define P800_M6_MG_FULL_SCALE 116.0f

/*
 * Extra control state lives beyond the fixed 4096-sample M4 chorus history.
 * At 96 kHz the M2 allocation is already larger than this; the M4 chorus only
 * addresses the first 4096 samples, so these positions remain private state.
 */
enum {
    CH_STATE_PHASE = P800_M4_CHORUS_HISTORY,
    CH_STATE_INIT,
    MG_STATE_SAMPLE_ACCUM,
    MG_STATE_COUNTER,
    MG_STATE_BIT,
    MG_STATE_DCO_NORM,
    MG_STATE_VCF_NORM,
    MG_STATE_DELAY_TICKS,
    MG_STATE_INIT
};

/* EX-800 ROM 0x14EE, used verbatim by SET_MG_FREQUENC. */
static const uint8_t p800_m6_mg_table[16] = {
    0x01, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0b, 0x0e,
    0x11, 0x15, 0x19, 0x1e, 0x24, 0x2b, 0x34, 0x3e
};

/*
 * EX-800 ROM 0x152B. P82 only addresses entries 0..15, but keep the complete
 * recovered table here because the same firmware table participates in the
 * DEG control calculations and is useful evidence for the next calibration.
 */
static const uint8_t p800_m6_linear_table[32] = {
    0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
    0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78,
    0x80, 0x88, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8,
    0xc0, 0xc9, 0xd2, 0xdb, 0xe4, 0xed, 0xf6, 0xfe
};

/* EX-800 ROM 0x14FE, retained as control-domain evidence for DEG work. */
static const uint8_t p800_m6_log_table[32] = {
    0xff, 0x80, 0x55, 0x40, 0x33, 0x2b, 0x25, 0x20,
    0x1d, 0x1b, 0x17, 0x15, 0x13, 0x11, 0x0f, 0x0d,
    0x0b, 0x09, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x0a
};

static int m6_param_index(float raw, int maximum)
{
    return clampi((int)lrintf(raw), 0, maximum);
}

static uint8_t m6_mg_increment(float raw)
{
    return p800_m6_mg_table[m6_param_index(raw, 15)];
}

static uint16_t m6_mg_delay_ticks(float raw)
{
    const unsigned index = (unsigned)m6_param_index(raw, 15);
    return (uint16_t)p800_m6_linear_table[index] << 1;
}

/*
 * Firmware routine 0x1CA8, expressed literally in unsigned 8-bit arithmetic.
 * B contains a 4-bit depth in its high nibble; C is the 6-bit folded MG ramp.
 * The routine consumes four bits from B and right-shifts C after each bit.
 */
static uint8_t m6_mul4(uint8_t b, uint8_t c)
{
    uint8_t h = 0;

    for (unsigned i = 0; i < 4; ++i) {
        const int carry = (b & 0x80u) != 0;
        b = (uint8_t)((b << 1) | (b >> 7));
        if (carry)
            h = (uint8_t)(h + c);
        c >>= 1;
    }
    return h;
}

static float* m6_state(Poly800Core* core)
{
    return core->chorus.delay;
}

static const float* m6_state_const(const Poly800Core* core)
{
    return core->chorus.delay;
}

static void m6_mg_reset_state(Poly800Core* core)
{
    float* st = m6_state(core);
    st[MG_STATE_SAMPLE_ACCUM] = 0.0f;
    st[MG_STATE_COUNTER] = 0.0f;
    st[MG_STATE_BIT] = 0.0f;
    st[MG_STATE_DCO_NORM] = 0.0f;
    st[MG_STATE_VCF_NORM] = 0.0f;
    st[MG_STATE_DELAY_TICKS] = 0.0f;
    st[MG_STATE_INIT] = 1.0f;
}

static void m6_mg_arm_note_delay(Poly800Core* core)
{
    float* st = m6_state(core);
    st[MG_STATE_DELAY_TICKS] = (float)m6_mg_delay_ticks(core->params.mg_delay);

    /*
     * The firmware holds the MG phase while its delay counter is active. Keep
     * the audible modulation centred during that hold; the free-running phase
     * itself is not reset, matching the instrument's non-retriggering MG.
     */
    if (st[MG_STATE_DELAY_TICKS] > 0.0f) {
        st[MG_STATE_DCO_NORM] = 0.0f;
        st[MG_STATE_VCF_NORM] = 0.0f;
    }
}

static float m6_depth_norm(float raw_depth, uint8_t magnitude, int negative)
{
    const uint8_t depth = (uint8_t)m6_param_index(raw_depth, 15);
    const uint8_t scaled = m6_mul4((uint8_t)(depth << 4), magnitude);
    const float value = (float)scaled / P800_M6_MG_FULL_SCALE;
    return negative ? -value : value;
}

/* One execution of firmware scheduler slot 11 (ROM 0x1BE6..0x1C33). */
static void m6_mg_control_tick(Poly800Core* core)
{
    float* st = m6_state(core);

    if (st[MG_STATE_INIT] == 0.0f)
        m6_mg_reset_state(core);

    if (st[MG_STATE_DELAY_TICKS] > 0.0f) {
        st[MG_STATE_DELAY_TICKS] -= 1.0f;
        return;
    }

    const uint8_t increment = m6_mg_increment(core->params.mg_frequency);
    const uint8_t old_counter = (uint8_t)st[MG_STATE_COUNTER];
    const unsigned sum = (unsigned)old_counter + (unsigned)increment;
    const uint8_t counter = (uint8_t)sum;
    uint8_t sign_bit = (uint8_t)st[MG_STATE_BIT] & 1u;

    if (sum > 0xffu)
        sign_bit ^= 1u;

    st[MG_STATE_COUNTER] = (float)counter;
    st[MG_STATE_BIT] = (float)sign_bit;

    /* ROM: if counter bit 7 is set, complement it, then keep bits 1..6. */
    const uint8_t ramper =
        (counter & 0x80u) ? (uint8_t)~counter : counter;
    const uint8_t magnitude = (uint8_t)((ramper & 0x7eu) >> 1);

    st[MG_STATE_DCO_NORM] =
        m6_depth_norm(core->params.mg_dco, magnitude, sign_bit != 0);
    st[MG_STATE_VCF_NORM] =
        m6_depth_norm(core->params.mg_vcf, magnitude, sign_bit != 0);
}

/*
 * Step the firmware-rate MG from the audio thread without interpolating it.
 * The original CV is sample-and-held, so preserving the 187.5 Hz control
 * staircase is more faithful than generating a smooth audio-rate triangle.
 */
static void m6_mg_audio_tick(Poly800Core* core,
    float* dco_norm, float* vcf_norm)
{
    float* st = m6_state(core);
    st[MG_STATE_SAMPLE_ACCUM] += P800_M6_MG_CONTROL_HZ;

    while (st[MG_STATE_SAMPLE_ACCUM] >= (float)core->sample_rate) {
        st[MG_STATE_SAMPLE_ACCUM] -= (float)core->sample_rate;
        m6_mg_control_tick(core);
    }

    *dco_norm = st[MG_STATE_DCO_NORM];
    *vcf_norm = st[MG_STATE_VCF_NORM];
}

/*
 * Stock MkI DCO harmonic law.
 *
 * The Poly-800 does not switch each footage generator between square and saw.
 * The tone generator always supplies square waves at 16', 8', 4' and 2'.
 * P12/P22 select the resistor-mix relationship:
 *
 *   waveform 1: 1, 1,   1,   1
 *   waveform 2: 1, 1/2, 1/4, 1/8
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

/* M5.4/M5.4.1 manual + factory-audio DEG calibration retained for M6. */
#define P800_M54_DEG_MAX_SECONDS 8.0f
#define P800_M54_DEG_LEVEL_EXP   2.2f
#define P800_M541_DEG_RATE_EXP   7.5f

static float stock_deg_level(float raw)
{
    const float n = clampf(raw, 0.0f, 31.0f) / 31.0f;
    if (n <= 0.0f)
        return 0.0f;
    if (n >= 1.0f)
        return 1.0f;
    return powf(n, P800_M54_DEG_LEVEL_EXP);
}

static float stock_deg_rate_step(const Poly800Core* core, float raw)
{
    const float n = clampf(raw, 0.0f, 31.0f) / 31.0f;
    if (n <= 0.0f)
        return 0.5f;

    const double seconds = (double)P800_M54_DEG_MAX_SECONDS
                         * pow((double)n, (double)P800_M541_DEG_RATE_EXP);
    return clampf((float)(1.0 / (seconds * core->sample_rate)),
                  0.0f, 0.5f);
}

static EnvConfig make_stock_env_config(const Poly800Core* core,
    float attack, float decay, float breakpoint,
    float slope, float sustain, float release)
{
    EnvConfig cfg;
    cfg.attack_step = stock_deg_rate_step(core, attack);
    cfg.decay_step = stock_deg_rate_step(core, decay);
    cfg.breakpoint = stock_deg_level(breakpoint);
    cfg.slope_step = stock_deg_rate_step(core, slope);
    cfg.sustain = stock_deg_level(sustain);
    cfg.release_step = stock_deg_rate_step(core, release);
    return cfg;
}

static void apply_stock_deg_calibration(Poly800Core* core)
{
    const Poly800Params* p = &core->params;

    core->derived.deg1 = make_stock_env_config(core,
        p->deg1_attack, p->deg1_decay, p->deg1_breakpoint,
        p->deg1_slope, p->deg1_sustain, p->deg1_release);
    core->derived.deg2 = make_stock_env_config(core,
        p->deg2_attack, p->deg2_decay, p->deg2_breakpoint,
        p->deg2_slope, p->deg2_sustain, p->deg2_release);
    core->derived.deg3 = make_stock_env_config(core,
        p->deg3_attack, p->deg3_decay, p->deg3_breakpoint,
        p->deg3_slope, p->deg3_sustain, p->deg3_release);
}

#define P800_M4_CHORUS_RATE_HZ       0.55f
#define P800_M4_CHORUS_DELAY_SEC     0.0068f
#define P800_M4_CHORUS_DEPTH_SEC     0.00060f
#define P800_M4_CHORUS_WET           0.25f
#define P800_M4_CHORUS_DRY           (1.0f - P800_M4_CHORUS_WET)

/*
 * P32 still uses the M5 documented endpoint model. The ROM proves that P32 is
 * a raw two-bit hardware selector, but does not reveal the pulse-thinning
 * ratios implemented outside the CPU; do not invent new intermediate steps.
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

    const int old_mode = core->params.dco_mode;
    poly800_core_set_params_m2(core, params);
    apply_stock_deg_calibration(core);
    apply_stock_dco2_detune(core);

    /* M2 resets all shared storage when WHOLE/DOUBLE changes. */
    if (core->params.dco_mode != old_mode)
        m6_mg_reset_state(core);
}

void poly800_core_reset(Poly800Core* core)
{
    if (!core)
        return;

    poly800_core_reset_m2(core);
    apply_stock_deg_calibration(core);
    apply_stock_dco2_detune(core);
    m6_mg_reset_state(core);
}

Poly800Core* poly800_core_create(double sample_rate)
{
    Poly800Core* core = poly800_core_create_m2(sample_rate);
    if (!core)
        return NULL;

    const uint32_t required =
        P800_M4_CHORUS_HISTORY + P800_M6_STATE_SLOTS;
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

void poly800_core_note_on(
    Poly800Core* core, uint8_t note, uint8_t velocity)
{
    if (!core)
        return;

    const unsigned was_held = core->held_count;
    poly800_core_note_on_m2(core, note, velocity);

    /* Korg MG delay is armed only when starting a new held-note phrase. */
    if (was_held == 0)
        m6_mg_arm_note_delay(core);
}

void poly800_core_render(
    Poly800Core* core, float* left, float* right, uint32_t nframes)
{
    if (!core || !left || !right)
        return;

    const int limit = voice_limit(core);
    const int double_mode = core->params.dco_mode == 2;
    int highest = highest_sounding_note(core);

    for (uint32_t frame = 0; frame < nframes; ++frame) {
        float mg_dco_norm;
        float mg_vcf_norm;
        m6_mg_audio_tick(core, &mg_dco_norm, &mg_vcf_norm);

        /* Stock DCO MG calibration: bipolar, +/-160 cents full scale. */
        const float mg_cents = mg_dco_norm * P800_M6_DCO_MG_MAX_CENTS;
        float pitch_ratio = exp2f(mg_cents / 1200.0f);
        if (!isfinite(pitch_ratio) || pitch_ratio < 0.001f)
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
            + mg_vcf_norm * P800_M6_VCF_DOMAIN_MAX;

        const float filtered =
            shared_filter_tick(core, mixed, modulation, highest);

        float out_l;
        float out_r;
        m4_chorus_tick(core, filtered, &out_l, &out_r);

        left[frame] += out_l * core->params.master_gain;
        right[frame] += out_r * core->params.master_gain;
    }

    /* Silence unused legacy quadrature drift; chorus has its own M4 phase. */
    core->lfo_sin = 0.0f;
    core->lfo_cos = 1.0f;

    /* Keep otherwise-unused recovered table visible to optimisers/tests. */
    (void)p800_m6_log_table[0];
    (void)m6_state_const(core);
}
