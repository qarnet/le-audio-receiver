# Refactor R1 handoff — ownership and concurrency hardening

## Goal

Eliminate real cross-thread races before structural file splits:

1. shell `audio stop` versus BT RX `audio_sink_push()`;
2. shell/Bluetooth lifecycle gate transitions;
3. nRF54 timing anchor versus reset;
4. undocumented but intentional FLPR semaphore/epoch ordering.

Start from clean R0 evidence commit `f654b58`.  Accepted R0 implementation
commit is `53d42db`; accepted plan is
`docs/development/refactor-plan.md`.

R1 changes ownership/synchronization only.  PCM output, BSim hashes/counts,
offload protocol/recovery policy, I2S prefill/ownership, callback responses,
shell output, and hardware behavior remain unchanged except one deliberate
public sink rule: pushes are rejected while stream admission is closed.

## Grounding evidence

- Current resolved receiver configurations use `CONFIG_BT_RECV_WORKQ_BT=y`;
  `audio_sink_push()`, ISO/ASCS stream callbacks, and connection teardown run
  through Bluetooth receive processing on BT RX workqueue thread.  `cmd_stop()`
  runs on separate shell thread and currently calls `audio_sink_stop()`
  directly.
- NCS v3.3.0 host source confirms these application callbacks are thread
  context, not ISR.  Therefore
  `k_mutex`/`k_condvar` are legal.  They must be held only around state changes;
  never around allocation, decode, offload, I2S, or Bluetooth calls.
- Current stop resets `saved_frame`, rate/ASRC state, and `offload_sequence`
  while push can mutate them.  DROP may race a just-queued DMA block.
- `audio_offload_process_asrc()` may wait up to bounded offload deadlines.
  Stop must wait for every admitted push; it must never timeout and proceed to
  DROP against a still-running push.
- `stream_lifecycle.c` is pure logic but its calls become cross-thread once
  shell routes through `bt_bap`.  Protect calls in `bt_bap.c`; do not add Zephyr
  dependencies to `stream_lifecycle.c`.
- nRF54 `audio_timing_sdu_ref_update()` and `audio_timing_reset()` are both
  thread-context APIs and currently mutate anchor/CC state without common
  serialization.  GRTC callback is ISR context; generation remains its stale
  payload guard.

## Exact scope

### Production

- `src/audio_sink.h`
- `src/audio_i2s.c`
- `src/bt_bap.c`, `src/bt_bap.h`
- `src/audio_shell.c`
- `src/audio_timing_nrf54.c`
- `src/stream_lifecycle.c`, `src/stream_lifecycle.h`
- `src/flpr_ring_mgr.c`
- `src/flpr_handshake.c`
- `src/flpr/main.c` (control ACK sequence echo only)
- `src/flpr_protocol.h` (shared pure ACK constructor)

### Direct tests and test-owned seams

- `tests/unit/audio_i2s/CMakeLists.txt`
- `tests/unit/audio_i2s_identity/CMakeLists.txt`
- `tests/unit/audio_i2s_common/audio_i2s_test_helpers.h`
- `tests/unit/audio_i2s_common/audio_i2s_test_hook.h`
- `tests/unit/audio_i2s_common/fake_i2s.c/.h`
- `tests/unit/audio_i2s_common/test_sink_common.c`
- `tests/unit/audio_i2s_common/test_sink_stop.c`
- Proposed `tests/unit/audio_i2s_common/test_sink_concurrent.c`
- ASRC/identity variant tests only where reopen expectations need mechanical
  updates.
- `tests/unit/audio_shell/src/fake_audio_shell_deps.c/.h`
- `tests/unit/audio_shell/src/test_audio_shell.c`
- `tests/unit/timing_nrf54/src/mock_hal.c/.h`
- `tests/unit/timing_nrf54/src/test_timing_nrf54.c`
- `tests/unit/lifecycle/src/test_lifecycle.c`
- Existing `tests/unit/flpr_ring_mgr` and `tests/unit/flpr_handshake` only if a
  comment-adjacent behavior assertion must be clarified; do not add copied
  concurrency models.
- `tests/unit/flpr_protocol` for shared RESET/STALL ACK construction and exact
  request-sequence echo.
