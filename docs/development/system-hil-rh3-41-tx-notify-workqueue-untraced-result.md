# RH3-41 untraced TX-notify workqueue result

## Status and scope

One runner-owned H41 physical execution completed and failed. This was one
bounded no-trace diagnostic, not RH3 acceptance and not a production change.
The failure is evidence only. It does not prove a trace effect, workqueue
causation, a root cause, a repair, production safety, or production adoption.

## Immutable evidence

The immutable H41 evidence root is:

```text
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/
```

The external JUnit file is:

```text
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced.junit.xml
```

The run identity was:

```text
run ID: rh3-20260830-41-tx-notify-wq-untraced
row:    rh3.fresh_mode_b_48_3_1
```

`environment.json` records command status `0`. Retained `result.json` records
`outcome=failed`, first failed boundary `run row`, and the exact failure detail:

```text
active status snapshot invalid: state='teardown'; aborted=True; first_errno=-116
```

Cleanup contains one failure:

```text
source idle cleanup failed: no status response for command cmd-0009
```

The external JUnit records one test and one failure, with zero skipped tests and
errors. Read-only `SHA256SUMS` verification passed all `22/22` retained
artifacts. The command status and retained failed result are separate recorded
facts; this document makes no new exit-status claim from them.

Relevant retained files include `result.json`, `environment.json`,
`images.json`, `identity.json`, `source-records.jsonl`, the source and receiver
console logs, and `SHA256SUMS`. Raw binary console logs remain in the immutable
evidence root and are not copied into this repository. No receiver status
summary or trace payload exists for this untraced pre-stream failure.

## Exact images and identities

H41 used the untraced diagnostic fragment:

```text
tests/hil/receiver-conn-tx-notify-wq-untraced.conf
SHA-256: 7c096e12c276d2e9e3e6c65f27e3564c5df10b8e6d0e708ceda81cba9ace85e7
```

The resolved receiver configuration retained the private workqueue and no
trace instrumentation:

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

Exact image SHA-256 values were:

- Receiver CPUAPP: `f2db9de95a17db47792f9601de033e9db4e851ae534df6fa00fdbd8fa7fcbd18`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

Raw target identity facts from `identity.json` were:

```text
receiver probe: backend=nrf-probes serial=8EE9B3FF family=nrf54l target=nRF54L15 dp idr=0x6ba02477 part=0x00054b15 variant=AAC0
receiver serial: path=/dev/ttyACM2 baud=115200 dtr=true rts=false
source probe:   backend=jlink serial=001050023938 family=nrf53 target=nRF5340 dp idr=0x6ba02477 part=0x00005340 variant=0x514b4141
source serial:   path=/dev/ttyACM1 baud=115200 dtr=true rts=false
```

## Observed failure

The row was fresh Mode B `48_3_1`, with one stream, a scored target of `16000`,
and an expected submitted count of `16859`.

The source start command was accepted. Source records reached these states in
order:

```text
idle -> configured -> connecting -> secured -> discovered -> qos
-> teardown (cause=timeout)
```

The source did not reach `streaming`. Its retained active status was
`state=teardown`, `aborted=true`, `first_errno=-116`, and `connected=true`, with
security level `2`, two sink ASEs, and `group=true`. The sole stream retained
zero progress: `seq=0 sub=0 sc=0 sf=0 cb=0 out=0`. Missing source streaming and
zero source stream progress are observations, not proof that the source caused
the timeout or that the receiver caused it.

During cleanup, the source asserted while issuing HCI Disconnect `0x0406`:

```text
ASSERTION FAIL [err == 0] @ WEST_TOPDIR/zephyr/subsys/bluetooth/host/hci_core.c:506
Controller unresponsive, command opcode 0x0406 timeout with err -11
```

The receiver completed connection, pairing and security, codec configuration,
QoS, and stream enable processing. It retained one stereo ASE and these exact
codec and QoS observations:

```text
Frequency: 48000 Hz
Frame Duration: 7500 us
Octets per frame: 90
Frames per SDU: 1
ASE[0] configured: num_sink_ase=1 chan_count=2 freq=48000 dur=7500 octets=90
QoS: interval 7500 framing 0x00 phy 0x02 sdu 180 rtn 5 latency 15 pd 40000
Enable: stream[0] meta_len 4
LC3 decoder[0]: 48000 Hz 7500 us ch=2
```

The receiver retained no `Stream[0] started` line. No receiver status snapshot
exists because the row failed before streaming and terminal collection. Missing
receiver `Stream[0] started` is an observation, not proof that the receiver
caused the timeout or that the source caused it.

## Bounded H40 comparison

H40, `rh3-20260826-40-sdc-iso-tx-notify-wq`, was one traced fresh Mode B
`48_3_1` execution that passed its row. H41 was one untraced execution of the
same row that failed after QoS and before streaming. This bounded difference
does not prove a trace effect, workqueue causation, a root cause, or a repair.

## Restoration

Normal local nRF54L15 build output was restored after H41 evidence review. The
restored and unchanged image hashes were:

- Normal receiver CPUAPP: `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

Restoration changed local build output only. It did not flash or otherwise
touch either board, and it does not prove current live hardware image state.
Disk remained at `101.3 GiB` free, with no cleanup.

## Stop point

H41 is failed diagnostic evidence, not RH3 acceptance. Do not retry H41, reuse
its run ID or evidence root, or adopt its untraced workqueue configuration in
production. No causal conclusion or repair follows from this result.

The smallest next investigation requires a new reviewed plan. That plan must
distinguish receiver enable callback and start behavior from source
enabled-completion observation before any further hardware work or production
action.
