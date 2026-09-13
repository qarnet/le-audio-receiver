# RH3 ModeA5 ISO RX buffer-depth isolation result

Status: completed one-run diagnostic. The receiver with
`CONFIG_BT_ISO_RX_BUF_COUNT=6` failed with the same Mode B 10 ms delivery-collapse
signature. Per the approved handoff, this exonerates host ISO RX pool depth at
six buffers and moves the next investigation locus to controller/window timing
between the SW Split central and SDC peripheral. It establishes no root cause,
repair, or acceptance result.

## Scope and immutable evidence

One runner-owned execution used the receiver-only diagnostic image:

```text
run ID: rh3-modeb-rx6-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-rx6-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-rx6-20260904.junit.xml
```

The one authorized runner invocation returned `status=1`. `result.json`
records `outcome="failed"`, `first_failed_boundary="session end"`, and
`cleanup_failures=[]`. `environment.json` retains only the normal `run`
arguments, including `--row rh3.fresh_mode_b_48_4_1`; no trace or special
runner flag was used. The runner owned identity resolution, flashing, console
capture, cleanup, and evidence finalization. No retry, manual target operation,
runner/parser change, or evidence mutation occurred.

## Preflight and diagnostic build proof

Run HEAD was:

```text
0c3c9f77a3473a7987a4ace7b8e14d3760d8e264
```

Initial free space was `163904032768` bytes (`152.6 GiB`), above the `80 GiB`
gate. The initial dirty tree contained only the pre-existing untracked
`system-hil-rh3-modea3-source-isoq-handoff.md` and the requested RX6 handoff.
The RX6 fragment was created after preflight. No production source changed.

Run-ID validation passed for `rh3-modeb-rx6-20260904` (length `22`). Its run
root and external JUnit path were absent and non-symlinks before invocation, and
`.locks` was empty. Fixture validation returned:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

The frozen source identities matched before the receiver build:

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

The diagnostic fragment has exactly these two lines and SHA-256
`357fe8b3c3c3a1b8c2c7b6e4c2d3e63e27bf58e2013af0ababcc38ef46fedd84`:

```text
CONFIG_BT_ISO_RX_BUF_COUNT=6
CONFIG_WARN_EXPERIMENTAL=y
```

Both pristine diagnostic builds used:

```text
nix develop --command fw-build-54l15 -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-rx6.conf"
```

The resolved diagnostic configuration proved:

```text
CONFIG_BT_ISO_RX_BUF_COUNT=6
CONFIG_AUDIO_OFFLOAD_ASRC=y
# CONFIG_TRACING is not set
# CONFIG_HIL_RX_TIMING_TRACE is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
```

Both builds linked. The diagnostic map retained `_image_ram_end=0x20027e8c`;
against the CPUAPP RAM limit `0x20028000`, remaining RAM was `372` bytes.
Build output contained only the documented empty `drivers__watchdog` library
warning and global `__ASSERT()` notice. No actionable compiler or Kconfig
warning occurred.

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Receiver CPUAPP | `cc1cc0a8dbd9a0f6a7e6e3bc78c535884d879a0aac2b0f731a990aeebe9ed0aa` | `cc1cc0a8dbd9a0f6a7e6e3bc78c535884d879a0aac2b0f731a990aeebe9ed0aa` | Byte-identical |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | Byte-identical |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `cc1cc0a8dbd9a0f6a7e6e3bc78c535884d879a0aac2b0f731a990aeebe9ed0aa` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

## Runner outcome and frozen transport verdict

The source reached `streaming`, `scored_complete`, `teardown`, terminal
`verdict="pass"`, and `idle`. Its terminal stop record retained one active
stream with `seq=12644 sub=12644 sc=12000 sf=0 cb=12644 out=0` and
`first_errno=0`.

The runner parsed this exact receiver summary for the sole required slot:

