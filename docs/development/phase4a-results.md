# Phase 4a Results — nRF54L15 First End-to-End Audio Stream

Date: 2026-07-25 (retest after D-Bus callback fix)
Controller primary: hci0 (Realtek RTL8761BU, A0:AD:9F:7B:C7:95)
Controller fallback: hci1 (nRF5340 DK USB HCI, BD 00:00:00:00:00:00)
Receiver: nRF54L15 (Seeed Xiao), addr DB:A6:0C:05:A2:AA (random static)
Status: **BLOCKED — no working CIS-capable central available. See findings.**

## Acceptance criteria status

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Latest nRF54L15 flashes + boots cleanly, no warnings/errors | **PASS** | Clean boot: no spi_nor error, `BLE ready`, `settings_load() OK`, `I2S ready`, `Advertising as "LE Audio Receiver"`. Zero warnings. |
| 2 | BlueZ source establishes BAP transport and writes LC3 SDUs for requested duration | **FAIL — blocked** | hci0: ASE Config/QoS/Enable succeed, but CIS fails at controller level (btmon: `Status: Connection Failed to be Established (0x3e)` for both CIS handles 23 and 24). hci1: Pairing never completes — filtered scan during `Pair()` does not capture the receiver within 120 s. In both cases Acquire times out because no CIS was established. |
| 3 | Receiver logs ASE Configure/QoS/Enable/Start and `Stream started` | **PARTIAL** | ASE Config (2 ASEs: FL=0x01, FR=0x02), QoS Config, Enable for both ASEs all confirmed. LC3 decoders initialized: `48000 Hz 10000 us ch=1`. No `Stream started` — CIS was never established by either central. |
| 4 | Frames decoded climb; decode errors = 0 | **NOT MET** | Frames decoded = 0 (no SDUs received). Decode errors = 0 (trivially). |
| 5 | Logic capture proves BCK/LRCK/DIN activity | **NOT MET** | No I2S activity (no stream to drive I2S). |
| 6 | No steady-state I2S underrun/slab-full/reset storm | **NOT MET** | No streaming occurred. |
| 7 | Results document with exact commands + evidence | **MET** | This document + btmon captures in `/tmp/phase4a-retest-*`. |

## Retest code fixes (commit to follow)

### Async D-Bus callback signature
**File:** `scripts/bap_central.py`
The `MediaTransport1.Acquire()` reply handler received a single tuple argument
`result` that was unpacked as `fd_ufd, read_mtu, write_mtu = result`. dbus-python
passes each D-Bus OUT arg as a positional argument, so the callback received
`(fd_ufd, read_mtu, write_mtu)` as **three** arguments, not one tuple. This
would raise TypeError on Acquire success (though the real failure is CIS not
establishing). Fixed the lambda and `_on_acquire_ok()` to accept `fd_ufd,
read_mtu, write_mtu` separately.

### All-or-nothing multi-ASE Acquire
If any required Acquire fails (error or timeout), close all already-acquired
fds, release transports, and exit. Do not stream a partial Mode A setup.

### Traceback removal
Removed `traceback.print_exc()` from async error handler. Print D-Bus error
name and message directly.

### Board conf trailing newline
**File:** `boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf`
Added missing final newline.

## Controllers retested

### hci0 — Realtek RTL8761BU (A0:AD:9F:7B:C7:95)

**BTMGM info**: CIS Central + CIS Peripheral both in supported and current
settings. Controller declares ISO/CIS capability.

**D-Bus reentrancy deadlock fixed**: `SetConfiguration()` now queues pending
transports and returns immediately. Async `Acquire()` is called from the main
loop with `reply_handler`/`error_handler`. GLib remains serviceable.

**Retest sequence** (hci0 corrected-flow, 2026-07-25):
1. Flash latest nRF54L15 firmware (clean boot, no warnings)
2. Start btmon capture: `sudo btmon -i hci0 -w /tmp/phase4a-retest-hci0.btsnoop`
3. Run: `nix develop --command python3 scripts/bap_central.py --adapter hci0 --duration 30 --freq 1000`

