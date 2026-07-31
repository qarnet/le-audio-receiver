# Phase T1 handoff — execute real FLPR production source in tests

## Goal

Replace false-confidence FLPR runtime, ring-manager, and handshake tests with
native_sim tests that compile and execute the real production implementations.
Keep wire formats, hardware behavior, memory layout, and public production APIs
stable except for small corrections proven by new tests.

Master plan: `docs/development/pre-refactor-testing-plan.md`.
Behavior contract: `docs/testing/behavior-contract.md`.
Coverage baseline: `docs/testing/coverage-matrix.md`.

## Git and execution

- Work on current branch `test/pre-refactor-behavior`, based on accepted T0
  commit `0453586206d1e0cdc15896693634883ff62da8c5`.
- Do not push, merge, open a PR, amend, or rewrite T0 commits.
- Commit complete T1 implementation before returning.
- Executor model must be `deepseek/deepseek-v4-flash`, variant `max`.

## In scope

- `src/flpr_runtime.c`, `src/flpr_runtime.h`
- `src/flpr_ring_mgr.c`, `src/flpr_ring_mgr.h`
- `src/flpr_handshake.c`, `src/flpr_handshake.h`
- test-only hook headers under corresponding `tests/unit/*/src/`
- all files in:
  - `tests/unit/flpr_runtime/`
  - `tests/unit/flpr_ring_mgr/`
  - `tests/unit/flpr_handshake/`
- T1 status/evidence updates:
  - `docs/testing/coverage-matrix.md`
  - new `docs/testing/t1-flpr-production-tests.md`
  - `STATUS.md`

## Out of scope

- Audio decode, I2S, BAP, timing, or actuator work from T2+.
- FLPR wire-format or ABI changes.
- Shared-memory address/layout changes.
- Recovery policy tuning, deadlines, backoff constants, or ring size changes.
- Broad production refactoring.
- Flashing or hardware interaction.
- Treating native cache instructions as proof of physical cache coherence.

## Common testability rules

1. Tests must compile production `.c` files directly.
2. Never copy production algorithms into test files.
3. Use test-only compile definitions without `CONFIG_` prefix:
   - `FLPR_RUNTIME_NATIVE_TEST`
   - `FLPR_RING_MGR_NATIVE_TEST`
   - `FLPR_HANDSHAKE_NATIVE_TEST`
4. Guard every test hook with its matching definition. Production builds must
   contain no test symbols or host arrays.
5. Keep hook declarations in test-owned headers added to include path only for
   that test suite. Production public headers should expose no test API unless
   compilation requires a guarded declaration.
6. Hooks may inject memory, transport, HAL, time, or observation events. They
   may not provide an alternate implementation of state transitions.
7. Tests invoke real public production functions and production callbacks.
8. Use Zephyr ztest and existing native_sim target. No new external dependency.

## Grounding evidence

- Raw native_sim dereference of fixed devicetree `reg` addresses is unsafe;
  native_sim has no MMU/address translation. Test memory must use host arrays.
- NCS v3.3.0 canonical fake IPC backend:
  `~/ncs/v3.3.0/zephyr/tests/subsys/ipc/ipc_service/`.
- NCS v3.3.0 fake backend uses `struct ipc_service_backend` plus
  `DEVICE_DT_INST_DEFINE`, and invokes registered endpoint callbacks.
- Shadow-HAL pattern exists under
  `~/ncs/v3.3.0/nrf/tests/subsys/bluetooth/controller/`.
- `sys_cache_data_flush_range()` compiles on native_sim and returns `-ENOTSUP`
  when cache management is absent; physical cache semantics remain hardware-
  only evidence.
- Production shared ring layout remains input `0x2002C000`, output
  `0x2002E000`, total end `0x20030000`; test arrays do not change production DT.

# T1A — FLPR runtime restart

## Build real production path

Change production-path guard in `src/flpr_runtime.c` so the nRF54 restart body
compiles when either `CONFIG_SOC_NRF54L15` or `FLPR_RUNTIME_NATIVE_TEST` is
defined. Production behavior remains selected by `CONFIG_SOC_NRF54L15`.

Under `FLPR_RUNTIME_NATIVE_TEST` only:

- include a test-owned hook header before hardware constants;
- use test-provided `NRF_VPR_Type` storage;
- use test-provided source and execution byte arrays of equal fixed size;
- bypass production-only fixed-address BUILD_ASSERTs;
- route busy wait, sleep, uptime, cache-flush/barrier observation, and
  after-copy fault injection through hooks;
