# RH3-7p5 Stage 2 result: 7.5 ms Mode B passes on the fixed fixture

Status: completed one-run diagnostic with a decisive PASS. On the proven
128 MHz controller-clock source fixture, the 7.5 ms Mode B row
(`rh3.fresh_mode_b_48_3_1`) passed the frozen transport limits:
receiver `rx_valid=16858` of `16859` submitted (99.994%), `plc=32` of
`decoded=33748` (0.095%, ceiling 5%), source fully healthy
(`sub=16859, sc=16000, sf=0, skip=0`, lead `2851..2939 us`, `under=0`),
and the pre-declared 7.5 ms receiver expectation held (FLPR `ACTIVE`
with `submit=0 success=0 fallback=0`, the documented cpuapp ASRC
fallback). The Stage 2 discriminator answered its question: the
two-LC3-encode-per-SDU budget - the exact workload that starved the old
64 MHz fixture at 10 ms - fits at 128 MHz even in the 25% tighter
7.5 ms interval. Controller CIG at Mode B 7.5 ms:
`iso_interval_1250us=6`, `nse=3`, `cig_sync_us=3426` (~46% duty),
`c_max_pdu=180`. This is Stage 2 of the RH3-7p5 phased sequence; it
does NOT reinstate `48_3_1` rows. Stage 3 (Mode A shared-grid at 7.5 ms)
is next. Not acceptance evidence beyond this row.

## Scope and immutable evidence

One runner-owned diagnostic execution used the current-HEAD source
images (byte-identical to the RH3-accepted tuple; the Stage 1 build
hashes were re-verified unchanged before invoking) and the normal
current-HEAD receiver build:

```text
run ID: rh3-7p5-modeb-20260911
row:    rh3.fresh_mode_b_48_3_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-7p5-modeb-20260911/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-7p5-modeb-20260911.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation
with `command.status=0`. `result.json` records `outcome="passed"`,
`first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`.

## Prediction versus outcome

| Handoff prediction | Observed |
| --- | --- |
| source `sub=16859, sc=16000, sf=0, skip=0` | exact match |
| lead healthy, `under=0` | `min=2851 us`, `max=2939 us`, `under=0` |
| `rx_valid >= 90%` of 16859 | `rx_valid=16858` (99.994%) |
| `plc <= 5%` of decoded | `plc=32` of `decoded=33748` (0.095%) |
| FLPR ACTIVE, submit/success 0 (ASRC fallback) | `ACTIVE`, `submit=0 success=0 fallback=0` |

The starvation shape did not occur and the clean-collapse shape did not
occur: the encode-budget question is closed affirmatively.

## Build proof

Same proven images as Stage 1 (verified unchanged before the run):
source CPUAPP `43bdef15f6cbca0ab4df9bbeda2594b0d1ccf0e5d9a534d0507998e712b4cb3d`,
source CPUNET `2c3af526538cacf56312a1b5649c7e036c92196ef0ced0efbfb2e6f583ec635b`,
receiver CPUAPP `767715b610f3b96576fbb67202b84b87040e84b2f2d2ef13d47a6d5460fcc917`
(APP_COMMIT-derived at the docs-only HEAD), receiver FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Resolved configs as in Stage 1 (source target 3, SDC block, receiver
`CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_BT_ISO_RX_BUF_COUNT=3`).
`images.json` is authoritative for the flashed tuple.

## Runner outcome and frozen limits

Source lifecycle completed with terminal `verdict="pass"`. Final
telemetry: `sub=16859, sc=16000, cb=16859, sf=0, out=0, skip=0`, lead
`min=2851 us, max=2939 us, under=0`, TX anchor `19487201`, sync
reference `145929701` with one successful final poll.

Receiver summary:

```text
Stream[0] summary: SDUs=16858 decoded=33748 plc=32 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=16858 rx_error=0 rx_lost=16 rx_unknown=0 rx_no_ts=9
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no TS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 16858 | 33748 | 32 | 0 | 0 | 0 | 0 | 16858 | 0 | 16 | 0 | 9 |

All frozen limits met; all zero-gates met (`rx_error=0`, `rx_unknown=0`,
`empty_sdu=0`, `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`).

## FLPR, QoS, and ISO tail

Active FLPR: `ACTIVE / epoch=1258343962 gen=2`,
`submit=0 success=0 fallback=0 busy=0` - the documented 360-frame
cpuapp ASRC fallback path (validated by the runner's `48_3_1` branch);
all fault and recovery counters zero.

Receiver QoS retained the 7.5 ms Mode B shape:

```text
QoS: interval 7500 framing 0x00 phy 0x02 sdu 180 rtn 5 latency 15 pd 40000
```

ISO tail:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=17 retransmitted=0 crc_error=2 rx_unreceived=11 duplicate=83 iso_interval_1250us=6 nse=3 cig_sync_us=3426 cis_sync_us=3426 c_max_pdu=180 c_phy=2 c_bn=1 c_flush_1250us=12
```

Bounded observations: `cig_sync_us=3426` (~46% duty at 7.5 ms - more
than mono's 31% but with margin left; `c_flush_1250us=12`, FT 15 ms per
the 15 ms latency parameter), `duplicate=83` (absorbed retransmissions),
`crc_error=2`, `rx_unreceived=11`. No warnings or errors on either
console.

## Classification and next step

Stage 2 PASS, classified clean: the pinned throughput lesson's
discriminating workload (two encodes per SDU) fits the 7.5 ms interval
at 128 MHz with the controller-clock scheduler. Per the staged sequence,
Stage 3 (Mode A `48_3_1`: two CISes sharing one timestamp grid at
7.5 ms) is next under its own handoff. Reinstatement remains a plan
revision out of these diagnostics' scope.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
3874f2cf3512bbc16ba18ed9b4efe8307a78085e11813bf9bc2689e7b419413a
```

Runner-retained raw identity evidence: receiver `serial=8EE9B3FF
target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15`; source J-Link
`001050023938` (raw scan in `source-jlink.txt`). Preserve this evidence
root; do not rerun this ID.