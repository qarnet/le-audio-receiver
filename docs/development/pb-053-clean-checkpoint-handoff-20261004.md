# PB-053: clean preparation checkpoint and PR verification

## Goal and explicit authorization

Owner requests implementation, verification, push to existing PR16 and continued
selected backlog work, allowing complicated/unavailable items to remain Blocked.
Delegator reviewed completed preparation utility and real compiler/process tests.
This handoff authorizes scoped commits and push to existing
`feature/independent-firmware-validation`, never merge, release or force-push.

PB-053 remains Blocked. Commit only completed preparation and truthful blocker
records. Failed guest lifecycle and its unaccepted runtime scaffolding do not ship.

## Exact allowed files

- `scripts/bluez_host_prepare.py`
- `tests/unit/bluez_host_prepare/test_bluez_host_prepare.py`
- `docs/development/pb-053-preparation-handoff-20261004.md`
- `docs/development/pb-053-preparation-hardening-handoff-20261004.md`
- `docs/development/pb-053-preparation-results-20261004.md`
- Nine existing task files PB-041,042,043,044,047,048,049,050,053 under
  `docs/product/backlog/tasks/` (use their exact existing filenames).

Do NOT stage AGENTS.md, opencode.json, PB-013, PB-051 or its scaffolding,
private graph data, any `bluez_guest_*` / `bluez_host_guest.py`,
`tests/unit/bluez_host_guest/`, other guest handoffs, or raw external evidence.
Preserve all those dirty bytes. No broad git add.

## Verified prerequisites and constraints

Current branch/PR16 HEAD before this checkpoint:
`ad7fd8eaeb162ebf52a6cc3d7269de9d04fbe571`, PR open/draft, head branch correct.
Active ZEPHYR_BASE is `/home/thomas-workstation/ncs/v3.4.1/zephyr`, revision
`33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`. west, gcovr8.4 and backlog exist.
`scripts/test-all.sh` owns complete gate; use clean detached candidate to avoid
explicitly paused/red PB-051 scaffolding. Frozen coverage and canonical BSim17/26
remain unchanged. Last accepted gate84/0/84; new preparation Python child should
add one discovered suite, not guest scaffold or skipped case.

## Steps

1. Reinspect git status, diff and log. Review intended new files with their
   contents. Verify five focused preparation tests, `git diff --check`, no em
   dashes in changed task prose, and compiler/vendor evidence in results doc.
   Stage ONLY allowed files, inspect staged diff. Commit normal project style:
   `PB-053: harden local emulator preparation and record scope blockers`.
   No attribution/footer. Item remains Blocked, no criteria checked or Done.
2. Verify `/tmp/opencode` exists, then create clean detached worktree at
   `/tmp/opencode/pb053-preparation-candidate-r1` for exact committed HEAD;
   unused suffix if occupied. Do not reuse/clobber old evidence/worktrees.
3. In that worktree run complete local gate, retaining raw full output at
   `/tmp/opencode/pb053-preparation-canonical-r1.log` and coverage evidence under
   exclusive `/tmp/opencode/pb053-preparation-canonical-r1/`:

   ```sh
   TEST_OUTPUT_DIR=/tmp/opencode/pb053-preparation-canonical-r1 bash scripts/test-all.sh
   ```

   Use external redirect for raw log, do not truncate/filter logs. Current shell
   already has correct SDK; no installers/downloads/old SDK override. Need no
   production build change in this preparation-only checkpoint. Retain warnings
   and fail unexplained warnings. Never run primary dirty-tree canonical gate or
   PB-051 tests. Never skip a failing gate, change baseline or frozen acceptance.
4. If full gate fails, return exact logs and failure to Delegator for grounded
   repair. Do not guess or commit known failing repairs. If passes, push normal
   branch to existing origin PR16. Confirm hosted CI via gh, latest head unit,
   coverage, BSim, tests and firmware success, release skipped. Do not open new
   PR or merge. Hosted ordinary failures return logs for Delegator analysis.
5. Update results doc and PB-053 notes with exact clean commit/gate totals,
   unchanged baseline, CI run/head/results and preparation-only proof boundary.
   Commit only those two files with normal `PB-053:` evidence message, push and
   verify final latest-head CI again. Keep status Blocked and all other held
   items Blocked. Evidence metadata is not full host-lane acceptance.
6. Return final hashes/commit messages, gate/log paths and SHA256, hosted run URL,
   actual final checks, preserved dirty status, selected status inventory and
   unresolved blockers. No background tasks left promising future work.

## Escalation

Missing prerequisite, warning, changed behavior, two different failed repair
attempts, scope change or missing decision: stop, preserve files/evidence and
report precise question. No new architecture, guessed hashes, unapproved tools,
weakened checks, mass staging, amendment, force-push, release or merge.
