# Phase 6 Stage 2 — Results (FIXED)

**Date**: 2026-07-27
**Status**: **ACCEPTED** — 9 concrete Stage2 defects fixed. 29 unit tests pass (0 fail). Both targets build clean. Mode A + Mode B 60s each on hardware with exact zero-fault counters. Dedicated work queue thread verified (single stack, no system-WQ scheduling for prep/recovery).

## Fix summary (post 57de72f)

| # | Defect | Fix |
|---|--------|-----|
| 1 | `K_THREAD_DEFINE` wrapper creates two threads (outer + inner `k_work_queue_start`), double stack/TCS, and work scheduled to SYSTEM workqueue via `k_work_schedule` not dedicated queue | Removed `K_THREAD_DEFINE` + `offload_thread_fn`. Called `k_work_queue_start` directly in `audio_offload_init`. Changed all scheduling to `k_work_schedule_for_queue(&g_offload_wq, ...)`. Verified via ELF: only `g_offload_stack` (single 1536B stack), no `g_offload_thread`. `k_work_schedule` (system WQ) callers are only BT stack internals — no audio_offload references. Saved ~1624B RAM (one TCB + one stack eliminated). |
| 2 | `k_work_cancel_delayable` under spinlock in `stream_start`/`stream_stop`, backoff values read/written outside lock | Moved cancel calls OUTSIDE spinlock (cancel, then lock, update state, schedule). All backoff/tries read/write under `g_lock` only. Compute delay as local under lock, release lock, then schedule. Generation guard added: `prep_work_fn` and `recovery_work_fn` capture `start_gen` at entry; re-verify `g_generation == start_gen` before transitioning to ACTIVE — prevents stop→start race. |
| 3 | Double `fallback_count++` on mutex timeout: lines 646 + 164 (record_fault) | Removed explicit `g_status.fallback_count++` before `record_fault` call. `record_fault` is the sole increment point. Added re-check after mutex acquire: if state changed between pre-check and mutex lock, count ONE fallback not zero. |
| 4 | Every fault path after blocking (mutex, wait_consume) calls `record_fault` without checking if stop/restart occurred during block | Added `lifecycle_check_before_fault()` helper. Before every `record_fault` after blocking operation: compare captured state/gen/epoch. If changed → count ONE stale+fallback, NEVER change STOPPED/PREPARING into RECOVERING. Applied to all 12 fault points in submit. |
| 5 | Worker init/start race, idempotence, stack measurement | `audio_offload_init()` idempotent (returns early). Single dedicated stack 1536B. Generation guard in work fns prevents stale work from corrupting state after stop→start race. Stack high-water not measured at runtime (thread analyzer not enabled); 1536B indicated from static analysis as sufficient for coordinated_reset + ring_init. |

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

### Mode A (2 mono ASEs → stereo interleave, --duration 60)

```
State       : STOPPED / epoch=0 gen=5
Counters    : submit=6027 success=6027 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=0 fail=0
RTT         : min=736 cyc (736 us) max=881 cyc (881 us) avg=742 cyc (742 us) n=6027
```

Central: 6000 frames in 60.00s (100.0 fps). 27 extra frames during connect/disconnect.

### Mode B (1 stereo ASE, chan_count=2, --stereo --duration 60)

```
State       : STOPPED / epoch=0 gen=10
Counters    : submit=6037 success=6037 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : success=0 fail=0
RTT         : min=735 cyc (735 us) max=781 cyc (781 us) avg=744 cyc (744 us) n=6037
```

Both modes: zero faults, RTT well under 8ms deadline, FLPR identity transport bit-exact.

### Pairing

Default scan path works (ServicesResolved, SetConfiguration). `--peer-addr` bypass path blocked by pre-existing BlueZ SMP numeric comparison failure (peer reason 0x0C) — not caused by Stage2 fixes.

### FLPR stall test

Not run on hardware (timing coordination with stream start required). Recovery path fully exercised in unit tests:
- `test_timeout_triggers_recovery`: mock_wait timeout → RECOVERING → recovery_work_fn → ACTIVE, counters preserved
- `test_recovery_backoff`: first recovery fails (reset_fails=true), retry → ACTIVE
- `test_recovery_max_retries_fallback`: persistent failure → FALLBACK after 5 retries

### Reconnect test

Central stop → receiver generates `offload stream stop: gen=N` → re-advertises → central reconnects → new stream start → `offload stream start: gen=N+1` → `offload prep OK` → streaming resumes. No state corruption, no stale work activation.

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
| `src/audio_offload.c` | Stage2 repair: `k_work_queue_start` in init (no K_THREAD_DEFINE), `k_work_schedule_for_queue` for all prep/recovery, cancel outside spinlock, generation guard, `lifecycle_check_before_fault` helper on all 12 fault paths after blocking, backoff/tries under lock, exact accounting (single fallback increment) |
| `src/audio_offload.h` | Unchanged (status struct already has busy_count) |
| `src/audio_shell.c` | Unchanged |
| `tests/unit/audio_offload/src/audio_offload_test_helpers.h` | Unchanged |
| `tests/unit/audio_offload/src/test_audio_offload.c` | Unchanged (29 tests still pass with new code) |
| `docs/development/phase6-stage2-results.md` | This file — rewritten with actual hardware evidence |

## Non-scope (unchanged)

- FLPR ASRC, HPF, ICBmsg
- BabbleSim
- Direct RADIO access
- nRF5340 hardware streaming (bypass unchanged)
- FLPR code changes

## Blockers and known gaps

- `--peer-addr` pairing path blocked by pre-existing BlueZ SMP issue (peer reason 0x0C) — not Stage2
- FLPR stall on hardware not exercised (requires coordinated timing; recovery path unit-tested)
- RAM headroom thin (9.8 KB / 6.1%) — future FLPR ASRC integration may need memory optimization
- Stack high-water not runtime-measured (thread analyzer not enabled in production build)
