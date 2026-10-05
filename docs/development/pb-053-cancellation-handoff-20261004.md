# PB-053: fresh run binding and actual VM cancellation controls

## Scope

Files: host runner, guest init, shared results/tests, guest tests and PB-053 notes.
No protocol/data changes, vendor/SDK/host Bluetooth, PB-051, arbitrary guest RPC,
downloads, or unrelated dirt. R25 passed bounded normal flow; item still needs
actual timeout/cancellation, fresh-guest proof and strict external accounting.
No commit until review.

## Exact decisions

1. Host `run(..., scenario='normal')` accepts enum only normal/hold. CLI adds
   `--scenario normal|hold`, default normal. No arbitrary guest command/options.
   Generate fresh lowercase32hex run_id with os.urandom before launch; record it.
   Fixed QEMU append gains `pb053_run=RUN_ID pb053_scenario=SCENARIO` alongside
   existing guest flag. Values generated/validated, no shell interpolation.
2. Guest after PID1 guard parses exactly one run token and scenario token; invalid,
   duplicate or missing tokens fail. Read `/proc/sys/kernel/random/boot_id`, validate
   canonical UUID string and retain. Final guest marker exact top fields add
   run_id,boot_id,scenario to current schema1. These identify fresh guest/run,
   not nRF silicon or calibrated clock. Existing actual phases/assertions unchanged.
3. Shared validate_guest now requires expected_run_id and expected_scenario from
   host caller, compares them exactly; boot UUID valid. Host parse_result accepts
   those arguments explicitly. Pure fixtures adapt with authored values; reject
   stale run ID/wrong scenario/invalid boot ID. Never accept producer-picked ID
   as independent freshness anchor. Existing phase/content/capture checks remain.
4. Normal scenario unchanged. Hold scenario after first daemon/adapter/ISO readiness
   and before public child prints/flushes exactly one `PB053_HOLD_READY ` JSON with
   run_id,boot_id,actor PID map (dbus,monitor,emulator,bluez-fresh1), then sleeps.
   Owned children stay alive; host signal/deadline must stop QEMU. Hold cannot
   emit successful final guest ledger or claim unexecuted public cases.
5. Host run always fails when run_owned reports cancellation/timeout, even if QEMU
   itself exits0 or any forged complete marker exists. Preserve full process record,
   capped raw log and anchored snapshots in run-record; hold readiness may be
   retained separately as diagnostic. Missing final marker is expected consequence
   but never accepted normal-run evidence. Existing namespace isolation unchanged.

## Tests and actual runs

Native tests cover token validation, stale nonce/scenario/boot IDs, encoded hold
marker shape and inability of hold/owned-failed result to pass normal validator.
No fake VM acceptance. Existing process owner10 tests cover real group/signal
boundary; this phase adds actual private QEMU proof below after review.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_results -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_process -p 'test_*.py' -v
git diff --check
```

Implement and return tests first, no VM. Delegator then authorizes prepare/runs:
one normal run, two hold runs cancelled externally with SIGINT/SIGTERM after live
READY, and one hold deadline run30s. Each exclusive external run keeps record;
all controls must prove QEMU/group no longer live, failed owned result with exact
signal/deadline and prior evidence intact. Required lane tests will assert these
expected failures as real public outcomes, not skip execution.

Missing detail/unexplained warning/two failed attempts: stop with exact evidence.
No increased normal timeout, fake cleanup, arbitrary RPC, golden-output changes
or new hardware operations.
