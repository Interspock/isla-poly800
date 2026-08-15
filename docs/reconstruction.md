# Reconstruction methodology and fidelity boundary

This document describes the current technical basis of ISLA Poly-800 v0.6/M6. It is the canonical explanation of **why the engine behaves as it does**, which evidence is considered authoritative, and which parts remain approximations.

The project does not use “sounds close” as its primary specification. Listening remains essential, but implementation decisions are ranked by evidence.

## 1. Evidence hierarchy

When two sources disagree, ISLA uses the following order of confidence.

### 1. Korg hardware and owner/service documentation

This establishes facts such as:

- WHOLE vs DOUBLE polyphony;
- shared/paraphonic filter architecture;
- MSM5232 tone-generator role and footage outputs;
- waveform synthesis by mixing square-wave footages;
- DCO2 detune direction/range;
- CPU/DAC/sample-and-hold control of envelope/filter paths;
- MN3209/MN3102 chorus hardware;
- component identities and signal routing.

### 2. EX-800 firmware reverse engineering

The EX-800 shares the MkI synthesis architecture and exposes the machine's digital control logic without the keyboard hardware. The recovered 8 KiB ROM is 80C85 code.

Firmware is strongest evidence for:

- parameter-to-control lookup tables;
- integer state machines;
- modulation waveform construction;
- fixed-point arithmetic;
- scheduler behaviour;
- digital delay counters;
- envelope-control setup before the analog path.

Firmware is **not** sufficient to prove what an analog VCA or NJM2069 does with the resulting control voltage.

### 3. Checksum-valid factory data

A preserved MkI factory cassette was demodulated and checksum-validated. This gives exact stored values for all 64 programs and avoids tuning the software around hand-transcribed patch sheets.

### 4. Real/reference audio

Audio becomes authoritative where the digital control value is known but the downstream analog transfer is undocumented. The current DEG calibration is the clearest example.

### 5. Bristol GPL source

Bristol is exceptionally valuable prior art. It supplied a coherent Poly-800 topology, envelope sequencing and a mature nonlinear filter implementation long before the ROM work was available.

But Bristol also contains extensions and interpretations that are not stock Korg behaviour. It is therefore a source, not an oracle.

### 6. Explicit inference

Where no stronger evidence exists, ISLA chooses the smallest practical approximation and records it as such. The goal is to make future replacement easy rather than bury assumptions inside unrelated code.

---

## 2. DCO reconstruction

### What the hardware actually does

The Poly-800 MkI does not generate a conventional free-running analog saw oscillator for each voice. Its tone-generator section uses the MSM5232 to provide square-wave pitch/footage outputs. Service documentation identifies the 16', 8', 4' and 2' buses and the following waveform-synthesis stage.

The EX-800 firmware reinforces this interpretation: routines such as `TG_SET`, `TG_SET_DCO1`, `TG_SET_DCO2`, `TG_SET_HARMONIC` and `SET_WAVE_DETUNE` configure tone-generator/latch state. They do not synthesize PCM samples.

The waveform/detune latch combines stock mode bits, DCO1/DCO2 waveform selection and the raw DCO2 detune value before writing hardware control state.

### Footage and waveform law

The two stock waveform relationships are modeled as:

```text
waveform 1  16' 8' 4' 2' = 1 : 1   : 1   : 1
waveform 2  16' 8' 4' 2' = 1 : 1/2 : 1/4 : 1/8
```

Every enabled footage is a square contribution. The second weighting creates the characteristic stepped/saw-like spectrum when several octaves are combined.

This matters because an earlier implementation followed Bristol's convenient “square vs mathematical saw” control interpretation. That produced a plausible synthesizer but not the documented MkI mechanism. M4/M5 replaced it with the stock footage mix.

### Polyphony and DCO2

ISLA reproduces the hardware mode split:

```text
WHOLE  = 8 voices, DCO1
DOUBLE = 4 voices, DCO1 + DCO2
```

DCO2 interval is modeled in equal-tempered semitones. The MkI documentation specifies DCO2 detune as downward clock thinning with a `-20 cent` maximum. M5 corrected the Bristol-derived positive/large detune path to that stock endpoint.

The current intermediate values are a linear 0..-20 cent mapping across P32=0..3. That is explicitly an inference. A future cycle-accurate model of the clock-thinning network could replace the intermediate ratios without changing the parameter API.

