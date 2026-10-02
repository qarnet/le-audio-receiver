---
id: PB-040
title: Upgrade nRF Connect SDK to v3.4.1
status: Done
assignee: []
created_date: '2026-09-25 16:28'
updated_date: '2026-10-02 17:17'
labels:
  - 'size:M'
  - 'area:build'
dependencies: []
priority: p2
type: tech-debt
ordinal: 37000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Repo dev shell, CI, artifact metadata, LC3 calibration, and HCI UART compatibility are pinned to NCS v3.3.0. Current software workflows need a coordinated v3.4.1 upgrade without losing evidence or changing product behavior.

### Desired outcome

Active development builds, software tests, and CI use exact NCS v3.4.1 and matching toolchain revisions; artifacts report the actual SDK and codec provenance. Firmware and software gates pass with existing audio behavior and acceptance limits.

### Scope

- Install v3.4.1 side by side and update repository flake/Nix and CI exact SDK/toolchain pins.
- Make required v3.4.1 API and Kconfig compatibility fixes; rebuild receiver, standalone source, and HCI images without ignored warnings.
- Update artifact metadata and version checks; validate LC3 codec separately from existing fixture provenance and preserve codec bytes and strict thresholds.
- Re-audit generated HCI UART compatibility patch against v3.4.1, without patching SDK files on disk.
- Update current developer/public guidance and run software gates.

### Non-goals

- RTT development or changes to user-owned nix-nrf-dev repository.
- PB-013 360-frame FLPR feature work or relaxed test thresholds.
- Rewriting historical evidence, fixture provenance, or published release assets.
- Claiming physical HCI/hardware qualification or publishing a release as part of this software-only upgrade.

### Technical context

Parent-verified target revisions: nrf b20f8619ba9a5530f8c34b0a130d829947cfe55d; zephyr 33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6; liblc3 unchanged at 48bbd3...; toolchain 8285d8ad56; official container digest sha256:45b97cad97a9967c52d77d1d1a0f7dd8fe027edd17c05c3eda2eeadc23729418. Existing fixture provenance remains distinct from v3.4.1 codec validation. Technical compile/regression investigation is implementation work, not a new product decision.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Dev shell and CI use exact NCS v3.4.1 and matching toolchain pins; version guards and artifact provenance report those versions.
- [x] #2 Receiver CPUAPP/FLPR, standalone XIAO source, and HCI UART images build without ignored compiler, Kconfig assigned-value, or build warnings.
- [x] #3 Native units, coverage, strict BSim, calibration, and packaging/version tests pass with retained codec bytes, existing fixture provenance, and unchanged audio acceptance limits.
- [x] #4 HCI UART generated compatibility patch is re-audited against v3.4.1 and guarded by tests; no SDK-on-disk patch is introduced.
- [x] #5 Current SDK guidance reflects v3.4.1; v3.3.0 historical evidence and published assets remain unmodified; software gate results make no physical HCI qualification or release-publication claim.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Preserve pre-existing dirty work, PB-013 changes, and immutable evidence. Install/verify NCS v3.4.1 beside v3.3.0 and verify nrf/zephyr/liblc3/toolchain/digest against target pins before edits; do not modify nix-nrf-dev or SDK on disk.
2. Update flake.nix (and flake.lock only if repository-owned pin requires it), .github/workflows/firmware-build.yml, repository scripts that guard SDK/toolchain versions, associated tests, and packaging/version metadata. Require exact revisions, not just directory presence.
3. Re-audit HCI UART generated compatibility patch in dongle/hci_uart/CMakeLists.txt and its tests against new SDK. Adjust only repo-owned patch/guards when needed; keep on-disk SDK untouched. Apply narrowly grounded API/Kconfig repairs in receiver, hil/source, dongle/hci_uart and their board configs/tests.
4. Validate LC3 codec via scripts/lc3_pcm_calibrate.py, tests/fixtures/lc3/calibrate.c, tests/unit/lc3_pcm_calibrate/test_lc3_pcm_calibrate.py and related CMake/generators/tests; keep existing fixture hashes/provenance distinct and strict bytes and metrics unchanged.
5. Update current README.md, docs/user-guide.md, docs/flashing.md, docs/development guidance and STATUS.md only where active SDK commands or metadata change. Retain historical v3.3.0 evidence and released assets.
6. Verify three builds with fw-build-54l15, fw-build-hil-source-54l15, fw-build-dongle; native unit tests, report-only coverage then clean enforcement with baseline unchanged unless justified by new source lines, strict scripts/bsim-stage1-run.sh, strict LC3 regeneration, calibration and package/version tests. Record exact commands and output; no flash, physical HCI acceptance, release publishing, commits or PR in this setup.

