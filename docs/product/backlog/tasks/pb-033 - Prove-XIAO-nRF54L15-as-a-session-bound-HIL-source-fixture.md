---
id: PB-033
title: Prove XIAO nRF54L15 as a session-bound HIL source fixture
status: In Progress
assignee: []
created_date: '2026-09-22 02:00'
updated_date: '2026-09-22 03:34'
labels:
  - 'size:L'
  - 'area:hil'
dependencies:
  - PB-032
priority: p1
type: feature
ordinal: 31000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem

The HIL runner assumes one unique nRF54L15 receiver and one nRF5340DK/J-Link source. With two XIAO nRF54L15 boards attached, family-only discovery is ambiguous, volatile `/dev/ttyACM*` paths cannot identify roles, and the existing nRF5340-specific source image cannot build for nRF54L15. The host also now exposes probe discovery as `nix-nrf probes` while the runner still invokes `nrf-probes`.

### Desired outcome

One test cycle can assign two attached XIAO nRF54L15 boards to receiver and source roles once, retain that mapping in an external ephemeral session manifest, revalidate each device before every target action, and run a bounded XIAO-to-XIAO LE Audio HIL proof without changing receiver behavior or permanently naming lab boards.

### Scope

- Add session-scoped role binding for same-family CMSIS-DAP boards. The operator explicitly selects two distinct probe serials; discovery then binds each role through probe fingerprint plus correlated USB/tty identity, never current tty numbering.
- Create the manifest outside the repository under `/tmp/opencode/hil-sessions/<session-id>/devices.json` with exclusive creation. Record probe serial/product, raw DPIDR, AP0 through AP3 IDRs, FICR PART/VARIANT, USB parent and stable udev properties, the current tty path as an observation, selected role, and expected firmware role.
- Reuse one immutable manifest across a caller-selected sequence of existing checked-in HIL rows. Re-read and compare hardware and USB/tty identity immediately before every flash, reset, serial-open, and row action.
- Fail closed before target mutation on ambiguous or missing probes, duplicate assignments, hardware fingerprint drift, USB/tty correlation drift, or manifest changes. After exact images are flashed, require receiver boot identity and source `hello` identity before any stream action.
- Update HIL discovery to invoke `nix-nrf probes` with explicit serials. Retain AP IDRs through a read-only explicit-probe OpenOCD fingerprint because the command's table exposes DPIDR/PART/VARIANT but not its internally scanned AP map.
- Port the dedicated BAP unicast-client/source fixture to a XIAO nRF54L15 single-core SDC image using GRTC system-counter time, an exact XIAO source overlay, and explicit source probe selection.
- Prove the checked-in `rh3.fresh_mono_48_4_1`, `rh3.fresh_mode_a_48_4_1`, and `rh3.fresh_mode_b_48_4_1` rows from source to receiver on two physical XIAO boards with retained evidence.
- Preserve current nRF5340-based fixtures and contracts until separate replacement acceptance.

### Non-goals

- Infer DAC/I2S wiring from GPIO electrical loading.
- Create a permanent global board-role registry or static probe-to-board documentation.
- Remove nRF5340BSim, the nRF5340DK HIL source, or the HCI-UART dongle.
- Replace the accepted nRF5340 HIL-source release archive or RH4 artifact schema; this proof uses a local nRF54L15 source image while the existing dual-image archive remains valid.
- Claim RH4/FR4 acceptance, public release acceptance, or 7.5 ms product acceptance.
- Change receiver audio behavior.

### Technical context

Current role schema and discovery live in `scripts/hil/model.py` and `scripts/hil/discovery.py`; they hardcode receiver `nrf-probes`/nRF54L and source J-Link/nRF53 roles. `tests/hil/fixture.json` and `tests/hil/fixture.local.example.json` pin the source to nRF5340DK. `scripts/bin/fw-build-hil-source` and `scripts/bin/fw-flash-hil-source` are dual-core nRF5340-only. `hil/source/app/src/hil_source_controller_time.c` mirrors the nRF5340 controller RTC through IPC and RTC0, which does not exist on nRF54L15. NCS v3.3.0 supports nRF54L15 central ISO and two CISes; `nrf/samples/bluetooth/iso_time_sync/src/controller_time_nrf54.c` uses `nrfx_grtc_syscounter_get()` for controller time. A direct build-only probe against nRF54L15 reaches source compilation and fails at the expected nRF5340 RTC/IPC implementation. The current `nix-nrf probes` implementation already scans AP0 through AP3 but prints only DPIDR, PART, and VARIANT, so repository discovery needs an explicit-probe read-only fingerprint to retain the raw AP map required by the session contract.

