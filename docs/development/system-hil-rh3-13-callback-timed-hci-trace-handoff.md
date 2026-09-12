# RH3-13 callback-timed HCI trace repair handoff

Status: software repair and host-proof phase only. Do not run physical HIL in
this phase. A later, separate handoff may authorize one new immutable trace
run after review.

## Triggering evidence

`rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace` is immutable.
Its evidence checksum passed 23/23. It failed before source configuration at:

```text
receiver command 'log enable dbg bt_hci_core bt_sdc_hci_driver' shell error:
log: command not found
```

The temporary image did not enable `CONFIG_LOG_CMDS`; NCS builds `log_cmds.c`
only when that symbol is enabled. Do not retry RH3-12 or alter its evidence.

RH3-12 also retained one pre-command:

```text
--- 276 messages dropped ---
```

This proves global/pre-stream debug activation is unsuitable for a 4 KiB
deferred log ring. More importantly, NCS v3.3.0 `bt_hci_core` logs ISO receive
work per packet at debug level (`hci_core.c:4507,4642,4650`), so enabling it
before a 120-second 7.5 ms stream would create high-rate log traffic and can
hide the exact teardown evidence sought.

## Goal

Replace shell-driven pre-stream activation with a diagnostic-only receiver
callback hook. It must raise filters for only `bt_hci_core` and
`bt_sdc_hci_driver` at first `lc3_disable()`, immediately before ASCS exits
streaming and calls `bt_bap_remove_iso_data_path()` / opcode `0x206f`.

Normal firmware remains bit-for-bit behaviorally unchanged because the new
Kconfig option defaults off. The trace image must emit an INFO arm marker before
the target debug records. Runner evidence must distinguish pre-arm dropped
messages from post-arm drops, since post-arm loss invalidates an absent
completion conclusion.

## Grounding

- Receiver `src/bt_bap.c:852-857`: `lc3_disable()` runs before it returns to
  ASCS.
- NCS `ascs.c:1228-1282`: ASCS invokes server `disable` callback, then queues
  transition away from streaming.
- NCS `ascs.c:408-450,567-570`: leaving streaming removes ISO data path before
  disabled/stopped callbacks.
- NCS `iso.c:346-379,409-432`: Remove ISO Data Path sends `0x206f` through
  synchronous HCI.
- NCS `hci_core.c:439-530` logs command send and completion at debug level.
- NCS `nrf/subsys/bluetooth/controller/hci_driver.c:383-481,614-631` logs
  HCI command TX and Command Complete/Status at debug level.
- NCS public logging APIs:

  ```c
  int log_source_id_get(const char *name);
  uint32_t log_filter_set(const struct log_backend *backend,
                          uint32_t domain_id, int16_t source_id,
                          uint32_t level);
  ```

  Use `log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, LOG_LEVEL_DBG)`.
  This works without `CONFIG_LOG_CMDS`, but a negative source ID must never be
  passed to `log_filter_set`; `log_source_id_get()` returns `-1` for unknown
  sources and NCS does not guard that lower bound.

## Scope

### In scope

1. Replace old shell-command trace behavior in `scripts/hil/cli.py`,
   `scripts/hil/runner.py`, `scripts/hil/receiver.py`, and
   `tests/hil/rh2_test.py`.
2. Add a diagnostic-only Kconfig gate and a small unit-testable filter arming
   helper under `src/`.
3. Invoke helper exactly once in `lc3_disable()` under the gate.
4. Add a native pure unit suite, test-matrix entry, HIL fake-boundary coverage,
   and update trace config fragment.
5. Run host tests and receiver builds, then restore normal nRF54L15 local image
   hashes.

### Out of scope

- Physical HIL, flashing, reset, serial, pairing, Bluetooth host control, RF,
  power, source build, or manual hardware command.
- NCS source changes, `CONFIG_LOG_CMDS`, production logging defaults, log buffer
  enlargement, warning-scanner changes, timeout changes, source fixture changes,
  or acceptance-policy changes.
- Normal firmware behavior when trace Kconfig option is disabled.
- `STATUS.md`, immutable RH3-12 evidence, result documents, commit, push,
  merge, PR, tag, release, worktree cleanup, reset, or stash.

## Exact implementation

