# RH3-39 ISO RX TX-notify flush pend scheduler and give software handoff

Status: planned software-only diagnostic phase. Implement and prove locally.
Do not run hardware in this phase.

## Goal

RH3-38 valid physical evidence narrowed one retained ISO RX buffer to the
exact flusher semaphore's wrapped `z_pend_curr()` entry without an observed
return. RH3-39 distinguishes two next kernel boundaries without modifying NCS:

1. Whether the exact H38-scoped current thread reaches Zephyr's built-in
   scheduler-pend trace hook after `z_mark_thread_as_pending()`.
2. Whether the exact stack-local `sync.flusher.sem` reaches
   `z_impl_k_sem_give()` entry.

RH3-39 answers only those two observations. It does not identify a work owner,
prove wait-queue insertion, prove a handler ran, prove a semaphore give
returned, prove a waiter resumed, diagnose a deadlock, identify controller
cause, or authorize a production repair.

## H38 evidence and installed-source grounding

Immutable H38 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace/
```

Evidence integrity and run facts:

```text
SHA256SUMS: 25/25 listed artifacts OK
runner outcome: failed
first failed boundary: session end
failure detail: missing receiver stream summary slot(s): [0]
cleanup_failures: []
source: streaming -> scored_complete -> teardown -> terminal fail -> idle
trace parser: schema 18; parser_errors=[]; validation_errors=[]
receiver: nRF54L15, probe 8EE9B3FF, DPIDR 0x6ba02477,
          PART 0x00054b15, VARIANT AAC0
source: nRF5340, J-Link fingerprint PART 0x00005340, VARIANT 0x514b4141
```

H38 trace image identity:

```text
receiver CPUAPP: 158ac6ea96a32ecd50629f5af7c8b4f919f51f42f8247c8ff7b2059ae3eaa32a
receiver FLPR:   45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
source app:      f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source cpunet:   4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48
```

At H38's post-arm `unavailable` snapshot:

```text
capacity=3 outstanding=3 high_water=3 allocations=19488
final_unrefs=19485 callbacks_active=0 callbacks_total=19485
undispatched=2 host_dispatched=0 tx_notify_flush_entered=0
```

H38 direct trace/link proof is accepted:

```text
hci_iso -> __wrap_bt_conn_recv
bt_conn_recv -> bt_conn_tx_notify
bt_conn_tx_notify -> __wrap_k_work_flush
k_work_flush -> __wrap_z_impl_k_sem_take
z_impl_k_sem_take -> __wrap_z_pend_curr
```

Relevant NCS v3.3.0 contracts:

```text
zephyr/kernel/sem.c:139-158
  z_impl_k_sem_take() holds its file-static semaphore lock and calls
  z_pend_curr(&lock, key, &sem->wait_q, timeout) only after zero count and
  a non-K_NO_WAIT timeout.

zephyr/kernel/sched.c:664-684
  z_pend_curr() acquires _sched_spinlock, calls pend_locked(), releases the
  caller lock, then calls z_swap().

zephyr/kernel/sched.c:574-605
  add_to_waitq_locked() calls z_mark_thread_as_pending(thread), then
  SYS_PORT_TRACING_FUNC(k_thread, sched_pend, thread), then writes pended_on
  and inserts into wait_q. This function runs with _sched_spinlock held.

zephyr/subsys/tracing/user/tracing_user.h:169
  CONFIG_TRACING_USER maps that scheduler trace to sys_trace_thread_pend().
zephyr/subsys/tracing/user/tracing_user.c:211-214
  sys_trace_thread_pend() calls weak sys_trace_thread_pend_user(thread).

zephyr/kernel/work.c:108-116
  finalize_flush_locked() calls k_sem_give(&flusher->sem) only for its
  stack-local k_work_sync flusher barrier.
zephyr/kernel/sem.c:95-121
  z_impl_k_sem_give() unpends a waiter when present, readies it, and then
  reschedules or unlocks.
