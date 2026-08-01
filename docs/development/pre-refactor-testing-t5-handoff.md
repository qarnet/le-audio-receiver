# Phase T5 handoff — lifecycle, timing, drift, and production actuators

## Goal

Close Phase T5 from `docs/development/pre-refactor-testing-plan.md` by testing
production lifecycle, nRF54 timing, drift-controller, APLL-actuator, and
NONE-actuator behavior directly. Fix defects exposed by those tests. Preserve
all accepted T0–T4 behavior.

Starting point: clean `test/pre-refactor-behavior` at `97490b9`. T4 is accepted;
do not weaken or repin its BabbleSim contract.

## Scope

In scope:

- `src/stream_lifecycle.{c,h}` and `tests/unit/lifecycle/`.
- `src/audio_timing_nrf54.c`, narrow test-only seams, and a new
  `tests/unit/timing_nrf54/` suite using mocked GRTC/GPPI/TIMER HAL.
- `src/audio_drift.{c,h}` and expanded `tests/unit/drift/`.
- `src/audio_clock_actuator_apll.c` plus a production-source APLL suite.
- `src/audio_clock_actuator_none.c` plus a production-source NONE suite.
- Rename/relabel retired sample-adjust tests as explicitly historical.
- Canonical test/docs/status updates and final validation.

Out of scope:

- BAP/ASCS/BSim scenario additions or hash changes.
- Boot coordinator, shell, resolved-config checker (T6).
- Coverage tooling/thresholds (T7).
- Physical hardware acceptance (T8).
- Refactoring unrelated production modules.
- Push, merge, PR, amend, force-push, or history rewrite.

## Grounded current behavior and decisions

### Lifecycle edge semantics

`stream_lifecycle_sink_started()` currently returns the gate level
(`should_open`). `src/bt_bap.c:947-967` treats the return as a new-open edge and
resets performance state, starts offload, and emits the observer event. A
duplicate start therefore repeats one-time work.

Change return semantics to **true only for a closed-to-open transition**:

```c
bool opened = should_open && !audio_path_open;
if (opened) {
    audio_path_open = true;
}
return opened;
```

Keep existing configuration, release, close, reset, Mode A, Mode B, mono, and
out-of-range behavior. Update header documentation. Do not add locking:
lifecycle calls remain serialized by BAP callback context.

Expand lifecycle tests across:

- duplicate start after a single-ASE open: false, gate remains open;
- duplicate first and second starts in Mode A: only completion edge is true;
- close then start: one new edge, then duplicates false;
- configure/start/close/reconfigure/start permutations;
- release then slot reuse;
- reset from closed, partially started, and open states;
- repeated close and rapid open/close cycles;
- invalid/zero configurations and indices remain inert.

### nRF54 timing production suite

Create `tests/unit/timing_nrf54/`. Compile these real production files:

- `src/audio_timing_nrf54.c`
- `src/audio_timing_math.c`

Provide project-owned include-shadow mocks for exactly the used installed APIs:

- `<nrfx_grtc.h>`
- `<helpers/nrfx_gppi.h>`
- `<hal/nrf_grtc.h>`
- `<hal/nrf_timer.h>`

Do not copy timing algorithms into tests. Mocks capture calls, arguments,
callbacks, captured TIMER value, GRTC `now`, allocation/schedule failures, and
resource state. Mock `audio_drift_frequency_error_update()` captures delivered
ppm.

Avoid fake devicetree parsing. Under `AUDIO_TIMING_NRF54_TEST`, use a test-owned
`NRF_TIMER_Type` object instead of `DT_NODELABEL(timer20)`/`DT_REG_ADDR`. Keep
production devicetree path unchanged outside that compile definition.

Add narrow test-only seams, guarded by `AUDIO_TIMING_NRF54_TEST`, for:

1. submitting deferred diagnostic work through a test wrapper that captures
   the `k_work` instead of dispatching it automatically;
2. resetting file-static module state between tests without pretending to
   release hardware;
3. only if mocks cannot otherwise observe it, reading minimal active/generation
   state.

Prefer invoking the callback captured by mocked
`nrfx_grtc_channel_callback_set()` and then explicitly running captured work.
Do not export production-only APIs or change non-test behavior.

Required timing tests:

1. GRTC allocation failure returns exact error and performs no later setup.
2. GPPI allocation failure disables GRTC compare/interrupt state through
   `nrfx_grtc_syscounter_cc_disable(grtc_channel)`, then frees the channel;
   no GPPI free occurs because allocation did not succeed.
3. Successful init configures TIMER mode/32-bit/prescaler 0, CLEAR then START,
   allocates/enables GPPI, initializes work, and is idempotent.
4. Update before init and zero SDU timestamp are no-ops.
5. First valid timestamp creates one anchor/compare; later updates in the same
   session do not reschedule.
6. Past first compare schedules `now + 1 s`; future anchor schedules
   `anchor + 1 s`; include 32-bit timestamp wrap.
7. First compare callback schedules next compare and records baseline but does
   not deliver feedforward.
8. Second callback delivers exact ppm; include TIMER32 wrap.
9. Late callback reschedules at `now + 1 s`.
10. Reschedule failure clears active and produces one deferred error payload;
    no later measurement is delivered.
11. Reset clears session state, disables compare, increments generation, and
    permits one new anchor.
12. Work captured before reset is rejected as stale by generation.
13. Every non-stale measurement reaches
    `audio_drift_frequency_error_update()`, while diagnostic log pacing does not
    suppress feedforward.

