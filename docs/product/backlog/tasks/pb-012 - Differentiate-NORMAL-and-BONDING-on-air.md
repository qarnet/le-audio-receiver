---
id: PB-012
title: Differentiate NORMAL and BONDING on air
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:pairing'
dependencies:
  - PB-004
priority: p2
type: research
ordinal: 12000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

NORMAL and BONDING are not yet distinguishable on air, allowing bonded central
auto-connect behavior that can occupy sole connection slot.

### Desired outcome

Standards-compatible marker or identity decision differentiates modes while
preserving bonds and privacy.

### Scope

- Evaluate marker or identity options after duplicate-advertisement invariant.
- Define custom-central behavior, old-central fallback, and BlueZ policy limits.
- Retain bond and privacy behavior.

### Non-goals

- Simultaneous advertisements as default.
- Reconnect loop workaround.

### Technical context

Migrated from PLANNED_FEATURES.md Advertisement differentiation section and
docs/development/user-pairing-control-plan.md.

### Open questions

Standards-compatible marker or identity, old-central fallback, and available
BlueZ policy hook.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Standards-compatible marker or identity decision is recorded.
- [ ] #2 Custom central auto-connects only NORMAL and requires explicit action for BONDING.
- [ ] #3 Bonds and privacy are retained.
- [ ] #4 Old-central fallback and BlueZ policy limits are documented.
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