```

`pend_locked()`, `add_to_waitq_locked()`, `finalize_flush_locked()`,
`handle_flush()`, and `work_queue_main()` internals are static. Do not attempt
to wrap them. NCS v3.3.0 has no user-tracing work-handler execution hook.
No NCS patch is authorized.

## Scope

### In scope

1. One default-off child Kconfig gate and one H39 trace fragment.
2. H39-only `CONFIG_TRACING_USER` scheduler-pend hook, gated and scoped by the
   already exact H38 `z_pend_curr()` context.
3. H39-only exact `z_impl_k_sem_give()` linker wrapper, scoped to the captured
   `&sync.flusher.sem` while the H38 outer receive context remains live.
4. Schema-19 parser support and preservation of schemas 14 through 18.
5. Focused native behavior tests, RH2 parser tests, matrix check, trace-image
   link proof, and normal local nRF54L15 build restoration.
6. This handoff document.

### Out of scope

- Hardware, HIL execution, target change, flash, reset, recover, erase,
  direct debugger use, serial, RF, pairing, `btattach`, `bap_central.py`,
  OpenOCD, or serial MCP use.
- Any production behavior/configuration change, including
  `CONFIG_BT_CONN_TX_NOTIFY_WQ`, queue priority, pool size, work-handler,
  callback, audio, source, controller, or NCS change.
- Source-image rebuild/replacement; nRF5340 build; HIL runner/row/threshold or
  build-contract change; `STATUS.md`; public docs; coverage baseline; broad
  formatting; unrelated dirty work.
- Reset, stash, clean, stage, commit, push, merge, PR, tag, release, or remote
  action.

Repository `HEAD` is `c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1`. Worktree is
intentionally dirty. Preserve all existing modifications and untracked files.

## Exact implementation

### 1. H39 gate and trace fragment

Add this default-off Kconfig child immediately after the H38 pend gate:

```text
HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE
```

Requirements:

- `bool`, `default n`.
- Depends on
  `HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND`,
  `TRACING_USER`, and `TRACING_THREAD`.
- Help must say it observes only the H38-scoped user scheduler-pend hook after
  a thread is marked pending, plus entry to the exact captured flusher
  semaphore's generated give implementation. It must explicitly exclude
  wait-queue insertion, work ownership/execution, wake completion, root cause,
  and repair claims.

Create only:

```text
tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend-sched-give.conf
```

Its exact line order:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND=y
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
CONFIG_TRACING_THREAD=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_LOG_RUNTIME_FILTERING=n
```

Do not alter the H38 fragment. H39 enables temporary global tracing machinery
only in this trace build. Normal builds remain tracing-disabled.

### 2. Linker wrapping

Under the new H39 gate only, nested below existing H38 pend wrapping in
`CMakeLists.txt`, add exactly:

```text
-Wl,--wrap=z_impl_k_sem_give
```

Do not move existing options, wrap `k_sem_give`, wrap static work internals, or
enable the give wrapper for H38 or normal builds.

### 3. Scoped trace source behavior

Touch only `src/sdc_hci_remove_iso_path_trace.c` for runtime behavior.

Define one H39 internal enable macro from either the production H39 Kconfig
symbol or this native-only test macro:

```text
SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE
```

Include `<tracing_user.h>` when either existing scheduler-unlock support or H39
needs it. Do not define a second `sys_trace_thread_switched_in_user()`.

Add a fixed atomic H39 scheduler-hook arm flag beside existing scoped receive
context state. It has no ownership meaning. Clear it in the existing diagnostic
context-clear helper, session-open reset path, and test reset path.

#### H39 scheduler-pend hook

Extend existing `__wrap_z_pend_curr()` only under H39:

1. Preserve H38's exact pre-real matching rule unchanged.
2. When and only when H38 scope matches, arm the H39 scheduler-hook flag
   immediately before `__real_z_pend_curr()`.
