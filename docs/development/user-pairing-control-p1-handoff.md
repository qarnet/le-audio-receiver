# P1 handoff — portable pairing-mode transition owner

Base commit: `b830018` (`docs: lock user pairing control P0 contract and planned features`).

Implement P1 from
`docs/development/user-pairing-control-plan.md`: one portable, directly tested
transition owner. Do not integrate GPIO, devicetree, Bluetooth, main, shell, or
board configuration in this phase.

## Goal

Add `src/pairing_mode.c/.h` as the sole owner of NORMAL, BONDING, RESETTING,
their asynchronous transition phases, LED patterns, supersession, completion,
and fatal recovery policy. All side effects use injected operations. Production
feature remains disabled on both boards after P1.

## In scope

- Root Kconfig definitions for portable controller and timing/work-queue
  defaults.
- Root CMake conditional source wiring.
- New direct Twister suite `tests/unit/pairing_mode` compiling production
  `src/pairing_mode.c` with fake operations.
- Test-matrix and coverage-baseline migration required by the new production
  source.
- P1 result evidence and active inventory docs.

## Out of scope

- `user_pairing_io`, `gpio-keys`, LED GPIO, devicetree aliases, or board conf.
- `bt_bap`, pairing policy, connection callbacks, advertising, settings, main,
  app lifecycle, or shell changes.
- Hardware tests.
- Enabling `CONFIG_USER_PAIRING_CONTROL` in a production board.
- NORMAL/BONDING advertising differentiation research beyond the already
  committed `PLANNED_FEATURES.md`.

## Public API

Use a singleton controller, matching the app's singleton Bluetooth/advertising
instance. Public header must not include Bluetooth, GPIO, or devicetree types.

```c
enum pairing_mode {
	PAIRING_MODE_NORMAL = 0,
	PAIRING_MODE_BONDING,
	PAIRING_MODE_RESETTING,
};

enum pairing_mode_phase {
	PAIRING_MODE_PHASE_UNINITIALIZED = 0,
	PAIRING_MODE_PHASE_IDLE,
	PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING,
	PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET,
	PAIRING_MODE_PHASE_RESET_FEEDBACK,
	PAIRING_MODE_PHASE_FATAL,
};

enum pairing_access_mode {
	PAIRING_ACCESS_NORMAL = 0,
	PAIRING_ACCESS_BONDING,
	PAIRING_ACCESS_SUSPENDED,
};

struct pairing_mode_ops {
	int (*set_access_mode)(enum pairing_access_mode mode, void *ctx);
	int (*advertising_suspend)(void *ctx);
	int (*advertising_start)(void *ctx);
	int (*disconnect_peer)(bool *pending, void *ctx);
	int (*delete_all_bonds)(void *ctx);
	int (*request_security)(void *ctx);
	int (*led_set)(bool active, void *ctx);
	void (*cold_reboot)(void *ctx);
};

struct pairing_mode_status {
	enum pairing_mode mode;
	enum pairing_mode_phase phase;
	enum pairing_access_mode access_mode;
	uint32_t transition_generation;
	bool connected;
	bool led_active;
	bool initialized;
	bool fatal;
};

int pairing_mode_init(const struct pairing_mode_ops *ops, void *ctx);
int pairing_mode_start(void);
int pairing_mode_request_bonding(void);
int pairing_mode_request_reset(void);
int pairing_mode_request_reset_sync(k_timeout_t timeout);
int pairing_mode_notify_connected(void);
int pairing_mode_notify_disconnected(void);
int pairing_mode_notify_pairing_complete(bool bonded);
int pairing_mode_notify_pairing_failed(void);
int pairing_mode_notify_security_changed(bool success, bool bonded);
void pairing_mode_get_status(struct pairing_mode_status *status);
```

Minor naming changes are allowed only for consistency. Preserve semantics.
`pairing_mode_get_status(NULL)` must be safe or return an error through a typed
API; decide once and test it.

## Event serialization

