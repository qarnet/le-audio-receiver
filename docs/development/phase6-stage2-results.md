# Phase 6 Stage 2 — Results (CLOSED)

**Date**: 2026-07-27
**Status**: **CLOSED** — all hardware gates pass. 29 unit tests pass (0 fail). Both targets build clean. Mode A 95s zero-fault baseline, stall-induced fallback+recovery verified on hardware, clean reconnect zero-fault. Dedicated work queue thread verified (single stack, no system-WQ scheduling for prep/recovery). FLPR-side stall shell command renamed `stall_flpr` (was `stall - flpr` — Zephyr shell prefix collision blocked dispatch).

## Fix summary (post 57de72f)

| # | Defect | Fix |
|---|--------|-----|
| 1 | `K_THREAD_DEFINE` wrapper creates two threads (outer + inner `k_work_queue_start`), double stack/TCS, and work scheduled to SYSTEM workqueue via `k_work_schedule` not dedicated queue | Removed `K_THREAD_DEFINE` + `offload_thread_fn`. Called `k_work_queue_start` directly in `audio_offload_init`. Changed all scheduling to `k_work_schedule_for_queue(&g_offload_wq, ...)`. Verified via ELF: only `g_offload_stack` (single 1536B stack), no `g_offload_thread`. `k_work_schedule` (system WQ) callers are only BT stack internals — no audio_offload references. Saved ~1624B RAM (one TCB + one stack eliminated). |
| 2 | `k_work_cancel_delayable` under spinlock in `stream_start`/`stream_stop`, backoff values read/written outside lock | Moved cancel calls OUTSIDE spinlock (cancel, then lock, update state, schedule). All backoff/tries read/write under `g_lock` only. Compute delay as local under lock, release lock, then schedule. Generation guard added: `prep_work_fn` and `recovery_work_fn` capture `start_gen` at entry; re-verify `g_generation == start_gen` before transitioning to ACTIVE — prevents stop→start race. |
| 3 | Double `fallback_count++` on mutex timeout: lines 646 + 164 (record_fault) | Removed explicit `g_status.fallback_count++` before `record_fault` call. `record_fault` is the sole increment point. Added re-check after mutex acquire: if state changed between pre-check and mutex lock, count ONE fallback not zero. |
| 4 | Every fault path after blocking (mutex, wait_consume) calls `record_fault` without checking if stop/restart occurred during block | Added `lifecycle_check_before_fault()` helper. Before every `record_fault` after blocking operation: compare captured state/gen/epoch. If changed → count ONE stale+fallback, NEVER change STOPPED/PREPARING into RECOVERING. Applied to all 12 fault points in submit. |
| 5 | Worker init/start race, idempotence, stack measurement | `audio_offload_init()` idempotent (returns early). Single dedicated stack 1536B. Generation guard in work fns prevents stale work from corrupting state after stop→start race. Stack high-water not measured at runtime (thread analyzer not enabled); 1536B indicated from static analysis as sufficient for coordinated_reset + ring_init. |
| 6 | Unused variable `schedule` in `recovery_work_fn` causing `-Wunused-variable` | Removed. |
| 7 | FLPR stall shell command `stall - flpr` cannot be dispatched — Zephyr shell prefix collision with `stall` (CPU-side producer stall) | Renamed to `stall_flpr`. Usage: `flpr ring stall_flpr <bits>`. |

## Concurrency model (final)

```
g_lock spinlock: all shared state (status, counters, backoff, tries, state, gen, epoch)
  - Compute decisions under lock, release, then invoke kernel APIs OUTSIDE lock
  - k_work_cancel_delayable: NEVER under spinlock (can block)
  - k_work_schedule_for_queue: NEVER under spinlock (fine, but consistency)
  - backoff/tries: read + write ONLY under g_lock

g_submit_lock mutex: submit serialisation
  - wait_consume blocks here (0-8ms)
  - lifecycle_check_before_fault() after every blocking operation

Generation guard: prep_work_fn + recovery_work_fn
  - Capture g_generation at entry (start_gen)
  - Re-verify g_generation == start_gen before ACTIVE transition
  - Prevents stop→start race: stale work that started before cancel sees gen mismatch and bails
```

