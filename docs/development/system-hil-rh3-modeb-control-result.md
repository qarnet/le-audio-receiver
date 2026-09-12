# RH3 fresh Mode B control result

Status: completed one-run diagnostic. Binary classification reached: FAIL on
frozen transport limits.

## Scope and immutable evidence

One runner-owned normal-production-image execution used the fixed row:

```text
run ID: rh3-modeb-control-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-control-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-control-20260904.junit.xml
```

The one authorized direct invocation returned `status=1`. `result.json`
records `outcome="failed"`, `first_failed_boundary="session end"`, and
`cleanup_failures=[]`. No retry, manual target operation, production-source
change, or evidence mutation occurred. Runner owned identity resolution,
flashing, console capture, cleanup, and evidence finalization.

## Preflight and normal image identity

The normal build and runner used this HEAD:

```text
f1c13f0273f653068efe4205a97a15f72d06915b
```

`git status --short` contained only the requested untracked execution handoff:

```text
?? docs/development/system-hil-rh3-modeb-control-execution-handoff.md
```

`git diff --check` passed. No production or unrelated tracked change was
present.

- Disk gate: `177178832896` free bytes (`165.0 GiB`), above the required
  `80 GiB`.
- Output-root validation, run-ID validation, and output ownership checks
  passed. `rh3-modeb-control-20260904` has length `26`; its run root and
  external JUnit path were absent and not symlinks. `.locks` was empty both at
  preflight and immediately before runner invocation.
- Fixture validation returned
  `{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.
- Frozen source hashes matched before the build:

| Artifact | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

One normal `fw-build-54l15` ran with no fragment or special flags. Its only
warnings were the documented nRF54L15 empty `drivers__watchdog` library and
global `__ASSERT()` notice. Resolved normal configuration proved:

```text
CONFIG_AUDIO_OFFLOAD_ASRC=y
CONFIG_WARN_EXPERIMENTAL=y
# CONFIG_TRACING is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
# CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE is not set
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

`images.json` is authoritative for the runner-flashed images:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `7b8109a464d4d4dbde2761bf45683a6b1169e058e8e25b18859ea0d35b8e329a` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

## Outcome, summary, and frozen limits

The source reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, and `teardown`, then emitted terminal
`verdict="pass"` and final `idle`. Its stop status retained
`seq=12644 sub=12644 sc=12000 sf=0 cb=12644 out=0`.

The receiver configured one stereo ASE (`chan_count=2`) with `sdu=240` at
`48000 Hz` and `10000 us`. Its sole retained stream summary was:

| Field | Slot 0 |
| --- | ---: |
| `SDUs` | 113 |
| `decoded` | 27422 |
| `plc` | 27196 |
| `decode_err` | 0 |
| `i2s_underrun` | 0 |
| `stream_reset` | 0 |
| `empty_sdu` | 0 |
| `rx_valid` | 113 |
| `rx_error` | 0 |
| `rx_lost` | 13598 |
| `rx_unknown` | 0 |
| `rx_no_ts` | 9 |

The runner's authoritative failure detail is:

```text
stream summary slot 0 transport limits: rx_valid=113 below floor: need >= 11379 (90% of 12644 submitted); plc=27196 above ceiling: need <= 1371 (5% of decoded=27422)
```

This is a runner-validated **FAIL on frozen transport limits** at `session
end`. `result.json` has `summary={}` because validation raised on this first
limits violation. The deciding verdict is that retained runner failure, not a
replacement calculation from raw receiver telemetry.

## FLPR and ISO observations

The active snapshot met the 10 ms normal-image expectation of `ACTIVE` with
successful submits. Initial active poll retained `ACTIVE / epoch=1424227717
gen=2`, top-level `submit=76 success=75 fallback=0 busy=0`, and ASRC-offload
`submit=80 success=80 fallback=0`. The runner settle retry retained equal
top-level counters `submit=96 success=96 fallback=0 busy=0`, with ASRC-offload
`submit=100 success=100 fallback=0`. Active timeout, full, stale, sequence,
frame, CRC, payload, recovery, probation, and busy-fallback counters were all
zero.

Session-end limits validation failed before post-stop collection. This evidence
root contains no `receiver-post-stop-status.txt`, `audio status`, `audio perf`,
or post-stop `flpr offload`/`flpr status` snapshot. No manual query was made to
fill that gap. Post-stop FLPR state and counters are therefore unproven for
this run.

The retained ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=11376 retransmitted=0 crc_error=0 rx_unreceived=13288 duplicate=0 iso_interval_1250us=8 nse=6 cig_sync_us=8184 cis_sync_us=8184 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=8
```

## Classification and stop point

**Classification: FAIL-on-limits. Collapse is broader than dual CIS; mono is
the only healthy shape in the current control evidence.**

Deciding evidence is this run's runner-owned `result.json`: its `session end`
failure explicitly validates both the `rx_valid` delivery-floor violation and
the PLC-ceiling violation for the fresh one-CIS Mode B row. The normal-image
active FLPR snapshot also reached `ACTIVE` with successful submits. Per the
approved grounding table, next work refocuses on the difference between this
`240`-byte SDU transport and the healthy mono `120`-byte transport.

This diagnostic does not identify RF, controller, source, receiver, FLPR,
payload-size, or audio root cause. It is not RH3, transport/runtime, analog, or
audio acceptance. Do not rerun or reuse this ID.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root and external JUnit files were byte-identical; both hash to:

```text
9336b1fda78e23223a01369c6e2aa4876fc033cd55ee1acdc92e0b8d078c5b6f
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
source APs: ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```
