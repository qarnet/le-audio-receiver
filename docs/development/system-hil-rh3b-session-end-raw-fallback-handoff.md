# RH3b session-end raw-evidence fallback handoff

Status: fixture-defect repair from the first RH3 matrix attempt
(`rh3-matrix-20260903-rh3a`, commit `ea73413`). Software only; no hardware
execution in this phase.

## Grounding (from immutable matrix evidence)

Child `rh3-matrix-20260903-r.p1.r2.rh3.fresh_mode_a_48_4_1.80dd38b48348`
failed at session end with `missing receiver stream summary slot(s): [0, 1]`,
yet its retained `receiver-console.bin` contains both complete, healthy-grammar
summary lines:

```text
[00:07:33.019,747] <inf> bt_bap: Stream[0] summary: SDUs=135 decoded=28726 plc=28458 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=135 rx_error=0 rx_lost=14305 rx_unknown=0 rx_no_ts=85
[00:07:33.159,310] <inf> bt_bap: Stream[1] summary: SDUs=133 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=133 rx_error=0 rx_lost=14244 rx_unknown=0 rx_no_ts=8
```

Root cause of the miss (fixture class): the source emits its `teardown` state
record when teardown STARTS; under the starved Mode A state the receiver
processed ASCS Disable (and therefore printed the summaries) more than the
runner's fixed `SUMMARY_TIMEOUT = 60.0` seconds later. The runner failed at
60 s while the summaries were still in flight; cleanup kept the console open
and the raw evidence retained them.

The same child also exhibits the true product defect (Mode A dual-CIS
delivery starvation: `rx_valid=135/133` of `12644` per stream) which the
frozen limits must be allowed to report. With this fallback, the row will
still fail, but with the truthful transport-limit violations instead of a
capture miss. That is the point: the runner must never report missing
evidence that it retained.

## Goal

When the live session-end wait cannot produce the required per-slot
summaries, validate them from the console's own retained raw evidence before
failing. Slot presence/duplication/range checks, and all frozen transport
limits, apply identically to raw-derived summaries.

## Exact changes

### 1. `scripts/hil/serial_io.py`

Add two public accessors to `SerialConsole`:

```python
def bytes_received(self):
    """Absolute count of RX bytes retained so far (mark-safe offset source)."""

def raw_text_since(self, offset):
    """Decode retained raw RX evidence from byte offset to current end.

    Returns (text, decode_failed) where decode_failed is True if any byte in
    the range could not be decoded as UTF-8; undecodable bytes are replaced.
    The evidence file is append-only under the reader thread; reading it is
    safe while the writer runs. A trailing partial line simply will not match
    the summary grammar.
    """
```

Implementation notes:
- `bytes_received()` returns `self._bytes_received` under `self._cond`.
- `raw_text_since(offset)` opens `self.evidence_path` in `"rb"` and reads from
  `offset`. If the file does not exist yet, return `("", False)`. Guard
  `offset` to `[0, file_size]`. Use `errors="replace"` decode; set the flag
  when a replacement occurred. This accessor is read-only; it must not touch
  `_decode_error` (that field stays owned by strict line decoding).

### 2. `scripts/hil/runner.py` `_step_session_end`

Restructure so the live wait and a raw fallback share one validation path:

- Capture `summary_scan_offset = receiver_console.bytes_received()` at the
  top of `_step_session_end`.
- Keep the existing live wait loop unchanged.
- At BOTH existing failure sites (deadline expiry and `summary_line is
  None`), instead of raising immediately: read
  `text, _decode_failed = receiver_console.raw_text_since(summary_scan_offset)`
  and parse `receiver.parse_stream_summary(text)` once (the parser is
  transcript-based and tolerates log prefixes/VT100).
  - Build summaries from parsed records with the SAME slot
    presence/duplication/range rules and the SAME zero-field and
    `validate_stream_transport` enforcement as the live path.
  - If the raw scan yields the complete required slot set, proceed exactly as
    if the lines had arrived live (the row continues; limits still fail the
    row when violated, with exact observed values).
  - Only if the raw scan also lacks any required slot do you raise the
    existing `missing receiver stream summary slot(s)` error, appending
    `"; raw evidence scan also missing them after <N> bytes"` so the failure
    detail proves the fallback ran.
