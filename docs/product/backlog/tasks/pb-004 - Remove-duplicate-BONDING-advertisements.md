---
id: PB-004
title: Remove duplicate BONDING advertisements
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:pairing'
dependencies: []
priority: p1
type: bug
ordinal: 4000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Two scanner entries or advertisements have been observed during BONDING, and
only one pairs. Root cause is unknown.

### Desired outcome

NORMAL and BONDING each expose exactly one connectable receiver advertisement.

### Scope

- Capture scanner, address, identity, and advertising-set evidence.
- Identify and remove duplicate source or artifact.
- Verify fresh-central pairing and stale-entry behavior.

### Non-goals

- Use two advertisements as a shortcut.

### Technical context

Migrated from PLANNED_FEATURES.md item D and docs/known-limitations.md section
5. This is distinct from future NORMAL and BONDING payload differentiation.

### Open questions

Root cause.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Exactly one connectable advertisement is visible in NORMAL and BONDING.
- [ ] #2 A fresh central pairs successfully from the visible entry.
- [ ] #3 A stale entry cannot steal the connection.
- [ ] #4 Scanner, address, and advertising-set evidence is retained.
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
