# P2 handoff — generic input and LED adapter

Base commit: `2ca4fb3` (P1 accepted; worktree clean).

Implement P2 from
`docs/development/user-pairing-control-plan.md`: a reusable Zephyr
`gpio-keys`/GPIO LED adapter that translates one debounced user button into the
already-accepted `pairing_mode` requests. Do not integrate Bluetooth, main,
shell, production board overlays, or production board configs in this phase.

## Goal

Add `src/user_pairing_io.c/.h` with no board-number conditionals. Hardware is
selected only through `user-button` and `user-led` devicetree aliases. Button
events execute no transition inline; they only schedule/cancel threshold work
and call the P1 request APIs when thresholds mature.

## In scope

- `CONFIG_USER_PAIRING_INPUT` and debounce configuration.
- Root CMake conditional wiring.
- DT-alias-based production adapter.
- Direct native GPIO/input-emulation Twister suite compiling production source.
- Matrix/inventory and coverage migration for one new production file.
- P2 results and active inventory docs.

## Out of scope

- Production XIAO overlay/pin mapping and inherited DK button disabling (P6).
- Enabling P1/P2 in any production board config.
- Bluetooth, policy, callbacks, lifecycle, shell, or advertising.
- Hardware tests.
- Changes to P1 state/event semantics.

## Devicetree contract

When `CONFIG_USER_PAIRING_INPUT=y`, these aliases are mandatory:

```dts
/ {
	aliases {
		user-button = &user_button_node;
		user-led = &user_led_node;
	};
};
```

Requirements:

- `user-button` references a child of a `gpio-keys` node.
- Button node has `gpios` and `zephyr,code`.
- `user-led` references a `gpio-leds` child with `gpios`.
- Active polarity and pull configuration come exclusively from DT.
- Module obtains the input device through `DEVICE_DT_GET(DT_PARENT(DT_ALIAS(user_button)))`.
- Static `INPUT_CALLBACK_DEFINE` registration filters the gpio-keys device;
  callback body filters `INPUT_EV_KEY`, selected `zephyr,code`, and `sync`.
  (`struct input_event` has only a `sync` batch-end flag, not a separate
  "complete" concept; gpio-keys reports each key event with `sync=true`.)
- Add compile-time alias/node/property assertions with useful errors.

## Public API

Use a singleton matching the one physical control pair:

```c
struct user_pairing_io_status {
	bool initialized;
	bool pressed;
	bool led_active;
	uint32_t hold_generation;
	bool bonding_threshold_armed;
	bool reset_threshold_armed;
	int last_error;
};

int user_pairing_io_init(void);
int user_pairing_io_led_set(bool active, void *ctx);
void user_pairing_io_get_status(struct user_pairing_io_status *status);
```

`user_pairing_io_led_set` deliberately matches `pairing_mode_ops.led_set` and
ignores its context value: P1 has one shared operation context, so this callback
must accept either NULL or non-NULL. A minor API naming change is acceptable
for style, not semantics. `get_status(NULL)` must be a documented safe no-op,
matching the P1 status convention.

No public API may expose GPIO devices, pins, input events, delayed work, or
board identifiers.

## Initialization

1. Reject a second init with `-EALREADY`.
2. Verify input device, button GPIO controller, and LED GPIO controller ready.
3. Configure LED with `GPIO_OUTPUT_INACTIVE`; NORMAL's physical default is off.
4. Read current logical button state through `gpio_pin_get_dt()`.
5. Publish initialized state only after all mandatory setup succeeds.
6. If button is already active at initialization, treat initialization time as
   the debounced press edge and arm both thresholds. This supports holding the
   user button during boot without special board code.

Any init failure returns the exact errno and leaves `initialized=false`, LED
inactive where possible, and no threshold work armed. P5 treats a production
init failure as fatal boot failure. Initialization occurs from application
thread after Zephyr POST_KERNEL input initialization; do not add an earlier
`SYS_INIT`, because gpio-keys configures and samples its GPIO at
`CONFIG_INPUT_INIT_PRIORITY` (default 90).

## Button event behavior

The `gpio-keys` driver owns electrical edge handling and debounce. This module
must not add a second debounce timer.

### Press

- Ignore duplicate press when already pressed.
- Increment nonzero `hold_generation` (wrap zero to one).
- Set pressed true.
- Capture generation separately for bonding and reset delayed work.
- Schedule bonding threshold at `CONFIG_USER_PAIRING_BOND_HOLD_MS`.
- Schedule reset threshold at `CONFIG_USER_PAIRING_RESET_HOLD_MS`.
- If either scheduling call returns negative, cancel both, clear
  pressed/armed state, record `last_error`, and `LOG_ERR` exact operation and
  errno. Do not invent a second fatal owner: P1 exposes no public fatal event,
  and delayed `k_work_reschedule()` normally returns 1 (nonnegative means
  success). Do not retry.

### Bonding threshold

- Recheck initialized, pressed, armed flag, and captured generation.
- Clear only bonding armed flag.
- Call `pairing_mode_request_bonding()` exactly once.
- `-ECANCELED` after P1 fatal is logged but causes no duplicate recovery.
- Any other unexpected negative return is an error surfaced in status/logs;
  do not retry or call transition operations directly.

### Reset threshold

- Recheck initialized, pressed, armed flag, and captured generation.
- Clear both armed flags.
- Cancel pending bonding work if still armed.
- Call `pairing_mode_request_reset()` exactly once.
- This remains valid if BONDING already fired at 3 seconds; P1 RESET priority
  supersedes it.

