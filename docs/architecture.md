# Architecture

## Goal

ISLA Poly-800 is intended to become a headless LV2 instrument that preserves the important synthesis behaviour of the Korg Poly-800 while remaining native, inspectable and rebuildable on GNU/Linux.

The project deliberately separates **host integration** from **synthesis emulation**.

```text
Ardour
  |
  | MIDI events + LV2 control ports
  v
LV2 wrapper
  |
  v
Poly-800 core
  |
  v
stereo audio
```

## Milestone 0 boundary

M0 proves only the LV2 boundary. The current oscillator is a simple project-original sine synth and must not be used as a reference for Poly-800 sound or behaviour.

M0 responsibilities:

- LV2 discovery by a host.
- Atom Sequence MIDI input.
- sample-offset handling for note events.
- stereo audio output.
- generic host-visible control ports.
- no GUI dependency.
- no dynamic allocation in the audio `run()` callback.

## Intended M1 boundary

M1 replaces the smoke-test voice engine with a Poly-800 core adapted from Bristol.

The LV2 layer should know as little as possible about the synthesis implementation. Its responsibilities should remain:

- translate LV2 MIDI events into core note/control calls;
- expose parameters;
- own one independent core instance per LV2 instance;
- render audio for the requested block;
- later serialize/restore state.

The synthesis core should contain no JACK, ALSA, Brighton GUI, TCP/IPC or host-specific code.

## Per-instance state

A major porting requirement is eliminating mutable global state from the adapted DSP path. Every plugin instance must own its own voices, buffers, envelopes, oscillators, filter state, LFO state and parameters.

This must be safe:

```text
Ardour track A -> LV2 instance A -> Poly-800 state A
Ardour track B -> LV2 instance B -> Poly-800 state B
```

No mutable DSP state may leak between instances.

## Real-time rules

The audio callback should avoid:

- heap allocation/free;
- filesystem access;
- locks;
- process spawning;
- network/IPC;
- unbounded work.

Buffers and voice state should be allocated/initialized outside `run()`.

## GUI

A custom GUI is explicitly out of scope for the early milestones. Ardour's generic LV2 editor is sufficient as long as all meaningful parameters are exposed with sensible names and ranges.
