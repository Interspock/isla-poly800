# M4 A/B calibration

M4 closes the two behaviours intentionally left provisional in M2: MG-to-DCO depth and the stock-visible chorus switch.

## Bristol baseline

Source oracle for MG/LFO behaviour: `nomadbyte/bristol-fixes` commit `116fb8a2d21727676e21db5f1efe295c1ea22d61`, corresponding to the Bristol 0.60 lineage.

Relevant files:

- `bristol/bristolpoly800.c`: Poly-800 LFO routing and P82/P83/P84 controller laws.
- `bristol/lfo.c`: P81 LFO frequency law.
- `bristol/dimensionD.c`: Bristol effect operator 12 used by its Poly-800 implementation.
- `brighton/brightonPoly800.c`: stock parameters plus Bristol-only extension routing.

For the chorus, the Korg Poly-800 owner/service documentation is also authoritative because Bristol exposes additional editable controls that the original hardware does not have.

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

P81 was already correct in M2 (`0.1 + value^3 * 20 Hz`). P82 was also already correct: normalized 0..1 maps to 0..15 seconds of delay followed by an equal-duration fade. P84 already used Bristol's `value^2 * 8` coefficient; M4 feeds it the same unipolar LFO bus Bristol uses.

## Chorus: source correction after the first M4 pass

The first M4 implementation ported Bristol's `dimensionD.c` literally and froze Bristol-only hidden controls 58/68/78 to values found in its supplied memories. That was internally consistent with Bristol, but an Ardour listening test immediately exposed an obvious periodic sweep/flanger character when P48 was enabled.

Reinspection showed why this was the wrong fidelity target for the stock instrument:

- the original Poly-800 exposes only P48 Chorus ON/OFF;
- Bristol adds hidden controls 58/68/78 for Dimension speed/depth/scan;
- Bristol's Dimension implementation uses a long variable-delay excursion, wet feedback and moving stereo wet gain;
- those extensions are useful Bristol features but are not evidence for the fixed analog chorus calibration of the original Korg.

The MkI hardware uses a fixed analog BBD chorus built around the MN3209 BBD and MN3102 clock driver. The stock control therefore remains a single P48 switch.

ISLA now models that topology with a deliberately restrained, fixed BBD-style approximation:

```text
LFO rate       = 0.55 Hz
centre delay   = 6.8 ms
mod depth      = +/-0.6 ms
wet mix        = 25%
dry mix        = 75%
feedback       = none
```

Two complementary taps from the same delay history provide a small stereo spread. The history and LFO continue while P48 is off, so enabling chorus does not restart an empty delay line or force its phase to zero.

These constants are explicitly **hardware-informed calibration values**, not a claim that the analog MN3209 clock waveform has been circuit-simulated. A real-hardware A/B can refine rate, delay, depth and mix later without changing the stock parameter surface.

## Why the sweep regression is now testable

The old Bristol-Dimension pass changed a stable C4 probe from 784 to roughly 930 zero crossings and produced very large stereo difference. The corrected stock-style chorus retains the dry zero-crossing density while adding a smaller stereo difference.

`tests/core_m4.c` now rejects a chorus whose zero-crossing density moves more than roughly 16.7% from the dry reference and also rejects excessive level changes. This is not a psychoacoustic proof, but it prevents the specific deep-sweep regression that was heard in Ardour.

## Reproducible probe

`core_ab_probe` renders a fixed C4 program at 48 kHz and prints RMS, zero-crossing count and stereo-difference RMS for:

- dry / P83=0
- dry / P83=7
- dry / P83=15
- stock BBD-style chorus on

Run:

```bash
./build/core_ab_probe
```

These metrics are not a claim of sample identity. They are a stable experiment protocol for comparing ISLA builds, Bristol renders, and future hardware/reference captures.

The 96 kHz path remains covered because the ISLA AudioLink configuration may run at 96 kHz.
