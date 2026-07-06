# Phase 3 — Clock Recovery v2 (nRF5340) — Implementation Handoff

Status: **ready for implementation**
Phase plan source: `docs/design.md` §Phase 3 + §Clock recovery

## Goal

Refactor `audio_drift` into a platform-independent PI controller that outputs
**ppm** (not APLL register units), with a **frequency term** from ISO timestamps
+ a **phase term** from I2S buffer fill (fixes design.md F6). Introduce an
actuator interface; APLL becomes the first actuator. The packet-repeat
fallback in `audio_i2s.c` must NOT fire in steady-state streaming.

Exit criterion: `audio status` shell shows zero I2S underruns during steady-
state streaming (no `Next buffers not supplied on time` log spam).

## Decisions locked

1. **Phase term source**: `k_mem_slab_num_free_get(&i2s_slab)` exposed to the
   controller. Setpoint = pre-fill level (6 blocks, matching the current
   `audio_i2s.c` pre-fill depth). Phase error = `(int)free_count - setpoint`.
   Positive = I2S consuming too slow (buffer filling, clock too slow →
   need +ppm). Negative = too fast (buffer draining, clock too fast →
   need -ppm). The slab free count is read at each SDU arrival (inside
   `audio_sink_sdu_ref_update`).

2. **PI gains**: conservative defaults, tunable via Kconfig later (not in this
   phase — just hardcode for now, add Kconfig in a follow-up if needed):
   - Frequency term: `Kp_f = 0.5`, `Ki_f = 0.1`
   - Phase term: `Kp_p = 0.3`, `Ki_p = 0.05`
   - Output = ppm (int32_t, can be negative)
   - Integrator wind-up protection: clamp integral terms to ±1000 ppm each.

3. **Actuator interface**: minimal `apply_ppm` shape. New files:
   - `src/audio_clock_actuator.h` — interface: `init`, `apply_ppm`, `reset`.
   - `src/audio_clock_actuator_apll.c` — nRF5340 APLL implementation (ppm →
     HFCLKAUDIO register trim). Selected via `CONFIG_AUDIO_CLOCK_ACTUATOR_APLL`.
   - Phase 4 will add `audio_clock_actuator_sample_adjust.c` behind the same
     interface. For now only the APLL actuator exists.

4. **Controller output convention**: positive ppm = "local clock is too slow,
   speed it up." The APLL actuator converts: ppm → register offset from center
   (1 APLL step ≈ 3.3 ppm, so offset = ppm / 3.3, register = center + offset).
   The conversion is actuator-specific, NOT in the controller.

5. **Keep the old `audio_drift.h` API as a thin shim** during the refactor.
   `audio_drift_update()` and `audio_drift_reset()` still exist but now
   delegate to the new controller. `audio_drift_state_str()` returns the new
   controller state. This avoids breaking the shell (`audio_shell.c` calls
   `audio_drift_state_str()`) and the bsim test. The APLL register constants
   (`AUDIO_DRIFT_APLL_CENTER` etc.) move to the actuator file — keep them
   also in `audio_drift.h` for backward compat (or have `audio_drift.h`
   include the actuator header).

6. **Measurement window**: keep 100 ms (same as today). The frequency term is
   computed per-window (ISO timestamp delta vs expected). The phase term is
   computed per-SDU (every ~10 ms) and fed to the integrator. The controller
   outputs a new ppm value at each SDU, but only applies it to the actuator
   at the same cadence (every SDU). This is finer-grained than the old 100 ms
   update — the PI loop reacts to phase drift every 10 ms, not every 100 ms.

## In scope

### A. Create `src/audio_clock_actuator.h` (interface)

