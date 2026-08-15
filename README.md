# ISLA Poly-800

**A ROM-grounded, hardware-informed, fully open-source LV2 reconstruction of the Korg Poly-800 MkI / EX-800 synthesis engine for GNU/Linux.**

Current project version: **0.6.0 — M6 ROM-grounded control engine**.

ISLA Poly-800 is not intended to be a generic subtractive synth that merely resembles a Poly-800. The project goal is narrower and harder: reproduce the original instrument's architecture, control laws, factory data and audible behaviour as closely as the surviving technical evidence allows, while keeping every buildable dependency free/open source and every remaining approximation explicit.

The result is a compact native LV2 instrument for Ardour and other GNU/Linux hosts, with the original sound-program parameter surface, the complete 64-program MkI factory bank, and a synthesis core whose most characteristic digital control behaviour is now derived from the EX-800 firmware itself.

## Why this reconstruction is different

Most software emulations begin with a plausible synthesizer architecture and tune it by ear. ISLA Poly-800 was developed in the opposite direction: whenever stronger evidence became available, earlier assumptions were removed even if they had sounded reasonable.

The current engine combines several independent evidence sources:

1. **Korg owner/service documentation** for topology, component roles, control ranges and hardware behaviour.
2. **EX-800 80C85 firmware reverse engineering** for digital control algorithms, lookup tables and fixed-point arithmetic.
3. **A checksum-valid preserved MkI factory cassette decode** for all 64 original factory programs and all 47 stored sound parameters.
4. **Reference/factory audio measurements** where firmware values pass through undocumented analog transfer functions.
5. **Bristol GPL source code** as free-software prior art for the Poly-800 signal architecture and filter implementation where it agrees with stock hardware.
6. **Explicit inference only where primary evidence ends.** Those areas are documented as approximations rather than presented as facts.

This evidence hierarchy is important. Bristol helped bootstrap the project, but it is not treated as the definition of a Poly-800. When Korg hardware documentation or recovered firmware contradicts a Bristol extension, the stock instrument wins.

See **[docs/reconstruction.md](docs/reconstruction.md)** for the complete technical reasoning and evidence boundary.

## Current fidelity status

### DCO / tone generation

The MkI does not contain conventional continuously variable analog oscillators, nor does the CPU generate audio samples. The tone generator hardware supplies square-wave footages at 16', 8', 4' and 2'. The waveform control changes the resistor-mix relationship between those square outputs.

ISLA reproduces the documented stock relationships:

```text
waveform 1: 1 : 1   : 1   : 1
waveform 2: 1 : 1/2 : 1/4 : 1/8
```

With the four footages enabled, waveform 2 produces the characteristic stepped saw-like spectrum of the hardware rather than an unrelated mathematical saw oscillator.

The engine preserves:

- WHOLE mode: 8 voices, DCO1 only;
- DOUBLE mode: 4 voices, DCO1 + DCO2;
- independent 16'/8'/4'/2' footage switches;
- DCO2 interval 0..12 semitones;
- MkI DCO2 detune direction and documented `-20 cents` full-scale endpoint;
- original additive footage behaviour rather than active-footage normalisation.

The exact intermediate detune errors produced by the original pulse-thinning circuit and exact MSM5232 divider imperfections remain candidates for future hardware-level refinement.

### MG / modulation generator — recovered from EX-800 ROM

M6 replaces the earlier Bristol sine-LFO approximation with the control algorithm recovered from the EX-800's 8 KiB 80C85 firmware.

P81 uses the original 16-byte frequency increment table at ROM `0x14EE`:

```text
01 02 03 04 06 08 0B 0E 11 15 19 1E 24 2B 34 3E
```

The firmware does not generate a sine wave. It advances an 8-bit accumulator, flips direction/sign on overflow, folds the accumulator into a triangular ramp and applies P83/P84 using the original four-bit fixed-point multiplication routine.

M6 therefore reproduces:

- the original table-driven 16-step MG frequency law;
- the firmware triangular waveform;
- the control-rate/sample-and-hold staircase instead of an artificially smooth audio-rate LFO;
- the original P83/P84 fixed-point depth law;
- P82 delay using the recovered firmware `LINEAR_TABLE` and counter law;
- bipolar MG-to-DCO modulation with the documented full-scale pitch range.

This change is one of the largest audible differences between the earlier reconstruction and M6 because the MG continuously shapes both pitch and filter movement in many factory programs.

### DEG envelopes

The EX-800 ROM also contains the actual `LOG_TABLE`, `LINEAR_TABLE` and envelope setup routines used by the three six-stage DEGs.

Those tables are preserved in the M6 source as control-domain evidence. They are **not** naively converted into audio amplitude. On the hardware, the CPU/DAC values continue through sample-and-hold circuitry and analog VCAs/filter control paths, so treating a ROM byte as a linear audio gain would create false precision.

The current audible DEG timing/level law therefore retains the M5.4/M5.4.1 calibration derived from Korg manual timing examples and factory-audio measurements while the recovered firmware tables define the next analog-modeling target.

### Shared VCF

The Poly-800's single shared/paraphonic filter is a defining part of the instrument. ISLA preserves the shared filter topology, keyboard tracking, DEG3 routing, envelope polarity/intensity and single/multi trigger behaviour.

The current nonlinear four-pole implementation is derived from the GPL Bristol Huovilainen filter path and is stable at both conventional rates and the 96 kHz AudioLink production path.

