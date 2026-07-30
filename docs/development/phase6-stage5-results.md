# Phase 6 Stage 5 — Final hardware regression results

**Status: Phase 6 CLOSED — PASS** (2026-07-29)

## Date

2026-07-29 — commit `e36a629` (`feat(offload): Phase 6 Stage 5 — remove dead identity API, migrate tests to ASRC`)

## Build

```
fw-build-54l15  (production: CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY not set)
fw-flash-54l15
```

Build target: nRF54L15 (xiao_nrf54l15). nRF5340 (ebyte_e83_nrf5340) build passes clean
(no FLPR, no code changes affecting nRF5340).

## Production .config verification

```
$ grep VERIFY build/nrf54l15/le-audio-receiver/zephyr/.config
# CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY is not set
```

Production assertion confirmed: ASRC shadow verify disabled. Actuator: NONE.

## Test command

```
python3 scripts/flpr_hang_gate.py --duration 180
```

Normal BlueZ discovery (no `--peer-addr` bypass). Central: nRF5340DK hci_uart
dongle (`C0:AA:BB:CC:DD:EE`). Receiver: `DB:A6:0C:05:A2:AA` (random).

## Gate output (Mode A mono, 180 s, production)

```
======================================================================
[gate] FLPR FAULT_HANG Gate — 180s Mode A mono
[gate] Port: /dev/ttyACM0 @ 115200
======================================================================
[10:10:11] Probing device console...
[10:10:11] Console responsive
[10:10:11] Device ready. Starting bap_central...
[10:10:31] ACTIVE with success=1014, epoch=1752432336. Injecting flpr hang...
[10:10:31] FAULT_HANG_ACK received (150 ms)
[10:10:31] Waiting for recovery chain...
[10:10:31] First fallback block detected
[10:10:32] Back to ACTIVE after 851 ms
[10:10:33] Probation cleared. Recovery complete.
[10:10:33] Capturing active-stream status...
  State=ACTIVE submit=1232 success=1186 fallback=45
[10:13:24] bap_central exited with code 0
  [bap] Stream mode: stereo_a, SDU size: 120 bytes
  [bap] Streaming 1000 Hz sine for 180 s...
  [bap] Done: 18000 frames in 180.00 s (100.0 fps)
  [bap] Releasing resources...

======================================================================
[gate] RESULT: PASSED
[gate] Mode: Mode A (mono/2-ASE)  Duration: 180s
[gate] Injection → ACK: 150 ms
[gate] Injection → ACTIVE: 851 ms

[gate] Final offload status:
  State       : STOPPED / epoch=0
  Counters    : submit=18030 success=17985 fallback=45
  Faults      : timeout=1 crc=0 seq=0 frame=0
  Recovery    : attempts=1 fail=0 exhaustion=0
  Probation   : cleared=1 active=0
  Runtime     : restarts=1 fails=0

[gate] ASRC stats:
  Counters    : submit=1232 success=1186 fallback=45
  Faults      : verify=0 state=0 crc=0 seq=0 frame=0

[gate] Checks:
  [PASS] ack_received
  [PASS] asrc_fallback_triggered
  [PASS] asrc_state_zero
  [PASS] asrc_verify_zero
  [PASS] crc_zero
  [PASS] epoch_changed
  [PASS] exhaustion_zero
  [PASS] frame_count_plausible
  [PASS] frame_zero
  [PASS] probation_cleared
  [PASS] recovery_attempts_eq_1
  [PASS] relapses_zero
  [PASS] runtime_fails_zero
  [PASS] runtime_restarts_eq_1
  [PASS] seq_zero
  [PASS] state_active
```

## Criteria verification

| Criterion | Required | Actual | Status |
|-----------|----------|--------|--------|
| Frame count | 18000 | 18000 | ✅ exact |
| Duration | 180.00 s | 180.00 s | ✅ exact |
| Frame rate | 100.0 fps | 100.0 fps | ✅ exact |
| FAULT_HANG_ACK | received | 150 ms | ✅ |
| Runtime restarts | exactly 1 | 1 | ✅ |
| CPU ASRC fallback | ~45 | 45 | ✅ exact |
| State ACTIVE | yes | ACTIVE captured | ✅ |
| Probation cleared | ≥1 | 1 | ✅ |
| Resumed success to end | submit ≈18000 | submit=18030, success=17985 | ✅ |
| Verify faults | 0 | 0 | ✅ |
| CRC faults | 0 | 0 | ✅ |
| Sequence faults | 0 | 0 | ✅ |
| Frame faults | 0 | 0 | ✅ |
| ASRC state faults | 0 | 0 | ✅ |
| Exhaustion | 0 | 0 | ✅ |
| Relapses (duplicate restart) | 0 | 0 | ✅ |
| Runtime restart fails | 0 | 0 | ✅ |

All 16 automated gate checks pass. All manual criteria pass.

## Recovery metrics

- Injection → ACK: 150 ms
- Injection → ACTIVE: 851 ms
- Fallback blocks during recovery gap: 45
- Recovery attempts: 1 (no duplicate)
- Probation: 100 consecutive successes after recovery

## Scope

No code edited. No config changed. Production `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY` unset
confirmed via resolved `.config`. Central uses normal BlueZ discovery. No
`--peer-addr` bypass. No mass erase, no security changes, no push, no release.

## Phase 6 status

All Stages 0–5 complete. Recovery chain verified end-to-end on hardware:
FAULT_HANG → ACK → RECOVERING → runtime restart → ACTIVE → probation cleared →
continued success to 180 s stream end. Zero integrity/audio/exhaustion/duplicate faults.

**Phase 6 CLOSED — PASS.**
