# PB-053: real pytest host-lane inventory and execution

## Goal and scope

Create real pytest suite over fixed private guest CLI, with independent collection
and native JUnit for existing PB-052 accountant. Files: new
`tests/host_bluez/test_host_lane.py`, `tests/host_bluez/conftest.py`,
`tests/host_bluez/inventory.json`, PB-053 notes. No runner/protocol changes or VM
until review; no SDK/vendor/host hardware/PB-051 changes. This external lane is
explicit, not silently skipped by canonical unit gate. Unavailable runtime fails
readiness, never pytest.skip.

## Verified prerequisites and contract

Installed pytest8.4.2 was verified at
`/nix/store/zd0i1b1admc06z39vgpml46wp56lmrjb-python3.13-pytest-8.4.2`.
R26 normal guest passed18cases/48frames; actual SIGINT,SIGTERM and30s hold timeout
failed CLI as intended, QEMU/groups gone, distinct run/boot IDs. No new rights or
installation required. Existing current runner and prepared image remain trusted
caller inputs, supplied expected manifest digest separately.

## Exact required pytest cases

All top-level functions in `tests/host_bluez/test_host_lane.py`, JUnit classname
`tests.host_bluez.test_host_lane`:

1. `test_public_phase[fresh1]`
2. `test_public_phase[retained]`
3. `test_public_phase[fresh2]`
4. `test_daemon_restart_state_and_disappearance`
5. `test_delivery_content_accounting`
6. `test_signal_cancellation[SIGINT]`
7. `test_signal_cancellation[SIGTERM]`
8. `test_timeout_owned_vm`
9. `test_fresh_guest_repetition`

Inventory schema1: suite=`pb053-isolated-bluez-host`, profile=`pytest-xunit2-v1`,
producer={name:pytest,version:8.4.2}, above9 exact [classname,name] tuples,
exclusions=[], prerequisites=[prepared-runtime,kvm-qemu,pinned-producer]. Rationale:
local explicit runtime, actual guest tests, no physical/codec claims. Do not add
or rename cases to fit observed reports. Return actual inventory digest for
Delegator review; future wrapper hardcodes reviewed digest rather than self-pins.

## Fixture behavior

Environment required: PB053_SUITE_ROOT (existing exclusive new evidence root),
PB053_PREPARED_ROOT, PB053_MANIFEST_SHA256. Absence fails tests/collection,
not skip. No credentials/host bus or arbitrary guest commands. All subprocess
argv lists use sys.executable and repository scripts/bluez_host_guest.py.
Children output roots are NEW siblings of suite root named
SUITE_BASENAME-normal, -sigint, -sigterm, -timeout, -repeat. Do not nest inside
preserved PB053 run/prepared directories or reuse roots. Check all before runs.

Session normal fixture executes fixed normal CLI --timeout240 and caller digest
once, retains stdout/stderr and actual run-record. Each phase case validates its
actual nested public result and required case/content checks. Restart test checks
three actual distinct daemon PIDs, bus disappearance, retained/fresh states and
state preservation/reset. Delivery test verifies all48 actual reported frame bytes
against independently loaded pinned fixture and raw observed ISO counts.

Signal parametrized cases launch real host CLI hold with --timeout60. Wait<=25s
for nonce-matched HOLD_READY in capped live serial, then signal HOST RUNNER PID
only; never guest PID. Await<=15s. Require failed CLI/owned process, exact signal,
no timeout/cleanup errors, no accepted guest result, and no live owned QEMU/group.
Deadline case uses hold30s, requires actual ready, timed_out=true, no cancellation
signal, failed CLI/no live QEMU. Fresh repetition launches NEW normal CLI, validates
same required behavior and independently generated different run_id and boot_id.
All prior source/record/logs immutable. Fixture finally terminates only owned host
runner if a test fails; its signal handler cleans detached QEMU. No kill by name.

## Independent collection

conftest.py implements pytest_collection_finish: enumerate ACTUAL session.items,
record nodeids and [classname,name] IDs into exclusive suite-root/collection.json.
For these module-level tests derive classname from collected path minus .py with
slashes replaced dots, name from rest after `::`; no copied inventory list.
Require every item belongs exact selected module, no unexpected class/wrapper.
Collection file opened x; plugin errors fail collection. Record pytest version.
No collector rereads policy to construct observed universe.

## Static verification first

Use real `python3 -m pytest --collect-only -q tests/host_bluez/test_host_lane.py`
with PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 and a NEW external suite root; preparation
env may reference currentr26 without VM since fixtures not executed in collection.
Require exactly9 actual IDs. Return collection.json, native collection stdout,
inventory SHA256 and IDs to Delegator. Do not execute full VM suite yet; wrapper
handoff follows review. Run git diff --check. No commit yet.

Stop missing API/detail, unexpected collection/version/case, warnings or two
failed attempts. Do not synthesize passing JUnit, skip prerequisites, installer,
change acceptance bounds or import GPL functional-test implementation.
