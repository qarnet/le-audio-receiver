# Honest coverage matrix — pre-refactor baseline

Version: T8, 2026-08-04.  This matrix maps every production source file to the
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
| `audio_clock_actuator_apll.c` | nRF5340 APLL steering | `tests/unit/actuator_apll/` — 8 tests compile production source against include-shadow HALs + register-write mock: init/reset center, exact ±33 ppm conversion, ±1..±3 truncation, exact MIN/MAX, beyond-rail and INT32_MIN/MAX clamps, repeated calls; `tests/unit/actuator_apll_nohfclk/` compiles the same file with `NRF_CLOCK_HAS_HFCLKAUDIO=0` proving all no-op returns with zero writes (UBSan-clean focused run) | — | — | Phase 4c: 600 s stream, APLL active, zero fault | Conversion, clamp, and reset now direct production-source proof. | T5 (closed) |
| `audio_clock_actuator_none.c` | nRF54L15 no-op actuator | `tests/unit/actuator_none/` — compiles production source: init/reset and apply of zero, both signs, and INT32_MIN/MAX all return zero | — | — | nRF54L15 Mode A/B with ASRC consuming ppm | No-op behavior now direct production-source proof. | T5 (closed) |
| `audio_clock_actuator_sample_adjust_historical.c` | **Historical/retired** | `tests/unit/actuator_sample_adjust_historical/` — compiles the test-local copy of the retired actuator (moved out of `src/` in R2); testcase ID/tags/comments labeled historical and retired | — | — | — | Tests retired implementation; does not protect production. Retained under the historical label. | T5 (closed) |
| `audio_decode.c` | LC3 decode + channel routing | `tests/unit/decode/` — 37 tests execute production source against deterministic checked-in 48 kHz LC3 fixtures: byte-exact golden PCM, full/per-channel CRC-32, config rejection (incl. liblc3-untouched), SDU rejection with guard preservation, PLC accounting, overlap-safe mono expansion, Mode B dual-accounting, hard-failure accounting via linker wrap | BSim T4 matrix: exercised through the full mono/Mode A/Mode B routing matrix with exact per-push decoder-invocation accounting and pinned PCM hashes | — | nRF54L15 hardware streaming | No golden PCM for alternate rates/codecs (unsupported by design). | T7 |
| `audio_drift.c` | PI clock recovery controller | `tests/unit/drift/` — 29 tests compile production source: INT32_MIN/MAX frequency updates, INT_MIN/MAX slab counts, cross-extreme EMA steps, exact rail boundaries, 100k-update long runs at setpoint and both phase extremes, symmetric feedforward-rail phase unwind, real-thread concurrent update/frequency/reset loops with deterministic final reset; focused run UBSan-clean | BSim: compiled only — `audio_drift_controller_update()`/`reset()` call sites live in `audio_i2s.c`, excluded from the BSim build; no BSim execution | Configurable clamps verified through Kconfig | nRF54L15 closed-loop streaming | Long-run boundedness and full-range arithmetic now direct production-source proof. | T5 (closed) |
| `audio_i2s.c` | I2S DMA + slab + underrun recovery | `tests/unit/audio_i2s/` (ASRC/NONE/offload) + `tests/unit/audio_i2s_identity/` (identity/APLL) — 60 / 58 tests execute real production source against a fake I2S driver with nrfx-style block ownership + mocked timing/drift/actuator/rate-converter/ASRC/offload/stats/perf: exact config, all init failures with retry, idempotent re-init preserving active queue/state, input-frame setter bound, push validation with zero side effects, transactional startup (six silence + data + START; every alloc/write/START failure cleanup), distinct-ownership proof, drift/actuator once-per-block, repeat-fallback ownership, `-EIO` PREPARE recovery, ASRC pre-state export/offload fallback for every fault class/invalid frame range/import rejection, offload sequence accounting, stop order/idempotence/state reset; R1 admission/drain concurrency (shared `test_sink_concurrent.c` in both suites): stop drains an admitted push before finalizing, two overlapping stops finalize once (one software reset, one PREPARE/DROP pair), closed rejection (`-EBUSY`) and reconnect, failure exits release admission, open waits for the full stop cohort, close is nonblocking, sleeping-non-owner stop wakeup (owner broadcast reaches a joiner blocked in the finalization condvar wait, proven via a lock-protected test-only waiter counter), and an admitted push's single input-frames snapshot (setter runs mid-push; every block keeps the 480-frame shape and the next 360-frame push uses the new setting while a 480-sample count is rejected) — all via the fake-driver write gate (entered/release semaphores, no timing-sleep synchronization) | — | — | nRF54L15 + nRF5340 hardware streaming | Physical nrfx DMA timing and I2S electrical behavior remain hardware-only (fake-driver release is explicit test control, not DMA interrupts). Offload/ASRC algorithm math covered by `tests/unit/asrc/` + `tests/unit/offload_asrc/`. | T3, T8 |
| `audio_iso_seq.c` | Pure per-CIS omitted-callback sequence tracker | `tests/unit/iso_seq/` — 18 tests compile production source: first/contiguous/single/multi gap, 16-bit wrap, duplicate/backward/over-bound explicit resync, boundary gap, lost-callback advance, Mode B omitted-callback exact-PLC pushes and multi-omitted cadence, Mode A simultaneous/one-sided omission, no-synthesis-on-resync, reset | — | — | Clean-session hardware silent (tracker silent when delta == 1); gap activation NOT observable on hardware — documented evidence limitation | Hardware gap activation non-deterministic; direct suite is the primary proof | T8 (closed) |
| `audio_modea.c` | Bounded two-CIS event assembler and per-channel PLC | `tests/unit/modea/` — compiles production source: equal-TS pairing emits once (order independent), missing-half resolution (right/left lost callback, newer-evidence discard), consecutive/alternating losses, TS wrap, out-of-order cross-channel, no-TS startup sentinel resolution, reset, queue overflow drops oldest, oversized rejection without mutation, hard-decode-error caller contract, PLC push accounting | — | — | nRF54L15 + nRF5340 Mode A 120 s hardware rows | Direct suite covers pairing/overflow/PLC; hardware activation remains per-session | T8 (closed) |
| `audio_offload.c` | FLPR offload manager | `tests/unit/audio_offload/` — compiles production source | BSim: compiled without CONFIG_SOC_NRF54L15 — only no-op/non-nRF stubs are exercised; not FLPR production-path integration | — | nRF54L15 FLPR healthy + fallback | No known functional gap; branch coverage unmeasured. | T7 |
| `audio_perf.c` | Performance timers | `tests/unit/perf/` — compiles production `src/audio_perf.c` | — | — | — | Basic tests exist; branch coverage unmeasured. | T7 |
| `audio_rate_convert.c` | Fixed-rate frame-count conversion (init/next_frames) | `tests/unit/rate_convert/` — 5 Twister tests compile production source: identity 48k→48k, nRF54L15 baseline 48k→47,619, remainder determinism, re-init reset, remainder proportion (476/477 distribution) | BSim: compiled only — rate-converter calls live in `audio_i2s.c`, excluded from the BSim build; no BSim execution | — | nRF54L15 streaming verification | Functional tests exist; branch coverage unmeasured. | T7 |
| `app_lifecycle.c` | Narrow boot coordinator (ordered fatal init + advertising restart) | `tests/unit/app_lifecycle/` — 13 tests compile production source: exact all-success order incl. optional platform step, platform absent, each of the seven fatal steps failing independently (no later callback, exactly one cold reboot, original errno), restart success (only advertising) and restart failure (one reboot, error), NULL/missing-required-callback `-EINVAL` with zero calls/reboots | — | — | Boot logs on both targets | Init order, reboot-once semantics, restart behavior now direct production-source proof; `main.c` wiring stays proven by production builds. | T6 (closed) |
| `audio_shell.c` | Audio status shell commands | `tests/unit/audio_shell/` — 14 tests compile production `src/audio_shell.c` + `src/bt_shell.c` with `AUDIO_SHELL_TEST` seams, executed through the real Zephyr dummy backend + `shell_execute_cmd` against mocked stats/drift/volume/sink/unpair and real `audio_perf.c` (deterministic cycle injection): exact `audio status` field order/labels, zero-frames `(0%)` without div0, large-value percentage without uint32 overflow, `audio perf` path labels/queue fields/integer one-decimal deadline %, zero-count averages, reset-stats/perf-reset/stop exactly-once + stable text (R1: `audio stop` routes through `bt_bap_audio_path_stop()` exactly once; the old direct sink-stop fake is proven not called), wrapper-seam equivalence, and (R4) config-off absence: the FLPR acceptance commands do not resolve when the acceptance TU is not compiled; `tests/unit/audio_shell_noperf/` — 10 tests, same production files with `CONFIG_AUDIO_PERF_MEASUREMENT=n`: truthful unavailable (zero) deadline percentage, zeroed queue fields, no div0; `tests/unit/audio_shell_nrf54/` — 42 tests compile all four production shell TUs (see the split rows below) | — | — | Log inspection during hardware gates | Zero-safe percentages and unpair propagation now direct production-source proof. | T6 (closed), R4 |
| `bt_shell.c` | `bt unpair` pairing-mode reset command | `tests/unit/audio_shell/`, `tests/unit/audio_shell_noperf/`, `tests/unit/audio_shell_nrf54/` — all compile production `src/bt_shell.c`; `bt unpair` success text and exact negative errno propagation executed through the dummy backend; seam `audio_shell_test_cmd_bt_unpair` proven dispatch-identical | — | — | — | Single command; fully covered. | R4 |
| `flpr_shell.c` | FLPR production diagnostics (`flpr status/offload/runtime/restart`) | `tests/unit/audio_shell_nrf54/` — 42 tests compile production `src/flpr_shell.c` (cross-TU section registration with the acceptance TU) against mocked FLPR APIs: `flpr status` ready/ACKed/healthy/epoch/errors/TX/RX/loss/order, `flpr offload` state/epoch/generation/counters/faults/recovery/probation/runtime-restart/heartbeat-dedup/RTT/last-error + ASRC counters/faults/RTT/cycles (gate-parsed fields), `flpr runtime` full field set with out-of-range enums printing UNKNOWN/unknown, `flpr restart` EBUSY/success-line/failure-errno | — | — | `scripts/flpr_hang_gate.py` + `scripts/flpr_stall_gate.py` parse `flpr offload/status/runtime` output on hardware | Parseable status fields, zero-safe percentages, FLPR gate fields now direct production-source proof. | R4 |
| `flpr_acceptance_shell.c` | FLPR acceptance-harness commands (`flpr ring *`, `flpr stress`, `flpr hang`), `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`-gated | `tests/unit/audio_shell_nrf54/` — 42 tests compile production `src/flpr_acceptance_shell.c` with the acceptance config forced; `flpr ring status` counters/diagnostics/test/latency/stall incl. conditional lines, `flpr ring init/reset/test/stall/stall_flpr/stall_flpr_ms`, `flpr ring acceptance` validation rejects, `flpr stress` ready/active/summary, `flpr hang` not-ready/ACK/failure-errno — all through the real registry (cross-TU section registration proven); long-running successful acceptance stays hardware evidence | — | — | `scripts/flpr_hang_gate.py` + `scripts/flpr_stall_gate.py` inject `flpr hang` / `flpr ring stall_flpr_ms` and parse the ring/offload output on hardware | Acceptance machinery no longer compiled into normal audio diagnostics builds (config-off absence proven in `audio_shell`). | R4 |
| `audio_stats.c` | Stream statistics counters | `tests/unit/stats/` — 10 tests execute production source: exact counter coupling (total = decoded + PLC), reset, by-value snapshots, deterministic repeats, 4-thread concurrent exact counts | BSim: compiled and genuinely exercised — production `audio_stats.c` runs through `audio_decode.c`/`bt_bap.c`, and the sink stub reads `audio_stats_get()` snapshots; local counters only supplement startup accounting | — | Hardware log verification | No known functional gap; branch coverage unmeasured. | T7 |
| `audio_stream_session.c` | App-owned BAP sink receive/session state (R6): validated codec shape, decoder contexts, per-CIS ISO sequence trackers, Mode A assembler, receive counters, presentation delay, mode inference, malformed-SDU rejection, omitted-callback synthesis, and the decode/conceal/volume/push orchestration with admission/lease discipline | `tests/unit/audio_stream_session/` — 29 tests compile production source against faithful fake sink/volume/observer seams with real liblc3 decode (checked-in 48 kHz fixtures, `--wrap=lc3_decode` hard-failure injection): config/accessors/invalid slots, mono/Mode B/Mode A classification and golden decode+push, Mode A equal-TS pair / one-sided loss / missing-TS rejection, malformed-SDU rejection with resume (mono + Mode A mutation-order), LOST PLC, decoder-not-ready skip, hard decode failure skip + Mode A event consumption, sequence-gap PLC cadence (mono/Mode B) and Mode A synthetic-LOST ordering, resync no-synthesis, admission closed/open, rx_close drain of an admitted lease + no-lock-across-decode/sink + generation reset (real threads), release slot reuse, reset_all, reconnect fresh session, sink-failure perf accounting, disable-keeps-shape, gate-independent valid-recv counting, start_clear re-base | BSim T4 matrix: full receive path through the real session (same pins) | — | nRF54L15 + nRF5340 Mode A/B 120 s hardware rows (R6) | Malformed/gap/PLC ordering proven direct; hardware gap activation remains non-deterministic | R6 |
| `audio_timing_math.c` | Timing math shared across platforms | `tests/unit/timing/` — Twister suite; also compiled by `tests/unit/timing_nrf54/` (production ppm path) | BSim: compiled only — consumed by `audio_timing_nrf54.c`, which is not compiled into BSim; no BSim execution | — | — | Functional tests exist; branch coverage unmeasured. | T7 |
| `audio_timing_none.c` | nRF5340 no-op timing | — | BSim: compiled; `audio_timing_sdu_ref_update()` no-op is called from the `bt_bap.c` stream path (init/reset live in `audio_i2s.c`, not called in BSim) | — | nRF5340 hardware streaming | No direct unit test; no-op implementation is low-risk. BSim integration evidence exists. Gap not in T5 scope. | — (low-risk no-op) |
| `audio_timing_nrf54.c` | nRF54L15 GRTC+PCLK timing | `tests/unit/timing_nrf54/` — 19 tests compile production `audio_timing_nrf54.c` + `audio_timing_math.c` against include-shadow mocks of nrfx_grtc/nrfx_gppi/nrf_grtc/nrf_timer and a drift-feedforward mock: alloc-failure exact errors, GPPI-failure cleanup (cc_disable then free, no GPPI free), full init sequence + idempotence, pre-init/zero-ts no-ops, one anchor per session, past/future/wrap first compares, baseline + exact-ppm callbacks incl. TIMER32 wrap, late reschedule, reschedule failure, reset semantics, stale-generation rejection, per-measurement drift delivery, plus the review-fix FIFO contract: 10-payload backlog drained in order from one work invocation, stale-then-fresh mixed generations in one FIFO, and full-FIFO overflow (observable fault, accepted entries drained in order, later callbacks inactive); R1 control-mutex serialization: update thread blocked inside the first compare programming (mock gate) holds the control mutex, the reset thread cannot disable/reset until release, final state inactive/generation-incremented/compare-disabled and a fresh anchor succeeds | — | — | nRF54L15 PCLK diagnostics + feedforward | GRTC compare, GPPI, TIMER20 capture, allocation cleanup, schedule failure, stale generation rejection, and FIFO/backlog/overflow behavior now direct production-source proof (narrow `AUDIO_TIMING_NRF54_TEST` seams only). | T5 (closed) |
| `audio_volume.c` | Volume control | `tests/unit/volume/` — 12 tests execute production VCP branch against a shadow of the exact NCS v3.3.0 renderer types + fake `bt_vcp_vol_rend_register()` (real `audio_perf.c` compiled for hook balance): registration fields/defaults, failure propagation, callback packing, mute/zero/unity/intermediate scaling incl. signed extremes, zero/null samples, perf-hook balance on all exits, concurrent callback toggling atomic snapshot | BSim: compiled and exercised | — | Hardware streaming at default volume | No known functional gap; branch coverage unmeasured. | T7 |
| `bt_bap.c` | BAP unicast server, ASCS, PACS, pairing, and the thin recv adapter (R6: app audio receive state moved to `audio_stream_session.c`) | — | BSim T4 matrix: 16 scenarios over real ASCS/PACS/ISO — mono 7.5/10 ms, Mode A (incl. reverse start), Mode B, malformed-SDU rejection + resume, one-CIS-loss (lossless-mode left hash + deterministic right PLC concealments), first-ASE stop, release-without-disable, disconnect-while-streaming, reconnect (second segment equals a fresh mono oracle), source-direction rejection (exact CONF_UNSUPPORTED/NONE), NO_MEM on third sink (resource seam), nine invalid-codec-field rejections (exact CONF_REJECTED/CODEC_DATA) + two successes (valid mono + missing-frame-blocks fallback); strict oracle with FNV-corrected full/L/R hashes, source-valid startup boundary with zero post-start PLC, per-push source validity from production flow, release sink-stop ordering proof, missing-TS validation; all pinned and pairwise deterministic | — | Hardware connection + streaming | No direct unit test; BSim scenarios cover the matrix (T4); the receive mechanics are direct-proven through `audio_stream_session.c`.  Alternate rates/codecs remain unsupported by design. | T7, R6 |
| `bt_pairing_policy.c` | Pure OPEN/BONDED_ONLY policy snapshot | `tests/unit/bt_pairing_policy/` — compiles production source: open default, set-bonds bonded-only/zero/overflow-atomic, pairing-accept open/bonded-only, mark-bonded add + bonded-only, full-snapshot overflow, request-open reset, snapshot atomic/empty | — | — | Bonded-only FAL filtering on both targets (hardware baseline pairing-filter phase) | Direct suite covers the pure policy; HCI/controller filter work remains in `bt_bap.c` | T8 (closed) |
| `flpr_audio_process.c` | FLPR audio block wrapper | `tests/unit/flpr_audio_process/` — compiles production source | — | — | nRF54L15 FLPR streaming | No known functional gap; branch coverage unmeasured. | T7 |
| `flpr_cache.c` | FLPR cache operations | Compiled by `flpr_ring`, `flpr_audio_process`, and `flpr_ring_mgr` (T1) suites (production source) | — | — | nRF54L15 FLPR activation | native_sim coverage is API/barrier-call proof only — the native branch uses `atomic_thread_fence`; physical cache/barrier semantics remain hardware-only. Branch coverage unmeasured. | T7 |
| `flpr_handshake.c` | FLPR boot handshake + VEVIF IPC (R8: production slot reset/consumer + diagnostic slot registration; stress/fault-hang state moved to `flpr_acceptance.c`) | `tests/unit/flpr_handshake/` — 46 tests compile and execute `src/flpr_handshake.c` against a fake IPC service backend (real `ipc_service_*` APIs, production callbacks); R1 adds validation counters under concurrent status reads (reader thread polls while invalid messages are injected; the counter pair stays monotonic and ends exact) and changed-epoch failed-ACK stale-state prevention (a changed READY epoch clears `acked`; a failed READY_ACK send cannot leave the stale fast path usable, restored only by a successful duplicate READY ACK) | — | — | nRF54L15 FLPR handshake | Bind/unbind, READY/duplicate/changed epoch, ACK send failures, heartbeat health transitions, ring dispatch, disconnect/reconnect, stress, fault-hang now direct production-source proof. Branch coverage unmeasured. | T7 |
| `flpr_ring.c` | SPSC ring buffer (shared SRAM) | `tests/unit/flpr_ring/` — compiles production source | — | — | nRF54L15 FLPR ring through I/O | No known functional gap; branch coverage unmeasured. | T7 |
| `flpr_ring_mgr.c` | Ring manager production core: paired rings, reset, typed ASRC produce/consume, notify, wait, remote restart (R8: acceptance APIs moved to `flpr_acceptance.c`) | `tests/unit/flpr_ring_mgr/` — 28 tests compile and execute `src/flpr_ring_mgr.c` (+ real `flpr_ring.c`, `flpr_cache.c`, `flpr_control_ack.c`) with host ring arrays and a handshake mock; R1 barrier/repeated-init/token/late-ACK tests retained; `tests/unit/flpr_acceptance/` compiles the same core with the acceptance hooks active | — | — | nRF54L15 FLPR ring manager through I/O | Production reset/consume/ASRC/notify/wait/remote-restart now direct production-source proof; acceptance counters proven in the flpr_acceptance suite. | R8 |
| `src/flpr_acceptance.c` | Cpuapp FLPR acceptance module (R8): ring test, stalls + ACK correlation, stale produce, report aggregation, acceptance status, stress, fault hang, gates 1–6 — `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` | `tests/unit/flpr_acceptance/` — 48 tests compile and execute `src/flpr_acceptance.c` (+ real core ring mgr, control-ACK engine, ring, cache) against a faithful two-slot handshake mock: test_run loop incl. rate-limit/timeout/full/notify-failure, stall packing/validation/ACK correlation/token boundary/remote-restart, stale produce, report subtypes, backpressure/CRC/payload/latency accounting, stress (match/stale/future/clamp/timeout/late PONG), fault hang, gate prep | — | — | `scripts/flpr_hang_gate.py` + `scripts/flpr_stall_gate.py` + nRF54L15 Mode A/B/hang/stall hardware | all-gates-PASS tail of `flpr_acceptance_run_gates` requires a live FLPR worker (hardware evidence). | R8 |
| `src/flpr_control_ack.c` | Shared control-ACK correlation engine (R8): ONE owner of reset + stall ACK correlation, token/arm/handle/wait/reset-session | `tests/unit/flpr_ring_mgr/` + `tests/unit/flpr_acceptance/` — direct production-source execution (reset side via the ring-mgr suite, stall side via the acceptance suite: timeout/disarm, first-ACK-wins, stale rejection by sequence, 0xFFFF token boundary + remote-restart reset) | — | — | — | Fully direct-covered. | R8 | `tests/unit/flpr_ring_mgr/` — 66 tests compile and execute `src/flpr_ring_mgr.c` (+ real `flpr_ring.c`, `flpr_cache.c`) with host ring arrays and a handshake mock; R1 adds the ring-data-lock barrier (producer paused after produce begin holds the data lock; reset cannot run/mutate until release), repeated-init preservation/serialization (repeated init during a live session is a non-destructive no-op proven by unchanged epoch/headers/contents/semaphore tokens; concurrent init blocks on the same data lock as a paused producer), epoch-0 produce rejection before slot mutation, reset/stall ACK sequence correlation (late-ACK-by-sequence retry regression), and the 16-bit token boundary (0xFFFF succeeds, next overflows -EOVERFLOW, remote restart permits token 1); R2 removed the dead `flpr_ring_mgr_set_consume_cb()` no-op test (67 → 66) | — | — | nRF54L15 FLPR ring manager through I/O | Coordinated reset, invalidation races, semaphore draining, producer/consumer validation, backpressure, sequence wrap, remote restart, data-lock barrier, ACK correlation now direct production-source proof. Branch coverage unmeasured. | T7 |
| `flpr_runtime.c` | Synchronous FLPR VPR runtime restart manager | `tests/unit/flpr_runtime/` — 21 tests compile and execute the real nRF54 restart body (shadow VPR HAL, host source/exec arrays, ordered event log) | — | — | nRF54L15 FLPR runtime restart + fault handling | DMCONTROL transitions, fault stages, CRC rejection, mutex busy, duration accounting now direct production-source proof. Physical cache/FLPR entry behavior remains hardware-only. Branch coverage unmeasured. | T7 |
| `main.c` | Boot wiring, watchdog device/thread, advertising loop | Boot ordering/reboot semantics delegated to `app_lifecycle.c` (direct suite above); `main.c` itself is not compiled into any unit suite — hardware wiring remains proven by the production builds | — | — | Boot logs on both targets | Fatal init order and reboot behavior now direct production-source proof via the coordinator; `main.c` remains adapter-only glue. | T6 (closed) |
| `pairing_mode.c` | Portable NORMAL/BONDING/RESETTING transition owner (P1): asynchronous transition phases, LED patterns, supersession, completion, synchronous reset, fatal recovery; all platform side effects injected | `tests/unit/pairing_mode/` — 32 tests compile the production source against fake injected operations with a spinlock-protected operation ledger, per-op event semaphores, blocking gates, and short tick-aligned Kconfig timings that preserve the production ratios (bond half 100 ms, reset half 20 ms, feedback 200 ms = ten half-periods / five complete flashes): invalid/missing-ops atomic rejection, init status + -EALREADY, start success/idempotence, start failures at every stage reboot once, bonding no-peer exact op order, bonding-connected waits for disconnect, duplicate bonding no-op, reset no-peer exact op order, reset-connected deletes bonds only after disconnect, reset supersedes pending/active bonding, exact rapid LED sequence + delayed BONDING start, slow blink exact half-periods, stale generation work cannot modify a newer transition, connected-in-BONDING requests security, stale connected re-disconnects, bonded pairing/secure-reconnect complete to NORMAL without disconnect/start, pairing failure / non-bonded security stay BONDING, security-request failure reboots, every platform-op failure reboots once, no operations after fatal, sync reset returns only after BONDING advertising (gated op), sync reset timeout continues the transition, second sync waiter -EBUSY, duplicate disconnect harmless, RESET priority over concurrently pending BONDING, uninitialized APIs -EINVAL, get_status NULL-safe | — | — | — (P4+ hardware integration phases) | The full transition/phase/LED/fatal matrix is now direct production-source proof; `user_pairing_io`/Bluetooth adapter integration is a later phase. | P1 |
| `stream_lifecycle.c` | Stream start/stop lifecycle | `tests/unit/lifecycle/` — 28 tests compile production source: Mode A/B/mono gates, close idempotence, closed-to-open edge semantics (duplicate starts return false), configure/start/close/reconfigure/start permutations, release-then-slot-reuse, reset from closed/partial/open states, repeated open/close cycles, inert unconfigured starts; R1 forced-close latch (was-open return for exact first-close observer emission, later starts blocked for the configured slot set, one-slot release does not unblock while another remains, final release + reconfigure permits open, reset permits fresh open, idle force-close does not latch a future first configure/start); R6 narrowed `stream_lifecycle_sink_configured()` to slot occupancy only (chan_count parameter/storage removed — three chan_count-specific tests deleted: zero/negative chan_count absence and the `configured(idx, 0)` stale-latch scenario, which no longer exists; released-slot inert start remains covered by release-then-slot-reuse) | BSim T4 matrix: gate open/close across mono, Mode A (two-ASE set), first-ASE stop, release-without-disable, disconnect-while-streaming, and reconnect; closed-gate receive evidence | — | Hardware connect/disconnect cycles | Duplicate-start edge semantics now direct production-source proof. | T5 (closed), R6 |
| `src/flpr/main.c` | FLPR firmware entry point (RISC-V VPR): READY/heartbeat/reset/producer/consumer + production ring processing (R8: acceptance handlers moved to `src/flpr/acceptance.c`) | — | — | — | nRF54L15 FLPR firmware loaded + active | **No direct unit test.** No RISC-V simulator test infrastructure exists. Hardware-only by design; the moved acceptance message/state logic IS direct-tested (`tests/unit/flpr_acceptance_flpr`). | — (hardware-only) |
| `src/flpr/acceptance.c` | FLPR-image acceptance handlers (R8): RING_TEST_START/STOP report cascade, RING_STALL timer + ACK echo, STRESS_PING/PONG, FAULT_HANG ACK-before-spin, diagnostic note hooks — `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` | `tests/unit/flpr_acceptance_flpr/` — 8 tests compile and execute `src/flpr/acceptance.c` directly via its injected send/wake dependency table (no ipc_service/DT/RV32 glue): report cascade exact subtype packing, stall ACK echo + timed auto-clear, ring-reset state reset, stress echo, hang ACK-before-pending, unknown-message false | — | — | nRF54L15 FLPR firmware + hang/stall gates | The hardware spin (irq_lock/busy-wait) stays in main.c (hardware-only); all message/state logic direct-covered. | R8 |