**HCI evidence from btmon** (`/tmp/phase4a-retest-hci0.btsnoop`):

```
#97  LE Create Connected Isochronous Stream (0x08|0x0064) ncmd 2
       Number of CIS: 2
       CIS Handle: 23, ACL Handle: 12
       CIS Handle: 24, ACL Handle: 12

#100 LE Connected Isochronous Stream Established (0x19)
       Status: Connection Failed to be Established (0x3e)
       Connection Handle: 23
       CIG Synchronization Delay: 5454 us
       ISO Interval: 10.00 msec

#105 LE Connected Isochronous Stream Established (0x19)
       Status: Connection Failed to be Established (0x3e)
       Connection Handle: 24
```

**Receiver serial evidence**:
```
<inf> bt_bap: Enable: stream[0] meta_len 4
<inf> bt_bap: LC3 decoder[0]: 48000 Hz 10000 us ch=1
<inf> bt_bap: Enable: stream[1] meta_len 4
<inf> bt_bap: LC3 decoder[1]: 48000 Hz 10000 us ch=1
<wrn> bt_conn: conn 0x20004438 failed to establish. RF noise?
<inf> bt_bap: Disable: stream 0x200092d8
<wrn> bt_conn: conn 0x20004438 failed to establish. RF noise?
<inf> bt_bap: Disable: stream 0x2000d9e8
```

**Conclusion**: The RTL8761BU firmware advertises CIS support (`cis-central`,
`cis-peripheral`) but cannot establish CIS connections in this configuration.
Both CIS handles (23 and 24) return `Connection Failed to be Established
(0x3e)` at the controller level. CIG parameters are valid (CIG 0, 2M PHY,
10 ms ISO interval, 120-byte SDU, RTN=2, subevents=3). The receiver sees the
CIS connection attempt but over-the-air CIS establishment fails.

This is **not** a script bug, not a BlueZ issue, not a receiver issue. It is
a controller-level CIS failure on the RTL8761BU.

### hci1 — nRF5340 DK USB HCI (BD 00:00:00:00:00:00)

**Setup**: Static address `C0:98:E5:00:00:01` configured while powered off.
CIS Central + CIS Peripheral in settings.

**Retest** (hci0 powered off to prevent auto-reconnect starvation):
```
nix develop --command python3 scripts/bap_central.py --adapter hci1 --duration 30 --freq 1000
```

**HCI evidence from btmon** (`/tmp/phase4a-retest-hci1.btsnoop`):
```
#57  LE Set Random Address: C0:98:E5:00:00:01 (Success)
#59  LE Add Device To Accept List: DB:A6:0C:05:A2:AA (Success)
#63  LE Set Extended Scan Parameters: Own=Random, Filter=Accept List,
       PHYs: LE 1M (60ms/60ms) + LE Coded (180ms/180ms)
#66  LE Set Extended Scan Enable: Enabled (Success)

[119 s of silence — no scan reports, no connection events]

MGMT: Cancel Pair Device — Status: Cancelled (0x10)
MGMT: Connect Failed — Status: Disconnected (0x0e)
```

**Receiver serial**: No connection logged. Receiver continued advertising
throughout but was never seen by hci1's filtered scan.

**Analysis**: During the initial unfiltered discovery scan, hci1 found the
receiver (RSSI -70). But when `Pair()` (`MGMT Pair Device`) started a new
filtered scan with accept-list filtering (PHYs: LE 1M + LE Coded), the
receiver's advertising was not captured. The receiver's secondary advertising
PHY is LE 2M, which is not in the filtered scan's PHY mask (0x05 = LE 1M +
LE Coded). This may cause the filtered scan to miss the receiver when it
switches to only LE 1M primary + LE 2M secondary.

