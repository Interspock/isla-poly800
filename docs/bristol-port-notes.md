# Bristol port notes

## Upstream baseline

Bristol is the principal free-software implementation used as the behavioural/code reference for the Poly-800 architecture.

Reference mirror/baseline used for M1/M2/M4 inspection:

- repository: `nomadbyte/bristol-fixes`
- branch: `develop`
- commit: `116fb8a2d21727676e21db5f1efe295c1ea22d61`
- relevant files: `bristol/bristolpoly800.c`, `bristol/bristolpoly800.h`, `bristol/nro.c`, `bristol/env5stage.c`, `bristol/filter.c`, `bristol/lfo.c`, `bristol/dimensionD.c`, `brighton/brightonPoly800.c`

The original Bristol Poly-800 source is Copyright (c) Nick Copeland <nickycopeland@hotmail.com> 1996,2012 and is distributed under GPL version 3 or later.

## Why this is not a mechanical Bristol wrapper

Inspection of `bristolPoly800Init()` shows that the Poly-800 algorithm depends on generic Bristol operators plus the effect system: two NRO oscillators, one filter, one noise generator, three ENV5S envelopes, one DCA, one LFO and the Dimension chorus operator.

Wrapping `bristolpoly800.c` directly would therefore drag a large part of the standalone Bristol engine and its internal voice/operator model into the plugin. That would defeat the architectural goal of a small, safe LV2 core and preserve assumptions that are undesirable inside a multi-instance DAW host.

ISLA instead ports/adapts the relevant behaviour into a compact instance-owned core while retaining explicit attribution for Bristol-derived algorithms and routing. The result remains GPL-3.0-or-later.

A second fidelity rule became explicit during M4: Bristol-specific extensions are not automatically treated as stock Korg behaviour. When Bristol exposes controls absent from the original instrument, Korg hardware/owner documentation takes precedence for the stock mode.

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

### MG and chorus status at M2

The verified MG frequency curve, delay ramp and VCF depth scaling remained in place. The absolute MG-to-DCO depth still needed a source/A-B pass because Bristol applies its LFO as multiplicative frequency-buffer modulation after an offset/ramp path.

The stock Poly-800 path instantiates Bristol palette operator 12 (`chorusinit`, implemented in `dimensionD.c`). M2 intentionally left the lightweight M1 chorus in place rather than assume that Bristol's extended Dimension controls represented the fixed Korg chorus.

Those two items became M4.

## M4 MG/LFO source calibration

`bristolpoly800.c` resolves the remaining DCO-modulation law directly:

```text
vcomod = value^2 * 4
```

Bristol generates the sine LFO, adds `+1`, then sends that signal through the delayed gain DCA. The modulation bus is therefore:

```text
lfo_bus = (1 + sine) * fade_gain
```

The DCO frequency-buffer path uses Bristol's `mult2buf()` relationship:

```text
frequency *= 1 + lfo_bus * vcomod
```

M4 reproduces that multiplicative path. At large P83 values this is intentionally an extreme effect and must not be silently reinterpreted as a conventional +/- semitone vibrato control.

P81 was already correct in M2: `0.1 + value^3 * 20 Hz` from `lfo.c`. P82 was also correct: normalized 0..1 becomes 0..15 seconds delay with a matching fade duration. P84 already carried Bristol's `value^2 * 8` coefficient; M4 feeds it the corrected unipolar LFO bus as Bristol does.

## M4 chorus correction: Bristol extension vs stock Korg

Bristol's Poly-800 requests palette operator 12 (`chorusinit`, `dimensionD.c`). That operator has a 4096-sample history, interpolated variable delay, sine-driven scan, feedback and moving stereo wet gain.

The Brighton frontend also exposes four chorus-related slots:

- P48: stock-visible Chorus ON/OFF;
- 58: Bristol-only speed control;
- 68: Bristol-only depth control;
- 78: Bristol-only stereo/scan control.

The first M4 pass froze 58/68/78 to values present in Bristol memories 11/12/13. In Ardour this produced a conspicuous periodic sweep. Source reinspection explained the result: with those values `dimensionD.c` makes a large variable-delay excursion and feeds the wet signal back into its history. This is coherent Bristol Dimension behaviour, but the hidden controls themselves are extensions and therefore do not establish stock Korg calibration.

