# RH3-36 ISO RX TX-notify flush trace software handoff

Status: superseded before completion by
`system-hil-rh3-36-tx-notify-flush-linkage-repair-handoff.md`. Do not execute
this version unchanged. GNU `--wrap` does not intercept the same-object
`bt_conn_recv()` to `bt_conn_tx_notify()` call in NCS `conn.c`.

Original status: focused software-only HIL diagnostic. This phase distinguishes a
tracked ISO RX buffer still inside the pre-`bt_iso_recv()` TX-notify flush from
one that has passed that flush. It does not execute HIL, flash hardware, change
production scheduling, enable a dedicated TX-notify queue, choose a repair, or
claim root cause.

## Goal

Extend the existing ISO RX lifetime disposition snapshot with three monotonic
stages that are active only in the HIL trace build:

```text
tx_notify_entered
tx_notify_flush_entered
tx_notify_flush_returned
```

At `disable` and `unavailable` snapshots, each live tracked slot must be
counted exactly once as one of:

1. `undispatched`: diagnostic wrapper did not enter `bt_conn_recv()`.
2. `host_dispatched`: `bt_conn_recv()` wrapper entered, but the tracked
   execution has not reached scoped `bt_conn_tx_notify()`.
3. `tx_notify_entered`: scoped `bt_conn_tx_notify()` wrapper entered, but its
   scoped `k_work_flush()` wrapper did not enter.
4. `tx_notify_flush_entered`: scoped `k_work_flush()` wrapper entered, but
   that wrapper did not return.
5. `tx_notify_flush_returned`: scoped `k_work_flush()` wrapper returned, but
   project `stream_recv()` has not started and `bt_conn_recv()` has not
   returned.
6. `host_returned`: `bt_conn_recv()` wrapper returned without project
   `stream_recv()` start.
7. `app_callback_seen`: project `stream_recv()` started.
8. `unclassified`: adjacent atomic slot-stage observation raced allocation or
   final release.

The new stages are wrapper observations only. In particular,
`tx_notify_flush_entered` does not identify work item ownership, prove that the
flush slept, establish a cycle, identify a controller/NCS defect, prove a leak,
or authorize a production change. `tx_notify_flush_returned` proves only that
the scoped wrapper returned from `k_work_flush()`.

## Grounding

Immutable RH3-34 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace/
```

and immutable RH3-35 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace/
```

both reached source `streaming`, armed ISO RX lifetime tracing at capacity
three, reported `retained_iso_buffer_unavailable`, and had no parser or
validation errors. Both rows ended at `session end` because receiver stream
summary slot zero was absent. Both evidence roots have verified SHA-256
manifests.

Their unavailable snapshots are materially identical except for schema:

```text
RH3-34, schema 14:
  outstanding=3 allocations=19485 final_unrefs=19482 callbacks_active=0
  undispatched=2 host_dispatched=1 app_callback_seen=0 unclassified=0

RH3-35, schema 15:
  outstanding=3 allocations=19485 final_unrefs=19482 callbacks_active=0
  undispatched=2 host_dispatched=1 host_returned=0
  app_callback_seen=0 unclassified=0
```

RH3-35 therefore proves only that the outstanding slot had entered
`bt_conn_recv()` and had not returned before the unavailable snapshot. It does
not prove why it had not returned.

Installed NCS v3.3.0 narrows the next bounded observation:

```text
zephyr/subsys/bluetooth/host/iso.c:117-159
  hci_iso() calls bt_conn_recv(iso, buf, flags), then unrefs iso.

zephyr/subsys/bluetooth/host/conn.c:492-512
  bt_conn_recv() first calls bt_conn_tx_notify(conn, true), then calls
  bt_iso_recv() for ISO.

zephyr/subsys/bluetooth/host/conn.c:340-355
  when caller is not TX-notify workqueue thread, bt_conn_tx_notify() submits
  conn->tx_complete_work and calls k_work_flush() when wait_for_completion is
  true.

zephyr/subsys/bluetooth/host/iso.c:653-795
  bt_iso_recv() has no work wait before direct channel recv callback. ASCS then
  calls project stream_recv() synchronously at ascs.c:914-924.

zephyr/kernel/work.c:458-488
  k_work_flush() waits on a semaphore only if work was queued or running.
```