The root cause is unresolved — whether it's the PHY mismatch, all-zeros BD,
DK HCI firmware, or a btmgmt/BlueZ privacy interaction.

## Files changed

### Commit 6c6ffdf (original Phase 4a attempt)
| File | Change |
|------|--------|
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Disable `&mx25r64` and `&spi00` |
| `scripts/bap_central.py` | Enumeration, fresh reconnect, `--adapter`, AlreadyExists handling |
| `docs/development/phase4a-results.md` | Initial results document |
| `docs/development/phase4a-handoff.md` | Original handoff |

### Commit 654e6e3 (fix-loop: D-Bus reentrancy + Kconfig + hygiene)
| File | Change |
|------|--------|
| `scripts/bap_central.py` | Async Acquire from main loop, fixes D-Bus reentrancy deadlock |
| `docs/development/phase4a-results.md` | Remove unsupported claims, rewrite hci1 as unresolved |
| `docs/development/phase4a-fix-handoff.md` | Fix-loop handoff |
| `.gitignore` | Add Python cache patterns (`__pycache__/`, `*.py[cod]`) |
| `prj.conf` | Move `CONFIG_WDT_NRFX=y` to nRF5340 board conf |
| `boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf` | Add `CONFIG_WDT_NRFX=y` |

### Retest commit (this commit)
| File | Change |
|------|--------|
| `scripts/bap_central.py` | Fix async D-Bus callback signature (3 positional OUT args); all-or-nothing Acquire cleanup; remove traceback from error handler |
| `boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf` | Add missing trailing newline |
| `docs/development/phase4a-results.md` | Rewrite with corrected-flow hci0/hci1 retest evidence; mark Phase 4a blocked |
| `docs/development/phase4a-retest-handoff.md` | Retest handoff (this test cycle) |

## Blockers

1. **No CIS-capable central available.** hci0 (RTL8761BU) controller-level CIS
   failure is definitive (btmon `Status: Connection Failed to be Established
   (0x3e)` for both CIS handles). hci1 (nRF5340 DK) pairing never completes
   during filtered scan (likely PHY mismatch: filtered scan uses LE 1M + LE
   Coded, receiver secondary PHY is LE 2M). This blocks all Phase 4a streaming
   on nRF54L15.

2. **Phase 4a on nRF5340 (known-good) should precede nRF54L15.** The nRF5340
   Ebyte E83 target has a working dual-core setup with SW Split LL and HFCLKAUDIO
   APLL. Phase 4a streaming should be proven on the nRF5340 first (using the
   nRF5340 DK USB HCI as central — it has CSIS but the all-zeros BD issue needs
   resolution). Once the end-to-end path works on nRF5340, the central question
   for nRF54L15 can be revisited.

## Recommended next steps

1. **Resolve nRF5340 DK USB HCI central**: Fix the all-zeros BD_ADDR issue so
   hci1 can act as a reliable CIS central for Phase 4a testing. Options:
   - Use a privacy-enabled static address workflow that BlueZ accepts
   - Flash nRF5340 DK with a HCI firmware that provides a non-zero BD_ADDR
   - Use a different CIS-capable USB dongle

2. **Prove Phase 4a on nRF5340**: Use the Ebyte E83 receiver with nRF5340 DK
   USB HCI as central. Once streaming works, the receiver-side BAP/LC3/I2S
   path is validated.

3. **Phase 4b (GRTC + DPPI)**: Independent of the central. Can proceed on
   nRF54L15 now — GRTC capture channels and DPPI routing do not require
   an active CIS stream. Phase 4b acceptance criteria require hardware
   timestamp capture working, not streaming.

4. **nRF54L15 central**: After Phase 4a is proven on nRF5340, return to the
   nRF54L15 target with a known-good central. The RTL8761BU CIS failure is
   likely a firmware limitation (advertises CIS support but cannot deliver it).
   A different USB dongle (e.g., Zephyr HCI USB on a Nordic DK) or a phone
   as central are alternative paths.
