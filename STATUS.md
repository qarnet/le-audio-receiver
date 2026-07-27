# STATUS — le-audio-receiver — 2026-07-27

> Probe identities are resolved at runtime via `nrf-probes`. Never assume a
> serial↔board mapping from docs — run `nrf-probes`.

## Stage 0 — PASS (2026-07-27)

Dongle compile-time identity fix landed (87b8d36). hci_ipc netcore firmware calls
`bt_ctlr_set_public_addr()` before `bt_enable_raw()` with lab-only address
`C0:AA:BB:CC:DD:EE` (see `dongle/hci_identity.h`). No more `btmgmt static-addr`
workaround — scanning and GATT discovery work natively. Build via `fw-build-dongle`.

SMP pairing fix landed (52abde1). `--peer-addr` raw-HCI path now uses
`own_address_type=public` (0x00) matching the dongle's compile-time identity.
Previously used Random (0x01) causing DHKey Check mismatch + SMP timeout.
ACL held open for `duration+120 s` so `Pair()` succeeds over existing ACL.

**Stage 0 gate closed**: 60 s Mode A stream (6000 frames, 100.0 fps) on nRF54L15.
Pairing + bond + 2 ASE config + CIS audio path fully verified on clean state
(no prior bonds). RX hex dump (chan_alloc 0x01+0x02) confirmed stereo content.
FLPR: healthy, errors zero, RX lost/dup/ooo/missed zero. Audio: push failures=0,
repeat fb=0, ASRC cap fail=0. Zero warnings, zero assertions, zero faults.

See `docs/development/phase6-stage0-results.md` for full verification evidence.

## Phase 5 — COMPLETE (2026-07-27)

cpuapp fixed-point linear stereo ASRC accepted. Mode A (two mono ASEs) +
Mode B (single stereo ASE) each ran 600 s autonomous central streams on
nRF54L15 with zero faults. SAMPLE_ADJUST actuator removed from production
Kconfig; two actuators remain: APLL (nRF5340) and NONE (nRF54L15, ASRC
consumes ppm). All 20 ASRC unit tests pass (native_sim). nRF5340 builds
(hardware regression deferred — no E83 probe). See
`docs/development/phase5-hardware-acceptance-results.md`.

## Bottom line

**BLE ISO transport verified** — 3,000 ISO Data TX packets over 15 s through
nRF5340DK `hci_uart` central; two CISes (Mode A stereo), 48 kHz LC3 at 100
fps, zero flow-control stalls. Receiver SDUs arrive correctly.

**Standalone I2S20 hardware/DMA verified** — a standalone I2S20 tone test ran
20.001 seconds, fed 2,016 blocks, zero EIO/underrun. I2S20 register state
(ENABLE, PSEL, FRAMESTART) confirmed working. GPIO mapping D0/P1.4 (BCK),
D1/P1.5 (LRCK), D2/P1.6 (SDOUT) proven.

**Old DAC caused LRCK anomaly** — with the old DAC breakout connected and MUTE
low, D1/LRCK was held high (no toggling). With digital wires removed, D1
toggles. The old breakout/wiring assembly is incompatible or defective.

