# RH3 ModeA15 raw readback diagnostic result

> [!WARNING]
> Historical run record. The readback and pin measurements remain valid, but
> HCI completions and receiver loss do not by themselves prove which SDUs the
> controller aired or why it returned buffers. The buffer-saturation
> classification and proposed next fix are superseded by
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status: completed one-run bounded diagnostic. The raw SDC HCI VS ISO Read TX
Timestamp values establish a 9999.05 us mean readback advance across 12642
samples, with no wrap or observed domain anomaly. The pinned timestamps stayed
two intervals ahead of `rb_last` while the receiver recorded only 167 valid
SDUs of 12644 submitted. HCI completions returned for every submitted SDU and
`sf=0`.

At the time, the pre-committed interpretation attributed the result to
future-pinned controller-buffer saturation and selected a controller-grid bound
as ModeA16. That hypothesis is part of the historical decision trail, but these
measurements did not rule out a host scheduling or throughput cause. The later
controller-clock scheduler and 128 MHz A/B evidence supersede that causal
classification. This run is not acceptance evidence.

## Scope and immutable evidence

One runner-owned diagnostic execution used the ModeA15 instrumented
source build (ModeA14 behavior unchanged plus six additive status
fields) and the normal current-HEAD receiver build:

```text
run ID: rh3-modeb-sdc-rbdiag-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-rbdiag-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-rbdiag-20260907.junit.xml
```

`environment.json` records the sole authorized `run` invocation with
`command.status=0`. `result.json` records `outcome="failed"` (expected;
this diagnostic's signal is the raw fields, not the limits verdict),
`first_failed_boundary="session end"`, `cleanup_failures=[]`.

## Raw diagnostic fields (the run's evidence)

| Status record | seq | rb_first | rb_min | rb_max | rb_last | rb_cnt | pin_last |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| active (streaming) | 29 | 19510229 | 19510229 | 19790229 | 19790229 | 29 | 19790229 |
| final idle | 12644 | 19510229 | 19510229 | 145920229 | 145920229 | 12642 | 145940229 |

Derived facts:

1. **Readback = event grid.** `rb_max - rb_first = 126410000 us` over
   `rb_cnt = 12642` readbacks: mean advance 9999.05 us, i.e. one SDU
   interval per readback, no jumps (`rb_min == rb_first`), no wrap
   within the segment. The returned values are the CIG event starts
   assigned to the submitted SDUs.
2. **Pins on the grid.** `pin_last - rb_last = +20000 us` (two
   intervals) at the final record: the last submitted pin plus the
   not-yet-read-back pending SDU — exactly the spec-advance chain.
   At the active snapshot `pin_last == rb_last` (29 readbacks, 29
   submissions, no divergence). Every pinned timestamp was a valid
   future event on the controller grid.
3. **Host submission outran event cadence in the retained snapshot.** The
   active snapshot shows 29 submissions with completions already equal and
   `out=0`. Pins advanced at host encode speed: about 30 submissions in the
   first ~0.9 s, with about 280 ms of pin span created in 90 ms of wall time.
4. **Receiver delivery collapsed while HCI sends completed.** Receiver summary:
   `SDUs=167 decoded=29146 plc=28812 ... rx_valid=167 rx_lost=14406`.
   Source summary: `sub=12644, sf=0`. This establishes the endpoint behavior,
   but not whether each absent SDU was never aired, aired but not received, or
   returned by a particular controller flush mechanism.

## Prediction versus outcome

The handoff predicted the collapse reproduction (met: `rx_valid=167`,
`plc=28812` within noise of ModeA13/14). It classified the raw fields as its
row 1 at the time. Later evidence shows that the fields did not uniquely
discriminate controller buffer saturation from source scheduling and throughput.

## Build proof and software verification

Native source-app Twister `70/70`; two pristine source builds
byte-identical (source CPUAPP
`32165c7543b86143510068cd027e4992ad0c318051ffe8840211faa9632f86a3`,
CPUNET unchanged `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`);
resolved app config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`; only
the documented dirty-tree and global `__ASSERT()` notices. Receiver:
normal current-HEAD build (`3e12402d...` CPUAPP, `45ab8d15...` FLPR,
`CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_BT_ISO_RX_BUF_COUNT=3`).
`images.json` is authoritative for the flashed tuple.

## Historical classification and next step

The run was classified as a fixture defect caused by unbounded future-pinned
submission depth saturating the controller's ISO TX buffers. That mechanism was
a hypothesis, not a direct measurement. The selected fix (ModeA16) was a
readback-anchored send gate — allow the next pinned send only when
`tx_ts_next <= rb_last + K x interval_us` with a small K (the
outstanding target plus margin, ~4), bounding how far ahead of the
controller's own schedule the host may queue. This reproduces the
ModeA12 delivery regime (few future pins in the controller) keyed to
the controller grid instead of the host clock (no offset learning, no
drift, no stale-pin guard). One fix-validation run under its own
handoff (`docs/development/system-hil-rh3-modea16-rbbound-handoff.md`). Preserve
this evidence root and do not rerun this ID. Use the controller-clock result for
the current implementation and next gate.
