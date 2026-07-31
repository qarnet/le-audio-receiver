# Phase 3 final review handoff

## Goal

Fix two preflight/state false positives and close Phase 3 documentation.

## In scope

- Replace `_wait_for_bluez_spa()` `session.services` fallback with proof actual
  WirePlumber process has `libspa-bluez5.so` mapped. For owned process use its
  PID; for active service resolve `MainPID`. Keep optional PipeWire bluez device
  evidence after connection, but pre-connect success needs actual plugin map.
- Add deterministic tests: services string without plugin must fail; actual map
  must pass; dead/missing PID must fail.
- Make failed `bluetoothctl remove` fatal unless exact postcondition shows device
  object no longer exists and no Paired/Bonded state remains. Add tests.
- Update `STATUS.md`, `docs/design.md`, and interoperability plan: Phase 3
  accepted with three strict stock playbacks; Phase 4 compatibility expansion
  not needed because bare BAP passed, leaving CAP/CAS disabled.
- Track this handoff, run Phase 2+3 tests, `git diff --check`, inspect status,
  commit, leave clean worktree.

## Constraints

No hardware rerun, behavior outside preflight/fail-closed checks, host changes,
push/PR, amend/rewrite.
