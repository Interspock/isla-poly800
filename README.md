# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 4 — Bristol MG/chorus calibration + reproducible A/B probe.**

M0 validated the LV2/MIDI/audio path in Ardour. M1 implemented the Poly-800 topology and parameter surface. M1.1 made the DSP suitable for real-time use on older ISLA hardware. M2 moved the actual synthesis behaviour closer to Bristol's GPL Poly-800 implementation. M3 made persistence explicit with LV2 State and deterministic presets. M4 closes the two source-level behaviours intentionally left provisional in M2: MG-to-DCO depth/routing and the stock-visible chorus path.

M4 does not change the LV2 URI or port layout and preserves M3 state/preset behaviour. Existing host integration therefore remains structurally compatible.

The emulator is still **not a claim of circuit-perfect, bit-identical, or sample-identical Poly-800 hardware emulation**. M4 adds a deterministic asset-free probe so broader comparison against Bristol renders and real/reference Poly-800 captures can be performed reproducibly rather than by guesswork.

## Synthesis architecture

- WHOLE mode: 8 voices, one DCO/DEG per voice.
- DOUBLE mode: 4 voices, two DCO/DEG paths per voice.
- DCO footages 16', 8', 4' and 2'.
- Square and saw wave choices.
- DCO2 interval and detune.
- Three six-stage DEGs (Attack, Decay, Break Point, Slope, Sustain, Release).
- One shared/paraphonic VCF and DEG3 path, matching the stock architecture.
- VCF cutoff, resonance, keyboard tracking, envelope polarity/intensity and single/multi trigger.
- Noise through DEG3.
- MG/LFO frequency, delay, DCO depth and VCF depth.
- Chorus on/off.
- Independent DSP state for every LV2 instance; no mutable DSP globals.

The LV2 exposes the original sound-program parameter numbers (11..84) directly through Ardour's generic editor. MIDI-specific original parameters 86..88 are intentionally omitted because the DAW owns MIDI channel/program routing.

## M2 calibration work

The M2 pass was checked against Bristol source at commit `116fb8a2d21727676e21db5f1efe295c1ea22d61`.

- P12/P22 select actual square or saw generation.
- The 16'/8'/4'/2' footages are additive, as in Bristol NRO, rather than normalised by the number enabled.
- P32 DCO2 detune follows NRO's linear frequency-ratio interpolation from unison to one semitone across values 0..3.
- The oscillator bus follows Bristol's Poly-800 routing gain before the shared filter.
- The shared VCF uses Bristol's filter type 4 Huovilainen path rather than the earlier lightweight Chamberlin approximation.
- Below 88 kHz the VCF uses Bristol's internally 2x-oversampled branch; at 88 kHz and above it switches to the high-rate branch. This covers both 48 kHz testing and the AudioLink 96 kHz path.
- VCF cutoff, resonance, keyboard tracking, DEG3 amount/polarity and MG-to-VCF controller scaling follow Bristol's Poly-800 mappings.
- ENV5S/ADBSSR timing uses Bristol's squared-rate law.
- M1.1 caches and Release-build optimisation remain in place.

`tests/core_calibration.c` locks the verified behaviour with regression checks for square/saw distinction, additive footages, the P32 detune endpoint, and finite 96 kHz filter operation.

## M3 state and presets

LV2 defines complete plugin persistence as port values plus an optional state dictionary. ISLA Poly-800 follows that model instead of serialising parameters 11..84 twice.

- Ardour/the host saves and restores all LV2 control-port values.
- `state:interface` stores a portable integer schema marker at `#stateVersion`.
- Schema 1 contains no duplicate sound parameters and no transient voice/phase state.
- `tests/lv2_state_smoke.c` exercises save, restore and empty-state fallback through the actual LV2 descriptor.
- Two project-original deterministic presets are bundled for host validation: **Init Whole** and **Double Fifth Test**.
- These M3 presets are test/utility sounds, not Korg factory programs.

See `docs/m3-state-presets.md` for the persistence model and Ardour acceptance test.

## M4 MG and chorus calibration

M4 returns to the same Bristol source baseline and resolves the two M2.1 items from source rather than choosing values by ear.

