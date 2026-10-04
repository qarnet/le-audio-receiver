# PB-053: safe preparation checkpoint hardening and blocked lane evidence

## Goal and scope

Finish independently verified local emulator preparation, not failing guest
lifecycle lane. Commit/push authorization will come only after Delegator review
and clean candidate gates. Touch `scripts/bluez_host_prepare.py`,
`tests/unit/bluez_host_prepare/test_bluez_host_prepare.py`, new
`docs/development/pb-053-preparation-results-20261004.md`, and PB-053 notes/status
through Backlog.md. Do not edit/stage guest scripts/tests/handoffs in this phase.
They remain unaccepted diagnostic scaffolding. Preserve other dirt and evidence.

## Completed analysis and disposition

Macro repair built pinned emulator warning-free at r2. Guest diagnostic work
subsequently established real isolated endpoint registration, LE discovery,
pairing and16 exact120-byte ISO frames at r15. R16 and R17 both fail mandatory
retained-state cleanup: public Disconnect completes but new/persistent LE
connections keep both Device1 Connected true through10s. Two-sided policy
cleanup did not repair r17. Retained daemon also emits MGMT AddDevice Failed03;
cause is unresolved in available installed kernel/daemon/controller combination.
No timeout, state or acceptance checks were relaxed.

Owner explicitly allows deferring too-complicated/unimplemented items as Blocked.
Record PB-053 Blocked with exact r16/r17 remaining behavior and unaccepted guest
tool review defects, rather than claim completion or ship unsafe failing runner.
Guest scaffolding has identified signal/ownership/result-validation defects and
must remain unstaged. Further source-matched host-stack diagnosis and runtime
refinement are needed before resuming. This is separate from codec/physical
acceptance and PB-051 owner pause. Do not label software-model/native CI results
as accepted isolated host lifecycle lane.

## Exact preparation hardening

1. Extend output check to reject home descendants and `/nix/store` descendants,
   output equal/within any preserved `/tmp/opencode/pb053-*` directory, repository
   and vendor source. Desired new sibling r3 output under `/tmp/opencode` remains
   valid. Resolve paths and reject existing/symlink output before mutation.
2. Replace direct subprocess.run compiler/link calls with helper
   `run_logged(argv, log_path, timeout)` using Popen(start_new_session=True),
   stdout/stderr to exclusive log, stdin DEVNULL and bounded wait. Install scoped
   SIGINT/SIGTERM handlers in main thread during owned operation to raise a
   cancellation exception. Finally restore handlers, TERM own group, wait<=3s,
   KILL group if still alive and wait<=3s. Tolerate ProcessLookupError; always
   clean own group even if leader exited leaving descendants. Never signal other
   groups or kill by name. Preserve cancellation/timeout logs and raise failure.
3. Add compiler identity: resolved executable path/hash and retained `--version`
   output, timeout10s. Record actual argv plus flags. Add `-MD -MF NN.d -MT
   PB053_DEP` to each compile, retaining full user/system header dependencies.
   Parse dependency file after successful compile: require prefix `PB053_DEP:`;
   join backslash-newline continuations and use shlex.split on remainder. Require
   all dependencies regular readable files; record size/SHA256, no unapproved
   sources copied. Source remains pinned and clean before and after build.
4. Retain build-record on every output-created failure, with explicit outcome
   success/failed, error/cancel reason, completed commands, source pin/hashes,
   compiler identity, header dependency hashes and on success binary size/hash
   plus version5.87. Existing finally record must not mask original failure.
   Do not mark success before link, version and vendor cleanliness all pass.
5. Add tests through real compiler/process/file boundaries. Existing macro
   regression stays. New tests prove forbidden/existing output untouched; real
   compiler dependency output includes authored header and changes its recorded
   hash after authored edit; owned helper timeout kills actual spawned sleeper
   descendant; external SIGTERM of helper harness kills owned child and exits
   nonzero. Use ordinary Python processes and temp files, no root/QEMU/Bluetooth.
   Do not fake source pin as feature acceptance. Tests must be bounded and fail,
   not skip, if prerequisite unavailable.
6. Retry actual full pinned local build into new exclusive
   `/tmp/opencode/pb053-emulator-build-r3` (unused suffix if needed). Retain all
   23 compiler logs/header depfiles, link/version logs and build record. Require
   no compiler warnings. Hash binary and record, verify vendor clean.
7. Results doc distinguishes preparation PASS, r15 real valid transport checkpoint,
   r16/r17 mandatory retained lifecycle FAIL, remaining safety/accounting review
   issues in unstaged diagnostic guest code, no PB-053 acceptance, no decoder/
   physical/analog/release claims. Reference raw external logs/hashes, never
   rewrite earlier evidence. PB-053 notes/status via CLI reflect this disposition.

## Verification

```sh
python3 -m unittest discover -s tests/unit/bluez_host_prepare -p 'test_*.py' -v
python3 scripts/bluez_host_prepare.py --source /tmp/opencode/bluetooth-test-resources-20261003/bluez --output /tmp/opencode/pb053-emulator-build-r3
git -C /tmp/opencode/bluetooth-test-resources-20261003/bluez status --porcelain
git diff --check
```

Return implementation, exact tests/logs/hashes, blocker note and status. Do not
commit/push yet. Stop missing decision or unexplained warning/failure; no unsafe
runner staging, no accepted partial item, no hidden waiver or retries by guessing.
