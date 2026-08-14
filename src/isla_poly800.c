// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ISLA Poly-800 — headless LV2 wrapper.
 *
 * M1 moves synthesis into poly800_core.c. This file contains only host glue:
 * LV2 ports, sample-accurate MIDI event timing and parameter transfer.
 */

#include "poly800_core.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

#define ISLA_POLY800_URI "https://interspock.github.io/isla-poly800"

typedef enum {
    PORT_MIDI_IN = 0,
    PORT_AUDIO_L,
    PORT_AUDIO_R,
    PORT_MASTER_GAIN,
    PORT_TUNE,

    PORT_P11_DCO1_OCTAVE,
    PORT_P12_DCO1_WAVEFORM,
    PORT_P13_DCO1_H16,
    PORT_P14_DCO1_H8,
    PORT_P15_DCO1_H4,
    PORT_P16_DCO1_H2,
    PORT_P17_DCO1_LEVEL,
    PORT_P18_DCO_MODE,

    PORT_P21_DCO2_OCTAVE,
    PORT_P22_DCO2_WAVEFORM,
    PORT_P23_DCO2_H16,
    PORT_P24_DCO2_H8,
    PORT_P25_DCO2_H4,
    PORT_P26_DCO2_H2,
    PORT_P27_DCO2_LEVEL,

    PORT_P31_DCO2_INTERVAL,
    PORT_P32_DCO2_DETUNE,
    PORT_P33_NOISE_LEVEL,

    PORT_P41_VCF_CUTOFF,
    PORT_P42_VCF_RESONANCE,
    PORT_P43_VCF_KEYTRACK,
    PORT_P44_VCF_POLARITY,
    PORT_P45_VCF_ENV_INTENSITY,
    PORT_P46_VCF_TRIGGER,
    PORT_P48_CHORUS,

    PORT_P51_DEG1_ATTACK,
    PORT_P52_DEG1_DECAY,
    PORT_P53_DEG1_BREAKPOINT,
    PORT_P54_DEG1_SLOPE,
    PORT_P55_DEG1_SUSTAIN,
    PORT_P56_DEG1_RELEASE,

    PORT_P61_DEG2_ATTACK,
    PORT_P62_DEG2_DECAY,
    PORT_P63_DEG2_BREAKPOINT,
    PORT_P64_DEG2_SLOPE,
    PORT_P65_DEG2_SUSTAIN,
    PORT_P66_DEG2_RELEASE,

    PORT_P71_DEG3_ATTACK,
    PORT_P72_DEG3_DECAY,
    PORT_P73_DEG3_BREAKPOINT,
    PORT_P74_DEG3_SLOPE,
    PORT_P75_DEG3_SUSTAIN,
    PORT_P76_DEG3_RELEASE,

    PORT_P81_MG_FREQUENCY,
    PORT_P82_MG_DELAY,
    PORT_P83_MG_DCO,
    PORT_P84_MG_VCF,

    PORT_COUNT
} PortIndex;

typedef struct {
    const void* ports[PORT_COUNT];
    LV2_URID midi_event;
    Poly800Core* core;
} IslaPoly800;

static float
control(const IslaPoly800* self, PortIndex port, float fallback)
{
    const float* value = (const float*)self->ports[port];
    return value ? *value : fallback;
}

static int
control_int(const IslaPoly800* self, PortIndex port, int fallback)
{
    return (int)lrintf(control(self, port, (float)fallback));
}

