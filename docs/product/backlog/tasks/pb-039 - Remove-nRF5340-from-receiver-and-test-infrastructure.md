---
id: PB-039
title: Remove nRF5340 from receiver and test infrastructure
status: Ready
assignee: []
created_date: '2026-09-24 05:32'
updated_date: '2026-09-24 11:34'
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
- [ ] #1 Active build/test/release entry points use only physical nRF54L15, nrf54l15bsim or portable host targets; obsolete nRF5340 receiver paths are removed, with historical evidence preserved.
- [ ] #2 All 17 BSim scenarios and 26 runs pass on nrf54l15bsim with unchanged TX hashes, PCM limits and lifecycle outcomes.
- [ ] #3 Two XIAO devices are distinguished by verified runtime identity and explicit roles; ambiguous, duplicated or incorrect bindings fail before changing targets.
- [ ] #4 The nRF54L15 standalone HIL source passes the existing mandatory transport/runtime matrix against the nRF54L15 receiver with retained image, identity and raw-log provenance.
- [ ] #5 The nRF54L15 Linux HCI fixture autonomously streams mono, Mode A and Mode B and reconnects through the stock UART bridge without transport corruption or unclassified receiver failures.
- [ ] #6 Receiver/source/controller builds, focused tests and canonical software gates pass; coverage changes account for retired code and retain surviving behavioral coverage; current docs describe only the migrated workflows.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-24: Product owner explicitly approved full migration and successor refinement. Readiness grounded in existing PB-033 through PB-038 work discovered on the continuation branch. This record tracks approval, not a second implementation. Continue PB-034 first; host native_sim remains platform-neutral. Historical evidence and existing approved startup-recipe refinement remain intact.
<!-- SECTION:NOTES:END -->
