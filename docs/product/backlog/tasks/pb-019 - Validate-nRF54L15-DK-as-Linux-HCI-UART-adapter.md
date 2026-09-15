---
id: PB-019
title: Validate nRF54L15 DK as Linux HCI UART adapter
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:interoperability'
dependencies: []
priority: p3
type: research
ordinal: 19000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

nRF54L15 DK HCI UART candidate lacks dynamic Linux and receiver validation.

### Desired outcome

Record evidence-backed verdict for nRF54L15 DK as Linux HCI UART adapter.

### Scope

- Use NCS hci_uart path cited by current adapter documentation.
- Retain exact H4 setup and identity evidence.
- Run full dynamic adapter checklist or record blockers.

### Non-goals

- Claim support before hardware run.

### Technical context

See docs/bluetooth-adapter-evaluation.md Nordic nRF54L15 DK HCI UART section,
docs/supported-sources.md hardware matrix row, and NCS v3.3.0
samples/bluetooth/hci_uart path cited there.

### Open questions

Dynamic acceptance verdict and any recorded blockers.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Exact H4 setup and identity evidence are retained.
- [ ] #2 Full dynamic adapter checklist passes or blockers are recorded.
- [ ] #3 No support claim is made before hardware run.
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
