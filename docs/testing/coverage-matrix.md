# Honest coverage matrix — pre-refactor baseline

Version: T0, 2026-07-31.  This matrix maps every production source file to the
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
| `audio_clock_actuator_apll.c` | nRF5340 APLL steering | — | — | — | Phase 4c: 600 s stream, APLL active, zero fault | No direct unit test. Ppm→register conversion, clamp, and reset not tested in isolation. | T5 |
| `audio_clock_actuator_none.c` | nRF54L15 no-op actuator | — | — | — | nRF54L15 Mode A/B with ASRC consuming ppm | No direct unit test. No-op and reset behavior not tested in isolation. | T5 |
| `audio_clock_actuator_sample_adjust.c` | **Historical/retired** | `tests/unit/actuator/` — compiles this file | — | — | — | Tests retired implementation; does not protect production. Retain under historical label. | T5 |
| `audio_decode.c` | LC3 decode + channel routing | `tests/unit/decode/` — basic exists; mostly "does not crash" | BSim: compiled and exercised in sink-only scenario | — | nRF54L15 hardware streaming | No golden-output PCM hashes for 7.5/10 ms, no fixture-based assertions, no LC3 codec error/rejection tests. | T2, T4 |
| `audio_drift.c` | PI clock recovery controller | `tests/unit/drift/` — 18 tests | BSim: compiled only — `audio_drift_controller_update()`/`reset()` call sites live in `audio_i2s.c`, excluded from the BSim build; no BSim execution | Configurable clamps verified through Kconfig | nRF54L15 closed-loop streaming | Long-run boundedness not tested; overflow resistance not exhaustively covered. | T5 |
| `audio_i2s.c` | I2S DMA + slab + underrun recovery | — | — | — | nRF54L15 hardware streaming | **No direct unit test.** Slab ownership, startup pre-fill count, PREPARE recovery order, stop sequence, offload fallback, and all error paths untested in isolation. Largest single gap. | T3 |
| `audio_offload.c` | FLPR offload manager | `tests/unit/audio_offload/` — compiles production source | BSim: compiled without CONFIG_SOC_NRF54L15 — only no-op/non-nRF stubs are exercised; not FLPR production-path integration | — | nRF54L15 FLPR healthy + fallback | No known functional gap; branch coverage unmeasured. | T7 |
| `audio_perf.c` | Performance timers | `tests/unit/perf/` — compiles production `src/audio_perf.c` | — | — | — | Basic tests exist; branch coverage unmeasured. | T7 |
| `audio_rate_convert.c` | Nearest-neighbor rate converter | `tests/unit/rate_convert/` — Twister suite | BSim: compiled only — rate-converter calls live in `audio_i2s.c`, excluded from the BSim build; no BSim execution | — | nRF54L15 streaming verification | Functional tests exist; branch coverage unmeasured. | T7 |
| `audio_shell.c` | Status shell commands | — | — | — | Log inspection during hardware gates | **No direct unit test.** Parseable status fields, zero-safe percentages, FLPR fields, unpair propagation not tested in isolation. | T6 |
| `audio_stats.c` | Stream statistics counters | — | BSim: compiled and genuinely exercised — production `audio_stats.c` runs through `audio_decode.c`/`bt_bap.c`, and the sink stub reads `audio_stats_get()` snapshots; local counters only supplement startup accounting | — | Hardware log verification | **No direct unit test.** Counter increments, PLC/total coupling, reset, concurrent access not tested in isolation. | T2 |
| `audio_timing_math.c` | Timing math shared across platforms | `tests/unit/timing/` — Twister suite | BSim: compiled only — consumed by `audio_timing_nrf54.c`, which is not compiled into BSim; no BSim execution | — | — | Functional tests exist; branch coverage unmeasured. | T7 |
| `audio_timing_none.c` | nRF5340 no-op timing | — | BSim: compiled; `audio_timing_sdu_ref_update()` no-op is called from the `bt_bap.c` stream path (init/reset live in `audio_i2s.c`, not called in BSim) | — | nRF5340 hardware streaming | No direct unit test; no-op implementation is low-risk. BSim integration evidence exists. | T5 |
| `audio_timing_nrf54.c` | nRF54L15 GRTC+PCLK timing | — | — | — | nRF54L15 PCLK diagnostics + feedforward | **No direct unit test.** GRTC compare, GPPI, TIMER20 capture, allocation cleanup, schedule failure, stale generation rejection not tested in isolation. | T5 |
| `audio_volume.c` | Volume control | — | BSim: compiled and exercised | — | Hardware streaming at default volume | **No direct unit test.** Mute, zero, unity, signed extremes, callback error not tested in isolation. | T2 |
| `bt_bap.c` | BAP unicast server, ASCS, PACS, pairing | — | BSim: compiled and exercised with one mono ASE + fake sink | — | Hardware connection + streaming | No direct unit test. BSim covers mono only; Mode A/B, reconnect, invalid config, malformed ASE, source-rejection, and teardown permutations not covered. | T4 |
| `flpr_audio_process.c` | FLPR audio block wrapper | `tests/unit/flpr_audio_process/` — compiles production source | — | — | nRF54L15 FLPR streaming | No known functional gap; branch coverage unmeasured. | T7 |
| `flpr_cache.c` | FLPR cache operations | Compiled by `flpr_ring` and `flpr_audio_process` exec-only suites (production source) | — | — | nRF54L15 FLPR activation | Exec-only suites run on native_sim; compiling real source there does not prove real hardware cache/barrier semantics — hardware-only behavior must be covered or classified in T1. Branch coverage unmeasured. | T1, T7 |
| `flpr_handshake.c` | FLPR boot handshake + VEVIF IPC | `tests/unit/flpr_handshake/` — Twister suite | — | — | nRF54L15 FLPR handshake | Suite tests **protocol helpers from headers only**; does NOT compile `src/flpr_handshake.c`. Bind/unbind, ACK send failures, callback lock, malformed messages, disconnect/reconnect, semaphore draining not tested against production source. | T1 |
| `flpr_ring.c` | SPSC ring buffer (shared SRAM) | `tests/unit/flpr_ring/` — compiles production source | — | — | nRF54L15 FLPR ring through I/O | No known functional gap; branch coverage unmeasured. | T7 |
| `flpr_ring_mgr.c` | Ring manager: paired input/output rings | `tests/unit/flpr_ring_mgr/` — Twister suite | — | — | nRF54L15 FLPR ring manager through I/O | Suite tests **copied model** with replicated reset/notification logic; does NOT compile `src/flpr_ring_mgr.c`. Coordinated reset, invalidation races, semaphore draining, producer/consumer validation, backpressure, sequence wrap, remote restart not tested against production source. | T1 |
| `flpr_runtime.c` | Synchronous FLPR VPR runtime restart manager | `tests/unit/flpr_runtime/` — Twister suite | — | — | nRF54L15 FLPR runtime restart + fault handling | Compiles `src/flpr_runtime.c` against **non-nRF54L stub** mocks. Production nRF54 path is preprocessor-excluded; mock register operations in test file are independent of real `flpr_runtime_restart()`. DMCONTROL transitions, fault stages, CRC rejection not tested with real VPR register access. | T1 |
| `main.c` | Boot, init wiring, watchdog, advertising loop | — | — | — | Boot logs on both targets | **No direct unit test.** Init order, fatal-reboot path, advertising restart loop not tested in isolation. | T6 |
| `stream_lifecycle.c` | Stream start/stop lifecycle | `tests/unit/lifecycle/` — Twister suite | BSim: compiled and exercised | — | Hardware connect/disconnect cycles | Functional tests exist; duplicate-start edge semantics not exhaustively covered. | T5 |
| `src/flpr/main.c` | FLPR firmware entry point (RISC-V VPR) | — | — | — | nRF54L15 FLPR firmware loaded + active | **No direct unit test.** No RISC-V simulator test infrastructure exists. Hardware-only by design; no practical simulation path. | T1 (classify as hardware-only, no plan to unit-test) |

