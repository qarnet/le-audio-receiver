# RH3-34 ISO RX lifetime disposition trace execution handoff

Status: approved one-run physical diagnostic. This run observes bounded
per-buffer progression for the three tracked host ISO RX buffers seen in
RH3-33. It is not an RH3 acceptance run, production repair, pool-size
experiment, source-fixture repair, controller/NCS change, or root-cause claim.

## Fixed execution identity

```text
run ID: rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace.junit.xml
```

All four destination absence checks passed when this handoff was written. The
executor must repeat them before trace build and immediately before runner
execution. If any destination exists or is a symlink, preserve it and stop. Do
not substitute another ID or retry this ID.

## Goal and evidence boundary

Immutable RH3-33 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace/
```

reached fresh Mode B `48_3_1` streaming with valid schema-13 trace evidence.
At `disable` and later `unavailable`, it recorded:

```text
capacity=3 outstanding=3 high_water=3 allocations=19488
final_unrefs=19485 callbacks_active=0 callbacks_total=19485
```

The later target SDC allocation reported:

```text
kind=rx type=32 buffer_available=0 target_busy=0x1
receive_disposition_outcome=retained_iso_buffer_unavailable
```

No first-final-unref marker arrived before later `0x206f` timeout/fatal.
RH3-33 proves full tracked ISO RX occupancy at sampled points and no active
project callback at those samples. It does not establish queue location,
per-buffer owner, leak, callback-to-unref pairing, or a production fault.

RH3-34 adds a HIL-only wrapper around NCS internal `bt_conn_recv()` and records
one bounded disposition snapshot immediately after each existing `disable` or
`unavailable` lifetime snapshot:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

For a currently tracked pointer, categories mean only:

1. `undispatched`: wrapper did not observe `bt_conn_recv()`.
2. `host_dispatched`: wrapper observed `bt_conn_recv()`, but project
   `stream_recv()` did not start.
3. `app_callback_seen`: project `stream_recv()` started.
4. `unclassified`: adjacent atomic slot-state observation raced allocation or
   final release.

These fields are adjacent atomic observations. Their sum need not equal the
regular lifetime `outstanding` counter. Each field and sum are bounded by the
three-slot capacity. Do not infer specific NCS queue location, ownership,
leak, or repair direction from any category.

Installed NCS v3.3.0 route:

```text
nrf/subsys/bluetooth/controller/hci_driver.c:522-547
  bt_buf_get_rx(BT_BUF_ISO_IN) -> recv_func
zephyr/subsys/bluetooth/host/hci_core.c
  bt_hci_recv queues ISO for hci_iso
zephyr/subsys/bluetooth/host/iso.c:148-158
  hci_iso -> bt_conn_recv(iso, buf, flags)
zephyr/subsys/bluetooth/host/conn_internal.h:414
  void bt_conn_recv(struct bt_conn *, struct net_buf *, uint8_t)
zephyr/subsys/bluetooth/host/iso.c
  ISO reassembly -> ASCS/app callback -> bt_conn_reset_rx_state
zephyr/subsys/bluetooth/host/conn.c
  final net_buf_unref(conn->rx)
```

`--wrap=bt_conn_recv` changes only trace-image linkage. It does not retain a
buffer, inspect private `struct bt_conn`, dereference wrapper arguments, add
per-packet logs, delay work, alter refcounts, or affect normal builds.

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

RH3-34 trace fragment and expected trace images:

```text
8f945656ea4f2ed6801a7a24166a01ec40f6183b15695b74358a31e5a3634dd8  tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf
2c5616a26ab425cf34f4c97c736f8a41270893f4c08db7065df5f655367f8361  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

The trace hash was produced by RH3-34 software verification, then local normal
build was restored. The only later edit is an RH2 parser test proving the
already-implemented disposition-count sum bound, so it cannot change the
receiver trace image. Any different trace image identity is a hard stop before
hardware.

RH3-34 software review accepted:

- native SDC trace suite: `15/15` passed;
- RH2 parser suite: `220 passed`, including field and aggregate disposition
  capacity rejection;
- Python compile passed;
- matrix checker: `0 errors, 0 notes`;
- `git diff --check` passed;
- trace link/disassembly proof showed `hci_iso` calls `__wrap_bt_conn_recv` and
  `bt_conn_reset_rx_state` calls `__wrap_net_buf_unref`.

## Scope and ownership

In scope:

1. Sequential host-only verification and exact trace receiver build.
2. Trace config, image identity, link, and disassembly proof.
3. One runner-owned fresh Mode B `48_3_1` row.
4. Read-only review of immutable RH3-34 evidence.
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
control, cleanup, and evidence finalization. Do not rely on a static
probe-to-board mapping. No HIL execution may overlap a build.

## Ordered preflight

Run sequentially from repository root. Every preflight failure is a hard stop.
If trace build replaces normal local receiver build, restore normal and verify
it before reporting failure. Do not touch hardware after a preflight failure.

### 1. Reserve immutable output destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace.junit.xml
```

### 2. Verify host-only behavior and fixture

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-disposition-rh3-34-execution-unit \
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
2c5616a26ab425cf34f4c97c736f8a41270893f4c08db7065df5f655367f8361  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
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

The trace build must have no actionable warning or Kconfig assignment
diagnostic. Do not run production build-contract checks against temporary trace
build.

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
  --run-id rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Runner status `0`, `1`, or `130` is evidence. A nonzero status never permits a
retry. Let runner cleanup and evidence finalization finish. Do not manually
restore hardware.

## Read-only evidence review and local normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace
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
   arm/snapshot/first-free records, all disposition records, receive-disposition
   records, and generic completion markers. Retain raw lines, offsets, and
   classifications.
4. Whether source reached `streaming` before trace arm and whether a lifetime
   arm arrived.

Interpret only as follows:

- No source `streaming` state or no lifetime arm is inconclusive. Do not blame
  trace code, source image, RF, pairing, or receiver image.
- `unavailable` with `outstanding == capacity` proves tracked occupancy reached
  capacity at that sample only.
- `undispatched` proves only no wrapper observation of `bt_conn_recv()` for
  those currently scanned tracked slots. It does not name a queue or owner.
- `host_dispatched` proves wrapper observation before any project callback
  observation. It does not establish reassembly state, retained reference, or
  exact host owner.
- `app_callback_seen` proves `stream_recv()` started. It does not establish
  which later path retained a reference or why final unref had not arrived.
- `unclassified` means an adjacent slot-state race, not an error or owner.
- A count sum smaller than `outstanding` is valid for this non-transactional
  diagnostic. A count above capacity, malformed trace, parser/validation
  failure, wrong image identity, or unexpected warning invalidates
  interpretation.
- No category proves persistent leak or authorizes pool-size, queue-priority,
  lifecycle, or production ownership changes.

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
