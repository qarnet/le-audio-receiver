---
id: PB-034
title: Migrate canonical BabbleSim to nRF54L15BSim
status: Done
assignee: []
created_date: '2026-09-22 22:50'
updated_date: '2026-10-03 00:14'
labels:
  - 'size:M'
  - 'area:testing'
dependencies: []
priority: p1
type: feature
ordinal: 32000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

Canonical BabbleSim Stage 1 still builds receiver and source against nRF5340BSim, leaving active nRF5340 controller and target dependencies in mandatory software acceptance. Its pinned startup PLC histories also describe nRF5340 HCI-IPC timing rather than target-independent receiver behavior.

### Desired outcome

Run full canonical BAP simulation matrix on `nrf54l15bsim/nrf54l15/cpuapp` for both simulated peers with deterministic transport, audio, loss, reconnect, and lifecycle behavior preserved, while pinning measured nRF54L15BSim-native startup histories.

### Scope

- Replace nRF5340BSim board defaults, executable naming, sysbuild/controller configuration, test metadata, and LSP links.
- Select and pin controller configuration supported by NCS v3.3.0 for nRF54L15BSim.
- Rebaseline only controller-induced startup PLC histories to measured nRF54L15BSim behavior using new target-native recipe IDs; retain nRF5340 startup recipes as historical calibration records.
- Preserve all 17 scenarios, 26 runs, LC3 fixture bytes, logical-sequence TX hashes, PCM oracle limits, explicit loss and malformed-SDU injections, reconnect behavior, lifecycle checks, and strict failure handling.
- Update build contracts and active documentation owned by Stage 1.

### Non-goals

- Simulate FLPR, I2S hardware, physical oscillator drift, or production SDC timing where nRF54L15BSim lacks those models.
- Change receiver production audio behavior or accepted numerical PCM limits.
- Inject artificial scenario-specific startup loss or delay to imitate legacy nRF5340 HCI-IPC timing.
- Rewrite or remove immutable historical evidence or legacy nRF5340 stateful recipes.

### Technical context

NCS v3.3.0 provides `nrf54l15bsim/nrf54l15/cpuapp`. Upstream Bluetooth Audio BSim uses nRF54L15BSim with SW Split. Repository BSim already uses I2S and ASRC stubs, so missing physical I2S and FLPR models do not block this migration. Correct role-specific ISOAL pools remove two-CIS setup failures. Integrated SW Split then delivers corpus frame 0 immediately, unlike legacy nRF5340 HCI-IPC runs that produced 8, 11, or asymmetric Mode A startup PLC actions. Product decision on 2026-09-23 selected target-native startup rebaselining instead of synthetic legacy timing injection.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Receiver and source BabbleSim builds use nrf54l15bsim/nrf54l15/cpuapp with no active nRF5340BSim target or CPUNET image dependency.
- [x] #2 All 17 canonical scenarios and 26 runs pass with measured nRF54L15BSim-native startup recipes while existing LC3 fixture bytes, logical-sequence TX hashes, explicit loss and malformed-SDU injections, reconnect behavior, lifecycle checks, PCM oracle limits, run counts, and strict failure handling remain unchanged.
- [x] #3 Resolved controller configuration and role-specific ISOAL capacity are pinned and documented, and build output contains no ignored compiler, linker, Kconfig, CMake, or runtime warnings.
- [x] #4 BSim metadata, helper defaults, executable discovery, LSP links, build-contract tests, STATUS.md, and applicable development documentation describe nRF54L15BSim as canonical Stage 1 target.
- [x] #5 Legacy nRF5340 startup recipes and historical evidence remain retained, and canonical nRF54L15BSim scenarios contain no artificial startup loss or delay injection.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Port BabbleSim board selection, executable naming, controller/sysbuild configuration, compile-database links, scan capacity, and role-specific ISOAL pools from nRF5340BSim to `nrf54l15bsim/nrf54l15/cpuapp`, preserving receiver/client roles and existing I2S/ASRC stubs.
2. Add measured nRF54L15BSim-native startup recipe records and map canonical scenarios to them without changing fixture bytes, TX hashes, numerical PCM limits, explicit loss/malformed injections, lifecycle semantics, or run counts. Retain legacy nRF5340 recipes unchanged.
3. Update Stage 1 metadata, parser contracts, build-contract regression tests, and active documentation so nRF54L15BSim is canonical and stale active nRF5340BSim paths fail validation.
4. Run focused recipe/parser/script/config tests and both BSim builds, resolving every compiler, linker, Kconfig, CMake, and runtime warning.
5. Run `scripts/bsim-stage1-run.sh` for all 17 scenarios and 26 runs, pin measured target-native startup totals from repeated evidence, confirm existing transport hashes and PCM limits, record exact evidence, and move PB-034 to Review.

