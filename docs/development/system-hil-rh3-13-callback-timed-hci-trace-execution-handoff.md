# RH3-13 callback-timed HCI trace execution handoff

Status: execution handoff only. This document prepares one future immutable
physical HIL trace. It does not execute HIL. After this run produces a result,
no second run, child retry, or replacement run is authorized.

## Fixed execution identity

Use exactly one direct runner row:

```text
run ID: rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
row: rh3.fresh_mode_b_48_3_1
JUnit: /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace.junit.xml
output directory: /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
```

This is one new observation. It is not a retry of RH3-12 or any earlier RH3
run.

## Immutable grounding

RH3-12, `rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace`, is
immutable failed evidence. Its evidence checksum was 23/23. It failed because
the receiver reported:

```text
log: command not found
```

Never retry RH3-12, alter its evidence, or reuse its run ID.

RH3-13 code now arms only on the first `lc3_disable()` invocation, through
`CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE`. Trace evidence is valid only when it
has all of these properties:

1. Exactly one successful arm marker exists.
2. Arm marker source IDs are nonnegative.
3. Arm marker core and driver levels are both `4`.
4. No dropped-message record occurs after arming.
5. At least one post-arm `bt_hci_core` send record contains HCI opcode
   `0x206f`.

Pre-arm records remain retained observations, but a pre-arm `0x206f` send
cannot satisfy the post-arm target-send requirement. A valid arm-failure marker
must fail at the `hci remove iso path trace evidence` boundary, not at generic
`log scan`. Unrelated warnings remain strict and must not be suppressed,
reclassified, or called acceptable.

## Exact image and fragment identities

The user-approved current normal baseline applies to this new execution
handoff only:

```text
27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Historical normal image identity remains immutable earlier evidence only. It is
not a required restore target:

```text
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723
```

Use these existing source images. Verify them before any trace build and do not
rebuild or replace them:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

After the exact trace build, expect:

```text
f32fd4452f943bedbacaba4bb8a305c1489ac7d9e4c5c39faa874b9d410f813c  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

The trace fragment must remain this exact file:

```text
33f7c2fb49773e8811dfabf09471b6670414bfb5fc2d3fdee1a4f46182b50dc9  tests/hil/receiver-hci-remove-iso-path.conf
```

## Scope and prohibitions

In scope:

- host-only preflight and tests;
- verification of the existing source images and current normal baseline;
- one exact nRF54L15 trace receiver build;
- one runner-owned direct HIL execution with the fixed run ID and row;
- read-only evidence review after runner-owned cleanup;
- restoration of only the local normal nRF54L15 build and its exact hashes.

Out of scope:

- any second HIL run, child retry, matrix retry, or substitute run ID;
- source firmware rebuild, source image replacement, or nRF5340 rebuild in this
  execution phase;
- edits to source, configuration, runner code, fixtures, evidence, result
  documents, `STATUS.md`, previous handoffs, or any other repository file;
- manual serial, `serial-mcp`, flash, reset, OpenOCD, probe, pairing, RF,
  power, HCI, BlueZ, or shell interaction;
- any shell `log` command or `CONFIG_LOG_CMDS` enablement;
- commit, push, merge, tag, release, destructive cleanup, reset, restore, or
  stash.

The runner is sole owner of its prescribed hardware boundaries, including
flash, reset, serial capture, pairing, HCI/BlueZ control, source streaming,
and cleanup. Do not run any parallel hardware or host process while it owns
the fixture. Do not manually restore hardware after the run. Local normal
image restoration below is a build-only action.

## Latest host proof and warning gate

Latest host proof is already complete:

- native helper: 6/6 passed after `CONFIG_FAKE_ENTROPY_NATIVE_SIM=n` removed
  the native test entropy warning;
- `pytest tests/hil/rh2_test.py`: 163 passed;
- Python compile, `check-test-matrix`, and `git diff --check`: passed, with
  check-test-matrix reporting 0 errors and 0 notes;
- `fw-build-54l15` trace and normal builds: passed;
- `fw-build-5340`: passed.

The future preflight repeats host checks before hardware. Known project
diagnostics are limited to the dirty worktree notice, `PARTITION_MANAGER`
deprecation, SW Split experimental symbols, the known ISO low-latency choice
warning, and the existing nRF54L15 watchdog `No SOURCES` diagnostic. These are
not a blanket warning allowance. Do not call generic warnings acceptable. Any
other build, Kconfig, CMake, runner, boot, or log warning is a hard stop.

## Ordered preflight and build

Run commands sequentially from the repository root. Every preflight, hash,
build, or configuration failure is a hard stop. Do not proceed to hardware,
delete outputs, alter evidence, or choose another ID after failure.

### 1. Reserve fresh output destinations

Run before any build or hardware action:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace.junit.xml
```

If any check fails, preserve the existing path. Do not delete, overwrite,
rename, or substitute it.

### 2. Validate fixture and run host checks

Fixture validation must report the local nRF54L15 fixture and no capture
capability, without hardware mutation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Run host checks before the trace build:

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/hci-remove-iso-path-trace-unit \
  tests/unit/hci_remove_iso_path_trace -p -t run

nix develop --command pytest -q tests/hil/rh2_test.py

python3 -m py_compile \
  scripts/hil/cli.py \
  scripts/hil/runner.py \
  scripts/hil/receiver.py

python3 scripts/check-test-matrix.py --repo-root .

git diff --check
git status --short
```

The native helper must remain 6/6, pytest must report 163 passed, and the
matrix checker must report 0 errors and 0 notes. Preserve the intentionally
dirty worktree shown by `git status --short`.