2026-09-25 integration follow-up: preserve dirty-tree gate logs in docs/development/ncs-3.4.1-upgrade-results.md; after intended changes are committed, rerun clean canonical software gate and baseline enforcement before checking acceptance criteria. Keep HCI physical qualification, RTT, PB-013 and release operations outside this software-only upgrade.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-25 setup only: product explicitly requested immediate v3.4.1 work. Definition of Ready met: explicit pinned version and observable software behavior, bounded components/gates, non-goals and unchanged thresholds; no remaining product decision. Parent verified SDK installed and matching revisions; verify again during implementation. Existing concurrent migration items PB-019/034/035/036/037 In Progress by explicit parallel work; PB-013 dirty edits preserved. No implementation or hardware operation performed in this setup.

2026-09-25 dirty-tree integration diagnostics: pinned NCS v3.4.1/toolchain; three pristine physical builds and 69 resolved contract assertions pass without compiler/Kconfig warnings; native 76 pass, HIL Python 340 pass/one intentional hardware skip, strict LC3 40 pass with original fixture bytes and host replay, strict BSim 17 scenarios/26 runs with scoped audited five-hash dependency -Werror exceptions, report-only coverage 46 suites with unchanged 36-file baseline (4971/5427 lines, 2203/3008 branches, 377/377 functions), ARM calibration build-only 296 pass. Native_sim CMake product-support notice remains raw and host-only. HCI source-hash-guarded UART compatibility proof is original 3/8192 vs generated 0/8192; physical HCI failure remains open. No hosted CI, clean canonical acceptance, flash, release, or commit. See docs/development/ncs-3.4.1-upgrade-results.md for logs and boundaries.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Software upgrade verified locally on committed daf7cd9404e32bacbff4b6431dafccbd28e4a8eb: clean canonical gate 80 PASS / 0 FAIL / 80 TOTAL (41 Twister, five exec-only, 31 Python, baseline coverage, matrix, strict BSim 17/26). Clean coverage manifest dirty=false; all 36 baseline pairs IDENTICAL. HIL Python 340 pass, one intentional hardware-opt-in skip. LC3 host replay uses NCS v3.4.1 and original v3.3.0 fixture corpus; 296 ARM calibration tests build-only. Three pristine physical-target builds pass; resolved build contract 69/69, zero compiler/Kconfig warnings. Exact Zephyr assertion-enabled CMake configuration notice retained with fault guards on; native_sim host-only product-support notice and five source/hash-specific upstream BabbleSim dependency compiler exceptions separately recorded, not globally waived. Workflow contract tests 36 pass; no hosted CI execution. HCI UART generated patch source-hash guarded, exhaustive original 3/8192 vs generated 0/8192; physical qualification remains open. Existing fixture bytes, PCM limits, coverage baseline, v3.3.0 historical evidence, VERSION 0.1.0 and draft assets unchanged. No hardware flash, RTT or 360-frame feature work, RH4/FR4 acceptance or publication. See docs/development/ncs-3.4.1-upgrade-results.md and external /tmp/opencode/pb040-clean-canonical-daf7cd9-r1.log plus coverage/run-manifest.json, clean HIL/LC3/build logs. Implementation ready for human PR review, not Done or accepted.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.

PB-040 remains a software-upgrade completion: exact SDK/toolchain and clean daf7cd9 80/0/80 evidence are retained in its result report. Later 104e67a integration passed canonical 80/0/80, build contract 73/73 and physical builds. Separate PB-019/PB-039 work owns later hardware qualification. Hosted CI is pending this PR, not a historical software-upgrade acceptance claim.
<!-- SECTION:FINAL_SUMMARY:END -->