- `tests/bsim/src/audio_sink_stub.c`

### Contracts/metadata/evidence

- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `tests/test-matrix.json`
- `docs/development/refactor-plan.md`
- This handoff.
- New `docs/development/refactor-r1-results.md` after acceptance.

## Decided sink API

Add to `src/audio_sink.h`:

```c
int audio_sink_stream_open(void);
void audio_sink_stream_close(void);
```

Contracts:

- `audio_sink_stream_open()` waits for an overlapping stop finalizer, then
  enables admission.  Returns `0`; returns `-EIO` if sink is not configured.
  Idempotent while already open.  It does not reset/start DMA or alter saved
  frame, frame duration, ASRC, rate, drift, or offload state.
- `audio_sink_stream_close()` atomically rejects new pushes and returns without
  waiting.  Idempotent and safe from shell/BT thread context.
- `audio_sink_push()` validates arguments first, then returns `-EIO` when not
  configured, then `-EBUSY` when configured but admission is closed.  A push
  admitted before close runs to completion and retains current return result.
- `audio_sink_stop()` closes admission, waits until every admitted push fully
  exits (including repeat fallback), finalizes stop exactly once for overlapping
  callers, then returns.  It retains `configured=true` and current
  PREPARE-before-DROP order.  Sequential repeated stops retain current reset
  semantics and issue no extra triggers.

Default after successful `audio_sink_init()` is configured but admission
closed.  Only BAP gate closed→open calls `audio_sink_stream_open()`.

## Decided sink synchronization

Use one short `k_mutex` and `k_condvar` in `audio_i2s.c`, not a spinlock plus
single semaphore.  Reason: multiple overlapping stop callers and an open waiter
need broadcast wakeup; verified callback contexts are threads.  Update R1 plan
wording to record this evidence-backed primitive refinement.

State protected by mutex:

```c
static bool stream_accepting;
static uint32_t active_pushes;
static uint32_t stop_callers;
static bool stop_finalizing;
```

Exact transition rules:

1. Push admission locks mutex, checks `configured`, checks
   `stream_accepting`, increments `active_pushes`, unlocks.
2. Every return after admission uses one common exit that decrements
   `active_pushes`; transition to zero broadcasts condition variable.
3. Stop locks mutex, closes admission, computes
   `owner = (stop_callers == 0)` from the **pre-increment** count, increments
   `stop_callers`, and only owner immediately sets `stop_finalizing=true`
   before waiting for `active_pushes == 0` (condition wait releases mutex).
4. Any non-owner executes `while (stop_finalizing) condvar_wait`.  An early
   joiner waits; a late cohort joiner arriving after owner cleared finalizing
   skips loop.  Both then decrement `stop_callers`, broadcast if last, unlock,
   and return without duplicate resets/triggers.
5. Owner waits for active pushes, snapshots/clears `started` while protected,
   unlocks, runs existing drift/timing reset and PREPARE/DROP outside mutex,
   then locks, clears finalizing, decrements its caller count, broadcasts, and
   returns.  **Every caller decrements exactly once before return.**
6. Open waits while `stop_callers != 0`, then admits.  Last caller broadcasts
   so open cannot remain asleep after cohort completion.  This prevents reopen
   between drain completion and DROP/reset.

Do not use timeout-and-proceed behavior.  Proceeding to DROP while a push still
runs recreates corruption.  All admitted production pushes are already bounded;
an invariant failure should remain blocked and become visible through watchdog
or test timeout rather than corrupt ownership.

`configured` publication/check joins this mutex at API boundaries.  Init remains
boot-owned; do not invent parallel-initialization support.  Setter assigns
`input_frames` under mutex.  Push performs null/zero/even basic validation,
then under the same admission lock captures one local `input_frames` snapshot,
validates exact sample count against it **before** configured/open checks
(preserving current malformed-input `-EINVAL` precedence), checks configured/
open state, and admits.  Fill, prefill, and rate conversion use that snapshot
only.  Thus even an unexpected setter call cannot race or change a block
mid-push.  ASCS ordering still calls
the setter from `lc3_enable()` before that stream's started callback; both Mode
A enables precede the two-start gate open.  Saved frame, rate context, ASRC
state, and sequence stay BT-RX-push-owned; stop accesses them only after drain.

