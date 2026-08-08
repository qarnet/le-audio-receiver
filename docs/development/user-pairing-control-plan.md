# User pairing control plan — button, LED, and access-mode state machine

Status: accepted design; implementation complete (P1–P8 accepted 2026-08-08, hardware acceptance on XIAO nRF54L15).
Scope: first production integration targets the Seeed XIAO nRF54L15 hardware,
but behavior and control logic must be portable to another Zephyr board by
supplying devicetree aliases and enabling Kconfig. The nRF5340 production target
stays behavior-identical until this feature is explicitly enabled there.

## Goal

Add one user-controlled pairing/access state machine with three visible modes:

| Mode | Connection policy | User LED | Exit |
|---|---|---|---|
| NORMAL | BONDED_ONLY, including an empty bond list | Off | 3-second or 8-second button threshold |
| BONDING | OPEN; existing bonds retained | 500 ms on / 500 ms off | Secure bonded connection or 8-second threshold |
| RESETTING | Advertising suspended; bonds removed after disconnect | 100 ms on / 100 ms off for 1 second | BONDING |

NORMAL is always the boot default. The application does not special-case zero
bonds: it advertises with connection filtering enabled and an empty filter list,
so no central may connect until the user enters BONDING.

## Grounding evidence

- Installed NCS v3.3.0 XIAO board source defines the user LED (`led0`) as
  P2.00 active-low and the user button (`usr_btn`) as P0.00, pull-up,
  active-low:
  `zephyr/boards/seeed/xiao_nrf54l15/xiao_nrf54l15_common.dtsi`.
- This project builds stock `nrf54l15dk/nrf54l15/cpuapp` with a project
  overlay. Current resolved aliases therefore point at DK hardware: `led0` is
  P2.09 and `sw0` is P1.13. The overlay must remap the intended user controls.
- Enabling the inherited DK `gpio-keys` instance unchanged would also configure
  DK button pins P1.08/P1.09, colliding with the XIAO UART20 bridge. Inherited
  unused button children must be disabled.
- Zephyr's `gpio-keys` driver reports debounced press and release events using
  both-edge GPIO interrupts. Its default debounce is 30 ms. Use this instead of
  board-specific ISR/debounce code.
- Current `bt_pairing_policy_set_bonds()` derives OPEN from an empty list. That
  conflicts with the new boot contract. Bond inventory and desired access mode
  must become separate state.
- Current `bt_bap_pairing_reset()` deletes all bonds. A 3-second BONDING request
  must preserve them, so it cannot reuse that transition directly.
- Current main loop restarts advertising after every disconnect. RESETTING
  needs an explicit suspended-advertising state so the disconnect wake cannot
  restart advertising during the one-second reset indication.
- Just Works is already configured by `CONFIG_BT_SMP_ENFORCE_MITM=n`. BONDING
  must also request `BT_SECURITY_L2` after connection rather than relying only
  on the central to initiate pairing.

## Fixed behavior

### Button thresholds

- A debounced press starts one hold generation.
- At 3 seconds, emit BONDING once.
- At 8 seconds, emit RESET once.
- RESET supersedes the earlier BONDING event from the same continuous hold.
- Release before 3 seconds cancels both thresholds and has no effect.
- Release after 3 seconds leaves BONDING active.
- Continuing to hold until 8 seconds always starts RESET, even if a central
  connected and the mode returned to NORMAL during seconds 3 through 8.
- Delayed work carries the hold generation; stale work from a prior press is a
  no-op.

Default configurable timings:

| Setting | Default |
|---|---:|
| Input debounce | 30 ms |
| Bonding threshold | 3000 ms |
| Reset threshold | 8000 ms |
| Bonding LED half-period | 500 ms |
| Reset LED half-period | 100 ms |
| Reset indication duration | 1000 ms |

### NORMAL to BONDING

1. Stop advertising and mark advertising suspended.
2. If a peer is connected, request disconnect and wait asynchronously for the
   matching disconnect completion.
3. Preserve every persisted bond and policy snapshot entry.
4. Select OPEN access mode.
5. Enter BONDING and start the slow LED pattern.
6. Start unrestricted connectable advertising.

No BONDING advertisement may start before the old connection closes.

### BONDING completion: new peer

1. A central connects.
2. Receiver requests `BT_SECURITY_L2`.
3. Pairing proceeds through Just Works.
4. Successful bonded pairing adds the peer to the bond snapshot.
5. State owner enters NORMAL and turns the LED off.
6. The existing connection remains active; do not disconnect it merely to
   apply NORMAL.
