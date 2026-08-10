# FR4 cadence tolerance review fix handoff

Date: 2026-08-10

## Goal

Close two narrow review gaps in `0f3ad2c` before hardware rerun:

1. structured observation must report computable off-grid error even when
   delivered callback positions exceed estimated events;
2. quarter-interval cap test must exercise the promised long no-TS delivered-
   position path, not only an over-bound omission.

No tolerance formula, result classification, production synthesis, warning
policy, API shape, or hardware scope change.

## Exact changes

In `src/audio_iso_seq.c`, compute `nearest_us` and absolute `err` immediately
after `event_count` and scaled tolerance, before RESYNC classification. Populate
`observation->error_us` with that computable value before checking
`event_count == 0` / `event_count < delivered`. Keep exact classification order
and reasons unchanged. Duplicate remains error 0. No synthesis change.

In `tests/unit/iso_seq/src/test_iso_seq.c`:

- update `test_cadence_observation_delivered_gt_events` to assert exact
  computed error (its existing on-grid case remains 0, so add a narrow
  off-grid variant or adjust timestamp so error is nonzero but inside scaled
  tolerance while `DELIVERED_GT_EVENTS` remains the winning reason);
- replace/expand quarter-cap part of
  `test_cadence_tolerance_cap_and_tiny_interval` with actual long no-TS spans:
  - first timestamp baseline;
  - 399 delivered no-TS callbacks;
  - current timestamp spanning 400 nominal 10 ms events plus exactly 2500 us;
  - require `CONTIG`, delivered positions 400, event count 400, error 2500,
    tolerance 2500, no omission/resync;
  - reset and repeat at +2501 us, require `DELTA_OFF_GRID` RESYNC with exact
    evidence and no synthesis;
- retain tiny-interval test and all existing tests.

Update misleading comments in this test block if they say computed
observations stay wholly zero for CONTIG/GAP. Contract is: observation struct is
cleared at call entry, computed fields are populated when cadence math runs,
reason remains NONE for non-RESYNC.

Touch only:

- this handoff;
- `src/audio_iso_seq.c`;
- `tests/unit/iso_seq/src/test_iso_seq.c`.

Run:

```bash
NIX_HARDENING_ENABLE="" west twister -T tests/unit/iso_seq \
  -p native_sim/native/64 --inline-logs
NIX_HARDENING_ENABLE="" west twister -T tests/unit/audio_stream_session \
  -p native_sim/native/64 --inline-logs
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Inspect status/diff/log. Commit without amend:

```text
test: complete ISO cadence diagnostic coverage
```

Require clean worktree, then run full `./scripts/test-all.sh`. Expected 65/65,
coverage population 36, build contract 95/95, BSim pins unchanged. Production
builds need not repeat because formula/classification/output behavior is
unchanged; full gate compiles the production source.

Do not run hardware, flash, push, merge, open PR, touch release/tag/VERSION, or
modify retained evidence. Return focused/full results, commit/status, and exact
hardware rerun entry point.
