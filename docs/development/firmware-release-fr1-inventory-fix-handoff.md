# FR1 review-fix handoff: refresh active gate inventory

Date: 2026-08-09

## Goal

Correct active test-inventory prose left stale when FR1 added the
`package_firmware_release` Python gate child. Preserve dated historical
evidence. This is documentation-only FR1 review cleanup; do not begin FR2.

## Grounding

Executable discovery now reports 35 Twister, 5 exec-only, and 20 Python
children. With coverage, matrix, and BSim, the canonical gate has 63 children.
Clean canonical evidence exists at correction commit `1671a9f`: 63 PASS / 0
FAIL / 63 TOTAL. Coverage is 4674/5130 lines, 2030/2824 branches, and 358/358
functions; build contract remains 95/95.

Current contradictions:

- `scripts/test-all.sh:21-23` says 19 Python, 59 unit, 62 total.
- `docs/testing/coverage-matrix.md` current inventory table and current-gate
  note say 19 Python and 62 total and omit `package_firmware_release`.
- `docs/design.md:23-29` calls P1-P8's 62-child result the current authoritative
  state, despite `STATUS.md` and FR1 evidence now superseding that summary.

Historical result documents that accurately report 62-child runs at their own
commits must not change.

## Exact changes

1. `scripts/test-all.sh`
   - Change current inventory comment to 35 Twister + 5 exec-only + 20 Python =
     60 unit children.
   - Change canonical total to 63 children (60 + coverage + matrix + BSim).

2. `docs/testing/coverage-matrix.md`
   - In "Current suite inventory", change Python count from 19 to 20 and add
     `package_firmware_release (test_package_firmware_release.py)` to the suite
     list.
   - Change total gate children from 62 to 63.
   - Update the note that explicitly labels the current accepted gate to 63
     children: 35 Twister + 5 exec-only + 20 Python + coverage + matrix + BSim,
     clean-tree 63/63 at `1671a9f`, linking
     `docs/development/firmware-release-fr1-results.md`.
   - Preserve all dated T7/T8 and earlier 62-child evidence where it describes
     the exact historical run rather than current inventory.

3. `docs/design.md`
   - Keep its historical architecture status and all R0-R10/P1-P8 history.
   - Replace only the top "current authoritative state" summary with FR1
     closeout facts: 63/63 composition, coverage population/counts above,
     build contract 95/95, BSim 17 scenarios / 26 runs, and pointers to
     `STATUS.md` plus
     `docs/development/firmware-release-fr1-results.md`.

Do not edit `AGENTS.md`, `STATUS.md`, FR1 results/plan, historical results,
source behavior, test discovery, or coverage baseline.

## Verification and commit

Run:

```bash
python3 scripts/test_inventory.py --python
python3 scripts/check-test-matrix.py
python3 scripts/test_package_firmware_release.py
git diff --check
```

Inspect `git status`, `git diff`, and `git log --oneline -10`. Stage only:

- `docs/development/firmware-release-fr1-inventory-fix-handoff.md`
- `scripts/test-all.sh`
- `docs/testing/coverage-matrix.md`
- `docs/design.md`

Commit:

```text
docs: refresh current test inventory
```

No full canonical rerun is required for this documentation/comment-only
correction because executable behavior is unchanged and clean 63/63 evidence
already exists at `1671a9f`. Finish with a clean worktree.

Do not push, merge, open a PR, tag, release, amend, or add attribution.

## Escalation and return

Stop and report if executable discovery differs from 35/5/20, matrix check
fails, or a requested edit would rewrite valid historical evidence. Return
changed files, verification results, commit hash/message, final status,
deviations, and blockers.
