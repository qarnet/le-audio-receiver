# Honest coverage matrix — pre-refactor baseline

Version: T6, 2026-08-02.  This matrix maps every production source file to the
current test coverage that exercises it.  A test that duplicates production
logic, compiles a stub, or checks copied constants is NOT counted as proof of
production behavior.

## Evidence classification

| Label | Meaning |
|-------|---------|
| **direct** | Compiles and links real production source; executes production branches, state transitions, and error paths. |
| **integration** | Multi-component test that exercises the production source through a real or simulated subsystem (e.g., BabbleSim). |
| **build** | Build contract asserted at compile time or through resolved `.config`/`zephyr.dts` parsing. |
| **hardware** | Exercised on physical hardware with log/register evidence; not simulated. |
| **model** | Tests a separate implementation that replicates production logic (copied algorithm, mirrored constants). Does NOT exercise production source. |
| **structural** | Tests that validate enum values, constant definitions, or header structure without executing production code paths. |
| **stub** | Compiles production source against injected stubs; structural or interface-level coverage only. |
| **historical** | Tests a retired or non-production implementation retained for regression comparison only. |

## Production source coverage

| Source | Responsibility | Direct unit proof | Integration / BSim | Build contract | Hardware proof | Current gap | Closing phase |
|--------|----------------|--------------------|----------------------|----------------|----------------|-------------|---------------|
| `audio_asrc.c` | Fixed-point linear stereo ASRC | `tests/unit/asrc/` — 20 tests compile production source | — | — | nRF54L15 Mode A/B 600 s + 120 s | Full branch/error-path coverage not measured | T2, T7 |
| `audio_clock_actuator_apll.c` | nRF5340 APLL steering | `tests/unit/actuator_apll/` — 8 tests compile production source against include-shadow HALs + register-write mock: init/reset center, exact ±33 ppm conversion, ±1..±3 truncation, exact MIN/MAX, beyond-rail and INT32_MIN/MAX clamps, repeated calls, consume==0; `tests/unit/actuator_apll_nohfclk/` compiles the same file with `NRF_CLOCK_HAS_HFCLKAUDIO=0` proving all no-op returns with zero writes (UBSan-clean focused run) | — | — | Phase 4c: 600 s stream, APLL active, zero fault | Conversion, clamp, and reset now direct production-source proof. | T5 (closed) |
| `audio_clock_actuator_none.c` | nRF54L15 no-op actuator | `tests/unit/actuator_none/` — compiles production source: init/reset/consume and apply of zero, both signs, and INT32_MIN/MAX all return zero | — | — | nRF54L15 Mode A/B with ASRC consuming ppm | No-op behavior now direct production-source proof. | T5 (closed) |
| `audio_clock_actuator_sample_adjust.c` | **Historical/retired** | `tests/unit/actuator_sample_adjust_historical/` — compiles this file; testcase ID/tags/comments labeled historical and retired | — | — | — | Tests retired implementation; does not protect production. Retained under the historical label. | T5 (closed) |
| `audio_decode.c` | LC3 decode + channel routing | `tests/unit/decode/` — 37 tests execute production source against deterministic checked-in 48 kHz LC3 fixtures: byte-exact golden PCM, full/per-channel CRC-32, config rejection (incl. liblc3-untouched), SDU rejection with guard preservation, PLC accounting, overlap-safe mono expansion, Mode B dual-accounting, hard-failure accounting via linker wrap | BSim T4 matrix: exercised through the full mono/Mode A/Mode B routing matrix with exact per-push decoder-invocation accounting and pinned PCM hashes | — | nRF54L15 hardware streaming | No golden PCM for alternate rates/codecs (unsupported by design). | T7 |
| `audio_drift.c` | PI clock recovery controller | `tests/unit/drift/` — 29 tests compile production source: INT32_MIN/MAX frequency updates, INT_MIN/MAX slab counts, cross-extreme EMA steps, exact rail boundaries, 100k-update long runs at setpoint and both phase extremes, symmetric feedforward-rail phase unwind, real-thread concurrent update/frequency/reset loops with deterministic final reset; focused run UBSan-clean | BSim: compiled only — `audio_drift_controller_update()`/`reset()` call sites live in `audio_i2s.c`, excluded from the BSim build; no BSim execution | Configurable clamps verified through Kconfig | nRF54L15 closed-loop streaming | Long-run boundedness and full-range arithmetic now direct production-source proof. | T5 (closed) |
| `audio_i2s.c` | I2S DMA + slab + underrun recovery | `tests/unit/audio_i2s/` (ASRC/NONE/offload) + `tests/unit/audio_i2s_identity/` (identity/APLL) — 98 tests execute real production source against a fake I2S driver with nrfx-style block ownership + mocked timing/drift/actuator/rate-converter/ASRC/offload/stats/perf: exact config, all init failures with retry, idempotent re-init preserving active queue/state, input-frame setter bound, push validation with zero side effects, transactional startup (six silence + data + START; every alloc/write/START failure cleanup), distinct-ownership proof, drift/actuator once-per-block, repeat-fallback ownership, `-EIO` PREPARE recovery, ASRC pre-state export/offload fallback for every fault class/invalid frame range/import rejection, offload sequence accounting, stop order/idempotence/state reset | — | — | nRF54L15 + nRF5340 hardware streaming | Physical nrfx DMA timing and I2S electrical behavior remain hardware-only (fake-driver release is explicit test control, not DMA interrupts). Offload/ASRC algorithm math covered by `tests/unit/asrc/` + `tests/unit/offload_asrc/`. | T3, T8 |
| `audio_offload.c` | FLPR offload manager | `tests/unit/audio_offload/` — compiles production source | BSim: compiled without CONFIG_SOC_NRF54L15 — only no-op/non-nRF stubs are exercised; not FLPR production-path integration | — | nRF54L15 FLPR healthy + fallback | No known functional gap; branch coverage unmeasured. | T7 |
| `audio_perf.c` | Performance timers | `tests/unit/perf/` — compiles production `src/audio_perf.c` | — | — | — | Basic tests exist; branch coverage unmeasured. | T7 |
| `audio_rate_convert.c` | Nearest-neighbor rate converter | `tests/unit/rate_convert/` — Twister suite | BSim: compiled only — rate-converter calls live in `audio_i2s.c`, excluded from the BSim build; no BSim execution | — | nRF54L15 streaming verification | Functional tests exist; branch coverage unmeasured. | T7 |
| `app_lifecycle.c` | Narrow boot coordinator (ordered fatal init + advertising restart) | `tests/unit/app_lifecycle/` — 13 tests compile production source: exact all-success order incl. optional platform step, platform absent, each of the seven fatal steps failing independently (no later callback, exactly one cold reboot, original errno), restart success (only advertising) and restart failure (one reboot, error), NULL/missing-required-callback `-EINVAL` with zero calls/reboots | — | — | Boot logs on both targets | Init order, reboot-once semantics, restart behavior now direct production-source proof; `main.c` wiring stays proven by production builds. | T6 (closed) |
| `audio_shell.c` | Status shell commands | `tests/unit/audio_shell/` — 13 tests compile production `src/audio_shell.c` with `AUDIO_SHELL_TEST` seams, executed through the real Zephyr dummy backend + `shell_execute_cmd` against mocked stats/drift/volume/sink/unpair and real `audio_perf.c` (deterministic cycle injection): exact `audio status` field order/labels, zero-frames `(0%)` without div0, large-value percentage without uint32 overflow, `audio perf` path labels/queue fields/integer one-decimal deadline %, zero-count averages, reset-stats/perf-reset/stop exactly-once + stable text, `bt unpair` success text and exact negative errno propagation, wrapper-seam equivalence; `tests/unit/audio_shell_noperf/` — 10 tests, same production file with `CONFIG_AUDIO_PERF_MEASUREMENT=n`: truthful unavailable (zero) deadline percentage, zeroed queue fields, no div0; `tests/unit/audio_shell_nrf54/` — 16 tests compile the same file with `CONFIG_SOC_NRF54L15` for the TU against mocked FLPR APIs: `flpr status` ready/ACKed/healthy/epoch/errors/TX/RX/loss/order, `flpr ring status` counters/diagnostics/test/latency/stall incl. conditional lines, `flpr offload` state/epoch/generation/counters/faults/recovery/probation/runtime-restart/heartbeat-dedup/RTT/last-error + ASRC counters/faults/RTT/cycles (gate-parsed fields), `flpr runtime` full field set with out-of-range enums printing UNKNOWN/unknown, `flpr restart` EBUSY/success-line/failure-errno | — | — | Log inspection during hardware gates | Parseable status fields, zero-safe percentages, FLPR gate fields, unpair propagation now direct production-source proof. | T6 (closed) |
| `audio_stats.c` | Stream statistics counters | `tests/unit/stats/` — 10 tests execute production source: exact counter coupling (total = decoded + PLC), reset, by-value snapshots, deterministic repeats, 4-thread concurrent exact counts | BSim: compiled and genuinely exercised — production `audio_stats.c` runs through `audio_decode.c`/`bt_bap.c`, and the sink stub reads `audio_stats_get()` snapshots; local counters only supplement startup accounting | — | Hardware log verification | No known functional gap; branch coverage unmeasured. | T7 |
| `audio_timing_math.c` | Timing math shared across platforms | `tests/unit/timing/` — Twister suite; also compiled by `tests/unit/timing_nrf54/` (production ppm path) | BSim: compiled only — consumed by `audio_timing_nrf54.c`, which is not compiled into BSim; no BSim execution | — | — | Functional tests exist; branch coverage unmeasured. | T7 |
| `audio_timing_none.c` | nRF5340 no-op timing | — | BSim: compiled; `audio_timing_sdu_ref_update()` no-op is called from the `bt_bap.c` stream path (init/reset live in `audio_i2s.c`, not called in BSim) | — | nRF5340 hardware streaming | No direct unit test; no-op implementation is low-risk. BSim integration evidence exists. Gap not in T5 scope. | — (low-risk no-op) |
| `audio_timing_nrf54.c` | nRF54L15 GRTC+PCLK timing | `tests/unit/timing_nrf54/` — 18 tests compile production `audio_timing_nrf54.c` + `audio_timing_math.c` against include-shadow mocks of nrfx_grtc/nrfx_gppi/nrf_grtc/nrf_timer and a drift-feedforward mock: alloc-failure exact errors, GPPI-failure cleanup (cc_disable then free, no GPPI free), full init sequence + idempotence, pre-init/zero-ts no-ops, one anchor per session, past/future/wrap first compares, baseline + exact-ppm callbacks incl. TIMER32 wrap, late reschedule, reschedule failure, reset semantics, stale-generation rejection, per-measurement drift delivery, plus the review-fix FIFO contract: 10-payload backlog drained in order from one work invocation, stale-then-fresh mixed generations in one FIFO, and full-FIFO overflow (observable fault, accepted entries drained in order, later callbacks inactive) | — | — | nRF54L15 PCLK diagnostics + feedforward | GRTC compare, GPPI, TIMER20 capture, allocation cleanup, schedule failure, stale generation rejection, and FIFO/backlog/overflow behavior now direct production-source proof (narrow `AUDIO_TIMING_NRF54_TEST` seams only). | T5 (closed) |
| `audio_volume.c` | Volume control | `tests/unit/volume/` — 12 tests execute production VCP branch against a shadow of the exact NCS v3.3.0 renderer types + fake `bt_vcp_vol_rend_register()` (real `audio_perf.c` compiled for hook balance): registration fields/defaults, failure propagation, callback packing, mute/zero/unity/intermediate scaling incl. signed extremes, zero/null samples, perf-hook balance on all exits, concurrent callback toggling atomic snapshot | BSim: compiled and exercised | — | Hardware streaming at default volume | No known functional gap; branch coverage unmeasured. | T7 |
| `bt_bap.c` | BAP unicast server, ASCS, PACS, pairing | — | BSim T4 matrix: 15 scenarios over real ASCS/PACS/ISO — mono 7.5/10 ms, Mode A (incl. reverse start), Mode B, malformed-SDU rejection + resume, first-ASE stop, release-without-disable, disconnect-while-streaming, reconnect (second segment equals a fresh mono oracle), source-direction rejection (exact CONF_UNSUPPORTED/NONE), NO_MEM on third sink (resource seam), nine invalid-codec-field rejections (exact CONF_REJECTED/CODEC_DATA) + two successes (valid mono + missing-frame-blocks fallback); strict oracle with FNV-corrected full/L/R hashes, source-valid startup boundary with zero post-start PLC, per-push source validity from production flow, release sink-stop ordering proof, missing-TS validation; all pinned and pairwise deterministic | — | Hardware connection + streaming | No direct unit test; BSim scenarios cover the matrix (T4).  Alternate rates/codecs remain unsupported by design. | T7 |
| `flpr_audio_process.c` | FLPR audio block wrapper | `tests/unit/flpr_audio_process/` — compiles production source | — | — | nRF54L15 FLPR streaming | No known functional gap; branch coverage unmeasured. | T7 |
| `flpr_cache.c` | FLPR cache operations | Compiled by `flpr_ring`, `flpr_audio_process`, and `flpr_ring_mgr` (T1) suites (production source) | — | — | nRF54L15 FLPR activation | native_sim coverage is API/barrier-call proof only — the native branch uses `atomic_thread_fence`; physical cache/barrier semantics remain hardware-only. Branch coverage unmeasured. | T7 |
| `flpr_handshake.c` | FLPR boot handshake + VEVIF IPC | `tests/unit/flpr_handshake/` — 43 tests compile and execute `src/flpr_handshake.c` against a fake IPC service backend (real `ipc_service_*` APIs, production callbacks) | — | — | nRF54L15 FLPR handshake | Bind/unbind, READY/duplicate/changed epoch, ACK send failures, heartbeat health transitions, ring dispatch, disconnect/reconnect, stress, fault-hang now direct production-source proof. Branch coverage unmeasured. | T7 |
| `flpr_ring.c` | SPSC ring buffer (shared SRAM) | `tests/unit/flpr_ring/` — compiles production source | — | — | nRF54L15 FLPR ring through I/O | No known functional gap; branch coverage unmeasured. | T7 |
| `flpr_ring_mgr.c` | Ring manager: paired input/output rings | `tests/unit/flpr_ring_mgr/` — 53 tests compile and execute `src/flpr_ring_mgr.c` (+ real `flpr_ring.c`, `flpr_cache.c`) with host ring arrays and a handshake mock | — | — | nRF54L15 FLPR ring manager through I/O | Coordinated reset, invalidation races, semaphore draining, producer/consumer validation, backpressure, sequence wrap, remote restart now direct production-source proof. Branch coverage unmeasured. | T7 |
| `flpr_runtime.c` | Synchronous FLPR VPR runtime restart manager | `tests/unit/flpr_runtime/` — 21 tests compile and execute the real nRF54 restart body (shadow VPR HAL, host source/exec arrays, ordered event log) | — | — | nRF54L15 FLPR runtime restart + fault handling | DMCONTROL transitions, fault stages, CRC rejection, mutex busy, duration accounting now direct production-source proof. Physical cache/FLPR entry behavior remains hardware-only. Branch coverage unmeasured. | T7 |
| `main.c` | Boot wiring, watchdog device/thread, advertising loop | Boot ordering/reboot semantics delegated to `app_lifecycle.c` (direct suite above); `main.c` itself is not compiled into any unit suite — hardware wiring remains proven by the production builds | — | — | Boot logs on both targets | Fatal init order and reboot behavior now direct production-source proof via the coordinator; `main.c` remains adapter-only glue. | T6 (closed) |
| `stream_lifecycle.c` | Stream start/stop lifecycle | `tests/unit/lifecycle/` — 22 tests compile production source: Mode A/B/mono gates, close idempotence, closed-to-open edge semantics (duplicate starts return false), configure/start/close/reconfigure/start permutations, release-then-slot-reuse, reset from closed/partial/open states, repeated open/close cycles, inert invalid/zero/negative configurations | BSim T4 matrix: gate open/close across mono, Mode A (two-ASE set), first-ASE stop, release-without-disable, disconnect-while-streaming, and reconnect; closed-gate receive evidence | — | Hardware connect/disconnect cycles | Duplicate-start edge semantics now direct production-source proof. | T5 (closed) |
| `src/flpr/main.c` | FLPR firmware entry point (RISC-V VPR) | — | — | — | nRF54L15 FLPR firmware loaded + active | **No direct unit test.** No RISC-V simulator test infrastructure exists. Hardware-only by design; no practical simulation path. | — (hardware-only) |

