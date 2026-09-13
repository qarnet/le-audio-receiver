# RH3-30 SDC HCI receive-disposition trace execution handoff

Status: one-use physical diagnostic execution handoff. Run one fresh immutable
HIL observation only. This handoff authorizes no retry, child run, replacement
run, matrix, production fix, or source-image rebuild.

## Fixed execution identity

```text
run ID: rh3-20260824-30-sdc-hci-receive-disposition-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace.junit.xml
```

The destination checks passed when this handoff was written. Repeat them before
the trace build and immediately before the runner. If any destination exists or
is a symlink, preserve it and stop. Never delete, overwrite, rename, or choose
a substitute ID.

## Goal and evidence boundary

RH3-29 is immutable failed evidence at:

```text
/tmp/opencode/hil-runs/rh3-20260823-29-sdc-hci-yield-switch-trace/
```

It proved that MPSL switched in twice during the sender's first post-unlock
yield while target receive work cleared. It did not observe
`hci_internal_msg_get` or command completion. This run distinguishes only these
target-owned receiver paths:

1. retained ISO message cannot obtain `BT_BUF_ISO_IN`;
2. HCI fetch returns an error before allocation;
3. fetched EVT, DATA, or ISO message cannot obtain its matching host buffer;
4. normal fetched and delivered target completion.

The temporary trace observes one first receive-work transaction only. It does
not allocate, wait, submit work, change a scheduler priority, change a buffer
count, change an HCI timeout, or patch NCS. Trace records are evidence, not a
root-cause conclusion or a production-fix acceptance.

`fetched_buffer_unavailable` may contain one zero-status generic target
completion marker. If present, it must be after scoped fetch return and before
the unavailable allocation. That records controller event retrieval before host
delivery failed. It must not be reported as successful host command delivery.

## Grounded inputs

Repository `HEAD` when this handoff was written:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

The worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
change remote state, tag, or alter unrelated files. `git diff --check` is a hard
preflight.

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

Trace fragment:

```text
32c8ff503a1bf84d1c6120d31a05f76f0d0ccd2668e93e238ae605eaf6f48660  tests/hil/receiver-sdc-remove-iso-path-receive-disposition.conf
```

The trace receiver CPUAPP hash is deliberately not predeclared. This reviewed
trace code is uncommitted in the existing dirty worktree. Build it once in this
handoff, record its SHA256 before hardware, and let the runner retain that exact
image hash in immutable `images.json`. The source images above remain fixed.

## Scope and ownership

In scope:

- host preflight and trace-only receiver build;
- one runner-owned RH3 row;
- read-only review of finalized evidence;
- local normal nRF54L15 build restoration after cleanup.

Out of scope:

- any production source or configuration change;
- source image build, source image replacement, nRF5340 build, or matrix;
- manual serial, `serial-mcp`, OpenOCD, reset, flash, probe, HCI, BlueZ, RF,
  pairing, source streaming, or receiver-shell interaction;
- shell `log` commands or `CONFIG_LOG_CMDS`;
- evidence mutation, `STATUS.md`, documentation edits, commit, staging, push,
  merge, tag, release, reset, stash, clean, or restore.

`scripts/hil-runner.py` exclusively owns fixture lock, identity resolution,
serial capture, flash/reset, pairing, source streaming, cleanup, and evidence
finalization. It must resolve current board identity and retain raw evidence.
Do not infer probe identity from a static mapping.

No HIL test may run concurrently with a build. Run all commands sequentially
from repository root.

## Ordered preflight

Every preflight failure is a hard stop. If a trace build replaced the normal
local receiver build, restore and verify the normal build before reporting the
failure. Do not touch hardware after a preflight failure.

### 1. Reserve immutable destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace.junit.xml
```

### 2. Host checks and fixture validation

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-receive-disposition-rh3-30-unit \
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

- native SDC trace suite: 11/11 passing;
- RH2 Python suite: 217 passing;
- matrix checker: 0 errors and 0 notes;
- fixture ID: `local-nrf54l15-receiver`;
- capture capability: `none`.

Only documented project diagnostics remain non-actionable: dirty-worktree
notice, partition-manager and sysbuild deprecation notices, required SW Split
experimental notices, upstream ISO low-latency choice gap, and known nRF54L15
watchdog empty-library diagnostic. Any other warning, Kconfig assignment
diagnostic, build failure, parser failure, or test failure stops this handoff.

### 3. Verify normal and source inputs

```bash
sha256sum --check <<'EOF'
07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
32c8ff503a1bf84d1c6120d31a05f76f0d0ccd2668e93e238ae605eaf6f48660  tests/hil/receiver-sdc-remove-iso-path-receive-disposition.conf
EOF
```

### 4. Build and prove trace image

`fw-build-54l15` supplies its own CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-receive-disposition.conf"

sha256sum \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
test -f "$trace_config"
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_TRACING is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | rg '__wrap_hci_internal_msg_get|__wrap_bt_buf_get_evt|__wrap_bt_buf_get_rx'
"$toolchain_objdump" -d --disassemble=hci_driver_receive_process "$trace_elf" | \
  rg '__wrap_hci_internal_msg_get|__wrap_bt_buf_get_evt|__wrap_bt_buf_get_rx'
```

The disassembly proof is required. A wrapper symbol listing alone is not enough.
Do not add a wrapper for `hci_driver_receive_process`: its same-object work
handler call is not an eligible GNU ld `--wrap` reference.

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
  --run-id rh3-20260824-30-sdc-hci-receive-disposition-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status 0, 1, or 130 is retained evidence. A nonzero status never permits
a retry. Let runner cleanup finish. Do not manually restore hardware.

## Read-only evidence review and normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260824-30-sdc-hci-receive-disposition-trace
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
- generic target completion presence, status, and order relative to scoped
  fetch return and allocation;
- all warnings/errors, including later fatal or I2S evidence, without causal
  overclaim.

Accepted trace outcome names are `retained_iso_buffer_unavailable`,
`fetch_error`, and `fetched_buffer_unavailable`. These outcomes validate trace
syntax and scope only. They do not change RH3 acceptance or diagnose why a
pool was unavailable.

After evidence review, restore only local normal nRF54L15 build:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx '# CONFIG_TRACING is not set' "$normal_config"
sha256sum --check <<'EOF'
07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

No hardware action follows restoration. Report evidence and stop.
