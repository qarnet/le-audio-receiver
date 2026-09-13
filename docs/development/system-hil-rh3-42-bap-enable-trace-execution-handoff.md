# RH3-42 BAP enable-callback trace execution handoff

Status: approved one-run physical diagnostic. This is not production
configuration, RH3 acceptance, or a retry of H40/H41.

## Goal and boundary

Run exactly one runner-owned fresh Mode B `48_3_1` row with H42's untraced
private TX-notify workqueue plus one receiver callback-return marker:

```text
HIL BAP enable: stream[<slot>] start=<return-code>
```

The marker is emitted only after receiver `stream_enabled_cb()` returns from
`bt_bap_stream_start()`. It distinguishes a returned receiver callback from
H41's missing observation. It does not prove ASCS peer delivery, on-air packet
order, workqueue causation, root cause, audio health, general safety, or a
production repair.

H41 must not be retried. H42 gets exactly one new run ID and one physical
execution regardless of outcome.

## Fixed identity and inputs

```text
run ID:           rh3-20260903-42-bap-enable-trace
run ID length:    32
row:              rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
external JUnit:   /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml
```

At handoff preparation, `validate_run_id()` passed and both output paths were
absent and non-symlinks. Recheck immediately before runner invocation. If any
path exists, is a symlink, or preflight fails, stop without substituting another
ID or touching hardware.

The diagnostic source inputs are exact:

```text
fragment: tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf
fragment SHA-256: 065162a7d152530ea511294b71a58f9d2488febd483718258efef0aa7b98c9f7

diagnostic receiver CPUAPP: 533f2f82f48e2e5d073eb419ddbb682acd24de838fa10d9f0e2627067dcd0e8a
receiver FLPR:             45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
source app:                f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333
source CPUNET:             4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48

normal local receiver CPUAPP: e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f
```

The already accepted software result is:

```text
docs/development/system-hil-rh3-42-bap-enable-trace-software-result.md
```

## Scope

### In scope

1. Read-only preflight and disk gate.
2. One H42 diagnostic receiver build in existing `build/nrf54l15` output path.
3. One runner-owned physical execution.
4. Read-only integrity and marker review of new immutable evidence.
5. One normal local nRF54L15 build restoration.

### Out of scope

- Production source/configuration changes, queue priority/stack changes, pool
  changes, controller/NCS changes, source image build, source fixture, runner,
  parser, row, threshold, test, build contract, `STATUS.md`, public docs, or
  coverage changes.
- Manual serial/debugger/OpenOCD/nrf-probes, manual flash/reset/recovery/erase,
  RF/Bluetooth actions, btattach, or bap_central.py.
- Trace flags, H40/H41 retry, a second H42 run, another run ID, evidence
  mutation, deletion, cleanup, Nix garbage collection, staging, commit, push,
  merge, PR, tag, release, or remote action.

Only `scripts/hil-runner.py` may change attached targets. It owns target
identity resolution, flashing, console capture, source control, cleanup, and
evidence finalization. Its raw identity evidence is authoritative.

## Disk and normal-output invariants

Use existing `build/nrf54l15` only. Before diagnostic build and after normal
restoration, require `>=80 GiB` free. Current observed free space is
`120.0 GiB`. If gate fails, stop. Do not delete files or run garbage collection.

Normal local output must be restored and hashes must match exactly after this
phase. Local rebuild does not prove board image state.

## Ordered preflight

Run sequentially from repository root. Treat actionable diagnostics as errors.
Allowed build diagnostics are only documented dirty-tree notice, nRF54L15
watchdog empty-library diagnostic, and global `__ASSERT()` information.

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
git diff --check

python3 -c 'import sys; sys.path.insert(0, "scripts"); from hil.lifecycle import validate_run_id; run_id = "rh3-20260903-42-bap-enable-trace"; validate_run_id(run_id); print(f"run_id={run_id}\nlength={len(run_id)}\nvalid=yes")'
test ! -e /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json

sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
065162a7d152530ea511294b71a58f9d2488febd483718258efef0aa7b98c9f7  tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf
EOF
```

Any failed preflight is a hard stop. Do not touch targets. If a later diagnostic
build has replaced local normal output, restore normal output before reporting
failure.

## Build and prove exact H42 diagnostic image

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf"

diag_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ=y' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_INIT_PRIORITY=50' \
  'CONFIG_HIL_BAP_ENABLE_TRACE=y' \
  '# CONFIG_WARN_EXPERIMENTAL is not set' \
  '# CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  '# CONFIG_TRACING is not set' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  'CONFIG_BT_RECV_WORKQ_BT=y' \
  'CONFIG_BT_TX_PROCESSOR_THREAD=y'; do
  rg -Fx "$expected" "$diag_config"
done

diag_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
"$toolchain_nm" -A "$diag_elf" | rg 'conn_tx_workq|conn_tx_workq_thread_stack|bt_conn_tx_workq_init'
strings "$diag_elf" | rg -F -x 'HIL BAP enable: stream[%zu] start=%d'
sha256sum --check <<'EOF'
533f2f82f48e2e5d073eb419ddbb682acd24de838fa10d9f0e2627067dcd0e8a  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
```

Wrong config, missing marker/linkage, unexpected source hash, diagnostic hash
mismatch, warning, or build failure: restore normal local output and stop. Do
not run hardware.

## Exact one-time physical execution

Immediately before runner invocation, repeat all four output ownership checks.
Run no build or target-changing command between those checks and runner. Use an
outer tool timeout of `7200000` ms. Do not use shell `timeout`.

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml

set +e
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260903-42-bap-enable-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1
runner_status=$?
set -e
printf 'runner_status=%s\n' "$runner_status"
```

Status `0`, `1`, or `130` is immutable evidence. Let runner cleanup and
finalization finish. Never rerun H42. Do not perform manual target action
afterward.

## Read-only evidence review

Verify integrity regardless of row outcome. Do not write into evidence root.

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)

python3 - "$RUN_DIR/receiver-console.bin" <<'PY'
import json
import re
import sys

raw = open(sys.argv[1], 'rb').read()
text = raw.decode('utf-8', errors='replace')
matches = re.findall(r'HIL BAP enable: stream\[(\d+)\] start=(-?\d+)', text)
print(json.dumps({'h42_enable_markers': matches}, sort_keys=True))
PY
```

Read `result.json`, JUnit, `MANIFEST.md`, `images.json`, `identity.json`, source
records, receiver console, source console, flash logs, and command ledger. Keep
raw marker extraction exact.

Interpretation limits:

- One marker with `start=0` proves only receiver callback return success. It
  does not prove source received enabled state or isolate a transport cause.
- One marker with a negative value proves only exact local start return error.
- Missing marker is inconclusive: callback could be absent or marker could be
  unretained. It does not prove a receiver root cause.
- More than one marker is retained evidence. Do not choose a preferred one or
  rerun H42.

No result document is written during this execution phase. Preserve evidence
for separate review and documentation.

## Normal restoration

After read-only evidence review, restore only local normal nRF54L15 output:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  '# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set' \
  '# CONFIG_HIL_BAP_ENABLE_TRACE is not set' \
  '# CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  '# CONFIG_TRACING is not set' \
  'CONFIG_WARN_EXPERIMENTAL=y' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3'; do
  rg -Fx "$expected" "$normal_config"
done

normal_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
if strings "$normal_elf" | rg -F -x 'HIL BAP enable: stream[%zu] start=%d'; then
  printf '%s\n' 'H42 marker leaked into normal image' >&2
  exit 1
fi
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

git diff --check
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
```

Local rebuild does not reflash boards. Report live board state as unknown after
runner cleanup unless retained evidence proves it.

## Return report

Return preflight/build proof; exact runner command/status; outcome/boundary/
detail/cleanup; integrity output; raw identity evidence; exact H42 marker
extraction; source/receiver terminal observations; normal restoration; disk
before/after; final status; no-manual-hardware/no-commit confirmation; blockers
or deviations. Do not claim production adoption, acceptance, or causal repair.
