# DEBUGGING SESSION — 2026-07-25 — nRF5340 hci_usb central → nRF54L15 receiver

> Session snapshot, not a permanent reference. Probe/port mappings live in
> `SESSION_USB_TABLE.md`; re-verify with `nrf-probes` and the re-verify
> commands listed there.

## Goal

Stream LE Audio from this PC to the nRF54L15 receiver using the nRF5340DK
flashed with hci_usb firmware as the Bluetooth central (`hci0`), since the
built-in Realtek "BT540" (`0b05:1bef`) is absent this session.

Starting symptom: could not reliably connect or stream. Unclear whether the
fault was the hci_usb dongle or the nRF54L15 receiver software.

## Verdict

**LE Audio transmission WORKS** — resolved end of session via hci_uart (see
"Resolution" below). Full chain: discovery → connect → JustWorks pairing
(bonded, persists) → BAP negotiation (2× SelectProperties / SetConfiguration /
Acquire, Mode A stereo) → 2× CIS established → **ISO data flows**:
3000 ISO Data TX (2 streams × 1500 frames) with matching Number of
Completed Packets, 15 s stream at exactly 100 fps, zero errors.

sigrok (earlier run) showed BCK/LRCK on the receiver exactly during the
stream window (LRCK = 48.0 kHz exact).

## Resolution: hci_usb → hci_uart

