# RH3-12 runtime-filtered HCI trace handoff

Status: implementation and host-proof phase only. This phase adds a fixed,
opt-in HIL runner diagnostic for the existing nRF54L15 receiver. It does not
run hardware, flash a board, alter acceptance criteria, or diagnose a root
cause.

## Goal

Capture HCI host and SDC-driver debug records around an existing direct HIL row
without overflowing the receiver's 4 KiB deferred log ring during boot. The
later physical diagnostic must be able to distinguish:

1. the host sending HCI opcode `0x206f`;
2. the SDC driver receiving a Command Complete or Command Status for `0x206f`;
3. no corresponding SDC-driver completion record before the existing ten-second
   host timeout.

This phase proves only host tooling and temporary-build configuration. A later,
separate handoff may authorize exactly one new runner-owned HIL execution.

## Grounding

- `rh3-20260822-10-modeb-7p5-selected-layout` failed during receiver teardown:
  `Controller unresponsive, command opcode 0x206f timeout with err -11`.
- In NCS v3.3.0, `zephyr/subsys/bluetooth/host/hci_core.c:460-508` sends the
  synchronous command and waits ten seconds for its completion semaphore.
- `zephyr/subsys/bluetooth/host/hci_core.c:79` registers `bt_hci_core`.
- `nrf/subsys/bluetooth/controller/hci_driver.c:37-40` registers
  `bt_sdc_hci_driver`; lines 616-629 log Command Complete and Command Status at
  debug level.
- The first global-debug attempt,
  `rh3-20260822-11-hci-remove-path-trace`, dropped 276 then 227 messages before
  all runner boot markers arrived. It is immutable and must not be retried.
- Current production receiver configuration is deferred logging with
  `CONFIG_LOG_BUFFER_SIZE=4096`, runtime filtering enabled, and one serial
  shell log backend. It currently starts at the system limit.
- NCS v3.3.0 `subsys/logging/log_mgmt.c:344-415` and
  `include/zephyr/logging/log_core.h:174-202,422-427` prove that a lower active
  backend filter lowers the aggregate runtime filter and prevents filtered
  debug messages from being created. No other active backend or log frontend is
  enabled in the resolved receiver configuration.
- `subsys/shell/backends/shell_uart.c:541-565` passes
  `CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL` to the serial shell log backend.
  `SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y` therefore starts that backend at INFO.
- `subsys/logging/log_cmds.c:103-206,222-239` supports exact commands
  `log enable dbg <module...>` and `log status`. Status exposes current and
  built-in levels per module.
- Installed NCS accepts an absolute `EXTRA_CONF_FILE`; it merges after
  `prj.conf` and board fragments. See
  `zephyr/cmake/modules/kconfig.cmake:286-300` and
  `zephyr/doc/build/kconfig/setting.rst:167-173`.

## Scope

### In scope

1. Add a checked-in HIL-only receiver configuration fragment:
   `tests/hil/receiver-hci-remove-iso-path.conf`.
2. Add a fixed `--hci-remove-iso-path-trace` option to `scripts/hil/cli.py`
   `run` only.
3. Add fixed trace activation and fail-closed status verification in
   `scripts/hil/runner.py`.
4. Add narrow `log status` parsing in `scripts/hil/receiver.py`.
5. Add fake-lab public-boundary tests in `tests/hil/rh2_test.py`.
6. Run targeted host tests and build proof. Restore normal receiver images
   exactly after temporary build proof.

### Out of scope

- Any physical HIL run, flash, reset, serial-MCP session, pairing, radio, or
  source-fixture action.
- Changes to receiver firmware source, NCS source, source firmware,
  `prj.conf`, board configuration, controller configuration, warning scanner,
  timeout, HCI behavior, test thresholds, or acceptance policy.
- New generic or arbitrary receiver-shell command interface.
- Changes to existing RH2/RH3 rows, matrix scheduling, artifact handling, or
  ordinary runner behavior.
- Updating `STATUS.md`, release documents, current evidence, or immutable run
  directories.
- Commit, push, merge, PR, tag, or worktree cleanup.

## Exact design

### HIL-only build fragment

Create `tests/hil/receiver-hci-remove-iso-path.conf` with only:

```conf
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_BT_HCI_CORE_LOG_LEVEL_DBG=y
CONFIG_BT_HCI_DRIVER_LOG_LEVEL_DBG=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
```

Do not enlarge `CONFIG_LOG_BUFFER_SIZE`. Do not change normal app or board
configuration. This fragment compiles debug records for two exact modules, but
starts the sole serial logging backend at INFO. Before runtime activation,
debug records must be filtered before deferred-message allocation.

