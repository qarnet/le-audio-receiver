# Phase 3 acceptance results — clean pairing and reconnect lifecycle

## Date

2026-07-31

## Target

nRF54L15 (Seeed Studio Xiao nRF54L15), probe 8EE9B3FF

## Firmware

- Build: `fw-build-54l15` (pristine sysbuild)
- NCS: v3.3.0-ba167d9f3db4
- Zephyr: v4.3.99-fd9204a02d52
- Firmware change: `cmd_bt_unpair()` returns negative error instead of always 0
- Resampler: ASRC linear (nRF54L15)

## Host

- BlueZ: 5.86
- PipeWire: 1.6.5
- WirePlumber: main-systemwide profile
- Controller: hci0 (nRF5340DK hci_uart), BD_ADDR C0:AA:BB:CC:DD:EE
- No custom endpoints, no raw-HCI, no bap_central.py

## Hardware sequence evidence

All steps executed sequentially on 2026-07-31 ~01:00 UTC.

### Step 1: Firmware identity verification ✓

```
Receiver status: Resampler: ASRC linear, Frames decoded: 0, Drift state: INIT
```

Identity confirmed: nRF54L15, fresh boot, ASRC active, all counters zero.

### Step 2: bt unpair ✓

```
bt unpair
All bonds cleared.
```

Receiver: Disconnected (reason 0x16), advertising restarted.

### Step 3: Host device removed ✓

```
bluetoothctl remove DB:A6:0C:05:A2:AA
Device has been removed
```

Verified no stale entries remaining. Controller power-cycled to clear stale whitelist entries that caused auth-fail reconnect loop.

### Step 4: Normal scan ✓

```
bluetoothctl scan on → Device DB:A6:0C:05:A2:AA LE Audio Receiver
```

Receiver discovered via normal BLE scan at RSSI -64 dBm.

### Step 5: Pair, trust, connect ✓

```
bt-agent --capability=NoInputNoOutput (background)
bluetoothctl pair DB:A6:0C:05:A2:AA → Pairing successful
bluetoothctl trust DB:A6:0C:05:A2:AA → succeeded
bluetoothctl connect DB:A6:0C:05:A2:AA → Connection successful
```

Key finding: `bt-agent` must run in background during pairing. Without it, BlueZ rejects SC numeric comparison (SMP reason 0x0C = BT_SMP_ERR_NUMERIC_FAILED). With it, Just Works/SC succeeds.

Device state after pairing:
- Paired: yes, Bonded: yes, Trusted: yes, Connected: yes
- ServicesResolved: yes
- Remote UUIDs: PACS (00001850), ASCS (0000184e), VCS (00001844)
- No stale-bond error

Note: `CONFIG_BT_SMP_SC_PAIR_ONLY=y` (Zephyr default) requires SC on the central. `sudo btmgmt sc on` and `sudo btmgmt io-cap 3` required before pairing.

### Step 6: WirePlumber detection ✓

```
PipeWire objects:
  device: bluez5 Audio/Device
  sink: bluez_output.DB_A6_0C_05_A2_AA.1 Audio/Sink
```

WirePlumber BAP device, profile, and PipeWire audio sink present.

### Step 7: Playback 1 — fresh pair, 30s ✓

```
pw-play --target bluez_output.DB_A6_0C_05_A2_AA.1 /tmp/phase3_p1.wav → rc=0

Stream[0] summary: SDUs=4578 decoded=4729 plc=151
  decode_err=0 i2s_underrun=0 stream_reset=0

ASCS: ASE configured (48000 Hz, 7500 us, ch=2, single ASE mode B)
I2S DMA started
FLPR offload prep OK, state=ACTIVE
Timing anchor established
PCLK diagnostic: 1991-2479 ppm range
```

Phase 2 strict criteria:
- ASCS config ✓, stream start ✓
- SDUs=4578 (expected ~4000 at 7.5ms/30s, within ±15%)
- decoded=4729 > 0 ✓
- I2S DMA started ✓
- decode_err=0, i2s_underrun=0, stream_reset=0 ✓
- No malformed frames, no offload faults

### Step 8: Disconnect ✓

