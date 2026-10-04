# PB-053: bounded host process owner, resumption phase 1

## Goal and scope

Create and verify safe reusable host process ownership before another VM run.
Files: new `scripts/bluez_host_process.py`, new
`tests/unit/bluez_host_process/test_bluez_host_process.py`, and PB-053 execution
notes through Backlog.md. No guest code/runner integration in this phase.
Existing reviewed preparation tool stays unchanged. No host Bluetooth/kernel,
downloads, SDK/vendor, PB-051, hardware or unrelated dirty-file changes.

Completed analysis: current `bluez_host_guest.py:567-600` detaches QEMU but has
no SIGTERM handler, only kills group when leader still alive, captures unbounded
serial log and can lose record on cleanup exceptions. Existing preparation
`run_logged` proves scoped group cleanup but does not provide bounded output or
structured cancellation results. Do not copy its exception-driven signal shape
into the new owner; use state-setting handlers and bounded polling.

## Exact public API

`run_owned(argv, log_path, timeout, max_log_bytes=33554432)` returns a dict.
It is stdlib-only, Linux process-group ownership, main-thread-only. Validate
nonempty string argv, positive finite numeric timeout (bool invalid), positive
integer log cap (bool invalid), and invocation in main thread before spawning.
Log path must not exist or be symlink; parent must exist. Open exclusively.

Returned fields: `schema_version=1`, `argv`, `pid`, `start_time`, `end_time`,
`returncode`, `ok`, `timed_out`, `cancelled_signal`, `log_limit_exceeded`,
`bytes_logged`, `log_sha256`, `cleanup_errors`, `error`,
`descendant_cleanup_required`. Nullable pid/returncode on spawn failure.
Success only for exit0, no timeout/cancellation/limit/error/cleanup failure and
no surviving descendant group after leader exits normally. Caller can retain
failure record instead of losing it through an exception.

## Implementation sequence

1. Save/install scoped SIGINT/SIGTERM handlers in main thread before spawn.
   Handler records first signal number only; never raises. Poll notices signal
   within50ms. Restore original handlers after all cleanup and log sealing.
2. `Popen(start_new_session=True, stdin=DEVNULL, stdout=PIPE, stderr=STDOUT)`.
   Mark stdout nonblocking; selectors or select polls at most50ms. Use monotonic
   deadline. Read at most65536 bytes per operation, stream to exclusive log and
   SHA256. No communicate/read-all or unbounded accumulated byte buffer.
3. On cancellation, timeout or cap exceed, stop normal pump and proceed cleanup.
   Do not write more than cap. If incoming chunk exceeds remaining cap, write
   remaining bytes, mark limit exceeded and fail; bounded retained evidence is
   explicitly incomplete, never an accepted full report. At exact cap, check
   subsequent available bytes/EOF before deciding success.
4. On leader normal exit, drain currently available bytes, then always inspect
   owned process group. Any still-existing group requires cleanup and sets
   descendant_cleanup_required=true (normal success must not leave descendants).
   Do not let descendants keep stdout open past leader exit/deadline.
5. Finally TERM only own group, bounded2s grace; then KILL if group persists,
   bounded2s leader wait. Ignore only ProcessLookupError. Close pipe and reap
   direct child. Cleanup actions independent: append errors and continue. Repeated
   SIGINT/SIGTERM during cleanup only update state, never interrupt finally.
   Never kill by process name or touch arbitrary groups. Zombies may be adopted
   by system init; tests check no live descendant, not misleading reap claim.
6. After cleanup, drain remaining pipe bytes nonblock with same cap before close,
   seal hash/size, set final outcome and restore handlers. Spawn/output failure
   returns structured failed outcome once log opened; invalid parameters before
   any operation raise ValueError. Preserve exact errors, no warning suppression.

## Regression tests (real OS boundary)

Use authored Python subprocesses, temp files and deterministic ready handshakes.
No QEMU/Bluetooth, no mocks replacing process boundary, no skips.

- Normal child produces known bytes and exits0: exact log/hash and success.
- Child exits nonzero: same retained bytes, failed outcome/returncode.
- Child plus real sleeping descendant hits short timeout: both not live afterward,
  failed timed_out result. A leader exiting0 with live descendant must also clean
  descendant and fail via descendant_cleanup_required.
- Helper harness prints ready only after its owned child has printed PID into
  log. Send SIGTERM and SIGINT in separate subtests to harness, which serializes
  returned record and exits nonzero. Require owned child/descendant not live,
  exact cancelled_signal, retained record and bounded completion.
- Noisy child exceeds small cap: log never larger than cap, failure flag/hash
  match retained bytes, child stopped. Exact-cap normal output succeeds.
- Existing/symlink log and invalid arguments refused without changing sentinel.
- Original signal handlers restored after normal and failed operations.

## Verification and commit

```sh
python3 -m unittest discover -s tests/unit/bluez_host_process -p 'test_*.py' -v
git diff --check
```

Do not commit until tests pass and recap returned for Delegator review. After
review a focused commit handoff will authorize only new module/tests and notes;
full canonical/hosted verification accompanies integration. Do not stage existing
unaccepted guest scaffolding just because this helper passes. Stop missing design
detail, unexplained failure/warning or two differing failed attempts; return exact
question/evidence rather than invent architecture or weaken tests.
