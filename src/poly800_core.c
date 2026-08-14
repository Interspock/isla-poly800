// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 DSP core — M1.1 performance pass.
 *
 * The Poly-800 signal architecture, five-stage/ADBSSR-style envelope rate
 * law, shared paraphonic filter routing and Chamberlin-style filter lineage
 * are adapted from Bristol's GPL-3.0-or-later implementation. Portions
 * adapted from Bristol are Copyright (c) Nick Copeland
 * <nickycopeland@hotmail.com> 1996,2012, notably bristolpoly800.c,
 * env5stage.c and filter.c.
 *
 * ISLA keeps all mutable state per instance. M1.1 moves pitch, envelope,
 * filter and modulation constants out of the per-sample/per-voice hot path,
 * while preserving the M1 architecture and parameter mapping.
 */

#include "poly800_core.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define P800_MAX_VOICES 8
#define P800_HARMONICS 4
#define P800_TAU 6.283185307179586476925286766559
#define P800_VCF_FREQ_MAX 0.825f
#define P800_LN2 0.69314718055994530942f

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
    float delay1;
    float delay2;
    float delay3;
    float delay4;
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
    float dco1_weight[P800_HARMONICS];
    float dco2_weight[P800_HARMONICS];
    float dco1_norm;
    float dco2_norm;
    double dco1_ratio;
    double dco2_ratio;
    float dco1_level;
    float dco2_level;
    float noise_level;

    float env_amount;
    float lfo_vcf_amount;
    float lfo_pitch_semitones;
    float lfo_gain_step;

    float filter_cutoff_base;
    float filter_key_coeff;
    float filter_qres;
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