- provide guarded reset and mutex-control helpers so every test starts clean
  and mutex-busy behavior can be tested;
- emit ordered test events from the actual production restart function.

Outside test mode retain exact DT derivation and every fixed-address assertion.
No production function pointer tables or runtime overhead.

## Runtime observation events

Define test-only ordered events covering:

1. snapshot status/epoch;
2. source CRC;
3. disconnect;
4. stop CPURUN;
5. assert reset;
6. copy;
7. cache flush + barriers;
8. execution CRC;
9. INITPC;
10. reconnect;
11. set CPURUN;
12. release reset;
13. wait bound;
14. wait new READY;
15. success;
16. failure stop, where applicable.

Events observe real control flow. They must not drive it.

## Runtime corrections required by contract/header

New tests must correct these current defects:

1. Set `failed_stage = FLPR_STAGE_DISCONNECT` before disconnect. A second
   restart whose disconnect fails must not retain prior `SUCCESS` stage.
2. Set `failed_stage = FLPR_STAGE_STOP` before stopping CPURUN, then advance to
   ASSERT_RESET. Keep enum meaningful even though HAL stop is void.
3. `max_duration_ms` is documented as longest restart. Include failed attempts,
   not only successful attempts.
4. `flpr_runtime.h` currently says `flpr_runtime_restart()` itself rejects an
   active offload stream. It does not and must not: automatic recovery calls it
   while offload is RECOVERING, and shell owns its separate ACTIVE guard.
   Correct header/module comments instead of adding an internal active guard.
5. Replace stale “pulse reset” wording with held-reset sequence matching code.

## Runtime tests

Replace structural/copied tests with tests invoking real:

- `flpr_runtime_init()`;
- `flpr_runtime_restart()`;
- `flpr_runtime_get_status()`.

Required cases:

- init success and idempotence;
- restart before init returns `-ENODEV`;
- full success event order;
- exactly two DMCONTROL writes: assert then release;
- DMACTIVE enabled in both masks;
- CPURUN false before assert and true before release;
- source copied exactly into execution memory;
- cache flush/barrier occurs after copy and before execution CRC;
- matching CRC success;
- injected execution corruption produces `-EIO` at CRC_VERIFY;
- disconnect failure reports DISCONNECT and does not touch VPR;
- reconnect failure reports RECONNECT and leaves CPURUN false/reset held;
- wait-bound failure reports WAIT_BOUND and stops CPURUN;
- wait-new-ready failure reports WAIT_READY and stops CPURUN;
- changed epoch required for successful mock wait;
- mutex busy returns `-EBUSY`, increments `busy_reject`, and does not increment
  restart requests;
- requests/success/fail/errno/reload/CRC/readback counters exact;
- cumulative duration and max duration include failed and successful attempts;
- second-run early failure does not retain prior success stage;
- null status output is harmless;
- non-nRF production stub behavior remains covered by a separate compile case
  only if useful; it must not be presented as nRF54 restart proof.

# T1B — FLPR ring manager

## Build real production source

`tests/unit/flpr_ring_mgr/CMakeLists.txt` must compile:

- real `src/flpr_ring_mgr.c`;
- real `src/flpr_ring.c`;
- real `src/flpr_cache.c`;
- test-owned handshake mock and test hooks.

Under `FLPR_RING_MGR_NATIVE_TEST` only, replace fixed DT-derived ring pointers
with two aligned host arrays, each `FLPR_RING_TOTAL_SIZE`. Keep all production
DT assertions unchanged outside test mode.

Provide guarded helpers to:

- reset all module-static state and semaphores between tests;
- access test input/output ring arrays for arranging peer-produced slots;
- invoke captured production IPC handlers through the handshake mock;
- inspect/drain semaphore state where no public observable exists.

Do not duplicate reset, produce, consume, or callback algorithms in tests.

## Handshake mock

Implement real `flpr_handshake.h` API surface needed by ring manager:

- controllable ready/acked status;
- capture registered ring callbacks and user data;
- record sent `struct flpr_msg` values;
- configurable send result;
- optional synchronous ACK injection for reset and stall messages.

Tests must trigger the callbacks captured from
`flpr_handshake_register_ring_handlers()`, thereby executing production static
handlers in `flpr_ring_mgr.c`.

## Ring-manager tests

