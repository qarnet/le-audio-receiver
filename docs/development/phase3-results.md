# Phase 3 results — strict host-fix correction ACCEPTED

## Status

**Phase 3 ACCEPTED.** All 12 steps pass autonomously with strict zero-fault
playback. Commit `9e65b06` was rejected; this correction replaces it.

## Date

2026-07-31 ~01:05–01:07 UTC

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
- WirePlumber: main-systemwide profile (owned by gate — seat detect, launch, SPA verify, restore)
- Controller: hci0 (nRF5340DK hci_uart), BD_ADDR C0:AA:BB:CC:DD:EE
- Stock BlueZ agent/API only — no custom MediaEndpoint, raw-HCI, bap_central.py

## Corrections from rejected commit (8/8)

| # | Fix | Evidence |
|---|------|----------|
| 1 | Own WirePlumber lifecycle | Seat detection, headless→main-systemwide, SPA plugin registration verification, service restore |
| 2 | Disconnect failure fatal | Poll until actually disconnected (10s timeout), fail on timeout |
| 3 | Advertising restart fatal | Raw serial read for "Restarting advertising...", 20s timeout, no fallback |
| 4 | No global process kills | Removed `pkill -f bt-agent` from cleanup, removed `pkill -f wireplumber` from WP lifecycle |
| 5 | Reuse Phase 2 strict parser | Continuous UART capture → log file → `parse_receiver_log()` |
| 6 | Continuous UART capture | Background thread from before pw-play through stream summary |
| 7 | Removed BlueZ auto-reconnect storm | Remove device + power-cycle controller before bt unpair |
| 8 | Serial CDC bridge survivable | Open serial eagerly, never close/reopen (SAMD11 CDC dies on close) |

## Hardware sequence evidence

All steps executed sequentially 2026-07-31 ~01:05 UTC.

### Steps 1–6: Clean pair setup

```
Step 1: Firmware identity — ASRC linear, all counters zero ✓
Step 2: Remove host device + power-cycle controller + bt unpair ✓
Step 3: No stale bond ✓
Step 4: D-Bus scan — Found DB:A6:0C:05:A2:AA ✓
Step 5: Pair/Trust/Connect — Paired=Bonded=Trusted=Connected=yes ✓
        ServicesResolved, PACS/ASCS/VCS present ✓
Step 6: PipeWire objects detected ✓
```

### Step 7: Playback 1 (fresh pair, 30s) ✓

```
Stream[0] summary: SDUs=4585 decoded=4729 plc=144
  decode_err=0 i2s_underrun=0 stream_reset=0
```

### Step 8: Disconnect ✓

```
Disconnect OK. Device disconnected. Receiver advertising restarted ✓
(verified via serial "Restarting advertising..." string)
```

### Step 9: Reconnect (persisted bond) ✓

```
Bond persisted in BlueZ. Reconnected without re-pairing.
ServicesResolved, all UUIDs, PipeWire sink restored.
```

### Step 10: Playback 2 (reconnect, 30s) ✓

```
Stream[0] summary: SDUs=4581 decoded=4729 plc=148
  decode_err=0 i2s_underrun=0 stream_reset=0
```

### Step 11: Reset + reconnect + playback ✓

```
Normal reset (no erase) — bond loaded from ZMS flash on receiver.
Reconnected with same BlueZ bond, ServicesResolved, all UUIDs.
Stream[0] summary: SDUs=4584 decoded=4729 plc=145
  decode_err=0 i2s_underrun=0 stream_reset=0
```

### Step 12: Settings restored ✓

```
Host settings restored. Own WirePlumber stopped, user service restored.
Valid bond left in place.
```

## Raw artifacts

| File | Bytes | Summary |
|------|-------|---------|
| `/tmp/phase3/phase3_playback1.log` | 3661 | SDUs=4585 decoded=4729 decode_err=0 i2s_underrun=0 stream_reset=0 |
| `/tmp/phase3/phase3_playback2.log` | 3309 | SDUs=4581 decoded=4729 decode_err=0 i2s_underrun=0 stream_reset=0 |
| `/tmp/phase3/phase3_playback3.log` | 3309 | SDUs=4584 decoded=4729 decode_err=0 i2s_underrun=0 stream_reset=0 |

## Gate test suite

```
Phase 2: 53/53 OK
Phase 3: 82/82 OK
Total: 135/135 OK
```

## Builds

```
fw-build-5340: clean (no firmware changes)
fw-build-54l15: clean (no firmware changes)
git diff --check: clean
```

## Acceptance

**Phase 3 STRICT ACCEPTED.** All 12 steps autonomous. Three playbacks:
zero decode_err, zero i2s_underrun, zero stream_reset. Clean first pair,
persisted-bond reconnect, reset-reconnect with bond survival confirmed.
Gate autonomous, fail-closed, owns its lifecycle. No external setup
beyond stock BlueZ/WirePlumber/PipeWire. No global process kills.
WirePlumber lifecycle owned from seat detect through service restore.
Serial CDC bridge kept alive throughout (eager open, never close).
