// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 DSP core — M2 Bristol calibration pass.
 *
 * The Poly-800 signal architecture, ADBSSR/ENV5S rate law, shared paraphonic
 * filter routing and Huovilainen filter implementation are adapted from
 * Bristol's GPL-3.0-or-later implementation. Portions adapted from Bristol
 * are Copyright (c) Nick Copeland <nickycopeland@hotmail.com> 1996,2012,
 * notably bristolpoly800.c, nro.c, env5stage.c and filter.c.
 *
 * ISLA keeps all mutable DSP state per plugin instance. M2 follows Bristol's
 * stock Poly-800 path more closely while preserving the headless LV2 API and
 * the M1.1 real-time performance work.
 */

#include "poly800_core.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define P800_MAX_VOICES 8
#define P800_HARMONICS 4
#define P800_TAU 6.283185307179586476925286766559
#define P800_PI 3.1415926535897932384626433832795
#define P800_LN2 0.69314718055994530942f
#define P800_FILTER_V2 40000.0f
#define P800_FILTER_OV2 0.000025f
#define P800_FILTER_MOD_GAIN 0.02f
#define P800_DCO_HARMONIC_GAIN 0.25f
#define P800_OSC_BUS_GAIN 8.0f

typedef enum {
    ENV_OFF = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SLOPE,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvStage;

typedef struct {
    float value;
    EnvStage stage;
} Envelope;

typedef struct {
    float attack_step;
    float decay_step;
    float breakpoint;
    float slope_step;
    float sustain;
    float release_step;
} EnvConfig;

typedef struct {
    uint8_t active;
    uint8_t gate;
    uint8_t note;
    uint64_t age;
    float phase1[P800_HARMONICS];
    float phase2[P800_HARMONICS];
    float inc1[P800_HARMONICS];
    float inc2[P800_HARMONICS];
    Envelope deg1;
    Envelope deg2;
} Voice;

typedef struct {
    float az1, az2, az3, az4, az5;
    float ay1, ay2, ay3, ay4;
    float amf;
    uint32_t noise1;
    uint32_t noise2;
} SharedFilter;

typedef struct {
    float* delay;
    uint32_t size;
    uint32_t write_pos;
    float sin_phase;
    float cos_phase;
    float sin_step;
    float cos_step;
} Chorus;

typedef struct {
    EnvConfig deg1;
    EnvConfig deg2;
    EnvConfig deg3;

    uint8_t dco1_mask;
    uint8_t dco2_mask;
    int dco1_waveform;
    int dco2_waveform;
    double dco1_ratio;
    double dco2_ratio;
    float dco1_level;
    float dco2_level;
    float noise_level;

    float env_amount;
    float lfo_vcf_amount;
    float lfo_pitch_semitones;
    float lfo_gain_step;

    float filter_cutoff;
    float filter_resonance;
    float filter_keytrack;
} Derived;

struct Poly800Core {
    double sample_rate;
    double inv_sample_rate;
    double note_freq[128];

    Poly800Params params;
    Derived derived;
    int last_mode;

    Voice voices[P800_MAX_VOICES];
    uint64_t age_counter;
    uint8_t held_notes[128];
    unsigned held_count;

    Envelope deg3;
    SharedFilter filter;

    float lfo_sin;
    float lfo_cos;
    float lfo_sin_step;
    float lfo_cos_step;
    double lfo_delay_remaining;
    float lfo_gain;

    uint32_t noise_state;
    Chorus chorus;
};

static float clampf(float value, float lo, float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static int clampi(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static uint32_t xorshift32(uint32_t* state)
{
    uint32_t x = *state;
    if (!x) x = 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x6d2b79f5u;
    return *state;
}

static void env_reset(Envelope* env)
{
    env->value = 0.0f;
    env->stage = ENV_OFF;
}

static void env_trigger(Envelope* env)
{
    env->value = 0.0f;
    env->stage = ENV_ATTACK;
}

static void env_release(Envelope* env)
{
    if (env->stage != ENV_OFF)
        env->stage = ENV_RELEASE;
}

/* Bristol ENV5S rate law, precomputed whenever program parameters change. */
static float env_rate_step(const Poly800Core* core, float raw)
{
    const float n = clampf(raw, 0.0f, 31.0f) / 31.0f;
    if (n <= 0.0f)
        return 0.5f;
    return clampf((float)(1.0 / ((double)n * (double)n
                                 * core->sample_rate * 10.0)),
                  0.0f, 0.5f);
}

static EnvConfig make_env_config(const Poly800Core* core,
    float attack, float decay, float breakpoint,
    float slope, float sustain, float release)
{
    EnvConfig cfg;
    cfg.attack_step = env_rate_step(core, attack);
    cfg.decay_step = env_rate_step(core, decay);
    cfg.breakpoint = clampf(breakpoint, 0.0f, 31.0f) / 31.0f;
    cfg.slope_step = env_rate_step(core, slope);
    cfg.sustain = clampf(sustain, 0.0f, 31.0f) / 31.0f;
    cfg.release_step = env_rate_step(core, release);
    return cfg;
}

static void env_move(Envelope* env, float target, float step, EnvStage next)
{
    if (env->value < target) {
        env->value += step;
        if (env->value >= target) {
            env->value = target;
            env->stage = next;
        }
    } else if (env->value > target) {
        env->value -= step;
        if (env->value <= target) {
            env->value = target;
            env->stage = next;
        }
    } else {
        env->stage = next;
    }
}

static float env_tick(Envelope* env, const EnvConfig* cfg)
{
    switch (env->stage) {
    case ENV_ATTACK:
        env_move(env, 1.0f, cfg->attack_step, ENV_DECAY);
        break;
    case ENV_DECAY:
        env_move(env, cfg->breakpoint, cfg->decay_step, ENV_SLOPE);
        break;
    case ENV_SLOPE:
        env_move(env, cfg->sustain, cfg->slope_step, ENV_SUSTAIN);
        break;
    case ENV_SUSTAIN:
        env->value = cfg->sustain;
        break;
    case ENV_RELEASE:
        env_move(env, 0.0f, cfg->release_step, ENV_OFF);
        break;
    case ENV_OFF:
    default:
        env->value = 0.0f;
        break;
    }
    return env->value;
}

static float poly_blep(float t, float dt)
{
    if (dt <= 0.0f)
        return 0.0f;
    if (t < dt) {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

static float square_sample(float* phase, float increment)
{
    float inc = increment;
    if (inc <= 0.0f)
        return 0.0f;
    if (inc > 0.49f)
        inc = 0.49f;

    const float p = *phase;
    float out = p < 0.5f ? 1.0f : -1.0f;
    out += poly_blep(p, inc);

    float shifted = p + 0.5f;
    if (shifted >= 1.0f)
        shifted -= 1.0f;
    out -= poly_blep(shifted, inc);

    float next = p + inc;
    if (next >= 1.0f)
        next -= 1.0f;
    *phase = next;
    return out;
}

static float saw_sample(float* phase, float increment)
{
    float inc = increment;
    if (inc <= 0.0f)
        return 0.0f;
    if (inc > 0.49f)
        inc = 0.49f;

    const float p = *phase;
    float out = 2.0f * p - 1.0f;
    out -= poly_blep(p, inc);

    float next = p + inc;
    if (next >= 1.0f)
        next -= 1.0f;
    *phase = next;
    return out;
}

static uint8_t harmonic_mask(int h16, int h8, int h4, int h2)
{
    return (uint8_t)((h16 ? 1u : 0u)
                   | (h8  ? 2u : 0u)
                   | (h4  ? 4u : 0u)
                   | (h2  ? 8u : 0u));
}

/*
 * Bristol maps P32 through NRO FINETUNE, which linearly interpolates the
 * frequency ratio between unison and one semitone. P32 itself is 0..3.
 */
static double dco2_fine_ratio(float raw)
{
    const double semitone = exp2(1.0 / 12.0);
    const double n = (double)clampf(raw, 0.0f, 3.0f) / 3.0;
    return 1.0 + (semitone - 1.0) * n;
}

/*
 * M1.1 used an exponential pitch interpretation. Until the absolute Bristol
 * MG depth is A/B measured, retain that bounded mapping but keep it out of the
 * expensive path. The verified M2 changes are waveform, footage, detune and
 * filter behaviour.
 */
static float fast_pitch_ratio(float semitones)
{
    const float x = semitones * (1.0f / 12.0f);
    const float y = P800_LN2 * x;
    return 1.0f + y + 0.5f * y * y + (1.0f / 6.0f) * y * y * y;
}

static int voice_limit(const Poly800Core* core)
{
    return core->params.dco_mode == 2 ? 4 : 8;
}

static void refresh_voice_increments(Poly800Core* core, Voice* voice)
{
    static const double footage[P800_HARMONICS] = {
        0.5, 1.0, 2.0, 4.0
    };
    const double base = core->note_freq[voice->note] * core->inv_sample_rate;

    for (unsigned i = 0; i < P800_HARMONICS; ++i) {
        voice->inc1[i] = (float)(base * core->derived.dco1_ratio * footage[i]);
        voice->inc2[i] = (float)(base * core->derived.dco2_ratio * footage[i]);
    }
}

static void refresh_all_voice_increments(Poly800Core* core)
{
    const int limit = voice_limit(core);
    for (int i = 0; i < limit; ++i)
        if (core->voices[i].active)
            refresh_voice_increments(core, &core->voices[i]);
}

/*
 * NRO adds every enabled footage to the oscillator bus; it does not divide by
 * the number of selected footages. A fixed trim keeps the LV2 in sane float
 * ranges while preserving that additive behaviour.
 */
static float render_dco(float phase[P800_HARMONICS],
    const float increment[P800_HARMONICS],
    uint8_t mask, int waveform, float pitch_ratio)
{
    float out = 0.0f;

    for (unsigned i = 0; i < P800_HARMONICS; ++i) {
        if (!(mask & (1u << i)))
            continue;

        const float inc = increment[i] * pitch_ratio;
        out += (waveform == 2
                ? saw_sample(&phase[i], inc)
                : square_sample(&phase[i], inc))
             * P800_DCO_HARMONIC_GAIN;
    }
    return out;
}

static float white_noise(Poly800Core* core)
{
    const uint32_t value = xorshift32(&core->noise_state);
    return ((float)(value >> 8) / 8388607.5f) - 1.0f;
}

static Voice* find_voice_for_note_on(Poly800Core* core, uint8_t note)
{
    const int limit = voice_limit(core);
    Voice* free_voice = NULL;
    Voice* oldest = &core->voices[0];

    for (int i = 0; i < limit; ++i) {
        Voice* voice = &core->voices[i];
        if (voice->active && voice->note == note)
            return voice;
        if (!voice->active && !free_voice)
            free_voice = voice;
        if (voice->age < oldest->age)
            oldest = voice;
    }
    return free_voice ? free_voice : oldest;
}

static int highest_sounding_note(const Poly800Core* core)
{
    const int limit = voice_limit(core);
    int highest = 0;
    int found = 0;

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < limit; ++i) {
            const Voice* voice = &core->voices[i];
            if (!voice->active || (pass == 0 && !voice->gate))
                continue;
            if (!found || voice->note > highest) {
                highest = voice->note;
                found = 1;
            }
        }
        if (found)
            break;
    }
    return found ? highest : 0;
}

static float filter_tanh(float x)
{
    return tanhf(x);
}

static float filter_denormal_noise(SharedFilter* f)
{
    /*
     * Bristol injects an extremely small signal only to prevent denormals.
     * Keep both RNG words per instance rather than using Bristol's globals.
     */
    f->noise1 ^= f->noise2;
    f->noise2 += f->noise1;
    return (float)(int32_t)f->noise2 * (1.0e-11f / 2147483648.0f);
}

/*
 * Bristol Poly-800 initializes filter type 4. At 44.1/48 kHz that enters the
 * 2x Huovilainen branch in filter.c. This is a direct per-instance adaptation
 * of that signal path, with the same tuning/correction constants.
 */
static float shared_filter_tick(Poly800Core* core,
    float input, float modulation, int highest_note)
{
    SharedFilter* f = &core->filter;
    const float cutoff = core->derived.filter_cutoff;
    const float tracking = core->derived.filter_keytrack;
    const float resonance = core->derived.filter_resonance;
    float coff;

    if (tracking <= 0.0f) {
        coff = cutoff * cutoff * (float)(20000.0 / core->sample_rate);
    } else {
        const double cfreq = core->note_freq[clampi(highest_note, 0, 127)];
        coff = (float)(tracking * cutoff * 4.0 * cfreq / core->sample_rate);
    }

    /*
     * Bristol uses one Huovilainen pass at >=88 kHz and a 2x internal
     * oversampling branch below 88 kHz. This matters for the AudioLink's
     * 96 kHz operating mode, so preserve both branches.
     */
    const int high_rate = core->sample_rate >= 88000.0;
    const float mod_gain = high_rate ? 0.03f : P800_FILTER_MOD_GAIN;
    const float limit = high_rate
        ? (float)(20000.0 / core->sample_rate)
        : 0.5f;

    float kfc = coff + modulation * mod_gain;
    kfc = clampf(kfc, 1.0e-10f, limit);

    const float kfcr =
        kfc * (kfc * (1.8730f * kfc + 0.4955f) - 0.6490f) + 0.9988f;
    const float kacr = kfc * (-3.9364f * kfc + 1.8409f) + 0.9968f;
    const float rate_scale = high_rate ? 1.0f : 0.5f;
    const float k2vg = 1.0f
        - expf(-2.0f * (float)P800_PI * kfcr * kfc * rate_scale);

    const int passes = high_rate ? 1 : 2;
    for (int pass = 0; pass < passes; ++pass) {
        const int add_noise = high_rate || pass == 1;
        const float feed =
            (input + (add_noise ? filter_denormal_noise(f) : 0.0f))
            * P800_FILTER_OV2
            - 4.0f * resonance * f->amf * kacr;

        f->ay1 = f->az1 + k2vg
            * (filter_tanh(feed) - filter_tanh(f->az1));
        f->az1 = f->ay1;

        f->ay2 = f->az2 + k2vg
            * (filter_tanh(f->ay1) - filter_tanh(f->az2));
        f->az2 = f->ay2;

        f->ay3 = f->az3 + k2vg
            * (filter_tanh(f->ay2) - filter_tanh(f->az3));
        f->az3 = f->ay3;

        f->ay4 = f->az4 + k2vg
            * (filter_tanh(f->ay3) - filter_tanh(f->az4));
        f->az4 = f->ay4;

        if (high_rate) {
            f->amf = f->ay4;
        } else {
            f->amf = 0.5f * (f->ay4 + f->az5);
            f->az5 = f->ay4;
        }
    }

    if (!isfinite(f->amf) || !isfinite(f->az1) || !isfinite(f->az2)
        || !isfinite(f->az3) || !isfinite(f->az4) || !isfinite(f->az5)) {
        const uint32_t n1 = f->noise1;
        const uint32_t n2 = f->noise2;
        memset(f, 0, sizeof(*f));
        f->noise1 = n1 ? n1 : 0x67452301u;
        f->noise2 = n2 ? n2 : 0xefcdab89u;
        return 0.0f;
    }

    return f->amf * P800_FILTER_V2 * (high_rate ? 1.0f : 0.5f);
}

static float chorus_read(const Chorus* chorus, float delay_samples)
{
    float read = (float)chorus->write_pos - delay_samples;
    if (read < 0.0f)
        read += (float)chorus->size;

    const uint32_t i0 = (uint32_t)read;
    uint32_t i1 = i0 + 1u;
    if (i1 >= chorus->size)
        i1 = 0;
    const float frac = read - (float)i0;
    return chorus->delay[i0]
         + (chorus->delay[i1] - chorus->delay[i0]) * frac;
}

static void advance_quadrature(float* sine, float* cosine,
    float sin_step, float cos_step)
{
    const float next_sine = *sine * cos_step + *cosine * sin_step;
    const float next_cosine = *cosine * cos_step - *sine * sin_step;
    *sine = next_sine;
    *cosine = next_cosine;
}

static void normalise_quadrature(float* sine, float* cosine)
{
    const float mag = sqrtf(*sine * *sine + *cosine * *cosine);
    if (mag > 0.0f && isfinite(mag)) {
        *sine /= mag;
        *cosine /= mag;
    } else {
        *sine = 0.0f;
        *cosine = 1.0f;
    }
}

static void chorus_tick(Poly800Core* core, float mono,
    float* left, float* right)
{
    Chorus* chorus = &core->chorus;
    chorus->delay[chorus->write_pos] = mono;

    if (!core->params.chorus_on) {
        *left = mono;
        *right = mono;
    } else {
        /*
         * Chorus remains the M1 implementation. Exact Bristol Dimension-chorus
         * constants are deliberately left for the next A/B calibration pass.
         */
        const float base = (float)(core->sample_rate * 0.010);
        const float depth = (float)(core->sample_rate * 0.003);
        const float delay_l = base
            + depth * (0.5f + 0.5f * chorus->sin_phase);
        const float delay_r = base
            + depth * (0.5f + 0.5f * chorus->cos_phase);
        *left = mono * 0.72f + chorus_read(chorus, delay_l) * 0.28f;
        *right = mono * 0.72f + chorus_read(chorus, delay_r) * 0.28f;
    }

    if (++chorus->write_pos >= chorus->size)
        chorus->write_pos = 0;
    advance_quadrature(&chorus->sin_phase, &chorus->cos_phase,
                       chorus->sin_step, chorus->cos_step);
}

static float lfo_tick(Poly800Core* core)
{
    const float raw = core->lfo_sin;
    advance_quadrature(&core->lfo_sin, &core->lfo_cos,
                       core->lfo_sin_step, core->lfo_cos_step);

    if (core->held_count == 0)
        return 0.0f;

    if (core->lfo_delay_remaining > 0.0) {
        core->lfo_delay_remaining -= 1.0;
        return 0.0f;
    }

    if (core->params.mg_delay <= 0.0f) {
        core->lfo_gain = 1.0f;
    } else if (core->lfo_gain < 1.0f) {
        core->lfo_gain += core->derived.lfo_gain_step;
        if (core->lfo_gain > 1.0f)
            core->lfo_gain = 1.0f;
    }
    return raw * core->lfo_gain;
}

void poly800_params_default(Poly800Params* p)
{
    if (!p)
        return;
    memset(p, 0, sizeof(*p));

    p->master_gain = 0.32f;

    p->dco1_octave = 2;
    p->dco1_waveform = 1;
    p->dco1_h8 = 1;
    p->dco1_level = 31.0f;
    p->dco_mode = 1;

    p->dco2_octave = 2;
    p->dco2_waveform = 1;
    p->dco2_h8 = 1;
    p->dco2_level = 24.0f;
    p->dco2_detune = 1.0f;

    p->vcf_cutoff = 62.0f;
    p->vcf_resonance = 2.0f;
    p->vcf_keytrack = 1;
    p->vcf_polarity = 2;
    p->vcf_env_intensity = 4.0f;
    p->vcf_trigger = 1;

    p->deg1_breakpoint = 31.0f;
    p->deg1_slope = 8.0f;
    p->deg1_sustain = 24.0f;
    p->deg1_release = 8.0f;

    p->deg2_breakpoint = 31.0f;
    p->deg2_slope = 8.0f;
    p->deg2_sustain = 24.0f;
    p->deg2_release = 8.0f;

    p->deg3_decay = 8.0f;
    p->deg3_breakpoint = 24.0f;
    p->deg3_slope = 8.0f;
    p->deg3_sustain = 18.0f;
    p->deg3_release = 8.0f;

    p->mg_frequency = 4.0f;
}

static void clamp_params(Poly800Params* p)
{
    p->master_gain = clampf(p->master_gain, 0.0f, 1.0f);
    p->tune_cents = clampf(p->tune_cents, -100.0f, 100.0f);

    p->dco1_octave = clampi(p->dco1_octave, 1, 3);
    p->dco1_waveform = clampi(p->dco1_waveform, 1, 2);
    p->dco1_h16 = !!p->dco1_h16;
    p->dco1_h8 = !!p->dco1_h8;
    p->dco1_h4 = !!p->dco1_h4;
    p->dco1_h2 = !!p->dco1_h2;
    p->dco1_level = clampf(p->dco1_level, 0.0f, 31.0f);
    p->dco_mode = clampi(p->dco_mode, 1, 2);

    p->dco2_octave = clampi(p->dco2_octave, 1, 3);
    p->dco2_waveform = clampi(p->dco2_waveform, 1, 2);
    p->dco2_h16 = !!p->dco2_h16;
    p->dco2_h8 = !!p->dco2_h8;
    p->dco2_h4 = !!p->dco2_h4;
    p->dco2_h2 = !!p->dco2_h2;
    p->dco2_level = clampf(p->dco2_level, 0.0f, 31.0f);
    p->dco2_interval = clampi(p->dco2_interval, 0, 12);
    p->dco2_detune = clampf(p->dco2_detune, 0.0f, 3.0f);

    p->noise_level = clampf(p->noise_level, 0.0f, 15.0f);

    p->vcf_cutoff = clampf(p->vcf_cutoff, 0.0f, 99.0f);
    p->vcf_resonance = clampf(p->vcf_resonance, 0.0f, 15.0f);
    p->vcf_keytrack = clampi(p->vcf_keytrack, 0, 2);
    p->vcf_polarity = clampi(p->vcf_polarity, 1, 2);
    p->vcf_env_intensity = clampf(p->vcf_env_intensity, 0.0f, 15.0f);
    p->vcf_trigger = clampi(p->vcf_trigger, 1, 2);
    p->chorus_on = !!p->chorus_on;

#define CLAMP_DEG(name) \
    p->name##_attack = clampf(p->name##_attack, 0.0f, 31.0f); \
    p->name##_decay = clampf(p->name##_decay, 0.0f, 31.0f); \
    p->name##_breakpoint = clampf(p->name##_breakpoint, 0.0f, 31.0f); \
    p->name##_slope = clampf(p->name##_slope, 0.0f, 31.0f); \
    p->name##_sustain = clampf(p->name##_sustain, 0.0f, 31.0f); \
    p->name##_release = clampf(p->name##_release, 0.0f, 31.0f)

    CLAMP_DEG(deg1);
    CLAMP_DEG(deg2);
    CLAMP_DEG(deg3);
#undef CLAMP_DEG

    p->mg_frequency = clampf(p->mg_frequency, 0.0f, 15.0f);
    p->mg_delay = clampf(p->mg_delay, 0.0f, 15.0f);
    p->mg_dco = clampf(p->mg_dco, 0.0f, 15.0f);
    p->mg_vcf = clampf(p->mg_vcf, 0.0f, 15.0f);
}

static void rebuild_derived(Poly800Core* core, const Poly800Params* old)
{
    Derived* d = &core->derived;

    d->deg1 = make_env_config(core,
        core->params.deg1_attack, core->params.deg1_decay,
        core->params.deg1_breakpoint, core->params.deg1_slope,
        core->params.deg1_sustain, core->params.deg1_release);
    d->deg2 = make_env_config(core,
        core->params.deg2_attack, core->params.deg2_decay,
        core->params.deg2_breakpoint, core->params.deg2_slope,
        core->params.deg2_sustain, core->params.deg2_release);
    d->deg3 = make_env_config(core,
        core->params.deg3_attack, core->params.deg3_decay,
        core->params.deg3_breakpoint, core->params.deg3_slope,
        core->params.deg3_sustain, core->params.deg3_release);

    d->dco1_mask = harmonic_mask(core->params.dco1_h16,
        core->params.dco1_h8, core->params.dco1_h4, core->params.dco1_h2);
    d->dco2_mask = harmonic_mask(core->params.dco2_h16,
        core->params.dco2_h8, core->params.dco2_h4, core->params.dco2_h2);
    d->dco1_waveform = core->params.dco1_waveform;
    d->dco2_waveform = core->params.dco2_waveform;

    const double tune_ratio = exp2((double)core->params.tune_cents / 1200.0);
    d->dco1_ratio = tune_ratio
                  * ldexp(1.0, core->params.dco1_octave - 2);
    d->dco2_ratio = tune_ratio
                  * ldexp(1.0, core->params.dco2_octave - 2)
                  * exp2((double)core->params.dco2_interval / 12.0)
                  * dco2_fine_ratio(core->params.dco2_detune);

    d->dco1_level = core->params.dco1_level / 31.0f;
    d->dco2_level = core->params.dco2_level / 31.0f;
    d->noise_level = core->params.noise_level / 15.0f;

    const float polarity =
        core->params.vcf_polarity == 1 ? -1.0f : 1.0f;
    d->env_amount = polarity
        * (core->params.vcf_env_intensity / 15.0f) * 2.0f;

    const float mg_vcf = core->params.mg_vcf / 15.0f;
    const float mg_dco = core->params.mg_dco / 15.0f;
    d->lfo_vcf_amount = mg_vcf * mg_vcf * 8.0f;
    d->lfo_pitch_semitones = mg_dco * mg_dco;
    d->lfo_gain_step = core->params.mg_delay > 0.0f
        ? (float)(1.0 / ((double)core->params.mg_delay * core->sample_rate))
        : 1.0f;

    d->filter_cutoff = core->params.vcf_cutoff / 99.0f;
    d->filter_resonance = core->params.vcf_resonance / 15.0f;
    d->filter_keytrack = (float)core->params.vcf_keytrack * 0.5f;

    const float n = core->params.mg_frequency / 15.0f;
    const double hz = 0.1 + (double)n * (double)n * (double)n * 20.0;
    const double lfo_step = P800_TAU * hz / core->sample_rate;
    core->lfo_sin_step = (float)sin(lfo_step);
    core->lfo_cos_step = (float)cos(lfo_step);

    const double chorus_step = P800_TAU * 0.33 / core->sample_rate;
    core->chorus.sin_step = (float)sin(chorus_step);
    core->chorus.cos_step = (float)cos(chorus_step);

    const int pitch_changed = !old
        || old->tune_cents != core->params.tune_cents
        || old->dco1_octave != core->params.dco1_octave
        || old->dco2_octave != core->params.dco2_octave
        || old->dco2_interval != core->params.dco2_interval
        || old->dco2_detune != core->params.dco2_detune;

    if (pitch_changed)
        refresh_all_voice_increments(core);
}

Poly800Core* poly800_core_create(double sample_rate)
{
    if (!(sample_rate > 1000.0))
        return NULL;

    Poly800Core* core = (Poly800Core*)calloc(1, sizeof(*core));
    if (!core)
        return NULL;

    core->sample_rate = sample_rate;
    core->inv_sample_rate = 1.0 / sample_rate;

    const double semitone_ratio = exp2(1.0 / 12.0);
    core->note_freq[69] = 440.0;
    for (int note = 70; note < 128; ++note)
        core->note_freq[note] =
            core->note_freq[note - 1] * semitone_ratio;
    for (int note = 68; note >= 0; --note)
        core->note_freq[note] =
            core->note_freq[note + 1] / semitone_ratio;

    poly800_params_default(&core->params);
    core->last_mode = core->params.dco_mode;

    core->chorus.size = (uint32_t)ceil(sample_rate * 0.050) + 8u;
    core->chorus.delay =
        (float*)calloc(core->chorus.size, sizeof(float));
    if (!core->chorus.delay) {
        free(core);
        return NULL;
    }

    rebuild_derived(core, NULL);
    poly800_core_reset(core);
    return core;
}

void poly800_core_destroy(Poly800Core* core)
{
    if (!core)
        return;
    free(core->chorus.delay);
    free(core);
}

void poly800_core_reset(Poly800Core* core)
{
    if (!core)
        return;

    memset(core->voices, 0, sizeof(core->voices));
    memset(core->held_notes, 0, sizeof(core->held_notes));
    memset(&core->filter, 0, sizeof(core->filter));
    env_reset(&core->deg3);

    core->filter.noise1 = 0x67452301u;
    core->filter.noise2 = 0xefcdab89u;
    core->age_counter = 1;
    core->held_count = 0;

    core->lfo_sin = 0.0f;
    core->lfo_cos = 1.0f;
    core->lfo_delay_remaining = 0.0;
    core->lfo_gain = 0.0f;

    core->noise_state = 0x12345678u;

    if (core->chorus.delay)
        memset(core->chorus.delay, 0,
               core->chorus.size * sizeof(float));
    core->chorus.write_pos = 0;
    core->chorus.sin_phase = 0.0f;
    core->chorus.cos_phase = 1.0f;
}

void poly800_core_set_params(
    Poly800Core* core, const Poly800Params* params)
{
    if (!core || !params)
        return;

    Poly800Params copy = *params;
    clamp_params(&copy);

    /* LV2 presents the same controls every block; do no work when unchanged. */
    if (memcmp(&copy, &core->params, sizeof(copy)) == 0)
        return;

    const Poly800Params old = core->params;
    core->params = copy;

    if (copy.dco_mode != core->last_mode) {
        core->last_mode = copy.dco_mode;
        rebuild_derived(core, &old);
        poly800_core_reset(core);
    } else {
        rebuild_derived(core, &old);
    }
}

void poly800_core_note_on(
    Poly800Core* core, uint8_t note, uint8_t velocity)
{
    (void)velocity;
    if (!core)
        return;

    const unsigned was_held = core->held_count;
    if (!core->held_notes[note]) {
        core->held_notes[note] = 1;
        ++core->held_count;
    }

    Voice* voice = find_voice_for_note_on(core, note);
    memset(voice->phase1, 0, sizeof(voice->phase1));
    memset(voice->phase2, 0, sizeof(voice->phase2));
    voice->active = 1;
    voice->gate = 1;
    voice->note = note;
    voice->age = ++core->age_counter;
    refresh_voice_increments(core, voice);
    env_trigger(&voice->deg1);
    env_trigger(&voice->deg2);

    if (was_held == 0 || core->params.vcf_trigger == 2)
        env_trigger(&core->deg3);

    if (was_held == 0) {
        const double delay = (double)core->params.mg_delay;
        core->lfo_delay_remaining = delay * core->sample_rate;
        core->lfo_gain = delay > 0.0 ? 0.0f : 1.0f;
    }
}

void poly800_core_note_off(Poly800Core* core, uint8_t note)
{
    if (!core)
        return;

    if (core->held_notes[note]) {
        core->held_notes[note] = 0;
        if (core->held_count > 0)
            --core->held_count;
    }

    const int limit = voice_limit(core);
    for (int i = 0; i < limit; ++i) {
        Voice* voice = &core->voices[i];
        if (voice->active && voice->note == note) {
            voice->gate = 0;
            env_release(&voice->deg1);
            env_release(&voice->deg2);
        }
    }

    if (core->held_count == 0)
        env_release(&core->deg3);
}

void poly800_core_all_notes_off(Poly800Core* core)
{
    if (!core)
        return;

    memset(core->held_notes, 0, sizeof(core->held_notes));
    core->held_count = 0;

    for (unsigned i = 0; i < P800_MAX_VOICES; ++i) {
        if (core->voices[i].active) {
            core->voices[i].gate = 0;
            env_release(&core->voices[i].deg1);
            env_release(&core->voices[i].deg2);
        }
    }
    env_release(&core->deg3);
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
        const float lfo = lfo_tick(core);
        const float pitch_ratio =
            fast_pitch_ratio(lfo * core->derived.lfo_pitch_semitones);

        float oscillator_bus = 0.0f;
        int highest_died = 0;

        for (int i = 0; i < limit; ++i) {
            Voice* voice = &core->voices[i];
            if (!voice->active)
                continue;

            const float env1 =
                env_tick(&voice->deg1, &core->derived.deg1);
            oscillator_bus += render_dco(
                voice->phase1, voice->inc1,
                core->derived.dco1_mask,
                core->derived.dco1_waveform,
                pitch_ratio)
                * env1 * core->derived.dco1_level;

            if (double_mode) {
                const float env2 =
                    env_tick(&voice->deg2, &core->derived.deg2);
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

        const float env3 =
            env_tick(&core->deg3, &core->derived.deg3);

        /*
         * Bristol multiplies the completed oscillator outbuf by eight before
         * DEG3-controlled noise and the shared filter are applied.
         */
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
        chorus_tick(core, filtered, &out_l, &out_r);

        /*
         * Master gain is an ISLA convenience control rather than a stock
         * program parameter. Keep it linear so the calibrated filter is not
         * followed by an unrelated waveshaper.
         */
        left[frame] += out_l * core->params.master_gain;
        right[frame] += out_r * core->params.master_gain;
    }

    normalise_quadrature(&core->lfo_sin, &core->lfo_cos);
    normalise_quadrature(
        &core->chorus.sin_phase, &core->chorus.cos_phase);
}
