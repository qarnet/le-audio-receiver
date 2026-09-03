# RH3-31 six-buffer ISO RX trace execution handoff

Status: approved one-run physical diagnostic. This phase uses a temporary
six-buffer receiver trace image to test host ISO RX headroom. It is not RH3
acceptance, does not change production defaults, and does not authorize a
production repair.

## Goal

Run one fresh Mode B 7.5 ms trace with
`CONFIG_BT_ISO_RX_BUF_COUNT=6`. Determine whether the first scoped pending ISO
message can obtain a host ISO RX buffer during `0x206f` teardown, and retain
the resulting command-path and end-of-session evidence.

## Grounded inputs

Repository `HEAD` when this handoff was written:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

The worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
change remote state, tag, or alter unrelated files. `git diff --check` is a hard
preflight.

RH3-30 immutable evidence proved the prior three-buffer trace parked on a
retained `BT_BUF_ISO_IN` packet with `buffer_available=0`. NCS v3.3.0
`hci_driver.c:690-713` retains that packet and does not fetch later messages
until an ISO host buffer frees. This run tests only extra headroom.

Normal receiver image baseline, restored before this handoff:

```text
07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Existing source images are immutable inputs. Do not rebuild or replace them:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

The reviewed temporary candidate has deterministic identities:

```text
33e61b0a5fa90b9b6b0d8e1c0e184f3c54f66c386a6318d8219a439f9d9c1187  tests/hil/receiver-sdc-remove-iso-path-receive-disposition-rx6.conf
6b7db600ab63047588e18f0543a4b04ac2724d5c698efe0eb4468209f0255795  candidate receiver CPUAPP
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  candidate receiver FLPR
```

The candidate fragment is HIL-only. It selects receive-disposition trace and
overrides the normal nRF54L15 count from three to six. It must not change
`CONFIG_BT_ISO_RX_MTU=251`, enable tracing, or select snapshot/scheduler
probes. The current build contract correctly pins production count three, so
do not run it against this candidate build.

## Scope

In scope:

1. Sequential host preflight and source-image verification.
2. Build the exact six-buffer trace receiver image and prove its config,
   wrapper linkage, ISO pool sizes, and hashes.
3. One runner-owned physical row with fixed ID below.
4. Check immutable evidence and restore normal local nRF54L15 build.

Out of scope:

- production configuration, C/C++ source, Kconfig, CMake, parser, runner,
  source fixture, rows, thresholds, test matrix, build contract, docs
  baselines, `STATUS.md`, or acceptance claims;
- manual flash, reset, recover, erase, serial read/write, probe action, RF
  change, pairing, `btattach`, `bap_central.py`, `serial-mcp`, or alternate
  central;
- source-image rebuild, retry, changed run ID, output deletion, commit, push,
  merge, PR, tag, release, reset, stash, clean, or broad formatting.

Only `scripts/hil-runner.py` may change targets. It owns probe identity,
flashing, console lifecycle, pairing, source control, and cleanup. Do not
infer probe identity from a static mapping.

No HIL test may run concurrently with a build. Run all commands sequentially
from repository root.

## Ordered preflight

Every preflight failure is a hard stop. If a trace build replaced the normal
local receiver build, restore and verify the normal build before reporting the
failure. Do not touch hardware after a preflight failure.

### 1. Reserve immutable destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260824-31-sdc-hci-iso-rx6-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260824-31-sdc-hci-iso-rx6-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260824-31-sdc-hci-iso-rx6-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260824-31-sdc-hci-iso-rx6-trace.junit.xml
```

### 2. Host checks and fixture validation

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-receive-disposition-rh3-31-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py

python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
git status --short

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected focused results:

- native SDC trace suite: `11/11` passing;
- RH2 Python suite: `217 passed`;
- matrix checker: `0 errors, 0 notes`;
- fixture ID: `local-nrf54l15-receiver`;
- capture capability: `none`.

Only documented project diagnostics remain non-actionable: dirty-worktree
notice, partition-manager and sysbuild deprecation notices, required SW Split
experimental notices, upstream ISO low-latency choice gap, and known nRF54L15
watchdog empty-library diagnostic. Any other warning, Kconfig assignment
diagnostic, build failure, parser failure, or test failure stops this handoff.

