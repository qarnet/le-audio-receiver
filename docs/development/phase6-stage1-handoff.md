# Phase 6 Stage 1 — shared PCM rings

## Goal

Add cache-correct shared-memory SPSC PCM transport between CPUAPP and FLPR,
without routing live audio through it yet. Prove bit-exact wrap, ordering,
backpressure, reset epochs, and recovery under stress.

## Memory and ownership

- Existing ICMsg: 0x20028000..0x2002C000.
- Reserve existing 16 KiB gap 0x2002C000..0x20030000 for PCM rings; FLPR
  execution remains 0x20030000..0x20040000.
- Split into CPUAPP→FLPR and FLPR→CPUAPP SPSC rings, each 8 KiB.
- Four fixed slots per direction if exact metadata+480/481-frame payload sizes
  fit with 32-byte cache-line alignment; otherwise choose largest proven count.
- CPUAPP owns input write and output read indices. FLPR owns input read and
  output write indices. One writer per index; no cross-core atomics/locks.

## Protocol

- Versioned ring header: magic, ABI version, stream/reset epoch, slot count,
  slot payload capacity, producer index, consumer index, error counters.
- Slot metadata: sequence, epoch, valid stereo frames, correction ppm,
  flags, payload CRC32/check value for test mode.
- Use acquire/release ordering and explicit cache maintenance. CPUAPP has 32-byte
  D-cache: flush payload+metadata before publishing producer index; invalidate
  header/index before observing, then invalidate slot before reading. FLPR
  cache behavior must be verified; no-op maintenance only when generated config
  proves no cache.
- Never use C11 atomics requiring RV32 A extension. Use volatile naturally
  aligned indices, barriers, and cache operations.
- ICMsg/VEVIF carries control only: ring reset/epoch, test start/stop, producer
  notification, fault report. No PCM payload copied through IPC Service.

## Implementation

- Pure ring core module shared by CPUAPP/FLPR with platform cache hooks.
- CPUAPP shell: `flpr ring-status`, `flpr ring-test <blocks>`, and controlled
  producer/consumer stall injection.
- FLPR loop consumes input slots, verifies metadata/CRC/sequence, copies valid
  payload bit-exact into output slot, publishes result, and notifies CPUAPP.
- Reset handshake clears rings only after both sides agree on new nonzero epoch.
  Stale slots from old epoch rejected/counted.
- Live audio remains cpuapp ASRC→I2S for Stage 1.

## Tests and gates

- Native unit tests: empty/full, wrap, exact capacity, sequence wrap, producer
  full, consumer empty, canaries, invalid frames, bad magic/version/epoch/CRC,
  stale slot, reset during pending data, forced producer/consumer stalls,
  barrier/cache-hook order via test hooks.
- Both builds and all units pass; nRF5340 excludes ring/FLPR code.
- Generated DTS/map proves 0x2002C000..0x20030000 reserved and no overlap.
- Flash both images; 100,000 synthetic 480-frame stereo blocks through roundtrip
  ring, exact sequence/CRC/payload, zero loss/corruption, wraps exercised.
- Forced stalls produce counted FULL/EMPTY/backpressure, no overwrite/corruption;
  reset recovers and rejects stale epoch.
- 60 s Mode A current CPUAPP ASRC stream remains zero-fault while ring transport
  idle, then while low-rate synthetic ring test runs if CPU budget permits.
- Record latency distribution and throughput; no subjective audio claim.

## Non-scope

- No live audio routing, identity offload, ASRC on FLPR, HPF/ICBmsg, BabbleSim,
  package install, direct RADIO, destructive recovery, push/release.
