# Poly-800 MkI factory program data

This directory is the canonical, reviewable input for M5.

## What is stored here

- `poly800-mk1-catalog.csv`: the 64 MkI program numbers and factory names.
- `poly800-mk1-verified.csv`: complete transcriptions of sound-program parameter values only.

The verified table deliberately starts empty. A program is added only when all 47 stock sound parameters can be checked against a preserved factory source. Partial rows are rejected by the generator.

`gain` and `tune` are ISLA host/plugin controls rather than Poly-800 program parameters. Generated presets set them deterministically to `0.32` and `0.0`; they are not represented in the source table.

Original MIDI/global parameters 86..88 are not part of a sound program in ISLA and are not stored here. Program locations 86, 87 and 88 are nevertheless valid factory program numbers; program numbers and parameter numbers are different concepts.

## Provenance

The 64-program catalog is cross-checked against public MkI factory-program listings, including SynthTools/SoundDiviner and preserved original-program archives.

The principal preserved data sources identified for parameter transcription are:

- Synth-DIY Yahoo Groups archive, `Poly-800 11-88.xls`, described by the archive as a spreadsheet of the original Poly-800 MkI programs with names and settings.
- Synth-DIY Yahoo Groups archive, `Poly800 preload patches.pdf`, described as a high-resolution scan of the original six-page factory patch sheet.
- Synth-DIY Yahoo Groups archive, `Audio Cassette of Original Patches.wav`, described as original MkI patches and sequencer data; useful as an independent binary/audio cross-check after decoding.

Source index:

`https://synth-diy.org/yg-archives2/group/korgpolyex/files`

Factory-name cross-check:

`https://synthtools.co.uk/sound-library/downloads/korg-poly-800-factory-programs/`

## Redistribution rule

Do **not** commit scans, Korg manuals, cassette audio, firmware, ROMs, or third-party binary dumps merely because they are downloadable. M5 stores only the project-original structured transcription of factual parameter values needed to reproduce a program.

If the provenance or interpretation of a row is uncertain, leave it out of `poly800-mk1-verified.csv` until it can be resolved.

## Validation/generation

Run:

```bash
python3 tools/generate_factory_presets.py
python3 tools/generate_factory_presets.py --check
```

The generator validates:

- exactly 64 catalog slots (`11..88`, digits `1..8` only);
- no duplicate or unordered verified programs;
- all 47 stock sound parameters present for every included program;
- integer values within the LV2/original parameter ranges;
- deterministic generated LV2 output.

When all 64 programs have been independently transcribed and checked, the release gate is:

```bash
python3 tools/generate_factory_presets.py --check --require-complete
```

Until that command passes, M5 factory-data acquisition is explicitly incomplete even if the pipeline itself is working.
