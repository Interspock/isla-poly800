# M5.4 — Korg DEG timing calibration

## Why this exists

Factory-program listening exposed a second source-level calibration gap after
M5.3 corrected the DCO mixer.  The six-stage DEG topology was already correct,
but M2 still used Bristol ENV5S's convenient provisional assumptions:

- rate law proportional to `(value / 31)^2`;
- a 10-second full-scale traversal at rate 31;
- linear breakpoint and sustain levels (`value / 31`).

The Korg Poly-800 Owner's Manual publishes concrete timing examples that let us
anchor these assumptions to the stock instrument instead of leaving them as a
Bristol approximation.

## Primary source

Korg POLY-800 Owner's Manual, section 3.4.7 DEG, currently distributed by Korg:

- https://www.korg.com/us/support/download/product/1/173/

The DECAY description says that with DECAY=31, the actual decay lasts about:

```text
Break Point 30   0.5 s
Break Point 29   1.2 s
Break Point 25   3.0 s
Break Point 20   5.0 s
```

The same section states that SLOPE and RELEASE are similarly affected by the
starting/ending level difference, and that 31 is long/high while 0 is
short/low.

## Compact calibration model

A two-parameter model fits the four printed Korg examples closely:

```text
maximum full-scale traversal = 8.0 seconds
DEG level(raw) = (raw / 31)^2.2
```

At DECAY=31 this predicts:

```text
BP 30   0.557 s
BP 29   1.092 s
BP 25   3.016 s
BP 20   4.950 s
```

The manual labels its numbers as approximate, so this is preferable to a
piecewise lookup table that would pretend the printed examples are exact
laboratory measurements.

Korg does not publish equivalent timing anchors for every rate value.  M5.4
therefore retains Bristol ENV5S's squared interpolation across rate values:

```text
full_scale_time(rate) = 8.0 * (rate / 31)^2
```

Only the absolute time anchor and level law are changed.

## Runtime design

The calibrated `EnvConfig` is rebuilt only when LV2 controls are synchronized.
`powf()` is therefore not executed in the per-sample render hot path.

The same calibrated law is applied to DEG1, DEG2 and DEG3.  Envelope topology
remains unchanged:

```text
Attack -> Decay -> Break Point -> Slope -> Sustain -> Release
```

Release still begins from the current envelope value if the key is released
before Sustain, matching the owner's manual description.

## Factory 84 reference

Program 84 (`Synthe Bass III`) uses DEG1:

```text
A=0 D=26 BP=23 Slope=26 Sustain=0 Release=20
```

Because Decay and Slope have the same rate and Sustain is zero, the total
peak-to-zero traversal while the key is held is the full-scale rate-26 time:

```text
8 * (26/31)^2 = 5.626 s
```

The breakpoint changes the contour shape and where the Decay/Slope transition
occurs, but not that total duration when both rates are equal.

## Regression

`tests/core_deg_timing.c` directly times internal DEG stages and requires all
four Korg DECAY=31 examples to remain within 0.20 seconds of the manual values.
This test is intentionally independent of oscillator, VCF and chorus behaviour.
