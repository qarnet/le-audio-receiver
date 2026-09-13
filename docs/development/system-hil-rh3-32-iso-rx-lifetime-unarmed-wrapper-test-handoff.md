# RH3-32 ISO RX lifetime unarmed-wrapper test handoff

Status: focused software-only regression proof. This phase adds one native
behavioral test for the existing HIL-only lifetime wrapper. It does not run
HIL, change production behavior, replace images, or claim why RH3-32 failed
before streaming.

## Goal

Prove public wrapper behavior before a stream opens: a valid `net_buf_unref()`
call must forward once to the real function and must not create ISO RX lifetime
trace state.

## Evidence

RH3-32 did not reach `stream_started()`. Source stayed `connecting` for
20.031 seconds, then reported `first_errno=-116` (`-ETIMEDOUT`). Receiver
reported `Security changed: level 1 err 9` and disconnect reason `0x3e`
(`BT_HCI_ERR_CONN_FAIL_TO_ESTAB`). No lifetime arm or allocation marker exists
in immutable evidence:

```text
/tmp/opencode/hil-runs/rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace/
```

The lifetime trace uses global linker wrapping:

```text
CMakeLists.txt:65-68: -Wl,--wrap=net_buf_unref
```

NCS ACL and SMP paths can call `net_buf_unref()`. Before a successful
`stream_started()` call, `sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed`
is zero. The wrapper helper returns after one atomic read and then calls the
real unref:

```text
src/sdc_hci_remove_iso_path_trace.c:365-371
src/sdc_hci_remove_iso_path_trace.c:1270-1276
```

Existing native tests cover tracked final unrefs only after
`session_open()`. They do not directly prove transparent unarmed forwarding.
This test closes that exact proof gap. It does not prove zero timing cost or
establish a physical cause for RH3-32.

## Scope

In scope:

- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- this handoff only if factual correction is required.

Out of scope:

- `src/sdc_hci_remove_iso_path_trace.c`, `CMakeLists.txt`, `Kconfig`, trace
  fragments, parser, runner, source fixture, receiver behavior, pool sizes,
  NCS code, build contracts, test matrix, and `STATUS.md`;
- all hardware, HIL, flash, reset, serial, RF, pairing, `btattach`,
  `bap_central.py`, or `serial-mcp` actions;
- commit, push, merge, PR, tag, reset, stash, clean, formatting unrelated
  files, or changing existing dirty work.

## Exact implementation

Add one `ZTEST` beside existing ISO RX lifetime tests, named:

```c
test_iso_rx_lifetime_unarmed_unref_forwards_without_tracking
```

Use only existing test helpers and observations:

1. Call `sdc_hci_remove_iso_path_trace_test_reset()` and
   `reset_lifetime_observations()`. Do **not** call
   `sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open()` or any
   allocation wrapper.
2. Initialize `lifetime_buffers[0]` with `ref == 1U` using
   `init_lifetime_buffer()`.
3. Call `__wrap_net_buf_unref(&lifetime_buffers[0])` exactly once.
4. Assert externally observable forwarding behavior:
   - `__real_net_buf_unref()` test observer saw exactly one call;
   - observer pointer equals `&lifetime_buffers[0]`;
   - fake real unref reduced `ref` to `0U`.
5. Assert no lifetime observation happened before arm:
   - arm count is zero;
   - snapshot count is zero;
   - first-free count is zero;
   - final-unref observation count is zero;
   - tracking-error count is zero.

Do not inspect private production slot storage or add production test hooks.
Do not alter the wrapper, its atomic semantics, test harness stubs, or any
existing test expectation. This test proves forwarding through the existing
public wrapper boundary, not implementation layout.

## Verification

Run sequentially from repository root. No hardware action is authorized.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-lifetime-rh3-32-unarmed-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
git status --short
```

Expected focused native result: `14/14` passing. Expected RH2 result:
`220 passed`. Expected matrix result: `0 errors, 0 notes`. If a total differs,
report exact output and stop. Do not weaken assertions or alter unrelated
tests to force a count.

## Return report

Report changed paths, exact test command results, any diagnostics, final
`git diff --check`, final `git status --short`, and explicit confirmation of
no hardware action and no commit. Escalate instead of guessing if the test
cannot prove forwarding without changing production behavior.
