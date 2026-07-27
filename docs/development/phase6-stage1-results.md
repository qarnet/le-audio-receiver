# Phase 6 Stage 1 — Results

**Date**: 2026-07-27  
**Status**: **STAGE CLOSED** — infrastructure complete, all gates passed. Root cause found: FLPR stack overflow from 1924B local array on 1024B main thread stack. Fixed + diagnostic counters added + test loop optimized. 100k blocks zero-loss loopback PASS. 60s Mode A audio zero faults with ring idle.

## Root cause diagnosis

`ring_process_input()` on FLPR declared `uint8_t recv_payload[FLPR_RING_PAYLOAD_CAPACITY_BYTES]` (= 1924 bytes) as a local variable. FLPR `CONFIG_MAIN_STACK_SIZE=1024` → stack overflow on first call. Corrupted return address / local state, silently prevented ring processing. Heartbeat IPC continued working because it runs before `ring_process_input()` in the loop and uses minimal stack.

**Evidence**: OpenOCD `mdw 0x2002C000` showed 3 produced input slots (prod=3) but consumer_idx=0 — FLPR never called `flpr_ring_consume_done()`. After fix, 100k blocks produce/consume indices match exactly.

## Fixes applied

### Stack overflow
- `recv_payload` moved from function-local to file-static (single-writer, no concurrency on FLPR)
- `CONFIG_MAIN_STACK_SIZE` raised from 1024 to 4096 in `src/flpr/prj.conf`

### Missing notification from polling path
FLPR's main loop polls `ring_process_input()` every 10ms but never sent RING_CONSUMER to CPUAPP. If IPC notification dropped, CPUAPP waited forever.
**Fix**: polling path now calls `ring_notify_cpuapp()` after processing. Only sends if slots actually consumed.

### `flpr_ring_mgr_notify_producer()` return value unchecked
Silent IPC send failures caused undetected notification loss.
**Fix**: notify_sent/notify_err counters tracked, return value checked in test loop.

### Spurious RING_CONSUMER on empty ring
IPC callback sent RING_CONSUMER even when ring was empty (causing spurious semaphore gives).
**Fix**: `ring_notify_cpuapp(consumed)` only sends when `consumed > 0`.

### Test loop bottleneck
Old per-block `k_sem_take(100ms)` dominated at high block counts (59ms/block average).
**Fix**: drain-driven loop with aggressive output draining, semaphore only on FULL, no per-block wait. Throughput improved from 59ms/block to 0.77ms/block (76x).

### Diagnostic counters
Added bounded counters for IPC dispatch tracing:
- CPUAPP: notify_sent, notify_err, sem_gives, sem_takes
- FLPR: notif_rcv, worker_wake, cons_ok, cons_empty, cons_stale, prod_ok, prod_full
- Shell displays both sets under `flpr ring status`

### Protocol comment fix
Swapped comments for `FLPR_MSG_RING_PRODUCER` / `FLPR_MSG_RING_CONSUMER` in `flpr_protocol.h` corrected.

### Multi-report FLPR diagnostics
RING_TEST_STOP sends 4 sequential RING_TEST_REPORT messages with subtype markers (seq high byte 0xD1/0xD2/0xD3) for full 32-bit diagnostic counters. Avoids 16-bit truncation on 100k-block tests.

## Hardware gate results

### 100,000 varying-payload loopback
```
flpr ring init → flpr ring reset → flpr ring test 100000

Sent=100000 Recv=100000 CRC_Err=0 Full=0 Empty=0 Stale=0
Duration: 76598 ms (0.77 ms/block)
PASS: all 100000 blocks transferred, zero CRC errors
```

Ring state after test:
```
Input  (→FLPR): prod=100000 cons=100000 used=0 space=3
Output (→CPU): prod=100000 cons=100000 used=0 space=3
Diag (CPUAPP): notify=100000 err=0 sem_give=99998 sem_take=0
Diag (FLPR):   cons_ok=100000 prod_ok=100000 prod_full=0
```

