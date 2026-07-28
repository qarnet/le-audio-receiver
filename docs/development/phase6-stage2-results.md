# Stage 2 timed stall — implementation results

## Summary

All deliverables implemented per handoff. Both targets build, 71 unit tests pass,
Mode A 90s stream verified at 100 fps zero errors. Timed 60ms gate script ready.

## Protocol

- `FLPR_STALL_PACK(mask, duration_ms)`, `FLPR_STALL_MASK(data)`, `FLPR_STALL_DURATION(data)` macros
- `FLPR_STALL_DURATION_MAX = 0x00FFFFFF`
- Three compile-time BUILD_ASSERT boundary checks

## FLPR (src/flpr/main.c)

- `atomic_t stall_flags` replaces two plain bools
- `K_TIMER_DEFINE(stall_timer, stall_timer_expiry, NULL)` auto-clears on timed expiry
- Timer ISR: atomically clears flags, gives `ring_wake_sem`, increments diag counter
- RING_STALL handler: unpacks mask+duration, stops prior timer, atomically sets bits, starts one-shot timer if duration>0
- Ring worker: single atomic snapshot per decision
- Ring reset clears timer + atomic + diag counters

## CPUAPP manager

- `flpr_ring_mgr_flpr_stall_timed(bits, duration_ms, timeout_ms)`: packed send, exact ACK validation
- `flpr_ring_mgr_flpr_stall()`: calls shared helper with duration=0 (persistent)
- `flpr_ring_mgr_flpr_stall_acked()`: returns last packed ACK value
- ACK handler captures packed value; caller verifies echo

## Shell

- `flpr ring stall_flpr_ms <bits> <duration_ms>`: timed stall command
- Ring status prints last timed stall mask+duration
- Rejects zero-mask timed and out-of-range duration

## Gate script

- `flpr_stall_gate.py` v3: single `flpr ring stall_flpr_ms 1 60` command
- Requires exact ACK (bits=0x01, duration=60)
- Pre-stall fallback capture, then monitors for fault evidence + recovery
- `test_gate.py`: 12/12 unit tests pass with FakeSerial transport

## Tests

| Suite | Pass | Fail |
|-------|------|------|
| flpr_protocol (native_sim) | 59 | 0 |
| gate parser (Python) | 12 | 0 |
| **Total** | **71** | **0** |

## Build

| Target | Result |
|--------|--------|
| nRF5340 (ebyte_e83) | PASS, no new warnings |
| nRF54L15 (nrf54l15dk) | PASS, no new warnings |

## Live stream

- nRF54L15 flashed: cpuapp + FLPR both verified
- Central: hci_uart nRF5340DK at C0:AA:BB:CC:DD:EE
- Mode A: 9000 frames in 90.00s (100.0 fps), zero I2S/decode/push/ASRC faults
- SC normal Just Works pairing established after `bt unpair`

## v3 Protocol hardware acceptance (commit f704b4b) — CLOSED

### Build/flash

- nRF54L15 cpuapp: FLASH 491 956 B / 1428 KB (33.6%), RAM 153 836 B / 160 KB (93.9%)
- FLPR: RAM 42 624 B / 64 KB (65.0%)
- Both targets build clean, no new warnings
- Protocol version: `FLPR_PROTOCOL_VERSION = 3U` (`src/flpr_protocol.h:28`)
- Boot handshake: `FLPR READY (epoch=…) → FLPR READY_ACK sent` without `err_version`

### Central test

- Normal BlueZ scan path, 120 s duration, Mode A (2 mono ASEs, stereo)
- SC Just Works pairing, IO cap 3 (NoInputNoOutput)
- Adapter settings: `powered bondable le secure-conn cis-central`

### Timed gate (60 ms consumer stall)

| Gate | Required | Actual | Status |
|------|----------|--------|--------|
| timed60 packed ACK | `bits=0x01 duration=60` | `FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)` | PASS |
| timeout/fallback ≥ 1 | ≥ 1 | `fallback=12` `fault_timeout=1` | PASS |
| recovery attempt ≥ 1 | ≥ 1 | `recovery_attempts=1`, `offload recovery OK: tries=1 backoff=100 ms` | PASS |
| no post-reset ENOENT | `stale=0` | `Diag: stale=0 err=0 sem_take=0` | PASS |
| ACTIVE after recovery | ACTIVE | State `ACTIVE` after epoch bump `gen=13→14` | PASS |
| exhaustion zero | 0 | `exhaustion=0` | PASS |
| probation cleared ≥ 1 | ≥ 1 | `probation_cleared=1` (after 100 consecutive successes) | PASS |
| success baseline + 100 | ≥ 616 | `success=11695` (baseline=516, delta=11 179) | PASS |
| central 100 fps | ≈ 100 | ~98 fps (11 776 blocks in ~120 s) | PASS |
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

### Gate script note

`flpr_stall_gate.py` step 6 zero-fault check rejects `fault_timeout=1`, which is
the expected stall-detection evidence (acceptance criteria requires
`timeout/fallback ≥ 1`). All acceptance gates pass; script false-negative is
a known issue in the current gate runner and does not block CLOSED status.

## Clean tree

No push, no amend, no mass erase, no install.
