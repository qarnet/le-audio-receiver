# PB-053 SEQPACKET revocation repair (2026-10-05)

Scope: `scripts/bluez_guest_public.py`, `scripts/bluez_host_results.py`, their focused host guest/results tests, and this document. No prepare, VM, PB-051 execution, stage, commit, or push.

## Failure boundary

An empty AF_UNIX `SOCK_SEQPACKET` message yields zero received bytes while peer can remain live. Existing `wait_revoked` treats zero bytes under `POLLIN` alone as EOF, and host validator accepts `eof: true` without `POLLHUP`. Neither proves peer revocation.

## Repair

For `POLLIN`, reject unread nonempty data and `MSG_TRUNC` as before. Zero-byte receive requires independently observed `POLLHUP` in same poll flags or raises `Empty ISO packet without peer hangup at revocation`. Only then return `eof: true`, `pollhup: true` with original flags. `POLLHUP`-only remains `eof: false`, `pollhup: true`; `POLLERR`-only still fails. Validator requires exact true `pollhup` and `POLLHUP` bit, exact bool `eof`, additional `POLLIN` bit when `eof` is true, and no `POLLNVAL`. No inventory, process, timeout, or schema changes.

## Public-boundary verification

Real socketpair sends empty SEQPACKET while still connected: reject it and prove subsequent nonempty packet arrives. Peer closure accepts real hangup; queued nonempty input still fails; no-data times out. Synthetic accepted result uses `POLLIN | POLLHUP`, with negative mutations for absent/false hangup, missing `POLLIN` for EOF and `POLLNVAL`. Run ResourceWarning-as-error unit suites for `bluez_host_guest`, `bluez_host_results`, `bluez_guest_limits`, and `git diff --check`. Component tests are not full VM acceptance.