## Push implementation constraint

Refactor `audio_sink_push()` mechanically to one admitted-operation cleanup
path.  Preserve all existing primary errnos and cleanup order.  Missing the
decrement on any startup/steady error path is a deadlock defect.

Do not hold sink mutex across:

- performance timing;
- drift/actuator;
- slab allocation/free;
- ASRC/offload;
- `i2s_write`/`i2s_trigger`;
- logging.

## Deterministic I2S concurrency tests

Extend fake driver without changing ownership semantics:

```c
void fake_i2s_block_write_at(int call_index,
                             struct k_sem *entered,
                             struct k_sem *release);
```

For selected successful write, transfer ownership/queue block as today, signal
`entered`, then wait on `release` before returning.  Reset clears gate.  Test
files own semaphores/threads; production gets no pause hook.

Add shared concurrent test file to both ASRC and identity CMake suites:

1. **Stop drains admitted push.** Start stream, block next steady write, launch
   push thread, wait `entered`, launch stop thread, prove stop has not returned
   and PREPARE/DROP have not run, release write, join both.  Push returns 0;
   stop then returns; queue purged; slab 16/16; no duplicate pointer; final
   state configured/closed/not-started/zero active pushes.
2. **Two overlapping stops finalize once.** Same paused push, launch two stop
   callers, wait through lock-protected test snapshots until both belong to the
   same `stop_callers == 2` cohort and one finalizer is claimed, then release
   push and join all.  Both stop calls return; exactly one software reset set
   and one PREPARE/DROP pair; caller count returns zero; no leak/double free.
   Add test snapshots for `stop_callers` and `stop_finalizing` under the existing
   test-only hook block.
3. **Closed rejection and reconnect.** After stop, valid push returns `-EBUSY`
   with no allocation/write/state/counter mutation.  Explicit stream open then
   next push performs fresh seven-block prefill and START without reconfigure.
4. **Failure exits release admission.** Existing startup/steady injected failure
   rows must leave active count zero so a following stop returns.  Add
   lock-protected test snapshots for accepting/active only under
   `AUDIO_I2S_NATIVE_TEST`; definitions stay in existing GCOVR-excluded test
   hook block.
5. **Open waits for full stop cohort.** With push paused and two stops in the
   same cohort, start an open waiter.  Prove it cannot return before push release
   and both stop callers exit; afterward it returns 0 and a push is admitted.
6. **Close is nonblocking.** Pause an admitted steady write, call
   `audio_sink_stream_close()` synchronously, prove it returns with no reset or
   PREPARE/DROP while admitted push remains blocked; new push is `-EBUSY`;
   release admitted push, then explicit open works.

Use bounded test waits/joins so broken drain fails rather than hanging suite.
Do not add timing sleeps as sole synchronization; use entered/release semaphores.

Update existing helpers so normal tests explicitly open after successful init.
Keep a direct test proving successful init alone leaves admission closed.
`test_push_before_init_eio` remains `-EIO`, not `-EBUSY`.

## BAP lifecycle and shell owner

Add to `src/bt_bap.h`:

```c
void bt_bap_audio_path_stop(void);
```

This is shell-routable, thread-context, and idempotent.  `cmd_stop()` calls it
instead of `audio_sink_stop()` and retains exact output and return behavior:
`I2S stopped; drift reset.` and command return 0.

Add a short `lifecycle_lock` mutex in `bt_bap.c` and protect every
`stream_lifecycle_*` call.  Keep `stream_lifecycle.c` dependency-free.  Fixed
nesting is lifecycle mutex then sink mutex; never reverse.

Add pure lifecycle APIs and direct tests:

```c
bool stream_lifecycle_force_close(void);
```

`force_close` closes gate and blocks all later `sink_started()` calls from
opening it and returns whether gate was open before force-close for exact
observer emission.  Latch persists for current configured slot set: mere
duplicate/second-ASE start requests and stream-start callbacks never clear it.
Reset clears latch; releasing the **last configured slot** clears it so later
reconfigure/start lifecycle can open.  Releasing only one Mode A slot does not.
Direct lifecycle tests prove `start(slot0) -> force_close -> start(slot1)` stays
closed, duplicates stay closed, one-slot release does not unblock while another
remains, final release + reconfigure permits open, and reset permits fresh open.
No `allow_open` API or ASCS transaction guessing.