static float
clampf(float value, float lo, float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static int
clampi(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static void
env_reset(Envelope* env)
{
    env->value = 0.0f;
    env->stage = ENV_OFF;
}

static void
env_trigger(Envelope* env)
{
    env->value = 0.0f;
    env->stage = ENV_ATTACK;
}

static void
env_release(Envelope* env)
{
    if (env->stage != ENV_OFF) {
        env->stage = ENV_RELEASE;
    }
}

/* Bristol env5stage.c rate law, calculated once when parameters change. */
static float
env_rate_step(const Poly800Core* core, float raw)
{
    const float n = clampf(raw, 0.0f, 31.0f) / 31.0f;
    if (n <= 0.0f) {
        return 0.5f;
    }
    return clampf((float)(1.0 / ((double)n * (double)n
                                 * core->sample_rate * 10.0)),
                  0.0f, 0.5f);
}

static EnvConfig
make_env_config(const Poly800Core* core,
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

static void
env_move(Envelope* env, float target, float step, EnvStage next)
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

static float
env_tick(Envelope* env, const EnvConfig* cfg)
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

static float
poly_blep(float t, float dt)
{
    if (dt <= 0.0f) {
        return 0.0f;
    }
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

static float
square_sample(float* phase, float increment)
{
    float inc = increment;
    if (inc <= 0.0f) {
        return 0.0f;
    }
    if (inc > 0.49f) {
        inc = 0.49f;
    }

    const float p = *phase;
    float out = p < 0.5f ? 1.0f : -1.0f;
    out += poly_blep(p, inc);

    float shifted = p + 0.5f;
    if (shifted >= 1.0f) {
        shifted -= 1.0f;
    }
    out -= poly_blep(shifted, inc);

    float next = p + inc;
    if (next >= 1.0f) {
        next -= 1.0f;
    }
    *phase = next;
    return out;
}

static uint8_t
harmonic_mask(int h16, int h8, int h4, int h2)
{
    return (uint8_t)((h16 ? 1u : 0u)
                   | (h8  ? 2u : 0u)
                   | (h4  ? 4u : 0u)
                   | (h2  ? 8u : 0u));
}

static void
dco_weights(int waveform, uint8_t mask,
            float weights[P800_HARMONICS], float* norm)
{
    static const float step[P800_HARMONICS] = {
        1.0f, 0.5f, 0.25f, 0.125f
    };
    float total = 0.0f;

    for (unsigned i = 0; i < P800_HARMONICS; ++i) {
        const float weight = (mask & (1u << i))
                           ? (waveform == 2 ? step[i] : 1.0f)
                           : 0.0f;
        weights[i] = weight;
        total += weight;
    }
    *norm = total > 0.0f ? 1.0f / total : 0.0f;
}

/*
 * The MG depth is at most +/-1 semitone. A cubic Taylor expansion of 2^x in
 * that range has relative error below 5e-7, avoiding exp/pow on every sample.
 */
static float
fast_pitch_ratio(float semitones)
{
    const float x = semitones * (1.0f / 12.0f);
    const float y = P800_LN2 * x;
    return 1.0f + y + 0.5f * y * y + (1.0f / 6.0f) * y * y * y;
}

static void
refresh_voice_increments(Poly800Core* core, Voice* voice)
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

static int
voice_limit(const Poly800Core* core)
{
    return core->params.dco_mode == 2 ? 4 : 8;
}

static void
refresh_all_voice_increments(Poly800Core* core)
{
    const int limit = voice_limit(core);
    for (int i = 0; i < limit; ++i) {
        if (core->voices[i].active) {
            refresh_voice_increments(core, &core->voices[i]);
        }
    }
}

static float
render_dco(float phase[P800_HARMONICS],
           const float increment[P800_HARMONICS],
           uint8_t mask,
           const float weights[P800_HARMONICS],
           float norm,
           float pitch_ratio)
{
    if (!mask) {
        return 0.0f;
    }

    float out = 0.0f;
    for (unsigned i = 0; i < P800_HARMONICS; ++i) {
        if (mask & (1u << i)) {
            out += square_sample(&phase[i], increment[i] * pitch_ratio)
                 * weights[i];
        }
    }
    return out * norm;
}

static uint32_t
xorshift32(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x6d2b79f5u;
    return *state;
}

static float
white_noise(Poly800Core* core)
{
    const uint32_t value = xorshift32(&core->noise_state);
    return ((float)(value >> 8) / 8388607.5f) - 1.0f;
}

static Voice*
find_voice_for_note_on(Poly800Core* core, uint8_t note)
{
    const int limit = voice_limit(core);
    Voice* free_voice = NULL;
    Voice* oldest = &core->voices[0];

    for (int i = 0; i < limit; ++i) {
        Voice* voice = &core->voices[i];
        if (voice->active && voice->note == note) {
            return voice;
        }
        if (!voice->active && !free_voice) {
            free_voice = voice;
        }
        if (voice->age < oldest->age) {
            oldest = voice;
        }
    }
    return free_voice ? free_voice : oldest;
}

static int
highest_sounding_note(const Poly800Core* core)
{
    const int limit = voice_limit(core);
    int highest = 0;
    int found = 0;

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < limit; ++i) {
            const Voice* voice = &core->voices[i];
            if (!voice->active || (pass == 0 && !voice->gate)) {
                continue;
            }
            if (!found || voice->note > highest) {
                highest = voice->note;
                found = 1;
            }
        }
        if (found) {
            break;
        }
    }
    return found ? highest : 0;
}

static float
shared_filter_tick(Poly800Core* core,
                   float input, float modulation, float key_offset)
{
    SharedFilter* filter = &core->filter;
    float freqcut = core->derived.filter_cutoff_base
                  + key_offset
                  + modulation * (1.0f / 12.0f);
    freqcut = clampf(freqcut, 0.000001f, P800_VCF_FREQ_MAX);

    /* Adapted from Bristol's default Chamberlin filter path. */
    const float in = tanhf(input * 0.75f);
    filter->delay2 += freqcut * filter->delay1;
    float highpass = in - filter->delay2
                   - core->derived.filter_qres * filter->delay1;
    filter->delay1 += freqcut * highpass;

    filter->delay4 += freqcut * filter->delay3;
    highpass = filter->delay2 - filter->delay4
             - core->derived.filter_qres * filter->delay3;
    filter->delay3 += freqcut * highpass;

    if (!isfinite(filter->delay1) || !isfinite(filter->delay2)
        || !isfinite(filter->delay3) || !isfinite(filter->delay4)) {
        memset(filter, 0, sizeof(*filter));
        return 0.0f;
    }

    filter->delay1 = clampf(filter->delay1, -24.0f, 24.0f);
    filter->delay2 = clampf(filter->delay2, -24.0f, 24.0f);
    filter->delay3 = clampf(filter->delay3, -24.0f, 24.0f);
    filter->delay4 = clampf(filter->delay4, -24.0f, 24.0f);
    return tanhf(filter->delay4);
}

static float
chorus_read(const Chorus* chorus, float delay_samples)
{
    float read = (float)chorus->write_pos - delay_samples;
    if (read < 0.0f) {
        read += (float)chorus->size;
    }

    const uint32_t i0 = (uint32_t)read;
    uint32_t i1 = i0 + 1u;
    if (i1 >= chorus->size) {
        i1 = 0;
    }
    const float frac = read - (float)i0;
    return chorus->delay[i0]
         + (chorus->delay[i1] - chorus->delay[i0]) * frac;
}

static void
advance_quadrature(float* sine, float* cosine,
                   float sin_step, float cos_step)
{
    const float next_sine = *sine * cos_step + *cosine * sin_step;
    const float next_cosine = *cosine * cos_step - *sine * sin_step;
    *sine = next_sine;
    *cosine = next_cosine;
}

static void
normalise_quadrature(float* sine, float* cosine)
{
    const float magnitude = sqrtf(*sine * *sine + *cosine * *cosine);
    if (magnitude > 0.0f && isfinite(magnitude)) {
        *sine /= magnitude;
        *cosine /= magnitude;
    } else {
        *sine = 0.0f;
        *cosine = 1.0f;
    }
}

static void
chorus_tick(Poly800Core* core, float mono, float* left, float* right)
{
    Chorus* chorus = &core->chorus;
    chorus->delay[chorus->write_pos] = mono;

    if (!core->params.chorus_on) {
        *left = mono;
        *right = mono;
    } else {
        const float base = (float)(core->sample_rate * 0.010);
        const float depth = (float)(core->sample_rate * 0.003);
        const float delay_l = base + depth * (0.5f + 0.5f * chorus->sin_phase);
        const float delay_r = base + depth * (0.5f + 0.5f * chorus->cos_phase);
        *left = mono * 0.72f + chorus_read(chorus, delay_l) * 0.28f;
        *right = mono * 0.72f + chorus_read(chorus, delay_r) * 0.28f;
    }

    if (++chorus->write_pos >= chorus->size) {
        chorus->write_pos = 0;
    }
    advance_quadrature(&chorus->sin_phase, &chorus->cos_phase,
                       chorus->sin_step, chorus->cos_step);
}

static float
lfo_tick(Poly800Core* core)
{
    const float raw = core->lfo_sin;
    advance_quadrature(&core->lfo_sin, &core->lfo_cos,
                       core->lfo_sin_step, core->lfo_cos_step);

    if (core->held_count == 0) {
        return 0.0f;
    }
    if (core->lfo_delay_remaining > 0.0) {
        core->lfo_delay_remaining -= 1.0;
        return 0.0f;
    }

    if (core->params.mg_delay <= 0.0f) {
        core->lfo_gain = 1.0f;
    } else if (core->lfo_gain < 1.0f) {
        core->lfo_gain += core->derived.lfo_gain_step;
        if (core->lfo_gain > 1.0f) {
            core->lfo_gain = 1.0f;
        }
    }
    return raw * core->lfo_gain;
}

void
poly800_params_default(Poly800Params* p)
{
    if (!p) {
        return;
    }
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

static void
clamp_params(Poly800Params* p)
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

static void
rebuild_derived(Poly800Core* core, const Poly800Params* old)
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
    dco_weights(core->params.dco1_waveform, d->dco1_mask,
                d->dco1_weight, &d->dco1_norm);
    dco_weights(core->params.dco2_waveform, d->dco2_mask,
                d->dco2_weight, &d->dco2_norm);

    const double tune_ratio = exp2((double)core->params.tune_cents / 1200.0);
    d->dco1_ratio = tune_ratio * ldexp(1.0, core->params.dco1_octave - 2);
    const double dco2_extra = (double)core->params.dco2_interval
                            + ((double)core->params.dco2_detune / 3.0) * 0.5;
    d->dco2_ratio = tune_ratio
                  * ldexp(1.0, core->params.dco2_octave - 2)
                  * exp2(dco2_extra / 12.0);

    d->dco1_level = core->params.dco1_level / 31.0f;
    d->dco2_level = core->params.dco2_level / 31.0f;
    d->noise_level = core->params.noise_level / 15.0f;

    const float polarity = core->params.vcf_polarity == 1 ? -1.0f : 1.0f;
    d->env_amount = polarity * (core->params.vcf_env_intensity / 15.0f) * 2.0f;
    const float mg_vcf = core->params.mg_vcf / 15.0f;
    const float mg_dco = core->params.mg_dco / 15.0f;
    d->lfo_vcf_amount = mg_vcf * mg_vcf * 8.0f;
    d->lfo_pitch_semitones = mg_dco * mg_dco;
    d->lfo_gain_step = core->params.mg_delay > 0.0f
        ? (float)(1.0 / (core->params.mg_delay * core->sample_rate))
        : 1.0f;

    d->filter_cutoff_base = (core->params.vcf_cutoff / 99.0f) * 2.0f;
    d->filter_key_coeff = ((float)core->params.vcf_keytrack * 0.5f)
                        * (2.0f / 512.0f);
    d->filter_qres = 2.0f - (core->params.vcf_resonance / 15.0f) * 1.97f;

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
    if (pitch_changed) {
        refresh_all_voice_increments(core);
    }
}

Poly800Core*
poly800_core_create(double sample_rate)
{
    if (!(sample_rate > 1000.0)) {
        return NULL;
    }

    Poly800Core* core = (Poly800Core*)calloc(1, sizeof(*core));
    if (!core) {
        return NULL;
    }
    core->sample_rate = sample_rate;
    core->inv_sample_rate = 1.0 / sample_rate;

    /* Build the equal-tempered MIDI table once per instance. */
    const double semitone_ratio = exp2(1.0 / 12.0);
    core->note_freq[69] = 440.0;
    for (int note = 70; note < 128; ++note) {
        core->note_freq[note] = core->note_freq[note - 1] * semitone_ratio;
    }
    for (int note = 68; note >= 0; --note) {
        core->note_freq[note] = core->note_freq[note + 1] / semitone_ratio;
    }

    poly800_params_default(&core->params);
    core->last_mode = core->params.dco_mode;

    core->chorus.size = (uint32_t)ceil(sample_rate * 0.050) + 8u;
    core->chorus.delay = (float*)calloc(core->chorus.size, sizeof(float));
    if (!core->chorus.delay) {
        free(core);
        return NULL;
    }

    rebuild_derived(core, NULL);
    poly800_core_reset(core);
    return core;
}

void
poly800_core_destroy(Poly800Core* core)
{
    if (core) {
        free(core->chorus.delay);
        free(core);
    }
}

void
poly800_core_reset(Poly800Core* core)
{
    if (!core) {
        return;
    }

    memset(core->voices, 0, sizeof(core->voices));
    memset(core->held_notes, 0, sizeof(core->held_notes));
    memset(&core->filter, 0, sizeof(core->filter));
    env_reset(&core->deg3);

    core->age_counter = 1;
    core->held_count = 0;
    core->lfo_sin = 0.0f;
    core->lfo_cos = 1.0f;
    core->lfo_delay_remaining = 0.0;
    core->lfo_gain = 0.0f;
    core->noise_state = 0x12345678u;

    if (core->chorus.delay) {
        memset(core->chorus.delay, 0, core->chorus.size * sizeof(float));
    }
    core->chorus.write_pos = 0;
    core->chorus.sin_phase = 0.0f;
    core->chorus.cos_phase = 1.0f;
}

void
poly800_core_set_params(Poly800Core* core, const Poly800Params* params)
{
    if (!core || !params) {
        return;
    }

    const Poly800Params old = core->params;
    Poly800Params copy = *params;
    clamp_params(&copy);
    core->params = copy;

    if (copy.dco_mode != core->last_mode) {
        core->last_mode = copy.dco_mode;
        rebuild_derived(core, &old);
        poly800_core_reset(core);
    } else {
        rebuild_derived(core, &old);
    }
}

void
poly800_core_note_on(Poly800Core* core, uint8_t note, uint8_t velocity)
{
    (void)velocity;
    if (!core) {
        return;
    }

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

    if (was_held == 0 || core->params.vcf_trigger == 2) {
        env_trigger(&core->deg3);
    }
    if (was_held == 0) {
        const double delay = (double)core->params.mg_delay;
        core->lfo_delay_remaining = delay * core->sample_rate;
        core->lfo_gain = delay > 0.0 ? 0.0f : 1.0f;
    }
}

void
poly800_core_note_off(Poly800Core* core, uint8_t note)
{
    if (!core) {
        return;
    }

    if (core->held_notes[note]) {
        core->held_notes[note] = 0;
        if (core->held_count > 0) {
            --core->held_count;
        }
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
    if (core->held_count == 0) {
        env_release(&core->deg3);
    }
}

void
poly800_core_all_notes_off(Poly800Core* core)
{
    if (!core) {
        return;
    }

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

void
poly800_core_render(Poly800Core* core,
                    float* left, float* right, uint32_t nframes)
{
    if (!core || !left || !right) {
        return;
    }

    static const float voice_scale[9] = {
        0.0f,
        0.55000000f,
        0.38890873f,
        0.31754265f,
        0.27500000f,
        0.24596748f,
        0.22453656f,
        0.20788046f,
        0.19445436f
    };

    const int limit = voice_limit(core);
    const int double_mode = core->params.dco_mode == 2;
    int highest = highest_sounding_note(core);
    float key_offset = core->derived.filter_key_coeff * (float)highest;

    for (uint32_t frame = 0; frame < nframes; ++frame) {
        const float lfo = lfo_tick(core);
        const float pitch_ratio = fast_pitch_ratio(
            lfo * core->derived.lfo_pitch_semitones);
        float mixed = 0.0f;
        unsigned sounding = 0;
        int highest_died = 0;

        for (int i = 0; i < limit; ++i) {
            Voice* voice = &core->voices[i];
            if (!voice->active) {
                continue;
            }

            const float env1 = env_tick(&voice->deg1, &core->derived.deg1);
            mixed += render_dco(voice->phase1, voice->inc1,
                                core->derived.dco1_mask,
                                core->derived.dco1_weight,
                                core->derived.dco1_norm,
                                pitch_ratio)
                   * env1 * core->derived.dco1_level;

            if (double_mode) {
                const float env2 = env_tick(&voice->deg2, &core->derived.deg2);
                mixed += render_dco(voice->phase2, voice->inc2,
                                    core->derived.dco2_mask,
                                    core->derived.dco2_weight,
                                    core->derived.dco2_norm,
                                    pitch_ratio)
                       * env2 * core->derived.dco2_level;
            }

            ++sounding;
            if (!voice->gate && voice->deg1.stage == ENV_OFF
                && (!double_mode || voice->deg2.stage == ENV_OFF)) {
                if (voice->note == highest) {
                    highest_died = 1;
                }
                voice->active = 0;
                --sounding;
            }
        }

        if (highest_died) {
            highest = highest_sounding_note(core);
            key_offset = core->derived.filter_key_coeff * (float)highest;
        }

        const float env3 = env_tick(&core->deg3, &core->derived.deg3);
        if (sounding > 0) {
            mixed *= voice_scale[sounding];
        }
        if (core->derived.noise_level > 0.0f) {
            mixed += white_noise(core) * core->derived.noise_level
                   * env3 * 0.20f;
        }

        const float modulation = env3 * core->derived.env_amount
                               + lfo * core->derived.lfo_vcf_amount;
        const float filtered = shared_filter_tick(
            core, mixed, modulation, key_offset);

        float out_l;
        float out_r;
        chorus_tick(core, filtered, &out_l, &out_r);

        if (core->params.chorus_on) {
            left[frame] += tanhf(out_l * core->params.master_gain);
            right[frame] += tanhf(out_r * core->params.master_gain);
        } else {
            const float mono = tanhf(filtered * core->params.master_gain);
            left[frame] += mono;
            right[frame] += mono;
        }
    }

    /* Recurrence oscillators are cheap but slowly accumulate float error. */
    normalise_quadrature(&core->lfo_sin, &core->lfo_cos);
    normalise_quadrature(&core->chorus.sin_phase, &core->chorus.cos_phase);
}