### 3. Verify normal, source, and candidate inputs

```bash
sha256sum --check <<'EOF'
07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
33e61b0a5fa90b9b6b0d8e1c0e184f3c54f66c386a6318d8219a439f9d9c1187  tests/hil/receiver-sdc-remove-iso-path-receive-disposition-rx6.conf
EOF
```

### 4. Build and prove six-buffer trace image

`fw-build-54l15` supplies its own CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-receive-disposition-rx6.conf"

sha256sum --check <<'EOF'
6b7db600ab63047588e18f0543a4b04ac2724d5c698efe0eb4468209f0255795  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

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
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -S "$trace_elf" | rg '_net_buf_iso_rx_pool|net_buf_data_iso_rx_pool|iso_info_data'
"$toolchain_nm" -A "$trace_elf" | rg '__wrap_hci_internal_msg_get|__wrap_bt_buf_get_evt|__wrap_bt_buf_get_rx'
"$toolchain_objdump" -d --disassemble=hci_driver_receive_process "$trace_elf" | \
  rg '__wrap_hci_internal_msg_get|__wrap_bt_buf_get_evt|__wrap_bt_buf_get_rx'
```

Required ISO pool sizes are `_net_buf_iso_rx_pool=0xa8`,
`net_buf_data_iso_rx_pool=0x630`, and `iso_info_data=0x30`. The disassembly
proof is required. A wrapper symbol listing alone is not enough. Do not add a
wrapper for `hci_driver_receive_process`: its same-object work-handler call is
not an eligible GNU ld `--wrap` reference.

Do not run `scripts/check-build-contract.py` against this temporary build. It
correctly expects normal production ISO RX count three.

### 5. Recheck output ownership

Repeat all four checks from step 1 immediately before the one runner command.

## One runner-owned physical execution

Use an outer terminal-tool timeout of 7200000 ms. Do not use shell `timeout`.
Run exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260824-31-sdc-hci-iso-rx6-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260824-31-sdc-hci-iso-rx6-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status 0, 1, or 130 is retained evidence. A nonzero status never permits
a retry. Let runner cleanup finish. Do not manually restore hardware.

## Read-only evidence review and normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260824-31-sdc-hci-iso-rx6-trace
```

Verify evidence from inside its own directory:

```bash
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read only `result.json`, both JUnit files, `MANIFEST.md`, `images.json`,
`sdc-hci-remove-iso-path-trace.json`, receiver/source console logs, flash logs,
and command ledger. Report:

- runner status, outcome, first failed boundary, detail, cleanup failures;
- checksum/artifact counts and exact four image hashes;
- raw nRF probe identity evidence recorded by runner;
- trace parser and validation errors;
- every receive-disposition marker, raw line, offset, classification, and
  parsed outcome;
- generic target completion presence, status, and order, if retained;
- all warnings/errors, including later fatal or I2S evidence, without causal
  overclaim.

Evidence interpretation is bounded:

1. A first scoped `kind=rx type=32 buffer_available=1`, no
   `retained_iso_buffer_unavailable`, a complete receiver summary, and no
   `0x206f` timeout/fatal supports the six-buffer headroom hypothesis for this
   row. It is still not acceptance or a production-fix claim.
2. `buffer_available=1` with another failure proves only the immediate
   allocation changed. Do not call it a repair.
3. Another `retained_iso_buffer_unavailable` disproves sufficiency of six
   buffers for this observed transaction.
4. Missing trace arm, parser/validation errors, or a changed image hash makes
   the diagnostic invalid.

The scoped trace observes only its first retained receive path. Absence of a
generic completion marker after a successful first ISO allocation does not by
itself prove the command failed. Use runner result and timeout/fatal evidence
for the later end-to-end result.

After evidence review, restore only local normal nRF54L15 build:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx '# CONFIG_TRACING is not set' "$normal_config"
sha256sum --check <<'EOF'
07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

No hardware action follows restoration. Report evidence and stop.
