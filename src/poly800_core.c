// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 DSP core.
 *
 * This is a per-instance extraction of the Poly-800 signal architecture for
 * use by the LV2 wrapper. The envelope rate law, shared-filter routing and
 * Chamberlin-style filter are adapted from Bristol's GPL-3.0-or-later
 * implementation. Portions adapted from Bristol are Copyright (c)
 * Nick Copeland <nickycopeland@hotmail.com> 1996,2012, notably
 * bristolpoly800.c, env5stage.c and filter.c.
 *
 * The state model, voice allocator, oscillator implementation and public core
 * API are project-original. M1 targets architectural/behavioural parity; exact
 * DCO detune, modulation depth, filter scaling and chorus constants remain
 * calibration work for later milestones.
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
#define P800_VCF_FREQ_MAX 0.825f

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
    uint8_t active;
    uint8_t gate;
    uint8_t note;
    uint64_t age;
    double phase1[P800_HARMONICS];
    double phase2[P800_HARMONICS];
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
    double phase;
} Chorus;

struct Poly800Core {
    double sample_rate;
    Poly800Params params;
    int last_mode;

    Voice voices[P800_MAX_VOICES];
    uint64_t age_counter;
    uint8_t held_notes[128];
    unsigned held_count;

    Envelope deg3;
    SharedFilter filter;

