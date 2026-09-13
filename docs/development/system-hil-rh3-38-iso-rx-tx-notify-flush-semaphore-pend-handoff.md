# RH3-38 ISO RX TX-notify flush semaphore pend software handoff

Status: approved software-only diagnostic phase. Implement one default-off HIL
trace extension that distinguishes entry/return of the exact
`z_pend_curr()` call reached from the H37-scoped flusher semaphore wait. This
phase does not run hardware, diagnose a root cause, or repair production code.

## Goal

H37 immutable evidence established this bounded path for one retained tracked
ISO RX buffer:

```text
hci_iso()
  -> __wrap_bt_conn_recv()
  -> bt_conn_tx_notify(conn, true)
  -> __wrap_k_work_flush()
  -> __wrap_z_impl_k_sem_take(&sync.flusher.sem, K_FOREVER)
```

H37 observed matching `z_impl_k_sem_take()` entry without its return before
the unavailable snapshot. It did not establish whether the implementation
selected `z_pend_curr()`.

H38 must add a narrow, exact-wait-queue observation point. It answers only:

```text
Did matching generated semaphore path enter and/or return from z_pend_curr()?
```

It must not claim `pend_locked()` executed, thread state changed, a work handler
failed to run, a deadlock exists, controller behavior caused the wait, buffer
ownership is known, or a product repair is justified.

## H37 evidence and installed-source grounding

Immutable H37 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260825-37-sdc-hci-iso-rx-tx-notify-flush-semaphore-trace/
```

H37 review facts:

```text
runner outcome: failed
first failed boundary: session end
failure detail: missing receiver stream summary slot(s): [0]
cleanup_failures: []
SHA256SUMS: all 25 listed artifacts OK
source: streaming -> scored_complete -> teardown -> terminal fail -> idle
parser schema: 17
parser_errors: []
validation_errors: []

unavailable lifetime snapshot:
capacity=3 outstanding=3 high_water=3 allocations=19485
final_unrefs=19482 callbacks_active=0 callbacks_total=19482

unavailable disposition snapshot:
undispatched=2 host_dispatched=0 tx_notify_flush_entered=0
tx_notify_flush_semaphore_entered=1 tx_notify_flush_semaphore_returned=0
tx_notify_flush_returned=0 host_returned=0 app_callback_seen=0 unclassified=0
```

Installed NCS v3.3.0 source is load-bearing:

```text
/home/thomas-workstation/ncs/v3.3.0/zephyr/kernel/work.c:458-488
  k_work_flush() calls k_sem_take(&flusher->sem, K_FOREVER) only when
  work_flush_locked() reports need_flush.

/home/thomas-workstation/ncs/v3.3.0/zephyr/kernel/sem.c
  z_impl_k_sem_take() calls z_pend_curr(&lock, key, &sem->wait_q, timeout)
  only after semaphore count is zero and timeout is not K_NO_WAIT.

/home/thomas-workstation/ncs/v3.3.0/zephyr/kernel/include/ksched.h:58-59
  int z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key,
                  _wait_q_t *wait_q, k_timeout_t timeout);

/home/thomas-workstation/ncs/v3.3.0/zephyr/kernel/sched.c:664-684
  z_pend_curr() acquires scheduler lock, calls pend_locked(), releases caller
  lock, then calls z_swap().

/home/thomas-workstation/ncs/v3.3.0/zephyr/include/zephyr/kernel.h:3594-3610
  struct k_sem exposes internal wait_q at sem->wait_q.
```

Current normal receiver disassembly proves direct call target before this work:

```text
z_impl_k_sem_take at 0x58e18:
  mov r2, r4
  mov r1, r7
  ...
  bl 0x5a88c <z_pend_curr>
```

Link wrapping must replace that call with `__wrap_z_pend_curr` in H38 trace
image. Do not add an NCS patch or copy kernel source.

## Scope

### In scope

1. New default-off child Kconfig gate and H38 trace fragment.
2. Linker wrap for `z_pend_curr` only under that new gate.
3. Exact active-context and wait-queue-scoped trace state/stages in
   `src/sdc_hci_remove_iso_path_trace.c`.
4. Schema-18 parser and tests, with schemas 14 through 17 still accepted.
5. Native trace unit coverage, RH2 parser coverage, matrix check, trace-image
   link proof, and normal local build restoration.

### Out of scope

- Any physical HIL execution, new run ID, evidence directory, flash, reset,
  serial, RF, pairing, central setup, OpenOCD, `btattach`, `bap_central.py`, or
  serial MCP use.
- Product behavior or configuration change, including pool depth, queue
  priority, `CONFIG_BT_CONN_TX_NOTIFY_WQ`, work handler, audio pipeline,
  callback/lifecycle, source/controller/NCS patch, or source-image rebuild.
- `STATUS.md`, public documentation, test matrix semantics, HIL runner behavior,
  row/threshold changes, build-contract changes, or broad formatting.
- Reset, stash, clean, staging, commit, push, merge, tag, release, or remote
  action. Worktree is intentionally dirty and contains unrelated user work.

## Immutable constraints

```text
HEAD: c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1

