# Phase 6 Stage 2 — live identity offload

## Goal

Route live decoded 480-frame stereo PCM through CPUAPP→FLPR→CPUAPP rings, with
FLPR bit-exact identity copy, then run existing CPUAPP ASRC and I2S. Prove live
transport/lifecycle. Preserve CPUAPP ASRC fallback on timeout/fault.

## Architecture

- Add `audio_offload` interface between decoded/volume-adjusted PCM and
  `audio_sink_push` resampling stage.
- nRF54 path: submit one 480-frame block with stream epoch + sequence + current
  correction metadata; notify FLPR; wait or pipeline for matching output under a
  bounded deadline derived from measured ~5.3 ms ring RTT and 10 ms SDU budget.
- CPUAPP ASRC still performs conversion/correction after identity output.
- nRF5340 compile-time bypass: direct PCM, no FLPR/ring code.
- On FLPR unhealthy, epoch mismatch, ring full, timeout, wrong sequence/frames,
  payload/CRC fault: count fault and process original input through CPUAPP ASRC.
  No drop, duplicate, or stale output. Recovery may re-enable offload only after
  clean ring reset/epoch handshake.
- Never wait while holding spinlock or from ISR. Record execution context and
  prove bounded callback deadline. If synchronous RTT breaks Mode B margin,
  implement one-block pipeline/dedicated work queue with explicit latency rather
  than widening deadline.

## Lifecycle

- New stream opens new ring epoch before first live submit.
- Two-ASE Mode A opens once; sequence increments per rendered stereo block.
- Stop/disconnect cancels wait, rejects late output, resets epoch, preserves
  configured I2S state.
- Reconnect starts clean sequence/epoch. FLPR reboot/unbound triggers fallback.

## Instrumentation

- Offload submit, success, timeout, full, stale, bad-seq/frame/payload/CRC,
  fallback, recovery counters.
- Wait/RTT min/avg/max and full sink/callback deadline.
- Shell reports active/bypassed/fallback state and current epoch/sequence.

## Tests and gates

- Native tests with mock transport: normal, delayed, timeout, out-of-order,
  duplicate, stale epoch, wrong frame count, corruption, ring full, FLPR reset,
  stop during wait, reconnect, sequence wrap, fallback exact input.
- Existing 101 ring/protocol + all audio units pass; both builds clean.
- nRF54 hardware:
  - Mode A 10 minutes and true Mode B 10 minutes through identity offload;
  - exact submit/success counts, zero fallback/fault in normal runs;
  - no I2S underrun/repeat/push/capacity/decode faults;
  - callback max <10 ms with measured margin;
  - forced FLPR stall/reset invokes CPUAPP ASRC fallback, stream continues, then
    clean recovery/re-offload after reset handshake;
  - disconnect/reconnect test.
- nRF5340 build and hardware stream if board present.

## Non-scope

- FLPR ASRC, removal of CPUAPP ASRC fallback, HPF/ICBmsg, BabbleSim, direct
  RADIO, destructive recovery, package install, push/release.