```c
#ifndef AUDIO_CLOCK_ACTUATOR_H
#define AUDIO_CLOCK_ACTUATOR_H

#include <stdint.h>

/**
 * Initialize the clock actuator (e.g. set APLL to center frequency).
 * Returns 0 on success, negative errno on failure.
 */
int audio_clock_actuator_init(void);

/**
 * Apply a ppm correction to the clock.
 * Positive ppm = local clock too slow, speed it up.
 * Negative ppm = local clock too fast, slow it down.
 * Returns 0 on success, negative errno on failure.
 */
int audio_clock_actuator_apply_ppm(int32_t ppm);

/**
 * Reset the actuator to its default/center state.
 */
int audio_clock_actuator_reset(void);

#endif /* AUDIO_CLOCK_ACTUATOR_H */
```

### B. Create `src/audio_clock_actuator_apll.c` (nRF5340 APLL actuator)

Implements the interface for nRF5340's HFCLKAUDIO APLL. Selected when
`CONFIG_AUDIO_CLOCK_ACTUATOR_APLL=y`.

- `init()`: set APLL to center frequency (`AUDIO_DRIFT_APLL_CENTER`).
- `apply_ppm(ppm)`: convert ppm → APLL register offset. 1 step ≈ 3.3 ppm.
  `offset = ppm / 3.3` (use integer math: `offset = (ppm * 10) / 33`).
  `register = CLAMP(center + offset, MIN, MAX)`.
  Call `nrfx_clock_hfclkaudio_config_set(register)`.
  Guard with `#if NRF_CLOCK_HAS_HFCLKAUDIO` — on platforms without it
  (nRF54L15), the functions are no-ops returning 0.
- `reset()`: set APLL back to center.

Move the APLL constants from `audio_drift.h` into this file (or a private
header). `audio_drift.h` can keep them for backward compat, or the actuator
file defines them privately and `audio_drift.h` is updated to not export them.

**Kconfig**: add to `Kconfig` (the project's, not Zephyr's):
```kconfig
choice AUDIO_CLOCK_ACTUATOR
	prompt "Audio clock actuator"
	default AUDIO_CLOCK_ACTUATOR_APLL

config AUDIO_CLOCK_ACTUATOR_APLL
	bool "APLL (nRF5340 HFCLKAUDIO)"
	help
	  Use the nRF5340 HFCLKAUDIO APLL for clock steering.
	  ppm → APLL register trim.

config AUDIO_CLOCK_ACTUATOR_NONE
	bool "None (no actuator)"
	help
	  No clock actuator. Controller runs but output is discarded.
	  For testing on platforms without a clock steering mechanism.
endchoice
```

### C. Refactor `src/audio_drift.c` → PI controller

Rewrite `audio_drift.c` as a PI controller. New internal state:

```c
struct drift_controller {
    /* Frequency term (computed per 100 ms window) */
    uint32_t meas_start_us;
    int32_t freq_err_ppm;      /* last frequency error in ppm */

    /* Phase term (computed per SDU) */
    int phase_setpoint;        /* target slab free count (6) */
    int32_t phase_err_ppm;     /* last phase error in ppm */

    /* PI integrators */
    int32_t freq_integral;     /* accumulated, clamped ±1000 */
    int32_t phase_integral;    /* accumulated, clamped ±1000 */

    /* Output */
    int32_t output_ppm;        /* current total ppm output */

    /* State */
    enum { DRIFT_INIT, DRIFT_CALIB, DRIFT_LOCKED } state;
};
```

**`audio_drift_update(sdu_ref_us)`** — new signature stays the same for
backward compat, but now also needs the phase term. Add a new function:

```c
int32_t audio_drift_controller_update(uint32_t sdu_ref_us, int slab_free_count);
```

This returns ppm (not APLL register). The caller (`audio_i2s.c`) passes
`k_mem_slab_num_free_get(&i2s_slab)` as `slab_free_count` and calls
`audio_clock_actuator_apply_ppm(result)`.

Keep `audio_drift_update(sdu_ref_us)` as a backward-compat shim that calls
`audio_drift_controller_update(sdu_ref_us, 6)` (setpoint, no phase error) —
used by the bsim stub and old tests until they're updated. Actually, better:
update the callers to use the new function. The old `audio_drift_update` can
be removed if all callers are updated. Check: `audio_i2s.c` is the only
caller of `audio_drift_update`. The bsim stub provides its own empty impl.
So: **change the API** — `audio_drift_update` → `audio_drift_controller_update`
with the new signature. Update `audio_i2s.c` and the test.