Close transition under lifecycle mutex:

1. normal callback uses ordinary close; shell uses forced close;
2. increment an audio-path transition generation used to invalidate open work;
3. call nonblocking `audio_sink_stream_close()`;
4. release lifecycle mutex.

Normal callback helper may then stop offload as today; callback runs on same BT
RX thread as pushes.  Public shell stop must use this order outside lifecycle
mutex:

1. close gate/admission;
2. `audio_sink_stop()` (drain + DROP);
3. `audio_offload_stream_stop()` only after drain.

This prevents admitted push from racing offload reset.  Preserve first-close
observer event exactly once.  **Shell thread must not clear/reset Mode A,
decoder, sequence, stats, or any other BT-RX-owned state.**  An RX callback that
passed lifecycle query before forced close may finish decode; when it reaches
sink, it either was already admitted and stop drains it, or receives `-EBUSY`.
Serialized BT callbacks retain ownership of Mode A cleanup.

Open transition:

1. under lifecycle mutex, run `stream_lifecycle_sink_started()`;
2. on closed→open increment/capture transition generation and call
   `audio_sink_stream_open()` under fixed lifecycle→sink lock order;
3. if sink open returns `-EIO`, roll back lifecycle gate to closed, increment
   generation, keep sink closed, release lock, log error, and skip clear/perf/
   offload/observer open work;
4. otherwise release lifecycle mutex;
5. run existing clear/perf/offload-start work outside lock;
6. recheck lifecycle-open + generation under lifecycle mutex and compute local
   `stale_open`, release mutex, then if stale call
   `audio_offload_stream_stop()` outside mutex so final state is closed.

Forced-close latch closes the reopen window: a stream-started callback arriving
during or after shell transaction cannot reopen current configured lifecycle.
Final slot release or disconnect/reset establishes next lifecycle.
Transition-generation recheck closes separate offload-start window.  Do not add
lifecycle condition waits or hold lifecycle mutex across complete shell stop.

No lifecycle mutex may span offload cancellation/scheduling, sink drain/I2S,
decode, logging, or Bluetooth stack calls.  Update future R6 plan bullet to
reuse R1 lifecycle lock rather than introduce a duplicate.

### Shell test

Fake `bt_bap_audio_path_stop()` in shared shell dependencies and count calls.
Update `test_stop_exactly_once` to prove:

- command calls BAP path stop exactly once;
- old direct fake `audio_sink_stop()` is not called;
- output and return remain exact.

No full `bt_bap.c` unit compile.  Direct lifecycle tests pin forced-close winner
semantics; sink concurrency tests pin drain/open order; BSim builds and executes
production BAP normal wiring; G2 exercises real callback context.  Do not invent
a copied BAP coordinator model.

## BSim sink stub

Implement new sink API in `tests/bsim/src/audio_sink_stub.c`:

- init sets configured=true and admission=false;
- `audio_sink_set_input_frames()` never enables admission;
- existing test-only begin/segment heuristics never enable admission;
- open returns 0 and alone restores admission for current/new segment;
- close makes future stub pushes return `-EBUSY` non-destructively;
- stop retains exact segment-finalization and statistics-snapshot ordering.

Do not move existing segment reset heuristics unless required.  BSim full/L/R
hashes, totals, observer counts, and segment counts must remain byte-identical.

## nRF54 timing synchronization

Add one thread-context control mutex in `audio_timing_nrf54.c`:

- `audio_timing_sdu_ref_update()` holds it across anchor decision, GRTC first
  compare programming, and anchor/active commit.  All error/no-op returns
  unlock correctly.
- `audio_timing_reset()` holds it across inactive/generation transition,
  compare disable, and anchor/last-state reset.
- GRTC ISR never takes this mutex.