3. Call real exactly once with untouched `lock`, `key`, `wait_q`, and `timeout`.
4. Clear the hook-arm flag immediately after real returns, before normal outer
   context cleanup. Preserve H38 return staging and exact result.
5. Foreign/no-scope calls must not arm it.

Define exactly one strong hook under H39:

```c
void sys_trace_thread_pend_user(struct k_thread *thread);
```

It may advance only when all conditions hold:

1. H39 scheduler-hook flag is armed by matching H38 `z_pend_curr()`.
2. `thread` equals captured outer receive thread.
3. Existing outer receive context remains active for the captured tracked
   buffer.
4. Captured buffer remains in tracked lifetime slots.

On match, monotonically advance the tracked buffer to
`tx_notify_flush_semaphore_pend_thread_marked_pending`. No log, allocation,
locking, work submission, wait, scheduler call, semaphore API, queue/field
inspection, or `k_current_get()` call is allowed in the hook. It runs with the
scheduler lock held. This stage means only that the built-in hook fired after
`z_mark_thread_as_pending()`; it does not establish `pended_on`, wait-queue
insertion, `z_swap()`, or sleep completion.

#### H39 flusher-sem give wrapper

Declare and define only under H39:

```c
void __real_z_impl_k_sem_give(struct k_sem *sem);
void __wrap_z_impl_k_sem_give(struct k_sem *sem);
```

Its match must require all of:

1. Existing H38 outer receive context remains active.
2. Captured tracked buffer is non-NULL and still occupies a lifetime slot.
3. Captured flusher semaphore is non-NULL.
4. `sem == captured_flusher_sem`.

Do not require current thread equality: the expected give occurs in a workqueue
thread, not in the blocked receive thread. Do not dereference `sem`, inspect
semaphore count, inspect a work field/handler/queue, or infer a work owner.

On an exact match, advance the tracked buffer to
`tx_notify_flush_semaphore_give_entered`, then call real exactly once. Do not
add a give-returned category: `z_impl_k_sem_give()` may reschedule the higher
priority waiter before a post-real wrapper check, allowing the outer context or
slot to disappear. The entry marker is bounded evidence only. Foreign calls
must forward untouched with no stage change.

The give wrapper and scheduler hook may use only fixed atomics, fixed pointer
identity, bounded slot scanning, and existing monotonic stage advancement. No
`LOG_*`, `printk`, allocation, waits, locks, work calls, scheduler calls, or
kernel object-field reads.

### 4. Stage and snapshot grammar

Extend the H38 enabled-only stage chain in this exact order:

```text
undispatched
host_dispatched
tx_notify_flush_entered
tx_notify_flush_semaphore_entered
tx_notify_flush_semaphore_pend_entered
tx_notify_flush_semaphore_pend_thread_marked_pending
tx_notify_flush_semaphore_give_entered
tx_notify_flush_semaphore_pend_returned
tx_notify_flush_semaphore_returned
tx_notify_flush_returned
host_returned
app_callback_seen
unclassified
```

H39 schema-19 disposition output must use this exact field order:

```text
reason=<disable|unavailable>
undispatched=<decimal>
host_dispatched=<decimal>
tx_notify_flush_entered=<decimal>
tx_notify_flush_semaphore_entered=<decimal>
tx_notify_flush_semaphore_pend_entered=<decimal>
tx_notify_flush_semaphore_pend_thread_marked_pending=<decimal>
tx_notify_flush_semaphore_give_entered=<decimal>
tx_notify_flush_semaphore_pend_returned=<decimal>
tx_notify_flush_semaphore_returned=<decimal>
tx_notify_flush_returned=<decimal>
host_returned=<decimal>
app_callback_seen=<decimal>
unclassified=<decimal>
```

Each currently live tracked slot contributes to one bounded category. Preserve
schemas 14 through 18 byte-for-byte when H39 is disabled. Do not add per-packet
logs or a new runtime snapshot.

### 5. Parser schema 19

