---
id: PB-051
title: ASCS protocol rejection and lifecycle regression matrix
status: Backlog
assignee: []
created_date: '2026-10-03 04:40'
labels:
  - 'size:M'
  - 'area:testing'
  - 'area:bluetooth'
dependencies: []
references:
  - docs/development/bluetooth-testing-repository-research-20261003.md
priority: p2
type: tech-debt
ordinal: 48000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
The repository survey found useful BlueZ ASCS negative/lifecycle wire cases which our codec/transport gates do not exhaustively cover through production public protocol boundaries.
### Desired outcome
Independently authored encoded ASCS transactions verify rejection, ASE state preservation, cleanup and valid recovery against production receiver integration.
### Scope
Cover truncated/invalid metadata and ASE control procedures, unsupported admitted codec/QoS values, multi-ASE asynchronous ordering, disable/release across states, partial acquisition and reconnect. Use a separate test lane; keep frozen BSim 17/26 and HIL recipes unchanged. Derive cases from applicable Bluetooth protocol behavior rather than copying GPL fixtures/private handles.
### Non-goals
No LC3plus, independent codec conformance claim, product capability expansion, host-only BlueZ unit success relabeled firmware acceptance, or weakened frozen gates.
### Technical context
src/bt_bap.c, src/audio_stream_session.c and tests/bsim/client/src/bsim_client_main.c expose current integration. The pinned survey identifies BlueZ unit/test-bap.c test_usr_spe and MTU-64 multi-ASE ordering cases; discover actual handles/ASEs dynamically.
### Open questions
Exact current SDK public encoded-GATT injection seam, spec version/response expectations and separate runner matrix are resolved during refinement before Ready.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Real encoded public control-point exchanges reject declared malformed/unsupported cases with expected responses, preserve valid state and subsequently start a valid stream.
- [ ] #2 Lifecycle and multi-ASE ordering cases prove externally visible disable/release/partial setup/reconnect cleanup and recovery, not mock call counts.
- [ ] #3 Tests execute production receiver integration in a separately provisioned lane with retained source/tool/image/case identities; synthetic BlueZ success is not substituted.
- [ ] #4 Existing 17/26 simulation recipes, physical limits, codec claims and independent-reference boundaries remain unchanged.
<!-- AC:END -->