### Open questions

None.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A public HIL session command accepts two explicit, distinct XIAO nRF54L15 probe serials, resolves their correlated USB serial endpoints, creates `/tmp/opencode/hil-sessions/<session-id>/devices.json` exclusively with complete probe/AP/FICR/USB identity, and lets repeated checked-in row invocations reuse the same role assignment without depending on volatile tty numbers.
- [ ] #2 Before each flash, reset, serial open, and row action, the runner re-reads the selected probe fingerprint and USB/tty correlation and rejects ambiguity, absence, duplicate assignment, manifest mutation, or identity drift before issuing the target action. After flashing, receiver boot markers and source `hello` firmware identity must pass before streaming.
- [ ] #3 The dedicated source fixture builds and flashes as a warning-clean single-image XIAO nRF54L15 SDC application with GRTC controller time, an exact XIAO UART/RF overlay, explicit CMSIS-DAP serial selection, the existing `le-audio-hil-source-rh1` hello identity, and no nRF5340 RTC/IPC or CPUNET dependency.
- [ ] #4 Retained physical XIAO-to-XIAO evidence passes `rh3.fresh_mono_48_4_1`, `rh3.fresh_mode_a_48_4_1`, and `rh3.fresh_mode_b_48_4_1` with deterministic HIL records and no unexpected firmware, controller, transport, audio, OpenOCD, or host-tool warnings.
- [ ] #5 Existing nRF5340 fixture paths remain usable, focused discovery/session/source/artifact tests pass, and the canonical software gate remains green.
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. session identity/manifest foundation;
2. nRF54L15 source firmware plus build/flash tooling;
3. runner integration, physical 10 ms proof, regressions, and PR gate.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-22 phase 1 complete: host-only XIAO nRF54L15 session foundation adds board-aware binding validation, explicit nix-nrf probes identity resolution, read-only CMSIS-DAP AP/FICR fingerprints, immutable external devices.json manifests, revalidation, and create-session CLI.

2026-09-22 review repair: fixture and binding parsing now uses retained byte snapshots, input drift fails before session-directory creation, session schema integers and IDs are strict, and custom roots reject filesystem and repository overlap. Manifest permission, type, symlink, path, and parent-ID drift remain fail-closed.

Validation, no hardware actions: `nix develop -c python3 scripts/test_hil_runner.py` ran 105 tests, PASS; `nix develop -c python3 tests/hil/rh2_test.py` ran 251 tests, PASS; `nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil` PASS; `nix develop -c backlog doctor` PASS; `git diff --check` PASS. Phase 2 source firmware/tooling and phase 3 runner/physical proof remain.

2026-09-22 phase 2 complete: source fixture now selects an nRF5340 RTC0/IPC4 implementation or an nRF54L15 direct-GRTC implementation by SoC. The nRF5340 controller-time implementation moved byte-for-byte. The nRF54L15 implementation reads the Zephyr-owned GRTC system counter and adds no private GRTC channel, compare, GPPI, IPC, or `SYS_INIT`. The source app adds XIAO UART20 P1.9/P1.8 pinctrl, active RF fixed regulators on P2.5 active-low and P2.3 active-high, disabled conflicting stock buttons and SPI flash, 16000 fF oscillator loads, integrated-SDC central-ISO config, and separate explicit build/flash helpers. The nRF54L15 flash helper requires one named CMSIS-DAP serial and rejects unsafe serials plus missing, empty, nonregular, or symlink images before OpenOCD. Fake-subprocess tests prove no `nrf-probes` or `nix-nrf` identity discovery call. No hardware command ran.

