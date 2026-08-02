# T7 evidence-fix handoff

Status: **task complete (2026-08-02)** — T7 final acceptance granted with
exact canonical gate evidence: **41 PASS / 0 FAIL / 41 TOTAL** observed on
the exact T7 code commit `4a31324` (see `STATUS.md` T7 section and
`docs/development/workstation-transfer-status.md`).

## Goal

Correct two review findings without changing coverage code, tests, baseline, or
thresholds. T7 remains open until evidence is exact.

## Required work

1. Correct baseline provenance everywhere:
   `tests/coverage-baseline.json` records `generated_commit = c6adce8...`.
   Documentation must say candidate baseline was generated from clean commit
   `c6adce8`, committed in `4a31324`, then default enforcement reran on clean
   `4a31324` with identical ratios. Do not claim baseline was generated on a
   commit that already contained itself.  **(Done — provenance corrected in
   `STATUS.md`, `docs/testing/coverage-matrix.md`,
   `docs/testing/behavior-contract.md` CV-001; CV-001 already stated the
   correct provenance.)**
2. Record exact canonical gate evidence. Current gate has 25 Twister + 4
   exec-only + 9 Python + coverage + matrix + BSim = **41 children**. Evidence
   must state the observed exact result and runtime/log source, not infer pass
   from script structure.  **(Done — observed exact
   `Gate complete: 41 PASS / 0 FAIL / 41 TOTAL`, exit 0, `real 14m51,504s`
   on `4a31324`, 2026-08-02, `thomas-workstation`; recorded in `STATUS.md`.)**
3. Search retained workstation logs/artifacts from prior T7 run first. If no
   retained log proves exact 41/41, rerun `./scripts/test-all.sh` on a detached
   workstation worktree of exact `4a31324`, capture complete log and elapsed
   runtime, and verify `41 PASS / 0 FAIL / 41 TOTAL`. Do not alter workstation
   main. Remove temporary worktree/ref/bundle after extracting evidence.
   **(Done — retained `/tmp/t7-canonical-gate.log` from a prior run on the
   same exact commit already showed `41 PASS / 0 FAIL / 41 TOTAL`,
   `GATE_EXIT=0`, `real 13m52,920s`; it was independently re-verified by a
   fresh run on a detached clone at exact `4a31324`
   (`real 14m51,504s`).  Note: the gate cannot run from a `git worktree`
   because `scripts/test-coverage.sh` requires a real `.git` directory
   (`[ -d .git ]`), so the detached checkout is a fresh clone.  Temporary
   worktrees/clones removed after evidence extraction.)**
4. Run `git diff --check`, inspect status/log, and commit only handoff plus
   corrected evidence docs. No production/tooling change.  **(Done by the
   evidence-fix commit.)**

## Files

- `STATUS.md`
- `docs/testing/coverage-matrix.md`
- `docs/testing/behavior-contract.md` only where provenance is stated
- this handoff

No baseline regeneration or lowering. No push/merge/PR/amend/hardware.

## Escalation

If exact full-gate run fails, stop after two materially different diagnostic
attempts and report failing child/log. Never rewrite evidence as accepted.
**(Not triggered — first fresh run passed 41/41; one environment correction
was required: detached clone instead of `git worktree`, because
`test-coverage.sh` requires a real `.git` directory.  The failed worktree
attempt is a known tooling constraint, not a test failure.)**
