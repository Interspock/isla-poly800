# Bristol port notes

## Upstream baseline

Bristol is the principal free-software implementation used as the behavioural/code reference for the Poly-800 architecture.

Reference mirror/baseline used for M1/M2 inspection:

- repository: `nomadbyte/bristol-fixes`
- branch: `develop`
- commit: `116fb8a2d21727676e21db5f1efe295c1ea22d61`
- relevant files: `bristol/bristolpoly800.c`, `bristol/bristolpoly800.h`, `bristol/nro.c`, `bristol/env5stage.c`, `bristol/filter.c`, `bristol/lfo.c`, `bristol/dimensionD.c`, `brighton/brightonPoly800.c`

The original Bristol Poly-800 source is Copyright (c) Nick Copeland <nickycopeland@hotmail.com> 1996,2012 and is distributed under GPL version 3 or later.

## Why this is not a mechanical Bristol wrapper

Inspection of `bristolPoly800Init()` shows that the Poly-800 algorithm depends on generic Bristol operators plus the effect system: two NRO oscillators, one filter, one noise generator, three ENV5S envelopes, one DCA, one LFO and the Dimension chorus operator.

Wrapping `bristolpoly800.c` directly would therefore drag a large part of the standalone Bristol engine and its internal voice/operator model into the plugin. That would defeat the architectural goal of a small, safe LV2 core and preserve assumptions that are undesirable inside a multi-instance DAW host.

ISLA instead ports/adapts the relevant behaviour into a compact instance-owned core while retaining explicit attribution for Bristol-derived algorithms and routing. The result remains GPL-3.0-or-later.

## Behaviour carried into M1

M1 established:

- Poly-800 WHOLE/DOUBLE voice topology and shared-filter routing from `bristolpoly800.c`;
- six-stage ADBSSR/ENV5S state sequencing and squared-rate law from `env5stage.c`;
- MG/LFO rate/delay shape and controller ranges;
- original parameter ranges/operator mappings documented in `brightonPoly800.c`;
- one fully instance-owned DSP state per LV2 instance.

M1 deliberately used simplified DCO and VCF implementations until the host path and performance were proven.

## M2 source-level calibration

M2 revisits those simplifications against the exact Bristol baseline above.

### DCO waveform and footages

`brightonPoly800.c` maps P12/P22 to NRO square and saw controls, so M2 exposes those as true square/saw choices. The plugin uses band-limited PolyBLEP generators rather than importing the complete NRO operator framework.

NRO treats the 16', 8', 4' and 2' controls as additive footage contributions. M2 therefore removes M1's active-footage normalisation. A fixed internal trim keeps normalised LV2 float levels practical without changing the additive relationship.

### DCO2 detune

NRO's `FINETUNE` parameter linearly interpolates frequency ratio around its centre. The Poly-800 frontend sends P32 to the positive half of that control, so values 0..3 span unison to approximately one semitone above. M2 implements that ratio directly rather than M1's temporary half-semitone mapping.

### Oscillator bus

`operateOnePoly800()` applies `bufmerge(outbuf, 7.0, outbuf, 1.0, ...)`, effectively multiplying the completed oscillator bus by eight before DEG3-controlled noise and the shared filter. M2 preserves that routing relationship with a fixed internal oscillator trim before the x8 bus gain.

### Shared VCF

Bristol initializes the Poly-800 filter operator with filter type 4. That selects the Huovilainen non-linear four-pole branch in `filter.c`, not the lightweight Chamberlin path used by M1.

M2 ports the relevant type-4 state and formulas into `Poly800Core`:

- cutoff/resonance correction constants from Bristol `filter.c`;
- resonance feedback through the four cascaded stages;
- DEG3 + MG modulation scaling from the Poly-800 controller mappings;
- highest-sounding-note keyboard tracking used by the shared/paraphonic filter;
- all filter and denormal-noise state held per plugin instance.

Bristol switches filter implementation detail at 88 kHz. M2 preserves both paths:

- below 88 kHz: internally 2x-oversampled Huovilainen branch;
- 88 kHz and above: single-pass high-rate branch with Bristol's corresponding modulation coefficient and cutoff ceiling.

The 96 kHz branch is covered by an automated regression test because the Midiplus AudioLink path used by ISLA can operate at 96 kHz.

### Envelope mapping

Poly-800 parameters 51..56, 61..66 and 71..76 map onto ENV5S as:

- Attack rate -> level 1 = 1.0;
- Decay rate -> Break Point;
- Break Point -> intermediate level;
- Slope rate -> Sustain;
- Sustain -> held level;
- Release rate -> final level 0.

M2 retains the existing M1/M1.1 implementation because it already matches this source mapping and Bristol's squared-rate conversion.

### MG and chorus status

The verified MG frequency curve, delay ramp and VCF depth scaling remain in place. The absolute MG-to-DCO depth still needs an A/B measurement because Bristol applies its LFO as a multiplicative frequency-buffer modulation after an offset/ramp path; M2 does not claim that final audible depth is calibrated yet.

The stock Poly-800 path instantiates Bristol palette operator 12 (`chorusinit`, implemented in `dimensionD.c`). M2 intentionally leaves the lightweight M1 chorus in place until a controlled A/B can establish the fixed stock setting we want for the original Poly-800's simple chorus on/off control.

These two items are tracked as M2.1 rather than being guessed.

## Regression tests

`tests/core_calibration.c` checks properties that follow directly from the source-level calibration:

- square and saw settings produce materially distinct outputs;
- enabling another footage raises the oscillator/output energy rather than being normalised away;
- P32 endpoint frequency ratio is approximately one semitone;
- the 96 kHz filter branch remains finite under a stressed DOUBLE/chorus configuration.

`tests/core_smoke.c` continues to guard basic sound generation and numerical stability, while `tests/core_bench.c` watches performance.

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

The design target is safe simultaneous use of multiple plugin instances with different programs.

## Fidelity strategy

M2 is a **source-level calibration milestone**, not a claim of sample-identical Bristol or hardware output. Verified source mappings are implemented first; remaining audible constants are changed only when there is a reproducible A/B reason to change them.

Next fidelity work should focus on MG-DCO depth, chorus timing/mix, output scaling and complete-patch comparison against Bristol/reference material without reintroducing Bristol's standalone architecture.

## Licensing/provenance rules

For all future changes:

1. retain Bristol copyright/license attribution in files containing Bristol-derived code;
2. record the exact upstream source/commit for any newly adapted algorithm;
3. keep project-original wrapper/core additions GPL-3.0-or-later;
4. never assume downloadable patches/ROMs/dumps are redistributable merely because they are publicly accessible;
5. keep the build reproducible without proprietary assets.
