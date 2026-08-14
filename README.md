# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 2 — Bristol calibration pass.**

M0 validated the LV2/MIDI/audio path in Ardour. M1 implemented the Poly-800 topology and parameter surface. M1.1 made the DSP suitable for real-time use on older ISLA hardware. M2 now moves the actual synthesis behaviour closer to Bristol's GPL Poly-800 implementation while keeping all state instance-local and the plugin GUI-free.

M2 is still **not a claim of circuit-perfect, bit-identical, or sample-identical emulation**. The verified source-level calibrations in this milestone are DCO waveform selection, additive footages, DCO2 fine detune, shared-filter algorithm/scaling, and the 48/96 kHz filter paths. Absolute MG-DCO depth and the stock chorus constants still need controlled A/B listening/measurement.

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
- Independent state for every LV2 instance; no mutable DSP globals.

The LV2 exposes the original sound-program parameter numbers (11..84) directly through Ardour's generic editor. MIDI-specific original parameters 86..88 are intentionally omitted because the DAW owns MIDI channel/program routing.

## M2 calibration work

The M2 pass was checked against Bristol source at commit `116fb8a2d21727676e21db5f1efe295c1ea22d61`.

- P12/P22 now select actual square or saw generation; M1's temporary "saw/step weighting" interpretation is gone.
- The 16'/8'/4'/2' footages are additive, as in Bristol NRO, rather than normalised by the number enabled.
- P32 DCO2 detune now follows NRO's linear frequency-ratio interpolation from unison to one semitone across values 0..3.
- The oscillator bus follows Bristol's Poly-800 routing gain before the shared filter.
- The shared VCF now uses Bristol's filter type 4 Huovilainen path rather than M1's lightweight Chamberlin approximation.
- Below 88 kHz the VCF uses Bristol's internally 2x-oversampled branch; at 88 kHz and above it switches to the high-rate branch. This explicitly covers both 48 kHz testing and the AudioLink 96 kHz path.
- VCF cutoff, resonance, keyboard tracking, DEG3 amount/polarity and MG-to-VCF controller scaling follow Bristol's Poly-800 mappings.
- ENV5S/ADBSSR timing continues to use Bristol's squared-rate law.
- M1.1 caches and Release-build optimisation remain in place.

`tests/core_calibration.c` locks the verified behaviour with regression checks for square/saw distinction, additive footages, the P32 detune endpoint, and finite 96 kHz filter operation.

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
└── manifest.ttl
```

Install for the current user:

```bash
mkdir -p ~/.lv2
rm -rf ~/.lv2/isla-poly800.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Then restart/rescan Ardour. The LV2 URI and port layout are unchanged from M1, so existing host integration remains compatible. The display name may still read **ISLA Poly-800 (M1 Architecture Port)** during this calibration stage.

## Source layout

```text
src/isla_poly800.c       LV2/MIDI host glue only
src/poly800_core.c       per-instance synthesis core
src/poly800_core.h       core API and stock parameter model
lv2/                     LV2 metadata / generic controls
tests/core_smoke.c       headless DSP smoke/stress test
tests/core_calibration.c M2 fidelity regression tests
tests/core_bench.c       reproducible DSP performance benchmark
```

## Provenance

The Poly-800 routing, NRO controller behaviour, ENV5S rate law and filter implementation are informed by/adapted from Bristol's GPL implementation by Nick Copeland. The port deliberately does not vendor Bristol's standalone engine, Brighton GUI, JACK/ALSA code or IPC architecture. See `docs/bristol-port-notes.md` for the exact upstream baseline and porting notes.

No Korg ROMs, firmware, cassette images or other proprietary assets are required or distributed.

## Milestones

- **M0:** headless LV2 host smoke test — complete and validated in Ardour.
- **M1:** Poly-800 architecture core + original parameter surface — complete.
- **M1.1:** Release build defaults + real-time DSP optimisation + benchmark — complete.
- **M2:** Bristol source-level parameter/behaviour calibration + regression tests — current.
- **M2.1:** controlled A/B calibration of MG-DCO depth, chorus constants and remaining audible scaling.
- **M3:** explicit LV2 state/preset workflow and reliable save/restore validation.
- **M4:** broader A/B comparison against Bristol/reference recordings and fidelity corrections.
- **M5:** reproducible factory-patch data, subject to asset/licensing verification.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and `docs/bristol-port-notes.md`.