Use one private Zephyr work queue owned by the controller. Public request and
notification functions may be called from different thread/workqueue contexts;
they enqueue events and never execute platform operations inline.

Requirements:

- No user callback/BT callback ever becomes a transition owner.
- RESET has priority over BONDING when both are pending.
- Idempotent duplicate notifications may collapse, but no required RESET,
  disconnect, completion, or fatal event may be lost.
- Transition generation rejects stale delayed reset/LED work.
- Work-queue submission failure or impossible state is fatal.
- Do not block the controller work queue waiting for disconnect.
- Synchronous shell-style reset waits on a completion object from the caller's
  thread; it must not hold controller locks while waiting.

An atomic pending-event mask plus one drain work item is acceptable because
events are idempotent and RESET priority is explicit. If using a queue, bound it
and define overflow as fatal. Document chosen event ordering in source.

## State transitions

### Start

`pairing_mode_start()`:

1. Set access NORMAL.
2. Force LED inactive.
3. Start advertising.
4. End in NORMAL/IDLE.

Any failure enters FATAL and invokes cold reboot exactly once.

### Request BONDING

1. Increment transition generation.
2. Suspend advertising.
3. Set access SUSPENDED.
4. Ask `disconnect_peer(&pending)`.
5. If pending, enter WAIT_DISCONNECT_FOR_BONDING and stop.
6. If no peer, finish immediately: set access BONDING, mode BONDING, start LED
   slow pattern, start advertising, phase IDLE.

Duplicate BONDING while already BONDING and not connected is a no-op. A request
while connected restarts the disconnect transition.

### Connected notification

- Mark connected.
- In BONDING/IDLE, call `request_security()` from controller work context.
- In NORMAL, perform no operation.
- During a suspended/wait/reset phase, treat connection as stale: suspend and
  disconnect again; never allow it to advance the wrong transition.

Security-request failure is fatal.

### Successful pairing/security

- `pairing_complete(bonded=true)` in BONDING completes NORMAL.
- `security_changed(success=true, bonded=true)` in BONDING also completes
  NORMAL for a previously bonded peer.
- Completion sets access NORMAL and LED inactive but does not suspend
  advertising, start advertising, or disconnect; the active connection stays.
- Non-bonded pairing completion or unsuccessful security does not complete.
- Pairing failure remains BONDING and is not fatal.

### Request RESET

1. RESET supersedes every BONDING transition and increments generation.
2. Suspend advertising and set access SUSPENDED.
3. Ask for disconnect.
4. If pending, enter WAIT_DISCONNECT_FOR_RESET.
5. Once disconnected, delete all bonds.
6. Enter RESETTING/RESET_FEEDBACK.
7. Run 100 ms half-period LED pattern for 1000 ms.
8. At reset-feedback expiry, set access BONDING, enter BONDING, start the slow
   LED pattern, start advertising, phase IDLE.
9. Complete every synchronous reset waiter only after BONDING advertising starts.

Delete bonds exactly once per accepted reset generation. Never delete before
disconnect completion.

### Disconnected notification

- Clear connected.
- WAIT_DISCONNECT_FOR_BONDING finishes BONDING entry.
- WAIT_DISCONNECT_FOR_RESET performs deletion and reset feedback.
- NORMAL/IDLE or BONDING/IDLE leaves advertising decisions to a future P4/P5
  integration event; P1 must not invent an automatic restart beyond an active
  transition.
- Stale/duplicate disconnect is a no-op.

## LED behavior

The controller owns logical pattern timing through `ops->led_set`:

- NORMAL: inactive.
- BONDING: immediately active on entry, then toggle every configured 500 ms.
- RESETTING: immediately active, toggle every configured 100 ms, exactly ten
  half-periods / five complete flashes at production defaults.
- On reset expiry, cancel/reset rapid work before starting BONDING pattern.
- Transition generation prevents stale blink work changing a newer mode.
- Any mandatory LED-set failure is fatal.

