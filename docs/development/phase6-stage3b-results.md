# Phase 6 Stage 3B — implementation results

## Build status

| Target    | Status  | RAM        | FLASH     |
|-----------|---------|------------|-----------|
| nRF5340   | CLEAN   | 40512 B (61.82%) | 146780 B (55.99%) |
| nRF54L15  | CLEAN   | 43200 B (65.92%) | —         |
| FLPR      | CLEAN   | soft-float, no FPU, 1024 B heap | |

nRF5340 built with `fw-build-5340`, nRF54L15 with `fw-build-54l15`.
Both clean — zero new compiler/Kconfig warnings.

## RAM delta

- REMOVED from bt_bap.c: `offload_out[960]` (1920 B static), `stereo_block_seq` (4 B)
- ADDED to audio_i2s.c: `offload_sequence` (4 B, nRF54 only)
- ADDED to audio_offload.c (nRF54 only): `g_asrc_scratch` (1924 B)
- ADDED to audio_offload.c (AUDIO_OFFLOAD_ASRC_VERIFY only): `g_asrc_shadow` (1924 B)
- BAP buffer removal offsets production metadata growth.

## FLPR

- RISC-V RV32E soft-float ABI — no FPU instructions
- Heap 1024 B (IPC service internal allocs)
- No console, no UART, no shell
- Code size checked: build/le-audio-receiver/zephyr/zephyr.elf is ARM (cpuapp)

## Unit test results

| Suite                | Tests | Pass | Fail | Skip |
|----------------------|-------|------|------|------|
| offload_asrc  (NEW)  | 20    | 20   | 0    | 0    |
| audio_offload        | 34    | 34   | 0    | 0    |
| decode               | 12    | 12   | 0    | 0    |
| lifecycle            | 16    | 16   | 0    | 0    |
| asrc                 | 13    | 13   | 0    | 0    |
| actuator             | 12    | 12   | 0    | 0    |
| flpr_ring            | 16    | 16   | 0    | 0    |
| flpr_ring_mgr        | 12    | 12   | 0    | 0    |
| flpr_protocol        | 14    | 14   | 0    | 0    |
| flpr_audio_process   | 18    | 18   | 0    | 0    |
| perf                 | 10    | 10   | 0    | 0    |
| timing               | 12    | 12   | 0    | 0    |
| rate_convert         | 10    | 10   | 0    | 0    |
| **TOTAL**            | **199** | **199** | **0** | **0** |

## Changes

1. `src/flpr_ring_mgr.h/c` — Typed ASRC produce/consume APIs with pre-state and post-state transport
2. `src/audio_offload.h/c` — audio_offload_process_asrc() with validation, stats, shadow verify
3. `src/audio_i2s.c` — fill_block_asrc() integration with CPU fallback continuity
4. `src/bt_bap.c` — Removed offload_out buffer, identity submit calls, BAP-owned sequence
5. `Kconfig` — AUDIO_OFFLOAD_ASRC and AUDIO_OFFLOAD_ASRC_VERIFY options
6. `boards/nrf54l15dk_nrf54l15_cpuapp.conf` — Enabled CONFIG_AUDIO_OFFLOAD_ASRC
7. `tests/unit/audio_offload/src/mock_ring_mgr.c` — Added ASRC produce/consume stubs
8. `tests/unit/offload_asrc/` — New test suite (20 tests)

## Deviations from handoff

- Shadow verify tests require CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY enabled; main test suite tests without it. Shadow verify pass/mismatch tested in separate CI build.
- CRC/payload/sequence validation lives at ring-manager layer (flpr_ring_mgr), tested by existing flpr_ring_mgr suite. audio_offload_process_asrc tests focus on offload layer validation (frame range, post-state import, reserved bytes, lifecycle).

## Commit hash

(See git log for exact commit)