### Remaining DCO boundary

The software currently uses ideal octave ratios for the MSM5232 footages. Small pitch errors caused by real divider topology, master-clock tolerance and individual hardware are not yet emulated.

---

## 3. MG reconstruction from the EX-800 ROM

M6 is the point where the modulation generator stopped being a synthesizer-style approximation and became a firmware reconstruction.

### Recovered parameter routines and state

The IDA database/ROM analysis identifies, among others:

```text
MG_INIT            0x0D59
MG_DELAY_SETUP     0x0E56
MG_DELAY_LOOP      0x0E65
SET_MG_FREQUENC    0x0E76
SET_VCF_MG         0x0E83
SET_DCO_MG         0x0E9E
MG_TABLE           0x14EE
LINEAR_TABLE       0x152B
```

Realtime RAM includes the MG frequency counter, sign/direction bit, ramp value, DCO/VCF depths and delay counters.

### Exact frequency table

P81 indexes the 16-byte ROM table verbatim:

```text
hex: 01 02 03 04 06 08 0B 0E 11 15 19 1E 24 2B 34 3E
dec:  1  2  3  4  6  8 11 14 17 21 25 30 36 43 52 62
```

This supersedes the earlier cubic Bristol frequency law.

### Waveform

The firmware adds the selected table increment to an 8-bit counter. Overflow toggles the MG sign/direction state. Counter bit 7 is folded/complemented to create a ramp magnitude.

The result is a **digital triangle**, not a sine.

M6 deliberately does not interpolate that control signal into a smooth audio-rate triangle. The original CV is updated by firmware and sample-held, so the staircase is part of the machine's behaviour.

### Scheduler rate

The service documentation gives the interrupt oscillator as approximately 2.4..3.6 kHz. The current reconstruction uses 3 kHz nominal and the recovered 16-slot scheduler, giving an MG control update rate of:

```text
3000 / 16 = 187.5 Hz
```

The exact oscillator frequency varies in real hardware; treating 3 kHz as nominal is documented in the source rather than hidden as an arbitrary LFO constant.

### Depth arithmetic

The original routine at `0x1CA8` applies a four-bit fixed-point multiply. M6 expresses that unsigned 8-bit arithmetic directly instead of using a generic normalized quadratic depth curve.

P83/P84 therefore inherit the quantization and scaling shape of the firmware.

For DCO modulation, the final normalized control is mapped to the documented full-scale pitch calibration (±160 cents).

For VCF modulation, the ROM-derived waveform/depth code is exact but the final conversion into the software filter's cutoff domain remains an analog-model calibration. M6 keeps the established filter-domain maximum while replacing the earlier sine and depth law.

### Delay

P82 uses entries 0..15 of the recovered `LINEAR_TABLE` and the firmware counter law. It is no longer interpreted as an arbitrary number of seconds plus a synthetic fade.

The MG phase is not restarted like a conventional per-note LFO; the delay behaviour follows the firmware's global modulation-generator model.

---

## 4. DEG reconstruction: what the ROM proves and what it does not

The ROM contains two important 32-byte tables:

```text
LOG_TABLE @ 0x14FE
FF 80 55 40 33 2B 25 20 1D 1B 17 15 13 11 0F 0D
0B 09 07 06 05 04 03 02 01 02 03 04 05 06 08 0A

LINEAR_TABLE @ 0x152B
00 08 10 18 20 28 30 38 40 48 50 58 60 68 70 78
80 88 90 98 A0 A8 B0 B8 C0 C9 D2 DB E4 ED F6 FE
```

The generic envelope setup invokes dedicated Attack, Decay-to-Breakpoint, Slope-to-Sustain and Release setup routines for all three DEGs. This confirms that the original CPU uses specific integer control laws rather than a generic modern ADSR curve.

However, those firmware outputs are **control values**, not audio samples. The hardware then sends them through D/A conversion, time-sharing/sample-and-hold and analog control elements.

A literal `ROM byte / 255 = amplitude` implementation would therefore be unjustified.

### Current decision

M5.4/M5.4.1 calibrated the audible envelope against:

- timing examples published in the Korg owner's documentation;
- factory-audio release behaviour from an unmodified instrument/reference source.

M6 keeps that audible law while retaining the recovered ROM tables in source as evidence for a future analog-control model.

This is a deliberate example of the project's fidelity rule: **do not call something ROM-exact when the ROM is only half of the signal path.**

