# R8 results — FLPR production/diagnostic boundary

Accepted: 2026-08-06.  Start commit `1d9775e` (R7 G3 docs acceptance;
worktree clean); handoff commit `42e59c9`; implementation + tests
`68f4003`; test-gap fixes `b443156`, `9e5d82a`; baseline migration
`54a6b8e`; matrix-schema fix `35bc635`; docs acceptance commit (this
document's commit).  No production BAP behavior, BSim hash/count,
wire-protocol value, ring ABI value, report subtype, stall packing,
ring consumer contract, or offload behavior changed.

## Goal met

Core FLPR cpuapp files (`flpr_ring_mgr.c/.h`, `flpr_handshake.c/.h`)
and the FLPR-image `src/flpr/main.c` contain production runtime only;
acceptance machinery is explicit and configurable:

- **cpuapp**: `src/flpr_acceptance.c/.h` (ring throughput test, stale
  produce, producer stall, FLPR stall/timed-stall + ACK correlation,
  report aggregation, acceptance status, handshake stress, fault hang,
  gates 1–6) gated by the R4 `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`;
  core produce/consume/consumer paths invoke narrow config-gated
  acceptance hooks for the diagnostic counters.
- **FLPR image**: `src/flpr/acceptance.c/.h` (RING_TEST_START/STOP
  report cascade, RING_STALL timer + ACK echo, STRESS_PING/PONG,
  FAULT_HANG ACK-before-spin, diagnostic note hooks) gated by the new
  `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` (src/flpr/Kconfig app root,
  no SHELL dependency; enabled in src/flpr/prj.conf).  Config-off FLPR
  builds contain no acceptance link references and every not-consumed
  message falls to the existing `err_unknown` branch.
- **Shared ACK correlation**: ONE owner — `src/flpr_control_ack.c/.h`
  (production-neutral engine), used by the core coordinated reset and
  the acceptance stall transaction; 16-bit token overflow, stale/late
  ACK rejection, timeout/disarm, and remote-restart reset semantics
  preserved exactly.
- **Handshake**: production slot (reset ACK + consumer) and diagnostic
  slot (report/stall-ack/pong/hang) registration split; snapshot-under-
  lock / invoke-outside-lock unchanged; stress + fault-hang blocking
  state moved out.
- **Shells**: parsing/printing/registration only; gates 1–6 moved to
  the acceptance module with byte-identical output through a
  severity-aware print sink (shell_print/warn/error preserved).

## API ownership before/after

| API | Before | After |
|---|---|---|
| `flpr_ring_mgr_test_run(_rate)` | core | `flpr_acceptance_test_run(_rate)` |
| `flpr_ring_mgr_flpr_stall(_timed)` | core | `flpr_acceptance_flpr_stall(_timed)` |
| `flpr_ring_mgr_flpr_stall_acked` | core | `flpr_acceptance_flpr_stall_acked` |
| `flpr_ring_mgr_stall_producer` | core | `flpr_acceptance_stall_producer` |
| `flpr_ring_mgr_produce_stale_test` | core | `flpr_acceptance_produce_stale_test` |
| `on_ring_test_report` / `on_ring_stall_ack` | core | acceptance diag handler |
| `flpr_handshake_stress` | handshake | `flpr_acceptance_stress(_active/_snapshot)` |
| `flpr_handshake_send_fault_hang` | handshake | `flpr_acceptance_send_fault_hang` |
| stress/hang state + PONG/HANG-ACK dispatch | handshake switch | diagnostic handler slot |
| Gates 1–6 | `flpr_acceptance_shell.c` | `flpr_acceptance_run_gates` |
| reset/stall ACK correlation | core private engine ×2 | `flpr_control_ack` (one engine) |
| FLPR-image test/stall/stress/hang handlers | `src/flpr/main.c` | `src/flpr/acceptance.c` |

The core `flpr_ring_mgr` retains init, coordinated_reset, remote_
restarted, typed ASRC produce/consume, notify_producer, wait_consume
(used by audio_offload), production reset/consumer handlers, and the
production status fields (initialized/epoch/ring occupancy/notify/sem).
`flpr_rate_limit_target_ms` stays in `flpr_ring_mgr.h` (compiled by the
unchanged flpr_protocol suite).

## Config / build contract parity

- `src/flpr/Kconfig` — FLPR-image app Kconfig root with
  `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` (bool, default n, no SHELL
  dependency) + `source "Kconfig.zephyr"`.
- `src/flpr/prj.conf`: `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y`;
  `src/flpr/CMakeLists.txt` wires `acceptance.c` via
  `zephyr_sources_ifdef`.
- Root CMake wires `src/flpr_acceptance.c` under
  `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` and `src/flpr_control_ack.c`
  on every nRF54L15 build (the engine serves the production reset ACK —
  a handoff correction: the engine is production-neutral and must
  compile whenever flpr_ring_mgr.c does; the handoff's original
  acceptance-only wiring was corrected in the implementation commit).
- Build contract 76 → **79**: `5340-029` (app acceptance not enabled on
  nRF5340), `54l15-035` (app acceptance enabled), `54l15-036` (FLPR
  image acceptance enabled) — parity proven from the resolved configs:
  `build/nrf54l15/le-audio-receiver/zephyr/.config` has
  `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y`,
  `build/nrf54l15/flpr/zephyr/.config` has
  `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y`; the nRF5340 app config leaves
  the symbol unset.  `check-build-contract.py` → **79 assertions, 0
  failed, PASSED**.

## Wire / ABI proof

`FLPR_PROTOCOL_VERSION=4`, `FLPR_RING_ABI_VERSION=4`, every `FLPR_MSG_*`
value, `struct flpr_msg` size/offsets, `FLPR_STALL_PACK/MASK/DURATION`
(+ compile-time asserts), report subtypes 0x00/0xD1/0xD2/0xD3/0xD4,
`RING_CONSUMER` seq/data contract, 480-input/481-capacity: all unchanged
(no edits to `flpr_protocol.h` / `flpr_ring.h`; `src/flpr/acceptance.c`
adds BUILD_ASSERTs pinning the protocol values).  `tests/unit/flpr_
protocol` (62) and `tests/unit/flpr_ring` (51) pass bit-exact.

## Tests

New direct suites (Twister children 49 → 51):

- `tests/unit/flpr_acceptance` (48 tests) — compiles the REAL
  `src/flpr_acceptance.c` + `src/flpr_control_ack.c` +
  `src/flpr_ring_mgr.c` + `src/flpr_ring.c` + `src/flpr_cache.c` against
  a faithful handshake mock implementing both handler slots; covers
  test_run (pre-init `-EAGAIN`, active `-EBUSY`, bounded success,
  rate-limited sleep, global timeout, input-full break, notify-failure
  continue, START/STOP wire), stalls (packing, validation, send
  failure, timeout, wrong/exact ACK, late-ACK-by-sequence retry, token
  boundary + remote-restart reset, stall-ACK sem drain), stale produce,
  report aggregation (all subtypes + unknown ignored), producer-stall
  backpressure, full/stale/CRC/payload/latency accounting, consumer
  notification FLPR-block recording, internal context accessors,
  stress (match/stale/future/inactive, rejects, clamp via blocking
  send, send failure, timeout, late PONG), fault hang (ACK, send
  failure, timeout), gate runner prep-reset-failure.  The all-gates-PASS
  tail and the FLPR-dependent drain paths remain hardware evidence
  (same honest boundary as the R4 shell test).
- `tests/unit/flpr_acceptance_flpr` (8 tests) — compiles the REAL
  `src/flpr/acceptance.c` via its injected send/wake dependency table:
  report cascade exact packing, stall persistent/timed ACK echo +
  timer auto-clear, clear + ring-reset state reset, STRESS_PING/PONG
  echo, FAULT_HANG ACK-before-pending ordering, unknown-message false.

Updated suites: `flpr_ring_mgr` (28 tests — production reset/ASRC/
notify/wait/remote-restart retained; acceptance-specific tests moved),
`flpr_handshake` (34 tests — split-slot dispatch incl. unregistered-diag
silent drop; stress/hang tests moved), `audio_shell_nrf54` (43 tests —
fake deps re-pointed to the `flpr_acceptance_*` surface; the 42
pre-existing output tests unchanged in their assertions; one added gate-
sink test proves `shell_gate_print` severity mapping).  Unchanged
suites pass bit-exact: flpr_protocol 62, flpr_ring 51,
flpr_audio_process 47, flpr_runtime 21, audio_offload 30,
offload_asrc 31, offload_asrc_verify 12, build_contract 33.

## Coverage migration (30 → 33)

Candidate generated on clean `9e5d82a` via
`scripts/test-coverage.sh --output /tmp/r8-cov-candidate3
--write-baseline /tmp/r8-baseline-candidate3.json` (gcovr 8.4 / gcov
GCC 14.3.0).  Split aggregate (old `flpr_ring_mgr` + `flpr_handshake` +
`flpr_acceptance_shell` vs the six replacement files incl. new
`flpr_acceptance.c`, `flpr_control_ack.c`, `src/flpr/acceptance.c`):

| metric | old 3-file | new 6-file |
|--------|-----------|-----------|
| lines | 83.88% (1233/1470) | **86.53% (1477/1707)** |
| branches | 61.88% (440/711) | **63.01% (523/830)** |
| functions | 100% (56/56) | **100% (99/99)** |

Every unchanged file remains at or above its committed record (verified
programmatically — zero decreases); zero-hit functions 289/289.  Overall
population: lines 3777/4164 → 4024/4402, branches 1611/2237 → 1695/2356,
functions 246/246 → 289/289.  The gate bodies' FLPR-dependent branches
stay hardware-evidenced (the R4 shell gates were 37% branch-covered via
fakes; the real-module gates are proven on hardware by the hang/stall
gates + this phase's hardware runs — no covered live behavior deleted).
Baseline committed `54a6b8e`; canonical enforcement passes on it.

## G1 (canonical)

`./scripts/test-all.sh` on clean `35bc635` → **51 PASS / 0 FAIL /
51 TOTAL**, exit 0 (31 twister + 5 exec-only + 12 Python + coverage +
matrix + BSim Stage 1; log `/tmp/r8-gate2.log`).  Coverage child:
population 33 baseline enforcement 0 errors.  Matrix child: 0 errors,
0 notes.  BSim Stage 1: all 17 scenarios strict-checked; existing pins
byte-identical (mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms `0xBAE24F7E`,
reconnect fresh mono oracle, `duplicate_release_10ms` total=56).
`fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
zero new/actionable compiler warnings (only the documented NCS v3.3.0
diagnostics; the FLPR-image `UART_CONSOLE` assigned-but-got warning is a
pre-existing nRF54L15 cpuflpr board-defconfig default vs the board conf's
`CONFIG_CONSOLE=n` — board files untouched by R8).  Build contract
79/79.  `git diff --check` clean.

## Hardware (nRF54L15 only; planned non-destructive flash; no recovery)

Evidence: `/tmp/r8-hw/` (`MANIFEST.md` + `SHA256SUMS`; raw logs for
boot, every stream, hang/stall gates, status/stress/ring-test commands,
probes).  Probes identical to baseline: XIAO CMSIS-DAP 8EE9B3FF DPIDR
0x6ba02477 PART 0x00054b15 VARIANT AAC0; central dongle
C0:AA:BB:CC:DD:EE settings `powered le secure-conn cis-central`.
Console captured before reset; resolved cpuapp + FLPR acceptance
configs verified `y`; production acceptance-enabled image left flashed
(same config) with a clean boot.  No audibility claim.

| Row | Receiver result | Offload |
|-----|-----------------|---------|
| Mode A fresh 120 s | streamed; offload counters: **submit=12022 success=12022 fallback=0 busy=0**; faults timeout/full/stale/seq/frame/crc/payload=0; verify=0 | ACTIVE gen=3 |
| Mode B fresh 120 s (stereo) | Stream[0] SDUs=10771 decoded=24050 plc=2508 **decode_err=0 i2s_underrun=0 stream_reset=0** | **submit=12025 success=12025 fallback=0**; all faults 0; verify=0 |
| flpr hang gate Mode A | 16/16 PASS (ACK→151 ms, ACTIVE→852 ms, restarts=1 fails=0, probation cleared, epochs changed, exhaustion 0, zero verify/crc/seq/frame/state faults) | recovery attempts=1 |
| flpr hang gate Mode B | 16/16 PASS (same checks) | recovery attempts=1 |
| flpr stall gate | GATE PASSED (timed 60 ms, exact ACK line, fallback bump, no exhaustion) | — |
| `flpr status` / `flpr ring status` | merged core+acceptance status; Test (done) + Stall (last) mask=0x01 duration=60 ms lines correct | — |
| `flpr stress 5` | Sent=5 Recv=5 Timeout=0 Stale=0 Mismatch=0 ErrSend=0 (log tag `flpr_acc` = moved module) | — |
| `flpr ring test 50` | Sent=50 Recv=50 CRC_Err=0 Payload_Err=0 PASS (FLPR report aggregation) | — |

Environmental note: the first Mode A/B raw-HCI connect attempts returned
controller-level `0x0d` (Limited Resources) because BlueZ held a live
bonded connection to the E83 receiver (E8:54:F0:E0:D9:42) occupying the
dongle's connection budget; removing that bond freed the slot and every
subsequent connect/stream/gate ran clean.  A mid-debugging reset also
produced one post-reset Mode B run without offload activation
(gen=0); the subsequent clean re-run activated offload normally
(submit=12025).  No firmware change; both anomalies are environment/
procedure artifacts, documented in the raw logs.

## Deviations / notes

- The handoff's `flpr_control_ack.c` wiring was corrected during
  implementation: the engine is production-neutral and compiled on every
  nRF54L15 build (the core coordinated reset uses it), not only under
  the acceptance config.  The handoff document's Config/build section
  was updated in the implementation commit.
- The acceptance gate bodies (gates 1–6) are natively reachable only up
  to the prep-reset-failure path (gate 1's test_run has a 60 s timeout
  floor and gates 2–5's drains need a live FLPR worker); the all-gates
  PASS tail stays hardware evidence, matching the R4 shell test's honest
  boundary.  This is the sole structural branch-coverage difference vs
  the fake-driven R4 shell, documented in the coverage migration table.
- `audio_shell_nrf54` grew 42 → 43 tests (one added gate-sink output
  test); the 42 pre-existing output tests are unchanged in their
  assertions.
- The FLPR-image `UART_CONSOLE` Kconfig warning is pre-existing (NCS
  nRF54L15 cpuflpr board defconfig sets `CONFIG_UART_CONSOLE=y` while
  the board conf disables `CONFIG_CONSOLE`); recorded in STATUS.md's
  build-warning diagnostics.

## Non-scope items untouched

R9–R10; release-default-off policy; protocol/ABI changes; 360-frame FLPR
offload; BSim pin changes; E83 hardware rows; destructive hardware
actions.
