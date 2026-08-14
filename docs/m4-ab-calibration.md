# M4 A/B calibration

M4 closes the two behaviours intentionally left provisional in M2: MG-to-DCO depth and the stock-visible chorus switch.

## Bristol baseline

Source oracle: `nomadbyte/bristol-fixes` commit `116fb8a2d21727676e21db5f1efe295c1ea22d61`, corresponding to the Bristol 0.60 lineage.

Relevant files:

- `bristol/bristolpoly800.c`: Poly-800 LFO routing and P82/P83/P84 controller laws.
- `bristol/lfo.c`: P81 LFO frequency law.
- `bristol/dimensionD.c`: effect operator 12 (`chorusinit`) used by the Poly-800.
- `brighton/brightonPoly800.c`: stock and hidden Poly-800 parameter routing.
- `memory/poly800/poly80011.mem`, `12`, `13`: Bristol-supplied programs used only to recover its hidden chorus defaults.

## MG calibration

Bristol maps normalized P83 as:

```text
vcomod = value^2 * 4
```

Its sine LFO receives a `+1` offset before the delayed gain DCA, creating the unipolar bus:

```text
lfo_bus = (1 + sine) * fade_gain
```

The oscillator frequency path then applies:

```text
frequency *= 1 + lfo_bus * vcomod
```

M4 reproduces that mapping directly. This deliberately permits extremely deep modulation at high P83 values; it is Bristol behaviour, not a conventional semitone vibrato control.

P81 was already correct in M2 (`0.1 + value^3 * 20 Hz`). P82 was also already correct: normalized 0..1 maps to 0..15 seconds of delay followed by an equal-duration fade. P84 already used Bristol's `value^2 * 8` coefficient; M4 now feeds it the same unipolar LFO bus Bristol uses.

## Chorus calibration

Bristol's Poly-800 initializes effect operator 12, which is `chorusinit()` from `dimensionD.c`, not `vchorusinit()`.

The original Poly-800 surface exposes only P48 on/off. Bristol adds hidden controls for speed/depth/scan. Since ISLA intentionally keeps the stock program surface, M4 freezes those hidden controls to the common values found in Bristol's supplied programs 11, 12 and 13:

```text
speed = 0.104142368
depth = 0.713488936
scan  = 0.159550920
```

P48 ON corresponds to Bristol's gain controller value `1.0`, giving the Dimension algorithm an internal gain of `1.5`.

The M4 implementation ports the 4096-sample history, interpolated variable delay, sine-driven scan position, wet feedback and stereo wet-gain scan from `dimensionD.c`. P48 OFF is a deliberate hard bypass in ISLA; this avoids Bristol's generic effect-engine dry-gain quirk when its gain parameter is zero.

## Reproducible probe

`core_ab_probe` renders a fixed C4 program at 48 kHz and prints RMS, zero-crossing count and stereo-difference RMS for:

- dry / P83=0
- dry / P83=7
- dry / P83=15
- Dimension chorus on

Run:

```bash
./build/core_ab_probe
```

These metrics are not a claim of sample identity. They are a stable experiment protocol for comparing ISLA builds, Bristol renders, and future hardware/reference captures.
