# Stage 2 deterministic FLPR timed-stall gate

## Goal

Replace racy host-issued stall-clear timing with FLPR-local timed fault
injection. Production audio/recovery behavior stays unchanged.

## Exact implementation

### Protocol

- Keep `FLPR_MSG_RING_STALL` and `FLPR_MSG_RING_STALL_ACK` IDs.
- Pack `data`: bits `[7:0]` stall mask; bits `[31:8]` duration milliseconds.
  Duration zero means persistent behavior for existing tests. Maximum duration
  `0x00ffffff` ms; reject zero bits with nonzero duration and out-of-range shell
  input.
- Add pack/unpack macros and compile-time assertions in `flpr_protocol.h`.

### FLPR

- Replace two shared plain `bool` stall flags with one `atomic_t` bitmask.
- Add `k_timer`. On timed-stall command: atomically set bits, start one-shot
  timer for duration, ACK packed value. On timer expiry: atomically clear all
  stall bits and `k_sem_give(&ring_wake_sem)` so queued input drains even if no
  later producer notification arrives.
- Persistent command stops timer before atomically applying mask.
- Ring worker reads one atomic snapshot per decision.
- Add diagnostics: timed stall start count, expiry count, current mask. No log
  call required from timer ISR.

### CPUAPP manager/shell

- Add `flpr_ring_mgr_flpr_stall_timed(bits, duration_ms, timeout_ms)`; existing
  API calls shared helper with duration zero.
- Stall ACK handler must retain packed ACK data; caller verifies exact requested
  packed value, not merely semaphore arrival.
- Add shell command:
  `flpr ring stall_flpr_ms <bits> <duration_ms>`.
- Status prints current requested/acked timed-stall diagnostics.

### Gate script

- Replace external on/wait/off flow with one
  `flpr ring stall_flpr_ms 1 60` command.
- Require exact ACK for mask 1/duration 60.
- Then observe at least one fault/fallback, recovery attempt >=1, ACTIVE,
  exhaustion=0, probation cleared>=1, and >=100 successes after recovery.
- Never send persistent clear as part of timed gate.

## Tests

- Protocol pack/unpack boundaries.
- Persistent stall stops prior timer.
- Timed expiry clears atomically and wakes worker.
- ACK mismatch rejects request.
- Gate parser success, no fault, no expiry/recovery, exhaustion, timeout.
- Existing Stage 1 persistent stall tests remain unchanged.

## Hardware

SC-only normal BlueZ pairing. Mode A >=90 s. Run timed 60 ms gate after ACTIVE
and success>500. Require 100 fps and zero I2S/decode/push/ASRC faults.

Build/test both targets; update Stage 2 results; commit. No security changes,
recovery-policy changes, raw preconnect, mass erase, push, or analog claim.
