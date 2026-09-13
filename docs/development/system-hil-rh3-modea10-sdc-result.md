# RH3 ModeA10 SDC fixture result

Status: completed one-run validation with a near-complete recovery that
selects the handoff's other-boundary arm. Switching the fixture net core
from the SW-split controller to the SoftDevice Controller (SDC) recovered
fresh Mode B 10 ms delivery to `12643` of `12644` submitted SDUs (99.99%,
floor met; the SN_STRICT=y collapse delivered 113, the SN_STRICT=n run
10141). The row still fails the frozen PLC ceiling: `plc=2378` above
`1383` (5% of `decoded=27664`). The entire residual is empty-event
concealment: `plc` equals exactly `2 x rx_lost (1189)`. Classification is
clean (fixture defect: completion-paced host refill jitter versus SDC event
preparation), the fix follows from the mechanism (deepen the source TX
outstanding target from 3 to 6, the existing committed Kconfig lever), and
one fix-validation rerun is authorized by the handoff's other-boundary arm.
The SDC wiring stays uncommitted until a row passes.

## Scope and immutable evidence

One runner-owned execution used the reworked SDC source images (uncommitted
fixture rework per the ModeA10 handoff) and the normal current-HEAD receiver
build:

```text
run ID: rh3-modeb-sdc-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-20260907.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation with
`command.status=0`, the fixed row, and no diagnostic runner flag.
`result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`. The runner
alone resolved identities, flashed targets, captured consoles, cleaned up,
and finalized evidence. No retry, manual target operation, receiver or runner
change, or evidence mutation occurred.

## Prediction versus outcome

The handoff predicted PASS (`rx_valid >= 11379`, `plc <= 5%` of decoded) if
SDC removes the SW-split ISO-AL loss class, or a new-signature FAIL pointing
at the SDC fixture config. The outcome is between the two: `rx_valid` passes
decisively (12643, no rx_valid violation appears in the failure detail), and
the remaining PLC-only failure has a single, exactly-attributable mechanism.
Per the handoff this is the other-boundary arm: record, classify, and either
fix-firmware-side and rerun once as fix-validation, or stop for user
decision. The classification below is clean and the fix is the designated
follow-up lever (the handoff pre-named queue-depth matching as the follow-up
with its own handoff: `docs/development/system-hil-rh3-modea11-sdc-txout6-handoff.md`).

## Preflight

Run HEAD was:

```text
3bb585f3d5179f5897ebeeef99ab052213716e93
```

The run-time dirty tree contained exactly the ModeA10 fixture rework (new
`hil/source/app/overlay-nrf5340_cpunet_sdc.conf`, edited
`hil/source/app/sysbuild.cmake`, comment updates in
`hil/source/app/boards/nrf5340dk_nrf5340_cpuapp.conf` and
`hil/source/app/Kconfig.sysbuild`), this handoff
(`system-hil-rh3-modea10-sdc-handoff.md`), the appended ModeA4 correction
note, and the pre-existing untracked ModeA3 handoff. `git status --porcelain`
matched that expectation. Initial free space was `164120100864` bytes, above
the `80 GiB` gate. Run-ID validation passed (run directory and external
JUnit absent and non-symlinks, `.locks` empty). Fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.

## Build proof and flashed images

Two pristine SDC source builds (plus one earlier iteration that fixed two
Kconfig errors during bring-up, see below) used:

```text
nix develop --command fw-build-hil-source
```

Bring-up corrections during this phase (all before any hardware run):

1. Removed `CONFIG_BT_CTLR_CONN_ISO=y` from the overlay: it is a derived
   symbol (default `CENTRAL_ISO || PERIPHERAL_ISO`, no prompt) and Kconfig
   rejects assigning it.
2. Removed `CONFIG_BT_CTLR_DATA_LENGTH_MAX=251` and
   `CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX=191`: both carry an unsatisfied
   `BT_LL_SW_SPLIT` dependency under SDC and produced "assigned value but
   got" warnings; per the warning policy they are errors. SDC manages its
   own data length.
3. Added `CONFIG_BT_CTLR_TX_PWR_PLUS_3=y`: under SDC the Zephyr TX power
   choice still applies (nrf `radio_nrf5_txp.h` maps it to
   `sdc_default_tx_power_set()`), but its default is 0 dBm; the fixture RF
   margin requires the same +3 dBm the SW-split build used.

The final resolved source CPUNET configuration proved:

```text
CONFIG_BT_LL_SOFTDEVICE=y
CONFIG_BT_LL_SOFTDEVICE_MULTIROLE=y
CONFIG_BT_CTLR_CENTRAL_ISO=y
CONFIG_BT_CTLR_CONN_ISO=y
CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT=0
CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=6
CONFIG_BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT=6
CONFIG_BT_CTLR_TX_PWR_PLUS_3=y
CONFIG_BT_ISO_CENTRAL=y
CONFIG_BT_CTLR_CONN_ISO_GROUPS=1
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2
CONFIG_BT_MAX_CONN=1
CONFIG_BT_ISO_TX_BUF_COUNT=6
```

`CONFIG_BT_LL_SW_SPLIT` and `CONFIG_BT_CTLR_PERIPHERAL_ISO` are absent with
unsatisfied dependencies (equal to the requested `n`). The app-core
configuration was unchanged: `CONFIG_HIL_SOURCE_QOS_RTN=5`,
`CONFIG_HIL_SOURCE_QOS_PHY=2`, `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`.
The SDC netcore fits with margin: FLASH 153176/262144 B (58.4%), RAM
39276/65536 B (59.9%).

Build determinism was byte-identical across the two final pristine builds:

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` | Unchanged from normal build (app core untouched) |
| Source CPUNET (SDC) | `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656` | `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656` | Byte-identical; new SDC identity |
| Merged CPUAPP | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` | Unchanged |
| Merged CPUNET | `40b9aadac6268905477c2c98418429bd93fc3acb8c8a5b0a5160298ea6aebd3a` | `40b9aadac6268905477c2c98418429bd93fc3acb8c8a5b0a5160298ea6aebd3a` | Byte-identical |

One pristine normal receiver build at the same HEAD (CPUAPP embeds
APP_COMMIT; hash derived at execution time). Resolved config proved
`CONFIG_AUDIO_OFFLOAD_ASRC=y` and `CONFIG_BT_ISO_RX_BUF_COUNT=3`:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `49d22c3a3cd089221b1963150db3dd8a602e19eeeab85434794b75da0944c66e` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656` |
| Receiver CPUAPP | `49d22c3a3cd089221b1963150db3dd8a602e19eeeab85434794b75da0944c66e` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Host-side software verification before the run: native source-app Twister
`68/68` passed (`west twister -T tests/unit/hil_source_app -p
native_sim/native/64`), and the runner host regression
(`tests/hil/rh2_test.py` + `tests/hil/rh3_matrix_test.py`) passed `257`
tests. Build output contained only the documented dirty-tree notice and the
global `__ASSERT()` notice.

