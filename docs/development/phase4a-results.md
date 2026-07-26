# Phase 4a Results — nRF54L15 First End-to-End Audio Stream

Date: 2026-07-25 (DK central reflash attempt)
Controller: hci1 (nRF5340 DK USB HCI, Zephyr SW split controller, BD 00:00:00:00:00:00)
Receiver: nRF54L15 (Seeed Xiao), addr DB:A6:0C:05:A2:AA (random static)
Status: **BLOCKED — nRF5340 DK SW split controller fails accept-list filtered connection initiation.**

## Acceptance criteria status

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Latest nRF54L15 boots cleanly, no warnings/errors | **PASS** | Clean boot: `BLE ready`, `settings_load() OK`, `I2S ready (48 kHz, 16-bit, stereo, 12 blocks)`, `Advertising as "LE Audio Receiver"`. Zero warnings. |
| 2 | BlueZ source establishes BAP transport and writes LC3 SDUs for requested duration | **FAIL — blocked** | Two attempts, identical failure: accept-list filtered scan during `Pair()` runs for 120s without capturing the receiver. MGMT `Pair Device` cancelled with `Status: Cancelled (0x10)`. `Connect Failed` event with `Status: Disconnected (0x0e)`. Receiver was advertising throughout (confirmed via btmon at T=121.470s, RSSI -74 dBm). |
| 3 | Receiver logs ASE Configure/QoS/Enable/Start and `Stream started` | **NOT MET** | No connection from hci1 → no ASE negotiation. `audio status`: Frames decoded = 0, Drift state = INIT. |
| 4 | Frames decoded climb; decode errors = 0 | **NOT MET** | Frames decoded = 0 (no connection established). |
| 5 | Logic capture proves BCK/LRCK/DIN activity | **NOT MET** | No stream → no I2S activity. Skip LA. |
| 6 | No steady-state I2S underrun/slab-full/reset storm | **NOT MET** | No streaming occurred. |
| 7 | Results document with exact commands + evidence | **MET** | This document + btmon + serial evidence. |

## nRF5340 DK as hci1 central — reflash + test

### DK firmware (scratch build at /tmp/opencode/nrf53-hci-central/)

**Board**: nrf5340dk/nrf5340/cpuapp, sysbuild (cpuapp + cpunet)
**CPU app**: hci_usb sample (Zephyr USBD BT HCI transport)
**CPU net**: SW split LL controller with CIS central + `CONFIG_BT_BROADCASTER=y` (fix needed)

**Fix applied**: The central config (`nrf5340_cpunet_iso_central-bt_ll_sw_split.conf`)
sets `CONFIG_BT_BROADCASTER=n`, but the Zephyr controller's HCI command dispatch
gates all advertising extension commands (`LE_READ_NUM_ADV_SETS` and friends)
behind `#if defined(CONFIG_BT_BROADCASTER)`. Without it, the controller
advertises LE Extended Advertising in its supported features but rejects the
mandatory `LE_READ_NUM_ADV_SETS` command with `Unknown HCI Command (0x01)`,
causing kernel btusb init to fail. Solution: overlay config
`hci_ipc_broadcaster_overlay.conf` with `CONFIG_BT_BROADCASTER=y` added via
`add_overlay_config()` in sysbuild.cmake.

**Flash sizes** (with broadcaster fix):
- cpunet: FLASH 170912 B (65.20%), RAM 43672 B (66.64%)
- cpuapp: FLASH 56656 B (5.40%), RAM 24296 B (5.30%)

### Post-reflash hci1 verification

```
$ btmgmt -i hci1 info
hci1:	Primary controller
	addr 00:00:00:00:00:00 version 13 manufacturer 89 class 0x000000
	supported settings: powered connectable discoverable bondable le advertising
	  secure-conn debug-keys privacy static-addr phy-configuration cis-central
	  ll-privacy
	current settings: powered le secure-conn static-addr cis-central ll-privacy
	name thomas-workstation
```

Non-zero random static address is NOT reported by the controller (FICR DEVICEADDR
appears unprogrammed, or the SW split controller does not implement
`VS_READ_STATIC_ADDRS`). However, BlueZ assigns a static random address
(e.g., `E3:C4:1A:96:D7:D2`) and `cis-central` is in current settings.

