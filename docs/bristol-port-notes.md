# Bristol port notes

## Upstream reference

The planned DSP source is Bristol, specifically the Poly-800 algorithm in `bristol/bristolpoly800.c` from the Bristol 0.60.11 source line.

Relevant upstream project:

- Bristol: https://sourceforge.net/projects/bristol/
- Poly-800 documentation: https://bristol.sourceforge.net/poly800.html

No Bristol source code is vendored in this repository at Milestone 0.

## Licensing/provenance

The Bristol Poly-800 source carries a GNU GPL notice permitting redistribution/modification under GPL version 3 or, at the recipient's option, any later version. When Bristol-derived code enters this repository:

1. retain the original Bristol copyright and license notice in every derived file;
2. mark substantial ISLA modifications clearly;
3. keep project-original wrapper code under GPL-3.0-or-later for compatibility;
4. record the exact upstream release/commit used as the extraction baseline;
5. do not import ROM dumps, factory cassette images, firmware or other assets merely because they are downloadable.

## What to reuse

The useful part of Bristol is the synthesis implementation and the common operators required by that implementation, expected to include some combination of:

- Poly-800 orchestration;
- oscillator/tone generation;
- ADBSSR envelope behaviour;
- shared/paraphonic filter path;
- LFO;
- noise;
- amplifier stage;
- chorus/effect components where required for faithful stock behaviour;
- voice/note bookkeeping needed by the algorithm.

## What not to port

Do not carry the standalone application architecture into the plugin:

- Brighton GUI;
- JACK/ALSA device handling;
- external MIDI device handling;
- GUI/engine IPC;
- process/session management;
- standalone command-line startup.

LV2/Ardour already owns those responsibilities.

## Known technical risk

Bristol's Poly-800 implementation comments explicitly warn about Poly-800 global buffers and multiple audio threads. The LV2 port must therefore avoid a mechanical wrapper around the old global state.

The extraction should progressively move mutable state into an instance-owned structure. The first correctness test after the real DSP works will be running two independent plugin instances with different patches and verifying that neither modifies the other's sound/state.

## Fidelity strategy

Do not rewrite the synthesis from scratch during M1. First make a minimally invasive, auditable extraction that can be compared against stock Bristol. Refactor only after behavioural equivalence is established.

Reference sources such as service documentation, HAWK-800 work, and hardware-analysis projects may be used to understand the original machine, but code is only reusable when its license is explicit and compatible.