static void
sync_params(IslaPoly800* self)
{
    Poly800Params p;
    poly800_params_default(&p);

    p.master_gain = control(self, PORT_MASTER_GAIN, p.master_gain);
    p.tune_cents = control(self, PORT_TUNE, p.tune_cents);

    p.dco1_octave = control_int(self, PORT_P11_DCO1_OCTAVE, p.dco1_octave);
    p.dco1_waveform = control_int(self, PORT_P12_DCO1_WAVEFORM, p.dco1_waveform);
    p.dco1_h16 = control_int(self, PORT_P13_DCO1_H16, p.dco1_h16);
    p.dco1_h8 = control_int(self, PORT_P14_DCO1_H8, p.dco1_h8);
    p.dco1_h4 = control_int(self, PORT_P15_DCO1_H4, p.dco1_h4);
    p.dco1_h2 = control_int(self, PORT_P16_DCO1_H2, p.dco1_h2);
    p.dco1_level = control(self, PORT_P17_DCO1_LEVEL, p.dco1_level);
    p.dco_mode = control_int(self, PORT_P18_DCO_MODE, p.dco_mode);

    p.dco2_octave = control_int(self, PORT_P21_DCO2_OCTAVE, p.dco2_octave);
    p.dco2_waveform = control_int(self, PORT_P22_DCO2_WAVEFORM, p.dco2_waveform);
    p.dco2_h16 = control_int(self, PORT_P23_DCO2_H16, p.dco2_h16);
    p.dco2_h8 = control_int(self, PORT_P24_DCO2_H8, p.dco2_h8);
    p.dco2_h4 = control_int(self, PORT_P25_DCO2_H4, p.dco2_h4);
    p.dco2_h2 = control_int(self, PORT_P26_DCO2_H2, p.dco2_h2);
    p.dco2_level = control(self, PORT_P27_DCO2_LEVEL, p.dco2_level);
    p.dco2_interval = control_int(self, PORT_P31_DCO2_INTERVAL, p.dco2_interval);
    p.dco2_detune = control(self, PORT_P32_DCO2_DETUNE, p.dco2_detune);
    p.noise_level = control(self, PORT_P33_NOISE_LEVEL, p.noise_level);

    p.vcf_cutoff = control(self, PORT_P41_VCF_CUTOFF, p.vcf_cutoff);
    p.vcf_resonance = control(self, PORT_P42_VCF_RESONANCE, p.vcf_resonance);
    p.vcf_keytrack = control_int(self, PORT_P43_VCF_KEYTRACK, p.vcf_keytrack);
    p.vcf_polarity = control_int(self, PORT_P44_VCF_POLARITY, p.vcf_polarity);
    p.vcf_env_intensity = control(self, PORT_P45_VCF_ENV_INTENSITY, p.vcf_env_intensity);
    p.vcf_trigger = control_int(self, PORT_P46_VCF_TRIGGER, p.vcf_trigger);
    p.chorus_on = control_int(self, PORT_P48_CHORUS, p.chorus_on);

    p.deg1_attack = control(self, PORT_P51_DEG1_ATTACK, p.deg1_attack);
    p.deg1_decay = control(self, PORT_P52_DEG1_DECAY, p.deg1_decay);
    p.deg1_breakpoint = control(self, PORT_P53_DEG1_BREAKPOINT, p.deg1_breakpoint);
    p.deg1_slope = control(self, PORT_P54_DEG1_SLOPE, p.deg1_slope);
    p.deg1_sustain = control(self, PORT_P55_DEG1_SUSTAIN, p.deg1_sustain);
    p.deg1_release = control(self, PORT_P56_DEG1_RELEASE, p.deg1_release);

    p.deg2_attack = control(self, PORT_P61_DEG2_ATTACK, p.deg2_attack);
    p.deg2_decay = control(self, PORT_P62_DEG2_DECAY, p.deg2_decay);
    p.deg2_breakpoint = control(self, PORT_P63_DEG2_BREAKPOINT, p.deg2_breakpoint);
    p.deg2_slope = control(self, PORT_P64_DEG2_SLOPE, p.deg2_slope);
    p.deg2_sustain = control(self, PORT_P65_DEG2_SUSTAIN, p.deg2_sustain);
    p.deg2_release = control(self, PORT_P66_DEG2_RELEASE, p.deg2_release);

    p.deg3_attack = control(self, PORT_P71_DEG3_ATTACK, p.deg3_attack);
    p.deg3_decay = control(self, PORT_P72_DEG3_DECAY, p.deg3_decay);
    p.deg3_breakpoint = control(self, PORT_P73_DEG3_BREAKPOINT, p.deg3_breakpoint);
    p.deg3_slope = control(self, PORT_P74_DEG3_SLOPE, p.deg3_slope);
    p.deg3_sustain = control(self, PORT_P75_DEG3_SUSTAIN, p.deg3_sustain);
    p.deg3_release = control(self, PORT_P76_DEG3_RELEASE, p.deg3_release);

    p.mg_frequency = control(self, PORT_P81_MG_FREQUENCY, p.mg_frequency);
    p.mg_delay = control(self, PORT_P82_MG_DELAY, p.mg_delay);
    p.mg_dco = control(self, PORT_P83_MG_DCO, p.mg_dco);
    p.mg_vcf = control(self, PORT_P84_MG_VCF, p.mg_vcf);

    poly800_core_set_params(self->core, &p);
}

