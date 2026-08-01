# STATUS — le-audio-receiver — 2026-07-31

> Probe identities are resolved at runtime via `nrf-probes`. Never assume a
> serial↔board mapping from docs — run `nrf-probes`.

## Pre-refactor testing track — STARTED (2026-07-31)

Test plan accepted: `docs/development/pre-refactor-testing-plan.md`.  Phases
T0–T8 lock current supported behavior before large-scale refactoring.

**Phase T0 — behavior contract and honest coverage map** — ACCEPTED (2026-07-31).

Creates `docs/testing/behavior-contract.md` (57 numbered contracts for
supported and unsupported behavior), `docs/testing/coverage-matrix.md` (every
production source file classified with current evidence and gaps), and
`docs/testing/v0.0.1-baseline.md` (release and RF-fix baseline evidence).

Acceptance evidence:

- Full gate **21 PASS / 0 FAIL / 21 TOTAL** on the provisioned workstation
  (`thomas-workstation`) from a detached temporary worktree of the exact T0
  commit, transferred via non-destructive git bundle; BSim hashes
  deterministic across repeated runs (10 ms `0xFE0D4245`,
  7.5 ms `0x5853F445`).
- All three builds pass on the T0 commit: `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle`.
- No production behavior changed; T0 touched documentation only.
- No numeric line/branch coverage is claimed; no honest coverage report
  exists until Phase T7 instrumentation.

**Phase T2 — audio pipeline unit characterization** — ACCEPTED (2026-08-01).

Locks LC3 decode/routing, volume, and statistics to direct production-source
proof: deterministic 48 kHz golden fixtures, real-decode golden tests, and
three production decoder defects fixed.  Evidence:
`docs/testing/t2-audio-pipeline-tests.md`; updated
`docs/testing/behavior-contract.md` (CODEC-006..010, STAT-001) and
`docs/testing/coverage-matrix.md`.

- `tests/fixtures/lc3/` — four checked-in fixture pairs (mono 7.5/10 ms,
  Mode B 7.5/10 ms; 60-byte LC3 frames), reproducible generator
  (`generate.sh`, liblc3 C API, `-O3 -std=c11 -ffast-math`), deterministic
  and path-independent (verified by repeated runs), SHA-256 + CRC-32
  recorded.  Tests embed the binaries; fixtures are never regenerated
  during test runs.
- `tests/unit/decode` — 37 tests execute real `audio_decode.c` +
  `audio_stats.c` + real liblc3 1.1.2: byte-exact golden PCM for all four
  fixtures, full/per-channel CRC-32, config rejection (liblc3 untouched),
  SDU rejection with output guards, PLC accounting, overlap-safe mono
  expansion, Mode B dual accounting, hard-failure accounting.
- `tests/unit/volume` — 12 tests execute the real VCP branch against a
  test-local shadow of the exact NCS v3.3.0 renderer types + fake
  `bt_vcp_vol_rend_register()`; real `audio_perf.c` proves hook balance on
  all exits; concurrent callback toggling proves atomic snapshot packing.
- `tests/unit/stats` — 10 tests execute real `audio_stats.c`: exact
  counter coupling, reset, by-value snapshots, 4-thread concurrent exact
  counts.
- Fixed defects: mono in-place expansion overlap corruption (backward
  expansion when input/output share the base); Mode B right-channel
  decoder accounting (was: success/PLC uncounted); hard decode failures
  propagated as `-EBADMSG` instead of success (second Mode B decoder still
  invoked to keep independent state aligned).
- liblc3 1.1.2 semantic correction: malformed bitstream of valid length
  returns 1 (PLC), not a hard negative — verified empirically; the
  hard-error accounting path is exercised via a test-only linker wrap of
  `lc3_decode()` (delegates to the real implementation otherwise).
- **BSim Stage 1 oracle hashes updated** to the corrected mono-decode
  values: 10 ms `0xFE0D4245` → `0x9225F075`, 7.5 ms `0x5853F445` →
  `0x2011C0F9`.  The old hashes locked in the forward-expansion overlap
  bug (every 960-sample push collapsed to the frame's first sample —
  constant energy 12480).  The T2B overlap-safe fix changes the decoded
  PCM by construction; the new values are deterministic across repeated
  runs (see `docs/testing/t2-audio-pipeline-tests.md`).
- ASCS response-code mapping of decode-layer rejection remains T4 (known
  gap preserved).

Focused suites (desktop `thomas-main`): decode 37/37, volume 12/12,
stats 10/10 — zero compiler warnings.

Acceptance evidence:

- Full gate on the provisioned workstation (`thomas-workstation`) from a
  detached temporary worktree of the exact T2 commit, transferred via
  non-destructive git bundle: **23 PASS / 0 FAIL / 23 TOTAL**, run twice
  consecutively, both clean; BSim hashes deterministic in every run —
  10 ms `0x9225F075`, 7.5 ms `0x2011C0F9` (pairwise equality enforced;
  these are the corrected mono-decode values, see the hash-change note
  above).
- All three builds pass on the T2 commit: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` (both desktop and workstation) —
  documented Kconfig/CMake diagnostics only, no compiler warnings.
- Desktop full gate is 22/23 locally: the `bsim: stage1` child cannot run
  on `thomas-main` because the BabbleSim component binaries are not built
  there (`~/ncs/v3.3.0/tools/bsim/bin/bs_2G4_phy_v1` missing); the
  workstation provides the authoritative BSim leg (same as T1).
- Production APIs/wire/audio formats unchanged except documented safe
  decode-layer rejection and defect corrections; production images
  contain no test hooks (test-only compile definitions and the
  `lc3_decode` linker wrap are applied only by test CMakeLists).
- Worktree clean after scoped commits; workstation repo returned to clean
  `main` with all temporary refs/worktrees/bundles removed.

**Phase T3 — I2S sink state-machine tests** — ACCEPTED (2026-08-01).

Compiles and executes the **real `src/audio_i2s.c`** under native_sim with
a controllable fake I2S driver and mocked platform dependencies, locking
slab ownership, startup, steady-state drift, ASRC offload/fallback,
underrun recovery, and stop behavior for both production
resampler/actuator shapes.  Evidence:
`docs/testing/t3-audio-i2s-tests.md`; updated
`docs/testing/behavior-contract.md` (I2S-001..009) and
`docs/testing/coverage-matrix.md` (`audio_i2s.c` row).

- `tests/unit/audio_i2s_common/` — shared harness: fake I2S driver
  (`struct i2s_driver_api`, nrfx-style TX block ownership, ordered
  records, failure injection, DROP/PREPARE purge through the captured
  config slab, double-submit violation detection), mocks for
  timing/drift/actuator/rate-converter/ASRC/offload/stats/perf, the
  narrow `AUDIO_I2S_NATIVE_TEST` production hooks, shared
  `vnd,audio-i2s-fake` binding + overlay exposing alias `i2s-audio`.
- `tests/unit/audio_i2s/` (ASRC_LINEAR + NONE actuator + OFFLOAD_ASRC +
  47619 Hz) — 50 tests; `tests/unit/audio_i2s_identity/` (IDENTITY + APLL
  + 48000 Hz) — 48 tests.  Variant selection via test-only CMake compile
  definitions; no invalid Kconfig assignments.
- Three production defects fixed (see commit `fix: make I2S startup
  ownership transactional`): non-transactional startup (silent pre-fill
  failures, START leak, START after incomplete pre-fill); unbounded input
  frame setter (identity path could copy past the fixed slab block); and
  untrusted offload output accepted (zero-frame/oversized results with
  post-state commit).  Also hardened: init failure clears stale state,
  saved-frame/sequence does not leak across stop, invalid CPU-ASRC frame
  counts rejected with slab release, rate-converter silence counts
  outside [1, 481] rejected, repeat fallback never issues zero-length
  writes.
- Production images contain no test hooks: everything test-side is
  guarded by `AUDIO_I2S_NATIVE_TEST`, which production firmware never
  defines.
- BSim Stage 1 oracle hashes re-verified on every workstation gate run:
  10 ms `0x9225F075`, 7.5 ms `0x2011C0F9` (corrected T2 values, unchanged
  by T3 — audio path behavior identical).

Acceptance evidence:

- Focused suites on the desktop (`thomas-main`): audio_i2s 50/50,
  audio_i2s_identity 48/48 — zero compiler warnings.
- Full gate on the provisioned workstation (`thomas-workstation`) from a
  detached temporary worktree of the exact final T3 commit, transferred
  via non-destructive git bundle: **25 PASS / 0 FAIL / 25 TOTAL**, run
  twice consecutively, both clean; BSim hashes deterministic in every run
  — 10 ms `0x9225F075`, 7.5 ms `0x2011C0F9`.
- All three builds pass on the T3 commit: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` (both desktop and workstation) —
  documented Kconfig/CMake/DT diagnostics only, no compiler warnings.
- Desktop full gate is 24/25 locally: the `bsim: stage1` child cannot run
  on `thomas-main` because the BabbleSim component binaries are not built
  there (`~/ncs/v3.3.0/tools/bsim/bin/bs_2G4_phy_v1` missing); the
  workstation provides the authoritative BSim leg (same as T1/T2).
- Worktree clean after scoped commits; workstation repo returned to clean
  `main` with all temporary refs/worktrees/bundles removed.
- **T4 is next**: BAP receive handling (stream receive → decode → sink
  push integration).

### T3 review-fix round (2026-08-01, commit `fix: preserve active I2S state across reinit`)

Removes the re-initialization regression introduced by T3's init state
clearing.  `audio_sink_init()` is now idempotent: when the sink is already
configured — possibly streaming — an accidental repeated call returns 0
immediately without touching device-ready/configure/dependency init,
`started`, the saved frame, input frame selection, ASRC/offload state, slab
ownership, or the I2S queue, and without issuing any DROP/PREPARE.  First-
attempt init failures still leave `configured` false and permit a retry that
performs the full normal init exactly once; re-init never resets the
negotiated input frame selection (360 preserved).

New tests in both variants (replacing the old re-init-clears-state test):
init-success then immediate second init (no-op, no additional calls); init +
started DMA then second init (started retained, exact queued pointers/count
and slab free count unchanged, no trigger or dependency call added); retry
success after configure / actuator / timing first-attempt failure (both
variants) and after ASRC-init failure (ASRC variant); input-frame-selection
preservation across re-init and across failed-then-successful retry.

