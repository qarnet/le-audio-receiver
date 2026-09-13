# RH3-36 ISO RX TX-notify flush trace execution handoff

Status: approved one-run physical diagnostic. This run observes whether a live
RH3-35 `host_dispatched` ISO RX buffer is inside, or has returned from, the
pre-callback `k_work_flush()` reached under live `bt_conn_recv()` context. It is
not RH3 acceptance, a production repair, pool-size or queue-priority experiment,
source/controller/NCS change, or root-cause claim.

## Fixed execution identity

```text
run ID: rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace.junit.xml
```

At handoff creation, no matching output path existed. Repeat all four absence
checks before trace build and immediately before runner execution. If any path
exists or is a symlink, preserve it and stop. Do not substitute another run ID
or retry this ID.

## Goal and evidence boundary

Immutable RH3-35 evidence is at:

```text
/tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace/
```

Its fresh Mode B `48_3_1` source reached `streaming`; receiver lifetime trace
armed at capacity three; target receive disposition reported
`retained_iso_buffer_unavailable`; and parser/validation errors were empty. At
unavailable snapshot it retained:

```text
outstanding=3 high_water=3 allocations=19485 final_unrefs=19482
callbacks_active=0 callbacks_total=19482
undispatched=2 host_dispatched=1 host_returned=0
app_callback_seen=0 unclassified=0
```

No first-final-unref marker occurred. Result was failed at `session end` because
receiver stream summary slot zero was absent. That failure is evidence, not an
authorization to retry H35.

Installed NCS v3.3.0 path grounds H36 observation:

```text
zephyr/subsys/bluetooth/host/iso.c:117-159
  hci_iso -> bt_conn_recv(iso, buf, flags)

zephyr/subsys/bluetooth/host/conn.c:492-506
  bt_conn_recv -> bt_conn_tx_notify(conn, true) -> bt_iso_recv

zephyr/subsys/bluetooth/host/conn.c:340-355
  bt_conn_tx_notify submits tx_complete_work and calls k_work_flush when caller
  is outside its TX-notify workqueue.

zephyr/kernel/work.c:458-488
  k_work_flush can wait only when work is queued or running.

zephyr/subsys/bluetooth/host/iso.c:787-794
  direct channel callback occurs later in bt_iso_recv.
```

Current nRF54L15 resolved normal configuration is:

```text
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_RX_PRIO=8
CONFIG_BT_CONN_TX=y
# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set
CONFIG_SYSTEM_WORKQUEUE_PRIORITY=-1
```

RH3-36 intentionally does not interpose same-object
`bt_conn_recv -> bt_conn_tx_notify`. GNU `--wrap` cannot redirect that NCS
same-object call. Instead it wraps cross-object `k_work_flush()` only while the
outer HIL `bt_conn_recv()` wrapper has matching live buffer/thread context.

H36 disposition grammar is:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> tx_notify_flush_entered=<decimal> tx_notify_flush_returned=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Field meanings are bounded:

1. `undispatched`: outer wrapper did not enter.
2. `host_dispatched`: outer wrapper entered but no scoped flush entry, callback,
   or outer return was observed.
3. `tx_notify_flush_entered`: matching scoped `k_work_flush()` wrapper entered
   but did not return before snapshot.
4. `tx_notify_flush_returned`: scoped flush wrapper returned but callback and
   outer wrapper return were not observed.
5. `host_returned`: outer wrapper returned without project callback observed.
6. `app_callback_seen`: project callback began.
7. `unclassified`: adjacent atomic slot-state observation raced allocation or
   final release.

No category identifies a work item owner, proves a flush slept, establishes a
cycle, names a NCS branch, proves a leak, or authorizes a repair. Count sum
smaller than lifetime `outstanding` remains valid because snapshot reads are
non-transactional. A malformed trace, parser/validation failure, wrong image,
or category/sum over capacity invalidates interpretation.

## Grounded immutable inputs

Repository `HEAD`:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
push, alter remote state, tag, or modify unrelated files.

Normal local receiver image:

```text
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Source images are immutable inputs. Do not rebuild or replace them:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

H36 trace fragment and expected trace image:

```text
aa8d87a615516433fb1d8e59b7b79a189e7bb34a909551144799c05d35d71ab8  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf
cedc7fecbc09cc25af74e05f6515ddeff3db22e847792c14108ea26616d96825  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

RH3-36 software review accepted:

- native SDC trace suite: `15/15` passed, including foreign-flush guard proof;
- RH2 parser suite: `220 passed`;
- Python compile passed;
- matrix checker: `0 errors, 0 notes`;
- `git diff --check` passed;
- schema is `16`; H35 five-field lines retain both new fields as `None`; H34
  four-field lines retain `host_returned` plus both new fields as `None`;
- trace link/disassembly proof showed `hci_iso -> __wrap_bt_conn_recv`,
  `bt_conn_recv -> bt_conn_tx_notify`,
  `bt_conn_tx_notify -> __wrap_k_work_flush`, and
  `bt_conn_reset_rx_state -> __wrap_net_buf_unref`.

## Scope and ownership

In scope:

