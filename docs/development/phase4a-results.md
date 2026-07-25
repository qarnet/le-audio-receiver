# Phase 4a Results — nRF54L15 First End-to-End Audio Stream

Date: 2026-07-25 (revised 2026-07-25 after review: fix reentrant Acquire deadlock)
Controller primary: hci0 (Realtek RTL8761BU, A0:AD:9F:7B:C7:95)
Controller fallback: hci1 (nRF5340 DK USB HCI, BD all-zeros)
Receiver: nRF54L15 (Seeed Xiao), addr DB:A6:0C:05:A2:AA (random static)
Status: **OPEN — not accepted. See review findings below.**

## Acceptance criteria status

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Latest nRF54L15 flashes + boots cleanly, no warnings/errors | **PASS** | SPI NOR boot error fixed; boot log in serial capture shows clean init: `BLE ready` / `settings_load() OK` / `I2S ready` / `Advertising as "LE Audio Receiver"`. Zero warnings/errors. |
| 2 | BlueZ source establishes BAP transport and writes LC3 SDUs for requested duration | **NOT MET — retest needed** | BAP codec + QoS negotiation succeeds on hci0. BUT: the test driver called `MediaTransport1.Acquire()` from inside `SetConfiguration()`, creating a D-Bus reentrancy deadlock: BlueZ cannot create the CIS until `SetConfiguration` returns, but `SetConfiguration` blocks on `Acquire`. This is a script bug, not a controller limitation. The `Input/output error` on `Acquire()` is the expected symptom of this deadlock. Must retest hci0 with the corrected flow before drawing any conclusion about RTL8761BU ISO/CIS support. |
| 3 | Receiver logs ASE Configure/QoS/Enable/Start and `Stream started` | **NOT MET** | ASE Config (2 ASEs: FL=0x01, FR=0x02), QoS Config, Enable for ASE[0] all confirmed in serial log. LC3 decoder initialized: `48000 Hz 10000 us ch=1`. No `Stream started` — streaming never reached send phase because the script's Acquire deadlock prevented CIS establishment. |
| 4 | Frames decoded climb; decode errors = 0 | **NOT MET** | Frames decoded = 0 (no SDUs received). Decode errors = 0 (trivially). |
| 5 | Logic capture proves BCK/LRCK/DIN activity | **NOT MET** | No I2S activity observed (no stream to drive I2S). |
| 6 | No steady-state I2S underrun/slab-full/reset storm | **NOT MET** | No streaming occurred, so steady-state not reached. |
| 7 | Results document with exact commands + evidence | **IN PROGRESS** | This document; corrected-flow retest still needed. |

## Fixes delivered

### A. SPI NOR boot noise on Xiao overlay
**File:** `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`
Disabled `&mx25r64` and `&spi00` in Xiao overlay (no external flash on Xiao hardware). Boot log clean: no `spi_nor: Device id 00 00 00 does not match config` error.

### B. bap_central.py robustness to cached/paired/connected devices
**File:** `scripts/bap_central.py`
- Now enumerates existing BlueZ devices via `ObjectManager.GetManagedObjects()` before starting discovery.
- If "LE Audio Receiver" already exists in BlueZ cache, uses its path directly.
- If BlueZ reports the device as connected, disconnects first to force fresh GATT discovery.
- Added `--adapter` argument (default hci0) for hci1 fallback.
- Handles `AlreadyExists` pairing errors gracefully.

## Controllers attempted

### hci0 — Realtek RTL8761BU (A0:AD:9F:7B:C7:95)
- Connection: OK (LE Enhanced Connection Complete, encryption AES-CCM)
- GATT discovery: OK (PACS 0x1844, ASCS 0x1850 both visible)
- ASE Codec Config: OK (FL + FR, both respond Success)
- ASE QoS Config: OK (CIG 0, CIS 0+1, both respond Success)
- ASE Enable: OK (ASE[0] → Enabling state)
- **CIS Acquire: FAIL** — `org.bluez.Error.Failed: Input/output error`
  - CIG parameters set successfully (2 CIS, handles 23+24)
  - Tried both 2M PHY and 1M PHY — same result
  - **Review finding: the test driver called Acquire() from inside the
    SetConfiguration D-Bus callback. This creates a reentrancy deadlock:
    BlueZ cannot complete CIS creation until SetConfiguration() returns,
    but the callback blocks on Acquire(). The I/O error is the expected
    symptom of this script bug, not proof of a controller limitation.**
  - No `LE CIS Established` was observed — this matches the deadlock:
    BlueZ never gets to issue `LE Set CIG` because the BAP config flow is
    stalled waiting for the SetConfiguration D-Bus reply.
  - RTL8761BU ISO/CIS status: **unverified**. Must retest with corrected
    flow that calls Acquire() from the main loop, after SetConfiguration
    returns.

