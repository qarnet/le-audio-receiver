---
id: PB-013
title: Decide 360-frame FLPR offload policy
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:flpr'
dependencies: []
priority: p2
type: research
ordinal: 13000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

7.5 ms 360-frame streams use cpuapp ASRC because current FLPR payload contract
is 480 frames. Audio remains functional.

### Desired outcome

Either implement 360-frame 7.5 ms FLPR support or record cpuapp-only policy as
permanent.

### Scope

- Ground decision in ring, protocol, memory, and runtime evidence.
- Align tests and public documentation with chosen policy.
- Preserve audio-functional 7.5 ms streaming.

### Non-goals

- Label current cpuapp fallback as failure.

### Technical context

See docs/development/refactor-plan.md Deferred feature list, STATUS.md known
behavior, and docs/known-limitations.md section 2.

### Open questions

Whether evidence supports FLPR contract expansion or permanent cpuapp-only
policy.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Decision is grounded in ring, protocol, memory, and runtime evidence.
- [ ] #2 Tests and public documentation match chosen policy.
- [ ] #3 7.5 ms remains audio-functional.
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
