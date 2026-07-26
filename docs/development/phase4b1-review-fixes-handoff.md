# Phase 4b.1 Review Fixes

Status: implemented (commit `c84d362`)

## Goal

Fix Phase 4b.1 correctness, test validity, DT grounding, and lifecycle issues
before any hardware run. Preserve measurement-only boundary; no PI integration.

## Required fixes

### 1. Test production helper code

Current timing unit tests copy three helper implementations into test source.
Copied tests do not test production behavior.

- Move ISO/GRTC expansion, unsigned count delta, and ppm math into a small
  platform-neutral production source/header (for example
  `src/audio_timing_math.c/.h`).
- Compile that exact source into firmware and `tests/unit/timing`.
- Remove all helper replicas from test source.
- Keep hardware/nrfx includes out of math files.
- Preserve normal/wrap/zero/fast/slow test coverage.

### 2. Use actual TIMER20 devicetree node

Installed NCS v3.3.0 defines `timer20` at:

`~/ncs/v3.3.0/zephyr/dts/vendor/nordic/nrf54l_05_10_15.dtsi:436`

- Add `&timer20 { status = "reserved"; };` to nRF54L15 overlay.
- Remove false comment claiming no DT node exists and remove raw `0x400CA000`
  address from project docs/comments.
- Derive timer register pointer from `DT_NODELABEL(timer20)` and
  `DT_REG_ADDR(...)`. Prefer nrf_timer HAL calls for COUNTER mode, start,
  COUNT/CAPTURE task addresses, and capture read so no numeric nrfx instance is
  needed.
- Remove `CONFIG_NRFX_TIMER=y` if no longer needed. Keep verified GRTC/GPPI
  dependencies only.
- Build must prove `status = "reserved"` remains directly addressable while no
  Zephyr timer driver binds it.

### 3. Validate every consumed ISO timestamp

`bt_bap.c` still calls `audio_sink_sdu_ref_update(info->ts)` unconditionally in
both LIBLC3 and pass-through callbacks.

- Never consume `info->ts` unless `BT_ISO_FLAGS_TS` is set.
- Keep timing anchor stricter: stream 0 plus VALID plus TS.
- Existing drift phase/frequency call may process valid TS independent of SDU
  validity if intended for PLC continuity, but TS flag is mandatory.
- Avoid duplicate flag logic and keep missing-TS warning bounded.

### 4. Fix init and stop state

- Do not set `configured = true` until actuator and timing initialization both
  succeed.
- Check `audio_clock_actuator_init()` return too.
- On timing init failure, leave sink unconfigured and return failure.
- `audio_sink_stop()` must call drift/timing reset even when `started == false`.
  Only I2S trigger operations should depend on `started`.

### 5. Make callback/work reset safe

- Reset must mark measurement inactive before disabling compare.
- Compare handler must not reschedule or publish diagnostics when inactive.
- Cancel/drain pending diagnostic work safely from thread-context reset, or use
  generation/state validation so old payload cannot log after disconnect and
  cannot overwrite new-session state.
- Protect ISR/thread shared state and diagnostic payload against races using
  suitable Zephyr atomics/spinlock/work semantics. Keep ISR short and do not log
  or use floating point there.
- Check and handle failure from scheduling every next GRTC compare. A failure
  must stop active measurement and emit one deferred error, not silently stop.
- Avoid stale `diag_count` reads in work handler; payload must carry its own
  sequence number.

### 6. Clean generated output

Delete untracked repo-root `twister-out/`, `twister-out.1/`, `twister-out.2/`,
and `twister-out.3/`. Do not commit generated output.

### 7. Correct docs

Update Phase 4b.1 handoff/plan docs and this review handoff for actual DT node,
production-helper tests, and final lifecycle behavior. Do not claim hardware
validation yet.

## Verification

From repo root/dev shell:

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/timing -d /tmp/le-audio-timing-test --pristine
/tmp/le-audio-timing-test/zephyr/zephyr.exe
git diff --check
git status --short
```

Also rerun existing drift/rate-convert unit suites if shared headers or CMake
changes affect them. Every warning must be fixed, not ignored.

## Scope constraints

- No hardware/flash/serial/central operation.
- No PI or actuator behavior change.
- No direct RADIO access.
- No ASRC work.
- Preserve nRF5340 behavior.
- Stage only fix files; commit fixes before returning.
- No amend, push, merge, PR, force operation, or attribution footer.

Return exact files, behavior, commands/results, commit hash/message, blockers,
deviations, and hardware-validation readiness.
