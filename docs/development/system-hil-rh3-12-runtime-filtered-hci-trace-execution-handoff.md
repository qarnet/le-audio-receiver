# RH3-12 runtime-filtered HCI trace execution handoff

Status: one authorized physical diagnostic only. This is not a retry of any
earlier ID, not RH3 acceptance, and not root-cause proof.

## Goal

Run exactly one fresh, runner-owned direct HIL diagnostic using temporary
runtime-filtered HCI debug logging. Capture whether host `0x206f` send and SDC
driver completion logging occur before the known Remove ISO Path timeout.

The exact direct row is the existing 7.5 ms Mode B row:

```text
rh3.fresh_mode_b_48_3_1
```

It is a new observation under a new trace image and must never be called a
retry of RH3-10.

## Immutable ID and output paths

Use only this run ID:

```text
rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace
```

Exact paths:

```text
/tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace
/tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace.junit.xml
```

Both path and symlink forms must be absent before any build or hardware action.
If either exists, stop. Do not delete, overwrite, rename, choose a substitute
ID, or retry an existing run.

All earlier physical IDs, including RH3-10 and RH3-11, are immutable and must
not be rerun or altered.

## Scope

### In scope

1. Host-only preflight and temporary receiver debug build.
2. One direct runner invocation with fixed trace flag.
3. Evidence inspection after runner-owned cleanup.
4. Normal local receiver build restoration and hash proof.

### Out of scope

- Any second HIL run, child retry, matrix run, source build, manual flash,
  manual reset, OpenOCD command, serial-MCP session, separate serial reader,
  pairing, Bluetooth control, RF or power change.
- Any source, receiver, runner, configuration, warning-scanner, timeout,
  acceptance-rule, or documentation-result change.
- `STATUS.md`, immutable evidence, commit, push, merge, PR, tag, release,
  reset, stash, restore, or cleanup of unrelated worktree files.
- Claims about root cause, RF, SDC behavior, source behavior, QoS, audio,
  audibility, or acceptance. Report observations only.

## Grounding

- RH3-10 used `rh3.fresh_mode_b_48_3_1` and failed during teardown at HCI
  opcode `0x206f`, with `-11` after the NCS fixed ten-second wait.
- In NCS v3.3.0, `bt_hci_core` emits send/completion debug records and
  `bt_sdc_hci_driver` emits Command Complete/Status debug records.
- RH3-11 compiled both modules at debug globally, overflowed the 4 KiB deferred
  log ring, and failed boot-marker collection before source activity.
- Reviewed host change adds only fixed direct-run flag
  `--hci-remove-iso-path-trace`. After boot and clean state, before source
  configure/start, it sends:

  ```text
  log enable dbg bt_hci_core bt_sdc_hci_driver
  log status
  ```

  It requires both modules to report `current=dbg` and `built-in=dbg`; otherwise
  runner fails before source configuration. It preserves raw receiver RX/TX and
  `hci-remove-iso-path-trace.json`.
- `tests/hil/receiver-hci-remove-iso-path.conf` compiles only those two modules
  at debug, starts serial shell logging at INFO, and retains runtime filtering.
- Host proof completed: `tests/hil/rh2_test.py` 160 passed; Python compile and
  whitespace check passed; temporary build resolved levels 4/4 and serial INFO
  level 3; normal local receiver images restored exactly.

## Mandatory safety rules

- `scripts/hil-runner.py` is sole owner of all HIL hardware boundaries.
- Do not use serial-mcp, terminal serial tools, `btattach`, `bluetoothctl`,
  OpenOCD, `nrf-probes`, direct flash helpers, or any second process while
  runner is active.
- Do not use `timeout`; set terminal-tool outer timeout to `7200000` ms.
- Run commands sequentially. No test, build, or HIL process concurrently with
  runner.
- Worktree is intentionally dirty. Preserve every unrelated modification and
  untracked file. Do not stage or commit anything.

## Preflight

Run from repository root. Record exact output. Each failure is a hard stop;
do not proceed to hardware.

1. Confirm fresh output ownership:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace.junit.xml
```

2. Validate fixture and binding without hardware mutation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected JSON includes:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

3. Verify host tooling and normal baseline before temporary build:

```bash
nix develop --command pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
sha256sum --check <<'EOF'
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
git diff --check
git status --short
```

4. Build temporary trace receiver. `fw-build-54l15` already inserts CMake's
separator. Do not add another user `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-hci-remove-iso-path.conf"
```

Treat every undocumented build warning as a failure. Existing documented NCS
diagnostics remain exceptions only.

5. Inspect effective receiver configuration. All five values must match:

```text
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_BT_HCI_CORE_LOG_LEVEL=4
CONFIG_BT_HCI_DRIVER_LOG_LEVEL=4
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL=3
```

Also record SHA-256 values of the trace configuration fragment and the four
images. The runner will retain exact image hashes in `images.json`.

## Single HIL execution

Run exactly once with outer tool timeout `7200000` ms:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --hci-remove-iso-path-trace
```

Runner owns flash, console marks, raw capture, shell commands, fresh pairing,
source configuration/start/stop, cleanup, evidence finalization, and warning
scan. Do not interfere. Let cleanup finish even after failure. A nonzero result
is evidence, not permission for another run.

## After runner completion

1. Preserve every produced artifact unchanged. Verify its own checksum file:

```bash
sha256sum --check /tmp/opencode/hil-runs/rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace/SHA256SUMS
```

2. Read-only inspect, at minimum:

- `result.json`, `junit.xml`, `MANIFEST.md`, `images.json`, `summary.json` when
  present;
- `hci-remove-iso-path-trace.json`;
- `receiver-console.bin` and `receiver-console-tx.bin`;
- `source-records.jsonl`, source and receiver flash logs, and `commands.jsonl`.

Report exact observations:

- runner exit status, retained outcome, first failed boundary, cleanup failures;
- trace module levels and validation errors;
- whether `--- N messages dropped ---` appears;
- receiver debug send record for `opcode 0x206f`;
- receiver SDC-driver Command Complete or Command Status record for `0x206f`,
  if any;
- host completion/timeout/fatal record for `0x206f`, if any;
- source lifecycle/terminal evidence and receiver stream summary availability;
- exact four runner image hashes and trace-config hash.

Do not infer a controller defect from an absent record. Do not infer an RF,
source, or application cause from any record.

3. After runner cleanup and evidence inspection, restore only local build
artifacts. This does not flash hardware. Do not use a direct flash helper.

```bash
nix develop --command fw-build-54l15
sha256sum --check <<'EOF'
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
git diff --check
git status --short
```

The physical receiver may remain flashed with the temporary trace image after
the runner. Do not manually reflash it. A future runner-owned operation will
flash its selected local images.

## Return format

Return evidence, not conclusions:

1. exact preflight/build/runner/restore commands and statuses;
2. output paths and all artifact integrity counts;
3. trace JSON values and targeted raw-log excerpts;
4. build and runner image hashes;
5. normal local image-restoration result;
6. confirmation of sole runner hardware ownership, no retry, no repository
   result-doc edit, no commit, and no manual hardware action;
7. current dirty worktree summary;
8. blockers only when backed by retained evidence.
