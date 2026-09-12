# RH3a receiver transport limits handoff

Status: next implementation phase of the System HIL plan of record
(`docs/development/system-hil-milestones.md`, revised 2026-09-03). Software
only; no hardware execution in this phase.

## Goal

Implement the frozen receiver transport limits from the plan so a row that
delivers a small fraction of transmitted audio FAILS at session end instead of
passing, and scope the mandatory RH3 matrix to 10 ms rows. This closes the
correctness hole where H42 passed with `rx_valid=24` of `16859` submitted SDUs.

## Grounding

1. Plan of record, "Receiver transport limits (frozen 2026-09-03)" and
   "Standing decisions": `rx_valid` floor 90% of expected submitted, `plc`
   ceiling 5% of `decoded`, `rx_error=0`, `rx_unknown=0`, `empty_sdu=0` in
   addition to existing `decode_err/i2s_underrun/stream_reset=0`. Missing
   extended field fails closed. Values live in one runner-owned constant set.
2. Enforcement site: `scripts/hil/runner.py` `_step_session_end`
   (lines ~1489-1545) currently checks only slot presence/duplication and the
   three zero fields. The parsed summary dict from
   `receiver.parse_stream_summary` already carries `sdus`, `plc`, `decoded`,
   `rx_valid`, `rx_error`, `rx_lost`, `rx_unknown`, `rx_no_ts` (extended fields
   `None` when absent).
3. Row expected-submitted math: `RowSpec.expected_submitted_per_stream`
   (`scripts/hil/rows.py` lines 126-134) is preamble + scored + tail per
   segment, times segment count. Per-stream `rx_valid` must be at least 90% of
   `expected_submitted_per_segment` (one segment; reconnect rows compare per
   segment, and the summary observed per segment).
4. Matrix scope: `RH3_HEALTHY_ROWS` in `scripts/hil/rows.py` (lines 159-167)
   still contains the three `48_3_1` rows. Plan removes them from the mandatory
   matrix; they stay as selectable diagnostic rows.
5. Receiver firmware already emits the extended summary fields
   (`src/bt_bap.c` `teardown_transition` TEARDOWN_DISABLED log:
   `rx_valid=... rx_error=... rx_lost=... rx_unknown=... rx_no_ts=...`),
   confirmed in H42 evidence `receiver-console.bin` line 59 area and
   `receiver.py` regex lines 40-46 (extended group optional for older logs).
6. Host test for the session-end boundary: `scripts/test_hil_runner.py`
   contains fake-console row tests; the existing summary validation tests live
   near `TestSessionEnd` or equivalent (search `parse_stream_summary` and
   `session end` in that file). Follow the existing fake-lab pattern; never
   open serial in tests.

## Exact changes

### 1. `scripts/hil/receiver.py`

Add one frozen constant block near the top (after `WARNING_PATTERNS`):

```python
#: Frozen receiver transport limits (system-hil-milestones.md, 2026-09-03).
#: Basis: adjacent boards, 2M PHY, RTN 5; healthy 10 ms hardware delivers
#: essentially every submitted SDU (RH2 formal pass sub=764, SDUs=764).
#: Values are plan-of-record constants: changing one is a plan revision.
RX_VALID_RATIO_FLOOR = 0.90   # fraction of expected submitted SDUs per stream
PLC_RATIO_CEILING = 0.05      # fraction of decoded frames
```

Add one pure function (host-testable, no I/O):

```python
def validate_stream_transport(summary, expected_submitted):
    """Return list of limit violations for one receiver stream summary.

    Fails closed when extended fields are missing (None).
    """
```

Rules, exactly:

- `rx_valid is None` -> `"rx_valid missing (firmware summary without extended fields)"`.
- `rx_valid < RX_VALID_RATIO_FLOOR * expected_submitted` ->
  `"rx_valid=%d below floor: expected >= %d of %d submitted"` with the exact
  floor value and expected submitted.
- `rx_error` None or nonzero -> violation naming the value or missing.
- `rx_unknown` None or nonzero -> violation.
- `empty_sdu` None or nonzero -> violation.
- `plc` ceiling: `decoded > 0 and plc > PLC_RATIO_CEILING * decoded` ->
  violation with exact values. (`decoded == 0` is already a delivery failure
  caught by the rx_valid floor; do not add a separate zero-decoded rule.)
- Do NOT gate `rx_lost`, `rx_no_ts` (record-only per plan).
- Empty list means pass.

### 2. `scripts/hil/runner.py` `_step_session_end`

After the existing per-summary zero-field loop (lines ~1537-1544), add:

