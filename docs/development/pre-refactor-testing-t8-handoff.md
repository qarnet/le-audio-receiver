# Phase T8 handoff — hardware baseline freeze

Status: **ACCEPTED (2026-08-04)** — executed and closed; this document is
the historical execution handoff.  Acceptance record, final hardware
matrix, final software gate (47 PASS / 0 FAIL / 47 TOTAL on the exact final
code `971e6a4`, coverage-baseline `1a5842d`, final docs `3c29421`), and the
sequence-gap evidence limitation are in
`docs/testing/pre-refactor-hardware-baseline.md`, `STATUS.md`,
`docs/development/pre-refactor-testing-plan.md` (Phase T8 row), and
`docs/development/workstation-transfer-status.md`.  Original execution
status at creation: ready for execution after T7 acceptance on `8f7bfca`.

## Goal

Freeze reproducible pre-refactor hardware behavior for nRF54L15 and nRF5340.
Run only repository-owned autonomous central/gate tooling, retain raw evidence,
and mark T8 accepted only when every matrix row passes without actionable
warnings or faults.

## Starting state

- Branch: `handoff/workstation-transfer`.
- T7 accepted code commit:
  `8f7bfcadde2cfd6446f5493bff7b88c6aa9d5a02`.
- T7 evidence docs commit at handoff creation:
  `029bde46971f549759025525821e83f15a2cc7d0`.
- Canonical software gate: 41/41, zero Kconfig assigned-value and compiler
  warnings.
- T8 is not started. Create `docs/testing/pre-refactor-hardware-baseline.md`.

Before hardware work, inspect `git status`, recent log, this handoff,
`AGENTS.md`, `docs/development/pre-refactor-testing-plan.md` Phase T8, and
`docs/development/workstation-transfer-status.md` hardware rules.

## In scope

1. Pristine production builds for nRF5340, nRF54L15, and central dongle.
2. Resolved build-contract check for both receivers.
3. Probe identity capture and reversible firmware flashing through repo OpenOCD
   helpers.
4. Autonomous Mode A, Mode B, reconnect, FLPR-recovery, and stock
   BlueZ/WirePlumber lifecycle gates listed below.
5. Raw command/log/counter/hash/runtime evidence and T8 status docs.
6. Narrow fixes only when an observed failure contradicts an already-decided
   contract. Escalate before architecture changes or destructive recovery.

## Out of scope

- Refactoring, new product behavior, new Bluetooth modes, threshold weakening,
  test bypasses, baseline lowering, or warning normalization.
- Human-operated central or phone testing.
- Probe-rs, nRF54L15 recovery, nRF53 mass erase, settings erase, bond deletion,
  or other destructive recovery without explicit orchestrator approval.
- Static probe-serial-to-board tables.
- Push, merge, PR, amend, force-push, or release tagging.
- Analog fidelity claims. User audibility observation may be recorded only as
  supplemental evidence.

## Safety and ownership rules

- Use `nrf-probes` immediately before each flash. Record raw tool output that
  includes probe serial, DPIDR, AP IDR map, and FICR PART evidence. Never infer
  target identity from USB port names.
- Flash only with `fw-flash-54l15`, `fw-flash-5340`, and, if needed,
  `fw-flash-dongle`. These overwrite firmware and reset target hardware; user
  requested continuation of T8 plan, which includes these operations.
- Do not run `nrf53_recover`, mass erase, probe-rs, arbitrary OpenOCD writes, or
  persistent-setting changes. Escalate if stale bonds block progress.
- Central must be nRF5340DK `hci_uart` on `/dev/ttyACM2`, attached as `hci0` at
  1,000,000 baud H4 with flow control. Verify controller address
  `C0:AA:BB:CC:DD:EE` and settings `powered le secure-conn cis-central`.
- Receiver consoles: nRF54L15 `/dev/ttyACM0`; nRF5340 `/dev/ttyUSB0`, both
  115200 8N1. Start capture before reset/flash. Preserve complete timestamped
  logs through acceptance review.
- No warning may be normalized. Compiler/Kconfig warnings fail. Boot `LOG_WRN`,
  OpenOCD warnings, assertions, faults, decode errors, underruns, ASRC capacity
  failures, integrity faults, and unexplained error logs fail unless repository
  policy explicitly documents that exact diagnostic as non-actionable.

## Stage 1 — clean preconditions

From repo development shell:

```bash
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
nrf-probes
sudo btmgmt --index hci0 info
```

Builds must be pristine and warning-clean. Build contract must pass. If dongle
identity or `hci0` state is wrong, use repository central setup from `AGENTS.md`;
do not improvise persistent controller settings.

## Stage 2 — nRF54L15 baseline

1. Start `/dev/ttyACM0` capture before flash/reset. Flash cpuapp + FLPR with
   `fw-flash-54l15`; retain flash/verify output and post-reset boot log.
2. Boot acceptance: `BLE ready`, `settings_load() OK`, advertising, I2S ready,
   FLPR handshake/active path, no actionable warning/error/assert/fault.
3. Run Mode A 120 s with `python3 scripts/bap_central.py --duration 120`.
4. Run Mode B 120 s with
   `python3 scripts/bap_central.py --stereo --duration 120`.
