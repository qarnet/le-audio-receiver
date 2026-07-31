# Phase 3 SPA proof correction

## Goal

Enforce actual WirePlumber process-map proof with no standalone fallback.

## In scope

- `_wait_for_bluez_spa()` returns true only when `/proc/<resolved-wp-pid>/maps`
  contains `libspa-bluez5.so`.
- Remove `pw-dump` factory/device success path and contradictory comments.
- Replace `test_wait_for_bluez_spa_pwdump_confirmatory`: maps missing + pw-dump
  bluez object must fail.
- Update docs if they mention optional fallback.
- Track handoff, run Phase 2+3 tests and diff check, commit clean.

## Constraints

No hardware/host actions, other behavior changes, push/PR, amend/rewrite.
