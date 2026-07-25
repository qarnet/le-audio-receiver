# Phase 4a Results — nRF54L15 First End-to-End Audio Stream

Date: 2026-07-25
Controller primary: hci0 (Realtek RTL8761BU, A0:AD:9F:7B:C7:95)
Controller fallback: hci1 (nRF5340 DK USB HCI, BD all-zeros)
Receiver: nRF54L15 (Seeed Xiao), addr DB:A6:0C:05:A2:AA (random static)

## Acceptance criteria status

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Latest nRF54L15 flashes + boots cleanly, no warnings/errors | **PASS** | SPI NOR boot error fixed; boot log in serial capture shows clean init: `BLE ready` / `settings_load() OK` / `I2S ready` / `Advertising as "LE Audio Receiver"`. Zero warnings/errors. |
| 2 | BlueZ source establishes BAP transport and writes LC3 SDUs for requested duration | **PARTIAL** | BAP codec + QoS negotiation succeeds on hci0 (both FL+FR ASEs configured). btmon capture proves ASE Codec Config → QoS Config → Enable flow. Acquire() fails: `org.bluez.Error.Failed: Input/output error` — Realtek RTL8761BU cannot establish CIS. hci1 fallback: connection + pairing fails with `Authentication Rejected` at controller level (reason 0x05); nRF5340 DK USB HCI all-zeros BD rejected by SDC peripheral. |
| 3 | Receiver logs ASE Configure/QoS/Enable/Start and `Stream started` | **PARTIAL** | ASE Config (2 ASEs: FL=0x01, FR=0x02), QoS Config, Enable for ASE[0] all confirmed in serial log. LC3 decoder initialized: `48000 Hz 10000 us ch=1`. No `Stream started` because streaming never reached send phase (ISO acquire failed). See serial capture at [00:21:14] timestamp. |
| 4 | Frames decoded climb; decode errors = 0 | **NOT MET** | Frames decoded = 0 (no SDUs received). Decode errors = 0 (trivially). |
| 5 | Logic capture proves BCK/LRCK/DIN activity | **NOT MET** | No I2S activity observed (no stream to drive I2S). |
| 6 | No steady-state I2S underrun/slab-full/reset storm | **NOT MET** | No streaming occurred, so steady-state not reached. |
| 7 | Results document with exact commands + evidence | **MET** | This document. |

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
  - No `LE CIS Established` HCI event observed
  - Kernel ISO socket creation fails
  - Tried both 2M PHY and 1M PHY — same result
  - Root cause: Realtek RTL8761BU firmware does not support ISO/CIS establishment (known limitation per Arch community threads)

### hci1 — nRF5340 DK USB HCI (BD 00:00:00:00:00:00)
- Static address `C0:98:E5:00:00:01` set via `sudo btmgmt` (while powered off only)
- Discovery: OK — receiver found as `LE Audio Receiver` (RSSI -68 to -73)
- Connection: OK — receiver logs `Connected: 28:82:0E:EE:A4:97 (random)`
- **Pairing: FAIL** — `org.bluez.Error.AuthenticationRejected`
  - Receiver disconnects with reason 0x05 (BT_HCI_ERR_AUTH_FAIL) within 300 ms
  - Receiver's `pairing_accept` returns `BT_SECURITY_ERR_SUCCESS` — rejection is at SDC controller level, not host level
  - Config attempts: `static-addr` (set while powered off, confirmed), `privacy on` (permission denied), `public-addr` (rejected)
  - Storage partition cleared (ZMS 4K at 0x15c000 zeroed via OpenOCD) — no stale bonds remain
  - Root cause: nRF5340 DK USB HCI firmware may not support LE Secure Connections properly with all-zeros public address

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

| File | Change |
|------|--------|
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Disable `&mx25r64` and `&spi00` (Xiao has no external flash) |
| `scripts/bap_central.py` | Enumeration of existing BlueZ devices; fresh reconnect on cached connections; `--adapter` argument; handle AlreadyExists pairing errors |
| `docs/development/phase4a-results.md` | This results document |
| `docs/development/phase4a-handoff.md` | Handoff (already untracked; included in commit) |

## Blockers

1. **R-4.2 confirmed: Realtek RTL8761BU ISO/CIS does not work.** CIG parameters are accepted by the controller, but `LE CIS Established` never fires and Acquire() returns I/O error. This blocks all LE Audio streaming on hci0. Neither 1M nor 2M PHY helps.
2. **nRF5340 DK USB HCI has all-zeros BD address.** Static-addr/Public-addr rejected by controller; privacy insufficient for pairing. Blocks hci1 fallback.
3. **No working central available for Phase 4a streaming.** Options for follow-up:
   - Use a phone (Android/iOS) as BAP source — most phones support LE Audio unicast
   - Use a different USB BT adapter with verified ISO/CIS support (Intel AX210, MediaTek MT7921)
   - Use a second nRF5340 DK as BAP central (requires BlueZ + HCI firmware rebuild)

## Recommended Phase 4b follow-up

Phase 4b (GRTC + DPPI drift measurement) should proceed in parallel with resolving the central issue:
- GRTC + DPPI implementation on nRF54L15 is independent of the central
- While waiting for a compatible central, implement the GRTC capture channels + DPPI routing
- Phase 4b acceptance criteria don't require streaming — they require hardware timestamp capture working
- Once a compatible central is available, re-run Phase 4a streaming to verify the data path
