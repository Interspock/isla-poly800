# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 5 — complete reproducible Korg Poly-800 MkI factory bank.**

M0 validated the LV2/MIDI/audio path in Ardour. M1 implemented the Poly-800 topology and parameter surface. M1.1 made the DSP suitable for real-time use on older ISLA hardware. M2 moved the synthesis behaviour closer to Bristol's GPL Poly-800 implementation. M3 made persistence explicit with LV2 State and deterministic presets. M4 closed MG-to-DCO routing and corrected the stock-visible chorus path. M5 adds all 64 original MkI factory programs from a checksum-valid factory cassette decode, independently cross-checked against the preserved three-sheet settings spreadsheet.

The LV2 URI and port layout remain unchanged and M3 state/preset behaviour is preserved.

The emulator is still **not a claim of circuit-perfect, bit-identical, or sample-identical Poly-800 hardware emulation**. The M4 probe and M5 same-patch bank now give us a reproducible basis for controlled A/B comparison against real/reference Poly-800 captures.

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
- DCO frequency follows Bristol's multiplicative `frequency *= 1 + lfo_bus * vcomod` path.
- P84 retains Bristol's `value^2 * 8` coefficient and receives that same unipolar LFO bus.

The chorus required a different fidelity decision. Bristol adds hidden editable speed/depth/scan controls (58/68/78) that do not exist on the stock Poly-800. An initial literal Dimension port produced an obvious sweep in Ardour. The MkI Korg hardware instead uses a fixed MN3209/MN3102 BBD chorus controlled only by P48.

The stock path now uses a conservative fixed BBD-style approximation:

```text
rate         0.55 Hz
centre delay 6.8 ms
mod depth    +/-0.6 ms
wet/dry      25% / 75%
feedback     none
```

See `docs/m4-ab-calibration.md` and `docs/bristol-port-notes.md` for the source decision and regression protocol.

## M5 factory preset bank

M5 provides the full **64-program Korg Poly-800 MkI factory bank**.

- `factory/poly800-mk1-catalog.csv` contains all original program slots/names.
- `factory/poly800-mk1-verified.csv` contains all 64 x 47 stock sound-parameter values.
- The numeric source of truth is a fully framed, checksum-valid decode of a preserved original-factory cassette image.
- `tools/decode_poly800_factory_tape.py` reproduces that decode from an external `.sdp` or mono PCM16 WAV without third-party Python dependencies.
- The preserved three-sheet `Poly-800 11-88.xls` independently cross-checks the decode: 2780 numeric cells agree exactly; 46 cells contain human transcription/column-shift discrepancies and 182 inactive/don't-care cells are marked `x`.
- The checksum-valid cassette is authoritative for those discrepancies and also recovers the stored DCO2/DEG2 values hidden by `x` in WHOLE-mode spreadsheet rows.
- `tools/generate_factory_presets.py` validates all ranges/completeness and converts the compact CSV to LV2 presets.
- CMake generates `build/isla-poly800.lv2/factory-presets.ttl` at build time; the roughly 200 kB generated Turtle is deliberately not duplicated as source in Git.
- Every factory preset sets all 47 stock sound controls plus deterministic project controls `gain=0.32` and `tune=0.0`.
- CI requires all 64 programs and checks that Lilv discovers the first and last factory presets.

Source-data validation:

```bash
python3 tools/generate_factory_presets.py --check --require-complete
```

Optional reproduction from an external factory cassette package:

```bash
python3 tools/decode_poly800_factory_tape.py Poly-800_Factory.sdp \
    -o /tmp/poly800-decoded.csv
cmp /tmp/poly800-decoded.csv factory/poly800-mk1-verified.csv
```

No Korg scans/manuals, cassette WAVs, SoundDiviner packages, ROM/firmware or raw binary dumps are vendored. See `factory/README.md` for hashes, provenance and the decoding method.

## Reproducible A/B probe

M4 adds:

```bash
./build/core_ab_probe
```

It renders a fixed C4 program at 48 kHz and prints RMS, zero-crossing count and stereo-difference RMS for dry P83=0/7/15 and the stock BBD-style chorus.

With M5 complete, the same factory program number can now be selected on ISLA and on real/reference hardware under an identical MIDI/rendering protocol for the next A/B stage.

## Performance

The project defaults to a Release build when using a single-config generator such as Unix Makefiles. A core benchmark is included:

```bash
./build/core_bench
```

It reports WHOLE 1/4/8 voices and DOUBLE 1/4 voices with and without chorus. Benchmark numbers are intended for comparisons on the same machine, not as absolute cross-machine ratings.

## Build

Requirements on Debian/Ubuntu/Mint:

```bash
sudo apt install build-essential cmake pkg-config lv2-dev python3
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
└── factory-presets.ttl    # generated from the canonical CSV
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
src/isla_poly800.c                    LV2/MIDI glue + versioned state interface
src/poly800_core.c                    M4 calibration layer
src/poly800_core_m2.inc               frozen M2 synthesis/filter implementation
src/poly800_core.h                    core API and stock parameter model
factory/poly800-mk1-catalog.csv       64 MkI factory program slots/names
factory/poly800-mk1-verified.csv      complete 64 x 47 factory parameter table
factory/README.md                     M5 provenance, hashes and redistribution policy
lv2/manifest.ttl                      plugin/preset discovery
lv2/state.ttl                         LV2 state-interface metadata
lv2/presets.ttl                       deterministic M3 utility presets
lv2/isla-poly800.ttl                  ports / generic controls
tests/core_smoke.c                    headless DSP smoke/stress test
tests/core_calibration.c              M2 fidelity regression tests
tests/core_m4.c                       M4 MG/chorus + 96 kHz regression tests
tests/lv2_state_smoke.c               M3 LV2 state save/restore test
tests/core_bench.c                    reproducible DSP performance benchmark
tools/core_ab_probe.c                 deterministic M4 A/B metrics
tools/decode_poly800_factory_tape.py  reproducible external cassette decoder
tools/generate_factory_presets.py     M5 validation/LV2 generation tool
```

## Provenance

The Poly-800 routing, NRO controller behaviour, ENV5S rate law, filter implementation and MG/LFO routing are informed by/adapted from Bristol's GPL implementation by Nick Copeland. The fixed chorus path is hardware-informed from the original Korg MkI BBD architecture rather than Bristol's non-stock hidden Dimension controls.

M5 adds project-original decoding/generation tools and a structured table of factual factory program settings recovered from independently preserved source material. Proprietary source assets themselves are not required or distributed.

## Milestones

- **M0:** headless LV2 host smoke test — complete and validated in Ardour.
- **M1:** Poly-800 architecture core + original parameter surface — complete.
- **M1.1:** Release build defaults + real-time DSP optimisation + benchmark — complete.
- **M2:** Bristol source-level parameter/behaviour calibration + regression tests — complete.
- **M3:** versioned LV2 state interface + deterministic preset workflow + restore tests — complete and validated in Ardour.
- **M4:** Bristol MG calibration + corrected fixed stock-style chorus + deterministic A/B infrastructure — complete and validated in Ardour.
- **M5:** complete checksum-verified 64-program MkI factory bank + reproducible decoder/generator — complete.
- **M5.1:** controlled same-patch A/B protocol using the completed factory bank.
- **M4.1:** external A/B against Bristol renders and/or hardware/reference captures.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and `docs/bristol-port-notes.md`.
