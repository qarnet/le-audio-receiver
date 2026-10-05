# PB-053 actor lifetime repair (2026-10-05)

Scope: `scripts/bluez_guest_init.py`, `tests/unit/bluez_host_guest/test_bluez_host_guest.py`, and this handoff only. No guest preparation, VM run, corpus/image edit, PB-051 execution, stage, commit, or push.

## Failure boundary

After final readiness, last daemon, emulator, monitor, or private bus can exit with status zero before final cleanup. Existing final cleanup skips TERM if `poll()` already reports an exit, then accepts zero. A `stopped` event only records exit status; it cannot prove actor remained live until owned stop.

## Repair

Factor `stop_owned_actor(label, proc)` for actual subprocess owners. Reject any pre-TERM exit, including zero, with label and code. Otherwise TERM and wait five seconds; on timeout KILL and wait five seconds, then fail with forced-kill error. Reject nonzero terminal exit. Reuse for phase daemon stops before existing private bus `NameHasOwner` check and phase event; reuse for final cleanup only on actors outside `stopped`. Keep cleanup failure recording, fallback kill/wait, remaining-actor cleanup, log validation and retained original stopped exit record. Unexpected early exit emits `cleanup_failure` and fails final result; no stage schema or timeout change. Already explicitly stopped fresh1 and retained daemons receive no extra stop.

## Public-boundary verification

Launch real ordinary child processes, not guest PID1: already-exited zero and nonzero are rejected; live child with TERM handler writes retained marker and exits zero; phase-stop actor is not stopped twice. Ensure child processes reaped and `ResourceWarning` treated as error. Synthetic successful guest result with `cleanup_failure` must fail host parser. Run `python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v`, equivalent `bluez_host_results` and `bluez_guest_limits` suites, and `git diff --check`. These component tests do not prove VM acceptance.
