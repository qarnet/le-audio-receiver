---
id: PB-003
title: Eliminate long-idle resume pop
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:audio'
dependencies: []
priority: p2
type: bug
ordinal: 3000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

An audible transient has been observed when playback resumes after a long idle
period. Root cause is not confirmed.

### Desired outcome

Resume after explicitly defined idle intervals without an audible transient.

### Scope

- Instrument stream-start and idle behavior.
- Determine whether cause is digital, electrical, or both.
- Validate tested receiver and DAC combinations with stated idle intervals.

### Non-goals

- Treat an energy-saving cause as confirmed before evidence identifies it.

### Technical context

Migrated from PLANNED_FEATURES.md item C and docs/known-limitations.md section
4.

### Open questions

Root cause; digital versus electrical classification; idle intervals to test.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 No audible transient occurs after each explicitly defined idle interval.
- [ ] #2 Evidence states tested idle intervals and receiver and DAC combinations.
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
