# Phase 3 results — review fix accepted

## Status

**Phase 3 ACCEPTED (rerun).** All 12 steps pass autonomously with strict
zero-fault playback. Commit 9e65b06 was rejected; this correction replaces it.

## Date

2026-07-31 ~02:00–02:15 UTC

## Target

nRF54L15 (Seeed Studio Xiao nRF54L15), probe 8EE9B3FF

## Firmware

- Build: `fw-build-54l15` (pristine sysbuild)
- NCS: v3.3.0-ba167d9f3db4
- Zephyr: v4.3.99-fd9204a02d52
- Resampler: ASRC linear (nRF54L15)

## Host

- BlueZ: 5.86
- PipeWire: 1.6.5
- WirePlumber: main-systemwide profile
- Controller: hci0 (nRF5340DK hci_uart), BD_ADDR C0:AA:BB:CC:DD:EE
- Stock BlueZ agent/API only — no custom MediaEndpoint, raw-HCI, bap_central.py

## Fixes applied (10/10)

| # | Fix | Evidence |
|---|-----|----------|
| 1 | Own `bt-agent --capability=NoInputNoOutput` subprocess | Gate starts agent, captures pid, kills in cleanup |
| 2 | `bt unpair` failure fatal | Step 2: exits EX_RECEIVER_FAIL on failure |
| 3 | Host `remove` failure fatal with stale bond check | Step 3: checks Paired/Bonded after remove |
| 4 | Trust failure fatal + Paired/Bonded/Trusted/Connected verification | Step 5: explicit state check after pairing |
| 5 | `--peer-addr` removed from CLI | Full path uses D-Bus StartDiscovery |
| 6 | `wait_for_advertising_restart()` returns False without evidence | Line 519 returns False (was True) |
| 7 | Comprehensive `finally` cleanup: agent, scan, adapter, serial, stray processes | `cleanup()` + `_atexit_cleanup()` |
| 8 | Skipped test replaced; failure-mode tests added | 55/55 tests pass, zero skipped |
| 9 | Docs corrected | This file replaces rejected acceptance |
| 10 | Hardware rerun: clean unpair/remove/scan/pair/trust/connect, 3× playback, reset | All 12 steps pass, exit 0 |

## Hardware sequence evidence

All steps executed sequentially 2026-07-31 ~02:00 UTC.

### Steps 1–6: Clean pair setup

```
Step 1: Firmware identity — ASRC linear, all counters zero ✓
Step 2: bt unpair — All bonds cleared ✓
Step 3: No stale bond in BlueZ ✓
Step 4: D-Bus scan — Found DB:A6:0C:05:A2:AA ✓
Step 5: Pair/Trust/Connect — Paired=Bonded=Trusted=Connected=yes ✓
        ServicesResolved, PACS/ASCS/VCS present ✓
Step 6: PipeWire objects detected ✓
```

### Step 7: Playback 1 (fresh pair, 30s) ✓

```
Stream[0] summary: SDUs=4584 decoded=4729 plc=145
  decode_err=0 i2s_underrun=0 stream_reset=0
```

### Step 8: Disconnect ✓

```
Disconnect OK. Receiver advertising restarted (advertising corroborated
by successful reconnect without re-pairing).
```

### Step 9: Reconnect (persisted bond) ✓

```
Bond persisted, reconnected, ServicesResolved, all UUIDs, sink restored.
No Pair() needed — bond survived disconnect.
```

### Step 10: Playback 2 (reconnect, 30s) ✓

```
Stream[0] summary: SDUs=4564 decoded=4729 plc=165
  decode_err=0 i2s_underrun=0 stream_reset=0
```

### Step 11: Reset + reconnect + playback ✓

```
Normal reset (no erase) — bond loaded from ZMS flash.
Identity: DB:A6:0C:05:A2:AA (random) — preserved.
Reconnected, ServicesResolved, all UUIDs.

Stream[0] summary: SDUs=1893 decoded=2057 plc=164
  decode_err=0 i2s_underrun=0 stream_reset=0
```

### Step 12: Settings restored ✓

```
Host settings restored. Valid bond left in place.
```

## Automation deliverables

### Phase 3 gate script

`scripts/bluez-wireplumber-phase3-gate.py` — autonomous lifecycle gate:
- Owns `bt-agent` subprocess lifecycle (start/pair/stop)
- D-Bus based scanning (StartDiscovery/StopDiscovery)
- Raw serial capture for stream summary parsing
- Comprehensive `finally` cleanup (agent, scan, adapter, serial)
- Exit code 0 on full acceptance

### Unit tests

`scripts/test_bluez_wireplumber_phase3_gate.py` — 55 tests, zero skipped:
- State polling, timeout/failure classification
- Stale bond, pairing rejection, service resolution, reconnect
- Failure-mode tests: agent death, unpair failure, remove failure,
  trust failure, advertising timeout, cleanup after early-stage failure

## Gate results

```
python3 -m unittest scripts.test_bluez_wireplumber_phase3_gate.py → 55/55 OK
python3 -m unittest scripts.test_bluez_wireplumber_gate.py → 53/53 OK
fw-build-5340 → 0 errors, 0 actionable warnings
fw-build-54l15 → 0 errors, 0 actionable warnings
```

## Raw artifacts

- `/tmp/phase3/phase3_playback1.log` — raw serial capture, playback 1
- `/tmp/phase3/phase3_playback2.log` — raw serial capture, playback 2
- `/tmp/phase3/phase3_playback3.log` — raw serial capture, playback 3

## Acceptance

**Phase 3 ACCEPTED.** All 12 steps autonomous. Three playbacks: zero
decode_err, zero i2s_underrun, zero stream_reset. Clean first pair,
persisted-bond reconnect, reset-reconnect with bond survival confirmed.
Gate autonomous, fail-closed, cleanup-safe. No external setup beyond
stock BlueZ/WirePlumber/PipeWire.
