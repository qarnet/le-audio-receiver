# RH3 ModeA11 SDC TX-outstanding six fix-validation result

Status: completed one-run fix-validation with a negative outcome. Deepening
the source per-stream TX outstanding target from 3 to 6 under the SDC
fixture left the Mode B row's empty-event residual statistically unchanged:
`rx_valid=12638` of `12644` (99.95%, floor met) but `plc=2374` above the
ceiling `1382` (5% of `decoded=27650`), with `rx_lost=1187` and the exact
identity `plc = 2 x rx_lost` preserved. The queue-depth hypothesis is
falsified: the NULL-event mechanism does not respond to provisioning depth
(1189 empty events at target 3, 1187 at target 6). The root cause is the
SDC data-provisioning mode (sequence-number/time-of-arrival pinning with a
per-event arrival margin), and the fix is the documented preferred mode:
timestamps. This run is the plan's allowed single fix-validation rerun; it
is not acceptance, and the next hardware run is ModeA12 under its own
handoff (`docs/development/system-hil-rh3-modea12-tsmode-handoff.md`).

## Scope and immutable evidence

One runner-owned execution used the SDC source images (uncommitted ModeA10
rework) plus the committed target-six fragment
(`tests/hil/source-txout6.conf`), and the normal current-HEAD receiver
build:

```text
run ID: rh3-modeb-sdc-txout6-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-txout6-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-txout6-20260907.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation with
`command.status=0`, the fixed row, and no diagnostic runner flag.
`result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`. The runner
alone resolved identities, flashed targets, captured consoles, cleaned up,
and finalized evidence. No retry, manual target operation, receiver or
runner change, or evidence mutation occurred.

## Prediction versus outcome

The ModeA11 handoff predicted that deepening the queue to six removes the
preparation-point misses and the row passes frozen limits. The outcome
falsifies it: `rx_lost` moved from 1189 to 1187 (noise) and `plc` from 2378
to 2374 while the active snapshot showed the live queue at `out=4`
(target-six regime, ModeA8 hardware already proved `out=5` reachable). Per
the handoff's FAIL arm this records the outcome, leaves everything
uncommitted, and stops for redesign review - which the deeper SDC
documentation analysis then closed by identifying the provisioning-mode
root cause and the timestamp-mode fix (ModeA12 handoff, section "Root
cause"). No second identical rerun occurs.

## Preflight

Run HEAD was:

```text
bba38db11311757d130f805e4cd2ef8a920effdb
```

The run-time dirty tree contained exactly the ModeA10 SDC rework files
(overlay, sysbuild.cmake, Kconfig.sysbuild comment, board conf comment),
the ModeA10 and ModeA11 handoff/result documents, and the pre-existing
untracked ModeA3 handoff. Free space was `164114403328` bytes, above the
`80 GiB` gate. Run-ID validation passed (run directory and external JUnit
absent and non-symlinks, `.locks` empty). Fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.

## Build proof and flashed images

Both pristine source builds used the committed fragment:

```text
nix develop --command fw-build-hil-source -DEXTRA_CONF_FILE="$PWD/tests/hil/source-txout6.conf"
```

The resolved app-core configuration proved:

```text
CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=6
CONFIG_HIL_SOURCE_QOS_RTN=5
CONFIG_HIL_SOURCE_QOS_PHY=2
```

The resolved CPUNET configuration still proved the SDC block
(`CONFIG_BT_LL_SOFTDEVICE=y`, `CONFIG_BT_CTLR_CENTRAL_ISO=y`,
`CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT=0`, both SDC ISO TX buffer counts 6,
`CONFIG_BT_CTLR_TX_PWR_PLUS_3=y`).

Build determinism was byte-identical across the two pristine builds:

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Source CPUAPP | `056614d14ecd4e3c6f78c57915b035e80ef2ceb37aeccbb9154936b6e6157552` | `056614d14ecd4e3c6f78c57915b035e80ef2ceb37aeccbb9154936b6e6157552` | Byte-identical; equals the ModeA8 target-six app identity (same fragment, same source) |
| Source CPUNET (SDC) | `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656` | `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656` | Byte-identical; unchanged from ModeA10 (net core unaffected) |
| Merged CPUAPP | `11b8402d89c82404c057c8e3b28800f22608168c6968409a40b48dd2fd2a243f` | `11b8402d89c82404c057c8e3b28800f22608168c6968409a40b48dd2fd2a243f` | Byte-identical |
| Merged CPUNET | `40b9aadac6268905477c2c98418429bd93fc3acb8c8a5b0a5160298ea6aebd3a` | `40b9aadac6268905477c2c98418429bd93fc3acb8c8a5b0a5160298ea6aebd3a` | Byte-identical; unchanged from ModeA10 |

One pristine normal receiver build at the same HEAD proved
`CONFIG_AUDIO_OFFLOAD_ASRC=y` and `CONFIG_BT_ISO_RX_BUF_COUNT=3`:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `ea2853bbb38829acaebc26453e39f8e46e6fd5a67409ecb4bc9c5dae90d993b3` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `056614d14ecd4e3c6f78c57915b035e80ef2ceb37aeccbb9154936b6e6157552` |
| Source CPUNET | `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656` |
| Receiver CPUAPP | `ea2853bbb38829acaebc26453e39f8e46e6fd5a67409ecb4bc9c5dae90d993b3` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Build output contained only the documented dirty-tree notice and the global
`__ASSERT()` notice.

## Runner outcome and frozen limits

The source completed its full lifecycle with terminal `verdict="pass"`:
`streaming` at `monotonic_ms=20121`, active snapshot
`seq=81 sub=81 sc=0 sf=0 cb=77 out=4`, `scored_complete` at `153345`,
`teardown` at `158296`, terminal at `158791`, final idle retaining
`seq=12644` with reset counters. The pacing sanity check passed: the live
queue reached `out=4` near the target-six regime (ModeA8 proved `out=5`);
the handoff's misconfiguration condition (never near 6) did not occur. All
12644 SDUs were submitted; the source host is exonerated of starvation.

The sole required receiver stream summary was:

```text
Stream[0] summary: SDUs=12638 decoded=27650 plc=2374 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=12638 rx_error=0 rx_lost=1187 rx_unknown=0 rx_no_ts=7
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 12638 | 27650 | 2374 | 0 | 0 | 0 | 0 | 12638 | 0 | 1187 | 0 | 7 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: plc=2374 above ceiling: need <= 1382 (5% of decoded=27650)
```

`rx_valid=12638` met its floor. `rx_error=0`, `rx_unknown=0`, `empty_sdu=0`,
`decode_err=0`, `i2s_underrun=0`, `stream_reset=0` all met their gates. No
receiver runtime warning, source warning, assertion, or shell error was
retained on either console.

## FLPR, QoS, and ISO tail

The active 10 ms FLPR snapshot met the active predicate:

```text
State       : ACTIVE / epoch=79006305 gen=2
Counters    : submit=129 success=129 fallback=0 busy=0
```

ASRC offload retained `submit=134 success=133 fallback=0`. Limits validation
failed before post-stop collection, so no post-stop FLPR snapshot is
retained, matching the established pattern.

The receiver QoS log retained the frozen shape:

```text
QoS: interval 10000 framing 0x00 phy 0x02 sdu 240 rtn 5 latency 20 pd 40000
```

The ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=6052 retransmitted=0 crc_error=1 rx_unreceived=8 duplicate=93 iso_interval_1250us=8 nse=3 cig_sync_us=4146 cis_sync_us=4146 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=16
```

