# Phase 6 Stage 4B — live FLPR hang, fallback, restart, resume

## Goal

Prove real FLPR core hang during Mode A/B causes immediate cpuapp ASRC fallback,
runtime VPR restart, fresh IPC/ring epoch, and exact FLPR ASRC resume without BLE,
I2S, or CPUAPP reset.

## Fault injection

- Bump wire protocol version for new messages.
- Add `FAULT_HANG` and `FAULT_HANG_ACK` control messages.
- FLPR callback sends ACK, atomically sets pending flag, wakes main. Main then
  disables interrupts and spins forever. This halts ring + heartbeat; ACK must
  be observed before hang. No WDT, fault exception, or CPUAPP reset.
- CPUAPP API waits exact ACK with timeout. Shell `flpr hang` reports ACK only;
  normal output timeout triggers recovery. Keep local-UART test hook documented.

## Ring restart API

Add explicit `flpr_ring_mgr_remote_restarted()` callable only while offload is
RECOVERING/stopped. It invalidates local epoch, drains consumer/reset/stall
semaphores, reinitializes shared headers and handlers after new READY. Follow it
with coordinated nonzero epoch reset. No active submit may race it.

## Offload recovery integration

- Runtime manager itself has no audio-active guard. Shell idle restart retains
  guard externally. Runtime mutex prevents concurrent restart.
- Recovery worker algorithm:
  1. If handshake healthy, try coordinated ring reset with 100 ms ACK timeout.
  2. On unhealthy handshake or reset timeout/error, call runtime restart with
     1500 ms bound/READY budget.
  3. After restart success: ring remote-restart reinit, then coordinated reset
     with 100 ms timeout.
  4. Transition ACTIVE + probation only after all steps pass.
  5. Existing cumulative five-attempt/backoff/exhaustion policy remains.
- First timed-out audio block returns with caller output untouched; I2S path runs
  cpuapp ASRC from unchanged state. Every fallback block advances cpuapp state.
  First resumed FLPR request carries latest state, requiring no hidden sync.
- Add runtime restart count/failure/duration and remote epoch to offload shell.

## Heartbeat supervisor

- Handshake exposes health-transition callback invoked outside lock only on
  healthy→unhealthy.
- Runtime supervisor work:
  - active/PREPARING offload: call public
    `audio_offload_remote_unavailable()` which atomically enters RECOVERING once
    and schedules recovery;
  - stopped offload: runtime-restart directly.
- Output timeout normally detects live hang before 1 Hz heartbeat threshold;
  duplicate notification must not schedule second restart.

## Tests

- ACK-before-hang protocol and exact version.
- Recovery ordering: short ring reset first; timeout→runtime restart→ring reinit
  →epoch reset→ACTIVE/probation.
- Runtime failure/backoff/exhaustion; concurrent heartbeat+audio fault dedup.
- Output/result untouched on first timeout; cpu fallback advances state once.
- FLPR→cpu fallback N blocks→resumed FLPR exact output/state continuity against
  uninterrupted host reference.
- New epoch rejects all stale pre-hang notifications/slots.
- Idle heartbeat hang auto-restarts without CPUAPP reboot.
- Existing runtime/ring/offload/ASRC tests pass.

## Hardware acceptance

Verification build with ASRC shadow enabled:

1. Idle `flpr hang`: ACK, heartbeat unhealthy, automatic restart ≤6 s, new
   epoch, healthy heartbeat, stress 1,000 passes, CPUAPP uptime continuous.
2. Mode A 180 s: inject hang after ACTIVE/success>1000. Require central 18,000
   frames/100 fps; timeout/fallback; runtime restart; ACTIVE; probation cleared;
   resumed success; verify/state/CRC/seq faults zero; I2S/decode/push errors zero.
3. True Mode B 180 s, same gates.

Record exact fallback block count and first-fault→ACTIVE recovery latency. Runtime
restart expected near Stage4A 219 ms; no assumed bound beyond 1.5 s.

Then normal production build (shadow n): Mode A 300 s with one hang; same gates.
Rebuild nRF5340 clean. Commit code/tests/docs/results only on pass. No raw
registers, WDT expiry, security changes, HPF, BabbleSim, mass erase, push,
install, or analog claim.