normal CPUAPP: e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f
normal FLPR:   45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2

source app:    f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source cpunet: 4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

H37 CPUAPP:    d5766415e92b953b5e45c5474d0e6c2062c71fdb3f90c47b3420b30bf00a362d
H37 FLPR:      45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

Normal local build currently restored and verified:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

No HIL target action occurs in this phase. Trace build changes local build
outputs only. Restore and verify normal local image before return.

## Exact implementation

### 1. Kconfig and trace fragment

Touch `Kconfig` and add this exact child symbol below existing H37 semaphore
gate:

```text
HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND
```

Rules:

- `bool`, `default n`.
- `depends on HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE`.
- Help says it observes entry/return of exact matching `z_pend_curr()` wait
  queue selected after scoped flusher semaphore path. It must explicitly say it
  does not establish a completed pend, work owner, blocking cause, deadlock, or
  repair.

Add new file:

```text
tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend.conf
```

It must contain, in this order:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_LOG_RUNTIME_FILTERING=n
```

Do not modify H37 fragment.

### 2. CMake wrap

Touch `CMakeLists.txt`. Under existing H37 semaphore conditional, nest new H38
conditional and add only:

```text
-Wl,--wrap=z_pend_curr
```

Do not enable this wrap for H37 or normal builds. Do not move other linker
options or change their conditions.

### 3. Trace source and stage grammar

Touch `src/sdc_hci_remove_iso_path_trace.c`.

Define enabled macro only when either production H38 Kconfig symbol or native
test H38 macro is defined:

```text
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED
```

Native test macro exact name:

```text
SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND
```

H38 uses existing captured scoped `struct k_sem *` from `__wrap_k_work_flush`.
No new global owner, work item, queue, logging, allocation, scheduling, or
timeout is allowed.

Add helper with this exact matching contract:

```c
static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_pend_context_matches(
    _wait_q_t *wait_q, k_timeout_t timeout, struct net_buf **buf_out)
```

It returns true only when all conditions hold:

1. Captured flusher semaphore is non-NULL.
2. `wait_q == &captured_sem->wait_q`.
3. `K_TIMEOUT_EQ(timeout, K_FOREVER)`.
4. Existing live receive context matches exact tracked buffer and current
   thread.
5. Exact buffer remains in tracked lifetime slots.

It must not dereference a NULL captured semaphore, inspect semaphore count,
inspect wait-queue contents, or use a loose pointer/type match.

Declare and implement exact wrapper signature, matching installed NCS source:

```c
int __real_z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key,
                       _wait_q_t *wait_q, k_timeout_t timeout);

int __wrap_z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key,
                       _wait_q_t *wait_q, k_timeout_t timeout);
