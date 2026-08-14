# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 1.1 — architecture port + performance pass.**

Milestone 0 validated the complete host path on the real ISLA machine: Ardour discovered the LV2 bundle, received MIDI, produced audio and exposed generic host controls.

M1 replaced the M0 sine smoke-test engine with a dedicated, per-instance Poly-800 core. M1.1 keeps that architecture and parameter surface intact while removing avoidable work from the real-time DSP path.

M1/M1.1 are **not yet a claim of calibrated/bit-accurate emulation**. Exact DCO behaviour, detune, modulation depth, filter scaling and chorus constants will be compared against Bristol and hardware documentation in later milestones.

## M1 synthesis architecture

- WHOLE mode: 8 voices, one DCO/DEG per voice.
- DOUBLE mode: 4 voices, two DCO/DEG paths per voice.
- DCO footages 16', 8', 4' and 2'.
- Square and saw/step harmonic weighting modes.
- DCO2 interval and detune.
- Three six-stage DEGs (Attack, Decay, Break Point, Slope, Sustain, Release).
- One shared/paraphonic VCF and DEG3 path by default, matching the original architecture.
- VCF cutoff, resonance, keyboard tracking, envelope polarity/intensity and single/multi trigger.
- Noise through DEG3.
- MG/LFO frequency, delay, DCO depth and VCF depth.
- Chorus on/off.
- Independent state for every LV2 instance; no mutable DSP globals.

The LV2 exposes the original sound-program parameter numbers (11..84) directly through Ardour's generic editor. MIDI-specific original parameters 86..88 are intentionally omitted because the DAW owns MIDI channel/program routing.

## M1.1 performance work

The original M1 proof intentionally prioritised clarity over speed and still did expensive work inside the sample loop. M1.1 changes that without changing the public parameter model:

- Release builds are now the default when no CMake build type is specified.
- MIDI note frequencies and per-voice DCO phase increments are cached instead of evaluating `pow()` for every harmonic on every sample.
- DEG rate/level constants are derived when parameters change instead of recalculated per sample.
- VCF static coefficients and modulation depths are cached.
- MG pitch modulation uses a bounded cubic approximation over its +/-1 semitone range instead of a per-sample exponential.
- MG and chorus LFOs use quadrature recurrence; `sin()`/`cos()` are no longer called per sample.
- shared-filter keyboard tracking scans voices once per render segment, only recalculating when the tracked voice actually dies.
- the voice-normalisation square root is replaced with a fixed table for the 1..8 voice range.
- noise generation is skipped when its level is zero.

A small reproducible core benchmark is included so performance can be compared on the actual ISLA machines.

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

With a single-config generator such as Unix Makefiles, the project now defaults to `Release`. To request another build explicitly, use e.g. `-DCMAKE_BUILD_TYPE=Debug`.

Run the DSP benchmark:

```bash
./build/core_bench
```

It reports realtime multiplier and nanoseconds/sample for WHOLE 1/4/8 voices and DOUBLE 1/4 voices with and without chorus. The numbers are intended for before/after comparisons on the same machine, not as a cross-machine absolute benchmark.

The build produces:

```text
build/isla-poly800.lv2/
├── isla-poly800.so
├── isla-poly800.ttl
└── manifest.ttl
```

Install for the current user:

```bash
mkdir -p ~/.lv2
rm -rf ~/.lv2/isla-poly800.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Then rescan plugins in Ardour and look for **ISLA Poly-800 (M1 Architecture Port)** as a MIDI instrument.

## Source layout

```text
src/isla_poly800.c     LV2/MIDI host glue only
src/poly800_core.c     per-instance synthesis core
src/poly800_core.h     core API and stock parameter model
lv2/                   LV2 metadata / generic controls
tests/core_smoke.c     headless DSP smoke/stress test
tests/core_bench.c     reproducible DSP performance benchmark
```

## Provenance

The Poly-800 routing, envelope-rate behaviour and filter implementation are informed by/adapted from Bristol's GPL implementation by Nick Copeland. The port deliberately does not vendor Bristol's standalone engine, Brighton GUI, JACK/ALSA code or IPC architecture. See `docs/bristol-port-notes.md` for the exact upstream baseline and porting notes.

No Korg ROMs, firmware, cassette images or other proprietary assets are required or distributed.

## Milestones

- **M0:** headless LV2 host smoke test — complete and validated in Ardour.
- **M1:** Poly-800 architecture core + original parameter surface — complete.
- **M1.1:** Release build defaults + real-time DSP optimisation + benchmark — current.
- **M2:** parameter/behaviour calibration and broader automated DSP tests.
- **M3:** explicit LV2 state/preset workflow and reliable save/restore validation.
- **M4:** A/B comparison against Bristol/reference recordings and fidelity corrections.
- **M5:** reproducible factory-patch data, subject to asset/licensing verification.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and `docs/bristol-port-notes.md`.