- Focused suites on the desktop (`thomas-main`): audio_i2s 50/50,
  audio_i2s_identity 48/48 — zero compiler warnings.
- Full gate on the provisioned workstation (`thomas-workstation`) from a
  detached temporary worktree of the exact final T3 commit (including the
  review fix), transferred via non-destructive git bundle: **25 PASS /
  0 FAIL / 25 TOTAL, run twice consecutively**, both clean; BSim hashes
  deterministic in every run — 10 ms `0x9225F075`, 7.5 ms `0x2011C0F9`
  (unchanged).
- All three builds pass on the review-fix commit (desktop and workstation):
  `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — documented
  Kconfig/CMake/DT diagnostics only, no compiler warnings.
- Desktop full gate is 24/25 locally: the `bsim: stage1` child cannot run
  on `thomas-main` because the BabbleSim component binaries are not built
  there (same as T1/T2); the workstation provides the authoritative BSim
  leg.

**Phase T4 — BAP and Bluetooth behavior matrix** — ACCEPTED (2026-08-01).

Expands the BabbleSim gate from one mono scenario into the full BAP
matrix over real `src/bt_bap.c`, `src/audio_decode.c`, real Zephyr
BAP/ASCS/PACS, real ISO transport, and real liblc3.  Evidence:
`docs/testing/t4-bap-bsim-matrix.md`; updated
`docs/testing/behavior-contract.md` (BT-001, CODEC-007 closed;
CODEC-011..013 added) and `docs/testing/coverage-matrix.md`.

- One receiver binary and one parameterized client binary carry the 15
  BST test IDs (`mono_10ms`, `mono_7p5ms`, `modea_10ms`, `modea_7p5ms`,
  `modea_reverse_start_10ms`, `modeb_10ms`, `modeb_7p5ms`,
  `invalid_sdu_resume_10ms`, `modea_first_stop_10ms`,
  `release_without_disable_10ms`, `disconnect_streaming_10ms`,
  `reconnect_second_stream_10ms`, `unsupported_source_direction`,
  `no_free_sink_slot`, `invalid_codec_fields`).  The client TX is a
  repository-owned deterministic multi-channel transmitter
  (`tests/bsim/client/src/bsim_tx.[ch]`, real BAP send + liblc3,
  integer-only channel/sequence-dependent PCM patterns, TX hold until the
  scenario-required stream count is streaming, exact send caps, one
  injectable malformed SDU).  The sink stub became a scenario-aware
  strict oracle (`audio_sink_stub.c`, `bsim_sink_oracle.h`) with
  per-segment full/L/R ordered FNV-1a hashes, per-channel energy bounds,
  startup/PLC accounting, and never-hidden push-after-stop.  BSim-only
  observer (`bsim_observer.[ch]`) emits passive events from real
  production flow; BSim-only resource seam (3 sink ASEs + 1 source PAC,
  pool limited to 2) reaches the NO_MEM path and source rejection
  through the real ASCS server.  The runner (`scripts/bsim-stage1-run.sh`,
  name retained) flocks the shared `bsim_out` tree, compiles once per
  gate, uses one private log root (preserved on failure or
  `BSIM_KEEP_LOGS=1`), runs scenarios 1–8 twice and 9–15 once, and
  strict-parses every named PASS field via
  `scripts/bsim_stage1_parse.py` (36 unit tests in
  `tests/unit/bsim_runner`); `BSIM_BASELINE=1` prints hashes for
  pinning.
- Known hashes (replacing the T2/T3 mono values, which the stronger T4
  TX pattern deliberately changes): mono 10 ms `0xD65641A8`
  (L==R `0x08D96D5C`), mono 7.5 ms `0x3CF61E00` (L==R `0xC915A389`),
  Mode A/B 10 ms `0x7335E317` (L `0x08D96D5C`, R `0x2AE744DB`; Mode A
  and Mode B identical because both produce the same deterministic
  L/R patterns — a deliberate channel-identity cross-check), Mode A
  7.5 ms `0xC05F0EA7`, Mode B 7.5 ms `0xE18E30AE`,
  invalid-SDU-resume `0xB29C3A18`, reconnect segment 2 equals a fresh
  mono 10 ms oracle (`0xD65641A8`).  Pinned from two identical baseline
  runs; reproduced exactly by the two acceptance matrix runs and both
  full gates.
- Production defects fixed: (1) the codec-shape validator stored the
  channel-allocation return value as octets-per-frame (0), silently
  disabling the exact SDU length check; (2) Mode A pairing could never
  match across CISes — the SW Split LL numbers each CIS from a
  CIG-global counter (constant seq offset = activation delay), so
  pairing now uses the ISO SDU reference time (`BT_ISO_FLAGS_TS`), equal
  on both CISes of one CIG at each event, with a wrap-safe 32-bit
  comparison discarding only the older unmatched half; (3) release-from-
  streaming crashed the ASCS server (the release path wiped
  `stream->iso`, which the server's streaming-exit transition
  dereferences) — release now clears only app-owned slot state and lets
  the server detach at the ASE idle transition; (4) the injected
  malformed SDU must be 119 bytes (a 1-byte SDU is dropped by the ISO
  stack before the BAP callback) — still an exact-length violation.
- Documented deviations: the malformed SDU is 119 bytes not 1 (above);
  Mode A normal scenarios carry a small deterministic post-start PLC
  delta (3 for 10 ms, 18 for 7.5 ms) with nonzero concealment — pinned
  per scenario instead of zero; stop-finalized segments pin the exact
  total decoder invocations instead of exact `dec_calls × pushes`;
  Zephyr's cosmetic `Invalid application error code: 9` warning is
  allowlisted for `invalid_codec_fields` (the wire response is exactly
  what the app chose); scenario 15 runs its nine invalid variants as
  three rounds of three attempts on fresh connections (a rejected Config
  leaves the client endpoint attached; no public detach for an idle
  ASE).
- Exact ASCS responses pinned: source direction
  `CONF_UNSUPPORTED/NONE`; third sink config `NO_MEM/NONE` (and clean
  releases + reusable slot); nine invalid-codec-field variants each
  `CONF_INVALID/CODEC_DATA`; valid mono and missing-frame-blocks
  fallback each `SUCCESS/NONE` (failures consumed no slot).
- Lifecycle evidence: first-ASE stop closes the gate with 10
  closed-gate receives and zero pushes after close; release-without-
  disable closes the gate and stops sink/offload exactly once;
  disconnect-while-streaming cleans up with zero late pushes and the
  advertising restart path ready; reconnect streams a second segment
  byte-identical to a fresh mono 10 ms oracle.

Acceptance evidence:

- Desktop: both BSim binaries compile warning-free; parser unit tests
  36/36; `fw-build-5340/54l15/dongle` clean (documented diagnostics
  only).
- Workstation (`thomas-workstation`, detached worktree of the exact
  final T4 commit, bundle-transferred, repo main never modified):
  matrix with pinned hashes **PASS twice consecutively** (four
  consecutive identical runs counting the baselines); full gate **26
  PASS / 0 FAIL / 26 TOTAL twice consecutively**; all three production
  builds pass; `git diff --check` clean.
- Worktree/repo cleanup: workstation returned to clean `main` with all
  temporary refs/worktrees/bundles removed (after the T4 review round).

**Phase T5 — lifecycle, timing, drift, and production actuators** — ACCEPTED (2026-08-01).

Closes T5 from `docs/development/pre-refactor-testing-plan.md` by testing
production lifecycle, nRF54 timing, drift-controller, APLL-actuator, and
NONE-actuator behavior directly, and fixing the defects those tests exposed.
Evidence: updated `docs/testing/behavior-contract.md` (LIFE-003 closed;
CLOCK-008..010 added) and `docs/testing/coverage-matrix.md`; handoff:
`docs/development/pre-refactor-testing-t5-handoff.md`.

- **Lifecycle closed-to-open edge** — `stream_lifecycle_sink_started()`
  returns true only for a closed-to-open transition; duplicate starts
  while the gate is open return false, so the caller's one-time open
  work (perf reset, offload start, observer event) runs exactly once
  per stream lifecycle.  Gate state and Mode A/B/mono decisions
  unchanged.  `tests/unit/lifecycle/` grows 13 → 22 tests.
- **Defined drift arithmetic** — every public input is defined across
  the full `int32_t`/`int` range: EMA delta/update, feedforward
  negation (INT32_MIN valid), phase subtraction/scaling, proportional
  term, integral candidate, phase sum, and final total computed in
  `int64_t`, clamped to the configured rails before narrowing.  The
  slab count is never silently clamped.  Tuning, signs, first-update
  behavior, filter ratio, anti-windup, and clamps unchanged for normal
  inputs.  `tests/unit/drift/` grows 18 → 29 tests, including
  100 000-update long runs at setpoint and both phase extremes,
  symmetric feedforward-rail phase unwind, and real-thread concurrent
  update/frequency/reset loops with a deterministic final reset; the
  focused run is clean under `-fsanitize=undefined` (trap-on-error).
- **nRF54 timing production suite** — new `tests/unit/timing_nrf54/`
  (18 tests) compiles real `audio_timing_nrf54.c` +
  `audio_timing_math.c` against test-owned include shadows of the
  installed nrfx_grtc/nrfx_gppi/nrf_grtc/nrf_timer HALs and a mock of
  `audio_drift_frequency_error_update()`.  Narrow
  `AUDIO_TIMING_NRF54_TEST` seams (test-owned `NRF_TIMER_Type` object
  instead of devicetree, deferred-work capture instead of dispatch,
  state reset, minimal active/generation/overflow reads) never enter
  production firmware.  Production defect fixed: **GPPI-allocation
  failure left the GRTC compare event and interrupt enabled** — init
  now runs
  `nrfx_grtc_syscounter_cc_disable(grtc_channel)` before
  `nrfx_grtc_channel_free(grtc_channel)` on that path (no GPPI free:
  the allocation never succeeded).  Tests pin allocation-failure exact
  errors, cleanup ordering, full init sequence + idempotence,
  pre-init/zero-ts no-ops, one anchor per session, past/future/32-bit-
  wrap first compares, baseline + exact-ppm callbacks incl. TIMER32
  wrap, late reschedule, reschedule failure (inactive + one deferred
  error), reset semantics, stale-generation rejection, and
  per-measurement drift delivery despite log pacing.
- **Production actuator suites** — new `tests/unit/actuator_apll/`
  (8 tests, UBSan-clean) compiles real `audio_clock_actuator_apll.c`
  against include-shadow `hal/nrf_clock.h`/`nrfx_clock_hfclkaudio.h`
  with a register-write mock; the ppm→register conversion now computes
  `(ppm * 10) / 33`, the center addition, and the rail clamp in
  `int64_t` before narrowing to `uint16_t`.  New
  `tests/unit/actuator_apll_nohfclk/` compiles the same production file
  with `NRF_CLOCK_HAS_HFCLKAUDIO=0` proving all no-op returns with zero
  writes (no conversion logic duplicated).  New `tests/unit/actuator_none/`
  compiles real `audio_clock_actuator_none.c`.  The retired
  sample-adjust suite moved to
  `tests/unit/actuator_sample_adjust_historical/` and is labeled
  historical/retired in testcase ID, tags, and comments (real retired
  source still compiled; not selectable in production).

Acceptance evidence (desktop `thomas-main` + workstation `thomas-workstation`):

- Focused suites on the desktop while developing: lifecycle 22/22, drift
  29/29 (plus a `-fsanitize=undefined` trap-on-error run), timing_nrf54
  15/15, actuator_apll 8/8 (plus UBSan run), actuator_apll_nohfclk 1/1,
  actuator_none 1/1, actuator_sample_adjust_historical 7/7 — zero
  compiler warnings.
- Full gate on the exact final code commit `1a504e0` (bundle-transferred
  to a detached worktree on `thomas-workstation`; repo main never
  modified): **30 PASS / 0 FAIL / 30 TOTAL** — all 20 twister C suites,
  4 exec-only C suites, 5 Python suites, and the accepted T4 BabbleSim
  matrix with all pinned hashes unchanged (mono 10 ms `0x22AB5C0D`,
  Mode A/B 10 ms `0xBAE24F7E`, reconnect = fresh mono oracle, etc.).
- All three production builds pass on `1a504e0`: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` — zero compiler warnings
  (documented non-actionable diagnostics only; the `-Winfinite-recursion`
  regression found by the first 54l15 build was fixed in `1a504e0`).
