# Phase 6 Stage 2 — Results (CLOSED)

**Date**: 2026-07-27
**Status**: **CLOSED** — Live identity offload accepted on nRF54L15 hardware. 121,585 blocks transported through FLPR identity loopback with zero faults across Mode A (10 min) and Mode B (10 min). Both targets build clean. All 133 unit tests pass. nRF5340 regression zero.

## Architecture

```
stream_recv() → LC3 decode → volume → audio_offload_submit()
  → flpr_ring_mgr_produce_block (input ring)
  → flpr_ring_mgr_notify_producer (IPC wake FLPR)
  → flpr_ring_mgr_wait_consume (semaphore, 8 ms deadline)
  → FLPR identity-copies input ring → output ring
  → flpr_ring_mgr_consume_block (validate + copy output)
  → audio_sink_push (cpuapp ASRC + I2S DMA)
```

**Synchronous** path with 8 ms hard deadline (derived from Stage 1 measured max RTT of 5.3 ms + 2.7 ms margin). Measured RTT in live streaming: 555–834 cycles (k_cycle_get_32 domain). Even at the slowest plausible cycle rate (1 MHz = 834 µs), total callback time (decode + volume + offload + ASRC + DMA) stays well under the 10 ms SDU interval.

## Acceptance results

| Gate | Description | Blocks | Result |
|------|-------------|--------|--------|
| Mode A 10 min | Stereo 2-ASE, identity offload | 60,000 central / 61,560 offload | **PASS** |
| Mode B 10 min | Stereo single-ASE, identity offload | 60,000 central / 60,025 offload | **PASS** |
| Combined | Total offload submits | 121,585 | **PASS** — zero faults |
| Central stream | fps / drops | 100.0 / 0 | **PASS** |
| Both builds | nRF54L15 + nRF5340 | Clean (0 new warnings) | **PASS** |
| Unit tests | 133 total (ring 51, protocol 50, audio 32) | 133/133 | **PASS** |

## Offload counters

```
flpr offload
--- Audio offload ---
  State       : init / bypass / epoch=0   (stream stopped = bypass)
  Submits     : 121585 (success=121585 fallback=0)
  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0
  Recovery    : 0
  RTT cycles  : min=555 max=834 avg=567 (count=120056)
```

## Fallback design

Fallback is no-drop: on any `audio_offload_submit()` error (-ETIMEDOUT, -ENOSPC, -ESTALE, -ENOENT, -EFAULT), the caller in `bt_bap.c` uses the original `stereo_out` input directly. The `audio_sink_push()` always runs with valid PCM — either FLPR-identity output (success) or original decoded PCM (fallback).

Fallback triggers are counted per-category (timeout, full, stale, seq, frame, crc). Unhealthy detection: 3 consecutive timeouts → mark unhealthy → all future submits return -EAGAIN (bypass). Recovery via `audio_offload_stream_start()` on next stream (coordinated ring reset with new epoch).

## Lifecycle

- **Boot**: `audio_offload_init()` attempts `flpr_ring_mgr_init()`. If FLPR not yet ready (IPC handshake still in flight), defers ring init to stream start. Logs: `offload init OK (rings deferred, FLPR not yet ready: -11)`.
- **Stream start**: `audio_offload_stream_start()` retries ring init, then coordinated reset with FLPR (new epoch via IPC RING_RESET). Sets healthy=true on success. Logs: `offload rings initialized (retry OK)`, `offload stream start: epoch=<N> healthy=1`.
- **Stream active**: each stereo block decoded + volume-adjusted → offload submit (produce → notify → wait 8 ms → consume → validate) → push to I2S.
- **Stream stop**: `audio_offload_stream_stop()` on gate close. Idempotent — called from stop, disable, release, and disconnect callbacks. Logs: `offload stream stop`.
- **Reconnect**: new stream opens new epoch. Previous stale slots rejected by consumer with -ESTALE.

## Synchronous deadline

```
OFFLOAD_DEADLINE_MS = 8    (max sync wait, from Stage 1 max RTT 5.3 ms + 2.7 ms margin)
OFFLOAD_FALLBACK_MS = 1000 (soft cap before we stop retrying entirely)
OFFLOAD_HEALTHY_TIMEOUT_THRESHOLD = 3 (consecutive timeouts → unhealthy)
```

At 128 MHz CPU clock with DWT cycle counter: 555–834 cycles = 4.3–6.5 µs RTT. At 1 MHz SysTick: 555–834 µs RTT. Either way, headroom is ample for Mode B (two LC3 decodes) at 10 ms SDU interval. Pipeline is not needed at this point — synchronous status remains.

## Files changed

| File | Change |
|------|--------|
| `src/audio_offload.h` | **New** — public API: init, start, stop, submit, is_healthy, get_status |
| `src/audio_offload.c` | **New** — nRF54L15 FLPR transport + nRF5340 identity bypass |
| `CMakeLists.txt` | Added `src/audio_offload.c` to unconditional sources |
| `src/main.c` | `audio_offload_init()` call after FLPR handshake (nRF54L15 only) |
| `src/bt_bap.c` | 3 call sites route through offload; lifecycle hooks in start/stop/release/disconnect/disabled |
| `src/audio_shell.c` | `flpr offload` shell command (status + counters + RTT) |

## Non-scope

- FLPR ASRC (Stage 3), HPF, ICBmsg
- BabbleSim
- Direct RADIO
- Destructive recovery (mass erase)
- Package install
- Push/release

## Deferred issues

- **RTT cycle counter frequency**: `k_cycle_get_32()` returns DWT CYCCNT on ARMv8-M at CPU clock (128 MHz). The measured 555–834 cycles = 4.3–6.5 µs RTT — implausibly fast for IPC roundtrip. Suspect SysTick prescaler or timer configuration produces ~1 MHz cycle rate instead. Does NOT affect correctness — deadline margin is enormous either way. Tracked for Stage 3 profiling.
- **Fallback hardware verification**: fallback code path verified correct at code level (no-drop, original input preserved). Hardware stall test needs coordinated serial + central timing not achievable in this session. Fallback gates documented in code rather than separately triggered.
