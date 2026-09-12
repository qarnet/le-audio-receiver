# RH3 critical tail snapshot sequencing fix handoff

Status: host-only repair after immutable direct diagnostic
`rh3-20260822-02-modea-iso-parser-fix`. No hardware action belongs in this
phase.

## Goal

Capture both CIS-dependent ISO link quality and live FLPR offload evidence
before normal source teardown can stop the receiver offload plane. Keep every
existing strict validator and evidence field. This phase repairs a host-side
false-negative order, not the persistent Mode A loss.

## Grounding

`scripts/hil/source_client.py::start()` invokes the receiver tail hook as soon
as the source emits `scored_complete`, then resumes source-record consumption
and normal teardown only after that hook returns. Source firmware emits
`scored_complete` at tail entry before tail SDUs, and
`HIL_SOURCE_TAIL_MIN_US` remains `5000000U` in
`hil/source/core/hil_source_types.h`.

The retained immutable evidence at
`/tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix/` proves current
command sequencing loses the active offload snapshot:

1. `receiver-status.txt` lines 75-78 contain an exact active-CIS
   `bt iso quality` result before disable logs.
2. Disable begins at line 79. `audio status` begins at line 92, after both
   stream summaries.
3. `flpr offload` runs only after `audio status` and `audio perf`, at lines
   130-144, and reports `STOPPED / epoch=0 gen=3` despite
   `submit=success=14375` and zero fallback, fault, and recovery counters.
4. Result and JUnit fail only at receiver tail with
   `invalid receiver status: offload state='STOPPED'`; cleanup is empty and
   evidence integrity passes `23/23` SHA-256 entries.

Current `LIVE_TAIL_COMMANDS` orders `bt iso quality`, `audio status`,
`audio perf`, `flpr offload`, then `flpr status`. Current
`_collect_receiver_tail()` delays `_settle_receiver_offload()` until after all
five commands. The prior parser repair proves ISO must remain first. Therefore
the smallest grounded repair is ISO first, FLPR offload second, and immediate
settlement before any noncritical tail command.

This does not weaken healthy `48_4_1` validation. `validate_receiver_blocks()`
moving single-pending behavior; zero fallback, busy, fault, and recovery
counters; zero audio fault counters; and healthy FLPR handshake. Nonzero ISO
link-quality counters remain retained evidence, not a new threshold.

## Scope

### In scope

1. Reorder host live-tail commands in `scripts/hil/runner.py`.
2. Invoke existing offload-settle logic immediately after its initial offload
   snapshot.
3. Update fake receiver transcript order and public-boundary regressions in
   `tests/hil/rh2_test.py`.
4. Add no result, resume-state, or status-document claim in this host-only
   phase. This handoff is the only documentation addition.

### Out of scope

- source or receiver firmware, FLPR firmware, shell grammar, source tail
  duration, source image hashes, Kconfig, devicetree, Bluetooth controller,
  audio, PLC, I2S, pairing, RF, power, or warning policy changes;
- relaxing offload state/counter validation, ISO counter thresholds, lifecycle
  checks, parser grammar, or source cleanup rules;
- hardware, build, flash, reset, serial, Bluetooth, OpenOCD, probe, RF, or
  HIL execution;
- edits to `STATUS.md`, existing immutable evidence, release files, or
  unrelated dirty files;
- commit, push, merge, PR, tag, or release.

## Exact implementation

### 1. Critical command order

In `scripts/hil/runner.py`, set the complete `LIVE_TAIL_COMMANDS` tuple to:

```python
LIVE_TAIL_COMMANDS = (
    "bt iso quality",
    "flpr offload",
    "audio status",
    "audio perf",
    "flpr status",
)
```

Keep `bt iso quality` first. Do not combine commands, write commands without
prompt boundaries, add a new shell command, or change any timeout.

### 2. Settle at capture point

In `Runner._collect_receiver_tail()`:

1. Initialize storage for one `offload_settle` result before the command loop.
2. When command equals `"flpr offload"`, parse its transcript exactly as now,
   then call `_settle_receiver_offload(receiver_console, row, recovery,
   offload, blocks)` immediately. Store both returned final offload status and
   settle evidence before continuing to `audio status`.
3. Remove the current one post-loop call to `_settle_receiver_offload()`.
4. Preserve each command transcript and every settle-retry transcript in
   `receiver-status.txt`, in exact execution order. A one-pending retry must
   therefore appear directly after the initial `# flpr offload` block and
   before `# audio status`.
5. Preserve the current returned `offload` and `offload_settle` fields and
   current `allow_moving_single_pending` rule. If future constant edits omit
   `flpr offload`, fail closed at receiver-tail with explicit missing offload
   snapshot evidence rather than using an uninitialized settle result.
6. Preserve named fault-row behavior. The existing recovery bypass remains
   owned by `_settle_receiver_offload()` and no fault command or recovery
   window moves.

Do not reuse post-stop `STOPPED` status as a healthy active snapshot. Do not
add a grace wait, extend source tail duration, or use concurrent console
commands.

### 3. Fake-lab coverage

In `tests/hil/rh2_test.py`:

1. Update `_receiver_passing_wire()` expected shell writes and chunks to match
   the new tuple. Its first tail writes must be `bt iso quality`, then
   `flpr offload`; any settle retry follows immediately; only then come audio
   status/perf and FLPR handshake.
2. Update `_receiver_recovery_wire()` expected writes and `write_responses` to
   use the same tail order after its existing recovery window. Keep baseline,
   fault command, and recovery snapshot ordering unchanged.
3. Strengthen
   `test_mode_a_retains_two_iso_link_quality_snapshots_with_nonzero_evidence`:
   assert `bt iso quality` precedes `flpr offload`, and `flpr offload`
   precedes `audio status`, both in writes and `receiver-status.txt`.
4. Update
   `test_healthy_10ms_tail_settles_one_live_flpr_submit` to assert exact tail
   write order:

   ```text
   bt iso quality
   flpr offload
   flpr offload
   audio status
   audio perf
   flpr status
   ```

   The second offload command is existing settle retry. Assert its evidence
   block appears before `# audio status`, and retain existing assertions for
   equality result and final counters.
5. Keep static pending failure and moving-single-pending regressions. Update
   their sequence assertions only as required by new order. They must still
   prove bounded retries and no acceptance of static mismatch.
6. No fake firmware grammar change. Existing valid offload and ISO helper
   transcripts remain source of shell payloads.

## Verification

Run sequentially from repository root. These are host-only commands. Do not
run them beside a source build.

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/runner.py \
  tests/hil/rh2_test.py
git diff --check
```

Success means all fake-lab tests pass, Python compilation succeeds, whitespace
check is clean, and only intended host test/runner changes plus this handoff
appear beyond pre-existing dirty work.

## Executor rules

Implement only this handoff. Preserve unrelated dirty work. Do not run HIL or
other hardware actions, build, flash, serial tooling, or commit. Stop and
report instead of guessing if implementation appears to require a source-tail
change, validator relaxation, new shell grammar, concurrent UART use, or an
unexplained warning exemption.

Return files changed, exact behavior, regression names, every verification
command/result, final `git status --short`, confirmation of no hardware/no
commit, deviations, and blockers.