### 1. Diagnostic gate and build fragment

In root `Kconfig`, add:

```kconfig
config HIL_HCI_REMOVE_ISO_PATH_TRACE
    bool "HIL callback-timed HCI Remove ISO Path trace"
    default n
    depends on SOC_NRF54L15 && BT && LOG_RUNTIME_FILTERING
```

Add help stating this is a temporary HIL-only diagnostic, arms two runtime log
sources at `lc3_disable()`, and must stay disabled in normal builds. Do not
select dependencies or alter normal settings.

Update `tests/hil/receiver-hci-remove-iso-path.conf` to contain exactly these
diagnostic settings:

```conf
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_BT_HCI_CORE_LOG_LEVEL_DBG=y
CONFIG_BT_HCI_DRIVER_LOG_LEVEL_DBG=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE=y
```

Do not add `CONFIG_LOG_CMDS=y`. No shell `log` command is used after this
repair.

### 2. Pure filter-arm helper

Add:

```text
src/hci_remove_iso_path_trace.h
src/hci_remove_iso_path_trace.c
```

The helper must be Zephyr-logging independent so native tests can inject two
dependencies. Define:

```c
struct hci_remove_iso_path_trace_ops {
    int (*source_id_get)(const char *name);
    uint32_t (*filter_set)(int source_id, uint32_t level);
};

struct hci_remove_iso_path_trace_result {
    int core_source_id;
    int driver_source_id;
    uint32_t core_level;
    uint32_t driver_level;
};

int hci_remove_iso_path_trace_arm(
    const struct hci_remove_iso_path_trace_ops *ops,
    uint32_t debug_level,
    struct hci_remove_iso_path_trace_result *result);
```

Rules:

1. Reject null operations, callbacks, or result with `-EINVAL` and make no
   dependency call.
2. Look up both exact names, `bt_hci_core` then `bt_sdc_hci_driver`, before any
   filter mutation. Initialize result source IDs to `-1` and levels to zero.
3. If either ID is negative, return `-ENOENT` and make no `filter_set` call.
4. Call injected `filter_set` exactly once per valid source, in core then driver
   order, with supplied `debug_level`.
5. Store actual returned levels. If either differs from `debug_level`, return
   `-ERANGE`; otherwise return zero.
6. No dynamic allocation, logging, blocking wait, global mutable state, or
   NCS-private API.

Add source only for diagnostic builds in root `CMakeLists.txt`:

```cmake
zephyr_sources_ifdef(CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE src/hci_remove_iso_path_trace.c)
```

### 3. Receiver callback adapter

In `src/bt_bap.c`, compile adapter code only when
`CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE` is enabled. Include public NCS headers:

```c
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_ctrl.h>
```

Adapt the pure helper to these exact calls:

```c
log_source_id_get(name)
log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, (int16_t)source_id, LOG_LEVEL_DBG)
```

Use an atomic once guard. First invocation only must call the helper. Repeated
disable callbacks must not mutate filters or emit a second arm marker.

On success emit exactly one INFO payload:

```text
HCI remove ISO path trace armed: core_id=%d driver_id=%d core_level=%u driver_level=%u
```

On failure emit exactly one ERROR payload:

```text
HCI remove ISO path trace arm failed: err=%d core_id=%d driver_id=%d core_level=%u driver_level=%u
```

Call the gated adapter as first action in `lc3_disable()`, before existing
`Disable: stream ...` logging and teardown transition. When gate is disabled,
the call must compile to a no-op. Do not alter `teardown_transition`, locking,
ASCS callbacks, stream behavior, or normal logs.

`lc3_disable()` already uses mutex-owning teardown code, so it is host thread
context, not ISR context. Do not claim `log_filter_set` is ISR-safe. The atomic
guard protects duplicate callbacks; no other diagnostic code may mutate these
filters.

### 4. Runner evidence, not shell control

Keep `--hci-remove-iso-path-trace` only on direct `hil-runner.py run`, but
change its meaning: request callback-timed trace evidence. It must no longer
send any receiver shell command, parse `log status`, require `CONFIG_LOG_CMDS`,
or fail before source configuration.

Remove old `parse_log_status()` and pre-stream command logic completely.

