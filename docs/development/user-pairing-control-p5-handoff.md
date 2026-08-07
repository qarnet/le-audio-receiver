# P5 handoff — lifecycle, disconnect restart, and shell integration

Base commit: `be18b60` (P4 accepted; worktree clean).

Implement P5 from `docs/development/user-pairing-control-plan.md`. Build the
full pairing-mode operation table, initialize controller + user I/O in fatal
boot order, make matching idle disconnects restart advertising through the
controller, and route feature-enabled `bt unpair` through synchronous RESET.
Production configs remain disabled until P6; feature-off nRF5340 behavior and
legacy shell output remain unchanged.

## Integration gate

Use `CONFIG_USER_PAIRING_INPUT` as the full-stack integration gate. Kconfig
already requires `USER_PAIRING_CONTROL`; a CONTROL-only scratch/test build must
still compile P1/P4 without requiring DT aliases or changing main/shell
ownership. P6 enables INPUT + CONTROL together only on nRF54L15.

## Fixed boot shape

Keep `app_lifecycle` generic and its accepted fatal sequence. Do not add a
second coordinator. Replace only main's final `advertising_start` adapter under
the full-stack gate:

```c
static int pairing_control_start(void)
{
	static const struct pairing_mode_ops pairing_ops = {
		.set_access_mode = bt_bap_pairing_set_access_mode,
		.advertising_suspend = bt_bap_pairing_advertising_suspend,
		.advertising_start = bt_bap_pairing_advertising_start,
		.disconnect_peer = bt_bap_pairing_disconnect_peer,
		.delete_all_bonds = bt_bap_pairing_delete_all_bonds,
		.request_security = bt_bap_pairing_request_security,
		.led_set = user_pairing_io_led_set,
		.cold_reboot = pairing_cold_reboot,
	};
	...
}
```

Exact order inside callback:

1. `pairing_mode_init(&pairing_ops, NULL)`;
2. `user_pairing_io_init()`;
3. `bt_bap_pairing_notifications_enable()`;
4. `pairing_mode_start()`.

Return first exact errno. Notification gate must not open after either init
failure. `pairing_mode_start()` is enqueue-only; accepted P1 owns asynchronous
platform failure and cold reboot. Main's lifecycle callback returns its enqueue
result. `pairing_cold_reboot(void *ctx)` ignores context and calls
`sys_reboot(SYS_REBOOT_COLD)`.

`app_lifecycle_ops.advertising_start` points to `pairing_control_start` only
under `CONFIG_USER_PAIRING_INPUT`; otherwise it remains
`bt_bap_restart_advertising` adapter. Preserve watchdog→BT→settings→volume→BAP
→sink→platform→final-access-start order and all generic lifecycle tests.

## Idle disconnect restart ownership

P1 currently treats NORMAL/IDLE and BONDING/IDLE disconnect as no-op. That was
safe before concrete callback identity existed but cannot replace main-loop
restart. P4 forwards exactly one matching `default_conn` disconnect after
teardown and ref cleanup, so P5 changes P1 behavior:

- capture `was_connected` before clearing status;
- duplicate/stale disconnect when `was_connected == false`: no-op;
- WAIT_DISCONNECT_FOR_BONDING: existing finish-BONDING path unchanged;
- WAIT_DISCONNECT_FOR_RESET: existing reset-feedback path unchanged;
- NORMAL/IDLE with `was_connected`: call injected `advertising_start()` once;
- BONDING/IDLE with `was_connected`: call injected `advertising_start()` once;
- restart failure is fatal through a new explicit operation ID/log context;
- no mode/access/LED/generation mutation during idle restart.

This restarts BONDED_ONLY advertising after normal peer disconnect and OPEN
advertising after pairing failure/remote disconnect in BONDING. Stale duplicate
disconnect can never restart wrong mode.

