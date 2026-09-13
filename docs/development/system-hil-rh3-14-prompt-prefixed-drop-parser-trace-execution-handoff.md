# RH3-14 prompt-prefixed drop parser trace execution handoff

Status: one-use physical execution handoff. The user authorized one fresh
immutable trace run after host parser and native-helper warning repairs. This
handoff authorizes no retry, child run, replacement run, matrix run, or other
hardware action.

## Fixed execution identity

Run exactly one direct runner row:

```text
run ID: rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace.junit.xml
```

This is one new observation. It is not a retry or modification of RH3-13.
After the runner produces a result, preserve its output and do not execute a
second run under any ID without a separate reviewed authorization.

## Goal and evidence boundary

RH3-13 is immutable failed evidence at
`/tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace`.
Its result first failed at `session end` because receiver stream-summary slot
`0` was missing. Its trace parser also retained this pre-arm line as malformed:

```text
uart:~$ --- 276 messages dropped ---
```

Read-only checksum review of RH3-13 passed all 25 listed payloads when run
from its evidence directory. Do not reparse, rewrite, rename, or alter it.

The repaired parser now removes exactly one leading `uart:~$ ` only while
matching a dropped-message candidate. It retains the original line and raw
offsets. A prompt-prefixed pre-arm drop must be recorded as `before_arm`; a
prompt-prefixed post-arm drop must remain an `after_arm` trace validation
failure. Foreign prefixes and malformed drop records remain parser errors.

This run records callback-timed HCI Remove ISO Path evidence for opcode
`0x206f`. It must not be used to infer a controller, RF, source, receiver,
application, audio, or root cause from any present or absent record.

## Image and configuration identities

The approved current normal and source-image inputs are:

```text
27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

The trace fragment is fixed:

```text
33f7c2fb49773e8811dfabf09471b6670414bfb5fc2d3fdee1a4f46182b50dc9  tests/hil/receiver-hci-remove-iso-path.conf
```

Firmware source and the trace fragment have not changed since the reviewed
RH3-13 trace build. The exact trace build must therefore produce:

```text
f32fd4452f943bedbacaba4bb8a305c1489ac7d9e4c5c39faa874b9d410f813c  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Any hash mismatch is a hard stop before hardware. Do not rebuild the source
fixture or nRF5340 image in this phase.

## Scope and safety rules

In scope:

- host-only preflight;
- one nRF54L15 temporary trace build;
- one runner-owned direct HIL execution with the fixed ID and row;
- read-only evidence review after runner cleanup;
- local normal nRF54L15 build restoration only.

Out of scope:

- source, firmware, Kconfig, CMake, fixture, runner, parser, or test edits;
- source fixture rebuild or image replacement;
- a second HIL run, retry, matrix, or alternate ID;
- manual serial, `serial-mcp`, flash, reset, OpenOCD, probe, pairing, HCI,
  BlueZ, RF, power, shell `log` command, or `CONFIG_LOG_CMDS` enablement;
- evidence/history changes, `STATUS.md`, commits, staging, push, merge, tag,
  release, reset, stash, clean, or restore.

`scripts/hil-runner.py` is sole owner of fixture identity resolution, locks,
serial capture, flash/reset, source setup and streaming, pairing, HCI/BlueZ,
cleanup, and evidence finalization. It resolves hardware from the checked-in
fixture and gitignored binding, records raw identity evidence, and rejects
cross-wired or held endpoints. Do not write or rely on a static
probe-to-board mapping.

Run host tests and builds sequentially. Do not run `tests/hil` concurrently
with a source build. This handoff performs no source build.

Only documented NCS build diagnostics remain non-actionable: repository dirty
tree notice, `PARTITION_MANAGER` and sysbuild deprecation notices, informational
`__ASSERT()` notices, required SW Split experimental notices, the upstream
low-latency policy choice gap, and the known nRF54L15 watchdog empty-library
diagnostic. These are not blanket warning allowances. In particular, the native
helper must not emit `No SOURCES given to Zephyr library: drivers__entropy`, and
no other warning, Kconfig assignment diagnostic, or build error is acceptable.

## Ordered preflight

Run all commands from repository root. Each preflight failure is a hard stop:
do not touch hardware and do not create a substitute run. If the trace build
has already replaced the normal local build, restore and verify the normal build
before reporting the preflight failure.

### 1. Reserve fresh destinations

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace.junit.xml
```

If any path exists, preserve it and stop. Do not delete, overwrite, rename, or
choose another ID.

### 2. Run host checks and fixture validation

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/hci-remove-iso-path-trace-unit-rh3-14 \
  tests/unit/hci_remove_iso_path_trace -p -t run

native_config=/tmp/hci-remove-iso-path-trace-unit-rh3-14/zephyr/.config
test -f "$native_config"
grep -Fx '# CONFIG_ENTROPY_GENERATOR is not set' "$native_config"
grep -Fx 'CONFIG_FAKE_ENTROPY_NATIVE_SIM=n' \
  tests/unit/hci_remove_iso_path_trace/prj.conf

nix develop --command pytest -q tests/hil/rh2_test.py

python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
git status --short

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

The native helper must report 6/6 passing tests. Its resolved entropy parent
must be unset; the child fake-entropy symbol is intentionally absent from the
resolved config because the disabled parent hides it. `pytest` must report 165
passed. The matrix checker must report 0 errors and 0 notes. Fixture validation
must report `local-nrf54l15-receiver` with `capture_capability` `none`.

### 3. Verify normal and source inputs

```bash
sha256sum --check <<'EOF'
27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
```

### 4. Build and verify exact trace image

The helper supplies its own CMake separator. Do not add another user `--`.

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-hci-remove-iso-path.conf"

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

### 5. Recheck output ownership

Repeat the four checks from step 1 immediately before the single runner command.

## Single runner-owned execution

Use an outer terminal-tool timeout of 7200000 ms. Do not use shell `timeout`.
Run exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --hci-remove-iso-path-trace
```

Runner status 0, 1, or 130 is retained evidence. A nonzero status does not
authorize retry. Let runner-owned cleanup finish. Then perform only read-only
evidence review and local build restoration.

## Evidence review and normal restoration

Set:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260823-14-prompt-prefixed-drop-parser-hci-trace
```

Verify evidence from its own directory, because `SHA256SUMS` names are relative:

```bash
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read only `result.json`, external and internal JUnit, `MANIFEST.md`,
`images.json`, `summary.json` when present, `hci-remove-iso-path-trace.json`,
receiver/source console records, flash logs, source records, and command ledger.
Report exact runner status, outcome, first boundary, detail, cleanup failures,
checksum count, artifact count, image hashes, and warnings/errors.

For trace evidence, report:

- parser and validation errors;
- successful and failed arm markers, IDs, and levels;
- every drop record, its original line, count, offset, and before/after-arm
  classification;
- post-arm core `0x206f` sends and any pre-arm sends;
- driver Command Complete/Status, core completion/done, and host timeout/fatal
  records for `0x206f`;
- whether the trace object joined `summary.json`.

Absence of any completion, status, done, timeout, stream summary, or audio
record is only an observation. Do not infer cause, health, audibility, or
acceptance.

After runner cleanup and read-only review, restore only the local normal
nRF54L15 build. Do not flash or reset hardware during restoration:

```bash
nix develop --command fw-build-54l15

sha256sum --check <<'EOF'
27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF

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

## Executor return format

Return the new handoff path; host checks and warnings; trace-build hashes and
resolved configuration; exact runner command and status; immutable evidence
facts; checksum result; normal-build restoration proof; final git status; all
deviations/blockers. Do not commit or change evidence.
