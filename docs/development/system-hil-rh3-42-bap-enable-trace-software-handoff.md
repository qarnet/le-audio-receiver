# RH3-42 BAP enable-callback trace software handoff

Status: planned software-only discriminator. Do not run hardware.

## Goal

Build one small nRF54L15 receiver diagnostic image that keeps H41's untraced
private TX-notify workqueue and emits one low-volume marker only when receiver
`stream_ops.enabled` returns from `bt_bap_stream_start()`.

H41 reached receiver `lc3_enable()` but source never observed remote enabled
completion. H41 retained no `Stream[0] started` log. Existing receiver code
only logs a start error, so it cannot distinguish these two facts:

1. receiver `stream_enabled_cb()` never ran; or
2. it ran and `bt_bap_stream_start()` returned a result, while source still did
   not observe remote enabled completion.

This phase adds one gated marker to distinguish those branches in a later,
separately reviewed physical execution. It does not diagnose a root cause,
repair H41, change production behavior, or authorize workqueue adoption.

## Grounding

Canonical H41 failure evidence:

```text
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-result.md
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/
```

H41 facts:

- Receiver logged `Enable: stream[0] meta_len 4` and LC3 decoder setup.
- Source remained in `qos`, then aborted with `cause=timeout` and
  `first_errno=-116`.
- Source never reached `streaming`; receiver retained no `Stream[0] started`
  log.
- H41 used `CONFIG_BT_CONN_TX_NOTIFY_WQ=y`, 1536-byte private stack, no trace,
  and only 316 bytes of measured CPUAPP RAM margin.

Installed NCS v3.3.0 source establishes this exact order:

1. `zephyr/subsys/bluetooth/audio/ascs.c:2327-2385` calls receiver
   `bt_bap_unicast_server_cb.enable`, then schedules ASE `ENABLING` state.
2. `ascs.c:513-605` attempts ASE status notification before invoking
   `stream_ops.enabled` through `ase_enter_state_enabling()`.
3. `src/bt_bap.c:1180-1187` currently calls `bt_bap_stream_start()` but logs
   only a nonzero return.
4. `zephyr/subsys/bluetooth/audio/bap_stream.c:893-931` routes a peripheral
   sink to `bt_bap_unicast_server_start()`.
5. `zephyr/subsys/bluetooth/audio/bap_unicast_server.c:130-156` returns
   immediately after either setting `receiver_ready` or scheduling streaming
   state if ISO already connected. It does not block on peer observation.
6. Source `hil/source/app/src/hil_source_bap.c:492-499` gives `sem_enabled`
   only from its remote `stream_ops.enabled` callback. Source coordinator
   `hil/source/app/src/hil_source_app.c:935-956` waits that semaphore before
   attempting CIS connect.

Therefore one post-return receiver marker is sufficient to prove callback
entry-and-return with exact start result. It does not prove peer delivery,
on-air ordering, workqueue causation, or root cause.

## Scope

### In scope

1. Add one default-off nRF54L15 HIL Kconfig gate.
2. Add one gated receiver log marker after `bt_bap_stream_start()` returns.
3. Add one H42-only configuration fragment, preserving H41 private workqueue
   settings and keeping broad trace instrumentation disabled.
4. Build and prove exact diagnostic linkage/configuration in existing
   `build/nrf54l15`.
5. Restore normal local nRF54L15 output and prove it remains byte-identical.
6. Add one factual software-result document after every required check passes.

### Out of scope

- Hardware, HIL execution, flashing, reset, recovery, erase, RF, pairing,
  serial, debugger, nrf-probes, btattach, source image build, source fixture,
  runner, parser, rows, thresholds, test matrix, or NCS source changes.
- `prj.conf`, board configuration, queue priority/stack policy, ISO pool depth,
  trace implementation, production logging, public docs, `STATUS.md`,
  acceptance claims, or production adoption.
- Retrying H40/H41, allocating a physical H42 run ID, copying/deleting evidence,
  Nix garbage collection, staging, committing, pushing, merging, release, or
  remote actions.