7. Future advertising rebuilds use BONDED_ONLY filtering.

Pairing failure is a normal remote outcome: remain in BONDING and allow another
attempt. It is not a fatal platform-transition failure.

### BONDING completion: previously bonded peer

A previously bonded central may auto-connect because NORMAL and BONDING
advertisements are not yet distinguishable to every central. For this feature:

1. Request `BT_SECURITY_L2` after connection.
2. On successful security change, verify `bt_le_bond_exists()` for the peer.
3. Count that secure bonded reconnection as successful completion.
4. Enter NORMAL, turn the LED off, and retain the active connection.

Future differentiation of NORMAL and BONDING advertisements is recorded in
`PLANNED_FEATURES.md`.

### Any mode to RESETTING

1. RESET supersedes any pending or active BONDING transition.
2. Stop advertising and mark advertising suspended.
3. If connected, request disconnect and wait for completion.
4. Only after the connection is closed, delete all persisted bonds and clear
   the in-memory snapshot.
5. Enter RESETTING and rapidly flash the LED for exactly one second.
6. Keep advertising fully stopped for the complete reset indication.
7. Enter BONDING, start the slow LED pattern, and start OPEN advertising.

If a peer connected during the 3-to-8-second BONDING interval, RESET disconnects
that peer before deleting any bond, including a bond created during the same
hold.

### Transition failure

These failures are fatal because continuing could leave access policy,
advertising, bond storage, and visible mode inconsistent:

- advertising stop, parameter update, filter rebuild, or start;
- disconnect request;
- security request;
- bond deletion;
- mandatory input/LED initialization;
- mandatory LED state update.

On failure, log current mode, internal phase, operation, errno, connection
state, transition generation, and hold generation; force the LED inactive when
possible; then call `sys_reboot(SYS_REBOOT_COLD)`. Boot returns to safe
NORMAL/BONDED_ONLY. Never continue from partial state.

## Ownership and architecture

### `src/pairing_mode.c/.h`

One portable transition owner. No GPIO, devicetree, or Bluetooth stack types in
its public API.

Responsibilities:

- own visible mode and internal transition phase;
- serialize all events on one dedicated work queue;
- own transition generation and reset/LED timers;
- execute injected platform operations;
- accept button, disconnect, pairing, and security events;
- expose a read-only status snapshot;
- support an asynchronous button request and bounded synchronous shell reset;
- invoke cold reboot after fatal operation failure.

Visible mode remains the requested three-mode contract. Private phases may be:

- `IDLE`;
- `WAIT_DISCONNECT_FOR_BONDING`;
- `WAIT_DISCONNECT_FOR_RESET`;
- `RESET_FEEDBACK`.

Proposed dependency surface:

```c
struct pairing_mode_ops {
	int (*advertising_suspend)(void);
	int (*advertising_start_normal)(void);
	int (*advertising_start_bonding)(void);
	int (*disconnect_peer)(bool *pending);
	int (*delete_all_bonds)(void);
	int (*request_security)(void);
	int (*led_set)(bool active);
	void (*cold_reboot)(void);
};
```

Exact signatures may add a context pointer and typed completion result, but the
ownership above must not move into callbacks or board code.

### `src/user_pairing_io.c/.h`

Reusable Zephyr hardware adapter.

Responsibilities:

- resolve `DT_ALIAS(user_button)` and `DT_ALIAS(user_led)`;
- subscribe to the selected `gpio-keys` device and event code;
- schedule and cancel 3-second/8-second threshold work;
- reject stale threshold work using hold generation;
- forward requests to `pairing_mode`;
- drive logical LED state with `gpio_pin_set_dt()`, leaving polarity in DT.

No Bluetooth knowledge and no board-number conditionals.

### `src/bt_pairing_policy.c/.h`

Remain pure. Refactor bond inventory away from desired mode:

- explicit mode setter for OPEN/BONDED_ONLY;
- bond enumeration replaces entries without deriving mode;
- OPEN preserves entries;
- clearing entries is separate and used only after successful storage deletion;
- NORMAL may be BONDED_ONLY with zero entries;
- snapshots remain atomic;
- marking a bond updates inventory but does not become a second transition
  owner.

When `CONFIG_USER_PAIRING_CONTROL=n`, preserve current nRF5340 behavior.

### `src/bt_bap.c/.h`

Concrete Bluetooth adapter only:

- stop/start extended advertising under existing `pairing_adv_lock`;
- represent NORMAL, BONDING, and SUSPENDED access;
- rebuild the controller filter from persisted bonds without deriving mode;
- allow an empty BONDED_ONLY filter;
- preserve bonds for BONDING and delete only for RESET;
- request L2 security after a BONDING connection;
- translate disconnect, pairing-complete, pairing-failed, and
  security-changed callbacks into queued controller events;
