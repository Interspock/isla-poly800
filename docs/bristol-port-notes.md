# Bristol port notes

## Upstream baseline

Bristol is the principal free-software implementation used as the behavioural/code reference for the Poly-800 architecture.

Reference mirror/baseline used for M1 inspection:

- repository: `nomadbyte/bristol-fixes`
- branch: `develop`
- commit: `116fb8a2d21727676e21db5f1efe295c1ea22d61`
- relevant files: `bristol/bristolpoly800.c`, `bristol/bristolpoly800.h`, `bristol/nro.c`, `bristol/env5stage.c`, `bristol/filter.c`, `bristol/lfo.c`, `brighton/brightonPoly800.c`

The original Bristol Poly-800 source is Copyright (c) Nick Copeland <nickycopeland@hotmail.com> 1996,2012 and is distributed under GPL version 3 or later.

## Why M1 is not a mechanical Bristol wrapper

Inspection of `bristolPoly800Init()` shows that the Poly-800 algorithm depends on nine generic Bristol operators plus the effect system: two NRO oscillators, one filter, one noise generator, three five/six-stage envelope operators, one DCA, one LFO and vibrachorus.

Wrapping `bristolpoly800.c` directly would therefore drag a large part of the standalone Bristol engine and its internal voice/operator model into the plugin. That would defeat the architectural goal of a small, safe LV2 core and preserve assumptions that are undesirable inside a multi-instance DAW host.

M1 instead ports/adapts the relevant behaviour into a compact instance-owned core while retaining explicit attribution for Bristol-derived algorithms and routing. The result remains GPL-3.0-or-later.

## Bristol behaviour carried into M1

The current core is specifically informed by/adapts:

- Poly-800 WHOLE/DOUBLE voice topology and shared-filter routing from `bristolpoly800.c`;
- Bristol's six-stage envelope rate law from `env5stage.c`;
- the shared Chamberlin-style filter state/routing from `filter.c`;
- MG/LFO rate-shape ideas from `lfo.c` and the Poly-800 controller mappings;
- original parameter ranges/operator mappings documented in `brightonPoly800.c`.

The DCO implementation is deliberately self-contained rather than importing Bristol's entire NRO operator framework. It follows the Poly-800's four-footage construction and provides a band-limited square building block suitable for the plugin architecture. Exact waveform parity remains a later calibration task.

## What is intentionally not ported

- Brighton GUI;
- JACK/ALSA device handling;
- external MIDI device handling;
- GUI/engine IPC;
- Bristol process/session management;
- standalone command-line startup;
- generic Bristol operator registry/palette;
- proprietary ROMs, firmware, tape dumps or factory assets.

LV2/Ardour owns host integration; `poly800_core.c` owns only synthesis state and rendering.

## Global-state risk and resolution

Bristol's Poly-800 source contains an explicit warning about global/shared buffers and multiple audio threads. ISLA Poly-800 resolves this structurally: every mutable field is owned by `Poly800Core`, including voices, phases, all envelopes, the shared VCF, LFO, RNG and chorus delay.

The design target is therefore safe simultaneous use of multiple plugin instances with different programs.

## Fidelity strategy

M1 is an **architecture port**, not a claim of sample-identical Bristol output. This is deliberate and documented in the plugin name while calibration is incomplete.

The next fidelity work should compare isolated subsystems and complete patches against Bristol/reference material, then adjust constants without reintroducing Bristol's standalone architecture. Priority areas are DCO waveform/harmonic weighting, DCO2 detune, DEG timing curves, VCF scaling, MG curves and chorus timing/mix.

## Licensing/provenance rules

For all future changes:

1. retain Bristol copyright/license attribution in files containing Bristol-derived code;
2. record the exact upstream source/commit for any newly adapted algorithm;
3. keep project-original wrapper/core additions GPL-3.0-or-later;
4. never assume downloadable patches/ROMs/dumps are redistributable merely because they are publicly accessible;
5. keep the build reproducible without proprietary assets.
