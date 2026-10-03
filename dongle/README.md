# XIAO nRF54L15 Linux HCI central

Second XIAO nRF54L15 serves two roles **sequentially**, not at once: standalone
HIL source or Linux HCI central. Flashing HCI firmware replaces source firmware;
restore standalone source image before source HIL runs. Receiver is separate
XIAO with I2S DAC. `dongle/hci_uart/` builds one
`xiao_nrf54l15/nrf54l15/cpuapp` SDC image, not a netcore/sysbuild pair.

The stock SAMD11 USB CDC bridge carries UART20 on P1.9 TX / P1.8 RX at
1,000,000 baud, 8N1, **no flow control**. Do not change SAMD11 firmware.
`hci_uart/src/main.c` owns asynchronous UART/H4-to-SDC transport;
`hci_uart/src/h4_rx.c` parses command, ACL and ISO traffic. Lab-only public
address `C0:AA:BB:CC:DD:EE` is in `hci_identity.h`, not a production OUI.
The adapter is session-scoped; no persistent btattach service or fixed HCI/tty
index is supported.

NCS v3.4.1 UART compatibility uses a guarded generated build-tree patch, not
an SDK-on-disk edit. Audited source SHA-256
`d68f45fbef9da8077efe6c9f94c609393fc3485bd1d486e4f710288f6d808bd3`
pins both full bounce-buffer `0xAA` initialization (original 3/8192 exhaustive
byte-value mismatches, generated 0/8192) and deferred old/new boundary
resolution. The initial deferred repair passed one six-case diagnostic, then
failed a monitored repeat. Captured RAM also showed a wrong cut when the
initial DMA pointer had advanced by seven bytes. The guarded coherent repair
checks count and DMA pointer across a 1 us quiet interval on **every** swap
and settles first-byte anomaly recovery before copying. Compiled callback
replay reproduces captured failures in earlier paths and emits the expected
bytes with this repair. Three exact-image six-case diagnostics passed 18 cases
and 216,000 source frames, with nonzero PLC (1,284 aggregate) and no HCI
hardware error `0x07`. Their normal fresh `Pair()` path recorded nine kernel
`unexpected SMP command 0x0b` messages. A separate six-case Connect-led fresh
bonding diagnostic recorded zero such warnings while retaining receiver-requested
security and successful encrypted streaming. The tracked normal CLI then passed
two more exact-image six-case runs: **12/12 cases, 144,000 source frames**, PLC
**1,005**, and zero kernel SMP `0x0b` warnings or HCI hardware errors. Clean-commit
acceptance remains pending; this 1 Mbaud/no-flow result does not claim generic
H4 reliability or completed qualification. Evidence chronology and limits:
[`pb-019-uarte-boundary-repair-results-20260928.md`](../docs/development/pb-019-uarte-boundary-repair-results-20260928.md).

## Build and session binding

From repo root in NCS v3.4.1 dev shell (from an old v3.3.0 shell, re-enter
with `env -u ZEPHYR_BASE nix develop`):

```sh
fw-build-dongle
# Image: build/dongle/zephyr/zephyr.hex
nix-nrf probes
python3 scripts/hil-runner.py create-session \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --session-id UNIQUE_SESSION_ID \
  --receiver-probe RECEIVER_PROBE_FROM_LIVE_DISCOVERY \
  --source-probe SOURCE_PROBE_FROM_LIVE_DISCOVERY \
  --session-root /tmp/opencode/hil-sessions
```

Resolve probes and USB roles live; never copy serial, tty or HCI index from a
prior run. Replace placeholders with observed probe identities. Session manifest
must be absolute external `devices.json`. Binding file is local example input;
verify it matches actual attached boards before creating session. Choose an
existing external output root and a new run ID per action. Every flash/reset/
attach rechecks DP/AP/FICR and serial identity and retains raw evidence.

```sh
fw-flash-dongle --session-manifest /absolute/external/session/devices.json \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --output-root /existing/external/output --run-id UNIQUE_FLASH_RUN
fw-reset-dongle --session-manifest /absolute/external/session/devices.json \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --output-root /existing/external/output --run-id UNIQUE_RESET_RUN
```

## Attach and stream

`fw-attach-dongle` attaches only session-bound lab adapter, checks HCI public
identity and `powered le secure-conn cis-central`, substitutes its live adapter
for `@HCI@`, and tears down owned attachment after child exits (also on child
failure/timeout). Do not attach unrelated host adapters. Run BAP child as
ordinary user, not sudo. Fresh pairing uses BlueZ discovery and `Device1.Connect()`;
receiver Security Request initiates Just Works via default NINO agent. Fresh mode
requires confirmed `Paired=True` and `Connected=True` before BAP. Bonded reconnect
still uses `--preserve-bond --peer-addr RECEIVER_ADDRESS_FROM_BOOT_LOG`, with its
existing BlueZ connection and bond. Optional raw-HCI exact-peer operation is not
the default on this controller and remains unchanged.

```sh
fw-attach-dongle --session-manifest /absolute/external/session/devices.json \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --output-root /existing/external/output --run-id UNIQUE_ATTACH_RUN \
  --timeout 180 -- python3 scripts/bap_central.py --adapter @HCI@ --duration 120
# Optional BAP flags: --mono (mono); no flag (Mode A); --stereo (Mode B).
```

For hard containment of detached sudo/root descendants, run session under a
**system-manager** transient `systemd-run` service with `User=` set to ordinary
user, `RuntimeMaxSec=`, `TimeoutStopSec=` and `KillMode=control-group`.
User-manager service or shell `timeout` alone does **not** contain detached
root descendants. Supervisor does not replace identity checks, owned adapter
cleanup, or immutable evidence finalization.

Qualification status: **Lab-qualified development fixture**. Standard
10 ms QoS RTN 5 / latency 20 ms and 40 ms presentation delay remain unchanged,
as do the frozen 90% valid / 5% PLC and zero-error limits. Original-AA and
earlier trace-image Mode A failures (HCI `0x07`, parser error, I2S reset) are
**historical failed-image evidence**, not a current coherent-v2 image result.
The coherent repair plus Connect-led **tracked normal CLI** passed those
dirty-tree diagnostics, then clean `104e67a` / NCS v3.4.1 validation passed
all six 120-second fresh/bonded mono/Mode A/Mode B cases: 72,000 writer frames,
PLC 428, zero case errors and kernel HCI/SMP alerts, with owned adapter and
process cleanup. Canonical software passed 80/80 and physical builds/contracts
passed from exact clean source. This is bounded fixture qualification, not a
generic no-flow reliability claim, public adapter recommendation, analog,
draft-release or complete standalone-source matrix acceptance. See current
[clean integration verification](../docs/development/nrf54l15-migration-verification-results-20261001.md) and the
historical `../docs/development/pb-019-hci-resume-results.md` for dated failures.
