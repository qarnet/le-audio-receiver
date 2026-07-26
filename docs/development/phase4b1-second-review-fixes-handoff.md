# Phase 4b.1 Second Review Fixes

Status: implemented (this commit)

## Required fixes

1. **Presentation target arithmetic order**
   - Compute `uint32_t target_us = sdu_ts_us + pd_us` first, preserving 32-bit
     ISO wrap.
   - Expand `target_us`, not raw SDU timestamp, to future 64-bit GRTC time.
   - Do not add `pd_us` after 64-bit expansion.
   - This matches Nordic sample `iso_rx.c` followed by
     `timed_led_toggle.c`. Raw received SDU timestamp may be behind current GRTC;
     expanding it first can incorrectly schedule about 71.6 minutes ahead.
   - Add production-helper tests covering:
     - raw SDU timestamp behind `now`, but timestamp + presentation delay ahead;
     - 32-bit addition wrap near `UINT32_MAX`;
     - stale target handling/fallback behavior used by caller.

2. **No ISR logging**
   - Remove `LOG_ERR` from GRTC compare handler.
   - Carry schedule error code, generation, and explicit payload kind to deferred
     work. Work handler emits one error if generation still current.
   - Do not encode error state as `elapsed_us == 0` and then silently return.

3. **Close work/reset race**
   - Protect payload publication and consumption with a Zephyr spinlock or
     equivalent safe snapshot mechanism.
   - Work handler copies payload under lock, then validates generation and
     active/error semantics from copied payload before logging.
   - Reset marks inactive, increments generation, disables compare, and clears
     pending payload under synchronization. No old-session diagnostic may log
     after reset returns.
   - Avoid blocking work drain if reset can run from context where that is unsafe;
     generation + synchronized payload invalidation is acceptable.

4. **Session diagnostic state**
   - Move diagnostic sequence into timing state; reset it on each session.
   - Payload carries sequence; work never reads mutable sequence from global
     state.

5. **Docs/clean tree**
   - Correct original Phase 4b.1 handoff with arithmetic order and final
     lifecycle behavior.
   - Mark review handoffs implemented after verification.
   - Include currently untracked
     `docs/development/phase4b1-review-fixes-handoff.md` in fix commit.
   - Leave no untracked build/test output.

## Verification

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/timing -d /tmp/le-audio-timing-test --pristine
/tmp/le-audio-timing-test/timing/zephyr/zephyr.exe
git diff --check
git status --short
```

No warnings ignored. No hardware, PI integration, RADIO access, push, merge,
PR, amend, force operation, or attribution. Commit scoped fixes and return exact
results/hash.