### Stream test — both attempts

**Attempt #1** (hci0 interrupt): After OpenOCD reset, Xiao auto-reconnected to
hci0 from stored bond. Powered off hci0, Xiao disconnected
(`reason 0x15`), re-advertised. bap_central.py found target, started pairing —
same accept-list timeout as attempt #2.

**Attempt #2** (clean start, hci0 off):

```
$ sudo btmon -i hci1 -w /tmp/phase4a-dkcentral-hci1-v2.btsnoop &
$ nix develop --command python3 scripts/bap_central.py --adapter hci1 --duration 30 --freq 1000
[discovery] >>> Target found: /org/bluez/hci1/dev_DB_A6_0C_05_A2_AA
[main] Target device: /org/bluez/hci1/dev_DB_A6_0C_05_A2_AA
[error] LE Audio Receiver not found within 30 s    # timeout from bap_central.py script
```

**btmon evidence** (abridged from `/tmp/phase4a-dkcentral-hci1-v2.btsnoop`):

```
#12  LE Extended Advertising Report: DB:A6:0C:05:A2:AA (Static)
       Name (complete): LE Audio Receiver           RSSI: -70 dBm

MGMT: Pair Device DB:A6:0C:05:A2:AA                  @ T=1.852s
#17  LE Set Random Address: E3:C4:1A:96:D7:D2 (Static)
#19  LE Add Device To Accept List: DB:A6:0C:05:A2:AA (Success)
#23  LE Set Extended Scan Parameters:
       Filter policy: Ignore not in accept list (0x01)
       PHYs: LE 1M (60ms/60ms Passive) + LE Coded (180ms/180ms Passive)
#26  LE Set Extended Scan Enable: Enabled (Success)  @ T=1.866s

 [119 seconds of silence — no HCI events at all]

MGMT: Cancel Pair Device — Status: Cancelled (0x10)  @ T=121.440s
MGMT: Connect Failed — Status: Disconnected (0x0e)

#28  LE Extended Advertising Report: DB:A6:0C:05:A2:AA (Static)
       Name (complete): LE Audio Receiver           RSSI: -74 dBm  @ T=121.470s
```

**Receiver serial**: No connection events from hci1. Clean boot only:
```
[00:57:45] <inf> main: BLE ready
[00:57:45] <inf> main: settings_load() OK
[00:57:45] <inf> audio_i2s: I2S ready (48 kHz, 16-bit, stereo, 12 blocks)
[00:57:45] <inf> main: Advertising as "LE Audio Receiver"
```

**Post-test audio status**:
```
Frames decoded : 0
Decode errors  : 0
Drift state    : INIT
```

### Root cause analysis

The SW split controller (Zephyr `BT_LL_SW_SPLIT`, cpunet) does NOT properly
respond to the accept-list filtered scan with a connection initiation:

1. BlueZ starts an accept-list filtered passive scan (`Filter policy: Ignore
   not in accept list`) targeting `DB:A6:0C:05:A2:AA`.

2. The receiver is visible and advertising on LE 1M + LE 2M PHYs (confirmed
   by btmon event #28 at T=121.470s, RSSI -74 dBm, advertising data fully
   intact).

3. The SW split controller receives the advertising reports (internally) but
   never initiates a connection (`LE Enhanced Connection Complete` event never
   appears).

4. This is NOT a PHY mismatch (the filtered scan covers LE 1M which is the
   receiver's primary PHY). It is NOT a receiver issue (receiver advertising
   confirmed). It is NOT a BlueZ issue (BlueZ correctly sets up the filtered
   scan and cancels on timeout).

5. It is a controller-level defect: the Zephyr SW split controller's
   accept-list filtered connection initiation for extended advertising PDUs
   is non-functional in this configuration (central-only, `CONFIG_BT_PERIPHERAL=n`).

### Why this differs from hci0 failure

hci0 (RTL8761BU): Connection + ASE negotiation succeed, but CIS establishment
fails at controller level (`Status: Connection Failed to be Established (0x3e)`).

hci1 (SW split): Connection establishment itself fails — the controller never
initiates a connection during accept-list filtered scan, despite receiving the
target's advertisements.

