# Dongle firmware — nRF5340DK hci_uart LE Audio central

This directory holds the configuration for the **Bluetooth central** used
to stream LE Audio from the Linux PC to the receiver: an nRF5340DK
flashed with Zephyr's `hci_uart` sample, tuned as an LE Audio central.

The app-core firmware is the **upstream Zephyr hci_uart sample** (a plain
H:4 pipe over UART0 @ 1 Mbaud with hardware flow control). The
SoftDevice Controller runs on the network core via `hci_ipc`. The two
`.conf` fragments in this directory pin the ISO/controller symbols on
top of the sample's own board configuration so the build is reproducible
from this repo.

## Why hci_uart and not hci_usb

`hci_usb` (Zephyr's USB device_next BT class,
`subsys/usb/device_next/class/bt_hci.c`) has **no ISO data path** — its
isochronous USB endpoints are descriptor stubs that exist only so Linux
`btusb` binds (source comment: "we do not implement isochronous
endpoints handling"). ISO TX from the host is dropped (and leaks a
net_buf); ISO RX is never armed. No Kconfig enables it because there is
no code. The legacy USB BT class (`subsys/usb/device/class/bluetooth.c`)
has no isochronous endpoints at all either. So **no Zephyr USB BT
transport can carry LE Audio ISO in v3.3.0.**

`hci_uart` sidesteps this: the app core is a dumb H:4 byte pipe, and ISO
packets pass through as ordinary H:4 type-0x05 frames. The IPC layer
(`drivers/bluetooth/hci/ipc.c`) and the SDC netcore are both fully
ISO-capable, so once the USB wall is removed the whole pipeline works.

Verified: 2× CIS stereo, 3000 ISO Data TX over 15 s, matching Number of
Completed Packets, zero stalls. See `STATUS.md` and
`SESSION_DEBUG_2026-07-25.md`.

## Files

| File | Purpose |
|------|---------|
| `hci_uart/app.conf` | App-core (cpuapp) fragment — re-asserts `BT_ISO_CENTRAL` + ISO buffer counts + ext adv |
| `hci_uart/netcore.conf` | Net-core (cpunet / hci_ipc) fragment — SDC tuning: ISO central, 2 conns / 2 CISes, ext adv, no Coded PHY, no privacy |

## Build + flash

From the repo root, inside the dev shell (`direnv allow` or `nix develop`):

```bash
fw-build-dongle     # builds into build/dongle/
fw-flash-dongle     # flashes both cores via the DK's onboard J-Link
```

The build points west at `$ZEPHYR_BASE/samples/bluetooth/hci_uart`
(resolved from the dev shell, so it tracks the active NCS version) and
applies both conf fragments via `EXTRA_CONF_FILE`.

## Attach to Linux (BlueZ)

The DK's J-Link interface MCU exposes UART0 as a CDC ACM port. On this
bench it enumerates as **`/dev/ttyACM2`** (USB interface 02 — not
ttyACM1, which stays silent). Verify by sending an HCI Reset manually if
in doubt:

```bash
python3 - <<'EOF'
import os, termios, time
fd = os.open("/dev/ttyACM2", os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd); a[2] = termios.B1000000 | termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B1000000; termios.tcsetattr(fd, termios.TCSANOW, a)
os.write(fd, bytes([0x01,0x03,0x0c,0x00])); time.sleep(0.5)
print(os.read(fd, 64).hex() or "<no reply — wrong port>")
EOF
# expect: 040e0401030c00
```

Attach + set up the adapter:

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3     # NoInputNoOutput — required for JustWorks receiver
sudo btmgmt --index hci0 sc on        # receiver requires Secure Connections
# verify: settings should include "powered le secure-conn static-addr cis-central"
```

`btattach` is not persistent — it dies with the session. Re-run the
`btattach` + `btmgmt` lines after any DK re-enumeration or reboot (a
udev rule / systemd unit would make this automatic).

## Stream

```bash
python3 scripts/bap_central.py --duration 15
```

Expected: `ACL link up` → `ServicesResolved` → 2× SelectProperties →
2× SetConfiguration → 2× Acquired → `Streaming 1000 Hz sine` →
`Done: 1500 frames in 15.00 s (100.0 fps)`.