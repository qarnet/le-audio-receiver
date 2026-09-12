# RH3-39 ISO RX TX-notify flush pend scheduler and give execution handoff

Status: approved single physical diagnostic execution. This phase runs exactly
one H39 trace image through the runner-owned fresh Mode B row, preserves the
new immutable evidence, then restores only the local normal build. It is not
production acceptance, a queue/pool experiment, a repair, an NCS patch, or a
root-cause conclusion.

Read the complete H39 software handoff first:

```text
docs/development/system-hil-rh3-39-iso-rx-tx-notify-flush-pend-sched-give-handoff.md
```

## Goal

H38 valid evidence observed the exact scoped `z_pend_curr()` wrapper entry
without wrapper return while host ISO RX occupancy reached three of three.
H39 adds two bounded observations:

1. Exact scoped `sys_trace_thread_pend_user()` after Zephyr has marked current
   thread pending.
2. Entry to generated `z_impl_k_sem_give()` for exact captured
   `&sync.flusher.sem`.

H39 asks only which of these markers is present at its lifetime snapshot. No
outcome proves wait-queue insertion, handler identity/ownership, give return,
waiter resumption, deadlock, controller defect, or production repair.

## Prior evidence and exact H39 image inputs

Immutable H38 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace/
```

H38 is valid diagnostic evidence:

```text
SHA256SUMS: 25/25 listed artifacts OK
schema: 18
parser_errors: []
validation_errors: []
source reached streaming and scored_complete
runner outcome: failed at session end only
failure detail: missing receiver stream summary slot(s): [0]
cleanup_failures: []

unavailable snapshot:
capacity=3 outstanding=3 high_water=3 allocations=19488
final_unrefs=19485 callbacks_active=0 callbacks_total=19485
undispatched=2 host_dispatched=0 tx_notify_flush_entered=0
host_returned=0 app_callback_seen=0 unclassified=0
```

H39 software phase passed:

```text
native SDC trace suite: 18 PASS / 0 FAIL
RH2 parser suite: 224 passed
Python compile: PASS
matrix: 0 errors / 0 notes
trace build/link proof: PASS
normal nRF54L15 local build restoration: PASS
```

Immutable inputs:

```text
HEAD: c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1

normal receiver CPUAPP: e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f
normal receiver FLPR:   45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2

source app:    f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source cpunet: 4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

H39 fragment:  052b98cae2123f79ad01ca3870f94a8711a7670666a37eeced133e38ab61ff45
H39 CPUAPP:    cd4b569523621a3192f23cb0b1841fcd3c6f3a9909bcd41cdaab90814d4c9675
H39 FLPR:      45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

H39 fragment:

```text
tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend-sched-give.conf
```

The installed NCS v3.3.0 source defines the observations:

```text
kernel/sched.c:add_to_waitq_locked()
  z_mark_thread_as_pending(thread)
  -> SYS_PORT_TRACING_FUNC(k_thread, sched_pend, thread)
  -> pended_on assignment and wait-queue insertion

kernel/work.c:finalize_flush_locked()
  -> k_sem_give(&flusher->sem)
```

`CONFIG_TRACING_USER=y` is H39 trace-only. It has no production use. NCS has no
work-handler execution hook. Do not add an NCS patch or invent a static-work
wrapper.

## Exact run identity

```text
run ID: rh3-20260826-39-sdc-iso-pend-sched-give
length: 39
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give
external JUnit: /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give.junit.xml
```

The ID was independently validated against `hil.lifecycle.validate_run_id()`.
Do not alter it or invent a replacement ID. If a destination exists or is a
symlink, preserve it and stop. No retry is permitted after any runner result.

## Scope and non-scope

### In scope

1. Host-only H39 verification and one H39 trace build.
2. One runner-owned fresh Mode B `48_3_1` execution.
3. Read-only review of newly finalized immutable evidence.
4. Local normal nRF54L15 build restoration after evidence review.

### Out of scope

- Any production source/configuration change, especially
  `CONFIG_BT_CONN_TX_NOTIFY_WQ`, queue priority, pool depth, work-handler,
  callback/lifecycle, audio, controller, source, or NCS change.
- Source-image rebuild/replacement; nRF5340 build; test-matrix, HIL runner,
  threshold, parser, build-contract, `STATUS.md`, public-doc, or coverage
  change.
- Manual flash, reset, recovery, erase, debugger, serial, RF, pairing,
  `btattach`, `bap_central.py`, OpenOCD, or serial MCP use.
- Output mutation, retry, deletion, reset, stash, clean, staging, commit,
  push, merge, PR, tag, release, or remote action.

Only `scripts/hil-runner.py` may change targets. It owns identity resolution,
flashing, console capture, fresh pairing flow, source control, cleanup, and
evidence finalization. The target may still hold an older H38 trace image. Do
not touch it outside the H39 runner invocation.

## Ordered preflight

Run sequentially from repository root. No HIL execution may overlap a build.
Every failure is a hard stop. If H39 trace build replaces normal local output,
restore normal local output before reporting failure. Do not touch hardware
after a preflight failure.

### 1. Reserve outputs and validate ID

```bash
python3 -c 'import sys; sys.path.insert(0, "scripts"); from hil.lifecycle import validate_run_id; run_id = "rh3-20260826-39-sdc-iso-pend-sched-give"; validate_run_id(run_id); print(f"run_id={run_id}\nlength={len(run_id)}\nvalid=yes")'

test ! -e /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give
test ! -L /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give
test ! -e /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give.junit.xml
```

