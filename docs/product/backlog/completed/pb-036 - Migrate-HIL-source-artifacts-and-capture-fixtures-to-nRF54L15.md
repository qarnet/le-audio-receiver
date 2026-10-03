---
id: PB-036
title: Migrate HIL source artifacts and capture fixtures to nRF54L15
status: Done
assignee: []
created_date: '2026-09-22 22:50'
updated_date: '2026-10-02 17:17'
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
- [x] #1 RH4 source archives contain exact XIAO nRF54L15 CPUAPP artifact, checksum, board identity, NCS version, source commit, and provenance with no nRF5340 CPUNET or merged-image fields.
- [x] #2 RH4 extraction and preflight reject missing, extra, modified, wrong-board, wrong-version, and provenance-mismatched source artifacts before flashing.
- [x] #3 Mono and stereo capture fixtures, helper tests, and retained evidence use XIAO nRF54L15 source with unchanged audio recipes and acceptance limits.
- [x] #4 Exact source artifact is flashed and revalidated in at least one physical RH4-compatible row, with immutable logs and no ignored warnings.
- [x] #5 Artifact schema, capture documentation, release-facing guidance, and build contracts contain no active nRF5340 source assumptions.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Integrate PB-035/PB-036 source archive slice: migrate deterministic source packager and strict RH4 resolver to pinned NCS v3.3.0 nrf54l15dk single CPUAPP image; prove valid archive and fail-closed negatives by public subprocess and resolver/matrix host tests. Coordinate later runner/flash helper, capture fixtures, docs, and clean-commit physical artifact acceptance after current matrix.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Shared RH4 integration needs coordinated PB-035/PB-036 changes. PB-035 predecessor matrix session propagation implemented/tested; full PB-035 acceptance awaits physical matrix. This approved full-migration dependency overlap changes no product criteria. Current slice does not build/flash firmware or modify active images; dirty state and prior evidence preserved.

2026-09-25: later runner/default/capture changes and host tests are present, but fixed-image RH3 matrix does not prove exact-source-archive physical RH4. No clean-commit source provenance or RH4 artifact flash acceptance; dirty-tree coverage command stopped before builds. All criteria remain unchecked. See docs/development/nrf54l15-only-continuation-20260925.md.

2026-09-28 exact-source-artifact checkpoint: clean 25a5cbf canonical software gate 80 PASS / 0 FAIL / 80 TOTAL and build contract 69/69; source archive SHA-256 cba5ee53d2e452e226c579e076af6b7604f1cbeb0d2a6daebb44cf066687fecc was consumed by the first six passing rows of the local exact-artifact matrix. Fresh Mode B 7.5 ms row 7 failed receiver teardown with 13 later rows skipped; the retained receiver postmortem and a separate receiver-only private TX-notify workqueue local-build replay passed (valid 16860/lost 14/PLC 28, zero decode errors, I2S underruns, resets, runtime warnings) with unchanged source HEX 805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c. This local-build diagnostic is not exact-artifact qualification. New clean full RH4 matrix and FR4 draft-artifact acceptance remain pending; no criteria or status changed. See docs/development/nrf54l15-tx-notify-workqueue-results-20260928.md.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Exact source archive SHA-256 1d0004d0c6fb73815e9dbc5d495b85377cb2bd57f94c23d5e34899b7787bc6b0 contains only CPUAPP and v3.4.1/exact-commit/board/checksum provenance; public resolver negatives reject missing/extra/modified/wrong-board/version/provenance inputs. All 20 physical rows consume/revalidate that archive. Mono/stereo migrated fixture snapshots and real model/runner/analyzer host tests retained at /tmp/opencode/nrf54-capture-contract-evidence-20261001-r1/. This proves capture infrastructure migration, not physical ADC/ALSA or analog qualification.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:FINAL_SUMMARY:END -->
