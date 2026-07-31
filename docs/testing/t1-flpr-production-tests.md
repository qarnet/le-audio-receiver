# T1 — FLPR production-source tests: evidence

Status: T1 implementation complete, full gate + builds pass (see results).

Phase T1 replaces false-confidence FLPR runtime, ring-manager, and
handshake tests with native_sim suites that compile and execute the real
production implementations.  Wire formats, hardware behavior, memory
layout, and public production APIs stay stable except for the small
corrections listed below, all proven by the new tests.

## Production sources linked by each suite

| Suite | Production sources compiled and executed |
|-------|-------------------------------------------|
| `tests/unit/flpr_runtime` | `src/flpr_runtime.c` (real nRF54 restart body), `src/flpr_ring.c` (real `flpr_ring_crc32`), `src/flpr_cache.c` (native barrier branch) |
| `tests/unit/flpr_ring_mgr` | `src/flpr_ring_mgr.c`, `src/flpr_ring.c`, `src/flpr_cache.c` |
| `tests/unit/flpr_handshake` | `src/flpr_handshake.c` (real IPC endpoint + receive path via the fake backend) |

No copied restart/reset/produce/consume/callback algorithm remains primary
proof in any of the three suites.

## Test-hook architecture

native_sim has no MMU or address translation: dereferencing the fixed
devicetree `reg` addresses (VPR registers, execution SRAM `0x20030000`,
shared rings `0x2002C000..0x20030000`) is unsafe.  Each suite therefore
replaces only the *memory/transport/HAL inputs* of the production source
with host storage, keeping every state transition in production code:

- **Runtime** (`FLPR_RUNTIME_NATIVE_TEST`): test-owned `NRF_VPR_Type`
  storage (shadow-HAL pattern), source and execution byte arrays of equal
  fixed size, and hooks for busy wait, sleep, uptime, cache flush, DSB/ISB
  barriers, after-copy fault injection, and an ordered 16-event log emitted
  by the production restart body.  All production fixed-address
  BUILD_ASSERTs are retained outside test mode.  A dedicated helper thread
  provides genuine mutex-busy behavior (Zephyr mutexes are recursive for
  the owning thread).
- **Ring manager** (`FLPR_RING_MGR_NATIVE_TEST`): the DT ring pointers are
  replaced by two aligned host arrays of `FLPR_RING_TOTAL_SIZE` each and
  the cycle counter is test-controlled.  The production static IPC
  handlers (`on_ring_reset_ack`, `on_ring_consumer`, `on_ring_test_report`,
  `on_ring_stall_ack`) run through the handlers captured by the handshake
  mock; the mock also offers synchronous ACK echo for reset/stall messages
  so coordinated reset and stall flows are deterministic.  Ring contents
  are arranged with the real `flpr_ring` APIs.
- **Handshake** (`FLPR_HANDSHAKE_NATIVE_TEST`): a fake IPC service backend
  (pattern from `zephyr/tests/subsys/ipc/ipc_service/` in NCS v3.3.0)
  implements `struct ipc_service_backend` via `DEVICE_DT_INST_DEFINE` with
  a test-local binding and `ipc0` node; production uses the real Zephyr
  `ipc_service_open_instance`/`register`/`deregister`/`send` APIs.  The
  backend captures the endpoint config, invokes the production callbacks,
  records sent messages, and exposes open/register/deregister/send return
  controls, optional automatic bound callback, arbitrary incoming
  messages, and a send-block mode.  Heartbeat scheduling is routed through
  hooks: the READY-triggered async start is recorded but never submitted
  (so the system workqueue never races the single-threaded ztest runner),
  while the per-iteration reschedule is genuinely scheduled at the
  documented interval and canceled by test teardown.

Test-only compile definitions (`FLPR_RUNTIME_NATIVE_TEST`,
`FLPR_RING_MGR_NATIVE_TEST`, `FLPR_HANDSHAKE_NATIVE_TEST`) have no
`CONFIG_` prefix and are applied only by the test CMakeLists; production
builds contain no test symbols or host arrays.

## Copied/stub tests removed or replaced

- `tests/unit/flpr_runtime`: the old suite compiled the non-nRF54 stub
  branch of `src/flpr_runtime.c` and tested copied DMCONTROL constants and
  enum structure.  Replaced by 21 tests executing the real restart body.
  The `mock_flpr_deps.{c,h}` stub-of-everything files were removed; the
  shadow VPR HAL now writes through to the register struct so production
  readbacks are real.
- `tests/unit/flpr_ring_mgr`: the old suite tested a separate copied model
  with replicated reset/notification logic (no production source).
  Replaced by 53 tests executing `src/flpr_ring_mgr.c` plus real ring code.
