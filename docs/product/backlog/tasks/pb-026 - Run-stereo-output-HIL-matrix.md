---
id: PB-026
title: Run stereo output HIL matrix
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:hil'
dependencies:
  - PB-025
priority: p3
type: feature
ordinal: 26000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Receiver HIL has no accepted stereo analog output matrix.

### Desired outcome

Run full matrix twice with qualified stereo fixture and retain distinct-channel
evidence.

### Scope

- Run full matrix twice.
- Verify correct mapping and signatures, transport gates, and frozen stereo
  oracle.
- Retain raw captures.

### Non-goals

- Claim calibrated fidelity.

### Technical context

See docs/development/system-hil-milestones.md SA1.

### Open questions

None beyond qualified fixture outputs.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Full matrix runs twice.
- [ ] #2 Correct mapping and signatures, all transport gates, and frozen stereo oracle pass.
- [ ] #3 Raw captures are retained.
- [ ] #4 Verdict is exactly STEREO_OUTPUT_ACCEPTED.
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
