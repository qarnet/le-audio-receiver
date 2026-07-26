# Phase 4b.1 — GRTC-Referenced LRCK Measurement Foundation

**Status: implemented** (commit `f71fe17`)

## Goal

Add supported, hardware-timed nRF54L15 measurement of I2S frame-clock progress
against Bluetooth controller/GRTC time. Validate measurement through bounded
diagnostic logs before changing PI-controller input.

## Design correction

Prior Phase 4b wording says `I2S FRAMESTART -> GRTC capture`. One captured edge
has no frame index and cannot measure frequency. It also cannot safely associate
every 10 ms SDU with a presentation target 40 ms in the future using one compare
channel; later SDUs overwrite pending compares.

Correct production measurement:

1. Validate both `BT_ISO_FLAGS_VALID` and `BT_ISO_FLAGS_TS` before consuming
   `info->ts`.
2. Use first valid `info->ts + qos->pd` as controller-time presentation anchor,
   expanded from 32-bit ISO time to future 64-bit GRTC time using Nordic
   `iso_time_sync` wrap logic.
3. Route I2S20 `FRAMESTART` through allocated GPPI to TIMER20 `TASKS_COUNT`.
   TIMER20 runs in 32-bit COUNTER mode and gives every LRCK edge a frame index.
4. At one-second absolute GRTC compare points anchored to presentation timeline,
   route compare event through GPPI to TIMER20 `TASKS_CAPTURE[0]`. Hardware
   snapshots frame count without callback latency.
5. GRTC handler reads captured count, computes unsigned count delta and elapsed
   GRTC time, and emits bounded diagnostics from work/thread context. Expected
   nominal result is about 47,619 LRCK frames per second.
6. Schedule one compare at a time. One-second spacing avoids pending-target
   overlap and gives about 21 ppm/count resolution.

This remains supported under SDC/MPSL: no RADIO register/event/IRQ/DPPI access,
no hard-coded DPPI/PPIB channels, and no SDC-owned interconnect resources.

## Scope

### Source

- Add `src/audio_timing.h` with platform-neutral API:
  - `int audio_timing_init(void);`
  - `void audio_timing_sdu_ref_update(uint32_t ts_us, uint32_t presentation_delay_us);`
  - `void audio_timing_reset(void);`
- Add nRF54 implementation: `src/audio_timing_nrf54.c` (HAL, DT-derived TIMER20).
  - TIMER20 register base derived from `DT_NODELABEL(timer20)` /
    `DT_REG_ADDR(TIMER20_NODE)`.  The base DTS defines `&timer20` at
    `reg = <0xca000 0x1000>`; the overlay marks it `status = "reserved"`.
  - Uses `nrf_timer_*` HAL functions instead of nrfx_timer; no
    `CONFIG_NRFX_TIMER=y` needed.
  - Production math helpers (`iso_ts_to_grtc64`, `counter_delta_u32`,
    `compute_ppm`) live in `src/audio_timing_math.c/.h` and are compiled
    into both firmware and `tests/unit/timing` for exact coverage.
- Add no-op implementation, suggested `src/audio_timing_none.c`, for nRF5340.
- Call `audio_timing_init()` from `audio_sink_init()` after I2S configuration.
- In `bt_bap.c`, save negotiated `qos->pd` per sink in `lc3_qos()` and call timing
  update only for stream index 0 when both VALID and TS flags are set. Count or
  warn in bounded form when TS is absent; never pass an unvalidated timestamp.
- Reset timing state from `audio_sink_stop()`.

### Build/config

- Add correct conditional sources in `CMakeLists.txt`; nRF54 implementation must
  compile only for nRF54L15/sample-adjust target, while nRF5340 uses no-op.
- nRF54 board config: enable `CONFIG_NRFX_GPPI=y` and any verified generic nrfx
  timer dependency required by build.
- nRF54 overlay: `&timer20 { status = "reserved"; }` for direct HAL
  use.  Do not mark it `okay` and bind Zephyr counter driver simultaneously.
