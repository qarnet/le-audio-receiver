# RH3-41 untraced TX-notify workqueue software handoff

Status: planned software-only feasibility phase. Do not run hardware.

## Goal

Prepare and build one no-trace diagnostic receiver image with Zephyr's private
connection TX-notify workqueue enabled. This separates the H40 workqueue
experiment from H40's trace instrumentation before any later physical plan.

This phase proves only that exact diagnostic configuration builds, links, and
restores local normal output. It does not prove production safety, a root cause,
audio health, or a production configuration choice.

## Evidence and design decision

H40 completed one fresh Mode B `48_3_1` diagnostic row successfully with a
trace-only private workqueue. Canonical result:

```text
docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/
```

H40's `1536`-byte private queue stack was physically exercised once, but H40
also enabled trace instrumentation and linked with only 104 bytes of CPUAPP RAM
margin. It cannot establish behavior without that instrumentation.

Installed NCS v3.3.0 grounds this diagnostic configuration:

```text
zephyr/subsys/bluetooth/host/Kconfig:160-187
  CONFIG_BT_CONN_TX_NOTIFY_WQ is experimental, depends on BT_CONN_TX,
  defaults its stack to SYSTEM_WORKQUEUE_STACK_SIZE, and defaults priority to 8.

zephyr/subsys/bluetooth/host/conn.c:283-290,4652-4669
  The enabled option selects one static conn_tx_workq and starts it at
  cooperative priority 8.
```

The current normal nRF54L15 image has only 2148 bytes between `_image_ram_end`
`0x2002779c` and RAM limit `0x20028000`. The upstream default 2048-byte stack
plus H40's observed 0x128-byte queue object cannot be assumed to fit. Reuse the
H40-tested diagnostic stack size of 1536 bytes. This is not a production stack
decision.

## Scope

### In scope

1. Create one untraced diagnostic Kconfig fragment.
2. Build that fragment once into the existing `build/nrf54l15` directory.
3. Prove exact resolved configuration, private-workqueue linkage, image hashes,
   and final RAM endpoint.
4. Restore normal local nRF54L15 build output and prove normal hashes.
5. Create one factual software-result document from observed outputs.

### Out of scope

- Hardware, HIL runner, flash, reset, recovery, erase, serial, debugger, RF,
  pairing, or source-image rebuild.
- Production `prj.conf`, board config, queue priority, pool depth, source,
  controller, NCS source, test matrix, runner, parser, build contract,
  `STATUS.md`, public docs, or coverage changes.
- Retrying H40, creating H41 physical evidence, choosing a production stack
  size, or claiming acceptance.
- Nix garbage collection, deleting evidence/build directories, staging,
  committing, pushing, merging, or remote actions.

## Exact changes

Create exactly:

```text
tests/hil/receiver-conn-tx-notify-wq-untraced.conf
```

with exactly this content and order:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_WARN_EXPERIMENTAL=n
```

Do not set queue priority or init priority. Installed defaults must resolve to
`CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8` and
`CONFIG_BT_CONN_TX_NOTIFY_WQ_INIT_PRIORITY=50`.

Create a second new file only after successful verification:

```text
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-software-result.md
```

It must state:

- no-trace diagnostic build outcome and no-hardware scope;
- exact fragment hash and observed CPUAPP, FLPR, and unchanged source hashes;
- exact resolved workqueue, stack, priority, init-priority, warning, ISO-pool,
  and trace-disabled lines;
- observed `_image_ram_end` and remaining bytes to `0x20028000` without calling
  that margin production-safe;
- private-workqueue `nm` and initializer disassembly proof;
- normal-build restoration proof with exact normal hashes;
- explicit statement that a later physical run needs a separate reviewed plan.

Do not fabricate a hash or RAM value. If build or proof fails, restore normal
output first, then stop and report. Do not create a success result document.

## Disk budget

`/`, `/tmp`, and `/nix/store` share 102 GiB free at handoff start. Reuse only
the existing `build/nrf54l15` output path. Do not create another build tree.

Before the diagnostic build and after normal restoration, run:

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
```

Stop before building if it fails. Do not free space by deleting files or running
garbage collection.

## Verification

Run sequentially from repository root. Do not overlap commands.

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
git diff --check

nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-conn-tx-notify-wq-untraced.conf"
```

Treat actionable warnings as errors. The only allowed build diagnostics are the
repository-documented nRF54L15 watchdog empty-library diagnostic and global
`__ASSERT()` information. The fragment suppresses only the known experimental
symbol warning for this diagnostic image.

On build success, prove all lines:

```bash
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
sha256sum \
  tests/hil/receiver-conn-tx-notify-wq-untraced.conf \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Always restore normal output after successful diagnostic proof:

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

## Return report

Return changed paths, diagnostic-build result, exact resolved configuration,
link proof, observed RAM endpoint/margin and hashes, normal-restoration proof,
disk space before and after, final status, no-hardware/no-commit confirmation,
and blockers or deviations. Do not make a physical-execution recommendation
beyond saying whether the software result is ready for review.
