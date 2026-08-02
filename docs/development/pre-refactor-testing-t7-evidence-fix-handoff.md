# T7 evidence-fix handoff

Status: **task pending** — T7 final acceptance not yet granted; expected
exact canonical gate is **41/41** (see step 2), not 40/40.

## Goal

Correct two review findings without changing coverage code, tests, baseline, or
thresholds. T7 remains open until evidence is exact.

## Required work

1. Correct baseline provenance everywhere:
   `tests/coverage-baseline.json` records `generated_commit = c6adce8...`.
   Documentation must say candidate baseline was generated from clean commit
   `c6adce8`, committed in `4a31324`, then default enforcement reran on clean
   `4a31324` with identical ratios. Do not claim baseline was generated on a
   commit that already contained itself.
2. Record exact canonical gate evidence. Current gate has 25 Twister + 4
   exec-only + 9 Python + coverage + matrix + BSim = **41 children**. Evidence
   must state the observed exact result and runtime/log source, not infer pass
   from script structure.
3. Search retained workstation logs/artifacts from prior T7 run first. If no
   retained log proves exact 41/41, rerun `./scripts/test-all.sh` on a detached
   workstation worktree of exact `4a31324`, capture complete log and elapsed
   runtime, and verify `41 PASS / 0 FAIL / 41 TOTAL`. Do not alter workstation
   main. Remove temporary worktree/ref/bundle after extracting evidence.
4. Run `git diff --check`, inspect status/log, and commit only handoff plus
   corrected evidence docs. No production/tooling change.

## Files

- `STATUS.md`
- `docs/testing/coverage-matrix.md`
- `docs/testing/behavior-contract.md` only where provenance is stated
- this handoff

No baseline regeneration or lowering. No push/merge/PR/amend/hardware.

## Escalation

If exact full-gate run fails, stop after two materially different diagnostic
attempts and report failing child/log. Never rewrite evidence as accepted.
