# Development history

This file exists so that the main README and current architecture can stay clean.

ISLA Poly-800 was developed by repeatedly replacing plausible approximations with stronger evidence. Old milestone documents are retained because they show how conclusions changed; they are **research history, not the current specification**.

## Milestones

### M0 — prove the host path

A temporary headless LV2 instrument proved MIDI -> DSP -> stereo audio inside Ardour on the target GNU/Linux machine.

### M1 — Poly-800 topology

Introduced the stock parameter surface, WHOLE/DOUBLE polyphony, three six-stage DEGs, shared filter/noise/MG/chorus topology and fully per-instance state.

### M1.1 — real-time performance

Moved expensive work out of the sample loop, established Release-by-default builds and added reproducible core benchmarks suitable for older ISLA hardware.

### M2 — Bristol source calibration

Compared the early implementation against Bristol GPL source and adopted its stronger routing/filter behaviour. This was a large improvement over the first generic approximations, but several Bristol interpretations were later superseded by stock hardware evidence.

### M3 — LV2 state and deterministic presets

Added versioned LV2 State support and deterministic utility presets while keeping sound parameters owned by ordinary LV2 control ports.

### M4 — MG/chorus investigation

Completed the Bristol MG routing interpretation and discovered that Bristol's hidden Dimension-style chorus controls were not stock Poly-800 controls. The production chorus was changed to a fixed hardware-informed BBD-style path.

The historical M4 MG was intentionally extreme because it accurately reproduced Bristol's modulation path. It was later shown not to reproduce the EX-800 firmware MG.

### M5 — factory bank and stock corrections

Recovered the complete 64-program MkI factory bank from a checksum-valid cassette decode and added reproducible generation/validation tooling.

Subsequent M5.x work corrected the MkI DCO2 detune direction/range, replaced mathematical saw generation with the documented square-footage resistor mix, and calibrated DEG timing against Korg manual examples plus factory audio.

### M6 — EX-800 ROM-grounded control engine

Reverse engineering of the EX-800 8 KiB 80C85 ROM recovered the original MG frequency table, accumulator/triangle algorithm, delay counters and four-bit depth multiplication.

The production core moved to `src/poly800_core_m6.c` and the previous M4/M5 wrapper was frozen in `src/poly800_core.c` for direct historical comparison.

M6 is the current production engine.

## Why old documents are kept

The milestone documents are useful because they record:

- the evidence available at a particular date;
- hypotheses that later proved wrong;
- A/B methodology and regression criteria;
- why a later correction was necessary.

Deleting them would make the final implementation look less rigorous, not more. What matters is that the current documentation clearly identifies them as superseded where appropriate.

Current documents:

```text
README.md
  project overview / current status

docs/reconstruction.md
  current evidence and fidelity boundary

docs/architecture.md
  current software/signal architecture

docs/bristol-port-notes.md
  current Bristol provenance and supersessions
```

Historical notebook documents include:

```text
docs/m3-state-presets.md
docs/m4-ab-calibration.md
docs/m5-1-detune-correction.md
docs/m5-3-stock-dco.md
docs/m5-4-deg-timing.md
docs/m5-4-1-factory-audio-rate.md
```

## Git checkpoints

`src/poly800_core_m2.inc` and `src/poly800_core.c` intentionally preserve important earlier synthesis checkpoints inside the repository.

The `m6-rom-mg` branch preserves the exact transition used to validate the ROM-grounded MG before it was fast-forwarded to `main`.

Git history is therefore the laboratory notebook; `main` documentation is the polished description of the current instrument.