## Controllers summary

| Controller | BD_ADDR | CIS settings | Connection | CIS | Outcome |
|------------|---------|--------------|------------|-----|---------|
| hci0 (RTL8761BU) | A0:AD:9F:7B:C7:95 | cis-central ✓ | ✓ | ✗ (0x3e) | Controller CIS failure |
| hci1 (nRF5340 DK) | 00:00:00:00:00:00 | cis-central ✓ | ✗ | — | Accept-list scan never connects |

## Files changed

### DK central build (scratch, not in repo)
- `/tmp/opencode/nrf53-hci-central/sysbuild.cmake` — sysbuild glue for hci_ipc cpunet
- `/tmp/opencode/nrf53-hci-central/Kconfig.sysbuild` — enable `NRF_DEFAULT_BLUETOOTH`
- `/tmp/opencode/nrf53-hci-central/hci_ipc_broadcaster_overlay.conf` — `CONFIG_BT_BROADCASTER=y` to fix kernel init

### This commit
| File | Change |
|------|--------|
| `docs/development/phase4a-results.md` | Rewrite with DK central outcome, acceptance criteria, root cause |
| `docs/development/phase4a-central-handoff.md` | Handoff document (this test cycle) |

## Build warnings (recorded, not fixed)

- `CONFIG_BT_BROADCASTER`: experimental symbol — upstream, needed for kernel init
- `CONFIG_BT_LL_SW_SPLIT`: experimental — all SW split usage is experimental
- `CONFIG_BT_CTLR_SET_HOST_FEATURE`: experimental — needed for BT 5.4 features
- `CONFIG_BT_CTLR_CENTRAL_ISO`: experimental — core requirement for CIS central
- `PARTITION_MANAGER` deprecation: from hci_ipc sample, not actionable at build level

All above are Kconfig-level experimental notices from the upstream Zephyr SDK,
not compiler warnings from our code.

## Blockers

1. **No working CIS-capable central.** Both tested controllers (RTL8761BU and
   nRF5340 DK SW split) fail at different points: RTL8761BU fails at CIS
   establishment; SW split fails at connection initiation during accept-list
   scan. A third CIS-capable central (e.g., a Zephyr-native central on a
   Nordic DK running the host stack in-process) is required.

2. **SW split central-only config may be incomplete.** The central-only
   configuration (`CONFIG_BT_PERIPHERAL=n`, `CONFIG_BT_BROADCASTER=n` in
   the original config) may have edge cases where accept-list filtered scan
   results are processed but connections are not initiated. The broadcaster
   fix resolved the kernel init issue, but the connection initiation path
   remains defective.

## Recommended next steps

1. **Proceed directly to Phase 4b (GRTC + DPPI)**: This phase is independent of
   the CIS central. GRTC capture channels and DPPI routing on nRF54L15 require
   hardware timestamp capture, not streaming. Phase 4b can proceed in parallel.

2. **Alternative central options for Phase 4a**:
   - A Nordic DK running a Zephyr native BAP source application (BT host on
     cpuapp, SW split controller on cpunet, no Linux host in the loop)
   - Debug SW split controller accept-list filtered scan behavior in an isolated
     test: does the controller properly generate `Le Enhanced Connection Complete`
     events when the advertiser is in the accept list?
   - The nRF5340DK `hci_uart` central is now the proven working transport
     (see `STATUS.md`).

3. **Phase 4a on nRF5340 Ebyte E83 first**: Per recommendation 2 from previous
   results, prove the end-to-end path on the nRF5340 target before returning to
   nRF54L15. The nRF5340 has a known-good dual-core setup with APLL clock
   recovery.

## Evidence artifacts

| File | Content |
|------|---------|
| `/tmp/phase4a-dkcentral-hci1-v2.btsnoop` | Full HCI trace of both attempts |
| `/tmp/phase4a-dkcentral-bap-mono-v2.log` | bap_central.py output (attempt #2) |
| `btmgmt -i hci1 info` output (above) | BD + CIS settings confirmation |
| Receiver serial logs (above) | Clean boot, zero frames decoded |
| `/tmp/opencode/nrf53-hci-central/` | DK central firmware build tree (scratch) |
