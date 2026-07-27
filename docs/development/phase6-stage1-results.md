# Phase 6 Stage 1 — Results (partial)

**Date**: 2026-07-27  
**Status**: Partial — ring infrastructure built and validated. FLPR integration roundtrip test not yet executed.

## Architecture

### Cache finding
nRF54L15 ICACHE/CACHEDATA peripheral only caches NVM (flash/RRAM) accesses over
the C-AHB (code) bus. SRAM at 0x2xxxxxxx is on S-AHB system bus — NOT cached.
Writes are write-around (bypass cache + invalidate line). Only `__DSB()` data
synchronization barriers needed for multi-core ordering, not explicit D-cache
maintenance. FLPR has no cache at all.

Evidence: `CONFIG_DCACHE` is not set in CPUAPP `.config`. Cache driver only
provides I-cache functions. Nordic datasheet confirms: "Both instruction and
data accesses towards NVM memory are cached."

### Ring layout
Two 8 KiB SPSC rings at reserved gap:
- CPUAPP→FLPR input: 0x2002C000
- FLPR→CPUAPP output: 0x2002E000

Four slots per direction. Each slot: 32-byte metadata + 1920-byte PCM payload
(480 stereo frames × 2 channels × 16-bit), padded to 2016 bytes (63 × 32).

Ring header: 128 bytes (magic 0x52494E47, ABI version 1, epoch, indices, error
counters).

### Protocol
- Pure SPSC ring core (`flpr_ring.h` / `flpr_ring.c`): no Zephyr deps, unit-testable
- Cache/barrier hooks (`flpr_cache.c`): per-platform __DSB barriers or no-ops
- CPUAPP manager (`flpr_ring_mgr.h` / `flpr_ring_mgr.c`): init, reset, produce, consume, test
- IPC control messages (Stage 0 protocol extended): RING_RESET, RING_RESET_ACK,
  RING_TEST_START, RING_TEST_STOP, RING_TEST_REPORT, RING_PRODUCER, RING_CONSUMER
- FLPR consumer loop: polls input ring, verifies metadata/CRC/seq, copies
  bit-exact to output ring, publishes. Reset handshake clears rings.

### Shell commands
```
flpr ring status   — PCM ring status (producer/consumer/epoch/initialized)
flpr ring init     — Initialize PCM rings in shared memory
flpr ring reset    — Reset rings with new epoch (non-zero)
flpr ring test <N> — Run ring throughput test (N blocks, default 10k)
```

## Build

| Target | Status | FLASH | RAM |
|--------|--------|-------|-----|
| nRF54L15 cpuapp | PASS | 473,736 B / 1428 KB | 141,732 B / 160 KB (86.51%) |
| nRF54L15 flpr | PASS | 28,568 B / 96 KB | 36,416 B / 64 KB (55.57%) |
| nRF5340 | PASS | 363,952 B / 1008 KB | 136,456 B / 448 KB |

Zero new warnings. nRF5340 has zero FLPR/ring impact.

## Memory map

| Region | Start | End | Size | Owner |
|--------|-------|-----|------|-------|
| cpuapp SRAM | 0x20000000 | 0x20028000 | 160 KB | CPUAPP linker (_end=0x200229a4) |
| shared IPC rx | 0x20028000 | 0x2002A000 | 8 KB | ICMsg (reserved) |
| shared IPC tx | 0x2002A000 | 0x2002C000 | 8 KB | ICMsg (reserved) |
| **shared PCM ring** | **0x2002C000** | **0x20030000** | **16 KB** | **SPSC rings (reserved, Stage 1)** |
| FLPR SRAM | 0x20030000 | 0x20040000 | 64 KB | FLPR linker |

No overlap verified via generated `zephyr.map` and `zephyr.dts`.

## Unit tests

| Suite | Tests | Pass | Fail |
|-------|-------|------|------|
| flpr_ring | **23** | **23** | **0** |
| flpr_protocol | 40 | 40 | 0 |
| lifecycle | 13 | 13 | 0 |
| decode | 6 | 6 | 0 |
| rate_convert | 10 | 10 | 0 |
| timing | 19 | 19 | 0 |
| asrc | 20 | 20 | 0 |
| perf | 19 | 19 | 0 |
| actuator | 7 | 7 | 0 |
| drift | 18 | 18 | 0 |
| **Total** | **175** | **175** | **0** |

Ring suite covers: init/magic/validation, produce/consume, produce-full,
consume-empty, stale-epoch rejection, wrap-around (10 batches of 2),
CRC-32 (known vector + different data + zero-length), reset-epoch/reject-zero/
reject-same, slot arithmetic/bounds, slot isolation (no clobber), and
two-ring independence.

## Hardware verification

### Shell commands working
```
uart:~$ flpr ring status
--- FLPR PCM rings ---
  Initialized   : no
  Epoch         : 0
  Input  (→FLPR): prod=0 cons=0 epoch=0
  Output (→CPU): prod=0 cons=0 epoch=0
```

### 60 s Mode A stream (ring idle) — PASS
```
Done: 6000 frames in 60.00 s (100.0 fps)
```
Zero faults, zero disconnects, zero warnings during stream with ring
infrastructure compiled in. CPUAPP audio path unchanged.

## Known gaps (Stage 1.1)

1. **FLPR ring roundtrip test not yet executed**: `flpr_ring_mgr_test_run()`
   produces blocks into input ring but does not send IPC `RING_PRODUCER`
   notifications. FLPR polls input ring every 10 ms, but test loop fills 4
   slots immediately and stalls on full ring. Need IPC notification path.
2. **Reset handshake not coordinated**: CPUAPP `ring reset` only affects local
   rings. Need `FLPR_MSG_RING_RESET` IPC to FLPR for coordinated reset.
3. **Shell responsiveness during BLE stream**: Stream callbacks block shell
   processing (main thread). Ring commands work between sessions.
4. **Stall injection not implemented**: Producer/consumer stall counters exist
   but no explicit stall injection shell command.

## Files

| File | Status | Purpose |
|------|--------|---------|
| `src/flpr_ring.h` | New | Ring protocol: header, slot, CRC, SPSC helpers |
| `src/flpr_ring.c` | New | Pure ring core: init/produce/consume/reset/CRC |
| `src/flpr_cache.c` | New | Per-platform cache/barrier hooks (__DSB or no-op) |
| `src/flpr_ring_mgr.h` | New | CPUAPP ring manager API |
| `src/flpr_ring_mgr.c` | New | CPUAPP ring manager: init/test/produce/consume |
| `src/flpr/main.c` | Modified | FLPR: ring init + consumer loop + IPC ring commands |
| `src/flpr_protocol.h` | Modified | Added 7 ring-control message types |
| `src/audio_shell.c` | Modified | Added `flpr ring *` shell subcommands |
| `CMakeLists.txt` | Modified | Added ring/mgr/cache sources (nRF54L15 only) |
| `src/flpr/CMakeLists.txt` | Modified | Added ring/cache sources to FLPR build |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Modified | Reserved 0x2002C000..0x20030000 for pcm_ring |
| `src/flpr/boards/nrf54l15dk_nrf54l15_cpuflpr.overlay` | Modified | Mirrored pcm_ring reservation |
| `tests/unit/flpr_ring/` | New | 23 unit tests for ring module |

## Next steps (Stage 1.1)

1. Wire IPC `RING_PRODUCER` / `RING_CONSUMER` notifications in test manager
2. Implement coordinated reset handshake (CPUAPP→FLPR IPC)
3. Run 100,000-block ring roundtrip with CRC verification
4. Run forced stall tests
5. Run 60s stream with low-rate ring test active