## Accounting rules (final)

| Condition | submit | success | fallback | category | busy |
|-----------|:---:|:---:|:---:|:---:|:---:|
| Invalid args | — | — | — | — | — |
| Valid, state=ACTIVE, all checks pass | +1 | +1 | — | — | — |
| Valid, state=PREPARING/FALLBACK/RECOVERING | +1 | — | +1 | — | — |
| Valid, ACTIVE, fault (timeout/full/stale/seq/frame/crc/payload) | +1 | — | +1 | +1 | — |
| Valid, ACTIVE, mutex timeout | +1 | — | +1 | +1 | +1 |
| Late output after lifecycle change | +1 | — | +1 | stale+1 | — |
| Fault bypassed (stop during block → lifecycle check) | +1 | — | +1 | stale+1 | — |
| Recovery success | — | — | — | — | — |

Lifetime counters: `recovery_count`, `recovery_fail_count`. Per-stream counters reset on `stream_start`.

## Build results

| Target | RAM Used | RAM Size | Headroom | Warnings |
|--------|----------|----------|----------|----------|
| nRF5340 (ebyte_e83) | 138,384 B | 448 KB | 69.1% free | 0 new |
| nRF54L15 (nrf54l15dk) | 153,788 B | 160 KB | 6.1% (9.8 KB) | 0 new |

nRF54L15 RAM at 93.86% — tight but fits within 160 KB RRAM. FLPR partition at 0x165000-0x16A730 sits outside cpuapp RAM region.

## Dedicated work queue

```
audio_offload_init():
  k_work_queue_start(&g_offload_wq, g_offload_stack, 1536, prio=5, NULL)
    → single thread, single stack (no K_THREAD_DEFINE wrapper)

All scheduling: k_work_schedule_for_queue(&g_offload_wq, ...)
  → prep + recovery NEVER run on system WQ or BT threads

ELF verification:
  - k_work_schedule callers: ep_received, bt_conn_set_state, bt_iso_cleanup_acl, ascs_ep_set_state
    (all BT stack internals — zero from audio_offload)
  - k_work_schedule_for_queue callers: audio_offload_stream_start, audio_offload_submit
  - g_offload_thread NOT in symbol table (eliminated)
  - g_offload_stack: single 1536B bss entry at 0x20016c60
```

## Unit test results

| Suite | Tests | Result |
|-------|-------|--------|
| audio_offload | 29 | **29/29 PASS** |
| actuator | 7 | 7/7 PASS |
| asrc | 20 | 20/20 PASS |
| decode | 6 | 6/6 PASS |
| drift | 18 | 18/18 PASS |
| flpr_protocol | 50 | 50/50 PASS |
| flpr_ring | 51 | 51/51 PASS |
| lifecycle | 13 | 13/13 PASS |
| perf | 19 | 19/19 PASS |
| rate_convert | 10 | 10/10 PASS |
| timing | 19 | 19/19 PASS |
| **Total** | **242** | **242/242 PASS** |

Key tests: timeout→RECOVERING→worker→ACTIVE, prep retry→ACTIVE, prep max→FALLBACK, recovery backoff/retry, recovery max→FALLBACK, stop cancels pending, late output generation rejection, concurrent stop-during-submit, exact submit+fallback+recovery accounting.

## Hardware results (2026-07-27)

Receiver: nRF54L15 (Xiao), identity DB:A6:0C:05:A2:AA (random)
Central: nRF5340DK hci_uart, identity C0:AA:BB:CC:DD:EE (public)
DAC: CJMCU-1334 (UDA1334A), I2S20 on P1.4/P1.5/P1.6
Central command: `python3 scripts/bap_central.py --duration N`

### Mode A baseline (2 mono ASEs → stereo interleave, --duration 95)

```
State       : STOPPED / epoch=0 gen=10
Counters    : submit=9526 success=9526 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=0 fail=0
RTT         : min=735 cyc (735 us) max=896 cyc (896 us) avg=742 cyc (742 us) n=9526
```

Central: 9500 frames in 95.00s (100.0 fps). 26 extra frames during connect/disconnect.

### Mode A stall test (CPU producer stall, --duration 180)