- `tests/unit/flpr_handshake`: the old suite tested header-only protocol
  helpers.  Those cases duplicate the `flpr_protocol` suite and were
  removed; replaced by 43 tests executing the production receive path
  through the fake backend.

## Behavior defects fixed (proven by new tests)

1. `flpr_runtime_restart()` now sets `failed_stage = FLPR_STAGE_DISCONNECT`
   before disconnecting, so a second restart whose disconnect fails no
   longer retains the previous `SUCCESS` stage.
2. `failed_stage = FLPR_STAGE_STOP` is now set before stopping CPURUN,
   then advances to `ASSERT_RESET`; the stage enum stays meaningful even
   though the HAL stop is void.
3. `max_duration_ms` now includes failed attempts (it documents the
   longest restart, successful or not); `total_duration_ms` already did.
4. `flpr_runtime.h` and the runtime module comment no longer claim that
   `flpr_runtime_restart()` rejects an active offload stream — automatic
   recovery calls it while offload is RECOVERING, and the shell owns its
   separate ACTIVE guard.  No internal active guard was added.
5. Stale "pulse reset" wording replaced with the held-reset sequence
   matching the code (assert → prepare → release as final launch edge).
6. `flpr_handshake_send_msg()` now routes through the internal
   `send_msg()`, so send failures increment `err_send` exactly as the
   header documents.
7. `flpr_handshake.h` no longer claims ring handlers run "under spinlock";
   production dispatches them from IPC receive context without holding
   `flpr_lock` (proven by callback lock probes under
   `CONFIG_SPIN_VALIDATE`).

Lifetime-counter preservation across unbind (ready_count, reboot_count,
err_*, rx_missed_total, last epoch) was already implemented and is now
locked in by tests.

## Characterized behaviors (documented, not changed)

- `flpr_ring_mgr_consume_asrc_result()` copies the payload before the CRC
  check, so a CRC failure leaves `result->output_frames == 0` and result
  fields untouched while the output buffer may carry the payload.  The
  header's "UNTOUCHED" wording is inaccurate for the CRC case; callers
  must key off `output_frames`.  Recorded as a documentation/refactor
  review candidate.
- `flpr_ring_mgr_set_consume_cb()` is a documented no-op.  Unchanged in
  T1; recorded as a refactor-review candidate.
- `k_sem_take(K_NO_WAIT)` returns `-EBUSY` (not `-EAGAIN`); timeout
  waits return `-EAGAIN`.  Tests assert the actual values.
- `flpr_handshake_wait_new_ready()`'s "already-new epoch" fast path
  succeeds without a semaphore token even if the READY_ACK send failed
  (epoch change is the primary signal).  The semaphore path is only given
  after ACK success.

## Hardware-only gaps (unchanged)

- Physical cache coherence and FLPR entry-point behavior remain
  hardware-only.  native_sim `flpr_cache.c` coverage is API/barrier-call
  proof only (the native branch uses `atomic_thread_fence`); the
  production nRF54L15 branches use ARM DMB/DSB and RISC-V fences.
  `src/flpr/main.c` (FLPR firmware) remains hardware-only — no RISC-V
  simulator infrastructure exists.
- No hardware claim is made from any native_sim result.

## Exact suite results (thomas-workstation, T1 commit)

Focused suites (west build -t run, native_sim/native/64):

| Suite | Result |
|-------|--------|
| `tests/unit/flpr_runtime` | 21 PASS / 0 FAIL |
| `tests/unit/flpr_ring_mgr` | 53 PASS / 0 FAIL |
| `tests/unit/flpr_handshake` | 43 PASS / 0 FAIL |

Full gate (`./scripts/test-all.sh`): see T1 commit evidence in STATUS.md.

Builds: `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — all pass on
the T1 commit.

T1 suite count total: 21 + 53 + 43 = 117 tests across the three suites,
all executing production source.

## Refactor-review candidates recorded

- `flpr_ring_mgr_set_consume_cb()` unused no-op (T1B).
- `flpr_ring_mgr_consume_asrc_result()` CRC-vs-payload-copy ordering and
  the header "UNTOUCHED" wording (T1B).
- `test_flpr_crc_err` internal counter is updated by the 0x00 report
  subtype but not exported in `struct flpr_ring_status` (T1B).
- The boards/`<board>.overlay` auto-discovery does not match the
  `native_sim/native/64` variant board string in NCS v3.3.0; the handshake
  suite sets `DTC_OVERLAY_FILE` explicitly in CMakeLists (T1C).