The current normal nRF54L15 resolved configuration establishes the relevant
build shape:

```text
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_RX_PRIO=8
CONFIG_BT_CONN_TX=y
# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set
CONFIG_SYSTEM_WORKQUEUE_PRIORITY=-1
```

`hci_core.c:4635-4688` processes one queued HCI buffer per BT RX work
invocation. Controller MPSL receive work may allocate and queue later ISO RX
buffers independently before host RX processing frees an older one. The new
diagnostic associates a TX-notify call only with the currently executing
`bt_conn_recv()` wrapper by equal connection pointer and equal current thread;
it does not inspect any private `bt_conn` field.

The current normal ELF exports all required link symbols:

```text
bt_conn_recv
bt_conn_tx_notify
k_work_flush
```

## Scope

In scope:

- `Kconfig`
- `CMakeLists.txt`
- `src/sdc_hci_remove_iso_path_trace.c`
- `tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt`
- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- `scripts/hil/receiver.py`
- `tests/hil/rh2_test.py`
- `tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf`
- this handoff only for factual correction.

Out of scope:

- production source/config behavior, especially `CONFIG_BT_CONN_TX_NOTIFY_WQ`,
  pool count, queue priority, controller/NCS/source patch, lifecycle change,
  or repair;
- `src/bt_bap.c`, `src/audio_*`, HIL runner behavior, rows, thresholds, build
  contract, `STATUS.md`, public documentation, coverage baseline, and unrelated
  dirty work;
- hardware, HIL execution, flash, reset, recovery, serial, RF, pairing,
  `btattach`, `bap_central.py`, OpenOCD, or `serial-mcp`;
- commit, push, merge, PR, tag, reset, stash, clean, or broad formatting.

Repository `HEAD` is:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Preserve all existing changes and immutable
evidence. No hardware action is authorized in this phase.

## Exact implementation

### 1. Add opt-in trace gate and link wrappers

In `Kconfig`, add one default-off option after
`HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION`:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH
```

It depends on ISO RX lifetime disposition, is nRF54L15 HIL diagnostic only,
and states that it observes scoped `bt_conn_tx_notify()` and `k_work_flush()`
progress without establishing a work owner, blocking cause, or repair. Update
the existing disposition help text only as needed to avoid claiming that it
wraps `bt_conn_recv()` alone.

In `CMakeLists.txt`, under the existing ISO RX lifetime-disposition link block,
conditionally add only when this new option is enabled:

```text
-Wl,--wrap=bt_conn_tx_notify
-Wl,--wrap=k_work_flush
```

Keep existing `--wrap=bt_conn_recv` and `--wrap=net_buf_unref` behavior
unchanged. Normal builds must not compile the new wrappers or gain either link
option.

Enable the new option in the existing trace-only fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf`.
Do not change buffer count, log filtering, or any other trace fragment value.

### 2. Extend monotonic slot stages without private-state access

In `src/sdc_hci_remove_iso_path_trace.c`, introduce a compile-time internal
enable macro for the new Kconfig option and a matching native-test compile
definition. Under that gate only, extend the existing lifetime stage enum to
this exact ascending order:

```text
UNCLASSIFIED = 0
UNDISPATCHED = 1
HOST_DISPATCHED = 2
TX_NOTIFY_ENTERED = 3
TX_NOTIFY_FLUSH_ENTERED = 4
TX_NOTIFY_FLUSH_RETURNED = 5
HOST_RETURNED = 6
APP_CALLBACK_SEEN = 7
```

Keep the existing monotonic CAS stage-advance helper. Higher observations must
remain higher. In particular, an in-call `APP_CALLBACK_SEEN` must survive both
the real `bt_conn_recv()` return and any later wrapper observation. When new
gate is disabled, retain RH3-35 stage values and behavior exactly. Do not
allocate, reference-count, dereference `conn`, `buf`, `work`, or `sync`, add
per-packet logs, sleep, lock, spin, schedule work, or inspect private host
state.

Retain a bounded atomic diagnostic context only while the real
`bt_conn_recv()` invocation is live:

```text
current connection pointer
current ISO RX buffer pointer
current thread ID
```