- Desktop `test-all.sh` run on the same code state: 29/30 — the single
  failure is `bsim: stage1` because the BabbleSim component binaries are
  not built on `thomas-main` (same as T0–T4; the workstation provides
  the authoritative BSim leg).
- `git diff --check` clean; worktree/repo cleanup: workstation returned
  to clean `main` with all temporary refs/worktrees/bundles/logs
  removed; desktop repo clean on `test/pre-refactor-behavior`.
- **T6 is next**: boot coordinator, shell, and resolved-config checker.

**Phase T6 — boot, shell, and resolved-config contracts** — ACCEPTED (2026-08-02).

Closes T6 from `docs/development/pre-refactor-testing-plan.md` with a narrow
testable boot coordinator, direct production shell-command tests, and a
stdlib-only resolved build-contract checker for both production targets.
Handoff: `docs/development/pre-refactor-testing-t6-handoff.md`.  Evidence
updates: `docs/testing/behavior-contract.md` (APP-001..003 closed;
APP-006, APP-007, BUILD-002..005 closed, BUILD-007, BUILD-008 added),
`docs/testing/coverage-matrix.md`, `STATUS.md`.

- **Boot coordinator** — new production `src/app_lifecycle.{c,h}` owns the
  fatal init order (watchdog → Bluetooth → settings → volume → BAP → I2S →
  optional nonfatal nRF54 platform init → advertising) and the
  disconnect advertising restart.  `main.c` retains all hardware wiring
  (watchdog device/thread, real subsystem wrappers incl.
  `bt_enable(NULL)` / `sys_reboot(SYS_REBOOT_COLD)`, nRF54-only
  `flpr_handshake_init` → `audio_offload_init` → `flpr_runtime_init`,
  device-name log, `bt_bap_wait_disconnect()` loop).  `settings_load()`
  stays after Bluetooth enable and before BAP/PACS registration.
  `tests/unit/app_lifecycle/` (13 tests, new twister suite) compiles the
  production coordinator: exact all-success order incl. platform,
  platform-absent, each of the seven fatal steps failing independently
  (no later callback, exactly one cold reboot, original errno returned),
  restart success (only advertising) / restart failure (one reboot +
  error), NULL ops and every missing required callback → `-EINVAL` with
  zero calls and zero reboots.  `main.c` never continues into normal
  operation after a nonzero boot/restart result.
- **Shell behavior tests** — new twister suites compile and execute the
  REAL production `src/audio_shell.c` through the Zephyr dummy backend +
  `shell_execute_cmd()` against mocked subsystem APIs (real
  `audio_perf.c` with deterministic cycle injection):
  `tests/unit/audio_shell/` 13 tests (perf enabled), `tests/unit/
  audio_shell_noperf/` 10 tests (`CONFIG_AUDIO_PERF_MEASUREMENT=n`),
  `tests/unit/audio_shell_nrf54/` 16 tests (`CONFIG_SOC_NRF54L15` TU,
  mocked FLPR APIs).  Locks exact field labels/values for `audio status`,
  `audio perf` (path labels, zero-count averages, integer one-decimal
  deadline %, queue fields), `audio reset-stats`, `audio perf-reset`,
  `audio stop`, `bt unpair` (exact negative errno propagation), and every
  FLPR field consumed by `scripts/flpr_hang_gate.py` (handshake health/
  epoch/errors/TX/RX/loss/order, ring counters/diagnostics/test/latency/
  stall, offload state/epoch/generation/counters/faults/recovery/
  probation/runtime-restart/heartbeat-dedup/RTT/last-error, ASRC
  counters/faults/RTT/cycles, runtime state/stage/requests/epochs/
  reload/CRC/errno/duration/DMCONTROL/INITPC/CPURUN, restart EBUSY/
  OK-line/failure).  Narrow `AUDIO_SHELL_TEST`-guarded wrappers expose
  static handlers to tests only; production firmware never compiles them.
- **Production defects fixed** (all found by the new tests):
  1. `audio reset-stats` and `audio perf-reset` were registered as
     `reset - stats` / `perf - reset` — a shell command name cannot
     contain spaces, so the commands were unreachable; renamed to the
     documented hyphenated names.
  2. `audio perf` with measurement disabled computed the deadline
     percentage against a fake 1 µs deadline (untruthful ~5000.0%);
     now prints a truthful unavailable (zero) `0.0%` — table shape
     unchanged, division-safe.
  3. `flpr runtime` indexed state/stage string arrays with raw enum
     values — an out-of-range enum read past the array; replaced with
     bounded switch-based conversion printing `UNKNOWN` / `unknown`.
  4. `audio status` PLC percentage multiplied in `uint32_t` — overflowed
     for large counters; numerator now `uint64_t`, integer truncation
     preserved, zero frames still prints `(0%)` with no division.
  Note: `flpr_hang_gate.py`'s `RE_RUNTIME_RESTART_OK` regex is
  informational-only (not a gate check) and has drifted from the
  production `FLPR restart OK: epoch a→b …` line since Stage 4A; T6
  locks the current production format and leaves the gate parser
  untouched (out of T6 scope, hardware-phase follow-up).
- **Resolved build-contract checker** — new `scripts/check-build-contract.py`
  (stdlib only, deterministic PASS/FAIL report listing every failed
  assertion in one run, exit 0 only when all pass) parses the resolved
  `.config` + `zephyr.dts` beneath each sysbuild root — app image,
  nRF5340 `hci_ipc` controller image, nRF54L15 `flpr` image — and asserts
  **74 contracts**: nRF5340 path (identity+APLL, no ASRC/NONE, 48000 Hz,
  LIBLC3, two sink ASEs, MCK bypass, 7/6/6 host counts, `i2s0` okay with
  12.288 MHz HFCLKAUDIO and exact BCK P1.15/LRCK P1.12/SDOUT P1.13 pin
  cells, QSPI disabled, WDT0 okay; netcore `BT_LL_SW_SPLIT=y` +
  peripheral/connection ISO, controller counts equal to host, chosen
  `zephyr,bt-hci` → okay `bt_hci_controller` with
  `zephyr,bt-hci-ll-sw-split` compatible while `bt_hci_sdc` is
  disabled), nRF54L15 path (ASRC linear + NONE, no APLL/identity,
  offload ASRC, 47619 Hz, LIBLC3, two sink ASEs, 3/1/1/3 counts with
  host ISO TX == controller ISO TX, `i2s20` okay + PCLK32M + exact
  SCK P1.4/LRCK P1.5/SDOUT P1.6/MCK P1.7 cells, PDM20/SPI00/MX25R64
  disabled, TIMER20 reserved, `rfsw_ctl` `<&gpio2 5 1>` + `rfsw_pwr`
  `<&gpio2 3 0>` both `regulator-boot-on`, LFXO/HFXO internal 16000 fF,
  exact non-overlapping contiguous SRAM ranges within
  `0x20000000..0x20040000`, FLPR code partition `0x165000`+`0x18000`,
  FLPR image cross-checks: `cpuflpr_sram`/chosen `zephyr,sram`/
  `zephyr,code-partition`/`FLASH_BASE_ADDRESS`/`FLASH_LOAD_SIZE` vs
  app-side launcher ranges) and the 48 kHz capability statement: resolved
  half proven from build data, PACS LTV explicitly NOT claimed as a
  resolved property (C object, pinned by T2/T4 tests per BT-002), plus a
  clearly-labeled source check (SRC-001/002) on `src/bt_bap.c`.
  `tests/unit/build_contract/test_build_contract.py` (28 tests, new
  python suite) covers a complete valid dual-target fixture, missing
  image/file, duplicate/malformed config, explicit unset vs set symbols,
  comment-only DTS satisfaction attempts, wrong status/compatible/
  chosen/pins/counts/RF polarity/capacitance, missing/overlapping/
  out-of-range memory intervals, SW Split Kconfig-only and DTS-only half
  failures, and the deterministic multi-error report with nonzero exit;
  it never depends on pre-existing firmware build directories.  Added to
  `scripts/test-all.sh`.

Acceptance evidence (desktop `thomas-main`, branch `test/pre-refactor-behavior`):

- Focused during development: app_lifecycle 13/13, audio_shell 13/13,
  audio_shell_noperf 10/10, audio_shell_nrf54 16/16, build_contract
  28/28 — zero compiler warnings (native_sim test-entropy notice is the
  same pre-existing line every twister suite emits).
