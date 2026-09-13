# RH3-32 ISO RX lifetime trace execution handoff

Status: approved one-run physical diagnostic. This phase observes host ISO RX
allocation and final-release lifetime during the existing Mode B teardown
failure. It is not RH3 acceptance, a production repair, or an artifact
acceptance claim.

## Fixed execution identity

```text
run ID: rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace.junit.xml
```

Destination did not exist when this handoff was written. Recheck before the
trace build and immediately before runner execution. If either destination
exists or is a symlink, preserve it and stop. Do not substitute another ID.

## Goal and evidence boundary

RH3-30, with three ISO RX buffers, and RH3-31, with six, both recorded:

```text
kind=rx type=32 buffer_available=0
receive_disposition_outcome=retained_iso_buffer_unavailable
```

NCS v3.3.0 `nrf/subsys/bluetooth/controller/hci_driver.c:690-713` retains
that ISO packet and cannot fetch later HCI messages until a host ISO RX buffer
frees. Six buffers were not sufficient, but prior evidence did not show why
the pool stayed full.

This trace uses normal three-buffer receiver configuration. It records:

- post-session-start successful `BT_BUF_ISO_IN` allocations;
- bounded outstanding/high-water/final-unref counters;
- app ISO callbacks active at disable and at target allocation failure;
- one unavailable snapshot before the existing receive-disposition marker;
- one first-final-free marker after unavailable, if one occurs.

It does not allocate, free, delay, schedule work, change priorities, change
pool depth, patch NCS, or prove root cause. Normal packet allocation and
release generate no extra trace logs.

## Grounded inputs

Repository HEAD:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
push, alter remote state, tag, or touch unrelated files. `git diff --check` is
a hard preflight.

Current normal receiver local-build identity:

```text
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

This normal identity was reproduced by two pristine local builds with trace
disabled and `CONFIG_BT_ISO_RX_BUF_COUNT=3`. Historical `07fdb...` RH3
evidence remains immutable and is not this current worktree's local normal
build identity. Neither hash is hardware or artifact acceptance.

Source images are immutable inputs. Do not rebuild or replace them:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Trace fragment:

```text
484ed01b6323b264206430d56142cc689ffac1d662b2c358e87cbd58ebda1b15  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf
```

The trace CPUAPP hash is intentionally not predeclared. Diagnostic source is
uncommitted in existing dirty worktree. Build once, record its exact hash
before hardware, and let runner retain same image identity in `images.json`.

## Scope and ownership

In scope:

1. Sequential host preflight and trace-only receiver build.
2. Exact trace config and linker/disassembly proof.
3. One runner-owned physical row with fixed identity above.
4. Read-only immutable evidence review.
5. Local normal nRF54L15 build restoration after evidence finalization.

Out of scope:

- production source or configuration changes, pool-depth experiments,
  controller changes, NCS patches, parser/runner changes, rows, thresholds,
  test matrix, build contract, `STATUS.md`, or acceptance claims;
- source-image build or replacement, nRF5340 build, matrix, manual flash,
  reset, recover, erase, serial interaction, probe action, RF action,
  pairing, `btattach`, `bap_central.py`, or `serial-mcp`;
- evidence mutation, retry, changed run ID, output deletion, commit, push,
  merge, PR, tag, release, reset, stash, clean, or broad formatting.

Only `scripts/hil-runner.py` may change targets. It owns live identity
resolution, flashing, console capture, pairing, source control, cleanup, and
evidence finalization. Do not infer probe identity from static mappings.

No HIL execution may overlap a build. Run commands sequentially from repo
root.

## Ordered preflight

Every preflight failure is a hard stop. If trace build replaced local normal
receiver build, restore normal build and verify it before reporting failure.
Do not touch hardware after a preflight failure.

### 1. Reserve immutable output destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace.junit.xml
```

### 2. Verify host-only behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-lifetime-rh3-32-unit \
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

- native SDC trace suite: `13/13` passing;
- RH2 Python suite: `220 passed`;
- matrix checker: `0 errors, 0 notes`;
- fixture ID: `local-nrf54l15-receiver`;
- capture capability: `none`.

Only documented project diagnostics remain non-actionable: dirty-worktree
notice, partition-manager and sysbuild deprecation notices, required SW Split
experimental notices, upstream ISO low-latency choice gap, and nRF54L15
watchdog empty-library diagnostic. Any other warning, Kconfig assignment
diagnostic, build failure, parser failure, or test failure stops handoff.

### 3. Verify normal, source, and trace-fragment inputs

```bash
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
484ed01b6323b264206430d56142cc689ffac1d662b2c358e87cbd58ebda1b15  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf
EOF
```

### 4. Build and prove trace image

`fw-build-54l15` supplies CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf"

sha256sum \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y' \
  'CONFIG_BT_ISO_RX=y' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  'CONFIG_BT_ISO_RX_MTU=251' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_NET_BUF_LOG is not set' \
  '# CONFIG_TRACING is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | \
  rg '__wrap_net_buf_unref|net_buf_unref|bt_conn_reset_rx_state|__wrap_bt_buf_get_rx'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | \
  rg '__wrap_net_buf_unref'
```

`CONFIG_NET_BUF_LOG=n` is required because NCS exposes `net_buf_unref` as an
external function only in that configuration. The disassembly call from
`bt_conn_reset_rx_state` to `__wrap_net_buf_unref` is required. Symbol listing
alone is insufficient.

Do not run `scripts/check-build-contract.py` against temporary trace build. It
correctly pins production three-buffer configuration.

### 5. Recheck output ownership

Repeat all four destination checks from step 1 immediately before runner.

## One runner-owned physical execution

Use outer terminal-tool timeout `7200000` ms. Do not use shell `timeout`. Run
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. Nonzero status never permits
retry. Let runner cleanup finish. Do not manually restore hardware.

## Read-only evidence review and local normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace
```

Verify immutable evidence:

```bash
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read only `result.json`, both JUnit files, `MANIFEST.md`, `images.json`,
`sdc-hci-remove-iso-path-trace.json`, console logs, flash logs, and command
ledger. Report:

- runner status, outcome, first failed boundary, detail, cleanup failures;
- checksum/artifact count, all four image hashes, raw nRF identity evidence;
- parser schema, parser errors, validation errors, direct trace outcome;
- all lifetime arm, snapshot, and first-free records with raw lines, offsets,
  classifications, capacity, outstanding, high-water, allocations, final
  unrefs, active callbacks, and total callbacks;
- every receive-disposition marker and generic completion marker;
- every warning/error/fatal and all I2S evidence without causal overclaim.

Interpret evidence only as follows:

1. `unavailable` with `outstanding == capacity` proves tracked allocation
   occupancy reached trace capacity at that sample. It does not identify every
   owner of a buffer.
2. A nonzero `callbacks_active` at unavailable correlates pool exhaustion with
   an app callback in progress. It does not prove that callback owns a specific
   tracked buffer.
3. First-free after unavailable proves a tracked final unref occurred later in
   this trace window. It does not prove why it was delayed.
4. At disable, `final_unrefs < allocations` describes outstanding tracked
   buffers at that sampled point. It does not establish a persistent leak.
5. Missing arm, malformed trace, parser/validation failure, unexpected trace
   image identity, or unexpected warning invalidates diagnostic interpretation.

After review, restore local normal nRF54L15 build only. Do not perform later
hardware action:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

Return exact commands/results, evidence path/checksum, raw identity evidence,
trace records, normal-restoration proof, final `git status --short`, and
blockers. Do not commit.
