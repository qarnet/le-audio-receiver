# Phase 6 Stage 1 — Results

**Date**: 2026-07-27  
**Status**: Core infrastructure validated. Coordinated reset + ring init working on hardware. Loopback test times out (FLPR not processing IPC — likely ICMsg buffer issue; root cause TBD).

## Executed defects fixed (from handoff)

### 1. Ring index math (critical)
Old `flpr_ring_space`/`flpr_ring_count` broke after index wrap beyond slot_count.
**Fix**: Monotonic uint32 counters. `used = producer - consumer` (always correct).
`full = used >= slot_count - 1`. `empty = used == 0`. `slot = counter % count`.
Tested: 100+ wraps, uint32 counter wrap near 2³². All correct.

### 2. Memory ordering
FLPR had NO-OP barriers (RV32E in-order assumed sufficient — wrong).
**Fix**: FLPR now uses `barrier_dmem_fence_full()` → on RV32 with BARRIER_OPERATIONS_BUILTIN:
`__atomic_thread_fence(__ATOMIC_SEQ_CST)` = full hw+compiler fence.
CPUAPP already used `barrier_dsync_fence_full()` → `__DSB()`. Correct.
Cache claim: SRAM on S-AHB bus not cached by CACHEDATA/ICACHE (NVM-only peripherals).
CONFIG_DCACHE not set. Generated MPU attributes: Normal WB-NoAlloc, Shareable.
Evidence: Nordic nRF54L15 OPS §5.6.1, generated .config, zephyr.map.

### 3. Devicetree addresses
Hardcoded `0x2002C000`/`0x2002E000` replaced with `DT_NODELABEL(pcm_ring)` +
`DT_REG_ADDR`/`DT_REG_SIZE`. BUILD_ASSERT: exactly 16 KiB, 8 KiB per ring,
aligned, contiguous, below FLPR SRAM at 0x20030000. Both CPUAPP and FLPR
use DT. Generated DTS confirms: 0x2002C000..0x20030000.

### 4. Capacity future-proof
Slot payload capacity: 481 stereo frames (1924 bytes). Input restricts ≤480.
Output permits ≤481. Slot stride 2016 bytes confirmed fits 481 frames.
CRC computed over valid_frames × 4 bytes (valid payload only). Remainder zeroed.
Verified: 481-frame slot does not clobber next slot metadata.

### 5. Error counters single-writer
`err_producer_full`: only writer is producer (CPUAPP for input, FLPR for output).
`err_consumer_empty`: only writer is consumer.
`err_stale_epoch`: only writer is consumer.
No cross-core increment races. Documented in header comment.

### 6. Stage0 ICMsg endpoint + RING_RESET_REQUEST/ACK
Handshake module extended: `flpr_handshake_send_msg()`,
`flpr_handshake_register_ring_handlers()`. Ring manager registers callbacks for
RING_RESET_ACK, RING_CONSUMER, RING_TEST_REPORT. Two-phase coordinated reset:
CPUAPP proposes epoch → FLPR initializes+acks → CPUAPP applies. **Verified on hardware**.

### 7. CPUAPP↔FLPR bit-exact loopback
FLPR consumer: validates CRC over valid bytes, copies payload bit-exact to output
ring, recomputes CRC, publishes. CPUAPP consumer verifies. Infrastructure complete;
**loopback roundtrip test pending** (FLPR not receiving RING_PRODUCER IPC reliably).

### 8. Shell ring test
Uses `k_sem` for backpressure (consumer notification), not `k_msleep(1)`.
Drain-then-produce loop with 100ms consumer semaphore timeout.
Test buffers moved to static allocation (1924 bytes each — too large for shell
thread stack; caused stack overflow crash — fixed).

### 9. Stall injection
`flpr_ring_mgr_stall_producer(bool)` implemented. When active, every
produce_block returns FULL without touching ring. Counter tracked.

### 10. Unit tests
**38 tests, 38 PASS (100%)**:
- Init, magic, version, field validation (bad magic/version/slot_count/capacity)
- Monotonic space/count/empty/full (at 0, at wrap, near UINT32_MAX)
- 100+ wraps through slot indices
- UINT32 counter wrap (near 2³²)
- Produce/consume: single, full, empty, wrap batches
- CRC-32: known vector 0xCBF43926, zero-length, different data, valid bytes only
- Epoch reset, nonzero reject, zero reject, stale epoch rejection, idempotent
- Slot arithmetic, 481-frame capacity, canary/no-clobber, max-input 480
- Two-ring independence, slot isolation
- Error counter single-writer
- Reset with pending data → stale rejected

### 11. Builds
| Target | Status | FLASH | RAM |
|--------|--------|-------|-----|
| nRF54L15 cpuapp | PASS | 474,888 B / 1428 KB | 141,812 B / 160 KB (86.56%) |
| nRF54L15 flpr | PASS | 28,568 B / 96 KB | 36,416 B / 64 KB (55.57%) |
| nRF5340 | PASS | 363,952 B / 1008 KB | 136,456 B / 448 KB |

Zero new warnings. nRF5340 has zero ring/FLPR impact.

### 12. Hardware verification
- FLPR handshake: READY + READY_ACK on every boot (verified via serial log).
- `flpr ring init`: rings initialized at DT addresses, 481-frame capacity confirmed.
- `flpr ring status`: indices, epoch, initialized state confirmed.
- `flpr ring reset`: **coordinated two-phase reset works** (RING_RESET → FLPR ACK → apply). Epoch synchronized across cores.
- `flpr ring test 20`: test starts, produces 4 blocks, then stalls on FLPR not draining. Full=591, FLPR=0. Timeout at 30s.
  - Root cause: FLPR not processing input slots. RING_PRODUCER IPC sent but FLPR
    never increments block counter. Possible causes: ICMsg buffer full from heartbeat,
    FLPR poll loop not seeing updated producer_idx (barrier gap), or RING_TEST_START
    not reaching FLPR.
  - Investigation deferred to Stage 1.1.

## Files changed (from 9e956bd partial commit)

| File | Change |
|------|--------|
| `src/flpr_ring.h` | Monotonic counters, ABI v2, 481-frame capacity, proper docs |
| `src/flpr_ring.c` | Fixed space/count math, CRC over valid bytes, idempotent reset, barrier ordering |
| `src/flpr_cache.c` | FLPR: barrier_dmem_fence_full (was NO-OP). ARM: DMB for ordering, DSB for full. Cache docs. |
| `src/flpr_ring_mgr.h` | DT addresses, coordinated reset API, stall, status v2 |
| `src/flpr_ring_mgr.c` | DT_BUILD_ASSERT, IPC handlers, coordinated reset, static test buffers |
| `src/flpr_handshake.h` | Added send_msg + ring handler registration API |
| `src/flpr_handshake.c` | Ring message routing + public send/handler functions |
| `src/flpr/main.c` | DT addresses, BUILD_ASSERT, CRC over valid bytes in loopback |
| `src/audio_shell.c` | Coordinated reset command, updated status fields |
| `tests/unit/flpr_ring/src/test_flpr_ring.c` | 38 tests: wraps, uint32 wrap, 481-frame, valid-bytes CRC, idempotent reset |
| `docs/development/phase6-stage1-results.md` | This document |

## Non-scope
- No live audio routing, ASRC offload, HPF, ICBmsg, BabbleSim, package install,
  direct RADIO, destructive recovery. These are later stages.
