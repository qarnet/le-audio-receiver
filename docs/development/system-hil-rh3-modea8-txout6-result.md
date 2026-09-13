# RH3 ModeA8 TX pacing-regime isolation result

Status: completed one-run diagnostic. Raising the source per-stream TX
outstanding target from `3` to `6` reached near-full depth (`out=5`) while the
fresh Mode B 10 ms row retained the same runner-validated delivery-collapse
signature. Per the approved handoff, scored-source TX submission pacing is
exonerated at this shape. Scored LC3-encoded content remains the trigger
candidate. This is not a root-cause, repair, or acceptance result.

## Scope and immutable evidence

One runner-owned execution used the diagnostic source image and normal receiver
image:

```text
run ID: rh3-modeb-txout6-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-txout6-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-txout6-20260904.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation with
`command.status=0`, the fixed row, and no diagnostic runner flag.
`result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`. The runner
alone resolved identities, flashed targets, captured consoles, cleaned up, and
finalized evidence. No retry, manual target operation, receiver or runner
change, or evidence mutation occurred.

## Source change and preflight

`HIL_SOURCE_TX_OUTSTANDING_TARGET` is an `int` Kconfig option with default
`3` and range `1..6`. The application header uses the Kconfig value when
available and retains a `3U` `#ifndef` fallback for native builds. The
diagnostic fragment contains exactly:

```text
CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=6
```

The default native source-app path remained unchanged and passed `68/68`:

```text
nix develop --command env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-txout6-default-twister
```

Preflight used HEAD:

```text
000a96065d95aca4a113b7039ccc55004bcea852
```

Initial free space was `173881245696` bytes (`161.9 GiB`), above the `80 GiB`
gate. `git diff --check` passed. The dirty tree contained only the two scoped
source edits, this fragment, this requested handoff, and the pre-existing
untracked ModeA3 handoff. Run-ID validation passed with length `25`; the run
directory and external JUnit were absent and non-symlinks, and `.locks` was
empty. Fixture validation returned:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## Build proof and flashed images

Both pristine diagnostic source builds used only the target-six fragment:

```text
nix develop --command fw-build-hil-source -DEXTRA_CONF_FILE="$PWD/tests/hil/source-txout6.conf"
```

The resolved source application configuration proved:

```text
CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=6
CONFIG_HIL_SOURCE_QOS_PHY=2
CONFIG_HIL_SOURCE_QOS_RTN=5
```

Diagnostic build determinism was byte-identical:

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Source CPUAPP | `056614d14ecd4e3c6f78c57915b035e80ef2ceb37aeccbb9154936b6e6157552` | `056614d14ecd4e3c6f78c57915b035e80ef2ceb37aeccbb9154936b6e6157552` | Byte-identical |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | Unchanged |
| Merged CPUAPP | `11b8402d89c82404c057c8e3b28800f22608168c6968409a40b48dd2fd2a243f` | `11b8402d89c82404c057c8e3b28800f22608168c6968409a40b48dd2fd2a243f` | Byte-identical |
| Merged CPUNET | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` | Unchanged |

One pristine normal receiver build at the same HEAD used no fragment. Its
resolved configuration included `CONFIG_AUDIO_OFFLOAD_ASRC=y` and
`CONFIG_BT_ISO_RX_BUF_COUNT=3`. Its hashes were:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `1a39c8eac3c1c058ef835e40360c3a7c9bb1f6995949f1bf2cb234a5e1cf16e3` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `056614d14ecd4e3c6f78c57915b035e80ef2ceb37aeccbb9154936b6e6157552` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `1a39c8eac3c1c058ef835e40360c3a7c9bb1f6995949f1bf2cb234a5e1cf16e3` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Build output contained no actionable compiler or Kconfig diagnostic. It
retained the documented dirty-tree notice, global `__ASSERT()` notice, required
SW Split controller informational notices, and the receiver's documented empty
`drivers__watchdog` library warning.

## Runner outcome and frozen limits

The source reached `streaming`, `scored_complete`, `teardown`, terminal
`verdict="pass"`, and `idle`. The final idle status retained `seq=12644`.
The sole required receiver stream summary was:

```text
Stream[0] summary: SDUs=137 decoded=27408 plc=27134 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=137 rx_error=0 rx_lost=13567 rx_unknown=0 rx_no_ts=9
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 137 | 27408 | 27134 | 0 | 0 | 0 | 0 | 137 | 0 | 13567 | 0 | 9 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: rx_valid=137 below floor: need >= 11379 (90% of 12644 submitted); plc=27134 above ceiling: need <= 1370 (5% of decoded=27408)
```

`rx_error=0`, `rx_unknown=0`, and `empty_sdu=0` met their zero requirements.
`rx_lost` and `rx_no_ts` remain record-only fields under the frozen limits.

## Pacing sanity, FLPR, QoS, and ISO tail

The source active snapshot was captured after `streaming` while the source was
active and showed the deepened regime:

```text
seq=29 sub=29 sc=0 sf=0 cb=24 out=5
```

The runner captures this active snapshot before scored completion, so the
recorded `sc=0` is expected at this boundary. Resolved Kconfig was target `6`,
and live `out=5` is near that target. The handoff's misconfiguration condition
(`out` never reaches near `6`) did not occur. The source later emitted
`scored_complete` and terminal `pass`.

The active 10 ms FLPR snapshot met the active predicate:

```text
State       : ACTIVE / epoch=1840465137 gen=2
Counters    : submit=83 success=83 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
```

The accompanying ASRC-offload counters were `submit=89 success=89 fallback=0`,
with all listed faults zero. Limits validation failed before post-stop
collection, so no post-stop FLPR snapshot is retained or inferred.

The receiver QoS log restored the required 2M/RTN5 baseline:

```text
QoS: interval 10000 framing 0x00 phy 0x02 sdu 240 rtn 5 latency 20 pd 40000
```

The ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=11615 retransmitted=0 crc_error=2 rx_unreceived=13571 duplicate=0 iso_interval_1250us=8 nse=6 cig_sync_us=8184 cis_sync_us=8184 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=8
```