Capture `generation` once at GRTC ISR entry before active/state reads and carry
that captured generation in every payload.  This prevents a callback accepted
under one session from being labeled as a later generation.  nRF54L15 cpuapp is
single-core, so reset thread cannot execute concurrently while ISR itself runs;
retain generation as queued-work staleness proof.  Do not hold `diag_lock`
across nrfx HAL calls.

Extend timing mock with deterministic blocking of first compare programming
(entered/release semaphores).  Test:

1. update thread enters blocked compare while holding control mutex;
2. reset thread begins but cannot disable/reset until release;
3. release update, join both;
4. final state inactive, generation incremented, compare disabled, and a fresh
   anchor succeeds.

Existing stale/fresh FIFO, overflow, schedule-failure, and reset tests must stay
unchanged in behavior.

## FLPR ordering hardening — no message-layout/version/recovery-policy change

### `flpr_ring_mgr.c`

Add `ring_data_lock` (`k_mutex`) to serialize cpuapp-side bulk ring memory/header
operations against reset/reinit.  Callback contexts never take it.

Lock discipline:

- fixed order is `ring_data_lock` then `ring_lock`; IPC handlers take
  `ring_lock` only; never reverse;
- hold data mutex across produce begin/fill/commit and consume
  begin/copy/done for block and ASRC APIs;
- hold it across coordinated reset send/ACK wait/local reset, direct reset,
  complete stall request/send/ACK wait transaction, remote-restarted header
  reinit, stale-test output production, and status ring header snapshots;
- `wait_consume()` does not hold data mutex, so recovery reset can proceed while
  submit waits for output;
- use private `_locked` reset/status helpers for composing public functions;
  do not rely on recursive mutex behavior;
- capture `ring_stream_epoch` under `ring_lock` while data mutex prevents reset,
  then use local snapshot; epoch 0 causes produce to return INVALID before slot
  mutation;
- `ring_lock` continues protecting manager control/diagnostic fields, not bulk
  copies.

This barrier makes reset unable to zero headers/memory during cpuapp
produce/consume.  Do not take `audio_offload` submit mutex in recovery reset;
that can deadlock a submit waiting for recovery/output.

Also harden reset/stall ACK request state under `ring_lock` with explicit
`*_ack_armed`, expected data, and monotonically incremented nonzero 16-bit
request sequence token:

1. request path disarms and clears fields under lock;
2. while disarmed, drain stale semaphore tokens (callbacks ignore ACKs and do
   not give);
3. under lock allocate next sequence token without wrap, install expected
   data/token, and arm
   before send; request uses token in existing `flpr_msg.seq` field;
4. add pure shared `flpr_control_ack_make(request, ack_type, data)` helper in
   `flpr_protocol.h`; it copies request seq and current protocol version.
   FLPR `src/flpr/main.c` uses it for RESET_ACK/STALL_ACK instead of hardcoded
   zero.  Direct `flpr_protocol` tests prove both ACK types echo exact sequence,
   data, type, and version; message layout/version/constants stay unchanged;
5. cpuapp handler accepts only first ACK while armed **and sequence matches**,
   stores payload, disarms, unlocks, then gives semaphore; ACK while disarmed or
   with stale sequence is ignored/counted stale;
6. send failure and timeout disarm under lock; late ACK is ignored even when its
   data equals a later retry because sequence differs;
7. successful waiter snapshots stored payload under lock after take and checks
   exact data; same-sequence wrong data preserves existing fail-fast `-EIO`.

Apply same state machine to reset and stall ACKs.  Add direct retry regression:
request A times out, request B uses same data, late ACK A arrives after B arms
and is ignored by sequence, ACK B completes.  Retain match/data-mismatch/timeout
tests.  **Never reuse a 16-bit token in one FLPR session:** after token 0xFFFF,
next request returns explicit `-EOVERFLOW` until
`flpr_ring_mgr_remote_restarted()` (known quiescence boundary) clears ACK state
and resets token counters.  Add boundary test: 0xFFFF succeeds, next overflows,
remote restart permits token 1.  Maintain at most one armed request per control
type.  Stall transaction holds `ring_data_lock` through ACK wait, so remote
restart cannot clear token/armed state beneath waiter; callback still takes
`ring_lock` only and can wake it.

Retain/document:

