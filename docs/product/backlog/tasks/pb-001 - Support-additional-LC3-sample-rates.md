---
id: PB-001
title: Support additional LC3 sample rates
status: Backlog
assignee: []
created_date: '2026-09-13 01:24'
labels:
  - 'size:L'
  - 'area:audio'
dependencies: []
priority: p1
type: feature
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Receiver support is hardcoded to 48 kHz, so sources negotiating another LC3
sample rate are rejected.

### Desired outcome

Deliberately selected rates negotiate and stream. Unsupported rates continue to
receive disciplined codec-configuration rejection.

### Scope

- Land selected rates one at a time across capability advertisement, validation,
  decode, sink, and rate-conversion paths.
- Add fixtures, BSim coverage, and hardware acceptance for every landed rate.

### Non-goals

- Promise every candidate rate at once.

### Technical context

Migrated from PLANNED_FEATURES.md item A. Current constraints are documented in
docs/known-limitations.md section 1 and implemented in src/bt_bap.c,
src/audio_decode.c, and src/audio_i2s.c.

### Open questions

Selected rates and landing order; whether 44.1 kHz with a 7.5 ms non-integer
frame shape should be supported.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Each selected rate can negotiate and stream.
- [ ] #2 Unsupported rates retain codec-configuration rejection discipline.
- [ ] #3 Fixtures, BSim coverage, and hardware acceptance evidence exist for every landed rate.
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
