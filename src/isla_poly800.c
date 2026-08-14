// SPDX-License-Identifier: GPL-3.0-or-later
//
// ISLA Poly-800 — Milestone 0 LV2 smoke-test instrument.
// This file is project-original code. It is NOT yet a Poly-800 emulation.

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
#define MAX_VOICES 8
#define TAU 6.283185307179586476925286766559

typedef enum {
    PORT_MIDI_IN = 0,
    PORT_AUDIO_L  = 1,
    PORT_AUDIO_R  = 2,
    PORT_GAIN     = 3,
    PORT_TUNE     = 4
} PortIndex;

typedef struct {
    uint8_t active;
    uint8_t note;
    float velocity;
    double phase;
} Voice;

typedef struct {
    double sample_rate;
    const LV2_Atom_Sequence* midi_in;
    float* audio_l;
    float* audio_r;
    const float* gain;
    const float* tune;
    LV2_URID midi_event;
    Voice voices[MAX_VOICES];
} IslaPoly800;

static float
clampf(const float value, const float lo, const float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static double
note_frequency(const uint8_t note, const float tune_cents)
{
    const double semitones = (double)note - 69.0 + ((double)tune_cents / 100.0);
    return 440.0 * pow(2.0, semitones / 12.0);
}

static void
note_off(IslaPoly800* self, const uint8_t note)
{
    for (unsigned i = 0; i < MAX_VOICES; ++i) {
        if (self->voices[i].active && self->voices[i].note == note) {
            self->voices[i].active = 0;
        }
    }
}

static void
note_on(IslaPoly800* self, const uint8_t note, const uint8_t velocity)
{
    Voice* voice = NULL;

    for (unsigned i = 0; i < MAX_VOICES; ++i) {
        if (self->voices[i].active && self->voices[i].note == note) {
            voice = &self->voices[i];
            break;
        }
        if (!voice && !self->voices[i].active) {
            voice = &self->voices[i];
        }
    }

    if (!voice) {
        // M0 uses deliberately simple voice stealing. Bristol's voice model
        // will replace this when the real Poly-800 DSP is integrated.
        voice = &self->voices[0];
    }

    voice->active = 1;
    voice->note = note;
    voice->velocity = (float)velocity / 127.0f;
    voice->phase = 0.0;
}

static void
handle_midi(IslaPoly800* self, const uint8_t* msg, const uint32_t size)
{
    if (size < 3) {
        return;
    }

    const uint8_t status = msg[0] & 0xF0u;
    const uint8_t note = msg[1] & 0x7Fu;
    const uint8_t velocity = msg[2] & 0x7Fu;

    if (status == LV2_MIDI_MSG_NOTE_ON) {
        if (velocity == 0) {
            note_off(self, note);
        } else {
            note_on(self, note, velocity);
        }
    } else if (status == LV2_MIDI_MSG_NOTE_OFF) {
        note_off(self, note);
    }
}

static void
render(IslaPoly800* self, const uint32_t from, const uint32_t to)
{
    const float gain = self->gain ? clampf(*self->gain, 0.0f, 1.0f) : 0.20f;
    const float tune = self->tune ? clampf(*self->tune, -100.0f, 100.0f) : 0.0f;

    unsigned active_voices = 0;
    for (unsigned v = 0; v < MAX_VOICES; ++v) {
        active_voices += self->voices[v].active ? 1u : 0u;
    }

    if (!active_voices) {
        return;
    }

    const float voice_scale = gain * 0.35f / sqrtf((float)active_voices);

    for (uint32_t i = from; i < to; ++i) {
        double mixed = 0.0;

        for (unsigned v = 0; v < MAX_VOICES; ++v) {
            Voice* const voice = &self->voices[v];
            if (!voice->active) {
                continue;
            }

            mixed += sin(voice->phase) * (double)voice->velocity;
            voice->phase += TAU * note_frequency(voice->note, tune) / self->sample_rate;
            if (voice->phase >= TAU) {
                voice->phase -= TAU;
            }
        }

        const float sample = (float)mixed * voice_scale;
        self->audio_l[i] += sample;
        self->audio_r[i] += sample;
    }
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            const double rate,
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

    IslaPoly800* self = (IslaPoly800*)calloc(1, sizeof(IslaPoly800));
    if (!self) {
        return NULL;
    }

    self->sample_rate = rate;
    self->midi_event = map->map(map->handle, LV2_MIDI__MidiEvent);
    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, const uint32_t port, void* data)
{
    IslaPoly800* self = (IslaPoly800*)instance;

    switch ((PortIndex)port) {
    case PORT_MIDI_IN:
        self->midi_in = (const LV2_Atom_Sequence*)data;
        break;
    case PORT_AUDIO_L:
        self->audio_l = (float*)data;
        break;
    case PORT_AUDIO_R:
        self->audio_r = (float*)data;
        break;
    case PORT_GAIN:
        self->gain = (const float*)data;
        break;
    case PORT_TUNE:
        self->tune = (const float*)data;
        break;
    }
}

static void
activate(LV2_Handle instance)
{
    IslaPoly800* self = (IslaPoly800*)instance;
    memset(self->voices, 0, sizeof(self->voices));
}

static void
run(LV2_Handle instance, const uint32_t n_samples)
{
    IslaPoly800* self = (IslaPoly800*)instance;

    if (!self->audio_l || !self->audio_r) {
        return;
    }

    memset(self->audio_l, 0, n_samples * sizeof(float));
    memset(self->audio_r, 0, n_samples * sizeof(float));

    if (!self->midi_in) {
        render(self, 0, n_samples);
        return;
    }

    uint32_t cursor = 0;
    LV2_ATOM_SEQUENCE_FOREACH(self->midi_in, event) {
        uint32_t frame = 0;
        if (event->time.frames > 0) {
            frame = (uint32_t)event->time.frames;
            if (frame > n_samples) {
                frame = n_samples;
            }
        }

        if (frame > cursor) {
            render(self, cursor, frame);
            cursor = frame;
        }

        if (event->body.type == self->midi_event) {
            const uint8_t* const msg = (const uint8_t*)LV2_ATOM_BODY(&event->body);
            handle_midi(self, msg, event->body.size);
        }
    }

    if (cursor < n_samples) {
        render(self, cursor, n_samples);
    }
}

static void
deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void
cleanup(LV2_Handle instance)
{
    free(instance);
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
lv2_descriptor(const uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
