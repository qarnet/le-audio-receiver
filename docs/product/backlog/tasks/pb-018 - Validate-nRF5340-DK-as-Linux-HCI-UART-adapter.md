---
id: PB-018
title: Validate nRF5340 DK as Linux HCI UART adapter
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:interoperability'
dependencies: []
priority: p3
type: research
ordinal: 18000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

nRF5340 DK HCI UART source-controller route exists but lacks full dynamic
adapter acceptance evidence.

### Desired outcome

Record evidence-backed verdict for nRF5340 DK as Linux HCI UART adapter.

### Scope

- Retain exact H4 setup and identity evidence.
- Run full dynamic adapter checklist or record blockers.

### Non-goals

- Claim USB HCI ISO support.
- Remove or clean up nRF5340 receiver path.

### Technical context

See docs/bluetooth-adapter-evaluation.md Nordic nRF5340 DK HCI UART section,
docs/supported-sources.md hardware matrix row, and dongle/.

### Open questions

Dynamic acceptance verdict and any recorded blockers.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Exact H4 setup and identity evidence are retained.
- [ ] #2 Full dynamic adapter checklist passes or blockers are recorded.
- [ ] #3 No USB HCI ISO claim or receiver cleanup or removal is introduced.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
<!-- SECTION:FINAL_SUMMARY:END -->