5. Disconnect/reconnect: run fresh Mode A and Mode B 120 s sessions again.
   `bap_central.py` cleanup must release transports and disconnect ACL; next
   session must pair/reconnect and stream without receiver reset.
6. Capture `audio status` and `flpr offload`/`flpr status` after each relevant
   run. Prove decoded/pushed accounting, zero PLC/decode/push/I2S/integrity/
   capacity faults, FLPR success progression, and no unexplained fallback.
7. Run forced recovery with existing autonomous gate:

   ```bash
   python3 scripts/flpr_hang_gate.py --duration 180 \
     --port /dev/ttyACM0 --log /tmp/t8-flpr-hang-mode-a.log
   python3 scripts/flpr_hang_gate.py --duration 180 --stereo \
     --port /dev/ttyACM0 --log /tmp/t8-flpr-hang-mode-b.log
   ```

   Gate must prove fault ACK, bounded cpuapp fallback, runtime restart, changed
   epoch, probation, FLPR reactivation, resumed success, and zero audio-path
   faults. Existing `scripts/flpr_stall_gate.py` may supplement evidence, but
   FAULT_HANG gate is required because it proves full restart/reactivation.
8. Run stock desktop lifecycle gate, no `bap_central.py`/raw-HCI data path:

   ```bash
   python3 scripts/bluez-wireplumber-phase3-gate.py \
     --receiver "LE Audio Receiver" --serial /dev/ttyACM0 \
     --duration 120 --log-dir /tmp/t8-phase3-54l15 --stage full
   ```

   Require clean first pair, 120 s playback, reconnect playback, receiver
   reset, bonded reconnect, and third playback, with gate exit 0 and preserved
   logs. Do not manually operate a central.

## Stage 3 — nRF5340 baseline

1. Start `/dev/ttyUSB0` capture before flash/reset. Flash both cores with
   `fw-flash-5340`; retain OpenOCD output including auto-detected target
   evidence and post-reset boot log.
2. Run Mode A 120 s, Mode B 120 s, then fresh reconnect Mode A and Mode B 120 s
   using `scripts/bap_central.py` as above.
3. Capture `audio status` after each run. Prove APLL actuator active, identity
   rate path, repeat fallback zero in steady state, expected frame accounting,
   and zero warnings/assertions/decode/push/I2S/integrity faults.
4. nRF54-only FLPR and stock desktop lifecycle gates are not repeated on
   nRF5340 unless needed to diagnose a shared regression; T8 plan assigns FLPR
   and Phase 3 desktop lifecycle evidence to nRF54L15.

## Evidence document

Create `docs/testing/pre-refactor-hardware-baseline.md` with:

- exact tested commit(s), branch, dirty/clean state, date, workstation, NCS
  version, commands, exit codes, elapsed durations;
- full build and build-contract outcomes;
- raw probe identity evidence (DPIDR, AP IDR map, FICR PART) without presenting
  a reusable static mapping table;
- flash/verify results and log paths;
- per-target/per-mode stream command, central frame count/fps, receiver status
  counters before/after, FLPR/APLL state, disconnect/reconnect result;
- FLPR hang gate ACK/restart/epoch/probation/reactivation/fallback evidence;
- BlueZ/WirePlumber phase-3 stage results and artifacts;
- explicit warning/error scan and disposition;
- matrix verdict for every row, plus missing/unavailable evidence clearly
  marked. Never infer a pass from script structure.

Update `STATUS.md`, `docs/development/pre-refactor-testing-plan.md`, and
`docs/development/workstation-transfer-status.md` only after every required row
passes. T8 remains open on any missing row.

## Verification and commit order

1. Before hardware: clean status and exact commit anchor.
2. After any code/test/config fix: focused verification, commit fix, rerun every
   affected hardware row on exact clean commit.
3. Final software gate on final code commit:

   ```bash
   ./scripts/test-all.sh
   ./scripts/test-coverage.sh
   fw-build-5340
   fw-build-54l15
   fw-build-dongle
   python3 scripts/check-build-contract.py \
     --nrf5340 build/nrf5340 \
     --nrf54l15 build/nrf54l15
   git diff --check
   ```

4. Commit scoped implementation fixes separately from final evidence docs.
   Commit this handoff and final evidence/status docs when acceptance is real.
5. Return files changed, exact commands/results, hardware logs/counters, warning
   scan, commits, deviations, and blockers. Leave worktree clean.

## Escalation

Stop and report without claiming T8 acceptance when:

- hardware identity is ambiguous or raw probe evidence is unavailable;
- sudo, D-Bus, PipeWire, serial access, hardware, DAC, or central is unavailable;
- flash/verify requires recovery or destructive action;
- two materially different attempts fail same gate;
- observed behavior contradicts contract or needs architecture invention;
- warning/fault cannot be explained and fixed within narrow scope;
- any required matrix row lacks direct evidence.

Preserve logs and current worktree. Report exact blocked criterion, attempts,
commands/errors, hardware state, git status, one precise question, and smallest
next-step hypothesis. Do not weaken gates, erase settings, normalize faults, or
commit incomplete acceptance evidence.
