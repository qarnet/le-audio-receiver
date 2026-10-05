# PB-053: authenticated local execution and PB-052 result accounting

## Goal and files

Add real external-lane owner, prerequisite execution, independent collection and
actual pytest xunit2 report passed to existing accountant. Files: new
`scripts/bluez_host_lane.py`, new
`tests/unit/bluez_host_lane/test_bluez_host_lane.py`, host runner check/preflight
refactor and guest tests, PB-053 notes. Existing nine test IDs/inventory bytes
unchanged. No protocol, VM options, SDK/vendor/host Bluetooth, PB-051, installer
or unrelated edits. No full VM suite or commit until review.

## Verified policy anchor

Reviewed inventory path `tests/host_bluez/inventory.json`, SHA256
`1de02a5083330284600ac740ab21a1f35ff76e5411377beb029ea251d324f276`.
Static actual pytest8.4.2 collection matched all9 required IDs; conftest writes
observed universe from session.items, never copies policy required list.
PB-052 contract at docs/testing/external-test-result-accounting.md specifies
exact inventory/run fields and normal exit requirements. Use existing CLI, don't
reimplement or invent BlueZ-tester summary. Real pytest emits native JUnit.

## Exact design

1. Factor existing host run readonly preflight into callable
   `check_prepared(prepared,manifest_sha256)`; it performs all current source,
   artifact regularity/caps/hash/kernel/config/QEMU checks but creates no output
   or child/VM. run reuses it unchanged. CLI adds `check --prepared ...
   --manifest-sha256 ...`, returns JSON success/profile. Preserve all rejection
   and snapshot/run semantics. Source hash changes require new prepare afterward.
2. New lane CLI required args --prepared,--manifest-sha256,--output. It reads
   fixed repo inventory and verifies HARDCODED reviewed digest above before any
   execution. Output new exclusive external directory, protected against prepared,
   repo/home/store/vendor/build and old evidence; never overwrite. No arbitrary
   pytest selection/command/root RPC option. Child run roots are known fresh
   siblings, not nested inside prior immutable records.
3. Run three actual prerequisites with run_owned, normal integer exit0 required:
   prepared-runtime = host `check` CLI; kvm-qemu = small fixed readonly helper
   checking existing QEMU version11.0.2 and current-user /dev/kvm read/write;
   pinned-producer = `python -c 'import pytest; ...8.4.2...'`. Capture each argv,
   output/hash/process outcome. No mutation or download. Failed prereq produces
   sealed failed suite record, never starts pytest/skip or accepted accounting.
4. Run fixed actual argv from repo cwd:
   `python -m pytest --runxfail -q --junitxml=OUTPUT/report.xml
   -o junit_family=xunit2 tests/host_bluez/test_host_lane.py`.
   Set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 and only explicit PB053_SUITE_ROOT,
   PB053_PREPARED_ROOT, PB053_MANIFEST_SHA256 env values plus ordinary trusted
   environment. Process owner now supports env; needs optional cwd to select repo
   explicitly, defaultNone preserves existing callers. Add exact env/cwd real
   process tests rather than mock Popen wiring.
5. Bound pytest owner1500s (three normal240s bounds, three hold bounds and metadata
   overhead), stdout8MiB. Partial/no JUnit, interrupted/spawned/signaled pytest,
   owner cleanup failure or test failure cannot pass even if XML looks successful.
   Native JUnit is retained original bytes, not reconstructed summary.
6. Load bounded conftest collection.json, require exact pytest_version8.4.2,
   no duplicate IDs and actual discovered universe equals reviewed policy. Keep
   raw collection and SHA. Do not compute observed IDs from inventory or report.
   Require expected9 JUnit IDs through existing accountant, not only counts.
7. Build PB-052 run record exact schema1 fields using actual process times/argv/cwd,
   unique caller run ID, reviewed inventory digest, observed collection and actual
   report bytes/SHA. Prerequisites exact declared three. Child termination exited
   only if real owner success/normal exit. Output record no unknown schema fields.
   Invoke existing check-external-test-results.py with fixed inventory/path and
   HARDCODED digest, actual record/report. Retain stdout/stderr/exit. Accounting
   acceptance and real execution prerequisites both required for suite success.
8. Final suite-record.json separates runtime/image/source hashes, producer/module
   versions, paths, process/collection/report/accountant identities, expected
   controls and verdict. Existing raw child runs remain external/immutable. No
   codec/physical RF/I2S/analog/full Bluetooth conformance claim.
9. After completed real run, negative accounting controls make NEW copies only:
   remove one required JUnit testcase and reconcile XML totals, recompute report
   hash in copied record, require accountant rejects missing case; add skipped
   required testcase state, require rejection; copied child exit nonzero and
   copied collection shrink each reject. Original accepted report unchanged.
   These encoded accounting controls prove no skip/shrink can turn lane green.

## Verification first, no VM yet

Add native public CLI prerequisite-failure tests: existing output sentinel
preserved, invalid expected manifest/hash/absent runtime fails before pytest;
wrong inventory fixture cannot replace reviewed anchor (fixed path remains).
Existing process tests add actual cwd selection output. No fake VM acceptance.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_lane -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_process -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
git diff --check
```

Return source/tests/diff. No full VM suite/commit until review. Stop missing API,
unexplained warning or two failed attempts with exact logs/question. No inventory
renaming, optional prereqs, fabricated JUnit, source-download or acceptance waiver.