Delete copied fake epoch logic as primary proof. Required real-source cases:

Initialization and status:

- init rejects remote not ready/acked with `-EAGAIN`;
- successful init registers handlers and initializes both ring headers;
- status before/after init, including null output;
- repeated init remains safe and produces valid headers.

Notification and reset:

- producer notification message fields and success counters;
- send failure and notify-error counter;
- local reset rejects zero epoch;
- coordinated reset rejects unhealthy peer;
- generated epoch zero failure through test-controlled cycle source if needed;
- reset send failure maps to documented error;
- ACK timeout;
- missing/mismatched ACK;
- exact matching ACK success;
- old-epoch notification rejected without semaphore give;
- matching-epoch notification gives semaphore;
- token present before reset is drained and observable;
- notification during epoch-zero invalidation window is stale;
- ring reset failure leaves local epoch invalid.

Produce/consume:

- frame count over input maximum rejected;
- null PCM with nonzero frames produces zero-filled payload as documented;
- valid produce writes exact sequence, epoch, frame count, ppm, timestamp,
  flags, payload, zero remainder, and optional CRC;
- producer stall returns FULL and counts backpressure without advancing index;
- physical four-slot ring full returns FULL and counts full event;
- index wrap preserves slot order;
- consume empty;
- stale slot advances past stale data and counts event;
- invalid output frame count is consumed/rejected safely;
- valid consume fills every optional output correctly;
- null optional outputs are safe;
- latency wrap uses unsigned subtraction and updates metrics only in test mode;
- CRC and deterministic payload mismatch accounting when test mode is active.

Typed ASRC:

- real `flpr_ring_mgr_produce_asrc()` metadata/pre-state/CRC;
- wrong input frame count and null state rejection;
- real `flpr_ring_mgr_consume_asrc_result()` success;
- empty, stale, bad status, bad flags, sequence/state corruption, bad frame
  range, bad CRC, and transactional output preservation.

Stall/report/restart:

- persistent and timed stall message packing;
- zero-mask timed rejection and duration overflow rejection;
- send failure, timeout, wrong ACK, exact ACK;
- every report subtype updates correct status fields;
- `flpr_ring_mgr_remote_restarted()` invalidates/drains/reinitializes state and
  requires subsequent coordinated reset.

`flpr_ring_mgr_set_consume_cb()` is currently unused and implemented as a
no-op. Do not redesign it in T1. Record it as refactor-review candidate in T1
results.

# T1C — FLPR handshake

## Fake IPC backend

Follow NCS v3.3.0 pattern from
`zephyr/tests/subsys/ipc/ipc_service/`:

- add test-local devicetree binding for a fake IPC backend;
- add test overlay defining node label `ipc0` with that compatible;
- implement `struct ipc_service_backend` using `DEVICE_DT_INST_DEFINE`;
- use real Zephyr `ipc_service_open_instance`, register, deregister, and send
  APIs;
- backend captures endpoint config and invokes its real callbacks;
- backend exposes test controls for open/register/deregister/send return values,
  optional automatic bound callback, sent-message history, and arbitrary
  incoming messages.

`tests/unit/flpr_handshake/CMakeLists.txt` must compile real
`src/flpr_handshake.c`. Keep existing pure `flpr_protocol.h` coverage in the
protocol suite; remove duplicate copied lifecycle simulations from handshake
suite when real callback tests replace them.

## Minimal handshake hooks

Under `FLPR_HANDSHAKE_NATIVE_TEST` only, provide helpers to:

- cancel heartbeat work and reset all module-static state/semaphores/callbacks
  between tests;
- invoke one heartbeat work iteration synchronously, then allow test teardown
  to cancel its reschedule;
- set peer health/timestamp state needed to test transition callback without a
  five-second wall-clock wait;
- inspect whether callback executes with module spinlock released, if needed.

Do not replace protocol helpers or callback switch logic.

## Handshake corrections required

1. `flpr_handshake_send_msg()` currently bypasses internal `send_msg()` and
   therefore contradicts its header claim that send failures increment
   `err_send`. Route it through `send_msg()` and test exact accounting.
2. Header says ring handlers run “under spinlock”; production invokes them from
   IPC receive context without holding `flpr_lock`. Correct header wording.
3. Preserve documented lifetime counters and last epoch across unbind.

## Handshake tests

Initialization:

