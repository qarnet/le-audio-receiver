# Stage 4B review fixes

## Recovery state machine corrections

- Make `g_recovery_scheduled` authoritative under `g_lock`:
  - scheduling decision sets it true exactly once;
  - recovery work keeps it true while running and across delayed retry;
  - only terminal ACTIVE/FALLBACK/STOPPED/cancel paths clear it;
  - heartbeat while RECOVERING increments dedup and never schedules another;
  - handle work-schedule API failure by clearing flag and counting error.
- Replace ad-hoc scheduling sites with one helper following this contract.
- On short coordinated-reset success, retain generated epoch and commit it to
  `g_stream_epoch` under lock at ACTIVE transition.
- On runtime path, do not write `g_stream_epoch` outside lock; carry post-restart
  ring epoch local and commit only at guarded ACTIVE transition.
- Idle heartbeat restart: after runtime restart success call
  `flpr_ring_mgr_remote_restarted()` so stale CPU semaphores/epoch cannot enter
  next stream. Record failure and leave unavailable if reinit fails.
- Simplify health switch dead branch; active/preparing transition once,
  recovering dedups, stopped idle-restarts, fallback remains unavailable.

## Tests

Add mocked Stage 4B tests:

1. healthy short reset → ACTIVE with exact new epoch, no runtime restart;
2. short reset timeout → one runtime restart → ring reinit → reset → ACTIVE;
3. heartbeat during queued/running recovery does not duplicate work/restart;
4. runtime/reinit/post-reset failures preserve one scheduled retry and bounded
   attempts/backoff/exhaustion;
5. stop/start while restart blocks cannot publish stale ACTIVE/epoch;
6. idle restart drains/reinitializes ring manager;
7. fallback block advances cpu ASRC, resumed FLPR state matches host reference;
8. exact counters and schedule-failure behavior.

Run all offload/runtime/ring/protocol tests and both builds. No hardware in this
fix commit. Correct Stage4B result status to PARTIAL until omitted Mode B and
production gates run. Commit new fix; no amend/security/WDT/direct register/
HPF/BabbleSim/push/mass erase/install.
