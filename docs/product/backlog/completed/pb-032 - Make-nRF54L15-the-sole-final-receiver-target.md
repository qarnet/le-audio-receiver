---
id: PB-032
title: Make nRF54L15 the sole final receiver target
status: Done
assignee: []
created_date: '2026-09-20 20:02'
updated_date: '2026-09-21 17:05'
labels:
  - 'size:M'
  - 'area:documentation'
  - 'area:release'
dependencies: []
priority: p1
type: docs
ordinal: 31000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Public documentation and test-contract terminology still present the nRF5340 Ebyte receiver as a peer supported or production target, even though current CI, packaging, HIL acceptance, and release assets are nRF54L15-only. This split product message keeps an unwanted nRF5340 parity obligation alive and makes users choose a receiver path the project no longer intends to ship.

### Desired outcome

The Seeed XIAO nRF54L15 is the project's sole supported final receiver target. The nRF5340 Ebyte receiver remains only as a legacy engineering/regression path during this phase, with no public-release, product-parity, or future-feature obligation.

### Scope

- Make README and public user, wiring, technology, limitation, and flashing documentation lead exclusively with the nRF54L15 receiver.
- Reclassify nRF5340 Ebyte receiver material as legacy developer/regression documentation, not a final product choice or release candidate.
- Remove the standing requirement that every change keep the nRF5340 receiver build working; retain it as best-effort regression evidence until later code removal.
- Make test-matrix and behavior-contract terminology distinguish the final nRF54L15 receiver from the legacy nRF5340 receiver build and nRF5340-based test fixtures.
- Preserve and verify the existing nRF54L15-only CI, package, draft-release, and exact-artifact boundary.
- Drop PB-005 because nRF5340 physical pairing-control parity is no longer product work.

### Non-goals

- Delete nRF5340 receiver source, Ebyte board definitions, build or flash helpers, or regression tests.
- Replace nRF5340BSim.
- Replace the nRF5340DK HIL source fixture or HCI-UART dongle; later work may migrate those roles to additional nRF54L15 boards.
- Rewrite historical plans, results, release evidence, or accepted baselines.
- Change runtime audio behavior or publish firmware.

### Technical context

Current final receiver pipeline is already nRF54L15-only in `.github/workflows/firmware-build.yml`, `scripts/package-firmware-release.py`, `scripts/prepare-draft-release.py`, PB-006, PB-007, PB-008, and PB-009. Conflicting public claims remain in `README.md`, `docs/user-guide.md`, `docs/hardware-wiring.md`, `docs/known-limitations.md`, `docs/technology/nrf5340.md`, `docs/technology/nrf54l15.md`, `docs/flashing.md`, and `release/flashing/nrf5340-e83.md`. `scripts/check-test-matrix.py`, `tests/test-matrix.json`, `docs/testing/behavior-contract.md`, `flake.nix`, and `AGENTS.md` still use dual-production terminology or policy.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Public product documentation identifies the XIAO nRF54L15 as the sole supported final receiver and no longer asks users to choose, build, flash, or expect release assets for the nRF5340 Ebyte receiver.
- [x] #2 nRF5340 Ebyte receiver documentation and test evidence are clearly labeled legacy engineering/regression material with no release, product-parity, or future-feature obligation; PB-005 is archived with that rationale.
- [x] #3 Test-matrix and behavior-contract terminology distinguishes the final nRF54L15 receiver from the legacy nRF5340 receiver build and from nRF5340BSim, HIL-source, and HCI-UART fixture roles without losing valid evidence resolution.
- [x] #4 Automated CI, package, provenance, and draft-release tests prove final receiver artifacts remain nRF54L15-only and reject nRF5340 receiver release artifacts.
- [x] #5 nRF5340 receiver source, board/build/flash helpers, nRF5340BSim, HIL source, and HCI-UART dongle remain present in this phase, and focused tests plus the canonical software gate pass without new warnings.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Reframe public target documentation so XIAO nRF54L15 is the only supported final receiver; retain nRF5340 Ebyte details only under explicit legacy engineering/regression labels. Verify public links and the no-em-dash rule.
2. Update current policy and build/test terminology in AGENTS.md, STATUS.md, flake.nix, docs/testing/behavior-contract.md, scripts/check-test-matrix.py, its focused tests, and tests/test-matrix.json. Keep all accepted build command names resolvable while distinguishing final receiver, legacy receiver regression, and fixture roles.
3. Preserve current nRF54L15-only CI/package/release implementation and prove it with existing public-boundary Python tests. Preserve nRF5340BSim, HIL source, HCI-UART dongle, Ebyte board/build/flash code, and historical evidence.
4. Run focused matrix and release tests, link/stale-wording checks, backlog doctor, git diff checks, and the canonical software gate. Record evidence, check acceptance criteria, and move PB-032 to Review because no commit/PR was requested.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-21 implementation evidence:

