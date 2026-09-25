---
id: PB-019
title: Replace Linux HCI UART adapter with XIAO nRF54L15
status: In Progress
assignee: []
created_date: '2026-09-13 01:24'
updated_date: '2026-09-25 00:33'
labels:
  - 'size:M'
  - 'area:interoperability'
dependencies: []
priority: p1
type: feature
ordinal: 19000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
The Linux central still requires an nRF5340 adapter. The approved all-nRF54L15 goal provides two XIAO boards, not a third DK. The source board must alternate standalone HIL and Linux HCI roles without changing receiver acceptance.

### Desired outcome
Use the second XIAO nRF54L15 as the autonomous Linux HCI central through its stock SAMD11 CDC UART bridge, with verified identity and lossless transport under the existing audio workload.

### Scope
Build a single-image nRF54L15 central controller and H4 bridge; preserve lab address C0:AA:BB:CC:DD:EE and two-CIS LE Audio. Qualify a baud rate with enough stereo bandwidth, explicit no-flow attachment (the stock XIAO has no RTS/CTS wiring), and mono/Mode A/Mode B pairing, stream and reconnect evidence. Revalidate hardware identities before each flash/reset and reject stale attachment. Replace old dongle helpers and active nRF5340 adapter documentation only after proof.

### Non-goals
No SAMD11 firmware replacement, USB HCI claim, persistent attachment service, weakened audio limits, or changed intended receiver capabilities. No claim that H4 without flow control is generically reliable: it must pass actual bridge and audio qualification.

### Technical context
NCS v3.3.0 hci_uart handles ISO H4 type0x05; UART driver supports no-flow operation. Stock sample documents RTS/CTS, so this XIAO adaptation needs explicit measured transport proof. Installed SDC supports nRF54L15 central ISO; integrated SW Split is another supported controller option, not an assumed hardware requirement. Existing PB-033 session binds both boards and validates DP/AP/FICR/USB identity. Host native_sim/Python tests remain platform-neutral.

### Open questions
No unresolved product choices. Bridge throughput, transport recovery and controller compatibility need technical validation. Escalate only for unavailable hardware/access or a necessary product/acceptance change after exhausting grounded repair paths.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Single-image XIAO nRF54L15 HCI firmware builds with NCS v3.3.0, fixed lab address C0:AA:BB:CC:DD:EE, two-CIS central support and no ignored build warnings.
- [ ] #2 Retained runtime identity, image hash, UART/baud/flow settings and Linux HCI evidence prove the selected XIAO exposes powered le secure-conn cis-central through its stock bridge without byte corruption.
- [ ] #3 Autonomous mono, Mode A and Mode B pairing, streaming, disconnect and reconnect pass through the XIAO central with receiver-side delivery and audio metrics meeting existing limits.
- [x] #4 Build/flash/reset/attachment helpers and tests fail closed on wrong or ambiguous identity, missing device, stale attachment and controller startup failure.
- [ ] #5 After replacement proof, active workflows and docs no longer depend on an nRF5340 HCI adapter; historical evidence remains unchanged.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Build repo-owned single-image HCI application using the installed H4 transport with explicit XIAO UART/identity/controller configuration. 2. Revalidate source role and qualify actual stock bridge through Linux HCI initialization and encoded HCI traffic. 3. Run autonomous receiver audio/lifecycle acceptance; investigate and repair transport/controller defects without weakening requirements. 4. Replace legacy dongle helpers, add public-boundary regressions, refresh current docs and run integrated gates.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-24 refinement follows explicit user-provided two-XIAO hardware and full-migration approval. The prior DK/RTS-CTS-specific implementation assumption is superseded, not the lossless LE Audio acceptance requirement. Same owner coordinates PB-034 final gate and this next component under the approved full migration; no implementation is delegated.

Paused by user on 2026-09-24 for OpenCode maintenance, not technically Blocked. Authoritative resume: docs/development/nrf54l15-only-resume-20260924.md. Primary repository feature/nrf54l15-only-continuation at 6941c82 owns all uncommitted work. Implemented prototype continuous async/TIMER21 H4 bridge, parser tests, assertion provenance and verified NCS UART initialization workaround (actual bounce switch threshold112 instead of502). Physical source currently contains async-r5 image SHA256 3e95fba54276d6651e1f099cc6d83e6e9d4c3e98649ee71222a2852b1ea5fed1. Six30s source/lifecycle cases ran at old QoS but ongoing receiver PLC required further diagnosis; failed20-credit proxy and100us idle-timeout probes were removed. Latest endpoint QoS5/20 with unchanged40ms presentation delay improved delivery. Interrupted120s-per-case run retained five12000-frame completions, not final ModeB reconnect; ModeA reconnect PLC increased78to194 and still needs classification. Root helpers/controller migration and final gates remain unfinished. Abort left detached btattach/btmon; they were explicitly stopped after identity/adapter checks, only lab peer removed and owned adapter powered off. No serial-MCP connection remains. No new commits or release changes.

