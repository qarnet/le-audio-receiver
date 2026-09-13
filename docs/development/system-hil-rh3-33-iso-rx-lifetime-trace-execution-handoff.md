# RH3-33 ISO RX lifetime trace execution handoff

Status: approved one-run physical diagnostic. This is a new immutable sample
after RH3-32 failed before stream start. It preserves RH3-32 unchanged. It is
not an RH3 acceptance run, production repair, pool-size experiment, source
fixture repair, or root-cause claim.

## Fixed execution identity

```text
run ID: rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace.junit.xml
```

All four destination absence checks passed when this handoff was written. The
executor must repeat them before trace build and immediately before runner
execution. If any path exists or is a symlink, preserve it and stop. Do not
substitute another ID.

## Goal and evidence boundary

RH3-30 and RH3-31 reached the intended post-stream failure with same immutable
source images:

```text
kind=rx type=32 buffer_available=0
receive_disposition_outcome=retained_iso_buffer_unavailable
```

RH3-32 instead failed before `secured` and before `stream_started()`:

```text
source: connecting for 20.031 s -> teardown cause=timeout first_errno=-116
receiver: Security changed: level 1 err 9 bonded 0
receiver: Disconnected ... reason 0x3e
```

`-116` is NCS `-ETIMEDOUT`; `0x3e` is
`BT_HCI_ERR_CONN_FAIL_TO_ESTAB`; `err 9` is
`BT_SECURITY_ERR_UNSPECIFIED`. The later source `0x0406` HCI-command timeout
and assertion occurred in cleanup. This run contains no stream, no lifetime
arm, and no trace result to interpret.

RH3-30 and RH3-31 prove same source image reached `secured`, `discovered`,
`qos`, and `streaming` in fresh Mode B sessions. RH3-32 alone does not prove
trace code, source image, receiver image, RF, or pairing caused its pre-stream
failure.

Before this run, native suite now proves unarmed global `net_buf_unref`
wrapper behavior: a valid pre-session unref forwards once, decrements through
real wrapper stub, and produces no lifetime observation. This is functional
forwarding proof only, not zero timing-cost proof.

RH3-33 observes same intended lifetime question as RH3-32 with one new output
ID. It must run once only. A pre-stream failure is retained as evidence and
does not authorize a second RH3-33 invocation.

## Grounded immutable inputs

Repository HEAD:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
push, alter remote state, tag, or change unrelated files.

Source images, do not rebuild or replace:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Current normal local receiver build, verified after RH3-32 restore:

```text
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Trace fragment:

```text
484ed01b6323b264206430d56142cc689ffac1d662b2c358e87cbd58ebda1b15  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf
```

Trace receiver CPUAPP from RH3-32 is reproducible input because source/config
code is unchanged by the added native test:

```text
1ca02b604cb9baee0837aee72cb3aecb2f6f039120acea83444beafbc6864bd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

`src/sdc_hci_remove_iso_path_trace.c` is unchanged by this phase. Do not
accept a different trace image identity without stopping before hardware.

## Scope and ownership

In scope:

1. Sequential host verification and trace receiver build.
2. Trace config, image hash, linker, and disassembly proof.
3. One runner-owned fresh Mode B `48_3_1` row.
4. Read-only review of RH3-33 evidence.
5. Local normal nRF54L15 build restoration after evidence finalization.

Out of scope:

- production source/config changes; HIL test or parser changes; source-image
  rebuild/replacement; source controller timeout/assertion repair; receiver,
  source, runner, row, threshold, pool-depth, or NCS change; `STATUS.md`;
- manual flash, reset, recover, erase, serial, RF, pairing, `btattach`,
  `bap_central.py`, OpenOCD, or `serial-mcp` activity;
- artifact mutation, retry, output deletion, changed run ID, commit, push,
  merge, PR, tag, release, reset, stash, clean, or broad formatting.

Only `scripts/hil-runner.py` may change targets. It owns live identity
resolution, flashing, console capture, fresh unpair/pair setup, source control,
cleanup, and evidence finalization. No HIL execution may overlap a build.

## Ordered preflight

Run sequentially from repository root. Each preflight failure is a hard stop.
If trace build replaced normal local receiver build, restore normal and verify
it before reporting failure. Do not touch hardware after a preflight failure.

### 1. Reserve immutable output destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace.junit.xml
```

### 2. Verify host-only behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-lifetime-rh3-33-unit \
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

Expected results:

- native SDC trace suite: `14/14` passing;
- RH2 Python suite: `220 passed`;
- matrix checker: `0 errors, 0 notes`;
- fixture ID: `local-nrf54l15-receiver`;
- capture capability: `none`.

Only documented dirty-worktree, partition-manager/sysbuild deprecation, SW
Split experimental, upstream ISO choice-gap, and nRF54L15 watchdog
empty-library diagnostics are non-actionable. Any other warning, Kconfig
assignment diagnostic, test failure, or parser failure stops execution.

### 3. Verify normal, source, and fragment inputs

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

sha256sum --check <<'EOF'
1ca02b604cb9baee0837aee72cb3aecb2f6f039120acea83444beafbc6864bd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

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

`CONFIG_NET_BUF_LOG=n` is required for external `net_buf_unref` linkage. The
disassembly call from `bt_conn_reset_rx_state` to `__wrap_net_buf_unref` is
required. Symbol listing alone is insufficient.

Do not run build-contract check against temporary trace build. Production build
contract correctly pins trace disabled.

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
  --run-id rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. Nonzero status never permits a
retry. Let runner cleanup finish. Do not manually restore hardware.

## Read-only evidence review and local normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace
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

1. Runner status, outcome, first failed boundary/detail, cleanup failures,
   checksum/artifact count, all image hashes, and raw identity evidence.
2. Source states, active status, source terminal/idle, connection/security
   errors, source controller assertions, receiver security/disconnect records,
   warnings/errors/fatals, and I2S evidence.
3. Parser schema/errors/validation errors, direct trace outcome, all lifetime
   arm/snapshot/first-free records, receive-disposition records, and generic
   completion markers.
4. Whether session reached `streaming` before diagnostic trace arm.

Use only these conclusions:

- No source `streaming` state or no lifetime arm means inconclusive lifetime
  diagnostic. Do not infer retained ISO ownership or blame trace wrapper.
- `unavailable` with `outstanding == capacity` proves tracked occupancy reached
  capacity at that sample only.
- Nonzero `callbacks_active` correlates active app callback with sample; it
  does not identify a buffer owner.
- First free after unavailable proves later tracked final unref only; it does
  not explain delay.
- Missing/malformed trace records, wrong image identity, or unexpected warning
  invalidates lifetime interpretation.

After evidence review, restore local normal nRF54L15 build only. Do not change
hardware after runner:

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

## Return report

Return exact preflight/build/run commands and results; one-execution
confirmation; evidence path/checksum; raw board identity evidence; image
hashes; all trace records; exact normal-restoration proof; final
`git status --short`; no-manual-hardware/no-commit confirmation; and blocker or
smallest evidence-backed next step. Do not edit files during execution.