```python
expected_submitted = row.expected_submitted_per_segment
for summary in summaries:
    violations = receiver.validate_stream_transport(summary, expected_submitted)
    if violations:
        raise HilRunnerError(
            "session end",
            "stream summary slot %d transport limits: %s"
            % (summary["slot"], "; ".join(violations)),
        )
```

Keep the existing zero-field checks unchanged. The new failure detail must
carry exact observed value, frozen threshold, and expected submitted so a
failure is self-explaining (plan requirement).

### 3. `scripts/hil/rows.py`

Remove the three `48_3_1` entries from `RH3_HEALTHY_ROWS` (lines 163-165).
Keep them defined as named diagnostic rows so `hil-runner.py run --row
rh3.fresh_mono_48_3_1` stays selectable. Concretely: add

```python
RH3_7P5_DIAGNOSTIC_ROWS = (
    RowSpec("rh3.fresh_mono_48_3_1", "fresh", "mono", "48_3_1", 16000),
    RowSpec("rh3.fresh_mode_a_48_3_1", "fresh", "mode_a", "48_3_1", 16000),
    RowSpec("rh3.fresh_mode_b_48_3_1", "fresh", "mode_b", "48_3_1", 16000),
)
```

after `RH3_HEALTHY_ROWS`, include them in `ALL_ROWS` (so `row_names()` and
`ROW_BY_NAME` keep them), but NOT in `RH3_PASS_ROWS`. Update the comment above
`RH3_HEALTHY_ROWS` to reference the plan's standing decision (10 ms mandatory,
7.5 ms diagnostic-only until RH3-7p5).

### 4. `scripts/test_hil_runner.py` or a new `tests/hil/` host test module

Follow the existing test-file pattern (look at how current summary tests build
fake summaries). Add tests proving PUBLIC behavior:

- `validate_stream_transport` returns `[]` for a healthy summary (use real H42
  numbers scaled to healthy: `sdus=16000+, rx_valid=16200, plc=200,
  decoded=16400, expected 16859`).
- H42's actual failing summary (`rx_valid=24`, `plc=38924`, `decoded=38972`,
  expected 16859) produces violations for BOTH rx_valid floor and plc ceiling.
- Missing extended fields (`rx_valid=None`) fails closed.
- Boundary: `rx_valid` exactly at floor passes; one below fails. `plc` exactly
  at ceiling passes; one above fails.
- `rx_error/rx_unknown/empty_sdu` nonzero each produce a named violation.
- `rx_lost` and `rx_no_ts` large values produce NO violation (record-only).
- `rh3_schedule()` no longer contains `48_3_1` rows; `row_names()` still lists
  `rh3.fresh_mono_48_3_1` (diagnostic selectable); `RH3_PASS_ROWS` length is
  now 7 (4 healthy + reconnect + hang + stall) so matrix = 14 child runs.

## Out of scope

- No firmware changes (receiver already emits the fields; source unchanged).
- No hardware runs, no flashing, no serial, no nrf-probes.
- No changes to fault-row validation, capture, or qualification paths.
- No 7.5 ms diagnosis (that is RH3-7p5, after RH3a lands).
- No `STATUS.md`/public docs updates (separate documentation phase).
- No commit (user commits after review).

## Verification (all host, run from repo root)

```bash
python3 -m py_compile scripts/hil/receiver.py scripts/hil/runner.py scripts/hil/rows.py
python3 -m pytest -q scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/          # full host HIL suite incl. new tests
python3 -m pytest -q scripts/test_hil_runner.py tests/hil/  # if suites overlap
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected: existing suites stay green (report exact totals; `tests/hil` last
recorded 154/154 RH2 + 14/14 matrix in resume-state; report if pre-existing
dirty work changes counts, do not weaken). New tests add coverage, none fail.

Also prove the gate bites, host-side, with a one-liner sanity check:

```bash
python3 - <<'PY'
import sys
sys.path.insert(0, "scripts")
from hil import receiver
h42 = {"slot": 0, "sdus": 24, "decoded": 38972, "plc": 38924,
       "rx_valid": 24, "rx_error": 0, "rx_lost": 19462, "rx_unknown": 0,
       "rx_no_ts": 12, "decode_err": 0, "i2s_underrun": 0,
       "stream_reset": 0, "empty_sdu": 0}
print(receiver.validate_stream_transport(h42, 16859))
PY
```

Must print BOTH the rx_valid floor violation and the plc ceiling violation.

## Return report

Changed paths; exact new constants; test totals before/after; the sanity-check
output; `git diff --check` result; confirmation of no hardware/no commit; any
deviation from this handoff with reason. Do not modify the plan document.