# M5.4.1 — DEG rate curve from factory-program audio

## Why this correction exists

M5.4 calibrated the Poly-800 DEG level law and the `rate=31` endpoint against
four timing examples printed in the Korg owner's manual. Those anchors remain
valid and unchanged.

The manual does not publish intermediate rate timings. M5.4 therefore retained
Bristol ENV5S's squared interpolation across the `0..31` rate range. Factory
program 84 (`Synthe Bass III`) exposed that interpolation as implausible:
`Release=20` produced a roughly 3.3 second full-scale release in ISLA.

A reference recording made with a stock Poly-800 factory program 84 was then
analysed. Two isolated short notes show a clear key-off knee followed by a fall
to near silence in roughly 0.2–0.3 seconds. The reference recording is used only
as a measurement source and is not redistributed in this repository.

## Calibrated law

Korg describes Attack, Decay, Slope and Release as the same class of `Rate`
parameter, each with range `0..31`. We therefore use one rate law for all four
stages rather than introducing a Release-only special case.

The M5.4 endpoint remains:

```text
rate 31, full 0..1 traversal ≈ 8.0 s
```

The interpolation becomes:

```text
seconds = 8.0 * (rate / 31)^7.5
```

This gives:

```text
rate 31  ≈ 8.000 s
rate 26  ≈ 2.139 s
rate 20  ≈ 0.299 s
rate 15  ≈ 0.035 s
```

Actual stage duration is shorter whenever the stage traverses only part of the
envelope level range, which is exactly the behaviour described by Korg.

## What remains anchored to the owner's manual

The level law remains:

```text
level = (raw / 31)^2.2
```

At `Decay=31`, it continues to reproduce Korg's published approximate timings:

```text
BP 30  ≈ 0.56 s   (manual: about 0.5 s)
BP 29  ≈ 1.09 s   (manual: about 1.2 s)
BP 25  ≈ 3.02 s   (manual: about 3.0 s)
BP 20  ≈ 4.95 s   (manual: about 5.0 s)
```

Thus M5.4.1 changes only the previously unsupported interpolation between rate
0 and rate 31; it does not discard the hardware-manual anchors.

## Regression coverage

`tests/core_deg_timing.c` now checks both classes of evidence:

1. the four Korg `Decay=31` manual examples;
2. a full-scale `Release=20` duration of approximately 0.30 seconds;
3. the resulting full-scale rate-26 reference used heavily by factory 84.

The reference-audio anchor is intentionally treated as empirical rather than as
laboratory-grade hardware characterization. A future controlled hardware A/B
can replace the fitted curve with a more exact table without changing preset
data or the LV2 interface.
