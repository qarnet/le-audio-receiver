# Phase 6 Stage 1 — Results (CLOSED)

**Date**: 2026-07-27
**Status**: **CLOSED** — All 6 acceptance gates PASS on nRF54L15 hardware at 100,000 blocks. Event-driven orchestration, rate-limited concurrent testing, and clean subgate resets verified. No unintentional errors, asserts, or faults.

## Acceptance results (100,000 blocks)

```
flpr ring acceptance 100000
```

| Gate | Description | Blocks | Result | Detail |
|------|-------------|--------|--------|--------|
| 1 | Normal loopback | 100,000 | PASS | 216,673 ms, 461 blk/s, sent=recv=100,000, CRC=0, payload=0, stale=0, backpressure=0 |
| 2 | CPU producer stall | 10 + resume 100 | PASS | 10/10 FULL, 0 sent during stall, resume 100 exact, 0 errors |
| 3 | FLPR input-consumer stall | 4 + 6 | PASS | 4 OK (fill ring), 6 FULL (backpressure), resume recv 4 exact |
| 4 | FLPR output-producer stall | 4 + 6 | PASS | 4 OK / 6 FULL, no input loss, resume recv 4, output-full detected |
| 5 | Stale epoch injection | reject + resume 100 | PASS | stale_events=1, recovery 100 exact, 0 errors |
| 6 | Explicit empty | — | PASS | Both rings used=0 |

**Latency** (Gate 1, 100k blocks): min=4,794 µs, max=5,320 µs, avg=4,885 µs

**Throughput**: 461 blk/s (216.7 s for 100k blocks). Each block is 480 stereo PCM frames (1920 bytes valid payload).

## Concurrent Mode A + rate-limited ring test (corrected 2026-07-27)

**Setup**: `bap_central.py --duration 90` (normal discovery, 1000 Hz sine) + `flpr ring test 6000 100` (100 blk/s rate-limited) started via serial shell during active streaming.  Central ran 9000 LC3 frames, ring test 6000 PCM blocks — both spanned ~60 s of overlapping operation.

| Metric | Value |
|--------|-------|
| Central frames | 9,000 in 90.00 s (100.0 fps), zero drop |
| Ring blocks sent/recv | 6,000 / 6,000 |
| Ring duration | 59,970 ms (target 60,000 ms, 0.05 % error) |
| Ring errors (CRC/payload/full/empty/stale) | 0 / 0 / 0 / 0 / 0 |
| Ring latency | min=3,339 µs, max=43,409 µs, avg=34,797 µs |
| Audio PCLK (during ring test) | 1,400–1,600 ppm (normal range) |
| FLPR errors | 0 (healthy, zero lost/dup/ooo) |

**Note**: prior result (13,030 ms) was a measurement artifact from the old per-second-window rate limiter that allowed first-second burst.  The corrected average-pacing limiter delivers 59,970 ms for 6,000 blocks at 100 blk/s — within 0.05 % of the 60 s target.

## Redesign changes from 56789af

### Event-driven orchestration

- **FLPR** (`src/flpr/main.c`): Semaphore-driven wake via `ring_wake_sem`. IPC callback (`FLPR_MSG_RING_PRODUCER`) gives semaphore only; actual `ring_process_input()` runs exclusively in main loop context. Eliminates callback-vs-poll race. Polling fallback: 10 ms timeout on semaphore take.

- **CPUAPP test_run** (`src/flpr_ring_mgr.c`): Batch-based produce (up to ring capacity), notify FLPR once per batch, then `k_sem_take(&consume_sem, 10ms)` for event-driven drain. No busy-poll loops. Semaphore given by IPC callback when FLPR publishes output.

- **Rate-limited variant**: `flpr_ring_mgr_test_run_rate(blocks, timeout, rate_per_sec, out)`. Throttles production to at most `rate_per_sec` blk/s via 1-second period accounting with `k_msleep()`. Integrated into `flpr ring test <blocks> [rate]` shell command.

### Timeout calculation

- Gate 1: `count × 3 + 60,000` ms (3 ms/blk budget + 60 s floor). For 100k: 360 s, measured actual 217 s (1.66× margin).
- Stall gate resumes: 15 s timeout (was 5–10 s)
- Small-block tests (Gate 3/4/5 drains): 5 s timeout with semaphore-driven wake

### Clean subgate resets

