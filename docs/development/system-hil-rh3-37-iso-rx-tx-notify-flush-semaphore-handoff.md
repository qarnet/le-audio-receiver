# RH3-37 ISO RX TX-notify flush semaphore diagnostic handoff

Status: planned software-only diagnostic. Implement and prove it locally. Do
not perform a physical HIL run in this phase.

## Goal

RH3-36 established one tracked ISO RX buffer at pool exhaustion with:

```text
outstanding=3/3
tx_notify_flush_entered=1
tx_notify_flush_returned=0
```

This phase determines whether that exact scoped `k_work_flush()` invocation
reaches its generated `z_impl_k_sem_take()` call and whether that call returns
before an existing lifetime snapshot. It does not establish that a semaphore
actually pended, identify a work item owner, identify a queued or running
handler, prove a cycle or leak, or authorize a production repair.

Immutable RH3-36 evidence remains at:

```text
/tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace/
```

The H36 output is valid evidence: `SHA256SUMS` passed `25/25`; source reached
`streaming` and `scored_complete`; parser schema was `16`; parser and
validation errors were empty; runner failed only at `session end` because
receiver summary slot zero was absent; cleanup failures were empty. The
receiver later logged controller command `0x206f` timeout `-11`, assertion at
`hci_core.c:506`, and a kernel oops. Do not retry H36 or infer a repair from it.

## Grounded NCS contract

Installed source is `/home/thomas-workstation/ncs/v3.3.0`.

`zephyr/subsys/bluetooth/host/conn.c` establishes:

```c
bt_conn_recv()
    -> bt_conn_tx_notify(conn, true)
    -> k_work_submit_to_queue(tx_notify_workqueue_get(),
                              &conn->tx_complete_work)
    -> k_work_flush(&conn->tx_complete_work, &sync)
```

With current normal nRF54L15 configuration,
`CONFIG_BT_CONN_TX_NOTIFY_WQ` is unset. Therefore
`tx_notify_workqueue_get()` resolves to `k_sys_work_q`; do not change this
configuration in RH3-37.

`zephyr/kernel/work.c` establishes:

```c
need_flush = work_flush_locked(work, flusher);
if (need_flush) {
    k_sem_take(&flusher->sem, K_FOREVER);
}
```

`work_flush_locked()` returns true only when its locked work-state read has
`K_WORK_QUEUED` or `K_WORK_RUNNING`. It queues a flusher work item before the
semaphore call. The public `k_work_flush()` documentation says that a true
return means it had to wait for completion, but H36 did not return and exposes
no return value.

Exact normal nRF54L15 ELF disassembly confirms that this build emits a
cross-object call from `k_work_flush` to `z_impl_k_sem_take`, not to a linkable
`k_sem_take` symbol:

```text
k_work_flush:
  bl z_impl_k_sem_take
```

`z_impl_k_sem_take()` in `zephyr/kernel/sem.c` may return immediately when the
semaphore count is positive or may pend current thread when count is zero and
timeout is not `K_NO_WAIT`. Consequently, entering its wrapper does not prove a
sleep; absent wrapper return at snapshot does not identify why completion did
not occur.

## Scope

In scope:

1. Add one default-off HIL Kconfig child for exact scoped semaphore
   progression.
2. Extend only HIL trace source, link wiring, focused native test, HIL parser,
   RH2 parser tests, and a new trace configuration fragment.
3. Preserve H34, H35, and H36 parser compatibility.
4. Run local software verification, an H37 trace build and linkage proof, then
   restore normal nRF54L15 local build.
5. Add this handoff document only.

Out of scope:

- any physical HIL execution, target change, manual flash/reset/recover/erase,
  serial/RF/pairing work, OpenOCD, `btattach`, `bap_central.py`, or serial MCP;
- production behavior or configuration changes, especially
  `CONFIG_BT_CONN_TX_NOTIFY_WQ`, queue priority, ISO buffer counts, source/NCS
  patches, callback/lifecycle changes, or repair;
- source-image rebuild/replacement, nRF5340 build, matrix execution,
  build-contract update, test-matrix change, `STATUS.md` edit, documentation
  cleanup, commit, push, tag, reset, stash, or clean.

Repository HEAD at planning time is:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Preserve all pre-existing changes and
untracked handoffs.

## Files to change

