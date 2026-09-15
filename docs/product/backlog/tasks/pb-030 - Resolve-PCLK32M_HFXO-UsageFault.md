---
id: PB-030
title: Resolve PCLK32M_HFXO UsageFault
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:M'
  - 'area:clock-recovery'
dependencies: []
priority: p2
type: bug
ordinal: 30000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

PCLK32M_HFXO path has UsageFault evidence; root cause remains unresolved.

### Desired outcome

Identify cause through reproducible hardware evidence and use supported
clock-source behavior or document explicit permanent restriction.

### Scope

- Reproduce fault on hardware.
- Identify cause and fix supported path or document permanent restriction.
- Verify selected path has no warnings or faults.

### Non-goals

- Make unsupported clock-source claim.

### Technical context

See STATUS.md PCLK32M clock-source evidence table, docs/design.md Appendix B
Established: standalone I2S hardware, and docs/development/phase4-acceptance-results.md.

### Open questions

Root cause.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Reproducible hardware evidence identifies cause.
- [ ] #2 Supported clock-source behavior is fixed or explicit permanent restriction is documented.
- [ ] #3 Chosen path has no warnings or faults.
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