### 3. Verify normal and source images before trace build

The current normal nRF54L15 image, not historical `d8e570...`, must be present
before the temporary trace build:

```bash
sha256sum --check <<'EOF'
27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
```

Do not build the source images in this handoff. Their verified hashes are the
source-image input to the runner.

### 4. Build exact trace receiver image

Only after normal and source hash checks pass, run this exact trace build. The
helper supplies its own CMake separator, so do not add another user `--`:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-hci-remove-iso-path.conf"
```

### 5. Verify trace image and resolved configuration

After the trace build, verify the source images, trace receiver image, trace
FLPR image, and fragment before permitting the runner:

```bash
sha256sum --check <<'EOF'
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
f32fd4452f943bedbacaba4bb8a305c1489ac7d9e4c5c39faa874b9d410f813c  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
33f7c2fb49773e8811dfabf09471b6670414bfb5fc2d3fdee1a4f46182b50dc9  tests/hil/receiver-hci-remove-iso-path.conf
EOF

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
test -f "$trace_config"
for expected in \
  'CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_LOG_RUNTIME_FILTERING=y' \
  'CONFIG_BT_HCI_CORE_LOG_LEVEL=4' \
  'CONFIG_BT_HCI_DRIVER_LOG_LEVEL=4' \
  'CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y' \
  'CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL=3' \
  '# CONFIG_LOG_CMDS is not set'; do
  grep -Fx "$expected" "$trace_config"
done
```

All required lines must be present exactly. `CONFIG_LOG_CMDS` must remain
unset. Do not start the runner until image hashes and resolved configuration
both pass.

### 6. Recheck ownership immediately before runner

Repeat the fresh-output gate immediately before the single hardware command:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace.junit.xml
```

## Single runner-owned execution

Use terminal-tool outer timeout `7200000` ms. Do not use shell `timeout`.
Execute exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260823-13-callback-timed-hci-remove-iso-path-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --hci-remove-iso-path-trace
```

The runner alone owns flash, reset, serial capture, pairing, HCI/BlueZ
interaction, source configuration and streaming, evidence capture, warning
scan, and cleanup. No manual serial, `serial-mcp`, flash, reset, OpenOCD,
pairing, RF or power action is permitted. No manual HCI or BlueZ command is
permitted. No shell `log` command is permitted, and `CONFIG_LOG_CMDS` must not
be enabled. Do not interrupt the runner or launch a second process while it is
active. Let runner-owned cleanup finish even when its result is failed.

## Evidence review

Preserve every artifact exactly as written. Do not repair, rename, copy over,
delete, or manually complete evidence. First validate the runner checksum:

```bash
sha256sum --check \
  /tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace/SHA256SUMS
```

Read-only review must include, when present:

- `result.json`, external JUnit, `junit.xml`, `MANIFEST.md`, `images.json`, and
  `summary.json`;
- `hci-remove-iso-path-trace.json`;
- `receiver-console.bin` and `receiver-console-tx.bin`;
- `commands.jsonl`;
- `source-records.jsonl`;
- source and receiver flash logs and all retained raw console records.

Report exact observations, not inferred causes:

- runner exit status, retained result, first failed boundary, cleanup failures,
  checksum result and artifact counts;
- exactly one successful arm marker, its nonnegative IDs, and both level `4`
  values;
- any arm-failure marker, which must be reported at the `hci remove iso path
  trace evidence` boundary;
- every dropped-message record, classified before or after the arm marker;
- the post-arm `bt_hci_core` send for opcode `0x206f`, and any pre-arm send that
  must not be counted as the target send;
- `bt_sdc_hci_driver` Command Complete or Command Status evidence for
  `0x206f`, if any;
- `bt_hci_core` completion or done evidence for `0x206f`, if any;
- host timeout or fatal evidence mentioning `0x206f`, if any;
- strict unrelated warning and error records, source records, command ledger,
  and exact image and fragment hashes.

Absence of a controller completion, driver status, core done record, or host
timeout is an observation only. Do not infer a controller defect, source cause,
RF cause, application cause, root cause, audio result, or audibility from an
absent record. A nonzero result is evidence, not permission for another run.

## Local normal image restoration

After runner-owned cleanup and read-only evidence review, restore only the
local normal nRF54L15 build. Do not flash or reset hardware. Do not rebuild the
source images or nRF5340 image. The old `d8e570...` identity is not a restore
target.

```bash
nix develop --command fw-build-54l15

sha256sum --check <<'EOF'
27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

The config-off normal result must show the trace gate unset, HCI core and
driver log levels `3`, and no trace source in the map or generated autoconf:

```bash
normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
normal_map=build/nrf54l15/le-audio-receiver/zephyr/zephyr.map
normal_autoconf=build/nrf54l15/le-audio-receiver/zephyr/include/generated/zephyr/autoconf.h

test -f "$normal_config"
test -f "$normal_map"
test -f "$normal_autoconf"
for expected in \
  '# CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  'CONFIG_BT_HCI_CORE_LOG_LEVEL=3' \
  'CONFIG_BT_HCI_DRIVER_LOG_LEVEL=3'; do
  grep -Fx "$expected" "$normal_config"
done
! grep -Fq 'hci_remove_iso_path_trace' "$normal_map"
! grep -Fq 'hci_remove_iso_path_trace' "$normal_autoconf"
! grep -Fq 'HIL_HCI_REMOVE_ISO_PATH_TRACE' "$normal_autoconf"
```

No repository cleanup or Git operation follows restoration. Do not commit,
push, merge, tag, release, reset, restore, stash, or modify `STATUS.md`,
previous handoffs, result documents, evidence, or source.
