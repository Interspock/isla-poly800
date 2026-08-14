# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 5 — reproducible MkI factory-preset bank: infrastructure complete, source transcription in progress.**

M0 validated the LV2/MIDI/audio path in Ardour. M1 implemented the Poly-800 topology and parameter surface. M1.1 made the DSP suitable for real-time use on older ISLA hardware. M2 moved the actual synthesis behaviour closer to Bristol's GPL Poly-800 implementation. M3 made persistence explicit with LV2 State and deterministic presets. M4 closed MG-to-DCO routing and corrected the stock-visible chorus path. M5 now adds a provenance-aware pipeline for the original 64 MkI factory programs.

The M5 generator/catalog/validation path is implemented and CI-enforced. The 64 names/slots are catalogued; parameter rows are added only after complete verification against preserved factory sources. Until `python3 tools/generate_factory_presets.py --check --require-complete` passes, the factory bank is explicitly not considered complete.

The LV2 URI and port layout remain unchanged and M3 state/preset behaviour is preserved.

The emulator is still **not a claim of circuit-perfect, bit-identical, or sample-identical Poly-800 hardware emulation**. The M4 probe and M5 factory-program pipeline are intended to make later A/B comparison against real/reference Poly-800 captures reproducible rather than subjective guesswork.

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
- Fixed stock-style BBD chorus on/off.
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

MG calibration follows the Bristol source baseline:

- P81 remains Bristol's cubic LFO law: `0.1 + value^3 * 20 Hz`.
- P82 remains Bristol's 0..15 second delay followed by an equal-duration fade-in.
- P83 follows Bristol's `value^2 * 4` modulation coefficient.
- Bristol offsets its sine LFO by +1 before the delayed gain DCA, so the DCO/VCF modulation bus is unipolar: `(1 + sine) * fade_gain`.
- DCO frequency follows Bristol's multiplicative `frequency *= 1 + lfo_bus * vcomod` path. High P83 values are therefore intentionally much deeper than conventional vibrato-in-semitones controls.
- P84 retains Bristol's `value^2 * 8` coefficient and receives that same unipolar LFO bus.

The chorus required a different fidelity decision. Bristol adds hidden editable speed/depth/scan controls (58/68/78) that do not exist on the stock Poly-800. An initial literal Dimension port using those hidden values produced an obvious sweep in Ardour. The MkI Korg hardware instead uses a fixed MN3209/MN3102 BBD chorus controlled only by P48.

The stock path now uses a conservative fixed BBD-style approximation:

```text
rate         0.55 Hz
centre delay 6.8 ms
mod depth    +/-0.6 ms
wet/dry      25% / 75%
feedback     none
```

It keeps the dry signal present, adds restrained complementary stereo delay taps, and keeps delay/LFO state running through bypass. The constants are hardware-informed calibration values and can be refined later from a real-hardware capture without exposing non-stock controls.

See `docs/m4-ab-calibration.md` and `docs/bristol-port-notes.md` for the source decision and regression protocol.

`tests/core_m4.c` locks the MG modulation scale, dry bypass, restrained stereo chorus behaviour, an anti-sweep zero-crossing guard, level sanity and the 96 kHz AudioLink path.

## M5 factory preset pipeline

M5 deliberately separates **factory facts** from third-party/Korg source assets.

- `factory/poly800-mk1-catalog.csv` contains the 64 original program slots/names, 11..88 using digits 1..8.
- `factory/poly800-mk1-verified.csv` is the canonical table for complete, verified parameter transcriptions.
- `tools/generate_factory_presets.py` validates every row and generates `lv2/factory-presets.ttl`.
- Every generated factory preset writes all 49 plugin controls deterministically: the 47 stock sound parameters plus project-level `gain=0.32` and `tune=0.0`.
- Partial programs, duplicates, invalid slots and values outside parameter ranges are rejected.
- Program locations 86/87/88 are valid presets; they are distinct from original MIDI/global parameter numbers 86..88, which remain outside the plugin's sound-program state.
- CI runs the generator in `--check` mode so checked-in LV2 data cannot drift from the canonical table.
- Korg scans/manuals, cassette WAVs, ROM/firmware and raw dumps are not vendored merely because they are publicly downloadable.