Zero CRC errors, zero full, zero empty, zero stale. Producer/consumer indices match exactly (100k each ring). FLPR processed exactly 100k slots with perfect symmetry.

### Stale rejection / reset recovery
- `flpr ring reset` → clean state → `flpr ring test 1` → PASS → `flpr ring reset` → `flpr ring status` shows prod=0 cons=0
- Reset with new epoch (3089291611) properly rejects old epoch slots (epoch check in `flpr_ring_consume_begin`)
- Idempotent reset: both sides agree on epoch via RING_RESET/ACK handshake
- OpenOCD verification: epoch 3089291611 committed on both input and output ring headers

### Empty explicit
`flpr ring test 1` after full reset: Sent=1 Recv=1, zero CRC errors. Duration 1002ms (dominated by final drain wait).

### 60s Mode A audio with ring idle
- 60s stereo audio stream via bap_central.py: 6000 frames in 60.00s (100.0 fps)
- Audio status: 0 decode errors, 0 I2S underruns, 0 stream resets
- Ring status: prod=0 cons=0 (ring idle — correct for Stage 1)
- FLPR health: Ready/ACKed/Healthy, zero errors, heartbeats flowing (TX 666, RX 659)
- **Zero audio faults, zero FLPR faults**

### Unit tests
**38 tests, 38 PASS (100%)** — monotonic counters, 100+ wraps, UINT32 wrap, CRC, epoch, capacity, canaries, stale rejection, error counters, two-ring independence.

### Builds
| Target | Status | FLASH | RAM |
|--------|--------|-------|-----|
| nRF54L15 cpuapp | PASS | 476,520 B / 1428 KB | 145,700 B / 160 KB (88.93%) |
| nRF54L15 flpr | PASS | 28,824 B / 96 KB | 41,696 B / 64 KB (63.62%) |
| nRF5340 | PASS | — | — |

Zero new warnings (all pre-existing: PARTITION_MANAGER deprecation, BT_LL_SW_SPLIT experimental, DTS simple-bus-reg).

## Files changed (cumulative from 9e956bd)

| File | Change |
|------|--------|
| `src/flpr_ring.h` | Monotonic counters, ABI v2, 481-frame capacity, proper docs |
| `src/flpr_ring.c` | Fixed space/count math, CRC over valid bytes, idempotent reset, barrier ordering |
| `src/flpr_cache.c` | FLPR: barrier_dmem_fence_full (was NO-OP). ARM: DMB/DSB. Cache docs |
| `src/flpr_ring_mgr.h` | DT addresses, coordinated reset API, stall, status v2, diagnostic counters |
| `src/flpr_ring_mgr.c` | DT_BUILD_ASSERT, IPC handlers, coordinated reset, static test buffers, drain-driven test loop, diagnostic counters |
| `src/flpr_handshake.h` | Added send_msg + ring handler registration API |
| `src/flpr_handshake.c` | Ring message routing + public send/handler functions |
| `src/flpr/main.c` | DT addresses, BUILD_ASSERT, static recv_payload, polling-path notification, multi-report diagnostics, ring_notify_cpuapp helper |
| `src/flpr/prj.conf` | MAIN_STACK_SIZE=4096 (was default 1024) |
| `src/flpr_protocol.h` | Fixed swapped RING_PRODUCER/RING_CONSUMER comments |
| `src/audio_shell.c` | Coordinated reset command, updated status fields, diag display |
| `tests/unit/flpr_ring/src/test_flpr_ring.c` | 38 tests: wraps, uint32 wrap, 481-frame, valid-bytes CRC, idempotent reset |
| `docs/development/phase6-stage1-results.md` | This document |

## Non-scope
- No live audio routing through ring, no ASRC offload, no HPF, no ICBmsg, no BabbleSim, no package install, no direct RADIO access, no destructive recovery. These are later stages.
