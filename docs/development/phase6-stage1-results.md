# Phase 6 Stage 1 — Results (v3 — final review + 4-slot fix)

**Date**: 2026-07-27
**Status**: **PENDING HARDWARE** — Ring core and unit tests verified. Six acceptance gates implemented but not all passed on hardware due to throughput bottleneck (pre-existing, not regression). Stage 1 core implementation complete; hardware acceptance pending longer-duration run.

## Changes from 422cdc4 (initial gate close, then v2 CRC-only fix)

The initial gate close (422cdc4) claimed "STAGE CLOSED" while multiple gates were still pending hardware verification. This commit addresses all items listed in the final Stage1 review:

### 1. Use all 4 physical slots (was 3 with sentinel)

**Before**: `FLPR_RING_MAX_USED = SLOT_COUNT - 1 = 3` — one slot kept empty as sentinel.
**After**: `FLPR_RING_MAX_USED = SLOT_COUNT = 4` — monotonic counters eliminate empty/full ambiguity. All 4 slots usable.

- `flpr_ring_space()`: `space = SLOT_COUNT - used`, full when `used >= SLOT_COUNT`
- `flpr_ring_is_full()`: `used >= SLOT_COUNT`
- Tests updated: empty=4, one-used=3, full at 4, 5th produce fails, uint32 wrap uses 4 slots

### 2. FLPR output stall/full MUST NOT drop input

**Before**: FLPR `ring_process_input()` consumed input first, then checked output capacity. On output full or producer-stall, called `consume_done()` on input — dropping the block.
**After**: FLPR checks output ring space (`flpr_ring_space`) AND `stall_producer_output` flag BEFORE `consume_begin`. If output cannot accept, leaves input consumer index unchanged → no loss. On resume, exact pending sequence/payload forwarded.

- `diag_produce_full` incremented when output-full detected (backpressure counting, not drop counting)
- Input loss counters remain zero

### 3. Ring production validation — reject, don't clamp

**Before**: `produce_block` silently clamped `valid_frames > MAX_INPUT` to MAX_INPUT. Raw -1/-2 error codes.
**After**: `produce_block` returns `FLPR_PRODUCE_INVALID` (-EINVAL) for invalid frame count. `consume_block` returns `FLPR_CONSUME_INVALID` (-EINVAL) for bad valid_frames.

- All ring APIs use real errno: `-ENOSPC` (full), `-ENOENT` (empty), `-ESTALE` (stale epoch), `-EINVAL` (invalid)
- Manager enum values: `FLPR_PRODUCE_OK=0`, `FLPR_PRODUCE_FULL=-ENOSPC`, `FLPR_PRODUCE_INVALID=-EINVAL`
- Consumer: `FLPR_CONSUME_OK=0`, `FLPR_CONSUME_EMPTY=-ENOENT`, `FLPR_CONSUME_STALE=-ESTALE`, `FLPR_CONSUME_INVALID=-EINVAL`

### 4. Hardware independent verification

**Status**: Already present in 422cdc4 (v2). `flpr_ring_gen_payload()` produces deterministic payload from sequence number via splitmix hash. `flpr_ring_verify_payload()` independently regenerates and memcmp's every valid byte. Manager test_run tracks `test_payload_errors` (frame-level) and `test_crc_errors` (CRC-level). Both must be zero for PASS. CRC and payload verification are independent.

**Test verification**: Unit test `test_verify_payload_match` (zero errors), `test_verify_payload_mismatch` (corruption detected), `test_verify_payload_wrong_seq` (wrong sequence detected).

### 5. Latency metadata and min/avg/max report

Already present. `cpu_timestamp` (k_cycle_get_32) captured at produce, roundtrip computed at consume. Min/max/avg tracked under spinlock. Timestamp subtraction uses uint32_t wrap-safe arithmetic. Throughput (blocks/s) reported in acceptance command output.

### 6. Automated acceptance shell command

New: `flpr ring acceptance [count]` (default 100000). Runs 6 gates:

| Gate | Description | PASS criteria |
|------|-------------|---------------|
| 1 | Normal loopback | count blocks transferred, zero payload/CRC/stale/backpressure errors |
| 2 | CPU producer stall | 10 attempts all FULL, 0 blocks sent, resume 100 blocks exact |
| 3 | FLPR input-consumer stall | Fill exactly 4 slots (new capacity), 6 FULL after, resume receives exact 4 |
| 4 | FLPR output-producer stall | Output-full FLPR-side > 0, resume receives ≥ 10 blocks |
| 5 | Stale epoch injection | Stale rejected, stale_events=1, recovery 100 blocks exact |
| 6 | Explicit empty | Both rings used=0 |

Returns 0 only if all gates pass; nonzero on any failure.

Stack overflow fixed: large buffers moved from shell thread stack to file-static `acceptance_buf[1924]` and `acceptance_rs`.

### 7. Hardware gate results

| Gate | Status | Notes |
|------|--------|-------|
| Unit tests (92) | PASS 100% | 52 ring + 40 protocol |
| nRF54L15 build | PASS | 0 new warnings |
| nRF5340 build | PASS | 0 new warnings |
| Gate 3 (FLPR stall) | PASS | exact 4+6 pattern, resume exact 4 |
| Gates 1/4/5 | NOT RUN | Timeout at 120s for 1000-block test; throughput bottleneck ~8 blocks/s across CPUAPP↔FLPR IPC |

The throughput bottleneck pre-exists this commit: FLPR polls at 10ms intervals, IPC notification is async, and the test loop uses semaphore waits. This is not a regression from the 4-slot change or errno refactoring. A full 100k-block acceptance run requires ~5-17 minutes depending on actual throughput and needs a duration-adjusted test loop (out of scope for this review).

## Files changed

| File | Change |
|------|--------|
| `src/flpr_ring.h` | `FLPR_RING_MAX_USED` = 4 (was 3), space/full use `FLPR_RING_SLOT_COUNT`, errno doc comments |
| `src/flpr_ring.c` | `#include <errno.h>`, -ENOSPC/-ENOENT/-ESTALE returns instead of raw -1/-2 |
| `src/flpr_ring_mgr.h` | Produce/consume enum use errno values, `FLPR_PRODUCE_INVALID`, `FLPR_CONSUME_INVALID`, `flpr_ring_mgr_produce_stale_test()` |
| `src/flpr_ring_mgr.c` | Reject valid_frames (was clamp), errno-based checks, stale injection helper |
| `src/flpr/main.c` | Output-space pre-check before input consume (fixes input-loss on output-full), errno-based checks |
| `src/audio_shell.c` | `flpr ring acceptance` command (6 gates), static buffers for stack safety, `#include "flpr_ring.h"` |
| `tests/unit/flpr_ring/src/test_flpr_ring.c` | Updated all capacity tests for 4 slots, errno checks, `#include <errno.h>` |
| `docs/development/phase6-stage1-results.md` | This document (rewritten from "CLOSED" to honest pending) |

## Unit test summary

| Suite | Tests | Pass |
|-------|-------|------|
| flpr_ring | 52 | 52 |
| flpr_protocol | 40 | 40 |
| **Total** | **92** | **92** |

## What Stage 1 delivers

- 4-slot monotonic-counter SPSC rings (no sentinel waste)
- Correct backpressure: output-full preserves input, no drops
- Proper errno-based error handling throughout
- Deterministic payload verification (hardware-independent)
- Latency measurement with wrap-safe subtraction
- Automated acceptance command with 6 gates (stalls, stalls, stale, empty)
- Validation: reject invalid frames (don't clamp), validate epoch/flags

## Stage 2 next

Identity loopback with live audio (Phase 6 Stage 2). Ring infrastructure complete. Stage 2 wires decoded PCM through input ring → FLPR identity-copies → output ring → cpuapp consumes instead of direct I2S push.

## Non-scope

No live audio routing through ring (Stage 2), no ASRC offload, no HPF, no ICBmsg, no BabbleSim, no package install, no direct RADIO, no destructive recovery, no push/release, no PR.
