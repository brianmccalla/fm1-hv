# Patches

An fm1-hv patch is 256 bytes in the classic Virus single layout: page A (bytes 0..127), page B (128..255). The
parameter at each index is the one the Virus has there; page A's index is also the Virus's MIDI CC number.
`firmware/src/hv/hv_engine.h` names the ones the engine reads.

Import of Virus A/B/C single and bank SysEx dumps comes in phase 2 (docs/ARCHITECTURE.md). What it will do: take
the 256 data bytes of each single out of the dump and store them unchanged.