`__wrap_bt_conn_recv()` must:

1. Store this context before forwarding and advance the equal tracked buffer to
   `HOST_DISPATCHED`.
2. Call `__real_bt_conn_recv(conn, buf, flags)` exactly once with unchanged
   arguments.
3. Advance a still-current equal tracked buffer to `HOST_RETURNED`.
4. Clear diagnostic context before returning.

The context is an association filter, not ownership state. Clear it during
existing session/test reset paths too. Do not assume static thread addresses.

Add `__wrap_bt_conn_tx_notify(struct bt_conn *conn, bool
wait_for_completion)` under the new gate. It may trace only when all are true:

```text
the bt_conn_recv context is live
conn equals the captured connection pointer
k_current_get() equals the captured thread ID
captured buffer pointer is non-null
```

For that scoped call, advance the captured tracked buffer to
`TX_NOTIFY_ENTERED`, publish a bounded atomic flush-context buffer and thread,
call `__real_bt_conn_tx_notify()` exactly once with unchanged arguments, then
clear only the flush context before returning. Nonmatching calls forward once
without changing lifetime stages.

Add `__wrap_k_work_flush(struct k_work *work, struct k_work_sync *sync)` under
the new gate. It may trace only when its current thread equals the active
scoped TX-notify thread and the scoped buffer is non-null. For a matching call:

1. Advance the captured tracked buffer to `TX_NOTIFY_FLUSH_ENTERED`.
2. Call `__real_k_work_flush(work, sync)` exactly once with unchanged
   arguments and preserve its Boolean return value.
3. Advance a still-current equal tracked buffer to
   `TX_NOTIFY_FLUSH_RETURNED`.
4. Return exact real Boolean result.

This wrapper deliberately does not read `work` or `sync`, identify
`tx_complete_work`, or claim whether the flush had to sleep. Existing NCS source
proves this scoped call originates from `bt_conn_tx_notify()` only under the
association rule above. A foreign `k_work_flush()` cannot match because its
thread differs or no scoped TX-notify context is live.

### 3. Emit schema-16 disposition grammar

When new gate is enabled, change lifetime disposition snapshot output to
exactly:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> tx_notify_entered=<decimal> tx_notify_flush_entered=<decimal> tx_notify_flush_returned=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Use one bounded eight-element local count array or equivalent. Scan each live
slot once and count it in exactly one category. Preserve regular lifetime
snapshot text, counters, snapshot order, all unrelated trace text, and the
disposition line immediately after its regular snapshot.

When new gate is disabled, retain exact RH3-35 five-field disposition output:
`undispatched`, `host_dispatched`, `host_returned`, `app_callback_seen`, and
`unclassified`. Do not silently change an existing disposition-only trace build
to schema-16 grammar or add TX-notify link wrappers.

Extend test-only disposition observation callback and native observations to
accept all eight counts. Keep no test-only behavior in production builds.

### 4. Parser schema 16 with old artifact compatibility

In `scripts/hil/receiver.py`:

1. Raise direct SDC trace schema from `15` to `16`.
2. Parse schema-16 grammar above into integer fields.
3. Continue accepting both old raw marker grammars:
   - RH3-35 schema-15 lines, containing `host_returned` but none of three
     TX-notify fields. Store each new TX-notify field as `None`.
   - RH3-34 schema-14 lines, containing neither `host_returned` nor the three
     TX-notify fields. Store all four absent fields as `None`.
4. Preserve raw line, offsets, classification, reason, and all existing fields.
5. Treat a supplied malformed new TX-notify field as malformed. Do not silently
   classify it as old grammar.
6. With a lifetime arm, validate every present category is at most capacity.
   Sum only present categories. New grammar sums eight categories, RH3-35 sums
   five, and RH3-34 sums four. Every sum must be at most capacity.
7. Preserve all existing arm uniqueness, reason uniqueness, parent-snapshot,
   ordering, and no-disposition compatibility rules.

Do not require new fields for immutable H34/H35 evidence. Do not modify old raw
records or infer a missing field as zero.

### 5. Native behavioral proof without changing test count

Add the matching compile definition to
`tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt`. Extend existing
native fake hooks only. Keep native method count `15`.