- Full gate on the exact code commit `cc13c85` (desktop `thomas-main`,
  branch `test/pre-refactor-behavior`): **34 PASS / 1 FAIL / 35 TOTAL** —
  all 24
  twister C suites (incl. the four new ones), 4 exec-only C suites, 6
  Python suites (incl. build_contract 28/28), and the accepted T4
  BabbleSim matrix leg; the single failure is `bsim: stage1` because the
  BabbleSim component binaries are not built on `thomas-main` (same
  environmental leg as T0–T5; the workstation provides the authoritative
  BSim leg).
- All three pristine production builds pass: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` — zero compiler warnings; only the
  documented non-actionable NCS v3.3.0 diagnostics (see "Build warning
  diagnostics"), no new entries.
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` on the pristine builds: **74 assertions,
  0 failed, BUILD CONTRACT PASSED, exit 0**.
- `git diff --check` clean; `git status --short` clean after the final
  commit.
- **T7 is next**: coverage enforcement (gcovr + `check-test-matrix.py`).

### T5 review-fix round (2026-08-01)

Closes the deferred-measurement loss defect found in Thinker review of
`102fc12`: the single `pending_diag` mailbox was overwritten by each GRTC
callback before `k_work_submit()`, and Zephyr may coalesce submissions
while the work item is pending/running, so a delayed system workqueue
could silently discard one-second feedforward measurements — contradicting
the every-measurement contract in `audio_drift.h` and CLOCK-008.
Handoff: `docs/development/pre-refactor-testing-t5-review-fix-handoff.md`.
Evidence updates: `docs/testing/behavior-contract.md` (CLOCK-008
review-fix), `docs/testing/coverage-matrix.md` (timing row).

- **Bounded FIFO** — the mailbox is replaced by a fixed, allocation-free
  16-entry `diag_fifo` protected by `diag_lock`.  The ISR producer appends
  measurement and schedule-error payloads in order via small private
  helpers (bounded critical section, no logging, no dynamic allocation);
  the work handler drains every queued payload in FIFO order in one
  invocation, so one work submission may represent many payloads and
  coalescing cannot lose measurements.  Each payload retains its
  generation: stale generations are discarded independently, reset does
  not rewrite queued payloads, and fresh-session payloads may follow old
  payloads and still deliver.  Logging cadence stays per-payload
  sequence.
- **Overflow is an explicit fault** — a full FIFO sets an observable
  overflow fault, clears `active`, and submits work; the handler emits
  one `LOG_ERR` and clears the report; measurement resumes only via a
  normal session reset and a new anchor; later callbacks while inactive
  do nothing (ISR entry guard).  The dropped payload belongs to that
  fault transition, not to normal feedforward loss.  Generation — not the
  inactive flag — is the staleness authority, so accepted payloads queued
  before a schedule failure or overflow still deliver (this removed the
  work-handler `active` discard that would have dropped the 16 accepted
  entries in the overflow case).
- **Tests** — `tests/unit/timing_nrf54/` grows 15 → 18: 10-payload
  backlog drained in FIFO order from one dispatch; stale-then-fresh mixed
  generations in one FIFO (old rejected, fresh delivered); full-FIFO
  overflow (16 accepted drained in order, fault observable and consumed,
  later callbacks inactive).  All pre-existing timing tests stay green;
  focused run 18/18 with zero warnings.

Acceptance evidence (exact final code commit `ad74125`,
bundle-transferred to a detached worktree on `thomas-workstation`; repo
main never modified):

- Full gate: **30 PASS / 0 FAIL / 30 TOTAL** — all 20 twister C suites
  (incl. timing_nrf54 18/18), 4 exec-only C suites, 5 Python suites, and
  the accepted T4 BabbleSim matrix with all pinned hashes unchanged.
- All three production builds pass: `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle` — zero compiler warnings (documented non-actionable
  diagnostics only).
- Desktop `test-all.sh` on the same commit: 29/30 — the single failure
  is `bsim: stage1` (BabbleSim component binaries not built on
  `thomas-main`; workstation provides the authoritative BSim leg).
- `git diff --check` clean; worktree/repo cleanup: workstation returned
  to clean `main`, all temporary refs/worktrees/bundles/logs removed;
  desktop repo clean on `test/pre-refactor-behavior`.

Closes the protocol, oracle, teardown, timestamp, and harness-race
defects found in orchestrator review.  Evidence:
`docs/testing/t4-bap-bsim-matrix.md` (review-fix section); updated
`docs/testing/behavior-contract.md` (CODEC-007, CODEC-012, CODEC-013)
and `docs/testing/coverage-matrix.md`.

- **Valid ASCS responses**: missing/invalid codec-shape configs now
  return `CONF_REJECTED / CODEC_DATA` (`CONF_INVALID` is excluded from
  the ASCS application response codes); source stays
  `CONF_UNSUPPORTED / NONE`, pool exhaustion `NO_MEM / NONE`.  The
  expected negative remote-request paths (unsupported source, rejected
  codec shape, pool full, malformed SDU) log at INFO level, so the
  strict runner rejects every remaining bt_bap/ASCS warning with **no
  allowlists**; the ASCS warning allowlist is removed entirely.
- **Mode A timestamp validation**: a VALID-flag SDU missing the ISO TS
  flag skips decoder/pairing/push, increments a receive/decode fault
  counter, emits a test observer event, and logs a real warning —
  **zero occurrences in every scenario** (`obs_mts=0`).  The ISOAL's
  `BT_ISO_FLAGS_LOST` sync-boundary SDUs carry no TS by definition and
  keep the concealment/startup-transient path.
- **Release stops the sink immediately**: Release calls
  `audio_sink_stop()` before returning to ASCS when it closes an open
  audio path; a passive observer event with a monotonic sequence number
  proves the stop precedes the ACL disconnect (scenario 10:
  `obs_rel_ss=1`, `rel_ss_seq=3 < disc_seq=5`).  Later
  disabled/disconnect paths stay idempotent and create no second
  segment.
- **Corrected ordered hashes**: every segment starts full/L/R at the
  FNV offset basis; samples convert to `uint16_t` before byte
  extraction; one helper prepends the 4-byte LE frame index then
  channel sample bytes.  New pinned values (mono 10 ms `0x22AB5C0D`,
  L==R `0x32777D65`; mono 7.5 ms `0x01A3EB05`; Mode A/B 10 ms
  `0xBAE24F7E`; Mode A 7.5 ms `0x00A5D3F9`; Mode B 7.5 ms
  `0xFF82CADB`; invalid-SDU-resume `0x0C61918D`; reconnect seg2 equals
  a fresh mono 10 ms oracle), baselined from two pairwise-identical
  post-fix runs and reproduced by two pinned runs and the full gate.
  Mode B L == mono L cross-checks hold.
- **Strict source-valid startup boundary, zero post-start PLC**: the
  production receive path reports per-push source validity to the
  oracle before every sink push; startup stays open until the first
  nonzero push sourced entirely from valid ISO input; `startup_plc`
  updates after each transient push (including the boundary-closing
  push, absorbing the interleaved sync-boundary LOST decodes).  Every
  scenario reports `plc == splc` exactly; the arbitrary 20-push
  zero-energy grace and the PLC-delta pin table are removed; the
  "inaudible" claim is removed.
- **Synchronized TX**: per-slot `generation` and `in_flight` counters
  under one mutex; candidate snapshots increment `in_flight` and
  decrement on every path after unlock; commits require matching
  generation and stream; register selects only idle slots, initializes
  encoders under the lock, reassigns a nonzero generation, and
  publishes `bap_stream` last; unregister/pause wait for `in_flight==0`
  without holding the lock; the lock never spans
  `net_buf_alloc`/`bt_bap_stream_send`; the registration log uses
  matching `%zu`/`%p`/`%u` arguments.  Focused runs after the rewrite
  were byte-identical to the baseline hashes.
- **Warnings-as-errors**: `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n` removed
  from the runner; both BSim binaries compile with warnings as errors,
  zero repo warnings; the glibc `_FORTIFY_SOURCE` diagnostic did not
  recur (no suppression needed).
- Scenario 15 accepts **two configs overall** (valid mono +
  missing-frame-blocks fallback) with nine `CONF_REJECTED / CODEC_DATA`
  rejections; the receiver PASSes only after both, so observer counts
  are not reset per round.  Mode A normal scenarios send 110 frames per
  stream (CIS-sync losses are startup transients; exactly 100
  valid-sourced pushes are pinned); mid-stream TX caps were removed
  from the lifecycle scenarios so the source stays valid until the gate
  closes.

Acceptance evidence (workstation detached worktree of the exact final
code commit `8542f1a`, bundle-transferred, repo main never modified):

- Focused hash-equivalence after the TX rewrite: mono_10ms /
  modea_10ms / release_without_disable byte-identical to the
  diagnostic baseline.
- Two post-fix baseline matrices: **PASS, pairwise identical fields**;
  two pinned matrices: **PASS** (four consecutive full-matrix PASSes).
- Parser unit tests: **33/33**.
- Full gate: **26 PASS / 0 FAIL / 26 TOTAL** (one run per the
  review-fix handoff — the matrix is the gate's long BSim child and no
  non-BSim code changed after it).
- Production builds `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle`: zero errors/warnings (documented non-actionable
  diagnostics only); `git diff --check` clean.
- Worktree/repo cleanup: workstation returned to clean `main` with all
  temporary refs/worktrees/bundles/logs removed.

### Transient gate run disposition (2026-08-01, T2 review fix)

One workstation gate run on the exact T2 commit (`63d8344`) produced
**21 PASS / 2 FAIL / 23 TOTAL**: children `exec: offload_asrc` and
`bsim: stage1` both failed because their generated `build.ninja` files
were truncated mid-write.  The same commit passed 23/23 immediately
before (run 1) and twice after (runs 3 and 4), so the acceptance evidence
above stands; this run is recorded here because the originally committed
STATUS omitted it.

Exact retained diagnostics (from the executor session; the full gate log
was deleted during cleanup before this review — see lost evidence below):

- Command context: `./scripts/test-all.sh` on `thomas-workstation`, worktree
  `/tmp/t2-validation/t2repo` at detached commit `63d8344`, invoked via ssh;
  strictly serial with the other gate runs (each invocation completed before
  the next started; no repo gate overlapped).