Phase 2 validation, no hardware actions: `nix develop -c python3 tests/hil/rh2_test.py` ran 260 tests, PASS; `nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil` PASS; `nix develop -c fw-build-hil-source-54l15` PASS; `nix develop -c fw-build-hil-source` PASS. The nRF54L15 build produced nonempty `build/hil-source-nrf54l15/zephyr/zephyr.elf` and `zephyr.hex`, with no `domains.yaml`, `hci_ipc`, `merged.hex`, or `merged_CPUNET.hex`. Its `build.ninja` selected only `hil_source_controller_time_nrf54.c`; legacy nRF5340 `build/hil-source/app/build.ninja` selected only `hil_source_controller_time_nrf53_app.c`, and both legacy app and CPUNET hex images were nonempty. Resolved nRF54L15 config matched SDC central ISO, one group, two streams, six host/controller ISO TX buffers, GRTC, regulator, and flash-page-layout requirements; it did not enable `NRFX_RTC`, `MPSL_TRIGGER_IPC_TASK_ON_RTC_START`, or `BT_LL_SW_SPLIT`. Resolved DTS used only XIAO UART20 pinctrl, both required RF fixed regulators, disabled `spi00` and `mx25r64`, internal 16000 fF LFXO/HFXO, and no source overlay I2S, FLPR IPC, or TIMER node.

Warning audit: full build logs are retained at `/tmp/opencode/pb-033-phase2-fw-build-hil-source-54l15-nosysbuild.log` and `/tmp/opencode/pb-033-phase2-fw-build-hil-source-nrf5340.log`. Neither build emitted compiler warnings, Kconfig assignment warnings, errors, or unexpected diagnostics. Both emitted Zephyr CMake `__ASSERT() statements are globally ENABLED`, an unchanged informational diagnostic recorded in `STATUS.md`. Nix also printed its dirty-worktree provenance notice; it was not a firmware build diagnostic and no warning suppression was used. `nix develop -c backlog doctor` PASS; `git diff --check` PASS.

2026-09-22 phase 3A complete: host-only runner integration adds optional run-only `--session-manifest`, requires it for the nRF54L15 XIAO source fixture, rejects it for nRF5340 source fixtures, copies immutable manifest evidence, and revalidates both roles at setup identity, receiver/source serial opens, source/receiver flashes, and row action. Fresh tty paths feed console opens; post-open tty drift fails before guarded actions. Local image inventory and source flash helper select by logical source board. Existing no-session nRF5340 and RH4 artifact behavior remain unchanged.

Phase 3A validation, no hardware actions: `nix develop -c python3 scripts/test_hil_runner.py` ran 106 tests, PASS; `nix develop -c python3 tests/hil/rh2_test.py` ran 266 tests, PASS; `nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil` PASS; `nix develop -c fw-build-hil-source-54l15` PASS; `nix develop -c fw-build-hil-source` PASS; `nix develop -c backlog doctor` PASS; `git diff --check` PASS. Full logs: `/tmp/opencode/pb-033-phase3a-fw-build-hil-source-54l15.log` and `/tmp/opencode/pb-033-phase3a-fw-build-hil-source-nrf5340.log`. Both contained only Nix dirty-worktree provenance and documented Zephyr `__ASSERT() statements are globally ENABLED`; no compiler, Kconfig assignment, unexpected CMake, OpenOCD, or host-tool warning. Phase 3B physical proof remains required; no HIL row, hardware acceptance, RH4/FR4, release, or publication claim.
<!-- SECTION:NOTES:END -->

## Comments

<!-- COMMENTS:BEGIN -->
created: 2026-09-22 02:12
---
Refined 2026-09-22 after repository, live probe-tool, and NCS v3.3.0 inspection. Clarified explicit same-family probe selection, external immutable session-manifest shape, pre-mutation hardware revalidation versus post-flash firmware-role validation, exact 10 ms proof rows, and retained nRF5340 fixture/artifact non-goals. Size remains L because host identity/session orchestration, source firmware, build/flash tooling, and physical proof are one coupled acceptance outcome. PB-032 has green PR #14 checks but still requires human merge before PB-033 implementation begins.
---

created: 2026-09-22 02:26
---
Phase 1 implementation shape completed in docs/development/pb-033-phase1-session-binding-handoff.md. Phase is host-only: board-aware binding schema, explicit nix-nrf probes discovery, raw AP/FICR fingerprint retention, strict external session manifest, create-session CLI, and fake-lab drift tests. Firmware port, runner action integration, and physical proof remain phases 2 and 3. Execution waits for human merge of green PR #14.
---
<!-- COMMENTS:END -->
