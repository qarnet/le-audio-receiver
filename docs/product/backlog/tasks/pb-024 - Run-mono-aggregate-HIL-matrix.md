---
id: PB-024
title: Run mono aggregate HIL matrix
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:hil'
dependencies:
  - PB-023
priority: p2
type: feature
ordinal: 24000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Transport/runtime HIL acceptance does not prove mono analog aggregate capture.

### Desired outcome

Run fixed matrix twice through qualified mono capture and record precise mono
output smoke verdict.

### Scope

- Run full fixed matrix twice with mono capture.
- Check expected carriers by mode, transport gates, and frozen mono oracle.
- Retain evidence.

### Non-goals

- Channel-order or stereo claim.

### Technical context

See docs/development/system-hil-milestones.md MA1.

### Open questions

None beyond qualified fixture outputs.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Full fixed matrix runs twice with mono capture.
- [ ] #2 Expected carriers by mode, transport gates, and frozen mono oracle pass.
- [ ] #3 Evidence is retained.
- [ ] #4 Verdict is exactly MONO_OUTPUT_SMOKE_ACCEPTED.
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
