# Phase 6 Stage 2 — Results (CLOSED)

**Date**: 2026-07-27 (Stage 2 closeout), 2026-07-28 (v3 protocol hardware acceptance)
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
| nRF5340 CPUAPP (ebyte_e83) | 138,384 B | 448 KB | 69.1% free | 0 new |
| nRF54L15 CPUAPP (nrf54l15dk) | 153,812 B | 160 KB | 6.1% (10.1 KB) | 0 new |
| nRF54L15 FLPR | 41,844 B | 64 KB | 36.2% (22.2 KB) | 0 new |

**Correction** (2026-07-27): prior doc incorrectly stated nRF54L15 RAM = 41,856 B. That figure is the FLPR binary size, not CPUAPP RAM. CPUAPP RAM is 153,812 B (93.88% used) with headroom of ~10 KB. FLPR code at 0x165000-0x16A730 sits outside CPUAPP RAM region.

Full `size` output:
```
nRF5340 CPUAPP: text=359,388 data=4,768 bss=133,623 → 138,384 B RAM (data+bss)
nRF54L15 CPUAPP: text=485,628 data=5,152 bss=148,665 → 153,812 B RAM (data+bss)
nRF54L15 FLPR:   text=28,444  data=532   bss=12,868  →  41,844 B RAM (data+bss)
```

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
| audio_offload | 34 | **34/34 PASS** |
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
| **Total** | **247** | **247/247 PASS** |

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

### Pairing

Default scan path works (ServicesResolved, SetConfiguration). `--peer-addr` bypass path blocked by pre-existing BlueZ SMP numeric comparison failure (peer reason 0x0C) — not caused by Stage2 fixes.

**SC_PAIR_ONLY clarification** (2026-07-28): Earlier iterations documented `CONFIG_BT_SMP_SC_PAIR_ONLY=n` as a workaround for the nRF5340DK hci_uart central's SW Split LL failing SC pairing. **This was REJECTED/INVALID.** The final and proven setup is `CONFIG_BT_SMP_SC_PAIR_ONLY=y` (Zephyr default) with normal BlueZ-owned Pair-before-ACL. The `=n` workaround was attempted during pre-recovery-storm iterations when the hci_uart central exhibited intermittent SC pairing failures — those failures were unrelated to the Stage 2 implementation and the SC_PAIR_ONLY=n override was removed. SC Just Works pairing completes reliably with IO cap 3 (NoInputNoOutput) on both sides. No board-level override exists in the final tree.

### Recovery stability hardware check (2026-07-27, post-storm fix)

Receiver: nRF54L15 firmware with probation/recovery-stability patch.
Multiple consecutive Mode A runs (--duration 60), zero-fault baseline:

```
State       : STOPPED / epoch=0 gen=5
Counters    : submit=6027 success=6027 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
Probation   : active=0 success=0 cleared=0
RTT         : min=736 cyc (736 us) max=883 cyc (883 us) avg=743 cyc (743 us) n=6027
```

Three consecutive 60s runs: 6027+5110+5083 submits = 16,220 total blocks. Zero faults. Zero recovery attempts. Central 100.0 fps throughout all runs. RAM: 41,856 B (63.87%). New recovery counters operational and reporting correctly.

### FLPR-side stall test with probation policy (2026-07-27, bbb1051 final validation)

**Purpose**: Verify recovery bounded to ≤5 cycles, no storm, probation exhausts cleanly.

**Setup**: nRF54L15 receiver, hci0 central (C0:AA:BB:CC:DD:EE). Mode A (2 mono ASEs), 180s stream.
SC pairing: `CONFIG_BT_SMP_SC_PAIR_ONLY=y` (default, Just Works). **Historical note**: the SC_PAIR_ONLY=n workaround was attempted at this time but proved unnecessary — SC pairing succeeded without it in final validation. The workaround was removed.

**Test**: `flpr ring stall_flpr 1` injected at T+20s via Zephyr shell. `stall_flpr 0` at T+21s. Stream continued for full 180s.

**Pre-stall baseline** (ACTIVE, ~17s into stream):
```
State       : ACTIVE / epoch=1336965753 gen=4
Counters    : submit=2037 success=2037 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
```

**Recovery sequence** (serial console timestamps, UTC+2):
```
[00:22:37.451] offload recovery OK: epoch=1357451738 gen=5 tries=1 backoff=100 ms
[00:22:37.664] offload recovery OK: epoch=1357664434 gen=6 tries=2 backoff=200 ms
[00:22:38.084] offload recovery OK: epoch=1358084413 gen=7 tries=3 backoff=400 ms
[00:22:38.904] offload recovery OK: epoch=1358904366 gen=8 tries=4 backoff=800 ms
[00:22:40.514] offload recovery OK: epoch=1360514300 gen=9 tries=5 backoff=1600 ms
[00:22:43.724] offload recovery: max tries (5) exhausted, staying in FALLBACK
```

Escalation verified: tries 1→5, backoff 100→200→400→800→1600 ms. Exactly 5 recovery cycles, 6th fault → FALLBACK. No 21-cycle storm. Relapses=5 (one per recovery cycle during active probation).

