---
id: PB-020
title: Evaluate self-contained USB LE Audio transmitters
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:interoperability'
dependencies: []
priority: p3
type: research
ordinal: 20000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Secondary self-contained USB transmitter candidates are unverified with this
receiver, and vendor claims do not establish BAP interoperability.

### Desired outcome

Evaluate exact transmitter configurations and receiver BAP 48 kHz behavior.

### Scope

- Evaluate FMA120, Creative BT-W6, Avantree C82 LEA, nRF5340 Audio DK, and
  SUNITEC BT400T.
- Retain exact identity and configuration for each tested candidate.
- Repeat pairing and reconnect evidence for strongest feasible candidate.

### Non-goals

- Treat Auracast-only support as compatible.

### Technical context

See docs/supported-sources.md Self-contained USB transmitters secondary
candidates.

### Open questions

Evaluation order and required purchases.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Every tested candidate has exact identity, configuration, and receiver BAP 48 kHz result.
- [ ] #2 Vendor claims remain distinct from project evidence.
- [ ] #3 Strongest feasible candidate has repeat pairing and reconnect evidence.
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