## Current suite inventory

R3 (2026-08-04): all suite discovery now flows through
`scripts/test_inventory.py` — the single filesystem classification source
consumed by `scripts/test-all.sh`, `scripts/test-coverage.sh`, and
`scripts/check-test-matrix.py` (which validates the inventory).  Adding a
suite cannot silently omit it from the gate or coverage.  The BabbleSim
Stage 1 matrix, run counts, and pinned hashes live in
`tests/bsim/stage1-scenarios.json` (shared by `bsim-stage1-run.sh` and
`bsim_stage1_parse.py`); FLPR status parsing is shared via
`scripts/flpr_status.py` (stall + hang gates).

| Category | Count | Suites |
|----------|-------|--------|
| Twister C (testcase.yaml) | 32 | actuator_apll, actuator_apll_nohfclk, actuator_none, actuator_sample_adjust_historical, app_lifecycle, asrc, audio_i2s, audio_i2s_identity, audio_shell, audio_shell_noperf, audio_shell_nrf54, audio_stream_session, bt_pairing_policy, decode, drift, flpr_acceptance, flpr_acceptance_flpr, flpr_handshake, flpr_protocol, flpr_ring_mgr, flpr_runtime, iso_seq, lifecycle, modea, pairing_mode, perf, rate_convert, stats, timing, timing_none, timing_nrf54, volume |
| Exec-only C (CMakeLists.txt, no testcase.yaml) | 5 | audio_offload, flpr_audio_process, flpr_ring, offload_asrc, offload_asrc_verify |
| Python | 16 | fw_flash_dongle (test_fw_flash_dongle.py), flpr_stall_gate (test_flpr_stall_gate.py), flpr_hang_gate (test_flpr_hang_gate.py), bluez_wireplumber_gate (test_bluez_wireplumber_gate.py), bluez_wireplumber_phase3_gate (test_bluez_wireplumber_phase3_gate.py), bsim_runner (test_bsim_stage1_parse.py), build_contract (test_build_contract.py), hci_raw_connect (test_hci_raw_connect.py), bap_central_policy (test_bap_central_policy.py), bap_central_writer (test_bap_central_writer.py), test_matrix (test_check_test_matrix.py), test_coverage_runner (test_test_coverage_runner.py), bap_central_device (test_bap_central_device.py), bap_central_security (test_bap_central_security.py), bap_central_endpoint (test_bap_central_endpoint.py), bap_central_session (test_bap_central_session.py) |
| Coverage | 1 | coverage (test-coverage.sh default mode: rebuilds the 31 twister + 5 exec suites with CONFIG_COVERAGE=y, enforces the committed baseline) |
| Test-matrix checker | 1 | matrix (check-test-matrix.py --coverage-json on the coverage run's coverage.json) |
| BabbleSim | 1 | bsim_stage1 (T4+R7 17-scenario BAP matrix, scenarios 1–9 twice, remaining eight once) |
| **Total gate children** | **56** | |

> **T7 canonical gate ACCEPTED (2026-08-02) — historical 41-child evidence.**
>  The 41-child composition
> (25 twister + 4 exec-only + 9 Python + coverage + matrix + BSim) was
> observed on the exact accepted T7 commit `8f7bfca` (the warning-fix
> commit; baseline commits `c6adce8`/`4a31324`) from a detached fresh
> clone on `thomas-workstation` in the flake dev shell: **`41 PASS /
> 0 FAIL / 41 TOTAL`**, script exit 0, elapsed 816 s (13m36s), **zero
> Kconfig assigned-value warnings** and zero compiler warnings.  Observed
> evidence, warning classification, and log provenance: `STATUS.md` (T7
> section) and `docs/development/workstation-transfer-status.md`.
> The **historical T8 accepted 47-child composition** (28 twister + 4
> exec-only + 12 Python + coverage + matrix + BSim) is the T8 gate
> described below; the **current accepted gate is 55 children** (31
> twister + 5 exec-only + 16 Python + coverage + matrix + BSim) since
> R8/R9 — see the "Current suite inventory" table above.

## Explicit weak-test facts

1. **Historical actuator suite** (`actuator_sample_adjust_historical`)
   compiles the retired `audio_clock_actuator_sample_adjust_historical.c`
   (test-local copy moved out of `src/` in R2), not either
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
    `audio_volume.c`, and `audio_offload.c`; the accepted T4 matrix (16
    scenarios, incl. one-CIS-loss) executes them through real mono/Mode
    A/Mode B, lifecycle, reconnect, rejection, and one-CIS-loss paths with
    a strict PCM oracle and no I2S or FLPR stack.  Compilation alone is
    still not execution evidence for
   `audio_drift.c`, `audio_rate_convert.c`, and `audio_timing_math.c` —
   those are compiled but never called (their call sites live in
   `audio_i2s.c` and `audio_timing_nrf54.c`, which are excluded from the
   BSim build).  `audio_offload.c` runs only its non-nRF54 stub branch
   (no CONFIG_SOC_NRF54L15).  `audio_asrc.c` and `audio_i2s.c` are not
   compiled into BSim at all.
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

## T7 numeric baseline (exact post-gap measurements, never lowered)

Generated by `scripts/test-coverage.sh --write-baseline` on the clean commit
`c6adce8` (schema v1), committed in `4a31324` as
`tests/coverage-baseline.json`; default enforcement then reran on clean
`4a31324` with identical ratios.
Scope: the 23-file numeric population (all `src/*.c` except `main.c`,
`bt_bap.c`, `src/flpr/main.c`, `audio_clock_actuator_sample_adjust.c`);
test-only helper blocks marked `GCOVR_EXCL_START`/`GCOVR_EXCL_STOP`
(`AUDIO_I2S_NATIVE_TEST`, `AUDIO_SHELL_TEST`, `AUDIO_TIMING_NRF54_TEST`,
`FLPR_HANDSHAKE_NATIVE_TEST`, `FLPR_RING_MGR_NATIVE_TEST`,
`FLPR_RUNTIME_NATIVE_TEST`, and the `CONFIG_ZTEST` perf injection helper)
never enter these numbers.

| Metric | Covered/Total | Percent |
|--------|---------------|---------|
| lines | 3070/3503 | 87.6% |
| branches | 1332/1921 | 69.3% |
| functions | 182/182 | 100.0% |

Per-file (covered/total): app_lifecycle 47/47 L, 50/54 B, 4/4 F;
audio_asrc 112/120 L, 76/98 B, 7/7 F; actuator_apll 17/17 L, 4/4 B, 4/4 F;
actuator_none 8/8 L, 0/0 B, 4/4 F; audio_decode 121/125 L, 100/102 B, 5/5 F;
audio_drift 65/68 L, 28/31 B, 5/5 F; audio_i2s 203/209 L, 132/176 B, 10/10 F;
audio_offload 588/714 L, 223/377 B, 18/18 F; audio_perf 82/85 L, 19/22 B, 8/8 F;
audio_rate_convert 30/32 L, 14/16 B, 3/3 F; audio_shell 302/519 L, 124/274 B,
23/23 F; audio_stats 30/30 L, 0/0 B, 7/7 F; audio_timing_math 16/16 L, 4/4 B,
4/4 F; audio_timing_none 6/6 L, 0/0 B, 3/3 F; audio_timing_nrf54 131/135 L,
56/78 B, 7/7 F; audio_volume 36/36 L, 24/32 B, 5/5 F;
flpr_audio_process 97/104 L, 46/58 B, 4/4 F; flpr_cache 9/9 L, 0/0 B, 3/3 F;
flpr_handshake 357/369 L, 134/181 B, 17/17 F; flpr_ring 86/87 L, 28/32 B, 8/8 F;
flpr_ring_mgr 559/599 L, 214/312 B, 24/24 F; flpr_runtime 126/126 L, 34/46 B,
3/3 F; stream_lifecycle 42/42 L, 22/24 B, 6/6 F.

Enforcement: `test-coverage.sh` default mode requires a clean worktree,
compares current vs baseline with integer cross multiplication (overall
lines/branches and every per-file lines/branches/functions record), and
hard-fails on population drift.  `check-test-matrix.py --coverage-json`
enforces that every compiled production function executes at least once
(zero-hit function error), that every direct public API appears in the
exact-outcome ledger, and that stateful entries carry transitions.
Both are ordered children of `scripts/test-all.sh` before the mandatory
BSim child.  These ratios and checker results are implementation evidence
on the exact commits `c6adce8`/`4a31324`; the canonical 41-child gate
acceptance on `8f7bfca` (warning-fix commit) is **ACCEPTED (2026-08-02)**:
observed exact `41 PASS / 0 FAIL / 41 TOTAL`, elapsed 816 s (13m36s),
zero Kconfig assigned-value warnings (see `STATUS.md` T7 section and
`docs/development/workstation-transfer-status.md`).

## Baseline refreshes after T7 (never lowered)

The committed baseline was refreshed twice after T7, each time only by
adding a direct-suite source file (no per-file regression, no population
removal, enforced by `test-coverage.sh --write-baseline` on a clean
commit and re-verified by the canonical gate):

- `ac1fa06` (2026-08-03) — added `src/audio_modea.c` (11/11 functions
  via the `modea` suite); population 23 → 25 files.
- `6578a9c` (2026-08-03) — added `src/bt_pairing_policy.c` (8/8 functions
  via the `bt_pairing_policy` suite); population 25 → 26 files.
- `1a5842d` (2026-08-04, T8 sequence-gap fix follow-up) — added
  `src/audio_iso_seq.c` (4/4 functions, 36/38 lines, 20/24 branches via
  the 18-test `iso_seq` suite).  At that point the numeric population was
  **26 files** (historical T8 record; the current population is 33 since
  the R4/R6/R8 migrations below):

| Metric | Covered/Total | Percent |
|--------|---------------|---------|
| lines | 3281/3722 | 88.2% |
| branches | 1433/2041 | 70.2% |
| functions | 205/205 | 100.0% |

Current per-file (covered/total) on the 26-file population, from
`tests/coverage-baseline.json` at `1a5842d`: app_lifecycle 47/47 L,
50/54 B, 4/4 F; audio_asrc 112/120 L, 76/98 B, 7/7 F; actuator_apll 17/17 L,
4/4 B, 4/4 F; actuator_none 8/8 L, 0/0 B, 4/4 F; audio_decode 121/125 L,
100/102 B, 5/5 F; audio_drift 65/68 L, 28/31 B, 5/5 F; audio_i2s 203/209 L,
132/176 B, 10/10 F; audio_iso_seq 36/38 L, 20/24 B, 4/4 F; audio_modea
109/115 L, 61/76 B, 11/11 F; audio_offload 588/714 L, 223/377 B, 18/18 F;
audio_perf 82/85 L, 19/22 B, 8/8 F; audio_rate_convert 30/32 L, 14/16 B,
3/3 F; audio_shell 302/519 L, 124/274 B, 23/23 F; audio_stats 30/30 L, 0/0 B,
7/7 F; audio_timing_math 16/16 L, 4/4 B, 4/4 F; audio_timing_none 6/6 L,
0/0 B, 3/3 F; audio_timing_nrf54 131/135 L, 56/78 B, 7/7 F; audio_volume
36/36 L, 24/32 B, 5/5 F; bt_pairing_policy 66/66 L, 20/20 B, 8/8 F;
flpr_audio_process 97/104 L, 46/58 B, 4/4 F; flpr_cache 9/9 L, 0/0 B, 3/3 F;
flpr_handshake 357/369 L, 134/181 B, 17/17 F; flpr_ring 86/87 L, 28/32 B,
8/8 F; flpr_ring_mgr 559/599 L, 214/312 B, 24/24 F; flpr_runtime 126/126 L,
34/46 B, 3/3 F; stream_lifecycle 42/42 L, 22/24 B, 6/6 F.

The T8-follow-up canonical gate on `1a5842d` is **47 PASS / 0 FAIL /
47 TOTAL** (28 twister suites including the new `iso_seq`, 4 exec suites,
12 python suites, coverage, matrix, BSim Stage 1) — historical T8
record; the current gate is 55 children (see the suite inventory above),
zero compiler and
Kconfig assigned-value warnings in the production builds.

Exact observed re-run of that gate retained (evidence-fix commit,
2026-08-04): `./scripts/test-all.sh` on `thomas-workstation` at
2026-08-04T05:26:57+02:00 (worktree clean; production tree identical to
`971e6a4`), stdout+stderr to `/tmp/t8-final-47.log` — **`Gate complete:
47 PASS / 0 FAIL / 47 TOTAL`**, `PASS`, exit 0, elapsed **1016.45 s**
(bash `time` builtin; `/usr/bin/time` not installed).  Warning scan of
the retained log: zero compiler warnings, zero Kconfig assigned-value
warnings; 85 `<wrn>`/`<err>` lines all inside deliberately-passing
failure-injection unit suites (expected negative-path test output); 2
native_sim test-entropy notices (pre-existing informational line).  The
log is transient (NOT repository-retained); this committed record is the
durable evidence.

## R2 baseline migration (2026-08-04) — dead-code deletion, population 26

The pre-R2 per-file rows above (generated at `1a5842d`) are the historical
record.  R2 deleted covered-but-dead production APIs (no production caller;
caller proof in `docs/development/refactor-r2-results.md`), which changes
source denominators, so the baseline was regenerated per the coverage
migration rule (refactor-plan.md) on the exact clean code commit
**`1343c35`** via `scripts/test-coverage.sh --write-baseline`; the committed
baseline now records that candidate.  Tool versions unchanged: **gcovr 8.4 /
gcov (GCC) 14.3.0**.

**Population stays 26** — the moved historical source was already excluded
from the numeric population, so its relocation out of `src/` changes only the
exclusion provenance, not the population.  The numeric exclusion list drops
`src/audio_clock_actuator_sample_adjust.c` (now a test-local file under
`tests/unit/actuator_sample_adjust_historical/src/`), leaving `bt_bap.c`,
`src/flpr/main.c`, and `main.c`.

Aggregate (old `1a5842d` → new `1343c35`):

| Metric | Old | New |
|--------|-----|-----|
| lines | 3281/3722 (88.2%) | 3505/3946 (88.8%) |
| branches | 1433/2041 (70.2%) | 1467/2067 (71.0%) |
| functions | 205/205 (100.0%) | 209/209 (100.0%) |

Per-file old → new for R2-affected surviving files (covered/total):

| File | Metric | Old | New | Denominator explanation |
|------|--------|-----|-----|--------------------------|
| `audio_clock_actuator_apll.c` | lines | 17/17 | 15/15 | removed `audio_clock_actuator_consume_sample_adjustment()` (1 line, fully covered, zero callers) |
| | branches | 4/4 | 4/4 | unchanged |
| | functions | 4/4 | 3/3 | removed function |
| `audio_clock_actuator_none.c` | lines | 8/8 | 6/6 | removed consume implementation (2 lines, fully covered, zero callers) |
| | branches | 0/0 | 0/0 | unchanged |
| | functions | 4/4 | 3/3 | removed function |
| `audio_rate_convert.c` | lines | 30/32 | 10/10 | removed `audio_rate_converter_nearest_stereo()` (zero callers; 2 previously-uncovered lines were inside it) |
| | branches | 14/16 | 0/0 | all branches were in the removed function |
| | functions | 3/3 | 2/2 | removed function |
| `flpr_ring_mgr.c` | lines | 559/599 | 694/741 | removed no-op `flpr_ring_mgr_set_consume_cb()` (2 lines, fully covered, zero callers); R1-era ring-manager tests were previously un-migrated and are absorbed here |
| | branches | 214/312 | 230/326 | no-op added no branches; R1-era tests absorbed |
| | functions | 24/24 | 29/29 | removed function; R1-era additions absorbed |
| `audio_offload.c` | lines | 588/714 | 583/707 | removed `audio_offload_is_stopped()` (7 measured lines, fully covered, zero callers); surviving `get_status(NULL)` null-guard now covered by a probe added to `test_is_healthy` (migration rule step 4: ratio otherwise decreases) |
| | branches | 223/377 | 223/375 | removed is_stopped branch slots (2), gained NULL-guard branch slot (1) |
| | functions | 18/18 | 17/17 | removed nRF54 + stub implementations (2 functions) |
| `audio_volume.c` | all | 36/36 L, 24/32 B, 5/5 F | unchanged | `DEFAULT_VOL` macro removal produces no executable lines |

Unchanged files remain at or above their `1a5842d` records.  The larger
increases in `audio_i2s.c`, `flpr_handshake.c`, `stream_lifecycle.c`, and
`audio_timing_nrf54.c` are R1-era test additions (accepted after `1a5842d`
with enforcement passing because current ≥ baseline) absorbed into the
baseline at this migration; they are not R2 changes.  No covered live
behavior was deleted to improve a percentage.

Canonical enforcement reran on the clean baseline commit (see
`docs/development/refactor-r2-results.md`): **47 PASS / 0 FAIL / 47 TOTAL**.

## R4 baseline migration (2026-08-04) — shell split, population 26 → 29

R4 split the monolithic `src/audio_shell.c` into four per-owner files
without changing any command name, help, arg count, output, or return
behavior (see `docs/development/refactor-r4-handoff.md`).  The pre-R4
per-file rows above (R2 migration at `1343c35`) are the historical record.
Per the coverage migration rule (refactor-plan.md), the baseline was
regenerated on the exact clean implementation commit **`39c318a`**
(`refactor: split shell command ownership by subsystem`) via
`scripts/test-coverage.sh --write-baseline /tmp/r4-baseline-candidate.json`;
the committed `tests/coverage-baseline.json` is the byte-exact copy of
that candidate.  Tool versions unchanged: **gcovr 8.4 / gcov (GCC)
14.3.0**, recorded in the baseline.

**Population 26 → 29 files** — the single `src/audio_shell.c` record is
replaced by `src/audio_shell.c`, `src/bt_shell.c`, `src/flpr_shell.c`,
`src/flpr_acceptance_shell.c`.  The numeric exclusion list is unchanged
(`src/bt_bap.c`, `src/flpr/main.c`, `src/main.c`).

### Mechanical split aggregate (old file → four replacement files)

| File | lines | branches | functions |
|------|-------|----------|-----------|
| `audio_shell.c` (old monolithic) | 302/519 | 124/274 | 23/23 |
| `audio_shell.c` (audio commands only) | 51/51 | 12/12 | 5/5 |
| `bt_shell.c` | 6/6 | 2/2 | 1/1 |
| `flpr_shell.c` | 95/134 | 34/56 | 7/7 |
| `flpr_acceptance_shell.c` | 150/328 | 76/204 | 10/10 |
| **Sum of the four replacements** | **302/519** | **124/274** | **23/23** |

The aggregate is **exactly equal** to the old record — a purely mechanical
split.  Per-file ratio movement is a compiler/config attribution effect,
not a behavior change: the audio-only TU became 100% covered because the
previously-uncovered FLPR/acceptance lines moved into `flpr_shell.c` /
`flpr_acceptance_shell.c`, where the long-running acceptance gates (which
need physical FLPR transport) remain hardware-evidence-covered lines.
Zero-hit functions remain forbidden: all 23 functions across the four
files execute (10 acceptance + 7 diagnostics + 5 audio + 1 bt unpair).

### Aggregate (pre-R4 committed → post-R4 candidate)

| Metric | Old (population 26) | New (population 29) |
|--------|---------------------|---------------------|
| lines | 3505/3946 (88.8%) | 3505/3946 (88.8%) |
| branches | 1467/2067 (71.0%) | 1467/2067 (71.0%) |
| functions | 209/209 (100.0%) | 209/209 (100.0%) |

All unchanged files remain at or above their pre-R4 records (verified by
per-file cross-multiplication against the committed baseline before the
copy).  No covered live behavior was deleted to improve a percentage.
`tests/test-matrix.json` records the four owners; hardware scripts
`scripts/flpr_hang_gate.py` and `scripts/flpr_stall_gate.py` attach to
`flpr_shell.c` and `flpr_acceptance_shell.c` where their parsed output is
produced.

Canonical enforcement reran on the clean R4 baseline commit (see
`docs/development/refactor-r4-results.md`): **47 PASS / 0 FAIL / 47
TOTAL**.

## R5 (2026-08-05) — offload decomposition, no baseline rewrite

R5 decomposed `audio_offload_process_asrc()` into private static stage
helpers in `src/audio_offload.c` (no physical split) with one shared
fault finalizer and one success commit; added the verify-enabled
exec-only suite `tests/unit/offload_asrc_verify`
(`CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=1`), which pulls the shadow code into
merged `audio_offload.c` totals.  Exec-only inventory 4 → 5; canonical
gate children 47 → 48.

Per the coverage migration rule and the R5 handoff policy, **no baseline
rewrite was performed**: population stays **29**, tool versions unchanged
(gcovr 8.4 / gcov (GCC) 14.3.0), and every per-file + aggregate ratio
improved over the committed baseline (the runner compares ratios +
population + tool versions, not exact totals):

| File | metric | committed baseline | current (report-only + gate) |
|------|--------|--------------------|------------------------------|
| `audio_offload.c` | lines | 583/707 (82.5%) | **581/633 (91.8%)** |
| | branches | 223/375 (59.5%) | **236/353 (66.9%)** |
| | functions | 17/17 | **29/29** |
| aggregate (29 files) | lines | 3505/3946 (88.8%) | **3503/3872 (90.5%)** |
| | branches | 1467/2067 (71.0%) | **1480/2045 (72.4%)** |
| | functions | 209/209 | **221/221** |

The line/branch denominator decrease is the decomposition itself (one
finalizer replaces ~17 copied fault epilogues; the shadow block newly
enters the totals via the verify suite); covered live behavior was not
deleted.  All 29 `audio_offload.c` functions execute (zero-hit check
clean via `check-test-matrix.py --coverage-json`).  `test-matrix.json`
records `offload_asrc_verify` as a direct suite for `audio_offload.c`
and `audio_asrc.c`.  Canonical enforcement on the clean `9dd5108`:
**48 PASS / 0 FAIL / 48 TOTAL**.

## R7 note (2026-08-05) — teardown coordinator, no baseline change

R7 added one private teardown transition owner in `src/bt_bap.c`
(integration-only, excluded from the numeric population — unchanged).
No production file split or deletion, so **no baseline migration**: the
population stays **30** and the coverage child enforced the committed
baseline with zero drift on the R7 gate.  New direct tests only improve
the `stream_lifecycle.c` and `audio_stream_session.c` ratios (lifecycle
28→33 tests, session 29→35 tests); every file remains at or above its
committed record.

## R8 baseline migration (2026-08-06) — FLPR production/diagnostic split, population 30 → 33

R8 moved the acceptance machinery out of the core FLPR cpuapp files
(`flpr_ring_mgr.c`, `flpr_handshake.c`) and the FLPR image
(`src/flpr/main.c`) into explicit configurable modules
(`src/flpr_acceptance.c` under `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`;
`src/flpr/acceptance.c` under `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS`),
generalized the shared reset/stall ACK correlation into
`src/flpr_control_ack.c` (one owner), and moved the gate orchestration
out of `flpr_acceptance_shell.c`.  The migration candidate was
generated on the clean implementation commit `9e5d82a` via
`scripts/test-coverage.sh --output /tmp/r8-cov-candidate3
--write-baseline /tmp/r8-baseline-candidate3.json` and committed as
`tests/coverage-baseline.json` (`54a6b8e`).  Tool versions unchanged:
**gcovr 8.4 / gcov (GCC) 14.3.0**.

Split aggregate (old `flpr_ring_mgr` + `flpr_handshake` +
`flpr_acceptance_shell` vs the six replacement files including the new
`src/flpr_acceptance.c`, `src/flpr_control_ack.c`, and
`src/flpr/acceptance.c`):

| metric | old 3-file | R8 candidate (6 files) |
|--------|-----------|------------------------|
| lines | 1233/1470 (83.9%) | **1477/1707 (86.5%)** |
| branches | 440/711 (61.9%) | **523/830 (63.0%)** |
| functions | 56/56 | **99/99** |

Every unchanged file remains at or above its committed record (verified
programmatically — zero decreases); zero-hit enforcement clean
(289/289).  New-file records: `src/flpr_acceptance.c` 443/647 L,
121/293 B, 28/28 F; `src/flpr_control_ack.c` 94/95 L, 25/32 B, 9/9 F;
`src/flpr/acceptance.c` 143/143 L, 14/18 B, 18/18 F (FLPR-image
acceptance now direct-tested natively via the injected send/wake
dependency table — no longer hardware-only).  The gate bodies'
FLPR-dependent branches (gates 1–6 drain paths, the 60 s gate-1 test_run
floor) stay hardware evidence, exactly as the R4 shell gate branches
were fake-driven at 37 %: the real-module gates are proven by the
hardware hang/stall gates and this phase's nRF54L15 runs — no covered
live behavior was deleted.  Aggregate totals:

| metric | committed (30 files) | R8 candidate (33 files) |
|--------|----------------------|-------------------------|
| lines | 3777/4164 (90.7%) | **4024/4402 (91.4%)** |
| branches | 1611/2237 (72.0%) | 1695/2356 (71.9%) |
| functions | 246/246 | **289/289** |

Canonical enforcement on the clean R8 baseline commit `54a6b8e`:
**51 PASS / 0 FAIL / 51 TOTAL** (31 twister + 5 exec-only + 12 Python +
coverage + matrix + BSim Stage 1, pins unchanged).

## R6 baseline migration (2026-08-05) — receive-pipeline split, population 29 → 30

R6 extracted the app audio receive/session state and the
decode/conceal/volume/push mechanics from `src/bt_bap.c` (integration-only,
excluded from the numeric population) into the new direct
`src/audio_stream_session.c`, and narrowed
`stream_lifecycle_sink_configured()` to slot occupancy only.  The
migration candidate was generated on the clean implementation commit
`3c7396a` via `scripts/test-coverage.sh --write-baseline
/tmp/r6-baseline-candidate.json`, inspected, and committed as
`tests/coverage-baseline.json`.  Tool versions unchanged: **gcovr 8.4 /
gcov (GCC) 14.3.0**, recorded in the baseline.

**Population 29 → 30** is a deliberate provenance change: the receive
mechanics moved from excluded integration `bt_bap.c` into included direct
`audio_stream_session.c`, so no old aggregate comparison against the
excluded file is possible.  The new file's direct record:
`audio_stream_session.c` **274/292 lines, 131/192 branches, 25/25
functions** (the 4 CONFIG_ZTEST lease accessors are GCOVR-excluded).

Every other file remains **at or above** its committed record (verified
programmatically across lines/branches/functions — zero ratio decreases).
`stream_lifecycle.c` (62/62 L, 34/36 B, 7/7 F) is unchanged: the removed
`chan_count` storage was unmeasured dead scalar space, and the R6
occupancy rewrite keeps every branch.  Aggregate:

| metric | committed (29 files) | R6 candidate (30 files) |
|--------|----------------------|-------------------------|
| lines | 3503/3872 (90.5%) | **3777/4164 (90.7%)** |
| branches | 1480/2045 (72.4%) | **1611/2237 (72.0%)** |
| functions | 221/221 | **246/246** |

Zero-hit enforcement stays clean (246/246 functions in the numeric
population; the session suite exercises every production session
function).  Canonical enforcement on the clean R6 baseline commit:
**49 PASS / 0 FAIL / 49 TOTAL** (29 twister + 5 exec-only + 12 Python +
coverage + matrix + BSim Stage 1, pins unchanged).

## P1 baseline migration (2026-08-06) — pairing-mode transition owner, population 33 → 34

P1 added the portable pairing-mode transition owner
`src/pairing_mode.c` (direct suite `tests/unit/pairing_mode`, 32
tests).  The migration candidate was generated on the clean
implementation commit `14be974` via
`scripts/test-coverage.sh --write-baseline /tmp/p1-baseline-candidate.json`
(`--output /tmp/p1-cov-candidate --clean-output`), inspected, and
committed as `tests/coverage-baseline.json` (byte-exact copy).
Tool versions unchanged: **gcovr 8.4 / gcov (GCC) 14.3.0**, recorded
in the baseline.

**Population 33 → 34** is a deliberate provenance change: the new
production source is directly covered by its own suite.  No per-file
regression (verified programmatically across lines/branches/functions —
zero decreases on every unchanged file; the committed baseline record
for every other file stays at or above its pre-P1 value).  New-file
record: `pairing_mode.c` **344/405 lines, 160/226 branches, 39/39
functions** (the `PAIRING_MODE_TEST` state-reset seam is
GCOVR-excluded and never enters the numeric population).  Aggregate:

| metric | committed (33 files) | P1 candidate (34 files) |
|--------|----------------------|-------------------------|
| lines | 4024/4402 (91.4%) | **4368/4807 (90.9%)** |
| branches | 1695/2356 (71.9%) | **1855/2582 (71.8%)** |
| functions | 289/289 | **328/328** |

Zero-hit enforcement stays clean (328/328 functions in the numeric
population; the pairing suite exercises every production function —
the only zero-execution record is the GCOVR-excluded seam variant).
No covered live behavior was deleted; the denominator increase is
exactly the new file's measured lines/branches.