**Phase 4b.2 — PCLK feedforward + phase PI: HARDWARE PASS** (2026-07-26).
Refactored `audio_drift.c` to explicit PCLK frequency feedforward
(`audio_drift_frequency_error_update()`) and per-block buffer-phase PI
(`audio_drift_controller_update(slab_free)`).  Pure integer, no floating
point.  Corrected sign (phase error = setpoint - slab_free).  Directional
anti-windup: at saturation rail, same-direction phase increments are
blocked; opposite-direction increments are always allowed so the
integrator can unwind toward range.  Thread-safe: k_spinlock serialises
workqueue/audio-path/reset calls.  Configurable output clamp and phase
integral clamp via Kconfig.  nRF54L15 board sets output clamp=2000,
phase integral=150.  nRF5340 keeps defaults (500/500).  Removed
timestamp-based frequency estimation and `audio_sink_sdu_ref_update()`.
ISO timestamps now go ONLY to `audio_timing_sdu_ref_update()` for GRTC
scheduling.  nRF54 timing feeds every 1 s PCLK measurement (not just
diagnostics) to `audio_drift_frequency_error_update()` via the work
handler (ISR-safe).  Bounded runtime actuator evidence: in `audio_i2s.c`,
counts insert/drop adjustments; logs first and every 500th adjustment.
Closed-loop streaming verified: 4,500 frames / 45 s at 100 fps, PCLK
diagnostics +1,500..+1,757 ppm, channel-pair gate correct (drops-only
before first PCLK measurement), inserts dominate (186:1 ratio), clean
teardown.  Phase 4c (10-minute stability + listening test): **Technical
PASS** (60,000 frames / 600.00 s, zero disconnect, zero
slab-full/underrun/warning/error/fault, clean teardown).  Physical
audibility UNAVAILABLE (user did not provide listening report) — not failed,
not blocking further measurable work. Phase 5 ASRC is planned implementation
work — see `docs/design.md` Phase 5.
See `docs/development/phase4b1-results.md` for Phase 4b.1 hardware PASS
evidence (3,000 frames / 30 s, +1,665..+1,884 ppm),
`docs/development/phase4b2-results.md` for Phase 4b.2 hardware PASS
evidence, and
`docs/development/phase4c-technical-results.md` for Phase 4c technical
PASS evidence.

| Test | Result |
|------|--------|
| fw-build-5340 | PASS (8 Kconfig/CMake diagnostics; no compiler warnings) |
| fw-build-54l15 | PASS (5 Kconfig/CMake diagnostics; no compiler warnings) |
| ASRC unit tests | 20/20 PASS (native_sim) |
| drift unit tests | 18/18 PASS |
| actuator unit tests | 7/7 PASS |
| timing unit tests | PASS |
| lifecycle unit tests | PASS |
| decode unit tests | PASS |
| rate_convert unit tests | PASS |
| perf unit tests | PASS |
| Mode A 600 s (nRF54L15) | PASS — 60000 frames, zero faults |
| Mode B 600 s (nRF54L15) | PASS — 60000 frames, zero faults |
3,500 frames at 100 fps, zero slab-full drops, zero DMA underruns,
I2S DMA started cleanly, push_ret=0 consistently. Root cause was fixed
PCLK32M hardware-rate mismatch (~47,619 Hz LRCK vs 48,000 Hz decoder output)
plus fixed 480-frame writes — not PI controller gain. Fix: bounded
nearest-neighbor rate converter maps 480 input → 476/477 output frames per
block. See `docs/development/phase4a2-rate-conversion-results.md`. Phase 4b
GRTC + PCLK feedforward + SAMPLE_ADJUST now operational (see Phase 4b.1/4b.2
results above). Physical audibility UNAVAILABLE (user did not provide
listening report); Phase 5 ASRC is planned implementation work.

### Build warning diagnostics (2026-07-27)

Neither target produces compiler warnings in application or Zephyr source.
All printed diagnostics are Kconfig/CMake configuration messages.

**nRF5340 (8 diagnostics):**

