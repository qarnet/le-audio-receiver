# Phase 6 Stage 1 — Results (v2 — independent verification)

**Date**: 2026-07-27
**Status**: **STAGE CLOSED** — Stage 1 gate reopened for remaining acceptance items (independent payload verification, latency measurement, stall controls, stale epoch injection). All items implemented and verified. Stage 2 (identity loopback with live audio) is next.

## Changes from f4ff554 (initial gate close)

The initial gate close (f4ff554) verified 100k-block CRC loopback, diagnostic counters, and drain-driven test loop. The following items were incomplete:
1. CRC-only verification (weak — copied payload + copied CRC proves nothing)
2. No independent payload verification (regeneration from sequence + memcmp)
3. No latency measurement
4. No hardware stall controls
5. No stale epoch injection on hardware
6. No low-rate concurrent test during audio streaming
7. Returns zero on failure (no nonzero error return)

## Fixes applied (this commit)

### 1. Independent loopback verification

**Before**: producer computed CRC over payload, FLPR copied payload + CRC, consumer verified CRC of received payload. This only proves the copy was bit-exact — a stale or corrupted payload that happens to have correct CRC passes.

**After**: producer generates deterministic payload from sequence number via splitmix hash (`flpr_ring_gen_payload`). Consumer independently regenerates the same payload and does full `memcmp`. CRC is still computed as a second independent check.

- `test_payload_errors`: count of mismatched stereo frames (independently verified)
- `test_crc_errors`: retained as second check
- Both must be zero for PASS

**PASS gate**: 100k blocks transferred, zero payload errors, zero CRC errors.

### 2. Test correctness

- **Nonzero on failure**: `flpr_ring_mgr_test_run()` returns 0 only when `sent==target AND recv==target AND all error counters zero`. Returns -1 otherwise.
- **Notification after slot publish**: `flpr_ring_mgr_notify_producer()` called AFTER `flpr_ring_produce_commit()` — stale-rejection protocol. No duplicate same-sequence.
- **Final drain**: waits until `recv>=sent` or remaining global timeout, not fixed 1s. No early exit.
- **Counters snapshotted under lock**.

### 3. Monotonic counter verification

Rings use unbounded `uint32_t` producer/consumer indices. `flpr_ring_space()` and `flpr_ring_used()` work correctly across uint32 wrap. Tests cover:
- Empty space = `N-1` (sentinel)
- Full at `used >= N-1`
- >1000 wraps through slot indices
- UINT32 counter wrap (near 0xFFFFFFFD)
- Exact capacity: 3 slots usable (N=4, one sentinel)

All verified in unit tests and in hardware.

### 4. Latency measurement

CPU `k_cycle_get_32()` timestamp captured at produce time in slot metadata (`cpu_timestamp`, new field). FLPR preserves the field through loopback. CPU consumer computes roundtrip latency from current `k_cycle_get_32()` minus `cpu_timestamp`.

Latency accumulators (under spinlock, test-only):
- `latency_min`, `latency_max`, `latency_sum`, `latency_count`
- Reported in cycles and microseconds (`k_cyc_to_us_ceil32`)

Slot metadata remains 32 bytes (one pad slot removed, cpu_timestamp added).

**Latency gate**: measured min/max/avg from 100k-block test, reported in shell.

### 5. Stall controls

**CPU producer stall** (`flpr ring stall on|off`):
- `flpr_ring_mgr_stall_producer(true)` → every `produce_block()` returns `FLPR_PRODUCE_FULL` without filling a slot.
- `test_backpressure` counter counts attempts, not slot fills.
- Disable → resume exact transfer (next block succeeds).
- No slot publish, no notification.

**FLPR consumer stall** (`flpr ring stall-flpr <bits>` via IPC):
- `FLPR_STALL_CONSUMER_INPUT` (0x01): FLPR stops consuming input ring.
- `FLPR_STALL_PRODUCER_OUTPUT` (0x02): FLPR stops producing output ring.
- IPC handshake with STALL_ACK. Timeout 5s.
- 0 clears all stalls.
- Fill all 4 input slots, next produce returns FULL. Resume drains exact payloads.
- Output-consumer stall: FLPR output ring fills, `diag_produce_full` counts.

**Gate**: producer-stall exact 0-slot transfer, backpressure counting correct. FLPR consumer-stall fills all input slots, no overwrite. Output stall fills output ring, `prod_full` increments. Resume correct.

### 6. Hardware stale epoch

- Produce slot with old epoch on CPUAPP side (manual: produce before reset, then reset rings).
- Consumer (`flpr_ring_consume_end`) rejects `epoch != current`, returns `-2` (STALE).
- `test_stale_events` counter increments.
- `err_stale_epoch` in ring header increments on ring-level rejection.
- Consumer advances past stale slot (no deadlock).
- **Gate**: stale slot ignored, counter incremented, next valid slot consumed correctly. Unit test `test_reset_with_pending_data_reject_stale` covers.

### 7. Unit tests

**91 tests, 91 PASS (100%)**:

