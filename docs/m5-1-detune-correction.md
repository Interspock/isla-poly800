# M5.1 stock DCO2 detune correction

Factory-bank validation exposed a calibration error in the synthesis engine rather than in the recovered program data.

## Symptom

Factory program `84 Synthe Bass III` loaded the checksum-verified cassette values correctly, including `P32=2`, but sounded conspicuously rough/out-of-tune in DOUBLE mode.

## Root cause

The earlier M2 calibration followed Bristol's Brighton/NRO path for P32. Bristol routes the Poly-800 frontend value through the positive half of NRO `FINETUNE`, effectively allowing roughly one semitone of upward detune.

That conflicts with the stock Korg hardware specification:

- the Poly-800 owner/service documentation specifies DCO2 DETUNE as **20 cents maximum**;
- the service description says the detune circuit **lowers frequency by thinning clock pulses**.

For the stock ISLA Poly-800 mode, explicit Korg hardware documentation therefore takes precedence over Bristol's broader emulation control path, following the same fidelity rule established for the chorus correction.

## Correct mapping

P32 is 0..3 and is now mapped linearly to downward cents:

```text
P32 0 ->   0.000 cents
P32 1 ->  -6.667 cents
P32 2 -> -13.333 cents
P32 3 -> -20.000 cents
```

The frequency ratio is:

```text
ratio = 2 ^ (cents / 1200)
```

This changes only DCO2 fine detune. DCO octave, P31 interval, global tune, envelopes, filter, MG and chorus are untouched.

## Regression guard

`tests/core_calibration.c` now measures isolated DCO2 frequency and requires:

- P32=3 to be approximately -20 cents;
- P32=2 (the value used by factory program 84) to be approximately -13.33 cents;
- both values to lower rather than raise DCO2 frequency;
- the 96 kHz stressed AudioLink path to remain finite.

This correction is intentionally narrow. The remaining PolyBLEP-vs-Bristol-NRO waveform-character difference is kept separate and should only be changed after listening to the corrected factory bank or a controlled A/B render.
