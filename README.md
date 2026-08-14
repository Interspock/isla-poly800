# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 5 — corrected checksum-derived Korg Poly-800 MkI factory bank, pending final Ardour revalidation.**

M0 validated the LV2/MIDI/audio path in Ardour. M1 implemented the Poly-800 topology and parameter surface. M1.1 made the DSP suitable for real-time use on older ISLA hardware. M2 moved the synthesis behaviour closer to Bristol's GPL Poly-800 implementation. M3 made persistence explicit with LV2 State and deterministic presets. M4 closed MG-to-DCO routing and corrected the stock-visible chorus path. M5 adds all 64 original MkI factory programs from a checksum-valid factory cassette decode.

An initial M5 Ardour smoke test exposed that the first checked-in factory table, while complete and range-valid, was not byte-for-byte the cassette decoder output. The canonical table is now direct decoder output and CI locks its SHA-256. M5 remains pending host revalidation until the corrected presets are auditioned again in Ardour.

The LV2 URI and port layout remain unchanged and M3 state/preset behaviour is preserved.

The emulator is still **not a claim of circuit-perfect, bit-identical, or sample-identical Poly-800 hardware emulation**. The M4 probe and M5 same-patch bank give us a reproducible basis for controlled A/B comparison against real/reference Poly-800 captures.

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

The build produces `build/isla-poly800.lv2/`, including the generated 64-preset `factory-presets.ttl`.

Install for the current user:

```bash
mkdir -p ~/.lv2
rm -rf ~/.lv2/isla-poly800.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Then restart/rescan Ardour.

## Factory bank integrity

The canonical data lives in:

```text
factory/poly800-mk1-catalog.csv
factory/poly800-mk1-verified.csv
```

The latter must be exactly the output of `tools/decode_poly800_factory_tape.py` for the checksum-valid reference cassette image. Its canonical SHA-256 is:

```text
13027b2b51138456e0d05ed30a893b12f48ff079020e1df444cb5eef59466619
```

Validate with:

```bash
python3 tools/generate_factory_presets.py --check --require-complete
sha256sum factory/poly800-mk1-verified.csv
```

See `factory/README.md` for cassette hashes, provenance, the decoder workflow and redistribution policy.

## Reproducible A/B probe

```bash
./build/core_ab_probe
```

It renders a fixed C4 program at 48 kHz and prints RMS, zero-crossing count and stereo-difference RMS. Once M5 is revalidated in Ardour, the same factory program number can be selected on ISLA and on real/reference hardware for controlled A/B work.

## Performance

```bash
./build/core_bench
```

It reports WHOLE 1/4/8 voices and DOUBLE 1/4 voices with and without chorus. Benchmark numbers are intended for comparisons on the same machine.

## Source layout

```text
src/isla_poly800.c                    LV2/MIDI glue + versioned state interface
src/poly800_core.c                    M4 calibration layer
src/poly800_core_m2.inc               frozen M2 synthesis/filter implementation
src/poly800_core.h                    core API and stock parameter model
factory/poly800-mk1-catalog.csv       64 MkI factory program slots/names
factory/poly800-mk1-verified.csv      canonical cassette-decoded 64 x 47 table
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

M5 adds project-original decoding/generation tools and a structured table of factual factory program settings recovered from independently preserved source material. Proprietary source assets themselves are not distributed.

## Milestones

- **M0:** headless LV2 host smoke test — complete and validated in Ardour.
- **M1:** Poly-800 architecture core + original parameter surface — complete.
- **M1.1:** Release build defaults + real-time DSP optimisation + benchmark — complete.
- **M2:** Bristol source-level parameter/behaviour calibration + regression tests — complete.
- **M3:** versioned LV2 state interface + deterministic preset workflow + restore tests — complete and validated in Ardour.
- **M4:** Bristol MG calibration + corrected fixed stock-style chorus + deterministic A/B infrastructure — complete and validated in Ardour.
- **M5:** checksum-derived 64-program MkI factory bank — corrected after first host smoke test; pending Ardour revalidation.
- **M5.1:** controlled same-patch A/B protocol using the completed factory bank.
- **M4.1:** external A/B against Bristol renders and/or hardware/reference captures.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and `docs/bristol-port-notes.md`.
