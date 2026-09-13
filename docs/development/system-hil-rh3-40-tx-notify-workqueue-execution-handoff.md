# RH3-40 TX-notify workqueue physical execution handoff

## Status

Approved single physical diagnostic execution. Run exactly one runner-owned fresh
Mode B H40 trace image, preserve all immutable evidence, then restore only the
local normal nRF54L15 build. This run is diagnostic only. It makes no production
change and cannot establish a root cause, production safety, or a repair.

Repository `HEAD` is:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

The worktree is intentionally dirty. Preserve every existing tracked and
untracked change. Create no other tracked change.

## Goal and bounded hypothesis

RH3-39 retained trace observations through the bounded wait-cycle window and
observed the pending-thread marker without an observed flusher-semaphore give
before its snapshot. H40 moves TX-notify work off `sysworkq` with Zephyr's
existing private TX-notify workqueue, while retaining every RH3-39 trace
observation. The run tests whether this bounded configuration avoids the same
wait-cycle behavior in the exact fresh Mode B row.

H40 can support or refute this experiment only. A successful row means that this
one configuration completed the one tested execution without the observed row
failure. It does not establish production safety, prove a root cause, prove that
the workqueue is sufficient under other load, or authorize production adoption.
A failure remains diagnostic evidence, not permission to repair or retry.

## Exact execution identity and fixed inputs

Use these values without substitution:

```text
run ID:          rh3-20260826-40-sdc-iso-tx-notify-wq
run ID length:   36
output root:     /tmp/opencode/hil-runs
output directory:/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
external JUnit:  /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
row:             rh3.fresh_mode_b_48_3_1
```

The H40 output directory and external JUnit path are fixed and currently unused.
If either destination exists or is a symlink, preserve it and stop. Never reuse,
overwrite, delete, or replace either destination. No retry is allowed after any
runner result.

H40 trace fragment:

```text
tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf
SHA256: b8b3c47a58cd6f71714999eedac83ec27e6e114c0ec242aeeda8cc44537aa32a
```

Expected H40 trace image hashes:

```text
receiver CPUAPP: c487f021a0ff29076e0a1b312a6826b4f2007623098f22c34fa19358f1bedb52
receiver FLPR:   45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

Expected source image hashes, unchanged and not rebuilt or replaced by this
execution:

```text
source app:      f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source cpunet:   4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48
```

Expected restored normal receiver image hashes:

```text
normal CPUAPP:   e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f
normal FLPR:     45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
source app:      f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source cpunet:   4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48
```

The trace CPUAPP links at `163736 / 163840` bytes. Its RAM margin is 104 bytes.
This is a trace-image fit risk. Do not change stack size, priority, pool depth,
source, or any other setting if fit or warning proof differs.

## Scope

In scope, in this order:

1. Read-only repository/status inspection and the ordered software preflight.
2. One exact H40 trace receiver build.
3. One exact runner-owned H40 physical command.
4. Read-only review and integrity verification of newly finalized evidence.
5. One local normal nRF54L15 build restoration and hash/config proof.

Out of scope:

- Any production source or configuration change, including `prj.conf`, board
  configuration, `CONFIG_BT_CONN_TX_NOTIFY_WQ`, queue priority, queue stack size,
  `CONFIG_BT_ISO_RX_BUF_COUNT`, source fixture, controller, or NCS source.
- Any change to HIL runner, parser, tests, rows, thresholds, matrix,
  build-contract, coverage, `STATUS.md`, or public documentation.
- Source-image rebuild or replacement, nRF5340 build, or any second receiver
  build outside the exact H40 trace build and required normal restoration.
- Manual hardware, serial, debugger, Bluetooth, RF, pairing, reset, flash,
  recovery, erase, `nrf-probes`, OpenOCD, `serial-mcp`, `btattach`, or
  `scripts/bap_central.py` commands.
- Output mutation, output deletion, retry, alternate runner command, or manual
  post-run target action.
- Git reset, stash, clean, stage, commit, push, merge, PR, tag, release, or
  remote action.

Only `scripts/hil-runner.py` may change targets. It owns target identity
resolution, flashing, console capture, pairing, source control, cleanup, and
evidence finalization. Build stages must not overlap HIL execution.

## Ordered preflight

Run from repository root. Treat every warning as an error except documented dirty
worktree output, the documented nRF54L15 watchdog empty-library diagnostic, and
global `__ASSERT()` diagnostics. H40 must emit no experimental-symbol warning,
because its trace fragment sets `CONFIG_WARN_EXPERIMENTAL=n` for the deliberate
upstream-marked diagnostic.

### 1. Validate identity and reserve output destinations

Run before any target-changing action:

```bash
python3 -c 'import sys; sys.path.insert(0, "scripts"); from hil.lifecycle import validate_run_id; run_id = "rh3-20260826-40-sdc-iso-tx-notify-wq"; validate_run_id(run_id); print(f"run_id={run_id}\nlength={len(run_id)}\nvalid=yes")'