After runner-owned console cleanup makes `receiver-console.bin` stable, parse
raw UTF-8 lines using existing raw-line facilities and write
`hci-remove-iso-path-trace.json`. Include:

- arm markers and arm-failure markers, with raw offsets and stripped lines;
- all `--- N messages dropped ---` records, classified before or after first
  arm marker;
- `bt_hci_core` debug sends containing `opcode 0x206f`;
- `bt_sdc_hci_driver` Command Complete and Command Status records for `0x206f`;
- `bt_hci_core` completion/done records for `0x206f`;
- host timeout/fatal records mentioning `opcode 0x206f`;
- parser and validation errors.

Validation:

1. Require exactly one successful arm marker.
2. Require nonnegative source IDs and both actual levels equal `4`.
3. Reject any arm-failure marker.
4. Reject any dropped-message record after arming. Pre-arm drops remain visible
   observations and do not alone invalidate target trace.
5. Require at least one core `0x206f` send record. Do not require completion,
   status, or timeout records because their presence/absence is the diagnostic
   result.

If validation fails on an otherwise passing row, fail at distinct boundary
`hci remove iso path trace evidence`. If an earlier row failure exists, preserve
that first boundary and retain trace validation errors in JSON. Do not change
warning scan semantics. For successful rows, add the trace object to
`summary.json` before final evidence finalization.

### 5. Tests and matrix

Add `tests/unit/hci_remove_iso_path_trace/` with `CMakeLists.txt`, `prj.conf`,
`testcase.yaml`, and C test source. Native tests must prove:

1. Exact lookup names, core-then-driver lookup/mutation order, requested debug
   level, returned levels, and success outcome.
2. Null dependency/result rejection leaves fakes untouched.
3. Core or driver lookup failure returns `-ENOENT` before filter mutation.
4. Either clamped return returns `-ERANGE` while preserving observed result.

Add direct entry for `src/hci_remove_iso_path_trace.c` to
`tests/test-matrix.json`, suite `hci_remove_iso_path_trace`, with exact public
outcomes `0`, `-EINVAL`, `-ENOENT`, and `-ERANGE`; mark `stateful: false`.

Update HIL fake tests to prove:

1. Default direct runner sends no trace shell commands and produces no trace
   evidence file.
2. Trace mode sends no `log enable` or `log status` command, completes normal
   source setup, and retains successful callback marker/target evidence.
3. Pre-arm dropped records are retained but accepted.
4. Missing/malformed/duplicate arm marker, invalid IDs/levels, arm failure,
   post-arm dropped message, or missing core `0x206f` send fails trace evidence
   after the ordinary row path and preserves trace JSON.
5. CLI flag remains direct-run-only and forwards its boolean.

Use public effects: runner TX, source configuration occurrence/absence,
artifact content, validation boundary, and pure helper outputs. Do not test
private call counts beyond injected dependency boundary ordering needed to prove
no mutation after lookup failure.

## Required verification

Run from repository root. No physical hardware.

```bash
env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/hci-remove-iso-path-trace-unit \
  tests/unit/hci_remove_iso_path_trace -p -t run
nix develop --command pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-hci-remove-iso-path.conf"
```

Trace build resolved config must show:

```text
CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_LOG_RUNTIME_FILTERING=y
CONFIG_BT_HCI_CORE_LOG_LEVEL=4
CONFIG_BT_HCI_DRIVER_LOG_LEVEL=4
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL=3
# CONFIG_LOG_CMDS is not set
```

Then build both production targets. Treat undocumented warnings as failures.

```bash
nix develop --command fw-build-5340
nix develop --command fw-build-54l15
sha256sum --check <<'EOF'
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
git diff --check
git status --short
```

If a build, test, matrix check, or normal-hash restore fails, stop and report
exact evidence. Do not run HIL or normalize failures.

## Worktree rules

Worktree is intentionally dirty. Touch only files named in this handoff. Do
not modify RH3-12 evidence or result documents. Do not stage, commit, push,
merge, tag, release, reset, restore, stash, or clean.

## Executor return format

Return files changed, exact trace timing behavior, unit/HIL fake/build/matrix
results, trace and normal resolved config evidence, nRF54L15 normal hash
restoration, warnings observed, git status summary, deviations, and blockers.