- never invoke transition/HCI work while holding a callback-context spinlock;
- retain all BAP/ASCS stream ownership and R7 teardown behavior.

Callbacks do not become transition owners.

### `src/main.c` and `src/app_lifecycle.c/.h`

- initialize pairing-mode control and user I/O after BAP creation and before
  initial advertising;
- make initialization fatal when feature is enabled;
- route initial advertising through pairing-mode NORMAL;
- route disconnect wakes to pairing-mode instead of independently restarting
  advertising;
- RESETTING disconnect wakes leave advertising suspended;
- NORMAL/BONDING disconnect wakes restart the correct policy;
- preserve current lifecycle behavior when feature is disabled.

### `src/bt_shell.c`

Keep command path `bt unpair`, but route it through the same RESET event owner.
Use a bounded synchronous completion API so success is printed only after:

- old connection is closed;
- bonds are deleted;
- one-second reset indication completes;
- BONDING advertising is active.

Update shell and BZ3 tests for the new completion text and timeout/error
contract. No direct bond/advertising calls remain in the shell path.

## Devicetree portability contract

Boards enable the hardware adapter by supplying:

```dts
/ {
	aliases {
		user-button = &button0;
		user-led = &led0;
	};
};
```

For the current stock-DK-target/XIAO overlay:

- override `button0` to `&gpio0 0 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)`;
- override `led0` to `&gpio2 0 GPIO_ACTIVE_LOW`;
- add `user-button` and `user-led` aliases;
- set 30 ms debounce;
- disable inherited `button1`, `button2`, and `button3` so enabling
  `gpio-keys` cannot claim UART20 P1.08/P1.09;
- preserve RF switch P2.03/P2.05 and I2S20 P1.04–P1.07;
- verify resolved `zephyr.dts`, not only overlay source.

Future boards port behavior by supplying aliases and Kconfig only.

## Kconfig

Proposed symbols:

```text
CONFIG_USER_PAIRING_CONTROL
CONFIG_USER_PAIRING_INPUT
CONFIG_USER_PAIRING_BOND_HOLD_MS=3000
CONFIG_USER_PAIRING_RESET_HOLD_MS=8000
CONFIG_USER_PAIRING_DEBOUNCE_MS=30
CONFIG_USER_PAIRING_BOND_LED_HALF_PERIOD_MS=500
CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS=100
CONFIG_USER_PAIRING_RESET_FEEDBACK_MS=1000
CONFIG_USER_PAIRING_WORKQ_STACK_SIZE
CONFIG_USER_PAIRING_WORKQ_PRIORITY
```

Add compile-time assertions that reset threshold exceeds bonding threshold and
that reset indication consists of complete LED half-periods. Enable only in
`boards/nrf54l15dk_nrf54l15_cpuapp.conf`. Leave nRF5340 disabled.

## Implementation phases

### P0 — contract and evidence lock

- Commit this plan and `PLANNED_FEATURES.md`.
- Add behavior-contract identifiers for mode, timing, ordering, failure, and
  portability.
- Record current baseline: canonical 55/55, population 33, build contract
  79/79, BSim 17 scenarios / 26 runs, both hardware matrices accepted.

Exit: implementation agent needs no architecture invention.

### P1 — pure transition owner

- Implement `pairing_mode.c/.h` with fake operations.
- Test state/phase transitions, threshold supersession, stale events,
  synchronous reset completion, and every fatal operation failure.
- No Bluetooth or GPIO changes.

Exit: one tested transition owner exists.

### P2 — generic input and LED adapter

- Implement `user_pairing_io.c/.h` using input events and DT aliases.
- Add native GPIO-emulation tests with shortened Kconfig timings.
- Prove debounce, exact thresholds, one event per hold, stale work rejection,
  release behavior, active-low LED handling, and pattern timing.

Exit: board-neutral hardware adapter proven through public behavior.

### P3 — pairing policy separation

- Separate desired access mode from bond inventory.
- Make empty BONDED_ONLY legal.
- Add OPEN-with-preserved-bonds and explicit clear behavior.
- Expand direct tests before Bluetooth integration.

Exit: policy expresses all three access requirements without HCI work.

### P4 — Bluetooth adapter and callback integration

- Add suspended advertising and explicit normal/bonding start operations.
- Route disconnect/pairing/security events to controller.
- Request L2 security in BONDING.
- Preserve active connection on successful completion.
- Remove old independent pairing-reset transition implementation.