Primary continuation evidence: docs/development/pb-019-hci-resume-results.md. Classified interrupted Mode A reconnect as real midstream loss but within unchanged 90-percent valid / 5-percent PLC limits; no controller-defect attribution. Completed new Mode A and Mode B fresh/reconnect pairs, then all six 120-second cases in /tmp/opencode/pb019-resume-six-20260924, each source exit zero and all terminal numerical limits satisfied with no UART warning/error lines. Retained standard 48_4_1 QoS 5/20; endpoint public-response suite passes 57 tests. Fresh primary HCI build exposed signed overflow in already-bypassed SDK static threshold. Added exact-SDK-file-hash-guarded, source-local GCC warning exception; assertions remain enabled and rebuilt HEX is byte-identical to async-r5. Physical RRAM segment readback matches all 199168 source image bytes. Post-verification debug interference required identity-checked reset; H4 Reset and lab address responses then passed and port closed. Supported helpers, complete HCI qualification provenance/lifecycle tests, new receiver image, downstream migration and clean final gates remain outstanding; no AC or release acceptance claimed.

2026-09-24 supported-helper integration: dongle/README.md and AGENTS.md active central setup now document session-bound XIAO HCI role, stock 1 Mbaud SAMD11, dynamic adapter and supervised cleanup; retired obsolete hci_ipc netcore and unused fragments. docs/development/pb-019-hci-resume-results.md records exact current receiver/HCI image hashes and six new 120-second helper rows, zero error counters but nonzero PLC, within unchanged limits. H4 parser matrix is direct, outside src/ numeric coverage. Matrix unit tests 44/44 and H4 traffic 1/1 passed; full matrix checker still fails on two pre-existing renamed Mode A queue-overflow witnesses in pending PB-034 edits, so AC1-4 remain unchecked pending green verification. AC5 awaits exhaustive active-doc audit; no Done/Review or release claim.

Inventory regression added after earlier note: test-matrix suite now passes 45/45, H4 suite passes 1/1; matrix checker still reports two pending PB-034 Mode A witness errors (test_queue_overflow_drops_oldest). No acceptance checkbox checked while that verification fails.

2026-09-24 matrix accounting correction: PB-034 Mode A DROP now cites oversized-input rejection and bounded absent-channel recovery cites survivor emission plus mate PLC, not dropped audio. No thresholds changed. Matrix checker 0 errors / 0 notes, matrix tests 45/45, git diff --check clean. With recorded clean HCI build HEX SHA-256 3e95fba54276d6651e1f099cc6d83e6e9d4c3e98649ee71222a2852b1ea5fed1, fresh DP/AP/FICR and H4/Linux identity evidence, six supported-helper 120-second physical rows within frozen limits, and helper fail-closed lifecycle tests (build7/flash8/reset4/attach9, H4 suite1), AC1-4 checked. AC5 remains unchecked until exhaustive active-doc/workflow audit; no clean-commit or release acceptance claimed.

2026-09-25 correction: final production-image repeat /tmp/opencode/pb019-final-six-20260925-r1 failed Mode A after mono/reconnect passed: HCI hardware error 0x07, parser -EPROTO, underrun 1, reset 1 and controller command timeouts. Earlier six-case successes remain historical, not final qualification. External RAM-trace six-case pass perturbs timing and is not a repair. AC2/AC3 unchecked; AC1 build/AC4 helper tests retain evidence, AC5 audit remains pending. Prototype / qualification incomplete. Continue grounded UART boundary investigation; ordinary engineering failure, not a technical hard blocker. See docs/development/nrf54l15-only-continuation-20260925.md. Dirty-tree coverage enforcement exited 1 before builds with exact clean-commit precondition; explicit LOCAL commit authorization needed before clean acceptance; no push/PR authority inferred.
<!-- SECTION:NOTES:END -->
