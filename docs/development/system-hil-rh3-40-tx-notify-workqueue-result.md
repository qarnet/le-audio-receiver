# RH3-40 TX-notify workqueue result

## Status and scope

One diagnostic H40 physical execution completed and passed. This is one bounded
RH3 diagnostic result, not RH3 acceptance and not a production change. The
passing runner row does not establish audio health, a root cause, production
safety, or production adoption.

## Immutable evidence

The immutable H40 evidence root is:

```text
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/
```

The external JUnit file is:

```text
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
```

`environment.json` records runner command status `0`. `result.json` records
`outcome=passed`, `first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`. The external JUnit contains one test with zero failures,
skipped tests, or errors. Read-only `SHA256SUMS` verification passed `27/27`
retained evidence files.

Relevant immutable records are `result.json`, `environment.json`, `images.json`,
`sdc-hci-remove-iso-path-trace.json`, `receiver-post-stop-status.txt`, and
`SHA256SUMS` within the evidence root. Raw console logs are not copied into
this repository.

## Exact image and configuration identities

H40 used only the diagnostic trace fragment
`tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf`. Its private
TX-notify workqueue settings were:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_WARN_EXPERIMENTAL=n
```

These settings were trace-only and are not a production configuration. The H40
CPUAPP RAM margin was 104 bytes.

Exact image SHA-256 values were:

- Receiver CPUAPP: `c487f021a0ff29076e0a1b312a6826b4f2007623098f22c34fa19358f1bedb52`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

## Observed execution

The executed row was `rh3.fresh_mode_b_48_3_1`, a fresh Mode B `48_3_1`
execution with one stream. The source final status was a pass with these exact
counters:

```text
verdict=pass sub=16859 sc=16000 sf=0 cb=16859 out=0
```

The active source lifecycle reached `state=streaming` with the source active
and connected. Its active stream snapshot was `seq=20 sub=20 sc=0 sf=0 cb=20
out=0`; the active FLPR offload snapshot retained `submit=0 success=0`.
The source then stopped through teardown and reached the passing terminal
status.

The receiver summary retained these high-loss values:

```text
rx_valid=23 rx_lost=19463 decoded=38972 plc=38926
```

It also retained `rx_no_ts=12`, `rx_error=0`, `decode_err=0`,
`i2s_underrun=0`, `stream_reset=0`, and `empty_sdu=0`. The ISO link-quality
tail reported `rx_unreceived=18884` and `crc_error=1`. These loss values are
recorded plainly. No cause is inferred from them.

Post-stop diagnostic snapshots had no listed audio, I2S, FLPR, or handshake faults:

- Audio counters were decoded `0`, PLC `0`, decode errors `0`, I2S underruns
  `0`, stream resets `0`, empty SDUs `0`, and push failures `0`.
- The FLPR state was `STOPPED`, with `submit=0 success=0 fallback=0 busy=0`
  and zero recorded faults, recovery attempts, or probation counters.
- FLPR handshake was Ready, ACKed, and Healthy, with zero length, version,
  unknown, send, RX-lost, duplicate, out-of-order, and missed errors.

The `48_3_1` 7.5 ms row has the 360-frame input shape. Existing nRF54L15
behavior therefore uses CPUAPP ASRC fallback, so zero FLPR submits are expected
here and are not an H40 offload fault.

The trace retained one zero-outstanding ISO RX lifetime `disable` snapshot:
`capacity=3`, `outstanding=0`, `high_water=3`, `allocations=19486`,
`final_unrefs=19486`, `callbacks_active=0`, and `callbacks_total=19486`. It
retained successful command and SDC completion records, and no `unavailable`
snapshot. Trace `parser_errors=[]` and `validation_errors=[]`.

## Bounded H39 comparison

H39 observed an `unavailable` snapshot with `outstanding=3`,
`tx_notify_flush_semaphore_pend_thread_marked_pending=1`, and
`tx_notify_flush_semaphore_give_entered=0`. H40 instead completed the row with
no `unavailable` snapshot and retained a zero-outstanding `disable` snapshot.

This is an observation-only comparison. It does not prove workqueue ownership,
cause, a general repair, workload safety, or production safety.

## Restoration and stop point

Local normal nRF54L15 build output was restored after H40. The restored normal
CPUAPP hash was
`e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`; FLPR
and source output retained the H40 values listed above. The local restoration
build did not flash either board, so it does not prove that hardware itself was
reflashed to the normal image.

H40 must not be retried. Any later hardware or production phase requires a new
reviewed plan.