The source archive identifies both `Poly-800 11-88.xls` (original MkI names/settings) and a six-page high-resolution preload patch sheet. Those sources are used for transcription/cross-checking, not copied into the repository.

Development check:

```bash
python3 tools/generate_factory_presets.py --check
```

M5 completion gate:

```bash
python3 tools/generate_factory_presets.py --check --require-complete
```

See `factory/README.md` for provenance and redistribution rules.

## Reproducible A/B probe

M4 adds:

```bash
./build/core_ab_probe
```

It renders a fixed C4 program at 48 kHz and prints RMS, zero-crossing count and stereo-difference RMS for dry P83=0/7/15 and the stock BBD-style chorus. The probe uses no proprietary audio assets; its output is intended as a stable experiment protocol for future Bristol/hardware comparisons.

After the M5 bank is complete, the same factory program numbers can be rendered by ISLA and by a real Poly-800 under an identical MIDI protocol for M5.1/M4.1 A/B work.

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
├── presets.ttl
└── factory-presets.ttl
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
src/isla_poly800.c                 LV2/MIDI glue + versioned state interface
src/poly800_core.c                 M4 calibration layer
src/poly800_core_m2.inc            frozen M2 synthesis/filter implementation
src/poly800_core.h                 core API and stock parameter model
factory/poly800-mk1-catalog.csv   64 MkI factory program slots/names
factory/poly800-mk1-verified.csv  canonical verified parameter values
factory/README.md                  M5 provenance and redistribution policy
lv2/manifest.ttl                   plugin/preset discovery
lv2/state.ttl                      LV2 state-interface metadata
lv2/presets.ttl                    deterministic M3 utility presets
lv2/factory-presets.ttl            generated M5 factory preset bank
lv2/isla-poly800.ttl               ports / generic controls
tests/core_smoke.c                 headless DSP smoke/stress test
tests/core_calibration.c           M2 fidelity regression tests
tests/core_m4.c                    M4 MG/chorus + 96 kHz regression tests
tests/lv2_state_smoke.c            M3 LV2 state save/restore test
tests/core_bench.c                 reproducible DSP performance benchmark
tools/core_ab_probe.c              deterministic M4 A/B metrics
tools/generate_factory_presets.py  M5 validation/generation tool
```

## Provenance

The Poly-800 routing, NRO controller behaviour, ENV5S rate law, filter implementation and MG/LFO routing are informed by/adapted from Bristol's GPL implementation by Nick Copeland. The fixed chorus path is hardware-informed from the original Korg MkI BBD architecture rather than Bristol's non-stock hidden Dimension controls. The port deliberately does not vendor Bristol's standalone engine, Brighton GUI, JACK/ALSA code or IPC architecture. See `docs/bristol-port-notes.md` and `docs/m4-ab-calibration.md` for the exact baseline and porting notes.

M5 adds only project-original structured transcriptions of factual factory program settings after verification. No Korg ROMs, firmware, cassette images, manual scans or other proprietary source assets are required or distributed.

## Milestones

- **M0:** headless LV2 host smoke test — complete and validated in Ardour.
- **M1:** Poly-800 architecture core + original parameter surface — complete.
- **M1.1:** Release build defaults + real-time DSP optimisation + benchmark — complete.
- **M2:** Bristol source-level parameter/behaviour calibration + regression tests — complete.
- **M3:** versioned LV2 state interface + deterministic preset workflow + restore tests — complete and validated in Ardour.
- **M4:** Bristol MG calibration + corrected fixed stock-style chorus + deterministic A/B infrastructure — complete and validated in Ardour.
- **M5:** reproducible MkI factory preset pipeline — infrastructure complete; verified 64-program parameter transcription in progress.
- **M5.1:** controlled same-patch A/B protocol using the completed factory bank.
- **M4.1:** external A/B against Bristol renders and/or hardware/reference captures.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and `docs/bristol-port-notes.md`.
