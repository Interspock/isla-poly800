# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument implementing the Korg Poly-800 synthesis architecture for the ISLA DAW environment.

## Status

**Milestone 4 — Bristol A/B calibration infrastructure and remaining source-level modulation/chorus calibration.**

M0 validated LV2/MIDI/audio in Ardour. M1 implemented the Poly-800 topology and stock parameter surface. M1.1 made it practical on older ISLA hardware. M2 calibrated the DCO/footage/detune/filter path against Bristol source. M3 added reliable LV2 state/preset workflow. M4 now closes Bristol's MG-to-DCO routing and Dimension chorus behaviour and adds a deterministic A/B probe.

This remains a source-faithful software emulation effort, **not a claim of circuit-perfect, bit-identical, or sample-identical Korg hardware emulation**. A later listening/measurement pass can compare the same probe programs against Bristol renders and real/reference Poly-800 captures.

## Synthesis architecture

- WHOLE mode: 8 voices, one DCO/DEG per voice.
- DOUBLE mode: 4 voices, two DCO/DEG paths per voice.
- DCO footages 16', 8', 4' and 2'.
- Square and saw/ramp wave choices.
- DCO2 interval and detune.
- Three six-stage DEGs (Attack, Decay, Break Point, Slope, Sustain, Release).
- One shared/paraphonic VCF and DEG3 path.
- VCF cutoff, resonance, keyboard tracking, envelope polarity/intensity and single/multi trigger.
- Noise through DEG3.
- Bristol-calibrated MG frequency, delay, DCO depth and VCF depth routing.
- Bristol Dimension-style chorus behind stock P48 on/off.
- Independent mutable state for every LV2 instance.

The LV2 exposes the original sound-program parameter numbers (11..84) directly through Ardour's generic editor. MIDI-specific original parameters 86..88 are intentionally omitted because the DAW owns MIDI channel/program routing.

## M4 calibration

M4 uses Bristol source commit `116fb8a2d21727676e21db5f1efe295c1ea22d61` as the source oracle.

- P83 now follows Bristol's `value² × 4` DCO-modulation law.
- The LFO modulation bus now follows Bristol's `(1 + sine) × delayed_gain` unipolar path before DCO/VCF modulation.
- P81's cubic frequency law and P82's up-to-15-second delay/fade were already correct in M2 and are retained.
- P84 retains Bristol's `value² × 8` VCF coefficient but now receives the same unipolar LFO bus as Bristol.
- P48 now invokes an adaptation of Bristol's stereo Dimension chorus operator rather than the temporary M1 dual-tap chorus.
- Because Bristol's speed/depth/scan controls are extensions absent from the stock Poly-800 surface, M4 fixes them to the common values in Bristol's supplied programs 11/12/13. See `docs/m4-ab-calibration.md`.
- P48 OFF remains an explicit hard bypass in ISLA.

`src/poly800_core_m2.inc` freezes the M2 DSP base. The small `src/poly800_core.c` M4 layer overrides only the remaining calibrated behaviours, making the difference auditable and easy to compare.

## Tests and A/B probe

Build and run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/core_ab_probe
./build/core_bench
```

`core_m4` checks that high P83 produces Bristol-scale deep frequency modulation, that P48 OFF remains mono/dry, that the Dimension chorus creates stereo decorrelation, and that output stays finite.

`core_ab_probe` renders a fixed C4 program at 48 kHz and prints RMS, zero crossings and stereo-difference RMS for P83 0/7/15 and chorus ON. The probe is deliberately asset-free so the same protocol can be reproduced against other implementations or hardware captures.

## Build/install

Requirements on Debian/Ubuntu/Mint:

```bash
sudo apt install build-essential cmake pkg-config lv2-dev
```

Install for the current user after building:

```bash
mkdir -p ~/.lv2
rm -rf ~/.lv2/isla-poly800.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Restart/rescan Ardour. The stable plugin name is **ISLA Poly-800**; the LV2 URI and port layout remain unchanged from the earlier milestones.

## Presets/state

M3 provides LV2 State support plus two deterministic utility presets:

- `Init Whole`
- `Double Fifth Test`

They are test/utility programs, not Korg factory patches.

## Source layout

```text
src/isla_poly800.c        LV2/MIDI/state host glue
src/poly800_core.c        M4 calibration layer
src/poly800_core_m2.inc   frozen M2 synthesis/filter base
src/poly800_core.h        core API and stock parameter model
lv2/                      LV2 metadata/state/presets
tests/core_smoke.c        DSP smoke/stress test
tests/core_calibration.c  M2 fidelity regressions
tests/core_m4.c           M4 MG/chorus regressions
tests/lv2_state_smoke.c   LV2 state round-trip
tests/core_bench.c        performance benchmark
tools/core_ab_probe.c     deterministic A/B metrics
```

## Provenance

The Poly-800 routing, NRO behaviour, ENV5S law, Huovilainen filter, LFO routing and Dimension chorus are informed by/adapted from Bristol GPL source by Nick Copeland. The port does not vendor Bristol's standalone engine, Brighton GUI, JACK/ALSA layer or IPC architecture. See `docs/bristol-port-notes.md` and `docs/m4-ab-calibration.md`.

No Korg ROMs, firmware, cassette images or other proprietary assets are required or distributed.

## Milestones

- **M0:** LV2/MIDI/audio smoke test — complete.
- **M1:** Poly-800 architecture + stock parameter surface — complete.
- **M1.1:** performance optimisation/benchmark — complete.
- **M2:** DCO/filter/source-level calibration — complete.
- **M3:** LV2 state and preset workflow — complete and validated in Ardour.
- **M4:** remaining Bristol MG/chorus calibration + reproducible A/B probe — current.
- **M4.1:** controlled external A/B against Bristol renders and/or hardware/reference captures.
- **M5:** reproducible factory-patch data, subject to asset/licensing verification.
- **M6:** optional refinements; custom GUI remains non-essential.

## License

GPL-3.0-or-later. Bristol-derived/adapted portions retain Bristol attribution and GPL-compatible licensing. See `LICENSE` and project documentation.
