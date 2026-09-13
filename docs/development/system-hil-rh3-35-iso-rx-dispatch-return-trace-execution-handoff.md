# RH3-35 ISO RX dispatch-return trace execution handoff

Status: approved one-run physical diagnostic. This run observes whether the
single RH3-34 host-dispatched tracked ISO RX buffer had returned from
`bt_conn_recv()` before the unavailable snapshot. It is not RH3 acceptance,
production repair, pool-size experiment, source-fixture repair, controller/NCS
change, or root-cause claim.

## Fixed execution identity

```text
run ID: rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace.junit.xml
```

All four destination absence checks passed when this handoff was written. The
executor must repeat them before trace build and immediately before runner
execution. If any destination exists or is a symlink, preserve it and stop. Do
not substitute another ID or retry this ID.

## Goal and evidence boundary

Immutable RH3-34 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace/
```

is valid schema-14 physical evidence with source `streaming`, lifetime trace
armed, expected trace image identity, zero parser/validation errors, and
target SDC `retained_iso_buffer_unavailable` outcome. At `unavailable` it
recorded:

```text
capacity=3 outstanding=3 high_water=3 allocations=19485
final_unrefs=19482 callbacks_active=0 callbacks_total=19482
undispatched=2 host_dispatched=1 app_callback_seen=0 unclassified=0
```

No first-final-unref marker appeared. The old trace saw wrapper entry, but did
not distinguish a call still inside `bt_conn_recv()` from one that returned
without project callback.

RH3-35 adds one HIL-only monotonic stage. New snapshots have exact grammar:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

For a live tracked pointer, fields mean only:

1. `undispatched`: diagnostic wrapper did not enter `bt_conn_recv()`.
2. `host_dispatched`: wrapper entered `bt_conn_recv()`, but snapshot observed
   neither wrapper return nor project `stream_recv()` start.
3. `host_returned`: wrapper returned from `bt_conn_recv()` without project
   `stream_recv()` start.
4. `app_callback_seen`: project `stream_recv()` started.
5. `unclassified`: adjacent atomic slot-stage observation raced allocation or
   final release.

The count fields are adjacent atomic observations. Their sum need not equal
regular lifetime `outstanding`, but each field and sum must be bounded by
capacity. Do not infer buffer owner, exact queue location, `bt_iso_recv()`
branch, leak, or repair from any field.

Installed NCS v3.3.0 path:

```text
nrf/subsys/bluetooth/controller/hci_driver.c:522-547
  bt_buf_get_rx(BT_BUF_ISO_IN) -> recv_func
zephyr/subsys/bluetooth/host/iso.c:148-158
  hci_iso -> bt_conn_recv(iso, buf, flags)
zephyr/subsys/bluetooth/host/conn.c:492-512
  bt_conn_recv -> bt_iso_recv for ISO -> return
zephyr/subsys/bluetooth/host/iso.c:653-795
  app callback path ends in bt_conn_reset_rx_state
zephyr/subsys/bluetooth/host/conn.c:391-399
  final net_buf_unref(conn->rx)
```

`host_returned` narrows only whether real `bt_conn_recv()` returned before the
snapshot. It deliberately does not inspect private ISO state.

## Grounded immutable inputs

Repository HEAD:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Do not reset, stash, clean, stage, commit,
push, alter remote state, tag, or modify unrelated files.

Normal local receiver image, verified before this handoff:

```text
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Source images are immutable inputs. Do not rebuild or replace them:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

RH3-35 trace fragment and expected trace images:

