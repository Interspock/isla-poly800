# Current architecture

This document describes the **current v0.6/M6 production architecture**. Older milestone documents are retained separately as development history.

## Goal

ISLA Poly-800 is a headless LV2 instrument that reconstructs the Korg Poly-800 MkI / EX-800 synthesis behaviour while remaining inspectable, reproducible and safe for multi-instance use on GNU/Linux.

The design separates host integration from synthesis:

```text
Ardour / LV2 host
      |
      | MIDI events + control ports + state
      v
src/isla_poly800.c
      |
      | note/control API
      v
src/poly800_core_m6.c
      |
      v
stereo audio
```

## Signal topology

```text
WHOLE
  8 x [DCO1 -> DEG1] -------------------+
                                         |
DOUBLE                                   +--> oscillator mix --+
  4 x [DCO1 -> DEG1] -------------------+                    |
      [DCO2 -> DEG2] -------------------+                    +--> shared VCF --> fixed chorus --> L/R
                                                               ^
                                                               |
                                            noise -> DEG3 -----+
                                                               |
                                            MG ----------------+
```

Important stock properties:

- WHOLE provides eight notes of DCO1 polyphony;
- DOUBLE provides four notes with DCO1+DCO2;
- DCO footages are 16', 8', 4', 2';
- DEG1/DEG2 are voice-local;
- DEG3 and the VCF are shared/paraphonic;
- parameter 46 controls single/multi VCF-envelope triggering;
- MG is synth-global;
- noise joins the shared DEG3/filter path;
- chorus is post-filter and stock-visible only as ON/OFF.

## Current core layering

The repository intentionally keeps several frozen checkpoints because fidelity work repeatedly replaced plausible assumptions with stronger evidence.

```text
src/poly800_core_m2.inc
    frozen M2 Bristol-calibrated synthesis/filter baseline
            |
            v
src/poly800_core.c
    frozen M4/M5 stock-DCO / DEG / detune / chorus checkpoint
            |
            v
src/poly800_core_m6.c
    current production core; adds EX-800 ROM-grounded MG control
```

CMake builds `src/poly800_core_m6.c` for the plugin, normal tests, benchmark and A/B probe.

The frozen pre-M6 `src/poly800_core.c` remains only to preserve the historical M4 regression executable and make the change in modulation behaviour auditable. It is not a second runtime engine selected by the LV2 host.

## DCO model

Each voice owns four phase accumulators per DCO, corresponding to the stock footages. Every enabled footage generates a band-limited square contribution.

The waveform selector changes only the footage weights:

```text
waveform 1: 1, 1,   1,   1
waveform 2: 1, 1/2, 1/4, 1/8
```

This reflects the documented tone-generator/resistor-mix mechanism rather than treating waveform 2 as an independent ideal saw oscillator.

## Envelopes

Each sounding voice owns DEG1 and DEG2 state. DEG3 is shared.

The stage machine is:

```text
OFF -> ATTACK -> DECAY to BREAKPOINT -> SLOPE to SUSTAIN -> SUSTAIN -> RELEASE -> OFF
```

The current audio-domain timing/level calibration is retained from M5.4/M5.4.1. EX-800 ROM envelope tables are present in M6 as control-domain evidence but are not directly mapped to gain until the downstream analog transfer is modeled.

## MG

M6 uses a firmware-rate state machine rather than an audio-rate sine oscillator.

Per instance, the MG owns:

- fractional audio-sample accumulator used to schedule firmware-rate updates;
- 8-bit phase/counter;
- sign/direction bit;
- held DCO modulation value;
- held VCF modulation value;
- delay counter.

P81 indexes the recovered 16-byte ROM increment table. Each control tick advances the 8-bit counter, folds it into the recovered triangle magnitude and applies P83/P84 with the original four-bit fixed-point multiply.

The held result is consumed by the audio renderer until the next control tick, preserving the original low-rate CV staircase.

## Shared filter

One `SharedFilter` object is owned by each plugin instance.

The filter is a compact adaptation of Bristol's nonlinear Huovilainen four-pole path, including its high-sample-rate branch. Modulation is the sum of shared DEG3 and M6 MG contributions plus keyboard tracking semantics.

No filter state is shared between plugin instances.

## Chorus

Each instance owns one persistent delay history. M6 retains the fixed M4 stock-style BBD approximation.

Chorus state advances while bypassed, avoiding a synthetic restart when P48 is enabled.

## LV2 boundary

`src/isla_poly800.c` owns only host-facing responsibilities:

- Atom Sequence MIDI input;
- sample-offset event dispatch;
- stereo output ports;
- control-port translation into `Poly800Params`;
- LV2 State schema marker;
- one `Poly800Core` object per plugin instance.

It intentionally does not contain oscillator/filter algorithms, JACK/ALSA device code, GUI logic or session filesystem logic.

## Parameter model

The LV2 exposes stock sound parameters by original number:

```text
11..18  DCO1 / mode
21..27  DCO2
31..33  interval / detune / noise
41..48  VCF / chorus
51..56  DEG1
61..66  DEG2
71..76  DEG3
81..84  MG
```

Hardware MIDI/global parameters 86..88 are omitted; the DAW owns those concerns.

## Per-instance state

Every mutable DSP object belongs to the instance:

```text
track A -> instance A -> voices/phases/DEGs/filter/MG/RNG/chorus A
track B -> instance B -> voices/phases/DEGs/filter/MG/RNG/chorus B
```

This makes simultaneous different patches safe and avoids global-state assumptions inherited from old standalone synthesizer architectures.

## Real-time rules

The audio callback performs no filesystem access, locks, process spawning, networking or routine heap allocation/free.

Expensive control recalculation occurs only when parameters change. The recovered MG state machine uses integer/table operations at its low control rate rather than trigonometric work per sample.

## Fidelity boundary

Architecture and digital control are modeled separately from analog transfer.

The production engine currently treats the following as established:

- stock voice/filter topology;
- DCO footage/waveform construction;
- stock parameter semantics;
- EX-800 MG table/state/fixed-point/delay behaviour;
- complete factory parameter bank.

The following remain refinement targets:

- exact NJM2069 control transfer and component behaviour;
- analog mapping of the recovered DEG DAC/control law;
- exact intermediate P32 pulse-thinning detune ratios;
- exact MSM5232 divider/clock imperfections;
- measured BBD chorus constants.

See [reconstruction.md](reconstruction.md) for the evidence behind those boundaries.