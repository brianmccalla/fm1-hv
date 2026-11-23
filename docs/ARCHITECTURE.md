# fm1-hv architecture

## Goals

1. A playable, good-sounding Virus-style subtractive synth on a $60 FM-1.
2. Virus A/B/C singles (SysEx) import and sound recognisably like themselves. TI singles later, partially.
3. A browser editor and a VST3, Shrub-style, that edit the same 256-byte patch.

Not a goal: a cycle-exact Virus. No Access ROM, OS or sample data ships with fm1-hv.

## The patch

The native patch *is* a classic Virus single: 256 bytes, page A (0..127) then page B (128..255), each parameter at
the index the Virus uses (page A index = the Virus's MIDI CC number). The engine reads these bytes directly and
interprets them with its own curves. Import is a copy; export to a real Virus is a copy back.

The value curves (cutoff in Hz, envelope times, LFO rates, detune in cents) are first guesses. They get tuned by
rendering the same patch on a real Virus and on fm1-hv and comparing (tools to come).

## Voice (per note, per unison voice)

```
osc1 (wave | saw | pulse morph, PW) ──┐
osc2 (same; semitone, detune, sync,  ─┼─ mix ── drive ── filter 1 ── saturation ── filter 2 ── amp ── pan
      FM from osc1, env -> pitch)     │                (LP/HP/BP/BS,              (LP/HP/BP/BS)
sub (square / triangle, -1 oct) ──────┤                 or 1..4-pole ladder)
noise, ring (osc1 x osc2), osc3 ──────┘   routing: serial 4, serial 6, parallel, split
filter env, amp env: ADSR + sustain slope.  LFO 1, LFO 2: sine, tri, saw, square, S&H, S&G, waves
```

Control rate: every 32 samples (envelopes, LFOs, filter coefficients). Audio rate: oscillators (polyBLEP),
filters (TPT state-variable; TPT ladder with tanh input), saturation.

Global: chorus (stereo, modulated), delay (ping-pong, colour). Reverb comes later.

## The FM-1's budget

| | Have | fm1-hv uses (0.0) |
| --- | --- | --- |
| CPU | 240 MHz pi32v2, FPU; the audio ISR has 5.8 ms per 256 frames | unmeasured on the chip; target under 60 % |
| Code + const | 568 KB | small |
| RAM | 96 KB .data + .bss | voices, small buffers |
| Pool | 336 KB | spectral waves 32 KB, delay 128 KB, chorus 16 KB |
| Flash | 1 MiB total | patches and imported wavetables: to plan |

Expected polyphony: 4 to 6 voices of the full voice (±2), fewer with unison. The second core (as Felucca uses it)
can roughly double that later.

## Phases

| Phase | What | Done when |
| --- | --- | --- |
| 0 | Engine on the host, app on Dinghy, CI builds the package | the package builds on GitHub |
| 1 | Runs in fm1-emu, CPU measured, then on a spare FM-1 | plays 4+ voices without crackle |
| 2 | Patch storage in flash (banks), Virus SysEx import over USB, the arpeggiator | a Virus bank plays |
| 3 | Reverb, phaser, distortion, osc3 modes, HyperSaw, the mod matrix (assign 1..6), velocity routings | |
| 4 | Wavetable import (the user's own files) into flash; the browser editor | |
| 5 | The VST3 (fm1-emu's plugin running this firmware, plus the editor) | |
| 6 | Curve tuning against real Virus units; TI singles | |
