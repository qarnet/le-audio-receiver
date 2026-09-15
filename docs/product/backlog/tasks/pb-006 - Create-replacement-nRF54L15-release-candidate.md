---
id: PB-006
title: Create replacement nRF54L15 release candidate
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:S'
  - 'area:release'
dependencies: []
priority: p1
type: feature
ordinal: 6000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Failed v0.1.0 exact-artifact acceptance cannot be reused or mutated, and no
replacement version or candidate is selected.

### Desired outcome

Product owner selects a new version and trusted-main flow creates an immutable
nRF54L15 draft candidate.

### Scope

- Select semantic version through product ownership.
- Run canonical gate and trusted-main release flow.
- Produce exact nRF54L15 ZIP and checksum as private, untagged draft assets.

### Non-goals

- Reuse or mutate failed v0.1.0.
- Create a manual tag.

### Technical context

Migrated from PLANNED_FEATURES.md item F. See docs/development/firmware-release-plan.md
opening, Current release-line scope update, and Release lifecycle; STATUS.md;
and root VERSION.

### Open questions

Replacement version.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Selected semantic version is recorded in VERSION.
- [ ] #2 Canonical gate passes before candidate creation.
- [ ] #3 Exact trusted-main target and nRF54L15 ZIP with checksum are created.
- [ ] #4 Draft remains private and untagged.
- [ ] #5 Version or release collision fails closed.
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