**Controller math** (per SDU call):

1. **Frequency term** (every 100 ms window):
   - `elapsed = sdu_ref_us - meas_start_us`
   - If `elapsed < 100000`: skip frequency update (keep last freq_err_ppm).
   - If `elapsed` in [100000, 300000]: compute `err_us = 100000 - elapsed`.
     `freq_err_ppm = err_us * 10` (100 ms window, 1 µs = 10 ppm).
     `meas_start_us = sdu_ref_us`.
   - If `elapsed > 300000`: reset window, `freq_err_ppm = 0`.
   - Frequency PI: `freq_integral += Ki_f * freq_err_ppm` (clamp ±1000).
     `freq_output = Kp_f * freq_err_ppm + freq_integral`.

2. **Phase term** (every SDU):
   - `phase_err = slab_free_count - setpoint` (e.g. free=8, setpoint=6 → +2).
   - Convert to ppm: `phase_err_ppm = phase_err * phase_scale`.
     `phase_scale` = ppm per block of drift. A 480-sample block at 48 kHz =
     10 ms. If the buffer is 1 block off, that's ~10 ms drift accumulated
     over... we need a time reference. Simpler: treat phase_err as a direct
     ppm-ish signal. `phase_scale = 50` (1 block off → 50 ppm correction).
     This is tunable. Keep it conservative.
   - Phase PI: `phase_integral += Ki_p * phase_err_ppm` (clamp ±1000).
     `phase_output = Kp_p * phase_err_ppm + phase_integral`.

3. **Output**: `output_ppm = freq_output + phase_output` (clamped to ±500 ppm
   total — the APLL range is ±600 ppm, leave headroom).

4. **State machine**: simplify. Drop the INIT/CALIB/LOCKED hysteresis — the
   PI loop IS the state machine. Keep a simple `DRIFT_INIT` (first call,
   no data yet) → `DRIFT_ACTIVE` (running). The `LOCKED` concept is replaced
   by the integrator settling. `audio_drift_state_str()` returns "INIT" or
   "ACTIVE" (or keep "CALIB"/"LOCKED" for shell compat — your call, but
   update the shell if you rename).

### D. Update `src/audio_i2s.c`

`audio_sink_sdu_ref_update` changes:
- Read slab free count: `int free = k_mem_slab_num_free_get(&i2s_slab);`
- Call `audio_drift_controller_update(sdu_ref_us, free)` → returns ppm.
- Call `audio_clock_actuator_apply_ppm(ppm)`.
- Remove the `nrfx_clock_hfclkaudio_config_set` call (now in the actuator).
- Remove the `#if NRF_CLOCK_HAS_HFCLKAUDIO` guard (now in the actuator).

`drift_reset()` changes:
- Call `audio_drift_reset()` (resets controller state).
- Call `audio_clock_actuator_reset()` (resets APLL to center).

`audio_sink_init()` changes:
- Call `audio_clock_actuator_init()` after I2S configure.

Remove the `#include "audio_drift.h"` APLL constant usage — the APLL
constants now live in the actuator. Keep `#include "audio_drift.h"` for
the controller API.

### E. Update `src/audio_drift.h`

New API:
```c
/**
 * PI controller update. Called per SDU with ISO timestamp + buffer fill.
 * Returns ppm correction (positive = speed up local clock).
 */
int32_t audio_drift_controller_update(uint32_t sdu_ref_us, int slab_free_count);

/** Reset controller to initial state. */
void audio_drift_reset(void);

/** Current state string for shell diagnostics. */
const char *audio_drift_state_str(void);
```

Keep `AUDIO_DRIFT_APLL_CENTER/MIN/MAX` defined here for backward compat
(the old test uses them). Or move them to a private actuator header and
update the test. **Simplest**: keep them in `audio_drift.h` for now —
the actuator file includes `audio_drift.h` to get them. Not ideal coupling
but avoids test breakage. Phase 4 can clean up.