This is deliberately **not claimed to be a component-perfect NJM2069 model**. The exact digital-CV-to-cutoff transfer and component tolerances belong to the analog section and cannot be proven from firmware alone.

### Chorus

The stock MkI exposes chorus only as ON/OFF and uses an MN3209 BBD with MN3102 clocking. Bristol exposes additional Dimension-style speed/depth/scan controls that do not exist on the instrument, so those extensions are not used by the stock ISLA engine.

ISLA uses a fixed, restrained BBD-style approximation with no hidden editable controls. This can be refined further from direct hardware captures without changing the original parameter surface.

### Factory bank

All **64 original MkI factory programs** are generated from a checksum-valid cassette decode, not reconstructed by ear.

The repository stores a compact canonical CSV containing all 64 x 47 sound-program parameter values. The original cassette audio/binary package is not redistributed. A project-original decoder can reproduce the CSV from an external copy of the preserved cassette package and CI locks its SHA-256.

See **[factory/README.md](factory/README.md)**.

## How close is it?

No software project can responsibly claim bit-identical or component-identical Poly-800 output without a complete electrical model, exact component tolerances and measurements across multiple physical instruments.

The stronger claim ISLA can make is this:

> **For every behaviour for which reliable evidence has been recovered, ISLA attempts to reproduce the stock Poly-800 mechanism rather than substitute a generic synthesizer convention. Where the evidence stops, the approximation is named and isolated.**

Given the currently available service documentation, recovered EX-800 firmware behaviour, preserved factory data, reference audio and free-software prior art, M6 is as close as this project can defensibly make a reproducible open-source Poly-800 reconstruction without inventing undocumented facts.

That distinction matters more than marketing language. The project is intentionally designed so that any future hardware measurement can replace one isolated approximation without reopening already established behaviour.

## Signal architecture

```text
WHOLE
  8 x [DCO1 -> DEG1] -------------------+
                                         |
DOUBLE                                   +--> shared mix --> shared VCF --> chorus --> stereo
  4 x [DCO1 -> DEG1] -------------------+          ^
      [DCO2 -> DEG2] -------------------+          |
                                                   +-- DEG3 / noise / MG
```

The implementation is headless by design. Ardour owns MIDI routing, automation, sessions and the user interface; the core owns synthesis state and audio rendering.

All mutable DSP state is per LV2 instance. There are no shared oscillator/filter/LFO globals, so multiple tracks can safely run different programs simultaneously.

## Original parameter surface

The LV2 exposes the stock sound parameters directly:

- `11..18` DCO1 / mode
- `21..27` DCO2
- `31..33` interval / detune / noise
- `41..48` VCF / chorus
- `51..56` DEG1
- `61..66` DEG2
- `71..76` DEG3
- `81..84` MG

Hardware MIDI/global parameters `86..88` are intentionally omitted because the DAW owns MIDI channel/program routing.

## Build and test

Requirements on Debian/Ubuntu/Mint:

```bash
sudo apt install build-essential cmake pkg-config lv2-dev python3
```

Build:

```bash
git clone https://github.com/Interspock/isla-poly800.git
cd isla-poly800
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Install for the current user:

```bash
mkdir -p ~/.lv2
rm -rf ~/.lv2/isla-poly800.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Restart/rescan the host after installation.

The test suite covers DSP smoke/stability, DCO stock behaviour, calibrated DEG timing, host parameter refresh, M6 ROM-grounded MG arithmetic, LV2 state restore and the preserved pre-M6 regression path.

A benchmark is included:

```bash
./build/core_bench
```

The M6 control engine is cheaper than the earlier audio-rate sine path and remains comfortably real-time on older ISLA hardware.

## Source map

```text
src/isla_poly800.c               LV2/MIDI/state glue
src/poly800_core_m6.c            current M6 production core
src/poly800_core.c               frozen pre-M6 M4/M5 checkpoint
src/poly800_core_m2.inc          frozen M2 synthesis/filter baseline
src/poly800_core.h               public core API + stock parameter model

factory/                         canonical 64-program MkI data
tools/                           cassette decoder, preset generator, probes
tests/                           DSP/LV2 regression and ROM-MG acceptance tests
docs/reconstruction.md           current fidelity/evidence document
docs/architecture.md             current software/signal architecture
docs/bristol-port-notes.md       Bristol provenance and current boundaries
docs/history.md                  milestone/research history
```

## Documentation policy

`README.md`, `docs/reconstruction.md` and `docs/architecture.md` describe the **current engine**.

Files named after older milestones (`m3-*`, `m4-*`, `m5-*`) are retained as a research notebook. They document what was known at that point in development and may intentionally describe assumptions later superseded by stronger evidence. Read them as history, not as the current specification.

Git history and the `m6-rom-mg` branch preserve the transition from the Bristol-grounded MG to the ROM-grounded M6 engine for anyone who wants to audit the evolution.

## Provenance and licensing

ISLA Poly-800 is **GPL-3.0-or-later**.

Bristol-derived/adapted portions retain attribution to Nick Copeland and GPL-compatible licensing. Project-original reverse-engineering interpretation, wrapper code, cassette decoder, tests and documentation are part of this repository under the project license.

No proprietary Korg ROM, firmware dump, manual scan, cassette audio or third-party binary package is required by or distributed with the build.

See **[docs/bristol-port-notes.md](docs/bristol-port-notes.md)** and **[docs/reconstruction.md](docs/reconstruction.md)** for detailed provenance.