```text
8f945656ea4f2ed6801a7a24166a01ec40f6183b15695b74358a31e5a3634dd8  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf
0e96798809047cd862735b829a34a0562fbf2ce8b0c20ca4f2a2a4923e8390bf  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

RH3-35 software review accepted:

- native SDC trace suite: `15/15` passed;
- RH2 parser suite: `220 passed`;
- Python compile passed;
- matrix checker: `0 errors, 0 notes`;
- `git diff --check` passed;
- schema is `15`, and RH3-34 four-field disposition lines parse with
  `host_returned: None`;
- trace link/disassembly proof showed `hci_iso` calls `__wrap_bt_conn_recv` and
  `bt_conn_reset_rx_state` calls `__wrap_net_buf_unref`.

## Scope and ownership

In scope:

1. Sequential host-only verification and exact trace receiver build.
2. Trace config, image identity, link, and disassembly proof.
3. One runner-owned fresh Mode B `48_3_1` row.
4. Read-only review of immutable RH3-35 evidence.
5. Local normal nRF54L15 build restoration after evidence finalization.

Out of scope:

- production source/config repair; pool-depth change; NCS/controller/source
  patch; parser/runner/row/threshold/build-contract change; `STATUS.md` edit;
- source-image build or replacement; nRF5340 build; matrix run; manual flash,
  reset, recover, erase, serial, RF, pairing, `btattach`, `bap_central.py`,
  OpenOCD, or `serial-mcp` activity;
- output mutation, retry, deletion, changed run ID, commit, push, merge, PR,
  tag, release, reset, stash, clean, or broad formatting.

Only `scripts/hil-runner.py` may change targets. It owns live identity
resolution, flashing, console capture, fresh unpair/pair setup, source
control, cleanup, and evidence finalization. Do not rely on static
probe-to-board mapping. No HIL execution may overlap a build.

## Ordered preflight

Run sequentially from repository root. Every preflight failure is a hard stop.
If trace build replaces normal local receiver build, restore normal and verify
it before reporting failure. Do not touch hardware after a preflight failure.

### 1. Reserve immutable output destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace.junit.xml
```

### 2. Verify host-only behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-dispatch-return-rh3-35-execution-unit \
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

Only documented dirty-worktree, partition-manager/sysbuild deprecation, SW
Split experimental, upstream ISO choice-gap, and nRF54L15 watchdog
empty-library diagnostics are non-actionable. Any other warning, Kconfig
assignment diagnostic, test failure, parser failure, fixture failure, or total
mismatch stops execution.

### 3. Verify normal, source, and fragment inputs

```bash
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
8f945656ea4f2ed6801a7a24166a01ec40f6183b15695b74358a31e5a3634dd8  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf
EOF
```

### 4. Build and prove exact trace image

`fw-build-54l15` supplies CMake separator. Do not add another `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf"

sha256sum --check <<'EOF'
0e96798809047cd862735b829a34a0562fbf2ce8b0c20ca4f2a2a4923e8390bf  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
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
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
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
  rg '__wrap_bt_conn_recv|bt_conn_recv|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | \
  rg '__wrap_bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | \
  rg '__wrap_net_buf_unref'
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
  --run-id rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. A nonzero status never permits a
retry. Let runner cleanup and evidence finalization finish. Do not manually
restore hardware.

## Read-only evidence review and local normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace
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

1. Exact runner status, outcome, first failed boundary/detail, cleanup
   failures, checksum/artifact count, all image hashes, and raw identity
   evidence.
2. Source states, active status, terminal/idle state, connection/security
   errors, source controller assertions, receiver security/disconnect records,
   warnings/errors/fatals, and I2S evidence.
3. Parser schema/errors/validation errors, direct trace outcome, all lifetime
   arm/snapshot/first-free/disposition records, receive-disposition records,
   and generic completion markers. Retain raw lines, offsets, and
   classifications.
4. Whether source reached `streaming` before trace arm and whether a lifetime
   arm arrived.

Interpret only as follows:

- No source `streaming` state or no lifetime arm is inconclusive. Do not blame
  trace code, source image, RF, pairing, or receiver image.
- `unavailable` with `outstanding == capacity` proves tracked occupancy reached
  capacity at that sample only.
- `host_dispatched` means wrapper entry had been observed without wrapper
  return or app callback observation at snapshot. It does not prove a queue or
  owner.
- `host_returned` means wrapper return had been observed without app callback
  observation while that slot was scanned live. It does not identify a
  `bt_iso_recv()` branch, retain owner, leak, or repair.
- `app_callback_seen` proves project callback start, not later ref ownership.
- `unclassified` means an adjacent slot-state race, not an error or owner.
- Count sum smaller than `outstanding` is valid for this non-transactional
  diagnostic. A count above capacity, malformed trace, parser/validation
  failure, wrong image identity, or unexpected warning invalidates
  interpretation.
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
confirmation; evidence path/checksum; raw board identity evidence; image
hashes; all trace records; exact normal-restoration proof; final `git status
--short`; no-manual-hardware/no-commit confirmation; and blocker or smallest
evidence-backed next step. Do not edit files during execution.