The fake tests use short Kconfig values while preserving ratios and complete
cycles.

## Fatal recovery

One private finalizer logs operation, errno, mode, phase, access mode,
connected flag, and transition generation; sets FATAL; attempts LED inactive;
and invokes `cold_reboot` once. If the fake reboot callback returns, no later
event may execute platform operations.

Do not call cold reboot for remote pairing failure or unsuccessful security
notification. Do call it for operation errors and impossible internal states.

## Kconfig

Add definitions with feature default `n`:

```text
USER_PAIRING_CONTROL
USER_PAIRING_BOND_HOLD_MS=3000
USER_PAIRING_RESET_HOLD_MS=8000
USER_PAIRING_BOND_LED_HALF_PERIOD_MS=500
USER_PAIRING_RESET_LED_HALF_PERIOD_MS=100
USER_PAIRING_RESET_FEEDBACK_MS=1000
USER_PAIRING_WORKQ_STACK_SIZE
USER_PAIRING_WORKQ_PRIORITY
```

P1 does not add `USER_PAIRING_INPUT`; P2 owns that symbol and debounce.

Validate relational timing in C with BUILD_ASSERT because Kconfig cannot express
all relationships:

- reset threshold > bonding threshold;
- reset feedback divisible by reset half-period;
- at least two reset half-periods;
- all timings nonzero.

## Direct tests

Create one production-source Twister suite. Tests assert public state and fake
operation ledger, not private fields/helper calls.

Required cases:

- invalid/missing ops rejected atomically;
- init status; start success and idempotence;
- start failures at set-access, LED, and advertising stages reboot once;
- bonding no-peer exact operation order;
- bonding connected waits for disconnect;
- duplicate bonding no-peer no-op;
- reset no-peer exact order;
- reset connected deletes only after disconnect;
- reset supersedes pending bonding disconnect;
- reset supersedes active bonding;
- reset feedback has exact rapid LED sequence and delayed BONDING start;
- slow blink starts active and follows exact half-periods;
- stale rapid/slow work cannot modify a newer generation;
- connected in BONDING requests security;
- connected during suspended phase is disconnected again;
- new bonded pairing enters NORMAL without disconnect/start;
- bonded secure reconnect enters NORMAL without disconnect/start;
- pairing failed remains BONDING;
- failed/nonbonded security remains BONDING;
- security request failure reboots;
- each platform operation failure reboots once;
- after fatal, later events cause no operations;
- reset-sync returns only after BONDING advertisement is active;
- reset-sync timeout is reported without corrupting the transition;
- duplicate disconnect is harmless;
- RESET priority over concurrently pending BONDING.

## Matrix and coverage

- Add `src/pairing_mode.c` as direct/stateful in `tests/test-matrix.json` with
  exact outcomes and transition witnesses.
- New Twister suite raises canonical children from 55 to 56.
- New numeric source raises population from 33 to 34.
- Follow committed coverage migration protocol: implementation/tests commit
  first with old baseline, generate candidate on clean commit, inspect all
  unchanged-file ratios and new-file zero-hit functions, then copy candidate in
  a separate coverage commit with provenance in `coverage-matrix.md`.
- No baseline weakening and no unexplained denominator migration.

## Verification

Focused:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/user-pairing-p1 tests/unit/pairing_mode -p -t run
scripts/test-coverage.sh --report-only --output /tmp/user-pairing-p1-cov --clean-output
python3 scripts/check-test-matrix.py --repo-root . \
  --coverage-json /tmp/user-pairing-p1-cov/coverage.json
git diff --check
```

After baseline migration, run canonical `scripts/test-all.sh`; expect 56 children,
all existing BSim pins unchanged, builds 3/3, and build contract unchanged at
79/79 because production feature remains disabled.

## Commit shape

1. P1 implementation + direct tests + matrix/inventory updates.
2. Coverage baseline migration and provenance.
3. P1 results/acceptance docs after focused and canonical gates.

No push, PR, amend, force push, or attribution footer.
