# RH3-ModeA1b offload-disabled runner support handoff

Status: fixture-defect repair escalated from RH3-ModeA1 (commit `13abf5e`,
run `rh3-modea1-20260903-offload-disabled` failed at `receiver active` with
`FLPR active offload deadline expired after 149 attempt(s)`). Software phase;
the rerun is a separate later phase.

## Grounding

1. Run evidence: offload-disabled diagnostic image (CPUAPP
   `91223b359b49f92eb1e5b7b2a03b025939d74ec40d8dda4598053f6d45347a84`)
   reached `ACTIVE` offload state with `submit=0 success=0 fallback=0 busy=0`
   - exactly correct for an offload-disabled plane.
2. `scripts/hil/receiver.py` `_validate_active_offload` (line ~3473) hard
   requires `submit >= 1` for `48_4_1`: `errors.append("offload submit
   missing or zero")`. The deadline poll in `runner.py`
   `_collect_receiver_active_offload` (line ~1395) then expires. The receiver
   side is behaving exactly as designed; the runner's validation has no
   offload-disabled concept.
3. `src/audio_offload.c` compiles the FLPR transport/handshake/state machine
   on nRF54L15 regardless of `CONFIG_AUDIO_OFFLOAD_ASRC`; only the
   `audio_i2s.c` submit call (line ~317) is compile-gated. So offload-disabled
   legitimately shows `state=ACTIVE`, `submit=0`.
4. Precedent seam: `allow_moving_single_pending` threads
   runner -> `validate_receiver_lifecycle_blocks` -> `_validate_active_offload`
   (runner.py lines 1371/1473, receiver.py 3461/3476/3555/3583/3739/3751).
5. Post-stop validation `48_4_1` path (receiver.py ~3608-3621) similarly
   requires `submit >= 1` for the terminal `STOPPED` snapshot - must accept
   `submit == 0 and success == 0` in the same mode, or the rerun would fail
   at `receiver post-stop` instead.

## Goal

Add one explicit, opt-in runner mode accepting an offload-disabled receiver
image for direct `run` invocations. Default behavior unchanged; matrix paths
never pass the flag (matrix rows always use the production image).

## Exact changes

### 1. `scripts/hil/receiver.py`

`_validate_active_offload(..., allow_moving_single_pending=False)` gains
`allow_offload_disabled=False`:

- In the `48_4_1` branch: when `allow_offload_disabled` is true, require
  `submit == 0 and success == 0` (a nonzero submit in this mode is an error:
  `offload submit=%d but offload-disabled run expects 0`). The
  `submit >= 1` requirement is replaced, not bypassed silently.
- Keep the `48_3_1` branch unchanged (it already expects zero counters).
- All other validations (state ACTIVE, fault fields, recovery fields) stay
  identical.

Thread `allow_offload_disabled` through
`validate_receiver_lifecycle_blocks` (signature + pass-through) and update
the post-stop validator so the same mode accepts terminal
`submit == 0 and success == 0` for `48_4_1` (nonzero submit/success remains
an error in this mode; production default still requires `submit >= 1`).

### 2. `scripts/hil/runner.py`

- `Runner.run(...)` gains `allow_offload_disabled=False` (bool-checked like
  the existing flags; TypeError on non-bool).
- Thread it to `_collect_receiver_active_offload` and
  `_collect_receiver_post_stop` call sites (lines ~1371, ~1473).
- In `_collect_receiver_active_offload`, when the mode is active the poll
  predicate is `state == ACTIVE` (already satisfied on first poll per the
  failed-run evidence); no deadline change.
- Store the mode in the row evidence: the flat `result.json` and
  `environment.json`-style `argv` already record the CLI flag; additionally
  include `"allow_offload_disabled": true` in the written row.json when set
  (small, additive; check where row.json is assembled near line ~1775).

### 3. `scripts/hil/cli.py`

`run` subparser gains:

```python
run.add_argument(
    "--allow-offload-disabled",
    action="store_true",
    help="accept an offload-disabled receiver image (diagnostic rows only)",
)
```

`cmd_run` passes it to `engine.run(...)`. Explicitly NOT added to
`run-rh3-matrix`, `run-rh4-matrix`, MA1/SA1 subparsers.

### 4. Tests (`tests/hil/rh2_test.py` + `scripts/test_hil_runner.py`)

Mirror the existing `_run_harness` pattern:

1. Offload-disabled mode with `flpr offload` transcript showing
   `state=ACTIVE submit=0 success=0`: row passes receiver-active and
   post-stop validation (prove the full row still passes limits with healthy
   summaries).
2. Same mode but submit=1: row fails with the new explicit error text.
3. Default mode unchanged: `submit=0` for `48_4_1` still fails
   (existing test coverage may already pin this - keep green).
4. CLI: `hil-runner.py run ... --allow-offload-disabled` maps to the runner
   kwarg; matrix CLI rejects the flag (argparse error) - assert via parser
   build.
5. Row evidence: row.json contains `"allow_offload_disabled": true` only when
   the flag is set.

## Out of scope

- Any change to `SUMMARY_TIMEOUT`, limits, rows, matrix schedule, source or
  receiver firmware, the offload-disabled fragment, or the failed
  `rh3-modea1-20260903-offload-disabled` evidence.
- Any hardware run in this phase (the rerun is the next phase with a NEW run
  ID: `rh3-modea1b-20260903-offload-disabled`; the first attempt stays
  immutable as the fixture-defect baseline).

## Verification

```bash
python3 -m py_compile scripts/hil/receiver.py scripts/hil/runner.py scripts/hil/cli.py
python3 -m pytest -q scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

All green (existing totals grow by new tests only). Commit exactly:

```text
fix(hil): accept offload-disabled receiver images in direct runs
```

Include the handoff. No attribution footers, no push.

## Return report

Changed paths; mode semantics (active + post-stop); new test names and what
each proves; totals before/after; commit hash; deviations.