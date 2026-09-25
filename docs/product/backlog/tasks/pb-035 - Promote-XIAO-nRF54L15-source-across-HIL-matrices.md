---
id: PB-035
title: Promote XIAO nRF54L15 source across HIL matrices
status: In Progress
assignee: []
created_date: '2026-09-22 22:50'
updated_date: '2026-09-25 00:33'
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
- [ ] #1 Every active HIL matrix command accepts and forwards one external session manifest to all child runs, and each child preserves six ordered identity checks with fail-closed same-family binding.
- [ ] #2 Default active source fixture, build, flash, reset, UART capture, and matrix metadata use XIAO nRF54L15 without nRF5340 source board assumptions.
- [ ] #3 Runner and matrix tests cover manifest forwarding, duplicate or swapped same-family hardware, stale identity, missing hardware, and no unsafe target-changing action before validation.
- [ ] #4 Representative mono, Mode A, and Mode B matrix rows pass on physical nRF54L15 source and receiver hardware with immutable evidence and no ignored warnings.
- [ ] #5 Active HIL docs and STATUS.md describe XIAO nRF54L15 as canonical source while historical evidence remains unchanged.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Phase 1: propagate immutable session through matrix CLI/coordinator/Runner; validate all-nRF54 capture fixture sessions with host synthetic boundaries. Later: defaults, board-aware paths, artifacts, docs and physical matrix evidence.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-24 TX stack repair diagnostic: preserved first three 120-second 10 ms mono/Mode A/Mode B matrix rows passed; preserved Mode B start ACK then failed with source UART stack overflow. Private core points to bt_tx_processor stack at 900-byte Nordic default. Board-local CONFIG_BT_TX_PROCESSOR_STACK_SIZE=2048; build exit 0 on dirty tree, resolved .config 2048, HEX 51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59, ELF c43b9228a651d7b49e8805f315fbe1183cb9310a42e7ec2554c6a1ae52640e83. See docs/development/pb-035-source-matrix-results.md and private build log /tmp/opencode/pb035-source-txstack-build-20260924-r1.log. Physical verification pending: preserved_mode_b_48_4_1 after fresh pair, then full two-pass 20-row matrix. No AC claimed from config-only test.

2026-09-25: full fixed-image RH3 matrix /tmp/opencode/hil-runs/pb035-xiao-matrix-20260924-r2 passed 20/20, no failed/cancelled/cleanup children, including preserved Mode B, 7.5 ms, reconnect, hang/stall with unchanged limits. Later final runner smoke /tmp/opencode/hil-runs/pb035-final-runner-smoke-20260925-r1 passed under six guarded checks and restored source standalone image. Matrix imported earlier runner code while subsequent defaults/schema/docs landed; no clean-commit final integration or AC claim. See docs/development/pb-035-source-matrix-results.md and nrf54l15-only-continuation-20260925.md.
<!-- SECTION:NOTES:END -->