| File | Required change |
| --- | --- |
| `Kconfig` | Add default-off `HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE`, dependent on existing TX-notify flush progression option. Help text must bound it to observing exact matching `z_impl_k_sem_take()` entry/return, not waiting, ownership, or repair. |
| `CMakeLists.txt` | Add `-Wl,--wrap=z_impl_k_sem_take` only when new child config is enabled, nested below existing `--wrap=k_work_flush`. Do not add `--wrap=k_sem_take` or restore rejected `--wrap=bt_conn_tx_notify`. |
| `src/sdc_hci_remove_iso_path_trace.c` | Add scoped semaphore context, stages, snapshot counts, and `__wrap_z_impl_k_sem_take()` described below. No hot-path log on each entry or return. |
| `tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore.conf` | New H37 trace fragment. Copy H36 fragment settings and add only new child config. |
| `tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt` | Add matching test-only semaphore macro. |
| `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c` | Extend fake-real seam and regression tests for exact semaphore scope and forwarding. |
| `scripts/hil/receiver.py` | Parse H37 grammar, expose schema 17 fields, and validate bounds/order without breaking schemas 14 through 16. |
| `tests/hil/rh2_test.py` | Cover schema-17 valid/malformed/bounds/order behavior and H36 legacy null fields. |
| `docs/development/system-hil-rh3-37-iso-rx-tx-notify-flush-semaphore-handoff.md` | This handoff. Do not modify it during implementation except correcting a factual blocker first reported to delegator. |

No other files are in scope.

## Exact H37 trace design

### Kconfig and compile gates

Add this child option immediately after current
`HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH`:

```text
HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE
```

Requirements:

- `bool`, `default n`;
- depends on existing TX-notify flush progression config;
- normal production builds compile no H37 wrapper or state;
- H36 fragment remains unchanged and resolves new option unset;
- new fragment enables the whole H36 chain plus this one child option.

Mirror existing compile-gate naming with a test-only macro:

```text
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED
SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE
```

### Scoped semaphore identity

Do not use a global `z_impl_k_sem_take()` trace. It would record unrelated
kernel traffic. Scope must require all of these conditions:

1. Existing outer HIL `__wrap_bt_conn_recv()` context is active.
2. Current thread equals captured outer receive thread.
3. Captured tracked ISO RX buffer still equals outer receive buffer.
4. `__wrap_k_work_flush()` captured the exact `&sync->flusher.sem` pointer for
   this outer invocation. Never dereference a null `sync`; let real
   `k_work_flush()` preserve its existing assertion behavior.
5. `__wrap_z_impl_k_sem_take()` receives that exact semaphore pointer and
   `K_FOREVER` timeout.

`struct k_work_sync` is public in this installed Zephyr build and contains
`flusher.sem`; this is the same member used by current `k_work_flush()` source.
Capture pointer before forwarding real `k_work_flush()`. Clear it together with
existing diagnostic context only after real call returns. Do not inspect
private `struct bt_conn` fields, read a work queue field, add a delay, submit
work, allocate memory, change scheduler state, or log per wrapper invocation.

### Stage grammar

When new child gate is enabled, final per-slot stage order is:

```text
0 unclassified
1 undispatched
2 host_dispatched
3 tx_notify_flush_entered
4 tx_notify_flush_semaphore_entered
5 tx_notify_flush_semaphore_returned
6 tx_notify_flush_returned
7 host_returned
8 app_callback_seen
```

`__wrap_k_work_flush()` retains H36 behavior. It advances only matching tracked
buffer to stage 3 before forwarding real function and stage 6 after return.

`__wrap_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout)` must:

1. Advance only exact matching tracked buffer to stage 4 immediately before
   calling `__real_z_impl_k_sem_take(sem, timeout)`.
2. Call real function exactly once with unchanged arguments.
3. Advance same still-matching context to stage 5 only after real function
   returns.
4. Return exact real integer result.
5. Leave every foreign/wrong-semaphore/non-`K_FOREVER` call unclassified by
   H37 while still forwarding it exactly once.

Do not call stage advancement, diagnostics, or extra kernel APIs when H37
scope does not match.

At existing disable/unavailable lifetime snapshots, emit existing lifetime
snapshot first, then H37 disposition snapshot. New exact grammar is:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> tx_notify_flush_entered=<decimal> tx_notify_flush_semaphore_entered=<decimal> tx_notify_flush_semaphore_returned=<decimal> tx_notify_flush_returned=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Each live tracked slot contributes to exactly one final stage at snapshot.
Adjacent atomics remain non-transactional, so count sum may be smaller than
`outstanding`; it must never exceed armed capacity. Preserve existing stage
names and H36 ordering around them. Do not emit a separate per-packet log.

Bounded meaning:

- `tx_notify_flush_semaphore_entered`: matching generated semaphore function
  entry observed but return not observed before snapshot. This means H37 reached
  `z_impl_k_sem_take()` for the scoped flush, not that it slept.
- `tx_notify_flush_semaphore_returned`: matching generated semaphore function
  returned before snapshot but `k_work_flush()` had not returned.
- `tx_notify_flush_returned`: existing H36 outer flush wrapper returned.

No count proves a semaphore was unavailable, a handler is queued/running, a
deadlock/cycle/leak exists, or any production change is correct.