test ! -e /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
test ! -L /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
test ! -e /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
```

A failed identity or ownership check is a hard stop. Do not touch targets.

### 2. Verify host behavior and fixture

Run sequentially. No HIL execution may overlap the native build or host tests:

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-tx-notify-wq-rh3-40-execution-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected current host totals are native trace `18 passed`, RH2 `224 passed`,
Python compile pass, and matrix `0 errors, 0 notes`. Report exact totals if
unrelated pre-existing work changes them. Fixture validation must report fixture
ID `local-nrf54l15-receiver` and capture capability `none`.

### 3. Verify fragment, normal receiver, and source inputs

Verify all fixed inputs before replacing local receiver build output:

```bash
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
b8b3c47a58cd6f71714999eedac83ec27e6e114c0ec242aeeda8cc44537aa32a  tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf
EOF
```

Any mismatch is a hard stop. If a later trace build has already replaced normal
local output, restore the normal build and prove its hashes before reporting the
failure. Do not touch hardware after a failed preflight.

### 4. Build exact H40 trace image

Build only the H40 receiver trace image. Do not rebuild or replace source images.
The build command is exact:

```bash
nix develop --command fw-build-54l15 -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf"
```

Any build failure, actionable warning, RAM overflow, or experimental-symbol
warning is a hard stop. Restore normal local output before reporting the failure.

### 5. Prove exact H40 configuration, linkage, and image identities

Check every H39 trace flag, the private workqueue settings, the workqueue
execution context, the ISO pool depth, and the explicitly excluded trace knobs:

```bash
trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE=y' \
  'CONFIG_TRACING=y' \
  'CONFIG_TRACING_USER=y' \
  'CONFIG_TRACING_THREAD=y' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ=y' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8' \
  'CONFIG_BT_RECV_WORKQ_BT=y' \
  'CONFIG_BT_TX_PROCESSOR_THREAD=y' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  '# CONFIG_WARN_EXPERIMENTAL is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_NET_BUF_LOG is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done
rg -Fx 'CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y' "$trace_config"

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | rg 'conn_tx_workq|bt_conn_tx_workq_init|__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|__wrap_z_impl_k_sem_give'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_workq_init "$trace_elf" | \
  rg 'k_work_queue_(init|start)'

sha256sum --check <<'EOF'
c487f021a0ff29076e0a1b312a6826b4f2007623098f22c34fa19358f1bedb52  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
```

The `nm` and `objdump` commands above are the complete H40 private-workqueue
link proof. Do not replace them with a production configuration or a different
disassembly match. Missing symbols, missing initializer calls, a wrong resolved
option, an unexpected trace knob, a wrong hash, or any actionable build
diagnostic stops execution before hardware.

### 6. Repeat output ownership check immediately before runner

Run all four checks again immediately before the runner command. Do not run any
build or other command between these checks and the runner:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
test ! -L /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
test ! -e /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
```

