# RH3-41 untraced TX-notify workqueue software result

## Status and scope

One no-trace H41 diagnostic receiver build completed successfully. The build
used the existing `build/nrf54l15` output path, touched no hardware, and ran no
HIL execution. This result proves only that the exact diagnostic configuration
builds, links, and can be followed by normal local output restoration. It does
not prove production safety, a root cause, audio health, or a production
configuration choice.

## Exact fragment and image identities

The diagnostic fragment was:

```text
tests/hil/receiver-conn-tx-notify-wq-untraced.conf
SHA-256: 7c096e12c276d2e9e3e6c65f27e3564c5df10b8e6d0e708ceda81cba9ace85e7
```

Diagnostic image SHA-256 values were:

- Receiver CPUAPP: `f2db9de95a17db47792f9601de033e9db4e851ae534df6fa00fdbd8fa7fcbd18`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app, unchanged: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET, unchanged: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

No HIL source firmware build was run.

## Resolved diagnostic configuration

The receiver `.config` resolved these required lines:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8
CONFIG_BT_CONN_TX_NOTIFY_WQ_INIT_PRIORITY=50
# CONFIG_WARN_EXPERIMENTAL is not set
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_TRACING is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_TX_PROCESSOR_THREAD=y
```

The diagnostic build completed with only the documented dirty-tree notice,
nRF54L15 watchdog empty-library diagnostic, and global `__ASSERT()` diagnostic.
No actionable warning was observed.

## Link and RAM proof

The diagnostic CPUAPP linked with:

```text
zephyr.elf:0003c018 t bt_conn_tx_workq_init
zephyr.elf:200052a8 b conn_tx_workq
zephyr.elf:2001b258 b conn_tx_workq_thread_stack
zephyr.elf:00063a44 r __init_bt_conn_tx_workq_init
```

Disassembly of `bt_conn_tx_workq_init` contained both initializer calls:

```text
3c02a: f01d fbff  bl 5982c <k_work_queue_init>
3c03e: f7dc fcf7  bl 18a30 <k_work_queue_start>
```

The observed linker values were:

```text
_image_ram_end  = 0x20027ec4
_image_ram_size = 0x00027ec4 (163524 bytes)
RAM limit       = 0x20028000
remaining      = 0x13c (316 bytes)
```

The build summary reported `RAM: 163524 B / 160 KB (99.81%)`. The 316-byte
remaining margin is a diagnostic observation, not a production-safety claim.

## Normal restoration

The normal local nRF54L15 build completed after diagnostic proof. Its resolved
configuration showed:

```text
# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set
CONFIG_WARN_EXPERIMENTAL=y
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_TRACING is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

Restored normal and unchanged image hashes were:

- Normal receiver CPUAPP: `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

The restoration was local build output only. No board was flashed or otherwise
touched during this software-only phase.

## Disk and stop point

The disk gate reported `free_gib=101.3` before the diagnostic build and
`free_gib=101.3` after normal restoration. Both passed the `80 GiB` minimum.
Only the existing `build/nrf54l15` path was reused, with no extra build tree or
storage cleanup.

The completed physical execution is recorded in the [canonical H41 result](system-hil-rh3-41-tx-notify-workqueue-untraced-result.md).