Verification: `python3 -m unittest tests.unit.build_contract.test_build_contract`; `python3 -m unittest discover -s tests/unit/bsim_runner -p "test_*.py"`; `bash scripts/gen-lsp-links.sh --check`; receiver/client builds through `scripts/bsim-stage1-run.sh`; full `scripts/bsim-stage1-run.sh`; `nix develop -c backlog doctor`; `git diff --check`.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Pre-implementation probes resolved controller shape. NCS `nrf54l15bsim/nrf54l15/cpuapp` defaults to integrated SDC. A full uncommitted SDC probe built both peers and passed 12/17 scenarios, but failed all 10 ms two-CIS startup/reconnect variants because initial SDUs reached strict recipes without corpus payload; it also emitted ignored `BT_LL_SW_SPLIT` assignment warnings when config was supplied without devicetree selection. Therefore SDC is not the accepted Stage 1 shape.

SW Split requires both `bt-ll-sw-split` snippet devicetree selection and Kconfig. Snippet plus old receiver-only controller fragment built both peers but failed at runtime: receiver advertising data exceeded controller default 31-byte limit (`-EDOM`), and client reported ISO packet/context mismatch (1 != 3). Upstream broad audio overlay is unsuitable for receiver because unrelated sync-transfer features trigger an NCS v3.3.0 SW Split compile defect. Implementation must use repo-owned role-specific integrated-controller fragments: receiver pins extended advertising/data length plus peripheral ISO and matching 3 TX/4 RX host-controller buffers; client pins central ISO plus matching 3 TX/1 RX buffers. No nRF5340 CPUNET image remains.

Post-build evidence on 2026-09-23 corrected receiver ISOAL sinks to 2 and client ISOAL sources to 2, eliminating all Setup ISO Data Path `0x206e` / status `0x0c` failures. SW Split then passed all three non-streaming scenarios but every streaming scenario reached corpus frame 0 at recipe action 0, while legacy recipes required nRF5340 HCI-IPC startup PLC metadata. Default SDC and integrated SW Split are two materially different controller attempts; neither reproduces legacy startup history. No supported controller setting adds old IPC timing. Product direction selected measured nRF54L15BSim-native startup recipes, with legacy recipes retained and no synthetic delay/loss injection.

2026-09-24 continuation: Found and preserved existing uncommitted migration on feature/nrf54l15-hil-fixture-session at 6941c82. Product owner renewed approval to finish full nRF54L15 migration (PB-039). Next verify current paced-client/native-recipe probe against all 26 runs before completing recipe manifests/parser pins. No legacy timing synthesis or production audio changes authorized.

2026-09-24: Two preserved nRF54L15BSim diagnostics at /tmp/opencode/pb034-continuation-20260924-01 and -02 both FAIL. Fixed first-stop fixture admission (remaining CIS now reaches 45 sends; both device processes exit zero, old parser pins still reject). Blocking loss-contract mismatch: omitting 18 right TX intervals produces no corresponding SW Split RX SDUs, Mode A accumulates stale halves, then left corpus66 arrives where48 is required. Explicit empty SDUs would test a different fault; production Mode A repair is outside PB-034 scope. Need explicit contract/scope decision before either change. Single-CIS unexpected loss at action5 also remains under diagnosis, not accepted startup. Full record: docs/development/pb-034-continuation-20260924.md. Seven focused runner tests passed; no acceptance boxes checked, no production changes or hardware mutation.