- Do not lengthen `SUMMARY_TIMEOUT` in this phase; the fallback is the fix.
  (Lengthening would only mask slow teardown, and the raw scan is
  deterministic.)

### 3. Tests (`tests/hil/rh2_test.py`)

The test seam is the real `SerialConsole` over `hil_fakes.FakeSerial` with
`summary_timeout=2.0` already injected by the harness (line ~317). Add
tests, using the existing `_run_harness`/wire pattern:

1. **Fallback rescues late summaries**: build a passing mono wire but deliver
   the summary line bytes to the receiver wire only AFTER the live wait
   should have expired. Practical construction: the harness runs with
   `summary_timeout=2.0`; the FakeSerial `read` loop pulls chunks
   continuously, so timing-based late delivery is flaky. Instead, use a
   custom Wire subclass that queues the summary chunk behind a gate that
   opens only once the runner has stopped waiting (e.g., first `read` after
   `wire.summary_release()` is called from a small helper thread after ~3 s,
   or simpler: extend the existing wire object so that the summary chunk is
   held in `pre_mark_chunks` and released by an explicit hook - acceptable
   simpler alternative: append the summary bytes to the receiver console's
   evidence file directly from the test after opening it, since
   `raw_text_since` reads the file; but the reader thread owns the file, so
   instead call `serial.write` path... If direct file injection proves
   fragile, implement the test at the unit seam instead: construct one real
   `SerialConsole` with a FakeSerial whose wire returns no summary bytes,
   write the summary line into the console's evidence file via the reader
   (wire chunk), advance the injected `clock` past the deadline, and call
   `_step_session_end` directly. Choose the construction that is
   deterministic; prefer file-injection through the wire if workable.)
   Assert: row outcome passed (values healthy), i.e. the fallback found the
   summaries and validation continued.
2. **Fallback enforces limits on raw-derived summaries**: same setup with
   H42-style numbers (`rx_valid=24`, `plc=38924`, `decoded=38972`) for a
   `48_3_1`-shaped expected count; assert the row fails at session end with
   BOTH `rx_valid=24 below floor` and `plc=38924 above ceiling` in the
   failure detail (proving limits apply to raw-derived summaries).
3. **Missing summaries still fail with proof of fallback**: wire delivers no
   summaries at all; assert failure detail contains
   `missing receiver stream summary slot(s)` AND `raw evidence scan also
   missing`.
4. **Segment scoping**: for a reconnect-shaped two-segment row (reuse
   existing reconnect test setup), summaries from segment 0's byte range must
   not satisfy segment 1's wait: deliver segment-0 summaries normally (live
   path consumes), then starve segment 1's live wait while its raw range
   contains only segment-1 lines. Assert correct failure or rescue per what
   the evidence contains. Keep this test minimal; the offset scoping is the
   behavior under proof.

Also keep every existing test green: the live path must behave exactly as
before when lines arrive within the window.

## Out of scope

- Any change to `SUMMARY_TIMEOUT`, source firmware, receiver firmware,
  transport limits, rows, or matrix schedule.
- Any Mode A starvation diagnosis (separate named phase; the product defect
  remains open and is NOT claimed fixed by this repair).
- Hardware runs, flashing, serial, `nrf-probes`, evidence mutation, cleanup,
  garbage collection.

## Verification

```bash
python3 -m py_compile scripts/hil/serial_io.py scripts/hil/runner.py
python3 -m pytest -q scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected: all green; existing totals grow only by the new tests. Then commit
exactly:

```text
fix(hil): validate session-end summaries from retained raw evidence
```

Include the handoff file in the commit. No attribution footers, no push.

## Return report

Changed paths; accessor semantics; fallback behavior at each failure site;
new test names and what each proves; totals before/after; the commit hash;
deviations with reasons. Escalate rather than improvising if the FakeSerial
seam cannot express "bytes in evidence file but not in live queue"
deterministically - report the exact seam limitation and your candidate
construction instead.