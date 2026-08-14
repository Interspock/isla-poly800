# Poly-800 MkI factory program data

This directory is the canonical, reviewable factory-program input for M5.

## What is stored here

- `poly800-mk1-catalog.csv`: all 64 MkI program numbers/names (`11`..`88`, digits 1..8).
- `poly800-mk1-verified.csv`: all 64 programs with the 47 stock sound parameters P11..P84 used by ISLA Poly-800.

`gain` and `tune` are ISLA host/plugin controls, not Poly-800 program parameters. Generated presets set them deterministically to `0.32` and `0.0`.

Original MIDI/global parameters 86..88 are intentionally omitted because Ardour owns MIDI routing. Program locations 86, 87 and 88 are still valid factory *program numbers*; program numbers and parameter numbers are different things.

## Numeric source of truth: factory cassette

The 64 parameter rows were recovered from a preserved Poly-800 MkI factory cassette image, not guessed or reconstructed by ear.

The reference SoundDiviner package used during M5 had:

```text
Poly-800_Factory.sdp SHA-256
39061588df6a5405163410dd706a7682eff2559e7613e36fe692748e59576173

embedded 44.1 kHz mono PCM WAV SHA-256
b4c32a934b119e96aee9afbe6f0684fc33b07b88aa6adec734dbbb1f78e8ad47
```

`tools/decode_poly800_factory_tape.py` demodulates the external WAV/.sdp, validates the serial framing and factory layout, requires the `B3 BF 00` header, and verifies the stored cassette checksum before decoding any presets. The M5 reference image decodes to 1636 bytes with stored/computed checksum `0x87`; its patch payload is exactly 64 x 21 bytes.

No cassette WAV or `.sdp` binary is distributed by this repository.

## Independent spreadsheet cross-check

A preserved three-sheet `Poly-800 11-88.xls` was also inspected. Its SHA-256 was:

```text
b95d64ee9c1d8b459ffbef6c8bde041bb9e9c4cd79477b1dffff0b3501f5283f
```

The sheets contain 24 + 24 + 16 programs = 64 total. Against the checksum-valid cassette decode:

```text
numeric XLS cells matching cassette exactly: 2780
numeric XLS cells differing:                 46
XLS don't-care cells marked "x":            182
```

The mismatches include clear spreadsheet transcription/column-shift errors (for example values impossible for 1/2 controls and envelope values outside 0..31). Therefore the cassette decode is authoritative for numeric values; the XLS is an independent human-readable cross-check and supplies useful evidence for the parameter interpretation. The cassette also preserves the stored DCO2/DEG2 values in programs where the spreadsheet marks those fields `x` because WHOLE mode does not audibly use them.

Factory names are cross-checked against the preserved factory-program list/PDF and public MkI listings.

## Reproducing the decode

Given an external copy of the same `.sdp` or its contained WAV:

```bash
python3 tools/decode_poly800_factory_tape.py Poly-800_Factory.sdp \
    -o /tmp/poly800-decoded.csv

cmp /tmp/poly800-decoded.csv factory/poly800-mk1-verified.csv
```

The decoder uses only the Python standard library.

## Generating the LV2 bank

The 64-preset Turtle file is deliberately a **build artifact**, not a second 200 kB source-of-truth copy in Git. CMake generates it from the compact CSV during every build:

```text
factory/poly800-mk1-verified.csv
        +
factory/poly800-mk1-catalog.csv
        |
        v
tools/generate_factory_presets.py
        |
        v
build/isla-poly800.lv2/factory-presets.ttl
```

Validate the source data directly with:

```bash
python3 tools/generate_factory_presets.py --check --require-complete
```

The validator requires:

- exactly 64 catalog slots in order;
- exactly 64 complete factory rows;
- all 47 stock parameters present in every row;
- integer values within the original/LV2 ranges;
- no duplicate or partial programs.

## Redistribution rule

Do **not** commit Korg scans/manuals, cassette audio, SoundDiviner packages, ROM/firmware, or third-party binary dumps merely because they can be found online. M5 stores only the project-original decoder/generator and the compact factual parameter table required to reproduce the programs.
