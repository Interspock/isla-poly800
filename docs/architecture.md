# Architecture

## Goal

ISLA Poly-800 is a headless LV2 instrument that preserves the important synthesis behaviour of the Korg Poly-800 while remaining native, inspectable and rebuildable on GNU/Linux.

The project deliberately separates **host integration** from **synthesis core**:

```text
Ardour
  |
  | MIDI events + LV2 control ports
  v
src/isla_poly800.c
  |
  | note/control API
  v
src/poly800_core.c
  |
  v
stereo audio
```

## M0 result

M0 proved the LV2 boundary with a temporary sine synth. It was compiled, installed and tested successfully in Ardour on the ISLA machine. That oscillator is now removed from the plugin path.

## M1 core

M1 introduces an instance-owned Poly-800 core with the original high-level signal topology:

```text
WHOLE:  8 x [DCO1 -> DEG1] --+
                                +--> shared mix --> DEG3/noise --> shared VCF --> chorus
DOUBLE: 4 x [DCO1 -> DEG1] --+
              [DCO2 -> DEG2] --+
```

Important properties:

- WHOLE mode provides eight voices.
- DOUBLE mode provides four voices and two DCO/DEG paths per voice.
- DCOs construct tones from the 16', 8', 4' and 2' footages.
- DEG1 and DEG2 are voice-local six-stage envelopes.
- DEG3 and the VCF are shared/paraphonic by default, which is a defining Poly-800 characteristic.
- DEG3 can use single or multi retrigger according to parameter 46.
- the MG/LFO is synth-global.
- noise feeds the shared DEG3/filter path.
- chorus is post-filter.

## LV2 boundary

The wrapper is intentionally small. It owns:

- Atom Sequence MIDI input;
- sample-offset event handling;
- stereo output buffers;
- control-port to `Poly800Params` translation;
- one `Poly800Core` object per LV2 instance.

It does **not** contain synthesis algorithms, JACK/ALSA integration, GUI code or filesystem/session logic.

## Parameter surface

M1 exposes the original sound parameters by their Poly-800 numbers so Ardour's generic editor is useful without a custom GUI:

- 11..18 DCO1/mode;
- 21..27 DCO2;
- 31..33 interval/detune/noise;
- 41..48 VCF/chorus (excluding unused 47);
- 51..56 DEG1;
- 61..66 DEG2;
- 71..76 DEG3;
- 81..84 MG.

Parameters 86..88 are MIDI configuration on the hardware and are deliberately not plugin controls; Ardour owns channel and program routing.

## Per-instance state

No mutable DSP state is global. Every instance owns its voices, oscillator phases, envelopes, shared filter, LFO, noise generator and chorus delay:

```text
Ardour track A -> LV2 instance A -> Poly-800 state A
Ardour track B -> LV2 instance B -> Poly-800 state B
```

This is an explicit departure from assumptions in the old standalone Bristol architecture and is required for safe multi-instance plugin use.

## Real-time rules

The audio callback performs no heap allocation/free, filesystem access, locks, process spawning or network/IPC. Persistent buffers, including the chorus delay, are allocated when the core is created.

## Fidelity boundary

M1 establishes architecture and parameter semantics, not final calibration. Later work must A/B oscillator waveform/aliasing behaviour, DCO2 detune, MG curves, filter scaling, chorus timing/mix and factory-program values. Those refinements should not require changing the LV2 host architecture.

## GUI

A custom GUI remains deliberately out of scope. Ardour's generic LV2 editor exposes the meaningful controls and allows host-level automation/MIDI mapping.