### hci1 — nRF5340 DK USB HCI (BD 00:00:00:00:00:00)
- Static address `C0:98:E5:00:00:01` set via `sudo btmgmt` (while powered off only)
- Discovery: OK — receiver found as `LE Audio Receiver` (RSSI -68 to -73)
- Connection: OK — receiver logs `Connected: 28:82:0E:EE:A4:97 (random)`
- **Pairing: FAIL** — `org.bluez.Error.AuthenticationRejected`
  - Receiver disconnects with reason 0x05 (BT_HCI_ERR_AUTH_FAIL) within 300 ms
  - Receiver's `pairing_accept` returns `BT_SECURITY_ERR_SUCCESS` — rejection
    occurs at controller level, not host level
  - The over-air address seen by the receiver (`28:82:0E:EE:A4:97`) is a
    **random** address, not the static address `C0:98:E5:00:00:01` that was
    configured via btmgmt. This means either the static-addr setting did not
    take effect at controller level, or BlueZ overrode it with privacy.
  - Root cause is **unresolved**: whether it's the all-zeros BD, nRF5340 DK
    HCI firmware LE Secure Connections support, or a btmgmt/BlueZ privacy
    interaction is not yet determined. Do not change hci1 address/privacy
    state until hci0 is definitively eliminated.

## Serial evidence

Boot log (clean, no errors):
```
*** Booting nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***
<inf> fs_zms: 2 Sectors of 4096 bytes
<inf> bt_sdc_hci_driver: SoftDevice Controller build revision: ...
<inf> bt_hci_core: HW Platform: Nordic Semiconductor (0x0002)
<inf> bt_hci_core: HW Variant: nRF54Lx (0x0005)
<inf> bt_hci_core: No ID address. App must call settings_load()
<inf> main: BLE ready
<inf> bt_hci_core: HCI transport: SDC
<inf> bt_hci_core: Identity: DB:A6:0C:05:A2:AA (random)
<inf> main: settings_load() OK
<inf> audio_volume: VCP ready (default vol=195)
<inf> audio_i2s: I2S ready (48 kHz, 16-bit, stereo, 12 blocks)
<inf> main: Advertising as "LE Audio Receiver"
```

ASE negotiation (hci0, timestamp 00:21:14):
```
<inf> bt_bap: ASE Config: conn 0x200040a0 ep 0x2000486c dir 1
<inf> bt_bap:   Frequency: 48000 Hz
<inf> bt_bap:   Frame Duration: 10000 us
<inf> bt_bap:   Octets per frame: 120
<inf> bt_bap:   chan alloc 0x00000001 count=1
<inf> bt_bap:   ASE[0] configured: num_sink_ase=1
<inf> bt_bap: ASE Config: conn 0x200040a0 ep 0x2000496c dir 1
<inf> bt_bap:   chan alloc 0x00000002 count=1
<inf> bt_bap:   ASE[1] configured: num_sink_ase=2
<inf> bt_bap: QoS: stream 0x200092d8
<inf> bt_bap: QoS: interval 10000 framing 0x00 phy 0x02 sdu 120 rtn 2 latency 10 pd 40000
<inf> bt_bap: QoS: stream 0x2000d9e8
<inf> bt_bap: QoS: interval 10000 framing 0x00 phy 0x02 sdu 120 rtn 2 latency 10 pd 40000
<inf> bt_bap: Enable: stream[0] meta_len 4
<inf> bt_bap: LC3 decoder[0]: 48000 Hz 10000 us ch=1
<inf> bt_bap: Release: stream 0x200092d8
<inf> bt_bap: Release: stream 0x2000d9e8
```

hci1 connection attempt (immediate auth failure):
```
<inf> bt_bap: Connected: 28:82:0E:EE:A4:97 (random)
<inf> bt_bap: Disconnected: 28:82:0E:EE:A4:97 (random) reason 0x05
```

## Commands executed

```bash
# Build & flash
fw-build-54l15
fw-flash-54l15

# Set hci1 static address (while powered off)
sudo btmgmt -i hci1 static-addr C0:98:E5:00:00:01

# Clear receiver bonds (zero storage partition)
openocd -f interface/cmsis-dap.cfg -c "adapter serial 8EE9B3FF" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf54l.cfg -c "init" -c "halt" \
  -f /tmp/clear-storage.tcl -c "reset run" -c "shutdown"

# Streaming attempts
nix develop --command python3 scripts/bap_central.py --duration 30 --freq 1000
nix develop --command python3 scripts/bap_central.py --adapter hci1 --duration 30 --freq 1000

# HCI capture
sudo btmon -w /tmp/phase4a-btmon2.log
```

