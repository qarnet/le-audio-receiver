---
id: PB-038
title: Complete nRF54L15 migration and retire active nRF5340 dependencies
status: Backlog
assignee: []
created_date: '2026-09-22 22:51'
updated_date: '2026-09-25 00:34'
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
- [ ] #1 PB-019 and PB-034 through PB-037 satisfy all acceptance criteria with retained evidence, and obsolete PB-018 is archived or marked superseded with rationale.
- [ ] #2 Fresh repository audit finds no active nRF5340 board target, hardware role, build or flash helper, sysbuild image, simulator target, release artifact, fixture default, CI job, or support claim.
- [ ] #3 Every retained nRF5340 mention is demonstrably immutable historical evidence, migration history, external SDK context, or an explicit statement that nRF5340 is unsupported.
- [ ] #4 Canonical unit, coverage, build-contract, nRF54L15 firmware, nRF54L15BSim Stage 1, matrix, and applicable physical HIL gates pass from a clean exact commit with no ignored warnings.
- [ ] #5 README.md, public docs, STATUS.md, AGENTS.md, product backlog, release guidance, and developer commands consistently describe nRF54L15-only active infrastructure.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-25: still Backlog. PB-019 production-image late Mode A HCI failure unresolved (not a technical hard blocker); RH3 fixed-image matrix passed but exact RH4 source artifact, analog/FR4, clean canonical integration and exhaustive reference audit remain open. Coverage enforcement exited 1 before builds because dirty-tree guard requires a clean exact commit; exact error retained in docs/development/nrf54l15-only-continuation-20260925.md. Explicit LOCAL commit authority required for clean exact-commit gate/provenance; no push/PR/release authority inferred. Preserve dirty tree and baseline.
<!-- SECTION:NOTES:END -->
