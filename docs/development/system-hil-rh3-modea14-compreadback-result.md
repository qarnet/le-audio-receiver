# RH3 ModeA14 completion-driven readback result

Status: completed one-run fix-validation with the SAME collapse signature
as ModeA13, falsifying the ModeA13 classification and reaching the plan's
two-consecutive-same-signature stop point for the timestamp-mode line.
The run (`rh3-modeb-sdc-compreadback-20260907`) reproduced ModeA13's
numbers almost exactly: `rx_valid=167` of 12644 (identical), `plc=28812`
above `1457` (5% of `decoded=29146`), `rx_lost=14406`, completions for
every submitted SDU (`sub` and `cb` tracking, `out=0` at the active
snapshot), and the stream window stretched 11% (`streaming` 20104 ->
`scored_complete` 161536). The completion-driven base learning (the
iso_time_sync chain: read the assigned timestamp only after the first
SDU's completion) changed nothing: after roughly SDU 167 the controller
flushed every subsequent pinned SDU while still completing it at HCI
level, so the source "submitted" the whole stream into a flushing
pipeline. The deterministic 167 in both no-gate runs, against ModeA12's
12643 delivered under the gated design, leaves the readback/timestamp
semantics on this Zephyr-host-over-IPC configuration unresolved from
documentation and two falsified classifications. Per the plan of record
(`docs/development/system-hil-milestones.md`, "Failure triage and rerun
discipline": a second consecutive same-signature failure is a stop point
for redesign review, not another identical attempt), hardware iteration
on this row stops here for user review. Not acceptance; nothing is
committed.

## Scope and immutable evidence

One runner-owned execution used the completion-driven timestamp-mode
source build and the normal current-HEAD receiver build:

```text
run ID: rh3-modeb-sdc-compreadback-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-compreadback-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-compreadback-20260907.junit.xml
```

`environment.json` records the sole authorized `run` invocation with
`command.status=0`. `result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`.

## The three-run evidence chain

| Observation | ModeA12 (gate + guard) | ModeA13 (no gate, send-driven readback) | ModeA14 (no gate, completion-driven readback) |
| --- | ---: | ---: | ---: |
| `rx_valid` of 12644 | 12643 | 167 | 167 |
| `rx_lost` | 2049 | 14420 | 14406 |
| `plc` | 4098 | 28840 | 28812 |
| `plc = 2 x rx_lost` | exact | exact | exact |
| Streaming window (s) | 141.85 | 141.56 | 141.43 |
| Completions | all | all (instant after collapse) | all (instant after collapse) |
| Guard fires (`pin_adv`) | 2040 | n/a (removed) | n/a (removed) |

What the chain establishes:

1. Timestamp pinning itself is accepted by the controller when the
   host-side gate paces submissions (ModeA12 delivered 12643/12644): the
   pin VALUES were on the controller's event grid for essentially the
   whole run.
2. Without the gate, both readback chains (send-driven and
   completion-driven) collapse at exactly the same stream position
   (`rx_valid=167` in both runs), with HCI-level completions continuing
   for every flushed SDU: the pinned timestamps systematically evaluate
   as past and are flushed after a deterministic trigger point roughly
   1.7 s into streaming.
3. The ModeA13 classification (pre-air readback returns a wrong base)
   is falsified: ModeA14 read the base only after the first SDU aired,
   and the collapse signature is byte-identical. The ModeA12
   classification (host-clock offset bias) explains only ModeA12's
   `pin_adv` regression, not the no-gate collapse.

What remains unresolved is the actual on-hardware semantics of the SDC
HCI VS ISO Read TX Timestamp values and the flush evaluation of pinned
timestamps on this Zephyr-host-over-IPC configuration: the installed
documentation carries two different descriptions of the returned
timestamp ("start of the CIG event in which the first PDU containing
the SDU is scheduled for transmission" in
`nrfxlib/softdevice_controller/include/sdc_hci_vs.h`; "the last
possible point in time that the previous SDU could have been provided"
in the `nrf/samples/bluetooth/iso_time_sync` sources), and the three
runs above cannot discriminate between them (or a third behavior)
without reading the raw values at the collapse point.

## Software verification (this change, before the run)

- Native source-app Twister: `70/70` passed three times (the suite now
  includes the base-completion precondition in the suppression tests;
  `testcase.yaml` timeout raised to 300 s for the legitimate
  stall-path durations).
- Two pristine source builds byte-identical: source CPUAPP
  `4f2334f8e6a204be8b5c3a70ea0d867b27c7067fb12b80c1b67bc9f5daf3fe6e`,
  CPUNET unchanged `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`.
- Resolved app config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`.
- Host runner regression: `257` passed.
- Only the documented dirty-tree and global `__ASSERT()` notices.

Receiver: normal current-HEAD `6467e84` build, CPUAPP
`3e12402d90cff3a66d278edde2df6c5e083e76820f7825750979d437426baec5`, FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
`images.json` is authoritative for the flashed tuple.

## Runner outcome

Source lifecycle completed with terminal `verdict="pass"`; all 12644
SDUs submitted and completed.

```text
Stream[0] summary: SDUs=167 decoded=29146 plc=28812 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=167 rx_error=0 rx_lost=14406 rx_unknown=0 rx_no_ts=7
```

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: rx_valid=167 below floor: need >= 11379 (90% of 12644 submitted); plc=28812 above ceiling: need <= 1457 (5% of decoded=29146)
```

ISO tail retained `nse=3`, `cig_sync_us=4146`, `c_max_pdu=240`,
`c_phy=2`, `retransmitted=0`, `crc_error=0`. No warnings or errors on
either console; integrity and raw identity checks passed.

## Stop point and options for review

Per the plan's rerun discipline this is the stop point: the next
hardware action must be a user-reviewed decision, not another classified
attempt. The options the evidence supports:

1. Bounded instrumented diagnostic (recommended): a diagnostic-only
   source build that records the RAW VS readback values and the pin
   values actually sent (for example as extra tolerated fields in the
   existing HIL1 status record, as the `pin_adv` field already proved
   tolerable), run once on this row. This discriminates the readback
   semantics and the flush behavior with direct evidence at the
   collapse point, closing the classification question before any
   further design iteration.
2. ModeA15 design attempt without new evidence: restore the ModeA12
   gate + stale-pin guard (which delivered 12643) with the offset
   resynced from every completion readback instead of once. Grounded in
   the delivery evidence but does not resolve the semantics question;
   risks a third falsified classification.
3. Fixture-controller review: step back from timestamp mode and
   reconsider the fixture design (for example a source host paced by a
   hardware timer against the completion path, or the SDC central
   scheduling knobs), at plan-revision level.

Everything (SDC rework, timestamp-mode source change, tests) stays
uncommitted pending the decision. Preserve this evidence root; do not
rerun this ID.