# RH3-42 BAP enable-callback trace result

## Status and scope

One runner-owned H42 physical execution completed and passed. This was one
bounded no-trace diagnostic with a single receiver callback-return marker, not
RH3 acceptance and not a production change. The passing row does not establish
audio health, a root cause for H41, workqueue causation, a trace effect,
production safety, or production adoption.

## Immutable evidence

The immutable H42 evidence root is:

```text
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/
```

The external JUnit file is:

```text
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace.junit.xml
```

The run identity was:

```text
run ID: rh3-20260903-42-bap-enable-trace
row:    rh3.fresh_mode_b_48_3_1
```

`environment.json` records runner command status `0` on NCS `3.3.0` with dirty
HEAD `c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1`. `result.json` records
`outcome=passed`, `first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`. The external JUnit records one test with zero failures,
skipped tests, or errors. Read-only `SHA256SUMS` verification passed all `26/26`
retained artifacts.

Relevant retained files include `result.json`, `environment.json`,
`images.json`, `identity.json`, `source-records.jsonl`, the source and receiver
console logs, receiver status captures, flash logs, and `SHA256SUMS`. Raw binary
console logs remain in the immutable evidence root and are not copied into this
repository. No audio capture exists because `capture_capability=none`.

## Exact images and configuration identities

H42 used the diagnostic fragment:

```text
tests/hil/receiver-conn-tx-notify-wq-enable-trace.conf
SHA-256: 065162a7d152530ea511294b71a58f9d2488febd483718258efef0aa7b98c9f7
```

The resolved receiver configuration retained the H41 private TX-notify
workqueue settings plus the one gated marker, with no broad trace
instrumentation:

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

Exact image SHA-256 values were:

- Receiver CPUAPP: `533f2f82f48e2e5d073eb419ddbb682acd24de838fa10d9f0e2627067dcd0e8a`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

Raw target identity facts from `identity.json` and `source-jlink.txt` were:

```text
receiver probe: backend=nrf-probes serial=8EE9B3FF family=nrf54l target=nRF54L15 dp idr=0x6ba02477 part=0x00054b15 variant=AAC0
receiver serial: path=/dev/ttyACM2 baud=115200 dtr=true rts=false
source probe:   backend=jlink serial=001050023938 family=nrf53 target=nRF5340 dp idr=0x6ba02477 ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000 part=0x00005340 variant=0x514b4141
source serial:   path=/dev/ttyACM1 baud=115200 dtr=true rts=false
```

`source-flash.log` retained the two existing documented page-tail OpenOCD
diagnostics, `Warn : Adding extra erase range` at
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`. Both source
programming and verification steps reported success. These are recorded tool
observations, not a new repair or diagnostic result.

## Marker observation

The diagnostic marker fired exactly once. Exact extraction from the receiver
console:

```json
{"h42_enable_markers": [["0", "0"]]}
```

The retained receiver log line was:

```text
[00:23:04.630,211] <inf> bt_bap: HIL BAP enable: stream[0] start=0
```

Interpretation limits, all explicit:

- The marker proves only that receiver `stream_enabled_cb()` ran and returned
  from `bt_bap_stream_start()` with result `0` in this execution.
- It does not prove that H41's callback ran, that H41 and H42 share a failure
  cause, that the source observed the remote enabled state, or anything about
  ASCS peer delivery, notification ordering, or radio timing.
- It does not prove workqueue causation, a trace effect, a root cause, a
  repair, production safety, or production adoption of the marker or the
  private workqueue.

## Observed execution

The row was fresh Mode B `48_3_1`, one stream, expected submitted count
`16859`, and scored target `16000`.

The source start command was accepted. Source records reached these states in
order:

```text
idle -> configured -> connecting -> secured -> discovered -> qos
-> streaming -> scored_complete -> teardown -> terminal pass
```

Final source stream counters were:

```text
seq=16859 sub=16859 sc=16000 sf=0 cb=16859 out=0 first_errno=0
```

The active source snapshot retained `state=streaming`, connected, security
level `2`, and two sink ASEs. The source active FLPR offload snapshot retained
`submit=0 success=0` with `state=ACTIVE`; the post-stop receiver snapshot
retained `state=STOPPED`.

The receiver completed connection, pairing and security, codec configuration,
QoS, enable processing, and streaming. The receiver stream summary retained:

```text
SDUs=24 decoded=38972 plc=38924 decode_err=0 i2s_underrun=0 stream_reset=0
empty_sdu=0 rx_valid=24 rx_error=0 rx_lost=19462 rx_unknown=0 rx_no_ts=12
```

The ISO link-quality tail retained `rx_unreceived=18891`, `crc_error=0`,
`retransmitted=0`, and `duplicate=0`. These high-loss values are recorded
plainly as observations. No cause is inferred from them, and with
`capture_capability=none` this row proves nothing about audible output.

Post-stop receiver diagnostics retained zero audio faults, zero push failures,
FLPR handshake Ready, ACKed, and Healthy with zero handshake errors. The
`48_3_1` 7.5 ms row has the 360-frame input shape, so documented nRF54L15
behavior uses CPUAPP ASRC fallback; zero FLPR submits are expected here and are
not an offload fault.

## Bounded H41 comparison

H41, `rh3-20260830-41-tx-notify-wq-untraced`, was one untraced private-workqueue
execution of the same row that failed after QoS and before streaming: source
aborted with `first_errno=-116`, receiver reached enable processing but retained
no `Stream[0] started` line. H42 was one execution of the same row with the
identical workqueue configuration plus one gated callback-return marker, and it
passed end to end.

The two runs are not identical images: H42 adds the marker. The single
H41-failed/H42-passed contrast does not prove a trace effect, workqueue
causation, a root cause, or a repair. H41's failure remains undiagnosed.

## Restoration

Normal local nRF54L15 build output was restored after H42 evidence review. The
restored and unchanged image hashes were:

- Normal receiver CPUAPP: `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

The restored normal configuration had the workqueue and marker disabled, and
the marker string was absent from the normal ELF. Restoration changed local
build output only. It did not flash or otherwise touch either board, and it does
not prove current live hardware image state. Disk remained at `120.0 GiB` free,
with no cleanup.

## Stop point

H42 is a completed bounded diagnostic, not RH3 acceptance. Do not retry H42,
reuse its run ID or evidence root, or adopt its untraced workqueue
configuration or callback marker in production. No causal conclusion, repair,
or production change follows from this result. H41 remains undiagnosed. Any
further hardware, causal, or production work requires a new reviewed plan.