Exit: no advertising/bond transition exists outside pairing-mode owner.

### P5 — lifecycle and shell integration

- Insert feature initialization into boot ordering.
- Route disconnect wakes and initial advertising through controller.
- Route `bt unpair` through synchronous RESET.
- Update BZ3 reset handling and exact output tests.

Exit: button, shell, callback, and boot paths share one owner.

### P6 — XIAO mapping and build contract

- Apply P2.00/P0.00 mappings and disable inherited DK button conflicts.
- Enable input/control only in nRF54L15 board conf.
- Extend build contract with resolved aliases, GPIO flags, disabled inherited
  buttons, feature Kconfig, and nRF5340 feature-off assertions.

Exit: resolved DT proves correct hardware without pin overlap.

### P7 — software acceptance

- Run all new direct suites plus policy/lifecycle/shell/BAP and
  BlueZ/WirePlumber gate suites.
- Run canonical gate and explicit coverage enforcement.
- Apply coverage migration rule for new production files; no ratio weakening.
- Build nRF5340, nRF54L15, and dongle; run updated build contract.
- Require all existing BSim pins byte-identical.

Exit: software/build acceptance complete with zero actionable warnings.

### P8 — hardware acceptance and closeout

- Execute complete XIAO mode/button/LED/pairing matrix below.
- Re-run nRF54L15 Mode A and Mode B streaming after transitions.
- Re-run nRF5340 build and representative stream to prove feature-off parity.
- Update AGENTS/README/STATUS/design/contracts/results only after hardware pass.

Exit: feature accepted and portable contract documented.

## Direct test matrix

- Boot NORMAL with zero bonds.
- Empty BONDED_ONLY rejects every peer.
- Boot NORMAL with one or multiple bonds.
- Press shorter than 3 seconds does nothing.
- Exactly 3 seconds requests BONDING.
- Exactly 8 seconds requests RESET and supersedes BONDING.
- Release after 3 seconds leaves BONDING active.
- Repeated/stale threshold callbacks cannot duplicate transitions.
- Existing connection closes before BONDING advertising.
- Existing connection closes before bond deletion.
- Advertising stays suspended through RESET feedback.
- BONDING preserves existing bonds.
- RESET deletes all bonds.
- New Just Works bond completes to NORMAL without disconnect.
- Existing bonded secure reconnect completes to NORMAL without disconnect.
- Pairing failure remains BONDING.
- Security-request and platform-operation failures cold-reboot.
- RESET supersedes pending BONDING disconnect.
- RESET supersedes completed BONDING during the same hold.
- Stale disconnect cannot restart the wrong advertising mode.
- Shell and button use the same transition owner.
- NORMAL LED is inactive.
- BONDING LED follows exact 500 ms phases.
- RESET LED follows exact 100 ms phases for one second, then BONDING.
- Reboot always returns NORMAL regardless of bond count.

## Hardware acceptance matrix

- Zero-bond boot advertises but rejects an unbonded central.
- Short press leaves NORMAL and LED inactive.
- Three-second threshold stops advertising, disconnects an active peer, then
  enters BONDING with 1 Hz LED pattern.
- Existing bonds survive BONDING entry.
- New central pairs through Just Works.
- Pairing completion turns LED off while connection remains active.
- Previously bonded secure reconnect during BONDING counts as completion.
- Continuous eight-second hold triggers BONDING at 3 seconds and RESET at 8.
- RESET disconnects any intervening peer before deleting bonds.
- No advertising occurs during one-second rapid indication.
- LED produces five complete rapid flashes over one second.
- Device then enters BONDING with slow pattern.
- Old bonds fail after RESET; a new bond succeeds.
- Reboot returns NORMAL and accepts only saved bonds.
- `bt unpair` produces the same RESET sequence.
- Mode A and Mode B stream cleanly after each relevant transition.
- nRF5340 gate/build/BSim/pairing behavior remains unchanged while disabled.

Hardware evidence must include raw console logs, central logs, mode transition
timestamps, advertising/connect outcomes, bond enumeration evidence, resolved
DT/build contract, and probe identity. No destructive recovery is part of this
plan.

## Non-scope

- Charge LED behavior.
- Physical RESET button behavior.
- Distinct on-air payloads or identities for NORMAL versus BONDING.
- Bond-capacity policy changes beyond the existing eight-entry limit.
- Audio pipeline, FLPR protocol, BSim pin, codec, or 360-frame offload changes.
- Enabling this feature on nRF5340 in the first implementation.
