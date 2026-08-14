# M3 — LV2 state and preset workflow

M3 makes persistence explicit without duplicating the Poly-800 program in two independent storage systems.

## State model

LV2 describes an instance as control-port values plus an optional state dictionary. ISLA Poly-800 therefore keeps the sound program in its existing LV2 control ports and uses `state:interface` only for non-port state.

Schema 1 currently stores one portable POD property:

- key: `https://interspock.github.io/isla-poly800#stateVersion`
- type: `atom:Int`
- value: `1`

There is deliberately no second copy of parameters 11..84 in the state dictionary. Ardour owns and restores the input control-port values. On the next `run()` the wrapper transfers those restored values to `Poly800Core` exactly as it does during normal operation.

This separation prevents a saved session from containing two conflicting versions of the same program.

## Why have a state interface if all program values are ports?

The version marker gives future non-port state a migration point without changing the plugin URI unnecessarily. If later milestones add data that cannot be represented by control ports, it can be added to the dictionary under a new schema while old sessions remain identifiable.

`restore()` also accepts an empty dictionary and falls back to the current schema, following the LV2 state contract.

## Built-in M3 presets

M3 includes two project-original presets only to validate the standard LV2 preset path:

- `Init Whole`
- `Double Fifth Test`

They are not Korg factory patches. Each preset specifies every control port so applying it is deterministic regardless of the previously loaded sound.

The presets live in `lv2/presets.ttl` and are exposed from `manifest.ttl` using the standard `pset:Preset` vocabulary.

## Automated validation

`tests/lv2_state_smoke.c` instantiates the real LV2 descriptor with a fake URID host, obtains `state:interface`, saves schema 1, restores it, and verifies empty-state fallback.

CI also asks Lilv to discover the built bundle. This catches malformed Turtle, missing bundle files and state-interface metadata errors in addition to the DSP tests.

## Ardour acceptance test

After installing M3:

1. Create a new MIDI/instrument track with ISLA Poly-800.
2. Set an unmistakable program, for example DOUBLE mode, DCO2 interval 7, detune 3, chorus on, and a clearly changed filter/envelope.
3. Save the Ardour session and close Ardour completely.
4. Reopen the session.
5. Verify the generic controls retain the exact values and that the sound matches.
6. Open the plugin preset menu and load `Init Whole`.
7. Change several controls, then load `Double Fifth Test` and verify the full program is replaced rather than partially overlaid.
8. Save the session again, reopen, and verify the preset-derived values persist.

A failure in steps 3–5 is a host/session persistence problem or a port-compatibility regression. A failure in steps 6–7 is an LV2 preset discovery/application problem. These are intentionally separated so they can be diagnosed independently.

## Compatibility rule

The plugin URI remains:

`https://interspock.github.io/isla-poly800`

M3 does not reorder, remove, or reinterpret existing LV2 ports. Existing M1/M2 sessions therefore remain structurally compatible.