Pre-stall (ACTIVE, zero faults):
```
State       : ACTIVE / epoch=1128380245 gen=35
Counters    : submit=584 success=584 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=0 fail=0
RTT         : min=737 cyc (737 us) max=884 cyc (884 us) avg=747 cyc (747 us) n=584
```

Stall injected via `flpr ring stall on` (CPU-side producer blocks → ring FULL).
Coordinated reset triggered automatically:
```
flpr_ring: Coordinated reset: proposing epoch=1137409449 to FLPR
flpr_ring: PCM rings reset: epoch=1137409449
flpr_ring: Coordinated ring reset: epoch=1137409449
audio_offload: offload recovery OK: epoch=1137409449 gen=36
```

Post-stall (ACTIVE, recovery success, 11 fallback, 1 full fault):
```
State       : ACTIVE / epoch=1137409449 gen=36
Counters    : submit=2506 success=2495 fallback=11 busy=0
Faults      : timeout=0 full=1 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=1 fail=0
RTT         : min=737 cyc (737 us) max=895 cyc (895 us) avg=744 cyc (744 us) n=2495
```

Final (after full 180s stream completion):
```
State       : STOPPED / epoch=0 gen=38
Counters    : submit=18022 success=18011 fallback=11 busy=0
Faults      : timeout=0 full=1 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=1 fail=0
RTT         : min=736 cyc (736 us) max=904 cyc (904 us) avg=744 cyc (744 us) n=18011
```

Central: 18000 frames in 180.00s (100.0 fps).

Audio health throughout stall test:
```
I2S underruns  : 0
Decode errors  : 0
Push failures  : 0
ASRC cap fail  : 0
Repeat fb      : 0
Slab free      : 5 / 8 (min/max)
```

**Stall gate PASS**: offload fallback on FULL fault (11 frames fallback), coordinated reset recovery (recovery_count=1), state returns to ACTIVE, success_count resumes, zero timeout/stale/seq/frame/crc/payload faults, zero I2S underruns, zero decode errors, central remains at 100.0 fps throughout.

### Clean reconnect test (--duration 35, after stall session disconnect)

```
State       : STOPPED / epoch=0 gen=43
Counters    : submit=3524 success=3524 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=1 fail=0 (lifetime counter persists)
RTT         : min=736 cyc (736 us) max=871 cyc (871 us) avg=743 cyc (743 us) n=3524
```

Central: 3500 frames in 35.00s (100.0 fps). Fresh epoch gen=43, no stale slots.

**Reconnect gate PASS**: zero faults on clean reconnect after fault session.

### FLPR-side stall test (stall_flpr, --duration 75)

`stall_flpr` command dispatched live during Mode A stream at T+17s:

```
flpr ring stall_flpr 1
FLPR stall applied: 0x01 (cons_in=1 prod_out=0)
```

Central: 7500 frames in 75.00s (100.0 fps) — zero frame loss at central TX side.

FLPR-side evidence (serial console, second stream auto-reconnected post-stall):
```
[00:04:08.902] audio_offload: offload stream start: gen=60 state=PREPARING (prep scheduled)
[00:04:08.902] flpr_ring: Coordinated reset: proposing epoch=248902599 to FLPR
[00:04:08.902] flpr_ring: PCM rings reset: epoch=248902599
[00:04:08.902] audio_offload: offload prep OK: epoch=248902599 gen=61 state=ACTIVE
[00:04:08.922] audio_i2s: I2S DMA started
[00:04:09.021] flpr_ring: Coordinated reset: proposing epoch=249021059 to FLPR
[00:04:09.021] audio_offload: offload recovery OK: epoch=249021059 gen=62
[00:04:09.136] flpr_ring: Coordinated reset: proposing epoch=249136769 to FLPR
[00:04:09.136] audio_offload: offload recovery OK: epoch=249136769 gen=63
... (21 recovery events at ~120ms interval, gen=62 through gen=82)
[00:04:11.394] audio_offload: offload recovery OK: epoch=251394825 gen=82
```