## Current suite inventory

| Category | Count | Suites |
|----------|-------|--------|
| Twister C (testcase.yaml) | 24 | actuator_apll, actuator_apll_nohfclk, actuator_none, actuator_sample_adjust_historical, app_lifecycle, asrc, audio_i2s, audio_i2s_identity, audio_shell, audio_shell_noperf, audio_shell_nrf54, decode, drift, flpr_handshake, flpr_protocol, flpr_ring_mgr, flpr_runtime, lifecycle, perf, rate_convert, stats, timing, timing_nrf54, volume |
| Exec-only C (CMakeLists.txt, no testcase.yaml) | 4 | audio_offload, flpr_audio_process, flpr_ring, offload_asrc |
| Python | 7 | gate (test_gate.py), flpr_stall_gate (test_flpr_stall_gate.py), flpr_hang_gate (test_flpr_hang_gate.py, 10 parser tests), bluez_wp_gate (test_bluez_wireplumber_gate.py), bluez_wp_phase3_gate (test_bluez_wireplumber_phase3_gate.py), bsim_runner (test_bsim_stage1_parse.py, 36 tests), build_contract (test_build_contract.py, 30 tests) |
| BabbleSim | 1 | bsim_stage1 (T4 15-scenario BAP matrix, scenarios 1-8 twice) |
| **Total gate children** | **36** | |