- `exec: offload_asrc` — Ninja parse error `ninja: error:
  build.ninja:7358: unexpected EOF`.  Build directory: a fresh
  `mktemp -d` root created by `test-all.sh` for that run (path not
  retained; removed by the script's own EXIT trap).
- `bsim: stage1` — `Failure building tests/bsim prj.conf for
  nrf5340bsim/nrf5340/cpuapp`; underlying `ninja: error:
  build.ninja:17942: unexpected EOF` during a CMake re-configure, with
  `CMake Error at cmake/modules/sysbuild_extensions.cmake:740 (message)`
  and `Configuring incomplete, errors occurred!`.  Build directory:
  `~/ncs/v3.3.0/zephyr/bsim_out/tests/bsim/bs_nrf5340bsim_nrf5340_cpuapp_le_audio_receiver_bsim_prj_conf`
  (shared `bsim_out` tree, subsequently overwritten by the later clean
  builds).
- Why repo source cannot truncate generated build files: `build.ninja`
  is written by CMake during configure into the build directory; repo
  sources are read-only inputs that are compiled into objects and never
  open, write, or truncate build files.  Truncation requires an external
  writer (concurrent process, filesystem fault, or an interrupted CMake
  write); no such writer can be identified from the retained evidence.
- No root-cause claim beyond the observed truncated generated files is
  made, and no claim that this is the same failure as T1's unidentified
  transient (no evidence supports that).

Lost evidence: `/tmp/t2_final_gate2.log` (the full run log) was removed
during post-acceptance cleanup before this review; the offload_asrc
temporary build directory was removed by `test-all.sh`'s EXIT trap; the
bsim build directory was overwritten by later successful builds.  Only
the diagnostics quoted above survive.

Per the review-fix handoff, the exact final commit was re-validated with
three consecutive serial full-gate runs on the workstation (all 23/23,
BSim hashes unchanged) — see the review-fix acceptance evidence below.

### T2 review-fix round (2026-08-01, commit `fix: harden audio test boundaries`)

Closes the portability/safety defects found during orchestrator review:

- **size_t-before-int validation** — `audio_decode_sdu()` keeps all
  length arithmetic in `size_t`; rejects `frame_len > INT_MAX` (valid and
  PLC), odd Mode B lengths (including PLC), out-of-range per-channel
  shapes (20..400, valid frames), and nonzero PLC shapes outside the
  range — before any narrowing cast.  Zero-length PLC stays supported
  (BSim startup).  Public header now documents exact supported config,
  PLC length semantics, output capacity, and `-EINVAL`/`-EBADMSG`
  outcomes.  New tests: `SIZE_MAX`, `INT_MAX+1`, odd/short/long PLC
  shapes, zero-length mono/Mode B PLC.
- **Volume null safety** — `audio_volume_apply()` gains an early no-data
  exit (NULL buffer or zero samples) after the performance timing starts,
  before the state read: mute/zero scaling can no longer run
  `memset(NULL, 0)`.  NULL with zero and nonzero samples is a
  deterministic no-op under muted/zero/unity/intermediate states with one
  balanced performance sample per call (locked by a new state-matrix
  test).
- **Generator defined behavior** — sample formulas convert the index to
  `uint32_t` before multiplication with `UINT32_C` constants; LE writing
  converts to `uint16_t` before shifting.  `generate.sh` uses `mktemp` +
  EXIT trap (no fixed executable path) and compiles without
  `-Wno-array-bounds` (verified warning-free under
  `-Wall -Wextra -Wdouble-promotion -Wvla -pedantic`; no liblc3
  false positive to suppress).  Root `.gitattributes` marks
  `tests/fixtures/lc3/*.lc3` and `*.pcm` binary.  Regeneration from two
  clean copies is byte-identical to the checked-in binaries — no fixture,
  README, or test CRC updates were needed.
- **Complete transient evidence** — the omitted 21/23 gate run is
  recorded above with exact retained diagnostics and a precise
  lost-evidence statement.

Review-fix acceptance evidence:

- Generator comparison: two clean-copy runs, all SHA-256/CRC-32 identical
  to checked-in values; `git diff` over the binary fixtures empty.
- Focused suites on the desktop (`thomas-main`): decode 43/43,
  volume 12/12 — zero compiler warnings.
- Full gate on the exact final commit (`fix: harden audio test
  boundaries`, the last commit of this round) from a detached temporary
  worktree of a non-destructive bundle on `thomas-workstation`:
  **three consecutive serial runs, all 23 PASS / 0 FAIL / 23 TOTAL**;
  BSim hashes unchanged in every run — 10 ms `0x9225F075`,
  7.5 ms `0x2011C0F9`.
- All three builds pass on the final commit: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` (documented diagnostics only).
- No recurrence of the truncated-`build.ninja` transient in any of the
  three runs.
- Worktree clean after the scoped commit; workstation repo returned to
  clean `main`; all temporary refs/worktrees/bundles/generator binaries
  removed.

**Phase T1 — FLPR production-source tests** — ACCEPTED (2026-07-31).

Replaces the false-confidence FLPR runtime, ring-manager, and handshake
suites with native_sim suites that compile and execute the real production
implementations.  Evidence and hook architecture:
`docs/testing/t1-flpr-production-tests.md`; updated
`docs/testing/coverage-matrix.md`.

- `tests/unit/flpr_runtime` — 21 tests execute the real nRF54 restart body
  (shadow VPR HAL, host source/execution arrays, ordered event log, mutex
  busy thread).  Fixed: `failed_stage` set before disconnect/stop, failed
  attempts included in `max_duration_ms`, header/module wording corrected
  (no active-stream rejection inside `flpr_runtime_restart()`, held-reset
  sequence).
- `tests/unit/flpr_ring_mgr` — 53 tests execute `src/flpr_ring_mgr.c` +
  real `flpr_ring.c`/`flpr_cache.c` with host ring arrays; production
  static IPC handlers run through the handshake-mock captured handlers.
- `tests/unit/flpr_handshake` — 44 tests execute `src/flpr_handshake.c`
  against a fake IPC service backend (NCS v3.3.0 ipc_service test pattern,
  real `ipc_service_*` APIs).  Fixed: `flpr_handshake_send_msg()` routes
  through `send_msg()` so failures count `err_send`; header wording
  corrected (ring handlers dispatch without the module spinlock).
- No copied restart/reset/produce/consume/callback algorithm remains
  primary proof in any of the three suites.
- Physical cache/FLPR entry-point behavior remains hardware-only; native
  `flpr_cache.c` coverage is API/barrier-call proof only.

**Review fix round (2026-07-31, commit `fix: enforce FLPR validation
before acceptance`)** — closes two production-contract violations found
during orchestrator review:

- `flpr_handshake_wait_new_ready()` fast path now requires
  `flpr.ready && flpr.acked && flpr.epoch != previous_epoch`.  A changed
  epoch whose READY_ACK send failed can no longer succeed without
  waiting; the fast path is taken only after the ACK for the changed
  epoch succeeded (new regression test + extended ACK-failure test).
- `flpr_ring_mgr_consume_asrc_result()` now verifies the payload CRC over
  the ring payload BEFORE copying PCM to the caller.  Every validation
  failure preserves both caller buffers byte-for-byte; only
  `result->output_frames` is zeroed, exactly as the header contract
  documents (sentinel-buffer tests for CRC, flags, frame range, reserved
  state, and zero-frame error output).
- T1 evidence (`docs/testing/t1-flpr-production-tests.md`) updated: the
  two previously "characterized" behaviors are now recorded as fixed
  defects; the inaccurate CRC "payload may carry data" characterization
  is removed.

Acceptance evidence:

- Focused suites on the desktop (`thomas-main`): runtime 21/21,
  ring manager 53/53, handshake 44/44 — zero compiler warnings.
- Full gate on the provisioned workstation (`thomas-workstation`) from a
  detached temporary worktree of the exact T1 commit, transferred via
  non-destructive git bundle: **21 PASS / 0 FAIL / 21 TOTAL** — three
  consecutive full-gate runs on the final validated commit, all clean;
  BSim hashes deterministic in every run — 10 ms `0xFE0D4245`,
  7.5 ms `0x5853F445`.
- Transient-failure disposition: the earlier single 20/21 gate run's
  failing child could not be identified after the fact — its console
  output was not retained (only BSim logs survive in `/tmp`) and no
  other evidence exists.  Per the review-fix handoff fallback rule, no
  root cause is claimed; the unsupported claim was removed and the
  final exact-commit gate was instead run three consecutive times, all
  passing (see above).
- All three builds pass on the T1 commit: `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle` (both desktop and workstation).
- Desktop gate is 20/21 locally: the `bsim: stage1` child cannot run on
  `thomas-main` because the BabbleSim component binaries are not built
  there (`~/ncs/v3.3.0/tools/bsim/bin/bs_2G4_phy_v1` missing); the
  workstation provides the authoritative BSim leg.
- Production fixed addresses and wire ABI unchanged; production images
  contain no test hooks (test-only compile definitions are applied only by
  test CMakeLists).
- Worktree clean after scoped commits; workstation repo returned to clean
  `main` with all temporary refs/worktrees/bundles removed.
- **T2 is next**: audio pipeline unit characterization (decode golden
  output, volume, stats).
- No numeric line/branch coverage is claimed; no honest coverage report
  exists until Phase T7 instrumentation.

The existing 432 unit tests (396 C + 36 Python) are a historical Phase 6
count and do NOT mean full production branch coverage.  Several suites
compile stubs, test copied models, or test retired implementations.
T1–T8 will replace weak tests with production-source tests and add coverage
for untested modules.

Manual connection after RF-switch fix (PR #3, merged
`20b37c405835e5c2c747fa7b072c4c0b752b29cd`) is recorded as hardware evidence
in the baseline but is not automated regression protection.

## Phase 3 — BlueZ/WirePlumber pairing and reconnect lifecycle — ACCEPTED (2026-07-31)

Phase 3 accepted with three autonomous strict stock playbacks on nRF54L15.
Full 12-step sequence (unpair, remove host device, pair, trust, connect,
playback, disconnect, reconnect, playback, reset, reconnect, playback) all
pass without repo harness. Three 30 s playbacks at 7.5 ms frame duration,
zero decode/I2S/offload faults each.

Preflight hardened with two corrections landed in final review:

- **SPA proof**: `_wait_for_bluez_spa()` now requires only `libspa-bluez5.so`
  mapped in the WirePlumber process (`/proc/<pid>/maps`). All fallback paths
  removed — `pw-cli info all` substring (final review) and `pw-dump`
  factory/device node (spa-proof-fix). Owned WP uses subprocess PID;
  active-seat WP resolves `MainPID` via systemd.
- **Remove fatal**: failed `bluetoothctl remove` is now fatal unless exact
  postcondition shows device object no longer exists and no `Paired`/`Bonded`
  state remains.

See `docs/development/phase3-results.md` for acceptance evidence,
`docs/development/bluez-wireplumber-phase3-final-review-handoff.md` for
execution handoff, and `docs/development/bluez-wireplumber-interoperability-plan.md`
for Phase 1–4 plan.

**Phase 4 compatibility expansion NOT needed.** Bare BAP passed with stock
WirePlumber main-systemwide playback. CAP/CAS remain disabled; no speculative
services or custom host policy required.

## Phase 2 — BlueZ/WirePlumber stock desktop gate — ACCEPTED (2026-07-31)

Phase 2 accepted with strict nonzero-audio/zero-fault evidence on nRF54L15,
stock WirePlumber main-systemwide playback:

- **30 s gate**: SDUs=4578, decoded=4729, decode_err=0, i2s_underrun=0,
  stream_reset=0 (~35.47 s at 7.5 ms / 133.3 fps).
- **120 s gate**: SDUs=16565, decoded=16722, decode_err=0, i2s_underrun=0,
  stream_reset=0 (~125.41 s at 7.5 ms / 133.3 fps).
- **Explicit runtime `I2S DMA started`** confirmed each run.
- **Canonical gate**: 20/20 gate tests pass.
- **BSim regression**: 10 ms hash `0xFE0D4245`, 7.5 ms hash `0x5853F445` —
  each scenario run twice with pairwise hash equality enforced; both
  fully deterministic across repeated runs.
- **Corrective fixes**: 10 ms missing-frame-duration fallback removed;
  I2S slab block count raised 12→16 (startup transient headroom);
  `INPUT_FRAMES` made dynamic for 7.5 ms stock PipeWire config.

See `docs/development/phase2-stock-desktop-gate-results.md` for full evidence
and `docs/development/bluez-wireplumber-interoperability-plan.md` for Phase
1–4 plan.

## Phase 1 — BlueZ/WirePlumber PACS availability — DONE (2026-07-30)

Sink Available Audio Contexts no longer cleared to `BT_AUDIO_CONTEXT_TYPE_NONE`
on ACL connect. ACL connection is not ASE ownership — stock desktop policy
(BlueZ/WirePlumber) reads PACS during connection and needs truthful contexts
to create audio devices. Regression test added to BSIM gate (PACS assertion
at PASS point verifies contexts non-NONE after connection + 100-frame stream).

Contexts persist from `bt_bap_init()` through connect/disconnect cycles.
Zephyr PACS restores default on ACL disconnect per spec — no manual restore
needed. See `docs/development/bluez-wireplumber-interoperability-plan.md` for
full Phase 1–4 plan and `docs/development/bluez-wireplumber-phase1-handoff.md`
for execution handoff.

## Stage 0 — PASS (2026-07-27)

Dongle compile-time identity fix landed (87b8d36). hci_ipc netcore firmware calls
`bt_ctlr_set_public_addr()` before `bt_enable_raw()` with lab-only address
`C0:AA:BB:CC:DD:EE` (see `dongle/hci_identity.h`). No more `btmgmt static-addr`
workaround — scanning and GATT discovery work natively. Build via `fw-build-dongle`.

SMP pairing fix landed (52abde1). `--peer-addr` raw-HCI path now uses
`own_address_type=public` (0x00) matching the dongle's compile-time identity.
Previously used Random (0x01) causing DHKey Check mismatch + SMP timeout.
ACL held open for `duration+120 s` so `Pair()` succeeds over existing ACL.

**Stage 0 gate closed**: 60 s Mode A stream (6000 frames, 100.0 fps) on nRF54L15.
Pairing + bond + 2 ASE config + CIS audio path fully verified on clean state
(no prior bonds). RX hex dump (chan_alloc 0x01+0x02) confirmed stereo content.
FLPR: healthy, errors zero, RX lost/dup/ooo/missed zero. Audio: push failures=0,
repeat fb=0, ASRC cap fail=0. Zero warnings, zero assertions, zero faults.

See `docs/development/phase6-stage0-results.md` for full verification evidence.

## BSIM Stage 1 — PASS + CLEANUP + REPEATED-RUN GATE (2026-07-31)

> **T2 update (2026-08-01):** the accepted oracle hashes below changed to
> `0x9225F075` (10 ms) and `0x2011C0F9` (7.5 ms) when the T2B mono
> overlap-safe expansion fix corrected the decoded PCM (the pre-T2 values
> locked in the forward-loop collapse defect).  See the T2 section above
> and `docs/testing/t2-audio-pipeline-tests.md`.

CONFIG_TEST decode bypass removed from `bt_bap.c`. BSIM now executes same
PLC/decode path as hardware.  Startup-zero/PLC oracle in `audio_sink_stub.c`
with local counters (not production `audio_stats`):
8 startup-zero pushes, 7 PLC frames (all before first nonzero PCM).  100
nonzero pushes, 104 client sends.  Fully deterministic across repeated runs.

10 ms (48_4_1) — two independent runs:
- `startup_zero=8`, `startup_plc=7`, `plc=7`, `total=108`
- `total == pushes + startup_zero` (108 = 100 + 8)
- `plc == startup_plc` (7 = 7, all PLC in startup)
- `hash=0xFE0D4245`, `energy=12480` (pairwise identical both runs)

7.5 ms (48_3_1) — two independent runs:
- `startup_zero=11`, `startup_plc=10`, `plc=10`, `total=111`
- `total == pushes + startup_zero` (111 = 100 + 11)
- `plc == startup_plc` (10 = 10, all PLC in startup)
- `hash=0x5853F445`, `energy=9636480..9637920` (pairwise identical both runs)

Repeated-run gate (`scripts/bsim-stage1-run.sh`): each scenario runs twice
with pairwise hash equality enforced AND known accepted values asserted
(10 ms → `0xFE0D4245`, 7.5 ms → `0x5853F445`).  Per-run unique logs with
all artifact paths and hashes printed.

Production `audio_stats` cleaned — `startup_zero`/`startup_plc` fields and
functions removed; startup accounting is local to sink stub.  Real-target
public API restored to pre-BSim shape.  Client `ASE_SRC_COUNT=2` (min viable;
upstream BUILD_ASSERT rejects 1; 0 compiles but `stream_tx_register` returns
-ENOMEM on zero-element `tx_streams[]`).  Official smoke exits non-zero →
**Baseline PARTIAL** (upstream teardown disable-race).  Both real-target builds
clean (nRF5340, nRF54L15).  Stage1 accepted as regular local gate; official
smoke remains PARTIAL.  Scope stops here: reconnect/Mode A/B/error injection
duplicate hardware coverage.  See `docs/development/bsim-stage1-results.md`.

## Phase 5 — COMPLETE (2026-07-27)

cpuapp fixed-point linear stereo ASRC accepted. Mode A (two mono ASEs) +
Mode B (single stereo ASE) each ran 600 s autonomous central streams on
nRF54L15 with zero faults. SAMPLE_ADJUST actuator removed from production
Kconfig; two actuators remain: APLL (nRF5340) and NONE (nRF54L15, ASRC
consumes ppm). All 20 ASRC unit tests pass (native_sim). nRF5340 builds
(hardware regression deferred — no E83 probe). See
`docs/development/phase5-hardware-acceptance-results.md`.

## Bottom line

**BLE ISO transport verified** — 3,000 ISO Data TX packets over 15 s through
nRF5340DK `hci_uart` central; two CISes (Mode A stereo), 48 kHz LC3 at 100
fps, zero flow-control stalls. Receiver SDUs arrive correctly.

**Standalone I2S20 hardware/DMA verified** — a standalone I2S20 tone test ran
20.001 seconds, fed 2,016 blocks, zero EIO/underrun. I2S20 register state
(ENABLE, PSEL, FRAMESTART) confirmed working. GPIO mapping D0/P1.4 (BCK),
D1/P1.5 (LRCK), D2/P1.6 (SDOUT) proven.

**Old DAC caused LRCK anomaly** — with the old DAC breakout connected and MUTE
low, D1/LRCK was held high (no toggling). With digital wires removed, D1
toggles. The old breakout/wiring assembly is incompatible or defective.

**Phase 4b.2 — PCLK feedforward + phase PI: HARDWARE PASS** (2026-07-26).
Refactored `audio_drift.c` to explicit PCLK frequency feedforward
(`audio_drift_frequency_error_update()`) and per-block buffer-phase PI
(`audio_drift_controller_update(slab_free)`).  Pure integer, no floating
point.  Corrected sign (phase error = setpoint - slab_free).  Directional
anti-windup: at saturation rail, same-direction phase increments are
blocked; opposite-direction increments are always allowed so the
integrator can unwind toward range.  Thread-safe: k_spinlock serialises
workqueue/audio-path/reset calls.  Configurable output clamp and phase
integral clamp via Kconfig.  nRF54L15 board sets output clamp=2000,
phase integral=150.  nRF5340 keeps defaults (500/500).  Removed
timestamp-based frequency estimation and `audio_sink_sdu_ref_update()`.
ISO timestamps now go ONLY to `audio_timing_sdu_ref_update()` for GRTC
scheduling.  nRF54 timing feeds every 1 s PCLK measurement (not just
diagnostics) to `audio_drift_frequency_error_update()` via the work
handler (ISR-safe).  Bounded runtime actuator evidence: in `audio_i2s.c`,
counts insert/drop adjustments; logs first and every 500th adjustment.
Closed-loop streaming verified: 4,500 frames / 45 s at 100 fps, PCLK
diagnostics +1,500..+1,757 ppm, channel-pair gate correct (drops-only
before first PCLK measurement), inserts dominate (186:1 ratio), clean
teardown.  Phase 4c (10-minute stability + listening test): **Technical
PASS** (60,000 frames / 600.00 s, zero disconnect, zero
slab-full/underrun/warning/error/fault, clean teardown).  Physical
audibility UNAVAILABLE (user did not provide listening report) — not failed,
not blocking further measurable work. Phase 5 ASRC is complete (see below).
Phase 4 evidence consolidated in `docs/development/phase4-acceptance-results.md`.

| Test | Result |
|------|--------|
| fw-build-5340 | PASS (8 Kconfig/CMake diagnostics; no compiler warnings) |
| fw-build-54l15 | PASS (5 Kconfig/CMake diagnostics; no compiler warnings) |
| ASRC unit tests | 20/20 PASS (native_sim) |
| drift unit tests | 18/18 PASS |
| actuator unit tests | 7/7 PASS |
| timing unit tests | PASS |
| lifecycle unit tests | PASS |
| decode unit tests | PASS |
| rate_convert unit tests | PASS |
| flpr unit tests | PASS (6 suites: handshake, protocol, ring, ring_mgr, runtime, audio_process) |
| audio_offload unit tests | PASS |
| offload_asrc unit tests | PASS |
| perf unit tests | PASS |
| Python gate tests | 36/36 PASS (gate + flpr_stall_gate) |
| BSIM Stage 1 | PASS (hash=0xFE0D4245 deterministic) |
| Total unit tests | 432 PASS (396 C + 36 Python) |

### Build warning diagnostics (2026-07-30)

Neither target produces compiler warnings in application or Zephyr source.
All printed diagnostics are Kconfig/CMake configuration messages.

**nRF5340 (8 diagnostics):**

| Diagnostic | Classification | Cannot remove because |
|---|---|---|
| Deprecated `PARTITION_MANAGER` / `_ENABLED` | NCS v3.3.0 SDK deprecation | Required for multi-image flash layout; no migration path in v3.3.0 |
| `__ASSERT()` statements globally ENABLED | Zephyr informational | Not a defect |
| `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice has no selection | Upstream Kconfig gap in SW Split LL | Both sub-options (`RELIABILITY`, `LOW_LATENCY`) depend on `BT_CTLR_CENTRAL_ISO`, which is disabled for peripheral-only; choice has no NONE fallback |
| Experimental `BT_LL_SW_SPLIT` | Required architecture | Only ISO-capable open-source controller for nRF5340 |
| Experimental `BT_CTLR_SET_HOST_FEATURE` | Required for ISO | Feature negotiation required |
| Experimental `BT_CTLR_PERIPHERAL_ISO` | Required for ISO | Peripheral ISO support required |
| `SB_CONFIG_PARTITION_MANAGER` sysbuild warning | Required sysbuild infrastructure | Partition manager required by NCS build system |

**nRF54L15 configuration diagnostics:**

| Diagnostic | Classification | Cannot remove because |
|---|---|---|
| FLPR `UART_CONSOLE=y` resolves to `n` | Upstream cpuflpr board defconfig conflict | Board enables UART console; FLPR image deliberately disables `SERIAL` and `CONSOLE` because it has no UART |
| FLPR/CPUAPP reserved-memory unit-address and `simple_bus_reg` warnings | Intentional downstream overlay of stock `nordic-flpr` memory nodes | Stock node names retain old unit addresses while repo overrides `reg` to reserve IPC and 64 KiB FLPR SRAM; resolved addresses and hardware operation are verified |
| FLPR stock RRAM `avoid_unnecessary_addr_size` warning | Upstream nRF54L15 DTS structure | Emitted from stock `rram@165000`; repo does not define that node |
| `__ASSERT()` statements globally ENABLED | Zephyr informational | Not a defect |
| `drivers__watchdog`: No SOURCES given | Zephyr internal: `CONFIG_WATCHDOG=y` but no DT node on nRF54L15 board | WD disabled on nRF54L15 would change firmware behavior (watchdog is desired) and is out of scope for closeout; DT node is board-level, not repo |

## Hardware in use

Probe identities resolved at runtime via `nrf-probes` — no static serials in docs.

| Role | Board | Console | Notes |
|------|-------|---------|-------|
| LE Audio central | nRF5340DK | none | runs `hci_uart`, attached to PC over J-Link VCOM |
| LE Audio receiver | nRF54L15 (Seeed Xiao) | `/dev/ttyACM0` @ 115200 (serial-mcp) | runs this repo's firmware |
| Logic analyzer | fx2lafw | — | D0=SCK (P1.4), D1=LRCK (P1.5), D2=SDOUT (P1.6) |

- PC-side BT controller: `hci0` = nRF5340DK `hci_uart` on
  **`/dev/ttyACM2`** (J-Link VCOM, USB iface 02 — not ttyACM1/iface-00)
  @ 1 000 000 baud, H4, HW flow control.
- Receiver advertises as "LE Audio Receiver".

## What works (verified)

- Discovery (raw unfiltered scan), connect (raw direct LE Extended Create
  Connection — kernel accept-list connect path is broken on SDC).
- JustWorks pairing, bonded, persists across reboots. Receiver logs
  `Pairing complete, bonded: 1`.
- BAP unicast server negotiation: 2× SelectProperties, 2×
  SetConfiguration, 2× Acquire (Mode A, FL/FR, SDU 120 @ 10 ms, 2M PHY).
- 2× CIS established, data path HCI both directions.
- ISO data TX: **3000 ISO Data TX packets over 15 s** (= 2 streams ×
  1500 frames) with **3024 Number of Completed Packets** events returned.
  No EAGAIN, no stall. `bap_central.py --duration 15` reports
  `Done: 1500 frames in 15.00 s (100.0 fps)`.
- ISO data RX at the receiver: valid SDUs arrive — `stream_recv tally:
  valid=1006 invalid=144` (climbing). BLE transport verified.
- Receiver-side recovery from the post-stream disconnect panic
  (`audio_sink_stop`: PREPARE before DROP in `src/audio_i2s.c`).
- **Clean ACL teardown** in `bap_central.py` (BlueZ Disconnect +
  raw-HCI helper termination) — three consecutive runs with no DK
  reset between them, no zombie-slot exhaustion. `fw-reset-dongle`
  helper exists for recovery from a crashed run that bypassed cleanup.

## What does NOT work / open

### I2S20 hardware evidence

Standalone I2S20 works. The old DAC breakout caused LRCK anomaly.

| Test | Result |
|------|--------|
| GPIO pin map | D0/P1.4 = BCK, D1/P1.5 = LRCK, D2/P1.6 = SDOUT. Confirmed. |
| PCLK32M clock source | Works. `PCLK32M_HFXO` UsageFault tracked separately. |
| Standalone I2S20 tone test | 20.001 s, 2,016 blocks fed, zero EIO/underrun. ENABLE=1, PSEL correct, FRAMESTART firing. |
| Old DAC digital wires connected, MUTE low | D1/LRCK held high — no toggling. Breakout/wiring incompatible or defective. |
| Old DAC digital wires removed | D1/LRCK toggles. GPIO toggling confirmed. |
| Main receiver with new DAC (Phase 4c) | **Technical PASS** — 60,000 frames / 600.00 s, zero disconnect, zero slab-full/underrun/warning/error/fault, clean teardown. See `docs/development/phase4-acceptance-results.md`. |
| External I2S analyzer | **PASS** — 24 MHz fx2lafw capture at DAC pins: BCK 1,525,637.347 Hz, LRCK 47,676.613 Hz, ratio 31.999701, SDOUT active. See `docs/development/phase4-acceptance-results.md`. |
| Phase 4a.2 rate conversion | **PASS** — 35 s stream, 0 slab-full, 0 underrun. Fixed-rate converter matches PCLK32M drain. See `docs/development/phase4-acceptance-results.md`. |

### Next actions (ordered)

 1. ~~**Phase 4b.1** — GRTC-referenced timing foundation~~ → PASS
 2. ~~**Phase 4b.2** — PCLK feedforward + phase PI~~ → PASS
 3. ~~**Phase 4c** — Hardware streaming verification~~ → Technical PASS
 4. ~~**Phase 5** — ASRC quality upgrade~~ → ACCEPTED (2026-07-27).
    Mode A + Mode B 600 s, zero faults. See
    `docs/development/phase5-hardware-acceptance-results.md`.
  5. ~~**Phase 6** — FLPR offload~~ → COMPLETE (2026-07-29). Stages 0–5 accepted.
     FLPR ASRC offload with cpuapp fallback. Stage 5: removed dead identity
     submit API + 1920 B scratch buffer, migrated lifecycle/recovery tests to
     ASRC, 432 unit tests pass, nRF54L15 CPUAPP FLASH 502904 B / RAM 152244 B.
     Hardware: Mode A 120 s + Mode B 120 s at 100 fps, zero faults.
     See `docs/development/phase6-stage5-results.md`.
   6. **BabbleSim** — cross-cutting verification track (research + implementation).
      Provision environment, fix sysbuild/harness, build smallest-useful
      nRF5340bsim dual-core scenario. See `docs/design.md` BabbleSim section.
   7. ~~**BabbleSim Stage 1** — ACCEPTED as regular local gate (2026-07-29).~~
      Production cleanup: startup accounting moved to local sink-stub counters;
      audio_stats.h/.c restored to pre-BSim shape.  Sink-only scenario, strict
      PCM oracle, hash=0xFE0D4245 deterministic across runs.  Client ASE_SRC_COUNT=2
      (min viable per upstream BUILD_ASSERT + stream_tx.c array sizing).  Official
      smoke remains PARTIAL.  Scope stops here: reconnect/Mode A/B/error injection
      duplicate hardware coverage under unmodeled I2S/FLPR.
      See `docs/development/bsim-stage1-results.md`.
   8. ~~**Phase 5 Final Gate (FLPR + cpuapp fallback)**~~ → COMPLETE (Phase 6 Stages 0–5).
      All gates met; 432 unit tests pass; nRF54L15 Mode A/B hardware proven.

### hci_usb firmware cannot do ISO (settled — don't revisit)

Zephyr's USB device_next BT HCI class
(`subsys/usb/device_next/class/bt_hci.c`) has **no ISO data path** — the
isochronous endpoints are descriptor stubs so Linux `btusb` binds (source
comment lines 85–90: "we do not implement isochronous endpoints
handling"). ISO TX (device→host) hits a `default:` case that drops the
packet **and leaks the net_buf**; ISO RX is never armed. The legacy USB BT
class (`subsys/usb/device/class/bluetooth.c`) has no isochronous
endpoints at all either. **No Zephyr USB BT transport can carry LE Audio
ISO in v3.3.0.** hci_uart is the only working transport. Patching hci_usb
for ISO would mean SDK surgery + nRF UDC EP8+ remap — declined.

### btattach not persistent

Runs as a background process from the session. Needs a udev rule /
systemd unit so it survives reboot and re-enumeration.

### LSP `gnu/stubs-32.h` not found warning

The C/C++ language server (clangd/editor) reports `gnu/stubs-32.h`
missing when parsing `src/*.c` and NCS headers — glibc on this system is
64-bit-only and the LSP falls back to the host sysroot instead of the
NCS toolchain's. It is **IDE noise only**; the firmware build
(`fw-build-*`) is unaffected (it uses the NCS toolchain's own sysroot).
Fix later by pointing the LSP/compiler-commands at the NCS toolchain
sysroot (e.g. clangd config with `--sysroot=` from `nix-nrf-dev`, or
generate `compile_commands.json` from the Zephyr build and let clangd
use it). Low priority — does not block builds or flashing.

## Reproduce

### 1. Build + flash the nRF5340DK central (hci_uart)

The dongle config lives in `dongle/hci_uart/{app,netcore}.conf`; the
build helpers build the upstream Zephyr hci_uart sample with those
fragments applied:

```bash
fw-build-dongle      # builds into build/dongle/
fw-flash-dongle      # flashes both cores via the DK's onboard J-Link
```

The netcore conf (`dongle/hci_uart/netcore.conf`) is the load-bearing
part: ISO central, 2 conns / 2 CISes, ext adv, no Coded PHY, no privacy.
See `dongle/README.md` for the full rationale.

### 2. Attach + set up hci0

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3     # NINO — required for JustWorks receiver
sudo btmgmt --index hci0 sc on        # receiver requires SC pairing
# verify: settings should include "powered le secure-conn cis-central"
```

If `btmgmt` reports no adapter, btattach isn't running or the DK
re-enumerated — re-run the btattach line. If the dongle's netcore has
zombie connection slots from a crashed run (`Connection Rejected 0x0d`),
reset with `fw-reset-dongle` then re-attach.

### 3. Stream

```bash
python3 scripts/bap_central.py --duration 15
```

Expected: `ACL link up` → `ServicesResolved` → 2× SelectProperties →
2× SetConfiguration → 2× Acquired → `Streaming 1000 Hz sine` →
`Done: 1500 frames in 15.00 s (100.0 fps)`.

Receiver console (serial-mcp on `/dev/ttyACM0`) during a good run:
`Pairing complete, bonded: 1`, 2× `ASE Config`, `LC3 decoder[0/1]`,
`Stream[x] started`, `audio_i2s: I2S DMA started` — then steady-state
streaming with ASRC correction active; no slab-full or underrun events
in steady state.

### 4. Verify ISO actually crossed HCI (optional)

```bash
setsid sudo btmon -i hci0 -w /tmp/btmon.btsnoop </dev/null >/tmp/btmon.log 2>&1 &
python3 scripts/bap_central.py --duration 15
sudo pkill -f "btmo[n] -i hci0 -w"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "ISO Data TX"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "Number of Completed Packets"
# expect: ~3000 ISO Data TX, ~3000+ Number of Completed Packets
```

## Why the BT transport works now (root causes fixed)

| # | Problem | Fix |
|---|---------|-----|
| 1 | hci_usb firmware had no ISO path at all — ISO packets died at the USB layer, no completions ever returned → 3-packet stall | Switched to `hci_uart` (app core is a plain H4 pipe; ISO passes as H4 type 0x05) |
| 2 | Dongle netcore had no ISO / ext-adv / coded-PHY tuning | Netcore conf with `BT_ISO_CENTRAL=y`, `BT_MAX_CONN=2`, `CONN_ISO_STREAMS=2`, `BT_EXT_ADV=y`, `BT_CTLR_PHY_CODED=n`, `BT_CTLR_PRIVACY=n` |
| 3 | Kernel LE connect uses accept-list filtered scan — broken on SDC (zero reports, even for legacy advertisers) | Raw-HCI direct `LE Extended Create Connection` via `scripts/hci_raw_connect.py`, wired into `bap_central.py` |
| 4 | BlueZ demanded MITM; receiver is JustWorks-only (`CONFIG_BT_SMP_ENFORCE_MITM=n`) | NINO agent + `btmgmt io-cap 3` (adapter-level IO cap must also be NINO — kernel uses it for auto-security SMP) |
| 5 | Pairing on a raw-HCI-created connection raced BlueZ ownership and rejected SMP confirmation | Current flow gives BlueZ ownership first, then calls asynchronous `Device.Pair()` before PACS/ASCS access. NINO agent accepts Just Works authorization. |
| 6 | Stale bond on PC vs wiped receiver keys → auth failure loop | Deleted `/var/lib/bluetooth/<adapter>/<receiver>/` bond dir, power-cycled hci0 (`btmgmt power off/on` flushes kernel key store — bluetoothd restart alone does not) |
| 7 | Receiver kernel panic on disconnect after stream (nrfx_i2s ASSERT on de-initialized instance) | `audio_sink_stop()` sends `TRIGGER_PREPARE` before `TRIGGER_DROP` (`src/audio_i2s.c`) |
| 8 | Zombie SDC connection slots on the dongle netcore after repeated raw-HCI connects without clean disconnect (`Connection Rejected 0x0d`) | `bap_central.py` cleanup now calls BlueZ `Device1.Disconnect()` (graceful HCI disconnect) then terminates the raw-HCI helper. Three consecutive runs with no DK reset. `fw-reset-dongle` helper for recovery. |

### Evidence for the hci_usb → hci_uart switch

The original hci_usb dongle carried a vanilla Zephyr hci_usb build
(SW-split LL). Connections hung ~90 s. Reflashing with the SDC netcore
tuning above fixed discovery but ISO data stalled after exactly 3
packets (SDC default ISO TX HCI buffer count): only 3 `ISO Data TX`
crossed HCI, no `Number of Completed Packets` ever returned, BlueZ's
userspace buffer filled (~445 writes ≈ 2.2 s) → EAGAIN. Root cause:
hci_usb has no ISO USB path (see "settled" above). Switching to hci_uart
made ISO flow as ordinary H4 type-0x05 frames — 3000 ISO TX / 3024
completions over 15 s.

### Evidence for the broken accept-list connect path

Linux 7.1 connects via accept-list + passive background scan. On this
SDC/hci_uart combo, **filtered scanning reports nothing** — verified
with raw HCI (bluetoothd stopped, btmon watching):
- unfiltered passive scan: 225 peer reports / 6 s (ext adv, 1M/2M)
- filter=accept-list, AR on: 1 report / 6 s
- filter=accept-list, AR off: 0 reports / 6 s
- same test against a legacy advertiser: also 0
- legacy scan interface (0x200B/0x200C): `Command Disallowed (0x0c)`

So BlueZ Pair/Connect hung; the raw-HCI direct-connect helper
(`scripts/hci_raw_connect.py`) issues `LE Extended Create Connection`
directly and holds the socket open (kernel reaps raw-socket connections
on close).

## Key implementation details (permanent reference)

| Component | File(s) | Note |
|-----------|---------|------|
| Audio pipeline | `audio_sink.h`, `audio_i2s.c`, `audio_decode.c` | Sink interface → I2S DMA (slab allocator), LC3 decode + channel routing |
| Clock recovery | `audio_drift.c`, `audio_drift.h` | PI controller: PCLK feedforward + phase term, ppm output |
| Actuators | `audio_clock_actuator_apll.c`, `audio_clock_actuator_none.c` | APLL (nRF5340) or NONE (nRF54L15, ASRC consumes ppm) |
| ASRC | `audio_asrc.{c,h}` | Fixed-point linear stereo ASRC (cpuapp + FLPR fallback) |
| Offload | `audio_offload.{c,h}`, `flpr_*.{c,h}`, `src/flpr/` | FLPR offload manager + firmware |
| Rate conversion | `audio_rate_convert.c` | Nearest-neighbor, 480→476/477 frames/block for PCLK32M mismatch |
| Timing (nRF54L15) | `audio_timing_nrf54.c` | TIMER20-vs-GRTC PCLK freq measurement, 1 s intervals |
| Central driver | `scripts/bap_central.py`, `scripts/hci_raw_connect.py` | Raw-HCI address bootstrap, BlueZ-owned connection, asynchronous `Device.Pair()`, NINO agent |
| Dongle firmware | `dongle/hci_uart/{app,netcore}.conf`, `scripts/bin/fw-build-dongle`, `scripts/bin/fw-flash-dongle` | nRF5340DK hci_uart central, ISO capable |

## Gotchas to remember

- **`pkill -f <pattern>` kills your own shell** when the pattern appears
  in the command line. Use a bracket: `pkill -f "btmo[n] -i hci0 -w"`,
  or `pkill -x btattach`.
- **UART0 on the nRF5340DK is on ttyACM2 (USB iface 02)**, not ttyACM1.
  ttyACM1 stays silent. Verified by sending HCI Reset manually:
  ttyACM2 replies `04 0e 04 01 03 0c 00`.
- **Raw HCI RX sockets are deaf on this kernel** — observe via
  `btmon -i hci0 -w <file>`, not by reading a raw socket.
- `hcitool lescan` fails with `I/O error` — legacy scan interface not
  supported by this controller build. Not a bug; use `btmon` or
  `bap_central.py` discovery.
- `bluetoothctl scan le` in the background exits instantly and stops
  discovery — useless for scan tests.
- `btmgmt io-cap 3` and `sc on` must be re-applied after adapter power
  loss / USB re-enumeration. Put them next to `btattach` in any
  persistent setup script.
- **Use serial-mcp** for the receiver console (`/dev/ttyACM0`), not
  `stty`/`cat` — serial-mcp holds the port exclusively and survives
  USB disconnects during reset.
- **Zephyr does NOT detect devicetree pinctrl overlaps** — two
  peripherals claiming the same pin produce no compile error. Verify pin
  assignments against all enabled peripherals by decoding the resolved
  `zephyr.dts` (psel encoding: `NRF_PSEL(fun, port, pin)` =
  `(fun << 24) | ((port*32+pin) & 0x1ff)`). AGENTS.md documents a past
  P1.10/P1.11/P1.12 conflict found this way.