Touch `scripts/hil/receiver.py`.

1. Add schema-19 regex before schema-18. Set top-level direct-SDC parser
   `schema_version` to `19`.
2. Every disposition record exposes integer schema-19 fields:

```text
tx_notify_flush_semaphore_pend_thread_marked_pending
tx_notify_flush_semaphore_give_entered
```

3. Schemas 14 through 18 expose both new keys as `None`.
4. Add both fields to per-field capacity and known-category aggregate checks,
   excluding `None` exactly like existing legacy fields.
5. Preserve raw offsets, line strings, classification, all old field values,
   malformed-marker rejection, duplicate/ordering rules, parent snapshots, and
   legacy compatibility. Do not add causality validation beyond existing
   bounds/order rules.

### 6. Tests

Touch only:

```text
tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt
tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c
tests/hil/rh2_test.py
```

Native test target must enable H39 native macro. Add fake real
`z_impl_k_sem_give()` and direct test control for
`sys_trace_thread_pend_user()`. Do not execute real scheduler or semaphore
internals.

Required observable native behavior:

1. Exact H38-scope fake `z_pend_curr()` invokes the H39 user hook before a
   bounded snapshot. Snapshot contains one
   `pend_thread_marked_pending`, zero `give_entered`, zero `pend_returned`, and
   zero outer returns. Real pend args/result stay exact.
2. Exact scoped fake pending path invokes the exact captured flusher semaphore
   give before snapshot. Snapshot contains one `give_entered`; fake give sees
   exact semaphore pointer and forwards once.
3. Wrong hook thread, unarmed hook, wrong give semaphore, and no active scope
   do not alter H39 categories while calls/arguments still forward exactly.
4. Existing H38 pend-entry/return, H37 semaphore, foreign path, lifetime, and
   parser behavior remain passing.

Extend existing test methods rather than adding a broad/new suite. Expected
focused totals remain native `18 passed` and RH2 `224 passed`; report exact
totals if tooling differs rather than weakening tests.

RH2 tests must prove valid schema-19 grammar/offsets with empty errors; both
new integer fields on schema 19; `None` for schemas 14 through 18; malformed
count; reordered new fields; individual capacity overflow; and aggregate sum
overflow.

## Verification

Run sequentially from repository root. No hardware action. Treat every warning
other than documented dirty-tree, nRF54L15 watchdog empty-library, and global
`__ASSERT()` diagnostics as a hard error.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-pend-sched-give-rh3-39-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Build exactly one H39 trace image, then prove its generated configuration and
linkage. Do not run production build-contract checks against this trace image.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend-sched-give.conf"

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
  rg '__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|__wrap_z_impl_k_sem_give|bt_conn_recv|bt_conn_tx_notify|k_work_flush|z_impl_k_sem_take|z_pend_curr|z_impl_k_sem_give|sys_trace_thread_pend(_user)?|work_queue_main'
"$toolchain_objdump" -d --disassemble=z_impl_k_sem_take "$trace_elf" | rg '__wrap_z_pend_curr'
"$toolchain_objdump" -d --disassemble=z_pend_curr "$trace_elf" | rg 'sys_trace_thread_pend'
"$toolchain_objdump" -d --disassemble=work_queue_main "$trace_elf" | rg '__wrap_z_impl_k_sem_give'
```

After trace proof, restore normal local nRF54L15 build:

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

Return changed files and exact scope; native/RH2 totals; parser schema-19 and
legacy proof; trace Kconfig/link/disassembly proof; trace image hashes as
observed output; normal restoration proof; final status; no-hardware/no-commit
confirmation; blockers/deviations; and smallest evidence-backed next step.

Stop and escalate rather than guessing if `TRACING_USER` cannot produce the
expected hook edge, linker wrapping does not interpose workqueue's
`z_impl_k_sem_give`, a warning/Kconfig diagnostic appears, schemas 14 through
18 lose compatibility, or normal restoration differs.