### Release

- Ignore duplicate release when not pressed.
- Set pressed false.
- Increment generation, invalidating both captured generations.
- Clear armed flags and cancel both delayable works.
- Never request a mode on release.
- Release after 3 seconds does not undo BONDING.

`k_work_cancel_delayable()` returns a work busy-status bitmask, not errno.
Cancellation is best-effort generation invalidation; never log its nonzero
return as failure and never use blocking cancel from input callback context.

At threshold/release races, generation + pressed + armed checks define one
observable action. No stale work from a prior hold may call P1.

## LED operation

- `user_pairing_io_led_set(active, ctx)` calls `gpio_pin_set_dt()` with a
  logical value; DT handles active-low/high polarity.
- Update cached `led_active` only after GPIO success.
- Return exact GPIO errno on failure so P1 invokes its accepted fatal reboot
  path.
- Calls are thread-safe between input callback, threshold work, controller
  work, and status readers using a short spinlock. Do not use a mutex: Zephyr
  can build input callbacks in synchronous/ISR context. Never hold spinlock
  across GPIO, P1 request, logging, or work-cancel calls.

## Kconfig

Add:

```text
config USER_PAIRING_INPUT
	bool "User button and LED pairing control"
	default n
	depends on USER_PAIRING_CONTROL && INPUT
	depends on DT_HAS_GPIO_KEYS_ENABLED
	select GPIO

config USER_PAIRING_DEBOUNCE_MS
	int "User pairing button debounce interval"
	default 30
	range 1 1000
	depends on USER_PAIRING_INPUT
```

`USER_PAIRING_DEBOUNCE_MS` is a board/devicetree contract value: P6 sets the
gpio-keys node's `debounce-interval-ms` to the same production default and build
contract checks equality. Add a source `BUILD_ASSERT` that selected gpio-keys
node's resolved `debounce-interval-ms` equals this Kconfig value. P2 test
overlay uses its suite value. Do not create a second debounce timer.

P1 already owns hold and LED timing symbols. Do not duplicate them.

## Test architecture

Add `tests/unit/user_pairing_io` as a Twister suite compiling the real
`src/user_pairing_io.c`.

Use:

- `gpio-emul` controller;
- a `gpio-keys` node with active-low/pull-up button and short debounce;
- a `gpio-leds` child with active-low LED;
- `user-button` / `user-led` aliases;
- real input subsystem/gpio-keys driver;
- fake link implementations of `pairing_mode_request_bonding()` and
  `pairing_mode_request_reset()` that record public calls/results;
- shortened hold thresholds that preserve reset > bonding.

Avoid copied driver models and private-field assertions.

Required public-boundary tests:

- alias/input/LED devices initialize and status reports inactive/unpressed;
- second init returns `-EALREADY`;
- LED logical active/inactive produces correct active-low raw pin state;
- LED GPIO failure returns errno and cached state does not lie;
- press shorter than bonding threshold produces no request;
- exactly bonding threshold produces one BONDING request while held;
- release after bonding threshold leaves one BONDING request and no RESET;
- continuous hold reaches BONDING then RESET exactly once each;
- reset threshold supersedes but does not erase the observed earlier BONDING
  request;
- release before reset cancels RESET;
- duplicate press/release events are no-ops;
- bounce shorter than gpio-keys debounce produces no threshold arm/request;
- stale bonding/reset work from prior generation cannot fire on a new hold;
- two complete holds produce independent generations and one request each;
- held-at-init arms thresholds from init time;
- events from another input device, another key code, wrong type, or
  `sync=false` produce no request;
- fake P1 negative returns do not retry or duplicate requests;
- `get_status(NULL)` is safe;
- no operation occurs before successful init.

Tests may use bounded real sleeps with suite timings in tens of milliseconds;
keep margins based on resolved tick rate and gpio-keys debounce. Prefer event
semaphores over arbitrary long sleeps.

Ground test shape in NCS v3.3.0 upstream
`zephyr/tests/drivers/input/gpio_keys/`: drive logical input with
`gpio_emul_input_set[_dt]()`, verify no event at half debounce, then wait beyond
full trailing-edge debounce. Read physical LED output with
`gpio_emul_output_get[_dt]()`. Ensure emulated GPIO supports rising and falling
edge interrupts; stock native_sim gpio0 already does.

## Matrix and coverage

- Add `src/user_pairing_io.c` direct entry with exact outcomes/witnesses.
- Suite count: 32 -> 33 Twister; canonical 56 -> 57 children.
- Numeric population: 34 -> 35 files.
- Follow coverage migration protocol in a separate commit. Every function must
  execute; no unchanged-file ratio decreases.
- Feature remains disabled in production board configs; production builds and
  build contract stay 79/79 in P2.

## Verification

Focused:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/user-pairing-p2 tests/unit/user_pairing_io -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/user-pairing-p1 tests/unit/pairing_mode -p -t run
scripts/test-coverage.sh --report-only \
  --output /tmp/user-pairing-p2-cov --clean-output
python3 scripts/check-test-matrix.py --repo-root . \
  --coverage-json /tmp/user-pairing-p2-cov/coverage.json
git diff --check
```

After baseline migration, run canonical gate; expect 57/57, unchanged BSim
pins, builds 3/3, contract 79/79, zero actionable warnings.

## Commit shape

1. P2 handoff.
2. Production adapter + direct tests + matrix/inventory.
3. Coverage migration.
4. P2 results/acceptance.

No push, PR, amend, force push, or attribution footer.
