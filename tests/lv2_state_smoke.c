// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#define PLUGIN_URI "https://interspock.github.io/isla-poly800"
#define STATE_VERSION_URI PLUGIN_URI "#stateVersion"

enum {
    URID_MIDI_EVENT = 1,
    URID_ATOM_INT = 2,
    URID_STATE_VERSION = 3,
    URID_OTHER = 99
};

typedef struct {
    uint32_t key;
    uint32_t type;
    uint32_t flags;
    size_t size;
    int32_t value;
    int available;
} SavedState;

extern const LV2_Descriptor* lv2_descriptor(uint32_t index);

static LV2_URID
map_uri(LV2_URID_Map_Handle handle, const char* uri)
{
    (void)handle;
    if (!strcmp(uri, LV2_MIDI__MidiEvent)) return URID_MIDI_EVENT;
    if (!strcmp(uri, LV2_ATOM__Int)) return URID_ATOM_INT;
    if (!strcmp(uri, STATE_VERSION_URI)) return URID_STATE_VERSION;
    return URID_OTHER;
}

static LV2_State_Status
store_value(LV2_State_Handle handle,
            uint32_t key,
            const void* value,
            size_t size,
            uint32_t type,
            uint32_t flags)
{
    SavedState* saved = (SavedState*)handle;
    saved->key = key;
    saved->type = type;
    saved->flags = flags;
    saved->size = size;
    saved->available = 1;
    if (size == sizeof(saved->value)) {
        memcpy(&saved->value, value, size);
    }
    return LV2_STATE_SUCCESS;
}

static const void*
retrieve_value(LV2_State_Handle handle,
               uint32_t key,
               size_t* size,
               uint32_t* type,
               uint32_t* flags)
{
    SavedState* saved = (SavedState*)handle;
    if (!saved->available || key != saved->key) {
        return NULL;
    }
    *size = saved->size;
    *type = saved->type;
    *flags = saved->flags;
    return &saved->value;
}

static int
fail(const char* message)
{
    fprintf(stderr, "lv2_state_smoke: %s\n", message);
    return 1;
}

int
main(void)
{
    const LV2_Descriptor* descriptor = lv2_descriptor(0);
    if (!descriptor || strcmp(descriptor->URI, PLUGIN_URI)) {
        return fail("plugin descriptor/URI mismatch");
    }

    LV2_URID_Map map = {NULL, map_uri};
    LV2_Feature map_feature = {LV2_URID__map, &map};
    const LV2_Feature* features[] = {&map_feature, NULL};

    LV2_Handle instance = descriptor->instantiate(descriptor, 48000.0, ".", features);
    if (!instance) {
        return fail("instantiate failed");
    }

    const LV2_State_Interface* state =
        (const LV2_State_Interface*)descriptor->extension_data(LV2_STATE__interface);
    if (!state || !state->save || !state->restore) {
        descriptor->cleanup(instance);
        return fail("state interface missing");
    }

    SavedState saved;
    memset(&saved, 0, sizeof(saved));

    if (state->save(instance, store_value, &saved, 0, NULL) != LV2_STATE_SUCCESS) {
        descriptor->cleanup(instance);
        return fail("save failed");
    }
    if (!saved.available || saved.key != URID_STATE_VERSION ||
        saved.type != URID_ATOM_INT || saved.size != sizeof(int32_t) ||
        saved.value != 1) {
        descriptor->cleanup(instance);
        return fail("saved schema marker is wrong");
    }
    if ((saved.flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) !=
        (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) {
        descriptor->cleanup(instance);
        return fail("saved state is not POD+portable");
    }

    if (state->restore(instance, retrieve_value, &saved, 0, NULL) != LV2_STATE_SUCCESS) {
        descriptor->cleanup(instance);
        return fail("restore failed");
    }

    saved.available = 0;
    if (state->restore(instance, retrieve_value, &saved, 0, NULL) != LV2_STATE_SUCCESS) {
        descriptor->cleanup(instance);
        return fail("empty-state fallback failed");
    }

    descriptor->cleanup(instance);
    puts("lv2_state_smoke: ok");
    return 0;
}
