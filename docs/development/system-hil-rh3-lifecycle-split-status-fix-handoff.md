# RH3 lifecycle-split receiver status repair handoff

Status: host-only repair after immutable direct diagnostic
`rh3-20260822-03-modea-critical-tail-snapshot`. No hardware action belongs in
this phase.

## Goal

Split receiver evidence by real stream lifecycle instead of requiring every
synchronous shell query to finish during a short source tail:

1. capture and validate active FLPR offload after source reaches `streaming`;
2. capture CIS-dependent ISO link quality at `scored_complete` while CISes are
   still live;
3. capture audio/performance, stopped offload counters, and FLPR handshake
   after receiver stream summaries during teardown;
4. validate active-to-stopped FLPR lifecycle strictly, including progress and
   accumulated fault counters.

This removes a host false negative without treating post-teardown `STOPPED` as
an active state, weakening any warning/fault rule, changing firmware, or
explaining persistent Mode A loss.

## Grounding

The current host-only critical-tail repair passed `146/146` fake-lab tests,
but its one immutable physical diagnostic still failed:

```text
run ID:     rh3-20260822-03-modea-critical-tail-snapshot
evidence:   /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot/
result:     failed, receiver tail
detail:     invalid receiver status: offload state='STOPPED'
checksum:   23/23 SHA-256 entries passed
```

Retained source and receiver clocks align at stream start: source `streaming`
at `20902 ms` corresponds to receiver stream-1 start at `00:27:06.819`. On
that alignment, source emitted `scored_complete` at `160477 ms`, roughly
receiver `00:29:26.394`, then source `teardown` at `164544 ms`, roughly
receiver `00:29:30.461`. This is only about `4.067 s` of usable tail.

The first synchronous `bt iso quality` command produced two valid active CIS
records. Before the second command could execute, receiver logs showed stream
disable and `audio_offload_stream_stop()`:

```text
Stream[0] handle=0x0001 ... crc_error=2 rx_unreceived=14322 duplicate=0
Stream[1] handle=0x0006 ... crc_error=0 rx_unreceived=14248 duplicate=1
...
Disable: stream ...
audio_offload: offload stream stop: gen=3
State       : STOPPED / epoch=0 gen=3
Counters    : submit=14377 success=14377 fallback=0 busy=0
```

`bt iso quality` is necessarily synchronous HCI work. `src/bt_bap.c` reads one
controller ISO-link-quality response for each active sink CIS through
`bt_hci_cmd_send_sync()`. Ordering a cheap `flpr offload` shell request second
does not make it execute before teardown. The prior physical run proves this;
do not retry its ID.

At normal close, `src/bt_bap.c::teardown_close_path()` drains sink work before
`audio_offload_stream_stop()`. `src/audio_offload.c::audio_offload_stream_stop()`
sets state `STOPPED`, epoch `0`, and generation forward while preserving
per-stream submit/success/fault counters. A post-stop snapshot is therefore
valuable lifecycle evidence, but cannot satisfy the existing active-state
validator.

Source `streaming` happens after both sink ASEs are connected. Receiver starts
offload at the audio-path gate, asynchronously prepares it, and resets
per-stream counters when it reaches `ACTIVE`. `audio_offload.c` may make up to
five preparation attempts with bounded backoff and a coordinated-reset call.
Use an early bounded polling capture, not a fixed sleep. The 120-second scored
interval leaves this capture outside the tail deadline.

## Scope

### In scope

1. Split host receiver evidence collection across streaming, tail, and
   post-stop lifecycle phases.
2. Add strict pure validation for active-to-stopped offload lifecycle.
3. Update fake-lab tests and capture-runner lifecycle probe for new collection
   boundaries.
4. Correct current HIL contributor docs that falsely call RH3-03 FLPR evidence
   a live tail snapshot and that still describe all status commands as tail
   commands.

### Out of scope

- source or receiver firmware, FLPR firmware, source-tail duration/frame
  count, source image identity, shell grammar, Kconfig, devicetree, controller,
  RF, power, pairing, audio, I2S, PLC, or warning-policy changes;
- accepting post-stop `STOPPED` as an active snapshot, accepting static/multiple
  pending FLPR work, lowering error/fault requirements, or adding ISO-loss
  thresholds;
- hardware, HIL execution, source/receiver build, flash, reset, serial,
  Bluetooth, OpenOCD, probe, RF, or power action;
- edits to `STATUS.md`, immutable evidence, release files, or unrelated dirty
  files;