static void
handle_midi(IslaPoly800* self, const uint8_t* msg, uint32_t size)
{
    if (!msg || size == 0) {
        return;
    }

    const uint8_t status = msg[0] & 0xF0u;
    if ((status == LV2_MIDI_MSG_NOTE_ON || status == LV2_MIDI_MSG_NOTE_OFF) && size >= 3) {
        const uint8_t note = msg[1] & 0x7Fu;
        const uint8_t velocity = msg[2] & 0x7Fu;
        if (status == LV2_MIDI_MSG_NOTE_ON && velocity != 0) {
            poly800_core_note_on(self->core, note, velocity);
        } else {
            poly800_core_note_off(self->core, note);
        }
        return;
    }

    if (status == 0xB0u && size >= 3) {
        const uint8_t cc = msg[1] & 0x7Fu;
        if (cc == 120 || cc == 123) {
            poly800_core_all_notes_off(self->core);
        }
    }
}

static void
render_range(IslaPoly800* self, uint32_t from, uint32_t to)
{
    if (to <= from) {
        return;
    }
    float* left = (float*)self->ports[PORT_AUDIO_L];
    float* right = (float*)self->ports[PORT_AUDIO_R];
    if (left && right) {
        poly800_core_render(self->core, left + from, right + from, to - from);
    }
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double rate,
            const char* bundle_path,
            const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle_path;

    const LV2_URID_Map* map = NULL;
    for (const LV2_Feature* const* f = features; f && *f; ++f) {
        if (!strcmp((*f)->URI, LV2_URID__map)) {
            map = (const LV2_URID_Map*)(*f)->data;
            break;
        }
    }
    if (!map) {
        return NULL;
    }

    IslaPoly800* self = (IslaPoly800*)calloc(1, sizeof(*self));
    if (!self) {
        return NULL;
    }

    self->core = poly800_core_create(rate);
    if (!self->core) {
        free(self);
        return NULL;
    }
    self->midi_event = map->map(map->handle, LV2_MIDI__MidiEvent);
    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    IslaPoly800* self = (IslaPoly800*)instance;
    if (port < PORT_COUNT) {
        self->ports[port] = data;
    }
}

static void
activate(LV2_Handle instance)
{
    IslaPoly800* self = (IslaPoly800*)instance;
    poly800_core_reset(self->core);
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    IslaPoly800* self = (IslaPoly800*)instance;
    float* left = (float*)self->ports[PORT_AUDIO_L];
    float* right = (float*)self->ports[PORT_AUDIO_R];
    if (!left || !right) {
        return;
    }

    memset(left, 0, n_samples * sizeof(float));
    memset(right, 0, n_samples * sizeof(float));
    sync_params(self);

    const LV2_Atom_Sequence* midi = (const LV2_Atom_Sequence*)self->ports[PORT_MIDI_IN];
    if (!midi) {
        render_range(self, 0, n_samples);
        return;
    }

    uint32_t cursor = 0;
    LV2_ATOM_SEQUENCE_FOREACH(midi, event) {
        uint32_t frame = event->time.frames > 0 ? (uint32_t)event->time.frames : 0u;
        if (frame > n_samples) {
            frame = n_samples;
        }
        if (frame > cursor) {
            render_range(self, cursor, frame);
            cursor = frame;
        }
        if (event->body.type == self->midi_event) {
            const uint8_t* msg = (const uint8_t*)LV2_ATOM_BODY(&event->body);
            handle_midi(self, msg, event->body.size);
        }
    }

    if (cursor < n_samples) {
        render_range(self, cursor, n_samples);
    }
}

static void
deactivate(LV2_Handle instance)
{
    IslaPoly800* self = (IslaPoly800*)instance;
    poly800_core_all_notes_off(self->core);
}

static void
cleanup(LV2_Handle instance)
{
    IslaPoly800* self = (IslaPoly800*)instance;
    if (self) {
        poly800_core_destroy(self->core);
        free(self);
    }
}

static const void*
extension_data(const char* uri)
{
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    ISLA_POLY800_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
