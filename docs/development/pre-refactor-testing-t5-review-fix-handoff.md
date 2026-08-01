# Phase T5 review-fix handoff — preserve deferred timing measurements

## Review finding

T5 remains open after `102fc12`. `src/audio_timing_nrf54.c` has one shared
`pending_diag` mailbox. Each GRTC callback overwrites it before calling
`k_work_submit()`. Zephyr may coalesce submissions while the work item is
pending/running, so a delayed system workqueue can silently discard one or more
one-second feedforward measurements. Current timing tests explicitly dispatch
work after each callback and do not exercise backlog. This contradicts
`audio_drift.h` and T5's contract that every non-stale measurement reaches the
drift feedforward path.

## Required design

Replace the single mailbox with a fixed, allocation-free FIFO protected by the
existing `diag_lock`.

- Capacity: 16 `diag_payload` entries.
- ISR producer appends measurement and schedule-error payloads in order.
- Work handler drains all queued payloads in FIFO order in one invocation.
- Each payload retains generation; stale generations are discarded
  independently.
- Logging cadence remains based on each payload's sequence.
- One work submission may represent many queued payloads; submission
  coalescing must not lose payloads.
- Reset increments generation but does not rewrite queued old-generation
  payloads; deferred work rejects them as stale. New-session payloads may follow
  old payloads in FIFO and still deliver.
- `AUDIO_TIMING_NRF54_TEST` state reset clears FIFO and overflow state.
- No dynamic allocation and no logging from ISR.

Queue overflow must never silently lose timing evidence:

1. If producer finds FIFO full, set an atomic/test-observable overflow fault,
   clear `active`, and submit work.
2. Work handler emits one `LOG_ERR` for that overflow and clears the pending
   overflow report. Do not resume measurement until normal session reset and a
   new anchor.
3. The callback that detects overflow may drop that payload because operation
   has transitioned to an explicit fault; this is not normal feedforward loss.
4. Later callbacks while inactive do nothing.

Use small private enqueue/pop helpers. Keep ISR critical section bounded. Do
not alter timing math, scheduling, HAL setup, controller APIs, or hardware
resource ownership.

## Required tests

Expand `tests/unit/timing_nrf54/` using production source and existing mocks:

1. Fire at least ten measurement callbacks without dispatching captured work;
   run captured work once; assert all ten exact ppm values arrive in FIFO order.
2. Queue old-generation measurements, reset, start a new session, queue a fresh
   measurement, then dispatch once; assert old payloads are rejected and fresh
   payload is delivered.
3. Fill all 16 entries, then produce one more without dispatch; assert active
   clears, overflow fault is observable, one work dispatch drains accepted
   entries in order, and later callbacks remain inactive.
4. Existing schedule-error, first-delta, wrap, reset, and log-pacing tests stay
   green.

Tests may add a guarded read of overflow fault state if mocks cannot observe
the log. Do not add a general production API.

## Validation and evidence

Run focused timing tests first. Then create a completed code commit and run on
that exact commit:

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Update `STATUS.md`, behavior contract, and coverage matrix with corrected timing
test count, FIFO/backlog/overflow semantics, exact final validated code commit,
and results. Commit docs separately. Leave both repositories clean; remove
temporary refs/worktrees/bundles/logs. No push, merge, PR, amend, force-push, or
hardware.

## Escalation

After two materially different failed attempts, stop. Do not weaken the
every-measurement contract, dispatch work after each callback to avoid backlog,
silently overwrite/drop payloads, suppress warnings, or invent dynamic ISR
allocation. Return exact blocker, attempts, logs/diff/status, one precise
question, and smallest hypothesis. Do not commit knowingly failing work.