2026-09-24 product-owner correction: the prior scope escalation was premature, not a genuine hard blocker. Grounded production/fixture repairs and expanded investigation are already authorized to preserve the approved product and meaningful acceptance. Resume diagnosis of single-CIS delivery and absent-callback Mode A behavior; do not substitute empty SDUs merely to pass the absent-transmission test. Standing authority is now recorded in AGENTS.md in both worktrees. Retain prior failed evidence and classification history.

2026-09-24: Continued directly in primary repository at /home/thomas-workstation/repos/le-audio-receiver on feature/nrf54l15-only-continuation, based on 6941c82. Moved all uncommitted continuation source/backlog changes from temporary worktree using file patches; kept old raw run directories untouched. Generated BSim compile database links will be refreshed by builds. Grounded fixture and production fixes are authorized; prior scope-only blocker is withdrawn.

2026-09-24 primary repair: full nRF54L15BSim matrix passed 17 scenarios/26 runs at /tmp/opencode/pb034-primary-canonical-20260924-04.log. Reliability policy repairs measured single-CIS payload loss without QoS/fixture/threshold changes. Production bounded Mode A concealment was reproduced red on physical XIAO before repair and green afterward (16/16), preserving real absent-transmission injection. Pinned liblc3 startup PLC seed required independent target-native loss golden; legacy golden unchanged, strict regeneration and host+physical ARM 46-record calibration passed including wrong-history rejection. Narrow exact PSN-gap diagnostic is required once only in deliberate loss case. Source board currently runs calibration pending fixture restore. Details: docs/development/pb-034-primary-repair-results.md. Current documentation/final gate still pending.

User paused work 2026-09-24. Continued (2026-09-24 snapshot, retired at Git rev `a94f010`) in primary repository, not prior temporary worktree. Existing strict BSim26-run PASS and physical red/green module plus46-record calibration evidence remain retained. Current documentation/coverage/final aggregate reruns remain pending; later HCI and QoS changes have not received a complete unit rerun. Do not treat earlier scope-only blocker or earlier source-board calibration-image note as current state.

2026-09-25: strict BSim 17 scenarios/26 runs passed (/tmp/opencode/nrf54-only-bsim-20260925-r1.log), unchanged TX hashes and 2048/512/32750 PCM limits (observed 257/182/32767); unit phase 75/0/75. Coverage report-only 36 sources, baseline unchanged. Clean exact-commit enforcement stopped before builds on dirty-worktree guard; AC4 documentation/integration remains unchecked. See docs/development/nrf54l15-migration-verification-results-20261001.md (the 2026-09-25 continuation snapshot was retired at Git rev `a94f010`).

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks were recorded in the retired 2026-10-02 wrap-up snapshot (Git rev `a94f010`; boundaries preserved in docs/development/nrf54l15-migration-verification-results-20261001.md). Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.

2026-10-02 PR15 runtime repair: reopened after retained hosted artifacts showed PHY255 for missing default NtNcable dlopen library, then peer300s timeouts. Prior local cached plugins masked incomplete build closure. Public builder now compiles exact PHY/NtNcable/Magic static and runtime closure with unchanged strict policy; actual-PHY loader readiness runs before peers. Real isolated cold builds and missing/corrupt-plugin/deadline/SIGTERM cleanup cases pass; unchanged local Stage1 passes17scenarios/26runs. No recipe, model, seed, PCM, transport or lifecycle limit changed. Hosted final verification pending.
Final runtime lifecycle validation expanded to SIGINT and kill escalation for a SIGTERM-ignoring owned PHY: focused 62 tests OK, explicit child-disappearance assertions. Exact PHY/NtNcable/Magic closure remains strict and no installed SDK source was modified.