## Explicit weak-test facts

1. **Historical actuator suite** (`actuator_sample_adjust_historical`)
   compiles the retired `audio_clock_actuator_sample_adjust.c`, not either
   production actuator (APLL or NONE); the production actuators have their
   own direct suites (`actuator_apll`, `actuator_apll_nohfclk`,
   `actuator_none`).
2. **FLPR runtime, ring-manager, and handshake suites** became direct
   production-source proof in T1 (see rows above); their former stub/copied/
   header-only tests were removed or replaced.
3. **flpr_cache.c native coverage is API/barrier-call proof only** — the
   native branch uses `atomic_thread_fence`; physical cache/barrier semantics
   are hardware-only evidence.
4. **Decode suite failure-injection seam**: NCS v3.3.0 liblc3 1.1.2 returns
   a hard negative only for parameter errors (pre-validated by production),
   so the hard-error accounting tests inject `-1` via a test-only linker
   wrap of `lc3_decode()` that delegates to the real implementation for all
   other calls.  Malformed valid-length data is asserted to be concealed
   (PLC), which is the real installed liblc3 semantic.
5. **Volume suite VCP branch** is enabled with test-only compile definitions
   (`CONFIG_BT_VCP_VOL_REND=1`, `CONFIG_BT_AUDIO_VOL_DEFAULT=195`) against a
   test-local shadow of the exact NCS v3.3.0 renderer types; the production
   firmware uses the real VCP stack and its own Kconfig.
