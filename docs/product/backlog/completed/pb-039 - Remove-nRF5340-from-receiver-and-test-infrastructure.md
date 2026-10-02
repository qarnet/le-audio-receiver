---
id: PB-039
title: Remove nRF5340 from receiver and test infrastructure
status: Done
assignee: []
created_date: '2026-09-24 05:32'
updated_date: '2026-10-02 17:17'
labels:
  - 'size:L'
  - 'area:testing'
  - 'area:hil'
  - 'area:hardware'
dependencies: []
priority: p1
type: refactor
ordinal: 31000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
PB-032 made nRF54L15 the final receiver but retained nRF5340 receiver code, simulator targets, HIL source and HCI-UART fixtures. These retained dependencies contradict the approved all-nRF54L15 direction.

### Desired outcome
The XIAO nRF54L15 is the only active physical target. BabbleSim uses nrf54l15bsim, portable host unit tests remain host tests, and physical source/HCI-UART roles run sequentially on the second XIAO while the I2S-connected XIAO remains receiver. No active build, test or release workflow needs nRF5340.

### Scope
Replace simulator and physical source/controller targets; qualify the stock XIAO UART bridge; add fail-closed per-role device identity; remove obsolete receiver code, board definitions and helpers; migrate contracts, tests, coverage and current documentation without weakening surviving receiver behavior. This explicitly supersedes PB-032 retained-fixture requirements and old nRF5340-only central rules.

### Non-goals
Do not rewrite historical evidence or completed backlog items, publish firmware, modify SAMD11 firmware, add codecs/rates, change audio quality thresholds, or claim analog audibility. Host Python/native_sim tests are not nRF5340 dependencies. No full FLPR/I2S simulation claim.

### Technical context
Installed NCS v3.3.0 supports nrf54l15bsim/nrf54l15/cpuapp and ISO audio. Migration affects tests/bsim, scripts/bsim-env.sh, hil/source/app, scripts/hil, dongle, board/build helpers, src, and tests/test-matrix.json. Source clock reference is nrf/samples/bluetooth/iso_time_sync/src/controller_time_nrf54.c. The XIAO bridge has TX/RX but no RTS/CTS: bounded no-flow H4 validation must precede dependency removal. Existing 17-scenario/26-run BSim and receiver transport limits remain acceptance boundaries.

Cross-worktree inspection found the actual continuation branch `feature/nrf54l15-hil-fixture-session` at `6941c82`, with PB-033 physical proof complete and PB-034 implementation already in progress. PB-019 and PB-035 through PB-038 already describe the remaining migration. This approval record does not create competing implementations: resume that worktree and its existing items, preserve its uncommitted changes, and use PB-038 for final integration. PB-034 already authorizes measured target-native startup recipes while preserving payload hashes, PCM limits and lifecycle semantics.

### Open questions
No unresolved product choices. Stock bridge lossless throughput, available debug tooling and same-chip role identification require measured technical validation; failed qualification blocks migration rather than weakening acceptance.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Active build/test/release entry points use only physical nRF54L15, nrf54l15bsim or portable host targets; obsolete nRF5340 receiver paths are removed, with historical evidence preserved.
- [x] #2 All 17 BSim scenarios and 26 runs pass on nrf54l15bsim with unchanged TX hashes, PCM limits and lifecycle outcomes.
- [x] #3 Two XIAO devices are distinguished by verified runtime identity and explicit roles; ambiguous, duplicated or incorrect bindings fail before changing targets.
- [x] #4 The nRF54L15 standalone HIL source passes the existing mandatory transport/runtime matrix against the nRF54L15 receiver with retained image, identity and raw-log provenance.
- [x] #5 The nRF54L15 Linux HCI fixture autonomously streams mono, Mode A and Mode B and reconnects through the stock UART bridge without transport corruption or unclassified receiver failures.
- [x] #6 Receiver/source/controller builds, focused tests and canonical software gates pass; coverage changes account for retired code and retain surviving behavioral coverage; current docs describe only the migrated workflows.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
2026-10-01 authorized final continuation: finish existing PB-019/PB-034/PB-035/PB-036/PB-037 components, repair source peer-enqueue scheduling without changing 3000/2000 us or receiver limits, verify clean exact-commit canonical/build/HCI and fixed 20-child artifact matrix, then perform generated-output/reference audit and PB-038 aggregate closure. Physical analog qualification and draft-release publication/FR4 remain separate. Same owner; no competing implementation.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-24: Product owner explicitly approved full migration and successor refinement. Readiness grounded in existing PB-033 through PB-038 work discovered on the continuation branch. This record tracks approval, not a second implementation. Continue PB-034 first; host native_sim remains platform-neutral. Historical evidence and existing approved startup-recipe refinement remain intact.

User explicitly requests verified migration completion, silent execution, local commits and attached lab hardware actions. Original PB-013 user refinement remains unchanged and unstaged. No push, merge or release operation inferred. Clean b21c7a7 canonical gate 80/0/80, HIL host 340 passed/1 hardware-opt-in skip, physical builds/73-check contract and six HCI cases are retained; full fixed RH4 local-artifact matrix is running under separate system-manager containment. Status/criteria are not yet acceptance.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Approved all-nRF54L15 migration verified locally across receiver/source/HCI, simulation, artifacts, identity, software and physical gates. Component PB-019 and PB-034 through PB-037 now have evidence-backed criteria and Review summaries. PB-018 is explicitly superseded, never falsely qualified. PB-038 aggregate product status is not silently promoted; its execution evidence is recorded separately.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:FINAL_SUMMARY:END -->
