# ISLA Poly-800

Headless, native GNU/Linux LV2 instrument project aimed at a free-software emulation of the Korg Poly-800 architecture for the ISLA DAW environment.

## Status

**Milestone 0 — LV2 host smoke test.**

The first implementation is intentionally **not yet a Poly-800 emulation**. It is a minimal LV2 instrument used to validate the complete host path:

`MIDI -> LV2 instrument -> audio -> Ardour generic controls`

Once that path is proven on the ISLA machine, the next milestone is to extract/adapt the Poly-800 DSP from Bristol and replace the smoke-test oscillator while keeping the LV2 wrapper small and host-native.

## Principles

- Free/open-source software only.
- Native GNU/Linux; no Wine, yabridge or proprietary runtime.
- No custom GUI required: Ardour's generic plugin editor is the initial UI.
- Reproducible build and installation.
- Preserve provenance and upstream license notices for adapted Bristol code.
- No proprietary ROMs or other non-redistributable assets.

## Build (Milestone 0)

Requirements on Debian/Ubuntu/Mint:

```bash
sudo apt install build-essential cmake pkg-config lv2-dev
```

Build:

```bash
git clone https://github.com/Interspock/isla-poly800.git
cd isla-poly800
cmake -S . -B build
cmake --build build
```

The build produces a complete bundle at:

```text
build/isla-poly800.lv2/
```

Install it for the current user using the standard LV2 user path:

```bash
mkdir -p ~/.lv2
cp -a build/isla-poly800.lv2 ~/.lv2/
```

Then rescan plugins in Ardour and look for **ISLA Poly-800 (M0 Smoke Test)** as a MIDI instrument.

## Milestones

- **M0:** compilable headless LV2, MIDI note input, audio output, generic parameters.
- **M1:** Bristol Poly-800 DSP extraction/adaptation.
- **M2:** expose the original Poly-800 parameter set through LV2 control ports.
- **M3:** reliable per-instance state/save/restore inside Ardour sessions.
- **M4:** compare behaviour/output against Bristol and correct regressions.
- **M5:** reproducible factory-patch data, subject to asset/licensing verification.
- **M6:** optional refinements; custom GUI remains non-essential.

See `docs/architecture.md` and `docs/bristol-port-notes.md`.

## License

Project-original code is released under **GPL-3.0-or-later**. Code adapted from Bristol will retain its original copyright and GPL notices. See `LICENSE` and `docs/bristol-port-notes.md`.