Add direct P1 tests proving NORMAL and BONDING idle restart once, stale
duplicate no-op, exact failure→single cold reboot, and wait-phase paths do not
double-start.

## Main loop

Under `CONFIG_USER_PAIRING_INPUT`, replace legacy disconnect-wait/restart loop
with a passive forever sleep (`k_sleep(K_FOREVER)` loop). Bluetooth callback →
P1 notification now owns restart. Do not consume `sem_disconnected` or call
`app_lifecycle_restart_advertising()` in this branch.

Feature-off branch remains byte-for-byte behavior-equivalent: wait semaphore,
log restart, call lifecycle restart, reboot on failure.

## Shell

Add Kconfig:

```text
config USER_PAIRING_SHELL_RESET_TIMEOUT_MS
	int "Pairing reset shell timeout (ms)"
	default 15000
	range 1000 120000
	depends on USER_PAIRING_INPUT && SHELL
```

Default exceeds disconnect + 1-second feedback + advertising completion with
ample controller margin.

Under `CONFIG_USER_PAIRING_INPUT`, `bt unpair` calls:

```c
pairing_mode_request_reset_sync(
	K_MSEC(CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS));
```

Exact feature-on output:

- success: `Pairing reset complete: bonds cleared; BONDING advertising active.`
- error: `pairing_mode reset failed: <errno>`

Return exact result. `-ETIMEDOUT` means transition continues asynchronously;
print error, never claim success or issue another reset.

Feature-off command continues calling `bt_bap_pairing_reset()` with exact old
success/error text, preserving nRF5340 and existing BlueZ/WirePlumber scripts.
No direct bond/advertising call exists in feature-on shell path.

Add direct feature-on shell suite or second configuration compiling real
`bt_shell.c` with fake `pairing_mode_request_reset_sync`; prove exact timeout
argument, success, `-ETIMEDOUT`, another errno, one call only, and no legacy API
call. Keep existing feature-off shell tests unchanged.

## Legacy API

Retain `bt_bap_pairing_reset()` only for feature-off compatibility. Mark docs as
legacy path used when full stack disabled. Full-stack main/shell/controller must
have zero references to it.

## Tests and counts

- Update `tests/unit/pairing_mode` direct production-source suite.
- Add one feature-on shell Twister suite if existing suite cannot represent both
  compile-time paths cleanly.
- Keep `tests/unit/app_lifecycle` passing unchanged; main wiring is compile/build
  verified.
- Update `tests/test-matrix.json`, coverage matrix, behavior-contract text, and
  BlueZ/WirePlumber parser/tests only if feature-on text needs explicit parsing;
  feature-off fixtures must remain accepted.
- No new production source file: coverage population stays 36.
- If new shell suite: Twister 34→35, canonical 58→59.
- Migrate baseline only for changed production numeric records; every production
  function executes and no unchanged-file ratio decreases.

Required focused verification:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/user-pairing-p5-mode tests/unit/pairing_mode -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/user-pairing-p5-lifecycle tests/unit/app_lifecycle -p -t run
# plus feature-on shell suite command
scripts/test-coverage.sh --report-only --output /tmp/user-pairing-p5-cov --clean-output
python3 scripts/check-test-matrix.py --repo-root . \
  --coverage-json /tmp/user-pairing-p5-cov/coverage.json
git diff --check
```

Also compile a full-stack scratch target with valid test DT aliases or an
nRF54L15 scratch config/overlay; CONTROL-only build is insufficient to verify
main/user-I/O wiring. Then canonical gate, all three feature-off production
builds, build contract 79/79, unchanged BSim pins, zero actionable warnings.

## Commit shape

1. P5 handoff.
2. P1 disconnect restart + lifecycle/main/shell integration + direct tests and
   matrix docs.
3. Coverage migration if numeric records change.
4. P5 results/acceptance.

No P6 board enablement/overlay edits, hardware actions, push, PR, amend,
force-push, or attribution footer.