**Final status** (after stream completion):
```
State       : STOPPED / epoch=0 gen=11
Counters    : submit=18024 success=2037 fallback=15987 busy=0
Faults      : timeout=4 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=5 fail=1 relapses=5 exhaustion=1
Probation   : active=0 success=0 cleared=0
RTT         : min=733 cyc (733 us) max=883 cyc (883 us) avg=740 cyc (740 us) n=2037
Last err    : -2 at seq 2356
```

**Central**: 18000 frames in 180.00s (100.0 fps) — zero frame loss at central TX side.

**Audio health**:
```
I2S underruns  : 0
Decode errors  : 0
Push failures  : 0
ASRC cap fail  : 0
```

**Gate results**:

| Gate | Expected | Actual | Pass |
|------|----------|--------|------|
| Central fps | 100.0 | 100.0 | ✅ |
| Audio faults | 0 | 0 (only offload timeouts) | ✅ |
| Recovery bounded | ≤5 | 5 | ✅ |
| No 21-reset storm | 0 | 0 | ✅ |
| Escalation (backoff) | doubles | 100→200→400→800→1600 | ✅ |
| Max exhaustion → FALLBACK | 1 | 1 | ✅ |
| Relapse count | 5 | 5 | ✅ |
| Fallback count increases | yes | 15987 | ✅ |
| RAM (FLPR) | <64 KB | 41,844 B (36.2%) | ✅ |
| RAM (CPUAPP) | <160 KB | 153,812 B (93.9%) | ✅ |

**Note**: Probation did not reach 100-success threshold (stall was persistent — clear arrived after most recovery cycles had already faulted). The policy correctly exhausted at max tries. In a real scenario where FLPR stall is brief (~1s) and clears before exhaustion, the probation would accumulate success blocks and eventually clear after 100 consecutive successes.

---

## v3 Protocol hardware acceptance (2026-07-28, commit ecafe3c) — CLOSED

### Build/flash

- nRF54L15 cpuapp: FLASH 491 956 B / 1428 KB (33.6%), RAM 153 836 B / 160 KB (93.9%)
- FLPR: RAM 42 624 B / 64 KB (65.0%)
- Both targets build clean, no new warnings
- Protocol version: `FLPR_PROTOCOL_VERSION = 3U` (`src/flpr_protocol.h:28`)
- Boot handshake: `FLPR READY (epoch=…) → FLPR READY_ACK sent` without `err_version`

### Central test

- Normal BlueZ scan path, 120 s duration, Mode A (2 mono ASEs, stereo)
- SC Just Works pairing, IO cap 3 (NoInputNoOutput), `CONFIG_BT_SMP_SC_PAIR_ONLY=y` (default)
- Adapter settings: `powered bondable le secure-conn cis-central`
- **Central result**: `12000 frames in 120.00 s (100.0 fps)` from `/tmp/bap_central.log`
- Offload blocks submitted: 11 776 (includes connect/disconnect frames not tracked by central)

### Timed gate (60 ms consumer stall)

| Gate | Required | Actual | Status |
|------|----------|--------|--------|
| timed60 packed ACK | `bits=0x01 duration=60` | `FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)` | PASS |
| timeout/fallback ≥ 1 | ≥ 1 | `fallback=12` `fault_timeout=1` | PASS |
| recovery attempt ≥ 1 | ≥ 1 | `recovery_attempts=1`, `offload recovery OK: tries=1 backoff=100 ms` | PASS |
| no post-reset integrity faults | stale=seq=frame=crc=payload=0 | `stale=0 seq=0 frame=0 crc=0 payload=0` | PASS |
| ACTIVE after recovery | ACTIVE | State `ACTIVE` after epoch bump `gen=13→14` | PASS |
| exhaustion zero | 0 | `exhaustion=0` | PASS |
| probation cleared ≥ 1 | ≥ 1 | `probation_cleared=1` (after 100 consecutive successes) | PASS |
| success baseline + 100 | ≥ 616 | `success=11695` (baseline=516, delta=11 179) | PASS |
| central 100 fps | ≈ 100 | 100.0 fps (`12000 frames in 120.00 s`) | PASS |
| audio faults zero | 0 | `Decode errors=0, I2S underruns=0, Stream resets=0` | PASS |
| stale_notify captured | — | `stale=0` | OBSERVED |
| sem_drained captured | — | `drained=1` | OBSERVED |

### Raw evidence

```text
flpr ring stall_flpr_ms 1 60
FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)

flpr offload
--- Audio offload ---
  State       : ACTIVE / epoch=335691771 gen=14
  Counters    : submit=11707 success=11695 fallback=12 busy=0
  Faults      : timeout=1 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0
  Probation   : active=0 success=100 cleared=1

flpr ring status
--- FLPR PCM rings ---
  Diag (CPUAPP): notify=11260 err=0 sem_give=11260 sem_take=0 stale=0 drained=1
  Stall (last): mask=0x01 (cons_in=1 prod_out=0) duration=60 ms

audio status
--- Audio status ---
  Decode errors  : 0
  I2S underruns  : 0
  Stream resets  : 0
```

### Gate script — v3 closeout fix (2026-07-28)