| Diagnostic | Classification | Cannot remove because |
|---|---|---|
| Deprecated `PARTITION_MANAGER` / `_ENABLED` | NCS v3.3.0 SDK deprecation | Required for multi-image flash layout; no migration path in v3.3.0 |
| `__ASSERT()` statements globally ENABLED | Zephyr informational | Not a defect |
| `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice has no selection | Upstream Kconfig gap in SW Split LL | Both sub-options (`RELIABILITY`, `LOW_LATENCY`) depend on `BT_CTLR_CENTRAL_ISO`, which is disabled for peripheral-only; choice has no NONE fallback |
| Experimental `BT_LL_SW_SPLIT` | Required architecture | Only ISO-capable open-source controller for nRF5340 |
| Experimental `BT_CTLR_SET_HOST_FEATURE` | Required for ISO | Feature negotiation required |
| Experimental `BT_CTLR_PERIPHERAL_ISO` | Required for ISO | Peripheral ISO support required |
| `SB_CONFIG_PARTITION_MANAGER` sysbuild warning | Required sysbuild infrastructure | Partition manager required by NCS build system |

**nRF54L15 (5 diagnostics):**

| Diagnostic | Classification | Cannot remove because |
|---|---|---|
| Deprecated `PARTITION_MANAGER` / `_ENABLED` | NCS v3.3.0 SDK deprecation | Same as nRF5340 |
| `__ASSERT()` statements globally ENABLED | Zephyr informational | Not a defect |
| `drivers__watchdog`: No SOURCES given | Zephyr internal: `CONFIG_WATCHDOG=y` but no DT node on nRF54L15 board | WD disabled on nRF54L15 would change firmware behavior (watchdog is desired) and is out of scope for closeout; DT node is board-level, not repo |
| `SB_CONFIG_PARTITION_MANAGER` sysbuild warning | Required sysbuild infrastructure | Same as nRF5340 |

## Hardware in use

Probe identities resolved at runtime via `nrf-probes` — no static serials in docs.

| Role | Board | Console | Notes |
|------|-------|---------|-------|
| LE Audio central | nRF5340DK | none | runs `hci_uart`, attached to PC over J-Link VCOM |
| LE Audio receiver | nRF54L15 (Seeed Xiao) | `/dev/ttyACM0` @ 115200 (serial-mcp) | runs this repo's firmware |
| Logic analyzer | fx2lafw | — | D0=SCK (P1.4), D1=LRCK (P1.5), D2=SDOUT (P1.6) |

- PC-side BT controller: `hci0` = nRF5340DK `hci_uart` on
  **`/dev/ttyACM2`** (J-Link VCOM, USB iface 02 — not ttyACM1/iface-00)
  @ 1 000 000 baud, H4, HW flow control.
- Receiver advertises as "LE Audio Receiver".

## What works (verified)

- Discovery (raw unfiltered scan), connect (raw direct LE Extended Create
  Connection — kernel accept-list connect path is broken on SDC).
- JustWorks pairing, bonded, persists across reboots. Receiver logs
  `Pairing complete, bonded: 1`.
- BAP unicast server negotiation: 2× SelectProperties, 2×
  SetConfiguration, 2× Acquire (Mode A, FL/FR, SDU 120 @ 10 ms, 2M PHY).
- 2× CIS established, data path HCI both directions.
- ISO data TX: **3000 ISO Data TX packets over 15 s** (= 2 streams ×
  1500 frames) with **3024 Number of Completed Packets** events returned.
  No EAGAIN, no stall. `bap_central.py --duration 15` reports
  `Done: 1500 frames in 15.00 s (100.0 fps)`.
- ISO data RX at the receiver: valid SDUs arrive — `stream_recv tally:
  valid=1006 invalid=144` (climbing). BLE transport verified.
- Receiver-side recovery from the post-stream disconnect panic
  (`audio_sink_stop`: PREPARE before DROP in `src/audio_i2s.c`).
- **Clean ACL teardown** in `bap_central.py` (BlueZ Disconnect +
  raw-HCI helper termination) — three consecutive runs with no DK
  reset between them, no zombie-slot exhaustion. `fw-reset-dongle`
  helper exists for recovery from a crashed run that bypassed cleanup.

## What does NOT work / open

### I2S20 hardware evidence

Standalone I2S20 works. The old DAC breakout caused LRCK anomaly.

| Test | Result |
|------|--------|
| GPIO pin map | D0/P1.4 = BCK, D1/P1.5 = LRCK, D2/P1.6 = SDOUT. Confirmed. |
| PCLK32M clock source | Works. `PCLK32M_HFXO` UsageFault tracked separately. |
| Standalone I2S20 tone test | 20.001 s, 2,016 blocks fed, zero EIO/underrun. ENABLE=1, PSEL correct, FRAMESTART firing. |
| Old DAC digital wires connected, MUTE low | D1/LRCK held high — no toggling. Breakout/wiring incompatible or defective. |
| Old DAC digital wires removed | D1/LRCK toggles. GPIO toggling confirmed. |
| Main receiver with new DAC (Phase 4c) | **Technical PASS** — 60,000 frames / 600.00 s, zero disconnect, zero slab-full/underrun/warning/error/fault, clean teardown. See `docs/development/phase4c-technical-results.md`. |
| External I2S analyzer | **PASS** — 24 MHz fx2lafw capture at DAC pins: BCK 1,525,637.347 Hz, LRCK 47,676.613 Hz, ratio 31.999701, SDOUT active. See `docs/development/phase4c-i2s-analyzer-results.md`. |
| Phase 4a.2 rate conversion | **PASS** — 35 s stream, 0 slab-full, 0 underrun. Fixed-rate converter matches PCLK32M drain. See `docs/development/phase4a2-rate-conversion-results.md`. |

### Next actions (ordered)

 1. ~~**Phase 4b.1** — GRTC-referenced timing foundation~~ → PASS
 2. ~~**Phase 4b.2** — PCLK feedforward + phase PI~~ → PASS
 3. ~~**Phase 4c** — Hardware streaming verification~~ → Technical PASS
 4. ~~**Phase 5** — ASRC quality upgrade~~ → ACCEPTED (2026-07-27).
    Mode A + Mode B 600 s, zero faults. See
    `docs/development/phase5-hardware-acceptance-results.md`.
 5. **Phase 6** — FLPR offload (intended implementation work). Move accepted
    ASRC from cpuapp to FLPR. Staged: handshake/rings → identity loopback →
    ASRC port → reset/fault/fallback → optimize. See `docs/design.md` Phase 6.
 6. **BabbleSim** — cross-cutting verification track (research + implementation).
    Provision environment, fix sysbuild/harness, build smallest-useful
    nRF5340bsim dual-core scenario. See `docs/design.md` BabbleSim section.

### hci_usb firmware cannot do ISO (settled — don't revisit)

Zephyr's USB device_next BT HCI class
(`subsys/usb/device_next/class/bt_hci.c`) has **no ISO data path** — the
isochronous endpoints are descriptor stubs so Linux `btusb` binds (source
comment lines 85–90: "we do not implement isochronous endpoints
handling"). ISO TX (device→host) hits a `default:` case that drops the
packet **and leaks the net_buf**; ISO RX is never armed. The legacy USB BT
class (`subsys/usb/device/class/bluetooth.c`) has no isochronous
endpoints at all either. **No Zephyr USB BT transport can carry LE Audio
ISO in v3.3.0.** hci_uart is the only working transport. Patching hci_usb
for ISO would mean SDK surgery + nRF UDC EP8+ remap — declined.

### btattach not persistent

Runs as a background process from the session. Needs a udev rule /
systemd unit so it survives reboot and re-enumeration.

### LSP `gnu/stubs-32.h` not found warning

The C/C++ language server (clangd/editor) reports `gnu/stubs-32.h`
missing when parsing `src/*.c` and NCS headers — glibc on this system is
64-bit-only and the LSP falls back to the host sysroot instead of the
NCS toolchain's. It is **IDE noise only**; the firmware build
(`fw-build-*`) is unaffected (it uses the NCS toolchain's own sysroot).
Fix later by pointing the LSP/compiler-commands at the NCS toolchain
sysroot (e.g. clangd config with `--sysroot=` from `nix-nrf-dev`, or
generate `compile_commands.json` from the Zephyr build and let clangd
use it). Low priority — does not block builds or flashing.

## Reproduce

### 1. Build + flash the nRF5340DK central (hci_uart)

The dongle config lives in `dongle/hci_uart/{app,netcore}.conf`; the
build helpers build the upstream Zephyr hci_uart sample with those
fragments applied:

```bash
fw-build-dongle      # builds into build/dongle/
fw-flash-dongle      # flashes both cores via the DK's onboard J-Link
```

The netcore conf (`dongle/hci_uart/netcore.conf`) is the load-bearing
part: ISO central, 2 conns / 2 CISes, ext adv, no Coded PHY, no privacy.
See `dongle/README.md` for the full rationale.

### 2. Attach + set up hci0

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3     # NINO — required for JustWorks receiver
sudo btmgmt --index hci0 sc on        # receiver requires SC pairing
# verify: settings should include "powered le secure-conn static-addr cis-central"
```

If `btmgmt` reports no adapter, btattach isn't running or the DK
re-enumerated — re-run the btattach line. If the dongle's netcore has
zombie connection slots from a crashed run (`Connection Rejected 0x0d`),
reset with `fw-reset-dongle` then re-attach.

### 3. Stream

```bash
python3 scripts/bap_central.py --duration 15
```

Expected: `ACL link up` → `ServicesResolved` → 2× SelectProperties →
2× SetConfiguration → 2× Acquired → `Streaming 1000 Hz sine` →
`Done: 1500 frames in 15.00 s (100.0 fps)`.

Receiver console (serial-mcp on `/dev/ttyACM0`) during a good run:
`Pairing complete, bonded: 1`, 2× `ASE Config`, `LC3 decoder[0/1]`,
`Stream[x] started`, `audio_i2s: I2S DMA started` — then steady-state
streaming with sample-insert/drop corrections logged at startup and every
500th adjustment; no slab-full or underrun events in steady state.

### 4. Verify ISO actually crossed HCI (optional)

```bash
setsid sudo btmon -i hci0 -w /tmp/btmon.btsnoop </dev/null >/tmp/btmon.log 2>&1 &
python3 scripts/bap_central.py --duration 15
sudo pkill -f "btmo[n] -i hci0 -w"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "ISO Data TX"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "Number of Completed Packets"
# expect: ~3000 ISO Data TX, ~3000+ Number of Completed Packets
```

## Why the BT transport works now (root causes fixed)

| # | Problem | Fix |
|---|---------|-----|
| 1 | hci_usb firmware had no ISO path at all — ISO packets died at the USB layer, no completions ever returned → 3-packet stall | Switched to `hci_uart` (app core is a plain H4 pipe; ISO passes as H4 type 0x05) |
| 2 | Dongle netcore had no ISO / ext-adv / coded-PHY tuning | Netcore conf with `BT_ISO_CENTRAL=y`, `BT_MAX_CONN=2`, `CONN_ISO_STREAMS=2`, `BT_EXT_ADV=y`, `BT_CTLR_PHY_CODED=n`, `BT_CTLR_PRIVACY=n` |
| 3 | Kernel LE connect uses accept-list filtered scan — broken on SDC (zero reports, even for legacy advertisers) | Raw-HCI direct `LE Extended Create Connection` via `scripts/hci_raw_connect.py`, wired into `bap_central.py` |
| 4 | BlueZ demanded MITM; receiver is JustWorks-only (`CONFIG_BT_SMP_ENFORCE_MITM=n`) | NINO agent + `btmgmt io-cap 3` (adapter-level IO cap must also be NINO — kernel uses it for auto-security SMP) |
| 5 | Kernel mgmt `Pair Device` on raw-created conn completes instantly (~6 µs) → BlueZ's `pair_device_complete` clears bonding early → auto-rejects the SMP User Confirmation (CVE-2020-26555: kernels always confirm JustWorks) | Script no longer calls `Pair()`; relies on BlueZ auto-security via GATT (encrypted access to PACS triggers kernel SMP directly). NINO agent's `RequestAuthorization` accepts the confirmation. |
| 6 | Stale bond on PC vs wiped receiver keys → auth failure loop | Deleted `/var/lib/bluetooth/<adapter>/<receiver>/` bond dir, power-cycled hci0 (`btmgmt power off/on` flushes kernel key store — bluetoothd restart alone does not) |
| 7 | Receiver kernel panic on disconnect after stream (nrfx_i2s ASSERT on de-initialized instance) | `audio_sink_stop()` sends `TRIGGER_PREPARE` before `TRIGGER_DROP` (`src/audio_i2s.c`) |
| 8 | Zombie SDC connection slots on the dongle netcore after repeated raw-HCI connects without clean disconnect (`Connection Rejected 0x0d`) | `bap_central.py` cleanup now calls BlueZ `Device1.Disconnect()` (graceful HCI disconnect) then terminates the raw-HCI helper. Three consecutive runs with no DK reset. `fw-reset-dongle` helper for recovery. |

### Evidence for the hci_usb → hci_uart switch

The original hci_usb dongle carried a vanilla Zephyr hci_usb build
(SW-split LL). Connections hung ~90 s. Reflashing with the SDC netcore
tuning above fixed discovery but ISO data stalled after exactly 3
packets (SDC default ISO TX HCI buffer count): only 3 `ISO Data TX`
crossed HCI, no `Number of Completed Packets` ever returned, BlueZ's
userspace buffer filled (~445 writes ≈ 2.2 s) → EAGAIN. Root cause:
hci_usb has no ISO USB path (see "settled" above). Switching to hci_uart
made ISO flow as ordinary H4 type-0x05 frames — 3000 ISO TX / 3024
completions over 15 s.

### Evidence for the broken accept-list connect path

Linux 7.1 connects via accept-list + passive background scan. On this
SDC/hci_uart combo, **filtered scanning reports nothing** — verified
with raw HCI (bluetoothd stopped, btmon watching):
- unfiltered passive scan: 225 peer reports / 6 s (ext adv, 1M/2M)
- filter=accept-list, AR on: 1 report / 6 s
- filter=accept-list, AR off: 0 reports / 6 s
- same test against a legacy advertiser: also 0
- legacy scan interface (0x200B/0x200C): `Command Disallowed (0x0c)`

So BlueZ Pair/Connect hung; the raw-HCI direct-connect helper
(`scripts/hci_raw_connect.py`) issues `LE Extended Create Connection`
directly and holds the socket open (kernel reaps raw-socket connections
on close).

## Key implementation details (permanent reference)

| Component | File(s) | Note |
|-----------|---------|------|
| Audio pipeline | `audio_sink.h`, `audio_i2s.c`, `audio_decode.c` | Sink interface → I2S DMA (slab allocator), LC3 decode + channel routing |
| Clock recovery | `audio_drift.c`, `audio_drift.h` | PI controller: PCLK feedforward + phase term, ppm output |
| Actuators | `audio_clock_actuator_apll.c`, `audio_clock_actuator_none.c` | APLL (nRF5340) or NONE (nRF54L15, ASRC consumes ppm) |
| Rate conversion | `audio_rate_convert.c` | Nearest-neighbor, 480→476/477 frames/block for PCLK32M mismatch |
| Timing (nRF54L15) | `audio_timing_nrf54.c` | TIMER20-vs-GRTC PCLK freq measurement, 1 s intervals |
| Central driver | `scripts/bap_central.py`, `scripts/hci_raw_connect.py` | Raw-HCI direct connect, NINO agent, auto-security via GATT |
| Dongle firmware | `dongle/hci_uart/{app,netcore}.conf`, `scripts/bin/fw-build-dongle`, `scripts/bin/fw-flash-dongle` | nRF5340DK hci_uart central, ISO capable |

## Gotchas to remember

- **`pkill -f <pattern>` kills your own shell** when the pattern appears
  in the command line. Use a bracket: `pkill -f "btmo[n] -i hci0 -w"`,
  or `pkill -x btattach`.
- **UART0 on the nRF5340DK is on ttyACM2 (USB iface 02)**, not ttyACM1.
  ttyACM1 stays silent. Verified by sending HCI Reset manually:
  ttyACM2 replies `04 0e 04 01 03 0c 00`.
- **Raw HCI RX sockets are deaf on this kernel** — observe via
  `btmon -i hci0 -w <file>`, not by reading a raw socket.
- `hcitool lescan` fails with `I/O error` — legacy scan interface not
  supported by this controller build. Not a bug; use `btmon` or
  `bap_central.py` discovery.
- `bluetoothctl scan le` in the background exits instantly and stops
  discovery — useless for scan tests.
- `btmgmt io-cap 3` and `sc on` must be re-applied after adapter power
  loss / USB re-enumeration. Put them next to `btattach` in any
  persistent setup script.
- **Use serial-mcp** for the receiver console (`/dev/ttyACM0`), not
  `stty`/`cat` — serial-mcp holds the port exclusively and survives
  USB disconnects during reset.
- **Zephyr does NOT detect devicetree pinctrl overlaps** — two
  peripherals claiming the same pin produce no compile error. Verify pin
  assignments against all enabled peripherals by decoding the resolved
  `zephyr.dts` (psel encoding: `NRF_PSEL(fun, port, pin)` =
  `(fun << 24) | ((port*32+pin) & 0x1ff)`). AGENTS.md documents a past
  P1.10/P1.11/P1.12 conflict found this way.