1. Sequential host-only verification and one exact trace receiver build.
2. Trace config, image identity, link, and disassembly proof.
3. One runner-owned fresh Mode B `48_3_1` row.
4. Read-only review of new immutable H36 evidence.
5. Local normal nRF54L15 build restoration after evidence finalization.

Out of scope:

- production source/config repair, especially `CONFIG_BT_CONN_TX_NOTIFY_WQ`,
  pool-depth change, queue-priority change, controller/NCS/source patch,
  parser/runner/row/threshold/build-contract change, or `STATUS.md` edit;
- source-image build or replacement; nRF5340 build; matrix run; manual flash,
  reset, recover, erase, serial, RF, pairing, `btattach`, `bap_central.py`,
  OpenOCD, or `serial-mcp` activity;
- output mutation, retry, deletion, changed run ID, commit, push, merge, PR,
  tag, release, reset, stash, clean, or broad formatting.

Only `scripts/hil-runner.py` may change targets. It owns live identity
resolution, flashing, console capture, fresh unpair/pair setup, source control,
cleanup, and evidence finalization. Do not rely on static probe-to-board
mapping. No HIL execution may overlap a build.

## Ordered preflight

Run sequentially from repository root. Every preflight failure is a hard stop.
If trace build replaces normal local receiver build, restore normal and verify
it before reporting failure. Do not touch hardware after preflight failure.

### 1. Reserve immutable output destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace.junit.xml
```

### 2. Verify host-only behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-rh3-36-execution-unit \
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

- native SDC trace suite: `15/15` passing;
- RH2 Python suite: `220 passed`;
- matrix checker: `0 errors, 0 notes`;
- fixture ID: `local-nrf54l15-receiver`;
- capture capability: `none`.

Only documented dirty-worktree, nRF54L15 watchdog empty-library, and global
`__ASSERT()` diagnostics are non-actionable. Any other warning, Kconfig
assignment diagnostic, test failure, parser failure, fixture failure, or total
mismatch stops execution.

### 3. Verify normal, source, and fragment inputs

```bash
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
aa8d87a615516433fb1d8e59b7b79a189e7bb34a909551144799c05d35d71ab8  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf
EOF
```

### 4. Build and prove exact trace image

`fw-build-54l15` supplies CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf"

sha256sum --check <<'EOF'
cedc7fecbc09cc25af74e05f6515ddeff3db22e847792c14108ea26616d96825  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
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
  rg '__wrap_bt_conn_recv|__wrap_k_work_flush|bt_conn_recv|bt_conn_tx_notify|k_work_flush|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | rg '(__wrap_)?bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg 'bt_conn_tx_notify'
! "$toolchain_objdump" -d --disassemble=bt_conn_recv "$trace_elf" | rg '__wrap_bt_conn_tx_notify'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg '__wrap_k_work_flush'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | rg '__wrap_net_buf_unref'
```

Trace build must have no actionable warning or Kconfig assignment diagnostic.
Do not run production build-contract checks against temporary trace build.

### 5. Recheck output ownership

Repeat all four destination checks from step 1 immediately before runner
execution.

## One runner-owned physical execution

Use outer terminal-tool timeout `7200000` ms. Do not use shell `timeout`. Run
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. A nonzero status never permits a
retry. Let runner cleanup and evidence finalization finish. Do not manually
restore hardware.

## Read-only evidence review and local normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace
```

Verify immutable evidence:

```bash
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read only `result.json`, both JUnit files, `MANIFEST.md`, `images.json`,
`sdc-hci-remove-iso-path-trace.json`, source records, console logs, flash logs,
and command ledger. Return:

1. Exact runner status, outcome, first failed boundary/detail, cleanup failures,
   checksum/artifact count, image hashes, and raw identity evidence.
2. Source states, active status, terminal/idle state, connection/security errors,
   source controller assertions, receiver security/disconnect records,
   warnings/errors/fatals, and I2S evidence.
3. Parser schema/errors/validation errors, direct trace outcome, all lifetime
   arm/snapshot/first-free/disposition records, receive-disposition records, and
   generic completion markers. Retain raw lines, offsets, and classifications.
4. Whether source reached `streaming` before trace arm and whether a lifetime
   arm arrived.

Interpret only as follows:

- No source `streaming` state or no lifetime arm is inconclusive. Do not blame
  trace code, source image, RF, pairing, or receiver image.
- `unavailable` with `outstanding == capacity` proves tracked occupancy at that
  sample only.
- `tx_notify_flush_entered` means scoped flush wrapper entry was observed without
  scoped wrapper return at snapshot. It does not prove a work owner or sleep.
- `tx_notify_flush_returned` means scoped wrapper return was observed before
  callback or outer return. It does not identify later ISO host behavior.
- `host_dispatched`, `host_returned`, `app_callback_seen`, and `unclassified`
  retain their definitions above only.
- No category authorizes pool-size, queue-priority, lifecycle, NCS, or
  production ownership changes.

After evidence review, restore local normal nRF54L15 build only. Do not change
hardware after runner:

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

Return exact preflight/build/run commands and results; one-execution
confirmation; evidence path/checksum; raw board identity evidence; image hashes;
all trace records; bounded interpretation; exact normal-restoration proof; final
`git status --short`; no-manual-hardware/no-commit confirmation; and smallest
evidence-backed next step. Do not edit files during execution.
