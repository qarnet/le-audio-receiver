# RH3-41 untraced TX-notify workqueue execution handoff

Status: approved one-run diagnostic execution. This experiment has no trace
instrumentation and does not change production configuration.

## Goal and boundary

Run exactly one runner-owned fresh Mode B `48_3_1` row with the H41 untraced
private TX-notify workqueue image. Compare its raw row outcome with H40 only as
one bounded observation. A pass does not prove production safety, root cause,
audio health, general workload safety, or permission to enable this experimental
option in production. A failure is evidence, not permission to retry or repair.

H40 passed one traced run. H41 software preparation built the no-trace image,
proved the private queue linkage, and measured only 316 bytes of RAM margin.
Those facts justify this one diagnostic execution, not production adoption.

## Fixed identity and inputs

```text
run ID:           rh3-20260830-41-tx-notify-wq-untraced
run ID length:    37
row:              rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced
external JUnit:   /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced.junit.xml
```

The output paths were read-only verified unused before this handoff. Recheck
them immediately before the runner. If either path exists or is a symlink, stop
without substitution. Never overwrite, delete, or retry this run ID.

H41 fragment and expected image hashes:

```text
fragment: tests/hil/receiver-conn-tx-notify-wq-untraced.conf
fragment SHA-256: 7c096e12c276d2e9e3e6c65f27e3564c5df10b8e6d0e708ceda81cba9ace85e7

diagnostic receiver CPUAPP: f2db9de95a17db47792f9601de033e9db4e851ae534df6fa00fdbd8fa7fcbd18
receiver FLPR:             45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
source app:                f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source CPUNET:             4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

normal local receiver CPUAPP: e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f
```

No HCI trace option appears in the H41 runner command. The resulting evidence
must not be read as trace evidence.

## Scope

### In scope

1. Read-only preflight and disk gate.
2. One H41 diagnostic receiver build in existing `build/nrf54l15` directory.
3. One runner-owned physical execution.
4. Read-only integrity review of its newly created evidence.
5. One normal local nRF54L15 build restoration.

### Out of scope

- Production source/configuration changes, including `prj.conf`, board files,
  queue priority or stack changes, pool changes, controller/NCS/source changes,
  tests, parser, runner, build contract, `STATUS.md`, public docs, or coverage.
- Source image rebuild/replacement, manual serial/debugger/OpenOCD/nrf-probes,
  manual flash/reset/recovery/erase/RF/Bluetooth actions, `btattach`, or
  `bap_central.py`.
- Trace flags, H40 retry, a second H41 execution, another run ID, evidence
  mutation, deletion, cleanup, Nix garbage collection, staging, commit, push,
  merge, PR, tag, release, or remote action.

Only `scripts/hil-runner.py` may change attached targets. It owns identity
resolution, flashing, console capture, source control, cleanup, and evidence
finalization. Its recorded identity evidence is authoritative for this run.

## Disk gate

All relevant paths share one filesystem. Use existing `build/nrf54l15` only.
Before any build and after normal restoration, require at least 80 GiB free:

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
```

Current observed free space is about 101 GiB. If the gate fails, stop. Do not
delete files or run garbage collection to make room.

## Ordered preflight

Run sequentially from repository root. Treat actionable diagnostics as errors.
Allowed build diagnostics are only the documented dirty-tree notice, nRF54L15
watchdog empty-library diagnostic, and global `__ASSERT()` information.

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
git diff --check

python3 -c 'import sys; sys.path.insert(0, "scripts"); from hil.lifecycle import validate_run_id; run_id = "rh3-20260830-41-tx-notify-wq-untraced"; validate_run_id(run_id); print(f"run_id={run_id}\nlength={len(run_id)}\nvalid=yes")'
test ! -e /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced
test ! -L /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced
test ! -e /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced.junit.xml

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json

sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
7c096e12c276d2e9e3e6c65f27e3564c5df10b8e6d0e708ceda81cba9ace85e7  tests/hil/receiver-conn-tx-notify-wq-untraced.conf
EOF
```

Any failed preflight is a hard stop. Do not touch targets. If a later build has
replaced normal local output, restore it before reporting the failure.

## Build and prove exact H41 diagnostic image

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-conn-tx-notify-wq-untraced.conf"

diag_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ=y' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_INIT_PRIORITY=50' \
  '# CONFIG_WARN_EXPERIMENTAL is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  '# CONFIG_TRACING is not set' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  'CONFIG_BT_RECV_WORKQ_BT=y' \
  'CONFIG_BT_TX_PROCESSOR_THREAD=y'; do
  rg -Fx "$expected" "$diag_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
diag_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$diag_elf" | rg 'conn_tx_workq|conn_tx_workq_thread_stack|bt_conn_tx_workq_init'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_workq_init "$diag_elf" | \
  rg 'k_work_queue_(init|start)'
rg -n '_image_ram_end|_image_ram_size' \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.map
sha256sum --check <<'EOF'
f2db9de95a17db47792f9601de033e9db4e851ae534df6fa00fdbd8fa7fcbd18  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
```

If build, config, linkage, RAM, or hash proof fails, restore normal local output
and stop. Do not run hardware.

## Exact one-time physical execution

Immediately before runner invocation, repeat all four output ownership checks.
Run no build or other command between that check and this command. Use outer
tool timeout `7200000` ms. Do not use shell `timeout`.

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260830-41-tx-notify-wq-untraced \
  --junit /tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced.junit.xml \
  --row rh3.fresh_mode_b_48_3_1
```

Runner exit status `0`, `1`, or `130` is immutable evidence. Let cleanup and
finalization finish. Never retry. Do not perform manual target action afterward.

## Read-only evidence review and normal restoration

After runner return, verify integrity regardless of outcome:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```

Read `result.json`, JUnit, `MANIFEST.md`, `images.json`, `identity.json`, source
records, receiver status, console logs, flash logs, and command ledger. Do not
write H41 result documentation in this execution phase.

If integrity verification fails, do not interpret the evidence. Still restore
normal local output below, then stop and report the exact integrity failure.

Then restore only local normal nRF54L15 build output:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set' "$normal_config"
rg -Fx 'CONFIG_WARN_EXPERIMENTAL=y' "$normal_config"
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx '# CONFIG_TRACING is not set' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

git diff --check
git status --short
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
```

The local rebuild does not reflash hardware. Report hardware image state as
unknown after runner cleanup unless retained evidence proves a state.

## Return report

Return preflight and build result, exact config/link/hash proof, runner command
and exit status, result outcome/boundary/detail/cleanup, integrity output, raw
target identity evidence, source/receiver terminal values, normal restoration,
disk before/after, final status, no-manual-hardware/no-commit confirmation, and
blockers or deviations. Do not claim production adoption or acceptance.