## Parser contract

Set current parser `schema_version` to `17` and add two nullable record fields:

```text
tx_notify_flush_semaphore_entered
tx_notify_flush_semaphore_returned
```

Support four exact disposition grammars in this order:

1. H37 schema 17, all nine counts.
2. H36 schema 16, current seven counts. New semaphore fields become `None`.
3. H35 schema 15, pre-flush count grammar. All four flush/semaphore fields
   become `None`.
4. H34 schema 14, pre-host-return grammar. Host-return and all flush/semaphore
   fields become `None`.

Reject duplicate prefix, malformed decimal, trailing content, invalid reason,
out-of-order snapshot, field greater than capacity, and full known-count sum
greater than capacity. H37 parser validation must require a matching preceding
lifetime snapshot for each disposition snapshot exactly as existing logic does.
Do not require H37 records for historical evidence or treat nullable legacy
fields as zero.

## Required behavior tests

### Native trace test

Extend current native fixture rather than create a second test target. Add fake
real `z_impl_k_sem_take()` and capture exact semaphore pointer, timeout, result,
and call count. New behavior must prove through public wrapper output:

1. Matching tracked receive context, exact `sync->flusher.sem`, and `K_FOREVER`
   advances to semaphore-entered before fake real semaphore returns. Snapshot
   then reports one semaphore-entered, zero semaphore-returned, and zero
   flush-returned slots.
2. Fake real semaphore return advances to semaphore-returned before fake real
   flush returns. A snapshot in that interval reports one semaphore-returned
   and zero flush-returned slots.
3. After fake real flush returns, existing flush-returned behavior remains
   intact.
4. Exact `struct k_sem *`, `k_timeout_t`, `struct k_work *`,
   `struct k_work_sync *`, integer semaphore result, and Boolean flush result
   survive wrappers unchanged.
5. Foreign semaphore call, wrong semaphore pointer, or non-`K_FOREVER` timeout
   does not advance a tracked buffer, yet forwards exactly once.
6. Existing no-flush, callback, final-unref, and foreign-flush behavior still
   passes.

Do not assert private data layout, wrapper call counts alone, or implementation
only details without the observable stage/snapshot result.

### RH2 parser tests

Add/update tests that prove:

1. Valid H37 raw console emits schema 17, both new integer fields, exact raw
offsets, and no parser/validation errors.
2. H36 valid raw console emits schema 17 parser output with both new fields
`None`; H35/H34 retain all prior null compatibility.
3. H37 malformed semaphore count, duplicate prefix, capacity overflow, count
sum overflow, and ordering violations fail with targeted parser/validation
errors.
4. Existing fixture behavior remains unchanged apart from expected parser schema
number and explicitly added fields.

## Required local verification

Run sequentially from repository root. No HIL run may overlap a build.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-semaphore-rh3-37-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
git status --short
```

All focused tests must pass. Report actual totals. Any test failure, compiler
warning, Kconfig assignment diagnostic, parser failure, or matrix error blocks
completion.

Build H37 trace image only after focused checks pass:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore.conf"
```

Trace config must show:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE=y
CONFIG_BT_ISO_RX_BUF_COUNT=3
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_CONN_TX=y
# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set
```

It must keep these unrelated probes disabled:

```text
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set
# CONFIG_NET_BUF_LOG is not set
# CONFIG_TRACING is not set
# CONFIG_LOG_RUNTIME_FILTERING is not set
```

Prove link behavior from exact trace ELF. Resolve tool paths from its
`CMakeCache.txt`; do not assume host tool names:

```bash
trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)

"$toolchain_nm" -A "$trace_elf" | rg '__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|bt_conn_recv|bt_conn_tx_notify|k_work_flush|z_impl_k_sem_take|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | rg '(__wrap_)?bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg 'bt_conn_tx_notify'
! "$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg '__wrap_bt_conn_tx_notify'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg '__wrap_k_work_flush'
"$toolchain_objdump" -d --disassemble=k_work_flush "$trace_elf" | rg '__wrap_z_impl_k_sem_take'
```

Any missing wrapped edge blocks completion. A direct `z_impl_k_sem_take` call
in trace `k_work_flush` means H37 did not interpose and must not be claimed
ready.

Do not run production build-contract checks against temporary trace build.
After trace proof, restore normal local nRF54L15 build:

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

Return changed-file list; behavior/proof summary; exact commands and actual
test totals; trace config and trace CPUAPP/FLPR hashes; exact linker and
disassembly evidence; normal restoration proof; `git diff --check`; final
`git status --short`; no-HIL/no-target-change/no-source-rebuild/no-commit
confirmation; and blockers, if any. Do not make a physical execution handoff
until software review accepts this implementation.