- commit, push, merge, PR, tag, or release.

## Exact implementation

### 1. Phase-specific runner collection

In `scripts/hil/runner.py`:

1. Replace `LIVE_TAIL_COMMANDS` with exactly:

   ```python
   LIVE_TAIL_COMMANDS = ("bt iso quality",)
   POST_STOP_COMMANDS = (
       "audio status",
       "audio perf",
       "flpr offload",
       "flpr status",
   )
   ```

   Update nearby comments to say ISO is tail-only because synchronous HCI
   quality reads consume the available tail window. Do not combine shell
   commands or bypass prompt termination.

2. Add bounded active-offload capture constants beside existing offload settle
   constants:

   ```python
   RECEIVER_ACTIVE_OFFLOAD_TIMEOUT = 45.0
   RECEIVER_ACTIVE_OFFLOAD_POLL_INTERVAL = 0.2
   ```

   The long bound is intentional: preparation can perform a coordinated reset
   with a `5000 ms` bound and up to five retry/backoff attempts. It occurs
   during the 120-second scored phase, not during tail. Keep the existing
   `0.5 s` settle bound unchanged.

3. Add private `_collect_receiver_active_offload(receiver_console, row)`.
   It owns only `flpr offload` prompt-bounded requests and writes readable
   `receiver-active-status.txt` on success or failure. For each attempt append
   a labeled complete transcript block. Poll until:

   - state is exactly `ACTIVE`; and
   - for `48_4_1`, parsed `submit >= 1` before calling existing
     `_settle_receiver_offload()`; for `48_3_1`, `ACTIVE` is sufficient.

   Then settle immediately using existing logic and return exactly:

   ```python
   {
       "offload": final_offload,
       "offload_settle": settle_evidence,
   }
   ```

   A `PREPARING`, `FALLBACK`, `STOPPED`, missing, or zero-work 10-ms snapshot
   does not qualify. On deadline, fail
   `HilRunnerError("receiver active", ...)` with clear active-offload evidence.
   Preserve cancellation checks and raw console capture. Do not use a fixed
   delay or issue audio/ISO/handshake commands here.

4. In `_step_run_row()` `collect_streaming` callback, retain existing source
   active-status validation first. For healthy rows, call the new active
   offload capture next and store it with that segment's source snapshot under
   `source_active[N]["receiver_offload"]`.

   For named fault rows, keep `_run_fault_window()` exactly where it is after
   source active validation. Store its validated `window_final` as the segment
   active offload snapshot with `offload_settle` absent or `None`; do not run a
   second generic active-capture poll before injecting a fault. The existing
   recovery window is already the active baseline/recovery evidence.

5. Keep `_collect_receiver_tail()` prompt-bounded and evidence-writing, but
   let it collect and validate only ISO link quality. Its returned receiver
   object is exactly:

   ```python
   {"iso_link_quality": parsed_quality}
   ```

   Remove tail audio, offload, settle, and handshake queries. ISO grammar,
   stream-count/slot validation, error boundary, and `receiver-status.txt`
   evidence remain strict and unchanged.

6. Add `_collect_receiver_post_stop(receiver_console, row, active_offload,
   recovery)`. Call it only after `_step_session_end()` has collected every
   stream summary for that segment. It executes `POST_STOP_COMMANDS` in exact
   order, writes `receiver-post-stop-status.txt`, parses concatenated audio
   faults, final offload, and handshake, then invokes new pure lifecycle
   validation described below. On error raise
   `HilRunnerError("receiver post-stop", ...)`. Return:

   ```python
   {
       "audio_faults": faults,
       "offload": post_stop_offload,
       "handshake": handshake,
   }
   ```

   Add this object to the existing segment result under
   `receiver_streams[N]["post_stop"]`. Do not replace `streams` or `last`.

7. Use a segment-indexed active-offload map inside `_step_run_row()`, not
   `[-1]`, so reconnect rows associate each post-stop snapshot with its own
   streaming segment. Keep schema version unchanged because fields are
   additive.

### 2. Strict lifecycle validation

In `scripts/hil/receiver.py`, preserve public
`validate_receiver_blocks()` behavior for existing callers. Factor private
audio, active-offload, and handshake checks as needed, then add this pure
function:

```python
def validate_receiver_lifecycle_blocks(
    audio_faults,
    active_offload,
    post_stop_offload,
    handshake,
    profile="48_4_1",
    recovery=None,
    allow_moving_single_pending=False,
):
    ...
```