- open/register success;
- open `-EALREADY` accepted;
- open failure propagated;
- register failure propagated;
- endpoint bound callback sets bound and semaphore;
- init state reset and idempotent test cleanup.

Receive path:

- short, oversized, and wrong-version messages update correct counters;
- unknown type increments unknown counter;
- first READY/new epoch updates state and sends exact READY_ACK;
- duplicate same-epoch READY does not count reboot or signal new-ready;
- changed-epoch READY does;
- READY_ACK send failure increments `err_send` and does not mark acked/signal;
- heartbeat updates RX sequence/health and sends exact echo only when acked;
- heartbeat ACK tracking;
- stress PONG match signals waiter; stale/future/inactive classification;
- all four ring-control message types dispatch exact captured handler and user
  data with no module spinlock held;
- FAULT_HANG_ACK signals waiter;
- unexpected-but-allowed message types do not increment unknown;
- endpoint error callback is safe for null/non-null messages.

Heartbeat and health:

- no send before ready/acked/session available;
- successful heartbeat increments TX sequence;
- send failure increments error without TX increment;
- healthy-to-unhealthy transition invokes callback once outside lock;
- repeated unhealthy checks do not duplicate callback;
- work reschedules at documented interval and teardown cancels it.

Lifecycle/public API:

- unbound clears volatile state and preserves every documented lifetime field;
- disconnect marks unavailable, cancels heartbeat, drains bound/new-ready state,
  and propagates deregister failure;
- reconnect drains stale semaphores, propagates register failure, and marks
  session available only after success;
- wait-bound timeout/success/reposted semantics;
- wait-new-ready returns `-ECANCELED` while unavailable;
- already-new epoch succeeds without waiting;
- same epoch times out;
- changed epoch succeeds only after READY_ACK send succeeds;
- public send rejects null and counts one error on backend failure;
- ring/health handler registration and unregistration;
- stress rejects unavailable/active states, clamps count, handles success,
  send failure, timeout, and late PONG;
- fault-hang send failure, ACK timeout, and ACK success.

# Documentation and evidence

Create `docs/testing/t1-flpr-production-tests.md` with:

- exact production sources linked by each suite;
- test-hook architecture and why raw DT addresses cannot be dereferenced on
  native_sim;
- list of copied/stub tests removed or replaced;
- behavior defects fixed;
- physical cache/FLPR entry-point behavior still hardware-only;
- exact suite/gate/build results;
- no hardware claim from native_sim.

Update `docs/testing/coverage-matrix.md`:

- runtime, ring manager, and handshake become direct production-source proof;
- remove obsolete weak-test statements;
- retain branch-coverage-unmeasured until T7;
- classify `flpr_cache.c` native tests as API/barrier-call proof only and
  physical cache semantics as hardware-only;
- retain `src/flpr/main.c` as hardware-only.

Update `STATUS.md` to T1 ACCEPTED only after all validation passes. Record T2
as next. Do not change historical acceptance sections.

# Verification

Run focused suites first:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t1_flpr_runtime tests/unit/flpr_runtime -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t1_flpr_ring_mgr tests/unit/flpr_ring_mgr -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t1_flpr_handshake tests/unit/flpr_handshake -p -t run
```

Then:

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Desktop may still lack BabbleSim prerequisites. Validate exact final T1 commit
on workstation via non-destructive git bundle + detached temporary worktree,
as T0 did. Require full gate success and deterministic accepted hashes. Remove
all temporary refs/worktrees/bundles and leave workstation main clean.

No flashing or serial interaction.

# Acceptance

- All three suites compile named real production `.c` files.
- No copied algorithm remains primary proof.
- Required tests pass.
- Existing tests remain green.
- Full exact-commit gate passes on workstation.
- All three builds pass.
- Production fixed addresses and wire ABI unchanged.
- Production images contain no test hooks.
- Docs accurately state remaining hardware-only gaps.
- Worktree clean after scoped commits.

# Commit policy

Logical interim commits are allowed because T1 is large, but T1 remains open
until final validation. Suggested messages:

```text
tests: execute FLPR runtime restart production path
tests: execute FLPR ring manager production path
tests: execute FLPR handshake production path
docs: record T1 FLPR production test evidence
```

Before each commit inspect status/diff/log and stage only intended files. Never
amend, push, merge, open PR, or add attribution.

Return files changed, exact test counts/results, builds, final commit hashes,
workstation cleanup, defects corrected, deviations, and blockers.