### F. Update `src/audio_shell.c`

The `cmd_status` command calls `audio_drift_state_str()` — still works.
Add the current ppm output to the status display:
```c
shell_print(sh, "  Drift state    : %s", audio_drift_state_str());
shell_print(sh, "  Drift ppm      : %d", audio_drift_get_ppm());
```
Add `audio_drift_get_ppm()` to `audio_drift.h` — returns the current
`output_ppm` for diagnostics.

### G. Update `CMakeLists.txt`

Add the actuator source (conditionally compiled):
```cmake
target_sources(app PRIVATE
  src/main.c
  src/bt_bap.c
  src/audio_decode.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_stats.c
  src/audio_volume.c
)

zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_APLL src/audio_clock_actuator_apll.c)
zephyr_sources_ifdef(CONFIG_SHELL src/audio_shell.c)
```

### H. Update `tests/unit/drift`

The existing tests test the OLD API (`audio_drift_update` returns uint16_t
APLL register). Rewrite them for the new API
(`audio_drift_controller_update` returns int32_t ppm). New test cases:

1. `test_zero_ts_ignored` — same concept, new API.
2. `test_init_to_active_on_first_ts` — first call enters ACTIVE, returns 0 ppm
   (no data yet, or small initial correction).
3. `test_freq_term_positive_err` — elapsed > 100 ms → freq_err negative →
   ppm positive (speed up). Verify `ppm > 0`.
4. `test_freq_term_negative_err` — elapsed < 100 ms → freq_err positive →
   ppm negative (slow down). Verify `ppm < 0`.
5. `test_phase_term_buffer_draining` — slab_free < setpoint → buffer draining
   → clock too fast → ppm negative. Verify `ppm < 0`.
6. `test_phase_term_buffer_filling` — slab_free > setpoint → buffer filling
   → clock too slow → ppm positive. Verify `ppm > 0`.
7. `test_integrator_clamp` — large sustained error → integral clamps at ±1000.
8. `test_output_clamp` — total output clamps at ±500 ppm.
9. `test_convergence` — feed a consistent +50 ppm freq error for N SDUs →
   output converges toward +50 ppm (within tolerance).
10. `test_gap_resets` — elapsed > 300 ms → window reset, freq_err = 0.
11. `test_uint32_wraparound` — same as existing, new API.

The test needs to simulate slab_free_count. The controller is pure math —
no hardware deps. The test calls `audio_drift_controller_update(ts, free)`
with crafted values.

**CMakeLists.txt** update: the test links `audio_drift.c` (same as today).
No actuator code needed — the controller is actuator-agnostic.

### I. Update `Kconfig` (project root)

Add the `AUDIO_CLOCK_ACTUATOR` choice (section B above). Default to
`AUDIO_CLOCK_ACTUATOR_APLL`. Add `AUDIO_CLOCK_ACTUATOR_NONE` for nRF54L15
(testing without an actuator — Phase 4 adds the real one).

The nRF54L15 board conf (`boards/nrf54l15dk_nrf54l15_cpuapp.conf`) should
set `CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=y` (no APLL on nRF54L15). The nRF5340
board defconfig or `prj.conf` keeps the default `APLL`.

### J. Packet-repeat fallback — keep but verify

The `DRIFT_THRESHOLD` + `saved_frame` packet-repeat logic in `audio_i2s.c`
stays as an emergency fallback. With the PI loop converging, it should NOT
fire in steady state. The exit criterion is verified via `audio status`
shell showing zero underruns. Do NOT remove the fallback — it's the safety
net for transient disruptions (connection events, ISO gaps).

## Out of scope (DO NOT touch)

- GRTC + DPPI drift measurement — Phase 4.
- `SAMPLE_ADJUST` actuator — Phase 4.
- ASRC — Phase 5/6.
- `audio_i2s.c` slab/DMA/underrun-recovery internal logic — only the
  `audio_sink_sdu_ref_update` + `drift_reset` + `audio_sink_init` functions
  change (to call the new controller + actuator).