2026-10-03 encrypted-peer closure: hosted37056518050 passedPHYready/unit/coverage/firmware butmissinglibCryptov1 disabled realencryption andsecuritytimedout. FullSDKruntime-loaderaudit addedthe precise ext_libCryptov1 target, visible hash-pinned bundledOpenSSLbuild with Werror and three exact source/hash diagnostic exceptions, and an ELF32 real-library probe requiring six APIs plus independent AES/CCM knownanswers and invalid-MIC rejection. Peer/probe/library ELF ABIs are checked before matrixlaunch. RealEncryption=1 unchanged. Cold/invalidABI/missingsymbol/source-drift/unrelated-warning tests pass; localunit77/0/77 and strictStage1 17/26 pass. See docs/development/nrf54l15-ci-crypto-closure-results-20261003.md. Hostedfullclosure verification pending newpush; no SDKsourcepatch, hostOpenSSLsubstitution, hardware or releaseclaim.
<!-- SECTION:NOTES:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-23 17:30
---
Refined during PB-034 implementation after controller evidence proved legacy startup PLC counts were nRF5340 HCI-IPC timing artifacts. Product owner selected target-native nRF54L15BSim startup rebaseline; transport payloads, TX hashes, loss injections, lifecycle checks, PCM limits, and strict failures remain frozen.
---
<!-- COMMENTS:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Canonical integrated SW Split receiver/client select nrf54l15bsim CPUAPP, with measured native startup recipes and role-specific ISOAL. All 17 scenarios/26 runs pass, preserving TX hashes, PCM limits, absent-transmission/malformed faults and lifecycle semantics. Historical recipes and narrow source/hash-scoped dependency warning dispositions remain retained. Metadata/helpers/LSP/contracts/current docs agree.

2026-10-01 final local verification: exact clean 104e67ade0e361093a88d1832b5dcb41552e8e8d (production e478e59) passed canonical 80/0/80 with unchanged coverage enforcement, 17-scenario/26-run nRF54L15BSim, three physical build shapes, resolved contract 73/73, HIL host 340 passed/one intentional hardware-opt-in skip, six normal-image HCI cases (72000 writer frames, PLC 428, zero case/kernel HCI/SMP alerts), and frozen exact-local-artifact matrix 20/20 with zero failed/cancelled/cleanup children. Independent review rehashed child/aggregate payloads, exact archives and 120 ordered identity checks. See docs/development/nrf54l15-migration-verification-results-20261001.md and docs/development/nrf54l15-final-reference-audit-20261001.md. Historical failures remain immutable. No human PR acceptance, hosted CI, analog MA1/SA1, active-draft FR4, publication, PB-013 offload or PB-041 nonce claim. Local review only; no push, PR, merge or Done transition.

2026-10-02: Product owner requested a combined migration pull request and Done transitions for completed work. All criteria were already checked from recorded implementation evidence. Done is PR-gated and remains pending human product-owner merge; no release or analog acceptance is inferred. This transition ships with the complete implementation in the PB-039-prefixed migration PR. Firmware verification remains exact 104e67a; later closure changes are documentation/backlog only. Fresh PR-preparation host checks were recorded in the retired 2026-10-02 wrap-up snapshot (Git rev `a94f010`; boundaries preserved in docs/development/nrf54l15-migration-verification-results-20261001.md). Prior private raw /tmp/opencode lab run roots are absent in this session; committed result reports retain their recorded outcomes, image/archive hashes and identity evidence. No fresh hardware rerun or independent rehash of those unavailable raw runs is claimed during PR preparation.

2026-10-02 CI runtime follow-up: complete default dynamic-model closure and actual loader/model initialization preflight remove the cold-SDK dependency gap, without building all components or accepting timeout as readiness. Precise NtNcable/Magic models are unchanged. Cancellation is recorded until child ownership is established, then terminate/kill/reap cleanup runs; public SIGTERM test verifies no surviving PHY. Focused60 tests OK; parser146/0; full local unit77/0/77 after correcting an outdated incomplete-runtime fixture expectation; local strict Stage1 unchanged17/26 passes. See docs/development/nrf54l15-ci-runtime-closure-results-20261002.md. Done repair remains in PB-039-prefixed PR15 pending hosted verification and human merge.
<!-- SECTION:FINAL_SUMMARY:END -->