- shell ring/stall/stale diagnostics require idle/quiesced production path and
  scenario ordering; they are not safe to run concurrently with streaming.
- stale-test output-ring production is shell-only after coordinated reset with
  FLPR quiesced.

Do not take `ring_lock` across memcpy/CRC or IPC notification.

Add deterministic native ring-manager barrier test with a test-only pause after
produce begin: producer holds data mutex and signals entered; reset thread must
not return or mutate epoch/header until release; after release producer exits,
reset completes, new epoch is exact, rings empty, no corruption/deadlock.  Test
hook remains under existing native-test guard/coverage exclusion.

### `flpr_handshake.c`

Remove real unsynchronized accesses rather than blessing them:

- call `flpr_msg_validate()` while holding `flpr_lock`, because it mutates
  `err_len`/`err_version` read by status;
- snapshot ring handler function + shared user-data under `flpr_lock`, then
  invoke outside lock;
- snapshot health callback + user-data under lock with transition state, then
  invoke outside lock;
- capture READY count under lock before logging;
- protect hang ACK clear/publish/read under `flpr_lock`; keep payload-before-
  semaphore-give and take-before-read ordering;
- heartbeat cancel from its own running work item may not prevent final
  reschedule; inactive checks make later wake a no-op.

No handler call occurs under spinlock.  No heartbeat/recovery behavior change
in R1.  Extend direct tests for handler re-registration/snapshot consistency,
validation counters under concurrent status reads where deterministic, health
callback snapshot, and hang ACK ordering; use production source, not copied
models.

## Contracts and matrix

Update behavior contract without renumbering existing IDs:

- fix malformed lifecycle heading separator if touched;
- LIFE-003: closed→open also opens sink admission once;
- LIFE-004/005/006: gate/admission close before teardown, late push rejected;
  sequential repeated stop reruns software resets but emits no extra I2S
  triggers; overlapping stop callers share one finalization;
- I2S-001: configured-but-closed valid push returns `-EBUSY`;
- I2S-007: stop closes admission, drains admitted pushes, finalizes once,
  PREPARE then DROP, configured retained;
- add next available I2S contract for admission/drain concurrency if needed.

Update `tests/test-matrix.json` `src/audio_i2s.c`:

- `audio_sink_stream_open`: `0` and `-EIO` witnesses;
- `audio_sink_stream_close`: void witness;
- `audio_sink_push`: `-EBUSY` witness;
- transitions `closed->open`, `open->closed`, and corrected
  `stopped->open->started` witness;
- concurrent drain/finalize witness.

Update lifecycle matrix for forced-close, last-slot-release latch clearing, and
reset-latch transitions.
Update FLPR/timing matrix witnesses only for actual new direct tests; public
outcome labels remain unchanged where APIs do not change.
For coordinated-reset and stall public APIs, document/add `-EOVERFLOW` outcome
with token-boundary witnesses in headers, behavior contract, and matrix.

Update coverage matrix test descriptions/counts after real observed focused
runs.  Do not estimate counts before running.

## Non-scope

- No R2 dead API deletion.
- No R4 shell file split.
- No R5 offload transaction decomposition.
- No R6 session extraction or codec-shape move.
- No R7 full teardown coordinator.
- No FLPR message layout/version/ABI, deadline, or recovery-policy change.
  RESET/STALL use existing `seq` field for ACK correlation as explicit R1
  concurrency hardening; both images and protocol tests change together.
- No drift tuning, audio format, slab count, I2S prefill, BSim expected-value,
  coverage baseline, Kconfig, devicetree, or board change.
- No new hardware behavior acceptance claim beyond G2 regression smoke.

## Test-first workflow

Before implementation, add deterministic public-boundary regression tests and
run them against pre-fix production where practical.  Record expected failures
(closed admission API may initially fail to link; race test may fail ordering).
Do not commit failing state.  Implement decided design, run focused green, then
commit tests+implementation together.

## Focused verification

Use repo tooling/discovered suites; exact commands may use Twister `-T` paths or
existing built executable helpers.  Minimum focused set:

```bash
bash -n scripts/test-all.sh
python3 scripts/check-test-matrix.py --repo-root "$PWD"
python3 -m json.tool tests/test-matrix.json >/dev/null
git diff --check
```

