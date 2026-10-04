---
id: PB-043
title: Generate independent LC3 reference vectors and strict frame adapter
status: Blocked
assignee: []
created_date: '2026-10-03 02:33'
updated_date: '2026-10-04 05:00'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:audio'
dependencies:
  - PB-042
references:
  - docs/development/independent-firmware-validation-research-20261003.md
priority: p2
type: tech-debt
ordinal: 40000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Existing LC3 payload/PCM anchors originate from the same liblc3 lineage as the DUT. Official reference files carry headers and lengths and cannot be transmitted directly as raw ISO SDUs.

### Desired outcome
Reproducible independently encoded raw LC3 streams and reference-decoded PCM have sealed provenance and a strict validated frame/container adapter.

### Scope
Use approved S1 tooling and authored deterministic PCM. Initially cover current 48 kHz, 10 ms/120 bytes and 7.5 ms/90 bytes per channel with continuous state and distinct L/R. Preserve original PCM, reference container, extracted frames, decoded PCM, state/reset history, alignment/delay/padding and generation identities.

### Non-goals
Do not generate expected output with the DUT, silently rebaseline old fixtures, widen frame budgets/sample rates, or publish tool/audio/derived artifacts without the S1 rights decision. LC3-only: no LC3plus implementation, source, vectors or licensing assumptions. Preserve existing regression fixtures, frozen HIL/PCM limits and immutable evidence. No public release, Bluetooth qualification or unrelated PB-013 feature claim.

### Technical context
tests/fixtures/lc3/gen_fixtures.c, portable-oracle-manifest.json and stateful-reference-manifest.json own existing regression provenance. External LC3_Reference_Binary/Readme.txt describes the private test container; tests/bsim/client/src/bsim_tx.c consumes raw frame bytes.
Research: docs/development/independent-firmware-validation-research-20261003.md

### Open questions
Selected reference container/version; approved storage/redistribution and retention; generation interface; authored signal recipe; exact delay/padding and manifest schema. Decide during refinement.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Approved independent tools generate both initial duration/geometry streams with continuous state; raw payloads exclude container/header/length bytes and maintain exact channel/frame order.
- [ ] #2 Repeated generation with identical authored input, seed, configuration and pinned tools produces the same sealed artifacts, or documents and resolves a reference repeatability failure before acceptance.
- [ ] #3 Adapter rejects invalid headers, geometry, inconsistent frame lengths, truncation, unsupported configuration and provenance/content mismatches without emitting accepted partial output.
- [ ] #4 Manifest binds input recipe/hash, tool and normative versions, codec shape, frame/sample counts, state history, alignment/delay/padding and payload/PCM hashes; existing same-library corpus remains retained.
- [ ] #5 DUT output cannot overwrite expected reference data, and artifact use/storage complies with the approved rights contract.
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-04 scope recovery: PB-043 waits for PB-042 approved independent LC3 tooling and rights before vectors or adapter can be accepted. No reference execution, vectors or skip waiver claimed.
<!-- SECTION:NOTES:END -->