- P81 remains Bristol's cubic LFO law: `0.1 + value^3 * 20 Hz`.
- P82 remains Bristol's 0..15 second delay followed by an equal-duration fade-in.
- P83 now follows Bristol's `value^2 * 4` modulation coefficient.
- Bristol offsets its sine LFO by +1 before the delayed gain DCA, so the DCO/VCF modulation bus is unipolar: `(1 + sine) * fade_gain`.
- DCO frequency follows Bristol's multiplicative `frequency *= 1 + lfo_bus * vcomod` path. High P83 values are therefore intentionally much deeper than conventional vibrato-in-semitones controls.
- P84 retains Bristol's `value^2 * 8` coefficient and now receives that same unipolar LFO bus.
- P48 uses an adaptation of Bristol palette operator 12 (`chorusinit`, `dimensionD.c`) rather than the temporary M1 dual-tap chorus.
- Bristol-only hidden speed/depth/scan controls remain hidden; with P48 ON they use the common values carried by Bristol's supplied programs 11/12/13.
- P48 OFF is an audible hard bypass, while the effect history/scan continues internally so switching it on does not restart the modulation engine from phase zero.

See `docs/m4-ab-calibration.md` for formulas, hidden-default provenance and the A/B protocol.

`tests/core_m4.c` locks the new modulation scale, dry bypass, stereo chorus behaviour and the 96 kHz AudioLink path.

## Reproducible A/B probe

M4 adds:

```bash
./build/core_ab_probe
```

It renders a fixed C4 program at 48 kHz and prints RMS, zero-crossing count and stereo-difference RMS for dry P83=0/7/15 and chorus ON. The probe uses no proprietary audio assets; its output is intended as a stable experiment protocol for future Bristol/hardware comparisons.

## Performance

The project defaults to a Release build when using a single-config generator such as Unix Makefiles. A small core benchmark is included so the same scenarios can be compared on the actual ISLA machines:

```bash
./build/core_bench
```

It reports WHOLE 1/4/8 voices and DOUBLE 1/4 voices with and without chorus. Benchmark numbers are intended for comparisons on the same machine, not as absolute cross-machine ratings.

## Build

Requirements on Debian/Ubuntu/Mint:

```bash
sudo apt install build-essential cmake pkg-config lv2-dev
```

Build and test:

```bash
git clone https://github.com/Interspock/isla-poly800.git
cd isla-poly800
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The build produces:

```text
build/isla-poly800.lv2/
├── isla-poly800.so
├── isla-poly800.ttl
├── manifest.ttl
├── state.ttl
└── presets.ttl
```

Install for the current user:

```bash
mkdir -p ~/.lv2
rm -rf ~/.lv2/isla-poly800.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Then restart/rescan Ardour. The plugin URI and port layout are unchanged from M1/M2/M3, so existing host integration remains structurally compatible.

## Source layout

```text
src/isla_poly800.c        LV2/MIDI glue + versioned state interface
src/poly800_core.c        M4 calibration layer
src/poly800_core_m2.inc   frozen M2 synthesis/filter implementation
src/poly800_core.h        core API and stock parameter model
lv2/manifest.ttl          plugin/preset discovery
lv2/state.ttl             LV2 state-interface metadata
lv2/presets.ttl           deterministic M3 utility presets
lv2/isla-poly800.ttl      ports / generic controls
tests/core_smoke.c        headless DSP smoke/stress test
tests/core_calibration.c  M2 fidelity regression tests
tests/core_m4.c           M4 MG/chorus + 96 kHz regression tests
tests/lv2_state_smoke.c   M3 LV2 state save/restore test
tests/core_bench.c        reproducible DSP performance benchmark
tools/core_ab_probe.c     deterministic M4 A/B metrics
```

## Provenance

The Poly-800 routing, NRO controller behaviour, ENV5S rate law, filter implementation, LFO routing and Dimension chorus are informed by/adapted from Bristol's GPL implementation by Nick Copeland. The port deliberately does not vendor Bristol's standalone engine, Brighton GUI, JACK/ALSA code or IPC architecture. See `docs/bristol-port-notes.md` and `docs/m4-ab-calibration.md` for the exact upstream baseline and porting notes.

No Korg ROMs, firmware, cassette images or other proprietary assets are required or distributed.

## Milestones

- **M0:** headless LV2 host smoke test — complete and validated in Ardour.
- **M1:** Poly-800 architecture core + original parameter surface — complete.
- **M1.1:** Release build defaults + real-time DSP optimisation + benchmark — complete.
- **M2:** Bristol source-level parameter/behaviour calibration + regression tests — complete.
- **M3:** versioned LV2 state interface + deterministic preset workflow + restore tests — complete and validated in Ardour.
- **M4:** Bristol MG/chorus source calibration + deterministic A/B infrastructure — current.
- **M4.1:** controlled external A/B against Bristol renders and/or hardware/reference captures.
- **M5:** reproducible factory-patch data, subject to asset/licensing verification.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and `docs/bristol-port-notes.md`.