**Bug**: `flpr_stall_gate.py` step 6 zero-fault check incorrectly rejected `fault_timeout=1` and `fault_full=1`, which are expected transport fault evidence of stall (acceptance criteria: `timeout/fallback ≥ 1`). Also `fault_seq` was parsed but not included in integrity-fault rejection.

**Fix**: Removed `fault_timeout` and `fault_full` from the zero-fault rejection check. Added `fault_seq` to integrity-fault rejection alongside `stale`, `frame`, `crc`, `payload`. Unit tests added proving timeout/full pass the gate and each integrity fault (stale/seq/frame/crc/payload) fails.

**Gate script unit tests**: 17/17 PASS (was 12, +5 new tests).

---

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
| `src/audio_offload.c` | Stage2 repair: `k_work_queue_start` in init (no K_THREAD_DEFINE), `k_work_schedule_for_queue` for all prep/recovery, cancel outside spinlock, generation guard, `lifecycle_check_before_fault` helper on all 12 fault paths after blocking, backoff/tries under lock, exact accounting (single fallback increment), remove unused `schedule` variable. **Recovery storm fix**: probation window after recovery success, cumulative tries across cycles, relapse escalation (backoff doubles), max-tries → FALLBACK, probation clears after 100 consecutive successes, `stream_start`/`stream_stop` reset policy. |
| `src/audio_offload.h` | Added recovery stability counters: `recovery_attempts`, `recovery_relapses`, `probation_success`, `probation_active`, `max_exhaustion_count`, `probation_cleared`. Updated fault-state-machine doc comment. |
| `src/audio_shell.c` | Fix: rename `stall - flpr` → `stall_flpr` (Zephyr shell prefix collision with `stall`). Extended offload status to show `attempts`, `fail`, `relapses`, `exhaustion`, `probation active/success/cleared`. |
| `tests/unit/audio_offload/src/audio_offload_test_helpers.h` | Unchanged |
| `tests/unit/audio_offload/src/test_audio_offload.c` | Added 5 recovery-stability tests: relapse exhaustion, probation cleared after 100 successes, fault after stable, stop/reconnect resets policy, bounded 5-attempt cap. 34/34 PASS. |
| `docs/development/phase6-stage2-results.md` | This file — comprehensive Stage 2 history with recovery storm fix documentation, FLPR stall validation (probation policy, 8 gates PASS), corrected RAM table, SC_PAIR_ONLY=y confirmation, v3 protocol hardware acceptance, gate script v3 closeout fix. |
| `scripts/hci_raw_connect.py` | Added `--device` parameter for HCI device index selection (default 0). Required when kernel assigns hci1 instead of hci0. |
| `scripts/bap_central.py` | Pass `--device` to raw-HCI helper based on `--adapter` index. |
| `scripts/flpr_stall_gate.py` | v3: single `flpr ring stall_flpr_ms 1 60` command. v3 closeout fix: timeout/full accepted as expected stall evidence; seq added to integrity-fault rejection (was missing). Unit tests: 17/17 PASS. |

## Recovery stability policy (Phase 6 Stage 2 fix for f3c5dd0)

**Problem**: `recovery_work_fn` success reset `tries=0` + `backoff=100ms` → every recovery cycle started fresh. Persistent FLPR stall produced 21 recovery cycles (unbounded storm).

**Fix**: Probation window after recovery success.

| State | Behavior |
|-------|----------|
| Recovery success → ACTIVE | `tries` bumped (cumulative). `probation_active=true`. Backoff preserved (NOT reset). |
| Fault during probation | `recovery_relapses++`. Backoff doubles (100→200→400→800→1600ms). |
| 100 consecutive success blocks during probation | Probation cleared. `tries=0`, `backoff=100ms` (fresh start). |
| `tries > MAX_TRIES (5)` | `max_exhaustion_count++`. Clean FALLBACK. No recovery scheduled. |
| `stream_start()` | Resets probation + tries/backoff to base. |
| `stream_stop()` | Resets probation + tries/backoff to base. |

**Result**: Recovery bounded to exactly 5 cycles (tries 1→5). 6th fault → FALLBACK. No storm.

**Unit test evidence**:
- `test_probation_relapse_exhaustion`: 4 relapses → ACTIVE, 5th → FALLBACK, `max_exhaustion_count=1`
- `test_probation_cleared_100_success`: 100 successes → `probation_cleared+1`, next fault NOT a relapse
- `test_fault_after_stable`: after probation clear, fault starts fresh (relapse counter unchanged)
- `test_stop_reconnect_resets_policy`: stop→start clears probation, lifetime counters preserved
- `test_recovery_bounded_5_attempts`: 5 recovery cycles succeed, 6th exhausts → FALLBACK

## Non-scope (unchanged)

- FLPR ASRC, HPF, ICBmsg
- BabbleSim
- Direct RADIO access
- nRF5340 hardware streaming (bypass unchanged)
- FLPR code changes

## Known gaps

- `--peer-addr` pairing path blocked by pre-existing BlueZ SMP issue (peer reason 0x0C) — not Stage2
- Stack high-water not runtime-measured (thread analyzer not enabled in production build)