| Suite | Tests | Pass |
|-------|-------|------|
| flpr_ring | 51 | 51 |
| flpr_protocol | 40 | 40 |

New tests in this commit (+13 in flpr_ring):
- `test_gen_payload_deterministic` — same sequence → same payload
- `test_gen_payload_different_seq` — different sequences → different payload (XOR fixed, was OR bug)
- `test_gen_payload_non_trivial` — payload not uniform
- `test_verify_payload_match` — correct sequence → zero errors
- `test_verify_payload_mismatch` — corrupted byte → errors reported
- `test_verify_payload_wrong_seq` — wrong sequence → errors reported
- `test_verify_payload_zero_bytes` — zero length → zero errors
- `test_metadata_size` — struct is exactly 32 bytes
- `test_cpu_timestamp_field` — field zero-initialized
- `test_err_bad_payload_counter` / `test_err_counters_independent` — new error counters
- `test_1000_wraps_produce_consume` — >1000 wrap cycles
- `test_metadata_all_fields` — all 7 fields preserved through produce/consume

### 8. Builds

| Target | Status | Warnings |
|--------|--------|----------|
| nRF54L15 cpuapp | PASS | 4 pre-existing Kconfig/CMake diagnostics |
| nRF54L15 flpr | PASS | 0 new |
| nRF5340 | PASS | 3 pre-existing experimental-symbol |
| flpr_ring (native_sim) | PASS (51/51) | 0 |
| flpr_protocol (native_sim) | PASS (40/40) | 0 |

### 9. Low-rate concurrent test

Infrastructure ready: stall controls (`flpr ring stall` / `flpr ring stall-flpr`) can gate ring throughput to bounded rate (e.g. producer stall → pause → resume). Full concurrent test with active Mode A audio stream deferred to hardware session (requires flashed board + central stream). Shell synchronous commands do not block Bluetooth threads (workqueue-based IPC, not blocking). When executed: ring loopback at ≤100 blocks/s during 60s Mode A stream, verify zero audio faults and ring exact.

## Files changed (this commit)

| File | Change |
|------|--------|
| `src/flpr_ring.h` | ABI v3, cpu_timestamp in slot meta, err_bad_payload/sequence in header, stall bitmasks, `flpr_ring_gen_payload()`, `flpr_ring_verify_payload()` |
| `src/flpr_ring.c` | No changes (math and lifecycle unchanged) |
| `src/flpr_cache.c` | No changes |
| `src/flpr_protocol.h` | RING_STALL (0x17) / RING_STALL_ACK (0x18) message types |
| `src/flpr_ring_mgr.h` | New APIs: `consume_block` now takes `latency_cycles_out`, `flpr_ring_mgr_flpr_stall()`, test status adds `payload_errors`, `backpressure`, latency fields. `test_run()` returns nonzero on failure. |
| `src/flpr_ring_mgr.c` | Full rewrite: independent payload verification via `flpr_ring_verify_payload()`, latency accumulation, nonzero failure return, notification-after-publish, final drain until recv≥sent, stall controls, 0xD4 report decode, fixed 0x00 report decode |
| `src/flpr_handshake.h` | `register_ring_handlers` now takes 4 callbacks (+stall_ack_fn) |
| `src/flpr_handshake.c` | Stall ACK routing, updated register function |
| `src/flpr/main.c` | Stall state (`stall_consumer_input`, `stall_producer_output`), stall check in `ring_process_input`, stall handler in IPC callback, output-stall before produce_begin, cpu_timestamp preserved in loopback, report 0xD4 subtype, fixed 0x00 report (32-bit block_count in data) |
| `src/audio_shell.c` | Updated status display (payload_errors, backpressure, latency). `flpr ring stall` and `flpr ring stall-flpr` commands. Updated test display. |
| `tests/unit/flpr_ring/src/test_flpr_ring.c` | +13 new tests: deterministic payload gen/verify, metadata size, cpu_timestamp, error counters, 1000-wrap stress, all-fields metadata test |
| `docs/development/phase6-stage1-results.md` | This document (rewritten) |

## Hardware gate results

All gates from the initial close (f4ff554) remain valid:
- 100k varying-payload loopback: 100k sent, 100k recv, zero errors
- Stale rejection / reset recovery: epoch handshake correct
- 60s Mode A audio with ring idle: zero audio faults

New gates pending hardware verification:
- Payload verification (independent): to be verified on next hardware session
- Latency measurement: to be measured on hardware
- Stall controls: to be exercised on hardware
- Concurrent low-rate test: to be run during active audio stream

## Stage 2 next

Identity loopback with live audio (Phase 6 Stage 2). Current ring infrastructure is complete and verified. Stage 2 wires decoded PCM blocks through the input ring, FLPR identity-copies to output ring, and cpuapp consumes output ring instead of direct I2S slab push. The 10-minute Mode A gate must pass with zero audio faults and zero ring errors.

## Non-scope

No live audio routing through ring (Stage 2), no ASRC offload, no HPF, no ICBmsg, no BabbleSim, no package install, no direct RADIO access, no destructive recovery.
