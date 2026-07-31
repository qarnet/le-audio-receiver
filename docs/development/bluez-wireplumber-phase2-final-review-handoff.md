# Phase 2 final review handoff

## Goal

Clean worktree and restore deterministic repeated-run BSim proof.

## In scope

- Modify `scripts/bsim-stage1-run.sh` to run both 10 ms and 7.5 ms scenarios
  twice, extract receiver hashes, require pairwise equality, and require known
  accepted values (`0xFE0D4245` and `0x5853F445`) unless evidence shows a
  legitimate deterministic update that must be reviewed.
- Keep per-run unique logs and print all artifact paths/hashes.
- Update Phase 2 results/STATUS with repeated-run evidence.
- Add all untracked Phase 2 handoff docs, including this document, to commit.
- Run BSim stage gate, focused Python tests, and `git diff --check`.
- Inspect status; worktree must be clean after new commit.

## Constraints

No firmware behavior/hardware/host changes, amend/rewrite, push/PR.