**Mandatory sanity check: PASS.** Source resolved configuration and live
`out=5` prove a near-target-six source queue. Receiver QoS and ISO tail retain
the frozen 2M, RTN5, 240-byte Mode B shape. Classification is therefore
permitted.

## Classification and stop point

This is the handoff's `FAIL with same signature` branch. The source completed
the full scored run, but the receiver accepted only `137` valid SDUs, fewer than
the `144`-SDU preamble, then retained `13567` LOST callbacks and `27134` PLC
frames. The runner reached `session end` and failed only frozen transport
limits. The ISO tail retained near-total `rx_unreceived=13571` with
`crc_error=2`, `nse=6`, `c_max_pdu=240`, and `c_phy=2`.

| Observation | RTN5/2M Mode B control | Target-six pacing run |
| --- | ---: | ---: |
| Expected submitted SDUs | 12644 | 12644 |
| `rx_valid` | 113 | 137 |
| `decoded` | 27422 | 27408 |
| `plc` | 27196 | 27134 |
| `rx_lost` | 13598 | 13567 |
| `rx_no_ts` | 9 | 9 |

Deepening the live per-stream source queue from the normal target `3` toward
the controller-pool target `6` did not recover delivery under frozen limits.

**Verdict: scored-source TX submission pacing is exonerated at this 240-byte,
2M, RTN5, fresh Mode B shape.** Scored LC3-encoded payload content remains the
trigger candidate. A new reviewed content-analysis handoff is required before
further physical work. This run does not identify an air, controller, source,
receiver, RF, FLPR, or LC3 root cause, and does not authorize a production
queue-depth change. Do not rerun or reuse this ID.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
8558fd9c5d7f72019d1bd2c55f64e6a88aa2154eab861ff9472ad02e243568d9
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
source APs: ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

`nrf-probes.txt` separately retained the attached `E6635C08CB1F502B`
CMSIS-DAP nRF5340 identity. The runner used source J-Link `001050023938`; its
raw OpenOCD scan is retained in `source-jlink.txt`. Receiver flash logs retained
verified CPUAPP and FLPR writes. Source flash logs retained two `** Verified OK
**` records, one for each core, plus only the documented page-tail erase
extensions.

## Normal-source restoration

One local, non-flashing `nix develop --command fw-build-hil-source` completed
after read-only evidence review. Its resolved source application configuration
proved:

```text
CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3
CONFIG_HIL_SOURCE_QOS_PHY=2
CONFIG_HIL_SOURCE_QOS_RTN=5
```

| Image | Local normal-build SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Merged CPUAPP | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` |
| Merged CPUNET | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` |

The normal source CPUAPP and merged hashes match the pre-existing normal target
three identities. Restoration did not flash either target; `images.json`
remains the authoritative last-flashed diagnostic tuple. Post-restoration free
space was `173842739200` bytes, above the `80 GiB` gate.