**Mandatory on-air sanity check: PASS.** Resolved config proves the SDC
build with the target-six fragment; receiver QoS and ISO tail retain the
frozen 2M, RTN5, 240-byte Mode B shape (`c_phy=2`, `c_max_pdu=240`, `nse=3`
present). Classification is therefore permitted.

## Analysis: queue depth is exonerated

Bounded comparison of the two SDC runs:

| Observation | ModeA10 (target 3) | ModeA11 (target 6) |
| --- | ---: | ---: |
| `rx_valid` of 12644 | 12643 | 12638 |
| `plc` | 2378 | 2374 |
| `rx_lost` | 1189 | 1187 |
| Live queue (`out`) | 2 | 4 |
| `plc = 2 x rx_lost` | exact | exact |
| Streaming window (s) | 133.25 | 133.22 |
| Controller `rx_unreceived` | 8 | 8 |

The empty-event count is invariant to queue depth. Combined with
near-total delivery, near-zero unreceived events, and the preserved
`plc = 2 x rx_lost` identity, the observations match the SDC
data-provisioning semantics documented in
`nrfxlib/softdevice_controller/doc/isochronous_channels.rst` (full root
cause and citations in the ModeA12 handoff): SDC pins each SDU to a specific
ISO event and sends NULL in any event whose pinned SDU missed the arrival
margin; queued future SDUs cannot backfill the missed slot. The fix is the
documented preferred provisioning mode (timestamps with host-side event
pinning), not queue tuning.

## Classification and stop point

Fixture defect: SDC data-provisioning mode mismatch (sequence-number/
time-of-arrival pinning under a completion-paced host with millisecond
refill jitter over the nRF53 IPC). The queue-depth fix-validation is
negative and the lever is exonerated; the root-cause analysis names the
documented preferred mode as the proper fix (ModeA12). Per the handoff's
FAIL arm, nothing is committed, and the next physical run is the ModeA12
timestamp-mode validation under its own handoff.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all retained entries. The
root and external JUnit files were byte-identical. Runner-retained raw
identity evidence: receiver `serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477
PART=0x00054b15`; source J-Link `001050023938` (raw scan in
`source-jlink.txt`).

## Restoration

No receiver change to restore. The source fixture is flashed with this
run's tuple (authoritative in this run's `images.json`); the source
currently runs the target-six app image, which is a committed-Kconfig
configuration, and the SDC netcore image. The next run (ModeA12) rebuilds
the app core at target 3 with the timestamp-mode change. Do not rerun or
reuse this ID.