## bap_central.py D-Bus output (hci0)

```
[enum] Existing device: /org/bluez/hci0/dev_DB_A6_0C_05_A2_AA name='LE Audio Receiver' paired=1 connected=1
[main] Disconnecting stale cached connection, reconnecting fresh...
[main] Already paired
[main] Trusted, connecting...
[main] Connected
[endpoint] SelectProperties: caps=03018000020203020303050414007800020501
[endpoint]  ChannelAllocation=0x0001
[endpoint] Returning config: {'Capabilities': '02010802020103047800050301000000', ...}
[endpoint] SelectProperties: caps=03018000020203020303050414007800020501
[endpoint]  ChannelAllocation=0x0002
[endpoint] Returning config: {'Capabilities': '02010802020103047800050302000000', ...}
[endpoint] SetConfiguration(/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA/pac_sink0/fd0)
[endpoint]  parsed channel_alloc=0x01
[endpoint] _acquire_transport FAILED: org.bluez.Error.Failed: Input/output error
```

## Files changed

### Commit 6c6ffdf (original Phase 4a attempt)

| File | Change |
|------|--------|
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Disable `&mx25r64` and `&spi00` (Xiao has no external flash) |
| `scripts/bap_central.py` | Enumeration of existing BlueZ devices; fresh reconnect on cached connections; `--adapter` argument; handle AlreadyExists pairing errors |
| `docs/development/phase4a-results.md` | Initial results document |
| `docs/development/phase4a-handoff.md` | Original handoff |

### Fix-loop commit (this commit) — review correction

| File | Change |
|------|--------|
| `scripts/bap_central.py` | **Refactor**: `SetConfiguration()` queues pending transport, returns immediately. Async `Acquire()` from main loop with `reply_handler`/`error_handler`, keeping GLib serviceable. Fixes D-Bus reentrancy deadlock. |
| `docs/development/phase4a-results.md` | Remove unsupported "R-4.2 confirmed" / "kernel ISO socket creation fails" claims. Rewrite hci1 as unresolved auth failure. Document script deadlock and corrected flow. Mark Phase 4a open. |
| `docs/development/phase4a-fix-handoff.md` | Fix-loop handoff (review instructions). |
| `.gitignore` | Add Python cache patterns (`__pycache__/`, `*.py[cod]`). |
| `scripts/__pycache__/` | Deleted (tracked cache artifact). |

## Review findings (2026-07-25 fix loop)

### Critical: D-Bus reentrancy deadlock in bap_central.py

`SetConfiguration()` called `MediaTransport1.Acquire()` (blocking, 30 s timeout)
from inside the D-Bus callback. BlueZ's BAP state machine cannot create the CIS
until the `SetConfiguration` D-Bus method returns. The callback never returns
because it blocks on Acquire. This is a **script bug**, not a controller
limitation.

**Fix applied** (this commit):
- `SetConfiguration()` now queues the transport path + channel allocation in
  `_pending_transports` and returns immediately.
- The main loop calls `Acquire()` asynchronously (dbus-python `reply_handler` /
  `error_handler`) after all `SetConfiguration` callbacks have returned.
- GLib remains serviceable during the Acquire wait because the main loop
  iterates the context rather than blocking inside a callback.

### hci1 pairing: unresolved, not SDC rejection

The previous conclusion "SDC peripheral rejected the all-zeros BD address" is
not supported by the evidence. The over-air address observed by the receiver
is `28:82:0E:EE:A4:97` (random), not the configured static address
`C0:98:E5:00:00:01`. This is consistent with BlueZ privacy overriding the
static-address setting. Disconnect reason 0x05 is `AUTH_FAIL` — the actual
causal chain is unproven.

### Retest required

Corrected-flow retest on hci0 is mandatory before any hardware-limitation
claim. See `docs/development/phase4a-fix-handoff.md` for the retest sequence
and evidence requirements.

## Blockers

1. **hci0 streaming unverified.** The `Input/output error` on Acquire matches
   the script's D-Bus reentrancy deadlock. Must retest hci0 with the corrected
   flow that calls Acquire() outside SetConfiguration().
2. **hci1 pairing failure unresolved.** Likely a BlueZ privacy/btmgmt
   interaction, not an SDC bug. Do not investigate further until hci0 is
   definitively eliminated.

## Recommended Phase 4b follow-up

Phase 4b (GRTC + DPPI drift measurement) should proceed in parallel with the
hci0 corrected-flow retest:
- GRTC + DPPI implementation on nRF54L15 is independent of the central
- While acquiring a working central, implement GRTC capture channels + DPPI
  routing
- Phase 4b acceptance criteria do not require streaming — they require
  hardware timestamp capture working
- Once hci0 is retested with corrected flow, re-run Phase 4a streaming
