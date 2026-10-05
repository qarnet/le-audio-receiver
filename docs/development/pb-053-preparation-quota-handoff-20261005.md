# PB-053 preparation quota repair (2026-10-05)

Scope: `scripts/bluez_host_guest.py`, `tests/unit/bluez_host_guest/test_bluez_host_guest.py`, this document. No preparation or VM run, stage, commit, push, PB-051, SDK/vendor or corpus edit.

Payload remains bounded at 4 GiB. New operational limits: 100000 closure and separately 100000 final-stage entries, 4096 roots, 4096 UTF-8 bytes per path/link target, 32 MiB cumulative path/link metadata; paths.list 32 MiB, manifest 64 MiB, uncompressed cpio 5 GiB, gzip 2 GiB, cpio stderr 64 KiB; nix query stdout 16 MiB/stderr 64 KiB. Nix timeout 120 seconds and cpio timeout 300 seconds unchanged.

Charge entries before retaining metadata, share closure budget across roots, reject excess roots before additions, and enumerate final stage with non-following `os.walk` before sorting bounded paths. Bounded exclusive outputs fail before writing overflow chunks. Stream manifest JSON, paths.list, gzip and cpio. Query and cpio run under host-private owned process group with separate bounded streams, fixed argv, TERM/KILL/reap and signal latch. Reject stderr, timeout, cancellation, nonzero exit and cap overflow; keep partial exclusive evidence on failure. Preserve existing source/stimulus pins, guest environment exception and console exclusion.

Component acceptance: small real trees and authored subprocesses prove overflows, valid underbound content, cancellation, timeout and group cleanup. Run ResourceWarning-as-error unittest suites for `bluez_host_guest`, `bluez_host_process`, `bluez_host_results`, `bluez_guest_limits`, plus `git diff --check`. This is not prepared-image or VM acceptance.
