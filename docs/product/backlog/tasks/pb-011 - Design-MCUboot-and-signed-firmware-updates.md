---
id: PB-011
title: Design MCUboot and signed firmware updates
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:security'
dependencies: []
priority: p2
type: research
ordinal: 11000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Factory-flash releases do not define signed in-field update behavior, including
custom FLPR companion-image handling.

### Desired outcome

ADR and target prototype establish safe signed firmware-update design.

### Scope

- Resolve companion images, key custody, rollback and downgrade, interruption
  and power loss, settings and bonds, and transport in an ADR.
- Prove signature rejection, recovery, and post-update audio on target.

### Non-goals

- Block factory release work.

### Technical context

Migrated from PLANNED_FEATURES.md item I and
docs/development/firmware-release-plan.md Future DFU track. Stock nRF54
MCUboot excludes custom FLPR image updates.

### Open questions

Update transport and companion-image design after ADR investigation.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 ADR resolves companion images, key custody, rollback and downgrade, interruption and power loss, settings and bonds, and transport.
- [ ] #2 Target prototype proves signature rejection.
- [ ] #3 Target prototype proves recovery and post-update audio.
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
