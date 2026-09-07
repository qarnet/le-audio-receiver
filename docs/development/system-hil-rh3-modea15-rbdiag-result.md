# RH3 ModeA15 raw readback diagnostic result

Status: completed one-run bounded diagnostic with a decisive outcome.
The raw SDC HCI VS ISO Read TX Timestamp values prove the readback IS
the controller's CIG event grid (mean advance 9999.05 us per readback
across 12642 samples, no wrap, no domain anomaly) and the pinned
timestamps WERE on that grid for the whole stream (`pin_last` stayed
2 intervals ahead of `rb_last`, exactly the spec-advance design), yet
the controller aired only 167 of 12644 SDUs. The collapse is therefore
NOT a wrong base, NOT a clock-domain error, and NOT a host chain bug:
the host free-runs (HCI-level completions fire instantly, the
outstanding target never engages, pins advance 10 ms per submission
while the radio airs one event per 10 ms), the controller's ISO TX
buffer pool fills with far-future-pinned SDUs, and the controller
flushes them to free buffers without airing (HCI completion for every
submitted SDU, `sf=0`, only 167 aired). This is interpretation row 1
of the pre-committed table, refined by the buffer-pressure mechanism:
the flush evaluation is not timestamp-pastness of individual SDUs; it
is future-pinned buffer saturation. The ModeA12 gated run corroborates:
its host-side gate throttled submissions to ~real time, at most a few
future pins sat in the controller, and 12643/12644 delivered. The
classified fix (ModeA16) bounds the pin-ahead distance against the
CONTROLLER grid itself (readback-anchored gate; no host clock, no
drift failure mode). Not acceptance; everything stays uncommitted.

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
3. **Host free-run proven.** The active snapshot shows 29 submissions
   with completions already equal (HCI-level instant), `out=0`: the
   outstanding target never throttles, so pins advance at host encode
   speed (~30 submissions in the first ~0.9 s, pin span ~280 ms pinned
   in 90 ms of wall time) while the radio airs one event per 10 ms.
   Over the run the host pinned the entire 126.44 s event grid into the
   controller within seconds.
4. **Controller flushed future-pinned SDUs without airing them.**
   Receiver summary: `SDUs=167 decoded=29146 plc=28812 ... rx_valid=167
   rx_lost=14406` (identical collapse to ModeA13/14, as predicted for
   this diagnostic). Source: `sub=12644, sf=0` — every HCI ISO send was
   accepted and completed; only the first ~167 events (the first ~1.7 s
   before the buffer pool saturated) carried data on air. Per the SDC
   documentation, far-future-pinned SDUs hold their HCI buffers ("it
   may take some time before the corresponding HCI buffers are freed");
   under the resulting pressure the controller freed buffers by
   flushing the pinned SDUs.

## Prediction versus outcome

The handoff predicted the collapse reproduction (met: `rx_valid=167`,
`plc=28812` within noise of ModeA13/14) and that the raw fields would
discriminate the interpretation rows (met: row 1, refined to the
buffer-saturation mechanism by the `rb_cnt`/completion evidence).

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

## Classification and next step

Fixture defect: unbounded future-pinned submission depth saturating
the controller's ISO TX buffers (HCI-instant completions disable the
outstanding backpressure). The classified fix (ModeA16): a
readback-anchored send gate — allow the next pinned send only when
`tx_ts_next <= rb_last + K x interval_us` with a small K (the
outstanding target plus margin, ~4), bounding how far ahead of the
controller's own schedule the host may queue. This reproduces the
ModeA12 delivery regime (few future pins in the controller) keyed to
the controller grid instead of the host clock (no offset learning, no
drift, no stale-pin guard). One fix-validation run under its own
handoff (`docs/development/system-hil-rh3-modea16-rbbound-handoff.md`).
Everything stays uncommitted pending a passing row. Preserve this
evidence root; do not rerun this ID.