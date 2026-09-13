# RH3-40 TX-notify workqueue experiment software handoff

Status: software preparation and the single physical diagnostic completed. See the [canonical H40 result](system-hil-rh3-40-tx-notify-workqueue-result.md).

## Goal

RH3-39 established a bounded physical wait cycle during removal of the ISO data
path on the nRF54L15 receiver. Prepare one diagnostic-only receiver fragment
that enables Zephyr's existing separate connection TX-notify workqueue while
retaining every RH3-39 trace observation. The later, separately authorized
single H40 physical execution will test whether moving `tx_complete_work` off
`sysworkq` breaks that cycle.

This phase does not enable the option in a normal build, claim a production
repair, change a source image, or run hardware.

## Grounding and bounded hypothesis

Immutable H39 evidence is at:

```text
/tmp/opencode/hil-runs/rh3-20260826-39-sdc-iso-pend-sched-give/
```

Integrity passed `25/25` listed artifacts. H39 ran fresh Mode B `48_3_1`,
reached `streaming` and `scored_complete`, then failed only at session end with
`missing receiver stream summary slot(s): [0]`. Its valid schema-19 unavailable
snapshot was:

```text
capacity=3 outstanding=3 high_water=3 allocations=19485
final_unrefs=19482 callbacks_active=0 callbacks_total=19482
undispatched=2 host_dispatched=0 tx_notify_flush_entered=0
tx_notify_flush_semaphore_entered=0
tx_notify_flush_semaphore_pend_entered=0
tx_notify_flush_semaphore_pend_thread_marked_pending=1
tx_notify_flush_semaphore_give_entered=0
tx_notify_flush_semaphore_pend_returned=0
tx_notify_flush_semaphore_returned=0 tx_notify_flush_returned=0
host_returned=0 app_callback_seen=0 unclassified=0
```

The receiver then reported:

```text
Controller unresponsive, command opcode 0x206f timeout with err -11
Current thread: ... (sysworkq)
```

Exact installed NCS v3.3.0 evidence:

1. `zephyr/subsys/bluetooth/audio/ascs.c:513-605` runs ASE state transitions
   from `state_transition_work_handler()`. `ascs_ep_set_state()` schedules that
   work at lines 702-710.
2. `zephyr/kernel/work.c:1132-1141` implements `k_work_schedule()` by using
   `&k_sys_work_q`.
3. On streaming exit, `ascs.c:408-428` calls
   `bt_bap_remove_iso_data_path()`. `bap_iso.c:215-235` calls
   `bt_iso_remove_data_path()`, and `iso.c:346-379` sends synchronous HCI
   opcode `0x206f` with `bt_hci_cmd_send_sync()`.
4. `hci_core.c:461-508` creates a stack-local command semaphore and waits for
   completion. Current resolved nRF54L15 config has
   `CONFIG_BT_TX_PROCESSOR_THREAD=y`, so the sysworkq special path at lines
   474-501 is not selected and line 505 blocks the state-transition work.
5. Current resolved nRF54L15 config has `CONFIG_BT_RECV_WORKQ_BT=y`,
   `CONFIG_BT_CONN_TX=y`, and `# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set`.
   Therefore `conn.c:283-290` selects `&k_sys_work_q` for connection TX-notify
   work. `bt_conn_recv()` calls `bt_conn_tx_notify(conn, true)` at lines
   492-501. Off that queue, `conn.c:340-355` submits `conn->tx_complete_work`
   and blocks in `k_work_flush()`.
6. `kernel/work.c:458-488` waits on the stack-local flush semaphore only when
   the target work is queued or running. H39 reached the exact marked-pending
   hook for that semaphore and did not observe its generated give.
7. `nrf/subsys/bluetooth/controller/hci_driver.c:522-548,690-713` retains an
   ISO HCI message after `BT_BUF_ISO_IN` allocation returns `-ENOBUFS`. It does
   not fetch the later cached command-complete message until an ISO RX buffer
   is freed. `hci_internal.c:1859-1875` returns the cached command complete
   before asking SDC for another message.
8. `host/buf.c:62-67,145-151` re-signals that retained HCI driver work only
   when an ISO RX buffer is freed. Command Complete itself uses `sync_evt_pool`
   (`buf.c:154-180`), so the block is head-of-line ordering in the SDC adapter,
   not command-complete allocation from the ISO pool.

This gives the bounded circular-wait hypothesis:

```text
sysworkq state-transition work
  -> waits for 0x206f Command Complete
  -> Command Complete waits behind retained incoming ISO HCI message
  -> retained ISO message waits for an ISO RX buffer
  -> BT RX WQ bt_conn_recv() waits for tx_complete_work on sysworkq
```

`CONFIG_BT_CONN_TX_NOTIFY_WQ` is the installed upstream option designed to use
a separate queue instead of `sysworkq` for this processing:

```text
zephyr/subsys/bluetooth/host/Kconfig:160-187
zephyr/subsys/bluetooth/host/conn.c:283-290,4652-4669
```

It is experimental. Upstream Zephyr PR #79258 describes this as improving
Bluetooth independence from `sysworkq`, but records callback-context and
testing considerations. H40 is therefore a diagnostic experiment, not a
production adoption decision.

## Reserved later execution identity

The later execution phase must use exactly this validated, currently unused ID:

```text
rh3-20260826-40-sdc-iso-tx-notify-wq
length=36
```

Do not create its evidence directory or JUnit output in this software phase.
Do not run HIL or touch targets.

## Scope

### In scope

1. Create one H40-only trace fragment that contains all H39 trace settings and
   enables `CONFIG_BT_CONN_TX_NOTIFY_WQ=y` only for that trace image.
2. Add this handoff document.
3. Run focused host verification, build the trace image, prove resolved config
   and private-workqueue linkage, record image hashes, then restore the local
   normal nRF54L15 build exactly.

### Out of scope

- Any production Kconfig/source behavior change, including changes to
  `prj.conf`, board configs, `CONFIG_BT_ISO_RX_BUF_COUNT`, queue priority,
  queue stack size, source fixture, controller, or NCS source.
- `CMakeLists.txt`, `Kconfig`, `src/sdc_hci_remove_iso_path_trace.c`, parser,
  HIL runner, row, threshold, test-matrix, build-contract, coverage baseline,
  `STATUS.md`, public docs, or source-image changes.
- Hardware, HIL execution, flash, reset, recovery, erase, debugger, serial,
  RF, pairing, `btattach`, `bap_central.py`, OpenOCD, or serial MCP use.
- Reset, stash, clean, stage, commit, push, merge, PR, tag, release, or remote
  action.

Repository `HEAD` is `c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1`. Worktree is
intentionally dirty. Preserve every existing modification and untracked file.

## Exact implementation

Create exactly this file:

```text
tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf
```

Its exact contents and order:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE=y
CONFIG_TRACING=y
CONFIG_TRACING_USER=y
CONFIG_TRACING_THREAD=y
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_WARN_EXPERIMENTAL=n
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_LOG_RUNTIME_FILTERING=n
```

Rules:

- Set `CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536` only in this trace
  fragment. The first H40 build used upstream's 2048-byte default and overflowed
  CPUAPP RAM by 408 bytes. Its pre-link map shows the private queue's 0x128-byte
  control object and 0x800-byte `conn.c` noinit stack, with image end
  `0x20028198` beyond the `0x20028000` RAM limit. Reducing only that stack to
  1536 bytes removes 0x200 bytes and leaves 0x68 bytes under the limit. This is
  a bounded trace-image fit adjustment, not a production stack decision.
- Do not set `CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO`. Preserve installed default
  cooperative priority `8`; the trace build must prove its final value.
- `CONFIG_WARN_EXPERIMENTAL=n` is permitted only in this trace fragment. Its
  recorded reason is the deliberate, upstream-marked experimental diagnostic.
  It prevents that expected Kconfig notice from violating repository warning
  policy. Normal build restoration must prove `CONFIG_WARN_EXPERIMENTAL=y` and
  `BT_CONN_TX_NOTIFY_WQ` disabled.
- Do not change H39 fragment or source. Schema remains `19`; parser and native
  trace behavior remain unchanged.

## Required verification

Run sequentially from repository root. No hardware action. Treat every warning
other than documented dirty-tree, nRF54L15 watchdog empty-library, and global
`__ASSERT()` diagnostics as a hard error. An experimental-symbol warning is not
acceptable in H40 because the trace-only fragment explicitly disables that
warning mechanism with the documented reason above.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-tx-notify-wq-rh3-40-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected current totals: native trace suite `18 passed`, RH2 `224 passed`, and
matrix `0 errors, 0 notes`. Report exact totals if unrelated pre-existing work
changes them. Do not weaken a test or alter a baseline.

Build only the local H40 receiver trace image:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf"
```

Then prove this exact resolved configuration:

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

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | rg 'conn_tx_workq|bt_conn_tx_workq_init|__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|__wrap_z_impl_k_sem_give'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_workq_init "$trace_elf" | \
  rg 'k_work_queue_(init|start)'
sha256sum build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Missing private-workqueue symbol/disassembly evidence, a wrong resolved option,
an actionable warning, or an unexpected source-image hash is a hard stop. Do
not substitute a production Kconfig change.

After trace proof, restore only the local normal nRF54L15 build:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
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

## Return report

Return changed paths, exact commands and results, all trace-config proof lines,
trace CPUAPP and FLPR hashes, source hashes, linker/disassembly evidence,
normal-restoration proof, final `git status --short`, no-hardware confirmation,
no-commit confirmation, blockers/deviations, and whether a separately reviewed
H40 execution handoff is ready. Do not create output evidence, execute HIL, or
claim that a trace build proves a production repair.