- Use DT-derived TIMER20 register address via `DT_NODELABEL(timer20)` /
  `DT_REG_ADDR`.  Do not introduce raw numeric peripheral addresses.

### Documentation

- Correct Phase 4b in `docs/design.md`, `STATUS.md`, and `AGENTS.md`: future GRTC
  presentation compare snapshots a hardware LRCK edge counter. Remove claim that
  one GRTC-captured FRAMESTART alone provides drift estimate.
- Mark this handoff implemented only after verification passes.

## Exact implementation constraints

- Reference:
  - `~/ncs/v3.3.0/nrf/samples/bluetooth/iso_time_sync/src/controller_time_nrf54.c`
  - `~/ncs/v3.3.0/nrf/samples/bluetooth/iso_time_sync/src/timed_led_toggle.c`
  - `~/ncs/v3.3.0/zephyr/tests/boards/nrf/gppi/src/main.c`
  - `~/ncs/v3.3.0/zephyr/samples/subsys/usb/uac2_explicit_feedback/src/feedback_nrf.c`
- Allocate GRTC and GPPI resources through APIs. Never hard-code channel IDs.
- Use TIMER20 32-bit COUNTER mode. FRAMESTART event endpoint:
  `nrf_i2s_event_address_get(..., NRF_I2S_EVENT_FRAMESTART)`. Counter task:
  `nrf_timer_task_address_get(..., NRF_TIMER_TASK_COUNT)`.
- GRTC compare event must trigger TIMER capture in hardware. Handler timing must
  not affect captured value.
- Convert ISO target to future 64-bit time exactly as Nordic sample does. Reject
  or defer target if not safely in future; never program stale absolute compare.
- Keep ISR short: capture value/state, schedule next absolute compare, submit
  work. No logging, floating point, slab access, or actuator calls in ISR.
- Use unsigned arithmetic for 32-bit TIMER count wrap.
- Log first measurement and then at most every 5 seconds. Include frame delta,
  elapsed microseconds, and integer ppm relative to
  `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ`.
- Return initialization failures. `audio_sink_init()` must fail loudly rather
  than silently run without required nRF54 timing hardware.
- Cleanup/reset must prevent stale callbacks/work from acting after disconnect.
  Static allocated hardware resources may remain allocated across reconnect;
  do not leak/reallocate each stream.
- Do not feed this measurement into `audio_drift_controller_update()` or alter
  actuator behavior in Phase 4b.1. That is Phase 4b.2 after hardware direction
  and stability are proven.
- Preserve central-only test rule. No direct RADIO access.

## Tests

Add host-unit tests for pure helpers, separated from nrfx hardware code:

1. 32-bit ISO timestamp to future 64-bit GRTC conversion across normal and wrap
   cases.
2. Unsigned TIMER count delta across wrap.
3. Integer ppm calculation for exact nominal, fast, and slow counts; guard zero
   elapsed time.

Do not require nrfx hardware in native_sim tests.

## Verification

Run from repo root/dev shell:

```bash
fw-build-5340
fw-build-54l15
west twister -T tests/unit -p native_sim --inline-logs
git diff --check
```

If broad Twister command has pre-existing unrelated failures, run all existing
unit suites individually and report exact commands/results. No warning may be
ignored.

Do not flash or run hardware in this implementation handoff. Hardware stream
validation follows orchestrator review.

## Out of scope

- PI/controller API changes.
- Applying measured ppm to SAMPLE_ADJUST.
- ASRC/Phase 5 work.
- Direct RADIO access or SDC event-start task.
- Audible-quality claims.
- nRF5340 clock behavior changes.

## Delivery

Inspect status/diff/log, stage only scoped files, and commit completed work.
No amend, push, merge, PR, force operation, or attribution footer. Return files,
behavior, tests with exact results, commit hash/message, blockers, deviations,
and recommended Phase 4b.2 follow-up.