## Exact implementation

Touch only these implementation files:

```text
Kconfig
src/bt_bap.c
tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf
docs/development/system-hil-rh3-42-bap-enable-trace-software-result.md
```

### Kconfig

Immediately before existing `HIL_HCI_REMOVE_ISO_PATH_TRACE` in root `Kconfig`,
add exactly one symbol:

```kconfig
config HIL_BAP_ENABLE_TRACE
	bool "HIL BAP enable callback trace"
	default n
	depends on SOC_NRF54L15 && BT && LOG
	help
	  Temporary nRF54L15 HIL-only marker after the receiver stream enabled
	  callback returns from bt_bap_stream_start(). It records the stream slot
	  and return code for one pre-stream diagnostic branch. Keep disabled in
	  normal builds. It does not establish peer delivery, a root cause, or a
	  production repair.
```

Do not select other symbols. Do not add a menu, source-file split, wrapper,
runtime filter, counter, shell command, or source-fixture protocol record.

### Receiver marker

In `src/bt_bap.c`, modify only `stream_enabled_cb()`.

Keep `int err = bt_bap_stream_start(s);` first. Directly after it, add this
compile-time-gated marker:

```c
#if defined(CONFIG_HIL_BAP_ENABLE_TRACE)
	LOG_INF("HIL BAP enable: stream[%zu] start=%d", sink_idx(s), err);
#endif
```

Keep existing nonzero-error `LOG_ERR` and all behavior unchanged. The marker
must appear after return, not before call, so one line proves callback return
and carries exact API result. It must not mutate state, schedule work, block,
allocate, change an error, or alter lifecycle order. Normal builds must compile
the marker out completely.

### H42 fragment

Create `tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf` with exactly
this content and order:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_HIL_BAP_ENABLE_TRACE=y
CONFIG_WARN_EXPERIMENTAL=n
```

Do not set queue priority or init priority. Do not enable `CONFIG_TRACING`, any
existing HCI/SDC trace option, runtime filtering, source logging, or a source
protocol extension.

## Required validation

This phase reuses only `build/nrf54l15`. All commands run sequentially from
repository root. Do not start a hardware action. Treat every actionable warning
as a hard error. Only existing documented nRF54L15 watchdog empty-library and
global `__ASSERT()` informational diagnostics are allowed. The fragment's
experimental-warning suppression is intentional and diagnostic-only.

Before any build and after normal restoration:

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
git diff --check
```

Build diagnostic image:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf"
```

Then prove exact config and compiled marker:

```bash
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
rg -n '_image_ram_end|_image_ram_size' build/nrf54l15/le-audio-receiver/zephyr/zephyr.map
sha256sum \
  tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Record exact diagnostic hashes and RAM endpoint/margin. Do not invent expected
diagnostic values. If the diagnostic image does not fit, has any wrong config
line, lacks the marker, lacks private-workqueue proof, or has an unexpected
source-image hash, restore normal output and stop. Do not touch hardware.

Restore normal local output:

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

No new unit test is required for this configuration-gated one-line marker. The
two real target builds prove enabled and compiled-out configurations; later H42
execution is separate public-boundary evidence. Do not add fragile tests of a
private static callback, logger-call count, or Kconfig text.

## Software result document

Create `docs/development/system-hil-rh3-42-bap-enable-trace-software-result.md`
only after all requirements pass. State:

- software-only scope and no physical H42 run;
- exact fragment/config/link/marker/RAM/hash facts observed;
- normal byte-identical restoration and marker absence;
- disk results;
- no production claim, no retry/adoption conclusion, and need for separate
  reviewed physical-execution plan.

Do not update `STATUS.md`, public docs, H41 result, or restart-state counts in
this software phase.

## Return report

Return changed paths; exact design applied; diagnostic build/config/link/marker
proof; observed RAM endpoint/margin and hashes; normal-restoration proof; disk
before/after; final status; no-hardware/no-commit confirmation; and blockers or
deviations. Stop and escalate before inventing a workaround for any build,
warning, hash, or RAM-fit failure.
