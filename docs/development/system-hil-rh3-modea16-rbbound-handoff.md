# RH3 ModeA16 handoff: readback-anchored grid bound

Status: approved one-run fix-validation diagnostic. Implements the
classification from the ModeA15 raw readback diagnostic
(`docs/development/system-hil-rh3-modea15-rbdiag-result.md`): the
readback values ARE the controller's CIG event grid (9999.05 us mean
advance across 12642 samples), the pins were ON that grid for the whole
stream (`pin_last - rb_last = 2 intervals`), and the collapse came from
the host free-running - HCI-level completions fire the instant the
controller ACCEPTS an SDU, the outstanding target never throttles, and
the controller flushes far-future-pinned SDUs to free buffers without
airing them.

## What changed (already implemented and software-verified)

In `hil/source/app/src/hil_source_app.c`, the base-wait block became a
two-condition gate, both keyed to the controller grid (never the host
clock):

1. Base wait (unchanged): no sends past the first untimestamped SDU
   until its readback establishes the pin base.
2. Grid bound (new): a stream's next send is allowed only while
   `tx_ts_next <= tx_rb_last + HIL_SOURCE_TX_TS_AHEAD_EVENTS x
   interval_us` (`HIL_SOURCE_TX_TS_AHEAD_EVENTS = 4`, defined in
   `hil_source_app.h` with the ModeA15 evidence in its comment).  The
   gate waits on the existing wake path (next completion's readback
   advances `tx_rb_last`; stop and runtime errors wake it; the progress
   timeout bounds the wait).

This keeps at most a few future-pinned SDUs in the controller - the
submission regime that delivered 12643/12644 in the ModeA12 gated run -
with no host-clock offset learning, no drift failure mode, and no
stale-pin guard. The ModeA15 diagnostic fields stay (they will directly
show the bound working: `pin_last - rb_last` must stay <= 4 intervals
throughout the run).

Software verification (this session): native Twister `70/70` three
times; two pristine source builds byte-identical (source CPUAPP
`f9a5144a9e53a40c6e3c15ee8e55a93432145847f8327c101f8d8d53bad09c8c`,
CPUNET unchanged `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`);
resolved app config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`; only
the documented notices.

## Fixed identity

```text
run ID: rh3-modeb-sdc-rbbound-20260907 (validate unused before invoking)
row:    rh3.fresh_mode_b_48_4_1
```

## Sequence

1. Preflight as usual (disk gate, HEAD + dirty tree expectation, run-ID
   validation, fixture validate).
2. Receiver: normal current-HEAD build (hashes derived at execution
   time; FLPR `45ab8d15...` expected unchanged; resolved config proves
   `CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_BT_ISO_RX_BUF_COUNT=3`).
3. Source builds: already double-proven at this tree state; rebuild once
   to re-hash if the tree changed since.
4. ONE runner run (outer timeout 3600000 ms):

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-modeb-sdc-rbbound-20260907 \
     --junit /tmp/opencode/hil-runs/rh3-modeb-sdc-rbbound-20260907.junit.xml \
     --row rh3.fresh_mode_b_48_4_1
   ```

5. Extract the status records' raw fields; verify `pin_last - rb_last`
   stayed within the bound; the limits verdict decides the row.

## Prediction (falsifiable)

With the grid bound, at most 4 future-pinned SDUs sit in the controller
at any time (directly observable: `pin_last - rb_last <= 40000 us` in
every status record). The flush-by-buffer-pressure mechanism cannot
engage; the row passes frozen limits: `rx_valid >= 11379` (expected
~12644 minus startup transients), `plc <= 5%` of decoded (expected near
the healthy mono baseline's 12), streaming window back to ~126.4 s.

## Classification arms

- PASS: the timestamp-mode line is validated as the fixture's production
  provisioning. Write the ModeA16 result doc, update resume-state,
  commit everything (SDC rework + timestamp-mode source + tests + docs)
  as:

    fix(hil): switch fixture to SDC timestamp-mode ISO provisioning

  Then report readiness for the full RH3 matrix attempt (`run-rh3-matrix`,
  new run ID `rh3-matrix-<date>-2`, 14 child runs, outer timeout
  10800000 ms).

- FAIL with the bound demonstrably held (`pin_last - rb_last <= 40000`
  in every record) but delivery still collapsed: the flush mechanism is
  not (only) buffer pressure. Record, leave uncommitted, STOP for user
  review with the raw fields as direct evidence; next candidates are
  controller-side (FT/nse/bn structure, CIG reserved time, HCI buffer
  counts).

- FAIL with the bound NOT held (a status record shows a larger gap):
  the gate itself is buggy on hardware. Record, leave uncommitted, one
  bounded fix-validation of the gate with the raw fields as evidence.

## Result documentation

`docs/development/system-hil-rh3-modea16-rbbound-result.md`:
prediction vs outcome, all status records' raw fields, per-slot summary,
limits verdict, FLPR active, QoS, ISO tail, integrity, raw identity,
restoration, stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single run. Status
0/1/130 immutable. Preserve all prior evidence roots.