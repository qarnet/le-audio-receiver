# P6 handoff — XIAO controls, production enablement, and build contract

Base commit: `04b5010` (P5 accepted; worktree clean).

Implement P6 from `docs/development/user-pairing-control-plan.md`: map the real
Seeed XIAO nRF54L15 user button/LED through project aliases, remove inherited DK
GPIO claims, enable the full stack only on nRF54L15, resolve cpuapp SRAM without
dropping accepted diagnostics, and extend executable build-contract proof.

## Scope

In scope:

- `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` control remap/cleanup;
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf` full-stack enablement and proven
  memory budget;
- `scripts/check-build-contract.py` resolved config/DT assertions;
- build-contract tests/docs/results and coverage/matrix docs if executable
  Python tests change.

Out of scope:

- flashing, button presses, pairing, LED observation, or streaming (P8);
- nRF5340 feature enablement;
- advertising payload differentiation;
- disabling FLPR/audio acceptance diagnostics to make memory fit;
- audio, FLPR protocol/ring, shared-memory layout, or pin changes unrelated to
  inherited DK control cleanup.

## Grounded DT mapping

Installed NCS v3.3.0 evidence:

- XIAO reference `zephyr/boards/seeed/xiao_nrf54l15/
  xiao_nrf54l15_common.dtsi`:
  - onboard user LED: P2.00, `GPIO_ACTIVE_LOW`;
  - onboard user button: P0.00, `GPIO_PULL_UP | GPIO_ACTIVE_LOW`.
- Stock target inherited controls from
  `zephyr/boards/nordic/nrf54l15dk/nrf54l15dk_common.dtsi`:
  - button0 P1.13, button1 P1.09, button2 P1.08, button3 P0.04;
  - led0 P2.09 active-high, led1 P1.10, led2 P2.07, led3 P1.14;
  - button1/button2 collide with project UART20 P1.09/P1.08.
- gpio-keys uses `DT_INST_FOREACH_CHILD_STATUS_OKAY`, so explicit disabled
  children are excluded.
- gpio-leds uses all children regardless of child status; inherited led1–3 must
  be `/delete-node/`, not merely disabled, or driver still configures pins.
- gpio-keys binding debounce default is 30 ms, but set it explicitly because
  P2 compile-time contract compares resolved DT to Kconfig.

## Exact overlay changes

Inside root aliases add:

```dts
user-button = &button0;
user-led = &led0;
```

Add/merge:

```dts
/ {
	buttons {
		debounce-interval-ms = <30>;
	};
};

&button0 {
	gpios = <&gpio0 0 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
};

&button1 { status = "disabled"; };
&button2 { status = "disabled"; };
&button3 { status = "disabled"; };

&led0 {
	gpios = <&gpio2 0 GPIO_ACTIVE_LOW>;
};

/delete-node/ &led1;
/delete-node/ &led2;
/delete-node/ &led3;
```

Keep inherited button0 `zephyr,code = <INPUT_KEY_0>`. `sw0` and mcuboot-button0
therefore also resolve to physical XIAO button; P2 uses only `user-button`.
Do not alter UART20 P1.8/P1.9, I2S20 P1.4–P1.7, RF switch P2.3/P2.5, TIMER20,
shared SRAM, or oscillator settings.

Verify resolved DTS, including no gpio-leds children except remapped led0 and no
status-okay gpio-keys child except remapped button0. Confirm no enabled pinctrl/
GPIO claimant overlaps P0.00 or P2.00.

## Kconfig enablement

In nRF54L15 board conf enable:

```text
CONFIG_USER_PAIRING_CONTROL=y
CONFIG_USER_PAIRING_INPUT=y
CONFIG_USER_PAIRING_DEBOUNCE_MS=30
CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000
```

Set `CONFIG_USER_PAIRING_WORKQ_STACK_SIZE` to the smallest build- and
runtime-defensible value, initially 1024 (P5 scratch needed this value). Do not
change production timing defaults otherwise.

nRF5340 board conf/prj remain feature-off. Build contract must prove CONTROL and
INPUT not enabled there.

## SRAM budget

Current feature-off nRF54L15 ELF reports roughly 5.3 KiB data + 156.5 KiB BSS
before full-stack enablement; cpuapp SRAM ends at shared-memory boundary
0x20028000 (160 KiB). P5 full-stack scratch linked only with workqueue stack
1024 and acceptance diagnostics disabled, but accepted production contract
requires `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y`. P6 must solve this explicitly.

Workflow:

1. Build full production config with workqueue stack 1024 and diagnostics still
   enabled; capture linker overflow or map usage.
2. Use map/config evidence to identify static RAM consumers introduced or
   retained. Do not guess.
3. Prefer removing feature-on-unreachable legacy static allocations or reducing
   a proven unused configurable pool over changing audio buffers, BT ISO/ACL
   counts, FLPR shared memory, ring sizes, or acceptance diagnostics.
4. `CONFIG_HEAP_MEM_POOL_SIZE=4096` is a candidate only after map/source proof;
   repo production source has no direct `k_malloc`/`k_free`, but Zephyr
   subsystems may use system heap. If reduced, document all resolved heap users,
   resulting margin, and add build-contract assertion for chosen value. P8 must
   validate runtime.
5. Require nonzero practical SRAM margin; do not accept a link with only a few
   bytes free. Record region used/free from linker output/map.

Do not move/resize reserved shared-memory regions or disable diagnostics.

## Build contract

Extend `scripts/check-build-contract.py` using parsed resolved `.config` and
`zephyr.dts`, not source-text matching.

nRF54 assertions:

- CONTROL=y, INPUT=y, debounce=30, shell timeout=15000, chosen workqueue stack;
- chosen heap value if changed;
- `user-button` alias resolves to button0;
- button0 parent compatible gpio-keys, parent debounce 30;
- button0 GPIO controller gpio0, pin 0, ACTIVE_LOW + PULL_UP flags, code
  INPUT_KEY_0;
- button1/button2/button3 status disabled;
- `user-led` alias resolves to led0;
- led0 GPIO controller gpio2, pin 0, ACTIVE_LOW;
- inherited led1/led2/led3 absent from resolved tree;
- existing UART/I2S/RF/shared-memory/diagnostic assertions remain passing.

nRF5340 assertions:

- CONTROL not enabled;
- INPUT not enabled.

Use stable new assertion IDs and update exact expected total in tests/docs. Add
or expand Python unit fixtures for alias resolution, GPIO phandle-array flags,
missing/deleted nodes, wrong pin/polarity/debounce, disabled-button status, and
feature-off checks. Tests must fail for wrong resolved artifacts, not copied
constants alone.

## Verification

```bash
fw-build-54l15
fw-build-5340
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
./scripts/test-all.sh
git diff --check
```

Also inspect:

- nRF54 app `.config`, `zephyr.dts`, map/region report, and symbols for P1/P2/P4
  full-stack wiring;
- nRF5340 `.config` for feature-off proof;
- all build logs for new/actionable warnings;
- BSim pins byte-identical.

Expected canonical child count stays 59 unless build-contract Python coverage
adds a registered suite (normally existing suite only). Numeric production
coverage population stays 36; migrate baseline only if production C records
change. Build-contract count grows from 79 by exact new assertions; document
new total rather than preselecting a number.

## Commit shape

1. P6 handoff.
2. Overlay/conf + SRAM solution + build-contract implementation/tests/docs.
3. Coverage migration only if required.
4. P6 results/acceptance after full software/build gate.

No flash, reset, serial/hardware action, push, PR, amend, force-push, destructive
operation, or attribution footer.
