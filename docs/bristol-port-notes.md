# Bristol provenance and current boundary

## Current status

Bristol was the principal free-software reference used to bootstrap ISLA Poly-800, but **the current M6 engine is not a Bristol Poly-800 wrapper and Bristol is no longer treated as authoritative when stronger stock evidence exists**.

This document records what remains Bristol-derived/adapted in the production engine and what has been superseded by Korg hardware documentation, factory data or EX-800 firmware reverse engineering.

Older milestone documents and git commits preserve the chronological experiments. This file describes the current relationship.

## Upstream baseline

Reference mirror/baseline used during the source-level calibration work:

- repository: `nomadbyte/bristol-fixes`
- branch: `develop`
- commit: `116fb8a2d21727676e21db5f1efe295c1ea22d61`
- relevant files included `bristolpoly800.c`, `nro.c`, `env5stage.c`, `filter.c`, `lfo.c`, `dimensionD.c` and the Brighton Poly-800 frontend.

The original Bristol Poly-800 source is Copyright (c) Nick Copeland and distributed under GPL version 3 or later. ISLA retains attribution in files containing adapted behaviour.

## Why Bristol was invaluable

Bristol supplied a coherent, inspectable implementation of several important ideas before the project had primary-source reverse engineering:

- WHOLE/DOUBLE voice organization;
- per-voice oscillator/envelope routing;
- one shared paraphonic filter;
- DEG stage sequencing;
- noise/shared-envelope routing;
- keyboard-tracking behaviour;
- a mature nonlinear four-pole filter implementation;
- useful original-parameter mappings in the Brighton frontend.

It also demonstrated how a Poly-800-like algorithm could be expressed efficiently enough for software synthesis.

Those contributions remain foundational to ISLA's architecture.

## Why ISLA is not a mechanical Bristol port

A literal wrapper around Bristol would pull in a large standalone operator engine, device/session assumptions and global/shared state that are undesirable in a DAW plugin.

ISLA instead implements a compact per-instance core and ports only behaviour justified for the stock instrument.

More importantly, Bristol sometimes exposes convenient or creative behaviours that are not evidence of the original Korg hardware. The project therefore applies this rule:

> Bristol is strong prior art; original hardware documentation, recovered firmware and direct measurements override it when they disagree.

## Behaviour still derived/adapted from Bristol

### Shared filter implementation

The current VCF remains a compact per-instance adaptation of Bristol's filter type 4 / Huovilainen nonlinear four-pole path.

ISLA preserves the separate low-rate internally oversampled and >=88 kHz high-rate branches. This is particularly useful for the ISLA AudioLink 96 kHz production path.

The stock topology around the filter is independently supported by Korg documentation; Bristol supplies the practical software transfer model.

This filter is not claimed to be a component-level NJM2069 simulation.

### Envelope state sequencing

The ADBSSR-style stage structure used by the core is compatible with Bristol ENV5S and with the original Poly-800 six-stage parameter surface:

```text
Attack -> Decay to Break Point -> Slope to Sustain -> Sustain -> Release
```

The current timing/level calibration is no longer simply Bristol's old squared-rate law; later Korg manual and factory-audio calibration superseded that audible mapping.

### Some routing/scaling scaffolding

The M2 baseline still contains Bristol-informed oscillator-bus/filter routing and supporting helper code. Later layers override stock behaviours where stronger evidence became available.

## Bristol behaviours superseded in the stock engine

### DCO waveform model

Earlier code followed the Brighton/NRO interpretation of P12/P22 as selecting a mathematical square or saw oscillator.

Korg hardware documentation instead shows square-wave 16'/8'/4'/2' tone-generator outputs followed by waveform synthesis through different footage-weight relationships.

The current engine therefore uses square generators for every footage and applies:

```text
1 : 1   : 1   : 1
1 : 1/2 : 1/4 : 1/8
```

This stock DCO model supersedes the Bristol waveform interpretation.

### DCO2 detune

The Bristol frontend/NRO path led to a positive, much larger fine-tune interpretation.

MkI Korg documentation describes a downward pulse-thinning detune with a `-20 cent` maximum. The current core uses that stock direction/endpoint.

### MG/LFO

This is the most important M6 supersession.

The historical M4 implementation followed Bristol's:

- audio-rate sine LFO;
- cubic frequency approximation;
- artificial delay/fade interpretation;
- quadratic depth coefficients;
- offset/unipolar modulation bus.

The recovered EX-800 80C85 firmware shows a different mechanism:

- P81 indexes a 16-byte increment table at ROM `0x14EE`;
- an 8-bit accumulator is folded into a digital triangle;
- sign flips on counter overflow;
- P83/P84 use a four-bit fixed-point multiplication routine;
- P82 uses the firmware `LINEAR_TABLE` and delay counters;
- the output is updated at the firmware scheduler/control rate and sample-held.

`src/poly800_core_m6.c` implements that recovered control engine. The older Bristol MG remains only in the frozen pre-M6 checkpoint used for historical regression comparison.

### Chorus

Bristol's Dimension operator exposes speed/depth/stereo scan controls that do not exist on the stock MkI.

Korg documentation identifies a fixed MN3209/MN3102 BBD chorus controlled only by P48 ON/OFF. ISLA therefore uses a fixed stock-style approximation and does not expose Bristol's hidden Dimension controls.

## Frozen checkpoints

For auditability:

```text
src/poly800_core_m2.inc   Bristol-calibrated baseline
src/poly800_core.c        pre-M6 M4/M5 checkpoint
src/poly800_core_m6.c     current production engine
```

The layering is deliberate historical evidence, not runtime complexity. CMake selects only M6 for the production LV2.

## Tests and historical assertions

`tests/core_m4.c` still builds against `src/poly800_core.c`. Its strong P83/Bristol-scale assertion is intentionally historical and proves that the old checkpoint remains reproducible.

`tests/core_m6_mg.c` is the current white-box acceptance test for the recovered ROM table, fixed-point multiply, delay law and triangle/control-rate behaviour.

This separation prevents a newer, more accurate implementation from being forced to satisfy an obsolete Bristol-specific expectation.

## What is intentionally not ported

- Brighton GUI;
- JACK/ALSA device handling;
- standalone process/session management;
- generic Bristol operator registry;
- GUI/engine IPC;
- Bristol-only Poly-800 controls in stock mode;
- mutable global DSP state.

LV2/Ardour owns host integration. The ISLA core owns synthesis only.

## Licensing rule

For future work:

1. retain Bristol attribution where code/algorithms remain derived from Bristol;
2. record the upstream commit when new Bristol material is adapted;
3. prefer primary Korg/firmware/hardware evidence for stock behaviour;
4. keep project-original additions GPL-3.0-or-later;
5. do not redistribute proprietary ROMs, manual scans or binary assets merely because they are available elsewhere.

For the full current fidelity argument, see [reconstruction.md](reconstruction.md).