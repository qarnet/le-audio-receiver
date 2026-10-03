---
id: PB-038
title: Complete nRF54L15 migration and retire active nRF5340 dependencies
status: Done
assignee: []
created_date: '2026-09-22 22:51'
updated_date: '2026-10-02 17:17'
labels:
  - 'size:L'
  - 'area:platform'
dependencies:
  - PB-019
  - PB-034
  - PB-035
  - PB-036
  - PB-037
priority: p1
type: feature
ordinal: 36000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

nRF54L15 is sole production receiver target, but canonical simulation, HIL source, Linux HCI adapter, legacy receiver paths, and active documentation still depend on nRF5340 hardware or board targets.

### Desired outcome

Complete repository-wide migration so all active product, build, test, HIL, release, and support workflows use nRF54L15 components, with nRF5340 retained only in immutable historical evidence or external compatibility context.

### Scope

- Close nRF54L15BSim migration, XIAO HIL source promotion, HIL artifact and capture migration, nRF54L15 HCI UART replacement, and E83 receiver removal.
- Run exhaustive active-reference and generated-output audit.
- Update product backlog, public docs, development guidance, build contracts, CI, and STATUS.md to one consistent platform model.
- Archive or supersede obsolete nRF5340 product items only after replacement evidence is accepted.
- Prove canonical software and applicable physical hardware gates after all component changes are integrated.

### Non-goals

- Rewrite immutable historical evidence or erase factual nRF5340 history.
- Remove nRF5340 support from upstream Zephyr or NCS.
- Publish firmware, merge pull requests, or claim release acceptance beyond existing FR4 process.
- Implement XIAO USB source-dongle research from PB-010.

### Technical context

Component work is tracked by PB-019 and PB-034 through PB-037. This item owns final integration, exhaustive classification of retained references, aggregate gates, and product-state closure.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 PB-019 and PB-034 through PB-037 satisfy all acceptance criteria with retained evidence, and obsolete PB-018 is archived or marked superseded with rationale.
- [x] #2 Fresh repository audit finds no active nRF5340 board target, hardware role, build or flash helper, sysbuild image, simulator target, release artifact, fixture default, CI job, or support claim.
- [x] #3 Every retained nRF5340 mention is demonstrably immutable historical evidence, migration history, external SDK context, or an explicit statement that nRF5340 is unsupported.
- [x] #4 Canonical unit, coverage, build-contract, nRF54L15 firmware, nRF54L15BSim Stage 1, matrix, and applicable physical HIL gates pass from a clean exact commit with no ignored warnings.
- [x] #5 README.md, public docs, STATUS.md, AGENTS.md, product backlog, release guidance, and developer commands consistently describe nRF54L15-only active infrastructure.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-25: still Backlog. PB-019 production-image late Mode A HCI failure unresolved (not a technical hard blocker); RH3 fixed-image matrix passed but exact RH4 source artifact, analog/FR4, clean canonical integration and exhaustive reference audit remain open. Coverage enforcement exited 1 before builds because dirty-tree guard requires a clean exact commit; exact error retained in docs/development/nrf54l15-only-continuation-20260925.md. Explicit LOCAL commit authority required for clean exact-commit gate/provenance; no push/PR/release authority inferred. Preserve dirty tree and baseline.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Aggregate technical criteria satisfied under explicit PB-039 full-migration authority. Component criteria have retained evidence; PB-018 supersession rationale is preserved. Backlog status remains product-owned pending lifecycle/PR bundling; this record does not start a competing implementation or claim accepted Done. Final audit and current guidance distinguish active nRF54L15 paths from immutable history, unsupported-input regressions and external SDK context.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:FINAL_SUMMARY:END -->
