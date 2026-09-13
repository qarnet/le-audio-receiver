# RH3-42 BAP enable-callback trace software result

## Status and scope

One software-only H42 diagnostic receiver build completed successfully. It
reused `build/nrf54l15`, touched no hardware, and ran no physical H42
execution. This result proves only diagnostic configuration, linkage, marker
retention, RAM fit, and normal local output restoration. It makes no
production, root-cause, peer-delivery, or audio-health claim.

## Exact design

The implementation added one default-off Kconfig symbol immediately before
`HIL_HCI_REMOVE_ISO_PATH_TRACE`:

```text
CONFIG_HIL_BAP_ENABLE_TRACE
depends on SOC_NRF54L15 && BT && LOG
default n
```

`stream_enabled_cb()` keeps `int err = bt_bap_stream_start(s);` first. The
following marker is compiled only when `CONFIG_HIL_BAP_ENABLE_TRACE` is set:

```text
HIL BAP enable: stream[%zu] start=%d
```

Existing nonzero-error logging and behavior remain unchanged. H42 fragment
contents and SHA-256 were:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_HIL_BAP_ENABLE_TRACE=y
CONFIG_WARN_EXPERIMENTAL=n

SHA-256: 065162a7d152530ea511294b71a58f9d2488febd483718258efef0aa7b98c9f7
```

## Diagnostic proof

Diagnostic receiver configuration resolved all required lines:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_BT_CONN_TX_NOTIFY_WQ_PRIO=8
CONFIG_BT_CONN_TX_NOTIFY_WQ_INIT_PRIORITY=50
CONFIG_HIL_BAP_ENABLE_TRACE=y
# CONFIG_WARN_EXPERIMENTAL is not set
# CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_TRACING is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
CONFIG_BT_RECV_WORKQ_BT=y
CONFIG_BT_TX_PROCESSOR_THREAD=y
```

The diagnostic CPUAPP ELF contained the private workqueue proof:

```text
build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf:0003c04c t bt_conn_tx_workq_init
build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf:200052a8 b conn_tx_workq
build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf:2001b258 b conn_tx_workq_thread_stack
build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf:00063a78 r __init_bt_conn_tx_workq_init
```

`strings` retained the exact marker string in diagnostic `zephyr.elf`.

Diagnostic RAM values were:

```text
_image_ram_end  = 0x20027ec4
_image_ram_size = 0x00027ec4 (163524 bytes)
RAM limit       = 0x20028000
remaining       = 0x13c (316 bytes)
```

The build summary reported `RAM: 163524 B / 160 KB (99.81%)`. This is a
diagnostic observation, not a production-safety claim.

Diagnostic SHA-256 values were:

- Receiver CPUAPP: `533f2f82f48e2e5d073eb419ddbb682acd24de838fa10d9f0e2627067dcd0e8a`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app, unchanged: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET, unchanged: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

No HIL source firmware build was run.

Both builds completed with only the pre-existing dirty-tree notice, the
documented nRF54L15 watchdog empty-library diagnostic, and the global
`__ASSERT()` informational diagnostic. No actionable warning was observed.

## Normal restoration

Normal local nRF54L15 output was rebuilt after diagnostic proof. Its resolved
configuration showed:

```text
# CONFIG_BT_CONN_TX_NOTIFY_WQ is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
# CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_TRACING is not set
CONFIG_WARN_EXPERIMENTAL=y
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

The exact H42 marker was absent from normal `zephyr.elf`. Required normal
hash checks passed byte-identically:

- Normal receiver CPUAPP: `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

## Disk and conclusion

The disk gate reported `free_gib=120.0` before diagnostic build and
`free_gib=120.0` after normal restoration. Both passed the `80 GiB` minimum.
Only existing `build/nrf54l15` output was reused. No cleanup or garbage
collection ran.

RH3-42 software preparation is complete and the one approved physical execution
has since been run and passed; the completed run is recorded in the
[canonical H42 result](system-hil-rh3-42-bap-enable-trace-result.md). The
software phase itself performed no hardware action and this result makes no
retry, production adoption, or workqueue conclusion.
