---
id: PB-035
title: Promote XIAO nRF54L15 source across HIL matrices
status: Done
assignee: []
created_date: '2026-09-22 22:50'
updated_date: '2026-10-02 17:17'
labels:
  - 'size:M'
  - 'area:hil'
dependencies: []
priority: p1
type: feature
ordinal: 33000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Single-row HIL supports session-bound XIAO nRF54L15 source selection, but matrix commands and default fixtures still select nRF5340DK source paths and do not propagate session manifests.

### Desired outcome

Make XIAO nRF54L15 canonical source for every active HIL matrix and runner entry point while preserving immutable session identity enforcement.

### Scope

- Add matrix CLI support for external session manifests.
- Forward one validated manifest through every child run without mutable role remapping.
- Change active default fixtures and board-aware build, flash, reset, and capture paths to XIAO nRF54L15 source.
- Cover RH3, RH4, MA1, SA1, and direct runner flows.
- Preserve fail-closed same-family identity checks and immutable run directories.

### Non-goals

- Change audio recipes, transport limits, receiver firmware behavior, or accepted metric thresholds.
- Modify immutable prior HIL evidence.
- Replace Linux HCI UART adapter ownership.

### Technical context

PB-033 proved XIAO nRF54L15 source for mono, Mode A, and Mode B 10 ms rows. `scripts/hil/cli.py` and `scripts/hil/matrix.py` still omit session-manifest propagation, while active fixture defaults still name nRF5340 source boards.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Every active HIL matrix command accepts and forwards one external session manifest to all child runs, and each child preserves six ordered identity checks with fail-closed same-family binding.
- [x] #2 Default active source fixture, build, flash, reset, UART capture, and matrix metadata use XIAO nRF54L15 without nRF5340 source board assumptions.
- [x] #3 Runner and matrix tests cover manifest forwarding, duplicate or swapped same-family hardware, stale identity, missing hardware, and no unsafe target-changing action before validation.
- [x] #4 Representative mono, Mode A, and Mode B matrix rows pass on physical nRF54L15 source and receiver hardware with immutable evidence and no ignored warnings.
- [x] #5 Active HIL docs and STATUS.md describe XIAO nRF54L15 as canonical source while historical evidence remains unchanged.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Phase 1: propagate immutable session through matrix CLI/coordinator/Runner; validate all-nRF54 capture fixture sessions with host synthetic boundaries. Later: defaults, board-aware paths, artifacts, docs and physical matrix evidence.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-24 TX stack repair diagnostic: preserved first three 120-second 10 ms mono/Mode A/Mode B matrix rows passed; preserved Mode B start ACK then failed with source UART stack overflow. Private core points to bt_tx_processor stack at 900-byte Nordic default. Board-local CONFIG_BT_TX_PROCESSOR_STACK_SIZE=2048; build exit 0 on dirty tree, resolved .config 2048, HEX 51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59, ELF c43b9228a651d7b49e8805f315fbe1183cb9310a42e7ec2554c6a1ae52640e83. See docs/development/pb-035-source-matrix-results.md and private build log /tmp/opencode/pb035-source-txstack-build-20260924-r1.log. Physical verification pending: preserved_mode_b_48_4_1 after fresh pair, then full two-pass 20-row matrix. No AC claimed from config-only test.

2026-09-25: full fixed-image RH3 matrix /tmp/opencode/hil-runs/pb035-xiao-matrix-20260924-r2 passed 20/20, no failed/cancelled/cleanup children, including preserved Mode B, 7.5 ms, reconnect, hang/stall with unchanged limits. Later final runner smoke /tmp/opencode/hil-runs/pb035-final-runner-smoke-20260925-r1 passed under six guarded checks and restored source standalone image. Matrix imported earlier runner code while subsequent defaults/schema/docs landed; no clean-commit final integration or AC claim. See docs/development/pb-035-source-matrix-results.md and nrf54l15-only-continuation-20260925.md.

2026-09-28 clean-commit checkpoint: 25a5cbf canonical software gate 80 PASS / 0 FAIL / 80 TOTAL and build contract 69/69; exact XIAO source archive SHA-256 cba5ee53d2e452e226c579e076af6b7604f1cbeb0d2a6daebb44cf066687fecc. Full local-artifact matrix passed six rows, then stopped on fresh Mode B 7.5 ms row 7 with receiver timeout and no summary; 13 rows skipped. Retained no-reset postmortem and receiver-only private TX-notify workqueue diagnostic show one unchanged-source (HEX 805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c) frozen-row replay PASS, receiver valid 16860/lost 14/PLC 28 with zero decode errors, I2S underruns, resets, and runtime warnings. This red/green row does not complete the clean full matrix; no acceptance checkbox or status change. See docs/development/nrf54l15-tx-notify-workqueue-results-20260928.md.

2026-09-30 continuation: recovered clean 2a0e792 software gate 80/0/80 and six-case HCI pass; retained exact-artifact matrix failed Mode A 48_4_1 on source under-lead (-ETIME), with one passed/one failed/18 skipped. New unchanged-image frozen Mode A row /tmp/opencode/hil-runs/nrf54-analyzer-resume-20260930-r2 passed 12000 scored and 12644 submitted per CIS, zero send failures/skips/under-lead, receiver valid 12645/12644 and global PLC 25, zero decode/I2S/reset/push faults. Two passive 100 ms I2S windows and four rejected offline negative controls retained separately. Startup silence is valid geometry, not content acceptance. No timing repair, full matrix or AC/status claim. See docs/development/logic-analyzer-continuation-results-20260930.md.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
All active matrix/runner contracts forward paired external sessions and use XIAO source CPUAPP. Public host negatives cover swapped/duplicate/stale/missing identity and pre-action rejection. Full physical matrix verifies 120 ordered checks, identical session snapshots, representative mono/Mode A/Mode B plus reconnect/hang/stall and 7.5-ms rows. Source enqueue guard preserves 3000/2000-us margins and frozen limits.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks are recorded in docs/development/nrf54l15-migration-pr-wrap-up-20261002.md. Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.
<!-- SECTION:FINAL_SUMMARY:END -->