The original MkI hardware instead has a fixed analog BBD chorus. Korg service documentation identifies the MN3209 BBD and MN3102 clock driver; the instrument exposes only P48 ON/OFF. The ISLA stock path therefore no longer ports Bristol's hidden Dimension settings.

Current fixed BBD-style approximation:

```text
rate         0.55 Hz
centre delay 6.8 ms
mod depth    +/-0.6 ms
wet/dry      25% / 75%
feedback     none
```

The implementation uses one per-instance delay history and two complementary read taps for restrained stereo decorrelation. Delay/LFO state keeps advancing through bypass so P48 does not restart the modulation engine.

These constants are hardware-informed and intentionally conservative. They can be refined by a controlled capture from real hardware without adding non-stock controls to the LV2 surface.

## M4 implementation checkpoint

To make the M4 delta easy to audit, the complete M2 synthesis/filter core is frozen in:

```text
src/poly800_core_m2.inc
```

`src/poly800_core.c` includes that checkpoint internally and overrides only construction/rendering needed for the corrected MG and chorus paths. It is not a runtime dependency, plugin split, or external engine.

This structure can be collapsed later once external A/B work stabilizes; during fidelity work it makes accidental changes to already-calibrated M2 DCO/filter behaviour obvious.

## Regression tests

`tests/core_calibration.c` checks properties that follow directly from the M2 source-level calibration:

- square and saw settings produce materially distinct outputs;
- enabling another footage raises the oscillator/output energy rather than being normalised away;
- P32 endpoint frequency ratio is approximately one semitone;
- the 96 kHz filter branch remains finite under a stressed DOUBLE/chorus configuration.

`tests/core_m4.c` adds:

- P83=15 produces materially deeper frequency modulation than P83=0;
- P48 OFF is true mono/dry output;
- P48 ON produces finite but restrained stereo decorrelation;
- a stable C4 keeps approximately its dry zero-crossing density, guarding against the deep-sweep regression found in the first M4 pass;
- P48 does not cause an excessive level jump/drop;
- the chorus remains finite on the 96 kHz AudioLink path.

`tools/core_ab_probe.c` renders deterministic, asset-free metrics for P83 0/7/15 and stock BBD-style chorus ON. It provides a repeatable protocol for later Bristol/hardware captures without claiming current sample identity.

`tests/core_smoke.c` continues to guard basic sound generation and numerical stability, while `tests/core_bench.c` watches performance.

## What is intentionally not ported

- Brighton GUI;
- JACK/ALSA device handling;
- external MIDI device handling;
- GUI/engine IPC;
- Bristol process/session management;
- standalone command-line startup;
- generic Bristol operator registry/palette;
- Bristol-only Poly-800 controls in stock mode;
- proprietary ROMs, firmware, tape dumps or factory assets.

LV2/Ardour owns host integration; the core owns only synthesis state and rendering.

## Global-state risk and resolution

Bristol's Poly-800 source contains an explicit warning about global/shared buffers and multiple audio threads. ISLA Poly-800 resolves this structurally: every mutable field is owned by `Poly800Core`, including voices, phases, all envelopes, the shared VCF, LFO, RNG and chorus delay/history.

The design target is safe simultaneous use of multiple plugin instances with different programs.

## Fidelity strategy

M2 and M4 are calibration milestones, not a claim of sample-identical Bristol or hardware output. Bristol remains a strong oracle where its model follows stock controls/architecture; original hardware documentation and later hardware captures take precedence where Bristol adds capabilities absent from the Korg.

M4.1 can use `core_ab_probe` plus hardware/reference recordings to refine residual output scaling, chorus constants and waveform/filter character without reopening already-source-verified controller mappings without evidence.

## Licensing/provenance rules

For all future changes:

1. retain Bristol copyright/license attribution in files containing Bristol-derived code;
2. record the exact upstream source/commit for any newly adapted algorithm;
3. keep project-original wrapper/core additions GPL-3.0-or-later;
4. never assume downloadable patches/ROMs/dumps are redistributable merely because they are publicly accessible;
5. keep the build reproducible without proprietary assets.
