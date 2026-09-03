# RH3-40 TX-notify workqueue RAM-fit repair handoff

Status: focused repair of the H40 software-only trace build. Do not run
hardware in this phase.

## Blocker

H40 host verification completed successfully:

```text
native SDC trace suite: 18/18 PASS
RH2 parser suite: 224 passed
Python compile: PASS
matrix: 0 errors, 0 notes
```

The exact H40 trace build then failed before final CPUAPP linking:

```text
zephyr/zephyr_pre0.elf section `noinit' will not fit in region `RAM'
region `RAM' overflowed by 408 bytes
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1
```

At this handoff's creation, local nRF54L15 build output contained only the failed H40 trace build; the repair must restore the normal image before reporting success or failure.

## Grounded RAM decision

The failed H40 pre-link map is:

```text
build/nrf54l15/le-audio-receiver/zephyr/zephyr_pre0.map
```

It proves:

```text
RAM limit:                         0x20028000
failed H40 _image_ram_end:         0x20028198
overflow:                          0x198 (408 bytes)
conn_tx_workq control object:      0x128 bytes
conn.c private noinit stack:       0x800 bytes (2048)
```

Installed NCS v3.3.0 defines the private queue stack through
`CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE`:

```text
zephyr/subsys/bluetooth/host/Kconfig:169-187
zephyr/subsys/bluetooth/host/conn.c:4652-4669
```

The symbol has a Kconfig prompt when `BT_CONN_TX_NOTIFY_WQ=y`, so a trace
fragment assignment is supported. Reducing only this trace-only queue stack
from 2048 to 1536 removes 512 bytes. With all other resolved H40 settings
unchanged, projected image end is `0x20027f98`, 104 bytes inside RAM.

`1536` is the largest simple reduction that restores fit. It remains a
diagnostic-only experimental stack size. It must not be copied to normal board
or application configuration, and it does not authorize a production adoption
of `BT_CONN_TX_NOTIFY_WQ`.

## Exact changes

Touch only these files:

```text
tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf
docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md
docs/development/system-hil-rh3-40-tx-notify-workqueue-ram-fit-fix-handoff.md
```

1. In the trace fragment, insert exactly this line immediately after
   `CONFIG_BT_CONN_TX_NOTIFY_WQ=y`:

   ```text
   CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
   ```

2. Correct the main H40 handoff so its exact fragment, rules, RAM rationale,
   and resolved-config assertion specify `1536`, not the upstream 2048-byte
   default.

Do not change `Kconfig`, `CMakeLists.txt`, production config, source, parser,
tests, HIL runner, source images, target configuration, `STATUS.md`, or public
documentation. Preserve all pre-existing dirty work.

## Required verification

The prior focused host tests remain valid because this repair changes only a
trace-fragment stack-size assignment. Run these checks and trace build from the
repository root. No hardware action.

```bash
git diff --check

nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf"
```

If the trace build fails for any reason, first restore the normal local
nRF54L15 build with `nix develop --command fw-build-54l15`, verify its hashes
below, then stop and report the failure. Do not choose another stack size.

On trace-build success, prove exact resolved settings and private queue linkage:

```bash
trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE=y' \
  'CONFIG_TRACING=y' \
  'CONFIG_TRACING_USER=y' \
  'CONFIG_TRACING_THREAD=y' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ=y' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536' \
  'CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8' \
  'CONFIG_BT_RECV_WORKQ_BT=y' \
  'CONFIG_BT_TX_PROCESSOR_THREAD=y' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  '# CONFIG_WARN_EXPERIMENTAL is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_NET_BUF_LOG is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | rg 'conn_tx_workq|bt_conn_tx_workq_init|__wrap_bt_conn_recv|__wrap_k_work_flush|__wrap_z_impl_k_sem_take|__wrap_z_pend_curr|__wrap_z_impl_k_sem_give'
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg 'conn_tx_workq'
sha256sum build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Then always restore normal local receiver output and prove it:

```bash
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

## Return report

Return changed paths, trace-build result, resolved config proof, trace CPUAPP
and FLPR hashes, source hashes, private-workqueue proof, normal-restoration
proof, final git status, no-hardware/no-commit confirmation, and blockers or
deviations. H40 physical execution is not authorized by this repair handoff.
