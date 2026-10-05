# PB-053 run-lifetime cancellation repair (2026-10-05)

Scope: `scripts/bluez_host_guest.py`, `tests/unit/bluez_host_guest/test_bluez_host_guest.py`, and this handoff. No guest preparation, VM run, fixture/image edit, PB-051 execution, stage, commit, or push.

## Failure boundary

Host `run()` owns output and evidence before and after `run_owned()` owns VM process. Outside `run_owned()`, default SIGTERM can end host without `run-record.json`. A cancellation while copying input or validating result must not yield an accepted record. `run_owned()` retains its process-group cancellation and cleanup ownership.

## Repair

Use main-thread-only `RunCancellation` around post-output run lifetime: save and restore SIGINT/SIGTERM handlers; latch first signal without raising asynchronously; `check()` raises cancellation `ValueError`. Extend `snapshot` with optional keyword `cancel_check`, called before opening, around every bounded chunk, and after close. Check around each run snapshot, before VM launch, after owned VM return, around diagnostic parsing, and before success. Transfer `run_owned()`'s `cancelled_signal` into first-signal latch before checking; retain hold readiness and other available diagnostics even on owned-process cancellation.

Publish `cancelled_signal` and `cancellation` on final record, never infer cancellation from timeout. Seal failure before and after first record write; if signal arrives during publication, rewrite at most once as failure while scoped handlers still protect publication. Restore handlers only after seal. Preserve original error on unrelated failures, and retain owned VM cleanup boundary and existing output/manifest pins.

## Verification

Real OS signal delivery under scoped handler and real temporary input files prove pre-copy and mid-chunk cancellation, first-signal retention and handler restoration. Harness verifies post-processing cancellation refuses success and publishes failure record; repeated signals do not interrupt publication. Parser/CLI behavior stays compatible. Run ResourceWarning-as-error unittest discovery for `bluez_host_guest`, `bluez_host_process`, `bluez_host_results`, then `git diff --check`. Component harness is not VM acceptance.