    double lfo_phase;
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

static double
note_frequency(double note)
{
    return 440.0 * pow(2.0, (note - 69.0) / 12.0);
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

/*
 * Bristol env5stage.c maps normalised rate to approximately
 * 1 / (rate^2 * samplerate * 10), capped at a 0.5/sample step.
 * The Poly-800 exposes 0..31 with zero fastest and 31 slowest.
 */
static float
env_rate_step(const Poly800Core* core, float raw)
{
    const float n = clampf(raw, 0.0f, 31.0f) / 31.0f;
    if (n <= 0.0f) {
        return 0.5f;
    }
    return clampf((float)(1.0 / ((double)n * (double)n * core->sample_rate * 10.0)),
                  0.0f, 0.5f);
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
env_tick(Poly800Core* core, Envelope* env,
         float attack, float decay, float breakpoint,
         float slope, float sustain, float release)
{
    const float bp = clampf(breakpoint, 0.0f, 31.0f) / 31.0f;
    const float sus = clampf(sustain, 0.0f, 31.0f) / 31.0f;

    switch (env->stage) {
    case ENV_ATTACK:
        env_move(env, 1.0f, env_rate_step(core, attack), ENV_DECAY);
        break;
    case ENV_DECAY:
        env_move(env, bp, env_rate_step(core, decay), ENV_SLOPE);
        break;
    case ENV_SLOPE:
        env_move(env, sus, env_rate_step(core, slope), ENV_SUSTAIN);
        break;
    case ENV_SUSTAIN:
        env->value = sus;
        break;
    case ENV_RELEASE:
        env_move(env, 0.0f, env_rate_step(core, release), ENV_OFF);
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
square_sample(double* phase, double frequency, double sample_rate)
{
    double inc = frequency / sample_rate;
    if (inc <= 0.0) {
        return 0.0f;
    }
    if (inc > 0.49) {
        inc = 0.49;
    }

    const float p = (float)*phase;
    float y = p < 0.5f ? 1.0f : -1.0f;
    y += poly_blep(p, (float)inc);

    float shifted = p + 0.5f;
    if (shifted >= 1.0f) {
        shifted -= 1.0f;
    }
    y -= poly_blep(shifted, (float)inc);

    *phase += inc;
    if (*phase >= 1.0) {
        *phase -= floor(*phase);
    }
    return y;
}

static float
render_dco(Poly800Core* core,
           double phase[P800_HARMONICS],
           uint8_t note, int octave, int waveform,
           int h16, int h8, int h4, int h2,
           double extra_semitones, double pitch_ratio)
{
    static const double footage[P800_HARMONICS] = {-12.0, 0.0, 12.0, 24.0};
    static const float step_weights[P800_HARMONICS] = {1.0f, 0.5f, 0.25f, 0.125f};
    const int enabled[P800_HARMONICS] = {h16, h8, h4, h2};
    const double octave_shift = (double)(clampi(octave, 1, 3) - 2) * 12.0;
    const double global_tune = (double)core->params.tune_cents / 100.0;
    float out = 0.0f;
    float weights = 0.0f;

    for (unsigned i = 0; i < P800_HARMONICS; ++i) {
        if (!enabled[i]) {
            continue;
        }
        const float weight = waveform == 2 ? step_weights[i] : 1.0f;
        const double midi_note = (double)note + octave_shift + footage[i]
                               + extra_semitones + global_tune;
        const double frequency = note_frequency(midi_note) * pitch_ratio;
        out += square_sample(&phase[i], frequency, core->sample_rate) * weight;
        weights += weight;
    }
    return weights > 0.0f ? out / weights : 0.0f;
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
    const uint32_t v = xorshift32(&core->noise_state);
    return ((float)(v >> 8) / 8388607.5f) - 1.0f;
}

static int
voice_limit(const Poly800Core* core)
{
    return core->params.dco_mode == 2 ? 4 : 8;
}

static Voice*
find_voice_for_note_on(Poly800Core* core, uint8_t note)
{
    const int limit = voice_limit(core);
    Voice* free_voice = NULL;
    Voice* oldest = &core->voices[0];

    for (int i = 0; i < limit; ++i) {
        Voice* v = &core->voices[i];
        if (v->active && v->note == note) {
            return v;
        }
        if (!v->active && !free_voice) {
            free_voice = v;
        }
        if (v->age < oldest->age) {
            oldest = v;
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
            const Voice* v = &core->voices[i];
            if (!v->active || (pass == 0 && !v->gate)) {
                continue;
            }
            if (!found || v->note > highest) {
                highest = v->note;
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
shared_filter_tick(Poly800Core* core, float input, float modulation)
{
    SharedFilter* f = &core->filter;
    const float cutoff = clampf(core->params.vcf_cutoff, 0.0f, 99.0f) / 99.0f;
    const float resonance = clampf(core->params.vcf_resonance, 0.0f, 15.0f) / 15.0f;
    const float keytrack = (float)clampi(core->params.vcf_keytrack, 0, 2) * 0.5f;
    const float key = (float)highest_sounding_note(core);

    /* Adapted from Bristol's default Chamberlin filter path. */
    float freqcut = (cutoff + keytrack * key / 512.0f) * 2.0f + modulation / 12.0f;
    freqcut = clampf(freqcut, 0.000001f, P800_VCF_FREQ_MAX);
    const float qres = 2.0f - resonance * 1.97f;

    const float in = tanhf(input * 0.75f);
    f->delay2 += freqcut * f->delay1;
    float highpass = in - f->delay2 - qres * f->delay1;
    f->delay1 += freqcut * highpass;

    f->delay4 += freqcut * f->delay3;
    highpass = f->delay2 - f->delay4 - qres * f->delay3;
    f->delay3 += freqcut * highpass;

    if (!isfinite(f->delay1) || !isfinite(f->delay2) ||
        !isfinite(f->delay3) || !isfinite(f->delay4)) {
        memset(f, 0, sizeof(*f));
        return 0.0f;
    }

    f->delay1 = clampf(f->delay1, -24.0f, 24.0f);
    f->delay2 = clampf(f->delay2, -24.0f, 24.0f);
    f->delay3 = clampf(f->delay3, -24.0f, 24.0f);
    f->delay4 = clampf(f->delay4, -24.0f, 24.0f);
    return tanhf(f->delay4);
}

static float
chorus_read(const Chorus* chorus, double delay_samples)
{
    if (!chorus->delay || chorus->size < 4) {
        return 0.0f;
    }
    double read = (double)chorus->write_pos - delay_samples;
    while (read < 0.0) {
        read += (double)chorus->size;
    }
    while (read >= (double)chorus->size) {
        read -= (double)chorus->size;
    }
    const uint32_t i0 = (uint32_t)read;
    const uint32_t i1 = (i0 + 1u) % chorus->size;
    const float frac = (float)(read - (double)i0);
    return chorus->delay[i0] + (chorus->delay[i1] - chorus->delay[i0]) * frac;
}

static void
chorus_tick(Poly800Core* core, float mono, float* left, float* right)
{
    Chorus* c = &core->chorus;
    if (!c->delay || c->size < 4) {
        *left = mono;
        *right = mono;
        return;
    }

    c->delay[c->write_pos] = mono;
    if (!core->params.chorus_on) {
        *left = mono;
        *right = mono;
    } else {
        const double base = core->sample_rate * 0.010;
        const double depth = core->sample_rate * 0.003;
        const double dl = base + depth * (0.5 + 0.5 * sin(c->phase));
        const double dr = base + depth * (0.5 + 0.5 * sin(c->phase + P800_PI * 0.5));
        *left = mono * 0.72f + chorus_read(c, dl) * 0.28f;
        *right = mono * 0.72f + chorus_read(c, dr) * 0.28f;
    }

    c->write_pos = (c->write_pos + 1u) % c->size;
    c->phase += P800_TAU * 0.33 / core->sample_rate;
    if (c->phase >= P800_TAU) {
        c->phase -= P800_TAU;
    }
}

static float
lfo_tick(Poly800Core* core)
{
    const float n = clampf(core->params.mg_frequency, 0.0f, 15.0f) / 15.0f;
    const double hz = 0.1 + (double)n * (double)n * (double)n * 20.0;
    const float raw = (float)sin(core->lfo_phase);

    core->lfo_phase += P800_TAU * hz / core->sample_rate;
    if (core->lfo_phase >= P800_TAU) {
        core->lfo_phase -= P800_TAU;
    }

    if (core->held_count == 0) {
        return 0.0f;
    }
    if (core->lfo_delay_remaining > 0.0) {
        core->lfo_delay_remaining -= 1.0;
        return 0.0f;
    }

    const double delay_seconds = (double)clampf(core->params.mg_delay, 0.0f, 15.0f);
    if (delay_seconds <= 0.0) {
        core->lfo_gain = 1.0f;
    } else if (core->lfo_gain < 1.0f) {
        core->lfo_gain += (float)(1.0 / (delay_seconds * core->sample_rate));
        core->lfo_gain = clampf(core->lfo_gain, 0.0f, 1.0f);
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
    poly800_params_default(&core->params);
    core->last_mode = core->params.dco_mode;

    core->chorus.size = (uint32_t)ceil(sample_rate * 0.050) + 8u;
    core->chorus.delay = (float*)calloc(core->chorus.size, sizeof(float));
    if (!core->chorus.delay) {
        free(core);
        return NULL;
    }

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
    core->lfo_phase = 0.0;
    core->lfo_delay_remaining = 0.0;
    core->lfo_gain = 0.0f;
    core->noise_state = 0x12345678u;
    if (core->chorus.delay) {
        memset(core->chorus.delay, 0, core->chorus.size * sizeof(float));
    }
    core->chorus.write_pos = 0;
    core->chorus.phase = 0.0;
}

void
poly800_core_set_params(Poly800Core* core, const Poly800Params* params)
{
    if (!core || !params) {
        return;
    }
    Poly800Params copy = *params;
    clamp_params(&copy);
    if (copy.dco_mode != core->last_mode) {
        core->params = copy;
        core->last_mode = copy.dco_mode;
        poly800_core_reset(core);
    } else {
        core->params = copy;
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
    env_trigger(&voice->deg1);
    env_trigger(&voice->deg2);

    if (was_held == 0 || core->params.vcf_trigger == 2) {
        env_trigger(&core->deg3);
    }
    if (was_held == 0) {
        const double delay = (double)clampf(core->params.mg_delay, 0.0f, 15.0f);
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
poly800_core_render(Poly800Core* core, float* left, float* right, uint32_t nframes)
{
    if (!core || !left || !right) {
        return;
    }

    const int limit = voice_limit(core);
    const int double_mode = core->params.dco_mode == 2;
    const float dco1_level = core->params.dco1_level / 31.0f;
    const float dco2_level = core->params.dco2_level / 31.0f;
    const float noise_level = core->params.noise_level / 15.0f;
    const float polarity = core->params.vcf_polarity == 1 ? -1.0f : 1.0f;
    const float env_amount = polarity * (core->params.vcf_env_intensity / 15.0f) * 2.0f;
    const float lfo_vcf_amount = powf(core->params.mg_vcf / 15.0f, 2.0f) * 8.0f;
    const float lfo_pitch_semitones = powf(core->params.mg_dco / 15.0f, 2.0f);
    const double dco2_extra = (double)core->params.dco2_interval
                            + ((double)core->params.dco2_detune / 3.0) * 0.5;

    for (uint32_t frame = 0; frame < nframes; ++frame) {
        const float lfo = lfo_tick(core);
        const double pitch_ratio = pow(2.0, (double)(lfo * lfo_pitch_semitones) / 12.0);
        float mixed = 0.0f;
        unsigned sounding = 0;

        for (int i = 0; i < limit; ++i) {
            Voice* voice = &core->voices[i];
            if (!voice->active) {
                continue;
            }

            const float env1 = env_tick(core, &voice->deg1,
                                        core->params.deg1_attack,
                                        core->params.deg1_decay,
                                        core->params.deg1_breakpoint,
                                        core->params.deg1_slope,
                                        core->params.deg1_sustain,
                                        core->params.deg1_release);
            mixed += render_dco(core, voice->phase1, voice->note,
                                core->params.dco1_octave,
                                core->params.dco1_waveform,
                                core->params.dco1_h16,
                                core->params.dco1_h8,
                                core->params.dco1_h4,
                                core->params.dco1_h2,
                                0.0, pitch_ratio) * env1 * dco1_level;

            if (double_mode) {
                const float env2 = env_tick(core, &voice->deg2,
                                            core->params.deg2_attack,
                                            core->params.deg2_decay,
                                            core->params.deg2_breakpoint,
                                            core->params.deg2_slope,
                                            core->params.deg2_sustain,
                                            core->params.deg2_release);
                mixed += render_dco(core, voice->phase2, voice->note,
                                    core->params.dco2_octave,
                                    core->params.dco2_waveform,
                                    core->params.dco2_h16,
                                    core->params.dco2_h8,
                                    core->params.dco2_h4,
                                    core->params.dco2_h2,
                                    dco2_extra, pitch_ratio) * env2 * dco2_level;
            }

            ++sounding;
            if (!voice->gate && voice->deg1.stage == ENV_OFF &&
                (!double_mode || voice->deg2.stage == ENV_OFF)) {
                voice->active = 0;
            }
        }

        const float env3 = env_tick(core, &core->deg3,
                                    core->params.deg3_attack,
                                    core->params.deg3_decay,
                                    core->params.deg3_breakpoint,
                                    core->params.deg3_slope,
                                    core->params.deg3_sustain,
                                    core->params.deg3_release);

        if (sounding > 0) {
            mixed *= 0.55f / sqrtf((float)sounding);
        }
        mixed += white_noise(core) * noise_level * env3 * 0.20f;

        const float modulation = env3 * env_amount + lfo * lfo_vcf_amount;
        const float filtered = shared_filter_tick(core, mixed, modulation);

        float l;
        float r;
        chorus_tick(core, filtered, &l, &r);
        left[frame] += tanhf(l * core->params.master_gain);
        right[frame] += tanhf(r * core->params.master_gain);
    }
}
