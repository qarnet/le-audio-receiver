# RH3-13 native entropy warning repair handoff

Status: software-only repair. No HIL authorization. Do not touch hardware.

## Blocker and root cause

Fresh RH3 execution preflight is blocked by this warning:

```text
CMake Warning ... No SOURCES given to Zephyr library: drivers__entropy
```

The helper suite sets `CONFIG_ZTEST=y` and
`CONFIG_FAKE_ENTROPY_NATIVE_SIM=n`. In NCS v3.3.0,
`nrf/Kconfig.nrf:119-125` weakly implies `ENTROPY_GENERATOR` through
`NRF_SECURITY_ENABLER` for `BOARD_NATIVE_SIM`. `ZTEST` does not select
entropy. With the fake driver disabled, `ENTROPY_GENERATOR=y` leaves an empty
`drivers__entropy` library. `CONFIG_ENTROPY_GENERATOR=n` overrides that weak
imply without an assigned-value warning.

## Exact config contract

Keep:

```conf
CONFIG_ZTEST=y
CONFIG_FAKE_ENTROPY_NATIVE_SIM=n
```

Add `CONFIG_ENTROPY_GENERATOR=n` with a local comment explaining that this
helper-only suite consumes no entropy and that the setting prevents the empty
native-sim entropy library. Do not enable `TEST_BUSY_SIM`, add an entropy
consumer, suppress warnings, or change fake-entropy defaults globally.

## Required proof

Use fresh build directory. Do not delete or overwrite existing build output:

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/hci-remove-iso-path-trace-unit-entropy-clean \
  tests/unit/hci_remove_iso_path_trace -p -t run
```

Proof must show all six helper tests pass and resolved config has exactly:

```text
# CONFIG_ENTROPY_GENERATOR is not set
```

`CONFIG_FAKE_ENTROPY_NATIVE_SIM=n` must remain in the suite's `prj.conf`.
Omission of `FAKE_ENTROPY_NATIVE_SIM` from resolved `.config` is expected
because NCS defines that child symbol inside the disabled entropy parent menu.
Build output must have no `No SOURCES given to Zephyr library:
drivers__entropy` warning and no assigned-value or other Kconfig warning. Also
run `git diff --check` and report `git status --short`.

## Scope

Only `tests/unit/hci_remove_iso_path_trace/prj.conf` and this internal handoff
are in scope. No firmware, Kconfig, CMake, fixtures, runner, test semantics,
HIL, hardware, serial, flashing, OpenOCD, Bluetooth, reset, evidence,
historical handoffs, `STATUS.md`, commits, staging, pushes, reset, stash, clean,
or restore.