**hci_usb can never carry ISO.** Zephyr's USB device_next BT HCI class
(`~/ncs/v3.3.0/zephyr/subsys/usb/device_next/class/bt_hci.c`) has no ISO
data path: the isochronous endpoints are descriptor stubs so Linux `btusb`
binds (source comment lines 85–90: "we do not implement isochronous
endpoints handling"). ISO TX (device→host) hits a `default:` case that
drops the packet **and leaks the net_buf**; ISO RX is never armed. No
Kconfig exists because no code exists. Patching would also require remapping
to EP8+ (nRF UDC treats only EP≥8 as isochronous). Not worth it.

**Fix: use `hci_uart` instead** — the app core is then a dumb H4 pipe and
ISO passes as ordinary H4 type-0x05 packets. The sample ships
`boards/nrf5340dk_nrf5340_cpuapp.conf` with ISO central + 18 ISO TX buffers
and UART0 @ 1 Mbaud with hardware flow control (RTS/CTS are wired through
the DK's J-Link interface MCU).

Build + flash (same netcore conf as hci_usb):

```bash
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild -p auto -d /tmp/hciuart_build \
  ~/ncs/v3.3.0/zephyr/samples/bluetooth/hci_uart -- \
  -DSB_CONFIG_NETCORE_HCI_IPC=y \
  -Dhci_ipc_EXTRA_CONF_FILE=/tmp/hciipc_iso.conf

sudo openocd -f interface/jlink.cfg -c "adapter serial 001050023938" \
  -c "transport select swd" -c "adapter speed 2000" \
  -f target/nordic/nrf53.cfg -c init \
  -c "targets nrf53.cpunet" \
  -c "program /tmp/hciuart_build/hci_ipc/zephyr/zephyr.hex verify" \
  -c "targets nrf53.cpuapp" \
  -c "program /tmp/hciuart_build/hci_uart/zephyr/zephyr.hex verify" \
  -c "reset run" -c shutdown
```

**Port trap:** on this DK, UART0 is exposed on the J-Link VCOM with USB
interface number **02** = `/dev/ttyACM2` (NOT ttyACM1/iface-00 — that one
stays silent). Verified by sending HCI Reset manually: ttyACM2 replies
`04 0e 04 01 03 0c 00`.

Attach + adapter setup (btattach must stay running; re-run after
re-enumeration):

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3    # NINO — required for JustWorks receiver
sudo btmgmt --index hci0 sc on       # receiver requires SC pairing
```

Expected settings: `powered le secure-conn static-addr cis-central`.

Stream test result: `python3 scripts/bap_central.py --duration 15` →
`Done: 1500 frames in 15.00 s (100.0 fps)`, btmon shows 3000 ISO Data TX +
3024 Number of Completed Packets events. Previously the stream died at
2.23 s after exactly 3 ISO packets (SDC default ISO TX buffer count) with
no completions ever returned.

## Open issue (historical): ISO data stalled after 3 packets on hci_usb

**SOLVED — not a flow-control or buffer-count bug.** The 3-packet stall was
the symptom of hci_usb having no ISO USB path at all: BlueZ consumed its 3
SDC TX credits writing into the void (ISO OUT endpoint never armed), and no
Number of Completed Packets could ever be generated because no ISO data
ever reached the SDC. See "Resolution" above. The earlier suspects below
were ruled out:

- ~~hci_usb ISO forwarding~~ → confirmed as the root cause (code absent)
- ~~SDC netcore completions lost through hci_ipc~~ → ipc.c forwards ISO
  H4 both directions unconditionally; fine
- ~~ISO TX buffer count 3 too small~~ → count was a symptom, not the cause

## Root causes found and fixes applied

### Original dongle firmware: hci_usb (SUPERSEDED — now hci_uart, see Resolution)

The dongle carried a vanilla Zephyr hci_usb build (SW-split link layer),
possibly without a network-core image at all. It enumerated and scanned but
connections never completed. Reflashed from NCS v3.3.0:

```bash
# App core: zephyr/samples/bluetooth/hci_usb for nrf5340dk/nrf5340/cpuapp
# Net core: hci_ipc with SoftDevice Controller
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild -p auto -d /tmp/hciusb_build \
  ~/ncs/v3.3.0/zephyr/samples/bluetooth/hci_usb -- \
  -DSB_CONFIG_NETCORE_HCI_IPC=y \
  -Dhci_usb_EXTRA_CONF_FILE=/tmp/hciusb_iso.conf \
  -Dhci_ipc_EXTRA_CONF_FILE=/tmp/hciipc_iso.conf
```

`/tmp/hciusb_iso.conf` (app core):
```
CONFIG_BT_ISO_CENTRAL=y
```

`/tmp/hciipc_iso.conf` (net core):
```
CONFIG_BT_ISO_CENTRAL=y
CONFIG_BT_MAX_CONN=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2
CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT=1
CONFIG_BT_EXT_ADV=y
CONFIG_BT_CTLR_PHY_CODED=n
CONFIG_BT_CTLR_PRIVACY=n
```

Config rationale — each symbol maps to an observed failure:

| Config | Without it |
|--------|-----------|
| `CONFIG_BT_ISO_CENTRAL=y` (app + netcore) | `cis-central` missing from btmgmt settings; BlueZ cannot do LE Audio at all. Netcore also needs `BT_MAX_CONN=2` / `CONN_ISO_STREAMS=2` or RAM overflows by ~20 KB (defaults balloon to 16). |
| `CONFIG_BT_EXT_ADV=y` | Receiver advertises with `BT_LE_ADV_OPT_EXT_ADV`; SDC without ext-adv never reports it (zero adv reports from `DB:A6:0C:05:A2:AA` while 200+ from others). |
| `CONFIG_BT_CTLR_PHY_CODED=n` | Linux 7.1 sends extended scan params with PHYs=0x05 (1M + Coded, **active** on both). SDC rejects active scanning on Coded with `0x11 Unsupported Feature` and the entire discovery fails. Zephyr SW-split LL was lenient about this. |
| `CONFIG_BT_CTLR_PRIVACY=n` | Removes LL privacy so the kernel doesn't enable address resolution. Part of making the connect path as simple as possible. |

Flash both cores via the DK's onboard J-Link:

```bash
sudo openocd -f interface/jlink.cfg -c "adapter serial 001050023938" \
  -c "transport select swd" -c "adapter speed 2000" \
  -f target/nordic/nrf53.cfg -c init \
  -c "targets nrf53.cpunet" \
  -c "program /tmp/hciusb_build/hci_ipc/zephyr/zephyr.hex verify" \
  -c "targets nrf53.cpuapp" \
  -c "program /tmp/hciusb_build/hci_usb/zephyr/zephyr.hex verify" \
  -c "reset run" -c shutdown
```

### Kernel/BlueZ vs SDC: broken accept-list connect path

Linux 7.1 connects to LE devices via accept-list + passive background scan
(even with LL privacy off). On this SDC/hci_usb combo, **filtered scanning
reports nothing** — verified with raw HCI (bluetoothd stopped, btmon
watching):

- unfiltered passive scan: 225 peer reports / 6 s (ext adv, 1M primary / 2M secondary)
- filter=accept-list, AR on: 1 report / 6 s
- filter=accept-list, AR off: 0 reports / 6 s
- same test against a **legacy** advertiser (LHB Valve tracker): also 0
- legacy scan interface (0x200B/0x200C): `Command Disallowed (0x0c)`

Result: every BlueZ Pair/Connect hung ~90 s and was cancelled. Receiver
console proved direct connection works fine, so the workaround is a
raw-HCI direct `LE Extended Create Connection`.

**Workaround (in repo):** `scripts/hci_raw_connect.py` issues the direct
connect and holds the raw socket open (kernel reaps raw-socket-created
connections when the socket closes). `scripts/bap_central.py` spawns it
before pairing and waits for `Device1.Connected`.

### Pairing chain (three separate failures)

1. **MITM rejected.** BlueZ's SMP Pairing Request demanded MITM
   (`KeyboardDisplay`, authreq 0x0d). Receiver is JustWorks-only
   (`CONFIG_BT_SMP_ENFORCE_MITM=n`) → `Pairing Failed`.
   Fix: agent capability `NoInputNoOutput` in `bap_central.py`, plus
   `sudo btmgmt -i hci0 io-cap 3` so the adapter-level IO capability is
   also NINO (kernel uses the adapter-level value for auto-security SMP).

2. **Kernel mgmt quirk on raw-created connections.** `MGMT Pair Device`
   on an already-connected link completes **instantly** (~6 µs) with
   Success. BlueZ's `pair_device_complete` treats that as pairing done and
   clears `device->bonding`; when the SMP User Confirmation then arrives
   (CVE-2020-26555: kernels always confirm JustWorks), BlueZ auto-rejects
   it (no bonding context, `jw_repairing=NEVER`) →
   `Numeric comparison failed` → `DHKey check failed` → disconnect.
   Verified in BlueZ 5.86 debug log (`pair_device_complete() Success`,
   then `btd_adapter_confirm_reply() success 0`).
   Fix: `bap_central.py` no longer calls `Device1.Pair()`. It relies on
   BlueZ auto-security (GATT access to the encrypted PACS characteristics
   triggers kernel SMP directly, no mgmt Pair). The NINO agent's
   `RequestAuthorization` accepts the confirmation. Result:
   `Pairing complete, bonded: 1` on the receiver, bond persists in ZMS.

3. **Stale Realtek-era bond.** `/var/lib/bluetooth/A0:AD:9F:7B:C7:95/DB:A6:0C:05:A2:AA/`
   held an LTK from an earlier session (the old BT540 adapter). BlueZ
   loaded it, elevated security with a key the receiver no longer had
   (its settings were wiped) → repeated auth failure loop. Fix: deleted
   the stale bond dir, power-cycled hci0 (`btmgmt power off/on`) to flush
   the kernel-side key store (bluetoothd restart alone does not clear it).

### Receiver panic on disconnect (was bricking every test)

After any stream, the next disconnect caused:

```
ASSERTION FAIL [p_instance && (p_instance->cb.state != NRFX_DRV_STATE_UNINITIALIZED)]
  @ modules/hal/nordic/nrfx/drivers/src/nrfx_i2s.c:420
>>> ZEPHYR FATAL ERROR 4: Kernel panic
```

Chain: DMA underrun → Zephyr `i2s_nrfx` driver sets `I2S_STATE_ERROR` and
`nrfx_i2s_uninit()`s the instance on transfer end → app's `started` flag
still true → next disconnect calls `audio_sink_stop()` → `TRIGGER_DROP` →
driver calls `nrfx_i2s_stop()` on the dead instance → NRFX_ASSERT → panic
→ board halted until manual reset. This is likely the main historical
"streams then dies and nothing works afterwards" cause.

Fix (`src/audio_i2s.c`): `audio_sink_stop()` now issues
`I2S_TRIGGER_PREPARE` (legal from ERROR state, resets driver to READY
without touching nrfx) before `I2S_TRIGGER_DROP`. PREPARE returns -EIO when
not in ERROR — harmless.

### Receiver SMP debug build — REMOVE BEFORE PRODUCTION

The receiver currently runs a build with `CONFIG_BT_SMP_LOG_LEVEL_DBG=y`
(built via `fw-build-54l15 -DCONFIG_BT_SMP_LOG_LEVEL_DBG=y`, not committed
to prj.conf). It prints private keys. Rebuild without it when done.

## Open issue: ISO data stalls after 3 packets

Symptom: script streams at exactly 100 fps for 2.23 s (223 frames), then
`os.write` fails with EAGAIN and BlueZ disables both streams.

Evidence:

- CIS 0 and CIS 1 both `LE Connected Isochronous Stream Established`,
  data paths set up (HCI, host→controller) — btmon.
- Only **3** `ISO Data TX` packets ever cross HCI (handles 10 and 11) —
  exactly the SDC default ISO TX HCI buffer count.
- The controller never sends `Number of Completed Packets` for the ISO
  handles → host flow control blocks → BlueZ's userspace buffer fills
  (~445 writes ≈ 2.2 s) → EAGAIN.
- Receiver never receives a valid SDU: its DIN/SDOUT is flat on the
  logic analyzer (only silence/packet-repeat fallback), and it logs
  `i2s_nrfx: Next buffers not supplied on time` when the streams are
  disabled.

Next suspects, in order:

1. Zephyr `hci_usb` (USB `device_next` stack) ISO forwarding —
   `BT_HCI_H4_ISO` handling in
   `~/ncs/v3.3.0/zephyr/subsys/usb/device_next/class/bt_hci.c`; verify the
   ISO USB endpoint actually moves data with `USB_DEVICE_STACK_NEXT=y`.
2. SDC netcore ISO TX completed-events not reaching the host through
   hci_ipc.
3. `CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT` (=3) too small — but
   raising it only delays the stall if completions never arrive.

## Working recipe (current, with hci_uart dongle)

See "Resolution" above for build/flash/attach commands. Summary per session:

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3
sudo btmgmt --index hci0 sc on
python3 scripts/bap_central.py --duration 15
```

Expected: `ACL link up` → `ServicesResolved` → 2× SelectProperties →
2× SetConfiguration → 2× Acquired → `Streaming 1000 Hz sine` →
`Done: 1500 frames in 15.00 s (100.0 fps)`.

Zombie connections after crashed runs are no longer an issue (hci_uart
firmware keeps no connection state across adapter re-init; worst case
re-attach btattach).

Receiver console during a good run: `Pairing complete, bonded: 1`,
2× `ASE Config`, `LC3 decoder[0/1]`, `Stream[x] started`,
`audio_i2s: I2S DMA started`.

## Files changed this session

| File | Change |
|------|--------|
| `scripts/bap_central.py` | NINO agent; raw-HCI connect step before pairing; explicit `Pair()` removed (auto-security via GATT); stale-connection disconnect now falls through to raw reconnect; `import subprocess` |
| `scripts/hci_raw_connect.py` | **New.** Raw-HCI direct `LE Extended Create Connection` helper; holds socket open |
| `src/audio_i2s.c` | `audio_sink_stop()` sends `TRIGGER_PREPARE` before `TRIGGER_DROP` (panic fix) |

Dongle firmware is **not** in this repo — rebuild recipe above
(`/tmp/hciusb_build`, `/tmp/hciusb_iso.conf`, `/tmp/hciipc_iso.conf`).
Consider adding it as a small west project (e.g. `dongle/hci_usb/` with
the two conf fragments) so it doesn't get lost.

## Hardware notes

- hci0 = nRF5340DK running **hci_uart** on `/dev/ttyACM2` (J-Link VCOM,
  USB iface 02!) @ 1 Mbaud, attached via btattach. BD address
  `00:00:00:00:00:00`, BlueZ assigns static random `E3:C4:1A:96:D7:D2`.
- The hci_usb firmware (2fe3:000b on USB path 1-7) is no longer flashed.
  If reverted to hci_usb, ISO breaks again — hci_usb has no ISO path.
- Kernel raw HCI sockets on Linux 7.1: sending works, **receiving is
  deaf** — use `btmon` (monitor channel) to observe results.
- `hcitool lescan` fails (`Set scan parameters failed: I/O error`) —
  legacy scan interface not supported by this controller build. Not a bug.
- `bluetoothctl scan le` in the background exits instantly and stops
  discovery after ~10 ms — useless for scan tests; use `btmon` + raw scan
  or `bap_central.py` discovery instead.
- `pkill -f <pattern>` kills your own shell when the pattern appears in
  the command line — use a bracket (`pkill -f "btmo[n] ..."`) or
  `pkill -x`.
