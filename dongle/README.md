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

## Build and session binding

From repo root in NCS v3.3.0 dev shell:

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
ordinary user, not sudo. Fresh pairing uses normal BlueZ discovery; for bonded
reconnect pass `--preserve-bond --peer-addr RECEIVER_ADDRESS_FROM_BOOT_LOG`.
Do not use raw-HCI exact-peer connection as default on this controller.

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

Qualification status: **Prototype / qualification incomplete**. Standard
10 ms QoS RTN 5 / latency 20 ms, unchanged 40 ms presentation delay.
Earlier six 120-second mono/Mode A/Mode B fresh and bonded rows met frozen
90% valid / 5% PLC limits with **nonzero** loss/PLC. Later final production-
image repeat failed during Mode A after mono/reconnect passed: kernel HCI
hardware error `0x07`, H4 parser `-EPROTO`, I2S underrun, stream reset and
controller command timeouts. Root cause unresolved. External RAM-trace six-
case pass perturbs timing and does not qualify the production image. See
`docs/development/pb-019-hci-resume-results.md` and
`docs/development/nrf54l15-only-continuation-20260925.md` for exact evidence.
No lab-qualification, public adapter recommendation, clean-commit release or
full migration acceptance is claimed. Continue controlled boundary diagnosis
before claiming restored qualification.
