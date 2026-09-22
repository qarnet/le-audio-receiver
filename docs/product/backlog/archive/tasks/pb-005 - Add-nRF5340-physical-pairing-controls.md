---
id: PB-005
title: Add nRF5340 physical pairing controls
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
updated_date: '2026-09-20 20:02'
labels:
  - 'size:M'
  - 'area:pairing'
dependencies: []
priority: p2
type: feature
ordinal: 5000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

nRF5340 build lacks physical user controls for pairing modes; reset currently
uses developer shell workflow.

### Desired outcome

nRF5340 physical controls match established pairing-mode contract.

### Scope

- Add reviewed physical input and LED integration for nRF5340.
- Implement 3 s BONDING and 8 s RESET behavior.
- Add feature-parity tests and nRF5340 hardware evidence.

### Non-goals

- Make this a production-release obligation.
- Remove nRF5340 receiver support.

### Technical context

Migrated from PLANNED_FEATURES.md item E and docs/known-limitations.md section
6. Existing pairing contract is in docs/development/user-pairing-control-plan.md.

### Open questions

Pin selection after hardware review.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A 3 s hold enters BONDING and an 8 s hold enters RESET.
- [ ] #2 LED and mode behavior match pairing-mode contract.
- [ ] #3 Feature-parity tests and nRF5340 hardware evidence are retained.
<!-- AC:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Dropped by product-owner direction on 2026-09-20. The XIAO nRF54L15 is now the sole supported final receiver target, so nRF5340 physical pairing-control parity is no longer product work. Existing nRF5340 Ebyte receiver code and developer/regression build remain temporarily; this item does not authorize their deletion. PB-032 records the target-policy transition, and future work may separately remove the legacy receiver path or migrate nRF5340-based fixtures.
<!-- SECTION:FINAL_SUMMARY:END -->
