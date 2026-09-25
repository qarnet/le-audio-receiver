---
id: PB-036
title: Migrate HIL source artifacts and capture fixtures to nRF54L15
status: In Progress
assignee: []
created_date: '2026-09-22 22:50'
updated_date: '2026-09-25 00:33'
labels:
  - 'size:M'
  - 'area:hil'
dependencies:
  - PB-035
priority: p1
type: feature
ordinal: 34000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

RH4 source artifact packaging, capture fixtures, and release-facing HIL contracts still assume nRF5340 multi-image sysbuild outputs after runtime source selection moved to XIAO nRF54L15.

### Desired outcome

Produce, validate, and consume exact XIAO nRF54L15 source artifacts in RH4 and analog capture workflows with board-aware single-image contracts.

### Scope

- Replace nRF5340 source artifact schema and archive layout with exact nRF54L15 source image identity.
- Update RH4 inventory, extraction, flash, checksum, provenance, and mismatch rejection paths.
- Migrate mono and stereo capture fixtures and their helper contracts to XIAO nRF54L15 source.
- Preserve exact-artifact and immutable evidence requirements.

### Non-goals

- Change candidate receiver release assets or publish a release.
- Relax RH4 exact-artifact acceptance.
- Change analog metric thresholds or audio recipes.
- Rewrite historical archives.

### Technical context

Current RH4 tests and fixtures encode nRF5340 source multi-image assumptions. XIAO source builds one nRF54L15 CPUAPP image and PB-033 already records its SHA-256 in session evidence.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 RH4 source archives contain exact XIAO nRF54L15 CPUAPP artifact, checksum, board identity, NCS version, source commit, and provenance with no nRF5340 CPUNET or merged-image fields.
- [ ] #2 RH4 extraction and preflight reject missing, extra, modified, wrong-board, wrong-version, and provenance-mismatched source artifacts before flashing.
- [ ] #3 Mono and stereo capture fixtures, helper tests, and retained evidence use XIAO nRF54L15 source with unchanged audio recipes and acceptance limits.
- [ ] #4 Exact source artifact is flashed and revalidated in at least one physical RH4-compatible row, with immutable logs and no ignored warnings.
- [ ] #5 Artifact schema, capture documentation, release-facing guidance, and build contracts contain no active nRF5340 source assumptions.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Integrate PB-035/PB-036 source archive slice: migrate deterministic source packager and strict RH4 resolver to pinned NCS v3.3.0 nrf54l15dk single CPUAPP image; prove valid archive and fail-closed negatives by public subprocess and resolver/matrix host tests. Coordinate later runner/flash helper, capture fixtures, docs, and clean-commit physical artifact acceptance after current matrix.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Shared RH4 integration needs coordinated PB-035/PB-036 changes. PB-035 predecessor matrix session propagation implemented/tested; full PB-035 acceptance awaits physical matrix. This approved full-migration dependency overlap changes no product criteria. Current slice does not build/flash firmware or modify active images; dirty state and prior evidence preserved.

2026-09-25: later runner/default/capture changes and host tests are present, but fixed-image RH3 matrix does not prove exact-source-archive physical RH4. No clean-commit source provenance or RH4 artifact flash acceptance; dirty-tree coverage command stopped before builds. All criteria remain unchecked. See docs/development/nrf54l15-only-continuation-20260925.md.
<!-- SECTION:NOTES:END -->