Any ownership failure is a hard stop. Preserve existing destinations and do not
substitute another ID.

## Exactly one physical execution

Use the outer terminal tool timeout `7200000` ms. Do not use shell `timeout`.
Invoke this command exactly once, after all preflight steps pass:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260826-40-sdc-iso-tx-notify-wq \
  --junit /tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner exit status `0`, `1`, or `130` is evidence. Never retry for any status.
Allow the runner to complete target cleanup and evidence finalization. Do not
perform manual post-run target action.

## Read-only evidence review

After the runner returns, read only newly created H40 evidence. Verify integrity
even for failure or cancellation:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Review, without editing, deleting, or regenerating:

- `result.json`, aggregate and child JUnit files, and the external JUnit at
  `/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml`;
- `MANIFEST.md`, `images.json`, `identity.json`, and all image/hash records;
- `sdc-hci-remove-iso-path-trace.json`, including schema, raw snapshot fields,
  parser status, validation status, `parser_errors`, and `validation_errors`;
- source records, source terminal records, receiver status/terminal records,
  console logs, flash logs, active/status artifacts, and the commands ledger;
- raw target identity evidence retained by the runner, including probe and
  target fingerprint data, without making a static mapping claim.

Final report must state the raw outcome, aggregate counts, first failure boundary,
failure detail, cleanup failures, source and receiver terminal state, exact image
hashes, trace fields, and trace parser/validation result. Include the complete
`SHA256SUMS` result. Do not infer missing trace categories beyond the parser's
reported meaning.

Interpretation is bounded:

| Evidence | Meaning | Does not establish |
| --- | --- | --- |
| Runner exit `0`, valid trace, and passing row | This one H40 configuration avoided the observed row failure in this one fresh Mode B execution. | Production safety, root cause, general workload safety, or production adoption. |
| Runner exit `1` or `130` with valid evidence | Immutable diagnostic result, including its exact boundary and raw markers. | A production defect, a repair, or permission to retry. |
| H39-like pending/give markers | H40 retained the H39 observation set and recorded what was present before its snapshot. | Wait-queue ownership, deadlock, controller cause, or proof that moving queues caused or fixed the behavior. |
| Trace parser or validation error | Evidence interpretation is invalid until separately reviewed. | Any hypothesis conclusion. |
| Stack overflow, RAM mismatch, or actionable warning | H40 trace configuration is unsound for this experiment. | Any design or production conclusion. |

Success is not physical audio or production acceptance. Failure with H39-like
markers is evidence, not a fix. Never edit evidence to make a result pass.

## Required normal local restoration

After immutable evidence review, regardless of runner pass, failure, or
cancellation, restore only the local normal nRF54L15 build:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx '# CONFIG_TRACING is not set' "$normal_config"
rg -Fx '# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set' "$normal_config"
rg -Fx 'CONFIG_WARN_EXPERIMENTAL=y' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

git diff --check
git status --short
```

Normal proof must show trace disabled, private TX-notify workqueue disabled,
`WARN_EXPERIMENTAL=y`, ISO pool count `3`, and the four exact normal/source
hashes. If restoration fails, do not run another target command or modify
evidence. Report the exact failure.

## Required final report

Report concisely but completely:

- changed paths, confirming this handoff is the only newly created path;
- exact preflight commands and results, including host totals, fixture result,
  output ownership checks, config lines, RAM usage, and linkage proof;
- confirmation that the exact runner command ran once, with outer timeout,
  exit status, outcome, boundary, detail, and cleanup result;
- evidence path, `SHA256SUMS` result, raw target identity evidence, image hashes,
  trace JSON fields, parser/validation errors or empty lists, and source/receiver
  terminal details;
- normal local restore command, config proof, exact hashes, final
  `git diff --check`, and `git status --short`;
- explicit no-manual-hardware and no-commit confirmation;
- blockers or deviations, if any;
- smallest evidence-backed recommendation. Do not recommend production adoption
  or a repair from this one diagnostic run.