### 2. Verify host behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-pend-sched-give-rh3-39-execution-unit \
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

Expected: native `18 passed`; RH2 `224 passed`; matrix `0 errors, 0 notes`;
fixture ID `local-nrf54l15-receiver`; capture capability `none`. Only
documented dirty-tree, nRF54L15 watchdog empty-library, and global
`__ASSERT()` diagnostics are non-actionable.

### 3. Verify normal, source, and fragment inputs

```bash
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
052b98cae2123f79ad01ca3870f94a8711a7670666a37eeced133e38ab61ff45  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend-sched-give.conf
EOF
```

### 4. Build and prove exact H39 trace image

`fw-build-54l15` supplies the CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend-sched-give.conf"

sha256sum --check <<'EOF'
cd4b569523621a3192f23cb0b1841fcd3c6f3a9909bcd41cdaab90814d4c9675  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

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
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  'CONFIG_BT_RECV_WORKQ_BT=y' \
  'CONFIG_BT_CONN_TX=y' \
  '# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_NET_BUF_LOG is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | \
  rg '__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|__wrap_z_impl_k_sem_give|bt_conn_recv|bt_conn_tx_notify|k_work_flush|z_impl_k_sem_take|z_pend_curr|z_impl_k_sem_give|sys_trace_thread_pend(_user)?|add_to_waitq_locked|work_queue_main'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | rg '(__wrap_)?bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg 'bt_conn_tx_notify'
! "$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg '__wrap_bt_conn_tx_notify'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg '__wrap_k_work_flush'
"$toolchain_objdump" -d --disassemble=k_work_flush "$trace_elf" | rg '__wrap_z_impl_k_sem_take'
"$toolchain_objdump" -d --disassemble=z_impl_k_sem_take "$trace_elf" | rg '__wrap_z_pend_curr'
"$toolchain_objdump" -d --disassemble=add_to_waitq_locked "$trace_elf" | rg 'sys_trace_thread_pend'
"$toolchain_objdump" -d --disassemble=sys_trace_thread_pend "$trace_elf" | rg 'sys_trace_thread_pend_user'
"$toolchain_objdump" -d --disassemble=work_queue_main "$trace_elf" | rg '__wrap_z_impl_k_sem_give'
```

H39 source proof uses `add_to_waitq_locked`, not a direct `z_pend_curr`
disassembly call. Current toolchain kept that static helper separate. A missing
edge, wrong image hash, config mismatch, Kconfig diagnostic, or actionable
warning stops this phase before hardware.

### 5. Recheck output ownership

Repeat all four absence/symlink checks from step 1 immediately before runner
execution. Do not substitute a new run ID if any fails.

## One runner-owned physical execution

Use outer terminal-tool timeout `7200000` ms. Do not use shell `timeout`. Invoke
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260826-39-sdc-iso-pend-sched-give \
  --junit /tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. A nonzero result never permits a
retry. Let runner cleanup and evidence finalization finish. Do not manually
restore hardware afterward.

## Read-only evidence review

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read only `result.json`, internal/external JUnit, `MANIFEST.md`, `images.json`,
`identity.json`, `sdc-hci-remove-iso-path-trace.json`, source records, console
logs, flash logs, active/status artifacts, and command ledger. Report all raw
H39 schema-19 records and source/receiver terminal state.

H39 interpretation is deliberately bounded:

| Snapshot state | Meaning | Does not establish |
| --- | --- | --- |
| `pend_thread_marked_pending=1`, `give_entered=0` | Exact H38-scoped thread reached user hook after Zephyr marked it pending; no exact captured flusher give entry observed before snapshot. | `pended_on` assignment, queue insertion, handler cause, deadlock, repair. |
| `give_entered=1` | Exact captured flusher semaphore entered generated give. In this NCS version source places that give in flush-barrier finalization. | Give return, wake/resume completion, handler semantic success, repair. |
| Both fields `0` while `pend_entered=1` | No H39 hook/give entry observed before snapshot. | Why the hook was absent, scheduler state, work state, cause. |
| `pend_returned=1` or later stage | H39 observed prior wrapper return progression before snapshot. | Why it returned or product correctness. |

Snapshot stages are exclusive and monotonic. A later give stage can supersede a
visible marked-pending stage. Do not infer a missing lower category means that
lower event never occurred.

Malformed trace, parser/validation error, wrong image hash, missing link proof,
or a category/sum above capacity invalidates H39 interpretation. A normal HIL
row failure remains evidence, not a permit to retry.

## Required local restoration

After immutable evidence review, restore only local normal nRF54L15 build:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx '# CONFIG_TRACING is not set' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF

git diff --check
```

## Return report

Return exact preflight/build/run commands and totals; one-execution
confirmation; immutable evidence path/checksum; raw target identity evidence;
image hashes; all schema-19 trace records; bounded interpretation; source and
receiver terminal/error/I2S/offload observations; normal-restoration proof;
final status; no-manual-hardware/no-commit confirmation; blockers/deviations;
and smallest evidence-backed next step.

Stop and escalate instead of guessing if output ownership fails, fixture or
trace build fails, any expected link edge is absent, artifact integrity fails,
normal restoration differs, or a change beyond diagnostic scope appears needed.
