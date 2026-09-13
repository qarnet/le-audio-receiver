# RH3-31 ISO RX buffer-depth trace preparation handoff

Status: software-only diagnostic preparation. This phase creates one temporary
HIL configuration fragment and proves its resolved build shape. It does not
run HIL, flash hardware, change production defaults, or claim a production
repair.

## Goal

Prepare a six-buffer nRF54L15 ISO RX trace image for one later RH3-31 physical
diagnostic. The run must answer whether extra host ISO receive headroom lets
the pending `0x206f` Command Complete pass the retained ISO packet without
changing production configuration.

## Grounding

RH3-30 is immutable evidence at:

```text
/tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace/
```

Its parsed trace is valid (`schema_version=12`, no parser or validation
errors) and records this scoped receiver-side allocation:

```text
kind=rx type=32 buffer_available=0 target_busy=0x1
```

`type=32` is `BT_BUF_ISO_IN`. The trace outcome is
`retained_iso_buffer_unavailable`; no generic target completion was received.

Installed NCS v3.3.0 proves exact behavior:

- `nrf/subsys/bluetooth/controller/hci_driver.c:522-548` allocates one
  `BT_BUF_ISO_IN` buffer before delivering an ISO HCI packet to the host.
- `hci_driver.c:690-713` retains that ISO packet and stops fetching later
  messages when allocation returns `NULL`; a later ISO-buffer free resubmits
  MPSL receive work.
- `zephyr/subsys/bluetooth/host/iso.c:54-76,637-646` defines the fixed host
  `iso_rx_pool` with exactly `CONFIG_BT_ISO_RX_BUF_COUNT` entries.
- `hci_driver.c:374-381` wakes the retained ISO retry only after a
  `BT_BUF_ISO_IN` free notification.

Current normal nRF54L15 resolution is:

```text
CONFIG_BT_ISO_RX=y
CONFIG_BT_ISO_RX_MTU=251
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

The normal ELF proves three current entries:

```text
_net_buf_iso_rx_pool       0x54 bytes
net_buf_data_iso_rx_pool   0x318 bytes
iso_info_data              0x18 bytes
```

For this NCS build each additional entry is 300 bytes: 28-byte `net_buf`,
264-byte data chunk, and 8-byte ISO info. Six entries therefore add 900 bytes
relative to normal. The candidate must build cleanly before any hardware run.

This is a bounded headroom experiment, not a root-cause claim. The current
20 ms nRF54L15 post-start I2S slab wait can delay BT RX work, but RH3-30 does
not prove that wait is sole cause of exhaustion. Do not alter the wait, I2S
logic, host driver, controller configuration, or production ISO RX count here.

## Exact implementation

Create exactly one tracked file:

```text
tests/hil/receiver-sdc-remove-iso-path-receive-disposition-rx6.conf
```

Its complete content must be:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_BT_ISO_RX_BUF_COUNT=6
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_LOG_RUNTIME_FILTERING=n
```

Do not modify the existing three-buffer fragment
`tests/hil/receiver-sdc-remove-iso-path-receive-disposition.conf`.

`EXTRA_CONF_FILE` merges after the board configuration, so the new fragment
must override the board's normal `CONFIG_BT_ISO_RX_BUF_COUNT=3` only in its
temporary trace build. It must retain receive-disposition selection and keep
the scheduler-unlock and work-state-snapshot choice options disabled.

## Scope

In scope:

- new configuration fragment named above;
- this handoff only if a factual correction is needed.

Out of scope:

- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`, `prj.conf`, Kconfig, CMake,
  C/C++ sources, test matrix, build contract, parser, runner, source image,
  documentation baselines, `STATUS.md`, and production defaults;
- HIL execution, flashing, reset, recovery, erase, serial interaction, RF
  changes, pairing, or direct probe actions;
- commit, push, merge, PR, tag, reset, stash, clean, formatting sweep, or
  unrelated worktree changes.

The worktree is intentionally dirty. Preserve all existing changes.

## Verification

Run commands sequentially from repository root. A failed command stops this
phase. The temporary trace build changes only local build output, so restore
the normal receiver build before reporting either success or failure.

Only documented project diagnostics remain non-actionable: dirty-worktree
notice, partition-manager and sysbuild deprecation notices, required SW Split
experimental notices, upstream ISO low-latency choice gap, and known nRF54L15
watchdog empty-library diagnostic. Any other warning, Kconfig assignment
diagnostic, build failure, parser failure, or test failure stops this phase.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-receive-disposition-rh3-31-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected focused results are native SDC trace suite `11/11`, RH2 Python suite
`217 passed`, and matrix checker `0 errors, 0 notes`.

Build the temporary receiver image without any hardware action:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-receive-disposition-rx6.conf"

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=6' \
  'CONFIG_BT_ISO_RX_MTU=251' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_TRACING is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -S "$trace_elf" | rg \
  '_net_buf_iso_rx_pool|net_buf_data_iso_rx_pool|iso_info_data'
```

The candidate symbol sizes must be exactly:

```text
_net_buf_iso_rx_pool       0xa8 bytes
net_buf_data_iso_rx_pool   0x630 bytes
iso_info_data              0x30 bytes
```

Do not run `scripts/check-build-contract.py` against this temporary build.
That checker correctly pins the normal production `54l15-012` value of three.

Record the new fragment SHA-256 and temporary CPUAPP/FLPR SHA-256 values for
the next execution handoff. Then restore the normal local build, without
flashing it:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
sha256sum --check <<'EOF'
07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

## Return report

Return changed paths, all test/build statuses, resolved candidate values,
candidate symbol sizes, candidate and normal image hashes, fragment hash, and
final `git status --short`. Do not run HIL or make a commit.
