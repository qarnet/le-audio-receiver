---
id: PB-028
title: Evaluate transmitter interoperability
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:interoperability'
dependencies:
  - PB-027
priority: p3
type: feature
ordinal: 28000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Receiver acceptance alone does not establish transmitter interoperability.

### Desired outcome

Evaluate defined transmitter contract with independent standards peer and retain
separate verdict.

### Scope

- Add transmitter identity and protocol without changing accepted receiver
  oracle.
- Run defined matrix, reconnect, repeats, and negative negotiation.
- Include at least one independent standards peer.

### Non-goals

- Claim broad interoperability from same-repository success alone.

### Technical context

See docs/development/system-hil-milestones.md Milestone 2.

### Open questions

Selected transmitter implementations and independent standards peer.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Transmitter identity and protocol are added without changing accepted receiver oracle.
- [ ] #2 Defined matrix, reconnect, repeats, and negative negotiation run.
- [ ] #3 At least one independent standards peer is included.
- [ ] #4 Separate verdict is recorded and same-repository success alone makes no broad claim.
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
