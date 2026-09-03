# RH3-36 TX-notify flush linkage repair handoff

Status: focused repair to incomplete RH3-36 software diagnostic. This handoff
supersedes `system-hil-rh3-36-iso-rx-tx-notify-flush-trace-handoff.md` where
they differ. It preserves the successful partial implementation and replaces
only failed same-object link-wrapping design. No hardware action is authorized.

## Blocker and decision

Initial RH3-36 implementation passed host tests, parser tests, config proof,
and produced trace CPUAPP image:

```text
b1a91236e72d6499ea413429d2ed2b44098e5406df54747c1cc409bd28af3395
```

It failed required linkage proof. `--wrap=bt_conn_tx_notify` appeared in
`build.ninja` and wrapper symbols existed, but target disassembly retained the
same-object NCS call:

```text
bt_conn_recv:
  bl <bt_conn_tx_notify>
```

GNU linker `--wrap` redirects unresolved references. It does not redirect this
call because `bt_conn_recv()` and `bt_conn_tx_notify()` are both defined in
`zephyr/subsys/bluetooth/host/conn.c`. No trace image was flashed and local
normal build was not restored after this failed proof.

Do not patch NCS, alter production code, or try linker flags to interpose that
same-object call. Smallest correct design is to retain outer dynamic context in
`__wrap_bt_conn_recv()` and wrap only `k_work_flush()`. The current NCS source
proves that, before `bt_iso_recv()` and project callback, `bt_conn_recv()` calls
`bt_conn_tx_notify(conn, true)` (`conn.c:492-506`), and that function calls
`k_work_flush()` when caller is not TX-notify workqueue thread
(`conn.c:340-355`). `k_work_flush()` resides in `zephyr/kernel/work.c`, so its
cross-object call is link-wrappable.

Current trace disassembly already proves:

```text
hci_iso -> __wrap_bt_conn_recv
bt_conn_tx_notify -> __wrap_k_work_flush
bt_conn_reset_rx_state -> __wrap_net_buf_unref
```

Final proof must preserve those three facts and additionally prove that
`bt_conn_recv` still calls real `bt_conn_tx_notify`, not a nonexistent wrapper.

## Goal

Produce one schema-16 HIL-only trace build that classifies a live tracked ISO RX
buffer at lifetime snapshots as exactly one of:

1. `undispatched`
2. `host_dispatched`
3. `tx_notify_flush_entered`
4. `tx_notify_flush_returned`
5. `host_returned`
6. `app_callback_seen`
7. `unclassified`

Definitions are bounded wrapper observations:

- `tx_notify_flush_entered`: while matching outer `bt_conn_recv()` wrapper is
  live on same thread, `__wrap_k_work_flush()` entered and did not return.
- `tx_notify_flush_returned`: same scoped wrapper returned before project
  `stream_recv()` or outer `bt_conn_recv()` returned.

Because exact installed NCS source has no pre-callback `k_work_flush()` except
inside `bt_conn_tx_notify()`, these stages are scoped TX-notify-flush
observations. They do not identify work item owner, prove the flush slept,
establish a dependency cycle, identify a root cause, prove a leak, or authorize
a production fix.

## Scope

Allowed paths remain exactly:

- `Kconfig`
- `CMakeLists.txt`
- `src/sdc_hci_remove_iso_path_trace.c`
- `tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt`
- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- `scripts/hil/receiver.py`
- `tests/hil/rh2_test.py`
- `tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf`
- both RH3-36 handoffs, only for factual status/correction.

Out of scope:

- NCS source patch, `CONFIG_BT_CONN_TX_NOTIFY_WQ`, pool count, queue priority,
  production behavior, controller/source patch, HIL runner, matrix rows,
  thresholds, build contract, `STATUS.md`, public docs, and unrelated dirty
  work;
- hardware, HIL execution, flash, reset, recovery, serial, RF, pairing,
  OpenOCD, `nrf-probes`, `btattach`, `bap_central.py`, `serial-mcp`, source
  image rebuild, or output-directory operation;
- commit, stage, push, merge, PR, tag, reset, stash, clean, or broad format.

Repository `HEAD` is `c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1`. Worktree is
intentionally dirty. Preserve current partial changes. Do not discard or reset
them.

## Exact repair

### 1. Remove unsupported same-object TX-notify wrapping

Remove only RH3-36 pieces that depend on `--wrap=bt_conn_tx_notify`:

- CMake link option `-Wl,--wrap=bt_conn_tx_notify`;
- `__wrap_bt_conn_tx_notify()` and its `__real_` declaration;
- `TX_NOTIFY_ENTERED` stage, snapshot field, test fake/control, parser field,
  and all related observation storage;
- connection-pointer and nested flush-context association used solely by that
  removed wrapper.