## Current suite inventory

| Category | Count | Suites |
|----------|-------|--------|
| Twister C (testcase.yaml) | 12 | actuator, asrc, decode, drift, flpr_handshake, flpr_protocol, flpr_ring_mgr, flpr_runtime, lifecycle, perf, rate_convert, timing |
| Exec-only C (CMakeLists.txt, no testcase.yaml) | 4 | audio_offload, flpr_audio_process, flpr_ring, offload_asrc |
| Python | 4 | gate (test_gate.py), flpr_stall_gate (test_flpr_stall_gate.py), bluez_wp_gate (test_bluez_wireplumber_gate.py), bluez_wp_phase3_gate (test_bluez_wireplumber_phase3_gate.py) |
| BabbleSim | 1 | bsim_stage1 (sink-only mono scenario) |
| **Total gate children** | **21** | |

## Explicit weak-test facts

1. **Actuator suite** compiles retired `audio_clock_actuator_sample_adjust.c`,
   not either production actuator (APLL or NONE).
2. **FLPR runtime suite** compiles `src/flpr_runtime.c` against non-nRF54L
   stubs. Production nRF54 path is preprocessor-excluded; mock register
   operations in the test file are independent of real
   `flpr_runtime_restart()`. DMCONTROL transitions, CRC rejection, and fault
   stages are not exercised through real VPR hardware access.
3. **FLPR ring-manager suite** tests a separate copied model with replicated
   reset and notification logic; does not compile `src/flpr_ring_mgr.c`.
4. **FLPR handshake suite** tests header-only protocol helpers; does not
   compile `src/flpr_handshake.c`.
5. **BabbleSim** compiles production `bt_bap.c`, `audio_decode.c`,
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
6. **Build contracts** (resolved `.config` and `zephyr.dts` for both targets)
   are not automatically asserted.  Host/controller ISO buffer agreement,
   ASRC/FLPR path selection, pin assignments, and SW Split overlay application
   are only checked manually.
7. **Hardware evidence** comes from logs and autonomous central automated
   streams.  These verify end-to-end data flow but do not replace direct
   branch/error-path unit tests.

No numeric code coverage (line/branch) exists yet.  No honest report can be
made without instrumentation (`gcovr`) and a stable production-file-to-test
manifest.  Both belong to Phase T7.