- Applied target-role policy: Seeed XIAO nRF54L15 is sole supported final receiver. Physical E83 nRF5340 receiver remains best-effort legacy engineering/regression with no release, product-parity, physical-control, or future-feature obligation. nRF5340BSim, nRF5340DK HIL source, and HCI-UART dongle remain required fixtures; migration is deferred.
- No runtime firmware, board, build/flash helper, CI workflow, release package/provenance, HIL, dongle, or release-asset change.
- Canonical validation mirrored the complete implementation patch before final task metadata updates into a temporary standalone clone under `/tmp/opencode`. Throwaway local commit `dbcd2c9deb7aa5a97151523e9e21b0b5ed2183be` ran `nix develop -c ./scripts/test-all.sh`: `Gate complete: 74 PASS / 0 FAIL / 74 TOTAL`. Clone was deleted and never pushed. The primary worktree was uncommitted during this validation; later PR-gate metadata updates did not change tested code or behavior.
- Focused primary checks passed: matrix unit test (43), matrix checker (0 errors, 0 notes), firmware CI (35), firmware package (19), draft release (47), HIL source package (4), and RH4 artifact (11). `backlog doctor` and `git diff --check` passed. User-doc U+2014, dual-choice phrase, stale command-name, archive/completed link, and retained-fixture path checks passed.
- Earlier linked-worktree validation could not run coverage because its `.git` is a file while `test-coverage.sh` requires a directory. Product owner selected standalone clone validation. No coverage or gate behavior changed.
- PR-gate state: implementation was committed on branch `feature/pb-032-nrf54l15-final-target` and submitted as PR #14. Human product-owner merge remains official acceptance.
<!-- SECTION:NOTES:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-20 20:02
---
Refined from product-owner direction: XIAO nRF54L15 becomes sole final receiver target now; nRF5340 Ebyte receiver remains legacy regression only, while nRF5340BSim, HIL source, and HCI-UART fixture migration stay explicitly deferred.
---
<!-- COMMENTS:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Public product documentation now presents Seeed XIAO nRF54L15 as sole supported final receiver. Ebyte nRF5340 receiver material is legacy engineering/regression reference only, with no release, product-parity, physical-control, or future-feature obligation.

Test-matrix terminology now separates final receiver, legacy receiver regression, and fixture build roles while retaining all recognized build commands. PB-005 is archived with product-owner rationale. Existing nRF54L15-only CI, package, provenance, draft-release, RH4, and FR4 boundaries remain unchanged.

Validation passed on throwaway local clone commit `dbcd2c9deb7aa5a97151523e9e21b0b5ed2183be`: `nix develop -c ./scripts/test-all.sh` reported `74 PASS / 0 FAIL / 74 TOTAL`. Focused matrix, release-boundary, artifact, backlog, diff, public-doc, and retained-path checks also passed. Work is committed on `feature/pb-032-nrf54l15-final-target` and submitted as PR #14; human product-owner merge remains official acceptance. Deferred: migrate retained nRF5340BSim, nRF5340DK HIL source, and HCI-UART fixture roles in separate work.
<!-- SECTION:FINAL_SUMMARY:END -->
