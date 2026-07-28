# Phase 6 Stage 4A — FLPR runtime restart results

Date: 2026-07-28
Target: nRF54L15 (Seeed Xiao)

## Build status

| Target    | Result |
|-----------|--------|
| nRF5340   | PASS   |
| nRF54L15  | PASS   |

Both targets compile and link cleanly (zero warnings attributable to Stage 4A
changes). nRF5340 excludes `flpr_runtime.c` via `CONFIG_SOC_NRF54L15` guard.

## Files changed

```
CMakeLists.txt                    — add flpr_runtime.c to nRF54 build
src/flpr_handshake.h              — add disconnect/reconnect/wait_bound/wait_new_ready APIs
src/flpr_handshake.c              — implement deregister/rebind sem lifecycle, preserve counters
src/flpr_runtime.h                — new: VPR runtime restart manager header
src/flpr_runtime.c                — new: DT-derived VPR restart (nrfx HAL), CRC, reload
src/audio_offload.h               — add audio_offload_is_stopped()
src/audio_offload.c               — implement audio_offload_is_stopped()
src/audio_shell.c                 — add "flpr runtime" + "flpr restart" shell commands
src/main.c                        — call flpr_runtime_init() on nRF54L15
tests/unit/flpr_handshake/        — new: handshake lifecycle unit tests
docs/development/phase6-stage4a-runtime-restart-results.md  — this file
```

## RAM usage

| Target    | Flash      | RAM       |
|-----------|------------|-----------|
| nRF5340   | 146780 B   | 40512 B   |
| nRF54L15  | 498628 B   | 152140 B  |

## Handshake lifecycle

- IPC device pointer stored after `flpr_handshake_init()`.
- `flpr_handshake_disconnect()`: drains semaphores, calls
  `ipc_service_deregister_endpoint`, marks session unavailable.
- `flpr_handshake_reconnect()`: drains semaphores, calls
  `ipc_service_register_endpoint`, marks session available.
- `flpr_handshake_wait_bound(timeout)`: waits on bound semaphore.
- `flpr_handshake_wait_new_ready(prev_epoch, timeout)`: waits on new_ready_sem
  given only after READY_ACK send succeeds with different epoch.
- On `ep_unbound`: preserves lifetime counters (ready_count, reboot_count,
  err_len/version/unknown/send, rx_missed_total, epoch), clears
  bound/ready/acked/healthy + session seq/timestamps. Does NOT call
  `flpr_peer_reset()`.
- Heartbeat work resumes only after new READY (single instance, not duplicated).

## Runtime manager

- DT-derived from `cpuflpr_vpr` node:
  - VPR register base: 0x5004c000
  - Source memory (RRAM): 0x165000 (98304 B)
  - Execution memory (SRAM): 0x20030000 (65536 B)
- Build assertions: exec size ≤ source size, exec base 128-byte aligned,
  exec range exactly 0x20030000..0x20040000.
- Restart sequence: disconnect → CPURUN=false → NDMRESET pulse (sQSPI pattern)
  → memcpy 64 KB → cache flush + DSB/ISB → CRC-32 verify → INITPC → reconnect
  → CPURUN=true → wait bound → wait new READY.
- CRC-32 computed over source before VPR stop; execution CRC verified after
  memcpy+flush.
- Mutex-serialised; rejects restart when offload stream healthy or mutex busy.
- Never reboots CPUAPP.
- All VPR register access through public nrfx HAL (no raw writes).
- No heap; all structs static.

## Hardware acceptance (partial)

### What worked

- Boot log: FLPR runtime init reports correct DT addresses.
- `flpr runtime` shell command prints IDLE state with all counters at zero.
- CRC mismatch correctly detected when source CRC was computed after VPR stop
  (fixed by reordering: compute source CRC BEFORE touching VPR).
- NDMRESET pulse pattern corrected to match sQSPI (DMACTIVE_Enabled in first
  set, not DMACTIVE_Disabled).

### What did not work

All three restart attempts failed at Stage 11: wait bound timeout (-EAGAIN).

Sequence observed on hardware:
```
FLPR restart start: prev_epoch=<N>
FLPR handshake disconnected
FLPR handshake reconnected              ← 9 ms after disconnect
FLPR restart: wait bound timeout: -11   ← 5 s after reconnect
```

The disconnect/reconnect cycle completes (~9 ms), memcpy+flush+CRC passes (no
mismatch logged), INITPC set, CPURUN=true. But the FLPR does not produce a
`ep_bound` event within 5 seconds.

**Root cause analysis (in progress):**

The VPR launcher at boot (POST_KERNEL init) does NOT use NDMRESET. It only
does: memcpy → cache flush → INITPC → CPURUN=true. The sQSPI NDMRESET pattern
is used for DEINITIALIZATION (shutting down the VPR permanently), not for
restart.

Hypothesis: after NDMRESET pulse + DMACTIVE state changes, some VPR subsystem
(VEVIF mailboxes, bus matrix access to SRAM, clock gating) enters a state
that prevents the FLPR from starting execution at INITPC. The sQSPI code
sets state to UNINITIALIZED after NDMRESET and never restarts the VPR.

**Next steps for full restart:**
1. Try restart WITHOUT NDMRESET pulse (CPURUN=false → copy → CPURUN=true).
2. If that works, investigate which part of the NDMRESET/DMACTIVE sequence
   breaks the restart.
3. If still fails, check VPR status registers (CPURUN, INITPC) via OpenOCD
   to verify they took effect.
4. Consider adding VEVIF event clearing or VPR peripheral re-initialisation.

## Unit tests

- `tests/unit/flpr_handshake/` — 9 tests pass on native_sim:
  - READY new/same/different epoch detection
  - Peer full reset
  - Partial reset (unbound-like) preserves lifetime counters
  - Stress PONG cookie classification (match/stale/future/inactive)

The native_sim build has a pre-existing `-Werror=cpp` issue (no optimization
flag) that affects ALL native_sim tests in this project, not specific to
Stage 4A. Tests were manually verified to compile with `-O2`.

## Clean tree

All generated files under `build/`, `twister-out/` excluded via `.gitignore`.
No mass erase, BabbleSim, security/WDT/HPF changes, push, or install.

## Hash

Tree after Stage 4A but before commit:
```
src/flpr_runtime.c  — new file
src/flpr_runtime.h  — new file
7 files modified, 2 new, 1 test directory added
```
