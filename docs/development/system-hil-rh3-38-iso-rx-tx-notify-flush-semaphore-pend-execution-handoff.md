# RH3-38 ISO RX TX-notify flush semaphore pend execution handoff

Status: superseded before hardware execution. The original 67-character run ID
failed `lifecycle.validate_run_id()` before lock acquisition, run-directory
creation, identity resolution, or target interaction. No HIL evidence exists
for that ID. See
`system-hil-rh3-38-iso-rx-tx-notify-flush-semaphore-pend-execution-fix-handoff.md`
for replacement execution. This diagnostic observes whether the H37-scoped
flusher semaphore path reaches the H38 `z_pend_curr()` wrapper. It is not RH3
acceptance, a production repair, a queue/pool experiment, an NCS patch, or a
root-cause claim.

## Fixed execution identity

```text
run ID: rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace.junit.xml
```

At handoff creation, no matching path exists. Repeat all four absence checks
before trace build and immediately before runner execution. If any destination
exists or is a symlink, preserve it and stop. Do not substitute another run ID
or retry this ID.

## Evidence goal and bounded interpretation

Immutable H37 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260825-37-sdc-hci-iso-rx-tx-notify-flush-semaphore-trace/
```

H37 source reached `streaming` and `scored_complete`; parser and validation
errors were empty; `SHA256SUMS` verified all 25 listed artifacts. Its
unavailable snapshot recorded:

```text
capacity=3 outstanding=3 high_water=3 allocations=19485
final_unrefs=19482 callbacks_active=0 callbacks_total=19482
undispatched=2 host_dispatched=0 tx_notify_flush_entered=0
tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=0
tx_notify_flush_returned=0 host_returned=0 app_callback_seen=0 unclassified=0
```

H37 established matching generated `z_impl_k_sem_take()` wrapper entry without
observing its return before snapshot. It did not establish semaphore count,
`z_pend_curr()` selection, completed pend, `pend_locked()` execution, thread
state, handler state, deadlock, ownership, controller cause, leak, or repair.

Installed NCS v3.3.0 source:

```text
zephyr/kernel/work.c:458-488
  k_work_flush() calls k_sem_take(&flusher->sem, K_FOREVER) when need_flush.

zephyr/kernel/sem.c
  z_impl_k_sem_take() calls z_pend_curr(&lock, key, &sem->wait_q, timeout)
  after zero semaphore count and non-K_NO_WAIT timeout.

zephyr/kernel/include/ksched.h:58-59
  int z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key,
                  _wait_q_t *wait_q, k_timeout_t timeout);
```

H38 wrapper scope is exact:

1. Active HIL outer `bt_conn_recv()` context, same tracked ISO RX buffer and
   same current thread.
2. Captured exact `&sync.flusher.sem` from scoped `k_work_flush()`.
3. `wait_q == &captured_sem->wait_q`.
4. `K_FOREVER` timeout.
5. Buffer remains in tracked lifetime slots.

H38 schema-18 disposition grammar:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> tx_notify_flush_entered=<decimal> tx_notify_flush_semaphore_entered=<decimal> tx_notify_flush_semaphore_pend_entered=<decimal> tx_notify_flush_semaphore_pend_returned=<decimal> tx_notify_flush_semaphore_returned=<decimal> tx_notify_flush_returned=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Interpret only:

1. `tx_notify_flush_semaphore_pend_entered=1` and
   `tx_notify_flush_semaphore_pend_returned=0` means the exact matching
   `z_pend_curr()` call selected H38 wrapper entry without observed wrapper
   return before snapshot. It does not prove entry to real `z_pend_curr()` body,
   `pend_locked()` execution, or completed pend.
2. `tx_notify_flush_semaphore_pend_returned=1` means H38 observed matching
   wrapped real `z_pend_curr()` return before snapshot. It does not identify why
   it returned or authorize a repair.
3. `tx_notify_flush_semaphore_entered=1` with both H38 pend counts zero means
   only that H38 did not observe matching pend-wrapper entry before snapshot.
   It does not prove semaphore count, lock behavior, owner, or scheduler cause.
4. `unavailable` with `outstanding == capacity` proves tracked occupancy at
   that sample only. Stage counts can sum below occupancy because snapshot reads
   are non-transactional; they must not exceed capacity.

No H38 category proves a semaphore count, completed pend, thread state,
deadlock, handler cause, controller defect, or production fix.

## Immutable inputs

```text
HEAD: c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1

normal CPUAPP: e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f
normal FLPR:   45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2

source app:    f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source cpunet: 4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

H38 fragment:  a1b1b4205e92f7506a81a203a36577dcac5522f02f1bee4b99909b76072eade8
H38 CPUAPP:    158ac6ea96a32ecd50629f5af7c8b4f919f51f42f8247c8ff7b2059ae3eaa32a
H38 FLPR:      45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

Trace image direct-link proof already passed:

```text
hci_iso -> __wrap_bt_conn_recv
bt_conn_recv -> bt_conn_tx_notify
bt_conn_tx_notify -> __wrap_k_work_flush
k_work_flush -> __wrap_z_impl_k_sem_take
z_impl_k_sem_take -> __wrap_z_pend_curr
```

Worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
push, tag, alter remote state, or modify unrelated files. Do not rebuild or
replace immutable source images.

H38 software acceptance is complete:

