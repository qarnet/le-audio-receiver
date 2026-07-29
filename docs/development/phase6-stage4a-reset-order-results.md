# Stage 4A reset-order fix — acceptance results

**Executed:** 2026-07-29
**Firmware hash:** to be captured from git

## Goal

Replace NDMRESET pulse/shutdown with single-variable DMCONTROL sequence:
assert reset+DMACTIVE enabled, prepare/rebind/cpurun while held,
release reset last with DMACTIVE still enabled.  Never write DMACTIVE
disabled in runtime restart.

## DMCONTROL mask semantics

| Mask               | NDMRESET (bit 1) | DMACTIVE (bit 0) | Hex     |
|---------------------|-------------------|-------------------|---------|
| RESET_ASSERT        | 1 (Active)       | 1 (Enabled)       | 0x03   |
| RESET_RELEASE       | 0 (Inactive)     | 1 (Enabled)       | 0x01   |

DMACTIVE bit = 1 in both masks.  Never zero/Disabled.

## Readbacks from hardware (all 3 restarts identical)

```
  DMCONTROL      : after_assert=0x00000003 before_release=0x00000003 after_release=0x00000001
  INITPC         : after_set=0x20030000
  CPURUN         : after_assert=0 after_set=1
```

- DMACTIVE = 1 (Enabled) in all three snapshots ✅
- NDMRESET = 1 in after_assert and before_release, = 0 in after_release ✅
- INITPC = 0x20030000 (execution base) ✅
- CPURUN = 0 after assert (stopped), = 1 after set (armed) ✅

## Restart tests (nRF54L15 hardware)

| #  | Epoch before      | Epoch after       | CRC        | Duration | Result |
|----|-------------------|-------------------|------------|----------|--------|
| 1  | 4077721384       | 4111087418        | 0x33a57974 | 219 ms   | PASS   |
| 2  | 49843930         | 77782201          | 0x33a57974 | 219 ms   | PASS   |
| 3  | 77782201         | 100078037         | 0x33a57974 | 219 ms   | PASS   |

All three restarts: epoch changes (new FLPR boot detected), CRC matches
(0x33a57974 source=execution), duration consistent at 219 ms.

## Stress test (after restart #1)

```
Sent=10000 Recv=10000 Timeout=0 Stale=0 Mismatch=0 ErrSend=0
```

10k ping/pong: zero faults.

## Mode A 60 s streaming (after restart #3)

```
Stream mode: stereo_a, SDU size: 120 bytes
Streaming 1000 Hz sine for 60 s...
Done: 6000 frames in 60.00 s (100.0 fps)
```

Audio offload stream active through entire test.  I2S DMA stable.
PCLK timer diag: ~2000–2359 ppm (normal nRF54L15 drift range).

## Unit tests (native_sim)

```
SUITE PASS - 100.00% [flpr_reset_order]: pass = 27, fail = 0, skip = 0, total = 27
```

27/27 tests pass covering:
- Mask constant validation (DMACTIVE=Enabled in both, fields distinct,
  only NDMRESET differs between assert and release)
- Stage enum ordering (assert → copy → flush → CRC → initpc → reconnect →
  cpurun → release → wait bound → wait ready → success)
- Mocked DMCONTROL write sequence (exactly 2 writes, correct order,
  DMACTIVE never zero in any mask)
- Readback struct layout (all 6 fields present, ≤256 bytes)
- State transition validation (IDLE→BUSY→IDLE, UNAVAILABLE on failure)
- Failure stage distinctness (all 13 stages unique)

## Files changed

- `src/flpr_runtime.h` — Added `flpr_runtime_stage` enum (13 stages),
  `flpr_runtime_readbacks` struct (6 HW register snapshots), extended
  `flpr_runtime_status` with `failed_stage` and `readbacks` fields.
- `src/flpr_runtime.c` — Replaced two-variable DMCONTROL mask (sQSPI
  pulse-and-disable) with single-variable sequence: RESET_ASSERT →
  prepare/copy/flush/CRC/initpc/reconnect/cpurun while held →
  RESET_RELEASE (DMACTIVE still Enabled).  Added readback snapshots
  after assert, before release, after release.  Never writes
  DMACTIVE=Disabled.
- `src/audio_shell.c` — `cmd_flpr_runtime_status` now prints failed stage
  (human-readable name), DMCONTROL readback values, INITPC, CPURUN.
- `tests/unit/flpr_runtime/` — New test module with 27 tests:
  mask constants, stage ordering, mocked DMCONTROL sequence, readback
  validation, state transitions, failure stage identification.