Run these production-source suites with zero warnings/failures:

- `audio_i2s`
- `audio_i2s_identity`
- `audio_shell`
- `audio_shell_noperf`
- `audio_shell_nrf54`
- `lifecycle`
- `timing_nrf54`
- `flpr_ring_mgr`
- `flpr_handshake`
- BSim Stage 1 full 16-scenario child at least through canonical G1

Run coverage in report-only mode while dirty if useful.  Numeric population and
all per-file/aggregate ratios may not regress.  No baseline rewrite.

## Commit and acceptance order

1. Implement tests/source/contracts/metadata from clean `f654b58`.
2. Run focused green and inspect status/diff/log.
3. Commit one R1 implementation commit (or one tests+source commit plus one
   contracts/metadata commit); no incomplete commit.
4. On clean exact implementation hash, run G1.
5. Run autonomous G2 on both targets.
6. Only after G1+G2 pass, write `refactor-r1-results.md`, mark R1 ACCEPTED/link
   results in plan, and create separate evidence commit.  Evidence commit gets
   focused docs/matrix/diff checks; no G1/G2 rerun solely for evidence.

Any non-evidence fix after G1/G2 creates a new implementation commit and
requires affected gates rerun.  Never amend.

## G1

```bash
./scripts/test-all.sh
./scripts/test-coverage.sh --output /tmp/r1-coverage --clean-output
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
```

Acceptance: 47/47 children, coverage population 26 with no ratio regression,
builds 3/3, build contract exactly 76/76 (build-contract code is non-scope),
BSim hashes/counts unchanged, zero actionable warnings.

## G2 autonomous hardware smoke

No human-operated central.  Use repository nRF5340DK `hci_uart` central and
`scripts/bap_central.py`.

For each receiver target:

1. Resolve probe with `nrf-probes`; never record static probe mapping.
2. Start serial capture before flash/reset (`ttyACM0` nRF54L15,
   `ttyUSB0` nRF5340/E83).
3. Flash with `fw-flash-54l15` or `fw-flash-5340` only.
4. Attach/power/configure `hci0` per AGENTS central setup and verify identity
   `C0:AA:BB:CC:DD:EE`, powered/le/secure-conn/cis-central.
5. Run Mode A 30 s and Mode B 30 s (`--stereo`) without sudo.  Use peer address
   bypass only when scan fails and record receiver boot-log identity.
6. Preserve raw logs under `/tmp` through review.

Require expected boot/advertising, nonzero SDUs/decoded frames, I2S DMA start,
clean stop/reconnect behavior, and zero compiler/Kconfig/boot warnings,
assertions, decode errors, I2S underruns, stream resets, offload integrity
faults, slab errors, deadlocks, or unexplained fallback.  nRF54 FLPR healthy
stream remains offloaded; E83 APLL path remains stable.  Audibility is not
required for this digital smoke.

If sudo/hardware/probe/port is unavailable, stop and report exact blocker; do
not replace G2 with user-operated or simulated evidence.

## Escalation

Stop instead of guessing when:

- condition-variable/open-stop design cannot meet ordering above;
- any admitted path misses decrement or a test hangs;
- lifecycle/offload generation cannot guarantee final closed state;
- timing mutex causes ISR/HAL deadlock or new warning;
- BSim hash/count changes;
- coverage ratio/population changes unexplained;
- hardware emits any warning/fault or central cannot run autonomously;
- two materially different attempts fail same criterion.

Return exact blocker, attempts, logs/diffs/status, and one precise question.  Do
not weaken tests, add timeout-and-corrupt fallback, repin output, or expand
architecture.

## Executor recap

Return exact:

- files and commits;
- pre-fix regression failures and post-fix focused passes;
- G1 child count, coverage totals/tool versions, builds, contract assertions,
  BSim hash disposition, warnings;
- G2 probe evidence without static mapping, receiver/central commands, raw log
  paths, SDU/decode/I2S/offload counters, and outcomes;
- final worktree status;
- deviations/blockers.

No push, merge, PR, amend, force-push, destructive recovery, attribution footer,
or unrelated cleanup.