```

Wrapper requirements:

1. Evaluate exact helper before real call.
2. If matching, advance tracked buffer to pend-entered stage.
3. Call `__real_z_pend_curr(lock, key, wait_q, timeout)` exactly once and
   preserve return value.
4. If initial scope matched and matching scope still holds after real return,
   advance same buffer to pend-returned stage.
5. Return exact real result. No logs in wrapper hot path.
6. Foreign/no-scope/wrong-wait-queue/non-forever calls forward untouched and
   do not change tracked stage.

Extend H37 stage chain only when H38 enabled. Exact H38 progression order:

```text
undispatched
host_dispatched
tx_notify_flush_entered
tx_notify_flush_semaphore_entered
tx_notify_flush_semaphore_pend_entered
tx_notify_flush_semaphore_pend_returned
tx_notify_flush_semaphore_returned
tx_notify_flush_returned
host_returned
app_callback_seen
unclassified
```

Stage names in C can use project naming style. Snapshot field order and exact
field spelling are mandatory:

```text
reason=<disable|unavailable>
undispatched=<decimal>
host_dispatched=<decimal>
tx_notify_flush_entered=<decimal>
tx_notify_flush_semaphore_entered=<decimal>
tx_notify_flush_semaphore_pend_entered=<decimal>
tx_notify_flush_semaphore_pend_returned=<decimal>
tx_notify_flush_semaphore_returned=<decimal>
tx_notify_flush_returned=<decimal>
host_returned=<decimal>
app_callback_seen=<decimal>
unclassified=<decimal>
```

Rules:

- H38 snapshot has 11 exclusive buckets, including unclassified. Exactly one
  bucket applies to each tracked buffer at snapshot time.
- Existing H37 output remains byte-for-byte grammar-compatible when H38 is
  disabled. Existing schemas 14, 15, 16, and 17 must remain parser-compatible.
- Never log stage entry/return individually. Existing bounded lifetime snapshot
  remains only runtime emission.
- Preserve H37 matching behavior. Do not broaden `__wrap_z_impl_k_sem_take`.

### 4. Parser schema 18

Touch `scripts/hil/receiver.py`.

Add schema-18 regex before schema-17 parser branch. Schema-18 grammar is exact
field order above. Change returned top-level parser `schema_version` from `17`
to `18`.

Every parsed disposition record must expose these keys:

```text
tx_notify_flush_semaphore_pend_entered
tx_notify_flush_semaphore_pend_returned
```

For schemas 14 through 17, both values must be `None`. Schema-18 values are
integers. Preserve all existing keys, parsed values, raw offsets, line strings,
classification, malformed-marker behavior, duplicate checks, parent ordering,
and capacity rules.

Add both fields to per-field capacity validation and exclusive count-sum
validation. `None` remains excluded from sum. Do not infer transitions or add a
causality validator.

### 5. Native and RH2 tests

Touch:

```text
tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt
tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c
tests/hil/rh2_test.py
```

Native unit test setup must define H38 native macro. Add fake
`__real_z_pend_curr()` with exact signature and controllable behavior. Do not
link or execute real scheduler code in native test.

Required observable native cases:

1. Exact captured flusher sem, exact `&sem->wait_q`, `K_FOREVER`, live tracked
   receive context: snapshot taken inside fake real pend before return records
   pend-entered `1`, pend-returned `0`, semaphore-returned `0`, flush-returned
   `0`; real args and return result preserved.
2. Matching pend return: snapshot taken after wrapped pend returns but before
   wrapped semaphore returns records pend-returned `1`, semaphore-returned
   `0`, flush-returned `0`; return result preserved.
3. Foreign wait queue, no active scope, or non-forever timeout: original
   `lock`, `key`, `wait_q`, and `timeout` forwarded exactly, no H38 stage
   changes.
4. Existing H37 matching/foreign sem cases remain passing.

RH2 parser tests must:

1. Add schema-18 raw fixture with exact grammar, values, line offsets, and
   empty parser/validation errors.
2. Assert schema-18 pend fields on both disable and unavailable records.
3. Reject malformed pend count, reordered pend fields, capacity excess, and
   stage-count sum excess.
4. Prove schemas 14 through 17 still parse with both new fields `None`.
5. Update existing top-level parser schema assertions to `18` without weakening
   existing behavior checks.
6. Preserve H37 schema-17 fixture coverage rather than silently converting it.

## Software verification

Run from repository root. No hardware actions. Every warning except documented
dirty-tree, nRF54L15 watchdog empty-library, and global `__ASSERT()`
diagnostics is a hard failure.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-rh3-38-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Build only H38 trace image to prove generated link path, then restore normal
local receiver build. `fw-build-54l15` supplies CMake separator. Do not add a
second `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend.conf"

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

nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF

git diff --check
git status --short
```

Do not run build-contract checks against trace image. Do not calculate or pin a
trace-image hash yet. Report it as observed output only.

## Return report

Return:

1. Files changed and exact diff scope.
2. Native/RH2 totals and all verification commands/results.
3. Schema-18 grammar and legacy parse proof.
4. Trace Kconfig and link/disassembly proof, including direct
   `z_impl_k_sem_take -> __wrap_z_pend_curr` observation.
5. Normal local restoration hash/config proof.
6. Final `git status --short`, no-hardware/no-commit confirmation, blockers,
   deviations, and smallest evidence-backed next step.

Stop and escalate rather than guessing if generated link proof fails, the
private signature differs, a warning appears, H37 grammar changes when H38 is
disabled, a legacy schema loses compatibility, a test requires an architectural
choice, or normal build does not restore exactly.