Installed NCS v3.3.0 grounding:

- `modules/hal/nordic/nrfx/drivers/include/nrfx_grtc.h` declares
  `int nrfx_grtc_syscounter_cc_disable(uint8_t channel);`.
- Its implementation disables CCEN and interrupt without requiring the channel
  to have been marked used, then unmarks use.
- `nrfx_grtc_channel_free()` alone does not undo callback interrupt/CCEN state.
- `nrfx_gppi_conn_alloc()` returns before writing a handle on allocation
  failure; no GPPI free is valid on that path.

Fix `audio_timing_init()` GPPI-failure cleanup accordingly. Do not invent a
callback-clear API; none exists in installed NCS.

### Drift arithmetic and concurrency

Keep controller tuning, signs, first-update behavior, filter ratio,
anti-windup, and clamps unchanged for normal production inputs.

Make all public `int32_t`/`int` input values defined:

- Compute EMA delta/update in `int64_t`, then clamp to `INT32_MIN..INT32_MAX`
  before storing.
- Negate filtered frequency in `int64_t` so `INT32_MIN` is valid.
- Compute phase subtraction and scaling in `int64_t`.
- Compute proportional term, integral increment/candidate, phase sum, and final
  total in `int64_t`.
- Clamp integral to configured integral rails before narrowing.
- Clamp final output to configured output rails before narrowing.

Do not silently clamp `slab_free_count` to a guessed slab size; arithmetic must
remain safe for full `int` range while preserving sign and final rails.

Add tests for:

- `INT32_MIN/MAX` frequency updates and `INT_MIN/MAX` slab counts;
- expected rail/sign without UBSan-visible signed overflow;
- long runs (at least 100,000 updates) at setpoint and both phase extremes,
  proving bounded output and setpoint stability;
- symmetric feedforward-rail phase unwind;
- actual concurrent update/frequency/reset loops using Zephyr threads and a
  deterministic final reset, proving no deadlock and final INIT/zero state;
- accepted normal boundaries and existing exact outputs remain unchanged.

### Production actuator suites

Create `tests/unit/actuator_apll/` that compiles
`src/audio_clock_actuator_apll.c` directly. Supply include-shadow mocks for
`<hal/nrf_clock.h>` and `<nrfx_clock_hfclkaudio.h>`, define
`NRF_CLOCK_HAS_HFCLKAUDIO=1`, and capture every register write.

Change APLL ppm conversion to use `int64_t` for `(ppm * 10) / 33`, center
addition, and clamp before conversion to `uint16_t`. Preserve C truncation
toward zero and existing constants. Cover:

- init and reset write `AUDIO_DRIFT_APLL_CENTER`;
- exact positive/negative conversions;
- ±1, ±2, ±3 ppm near-zero truncation;
- values producing exact MIN/MAX;
- values beyond both rails including `INT32_MIN/MAX`;
- repeated calls and `consume_sample_adjustment()==0`.

If practical, add a second no-HFCLKAUDIO test variant that compiles the same
production file with `NRF_CLOCK_HAS_HFCLKAUDIO=0` and proves all no-op returns.
This variant must not duplicate conversion logic.

Create `tests/unit/actuator_none/` compiling
`src/audio_clock_actuator_none.c` directly. Cover init/reset/consume and
apply of zero, both signs, and `INT32_MIN/MAX`; all return zero and have no
state.

Rename `tests/unit/actuator/` to a name such as
`tests/unit/actuator_sample_adjust_historical/`. Change testcase ID/tags and
file comments so both runner output and documentation say `historical` and
`retired`; continue compiling the real retired source. Do not delete tests or
make this actuator selectable in production.

## Test infrastructure and warnings

Follow existing native_sim production-source patterns under `tests/unit/`.
Each new suite needs `CMakeLists.txt`, `prj.conf`, and `testcase.yaml` and must
be auto-discovered by `scripts/test-all.sh`. Test-only include shadows and hooks
must never enter production firmware builds.

Compiler warnings and Kconfig assigned-value warnings are failures. Do not add
warning suppressions or `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n`. Do not weaken
T4 parser warning policy.

## Documentation

Update:

- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `STATUS.md`
- `scripts/test-all.sh` header counts if its documented counts change

Record exact test counts, exact validated commit, production defects fixed, and
commands/results. Mark T5 accepted only after final validation. Say T6 next.

## Verification

Run focused suites while developing, then on exact final code commit run:

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
git status --short
```

The full gate includes accepted T4 BabbleSim matrix; no alternate reduced gate
is acceptance evidence. Workstation validation may use a detached exact-commit
worktree/bundle, but leave workstation `main` and both repositories clean and
remove temporary refs/worktrees/bundles/logs afterward.

## Commit and recap requirements

Use logical commits for production/test changes and final evidence docs. Do not
amend existing commits. Before returning, inspect status, diff, and recent log;
stage only intended files. Return files/behavior changed, focused/full tests,
build results, exact commit hashes/messages, blockers, deviations, and cleanup
state.

## Escalation rule

Stop and ask Orchestrator for help after two materially different failed
attempts, or if production/SDK evidence conflicts with this handoff. Do not
copy production logic into tests, weaken assertions, normalize warnings, pin
unexplained values, invent architecture, or loop on speculative patches.
Preserve worktree state and report exact blocker, attempts, commands/logs/diff,
git status, one precise question, and smallest hypothesis. Do not commit
knowingly failing or incomplete work during escalation.
