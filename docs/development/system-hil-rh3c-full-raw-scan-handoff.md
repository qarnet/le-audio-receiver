# RH3c session-end full-raw scan handoff

Status: fixture-defect repair, root cause proven from immutable evidence of
both `rh3-modea1b-20260903-offload-disabled` and matrix child
`rh3-matrix-20260903-r.p1.r2.rh3.fresh_mode_a_48_4_1.80dd38b48348`. Software
only; no hardware in this phase.

## Root cause (proven)

The scored_complete tail hook issues the prompt-bounded receiver command
`bt iso quality` (`command_receiver`, 15 s window). The source's teardown
Disable PDU arrives a few seconds later; the receiver then prints the
Disable/summary/stopped/Release/Disconnected burst WHILE that command is
still collecting lines waiting for its prompt. `command_receiver` consumes
those lines into the tail transcript (`receiver-status.txt` - both runs
physically contain the summary lines there, e.g. modea1b lines 49 and 53).
Session-end then begins after those bytes and can never see the summaries:
not live (already consumed), not via the RH3b offset scan (offset captured
after the bytes). The raw-evidence fallback was sound but scanned the wrong
byte range.

Timeline anchor (modea1b): streaming 00:16:57, PCLK diag[140] 00:19:18.8,
`bt iso quality` block immediately after, Disable burst 00:19:21.47,
summaries 00:19:21.468/608, session-end failure with "raw evidence scan also
missing them after 333 bytes" - the 333 bytes are the post-burst tail.

## Goal

Session-end validation must find the segment's stream summaries wherever the
raw evidence retained them - live queue, tail transcripts, or raw file - and
apply identical slot/limit validation. The receiver prints each slot's
summary exactly once per stream lifetime, so the earliest complete summary
per slot within the segment's byte range is the segment's summary.

## Exact changes

### 1. `scripts/hil/runner.py`

- In the row `run` flow, capture `segment_summary_base =
  receiver_console.bytes_received()` immediately BEFORE the
  `scored_complete_hook`/tail collection runs (i.e., right after the
  streaming-phase active-offload collection completes for that segment), and
  pass it into `collect_summary` so `_step_session_end` scans from
  `segment_summary_base` instead of its own entry offset. For multi-segment
  rows, the base must be captured per segment BEFORE that segment's tail, so
  segment N never adopts segment N-1's summaries.
- `_step_session_end(receiver_console, source_client, row, segment,
  scan_offset)`: rename the internal offset to the passed-in
  `scan_offset`; the live-wait loop and both raw-fallback sites use it
  unchanged otherwise.
- In `raw_fallback_summaries`, when a slot appears multiple times in the
  scanned range (possible now that the range includes the tail window),
  keep the FIRST complete record for each slot (earliest = this segment's);
  do NOT raise duplicate-slot on the scan path (the live path keeps its
  existing duplicate detection; a duplicate within the scanned raw window is
  a natural consequence of transcript retention, not a protocol violation).
  Record in the returned summaries that they were raw-derived (internal
  marker only if needed for the result doc; validation is identical).
- The live wait stays exactly as is: if the summaries appear in the queue
  (normal healthy rows where teardown is slow relative to tail), the live
  path consumes them and the fallback never runs.

### 2. Tests (`tests/hil/rh2_test.py`)

Extend the RH3b fallback tests:

1. Summaries consumed by the tail transcript: construct a wire where the
   summary lines arrive DURING the `bt iso quality` prompt-bounded
   collection (deliver them between the iso-quality output and its trailing
   prompt in the wire sequence). Assert the row PASSES (healthy values) with
   summaries recovered via the segment-base raw scan, and that
   `receiver-status.txt`-equivalent transcript retention is unaffected.
2. Same construction with H42-style violating values: row FAILS at session
   end with BOTH limit violations (delivery floor and PLC ceiling) - proving
   limits apply to tail-swallowed summaries.
3. Segment scoping for reconnect rows: segment 0's summaries swallowed by
   segment 0's tail must satisfy segment 0; segment 1's scan base excludes
   segment 0's byte range (assert segment 1 cannot be satisfied by segment
   0's summaries - starve segment 1 and expect the missing-slot failure).
4. Duplicate-slot within scanned range: two summary prints for slot 0 in the
   scanned window (simulating transcript + queue retention) - scan keeps the
   first, row still passes; live path duplicate detection unchanged
   (existing tests cover).

### 3. Docs

Update `docs/development/system-hil-rh3-modea1b-result.md` stop-point
section ONLY by appending one line noting the root cause was subsequently
identified and fixed in RH3c (commit hash filled after commit), pointing to
the RH3c handoff/result. Do not rewrite the recorded observations.

## Out of scope

- No change to tail collection itself (it correctly retains swallowed lines
  in `receiver-status.txt`), no timeout changes, no source/receiver firmware
  changes, no new hardware run (that is the next phase, new run ID, still
  pending).

## Verification

```bash
python3 -m py_compile scripts/hil/runner.py
python3 -m pytest -q scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Commit exactly:

```text
fix(hil): scan full segment raw evidence for stream summaries
```

Include the handoff. No attribution footers, no push.

## Return report

Changed paths; scan-base semantics; duplicate handling; new test names with
what each proves; totals before/after; commit hash; deviations.