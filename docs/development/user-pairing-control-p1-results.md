# P1 results — portable pairing-mode transition owner

Accepted: 2026-08-07.  Base commit `b830018` (P0 contract lock); handoff
commit `4dc0b72` (`docs: record P1 handoff — portable pairing-mode
transition owner`); implementation commit `14be974`; coverage migration
commit `2ccbb44`; acceptance commit (this document's commit).  Handoff:
`docs/development/user-pairing-control-p1-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P1 delivers one portable, directly tested transition owner
(`src/pairing_mode.c/.h`) with no GPIO, Bluetooth, main, shell, or
board integration.  The production feature stays disabled on both
boards: `CONFIG_USER_PAIRING_CONTROL` is `n` in every production board
config, so `zephyr_sources_ifdef` never compiles the new source into
firmware.  Builds 3/3, build contract 79/79, all existing BSim pins
byte-identical, and the canonical gate grew 55 → 56 children exactly as
the handoff specified.

## Files and public API

- `src/pairing_mode.h` — public singleton controller API.  Contains no
  Bluetooth, GPIO, or devicetree types (only `<zephyr/kernel.h>` for
  `k_timeout_t`).
  - `enum pairing_mode` — NORMAL / BONDING / RESETTING.
  - `enum pairing_mode_phase` — UNINITIALIZED / IDLE /
    WAIT_DISCONNECT_FOR_BONDING / WAIT_DISCONNECT_FOR_RESET /
    RESET_FEEDBACK / FATAL.
  - `enum pairing_access_mode` — NORMAL / BONDING / SUSPENDED.
  - `struct pairing_mode_ops` — `set_access_mode`, `advertising_suspend`,
    `advertising_start`, `disconnect_peer(bool *pending)`,
    `delete_all_bonds`, `request_security`, `led_set`, `cold_reboot`;
    every callback takes the injected `void *ctx`.
  - `struct pairing_mode_status` — mode, phase, access_mode,
    transition_generation, connected, led_active, initialized, fatal.
  - `pairing_mode_init` (0 / -EINVAL atomic / -EALREADY),
    `pairing_mode_start` (0 / -EINVAL / -ECANCELED),
    `pairing_mode_request_bonding`, `pairing_mode_request_reset`,
    `pairing_mode_request_reset_sync(k_timeout_t)` (0 / -ETIMEDOUT /
    -EBUSY / -EINVAL / -ECANCELED), `pairing_mode_notify_connected`,
    `pairing_mode_notify_disconnected`,
    `pairing_mode_notify_pairing_complete(bool)`,
    `pairing_mode_notify_pairing_failed`,
    `pairing_mode_notify_security_changed(bool,bool)`, and
    `pairing_mode_get_status` (documented NULL-safe no-op).
- `src/pairing_mode.c` — the controller: one private work queue, the
  pending-event mask + drain, all transition logic, LED pattern timing,
  the single-slot synchronous-reset waiter, and the fatal finalizer.
- Root `Kconfig` — `USER_PAIRING_CONTROL` (default `n`, `select EVENTS`)
  and the timing/work-queue symbols (`BOND_HOLD_MS=3000`,
  `RESET_HOLD_MS=8000`, `BOND_LED_HALF_PERIOD_MS=500`,
  `RESET_LED_HALF_PERIOD_MS=100`, `RESET_FEEDBACK_MS=1000`,
  `WORKQ_STACK_SIZE=2048`, `WORKQ_PRIORITY=5`), all `depends on
  USER_PAIRING_CONTROL`.  Relational timing is validated in C with
  BUILD_ASSERT (reset threshold > bonding threshold; feedback divisible
  by the reset half-period; at least two reset half-periods; all timings
  nonzero; minimum work-queue stack).
- Root `CMakeLists.txt` — `zephyr_sources_ifdef(CONFIG_USER_PAIRING_CONTROL
  src/pairing_mode.c)`.
- `tests/unit/pairing_mode/` — the direct twister suite.
- `scripts/check-test-matrix.py` — `PAIRING_MODE_TEST` added to the
  test-only macro set (seam stripped from public-API discovery).
- `tests/test-matrix.json` — stateful direct entry for
  `src/pairing_mode.c` with 33 exact outcomes and 11 transition
  witnesses.
- `docs/testing/coverage-matrix.md` — source row + suite inventory
  (twister 31 → 32; gate 55 → 56) in the implementation commit, and the
  "P1 baseline migration" provenance in the coverage commit.

## Event serialization and state/event ordering (documented contract)

- One private work queue (`k_work_queue_start`, stack/priority from
  Kconfig).  Public request/notification functions are callable from any
  thread or work-queue context; they set bits in an atomic pending mask
  and submit ONE drain work item.  No user/BT callback ever becomes a
  transition owner.
- The drain consumes the whole mask per pass and processes bits in
  strict priority order: RESET (incl. sync) > BONDING > CONNECTED >
  DISCONNECTED > PAIRING_COMPLETE > SECURITY_CHANGED > PAIRING_FAILED >
  START.  RESET therefore wins over a concurrently pending BONDING, and
  a RESET supersedes a mid-flight BONDING transition by bumping the
  transition generation (stale delayed LED/feedback work from the older
  generation is rejected when it fires).
- Notifications with boolean payloads collapse to the latest value;
  idempotent duplicates may collapse, but no required RESET, disconnect,
  completion, or fatal event is lost (every set bit is consumed; an
  initiated reset is never dropped).
- The drain never blocks: a transition needing a disconnect enters
  WAIT_DISCONNECT_* and returns; the matching disconnected notification
  advances it.  Bond deletion happens exactly once per accepted reset,
  strictly after disconnect completion.
- Synchronous reset (`request_reset_sync`) registers a single-slot
  waiter and blocks the CALLER on a `k_event` until BONDING advertising
  starts; no controller lock is held while waiting (a second concurrent
  waiter gets -EBUSY; a timeout returns -ETIMEDOUT and the transition
  continues in the background).  Waiter signaling happens only in the
  reset-feedback expiry path, after `advertising_start`.
- Work-queue submission failure or an impossible pending-mask state is
  fatal: one finalizer logs op/errno/mode/phase/access/connected/
  generation, sets FATAL, attempts LED-inactive, and invokes
  `cold_reboot` exactly once (CAS-guarded).  After fatal, no later event
  executes any platform operation (APIs return -ECANCELED).  Remote
  pairing failure and unsuccessful/non-bonded security are never fatal.
- LED pattern timing: NORMAL inactive; BONDING active on entry then a
  toggle per bond half-period; RESETTING active on entry then
  (FEEDBACK/HALF − 1) rapid toggles — nine at production defaults
  (100…900 ms), completing five full flashes — with the reset-feedback
  expiry work at RESET_FEEDBACK_MS as the authoritative end (it cancels
  the rapid work and re-arms the BONDING pattern).  This interpretation
  keeps every rapid toggle strictly before the expiry deadline so no
  same-deadline work-queue ordering can corrupt the count; it is
  documented in the source module comment and pinned by the direct
  tests.

## Tests (32 direct, new twister suite)

`tests/unit/pairing_mode` compiles the production `src/pairing_mode.c`
against fake injected operations: a spinlock-protected operation ledger
with controller-side timestamps, per-op event semaphores, optional
blocking op gates (to hold the controller mid-transition), and short
tick-aligned Kconfig values preserving the production ratios (bond half
100 ms, reset half 20 ms, feedback 200 ms = ten half-periods / five
complete flashes; bond hold 250 ms, reset hold 500 ms).  The suite runs
at `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000` because native_sim lands every
delayed-work deadline one tick late (measured at 100 Hz: a 20 ms period
fires at 30 ms) — 1 ms ticks keep the cadence exact.  The
`PAIRING_MODE_TEST` seam (`pairing_mode_test_reset`) resets all
file-static state between tests; it is GCOVR-excluded and never enters
production firmware.

Every handoff case is covered (32 tests): invalid/missing ops rejected
atomically; init status + `-EALREADY` + uninitialized APIs `-EINVAL`;
start success and idempotence; start failures at set-access/LED/
advertising reboot once; bonding no-peer exact op order; bonding
connected waits for disconnect; duplicate bonding no-peer no-op; reset
no-peer exact op order; reset connected deletes bonds only after
disconnect; reset supersedes pending bonding disconnect; reset
supersedes active bonding; reset feedback exact rapid LED sequence and
delayed BONDING start; slow blink starts active and follows exact
half-periods; stale rapid/slow work cannot modify a newer generation;
connected in BONDING requests security; connected during suspended
phase is disconnected again; new bonded pairing enters NORMAL without
disconnect/start; bonded secure reconnect enters NORMAL without
disconnect/start; pairing failure remains BONDING; failed/non-bonded
security remains BONDING; security-request failure reboots; every
platform-op failure reboots once (table-driven); after fatal later
events cause no operations; reset-sync returns only after BONDING
advertising is active (gated advertising op); reset-sync timeout is
reported without corrupting the transition; second synchronous waiter
`-EBUSY`; duplicate disconnect harmless; RESET priority over
concurrently pending BONDING; `get_status(NULL)` safe.

Focused verification (handoff commands): `west build --no-sysbuild -b
native_sim/native/64 -d /tmp/user-pairing-p1 tests/unit/pairing_mode -p
-t run` → **32 PASS / 0 FAIL**, zero warnings; `scripts/test-coverage.sh
--report-only --output /tmp/user-pairing-p1-cov --clean-output` →
population 34, `pairing_mode.c` 344/405 L, 160/226 B, 39/39 F;
`python3 scripts/check-test-matrix.py --repo-root . --coverage-json
/tmp/user-pairing-p1-cov/coverage.json` → 0 errors; `git diff --check`
clean.

## Coverage migration (33 → 34)

Candidate generated on the clean implementation commit `14be974` via
`scripts/test-coverage.sh --write-baseline
/tmp/p1-baseline-candidate.json`, inspected, and committed byte-exact as
`tests/coverage-baseline.json` (commit `2ccbb44`).  Tool versions
unchanged (gcovr 8.4 / gcov (GCC) 14.3.0).  Only `src/pairing_mode.c`
enters the population (33 → 34); every unchanged file stays at or above
its committed record (programmatic check: zero decreases).  New-file
record: 344/405 lines, 160/226 branches, 39/39 functions.  Aggregate:
lines 4024/4402 → 4368/4807, branches 1695/2356 → 1855/2582, functions
289/289 → 328/328.  Provenance: `docs/testing/coverage-matrix.md` "P1
baseline migration".  No baseline weakening and no unexplained
denominator migration.

## Canonical gate (G1)

`./scripts/test-all.sh` on clean `2ccbb44` → **56 PASS / 0 FAIL /
56 TOTAL**, exit 0 (32 twister + 5 exec-only + 16 Python + coverage +
matrix + BSim Stage 1; log `/tmp/p1-gate.log`, elapsed 18m27s).
Coverage child: baseline enforcement 0 errors against the migrated
baseline.  Matrix child: 0 errors.  BSim Stage 1: all 17 scenarios
strict-checked; every existing pin byte-identical (mono 10 ms
`0x22AB5C0D`, Mode A/B 10 ms `0xBAE24F7E`, 7.5 ms set, reconnect fresh
mono oracle, `duplicate_release_10ms`).  `git diff --check` clean.

## Builds and build contract

- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
  only the documented pre-existing NCS v3.3.0 diagnostics
  (PARTITION_MANAGER deprecation, SW Split experimental symbols,
  FLPR-image `UART_CONSOLE` assigned-but-got, nRF54L15 DTS/memory
  notices) — zero new/actionable warnings, and zero warnings from the
  new suite or changed files in any gate build.
- Build contract **79/79** (`python3 scripts/check-build-contract.py
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` → 79 assertions,
  0 failed, BUILD CONTRACT PASSED).

## Deviations and notes

- The handoff's rapid-LED requirement "exactly ten half-periods / five
  complete flashes" is implemented as (FEEDBACK/HALF − 1) toggles (nine
  at defaults) plus the authoritative feedback-expiry re-arm — five
  complete flashes over exactly RESET_FEEDBACK_MS with no
  same-deadline work-queue race.  Documented in source and pinned by
  the tests.
- `pairing_mode_get_status(NULL)` is a safe no-op (decided and tested).
- The test suite runs at 1 kHz ticks (see above) — a test-app Kconfig
  choice, not a production change.
- No hardware tests (P8), no `USER_PAIRING_INPUT` (P2), no
  Bluetooth/policy/lifecycle/shell integration (P3–P5), no production
  board enabling — all per the handoff scope.

## Next-phase grounding

P2 (`user_pairing_io.c/.h`, gpio-keys input + LED GPIO + DT aliases)
can consume the already-defined `USER_PAIRING_BOND_HOLD_MS` /
`USER_PAIRING_RESET_HOLD_MS` thresholds (the relational BUILD_ASSERTs
live in the P1 controller) and drive `pairing_mode_request_bonding() /
request_reset()`.  P3–P5 wire `struct pairing_mode_ops` to
`bt_pairing_policy` / `bt_bap` / lifecycle / shell, including routing
`bt unpair` through `pairing_mode_request_reset_sync()`.  P6 enables
`CONFIG_USER_PAIRING_CONTROL` in the nRF54L15 board conf only; P8 runs
the hardware acceptance matrix.
