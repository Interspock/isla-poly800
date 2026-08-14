# M5.3 — stock Poly-800 DCO mixer correction

## Why this exists

Factory-program listening exposed a structural DCO error that the earlier
architecture tests did not catch.  Program 84 (`Synthe Bass III`) sounded like
an organ because the M2 core interpreted P12/P22 as a literal choice between a
square oscillator and a saw oscillator for each enabled footage.

The original Poly-800 documentation describes a different circuit/algorithm.
The tone generator supplies square waves at 16', 8', 4' and 2'.  P12/P22 select
how those four square-wave outputs are mixed:

```text
Waveform 1: 16' 1, 8' 1,   4' 1,   2' 1
Waveform 2: 16' 1, 8' 1/2, 4' 1/4, 2' 1/8
```

With all four footages enabled, waveform 2 forms the stepped approximation to a
sawtooth shown in the owner's manual.  It is not four independent sawtooth
oscillators.

## Sources

Primary hardware documentation used for this correction:

- Korg Poly-800 Owner's Manual, DCO1/DCO2 waveform and harmonics section;
- Korg Poly-800 Service Manual, tone-generator/output mixing description.

The service description explicitly gives the two resistor-mix relationships as
`1:1:1:1` and `1:1/2:1/4:1/8`.

This hardware evidence supersedes the provisional M2 interpretation recorded in
`docs/bristol-port-notes.md` where P12/P22 were described as true square/saw
choices.  That older note remains useful as milestone history but is not the
current stock-DCO model.

## Implementation

The M2 core remains frozen in `src/poly800_core_m2.inc`.  The current wrapper
renames the provisional `render_dco()` while including M2 and supplies a stock
implementation that:

1. always renders band-limited square-wave footage generators;
2. preserves the additive ON/OFF footage model;
3. uses equal weights for waveform 1;
4. uses `1, 1/2, 1/4, 1/8` for waveform 2;
5. keeps the existing octave, interval, detune, DEG, VCF and chorus paths
   unchanged.

## Regression

`tests/core_dco_stock.c` renders both waveform settings at low internal level
and measures the four octave components.  It checks that the waveform-2 to
waveform-1 ratios follow approximately:

```text
16'  1.000
 8'  0.500
 4'  0.250
 2'  0.125
```

The existing host-refresh regression continues to protect the separate P32
idempotence fix discovered while analysing the same factory-patch recordings.
