# P2 results — generic input and LED adapter

Accepted: 2026-08-07.  Base commit `2ca4fb3` (P1 accepted); handoff
commit `1fa7f17` (`docs: record P2 handoff — generic input and LED
adapter`); implementation commit `495b3a7`; coverage migration commit
`184969a`; acceptance commit (this document's commit).  Plan:
`docs/development/user-pairing-control-plan.md`.

P2 delivers one reusable Zephyr `gpio-keys`/GPIO LED adapter
(`src/user_pairing_io.c/.h`) with no board-number conditionals that
translates one debounced user button into the already-accepted
`pairing_mode` requests, plus a direct native GPIO/input-emulation
Twister suite compiling the production source.  The production feature
stays disabled on both boards: `CONFIG_USER_PAIRING_INPUT` is `n` in
every production board config, so `zephyr_sources_ifdef` never compiles
the new source into firmware.  Builds 3/3, build contract 79/79, all
existing BSim pins byte-identical, and the canonical gate grew 56 → 57
children exactly as the handoff specified.

## Files and public API

- `src/user_pairing_io.h` — public singleton API.  Contains no GPIO,
  input-event, devicetree, or Bluetooth types.
  - `struct user_pairing_io_status` — initialized, pressed, led_active,
    hold_generation, bonding_threshold_armed, reset_threshold_armed,
    last_error.
  - `user_pairing_io_init` (0 / -EALREADY / exact step errno with
    initialized=false, LED inactive where possible, no armed work),
    `user_pairing_io_led_set(bool active, void *ctx)` (0 / exact GPIO
    errno; signature deliberately matches `pairing_mode_ops.led_set`,
    ctx ignored — one shared P1 operation context), and
    `user_pairing_io_get_status` (documented NULL-safe no-op).
- `src/user_pairing_io.c` — the adapter: DT-alias hardware bindings
  (`DEVICE_DT_GET(DT_PARENT(DT_ALIAS(user_button)))` input device,
  `GPIO_DT_SPEC_GET` button/LED specs), the static
  `INPUT_CALLBACK_DEFINE` registration filtered to the gpio-keys device,
  a callback body filtering `INPUT_EV_KEY` + selected `zephyr,code` +
  `sync`, the hold-generation-guarded BONDING/RESET threshold works on
  the system work queue, logical LED driving through a short-spinlock
  status cache, and compile-time alias/node/property assertions.
- Root `Kconfig` — `USER_PAIRING_INPUT` (default `n`, `depends on
  USER_PAIRING_CONTROL && INPUT` + `DT_HAS_GPIO_KEYS_ENABLED`,
  `select GPIO`) and `USER_PAIRING_DEBOUNCE_MS` (default 30, range
  1–1000).  P1 already owns the hold/LED timing symbols; P2 does not
  duplicate them.
- Root `CMakeLists.txt` — `zephyr_sources_ifdef(CONFIG_USER_PAIRING_INPUT
  src/user_pairing_io.c)`.
- `tests/unit/user_pairing_io/` — the direct twister suite: real input
  subsystem + real gpio-keys driver + real gpio-emul controller on
  native_sim, fake link implementations of
  `pairing_mode_request_bonding()/request_reset()`.
- `tests/test-matrix.json` — stateful direct entry for
  `src/user_pairing_io.c` with exact outcomes and transition witnesses.
- `docs/testing/coverage-matrix.md` — source row + suite inventory
  (twister 32 → 33; gate 56 → 57) in the implementation commit, and the
  "P2 baseline migration" provenance in the coverage commit.

## Devicetree contract (compile-time proven)

When `CONFIG_USER_PAIRING_INPUT=y` the `user-button` and `user-led`
aliases are mandatory; missing aliases, a non-`gpio-keys` parent, a
missing `gpios`/`zephyr,code`, or a `debounce-interval-ms` unequal to
`CONFIG_USER_PAIRING_DEBOUNCE_MS` all fail the build with a specific
`#error`/`BUILD_ASSERT` message.  The test overlay wires an
active-low/pull-up button on gpio0 pin 1 (30 ms debounce, `INPUT_KEY_0`)
and an active-low LED on gpio0 pin 2 through the aliases; the stock
native_sim `leds` node (pin 0) is untouched.

## Button event behavior (documented contract)

- The gpio-keys driver owns electrical edge handling and debounce; the
  module adds no second debounce timer.
- Press: duplicate press no-op; nonzero hold generation (wrap zero to
  one); pressed=true; both thresholds armed with the captured
  generation; scheduled at `USER_PAIRING_BOND_HOLD_MS` /
  `USER_PAIRING_RESET_HOLD_MS`.  A negative scheduling result cancels
  both, clears pressed/armed, records `last_error`, and logs the exact
  operation + errno — no second fatal owner, no retry.
- Bonding threshold: rechecks initialized/pressed/armed/generation,
  clears only the bonding armed flag, calls
  `pairing_mode_request_bonding()` exactly once.  `-ECANCELED` after a
  P1 fatal is logged; any negative return is surfaced in status/logs,
  never retried.
- Reset threshold: rechecks initialized/pressed/armed/generation, clears
  both armed flags, cancels a still-armed bonding work, calls
  `pairing_mode_request_reset()` exactly once — valid even when BONDING
  already fired (P1 RESET priority supersedes it).
- Release: duplicate release no-op; pressed=false; generation bumped
  (invalidating both captured generations); both armed flags cleared;
  both works cancelled best-effort (`k_work_cancel_delayable()` returns
  a busy-state bitmask, not errno — never logged as failure, never
  blocking); never requests a mode on release.
- `k_work_cancel_delayable()` returns a work busy-status bitmask, not
  errno.  Cancellation is best-effort generation invalidation; never log
  its nonzero return as failure and never use blocking cancel from input
  callback context.

## LED operation

- `user_pairing_io_led_set(active, ctx)` calls `gpio_pin_set_dt()` with
  a logical value; active-low/high polarity comes exclusively from DT.
- Cached `led_active` updates only after GPIO success; the exact GPIO
  errno propagates so P1 invokes its accepted fatal reboot path.
- All calls are thread-safe between input callback, threshold work,
  controller work, and status readers via a short spinlock; the lock is
  never held across GPIO, P1 request, logging, or work-cancel calls.

## Tests (21 direct, new twister suite)

`tests/unit/user_pairing_io` compiles the production
`src/user_pairing_io.c` against the REAL input subsystem, REAL gpio-keys
driver, and REAL gpio-emul controller on native_sim with fake link
implementations of `pairing_mode_request_bonding()` /
`pairing_mode_request_reset()` recording public calls/results, shortened
hold thresholds preserving the production ordering (bond 100 ms, reset
200 ms; debounce 30 ms), and `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000` so
native_sim delayed-work deadlines stay within 1 ms of nominal.  The
`USER_PAIRING_IO_TEST` seam (`user_pairing_io_test_reset`, plus the LED
and schedule-failure fault-injection hooks) is GCOVR-excluded and never
enters production firmware; the fault injections exist because the
native_sim emulated controller never fails a `gpio_pin_set_dt()` and the
system work queue never rejects a `k_work_reschedule()`.

The threshold works are re-armed with `k_work_reschedule()` exactly as
the P2 handoff mandates ("delayed `k_work_reschedule()` normally returns
1 (nonnegative means success)"): unlike `k_work_schedule()`, reschedule
re-deadlines an item that is still submitted and schedules an item in any
state (idle, submitted, or running), so a rapid release/new hold or a
race with a still-pending item can never leave the new hold without a
fresh threshold deadline.

Every handoff case is covered (21 tests): init status reports
inactive/unpressed with the active-low LED raw-high; second init
`-EALREADY`; LED logical active/inactive produces the correct active-low
raw pin state with the ctx value ignored (NULL and non-NULL); LED GPIO
failure returns the exact errno and the cached state does not lie; press
shorter than the bonding threshold produces no request; exactly the
bonding threshold produces one BONDING while held; release after bonding
leaves one BONDING and no RESET; continuous hold reaches BONDING then
RESET exactly once each (supersedes, does not erase); release before
reset cancels the pending RESET; duplicate press/release events are
no-ops; a bounce shorter than the gpio-keys debounce produces no
threshold arm/request; stale bonding/reset work from a prior generation
cannot fire on a new hold; two complete holds produce independent
generations and one request each; a rapid release/new hold re-arms both
thresholds without a stale deadline or a missed request (hold 2 starts
immediately after hold 1's release and still reaches BONDING then RESET
exactly once with a fresh generation); held-at-init arms thresholds from
init time; events from another input device, another key code, the wrong
type, or `sync=false` produce no request; a hold-threshold scheduling
failure cancels both thresholds and clears pressed/armed state (seam);
fake P1 negative returns (including `-ECANCELED`) are surfaced in status
and never retried or duplicated; `get_status(NULL)` is safe; no
operation occurs before a successful init.

Focused verification (handoff commands): `west build --no-sysbuild -b
native_sim/native/64 -d /tmp/user-pairing-p2 tests/unit/user_pairing_io
-p -t run` → **21 PASS / 0 FAIL**, zero warnings; `west build
--no-sysbuild -b native_sim/native/64 -d /tmp/user-pairing-p1
tests/unit/pairing_mode -p -t run` → **32 PASS / 0 FAIL** (P1
regression); `scripts/test-coverage.sh --report-only --output
/tmp/user-pairing-p2-cov --clean-output` → population 35,
`user_pairing_io.c` 130/147 L, 56/106 B, 11/11 F; `python3
scripts/check-test-matrix.py --repo-root . --coverage-json
/tmp/user-pairing-p2-cov/coverage.json` → 0 errors, 0 notes; `git diff
--check` clean.

## Coverage migration (34 → 35)

Candidate generated on the clean implementation commit `495b3a7` via
`scripts/test-coverage.sh --write-baseline
/tmp/p2-baseline-candidate.json` (`--output /tmp/p2-cov-candidate
--clean-output`), inspected, and committed byte-exact as
`tests/coverage-baseline.json` (commit `184969a`).  Tool versions
unchanged (gcovr 8.4 / gcov (GCC) 14.3.0).  Only `src/user_pairing_io.c`
enters the population (34 → 35); every unchanged file stays at or above
its committed record (programmatic check: zero decreases).  New-file
record: 130/147 lines, 56/106 branches, 11/11 functions.  Aggregate:
lines 4368/4807 → 4498/4954, branches 1855/2582 → 1911/2688, functions
328/328 → 339/339.  Zero-hit enforcement clean: 339/339 — every
production function executes, including the scheduling-failure cleanup
`cancel_and_clear`, which is exercised through the test-only
schedule-failure injection because the native_sim system work queue
never rejects.  Provenance: `docs/testing/coverage-matrix.md` "P2
baseline migration".  No baseline weakening and no unexplained
denominator migration.

## Canonical gate (G1)

`./scripts/test-all.sh` on clean `184969a` → **57 PASS / 0 FAIL /
57 TOTAL**, exit 0 (33 twister + 5 exec-only + 16 Python + coverage +
matrix + BSim Stage 1; log `/tmp/p2-gate.log`).  Coverage child:
baseline enforcement 0 errors against the migrated baseline.  Matrix
child: 0 errors, 0 notes.  BSim Stage 1: all 17 scenarios strict-checked;
every existing pin byte-identical (mono 10 ms `0x22AB5C0D`, Mode A/B
10 ms `0xBAE24F7E`, 7.5 ms set, reconnect fresh mono oracle).  `git diff
--check` clean.

## Builds and build contract

- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
  only the documented pre-existing NCS v3.3.0 diagnostics
  (PARTITION_MANAGER deprecation, SW Split experimental symbols, FLPR
  `UART_CONSOLE` assigned-but-got, `BT_CTLR_ADVANCED_FEATURES` CMake
  notices) — zero new/actionable warnings from P2.
- Build contract **79/79** (`python3 scripts/check-build-contract.py
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` → 79 assertions,
  0 failed, BUILD CONTRACT PASSED).
- The three `warning:` lines in the canonical gate log are pre-existing
  in untouched suites (the `audio_stream_session` prj.conf BT buffer
  "assigned value but got ''" pair at base `2ca4fb3`, and the
  `audio_shell_nrf54` fake-fixture enum/int-mismatch) — not introduced
  by P2.

## Deviations and notes

- The handoff's "LED GPIO failure" and "threshold scheduling failure"
  cases are proven through the `USER_PAIRING_IO_TEST` fault-injection
  seams (GCOVR-excluded, never in production): the native_sim emulated
  GPIO never fails `gpio_pin_set_dt()` and the system work queue never
  rejects `k_work_reschedule()`, so the exact-errno propagation and the
  cancel-and-clear cleanup are injected rather than observed on a
  failing driver.  This mirrors the P1 fake-op rets and the repo's
  established fault-injection pattern.
- The review-fix regression (`test_rapid_release_new_hold_rearms_thresholds`)
  pins the public re-arm contract (rapid release/new hold reaches both
  thresholds exactly once with a fresh generation — no stale deadline, no
  missed request).  In the serialized test harness (SYNCHRONOUS input
  callbacks and threshold handlers share the system work queue, release
  always cancels, and duplicate presses are rejected) the works are
  always idle at a new arm, so the k_work_reschedule-vs-k_work_schedule
  divergence (schedule no-ops on delayed/queued/canceling items) is not
  deterministically reachable; the API choice itself follows the handoff
  contract and protects the production INPUT_MODE_THREAD race where the
  input callback and the threshold handlers run on different threads.
- `user_pairing_io_get_status(NULL)` is a safe no-op (documented
  contract, matching `pairing_mode_get_status`).
- The test suite runs at 1 kHz ticks and uses real bounded sleeps in
  tens of milliseconds with margins derived from the resolved tick rate
  and the 30 ms gpio-keys debounce — a test-app choice, not a production
  change.  No production Kconfig default was changed.
- No hardware tests (P8), no XIAO overlay/pin mapping or inherited DK
  button disabling (P6), no Bluetooth/policy/lifecycle/shell
  integration (P3–P5), no production board enabling — all per the
  handoff scope.

## Next-phase grounding

P3 (pairing-policy separation) can consume the already-accepted adapter
unchanged: P5 wires `user_pairing_io_init()` and
`user_pairing_io_led_set()` into the boot/lifecycle path and
`struct pairing_mode_ops`, and P6 supplies the XIAO `user-button` /
`user-led` aliases with the production `debounce-interval-ms` equal to
`CONFIG_USER_PAIRING_DEBOUNCE_MS` (the BUILD_ASSERT contract) plus the
inherited DK button disabling.