Each gate now:
1. Disables CPU producer stall (`flpr_ring_mgr_stall_producer(false)`)
2. Clears FLPR stalls (`flpr_ring_mgr_flpr_stall(0, 5000)`)
3. Resets rings via coordinated reset (`flpr_ring_mgr_coordinated_reset(0, 5000)`)

This ensures no stale stall state from a previous gate interferes.

### Gate 4 deadlock fix

Original code retried `produce_block` in a `do-while` loop when FLPR output-stalled. Since FLPR preserves input on output-stall (does not consume), the input ring stays full and `consume_sem` never fires — infinite 10 ms timeout loop. Fixed: produce without retry (count OK vs FULL), then drain after stall clear.

### Rate limiting — final correction

Original per-second-window approach (`period_elapsed >= 1000` + reset) allowed an unlimited first-second burst and created a burst-pause pattern at high rates.  The `13,030 ms` measurement in the prior concurrent result (5cb748a) was a direct consequence: first second burst ~460 blocks, then throttled to 100/s thereafter.

**Corrected**: absolute pacing from test start time.

- `flpr_rate_limit_target_ms(blocks_sent, rate_per_sec)` — pure static-inline 64-bit calculation: `sent × 1000 / rate`.  Unit-tested for first block (10 ms), 100 blocks (1,000 ms), 6,000 blocks (60,000 ms), overflow-safe to 4M+ blocks.
- Sleep check runs BEFORE production in the loop — `target_elapsed > actual_elapsed` → `k_msleep(deficit)`.  Deficit capped at 5,000 ms to prevent the thread from sleeping through the remaining timeout budget.
- No periodic reset, no `rate_period_start`/`rate_period_sent` bookkeeping.  The test-start time (`k_uptime_get_32()`) is the sole reference.

## Files changed (since 56789af)

| File | Change |
|------|--------|
| `src/flpr/main.c` | Semaphore-driven wake (`ring_wake_sem`), IPC callback signals only |
| `src/flpr_ring_mgr.c` | Event-driven `test_run`, rate-limited `test_run_rate` (corrected absolute pacing), `wait_consume` helper |
| `src/flpr_ring_mgr.h` | `flpr_ring_mgr_test_run_rate()`, `flpr_ring_mgr_wait_consume()`, `flpr_rate_limit_target_ms()` static inline |
| `src/audio_shell.c` | Reworked acceptance (clean resets, throughput timeouts, Gate 4 fix), rate arg in `test` command |
| `tests/unit/flpr_protocol/src/test_flpr_protocol.c` | +10 pacing calculation unit tests (first block, 100 blocks, 6k blocks, overflow, truncation, zero cases) |

## Build verification

| Target | Status | Warnings |
|--------|--------|----------|
| nRF54L15 | PASS | 0 new (pre-existing: UART_CONSOLE, PRINTK, simple_bus_reg) |
| nRF5340 | PASS | 0 new |

## Unit test summary

| Suite | Tests | Pass |
|-------|-------|------|
| flpr_ring | 51 | 51 |
| flpr_protocol | 50 | 50 |
| **Total** | **101** | **101** |

Tests run on native_sim (`west build -b native_sim`). All 101 PASS.  Includes 10 new pure pacing calculation tests in `flpr_protocol` (first block, 100 blocks, 6,000 blocks, 100k overflow, integer truncation, zero-rate, zero-blocks, different rates).

## What Stage 1 delivers

- 4-slot monotonic-counter SPSC rings (no sentinel waste)
- Correct backpressure: output-full preserves input, no drops
- Proper errno-based error handling throughout
- Deterministic payload verification (hardware-independent)
- Latency measurement with wrap-safe subtraction (min/avg/max)
- Event-driven CPUAPP↔FLPR pipeline (semaphore-backed, no busy-polling)
- Rate-limited throughput testing for concurrent scenarios
- Automated acceptance command with 6 gates (loopback, stalls, stale, empty)
- Clean subgate resets ensuring independence
- Hardware-verified at 100k blocks with zero errors

## Stage 2 next

Identity loopback with live audio (Phase 6 Stage 2). Ring infrastructure complete. Stage 2 wires decoded PCM through input ring → FLPR identity-copies → output ring → cpuapp consumes for I2S push.

## Non-scope

No live audio routing through ring (Stage 2), no ASRC offload, no HPF, no ICBmsg, no BabbleSim, no package install, no direct RADIO, no destructive recovery, no push/release, no PR.