- native SDC trace suite: `18 passed`;
- RH2 parser suite: `224 passed`;
- Python compile passed;
- matrix checker: `0 errors, 0 notes`;
- trace image build passed with only dirty-tree warning;
- normal nRF54L15 build restored with trace disabled;
- `git diff --check` passed.

## Scope and ownership

In scope:

1. Sequential host-only verification and one exact H38 trace build.
2. Trace configuration, image identity, and link/disassembly proof.
3. One runner-owned fresh Mode B `48_3_1` row.
4. Read-only review of newly finalized immutable H38 evidence.
5. Local normal nRF54L15 build restoration after evidence review.

Out of scope:

- production source/config repair, especially `CONFIG_BT_CONN_TX_NOTIFY_WQ`,
  pool depth, queue priority, callback/lifecycle change, source/controller/NCS
  patch, parser/runner/row/threshold/build-contract change, or `STATUS.md`;
- source-image build/replacement; nRF5340 build; matrix run; manual flash,
  reset, recovery, erase, serial, RF, pairing, `btattach`, `bap_central.py`,
  OpenOCD, or serial MCP;
- output mutation, retry, deletion, changed run ID, commit, push, merge, PR,
  tag, release, reset, stash, clean, or broad formatting.

Only `scripts/hil-runner.py` may change targets. It owns live identity
resolution, flashing, console capture, fresh unpair/pair setup, source control,
cleanup, and evidence finalization. Do not rely on a static probe mapping. No
HIL execution may overlap a build.

## Ordered preflight

Run sequentially from repository root. Every failure is a hard stop. If trace
build has replaced normal local receiver build, restore normal and verify it
before reporting failure. Do not touch hardware after preflight failure.

### 1. Reserve outputs

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace.junit.xml
```

### 2. Verify host behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-rh3-38-execution-unit \
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

Expected:

- native SDC trace suite: `18 passed`;
- RH2 parser suite: `224 passed`;
- matrix checker: `0 errors, 0 notes`;
- fixture ID: `local-nrf54l15-receiver`;
- capture capability: `none`.

Only documented dirty-tree, nRF54L15 watchdog empty-library, and global
`__ASSERT()` diagnostics are non-actionable. Any other warning, Kconfig
assignment diagnostic, test/parser/fixture failure, or total mismatch stops
execution.

### 3. Verify normal, source, and fragment inputs

```bash
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
a1b1b4205e92f7506a81a203a36577dcac5522f02f1bee4b99909b76072eade8  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend.conf
EOF
```

### 4. Build and prove exact H38 trace image

`fw-build-54l15` supplies CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend.conf"

sha256sum --check <<'EOF'
158ac6ea96a32ecd50629f5af7c8b4f919f51f42f8247c8ff7b2059ae3eaa32a  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
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
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  'CONFIG_BT_RECV_WORKQ_BT=y' \
  'CONFIG_BT_CONN_TX=y' \
  '# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set' \
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
  rg '__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|bt_conn_recv|bt_conn_tx_notify|k_work_flush|z_impl_k_sem_take|z_pend_curr|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | rg '(__wrap_)?bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg 'bt_conn_tx_notify'
! "$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg '__wrap_bt_conn_tx_notify'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg '__wrap_k_work_flush'
"$toolchain_objdump" -d --disassemble=k_work_flush "$trace_elf" | rg '__wrap_z_impl_k_sem_take'
"$toolchain_objdump" -d --disassemble=z_impl_k_sem_take "$trace_elf" | rg '__wrap_z_pend_curr'
```

Trace build must have no actionable warning or Kconfig assignment diagnostic.
Do not run production build-contract checks against trace build.

### 5. Recheck output ownership

Repeat all four absence checks from step 1 immediately before runner execution.

## One runner-owned physical execution

Use outer terminal-tool timeout `7200000` ms. Do not use shell `timeout`. Run
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. A nonzero status never permits a
retry. Let runner cleanup and evidence finalization finish. Do not manually
restore hardware.

## Read-only evidence review and local restoration

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read only `result.json`, internal/external JUnit, `MANIFEST.md`, `images.json`,
`identity.json`, `sdc-hci-remove-iso-path-trace.json`, source records, console
logs, flash logs, and command ledger. Return:

1. Exact runner status, outcome, first failed boundary/detail, cleanup failures,
   checksum/artifact count, image hashes, and raw target identity evidence.
2. Source states, active status, terminal/idle state, connection/security
   errors, controller assertions, receiver security/disconnect records,
   warnings/errors/fatals, I2S evidence, and offload state.
3. Parser schema/errors/validation errors, direct trace outcome, all lifetime
   arm/snapshot/first-free/disposition records, receive-disposition records,
   and generic completion markers. Retain raw lines, offsets, and exact fields.
4. Whether source reached `streaming` before trace arm and whether a lifetime
   arm arrived.

Malformed trace, parser/validation error, wrong image hash, missing link proof,
or category/sum above capacity invalidates H38 interpretation.

After immutable evidence review, restore local normal nRF54L15 build only. Do
not change hardware after runner:

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

Return exact preflight/build/run commands and totals; one-execution
confirmation; evidence directory/checksum; raw identity evidence; image hashes;
all trace records; bounded interpretation; exact normal-restoration proof;
final `git status --short`; no-manual-hardware/no-commit confirmation; and
smallest evidence-backed next step. Do not edit files during execution.
