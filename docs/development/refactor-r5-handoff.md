# R5 handoff — offload transaction decomposition

Start commit: `0b29b14` (R4 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before implementation; the implementation must match it and the
results document (`docs/development/refactor-r5-results.md`) must record
what actually happened.

## Goal / invariant

Decompose `audio_offload_process_asrc()` (nRF54L15 FLPR ASRC submit path,
`src/audio_offload.c`) into private stage helpers WITHOUT changing any
counter, transition, 8 ms deadline, lock/recheck ordering, fallback, errno,
recovery scheduling, output mutation, probation, RTT/cycle accounting,
protocol, or the nRF5340 `-ENOSYS` stub.  No 360-frame support.

No physical file split: helpers stay `static` in `src/audio_offload.c`.

## Exact production shape

### One transaction/capture struct

```c
struct asrc_txn {
	/* Caller args. */
	const int16_t *input;
	uint16_t input_frames;
	uint32_t sequence;
	int32_t correction_ppm;
	const struct audio_asrc_state *pre_state;
	int16_t *output;
	uint16_t output_capacity;
	struct audio_offload_asrc_result *result;

	/* Lifecycle captured at pre-check; revalidated after every
	 * blocking operation (mutex wait, ring roundtrip). */
	enum audio_offload_state captured_state;
	uint32_t captured_generation;
	uint32_t captured_epoch;

	/* Submit-mutex ownership — exactly one unlock per acquired path. */
	bool owns_submit_lock;
};
```

No caller output/result copy happens before the success commit; the struct
carries pointers only.

### Stage helpers (all `static`)

1. **`asrc_validate_args()`** — current null/zero-arg checks, exact 480
   frame check, `output_capacity >= FLPR_RING_PAYLOAD_CAPACITY_FRAMES`
   check.  Returns `-EINVAL`.  No counters touched.

2. **`asrc_precheck()`** — under `g_lock`: uninitialized or STOPPED →
   `-EAGAIN` with no counters; capture state/generation/epoch; increment
   `g_asrc_stats.submit_count` + `g_status.submit_count`; non-ACTIVE →
   `g_asrc_stats.fallback_count++` + `g_status.fallback_count++` and
   `-EAGAIN` (no `last_error` written).  ACTIVE → 0.

3. **`asrc_acquire_submit()`** — `k_mutex_lock(&g_submit_lock,
   K_MSEC(OFFLOAD_DEADLINE_MS)) != 0` → busy/stale finalize (below) with
   `-EBUSY`, `status_cat = &g_status.busy_count`, `owns_mutex = false`;
   sets `owns_submit_lock = true` on success.

4. **`asrc_postmutex_recheck()`** — under `g_lock`: `g_state != ACTIVE` →
   the special non-recovery epilogue (asrc fallback++, status fallback++,
   `last_error = -EAGAIN`, `last_error_seq = sequence`, unlock, release
   submit mutex, `-EAGAIN`) — **not** routed through the finalizer because
   its lifecycle is always "changed" and its epilogue differs from every
   finalizer branch (no stale count, no `record_fault`, no recovery);
   otherwise re-capture state/generation/epoch.

5. **`asrc_ring_roundtrip()`** — produce (`OFFLOAD_EXPECTED_FRAMES`),
   notify, wait, lifecycle recheck, consume, lifecycle recheck, in the
   exact current order, with result mappings preserved:
   `FLPR_PRODUCE_FULL → -ENOSPC`, produce other `→ -EIO`, notify `< 0 →
   notify_ret` (exact errno), wait `!= 0 → -ETIMEDOUT`, consume
   `FLPR_CONSUME_EMPTY → -ENOENT`, `FLPR_CONSUME_STALE → -ESTALE`, consume
   other `→ -EIO`.  The two pre/post-consume lifecycle rechecks use the
   shared recheck helper (stale-only epilogue, never a fault).

6. **`asrc_validate_metadata()`** — exact current ordering: FLPR negative
   status transport (`status < 0 && frames == 0 → status` as errno),
   frame range `1..481 → -EFAULT` (+frame_fault_count), nonzero status
   `→ -EFAULT`, exact flags `→ -EFAULT`, sequence `→ -EFAULT`
   (+seq_fault_count), ppm echo `→ -EFAULT`, reserved bytes `→ -EFAULT`
   (+asrc state_fault_count), post-state import `→ -EFAULT`
   (+asrc state_fault_count), `step_base` match `→ -EFAULT`
   (+asrc state_fault_count).

7. **`asrc_shadow_verify()`** — `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY`-only:
   the exact current shadow body (import pre-state, `audio_asrc_process`,
   compare return/frame count/samples/post-state).  Any mismatch routes to
   the shared finalizer with `-EFAULT` and `asrc_cat =
   &g_asrc_stats.verify_fault_count`.

8. **`asrc_lifecycle_ok_locked()`** — called with `g_lock` held; wraps the
   existing `lifecycle_check_before_fault()` plus the ASRC fallback
   increment on mismatch (exact `ASRC_LIFECYCLE_CHECK` macro semantics:
   status stale++/fallback++, asrc fallback++, `last_error = -ESTALE`).
   Returns true when lifecycle unchanged.

9. **`asrc_recheck_lifecycle()`** — acquires `g_lock`, runs
   `asrc_lifecycle_ok_locked()`, releases, and when stale releases the
   submit mutex exactly once if owned.  Used for the two pure rechecks
   (a stale recheck must not `record_fault` or schedule recovery).

10. **ONE shared finalizer `asrc_fault_finalize()`** — used by every
    recovery-eligible post-lock transaction fault and the busy path.
    Signature: `(struct asrc_txn *t, int error, uint32_t *status_cat,
    uint32_t *asrc_cat, bool owns_mutex)`.  Acquires `g_lock`:
    - lifecycle mismatch → `asrc_lifecycle_ok_locked()` accounting,
      state preserved, no `record_fault`, no recovery;
    - otherwise → `g_asrc_stats.fallback_count++`, optional `asrc_cat`
      increment, `record_fault(status_cat, error, t->sequence)` ONCE,
      `recovery_try_schedule_unlock()` ONCE (releases `g_lock` and
      schedules when RECOVERING);
    - releases the submit mutex exactly once when `owns_mutex`.
    Always returns `-EAGAIN` (the caller-visible errno for every
    post-lock transaction fault).

11. **`asrc_commit()`** — the single success linearization point under
    `g_lock`: final lifecycle guard (stale → exact commit-stale epilogue:
    stale++/asrc fallback++/status fallback++/`-ESTALE`, no
    `record_fault`, no recovery, release mutex, `-EAGAIN`); then exact
    success accounting: asrc + status `success_count++`,
    `last_error = 0`, per-stream ASRC RTT min/max/sum/count,
    processing-cycles min/max/sum/count, probation success tracking and
    100-success clear (exact statement order incl. the cleared LOG_INF),
    and `record_latency()` for generic RTT.  No blocking under the
    spinlock.

12. **`asrc_copy_output()`** — caller `output`/`result` copy only after
    `asrc_commit()` succeeded; explicit field assignment as today; then
    release the submit mutex and return 0.  Faults leave output/result
    byte-identical.

### Test hooks (CONFIG_ZTEST only, GCOVR_EXCL, excluded from numeric coverage)

Stage-boundary callback so unit tests can inject a lifecycle change
(`audio_offload_stream_stop()`) deterministically at exact points without
thread races:

```c
enum asrc_test_stage { ASRC_TEST_STAGE_MUTEX_ACQUIRED,
                       ASRC_TEST_STAGE_MUTEX_TIMEOUT,
                       ASRC_TEST_STAGE_BEFORE_COMMIT };
void (*asrc_test_stage_hook)(enum asrc_test_stage, void *);
void *asrc_test_stage_user_data;
```

Call sites (guarded by `#if defined(CONFIG_ZTEST)`):
- `ASRC_TEST_STAGE_MUTEX_ACQUIRED` — in `asrc_acquire_submit()` after lock
  success, before `asrc_postmutex_recheck()` (post-mutex non-ACTIVE test);
- `ASRC_TEST_STAGE_MUTEX_TIMEOUT` — in `asrc_acquire_submit()` on lock
  timeout, before the finalizer (mutex-timeout stale test);
- `ASRC_TEST_STAGE_BEFORE_COMMIT` — in the public function before
  `asrc_commit()` (final commit stale race test).

Plus a CONFIG_ZTEST-scoped accessor for the single-recovery-schedule proof:

```c
bool audio_offload_test_is_recovery_scheduled(void); /* GCOVR_EXCL */
```

The three stage hooks are the ONLY new production-file test seams; they
are compiled out of production builds and GCOVR-excluded.

## Tests before/alongside extraction

1. **Replace placeholder `test_asrc_busy`** (audio_offload suite) with
   honest second-thread contention: a helper thread locks `g_submit_lock`
   (already exposed via `audio_offload_test_helpers.h`), signals a "locked"
   semaphore, waits on a "release" semaphore, then unlocks; the main thread
   calls `audio_offload_process_asrc()` and times out after the 8 ms
   deadline.  Assert: `ret == -EAGAIN`; submit +1 / fallback +1 in BOTH
   generic and asrc stats; `busy_count` +1; `last_error == -EBUSY` and
   `last_error_seq`; state RECOVERING / healthy false;
   `audio_offload_test_is_recovery_scheduled()` true; output untouched;
   one recovery run after → ACTIVE, recovery_attempts +1, flag cleared.
   No recursive same-thread mutex use.

2. **Deterministic hook tests** (audio_offload suite):
   - `test_asrc_post_mutex_not_active` — hook `stop_stream_hook` at
     MUTEX_ACQUIRED; assert fallback counts, `last_error == -EAGAIN`, no
     stale count, state preserved STOPPED, no recovery, output untouched.
   - `test_asrc_mutex_timeout_lifecycle_changed` — holder thread + hook at
     MUTEX_TIMEOUT; assert stale accounting (stale+1, fallback+1 both),
     `last_error == -ESTALE`, busy_count NOT incremented, state STOPPED
     (no RECOVERING), no recovery schedule, output untouched.
   - `test_asrc_commit_stale_race` — full successful roundtrip with hook
     at BEFORE_COMMIT; assert `ret == -EAGAIN`, stale+1, success NOT
     counted, `last_error == -ESTALE`, no RECOVERING, no recovery,
     output/result untouched.

3. **Table-driven fault snapshots** — one table-driven test covering every
   recovery-eligible post-lock fault stage with exact before/after
   `audio_offload_status` + `audio_offload_asrc_stats` category deltas +
   `last_error` + `last_error_seq` + state/healthy + recovery-scheduled +
   output untouched: produce FULL, produce other, notify errno, wait
   timeout, consume EMPTY, consume STALE, consume other, FLPR error
   transport (`status < 0`), frame range, status nonzero, flags wrong,
   seq mismatch, correction mismatch, reserved nonzero, post-state import
   fail, step_base mismatch.  Recovery between rows to restore ACTIVE.

4. **Missing category assertions** — `verify_asrc_category` state_fault
   deltas on the reserved/post-state-import/step_base paths; RTT
   min/max/sum/count across several successes with varied mock
   `rtt_cycles`; exact `last_error`/`last_error_seq` classes per fault.

5. **Ret-level `offload_asrc` suite additions** — `test_produce_other`,
   `test_consume_other`, `test_processing_status_positive` (each:
   `ret == -EAGAIN`, output/result untouched).

## Verify-enabled suite — `tests/unit/offload_asrc_verify`

New exec-only suite compiling PRODUCTION `src/audio_offload.c` +
`src/audio_asrc.c` + `src/flpr_ring.c` with
`CONFIG_SOC_NRF54L15=1`, `CONFIG_AUDIO_OFFLOAD_ASRC=1`,
`CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=1`.

Mock: reuse/copy the offload_asrc mock shape, but `consume_asrc_result`
computes the EXACT real CPU ASRC output + exported post-state by running
`audio_asrc_process()`/`audio_asrc_state_export()` on the captured produce
input/pre-state/ppm (identical to what the shadow computes), then applies
controlled corruptions:
- exact match (default);
- sample corruption (flip one scratch sample after compute);
- frame-count override (produced ± 1, still in 1..481 → shadow compare);
- post-state phase / prev_l / prev_valid corruption (shadow compare);
- sequence corruption (metadata boundary → seq_fault_count);
- out-of-range ppm (request 5000, echo 5000, compute with 0 → shadow
  `audio_asrc_process` returns -EINVAL → verify fault);
- post-state import failure (step_base = 0 → metadata state fault before
  shadow — the REACHABLE cpuapp/post-state import failure);
- post-state reserved / step_base-mismatch (metadata state faults).

Tests: exact-match success (output copied, stats success+1, verify 0);
each corruption → `ret == -EAGAIN`, exact `verify_fault_count` /
`state_fault_count` / `seq_fault_count` deltas, `last_error == -EFAULT`,
RECOVERING + healthy false, ONE shared-finalizer recovery schedule
(`audio_offload_test_is_recovery_scheduled()`), output/result untouched;
RTT/cycles stats across successes.

**Shadow pre-state import branch:** preserved in code exactly as today,
but structurally unreachable in the unit tests because metadata validation
runs first and requires `cr.post_state.step_base == pre_state->step_base`
with `cr.post_state.step_base != 0`, and cpuapp exports a valid
pre_state — documented as a coverage exclusion, never fabricated.

CRC corruption note: payload CRC is validated inside the real
`flpr_ring_mgr_consume_asrc_result()` (transport layer) and never reaches
`audio_offload_process_asrc()`; the verify suite covers the
process_asrc-layer validation boundaries (seq/frame/status/flags/ppm/
state/shadow) and documents CRC as transport-owned.

## Matrix / inventory

- `tests/test-matrix.json`: add `offload_asrc_verify` as a direct suite
  for `src/audio_offload.c`; add outcome witnesses from the verify suite;
  exec-only inventory 4 → 5; canonical gate 47 → 48.
- `scripts/test-all.sh` / `scripts/test-coverage.sh` /
  `scripts/check-test-matrix.py` need no code changes (shared inventory
  auto-discovers the new suite).
- Coverage: no physical split; verify-enabled build pulls the shadow code
  into merged `audio_offload.c` totals.  Population stays 29 (no new
  population file).  If per-file/aggregate ratios pass the committed
  baseline, do NOT rewrite it merely for increased totals (the runner
  compares ratios + population + tool versions).  Only if a ratio
  regresses or population shifts must a candidate be written with
  `--write-baseline` on the clean implementation commit, inspected, and
  committed separately with old/new provenance in
  `docs/testing/coverage-matrix.md`.  Never weaken thresholds/exclusions.

## Verification

- Focused: `audio_offload`, `offload_asrc`, `offload_asrc_verify`
  (plus `flpr_ring_mgr` only if mocks/API touched), matrix checker,
  report-only coverage, warning/diff inspection.
- G1: `./scripts/test-all.sh` → expected **48 PASS**; full coverage +
  matrix + BSim pins unchanged; `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle`; `check-build-contract.py` → 76/76 expected;
  warnings classified under AGENTS.md.
- Hardware (nRF54L15 only; planned non-destructive flash):
  1. `fw-build-54l15 -- -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y`; confirm
     resolved app config shows `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y` and
     zero new/actionable warnings.
  2. Flash verify-enabled image (probe via `nrf-probes`); capture serial
     before reset; central per AGENTS (`btattach` hci0, peer
     `DB:A6:0C:05:A2:AA`); Mode A + Mode B 120 s via
     `scripts/bap_central.py`; pass = stream summaries + offload counters
     show valid use, zero verify/state/seq/frame/crc faults, no decode/
     I2S errors.
  3. `flpr hang` gate Mode A and Mode B (accepted bounded duration);
     capture evidence/checks.
  4. Restore production verify-off build and flash; confirm resolved
     config off and clean boot/status.  No mass erase/recovery.
  Preserve unique `/tmp/r5-*` evidence manifest + hashes and report raw
  probe identity evidence.

## Docs / commits

- Handoff (this document).
- Tests + implementation commit.
- Coverage migration commit ONLY if required (see above).
- `docs/development/refactor-r5-results.md` — return/counter invariant
  table, helper ownership, exact gate/build/hardware evidence, coverage
  numbers, deviations.
- Update `docs/development/refactor-plan.md` (R5 COMPLETE),
  `STATUS.md`, `docs/testing/coverage-matrix.md`, README inventory
  claims if active.
- No push / PR / amend / force / attribution.  Commits: docs handoff →
  tests+implementation → (coverage migration if required) → docs
  acceptance.

## Escalate only on

Two failed approaches, decomposition cannot reproduce semantics, test
requires weakening, unexplained coverage regression/warning, BSim pin
mismatch, hardware unavailable after non-destructive diagnosis,
destructive recovery needed, or scope/architecture expansion.
