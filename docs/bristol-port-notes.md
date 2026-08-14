# Bristol port notes

ISLA Poly-800 uses Bristol as a GPL source reference and test oracle, not as a runtime dependency.

## Baseline

The planned/source oracle baseline is Bristol 0.60.11 / the mirrored source commit `116fb8a2d21727676e21db5f1efe295c1ea22d61` used throughout M2–M4.

Before any Bristol-derived code was incorporated, the project recorded attribution and kept the headless LV2 architecture separate from Bristol's standalone engine, GUI and I/O layers.

## Retained/adapted DSP behaviour

- Poly-800 voice orchestration
- NRO/footage behaviour
- ENV5S six-stage envelope timing law
- shared Huovilainen filter path
- LFO controller laws/routing
- Dimension chorus behaviour

## Deliberately excluded infrastructure

- Brighton GUI
- JACK/ALSA standalone plumbing
- TCP GUI/engine communication
- session manager
- standalone `main()`
- Bristol global/mutable Poly-800 buffers

All mutable DSP state in ISLA is per plugin instance.

## M4 notes

M4 freezes the M2 implementation in `src/poly800_core_m2.inc` and layers only the remaining source-level calibration in `src/poly800_core.c`. The relevant additional Bristol sources are `bristolpoly800.c`, `lfo.c`, `dimensionD.c` and `brightonPoly800.c`.

Bristol's Poly-800 uses palette operator 12 (`chorusinit` / `dimensionD.c`), not operator 13 (`vchorusinit`). Its Brighton front-end adds hidden chorus parameters not present on the original stock Poly-800 program surface. ISLA therefore keeps P48 as the sole visible chorus control and uses documented Bristol-memory defaults for those hidden values. See `docs/m4-ab-calibration.md`.

No Korg ROM, firmware or factory cassette data is copied into the project.