## Runner outcome and frozen limits

The source completed its full lifecycle with terminal `verdict="pass"`:
`streaming` at `monotonic_ms=20092`, active snapshot
`seq=61 sub=61 sc=0 sf=0 cb=59 out=2`, `scored_complete` at `153341`,
`teardown` at `158288`, terminal at `158833`, final idle retaining `seq=12644`
with reset counters. All 12644 SDUs were submitted and all callbacks fired
(`sub=12644`, `sf=0`); the source host is exonerated of starvation.

The sole required receiver stream summary was:

```text
Stream[0] summary: SDUs=12643 decoded=27664 plc=2378 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=12643 rx_error=0 rx_lost=1189 rx_unknown=0 rx_no_ts=7
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 12643 | 27664 | 2378 | 0 | 0 | 0 | 0 | 12643 | 0 | 1189 | 0 | 7 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: plc=2378 above ceiling: need <= 1383 (5% of decoded=27664)
```

`rx_valid=12643` met its floor (no rx_valid term appears in the failure
detail); `rx_error=0`, `rx_unknown=0`, `empty_sdu=0`, `decode_err=0`,
`i2s_underrun=0`, and `stream_reset=0` all met their gates. No receiver
runtime warning, source warning, assertion, or shell error was retained on
either console.

## FLPR, QoS, and ISO tail

The active 10 ms FLPR snapshot met the active predicate:

```text
State       : ACTIVE / epoch=1696991133 gen=2
Counters    : submit=111 success=111 fallback=0 busy=0
Faults      : (all zero)
```

ASRC offload retained `submit=115 success=115 fallback=0`. Limits validation
failed before post-stop collection, so no post-stop FLPR snapshot is
retained, matching the established pattern.

The receiver QoS log retained the frozen shape:

```text
QoS: interval 10000 framing 0x00 phy 0x02 sdu 240 rtn 5 latency 20 pd 40000
```

The ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=6037 retransmitted=0 crc_error=6 rx_unreceived=8 duplicate=107 iso_interval_1250us=8 nse=3 cig_sync_us=4146 cis_sync_us=4146 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=16
```

**Mandatory on-air sanity check: PASS.** Resolved CPUNET config proves the
SDC build; receiver QoS and ISO tail retain the frozen 2M, RTN5, 240-byte
Mode B shape (`c_phy=2`, `c_max_pdu=240`, `nse=3` present). SDC chose a much
tighter schedule than SW-split (`cig_sync_us=4146` at ~41% duty versus
`8184` at ~82%), so the fixture now has large scheduling margin.
Classification is therefore permitted.

## Analysis

Bounded comparison across the three fixture configurations at this row:

| Observation | SW-split SN_STRICT=y | SW-split SN_STRICT=n | SDC (this run) |
| --- | ---: | ---: | ---: |
| `rx_valid` of 12644 | 113 | 10141 | 12643 |
| `plc` | 27196 | 14800 | 2378 |
| `rx_lost` | 13598 | 7400 | 1189 |
| Controller `rx_unreceived` | 13571 | 7231 | 8 |
| `crc_error` | 1 | 2 | 6 |
| `cig_sync_us` (duty) | 8184 (~82%) | 8184 (~82%) | 4146 (~41%) |
| `nse` | 6 | 6 | 3 |
| Source streaming window (s) | ~137 (nominal) | 169.4 (stretched 34%) | 133.25 (stretched 5.4%) |

Two bounded observations follow:

1. **SDC removes the SW-split loss class.** Delivery is essentially perfect
   (12643/12644) with near-zero unreceived events (8) and a 41%-duty
   schedule. The receiver decodes every delivered SDU with zero decode
   errors, zero I2S underruns, and zero push faults.
2. **The entire residual failure is empty-event concealment.** `plc=2378`
   equals exactly `2 x rx_lost=1189` (two Mode B frames concealed per empty
   event), and `decoded=27664` equals `2 x 13832` callback events for 12644
   SDUs. The stream stretched 5.4%: the receiver fired 1189 events in which
   no payload was transmitted.

Mechanism (fixture-side, consistent with every counter): SDC preserves
stream alignment, transmitting each SDU in its designated CIS event. The
completion-paced source host refills after each HCI completion callback at
outstanding target 3; when a refill lands just past an event's preparation
point, SDC transmits the SDU in the next event and the current event fires
empty, which the receiver logs as LOST and conceals. The single retained
active snapshot shows the live queue at `out=2` (below target, zero margin).
Under SW-split the same lateness expired (SN_STRICT=y) or cumulatively
shifted (SN_STRICT=n) the payloads; under SDC it costs at most one event per
miss. The per-miss cost fell from catastrophic to two concealed frames, but
at target 3 the misses still accumulate to 1189 events.

## Classification and follow-up

Fixture defect: source-host TX pacing margin versus the controller's event
preparation. Not a receiver defect (delivery, decode, and I2S are clean; the
receiver conceals exactly as designed). Not an environment defect. Not
acceptance evidence.

The fix follows from the mechanism: deepen the per-stream TX outstanding
target from 3 to 6 so the host always holds up to six SDUs (60 ms) of
scheduling margin. The lever is the committed Kconfig
(`CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET`, int, default 3, range 1..6), the
fragment (`tests/hil/source-txout6.conf`) is committed from the ModeA8
phase, and ModeA8 hardware already proved the live queue reaches `out=5` at
target 6. The ModeA10 handoff explicitly deferred queue-depth matching to a
follow-up handoff: `docs/development/system-hil-rh3-modea11-sdc-txout6-handoff.md`
(ModeA11). One fix-validation rerun on the same row is authorized by this
handoff's other-boundary arm; no blind retry occurs because the mechanism is
classified and the fix targets it.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all retained entries. The
root and external JUnit files were byte-identical. Runner-retained raw
identity evidence: receiver `serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477
PART=0x00054b15`; source J-Link `001050023938` (raw scan in `source-jlink.txt`).

## Restoration and stop point

No receiver change to restore. The source fixture is flashed with the SDC
tuple from this run (authoritative in this run's `images.json`); the SDC
wiring stays uncommitted pending a passing row. Do not rerun or reuse this
ID; the next physical run is the ModeA11 fix-validation rerun under its own
handoff.