6. **BabbleSim** compiles production `bt_bap.c`, `audio_decode.c`,
   `audio_stats.c`, `audio_drift.c`, `audio_rate_convert.c`,
   `audio_timing_math.c`, `audio_timing_none.c`, `stream_lifecycle.c`,
   `audio_volume.c`, and `audio_offload.c`, but exercises one mono ASE with a
   fake sink stub and no I2S or FLPR stack.  Compilation alone is not
   execution evidence: `audio_drift.c`, `audio_rate_convert.c`, and
   `audio_timing_math.c` are compiled but never called (their call sites
   live in `audio_i2s.c` and `audio_timing_nrf54.c`, which are excluded
   from the BSim build).  `audio_offload.c` runs only its non-nRF54 stub
   branch (no CONFIG_SOC_NRF54L15).  `audio_asrc.c` and `audio_i2s.c` are
   not compiled into BSim at all.  Mode A, Mode B, reconnect, packet-loss,
   and malformed-configuration scenarios are not covered.
7. **Build contracts are now automatically asserted** by
   `scripts/check-build-contract.py` (T6): resolved `.config` and
   `zephyr.dts` for both targets (app, nRF5340 `hci_ipc` controller, and
   nRF54L15 `flpr` images) are parsed and checked for the resampler/
   actuator path selection, host/controller ISO buffer agreement, pin
   assignments, RF-switch polarity, crystal capacitance, exact
   non-overlapping FLPR/ring memory ranges, and both SW Split overlays.
   The checker's own suite (`tests/unit/build_contract/`, 30 tests) uses
   minimal temporary fixtures and never depends on pre-existing firmware
   build directories; the real contract run happens after pristine
   production builds.
8. **Hardware evidence** comes from logs and autonomous central automated
   streams.  These verify end-to-end data flow but do not replace direct
   branch/error-path unit tests.

No numeric code coverage (line/branch) exists yet.  No honest report can be
made without instrumentation (`gcovr`) and a stable production-file-to-test
manifest.  Both belong to Phase T7.