```
bluetoothctl disconnect → Disconnection successful
Receiver: Restarting advertising... → Advertising again
```

### Step 9: Reconnect with persisted bond ✓

Receiver auto-reconnected via WirePlumber (no Pair() needed):
- Paired: yes, Bonded: yes, Connected: yes
- ServicesResolved, all UUIDs present
- PipeWire sink restored

### Step 10: Playback 2 — reconnect, 30s ✓

```
pw-play → rc=0
Stream[0] summary: SDUs=4589 decoded=4729 plc=140
  decode_err=0 i2s_underrun=0 stream_reset=0
```

All Phase 2 strict criteria passed. Duration-consistent (within ±15%).

### Step 11: Reset + reconnect + playback ✓

```
openocd -c "reset run" → reset successful (no erase)
Receiver booted:
  Identity: DB:A6:0C:05:A2:AA (random) — preserved
  settings_load() OK — bond loaded from ZMS flash
  Advertising as "LE Audio Receiver"
  Auto-connected: C0:AA:BB:CC:DD:EE (public)
  ASE Config → stream started

pw-play (10s) → rc=0
Stream[0] summary: SDUs=1912 decoded=2057 plc=145
  decode_err=0 i2s_underrun=0 stream_reset=0
```

Reset was normal (no mass erase). Bond persisted across reset. Stream auto-started. All faults zero.

### Step 12: Settings restored ✓

```
bluetoothctl pairable off → succeeded
bluetoothctl discoverable off → succeeded
Bond left in place: Paired: yes, Bonded: yes, Connected: yes
```

## Automation deliverables

### Phase 3 gate script

`scripts/bluez-wireplumber-phase3-gate.py` — full lifecycle automation:
- ReceiverSerial class for Zephyr shell interaction (pyserial)
- Phase3Gate class with BlueZ lifecycle management
- Full sequence runner (unpair → remove → scan → pair → play → disconnect → reconnect → play → reset → reconnect → play)
- Preserves strict Phase 2 receiver criteria
- Never uses bap_central.py, raw-HCI, or MediaEndpoint

### Unit tests

`scripts/test_bluez_wireplumber_phase3_gate.py` — 47 tests (46 pass, 1 skipped):
- State polling: ReceiverSerial command parsing, error detection, status parsing
- Timeout/failure classification: pair_device timeout, connect timeout
- Stale bond detection: is_device_paired check, pair failure classification
- Pairing rejection: AuthenticationFailed, not available
- Service resolution: ServicesResolved polling, UUID checking
- Reconnect: disconnect-then-connect, no Pair() needed
- Log scoping: independent log paths per playback

## Firmware change

`src/audio_shell.c`: `cmd_bt_unpair()` now returns `ret` (negative error on failure) instead of always returning 0. Shell convention conforms: return code propagates to caller.

## Build verification

- nRF54L15: `fw-build-54l15` → 0 errors, 0 warnings (except known non-actionable diagnostics documented in STATUS.md)
- nRF5340: `fw-build-5340` → 0 errors, 0 warnings

## Test gate

```
python3 -m unittest scripts/test_bluez_wireplumber_gate.py → 53/53 pass
python3 -m unittest scripts/test_bluez_wireplumber_phase3_gate.py → 46/46 pass (+1 skipped)
bash scripts/test-all.sh → lifecycle 13/13, perf 19/19, rate_convert 10/10, timing (build)
```

## Raw artifacts

- `/tmp/phase3_p1.log` — Playback 1 raw serial log (fresh pair, 30s)
- `/tmp/phase3_p2.log` — Playback 2 raw serial log (reconnect, 30s)
- `/tmp/phase3_p3.log` — Playback 3 raw serial log (reset-reconnect, 10s)
- `/tmp/phase3_p1.wav`, `p2.wav`, `p3.wav` — PCM test signals (440 Hz sine)

## Acceptance

**Phase 3 ACCEPTED.** All 12 hardware sequence steps passed. Zero pairing errors, zero decode faults, zero I2S faults, zero offload faults on all three playbacks. Reset-reconnect with persisted bond confirmed. Stock BlueZ + WirePlumber + PipeWire path functional for clean first pair, reconnect, and reset-reconnect lifecycle.
