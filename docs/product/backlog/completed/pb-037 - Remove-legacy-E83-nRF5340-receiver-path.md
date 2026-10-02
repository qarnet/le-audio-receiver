---
id: PB-037
title: Remove legacy E83 nRF5340 receiver path
status: Done
assignee: []
created_date: '2026-09-22 22:50'
updated_date: '2026-10-02 17:17'
labels:
  - 'size:M'
  - 'area:hardware'
dependencies: []
priority: p1
type: tech-debt
ordinal: 35000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Repository still carries Ebyte E83 nRF5340 receiver board definitions, net-core sysbuild, build and flash helpers, release guidance, and compatibility tests despite nRF54L15 being sole production receiver target.

### Desired outcome

Remove legacy E83 receiver implementation and operational paths so production receiver build, flash, packaging, and support surface are nRF54L15-only.

### Scope

- Delete E83 board definition and nRF5340-only receiver Kconfig, devicetree, sysbuild, flash, and helper paths.
- Remove nRF5340 production actuator selection and code that has no remaining simulation or test owner.
- Update build contracts, packaging, flashing docs, public support tables, and active developer guidance.
- Retain shared protocol tests and historical evidence where they remain useful and clearly historical.

### Non-goals

- Remove nRF5340 references from immutable historical results.
- Delete generic Zephyr or NCS support outside repository.
- Change nRF54L15 receiver behavior, transport limits, or release assets.
- Remove nRF54L15BSim or HIL infrastructure.

### Technical context

PB-032 established nRF54L15 as sole final receiver target. Current project policy retains E83 only pending a separate cleanup decision; this item is that decision and execution.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 E83 board tree, nRF5340 receiver board config, net-core sysbuild overlays, build and flash helpers, and nRF5340 release-flashing path are removed.
- [x] #2 Production CMake, Kconfig, devicetree, packaging, and public docs expose only nRF54L15 receiver support; obsolete nRF5340-only actuator code is removed or retained only under explicit test-local historical ownership.
- [x] #3 Build-contract and documentation tests reject reintroduction of active E83 or nRF5340 production receiver paths.
- [x] #4 nRF54L15 firmware builds, unit suites, coverage, canonical BSim, and applicable HIL smoke remain green with no ignored warnings.
- [x] #5 Historical evidence remains intact and every retained nRF5340 mention is classified as historical, compatibility context, or external SDK reference.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Remove active E83 receiver board, net-core sysbuild, production-only APLL and identity choices, and receiver build/flash helpers. Preserve APLL arithmetic/rail and no-HFCLK regression under test-local historical ownership, plus generic identity and concealment tests.
2. In later slice, update build-contract checks, packaging, release flashing and public/developer docs; classify active versus historical references without changing immutable evidence.
3. Validate nRF54L15 firmware build and focused native actuator/I2S tests now; later run contract/documentation tests, unit suite, coverage on clean commit, canonical BSim and applicable HIL smoke. Check warnings and preserve existing transport limits and receiver behavior.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-25: diagnostic receiver/source/HCI builds, 75/0/75 unit, 69/0 actual build contract, strict BSim 17/26 and fixed-image RH3 20/20 passed. Coverage report-only population36 (4971/5427 lines, 2203/3008 branches, 377/377 functions), baseline unchanged. Clean enforcement failed before builds on dirty-tree precondition. Exhaustive reference/comment classification and clean exact-commit gates remain; no criterion or Done claim. See docs/development/nrf54l15-only-continuation-20260925.md.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
E83 board/netcore/receiver helper and production APLL paths are deleted. Only explicit portable test-local actuator history remains. Build/package/doc regressions reject active legacy selections; public support is nRF54L15-only. Per-file reference ledger classifies retained history, negative tests and SDK context; 12 generated outputs have no enabled legacy selection.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:FINAL_SUMMARY:END -->