- `audio_decode.c/h`, `bt_bap.c/h`, `audio_sink.h` — unchanged.
- `audio_volume.c/h`, `audio_stats.c/h` — unchanged.
- `prj.conf` — unchanged (the actuator choice goes in Kconfig, defaults to APLL).
- `boards/ebyte/e83_nrf5340/` — unchanged.
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf` — add `CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=y`
  (one line, that's it).
- `docs/design.md`, `docs/nrf54l15-drift-compensation.md` — unchanged.
- `sysbuild.cmake`, `Kconfig.sysbuild` — unchanged.

## Test plan / verification

1. **nRF5340 build**:
   ```bash
   fw-build-5340
   ```
   Must complete. Check `CONFIG_AUDIO_CLOCK_ACTUATOR_APLL=y` is in the
   resolved config: `grep AUDIO_CLOCK_ACTUATOR build/nrf5340/le-audio-receiver/zephyr/.config`.

2. **nRF54L15 build**:
   ```bash
   fw-build-54l15
   ```
   Must complete. Check `CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=y`.

3. **Unit tests** (rewrite existing):
   ```bash
   west build -b native_sim tests/unit/drift -d build/test-drift --pristine
   ./build/test-drift/drift/zephyr/zephyr.exe
   ```
   All test cases must pass. Report output.

4. **Decode unit test still passes** (regression):
   ```bash
   ./build/test-decode/decode/zephyr/zephyr.exe
   ```

5. **No stale APLL register output**: the controller outputs ppm, NOT APLL
   register values. Verify:
   ```bash
   git grep -n 'audio_drift_update\|APLL_FREQ_ADJ' -- src/
   ```
   `audio_drift_update` should not exist (replaced by
   `audio_drift_controller_update`). `APLL_FREQ_ADJ` should not exist (the
   conversion is in the actuator).

6. **Actuator called from audio_i2s.c**:
   ```bash
   grep -n 'audio_clock_actuator' src/audio_i2s.c
   ```
   Should show `init`, `apply_ppm`, `reset` calls.

7. **Shell status shows ppm**:
   ```bash
   grep -n 'ppm\|drift_get_ppm' src/audio_shell.c
   ```
   Should show the new ppm display line.

## Constraints and invariants

- **nRF5340 must keep building + booting identically.** Boot log unchanged.
- **nRF54L15 must keep building.** With `ACTUATOR_NONE`, the controller runs
  but output is discarded — no crash.
- **`settings_load()` ordering** — unchanged (not touched).
- **`audio_i2s_stop` must not clear `configured`** — unchanged.
- **I2S double-write gotcha** — unchanged (slab logic untouched).
- **No phone testing** — use BlueZ + USB BT adapter for future tests.

## Commit structure

1. **actuator interface**: `audio_clock_actuator.h` + `audio_clock_actuator_apll.c`
   + Kconfig choice. No callers yet — just the new files.
2. **controller refactor**: rewrite `audio_drift.c/h` to PI controller with
   ppm output + phase term. Update `audio_i2s.c` to call new controller +
   actuator. Update `audio_shell.c` for ppm display.
3. **unit tests**: rewrite `tests/unit/drift` for new API. All cases pass.
4. **nRF54L15 actuator none**: set `CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=y` in
   the nRF54L15 board conf.
5. **docs**: update AGENTS.md (drift section, status, gotchas).

Commit messages: no AI attribution. Imperative style.

## Hard rules

- Implement ONLY the scoped Phase 3 work.
- Do NOT touch `audio_i2s.c` slab/DMA/pre-fill/underrun logic (only the
  `audio_sink_sdu_ref_update`, `drift_reset`, `audio_sink_init` functions
  change).
- Do NOT run `fw-flash-5340` or any hardware command. Build + unit tests are
  the exit criterion. Hardware streaming verification (underrun count) is
  the orchestrator's job after review.
- Do NOT push, merge, open PRs, amend, or force-push. Commit locally only.
- No AI/tool attribution in commit messages.
- If the build fails for a reason not covered here, STOP and report it.