### CLI and runner behavior

Add `--hci-remove-iso-path-trace` only to `hil-runner.py run`. It is a boolean
opt-in diagnostic switch. Do not add it to `run-rh3-matrix`, `run-rh4-matrix`,
or capture matrix commands. Do not add any free-form command, module, or level
argument.

Pass the boolean through `cli.cmd_run()` into a new optional
`Runner.run(..., hci_remove_iso_path_trace=False)` argument. Validate it is a
boolean. Default false must preserve exact existing command order and ordinary
runner behavior.

After `_step_boot()` and `_step_clean_state()` complete, but before
`_start_capture()` or `_step_run_row()` can configure/start the source, execute
these exact receiver commands through existing `_checked_receiver_command()`:

```text
log enable dbg bt_hci_core bt_sdc_hci_driver
log status
```

No other receiver command belongs in this trace step. Existing console code
already retains raw RX and TX evidence and owns prompt synchronization.

Add constants for the two module names and two commands. Keep the trace-step
name clear, for example `_step_hci_remove_iso_path_trace()`.

### Status validation and evidence

In `scripts/hil/receiver.py`, add a narrow parser for rows emitted by
`log status`:

```text
<module name> | <current level> | <built-in level>
```

Recognize only `none`, `err`, `wrn`, `inf`, and `dbg` levels. Ignore header and
separator lines. Preserve exact module names. Duplicate rows for one module or
malformed matching rows must not be silently accepted.

The runner must require, for both `bt_hci_core` and `bt_sdc_hci_driver`:

```text
current = dbg
built-in = dbg
```

If either source is missing or any level differs, fail at a distinct
`hci remove iso path trace` boundary before source configuration/start. Do not
silently continue, downgrade, or alter scanner policy.

Persist `hci-remove-iso-path-trace.json` immediately after the command
transcripts and parsed status are available, before raising a validation error.
Use a small stable object containing:

- schema version;
- exact trace command strings and transcripts;
- parsed current and built-in levels for both target modules;
- validation errors, if any.

On success, also include this object in normal `summary.json` under
`hci_remove_iso_path_trace`. Raw receiver RX/TX evidence remains authoritative.
Do not put trace information into `RowSpec`; this is an explicit direct-run
diagnostic, not a new acceptance row.

### Test behavior

Extend fake receiver wiring only as needed to model the two prompt-bounded
commands. Do not weaken existing write-order assertions.

Add tests proving:

1. Default direct run sends no trace commands and preserves existing behavior.
2. Opt-in trace sends exact enable command then exact status command after clean
   state and before source configuration/start.
3. Successful status with both modules `dbg | dbg` retains trace JSON and
   summary payload.
4. Missing module, non-debug current level, non-debug built-in level, duplicate
   module row, or malformed matching row fails closed at trace boundary before
   source configuration/start. Retained trace JSON must expose observed status
   and error.
5. CLI accepts trace flag only on `run`, forwards true to runner, and no generic
   command flag is introduced.

Test public outcomes: exact receiver TX, retained evidence, failure boundary,
and absence of source configure/start. Do not test private helper call counts.

## Verification

Run from repository root, sequentially. Do not use hardware.

```bash
nix develop --command pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
git diff --check
```

Build temporary trace image with the tracked fragment. `fw-build-54l15` itself
adds the CMake separator, so do not pass an extra user `--`:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-hci-remove-iso-path.conf"
```

Inspect `build/nrf54l15/le-audio-receiver/zephyr/.config`. It must show:

```text
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_BT_HCI_CORE_LOG_LEVEL=4
CONFIG_BT_HCI_DRIVER_LOG_LEVEL=4
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL=3
```

Treat every undocumented warning as a failure. Existing documented NCS
diagnostics remain documented exceptions only.

Then restore normal build and prove exact pre-phase image identities:

```bash
nix develop --command fw-build-54l15
sha256sum --check <<'EOF'
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
git diff --check
git status --short
```

If temporary build or restore fails, stop and report exact output. Do not run
hardware and do not repair by changing production configuration.

## Worktree rules

Worktree is intentionally dirty. Touch only files named in this handoff.
Preserve all other edits and untracked files. Do not stage or commit anything.
Do not use reset, restore, checkout, clean, stash, mass formatting, or any
destructive command.

## Executor return format

Return concise recap with:

1. files changed;
2. exact behavior added;
3. tests/builds run and outcomes;
4. resolved temporary and restored normal `.config` evidence;
5. receiver image hash restoration result;
6. current `git status --short` summary;
7. no commit hash, because no commit is authorized;
8. blockers or deviations.
