# PB-053 portable source snapshot fixture (2026-10-05)

Scope: `scripts/bluez_host_guest.py`, `tests/unit/bluez_host_guest/test_bluez_host_guest.py`, and this document. No new preparation/image, VM, PB-051 execution, stage, commit, push, or binary/corpus edit.

## Failure boundary

Native source snapshot regression currently reads private `/tmp/opencode/.../btvirt`; that binary is not available to hosted CI. This regression proves bounded copied-byte provenance across nine source-map keys, not executable emulator acceptance. Production preparation and manifest checking must still require the fixed `EMULATOR_SHA`.

## Repair

Give pure `freeze_sources(paths)` helper a keyword-only `expected_emulator_sha=EMULATOR_SHA`. Validate supplied lowercase 64-hex digest and compare it to the computed emulator entry. Production caller uses unchanged default; no CLI or manifest pin override. In native regression, create a real authored emulator byte file in `TemporaryDirectory`, independently hash its bytes and pass that expected digest. Retain real source/staged drift and emulator mode `0755` checks. Reject mismatched and invalid expected digests. Do not claim authored file is real emulator.

## Verification

Run ResourceWarning-as-error unittest discovery for `bluez_host_guest`, `bluez_host_results`, `bluez_host_process`, `bluez_guest_limits` and `git diff --check`. These native checks are not image, VM or emulator qualification.
