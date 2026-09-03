# RH3-40 TX-notify workqueue link-proof repair handoff

Status: focused proof correction after H40 trace image built successfully. Do
not run hardware in this phase.

## Completed evidence

After the RAM-fit repair, the H40 trace build passed:

```text
CPUAPP RAM: 163736 / 163840 B (99.94%)
FLPR RAM:    43632 / 65536 B (66.58%)
CPUAPP SHA256: c487f021a0ff29076e0a1b312a6826b4f2007623098f22c34fa19358f1bedb52
FLPR SHA256:   45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2
```

The resolved trace config proved:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_TX_PROCESSOR_THREAD=y
CONFIG_BT_ISO_RX_BUF_COUNT=3
# CONFIG_WARN_EXPERIMENTAL is not set
```

`nm` found `conn_tx_workq`, `conn_tx_workq_thread_stack`,
`bt_conn_tx_workq_init`, and every required RH3-39 wrapper. The normal local
nRF54L15 build was restored and all normal/source hashes passed.

## Blocker correction

The earlier command required this fragile match:

```bash
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg 'conn_tx_workq'
```

It produced no text. This does not disprove the configuration. ARM linked
disassembly does not necessarily annotate a data-address literal with its local
symbol name, even when the compile-time `#if` branch selected it.

The proof must instead use all three grounded facts:

1. Resolved `.config` has `CONFIG_BT_CONN_TX_NOTIFY_WQ=y`.
2. Exact installed `conn.c:283-290` selects `&conn_tx_workq` under that
   compile-time condition; the `&k_sys_work_q` branch is excluded.
3. Linked ELF `nm` contains the private queue object, its stack, and
   `bt_conn_tx_workq_init`; `conn.c:4652-4669` initializes and starts that
   exact queue.

Disassemble the private queue initializer, where call symbols are stable, not
the data-address selection in `bt_conn_tx_notify()`.

## Exact scope

Touch only:

```text
docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md
docs/development/system-hil-rh3-40-tx-notify-workqueue-link-proof-fix-handoff.md
```

The main H40 handoff must replace only its failed `bt_conn_tx_notify` objdump
match with:

```bash
"$toolchain_objdump" -d --disassemble=bt_conn_tx_workq_init "$trace_elf" | \
  rg 'k_work_queue_(init|start)'
```

Do not change source, Kconfig, CMake, trace fragment, parser, tests, HIL
runner, source images, board config, `STATUS.md`, or public docs.

## Verification

The trace fragment and source did not change. Prior native `18/18`, RH2 `224`,
Python compile, matrix, and `git diff --check` results remain valid. Rebuild the
H40 trace image only to obtain a fresh ELF, run corrected proof, then restore
normal local output regardless of result.

```bash
git diff --check

nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf"

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx 'CONFIG_BT_CONN_TX_NOTIFY_WQ=y' "$trace_config"
rg -Fx 'CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536' "$trace_config"
rg -Fx 'CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8' "$trace_config"
rg -Fx 'CONFIG_BT_RECV_WORKQ_BT=y' "$trace_config"
rg -Fx 'CONFIG_BT_TX_PROCESSOR_THREAD=y' "$trace_config"

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | rg 'conn_tx_workq|conn_tx_workq_thread_stack|bt_conn_tx_workq_init|__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|__wrap_z_impl_k_sem_give'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_workq_init "$trace_elf" | \
  rg 'k_work_queue_(init|start)'
sha256sum build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex

nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx '# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set' "$normal_config"
rg -Fx 'CONFIG_WARN_EXPERIMENTAL=y' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF

git diff --check
git status --short
```

If any trace proof or rebuild fails, restore normal build first, then stop and
report exact output. Do not use hardware, change design, stage, or commit.

## Return report

Return exact corrected proof output, trace/source hashes, normal restoration,
git status, and no-hardware/no-commit confirmation. A successful software phase
only prepares a separate H40 physical execution handoff; it does not authorize
that execution or a production configuration change.