It returns error strings using existing missing/malformed conventions. It must
perform all existing audio, active-offload, and handshake checks from
`validate_receiver_blocks()`, against `active_offload` for active-state
requirements, plus these post-stop rules:

1. `post_stop_offload["state"]` must be exactly `"STOPPED"`.
2. Final `submit` and `success` must be nonnegative integers and exactly equal.
3. For healthy `48_4_1`, final `submit >= 1` and final `success` must be
   strictly greater than active `success`. This proves real FLPR work occurred
   after the active snapshot rather than accepting startup-only state.
4. For `48_3_1`, retain existing zero submit/success/fallback/busy contract;
   no offload-progress requirement applies.
5. For healthy rows, final fallback, busy, recovery-attempt/failure/relapse/
   exhaustion, probation-active, and all existing fault categories must be
   present and zero. Do not lose any current strict check.
6. For named recovery rows, first retain existing active recovery validation.
   Final state must still be `STOPPED`, submit/success must be equal and not
   regress, and terminal recovery/fallback/fault counters that recovery
   validation observes must equal their active recovered values. A new late
   fault, reset, or counter regression fails.
7. Do not require exact final epoch/generation. Normal stream stop deliberately
   resets epoch and advances generation.

Keep `allow_moving_single_pending` limited to an active snapshot with existing
proof from `_settle_receiver_offload()`. It never permits a final mismatch.

### 3. Fake-lab coverage

Update `tests/hil/rh2_test.py` and only supporting fake helpers when needed:

1. Make full receiver wires represent actual phase order:
   active `flpr offload`; tail `bt iso quality`; stream summaries; post-stop
   `audio status`, `audio perf`, `flpr offload`, `flpr status`.
   Give normal fake data an active `ACTIVE` 10-ms snapshot with completed work
   and a later `STOPPED` snapshot with greater equal submit/success values.
2. Preserve active settle regressions. Their retry must occur in
   `receiver-active-status.txt` before tail ISO, not in tail evidence.
3. Add one normal Mode A public-boundary test proving structured evidence has:
   - active `source_active[0].receiver_offload.offload.state == "ACTIVE"`;
   - tail ISO records for both slots;
   - `receiver_streams[0].post_stop.offload.state == "STOPPED"`;
   - final equal success greater than active success;
   - command/evidence order matching lifecycle phases.
4. Add lifecycle-validator coverage for a healthy active-to-stopped transition,
   then prove final `ACTIVE`, no final progress, and nonzero final fallback each
   fail. Keep existing active validator tests unchanged.
5. Update fault-wire ordering and fault-row tests so recovery-window active
   evidence remains valid and post-stop terminal counters are checked. Do not
   weaken any hang/stall warning-window test.
6. Preserve existing receiver-tail ISO failure regression: it must still fail
   before post-stop collection and retain partial `receiver-status.txt`.

Update `tests/hil/capture_runner_test.py` timeline probe for added active and
post-stop hooks. Assert capture still starts before source START, active
receiver capture happens after source streaming hook, tail precedes summary,
and post-stop follows summary. This is lifecycle behavior, not private-call
count testing.

### 4. Documentation truth

Update only:

- `docs/development/system-hil-milestones.md`;
- `docs/development/system-hil-resume-state.md`;
- `docs/development/system-hil-rh3-software-status.md`.

Correct RH3-03 wording: it retained a live ISO snapshot but a **post-teardown
STOPPED** FLPR snapshot. Do not describe it as a live FLPR tail snapshot.
State next physical action is blocked on this host-only lifecycle-split repair
and review. Update milestone lifecycle wording to distinguish active offload,
tail ISO, and post-stop diagnostics. Preserve every immutable evidence value,
no-acceptance language, and no-retry rule.

## Verification

Run sequentially from repository root. Host-only commands only; do not run
beside a source build:

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/runner.py \
  scripts/hil/receiver.py \
  tests/hil/rh2_test.py \
  tests/hil/capture_runner_test.py
git diff --check
```

No hardware, HIL runner, build, flash, serial, Bluetooth, RF, source build,
commit, push, merge, PR, tag, or release action.

## Executor rules

Implement only this handoff. Preserve all unrelated dirty work. Do not commit.
Stop and report instead of guessing if this needs a source-tail change, a
firmware/shell change, a warning exception, a relaxed fault rule, a schema
version change, or concurrent UART use.

Return changed files, exact lifecycle behavior, added/updated regression names,
every verification result, final `git status --short`, confirmation of no
hardware/no build/no commit, deviations, and blockers.
