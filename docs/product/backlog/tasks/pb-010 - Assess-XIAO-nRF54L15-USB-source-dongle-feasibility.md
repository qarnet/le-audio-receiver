---
id: PB-010
title: Assess XIAO nRF54L15 USB source-dongle feasibility
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:interoperability'
dependencies: []
priority: p3
type: research
ordinal: 10000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

XIAO nRF54L15 USB source-dongle concept remains unproven and existing native-HCI
interpretation is not an active candidate.

### Desired outcome

Evidence-backed architecture decision resolves feasibility of a source-dongle
route.

### Scope

- Compare UAC and HCI-like transport, SAMD11 limits, Linux integration, and
  required source role.
- If feasible, build prototype that appears in PipeWire and streams without
  manual setup; otherwise record blockers.

### Non-goals

- Claim native-HCI adapter candidacy for existing XIAO interpretation.

### Technical context

Migrated from PLANNED_FEATURES.md item G and
docs/bluetooth-adapter-evaluation.md Seeed XIAO nRF54L15 hardware section.

### Open questions

Whether broader custom SAMD11 and UAC route is worth pursuing.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Architecture decision covers UAC versus HCI-like transport, SAMD11 limits, Linux integration, and source role.
- [ ] #2 If feasible, prototype appears in PipeWire and streams without manual setup.
- [ ] #3 If infeasible, evidence-backed blockers close the item.
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