Keep default-off Kconfig option named:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH
```

Its help must accurately say it wraps `k_work_flush()` while an HIL diagnostic
`bt_conn_recv()` context is live. It must not claim direct `bt_conn_tx_notify()`
wrapping. Keep the fragment selecting this option.

Keep only this additional trace-build link option:

```text
-Wl,--wrap=k_work_flush
```

Existing H35 disposition-only builds must retain their exact five-field output
and must not gain the `k_work_flush` link option when this new Kconfig option is
off.

### 2. Scope k_work_flush by outer bt_conn_recv context

When new gate is on, retain bounded atomic outer context only while real
`bt_conn_recv()` executes:

```text
tracked buffer pointer
current thread ID
active bit
```

`__wrap_bt_conn_recv()` must set buffer and thread, set active last, advance
tracked buffer to `HOST_DISPATCHED`, call real function exactly once with
unchanged arguments, advance still-current tracked buffer to `HOST_RETURNED`,
clear active first and remaining context before return. Clear context in existing
session/test reset paths too.

`__wrap_k_work_flush(work, sync)` must match only when outer context active is
set, current thread equals captured thread, and captured buffer is non-null. It
must:

1. Advance captured tracked buffer to `TX_NOTIFY_FLUSH_ENTERED`.
2. Call `__real_k_work_flush(work, sync)` exactly once.
3. Advance still-current matching buffer to `TX_NOTIFY_FLUSH_RETURNED`.
4. Return exact real Boolean result.

It must not inspect/dereference `work`, `sync`, `conn`, or `buf`; allocate,
reference-count, sleep, lock, spin, schedule work, log per packet, or access
private host fields. A nested application-callback flush cannot lower
`APP_CALLBACK_SEEN` because all stage transitions stay monotonic.

Use exact ascending stage values under new gate:

```text
UNCLASSIFIED = 0
UNDISPATCHED = 1
HOST_DISPATCHED = 2
TX_NOTIFY_FLUSH_ENTERED = 3
TX_NOTIFY_FLUSH_RETURNED = 4
HOST_RETURNED = 5
APP_CALLBACK_SEEN = 6
```

When new gate is off, preserve exact RH3-35 values and behavior.

### 3. Schema-16 grammar and compatibility

New-gate disposition lines must use exactly:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> tx_notify_flush_entered=<decimal> tx_notify_flush_returned=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Use seven bounded categories, exactly one per scanned live slot. Parser schema
remains `16` and must preserve:

- RH3-35 schema-15 grammar: both new flush fields are `None`.
- RH3-34 schema-14 grammar: `host_returned` and both new flush fields are
  `None`.

Malformed supplied new flush fields must fail parsing. Bounds validation checks
each present category and sums present categories only: seven new, five H35,
four H34. Preserve raw lines, offsets, classifications, legacy no-disposition,
and every existing validation rule.

### 4. Native and parser proof

Keep native count `15` and RH2 count `220`; modify existing test methods only.

In existing native progression test, remove direct TX-notify wrapper fakes.
Make fake real `bt_conn_recv()` optionally call `__wrap_k_work_flush()` directly
while outer diagnostic context is live. This models only wrapper dynamic scope;
trace ELF disassembly proves real NCS call chain separately. Prove in bounded
fresh sessions:

1. snapshot inside fake real flush before return has exactly one
   `tx_notify_flush_entered`, and work pointer, sync pointer, and Boolean return
   forward exactly;
2. snapshot after fake real flush returns but before fake real `bt_conn_recv()`
   returns has exactly one `tx_notify_flush_returned`;
3. fresh post-return and in-call callback paths retain one `host_returned`, one
   `app_callback_seen`, and one `undispatched` buffer;
4. unrelated direct flush outside outer context forwards once and cannot change
   lifetime stages; reset clears all new controls and context.

Update existing RH2 valid fixture and assertions to seven-field grammar. Within
existing parser tests prove both old compatibility forms, malformed supplied
`tx_notify_flush_entered`, per-field overflow, seven-field aggregate overflow,
and accepted H35/H34 sums. Update all schema assertions to `16`.

## Required verification

Run sequentially from repository root. No hardware action.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-rh3-36-repair-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check

nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf"
```

Expected results: native `15/15`, RH2 `220 passed`, matrix `0 errors, 0 notes`.
Any mismatch or actionable build/Kconfig warning is a stop.

For trace config, verify all existing H35 trace settings plus:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_CONN_TX=y
# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set
```

Verify exact link chain, not unsupported interposition:

```bash
toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | \
  rg '__wrap_bt_conn_recv|__wrap_k_work_flush|bt_conn_recv|bt_conn_tx_notify|k_work_flush|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | rg '(__wrap_)?bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg 'bt_conn_tx_notify'
! "$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg '__wrap_bt_conn_tx_notify'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg '__wrap_k_work_flush'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | rg '__wrap_net_buf_unref'
```

Record trace CPUAPP and FLPR hashes. Then mandatory local-only normal restoration:

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

Return changed paths; exact test/build output; exact disassembly facts; trace
and normal hashes; final status/diff; documented versus actionable diagnostics;
and explicit no-hardware/no-commit status. Stop and escalate if this narrower
design fails rather than guessing another interposition method.