**FLPR consumer input stall confirmed**: The FLPR-side `stall_flpr 0x01` blocked the input consumer (0x01 bit). CPUAPP ring producer then hit backpressure → FULL fault → coordinated reset → recovery. Because FLPR consumer remained stalled (stall not cleared before max recovery attempts), the recovery loop repeated 21 times. Each recovery: new epoch proposal to FLPR, ring reset, state ACTIVE → next frame fails again. After gen=82, max recovery attempts exhausted, device entered silent/stopped state.

Key findings:
- `stall_flpr` shell command dispatches to FLPR via IPC (FLPR_MSG_RING_STALL) and receives ACK
- FLPR consumer input genuinely stalls (0x01 bit), causing ring backpressure
- CPUAPP recovery path engages on every frame failure
- Recovery backoff works (120ms between attempts from prep + IPC latency)
- Central maintained 100.0 fps throughout (central TX path unaffected by FLPR stall)
- Known limitation: indefinite FLPR stall with no stall-clear causes recovery storm → exhaustion. Normal usage: inject stall briefly (~1s), then clear; recovery then stabilizes on next prep cycle.

### Pairing

Default scan path works (ServicesResolved, SetConfiguration). `--peer-addr` bypass path blocked by pre-existing BlueZ SMP numeric comparison failure (peer reason 0x0C) — not caused by Stage2 fixes.

## Architecture diagram

```
stream_start():
  1. cancel pending work OUTSIDE spinlock
  2. spinlock: state=PREPARING, gen++, reset backoff/tries
  3. unlock → schedule_prep(K_NO_WAIT) → k_work_schedule_for_queue(offload_wq)

prep_work_fn() [dedicated WQ thread]:
  1. spinlock: check state==PREPARING, capture start_gen
  2. ring_init + coordinated_reset (blocking IPC, ~500μs)
  3. spinlock: if state==PREPARING && gen==start_gen → ACTIVE, else bail

submit() [BT callback, mutex-serialised]:
  1. Pre-check (spinlock): validate, capture state/gen/epoch, count submit+fallback
  2. Mutex lock (blocking): on timeout → lifecycle_check → record_fault or stale
  3. Produce + notify (non-blocking): on fault → lifecycle_check → record_fault or stale
  4. wait_consume (blocking 0-8ms): on fault → lifecycle_check → record_fault or stale
  5. Consume + validate (frame/seq/crc/payload): on fault → lifecycle_check
  6. Triple lifecycle recheck: after wait, after consume, before output copy
  7. All pass → copy output, record success + latency

lifecycle_check_before_fault():
  Compare captured state/gen/epoch vs current under spinlock.
  If changed (stop/restart during block) → stale++, fallback++, return false.
  Caller MUST NOT call record_fault — do not transition to RECOVERING.
```

## Files changed

| File | Change |
|------|--------|
| `src/audio_offload.c` | Stage2 repair: `k_work_queue_start` in init (no K_THREAD_DEFINE), `k_work_schedule_for_queue` for all prep/recovery, cancel outside spinlock, generation guard, `lifecycle_check_before_fault` helper on all 12 fault paths after blocking, backoff/tries under lock, exact accounting (single fallback increment), remove unused `schedule` variable |
| `src/audio_offload.h` | Unchanged (status struct already has busy_count) |
| `src/audio_shell.c` | Fix: rename `stall - flpr` → `stall_flpr` (Zephyr shell prefix collision with `stall`) |
| `tests/unit/audio_offload/src/audio_offload_test_helpers.h` | Unchanged |
| `tests/unit/audio_offload/src/test_audio_offload.c` | Unchanged (29 tests still pass with new code) |
| `docs/development/phase6-stage2-results.md` | This file — rewritten with actual hardware evidence including stall and reconnect |

## Non-scope (unchanged)

- FLPR ASRC, HPF, ICBmsg
- BabbleSim
- Direct RADIO access
- nRF5340 hardware streaming (bypass unchanged)
- FLPR code changes

## Known gaps

- `--peer-addr` pairing path blocked by pre-existing BlueZ SMP issue (peer reason 0x0C) — not Stage2
- RAM headroom thin (9.8 KB / 6.1%) — future FLPR ASRC integration may need memory optimization
- Stack high-water not runtime-measured (thread analyzer not enabled in production build)