---

## 5. Shared VCF

The Poly-800 is paraphonic at the filter: voices are mixed before a single shared VCF. DEG3, noise, keyboard tracking and MG all interact with that shared path.

ISLA preserves that topology and the original parameter semantics.

The current VCF is a compact per-instance adaptation of Bristol's nonlinear four-pole Huovilainen path. It includes Bristol's separate below-88-kHz and high-rate branches; the latter matters because ISLA's Midiplus AudioLink setup can run at 96 kHz.

### What remains analog

The original NJM2069 circuit, DAC/control scaling and component tolerances are not reconstructed component by component. Consequently:

- exact cutoff frequency vs P41;
- exact resonance gain/threshold;
- exact DEG3/MG control-voltage transfer;
- unit-to-unit component variation

remain areas where hardware measurements can improve the model.

The important architectural point is that these uncertainties are confined to the filter/control transfer; they do not invalidate the recovered DCO or MG algorithms.

---

## 6. Chorus

The MkI uses a fixed MN3209/MN3102 BBD chorus and exposes only P48 ON/OFF.

Bristol's Dimension operator includes extra speed, depth and stereo/scan controls. Those are useful Bristol features but not evidence of stock Poly-800 controls.

The ISLA production core therefore uses a fixed BBD-style approximation with a short modulated delay, conservative wet mix and no hidden user parameters.

Direct hardware capture remains the appropriate way to refine its exact clock/depth/mix values.

---

## 7. Factory programs

The complete 64-program bank is based on a preserved MkI factory cassette decode.

The decoder:

- reads an external SoundDiviner package or mono PCM WAV;
- recovers framing;
- validates the expected factory header/layout;
- verifies the stored checksum;
- decodes exactly 64 x 21-byte patch payloads;
- emits the canonical 64 x 47 parameter CSV used by the build.

The resulting CSV is independently cross-checked against a preserved three-sheet parameter spreadsheet, but the checksum-valid cassette is authoritative where human transcription differs.

This means a factory program in ISLA is not a hand-tuned approximation of the patch settings. It starts from the original stored numeric program.

---

## 8. Why the project can make a strong fidelity claim

The strength of ISLA Poly-800 is not that every subsystem is “exact”. Some analog transfers are still approximations.

The strength is that **the boundary between fact and approximation is known**.

Today the project has:

- stock WHOLE/DOUBLE voice topology;
- stock square-footage DCO construction and waveform weighting;
- stock parameter surface;
- documented MkI detune endpoint/direction;
- recovered EX-800 MG lookup table;
- recovered MG counter/triangle/sign state machine;
- recovered fixed-point MG depth arithmetic;
- recovered MG delay table/counter law;
- recovered DEG control tables/routines retained as evidence;
- shared paraphonic VCF architecture;
- stock-style fixed BBD chorus surface;
- checksum-valid complete factory bank;
- deterministic tests and benchmarks;
- no proprietary runtime assets.

For that reason, M6 is best described as a **ROM-grounded and hardware-informed reconstruction**, not merely an emulation inspired by the Poly-800.

It is reasonable to say that this is as close as the project can currently get using the available evidence without pretending that undocumented analog behaviour has been solved.

---

## 9. Known remaining refinement targets

The current high-value targets are:

1. measure/model the NJM2069 cutoff, resonance and control-voltage transfer;
2. map the recovered DEG digital control law through the real VCA/filter analog response;
3. measure the four MkI P32 detune positions directly and/or reproduce the exact pulse-thinning circuit;
4. model MSM5232 divider/clock errors if they are audibly significant;
5. capture the stock BBD chorus wet/dry, clock and modulation depth from hardware;
6. compare identical factory programs against multiple real units to separate design behaviour from component tolerance.

None of these requires changing the LV2 parameter model or discarding the recovered ROM work.

---

## 10. Reproducibility and legal boundary

The repository intentionally does **not** distribute Korg ROM dumps, proprietary manual scans, cassette audio or third-party binary packages.

The build contains only:

- project source;
- factual parameter tables required for presets;
- project-original decoding/generation tools;
- tests and documentation;
- GPL-compatible Bristol-derived/adapted portions with attribution.

A clean build therefore remains reproducible entirely from the repository and ordinary GNU/Linux development packages.

For the development timeline and superseded assumptions, see [history.md](history.md).