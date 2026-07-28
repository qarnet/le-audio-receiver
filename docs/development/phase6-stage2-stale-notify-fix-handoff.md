# Stage 2 stale consumer-notification recovery fix

## Root cause

`flpr_ring_mgr_reset()` resets ring indices but leaves `consume_sem` tokens.
`FLPR_MSG_RING_CONSUMER` carries only consumed count, not epoch. After recovery,
a stale token wakes submit against empty reset output ring, producing `-ENOENT`.
Probation treats this as relapse and exhausts all five retries.

## Exact fix

- Change `FLPR_MSG_RING_CONSUMER.data` to current nonzero ring epoch. Consumer
  count is not needed for wake semantics; retain diagnostic block count in seq.
- FLPR reads epoch from initialized ring header after successful consume. Send
  notification only with that epoch.
- CPU callback locks `ring_lock`, compares message epoch to
  `ring_stream_epoch`, increments new `diag_stale_notify` and does not give
  `consume_sem` on mismatch. Matching epoch preserves existing sem/counter flow.
- In CPU `flpr_ring_mgr_reset()`, after shared-ring epoch reset and before
  publishing new `ring_stream_epoch`, drain `consume_sem` with `K_NO_WAIT`.
  Track drained-token count in new diagnostic. Never block under spinlock.
- Expose stale/drained notification counters in ring status and shell output.
- Protocol version bump only if message semantic versioning requires it under
  current project convention; update both images together. Do not change struct
  size or message ID.

## Tests

- Matching-epoch notification wakes consumer.
- Old-epoch notification after reset does not wake and increments stale count.
- Preloaded consume token is drained by reset.
- New-epoch notification after reset still wakes.
- Existing ring/offload/protocol tests pass.

## Hardware gate

SC-only normal BlueZ connection. Mode A 120 s. Inject timed consumer stall 60
ms after success>500. Require timeout/fallback, recovery attempt, no post-reset
`-ENOENT`, ACTIVE, probation clear after 100 successes, exhaustion zero, 100
fps, zero audio faults.

Build both targets, run tests, update Stage 2 result, commit. No policy/security
changes, raw preconnect, mass erase, push, or unrelated work.