```text
Stream[0] summary: SDUs=109 decoded=27422 plc=27204 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=109 rx_error=0 rx_lost=13602 rx_unknown=0 rx_no_ts=9
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 109 | 27422 | 27204 | 0 | 0 | 0 | 0 | 109 | 0 | 13602 | 0 | 9 |

The authoritative frozen-limit failure is:

```text
stream summary slot 0 transport limits: rx_valid=109 below floor: need >= 11379 (90% of 12644 submitted); plc=27204 above ceiling: need <= 1371 (5% of decoded=27422)
```

`rx_error=0`, `rx_unknown=0`, and `empty_sdu=0` met their zero requirements.
`rx_lost` and `rx_no_ts` are record-only fields under the frozen limits.

## Active FLPR and ISO tail

The active 10 ms FLPR observation met the required active predicate:

```text
State       : ACTIVE / epoch=1497861741 gen=2
Counters    : submit=82 success=82 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
Probation   : active=0 success=0 cleared=0
RTT         : min=1830 cyc (1830 us) max=1942 cyc (1942 us) avg=1849 cyc (1849 us) n=82
```

The accompanying ASRC-offload counters were `submit=86 success=86 fallback=0`,
with all reported fault counters zero. Thus this 10 ms run had `ACTIVE` plus
`submit >= 1` and `success >= 1`. Limits validation failed before the runner's
post-stop collection, so no post-stop FLPR snapshot is retained or inferred.

The retained ISO tail is:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=11358 retransmitted=0 crc_error=1 rx_unreceived=13270 duplicate=0 iso_interval_1250us=8 nse=6 cig_sync_us=8184 cis_sync_us=8184 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=8
```

## Classification

This is the handoff's `FAIL with the same death signature` branch, not another
failure boundary. The runner received and validated the required summary, then
failed at `session end` only because frozen transport limits failed; cleanup was
empty and integrity passed.

The deciding runner-validated comparison is:

| Observation | RX6 run | Prior Mode B RX-timing run |
| --- | ---: | ---: |
| Expected submitted SDUs | 12644 | 12644 |
| `rx_valid` | 109 | 113 |
| `decoded` | 27422 | 27422 |
| `plc` | 27204 | 27196 |
| `rx_lost` | 13602 | 13598 |
| `rx_no_ts` | 9 | 9 |

The prior values are the runner-validated values from
`rh3-modeb-rxtiming-20260904`, recorded in
`system-hil-rh3-modea4-rxtiming-result.md`. The new ISO tail's
`crc_error=1` is recorded, but it did not change the first failed boundary or
the near-total valid-delivery collapse. Doubling the host ISO RX pool from
three to six therefore did not restore delivery.

**Verdict: ISO RX pool depth is exonerated at six buffers.** The approved
handoff moves the investigation locus to controller/window timing between the
SW Split central and SDC peripheral. This single result does not identify a
specific controller or window mechanism, establish RF causality, or authorize a
production configuration change. Do not rerun this ID; later physical work
requires a new reviewed handoff.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root JUnit and external JUnit were byte-identical, both hashing to:

```text
4276f20e99f68e08fea1489e093fbc645a14e5c7151308c266fcf097014dec08
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
source APs: ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

`receiver-flash.log` retains verified CPUAPP and FLPR writes. The source flash
log retains two `** Verified OK **` records for CPUAPP and CPUNET.

## Normal-build restoration

One local, non-flashing `nix develop --command fw-build-54l15` normal build
completed after read-only evidence review. It contained only the same documented
watchdog and global-assert diagnostics. Resolved normal configuration proved:

```text
CONFIG_BT_ISO_RX_BUF_COUNT=3
CONFIG_AUDIO_OFFLOAD_ASRC=y
# CONFIG_TRACING is not set
# CONFIG_HIL_RX_TIMING_TRACE is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
```

| Image | Local normal-build SHA-256 |
| --- | --- |
| Receiver CPUAPP | `1b3b5ed1197d514b66a7c8ffad0911eab610250752baefab826f696459170f6e` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

Post-restoration free space was `163873361920` bytes (`152.6 GiB`), above the
`80 GiB` gate. The normal local build did not flash either target. The runner's
`images.json` remains authoritative for the last flashed diagnostic image.
