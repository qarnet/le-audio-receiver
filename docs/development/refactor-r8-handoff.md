# R8 handoff — FLPR production/diagnostic boundary

Start commit: `1d9775e` (R7 G3 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before implementation; the implementation must match it and the
results document (`docs/development/refactor-r8-results.md`) must record
what actually happened.

## Goal / invariant

Core FLPR cpuapp files (`flpr_ring_mgr.c/.h`, `flpr_handshake.c/.h`) and
the FLPR-image files (`src/flpr/main.c`) contain production runtime only;
acceptance machinery is explicit and configurable.  Direct tests still
compile the real production sources.  Cpuapp acceptance is gated by the
existing `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` (R4), FLPR-image acceptance
by the new `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS`, and both are proven in
lockstep for the current lab build.  Wire protocol v4 / ring ABI v4 /
every `FLPR_MSG_*` value / `struct flpr_msg` layout / stall packing /
report subtypes / ring consumer contract / 480-input / 481-capacity are
byte-for-byte unchanged.

**No copied algorithms.**  The shared reset/stall ACK correlation engine
is generalized once into a production-neutral helper
(`src/flpr_control_ack.c/.h`) compiled and tested directly; no core file
and no acceptance file duplicates it.  The FLPR-side acceptance module is
built around an injected send/wake dependency table so its message/state
logic is direct-tested on native_sim (it stays hardware-free by design;
no RV32/IPC glue lives in it).

## Config / build wiring

### New FLPR-image app Kconfig root: `src/flpr/Kconfig`

The FLPR image (sysbuild `APPLICATION flpr SOURCE_DIR ${APP_DIR}/src/flpr`)
currently has no app Kconfig.  Add the standard app-Kconfig pattern:

```kconfig
mainmenu "FLPR acceptance-aware image"

config FLPR_ACCEPTANCE_DIAGNOSTICS
	bool "FLPR acceptance diagnostics"
	default n
	help
	  Compile the FLPR-image acceptance handlers (RING_TEST_START/STOP
	  report cascade, RING_STALL/timed-stall timer + ACK echo,
	  STRESS_PING/PONG, FAULT_HANG ACK-before-spin state, and the
	  diagnostic counters/hooks invoked from production ring
	  processing) into the FLPR firmware.  Enabled only in
	  src/flpr/prj.conf for the current nRF54L15 acceptance build;
	  the release default is off (separate work).  The cpuapp side of
	  the acceptance harness is gated by the separate
	  CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS symbol.
	  This option must NOT depend on SHELL (FLPR images build with
	  CONFIG_SHELL=n).

source "Kconfig.zephyr"
```

Verified against the NCS v3.3.0 app-Kconfig pattern (`source
"Kconfig.zephyr"` at the end; `mainmenu` first).  The symbol has **no
`depends on SHELL`** (FLPR `prj.conf` sets `CONFIG_SHELL=n`).

### `src/flpr/prj.conf`

Add `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y` (current acceptance build).

### `src/flpr/CMakeLists.txt`

```cmake
zephyr_sources_ifdef(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS acceptance.c)
```

`main.c` references acceptance symbols only under
`#if defined(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS)` — config-off builds
contain no acceptance link references and every not-consumed message
(`RING_TEST_START/STOP`, `RING_STALL`, `STRESS_PING`, `FAULT_HANG`) falls
to the existing `default: cpuapp.err_unknown++` branch (current
unknown/error behavior, no link refs).

### Root `CMakeLists.txt`

`zephyr_sources_ifdef(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS
src/flpr_acceptance.c)` next to the existing
`flpr_acceptance_shell.c` line.  Add
`zephyr_sources_ifdef(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS
src/flpr_control_ack.c)` — the engine is compiled into the app only with
the acceptance build so production (config-off) firmware carries no
acceptance machinery.  The engine is a small production-neutral module;
it is compiled by the direct suites in every case that matters.

### Root `Kconfig`

`AUDIO_ACCEPTANCE_DIAGNOSTICS` stays as-is (R4 shell/orchestration gate,
`depends on SOC_NRF54L15 && SHELL`, enabled only in
`boards/nrf54l15dk_nrf54l15_cpuapp.conf`).  No new cpuapp symbol.

### Boot wiring

`flpr_acceptance_init()` (cpuapp: registers the diagnostic message
handler with the handshake module + initializes the stall-ACK control
state) is called from `src/main.c` `platform_init()` under
`#if defined(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS)`, after
`flpr_handshake_init()`.  This is boot wiring, not core coupling; the
handshake/ring production handlers are still registered by
`flpr_ring_mgr_init()` (audio_offload init/prep), exactly as today.

### Build contract — exact parity checks (`scripts/check-build-contract.py`)

Add three assertions, atomically with fixture updates in
`tests/unit/build_contract/test_build_contract.py`:

- `5340-029` — app `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` **not** enabled
  (nRF5340 has no FLPR acceptance commands).
- `54l15-035` — app `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y` (current lab
  build).
- `54l15-036` — FLPR image `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y`
  (current lab build; same build as `54l15-035` — lockstep proof).

Contract total 76 → **79**.  Fixture updates: `APP5340_CONFIG` gets
`# CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is not set`, `APP54_CONFIG` gets
`CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y`, `FLPR_CONFIG` gets
`CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y`, plus one failure-path test each
for `5340-029` (parity inversion) and `54l15-036` (FLPR off).  The
resolved-config proof for G1 is `check-build-contract.py` against the
real `build/nrf5340` and `build/nrf54l15` trees.

## Exact module ownership

### 1. Core `src/flpr_ring_mgr.c/.h` retains (production only)

- `flpr_ring_mgr_init()` — registers the **production** ring handlers
  (reset ACK, consumer) via the split handshake registration; initializes
  rings/semaphores/epoch; repeated-call no-op semantics unchanged.
- `flpr_ring_mgr_coordinated_reset()` — reset ACK correlation via the
  shared control-ACK engine; `-EOVERFLOW` on reset token-space exhaustion.
- `flpr_ring_mgr_reset()` / `flpr_ring_mgr_reset_locked()`.
- `flpr_ring_mgr_remote_restarted()` — calls
  `flpr_control_ack_reset_session()` (clears **all** registered control
  instances incl. the acceptance stall instance) + drains sems + re-inits
  headers + resets production diag counters.
- `flpr_ring_mgr_notify_producer()`, `flpr_ring_mgr_wait_consume()`
  (stays core: `audio_offload.c` calls it), typed ASRC
  produce/consume, plain produce/consume.
- Production IPC handlers `on_ring_reset_ack` (via engine) and
  `on_ring_consumer` (epoch gate + consume_sem give + **config-gated**
  `flpr_acceptance_note_flpr_blocks(msg->seq)` so the FLPR-reported block
  count stays an acceptance field).
- Production status fields in `struct flpr_ring_status`: `initialized`,
  `epoch`, both ring occupancy/epoch blocks, and the notify/sem
  diagnostics (`notify_sent`, `notify_err`, `sem_gives`, `sem_takes`,
  `stale_notify`, `sem_drained`).  `diag_sem_takes` remains a live-but-
  never-incremented field exactly as today.
- `flpr_rate_limit_target_ms()` static-inline **stays in
  `flpr_ring_mgr.h`** — `tests/unit/flpr_protocol` compiles it from that
  header and must pass unchanged.
- `FLPR_RING_MGR_NATIVE_TEST` test hooks that stay with production
  concerns: host ring/cycle accessors (implemented in the suite's
  `flpr_ring_mgr_hooks.c`), `test_reset_state`, `test_set_test_active` is
  REPLACED by the acceptance test-mode accessor (below), reset-ACK
  semaphore count, R1 pause barrier hooks, reset token setter, and
  reset-side stale-ACK count.  Stall-side hooks move to the acceptance
  module.

**Removed from core** (moved to `src/flpr_acceptance.c`):
`flpr_ring_mgr_stall_producer`, `flpr_ring_mgr_flpr_stall`,
`flpr_ring_mgr_flpr_stall_timed`, `flpr_ring_mgr_flpr_stall_acked`,
`flpr_ring_mgr_test_run`, `flpr_ring_mgr_test_run_rate`,
`flpr_ring_mgr_produce_stale_test`, `on_ring_test_report`,
`on_ring_stall_ack`, the test/latency/FLPR-reported counters, the
`test_*`/`flpr_*`/`latency_*` status fields.  The public header drops the
acceptance-named declarations; the internal context accessor (below)
replaces them for the acceptance module.

### 2. New cpuapp `src/flpr_acceptance.c/.h`

Owns, moved verbatim in behavior from core + `flpr_acceptance_shell.c`:

- Raw diagnostic produce/consume blocks: `flpr_acceptance_test_run()` /
  `flpr_acceptance_test_run_rate()` (the full event-driven batch loop,
  RING_TEST_START/STOP sends, final drain, `test_active` management,
  `test_blocks_sent`/`test_seq_gaps` bookkeeping; `test_pattern` /
  `test_recv_buf` static buffers move here).
- `flpr_acceptance_produce_stale_test()` — stale-epoch slot production
  into the output ring via the internal context accessor
  (`ring_data_lock` + output ring base), `-EAGAIN` when uninitialized,
  `-EINVAL` on zero stale epoch.
- Producer stall injection: `flpr_acceptance_stall_producer(bool)` and
  `flpr_acceptance_stall_producer_active()` (read by core produce paths,
  config-gated).
- FLPR stall/timed-stall requests + ACK correlation:
  `flpr_acceptance_flpr_stall()`, `flpr_acceptance_flpr_stall_timed()`
  (incl. the zero-mask-with-duration `-EINVAL` and
  `FLPR_STALL_DURATION_MAX` guards), `flpr_acceptance_flpr_stall_acked()`
  — the transaction holds `ring_data_lock` (via the internal accessor)
  through the ACK wait exactly as the current
  `flpr_ring_mgr_stall_internal` does, using the shared control-ACK
  engine's stall instance (`-EOVERFLOW` on stall token-space exhaustion).
- Test-report aggregation: the diagnostic handler's
  `FLPR_MSG_RING_TEST_REPORT` subtype decode (0x00/0xD1/0xD2/0xD3/0xD4)
  into the acceptance FLPR-reported counters.
- Acceptance status: `struct flpr_acceptance_status` with the moved
  fields (`test_active`, `test_blocks_sent/recv`, `test_crc_errors`,
  `test_payload_errors`, `test_seq_gaps`, `test_full_events`,
  `test_backpressure`, `test_empty_events`, `test_stale_events`,
  `test_producer_blocks`, `test_output_full`, `flpr_*` reported
  counters, `latency_*`, `stall_acked`); `flpr_acceptance_get_status()`.
- Diagnostic counter hooks called from core production paths
  (config-gated in core): `flpr_acceptance_note_recv()`,
  `flpr_acceptance_note_full()`, `flpr_acceptance_note_stale()`,
  `flpr_acceptance_note_backpressure()`, `flpr_acceptance_note_crc_err()`,
  `flpr_acceptance_note_payload_err(n)`, `flpr_acceptance_note_latency(v)`,
  `flpr_acceptance_note_flpr_blocks(seq)`, and the test-mode accessor
  `flpr_acceptance_test_active()`.
- Handshake stress + fault-hang orchestration wrappers (moved from
  `flpr_handshake.c`): `flpr_acceptance_stress(count, out)`,
  `flpr_acceptance_stress_active()`, `flpr_acceptance_stress_snapshot(out)`,
  `flpr_acceptance_send_fault_hang(timeout_ms)`.  The stress loop sends
  STRESS_PING through the narrow core `flpr_handshake_send_msg()` seam.
  **Documented delta:** a stress-ping send failure now also increments the
  handshake `err_send` counter (shared transport seam), in addition to the
  stress-local `stress_err_send`.  This is visible only in `flpr status`
  Errors line during an acceptance stress-send-failure; no gate parses it;
  all `flpr stress` summary fields are unchanged.
- Gate 1–6 orchestration: `flpr_acceptance_run_gates(count, ctx, print)`
  — the full body currently inline in `cmd_flpr_ring_acceptance`
  (prep reset + stall clears, gate 1 loopback incl. latency/throughput
  lines, gate 2 producer stall, gate 3 input-consumer stall, gate 4
  output-producer stall, gate 5 stale injection, gate 6 explicit empty,
  PASS/FAIL decisions, `ACCEPTANCE PASSED/FAILED` tail, throughput line).
  Output goes through a severity-aware print callback so the shell
  reproduces byte-identical lines including shell_error/shell_warn color
  paths:
  ```c
  enum flpr_acceptance_print_level { FLPR_ACC_PRINT_NORMAL, FLPR_ACC_PRINT_WARN, FLPR_ACC_PRINT_ERROR };
  typedef void (*flpr_acceptance_print_t)(void *ctx, enum flpr_acceptance_print_level lvl,
                                          const char *line);
  int flpr_acceptance_run_gates(uint32_t count, void *ctx, flpr_acceptance_print_t print);
  ```
  The module formats each line with `vsnprintf` into a bounded buffer
  (256 B — longest gate line is < 100 chars) and hands the finished line
  to the callback; the shell maps NORMAL/WARN/ERROR to
  `shell_print`/`shell_warn`/`shell_error` with `"%s"`.  All gate
  formatting strings move verbatim.
- `flpr_acceptance_init()` — registers the diagnostic handler with the
  handshake module, initializes the stall-ACK semaphore + engine instance
  + acceptance lock/state.  Called from `main.c` platform_init
  (config-gated).
- `FLPR_ACCEPTANCE_NATIVE_TEST` hooks: `flpr_acceptance_test_reset_state()`,
  stall-ACK/hang/stress semaphore counts, `flpr_acceptance_test_set_next_stall_token()`,
  stall-side stale-ACK count.

Lock discipline: one module spinlock `acc_lock` protects acceptance
counters/latency/stall-flag/stress/hang state.  Fixed nesting:
`ring_data_lock` → (`ring_lock` | `acc_lock`) → engine lock.  Engine
functions are leaf (never take `ring_data_lock`/`ring_lock`/`acc_lock`).
Core produce/consume paths call note hooks while holding `ring_data_lock`
(acc_lock is leaf) — no deadlock, no duplicate lock over ring memory.

### 3. New FLPR-image `src/flpr/acceptance.c/.h`

Owns (compiled only under `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS`):

- `RING_TEST_START`/`RING_TEST_STOP` + the 5-report cascade (subtypes
  0x00/0xD1/0xD2/0xD3/0xD4 with exact packing), test-active flag,
  `ring_test_*` counters.
- `RING_STALL` persistent/timed handling: `stall_flags` atomic, one-shot
  `k_timer` timed auto-clear, exact packed-value ACK echo via
  `flpr_control_ack_make`, `diag_timed_stall_*` counters.
- `STRESS_PING` → `STRESS_PONG` echo (seq + cookie).
- `FAULT_HANG` → `FAULT_HANG_ACK` sent **first**, then `hang_pending`
  set (ACK-before-spin), then `deps->wake()`; `flpr_acceptance_hang_pending()`
  read by main.c's loop which performs the `k_busy_wait(1000)` +
  `irq_lock()` + spin (production glue stays in main.c).
- Diagnostic counters/state hooks invoked from production ring
  processing: `flpr_acceptance_note_worker_wake()`,
  `note_consume_ok/empty/stale`, `note_produce_ok/full`,
  `note_notify_rcv()` (from main.c's `RING_PRODUCER` case),
  `note_block_processed()`, `note_crc_error()`, `note_empty_poll()`,
  `note_epoch_stale()`, `flpr_acceptance_test_active()`,
  `flpr_acceptance_stall_flags()`.
- `flpr_acceptance_on_ring_reset()` — called from main.c's
  `ring_reset_with_epoch()` (config-gated): stops the stall timer, clears
  stall flags, resets test/diag/timed-stall counters (exactly the state
  today's reset clears).
- `flpr_acceptance_handle_msg(msg)` → bool — the message dispatcher for
  the five acceptance types; main.c's `ep_received` calls it from its
  default case when configured and counts `err_unknown` only if it
  returns false.
- Dependency table injected at boot by main.c:
  ```c
  struct flpr_acceptance_deps {
      int (*send)(const struct flpr_msg *msg); /* ipc_service_send wrapper */
      void (*wake)(void);                      /* give ring_wake_sem */
  };
  void flpr_acceptance_init(const struct flpr_acceptance_deps *deps);
  ```
  No `ipc_service.h`, no `device.h`, no DT, no `irq_lock` in the module —
  this is what makes it natively compilable.  `k_timer`/`atomic_t`/
  `k_uptime_get_32` are kernel APIs available on native_sim.

### 4. `src/flpr_handshake.c/.h` retains

IPC endpoint lifecycle, READY/READY_ACK/heartbeat/HEARTBEAT_ACK handling,
peer state machine, health transition callback, `send_msg`/
`flpr_handshake_send_msg`, reconnect/waits, and the **split** handler
registration:

```c
/* production: reset ACK + consumer (registered by flpr_ring_mgr_init) */
void flpr_handshake_register_ring_handlers(flpr_handshake_ring_handler_t reset_ack_fn,
                                           flpr_handshake_ring_handler_t consumer_fn,
                                           void *user_data);
/* diagnostic: TEST_REPORT, STALL_ACK, STRESS_PONG, FAULT_HANG_ACK
 * (registered by flpr_acceptance_init; NULL → messages silently dropped,
 * exactly the current inert behavior) */
void flpr_handshake_register_diag_handlers(flpr_handshake_ring_handler_t diag_fn,
                                           void *user_data);
```

`ep_received` keeps the existing per-slot snapshot-under-`flpr_lock` /
invoke-outside-lock semantics for every handler slot.  `RING_RESET_ACK` +
`RING_CONSUMER` route to the production slot; the four diagnostic types
route to the diag slot.  `STRESS_PING`/`READY_ACK` remain handled-inline
allowed no-ops.  Unknown types still increment `err_unknown`.

**Removed from handshake:** `flpr_handshake_stress`,
`flpr_handshake_send_fault_hang`, the stress state machine
(`stress_sem`, `stress_*` counters, `stress_cookie`), the hang ACK state
(`hang_ack_sem`, `hang_ack_received`), and the inline `STRESS_PONG` /
`FAULT_HANG_ACK` switch cases (now dispatched to the diag slot).
`struct flpr_status` keeps its `stress_*` fields as the shared
shell-status contract; `flpr_handshake_get_status()` explicitly zeroes
them (handshake no longer owns stress state — documented in the header).
`FLPR_HANDSHAKE_NATIVE_TEST` hooks drop the stress/hang semaphore
counters (they move to the acceptance module).

### 5. New `src/flpr_control_ack.c/.h` — shared control-ACK engine

Generalization of the current `struct control_ack` + ack primitives in
`flpr_ring_mgr.c`, production-neutral, ONE owner of correlation:

```c
struct flpr_control_ack {
    struct k_sem *sem;
    uint32_t payload;
    uint32_t expected_data;
    uint16_t expected_seq;
    bool armed;
    uint32_t next_token;   /* 1..0xFFFF, no wrap in-session */
    uint32_t stale_count;
};

int  flpr_control_ack_init(struct flpr_control_ack *ctl, struct k_sem *sem);
void flpr_control_ack_begin(struct flpr_control_ack *ctl);   /* disarm + clear + drain */
int  flpr_control_ack_arm(struct flpr_control_ack *ctl, uint32_t expected_data);
void flpr_control_ack_disarm(struct flpr_control_ack *ctl);
void flpr_control_ack_handle(struct flpr_control_ack *ctl, const struct flpr_msg *msg);
int  flpr_control_ack_wait(struct flpr_control_ack *ctl, uint32_t timeout_ms,
                           uint32_t expected_data);
uint32_t flpr_control_ack_payload(struct flpr_control_ack *ctl);
void flpr_control_ack_register(struct flpr_control_ack *ctl);
void flpr_control_ack_reset_session(void); /* disarm + reset tokens + drain sems, all registered */
```

Exact behaviors preserved: first-ACK-while-armed-and-matching wins;
stale/duplicate ACKs counted and ignored (per-instance `stale_count`);
timeout disarms (`-ETIMEDOUT`); wrong-data same-sequence ACK → `-EIO`;
16-bit token overflow → `-EOVERFLOW` until
`flpr_control_ack_reset_session()` (called by
`flpr_ring_mgr_remote_restarted()`); semaphore give outside the lock.
Registration: the engine keeps a small fixed array of registered
instances (reset + stall); `reset_session` iterates it.  Engine state is
protected by the engine's own spinlock (leaf).  `flpr_ring_mgr` registers
the reset instance in `init`; `flpr_acceptance` registers the stall
instance in `flpr_acceptance_init`.

### 6. New internal seam `src/flpr_ring_mgr_internal.h`

Tiny production-neutral accessor compiled into `flpr_ring_mgr.c` (under
both native-test and production builds) so the acceptance module can hold
the same bulk-op lock and reach the raw output ring without duplicating
locks or ring-memory knowledge:

```c
struct k_mutex *flpr_ring_mgr_data_lock(void);
uint8_t *flpr_ring_mgr_input_ring(void);
uint8_t *flpr_ring_mgr_output_ring(void);
```

Under `FLPR_RING_MGR_NATIVE_TEST` these return the host arrays; production
returns the DT-derived addresses.  Used only by `flpr_acceptance.c`
(produce_stale_test, stall transaction) and by nothing else in production
except `flpr_ring_mgr.c` itself.  Included in handoff/matrix/coverage;
external production headers stay clean.

## Entanglement resolution (decisions)

1. **ACK correlation — ONE owner:** the `flpr_control_ack` engine.  No
   duplicated token/arm/handle/wait logic anywhere.  Reset instance owned
   by `flpr_ring_mgr`, stall instance by `flpr_acceptance`; both cleared
   by `remote_restarted()` via `flpr_control_ack_reset_session()`,
   preserving the exact per-type token spaces and the
   `-EOVERFLOW`-until-restart contract.
2. **Handler registration split:** handshake keeps two slots (production
   reset/consumer; diagnostic report/stall/pong/hang).  Acceptance code
   never enters core; core never registers acceptance handlers.
   Snapshot-under-lock / invoke-outside-lock unchanged for every slot.
3. **Status split:** `struct flpr_ring_status` (core) keeps
   initialized/epoch/ring occupancy/notify/sem fields;
   `struct flpr_acceptance_status` (acceptance) owns test/stall/
   FLPR-report/latency fields.  `flpr ring status` (acceptance shell)
   queries both and prints byte-identically.  `flpr status` (production
   shell) merges the stress block from `flpr_acceptance_stress_snapshot()`
   under config.
4. **Diagnostic counters in core paths:** core produce/consume/consumer
   paths call narrow `flpr_acceptance_note_*()` hooks under
   `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` only (the same explicit/
   configurable pattern the FLPR side uses for its hooks).  Config-off:
   no counters, no acceptance symbols in core object files.
5. **Locks:** no new lock over ring memory; `ring_data_lock` remains the
   single bulk-op authority and is reached by acceptance through the
   internal accessor.  Nesting `ring_data_lock` → leaf locks only.
6. **Test seams:** the R1 barrier-pause and reset-token/cycle hooks stay
   in `flpr_ring_mgr.c` under `FLPR_RING_MGR_NATIVE_TEST` (they test core
   production serialization).  Stall-side seams and all diagnostic seams
   move with the acceptance module under `FLPR_ACCEPTANCE_NATIVE_TEST`.
   `FLPR_HANDSHAKE_NATIVE_TEST` keeps the production heartbeat/ready/health
   hooks; stress/hang seams move out.

## Shell ownership (after R8)

- `src/flpr_acceptance_shell.c` — parsing/printing/registration only.
  `cmd_flpr_ring_status` queries core + acceptance status; `cmd_flpr_ring_
  test` validates + calls `flpr_acceptance_test_run(_rate)`;
  `cmd_flpr_ring_stall_producer` → `flpr_acceptance_stall_producer`;
  `stall_flpr`/`stall_flpr_ms` → `flpr_acceptance_flpr_stall(_timed)`;
  `cmd_flpr_ring_acceptance` validates (count range `1–10,000,000`,
  rings-initialized pre-check, exact error strings/errnos) then calls
  `flpr_acceptance_run_gates`; `cmd_flpr_stress` pre-checks ready/acked
  (handshake) + `flpr_acceptance_stress_active()`, calls
  `flpr_acceptance_stress`, prints the summary; `cmd_flpr_hang` →
  `flpr_acceptance_send_fault_hang`.  The static `acceptance_buf`/
  `acceptance_rs` move into the acceptance module.  Output and errno
  returns byte-identical.
- `src/flpr_shell.c` — `cmd_flpr_status` gains the config-gated
  `flpr_acceptance_stress_snapshot(&s)` merge before printing the stress
  section.  `flpr status/offload/runtime/restart` otherwise unchanged.

## Test migration

### `tests/unit/flpr_ring_mgr` (stays direct, compiles real core)

Keeps: init/status/repeated-init/no-op-reinit, notify fields/counters,
local + coordinated reset (incl. generated-epoch-zero `-EINVAL`, send
failure `-EIO`, timeout, mismatch, exact match), old-epoch/matching-epoch
consumer notification, token drain at reset, epoch-zero produce
rejection, produce/consume validation (frames, null-PCM, CRC, wrap),
typed ASRC success/failure matrix (bad status/flags/state/frame-range/
CRC/transactional), latency accounting, wait_consume, remote-restarted
(consume + reset-ACK sem drained; the **stall-ACK-sem assert moves** to
the acceptance suite where the stall instance is registered), R1 barrier
tests, reset-token boundary + reset-side late-ACK retry.  Changes:
`test_remote_restarted_invalidates_and_requires_reset` drops the stall-ack
semaphore assert; `test_ack_token_boundary_overflow_and_remote_restart`
is split — reset half stays, stall half moves; `test_consume_asrc_empty_
and_stale` moves to the acceptance suite (its stale arrangement uses
`produce_stale_test`, now acceptance-owned); the suite's mock is updated
to the split registration API (reset/consumer slots only).

Moved out (to `tests/unit/flpr_acceptance`): `test_stall_*`,
`test_report_subtypes_update_status`, `test_ring_test_run_*`,
`test_wait_consume_*` stays (core `wait_consume`), `test_stale_slot_
advances`, `test_producer_stall_full_backpressure` (backpressure counting
now acceptance-owned via the note hook, but the produce-stall *return*
contract is still asserted here through the acceptance seam where
applicable — see acceptance suite).

### New `tests/unit/flpr_acceptance` (Twister child; gate 49 → 50)

Compiles **real** `src/flpr_acceptance.c`, `src/flpr_control_ack.c`,
`src/flpr_ring.c`, `src/flpr_cache.c`, `src/flpr_ring_mgr.c`
(`FLPR_RING_MGR_NATIVE_TEST` + `FLPR_ACCEPTANCE_NATIVE_TEST`), the shared
`flpr_ring_mgr_hooks.c` (reused via `../flpr_ring_mgr/src`), and a
suite-owned faithful handshake mock implementing the split registration
API (production reset/consumer capture + diagnostic handler capture +
send recording + auto-ACK + invoke helpers).  Tests (public behavior):

- test_run/test_run_rate: pre-init `-EAGAIN`, active `-EBUSY`, bounded
  success with arranged output slots, rate path, TEST_START/STOP wire
  messages exact, `test_active` cleared.
- Stall: persistent/timed packing wire values, zero-mask-with-duration
  and duration-overflow `-EINVAL`, send failure, ACK timeout, wrong ACK
  `-EIO`, exact ACK + `flpr_acceptance_flpr_stall_acked()`, token
  boundary `-EOVERFLOW` + remote-restart reset, stall transaction
  serializes against `remote_restarted` (ring_data_lock held through the
  ACK wait — barrier variant of the existing R1 pattern).
- Stale produce: `-EAGAIN` uninitialized, `-EINVAL` zero epoch, ESTALE
  consume advance + stale count.
- Report aggregation: all five subtypes exact field mapping + unknown
  subtype ignored.
- Producer stall: `flpr_acceptance_stall_producer(true)` → real core
  `flpr_ring_mgr_produce_block` returns FULL and `test_backpressure`
  increments; resume exact.
- Stress: match/stale/future/inactive PONG classification through the
  registered diagnostic handler, rejects-unavailable, rejects-active,
  clamps count, send-failure accounting (stress_err_send AND handshake
  err_send per the documented seam delta), timeout, late PONG.
- Fault hang: ACK success, send failure `-EIO`, ACK timeout `-ETIMEDOUT`.
- Remote-restart: stall-ACK sem drained + stall token reset to 1 (the
  assert moved from the ring_mgr suite).
- Gates: `flpr_acceptance_run_gates(1, …)` with mock auto-ACK and one
  arranged output slot → gate 1 passes; inject a send failure after gate
  1 so gate 2's coordinated reset fails fast → `ACCEPTANCE FAILED` +
  `-1`; a second call with rings clean exercises gate 6's empty check and
  the FAIL tail.  The all-gates-PASS tail remains hardware evidence
  (requires a live FLPR worker; documented — same honest boundary as
  today's shell test which keeps long-running acceptance on hardware).

### `tests/unit/flpr_handshake` (stays direct)

Keeps all production tests: init/register/bound, receive-path validation,
READY/duplicate/changed-epoch/ACK-failure, heartbeat echo + tx/rx
tracking + health transition callback, ring dispatch (updated to the
split registration: reset/consumer to the production slot, report/stall/
pong/hang to the diagnostic slot, NULL-diag silent drop), unregister,
disconnect/reconnect/waits, send_msg accounting, concurrent-status-read
validation counters.  Stress tests (`test_stress_*`) and fault-hang tests
(`test_fault_hang_*`) move to `tests/unit/flpr_acceptance` (they now
exercise the acceptance module through the real handshake routing — the
handshake suite keeps only the *routing* proof via the diagnostic slot).

### New `tests/unit/flpr_acceptance_flpr` (Twister child; gate 50 → 51)

Compiles **real** `src/flpr/acceptance.c` against a fake dependency table
(send recorder + wake counter) — no IPC, no DT, no RV32 glue.  prj.conf:
`CONFIG_ZTEST=y`, `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y`.  Tests:

- TEST_START sets active + zeroes counters; note hooks update counters;
  TEST_STOP emits the five reports with exact subtype packing and counter
  values; active cleared; START during active restarts counters.
- Stall persistent: flags set, ACK echoes exact packed value; timed:
  timer auto-clears after the duration (short 50 ms duration, bounded
  wait); `on_ring_reset` stops timer + clears flags + resets counters;
  `flpr_acceptance_stall_flags()` exact.
- STRESS_PING → PONG echo (seq + cookie exact).
- FAULT_HANG → ACK recorded **before** `hang_pending` visible; `wake`
  invoked; `flpr_acceptance_hang_pending()` true then reset by
  `on_ring_reset` (or dedicated reset) — ACK-before-spin ordering proven
  by send-record order vs pending read.
- Unknown message → `false` (main.c counts `err_unknown`).
- `test_active()` accessor true/false across START/STOP.

`src/flpr/main.c` remains hardware-only/excluded (no RISC-V simulator);
its production glue (READY/heartbeat/reset/producer/consumer/ring
processing/hang spin) is unchanged and proven by builds + hardware.

### `tests/unit/audio_shell_nrf54`

`fake_flpr_deps.c/.h` re-point to the `flpr_acceptance_*` surface:
`flpr_acceptance_stress`, `flpr_acceptance_stress_active`,
`flpr_acceptance_send_fault_hang`, `flpr_acceptance_test_run(_rate)`,
`flpr_acceptance_stall_producer`, `flpr_acceptance_flpr_stall(_timed)`,
`flpr_acceptance_flpr_stall_acked`, `flpr_acceptance_produce_stale_test`,
`flpr_acceptance_get_status`, `flpr_acceptance_stress_snapshot`,
`flpr_acceptance_run_gates`; `flpr_ring_mgr_get_status` stays (core
status).  All 42 output tests unchanged in their assertions; the fake
plumbing (and `test_flpr_set_*` controls) is extended for the acceptance
status struct and stress snapshot.

### Unchanged suites (must pass bit-exact)

`flpr_protocol`, `flpr_ring`, `flpr_audio_process`, `flpr_runtime`,
`audio_offload`, `offload_asrc`, `offload_asrc_verify` — untouched.
`tests/unit/flpr_protocol` still compiles `flpr_rate_limit_target_ms`
from `flpr_ring_mgr.h` (the inline stays).

## Wire / ABI proof

- `FLPR_PROTOCOL_VERSION` stays `4U`; `FLPR_RING_ABI_VERSION` stays `4U`;
  `FLPR_RING_MAGIC` unchanged; `FLPR_MSG_*` values unchanged;
  `struct flpr_msg` size/offsets unchanged (existing BUILD_ASSERTs);
  `FLPR_STALL_PACK/MASK/DURATION` macros and their compile-time asserts
  unchanged; report subtypes 0x00/0xD1/0xD2/0xD3/0xD4 unchanged;
  `RING_CONSUMER` seq/data contract unchanged; 480-input/481-capacity
  unchanged.  No source touched in `flpr_protocol.h` or `flpr_ring.h`
  except none.  `tests/unit/flpr_protocol` and `tests/unit/flpr_ring`
  pass byte-identical.  Optional added structural asserts are welcome
  only where they pin existing values (e.g. `BUILD_ASSERT` in
  `src/flpr/acceptance.c` on the subtype constants).

## Matrix / coverage

- `tests/test-matrix.json`:
  - `src/flpr_ring_mgr.c` — remove the moved APIs/outcomes; update
    witnesses; stateful stays.
  - `src/flpr_handshake.c` — remove `flpr_handshake_stress` /
    `flpr_handshake_send_fault_hang` outcomes; update
    `flpr_handshake_register_ring_handlers` outcome; add
    `flpr_handshake_register_diag_handlers`.
  - New `src/flpr_acceptance.c` — direct; public outcomes for every
    function (test_run, test_run_rate, stall producer, flpr_stall,
    flpr_stall_timed, stall_acked, produce_stale_test, get_status,
    init, stress, stress_active, stress_snapshot, send_fault_hang,
    run_gates, note hooks, test_active); stateful.
  - New `src/flpr_control_ack.c` — direct; outcomes for init/begin/arm/
    disarm/handle/wait/payload/register/reset_session.
  - New `src/flpr/acceptance.c` — **direct** (native suite exists);
    outcomes for init/handle_msg/on_ring_reset/hang_pending/test_active/
    stall_flags + every note hook.
  - `src/flpr_acceptance_shell.c` — stays direct via audio_shell_nrf54;
    outcomes/witnesses re-pointed to the new surface.
  - `src/flpr/main.c` — stays excluded_from_numeric (hardware-only
    FLPR firmware; reason updated to note acceptance handlers moved to
    `src/flpr/acceptance.c`, itself direct-tested).
- Coverage population: **30 → 33** (new `src/flpr_acceptance.c`,
  `src/flpr_control_ack.c`, `src/flpr/acceptance.c`).  Split migration
  rule: implementation/tests committed with the old baseline; candidate
  generated on the clean commit into `/tmp/r8-cov-candidate`; old
  `flpr_ring_mgr.c` (694/741 L, 230/326 B, 29/29 F) + `flpr_handshake.c`
  (389/401 L, 134/181 B, 17/17 F) aggregates compared against the
  replacement set (`flpr_ring_mgr.c` + `flpr_acceptance.c` +
  `flpr_control_ack.c` + slimmed `flpr_handshake.c`) and the shell split
  (`flpr_acceptance_shell.c` 150/328 L, 76/204 B, 10/10 F → thin shell +
  gate-bearing `flpr_acceptance.c`); no aggregate ratio decrease; every
  unchanged file ≥ its committed record; every function hit.  Commit the
  new baseline separately with the migration table in
  `docs/testing/coverage-matrix.md` and exact generated-commit provenance.
- `scripts/test-all.sh` comment inventory: 29 → **30** twister, 6
  exec-only comment line unchanged (no exec change), total children 49 →
  **51** (two new Twister children).  `scripts/test_inventory.py` picks
  both up automatically.

## G1 (canonical)

`./scripts/test-all.sh` → **51 PASS / 0 FAIL / 51 TOTAL** (30 twister + 5
exec-only + 12 Python + coverage + matrix + BSim Stage 1); coverage
population 33 on the migrated baseline; matrix 0 errors; BSim Stage 1
pins byte-identical (17 scenarios; mono 10 ms `0x22AB5C0D`, Mode A/B
10 ms `0xBAE24F7E`, reconnect fresh mono oracle,
`duplicate_release_10ms` total=56); `fw-build-5340`, `fw-build-54l15`,
`fw-build-dongle` all PASS with zero actionable warnings; build contract
**79/79**; `git diff --check` clean.

## Hardware (nRF54 only; planned non-destructive flash; no recovery)

Per the task's hardware section, run on the Xiao nRF54L15 with the
production acceptance-enabled build (config parity: resolved cpuapp
`CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y` + FLPR image
`CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y`):

1. Capture raw probe identity (`nrf-probes` DPIDR/AP/FICR) and console
   before reset; verify resolved configs on the flashed images.
2. Autonomous central (hci0 dongle ritual per AGENTS): Mode A 120 s and
   Mode B 120 s with offload `submit==success fallback=0` and
   timeout/full/stale/seq/frame/crc/payload/verify faults = 0.
3. `flpr hang` gate Mode A and Mode B — all checks incl. FAULT_HANG_ACK,
   single recovery, runtime_restart=1, new epochs, probation cleared,
   resumed success, zero verify/crc/seq/frame/state faults, zero
   decode/I2S faults, exhaustion=0.
4. `flpr ring stall_flpr_ms 1 60` stall gate (exact timed-ACK line +
   fallback-bump + recovery + no exhaustion).
5. Exercise `flpr status`, `flpr ring status`, `flpr stress` (bounded
   count), and `flpr ring test` (bounded count) to prove the moved
   diagnostic handlers and parser output.
6. Restore the normal current acceptance build (same config) and a clean
   boot.  Unique `/tmp/r8-*` MANIFEST + SHA256SUMS for all raw logs.
   No audibility claim.

## Commits (no push/PR/amend/force/attribution)

1. `docs: record R8 handoff — FLPR production/diagnostic boundary`
   (this document).
2. `refactor: split FLPR acceptance into cpuapp + FLPR image modules`
   — new `src/flpr_acceptance.c/.h`, `src/flpr/acceptance.c/.h`,
   `src/flpr_control_ack.c/.h`, `src/flpr_ring_mgr_internal.h`,
   `src/flpr/Kconfig` + prj.conf/CMake wiring, core slimming, handshake
   split, shell re-pointing, `main.c` boot wiring, new suites
   `flpr_acceptance` + `flpr_acceptance_flpr`, updated
   `flpr_ring_mgr`/`flpr_handshake`/`audio_shell_nrf54` suites, build
   contract 76→79 + fixtures, matrix updates.
3. `coverage: migrate baseline after R8 FLPR diagnostic split (30 -> 33)`
   — new `tests/coverage-baseline.json` + migration table in
   `docs/testing/coverage-matrix.md` + `scripts/test-all.sh` inventory
   comments.
4. `docs: accept R8 FLPR production/diagnostic boundary` — results doc,
   plan/STATUS/AGENTS/README/behavior-contract/build-contract
   references, gate + hardware evidence.

## Non-scope

Release-default-off policy; protocol/ABI/wire changes; R9 build
partition; 360-frame FLPR offload; BSim pin changes; E83 hardware rows;
destructive hardware actions; architecture expansion beyond the decided
shape.  Escalate only genuine blockers: two failed design attempts or
contradictory evidence, test weakening or copied production models,
unexplained coverage/hash/warning, hardware unavailable after safe
diagnosis, destructive action, architecture expansion.  Preserve
state/evidence/question; never commit a knowingly failing state.