Extend existing
`test_iso_rx_lifetime_disposition_tracks_progression` to prove wrapper argument
forwarding and stage behavior in bounded test sessions. Add fake controls for:

- real `bt_conn_recv()` optionally calling scoped `__wrap_bt_conn_tx_notify()`;
- real `bt_conn_tx_notify()` optionally triggering existing disable snapshot
  before scoped `__wrap_k_work_flush()`, then optionally calling that wrapper;
- real `k_work_flush()` optionally triggering existing disable snapshot before
  its stub return;
- real `bt_conn_recv()` optionally triggering existing disable snapshot after
  TX-notify return and before its own return;
- existing optional application callback before real `bt_conn_recv()` return.

Use opaque non-null fake pointer values only for connection identity. Do not
define or inspect a real `struct bt_conn` in native tests.

Within that one existing test, prove at minimum:

1. At a snapshot taken from fake real `bt_conn_tx_notify()` before it calls
   fake `k_work_flush()`, tracked state includes exactly one
   `tx_notify_entered`.
2. At a snapshot taken from fake real `k_work_flush()` before return, tracked
   state includes exactly one `tx_notify_flush_entered`; wrapper forwarding of
   `conn`, `work`, `sync`, and Boolean return is exact.
3. At a snapshot taken after fake real `k_work_flush()` returns but before fake
   real `bt_conn_recv()` returns, state includes exactly one
   `tx_notify_flush_returned`.
4. In a fresh session, post-return no-callback and in-call callback paths retain
   exactly one `host_returned` and one `app_callback_seen`, respectively, with
   an additional undispatched buffer.
5. Reset paths clear every new fake control, observation, and diagnostic
   context. Nonmatching direct TX-notify/flush fake calls forward exactly once
   and do not alter tracked stage counts.

Preserve existing assertions for allocation, callback counters, final unrefs,
and no tracking error. Do not add a test method or weaken existing assertions.

### 6. Parser behavioral proof without changing test count

Extend existing RH2 parser test methods only. Keep RH2 method count `220`.

Update valid lifetime-disposition fixture grammar to schema 16. In existing
tests, prove records retain all three new integer fields and offsets. Derive
both compatibility variants from that valid fixture:

1. Remove exactly three TX-notify fields, retaining `host_returned`, and prove
   schema-15 compatibility with all new fields `None`.
2. Also remove `host_returned`, proving schema-14 compatibility with
   `host_returned` plus all new fields `None`.

Within existing malformed/bounds coverage, prove:

- malformed supplied `tx_notify_flush_entered` is rejected;
- per-category over-capacity is rejected for a new TX-notify field;
- eight-field aggregate sum above capacity is rejected when no individual field
  exceeds capacity;
- schema-15 five-field and schema-14 four-field sums remain accepted when
  within capacity.

Update every current direct-SDC parser schema assertion from `15` to `16`.
Do not add test methods or weaken current assertions.

## Verification

Run sequentially from repository root. No hardware action is authorized.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-rh3-36-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected focused results: native SDC trace suite `15/15`; RH2 `220 passed`;
matrix `0 errors, 0 notes`. If totals differ, report exact totals and stop. Do
not weaken tests to force a count.

Build one trace image only, without hardware:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf"

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y' \
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
  rg '__wrap_bt_conn_recv|__wrap_bt_conn_tx_notify|__wrap_k_work_flush|bt_conn_recv|bt_conn_tx_notify|k_work_flush|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | \
  rg '__wrap_bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | \
  rg '__wrap_bt_conn_tx_notify'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | \
  rg '__wrap_k_work_flush'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | \
  rg '__wrap_net_buf_unref'
```

Trace build must have no actionable warning or Kconfig assignment diagnostic.
Do not run production build-contract checks against temporary trace build.
Record trace CPUAPP and FLPR SHA-256 values.

Then restore normal local receiver build without hardware:

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

Return changed paths; exact test/build results; Kconfig/link/disassembly proof;
stage and parser schema/compatibility behavior; trace image hashes; normal
restoration proof; documented versus actionable diagnostics; final
`git status --short`; and explicit no-hardware/no-commit status. Stop and
escalate if wrapper association safety, legacy parser behavior, warning policy,
or test requirements need design invention or production behavior change.
