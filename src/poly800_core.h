// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ISLA_POLY800_CORE_H
#define ISLA_POLY800_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Poly800Core Poly800Core;

typedef struct {
    float master_gain;       /* 0..1, ISLA convenience control */
    float tune_cents;        /* -100..100, ISLA convenience control */

    int dco1_octave;         /* P11: 1 low, 2 middle, 3 high */
    int dco1_waveform;       /* P12: 1 square, 2 saw/step weighting */
    int dco1_h16;            /* P13: 0/1 */
    int dco1_h8;             /* P14: 0/1 */
    int dco1_h4;             /* P15: 0/1 */
    int dco1_h2;             /* P16: 0/1 */
    float dco1_level;        /* P17: 0..31 */
    int dco_mode;            /* P18: 1 WHOLE/8 voices, 2 DOUBLE/4 voices */

    int dco2_octave;         /* P21: 1..3 */
    int dco2_waveform;       /* P22: 1..2 */
    int dco2_h16;            /* P23: 0/1 */
    int dco2_h8;             /* P24: 0/1 */
    int dco2_h4;             /* P25: 0/1 */
    int dco2_h2;             /* P26: 0/1 */
    float dco2_level;        /* P27: 0..31 */
    int dco2_interval;       /* P31: 0..12 semitones */
    float dco2_detune;       /* P32: 0..3 */

    float noise_level;       /* P33: 0..15 */

    float vcf_cutoff;        /* P41: 0..99 */
    float vcf_resonance;     /* P42: 0..15 */
    int vcf_keytrack;        /* P43: 0 off, 1 half, 2 full */
    int vcf_polarity;        /* P44: 1 inverted, 2 normal */
    float vcf_env_intensity; /* P45: 0..15 */
    int vcf_trigger;         /* P46: 1 single, 2 multi */
    int chorus_on;           /* P48: 0/1 */

    float deg1_attack;       /* P51: 0..31 */
    float deg1_decay;        /* P52: 0..31 */
    float deg1_breakpoint;   /* P53: 0..31 */
    float deg1_slope;        /* P54: 0..31 */
    float deg1_sustain;      /* P55: 0..31 */
    float deg1_release;      /* P56: 0..31 */

    float deg2_attack;       /* P61: 0..31 */
    float deg2_decay;        /* P62: 0..31 */
    float deg2_breakpoint;   /* P63: 0..31 */
    float deg2_slope;        /* P64: 0..31 */
    float deg2_sustain;      /* P65: 0..31 */
    float deg2_release;      /* P66: 0..31 */

    float deg3_attack;       /* P71: 0..31 */
    float deg3_decay;        /* P72: 0..31 */
    float deg3_breakpoint;   /* P73: 0..31 */
    float deg3_slope;        /* P74: 0..31 */
    float deg3_sustain;      /* P75: 0..31 */
    float deg3_release;      /* P76: 0..31 */

    float mg_frequency;      /* P81: 0..15 */
    float mg_delay;          /* P82: 0..15 */
    float mg_dco;            /* P83: 0..15 */
    float mg_vcf;            /* P84: 0..15 */
} Poly800Params;

void poly800_params_default(Poly800Params* params);
Poly800Core* poly800_core_create(double sample_rate);
void poly800_core_destroy(Poly800Core* core);
void poly800_core_reset(Poly800Core* core);
void poly800_core_set_params(Poly800Core* core, const Poly800Params* params);
void poly800_core_note_on(Poly800Core* core, uint8_t note, uint8_t velocity);
void poly800_core_note_off(Poly800Core* core, uint8_t note);
void poly800_core_all_notes_off(Poly800Core* core);
void poly800_core_render(Poly800Core* core, float* left, float* right, uint32_t nframes);

#ifdef __cplusplus
}
#endif

#endif
